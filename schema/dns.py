# dns: the resolver's servers (DESIGN.md 6.4), and a lookup: `do dns.lookup
# NAME` starts it, state.dns.lookup.status and .ip have the answer (the
# old `dns lookup` command's).

obj("dns", apply="om_apply_dns", order=12, tip="The DNS resolver")
cfg("server1", IPv4, "0.0.0.0", tip="The first DNS server; 0.0.0.0: none")
cfg("server2", IPv4, "0.0.0.0", tip="The second DNS server; 0.0.0.0: none")
action("lookup", "om_do_dns_lookup", arg="text", tip="Look NAME up; get state.dns.lookup has the answer")
message("dns_name", "a name, up to 47 characters")
message("dns_busy", "a lookup is running")
