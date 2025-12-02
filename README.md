# LogMonitor

A tiny, dependency‑free C++17 tool that *tails* a growing log file and writes only the
lines that contain any of the specified keywords to a second file. It is designed to
handle extremely large files and lines that can span
many gigabytes by truncating each logical line to the first **5000 characters** (line will only be printed if any keyword exists in the first 5000 characters of each line).

> Runs natively on Ubuntu 22.04 LTS (and other Linux/Unix) with no external dependencies

## Build

To build, run the script:

```bash
./build.sh
```

This produces the binary:

```
build/log-monitor
```

> Note: The project requires CMake ≥ 3.16 and a C++17 compiler (e.g., GCC 11+ or Clang 14+ on Ubuntu 22.04).

## Usage

```
log-monitor <input_log> <output_log> [--bench-stamp] [keyword1 keyword2 ...]
```

- `<input_log>`: path to the file to monitor (existing file)
- `<output_log>`: path to the output file (will be created/appended)
- `--bench-stamp`: if present, appends `\t#MON_TS=<epoch_ns>` to each emitted line to help latency benchmarking
- `--pin-reader=<cpu>`: pins reader thread to the logical CPU
- `--pin-consumer=<cpu>`: pins reader thread to the logical CPU
- `keyword...`: zero or more keywords. If no keywords are provided, all lines are emitted

### Examples

Filter two keywords:

```bash
./build/log-monitor a.log b.log key1 key2
```

Same but with per-line benchmark stamp for latency measurements:

```bash
./build/log-monitor a.log b.log --bench-stamp key1 key2
```

Emit every line (no filtering):

```bash
./build/log-monitor a.log b.log
```

Stop with **Ctrl‑C** (SIGINT)


## How it works

- The program opens the input log with a POSIX file descriptor and seeks to EOF (end-of-file), it then loops calling `read()` into a buffer.
- Each logical line is accumulated up to **5000 chars**. Upon hitting the limit, the remainder of that line is skipped until the next `\n` is seen.
- Right after reaching limit, lines are placed onto a bounded queue and a consumer thread performs keyword checks and writes accepted lines to the output file 

> Rationale and trade‑offs are explained in detail in **DESIGN.md**.

## Benchmarking

Simple benchmarking scripts can be found in the `/benchmarks` directory. For simplicity we are using `a.log` as the log file our program is monitoring and `b.log` as the output.

- `writer.py`: a script that write to the specified log file (the log file being monitored), taking paramaters such as number of lines, lines/sec, line length, etc.
- `verify_output.py`: a script that will run the writer script and our program, then read the output log to check if lines exceed the limit or doesn't contain any keywords.
- `bench_latency.py`: a script that will run the writer script and our program in `--bench-stamp` mode to know the end timestamp. 
- `parse_latency.py`: this should be run after `bench_latency.py`, which will read the output logs and calculate latency statistics (p50, p90, p95, p99) in milliseconds.

Simple benchmarking to try:

- Checking correctness:

```bash
python3 benchmarks/verify_output.py --monitor-cmd "./build/log-monitor a.log b.log key1"
```

By default will write 1000 lines with keywords 'key1' with base line length 1000000 and longer lines will be 5000000, each having 0,5 probability. Program will monitor and write to the output, which then will be checked its correctness.

- Latency testing:

```bash
python3 benchmarks/bench_latency.py --lines 10000 --rps 1000 --monitor-cmd "./build/log-monitor a.log b.log --bench-stamp key1"

python3 benchmarks/parse_latency.py --path b.out
```

Writer will write 10000 lines with 1000 lines/s each of length 1000000. Program will monitor and write to output, which the second command will parse the output to get latency stats.