#pragma once
// Bridges the host audio thread (producer) and the spatial output thread (consumer).
//  - lock-free ring buffer between the two threads
//  - sample-rate conversion host rate -> output rate (1:1 at 48 kHz, plus tiny drift corrections)
//  - clock-drift compensation: tiny ratio adjustments keep the buffer at its target fill
//  - three latency modes (Music / Low / Ultra), priming, underrun detection, latency statistics

#include "RingBuffer.h"
#include "SincResampler.h"
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
    static constexpr double kLowMarginMs     = 20.0;
    static constexpr double kLowMaxExtraMs   = 60.0;  // Low mode grows by 10 ms per dropout, up to +60 ms
    static constexpr double kUltraMarginMs   = 3.0;

    explicit OutputFeeder (int numChannels) : numCh (numChannels)
    {
        ring.init (numCh, 1 << 16);
        resampler.setup (numCh, 1 << 16);
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

    //==================================================== settings (any thread)
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
        double correction = e / (8.0 * hr) + integ;  // drains the error over ~8 s
        correction = std::clamp (correction, -0.001, 0.001);
        const double ratio = baseRatio * (1.0 + correction);

        const int need = resampler.framesNeeded (numOut, ratio);
        if (need > ring.availableToRead() || need > resampler.spaceAvailable())
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

        // Plugin-side latency: half a host block (block delivery) + queued audio + one output period.
        const double latency = 1000.0 * (0.5 * (double) blk / hr + smoothFill / hr + (double) numOut / outRate);
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

    // consumer-only
    double outRate = 48000.0, smoothFill = -1.0, integ = 0.0, burstScore = 0.0;
    bool primed = false;

    std::atomic<int> state { 0 };
    std::atomic<float> latencyMs { 0.0f }, driftPpm { 0.0f };
    std::atomic<uint32_t> dropouts { 0 }, overflowCount { 0 }, resyncCount { 0 };
};
} // namespace ab
