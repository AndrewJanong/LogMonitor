#!/usr/bin/env python3
import argparse, os, random, sys, time

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--path", default="a.log")
    p.add_argument("--lines", type=int, default=10000)
    p.add_argument("--rps", type=float, default=1000, help="0 = as fast as possible")
    p.add_argument("--keywords", default="key1")
    p.add_argument("--p-key", type=float, default=0.5, help="probability a line contains a keyword")
    p.add_argument("--long-frac", type=float, default=0.0, help="fraction of lines that are long")
    p.add_argument("--long-len", type=int, default=6000, help="length target for long lines")
    p.add_argument("--base-len", type=int, default=120, help="length target for normal lines")
    args = p.parse_args()

    keys = [k for k in args.keywords.split(",") if k]
    assert 0.0 <= args.p_key <= 1.0
    assert 0.0 <= args.long_frac <= 1.0

    os.makedirs(os.path.dirname(args.path) or ".", exist_ok=True)

    with open(args.path, "a", buffering=1) as f:
        next_deadline = time.perf_counter()
        for i in range(args.lines):
            ts_ns = time.time_ns()

            use_long = random.random() < args.long_frac
            line_len = args.long_len if use_long else args.base_len
            payload = ("x" * max(0, line_len - 40))  # keep total length near line_len
            kw = random.choice(keys) if (keys and random.random() < args.p_key) else ""
            line = f"{ts_ns} idx={i} {kw} {payload}\n"

            f.write(line)
            f.flush()

            if args.rps > 0:
                next_deadline += 1.0 / args.rps
                sleep_for = next_deadline - time.perf_counter()
                if sleep_for > 0:
                    time.sleep(sleep_for)

if __name__ == "__main__":
    main()
