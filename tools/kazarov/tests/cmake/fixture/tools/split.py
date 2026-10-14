#!/usr/bin/env python3
"""Stand-in for the split: writes unit_c.s into DIR. FIXTURE_VARIANT changes its content."""

import os
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "unit_c.s"), "w", encoding="utf-8") as f:
    f.write("# variant %s\n    nop\n" % os.environ.get("FIXTURE_VARIANT", "1"))
