#!/usr/bin/env python3
"""Pre-swizzled NovaGL texture cache (.nsw v4) generator for the 3DS port.

Walks the game's VFS (BSA archives in openmw.cfg fallback-archive order, then
loose files — later sources override earlier ones, matching OpenMW), decodes
every image, downscales it (halve anything over --halve-min, cap at --cap,
force power-of-two), builds a full mip chain down to 8px, compresses to the
PICA's native block formats (ETC1 opaque / ETC1A4 with alpha — 4/8 bits per
pixel instead of RGBA8's 32), lays blocks out exactly the way the PICA reads
them, and writes <out>/<fnv1a(path)>.nsw.

v4 notes (all verified byte-for-byte against devkitPro tex3ds output):
  - Orientation is TOP-DOWN: no vertical flip anywhere. OpenMW's world
    textures arrive at NovaGL pre-flipped by OSG's dds_flip, and NovaGL's CPU
    swizzler flips again — the two cancel, so PICA memory holds row 0 = top
    row.  v3 baked bottom-up and every cached texture rendered upside down.
  - ETC1 payload: 8x8 tiles row-major; inside a tile the four 4x4 blocks go
    in Morton order (0,0),(1,0),(0,1),(1,1); every 64-bit ETC1 block is
    stored little-endian (byte-reversed vs. the standard ETC1 stream).
  - ETC1A4 payload: per block, an 8-byte alpha word FIRST (4 bits per texel,
    nibble index = x*4+y, not byte-reversed), then the 8-byte ETC1 block.

At runtime imagemanager hashes the normalized VFS path with the same FNV-1a,
finds the file and NovaGL streams it straight into texture memory: no DDS
decode, no downscale, no mip generation, no swizzle, no compression on the
console — and at 4-8x less memory than the old RGBA8/RGB565 bake.

Usage (WSL):
  python3 preprocess_textures_nsw.py \
      --data-dir "/mnt/c/Users/.../Citra/sdmc/3ds/openmw/Data Files" \
      --out      "/mnt/c/Users/.../Citra/sdmc/3ds/openmw/cache/tex"

Requires: Pillow (DDS/TGA decode), numpy, etcpak (ETC1 encoder).
"""

import argparse
import io
import os
import struct
import sys

import numpy as np
import etcpak
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

IMAGE_EXTS = (".dds", ".tga", ".bmp", ".png", ".jpg", ".jpeg")

GPU_ETC1 = 0xC
GPU_ETC1A4 = 0xD

NSW_MAGIC = 0x5753564E  # 'NVSW' LE
NSW_VERSION = 4


def fnv1a(s: str) -> int:
    h = 2166136261
    for ch in s.encode("utf-8"):
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def normalize(path: str) -> str:
    return path.replace("\\", "/").lower()


# --- Morrowind BSA reader -------------------------------------------------

def read_bsa_index(path):
    """Yields (normalized_name, absolute_offset, size) for each file."""
    with open(path, "rb") as f:
        head = f.read(12)
        if len(head) < 12:
            return
        version, hash_off, num = struct.unpack("<III", head)
        if version != 0x100:
            print(f"  ! {os.path.basename(path)}: not a Morrowind BSA (version 0x{version:x})")
            return
        dir_bytes = f.read(hash_off)
        sizes_offsets = struct.unpack_from(f"<{num * 2}I", dir_bytes, 0)
        name_offs = struct.unpack_from(f"<{num}I", dir_bytes, num * 8)
        names_blob = dir_bytes[num * 12 :]
        f.seek(12 + hash_off + num * 8)
        data_start = 12 + hash_off + num * 8
        for i in range(num):
            size = sizes_offsets[i * 2]
            off = sizes_offsets[i * 2 + 1]
            end = names_blob.find(b"\0", name_offs[i])
            name = names_blob[name_offs[i] : end].decode("cp1252", "replace")
            yield normalize(name), data_start + off, size


# --- VFS assembly ----------------------------------------------------------

def build_vfs(data_dir, cfg_path):
    """normalized path -> ('bsa', bsa_path, off, size) | ('file', fs_path)."""
    archives = []
    if cfg_path and os.path.isfile(cfg_path):
        with open(cfg_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("fallback-archive="):
                    archives.append(line.split("=", 1)[1].strip().strip('"'))
    if not archives:
        archives = [n for n in sorted(os.listdir(data_dir)) if n.lower().endswith(".bsa")]

    vfs = {}
    for arch in archives:
        p = os.path.join(data_dir, arch)
        if not os.path.isfile(p):
            print(f"  ! archive missing: {arch}")
            continue
        n = 0
        for name, off, size in read_bsa_index(p):
            vfs[name] = ("bsa", p, off, size)
            n += 1
        print(f"  {arch}: {n} entries")

    # Loose files override everything (VFS adds them last).
    loose = 0
    for root, _dirs, files in os.walk(data_dir):
        for fn in files:
            if fn.lower().endswith(".bsa"):
                continue
            full = os.path.join(root, fn)
            rel = normalize(os.path.relpath(full, data_dir))
            vfs[rel] = ("file", full)
            loose += 1
    print(f"  loose files: {loose}")
    return vfs


def read_entry(entry) -> bytes:
    if entry[0] == "bsa":
        _, p, off, size = entry
        with open(p, "rb") as f:
            f.seek(off)
            return f.read(size)
    with open(entry[1], "rb") as f:
        return f.read()


# --- PICA ETC1 block layout -------------------------------------------------

_pica_perms = {}


def pica_block_perm(w, h):
    """Index into the standard row-major 4x4 block stream for each PICA
    storage position: 8x8 tiles row-major, blocks Morton-ordered in-tile."""
    key = (w, h)
    p = _pica_perms.get(key)
    if p is None:
        nbx = w // 4
        order = []
        for ty in range(h // 8):
            for tx in range(w // 8):
                for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    order.append((ty * 2 + dy) * nbx + (tx * 2 + dx))
        p = np.array(order, np.int64)
        _pica_perms[key] = p
    return p


def encode_level(arr, fmt):
    """arr: (h, w, 4) uint8, TOP-DOWN. Returns PICA-ordered payload bytes."""
    h, w = arr.shape[:2]
    perm = pica_block_perm(w, h)
    enc = np.frombuffer(
        etcpak.compress_etc1_rgb(np.ascontiguousarray(arr).tobytes(), w, h), np.uint8
    ).reshape(-1, 8)
    color = enc[perm][:, ::-1]  # PICA reads each 64-bit block little-endian
    if fmt == GPU_ETC1:
        return color.tobytes()

    # ETC1A4: 8 alpha bytes (nibble i = x*4+y) precede each color block.
    a4 = (arr[..., 3] >> 4).astype(np.uint8)
    nib = a4.reshape(h // 4, 4, w // 4, 4).transpose(0, 2, 3, 1).reshape(-1, 16)
    abytes = (nib[:, 0::2] | (nib[:, 1::2] << 4)).astype(np.uint8)
    return np.concatenate([abytes[perm], color], axis=1).tobytes()


# --- Conversion ------------------------------------------------------------

def pot_clamp(v, cap):
    v = max(8, min(cap, v))
    return 1 << int(round(np.log2(v)))


def convert_one(name, raw, cap, halve_min):
    try:
        img = Image.open(io.BytesIO(raw))
        img.load()
    except Exception as e:
        return None, f"decode failed: {e}"

    orig_w, orig_h = img.size
    if orig_w < 1 or orig_h < 1:
        return None, "empty image"

    # Morrowind quirk: 16bpp TGA alpha is ignored by the original engine.
    kill_alpha = name.endswith(".tga") and img.mode in ("RGB", "P", "L")

    # DXT1 punch-through: blocks in color0<=color1 mode decode as
    # transparent BLACK texels. Most Morrowind DXT1 content is meant to be
    # opaque — stray punch-through blocks turned dock planks and armor
    # pitch black in-game. Keep alpha only when it is clearly intentional
    # (a meaningful share of the image, e.g. foliage cutouts).
    is_dxt1 = len(raw) > 88 and raw[:4] == b"DDS " and raw[84:88] == b"DXT1"

    img = img.convert("RGBA")
    if is_dxt1:
        a = np.asarray(img, dtype=np.uint8)[..., 3]
        transparent_frac = float((a < 128).mean())
        if transparent_frac < 0.01:
            kill_alpha = True

    tgt_w, tgt_h = orig_w, orig_h
    if max(orig_w, orig_h) > halve_min:
        tgt_w, tgt_h = orig_w // 2, orig_h // 2
    tgt_w = pot_clamp(tgt_w, cap)
    tgt_h = pot_clamp(tgt_h, cap)

    if (tgt_w, tgt_h) != (orig_w, orig_h):
        img = img.resize((tgt_w, tgt_h), Image.LANCZOS)

    arr = np.asarray(img, dtype=np.uint8)
    if kill_alpha:
        arr = arr.copy()
        arr[..., 3] = 255

    has_alpha = int(arr[..., 3].min()) < 250
    fmt = GPU_ETC1A4 if has_alpha else GPU_ETC1

    # Mip chain down to 8px on the SMALLER axis (8x8 = one PICA tile).
    levels = 1
    while (tgt_w >> levels) >= 8 and (tgt_h >> levels) >= 8:
        levels += 1
    levels = min(levels, 8)

    payload = bytearray()
    cur = img
    for lv in range(levels):
        if lv > 0:
            cur = cur.resize((tgt_w >> lv, tgt_h >> lv), Image.LANCZOS)
        a = np.asarray(cur, dtype=np.uint8)
        if kill_alpha:
            a = a.copy()
            a[..., 3] = 255
        payload += encode_level(a, fmt)

    hdr = struct.pack(
        "<11I", NSW_MAGIC, NSW_VERSION, fmt, tgt_w, tgt_h, tgt_w, tgt_h, orig_w, orig_h, len(payload), levels)
    return hdr + bytes(payload), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cfg", default=None, help="openmw.cfg for fallback-archive order")
    ap.add_argument("--cap", type=int, default=512)
    ap.add_argument("--halve-min", type=int, default=64,
        help="halve only textures larger than this (keeps icons/UI sharp)")
    ap.add_argument("--force", action="store_true", help="regenerate existing entries")
    args = ap.parse_args()

    cfg = args.cfg
    if cfg is None:
        guess = os.path.join(os.path.dirname(args.data_dir.rstrip("/\\")), "config", "openmw.cfg")
        cfg = guess if os.path.isfile(guess) else None
    print(f"cfg: {cfg}")

    print("indexing VFS...")
    vfs = build_vfs(args.data_dir, cfg)

    images = {p: e for p, e in vfs.items() if p.endswith(IMAGE_EXTS)}
    print(f"{len(images)} images to consider")

    # Hash collision audit — a collision would show the wrong texture in game.
    by_hash = {}
    for p in images:
        h = fnv1a(p)
        if h in by_hash and by_hash[h] != p:
            print(f"FATAL: fnv1a collision: {by_hash[h]} vs {p}")
            sys.exit(2)
        by_hash[h] = p

    os.makedirs(args.out, exist_ok=True)

    done = skipped = failed = 0
    total_bytes = 0
    for i, (path, entry) in enumerate(sorted(images.items())):
        h = fnv1a(path)
        out_path = os.path.join(args.out, f"{h:08x}.nsw")
        if not args.force and os.path.exists(out_path):
            skipped += 1
            continue
        try:
            raw = read_entry(entry)
        except Exception as e:
            print(f"  ! read {path}: {e}")
            failed += 1
            continue
        blob, err = convert_one(path, raw, args.cap, args.halve_min)
        if blob is None:
            print(f"  ! {path}: {err}")
            failed += 1
            continue
        with open(out_path, "wb") as f:
            f.write(blob)
        total_bytes += len(blob)
        done += 1
        if done % 250 == 0:
            print(f"  {done} done ({total_bytes // (1024 * 1024)} MB)...")

    print(f"done: {done} converted, {skipped} skipped, {failed} failed, {total_bytes // (1024 * 1024)} MB written")


if __name__ == "__main__":
    main()
