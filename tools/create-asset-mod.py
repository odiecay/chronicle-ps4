#!/usr/bin/env python3
"""Package changed, extracted game assets without altering either input tree.

This does not translate PS2 executable patches. Model/container compatibility
still requires testing against Chronicle, especially normalized NTSC assets.
"""
import argparse
import filecmp
import json
import re
import shutil
from pathlib import Path

ID = re.compile(r"[a-z0-9_-]{1,64}\Z")


def fingerprint(path):
    value = 14695981039346656037
    with path.open("rb") as stream:
        while chunk := stream.read(65536):
            for byte in chunk:
                value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def index(root):
    result = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if not path.resolve().is_relative_to(root):
            raise ValueError(f"Input symlink escapes its tree: {path}")
        relative = path.relative_to(root).as_posix()
        if not relative.startswith("normalized/"):
            key = relative.lower()
            if key in result:
                raise ValueError(f"Case-colliding input paths: {relative}")
            result[key] = path
    normalized = root / "normalized"
    if normalized.is_dir():
        for path in sorted(normalized.rglob("*")):
            if path.is_file():
                if not path.resolve().is_relative_to(root):
                    raise ValueError(f"Normalized symlink escapes its tree: {path}")
                result[path.relative_to(normalized).as_posix().lower()] = path
    return result


def build(args):
    base = args.base.resolve(strict=True)
    modified = args.modified.resolve(strict=True)
    output = args.output.resolve()
    if not base.is_dir() or not modified.is_dir() or output.exists():
        raise ValueError("Use two input directories and a new output directory")
    if output.is_relative_to(base) or output.is_relative_to(modified):
        raise ValueError("Output must be outside both input trees")
    if not ID.fullmatch(args.id) or not args.name or len(args.name) > 256 or not args.version or len(args.version) > 256:
        raise ValueError("Invalid ID, name or version")
    if args.character_name and not re.fullmatch(r"[A-Za-z]{1,10}", args.character_name):
        raise ValueError("Character default name needs 1 to 10 Latin letters")
    original, replacement = index(base), index(modified)
    files, sources, total = [], [], 0
    for relative, path in replacement.items():
        if relative not in original:
            raise ValueError(f"No original asset for {relative}; new files need an engine feature first")
        old = original[relative]
        if filecmp.cmp(old, path, shallow=False):
            continue
        leaf = Path(relative).name
        # Index/release metadata belongs to the preserved base tree. Changed
        # offsets and sizes are represented in this pack's manifest instead.
        if relative in ("data.hd2", "languages.json"):
            continue
        if leaf.startswith(("slus_", "scus_", "sles_", "sces_")) or path.suffix.lower() in (".elf", ".irx"):
            raise ValueError(f"Executable/index changes need a native port: {relative}")
        size = path.stat().st_size
        original_size = old.stat().st_size
        if not 0 < size <= 67108864 or not 0 < original_size <= 67108864:
            raise ValueError(f"File sizes need 1..64 MiB: {relative}")
        if (size+2047)//2048 > (original_size+2047)//2048 and not args.allow_arena_growth:
            raise ValueError(f"Larger asset needs --allow-arena-growth and a checked destination at runtime: {relative}")
        source = f"files/{relative}"
        if len(source) > 240 or any(part in (".", "..") for part in Path(relative).parts) or any(c in relative for c in "\\:\0"):
            raise ValueError(f"Unsupported asset path: {relative}")
        total += size
        files.append({"path": relative, "source": source, "size": size, "original_size": original_size,
                      "original_fnv1a64": fingerprint(old), "replacement_fnv1a64": fingerprint(path)})
        sources.append(path)
    if not files or len(files) > 512 or total > 536870912:
        raise ValueError("Pack needs 1..512 changed files totaling <=512 MiB")
    manifest = {"format": 2, "id": args.id, "name": args.name, "version": args.version,
                "game": "dark-cloud", "type": "character-pack" if args.character_name else "asset-pack", "files": files}
    if args.character_name:
        manifest["character"] = {"slot": 0, "default_name": args.character_name}
    if args.allow_arena_growth:
        manifest["arena_checked_growth"] = True
    if args.requires:
        dependencies = []
        for value in args.requires:
            name, separator, version = value.partition("@")
            if not ID.fullmatch(name) or name == args.id or (separator and not version):
                raise ValueError(f"Invalid dependency: {value}")
            dependency = {"id": name}
            if separator:
                dependency["version"] = version
            dependencies.append(dependency)
        if len(dependencies) > 32 or len({d["id"] for d in dependencies}) != len(dependencies):
            raise ValueError("Invalid or duplicate dependencies")
        manifest["requires"] = dependencies
    if args.conflicts:
        if len(args.conflicts) > 32 or len(set(args.conflicts)) != len(args.conflicts) or any(not ID.fullmatch(c) or c == args.id for c in args.conflicts):
            raise ValueError("Invalid conflicting IDs")
        manifest["conflicts"] = args.conflicts
    text = json.dumps(manifest, indent=2) + "\n"
    if len(text.encode()) > 262144:
        raise ValueError("Manifest exceeds the runtime's 256 KiB limit")
    # All contract checks precede creation. An interrupted copy remains a
    # diagnosable incomplete new folder; it never replaces an earlier pack.
    output.mkdir(parents=True, exist_ok=False)
    for entry, source in zip(files, sources):
        destination = output / entry["source"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    (output / "mod.json").write_text(text, encoding="utf-8")
    return len(files), total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("base", "modified", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", default="1")
    parser.add_argument("--character-name")
    parser.add_argument("--allow-arena-growth", action="store_true",
                        help="Permit larger replacements only when the runtime can bound their destination arena")
    parser.add_argument("--requires", action="append", default=[], metavar="ID[@VERSION]")
    parser.add_argument("--conflicts", action="append", default=[], metavar="ID")
    args = parser.parse_args()
    try:
        count, total = build(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Cannot build pack: {error}\n")
    print(f"Created {args.output}: {count} changed assets, {total} bytes. Inputs unchanged.")


if __name__ == "__main__":
    main()
