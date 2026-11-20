# 9. Modules: tasks and states

The firmware's tasks, as declared in `tasks.c` and `flash_job.c`. The
bodies of the old loop's phases stay in `rtlplayground.c` (resident, as the
loop was): each step calls one and says when it is due again.

## Fixed tasks

In slot order (12 slots; evidence/fb-build.txt):

| Slot | Task | Freq. | States | Wakes | Was |
|---|---|---|---|---|---|
| 0 | `rx` | high | `RX_IDLE`, `RX_STEP`: up to 4 frames a step (chapter 8) | `HW_RX` | `handle_rx`, `idle()` |
| 1 | `link` | high | `LINK_READ`: `link_service()`: read and clear `ISR_INT_PORT_LINK_CHG`, re-arm EX0, `check_links()` (which may run `sds_config()` for a port's SerDes), `lacp_link_change()` | `HW_LINK`; house sets `link_irq` once a second, as the old loop did | `idle()`, `handle_tick` 1 Hz |
| 2 | `loop` | high | `LOOP_START`, `LOOP_POLL`: `rldp_tick()` if loop detection is on, once a second (`task_until(loop_due)`) | 1 s | `rldp_tick` |
| 3 | `con` | high | `CON_EDIT` (chapter 7) | `HW_CON_RX` | `cmd_edit` |
| 4 | `cli` | once | `CLI_IDLE`, `CLI_RUN`, `CLI_NEXT`, `CLI_MORE`, `CLI_PROMPT` (chapter 7) | woken by con (a line), rx (a `/cmd`); `HW_CON_TX` | `cmd_parser` |
| 5 | `net` | once | `NET_START`, `NET_TICK` (chapter 8) | every tick; woken by the flash job | `handle_tx` |
| 6 | `stp` | medium | `STP_TICK`: every tick, the 50 Hz `stp_timers()` caught up by at most `STP_CATCH_UP` (8) runs, if STP is on | every tick | `stp_timers` |
| 7 | `lacp` | medium | `LACP_TICK`: `lacp_timers()` every tick, if LACP is on | every tick | `lacp_timers` |
| 8 | `lldp` | once | `LLDP_TICK`: `lldp_service()` (its own timer decides when to send), and the cable test's `cabletest_tick()` | every tick | `lldp_tick`, `cabletest_tick` |
| 9, 10 | `sfp0`, `sfp1` | once | below; `SFP_START` ends the slot on a board without that cage | 50 ms polls | `handle_sfp`, `sfp_module_read` |
| 11 | `house` | once | `HOUSE_START`, `HOUSE_SEC`: `house_second()`: the loader's boot-count confirm (a flash job request), the uptime register, `handle_button()`, `link_irq`, the ARP timer every 10 s | 1 s | `handle_tick` 1 Hz |

stp, lacp and lldp run each tick into their own timers, as the old loop
did, rather than sleeping to them: that kept old-vs-new equal in M2
(d2-tasks.txt). What the plan had and the code doesn't: an `igmp` task
(IGMP has no timers: rx-driven), a cable-test job (its state machine rides
in lldp's step), stp's per-second `STP_SECOND`, lldp's port-a-step
`LLDP_PORT`, and rx's and link's 1 s timeouts.

## Jobs

| Job | States | Claimed by |
|---|---|---|
| `fl` (the flash job) | `FL_ERASE`, `FL_PROG`, `FL_VERIFY`, `FL_DONE` (chapter 10) | an upload, a config save, `factory-reset` and the button, `loader_confirm()` |

The long CLI commands are not jobs of their own but states of `cli` (cli
jobs, chapter 7): one command runs at a time, so they need no slot. They
are `l2`, `stat`, `lacp show`, `history`, `health tasks`, stp's BPDU
entries and `eee on|off`. The other 11 dynamic slots are unused today.

## SFP, per slot (E2)

The steps are in `tasks.c`, the I2C and the caches in `sfp.c`. A helper
does one transaction and says `SFP_MORE`, `SFP_DONE` or `SFP_FAIL`; the
step chooses the state (decision 22).

| State | Does | Next |
|---|---|---|
| `SFP_START` | the first poll tick into the slot's `arg`; no cage: `STEP_END` | `SFP_POLL` |
| `SFP_POLL` | `sfp_pins()`: detect and LOS, the old messages; every 50 ms (the slot's `arg` is its poll tick) | inserted and awake: `SFP_READ`; DDM due: `SFP_DDM`; in its wake-up delay: `SFP_WAKE` |
| `SFP_WAKE` | polls the pins on until the wake-up delay (`SFP_WAKE_TICKS`, as before) has passed | `SFP_READ`; removed: `SFP_POLL` |
| `SFP_READ` | one transaction a step: the old read's 8 (rate and encoding, the ASCII fields x3, options, vendor, model, serial), then the quirks' (at most 1), printing as before | `SFP_CONFIG`; a failed read: said, the caches cleared, and `SFP_POLL` finds the module again |
| `SFP_CONFIG` | `sds_config()` for the rate | `SFP_DDM` (a DDM module), else `SFP_POLL` |
| `SFP_DDM` | one of 4 transactions into the slot's diagnostics cache (0x51 bytes 184..239), every 40 polls (2 s) | `SFP_POLL` |

`/ports.json`, `/information.json` and the cli's `sfp` read the caches
(`sfp_id`, `sfp_diag`, `sfp_raw`), never I2C: before, a page with a DDM
module did up to 13 transactions in one rx step. A cache not yet filled
gives what a failed read gave (an empty field); a module's text is there
once `SFP_READ` has read it, ~0.5 s after insertion. `sfp_read_block()`
gives up after 1-2 ticks (5-10 ms) instead of spinning. `sfp N <speed>`
marks the slot empty and runs `sfp_pins()` at once. A module pulled during
its read clears the caches (`tools/sfp_pull.py`; e4-polls-vlan-eee.txt).

Measured in the sim (e2-sfp.txt): every SFP state under 1 ms (the sim's I2C
answers at once); on silicon a step is one transaction, ~2 ms at 100 kHz
(estimated, not measured). The old `SFP_POLL` did the whole read and
`sds_config` at once: 24.9 ms on the 8x (silicon.md, DB). EB read the DAC's
data from the caches; `SFP_CONFIG` on an insert and a failing transaction
are not yet measured on silicon (F2).

## Hardware polls get timeouts (E4)

In the common area (`rtlplayground.c`), so the flash's `WEL` wait can use
them too: `hw_start(n)` and `hw_expired()` bound a poll at n to n + 1 whole
ticks, and `hw_timeout(reg)` counts a poll that gave up, with its register.
`hw_poll(reg)` (banked, in `rtl837x_port.c`: only banked code calls it, and
the common area is the scarcer space) waits for bit 0 of the register's low
byte. That covers the table execute (`TBL_CTRL`: every
`wait_table_ready()`, the page and IGMP table walks), `STAT_GET` and the
I2C controller. It gives up after `HW_POLL_TICKS` (5-10 ms) and returns 1.
The L2 flush waits up to `HW_FLUSH_TICKS` (15-20 ms; its time on silicon
is not measured), and `rtl8224_sds_write` and `WEL` up to `HW_POLL_TICKS`.
The callers go on as if the operation had finished; only
`sfp_read_block()` fails its read. `health tasks` prints the count and the
last register (`ff01`: the flash never set `WEL`, so the erase or program
did nothing). The loader keeps its unbounded `WEL` wait.

A step waits out one timeout at most. After a timeout `hw_stuck` makes
every later poll in the same step give up at once, and the scheduler
clears it at the next step's start (`sched_m_step_start`). Without it a
page of polls with the bit held would wait out every one: /counters.json
has 55 `STAT_GET`s, ~550 ms. Every later poll that gives up is still
counted. Only steps get this cap: before the scheduler runs (`swdt_on`
clear, the boot) every poll waits its own bound. Otherwise one early
timeout would make every later boot poll give up on the execute bit's first
busy read, and the table work after it would read stale data and lose
writes: `tools/boot_stuck.py` with rtlsim's `reg busy` dropped 2733 table
commands without that check, 0 with it.

Forced in rtlsim (`tools/hw_timeouts.py`, rtlsim's `reg hold`, `phy hold`,
`flash wp`), each path finishes, is counted, and works again once released;
its longest step: table 9.6 ms, stat 10.8 ms, flush 16.6 ms, I2C 9.5 ms,
`WEL` 8.8 ms (e4-polls-vlan-eee.txt). On the 8x a link flap and EB's whole
run counted 0 timeouts.

Not bounded: `SFR_EXEC_STATUS` under every register and SMI access (the
primitive itself, microseconds; if it stuck, nothing could run), the
flash's `BUSY` (an erase's own time), and the NIC DMA (already 65536
spins). A hang in these is the software watchdog's (chapter 11).

## VLAN bitmap (E4)

`vlan_bits[512]` holds a bit per VLAN ID. `vlan_create` sets its bit,
`vlan_delete` clears it (console, `/cmd` and the config at boot all go
through them), and `vlan_setup` fills the bitmap from the table at boot (a
reset may leave entries valid; rtlsim keeps them). The VLAN table has no
other writers. `/vlanlist.json` walks it with `vlan_next()` instead of 4094
table reads: 37.5 -> 2.9 ms with VLAN 1, 39.0 -> 4.2 ms with 6 VLANs. A byte
map would have saved 158 B of code but cost ~1.2 ms more a walk, which
takes the 6-VLAN case over 5 ms; the bitmap stayed (e4-polls-vlan-eee.txt).
`vlan_delete` now waits for its own table write (U10, 3b7886c).

## `eee` as a cli job (E4)

`eee on|off [port]`: a port a step for its settings and PHY off, the cli
asleep `CLI_JOB_SLEEP_MS` (10 ms), a step for PHY on; it was `delay(2)` a
port, 78-80 ms in one step for all ports. Now `CLI_MORE` ≤ 386 µs,
`CLI_RUN` ≤ 257 µs, output equal to the old (e4-polls-vlan-eee.txt).

## Modules that need no task

- **DHCP, DNS, NTP, syslog:** uIP UDP callbacks, driven by `net`.
- **httpd:** uIP TCP callback (chapter 8).
- **IGMP snooping, ARP:** rx-driven; the ARP timer is house's.
- **VLAN, mirror, storm, bandwidth, ACL:** command-driven; a command runs in
  `CLI_RUN`.

## What is still long

No known failure is left in the sim: every line of ovs-rtlpg's
`tests/oldnew/known-failures` (the `l2` LACP gap, the upload's hello, the
CLI_RUN, HOUSE_SEC and RX_STEP long steps) matched no failed check by E4,
on both boards (e1-chunked-cli.txt, e1e3-merge.txt, e3-flash-job.txt,
e4-polls-vlan-eee.txt). The file itself still lists them; F1 shrinks it.
What remains:

| Step | Time | Where measured | Owner |
|---|---|---|---|
| `FL_ERASE` | 40 ms; 35.5-41.6 ms on the 8x | sim, 8x | inside the 50 ms limit (decision 25) |
| RX_STEP while a config POST arrives (its multipart parse in one step) | 18.2 ms; 8.6 ms in the sim with Linux curl | 8x (EB), sim (F1b) | the object-model project (decision 25) |
| RX_STEP for `/vlanlist.json` with 40 VLANs of 20-character names | 34 ms (67-100 ms before) | sim (e4); not a reference scenario | the object-model project (decision 25; F1b's analysis: 41 `vlan_name()` scans ~16 ms, the output the rest; a patch on the local branch `f1b-dropped`) |
| `FL_VERIFY`, `FL_PROG` | 14.2, 8.1 ms | sim (e3) | inside the 50 ms limit, over the 5 ms target |
| a forced poll timeout's step | 9.5-16.6 ms | sim (e4) | by design: one timeout a step |
| the end of a `/loader` or install-image upload, `loader_update()` | not measured | - | rare paths (chapter 10) |
