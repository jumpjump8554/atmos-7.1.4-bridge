#include "SpatialEngine.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <functional>
#include <mutex>
#include <thread>

namespace ab
{
const char* toString (EngineStatus s) noexcept
{
    switch (s)
    {
        case EngineStatus::Stopped:     return "Stopped";
        case EngineStatus::Connecting:  return "Connecting";
        case EngineStatus::Running:     return "Running";
        case EngineStatus::SpatialOff:  return "SpatialOff";
        case EngineStatus::NoDevice:    return "NoDevice";
        case EngineStatus::Error:       return "Error";
        case EngineStatus::Unsupported: return "Unsupported";
        case EngineStatus::Busy:        return "Busy";
        case EngineStatus::ExclusiveOff: return "ExclusiveOff";
    }
    return "?";
}
} // namespace ab

//==============================================================================
#if defined (_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <spatialaudioclient.h>
#include <mmreg.h>
#include <avrt.h>
#include <wrl/client.h>
#include "SpatialFormat.h"

using Microsoft::WRL::ComPtr;

#ifndef AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED
 #define AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED AUDCLNT_ERR (0x00e)
#endif
#ifndef AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED
 #define AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED AUDCLNT_ERR (0x019)
#endif

namespace ab
{
namespace
{
std::string toUtf8 (const wchar_t* w)
{
    if (w == nullptr || *w == 0) return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) (n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide (const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w ((size_t) (n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string hrToString (HRESULT hr)
{
    char buf[16];
    std::snprintf (buf, sizeof (buf), "0x%08lX", (unsigned long) hr);
    return buf;
}

std::string deviceFriendlyName (IMMDevice* dev)
{
    ComPtr<IPropertyStore> props;
    if (FAILED (dev->OpenPropertyStore (STGM_READ, &props))) return "Unknown device";
    PROPVARIANT v;
    PropVariantInit (&v);
    std::string name = "Unknown device";
    if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        name = toUtf8 (v.pwszVal);
    PropVariantClear (&v);
    return name;
}

std::string deviceId (IMMDevice* dev)
{
    LPWSTR id = nullptr;
    std::string s;
    if (SUCCEEDED (dev->GetId (&id)) && id != nullptr)
    {
        s = toUtf8 (id);
        CoTaskMemFree (id);
    }
    return s;
}

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT / _PCM, defined locally to avoid header/GUID-definition issues.
const GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID kSubtypePcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

enum class SampleKind { Float32, Int16, Int32, Unsupported };

SampleKind kindOf (const WAVEFORMATEX* f)
{
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22)
    {
        const auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (f);
        if (IsEqualGUID (x->SubFormat, kSubtypeFloat)) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (IsEqualGUID (x->SubFormat, kSubtypePcm)) tag = WAVE_FORMAT_PCM;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32) return SampleKind::Float32;
    if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 16)        return SampleKind::Int16;
    if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 32)        return SampleKind::Int32;
    return SampleKind::Unsupported;
}

// The 12 output positions (UI numbers 1..12) mapped to Windows static audio objects:
// L R C LFE SBL SBR SL SR TFL TFR TRL TRR
const AudioObjectType kObjectTypes[12] = {
    AudioObjectType_FrontLeft,    AudioObjectType_FrontRight,    AudioObjectType_FrontCenter, AudioObjectType_LowFrequency,
    AudioObjectType_BackLeft,     AudioObjectType_BackRight,     AudioObjectType_SideLeft,    AudioObjectType_SideRight,
    AudioObjectType_TopFrontLeft, AudioObjectType_TopFrontRight, AudioObjectType_TopBackLeft, AudioObjectType_TopBackRight
};

// True if the device currently offers a spatial audio object stream (a spatial sound format is active).
bool spatialAvailable (IMMDevice* dev)
{
    ComPtr<ISpatialAudioClient> c;
    if (FAILED (dev->Activate (__uuidof (ISpatialAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void**) c.GetAddressOf())) || ! c)
        return false;
    return SUCCEEDED (c->IsSpatialAudioStreamAvailable (__uuidof (ISpatialAudioObjectRenderStream), nullptr));
}

bool looksLikeHdmi (const std::string& name)
{
    std::string n = name;
    for (auto& ch : n) ch = (char) std::tolower ((unsigned char) ch);
    for (const char* key : { "hdmi", "avr", "receiver", "amd high definition", "nvidia high definition", "display audio", "intel(r) display" })
        if (n.find (key) != std::string::npos) return true;
    return false;
}

//==============================================================================
class NotificationClient final : public IMMNotificationClient
{
public:
    explicit NotificationClient (std::function<void (bool defaultChanged)> cb) : callback (std::move (cb)) {}

    ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) InterlockedIncrement (&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = (ULONG) InterlockedDecrement (&refs);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
    {
        if (riid == __uuidof (IUnknown) || riid == __uuidof (IMMNotificationClient))
        {
            *ppv = static_cast<IMMNotificationClient*> (this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged (LPCWSTR, DWORD) override         { callback (false); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded (LPCWSTR) override                       { callback (false); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved (LPCWSTR) override                     { callback (false); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged (LPCWSTR, const PROPERTYKEY) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged (EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && role == eConsole) callback (true);
        return S_OK;
    }

private:
    LONG refs = 1;
    std::function<void (bool)> callback;
};
} // namespace

//==============================================================================
struct SpatialEngine::Impl
{
    explicit Impl (OutputFeeder& f) : feeder (f) {}

    OutputFeeder& feeder;
    std::thread thread;
    std::atomic<bool> stopFlag { false }, reconnectFlag { false }, reconnecting { false };
    std::atomic<int> status { (int) EngineStatus::Stopped };
    std::atomic<int> maxDynamic { 0 }, periodFrames { 0 };
    std::atomic<double> outRate { 48000.0 };
    std::atomic<uint32_t> reconnects { 0 };

    mutable std::mutex infoLock;
    std::string deviceIdWanted, deviceName, lastError;

    HANDLE wakeEvent = nullptr;   // device-change notifications / stop requests
    HANDLE bufferEvent = nullptr; // ISAC "ready for data" event

    // Per-connection objects (engine thread only)
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<ISpatialAudioClient> client;
    ComPtr<ISpatialAudioObjectRenderStream> stream;
    ComPtr<ISpatialAudioObject> objects[12];
    std::vector<unsigned char> formatBlob;
    SampleKind kind = SampleKind::Float32;
    std::vector<std::vector<float>> convertBuf;
    std::atomic<bool> usingDefault { true };
    std::atomic<bool> memLocked { false };

    // 2ch Stereo (WASAPI exclusive)
    std::atomic<bool> stereoWanted { false }, stereoActive { false };
    ComPtr<IAudioClient> exClient;
    ComPtr<IAudioRenderClient> exRender;
    UINT32 exFrames = 0;
    SampleKind exKind = SampleKind::Int32;
    int exBytes = 4;
    std::vector<std::vector<float>> exBuf;
    std::string spatialTriedFor;      // device on which turning on Dolby Atmos for home theater was tried
    DWORD spatialTriedAt = 0;
    std::vector<std::pair<const void*, size_t>> locked;

    void setStatus (EngineStatus s, const std::string& err = {})
    {
        const auto prev = (EngineStatus) status.exchange ((int) s);
        if (! err.empty())
        {
            std::lock_guard<std::mutex> g (infoLock);
            lastError = err;
        }
        if (prev != s)
            logLine (std::string ("Engine status: ") + toString (prev) + " -> " + toString (s) + (err.empty() ? "" : "  (" + err + ")"));
    }

    void onDeviceChange (bool defaultChanged)
    {
        // Called on a system thread: only flag and wake.
        if (! defaultChanged || usingDefault.load())
        {
            reconnectFlag.store (true);
            if (wakeEvent) SetEvent (wakeEvent);
        }
    }

    void disconnect()
    {
        if (stream) stream->Stop();
        for (auto& o : objects) o.Reset();
        stream.Reset();
        client.Reset();
        if (exClient) exClient->Stop();
        exRender.Reset();
        exClient.Reset();
        stereoActive.store (false);
    }

    // Atmos: spatial sound is off for this device -> try to select "Dolby Atmos for home theater"
    // (at most once a minute per device). Windows may refuse it; then the user selects it by hand.
    void tryEnableAtmos (IMMDevice* dev)
    {
        const std::string id = deviceId (dev);
        const DWORD now = GetTickCount();
        if (id == spatialTriedFor && now - spatialTriedAt < 60000) return;
        spatialTriedFor = id;
        spatialTriedAt = now;
        const auto r = enableDolbyAtmosForHomeTheater (toWide (id));
        logLine ("Turning on 'Dolby Atmos for home theater' for \"" + deviceFriendlyName (dev) + "\": " + toString (r));
    }

    // Auto: never a virtual cable (the host captures from it -> feedback loop). Among devices with spatial sound:
    // an HDMI/receiver-like device first, then the default output, then any other.
    // requireSpatial = false (2ch / turning spatial sound on): devices without spatial sound are accepted too
    // (still preferring the ones with it).
    ComPtr<IMMDevice> pickAutoDevice (bool requireSpatial = true)
    {
        if (! requireSpatial)
            if (auto d = pickAutoDevice (true)) return d;

        std::wstring defaultId;
        {
            ComPtr<IMMDevice> def;
            if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &def)) && def)
            {
                LPWSTR id = nullptr;
                if (SUCCEEDED (def->GetId (&id)) && id) { defaultId = id; CoTaskMemFree (id); }
            }
        }

        ComPtr<IMMDeviceCollection> coll;
        if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &coll)) || ! coll)
            return {};
        UINT count = 0;
        coll->GetCount (&count);
        ComPtr<IMMDevice> hdmi, def, other;
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> d;
            if (FAILED (coll->Item (i, &d)) || ! d) continue;
            const std::string name = deviceFriendlyName (d.Get());
            if (SpatialEngine::isVirtualDeviceName (name)) continue;
            if (requireSpatial && ! spatialAvailable (d.Get())) continue;

            LPWSTR id = nullptr;
            std::wstring wid;
            if (SUCCEEDED (d->GetId (&id)) && id) { wid = id; CoTaskMemFree (id); }

            if (! hdmi && looksLikeHdmi (name)) hdmi = d;
            else if (! def && ! defaultId.empty() && wid == defaultId) def = d;
            else if (! other) other = d;
        }
        if (hdmi) return hdmi;
        if (def) return def;
        return other;
    }

    // Best effort: keep the real-time buffers in physical memory (no page faults on the output thread).
    void lockMemory()
    {
        size_t total = 0;
        feeder.forEachBuffer ([&] (const void*, size_t b) { total += b; });
        if (total == 0) return;
        HANDLE proc = GetCurrentProcess();
        SIZE_T mn = 0, mx = 0;
        if (GetProcessWorkingSetSize (proc, &mn, &mx))
            SetProcessWorkingSetSize (proc, mn + total + (1u << 20), mx + total + (1u << 20));
        int ok = 0, fail = 0;
        feeder.forEachBuffer ([&] (const void* p, size_t b)
        {
            if (VirtualLock (const_cast<void*> (p), b)) { locked.push_back ({ p, b }); ++ok; }
            else ++fail;
        });
        memLocked.store (fail == 0 && ok > 0);
        logLine ("Memory lock: " + std::to_string (ok) + " buffers locked, " + std::to_string (fail) + " failed ("
                 + std::to_string (total / 1024) + " KB)");
    }

    void unlockMemory()
    {
        for (auto& r : locked) VirtualUnlock (const_cast<void*> (r.first), r.second);
        locked.clear();
        memLocked.store (false);
    }

    bool connect()
    {
        // Keep NoDevice / SpatialOff / Error visible (and the log quiet) while retrying every 2 s.
        const auto cur = (EngineStatus) status.load();
        if (cur != EngineStatus::NoDevice && cur != EngineStatus::SpatialOff && cur != EngineStatus::Error
            && cur != EngineStatus::Busy && cur != EngineStatus::ExclusiveOff)
            setStatus (EngineStatus::Connecting);
        std::string wanted;
        {
            std::lock_guard<std::mutex> g (infoLock);
            wanted = deviceIdWanted;
        }
        usingDefault.store (wanted.empty());

        const bool stereo = stereoWanted.load();
        ComPtr<IMMDevice> device;
        HRESULT hr = S_OK;
        if (usingDefault.load())
        {
            device = pickAutoDevice (! stereo);
            if (! device)
            {
                if (! stereo)
                    if (auto candidate = pickAutoDevice (false)) tryEnableAtmos (candidate.Get());
                setStatus (stereo ? EngineStatus::NoDevice : EngineStatus::SpatialOff,
                           stereo ? "No output device (other than virtual cables) found."
                                  : "No output device (other than virtual cables) has a spatial sound format enabled. Select 'Dolby Atmos for home theater' for the AV receiver, or pick the receiver in the device list.");
                return false;
            }
        }
        else
        {
            hr = enumerator->GetDevice (toWide (wanted).c_str(), &device);
            DWORD state = 0;
            if (SUCCEEDED (hr) && device) device->GetState (&state);
            if (FAILED (hr) || ! device || state != DEVICE_STATE_ACTIVE)
            {
                setStatus (EngineStatus::NoDevice, "Output device not available (" + hrToString (hr) + ")");
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> g (infoLock);
            deviceName = deviceFriendlyName (device.Get());
        }

        if (stereo)
            return connectExclusive (device.Get());

        hr = device->Activate (__uuidof (ISpatialAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void**) client.ReleaseAndGetAddressOf());
        if (FAILED (hr) || ! client)
        {
            setStatus (EngineStatus::Error, "ISpatialAudioClient activation failed " + hrToString (hr));
            return false;
        }

        const HRESULT avail = client->IsSpatialAudioStreamAvailable (__uuidof (ISpatialAudioObjectRenderStream), nullptr);
        UINT32 maxDyn = 0;
        client->GetMaxDynamicObjectCount (&maxDyn);
        maxDynamic.store ((int) maxDyn);
        AudioObjectType nativeMask = AudioObjectType_None;
        client->GetNativeStaticObjectTypeMask (&nativeMask);

        if (FAILED (avail))
        {
            client.Reset();
            tryEnableAtmos (device.Get());
            setStatus (EngineStatus::SpatialOff,
                       "Spatial sound is off for this device (" + hrToString (avail) + "). Select 'Dolby Atmos for home theater' in Windows sound settings.");
            return false;
        }

        // Object format: the renderer's preferred one (first in the enumerator).
        ComPtr<IAudioFormatEnumerator> formats;
        hr = client->GetSupportedAudioObjectFormatEnumerator (&formats);
        WAVEFORMATEX* fmt = nullptr;
        if (SUCCEEDED (hr)) hr = formats->GetFormat (0, &fmt);
        if (FAILED (hr) || fmt == nullptr)
        {
            setStatus (EngineStatus::Error, "No supported object format " + hrToString (hr));
            client.Reset();
            return false;
        }
        formatBlob.assign ((const unsigned char*) fmt, (const unsigned char*) fmt + sizeof (WAVEFORMATEX) + fmt->cbSize);
        const WAVEFORMATEX* f = reinterpret_cast<const WAVEFORMATEX*> (formatBlob.data());
        kind = kindOf (f);
        if (kind == SampleKind::Unsupported || f->nChannels != 1)
        {
            setStatus (EngineStatus::Error, "Unsupported object format (tag " + std::to_string (f->wFormatTag)
                                              + ", " + std::to_string (f->wBitsPerSample) + " bit, "
                                              + std::to_string (f->nChannels) + " ch)");
            client.Reset();
            return false;
        }

        AudioObjectType mask = AudioObjectType_None;
        for (auto t : kObjectTypes) mask = (AudioObjectType) (mask | t);

        SpatialAudioObjectRenderStreamActivationParams params {};
        params.ObjectFormat = f;
        params.StaticObjectTypeMask = mask;
        params.MinDynamicObjectCount = 0;
        params.MaxDynamicObjectCount = 0;
        params.Category = AudioCategory_Media;
        params.EventHandle = bufferEvent;
        params.NotifyObject = nullptr;

        PROPVARIANT pv;
        PropVariantInit (&pv);
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof (params);
        pv.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

        hr = client->ActivateSpatialAudioStream (&pv, __uuidof (ISpatialAudioObjectRenderStream), (void**) stream.ReleaseAndGetAddressOf());
        if (FAILED (hr) || ! stream)
        {
            setStatus (EngineStatus::Error, "ActivateSpatialAudioStream failed " + hrToString (hr));
            client.Reset();
            return false;
        }

        for (int k = 0; k < 12; ++k)
        {
            hr = stream->ActivateSpatialAudioObject (kObjectTypes[k], objects[k].ReleaseAndGetAddressOf());
            if (FAILED (hr))
            {
                setStatus (EngineStatus::Error, "ActivateSpatialAudioObject(" + std::to_string (k) + ") failed " + hrToString (hr));
                disconnect();
                return false;
            }
        }

        outRate.store ((double) f->nSamplesPerSec);
        feeder.consumerStart ((double) f->nSamplesPerSec);

        hr = stream->Start();
        if (FAILED (hr))
        {
            setStatus (EngineStatus::Error, "Stream Start failed " + hrToString (hr));
            disconnect();
            return false;
        }

        char info[256];
        std::snprintf (info, sizeof (info), "Connected: %u Hz, %u-bit, maxDynamicObjects=%u, nativeStaticMask=0x%X",
                       (unsigned) f->nSamplesPerSec, (unsigned) f->wBitsPerSample, (unsigned) maxDyn, (unsigned) nativeMask);
        logLine (info + std::string ("  device=\"") + getName() + "\"");
        setStatus (EngineStatus::Running);
        return true;
    }

    // 2ch Stereo: the device in WASAPI exclusive mode, PCM 2.0, event driven, one device period per buffer.
    bool connectExclusive (IMMDevice* device)
    {
        HRESULT hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) exClient.ReleaseAndGetAddressOf());
        if (FAILED (hr) || ! exClient)
        {
            setStatus (EngineStatus::Error, "IAudioClient activation failed " + hrToString (hr));
            return false;
        }

        // Formats, preferred first: 24 bit in 32, 16 bit, 32 bit, float. Rates: 48 kHz first.
        struct Candidate { DWORD rate; WORD container, valid; bool isFloat; SampleKind kind; };
        const Candidate candidates[] = {
            { 48000, 32, 24, false, SampleKind::Int32 }, { 48000, 16, 16, false, SampleKind::Int16 },
            { 48000, 32, 32, false, SampleKind::Int32 }, { 48000, 32, 32, true,  SampleKind::Float32 },
            { 44100, 32, 24, false, SampleKind::Int32 }, { 44100, 16, 16, false, SampleKind::Int16 },
            { 96000, 32, 24, false, SampleKind::Int32 }, { 96000, 16, 16, false, SampleKind::Int16 },
        };
        WAVEFORMATEXTENSIBLE fmt {};
        const Candidate* chosen = nullptr;
        for (const auto& c : candidates)
        {
            WAVEFORMATEXTENSIBLE x {};
            x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            x.Format.nChannels = 2;
            x.Format.nSamplesPerSec = c.rate;
            x.Format.wBitsPerSample = c.container;
            x.Format.nBlockAlign = (WORD) (2 * c.container / 8);
            x.Format.nAvgBytesPerSec = c.rate * x.Format.nBlockAlign;
            x.Format.cbSize = 22;
            x.Samples.wValidBitsPerSample = c.valid;
            x.dwChannelMask = 0x3; // front left | front right
            x.SubFormat = c.isFloat ? kSubtypeFloat : kSubtypePcm;
            if (exClient->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE, &x.Format, nullptr) == S_OK)
            {
                fmt = x;
                chosen = &c;
                break;
            }
        }
        if (chosen == nullptr)
        {
            setStatus (EngineStatus::Error, "The device accepts none of the 2ch PCM formats (48/44.1/96 kHz) in exclusive mode");
            exClient.Reset();
            return false;
        }

        REFERENCE_TIME defPeriod = 0, minPeriod = 0;
        exClient->GetDevicePeriod (&defPeriod, &minPeriod);
        REFERENCE_TIME period = defPeriod > 0 ? defPeriod : 100000;
        hr = exClient->Initialize (AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &fmt.Format, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            UINT32 aligned = 0;
            exClient->GetBufferSize (&aligned);
            period = (REFERENCE_TIME) (10000.0 * 1000.0 * aligned / fmt.Format.nSamplesPerSec + 0.5);
            exClient.Reset();
            hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) exClient.ReleaseAndGetAddressOf());
            if (SUCCEEDED (hr))
                hr = exClient->Initialize (AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &fmt.Format, nullptr);
        }
        if (FAILED (hr))
        {
            exClient.Reset();
            if (hr == AUDCLNT_E_DEVICE_IN_USE)
                setStatus (EngineStatus::Busy, "The device is in use by another application (" + hrToString (hr) + "). "
                                               "Set the host's output to another device, or close the other application.");
            else if (hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED)
                setStatus (EngineStatus::ExclusiveOff, "Exclusive mode is not allowed for this device (" + hrToString (hr) + "). "
                                                       "Sound settings > device properties > Advanced: allow applications to take exclusive control.");
            else
                setStatus (EngineStatus::Error, "Exclusive 2ch Initialize failed " + hrToString (hr));
            return false;
        }

        exClient->SetEventHandle (bufferEvent);
        exClient->GetBufferSize (&exFrames);
        hr = exClient->GetService (__uuidof (IAudioRenderClient), (void**) exRender.ReleaseAndGetAddressOf());
        if (FAILED (hr) || ! exRender || exFrames == 0)
        {
            setStatus (EngineStatus::Error, "Exclusive render client failed " + hrToString (hr));
            disconnect();
            return false;
        }
        exKind = chosen->kind;
        exBytes = chosen->container / 8;
        exBuf.assign (12, std::vector<float> (std::max<size_t> (exFrames, 4096), 0.0f));

        // Start with one buffer of silence (exclusive streams play the buffer as soon as they start).
        BYTE* data = nullptr;
        if (SUCCEEDED (exRender->GetBuffer (exFrames, &data)))
            exRender->ReleaseBuffer (exFrames, AUDCLNT_BUFFERFLAGS_SILENT);

        outRate.store ((double) fmt.Format.nSamplesPerSec);
        periodFrames.store ((int) exFrames);
        maxDynamic.store (0);
        feeder.consumerStart ((double) fmt.Format.nSamplesPerSec);

        hr = exClient->Start();
        if (FAILED (hr))
        {
            setStatus (EngineStatus::Error, "Exclusive 2ch Start failed " + hrToString (hr));
            disconnect();
            return false;
        }
        stereoActive.store (true);

        char info[256];
        std::snprintf (info, sizeof (info), "Connected (2ch exclusive): %u Hz, %u-bit (in %u), buffer %u frames (%.1f ms)",
                       (unsigned) fmt.Format.nSamplesPerSec, (unsigned) chosen->valid, (unsigned) chosen->container,
                       (unsigned) exFrames, 1000.0 * exFrames / fmt.Format.nSamplesPerSec);
        logLine (info + std::string ("  device=\"") + getName() + "\"");
        setStatus (EngineStatus::Running);
        return true;
    }

    // One exclusive period: the whole buffer. Returns false if the stream must be reconnected.
    bool renderExclusive()
    {
        BYTE* data = nullptr;
        HRESULT hr = exRender->GetBuffer (exFrames, &data);
        if (FAILED (hr) || data == nullptr)
        {
            logLine ("Exclusive GetBuffer failed " + hrToString (hr) + " -> reconnect");
            return false;
        }
        float* out[12];
        for (int k = 0; k < 12; ++k) out[k] = exBuf[(size_t) k].data();
        feeder.render (out, (int) exFrames);   // positions L/R carry the 2ch signal

        for (UINT32 i = 0; i < exFrames; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const float v = std::max (-1.0f, std::min (1.0f, out[c][i]));
                BYTE* p = data + ((size_t) i * 2 + (size_t) c) * (size_t) exBytes;
                if (exKind == SampleKind::Float32)    *reinterpret_cast<float*> (p) = v;
                else if (exKind == SampleKind::Int16) *reinterpret_cast<int16_t*> (p) = (int16_t) std::lrint (v * 32767.0f);
                else                                  *reinterpret_cast<int32_t*> (p) = (int32_t) std::llrint ((double) v * 2147483647.0);
            }
        hr = exRender->ReleaseBuffer (exFrames, 0);
        if (FAILED (hr))
        {
            logLine ("Exclusive ReleaseBuffer failed " + hrToString (hr) + " -> reconnect");
            return false;
        }
        return true;
    }

    std::string getName() const
    {
        std::lock_guard<std::mutex> g (infoLock);
        return deviceName;
    }

    // One ISAC period. Returns false if the stream must be reconnected.
    bool renderOnce()
    {
        UINT32 availableDynamic = 0, frameCount = 0;
        HRESULT hr = stream->BeginUpdatingAudioObjects (&availableDynamic, &frameCount);
        if (FAILED (hr))
        {
            logLine ("BeginUpdatingAudioObjects failed " + hrToString (hr) + " -> reconnect");
            return false;
        }
        periodFrames.store ((int) frameCount);

        BYTE* raw[12] = {};
        UINT32 frames = frameCount;
        bool ok = true;
        for (int k = 0; k < 12; ++k)
        {
            UINT32 bytes = 0;
            hr = objects[k]->GetBuffer (&raw[k], &bytes);
            if (FAILED (hr) || raw[k] == nullptr) { ok = false; break; }
            const UINT32 bps = (kind == SampleKind::Int16) ? 2u : 4u;
            frames = std::min (frames, bytes / bps);
        }

        if (ok && frames > 0)
        {
            if (kind == SampleKind::Float32)
            {
                float* out[12];
                for (int k = 0; k < 12; ++k) out[k] = reinterpret_cast<float*> (raw[k]);
                feeder.render (out, (int) frames);
            }
            else
            {
                if (convertBuf.size() != 12 || convertBuf[0].size() < frames)
                    convertBuf.assign (12, std::vector<float> (std::max<size_t> (frames, 4096), 0.0f));
                float* out[12];
                for (int k = 0; k < 12; ++k) out[k] = convertBuf[(size_t) k].data();
                feeder.render (out, (int) frames);
                for (int k = 0; k < 12; ++k)
                {
                    for (UINT32 i = 0; i < frames; ++i)
                    {
                        const float v = std::max (-1.0f, std::min (1.0f, out[k][i]));
                        if (kind == SampleKind::Int16) reinterpret_cast<int16_t*> (raw[k])[i] = (int16_t) (v * 32767.0f);
                        else                           reinterpret_cast<int32_t*> (raw[k])[i] = (int32_t) ((double) v * 2147483647.0);
                    }
                }
            }
        }

        hr = stream->EndUpdatingAudioObjects();
        if (FAILED (hr))
        {
            logLine ("EndUpdatingAudioObjects failed " + hrToString (hr) + " -> reconnect");
            return false;
        }
        if (! ok)
        {
            logLine ("GetBuffer failed " + hrToString (hr) + " -> reconnect");
            return false;
        }
        return true;
    }

    void run()
    {
        const HRESULT coHr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        DWORD taskIndex = 0;
        HANDLE avrt = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
        // Highest MMCSS priority inside the "Pro Audio" task (the output thread does very little work,
        // so this costs nothing but keeps it ahead of other audio threads when the CPU is busy).
        const bool critical = avrt != nullptr && AvSetMmThreadPriority (avrt, AVRT_PRIORITY_CRITICAL);
        logLine (avrt ? (critical ? "Output thread registered with MMCSS 'Pro Audio' (priority: critical)"
                                  : "Output thread registered with MMCSS 'Pro Audio' (critical priority refused)")
                      : "MMCSS registration failed");
        lockMemory();

        HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) enumerator.ReleaseAndGetAddressOf());
        if (FAILED (hr))
        {
            setStatus (EngineStatus::Error, "MMDeviceEnumerator failed " + hrToString (hr));
            enumerator.Reset();
            if (avrt) AvRevertMmThreadCharacteristics (avrt);
            if (SUCCEEDED (coHr)) CoUninitialize();
            return;
        }

        auto* notifier = new NotificationClient ([this] (bool d) { onDeviceChange (d); });
        enumerator->RegisterEndpointNotificationCallback (notifier);

        bool connected = false;
        int timeouts = 0;
        while (! stopFlag.load())
        {
            if (! connected)
            {
                reconnectFlag.store (false);
                connected = connect();
                if (! connected)
                {
                    reconnecting.store (true);
                    WaitForSingleObject (wakeEvent, 2000); // retry every 2 s or on device change
                    continue;
                }
                if (reconnecting.exchange (false)) reconnects.fetch_add (1);
                timeouts = 0;
            }

            if (reconnectFlag.exchange (false))
            {
                logLine ("Device change / reconnect request -> reopening stream");
                disconnect();
                connected = false;
                reconnecting.store (true);
                setStatus (EngineStatus::Connecting);
                continue;
            }

            HANDLE handles[2] = { bufferEvent, wakeEvent };
            const DWORD w = WaitForMultipleObjects (2, handles, FALSE, 200);
            if (w == WAIT_OBJECT_0 + 1) continue; // woken: re-check flags
            if (w != WAIT_OBJECT_0)
            {
                if (++timeouts >= 10) // 2 s without a buffer request: stream is dead
                {
                    logLine ("No buffer requests for 2 s -> reconnect");
                    disconnect();
                    connected = false;
                    reconnecting.store (true);
                }
                continue;
            }
            timeouts = 0;

            if (! (stereoActive.load() ? renderExclusive() : renderOnce()))
            {
                disconnect();
                connected = false;
                reconnecting.store (true);
                setStatus (EngineStatus::Connecting);
            }
        }

        disconnect();
        enumerator->UnregisterEndpointNotificationCallback (notifier);
        notifier->Release();
        enumerator.Reset();
        unlockMemory();
        if (avrt) AvRevertMmThreadCharacteristics (avrt);
        if (SUCCEEDED (coHr)) CoUninitialize();
        setStatus (EngineStatus::Stopped);
    }
};

SpatialEngine::SpatialEngine (OutputFeeder& feeder) : impl (std::make_unique<Impl> (feeder))
{
    impl->wakeEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    impl->bufferEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
}

SpatialEngine::~SpatialEngine()
{
    stop();
    if (impl->wakeEvent) CloseHandle (impl->wakeEvent);
    if (impl->bufferEvent) CloseHandle (impl->bufferEvent);
}

void SpatialEngine::start()
{
    if (impl->thread.joinable()) return;
    impl->stopFlag.store (false);
    logLine ("Engine start");
    impl->thread = std::thread ([this] { impl->run(); });
}

void SpatialEngine::stop()
{
    if (! impl->thread.joinable()) return;
    impl->stopFlag.store (true);
    SetEvent (impl->wakeEvent);
    impl->thread.join();
    logLine ("Engine stopped");
}

void SpatialEngine::setDeviceId (const std::string& endpointId)
{
    {
        std::lock_guard<std::mutex> g (impl->infoLock);
        if (impl->deviceIdWanted == endpointId) return;
        impl->deviceIdWanted = endpointId;
    }
    requestReconnect();
}

std::string SpatialEngine::getDeviceId() const
{
    std::lock_guard<std::mutex> g (impl->infoLock);
    return impl->deviceIdWanted;
}

void SpatialEngine::setStereo (bool stereo2ch)
{
    if (impl->stereoWanted.exchange (stereo2ch) == stereo2ch) return;
    logLine (std::string ("Output: ") + (stereo2ch ? "2ch Stereo (exclusive)" : "Atmos (spatial audio)"));
    requestReconnect();
}

bool SpatialEngine::isStereoActive() const noexcept { return impl->stereoActive.load(); }

void SpatialEngine::requestReconnect()
{
    impl->reconnectFlag.store (true);
    if (impl->wakeEvent) SetEvent (impl->wakeEvent);
}

EngineStatus SpatialEngine::getStatus() const noexcept   { return (EngineStatus) impl->status.load(); }
bool SpatialEngine::isReconnecting() const noexcept      { return impl->reconnecting.load(); }
std::string SpatialEngine::getDeviceName() const         { return impl->getName(); }
std::string SpatialEngine::getLastError() const
{
    std::lock_guard<std::mutex> g (impl->infoLock);
    return impl->lastError;
}
int SpatialEngine::getMaxDynamicObjects() const noexcept { return impl->maxDynamic.load(); }
double SpatialEngine::getOutputRate() const noexcept     { return impl->outRate.load(); }
int SpatialEngine::getPeriodFrames() const noexcept      { return impl->periodFrames.load(); }
uint32_t SpatialEngine::getReconnectCount() const noexcept { return impl->reconnects.load(); }
bool SpatialEngine::isMemoryLocked() const noexcept { return impl->memLocked.load(); }

std::vector<OutputDevice> SpatialEngine::listOutputDevices()
{
    std::vector<OutputDevice> result;
    const HRESULT coHr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);

    ComPtr<IMMDeviceEnumerator> en;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) en.ReleaseAndGetAddressOf())))
    {
        std::string defaultId;
        ComPtr<IMMDevice> def;
        if (SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, &def)))
            defaultId = deviceId (def.Get());

        ComPtr<IMMDeviceCollection> coll;
        if (SUCCEEDED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &coll)))
        {
            UINT count = 0;
            coll->GetCount (&count);
            for (UINT i = 0; i < count; ++i)
            {
                ComPtr<IMMDevice> d;
                if (FAILED (coll->Item (i, &d))) continue;
                OutputDevice od;
                od.id = deviceId (d.Get());
                od.name = deviceFriendlyName (d.Get());
                od.isDefault = (od.id == defaultId);
                result.push_back (od);
            }
        }
    }

    if (coHr == S_OK || coHr == S_FALSE) CoUninitialize();
    return result;
}
} // namespace ab

//==============================================================================
#else // not Windows: stub so the rest of the code can be compiled and tested elsewhere

namespace ab
{
struct SpatialEngine::Impl {};
SpatialEngine::SpatialEngine (OutputFeeder&) : impl (std::make_unique<Impl>()) {}
SpatialEngine::~SpatialEngine() = default;
void SpatialEngine::start() {}
void SpatialEngine::stop() {}
void SpatialEngine::setDeviceId (const std::string&) {}
void SpatialEngine::setStereo (bool) {}
bool SpatialEngine::isStereoActive() const noexcept { return false; }
std::string SpatialEngine::getDeviceId() const { return {}; }
void SpatialEngine::requestReconnect() {}
EngineStatus SpatialEngine::getStatus() const noexcept { return EngineStatus::Unsupported; }
bool SpatialEngine::isReconnecting() const noexcept { return false; }
std::string SpatialEngine::getDeviceName() const { return "Windows only"; }
std::string SpatialEngine::getLastError() const { return "ISpatialAudioClient is only available on Windows"; }
int SpatialEngine::getMaxDynamicObjects() const noexcept { return 0; }
double SpatialEngine::getOutputRate() const noexcept { return 48000.0; }
int SpatialEngine::getPeriodFrames() const noexcept { return 480; }
uint32_t SpatialEngine::getReconnectCount() const noexcept { return 0; }
bool SpatialEngine::isMemoryLocked() const noexcept { return false; }
std::vector<OutputDevice> SpatialEngine::listOutputDevices() { return {}; }
} // namespace ab

#endif
