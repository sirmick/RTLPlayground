# mirror: port mirroring (DESIGN.md 6.9). Each source port's received (rx)
# and sent (tx) frames are copied to the mirror port. Check: on needs a
# port, and the mirror port isn't a source.

obj("mirror", apply="om_apply_mirror", check="om_check_mirror", order=66, tip="Port mirroring")
cfg("enabled", Bool, False, tip="Copy the sources' frames to the mirror port")
cfg("port", Port(none=True), "none", tip="The mirror port, where the copies go")
cfg("rx", PortMask(), 0, tip="The ports whose received frames are copied")
cfg("tx", PortMask(), 0, tip="The ports whose sent frames are copied")
message("mirror_port", "mirror.enabled needs a port")
message("mirror_source", "the mirror port can't be a source")
