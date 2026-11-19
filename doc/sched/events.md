# 6. Events and wakes

Two ways a waiting task gets going again (decision 23):

- **Hardware events:** 8 bits, the interrupts and the sampled console
  rings. A task's `mask` says which wake it; `sched_ev` holds this pass's.
- **Wakes:** one task waking another, `task_wake(T_X)`. Not an event: the
  woken slot is simply due now.

## Hardware events

1. **ISR bits.** ISRs keep setting their `__bit` flags (`link_irq`,
   `rx_irq`, `tick_pending`, ...) as today: one `setb`, no xdata. At pass
   start `sched_events()` turns them into event bits and clears them, with
   interrupts off for the few instructions it takes.
2. **Sampled hardware state.** Also at pass start, cheap reads that only an
   SFR or one variable cost: the console RX ring is not empty, the TX ring
   has room for a line. A source that costs more than ~2 µs may not be
   sampled.

```c
uint8_t sched_events(void)
{
	ev = 0;
	EA = 0;
	if (link_irq)   { link_irq = 0;   ev |= HW_LINK; }
	if (rx_irq)     { rx_irq = 0;     ev |= HW_RX; }
	EA = 1;
	if (con_rx_ready())                  ev |= HW_CON_RX;
	if (con_tx_room() >= CON_TX_LINE)    ev |= HW_CON_TX;
	return ev;
}
```

Events latch for **one pass**. A task that doesn't run while its event is
in the snapshot (it wasn't ready, or it has no position left) misses that
edge, so level conditions are re-checked by the step itself, never assumed
from the edge alone: the rx task reads the NIC ring, the link task reads the
ISR register.

| Bit | Name | Source | Woken |
|---|---|---|---|
| 0 | `HW_RX` | INT1 (`rx_irq`) | rx |
| 1 | `HW_LINK` | INT0 (`link_irq`) | link |
| 2 | `HW_CON_RX` | sampled: the UART RX ring is not empty (no new ISR bit) | con |
| 3 | `HW_CON_TX` | sampled: TX ring has room for a line (`CON_TX_LINE`, 100 B) | cli's commands and dumps, schedtest's printer |
| 4 | reserved | was `EV_FLASH_IDLE`. Dropped: a flash step waits for its operation in the common area before it returns (chapter 10), so the flash is never busy between steps and there is nothing to wake on. Don't build a start-and-return flash step on this bit | - |
| 5-6 | spare | | |
| 7 | `EV_FOREVER` | the scheduler's own: marks a wait without timeout | - |

The tick isn't an event: time is the `wake` field.

## Wakes

`task_wake(slot)` (resident, `sched.c`) makes the slot due now, a wait
without timeout too (its `EV_FOREVER` mark goes): it runs at its next
position, later in this pass or in the next, whatever it was waiting for.
The waker says why in a level the woken step re-checks; a wake is not
queued and carries nothing.

| Waker | Woken | Why (the level its step re-checks) |
|---|---|---|
| con (`CON_EDIT`) | cli (`T_CLI`) | a console line is complete: `con_line_ready` |
| rx (`RX_STEP`, httpd's `/cmd`) | cli | a web command is queued: `webcmd_pending` |
| the flash job (`FL_DONE`) | net (`T_NET`) | the job has ended: `fl_poll`, poll the connection now |
| schedtest's poster | waiter | `st_poked` |
| schedtest's cli | printer, jobber, slow, hot | their request variables |

A woken task may be in any state: a step re-checks its level and its time
before it acts, and waits again if neither is there (cli in `CLI_MORE`,
woken by con, finds no TX room and waits on `HW_CON_TX` again). **A woken
periodic task checks it is due** before it advances its period: net's
`NET_TICK` polls the connection for `fl_poll` at once and does its per-tick
work only when `net_due` has come.

What the old software events became (E5): `EV_LINE` and `EV_WEBCMD` are
cli's wakes, `EV_UPLOAD_DONE` net's; `EV_CMD_DONE` (httpd polls
`webcmd_done`), `EV_UPLOAD`, `EV_CFG` and `EV_LINKCHG` had no reader and are
gone. A link-change consumer would follow the same pattern: `link_read`
wakes the task (stp, lacp or loop) after it decodes a change, and that
task's step checks it is due before it advances its own timer. Not built:
nothing consumes a link change between ticks today.

## Rules

- ISRs never touch xdata event words: they set bits, as today.
- A step that consumes a level (ring not empty, frames waiting) re-checks it
  and keeps itself ready (`return STEP_STAY;`) while work remains.
- A woken step re-checks why it was woken; a wake the step finds nothing
  for is simply lost, and a second wake before it runs is the same wake.
