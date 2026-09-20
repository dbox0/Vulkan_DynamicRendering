// src/render/core/GpuProfiler.h
#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <vector>

class VulkanContext;
class GpuProfiler
{
public:
    static constexpr uint32_t MaxScopes = 32;
    static constexpr uint32_t MaxFrames = 4;

    struct Result
    {
        const char *name  = nullptr;
        uint32_t    depth = 0;       // nesting level, for indenting the UI
        float       ms    = 0.0f;
        float       avgMs = 0.0f;
        float       maxMs = 0.0f;    // decaying peak, for spotting spikes
    };

    GpuProfiler() = default;
    GpuProfiler(const GpuProfiler &) = delete;
    GpuProfiler &operator=(const GpuProfiler &) = delete;

    // False when the queue reports no usable timestamp bits. Every other
    // method is a no-op after that, so callers need no guards.
    bool initialize(VulkanContext &ctx, uint32_t framesInFlight);
    void destroy();

    // Harvests this slice's previous results, then resets it. Call once,
    // right after vkBeginCommandBuffer.
    void beginFrame(VkCommandBuffer cmd);

    // name must outlive the profiler -- string literals only. Identity is the
    // pointer, so don't build names at runtime.
    void beginScope(VkCommandBuffer cmd, const char *name);
    void endScope(VkCommandBuffer cmd);

    // Results from the frame that used this slice last, in the order its
    // scopes opened. Valid until the next beginFrame.
    const std::vector<Result> &results() const { return m_results; }

    bool enabled() const { return m_pool != nullptr; }

private:
    struct Scope
    {
        const char *name  = nullptr;
        uint32_t    depth = 0;
    };

    struct Slice
    {
        std::array<Scope, MaxScopes> scopes{};
        uint32_t count    = 0;
        bool     recorded = false;
    };

    struct History
    {
        const char *name      = nullptr;
        float       avgMs     = 0.0f;
        float       maxMs     = 0.0f;
        uint64_t    lastFrame = 0;
    };

    void    collect(uint32_t frameIndex);
    History &history(const char *name);
    VkDevice    m_device = nullptr;
    VkQueryPool m_pool   = nullptr;

    float    m_period    = 1.0f;
    uint64_t m_validMask = ~0ull;
    uint32_t m_frames    = 0;

    bool m_labels = false;

    uint32_t m_frameIndex   = 0;
    uint64_t m_frameCounter = 0;

    std::array<Slice, MaxFrames> m_slices{};
    std::vector<uint32_t>        m_stack;
    std::vector<Result>          m_results;
    std::vector<History>         m_history;
};