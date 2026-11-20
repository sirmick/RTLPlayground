# A schema for rtlsim measurements only (om.mk OM_SIM=1): a keyed object
# with the owner's MAX_VLANS (64) and no handlers, so get, diff, set and
# apply over 64 entries can be timed on the firmware. Not in any release.

obj("svlan", keyed=U16(1, 4094), max=64)
cfg("name", Str(15), "")
cfg("members", PortMask(), 0)
cfg("tagged", PortMask(), 0)
default_entry(1, members=0x1ff)
