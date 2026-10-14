#!/usr/bin/env python3
"""Stand-in for the compiler in tests/cmake: writes -o, and with -MD a <stem>.d in the cwd.

    fake_cc.py [flags] -o OBJ SOURCE

The object holds the source text. The depfile lists the source and every #include "..."
reached from it, searched beside the including file and then in the -i/-I directories,
in the same way MWCC's map lists them.
"""

import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def reached(path, dirs, seen):
    """Append path and the files it includes, depth first, once each."""
    if path in seen:
        return
    seen.append(path)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    for name in INCLUDE.findall(text):
        for base in [os.path.dirname(path)] + dirs:
            found = os.path.join(base, name)
            if os.path.isfile(found):
                reached(found, dirs, seen)
                break
        else:
            sys.exit("fake_cc: cannot find %s" % name)


def main(argv):
    out = source = None
    dirs = []
    want_map = False
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "-o":
            out = argv[i + 1]
            i += 2
            continue
        if arg in ("-i", "-I"):
            dirs.append(argv[i + 1])
            i += 2
            continue
        if arg == "-MD":
            want_map = True
        elif not arg.startswith("-"):
            source = arg
        i += 1
    if out is None or source is None:
        sys.exit("fake_cc: needs -o OBJ and a SOURCE")

    deps = []
    reached(source, dirs, deps)
    with open(source, encoding="utf-8") as f:
        text = f.read()
    with open(out, "w", encoding="utf-8") as f:
        f.write("fake object for %s\n%s" % (source, text))

    if want_map:
        stem = os.path.splitext(os.path.basename(source))[0]
        with open(stem + ".d", "w", encoding="utf-8") as f:
            f.write("%s.o: %s\n" % (stem, " ".join(deps)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
