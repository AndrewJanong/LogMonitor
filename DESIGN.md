# Design Overview

The program monitors a growing log file, reading and truncating each line to a maximum of 5000 characters, and writes only those that match a keyword filter into a separate output file. It is designed to handle extremely long lines and high throughput. The design prioritizes correctness, predictable memory usage, and low latency.

# Core Architecture

The system is a two‑stage pipeline implemented in a single class (`LogMonitor`):

1. **Producer (reader)**: sequentially reads raw bytes from the input file into a fixed‑size buffer, assembles lines, and enqueues completed or truncated lines.
2. **Consumer (filter/writer)**: dequeues lines, applies keyword filtering, and appends accepted lines to the output file.

A bounded `deque<std::string>` bridges the stages, guarded by a mutex and condition variable to implement **backpressure** and **avoid unbounded memory growth**.

# I/O Strategy

- **POSIX read loop**: The reader uses `open`/`read` and seeks the input to end at startup, reading only newly appended data.
- **Chunked reads**: Data is read into a reusable buffer of configurable size (`buffer_size`, default 1 MiB). Reading in chunks was decided to minimize syscalls while keeping latency low for line assembly.
- **Polling**: If no new data is available, the reader will sleep for a short interval (poll_interval_ms, default 1 ms). This avoids busy‑spinning, but still responsive.

# Line Assembly & Truncation

- **Delimiter scan**: Within each buffer, the reader scans for `\n` using `memchr` semantics. Carriage returns (`\r`) are ignored.
- **Truncated line building**: Characters are appended into a reusable `current_line_` string.
- **Hard limit**: Once `current_line_` reaches `max_line_length` (default 5000), the line is **emitted immediately** and the remainder of the physical line is **skipped** until the next `\n`. This will ignore unecessary handling of unused characters, and allows the consumer thread to start processing the truncated line as soon as possible.
- **Capacity reuse**: After emission, `current_line_` is reserved back to `max_line_length` so that appending will not result in multiple reallocations.

# Filtering

- **Substring match**: If the number of keywords is small (< 4), then consumer simple applies a simple substring search using `find()`. Otherwise, use Aho-Corasick algorithm to do string matching across many keywords efficiently.

# Concurrency & Backpressure

- **Two threads**: A dedicated reader thread and a dedicated consumer thread provide natural overlap between I/O and CPU work without over‑complicating synchronization.
- **Bounded queue**: The queue has a configurable capacity (`queue_capacity`). The reader blocks when the queue is full, applying backpressure so the process cannot run out of memory if the producer is faster than the consumer or if the output is slow.
- **Minimal copying**: Completed lines are moved into the queue and moved again into the consumer, reducing allocations and copies on the hot path.

# Output

- **Append‑only writes**: The consumer opens the output file using `std::osfstream` in append mode and flushes on shutdown. Each accepted line is written followed by `\n`.
