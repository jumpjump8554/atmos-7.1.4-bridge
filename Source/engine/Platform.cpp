#include "Platform.h"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <initguid.h>
 #include <knownfolders.h>
 #include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace ab
{
#if defined (_WIN32)
static std::string wideToUtf8 (const wchar_t* w)
{
    if (w == nullptr || *w == 0) return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) (n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

std::string getDownloadsFolderUtf8()
{
#if defined (_WIN32)
    PWSTR p = nullptr;
    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &p)) && p != nullptr)
    {
        std::string s = wideToUtf8 (p);
        CoTaskMemFree (p);
        if (! s.empty()) return s;
    }
    if (const char* up = std::getenv ("USERPROFILE"))
        return std::string (up) + "\\Downloads";
    return "C:\\";
#else
    const char* home = std::getenv ("HOME");
    return std::string (home ? home : ".") + "/Downloads";
#endif
}

std::string getAppDataFolderUtf8()
{
    const fs::path dir = fs::u8path (getDownloadsFolderUtf8()) / "AtmosBridge";
    std::error_code ec;
    fs::create_directories (dir, ec);
    return dir.u8string();
}

FileLogger& FileLogger::get()
{
    static FileLogger instance;
    return instance;
}

FileLogger::FileLogger() = default;

void FileLogger::openIfNeeded()
{
    if (opened) return;
    opened = true;
    std::error_code ec;
    const fs::path dir = fs::u8path (getAppDataFolderUtf8()) / "Logs";
    fs::create_directories (dir, ec);
    const fs::path file = dir / "atmosbridge.log";

    // Keep the log small: rotate at 1 MB.
    if (fs::exists (file, ec) && fs::file_size (file, ec) > 1024 * 1024)
    {
        fs::remove (dir / "atmosbridge.old.log", ec);
        fs::rename (file, dir / "atmosbridge.old.log", ec);
    }
    stream.open (file, std::ios::out | std::ios::app);
    pathUtf8 = file.u8string();
}

void FileLogger::log (const std::string& message)
{
    std::lock_guard<std::mutex> g (lock);
    openIfNeeded();
    if (! stream.is_open()) return;

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t (now);
    const auto ms = (int) (std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch()).count() % 1000);
    std::tm tmv {};
#if defined (_WIN32)
    localtime_s (&tmv, &t);
#else
    localtime_r (&t, &tmv);
#endif
    char ts[32];
    std::snprintf (ts, sizeof (ts), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                   tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
    stream << ts << "  " << message << "\n";
    stream.flush();
}
} // namespace ab
