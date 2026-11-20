# The CLI: get, set, apply, save

The serial console and the web (`POST /cli`, doc/api.md) take the same lines.
The configuration, the switch's state and its actions are one tree of paths:
`port.3.speed` (config), `state.port.3.speed` (state), `do port.3.cabletest`
(an action). Every line is a verb and a path. Its output ends with one status
line: `ok`, or `err <path>: <reason>`.

```
> get port.3
port.3.name uplink
port.3.enabled on
port.3.speed auto
...
ok
> set port.3.pvid 5000
err port.3.pvid: 0-4094
```

## The verbs

| Verb | Does |
|---|---|
| `get [path]` | Every field under the path, from running (what the switch does now). No path: the top-level names. `get -c path`: from the candidate. `get -d [path]`: only what differs from the default. `get -s`: the saved config (flash), all of it. |
| `set path value` | Changes the candidate. A wildcard index sets every instance: `set port.*.eee off`. `set path` alone empties a text field (`set sys.hostname`: derived again). The value's type and range are checked at once. |
| `del path` | A field back to its default, in the candidate. |
| `diff` | The candidate against running: `- path old` and `+ path new` lines. `diff -s`: running against saved (what `save` would write). |
| `apply [seconds]` | Checks the whole candidate, then makes it running, object by object. With seconds: a confirm window (below). |
| `confirm` | Keeps a change applied with a window. |
| `discard` | Candidate back to running. |
| `load saved\|default` | The saved config, or the factory default, into the candidate; then `diff`, `apply`. |
| `save` | Running to flash. Refused while the candidate differs (`err save: apply or discard first`) or a window is open. |
| `do path [arg]` | An action, now: `do sys.reset`, `do sys.factory_reset`, `do port.3.cabletest`, `do mgmt.dhcp_renew`, `do ntp.sync`, `do sched.clear`. |

A `set` changes nothing on the switch until `apply`, and nothing survives a
restart until `save`. The prompt shows the state: `*>` while the candidate has
changes (`state.cfg.pending` counts them), and `[confirm N]` while a window
runs. There's one candidate, shared by the console and every web session.

## Values

`on`/`off`; decimal numbers (range checked: the error gives the range); one
word of a list (the error lists the words); `a.b.c.d` addresses; text to the
end of the line, printable, without `"` or `\`; port lists as front-panel
numbers, `1-4,9`, `none`, `all`, and `cpu` where a field allows it. A Secret
(`web.password`) is set like text, and `get` prints it as `*`.

## The confirm window

`apply 60` applies, then waits up to 60 s for `confirm`. Without it the switch
goes back to what it had, by itself. Use it for any change that can cut you off:
the management address or VLAN (`mgmt.*`), your own port, isolation of the
CPU. `state.cfg.confirm_s` counts down; `state.cfg.apply` says `confirm` while
it waits, then `done`, or `rolled_back`. A reset during the window boots the
saved config, which is why `save` waits for the window to close.

```
> set mgmt.ip 192.168.10.20
> apply 60
ok
                  (reconnect to 192.168.10.20 within 60 s)
> confirm
ok
> save
ok
```

## Save, load, the factory default

The saved config is the fields that differ from the default, as binary
records in one flash sector. Everywhere you see it, it's text: `get -s`, and
`GET /config` on the web. A restore is those lines posted back, then `apply`
and `save`.

- `do sys.factory_reset` writes the factory default and resets. To see the
  difference first: `load default`, `diff`, then `apply` and `save`.
- `load saved` + `apply` drops unsaved changes.
- The factory default is the board's `config.txt` at build time (the address
  192.168.10.247/24, gateway .10.1), over the schema's defaults. A switch whose
  saved sector is blank, bad, or old text from firmware before the object
  model boots on it (`state.cfg.format` says which). An old switch keeps its
  config across the update only through `tools/fwupdate.py`, which converts
  it (doc/automation.md).

## Tab completion

Tab at the end of a console line completes the verb, then the path a segment
at a time (objects, port numbers, indices, fields; actions after `do`), then
words (`load saved|default`). When several names fit, it completes what they
share (`get sy` becomes `get sys`: sys and syslog), and a second Tab lists them
and redraws the line. It beeps when nothing fits, or while a job runs.

## Help (after P3D-c)

Not on feat/om yet. As DESIGN.md 13 has it: `h`, `help` and `?` are one command.
Alone, one line per verb. With a path (`help port.2.speed`), the object's tip
and one line per field, in a fixed format other tools parse:

```
port.N.speed enum auto|10m|100m|1g|2g5|5g|10g default auto "Autonegotiate, or force one speed"
```

## What is on the model, and what still has its old command

On the model (get/set/apply/save): `sys` (hostname, LEDs), `mgmt` (address,
mode, VLAN), `dns`, `ntp`, `syslog`, `web` (password, session timeout),
`port.N` (name, enabled, speed (an SFP cage's too), duplex, mtu, pvid, ingress,
ingress filter, LLDP, EEE and its speeds, isolation, bandwidth limits),
`port.N.storm.T` (storm control), and every `state.*` path.

Not yet. Their old commands still work, and their settings survive `save` only
once they move:

| Object | Old command, until | The paths after |
|---|---|---|
| VLANs | `vlan`, until P4-2 | `vlan.V.name`, `.members`, `.tagged` |
| LAGs, LACP | `lag`, `laghash`, `lacp`, until P4-3 | `lag.L.mode`, `.members`, `.hash` |
| STP | `stp ...`, until P4-4 | `stp.*`, `stp.port.N.*`, `stp.lag.L.*` |
| IGMP snooping, LLDP, loop detection, mirroring | `igmp`, `lldp`, `loop`, `mirror`, until P4-5 | `igmp.*`, `lldp.*`, `loop.*`, `mirror.*` |
| the switch's MAC | `mac` | `sys.mac` (deferred) |

`dns lookup <name>` stays a command, and so does `l2 forget` (it flushes the
learned MAC entries; the table itself is `get state.l2`). The debugging
tools stay: `regget`, `regset`, `phyget`, `physet`, `sds`, `sdsget`,
`sdsset`, `gpio`, `leds`, `flash`. Removed, with no command:
`ip`, `gw`, `netmask`, `hostname`, `passwd`, `session`, `led`, `ntp ...`,
`syslog ...`, `dns server`, `pvid`, `ingress`, `isolate`, `eee`, `mtu`,
`port N ...`, `reset`, `health`, `history`, `time`, `version`. Use their
paths, or `get state.sys` for the version. Any other word: `Error: Unknown command`.
