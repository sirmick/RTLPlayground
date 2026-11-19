# 10. Flash

## The rule

**While the flash is busy (erase or program), no banked code may run.** On
silicon, reads from the bank window return garbage during an erase (chapter
2). Only the common area and ISRs are safe. The simulator enforces it
(chapter 13): an instruction fetch or `movc` from the bank window while the
flash is busy is a test failure.

## Policy (first version)

A flash step **starts an operation and waits for it to finish, in the common
area, before returning.** Erase: ~40 ms, the one sanctioned exception to the
20 ms step limit. Page program: ~1 ms.

Why not start and return: the next task in the sequence would most likely be
banked. A "flash busy" mode where the loop only calls common-area steps is
possible, but those are few, and it complicates the loop for ~40 ms per
4 KiB. Revisit if uploads prove too slow.

Waits run with interrupts on: ISRs keep filling rings and setting bits.

## The flash job

```
FL_ERASE   erase the next 4 KiB sector of the target range     (~40 ms)
FL_PROG    program up to N pages (N = 4: ~4 ms) from the stage  repeat
FL_VERIFY  read back and CRC what was written (common area)
FL_DONE    post the requester's event, free the slot
```

- **Requests:** a small struct in xdata `{target, length, source, done_event}`.
  The requester claims the job and waits on its done event.
- **Upload (firmware):** httpd stages one TCP segment (chapter 8), the job
  erases ahead when a sector boundary nears, then programs the staged bytes,
  then `EV_UPLOAD_DONE` reopens the window. Expected upload time: segment
  count × (RTT + ~2-5 ms), plus 40 ms per 4 KiB sector. Roughly today's
  speed, without the long passes.
- **Config save:** buffered in `config_buf` as today. The job erases one
  sector and programs ≤ 16 pages over a few steps.
- **Factory reset:** the same, then a reset.

### As built (E3)

`flash_job.c` (banked), the job `fl` declared there (`TASK(fl, job, once, first=prog)`); `flash_job.h`
carries the request so httpd can make one without `sched.h`. A requester
sets `fl_req` (a config save also `fl_src`, `fl_len`) and calls
`flash_job_start()`: 0 if the job is busy or no slot is free (the request
is then refused: 400 for the web, "Flash busy, not reset" on the console).
At its end the job sets `fl_finished` and posts `EV_UPLOAD_DONE`.

| Kind | Steps |
|---|---|
| `FL_UPLOAD` | `FL_PROG`: httpd.c `upload_prog()`, today's `stream_upload()` split at its flash writes: it parses the stage into `flash_buf` and programs up to `UP_PAGES` (2) pages a step, ~8 ms, asking for `FL_ERASE` before the first page of each sector. When the part has ended: `FL_VERIFY`, the read-back CRC 2 KiB a step (`upload_verify()`, ~14 ms), then the answer is built. `FL_DONE`: `upload_done()` sends it back to `FL_PROG` if more of the body came meanwhile (the stage starts again only as the job ends) |
| `FL_CONFIG` | `FL_ERASE` of the config sector, then `FL_PROG` copies 512 B a step from `config_buf` (`FL_CHUNK`; 0.2 ms for a short config) |
| `FL_FACTORY`, `FL_FACTORY_BTN` | the same from `DEFAULT_CONFIG_START` (read into `flash_buf`, 5 ms a step), then `FL_DONE` prints and resets; the console's `factory-reset` shows no prompt meanwhile |
| `FL_BOOTCNT` | `loader_confirm()`, 15 s after a boot through the loader: one `FL_ERASE` of the boot count sector (it was a 40 ms erase in the house task's step); house asks again next second if the job is busy |

No new common-area helpers: `flash_sector_erase()` and
`flash_write_bytes()` already return only when the flash is idle, so the
steps call them as they are. `flash_write_bytes()` programs 4 bytes per
command (~70 µs each in the sim), which is what sets `UP_PAGES` at 2 and
`FL_CHUNK` at 512.

While an upload's body is coming (`up_active`, from its first staged
segment to the end of the part or of its connection), no other kind of job
starts between its segments: `loader_confirm()` asks again next second,
`factory-reset` and the button answer busy. Otherwise a segment staged while
that job ran would wait for a job that doesn't take it, and the other job
would overwrite `flash_buf`, the upload's partial page. As a second line,
`FL_DONE` of any kind but a factory reset goes on with the upload if the
stage holds unwritten bytes. An upload whose body stops coming is aborted
after 30 s (`UPLOAD_TIMEOUT`), so it can't hold the other jobs off for ever.

The flow control of an upload is the receive window, not `uip_stop()`
(chapter 8 item 1, decision 20). Upload time: see chapter 8. The longest
steps, measured in the sim (`evidence/e3-flash-job.txt`): `FL_ERASE` 40 ms
(the exception), `FL_VERIFY` 14.2 ms, `FL_PROG` 8.1 ms.

Not through the job yet: the end of a `/loader` or install-image upload
(`loader_stage_fits()`'s bitwise CRC over 16 KiB, `loader_stage_drop()`'s
four erases) and `loader_update()` before the reset; all one step each,
on the rare paths (open items in the E3 evidence).

## Unchanged

- `rtl837x_flash.c` stays resident (common area). The new job states call
  its primitives from short common-area helpers: `flash_erase_wait()`,
  `flash_program_pages_wait()` (today's `flash_write_bytes()` issues one
  4-byte program per ~1 ms, so a step can program only ~32 B within the
  step budget; B3's flasher does that). The job's states can themselves be banked:
  they call the common-area helper, which returns only when the flash is
  idle.
- Update apply at boot (runs before any task) is unchanged.
- The CRC routine (`crc16_bank1`) leaves `PSBANK` at 1 (an audit finding);
  fixed on the way, independently.
