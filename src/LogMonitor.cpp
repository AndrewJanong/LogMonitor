#include "LogMonitor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

LogMonitor::LogMonitor(const Config& config) 
    : config_(config) {
    current_line_.reserve(config.max_line_length);
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

void LogMonitor::emitLine(std::string& line) {
    std::string out;
    out.swap(line);
    processLine(std::move(out));
    line.reserve(config_.max_line_length);
}

void LogMonitor::processLine(std::string&& line) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [&]{
        return !running_ || line_queue_.size() < config_.queue_capacity;
    });

    if (!running_) return;
    line_queue_.push_back(std::move(line));

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
        }

        const char* segment_end = newline ? newline : buffer_end;

        for (const char* q = read_cursor; q < segment_end; ++q) {
            char ch = *q;
            if (ch == '\r') continue;

            if (current_line_.size() < config_.max_line_length) {
                current_line_ += ch;

                // if reaches limit, enqueue first to be filtered (same behavior as before)
                if (current_line_.size() == config_.max_line_length) {
                    emitLine(current_line_);
                    skip_line_ = true;
                    break;
                }
            }
        }

        if (newline) {
            if (current_line_.size() < config_.max_line_length) {
                emitLine(current_line_);
            }
            current_line_.clear();
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
        std::string line;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [&]{
                return !running_ || !line_queue_.empty();
            });

            if (!running_ && line_queue_.empty()) break;

            line = std::move(line_queue_.front());
            line_queue_.pop_front();

            lock.unlock();
            queue_cv_.notify_one(); // wake producer if it was waiting on capacity
        }

        if (containsKeyword(line)) {
            if (config_.bench_stamp) {
                output_stream_ << line << "\t#MON_TS=" << std::to_string(now_epoch_ns()) << "\n";
            } else {
                output_stream_ << line << "\n";
            }
        }
    }

    output_stream_.flush();
}

void LogMonitor::run() {
    if (!openFiles()) {
        return;
    }

    running_ = true;

    consumer_thread_ = std::thread(&LogMonitor::consumerLoop, this);

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
