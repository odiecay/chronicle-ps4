#!/usr/bin/env python3
"""Stand-in for the MIPS assembler: fake_as.py [flags] -o OBJ SOURCE copies SOURCE to OBJ."""

import sys

args = sys.argv[1:]
obj = args[args.index("-o") + 1]
with open(args[-1], encoding="utf-8") as f:
    text = f.read()
with open(obj, "w", encoding="utf-8") as f:
    f.write("fake assembly\n" + text)
