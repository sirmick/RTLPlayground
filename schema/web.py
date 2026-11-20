# web: the login (DESIGN.md 6.2). A new password logs every session out.
# The password is kept in flash as it is; get prints it as *.

obj("web", apply="om_apply_web", order=18, tip="The web interface's login")
cfg("password", Secret(20), "1234", tip="The login password")
cfg("session_timeout_s", U16(60, 65535), 3600, tip="A login ends after this long without a request", unit="s")
