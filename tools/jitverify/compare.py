#!/usr/bin/env python3
"""Compares two jitverify outputs: prints the first frame whose lines differ (and which fields),
or "identical over N frames". Frames are paired by their number. The header lines (# ...) are
compared too, and a difference in anything but rom= is printed as a warning.

    python tools/jitverify/compare.py a.txt b.txt [--context N] [--fields top,bot]

--context N   also print the N frames before the difference
--fields L    compare only these fields (comma list, e.g. top,bot,aud), and list only them

Exit code 0 identical, 1 different, 2 usage or input error."""
import sys


class Error(Exception):
    pass


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = [line.rstrip("\r\n") for line in f]
    except OSError as e:
        raise Error(f"can't read {path}: {e.strerror}")
    header = next((l for l in lines if l.startswith("# jitverify")), None)
    frames = {}
    order = []
    for line in lines:
        if not line.startswith("F "):
            continue
        parts = line.split()
        if len(parts) < 2 or not parts[1].isdigit():
            raise Error(f"{path}: bad line: {line[:80]}")
        n = int(parts[1])
        if n in frames:
            raise Error(f"{path}: frame {n} twice")
        frames[n] = line
        order.append(n)
    if not frames:
        raise Error(f"{path}: no frames")
    return header, frames, order


def fields(line):
    out = {}
    for part in line.split()[2:]:
        key, _, value = part.partition("=")
        out[key] = value
    return out


def header_fields(header):
    out = {}
    for part in (header or "").split()[2:]:
        key, _, value = part.partition("=")
        out[key] = value
    return out


def parse_args(argv):
    paths, context, only = [], 0, None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--context", "--fields"):
            if i + 1 >= len(argv):
                raise Error(f"{a} needs a value")
            v = argv[i + 1]
            if a == "--context":
                if not v.isdigit():
                    raise Error(f"--context needs a number, not {v}")
                context = int(v)
            else:
                only = [f for f in v.split(",") if f]
                if not only:
                    raise Error("--fields needs at least one name")
            i += 2
        elif a.startswith("--") or a == "-h":
            raise Error(f"unknown option {a}")
        else:
            paths.append(a)
            i += 1
    if len(paths) != 2:
        raise Error("needs two files")
    return paths, context, only


def main():
    try:
        (pa, pb), context, only = parse_args(sys.argv[1:])
        ha, a, order_a = read(pa)
        hb, b, order_b = read(pb)
        if only:
            for path, frames in ((pa, a), (pb, b)):
                have = fields(frames[min(frames)])
                missing = [k for k in only if k not in have]
                if missing:
                    raise Error(f"{path} has no field {', '.join(missing)}")
    except Error as e:
        print(f"error: {e}", file=sys.stderr)
        print(__doc__.strip(), file=sys.stderr)
        return 2

    fa_h, fb_h = header_fields(ha), header_fields(hb)
    differ = sorted(k for k in set(fa_h) | set(fb_h) if k != "rom" and fa_h.get(k) != fb_h.get(k))
    if ha is None or hb is None:
        print("warning: a file has no header line")
    elif differ:
        print("warning: the headers differ in " + ", ".join(f"{k} ({fa_h.get(k, 'missing')} vs {fb_h.get(k, 'missing')})" for k in differ))

    for n in order_a:
        la = a[n]
        lb = b.get(n)
        if lb is None:
            print(f"frame {n} is only in A ({len(a)} frames in A, {len(b)} in B)")
            return 1
        fa, fb = fields(la), fields(lb)
        keys = only if only else list(dict.fromkeys(list(fa) + list(fb)))
        diff = [k for k in keys if fa.get(k) != fb.get(k)]
        if diff:
            print(f"first difference at frame {n}: {', '.join(diff)}")
            earlier = [m for m in order_a if m < n][-context:] if context else []
            for m in earlier:
                print(f"  same {a[m]}")
            print(f"  A    {la}")
            print(f"  B    {lb}")
            return 1
    extra = sorted(set(b) - set(a))
    what = f" ({','.join(only)})" if only else ""
    if extra:
        print(f"identical{what} over A's {len(a)} frames, but B has {len(extra)} more (from frame {extra[0]})")
        return 1
    print(f"identical{what} over {len(a)} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
