#pragma once
// Streaming multichannel windowed-sinc resampler with a variable ratio.
// ratio = input frames consumed per output frame (hostRate / outputRate, plus drift correction).
// Input is appended into an internal planar buffer; process() consumes it.

#include <vector>
#include <functional>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ab
{
class SincResampler
{
public:
    static constexpr int kHalfTaps = 16;            // taps on each side
    static constexpr int kTaps     = kHalfTaps * 2; // 32 taps
    static constexpr int kPhases   = 256;

    // Not real-time safe.
    void setup (int numChannels, int capacityFrames)
    {
        numCh = numChannels;
        capacity = capacityFrames;
        buffers.assign ((size_t) numCh, std::vector<float> ((size_t) capacity, 0.0f));
        ptrs.resize ((size_t) numCh);
        for (int c = 0; c < numCh; ++c) ptrs[(size_t) c] = buffers[(size_t) c].data();
        if (table.empty()) setCutoff (0.97);
        reset();
    }

    // cutoff relative to the input Nyquist frequency (0..1). Rebuilds the kernel table.
    void setCutoff (double cutoff)
    {
        cutoffUsed = cutoff;
        table.assign ((size_t) (kPhases + 1) * kTaps, 0.0f);
        const double beta = 8.6;
        const double i0b = besselI0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            double sum = 0.0;
            float* row = table.data() + (size_t) p * kTaps;
            for (int t = 0; t < kTaps; ++t)
            {
                const double x = (double) (t - (kHalfTaps - 1)) - frac; // distance in input samples
                const double u = x / (double) kHalfTaps;
                double w = 0.0;
                if (std::abs (u) < 1.0)
                    w = besselI0 (beta * std::sqrt (1.0 - u * u)) / i0b;
                const double s = (std::abs (x) < 1e-9) ? 1.0 : std::sin (kPi * cutoff * x) / (kPi * cutoff * x);
                const double v = cutoff * s * w;
                row[t] = (float) v;
                sum += v;
            }
            if (sum != 0.0)
                for (int t = 0; t < kTaps; ++t) row[t] = (float) (row[t] / sum); // unity DC gain
        }
    }

    double getCutoff() const noexcept { return cutoffUsed; }

    // Clears history (zeros) and positions the read pointer after the history.
    void reset() noexcept
    {
        for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
        count = kHalfTaps;            // kHalfTaps frames of silent history
        pos = (double) (kHalfTaps - 1);
    }

    // Unconsumed input frames that are buffered (can be fractional).
    double buffered() const noexcept { return (double) count - pos - 1.0; }

    // How many more input frames must be appended before process(numOut, ratio) can run.
    int framesNeeded (int numOut, double ratio) const noexcept
    {
        const double lastPos = pos + (double) (numOut - 1) * ratio;
        const int needCount = (int) std::floor (lastPos) + kHalfTaps + 1;
        return std::max (0, needCount - count);
    }

    int spaceAvailable() const noexcept { return capacity - count; }

    // Planar destination pointers for appending; caller writes n frames at offset getCount().
    float* const* channelPointers() noexcept { return ptrs.data(); }
    int getCount() const noexcept { return count; }
    void commitAppended (int n) noexcept { count += n; }

    void appendSilence (int n) noexcept
    {
        n = std::min (n, spaceAvailable());
        for (auto& b : buffers) std::fill (b.begin() + count, b.begin() + count + n, 0.0f);
        count += n;
    }

    // Produces numOut frames per channel. Caller must have ensured framesNeeded() == 0.
    void process (float* const* out, int numOut, double ratio) noexcept
    {
        for (int i = 0; i < numOut; ++i)
        {
            const int ip = (int) std::floor (pos);
            const double frac = pos - (double) ip;
            const double ph = frac * kPhases;
            const int p0 = std::min ((int) ph, kPhases - 1);
            const float a = (float) (ph - (double) p0);
            const float* r0 = table.data() + (size_t) p0 * kTaps;
            const float* r1 = r0 + kTaps;
            const int start = ip - (kHalfTaps - 1);
            for (int c = 0; c < numCh; ++c)
            {
                const float* in = buffers[(size_t) c].data() + start;
                float s0 = 0.0f, s1 = 0.0f;
                for (int t = 0; t < kTaps; ++t)
                {
                    s0 += in[t] * r0[t];
                    s1 += in[t] * r1[t];
                }
                out[c][i] = s0 + a * (s1 - s0);
            }
            pos += ratio;
        }
        compact();
    }

    void forEachBuffer (const std::function<void (const void*, size_t)>& fn) const
    {
        for (const auto& b : buffers) if (! b.empty()) fn (b.data(), b.size() * sizeof (float));
        if (! table.empty()) fn (table.data(), table.size() * sizeof (float));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double y = x * x / 4.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= y / ((double) k * (double) k);
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    // Drop input that is no longer needed (keep kHalfTaps of history before pos).
    void compact() noexcept
    {
        const int keepFrom = (int) std::floor (pos) - (kHalfTaps - 1);
        if (keepFrom <= 0) return;
        const int remain = count - keepFrom;
        if (remain > 0)
            for (auto& b : buffers)
                std::memmove (b.data(), b.data() + keepFrom, sizeof (float) * (size_t) remain);
        count = std::max (0, remain);
        pos -= (double) keepFrom;
    }

    int numCh = 0, capacity = 0, count = 0;
    double pos = 0.0, cutoffUsed = 0.97;
    std::vector<std::vector<float>> buffers;
    std::vector<float*> ptrs;
    std::vector<float> table;
};
} // namespace ab
