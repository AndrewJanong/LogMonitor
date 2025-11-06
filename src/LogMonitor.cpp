#include "LogMonitor.h"
#include <iostream>
#include <thread>

LogMonitor::LogMonitor(const Config& config) 
    : config_(config) 
    , running_(false) {}

LogMonitor::~LogMonitor() {
    if (input_stream_.is_open()) input_stream_.close();
    if (output_stream_.is_open()) output_stream_.close();
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

void LogMonitor::processLine(const std::string& line) {
    output_stream_ << line << std::endl;
}

void LogMonitor::processBuffer(const char* buffer, size_t bytes_read) {
    for (size_t i = 0; i < bytes_read; i++) {
        char ch = buffer[i];

        if (ch == '\r') {
            continue;
        } else if (ch == '\n') {
            processLine(current_line_);
            current_line_.clear();
        } else {
            if (current_line_.size() < 5000) {
                current_line_ += ch; // only add to current line if haven't exceeded line limit
            }
        }
    }
}

void LogMonitor::waitForData() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

void LogMonitor::run() {
    if (!openFiles()) {
        return;
    }

    running_ = true;

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
}

void LogMonitor::stop() {
    running_ = false;
}