# 13. Simulator work (ovs-rtlpg, branch `feat/sched-sim`)

rtlsim already has: a MAME-derived DW8051 core with the chip's SFRs; the
bank checker (`--check-banks`: a fetch from the bank window must come from
the bank that owns the address), stack-overflow and xdata-before-write checks;
register, SFR, flash, I2C and NIC traces; `--flash-timing`; real-time pacing
for peers; OVS topologies and the `tests/net` harness. Firmware time is the
cycle count, 31.25 M machine cycles per firmware second (`CYCLES_PER_S`).
With no peer it runs unthrottled.

The scheduler needs four more things, all **P0**:

## A1. Control socket and test-driven clock

`rtlsim --control /path/sock ...` starts **paused**. The sim only advances
when told, and applies every input at an exact firmware time. Runs are
deterministic, so old and new builds see identical inputs at identical
firmware times. Without `--control`, every existing mode is unchanged.

**Protocol:** one text line per request, one reply line, or a counted block
for bulk replies. Over a unix socket.

| Request | Reply |
|---|---|
| `time` | `ok <cycles> <ms.µs>` |
| `run <ms>` | `ok <ms.µs>` after advancing |
| `run until pc=<sym or addr> [max <ms>]` | `ok hit <ms.µs>` or `ok timeout` |
| `run until uart=/<regex>/ [max <ms>]` | as above; matched against output since the last `uart read` |
| `run until <sym> <op> <value> [max <ms>]` | checked every N cycles (op: `== != < > & `) |
| `sym <name>` | `ok <space> <addr> <size>` from the `.map` (`--map FILE`) |
| `peek <space>:<addr or sym> <len>` | `ok <hex>`; space: `x` xdata, `i` idata, `s` SFR, `c` code (current bank), `b<n>:` bank n |
| `poke <space>:<addr> <hex>` | `ok` |
| `uart write <text or hex:...>` | `ok`; queued, fed one byte per character-time |
| `uart read` | counted block: each line stamped `<ms.µs> <text>` |
| `nic inject <port> <hex>` / `nic capture on|off|read` | frames, stamped |
| `link <port> up|down` | `ok` |
| `cov reset` / `cov dump <file>` | `ok` |
| `checks` | counted block: bank-checker, flash-busy, stack, unmodelled-register reports |
| `reset` / `powercut` | `ok` |
| `quit` | `ok`, exit |

A Python client, `tests/rtlsim_ctl.py`, with `sim.run_until_pc("sched_pass_end")`,
`sim.peek_sym("sched_task", 120)`, a symbol-aware struct decoder, and a
context manager that starts and stops rtlsim.

## A2. UART at the baud rate

- TX: after an SBUF write, TI sets one character-time later: 10 bit-times
  at the rate the firmware programmed (Timer 1 reload, `SMOD`).
- RX: `uart write` bytes enter one per character-time, so a firmware that
  doesn't drain its ring overflows as on silicon.
- Check: `bench` on the probe image reports ~87 µs per character, as on silicon.
- Boot transcripts are unchanged; the boot just takes more firmware time.

## A3. Flash busy

- With `--flash-timing` (on by default in control mode): sector erase
  40 ms, block erase per the datasheet, page program 1 ms. These are the
  silicon numbers to confirm in C.
- While the flash is busy, **reads from the bank window return 0xFF**.
- **Checker:** an instruction fetch or `movc` from 0x4000-0xffff while the
  flash is busy is a violation, reported with PC, PSBANK, SP and firmware
  time; `checks` lists them; the exit code says so.
- Test: a small SDCC program that jumps into a bank during an erase is
  flagged; the version that waits in the common area isn't.

## A4. Coverage

- A bitmap of executed instruction addresses: common area, plus each bank
  (bank × 48 KiB).
- `cov dump FILE` writes it; `tools/cov_report.py FILE --map M --rst DIR`
  maps it to functions (`.map`) and source lines (`.rst`), and diffs two
  dumps by function name.

## P1 (phase D0)

- NIC RX ring capacity and drops as on silicon (10 KiB ring).
- An LACP partner: a Linux bond on a tap, or two sims in a LAG.
- The old-vs-new harness (chapter 14).
