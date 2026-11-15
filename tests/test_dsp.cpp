// Linux/macOS unit test for the platform-independent DSP core (feeder modes, noise table).
// Build: g++ -std=c++17 -O2 -I Source tests/test_dsp.cpp -o test_dsp && ./test_dsp
#include "dsp/OutputFeeder.h"
#include "dsp/Dsp.h"
#include <cstdio>
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
    const double expect[4] = { 135, 38, 56, 135 };
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

    std::printf (fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails;
}
