#!/usr/bin/env python3
"""step_probe.py ASM - check SDCC kept each probe's call (tools/step_probe.c):
every probe_* function's body holds the symbols its comment names."""
import re, sys

src = open(sys.argv[2] if len(sys.argv) > 2 else "tools/step_probe.c").read()
want = {m[0]: m[1].split() for m in re.findall(r"^uint8_t (probe_\w+)\(.*/\* (.*) \*/$", src, re.M)}
body, cur = {}, None
for line in open(sys.argv[1]):
    m = re.match(r"^_(probe_\w+):", line)
    if m:
        cur = m[1]
        body[cur] = ""
    elif cur and not line.startswith(";"):	# not SDCC's echo of the source
        body[cur] += line
bad = [f"{f}: no {s}" for f, ss in want.items() for s in ss
       if not re.search(re.escape(s) + r"\b", body.get(f, ""))]
if not want or bad:
    print("step_probe: SDCC dropped a STEP_* macro's side effect (sched.h):", *(bad or ["no probes"]), sep="\n  ")
    sys.exit(1)
