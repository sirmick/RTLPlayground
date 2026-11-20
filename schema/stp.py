# stp, stp.port.N, stp.lag.L (DESIGN.md 6.8): the spanning tree's config.
# Its state is state.stp (state_stp.py). Apply's check (6.20.5):
# 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1).
#
# One entity's fields for a port and for a LAG. A LAG member's stp.port.N is
# kept and has no effect while it is in the LAG: its stp.lag.L applies.
# The runtime bits (operational edge, guard tripped) are state.
#
# /stp.json keeps, until P7: tcs, rMaxage, rFwd (state.stp.tc_age_s,
# root_maxage_s, root_fwd_s, deferred by P5-4), per entity f's runtime bits
# (guard_tripped, operational edge), lk, lg, db, dp, dc (link, legacy,
# dbridge, dport, dcost), and what state.stp has too (rootPrio, rootMac,
# myMac, cost, weRoot, tc; p, st, role, mbr).

obj("stp", apply="om_apply_stp", check="om_check_stp", order=45, tip="The spanning tree")
cfg("enabled", Bool, False, tip="Run the spanning tree")
cfg("version", Enum("stpver", "rstp stp"), "rstp", tip="RST BPDUs, or 802.1D's Config BPDUs")
cfg("priority", U8(0, 15), 8, tip="Bridge priority, in units of 4096")
cfg("hello_s", U8(1, 10), 2, tip="Hello time", unit="s")
cfg("maxage_s", U8(6, 40), 20, tip="Max age", unit="s")
cfg("fwd_delay_s", U8(4, 30), 15, tip="Forward delay", unit="s")
cfg("txhold", U8(1, 10), 6, tip="BPDUs a port sends a second at most")
cfg("pathcost", Enum("stpcost", "long short"), "long",
    tip="Automatic port costs: 802.1Q's, or 802.1D-1998's")
cfg("bpdu", Enum("stpbpdu", "flood filter"), "flood", tip="BPDUs while the tree is off")
action("counters_clear", "om_do_stp_counters_clear", tip="Zero the BPDU counters")
message("stp_times", "needs 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1)")
message("stp_prio", "priority is a multiple of 16")

for name, count, order in (("stp.lag", 4, 46), ("stp.port", PORTS, 47)):
    fn = name.replace(".", "_")
    obj(name, count=count, apply="om_apply_" + fn, check="om_check_" + fn, order=order,
        tip="A spanning tree port" if count == PORTS else "A LAG as one spanning tree port")
    cfg("enabled", Bool, True, tip="Take part; off: forwarding, no BPDUs")
    cfg("edge", Enum("stpedge", "auto on off"), "auto",
        tip="An edge port forwards at once; auto: after 3 s without a BPDU")
    cfg("cost", U32(0, 200000000), 0, tip="Path cost; 0: from the link speed")
    cfg("priority", U8(0, 240), 128, tip="Port priority, a multiple of 16")
    cfg("guard", Enum("stpguard", "none bpdu root"), "none",
        tip="bpdu: a BPDU in disables it; root: it never becomes the root port")
    cfg("filter", Bool, False, tip="Neither send nor take BPDUs")
    cfg("p2p", Enum("stpp2p", "auto on off"), "auto", tip="A point-to-point link")
    action("mcheck", "om_do_" + fn + "_mcheck", tip="Try RSTP again with the neighbour")
    action("clear_guard", "om_do_" + fn + "_clear_guard", tip="Bring it back after BPDU guard disabled it")
