#include "Uploader.h"

#include <volk.h>
#include <algorithm>
#include <cassert>

#include "../../common/errors.h"

bool Uploader::initialize(VkDevice device, VmaAllocator allocator,
                          VkQueue queue, uint32_t queueFamily)
{
    m_device    = device;
    m_allocator = allocator;
    m_queue     = queue;

    const VkSemaphoreTypeCreateInfo typeInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0
    };
    const VkSemaphoreCreateInfo semInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &typeInfo
    };
    if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_timeline) != VK_SUCCESS) {
        showError("Uploader: could not create the upload timeline semaphore");
        return false;
    }

    // A pool per slot, so resetting one slot never touches a command buffer
    // that is still executing in another.
    for (Slot &slot : m_slots) {
        const VkCommandPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = queueFamily
        };
        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &slot.pool) != VK_SUCCESS) {
            showError("Uploader: could not create an upload command pool");
            return false;
        }

        const VkCommandBufferAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = slot.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        if (vkAllocateCommandBuffers(m_device, &allocInfo, &slot.cmd) != VK_SUCCESS) {
            showError("Uploader: could not allocate an upload command buffer");
            return false;
        }
    }
    return true;
}

void Uploader::shutdown()
{
    if (!m_device) {
        return;
    }

    waitIdle();

    for (Slot &slot : m_slots) {
        if (slot.pool) {
            // Frees slot.cmd with it.
            vkDestroyCommandPool(m_device, slot.pool, nullptr);
            slot.pool = nullptr;
            slot.cmd  = nullptr;
        }
    }
    if (m_timeline) {
        vkDestroySemaphore(m_device, m_timeline, nullptr);
        m_timeline = nullptr;
    }
    m_device    = nullptr;
    m_allocator = nullptr;
    m_queue     = nullptr;
    m_recording = UINT32_MAX;
}

uint64_t Uploader::completedValue() const
{
    uint64_t value = 0;
    vkGetSemaphoreCounterValue(m_device, m_timeline, &value);
    return value;
}

void Uploader::recycle(Slot &slot)
{
    for (GPUBuffer &buffer : slot.buffers) {
        if (buffer.vkBuffer) {
            vmaDestroyBuffer(m_allocator, buffer.vkBuffer, buffer.allocation);
        }
    }
    slot.buffers.clear();

    for (GPUImage &image : slot.images) {
        if (image.imageView) {
            vkDestroyImageView(m_device, image.imageView, nullptr);
        }
        if (image.image) {
            vmaDestroyImage(m_allocator, image.image, image.allocation);
        }
    }
    slot.images.clear();

    slot.ticket = 0;
}

Uploader::Slot *Uploader::openSlot()
{
    return m_recording == UINT32_MAX ? nullptr : &m_slots[m_recording];
}

VkCommandBuffer Uploader::begin()
{
    if (!m_device) {
        showError("Uploader::begin before initialize");
        return nullptr;
    }
    if (m_recording != UINT32_MAX) {
        showError("Uploader::begin called twice without a submit");
        return nullptr;
    }

    poll();

    // Free slot, or the oldest busy one if every slot is in flight.
    uint32_t chosen = UINT32_MAX;
    for (uint32_t i = 0; i < SlotCount; ++i) {
        if (m_slots[i].ticket == 0) {
            chosen = i;
            break;
        }
    }
    if (chosen == UINT32_MAX) {
        chosen = 0;
        for (uint32_t i = 1; i < SlotCount; ++i) {
            if (m_slots[i].ticket < m_slots[chosen].ticket) {
                chosen = i;
            }
        }
        // The only blocking path in this class, and only when SlotCount
        // uploads are already queued ahead of this one.
        wait(m_slots[chosen].ticket);
        recycle(m_slots[chosen]);
    }

    Slot &slot = m_slots[chosen];
    if (vkResetCommandPool(m_device, slot.pool, 0) != VK_SUCCESS) {
        showError("Uploader: could not reset an upload command pool");
        return nullptr;
    }

    const VkCommandBufferBeginInfo beginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if (vkBeginCommandBuffer(slot.cmd, &beginInfo) != VK_SUCCESS) {
        showError("Uploader: could not begin an upload command buffer");
        return nullptr;
    }

    m_recording = chosen;
    return slot.cmd;
}

void Uploader::trackBuffer(VkCommandBuffer cmd, const GPUBuffer &buffer)
{
    Slot *slot = openSlot();
    if (!slot || slot->cmd != cmd) {
        showError("Uploader::trackBuffer for a command buffer that is not recording");
        return;
    }
    if (buffer.vkBuffer) {
        slot->buffers.push_back(buffer);
    }
}

void Uploader::trackImage(VkCommandBuffer cmd, const GPUImage &image)
{
    Slot *slot = openSlot();
    if (!slot || slot->cmd != cmd) {
        showError("Uploader::trackImage for a command buffer that is not recording");
        return;
    }
    if (image.image || image.imageView) {
        slot->images.push_back(image);
    }
}

Uploader::Ticket Uploader::submit()
{
    Slot *slot = openSlot();
    if (!slot) {
        showError("Uploader::submit without a matching begin");
        return 0;
    }
    m_recording = UINT32_MAX;

    if (vkEndCommandBuffer(slot->cmd) != VK_SUCCESS) {
        showError("Uploader: could not end an upload command buffer");
        recycle(*slot);
        return 0;
    }

    const Ticket ticket = m_nextTicket;

    const VkCommandBufferSubmitInfo cmdInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = slot->cmd
    };
    const VkSemaphoreSubmitInfo signalInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_timeline,
        .value = ticket,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
    };
    const VkSubmitInfo2 submitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo
    };

    if (vkQueueSubmit2(m_queue, 1, &submitInfo, nullptr) != VK_SUCCESS) {
        showError("Uploader: upload submit failed");
        recycle(*slot);
        return 0;
    }

    slot->ticket = ticket;
    ++m_nextTicket;
    return ticket;
}

bool Uploader::isComplete(Ticket ticket) const
{
    return ticket == 0 || completedValue() >= ticket;
}

void Uploader::wait(Ticket ticket) const
{
    if (ticket == 0 || !m_timeline) {
        return;
    }
    const VkSemaphoreWaitInfo waitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_timeline,
        .pValues = &ticket
    };
    vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
}

void Uploader::poll()
{
    if (!m_device) {
        return;
    }
    const uint64_t completed = completedValue();
    for (uint32_t i = 0; i < SlotCount; ++i) {
        if (i == m_recording) {
            continue;
        }
        Slot &slot = m_slots[i];
        if (slot.ticket != 0 && slot.ticket <= completed) {
            recycle(slot);
        }
    }
}

void Uploader::waitIdle()
{
    if (!m_device) {
        return;
    }
    // An open recording was never submitted, so nothing will ever signal its
    // ticket -- drop it rather than wait forever.
    if (Slot *slot = openSlot()) {
        vkEndCommandBuffer(slot->cmd);
        recycle(*slot);
        m_recording = UINT32_MAX;
    }

    wait(lastSubmitted());
    for (Slot &slot : m_slots) {
        recycle(slot);
    }
}