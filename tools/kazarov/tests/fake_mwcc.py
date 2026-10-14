#!/usr/bin/env python3
"""Stand-in for mwcc, used as the EXE of kz-mwcc in tests/test_kz_mwcc.py.

It is run as `RUNNER fake_mwcc.py ARGS...`, RUNNER being `sys.executable` in the tests.
It needs the source and each -i/-I directory's common.h to resolve from its cwd, and fails
otherwise. Controls, all from the environment:

  FAKE_MWCC_LOG     JSON record of the run (cwd, listing, arguments) is written here.
  FAKE_MWCC_EXIT    nonzero: exit with this code and write nothing.
  FAKE_MWCC_OUTPUT  ok (default), empty, or none (no -o file).
  FAKE_MWCC_DEP     ok (default; <source stem>.d), object (<object stem>.d), or none.

The map is written the way MWCC's is, with CRLF line ends: a relative path, a path under the
run's cwd (temporary directory), a Z:\\ path under the project root and a temporary target.
"""

import json
import os
import sys


def main(argv):
    args = argv[1:]
    obj = None
    includes = []
    i = 0
    while i < len(args) - 1:
        arg = args[i]
        if arg == "-o":
            obj = args[i + 1]
            i += 2
            continue
        if arg in ("-i", "-I"):
            includes.append(args[i + 1])
            i += 2
            continue
        if arg[:2] in ("-i", "-I") and len(arg) > 2:
            includes.append(arg[2:])
        i += 1
    source = args[-1]

    cwd = os.getcwd()
    code = int(os.environ.get("FAKE_MWCC_EXIT", "0"))
    with open(os.environ["FAKE_MWCC_LOG"], "w", encoding="utf-8") as f:
        json.dump({"cwd": cwd, "env_cwd": os.environ.get("KZ_MWCC_CWD"),
                   "listing": sorted(os.listdir(".")), "argv": args}, f)
    if code:
        return code

    if not os.path.isfile(source):
        print("fake_mwcc: cannot open %s" % source, file=sys.stderr)
        return 1
    header = None
    for directory in includes:
        candidate = os.path.join(directory, "common.h")
        if os.path.isfile(candidate):
            header = candidate
            break
    if header is None:
        print("fake_mwcc: common.h not found in %s" % includes, file=sys.stderr)
        return 1

    output = os.environ.get("FAKE_MWCC_OUTPUT", "ok")
    if output == "ok":
        with open(obj, "w", encoding="utf-8") as f:
            f.write("object for %s\n" % source)
    elif output == "empty":
        open(obj, "w").close()

    dep = os.environ.get("FAKE_MWCC_DEP", "ok")
    if dep != "none":
        stem = os.path.splitext(os.path.basename(obj if dep == "object" else source))[0]
        root = os.environ.get("KZ_PROJECT_ROOT", cwd)
        windows_root = "Z:" + root.replace("/", "\\")
        lines = [
            "%s: %s \\" % (os.path.basename(obj), source),
            "  %s \\" % header,
            "  %s/own.h \\" % cwd,
            "  tmpQ7x.o: \\",
            "  %s\\private\\win.h" % windows_root,
        ]
        with open(os.path.join(cwd, stem + ".d"), "w", encoding="utf-8", newline="") as f:
            f.write("\r\n".join(lines) + "\r\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
