#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <vector>
#include <atomic>
#include <cstddef>

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity)
        : capacity_(capacity + 1), buffer_(capacity_), head_(0), tail_(0) {}

    bool push(const T& value) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            // full
            return false;
        }
        buffer_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) {
        auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            // empty
            return false;
        }
        value = buffer_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

private:
    std::size_t increment(std::size_t i) const noexcept {
        return (i + 1) % capacity_;
    }

    const std::size_t capacity_;
    std::vector<T> buffer_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
};

#endif // SPSC_QUEUE_H
