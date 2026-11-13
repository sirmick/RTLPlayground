#!/usr/bin/env python3
"""
pack_banks.py - measure the per-file bank code sizes of a build and pack them
into the fewest 48 KiB banks.

Each banked source file's bank is set in banks.txt (`make repack` runs this
with --banks to rewrite it). The bank number is filled in by the linker, so the
compiled objects don't care which bank they end up in: a placement can be
worked out from one build and checked before anything is recompiled.

This reads a linked build's .rel files, sums each file's banked code, packs the
files into banks with first-fit-decreasing, reports the current vs packed bank
use, and writes a placement (file -> bank) that pack a build can link with and
that check_cross_bank.py can validate before any relink.

Files that call each other with plain (non-__banked) calls must stay in the
same bank, or those calls break, and so must a file and the __code data it
reads directly (httpd and the web UI's html_data) - so the packer keeps such
files together: it groups them and packs the groups, not loose files.
check_cross_bank.py --placement proves the result call-safe.

A file's strings and __code consts go to its own bank with its code
(--constseg), and the sizes here include them. check_cross_bank.py, run on
every link, checks the result.

Usage: pack_banks.py <build-output-dir>... [--banks banks.txt] [-o placement.json]

With several build directories (machines, layouts, HEALTH=1), each file counts
at its largest size, so the placement fits all of them. --banks rewrites
banks.txt (the build's placement) with the groups spread over the banks so the
free space is shared out evenly; the board's init file is MACHINE_INIT there.
Modules that place themselves (assembly, the generated html_data) are pinned
to the bank they are in and not written.
"""
import sys, os, re, json, argparse
from collections import defaultdict

BANK_USABLE = 0xC000            # 48 KiB usable per 64 KiB bank window
AREA = re.compile(r"^A (BANK[1-4]) size ([0-9A-Fa-f]+) ")
MAP_SYM = re.compile(r"^C:\s+([0-9A-Fa-f]{8})\s+(\S+)\s+(\S+)?")
INSN = re.compile(r"^\s+([0-9A-Fa-f]{4,6})\s.*\b(lcall|ljmp)\s+(\S+)\s*$")
# an immediate symbol operand (mov dptr,#_f_data): a direct read of __code data,
# which goes through the running bank's window, so the reader shares its bank
IMM = re.compile(r"^\s+([0-9A-Fa-f]{4,6})\s.*\bmov\s+\S+,#\(?\s*(_\w+)")
FUNC = re.compile(r";\s+function\s+(\w+)\s*$")     # SDCC's per-function comment
TRAMPOLINE = {"__sdcc_banked_call", "__sdcc_banked_ret"}


MACHINE_INIT = "MACHINE_INIT"


# The boards' init files (one is linked per machine): machine/*_init.c and
# machine/pcb/*_init.c.
INIT_FILES = {os.path.basename(f)[:-2] for d in ("machine", "machine/pcb")
              if os.path.isdir(d) for f in os.listdir(d) if f.endswith("_init.c")
              for f in [os.path.join(d, f)]}


def module_name(name, reldir=""):
    """A module's name in the placement: its file name, or MACHINE_INIT for
    the board's init file."""
    return MACHINE_INIT if name in INIT_FILES else name


SOURCES = {}     # module -> source path relative to the tree (for banks.txt)


def measure(outdir):
    """module -> (current_bank, size) for every file with banked code."""
    items = {}
    for root, _, files in os.walk(outdir):
        if os.sep + "loader" in root[len(outdir):] or os.sep + "tmp" in root[len(outdir):]:
            continue
        for fn in files:
            if not fn.endswith(".rel"):
                continue
            path = os.path.join(root, fn)
            reldir = os.path.relpath(root, outdir)
            reldir = "" if reldir == "." else reldir
            name = module_name(fn[:-4], reldir)
            with open(path) as f:
                for line in f:
                    m = AREA.match(line)
                    if not m:
                        continue
                    size = int(m.group(2), 16)
                    if size:
                        items[name] = (int(m.group(1)[4]), size)
                        src = os.path.join(reldir, fn[:-4] + ".c")
                        if name != MACHINE_INIT and name != "html_data" and os.path.exists(src):
                            SOURCES[name] = src
    return items


def affinity_groups(outdir, banked):
    """Union files joined by a direct (non-__banked) call. Returns groups as a
    list of sets of file names, so files that must share a bank stay together."""
    sym_mod = {}
    with open(os.path.join(outdir, "rtlplayground.map")) as f:
        for line in f:
            m = MAP_SYM.match(line)
            if m and not m.group(2).startswith("l_") and int(m.group(1), 16):
                sym_mod[m.group(2)] = module_name(m.group(3) or "")
    parent = {b: b for b in banked}
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x
    def union(a, b):
        parent[find(a)] = find(b)
    # function symbols, so a banked call's mov r0,#_func isn't taken for data
    funcs = set()
    for root, _, files in os.walk(outdir):
        for fn in files:
            if fn.endswith(".rst"):
                with open(os.path.join(root, fn)) as f:
                    funcs.update("_" + m.group(1) for m in map(FUNC.search, f) if m)
    def data_module(sym):
        mod = sym_mod.get(sym)
        if sym in funcs or mod not in banked or not (mod in SOURCES or mod == "html_data"):
            return None
        return mod
    for root, _, files in os.walk(outdir):
        for fn in files:
            if not fn.endswith(".rst"):
                continue
            reldir = os.path.relpath(root, outdir)
            caller = module_name(fn[:-4], "" if reldir == "." else reldir)
            if caller not in banked:
                continue
            with open(os.path.join(root, fn)) as f:
                for line in f:
                    m = INSN.match(line)
                    if not m:
                        m = IMM.match(line)
                        mod = data_module(m.group(2)) if m else None
                        if mod and mod != caller:
                            union(caller, mod)
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


def balance(sizes, nbanks, cap=BANK_USABLE, pinned=None):
    """Largest first into the least-full bank, so the free space is spread over
    the banks rather than left in the last one. `pinned` as for ffd(). Returns
    (placement, bins) like ffd(); a group that fits nowhere gets a new bank."""
    pinned = pinned or {}
    bins = [[0, []] for _ in range(nbanks)]
    placement = {}
    for name, bank in pinned.items():
        while len(bins) < bank:
            bins.append([0, []])
        bins[bank - 1][0] += sizes[name]; bins[bank - 1][1].append(name); placement[name] = bank
    for name, size in sorted((kv for kv in sizes.items() if kv[0] not in pinned), key=lambda kv: (-kv[1], kv[0])):
        i = min((i for i, b in enumerate(bins) if b[0] + size <= cap), key=lambda i: bins[i][0], default=None)
        if i is None:
            bins.append([0, []]); i = len(bins) - 1
        bins[i][0] += size; bins[i][1].append(name); placement[name] = i + 1
    return placement, bins


def write_banks(path, placement):
    """Rewrite banks.txt: keep its header comment, then one "source bank" line
    per placed C file (MACHINE_INIT for the board's init file)."""
    head, kept = [], []
    placed = {SOURCES[n] for n in placement if n in SOURCES} | {MACHINE_INIT}
    if os.path.exists(path):
        body = False
        for line in open(path):
            if not body and (line.startswith("#") or not line.strip()):
                head.append(line)
                continue
            body = True
            f = line.split()
            if f and not line.startswith("#") and f[0] not in placed:
                kept.append(line.strip())   # no banked code in these builds: leave as is
    lines = list(kept)
    for name, bank in placement.items():
        if name == MACHINE_INIT:
            lines.append(f"{MACHINE_INIT} {bank}")
        elif name in SOURCES:
            lines.append(f"{SOURCES[name]} {bank}")
    with open(path, "w") as f:
        f.writelines(head)
        f.write("\n".join(sorted(lines, key=str.lower)) + "\n")


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
    ap.add_argument("outdir", nargs="+", help="build output directories to size from")
    ap.add_argument("-o", "--out", help="write placement JSON (file -> bank)")
    ap.add_argument("--banks", metavar="BANKS_TXT",
                    help="rewrite banks.txt with a balanced placement")
    ap.add_argument("--pin", action="append", default=[], metavar="FILE=BANK",
                    help="pin a file (and its group) to a bank")
    ap.add_argument("--cap", type=lambda s: int(s, 0), default=BANK_USABLE,
                    help="usable bytes per bank; set below 0xC000 for headroom "
                         "(a file's size shifts a little when it changes bank)")
    args = ap.parse_args(argv[1:])
    BANK_USABLE = args.cap
    pins = {}
    for p in args.pin:
        f, b = p.split("="); pins[f] = int(b)

    # Each file at its largest size over the builds; groups joined across them.
    items, groups = {}, []
    for d in args.outdir:
        got = measure(d)
        if not got:
            sys.exit(f"no banked code found under {d} (build it first?)")
        for n, (bank, size) in got.items():
            if n not in items or size > items[n][1]:
                items[n] = (bank, size)
        groups += affinity_groups(d, set(got))
    # merge overlapping groups from the different builds
    merged = []
    for g in groups:
        g = set(g)
        for m in [m for m in merged if m & g]:
            g |= m; merged.remove(m)
        merged.append(g)
    groups = merged

    show("current", occupancy(items))
    sizes = {n: s for n, (_, s) in items.items()}

    # Modules that place themselves stay where they are.
    for n, (bank, _) in items.items():
        if n != MACHINE_INIT and n not in SOURCES:
            pins.setdefault(n, bank)

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
                if min(g) in group_pin and group_pin[min(g)] != pins[f]:
                    sys.exit(f"group {sorted(g)} is pinned to two banks")
                group_pin[min(g)] = pins[f]
    if args.banks:
        gplace, gbins = balance(group_sizes, len(occupancy(items)), cap=BANK_USABLE, pinned=group_pin)
        title = "balanced (direct-call groups kept together)"
    else:
        gplace, gbins = ffd(group_sizes, cap=BANK_USABLE, pinned=group_pin)
        title = "packed (direct-call groups kept together)"
    placement = {f: gplace[min(g)] for g in groups for f in g}
    show(title, {i + 1: b[0] for i, b in enumerate(gbins)})

    multi = [sorted(g) for g in groups if len(g) > 1]
    print(f"{len(groups)} group(s); {len(multi)} must stay together:")
    for g in sorted(multi, key=lambda g: -sum(sizes[f] for f in g))[:6]:
        print(f"  {{{', '.join(g)}}}")
    print(f"\nresult: {len(occupancy(items))} banks -> {len(gbins)} banks")

    if args.out:
        with open(args.out, "w") as f:
            json.dump(placement, f, indent=1, sort_keys=True)
        print(f"placement written to {args.out}")
    if args.banks:
        write_banks(args.banks, placement)
        print(f"placement written to {args.banks}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
