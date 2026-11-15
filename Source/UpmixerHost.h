#pragma once
// Hosts one VST3 upmixer (e.g. zAuro Pro 2D-3D Upmixer) inside Atmos Bridge.
//
// Some hosts only open a plugin with its default channel layout (stereo in / stereo out), so a
// "stereo in -> 7.1.4 out" upmixer can only output L/R there. Atmos Bridge opens the upmixer itself,
// asks for stereo in -> 7.1.4 out (with fallbacks), and feeds the 12 channels straight into its own
// input stage, re-ordered to L R C LFE SBL SBR SL SR TFL TFR TRL TRR when the layout names the channels.
//
// Threading: load/unload/prepare/state = message thread. process() = audio thread (never blocks:
// it uses a try-lock and reports "not available" while a load/unload is in progress).

#include <JuceHeader.h>
#include "Shared.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>

class UpmixerHost
{
public:
    struct Info
    {
        bool loaded = false;
        bool namedLayout = false;   // true: channels re-ordered by speaker type (no manual mapping needed)
        juce::String name, path, layout, error;
        int latencySamples = 0;
        int numOutputs = 0;
    };

    UpmixerHost();
    ~UpmixerHost();

    // Message thread
    void prepare (double sampleRate, int maxBlockSize);
    bool load (const juce::String& path);           // replaces the current upmixer; sets getInfo().error on failure
    void unload();
    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled); }
    bool isEnabled() const noexcept { return enabled.load(); }
    const Info& getInfo() const noexcept { return info; }
    juce::AudioPluginInstance* getInstance() const noexcept { return slot ? slot->instance.get() : nullptr; }

    bool refreshLatency(); // true if the hosted plugin's latency changed
    void resetPlugin();    // clears the hosted plugin's internal buffers (before it becomes active again)

    juce::String getStateBase64() const;
    void setStateBase64 (const juce::String& base64);

    // Called right before the hosted plugin is destroyed (the editor closes the plugin's window here).
    std::function<void()> beforeUnload;

    // Installed VST3 plugins in the default folders (bundle paths, without loading them).
    static juce::Array<juce::File> findInstalledVst3();

    // Audio thread. Upmixes (inL, inR) into the 12 output positions dst[0..11].
    // Returns false (dst untouched) if no upmixer is active or it is being swapped.
    bool isActive() const noexcept { return enabled.load() && loaded.load(); }
    bool process (const float* inL, const float* inR, float* const* dst, int numSamples) noexcept;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        int numIn = 0, numOut = 0, inLeft = 0, inRight = 1;
        std::array<int, ab::kNumChannels> posToOut {};
    };

    static bool negotiateLayout (juce::AudioPluginInstance& p, juce::String& layoutText);
    static void computeRouting (Slot& s, bool& named);
    void prepareSlot (Slot& s);

    juce::AudioPluginFormatManager formats;
    std::unique_ptr<Slot> slot;
    juce::CriticalSection lock;
    std::atomic<bool> enabled { true }, loaded { false };
    double sampleRate = 48000.0;
    int blockSize = 512;
    Info info;
};
