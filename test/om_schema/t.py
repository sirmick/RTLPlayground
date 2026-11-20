# A throwaway schema for test_om (test/Makefile builds it with the real one):
# every object kind and type that no real object uses yet. Names start with
# "t" so they never meet a real object's. Its IDs (registry.txt) start at 200.

obj("tsys", apply="om_apply_tsys", check="om_check_tsys", derive="om_derive_tsys")
cfg("hostname", Str(23), "", tip="derived: empty is RTLPlayground-<mac>")
cfg("ip", IPv4, "192.168.2.2")
cfg("mask", Mask4, "255.255.255.0")
cfg("mac", MAC(auto=True), "auto")
cfg("password", Secret(20), "1234")
cfg("offset", I16(-600, 600), 0)
cfg("tz", TZ(), "+00:00")
cfg("mirror", Port(none=True), "none")
cfg("led", Enum("tled", "on ports_off off"), {"rtl8372": "on", "rtl8373": "off"})
cfg("hash", Flags("thash", "spa smac dmac sip dip sport dport"), "smac,dmac")
cfg("lp", Flags("tlp", "10h 10f 100h 100f 1000h 1000f 2g5 5g 10g"), "none")
action("rnd", "om_do_tsys_rnd")

obj("tvlan", keyed=U16(1, 4094), max=4, apply="om_apply_tvlan")
cfg("name", Str(15), "")
cfg("members", PortMask(), 0)
cfg("tagged", PortMask(), 0)
default_entry(1, members=0x1ff)

obj("tlag", count=4, apply="om_apply_tlag")
cfg("mode", Enum("tlmode", "off static lacp"), "off")
cfg("members", PortMask(), 0)

obj("tstp")
cfg("enabled", Bool, False)

obj("tstp.port", count=PORTS, apply="om_apply_tstp_port")
cfg("cost", U32(0, 200000000), 0)
cfg("edge", Enum("tedge", "auto on off"), "auto")

obj("tstorm", parent="port", count=Enum("tstype", "bcast mcast ucast umcast"), apply="om_apply_tstorm")
cfg("rate", U32(0, 1048575), 0)
cfg("unit", Enum("tsunit", "pps kbps"), "pps")

state("tsys", get="om_get_tsys")
st("bridge", BridgeId)
st("ip", IPv4)
st("offset", I16())
st("lp", Flags("tlp", "10h 10f 100h 100f 1000h 1000f 2g5 5g 10g"))
