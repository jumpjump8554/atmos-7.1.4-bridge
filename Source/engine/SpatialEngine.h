#pragma once
// Output engine: renders 12 static 7.1.4 objects through Windows' ISpatialAudioClient.
// Windows' spatial renderer ("Dolby Atmos for home theater") encodes them (Dolby MAT over HDMI).
// 2ch Stereo (v1.5): instead, the same device is opened in WASAPI exclusive mode as PCM 2.0 (positions L/R),
// so the AV receiver gets plain stereo and can use its own upmixers.
// Runs its own event-driven thread (MMCSS "Pro Audio"), reconnects automatically after device changes.

#include "../dsp/OutputFeeder.h"
#include <atomic>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace ab
{
struct OutputDevice
{
    std::string id;    // endpoint ID (UTF-8)
    std::string name;  // friendly name (UTF-8)
    bool isDefault = false;
};

enum class EngineStatus : int
{
    Stopped = 0,
    Connecting,     // trying to open the device / stream
    Running,        // spatial stream running, spatial sound format active
    SpatialOff,     // device found but no spatial sound format active (stream not available)
    NoDevice,       // selected device missing
    Error,          // other failure (see getLastError)
    Unsupported,    // not Windows
    Busy,           // 2ch: the device is used by another application (exclusive mode refused)
    ExclusiveOff    // 2ch: exclusive mode is not allowed for this device (Windows sound settings)
};

class SpatialEngine
{
public:
    explicit SpatialEngine (OutputFeeder& feeder);
    ~SpatialEngine();

    void start();
    void stop();

    void setDeviceId (const std::string& endpointId); // "" = automatic (first device with spatial sound)
    void setStereo (bool stereo2ch);                  // true = 2ch PCM, WASAPI exclusive (reconnects)
    void setExclusiveBufferFrames (int frames48k);   // 2ch exclusive buffer (= output latency), 0 = device default
    void setAutoEnableAtmos (bool on);                // Atmos: select "Dolby Atmos for home theater" when it is off
    bool isStereoActive() const noexcept;             // the running connection is the 2ch exclusive one
    std::string getDeviceId() const;
    void requestReconnect();

    EngineStatus getStatus() const noexcept;
    bool isReconnecting() const noexcept;
    std::string getDeviceName() const;
    std::string getLastError() const;
    int getMaxDynamicObjects() const noexcept;
    double getOutputRate() const noexcept;
    int getPeriodFrames() const noexcept;
    uint32_t getReconnectCount() const noexcept;
    bool isMemoryLocked() const noexcept;

    static std::vector<OutputDevice> listOutputDevices();

    // Virtual audio cables (VB-Audio, VoiceMeeter, ...): the host usually captures from these,
    // so sending the Atmos output there creates a feedback loop. Never picked automatically.
    static bool isVirtualDeviceName (const std::string& name)
    {
        std::string n = name;
        for (auto& ch : n) ch = (char) std::tolower ((unsigned char) ch);
        for (const char* key : { "cable", "vb-audio", "voicemeeter", "virtual" })
            if (n.find (key) != std::string::npos) return true;
        return false;
    }

    struct Impl;
private:
    std::unique_ptr<Impl> impl;
};

const char* toString (EngineStatus s) noexcept;
} // namespace ab
