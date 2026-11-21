# The object model

The switch's configuration, its state and its actions are one tree of paths,
read and written as `path value` lines:

```
port.3.speed 1g                 config: set, saved, applied
state.port.3.link up            state: read only
do port.3.cabletest             an action
```

The serial console and the web's `POST /cli` take the same lines (`doc/cli.md`
is the user's reference, `doc/api.md` the web's). This file is the
developer's: how the pieces fit, and how to add to them. `DESIGN.md` here is the
design as it was written before and during the work; where it and the code
differ, the code and this file win.

## The pieces

| Where | What |
|---|---|
| `schema/*.py` | The schema: every object, field and action, with its type, default, limits, tip and handler names. The one source of truth. |
| `schema/registry.txt` | Stable IDs for every object and field (`obj`, `field`), and for state (`sobj`, `sfield`). Appended by the generator, never edited. |
| `tools/om_gen.py` | The generator: reads the schema and the registry, writes the tables, the help text and the web UI's `html/schema.js`. |
| `om/om.c` | The engine: the verbs, path lookup, value parse and print, the copies, apply and rollback, the binary saved config, help, Tab. Generic: no object is named in it. |
| `om/om.h` | The engine's interface to the handlers (types, `OM_OK`/`OM_ERR`, `om_fail()`, the copies `cfg_run`/`cfg_cand`/`cfg_roll`). |
| `om/om_<area>.c` | Handlers: the code that knows the hardware. `om_port.c`, `om_vlan.c`, `om_lag.c`, `om_stp.c`, `om_sys.c`, `om_svc.c` (dns, ntp, syslog, web, mgmt), `om_health.c`, `om_dbg.c`. Some handlers live next to their subsystem (`dns.c`'s `om_get_dns`). |
| `om/om_job.c` | The `cfg` scheduler job: apply, the confirm window, rollback, a step at a time. |
| `om/om.mk` | The build's part: runs the generator, lists the sources. |
| generated, in `output/<board>/om/` | `om_gen.h` (structs, constants, handler prototypes), `om_gen.c` (the tables), `help.bin` (the help text, placed after the default config in flash). |
| `html/schema.js` | Generated: every field's type, limits, default, unit, label and ID, for the UI. |

## The model

**Objects** hold fields. An object is one of:
- a singleton: `sys`, `mgmt`, `stp`;
- indexed by front port: `port.3`;
- 1..n: `lag.2`;
- by a word: `port.3.storm.bcast`;
- keyed, with entries made by `set` and removed by `del`: `vlan.100`;
- streamed (state only, a getter that finds each instance): `state.l2.K`.

A sub-object has its parent's index first (`port.N.storm.W`).

**Config fields** live in three copies of one packed struct:
- `cfg_run`: what the switch does now;
- `cfg_cand`: the candidate, which `set` and `del` change;
- `cfg_roll`: what `apply N` rolls back to if nobody confirms.

`save` writes `cfg_run` to flash.

**State fields** are filled by the object's `get=` handler into `om_st` when
read. Nothing stores them.

**Actions** call their handler at once. Their argument, if any, is a number
(`om_key`) or text (`om_arg`, with `action(..., arg="text")`).

## Apply

`apply` checks the whole candidate first: every object's `check=` handler, on
`cfg_cand`. One refusal refuses it all, with the handler's message. Then it
walks the objects in `order=` (lower first; parents before children on a
tie), and calls `apply=` for each instance that differs from running. A
handler reads `cfg_run`, already the new value, and `cfg_roll`, the old one.
It runs in the `cfg` job, so a long apply takes several steps.

With `apply N`, the window stays open N seconds. If `confirm` doesn't come,
the same walk runs back to `cfg_roll`.

At boot, the saved config is loaded into the candidate and applied the same
way, synchronously, with `om_booting` set. A check may fix a reference
instead of refusing: a PVID on a missing VLAN goes to 1.
`state.cfg.boot_defaults` names what was fixed.

## The saved config

Binary records in one flash sector, each a field's value keyed by its object
and field ID. Only fields that differ from the default are saved. IDs never
change, so a rename keeps saved configs, and a field whose type changes gets
a new ID (retire the old one in the registry). Unknown IDs on load are skipped
and counted in `state.cfg.load`. Text, the same `path value` lines, is used
everywhere outside the firmware: `get -s`, `GET /config`, `tools/rtlpg_client.py`.

## Steps and time

The firmware runs on a cooperative scheduler, and no step may take more than
5 ms (a flash job's step 10 ms). So:
- the engine prints a dump a line a step (the cli job);
- a save encodes and writes a chunk at a time;
- a check or apply walk does a few instances a step.

A step that bounds its own work checks the clock with `step_time.h`:
`if (STEP_PAST(2000))` means the step has run 2 ms. The simulator runs
banked code about 2x faster than the 4+2 does, so budgets are set with room.

## Help, Tab, the web UI's strings

Each field's `tip=` is its help text. The generator packs the tips into
`help.bin` (`help`, `help <path>`, `help -a`; `OM_HELP=0` leaves it out).
The web UI reads them from the switch with `help -a`, once a session, so the
English is in flash once.

The UI names a field by path (`data-f="mgmt.ip"` in `index.html`, or
`fLabel(fId("mgmt.ip"))` in `app.js`). Its label is, in order:
1. the language's override, by ID (`LANG.de.f["8.2"] = {l: "IP-Adresse"}`; 8.2 is `mgmt.ip`);
2. the schema's `label=`;
3. one made from the field's name and unit (`session_timeout_s` -> "Session timeout (s)").

English is never in the UI's own tables for a field. `test/test_ui_keys.py`
checks that every path and override ID exists.

Tab completion on the console is the engine's (`om_tab()`). The web's CLI page
completes in the browser, from `schema.js`.

## Adding to it

**A config field.** Add a `cfg()` line to its object in `schema/`, for example:

```python
cfg("mtu", U16(64, 16383), 16383, tip="Largest frame accepted, in bytes", unit="B")
```

Then run `python3 tools/om_gen.py -s schema -s output/<board>-compact/sched/om --register`
and commit `schema/registry.txt`. The object's `apply=` handler sees the new
field in `cfg_run`; make it do something with it, and `check=` refuse what the
hardware can't take. Types: `Bool`, `U8/U16/U32(lo, hi)`, `I16()`, `Str(n)`,
`Secret(n)`, `Enum(name, "words")`, `Flags(name, "words")`, `IPv4`, `Mask4`,
`MAC(auto=)`, `Port()`, `PortMask(cpu=)`, `TZ()`. `when=` (`copper`, `sfp`,
`10g`) limits it to some ports.

**An object.** Add `obj("name", apply="om_apply_name", check=..., order=N,
tip=...)` and its fields. Write the handlers in the `om_<area>.c` it belongs
to: `uint8_t om_apply_name(uint8_t idx) __banked`, returning `OM_OK`, or
`OM_ERR` after `om_fail(OM_E_<MESSAGE>)` (declare it with `message()` in the
schema). `idx` is the instance: a logical port, 0..n-1, or a keyed entry's
slot.

**State.** `state("name", get="om_get_name")` and `st()` lines. The getter
fills `om_st.<name>`, a union shared by every state object, for the
instance `idx`, and returns `OM_OK`. Register, as for a field: state IDs are
the UI's string keys.

**An action.** `action("name", "om_do_name", tip=..., arg="number"|"text")` in a
config object.

**Changing a field's type.** Don't: mark the old one `retired` in the registry
and add a new name. The generator refuses a type change.

**Testing.** `make -C test` builds `om.c` on the host, against the real schema
plus `test/om_schema/` (`test_om.c`: verbs, copies, apply, save and load,
help, Tab, at maximum table sizes). In the simulator (ovs-rtlpg's rtlsim), the
whole firmware runs: `tools/ui_sim.py` serves its web UI, and the old-vs-new
harness compares it with the firmware before the object model.

## Build switches

| Switch | Default | What |
|---|---|---|
| `OM_HELP` | 1 | The help text and its code (~3.5 KB of flash outside the banks, 0.9 KB of code). |
| `OM_DBG` | 1 | The debugging commands and `state.dbg` (~3.8 KB of banks). |
| `OM_DBG_LEDS` | 0 | The `leds` dump, with `OM_DBG` (2.5 KB, one long step). |

`om_gen.h` also writes `OM_USE_CHIPDEF` and `OM_SET_BRID`, which compile out
per-chip defaults and parsing a bridge ID while the schema has neither.

## The engine's conventions

`om.c` is written for SDCC on the 8051:
- **Scratch registers.** Locals of a non-leaf function are static xdata, and
  a few scratch registers (`i`, `k`, `v8`, `w`) hold a value only until the
  next call.
- **No second parameter.** Functions take one parameter or none, and pass
  the rest in globals.
- **A cursor.** The engine walks with a cursor (`r_o` object, `cur_f` field,
  the instance), set by `sel_obj()`/`sel_field()`.
- **Named states.** `JH_*` are a path's states, `RF_*` which index levels a
  path gave, `K_*` which copies a keyed walk reads, `J_*` the cli job's modes.
