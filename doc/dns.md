# DNS

The switch can resolve host names to IPv4 addresses, for the NTP client and
for anything else that is given a name instead of an address.

```
dns server <ip> [<ip>]   # one or two servers, 0.0.0.0 removes them
dns lookup <name>        # resolve a name and print the address
dns                      # show the servers
get state.dns            # DHCP's server, the last lookup and its answer
```

The answer to a lookup arrives after the command has returned, so it is
printed on the serial console and in the syslog. In the console of the web
interface, run `get state.dns.lookup` afterwards to see it
(`state.dns.lookup.status`: none, pending, done or failed).

Without a configured server the resolver uses the one handed out by DHCP.
Only address records are looked up; a query goes to the servers in turn,
every two seconds, and gives up after six tries. A name that already is a
dotted address is taken as it is, without a query.

The DNS servers are set in the Network card of the System page as well.
Settings made there or on the console are kept only once the configuration is
saved.
