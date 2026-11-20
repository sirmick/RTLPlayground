# state.sys: the build, the board, the chip (DESIGN.md 7.1), and
# state.sys.reset: the last reset, from the crash record (7.2).
#
# Deferred for room (P5-1), to return when the banks have it: state.sys.
# build_id, toolchain, chip_n, chip_ok, layout, temp_poweron_c, and
# state.sys.reset.count.

state("sys", get="om_get_sys")
st("version", Str(31))
st("build_date", Str(19))
st("machine", Str(30))
st("pcb", Str(39))
st("chip", Enum("chip", "rtl8372 rtl8373"))
st("ports", PortMask())
st("sfp_ports", PortMask())
st("ports_10g", PortMask())
st("image_size", U32(), tip="The size a firmware upload must have")
st("uptime_s", U32())
st("mac", Str(17), tip="aa:bb:cc:dd:ee:ff (a MAC once sys.mac brings the type)")
st("temp_c", Scaled(1), tip="0 below zero")
st("flash.size", U32())
st("poe", Enum("poe", "none no_image no_answer no_chip_image failed ok no_room"),
   tip="How loading the PoE controller's firmware went at boot")
st("button", Bool, tip="The reset button is held")

state("sys.reset", get="om_get_sys_reset")
st("cause", Enum("rcause", "power watchdog ctrl_r cmd update web button factory"))
st("slot", Str(11))
st("state", Str(11))
st("step_ms", U16())
st("pc", U16(), tip="0: not from an interrupt")
st("bank", U8())
st("states", Str(95), tip="The last 8 states run, oldest first")

# state.loader (DESIGN.md 7.12): the rest of it with P5-3
state("loader", get="om_get_loader")
st("present", Bool, tip="Updates are installed by the loader")
