#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "UndoHistory.h"

class Scene;
class SceneUndoWorld : public UndoWorld
{
public:
    explicit SceneUndoWorld(Scene &scene) : m_scene(scene) {}

    [[nodiscard]] bool capture(const EditTarget &target, reflect::Blob &out) const final;
    [[nodiscard]] bool apply(const EditTarget &target, std::span<const uint8_t> snapshot) final;

    [[nodiscard]] bool placement(Guid node, NodePlacement &out) const final;
    [[nodiscard]] bool moveNode(Guid node, const NodePlacement &to) final;
    [[nodiscard]] bool captureSubtree(Guid root, SubtreeSnapshot &out) const final;
    [[nodiscard]] bool restoreSubtree(const SubtreeSnapshot &snapshot) final;
    [[nodiscard]] bool destroySubtree(Guid root) final;

    [[nodiscard]] bool exists(const EditTarget &target) const;

protected:
    [[nodiscard]] virtual bool existsOther(const EditTarget &) const { return false; }
    [[nodiscard]] virtual bool captureOther(const EditTarget &, reflect::Blob &) const { return false; }
    [[nodiscard]] virtual bool applyOther(const EditTarget &, std::span<const uint8_t>) { return false; }

    // Meshes no live node references any more after a destroy. Not freed
    // here: the history may bring their nodes back.
    virtual void onMeshesOrphaned(const std::vector<uint32_t> &) {}

    Scene &m_scene;

private:
    std::vector<uint32_t> m_orphans;   // scratch
};
