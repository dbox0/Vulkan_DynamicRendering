#pragma once
#include <cstdint>

// Renderer state, not scene data: not undoable, not serialized.
// Same status as ShadowSettings.
struct CullSettings
{
    bool enabled = true;
    bool freeze  = false;

    // Only meaningful while frozen: rebuilds the latched view with new
    // projection params so you can watch the cull set change.
    bool  overrideProjection = false;
    float fovDegrees = 60.0f;
    float nearClip   = 0.1f;
    float farClip    = 100.0f;

    bool drawFrustum = true;
    bool drawVisible = false;
    bool drawCulled  = false;

    int boxBudget = 256;   // wireframe boxes before we stop emitting
};

struct CullStats
{
    uint32_t total     = 0;
    uint32_t submitted = 0;
    uint32_t boxesDrawn = 0;
    bool     clamped   = false;   // hit maxDraws
};