# sys: the switch as a whole (DESIGN.md 6.1 and 8). Actions for now; the
# config (hostname, mac, led) joins it with P4-1.

obj("sys", order=0, tip="The switch")
action("reset", "om_do_sys_reset", tip="Reset the switch; the web's reply goes out first")
action("factory_reset", "om_do_sys_factory_reset",
       tip="Write the default config to flash, then reset")
action("rnd", "om_do_sys_rnd", tip="Six random bytes from the chip, as an event line")
message("flash_busy", "the flash is busy")
