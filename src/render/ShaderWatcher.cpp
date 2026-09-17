#include "ShaderWatcher.h"

#include <utility>

ShaderWatcher::ShaderWatcher(std::filesystem::path dir, std::chrono::milliseconds interval)
    : m_dir(std::move(dir)), m_interval(interval), m_stamps(scan())
{
}

ShaderWatcher::StampMap ShaderWatcher::scan() const
{
    StampMap stamps;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const Stamp stamp = entry.last_write_time(ec);
        if (!ec) {
            stamps.emplace(entry.path().filename().string(), stamp);
        }
    }
    return stamps;
}

std::vector<std::string> ShaderWatcher::poll()
{
    std::vector<std::string> changed;

    const Clock::time_point now = Clock::now();
    if (now < m_nextScan) {
        return changed;
    }
    m_nextScan = now + m_interval;

    for (const auto &[name, stamp] : scan()) {
        auto [known, inserted] = m_stamps.try_emplace(name, stamp);
        if (inserted || known->second == stamp) {
            // New file (nothing references it yet) or untouched.
            m_pending.erase(name);
            continue;
        }

        // Only report once the new stamp has held for a whole interval, so a
        // save that lands in more than one write isn't compiled half-done.
        const auto pending = m_pending.find(name);
        if (pending != m_pending.end() && pending->second == stamp) {
            known->second = stamp;
            m_pending.erase(pending);
            changed.push_back(name);
        } else {
            m_pending[name] = stamp;
        }
    }
    return changed;
}
