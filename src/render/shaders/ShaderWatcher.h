#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Polls a directory's mtimes rather than using inotify
class ShaderWatcher
{
public:
    explicit ShaderWatcher(std::filesystem::path dir,
                           std::chrono::milliseconds interval = std::chrono::milliseconds(250));

    // File names (not paths) that changed since the last report. Cheap to call
    // every frameonly touches the disk once per interval.
    std::vector<std::string> poll();

private:
    using Clock    = std::chrono::steady_clock;
    using Stamp    = std::filesystem::file_time_type;
    using StampMap = std::unordered_map<std::string, Stamp>;

    StampMap scan() const;

    std::filesystem::path     m_dir;
    std::chrono::milliseconds m_interval;
    Clock::time_point         m_nextScan{};
    StampMap                  m_stamps;   // last stamp that was reported
    StampMap                  m_pending;  // changed, waiting to settle
};
