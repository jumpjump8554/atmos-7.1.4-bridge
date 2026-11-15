// Linux/macOS unit test for the platform-independent DSP core (feeder modes, noise table).
// Build: g++ -std=c++17 -O2 -I Source tests/test_dsp.cpp -o test_dsp && ./test_dsp
#include "dsp/OutputFeeder.h"
#include "dsp/Dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <complex>

using namespace ab;
static constexpr double PI = 3.14159265358979323846;

struct Result { uint32_t dropouts; float latency; double snrDb; double drift; };

static Result run (double hostRate, int hostBlock, double hostClockErrPpm, double outRate, int outPeriod,
                   int mode, double seconds, double jitterFrac, bool hostProcessesTwice = false)
{
    const int N = 12;
    OutputFeeder f (N);
    f.producerPrepare (hostRate, hostBlock);
    f.setMode (mode);
    f.consumerStart (outRate);

    std::vector<std::vector<float>> in (N, std::vector<float> (hostBlock));
    std::vector<const float*> inP (N);
    std::vector<std::vector<float>> out (N, std::vector<float> (outPeriod));
    std::vector<float*> outP (N);
    for (int c = 0; c < N; ++c) { inP[c] = in[c].data(); outP[c] = out[c].data(); }

    const double hostPeriod = hostBlock / (hostRate * (1.0 + hostClockErrPpm * 1e-6)); // real seconds
    const double outPer = outPeriod / outRate;
    double tProd = 0, tCons = 0;
    long long prodFrame = 0;
    std::vector<float> rec; // channel 0 output
    uint32_t xorshift = 1;
    auto rnd = [&]() { xorshift ^= xorshift << 13; xorshift ^= xorshift >> 17; xorshift ^= xorshift << 5; return (xorshift & 0xffff) / 65535.0; };

    while (tCons < seconds)
    {
        double jitter = (rnd() - 0.5) * jitterFrac * hostPeriod;
        if (tProd + jitter <= tCons)
        {
            for (int i = 0; i < hostBlock; ++i)
            {
                double t = (double) (prodFrame + i) / hostRate;
                for (int c = 0; c < N; ++c) in[c][i] = (float) (0.5 * std::sin (2 * PI * (997.0 + 31 * c) * t));
            }
            prodFrame += hostBlock;
            f.producerPush (inP.data(), hostBlock, (int64_t) ((tProd + jitter) * 1e6) + 1000000);
            if (hostProcessesTwice) // same block again, immediately (buggy/double-processing host)
                f.producerPush (inP.data(), hostBlock, (int64_t) ((tProd + jitter) * 1e6) + 1000050);
            tProd += hostPeriod;
        }
        else
        {
            f.render (outP.data(), outPeriod, (int64_t) (tCons * 1e6) + 1000000);
            rec.insert (rec.end(), out[0].begin(), out[0].end());
            tCons += outPer;
        }
    }
    // Worst local SNR over 100 ms windows of the last 5 s (fit amplitude+phase at the expected frequency).
    const double fq = 997.0 * (1.0 + hostClockErrPpm * 1e-6);
    const int W = (int) (outRate / 10);
    double worst = 1e9;
    for (size_t s0 = rec.size() - (size_t) (5 * outRate); s0 + W <= rec.size(); s0 += W)
    {
        auto fit = [&] (double f2)
        {
            double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
            for (int i = 0; i < W; ++i)
            {
                double ph = 2 * PI * f2 * (double) i / outRate;
                double s = std::sin (ph), co = std::cos (ph), y = rec[s0 + i];
                ss += s * s; cc += co * co; sc += s * co; ys += y * s; yc += y * co;
            }
            double det = ss * cc - sc * sc;
            double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            double sig = 0, err = 0;
            for (int i = 0; i < W; ++i)
            {
                double ph = 2 * PI * f2 * (double) i / outRate;
                double m = a * std::sin (ph) + b * std::cos (ph);
                sig += m * m; err += (rec[s0 + i] - m) * (rec[s0 + i] - m);
            }
            return 10 * std::log10 (sig / std::max (err, 1e-20));
        };
        double best = -1e9, bestF = fq;
        for (double f2 = fq - 0.3; f2 <= fq + 0.3; f2 += 0.01) { double v = fit (f2); if (v > best) { best = v; bestF = f2; } }
        for (double f2 = bestF - 0.01; f2 <= bestF + 0.01; f2 += 0.0002) best = std::max (best, fit (f2));
        worst = std::min (worst, best);
    }
    double bestSnr = worst;
    return { f.getDropouts(), f.getLatencyMs(), bestSnr, f.getDriftPpm() };
}

// Mode switches while running: Music -> Ultra -> Low -> Music. No dropouts, latency follows the mode quickly.
static int modeSwitchTest()
{
    const int N = 12, blk = 480, per = 480;
    OutputFeeder f (N);
    f.producerPrepare (48000, blk);
    f.setMode (0);
    f.consumerStart (48000);
    std::vector<std::vector<float>> in (N, std::vector<float> (blk, 0.1f)), out (N, std::vector<float> (per));
    std::vector<const float*> inP (N); std::vector<float*> outP (N);
    for (int c = 0; c < N; ++c) { inP[c] = in[c].data(); outP[c] = out[c].data(); }
    int fails = 0;
    double t = 0;
    const int modes[4] = { 0, 2, 1, 0 };
    const double expect[4] = { 130, 35, 38, 130 }; // true plugin latency (v1.2 margins: 100 / 4.5 / 8 ms)
    for (int phase = 0; phase < 4; ++phase)
    {
        f.setMode (modes[phase]);
        for (int k = 0; k < 1000; ++k) // 10 s per phase, producer and consumer interleaved (same clock)
        {
            f.producerPush (inP.data(), blk, (int64_t) (t * 1e6) + 1000000);
            f.render (outP.data(), per, (int64_t) (t * 1e6) + 1000100);
            t += 0.01;
        }
        const float lat = f.getLatencyMs();
        const bool ok = std::abs (lat - expect[phase]) < 8.0;
        std::printf ("mode switch -> %d: latency %.1f ms (expected ~%.0f) dropouts=%u %s\n", modes[phase], lat, expect[phase], f.getDropouts(), ok ? "OK" : "FAIL");
        if (! ok) ++fails;
    }
    if (f.getDropouts() != 0) { std::printf ("mode switch dropouts FAIL\n"); ++fails; }
    return fails;
}

// Ultra low latency + direct loopback: adaptive margin. Packets of 480 frames every 10 ms (other clock, +40 ppm)
// arrive with scheduling jitter; the output must never run dry after the first stalls have been learned.
struct AdaptResult { uint32_t dropouts, lateDropouts, concealed; double marginEnd, latency; double settleAtStart; };
static AdaptResult adaptiveRun (double minutes, double pSpike, double spikeLo, double spikeHi, double pBig, double bigLo, double bigHi, unsigned seed)
{
    const int N = 12, pkt = 480, per = 480; const double fs = 48000.0;
    OutputFeeder f (N);
    f.producerPrepare (fs, pkt);
    f.setMode (2);
    f.setAdaptive (true);
    f.consumerStart (fs);
    std::vector<std::vector<float>> in (N, std::vector<float> (pkt, 0.1f)), out (N, std::vector<float> (per));
    std::vector<const float*> inP (N); std::vector<float*> outP (N);
    for (int c = 0; c < N; ++c) { inP[c] = in[c].data(); outP[c] = out[c].data(); }
    uint32_t x = seed;
    auto rnd = [&] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (x & 0xffffff) / 16777215.0; };
    const double pktT = pkt / fs * (1.0 + 40e-6), T = minutes * 60.0;
    double nextNominal = pktT, prevArrival = 0.0, tc = 0.0037;
    struct P { double arrive, late; };
    std::vector<P> queue;
    const int64_t base = 1000000;
    AdaptResult r {};
    bool settleRead = false;
    while (tc < T)
    {
        // schedule packets up to the consumer time
        while (nextNominal <= tc + 0.05)
        {
            double d = rnd() * 0.5e-3;
            const double u = rnd();
            if (u < pBig) d += (bigLo + rnd() * (bigHi - bigLo)) * 1e-3;
            else if (u < pBig + pSpike) d += (spikeLo + rnd() * (spikeHi - spikeLo)) * 1e-3;
            const double a = std::max (nextNominal + d, prevArrival);
            queue.push_back ({ a, (a - nextNominal) * 1e3 });
            prevArrival = a;
            nextNominal += pktT;
        }
        size_t k = 0;
        for (; k < queue.size() && queue[k].arrive <= tc; ++k)
        {
            for (int c = 0; c < N; ++c) in[(size_t) c][0] = (float) rnd() * 0.1f;   // real audio is never the same twice
            f.producerReportLateness (queue[k].late);
            f.producerPush (inP.data(), pkt, base + (int64_t) (queue[k].arrive * 1e6));
        }
        queue.erase (queue.begin(), queue.begin() + (long) k);
        const uint32_t before = f.getDropouts();
        f.render (outP.data(), per, base + (int64_t) (tc * 1e6));
        if (! settleRead) { r.settleAtStart = f.getSettleRemainingSec (base + (int64_t) (tc * 1e6)); settleRead = true; }
        if (f.getDropouts() != before && tc > T - 600.0) ++r.lateDropouts;
        if (f.getDropouts() != before && std::getenv ("AB_TRACE")) std::printf ("   dropout at %.1f s, margin now %.2f ms\n", tc, f.getMarginMs());
        tc += per / fs;
    }
    r.dropouts = f.getDropouts();
    r.concealed = f.getConcealed();
    r.marginEnd = f.getMarginMs();
    r.latency = f.getLatencyMs();
    return r;
}

static int adaptiveTest()
{
    int fails = 0;
    {
        const auto r = adaptiveRun (30, 0.002, 2, 5, 0.0, 0, 0, 7);
        const bool ok = r.dropouts == 0 && r.marginEnd < 8.5 && r.settleAtStart > 100.0;
        std::printf ("adaptive, normal PC 30 min: dropouts=%u margin %.2f ms latency %.1f ms (settle at start %.0f s) %s\n",
                     r.dropouts, r.marginEnd, r.latency, r.settleAtStart, ok ? "OK" : "FAIL");
        if (! ok) ++fails;
    }
    {
        const auto r = adaptiveRun (30, 0.004, 2, 6, 0.0002, 8, 12, 11);
        const bool ok = r.lateDropouts == 0 && r.dropouts <= 3 && r.dropouts == r.concealed;
        std::printf ("adaptive, busy PC 30 min (8-12 ms stalls): dropouts=%u (concealed %u, last 10 min %u) margin %.2f ms latency %.1f ms %s\n",
                     r.dropouts, r.concealed, r.lateDropouts, r.marginEnd, r.latency, ok ? "OK" : "FAIL");
        if (! ok) ++fails;
    }
    return fails;
}

// Clipping prevention: linked, zero latency, never above the ceiling, untouched below it, recovers.
static int limiterTest()
{
    int fails = 0;
    LinkedLimiter lim;
    lim.prepare (48000.0, -1.0f);
    const float ceil = lim.getCeiling();
    const int n = 48000;
    std::vector<std::vector<float>> ch (12, std::vector<float> (n, 0.0f));
    std::vector<float*> p (12);
    for (int c = 0; c < 12; ++c) p[c] = ch[c].data();

    // 1) quiet signal (peak 0.5): bit-exact pass-through
    for (int i = 0; i < n; ++i) { ch[0][i] = 0.5f * (float) std::sin (i * 0.05); ch[5][i] = 0.3f * (float) std::sin (i * 0.031); }
    const auto ref0 = ch[0], ref5 = ch[5];
    float g = 1.0f;
    for (int off = 0; off < n; off += 480) { float* q[12]; for (int c = 0; c < 12; ++c) q[c] = p[c] + off; g = std::min (g, lim.process (q, 12, 480)); }
    const bool untouched = ch[0] == ref0 && ch[5] == ref5 && g == 1.0f;
    std::printf ("limiter: quiet signal untouched %s\n", untouched ? "OK" : "FAIL");
    if (! untouched) ++fails;

    // 2) hot signal (+6 dBFS on ch 0) with a quieter ch 7: never above -1 dBFS, balance kept
    for (int i = 0; i < n; ++i) { ch[0][i] = 2.0f * (float) std::sin (i * 0.013); ch[7][i] = 0.5f * (float) std::sin (i * 0.013); }
    for (int off = 0; off < n; off += 480) { float* q[12]; for (int c = 0; c < 12; ++c) q[c] = p[c] + off; lim.process (q, 12, 480); }
    float peak = 0.0f, worstRatio = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        peak = std::max (peak, std::abs (ch[0][i]));
        if (std::abs (ch[0][i]) > 0.05f) worstRatio = std::max (worstRatio, std::abs (ch[7][i] / ch[0][i] - 0.25f));
    }
    const bool capped = peak <= ceil + 1e-6f && peak > ceil * 0.99f;
    std::printf ("limiter: +6 dBFS input -> peak %.4f (ceiling %.4f) %s, linked ratio error %.5f %s\n",
                 peak, ceil, capped ? "OK" : "FAIL", worstRatio, worstRatio < 1e-5f ? "OK" : "FAIL");
    if (! capped) ++fails;
    if (worstRatio >= 1e-5f) ++fails;

    // 3) back to a quiet signal: the gain recovers to 1 within a second (80 ms release)
    for (auto& c : ch) std::fill (c.begin(), c.end(), 0.0f);
    for (int i = 0; i < n; ++i) ch[0][i] = 0.5f * (float) std::sin (i * 0.05);
    for (int off = 0; off < n; off += 480) { float* q[12]; for (int c = 0; c < 12; ++c) q[c] = p[c] + off; lim.process (q, 12, 480); }
    const bool recovered = lim.getGain() == 1.0f;
    std::printf ("limiter: gain after 1 s of quiet audio %.5f %s\n", lim.getGain(), recovered ? "OK" : "FAIL");
    if (! recovered) ++fails;
    return fails;
}

int main()
{
    int fails = 0;
    auto check = [&](const char* name, Result r, double maxLat)
    {
        bool ok = r.dropouts == 0 && r.snrDb > 65 && r.latency < maxLat;
        std::printf ("%-52s dropouts=%u latency=%.1fms snr=%.1fdB drift=%.0fppm %s\n", name, r.dropouts, r.latency, r.snrDb, r.drift, ok ? "OK" : "FAIL");
        if (! ok) ++fails;
    };
    // mode: 0 = Music, 1 = Low, 2 = Ultra
    check ("music, 48k block 480, same clock",              run (48000, 480,   0, 48000, 480, 0, 60, 0.2), 150);
    check ("low, 48k block 480, same clock",                run (48000, 480,   0, 48000, 480, 1, 60, 0.2), 70);
    check ("ultra, 48k block 480, same clock",              run (48000, 480,   0, 48000, 480, 2, 60, 0.2), 45);
    check ("ultra, 48k block 1024, same clock",             run (48000, 1024,  0, 48000, 480, 2, 60, 0.2), 65);
    check ("ultra, 48k block 480, +30ppm (other clock)",    run (48000, 480,  30, 48000, 480, 2, 60, 0.2), 45);
    check ("low, 48k block 1024, -40ppm",                   run (48000, 1024, -40, 48000, 480, 1, 60, 0.2), 90);
    check ("music, 44.1k->48k block 441, +80ppm",           run (44100, 441,  80, 48000, 480, 0, 60, 0.2), 150);
    check ("low, host processes every block twice",         run (48000, 480,  20, 48000, 480, 1, 30, 0.2, true), 70);

    // Pink noise table: RMS = -20 dBFS for both versions.
    NoiseTable nt;
    nt.build (48000.0, 4.0, std::pow (10.0f, -20.0f / 20.0f), 120.0);
    for (int v = 0; v < 2; ++v)
    {
        std::vector<float> buf (nt.getLength());
        nt.read (buf.data(), (int) buf.size(), v == 1);
        double acc = 0; for (float x : buf) acc += (double) x * x;
        double db = 10 * std::log10 (acc / buf.size());
        bool ok = std::abs (db + 20.0) < 0.05;
        std::printf ("noise table %s rms %.2f dBFS %s\n", v ? "LFE " : "full", db, ok ? "OK" : "FAIL");
        if (! ok) ++fails;
    }
    // LFE version: energy above 300 Hz must be tiny (DFT probe at a few frequencies).
    {
        std::vector<float> buf (48000);
        nt.read (buf.data(), 48000, true);
        auto mag = [&] (double fHz)
        {
            std::complex<double> s = 0;
            for (int i = 0; i < 48000; ++i) s += (double) buf[i] * std::polar (1.0, -2 * PI * fHz * i / 48000.0);
            return std::abs (s);
        };
        double lo = mag (50), hi = mag (1000);
        bool ok = hi < lo * 0.01;
        std::printf ("LFE noise 50 Hz vs 1 kHz ratio %.4f %s\n", hi / std::max (lo, 1e-12), ok ? "OK" : "FAIL");
        if (! ok) ++fails;
    }

    fails += modeSwitchTest();
    fails += limiterTest();
    fails += adaptiveTest();

    std::printf (fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails;
}
