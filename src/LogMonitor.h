#ifndef LOG_MONITOR_H
#define LOG_MONITOR_H

#include <string>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

class LogMonitor {
public:
    struct Config {
        std::string input_file;
        std::string output_file;
        size_t buffer_size = 8192; // 8KB
        size_t max_line_length = 5000; // 5000 characters per line
        int poll_interval_ms = 1; // poll every 1ms to check new data
        std::vector<std::string> keywords;
        bool bench_stamp = false;
        size_t queue_capacity = 4096; // bound for line queue
    };

    explicit LogMonitor(const Config& config);
    ~LogMonitor();

    // starts monitoring
    void run();

    // stop monitoring
    void stop();
private:
    Config config_;
    std::atomic<bool> running_{false};

    std::ifstream input_stream_;
    std::ofstream output_stream_;

    std::string current_line_;

    std::thread consumer_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::string> line_queue_;

    bool openFiles();
    void processBuffer(const char* buffer, size_t bytes_read);
    void processLine(const std::string& line);
    bool containsKeyword(const std::string& line) const;
    void waitForData();
    void consumerLoop();
};

#endif // LOG_MONITOR_H
