# state.lag.L, state.lacp, state.lacp.port.N (DESIGN.md 7.10): the LAGs as
# they run. Their config (lag.L.*) is P4-3's; `lag show` and lag.json show
# it until then.
#
# Deferred for room (P5-4): state.lacp.running (an object of its own for
# one Bool: it is on while a LAG is in LACP mode, lag.L.mode says that), and
# state.lacp.port.N.partner_key, .partner_port, .partner_sys_prio,
# .partner_port_prio (nothing shows them today).

state("lag", count=4, get="om_get_lag")
st("active", PortMask(), tip="The members aggregated now: LACP's choice, or a static LAG's")
st("agg_valid", Bool, tip="LACP: a partner aggregator is chosen")
st("aggregator", MAC(), tip="LACP: the partner system it aggregates with")

LACPST = Flags("lacpst", "activity timeout aggregation sync collecting distributing defaulted expired")
state("lacp.port", count=PORTS, get="om_get_lacp_port")
st("lag", U8(), tip="The LACP LAG it is in, 0 none")
st("actor", LACPST)
st("partner", LACPST)
st("rx_state", Enum("lacprx", "initialize port_disabled expired defaulted current"))
st("rx_pdus", U16())
st("partner_sys", MAC())
