# 7. Console and CLI

## UART rings (phase D1, can land before the scheduler)

| Ring | Size | Filled by | Drained by |
|---|---|---|---|
| RX | 256 B (from 16), 8-bit indexes that wrap by themselves (decision 19) | `isr_serial` | the `con` task |
| TX | 2 KiB, 16-bit indexes (decision 21; 128 B until E1) | `write_char` | `isr_serial` on TI |

- **RX:** the ISR stores, or counts an overrun when the ring is full (shown
  in `health`). `EV_CON_RX` is sampled (the ring not empty), so no new ISR
  bit. `^R ^R` stays in the ISR, as today, and records the reset
  (chapter 11).
- The ISR side is two macros, `CON_ISR_RX(c)` and `CON_ISR_TX()`
  (`con_ring.h`); with them the serial ISR pushes 8 bytes (6 before D1; 7
  with the 128 B TX ring's 8-bit indexes; the RX ring's 8-bit indexes wrap
  by themselves).
- **TX indexes are 16 bits (E1),** so neither side may read the other's
  half-written: the head is written only by `con_putc()`, with ES off, and
  read by the ISR; the tail is written only by the ISR (and by `con_putc()`
  starting an idle transmitter, ES off), and read in `con_putc()` and
  `con_tx_room()` with ES off. Host tests: wrap and full at 16 bits
  (`test_sched`).
- **TX:** `write_char` puts a byte in the ring and, if the transmitter is
  idle, starts it. **If the ring is full it busy-waits** for room, as today's
  code waits for every byte. With 2 KiB a command's output (the tour's
  largest unchunked one, `vlan show`, 465 B) goes in without waiting: its
  step costs its work, not the UART (87 µs a byte at 115200).
- `cmd_capture` (web commands) and syslog stay in `write_char`, unchanged.
- **Before a reset:** `reset_chip()` first waits with `con_drain()` until the
  TX ring is empty and its last byte is out, so what was printed just
  before (`reset`, `factory-reset`, the button, "Resetting to apply
  update") isn't lost. The `^R ^R` and watchdog paths reset at once. With
  2 KiB queued the drain can take ~180 ms (2048 × 87 µs).
- `EV_CON_TX` = "room for a full line" (≥ 100 free bytes), sampled at
  pass start.

## The `con` task (editor)

Fixed, frequency **high**: at 115200 a byte arrives every 87 µs, so the
256 B RX ring fills in ~22 ms. One step of up to the 20 ms limit between two
runs of `con` loses nothing (schedtest: a 500-character paste across a 20 ms
step); a ~40 ms flash erase across a fast paste can (decision 19). High
frequency keeps `con` within a few steps of every byte.

| State | Does | Next |
|---|---|---|
| `CON_EDIT` | drains the RX ring into the line editor (today's `cmd_edit`, on its own `con_line`), echoes. On Enter: sets `con_line_ready`, posts `EV_LINE`, and reads nothing more until cli has taken the line (U2) | `task_wait(EV_CON_RX, FOREVER)` |

As built (D3), `con` waits on `EV_CON_RX` only: the echo goes through
`write_char`, which busy-waits while the TX ring is full, as every other
print does. The RX ring buffers meanwhile.

## The `cli` task

Fixed, frequency **once**. One command at a time, from either source.

| State | Does | Next |
|---|---|---|
| `CLI_IDLE` | waits for a console line or a queued web command | `task_wait(EV_LINE | EV_WEBCMD, FOREVER)` |
| `CLI_RUN` | copies the line into `cmd_buffer`, tokenizes, dispatches. A short command runs to completion here. A long one sets up its job state and continues in it | `CLI_PROMPT` or the command's own state |
| `CLI_PROMPT` | prints the prompt (console source), or posts `EV_CMD_DONE` (web source) | `CLI_IDLE` |

**Two input buffers, one parser.** The console editor gets its own line
buffer, and httpd a separate web command buffer. `CLI_RUN` copies the source
into `cmd_buffer` and runs the parser on it. This fixes today's bug where a
web `/cmd` overwrites a half-typed console line.

As built (D3): the console line is `con_line`, copied into `cmd_buffer` in
`CLI_RUN`; the prompt is printed only in `CLI_PROMPT`, which then clears
`con_line_ready`, so type-ahead echoes after the prompt. The web command
buffer is **`config_buf`**, the buffer httpd already holds each request in
(no new bytes); `exec_line()` copies it a line at a time into
`cmd_buffer`, and the reply is captured into `outbuf`. That is safe because:

- uIP has **one** TCP connection (`UIP_CONF_MAX_CONNECTIONS 1`,
  `uip/uip-conf.h`; `health`'s tcp dump `#error`s otherwise). While the
  `/cmd` connection waits in `TSTATE_WEBCMD` no other request can arrive,
  so nothing else writes `config_buf` or `outbuf`.
- No command touches `config_buf` (only `httpd.c` uses it; the config
  upload is a request of its own, POST `/config`).
- If the `/cmd`'s connection ends first (client reset, abort, timeout),
  httpd clears `webcmd_pending` and `webcmd_done` before the next connection
  can exist. `CLI_RUN` re-checks `webcmd_pending` right before parsing, and
  `CLI_PROMPT` posts `EV_CMD_DONE` only if `webcmd_done`: a cancelled
  command runs nothing, and the next request in `config_buf` is never run
  as commands (`tools/webcmd_abort.py`). If a new `/cmd` was queued in
  between, that one runs: it is the one in `config_buf`, and its connection
  is the one waiting.
- Data that arrives on the `/cmd`'s own connection while it waits (a
  trailing CRLF, a pipelined request) is dropped in `TSTATE_WEBCMD`, as
  today's code drops data that arrives while a reply is sent; taking it
  would copy it over the command (`tools/webcmd_abort.py` W3, W4).
- `httpd.c` `#error`s if `UIP_CONNS != 1`. With more connections the web
  command and its reply need buffers of their own (the body limit is
  `CONFIG_UPLOAD_BUF`, 4480 B; the reply up to `TCP_OUTBUF_SIZE`, 2500 B).

## Chunked output

Measured (E1, `tools/cli_steps.py`, 2026-11-19): with the 128 B ring every
command that printed more than ~185 B was over 5 ms, `version` included,
and almost all of it was the wait for the UART. Without the wait (a test
build whose `con_putc` dropped bytes) only `history` (8.9 ms) and
`health tasks` (9.2 ms) were over, and `l2` grows 3.1 ms a MAC. So the TX
ring grew to 2 KiB (decision 21), and only dumps that grow with a table, or
whose work passes 5 ms, are chunked: a cli **job**.

- `cli_job` (cmd_parser.c) names the job; its cursor is the module's own
  xdata (the job's line function reads it). The command prints its header
  and sets `cli_job`; `cli_job_more()` prints the next line and clears
  `cli_job` after the last.
- Jobs: `l2` (a table search a step), `stat` and `lacp show` (a port, a
  LAG a step: their work plus a web reply's capture passed 5 ms), `history`
  (a line, at most 64 bytes looked at), `health tasks` (a slot, a state, the
  totals), and `stp off`'s BPDU entries (a walk of the 4094 VLAN entries,
  256 a step; it prints nothing). evidence/e1-chunked-cli.txt.
- The cli task: `CLI_RUN` parses; with a job set it goes to `CLI_MORE`,
  which waits for `EV_CON_TX` (room for a line), runs one line, and stays
  until the job ends, then `CLI_PROMPT`.
- Any other caller (the config at boot, the boot's `stat`) runs the job
  to its end at once (`cmd_finish()`), as before.
- `history`'s own line goes into the history after its job, as before
  (`cmd_save()` deferred), so the dump never shows itself.
- A command starts only with 1 KiB of the ring free (`CLI_ROOM`), more than
  any unchunked command prints, so a paste or a multi-line `/cmd` doesn't
  wait on the UART mid-step.
- **Web commands** run a line a step (`CLI_NEXT`: `exec_line()`, the old
  old `execute_commands()` split at each command; the same stop rules), and a
  job's lines in `CLI_MORE`, into the capture buffer. The capture is on only
  during cli's own steps (`WEBCMD_CAPTURE_ON/OFF`): another task's print
  between two steps isn't in the reply. The reply is completed in
  `CLI_PROMPT` (`httpd_webcmd_end`). If the connection ends meanwhile, cli
  drops the command at its next step: it checks at every step that its
  command's connection is still the one waiting (`webcmd_busy`, and
  `webcmd_gen`, which each queued `/cmd` bumps: a new `/cmd` after the old
  connection ended holds `config_buf` now). A dropped command parses no
  more lines and sends no reply; a pure dump stops, a job with side effects
  (`CLI_JOB_KEEPS`: stp off's entries) runs to its end, its output not
  captured. Then cli serves the queued one.

## Rules

- A job prints one line a step, after checking room for it; a command
  starts with `CLI_ROOM` free.
- The prompt is only printed by `CLI_PROMPT`, so asynchronous messages (link
  changes, STP events) can't land in the middle of a command's output.
