#pragma once
#include <cstddef>
#include <vector>

#include "../common/Guid.h"
#include "../reflect/BinaryArchive.h"

struct NodePlacement
{
    Guid parent;     // null = scene root
    Guid previous;   // the sibling this node follows; null = first child / first root

    friend bool operator==(const NodePlacement &, const NodePlacement &) = default;
};

struct SubtreeSnapshot
{
    struct Entry
    {
        Guid          guid;
        Guid          parent;   // the root's is placement.parent
        reflect::Blob data;     // reflect::writeBinary(Node)
    };

    NodePlacement      placement;   // of the root
    std::vector<Entry> nodes;       // depth-first pre-order, root first, siblings in order

    [[nodiscard]] Guid root() const { return nodes.empty() ? Guid{} : nodes.front().guid; }

    [[nodiscard]] size_t byteSize() const
    {
        size_t bytes = sizeof(*this);
        for (const Entry &entry : nodes) {
            bytes += sizeof(Entry) + entry.data.size();
        }
        return bytes;
    }
};
