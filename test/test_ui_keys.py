#!/usr/bin/env python3
"""test_ui_keys.py - every string key the web UI uses has an English text.

html/index.html's data-i18n* attributes and app.js's t("key") calls name keys
in app.js's language tables; a key with no English entry shows as the key
itself (the System tab showed i_ip, i_mask after 618073c dropped them). Keys
built at run time (t("nav_" + id)) are their prefix here and are skipped.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
JS = open(os.path.join(HERE, "..", "html", "app.js")).read()
HTML = open(os.path.join(HERE, "..", "html", "index.html")).read()
DYNAMIC = ("nav_", "stp_r", "stp_s")	# t(prefix + ...)


def table(lang):
    m = re.search(r"^\s*%s:\s*\{\n" % lang, JS, re.M)
    i, depth = m.end(), 1
    while depth:
        depth += (JS[i] == "{") - (JS[i] == "}")
        i += 1
    return set(re.findall(r"^\s*([a-z][a-z0-9_]*):", JS[m.end():i], re.M))


def main():
    en = table("en")
    used = set(re.findall(r'data-i18n(?:-[a-z]+)?="([a-z0-9_]+)"', HTML + JS)) | set(re.findall(r'\bt\("([a-z0-9_]+)"', JS))
    missing = sorted(k for k in used - en if k not in DYNAMIC)
    print("%d keys used, %d in English" % (len(used), len(en)))
    if missing:
        print("FAIL: no English text for " + ", ".join(missing))
        return 1
    print("TEST_UI_KEYS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
