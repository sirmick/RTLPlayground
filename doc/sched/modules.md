# 9. Modules: tasks and states

Fixed tasks first, then jobs. "From" names the code it replaces (file:line
on fork main 612116e, from `private notes: os-audit.md`).

## Fixed tasks

| Task | Freq. | States | Wakes | From |
|---|---|---|---|---|
| `rx` | high | `RX_STEP` | `HW_RX`, 1 s | `handle_rx`, `idle()` |
| `link` | high | `LINK_READ`: read + clear `ISR_INT_PORT_LINK_CHG` (and every other unmasked ISR bit, chapter 2), re-arm EX0, `check_links()`, `lacp_link_change()`. (A wake of stp, lacp, loop on a change was planned as `EV_LINKCHG`; not built, nothing consumes it: events.md.) On RTL8221 boards `sds_config()` (~8 ms) stays in the step | `HW_LINK`, 1 s fallback | `idle()`, `handle_tick` 1 Hz |
| `loop` | high | `LOOP_POLL`: read `LOOPED`, block/unblock (RLDP, on the loop-detect branch) | 1 s | `rldp_tick` |
| `con` | high | `CON_EDIT` (chapter 7) | `HW_CON_RX`, `HW_CON_TX` | `cmd_edit` |
| `cli` | once | `CLI_IDLE`, `CLI_RUN`, `CLI_PROMPT` + dump states (chapter 7) | woken by con (a line), rx (a `/cmd`) | `cmd_parser` |
| `net` | once | `NET_TCP`, `NET_UDP` (chapter 8) | 5 ms | `handle_tx` |
| `stp` | medium | `STP_STEP`: today's 50 Hz `stp_timers()` step (≤ `STP_CATCH_UP` per run). The per-second block becomes `STP_SECOND` | 20 ms | `stp_timers` |
| `lacp` | medium | `LACP_STEP`: one `lacp_step()` | 20 ms | `lacp_timers` |
| `lldp` | once | `LLDP_PORT`: one port's frame per step, 9 per second. Then sleep to the next second | 1 s | `lldp_tick` |
| `sfp` ×2 | once | below | sleeps | `handle_sfp`, `sfp_module_read` |
| `igmp` | once | `IGMP_TICK` if IGMP has timers (querier, ageing); otherwise no task, rx-driven only | 1 s | igmp |
| `house` | once | `HOUSE_SEC`: uptime register, `handle_button()`, ARP timer every 10 s | 1 s | `handle_tick` 1 Hz |

13 fixed slots (with two SFP slots), leaving 11 for jobs.

### SFP, per slot (the #770 shape)

| State | Does | Next |
|---|---|---|
| `SFP_POLL` | 2 GPIO reads: present, LOS | on insert `SFP_WAKE`; else sleep 50 ms |
| `SFP_WAKE` | (module power-up) | sleep 500 ms → `SFP_READ` |
| `SFP_READ` | one I2C transaction (of ~9), with a timeout | `SFP_READ` until done → `SFP_CONFIG` |
| `SFP_CONFIG` | `sds_config()` for the module's rate; prints the module line | `SFP_DDM` |
| `SFP_DDM` | one DDM I2C read into the slot's cache; every few s | `SFP_POLL` cycle continues |

A removed module: back to `SFP_POLL`, cache cleared.

**As built (E2).** The steps are in tasks.c (the slot order unchanged), the
I2C and the caches in sfp.c; a helper does one transaction and says
SFP_MORE / SFP_DONE / SFP_FAIL, the step chooses the state.

| State | Does | Next |
|---|---|---|
| `SFP_POLL` | `sfp_pins()`: detect and LOS, the old messages; every 50 ms (the slot's `arg` is its poll tick) | inserted: `SFP_WAKE`; DDM due: `SFP_DDM` |
| `SFP_WAKE` | polls the pins on, until the wake-up delay (`SFP_WAKE_TICKS`, as before) has passed | `SFP_READ`; removed: `SFP_POLL` |
| `SFP_READ` | one transaction a step, the old read's 8 (rate and encoding, the ASCII fields x3, options, vendor, model, serial), then the quirks' (at most 1), printing as before | `SFP_CONFIG`; a failed read: said, and `SFP_POLL` finds the module again |
| `SFP_CONFIG` | `sds_config()` for the rate | `SFP_DDM` (DDM modules), else `SFP_POLL` |
| `SFP_DDM` | one of 4 transactions into the slot's diagnostics cache (0x51 bytes 184..239), every 40 polls (2 s) | `SFP_POLL` |

`/ports.json` and `/information.json` read the caches (`sfp_diag`,
`sfp_id`), never I2C: before, a page with a DDM module did up to 13
transactions in one rx step (~25 ms on silicon). A cache not yet filled
gives what a failed read gave (an empty field). `sfp_read_block()` gives up
after 1-2 ticks (5-10 ms) instead of spinning. The cli's `sfp` still reads
the module live; `sfp N <speed>` marks the slot empty and runs
`sfp_pins()` at once, as `handle_sfp()` did. Measured in the sim
(`evidence/e2-sfp.txt`): every SFP state under 1 ms; on silicon a step is
one transaction, ~2 ms at 100 kHz.

## Jobs

| Job | States | Claimed by |
|---|---|---|
| dumps | `L2_FIRST`/`L2_MORE`, `STAT_ROW`, `VLAN_ROW`, `STP_ROW`, `PORT_ROW`, `HIST_LINE`, `HEALTH_LINE` | cli (states of the cli task, not separate slots) |
| `flash` | `FL_ERASE`, `FL_PROG`, `FL_VERIFY`, `FL_DONE` (chapter 10) | upload, config save, factory reset |
| `cable` | today's cabletest state machine, one state per 50 ms tick | `port N cabletest` |
| `eee` | `EEE_PORT`: one `phy_reset()` (10 ms) per step instead of `delay(2)` in a loop | `eee ... all` |

The dumps are **states of `cli`**, not separate jobs: one command runs at a
time, so they need no slot of their own.

## Modules that need no task

- **DHCP, DNS, NTP, syslog:** uIP UDP callbacks, driven by `net`.
- **httpd:** uIP TCP callback (chapter 8).
- **IGMP snooping, ARP:** rx-driven.
- **VLAN, mirror, storm, bandwidth, ACL:** command-driven; a command runs in
  `CLI_RUN`.

## Hardware polls get timeouts (phase E4)

Every unbounded poll gets a bound and an error path: I2C (`sfp_read_block`),
flash `WEL`, table execute, L2 flush, `rtl8224_sds_write`. A step that hits a
timeout reports it (`health`) and moves on; it doesn't retry in a loop.
