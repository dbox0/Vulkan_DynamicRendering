#pragma once
#include <cstdio>
#include <cstdlib>
#include <string_view>

// For programmer errors that must be loud in RELEASE builds too: registering a
// type twice, reusing a live Guid, a reflection table that does not match its
// own data. assert() compiles out exactly where these would do the most
// damage -- silently, in a build someone is actually editing a scene with.
//
// Not for user or file errors. A hand-edited scene with a duplicate Guid is a
// load error to report and recover from, never a reason to abort.
[[noreturn]] inline void fatalError(std::string_view message)
{
    std::fprintf(stderr, "[fatal] %.*s\n", static_cast<int>(message.size()), message.data());
    std::fflush(stderr);
    std::abort();
}
