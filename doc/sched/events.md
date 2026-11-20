# 6. Events and wakes

A waiting task gets going again in one of three ways (decision 23):

- **Its time comes:** the `wake` tick (chapter 4). The tick itself is not an
  event.
- **A hardware event** in its `mask`: 4 bits, from the interrupts and from
  the console rings, taken once per pass into `sched_ev`.
- **A wake:** another task calls `task_wake(T_X)`. Not an event: the woken
  slot is simply due now.

Code: `sched_events()` and `sched_idle()` in `sched_bank.c`, `task_wake()`
in `sched.c`, the bits in `sched.h`.

## Hardware events

Two sources, both read at the pass's start by `sched_events()`:

1. **ISR bits.** The ISRs set their `__bit` flags (`link_irq`, `rx_irq`) as
   before: one `setb`, no xdata. `sched_events()` turns them into event bits
   and clears them, with interrupts off for the few instructions it takes.
2. **Sampled state.** Cheap reads that cost only a variable or two: the
   console RX ring is not empty, the TX ring has room for a line. No new
   ISR bit.

```c
uint8_t sched_events(void) __banked
{
	ev = 0;
	EA = 0;
	if (link_irq) { link_irq = 0; ev |= HW_LINK; }
	if (rx_irq)   { rx_irq = 0;   ev |= HW_RX; }
	EA = 1;
	if (con_rx_ready())                ev |= HW_CON_RX;
	if (con_tx_room() >= CON_TX_LINE)  ev |= HW_CON_TX;
	return ev;
}
```

| Bit | Name | Source | Wakes |
|---|---|---|---|
| 0 | `HW_RX` | INT1 (`rx_irq`) | rx (`RX_IDLE`, `RX_STEP`) |
| 1 | `HW_LINK` | INT0 (`link_irq`); house also sets `link_irq` once a second, as the old loop did | link (`LINK_READ`) |
| 2 | `HW_CON_RX` | sampled: the console RX ring is not empty | con (`CON_EDIT`) |
| 3 | `HW_CON_TX` | sampled: the TX ring has room for a line (`CON_TX_LINE`, 100 B) | cli (`CLI_RUN`, `CLI_NEXT`, `CLI_MORE`); schedtest's printer |
| 4 | reserved | was `EV_FLASH_IDLE`: no flash operation outlives its step (chapter 10), so there is nothing to wake on. Don't build a start-and-return flash step on it | - |
| 5-6 | spare | | |
| 7 | `EV_FOREVER` | the scheduler's own: marks a wait without timeout | - |

Events latch for **one pass**. A task that doesn't run while its event is
in the snapshot (it wasn't at a ready position, or the event came after its
position) sees it only if it is still there at the next pass's start. So a
step **re-checks the level, never trusts the edge alone**: rx reads the NIC
ring's count, link reads and clears the ISR register, con drains the ring
until it is empty or a line waits for cli, cli checks the free room again (`CLI_RUN` and
`CLI_NEXT` want `CLI_ROOM`, 1 KiB, though `HW_CON_TX` means 100 B; they wait
again if it isn't there). The ISR bits are set again on the next edge, and
the sampled ones are levels, read every pass.

`sched_idle()` reads the same sources (and whether a tick has passed) with
interrupts off before it sleeps (chapter 4).

## Wakes

`task_wake(slot)` (resident) makes the slot due now, a wait without timeout
too (its `EV_FOREVER` mark goes): it runs at its next position, later in
this pass or in the next, whatever it was waiting for. The waker says why
in a level the woken step re-checks; a wake is not queued and carries
nothing.

| Waker | Woken | Why (the level its step re-checks) |
|---|---|---|
| con (`CON_EDIT`) | cli (`T_CLI`) | a console line is complete: `con_line_ready` |
| rx (`RX_STEP`, after httpd queued a `/cmd`) | cli | a web command is queued: `webcmd_pending` |
| the flash job (`FL_DONE`) | net (`T_NET`) | the job has ended: `fl_poll`, poll the connection now |
| schedtest's poster | waiter | `st_poked` |
| schedtest's cli | printer, jobber, slow, hot | their request variables |

A woken task may be in any state: its step re-checks its level and its
time before it acts, and waits again if neither is there (cli in
`CLI_MORE`, woken by con, finds no TX room and waits on `HW_CON_TX` again).
**A woken periodic task checks it is due** before it advances its period:
net's `NET_TICK` polls the connection for `fl_poll` at once, and does its
per-tick work only when `net_due` has come.

## A link-change wake: the pattern, not built

A task that should act on a link change between its ticks (stp, lacp,
loop) would be woken by `link_read` after it decodes a change, and its step
would check it is due before it advances its own timer, as net does. Not
built: nothing consumes a link change between ticks today (QA e5-linkchg,
decision 23). lacp hears of link changes inside `link_service()`
(`lacp_link_change()`), as before.

## Rules

- ISRs never touch xdata event words: they set bits, as before.
- A step that consumes a level (ring not empty, frames waiting) re-checks it
  and keeps itself ready (`return STEP_STAY;`) while work remains.
- A woken step re-checks why it was woken; a wake the step finds nothing
  for is simply lost, and a second wake before it runs is the same wake.
- No new hardware event without a reader: the old software events
  (decision 4's 16-bit mask with task posts) went where they had no reader
  (decision 23).
