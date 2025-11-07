#!/usr/bin/env python3
import os, subprocess, time, statistics, argparse, re

STAMP_RE = re.compile(r"\t#MON_TS=(\d{12,})")

def read_new_lines(path, last_pos):
    try:
        with open(path, "r") as f:
            f.seek(last_pos)
            data = f.read()
            last_pos = f.tell()
        if not data:
            return [], last_pos
        lines = data.splitlines()
        return lines, last_pos
    except FileNotFoundError:
        return [], last_pos

def parse_ts_ns(line):
    m = re.match(r"(\d{12,})", line)
    return int(m.group(1)) if m else None

def parse_mon_ts_ns(line):
    m = STAMP_RE.search(line)
    return int(m.group(1)) if m else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--monitor-cmd", default=os.environ.get("MONITOR_CMD", "./build/log-monitor a.log b.log --bench-stamp key1"))
    ap.add_argument("--lines", type=int, default=10000)
    ap.add_argument("--rps", type=float, default=2000)
    ap.add_argument("--warmup", type=float, default=0.3)
    ap.add_argument("--poll-ms", type=float, default=10.0)
    ap.add_argument("--require-stamp", action="store_true")
    
    args = ap.parse_args()

    for p in ["a.log", "b.log"]:
        try: os.remove(p)
        except FileNotFoundError: pass

    open("a.log", "a").close()

    mon = subprocess.Popen(args.monitor_cmd, shell=True)
    time.sleep(args.warmup)

    writer = subprocess.Popen(
        f"python3 benchmarks/writer.py --path a.log --lines {args.lines} --rps {args.rps} --p-key 0.5 --long-frac 0.0",
        shell=True
    )

    last_pos = 0
    end_to_end_ms = []
    proc_ms = []
    stamped_count = 0
    last_growth_time = time.time()
    seen_lines = 0

    while True:
        lines, last_pos_new = read_new_lines("b.log", last_pos)
        if lines:
            now_ns = time.time_ns()
            for line in lines:
                ts = parse_ts_ns(line)
                if ts:
                    end_to_end_ms.append((now_ns - ts) / 1e6)
                    mts = parse_mon_ts_ns(line)
                    if mts is not None:
                        stamped_count += 1
                        proc_ms.append((mts - ts) / 1e6)
            last_growth_time = time.time()
            seen_lines += len(lines)
        last_pos = last_pos_new

        if writer.poll() is not None:
            # stop after 2s of no growth
            if time.time() - last_growth_time > 2.0:
                break
        time.sleep(max(0.0, args.poll_ms / 1000.0))

    try:
        mon.terminate()
    except Exception:
        pass

    if not end_to_end_ms:
        print("No filtered lines observed, check your --keywords or monitor wiring")
        return
    if args.require_stamp and stamped_count == 0:
        print("No #MON_TS found, check if you used --bench-stamp")
        return

    end_to_end_ms.sort()
    p50_e2e = end_to_end_ms[len(end_to_end_ms)//2]
    p95_e2e = end_to_end_ms[int(len(end_to_end_ms)*0.95)]
    print(f"End-to-end: p50={p50_e2e:.2f} ms, p95={p95_e2e:.2f} ms over {len(end_to_end_ms)} lines")

    if proc_ms:
        proc_ms.sort()
        p50_p = proc_ms[len(proc_ms)//2]
        p95_p = proc_ms[int(len(proc_ms)*0.95)]
        print(f"Process: p50={p50_p:.2f} ms, p95={p95_p:.2f} ms (from {stamped_count} stamped lines)")

if __name__ == "__main__":
    main()
