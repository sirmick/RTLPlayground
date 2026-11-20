# dns: the resolver's servers (DESIGN.md 6.4). The lookup (`dns lookup`) and
# state.dns are P5's.

obj("dns", apply="om_apply_dns", order=12, tip="The DNS resolver")
cfg("server1", IPv4, "0.0.0.0", tip="The first DNS server; 0.0.0.0: none")
cfg("server2", IPv4, "0.0.0.0", tip="The second DNS server; 0.0.0.0: none")
