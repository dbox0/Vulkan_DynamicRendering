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

    for (const auto &entry : std::filesystem::recursive_directory_iterator(m_dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const Stamp stamp = entry.last_write_time(ec);
        if (ec) {
            continue;
        }
        const std::filesystem::path rel = std::filesystem::relative(entry.path(), m_dir, ec);
        if (!ec) {
            stamps.emplace(rel.generic_string(), stamp);
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
        const auto known = m_stamps.find(name);
        if (known != m_stamps.end() && known->second == stamp) {
            m_pending.erase(name);
            continue;
        }

        // Changed or new. New files are reported too:
        // a missing #include that  just got created is a dependency of whatever
        // failed to compile.
        // Only report once the stamp has held for a whole interval
        // a save that lands in more than one write isn't compiled half-done.
        const auto pending = m_pending.find(name);
        if (pending != m_pending.end() && pending->second == stamp) {
            m_stamps[name] = stamp;
            m_pending.erase(pending);
            changed.push_back(name);
        } else {
            m_pending[name] = stamp;
        }
    }
    return changed;
}
