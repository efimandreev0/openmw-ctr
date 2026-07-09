"""Pure-Python writer for Morrowind BSA v100 archives.

Format (matches OpenMW's components/bsa/bsafile.cpp):

    Header (12 bytes):
        u32 magic    = 0x00000100  ("version 100")
        u32 dirsize  = 12 * filenum + sizeof(name_string_buffer)
        u32 filenum

    Directory block (starts at offset 12):
        u32[2] * filenum   (fileSize, dataOffset) per file
        u32    * filenum   (nameOffset into the name string buffer)
        bytes              null-terminated, cp1252-encoded filenames

    Hash table (starts at offset 12 + dirsize):
        u64 * filenum      (lo:u32, hi:u32) per file

    File data (starts at offset 12 + dirsize + 8 * filenum):
        raw concatenated bytes for every file, in the same order as the
        size/offset and hash tables.

Files in the on-disk tables are sorted by (hash.lo, hash.hi) ascending.
Filenames are stored lowercase, with backslash separators.
"""

from __future__ import annotations

import struct
from typing import List, Tuple

_MAGIC = 0x00000100


def bsa_hash(name: str) -> Tuple[int, int]:
    """Compute Morrowind BSA's hash. Ports OpenMW's getHash() byte-for-byte.

    `name` should already be lowercase with backslash separators (the on-disk
    form). We pass it through .lower() defensively but do not change slashes
    here — the caller is responsible for using the canonical form.
    """
    data = name.lower().encode("cp1252", errors="replace")
    n = len(data)
    half = n >> 1

    sum_lo = 0
    off = 0
    for i in range(half):
        sum_lo ^= (data[i] << (off & 0x1F)) & 0xFFFFFFFF
        off += 8
    sum_lo &= 0xFFFFFFFF

    sum_hi = 0
    off = 0
    for i in range(half, n):
        temp = (data[i] << (off & 0x1F)) & 0xFFFFFFFF
        sum_hi = (sum_hi ^ temp) & 0xFFFFFFFF
        rot = temp & 0x1F
        if rot:
            sum_hi = ((sum_hi << (32 - rot)) | (sum_hi >> rot)) & 0xFFFFFFFF
        off += 8

    return sum_lo, sum_hi


def _canonical_name(path: str) -> str:
    """Convert a VFS path (forward-slash, mixed case) to Morrowind BSA form
    (lowercase, backslash-separated). OpenMW's VFS normalizes the inverse
    at lookup time, so the path round-trips correctly."""
    return path.replace("/", "\\").lower()


def build_bsa(entries: List[Tuple[str, bytes]]) -> bytes:
    """Build a BSA archive in memory.

    Args:
        entries: List of (vfs_path, file_bytes). vfs_path uses forward slashes
            (e.g. "meshes/foo.nif"); this function normalizes to the on-disk
            backslash form.

    Returns:
        Complete BSA file bytes ready to write.
    """
    filenum = len(entries)

    # Compute hashes and sort by (lo, hi). The on-disk tables are aligned in
    # this hash-sorted order.
    hashed = []
    for vfs_path, data in entries:
        name = _canonical_name(vfs_path)
        h = bsa_hash(name)
        hashed.append((h, name, data))
    hashed.sort(key=lambda e: e[0])

    # Build name buffer with offsets
    name_offsets: List[int] = []
    name_buf = bytearray()
    for _h, name, _data in hashed:
        name_offsets.append(len(name_buf))
        name_buf.extend(name.encode("cp1252", errors="replace"))
        name_buf.append(0)  # null terminator

    # Compute data offsets (relative to start of data buffer)
    data_offsets: List[int] = []
    data_sizes: List[int] = []
    cursor = 0
    for _h, _name, data in hashed:
        data_offsets.append(cursor)
        data_sizes.append(len(data))
        cursor += len(data)

    dirsize = 12 * filenum + len(name_buf)

    out = bytearray()
    out.extend(struct.pack("<III", _MAGIC, dirsize, filenum))
    for size, offset in zip(data_sizes, data_offsets):
        out.extend(struct.pack("<II", size, offset))
    for noff in name_offsets:
        out.extend(struct.pack("<I", noff))
    out.extend(name_buf)
    for (lo, hi), _name, _data in hashed:
        out.extend(struct.pack("<II", lo, hi))
    for _h, _name, data in hashed:
        out.extend(data)

    return bytes(out)


if __name__ == "__main__":
    # Tiny round-trip smoke test: build a 2-file BSA, parse it back, verify.
    sample = [
        ("Meshes/Test.nif", b"\x4eIF\x00fake-mesh-bytes"),
        ("textures/foo.dds", b"\x44\x44\x53\x20fake-dds-bytes"),
    ]
    blob = build_bsa(sample)

    magic, dirsize, filenum = struct.unpack_from("<III", blob, 0)
    assert magic == _MAGIC, f"bad magic 0x{magic:08x}"
    assert filenum == 2, f"bad filenum {filenum}"

    # Parse offset/size table
    off_tbl = struct.unpack_from(f"<{2*filenum}I", blob, 12)
    name_tbl = struct.unpack_from(f"<{filenum}I", blob, 12 + 8 * filenum)
    name_buf_start = 12 + 12 * filenum
    name_buf_end = 12 + dirsize
    name_buf = blob[name_buf_start:name_buf_end]
    data_buf_start = 12 + dirsize + 8 * filenum

    print(f"magic=0x{magic:08x} dirsize={dirsize} filenum={filenum}")
    for i in range(filenum):
        size = off_tbl[i * 2]
        doff = off_tbl[i * 2 + 1]
        noff = name_tbl[i]
        name_end = name_buf.find(b"\x00", noff)
        name = name_buf[noff:name_end].decode("cp1252")
        chunk = blob[data_buf_start + doff:data_buf_start + doff + size]
        print(f"  [{i}] name={name!r} size={size} data_off=0x{doff:x} -> {chunk[:24]!r}")
        assert name in {"meshes\\test.nif", "textures\\foo.dds"}, name
        assert chunk in {sample[0][1], sample[1][1]}, "data mismatch"
    print("ok: round-trip smoke test passed")
