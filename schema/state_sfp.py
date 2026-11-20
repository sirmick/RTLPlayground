# state.sfp.S (DESIGN.md 7.7; S = the cage, 1-2): the module, from sfp.c's
# caches (its EEPROM as read at insertion, the diagnostics from the last
# poll: a get never talks I2C).
#
# Deviation (P5-4): the diagnostics are SFF-8472's raw words, not converted
# or calibrated: vcc and the powers in their exact decimal units (Scaled),
# temp and tx_bias raw with their unit; ddm.cal holds the 40 calibration
# bytes in hex when the module asks for external calibration. dBm, the
# calibration and the conversions are the UI's (and a host's).
# Deferred for room (P5-4): port (the cage's port: derived from
# state.sys.sfp_ports), oui.

state("sfp", count=2, get="om_get_sfp")
st("present", Bool)
st("los", Enum("sfplos", "ok los none"), tip="none: the cage has no LOS pin")
st("ready", Bool, tip="Read and configured")
st("id_ok", Bool)
st("ddm_ok", Bool)
st("vendor", Str(16))
st("part", Str(16))
st("rev", Str(4))
st("serial", Str(16))
st("encoding", U8())
st("rate_mbps", U16())
st("options", U8(), tip="After the quirks")
st("quirks", U8())
st("ddm.temp", U16(), unit="1/256 C", tip="Signed; uncalibrated with ddm.cal")
st("ddm.vcc_v", Scaled(4), unit="V", tip="Uncalibrated with ddm.cal")
st("ddm.tx_bias", U16(), unit="2 uA", tip="Uncalibrated with ddm.cal")
st("ddm.tx_power_mw", Scaled(4), unit="mW", tip="Uncalibrated with ddm.cal")
st("ddm.rx_power_mw", Scaled(4), unit="mW", tip="Uncalibrated with ddm.cal")
st("ddm.laser", U16(), tip="Laser temperature or wavelength: the module's own meaning")
st("ddm.status", Flags("sfpst", "not_ready rx_los tx_fault b3 b4 b5 b6 tx_disable"))
st("ddm.cal", Str(80), tip="External calibration, bytes 184-223 in hex; empty: internal")
