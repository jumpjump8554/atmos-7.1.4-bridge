#pragma once
// Small platform helpers that do not depend on JUCE: Downloads folder lookup and a file logger.

#include <string>
#include <mutex>
#include <fstream>

namespace ab
{
// UTF-8 path of the user's Downloads folder (Windows known folder; falls back to ~/Downloads).
std::string getDownloadsFolderUtf8();

// UTF-8 path of <Downloads>/AtmosBridge (created on demand).
std::string getAppDataFolderUtf8();

// Thread-safe line logger. Never call from the host audio thread.
class FileLogger
{
public:
    static FileLogger& get();

    void log (const std::string& message);
    std::string getPathUtf8() const { return pathUtf8; }

private:
    FileLogger();
    void openIfNeeded();

    std::mutex lock;
    std::ofstream stream;
    std::string pathUtf8;
    bool opened = false;
};

inline void logLine (const std::string& message) { FileLogger::get().log (message); }
} // namespace ab
