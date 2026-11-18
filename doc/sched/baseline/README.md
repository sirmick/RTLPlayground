# Baselines (A0)

Taken before any scheduler code, at feat/sched 063016e (fork main 612116e +
the design book). Later packages compare against these.

| File | What | How it was produced |
|---|---|---|
| `host-tests.txt` | RTLPlayground host tests | `make -C test -j8 quick`, then `make -C test -k -j8 quick` for the full picture; summary at the top, full log below |
| `resources-8x.txt` | 8x image's resource picture: internal RAM areas, stack, common-area end, xdata, per-bank use, DSEG symbols, and the sloc/PARM subset that seeds B6's baseline | `tools/docker-make.sh MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact LOADER=1`, then areas from `rtlplayground.map`, stack and use from `rtlplayground.mem`, DSEG symbols from every application `.asm` (`.area DSEG` labels and `.ds` sizes; loader excluded, it is a separate program) |
| `ovs-rtlpg-tests.txt` | every ovs-rtlpg test | by sim: see the file's header |
| `gate-8x.json`, `gate-schedtest.json` | the resource gate's baselines (B6): internal RAM, stack, common end, banks, DSEG symbols | `tools/resource_gate.py <builddir> --baseline <file> --update` on the A0 8x build and on the schedtest build at B6 |
| `boot-8x.txt` | the 8x image's boot transcript in rtlsim | by sim: see the file's header |

Notes:

- The host tests are red at this baseline: 7 of 17 quick tests fail to link
  because the test stubs lack `leds_mode`, `rldp_*` and `send_loop` (added on
  fork main by the LED and RLDP web commits). See `host-tests.txt`.
- The image names carry `-dirty` because `.wash/plan.toml` (the workspace
  plan, tracked) was modified in the worktree at build time. The Makefile's
  dirty test is `git status --porcelain --untracked-files=no`, so that was the
  only cause: no source file differed from 063016e.
- DSEG: the `.map` gives the area as 128 B from 0, which is its span, not its
  use. The `.mem` counts 105 B of data: the 104 B listed from the `.asm` files
  plus `_bp` from the SDCC library.
- The probe work's `/tmp/ds-*.txt` lists came from a `-DHEALTH` build (hence
  `_ph_line_PARM_2`) and included the loader. This list is the default build
  without the loader.
