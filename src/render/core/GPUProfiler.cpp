
#include "GPUProfiler.h"

#include <volk.h>
#include <algorithm>
#include <iostream>
#include <vector>

#include "VulkanContext.h"

namespace {
    constexpr float kSmoothing = 0.05f;
    constexpr float kPeakDecay = 0.99f;
}

bool GpuProfiler::initialize(VulkanContext &ctx, uint32_t framesInFlight)
{
    m_device = ctx.device();
    m_frames = std::min(framesInFlight, MaxFrames);

    if (framesInFlight > MaxFrames) {
        std::cerr << "[warn] GpuProfiler: " << framesInFlight
                  << " frames in flight exceeds MaxFrames" << std::endl;
        return false;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physical(), &props);
    m_period = props.limits.timestampPeriod;

    if (m_period == 0.0f) {
        std::cerr << "[warn] GpuProfiler: device reports no timestamp support" << std::endl;
        return false;
    }

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(ctx.physical(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties2> families(
        familyCount, { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
    vkGetPhysicalDeviceQueueFamilyProperties2(ctx.physical(), &familyCount, families.data());

    const uint32_t validBits = families[ctx.gfxFamily()].queueFamilyProperties.timestampValidBits;
    if (validBits == 0) {
        std::cerr << "[warn] GpuProfiler: graphics queue has no valid timestamp bits" << std::endl;
        return false;
    }
    // Shifting by 64 is undefined, so the full-width case needs its own branch.
    m_validMask = validBits >= 64 ? ~0ull : (1ull << validBits) - 1ull;

    const VkQueryPoolCreateInfo poolInfo
    {
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = m_frames * MaxScopes * 2
    };
    if (vkCreateQueryPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        std::cerr << "[warn] GpuProfiler: failed to create the query pool" << std::endl;
        m_pool = nullptr;
        return false;
    }

    m_labels = vkCmdBeginDebugUtilsLabelEXT != nullptr && vkCmdEndDebugUtilsLabelEXT != nullptr;

    m_stack.reserve(8);
    m_results.reserve(MaxScopes);
    m_history.reserve(MaxScopes);
    return true;
}

void GpuProfiler::destroy()
{
    if (m_pool) {
        vkDestroyQueryPool(m_device, m_pool, nullptr);
        m_pool = nullptr;
    }
    m_slices = {};
    m_stack.clear();
    m_results.clear();
    m_history.clear();
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd)
{
    if (!m_pool) {
        return;
    }

    m_frameIndex = static_cast<uint32_t>(m_frameCounter % m_frames);
    ++m_frameCounter;

    // Read before resetting: these queries hold the results of the frame that
    // used this slice, which the renderer's timeline wait has already retired.
    collect(m_frameIndex);

    // The whole slice, not just the part used last time. A shorter reset
    // leaves stale queries marked available and they read as live data.
    vkCmdResetQueryPool(cmd, m_pool, m_frameIndex * MaxScopes * 2, MaxScopes * 2);

    Slice &slice = m_slices[m_frameIndex];
    slice.count    = 0;
    slice.recorded = true;
    m_stack.clear();
}

void GpuProfiler::beginScope(VkCommandBuffer cmd, const char *name)
{
    if (!m_pool) {
        return;
    }

    if (m_labels) {
        const VkDebugUtilsLabelEXT label
        {
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = name,
            .color      = { 0.4f, 0.6f, 1.0f, 1.0f }
        };
        vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
    }

    Slice &slice = m_slices[m_frameIndex];
    if (slice.count >= MaxScopes) {
        m_stack.push_back(UINT32_MAX);   // still needs its endScope
        return;
    }

    const uint32_t index = slice.count++;
    slice.scopes[index] = Scope{ name, static_cast<uint32_t>(m_stack.size()) };
    m_stack.push_back(index);

    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_pool,
                         m_frameIndex * MaxScopes * 2 + index * 2);
}

void GpuProfiler::endScope(VkCommandBuffer cmd)
{
    if (!m_pool || m_stack.empty()) {
        return;
    }

    const uint32_t index = m_stack.back();
    m_stack.pop_back();

    if (index != UINT32_MAX) {
        // ALL_COMMANDS, not a single stage: the scope ends when everything
        // recorded inside it has finished, not when it was issued.
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_pool,
                             m_frameIndex * MaxScopes * 2 + index * 2 + 1);
    }

    if (m_labels) {
        vkCmdEndDebugUtilsLabelEXT(cmd);
    }
}

void GpuProfiler::collect(uint32_t frameIndex)
{
    m_results.clear();

    const Slice &slice = m_slices[frameIndex];
    if (!slice.recorded || slice.count == 0) {
        return;
    }

    const uint32_t queryCount = slice.count * 2;
    const uint32_t base       = frameIndex * MaxScopes * 2;

    // Two uint64s per query: the value, then the availability flag.
    std::array<uint64_t, MaxScopes * 4> raw{};
    vkGetQueryPoolResults(m_device, m_pool, base, queryCount,
                          queryCount * 2 * sizeof(uint64_t), raw.data(),
                          2 * sizeof(uint64_t),
                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    for (uint32_t i = 0; i < slice.count; ++i) {
        if (raw[i * 4 + 1] == 0 || raw[i * 4 + 3] == 0) {
            continue;
        }

        const uint64_t beginTick = raw[i * 4 + 0] & m_validMask;
        const uint64_t endTick   = raw[i * 4 + 2] & m_validMask;
        const uint64_t ticks     = endTick >= beginTick ? endTick - beginTick : 0;
        const float    ms        = static_cast<float>(ticks) * m_period * 1e-6f;

        History &hist = history(slice.scopes[i].name);

        // A scope that was skipped for a while restarts rather than averaging
        // across the gap.
        const bool contiguous = m_frameCounter - hist.lastFrame <= m_frames + 1;
        hist.avgMs    = contiguous ? hist.avgMs + (ms - hist.avgMs) * kSmoothing : ms;
        hist.maxMs    = contiguous ? std::max(ms, hist.maxMs * kPeakDecay) : ms;
        hist.lastFrame = m_frameCounter;

        m_results.push_back(Result{ slice.scopes[i].name, slice.scopes[i].depth,
                                    ms, hist.avgMs, hist.maxMs });
    }
}

GpuProfiler::History &GpuProfiler::history(const char *name)
{
    for (History &hist : m_history) {
        if (hist.name == name) {
            return hist;
        }
    }
    m_history.push_back(History{ name, 0.0f, 0.0f, 0 });
    return m_history.back();
}