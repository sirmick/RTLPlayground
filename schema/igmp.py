# igmp: IGMP snooping (DESIGN.md 6.9). Apply is the old igmp on/off: on
# traps IGMP to the CPU and drops unknown IP multicast, off floods it again.
# Deferred to P7: state.dbg.igmp.port.N (each port's IGMP_PORT_CFG word);
# until then `igmp show` prints it.

obj("igmp", apply="om_apply_igmp", order=60, tip="IGMP snooping")
cfg("enabled", Bool, False, tip="Send IP multicast only to the ports that joined its group")
