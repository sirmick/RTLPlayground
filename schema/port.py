# port.N: a front-panel port (DESIGN.md 6.5 and 7.5). N is the front-panel
# number; the struct is indexed by logical port.

obj("port", count=PORTS, apply="om_apply_port", check="om_check_port", order=40,
    tip="A front-panel port")
cfg("name", Str(31), "", tip="Shown in the UI and sent as the LLDP port description")
cfg("enabled", Bool, True, when="copper", tip="Administrative state")
cfg("speed", Enum("speed", "auto 10m 100m 1g 2g5 5g 10g"), "auto", when="copper",
    tip="Autonegotiate, or force one speed; 5g and 10g only on 10G ports")
cfg("duplex", Enum("duplex", "auto full half"), "auto", when="copper",
    tip="Duplex for a forced 10m or 100m")
cfg("mtu", U16(64, 16383), 16383, tip="Largest frame accepted, in bytes")
cfg("pvid", U16(0, 4094), 1, tip="VLAN for untagged frames; 0 parks a tagged-only port")
cfg("ingress", Enum("ingress", "all tagged untagged"), "all",
    tip="Acceptable frame types")
cfg("ingress_filter", Bool, True, tip="Drop frames of VLANs the port is not a member of")
cfg("lldp", Enum("lldp", "permit block"), "permit", tip="Send LLDP on this port")
cfg("eee", Bool, True, when="copper", tip="Energy-efficient Ethernet")
cfg("isolate", PortMask(cpu=True), "OM_ISOLATE_DEFAULT",
    tip="Ports this port may send to")
action("cabletest", "om_do_port_cabletest", when="copper",
       tip="Time-domain reflectometry on the four pairs")

state("port", count=PORTS, get="om_get_port")
st("link", Enum("link", "down up"))
st("speed", Enum("lspeed", "10m 100m 1g 500m 10g 2g5 5g unknown"))
st("duplex", Enum("lduplex", "half full"), when="copper")
st("media", Enum("media", "copper sfp"))
st("ms_role", Enum("role", "slave master"), when="copper")
st("pause_tx", Bool)
st("pause_rx", Bool)
st("macs", U16())
st("tx_good", U64)
st("rx_good", U64)
st("tx_bad", U32())
st("rx_bad", U32())
st("eee_active", Bool, when="copper")
st("cabletest.state", Enum("ctstate", "idle running done timeout"), when="copper")
for p in "abcd":
    st("cabletest.%s.result" % p,
       Enum("ctres", "unknown ok open short cross_short mismatch"), when="copper")
    st("cabletest.%s.length_m" % p, Scaled(1), when="copper")
