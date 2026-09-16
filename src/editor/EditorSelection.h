#pragma once
#include <cstdint>

#include "../common/Guid.h"

// What the editor has selected, as plain data. Every undo step remembers the
// selection before and after it, so stepping back through history also
// steps the selection back. Undoing a delete re-selects the node that came
// back. A selected node that no longer exists just fails to resolve.
struct EditorSelection
{
    enum class Mode : uint8_t
    {
        None,
        Node,
        Material,
        Texture
    };

    Mode     mode     = Mode::None;
    Guid     node;
    uint32_t subMesh  = 0;
    uint32_t material = 0;
    uint32_t texture  = 0;

    friend bool operator==(const EditorSelection &, const EditorSelection &) = default;
};
