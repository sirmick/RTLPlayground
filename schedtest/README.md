# schedtest: the scheduler test image

doc/sched/testing.md, layer 2. The whole firmware is linked as usual, but
`SCHEDTEST` gives `rtlplayground.c` another `main()`. That `main()` sets up
the CPU clock, the 200 Hz tick, the UART and the flash controller, does **no
switch init**, and runs the scheduler over the dummy tasks here. The ISRs
are rtlplayground.c's own (O7): under `SCHEDTEST`, isr_timer2 carries the
software watchdog and isr_serial the console rings and the `^R ^R` crash
record. The resource numbers are therefore the real firmware's plus the
scheduler's.

## Build

    tools/docker-make.sh schedtest MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact LOADER=1 [ST_NEG=...]

- `make schedtest` runs the Makefile again with `SCHEDTEST=1`, which
  includes `schedtest/schedtest.mk`.
- Build directory: `output/<MACHINE>-compact-schedtest[-<ST_NEG>]`.
- Images: the normal set, with `schedtest[-<ST_NEG>]` before the version:
  `rtlplayground-{boot,install,loader,}-schedtest-v0.1.0-<git>[-dirty]-<MACHINE>-compact.bin`.
  - `<git>` is `git rev-parse --short HEAD`.
  - `-dirty` means tracked files differ from HEAD.
  - The version string printed at boot is the same (`schedtest v0.1.0-<git>`).
- Use `-boot` in rtlsim and on a programmer: the loader, then schedtest.
- The build prints the generated run sequence and the resource report
  (`tools/resource_report.py`).
- It checks that the ISR prologues push what `ST_ISR_*_PUSHES` say
  (`tools/isr_frame.py`): the crash record's PC depends on it.

`ST_NEG` builds the negative variants for B5:

| ST_NEG | Effect |
|---|---|
| `hang` | the `hang` command enters `SL_HANG`, which spins for ever (counting `st_hang_spins`). A jump-to-self would be taken by rtlsim as a halt. The watchdog resets after 5 s; the next boot prints `Last reset: watchdog in slow/SL_HANG ...` |
| `overrun` | `slow` runs once by itself, 2 s after boot: one 30 ms step, counted as an overrun of `SL_SLOW` |
| `bad_flash` | the `badflash` command starts the flasher with an erase that returns while the flash is busy. rtlsim's checker reports the next fetch from a bank |
| `bad_spec` | adds `bad_spec.def`, whose step is not `__banked`: the generator fails the build |

## The sim tests (B4)

    make schedtest-sim MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact [OVS=...] [SIM_ARGS=--twice]

`schedtest/sim_tests.py` boots the `-boot` image in a fresh rtlsim per test.
It drives it over the control socket (ovs-rtlpg `tests/rtlsim_ctl.py`, 120 s
socket timeout) in firmware time, so a run is deterministic; `--twice`
compares every measured number.

| Test | Checks |
|---|---|
| wake | sleep_a/b/c (5, 37, 1000 ms): never early; late by at most a pass (+ the tick) |
| waiter | post -> waiter within one pass; the 100 ms timeout path, never early |
| isr_ev | each UART byte -> ISR -> `EV_CON_RX` -> isr_ev, within 2 ms; the ring head seen moving |
| hot | worst gap <= spacing x (longest step + 60 us) |
| jobs | claims fill the 11 dynamic slots, then `SCHED_NONE`; every job ends; slots reused |
| printer | 3000 lines, all present in order; no printer or console step over 5 ms |
| flash | erase + program + verify ok; program/verify steps < 5 ms; erase within its 40 ms; the checker clean |
| slow | one overrun, state `SL_SLOW`, its length |
| paste | 500 characters + CR all taken, no RX overrun (deep off): quiet, and across a ~20 ms step (`slow 20`; ~230 B arrive meanwhile, the 256 B ring holds them). Across the 40 ms erase a paste may lose bytes (decision 19): recorded, not asserted |
| invariants | 30 pass ends: states valid, defined mask bits, wakes within the task's longest wait; `sched_posted` empty at the next pass's first step |
| stack | `deep 9` under console traffic both ways: headroom >= 30 B, the scanner agrees with the raw paint |
| soak | 600 s of firmware time (the tick wraps twice), stimuli every 10 s, sampled invariants; no early wake, no reset, no RX overrun, headroom >= 30 B, checks clean |

Every test also requires the simulator's checks to be clean
("unmodelled" register notes from the CPU setup excepted).

## Console

The prompt is `> ` (the firmware's).

| Command | Does |
|---|---|
| `health` | every slot (task, state, mask, wake), runs and longest step per state, passes, overruns, console RX overruns, the last reset |
| `health clear` | resets those counters |
| `status` | the dummies' counters |
| `print [N]` | the printer prints N lines (default 3000), `P<n> the quick brown fox jumps`, one per step, only when the TX ring has room; then `print done` |
| `jobs` | the jobber claims jobs until the table is full (`task_claim` returns `SCHED_NONE`); each runs 5 steps and ends |
| `slow [ms]` | one step of ms (default 30, up to 400) in `SL_SLOW`, timed on the fine clock |
| `hang`, `badflash` | the negative variants above; otherwise they say how to build them |
| `flash` | the flasher job: erase the 4 KiB sector at UPLOAD_START (0x40000), program it 128 B per step (~2.7 ms in rtlsim), verify it 256 B per step, then `flash: ok` or `flash: verify FAILED` (or `flash: no free slot`) |
| `deep [N]` | every 20 ms, a step runs a chain of N (default 9; 0 stops) banked calls through banks 1, 2, 3, 1, ..., and at the bottom prints a line and spins 5-10 ms, so the console ISRs land on the deepest stack. Paste input meanwhile for RX |
| `hot on`, `hot off` | the high-frequency task runs (yielding: the scheduler never idles) or waits; off at boot |
| `reset` | `crash_reset(CRASH_RESET_CMD)` |

Commands that print a lot (`health`, `status`) do it in one `CLI_RUN` step,
so they show up as `CLI_RUN` overruns: the console takes 87 µs a byte.

## Tasks and where they live

| Task | Bank | freq | What |
|---|---|---|---|
| `con` | st_main.c (placed) | high | line editor: drains the RX ring, echoes, hands the line to cli (`EV_LINE`) |
| `cli` | st_main.c | once | `CLI_IDLE` / `CLI_RUN` / `CLI_PROMPT` |
| `sleep_a`, `waiter`, `hot`, `printer` | st_b1.c, BANK1 | once, once, **high**, once | 5 ms sleep; waits `EV_ST_POKE` with a 100 ms timeout; ~1 µs per run; the printer |
| `sleep_b`, `poster`, `jobber` + job `jb`, job `fl` | st_b2.c, BANK2 | once | 37 ms sleep; posts `EV_ST_POKE` every 300 ms; the jobber; the flasher |
| `sleep_c`, `isr_ev`, `deep`, `slow` | st_b3.c, BANK3 | once | 1000 ms sleep (and the stack rescan); woken by `EV_CON_RX` (UART ISR -> event); the deep call chain; slow/hang |

12 fixed slots, 12 dynamic. The generated sequence and its spacing are in
`<builddir>/sched/sequence.txt`.

## Symbols for the control client

All are xdata, little-endian, read by name from the `.map`.

`struct st_log` (schedtest.h) is:
- `n`: u16, entries so far;
- `gap_min`, `gap_max`: u16, in fine units, saturating at 0xffff;
- `tick[64]`, `cnt[64]`: the time of each entry;
- `aux[64]`: a per-log value.

Entry i is at index i % 64. Time is (`tick`, `cnt`): the 16-bit 200 Hz tick,
then fine units of 6.144 µs into it, with `sched_fine_per_tick` (813) units
per tick.

| Symbol | What |
|---|---|
| `st_log_sleep_a`, `_sleep_b`, `_sleep_c` | each run of the sleepers (5, 37, 1000 ms) |
| `st_log_waiter` | each wake; aux 1 = by `EV_ST_POKE`, 2 = timeout |
| `st_log_poster` | each post |
| `st_log_isr_ev` | each wake by `EV_CON_RX`; aux = `con_rx_head` |
| `st_log_hot` | each run of the high-frequency task; aux = `sched_passes_lo` |
| `st_print_done` | lines printed by the last `print` |
| `st_jobs_claimed`, `st_jobs_full`, `st_jobs_done` | u16 counters |
| `st_fl_state`, `st_fl_bad` | flasher: u8 0 never, 1 running, 2 ok, 3 verify failed, 4 no slot; u16 bytes that read back wrong |
| `st_slow_runs`, `st_hang_spins` | u16 |
| `st_con_rx_n`, `st_con_rx_sum` | console bytes the con task took, and their 16-bit sum |
| `st_sp_step_max` | u8, the highest SP at a step's entry (sampled by `hot`) |
| `st_stack_paint_from`, `st_stack_hw` | u8: idata from `paint_from` to 0xff is painted 0xa5 at boot; `st_stack_hw` is the highest address found changed since, rescanned by `sleep_c` each second (0: none). Peak stack use = `st_stack_hw` - 0x7c. The raw paint is readable too (`i:0x80`...) |
| `st_deep_depth`, `st_deep_sp`, `st_deep_runs` | `deep`'s depth, the highest SP at the bottom of the chain, and its runs |
| `sched_task[24]` | `{u8 state, u16 mask, u16 wake, u16 arg}`, 7 B each |
| `sched_st_max[]`, `sched_st_runs[]` | u16 per state ID |
| `sched_overruns`, `sched_overrun_state`, `sched_pass_max`, `sched_passes_lo/hi`, `sched_idle_lo/hi` | |
| `sched_posted`, `sched_ev`, `sched_now`, `sched_cur` | |
| `con_rx_overruns` | u16 |
| `crash_rec` (0xbfe0), `crash_last` | `struct crash_rec` (crash.h) |

## Stack

Scheduler cost over today's direct call: ~9-10 B per step (memory.md). B4
measures it with `st_stack_hw` (all paths, ISRs included) and `deep`.

- The `.mem` gives 131 B of stack, starting at 0x7d.
- At a step's entry SP is 0x8a (`st_sp_step_max` in the sim), so 14 B are in
  use: main -> schedtest_main -> sched_run (banked calls) -> sched_pass ->
  the dispatch.
- 3 of those 14 B are r5-r7, which the dispatcher keeps pushed for the
  length of each step (B2 evidence).
- That leaves 117 B above a step's entry. The deepest ISR frame is 10 B
  (isr_timer2's 8 pushes plus the PC), so a step and everything it calls
  have ~107 B.
