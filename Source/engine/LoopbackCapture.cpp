#include "LoopbackCapture.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <set>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace ab
{
const char* toString (LoopbackStatus s) noexcept
{
    switch (s)
    {
        case LoopbackStatus::Stopped:     return "Stopped";
        case LoopbackStatus::Starting:    return "Starting";
        case LoopbackStatus::Running:     return "Running";
        case LoopbackStatus::NoDevice:    return "NoDevice";
        case LoopbackStatus::Error:       return "Error";
        case LoopbackStatus::Unsupported: return "Unsupported";
    }
    return "?";
}

int64_t LoopbackCapture::nowUs() noexcept
{
    using namespace std::chrono;
    return duration_cast<microseconds> (steady_clock::now().time_since_epoch()).count();
}

bool LoopbackCapture::isAutoCandidate (const std::string& friendlyName, bool strict)
{
    std::string n = friendlyName;
    for (auto& c : n) c = (char) std::tolower ((unsigned char) c);
    if (strict) return n.find ("cable in 16ch") != std::string::npos;
    return n.find ("cable in") != std::string::npos;
}

bool LoopbackCapture::isVirtualCable (const std::string& friendlyName)
{
    std::string n = friendlyName;
    for (auto& c : n) c = (char) std::tolower ((unsigned char) c);
    return n.find ("vb-audio") != std::string::npos;
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
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <mmreg.h>
#include <avrt.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

#ifndef AUDCLNT_E_ENGINE_PERIODICITY_LOCKED
 #define AUDCLNT_E_ENGINE_PERIODICITY_LOCKED AUDCLNT_ERR (0x028)
#endif

namespace ab
{
namespace
{
std::string utf8 (const wchar_t* w)
{
    if (w == nullptr || *w == 0) return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) (n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring wide (const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w ((size_t) (n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string hex (HRESULT hr)
{
    char b[16];
    std::snprintf (b, sizeof (b), "0x%08lX", (unsigned long) hr);
    return b;
}

std::string friendlyName (IMMDevice* d)
{
    ComPtr<IPropertyStore> props;
    if (FAILED (d->OpenPropertyStore (STGM_READ, &props))) return {};
    PROPVARIANT v;
    PropVariantInit (&v);
    std::string name;
    if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = utf8 (v.pwszVal);
    PropVariantClear (&v);
    return name;
}

std::string idOf (IMMDevice* d)
{
    LPWSTR w = nullptr;
    std::string id;
    if (SUCCEEDED (d->GetId (&w)) && w != nullptr) { id = utf8 (w); CoTaskMemFree (w); }
    return id;
}

const GUID kSubFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID kSubPcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

enum class Kind { F32, I16, I24, I32, Unsupported };

Kind kindOf (const WAVEFORMATEX* f)
{
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22)
    {
        const auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (f);
        if (IsEqualGUID (x->SubFormat, kSubFloat)) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (IsEqualGUID (x->SubFormat, kSubPcm)) tag = WAVE_FORMAT_PCM;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32) return Kind::F32;
    if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 16) return Kind::I16;
    if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 24) return Kind::I24;
    if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 32) return Kind::I32;
    return Kind::Unsupported;
}

int64_t qpcNow100ns()
{
    static const int64_t freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency (&f); return (int64_t) f.QuadPart; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter (&c);
    return (int64_t) ((long double) c.QuadPart * 10000000.0L / (long double) freq);
}
} // namespace

struct LoopbackCapture::Impl
{
    std::thread thread;
    std::atomic<bool> stopFlag { false }, started { false };
    HANDLE wakeEvent = nullptr, captureEvent = nullptr;

    std::string sourceId, excludeName;
    PacketFn onPacket;

    mutable std::mutex infoLock;
    std::string deviceName, lastError;
    std::atomic<int> status { (int) LoopbackStatus::Stopped };
    std::atomic<double> rate { 0.0 };
    std::atomic<int> channels { 0 };
    std::atomic<int64_t> lastPacketUs { 0 };
    std::atomic<uint32_t> packets { 0 }, discontinuities { 0 };
    std::atomic<bool> keeperOk { false };

    // short engine period (Ultra low latency+)
    bool wantShort = false, shortAllowed = true, shortChecked = false, restartNow = false, reopenAtOnce = false;
    std::atomic<bool> shortActive { false }, trialPending { false };
    std::string deviceId;

    // Devices on which the short period was tried without effect (this host session): never asked again.
    static std::mutex& noShortLock() { static std::mutex m; return m; }
    static std::set<std::string>& noShortDevices() { static std::set<std::string> s; return s; }
    static bool isNoShort (const std::string& id)
    {
        std::lock_guard<std::mutex> g (noShortLock());
        return noShortDevices().count (id) > 0;
    }
    static void rememberNoShort (const std::string& id)
    {
        std::lock_guard<std::mutex> g (noShortLock());
        noShortDevices().insert (id);
    }
    // Devices on which the short period did shorten the packets: no new trial when reopened.
    static std::set<std::string>& shortOkDevices() { static std::set<std::string> s; return s; }
    static bool isShortOk (const std::string& id)
    {
        std::lock_guard<std::mutex> g (noShortLock());
        return shortOkDevices().count (id) > 0;
    }
    static void rememberShortOk (const std::string& id)
    {
        std::lock_guard<std::mutex> g (noShortLock());
        shortOkDevices().insert (id);
    }
    std::atomic<int> periodNote { 0 };
    int64_t shortSinceUs = 0;
    std::atomic<UINT32> defaultPeriod { 0 };
    static constexpr int kPktHist = 128;
    std::array<std::atomic<uint32_t>, kPktHist> pktFrames {};
    std::atomic<uint32_t> pktIndex { 0 };
    ComPtr<IMMDevice> device;
    std::vector<uint8_t> mixCopy;

    // stream objects (capture thread only)
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IAudioClient> capClient, keepClient;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> keeper;
    UINT32 keepFrames = 0;
    Kind kind = Kind::Unsupported;
    int ch = 0, blockAlign = 0;
    double sr = 0.0;
    std::vector<float> conv;

    void setStatus (LoopbackStatus s, const std::string& err = {})
    {
        status.store ((int) s);
        if (! err.empty())
        {
            std::lock_guard<std::mutex> g (infoLock);
            if (err != lastError) logLine ("Direct loopback: " + err);
            lastError = err;
        }
    }

    ComPtr<IMMDevice> findDevice()
    {
        if (! sourceId.empty())
        {
            ComPtr<IMMDevice> d;
            if (SUCCEEDED (enumerator->GetDevice (wide (sourceId).c_str(), &d)) && d) return d;
            return nullptr;
        }
        ComPtr<IMMDeviceCollection> coll;
        if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &coll)) || ! coll) return nullptr;
        UINT n = 0;
        coll->GetCount (&n);
        ComPtr<IMMDevice> loose;
        for (UINT i = 0; i < n; ++i)
        {
            ComPtr<IMMDevice> d;
            if (FAILED (coll->Item (i, &d)) || ! d) continue;
            const auto name = friendlyName (d.Get());
            if (name == excludeName) continue;
            if (isAutoCandidate (name, true)) return d;
            if (! loose && isAutoCandidate (name, false)) loose = d;
        }
        return loose;
    }

    bool open()
    {
        setStatus (LoopbackStatus::Starting);
        auto dev = findDevice();
        if (! dev)
        {
            setStatus (LoopbackStatus::NoDevice, sourceId.empty() ? "CABLE In 16ch not found" : "selected device not available");
            return false;
        }
        const auto name = friendlyName (dev.Get());
        if (name == excludeName)
        {
            setStatus (LoopbackStatus::Error, "refusing to capture the Atmos output device (feedback)");
            return false;
        }

        HRESULT hr = dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) capClient.ReleaseAndGetAddressOf());
        if (FAILED (hr)) { setStatus (LoopbackStatus::Error, "Activate failed " + hex (hr)); return false; }

        WAVEFORMATEX* mix = nullptr;
        hr = capClient->GetMixFormat (&mix);
        if (FAILED (hr) || ! mix) { setStatus (LoopbackStatus::Error, "GetMixFormat failed " + hex (hr)); return false; }
        kind = kindOf (mix);
        ch = (int) mix->nChannels;
        blockAlign = (int) mix->nBlockAlign;
        sr = (double) mix->nSamplesPerSec;
        if (kind == Kind::Unsupported || ch <= 0)
        {
            CoTaskMemFree (mix);
            setStatus (LoopbackStatus::Error, "unsupported mix format");
            return false;
        }

        // Silence keeper first: a silent render stream on the same endpoint keeps the engine (and the loopback)
        // running; with the short period requested it sets the engine period before the loopback stream opens.
        device = dev;
        deviceId = idOf (dev.Get());
        mixCopy.assign (reinterpret_cast<const uint8_t*> (mix), reinterpret_cast<const uint8_t*> (mix) + sizeof (WAVEFORMATEX) + mix->cbSize);
        bool tryShort = wantShort && shortAllowed;
        if (tryShort && LoopbackCapture::isVirtualCable (name))
        {
            tryShort = false;
            periodNote.store ((int) PeriodNote::SkippedVirtual);
            logLine ("Direct loopback: short engine period not requested (VB-Audio cable: packets stay 10 ms)");
        }
        else if (tryShort && isNoShort (deviceId))
        {
            tryShort = false;
            periodNote.store ((int) PeriodNote::SkippedKnown);
            logLine ("Direct loopback: short engine period not requested (no effect on this device before)");
        }
        openKeeper (tryShort);

        // Event-driven loopback; the 100 ms buffer only holds data if this thread is ever late (no added latency).
        hr = capClient->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    1000000, 0, mix, nullptr);
        if (SUCCEEDED (hr)) hr = capClient->SetEventHandle (captureEvent);
        if (SUCCEEDED (hr)) hr = capClient->GetService (__uuidof (IAudioCaptureClient), (void**) capture.ReleaseAndGetAddressOf());
        UINT32 bufFrames = 0;
        if (SUCCEEDED (hr)) hr = capClient->GetBufferSize (&bufFrames);
        if (FAILED (hr))
        {
            CoTaskMemFree (mix);
            setStatus (LoopbackStatus::Error, "loopback Initialize failed " + hex (hr));
            close();
            return false;
        }
        conv.assign ((size_t) std::max<UINT32> (bufFrames, 4096) * (size_t) ch, 0.0f);

        CoTaskMemFree (mix);

        hr = capClient->Start();
        if (FAILED (hr)) { setStatus (LoopbackStatus::Error, "loopback Start failed " + hex (hr)); close(); return false; }

        rate.store (sr);
        channels.store (ch);
        {
            std::lock_guard<std::mutex> g (infoLock);
            deviceName = name;
            lastError.clear();
        }
        lastPacketUs.store (nowUs());
        char info[200];
        std::snprintf (info, sizeof (info), "Direct loopback open: %.0f Hz, %d ch, buffer %u frames, keeper=%d  device=\"",
                       sr, ch, (unsigned) bufFrames, keeperOk.load() ? 1 : 0);
        logLine (info + name + "\"");
        setStatus (LoopbackStatus::Running);
        return true;
    }

    void closeKeeper()
    {
        if (keepClient) keepClient->Stop();
        keeper.Reset();
        keepClient.Reset();
        keeperOk.store (false);
        shortActive.store (false);
        trialPending.store (false);
    }

    // Silent render stream; with tryShort it first asks for a ~5 ms engine period (IAudioClient3).
    void openKeeper (bool tryShort)
    {
        closeKeeper();
        const auto* mix = reinterpret_cast<const WAVEFORMATEX*> (mixCopy.data());
        if (! device || mixCopy.empty()) return;
        if (tryShort)
        {
            ComPtr<IAudioClient3> c3;
            UINT32 def = 0, fund = 0, mn = 0, mx = 0;
            if (FAILED (device->Activate (__uuidof (IAudioClient3), CLSCTX_ALL, nullptr, (void**) c3.GetAddressOf())) || ! c3
                || FAILED (c3->GetSharedModeEnginePeriod (mix, &def, &fund, &mn, &mx)) || fund == 0)
            {
                periodNote.store ((int) PeriodNote::Unsupported);
                rememberNoShort (deviceId);
            }
            else
            {
                defaultPeriod = def;
                UINT32 period = std::max (mn, (UINT32) std::lround (sr * 0.005));
                period = ((period + fund - 1) / fund) * fund;
                if (mn >= def || period >= def || period > mx)
                {
                    periodNote.store ((int) PeriodNote::Unsupported);
                    rememberNoShort (deviceId);
                    logLine ("Direct loopback: short engine period not offered by the driver (default " + std::to_string (def)
                             + ", min " + std::to_string (mn) + " frames)");
                }
                else
                {
                    const HRESULT hr = c3->InitializeSharedAudioStream (0, period, mix, nullptr);
                    if (SUCCEEDED (hr))
                    {
                        keepClient = c3;
                        shortActive.store (true);
                        shortSinceUs = nowUs();
                        shortChecked = isShortOk (deviceId);          // proven before: no new trial
                        trialPending.store (! shortChecked);
                        periodNote.store ((int) PeriodNote::Active);
                        logLine ("Direct loopback: short engine period " + std::to_string (period) + " frames requested (default "
                                 + std::to_string (def) + ")");
                    }
                    else
                    {
                        periodNote.store ((int) (hr == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED ? PeriodNote::Locked : PeriodNote::Failed));
                        if (hr != AUDCLNT_E_ENGINE_PERIODICITY_LOCKED) rememberNoShort (deviceId);
                        logLine ("Direct loopback: short engine period refused " + hex (hr));
                    }
                }
            }
        }
        else if (! wantShort)
            periodNote.store ((int) PeriodNote::NotRequested);

        if (! keepClient)
        {
            if (FAILED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) keepClient.ReleaseAndGetAddressOf()))
                || FAILED (keepClient->Initialize (AUDCLNT_SHAREMODE_SHARED, 0, 500000, 0, mix, nullptr)))
                keepClient.Reset();
        }
        if (keepClient
            && SUCCEEDED (keepClient->GetService (__uuidof (IAudioRenderClient), (void**) keeper.ReleaseAndGetAddressOf()))
            && SUCCEEDED (keepClient->GetBufferSize (&keepFrames)))
        {
            topUpSilence();
            keeperOk.store (SUCCEEDED (keepClient->Start()));
        }
        if (! keeperOk.load())
        {
            closeKeeper();
            logLine ("Direct loopback: silence keeper not available (loopback pauses while nothing plays)");
        }
    }

    // After ~3 s with the short period (a trial: the input is not used yet): if the loopback packets did not
    // get shorter, reopen BOTH streams at the normal period. Reopening only the keeper is not enough: the
    // capture stream opened during the short period keeps an irregular packet timing.
    void checkShortPeriod()
    {
        if (! shortActive.load() || shortChecked || nowUs() - shortSinceUs < 3000000) return;
        const int frames = typicalPacketFrames();
        if (frames <= 0) return;                // no packets yet: decide when there are some
        shortChecked = true;
        if (frames > 0 && defaultPeriod > 0 && (UINT32) frames * 4 > defaultPeriod * 3)
        {
            logLine ("Direct loopback: loopback packets stay at " + std::to_string (frames)
                     + " frames with the short period -> reopening at the normal period (device not asked again)");
            shortAllowed = false;
            rememberNoShort (deviceId);
            periodNote.store ((int) PeriodNote::NoEffect);
            restartNow = true;              // run(): status "Starting", close and reopen right away
        }
        else
        {
            logLine ("Direct loopback: loopback packets " + std::to_string (frames) + " frames with the short period");
            rememberShortOk (deviceId);
            trialPending.store (false);
        }
    }

    int typicalPacketFrames() const noexcept
    {
        // most frequent packet length of the last 128 packets
        std::array<uint32_t, kPktHist> v {};
        int n = 0;
        for (auto& f : pktFrames) { const uint32_t x = f.load (std::memory_order_relaxed); if (x > 0) v[(size_t) n++] = x; }
        if (n == 0) return 0;
        std::sort (v.begin(), v.begin() + n);
        uint32_t best = v[0]; int bestN = 0;
        for (int i = 0; i < n;)
        {
            int j = i; while (j < n && v[(size_t) j] == v[(size_t) i]) ++j;
            if (j - i > bestN) { bestN = j - i; best = v[(size_t) i]; }
            i = j;
        }
        return (int) best;
    }

    void close()
    {
        if (capClient) capClient->Stop();
        closeKeeper();
        capture.Reset();
        capClient.Reset();
        device.Reset();
    }

    void topUpSilence()
    {
        if (! keeper || ! keepClient) return;
        UINT32 pad = 0;
        if (FAILED (keepClient->GetCurrentPadding (&pad)) || pad >= keepFrames) return;
        const UINT32 n = keepFrames - pad;
        BYTE* p = nullptr;
        if (SUCCEEDED (keeper->GetBuffer (n, &p)))
            keeper->ReleaseBuffer (n, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    // Reads every packet that is ready. Returns false if the stream must be reopened.
    bool drain()
    {
        for (;;)
        {
            UINT32 next = 0;
            HRESULT hr = capture->GetNextPacketSize (&next);
            if (FAILED (hr)) { setStatus (LoopbackStatus::Error, "GetNextPacketSize failed " + hex (hr)); return false; }
            if (next == 0) return true;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devPos = 0, qpc = 0;
            hr = capture->GetBuffer (&data, &frames, &flags, &devPos, &qpc);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) return true;
            if (FAILED (hr)) { setStatus (LoopbackStatus::Error, "GetBuffer failed " + hex (hr)); return false; }

            // Lateness: now vs. the end of this packet (its first frame's timestamp + its length).
            double lateMs = 0.0;
            if ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) == 0 && qpc != 0)
                lateMs = std::clamp ((double) (qpcNow100ns() - (int64_t) qpc) / 10000.0 - 1000.0 * frames / sr, 0.0, 1000.0);
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) discontinuities.fetch_add (1);

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr;
            UINT32 done = 0;
            const UINT32 maxFrames = (UINT32) (conv.size() / (size_t) ch);
            while (done < frames)
            {
                const UINT32 n = std::min (frames - done, maxFrames);
                float* dst = conv.data();
                const size_t count = (size_t) n * (size_t) ch;
                if (silent)
                    std::fill (dst, dst + count, 0.0f);
                else
                {
                    const BYTE* src = data + (size_t) done * (size_t) blockAlign;
                    switch (kind)
                    {
                        case Kind::F32: std::memcpy (dst, src, count * sizeof (float)); break;
                        case Kind::I16: { auto* s = reinterpret_cast<const int16_t*> (src); for (size_t i = 0; i < count; ++i) dst[i] = s[i] * (1.0f / 32768.0f); } break;
                        case Kind::I32: { auto* s = reinterpret_cast<const int32_t*> (src); for (size_t i = 0; i < count; ++i) dst[i] = (float) (s[i] * (1.0 / 2147483648.0)); } break;
                        case Kind::I24:
                            for (size_t i = 0; i < count; ++i)
                            {
                                const BYTE* b = src + i * 3;
                                const int32_t v = (int32_t) ((uint32_t) b[0] << 8 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 24) >> 8;
                                dst[i] = v * (1.0f / 8388608.0f);
                            }
                            break;
                        case Kind::Unsupported: default: std::fill (dst, dst + count, 0.0f); break;
                    }
                }
                if (onPacket) onPacket (dst, (int) n, ch, lateMs, silent);
                done += n;
            }

            capture->ReleaseBuffer (frames);
            pktFrames[(size_t) (pktIndex.fetch_add (1, std::memory_order_relaxed) % kPktHist)].store (frames, std::memory_order_relaxed);
            packets.fetch_add (1, std::memory_order_relaxed);
            lastPacketUs.store (nowUs(), std::memory_order_relaxed);
        }
    }

    void run()
    {
        const HRESULT coHr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        DWORD taskIndex = 0;
        HANDLE avrt = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
        if (avrt) AvSetMmThreadPriority (avrt, AVRT_PRIORITY_CRITICAL);

        HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                       (void**) enumerator.ReleaseAndGetAddressOf());
        if (FAILED (hr)) setStatus (LoopbackStatus::Error, "MMDeviceEnumerator failed " + hex (hr));

        while (! stopFlag.load() && enumerator)
        {
            if (! open())
            {
                close();
                WaitForSingleObject (wakeEvent, 2000);   // retry every 2 s
                continue;
            }
            int quiet = 0;
            while (! stopFlag.load())
            {
                HANDLE h[2] = { captureEvent, wakeEvent };
                const DWORD w = WaitForMultipleObjects (2, h, FALSE, 20);
                if (stopFlag.load()) break;
                topUpSilence();
                checkShortPeriod();
                if (restartNow) { restartNow = false; reopenAtOnce = true; setStatus (LoopbackStatus::Starting); break; }
                if (w == WAIT_TIMEOUT && ++quiet < 100) { if (! drain()) break; continue; }
                quiet = 0;
                if (! drain()) break;
                if (nowUs() - lastPacketUs.load() > 2000000)   // 2 s without data: reopen
                {
                    setStatus (LoopbackStatus::Error, "no loopback data for 2 s");
                    break;
                }
            }
            close();
            if (reopenAtOnce) { reopenAtOnce = false; continue; }
            if (! stopFlag.load()) WaitForSingleObject (wakeEvent, 500);
        }

        close();
        enumerator.Reset();
        if (avrt) AvRevertMmThreadCharacteristics (avrt);
        if (SUCCEEDED (coHr)) CoUninitialize();
        status.store ((int) LoopbackStatus::Stopped);
    }
};

LoopbackCapture::LoopbackCapture() : impl (std::make_unique<Impl>())
{
    impl->wakeEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    impl->captureEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
}

LoopbackCapture::~LoopbackCapture()
{
    stop();
    if (impl->wakeEvent) CloseHandle (impl->wakeEvent);
    if (impl->captureEvent) CloseHandle (impl->captureEvent);
}

void LoopbackCapture::start (const std::string& sourceId, const std::string& excludeName, PacketFn onPacket, bool shortPeriod)
{
    stop();
    impl->wantShort = shortPeriod;
    impl->shortAllowed = true;
    impl->restartNow = false;
    impl->reopenAtOnce = false;
    impl->trialPending.store (false);
    impl->periodNote.store (0);
    for (auto& f : impl->pktFrames) f.store (0);
    impl->sourceId = sourceId;
    impl->excludeName = excludeName;
    impl->onPacket = std::move (onPacket);
    impl->stopFlag.store (false);
    impl->packets.store (0);
    impl->lastPacketUs.store (0);
    impl->started.store (true);
    impl->thread = std::thread ([this] { impl->run(); });
}

void LoopbackCapture::stop()
{
    if (! impl->started.exchange (false)) return;
    impl->stopFlag.store (true);
    SetEvent (impl->wakeEvent);
    if (impl->thread.joinable()) impl->thread.join();
    impl->onPacket = nullptr;
    impl->status.store ((int) LoopbackStatus::Stopped);
}

bool LoopbackCapture::isStarted() const noexcept            { return impl->started.load(); }
LoopbackStatus LoopbackCapture::getStatus() const noexcept  { return (LoopbackStatus) impl->status.load(); }
std::string LoopbackCapture::getDeviceName() const          { std::lock_guard<std::mutex> g (impl->infoLock); return impl->deviceName; }
std::string LoopbackCapture::getLastError() const           { std::lock_guard<std::mutex> g (impl->infoLock); return impl->lastError; }
double LoopbackCapture::getSampleRate() const noexcept      { return impl->rate.load(); }
int LoopbackCapture::getChannels() const noexcept           { return impl->channels.load(); }
int64_t LoopbackCapture::getLastPacketUs() const noexcept   { return impl->lastPacketUs.load(); }
uint32_t LoopbackCapture::getPackets() const noexcept       { return impl->packets.load(); }
uint32_t LoopbackCapture::getDiscontinuities() const noexcept { return impl->discontinuities.load(); }
bool LoopbackCapture::hasSilenceKeeper() const noexcept     { return impl->keeperOk.load(); }
int LoopbackCapture::getPacketFrames() const noexcept       { return impl->typicalPacketFrames(); }
double LoopbackCapture::getPacketMs() const noexcept
{
    const double r = impl->rate.load();
    return r > 0.0 ? 1000.0 * (double) impl->typicalPacketFrames() / r : 0.0;
}
bool LoopbackCapture::isShortPeriodActive() const noexcept
{
    if (! impl->shortActive.load() || impl->defaultPeriod == 0) return false;
    const int f = impl->typicalPacketFrames();
    return f > 0 && (UINT32) f * 4 <= impl->defaultPeriod * 3;
}
LoopbackCapture::PeriodNote LoopbackCapture::getPeriodNote() const noexcept { return (PeriodNote) impl->periodNote.load(); }
bool LoopbackCapture::isPeriodTrialPending() const noexcept { return impl->trialPending.load(); }
} // namespace ab

//==============================================================================
#else // not Windows: no loopback (the plugin keeps using the host input)

namespace ab
{
struct LoopbackCapture::Impl { bool started = false; };
LoopbackCapture::LoopbackCapture() : impl (std::make_unique<Impl>()) {}
LoopbackCapture::~LoopbackCapture() = default;
void LoopbackCapture::start (const std::string&, const std::string&, PacketFn, bool) { impl->started = true; }
void LoopbackCapture::stop() { impl->started = false; }
bool LoopbackCapture::isStarted() const noexcept { return impl->started; }
LoopbackStatus LoopbackCapture::getStatus() const noexcept { return impl->started ? LoopbackStatus::Unsupported : LoopbackStatus::Stopped; }
std::string LoopbackCapture::getDeviceName() const { return {}; }
std::string LoopbackCapture::getLastError() const { return "WASAPI loopback is only available on Windows"; }
double LoopbackCapture::getSampleRate() const noexcept { return 0.0; }
int LoopbackCapture::getChannels() const noexcept { return 0; }
int64_t LoopbackCapture::getLastPacketUs() const noexcept { return 0; }
uint32_t LoopbackCapture::getPackets() const noexcept { return 0; }
uint32_t LoopbackCapture::getDiscontinuities() const noexcept { return 0; }
bool LoopbackCapture::hasSilenceKeeper() const noexcept { return false; }
double LoopbackCapture::getPacketMs() const noexcept { return 0.0; }
int LoopbackCapture::getPacketFrames() const noexcept { return 0; }
bool LoopbackCapture::isShortPeriodActive() const noexcept { return false; }
LoopbackCapture::PeriodNote LoopbackCapture::getPeriodNote() const noexcept { return PeriodNote::NotRequested; }
bool LoopbackCapture::isPeriodTrialPending() const noexcept { return false; }
} // namespace ab

#endif
