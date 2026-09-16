#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "Reflection.h"

// ============================================================================
// In-memory snapshots. The format undo records are made of.
//
// NOT A FILE FORMAT. Native endianness, positional fields, no migration: a
// blob is only ever read back by the same build that wrote it, in the same
// process. Anything that reaches disk goes through JsonArchive.
//
// GUARANTEES
//   * Deterministic. Same field values -> same bytes, always. Values are
//     written one component at a time, never as whole structs, so padding
//     (including glm's SIMD alignment) never reaches the blob. Two blobs can
//     be compared with == to ask "did anything change", which is how an undo
//     transaction decides it was a no-op.
//   * Exact. Floats are stored bit for bit: -0.0 and NaN payloads survive, so
//     undo restores precisely what was there.
//   * Validated before applied. readBinary() walks the whole blob first and
//     only writes into the object once it has proven the blob matches the
//     type. A bad blob fails without touching the object.
//
// Each struct is prefixed with a hash of its type name and its field count,
// so feeding a Light blob into a Node is caught rather than misread.
// ============================================================================

namespace reflect
{
    using Blob = std::vector<uint8_t>;

    // Appends to `out` -- several objects can share one blob.
    void writeBinary(const TypeInfo &type, const void *object, Blob &out);

    // Reads one object from the start of `data`. On success `consumed` is the
    // number of bytes it occupied. On failure the object is untouched.
    [[nodiscard]] bool readBinaryPrefix(const TypeInfo &type, void *object,
                                        std::span<const uint8_t> data, size_t &consumed);

    // Like readBinaryPrefix, but `data` must be exactly one object.
    [[nodiscard]] bool readBinary(const TypeInfo &type, void *object, std::span<const uint8_t> data);

    template <class T>
    void writeBinary(const T &object, Blob &out)
    {
        writeBinary(typeOf<T>(), &object, out);
    }

    template <class T>
    [[nodiscard]] Blob toBlob(const T &object)
    {
        Blob blob;
        writeBinary(typeOf<T>(), &object, blob);
        return blob;
    }

    template <class T>
    [[nodiscard]] bool readBinary(T &object, std::span<const uint8_t> data)
    {
        return readBinary(typeOf<T>(), &object, data);
    }
}
