#include "LogMonitor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>

LogMonitor::LogMonitor(const Config& config) 
    : config_(config) {
    current_line_.reserve(config.max_line_length);
}

LogMonitor::~LogMonitor() {
    // ensure the consumer is stopped before closing files
    stop();
    if (consumer_thread_.joinable()) consumer_thread_.join();
    if (input_stream_.is_open()) input_stream_.close();
    if (output_stream_.is_open()) { 
        output_stream_.flush();
        output_stream_.close();
    }
}

bool LogMonitor::openFiles() {
    // open input file
    input_stream_.open(config_.input_file, std::ios::in | std::ios::binary);
    if (!input_stream_.is_open()) {
        std::cerr << "Error: Cannot open input file: " << config_.input_file << std::endl;
        return false;
    }

    // only track updates to the log file
    input_stream_.seekg(0, std::ios::end);

    // open output file
    output_stream_.open(config_.output_file, std::ios::out | std::ios::app);
    if (!output_stream_.is_open()) {
        std::cerr << "Error: Cannot open output file: " << config_.output_file << std::endl;
        input_stream_.close();
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

void LogMonitor::processLine(const std::string& line) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [&]{
        return !running_ || line_queue_.size() < config_.queue_capacity;
    });

    if (!running_) return;
    line_queue_.push_back(line);

    lock.unlock();
    queue_cv_.notify_one();
}

void LogMonitor::processBuffer(const char* buffer, size_t bytes_read) {
    for (size_t i = 0; i < bytes_read; i++) {
        char ch = buffer[i];

        if (ch == '\r') {
            continue;
        } else if (ch == '\n') {
            if (current_line_.size() < config_.max_line_length) processLine(current_line_);
            current_line_.clear();
        } else {
            if (current_line_.size() < config_.max_line_length) {
                current_line_ += ch;

                // if reaches limit, enqueue first to be filtered
                if (current_line_.size() == config_.max_line_length) {
                    processLine(current_line_);
                }
            }
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
    std::streampos last_pos = input_stream_.tellg();

    while (running_) {
        // read input stream to buffer
        input_stream_.read(buffer.data(), buffer.size());
        std::streamsize bytes_read = input_stream_.gcount();

        if (bytes_read > 0) {
            processBuffer(buffer.data(), bytes_read);
            last_pos = input_stream_.tellg();
        }

        if (input_stream_.eof()) {
            input_stream_.clear(); // clear eof flag to continue monitoring
            input_stream_.seekg(0, std::ios::cur);
        }

        if (bytes_read == 0) {
            waitForData();
        }
    }

    // signal consumer to finish and drain queue
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_cv_.notify_all();

    if (consumer_thread_.joinable()) consumer_thread_.join();
}

void LogMonitor::stop() {
    running_ = false;
    queue_cv_.notify_all(); // wake producer/consumer
}
