#!/usr/bin/env python3
"""
filter_langs.py - keep only selected UI languages, before minification.

The web UI ships six languages; most of app.js is that translation table. A
build that needs only some of them can drop the rest to shrink the image -
useful anywhere, essential on 512 KiB boards. This prunes the three places a
language appears: the LANG object in app.js, the langSel options in
index.html, and the inline L table in login.html. English is always kept (it
is the fallback). With no language list it copies the file unchanged, so the
default build is byte-for-byte as before.

Usage: filter_langs.py "<langs>" <src> <dst>
       langs is a space/comma list, e.g. "en" or "en,de"; empty = keep all.
"""
import sys, re


def match_delim(s, i):
    """s[i] is '{' or '['; return the index of the matching close, skipping
    over string literals so braces inside strings (e.g. "{name}") don't count."""
    open_ch = s[i]; close_ch = '}' if open_ch == '{' else ']'
    depth = 0; n = len(s)
    while i < n:
        c = s[i]
        if c in '"\'':
            i += 1
            while i < n and s[i] != c:
                i += 2 if s[i] == '\\' else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unbalanced " + open_ch)


def filter_object(s, open_marker, keep):
    """Find the JS object/opening at `open_marker` and keep only top-level
    keys in `keep`. Works for {..} values (LANG) and [..] values (login L)."""
    m = re.search(open_marker, s)
    if not m:
        return s
    brace = s.index('{', m.start())
    end = match_delim(s, brace)
    i = brace + 1
    kept = []
    while i < end:
        km = re.match(r'\s*(\w+)\s*:\s*', s[i:])
        if not km:
            i += 1
            continue
        key = km.group(1)
        keystart = i + km.start(1)            # where the key name begins
        vstart = i + km.end()
        vend = match_delim(s, vstart)         # value is { .. } or [ .. ]
        if key in keep:
            kept.append(s[keystart:vend + 1])
        i = vend + 1
    return s[:brace] + "{\n" + ",\n".join(kept) + "\n}" + s[end + 1:]


def main(argv):
    if len(argv) != 4:
        sys.exit(__doc__)
    langs = {x for x in re.split(r"[ ,]+", argv[1].strip()) if x}
    src, dst = argv[2], argv[3]
    data = open(src, "rb").read()
    try:
        s = data.decode("utf-8")
    except UnicodeDecodeError:
        open(dst, "wb").write(data)           # binary (e.g. .ico): copy
        return
    if langs:
        keep = langs | {"en"}                 # en is the fallback, always kept
        if "var LANG" in s:
            s = filter_object(s, r"var LANG\s*=\s*", keep)
        if re.search(r"var L\s*=\s*\{", s):   # login.html inline table
            s = filter_object(s, r"var L\s*=\s*", keep)
        if 'id="langSel"' in s:               # drop options for removed langs
            s = re.sub(r'\s*<option value="(\w+)">[^<]*</option>',
                       lambda o: o.group(0) if o.group(1) in keep else "", s)
    open(dst, "w").write(s)


if __name__ == "__main__":
    main(sys.argv)
