#ifndef LOG_MONITOR_H
#define LOG_MONITOR_H

#include <string>
#include <fstream>
#include <vector>

class LogMonitor {
public:
    struct Config {
        std::string input_file;
        std::string output_file;
        size_t buffer_size = 8192; // 8KB
        size_t max_line_length = 5000; // 5000 characters per line
        int poll_interval_ms = 100; // poll every 100ms to check new data
        std::vector<std::string> keywords;
    };

    explicit LogMonitor(const Config& config);
    ~LogMonitor();

    // starts monitoring
    void run();

    // stop monitoring
    void stop();
private:
    Config config_;
    bool running_;

    std::ifstream input_stream_;
    std::ofstream output_stream_;

    std::string current_line_;

    bool openFiles();
    void processBuffer(const char* buffer, size_t bytes_read);
    void processLine(const std::string& line);
    bool containsKeyword(const std::string& line) const;
    void waitForData();

};

#endif // LOG_MONITOR_H