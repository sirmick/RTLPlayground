# sys: the switch as a whole (DESIGN.md 6.1 and 8): its name and the LEDs,
# and the actions. sys.mac waits (the mac command stays): see P4-1's report.

obj("sys", order=0, apply="om_apply_sys", derive="om_derive_sys", tip="The switch")
cfg("hostname", Str(23), "", derived=True, tip="The name sent by DHCP and LLDP; empty: RTLPlayground- and the MAC's last three bytes")
cfg("led", Enum("led", "on ports_off off"), "on", tip="The LEDs: all on, the ports' off, or all off")
action("reset", "om_do_sys_reset", tip="Reset the switch; the web's reply goes out first")
action("factory_reset", "om_do_sys_factory_reset",
       tip="Write the default config to flash, then reset")
action("rnd", "om_do_sys_rnd", tip="Six random bytes from the chip, as an event line")
message("flash_busy", "the flash is busy")
