#ifndef LOG_MONITOR_H
#define LOG_MONITOR_H

#include <string>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

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


class LogMonitor {
public:
    struct Config {
        std::string input_file;
        std::string output_file;
        size_t buffer_size = 1024 * 1024; // 1MiB
        size_t max_line_length = 5000; // 5000 characters per line
        int poll_interval_ms = 1; // poll every 1ms to check new data
        std::vector<std::string> keywords;
        bool bench_stamp = false;
        size_t queue_capacity = 4096; // bound for line queue
        size_t pool_initial_capacity = 4096; // default size of buffer pool

        int reader_cpu   = -1; // default -1 = no pinning
        int consumer_cpu = -1; // default -1 = no pinning
    };

    explicit LogMonitor(const Config& config);
    ~LogMonitor();

    // starts monitoring
    void run();

    // stop monitoring
    void stop();
private:
    struct AhoCorasick;

    Config config_;
    std::atomic<bool> running_{false};

    int input_fd_{-1};
    std::ofstream output_stream_;

    std::string* current_line_;

    std::thread consumer_thread_;
    SpscQueue<std::string*> line_queue_;

    std::vector<std::unique_ptr<std::string>> buffer_pool_;
    std::vector<std::string*> free_buffers_;
    std::mutex pool_mutex_;

    bool skip_line_ = false;

    bool use_aho_ = false;
    std::unique_ptr<AhoCorasick> aho_;

    bool openFiles();
    void processBuffer(const char* buffer, size_t bytes_read);
    void emitLine();
    bool containsKeyword(const std::string& line) const;
    void waitForData();
    void consumerLoop();

    static void pinThread(int cpu);

    std::string* acquireBuffer();
    void releaseBuffer(std::string* buf); 
};

#endif // LOG_MONITOR_H
