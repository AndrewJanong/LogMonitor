#include "LogMonitor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <queue>
#include <memory>

#ifdef __linux__
  #include <pthread.h>
  #include <sched.h>
#endif

struct LogMonitor::AhoCorasick {
    struct Node {
        int next[256];
        int fail;
        bool output;
        Node() : fail(0), output(false) {
            for (int i = 0; i < 256; ++i) {
                next[i] = -1;
            }
        }
    };

    std::vector<Node> nodes;

    explicit AhoCorasick(const std::vector<std::string>& patterns) {
        build(patterns);
    }

    void build(const std::vector<std::string>& patterns) {
        nodes.clear();
        nodes.emplace_back(); // root

        // build trie
        for (const auto& p : patterns) {
            int v = 0;
            for (char ch : p) {
                unsigned char c = static_cast<unsigned char>(ch);
                int& nxt = nodes[v].next[c];
                if (nxt == -1) {
                    nxt = nodes.size();
                    nodes.emplace_back();
                }
                v = nxt;
            }
            nodes[v].output = true;
        }

        // build failure links and complete transitions
        std::queue<int> q;
        for (int c = 0; c < 256; ++c) {
            int nxt = nodes[0].next[c];
            if (nxt != -1) {
                nodes[nxt].fail = 0;
                q.push(nxt);
            } else {
                nodes[0].next[c] = 0;
            }
        }

        while (!q.empty()) {
            int v = q.front();
            q.pop();
            for (int c = 0; c < 256; ++c) {
                int nxt = nodes[v].next[c];
                if (nxt != -1) {
                    nodes[nxt].fail = nodes[nodes[v].fail].next[c];
                    nodes[nxt].output = nodes[nxt].output || nodes[nodes[nxt].fail].output;
                    q.push(nxt);
                } else {
                    nodes[v].next[c] = nodes[nodes[v].fail].next[c];
                }
            }
        }
    }

    bool matches(const std::string& text) const {
        int state = 0;
        for (char ch : text) {
            unsigned char c = static_cast<unsigned char>(ch);
            state = nodes[state].next[c];
            if (nodes[state].output) {
                return true; // found at least one pattern
            }
        }
        return false;
    }
};

LogMonitor::LogMonitor(const Config& config) 
    : config_(config) {

    // choose to use Aho-Corasick when number of keywords exceed 4
    if (config_.keywords.size() >= 4) {
        use_aho_ = true;
        aho_ = std::make_unique<AhoCorasick>(config_.keywords);
    }

    buffer_pool_.reserve(config_.pool_initial_capacity + 1);

    for (int i = 0; i < config_.pool_initial_capacity; ++i) {
        auto buf = std::make_unique<std::string>();
        buf->reserve(config_.max_line_length);
        free_buffers_.push_back(buf.get());
        buffer_pool_.push_back(std::move(buf));
    }

    current_line_ = acquireBuffer();
}

LogMonitor::~LogMonitor() {
    // ensure the consumer is stopped before closing files
    stop();
    if (consumer_thread_.joinable()) consumer_thread_.join();

    if (input_fd_ >= 0) {
        ::close(input_fd_);
        input_fd_ = -1;
    }

    if (output_stream_.is_open()) { 
        output_stream_.flush();
        output_stream_.close();
    }
}

void LogMonitor::pinThread(int cpu) {
#ifdef __linux__
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<unsigned>(cpu), &set);

    // Ignore errors to keep behavior identical if pin fails.
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void) cpu;
#endif
}

bool LogMonitor::openFiles() {
    input_fd_ = ::open(config_.input_file.c_str(), O_RDONLY);
    if (input_fd_ < 0) {
        std::cerr << "Error: Cannot open input file\n";
        return false;
    }

    // only track updates to the log file, seek to end
    if (::lseek(input_fd_, 0, SEEK_END) == (off_t)-1) {
        std::cerr << "Error: lseek on input file failed\n";
        ::close(input_fd_);
        input_fd_ = -1;
        return false;
    }

    output_stream_.open(config_.output_file, std::ios::out | std::ios::app);
    if (!output_stream_.is_open()) {
        std::cerr << "Error: Cannot open output file: " << config_.output_file << std::endl;
        ::close(input_fd_);
        input_fd_ = -1;
        return false;
    }

    return true;
}

bool LogMonitor::containsKeyword(const std::string& line) const {
    if (config_.keywords.empty()) {
        return true;  // no filter, accept all lines
    }

    if (use_aho_ && aho_) {
        return aho_->matches(line);
    }

    for (const auto& keyword : config_.keywords) {
        if (line.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static inline long long now_epoch_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

std::string* LogMonitor::acquireBuffer() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (!free_buffers_.empty()) {
        std::string* buf = free_buffers_.back();
        free_buffers_.pop_back();
        buf->clear();
        return buf;
    }

    // allocate new buffer if pool is exhausted
    auto buf = std::make_unique<std::string>();
    buf->reserve(config_.max_line_length);
    std::string* raw = buf.get();
    buffer_pool_.push_back(std::move(buf));
    return raw;
}

void LogMonitor::releaseBuffer(std::string* buf) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    buf->clear();
    free_buffers_.push_back(buf);
}

void LogMonitor::emitLine() {
    std::string* ready_line = current_line_;

    current_line_ = acquireBuffer();

    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [&]{
        return !running_ || line_queue_.size() < config_.queue_capacity;
    });
    if (!running_) {
        releaseBuffer(ready_line);
        return;
    }

    line_queue_.push_back(ready_line);

    lock.unlock();
    queue_cv_.notify_one();
}

void LogMonitor::processBuffer(const char* buffer, size_t bytes_read) {
    const char* read_cursor = buffer;
    const char* buffer_end = buffer + bytes_read;

    while (read_cursor < buffer_end) {
        // find next newline
        const char* newline = static_cast<const char*>(
            std::memchr(read_cursor, '\n', static_cast<size_t>(buffer_end - read_cursor))
        );

        // if limit is reached and current batch doesn't contain next line, skip 
        if (skip_line_ && !newline) {
            read_cursor = buffer_end;
            break;
        } else if (skip_line_ && newline) {
            read_cursor = newline + 1;
            skip_line_ = false;
            continue;
        }

        const char* segment_end = newline ? newline : buffer_end;

        for (const char* q = read_cursor; q < segment_end; ++q) {
            char ch = *q;
            if (ch == '\r') continue;

            if (current_line_->size() < config_.max_line_length) {
                current_line_->push_back(ch);

                // if reaches limit, enqueue first to be filtered (same behavior as before)
                if (current_line_->size() == config_.max_line_length) {
                    emitLine();
                    skip_line_ = true;
                    break;
                }
            }
        }

        if (newline) {
            if (current_line_->size() < config_.max_line_length) {
                emitLine();
            }
            read_cursor = newline + 1; // continue after '\n'
            skip_line_ = false;
        } else {
            read_cursor = buffer_end;
        }
    }
}

void LogMonitor::waitForData() {
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
}


void LogMonitor::consumerLoop() {
    while (true) {
        std::string* line_buf = nullptr;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [&]{
                return !running_ || !line_queue_.empty();
            });

            if (!running_ && line_queue_.empty()) break;

            line_buf = line_queue_.front();
            line_queue_.pop_front();

            lock.unlock();
            queue_cv_.notify_one(); // wake producer if it was waiting on capacity
        }

        const std::string& line = *line_buf;

        if (containsKeyword(line)) {
            if (config_.bench_stamp) {
                output_stream_ << line << "\t#MON_TS=" << std::to_string(now_epoch_ns()) << "\n";
            } else {
                output_stream_ << line << "\n";
            }
        }

        // recycle the buffer after done, instead of deallocation
        releaseBuffer(line_buf);
    }

    output_stream_.flush();
}

void LogMonitor::run() {
    if (!openFiles()) {
        return;
    }

    // pin reader thread if configured
    if (config_.reader_cpu >= 0) pinThread(config_.reader_cpu);

    running_ = true;

    // create and pin consumer thread if configured
    consumer_thread_ = std::thread([this] {
        if (config_.consumer_cpu >= 0) pinThread(config_.consumer_cpu);
        consumerLoop();
    });

    std::vector<char> buffer(config_.buffer_size);

    while (running_) {
        ssize_t n = ::read(input_fd_, buffer.data(), buffer.size());
        if (n > 0) {
            processBuffer(buffer.data(), n);
        } else if (n == 0) {
            waitForData();
        } else {
            if (errno == EINTR) {
                continue; // interrupted by signal, retry
            }
            std::cerr << "read() error\n";
            waitForData();
        }
    }

    // signal consumer to finish and drain queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
    }
    queue_cv_.notify_all();

    if (consumer_thread_.joinable()) consumer_thread_.join();
}

void LogMonitor::stop() {
    running_ = false;
    queue_cv_.notify_all(); // wake producer/consumer
}
