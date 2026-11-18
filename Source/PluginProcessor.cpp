#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/Platform.h"
#include "ChannelOrder.h"

using namespace ab;

std::atomic<AtmosBridgeProcessor*> AtmosBridgeProcessor::owner { nullptr };
std::unique_ptr<SharedOutput> AtmosBridgeProcessor::shared;

namespace
{
// Per-user settings file: %APPDATA%/Atmos Bridge 7.1.4ch/AtmosBridge.settings
// (the file name must not contain the product name: its ".4ch" would be taken as the extension)
juce::PropertiesFile::Options userSettingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "AtmosBridge";
    o.folderName = JucePlugin_Name;
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    return o;
}

int readUserTheme()
{
    juce::PropertiesFile f (userSettingsOptions());
    const int t = f.getIntValue ("theme", -1);
    return (t >= 0 && t < kNumThemes) ? t : -1;
}

void writeUserTheme (int t)
{
    juce::PropertiesFile f (userSettingsOptions());
    f.setValue ("theme", t);
    if (! f.saveIfNeeded())
        logLine ("Could not save the theme to " + f.getFile().getFullPathName().toStdString());
}
} // namespace

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
        juce::StringArray { "Default", "Low latency", "Ultra low latency+" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "limiter", 2 }, "Clipping Prevention", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "bypass", 3 }, "Bypass", false));
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
    limiterParam = apvts.getRawParameterValue ("limiter");
    bypassParam = apvts.getRawParameterValue ("bypass");
    for (auto& h : hosts) h = std::make_unique<UpmixerHost>();

    const int userTheme = readUserTheme();
    if (userTheme >= 0) theme = (ThemeId) userTheme;

    for (int i = 0; i < kMaxInputs; ++i) hostToJuce[(size_t) i].store (i);
    updateChannelTable();
    writeExtrasToState();
    logLine ("Plugin instance created (" JucePlugin_Name " " JucePlugin_VersionString ")");
    tryClaimOutput();
    startTimer (100);
}

AtmosBridgeProcessor::~AtmosBridgeProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    directActive.store (false);
    if (loopback) loopback->stop();
    activeHost.store (-1);
    for (auto& h : hosts) { h->beforeUnload = nullptr; h->unload(); }
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

    if (++latencyTick % 5 == 0)
    {
        bool latencyChanged = false;
        for (auto& h : hosts) latencyChanged = h->refreshLatency() || latencyChanged;
        if (latencyChanged) updateLatency();
    }

   #if AB_TEST_HOOKS
    if (! testNoAutoDirect)
   #endif
        updateDirect();

    // Every 5 s while audio is flowing: one diagnostics line in the log.
    if (auto* s = getSharedOutput())
    {
        if (++statTick % 50 != 0) return;
        const auto pushes = s->feeder.takePushCount();
        if (pushes == 0 && ! s->feeder.isProducerAlive()) return;
        char line[512];
        std::snprintf (line, sizeof (line),
                       "stats/5s: blocks=%u host=%.0f Hz/%d mode=%d margin=%.0f ms fill=%.1f ms lat=%.1f ms drift=%+.0f ppm "
                       "drop=%u resync=%u ovf=%u dup=%u bursty=%d engine=%s period=%d memlock=%d direct=%d jit=%.1f ms conceal=%u pkt=%d short=%d",
                       pushes, s->feeder.getHostRate(), s->feeder.getHostBlock(), s->feeder.getMode(),
                       s->feeder.getMarginMs(), s->feeder.getFillMs(), (double) s->feeder.getLatencyMs(),
                       (double) s->feeder.getDriftPpm(), s->feeder.getDropouts(), s->feeder.getResyncs(),
                       s->feeder.getOverflows(), s->feeder.getDuplicates(), s->feeder.isProducerBursty() ? 1 : 0,
                       toString (s->engine.getStatus()), s->engine.getPeriodFrames(), s->engine.isMemoryLocked() ? 1 : 0,
                       directActive.load() ? 1 : 0, s->feeder.getMaxJitterMs(), s->feeder.getConcealed(),
                       loopback ? loopback->getPacketFrames() : 0, loopback && loopback->isShortPeriodActive() ? 1 : 0);
        logLine (line);
    }
}

//==============================================================================
void AtmosBridgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType sl (procLock); // the loopback thread may be processing
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    currentBlockSize = juce::jmax (1, samplesPerBlock);

    updateChannelTable();
    const int chunk = juce::jmax (512, currentBlockSize);
    inWork.setSize (kNumChannels, chunk, false, true, true);
    posWork.setSize (kNumChannels, chunk, false, true, true);
    directIn.setSize (kMaxInputs, chunk, false, true, true);
    for (int c = 0; c < kNumChannels; ++c)
    {
        inPtrs[(size_t) c] = inWork.getWritePointer (c);
        posPtrs[(size_t) c] = posWork.getWritePointer (c);
        gainNow[(size_t) c] = isMuted (c) ? 0.0f : 1.0f;
        inMeanSq[(size_t) c] = 0.0f;
        posMeanSq[(size_t) c] = 0.0f;
    }

    for (auto& h : hosts) h->prepare (currentSampleRate, chunk);
    limiter.prepare (currentSampleRate, kLimiterCeilingDb);
    updateLatency();

    // Pink noise is generated here, once (4 s loop), so the audio thread only copies it.
    noise.build (currentSampleRate, 4.0, juce::Decibels::decibelsToGain (kNoiseDbfs), kLfeNoiseCutHz);
    seqSamples = 0.0;

    if (auto* s = getSharedOutput())
        s->feeder.producerPrepare (currentSampleRate, directActive.load() ? (directPacketFrames > 0 ? directPacketFrames
                                                                                                    : juce::roundToInt (currentSampleRate / 100.0))
                                                                          : currentBlockSize);
    prepared = true;

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
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());

    // BYPASS crossfade on the host side (20 ms): the original fades in here while the Atmos side fades out.
    const float target = bypassParam->load (std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f;
    const float step = (float) (1.0 / (0.020 * currentSampleRate));
    const float hostStart = hostMix;
    hostMix = target > hostMix ? juce::jmin (target, hostMix + step * (float) total)
                               : juce::jmax (target, hostMix - step * (float) total);

    if (directActive.load (std::memory_order_acquire))
    {
        // Direct loopback (Low / Ultra+): the Atmos side runs on the loopback thread. Here only the host input level
        // is taken (to notice a loopback that stays silent while the host hears sound).
        float pk = 0.0f;
        for (int c = 0; c < juce::jmin (2, numIn); ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, total));
        float prev = hostInPeak.load (std::memory_order_relaxed);
        while (pk > prev && ! hostInPeak.compare_exchange_weak (prev, pk)) {}
    }
    else if (chunk > 0)
    {
        const juce::SpinLock::ScopedTryLockType tl (procLock); // never wait on the host audio thread
        if (tl.isLocked())
        {
            const int nHost = juce::jmin (numHostInputs.load (std::memory_order_relaxed), kMaxInputs);
            for (int start = 0; start < total; start += chunk)
            {
                const int n = juce::jmin (chunk, total - start);
                for (int i = 0; i < nHost; ++i)
                {
                    const int idx = hostToJuce[(size_t) i].load (std::memory_order_relaxed);
                    srcPtrs[(size_t) i] = (idx >= 0 && idx < numIn) ? buffer.getReadPointer (idx, start) : nullptr;
                }
                processSource (srcPtrs.data(), nHost, n);
            }
        }
    }

    // Normally the audio leaves through Windows Spatial Audio (HDMI) only: the host gets silence,
    // so nothing is played twice (the host output only provides the clock).
    // BYPASS: the host input passes through unchanged.
    for (int c = numIn; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, total);
    if (hostStart <= 0.0f && hostMix <= 0.0f)
        buffer.clear();
    else if (! (hostStart >= 1.0f && hostMix >= 1.0f))
        for (int c = 0; c < numIn; ++c) buffer.applyGainRamp (c, 0, total, hostStart, hostMix);
}

void AtmosBridgeProcessor::processSource (const float* const* src, int numSrc, int n)
{
    // BYPASS crossfade on the Atmos side (20 ms)
    const float target = bypassParam->load (std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f;
    const float step = (float) (1.0 / (0.020 * currentSampleRate));
    const float m0 = atmosMix;
    const float m1 = target > m0 ? juce::jmin (target, m0 + step * (float) n) : juce::jmax (target, m0 - step * (float) n);
    atmosMix = m1;
    if (m0 >= 1.0f && m1 >= 1.0f)
        pushSilence (n);                         // fully bypassed: keep the Atmos stream fed with silence
    else
        processChunk (src, numSrc, n, 1.0f - m0, 1.0f - m1);
}

//==============================================================================
void AtmosBridgeProcessor::onDirectPacket (const float* inter, int frames, int ch, double lateMs, bool silent)
{
    // Level of the loopback (first two channels) for the silent-loopback check
    float pk = 0.0f;
    if (! silent)
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < juce::jmin (2, ch); ++c) pk = juce::jmax (pk, std::abs (inter[(size_t) f * (size_t) ch + (size_t) c]));
    float prev = directInPeak.load (std::memory_order_relaxed);
    while (pk > prev && ! directInPeak.compare_exchange_weak (prev, pk)) {}

    if (! directActive.load (std::memory_order_acquire)) return;
    const juce::SpinLock::ScopedLockType sl (procLock);
    if (! directActive.load (std::memory_order_acquire) || ! prepared) return;
    auto* s = getSharedOutput();
    if (s == nullptr) return;

    s->feeder.producerReportLateness (lateMs);
    const int chunk = directIn.getNumSamples();
    const int nch = juce::jmin (ch, kMaxInputs, directIn.getNumChannels());
    for (int off = 0; off < frames; off += chunk)
    {
        const int n = juce::jmin (chunk, frames - off);
        for (int c = 0; c < nch; ++c)
        {
            float* d = directIn.getWritePointer (c);
            const float* p = inter + (size_t) off * (size_t) ch + (size_t) c;
            for (int i = 0; i < n; ++i) d[i] = p[(size_t) i * (size_t) ch];
            srcPtrs[(size_t) c] = d;
        }
        processSource (srcPtrs.data(), nch, n);
    }
}

void AtmosBridgeProcessor::activateDirect()
{
    auto* s = getSharedOutput();
    if (s == nullptr) return;
    const auto policy = getMode() == LatencyMode::Ultra ? OutputFeeder::AdaptPolicy::Fast : OutputFeeder::AdaptPolicy::Safe;
    const int seen = loopback ? loopback->getPacketFrames() : 0;
    const int pkt = seen > 0 ? seen : juce::roundToInt (currentSampleRate / 100.0);
    {
        const juce::SpinLock::ScopedLockType sl (procLock); // waits for a host block in progress
        s->feeder.producerPrepare (currentSampleRate, pkt);
        s->feeder.setAdaptive (true, policy);
        directPacketFrames = pkt;
        activePolicy = (int) policy;
        directActive.store (true, std::memory_order_release);
    }
    silentStrikes = 0;
    directReason.clear();
    logLine (std::string ("Direct loopback ON (") + (policy == OutputFeeder::AdaptPolicy::Fast ? "Ultra low latency+" : "Low latency")
             + ", packet " + std::to_string (pkt) + " frames, " + (loopback ? loopback->getDeviceName() : std::string()) + ")");
}

void AtmosBridgeProcessor::deactivateDirect (const juce::String& reason, int retryMs)
{
    const bool was = directActive.load();
    {
        const juce::SpinLock::ScopedLockType sl (procLock); // waits for a loopback packet in progress
        directActive.store (false, std::memory_order_release);
        if (auto* s = getSharedOutput())
        {
            s->feeder.setAdaptive (false);
            s->feeder.producerPrepare (currentSampleRate, currentBlockSize);
        }
    }
    directRetryAtMs = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, retryMs);
    activePolicy = -1;
    if (was) logLine ("Direct loopback OFF -> host input (" + reason.toStdString() + ")");
}

void AtmosBridgeProcessor::updateDirect()
{
    using DS = DirectStatus;
    auto* s = getSharedOutput();
    const auto mode = getMode();
    const bool want = s != nullptr && prepared && mode != LatencyMode::Music && directSourceId != "host";
    const bool wantShort = mode == LatencyMode::Ultra;
    const auto policy = wantShort ? OutputFeeder::AdaptPolicy::Fast : OutputFeeder::AdaptPolicy::Safe;

    DirectStatus st;
    if (! want)
    {
        if (directActive.load()) deactivateDirect ("mode without direct loopback", 0);
        if (loopback && loopback->isStarted()) { loopback->stop(); logLine ("Direct loopback stopped"); }
        startedSource = "?";
        directReason.clear();
        setDirectStatus (st);
        return;
    }

    if (loopback == nullptr) loopback = std::make_unique<LoopbackCapture>();
    if (! loopback->isStarted() || startedSource != directSourceId || startedShort != wantShort)
    {
        if (directActive.load()) deactivateDirect (startedShort != wantShort ? "latency mode changed" : "input device changed", 0);
        loopback->start (directSourceId.toStdString(), s->engine.getDeviceName(),
                         [this] (const float* d, int f, int c, double late, bool silent) { onDirectPacket (d, f, c, late, silent); },
                         wantShort);
        startedSource = directSourceId;
        startedShort = wantShort;
        directRetryAtMs = 0;
        silentStrikes = 0;
    }

    const auto ls = loopback->getStatus();
    juce::String problem;
    int retryMs = 3000;
    bool connecting = false;
    const auto tr = [] (const char* u) { return juce::String::fromUTF8 (u); };
    if (ls == LoopbackStatus::Unsupported)        problem = tr ("이 시스템에서는 직접 루프백을 쓸 수 없음");
    else if (ls == LoopbackStatus::NoDevice)      problem = tr ("입력 장치(CABLE In 16ch)를 찾지 못함");
    else if (ls == LoopbackStatus::Error)         problem = tr ("루프백 오류: ") + juce::String (loopback->getLastError());
    else if (ls != LoopbackStatus::Running)       { problem = tr ("연결 중"); connecting = true; }
    else if (loopback->isPeriodTrialPending())    { problem = tr ("CABLE In 짧은 주기 시험 중 (3초)"); connecting = true; }
    else if (std::abs (loopback->getSampleRate() - currentSampleRate) > 1.0)
        problem = tr ("샘플레이트가 호스트와 다름 (") + juce::String (loopback->getSampleRate(), 0) + " Hz)";
    else if (LoopbackCapture::nowUs() - loopback->getLastPacketUs() > 300000)
        problem = tr ("루프백 데이터가 끊김");

    // Once a second: the host hears sound but the loopback stays digitally silent (e.g. exclusive-mode player)
    if (++silentTick >= 10)
    {
        silentTick = 0;
        const float hostPk = hostInPeak.exchange (0.0f), dirPk = directInPeak.exchange (0.0f);
        silentStrikes = (directActive.load() && hostPk > 0.003f && dirPk < 1.0e-6f) ? silentStrikes + 1 : 0;
    }
    if (problem.isEmpty() && silentStrikes >= 2)
    {
        problem = tr ("루프백에 소리가 없음 (플레이어가 독점 모드?)");
        retryMs = 10000;
        silentStrikes = 0;
    }

    if (problem.isNotEmpty())
    {
        if (directActive.load()) deactivateDirect (problem, retryMs);
        directReason = problem;
    }
    else if (! directActive.load() && juce::Time::getMillisecondCounter() >= directRetryAtMs)
    {
        activateDirect();
    }
    else if (directActive.load())
    {
        if (activePolicy != (int) policy)
            activateDirect();                                   // policy follows the mode
        // The packet length can change once the short engine period takes effect (or is given up)
        const int pf = loopback->getPacketFrames();
        if (pf > 0 && pf != directPacketFrames)
        {
            s->feeder.producerPrepare (currentSampleRate, pf);
            logLine ("Direct loopback: packet length " + std::to_string (directPacketFrames) + " -> " + std::to_string (pf) + " frames");
            directPacketFrames = pf;
        }
    }

    st.source = juce::String::fromUTF8 (loopback->getDeviceName().c_str());
    st.reason = directReason;
    st.marginMs = s->feeder.getMarginMs();
    st.packetMs = loopback->getPacketMs();
    st.shortPeriod = wantShort && loopback->isShortPeriodActive();
    if (wantShort)
    {
        using PN = LoopbackCapture::PeriodNote;
        switch (loopback->getPeriodNote())
        {
            case PN::Active:      st.periodNote = st.shortPeriod ? juce::String() : tr ("짧은 주기 확인 중"); break;
            case PN::Unsupported: st.periodNote = tr ("드라이버가 짧은 주기 미지원"); break;
            case PN::Locked:      st.periodNote = tr ("다른 앱이 주기를 고정함"); break;
            case PN::NoEffect:    st.periodNote = tr ("패킷이 짧아지지 않아 기본 주기로 다시 열고, 다시 요청 안 함"); break;
            case PN::Failed:      st.periodNote = tr ("짧은 주기 요청 실패"); break;
            case PN::SkippedVirtual: st.periodNote = tr ("VB-Cable은 짧은 주기를 지원하지 않아 요청 안 함"); break;
            case PN::SkippedKnown:   st.periodNote = tr ("이 장치는 짧은 주기 효과가 없어 요청 안 함"); break;
            case PN::NotRequested: default: break;
        }
    }
    if (directActive.load())
    {
        st.settleSec = s->feeder.getSettleRemainingSec();
        st.state = st.settleSec > 0.0 ? DS::Settling : DS::Running;
        st.latencyMs = s->feeder.getState() == OutputFeeder::State::Running ? (double) s->feeder.getLatencyMs() : 0.0;
    }
    else
    {
        st.state = connecting ? DS::Settling : DS::Fallback;
        st.settleSec = connecting ? OutputFeeder::paramsFor (policy).settleS : 0.0;
    }
    setDirectStatus (st);
}

void AtmosBridgeProcessor::pushSilence (int n)
{
    for (int c = 0; c < kNumChannels; ++c)
    {
        juce::FloatVectorOperations::clear (posPtrs[(size_t) c], n);
        inRms[(size_t) c].store (0.0f, std::memory_order_relaxed);
        posRms[(size_t) c].store (0.0f, std::memory_order_relaxed);
        inMeanSq[(size_t) c] = posMeanSq[(size_t) c] = 0.0f;
    }
    limGainDb.store (0.0f, std::memory_order_relaxed);
    if (ownsOutput.load (std::memory_order_acquire) && ! isNonRealtime())
    {
        shared->feeder.setMode ((int) getMode());
        shared->feeder.producerPush (posPtrs.data(), n);
    }
}

void AtmosBridgeProcessor::processChunk (const float* const* src, int numSrc, int n, float atmosGain0, float atmosGain1)
{
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
    //    - an upmixer slot is on (and not bypassed): host channels 1-2 -> upmixer -> inputs 1..12
    //      (L R C LFE SBL SBR SL SR TFL TFR TRL TRR)
    //    - otherwise: host channel n -> input n
    //    (src = host channels, or the loopback channels with the direct loopback input)
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
    else if (const int ah = activeHost.load (std::memory_order_acquire); ah >= 0 && hosts[(size_t) ah]->isActive())
    {
        const float* l = numSrc > 0 ? src[0] : nullptr;
        const float* r = numSrc > 1 ? src[1] : nullptr;
        if (! hosts[(size_t) ah]->process (l, r, inPtrs.data(), n))
            for (int i = 0; i < kNumChannels; ++i)
                juce::FloatVectorOperations::clear (inPtrs[(size_t) i], n); // upmixer being swapped
    }
    else
    {
        for (int i = 0; i < kNumChannels; ++i)
        {
            float* dst = inPtrs[(size_t) i];
            if (i < numSrc && src[i] != nullptr)
                juce::FloatVectorOperations::copy (dst, src[i], n);
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

    // 5) Clipping prevention: -1.0 dBFS, one gain for all 12 positions, no look-ahead (0 ms)
    const bool limOn = limiterParam->load (std::memory_order_relaxed) > 0.5f;
    if (limOn)
    {
        if (! limiterWasOn) limiter.reset();
        const float gMin = limiter.process (posPtrs.data(), kNumChannels, n);
        limGainDb.store (gMin < 1.0f ? 20.0f * std::log10 (gMin) : 0.0f, std::memory_order_relaxed);
    }
    else
    {
        limGainDb.store (0.0f, std::memory_order_relaxed);
    }
    limiterWasOn = limOn;

    // BYPASS crossfade on the Atmos side
    if (atmosGain0 < 1.0f || atmosGain1 < 1.0f)
    {
        const float d = (atmosGain1 - atmosGain0) / (float) n;
        for (int p = 0; p < kNumChannels; ++p)
        {
            float* x = posPtrs[(size_t) p];
            for (int k = 0; k < n; ++k) x[k] *= atmosGain0 + d * (float) k;
        }
    }

    // 6) Meters (~80 ms RMS)
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

    // 7) Hand over to the spatial output thread (not while rendering offline)
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

juce::String AtmosBridgeProcessor::mapToString (const Map& m)
{
    juce::StringArray a;
    for (int v : m) a.add (juce::String (v + 1)); // 1..12, 0 = off
    return a.joinIntoString (",");
}

AtmosBridgeProcessor::Map AtmosBridgeProcessor::mapFromString (const juce::String& text, const Map& fallback)
{
    const auto t = juce::StringArray::fromTokens (text, ",", "");
    if (t.size() != kNumChannels) return fallback;
    Map m {};
    for (int c = 0; c < kNumChannels; ++c) m[(size_t) c] = juce::jlimit (-1, kNumChannels - 1, t[c].getIntValue() - 1);
    return m;
}

AtmosBridgeProcessor::Map AtmosBridgeProcessor::liveMap() const noexcept
{
    Map m {};
    for (int c = 0; c < kNumChannels; ++c) m[(size_t) c] = outMap[(size_t) c].load();
    return m;
}

void AtmosBridgeProcessor::applyLiveMap (const Map& m) noexcept
{
    for (int c = 0; c < kNumChannels; ++c) outMap[(size_t) c].store (m[(size_t) c]);
}

AtmosBridgeProcessor::Map& AtmosBridgeProcessor::activeMapMemory() noexcept
{
    if (activeSlot >= 0 && activeSlot < (int) slots.size()) return slots[(size_t) activeSlot].map;
    return directMap;
}

void AtmosBridgeProcessor::setPosition (int input, int position)
{
    if (input < 0 || input >= kNumChannels) return;
    const int p = juce::jlimit (-1, kNumChannels - 1, position);
    outMap[(size_t) input].store (p);
    activeMapMemory()[(size_t) input] = p;
    writeExtrasToState();
}

void AtmosBridgeProcessor::resetMapping()
{
    activeMapMemory() = defaultMap();
    applyLiveMap (defaultMap());
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

void AtmosBridgeProcessor::setDirectSourceId (const juce::String& id)
{
    directSourceId = id;
    writeExtrasToState();
    logLine ("Direct loopback input: " + (id.isEmpty() ? std::string ("<auto: CABLE In 16ch>") : id.toStdString()));
}

void AtmosBridgeProcessor::setUiScale (float s)
{
    uiScale = juce::jlimit (0.5f, 1.5f, s);
    writeExtrasToState();
}

void AtmosBridgeProcessor::setLimiterOn (bool on)
{
    setParam ("limiter", on ? 1.0f : 0.0f);
    logLine (std::string ("Clipping prevention ") + (on ? "on" : "off"));
}

void AtmosBridgeProcessor::setBypassed (bool shouldBypass)
{
    setParam ("bypass", shouldBypass ? 1.0f : 0.0f);
    logLine (std::string ("Atmos Bridge ") + (shouldBypass ? "BYPASS on" : "BYPASS off"));
}

void AtmosBridgeProcessor::setTheme (ThemeId t)
{
    theme = t;
    writeExtrasToState();
    writeUserTheme ((int) t);
    logLine ("Theme: " + std::to_string ((int) t));
}

//============================================================================== upmixer slots
int AtmosBridgeProcessor::findFreeHost() const noexcept
{
    for (int h = 0; h < kMaxUpmixers; ++h)
    {
        bool used = false;
        for (const auto& e : slots) used = used || e.host == h;
        if (! used) return h;
    }
    return -1;
}

int AtmosBridgeProcessor::getSlotHostIndex (int index) const noexcept
{
    return (index >= 0 && index < (int) slots.size()) ? slots[(size_t) index].host : -1;
}

juce::AudioPluginInstance* AtmosBridgeProcessor::getSlotInstance (int index) const noexcept
{
    const int h = getSlotHostIndex (index);
    return h >= 0 ? hosts[(size_t) h]->getInstance() : nullptr;
}

AtmosBridgeProcessor::SlotView AtmosBridgeProcessor::getSlot (int index) const
{
    SlotView v;
    if (index < 0 || index >= (int) slots.size()) return v;
    const auto& e = slots[(size_t) index];
    const auto& info = hosts[(size_t) e.host]->getInfo();
    v.loaded = info.loaded;
    v.name = info.loaded ? info.name : (e.name.isNotEmpty() ? e.name : juce::File (e.path).getFileNameWithoutExtension());
    v.path = e.path;
    v.layout = info.layout;
    v.namedLayout = info.namedLayout;
    v.error = info.loaded ? juce::String() : (e.error.isNotEmpty() ? e.error : info.error);
    v.bypass = e.bypass;
    v.active = index == activeSlot;
    v.latencyMs = info.loaded ? 1000.0 * info.latencySamples / juce::jmax (1.0, currentSampleRate) : 0.0;
    return v;
}

bool AtmosBridgeProcessor::addUpmixer (const juce::String& path, juce::String& error)
{
    error.clear();
    if ((int) slots.size() >= kMaxUpmixers)
    {
        error = juce::String::fromUTF8 ("업믹서는 최대 4개까지 추가할 수 있습니다");
        return false;
    }
    const int h = findFreeHost();
    if (h < 0) return false;

    auto& host = *hosts[(size_t) h];
    if (! host.load (path))
    {
        error = host.getInfo().error;
        return false;
    }
    host.setEnabled (true);

    SlotEntry e;
    e.host = h;
    e.path = path;
    e.name = host.getInfo().name;
    e.map = defaultMap(); // named layouts arrive in L R C LFE SBL SBR SL SR TFL TFR TRL TRR order
    slots.push_back (e);
    bumpSlots();
    logLine ("Upmixer slot added: " + e.name.toStdString());
    setActiveSlot ((int) slots.size() - 1); // a new upmixer is switched on right away
    return true;
}

void AtmosBridgeProcessor::removeSlot (int index)
{
    if (index < 0 || index >= (int) slots.size()) return;
    if (index == activeSlot) setActiveSlot (-1);

    const auto e = slots[(size_t) index];
    slots.erase (slots.begin() + index);
    if (activeSlot > index) --activeSlot;
    hosts[(size_t) e.host]->unload();
    bumpSlots();
    writeExtrasToState();
    updateLatency();
    logLine ("Upmixer slot removed: " + e.name.toStdString());
}

void AtmosBridgeProcessor::toggleSlotActive (int index)
{
    if (index < 0 || index >= (int) slots.size()) return;
    setActiveSlot (index == activeSlot ? -1 : index);
}

void AtmosBridgeProcessor::setActiveSlot (int index)
{
    if (index >= (int) slots.size()) index = -1;

    // Keep the table of the current choice, then switch to the table of the new one.
    activeMapMemory() = liveMap();
    activeSlot = index;

    if (index >= 0)
    {
        auto& host = *hosts[(size_t) slots[(size_t) index].host];
        host.resetPlugin();
        activeHost.store (slots[(size_t) index].host, std::memory_order_release);
    }
    else
    {
        activeHost.store (-1, std::memory_order_release);
    }
    applyLiveMap (activeMapMemory());

    bumpSlots();
    writeExtrasToState();
    updateLatency();
    logLine ("Upmixer on: " + (index >= 0 ? slots[(size_t) index].name.toStdString() : std::string ("<none>")));
}

void AtmosBridgeProcessor::toggleSlotBypass (int index)
{
    if (index < 0 || index >= (int) slots.size()) return;
    auto& e = slots[(size_t) index];
    e.bypass = ! e.bypass;
    hosts[(size_t) e.host]->setEnabled (! e.bypass);
    bumpSlots();
    writeExtrasToState();
    updateLatency();
    logLine ("Upmixer " + e.name.toStdString() + (e.bypass ? ": bypass" : ": bypass off"));
}

void AtmosBridgeProcessor::unloadAllSlots()
{
    activeHost.store (-1, std::memory_order_release);
    activeSlot = -1;
    for (const auto& e : slots) hosts[(size_t) e.host]->unload();
    slots.clear();
    bumpSlots();
}

double AtmosBridgeProcessor::getUpmixerLatencyMs() const noexcept
{
    const int h = activeHost.load();
    if (h < 0 || ! hosts[(size_t) h]->isActive()) return 0.0;
    return 1000.0 * hosts[(size_t) h]->getInfo().latencySamples / juce::jmax (1.0, currentSampleRate);
}

void AtmosBridgeProcessor::updateLatency()
{
    const int h = activeHost.load();
    setLatencySamples ((h >= 0 && hosts[(size_t) h]->isActive()) ? hosts[(size_t) h]->getInfo().latencySamples : 0);
}

void AtmosBridgeProcessor::handleAsyncUpdate()
{
    if (! pendingRestore.valid) return;
    auto p = std::move (pendingRestore);
    pendingRestore = {};

    unloadAllSlots();
    for (auto& r : p.slots)
    {
        if ((int) slots.size() >= kMaxUpmixers) break;
        SlotEntry e = r;
        e.host = findFreeHost();
        auto& host = *hosts[(size_t) e.host];
        if (host.load (e.path))
        {
            host.setStateBase64 (r.pendingState);
            e.pendingState.clear();
            e.name = host.getInfo().name;
            e.error.clear();
        }
        else
        {
            // Keep the entry (and its settings) so nothing is lost; the slot shows why it failed.
            e.error = host.getInfo().error;
            logLine ("Upmixer could not be restored: " + e.path.toStdString());
        }
        host.setEnabled (! e.bypass);
        slots.push_back (e);
    }
    directMap = p.direct;
    activeSlot = -1;
    const Map restoredLive = liveMap();
    const int want = (p.active >= 0 && p.active < (int) slots.size()) ? p.active : -1;
    if (want >= 0)
    {
        activeSlot = want;
        activeHost.store (slots[(size_t) want].host, std::memory_order_release);
        slots[(size_t) want].map = restoredLive;
    }
    else
    {
        directMap = restoredLive;
    }
    bumpSlots();
    writeExtrasToState();
    updateLatency();
}

//==============================================================================
void AtmosBridgeProcessor::writeExtrasToState()
{
    auto& s = apvts.state;
    s.setProperty ("map", mapToString (liveMap()), nullptr);
    s.setProperty ("outputDevice", outputDeviceId, nullptr);
    s.setProperty ("uiScale", uiScale, nullptr);
    s.setProperty ("theme", (int) theme, nullptr);
    s.setProperty ("directSource", directSourceId, nullptr);
    s.setProperty ("stateVersion", 3, nullptr);

    // While a restore is still pending, the slots that are about to be loaded are what we save.
    const bool pending = pendingRestore.valid;
    const auto& list = pending ? pendingRestore.slots : slots;

    juce::ValueTree ups ("UPMIXERS");
    ups.setProperty ("active", pending ? pendingRestore.active : activeSlot, nullptr);
    ups.setProperty ("directMap", mapToString (pending ? pendingRestore.direct
                                                      : (activeSlot < 0 ? liveMap() : directMap)), nullptr);
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& e = list[(size_t) i];
        juce::ValueTree t ("SLOT");
        t.setProperty ("path", e.path, nullptr);
        t.setProperty ("name", e.name, nullptr);
        t.setProperty ("bypass", e.bypass, nullptr);
        t.setProperty ("map", mapToString ((! pending && i == activeSlot) ? liveMap() : e.map), nullptr);
        ups.appendChild (t, nullptr);
    }
    if (auto old = s.getChildWithName ("UPMIXERS"); old.isValid()) s.removeChild (old, nullptr);
    s.appendChild (ups, nullptr);
}

void AtmosBridgeProcessor::readExtrasFromState()
{
    auto& s = apvts.state;
    const Map live = mapFromString (s.getProperty ("map", "").toString(), liveMap());
    applyLiveMap (live);

    uiScale = juce::jlimit (0.5f, 1.5f, (float) s.getProperty ("uiScale", uiScale));
    directSourceId = s.getProperty ("directSource", "").toString();

    // Theme: the one last chosen on this PC wins; the project's own value is used when there is none.
    const int userTheme = readUserTheme();
    const int projectTheme = (int) s.getProperty ("theme", (int) theme);
    theme = (ThemeId) juce::jlimit (0, kNumThemes - 1, userTheme >= 0 ? userTheme : projectTheme);

    PendingRestore p;
    p.valid = true;
    p.direct = live;
    const int version = (int) s.getProperty ("stateVersion", 1);
    if (version >= 3)
    {
        const auto ups = s.getChildWithName ("UPMIXERS");
        p.active = (int) ups.getProperty ("active", -1);
        p.direct = mapFromString (ups.getProperty ("directMap", "").toString(), defaultMap());
        for (int i = 0; i < ups.getNumChildren() && i < kMaxUpmixers; ++i)
        {
            const auto t = ups.getChild (i);
            SlotEntry e;
            e.path = t.getProperty ("path", "").toString();
            e.name = t.getProperty ("name", "").toString();
            e.pendingState = t.getProperty ("state", "").toString();
            e.bypass = (bool) t.getProperty ("bypass", false);
            e.map = mapFromString (t.getProperty ("map", "").toString(), defaultMap());
            if (e.path.isNotEmpty()) p.slots.push_back (e);
        }
        if (p.active >= (int) p.slots.size()) p.active = -1;
    }
    else if (const auto path = s.getProperty ("upmixerPath", "").toString(); path.isNotEmpty())
    {
        // v1.1 project: one upmixer ("off" there = bypass here), same table as before.
        SlotEntry e;
        e.path = path;
        e.pendingState = s.getProperty ("upmixerState", "").toString();
        e.bypass = ! (bool) s.getProperty ("upmixerEnabled", true);
        e.map = live;
        p.slots.push_back (e);
        p.active = 0;
    }
    pendingRestore = std::move (p);
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
    // Each hosted upmixer's own settings travel with ours.
    auto ups = state.getChildWithName ("UPMIXERS");
    const bool pending = pendingRestore.valid;
    for (int i = 0; i < ups.getNumChildren(); ++i)
    {
        juce::String st;
        if (pending)
        {
            if (i < (int) pendingRestore.slots.size()) st = pendingRestore.slots[(size_t) i].pendingState;
        }
        else if (i < (int) slots.size())
        {
            const auto& e = slots[(size_t) i];
            st = hosts[(size_t) e.host]->getInfo().loaded ? hosts[(size_t) e.host]->getStateBase64() : e.pendingState;
        }
        ups.getChild (i).setProperty ("state", st, nullptr);
    }
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
