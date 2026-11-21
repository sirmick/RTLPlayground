# RTLPlayground object model: design

Status: the design as written before and during the work (2026-11-20/21), kept for its reasoning and its tables. Where it and the code differ, the code and om/README.md win.
(`feat/sched`, e435c03). Intended to run as a wash project once agreed.

This is a free-hand refactor of the CLI, the config, the state reporting and the
web UI. Everything a user can set, see or do becomes one tree of paths, described
once in a Python schema. The firmware carries one engine that reads, writes,
prints and parses that tree; the generator produces the C tables, the web UI's
schema, a Python client, the docs and the tests. Most of today's parsers, printers
and JSON pages are deleted.

The evidence behind every inventory below is in [research/](research/):
[config.md](research/config.md) (every config item),
[state.md](research/state.md) (every reported value),
[sched-system.md](research/sched-system.md) (scheduler, health, actions, system info),
[ui-httpd.md](research/ui-httpd.md) (web UI and httpd),
[build-gen-test.md](research/build-gen-test.md) (generator pattern, banking, SDCC, machines, tests).
File:line references in this document point into those files' sources.

Contents

1. Decisions so far
2. The model: config, state, actions
3. The text format
4. The CLI
5. Staging: candidate, apply, confirm, save
6. Config tree: every item
7. State tree: every item
8. Actions: every item
9. Debug namespace
10. The schema
11. The generators
12. The engine (firmware)
13. Tab completion and the line editor
14. The web: one endpoint, and the UI
15. Boot, factory reset, migration of deployed switches
16. What is deleted
17. Size: code, RAM, web flash
18. Testing
19. Work plan (for a wash project)
20. Open decisions

---

## 1. Decisions so far

From the 2026-11-19 notes ([README.md](README.md)) and the 2026-11-20 discussion:

1. **One tree** of paths for config, state and actions. Text form: one line per
   field, `path value`. The console, the web and the saved file all use it.
2. **Config and state are two trees**: `port.3.speed auto` (config) vs
   `state.port.3.speed 2g5` (state). Save, import and diff only ever touch the
   config tree.
3. **Verbs, not commands.** The CLI is `get set del diff apply confirm discard
   load save do`. Everything else is a path. Reset and factory reset are actions
   (`do sys.reset`, `do sys.factory_reset`); `^R ^R` stays in the serial ISR.
4. **Staged config.** `set` edits a candidate; `apply` makes it live (checked as a
   whole, applied object by object); `apply 60` rolls back unless `confirm`ed;
   `save` writes running to flash.
5. **Generated, not hand-written**: structs, tables, dispatch, schema.js, the
   Python client, docs and most tests come from the schema.
6. **Tab completion** from the same tables.
7. **The saved config is binary on flash, text everywhere else** (2026-11-20;
   confirms decision 3 of 2026-11-19). Tagged records keyed by stable numeric IDs
   from the path registry, defaults omitted (15.3). Nobody sees the binary: `get -s`,
   the web download, backups and restores are text. Reason: a text dump can exceed
   the 4 KiB sector (~6 KiB worst case); the binary worst case is ~2.1 KB, and its
   table-driven codec is smaller than parsing text at boot.
8. **Old configs are converted on the host** by the Python client
   (`rtlpg_client.py convert`); the firmware has no legacy importer (15.2).
9. **Big change, fork first**; prove it on one object (`port`), measured, before
   migrating the rest.
10. Languages (i18n) are out of scope.

---

## 2. The model

```
config tree     (staged; saved)   sys.* mgmt.* dns.* ntp.* syslog.* web.*
                                  port.N.* vlan.V.* lag.L.* stp.* stp.port.N.* stp.lag.L.*
                                  igmp.* lldp.* loop.* mirror.*
state tree      (read-only)       state.sys.* state.mgmt.* state.port.N.* state.sfp.S.*
                                  state.l2.K.* state.stp.* state.lag.L.* state.lacp.port.N.*
                                  state.loop.* state.ntp.* state.dns.* state.web.*
                                  state.sched.* state.health.* state.con.* state.cfg.* state.fw.*
                                  state.loader.*
actions         (do)              sys.reset sys.factory_reset mgmt.dhcp_renew port.N.cabletest
                                  l2.flush l2.delete stp.counters_clear stp.port.N.mcheck ...
debug tree      (debug builds)    dbg.reg.* dbg.phy.* dbg.sds.* dbg.flash.* ...
```

- **Objects** are singletons (`sys`), numbered (`port.N`, `lag.L`, `sfp.S`: fixed
  count) or keyed (`vlan.V`: entries exist or don't, keyed by value). Objects
  nest (`stp.port.N`, `state.port.N.cabletest.pair.a`).
- **Fields** have a type (section 3.2), an access class (`cfg`, `secret`, `state`,
  `action`) and, for config, a default.
- **Port numbers are the front-panel numbers** (1-based), as every surface prints
  them today; the engine maps them through `machine.phys_to_log_port[]`. The CPU
  port appears only inside port masks, as `cpu`.
- Which ports, SFP slots and 10G ports a board has is runtime data
  (`machine.n_sfp`, `machine.n_10g`, `is_sfp[]`; build-gen-test.md section 3), exposed as
  `state.sys.ports`, `state.sys.sfp_ports`, `state.sys.ports_10g`. Fields carry a
  `when` rule (copper only, SFP only, 10G only) that the engine checks at run time;
  the tables are identical for every board of a chipset.

---

## 3. The text format

### 3.1 Lines

```
line     = path SP value          a field and its value
         | path                   an action with no argument, or an object (get/del)
path     = seg ("." seg)*
seg      = name | index | "*"     name: [a-z][a-z0-9_]*, index: decimal, "*": wildcard
value    = the rest of the line, trimmed; its syntax depends on the field's type
comment  = "#" ...                 files only; ignored
```

- Strings run to the end of the line: no quoting, no escapes. `"` and `\` are
  refused in strings (today they become `.`); control characters are refused.
- Output is canonical: one space after the path, values in the forms below,
  objects in schema order, indices ascending. So `get` output can be replayed,
  diffed and parsed by a one-screen parser.
- A command's output ends with one status line: `ok`, or `err <path>: <reason>`.
  Events printed by finished actions are `event <path> <value>` (console only).

### 3.2 Types and their text

| Type | Text | Notes |
|---|---|---|
| `Bool` | `on` / `off` | |
| `U8`, `U16`, `U32(min,max)` | decimal | range checked; `err ...: 1-4094` |
| `U64` | decimal | counters; the UI parses with BigInt |
| `I16(min,max)` | `-12`, `+5` | |
| `Scaled(base, num, den, decimals)` | `41.5` | a raw integer shown in engineering units, e.g. chip temp raw/128 C; SFP mV, mA, dBm×100 |
| `Enum(words)` | one word | `err ...: auto 10m 100m 1g 2g5 5g 10g` |
| `Flags(words)` | words joined by `,`, or `none` | LAG hash fields, STP flags, LACP state bits |
| `Str(n)` | to end of line | printable ASCII, max n |
| `Secret(n)` | write-only | never printed by `get` (prints `*`), written by `save` |
| `IPv4` | `a.b.c.d` | |
| `Mask4` | `a.b.c.d` | contiguous check |
| `MAC` | `aa:bb:cc:dd:ee:ff` (accepts `-`) | |
| `PortMask` | `1-4,9`, `cpu`, `none`, `all` | front-panel numbers; `cpu` where the field allows it |
| `Port` | `3` | one port; `none` where allowed |
| `TZ` | `+05:30` | I16 minutes inside |
| `BridgeId` | `8.20:7c:14:a2:25:fa` | priority/4096 . MAC |
| `Time` | `2026-11-20T21:04:00Z` | NTP time state |
| `Dur(unit)` | decimal in the unit named by the path (`_s`, `_ms`, `_us`) | |

Numbers of any width are printed and parsed with one byte-wise routine (8-bit
operations only: repeated divide-by-10 over an N-byte little-endian value,
multiply-by-10-and-add on parse). The same routine does `Scaled` (multiply by a
small constant, divide by a small constant). No 32-bit arithmetic on step paths
(memory.md rule 3), no float.

### 3.3 Examples

```
> get port.3
port.3.name uplink
port.3.enabled on
port.3.speed auto
port.3.duplex auto
port.3.mtu 16383
port.3.pvid 10
...
ok
> get state.port.3
state.port.3.link up
state.port.3.speed 2g5
state.port.3.duplex full
state.port.3.rx_good 1234567
...
ok
> set port.3.pvid 5000
err port.3.pvid: 0-4094
```

---

## 4. The CLI

### 4.1 Verbs

| Verb | Does |
|---|---|
| `get [path]` | Print every field under `path`, config from **running**. No path: list the top-level objects. `get -c path`: from the candidate. `get -s path`: from saved (flash). `get -d path`: only fields that differ from the default (the form `save` writes). |
| `set path value` | Change the candidate. Creates a keyed entry if needed (`set vlan.20.members 3-4`). Wildcards allowed (`set port.*.eee off`). Checks the value's type and range at once; cross-field checks wait for `apply`. |
| `del path` | A field: back to its default in the candidate. A keyed entry: removed (`del vlan.20`). |
| `diff [path]` | Candidate vs running, as `- path old` / `+ path new` lines. `diff -s`: running vs saved (what `save` would change). |
| `apply [seconds]` | Check the whole candidate; apply the changed objects in schema order, as a job; with `seconds`, open a confirm window (5.3). |
| `confirm` | Close the confirm window, keeping the change. |
| `discard` | Candidate = running. |
| `load saved\|default` | Candidate = the saved config, or the factory default. Then `diff` / `apply`. |
| `save` | Running to flash, as a flash job. Refused while a confirm window is open, or while candidate differs from running (`err save: unapplied changes, apply or discard`). |
| `do path [arg]` | Run an action now (section 8). Never staged, never saved. |

Ten words. The verbs take at most one option letter; there is no other syntax.

### 4.2 Top-level operations

| Want | Do | Notes |
|---|---|---|
| reboot | `do sys.reset` | records cause `cmd` in the crash record (today the `reset` command records nothing, sched-system.md A.4) |
| hard reset, from anywhere | `^R ^R` on the serial console | stays in `isr_serial()` (rtlplayground.c:189), works with the main loop stuck |
| factory reset | `do sys.factory_reset` | the flash job writes the default config, then resets (today's `FL_FACTORY`); or `load default` + `apply` + `save` to see the diff first |
| save | `save` | |
| revert unsaved changes | `load saved` + `apply` | |
| reset button | > 3 s: `sys.reset` (cause `button`); > 10 s: `sys.factory_reset` (cause `button`) | unchanged behaviour; held at boot: unchanged (boot.c:66) |
| firmware update | web upload (unchanged) | progress as `state.fw.*` |

### 4.3 Errors

One line, always `err <path>: <reason>`, generated from the tables:

- unknown name: `err port.3.x: name enabled speed duplex mtu pvid ...` (the
  children at that level);
- bad index: `err port.12: 1-9`; `err vlan.20: no such vlan` (for `get`/`del`);
- bad value: the type's allowed form (`1-4094`, the enum words);
- `when` rule: `err port.9.eee: not on sfp ports`;
- apply check: `err apply: vlan.10.tagged not in members` (5.2).

No usage strings in flash: `get` is the reference, the tips live in schema.js and
the generated docs.

### 4.4 Wildcards and aliases

- `*` in an index position: `get state.port.*.link`, `set port.*.eee off`,
  `do port.*.cabletest` (an action declared `multi` queues one run per index).
- No aliases. The old command names (`reset`, `vlan show`, ...) are gone; the
  error for an unknown verb lists the ten verbs.

---

## 5. Staging

### 5.1 Four configs

| Name | Where | What |
|---|---|---|
| **saved** | flash, `CONFIG_START` (0x7f000 compact, 0x70000 standard) | what boot loads |
| **running** | `cfg_run`, xdata | what the hardware has |
| **candidate** | `cfg_cand`, xdata | edits not yet applied |
| **rollback** | `cfg_roll`, xdata | running before the last apply; valid until confirmed |

Plus `cfg_default`, a `__code` image generated from the schema defaults, overlaid
at build time with the board's default config file (15.1).

One candidate, shared by the console and every web session. `state.cfg.pending`
(number of changed fields) is shown in the console prompt (`spare2*>`) and in the
UI, so a second user sees staged work. Per-session candidates would cost a struct
per session and a merge rule; not proposed.

### 5.2 apply

1. **Check the whole candidate.** Every field is already type- and range-checked
   by `set`; `apply` runs the cross-field and cross-object checks (6.20) over the
   candidate as a whole, so the order fields were typed in doesn't matter. First
   failure: `err apply: <rule>`; nothing changes.
2. **Find changed objects**: per object instance, `memcmp(cand, run)`.
3. `cfg_roll = cfg_run`; `cfg_run = cfg_cand`.
4. **Apply** the changed objects as a scheduler job (`TASK(cfg, job, once)`), in
   the schema's apply order (6.21), one object instance per step (objects whose
   apply is long carry their own cursor, like `port_eee_line()` and
   `stp_fdb_line()` today). Each object's `*_apply(idx)` reads `cfg_run` and
   programs the hardware.
5. `state.cfg.apply` moves `idle → running N/M → done | failed: <obj>`. A failed
   object (a hardware timeout) restores `cfg_run = cfg_roll` and re-applies the
   objects already done.

`set` + `apply` in one console line or one web POST is common; the web UI's
"Apply" posts the lines and `apply 30` together.

### 5.3 apply with a confirm window

`apply 60`: as above, then the job sleeps up to 60 s (`STEP_SLEEP`, fits one wake;
sched-system.md A.6). `confirm` wakes it and drops the rollback. On timeout the
job sets `cfg_cand = cfg_run = cfg_roll` and applies every changed object again.
`state.cfg.confirm_s` counts down.

The web UI applies with a window, reloads `state.mgmt` from the (possibly new)
address, and posts `confirm` when it gets through. A change that cuts the browser
off (management VLAN, IP, own uplink port, isolation of the CPU) is never
confirmed and rolls back by itself. Cross-origin: when the address changed, the
UI navigates to the new address and confirms from there (the CSP allows only
same-origin requests; ui-httpd.md 5.3).

A reset during the window boots the saved config, which predates the change;
that is why `save` is refused while a window is open.

### 5.4 save, load, boot

- `save`: a new flash-job kind `FL_SAVE`: a sizing pass (the encoder run without
  writing, to get the length and CRC for the header), erase `CONFIG_START`, then
  each `FL_PROG` step encodes the next 512 B of binary records (15.3) into
  `flash_buf` and programs it (no 4 KiB buffer; sched-system.md A.6), then a
  read-back CRC check. `state.cfg.save` shows progress; `state.cfg.saved_bytes`
  the size.
- `load saved|default`: decode the binary sector into the candidate, 256 B of
  flash a step, as a job. Unknown objects or fields are skipped by their length and
  counted (`state.cfg.load.skipped`, the first in `state.cfg.load.first_skipped` as
  `obj_id.field_id`); a value outside the field's range keeps the default and
  counts as skipped. A bad CRC or magic loads nothing (`state.cfg.format bad`).
- **Boot**: `cfg_run = cfg_cand = cfg_default`, `load saved` run to completion
  before the scheduler starts (blocking is fine there, as `execute_config()` is
  today), then apply what differs from the defaults, in schema order
  (2026-11-20, phase 2: the hardware after reset + init code *is* the default
  state, so boot re-programs no PHY and bounces no link). Every schema default
  must therefore equal the chip's state after reset + init; old-vs-new checks it
  field by field against the registers, and a field whose init state isn't known
  gets a per-chipset default or is programmed by its handler at boot. Recovery boot (`loader_recovery()`)
  loads `default` instead, as today. A sector that is blank, bad or old-format
  text boots on `default` and is left untouched until `save` (15.2).
- **Boot's checks** (P4-4b, the orchestrator's call after review3): apply's checks
  run on the loaded candidate first. An object a check refuses (values that pass
  each field's range but not this build's cross-field rule, e.g. stp timers from
  another release) goes back to its defaults in the candidate, is named in
  `state.cfg.boot_defaults` (its pattern: `stp`, `stp.port.#`), and the checks run
  again; the rest applies, so a saved mgmt survives. An object that refuses at its
  defaults (it depends on another's values) applies nothing: `Boot config not
  applied`, `state.cfg.apply rolled_back`, as before. The saved sector is untouched.
  P4-4c (after review4): the reference checks fix their field instead, at boot
  only: a `mgmt.vlan` naming a missing VLAN becomes 0 (any), a `port.N.pvid` 1;
  both are named in `boot_defaults` (`mgmt.vlan`, `port.#.pvid`) and the object
  isn't reset. If the fix still names nothing (VLAN 1 itself deleted), the check
  names the referenced object (`om_boot_ref = O_CFG_VLAN`) and boot resets that
  one (VLAN 1 back) before checking again. A live apply still refuses.
- `get -s` needs no buffer: each record names its object, index and field, so the
  decoder prints one `path value` line per record as it reads the sector (the
  non-default fields, the same lines `get -d` gives for running). `GET /config`
  streams the same text, password omitted.
- `state.cfg.dirty` = running differs from saved (tracked by a flag set on apply
  and cleared on save/load, plus a compare at boot).

---

## 6. Config tree: every item

66 fields today ([research/config.md](research/config.md), section 18), plus the
ones the refactor adds or splits. "Today" is the command that sets it now; "store"
is where it lives now ("reg" = only in a hardware register, no RAM copy). In the
new design every field lives in `cfg_run`, and the module's apply function
programs the register from it. Defaults marked *derived* are computed at boot
when the field is empty (10.3).

### 6.1 sys

| Path | Type | Default | Today | Store today | Notes |
|---|---|---|---|---|---|
| `sys.hostname` | Str(23), no space | *derived* `RTLPlayground-<mac[3..5]>` | `hostname <name>` | `hostname[24]` | used by DHCP opt 12, LLDP |
| `sys.mac` | MAC or `auto` | `auto` (flash at `mac_flash_offset`, else LAA from chip id) | `mac <mac>` | `uip_ethaddr` | not in CONF_CMDS today (never saved by the UI); apply moves the management L2 entry |
| `sys.led` | Enum{on, ports_off, off} | on | `led on\|off [ports]` | `leds_mode` | applied after the system LED init |

### 6.2 web (login)

| Path | Type | Default | Today | Store | Notes |
|---|---|---|---|---|---|
| `web.password` | Secret(20), no space | `1234` | `passwd <pw>` | `passwd[21]` | apply logs out every session; `get` prints `*`; `save` writes it; `get -s` and the web's download omit it (20.6) |
| `web.session_timeout_s` | U16(60..65535) | 3600 | `session <s>` | `session_timeout` | |

### 6.3 mgmt (the management interface)

| Path | Type | Default | Today | Store | Notes |
|---|---|---|---|---|---|
| `mgmt.mode` | Enum{static, dhcp} | static | `ip dhcp` / `ip <addr>` | `dhcp_state.state` | today both config and action; renew is now `do mgmt.dhcp_renew` |
| `mgmt.ip` | IPv4 | 192.168.2.2 (code); factory file 192.168.10.247 | `ip <addr>` | `uip_hostaddr` | kept, unused, in dhcp mode (the static fallback) |
| `mgmt.netmask` | Mask4 | 255.255.255.0 | `netmask` | `uip_netmask` | |
| `mgmt.gateway` | IPv4 | 192.168.2.22 (code) | `gw` | `uip_draddr` | |
| `mgmt.vlan` | U16(0..4094), 0 = any | 1 | `vlan <vid> mgmt` | `management_vlan` | check: VLAN exists and has the CPU, unless 0 |

### 6.4 dns, ntp, syslog

| Path | Type | Default | Today | Store |
|---|---|---|---|---|
| `dns.server1` | IPv4, 0.0.0.0 = none | 0.0.0.0 | `dns server a [b]` | `dns_state.server[0]` |
| `dns.server2` | IPv4 | 0.0.0.0 | (same) | `dns_state.server[1]` |
| `ntp.enabled` | Bool | off | `ntp on\|off` | `ntp_state.enabled` |
| `ntp.server` | Str(47), host or IP | `pool.ntp.org` | `ntp server` | `ntp_state.server` |
| `ntp.interval_min` | U16(1..1440) | 60 | `ntp interval` | `ntp_state.interval` |
| `ntp.timezone` | TZ(-12:00..+14:00, mm in 00/30/45) | +00:00 | `ntp timezone` | `ntp_state.offset` |
| `ntp.dst` | Enum{off, eu, us} | off | `ntp dst` | `ntp_state.dst` |
| `syslog.enabled` | Bool | off | `syslog on\|off` | `syslog_state.enabled` |
| `syslog.server` | IPv4 | 0.0.0.0 | `syslog ip` | `syslog_state.server_ip` |
| `syslog.port` | U16(1..65535) | 514 | `syslog port` | `syslog_state.server_port` |

Apply: ntp and syslog stop, take the new fields, then start if enabled (today the
UI orders the commands to get this; config.md section 5).

### 6.5 port.N (N = 1..ports)

| Path | Type | Default | Today | Store | Notes |
|---|---|---|---|---|---|
| `port.N.name` | Str(31) | "" | `port N name [text]` | `port_names[9][32]` | LLDP port description |
| `port.N.enabled` | Bool | on | `port N on\|off` | reg (PHY power-down) | **split** from speed (today `on` = enabled+auto). On SFP ports: SerDes off (new; today silently ignored) or `when` copper until implemented (20.2) |
| `port.N.speed` | Enum{auto, 10m, 100m, 1g, 2g5, 5g, 10g} | auto | `port N <speed>`; SFP: `sfp S <speed>` | reg (PHY AN/forced) / `sfp_speed[]` | `when`: 5g/10g only on 10G ports; SFP ports take auto/100m/1g/2g5/10g (today's `sfp S` command, now on the port; 20.2). Apply fixes the sticky `is10g_port` bug (rtl837x_phy.c:281) |
| `port.N.duplex` | Enum{auto, full, half} | auto | `port N 10m half`, `port N duplex` | reg | check: half only with 10m/100m. Not in CONF_CMDS today (lost on save) |
| `port.N.mtu` | U16(64..16383) | 16383 | `mtu N v` | reg `MAC_L2_PORT_MAX_LEN` | |
| `port.N.pvid` | U16(0..4094) | 1 | `pvid N v` | reg | 0 parks a tagged-only port. Check: VLAN exists (unless 0). LAG members: see 6.20 |
| `port.N.ingress` | Enum{all, tagged, untagged} | all | `ingress ...` | reg `RTL837x_REG_INGRESS` | acceptable frame types |
| `port.N.ingress_filter` | Bool | on | (none) | reg `VLAN_PORT_IGR_FLTR` | new: exposes today's fixed default |
| `port.N.lldp` | Enum{permit, block} | permit | `port N lldp ...` | `lldp_logical_port_status` | |
| `port.N.eee` | Bool | on | `eee on\|off N` | reg + PHY MMD7 | `when` copper |
| `port.N.eee_speeds` | Flags{100m, 1g, 2g5, 5g, 10g} | all (P4-6: capped at apply) | `eee on N <spd>` | reg | P4-6: no check; apply advertises the speeds the port has (a 2.5G port's 5g/10g ignored) |
| `port.N.isolate` | PortMask with cpu | all + cpu | `isolate N ...` | reg | egress allow-list. Check: warning-free, but `apply` with a window is advised when the CPU is removed |
| `port.N.bw_in_kbps` | U32(0, 16..10000000), 0 = off, 16 kbit steps | 0 | `bw in N <hex>` (hex!) | reg | decimal now |
| `port.N.bw_in_mode` | Enum{fc, drop} | drop (P4-6) | `bw in N fc\|drop` | reg | apply sets rate before mode. drop: the chip's power-on FC bit (the boot writes nothing new) |
| `port.N.bw_out_kbps` | U32 as above | 0 | `bw out N <hex>` | reg | |
| `port.N.storm.T.rate` (T = bcast, mcast, ucast, umcast) | U32, 0 = off | 0 | `storm N T <r> pps\|kbps`, `... off` | reg meter | pps 1..1048575, kbps 1..10000000 |
| `port.N.storm.T.unit` | Enum{pps, kbps} | pps | (same) | reg | |

**Deviations (P4-6, shipped):** `bw_in_mode` defaults to `drop`, the chip's FC bit after
power-on and init, so the boot writes nothing new (the old `bw in N <rate>` turned fc on: a
translated old config says fc). `eee_speeds` defaults to every speed and is capped by the
port's at apply, with no check: one default for every port (the engine has no per-port
default). An SFP port's `speed` takes the old `sfp S` words, auto/100m/1g/2g5/10g. The
bandwidth limits are 0 or a multiple of 16 (16 kbit/s steps), refused otherwise. The boot
writes every limit and meter, off as the power-on value (unconfirmed: P8 silicon read).
`port.N.storm.T` is a child object (ID 18).

### 6.6 vlan.V (keyed, V = 1..4094, at most `MAX_VLANS`)

| Path | Type | Default | Today | Store | Notes |
|---|---|---|---|---|---|
| `vlan.V` | keyed entry | VLAN 1 exists: all ports untagged, CPU tagged | `vlan V ...`, `vlan V d` | HW table + `vlan_bits[]` | `del vlan.V` removes. VLAN 1 may be deleted (as today) |
| `vlan.V.name` | Str(15), starts with a letter | "" | 2nd word of `vlan V name ...` | `vlan_names[1024]` | renaming no longer needs the membership resent; the `vlan 10 dmz` = delete quirk disappears |
| `vlan.V.members` | PortMask | VLAN 1: all | the port list | HW table | the CPU is always a tagged member (forced by apply, as today) |
| `vlan.V.tagged` | PortMask | none | `3t` suffix | HW table | check: tagged ⊆ members |

`MAX_VLANS` is a schema constant (proposed 32; 20.7). The 1 KiB packed name table
goes; names live in the struct.

### 6.7 lag.L (L = 1..4)

| Path | Type | Default | Today | Store | Notes |
|---|---|---|---|---|---|
| `lag.L.mode` | Enum{off, static, lacp} | off | `lag L ...`, `lag L lacp ...`, `lag L d`, `lacp off` | HW trunk reg / `lacp_lag_ports[]` | `lacp off` (all LAGs) becomes `set lag.*.mode off` |
| `lag.L.members` | PortMask | none | the port list | as above | check: a port in at most one LAG (today static doesn't check) |
| `lag.L.hash` | Flags{spa, smac, dmac, sip, dip, sport, dport} | smac,dmac,sip,dip,sport,dport | `laghash L ...` | reg | |

Fixed, not exposed (could be later): LACP system priority 0xffff, port priority
0xff, key L+1, active mode, fast timeout.

### 6.8 stp, stp.port.N, stp.lag.L

| Path | Type | Default | Today | Store |
|---|---|---|---|---|
| `stp.enabled` | Bool | off | `stp on\|off` | `stp_enabled` |
| `stp.version` | Enum{rstp, stp} | rstp | `stp version` | `stp_rstp` |
| `stp.priority` | U8(0..15), ×4096 | 8 | `stp prio` | `stp_prio` (n<<4) |
| `stp.hello_s` | U8(1..10) | 2 | `stp hello` | `stp_hello_s` |
| `stp.maxage_s` | U8(6..40) | 20 | `stp maxage` | `stp_maxage_s` |
| `stp.fwd_delay_s` | U8(4..30) | 15 | `stp fwd` | `stp_fwddelay_s` |
| `stp.txhold` | U8(1..10) | 6 | `stp txhold` | `stp_txhold` |
| `stp.pathcost` | Enum{long, short} | long | `stp pathcost` | `stp_pcost_short` |
| `stp.bpdu` | Enum{flood, filter} (when STP is off) | flood | `stp bpdu` | `stp_bpdu_filter` |

Check: `2*(fwd-1) >= maxage >= 2*(hello+1)` (today only enforced outside boot).

Per STP entity: `stp.port.N.*` (N = 1..ports) and `stp.lag.L.*` (L = 1..4):

| Field | Type | Default | Store today |
|---|---|---|---|
| `enabled` | Bool | on | `stp_pflags` 0x01 |
| `edge` | Enum{auto, on, off} | auto | `stp_pflags` 0x02/0x04 |
| `cost` | U32(0..200000000), 0 = auto | 0 | `stp_pcost[]` |
| `priority` | U8(0..240), multiple of 16 | 128 | `stp_pprio[]` |
| `guard` | Enum{none, bpdu, root} | none | `stp_pflags` 0x08/0x10 |
| `filter` | Bool | off | `stp_pflags` 0x20 |
| `p2p` | Enum{auto, on, off} | auto | `stp_pp2p[]` |

`stp.port.N.*` of a LAG member is kept but has no effect while the port is in a
LAG (today it is refused, so config order mattered at boot); the LAG's
`stp.lag.L.*` applies. `stp_pflags` today mixes config bits with two runtime bits
(OPEREDGE, TRIPPED): the struct holds only the config; the runtime bits are state.

Actions (P4-4): `do stp.counters_clear` (was `stp clear`); per entity
`do stp.port.N.mcheck` / `stp.lag.L.mcheck`, and `clear_guard`, which brings back
an entity BPDU guard disabled (listening, or forwarding if an admin edge;
orchestrator's call: the old `stp port N on` also did it, a set of an unchanged
value doesn't). `enabled` off then on clears it too.

### 6.9 igmp, lldp, loop, mirror

| Path | Type | Default | Today | Store |
|---|---|---|---|---|
| `igmp.enabled` | Bool | off | `igmp on\|off` | `igmpEnabled` |
| `lldp.enabled` | Bool | off | `lldp on` / anything else = off (quirk dropped) | `lldp_enabled` |
| `loop.enabled` | Bool | off | `loop on\|off` | `rldp_enabled` |
| `mirror.enabled` | Bool | off | `mirror ...` / `mirror off` | reg `MIRROR_CTRL` |
| `mirror.port` | Port | none | first arg | reg |
| `mirror.rx` | PortMask | none | `p` / `pr` | reg |
| `mirror.tx` | PortMask | none | `p` / `pt` | reg |

Check: `mirror.port` not in `rx`/`tx`; `mirror.enabled` needs a port.

### 6.10 Not config (kept fixed, listed so apply re-creates them)

Applied by init code, not fields (config.md section 14): global bandwidth control,
VLAN egress tag mode, 4k VLAN mode, learning, L2 learn limit 0x1040, flood
action, IGMP flood masks, trap CPU port, the static management L2 entry (derived
from `sys.mac` + `mgmt.vlan`), LED sets and mux, machine GPIO/SerDes setup. PoE has
no config (firmware load only); there is no fan control.

### 6.20 Cross-field and cross-object checks (run by `apply`)

1. `vlan.V.tagged ⊆ vlan.V.members`.
2. `port.N.pvid` names an existing VLAN, or 0.
3. `mgmt.vlan` names an existing VLAN that has the CPU, or 0.
4. A port is in at most one `lag.L.members`; members of one LAG share one
   `pvid` (apply writes it to all members, as `port_pvid_set()` does today; the
   check rejects members with different PVIDs instead of silently spreading one).
5. STP timers: `2*(fwd_delay-1) >= maxage >= 2*(hello+1)`.
6. `port.N.duplex half` only with `speed 10m|100m`.
7. `port.N.speed` within the port's capability (`when` rules): 5g/10g only on 10G ports; an SFP
   port takes auto, 100m, 1g, 2g5, 10g (P4-6: the old `sfp S` words; no 10m or 5g). `eee_speeds`
   has no check (P4-6): apply caps it by the port's speed.
8. `mirror.port` not a source; enabled needs a port.
9. `bw_*_kbps` 0 or 16..10000000; storm rate within the unit's range.
10. The saved dump fits `CONFIG_LEN` (checked by `save`, reported by `apply` as a
    warning when close).

### 6.21 Apply order

The schema lists objects in this order; apply-all (boot, rollback) and apply-changed
both follow it:

`sys` (0) → `vlan` (8) → `mgmt` (10) → `dns` → `ntp` → `syslog` → `web` → `lag` (35) →
`port` (40; pvid after VLAN and LAG; bw rate before mode) → `stp` → `stp.lag` →
`stp.port` → `igmp` (60) → `lldp` → `loop` → `mirror` (66). As built: `sys` is one
object at order 0, hostname and led together; `vlan` comes before `mgmt` so a new
VLAN exists before `mgmt.vlan` points at it (P4-2, 31c7d44). om_gen sorts config
objects by `order=` (stable, a child at its parent's order, the parent first).

Count: 72 config fields (66 today + `ingress_filter`, `eee_speeds`, `mgmt.mode` as a
field, the split of `port N on` into `enabled` and `speed`, `lag.L.mode` off, the
storm `unit`), about 1.5 KiB per struct copy (17.2).

---

## 7. State tree: every item

From [research/state.md](research/state.md) (about 150 fields plus 87 MIB counters)
and [research/sched-system.md](research/sched-system.md) (scheduler and system,
about 60). "Shown today" lists the surfaces, so the duplicates this removes are
visible (DUP = on two or more surfaces today). Config echoed back as state today
(about 60 fields, state.md section 16) is **not** state: it is read with `get` on
the config tree. Getter cost: free (a global), reg (~1 µs), MDIO (~161 µs each),
STAT (~0.1 ms), TBL (table op), I2C (never in a getter: the sfp task caches).

### 7.1 state.sys

| Path | Type | Source | Shown today | Cost |
|---|---|---|---|---|
| `state.sys.version` | Str | `VERSION_SW` | information.json, `version`, boot, LLDP (DUP) | free |
| `state.sys.build_date` | Str | `BUILD_DATE` | information.json, `version`, boot (DUP) | free |
| `state.sys.build_id` | U32 hex | `CRASH_IMAGE_HI/LO` | crash record only | free |
| `state.sys.toolchain` | Str | `SDCC_TOOLCHAIN` + SDCC version | `version` | free |
| `state.sys.machine` | Str(30) | `machine.machine_name` | information.json hw_ver, `version` (DUP) | free |
| `state.sys.pcb` | Str | `MACHINE_PCB_INFO` | information.json pcb_ver | free |
| `state.sys.chip` | Enum{rtl8372, rtl8373} | `machine_detected.isRTL8373` | boot only | free |
| `state.sys.chip_n` | Bool | `machine_detected.isN` | boot only | free |
| `state.sys.chip_ok` | Bool | detected == `machine.isRTL8373` | boot "INCORRECT MACHINE!" | free |
| `state.sys.chip_rev` | U8 | `CHIP_INFO`, cached at boot | boot (prints the byte twice: bug) | free (cached) |
| `state.sys.ports` | PortMask | machine maps | none | free |
| `state.sys.sfp_ports` | PortMask | `machine.is_sfp[]` | ports.json isSFP | free |
| `state.sys.ports_10g` | PortMask | `machine.n_10g` | none | free |
| `state.sys.layout` | Enum{compact, standard} | `LAYOUT_COMPACT` | none (implied by img_size) | free |
| `state.sys.image_size` | U32 | `IMAGE_SIZE` | information.json img_size | free |
| `state.sys.uptime_s` | U32 | `REG_SEC_COUNTER` | information.json (hex), `health` (DUP) | reg |
| `state.sys.ticks` | U32 | `ticks` | `health` | free |
| `state.sys.mac` | MAC | `uip_ethaddr` (effective, after `auto`) | information.json, stp.json, boot (DUP) | free |
| `state.sys.mac_source` | Enum{flash, derived, config} | boot logic, stored | none | free |
| `state.sys.temp_c` | Scaled(raw/128, 1 decimal) | `TM_RESULT` | information.json (string), `temp` (DUP) | reg |
| `state.sys.temp_poweron_c` | Scaled | `TM_RESULT_POWERON` | `temp` | reg |
| `state.sys.flash.size` | U32 bytes | `flash_size` | information.json, boot, `flash j` (DUP) | free |
| `state.sys.flash.jedec` | 3 × hex byte | JEDEC id, now cached at boot | boot, `flash j` | free (cached) |
| `state.sys.flash.sio` | Bool | `machine.flash_sio` | none | free |
| `state.sys.poe` | Enum{none, ok, no_image, no_answer, no_chip_image, failed, no_room} | `poe_state` (MACHINE_POE builds) | information.json poe, boot | free |
| `state.sys.button` | Bool | `button_last` | events only | free |
| `state.sys.button_held_s` | U8 | house task | events only | free |

### 7.2 state.sys.reset (the crash record) and state.sys.wdt

Every reset path is routed through `crash_reset(cause)` (new causes: `cmd`, `web`,
`button`, `factory`, `update`), so the last reset always has a cause (today only the
watchdog and ^R^R record one; sched-system.md A.4).

| Path | Type | Source | Shown today |
|---|---|---|---|
| `state.sys.reset.cause` | Enum{power, watchdog, ctrl_r, cmd, web, button, factory, update} | `crash_last.cause`, `power` when no valid record | boot "Last reset", `health tasks` (DUP) |
| `state.sys.reset.slot` | Str (slot name) | `crash_last.slot` | same |
| `state.sys.reset.state` | Str (state name) | `crash_last.state` | same |
| `state.sys.reset.step_ms` | U16 | (tick - step_tick) × 5 | same |
| `state.sys.reset.pc` | U16 hex, 0 = not from an ISR | `crash_last.pc` | same |
| `state.sys.reset.bank` | U8 | `crash_last.bank` | same |
| `state.sys.reset.sp` | U8 | `crash_last.sp` | none |
| `state.sys.reset.states` | 8 × state name, oldest first | `crash_last.ring[]` | same |
| `state.sys.reset.count` | U8 | `crash_prior` (since power-on) | none |
| `state.sys.wdt.timeout_ms` | U16 | `SWDT_TICKS` × 5 | none |

(The update cause must survive the image change: the record's image check is
relaxed for that one cause; sched-system.md, open item 1.)

### 7.3 state.sched (the scheduler) and state.health

Names come from generated tables: the scheduler generator adds `task_name[]` and
`state_task[]` beside `slot_name[]`/`state_name[]` (sched-system.md A.1), so a job
slot shows which task holds it.

| Path | Type | Source | Shown today |
|---|---|---|---|
| `state.sched.slot.<slot>.task` | Str | `state_task[state]` | `health tasks` (slot name only) |
| `state.sched.slot.<slot>.state` | Str, `none` for a free slot | `sched_task[i].state` | `health tasks` |
| `state.sched.slot.<slot>.mask` | Flags{rx, link, con_rx, con_tx, forever} | `sched_task[i].mask` | `health tasks` (hex) |
| `state.sched.slot.<slot>.wake` | U16 ticks ahead, 0 = due | `wake - sched_now` | `health tasks` |
| `state.sched.slot.<slot>.arg` | U16 | `sched_task[i].arg` | none |
| `state.sched.state.<STATE>.runs` | U16 (saturating) | `sched_st_runs[]` | `health tasks` |
| `state.sched.state.<STATE>.max_us` | U32 (from fine units × 6.144; `>400000` saturated) | `sched_st_max[]` | `health tasks` (µs/ms text) |
| `state.sched.task.<task>.max_us` | U32 | max over the task's states | none |
| `state.sched.passes` | U32 | `sched_passes_hi/lo` | `health tasks` (hex) |
| `state.sched.idle` | U32 | `sched_idle_hi/lo` | `health tasks` |
| `state.sched.pass_max_us` | U32 | `sched_pass_max` | `health tasks` |
| `state.sched.overruns` | U16 | `sched_overruns` | `health tasks` |
| `state.sched.overrun_last` | Str (state) | `sched_overrun_state` | `health tasks` |
| `state.sched.slots` / `.fixed` | U8 | `SCHED_SLOTS` 24 / `T_FIXED` 12 | none |
| `state.sched.step_limit_ms` / `.tick_ms` | U8 | 50 / 5 | none (deferred in P5-2: compile-time constants, not worth the bank bytes yet) |
| `state.sched.fine_per_tick` | U16 | `sched_fine_per_tick` | none |
| `state.health.hw_timeouts` | U16 | `hw_timeouts` | `health tasks` |
| `state.health.hw_timeout_reg` | U16 hex | `hw_timeout_reg` | `health tasks` |
| `state.health.stack_headroom` | U8 | 0xa5 paint above SP (painted in every build: 20.10) | `health` (HEALTH=1 only) |
| `state.health.rx_frames` | U16 | `health_rx_frames` | `health` (HEALTH=1) |
| `state.con.rx_overruns` | U16 | `con_rx_overruns` | `health tasks` |
| `state.con.rx_used` / `.tx_free` | U8 / U16 | ring heads/tails | none |
| `state.web.tcp.*` (st, tmr, rtx, rto, len, mss, lport, rport, rip) | U8/U16/IPv4 | `uip_conns[0]` | `health` (HEALTH=1) |
| `state.web.sessions` | U8 active | `session_id[]`, never the ids | none |
| `state.web.session.<i>.idle_s` | U32 | `last_session_use[]` | none |

Left out on purpose: `sched_cur`, `sched_ev`, `hw_stuck`, the watchdog counter
(always the reader's own values when dumped from a step), and the dead HEALTH
counters (`health_loops`, `ph_max/ph_slow`, `tick_pending`, `idle_ready`), which are
deleted (16.4). `do sched.clear` zeroes the scheduler stats, `con.rx_overruns` and
`health.hw_timeouts` (today `health clear` misses the last).

A full `get state.sched` is about 24 slot × 4 + 29 state × 2 + 10 lines ≈ 165 lines:
streamed one line per step on the console (the `health tasks` job is the template),
continued on ACK over the web (14.2).

### 7.4 state.mgmt (the effective IP config and DHCP)

| Path | Type | Source | Shown today |
|---|---|---|---|
| `state.mgmt.ip` | IPv4 | `uip_hostaddr` | information.json, `ip` (DUP) |
| `state.mgmt.netmask` | IPv4 | `uip_netmask` | information.json, `netmask` (DUP) |
| `state.mgmt.gateway` | IPv4 | `uip_draddr` | information.json, `gw` (DUP) |
| `state.mgmt.dhcp.state` | Enum{off, start, discover, request, leasing} | `dhcp_state.state` | `ip` (leasing or not) |
| `state.mgmt.dhcp.renew_s` | U16 | `dhcp_timer` | `ip` (hex) |
| `state.mgmt.dhcp.server` | IPv4 | `dhcp_state.server` | none |
| `state.mgmt.dhcp.router` / `.subnet` / `.broadcast` | IPv4 | `dhcp_state.*` | events. **Deferred (P5-3)**: while leasing, the same as gateway and netmask, and derived |
| `state.mgmt.dhcp.lease_s` / `.rebind_s` / `.renewal_s` | U32 | `dhcp_state.*` | events (lease only). **Deferred (P5-3)**: renew_s counts down the renewal |

`state.mgmt.dhcp.dns` is not a path: DHCP's DNS server is `state.dns.dhcp_server`
(one path a value). `dhcp.state` is the client's state (off, start, discover,
request, leasing); a client that is on but not leasing is `dhcp` mode all the same.

### 7.5 state.port.N

| Path | Type | Source | Shown today | Cost |
|---|---|---|---|---|
| `link` | Enum{down, up} | `LINKS_STS` | traffic.json, `stat`, `port N show` (DUP) | reg |
| `speed` | Enum{10m, 100m, 500m, 1g, 2g5, 5g, 10g, unknown} | `LINKS`/`LINKS_89` nibble | traffic.json, `stat` (DUP, diverge on 500M/unknown) | reg |
| `duplex` | Enum{full, half} | PHYSR bit 3 | `port N show` | MDIO |
| `media` | Enum{copper, sfp} | `machine.is_sfp[]` | ports.json isSFP, eee.json | free |
| `ms_role` | Enum{master, slave} | `MAC_MSTR_SLV_STS` | `port N show` | reg |
| `pause_tx` / `pause_rx` | Bool | `MAC_TX/RX_PAUSE_STS` | `port N show` | reg |
| `lp_adv` | Flags{10h, 10f, 100h, 100f, 1000h, 1000f, 2g5, 5g, 10g} | ANEG_LP_ABILITY, GANLPAR, MGBASE_ADV | `port N show` | 3 MDIO |
| `macs` | U16 | `L2_LRN_PORT_CONSTRT_CNT` & 0x1fff | traffic.json (raw), `stat` (DUP) | reg |
| `lag` | U8 (0 = none) | trunk member regs | l2.json, `l2` | 4 reg |
| `tx_good` / `rx_good` | U64 | STAT 46 / 47 | traffic.json, `stat` (low 32 bits only), counters.json (DUP ×3) | STAT |
| `tx_bad` / `rx_bad` | U32 | STAT 48 lo / hi | traffic.json, `stat`, counters.json (DUP ×3) | STAT |
| `eee.active` | Bool | `PHY_EEE_ABLTY` | eee.json, `eee status` (DUP) | reg |
| `eee.lp` | Flags{100m, 1g, 2g5, 5g, 10g} | PHY EEE LP ability | eee.json (no 5g/10g), `eee status` (DUP, diverge) | 2 MDIO |
| `cabletest.state` | Enum{idle, running, done, timeout} | `ct_state`, `ct_have` | `port N cabletest show` | free |
| `cabletest.pair.{a,b,c,d}.result` | Enum{ok, open, short, cross_short, mismatch, unknown} | `ct_result[]` | same | free |
| `cabletest.pair.{a,b,c,d}.length_m` | Scaled(dm/10, 1 decimal) | `ct_len[]` | same | free |

The port getter costs up to 6 MDIO reads (~1 ms) for a copper port: one port per
step. The cable test keeps one result per port (today one in total: `ct_port`);
9 ports × 9 bytes.

### 7.6 state.port.N.mib (the full counter set; its own object, one port per step)

55 STAT indexes, ~5.5 ms per port. Index 0-7 and 46, 47, 49, 50 are 64-bit; the
others hold two 32-bit counters (hi / lo). Names are the UI's (app.js:3519); they
move into the schema, so the firmware emits names it never had.

| Index | Counters |
|---|---|
| 0-7 (U64) | `if_in_octets`, `if_out_octets`, `if_in_ucast`, `if_in_mcast`, `if_in_bcast`, `if_out_ucast`, `if_out_mcast`, `if_out_bcast` |
| 8 | `if_out_discards` / `dot1d_tp_port_in_discards` |
| 9 | `dot3_single_collision` / `dot3_multi_collision` |
| 10 | `dot3_deferred_tx` / `dot3_late_collisions` |
| 11 | `dot3_excessive_collisions` / `dot3_symbol_errors` |
| 12 | `dot3_ctrl_in_unknown_opcodes` / `dot3_in_pause` |
| 13 | `dot3_out_pause` / `ether_drop_events` |
| 14 | `tx_bcast` / `tx_mcast` |
| 15 | `tx_crc_align_err` / `rx_crc_align_err` |
| 16 | `tx_undersize` / `rx_undersize` |
| 17 | `tx_oversize` / `rx_oversize` |
| 18 | `tx_fragments` / `rx_fragments` |
| 19 | `tx_jabbers` / `rx_jabbers` |
| 20 | `tx_collisions` / `tx_64` |
| 21 | `rx_64` / `tx_65_127` |
| 22 | `rx_65_127` / `tx_128_255` |
| 23 | `rx_128_255` / `tx_256_511` |
| 24 | `rx_256_511` / `tx_512_1023` |
| 25 | `rx_512_1023` / `tx_1024_1518` |
| 26 | `rx_1024_1518` / `x26_lo` |
| 27 | `rx_undersize_drop` / `tx_over_1518` |
| 28 | `rx_over_1518` / `tx_too_large` |
| 29 | `rx_too_large` / `tx_flex_octets_s1` |
| 30 | `rx_flex_octets_s1` / `tx_flex_octets_crc_s1` |
| 31 | `rx_flex_octets_crc_s1` / `tx_flex_octets_s0` |
| 32 | `rx_flex_octets_s0` / `tx_flex_octets_crc_s0` |
| 33 | `rx_flex_octets_crc_s0` / `length_field_errors` |
| 34 | `false_carriers` / `undersized_octets` |
| 35 | `framing_errors` / `x35_lo` |
| 36 | `rx_mac_discards` / `rx_mac_ipg_short_drop` |
| 37 | `x37_hi` / `dot1d_tp_learned_entry_discards` |
| 38-41 | `egress_q7_drop` .. `egress_q0_drop` |
| 42-45 | `egress_q7_out` .. `egress_q0_out` |
| 46, 47 (U64) | `tx_good`, `rx_good` (also in 7.5) |
| 48 | `rx_err` / `tx_err` (also in 7.5) |
| 49, 50 (U64) | `tx_good_phy`, `rx_good_phy` |
| 51 | `rx_err_phy` / `tx_err_phy` |
| 52-54 | `x52`, `x53`, `x54` (read today, meaning unknown; 20.12) |

The SDK-order names are unverified on hardware; `x..` names stand for the unnamed
halves until they are identified.

**Deviation (P5-5, shipped):** `state.port.N.mib.K`, K the STAT index 0-54 (a
level of its own, from 0: IX_IDX), one STAT_GET an instance and so a step (the 55
in one getter were ~5.5 ms). Its one field is nameless: the line is
`state.port.3.mib.17 123`, the index's raw 64-bit word (the high half, then the
low, for two 32-bit counters). The 87 names are only in schema.js (`n`, from
schema/state_mib.py) and `rtlpg_client.py mib N`: names in the firmware would
have been ~1.7 KB of bank. The nameless field costs 17 B over a field `v`.
`get state.port.N` includes the port's 55 words (a prefix names the children).

### 7.7 state.sfp.S (S = slot 1..2; from the sfp task's caches, free to read)

| Path | Type | Source | Shown today |
|---|---|---|---|
| `port` | Port | `machine.sfp_port[]` | derived in JS |
| `present` | Bool | `sfp_pins_last` | ports.json enabled (overloaded), `stat`, `sfp`, events (DUP ×3, two sources) |
| `los` | Enum{ok, los, none} | pins (`none`: no LOS pin) | ports.json, events |
| `ready` / `id_ok` / `ddm_ok` | Bool | `sfp_ready[]`, `sfp_id_ok[]`, `sfp_diag_ok[]` | implicit |
| `vendor` | Str(16) | bytes 20-35 | ports.json, information.json, `sfp` (DUP ×3) |
| `oui` | hex 3 bytes | bytes 37-39 | none |
| `part` | Str(16) | bytes 40-55 | ports.json, information.json, `sfp` (DUP ×3) |
| `rev` | Str(4) | bytes 56-59 | inside a concatenated string |
| `serial` | Str(16) | bytes 68-83 | ports.json |
| `encoding` | U8 | byte 11 | `sfp` |
| `rate_mbps` | U16 | byte 12 × 100 | `sfp` |
| `options` | hex U8 (after quirks) | `sfp_options[]` | ports.json (after), `sfp` (before): DUP, diverge |
| `quirks` | hex U8 | `sfp_quirks[]` | none |
| `ddm.temp_c` | Scaled, calibrated | A2 224-225 | ports.json (raw hex + cal blob), `sfp` (hex) (DUP) |
| `ddm.vcc_v` | Scaled | 226-227 | (DUP) |
| `ddm.tx_bias_ma` | Scaled | 228-229 | (DUP) |
| `ddm.tx_power_dbm` | Scaled | 230-231 | (DUP) |
| `ddm.rx_power_dbm` | Scaled | 232-233 | (DUP) |
| `ddm.laser` | U16 raw | 234-235 | `sfp` only (meaning per module; 20.12) |
| `ddm.status` | Flags{tx_disable, tx_fault, rx_los, not_ready} | 238 | ports.json, `sfp` (DUP) |

**Deviation (P5-4, shipped):** the diagnostics are SFF-8472's raw words, not
converted or calibrated in firmware (no log table, no 32-bit math): `ddm.vcc_v`
Scaled(4) V, `ddm.tx_power_mw` / `ddm.rx_power_mw` Scaled(4) mW (their raw
units are exact decimals), `ddm.temp` raw U16 (1/256 C, signed) and
`ddm.tx_bias` raw U16 (2 uA), `ddm.laser` raw, `ddm.status` Flags. When the
module flags external calibration, `ddm.cal` holds bytes 184-223 (the RX
polynomial, then the slope/offset pairs) as 80 hex characters, and the printed
values are uncalibrated. dBm, the calibration and the temp/bias conversions are
the UI's (and a host's, rtlpg_client). The getter reads only the sfp task's
caches, never I2C. Deferred: `port` (derived from `state.sys.sfp_ports`), `oui`.

The original plan, kept for reference: external calibration (SFF-8472
slope/offset for temp/vcc/bias/tx power) applied by the getter in integer
arithmetic. The RX-power calibration is a polynomial in
float32; the getter emits the uncalibrated value plus `ddm.rx_cal on` when
external calibration is flagged, and the UI/host applies the 5 coefficients
(`ddm.rx_cal_coef` as 40 hex chars), as the UI does today (20.13).

### 7.8 state.l2 (the MAC table, streamed)

Keyed by the hardware table index (`state.l2.<idx>.*`), walked with
`TBL_LUTREAD_NEXT_L2UC`, one entry per step (console) or per ACK refill (web).
`get state.l2` replaces both walkers (`l2` CLI job and `/l2.json?idx=` paging).

| Field | Type | Shown today |
|---|---|---|
| `mac` | MAC | l2.json, `l2` (DUP) |
| `vlan` | U16 | l2.json (hex string), `l2` (hex) (DUP) |
| `port` | Port or `cpu` | l2.json, `l2` (DUP) |
| `lag` | U8, 0 = none | l2.json, `l2` (DUP); resolved once per walk from the LAG masks |
| `static` | Bool | l2.json, `l2` (DUP) |
| `age` | U8 | none (new) |

IGMP/IPMC entries are not listed anywhere today; `state.igmp.group.*` would be a
second streamed object, later.

**As shipped (P5-5):** a new index kind, IX_STREAM (`state("l2", stream=4096,
get=...)`): the engine hands the getter the cursor in `om_key`; it searches the
table from there (one TBL_LUTREAD_NEXT_L2UC a step), fills om_st and sets
`om_key` to the entry found, or OM_ERR when the search wraps, finds nothing or the
table's poll times out (a held table ends the walk, not 4096 timeouts).
`get state.l2.<idx>` is the search from idx that must land on it ("no such
entry"). The key is the hardware index: not stable over aging (the tip says so). A page
(P5-5b): `get state.l2.K-` lists the first OM_PAGE (25) entries at or after K, then
ok (empty: ok alone); the UI pages with K = the last index + 1 until a page is
short, and the cli serves the console between pages (rtlsim, 300 hosts: a
console line waits 118 ms, against 2.45 s behind one whole get). Any streamed
level takes `K-`.
Over the web it is one `get state.l2`: the /cli reply continues on ACK (rtlsim,
300 hosts: 36 KB in 3.3 s, steps under 3.2 ms); no paging. `lag` is resolved per
entry (port_lag_of, 4 reg reads), not once per walk. Deferred: `age`. Kept:
/l2_del.json and `l2 forget` (their action form is P7's); `l2` alone prints its
usage.

### 7.9 state.stp, state.stp.port.N, state.stp.lag.L

| Path | Type | Source | Shown today |
|---|---|---|---|
| `state.stp.bridge` | BridgeId | `stp_prio`, `uip_ethaddr` | stp.json, `stp status` (DUP) |
| `state.stp.root` | BridgeId | `root_bridge` | stp.json, `stp status` (DUP) |
| `state.stp.is_root` | Bool | `stp_root_port == 0xff` | stp.json, `stp status` (DUP) |
| `state.stp.root_port` | Port, `lag.L` or none | `stp_root_port` | `stp status` |
| `state.stp.root_cost` | U32 | `root_bridge_cost` | stp.json, `stp status` (DUP) |
| `state.stp.root_maxage_s` / `.root_fwd_s` | U8 | `stp_root_maxage/fwd` | stp.json |
| `state.stp.msg_age_s` | U8 | `stp_msg_age` | none |
| `state.stp.tc_count` | U16 | `stp_tc_count` | stp.json, stpcnt.json, `stp status` (DUP ×3) |
| `state.stp.tc_age_s` | U32 | `stp_tc_secs` | stp.json |

Per entity (`state.stp.port.N.*`, `state.stp.lag.L.*`):

| Field | Type | Source | Shown today |
|---|---|---|---|
| `state` | Enum{off, block, learn, fwd} | `MSTP_STATES` | stp.json, `stp status` (DUP) |
| `role` | Enum{disabled, root, designated, alternate, backup} | `stp_port_role()` | stp.json, `stp status` (DUP) |
| `edge` | Bool | `STP_PF_OPEREDGE` | stp.json (inside `f`), `stp status` (DUP) |
| `guard_tripped` | Bool | `STP_PF_TRIPPED` | stp.json (inside `f`) |
| `link` | Bool | `stp_link_prev` | stp.json |
| `legacy` | Bool | `stp_legacy` (802.1D neighbour) | stp.json, event |
| `members` | PortMask (LAG entities) | `stp_lag_mask[]` | stp.json |
| `dbridge` | BridgeId | `stp_dbridge[]` or own | stp.json |
| `dport` | U16 hex | `stp_dpid[]` or own | stp.json |
| `dcost` | U32 | `stp_dcost[]` | stp.json |
| `bpdu_age_ms` | U32 | `stp_bpdu_age[]` (ticks @ 50 Hz) | stpcnt.json (ticks), `stp status` (s, capped) (DUP) |
| `rx_bpdu` / `tx_bpdu` | U32 | `stp_cnt[RX/TX][]` | stpcnt.json, `stp status` (tx: low byte only) (DUP) |
| `rx_tc` / `tx_tc` | U32 | `stp_cnt[TCRX/TCTX][]` | stpcnt.json |
| `loop_held` | Port or none | `stp_loop_held[]` | events |

**P5-4 (shipped):** `root_port` is a word (`none`, the port's number, `lag.L`),
not a Port type (a LAG isn't a port). Every entity prints (a port in a LAG and an
unused LAG read `off`/`disabled`); `state.stp.lag.L.members` says which LAGs
exist. With STP off the state still prints (STP's on/off is config, P4-4).
Deferred, all still in stp.json until P4-4: `root_maxage_s`, `root_fwd_s`,
`msg_age_s`, `tc_age_s`, and per entity `guard_tripped`, `link`, `legacy`,
`dbridge`, `dport`, `dcost`, `loop_held`. Deleted: `stp status`, `/stpcnt.json`.
**P7:** the deferred ones are in, but for `msg_age_s` and `loop_held` (no
surface showed them); `/stp.json` is deleted and the STP tab reads
`get state.stp`. `dport` is the port ID as a number (priority * 256 + port).

The unreported protocol internals (rx timers, `stp_rpcost`, `info_while`,
`tcwhile`, the alt/backup/agree bitmaps) are not exposed; a `dbg.stp.*` subtree can
add them when needed.

### 7.10 state.lag.L and state.lacp.port.N

| Path | Type | Source | Shown today |
|---|---|---|---|
| `state.lag.L.active` | PortMask | `lacp_members_last[]` (LACP) / HW register (static) | lag.json, `lag show`, `lacp show` (DUP, and `lag show` prints active where lag.json prints configured) |
| `state.lag.L.agg_valid` | Bool | `lacp_agg_valid[]` | lag.json, `lacp show` (DUP) |
| `state.lag.L.aggregator` | MAC | `lacp_agg_sys[]` | lag.json, `lacp show` (DUP) |
| `state.lacp.running` | Bool | `lacpEnabled` | `lacp show` |
| `state.lacp.port.N.lag` | U8, 0 = none | `lacp_port_lag[]` (255 → 0) | lag.json |
| `state.lacp.port.N.actor` | Flags{activity, timeout, aggregation, sync, collecting, distributing, defaulted, expired} | `lacp_actor_state[]` | lag.json (hex), `lacp show` (DUP) |
| `state.lacp.port.N.partner` | Flags (same) | `lacp_partner_state[]` | lag.json, `lacp show`, events (DUP) |
| `state.lacp.port.N.rx_state` | Enum{initialize, port_disabled, expired, defaulted, current} | `lacp_rx_state[]` | lag.json, `lacp show` (DUP) |
| `state.lacp.port.N.rx_pdus` | U16 | `lacp_rx_count[]` | lag.json, `lacp show` (DUP) |
| `state.lacp.port.N.partner_sys` | MAC | `lacp_partner_sys[]` | lag.json, `lacp show` (DUP) |
| `state.lacp.port.N.partner_key` / `.partner_port` / `.partner_sys_prio` / `.partner_port_prio` | U16 | `lacp_partner_*[]` | none (new) |

**P5-4 (shipped):** `state.lag.L` and `state.lacp.port.N` as above; `state.lacp.running`
is deferred (an object of its own for one Bool: LACP runs while a LAG is in LACP
mode, `lag.L.mode` says so), and so are the partner's key, port and priorities.
Deleted: `lacp show`. `lag show` and lag.json stay until P4-3 (they carry the
config).

### 7.11 state.loop, state.ntp, state.dns

| Path | Type | Source | Shown today |
|---|---|---|---|
| `state.loop.looped` | PortMask | `rldp_looped` | loop.json, `loop show`, events (DUP) |
| `state.loop.blocked` | PortMask | `rldp_blocked` | loop.json, `loop show` (DUP) |
| `state.loop.pair.N` | Port or none | `RLDP_LOOP_PAIR` | events. **Deferred (P5-3)**: a Port type (~100 B) and a register read a port |
| `state.ntp.synced` | Bool | `ntp_state.synced` | `ntp`, `time` (scraped by the UI) (DUP) |
| `state.ntp.time` | U32, seconds since 1970 UTC (P5-3: not a Time type; the UI formats it), 0 until synced | `utc + uptime - at` | `time` (local), scraped |
| `state.ntp.local_offset_min` | I16 (with DST) | offset + DST | `time` |
| `state.ntp.dst_active` | Bool | `ntp_in_dst()` | `time` |
| `state.ntp.last_sync_s` | U32 | `uptime - at` | `time` (minutes), scraped |
| `state.ntp.next_sync_s` | U32 | `ntp_state.next` | none. **Deferred (P5-3)**: 83 B of 32-bit code |
| `state.ntp.stratum` | U8 | `ntp_state.stratum` | `time` |
| `state.ntp.server_ip` | IPv4 | `ntp_state.addr` | `ntp`, scraped |
| `state.dns.dhcp_server` | IPv4 | `dhcp_state.dns` | `dns`, scraped |
| `state.dns.lookup.name` | Str | `dns_state.name` | `dns`. **Deferred (P5-3)**: a Str(47); `dns lookup` names it |
| `state.dns.lookup.status` | Enum{none, pending, done, failed} | `dns_state.status` | `dns` |
| `state.dns.lookup.ip` | IPv4 | `dns_state.addr` | `dns`, `dns lookup` |

P5-3 shipped these with their printers' state parts deleted (`time`;
`ntp`'s time line; `dns`'s DHCP and lookup lines; `ip`, `gw`, `netmask` without an
argument; `loop`'s port lists). The printers' config parts stay until P4-1/P4-5;
loop.json stays whole until P4-5. state.fw.* and state.loader.* beyond `present`
(7.12) are deferred: nothing prints them, so they would delete nothing.

No state exists for `lldp` (transmit only: no neighbour table), `igmp` (no group
list), `vlan`, `storm`, `bw`, `mirror`, `isolate`: all of those are config, read
with `get`. PoE has no per-port state anywhere today.

### 7.12 state.cfg, state.fw, state.loader

| Path | Type | Source |
|---|---|---|
| `state.cfg.pending` | U16 (fields candidate ≠ running) | engine |
| `state.cfg.dirty` | Bool (running ≠ saved) | engine |
| `state.cfg.apply` | Enum{idle, running, done, failed} + `state.cfg.apply_at` (obj) | apply job |
| `state.cfg.confirm_s` | U16, 0 = no window | apply job |
| `state.cfg.save` | Enum{idle, erasing, writing, done, failed, too_big} | flash job `FL_SAVE` |
| `state.cfg.saved_bytes` / `.max_bytes` | U16 | engine / `CONFIG_LEN` |
| `state.cfg.load.skipped` / `.first_skipped` | U16 / Str | load job |
| `state.cfg.format` | Enum{ok, blank, bad} (what boot found; old command text is `bad`, booted on default; P3K dropped `legacy`) | boot |
| `state.fw.upload.kind` | Enum{none, app, install, loader, mdata} | httpd `up_*` |
| `state.fw.upload.state` | Enum{idle, receiving, verifying, done, failed} | httpd / `FL_UPLOAD` |
| `state.fw.upload.bytes` / `.total` | U32 | `uptr - start`, `upload_end` |
| `state.fw.upload.result` | Str (the verdict text) | `upload_verdict()` |
| `state.fw.mdata.valid` / `.len` | Bool / U32 | `mdata_len(MDATA_START)` |
| `state.loader.present` | Bool | `has_loader` |
| `state.loader.recovery_boot` | Bool | `loader_recovery()` |
| `state.loader.unconfirmed_boots` | U8 | boot.c `loader_boots` |
| `state.loader.confirmed` | Bool | `loader_confirmed` |
| `state.loader.layout` | U8 | the loader's "LDR"+layout tag |
| `state.loader.app_crc` | hex U16 | `LDR_META` bytes 4-5 |

### 7.13 Counts

About 200 state fields in 25 objects, plus 87 MIB counters. Of the ~45 values
printed on two or more surfaces today, every one now has one getter and one path;
the divergences listed in state.md section 15 (truncated counters, missing 5G/10G
bits, two speed sources, raw vs masked MAC counts) disappear by construction.

---

## 8. Actions: every item

`do <path> [arg]`. Each action claims a job (or, for the instant ones, runs in the
cli step), reports progress under `state.<path>` where it has any, and prints an
`event` line on the console when it ends.

| Action | Arg | Does | Today | Result state |
|---|---|---|---|---|
| `sys.reset` | | reset with cause `cmd` (`web` from the web) | `reset`, GET `/reset` | `state.sys.reset.cause` after boot |
| `sys.factory_reset` | | flash job: default config to flash, reset (cause `factory`) | `factory-reset`, button > 10 s | `state.cfg.save` |
| `mgmt.dhcp_renew` | | restart the DHCP client | `ip dhcp` (half of it) | `state.mgmt.dhcp.*` |
| `dns.lookup` | name | resolve | `dns lookup <name>` | `state.dns.lookup.*` |
| `ntp.sync` | | sync now | (`ntp on` side effect) | `state.ntp.*` |
| `port.N.cabletest` (multi) | | TDR test (own job, out of the lldp step) | `port N cabletest` | `state.port.N.cabletest.*` |
| `port.N.selftest` (multi) | kind | port self test: kinds to be decided (20.9): `tdr` (= cabletest), `loopback` (PHY loopback), `prbs` (SerDes PRBS on SFP/10G) | none | `state.port.N.selftest.{state,kind,result,errors}` |
| `sys.selftest` | | runs every port's default test and the health checks in sequence | none | `state.sys.selftest.{state,result,failed}` |
| `sfp.S.reread` | | re-read the module EEPROM | (`sfp S <speed>` side effect) | `state.sfp.S.*` |
| `l2.flush` | [port] | flush dynamic entries | `l2 forget` | |
| `l2.delete` | idx | delete one unicast entry (refuses our own MAC) | GET `/l2_del.json?idx=` | `state.l2.delete_result` |
| `stp.counters_clear` | | zero the BPDU/TC counters | `stp clear` | |
| `stp.port.N.mcheck`, `stp.lag.L.mcheck` | | restart protocol migration | `stp port N mcheck` | |
| `stp.port.N.clear_guard`, `stp.lag.L.clear_guard` | | un-trip a BPDU-guard port (P4-4; replaces `stp port N on` on a port already on) | `stp port N on` | |
| `sched.clear` | | zero scheduler stats, overruns, hw timeouts | `health clear` | |
| `web.logout` | | end this session | none (new) | |
| `web.logout_all` | | end every session | none (new; today only `passwd` does it) | |
| `sys.rnd` | | print 6 random bytes (an `event` line) | `rnd` | |

**P7 (shipped):** `do <path> N` passes N (decimal, 16 bits) to the handler.
`l2.delete K` has no `state.l2.delete_result`: its ok or err line is the
result (`no such entry`, `this switch's own entry`). `l2.flush` takes no port
(as `l2 forget`): nothing asked for one.

Gone as actions, now config: `lacp off` (`set lag.*.mode off`), `ip dhcp`
(`mgmt.mode`), `igmp off`, `passwd` (logs everyone out on apply), `mac`.

Gone: `history`, `/cmd_log`, `/cmd_log_clear` (the history ring's job was the
save merge); GET `/reset` and GET `/l2_del.json` (side-effect GETs, CSRF-able:
everything that changes something is a POST).

Outside the model, unchanged: the loader (its console, XMODEM recovery, boot
count), the button-held-at-boot factory path (boot.c:66), staged-image install and
loader conversion at boot, the 15 s loader boot-count confirm (20.11).

---

## 9. Debug namespace

Compiled in with `DEBUG_OBJ=1` (default off in release images; on in rtlsim and
bench builds). Same engine, keyed objects, hex keys:

| Path | Today |
|---|---|
| `get dbg.reg.<addr>` / `set dbg.reg.<addr> <val>` | `regget`, `regset` |
| `get dbg.phy.<port>.<dev>.<reg>` / `set ...` | `phyget`, `physet` |
| `get dbg.sds.<sds>.<page>.<reg>` / `set ...` | `sdsget`, `sdsset` |
| `get state.dbg.sds_modes` | `sds` |
| `get state.dbg.gpio` | `gpio` (no "changed since last" side effect) |
| `get state.dbg.leds` | `leds` (leds_dump, 1.7 KB, moves here) |
| `get state.dbg.igmp.port.N` | `igmp show` (raw per-port config register) |
| `do dbg.flash.dump <addr> <len>` | `flash r` (now a job, a line per step; today up to 32 KiB in one step) |
| `get state.dbg.flash.{uid,security}` | `flash u`, `flash s` |
| `state.dbg.stp.*` | STP protocol internals (7.9) |

`set dbg.*` applies at once (no candidate): debug pokes are actions in all but
name; the schema marks the subtree `immediate`.

**P7 (shipped), a deviation:** only the reads that are state became objects:
`state.dbg.{sds_modes,gpio_0,gpio_1}` and `state.dbg.igmp.port.N.cfg`, as
Hex32 words (`sds`, `gpio`, `igmp show` deleted). `regget`/`regset`,
`phyget`/`physet`, `sdsget`/`sdsset`, `flash s|j|u|r` and `leds` stay console
commands. As objects they need an immediate set on a config-tree path (get by
getter, set by handler, skipped by diff/apply/save: ~250-400 B), hex indices
and values (~60 B), a third index level for `dbg.phy`/`dbg.sds` (~150-250 B;
dev + reg don't pack into a 16-bit key) and a must-index rule (or `get
state.dbg.reg` walks 64 K registers): ~600-800 B of engine for ~400 B saved in
debug builds and none in release. `state.dbg.stp.*` is not added (nothing
showed it). The flag is `OM_DBG` (DEBUG_OBJ gone): default 1 (rtlsim, the
bench, the tests use `regget`), 0 for release (P8 decides); OM_DBG=0 drops
state.dbg, the pokes and the leds dump: 6.3 KB of banks on the 8x.

---

## 10. The schema

### 10.1 Where it lives

A separate Python package, `schema/` in the firmware tree, one module per object
group (`sys.py`, `port.py`, `vlan.py`, `stp.py`, `state_sched.py`, ...), read by
`tools/om_gen.py`. Not inline in C: the schema is data that the web UI, the Python
client and the docs need without SDCC or a machine build, and the path registry
needs one place to check (build-gen-test.md section 1). The C handlers bind to it
the decision-24 way (10.4).

### 10.2 Declarations

```python
from om import *

SPEED = Enum("speed", "auto 10m 100m 1g 2g5 5g 10g")

obj("port", count=PORTS, apply="port_apply", order=40,
    label="Port", tip="A front-panel port")
cfg("name",    Str(31),          default="",     tip="Shown in the UI and sent in LLDP")
cfg("enabled", Bool,             default=True,   tip="Administrative state")
cfg("speed",   SPEED,            default="auto", when={"5g": TEN_G, "10g": TEN_G},
    tip="Autonegotiate, or force one speed")
cfg("duplex",  Enum("duplex", "auto full half"), default="auto")
cfg("mtu",     U16(64, 16383),   default=16383,  unit="bytes")
cfg("pvid",    U16(0, 4094),     default=1,      tip="VLAN for untagged frames; 0 parks the port")
cfg("eee",     Bool,             default=True,   when=COPPER)
...
check("duplex != 'half' or speed in ('10m', '100m')", "half duplex needs 10m or 100m")

state("port", get="port_state_get", cost=MDIO(6))
st("link",   Enum("link", "down up"))
st("speed",  Enum("lspeed", "10m 100m 500m 1g 2g5 5g 10g unknown"))
st("tx_good", U64, unit="packets")
...
obj("cabletest", parent="port", get="cabletest_get")
...

action("cabletest", parent="port", start="cabletest_start", multi=True,
       tip="Time-domain reflectometry on the four pairs")
```

```python
obj("vlan", keyed=U16(1, 4094), max="MAX_VLANS", apply="vlan_apply", order=30)
cfg("name",    Str(15),  default="", check="name == '' or name[0].isalpha()")
cfg("members", PortMask, default=NONE)
cfg("tagged",  PortMask, default=NONE)
check("tagged <= members", "tagged ports must be members")
default_entry(1, members=ALL_PORTS)
```

### 10.3 Field attributes

| Attribute | Meaning |
|---|---|
| name | path segment, `[a-z][a-z0-9_]*`; units in the name (`_s`, `_ms`, `_kbps`) |
| type | 3.2 |
| access | `cfg`, `secret`, `state`, `action`, `immediate` (debug) |
| default | constant, `derived("fn")` (computed at boot when empty: hostname, MAC), or per-chipset `{RTL8372: x, RTL8373: y}` |
| when | availability: `COPPER`, `SFP`, `TEN_G`, `POE`, or a build flag (`HEALTH`, `MACHINE_POE`, `DEBUG_OBJ`) |
| check | a field or object rule, in a small expression language the generator compiles to C (6.20) |
| order | apply order of the object (6.21) |
| cost | getter cost class (`FREE`, `REG(n)`, `MDIO(n)`, `STAT(n)`, `TBL`): the engine batches by it per step |
| label, tip, unit | UI and docs only; never in firmware flash |
| alias | old names, so a renamed path still loads (the registry rule, 10.5). Not built yet. When it is: the path lookup's first-character chains (om_nx/om_fc, built by om_gen) must chain an alias under its own first letter, or the old path stops resolving |
| since | the release that added it (docs, client) |

### 10.4 Binding to C

Handlers are ordinary banked functions with one argument (the object index),
declared with marker macros that expand to the prototype, like `STEP`:

```c
OM_APPLY(port)        /* uint8_t om_apply_port(uint8_t idx) __banked */
{
	__xdata struct cfg_port *c = &cfg_run.port[idx];
	phy_set_speed(idx, c->enabled ? c->speed : PHY_OFF);
	...
	return OM_OK;
}

OM_GET(port)          /* uint8_t om_get_port(uint8_t idx) __banked; fills om_st.port */
OM_DO(port, cabletest)
```

A sweep (`tools/om_sweep.py`, the `sched_sweep.py` pattern) runs the build's own
cpp over the feature sources and checks: every schema handler has exactly one
definition with the right signature; no marker sits outside the swept sources;
a handler compiled out by `#ifdef` drops its fields from that build's tables (and
the schema must name the condition, so the web and docs agree).

### 10.5 The path registry

`schema/registry.txt` (committed) lists every object and field ever shipped,
with its **numeric ID**: objects `u8`, fields `u8` within their object, assigned
by the generator on first appearance and never reused. These IDs are the keys of
the binary saved config (15.3). The generator fails the build if a registered entry
disappears without an `alias` (a rename keeps its ID) or an explicit `retired`
entry (its ID is never handed out again), or if a field's type changes without a
new ID. This is what makes a saved config from any release load in any later one,
and a text file from any release (paths, with aliases) load too.

- **Position is not identity.** A field's place in the schema or the struct has no
  bearing on its ID, so a field added anywhere in an object takes the next free ID
  and nothing renumbers. Array position is not part of an ID either: the port, LAG
  or VLAN number travels in the record's index, so more ports, a larger
  `MAX_VLANS` or a 6-port save on a 9-port board change no ID (an index the board
  lacks is skipped and counted). Nested arrays (`port.N.storm.T`) use a two-level
  index (`N<<8 | T`), so a new storm type is a new index value, not a new ID.
- **A changed range or enum list keeps the ID**; a stored value outside the new
  range loads as the default and is counted (`state.cfg.load.skipped`).
- **Parallel branches** that each add a field get the same next ID; the registry
  line conflicts in git at merge, and the second branch regenerates and takes the
  next number. Safe as long as neither branch has shipped a save.
- IDs are assigned, not hashed from the path: a hash would break on rename and
  could collide silently.

---

## 11. The generators

`tools/om_gen.py` (stdlib Python), run by a new `om.mk` beside `sched.mk`, with
the same mechanics: outputs in `$(BUILDDIR)/om/`, written only if changed, a stamp,
explicit compile rules, `$(OBJS): | om_gen.h`, the `.d` re-include gotcha
(sched.mk:50-53), `file:line` in every error (build-gen-test.md section 1).

### 11.1 Outputs

| Output | Where | Contents |
|---|---|---|
| `om_gen.h` | build dir | `struct cfg_<obj>` per object, `struct cfg` (the whole), `struct st_<obj>` (getter buffers, one union `om_st`), object/field enums, `MAX_VLANS` etc., externs |
| `om_gen.c` (banked, one group with the engine) | build dir | field descriptor tables `__code const struct om_field f_<obj>[]`, object table `om_obj[]`, enum word tables, limit table, check bytecode, `cfg_default` image, the `_Static_assert`s pinning every `offsetof`/`sizeof` |
| `om_gen_fn.c` | build dir | banked function-pointer tables `om_apply_fn[]`, `om_get_fn[]`, `om_do_fn[]` (3 B per entry; one `uint8_t` argument: within SDCC's limit for calls through a pointer) |
| `default.bin` | build dir | the factory config: the board's default file (text, `configs/<machine>.cfg` or `config.cfg`) checked against the schema and encoded to binary records by the Python codec; placed by `fileadder` (15.1) |
| `registry.txt` updates | source tree | new object/field IDs, written by the generator and committed (10.5) |
| `schema.js` | source tree (`html/`, like html_data.c), machine-independent | paths, types, enums, limits, defaults, `when` rules, labels, tips, apply order; terse encoding |
| `rtlpg_client.py` | `tools/` | stdlib-only client: login, `get`/`set`/`diff`/`apply`/`confirm`/`save`/`do`, parse lines into nested dicts, typed values from the schema; the text and binary codecs the tests use; `convert` for old command-text configs (15.2) |
| `doc/cli.md`, `doc/paths.md` | `doc/` | the reference: every path, type, default, tip, `when`, the verbs |
| `test/om/` cases | build dir | per-field edge cases (11.2) |
| `legacy_map.py` | `tools/` | the old-command → path table that `rtlpg_client.py convert` uses (15.2); host only |

Structs and offsets: never computed in Python. SDCC packs structs and gcc (host
tests) pads them, so tables use `offsetof()` and the generator emits
`_Static_assert(offsetof(struct cfg_port, pvid) == ...)` only for the packed
target layout that rtlsim's `read_struct` and the client's struct view assume
(build-gen-test.md, constraint 4). No integer casts of addresses in `__code`
initializers (not constant in SDCC).

Array sizes: `cfg.port[]` is indexed by logical port and sized 9 (logical 0..8) for
every board, so one layout per build regardless of chipset; RTL8372 wastes three
entries (~180 B a copy). SFP arrays are 2.

### 11.2 Generated tests

From each field's type and limits: min, max, min-1, max+1, overflow, empty,
trailing junk, string one too long, every enum word and one wrong word,
`when`-forbidden values per board shape. From each check: one passing and one
failing candidate. From each object: get → parse → equal round trip. These run in
the host harness (18).

### 11.3 The scheduler generator, extended

`tools/sched_gen.py` gains `task_name[]` and `state_task[]` (7.3), and emits the
`state.sched.*` object rows (one per slot, one per state) as an input to
`om_gen.py`, so the scheduler's state tree is generated from the same TASK/STEP
declarations that build the scheduler.

---

## 12. The engine (firmware)

New sources, one place_banks group (the engine reads the tables with `mov dptr`,
which unions the files; the group must fit one bank, which the deletions make
room for):

| File | Does |
|---|---|
| `om.c` | path lookup, cursor, get/set/del, diff, check interpreter |
| `om_text.c` | per-type parse and emit, the byte-wise number routines |
| `om_bin.c` | the binary record encoder and decoder for the saved config (15.3), driven by the same tables |
| `om_cli.c` | the ten verbs, replaces `cmd_parser.c`'s dispatcher; the console and web front end |
| `om_job.c` (a step source, in `SCHED_STEP_SRCS`) | the `cfg` job: apply, confirm window, load; the `act` job: actions |
| `flash_job.c` | gains `FL_SAVE` (encode 512 B of records a step into `flash_buf`) |

### 12.1 Tables

```c
struct om_field {                 /* 8 bytes + name */
	__code const char *name;
	uint8_t id;               /* stable field ID from the registry (binary save key) */
	uint8_t type;             /* T_BOOL, T_U16, T_ENUM, ... | access bits */
	uint8_t off;              /* offsetof within the element (< 256) */
	uint8_t arg;              /* enum table / string length / scale index */
	uint8_t lim;              /* index into the limits table */
	uint8_t when;             /* availability rule index */
};
struct om_obj {
	__code const char *name;
	uint8_t id;               /* stable object ID from the registry */
	__code const struct om_field *f;
	uint8_t nf, kind;         /* singleton / numbered / keyed / parent link */
	uint8_t count, size;      /* elements and element size */
	uint16_t base;            /* offsetof(struct cfg, obj) or the st buffer */
	uint8_t parent, fn;       /* nesting; handler table index */
};
```

### 12.2 Rules it follows (memory.md, build-gen-test.md section 2)

- All state `static __xdata`; no internal RAM growth; no function with two
  parameters (arguments through globals, the `flash_region` idiom); typed
  `__xdata *`/`__code *` pointers only (generic pointers call `__gptrget` per byte).
- Non-reentrant, so **no recursion**: path walking and nested emit use an explicit
  cursor `{obj, idx, field, depth, stack[4]}` in xdata.
- No 32-bit arithmetic on step paths: numbers go through the byte-wise routines.
- Steps bounded: one object instance per step for getters with a cost above `REG`,
  one line per step on the console (`cli_job = CLI_JOB_GET`, the `health tasks`
  pattern), one MSS per ACK on the web.
- Code around the two SDCC miscompiles (a `(uint8_t)` cast dropped when passed as
  `uint32_t`; signed variable shifts).

### 12.3 Paths and cursors

`om_find()` walks a path segment by segment against the tables: name → object or
field, decimal → index (checked against count or the keyed entries), `*` → a
wildcard flag on the cursor. `om_next()` advances a cursor through every field
under a prefix, skipping fields whose `when` fails on this board and, for `get -d`
and `save`, fields equal to the default. The same cursor drives `get`, `diff`,
`save`, tab completion and the web continuation.

### 12.4 set and check

`set`: find, parse the value into a scratch buffer by type, range-check, store into
`cfg_cand`, bump `state.cfg.pending`. Nothing is written on error. Checks
(`check=`) compile to a small bytecode (load field, compare, subset, in-set,
and/or) interpreted by `om_check()`: smaller than generated C per rule, and one
interpreter.

### 12.5 Getters

A getter fills `om_st` (a union of the `struct st_<obj>` buffers) for one
instance; the emitter then walks that object's state fields over the buffer. So
state costs no RAM beyond one instance, and a getter that reads MDIO is called
once per port, not once per field.

As shipped (P5-5): a two-level state object's getter gets level 0's element
(the logical port) and level 1's index in `om_ix1`; a streamed one's getter finds
its instance from `om_key` (7.8).

---

## 13. Tab completion and the line editor

In `cmd_editor.c`, which already handles the line:

- **TAB** completes the word under the cursor: a verb in word 1; then path segments
  (object and field names from `om_obj[]`/`om_field[]`; indices from the count, the
  existing keyed entries, or the board's ports, honouring `when`); then the value
  (enum and flag words, `on/off`, `saved/default` for `load`). One match: complete
  and add `.` or a space. Several: complete the common prefix; a second TAB lists
  them (on a new line, then redraws the prompt and the line).
- **Help** (the owner, 2026-11-20): help is the CLI's, English only, one source
  (the schema's `tip=` strings), and **optional**: `OM_HELP=0` compiles it out, with
  its text, for builds that are tight on space (then `help` says so, one line).
  - `h`, `help` and `?` are one command. Alone: one line per verb with its usage, and
    a line about Tab. With a path (`help port`, `help port.2`, `help port.2.speed`,
    `help state.port`): the object's tip and index form, then one line per field.
    Tab completes after `help`, for discovery. `?` is help only as the first word of
    a line; typed elsewhere it's a character (passwords).
  - **The help line is an interface** (the schema page below, and the owner's planned
    CLI-based mini UI parse it), so its format is fixed and versioned:
    `<path> <type> <range|words> [default <v>] [unit <u>] "<tip>"`, e.g.
    `port.N.speed enum auto|10m|100m|1g|2g5|5g|10g default auto "Autonegotiate, or force one speed"`.
    `help -a` prints every object's lines (state and actions included), for a dump.
  - The tips are a data blob the generator writes into the default config sector's
    free tail (0x39200-0x39ffe, 3,582 B; the compact map has no other free region),
    word-dictionary packed, read a chunk a step: not bank code, no web flash.
    check_layout guards the boundary both ways; om_gen fails the build, never drops
    text silently, when the packed tips don't fit (then tips get shorter).
  - The existing UI tabs get no tooltips; the web UI is left alone except where a
    migration forces it. One new **Schema** page (a nav item at the end) is a
    read-only reference: it fetches `help -a` over `POST /cli` and lists every
    object and field. A small generic renderer (< 1 KB of app.js), never stale;
    with OM_HELP=0 it says help isn't in this build.
  (The `?` key of the original design was dropped in P3D for bytes.)
- The candidate walk is `om_next()` over one level, so completion is a few hundred
  bytes of editor code on top of the engine.
- **History**: up/down recall over a small ring (512 B) local to the editor. The
  2 KiB `cmd_history` ring and its role in saving go.
- **Prompt**: `<hostname>[*]> ` with `*` while the candidate differs from running,
  and `[confirm 42]` during a confirm window.

The web console card gets the same completion through `POST /cli` with a
`complete <line>` request (an internal verb that returns the candidate words); the
UI's path inputs use schema.js directly.

---

## 14. The web: one endpoint, and the UI

### 14.1 Endpoints

| Endpoint | Does |
|---|---|
| `POST /cli` | body: one or more CLI lines; reply: their output, `text/plain`, each command ending in `ok` or `err ...`. Runs through the existing webcmd path (queued to the cli task, a line per step, captured into `outbuf`; ui-httpd.md section 4) |
| `GET /`, static files | unchanged (`find_entry`, flash continuation) |
| `POST /login` | unchanged; adds `HttpOnly` to the cookie |
| `POST /upload`, `/loader`, `/mdata` | unchanged (binary bodies); progress in `state.fw.*` |
| `GET /config` | the saved config text (download), password omitted; kept because it streams from flash for free |

Deleted: the 22-branch GET chain and every `*.json`, `/cmd`, `/cmd_log`,
`/cmd_log_clear`, `/reset`, `/l2_del.json`, multipart `POST /config` (`config_take`,
`handle_config_fragment`). Restoring a file is the UI reading it locally and
posting its lines to `/cli` as `set` lines, then `diff`, then `apply`.

### 14.2 Replies longer than one buffer

Every RAM-built reply must fit `outbuf` (2,500 B) today; only flash-backed replies
continue (ui-httpd.md section 4). `/cli` gets a **generator continuation**: the
cli job keeps its cursor while the reply is in flight; when `outbuf` is full the
command pauses (it does not truncate); on each ACK, after the sent bytes are
released, the job resumes and fills the next MSS. The reply ends at close, as
today. One connection (`UIP_CONNS 1`), so one cursor. This also removes decision
25's leftover long steps (`/vlanlist.json` 34 ms, the config POST parse 18.2 ms),
left to this project.

### 14.3 The UI

- **Transport**: `api()` stays (the one-request queue, timeout, 401 → login);
  `cli(lines)` posts and returns lines; `parseLines()` turns `path value` lines into
  nested objects (a dozen lines of JS).
- **schema.js** gives every widget its type, enum words, limits, default, label, tip
  and `when` rule. Board shape (`state.sys.ports`, `sfp_ports`, `ports_10g`) comes
  from the switch at load.
- **Generic widgets bound to paths**: toggle, enum select, number with limits,
  IPv4, text, port mask (the -/U/T segmented control generalised), per-port table
  (rows = ports, columns = fields), read-only state table. An edited or focused
  field is not overwritten by polling (today's per-card `dataset.dirty` guards,
  ui-httpd.md 5.19).
- **Staging UI**: edits `set` into the candidate as they are made; a bar shows
  `state.cfg.pending` with **Diff**, **Discard**, **Apply** (always with a window:
  apply 30, auto-confirm when the UI gets back, countdown otherwise) and **Save**
  (`state.cfg.dirty`). This replaces the dirty-by-log heuristic, `mergeConf` and the
  config editor modal.
- **Kept custom pieces**: the port strip and tooltip, pps rates from U64 deltas
  (BigInt), the MIB modal (names now from schema.js), the VLAN, mirror and
  isolation matrices (rebound to paths), the L2 table (streamed `get state.l2`, sort
  and filter in JS), the NTP clock, firmware upload, login, theme.
- **Tabs** are declared as lists of paths (and a few custom widgets), so most of
  index.html's per-tab static markup (about 24 KB raw) goes.
- The VLAN editor's invariants (making a port untagged in V removes it as untagged
  elsewhere, parking tagged-only ports with PVID 0) stay in the UI as widget logic:
  they are UI conveniences, and the firmware's checks (6.20) catch the rest.

### 14.4 Polling

One poll every 2.5 s: `get state.port.*.link state.port.*.speed
state.port.*.tx_good state.port.*.rx_good state.port.*.tx_bad state.port.*.rx_bad
state.cfg` (one request, several paths: `get` takes several; as shipped (P5-5)
two lines in one POST, `get state.port.link state.port.speed state.port.tx_good
state.port.rx_good` and `get state.port.tx_bad state.port.rx_bad
state.port.macs`, since a line is at most 127 B; each path calls the port's
getter once per port, 63 calls a poll, 165 ms in rtlsim). Per-tab pollers
(STP 3 s, LAG 3 s, EEE 4 s) become `get state.stp`, `get state.lag
state.lacp`, `get state.port.*.eee` on the same queue.

---

## 15. Boot, factory reset, migration of deployed switches

### 15.1 Factory default

The board's default file stays **text** in the tree, in the new path format (today
`config.txt`: `ip`, `gw`, `netmask`, giving 192.168.10.247; it becomes
`mgmt.ip 192.168.10.247` and so on). At build time the Python codec checks it
against the schema and encodes it to binary records (`default.bin`), which
`fileadder` places at `DEFAULT_CONFIG_START` as today. `cfg_default` in `__code`
holds the schema defaults the records are applied over. `do sys.factory_reset`
copies the default sector to `CONFIG_START` (today's `FL_FACTORY`).

### 15.2 Deployed switches hold old command text

Every deployed switch has an old-format config (the bench 4+2's VLAN 100 setup
among them). The firmware does **not** read it: there is no on-switch importer.

- **On the host**: `rtlpg_client.py convert old.txt > new.cfg` translates the old
  command text to `path value` lines through a table (`tools/legacy_map.py`) that
  covers the 49 `CONF_CMDS` forms plus `mac`, `sfp`, `port N duplex`; lines it
  can't translate are reported, not dropped silently. Python only, so it can be
  rough and grow as configs turn up; it is tested over every legacy file in the
  corpus.
- **The update flow converts**: `fwupdate.py` (and the bench scripts) read
  `GET /config` from the old firmware, convert it, upload the new image, and after
  the reboot post the converted lines with `set` + `apply` + `save`. Built: P7a,
  feat/om 91e6a0c. The plan file (0600, the switch's MAC in its header) lets
  `--plan FILE HOST` redo the last step; the switch is found by MAC only; it
  refuses before the upload on anything it can't carry over.
- **The firmware has no text config at all** (P3K, the owner's call 2026-11-20):
  no replay of old command text at boot, no standard → compact move of the saved
  config (`config_migrate()` and `legacy_is_config_text()` are gone), no `legacy`
  format. A config file that isn't all path lines fails the build
  (`rtlpg_client.py default`). Upload staging on the compact layout
  (0x40000-0x7a000) covers the standard layout's config sector (0x70000), so a
  text test there could take an upload's bytes for a config (P3B5's bug): with no
  move there is nothing to get wrong.
- **If a switch boots the new firmware with old text anyway** (updated from the
  old web UI, or moved from the standard layout, say): the sector is
  `state.cfg.format bad`, it boots on the default config, and the sector is
  **left untouched** until someone runs `save`, so the old text can still be read
  back (`flash r` (the console's raw read; a dbg action is P7), or the loader's console) and converted. The catch:
  the default config's management address, not the old one. A standard-layout
  switch's config at 0x70000 is not read either: the host carries it over (the
  update flow above).
- **The machine data move stays** (`mdata_migrate()`, update_apply.c): a bridge
  image from the standard layout carries the board's own blob (the
  POE-2G040210GSM's PoE controller firmware) at `MDATA_XFER_START`, which the
  first compact boot moves to `MDATA_START`. That is binary data with a magic and
  a CRC, not config text, and no `set` line can restore it.

### 15.3 The binary saved config

On flash the config is **tagged records**, defaults omitted; everywhere a person or
a tool sees it, it is text.

```
header     "\0RC1"  u16 length  u16 crc16              (CRC-16/0xA001, the mdata convention)
instance   u8 obj_id  u16 index  u8 n_fields          (index: 0 for singletons, the
                                                        number for numbered objects,
                                                        the key for keyed ones: vlan vid)
field      u8 field_id  u8 len  value[len]            (little endian, as in the struct)
```

- **Keys** are the registry's stable IDs (10.5), not struct offsets, so a field
  added, removed or moved in a later schema doesn't break an older save. An
  unknown object or field is skipped by its length; a missing one stays at its
  default; a value outside the new range keeps the default and is counted
  (`state.cfg.load.skipped`). A type change needs a new ID (enforced by the
  generator).
- **Size**: 2 bytes plus the value per non-default field, 4 per instance. Typical
  configs: under 200 B. Worst case (every field non-default: ~60 instances, ~420
  fields, 32 named VLANs, every port named with bandwidth, storm and STP settings):
  about 2.1 KB, so it always fits the 4 KiB sector. The generator prints the worst
  case and fails the build if it ever exceeds `CONFIG_LEN`.
- **The magic starts with a NUL**, so old firmware (which reads its config up to the
  first NUL or 0xff) sees an empty config and boots on its compiled-in defaults:
  a downgrade is predictable, never binary parsed as commands.
- **Code**: encoder and decoder both walk the tables the engine already has (`id`,
  `off`, size from the type, `cfg_default` for the omit test): ~0.5-1 KB in
  `om_bin.c`, less than parsing text at boot (no string matching).
- **Text views**: `get -s` and `GET /config` decode record by record into
  `path value` lines; a restore posts text lines through `set`. The Python codec
  reads and writes both forms (`rtlpg_client.py bin2txt / txt2bin`), for backups,
  the factory default and the tests.
- **Power cuts**: one sector, as today: a cut between erase and the last write
  leaves a blank or bad sector, which boots on the default (`state.cfg.format
  blank|bad`). Not worse than today; a two-sector A/B scheme would need a sector the
  compact layout doesn't have (20.21).

---

## 16. What is deleted

Sizes are code + string literals from the 8x compact build (`tools/funcsize.py`,
`funcs-8x.tsv` here; es-code-space.txt).

### 16.1 Firmware: whole files

| File | Bytes (code + strings) | Replaced by |
|---|---|---|
| `cmd_parser.c` (all `parse_*`, the dispatcher, `execute_config`, history, `cmd_save`, `print_*` helpers) | ~18.4 KB | `om_cli.c` + engine; tokenizer and number helpers reused |
| `rtl837x_stp_cli.c` | ~2.7 KB | schema |
| `ntp_cli.c`, `dns_cli.c` | ~1.2 KB | schema |
| `httpd/page_impl.c` (every JSON page) | ~10.7 KB | `get` |
| `httpd/page_stp.c` | ~2.3 KB | `get` |
| `html_out.c` JSON helpers | ~0.6 KB | |

### 16.2 Firmware: functions in modules that stay

| Module | Functions | Become |
|---|---|---|
| httpd.c | GET chain (`/x.json`), `/cmd_log*`, `/reset`, `/l2_del`, `config_take`, `handle_config_fragment` (~1.5-2 KB) | `/cli` |
| rtl837x_port.c | `port_stats_line`, `port_stats_print`, `port_l2_line`, `port_eee_status`, `port_eee_line` (printing part), `port_media_show`, `vlan_dump`, `print_vlan_ingress_port`, `print_port_ingress_filter_mode` | `om_get_port`, `om_get_l2`, `om_apply_port`, `om_apply_vlan` |
| rtl837x_phy.c | `phy_show` (918 B) | `om_get_port` |
| rtl837x_stp.c | `stp_status`, `print_field`, `print_bridge_id` | `om_get_stp*` |
| rtl837x_lacp.c | `lacp_show_line`, `lacp_show` | `om_get_lag`, `om_get_lacp` |
| rtl837x_storm.c, rtl837x_bandwidth.c, rtl837x_rldp.c, rtl837x_igmp.c | `storm_show`, `bandwidth_status`, `rldp_show`, `igmp_show` | config `get`, `om_get_loop`, `dbg` |
| ntp.c, dns.c | `ntp_show`, `ntp_show_time`, `ntp_print_offset`, `dns_show` | `om_get_ntp`, `om_get_dns` |
| sfp.c | `sfp_print` | `om_get_sfp` |
| rtl837x_cabletest.c | `cabletest_print`, `cabletest_show` | `om_get_cabletest` |
| sched_health.c | `sched_health_line`, `crash_print`, `print_state`, `print_fine` | `om_get_sched`, `om_get_reset` |
| rtl837x_leds_dump.c | `leds_dump` (1.7 KB) | `dbg` only |
| rtlplayground.c / cmd_parser.c | `print_temperature`, `print_sw_version`, `print_gpio_status`, dead HEALTH code (`health_show`, `health_loop_start`, `health_phase`, `ph_*`, `tick_pending`, `idle_ready`) | getters; deleted |
| rtl837x_lldp.c, rtl837x_port.c, rtl837x_stp.c | the hardware side of the deleted `parse_*` moves into `om_apply_*` | |

### 16.3 Firmware: data

`cmd_history[2048]` and its pointer; `vlan_names[1024]` (names into the struct);
`port_names[9][32]` (into the struct); the scattered config globals (`stp_prio`,
`stp_hello_s`, ..., `lldp_enabled`, `igmpEnabled`, `rldp_enabled`, `leds_mode`,
`session_timeout`, `passwd`, `hostname`, `syslog_state` config fields, `ntp_state`
config fields, `sfp_speed[]`, `lacp_lag_ports[]`): they become `cfg_run` fields,
read through the struct.

### 16.4 Web UI

From app.js (ui-httpd.md section 3): the data layer (~32.7 KB min / 10.3 KB gz:
per-tab JSON decoding, command builders, `CONF_CMDS`, `isConfCmd`, `postCmd`,
`mergeConf` 4.6 KB, `CONF_OVERWRITE/TOGGLE`, `saveBtn`, `doWriteConfig`, `dnsLoad`
and `ntpLoad`'s regex scraping, `cfgParseKnown`, `decodeCounters`), the
per-tab form builders (`buildPorts`, `stpBuild`, `buildLag`, `portsStatus`,
`renderInfo`'s table), most static per-tab markup in index.html.

### 16.5 Host tools and docs

`fwupdate.py`'s hand-written JSON reads and `LINK_SPEEDS` table (→ the client);
`doc/api.md`'s CLI reference (→ generated `doc/cli.md`, `doc/paths.md`); the
oldnew harness's per-command expectations (→ path-based, 18.4).

---

## 17. Size

### 17.1 Code (banked flash), 8x compact

| | KB |
|---|---|
| Removed (16.1-16.2, measured) | **~41** |
| Engine (`om.c`, `om_text.c`, `om_cli.c`, `om_job.c`, `FL_SAVE`), estimated | 4-6 |
| Tables: ~72 config + ~200 state + 87 MIB + ~20 action fields, ~8 B + name each; enum words; limits; check bytecode | ~4 |
| Banked fn-ptr tables (3 B per handler) | ~0.3 |
| `om_apply_*` / `om_get_*` glue (the hardware half of the old parsers and printers) | 4-6 |
| Binary save codec (`om_bin.c`) | 0.5-1 |
| **Net saving** | **~25-30 KB** |
| Release builds, `dbg` compiled out (register/PHY/SerDes/flash/GPIO/LED pokes) | a further ~3.5-4 |

Against ~1.1 KB free in the three banks today (554 / 71 / 507 B on the 8x).

### 17.2 RAM (xdata)

| | Bytes |
|---|---|
| `cfg` struct, per copy (sys 55, mgmt 15, dns 8, ntp 54, syslog 7, web 23, port 9×60, vlan 32×22, lag 12, stp 9 + 14 entities×7, igmp/lldp/loop 3, mirror 6) | ~1,530 |
| × 3 (running, candidate, rollback) | **+4,600** |
| `om_st` getter buffer, cursor, check stack | +200 |
| cable test results per port | +80 |
| `cmd_history` | −2,048 |
| `vlan_names` | −1,024 |
| `port_names` | −288 |
| scattered config globals | −~250 |
| editor recall ring | +512 |
| **Net** | **~+1.8 KB**, of ~27 KiB free (memory.md) |

(`config_buf`, 4,480 B, stays: it is also the header accumulator, the POST body
and the firmware upload stage. An earlier estimate in the discussion counted it as
freed; it is not.)

### 17.3 Web flash (compact region, 66,281 of 69,632 B used)

app.js code 23.3 KB gz → ~16-18 KB gz with schema.js, widgets and the staging UI
(saving ~5-7 KB gz); index.html static markup −3-4 KB gz. **~8-11 KB gz freed**,
from 3.3 KB free to ~11-14 KB free. (Languages: out of scope; `LANGS=` remains the
biggest lever.)

These are estimates. **Phase 2 measured them (2026-11-20, [PHASE2.md](PHASE2.md))**:
the engine is 6.7 KB (more with phase 3's parts: ~9 KB), tables ~30 B a field, and an
object saves about two-thirds of the code it deletes. Revised net: **~15-20 KB**, not
25-30. Still far more than the ~1.1 KB the banks have free.

---

## 18. Testing

| Layer | What it proves | Object-model tests |
|---|---|---|
| **Generator** (`test/test_om_gen.py`, the `test_sched_gen.py` pattern) | the schema is consistent | golden outputs for a small schema; broken-input cases (duplicate path, default out of range, bad check expression, missing handler, removed registered path) each fail with their message; a must-fail build variant (`OM_NEG=bad_schema`) |
| **Host unit** (`test/`, gcc + ASan + UBSan, `hw_mock`, `flash_dev`) | the engine's logic, byte for byte the firmware source | round trip (random valid struct → emit → parse → equal; text → struct → canonical text); generated per-field edge cases (11.2); C engine vs `rtlpg_client.py` codec on the same inputs (differential); libFuzzer on `set`/`load` input; staging state machine (apply, discard, confirm, timeout rollback, save refused in a window, hardware failure mid-apply restores exactly, empty diff writes zero registers); `FL_SAVE` and `load` against `flash_dev` with a power cut at every flash operation; binary codec round trips (struct → records → struct; text → struct → records → struct → text; C vs Python on the same bytes; unknown IDs and out-of-range values skipped; corrupt CRC/length rejected); the Python `convert` over every legacy file in the corpus; completion cases in `test_cmd_editor.c` |
| **Build gates** | code and RAM budgets | `resource_gate.py` (no IRAM growth, stack ≥ 120 B), `check_cross_bank.py`, the `_Static_assert`s, bank occupancy recorded per migrated object; a step over 5 ms in a `cli` state fails (`cli_steps.py`) |
| **schedtest / rtlsim** | steps stay bounded | worst cases: apply of every port and `MAX_VLANS`, `get state.port.*.mib`, `get state.l2` with a full table, `save` of the largest config; stack headroom with the engine's frames; `read_struct` on `cfg_run`/`cfg_cand` after `set` equals the client's model |
| **Old vs new** (`ovs-rtlpg/tests/oldnew`) | behaviour unchanged while the interface changes | old image + old config vs new image + the same config converted by `rtlpg_client.py convert`: registers, VLAN/L2 tables, frames must match exactly; every old command mapped to its path and compared by value; every old JSON key mapped to its state path and compared by value; existing scenarios (stp, lacp, vlan, loop, igmp, sfp, flaps, l2_big, upload) keep passing; the `config` scenario's flash region differs by design (expect file) |
| **UI** (Playwright, against rtlsim) | the UI works and safe restore is safe | per field: edit in a generic widget → `diff` shows it → apply → `get` shows it; **safe restore**: change `mgmt.ip` (and `mgmt.vlan`, own port disabled, CPU isolated) from the UI, never reconnect, run the sim past the window, the switch is back on the old address with the old registers; reset during a window boots the saved config |
| **Silicon** (the bench: a host's 8 NICs to the 8x, the DAC to the 4+2) | real hardware | VLAN/PVID changes checked with traffic from network namespaces on the bench host; cable test with the far end unplugged (open, length); the self-test kinds; safe restore with a real management cut; ^R ^R; the 4+2's VLAN 100 config converted on the host and restored through the update flow (15.2); rollout 8x → 4+2 → estate |

---

## 19. Work plan (for a wash project)

Each phase ends green on its gates and measured; each object migration is one
reviewable unit. Branch off `feat/sched` (or `main` once the scheduler is upstream;
20.1).

| Phase | Deliverable | Gate |
|---|---|---|
| **0. Decide** | the open decisions (20) settled; xdata and bank budgets re-measured on HEAD; fn-ptr vs switch dispatch measured | owner sign-off |
| **1. Schema tool** | `schema/` package, `om_gen.py`, registry, golden and broken-input tests, schema.js and client output for a toy schema | `make -C test` |
| **2. Engine + `port`** | `om*.c`, the ten verbs beside the old CLI (dotted paths go to the engine, everything else to the old parser), `port` config + state + cabletest end to end, `/cli` with continuation | host tests, fuzz, bank budget, the port numbers vs 17.1, oldnew `port` scenario |
| **3. Staging and boot** | candidate/running/rollback, apply job, confirm window, `FL_SAVE` with the binary codec, `load`, boot; `rtlpg_client.py convert` and the fwupdate flow | power-cut tests, staging state machine, safe-restore sim test |
| **4. Config objects** | one unit each: `sys`+`web`+`mgmt`+`dns`+`ntp`+`syslog`; `vlan`; `lag`; `stp`; `igmp`+`lldp`+`loop`+`mirror`; `bw`+`storm`; SFP speed on the port. Each deletes its old parser, printer, JSON page and UI rules | oldnew per object, bank occupancy recorded |
| **5. State objects** | `state.sys`, `state.sched`/`health`/`con`/`web`, `state.mgmt`, `state.sfp`, `state.l2`, `state.stp`, `state.lag`/`lacp`, `state.loop`, `state.ntp`/`dns`, `state.cfg`/`fw`/`loader`, MIB; `crash_reset(cause)` on every reset path; JSON pages gone | oldnew values by path, step bounds |
| **6. UI** | the line client, schema.js widgets, staging bar, tabs as path lists, custom widgets rebound; old data layer deleted | Playwright suite incl. safe restore; web flash budget |
| **7. Finish** | old CLI and `/cmd` deleted; tab completion; `dbg` namespace behind `DEBUG_OBJ`; generated docs; `fwupdate.py` on the client; actions not yet done (selftest kinds) | full suite, size report vs 17 |
| **8. Silicon and rollout** | 8x, then the 4+2 (converted by the update flow), then the estate in rollout order | bench checks (18) |

Parallel work once phase 2 lands: the schema for the remaining objects (Python),
the UI (against rtlsim), and the test harness additions can proceed alongside the
per-object firmware migrations.

---

## 20. Open decisions

Recommendations in **bold**; each changes the work, so each needs the owner's call.

1. **Order relative to upstreaming the scheduler** (plan.md question 6): build on
   `feat/sched` now, or wait for it to land upstream. **Build on feat/sched; it is
   fork-first anyway.**
2. **SFP ports**: `port.N.speed` on the port (the UI's model) rather than
   `sfp.S.speed` (today's CLI). **On the port.** And `port.N.enabled off` on an SFP
   port: **implement (SerDes down)**, or forbid by `when` until then.
3. **One shared candidate** for all sessions, **yes**, vs one per session.
4. **`get` reads running** by default and the candidate with `-c`, **yes**, vs the
   other way round.
5. **`save` refused** during a confirm window and with unapplied changes, **yes**.
6. **Password**: Secret, plain in flash as today, never shown by `get` or the
   download, **yes**; hashing (chacha20 is in the tree) is possible later but not
   needed to cut code.
7. **`MAX_VLANS`**: **32**, names Str(15). 64 doubles that part of the struct
   (~0.7 KB a copy).
8. **The `do` verb** for actions (staging makes `set` wrong for them), **yes**.
9. **Self-test kinds** for `port.N.selftest`: TDR (exists), PHY loopback, SerDes
   PRBS on SFP/10G ports, port-to-port traffic between two cabled ports (needs a
   two-port argument). Which ones, and in which phase? **TDR in phase 2, loopback
   and PRBS in phase 7, port-to-port later.**
10. **Stack paint in every build** (a few bytes; `state.health.stack_headroom`
    always available), **yes**.
11. **Loader boot-count confirm**: keep the 15 s automatic confirm, **yes**; tying it
    to `confirm` after a firmware update (a bad image that boots but loses the
    network rolls back through the loader) is a later option.
12. **MIB indexes 52-54 and the unnamed halves**: emit as `x52`.. until identified on
    hardware, **yes**.
13. **SFP RX-power calibration** (float polynomial): host/UI applies it, firmware
    emits the raw value and the coefficients, **yes**; integer calibration (temp,
    vcc, bias, tx power) on the switch.
14. **Saved format**: binary tagged records on flash, text everywhere else
    (decided 2026-11-20, decision 7).
15. **Old configs**: host-side Python conversion only (decided 2026-11-20,
    decision 8). Open: should the new firmware, on finding old text, keep just the
    management lines (`ip`, `gw`, `netmask`, `ip dhcp`, `vlan N mgmt`) so a switch
    updated from the old web UI stays reachable? A few hundred bytes; **only if
    web-UI updates of estate switches are expected before the estate is
    converted**.
16. **Dispatch**: banked fn-ptr tables (smaller, proven by `step_addr`), **yes**,
    confirmed by measurement in phase 0.
17. **Ports array sized 9 for every board**, **yes** (one layout), vs sized by
    `MACHINE_MIN/MAX_PORT`.
18. **Debug namespace** compiled only with `DEBUG_OBJ=1`, **yes**; rtlsim and bench
    images build with it.
19. **Strictness**: today's silent failures (`dns`, `ntp`, `session`, `storm`, `port
    lldp`) and quirks (`lldp <anything>` = off, `vlan 10 dmz` = delete, `port N on`
    = auto) become typed errors or simply can't be expressed, **yes**.
20. **CPU always a tagged member** of every VLAN (forced by apply, as today), **yes**;
    not a field.
21. **Power-cut safety of `save`**: one sector, as today (a cut mid-save boots the
    default), **yes**; an A/B pair of sectors would need a second sector the compact
    layout doesn't have.
