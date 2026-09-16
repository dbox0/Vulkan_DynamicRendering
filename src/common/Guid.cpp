#include "Guid.h"

#include <charconv>
#include <chrono>
#include <random>

Guid Guid::generate()
{
    // One engine per thread, seeded once. random_device alone has been
    // deterministic on some toolchains (old MinGW), so the clock is mixed in:
    // two editor sessions must never produce the same sequence.
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        const auto now = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());

        std::seed_seq seed{ device(), device(), device(), device(),
                            static_cast<uint32_t>(now), static_cast<uint32_t>(now >> 32) };
        return std::mt19937_64(seed);
    }();

    uint64_t value = 0;
    while (value == 0) {
        value = engine();
    }
    return Guid{ value };
}

std::string Guid::toString() const
{
    static constexpr char digits[] = "0123456789abcdef";

    std::string text(16, '0');
    uint64_t    rest = value;
    for (int i = 15; i >= 0; --i) {
        text[static_cast<size_t>(i)] = digits[rest & 0xF];
        rest >>= 4;
    }
    return text;
}

std::optional<Guid> Guid::parse(std::string_view text)
{
    if (text.size() != 16) {
        return std::nullopt;
    }
    // from_chars would also take uppercase. Only accept what toString()
    // writes, so a file never has two spellings of the same ID.
    for (const char c : text) {
        const bool isDigit = c >= '0' && c <= '9';
        const bool isLower = c >= 'a' && c <= 'f';
        if (!isDigit && !isLower) {
            return std::nullopt;
        }
    }

    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return Guid{ value };
}
