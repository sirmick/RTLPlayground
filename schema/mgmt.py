# mgmt: the management interface (DESIGN.md 6.3). The defaults are the
# firmware's own (rtlplayground.c); the factory config (config.txt) sets the
# documented 192.168.10.247. In dhcp mode the address fields are kept, unused.

obj("mgmt", apply="om_apply_mgmt", check="om_check_mgmt", order=10, tip="The switch's own IPv4 interface")
cfg("mode", Enum("mgmtmode", "static dhcp"), "static", tip="A fixed address, or one from DHCP")
cfg("ip", IPv4, "192.168.2.2", tip="The address, in static mode")
cfg("netmask", Mask4, "255.255.255.0", tip="The subnet mask, in static mode")
cfg("gateway", IPv4, "192.168.2.22", tip="The default gateway, in static mode")
# mgmt.vlan: the VLAN exists (the CPU is in every VLAN: vlan.py)
cfg("vlan", U16(0, 4094), 1, tip="The VLAN the switch answers on; 0: any")
action("dhcp_renew", "om_do_mgmt_dhcp_renew", tip="Ask the DHCP server again")
message("mgmt_static", "mgmt.mode is static")
message("mgmt_vlan", "vlan: no such vlan")
