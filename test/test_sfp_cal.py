#!/usr/bin/env python3
"""test_sfp_cal.py - the web UI's SFP diagnostics math (html/app.js: sfpDdm,
calSO, calRx, mwText) against SFF-8472 external calibration with known
constants. The functions are cut out of app.js and run in node; the expected
values are computed here from the spec's formulas. No node: skipped."""
import json, math, os, re, shutil, struct, subprocess, sys

APP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "html", "app.js")
FUNCS = ("pU16", "pI16", "calSO", "calRx", "mwText", "sfpDdm")
fails = 0


def check(cond, msg, detail=""):
    global fails
    print(("  ok:   " if cond else "  FAIL: ") + msg)
    if not cond:
        fails += 1
        print("        " + str(detail))


def func(src, name):
    """function name(...) {...} out of src, by its braces"""
    i = src.index("\nfunction %s(" % name) + 1
    j, depth = src.index("{", i), 0
    for k in range(j, len(src)):
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return src[i:k + 1]


# bytes 184-223: RX_PWR(4..0) as floats, then the slope (unsigned 8.8) and
# offset (signed) of TX_I, TX_PWR, T, V, then 4 reserved
RX = (0.0, 0.0, 0.0, 2.0, 5.0)			# rx = 2 * raw + 5 (0.1 uW)
SO = ((0x0200, 10), (0x0100, -10), (0x0100, 256), (0x0080, 0))
CAL = (struct.pack(">5f", *RX) + struct.pack(">" + "Hh" * 4, *[x for p in SO for x in p]) + bytes(4)).hex()
RAW = {"temp": 9088, "vcc": 33000, "bias": 3000, "tx": 6026, "rx": 3981}
DDM = {"temp": str(RAW["temp"]), "vcc_v": "%.4f" % (RAW["vcc"] / 10000), "tx_bias": str(RAW["bias"]),
       "tx_power_mw": "%.4f" % (RAW["tx"] / 10000), "rx_power_mw": "%.4f" % (RAW["rx"] / 10000)}


def so(raw, i):
    return SO[i][0] / 256 * raw + SO[i][1]


WANT_CAL = {"temp": so(RAW["temp"], 2) / 256, "vcc": so(RAW["vcc"], 3) / 10000, "bias": so(RAW["bias"], 0) / 500,
            "tx": so(RAW["tx"], 1) / 10000, "rx": sum(c * RAW["rx"] ** (4 - k) for k, c in enumerate(RX)) / 10000}
WANT_RAW = {"temp": RAW["temp"] / 256, "vcc": RAW["vcc"] / 10000, "bias": RAW["bias"] / 500,
            "tx": RAW["tx"] / 10000, "rx": RAW["rx"] / 10000}


def main():
    node = shutil.which("node")
    if not node:
        print("test_sfp_cal: no node, skipped")
        return 0
    src = open(APP).read()
    js = "\n".join(func(src, f) for f in FUNCS) + """
var cases = %s, out = {};
for (var k in cases) out[k] = sfpDdm(cases[k]);
out.mw = [mwText(0.7967), mwText(0), mwText(0.00005), mwText(0.0001)];
console.log(JSON.stringify(out));
""" % json.dumps({"cal": {"ddm": dict(DDM, cal=CAL)}, "raw": {"ddm": dict(DDM, cal="")},
                  "neg": {"ddm": dict(DDM, temp="65280", cal="")}})
    r = json.loads(subprocess.run([node, "-e", js], capture_output=True, text=True, check=True).stdout)
    print("sfpDdm: external calibration (SFF-8472 bytes 184-223)")
    check(all(abs(r["cal"][k] - v) < 1e-6 for k, v in WANT_CAL.items()),
          "slope/offset and the RX polynomial applied (%s)" % ", ".join("%s %.4f" % kv for kv in WANT_CAL.items()),
          r["cal"])
    check(all(abs(r["raw"][k] - v) < 1e-6 for k, v in WANT_RAW.items()), "no cal: the raw words in units", r["raw"])
    check(abs(r["neg"]["temp"] + 1.0) < 1e-9, "temperature 0xff00 is -1.0 C (an int16)", r["neg"]["temp"])
    print("mwText")
    check(r["mw"] == ["0.797 mW / %.2f dBm" % (10 * math.log10(0.7967)), "0.000 mW / - dBm", "0.000 mW / - dBm",
                      "0.000 mW / -40.00 dBm"], "dBm, and '-' for a dark port under 1e-4 mW", r["mw"])
    print("\n%s" % ("TEST_SFP_CAL PASSED" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
