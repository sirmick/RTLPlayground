# lldp: LLDP (DESIGN.md 6.9); port.N.lldp blocks it on a port. On traps
# 01:80:c2:00:00:0e to the CPU and sends; off sends a TTL of 0 on the ports
# it was sent on, and forwards it again.

obj("lldp", apply="om_apply_lldp", order=62, tip="LLDP")
cfg("enabled", Bool, False, tip="Send LLDP frames on the ports that permit it")
