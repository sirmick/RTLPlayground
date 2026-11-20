# state.stp, state.stp.port.N, state.stp.lag.L (DESIGN.md 7.9): the
# spanning tree as it runs. Its config is stp.* (stp.py).
#
# Deviation: root_port is a word, the port's number or lag.L (none: this
# switch is the root), not a Port type: a LAG isn't a port.
# P7 added what /stp.json had (it is gone): root_maxage_s, root_fwd_s,
# tc_age_s; per entity guard_tripped, link, legacy, dbridge, dport, dcost.
# Not added (no surface shows them): msg_age_s, loop_held.

state("stp", get="om_get_stp")
st("bridge", BridgeId)
st("root", BridgeId)
st("is_root", Bool)
st("root_port", Enum("stproot", "none 1 2 3 4 5 6 7 8 9 lag.1 lag.2 lag.3 lag.4"))
st("root_cost", U32())
st("tc_count", U16(), tip="Topology changes since the boot")
st("tc_age_s", U32(), tip="Since the last topology change", unit="s")
st("root_maxage_s", U8(), tip="The max age in use: the root's", unit="s")
st("root_fwd_s", U8(), tip="The forward delay in use: the root's", unit="s")

# one layout for both (rtl837x_stp.c fills either; a LAG's members last):
# a LAG member port is off
for name, count in (("stp.port", PORTS), ("stp.lag", 4)):
    state(name, count=count, get="om_get_" + name.replace(".", "_"))
    st("state", Enum("stpst", "off block learn fwd"))
    st("role", Enum("stprole", "disabled root designated alternate backup"))
    st("edge", Bool)
    st("bpdu_age_ms", U32(), tip="Since the last BPDU in")
    st("rx_bpdu", U32())
    st("tx_bpdu", U32())
    st("rx_tc", U32(), tip="BPDUs in with TC, and TCNs")
    st("tx_tc", U32())
    st("guard_tripped", Bool, tip="BPDU guard disabled it: do clear_guard")
    st("link", Bool)
    st("legacy", Bool, tip="The neighbour speaks 802.1D: Config BPDUs")
    st("dbridge", BridgeId, tip="The designated bridge (this switch when it is)")
    st("dport", U16(), tip="The designated port ID: priority * 256 + number")
    st("dcost", U32(), tip="The designated bridge's cost to the root")
    if name == "stp.lag":
        st("members", PortMask(), tip="none: no such LAG (its ports are entities of their own)")
