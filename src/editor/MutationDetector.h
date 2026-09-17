#pragma once
#include <cstdint>
#include <string>

// ============================================================================
// Untracked-mutation detector. DEBUG BUILDS ONLY.
//
// Hash the entire scene and material state at the start of a frame and again
// at the end. If the hash changed while no undo transaction was open, a
// direct write bypassed the history: assert immediately.
//
// This is the check that keeps undo correct forever. Adding a new component
// type or store is the one case where you need to update hashState() -- but
// that only happens when you add a new EditTarget kind, and the compiler will
// remind you because the unhandled case is a warning.
//
// DISABLED in release: hashing the whole scene every frame is measurable.
// The check is enabled in debug and in the CI test runner. That is enough:
// the bugs it catches are design flaws, not race conditions, so they
// reproduce reliably in debug.
// ============================================================================

class Scene;
class ResourceStore;
class GeometryStore;
class UndoHistory;

class MutationDetector
{
public:
    MutationDetector() = default;

    void initialize(const Scene &scene, const ResourceStore &resources,
                    const GeometryStore &geometry, const UndoHistory &history);

    // Call at the START of applyEditorCommands, after panels have run.
    // Stores the current hash if nothing is open; checks it if nothing was
    // open last frame and nothing is open now.
    void beginFrame();

    // Call at the END of applyEditorCommands.
    // Compares the hash and asserts if it changed outside any transaction.
    void endFrame();

    [[nodiscard]] bool isEnabled() const;

private:
    [[nodiscard]] uint64_t hashState() const;

    const Scene          *m_scene     = nullptr;
    const ResourceStore  *m_resources = nullptr;
    const GeometryStore  *m_geometry  = nullptr;
    const UndoHistory    *m_history   = nullptr;

    uint64_t m_hash       = 0;
    bool     m_hadState   = false;  // whether m_hash was captured
};
