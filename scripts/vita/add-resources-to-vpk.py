#!/usr/bin/env python3
"""Inject a directory tree into a VPK (zip) under resources/.

Usage: add-resources-to-vpk.py <vpk> <resources_dir>

vita_create_vpk only packs explicit FILE entries; this adds the generated
resources/ tree that OpenMW needs at runtime. Idempotent on re-run.
"""

import os
import sys
import zipfile


def add_resources(vpk_path: str, resources_dir: str) -> int:
    if not os.path.isfile(vpk_path):
        print(f"Error: VPK not found: {vpk_path}", file=sys.stderr)
        return 1
    if not os.path.isdir(resources_dir):
        print(f"Error: resources dir not found: {resources_dir}", file=sys.stderr)
        return 1

    to_add = []
    for root, _dirs, files in os.walk(resources_dir):
        for name in files:
            abs_path = os.path.join(root, name)
            rel = os.path.relpath(abs_path, resources_dir)
            arcname = "resources/" + rel.replace(os.sep, "/")
            to_add.append((abs_path, arcname))

    with zipfile.ZipFile(vpk_path, "r") as zf:
        existing = set(zf.namelist())

    arcnames = {a for _s, a in to_add}
    collisions = arcnames & existing

    if collisions:
        # zipfile's append mode can't replace entries, only add; rewrite instead.
        tmp_path = vpk_path + ".tmp"
        with zipfile.ZipFile(vpk_path, "r") as zin, \
             zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                if item.filename not in collisions:
                    zout.writestr(item, zin.read(item.filename))
            for src, arcname in to_add:
                zout.write(src, arcname)
        os.replace(tmp_path, vpk_path)
    else:
        with zipfile.ZipFile(vpk_path, "a", zipfile.ZIP_DEFLATED) as zf:
            for src, arcname in to_add:
                zf.write(src, arcname)

    print(f"Injected {len(to_add)} files under resources/ into {vpk_path}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <vpk> <resources_dir>", file=sys.stderr)
        sys.exit(2)
    sys.exit(add_resources(sys.argv[1], sys.argv[2]))
