#!/usr/bin/env python3
import os, subprocess, time, sys, argparse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keywords", default="key1")
    ap.add_argument("--lines", type=int, default=2000)
    ap.add_argument("--long-len", type=int, default=6000)
    args = ap.parse_args()

    keys = [k for k in args.keywords.split(",") if k]
    keys_l = [k.lower() for k in keys]

    for p in ["a.log", "b.log"]:
        try: os.remove(p)
        except FileNotFoundError: pass

    open("a.log", "a").close()

    mon = subprocess.Popen(os.environ.get("MONITOR_CMD", "./build/log-monitor a.log b.log key1"), shell=True)
    time.sleep(0.3)

    subprocess.run(
        f"python3 benchmarks/writer.py --path a.log --lines {args.lines} --rps 0 --p-key 1.0 --long-frac 1.0 --long-len {args.long_len} --keywords {args.keywords}",
        shell=True, check=True
    )

    time.sleep(1.0)
    try: mon.terminate()
    except Exception: pass
    time.sleep(0.2)

    total = 0
    trunc_fail = 0
    keyword_fail = 0

    try:
        with open("b.log", "r") as f:
            for raw in f:
                total += 1
                line = raw.rstrip("\n")

                if len(line) > 5000:
                    trunc_fail += 1

                ll = line.lower()
                if keys_l and not any(k in ll for k in keys_l):
                    keyword_fail += 1
    except FileNotFoundError:
        print("FAIL: b.log not found")
        sys.exit(1)

    if total == 0:
        print("FAIL: no output lines found in b.log")
        sys.exit(1)

    if trunc_fail == 0 and keyword_fail == 0:
        print(f"OK: {total} lines — all <= 5000 chars and contain one of: {', '.join(keys)}")
        sys.exit(0)
    else:
        if trunc_fail:
            print(f"Truncation violations: {trunc_fail}/{total} lines exceed 5000 chars")
        if keyword_fail:
            print(f"Keyword violations: {keyword_fail}/{total} lines missing any of: {', '.join(keys)}")
        sys.exit(1)

if __name__ == "__main__":
    main()
