#include "UpmixerHost.h"
#include "engine/Platform.h"

using ab::logLine;

namespace
{
juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
}

UpmixerHost::UpmixerHost()
{
   #if JUCE_PLUGINHOST_VST3
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif
}

UpmixerHost::~UpmixerHost()
{
    beforeUnload = nullptr;
    unload();
}

//==============================================================================
juce::Array<juce::File> UpmixerHost::findInstalledVst3()
{
    juce::Array<juce::File> result;
   #if JUCE_PLUGINHOST_VST3
    juce::VST3PluginFormat fmt;
    const auto paths = fmt.getDefaultLocationsToSearch();

    std::function<void (const juce::File&, int)> walk = [&] (const juce::File& dir, int depth)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            if (f.hasFileExtension (".vst3"))
            {
                // A .vst3 bundle is a folder: never descend into it. Skip ourselves.
                if (! f.getFileNameWithoutExtension().containsIgnoreCase ("Atmos Bridge"))
                    result.addIfNotAlreadyThere (f);
            }
            else if (entry.isDirectory() && depth < 3)
            {
                walk (f, depth + 1);
            }
        }
    };

    for (int i = 0; i < paths.getNumPaths(); ++i)
        if (paths[i].isDirectory())
            walk (paths[i], 0);

    std::sort (result.begin(), result.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
   #endif
    return result;
}

//==============================================================================
bool UpmixerHost::negotiateLayout (juce::AudioPluginInstance& p, juce::String& layoutText)
{
    using CS = juce::AudioChannelSet;
    const auto original = p.getBusesLayout();
    if (original.inputBuses.isEmpty() || original.outputBuses.isEmpty())
        return false;

    // Preferred first: stereo in -> 7.1.4 out (channels named, so they can be re-ordered automatically).
    const std::pair<CS, CS> attempts[] = {
        { CS::stereo(),                CS::create7point1point4() },
        { CS::stereo(),                CS::discreteChannels (12) },
        { CS::create7point1point4(),   CS::create7point1point4() },
        { CS::discreteChannels (12),   CS::discreteChannels (12) },
    };

    for (const bool disableAux : { true, false })
        for (const auto& a : attempts)
        {
            auto l = original;
            l.inputBuses.getReference (0) = a.first;
            l.outputBuses.getReference (0) = a.second;
            if (disableAux)
            {
                for (int i = 1; i < l.inputBuses.size(); ++i)  l.inputBuses.getReference (i) = CS::disabled();
                for (int i = 1; i < l.outputBuses.size(); ++i) l.outputBuses.getReference (i) = CS::disabled();
            }
            if (p.setBusesLayout (l))
            {
                layoutText = a.first.getDescription() + juce::String::fromUTF8 (" \xe2\x86\x92 ")
                           + a.second.getDescription() + " (" + juce::String (a.second.size()) + "ch)";
                return true;
            }
        }

    // Nothing accepted: keep the plugin's own layout if it already has 12+ outputs.
    p.setBusesLayout (original);
    if (p.getMainBusNumOutputChannels() >= ab::kNumChannels)
    {
        const auto in = p.getChannelLayoutOfBus (true, 0), out = p.getChannelLayoutOfBus (false, 0);
        layoutText = in.getDescription() + juce::String::fromUTF8 (" \xe2\x86\x92 ") + out.getDescription()
                   + " (" + juce::String (out.size()) + "ch)";
        return true;
    }
    return false;
}

void UpmixerHost::computeRouting (Slot& s, bool& named)
{
    using CS = juce::AudioChannelSet;
    auto& p = *s.instance;
    const auto in = p.getChannelLayoutOfBus (true, 0);
    const auto out = p.getChannelLayoutOfBus (false, 0);
    s.numIn = p.getTotalNumInputChannels();
    s.numOut = p.getTotalNumOutputChannels();

    s.inLeft = in.getChannelIndexForType (CS::left);
    s.inRight = in.getChannelIndexForType (CS::right);
    if (s.inLeft < 0) s.inLeft = 0;
    if (s.inRight < 0) s.inRight = s.numIn > 1 ? 1 : 0;

    named = out.size() > 0 && ! out.isDiscreteLayout();
    if (! named)
    {
        for (int pos = 0; pos < ab::kNumChannels; ++pos)
            s.posToOut[(size_t) pos] = pos < s.numOut ? pos : -1;
        return;
    }

    // Output position (L R C LFE SBL SBR SL SR TFL TFR TRL TRR) -> speaker type(s), in order of preference.
    // JUCE keeps the hosted plugin's buffer in its own channel order, so look the index up by type.
    const std::vector<std::vector<CS::ChannelType>> wanted = {
        { CS::left }, { CS::right }, { CS::centre }, { CS::LFE },
        { CS::leftSurroundRear }, { CS::rightSurroundRear },
        { CS::leftSurroundSide, CS::leftSurround }, { CS::rightSurroundSide, CS::rightSurround },
        { CS::topFrontLeft }, { CS::topFrontRight },
        { CS::topRearLeft, CS::topSideLeft }, { CS::topRearRight, CS::topSideRight },
    };
    for (int pos = 0; pos < ab::kNumChannels; ++pos)
    {
        int idx = -1;
        for (auto t : wanted[(size_t) pos])
            if ((idx = out.getChannelIndexForType (t)) >= 0) break;
        s.posToOut[(size_t) pos] = idx;
    }
}

void UpmixerHost::prepareSlot (Slot& s)
{
    s.instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
    s.instance->prepareToPlay (sampleRate, blockSize);
    s.buffer.setSize (juce::jmax (2, s.numIn, s.numOut), blockSize, false, true, false);
    s.midi.ensureSize (256);
}

//==============================================================================
void UpmixerHost::prepare (double newRate, int maxBlockSize)
{
    sampleRate = newRate > 0 ? newRate : 48000.0;
    blockSize = juce::jmax (32, maxBlockSize);

    const juce::ScopedLock sl (lock);
    if (slot != nullptr)
    {
        slot->instance->releaseResources();
        prepareSlot (*slot);
        info.latencySamples = slot->instance->getLatencySamples();
    }
}

bool UpmixerHost::load (const juce::String& path)
{
   #if JUCE_PLUGINHOST_VST3
    auto* format = formats.getFormat (0);
    juce::OwnedArray<juce::PluginDescription> types;
    if (format != nullptr)
        format->findAllTypesForFile (types, path);

    const juce::PluginDescription* desc = nullptr;
    for (auto* d : types)
        if (! d->isInstrument) { desc = d; break; }
    if (desc == nullptr && ! types.isEmpty()) desc = types[0];

    if (desc == nullptr)
    {
        info.error = tr ("VST3를 읽을 수 없습니다: ") + juce::File (path).getFileName();
        logLine ("Upmixer load failed (no plugin in file): " + path.toStdString());
        return false;
    }
    if (desc->name.containsIgnoreCase ("Atmos Bridge"))
    {
        info.error = tr ("Atmos Bridge 자신은 불러올 수 없습니다");
        return false;
    }

    juce::String err;
    auto instance = formats.createPluginInstance (*desc, sampleRate, blockSize, err);
    if (instance == nullptr)
    {
        info.error = tr ("불러오기 실패: ") + desc->name + (err.isNotEmpty() ? " (" + err + ")" : juce::String());
        logLine ("Upmixer load failed: " + desc->name.toStdString() + " " + err.toStdString());
        return false;
    }

    juce::String layoutText;
    if (! negotiateLayout (*instance, layoutText))
    {
        info.error = desc->name + tr (": 12채널 출력 구성을 받아들이지 않습니다");
        logLine ("Upmixer refused all 12-channel layouts: " + desc->name.toStdString());
        return false;
    }

    auto s = std::make_unique<Slot>();
    s->instance = std::move (instance);
    bool named = false;
    computeRouting (*s, named);
    prepareSlot (*s);

    unload(); // closes the old plugin's window first

    Info ni;
    ni.loaded = true;
    ni.namedLayout = named;
    ni.name = desc->name;
    ni.path = path;
    ni.layout = layoutText;
    ni.latencySamples = s->instance->getLatencySamples();
    ni.numOutputs = s->numOut;

    {
        const juce::ScopedLock sl (lock);
        slot = std::move (s);
    }
    info = ni;
    loaded.store (true);

    juce::String routing;
    for (int pos = 0; pos < ab::kNumChannels; ++pos)
        routing << ab::kPosCodes[(size_t) pos] << "<-" << slot->posToOut[(size_t) pos] + 1 << " ";
    logLine ("Upmixer loaded: " + ni.name.toStdString() + " | " + layoutText.toStdString() + " | latency "
             + std::to_string (ni.latencySamples) + " | " + routing.toStdString());
    return true;
   #else
    juce::ignoreUnused (path);
    info.error = tr ("이 빌드는 플러그인 불러오기를 지원하지 않습니다");
    return false;
   #endif
}

void UpmixerHost::unload()
{
    if (slot == nullptr)
    {
        loaded.store (false);
        return;
    }
    if (beforeUnload) beforeUnload();

    loaded.store (false);
    std::unique_ptr<Slot> old;
    {
        const juce::ScopedLock sl (lock); // waits for a running process() call to finish
        old = std::move (slot);
    }
    if (old != nullptr && old->instance != nullptr)
    {
        logLine ("Upmixer unloaded: " + old->instance->getName().toStdString());
        old->instance->releaseResources();
    }
    old.reset();
    info = Info {};
}

bool UpmixerHost::refreshLatency()
{
    if (slot == nullptr) return false;
    const int l = slot->instance->getLatencySamples();
    if (l == info.latencySamples) return false;
    info.latencySamples = l;
    return true;
}

//==============================================================================
juce::String UpmixerHost::getStateBase64() const
{
    if (slot == nullptr) return {};
    juce::MemoryBlock mb;
    slot->instance->getStateInformation (mb);
    return mb.toBase64Encoding();
}

void UpmixerHost::setStateBase64 (const juce::String& base64)
{
    if (slot == nullptr || base64.isEmpty()) return;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (base64) && mb.getSize() > 0)
        slot->instance->setStateInformation (mb.getData(), (int) mb.getSize());
}

//==============================================================================
bool UpmixerHost::process (const float* inL, const float* inR, float* const* dst, int n) noexcept
{
    if (! isActive()) return false;

    const juce::ScopedTryLock tl (lock);
    if (! tl.isLocked() || slot == nullptr) return false;

    auto& s = *slot;
    if (n > s.buffer.getNumSamples()) return false;

    const int nch = s.buffer.getNumChannels();
    for (int c = 0; c < nch; ++c)
        juce::FloatVectorOperations::clear (s.buffer.getWritePointer (c), n);

    if (s.numIn >= 2)
    {
        if (inL != nullptr && s.inLeft < nch)  juce::FloatVectorOperations::copy (s.buffer.getWritePointer (s.inLeft), inL, n);
        if (inR != nullptr && s.inRight < nch) juce::FloatVectorOperations::copy (s.buffer.getWritePointer (s.inRight), inR, n);
    }
    else if (s.numIn == 1)
    {
        float* d = s.buffer.getWritePointer (0);
        if (inL != nullptr) juce::FloatVectorOperations::addWithMultiply (d, inL, 0.5f, n);
        if (inR != nullptr) juce::FloatVectorOperations::addWithMultiply (d, inR, 0.5f, n);
    }

    juce::AudioBuffer<float> view (s.buffer.getArrayOfWritePointers(), nch, n);
    s.midi.clear();
    s.instance->processBlock (view, s.midi);

    for (int pos = 0; pos < ab::kNumChannels; ++pos)
    {
        const int o = s.posToOut[(size_t) pos];
        if (o >= 0 && o < nch) juce::FloatVectorOperations::copy (dst[pos], view.getReadPointer (o), n);
        else                   juce::FloatVectorOperations::clear (dst[pos], n);
    }
    return true;
}
