#pragma once
// Direct WASAPI loopback capture of a render endpoint: the device the player plays to (CABLE In 16ch).
// Used by the Low latency and Ultra low latency+ modes instead of the host's own capture (the host input
// stays as the fallback path).
//  - event-driven loopback (Windows 10 1703+) on its own thread, MMCSS "Pro Audio" at critical priority
//  - a silent render stream on the same endpoint keeps the loopback running while nothing plays
//    (WASAPI loopback delivers no data at all while the endpoint is idle)
//  - for every packet: how late it arrived (packet timestamp vs. now), used by the adaptive buffer
//  - reopens after errors / device changes; never captures the Atmos output device
//  - optional (Ultra low latency+): the silence keeper asks the endpoint for a shorter engine period
//    (IAudioClient3, ~5 ms) so the loopback packets get shorter. Only if the driver allows it and no other
//    application has fixed the period; if the packets do not get shorter, it goes back to the normal period.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ab
{
enum class LoopbackStatus : int { Stopped = 0, Starting, Running, NoDevice, Error, Unsupported };
const char* toString (LoopbackStatus s) noexcept;

class LoopbackCapture
{
public:
    // Called on the capture thread for every packet: interleaved float samples, frame count, channel count,
    // lateness in ms (>= 0) and whether the packet is flagged silent (then the samples are zero).
    using PacketFn = std::function<void (const float* interleaved, int frames, int channels, double lateMs, bool silent)>;

    LoopbackCapture();
    ~LoopbackCapture();

    // sourceId: endpoint ID, or "" = automatic: "CABLE In 16ch", else any "CABLE In".
    // excludeName: friendly name of the Atmos output device (never captured).
    void start (const std::string& sourceId, const std::string& excludeName, PacketFn onPacket, bool shortPeriod = false);
    void stop();
    bool isStarted() const noexcept;

    LoopbackStatus getStatus() const noexcept;
    std::string getDeviceName() const;
    std::string getLastError() const;
    double getSampleRate() const noexcept;
    int getChannels() const noexcept;
    int64_t getLastPacketUs() const noexcept;   // steady-clock microseconds of the last packet (0 = none yet)
    uint32_t getPackets() const noexcept;
    uint32_t getDiscontinuities() const noexcept;
    bool hasSilenceKeeper() const noexcept;

    // Short engine period (see above)
    enum class PeriodNote : int { NotRequested = 0, Active, Unsupported, Locked, NoEffect, Failed };
    double getPacketMs() const noexcept;           // typical packet length of the last ~128 packets (0 = none yet)
    int getPacketFrames() const noexcept;
    bool isShortPeriodActive() const noexcept;     // short period running and the packets really are shorter
    PeriodNote getPeriodNote() const noexcept;

    static int64_t nowUs() noexcept;
    static bool isAutoCandidate (const std::string& friendlyName, bool strict); // strict: "CABLE In 16ch"

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ab
