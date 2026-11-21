# port.N: a front-panel port (DESIGN.md 6.5 and 7.5). N is the front-panel
# number; the struct is indexed by logical port.

obj("port", count=PORTS, apply="om_apply_port", check="om_check_port", order=40,
    tip="A front-panel port")
cfg("name", Str(31), "", tip="Shown in the UI and sent as the LLDP port description")
cfg("enabled", Bool, True, when="copper", tip="Administrative state")
cfg("speed", Enum("speed", "auto 10m 100m 1g 2g5 5g 10g"), "auto",
    tip="Auto, or forced; 5g/10g on 10G ports only")
cfg("duplex", Enum("duplex", "auto full half"), "auto", when="copper",
    tip="Duplex for a forced 10m or 100m")
cfg("mtu", U16(64, 16383), 16383, tip="Largest frame accepted, in bytes", unit="B")
cfg("pvid", U16(0, 4094), 1, tip="VLAN for untagged frames; 0 parks a tagged-only port")
cfg("ingress", Enum("ingress", "all tagged untagged"), "all",
    tip="Acceptable frame types", label="Accept")
cfg("ingress_filter", Bool, True, tip="Drop frames of VLANs the port is not a member of", label="Filter")
cfg("lldp", Enum("lldp", "permit block"), "permit", tip="Send LLDP on this port")
cfg("eee", Bool, True, when="copper", tip="Energy-efficient Ethernet")
cfg("eee_speeds", Flags("eee", "100m 1g 2g5 5g 10g"), "100m,1g,2g5,5g,10g", when="copper",
    tip="Capped by the port's speed")
cfg("isolate", PortMask(cpu=True), "OM_ISOLATE_DEFAULT",
    tip="Ports this port may send to", label="Sends to")
cfg("bw_in_kbps", U32(0, 10000000), 0, unit="kbit/s", label="Ingress limit",
    tip="Ingress limit, 16 kbit/s steps, 0 none")
cfg("bw_in_mode", Enum("bwmode", "fc drop"), "drop",
    tip="Pause the sender, or drop")
cfg("bw_out_kbps", U32(0, 10000000), 0, unit="kbit/s", label="Egress limit",
    tip="Egress limit, as bw_in_kbps")
action("cabletest", "om_do_port_cabletest", when="copper",
       tip="Time-domain reflectometry on the four pairs", label="Cable test")
message("duplex", "duplex full or half needs speed 10m or 100m")
message("speed_10g", "speed 5g and 10g only on a 10G port")
message("ct_running", "a cable test is running")
message("ct_phy", "no cable test on this PHY")
message("ct_off", "the port is off")
message("pvid", "pvid: no such vlan")
message("pvid_one", "port.#.pvid")	# boot's fix (1), named in state.cfg.boot_defaults
message("bw_16", "a limit is 0 or a multiple of 16")
message("sfp_speed", "an SFP port takes auto, 100m, 1g, 2g5 or 10g")

obj("storm", parent="port", count=Enum("storm", "bcast mcast ucast umcast"), apply="om_apply_port_storm",
    check="om_check_port_storm", tip="Storm control")
cfg("rate", U32(0, 10000000), 0)
cfg("unit", Enum("sunit", "pps kbps"), "pps")
message("storm_pps", "pps at most 1048575")

state("port", count=PORTS, get="om_get_port")
st("link", Enum("link", "down up"))
st("speed", Enum("lspeed", "10m 100m 1g 500m 10g 2g5 5g unknown"))
st("duplex", Enum("lduplex", "half full"), when="copper")
st("media", Enum("media", "copper sfp"))
st("ms_role", Enum("role", "slave master"), when="copper", label="Role")
st("pause_tx", Bool)
st("pause_rx", Bool)
st("macs", U16(), label="Devices learned")
st("tx_good", U64)
st("rx_good", U64)
st("tx_bad", U32())
st("rx_bad", U32())
st("lp_adv", Flags("lpadv", "10h 10f 100h 100f 1000h 1000f 2g5 5g 10g"), when="copper")
st("lag", U8())
st("eee.active", Bool, when="copper")
st("eee.lp", Flags("eee", "100m 1g 2g5 5g 10g"), when="copper")
st("cabletest.state", Enum("ctstate", "idle running done timeout"), when="copper")
for p in "abcd":
    st("cabletest.pair.%s.result" % p,
       Enum("ctres", "unknown ok open short cross_short mismatch"), when="copper")
    st("cabletest.pair.%s.length_m" % p, Scaled(1), when="copper", unit="m")
