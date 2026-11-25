#pragma once
// Bridges the host audio thread (producer) and the spatial output thread (consumer).
//  - lock-free ring buffer between the two threads
//  - sample-rate conversion host rate -> output rate (1:1 at 48 kHz, plus tiny drift corrections)
//  - clock-drift compensation: tiny ratio adjustments keep the buffer at its target fill
//  - three latency modes (Music / Low / Ultra), priming, underrun detection, latency statistics

#include "RingBuffer.h"
#include "SincResampler.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <vector>
#include <algorithm>

namespace ab
{
class OutputFeeder
{
public:
    enum class State : int { Idle = 0, Priming = 1, Running = 2 };

    // Safety margin on top of one host block + one output period, per latency mode.
    static constexpr double kMusicMarginMs   = 100.0;
    static constexpr double kLowMarginMs     = 8.0;
    static constexpr double kLowMaxExtraMs   = 60.0;  // Low mode grows by 10 ms per dropout, up to +60 ms
    static constexpr double kUltraMarginMs   = 4.5;

    // Adaptive margin (direct loopback input: Low latency and Ultra low latency+). Instead of a fixed margin,
    // the margin follows the measured lateness ("jitter") of the input packets so the output never runs dry.
    //  - first 2 minutes: 15 ms while the jitter of this PC is measured
    //  - then the margin goes straight to what the measurement needs; a near miss or a later-than-ever packet
    //    raises it at once and the raised value is kept for 10 minutes
    //  - every change is made by a tiny speed change (max 0.3 %, 0.5 % while refilling), never by skipping
    //    or inserting audio
    //  - if the input still runs dry, the gap is concealed (the last period mirrored and faded) instead of
    //    a silent gap and a re-prime, and the margin grows by the shortfall
    static constexpr double kAdaptMinMs   = 1.0;
    static constexpr double kAdaptMaxMs   = 25.0;
    static constexpr double kNearMissMs   = 0.75;
    static constexpr int    kJitBuckets   = 10;     // 10 x 60 s

    // Two policies for the direct loopback input:
    //  Safe (Low latency):      margin = worst lateness of the last 10 minutes + 1.5 ms.
    //  Fast (Ultra low latency+): phase-aware. A late packet only empties the buffer if the output pass that
    //      needs it comes before the packet. With phase p = time from a packet's nominal arrival to the next
    //      output pass, the margin only has to cover the lateness where the phase makes it necessary:
    //          margin = largest (p + k * period) below the worst lateness, + 1 ms guard
    //      The phase moves only as fast as the two clocks drift apart (tens of ppm); it is tracked
    //      continuously and the margin is raised ahead of time (phases up to 1.5 ms earlier are covered).
    //  Stable (host app, Default mode): like Safe with a generous floor (20..60 ms, + 3 ms safety).
    enum class AdaptPolicy : int { Safe = 0, Fast = 1, Stable = 2 };
    struct PolicyParams { double startMs, addMs, holdS, settleS; bool phaseAware; double minMs = kAdaptMinMs, maxMs = kAdaptMaxMs; };
    static PolicyParams paramsFor (AdaptPolicy p) noexcept
    {
        if (p == AdaptPolicy::Fast)   return { 15.0, 1.0, 600.0, 120.0, true };
        if (p == AdaptPolicy::Stable) return { 30.0, 3.0, 600.0, 60.0, false, 20.0, 60.0 };
        return { 15.0, 1.5, 600.0, 120.0, false };
    }

    // Margin needed for lateness j at phase p (all ms), looking lookMs ahead (phase moving to lower values,
    // wrapping around the packet period).
    static double phaseMarginMs (double jMs, double pMs, double periodMs, double guardMs, double lookMs) noexcept
    {
        double need = 0.0;
        for (double d = 0.0; d <= lookMs + 1e-9; d += 0.25)
        {
            double q = std::fmod (pMs - d, periodMs);
            if (q < 0.0) q += periodMs;
            for (double v = q; v < jMs; v += periodMs) need = std::max (need, v + guardMs);
        }
        return need;
    }

    explicit OutputFeeder (int numChannels) : numCh (numChannels)
    {
        ring.init (numCh, 1 << 16);
        resampler.setup (numCh, 1 << 16);
        lastOut.assign ((size_t) numCh, std::vector<float> (8192, 0.0f));
        for (auto& b : jitBucket) b.store (0.0f);
    }

    //==================================================== producer (host audio thread)
    // Call from prepareToPlay (not real-time critical).
    void producerPrepare (double hostRate, int hostBlockSize) noexcept
    {
        hostSampleRate.store (hostRate > 0 ? hostRate : 48000.0);
        hostBlock.store (std::max (1, hostBlockSize));
    }

    void producerPush (const float* const* channels, int numFrames, int64_t nowUs = nowMicros()) noexcept
    {
        // Bursty delivery detection: blocks arriving much faster than real time.
        const int64_t prev = lastPushUs.exchange (nowUs, std::memory_order_relaxed);
        const double blockUs = (double) numFrames / hostSampleRate.load (std::memory_order_relaxed) * 1.0e6;
        const bool backToBack = prev > 0 && (double) (nowUs - prev) < 0.25 * blockUs;
        if (backToBack)
            fastPushes.fetch_add (1, std::memory_order_relaxed);

        // A host that processes the same block twice in a row would make every block play twice:
        // detect identical, back-to-back, non-silent blocks and drop the copy.
        // (Not with the direct loopback input: after a stall its packets arrive back-to-back by design.)
        if (! adaptiveReq.load (std::memory_order_relaxed))
        {
            uint64_t h = 1469598103934665603ull;
            float energy = 0.0f;
            const int nCh = std::min (numCh, 4);
            for (int c = 0; c < nCh; ++c)
                for (int i = 0; i < numFrames; ++i)
                {
                    uint32_t bits;
                    std::memcpy (&bits, &channels[c][i], 4);
                    h = (h ^ bits) * 1099511628211ull;
                    energy += channels[c][i] * channels[c][i];
                }
            const bool duplicate = energy > 1.0e-9f && h == lastBlockHash && numFrames == lastBlockFrames && backToBack;
            lastBlockHash = h;
            lastBlockFrames = numFrames;
            if (duplicate)
            {
                duplicates.fetch_add (1, std::memory_order_relaxed);
                return;
            }
        }

        statPushes.fetch_add (1, std::memory_order_relaxed);
        lastHostBlock.store (numFrames, std::memory_order_relaxed);
        if (ring.write (channels, numFrames) < numFrames)
            overflowCount.fetch_add (1, std::memory_order_relaxed);
    }

    // Producer: lateness of the packet just pushed (ms). The constant part is removed (slowly rising minimum),
    // what remains is the jitter; the worst value per minute is kept for 10 minutes.
    void producerReportLateness (double lateMs) noexcept
    {
        if (latenessReset.exchange (false, std::memory_order_acquire)) { minLate = 1e9; curBucket = -1; }
        if (lateMs < minLate) minLate = lateMs;
        else minLate += 0.0005;   // follows a rising baseline (0.05 ms/s at 100 packets/s)
        const float jitter = (float) std::max (0.0, lateMs - minLate);
        lastJitterMs.store (jitter, std::memory_order_relaxed);
        const int b = (int) ((nowMicros() / 60000000) % kJitBuckets);
        if (b != curBucket)
        {
            curBucket = b;
            jitBucket[(size_t) b].store (0.0f, std::memory_order_relaxed);
        }
        auto& slot = jitBucket[(size_t) b];
        float prev = slot.load (std::memory_order_relaxed);
        while (jitter > prev && ! slot.compare_exchange_weak (prev, jitter, std::memory_order_relaxed)) {}
    }

    //==================================================== settings (any thread)
    void setAdaptive (bool on, AdaptPolicy policy = AdaptPolicy::Safe) noexcept
    {
        adaptPolicy.store ((int) policy, std::memory_order_relaxed);
        adaptiveReq.store (on, std::memory_order_relaxed);
        adaptiveChanged.store (true, std::memory_order_release);
        if (on)
        {
            for (auto& b : jitBucket) b.store (0.0f, std::memory_order_relaxed);
            latenessReset.store (true, std::memory_order_release);
        }
    }
    AdaptPolicy getAdaptPolicy() const noexcept { return (AdaptPolicy) adaptPolicy.load(); }
    double getPhaseMs() const noexcept { return phaseMs.load(); }
    bool isAdaptive() const noexcept { return adaptiveOn.load (std::memory_order_relaxed); }
    double getSettleRemainingSec (int64_t now = nowMicros()) const noexcept
    {
        if (! adaptiveOn.load()) return 0.0;
        return std::max (0.0, (double) (settleUntilUs.load() - now) * 1e-6);
    }
    double getMaxJitterMs() const noexcept
    {
        float m = 0.0f;
        for (auto& b : jitBucket) m = std::max (m, b.load (std::memory_order_relaxed));
        return m;
    }
    uint32_t getConcealed() const noexcept { return concealed.load(); }

    void setMode (int newMode) noexcept
    {
        newMode = std::clamp (newMode, 0, 2);
        if (mode.exchange (newMode, std::memory_order_relaxed) != newMode)
            modeChanged.store (true, std::memory_order_release);
    }
    int getMode() const noexcept { return mode.load (std::memory_order_relaxed); }

    // Current safety margin in ms (Low mode includes the automatic growth after dropouts).
    double getMarginMs() const noexcept
    {
        if (adaptiveOn.load (std::memory_order_relaxed)) return adaptMarginMs.load (std::memory_order_relaxed);
        switch (mode.load (std::memory_order_relaxed))
        {
            case 0:  return kMusicMarginMs;
            case 1:  return kLowMarginMs + lowExtraMs.load (std::memory_order_relaxed);
            default: return kUltraMarginMs;
        }
    }

    //==================================================== consumer (spatial output thread)
    // Call when the output stream (re)starts.
    void consumerStart (double outputRate) noexcept
    {
        outRate = outputRate > 0 ? outputRate : 48000.0;
        primed = false;
        smoothFill = -1.0;
        integ = 0.0;
        resampler.reset();
        state.store ((int) State::Priming);
    }

    // Fills out[c][0..numOut). Always writes numOut frames (silence when not running).
    void render (float* const* out, int numOut, int64_t nowUs = nowMicros()) noexcept
    {
        const double hr = hostSampleRate.load();
        const double baseRatio = hr / outRate;
        updateCutoff (baseRatio);

        if (adaptiveChanged.exchange (false, std::memory_order_acquire))
        {
            const bool on = adaptiveReq.load (std::memory_order_relaxed);
            adaptiveOn.store (on, std::memory_order_relaxed);
            if (on)
            {
                pp = paramsFor ((AdaptPolicy) adaptPolicy.load());
                adaptMarginMs.store (pp.startMs);
                floorMs = pp.startMs;
                holdUntilUs = nowUs + (int64_t) (pp.settleS * 1e6);
                settleUntilUs.store (holdUntilUs);
                winN = 0; concealRun = 0;
            }
        }
        const bool adaptive = adaptiveOn.load (std::memory_order_relaxed);

        const int64_t sincePushUs = nowUs - lastPushUs.load (std::memory_order_relaxed);
        const bool producerAlive = sincePushUs < 300000;
        if (! producerAlive)
        {
            if (primed) { primed = false; resampler.reset(); }
            state.store ((int) State::Idle);
            silence (out, numOut);
            return;
        }

        const int blk = std::max (hostBlock.load(), lastHostBlock.load());
        const double marginFrames = getMarginMs() * 0.001 * hr;
        const double perCall = (double) numOut * baseRatio;
        // Target fill: what one render consumes + one full host block (block-based delivery)
        // + the mode's safety margin + the resampler's look-ahead.
        const double target = perCall + (double) blk + marginFrames + SincResampler::kHalfTaps;
        // "Virtual" fill: queued frames plus the frames the host has produced in real time since its
        // last push (block delivery sawtooth removed, so the controller sees only real clock drift).
        const double pending = std::clamp ((double) sincePushUs * 1e-6 * hr, 0.0, (double) blk);
        auto measureFill = [&] { return (double) ring.availableToRead() + resampler.buffered() + pending; };

        double fill = measureFill();

        // Mode switch: smaller target -> drop the excess now; larger target -> re-prime.
        if (modeChanged.exchange (false, std::memory_order_acquire))
        {
            lowExtraMs.store (0.0, std::memory_order_relaxed);
            if (primed)
            {
                if (fill > target + 8.0)
                {
                    ring.skip ((int) std::floor (fill - target));
                    fill = measureFill();
                    smoothFill = fill;
                }
                else if (fill < target - 8.0)
                {
                    primed = false;
                }
            }
        }

        if (! primed)
        {
            if (fill < target)
            {
                state.store ((int) State::Priming);
                silence (out, numOut);
                return;
            }
            // Drop stale/excess audio so we start exactly at the target fill.
            const int excess = (int) std::floor (fill - target);
            if (excess > 0) ring.skip (excess);
            fill = measureFill();
            smoothFill = fill;
            integ = 0.0;
            primed = true;
            state.store ((int) State::Running);
        }

        // Smoothed fill -> drift correction (~2 s smoothing; real clock drift is only tens of ppm).
        smoothFill = (smoothFill < 0.0) ? fill : smoothFill + 0.01 * (fill - smoothFill);
        double err = smoothFill - target;

        burstScore = burstScore * 0.99 + (double) fastPushes.exchange (0, std::memory_order_relaxed);
        bursty.store (burstScore > 3.0, std::memory_order_relaxed);

        // Far too much buffered (e.g. a host hiccup produced a burst): resync hard.
        if (err > (bursty.load() ? 0.5 : 0.1) * hr)
        {
            ring.skip ((int) std::floor (err));
            fill = measureFill();
            smoothFill = fill;
            err = smoothFill - target;
            resyncCount.fetch_add (1, std::memory_order_relaxed);
        }

        // Proportional + slow integral control with a small deadband (frames).
        const double dead = 8.0;
        const double e = (err > dead) ? err - dead : (err < -dead ? err + dead : 0.0);
        integ = std::clamp (integ + e / hr * 1e-4, -0.0005, 0.0005);
        // Drains the error over ~8 s; with the adaptive margin over ~1.5 s, so a raised margin is refilled
        // quickly (still at most 0.3 % speed change, which is inaudible).
        double correction = e / ((adaptive ? 1.5 : 8.0) * hr) + integ;
        // Refilling a raised margin (more than 3 ms short) may use 0.5 %, otherwise 0.3 % / 0.1 %.
        const double maxCorr = adaptive ? (e < -0.003 * hr ? 0.005 : 0.003) : 0.001;
        correction = std::clamp (correction, -maxCorr, maxCorr);
        const double ratio = baseRatio * (1.0 + correction);

        const int need = resampler.framesNeeded (numOut, ratio);
        const int avail = ring.availableToRead();
        if (adaptive && need > avail && need <= resampler.spaceAvailable() && concealRun < 3)
        {
            // Not enough input: conceal this period instead of a silent gap + re-prime,
            // and grow the margin by the shortfall (kept for 10 minutes).
            dropouts.fetch_add (1, std::memory_order_relaxed);
            concealed.fetch_add (1, std::memory_order_relaxed);
            ++concealRun;
            const double shortMs = 1000.0 * (double) (need - avail) / hr;
            const double m = std::min (pp.maxMs, adaptMarginMs.load() + shortMs + 3.0);
            adaptMarginMs.store (m);
            floorMs = m;
            holdUntilUs = nowUs + (int64_t) (pp.holdS * 1e6);
            conceal (out, numOut);
            return;
        }
        if (need > avail || need > resampler.spaceAvailable())
        {
            // Underrun: not enough input. Output silence and re-prime.
            dropouts.fetch_add (1, std::memory_order_relaxed);
            if (mode.load (std::memory_order_relaxed) == 1) // Low: grow the margin automatically
                lowExtraMs.store (std::min (kLowMaxExtraMs, lowExtraMs.load (std::memory_order_relaxed) + 10.0),
                                  std::memory_order_relaxed);
            primed = false;
            resampler.reset();
            state.store ((int) State::Priming);
            silence (out, numOut);
            return;
        }
        if (need > 0)
        {
            const int got = ring.read (resampler.channelPointers(), resampler.getCount(), need);
            resampler.commitAppended (got);
        }
        resampler.process (out, numOut, ratio);
        concealRun = 0;
        if (fadeInPending)
        {
            const int nf = std::min (numOut, 64);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < nf; ++i) out[c][i] *= (float) i / (float) nf;
            fadeInPending = false;
        }
        if (numOut <= (int) lastOut[0].size())
        {
            for (int c = 0; c < numCh; ++c) std::copy (out[c], out[c] + numOut, lastOut[(size_t) c].begin());
            lastN = numOut;
        }

        if (adaptive)
        {
            const bool settling = nowUs < settleUntilUs.load();
            // Safe: a packet later than ever seen before raises the margin now (not at the next check).
            if (! pp.phaseAware && ! settling)
            {
                const double needNow = std::min (pp.maxMs, std::max (pp.minMs, getMaxJitterMs() + pp.addMs));
                if (needNow > adaptMarginMs.load())
                {
                    adaptMarginMs.store (needNow);
                    floorMs = std::max (floorMs, needNow);
                    holdUntilUs = std::max (holdUntilUs, nowUs + (int64_t) (pp.holdS * 1e6));
                }
            }

            // Headroom right before this read; a near miss grows the margin before anything is heard.
            const double slackMs = 1000.0 * (double) (avail - need) / hr;
            if (slackMs < kNearMissMs)
            {
                const double m = std::min (pp.maxMs, adaptMarginMs.load() + 1.0);
                adaptMarginMs.store (m);
                floorMs = std::max (floorMs, m - 1.0);
                holdUntilUs = std::max (holdUntilUs, nowUs + (int64_t) (pp.holdS * 1e6));
            }

            // Phase tracking (time since the last packet's nominal arrival, circular mean over ~1 s)
            if (pp.phaseAware && blk > 0)
            {
                const double periodMs = 1000.0 * (double) blk / hr;
                const double p = std::fmod ((double) sincePushUs * 1e-3 + (double) lastJitterMs.load (std::memory_order_relaxed), periodMs);
                const double a = 2.0 * 3.14159265358979 * p / periodMs;
                phaseC += 0.02 * (std::cos (a) - phaseC);
                phaseS += 0.02 * (std::sin (a) - phaseS);
                double pm = std::atan2 (phaseS, phaseC) / (2.0 * 3.14159265358979) * periodMs;
                if (pm < 0.0) pm += periodMs;
                phaseMs.store (pm, std::memory_order_relaxed);
                if (! settling && winN % 10 == 0)     // every ~100 ms
                {
                    const double j = getMaxJitterMs() + 0.25;
                    const double needMs = std::clamp (std::max (pp.minMs, phaseMarginMs (j, pm, periodMs, pp.addMs, 1.5)), pp.minMs, pp.maxMs);
                    adaptMarginMs.store (std::max (needMs, floorMs > needMs && nowUs <= holdUntilUs ? floorMs : needMs));
                }
            }

            // Every ~10 s (after the measuring phase): margin = measured lateness (+ policy safety),
            // never below the floor kept after a concealment / near miss. The change itself is made
            // by the speed control above (no audio skipped or inserted).
            if (++winN >= 1000 && ! settling)
            {
                if (pp.phaseAware)
                {
                    if (nowUs > holdUntilUs) floorMs = pp.minMs;
                }
                else
                {
                    const double needMs = std::clamp (getMaxJitterMs() + pp.addMs, pp.minMs, pp.maxMs);
                    if (nowUs > holdUntilUs) floorMs = needMs;
                    adaptMarginMs.store (std::clamp (std::max (floorMs, needMs), pp.minMs, pp.maxMs));
                }
                winN = 0;
            }
        }

        // Plugin-side latency (what an impulse actually takes, measured in simulation):
        // every sample waits one host block plus the real queue in front of it; the "virtual" fill already
        // counts half a block on average, so: smoothed virtual fill + one output period.
        const double latency = 1000.0 * (smoothFill / hr + (double) numOut / outRate);
        latencyMs.store ((float) latency, std::memory_order_relaxed);
        driftPpm.store ((float) (correction * 1e6), std::memory_order_relaxed);
    }

    //==================================================== statistics (UI thread)
    State getState() const noexcept           { return (State) state.load(); }
    float getLatencyMs() const noexcept       { return latencyMs.load(); }
    uint32_t getDropouts() const noexcept     { return dropouts.load(); }
    void resetDropouts() noexcept             { dropouts.store (0); }
    float getDriftPpm() const noexcept        { return driftPpm.load(); }
    uint32_t getOverflows() const noexcept    { return overflowCount.load(); }
    uint32_t getResyncs() const noexcept      { return resyncCount.load(); }
    uint32_t getDuplicates() const noexcept   { return duplicates.load(); }
    uint32_t takePushCount() noexcept         { return statPushes.exchange (0); }
    double getHostRate() const noexcept       { return hostSampleRate.load(); }
    int getHostBlock() const noexcept         { return std::max (hostBlock.load(), lastHostBlock.load()); }
    bool isProducerBursty() const noexcept    { return bursty.load(); }
    bool isProducerAlive() const noexcept     { return (nowMicros() - lastPushUs.load()) < 300000; }
    double getFillMs() const noexcept         { return (double) ring.availableToRead() / hostSampleRate.load() * 1000.0; }

    // Calls fn(pointer, bytes) for every large buffer (used to lock the pages in physical memory).
    void forEachBuffer (const std::function<void (const void*, size_t)>& fn) const
    {
        ring.forEachBuffer (fn);
        resampler.forEachBuffer (fn);
    }

    static int64_t nowMicros() noexcept
    {
        using namespace std::chrono;
        return duration_cast<microseconds> (steady_clock::now().time_since_epoch()).count();
    }

private:
    void silence (float* const* out, int n) noexcept
    {
        for (int c = 0; c < numCh; ++c) std::fill (out[c], out[c] + n, 0.0f);
    }

    // The last output period played backwards (continuous at the joint) with a raised-cosine fade-out;
    // the next real period fades in. Much less audible than a silent gap.
    void conceal (float* const* out, int n) noexcept
    {
        if (lastN <= 0) { silence (out, n); fadeInPending = true; return; }
        for (int c = 0; c < numCh; ++c)
        {
            const auto& src = lastOut[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const int k = lastN - 1 - (i % lastN);
                const float g = 0.5f * (1.0f + std::cos (3.14159265f * (float) i / (float) n));
                out[c][i] = src[(size_t) k] * g;
            }
        }
        fadeInPending = true;
    }

    void updateCutoff (double baseRatio) noexcept
    {
        // Downsampling needs the anti-alias cutoff at the output Nyquist.
        const double cutoff = 0.97 * std::min (1.0, 1.0 / baseRatio);
        if (std::abs (cutoff - resampler.getCutoff()) > 1e-6)
            resampler.setCutoff (cutoff); // only when the host rate changes
    }

    const int numCh;
    RingBuffer ring;
    SincResampler resampler;

    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<int> hostBlock { 512 }, lastHostBlock { 0 };
    std::atomic<int64_t> lastPushUs { 0 };
    std::atomic<uint32_t> fastPushes { 0 }, statPushes { 0 }, duplicates { 0 };
    std::atomic<bool> bursty { false };
    uint64_t lastBlockHash = 0; // producer thread only
    int lastBlockFrames = 0;

    std::atomic<int> mode { 0 };
    std::atomic<bool> modeChanged { false };
    std::atomic<double> lowExtraMs { 0.0 };

    // adaptive margin
    std::atomic<bool> adaptiveReq { false }, adaptiveChanged { false }, adaptiveOn { false }, latenessReset { false };
    std::atomic<int> adaptPolicy { 0 };
    std::atomic<double> adaptMarginMs { 15.0 };
    PolicyParams pp = paramsFor (AdaptPolicy::Safe);       // consumer only
    double phaseC = 1.0, phaseS = 0.0;                      // consumer only
    std::atomic<float> lastJitterMs { 0.0f };
    std::atomic<double> phaseMs { 0.0 };
    std::atomic<int64_t> settleUntilUs { 0 };
    std::array<std::atomic<float>, kJitBuckets> jitBucket;
    std::atomic<uint32_t> concealed { 0 };
    double minLate = 1e9; int curBucket = -1;               // producer only
    double floorMs = 15.0;                                  // consumer only
    int64_t holdUntilUs = 0; int winN = 0, concealRun = 0, lastN = 0;
    bool fadeInPending = false;
    std::vector<std::vector<float>> lastOut;

    // consumer-only
    double outRate = 48000.0, smoothFill = -1.0, integ = 0.0, burstScore = 0.0;
    bool primed = false;

    std::atomic<int> state { 0 };
    std::atomic<float> latencyMs { 0.0f }, driftPpm { 0.0f };
    std::atomic<uint32_t> dropouts { 0 }, overflowCount { 0 }, resyncCount { 0 };
};
} // namespace ab
