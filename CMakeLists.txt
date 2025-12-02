cmake_minimum_required(VERSION 3.16)
project(log_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(log-monitor
    src/main.cpp
    src/LogMonitor.cpp
    src/AhoCorasick.cpp
)

target_include_directories(log-monitor PRIVATE src)
