#!/usr/bin/env python3
"""POST tool stand-in: post.py FILE appends one marker line to FILE, once."""

import sys

path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    text = f.read()
marker = "# post-processed\n"
if not text.endswith(marker):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text + marker)
