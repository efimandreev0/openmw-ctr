# -*- coding: utf-8 -*-
"""Texture downscale preprocessor for the 3DS port.

Walks every TES3 .bsa in a Morrowind "Data Files" directory, extracts each
textures/*.dds that has a mip chain, drops the top mip level(s) (halving the
resolution per drop, quartering the pixel footprint — no re-encode, the
remaining mips are bit-identical to the originals), and writes the result as
a loose file under "Data Files/textures/...". OpenMW's VFS gives loose files
priority over BSA content, so the smaller textures win at load time; deleting
the generated files reverts everything.

PICA200 has no DXT support: NovaGL decompresses DXT to RGBA/RGB565 on the CPU
at upload, so smaller sources directly cut linear-heap usage, decode time and
load-time RAM spikes on the 3DS.

Usage: python downscale_textures.py "<path to Data Files>" [--dry]
"""
import os
import struct
import sys

# Never touch UI/readable content: fonts and book pages become unreadable,
# splash/menu art is upscaled fullscreen where artifacts are obvious.
SKIP_PREFIXES = (
    "textures/menu_",
    "textures/tx_menubook",
    "textures/bookart/",
    "textures/fonts/",
    "textures/splash/",
    "textures/cursor",
    "textures/omw_",  # OpenMW UI overlays
)

# Drop mips while the larger side exceeds this, up to MAX_DROPS per texture.
# 128: on the 400x240 screen a typical surface covers well under 100px, so
# 128px sources are visually indistinguishable there while quartering (or
# sixteenth-ing) the decoded footprint.
TARGET_MAX_SIDE = 128
MIN_SIDE_AFTER_DROP = 32  # never drop below this
MAX_DROPS = 2

DDSD_MIPMAPCOUNT = 0x20000
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40


def read_bsa_index(path):
    """Return {name_lower: (offset, size)} for a TES3 BSA."""
    with open(path, "rb") as f:
        head = f.read(12)
        if len(head) < 12:
            return {}
        version, hash_off, count = struct.unpack("<III", head)
        if version != 0x100:
            return {}
        sizes_offsets = struct.unpack("<%dI" % (count * 2), f.read(count * 8))
        name_offs = struct.unpack("<%dI" % count, f.read(count * 4))
        names_size = hash_off - (count * 8 + count * 4)
        names_blob = f.read(names_size)
        f.seek(12 + hash_off + count * 8)
        data_base = 12 + hash_off + count * 8
        out = {}
        for i in range(count):
            end = names_blob.index(b"\x00", name_offs[i])
            name = names_blob[name_offs[i]:end].decode("cp1251", "replace").lower().replace("\\", "/")
            size = sizes_offsets[i * 2]
            off = sizes_offsets[i * 2 + 1]
            out[name] = (data_base + off, size)
        return out


def mip0_bytes(w, h, fourcc, rgb_bitcount):
    if fourcc in (b"DXT1",):
        return max(1, w // 4) * max(1, h // 4) * 8
    if fourcc in (b"DXT2", b"DXT3", b"DXT4", b"DXT5"):
        return max(1, w // 4) * max(1, h // 4) * 16
    if fourcc == b"":
        return w * h * (rgb_bitcount // 8)
    return None  # unknown compression — skip


def drop_mips(data):
    """Return downscaled DDS bytes or None if not applicable."""
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    (size, flags, height, width) = struct.unpack_from("<IIII", data, 4)
    if size != 124:
        return None
    mip_count = struct.unpack_from("<I", data, 28)[0]
    pf_flags = struct.unpack_from("<I", data, 80)[0]
    fourcc = data[84:88] if (pf_flags & DDPF_FOURCC) else b""
    rgb_bitcount = struct.unpack_from("<I", data, 88)[0]
    if not (flags & DDSD_MIPMAPCOUNT) or mip_count < 2:
        return None

    header = bytearray(data[:128])
    payload_off = 128
    w, h, mips = width, height, mip_count
    drops = 0
    while (
        drops < MAX_DROPS
        and mips >= 2
        and max(w, h) > TARGET_MAX_SIDE
        and min(w, h) // 2 >= MIN_SIDE_AFTER_DROP
    ):
        top = mip0_bytes(w, h, fourcc, rgb_bitcount)
        if top is None or payload_off + top > len(data):
            return None
        payload_off += top
        w //= 2
        h //= 2
        mips -= 1
        drops += 1
    if drops == 0:
        return None

    struct.pack_into("<I", header, 12, h)
    struct.pack_into("<I", header, 16, w)
    new_linear = mip0_bytes(w, h, fourcc, rgb_bitcount)
    struct.pack_into("<I", header, 20, new_linear or 0)
    struct.pack_into("<I", header, 28, mips)
    return bytes(header) + data[payload_off:]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data_dir = sys.argv[1]
    dry = "--dry" in sys.argv
    bsas = [f for f in os.listdir(data_dir) if f.lower().endswith(".bsa")]
    # openmw.cfg registration order decides priority; alphabetical covers the
    # vanilla set (Bloodmoon > Tribunal > Morrowind is not alphabetical, so
    # apply base game first and expansions after to let them win collisions).
    order = ["morrowind.bsa", "tribunal.bsa", "bloodmoon.bsa"]
    bsas.sort(key=lambda n: (order.index(n.lower()) if n.lower() in order else 99, n.lower()))

    merged = {}
    for bsa in bsas:
        idx = read_bsa_index(os.path.join(data_dir, bsa))
        for name, loc in idx.items():
            if name.startswith("textures/") and name.endswith(".dds"):
                merged[name] = (os.path.join(data_dir, bsa), loc)

    done = skipped = saved = 0
    for name, (bsa_path, (off, size)) in sorted(merged.items()):
        if any(name.startswith(p) for p in SKIP_PREFIXES):
            skipped += 1
            continue
        with open(bsa_path, "rb") as f:
            f.seek(off)
            blob = f.read(size)
        smaller = drop_mips(blob)
        if smaller is None:
            skipped += 1
            continue
        out_path = os.path.join(data_dir, *name.split("/"))
        if not dry:
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(out_path, "wb") as f:
                f.write(smaller)
        done += 1
        saved += size - len(smaller)
    print("downscaled: %d | skipped: %d | bytes saved: %.1f MB" % (done, skipped, saved / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
