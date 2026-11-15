#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/Platform.h"
#include "ChannelOrder.h"

using namespace ab;

std::atomic<AtmosBridgeProcessor*> AtmosBridgeProcessor::owner { nullptr };
std::unique_ptr<SharedOutput> AtmosBridgeProcessor::shared;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AtmosBridgeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using juce::ParameterID;

    for (int c = 0; c < kNumChannels; ++c)
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { muteId (c), 1 },
                                                                "Mute Ch " + juce::String (c + 1), false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Latency Mode",
        juce::StringArray { "Music", "Low latency", "Ultra low latency" }, 0));
    return layout;
}

//==============================================================================
AtmosBridgeProcessor::AtmosBridgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::create7point1point4(), true)
                          .withOutput ("Output", juce::AudioChannelSet::create7point1point4(), true)),
      apvts (*this, nullptr, "AtmosBridge", createLayout())
{
    for (int c = 0; c < kNumChannels; ++c)
    {
        muteParams[(size_t) c] = apvts.getRawParameterValue (muteId (c));
        outMap[(size_t) c].store (c); // default: input n -> position n (L R C LFE SBL SBR SL SR TFL TFR TRL TRR)
        inRms[(size_t) c].store (0.0f);
        inPeak[(size_t) c].store (0.0f);
        posRms[(size_t) c].store (0.0f);
        posPeak[(size_t) c].store (0.0f);
    }
    modeParam = apvts.getRawParameterValue ("mode");

    for (int i = 0; i < kMaxInputs; ++i) hostToJuce[(size_t) i].store (i);
    updateChannelTable();
    writeExtrasToState();
    logLine ("Plugin instance created (" JucePlugin_Name " " JucePlugin_VersionString ")");
    tryClaimOutput();
    startTimer (500);
}

AtmosBridgeProcessor::~AtmosBridgeProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    upmixer.unload();
    releaseOutput();
}

//==============================================================================
void AtmosBridgeProcessor::tryClaimOutput()
{
    AtmosBridgeProcessor* expected = nullptr;
    if (! owner.compare_exchange_strong (expected, this))
        return;

    shared = std::make_unique<SharedOutput>();
    shared->feeder.producerPrepare (currentSampleRate, currentBlockSize);
    shared->feeder.setMode ((int) getMode());
    shared->engine.setDeviceId (outputDeviceId.toStdString());
    shared->engine.start();
    ownsOutput.store (true, std::memory_order_release);
    logLine ("This instance owns the spatial (HDMI) output");
}

void AtmosBridgeProcessor::releaseOutput()
{
    if (owner.load() != this) return;
    ownsOutput.store (false, std::memory_order_release);
    if (shared)
    {
        shared->engine.stop();
        shared.reset();
    }
    owner.store (nullptr);
    logLine ("Instance released the spatial output");
}

void AtmosBridgeProcessor::timerCallback()
{
    // If the owning instance was removed, another instance can take over.
    if (owner.load() == nullptr)
        tryClaimOutput();

    if (upmixer.refreshLatency())
        updateLatency();

    // Every 5 s while audio is flowing: one diagnostics line in the log.
    if (auto* s = getSharedOutput())
    {
        if (++statTick % 10 != 0) return;
        const auto pushes = s->feeder.takePushCount();
        if (pushes == 0 && ! s->feeder.isProducerAlive()) return;
        char line[400];
        std::snprintf (line, sizeof (line),
                       "stats/5s: blocks=%u host=%.0f Hz/%d mode=%d margin=%.0f ms fill=%.1f ms lat=%.1f ms drift=%+.0f ppm "
                       "drop=%u resync=%u ovf=%u dup=%u bursty=%d engine=%s period=%d memlock=%d",
                       pushes, s->feeder.getHostRate(), s->feeder.getHostBlock(), s->feeder.getMode(),
                       s->feeder.getMarginMs(), s->feeder.getFillMs(), (double) s->feeder.getLatencyMs(),
                       (double) s->feeder.getDriftPpm(), s->feeder.getDropouts(), s->feeder.getResyncs(),
                       s->feeder.getOverflows(), s->feeder.getDuplicates(), s->feeder.isProducerBursty() ? 1 : 0,
                       toString (s->engine.getStatus()), s->engine.getPeriodFrames(), s->engine.isMemoryLocked() ? 1 : 0);
        logLine (line);
    }
}

//==============================================================================
void AtmosBridgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    currentBlockSize = juce::jmax (1, samplesPerBlock);

    updateChannelTable();
    const int chunk = juce::jmax (512, currentBlockSize);
    inWork.setSize (kNumChannels, chunk, false, true, true);
    posWork.setSize (kNumChannels, chunk, false, true, true);
    for (int c = 0; c < kNumChannels; ++c)
    {
        inPtrs[(size_t) c] = inWork.getWritePointer (c);
        posPtrs[(size_t) c] = posWork.getWritePointer (c);
        gainNow[(size_t) c] = isMuted (c) ? 0.0f : 1.0f;
        inMeanSq[(size_t) c] = 0.0f;
        posMeanSq[(size_t) c] = 0.0f;
    }

    upmixer.prepare (currentSampleRate, chunk);
    updateLatency();

    // Pink noise is generated here, once (4 s loop), so the audio thread only copies it.
    noise.build (currentSampleRate, 4.0, juce::Decibels::decibelsToGain (kNoiseDbfs), kLfeNoiseCutHz);
    seqSamples = 0.0;

    if (auto* s = getSharedOutput())
        s->feeder.producerPrepare (currentSampleRate, currentBlockSize);

    logLine ("prepareToPlay: " + std::to_string (currentSampleRate) + " Hz, block " + std::to_string (currentBlockSize));
}

void AtmosBridgeProcessor::releaseResources() {}

bool AtmosBridgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Default is 7.1.4 in / 7.1.4 out, but accept what the host offers (e.g. discrete 12 channels):
    // input numbers always refer to host channels, and the plugin's own output is always silent.
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return ! in.isDisabled() && in.size() >= 1 && in.size() <= kMaxInputs && out.size() <= kMaxInputs;
}

void AtmosBridgeProcessor::processorLayoutsChanged()
{
    updateChannelTable();
}

void AtmosBridgeProcessor::updateChannelTable()
{
    const auto set = getChannelLayoutOfBus (true, 0);
    const auto order = hostChannelOrder (set);
    const int n = juce::jmin ((int) order.size(), kMaxInputs);
    for (int i = 0; i < kMaxInputs; ++i)
    {
        int idx = i < n ? set.getChannelIndexForType (order[(size_t) i]) : -1;
        if (i < n && idx < 0) idx = i;
        hostToJuce[(size_t) i].store (idx);
    }
    numHostInputs.store (n);
    logLine ("Input layout: " + set.getDescription().toStdString() + " (" + std::to_string (n) + " ch)");
}

//==============================================================================
void AtmosBridgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int chunk = inWork.getNumSamples();

    if (chunk > 0)
        for (int start = 0; start < total; start += chunk)
            processChunk (buffer, start, juce::jmin (chunk, total - start));

    // The audio leaves through Windows Spatial Audio (HDMI) only: the host gets silence,
    // so nothing is played twice. The host output only provides the clock.
    buffer.clear();
}

void AtmosBridgeProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n)
{
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const double sr = currentSampleRate;

    // Test signals
    const int iNoise = inputNoise.load (std::memory_order_relaxed);
    int pNoise = positionNoise.load (std::memory_order_relaxed);
    const bool seqOn = sequenceOn.load (std::memory_order_relaxed);
    if (seqOn)
    {
        if (! seqWasOn) { seqSamples = 0.0; seqIndex = 0; }
        seqSamples += n;
        if (seqSamples >= kSequenceSeconds * sr)
        {
            seqSamples = 0.0;
            seqIndex = (seqIndex + 1) % kNumChannels;
        }
        sequencePos.store (seqIndex, std::memory_order_relaxed);
        pNoise = seqIndex;
    }
    else if (seqWasOn)
    {
        sequencePos.store (-1, std::memory_order_relaxed);
    }
    seqWasOn = seqOn;
    const bool testActive = iNoise >= 0 || pNoise >= 0;

    // 1) Inputs (program audio is silenced while a test signal plays)
    //    - built-in upmixer active: host channels 1-2 -> upmixer -> inputs 1..12 (L R C LFE SBL SBR SL SR TFL TFR TRL TRR)
    //    - otherwise: host channel n -> input n
    if (testActive)
    {
        for (int i = 0; i < kNumChannels; ++i)
        {
            float* dst = inPtrs[(size_t) i];
            if (i == iNoise)
                noise.read (dst, n, outMap[(size_t) i].load (std::memory_order_relaxed) == kLfePos);
            else
                juce::FloatVectorOperations::clear (dst, n);
        }
    }
    else if (upmixer.isActive())
    {
        auto hostPtr = [&] (int i) -> const float*
        {
            const int src = hostToJuce[(size_t) i].load (std::memory_order_relaxed);
            return (src >= 0 && src < numIn) ? buffer.getReadPointer (src, start) : nullptr;
        };
        if (! upmixer.process (hostPtr (0), hostPtr (1), inPtrs.data(), n))
            for (int i = 0; i < kNumChannels; ++i)
                juce::FloatVectorOperations::clear (inPtrs[(size_t) i], n); // upmixer being swapped
    }
    else
    {
        for (int i = 0; i < kNumChannels; ++i)
        {
            float* dst = inPtrs[(size_t) i];
            const int src = hostToJuce[(size_t) i].load (std::memory_order_relaxed);
            if (src >= 0 && src < numIn)
                juce::FloatVectorOperations::copy (dst, buffer.getReadPointer (src, start), n);
            else
                juce::FloatVectorOperations::clear (dst, n);
        }
    }

    // 2) Mute with a 10 ms ramp (also applies to the input test noise)
    const float step = (float) (1.0 / (0.010 * sr));
    for (int i = 0; i < kNumChannels; ++i)
    {
        const float target = muteParams[(size_t) i]->load (std::memory_order_relaxed) > 0.5f ? 0.0f : 1.0f;
        float g = gainNow[(size_t) i];
        float* dst = inPtrs[(size_t) i];
        if (g == target)
        {
            if (g == 0.0f) juce::FloatVectorOperations::clear (dst, n);
            continue;
        }
        for (int k = 0; k < n; ++k)
        {
            g = (target > g) ? juce::jmin (target, g + step) : juce::jmax (target, g - step);
            dst[k] *= g;
        }
        gainNow[(size_t) i] = g;
    }

    // 3) Mapping: input i -> position outMap[i] (0 dB, no level change; duplicates are summed)
    for (int p = 0; p < kNumChannels; ++p)
        juce::FloatVectorOperations::clear (posPtrs[(size_t) p], n);
    for (int i = 0; i < kNumChannels; ++i)
    {
        const int p = outMap[(size_t) i].load (std::memory_order_relaxed);
        if (p >= 0 && p < kNumChannels)
            juce::FloatVectorOperations::add (posPtrs[(size_t) p], inPtrs[(size_t) i], n);
    }

    // 4) Position test noise (speaker click / sequential test): straight to the position
    if (pNoise >= 0 && pNoise < kNumChannels)
    {
        for (int p = 0; p < kNumChannels; ++p)
            juce::FloatVectorOperations::clear (posPtrs[(size_t) p], n);
        noise.read (posPtrs[(size_t) pNoise], n, pNoise == kLfePos);
    }
    noise.advance (n);

    // 5) Meters (~80 ms RMS)
    const float a = (float) std::exp (-(double) n / (0.08 * sr));
    auto meter = [n, a] (const float* d, float& meanSq, std::atomic<float>& rms, std::atomic<float>& peak)
    {
        float pk = 0.0f, ss = 0.0f;
        for (int k = 0; k < n; ++k) { const float v = d[k]; pk = juce::jmax (pk, std::abs (v)); ss += v * v; }
        meanSq = meanSq * a + (1.0f - a) * (ss / (float) n);
        rms.store (std::sqrt (meanSq), std::memory_order_relaxed);
        float prev = peak.load (std::memory_order_relaxed);
        while (pk > prev && ! peak.compare_exchange_weak (prev, pk)) {}
    };
    for (int c = 0; c < kNumChannels; ++c)
    {
        meter (inPtrs[(size_t) c], inMeanSq[(size_t) c], inRms[(size_t) c], inPeak[(size_t) c]);
        meter (posPtrs[(size_t) c], posMeanSq[(size_t) c], posRms[(size_t) c], posPeak[(size_t) c]);
    }

    // 6) Hand over to the spatial output thread (not while rendering offline)
    if (ownsOutput.load (std::memory_order_acquire) && ! isNonRealtime())
    {
        shared->feeder.setMode ((int) getMode());
        shared->feeder.producerPush (posPtrs.data(), n);
    }
}

//==============================================================================
AtmosBridgeProcessor::Meter AtmosBridgeProcessor::takeInputMeter (int ch) noexcept
{
    return { inRms[(size_t) ch].load(), inPeak[(size_t) ch].exchange (0.0f) };
}

AtmosBridgeProcessor::Meter AtmosBridgeProcessor::takePositionMeter (int p) noexcept
{
    return { posRms[(size_t) p].load(), posPeak[(size_t) p].exchange (0.0f) };
}

//==============================================================================
void AtmosBridgeProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void AtmosBridgeProcessor::setPosition (int input, int position)
{
    if (input < 0 || input >= kNumChannels) return;
    outMap[(size_t) input].store (juce::jlimit (-1, kNumChannels - 1, position));
    writeExtrasToState();
}

void AtmosBridgeProcessor::resetMapping()
{
    for (int c = 0; c < kNumChannels; ++c) outMap[(size_t) c].store (c);
    writeExtrasToState();
    logLine ("Mapping reset to default");
}

bool AtmosBridgeProcessor::isDuplicatePosition (int position) const noexcept
{
    if (position < 0) return false;
    int n = 0;
    for (auto& m : outMap) if (m.load() == position) ++n;
    return n > 1;
}

void AtmosBridgeProcessor::setMuted (int input, bool shouldMute) { setParam (muteId (input), shouldMute ? 1.0f : 0.0f); }

bool AtmosBridgeProcessor::areAllMuted() const noexcept
{
    for (int c = 0; c < kNumChannels; ++c)
        if (! isMuted (c)) return false;
    return true;
}

void AtmosBridgeProcessor::setAllMuted (bool shouldMute)
{
    for (int c = 0; c < kNumChannels; ++c) setMuted (c, shouldMute);
}

void AtmosBridgeProcessor::toggleInputNoise (int input)
{
    sequenceOn.store (false);
    positionNoise.store (-1);
    inputNoise.store (inputNoise.load() == input ? -1 : input);
}

void AtmosBridgeProcessor::togglePositionNoise (int position)
{
    sequenceOn.store (false);
    inputNoise.store (-1);
    positionNoise.store (positionNoise.load() == position ? -1 : position);
}

void AtmosBridgeProcessor::toggleSequence()
{
    inputNoise.store (-1);
    positionNoise.store (-1);
    sequenceOn.store (! sequenceOn.load());
}

void AtmosBridgeProcessor::stopTests()
{
    sequenceOn.store (false);
    inputNoise.store (-1);
    positionNoise.store (-1);
}

void AtmosBridgeProcessor::setMode (LatencyMode m)
{
    setParam ("mode", (float) (int) m);
    logLine ("Latency mode: " + std::to_string ((int) m));
}

void AtmosBridgeProcessor::setOutputDeviceId (const juce::String& id)
{
    outputDeviceId = id;
    if (auto* s = getSharedOutput())
        s->engine.setDeviceId (id.toStdString());
    writeExtrasToState();
    logLine ("Output device set to: " + (id.isEmpty() ? std::string ("<auto>") : id.toStdString()));
}

void AtmosBridgeProcessor::setUiScale (float s)
{
    uiScale = juce::jlimit (0.5f, 1.5f, s);
    writeExtrasToState();
}

//==============================================================================
bool AtmosBridgeProcessor::loadUpmixer (const juce::String& path)
{
    pendingUpmixer.valid = false;
    if (! upmixer.load (path)) return false;
    upmixer.setEnabled (true);
    if (upmixer.getInfo().namedLayout)
        resetMapping(); // channels already arrive in L R C LFE SBL SBR SL SR TFL TFR TRL TRR order
    writeExtrasToState();
    updateLatency();
    return true;
}

void AtmosBridgeProcessor::unloadUpmixer()
{
    pendingUpmixer.valid = false;
    upmixer.unload();
    writeExtrasToState();
    updateLatency();
}

void AtmosBridgeProcessor::setUpmixerEnabled (bool shouldBeEnabled)
{
    upmixer.setEnabled (shouldBeEnabled);
    writeExtrasToState();
    updateLatency();
    logLine (std::string ("Upmixer ") + (shouldBeEnabled ? "enabled" : "bypassed"));
}

double AtmosBridgeProcessor::getUpmixerLatencyMs() const noexcept
{
    return upmixer.isActive() ? 1000.0 * upmixer.getInfo().latencySamples / currentSampleRate : 0.0;
}

void AtmosBridgeProcessor::updateLatency()
{
    setLatencySamples (upmixer.isActive() ? upmixer.getInfo().latencySamples : 0);
}

void AtmosBridgeProcessor::handleAsyncUpdate()
{
    if (! pendingUpmixer.valid) return;
    const auto p = pendingUpmixer;
    pendingUpmixer.valid = false;

    if (p.path.isEmpty())
    {
        upmixer.unload();
    }
    else
    {
        if (! upmixer.getInfo().loaded || upmixer.getInfo().path != p.path)
            upmixer.load (p.path);
        if (upmixer.getInfo().loaded)
            upmixer.setStateBase64 (p.state);
    }
    upmixer.setEnabled (p.enabled);
    updateLatency();
}

//==============================================================================
void AtmosBridgeProcessor::writeExtrasToState()
{
    juce::StringArray map;
    for (int c = 0; c < kNumChannels; ++c)
        map.add (juce::String (outMap[(size_t) c].load() + 1)); // 1..12, 0 = off

    auto& s = apvts.state;
    s.setProperty ("map", map.joinIntoString (","), nullptr);
    s.setProperty ("outputDevice", outputDeviceId, nullptr);
    s.setProperty ("uiScale", uiScale, nullptr);
    s.setProperty ("upmixerPath", upmixer.getInfo().loaded ? upmixer.getInfo().path
                                                           : (pendingUpmixer.valid ? pendingUpmixer.path : juce::String()), nullptr);
    s.setProperty ("upmixerEnabled", upmixer.isEnabled(), nullptr);
    s.setProperty ("stateVersion", 2, nullptr);
}

void AtmosBridgeProcessor::readExtrasFromState()
{
    auto& s = apvts.state;
    auto map = juce::StringArray::fromTokens (s.getProperty ("map", "").toString(), ",", "");
    if (map.size() == kNumChannels)
        for (int c = 0; c < kNumChannels; ++c)
            outMap[(size_t) c].store (juce::jlimit (-1, kNumChannels - 1, map[c].getIntValue() - 1));

    uiScale = juce::jlimit (0.5f, 1.5f, (float) s.getProperty ("uiScale", 0.9f));

    pendingUpmixer.path = s.getProperty ("upmixerPath", "").toString();
    pendingUpmixer.state = s.getProperty ("upmixerState", "").toString();
    pendingUpmixer.enabled = (bool) s.getProperty ("upmixerEnabled", true);
    pendingUpmixer.valid = true;
    triggerAsyncUpdate(); // plugins must be loaded on the message thread

    const juce::String dev = s.getProperty ("outputDevice", "").toString();
    if (dev != outputDeviceId)
    {
        outputDeviceId = dev;
        if (auto* sh = getSharedOutput()) sh->engine.setDeviceId (dev.toStdString());
    }
}

void AtmosBridgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    writeExtrasToState();
    auto state = apvts.copyState();
    // The hosted upmixer's own settings travel with ours.
    state.setProperty ("upmixerState", upmixer.getInfo().loaded ? upmixer.getStateBase64()
                                                               : (pendingUpmixer.valid ? pendingUpmixer.state : juce::String()), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AtmosBridgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            readExtrasFromState();
            logLine ("State restored");
        }
}

//==============================================================================
juce::AudioProcessorEditor* AtmosBridgeProcessor::createEditor()
{
    return new AtmosBridgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AtmosBridgeProcessor();
}
