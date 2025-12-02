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
#include <memory>

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
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::string*> line_queue_;

    std::vector<std::unique_ptr<std::string>> buffer_pool_;
    std::vector<std::string*> free_buffers_;
    std::mutex pool_mutex_;

    bool skip_line_ = false;

    bool use_aho_ = false;
    std::unique_ptr<AhoCorasick> aho_;

    bool openFiles();
    void processBuffer(const char* buffer, size_t bytes_read);
    void emitLine();
    void processLine(std::string&& line);
    bool containsKeyword(const std::string& line) const;
    void waitForData();
    void consumerLoop();

    static void pinThread(int cpu);

    std::string* acquireBuffer();
    void releaseBuffer(std::string* buf); 
};

#endif // LOG_MONITOR_H
