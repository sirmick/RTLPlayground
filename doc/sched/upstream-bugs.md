# Upstream candidates

Bugs in upstream RTLPlayground (logicog) found during the scheduler work, on
fork main 612116e unless noted. Each would be its own small PR: on upstream,
proven on the sim, one bug each. None is filed yet; Michael decides when.

| # | Bug | Where | Found by | Fixed here by |
|---|---|---|---|---|
| U1 | The boot prompt is printed before `cmd_editor_init()`, so console bytes arriving in between are dropped (`version` arrives as `ersion`) | `rtlplayground.c` (prompt), `cmd_editor.c:19` | sim, A1 | not yet (independent of the scheduler) |
| U2 | `cmd_edit` copies a character into `cmd_buffer` before checking `cmd_available`, so a multi-line paste overwrites the command still waiting to run (`stat` runs as `v...`) | `cmd_editor.c` | sim, A2 | fixed in D3 (689e548: the editor's own `con_line`, read stops while a line waits; tools/console_bugs.py) |
| U3 | The 16 B console RX ring laps silently: no overrun count | `rtlplayground.c` `isr_serial` | sim, A2 | fixed in D1 (256 B ring, overrun count in `health`, decision 19) |
| U4 | A web `/cmd` overwrites a half-typed console line (one shared buffer) | `cmd_parser` / httpd | audit (os-audit) | fixed in D3 (689e548: the web command runs from its own buffer in the cli task; f142690 cancels one whose connection ends; tools/console_bugs.py) |
| U5 | The CRC routine leaves `PSBANK` at 1 | `crc16_bank1` | audit | already fixed upstream before this work: 67d09dd (#418), in fork main 612116e |
| U6 | The health phase IDs clash | `health` | audit | phases.md, independent |
| U8 | An unknown escape sequence (e.g. ESC O P) makes `cmd_edit` spin forever: two `continue`s don't advance the read position | `cmd_editor.c:61-62`, `:170-171` | review, D1 | not yet |
| U7 | The host-test stubs lack `leds_mode` and `rldp_*`, so four host tests fail to link (fork only: the LED and loop-detection web pages) | `test/env_tables.c`, `test/test_httpd_tx.c` | A0 | fixed on fork main b635486 |
| U10 | `vlan_delete` doesn't wait for its VLAN-table write to finish, so the next table command can be dropped (seen as VLAN 4000 missing after `vlan 20 d`) | `rtl837x_port.c` vlan_delete | E4 test (tools/boot_stuck.py) | fixed in E4 (3b7886c) |
| U9 | uIP doesn't answer a zero-window probe or keep-alive (an empty segment out of sequence, seq = rcv_nxt - 1), and takes an in-sequence segment past the window it advertised: a lost window update is never repaired (uIP-wide, uIP 1.0; latent upstream, whose window is a constant MSS) | `uip/uip.c`, the sequence check in `uip_process()` | E3 review, sim (`tools/flash_job_steps.py` S6) | fixed in E3 (the check answers them with an ACK carrying the current window, marked "RTLPlayground"; test_httpd_tx probe checks) |

## Silicon facts worth upstreaming (documentation, not bugs)

- A chip reset before the switch init (NIC reset, SMI, PHY/SerDes set-up)
  hangs the RTL8373 until power is cycled, as already recorded for the
  SWTG024AS in `rtl837x_sfr.h` (silicon.md). Any watchdog must stay off
  during the boot.
- During a flash erase, memory-mapped reads of the bank window return 0x00
  on the RTL8373 (silicon.md); banked code must not run while the flash is
  busy.
- The loader's XMODEM accepts at most 1856 blocks (237,568 B): the app
  image, not the install image (whose first 237,568 B are the app).
