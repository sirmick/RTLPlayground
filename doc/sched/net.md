# 8. Network: rx, uIP, httpd

## Tasks

| Task | Freq. | States | Wakes |
|---|---|---|---|
| `rx` | high | `RX_STEP`: up to `RX_BUDGET` (4) frames from the NIC ring. Each is dispatched as today (STP, LACP, IGMP, ARP, uIP). Ack `NIC_INT_STS`. If the ring still holds frames: `task_yield()`, else re-arm EX1 and `task_wait(EV_RX, 1000)` (the timeout is a safety net for a lost edge, cf. #818) | `EV_RX` |
| `net` | once | `NET_TCP`: `uip_periodic()` for the TCP connection. `NET_UDP`: one UDP connection per step, rotating (DHCP, DNS, NTP, syslog...). Then `task_sleep(5)` | 5 ms, `EV_CMD_DONE`, `EV_UPLOAD_DONE` |

## httpd stays a uIP callback

httpd runs inside `uip_input()` (from `rx`) and `uip_periodic()` (from
`net`). Generating one JSON page into `outbuf` takes ≤ 2 ms. That is fine in
a step, with three exceptions that change:

1. **Uploads (firmware and config).** The callback copies the segment into
   a staging buffer (xdata, ~1.5 KiB), posts `EV_UPLOAD`, and calls
   `uip_stop()`: the window closes, so the client stops sending. The flash
   job (chapter 10) writes the staged data, then posts `EV_UPLOAD_DONE`. The
   next `net` poll sees it and calls `uip_restart()`. One segment in flight;
   the client's TCP waits.
2. **`/cmd` (web commands).** The callback queues the command (copies it to
   the web command buffer) and posts `EV_WEBCMD`. On `uip_poll()` it checks
   for `EV_CMD_DONE`, then sends the captured output. The connection stays
   open meanwhile; uIP's poll keeps it alive.
   As built (D3): the web command buffer is `config_buf` and the reply is
   captured in `outbuf`, because uIP has one connection
   (`UIP_CONF_MAX_CONNECTIONS 1`; `httpd.c` `#error`s otherwise); the rx
   task posts `EV_WEBCMD` (httpd can't include `sched.h`), and the poll
   checks `webcmd_done`. A `/cmd` whose connection ends before cli runs it
   is cancelled (chapter 7, "Two input buffers, one parser"). The reply goes
   out on a later poll, not with the request's ACK: one TCP frame more per
   `/cmd` (d3-oldnew.expect).
3. **Pages that do slow I/O.** `/ports.json` and `/information.json` read
   the SFP diagnostics cache (chapter 9), not I2C. `/vlanlist.json` reads a
   VLAN bitmap the VLAN code keeps, not 4094 table reads.

## The control-frame TX buffer

Today STP, LACP, LLDP and loop-detect frames are built in `uip_buf`. With
steps, httpd may hold a half-sent response there between steps; a BPDU built
in between would corrupt it. Control frames get their own small buffer (one
max-size control frame, ~128 B) and their own send path (`nic_tx_packet`
from it). `uip_buf` then belongs to uIP alone.

## uIP timers

`uip_periodic()` runs from `net` every 5 ms of real time, measured by
ticks. A pass longer than 5 ms no longer merges ticks into one (today's
`tick_pending` is a single bit): the `net` task runs once per pass, and uIP's
timer granularity is 5 ms per call. A long pass therefore still stretches
TCP timers, just less than today. Acceptable: TCP retransmits are harmless.
