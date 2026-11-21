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
| `do path [arg]` | An action, now: `do sys.reset`, `do sys.factory_reset`, `do port.3.cabletest`, `do mgmt.dhcp_renew`, `do ntp.sync`, `do sched.clear`, `do l2.delete K`, `do dns.lookup NAME`. |

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

## Help

`h`, `help` and `?` are one command. Alone, one line per verb. With a path
(`help port.2.speed`), the object's tip and one line per field; `help -a`,
every path (the web UI's Schema page reads it). Tab completes after `help` as
after `get`. The lines have a fixed format other tools parse, after a first
`help 1` line naming its version:

```
<path> <type> <form> [default <v>] [unit <u>] "<tip>"
port.*.speed enum auto|10m|100m|1g|2g5|5g|10g default auto "Auto, or forced; 5g/10g on 10G ports only"
```

English only. An image built with `OM_HELP=0` has no help (3.5 KB of text
in flash outside the banks, 0.9 KB of code): `help` answers
`err help: not in this build`.

## What is on the model

Everything you configure: `sys` (hostname, LEDs), `mgmt` (address, mode,
VLAN), `dns`, `ntp`, `syslog`, `web` (password, session timeout), `vlan.V`
(name, members, tagged), `lag.L` (mode off/static/lacp, members, hash), `stp`
with `stp.port.N` and `stp.lag.L`, `igmp`, `lldp`, `loop`, `mirror`, `port.N`
(name, enabled, speed (an SFP cage's too), duplex, mtu, pvid, ingress, ingress
filter, LLDP, EEE and its speeds, isolation, bandwidth limits) and
`port.N.storm.T` (storm control); and every `state.*` path, `state.sched`'s
runs a second (`.rate`) and `state.sched.load` among them. Only the switch's
MAC keeps its old command, `mac` (`sys.mac` is deferred).

`dns lookup <name>` is `do dns.lookup <name>` (the answer in `get
state.dns.lookup`). `l2 forget` is `do l2.flush` (it flushes the
learned MAC entries; the table itself is `get state.l2`), and `do l2.delete K` deletes entry K. The debugging
tools stay: `regget`, `regset`, `phyget`, `physet`, `sdsget`, `sdsset`,
`leds`, `flash`; `sds`, `gpio` and `igmp show` are `get state.dbg` (the SerDes
modes, the GPIO inputs, each port's IGMP_PORT_CFG, as raw words). An image
built with `OM_DBG=0` (for release) has none of them: they answer
`Error: Unknown command`. `leds` also needs `OM_DBG_LEDS=1` (off by default:
its dump is 2.5 KB and one long step). Removed, with no command:
`ip`, `gw`, `netmask`, `hostname`, `passwd`, `session`, `led`, `ntp ...`,
`syslog ...`, `dns server`, `pvid`, `ingress`, `isolate`, `eee`, `mtu`,
`port N ...`, `reset`, `health`, `history`, `time`, `version`. Use their
paths, or `get state.sys` for the version. Any other word: `Error: Unknown command`.
