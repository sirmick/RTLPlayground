#!/usr/bin/env python3
"""
place_banks.py - place the banked files into the code banks, in every build.

The banked sources are compiled into an area named BANKED (--codeseg and
--constseg BANKED: a file's __code data goes with its code). The bank numbers
are only filled in by the linker, so the placement can be made after compiling:
this sizes each file from its .rel, works out from the .asm which files must
share a bank, packs them, and writes a copy of each banked .rel (and its .lst,
for the linker's .rst) under <builddir>/placed/ with BANKED renamed to its bank.
The build links those copies. <builddir>/banks.txt shows the result.

Files must share a bank when one
  - calls another's function directly (lcall/ljmp, not __banked), or
  - reads another C file's __code data directly (mov dptr,#_table): the read
    goes through the bank window, so it sees the running bank, or
  - takes the address of another's function that isn't __banked, or
  - passes a __code address (a string literal, or a named __code const of
    its own or another file's) to another's __banked function, as in
    om_fail("...") from a handler: the callee reads it through the bank
    window, in its own bank. Seen as `mov dptr,#<data>` and then, before any
    other call, `mov r0,#_f` (the banked call's target). Any function so named
    counts, a plain call's argument too: grouping too much only costs room.
    Not seen: an address kept across a call or stored to xdata and passed
    later, or passed through a function pointer; tools/check_cross_bank.py
    checks the linked image for the case seen here.
Such files form a group, and the groups are packed largest first into the least
full bank, so the free space is shared out. If that leaves a group without a
bank, they are packed best fit instead (each into the fullest bank it fits), and
if that fails too, by a search: largest first, each into every bank in turn,
backtracking (at most SEARCH_STEPS placements, then it gives up). Each
fallback runs only when the one before fails, so a build that packs keeps its
placement. A module that names its own bank
(assembly, the generated html_data) stays there and pins its group.

Usage: place_banks.py <builddir> <banked .rel>... -- <other .rel>...
Exit status 1 if the groups don't fit into the banks.
"""
import os, re, sys
from collections import defaultdict

BANKS = 3
BANK_SIZE = 0xC000              # the 0x4000-0xffff window
PLACEHOLDER = "BANKED"
SEARCH_STEPS = 200000           # the search's bound: placements tried

AREA = re.compile(r"^A (\S+) size ([0-9A-Fa-f]+) ")
SYM_DEF = re.compile(r"^S (_\w+) Def")
FUNC = re.compile(r";\s+function\s+(\w+)\s*$")
CALL = re.compile(r"^\s+(?:lcall|ljmp)\s+(_\w+)\s*$")
IMM = re.compile(r"^\s+mov\s+\S+,\s*#\(?\s*(_\w+)")
BANKED_RET = re.compile(r"^\s+ljmp\s+__sdcc_banked_ret\b")
TRAMPOLINE = {"__sdcc_banked_call", "__sdcc_banked_ret"}
DPTR_IMM = re.compile(r"^\s+mov\s+dptr,\s*#(_\w+)\s*$")
BANKED_TARGET = re.compile(r"^\s+mov\s+r0,\s*#(_\w+)\s*$")


def read_rel(path):
    """(banked area name or None, its size, {symbol: area} for the symbols
    the module defines)."""
    area, bank, size, defs = None, None, 0, {}
    for line in open(path):
        m = AREA.match(line)
        if m:
            area = m.group(1)
            n = int(m.group(2), 16)
            if (area == PLACEHOLDER or re.fullmatch(r"BANK[1-9]", area)) and n:
                bank, size = area, size + n
            continue
        m = SYM_DEF.match(line)
        if m:
            defs[m.group(1)] = area
    return bank, size, defs


def read_asm(path):
    """Direct calls, immediate symbol operands, {function: is __banked}, and
    (address, function) for an address loaded into dptr and then a banked
    call's target, before any other call: what may be data handed over."""
    calls, imms, funcs, handed = set(), set(), {}, set()
    func = None
    data = None
    if not os.path.exists(path):
        return calls, imms, funcs, handed
    for line in open(path, errors="replace"):
        m = DPTR_IMM.match(line)
        if m:
            data = m.group(1)
        elif data:
            m = BANKED_TARGET.match(line)
            if m:
                handed.add((data, m.group(1)))
                data = None
            elif re.match(r"^\s+(lcall|ljmp|ret)\b", line):
                data = None
        m = FUNC.search(line)
        if m:
            func = "_" + m.group(1)
            funcs[func] = False
            continue
        if BANKED_RET.match(line) and func:
            funcs[func] = True
        m = CALL.match(line)
        if m and m.group(1) not in TRAMPOLINE:
            calls.add(m.group(1))
            continue
        m = IMM.match(line)
        if m:
            imms.add(m.group(1))
    return calls, imms, funcs, handed


def main(argv):
    if len(argv) < 2 or "--" not in argv:
        sys.exit(__doc__)
    builddir = os.path.normpath(argv[1])
    sep = argv.index("--")
    banked_rels, other_rels = argv[2:sep], argv[sep + 1:]

    mods = {}                   # module -> dict(rel, size, pin, defs, c)
    for rel in banked_rels + other_rels:
        bank, size, defs = read_rel(rel)
        name = os.path.splitext(os.path.relpath(rel, builddir))[0]
        if rel in banked_rels and bank != PLACEHOLDER and size:
            sys.exit(f"place_banks: {rel} was not compiled into {PLACEHOLDER}")
        mods[name] = dict(rel=rel, size=size, defs=defs,
                          pin=None if bank in (None, PLACEHOLDER) else int(bank[4:]),
                          banked=bank is not None,
                          asm=os.path.splitext(rel)[0] + ".asm")
    owner = {s: n for n, m in mods.items() for s, a in m["defs"].items()
             if a == PLACEHOLDER or (a and re.fullmatch(r"BANK[1-9]", a))}
    banked_funcs, c_mods = {}, set()
    refs = {}
    for n, m in mods.items():
        calls, imms, funcs, handed = read_asm(m["asm"])
        if os.path.exists(m["asm"]):
            c_mods.add(n)       # compiled from C (assembly sources have no .asm here)
        banked_funcs.update(funcs)
        refs[n] = (calls, imms, handed)

    parent = {n: n for n, m in mods.items() if m["banked"]}
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    def union(a, b):
        parent[find(a)] = find(b)
    for n, (calls, imms, handed) in refs.items():
        if n not in parent:
            continue            # home code may reach any bank through __banked calls
        for s in calls:
            t = owner.get(s)
            if t and t != n and not banked_funcs.get(s, False):
                union(n, t)
        for s in imms:
            t = owner.get(s)
            if not t or t == n:
                continue
            if s in banked_funcs:
                if not banked_funcs[s]:
                    union(n, t)     # a pointer to a function that isn't __banked
            elif t in c_mods:
                union(n, t)         # a direct read of another file's __code data
        for d, f in handed:
            t = owner.get(f)
            if not t or t == n:
                continue
            if d.startswith("___str_") or (d in owner and d not in banked_funcs):
                union(n, t)         # it reads our __code data in its bank

    groups = defaultdict(list)
    for n in parent:
        groups[find(n)].append(n)
    pinned = [[0, []] for _ in range(BANKS)]
    pgbank = {}
    todo = []
    for g, members in groups.items():
        size = sum(mods[n]["size"] for n in members)
        pins = {mods[n]["pin"] for n in members if mods[n]["pin"]}
        if len(pins) > 1:
            sys.exit(f"place_banks: {sorted(members)} must share a bank but are pinned to {sorted(pins)}")
        if pins:
            b = pins.pop()
            pinned[b - 1][0] += size
            pinned[b - 1][1].extend(members)
            pgbank[g] = b
        else:
            todo.append((size, sorted(members)[0], g))

    def pack(best_fit):
        """largest first, into the least full bank that fits (the free space
        shared out); or, best_fit, into the fullest one that fits"""
        bins = [[u, list(m)] for u, m in pinned]
        gbank = dict(pgbank)
        for size, _, g in sorted(todo, key=lambda t: (-t[0], t[1])):
            fits = [i for i in range(BANKS) if bins[i][0] + size <= BANK_SIZE]
            if not fits:
                return bins, gbank, (size, g)
            if best_fit:
                i = max(fits, key=lambda i: (bins[i][0], -i))
            else:
                i = min(fits, key=lambda i: (bins[i][0], i))
            bins[i][0] += size
            bins[i][1].extend(groups[g])
            gbank[g] = i + 1
        return bins, gbank, None

    def search():
        """largest first, every bank in turn for each group, backtracking;
        banks as full as one already tried are skipped. Deterministic: the
        same groups, the same placement"""
        order = sorted(todo, key=lambda t: (-t[0], t[1]))
        used = [u for u, _ in pinned]
        at = [0] * len(order)           # each group's bank (index)
        steps = 0
        k = 0
        nxt = 0                         # the bank to try first for group k
        while 0 <= k < len(order):
            size = order[k][0]
            tried = {used[i] for i in range(nxt)}
            for i in range(nxt, BANKS):
                if used[i] + size <= BANK_SIZE and used[i] not in tried:
                    break
                tried.add(used[i])
            else:
                k -= 1                  # none: back to the group before
                if k >= 0:
                    used[at[k]] -= order[k][0]
                    nxt = at[k] + 1
                continue
            steps += 1
            if steps > SEARCH_STEPS:
                break
            used[i] += size
            at[k] = i
            k += 1
            nxt = 0
        if k != len(order):
            return None
        bins = [[u, list(m)] for u, m in pinned]
        gbank = dict(pgbank)
        for (size, _, g), i in zip(order, at):
            bins[i][0] += size
            bins[i][1].extend(groups[g])
            gbank[g] = i + 1
        return bins, gbank, None

    bins, gbank, miss = pack(False)
    if miss:        # only then: a build that packs keeps its placement
        bins, gbank, miss2 = pack(True)
        how = "packed best fit"
        if miss2:
            found = search()
            if found:
                bins, gbank, miss2 = found
                how = "packed by a search"
        if miss2:
            size, g = miss
            print(f"place_banks: no bank has room for {size} bytes: "
                  + " ".join(sorted(groups[g])), file=sys.stderr)
            for i, b in enumerate(bins):
                print(f"  bank {i + 1}: {b[0]} of {BANK_SIZE}", file=sys.stderr)
            return 1
        print("place_banks: the free space couldn't be shared out: " + how, file=sys.stderr)

    with open(os.path.join(builddir, "banks.txt"), "w") as out:
        out.write("# Generated by tools/place_banks.py: the bank each banked file was placed in\n")
        for i, (used, members) in enumerate(bins):
            out.write(f"# bank {i + 1}: {used} of {BANK_SIZE} bytes, {BANK_SIZE - used} free\n")
        for n in sorted(parent, key=str.lower):
            out.write(f"{n} {gbank[find(n)]}{'' if mods[n]['pin'] is None else ' (pinned)'}\n")

    for rel in banked_rels:
        n = os.path.splitext(os.path.relpath(rel, builddir))[0]
        dst = os.path.join(builddir, "placed", n + ".rel")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        bank = f"BANK{gbank[find(n)]}" if n in parent else PLACEHOLDER
        with open(rel) as f, open(dst + ".tmp", "w") as o:
            for line in f:
                if line.startswith(f"A {PLACEHOLDER} "):
                    line = f"A {bank} " + line[len(f"A {PLACEHOLDER} "):]
                o.write(line)
        os.replace(dst + ".tmp", dst)
        lst = os.path.splitext(rel)[0] + ".lst"
        if os.path.exists(lst):
            with open(lst) as f, open(os.path.splitext(dst)[0] + ".lst", "w") as o:
                o.write(f.read())
        stale = os.path.splitext(rel)[0] + ".rst"      # from a build before placement
        if os.path.exists(stale):
            os.remove(stale)

    print("place_banks: " + ", ".join(f"bank {i + 1} {b[0]} B ({BANK_SIZE - b[0]} free)"
                                      for i, b in enumerate(bins)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
