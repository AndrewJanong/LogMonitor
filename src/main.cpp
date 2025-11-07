#include "LogMonitor.h"
#include <iostream>
#include <csignal>

static LogMonitor* g_monitor = nullptr;

static void handle_sigint(int) {
    if (g_monitor) g_monitor->stop();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Missing arguments!\n";
        return 1;
    }

    LogMonitor::Config cfg;
    cfg.input_file = argv[1];
    cfg.output_file = argv[2];
    if (argc > 3) {
        std::string arg = argv[3];
        std::cout << arg << std::endl;
        if (arg == "--bench-stamp") {
            cfg.bench_stamp = true;
            cfg.keywords.assign(argv + 4, argv + argc);
        } else {
            cfg.keywords.assign(argv + 3, argv + argc);
        }
    }   

    LogMonitor monitor(cfg);
    g_monitor = &monitor;

    // Ctrl-C support
    std::signal(SIGINT, handle_sigint);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_sigint);
#endif

    monitor.run();

    std::cout << "Done\n";
    return 0;
}
