#pragma once

#include <JuceHeader.h>
#include "Shared.h"
#include "dsp/Dsp.h"
#include "dsp/OutputFeeder.h"
#include "engine/SpatialEngine.h"

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
                                   private juce::Timer
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

    // Output device ("" = automatic)
    juce::String getOutputDeviceId() const { return outputDeviceId; }
    void setOutputDeviceId (const juce::String& id);

    float getUiScale() const noexcept { return uiScale; }
    void setUiScale (float s);

    bool isOutputOwner() const noexcept { return ownsOutput.load (std::memory_order_acquire); }
    SharedOutput* getSharedOutput() const noexcept { return isOutputOwner() ? shared.get() : nullptr; }
    double getHostSampleRate() const noexcept { return currentSampleRate; }
    int getNumHostInputs() const noexcept { return numHostInputs.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void tryClaimOutput();
    void releaseOutput();
    void updateChannelTable();
    void writeExtrasToState();
    void readExtrasFromState();
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int num);
    void setParam (const juce::String& id, float plainValue);

    // Only one instance per process owns the HDMI output.
    static std::atomic<AtmosBridgeProcessor*> owner;
    static std::unique_ptr<SharedOutput> shared;
    std::atomic<bool> ownsOutput { false };

    std::array<std::atomic<float>*, ab::kNumChannels> muteParams {};
    std::atomic<float>* modeParam = nullptr;

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
    double seqSamples = 0.0;
    int seqIndex = 0;
    bool seqWasOn = false;

    std::array<std::atomic<float>, ab::kNumChannels> inRms, inPeak, posRms, posPeak;

    juce::String outputDeviceId;
    float uiScale = 0.9f;
    int statTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosBridgeProcessor)
};
