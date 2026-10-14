#!/bin/sh
# Checks kazarov/port.h: MWCC must compile each macro use to the same code as the hand-written PS2 text,
# and a PORT build must take the values the macros give.
# Usage: run.sh MWCC_EXE, with MWCC_EXE relative to the working directory or absolute.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
kz=$(cd "$here/../.." && pwd)
mwcc_arg=${1:?usage: run.sh MWCC_EXE}
mwcc=$(cd "$(dirname "$mwcc_arg")" && pwd)/$(basename "$mwcc_arg")
wibo=${WIBO:-wibo}
objdump=${OBJDUMP:-mips-ps2-decompals-objdump}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/port_h.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

# compile LANG SOURCE OBJECT: MWCC -c -O2 on a test source, with kazarov/port.h on the include path.
compile() {
    "$wibo" "$mwcc" -c -O2 -lang "$1" -i "$kz/include" -o "$tmp/$3" "$here/$2"
}

# text OBJECT: the .text disassembly with the file-name lines dropped.
text() {
    "$objdump" -d -j .text "$1" | grep -v -e 'file format' -e '^[^ ]*\.o:' > "$1.text"
    [ -s "$1.text" ] || { echo "run.sh: no .text in $1" >&2; exit 1; }
    cat "$1.text"
}

status=0
for pair in "c uses.c plain.c" "c++ uses.cpp plain.cpp"; do
    # shellcheck disable=SC2086
    set -- $pair
    lang=$1 uses=$2 plain=$3
    compile "$lang" "$uses" "uses_$uses.o"
    compile "$lang" "$plain" "plain_$plain.o"
    if text "$tmp/uses_$uses.o" > "$tmp/uses_$uses.s" && text "$tmp/plain_$plain.o" > "$tmp/plain_$plain.s" \
        && cmp -s "$tmp/uses_$uses.s" "$tmp/plain_$plain.s"; then
        echo "PASS $uses matches $plain ($(grep -c '^ ' "$tmp/uses_$uses.s") instruction lines)"
    else
        echo "FAIL $uses differs from $plain:" >&2
        diff "$tmp/plain_$plain.s" "$tmp/uses_$uses.s" >&2 || true
        status=1
    fi
done
[ "$status" -eq 0 ] || exit "$status"

cxx=
for candidate in clang++ g++; do
    if command -v "$candidate" >/dev/null 2>&1; then
        cxx=$candidate
        break
    fi
done
[ -n "$cxx" ] || { echo "run.sh: neither clang++ nor g++ is on PATH" >&2; exit 1; }

"$cxx" -std=c++17 -Wall -Werror -DPORT -I "$kz/include" -o "$tmp/port_run" "$here/port_run.cpp"
echo "PASS port_run built with $cxx"
"$tmp/port_run"
