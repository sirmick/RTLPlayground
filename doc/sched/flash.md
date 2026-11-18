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
