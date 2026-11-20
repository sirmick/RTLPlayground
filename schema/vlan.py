# vlan.V: a VLAN, keyed by its VID (DESIGN.md 6.6). The CPU is a tagged
# member of every VLAN (apply adds it), so it is in neither mask. VLAN 1
# exists by default, every port an untagged member; it may be deleted.

const("MAX_VLANS", 64)
obj("vlan", keyed=U16(1, 4094), max="MAX_VLANS", apply="om_apply_vlan", check="om_check_vlan", order=8,
    tip="A VLAN")
cfg("name", Str(15), "", tip="A name, starting with a letter")
cfg("members", PortMask(), 0, tip="Its ports")
cfg("tagged", PortMask(), 0, tip="The members that send it tagged")
default_entry(1, members="OM_PORTS_USER")
message("vlan_tagged", "tagged ports must be members")
message("vlan_name", "a name starts with a letter and has no space")
