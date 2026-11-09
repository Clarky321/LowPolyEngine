#pragma once

// Slot storage behind the renderer's handles. Internal to the renderer; free
// of GPU types so it can be unit-tested.
//
// A handle is (slot index, generation). Removing an element bumps its slot's
// generation, so old handles to a reused slot are detected instead of silently
// pointing at the new element.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace engine::render::detail {

struct PoolHandle {
    std::uint32_t index = 0;
    // 0 is never a live generation: a default handle is always invalid.
    std::uint32_t generation = 0;
};

template <typename T>
class HandlePool {
public:
    PoolHandle insert(T value)
    {
        std::uint32_t index = 0;
        if (!freeSlots_.empty()) {
            index = freeSlots_.back();
            freeSlots_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.value.emplace(std::move(value));
        ++liveCount_;
        return PoolHandle{index, slot.generation};
    }

    [[nodiscard]] T* find(PoolHandle handle) noexcept
    {
        Slot* slot = liveSlot(handle);
        return slot != nullptr ? &*slot->value : nullptr;
    }

    [[nodiscard]] const T* find(PoolHandle handle) const noexcept
    {
        return const_cast<HandlePool*>(this)->find(handle);
    }

    // Destroys the element. Returns false for an invalid or stale handle.
    bool erase(PoolHandle handle) noexcept
    {
        Slot* slot = liveSlot(handle);
        if (slot == nullptr) {
            return false;
        }
        slot->value.reset();
        --liveCount_;
        // A slot whose generation would wrap around to 0 is retired for good.
        if (slot->generation == std::numeric_limits<std::uint32_t>::max()) {
            slot->generation = 0;
        } else {
            ++slot->generation;
            freeSlots_.push_back(handle.index);
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return liveCount_; }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 1;
    };

    Slot* liveSlot(PoolHandle handle) noexcept
    {
        if (handle.generation == 0 || handle.index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[handle.index];
        return (slot.generation == handle.generation && slot.value) ? &slot : nullptr;
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
    std::size_t liveCount_ = 0;
};

} // namespace engine::render::detail
