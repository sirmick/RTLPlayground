# loop: loop detection with the chip's RLDP engine (DESIGN.md 6.9); what it
# found is state.loop. While STP is off it blocks one port of a loop.

obj("loop", apply="om_apply_loop", order=64, tip="Loop detection")
cfg("enabled", Bool, False, tip="Probe every port for loops; with STP off, block one port of each")
