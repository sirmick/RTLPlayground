# 14. Testing

Four layers, cheapest first, then the bench. Every package's evidence file
gives the exact commands and their results (`evidence/`).

## Layer 1: host unit tests (`test/`, gcc)

`make -C test quick`: 20 tests, `test_loader` skipped; seconds. Those that
cover the scheduler:

- `test_sched`: the core, the rings and the crash record compiled for the
  host (`sched.c`, `sched_bank.c`, `con_ring.c`, `crash.c`,
  `sched_health.c`), over tables the generator makes from `test_sched.c`'s
  own `TASK`/`STEP` lines (`--slots 8`, `test/sched_core/sequence.def`):
  the ready check across the 16-bit tick wrap, ms to ticks never early,
  claim and free and a full table, the return macros and `arg`,
  `task_wake`, the TX ring at 16 bits (wrap, full, the drain, a byte into a
  full ring with ES off), the crash record's identity. 119 checks at E1.
- `test_sched_gen.py`: the generator's parse, every check with a broken
  input, the sweep's checks (decision 22's two), a derived sequence, the
  golden output (`test/sched_gen/golden/`).
- `test_resource_gate.py`: the gate on fixtures (a real SDCC spill, a
  common-area overflow, a short stack), then on the 8x builds in `output/`
  (chapter 12).
- `test_httpd_tx`: `httpd.c` and `uip.c` unmodified, with a flash-job
  stand-in that calls the real `upload_prog/verify/done` at once or a step
  at a time: the upload window (no ACK moves the right edge left), the
  window race, an upload during another job, a busy job, the zero-window
  probe and the segment past the window (U9) (e3-flash-job.txt).
- `test_cmd_editor`: the editor, incl. U2.

`make -C test/stp`: the STP scenarios, catching BPDUs at `ctl_output()`.

## Layer 2: the schedtest image in the simulator

`make schedtest MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact LOADER=1`
(`schedtest/README.md` has the commands, symbols and variants): the whole
firmware linked with the real toolchain flow (crt, banking, `place_banks`,
the generator, the 3-byte table) and the real boot (`firmware_init()`),
but the firmware's tasks replaced by dummies spread over **every bank**
(decision 17). Its gate runs at every build.

| Dummy (task) | Checks |
|---|---|
| `sleep_a/b/c`: 5 ms, 37 ms, 1000 ms; each logs the fine clock per run | wake accuracy: never early, late by at most a pass |
| `waiter`, woken by `poster` (`task_wake`), 100 ms timeout | wake latency within one pass (the waiter is before the poster in `run_seq`); the timeout path |
| `isr_ev`, waits on `HW_CON_RX`; the test writes UART bytes | ISR -> event -> task |
| `hot`, high, 1 µs of work | spacing: worst gap ≤ its spacing × the longest step |
| `jobber` and its `jb` jobs, N steps each, claimed until the table is full | claim, free, `SCHED_NONE` |
| `pr`, a 3000-line dump, a line a step | TX ring, `HW_CON_TX`, nothing lost |
| `fl`, erases, programs and verifies a sector of the upload area | flash steps; the checker clean |
| `sl`: one 25-30 ms step on command (`slow`), or a spin (`hang`) | the overrun count and its state; the watchdog |
| `deep`, a 9-deep call chain under console input | the stack's high-water |
| `con`, `cli`: schedtest's own console | a 500-character paste: nothing lost |

`make schedtest-sim MACHINE=... LAYOUT=compact [SIM_ARGS=--twice]` runs
`schedtest/sim_tests.py` (14 tests: wake, waiter, isr_ev, hot, jobs,
printer, flash, slow, paste, invariants, stack, horizon, soak, probes) over
the control socket. Besides the table: at pass ends every slot's state is 0
or valid, masks use only defined bits, no wake lies further ahead than its
longest wait; every state's step function and every line of `sched.c` ran;
`checks` is clean; stack headroom ≥ 30 B; a 10-minute soak of firmware
time; a SCHED_FOREVER horizon pushed on without running the step; the
flash's bank window reads 0x00 while busy (`erasepeek`). `--twice` runs it
all twice and requires every number the same. Measured numbers:
b4-sim-tests.txt; the suite caught a real fault (`sched_ms_to_ticks`
without its extra tick: "never early" failed).

## Layer 2b: negative tests (must fail as intended)

`schedtest/sim_neg.py`, in the same target:

| Variant | Expected |
|---|---|
| `hang`: a step that spins for ever | the watchdog resets 4995-5000 ms after the step began; through the loader; the record names `sl/SL_HANG`, the PC inside `sl_hang`, SP, the reset count; the boot prints it, clears it, `health` keeps it |
| `ctrl_r` (on the hang image) | `^R ^R` and the `reset` command: their records, causes 2 and 3 |
| `powercut` | the record is gone, nothing printed |
| `overrun`: a 55 ms step (25 ms until decision 25) | one overrun, the state named in `health` |
| `bad_flash`: starts an erase and returns | exactly one check, `flash-busy`, in `fl_erase`; rtlsim exits 6 |
| `bad_spec`: a state no step reaches | the generator fails the build |
| `fall_through`: a step that can fall off its end | the build fails with SDCC's error 59 |

(b5-neg-tests.txt, e5-step-api.txt.)

## Layer 3: old vs new (from D)

The harness in ovs-rtlpg's `tests/oldnew/` (chapter 13): 17 scenarios on
fork main (b635486) and the new build, compared on results, with each
package's expect file (`evidence/*-oldnew.expect`), and the new-only
checks with `--checks --known`:

    python3 tests/oldnew/oldnew.py --old <b635486 -boot> --new <new -boot> \
      --new-payload <new app> --board boards/<board>.json --checks \
      --known tests/oldnew/known-failures --expect <the package's expect file>

Run on both boards, the 8x and SWGT024. At every M3 package: 17 of 17
(e1-e4 evidence, e5-step-api.txt, es-loop-flags.txt).

**The new-only acceptance checks** and where they stand:

| Check | Status |
|---|---|
| LACP PDU gap < 1.2 s while `l2` dumps 1000 entries | PASS since E1: 1.011-1.029 s (8x), 1.010-1.030 s (SWGT024) |
| BPDU hello within ±50 ms during an upload | PASS since E3: 1.966-2.036 s (8x), 1.968-2.034 s (SWGT024) (e1e3-merge.txt) |
| a pasted 500-character line arrives intact | schedtest: 501 of 501 (b4); the firmware: no overrun, the line cut at the editor's 127 as before (d1-uart-rings.txt) |
| no step > 20 ms except `FL_ERASE` | PASS in every scenario since E4 (chapter 9) |
| stack and internal RAM within the gate | PASS at every package |
| coverage of the new build ⊇ the old build's, by function, minus the functions deliberately replaced | not yet run: F1 |

F1 runs them all at once on the final branch (`evidence/f1-acceptance.txt`).

## The tools (`tools/`, each on one firmware image in rtlsim)

| Tool | What |
|---|---|
| `sched_tour.py` | the reference boot, CLI tour and web pages, then `health tasks`: every slot listed, no step over 50 ms (decision 25; 20 ms but `FL_ERASE` until F1b), the stack's headroom (D2) |
| `cli_steps.py` | each command's longest cli step between `health clear` and `health tasks`, console and web; fails a cli state over 5 ms (E1) |
| `console_bugs.py` | U2 and U4 (D3) |
| `webcmd_abort.py` | the web command in `config_buf`: W1-W6 (D3, E1) |
| `lldp_overrun.py` | LLDP frames at their longest; the VLAN names intact (D3) |
| `crash_foreign.py` | another image's crash record is ignored (E0) |
| `flash_job_steps.py` | the flash job: S0 boot count, S1-S4 uploads, config, factory reset, S5 an upload over the confirm, S6 a lost window update (E3) |
| `curl_upload.py` | uploads from Linux curl to a simulated switch on an OVS bridge; needs sudo (E3) |
| `hw_timeouts.py` | each bounded poll forced to time out (E4) |
| `boot_stuck.py` | a boot-time timeout doesn't spoil the boot's table work (E4) |
| (`vlan_eee.py`) | retired in P4-6: its `vlan` and `eee` commands are gone; ovs-rtlpg tests/oldnew compares /vlanlist.json and eee by value (E4) |
| `sfp_pull.py` | an SFP module pulled in the middle of its read (E2 review) |
| `sched_rates.py` | `health tasks`' rates and load (decision 26): the periodic states at their periods' rates, the load against rtlsim's busy cycles, idle and during `l2` with 1000 hosts (FR) |
| `isr_frame.py`, `resource_gate.py` | in every build: the ISR push counts, the gate |

## On silicon (the bench)

The orchestrator's alone. `schedtest/silicon.py` drives schedtest from the
console only (the firmware's `peek` instead of the simulator's), with the
same bounds (phase C); the firmware runs (DB, EB) use `health tasks` and
the bench host. Results: silicon.md. Still to come: LACP against a Linux
bond, the 4+2 (F2), a 24 h soak (F3).
