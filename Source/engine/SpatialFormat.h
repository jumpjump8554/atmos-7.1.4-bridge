#pragma once
// Selects "Dolby Atmos for home theater" as the spatial sound format of an output device
// (Windows.Media.Audio.SpatialAudioDeviceConfiguration, Windows 10 1809+). Windows may refuse it
// for desktop applications; the caller then asks the user to select it in the sound settings.

#include <string>

namespace ab
{
enum class SpatialFormatResult { Succeeded, AlreadyActive, NotSupported, AccessDenied, Failed };
const char* toString (SpatialFormatResult r) noexcept;

// endpointId: the MMDevice endpoint ID ({0.0.0.00000000}.{...}). Blocks up to ~3 s.
SpatialFormatResult enableDolbyAtmosForHomeTheater (const std::wstring& endpointId);
} // namespace ab
