## HTTP API

The switch firmware implements a JSON API which underpins the webUI.
Port numbers referred to throughout the API are physical port numbers
and should reflect the physical numbers visible on the switch.

The following endpoints are supported:
 * /bandwidth.json
 * /eee.json
 * /isolation.json
 * /lag.json
 * /lldp.json
 * /mirror.json
 * /stp.json
 * /storm.json
 * /vlan.json?vid=<VLANID>
 * /vlanlist.json



## bandwidth
```json
[
    {"port":1,"iLimited":false,"iBW":"0x0fffff","iAction":"drop","eLimited":false,"eBW":"0x0fffff"},
    {"port":2,"iLimited":false,"iBW":"0x0fffff","iAction":"fc","eLimited":false,"eBW":"0x0fffff"},
    ...
]
```
Array of port properties


| Property | Description
|----------|------------
| `port` | The physical port number
| `iLimited` | Apply ingress (inbound) limit
| `iBW` | The ingress rate limit applied
| `iAction` | Action to preform on limit exceedence, "drop" - drop frame "fc" - use TCP flow control
| `eLimited` | Apply egress (outbound) limit
| `eBW` | The egress rate limit applied

## eee
```json
[
    {"port":1,"isSFP":false,"advertised":["2.5G","1G","100M"],"linkPartner":["1G","100M"],"active":true},
    {"port":2,"isSFP":false,"advertised":["2.5G","1G","100M"],"linkPartner":[],"active":false},
    ...
    {"port":9,"isSFP":true}]
```
Array of ports EEE configuration and status

| Property | Description
|----------|------------
| `port` | The physical port number
| `isSFP` | Whether the port is SFP
| `advertised` | Links advertised by the port (only for non-SPF links)
| `linkPartner` | Links advertised by the remote-device (only for non-SPF links)
| `active` | Whether EEE is enabled on the port (only for non-SPF links)

## information

Gone (the object model, P5-1): `POST /cli` with `get state.sys` (version,
build date, machine, PCB, chip, ports, image size, uptime, MAC, temperature,
flash size, PoE, button), `get state.sys.reset` (the last reset and its
cause) and `get state.loader`. The config it echoed is read with `get`.


## isolation
```json
[
    {"port":1,"allow":[1,2,3,4,5,6,7,8,9,0]},
    {"port":2,"allow":[1,2,3,4,5,6,7,8,9,0]},
    ...
]
```
Array of ports

| Property | Description
|----------|------------
| `port` | The physical port number
| `allow` | The list of physical ports this port is permitted to send to, 0 is the CPU

## lag
```json
{
    "lags":[
        {"lagNum":0,"type":"static","members":[],"hash":"3f"},
        {"lagNum":1,"type":"lacp","members":[6,7],"active_members":[6],"valid":true,"aggregator":"ce11a001a010","hash":"7e"},
        ...
    ],
    "ports":[
        {"p":1,"lag":255,"a":"00","pt":"00","rs":0,"rx":"0000","psys":"000000000000"},
        ...
        {"p":6,"lag":1,"a":"3f","pt":"3f","rs":4,"rx":"0053","psys":"ce11a001a010"},
        {"p":7,"lag":1,"a":"07","pt":"40","rs":0,"rx":"0000","psys":"000000000000"},
        ...
    ]
}
```
Object representing static and LACP lags

| Property | Description
|----------|------------
| `lags` | Array of LAG objects
| `ports` | Array of current port status objects


### LAG objects

| Property | Description
|----------|------------
| `lagNum` | 0-4 the identifier for the LAG
| `type` | `static` or `lacp`
| `members` | The physical port numbers that are configured to be in the LAG
| `active_members` | For LACP LAGs, the members that currently participating
| `valid` | Whether the LACP LAG is currently valid
| `aggregator` | MAC Address of the aggregating device
| `hash` | Load balancing methods to use

### Current port status objects

| Property | Description
|----------|------------
| `p` | Physical port number
| `lag` | Number of the LAG the port is participating in
| `a` | Actor
| `pt` | Partner
| `rs` | RX State
| `rx` | Number of received LACPDUs
| `psys` | Remote system mac address

## lldp
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


## mirror
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


## stp
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

## vlan
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

## storm
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

## vlanlist
```json
{
    "mgmt":1,"vlan":[
        {"id":1,"name":""},
        {"id":2,"name":"vlanTwo"},
        ...
    ]
}
```
| Property | Description
|----------|------------
| `mgmt` | The management VLAN ID
| `vlan` | Array of VLAN objects

### VLAN Objects

| Property | Description
|----------|------------
| `id` | The VLAN id
| `name` | The VLAN name
