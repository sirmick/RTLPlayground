# lag.L: a link aggregation group (DESIGN.md 6.7). Off keeps its members,
# unused. Static writes them to the trunk; LACP hands them to the protocol,
# which writes the ones it aggregates. Apply's checks (6.20.4): a port in at
# most one LAG that isn't off, a LAG's members share one port.N.pvid.
# Fixed: LACP system priority 0xffff, port priority 0xff, key L+1, active,
# fast timeout.

obj("lag", count=4, apply="om_apply_lag", check="om_check_lag", order=35, tip="A link aggregation group")
cfg("mode", Enum("lagmode", "off static lacp"), "off", tip="Off, a static trunk, or negotiated by LACP")
cfg("members", PortMask(), 0, tip="Its ports")
cfg("hash", Flags("laghash", "spa smac dmac sip dip sport dport"), "smac,dmac,sip,dip,sport,dport",
    tip="What picks a frame's member: source port, MACs, IPs, L4 ports")
message("lag_port", "a port is in one lag at most")
message("lag_pvid", "a lag's members need one pvid; they differ on")
message("lag_hash", "hash needs a field")
