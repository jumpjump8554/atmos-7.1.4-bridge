#pragma once
// Small DSP helpers: pink noise generator, biquad low-pass and a pre-generated noise table.

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace ab
{
// Paul Kellet's refined pink noise filter, normalised to unit RMS.
class PinkNoise
{
public:
    void seed (uint32_t s) noexcept { state = s ? s : 0x12345678u; }
    void reset() noexcept { b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f; }

    float next() noexcept
    {
        const float w = white();
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return pink * kUnitRms;
    }

private:
    static constexpr float kUnitRms = 1.0f / 1.7556f; // measured RMS of the raw filter output

    float white() noexcept // uniform in [-1, 1)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) ((double) state / 2147483648.0 - 1.0);
    }

    uint32_t state = 0x12345678u;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};

// RBJ biquad (low-pass), transposed direct form II.
class Biquad
{
public:
    void setLowPass (double sampleRate, double freq, double q = 0.7071067811865476)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c = std::cos (w0);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 - c) / 2.0) / a0);
        b1 = (float) ((1.0 - c) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// Pre-generated pink noise (built outside the audio thread, read in real time without any computation).
//  full: full-band pink noise at the target RMS level
//  lfe : the same noise low-passed (LFE test signal), re-normalised to the same RMS
class NoiseTable
{
public:
    // Not real-time safe (allocates). seconds = loop length.
    void build (double sampleRate, double seconds, float rmsGain, double lfeCutHz)
    {
        const int n = std::max (1024, (int) (sampleRate * seconds));
        full.assign ((size_t) n, 0.0f);
        lfe.assign ((size_t) n, 0.0f);

        PinkNoise gen;
        gen.seed (0x9E3779B9u);
        for (int i = 0; i < 4096; ++i) gen.next(); // settle the filter
        for (int i = 0; i < n; ++i) full[(size_t) i] = gen.next();

        // LFE: low-pass twice (run-in over one loop so the loop seam is continuous enough).
        Biquad f1, f2;
        f1.setLowPass (sampleRate, lfeCutHz);
        f2.setLowPass (sampleRate, lfeCutHz);
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < n; ++i)
                lfe[(size_t) i] = f2.process (f1.process (full[(size_t) i]));

        normalise (full, rmsGain);
        normalise (lfe, rmsGain);
        length = n;
        pos = 0;
    }

    bool isReady() const noexcept { return length > 0; }
    int getLength() const noexcept { return length; }

    // Copies n samples starting at the shared read position (does not advance it).
    void read (float* dst, int n, bool lfeVersion) const noexcept
    {
        if (length <= 0) { std::fill (dst, dst + n, 0.0f); return; }
        const float* src = lfeVersion ? lfe.data() : full.data();
        int p = pos;
        for (int i = 0; i < n; ++i)
        {
            dst[i] = src[p];
            if (++p >= length) p = 0;
        }
    }

    void advance (int n) noexcept
    {
        if (length > 0) pos = (int) (((long long) pos + n) % length);
    }

    // Raw access (used to lock the memory pages).
    const float* fullData() const noexcept { return full.data(); }
    const float* lfeData() const noexcept { return lfe.data(); }
    size_t bytes() const noexcept { return full.size() * sizeof (float); }

private:
    static void normalise (std::vector<float>& v, float rms)
    {
        double acc = 0.0;
        for (float x : v) acc += (double) x * x;
        const double cur = std::sqrt (acc / (double) std::max<size_t> (1, v.size()));
        const float k = cur > 1e-12 ? (float) (rms / cur) : 0.0f;
        for (auto& x : v) x *= k;
    }

    std::vector<float> full, lfe;
    int length = 0, pos = 0;
};

// Clipping prevention: 12-channel linked peak limiter with zero latency (no look-ahead).
//  - the gain is computed from the loudest channel, and the same gain is applied to every channel,
//    so the balance between the speakers never changes
//  - a sample that would exceed the ceiling gets exactly the gain that puts it on the ceiling
//    (instant attack), then the gain holds for 5 ms and recovers smoothly (80 ms release)
class LinkedLimiter
{
public:
    void prepare (double sampleRate, float ceilingDb = -1.0f) noexcept
    {
        ceiling = std::pow (10.0f, ceilingDb / 20.0f);
        holdSamples = std::max (1, (int) (0.005 * sampleRate));
        release = (float) std::exp (-1.0 / (0.080 * sampleRate));
        reset();
    }

    void reset() noexcept { atten = 0.0f; hold = 0; }

    float getCeiling() const noexcept { return ceiling; }
    float getGain() const noexcept { return 1.0f - atten; }

    // Processes numCh channels in place; returns the lowest gain used in this block (1 = untouched).
    // The state is the attenuation (1 - gain): near unity gain a float cannot represent the tiny
    // release steps of the gain itself, but it can represent them for the attenuation.
    float process (float* const* ch, int numCh, int n) noexcept
    {
        float minGain = 1.0f;
        for (int k = 0; k < n; ++k)
        {
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
                peak = std::max (peak, std::abs (ch[c][k]));

            const float need = peak > ceiling ? ceiling / peak : 1.0f;
            const float needAtten = 1.0f - need;
            if (needAtten > atten)  { atten = needAtten; hold = holdSamples; }
            else if (hold > 0)      { --hold; }
            else if (atten > 0.0f)
            {
                atten = std::max (needAtten, atten * release);
                if (atten < 1.0e-5f) atten = 0.0f; // -0.0001 dB: done
            }

            const float g = std::min (need, 1.0f - atten); // never above the ceiling
            if (g < 1.0f)
                for (int c = 0; c < numCh; ++c)
                    ch[c][k] *= g;
            minGain = std::min (minGain, g);
        }
        return minGain;
    }

private:
    float ceiling = 0.891251f, atten = 0.0f, release = 0.9997f;
    int holdSamples = 240, hold = 0;
};
} // namespace ab
