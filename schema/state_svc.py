# state.ntp, state.dns, state.loop (DESIGN.md 7.11): what the services found.
# Their config (ntp.*, dns.*, loop.enabled) is P4-1's and P4-5's; until then
# `ntp`, `dns` and `loop` print it.
#
# Deviations: state.ntp.time is seconds since 1970 (UTC), not a Time type.
# Deferred for room (P5-3): state.ntp.next_sync_s (83 B of 32-bit code;
# nothing shows it today), state.loop.pair.N (a Port type, ~100 B in the
# engine, and a register read a port; the events name the pair).

state("ntp", get="om_get_ntp")
st("synced", Bool)
st("time", U32(), tip="Seconds since 1970, UTC")
st("local_offset_min", I16(), tip="The time zone with DST, minutes east of UTC")
st("dst_active", Bool)
st("last_sync_s", U32())
st("stratum", U8())
st("server_ip", IPv4)

state("dns", get="om_get_dns")
st("dhcp_server", IPv4, tip="The server DHCP gave, 0.0.0.0 if none")
st("lookup.name", Str(47), tip="The current or last lookup")
st("lookup.status", Enum("dnsst", "none pending done failed"))
st("lookup.ip", IPv4)

state("loop", get="om_get_loop")
st("looped", PortMask(), tip="Ports whose probe came back (none while detection is off)")
st("blocked", PortMask(), tip="Ports loop detection put in blocking (STP off)")
