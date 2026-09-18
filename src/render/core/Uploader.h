#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <array>
#include <vector>
#include <cstdint>
#include "../../common/gpu_types.h"

// Load-time and streaming uploads that do not stall the queue.
//
// Every submit gets a ticket (a value on a private timeline semaphore).
// Staging buffers and anything else handed to trackBuffer/trackImage
// are held by the slot that recorded them and destroyed only once the GPU has
// passed that ticket. Nothing is freed while a copy is still in flight, and
// the CPU never blocks waiting for one.
//
// SlotCount submissions can be in flight at once. begin() blocks only when
// every slot is still busy, which at load time means we are uploading
// faster than the GPU can copy (which we want to wait on)
//
// ORDERING: everything goes to the graphics queue. Submissions on one queue
// start in submission order, and the barriers recorded inside the upload
// command buffer apply to every command that comes after them on that queue.
// So a mesh or texture uploaded here is safe to draw in any frame submitted
// afterwards, with no extra semaphore. If uploads ever move to a dedicated
// transfer queue, that stops being true: you then need a queue-family
// ownership transfer (release barrier on transfer, acquire barrier on
// graphics) plus a semaphore wait in the frame submit.

class Uploader
{
public:
    using Ticket = uint64_t;

    // Three is enough to keep the copy engine fed without holding much
    // staging memory. Raise it if load hitches show begin() blocking.
    static constexpr uint32_t SlotCount = 3;

    Uploader() = default;
    Uploader(const Uploader &) = delete;
    Uploader &operator=(const Uploader &) = delete;

    bool initialize(VkDevice device, VmaAllocator allocator,
                    VkQueue queue, uint32_t queueFamily);
    void shutdown();

    // Starts recording on a free slot. Returns nullptr on failure.
    // One recording at a time: submit() before the next begin().
    VkCommandBuffer begin();

    // Hands ownership of a resource to the slot currently recording. It is
    // destroyed when that slot's ticket completes. cmd is only there to catch
    // the mistake of tracking against a slot that is not the open one.
    void trackBuffer(VkCommandBuffer cmd, const GPUBuffer &buffer);
    void trackImage(VkCommandBuffer cmd, const GPUImage &image);

    // Ends and submits the open slot. Returns its ticket, or 0 on failure.
    Ticket submit();

    bool isComplete(Ticket ticket) const;
    void wait(Ticket ticket) const;

    // Recycles finished slots and frees their staging memory. Cheap; call it
    // once per frame. Without it, staging buffers linger until the next
    // begin() reuses their slot.
    void poll();

    // Drains every outstanding upload. For shutdown, not for the hot path.
    void waitIdle();

    Ticket lastSubmitted() const { return m_nextTicket - 1; }

private:
    struct Slot
    {
        VkCommandPool          pool   = nullptr;
        VkCommandBuffer        cmd    = nullptr;
        Ticket                 ticket = 0;      // 0 == idle, never submitted
        std::vector<GPUBuffer> buffers;
        std::vector<GPUImage>  images;
    };

    uint64_t completedValue() const;
    void     recycle(Slot &slot);
    Slot    *openSlot();

    VkDevice     m_device    = nullptr;
    VmaAllocator m_allocator = nullptr;
    VkQueue      m_queue     = nullptr;
    VkSemaphore  m_timeline  = nullptr;

    Ticket   m_nextTicket = 1;                  // ticket 0 means "never submitted"
    uint32_t m_recording  = UINT32_MAX;         // index of the open slot

    std::array<Slot, SlotCount> m_slots{};
};