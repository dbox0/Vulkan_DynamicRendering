#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Persistent object identity.
//
// SLOT IDs ARE NOT IDENTITY. NodeWorld hands out 1-based slot IDs and recycles
// them through a free list, so "node 7" before a delete and "node 7" after the
// next create are different objects. Anything that outlives the current frame
// -- undo records, scene files, cross-node references, prefab links, the
// editor selection -- names an object by Guid and resolves it to a slot at the
// moment it is used.
//
// 64 random bits. The chance of any collision among a million objects is about
// 3e-8. This is scene-object identity; an asset database may want 128 bits
// later, and that would be a separate type on purpose.
//
// 0 is the null Guid. generate() never returns it.
struct Guid
{
    uint64_t value = 0;

    [[nodiscard]] static Guid generate();

    // Exactly 16 lowercase hex digits, zero padded. Fixed width so scene files
    // diff cleanly and so parse() can reject anything that is not that shape.
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] static std::optional<Guid> parse(std::string_view text);

    [[nodiscard]] constexpr bool isNull() const { return value == 0; }
    constexpr explicit operator bool() const { return value != 0; }

    friend constexpr bool operator==(Guid, Guid) = default;
    friend constexpr auto operator<=>(Guid, Guid) = default;
};

// A reference from one scene object to another, e.g. an enemy's patrol target.
//
// A distinct type rather than a bare Guid field so reflection can tell the two
// apart: duplicating a subtree or instantiating a prefab has to remap
// references that point INSIDE the copied set, and it can only find them if
// they are typed as references. A Guid field is identity; a NodeRef is a link.
struct NodeRef
{
    Guid guid;

    [[nodiscard]] constexpr bool isNull() const { return guid.isNull(); }
    friend constexpr bool operator==(NodeRef, NodeRef) = default;
};

template <>
struct std::hash<Guid>
{
    size_t operator()(Guid guid) const noexcept { return std::hash<uint64_t>{}(guid.value); }
};
