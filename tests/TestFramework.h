#pragma once
#include <cstdio>

namespace test
{
    inline int failures = 0;
    inline int checks   = 0;

    inline void section(const char *name) { std::printf("%s\n", name); }
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++test::checks;                                                          \
        if (!(cond)) {                                                           \
            ++test::failures;                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

using test::section;
