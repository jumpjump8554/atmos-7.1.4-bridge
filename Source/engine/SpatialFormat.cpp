#include "SpatialFormat.h"

namespace ab
{
const char* toString (SpatialFormatResult r) noexcept
{
    switch (r)
    {
        case SpatialFormatResult::Succeeded:     return "turned on";
        case SpatialFormatResult::AlreadyActive: return "already on";
        case SpatialFormatResult::NotSupported:  return "not supported by this device";
        case SpatialFormatResult::AccessDenied:  return "refused by Windows (access denied)";
        case SpatialFormatResult::Failed:        return "failed";
    }
    return "?";
}
} // namespace ab

#if defined (_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.media.audio.h>

#pragma comment (lib, "runtimeobject.lib")

namespace ab
{
SpatialFormatResult enableDolbyAtmosForHomeTheater (const std::wstring& endpointId)
{
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::Wrappers::HString;
    using Microsoft::WRL::Wrappers::HStringReference;
    namespace Audio = ABI::Windows::Media::Audio;
    namespace Found = ABI::Windows::Foundation;

    if (endpointId.empty()) return SpatialFormatResult::Failed;

    // WinRT device id of an audio render endpoint
    const std::wstring devId = L"\\\\?\\SWD#MMDEVAPI#" + endpointId + L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

    ComPtr<Audio::ISpatialAudioDeviceConfigurationStatics> statics;
    HRESULT hr = RoGetActivationFactory (HStringReference (L"Windows.Media.Audio.SpatialAudioDeviceConfiguration").Get(),
                                         IID_PPV_ARGS (&statics));
    if (FAILED (hr) || ! statics) return SpatialFormatResult::Failed;

    ComPtr<Audio::ISpatialAudioDeviceConfiguration> cfg;
    hr = statics->GetForDeviceId (HStringReference (devId.c_str()).Get(), &cfg);
    if (FAILED (hr) || ! cfg) return SpatialFormatResult::Failed;

    ComPtr<Audio::ISpatialAudioFormatSubtypeStatics> subtypes;
    hr = RoGetActivationFactory (HStringReference (L"Windows.Media.Audio.SpatialAudioFormatSubtype").Get(), IID_PPV_ARGS (&subtypes));
    if (FAILED (hr) || ! subtypes) return SpatialFormatResult::Failed;

    HString atmos;
    if (FAILED (subtypes->get_DolbyAtmosForHomeTheater (atmos.GetAddressOf()))) return SpatialFormatResult::Failed;

    boolean supported = false;
    if (FAILED (cfg->IsSpatialAudioFormatSupported (atmos.Get(), &supported)) || ! supported)
        return SpatialFormatResult::NotSupported;

    HString active;
    if (SUCCEEDED (cfg->get_ActiveSpatialAudioFormat (active.GetAddressOf())))
    {
        int cmp = 1;
        if (SUCCEEDED (WindowsCompareStringOrdinal (active.Get(), atmos.Get(), &cmp)) && cmp == 0)
            return SpatialFormatResult::AlreadyActive;
    }

    ComPtr<Found::IAsyncOperation<Audio::SetDefaultSpatialAudioFormatResult*>> op;
    hr = cfg->SetDefaultSpatialAudioFormatAsync (atmos.Get(), &op);
    if (FAILED (hr) || ! op) return hr == E_ACCESSDENIED ? SpatialFormatResult::AccessDenied : SpatialFormatResult::Failed;

    // Wait for the result (GetResults fails with E_ILLEGAL_METHOD_CALL until the operation has completed).
    ComPtr<Audio::ISetDefaultSpatialAudioFormatResult> result;
    for (int i = 0; i < 300; ++i)
    {
        hr = op->GetResults (&result);
        if (hr != E_ILLEGAL_METHOD_CALL) break;
        Sleep (10);
    }
    if (FAILED (hr) || ! result) return SpatialFormatResult::Failed;

    Audio::SetDefaultSpatialAudioFormatStatus status {};
    if (FAILED (result->get_Status (&status))) return SpatialFormatResult::Failed;
    switch ((int) status)
    {
        case 0:  return SpatialFormatResult::Succeeded;      // Succeeded
        case 1:  return SpatialFormatResult::AccessDenied;   // AccessDenied
        case 4:  return SpatialFormatResult::NotSupported;   // NotSupportedOnAudioEndpoint
        default: return SpatialFormatResult::Failed;         // license / unknown
    }
}
} // namespace ab

#else

namespace ab
{
SpatialFormatResult enableDolbyAtmosForHomeTheater (const std::wstring&) { return SpatialFormatResult::Failed; }
} // namespace ab

#endif
