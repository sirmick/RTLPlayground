# 13. The simulator's side (ovs-rtlpg, branch `feat/sched-sim`)

rtlsim already had: a MAME-derived DW8051 core with the chip's SFRs; the
bank checker (`--check-banks`: a fetch from the bank window must come from
the bank that owns the address), stack-overflow and xdata-before-write
checks; register, SFR, flash, I2C and NIC traces; real-time pacing for
peers; OVS topologies and the `tests/net` harness. Firmware time is the
cycle count, 31.25 M machine cycles per firmware second.

The scheduler work added the pieces below. The protocol is documented in
`src/ctl/control.h` (read here at ovs-rtlpg 999584e); the commits named
are ovs-rtlpg's.

## Control socket and test-driven clock (A1)

`rtlsim --control PATH ...` listens on a UNIX socket and starts **paused**:
the firmware runs only when a request says so, and every input (a UART
byte, a frame, a link change) goes in at the firmware time it is given at.
Nothing reads the wall clock, so the same requests give the same replies,
byte for byte, run after run. The CPU runs in slices that end on multiples
of 256 cycles (8.192 µs); inputs, resets and the halt check happen only
there, so however a test cuts a run up, an input lands at the same time.
Without `--control` every other mode is unchanged.

One request per line; one reply, `ok ...` or `err <why>`, or a counted
block (`ok <n>` and n lines):

| Request | What |
|---|---|
| `time` | cycles and firmware time |
| `run <ms>` | advance |
| `run until pc=<loc>` / `uart=/<re>/` / `<loc> <op> <value>` `[max <ms>]` | to a breakpoint (a symbol, an address, `b<n>:0x...`), a match in the UART output, or a value (`== != < > &`, checked each slice) |
| `sym <name>`, `peek <loc> <len>`, `poke <loc> <hex>` | symbols from the `.map` (`--map`); spaces `x` xdata, `i` idata, `s` SFR, `c` code, `b<n>` a bank |
| `uart write <text>`, `uart read` | bytes onto the wire one per character time; the output, each line stamped |
| `nic inject <port> <hex>`, `nic stats`, `nic capture on\|off\|read` | a frame in (learned as the fabric learns; RLDP probes to the chip's engine); the NIC's counts and ring use; the frames out, stamped |
| `link <port> up [<speed>]\|down` | a link |
| `sfp <slot> <hex>\|empty` | a module in or out |
| `checks` | the model's findings: unmodelled, bank-ret, uart-lost, bank, flash-busy, early-reset, nic-rx-full, table-busy |
| `cov reset`, `cov dump <file>` | coverage |
| `reg dump`, `table dump vlan\|l2`, `flash read <addr> <len>` | the switch's stored state, the tables, the flash chip |
| `reg hold <addr> <mask>`, `phy hold <phy> <dev> <reg> <mask>`, `reg busy 5cac 1 <n>`, `flash wp on\|off`, `holds`, `... release` | force a poll's timeout (below) |
| `reset`, `powercut`, `quit` | a chip reset, off and on, exit |

Exit codes: 0; 6 if the firmware read the bank window while the flash was
busy; 7 if a chip reset hung the chip.

`tests/rtlsim_ctl.py` is the Python client: requests as methods
(`run_until_pc`, `peek_sym`, `read_struct` for an xdata array of structs),
and a context manager that starts and stops rtlsim.

A check the plan listed is not built: `checks` has no stack report
("stack is to come", control.h). The stack is measured by schedtest's paint
instead (chapter 14).

## UART at the baud rate (A2)

- TX: after an SBUF write, TI sets one character time later: 10 bit times
  at the rate the firmware programmed (87 µs at 115200).
- RX: `uart write` bytes arrive one per character time whether the firmware
  reads them or not; one that finds RI still set is lost (`uart-lost`).
- The boot transcript is unchanged; the boot takes more firmware time.

## Flash busy (A3, then matched to the 8x)

- With flash timing (on in control mode; `--no-flash-timing` turns it
  off): a sector erase 40 ms, a 64 KiB block erase 150 ms, a program of n
  bytes 10.5 + (n - 1) µs, a controller command 1 µs, a data read 8 µs:
  fitted to the 8x, where a 4-byte `flash_write_bytes()` takes 49-55 µs and
  a 256-byte one 1.71 ms (0d03a2a; silicon.md). Its 4-byte write stays
  ~6-12 µs short of the 8x's.
- While the flash is busy, **the bank window reads 0x00**, as the 8x's does
  (it was 0xFF until C measured it).
- **Checker:** an instruction fetch, `movc` or `movx` read from 0x4000-0xffff
  while the flash is busy is a violation: `checks` lists the first of each
  operation as `flash-busy`, with PC, PSBANK, SP and the firmware time, and
  the exit code is 6. schedtest's `bad_flash` is caught; the image that
  waits in the common area isn't (b5-neg-tests.txt).
- A power cut during an operation leaves it done in part, in proportion to
  its time.

## Coverage (A4)

A bitmap of executed instruction addresses: the common area, plus each bank.
`cov dump FILE` writes it; `tools/cov_report.py FILE --map M --rst DIR` maps
it to functions (`.map`) and source lines (`.rst`), and diffs two dumps by
function name. sim_tests asserts every state's step function and every
line of `sched.c` ran, sim_neg every line of `crash.c`.

## The early-reset hang (scoped)

A chip reset made by the application (booted by the loader) before the
switch init hangs the chip, as the 8x's did (silicon.md, C): the CPU never
runs again, frames are dropped, runs time out, until a `powercut`.
`checks` lists it once as `early-reset`; rtlsim exits 7. The proxy for "the
switch init" is a 0x632c write with bits 16-20 set since power-on; the
exact silicon condition is unknown, and a reset by the loader itself boots
(unconfirmed on silicon). On in control mode, `--no-early-reset-hang` turns
it off; off in plain mode. Michael's call, scoped to this evidence
(26830b7, 05424fe).

## The NIC RX ring and an LACP partner (D0a)

- The RX ring holds `--nic-rx-ring` bytes, 10 KiB by default; a frame that
  finds it full is dropped and `checks` lists the run as `nic-rx-full`.
  Nothing is received on a port with no link (ceb7e0a). The capacity is the
  plan's figure, not one measured on silicon (O5).
- `tests/oldnew/lacp_partner.py`: an 802.1AX partner, deterministic, fast
  or slow timeout, that can go silent and come back (fa7c7ed, 09d712e).
- MAC learning on `nic inject`, as the fabric's, so `l2` has a real table
  (b24a643). The L2 table has the chip's geometry: 1024 buckets of 4 ways,
  an index per entry; a MAC whose bucket is full isn't learned, as on the
  chip (e2b1bd2; the hash is a placeholder).

## Holding a bit busy (E4)

To force each bounded poll's timeout (chapter 9):

- `reg hold <addr> <mask>`: every firmware read of the register returns
  its value with the mask's bits set (`5cac 1` the table execute, `0f60 1`
  `STAT_GET`, `53d4 1ffff` the L2 flush, `0418 1` the I2C controller);
  `phy hold` the same for a PHY register over SMI (the RTL8224's SerDes
  busy bit; its ID must be held too, the sim models no 8224);
  `flash wp on`: write enable is ignored, so `WEL` stays clear (0514263).
- `reg busy 5cac 1 <n>`: a table command takes time, as on silicon: the
  next n reads of `TBL_CTRL` after a write with execute set still show it
  busy, then the command is done with the inputs it was started with; a
  command written while one is busy is dropped (`table-busy`). This is what
  showed that a boot-time timeout must not cap the later polls, and U10
  (999584e; e4-polls-vlan-eee.txt).

All are kept over a power cut, as links are.

## The old-vs-new harness (D0b, then EH)

`tests/oldnew/` runs the same scenarios on two boot images, each in its own
rtlsim with the same inputs at the same firmware times, and compares
**results**, not traces: every console reply and everything else the
console said between steps, every web response, the boot log, table
snapshots and frame counts, LLDP frames TLV by TLV, and at the end the
registers, the VLAN and L2 tables, the saved config and the frames out per
port. The version string and build dates are scrubbed. `--expect FILE`
lists the differences a package means, one line each with its reason;
each package's file is in `evidence/` (`d1-oldnew.expect` ... `e4-...`).

17 scenarios: boot, cli, web, vlan, stp, lacp, lacp_slow, lldp, loop, igmp,
config, upload, sfp, flaps, l2_big (1000 hosts learned), lacp_l2, and
stp_upload. The bench (`bench.py`) has a host on front port 1 that speaks
ARP, TCP and HTTP (keeping to the switch's TCP window since 7255e8b), the
LACP partner, cables, SFP modules, and a count of frames by port and kind.

**The new-only checks** (`checks.py`, `oldnew.py --checks`; d3d1843): what
the scheduler must do better than fork main, measured on one image:

| check | where | bound |
|---|---|---|
| lacp gap | lacp, lacp_l2 (the `l2` dump and /l2.json) | the switch's LACPDUs < 1.2 s apart on each LAG port |
| stp hello | stp, stp_upload (from 4 s before the upload to its answer) | BPDUs 2 s ± 50 ms apart |
| long step | every scenario, at its end | `health tasks`: no state over 20 ms but `FL_ERASE` |

`--known FILE` (`tests/oldnew/known-failures`) lists failures on record,
each for named scenarios and under a ceiling; a line that matches no failed
check is flagged as stale, so a fix shows (9020430, fc5b4f5). At M3's end
every line in it is stale (chapter 9).
