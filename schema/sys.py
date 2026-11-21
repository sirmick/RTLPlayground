# sys: the switch as a whole (DESIGN.md 6.1 and 8): its name, its MAC (the
# old `mac` command's, now saved), the LEDs, and the actions.

obj("sys", order=0, apply="om_apply_sys", check="om_check_sys", derive="om_derive_sys", tip="The switch")
cfg("hostname", Str(23), "", derived=True, tip="Sent by DHCP and LLDP, no space; empty: RTLPlayground-<mac>")
cfg("led", Enum("led", "on ports_off off"), "on", tip="The LEDs: all on, the ports' off, or all off", label="LEDs")
cfg("mac", MAC(auto=True), "auto", tip="The switch's MAC; auto: the board's (its flash, else from the chip)")
action("reset", "om_do_sys_reset", tip="Reset the switch; the web's reply goes out first")
action("factory_reset", "om_do_sys_factory_reset",
       tip="Write the default config to flash, then reset")
action("rnd", "om_do_sys_rnd", tip="Six random bytes from the chip, as an event line")
message("flash_busy", "the flash is busy")
message("hostname_space", "sys.hostname: no space")
message("mac_bad", "sys.mac: unicast, its first three bytes not all 0")
