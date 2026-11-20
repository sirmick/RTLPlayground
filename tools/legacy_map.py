"""legacy_map.py - old command text (a deployed switch's saved config) as
object-model paths (DESIGN.md 15.2), for rtlpg_client.py convert.

Each row is a regex an old line must match in full, and what it sets:
{path: value}, DEL for a keyed entry removed. The rows are the old
cmd_parser.c forms (CONF_CMDS and the rest a config file can hold:
mac, sfp, port N duplex), after ovs-rtlpg's tests/oldnew/translate.py,
which checks each against the old firmware. The paths are DESIGN.md
section 6's. Given the schema the new firmware was built from, a line whose
paths it has becomes path lines, checked as set takes them; a line with a
path it lacks (an object not migrated yet) stays legacy, the old text as it
was, with the plan node that migrates it (OWNER). A line no row takes is
reported, never dropped; one a row changes to fit says why (WARN).
"""
import re
import sys

DEL = object()
WARN = object()
IP = r"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"
INGRESS = {"u": "untagged", "t": "tagged", "a": "all"}
STP_G = {"prio": ("stp.priority", 0, 15), "hello": ("stp.hello_s", 1, 10), "maxage": ("stp.maxage_s", 6, 40),
         "fwd": ("stp.fwd_delay_s", 4, 30), "txhold": ("stp.txhold", 1, 10)}
STORM_T = r"(bcast|mcast|ucast|umcast)"
ROWS = []


def row(rx, fn):
    ROWS.append((re.compile(rx), fn))


def plist(ports, cpu=False):
    w = [str(p) for p in sorted(set(ports))] + (["cpu"] if cpu else [])
    return ",".join(w) or "none"


def name_char(c):
    """What the old firmware stored of a name's character."""
    return "." if ord(c) < 0x20 or ord(c) > 0x7e or c in '"\\' else c


def _ip(s):
    a = re.fullmatch(r"(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})", s or "")
    return s if a and all(int(x) <= 255 for x in a.groups()) else None


def _mac(s):
    """Six hex pairs, ':' or '-'; unicast, the first three bytes not all zero."""
    a = re.fullmatch(r"([0-9a-fA-F]{2})[:-]" * 5 + r"([0-9a-fA-F]{2})", s)
    if not a:
        return None
    b = [int(x, 16) for x in a.groups()]
    return None if b[0] & 1 or not (b[0] | b[1] | b[2]) else ":".join("%02x" % x for x in b)


def _tz(s):
    """[+-]h[h][:mm], mm 00/30/45, -12:00..+14:00, as +hh:mm."""
    a = re.fullmatch(r"([+-]?)(\d{1,2})(?::(00|30|45))?", s)
    if not a:
        return None
    v = int(a[2]) * 60 + int(a[3] or 0)
    neg = a[1] == "-"
    if v > (12 * 60 if neg else 14 * 60):
        return None
    return "%s%02d:%02d" % ("-" if neg and v else "+", v // 60, v % 60)


def _n(s, lo, hi):
    return str(int(s)) if lo <= int(s) <= hi else None


# --- port.N

row(r"pvid (\d+) (\d+)", lambda m, b, p: {f"port.{m[1]}.pvid": m[2]})
row(r"mtu (\d+) (\d+)", lambda m, b, p: {f"port.{m[1]}.mtu": m[2]})
row(r"port (\d+) name (\S.*)", lambda m, b, p: {f"port.{m[1]}.name": "".join(map(name_char, m[2]))[:31]})
row(r"port (\d+) lldp (block|permit)", lambda m, b, p: {f"port.{m[1]}.lldp": m[2]})
def _sfp_port(b, n):
    lp = b.log_of(int(n))
    return lp is not None and b.sfp[lp]


def _port_on(m, b, p):
    """port N on: an SFP port has no speed or duplex field (copper only)."""
    out = {f"port.{m[1]}.enabled": "on"}
    if not _sfp_port(b, m[1]):
        out.update({f"port.{m[1]}.speed": "auto", f"port.{m[1]}.duplex": "auto"})
    return out


def _port_speed(m, b, p):
    out = {f"port.{m[1]}.enabled": "on", f"port.{m[1]}.speed": m[2]}
    if not _sfp_port(b, m[1]):
        out[f"port.{m[1]}.duplex"] = m[3] if m[2] in ("10m", "100m") and m[3] in ("half", "full") else "auto"
    return out


row(r"port (\d+) on", _port_on)
row(r"port (\d+) off", lambda m, b, p: {f"port.{m[1]}.enabled": "off"})
row(r"port (\d+) (auto|10m|100m|1g|2g5|5g|10g)(?: (\S+).*)?", _port_speed)
row(r"port (\d+) duplex(?: (\S+).*)?", lambda m, b, p: {f"port.{m[1]}.duplex": "full" if m[2] == "full" else "half"})
row(r"isolate (\d+) off", lambda m, b, p: {f"port.{m[1]}.isolate": plist(b.fronts(), True)})
row(r"isolate (\d+)((?: \d+)+)", lambda m, b, p: {f"port.{m[1]}.isolate": plist(
    [int(x) for x in m[2].split() if x != "0"], "0" in m[2].split())})
row(r"ingress ([uta])", lambda m, b, p: {f"port.{n}.ingress": INGRESS[m[1]] for n in b.fronts()})
row(r"ingress((?: \d+[uta])+)", lambda m, b, p: {f"port.{w[:-1]}.ingress": INGRESS[w[-1]] for w in m[1].split()})
row(r"eee (on|off)", lambda m, b, p: {f"port.{n}.eee": m[1] for n in b.fronts() if not b.sfp[b.log_of(n)]})
row(r"eee (on|off) (\d+)", lambda m, b, p: {f"port.{m[2]}.eee": m[1]})


def _bw_rate(s):
    """bw's hex kbit/s: the register keeps 16 kbit steps."""
    return str(int(s, 16) & ~15)


row(r"bw in (\d) ([0-9a-fA-F]{1,8})", lambda m, b, p: {f"port.{m[1]}.bw_in_kbps": _bw_rate(m[2]),
                                                        f"port.{m[1]}.bw_in_mode": "fc"})
row(r"bw in (\d) off", lambda m, b, p: {f"port.{m[1]}.bw_in_kbps": "0"})
row(r"bw in (\d) (fc|drop)", lambda m, b, p: {f"port.{m[1]}.bw_in_mode": m[2]})
row(r"bw out (\d) ([0-9a-fA-F]{1,8})", lambda m, b, p: {f"port.{m[1]}.bw_out_kbps": _bw_rate(m[2])})
row(r"bw out (\d) off", lambda m, b, p: {f"port.{m[1]}.bw_out_kbps": "0"})
row(r"storm (\d) " + STORM_T + r" (\d+) (pps|kbps)",
    lambda m, b, p: {f"port.{m[1]}.storm.{m[2]}.rate": str(int(m[3])), f"port.{m[1]}.storm.{m[2]}.unit": m[4]}
    if 1 <= int(m[3]) <= (0xfffff if m[4] == "pps" else 10000000) else None)
row(r"storm (\d) " + STORM_T + r" off", lambda m, b, p: {f"port.{m[1]}.storm.{m[2]}.rate": "0"})


def _sfp_speed(m, b, p):
    """sfp S <speed>: the cage's port (DESIGN 20.2: port.N.speed)."""
    s = int(m[1])
    ports = [n for n in b.fronts() if b.sfp[b.log_of(n)] == s]
    return {f"port.{ports[0]}.speed": m[2]} if ports else None


row(r"sfp ([12]) (10g|2g5|1g|100m|auto)", _sfp_speed)

# --- sys, web

row(r"hostname (\S+)", lambda m, b, p: {"sys.hostname": "".join(map(name_char, m[1]))[:23]})
row(r"mac (\S+)", lambda m, b, p: {"sys.mac": _mac(m[1])} if _mac(m[1]) else None)
row(r"led on", lambda m, b, p: {"sys.led": "on"})
row(r"led off", lambda m, b, p: {"sys.led": "off"})
row(r"led off ports", lambda m, b, p: {"sys.led": "ports_off"})
# web.password is a Secret: no space, quote or backslash (om.c str_ch)
row(r"passwd (.+)", lambda m, b, p: {"web.password": m[1][:20]}
    if all(" " < c <= "~" and c not in '"\\' for c in m[1][:20]) else None)


def _session(m, b, p):
    """session N: the old 1-65535; web.session_timeout_s starts at 60."""
    v = int(m[1])
    if not 1 <= v <= 65535:
        return None
    if v < 60:
        return {"web.session_timeout_s": "60", WARN: "web.session_timeout_s %d raised to 60" % v}
    return {"web.session_timeout_s": str(v)}


row(r"session (\d+)", _session)

# --- mgmt, dns, ntp, syslog

row(r"ip " + IP, lambda m, b, p: {"mgmt.mode": "static", "mgmt.ip": m[1]} if _ip(m[1]) else None)
row(r"ip dhcp", lambda m, b, p: {"mgmt.mode": "dhcp"})
row(r"gw " + IP, lambda m, b, p: {"mgmt.gateway": m[1]} if _ip(m[1]) else None)
row(r"netmask " + IP, lambda m, b, p: {"mgmt.netmask": m[1]} if _ip(m[1]) else None)
row(r"vlan (\d+) mgmt", lambda m, b, p: {"mgmt.vlan": str(int(m[1]))} if int(m[1]) <= 4094 else None)
row(r"dns server " + IP, lambda m, b, p: {"dns.server1": m[1], "dns.server2": "0.0.0.0"} if _ip(m[1]) else None)
row(r"dns server " + IP + " " + IP, lambda m, b, p: {"dns.server1": m[1], "dns.server2": m[2]}
    if _ip(m[1]) and _ip(m[2]) else None)
row(r"ntp on", lambda m, b, p: {"ntp.enabled": "on"})
row(r"ntp off", lambda m, b, p: {"ntp.enabled": "off"})
row(r"ntp server (\S{1,47})", lambda m, b, p: {"ntp.server": m[1]})
row(r"ntp interval (\d+)", lambda m, b, p: {"ntp.interval_min": _n(m[1], 1, 1440)} if _n(m[1], 1, 1440) else None)
row(r"ntp timezone (\S+)", lambda m, b, p: {"ntp.timezone": _tz(m[1])} if _tz(m[1]) else None)
row(r"ntp dst (off|eu|us)", lambda m, b, p: {"ntp.dst": m[1]})
row(r"syslog (on|off)", lambda m, b, p: {"syslog.enabled": m[1]})
row(r"syslog ip " + IP, lambda m, b, p: {"syslog.server": m[1]} if _ip(m[1]) else None)
row(r"syslog port (\d+)", lambda m, b, p: {"syslog.port": _n(m[1], 1, 65535)} if _n(m[1], 1, 65535) else None)

# --- vlan.V


def _vlan(m, b, p):
    """vlan V [name] [port[t]]...: the membership replaced, the name if one
    is given (a word starting with a letter)."""
    v, w = int(m[1]), m[2].split() if m[2] else []
    if not 1 <= v <= 4094:
        return None
    out, mem, tag = {}, [], []
    if w and w[0][0].isalpha():
        out[f"vlan.{v}.name"] = "".join(map(name_char, w[0]))
        w = w[1:]
    for x in w:
        a = re.fullmatch(r"(\d)(t?)", x)
        if not a:
            return None
        mem.append(int(a[1]))
        if a[2]:
            tag.append(int(a[1]))
    out[f"vlan.{v}.members"], out[f"vlan.{v}.tagged"] = plist(mem), plist(tag)
    return out


row(r"vlan (\d+) d\S*", lambda m, b, p: {f"vlan.{int(m[1])}": DEL} if 1 <= int(m[1]) <= 4094 else None)
row(r"vlan (\d+)((?: \S+)*)", _vlan)

# --- lag.L


def _ports_of(p, path):
    v = p.get(path, "none")
    return [int(x) for x in v.split(",") if x.isdigit()]


def _lacp(m, b, p):
    """lag L lacp P...: a port joining leaves any other LACP LAG (one left
    empty goes off)."""
    L, ports = int(m[1]), [int(x) for x in m[2].split()]
    out = {f"lag.{L}.mode": "lacp", f"lag.{L}.members": plist(ports)}
    for k in range(1, 5):
        if k == L or p.get(f"lag.{k}.mode") != "lacp":
            continue
        rest = _ports_of(p, f"lag.{k}.members")
        keep = [x for x in rest if x not in ports]
        if keep != rest:
            out[f"lag.{k}.members"] = plist(keep)
            if not keep:
                out[f"lag.{k}.mode"] = "off"
    return out


row(r"lag ([1-4]) d", lambda m, b, p: {f"lag.{m[1]}.mode": "off", f"lag.{m[1]}.members": "none"})
row(r"lag ([1-4]) lacp off", lambda m, b, p: {f"lag.{m[1]}.mode": "off", f"lag.{m[1]}.members": "none"}
    if p.get(f"lag.{m[1]}.mode") == "lacp" else {})
row(r"lag ([1-4]) lacp((?: \d)+)", _lacp)
row(r"lag ([1-4])((?: \d)+)", lambda m, b, p: {f"lag.{m[1]}.mode": "static",
                                               f"lag.{m[1]}.members": plist(int(x) for x in m[2].split())})
row(r"lacp off", lambda m, b, p: {q: x for k in range(1, 5) if p.get(f"lag.{k}.mode") == "lacp"
                                  for q, x in ((f"lag.{k}.mode", "off"), (f"lag.{k}.members", "none"))})
row(r"laghash ([1-4])((?: (?:spa|smac|dmac|sip|dip|sport|dport))+)",
    lambda m, b, p: {f"lag.{m[1]}.hash": ",".join(sorted(set(m[2].split())))})

# --- stp, stp.port.N, stp.lag.L

STP_E = r"stp (port|lag) (\d)"
row(r"stp (on|off)", lambda m, b, p: {"stp.enabled": m[1]})
row(r"stp (prio|hello|maxage|fwd|txhold) (\d+)", lambda m, b, p: {STP_G[m[1]][0]: str(int(m[2]))}
    if STP_G[m[1]][1] <= int(m[2]) <= STP_G[m[1]][2] else None)
row(r"stp version (rstp|stp)", lambda m, b, p: {"stp.version": m[1]})
row(r"stp pathcost (long|short)", lambda m, b, p: {"stp.pathcost": m[1]})
row(r"stp bpdu (filter|flood)", lambda m, b, p: {"stp.bpdu": m[1]})
row(STP_E + r" (on|off)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.enabled": m[3]})
row(STP_E + r" edge (on|off|auto)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.edge": m[3]})
row(STP_E + r" cost (\d+)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.cost": str(int(m[3]))} if int(m[3]) <= 200000000 else None)
row(STP_E + r" p2p (auto|on|off)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.p2p": m[3]})
row(STP_E + r" prio (\d+)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.priority": str(int(m[3]))}
    if int(m[3]) <= 240 and not int(m[3]) & 15 else None)
row(STP_E + r" guard (none|bpdu|root)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.guard": m[3]})
row(STP_E + r" filter (on|off)", lambda m, b, p: {f"stp.{m[1]}.{m[2]}.filter": m[3]})

# --- igmp, lldp, loop, mirror

row(r"igmp (on|off)", lambda m, b, p: {"igmp.enabled": m[1]})
row(r"lldp on", lambda m, b, p: {"lldp.enabled": "on"})
row(r"lldp off", lambda m, b, p: {"lldp.enabled": "off"})
row(r"loop (on|off)", lambda m, b, p: {"loop.enabled": m[1]})


def _mirror(m, b, p):
    """mirror <monitor> <p>[r|t]...: no suffix both ways, r received, t sent."""
    rx, tx = [], []
    for w in m[2].split():
        a = re.fullmatch(r"(\d)(\D?)", w)
        if not a:
            return None
        if a[2] != "t":
            rx.append(int(a[1]))
        if a[2] != "r":
            tx.append(int(a[1]))
    return {"mirror.enabled": "on", "mirror.port": m[1], "mirror.rx": plist(rx), "mirror.tx": plist(tx)}


row(r"mirror off", lambda m, b, p: {"mirror.enabled": "off"})
row(r"mirror (\d)((?: \S+)*)", _mirror)


# the plan node that moves a path's object to the object model (plan.toml)
OWNER = [(r"vlan\.", "P4-2"), (r"lag\.", "P4-3"), (r"stp\.", "P4-4"),
         (r"(igmp|lldp|loop|mirror)\.", "P4-5"), (r"port\.\d+\.(bw_|storm\.)", "P4-6"),
         (r"sys\.mac$", "sys.mac (deferred in P4-1)")]


def owner(path):
    return next((n for rx, n in OWNER if re.match(rx, path)), "?")


def text_of(data):
    """The sector's text: up to the first NUL or erased byte, as the old
    firmware read it."""
    end = len(data)
    for i, c in enumerate(data):
        if c in (0, 0xff):
            end = i
            break
    return data[:end].decode("latin-1")


def undump(data):
    """A console flash dump ("07f000  69 70 20 ...  |ip ..."), as its bytes;
    anything else as it is."""
    rows = re.findall(rb"(?m)^[0-9a-fA-F]{6}\s+((?:[0-9a-fA-F]{2}\s+){1,16})", data)
    if not rows:
        return data
    return bytes(int(x, 16) for r in rows for x in r.split())


def convert(data, board, warn=None, sch=None):
    """Old command text (bytes, or a flash dump of it) -> ("path value" /
    "del path" lines, legacy, the lines no row took). A later line overrides
    an earlier one, as at boot. Lines changed to fit go to warn, a list.
    With sch (rtlpg_client.Schema), legacy is the old lines with a path sch
    lacks, as (line, owner node), in order, their paths left out; a line
    whose value sch refuses is not taken (the reason to warn). Without it,
    every line is converted and legacy is empty."""
    data = undump(data)
    vals, seen, bad, legacy = {}, {}, [], []	# seen: every row's, what a row reads
    cfg = None
    if sch is not None:		# rtlpg_client's Config, from sch's module (__main__ too)
        cfg = sys.modules[type(sch).__module__].Config(sch)
    for ln in re.split(r"\r?\n|\r", text_of(data)):
        ln = ln.strip()
        if not ln:
            continue
        out = None
        for rx, fn in ROWS:
            m = rx.fullmatch(ln)
            if m:
                out = fn(m, board, seen)
                break
        if out is None:
            bad.append(ln)
            continue
        if WARN in out:
            if warn is not None:
                warn.append("%s: %s" % (ln, out[WARN]))
            del out[WARN]
        if cfg is not None:
            why, path = _check(cfg, out)
            if why in ("legacy", "port"):
                legacy.append((ln, owner(path) if why == "legacy" else "P4-6"))
                _take(seen, out)
                continue
            if why:
                bad.append(ln)
                if warn is not None:
                    warn.append("%s: %s" % (ln, why))
                continue
        _take(seen, out)
        _take(vals, out)
    lines = ["del %s" % p if v is DEL else "%s %s" % (p, v) for p, v in vals.items()]
    return lines, legacy, bad


def _take(vals, out):
    for path, v in out.items():
        if v is DEL:
            for q in [q for q in vals if q.startswith(path + ".")]:
                del vals[q]
        vals.pop(path, None)
        vals[path] = v


def _bad(sch):
    return sys.modules[type(sch).__module__].Bad


def _known(sch, path):
    """sch has the path's field (or keyed entry): its object is migrated."""
    try:
        sch.resolve(path)
    except _bad(sch) as e:
        return not str(e).endswith("no such path")
    return True


def _check(cfg, out):
    """(None, None) if cfg takes every path of a row's result (it keeps
    them); ("legacy", path) for the first path the schema lacks, ("port",
    path) for one it has but not on this port (an SFP's, P4-6); else (why
    it is refused, path)."""
    for path in out:
        if not _known(cfg.s, path):
            return "legacy", path
    for path, v in out.items():
        try:
            cfg.line("del %s" % path if v is DEL else "%s %s" % (path, v))
        except _bad(cfg.s) as e:
            return ("port" if str(e).endswith("not on this port") else str(e)), path
    return None, None
