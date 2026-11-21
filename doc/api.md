## HTTP API

The API is the CLI. `POST /cli` takes console lines as its body and answers
with what the console would print: `get`, `set`, `apply`, `save`, `do` and
paths (doc/cli.md). The web UI uses nothing else for what is on the object
model. A few JSON pages remain, for the objects not migrated yet; each goes
when its object moves.

Port numbers everywhere are the front-panel numbers printed on the switch.

## Logging in

```
POST /login            body: pwd=<password>    (application/x-www-form-urlencoded)
```

A good password gets `302` to `/index.html` and a `session` cookie (HttpOnly).
A wrong one gets `302` to `/login.html`. Every other request needs the
cookie, or gets `401`. A session ends after `web.session_timeout_s` without a
request, and when `web.password` changes.

## POST /cli

```
POST /cli              body: CLI lines, one per line (text/plain)
-> 200 text/plain      each line's output, ending with "ok" or "err <path>: <reason>"
```

```
$ curl -b cookies.txt http://192.168.10.247/cli --data-binary $'set port.3.name uplink\napply'
ok
ok
```

- **The status is always 200.** Whether a line worked is in its `ok` or `err` line.
- **Lines run in order**, as typed on the console. `set ...` lines and an `apply` in one
  POST is the usual change. Use `apply N` and a later `confirm` when the change can cut
  you off (doc/cli.md, the confirm window).
- **Continuation:** a reply of any length is streamed, an output buffer at a time, and
  ends when the switch closes the connection. Nothing is truncated: `get state.port` is
  ~5.5 KB, `get state.l2` pages 25 entries at a time (`get state.l2.K-`).
- **One connection at a time** (uIP with one connection): another is refused until the
  first closes, so a client retries. Closing the connection mid-reply stops the rest of
  the run.
- **Commands not on the model** (`stp`, ...) take their old syntax here too,
  until they move.
- `POST /cmd` is the old endpoint for the old commands. It streams the same way, and is
  kept for the old UI's tabs until P6.

## The other endpoints

| Request | Does |
|---|---|
| `GET /config` | The saved config as text: `get -s`, the password left out. |
| `POST /upload` | A firmware image, multipart (`uploadedfile`): the switch checks it, answers, resets, and the loader installs it. Compact boards take `rtlplayground-install-*.bin`. `tools/fwupdate.py` does it all, and carries an old config over. |
| `POST /loader` | A new loader image, staged; installed at the next boot. |
| `POST /mdata` | The board's machine-data blob (PoE firmware) into its own region. |
| `POST /reset` | Resets after the reply (`do sys.reset` does the same over `/cli`). |
| `GET /`, `/index.html`, `/login.html`, `app.js`, `favicon.ico` | The web UI's files. |

Gone, onto paths: `/status.json`, `/ports.json`, `/traffic.json` (`get state.port`,
`port.*`); `/eee.json` (`port.N.eee`, `.eee_speeds`, `state.port.N.eee.active`, `.eee.lp`); `/isolation.json`
(`port.N.isolate`); `/information.json` (`get state.sys`, `state.sys.reset`,
`state.loader`); `/l2.json` (`get state.l2`), `/counters.json` (`state.port.N.mib`);
`/bandwidth.json`, `/storm.json` (`port.N.bw_*`, `port.N.storm.T.*`);
`/cmd_log`, `/cmd_log_clear` (no command history any more); `/lag.json` (`get lag`,
`state.lag`, `state.lacp.port`); `/l2_del.json` (`do l2.delete K`); `/stp.json`
(`get stp`, `get state.stp`).

## The JSON pages left

Each is a GET, needs the cookie, and goes when its object moves:

| Page | Until | Then |
|---|---|---|
| `/vlan.json?vid=V`, `/vlanlist.json` | P4-2 | `vlan.V.*` |

## vlan (until P4-2)

`/vlan.json?vid=V`: one VLAN.
```json
{
    "untagged_members":[1,2,3,4,5,8,9],
    "tagged_members":[],
    "name":"",
    "pvid":[1,2,3,4,5,8,9]
}
```
| Property | Description
|----------|------------
| `untagged_members` | Ports that will send participate in this VLAN by transmitting untagged frames
| `tagged_members` | Ports that will send participate in this VLAN by transmitting and receiving tagged frames
| `name` | The name of the VLAN
| `pvid` | List of physical ports that have their Port VLAN ID set to this VLAN

## vlanlist (until P4-2)
```json
{
    "vlan":[
        {"id":1,"name":""},
        {"id":2,"name":"vlanTwo"},
        ...
    ]
}
```
| Property | Description
|----------|------------
| `vlan` | Array of VLAN objects

### VLAN Objects

| Property | Description
|----------|------------
| `id` | The VLAN id
| `name` | The VLAN name
