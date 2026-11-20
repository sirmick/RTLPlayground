# Health: the switch's vital signs

The switch's own view of itself, read from inside: it works on the console
while HTTP does not. With the object model (DESIGN.md 7.3) it is state, read
with `get`; `health`, `health tasks`, `health clear` and the `HEALTH=1` build
are gone.

| Path | What |
|---|---|
| `state.sched` | passes, idle passes, the longest pass, overruns (steps of 50 ms or more) and the last one's state, the slot counts, fine units per tick |
| `state.sched.slot.<slot>.*` | each slot (`rx` .. `house`, `job0` ..): its task, state (`none`: free), wake mask, ticks to its wake, arg |
| `state.sched.state.<STATE>.*` | each state: runs (saturating), the longest run in µs |
| `state.sched.task.<task>.max_us` | the longest of a task's states |
| `state.health` | hardware polls that gave up and the last one's register; the stack never used since the boot (`stack_headroom`, from the paint at boot) |
| `state.con` | console RX overruns, RX bytes waiting, TX room |
| `state.web` | open sessions; the TCP connection (state, timers, ports, peer) |
| `state.web.session.<n>.idle_s` | seconds since each session's last request |
| `state.sys.reset.*` | the last reset (doc/sched/watchdog.md) |

`do sched.clear` zeroes the step statistics, the console's overruns and the
hardware timeouts.

```
> get state.sched.state.SFP_POLL
state.sched.state.SFP_POLL.runs 120
state.sched.state.SFP_POLL.max_us 2310
ok
```

`get state.sched` is about 140 lines, a line a step on the console; durations
are in µs (fine units of 6.144 µs; 402630 and more: past 400 ms, saturated).
The slots, states and tasks are the build's own: tools/sched_gen.py writes
their rows (`--om`) from the same TASK/STEP lines that build the scheduler.

Not shown, on purpose: the running slot, this pass's events and the
watchdog's count, which are always the reader's own when read from a step.
