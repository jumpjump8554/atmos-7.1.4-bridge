#include "LoopbackCapture.h"
#include "Platform.h"

#include <algorithm>
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

        // Silence keeper: a silent render stream on the same endpoint keeps the engine (and the loopback) running.
        keeperOk.store (false);
        if (SUCCEEDED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) keepClient.ReleaseAndGetAddressOf())))
        {
            if (SUCCEEDED (keepClient->Initialize (AUDCLNT_SHAREMODE_SHARED, 0, 500000, 0, mix, nullptr))
                && SUCCEEDED (keepClient->GetService (__uuidof (IAudioRenderClient), (void**) keeper.ReleaseAndGetAddressOf()))
                && SUCCEEDED (keepClient->GetBufferSize (&keepFrames)))
            {
                topUpSilence();
                keeperOk.store (SUCCEEDED (keepClient->Start()));
            }
        }
        if (! keeperOk.load())
        {
            keeper.Reset();
            keepClient.Reset();
            logLine ("Direct loopback: silence keeper not available (loopback pauses while nothing plays)");
        }
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

    void close()
    {
        if (capClient) capClient->Stop();
        if (keepClient) keepClient->Stop();
        capture.Reset();
        keeper.Reset();
        capClient.Reset();
        keepClient.Reset();
        keeperOk.store (false);
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

void LoopbackCapture::start (const std::string& sourceId, const std::string& excludeName, PacketFn onPacket)
{
    stop();
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
} // namespace ab

//==============================================================================
#else // not Windows: no loopback (the plugin keeps using the host input)

namespace ab
{
struct LoopbackCapture::Impl { bool started = false; };
LoopbackCapture::LoopbackCapture() : impl (std::make_unique<Impl>()) {}
LoopbackCapture::~LoopbackCapture() = default;
void LoopbackCapture::start (const std::string&, const std::string&, PacketFn) { impl->started = true; }
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
} // namespace ab

#endif
