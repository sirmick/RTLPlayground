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
| `hang` | the `hang` command enters `SL_HANG`, which spins for ever (counting `st_hang_spins`). A jump-to-self would be taken by rtlsim as a halt. The watchdog resets after 5 s; the next boot prints `Last reset: watchdog in sl/SL_HANG ...` |
| `overrun` | `sl` runs once by itself, 2 s after boot: one 25 ms step, counted as an overrun of `SL_SLOW` |
| `bad_flash` | the `badflash` command starts the flasher with an erase that returns while the flash is busy. rtlsim's checker reports the next fetch from a bank |
| `bad_spec` | adds `bad_spec.c` to the sweep: its state `BAD_LOST` is named by no step, so the generator fails the build |
| `fall_through` | adds `fall_through.c`, whose step can fall off its end: SDCC's warning 59, an error for a step source, fails the build (decision 22) |

## The sim tests (B4)

    make schedtest-sim MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact [OVS=...] [SIM_ARGS=--twice]

runs these (layer 2), then the negative tests (layer 2b, below);
`make schedtest-neg` runs the negative tests alone.

`schedtest/sim_tests.py` boots the `-boot` image in a fresh rtlsim per test.
It drives it over the control socket (ovs-rtlpg `tests/rtlsim_ctl.py`, 120 s
socket timeout) in firmware time, so a run is deterministic; `--twice`
compares every measured number.

| Test | Checks |
|---|---|
| wake | sleep_a/b/c (5, 37, 1000 ms): never early; late by at most a pass (+ the tick) |
| waiter | post -> waiter within one pass; the 100 ms timeout path, never early |
| isr_ev | each UART byte -> ISR -> `HW_CON_RX` -> isr_ev, within 2 ms; the ring head seen moving |
| hot | worst gap <= spacing x (longest step + 60 us) |
| jobs | claims fill the 11 dynamic slots, then `SCHED_NONE`; every job ends; slots reused |
| printer | 3000 lines, all present in order; no printer or console step over 5 ms |
| flash | erase + program + verify ok; program/verify steps < 5 ms; erase within its 40 ms; the checker clean |
| slow | one overrun, state `SL_SLOW`, its length |
| paste | 500 characters + CR all taken, no RX overrun (deep off): quiet, and across a ~20 ms step (`slow 20`; ~230 B arrive meanwhile, the 256 B ring holds them). Across the 40 ms erase a paste may lose bytes (decision 19): recorded, not asserted |
| invariants | 30 pass ends: states valid, defined mask bits, wakes within the task's longest wait |
| stack | `deep 9` under console traffic both ways: headroom >= 30 B, the scanner agrees with the raw paint |
| horizon | 170 s with no input: con's wait without timeout reaches its horizon (~164 s) and is pushed on, without running |
| soak | 600 s of firmware time (the tick wraps twice), stimuli every 10 s, sampled invariants; no early wake, no reset, no RX overrun, headroom >= 30 B, checks clean |
| probes | silicon.py's commands: `peek` prints what the simulator reads; `flashtime` verifies and times (erase ~40 ms); `erasepeek`'s busy row is rtlsim's 0x00, its idle row the bank's code, and its reads are the one checker finding (flash-busy, a movc in `flash_erase_peek`): rtlsim exits 6 |

Every test also requires the simulator's checks to be clean
("unmodelled" register notes from the CPU setup excepted), and dumps its
coverage. After a full run, the dumps are ORed together and
ovs-rtlpg's `tools/cov_report.py` (region `loaded`, the application)
requires every state's step function (the generated list) and every line of
`sched.c` to have run. sched_bank.c, crash.c and con_ring.c are reported,
not required: crash.c runs only when the image resets (required by the
negative tests), and sched_bank.c
has two lines nothing here reaches (a step over 400 ms, `task_sleep(0)`).

The poster keeps its 300 ms beat with the low-level `task_until`, so the
API's every call runs; `hang` in a build without `ST_NEG=hang` enters
`SL_HANG`, which only says so.

## The negative tests (B5)

    make schedtest-neg MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact [OVS=...] [NEG_ARGS=--twice]

`schedtest/sim_neg.py` needs the `hang`, `overrun` and `bad_flash` builds
(one at a time: they share the tree). Each variant must fail exactly as
intended:

| Test | Variant | Checks |
|---|---|---|
| hang | hang | `hang`, twice: the watchdog counts 1000 ticks from the kick at its pass's start, so it fires at the tick that ends the count, 4990-5000 ms after SL_HANG began (`swdt_cnt` at the step's entry says which); after the reset, through the loader, the record at 0xbfe0 is valid (magic, sum): cause watchdog, slot sl, state SL_HANG, age = that count, PC inside `sl_hang` and its bank, SP on the stack, resets 0 then 1; the boot prints the `Last reset:` line; the record is cleared, kept in `crash_last` (`health`), `crash_prior` counts |
| ctrl_r | hang | `^R ^R`: cause ^R ^R, the interrupted PC named from the .map; then `reset`: cause reset command, PC 0, cli/CLI_RUN, resets 1; both printed and cleared |
| powercut | hang | a valid record in xdata, then a power cut instead of a reset: the next boot finds none, prints none |
| overrun | overrun | one 25 ms step: `sched_overruns` 1, `sched_overrun_state` SL_SLOW; `health` says `overruns 1 last SL_SLOW` and lists SL_SLOW at ~25 ms |
| bad_flash | bad_flash | `badflash`: exactly one check, flash-busy, a fetch from a bank in `fl_erase` with PC/PSBANK/SP/time; rtlsim exits 6 |
| bad_spec | - | make's generator rule with `ST_NEG=bad_spec` exits 2 with `sched_gen: error: schedtest/bad_spec.c:10: state BAD_LOST is reached by no return/STEP_*/task_claim ...` |
| fall_through | - | make of `schedtest/fall_through.rel` with `ST_NEG=fall_through` exits 2 with `schedtest/fall_through.c:12: error 59: function 'ft_step' must return value` |

Over the hang variant's tests every line of crash.c must run.

## On silicon (C)

The build writes `silicon.py` into its directory: `schedtest/silicon.py`
with the build's constants embedded (the .map's addresses, the states and
slots, `sl_hang`'s range, the bank window's bytes, the version). Copy it to
the bench host; it needs Python 3's stdlib only and opens the port raw at
115200.

    python3 silicon.py [--port /dev/ttyUSB0] [--only TEST...] [--json F] [--log F]
    python3 silicon.py report [--port ...]
    python3 silicon.py --sim          # the same through a pty into rtlsim

The base image's copy runs layer 2's scenario from the console alone: wake,
waiter, isr_ev (runs and ring head; the latency needs the sim's clock), hot,
jobs, printer (the 3000 lines checked on the host), flash, slow 30 and 20,
paste (500 characters at 115200, quiet and across `slow 20`), stack
(`deep 9`), the report, flashtime, erasepeek. It reads the firmware's state
with `peek` snapshots and decodes them as sim_tests.py does, against the
same bounds with silicon's own pass length. The hang image's copy runs
`hang` twice, `health`, `^R ^R` and `reset`, and checks the `Last reset:`
lines (cause, slot/state, 4995-5005 ms, PC inside `sl_hang`). The image's
version must match the copy's. `--sim` runs either against rtlsim, paced
to the wall clock.

## Console

The prompt is `> ` (the firmware's).

| Command | Does |
|---|---|
| `health` | every slot (task, state, mask, wake), runs and longest step per state, passes, overruns, console RX overruns, the last reset |
| `health clear` | resets those counters |
| `peek A N [A N..]` | N bytes of xdata from A (decimal) in hex, `x <hex>`, a line per pair; all of a line's pairs in one step (a snapshot), 64 bytes at a time with the interrupts off. For silicon.py |
| `clear` | `health clear`, and the timestamp logs start again (but the poster's) |
| `flashtime` | erase, 4-byte and 256-byte program in the upload area's second sector, timed on the fine clock into `st_ft`, read back into `st_ft_bad`; then `flashtime done` |
| `erasepeek` | erases the upload area's third sector and reads 64 bytes spread over the bank window **while it is busy**, from the common area (`flash_erase_peek`, the one sanctioned read), then the same once idle, into `flash_peek`; then `erasepeek done` |
| `print [N]` | the printer prints N lines (default 3000), `P<n> the quick brown fox jumps`, one per step, only when the TX ring has room; then `print done` |
| `jobs` | the jobber claims jobs until the table is full (`task_claim` returns `SCHED_NONE`); each runs 5 steps and ends |
| `slow [ms]` | one step of ms (default 30, up to 400) in `SL_SLOW`, timed on the fine clock |
| `hang`, `badflash` | the negative variants above; otherwise they say how to build them |
| `flash` | the flasher job: erase the 4 KiB sector at UPLOAD_START (0x40000), program it 128 B per step (~2.7 ms in rtlsim), verify it 256 B per step, then `flash: ok` or `flash: verify FAILED` (or `flash: no free slot`) |
| `deep [N]` | every 20 ms, a step runs a chain of N (default 9; 0 stops) banked calls through banks 1, 2, 3, 1, ..., and at the bottom prints a line and spins 5-10 ms, so the console ISRs land on the deepest stack. Paste input meanwhile for RX |
| `hot on`, `hot off` | the high-frequency task runs (yielding: the scheduler never idles) or waits; off at boot |
| `reset` | `crash_reset(CRASH_RESET_CMD)` |
| `^R ^R` | `CRASH_FROM_ISR(CRASH_CTRL_R)`, once the scheduler runs. One typed during the boot (`firmware_init()`, before `schedtest_main()` sets `serial_reset_ok`) is dropped: unlike the firmware's main loop, schedtest doesn't act on `serial_reset_pending` |
| anything else | the usage line |

Commands that print a lot (`health`, a long `peek`) do it in one `CLI_RUN` step,
so they show up as `CLI_RUN` overruns: the console takes 87 µs a byte.

## Tasks and where they live

| Task | Bank | freq | What |
|---|---|---|---|
| `con` | st_main.c (placed) | high | line editor: drains the RX ring, echoes, hands the line to cli (`task_wake(T_CLI)`) |
| `cli` | st_main.c | once | `CLI_IDLE` / `CLI_RUN` / `CLI_PROMPT` |
| `sleep_a`, `waiter`, `hot`, `pr` (the printer) | st_b1.c, BANK1 | once, once, **high**, once | 5 ms sleep; woken by the poster (`st_poked`), 100 ms timeout; ~1 µs per run; the printer |
| `sleep_b`, `poster`, `jobber` + job `jb`, job `fl` | st_b2.c, BANK2 | once | 37 ms sleep; wakes the waiter every 300 ms; the jobber; the flasher |
| `sleep_c`, `isr_ev`, `deep`, `sl` (slow) | st_b3.c, BANK3 | once | 1000 ms sleep (and the stack rescan); woken by `HW_CON_RX` (UART ISR -> event); the deep call chain; slow/hang |

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
| `st_log_waiter` | each wake; aux 1 = by the poster, 2 = timeout |
| `st_log_poster` | each post |
| `st_log_isr_ev` | each wake by `HW_CON_RX`; aux = `con_rx_head` |
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
| `sched_ev`, `sched_now`, `sched_cur` | |
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
