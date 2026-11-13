"""
layout.py - the flash layout (layout.h, loader/layout.h) as a dict of numbers.

The layout is defined once, in C headers. Tools read it here, through the host
C preprocessor, instead of keeping their own copy. LAYOUT_DEFS (from the
Makefile, e.g. -DLAYOUT_COMPACT) selects the profile.
"""
import os, re, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(defs=None):
    defs = os.environ.get("LAYOUT_DEFS", "") if defs is None else defs
    cc = os.environ.get("HOSTCC", "gcc")
    out = subprocess.run([cc, "-E", "-dM", "-x", "c", *defs.split(),
                          "-include", "loader/layout.h", "/dev/null"],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout
    raw = dict(re.findall(r"^#define (\w+) (.+)$", out, re.M))

    def value(name):
        expr = raw[name].replace("(uint32_t)", "")
        expr = re.sub(r"\b(0x[0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", expr)
        expr = re.sub(r"\b[A-Z_][A-Z0-9_]*\b",
                      lambda m: str(value(m.group(0))) if m.group(0) in raw else m.group(0), expr)
        return eval(expr, {})

    ours = ("LDR_", "IMAGE_", "UPLOAD_", "CONFIG_", "HTML_", "DEFAULT_CONFIG_",
            "FIRMWARE_UPLOAD_", "CODE", "MDATA_", "LEGACY_")
    return {k: value(k) for k in raw if k.startswith(ours)}


if __name__ == "__main__":
    for k, v in sorted(read().items()):
        print(f"{k:24} 0x{v:x}")
