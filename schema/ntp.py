# ntp: the time client (DESIGN.md 6.4). Apply stops it, takes the fields,
# then starts it if enabled (a sync). The time and the client's state stay
# with `time` and `ntp` until P5's state.ntp.

obj("ntp", apply="om_apply_ntp", order=14, tip="Set the clock from an NTP server")
cfg("enabled", Bool, False, tip="Synchronise the clock", label="NTP")
cfg("server", Str(47), "pool.ntp.org", tip="Host name or address of the NTP server", label="NTP server")
cfg("interval_min", U16(1, 1440), 60, tip="Minutes between two synchronisations", unit="min", label="Poll interval")
cfg("timezone", TZ(), "+00:00", tip="The local time's offset from UTC", label="Time zone")
cfg("dst", Enum("dst", "off eu us"), "off", tip="Daylight saving time rules", label="Daylight saving")
action("sync", "om_do_ntp_sync", tip="Synchronise now")
message("ntp_off", "ntp.enabled is off")
