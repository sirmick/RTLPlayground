#!/usr/bin/env python3
"""
pack_banks.py - measure the per-file bank code sizes of a build and pack them
into the fewest 48 KiB banks.

Today each source file is pinned to a code bank by hand (#pragma codeseg). The
result is uneven - one bank is nearly empty - which pushes the code, and
everything after it, further up the flash than necessary. Because the compiled
objects don't care which bank they end up in (the bank number is filled in by
the linker), the files can be repacked with no recompile.

This reads a linked build's .rel files, sums each file's banked code, packs the
files into banks with first-fit-decreasing, reports the current vs packed bank
use, and writes a placement (file -> bank) that pack a build can link with and
that check_cross_bank.py can validate before any relink.

Files that call each other with plain (non-__banked) calls must stay in the
same bank, or those calls break - so the packer keeps such files together: it
groups files joined by a direct call and packs the groups, not loose files.
check_cross_bank.py --placement proves the result call-safe.

When applying a placement, move a file's `#pragma constseg` together with its
`#pragma codeseg`: its strings and __code consts are read through the bank
window of whichever bank is running, so they must sit in the code's own bank.
The sizes here count the CODE area only; consts add to a bank on top. A plain
check_cross_bank.py run on the relinked build checks the const placement.

Usage: pack_banks.py <build-output-dir> [-o placement.json]
"""
import sys, os, re, json, argparse
from collections import defaultdict

BANK_USABLE = 0xC000            # 48 KiB usable per 64 KiB bank window
AREA = re.compile(r"^A (BANK[1-4]) size ([0-9A-Fa-f]+) ")
MAP_SYM = re.compile(r"^C:\s+([0-9A-Fa-f]{8})\s+(\S+)\s+(\S+)?")
INSN = re.compile(r"^\s+([0-9A-Fa-f]{4,6})\s.*\b(lcall|ljmp)\s+(\S+)\s*$")
TRAMPOLINE = {"__sdcc_banked_call", "__sdcc_banked_ret"}


def measure(outdir):
    """file basename -> (current_bank, size) for every file with banked code."""
    items = {}
    for root, _, files in os.walk(outdir):
        for fn in files:
            if not fn.endswith(".rel"):
                continue
            path = os.path.join(root, fn)
            with open(path) as f:
                for line in f:
                    m = AREA.match(line)
                    if not m:
                        continue
                    size = int(m.group(2), 16)
                    if size:
                        items[fn[:-4]] = (int(m.group(1)[4]), size)
    return items


def affinity_groups(outdir, banked):
    """Union files joined by a direct (non-__banked) call. Returns groups as a
    list of sets of file names, so files that must share a bank stay together."""
    sym_mod = {}
    with open(os.path.join(outdir, "rtlplayground.map")) as f:
        for line in f:
            m = MAP_SYM.match(line)
            if m and not m.group(2).startswith("l_") and int(m.group(1), 16):
                sym_mod[m.group(2)] = m.group(3) or ""
    parent = {b: b for b in banked}
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x
    def union(a, b):
        parent[find(a)] = find(b)
    for root, _, files in os.walk(outdir):
        for fn in files:
            if not fn.endswith(".rst"):
                continue
            caller = fn[:-4]
            if caller not in banked:
                continue
            with open(os.path.join(root, fn)) as f:
                for line in f:
                    m = INSN.match(line)
                    if not m:
                        continue
                    t = m.group(3)
                    if t in TRAMPOLINE or t[0] != "_":
                        continue
                    tmod = sym_mod.get(t)
                    if tmod in banked and tmod != caller:
                        union(caller, tmod)
    groups = defaultdict(set)
    for b in banked:
        groups[find(b)].add(b)
    return list(groups.values())


def ffd(sizes, cap=BANK_USABLE, pinned=None):
    """First-fit-decreasing: name->size into bins of cap. `pinned` (name->bank)
    seeds those names into their bank first (e.g. hand-asm that self-banks).
    Returns name->bank(1..)."""
    pinned = pinned or {}
    bins = []                   # list of [used, [names]]; index i -> bank i+1
    placement = {}
    def ensure(b):
        while len(bins) < b:
            bins.append([0, []])
    for name, bank in pinned.items():
        ensure(bank)
        bins[bank - 1][0] += sizes[name]; bins[bank - 1][1].append(name); placement[name] = bank
    for name, size in sorted((kv for kv in sizes.items() if kv[0] not in pinned), key=lambda kv: -kv[1]):
        for i, b in enumerate(bins):
            if b[0] + size <= cap:
                b[0] += size; b[1].append(name); placement[name] = i + 1
                break
        else:
            bins.append([size, [name]]); placement[name] = len(bins)
    return placement, bins


def occupancy(items):
    banks = {}
    for name, (bank, size) in items.items():
        banks.setdefault(bank, 0)
        banks[bank] += size
    return banks


def show(title, banks):
    print(f"{title}:")
    for b in sorted(banks):
        used = banks[b]
        print(f"  bank {b}: {used:6d} / {BANK_USABLE} B  ({100*used/BANK_USABLE:3.0f}%)")
    print(f"  {len(banks)} bank(s), {sum(banks.values())} B of code\n")


def main(argv):
    global BANK_USABLE
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("-o", "--out", help="write placement JSON (file -> bank)")
    ap.add_argument("--pin", action="append", default=[], metavar="FILE=BANK",
                    help="pin a file (and its group) to a bank, e.g. self-banking asm")
    ap.add_argument("--cap", type=lambda s: int(s, 0), default=BANK_USABLE,
                    help="usable bytes per bank; set below 0xC000 for headroom "
                         "(a file's size shifts a little when it changes bank)")
    args = ap.parse_args(argv[1:])
    BANK_USABLE = args.cap
    pins = {}
    for p in args.pin:
        f, b = p.split("="); pins[f] = int(b)

    items = measure(args.outdir)
    if not items:
        sys.exit(f"no banked code found under {args.outdir} (build it first?)")

    show("current (hand-assigned)", occupancy(items))

    sizes = {n: s for n, (_, s) in items.items()}
    # Pack whole direct-call groups, so co-calling files share a bank.
    groups = affinity_groups(args.outdir, set(sizes))
    group_sizes = {min(g): sum(sizes[f] for f in g) for g in groups}
    biggest = max(group_sizes.values())
    if biggest > BANK_USABLE:
        big = max(group_sizes, key=group_sizes.get)
        members = next(g for g in groups if min(g) == big)
        print(f"WARNING: a direct-call group is {biggest} B, larger than a {BANK_USABLE} B bank:")
        print("  " + ", ".join(sorted(members)))
        print("  -> some of its cross-file calls must be made __banked to split it.\n")

    group_pin = {}
    for g in groups:
        for f in g:
            if f in pins:
                group_pin[min(g)] = pins[f]
    gplace, gbins = ffd(group_sizes, cap=BANK_USABLE, pinned=group_pin)
    placement = {f: gplace[min(g)] for g in groups for f in g}
    show("packed (direct-call groups kept together)", {i + 1: b[0] for i, b in enumerate(gbins)})

    cur_banks = len(occupancy(items))
    new_banks = len(gbins)
    freed = cur_banks - new_banks
    multi = [sorted(g) for g in groups if len(g) > 1]
    print(f"{len(groups)} group(s); {len(multi)} must stay together:")
    for g in sorted(multi, key=lambda g: -sum(sizes[f] for f in g))[:6]:
        print(f"  {{{', '.join(g)}}}")
    print(f"\nresult: {cur_banks} banks -> {new_banks} banks"
          + (f", frees {freed} bank ({freed*BANK_USABLE} B of address space)" if freed else ", no change"))

    if args.out:
        with open(args.out, "w") as f:
            json.dump(placement, f, indent=1, sort_keys=True)
        print(f"placement written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
