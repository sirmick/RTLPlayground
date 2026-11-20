# Mirroring

> On feat/om mirroring keeps the old `mirror` command until P4-5, which moves it to
> `mirror.enabled`, `mirror.port`, `mirror.rx` and `mirror.tx` (doc/cli.md).

The RTL827x provides support to mirror packages from a number of ports to a mirroring
ports. For each mirrored port it is possible to define whether received, transmitted
or both types of packages are being mirrored.

## Mirroring control
Mirroring is enabled by setting RTL837x_MIRROR_CTRL(0x6048):

```
RTL837x_MIRROR_CTRL = port << 1 | 0x1
```
Ports are numbered 0-9, 9 being the CPU-port.
Mirroring is stopped by writing 0 to this register.

The mirrored ports are configured in RTL837x_MIRROR_CONF(0x604c):

```
RTL837x_MIRROR_CONF = RRRR TTTT
RRRR: 16 bit mask for ports where received packets are mirrored
TTTT: 16 bit mask for ports where transmitted packets are mirrored
```

## Configuration
Mirroring is the object model's `mirror` (schema/mirror.py); apply
(om_apply_mirror, om_port.c) writes MIRROR_CONF, then MIRROR_CTRL:
```
mirror.enabled on|off     mirroring on (default off)
mirror.port <port>|none   the mirroring port, where the copies go
mirror.rx <ports>         the ports whose received frames are copied
mirror.tx <ports>         the ports whose sent frames are copied
```
Ports are front-panel numbers (a list like 1-3,5). apply refuses mirror.enabled
on without a port, and a mirroring port that is also in rx or tx. On the
console, or by POST /cli from the web:
```
> set mirror.port 1
> set mirror.rx 2,3
> set mirror.tx 2,4
> set mirror.enabled on
> apply
> get mirror
```
