# 7. Console and CLI

## UART rings (phase D1, can land before the scheduler)

| Ring | Size | Filled by | Drained by |
|---|---|---|---|
| RX | 256 B (from 16), 8-bit indexes that wrap by themselves (decision 19) | `isr_serial` | the `con` task |
| TX | 128 B (one long line plus room) | `write_char` | `isr_serial` on TI |

- **RX:** the ISR stores, or counts an overrun when the ring is full (shown
  in `health`). `EV_CON_RX` is sampled (the ring not empty), so no new ISR
  bit. `^R ^R` stays in the ISR, as today, and records the reset
  (chapter 11).
- The ISR side is two macros, `CON_ISR_RX(c)` and `CON_ISR_TX()`
  (`con_ring.h`); with them the serial ISR pushes 7 bytes instead of 6 (8-bit
  RX indexes: the 256 B ring wraps by itself).
- **TX:** `write_char` puts a byte in the ring and, if the transmitter is
  idle, starts it. **If the ring is full it busy-waits** for room, as today's
  code waits for every byte. Old code that prints a lot keeps working at
  today's cost; converting it is an improvement, not a requirement.
- `cmd_capture` (web commands) and syslog stay in `write_char`, unchanged.
- **Before a reset:** `reset_chip()` first waits with `con_drain()` until the
  TX ring is empty and its last byte is out, so what was printed just
  before (`reset`, `factory-reset`, the button, "Resetting to apply
  update") isn't lost. The `^R ^R` and watchdog paths reset at once.
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
(no new bytes); `execute_commands()` copies it a line at a time into
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

A long command is states owned by the cli task, with its cursor (the "next
param") in the slot's `arg`:

```c
void l2_first(void) __banked
{
	STEP_ARG(S_L2_MORE, 0);              /* from the first entry */
}

void l2_more(void) __banked
{
	if (!con_tx_room_line())
		STEP_WAIT(S_L2_MORE, EV_CON_TX, SCHED_FOREVER);
	if (!l2_print_entry(task_arg()))     /* one line */
		STEP_NEXT(S_CLI_PROMPT);
	task_set_arg(task_arg() + 1);       /* stays in L2_MORE; yields */
}
```

- One line per step: a line is the natural unit, and the TX ring holds one.
- First conversions, by measured cost: `l2` (3.1 ms/entry), `stat` (~60 ms),
  `vlan` (~55 ms), `stp` status (~48 ms), `port N show` (~25 ms), `history`
  (≤ 180 ms), `health`.
- Web commands run the same states. The output goes to the capture buffer
  instead of waiting for TX room. The console mirror still waits, so a web
  command's speed stays bounded by the UART, as today.

## Rules

- No step prints more than one line without checking room.
- The prompt is only printed by `CLI_PROMPT`, so asynchronous messages (link
  changes, STP events) can't land in the middle of a command's output.
