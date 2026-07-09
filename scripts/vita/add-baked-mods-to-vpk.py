#!/usr/bin/env python3
"""Inject baked-in mod assets into the VPK as Morrowind BSA archives.

Each sub-mod becomes one `.bsa` written at `resources/baked-mods/<NN>-<name>.bsa`.
The numeric prefix encodes load order (lower prefix loads first; later BSAs
win on conflict in OpenMW's fallback-archive precedence), matching the
Project Atlas README install order (MOP -> Atlas Core -> textures -> patches).

Why BSA: VitaShell's VPK install pays a per-file FAT stat/create/fsync cost.
Loose-file injection (~3,800 small files) makes installs take ~25 min on a
class-4 memory card. Packing each sub-mod into one BSA cuts that to ~1 min
without losing override semantics: ux0 loose-file mods always override BSAs
at the VFS layer (see registerarchives.cpp: BSAs registered first, loose
dirs after).

The corresponding `data=` + `fallback-archive=` cfg entries are written by
VitaInit.cpp's autoDetectBakedMods() at boot — by scanning the actual
contents of app0:/resources/baked-mods/, so the cfg side requires no
build-time coordination.

Usage: add-baked-mods-to-vpk.py <vpk> <mods_root>

Behaviour:
- Missing <mods_root> or missing individual sources is a logged no-op
  (devs without the mods/ tree can still build the VPK).
- Re-running is idempotent: prior `resources/baked-mods/` entries are
  replaced, and stale entries (sub-mod removed from manifest) are dropped.
"""

import os
import sys
import zipfile

# Make sibling bsa_pack importable when invoked via `python3 scripts/vita/...`
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsa_pack  # noqa: E402

# (source_dir_relative_to_mods_root, slug, load_order_prefix)
# Load order: MOP first, Atlas Core, Atlas textures, then Atlas mesh-replacer
# patches. Later BSAs win in OpenMW. Numeric prefixes ensure alphabetical
# sort at autodetect time matches intent.
MANIFEST = [
    (
        "Morrowinf Optimization Patch/"
        "Morrowind Optimization Patch-45384-1-18-0-1751572864/00 Core",
        "mop-core",
        "01",
    ),
    (
        "Morrowinf Optimization Patch/"
        "Morrowind Optimization Patch-45384-1-18-0-1751572864/"
        "04 Better Vanilla Textures",
        "mop-better-vanilla-textures",
        "02",
    ),
    (
        "Project Atlas/Project Atlas-45399-0-7-5-1747751438/00 Core",
        "atlas-core",
        "03",
    ),
    (
        "Project Atlas/Project Atlas-45399-0-7-5-1747751438/"
        "01 Textures - Vanilla",
        "atlas-vanilla-textures",
        "04",
    ),
    (
        "Project Atlas/Project Atlas-45399-0-7-5-1747751438/"
        "02 Urns - Smoothed",
        "atlas-smoothed-urns",
        "05",
    ),
    (
        "Project Atlas/Project Atlas-45399-0-7-5-1747751438/"
        "03 Redware - Smoothed",
        "atlas-smoothed-redware",
        "06",
    ),
    (
        "Project Atlas/Project Atlas-45399-0-7-5-1747751438/"
        "04 Emperor Parasols - Smoothed",
        "atlas-smoothed-parasols",
        "07",
    ),
]

ARCHIVE_PREFIX = "resources/baked-mods/"

# fomod/ is installer metadata, not game assets.
EXCLUDED_DIR_NAMES = {"fomod", "FOMOD"}

# Top-level credit / authorship files preserved alongside the BSAs.
# Kept loose (not packed into a BSA) so they remain readable for users
# inspecting the VPK contents.
CREDITS = [
    (
        "Project Atlas/Project Atlas-45399-0-7-5-1747751438/README.md",
        "credits/Project-Atlas-README.md",
    ),
    (
        "Morrowinf Optimization Patch/"
        "Morrowind Optimization Patch-45384-1-18-0-1751572864/"
        "Contributors.txt",
        "credits/MOP-Contributors.txt",
    ),
]


def should_skip_file(name: str) -> bool:
    # Windows alternate-data-stream files written out as regular files
    # when the source archive was extracted on a non-NTFS filesystem.
    return name.endswith(":Zone.Identifier")


def collect_files(source_dir: str):
    """Walk a sub-mod dir and yield (absolute_path, vfs_relative_path) tuples.
    VFS-relative path uses forward slashes and preserves source casing —
    bsa_pack normalizes to lowercase backslash form on the way out."""
    out = []
    for root, dirs, files in os.walk(source_dir):
        dirs[:] = [d for d in dirs if d not in EXCLUDED_DIR_NAMES]
        for name in files:
            if should_skip_file(name):
                continue
            abs_path = os.path.join(root, name)
            rel = os.path.relpath(abs_path, source_dir).replace(os.sep, "/")
            out.append((abs_path, rel))
    return out


def build_bsa_for_mod(source_dir: str) -> tuple[bytes, int, int]:
    """Read every file under source_dir into memory and pack a BSA.
    Returns (bsa_bytes, file_count, total_uncompressed_bytes)."""
    files = collect_files(source_dir)
    entries: list[tuple[str, bytes]] = []
    total = 0
    for abs_path, vfs_rel in files:
        with open(abs_path, "rb") as f:
            data = f.read()
        entries.append((vfs_rel, data))
        total += len(data)
    blob = bsa_pack.build_bsa(entries)
    return blob, len(entries), total


def main() -> int:
    if len(sys.argv) != 3:
        print(
            f"Usage: {sys.argv[0]} <vpk> <mods_root>",
            file=sys.stderr,
        )
        return 2

    vpk_path, mods_root = sys.argv[1], sys.argv[2]

    if not os.path.isfile(vpk_path):
        print(f"Error: VPK not found: {vpk_path}", file=sys.stderr)
        return 1

    if not os.path.isdir(mods_root):
        print(
            f"Note: mods root '{mods_root}' not present; "
            "skipping baked-mod bundle (VPK unchanged)."
        )
        return 0

    # Build BSAs in memory. Each entry: (vpk_arcname, bytes_payload,
    # human_label, file_count, uncompressed_bytes).
    bsa_entries = []
    missing = []
    for rel_src, slug, prefix in MANIFEST:
        src = os.path.join(mods_root, rel_src)
        if not os.path.isdir(src):
            missing.append((rel_src, slug))
            continue
        blob, count, uncompressed = build_bsa_for_mod(src)
        arcname = f"{ARCHIVE_PREFIX}{prefix}-{slug}.bsa"
        bsa_entries.append((arcname, blob, slug, count, uncompressed))

    # Credit files: kept as loose entries in the VPK (not in any BSA).
    loose_entries: list[tuple[str, str]] = []
    for rel_src, arc_rel in CREDITS:
        src = os.path.join(mods_root, rel_src)
        if os.path.isfile(src):
            loose_entries.append((src, ARCHIVE_PREFIX + arc_rel))

    if missing:
        print("Note: baked-mod sources missing on disk (skipped):")
        for rel_src, slug in missing:
            print(f"  - {slug}: {rel_src}")

    if not bsa_entries and not loose_entries:
        print("No baked-mod assets to add; VPK unchanged.")
        return 0

    new_arcnames = {a for a, _b, _s, _c, _u in bsa_entries} | {
        a for _src, a in loose_entries
    }

    with zipfile.ZipFile(vpk_path, "r") as zf:
        existing = set(zf.namelist())

    collisions = new_arcnames & existing
    # Sweep all stale entries under our prefix that aren't in the new set.
    # This cleans up loose-file leftovers from a prior bake version.
    stale = {
        e for e in existing if e.startswith(ARCHIVE_PREFIX)
    } - new_arcnames
    drop = collisions | stale

    if drop:
        tmp_path = vpk_path + ".tmp"
        with zipfile.ZipFile(vpk_path, "r") as zin, zipfile.ZipFile(
            tmp_path, "w", zipfile.ZIP_DEFLATED
        ) as zout:
            for item in zin.infolist():
                if item.filename in drop:
                    continue
                zout.writestr(item, zin.read(item.filename))
            for arcname, blob, _slug, _c, _u in bsa_entries:
                # BSAs are already-compressed-ish (NIF/DDS resist further
                # deflate) — STORED keeps the install fast and saves CPU.
                zout.writestr(arcname, blob, compress_type=zipfile.ZIP_STORED)
            for src, arcname in loose_entries:
                zout.write(src, arcname, compress_type=zipfile.ZIP_DEFLATED)
        os.replace(tmp_path, vpk_path)
    else:
        with zipfile.ZipFile(vpk_path, "a", zipfile.ZIP_DEFLATED) as zf:
            for arcname, blob, _slug, _c, _u in bsa_entries:
                zf.writestr(arcname, blob, compress_type=zipfile.ZIP_STORED)
            for src, arcname in loose_entries:
                zf.write(src, arcname, compress_type=zipfile.ZIP_DEFLATED)

    total_files = sum(c for _a, _b, _s, c, _u in bsa_entries)
    total_uncompressed = sum(u for _a, _b, _s, _c, u in bsa_entries)
    total_bsa_bytes = sum(len(b) for _a, b, _s, _c, _u in bsa_entries)
    print(
        f"Baked {total_files} files "
        f"({total_uncompressed / (1024 * 1024):.1f} MB uncompressed) "
        f"into {len(bsa_entries)} BSA(s) "
        f"({total_bsa_bytes / (1024 * 1024):.1f} MB) "
        f"under {vpk_path}"
    )
    for arcname, _blob, slug, count, uncompressed in bsa_entries:
        print(
            f"  + {os.path.basename(arcname)}  "
            f"({count} files, {uncompressed / (1024 * 1024):.1f} MB)"
        )
    if loose_entries:
        print(f"  + {len(loose_entries)} credit file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
