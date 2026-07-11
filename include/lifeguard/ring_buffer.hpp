#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace lifeguard {

// Lock-free single-producer / single-consumer ring buffer.
//
// The capture thread is the sole producer; the inference thread is the sole
// consumer. When the buffer is full the producer overwrites the oldest slot
// (drop-oldest) so that a slow consumer never stalls capture — for a live
// safety monitor, the freshest frame matters more than a complete history.
template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
    // Push an item. Returns false if it overwrote an un-consumed slot.
    bool push(T value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);

        bool overwrote = false;
        if (next == tail_.load(std::memory_order_acquire)) {
            // Full: advance tail to drop the oldest element.
            tail_.store(increment(tail_.load(std::memory_order_relaxed)),
                        std::memory_order_release);
            overwrote = true;
        }

        slots_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
        return !overwrote;
    }

    // Pop the oldest item, or std::nullopt if empty.
    std::optional<T> pop() {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        T value = std::move(slots_[tail]);
        tail_.store(increment(tail), std::memory_order_release);
        return value;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kSlots = Capacity + 1;  // one empty sentinel
    static std::size_t increment(std::size_t i) { return (i + 1) % kSlots; }

    std::array<T, kSlots> slots_{};
    std::atomic<std::size_t> head_{0};  // written by producer
    std::atomic<std::size_t> tail_{0};  // written by consumer
};

}  // namespace lifeguard
