# VLAN

> The VLANs are paths (doc/cli.md): `vlan.V.name`, `vlan.V.members`,
> `vlan.V.tagged`, with a port's `port.N.pvid` and `port.N.ingress`. The old
> `vlan` command is gone.

The RTL827x provides support for up to 4096 802.1Q VLANs, each port can be
assigend a PVID.

## VLAN control
VLANs are controlled by the VLAN table. Configuration of entries is done as
for the L2 entries, apart from that the ASIC does not add entries by itself.

Adding a VLAN entry is done by setting:

```
RTL837x_TBL_DATA_IN_A = 02 0v vv vv
```
where 0x02 designates a valid entry and vvvvv is a 20bit field made of lower 10bits designating whether a
port is a member of a VLAN. The higher 10 bits are '0' for tagged ports
and '1' for untagged ports (the bit-logic definition is `v = (~members) ^ tagged ^
members`). Ports are numbered 0-9, 9 being the CPU-port. Note that for
the RTL8372 devices, the ports are not in their physical order.

Once RTL837x_TBL_DATA_IN_A is set, the entry is added to the table by:

```
RTL837X_TBL_CTRL = 0V VV TT CC
CC: TBL_WRITE | TBL_EXECUTE
VVV: VLAN-Id
TT: 0x02 (TBL_VLAN)
CC: 0x03 (TBL_WRITE | TBL_EXECUTE)
```
The ASIC will clear bit 0 once the entry has been added.

An entry is deleted by adding an invalid entry (00 instead of 0x02 in
RTL837x_TBL_DATA_IN_A).

A port is assigned a PVID by setting the PVID-bits of the corresponding
register of the port. 2 ports share a register. An odd port uses bits [23:12],
an even port uses bits [11:0].  The base register is
RTL837x_PVID_BASE_REG (0x4e1c) and the registers go to 0x4e2c so that also
the CPU-Port may have a PVID.

Register RTL837x_REG_INGRESS (0x4e10) allows to define the ingress rules of
a port. 2 bits define a rule and bits 0-19 are being used. A value of 00
defines no filtering, 01 (0x01) allows only tagged packets, while 10 (0x02)
allows only untagged packets to enter a port.

Register RTL837X_VLAN_PORT_IGR_FLTR (0x4e18) enables or disables ingres VLAN
filtering, each bit corresponds to given port (port0 -> bit0, port9 -> bit9).
When enabled, incomming package's vlan tag is checked against VLAN membership
on given port. When package contains VLAN not in member list, package is dropped.

The default PVID on all port is 1, ingress VLAN filtering is enabled and all types of
frames are accepted on input on all ports.

By default, the ports transmit Ethernet frames with Realtek's proprietary
tag format. By setting bit 6 (0x40) of the respective port configuration
registers 0x1238, 0x1338, ...

## VLAN API
The code currently provides the following functions:
```
void port_pvid_set(uint8_t port, __xdata uint16_t pvid) __banked;  // one port: a LAG's members share one (apply checks)
uint16_t port_pvid_get(uint8_t port) __banked;
void vlan_create(void) __banked;   // reads from global vlan_settings
int8_t vlan_get(register uint16_t vlan) __banked;  // returns data in sfr_data
void vlan_delete(uint16_t vlan) __banked;

```

# VLAN configuration (the console and POST /cli)
A VLAN is `vlan.V`, V 1-4094, made by its first `set` and removed by `del`:

```
set vlan.<V>.members <ports>
  its member ports, front-panel numbers: `1-4,9`, `none`.

set vlan.<V>.tagged <ports>
  the members that send it tagged; the others send it untagged.

set vlan.<V>.name <text>
  up to 15 characters, starting with a letter, no spaces.

del vlan.<V>
  removes it.

get vlan
  every VLAN; `get vlan.<V>` one.
```

`apply` checks the whole candidate first: a VLAN's tagged ports must be
members, and every `port.N.pvid` and `mgmt.vlan` must name a VLAN that
exists (or 0). So a VLAN still some port's PVID can't be deleted alone.

The management VLAN, a port's PVID and its ingress rule:

```
set mgmt.vlan <VLAN-ID>
  Restricts network access to the switch (web UI, syslog) to the given
  VLAN; 0 disables the filter. The default is 1. Apply it with a confirm
  window (`apply 60`): an unreachable VLAN would lock out the web UI.

set port.<N>.pvid <VLAN-ID>
  assigns the PVID to a port, numbered as on the casing.

set port.<N>.ingress tagged|untagged|all
  admits frames on the port only when tagged, untagged, or any (the default).
  `set port.*.ingress all` sets every port. The CPU port can't be changed.

get port.*.pvid port.*.ingress
  shows them.
```

A saved config whose PVID or `mgmt.vlan` names a VLAN that is missing boots
with that reference reset (a PVID to 1, `mgmt.vlan` to 0), and
`state.cfg.boot_defaults` says so.
