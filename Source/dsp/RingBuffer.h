#pragma once
// Lock-free single-producer / single-consumer ring buffer for interleaved multichannel audio.
// Producer: the host audio thread (PluginProcessor::processBlock).
// Consumer: the spatial output thread (SpatialEngine).

#include <atomic>
#include <functional>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace ab
{
class RingBuffer
{
public:
    // Not real-time safe. Call only while neither side is running.
    void init (int numChannels, int capacityFramesPow2)
    {
        int cap = 1;
        while (cap < capacityFramesPow2) cap <<= 1;
        numCh = numChannels;
        capacity = cap;
        mask = (uint64_t) cap - 1;
        data.assign ((size_t) cap * (size_t) numCh, 0.0f);
        writeIdx.store (0);
        readIdx.store (0);
    }

    int getNumChannels() const noexcept { return numCh; }
    int getCapacity() const noexcept     { return capacity; }

    int availableToRead() const noexcept
    {
        return (int) (writeIdx.load (std::memory_order_acquire) - readIdx.load (std::memory_order_acquire));
    }

    int availableToWrite() const noexcept { return capacity - availableToRead(); }

    // Producer side. src = planar channel pointers (numCh of them). Returns frames written.
    int write (const float* const* src, int numFrames) noexcept
    {
        const uint64_t w = writeIdx.load (std::memory_order_relaxed);
        const uint64_t r = readIdx.load (std::memory_order_acquire);
        const int space = capacity - (int) (w - r);
        const int n = std::min (numFrames, space);
        for (int i = 0; i < n; ++i)
        {
            float* frame = data.data() + (size_t) ((w + (uint64_t) i) & mask) * (size_t) numCh;
            for (int c = 0; c < numCh; ++c)
                frame[c] = src[c][i];
        }
        writeIdx.store (w + (uint64_t) n, std::memory_order_release);
        return n;
    }

    // Consumer side. dest = planar channel pointers; frames are written at dest[c][destOffset + i].
    int read (float* const* dest, int destOffset, int numFrames) noexcept
    {
        const uint64_t r = readIdx.load (std::memory_order_relaxed);
        const uint64_t w = writeIdx.load (std::memory_order_acquire);
        const int n = std::min (numFrames, (int) (w - r));
        for (int i = 0; i < n; ++i)
        {
            const float* frame = data.data() + (size_t) ((r + (uint64_t) i) & mask) * (size_t) numCh;
            for (int c = 0; c < numCh; ++c)
                dest[c][destOffset + i] = frame[c];
        }
        readIdx.store (r + (uint64_t) n, std::memory_order_release);
        return n;
    }

    // Consumer side: drop frames without reading them.
    int skip (int numFrames) noexcept
    {
        const uint64_t r = readIdx.load (std::memory_order_relaxed);
        const uint64_t w = writeIdx.load (std::memory_order_acquire);
        const int n = std::min (numFrames, (int) (w - r));
        readIdx.store (r + (uint64_t) n, std::memory_order_release);
        return n;
    }

    void forEachBuffer (const std::function<void (const void*, size_t)>& fn) const
    {
        if (! data.empty()) fn (data.data(), data.size() * sizeof (float));
    }

private:
    std::vector<float> data;
    int numCh = 0, capacity = 0;
    uint64_t mask = 0;
    std::atomic<uint64_t> writeIdx { 0 }, readIdx { 0 };
};
} // namespace ab
