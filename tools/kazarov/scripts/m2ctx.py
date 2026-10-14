#!/usr/bin/env python3
"""Write the declaration context m2c decompiles against.

Run from the project root; relative paths are relative to the working directory.

    m2ctx.py -o OUT.c                       whole project: OUT.cpp (headers as one C++ file) and OUT.c
    m2ctx.py -o OUT.c path/to/unit.cpp      one translation unit instead of the project
    m2ctx.py -o OUT.c -I DIR -D NAME ...    extra include directories and macro definitions

-I may repeat; the default is ps2/include ps2/include/std ps2/include/sce. The first -I directory
is the header root that whole-project mode amalgamates. -D takes NAME or NAME=VALUE (or a function-like
macro such as 'SCRIPT(...)={}'). OUT.cpp is what decomp.me and mwcc with -lang c++ compile; OUT.c is
the same declarations as C, which m2c parses.

Whole-project mode also works around two things in the headers that stop OUT.cpp compiling: a
STATIC_ASSERT that pastes __COUNTER__ instead of expanding it, and records defined twice.

The conversion keeps what C can express (record layouts, enums, typedefs, free functions) and drops
member functions, access specifiers and templates. Emission order matters: enums first, then forward
typedefs, typedefs, records, and finally functions and variables.
"""

import argparse
import os
import re
import sys

import clang.cindex as ci

DEFAULT_INCLUDES = ["ps2/include", "ps2/include/std", "ps2/include/sce"]

# -ffreestanding keeps the host's system headers out of a context describing a PS2 binary.
# M2CTX is defined for headers that need to say something different here.
BASE_FLAGS = [
    "-D_LANGUAGE_C",
    "-DF3DEX_GBI_2",
    "-D_MIPS_SZLONG=32",
    "-DSCRIPT(...)={}",
    "-D__attribute__(...)=",
    "-D__asm__(...)=",
    "-ffreestanding",
    "-DM2CTX",
]

K = ci.CursorKind
T = ci.TypeKind

REF = (T.LVALUEREFERENCE, T.RVALUEREFERENCE)
ARRAY = (T.CONSTANTARRAY, T.INCOMPLETEARRAY, T.VARIABLEARRAY)
FUNC = (T.FUNCTIONPROTO, T.FUNCTIONNOPROTO)

# Both spellings: a library is reached as <cstring> or <libvu0.h>.
INCLUDE = re.compile(r'^\s*#\s*include\s+(?:"([^"]+)"|<([^>]+)>)')
# Searched in this order; std comes first because it carries size_t.
LIB_SUBDIRS = ("std", "sce")
STATIC_ASSERT_DEF = re.compile(r'^\s*#\s*define\s+STATIC_ASSERT\b.*$', re.MULTILINE)
REDEFINED = re.compile(r"redefinition of '([^']+)'")


#
# Amalgamation: the headers as one C++ file
#

def amalgamate(headers_dir):
    """Every header in headers_dir, in one file, each inlined once."""
    seen = set()
    out = []

    def expand(path):
        real = os.path.realpath(path)
        if real in seen:
            return
        seen.add(real)

        with open(path, encoding="utf-8") as f:
            for line in f:
                m = INCLUDE.match(line)
                if m:
                    name = m.group(1) or m.group(2)
                    nested = os.path.join(headers_dir, name)
                    for subdir in LIB_SUBDIRS:
                        if os.path.exists(nested):
                            break
                        nested = os.path.join(headers_dir, subdir, name)
                    if os.path.exists(nested):
                        expand(nested)
                    else:
                        out.append("/* unresolved: %s */\n" % line.strip())
                    continue
                if line.strip() == "#pragma once":
                    continue
                out.append(line)

        # Keeps the next header from being glued onto a last line that has no newline.
        if out and not out[-1].endswith("\n"):
            out.append("\n")

    # Libraries first: they have no extension, so the header filter below would skip them.
    for subdir in LIB_SUBDIRS:
        lib_dir = os.path.join(headers_dir, subdir)
        if not os.path.isdir(lib_dir):
            continue
        for name in sorted(os.listdir(lib_dir)):
            expand(os.path.join(lib_dir, name))

    for name in sorted(os.listdir(headers_dir)):
        if name.endswith((".hpp", ".h")):
            expand(os.path.join(headers_dir, name))

    return STATIC_ASSERT_DEF.sub("#define STATIC_ASSERT(expr) /* see m2ctx.py */",
                                 "".join(out))


def redefined_names(text, flags, filename="ctx.cpp"):
    """Records defined more than once, taken from clang's diagnostics (its AST keeps only the first)."""
    tu = ci.Index.create().parse(
        filename, args=list(flags) + ["-x", "c++"],
        unsaved_files=[(filename, text)])

    names = []
    for d in tu.diagnostics:
        m = REDEFINED.search(d.spelling)
        if m and m.group(1) not in names:
            names.append(m.group(1))
    return names


def definition_spans(text, name):
    """(start, end) of each `class/struct/union <name> { ... };` in text, skipping comments and strings."""
    opener = re.compile(r'\b(?:class|struct|union)\s+%s\b\s*(?::[^{;]*)?\{'
                        % re.escape(name))
    spans = []

    for m in opener.finditer(text):
        i = m.end()            # just past the opening brace
        depth = 1
        while i < len(text) and depth:
            c = text[i]
            if c == '/' and text[i:i + 2] == '//':
                i = text.find('\n', i)
                if i < 0:
                    break
                continue
            if c == '/' and text[i:i + 2] == '/*':
                i = text.find('*/', i)
                if i < 0:
                    break
                i += 2
                continue
            if c in '"\'':
                quote, i = c, i + 1
                while i < len(text) and text[i] != quote:
                    i += 2 if text[i] == '\\' else 1
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
            i += 1
        while i < len(text) and text[i] in ' \t;':
            i += 1
        spans.append((m.start(), i))

    return spans


def duplicate_extents(text, flags):
    """Byte ranges of every record definition after the first of its name."""
    drop = []
    for name in redefined_names(text, flags):
        drop += definition_spans(text, name)[1:]
    return sorted(drop)


def elide(text, extents):
    """Remove those ranges, latest first so earlier offsets stay valid."""
    for start, end in reversed(extents):
        text = (text[:start]
                + "/* definition elided: redefined elsewhere */"
                + text[end:])
    return text


def build_cpp(headers_dir, flags):
    text = amalgamate(headers_dir)
    return elide(text, duplicate_extents(text, flags))


#
# Conversion: that C++ as C declarations
#

def c_type_name(name):
    """Flattens nested C++ template arguments into a C identifier."""
    name = re.sub(r'\b\w+::', '', name)
    while "<" in name:
        flattened = re.sub(r'<\s*([^<>]*?)\s*>',
                           lambda m: '_' + re.sub(r'\W+', '_', m.group(1)) + '_',
                           name)
        if flattened == name:
            break
        name = flattened
    return name


def declarator(t, name=""):
    """A C declaration of name with type t, built inside-out as C's declarator syntax nests."""
    k = t.kind

    if k == T.ELABORATED:
        return declarator(t.get_named_type(), name)

    if k == T.POINTER:
        return declarator(t.get_pointee(), "*" + name)

    if k in REF:
        # A reference is a pointer in the ABI, and C has no other spelling for one.
        return declarator(t.get_pointee(), "*" + name)

    if k in ARRAY:
        if name.startswith("*"):
            name = "(" + name + ")"
        n = t.element_count if k == T.CONSTANTARRAY else ""
        return declarator(t.element_type, "%s[%s]" % (name, n))

    if k in FUNC:
        if name.startswith("*"):
            name = "(" + name + ")"
        args = [declarator(a) for a in t.argument_types()]
        if k == T.FUNCTIONPROTO and t.is_function_variadic():
            # m2c parses C, where a lone ellipsis is not legal; () keeps the arguments unspecified.
            if not args:
                return declarator(t.get_result(), "%s()" % name)
            args.append("...")
        return declarator(t.get_result(),
                          "%s(%s)" % (name, ", ".join(args) or "void"))

    # clang spells C++ record types bare; the forward typedefs make that legal C.
    base = t.spelling
    for tag in ("class ", "struct ", "union ", "enum "):
        base = base.replace(tag, "")
    # m2c cannot parse a C++ template-id, so each becomes a flat name (see TEMPLATE_NAMES).
    spelled = c_type_name(base)
    if spelled != base:
        TEMPLATE_NAMES.add(re.sub(r'[\s\*&]+$', '', spelled).split()[-1].rstrip('*&'))
    return (spelled + " " + name).rstrip()


# Every template-id spelled as a plain identifier above; each becomes an opaque forward typedef.
TEMPLATE_NAMES = set()


ANONYMOUS = re.compile(r"\((?:unnamed|anonymous)\b")


def anonymous_record(t):
    """The declaration of an unnamed record t is built from, or None.

    clang prints no name for one, so its definition is written out again where the field is declared.
    Arrays are looked through.
    """
    while t.kind == T.ELABORATED:
        t = t.get_named_type()
    while t.kind in ARRAY:
        t = t.element_type
        while t.kind == T.ELABORATED:
            t = t.get_named_type()
    if t.kind != T.RECORD:
        return None
    decl = t.get_declaration()
    return decl if ANONYMOUS.search(decl.type.spelling) else None


def array_suffix(t):
    """The `[n]` an array type carries, so an inlined record keeps it."""
    while t.kind == T.ELABORATED:
        t = t.get_named_type()
    if t.kind not in ARRAY:
        return ""
    n = t.element_count if t.kind == T.CONSTANTARRAY else ""
    return "[%s]%s" % (n, array_suffix(t.element_type))


def record_fields(cur, depth=1):
    pad = "    " * depth
    out = []
    for f in cur.get_children():
        if f.kind != K.FIELD_DECL:
            continue
        decl = anonymous_record(f.type)
        if decl is not None:
            tag = "union" if decl.kind == K.UNION_DECL else "struct"
            body = record_fields(decl, depth + 1) or ["%s    char _empty;" % pad]
            out.append("%s%s {\n%s\n%s} %s%s;" % (
                pad, tag, "\n".join(body), pad,
                f.spelling, array_suffix(f.type)))
        elif f.is_bitfield():
            width = f.get_bitfield_width()
            field = ("unsigned long long %s" % f.spelling
                     if width > 32 else declarator(f.type, f.spelling))
            out.append("%s%s : %d;" % (pad, field, width))
        else:
            out.append("%s%s;" % (pad, declarator(f.type, f.spelling)))
    return out


def usable(cur, root):
    """Named, declared under root, and not one of the artefacts below."""
    if not cur.spelling or "(" in cur.spelling or cur.spelling.startswith("operator"):
        # Anonymous records are spelled "(unnamed struct at ...)".
        return False

    loc = cur.location.file
    # clang reports paths as written, relative to the working directory under -I; resolve them first.
    # Builtins have no file, which is what this mainly filters out.
    if loc is None or not os.path.abspath(str(loc)).startswith(root):
        return False

    # common.h's STATIC_ASSERT is defined away by the amalgamation; a lone .cpp still reaches it.
    return not cur.spelling.startswith("_static_assert")


# A linkage block (`extern "C" { ... }`) is a cursor of its own, with its declarations nested inside.
# Older libclang spells it UNEXPOSED_DECL and newer ones LINKAGE_SPEC.
TRANSPARENT = (K.UNEXPOSED_DECL, K.LINKAGE_SPEC, K.NAMESPACE)


def declarations(cursor):
    """Every declaration below cursor, with linkage blocks walked through."""
    for cur in cursor.get_children():
        if cur.kind in TRANSPARENT:
            for inner in declarations(cur):
                yield inner
        else:
            yield cur


def convert(tu, root):
    enums, fwd, typedefs, records, rest = [], [], [], [], []
    fwd.append("typedef unsigned char bool;")
    # Opaque project template specialization used by free-function ABIs.
    fwd.append("typedef struct CDataAlloc2_1_ CDataAlloc2_1_;")
    tags = []
    defined = set()

    for cur in declarations(tu.cursor):
        if not usable(cur, root):
            continue

        if cur.kind == K.ENUM_DECL and cur.is_definition():
            if cur.spelling in defined:
                continue
            defined.add(cur.spelling)
            vals = ["    %s = %d," % (c.spelling, c.enum_value)
                    for c in cur.get_children() if c.kind == K.ENUM_CONSTANT_DECL]
            enums.append("enum %s {\n%s\n};" % (cur.spelling, "\n".join(vals)))
            enums.append("typedef enum %s %s;" % (cur.spelling, cur.spelling))

        elif cur.kind in (K.STRUCT_DECL, K.CLASS_DECL, K.UNION_DECL):
            tag = "union" if cur.kind == K.UNION_DECL else "struct"
            name = c_type_name(cur.spelling)
            if name not in tags:
                tags.append(name)
                fwd.append("typedef %s %s %s;" % (tag, name, name))
            # The first definition wins, matching duplicate_extents.
            if cur.is_definition() and name not in defined:
                defined.add(name)
                body = record_fields(cur)
                record = "%s %s {\n%s\n};" % (
                    tag, name, "\n".join(body) or "    char _empty;")
            else:
                record = None
            # A member may name a nested record unqualified, and clang does not expose
            # nested records at translation-unit scope.
            for nested in cur.get_children():
                if (nested.kind not in (K.STRUCT_DECL, K.CLASS_DECL, K.UNION_DECL)
                        or not nested.spelling or "(" in nested.spelling):
                    continue
                nested_name = c_type_name(nested.spelling)
                nested_tag = "union" if nested.kind == K.UNION_DECL else "struct"
                if nested_name not in tags:
                    tags.append(nested_name)
                    fwd.append("typedef %s %s %s;" % (nested_tag, nested_name, nested_name))
                if nested.is_definition() and nested_name not in defined:
                    defined.add(nested_name)
                    nested_body = record_fields(nested)
                    records.append("%s %s {\n%s\n};" % (
                        nested_tag, nested_name,
                        "\n".join(nested_body) or "    char _empty;"))
            if record is not None:
                records.append(record)

        elif cur.kind == K.TYPEDEF_DECL:
            under = cur.underlying_typedef_type
            if under.get_declaration().spelling == cur.spelling:
                continue        # `typedef struct X X;`, already emitted above
            typedefs.append("typedef %s;" % declarator(under, cur.spelling))

        elif cur.kind == K.FUNCTION_DECL:
            rest.append("%s;" % declarator(cur.type, cur.spelling))

        elif cur.kind == K.VAR_DECL:
            # C++ lets a variable share a tag's name; C typedefs do not.
            if cur.spelling not in tags:
                rest.append("extern %s;" % declarator(cur.type, cur.spelling))

    template_records = []
    for name in sorted(TEMPLATE_NAMES):
        line = f"typedef struct {name} {name};"
        if line not in fwd:
            fwd.append(line)
        if name.startswith("mgRect_") and name.endswith("_"):
            element = name[len("mgRect_"):-1]
            if element in ("int", "float"):
                template_records.append("struct %s { %s left, top, right, bottom; };" % (name, element))
        elif name == "mgCObjectStack_CList_EMAP_MESSAGE__":
            template_records.append("struct %s { unsigned char unk_0[8]; int unk_8; unsigned char unk_c[8]; };" % name)
    return "\n".join(enums + fwd + typedefs + template_records + records + rest) + "\n"


def translate(source, flags, root=None):
    """Parses source as C++ with flags, and returns it as C declarations."""
    root = root or os.getcwd()
    tu = ci.Index.create().parse(source, args=list(flags) + ["-x", "c++"])

    # Quiet by default: a whole-project context is not a translation unit any build produces, so
    # clang's errors there are expected. M2CTX_VERBOSE=1 prints them.
    if os.environ.get("M2CTX_VERBOSE"):
        for d in tu.diagnostics:
            if d.severity >= ci.Diagnostic.Error:
                print("m2ctx: %s" % d.spelling, file=sys.stderr)

    return convert(tu, root)


#
# Entry point
#

def import_project(cpp_path, includes, flags, root):
    """Context for the whole project. The amalgamated C++ is written to cpp_path and converted from
    there, so the two files cannot diverge."""
    text = build_cpp(includes[0], flags)

    os.makedirs(os.path.dirname(cpp_path) or ".", exist_ok=True)
    with open(cpp_path, "w", encoding="UTF-8") as f:
        f.write(text)

    return translate(os.path.relpath(cpp_path, root), flags, root)


def main():
    parser = argparse.ArgumentParser(
        description="Create a context file which can be used for m2c")
    parser.add_argument(
        "c_file", nargs="?",
        help="translation unit to build the context from; omit for the whole project")
    parser.add_argument(
        "-o", "--output", required=True,
        help="where to write the context; the whole-project C++ goes beside it, as .cpp")
    parser.add_argument(
        "-I", dest="include", action="append", metavar="DIR",
        help="include directory (repeatable; default: %s)" % " ".join(DEFAULT_INCLUDES))
    parser.add_argument(
        "-D", dest="define", action="append", default=[], metavar="NAME",
        help="macro definition, NAME or NAME=VALUE (repeatable)")
    args = parser.parse_args()

    root = os.getcwd()
    includes = args.include or DEFAULT_INCLUDES
    flags = BASE_FLAGS + ["-I" + d for d in includes] + ["-D" + d for d in args.define]

    out_path = os.path.abspath(args.output)

    if args.c_file:
        # One translation unit: there is no amalgamation to write.
        source = os.path.relpath(os.path.abspath(args.c_file), root)
        output = translate(source, flags, root)
    else:
        cpp_path = os.path.splitext(out_path)[0] + ".cpp"
        output = import_project(cpp_path, includes, flags, root)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    with open(out_path, "w", encoding="UTF-8") as f:
        f.write(output)


if __name__ == "__main__":
    main()
