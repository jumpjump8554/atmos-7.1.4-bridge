#pragma once

#include <JuceHeader.h>
#include "Shared.h"
#include "dsp/Dsp.h"
#include "dsp/OutputFeeder.h"
#include "engine/SpatialEngine.h"
#include "engine/LoopbackCapture.h"
#include "UpmixerHost.h"

#include <array>
#include <atomic>
#include <memory>

// One spatial output per process (only one plugin instance drives the HDMI output at a time).
struct SharedOutput
{
    SharedOutput() : feeder (ab::kNumChannels), engine (feeder) {}
    ab::OutputFeeder feeder;
    ab::SpatialEngine engine;
};

//==============================================================================
class AtmosBridgeProcessor final : public juce::AudioProcessor,
                                   private juce::Timer,
                                   private juce::AsyncUpdater
{
public:
    AtmosBridgeProcessor();
    ~AtmosBridgeProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processorLayoutsChanged() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //========================================================================== editor API (message thread)
    juce::AudioProcessorValueTreeState apvts;

    static juce::String muteId (int ch) { return "mute" + juce::String (ch + 1); }

    // Mapping: input channel (0..11) -> output position (0..11), or -1 = off.
    int getPosition (int input) const noexcept { return outMap[(size_t) input].load(); }
    void setPosition (int input, int position);
    void resetMapping();
    bool isDuplicatePosition (int position) const noexcept;

    bool isMuted (int input) const noexcept { return muteParams[(size_t) input]->load() > 0.5f; }
    void setMuted (int input, bool shouldMute);
    bool areAllMuted() const noexcept;
    void setAllMuted (bool shouldMute);

    // Test signals (pink noise, -20 dBFS RMS). Only one at a time; the program audio is silenced meanwhile.
    int getInputNoise() const noexcept { return inputNoise.load(); }
    int getPositionNoise() const noexcept { return positionNoise.load(); }
    bool isSequenceOn() const noexcept { return sequenceOn.load(); }
    int getSequencePosition() const noexcept { return sequencePos.load(); }
    void toggleInputNoise (int input);
    void togglePositionNoise (int position);
    void toggleSequence();
    void stopTests();

    ab::LatencyMode getMode() const noexcept { return (ab::LatencyMode) juce::jlimit (0, 2, (int) std::lround (modeParam->load())); }
    void setMode (ab::LatencyMode m);

    // Meters: RMS (linear) and peak since the last call.
    struct Meter { float rms = 0.0f, peak = 0.0f; };
    Meter takeInputMeter (int input) noexcept;
    Meter takePositionMeter (int position) noexcept;

    // Low latency / Ultra low latency+ input: direct WASAPI loopback of the device the player plays to
    // (CABLE In 16ch), instead of the host's own capture. Self-tuning margin (Low: Safe policy, Ultra+:
    // phase-aware policy + short CABLE In period); falls back to the host input on any problem.
    struct DirectStatus
    {
        enum State { Off = 0, Settling, Running, Fallback };
        int state = Off;
        double marginMs = 0.0, latencyMs = 0.0, settleSec = 0.0;
        double packetMs = 0.0;        // loopback packet period (Ultra+: 5 ms if the short engine period took effect)
        bool shortPeriod = false;     // Ultra+: CABLE In runs at the short engine period
        juce::String source, reason, periodNote;
    };
    DirectStatus getDirectStatus() const { const juce::ScopedLock sl (directLock); return directStatus; }
    void setDirectStatus (const DirectStatus& st) { const juce::ScopedLock sl (directLock); directStatus = st; }
    juce::String getDirectSourceId() const { return directSourceId; }   // "" = automatic (CABLE In 16ch), "host" = off
    void setDirectSourceId (const juce::String& id);

    // Output device ("" = automatic)
    juce::String getOutputDeviceId() const { return outputDeviceId; }
    void setOutputDeviceId (const juce::String& id);

    float getUiScale() const noexcept { return uiScale; }
    void setUiScale (float s);

    // BYPASS of the whole plugin: the host input goes straight back to the host output (unchanged),
    // the Atmos output plays silence (the stream stays open, so switching back is instant).
    // Also the VST3 bypass parameter, so the host's own bypass button does the same. 20 ms crossfade.
    bool isBypassed() const noexcept { return bypassParam->load() > 0.5f; }
    void setBypassed (bool shouldBypass);
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    // Clipping prevention (-1.0 dBFS, 12 channels linked, zero latency)
    bool isLimiterOn() const noexcept { return limiterParam->load() > 0.5f; }
    void setLimiterOn (bool on);
    float getLimiterGainDb() const noexcept { return limGainDb.load (std::memory_order_relaxed); }

    // Theme preset. The last chosen theme is also remembered for the whole PC user
    // (%APPDATA%\Atmos Bridge 7.1.4ch), so every new window opens with it.
    ab::ThemeId getTheme() const noexcept { return theme; }
    void setTheme (ab::ThemeId t);

    //========================================================================== upmixer slots (message thread)
    // Up to 4 VST3 upmixers. One of them (or none) is "on" (O); its output replaces the host channels.
    // B = bypass (the slot stays selected but host channels 1..12 pass straight through).
    // Every slot (and "no upmixer") remembers its own channel number table.
    struct SlotView
    {
        juce::String name, path, layout, error;
        bool loaded = false, namedLayout = false, bypass = false, active = false;
        double latencyMs = 0.0;
    };
    int getNumSlots() const noexcept { return (int) slots.size(); }
    SlotView getSlot (int index) const;
    int getActiveSlot() const noexcept { return activeSlot; }
    bool addUpmixer (const juce::String& path, juce::String& error); // new slot, switched on; false = not added
    void removeSlot (int index);
    void toggleSlotActive (int index);
    void toggleSlotBypass (int index);
    int getSlotHostIndex (int index) const noexcept;
    UpmixerHost& getHost (int hostIndex) noexcept { return *hosts[(size_t) hostIndex]; }
    juce::AudioPluginInstance* getSlotInstance (int index) const noexcept;
    double getUpmixerLatencyMs() const noexcept;    // latency of the upmixer that is running now
    int getSlotsVersion() const noexcept { return slotsVersion; } // changes whenever the slot list changes

    //========================================================================== host app (Atmos Bridge Host)
    // The standalone host runs this processor without a plugin host: the input is always the direct loopback
    // (all three modes), the chain holds stereo plugins and upmixers in any order, the output is Atmos or 2ch Stereo.
    // The plugin never calls any of this (hostApp stays false and nothing below changes its behaviour).
    void setHostApp (bool on);                       // before prepareToPlay
    bool isHostApp() const noexcept { return hostApp; }

    ab::OutputFormat getOutputFormat() const noexcept { return (ab::OutputFormat) outputFormat.load(); }
    void setOutputFormat (ab::OutputFormat f);
    int getExclusiveBufferFrames() const noexcept { return exclusiveFrames; }   // 2ch exclusive buffer at 48 kHz
    void setExclusiveBufferFrames (int frames);

    float getMasterDb() const noexcept { return masterDb.load(); }
    void setMasterDb (float db);
    bool isMasterMuted() const noexcept { return masterMute.load(); }
    void setMasterMuted (bool m);

    // Routing: CABLE In channel i (0..15) -> processing channel p (0..11), per-input gain (dB) and mute.
    bool isRouted (int in, int p) const noexcept { return ((routeMask[(size_t) p].load() >> in) & 1u) != 0; }
    void setRouted (int in, int p, bool on);
    float getInputGainDb (int in) const noexcept { return inGainDb[(size_t) in].load(); }
    void setInputGainDb (int in, float db);
    bool isInputMuted (int in) const noexcept { return inMute[(size_t) in].load(); }
    void setInputMuted (int in, bool m);
    void setRoutingPreset (int preset);              // 0 = 1:1, 1 = swap 1/2, 2 = mono (1+2 -> 1 and 2), 3 = clear
    int getRoutingVersion() const noexcept { return routingVersion; }

    // The chain: stereo plugins (2 -> 2) and upmixers (2 -> 12) in one list, processed top to bottom.
    // Before the running upmixer the chain carries channels 1-2; after it 12 (a stereo plugin there works on 1-2).
    struct ChainView
    {
        bool upmixer = false;
        juce::String name, path, error;
        bool loaded = false, bypass = false, active = true;
        int scope = 0;              // stereo plugins: 0 both formats, 1 2ch only, 2 Atmos only
        double latencyMs = 0.0;
        bool afterUpmixer = false;  // stereo plugin after the running upmixer
        bool idle = false;          // not used in the current output format
    };
    int getChainSize() const noexcept { return (int) chain.size(); }
    ChainView getChainItem (int pos) const;
    int getChainVersion() const noexcept { return chainVersion + slotsVersion; }
    bool addStereoPlugin (const juce::String& path, juce::String& error);
    void removeChainItem (int pos);
    void moveChainItem (int from, int to);
    void toggleChainBypass (int pos);
    void toggleChainActive (int pos);                // upmixers: O / X
    void cycleChainScope (int pos);                  // stereo plugins: both / 2ch only / Atmos only
    juce::AudioPluginInstance* getChainInstance (int pos) const noexcept;
    int getChainWindowKey (int pos) const noexcept;  // stable key per hosted plugin (0..3 upmixers, 4..7 stereo)
    UpmixerHost& getHostByKey (int key) noexcept     { return key < ab::kMaxUpmixers ? *hosts[(size_t) key] : *fxHosts[(size_t) (key - ab::kMaxUpmixers)]; }
    double getPluginLatencyMs() const noexcept;      // the chain in the current format
    double getOutputPeriodMs() const noexcept;       // 2ch: exclusive buffer, Atmos: spatial pass
    std::function<void()> onStateChanged;            // host app: something worth saving changed (message thread)
    std::function<void()> onHideToTray;              // host app: the window's tray button

    bool isOutputOwner() const noexcept { return ownsOutput.load (std::memory_order_acquire); }
    SharedOutput* getSharedOutput() const noexcept { return isOutputOwner() ? shared.get() : nullptr; }
    double getHostSampleRate() const noexcept { return currentSampleRate; }
    int getNumHostInputs() const noexcept { return numHostInputs.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void updateLatency();
    void tryClaimOutput();
    void releaseOutput();
    void updateChannelTable();
    void writeExtrasToState();
    void readExtrasFromState();
    void processChunk (const float* const* src, int numSrc, int num, float atmosGain0, float atmosGain1);
    void processSource (const float* const* src, int numSrc, int num); // BYPASS ramp + processChunk / silence
    void pushSilence (int num);
    void routeInputs (int numIn, int num);         // host app: loopback channels -> 12 processing channels
    void runChain (int num);                       // host app: the chain on inPtrs
    void rebuildChainSeq();
    void stateChanged();
    ab::OutputFeeder::AdaptPolicy wantedPolicy() const noexcept;
    int findFreeFxHost() const noexcept;
    int fxIndexForHost (int h) const noexcept;
    int slotIndexForHost (int h) const noexcept;
    void unloadAllFx();
    void writeHostState (juce::ValueTree& s);
    void readHostState (const juce::ValueTree& s);

    // Direct loopback input (Low latency / Ultra low latency+)
    void onDirectPacket (const float* interleaved, int frames, int channels, double lateMs, bool silent);
    void updateDirect();                                           // message thread, every timer tick
    void activateDirect();
    void deactivateDirect (const juce::String& reason, int retryMs);
    void setParam (const juce::String& id, float plainValue);

    using Map = std::array<int, ab::kNumChannels>;
    static Map defaultMap() noexcept { Map m {}; for (int c = 0; c < ab::kNumChannels; ++c) m[(size_t) c] = c; return m; }
    static juce::String mapToString (const Map& m);
    static Map mapFromString (const juce::String& s, const Map& fallback);
    Map liveMap() const noexcept;
    void applyLiveMap (const Map& m) noexcept;
    Map& activeMapMemory() noexcept;               // the table that belongs to the current choice
    void setActiveSlot (int index);
    void unloadAllSlots();
    int findFreeHost() const noexcept;
    void bumpSlots() noexcept { ++slotsVersion; }

    // Only one instance per process owns the HDMI output.
    static std::atomic<AtmosBridgeProcessor*> owner;
    static std::unique_ptr<SharedOutput> shared;
    std::atomic<bool> ownsOutput { false };

    std::array<std::atomic<float>*, ab::kNumChannels> muteParams {};
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    float atmosMix = 0.0f;    // processing thread: BYPASS ramp of the Atmos side (0 = active, 1 = bypassed)
    float hostMix = 0.0f;     // host audio thread: BYPASS ramp of the host pass-through

    std::array<std::atomic<int>, ab::kNumChannels> outMap;
    std::array<std::atomic<int>, ab::kMaxInputs> hostToJuce;   // host channel index -> JUCE buffer index
    std::atomic<int> numHostInputs { ab::kNumChannels };

    std::atomic<int> inputNoise { -1 }, positionNoise { -1 }, sequencePos { -1 };
    std::atomic<bool> sequenceOn { false };

    // Audio-thread state
    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    juce::AudioBuffer<float> inWork, posWork, noiseBuf;
    std::array<float*, ab::kNumChannels> inPtrs {}, posPtrs {};
    std::array<float, ab::kNumChannels> gainNow {}, inMeanSq {}, posMeanSq {};
    ab::NoiseTable noise;
    ab::LinkedLimiter limiter;
    bool limiterWasOn = true;
    std::atomic<float> limGainDb { 0.0f };
    double seqSamples = 0.0;
    int seqIndex = 0;
    bool seqWasOn = false;

    std::array<std::atomic<float>, ab::kNumChannels> inRms, inPeak, posRms, posPeak;

    // Upmixer slots. The hosts never move (the audio thread reads them through activeHost);
    // "slots" is the list shown in the editor, in display order.
    struct SlotEntry
    {
        int host = -1;
        juce::String path, name, pendingState, error;
        bool bypass = false;
        Map map = defaultMap();
    };
    std::array<std::unique_ptr<UpmixerHost>, ab::kMaxUpmixers> hosts;
    std::vector<SlotEntry> slots;
    int activeSlot = -1;
    Map directMap = defaultMap();                  // table used with no upmixer switched on
    std::atomic<int> activeHost { -1 };
    int slotsVersion = 0;

    // Host app: stereo plugins, the chain order (stable host indices) and its audio-thread copy.
    struct FxEntry
    {
        int host = -1;
        juce::String path, name, pendingState, error;
        bool bypass = false;
        int scope = 0;
    };
    struct ChainRef { bool upmixer = false; int host = -1; };
    std::array<std::unique_ptr<UpmixerHost>, ab::kMaxFx> fxHosts;
    std::vector<FxEntry> fxList;
    std::vector<ChainRef> chain;
    static constexpr int kMaxChain = ab::kMaxFx + ab::kMaxUpmixers;
    std::array<std::atomic<int>, kMaxChain> chainSeq;   // >= 0 stereo host, <= -2 upmixer host (-v - 2), -1 end
    int chainVersion = 0;
    juce::AudioBuffer<float> fxWork, routeBuf;
    std::array<const float*, ab::kNumChannels> routePtrs {};

    bool hostApp = false;
    std::atomic<int> outputFormat { 0 };
    int exclusiveFrames = 240;
    std::atomic<float> masterDb { 0.0f };
    std::atomic<bool> masterMute { false };
    float masterNow = 1.0f;
    std::array<std::atomic<uint32_t>, ab::kNumChannels> routeMask;
    std::array<std::atomic<float>, ab::kRouteInputs> inGainDb;
    std::array<std::atomic<bool>, ab::kRouteInputs> inMute;
    std::array<float, ab::kRouteInputs> inGainNow {};
    int routingVersion = 0;

    struct PendingFx { std::vector<FxEntry> fx; std::vector<std::pair<bool, int>> order; bool valid = false; };   // order: (upmixer, list index)
    struct PendingRestore { bool valid = false; std::vector<SlotEntry> slots; int active = -1; Map direct = defaultMap(); PendingFx host; };
    PendingRestore pendingRestore;   // restored state, applied on the message thread

    ab::ThemeId theme = ab::ThemeId::Cyberpunk;
    juce::String directSourceId;
    juce::CriticalSection directLock;
    DirectStatus directStatus;

    // Direct loopback: the capture thread runs the same processing as the host thread does otherwise.
    // procLock makes sure only one of them processes at a time (the host thread never waits for it).
    std::unique_ptr<ab::LoopbackCapture> loopback;
    std::atomic<bool> directActive { false };
    juce::SpinLock procLock;
    bool prepared = false;
    juce::AudioBuffer<float> directIn;
    std::array<const float*, ab::kMaxInputs> srcPtrs {};
    std::atomic<float> hostInPeak { 0.0f }, directInPeak { 0.0f };
    juce::String startedSource { "?" }, directReason;
    juce::uint32 directRetryAtMs = 0;
    int silentStrikes = 0, silentTick = 0;
    bool startedShort = false;          // loopback started with the short engine period request (Ultra+)
    int directPacketFrames = 0;         // packet length the feeder was prepared for
    int activePolicy = -1;              // OutputFeeder::AdaptPolicy of the active direct path

public:
   #if AB_TEST_HOOKS
    // Linux test harness only: drive the direct path without a Windows loopback device.
    bool testNoAutoDirect = false;
    int testHostToJuce (int i) const { return hostToJuce[(size_t) i].load(); }
    void testSetDirect (bool on) { testNoAutoDirect = true; if (on) activateDirect(); else deactivateDirect ("test", 0); }
    void testDirectPacket (const float* inter, int frames, int ch, double lateMs) { onDirectPacket (inter, frames, ch, lateMs, false); }
    bool testIsDirectActive() const noexcept { return directActive.load(); }
   #endif
private:
    juce::String outputDeviceId;
    float uiScale = 0.75f;
    int statTick = 0, latencyTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosBridgeProcessor)
};
