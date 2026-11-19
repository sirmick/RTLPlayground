# 8. Network: rx, uIP, httpd

## Tasks

| Task | Freq. | States | Wakes |
|---|---|---|---|
| `rx` | high | `RX_STEP`: up to `RX_BUDGET` (4) frames from the NIC ring. Each is dispatched as today (STP, LACP, IGMP, ARP, uIP). Ack `NIC_INT_STS`. If the ring still holds frames: `return STEP_STAY`, else re-arm EX1 and `task_wait(HW_RX, 1000)` (the timeout is a safety net for a lost edge, cf. #818) | `HW_RX` |
| `net` | once | `NET_TCP`: `uip_periodic()` for the TCP connection. `NET_UDP`: one UDP connection per step, rotating (DHCP, DNS, NTP, syslog...). Then `task_sleep(5)` | 5 ms, woken by the flash job |

## httpd stays a uIP callback

httpd runs inside `uip_input()` (from `rx`) and `uip_periodic()` (from
`net`). Generating one JSON page into `outbuf` takes ≤ 2 ms. That is fine in
a step, with three exceptions that change:

1. **Uploads (firmware and config).** The callback copies each segment of
   the body into the stage, behind what the flash job (chapter 10) still
   has to write, and claims the job if it isn't running. uIP's receive
   window is the room left in the stage, at most an MSS
   (`UIP_CONF_RECEIVE_WINDOW` is `uip_rcv_wnd`, set by httpd): the client
   sends only what fits, and the window's right edge (ACK + window) never
   moves left, so nothing it has sent inside the window is lost. The job
   takes what has come; at its `FL_DONE` it goes back to `FL_PROG` if more
   came meanwhile, else the stage starts again (in the same step) and it
   wakes `net` (`fl_poll`, `task_wake(T_NET)`; it was `EV_UPLOAD_DONE`
   until E5): `net` polls the connection at once, which sends
   a window update if the window was short of an MSS, or the answer when
   the part has ended. Decision 20.
   - **Why not `uip_stop()`** (the first plan, O3): Linux bounds its
     segments to half the largest window it has seen, so against uIP's
     one-MSS window it sends two of 730 B. Stopping after the first shrank
     the window under the second, already in flight; uIP drops it without
     an ACK, and it came back only after a retransmission timeout, with
     backoff (curl: 0.5 KiB/s, then uIP's reset at 82 s; 2026-11-19,
     `tools/curl_upload.py`).
   - **The stage is `config_buf`** (0 B of xdata), as the web command
     buffer is, because uIP has one connection (`UIP_CONNS 1`; the `#error`
     at `webcmd_queue()` in httpd.c): nothing else reaches it while the
     upload's connection lives. While the job writes for a connection that
     has ended, a new connection is reset, so no request lands in the stage
     under it. 4480 B (`CONFIG_UPLOAD_BUF`); `make UP_STAGE=n` builds a
     smaller one, to test a window that closes.
   - **A config save** takes the same job: its body is buffered whole in
     `config_buf` (as before), then the job erases the sector and copies it;
     data that comes meanwhile is dropped, as behind a queued `/cmd`, and the
     answer goes out on the poll after the job.
   - Linux doesn't fill a short window (below half its segment size it
     waits for the update), so its uploads in the sim never saw a zero
     window unloaded; a client that fills it exactly (the old-vs-new bench)
     sees many, each reopened by the update. uIP doesn't answer zero-window
     probes, so that update must come: it does, from the poll after the job
     (`tools/flash_job_steps.py`, the `UP_STAGE=2000` runs in
     `evidence/e3-flash-job.txt`).
   - Upload time in the sim (curl, 237568 B, wall-clock paced): 3.9 s
     refused / 5.1 s accepted, against fork main's 3.1 s / 4.3 s.
2. **`/cmd` (web commands).** The callback queues the command (copies it to
   the web command buffer) and cli is woken. On `uip_poll()` it checks
   that the command is done, then sends the captured output. The connection stays
   open meanwhile; uIP's poll keeps it alive.
   As built (D3): the web command buffer is `config_buf` and the reply is
   captured in `outbuf`, because uIP has one connection
   (`UIP_CONF_MAX_CONNECTIONS 1`; `httpd.c` `#error`s otherwise); the rx
   task wakes cli (`task_wake(T_CLI)`; httpd can't include `sched.h`), and the poll
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

As built (D3): `ctl_buf` in `frame_tx.c`, `RTL_FRAME_DESC_SIZE` +
`CTL_FRAME_MAX` (256, `LLDP_MAX_FRAME`): 268 B, not ~128. LLDP is the
largest: its variable TLVs (system name, description, port description) are
cut to their arrays, and `rtl837x_lldp.c` checks at compile time that every
TLV at its bound fits (about 192 B of payload with today's version string;
205 B on the wire measured with a 23-character hostname and a 31-character
port name, `tools/lldp_overrun.py`). Each user asserts its frame struct fits
`CTL_FRAME_MAX`. RLDP builds no frames (the chip's engine); IGMP replies stay
in `uip_buf`. STP, LACP and LLDP send with `ctl_output()`, uIP with
`tcpip_output()`, both through the static `frame_output()`.

## uIP timers

`uip_periodic()` runs from `net` every 5 ms of real time, measured by
ticks. A pass longer than 5 ms no longer merges ticks into one (today's
`tick_pending` is a single bit): the `net` task runs once per pass, and uIP's
timer granularity is 5 ms per call. A long pass therefore still stretches
TCP timers, just less than today. Acceptable: TCP retransmits are harmless.
