# 7. Console and CLI

Code: `con_ring.h`, `con_ring.c` (resident), the serial ISR in
`rtlplayground.c`, the editor `cmd_editor.c`, the `con` and `cli` tasks in
`tasks.c`, the cli jobs in `cmd_parser.c`, the web side in `httpd/httpd.c`.

## UART rings

| Ring | Size | Filled by | Drained by |
|---|---|---|---|
| RX | 256 B (was 16), 8-bit indexes that wrap by themselves (decision 19) | `isr_serial` | the `con` task |
| TX | 2 KiB, 16-bit indexes (decision 21) | `con_putc()`, under `write_char` | `isr_serial` on TI |

- **RX:** the ISR stores each byte, or counts an overrun when the ring is
  full (`con_rx_overruns`, shown in `health tasks`). `HW_CON_RX` is sampled
  (the ring not empty), so there is no new ISR bit. `^R ^R` stays in the
  ISR and records the reset (chapter 11).
- The ISR side is two macros, `CON_ISR_RX(c)` and `CON_ISR_TX()`
  (`con_ring.h`): a call in an ISR would make SDCC save every register on
  each entry. With them `isr_serial` pushes 8 bytes (`ISR_SERIAL_PUSHES`,
  checked by `tools/isr_frame.py` at every build).
- **The TX indexes are 16 bits,** so neither side may read the other's
  half-written. The head is written only by `con_putc()`, with ES off, and
  read by the ISR. The tail is written only by the ISR (and by `con_putc()`
  when it starts an idle transmitter, in the same ES-off section), and read
  by `con_putc()` and `con_tx_room()` with ES off. Host tests: wrap and full
  at 16 bits (`test_sched`).
- **TX:** `con_putc()` puts a byte in the ring and, if the transmitter is
  idle, starts it. If the ring is full it waits for room. With EA or ES off
  the ISR can't drain it, so the wait polls TI and sends the next byte
  itself: it never hangs, and it leaves ES as it found it. The wait doesn't
  kick the watchdog (chapter 11).
- With 2 KiB most commands print without waiting: a step costs its work,
  not the UART's 87 µs a byte. The largest unchunked output found, `vlan
  show` on the 8x with every port up and named, is 465 B (e1-chunked-cli.txt).
- `cmd_capture` (web commands) and syslog stay in `write_char`, unchanged.
- **Before a reset:** `reset_chip()` first waits with `con_drain()` until the
  TX ring is empty and its last byte is out, so what was printed just
  before (`reset`, `factory-reset`, the button, "Resetting to apply
  update") isn't lost. `^R ^R` and the watchdog reset at once. A full ring
  takes 2048 × 87 µs, ~180 ms, to drain.
- `HW_CON_TX` means room for a line: 100 free bytes (`CON_TX_LINE`).

## The `con` task (editor)

Fixed, frequency **high**: at 115200 a byte arrives every 87 µs, so the
256 B ring fills in ~22 ms, and con is never more than a few steps from its
next run. A step of up to the 20 ms limit loses nothing (schedtest: 501 of
501 pasted bytes across a 20.1 ms step); a 40 ms erase across a fast paste
can (294 of 501; decision 19; b4-sim-tests.txt).

| State | Does | Next |
|---|---|---|
| `CON_EDIT` | `cmd_edit()`: drains the RX ring into the line editor's own line, `con_line`, and echoes. On Enter it sets `con_line_ready` (an empty line too) and reads nothing more until cli has taken the line (U2). The step wakes cli if a line is ready | `STEP_WAIT(S_CON_EDIT, HW_CON_RX, SCHED_FOREVER)` |

The echo goes through `write_char`, which waits while the TX ring is full,
as every print does; the RX ring buffers meanwhile. A pasted line longer
than the editor's 127 characters is cut there, as before.

## The `cli` task

Fixed, frequency **once**. One command at a time, from the console or the
web (decision 10).

| State | Does | Next |
|---|---|---|
| `CLI_IDLE` | woken by con or rx (`task_wake(T_CLI)`), takes a console line first (`con_line_ready`), then a queued web command (`webcmd_pending`) | `CLI_RUN`; else `STEP_SLEEP(S_CLI_IDLE, SCHED_FOREVER)` |
| `CLI_RUN` | console: waits for `CLI_ROOM` (1 KiB) free in the TX ring, copies `con_line` into `cmd_buffer`, tokenizes and parses: a short command runs to completion here. Web: re-checks `webcmd_pending`, records `webcmd_gen`, begins the reply (`httpd_webcmd_begin()`) | `CLI_MORE` if the command set a cli job, `CLI_NEXT` (web), else `CLI_PROMPT` |
| `CLI_NEXT` | web: one line of the `/cmd` body a step (`exec_line()`), capture on, after `CLI_ROOM` free (the reply is mirrored on the console) | `CLI_NEXT`, `CLI_MORE` for a job, `CLI_PROMPT` after the last line |
| `CLI_MORE` | a cli job's next line (`cli_job_more()`), after room for a line (`HW_CON_TX`); an `eee` job's PHY reset sleeps `CLI_JOB_SLEEP_MS` (10 ms) here | `CLI_MORE` until the job ends; then `CLI_NEXT` (web, more lines) or `CLI_PROMPT` |
| `CLI_PROMPT` | console: prints the prompt, then clears `con_line_ready`, so type-ahead echoes after the prompt. Web: ends the reply (`httpd_webcmd_end()`; httpd sends it on its next poll), if its connection still waits. A factory reset in progress: no prompt, the job resets | `CLI_IDLE` |

The prompt is printed only in `CLI_PROMPT`, so asynchronous lines (link
changes, STP events) can't land inside a command's output.

## Two input buffers, one parser

The console editor has its own line, `con_line`; a web command waits in
httpd's request buffer, `config_buf`. `CLI_RUN` (console) and `exec_line()`
(web, a line at a time) copy the source into `cmd_buffer`, and the one
parser runs on it. That fixed the old bug where a web `/cmd` overwrote a
half-typed console line (U4). The web reply is captured into `outbuf`.

The web command takes no buffer of its own (0 B), and that is safe because:

- uIP has **one** TCP connection (`UIP_CONF_MAX_CONNECTIONS 1`,
  `uip/uip-conf.h`). While the `/cmd` connection waits in `TSTATE_WEBCMD`
  no other request can arrive, so nothing else writes `config_buf` or
  `outbuf`. `httpd.c` `#error`s if `UIP_CONNS != 1`: with more connections
  the command and its reply need their own buffers (the body up to
  `CONFIG_UPLOAD_BUF`, 4480 B; the reply up to `TCP_OUTBUF_SIZE`, 2500 B).
  A version with its own buffers (+6983 B of xdata) was built and dropped
  (d3-console-net.txt).
- No command touches `config_buf` (only `httpd.c` uses it; a config upload
  is a request of its own, POST `/config`).
- If the `/cmd`'s connection ends first (client reset, abort, timeout),
  httpd clears `webcmd_pending`, `webcmd_done` and `webcmd_busy` before the
  next connection can exist. `CLI_RUN` re-checks `webcmd_pending` right
  before it starts, so a cancelled command runs nothing, and the next
  request in `config_buf` is never run as commands.
- A `/cmd` spans several cli steps, so each step checks that its command's
  connection is still the one waiting (`cli_web_live()`: `webcmd_busy`, and
  `webcmd_gen`, which each queued `/cmd` bumps). If not, the command is
  dropped: no more of its lines, no reply. A pure dump stops; a job with
  side effects (`CLI_JOB_KEEPS`: stp off's BPDU entries, `eee`) runs to its
  end, its output not captured. Then cli serves the queued one.
- Data that arrives on the `/cmd`'s own connection while it waits (a
  trailing CRLF, a pipelined request) is dropped in `TSTATE_WEBCMD`, as the
  old code dropped data that arrived while a reply was sent.

`tools/webcmd_abort.py` W1-W6 test each of these (d3-console-net.txt,
e1-chunked-cli.txt).

## Cli jobs: the long commands, a line a step

Measured (E1, `tools/cli_steps.py`): with the old 128 B TX ring every
command that printed more than ~185 B was over 5 ms, `version` included,
and almost all of it was the wait for the UART. Without the wait only
`history` (8.9 ms) and `health tasks` (9.2 ms) were over, and `l2` grows
3.1 ms a MAC. So the ring grew to 2 KiB (decision 21), and only dumps that
grow with a table, or whose work passes 5 ms, run as **cli jobs**.

- `cli_job` (`cmd_parser.c`) names the job; its cursor is the module's own
  xdata. The command prints its header and sets `cli_job`;
  `cli_job_more()` does the next line and clears `cli_job` after the last.
- The jobs: `l2` (a table search a step), `stat` and `lacp show` (a port or
  a LAG a step: their work plus a web reply's capture passed 5 ms),
  `history` (a line, at most 64 bytes looked at), `health tasks` (a slot, a
  state, the totals), stp off's and the BPDU commands' entries (a walk of
  the 4094 VLAN entries, 256 a step; it prints nothing), and `eee on|off`
  (a port a step, its PHY reset in two steps with the cli asleep between).
  The last two keep their side effects if the web drops them
  (`CLI_JOB_KEEPS`).
- Any other caller (the config at boot, the boot's `stat`) runs the job to
  its end at once (`cmd_finish()`).
- `history`'s own line goes into the history after its job, as before, so
  the dump never shows itself.
- A command starts only with 1 KiB of the ring free (`CLI_ROOM`), more than
  any unchunked command prints, so a paste or a multi-line `/cmd` doesn't
  wait on the UART mid-step. A job's line needs 100 B (`HW_CON_TX`).
- Web: a job's lines run in `CLI_MORE` into the capture. The capture is on
  only during cli's own steps (`WEBCMD_CAPTURE_ON/OFF`), so another task's
  print between two steps isn't in the reply. The reply stops at the
  2500 B capture with its marker, as before.

Result: no cli state over 5 ms in the CLI tour, on the console and the web,
both boards (e1-chunked-cli.txt, e4-polls-vlan-eee.txt); on the 8x, `l2` of
1000 MACs as 1966 steps, the longest 429 µs (silicon.md, EB).

## Rules

- A job prints at most a line a step, after checking room for it; a command
  starts with `CLI_ROOM` free.
- The prompt is only printed by `CLI_PROMPT`.
