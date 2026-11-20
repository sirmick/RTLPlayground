# DNS

The switch can resolve host names to IPv4 addresses, for the NTP client and
for anything else that is given a name instead of an address.

```
get dns                              # dns.server1, dns.server2
set dns.server1 192.168.10.53        # 0.0.0.0: none
set dns.server2 0.0.0.0
apply
dns lookup <name>                    # resolve a name (an old command, kept)
get state.dns                        # DHCP's server, the last lookup and its answer
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
A `set` changes the candidate; `apply` makes it live, and `save` keeps it
across a restart (doc/cli.md).
