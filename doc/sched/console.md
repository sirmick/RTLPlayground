# 7. Console and CLI

## UART rings (phase D1, can land before the scheduler)

| Ring | Size | Filled by | Drained by |
|---|---|---|---|
| RX | 64 B (from 16) | `isr_serial` | the `con` task |
| TX | 128 B (one long line plus room) | `write_char` | `isr_serial` on TI |

- **RX:** the ISR stores, or counts an overrun when the ring is full (shown
  in `health`). `EV_CON_RX` is sampled (the ring not empty), so no new ISR
  bit. `^R ^R` stays in the ISR, as today, and records the reset
  (chapter 11).
- The ISR side is two macros, `CON_ISR_RX(c)` and `CON_ISR_TX()`
  (`con_ring.h`); with them the serial ISR pushes 9 bytes instead of 6.
- **TX:** `write_char` puts a byte in the ring and, if the transmitter is
  idle, starts it. **If the ring is full it busy-waits** for room, as today's
  code waits for every byte. Old code that prints a lot keeps working at
  today's cost; converting it is an improvement, not a requirement.
- `cmd_capture` (web commands) and syslog stay in `write_char`, unchanged.
- `EV_CON_TX` = "room for a full line" (≥ 100 free bytes), sampled at
  pass start.

## The `con` task (editor)

Fixed, frequency **high** (pasted input must not overflow 64 B: at 115200 a
byte arrives every 87 µs, so `con` must run at least every ~5 ms while
input flows).

| State | Does | Next |
|---|---|---|
| `CON_EDIT` | drains the RX ring into the line editor (today's `cmd_edit`), echoes. On Enter: hands the line to cli (posts `EV_LINE`) and waits for cli to take it | `task_wait(EV_CON_RX | EV_CON_TX, FOREVER)` |

If the echo doesn't fit the TX ring, `con` stops draining and waits for
`EV_CON_TX`; the RX ring buffers meanwhile.

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

## Chunked output

A long command is states owned by the cli task, with a cursor in the cli
context (the "next param"):

```c
static __xdata uint16_t cli_cursor;      /* next entry / row / line */

void l2_more(void) __banked
{
	if (!con_tx_room_line()) { task_wait(EV_CON_TX, SCHED_FOREVER); return; }
	if (!l2_print_entry(cli_cursor++))   /* one line */
		{ task_next(S_CLI_PROMPT); return; }
	/* stays in L2_MORE; yields */
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
