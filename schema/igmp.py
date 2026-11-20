# igmp: IGMP snooping (DESIGN.md 6.9). Apply is the old igmp on/off: on
# traps IGMP to the CPU and drops unknown IP multicast, off floods it again.

obj("igmp", apply="om_apply_igmp", order=60, tip="IGMP snooping")
cfg("enabled", Bool, False, tip="Send IP multicast only to the ports that joined its group")
