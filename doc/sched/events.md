# 6. Events

16 bits. A task's `mask` says which wake it; `sched_ev` holds this pass's
events.

## Sources

1. **ISR bits.** ISRs keep setting their `__bit` flags (`link_irq`,
   `rx_irq`, `tick_pending`, ...) as today: one `setb`, no xdata. At pass
   start `events_snapshot()` turns them into event bits and clears them,
   with interrupts off for the few instructions it takes.
2. **Sampled hardware state.** Also at pass start, cheap reads that only an
   SFR or one switch register cost: console TX ring has room, flash idle. A
   source that costs more than ~2 µs may not be sampled.
3. **Posted by tasks.** `event_post(EV_x)` ORs into `sched_posted`; the
   snapshot takes and clears it. A post therefore lands in the next pass.

```c
uint16_t events_snapshot(void)
{
	uint16_t ev = sched_posted;   sched_posted = 0;
	EA = 0;
	if (link_irq)   { link_irq = 0;   ev |= EV_LINK; }
	if (rx_irq)     { rx_irq = 0;     ev |= EV_RX; }
	EA = 1;
	if (con_rx_ready())                  ev |= EV_CON_RX;
	if (con_tx_room() >= CON_TX_LINE)    ev |= EV_CON_TX;
	if (!sched_flash_busy)               ev |= EV_FLASH_IDLE;
	return ev;
}
```

Events latch for **one pass**. A task that doesn't run while its event is
in the snapshot (it wasn't ready, or it has no position left) misses that
edge, so level conditions are re-checked by the step itself, never assumed
from the edge alone: the rx task reads the NIC ring, the link task reads the
ISR register.

## The list (draft)

| Bit | Name | Source | Woken |
|---|---|---|---|
| 0 | `EV_RX` | INT1 (`rx_irq`) | rx |
| 1 | `EV_LINK` | INT0 (`link_irq`) | link |
| 2 | `EV_CON_RX` | sampled: the UART RX ring is not empty (no new ISR bit) | con |
| 3 | `EV_CON_TX` | sampled: TX ring has room for a line | con, cli dumps |
| 4 | `EV_FLASH_IDLE` | sampled | flash job |
| 5 | `EV_LINE` | posted by con: a console line is complete | cli |
| 6 | `EV_WEBCMD` | posted by httpd: a web command is queued | cli |
| 7 | `EV_CMD_DONE` | posted by cli: a web command finished | httpd poll (net) |
| 8 | `EV_LINKCHG` | posted by link: decoded link changes | stp, lacp, loop |
| 9 | `EV_UPLOAD` | posted by httpd: upload data staged | flash job |
| 10 | `EV_UPLOAD_DONE` | posted by the flash job | httpd poll (net) |
| 11 | `EV_CFG` | posted: the config changed | (spare, for save-on-change) |
| 12-14 | spare | | |
| 15 | `EV_FOREVER` | the scheduler's own: marks a wait without timeout | - |

The tick isn't an event: time is the `wake` field.

## Rules

- ISRs never touch xdata event words: they set bits, as today.
- A step that consumes a level (ring not empty, frames waiting) re-checks it
  and keeps itself ready (`task_yield()`) while work remains.
- An event posted for a task that isn't waiting for it is simply lost after
  one pass; posting is a wake-up, not a queue.
