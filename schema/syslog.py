# syslog: the remote log (DESIGN.md 6.4). Apply stops it, takes the fields,
# then starts it if enabled.

obj("syslog", apply="om_apply_syslog", order=16, tip="Send the console's log lines to a syslog server")
cfg("enabled", Bool, False, tip="Send log lines")
cfg("server", IPv4, "0.0.0.0", tip="The syslog server")
cfg("port", U16(1, 65535), 514, tip="Its UDP port")
