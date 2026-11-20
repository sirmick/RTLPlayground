# 10. Flash

Code: `flash_job.c` and `flash_job.h` (the job), `httpd/httpd.c`
(`upload_prog()`, `upload_verify()`, `upload_done()`, `up_active`),
`rtl837x_flash.c` (the primitives, resident, unchanged), `boot.c`
(`loader_confirm()`), `cmd_parser.c` (`factory-reset`).

## The rule

**While the flash is busy (erase or program), no banked code may run.** On
silicon, reads from the bank window return 0x00 during an erase (chapter
2). Only the common area and ISRs are safe. rtlsim enforces it (chapter 13):
an instruction fetch, `movc` or `movx` read from the bank window while the
flash is busy is a violation, and the run exits 6. schedtest's
`ST_NEG=bad_flash`, a step that starts an erase and returns, is caught
(b5-neg-tests.txt).

## Policy

A flash step **starts an operation and waits for it to finish, in the
common area, before it returns.** `flash_sector_erase()` and
`flash_write_bytes()` already return only when the flash is idle, so the
job's steps call them as they are: no new common-area helper. An erase is
the one sanctioned exception to the 20 ms step limit: 40 ms in the sim,
35.5-41.6 ms on the 8x. The waits run with interrupts on: ISRs keep filling
rings and setting bits.

Why not start and return: the next task in the sequence is most likely
banked. A "flash busy" mode where the loop runs only common-area steps is
possible, but those are few, and it complicates the loop for ~40 ms per
4 KiB. Not built.

## The flash job

`TASK(fl, job, once, first=prog)` in `flash_job.c`; `flash_job.h` carries
the request, so httpd can make one without `sched.h`. A requester sets
`fl_req` (a config save also `fl_src` and `fl_len`) and calls
`flash_job_start()`. It returns 0 if the job is busy, if an upload is
coming and the request is another kind (`up_active`, below), or if no slot
is free; the request is then refused (400 for the web, "Flash busy, not
reset" on the console). At its end the job sets `fl_finished` and
`fl_poll` and wakes net (`task_wake(T_NET)`), which polls the connection at
once (chapter 8).

| State | Does |
|---|---|
| `FL_ERASE` | erases the sector at `fl_addr`; then `FL_PROG` (`FL_DONE` for the boot count) |
| `FL_PROG` | an upload: `upload_prog()`; a config or factory reset: programs `FL_CHUNK` (512 B) from the source |
| `FL_VERIFY` | an upload: `upload_verify()`, the read-back CRC |
| `FL_DONE` | goes back to `FL_PROG` if the stage holds unwritten upload bytes; a factory reset prints and resets; else ends the job, `fl_poll`, wakes net |

| Kind | Steps |
|---|---|
| `FL_UPLOAD` | starts in `FL_PROG`: `upload_prog()` is the old `stream_upload()` split at its flash writes. It parses the stage into `flash_buf` and programs up to `UP_PAGES` (2) pages a step, asking for `FL_ERASE` before the first page of each sector. When the part has ended: `FL_VERIFY`, the CRC 2 KiB a step, then the answer is built. `FL_DONE`: `upload_done()` sends it back to `FL_PROG` if more of the body came meanwhile; the stage starts again only as the job ends |
| `FL_CONFIG` | `FL_ERASE` of the config sector, then `FL_PROG` copies 512 B a step from `config_buf` |
| `FL_FACTORY`, `FL_FACTORY_BTN` | the same from `DEFAULT_CONFIG_START` (read into `flash_buf` a chunk a step), then `FL_DONE` prints and resets; the console's `factory-reset` shows no prompt meanwhile |
| `FL_BOOTCNT` | `loader_confirm()`, 15 s after a boot through the loader: one `FL_ERASE` of the boot-count sector. It was a ~40 ms erase in the house task's step (e0-leftovers.txt); house asks again next second if the job is busy |

32-bit flash addresses advance by 16-bit halves (`FL_ADVANCE`): a plain
32-bit add spilled 2 B into internal RAM, and the gate failed it
(e3-flash-job.txt).

### Uploads and the other kinds

While an upload's body is coming (`up_active`, from its first staged
segment to the end of the part or of its connection, or a segment past the
window, or a job that couldn't start), no other kind of job starts between
its segments: `loader_confirm()` asks again next second, `factory-reset`
and the button answer busy. Otherwise a segment staged while that job ran
would wait for a job that doesn't take it, and a factory reset would
overwrite `flash_buf`, the upload's partial page. As a second line,
`FL_DONE` of any kind but a factory reset goes on with the upload if the
stage holds unwritten bytes. An upload whose body stops coming is aborted
after 30 s (`UPLOAD_TIMEOUT`), so it can't hold the other jobs off for
ever; the old code waited for ever (e3-flash-job.txt, the review fix
7e30e5e and 1dc9745).

The flow control of an upload is the receive window, not `uip_stop()`
(chapter 8, decision 20).

### Measured

| Step | Sim (e3-flash-job.txt) | 8x (silicon.md, EB) |
|---|---|---|
| `FL_ERASE` | 40.0 ms | 35.5 ms (config save) |
| `FL_PROG`, upload | 8.1 ms | - |
| `FL_PROG`, config | 0.2 ms (a short config) | 0.47 ms |
| `FL_PROG`, factory reset | 5.2 ms | - |
| `FL_VERIFY` | 14.2 ms | - |
| `HOUSE_SEC` around the boot-count erase | 0.1 ms | - |

No step over 20 ms but `FL_ERASE` in any case, both boards
(`tools/flash_job_steps.py` S0-S6). On the 8x a config save read back
identical, and an upload went through (16.2 s, chapter 8).

### Not through the job

The end of a `/loader` or install-image upload (`loader_stage_fits()`'s
bitwise CRC over 16 KiB, `loader_stage_drop()`'s four erases) and
`loader_update()` before the reset are still one step each: rare paths,
not measured. The plain firmware upload is all in steps.

## Unchanged

- `rtl837x_flash.c` stays resident (common area); its erase and program
  waits are where the flash is busy. The `WEL` wait got E4's bound
  (chapter 9); the flash's `BUSY` wait is the erase's own time.
- `flash_write_bytes()` programs 4 bytes per command and waits for each:
  1.71 ms per 256 B on the 8x (chapter 2). That sets `UP_PAGES` at 2 and
  `FL_CHUNK` at 512.
- Update apply at boot (it runs before any task) is unchanged.
- The CRC routine leaving `PSBANK` at 1 (the audit's U5) was already fixed
  on fork main before this work (67d09dd, in 612116e).
