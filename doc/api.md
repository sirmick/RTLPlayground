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
| `POST /l2_del.json?idx=N` | Deletes L2 entry N (the L2 tab's delete). |
| `GET /`, `/index.html`, `/login.html`, `app.js`, `favicon.ico` | The web UI's files. |

Gone, onto paths: `/status.json`, `/ports.json`, `/traffic.json` (`get state.port`,
`port.*`); `/eee.json` (`port.N.eee`, `state.port.N.eee_active`); `/isolation.json`
(`port.N.isolate`); `/information.json` (`get state.sys`, `state.sys.reset`,
`state.loader`); `/l2.json` (`get state.l2`), `/counters.json` (`state.port.N.mib`);
`/cmd_log`, `/cmd_log_clear` (no command history any more); `/lag.json` (`get lag`,
`state.lag`, `state.lacp.port`).

## The JSON pages left

Each is a GET, needs the cookie, and goes when its object moves:

| Page | Until | Then |
|---|---|---|
| `/vlan.json?vid=V`, `/vlanlist.json` | P4-2 | `vlan.V.*` |
| `/stp.json` | P4-4 | `stp.*`, `state.stp` |
| `/lldp.json`, `/loop.json`, `/mirror.json` | P4-5 | `lldp.*`, `loop.*`, `mirror.*` |
| `/bandwidth.json`, `/storm.json` | P4-6 | `port.N.bw_*`, `port.N.storm.*` |

## bandwidth
```json
[
    {"port":1,"iLimited":false,"iBW":"0x0fffff","iAction":"drop","eLimited":false,"eBW":"0x0fffff"},
    {"port":2,"iLimited":false,"iBW":"0x0fffff","iAction":"fc","eLimited":false,"eBW":"0x0fffff"},
    ...
]
```
Array of port properties (until P4-6)


| Property | Description
|----------|------------
| `port` | The physical port number
| `iLimited` | Apply ingress (inbound) limit
| `iBW` | The ingress rate limit applied
| `iAction` | Action to preform on limit exceedence, "drop" - drop frame "fc" - use TCP flow control
| `eLimited` | Apply egress (outbound) limit
| `eBW` | The egress rate limit applied

## lldp (until P4-5)
```json
{
    "on":1,
    "portStatus":[
        {"port":1,"name":"","permitted":true},
        ...
    ]
}
```
| Property | Description
|----------|------------
| `on` | Global LLDP on/off, in off the switch will forward LLDPDU's (like a "dumb switch")
| `PortStatus` | Array of LLDP Port Configurations

### LLDP Port Configurations

| Property | Description
|----------|------------
| `port` | Physical port number
| `name` | The name of the port
| `permitted` | if true, the switch transmits LLDPBUs on this port, if false it does not


## mirror (until P4-5)
```json
{
    "enabled":false,
    "mirrorPort":1,
    "mirror_rx":[],
    "mirror_tx":[]
}
```
| Property | Description
|----------|------------
| `enabled` | Whether port mirroring is enabled or not
| `mirrorPort` | The port that will receive the mirrored data
| `mirror_rx` | List of physical port numbers that the mirror will receive a copy of received packets for
| `mirror_tx` | List of physical port numbers that the mirror will receive a copy of transmitted packets for


## stp (until P4-4)
```json
{
    "on":true,"type":"rstp","prio":8,"hello":2,"maxage":20,"fwd":15,"txhold":6,"pcs":"long","bh":"flood","tcs":"0x000002a8","rMaxage":20,"rFwd":15,"rootPrio":"8000","rootMac":"010203040506","myMac":"010203040506","cost":"0x00000000","weRoot":true,"tc":"0x0000",
    "ports":[
        {"p":1,"st":3,"role":2,"f":69,"pc":"0x00000000","prio":128,"p2":0,"lk":1,"lg":0,"db":"8000010203040506","dp":"8001","dc":"0x00000000"},
        {"p":2,"st":3,"role":0,"f":69,"pc":"0x00000000","prio":128,"p2":0,"lk":0,"lg":0,"db":"8000010203040506","dp":"8002","dc":"0x00000000"},
        ...
        {"p":102,"st":3,"role":2,"f":69,"pc":"0x00000000","mbr":[6],"prio":128,"p2":0,"lk":1,"lg":0,"db":"8000010203040506","dp":"8066","dc":"0x00000000"}
    ]
}
```
| Property | Description
|----------|------------
| `on` | Enabled/Disabled globally
| `type` | "rstp" (rapid) or "stp" (legacy)
| `prio` | Bridge Priority
| `hello` | Hello time (s)
| `maxage` | Max Age (s)
| `fwd` | This device's configured foward delay (s)
| `txhold` | Transmit Hold
| `pcs` | Path cost ("short" or "long")
| `bh` | BPDU behaviour while disabled, "flood" or "filter"
| `tcs` | Seconds since last topology change
| `rMaxage` | Root bridge max age (s)
| `rFwd` |  Root bridge forward delay (s)
| `rootPrio` | Root bridge priority
| `rootMac` | Root bridge MAC address
| `myMac` | This device MAC address
| `cost` | Cost to reach the root bridge
| `weRoot` | true if we are the root bridge
| `tc` | Number of topology changes
| `ports` | Array of STP Port Status

### STP Port Status
| Property | Description
|----------|------------
| `p` | Physical port number, if > 100 it is LAG number + 100
| `st` | Port state 
| `role` | STP port role
| `f` | Port flags (such as edge, root, BPDUGuard etc)
| `pc` | Path cost
| `mbr` | List of port members (for LAGs)
| `prio` | Port Priority
| `p2` | Point-to-point 
| `lk` | Previous link state that STP saw
| `lg` | Link partner is using legacy STP (not RSTP/MSTP etc)
| `db` | Designated bridge
| `dp` | Designated port
| `dc` | Designated cost

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

## storm (until P4-6)
```json
[
    {
        "port":1,
        "bcast":{"enabled":true,"unit":"kbit/s","rate":"0x000037"},
        "mcast":{"enabled":false,"unit":"pps","rate":"0x000000"},
        "ucast":{"enabled":false,"unit":"pps","rate":"0x000000"},
        "umcast":{"enabled":false,"unit":"pps","rate":"0x000000"}
    }
]
```
Array of port settings
| Property | Description
|----------|------------
| `port` | Physical port number
| `bcast` | Broadcast traffic settings
| `mcast` | Multicast traffic settings
| `ucast` | Unknown unicast traffic settings
| `umcast` | Unknown multicast traffic settings

* Setting
| Property | Description
|----------|------------
| `enabled` | Storm control on or off
| `unit` | The unit of the rate "pps" or "kbit/s"
| `rate` | The value of the limit

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
