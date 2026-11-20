# Spanning Tree (STP / RSTP)

> On feat/om STP keeps the old `stp` commands until P4-4, which moves them to
> `stp.*`, `stp.port.N.*` and `stp.lag.L.*` (doc/cli.md). The state is on the model
> already: `get state.stp`, `state.stp.port.N`, `state.stp.lag.L`.

The switch can take part in a spanning tree (IEEE 802.1D / 802.1w) so that
redundant links between bridges are blocked instead of forming a loop. The
implementation elects a root bridge from the BPDUs it receives, picks the
cheapest path to it as the root port, blocks ports on which a better bridge is
already designated, promotes ports to forwarding once their listen period
expires, drops information a neighbour stopped sending, and blocks a port on
which it sees its own BPDU.

STP is config on the object model (`stp`, `stp.port.N`, `stp.lag.L`): set it
on the command line, or over POST /cli, and apply it.

## Quick start

```
set stp.enabled on      # start participating
apply
set stp.enabled off     # stop, all ports back to forwarding
apply
```

Live status is on the Spanning Tree page of the web UI (or `/stp.json`),
and on the console via `get state.stp`.

With no other bridge around, the switch elects itself root and every port ends
up forwarding — you can leave it on safely. `save` keeps the settings over a
reboot:

```
set stp.priority 15
set stp.port.1.edge on
set stp.enabled on
apply
save
```

`get stp` shows the config (`get -d stp` what differs from the defaults);
`del stp`, `del stp.port`, `del stp.lag` put each back to its defaults.

## Hardware background

BPDUs are addressed to `01:80:C2:00:00:00`, a reserved link-local group. The
ASIC's Reserved-Multicast action for that address decides what happens to the
frame.

While STP runs, the action is *trap*. A trap delivers to the port named in
`EXT_CPU_CTRL` (0x6724), whose reset value 0xf names no port, and with that
value the trapped frame is dropped at ingress. `trap_init()` sets it to 9, the
port the 8051 sits behind, once at startup. The trap needs no per-VLAN L2
entries, so it does not depend on which VLANs exist or on the PVIDs, and turning
STP on no longer walks the whole VLAN table. BPDUs reach the CPU on a port that
STP holds in blocking with either action: measured on a SWTGW218AS by sending
BPDUs into a blocked port with forward and with trap, both ports stopped being
edge and waited out the forward delay. The chip exempts this address from the
ingress state check; the slow-protocols address used by LACP is not exempt.

A trapped frame reaches the CPU in the format it came in with: unlike a
forwarded one it has no VLAN tag inserted after the RTL tag, and the RTL tag
carries the trap reason. `stp_in()` reads the BPDU from the offset
`TRAP_RX_BODY()` returns, which works for both.

With STP off, the action goes back to *forward* and a static L2 multicast
entry per VLAN in use (`port_l2mc_set()`) targets all ports, restoring the
transparency an unmanaged switch is expected to have, so a surrounding
spanning tree can span *through* this device.

The port's acceptable-frame-type filter treats the two actions differently.
BPDUs are untagged, so a port set to admit tagged frames only
(`port.N.ingress tagged`) drops a forwarded BPDU before it reaches the CPU, while a
trapped one passes. Both were measured on a SWTGW218AS with port 3 set to tagged only:
LACPDUs, delivered by forward, stopped arriving on port 3, and BPDUs sent into
the same port, delivered by trap, kept arriving. A port that admits tagged
frames only therefore takes part in STP.

Port states live in `RTL837X_MSTP_STATES (0x5310)`, two bits per port:
`00` disabled, `01` blocking, `10` learning, `11` forwarding. The boot
(`stp_defaults()`) and STP off write every port forwarding, as at power-on
(0xfffff): a soft reset keeps what the register held. A port in
blocking forwards nothing between ports, but it still sends what the CPU
hands it and still passes a received BPDU up to the CPU, which is what lets
loop detection go on working on a port it has already blocked.

## Timers

`stp_timers()` runs at 50 Hz (the main loop idles on the 200 Hz system tick and
STP is called every fourth pass), which is what `STP_HZ` in `rtl837x_stp.h`
encodes. All configured values are in seconds:

| setting | default | range |
|---|---|---|
| `stp.hello_s` | 2 | 1–10 |
| `stp.maxage_s` | 20 | 6–40 |
| `stp.fwd_delay_s` | 15 | 4–30 |
| `stp.txhold` | 6 | 1–10 (BPDUs a port sends a second) |

802.1D also requires `2 * (fwd - 1) >= maxage >= 2 * (hello + 1)`. `apply`
checks it on the whole candidate, so the values can be set in any order;
an apply that breaks it is refused and nothing changes:
`err stp: needs 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1)`. The saved
config is checked the same way at boot: a saved `stp` this build refuses (timers
from another release) boots at its defaults, named in `get state.cfg.boot_defaults`,
and the rest of the config applies.

The values above are what the switch uses while it is the root. Otherwise it
takes max age and forward delay from the BPDUs on its root port, as 802.1D
requires, and passes them on in its own BPDUs with the message age one second
higher; the hello time stays its own.

A port entering the tree discards for one forward delay, then learns
addresses without forwarding for a second one, and only then forwards (an
edge port skips the wait). In RSTP mode on a point-to-point link
(`p2p auto` or `on`) it can go faster: a designated port that is still
discarding sets the proposal flag, and forwards as soon as the bridge on the
other side answers with an agreement. When the root port receives a proposal,
the switch first puts its other non-edge designated ports back to discarding
(they propose in turn), then forwards the root port and answers with an
agreement; this sync happens once per root port, later proposals are only
answered. An alternate or backup port that receives a proposal does the same
sync once for the information it holds and answers with an agreement in a
BPDU with the alternate role, so the bridge on the other side does not have
to wait out its timers; it stays discarding itself. An agreement is accepted
from a root or an alternate port on the other side. A new root port forwards at once, after every port that was root within
the last forward delay has been put back to discarding; such a recent root
port stays closed, even to an agreement, until that forward delay is over.
A new root port that was a backup port within the last two hello times does
not take the fast path and waits for its timers.

A designated port that hears worse information from a bridge that still
claims to be designated and learning or forwarding on the same link has a
dispute: usually one direction of the link is lost and the other bridge does
not hear this switch's BPDUs. The port goes back to discarding, and every
further such BPDU restarts its listen period, so it does not forward while
the dispute lasts.

With `stp.version rstp` each port still talks 802.1D to a neighbour that
does: once a Config BPDU or TCN arrives and the migrate time (3 s since the
port came up or last switched) is over, the port sends Config BPDUs, uses
the 802.1D TC window and reports changes with TCN if it is the root port. An
RST BPDU after the migrate time switches it back, and so does a link that
comes up again or `do stp.port.N.mcheck`. Any worse information heard on a designated port, disputed or
not, is answered with a BPDU at once instead of at the next hello, so the
other bridge learns sooner that it is not the designated one. Information heard on a port stays
valid for three of the sender's hello times; a BPDU whose message age has
reached its max age is not used at all. When the root port's information
expires the switch moves to the next best port, or reclaims the root role if
there is none.

Among ports offering the same root, the lowest root path cost wins, then the
designated bridge, the designated port ID, and finally this switch's own port
ID. A worse BPDU from a different bridge does not replace what a port holds;
a worse one from the same designated bridge and port does.
The number in a port ID is the one on the front panel, and 101 and up for
the LAGs, so it matches the port labels on both ends of a link.

## Topology changes

A topology change is detected when a non-edge port enters forwarding. That
port, and every other forwarding non-edge port, then carries the TC flag in
its BPDUs for hello time plus one second (max age plus forward delay with
`stp.version stp`), and each of the other ports flushes the addresses it
learned. The root port takes part too: while its TC window runs it sends
BPDUs toward the root, so the change reaches the rest of the network. With
`stp.version stp` the root port sends TCN instead, until a Config BPDU with
TCA comes back.

A TC flag received on a forwarding root or designated port is passed on the
same way to the other ports, not back to the port it came in on; a port that
is still discarding or learning drops it. A legacy TCN is acknowledged with
TCA and then treated like a received TC. A port that turns
alternate, loses its link, or is blocked for a loop flushes its own addresses
without raising a topology change, and an edge port never raises one.

## Bridge settings

```
stp.priority 0-15       # bridge priority = n * 4096, default 8 (32768)
stp.version rstp|stp    # RST BPDUs (default) or legacy Config BPDUs
stp.hello_s, stp.maxage_s, stp.fwd_delay_s, stp.txhold
stp.pathcost long|short # automatic port costs, 802.1D-2004 (default) or 802.1D-1998
stp.bpdu flood|filter   # BPDUs while STP is off: pass them on (default) or drop them
```

The bridge with the lowest priority wins the root election; ties are broken by
the MAC address. If you do not want this switch to become the root of an
existing network, give it a worse priority than the current root — `stp.priority 15`
(61440) is the usual "never me" value.

A port without a configured cost takes it from its link speed. The long
method uses the 802.1D-2004 values (20000 for 1G, 2000 for 10G); the short
one the 802.1D-1998 table for bridges that still use 16-bit costs (100 for
10M, 19 for 100M, 4 for 1G, 2 for 10G, and 3 for 2.5G and 5G, which that
table does not list). All bridges in a network should use the same method.

`stp.bpdu filter` keeps BPDUs from being passed between ports while STP is
off, instead of the default flooding that lets a surrounding spanning tree
run through the switch. It takes effect at once when STP is off, otherwise
when STP is turned off. The L2 entries for the VLANs in use are written by the
stp task, a share of the VLAN table a step, after the apply.

## Per-port settings

```
stp.port.N.enabled on|off          # take part in STP, or stay plain forwarding
stp.port.N.edge auto|on|off        # host-facing port handling (default: auto)
stp.port.N.cost 0-200000000        # path cost, 0 = automatic (20000)
stp.port.N.priority 0-240          # port priority, a multiple of 16 (128)
stp.port.N.guard none|bpdu|root
stp.port.N.filter on|off           # neither send nor accept BPDUs
stp.port.N.p2p auto|on|off
do stp.port.N.mcheck               # try RSTP again on a port that fell back to STP
do stp.port.N.clear_guard          # bring back a port BPDU guard disabled
```

A port priority that isn't a multiple of 16 is refused by apply
(`err stp.port.N: priority is a multiple of 16`). A running port whose
`enabled` or `edge` changes starts over: forwarding if it is off or an
edge, else from discarding.

**edge** — an edge port forwards immediately and does not trigger a
topology change when its link comes and goes; `auto` promotes a port to edge
after three seconds without a BPDU, and demotes it as soon as one arrives. Use
`edge on` for ports where only hosts are attached.

**guard** — `bpdu` disables a port as soon as a BPDU arrives on it (a host port
should never see one; `do stp.port.N.clear_guard` brings it back, as do
`enabled` off and on, STP off and on, or the link going down); `root` keeps a port from ever becoming the path to the
root, which protects an existing topology from a newly attached bridge that
claims a better priority.

**filter** — the port neither sends nor accepts BPDUs. Useful when the device
on the far side reacts badly to them (some unmanaged switches with loop
prevention cut the link) but you still want STP on the rest of the ports.

## Link aggregation

A LAG (Link Aggregation Group) is one port of the tree, with its own roles,
timers and states. The switch supports up to 4 LAGs; each one takes the place
of its member ports in the STP status and gets its own row on the Spanning
Tree page. A group carries its own path cost, priority, edge, guard and
point-to-point settings, the same fields as a port:

```
stp.lag.L.enabled, .edge, .cost, .priority, .guard, .filter, .p2p
do stp.lag.L.mcheck, do stp.lag.L.clear_guard
set stp.lag.1.cost 10000    # the group decides, not its members
```

LAGs are configured as `lag.L`. A member's own `stp.port.N` is kept and has no
effect while the port is in the LAG; it applies again when the port leaves.
Membership is read from the
aggregation registers once a second, so a group changing under LACP is picked
up without any coordination between the two; a port that joins or leaves a
group, and the group itself, start over from discarding. The automatic path
cost of a group is the one of its lowest member that has a link.

The hardware keeps no STP state for a LAG as a whole, so every state change is
written to all member ports, in a single register write. BPDUs go out through
the lowest member that has a link and carry the group's own port id, and a
BPDU received on any member belongs to the group. Losing one member of a live
LAG is not a topology change; the group only goes down with its last link.

## Status

The Spanning Tree page shows the elected root (priority and MAC), the path cost
to it, the root port, the topology-change counter and the time since it last
moved, the max age and forward
delay in use (the root's, when this switch is not the root) and, per port, the
live state read from the ASIC, or "No link", together with the configured
options. For a port taking part in the tree it also shows the designated
bridge, port and cost, and whether it talks RSTP or 802.1D to its neighbour;
the Check button runs `do stp.port.N.mcheck`. The live data (not the config:
`get stp`) is available as JSON:

```
GET /stp.json
```

The Counters section of the same page shows per port the BPDUs received and
sent, those of them that carried the topology change flag (TCNs included),
and the time since the last BPDU arrived. The counters start from zero when
STP turns on, and `do stp.counters_clear` resets them; the topology change counter is not
reset.

`get state.stp` shows the same on the console (or over POST /cli): the
bridge and root IDs, whether this switch is the root, the root port
(`lag.L` for a LAG) and cost, the topology changes, and per port and LAG
(`state.stp.port.N`, `state.stp.lag.L`) the state, role, edge, the time
since the last BPDU in milliseconds and the four counters. A port is
reported with one of the roles root, designated, alternate, backup or
disabled; disabled covers a port with STP turned off on it, one shut down by
BPDU guard and one without link. A port in a LAG, and a LAG with no
members, read as off.

An own BPDU received back on another port can temporarily hold its sending
port in discarding, even if that sending port has received no BPDUs itself.
When the hold expires, auto-edge is restored if the sending port has remained
silent. A continuing loop keeps the hold active.

BPDUs shorter than their type allows, judged by the 802.3 length field, are
dropped: 7 bytes for a TCN, 38 for a Config BPDU and 39 for an RST BPDU,
LLC header included.

## Limitations

* One spanning-tree instance; no MSTP, no per-VLAN trees.
* A backup port, one that hears the BPDUs of another port of this switch,
  is blocked and reported as Backup, but keeps sending BPDUs: loop detection
  on that segment relies on them.
