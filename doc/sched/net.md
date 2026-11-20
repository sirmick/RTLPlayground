# 8. Network: rx, net, uIP, httpd

Code: the `rx` and `net` tasks in `tasks.c`; `rx_service()` and
`handle_tx()` in `rtlplayground.c`; `frame_tx.c` (the control-frame
buffer); `httpd/httpd.c`; `uip/` (one change, U9).

## Tasks

| Task | Freq. | States | Wakes |
|---|---|---|---|
| `rx` | high | `RX_IDLE` waits. `RX_STEP`: `rx_service()`, up to `RX_BUDGET` (4) frames from the NIC ring, each dispatched as before (STP, LACP, LLDP, IGMP, ARP, uIP); acks `NIC_INT_STS`. If the ring still holds frames: `STEP_STAY`; else re-arms EX1 and waits on `HW_RX`, no timeout. Wakes cli when httpd has queued a `/cmd` | `HW_RX` |
| `net` | once | `NET_START` sets the first due tick. `NET_TICK`: when due, `handle_tx()`, uIP's periodic calls for the TCP connection and every UDP one (DHCP, DNS, NTP, syslog), once a tick; when woken by the flash job's end (`fl_poll`), a poll of the TCP connection at once | every tick; woken by the flash job |

The plan's 1 s safety timeout on rx's wait was not built: rx waits on
`HW_RX` alone, as the old loop had no fallback either (d2-tasks.txt). The
plan's `NET_TCP`/`NET_UDP` split was not needed: `NET_TICK`'s longest step
was 202 µs (d2-tasks.txt).

## httpd stays a uIP callback

httpd runs inside `uip_input()` (from `rx`) and `uip_periodic()` or
`uip_poll_conn()` (from `net`). Most pages are generated into `outbuf` in
one step. Three things changed:

### 1. Uploads: the receive window is the stage's room (decision 20)

The callback copies each segment of the body into the stage, behind what
the flash job (chapter 10) still has to write, and claims the job if it
isn't running. uIP's receive window is the room left in the stage, at most
an MSS (`UIP_CONF_RECEIVE_WINDOW` is `uip_rcv_wnd`, which httpd sets): the
client sends only what fits, and the window's right edge (ACK + window)
never moves left, so nothing it has sent inside the window is lost. The
job takes what has come; at its `FL_DONE` it goes back to `FL_PROG` if more
came meanwhile, else the stage starts again (in the same step) and the job
wakes `net` (`fl_poll`, `task_wake(T_NET)`). `net` polls the connection at
once, which sends a window update if the window was short of an MSS, or
the answer when the part has ended.

- **Why not `uip_stop()`** (the plan, O3): Linux bounds its segments to
  half the largest window it has seen, so against uIP's one-MSS window it
  sends two of 730 B. Stopping after the first shrank the window under the
  second, already in flight; uIP dropped it without an ACK, and it came
  back only after a retransmission timeout, with backoff: curl ran at
  0.5 KiB/s, then uIP reset the connection at 82 s
  (`tools/curl_upload.py`, e3-flash-job.txt).
- **The stage is `config_buf`** (0 B of xdata), as the web command buffer
  is, because uIP has one connection (`UIP_CONNS 1`; the `#error` at
  `webcmd_queue()` in httpd.c): nothing else reaches it while the upload's
  connection lives. While the job writes for a connection that has ended, a
  new connection is reset, so no request lands in the stage under it. The
  stage is 4480 B (`CONFIG_UPLOAD_BUF`); `make UP_STAGE=n` builds a smaller
  one (at least 1600 B), to test a window that closes.
- **No other flash job between an upload's segments** (`up_active`; an
  upload whose body stops for 30 s is aborted): chapter 10.
- **A config save** takes the same job: its body is buffered whole in
  `config_buf` (as before), then the job erases the sector and copies it;
  data that comes meanwhile is dropped, as behind a queued `/cmd`, and the
  200 goes out on the poll after the job.
- **Zero windows.** Linux doesn't fill a short window (below half its
  segment size it waits for the update), so curl uploads in the sim saw no
  zero window. A client that fills it exactly (the old-vs-new bench) sees
  many, each reopened by the poll after the job: 53 with the 4480 B stage,
  118 with `UP_STAGE=2000`, the image staged whole (e3-flash-job.txt).
- **A lost window update is repaired** (U9, d98a7b8): uIP 1.0 didn't answer
  a zero-window probe (an empty segment, seq = rcv_nxt - 1) and took an
  in-sequence segment past the window it had advertised. Now, on an
  established connection, such a probe gets an ACK with the current window,
  and data past the window isn't taken but ACKed; both marked
  "RTLPlayground" in `uip.c`. `tools/flash_job_steps.py` S6: a client that
  probes every 200 ms and loses the first update: 1 probe, upload done in
  7.08 s; without the change, 149 probes unanswered and no answer by 120 s.
- **Upload time.** In the sim (curl, 237568 B, paced to the wall clock):
  refused by CRC 3.89 s, accepted 5.10 s (e5-step-api.txt), against fork
  main's 3.14 s / 4.27 s (e3-flash-job.txt). On the 8x (curl, an install
  image): 16.2 s, against fork main's 14.9 s (silicon.md, EB): the sim's
  paced links don't show the real per-segment round trips. Whether to
  advertise more than one MSS when the stage has room is F1b's (O9).

### 2. `/cmd` (web commands)

The callback queues the command in `config_buf` (`webcmd_pending`,
`TSTATE_WEBCMD`); the rx step that ran it wakes cli (`task_wake(T_CLI)`:
httpd can't include `sched.h`, the host tests have no `sched_gen.h`). cli
runs it a line a step, the reply captured in `outbuf`, and sets
`webcmd_done`; the connection's next poll sends the reply. The connection
stays open meanwhile. A `/cmd` whose connection ends before or while cli
runs it is dropped (chapter 7). The reply goes out on a later poll, not with
the request's ACK: one TCP frame more per `/cmd` (d3-oldnew.expect).

### 3. Pages that did slow I/O

- `/ports.json` and `/information.json` read the SFP caches (chapter 9),
  not I2C: before, a page with a DDM module did up to 13 I2C transactions
  in one rx step.
- `/vlanlist.json` walks a VLAN bitmap the VLAN code keeps, not 4094 table
  reads: 37.5 ms -> 2.9 ms with VLAN 1 only (e4-polls-vlan-eee.txt). The
  reply's own size still bounds it: 34 ms with 40 named VLANs (chapter 9).

## The control-frame TX buffer

STP, LACP and LLDP used to build their frames in `uip_buf`. With steps,
httpd may hold a half-sent response there between steps, and a BPDU built
in between would corrupt it. So control frames have their own buffer and
send path, and `uip_buf` belongs to uIP alone.

- `ctl_buf` in `frame_tx.c`: `RTL_FRAME_DESC_SIZE` (12) + `CTL_FRAME_MAX`
  (256, `LLDP_MAX_FRAME`): 268 B.
- LLDP is the largest. Its variable TLVs (system name, description, port
  description) are cut to their arrays (hostname 24, machine name 30, port
  name 32), and `rtl837x_lldp.c` checks at compile time that every TLV at
  its bound fits: 192 B of payload with a 20-character version string,
  234 B allowed. On the wire, with a 23-character hostname and a
  31-character port name: 199 B on the 8x, 205 B with a "-dirty" version
  (`tools/lldp_overrun.py`, d3-console-net.txt).
- Each user asserts its frame struct fits `CTL_FRAME_MAX`. RLDP builds no
  frames (the chip's engine); IGMP replies stay in `uip_buf`.
- STP, LACP and LLDP send with `ctl_output()`, uIP with `tcpip_output()`,
  both through the static `frame_output()`.

D3's first `ctl_buf` was 152 B and only `struct lldp_pkt` was checked: a
long hostname and port name ran an LLDP frame into the next xdata, the VLAN
names (d3-console-net.txt). The bound above is the fix.

## uIP timers

`handle_tx()` runs from `net` once per tick at most: `NET_TICK` runs it when
`net_due` has come and sets the next due tick one on. A pass longer than a
tick runs it once, as the old `tick_pending` bit did, so a long pass still
stretches TCP timers. Acceptable: TCP retransmits are harmless.
