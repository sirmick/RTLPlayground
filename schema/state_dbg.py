# state.dbg (DESIGN.md 9): raw words for debugging, built with OM_DBG=1
# (the Makefile's default; release images OM_DBG=0). The raw pokes
# (regget/regset, phyget/physet, sdsget/sdsset, flash, leds) stay console
# commands under the same flag: as objects they would need an immediate set,
# hex indices and a third index level, for debugging alone (P7).

state("dbg", get="om_get_dbg")
st("sds_modes", Hex32, tip="SDS_MODES (0x7b20): the SerDes modes")
st("gpio_0", Hex32, tip="GPIO 0-31 in (0x44)")
st("gpio_1", Hex32, tip="GPIO 32-63 in (0x48)")

state("dbg.igmp.port", count="ports", get="om_get_dbg_igmp_port")
st("cfg", Hex32, tip="IGMP_PORT_CFG (0x52a0 + 4 * port); on or off: igmp.enabled")
