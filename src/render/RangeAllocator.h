#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// First-fit free-list suballocator over a fixed capacity, counted in
// ELEMENTS, not bytes -- the caller multiplies by sizeof(Vertex) or
// sizeof(uint32_t) when it needs an offset into a VkBuffer.
//
// Free ranges are kept sorted by offset and coalesced on release, so a load /
// unload / reload cycle of the same model reuses the same space instead of
// fragmenting the buffer one model at a time.
//
// highWater() is the largest offset ever handed out. The CPU-side mirror only
// has to be that big, which is why nothing needs to touch the full budget at
// startup.

class RangeAllocator
{
public:
    static constexpr size_t kInvalid = std::numeric_limits<size_t>::max();

    void reset(size_t capacity)
    {
        m_capacity  = capacity;
        m_used      = 0;
        m_highWater = 0;
        m_free.clear();
        if (capacity) {
            m_free.push_back(Range{ 0, capacity });
        }
    }

    // Returns the start offset, or kInvalid when no single free range is big
    // enough. A caller that gets kInvalid with plenty of used() left is
    // fragmented, not full -- see largestFreeBlock().
    size_t allocate(size_t count)
    {
        if (count == 0) {
            return 0;
        }
        for (size_t i = 0; i < m_free.size(); ++i) {
            if (m_free[i].count < count) {
                continue;
            }
            const size_t offset = m_free[i].offset;
            if (m_free[i].count == count) {
                m_free.erase(m_free.begin() + static_cast<ptrdiff_t>(i));
            } else {
                m_free[i].offset += count;
                m_free[i].count  -= count;
            }
            m_used += count;
            m_highWater = std::max(m_highWater, offset + count);
            return offset;
        }
        return kInvalid;
    }

    void release(size_t offset, size_t count)
    {
        if (count == 0) {
            return;
        }
        m_used -= std::min(m_used, count);

        // Insert sorted by offset, then merge with either neighbour.
        size_t i = 0;
        while (i < m_free.size() && m_free[i].offset < offset) {
            ++i;
        }
        m_free.insert(m_free.begin() + static_cast<ptrdiff_t>(i), Range{ offset, count });

        if (i + 1 < m_free.size() &&
            m_free[i].offset + m_free[i].count == m_free[i + 1].offset)
        {
            m_free[i].count += m_free[i + 1].count;
            m_free.erase(m_free.begin() + static_cast<ptrdiff_t>(i) + 1);
        }
        if (i > 0 && m_free[i - 1].offset + m_free[i - 1].count == m_free[i].offset) {
            m_free[i - 1].count += m_free[i].count;
            m_free.erase(m_free.begin() + static_cast<ptrdiff_t>(i));
        }
    }

    size_t capacity()  const { return m_capacity; }
    size_t used()      const { return m_used; }
    size_t highWater() const { return m_highWater; }
    size_t freeBlockCount() const { return m_free.size(); }

    size_t largestFreeBlock() const
    {
        size_t largest = 0;
        for (const Range &range : m_free) {
            largest = std::max(largest, range.count);
        }
        return largest;
    }

private:
    struct Range { size_t offset = 0; size_t count = 0; };

    std::vector<Range> m_free;
    size_t m_capacity  = 0;
    size_t m_used      = 0;
    size_t m_highWater = 0;
};