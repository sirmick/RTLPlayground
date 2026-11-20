# state.mgmt (DESIGN.md 7.4): the address in use, and DHCP's view. Its
# config (mgmt.*) is P4-1's; until then `ip`, `gw` and `netmask` set it.
#
# Deferred for room (P5-3): dhcp.router, .subnet, .broadcast (while
# leasing, the same as gateway and netmask, and derived) and dhcp.lease_s,
# .rebind_s, .renewal_s (renew_s counts down the renewal). dhcp.dns is
# state.dns.dhcp_server (one path a value).

state("mgmt", get="om_get_mgmt")
st("ip", IPv4)
st("netmask", IPv4)
st("gateway", IPv4)
st("dhcp.state", Enum("dhcpst", "off start discover request leasing"))
st("dhcp.renew_s", U16(), tip="Seconds to the next DHCP message")
st("dhcp.server", IPv4)
