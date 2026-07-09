#!/usr/bin/env python3
"""
Fix ARM ELF relocations that vita-elf-create doesn't support.

Patches R_ARM_BASE_PREL (25) and R_ARM_GOT_BREL (26) relocations to
R_ARM_NONE (0) so vita-elf-create can process the binary.

These relocations are GOT-relative references from C++ exception handling
that are already resolved by the linker in static executables.
"""

import struct
import sys
import os

# ARM relocation types to patch → R_ARM_NONE (0)
UNSUPPORTED_TYPES = {25, 26, 38, 41, 64}  # R_ARM_BASE_PREL, R_ARM_GOT_BREL, R_ARM_TARGET1, R_ARM_TARGET2, R_ARM_V4BX

def patch_elf(path):
    with open(path, 'r+b') as f:
        # Read ELF header
        f.seek(0)
        e_ident = f.read(16)
        if e_ident[:4] != b'\x7fELF':
            print(f"Error: {path} is not an ELF file", file=sys.stderr)
            return False

        is_32 = e_ident[4] == 1
        is_le = e_ident[5] == 1

        if not is_32 or not is_le:
            print("Error: Expected 32-bit little-endian ELF", file=sys.stderr)
            return False

        # Read ELF header fields
        f.seek(32)  # e_shoff at offset 32 for 32-bit
        e_shoff = struct.unpack('<I', f.read(4))[0]
        f.seek(46)  # e_shentsize at offset 46
        e_shentsize = struct.unpack('<H', f.read(2))[0]
        e_shnum = struct.unpack('<H', f.read(2))[0]

        patched = 0

        # Iterate section headers
        for i in range(e_shnum):
            sh_offset = e_shoff + i * e_shentsize
            f.seek(sh_offset + 4)  # sh_type
            sh_type = struct.unpack('<I', f.read(4))[0]

            # SHT_REL = 9 (without explicit addend), SHT_RELA = 4 (with addend)
            if sh_type not in (4, 9):
                continue

            f.seek(sh_offset + 16)  # sh_offset
            rel_offset = struct.unpack('<I', f.read(4))[0]
            rel_size = struct.unpack('<I', f.read(4))[0]
            f.seek(sh_offset + 36)  # sh_entsize
            rel_entsize = struct.unpack('<I', f.read(4))[0]

            if rel_entsize == 0:
                continue

            num_entries = rel_size // rel_entsize

            for j in range(num_entries):
                entry_offset = rel_offset + j * rel_entsize
                f.seek(entry_offset + 4)  # r_info
                r_info = struct.unpack('<I', f.read(4))[0]

                rel_type = r_info & 0xFF

                if rel_type in UNSUPPORTED_TYPES:
                    # Patch to R_ARM_NONE (0), preserve symbol index
                    new_r_info = r_info & 0xFFFFFF00  # Zero out type bits
                    f.seek(entry_offset + 4)
                    f.write(struct.pack('<I', new_r_info))
                    patched += 1

        print(f"Patched {patched} unsupported relocations to R_ARM_NONE")
        return True

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <elf-file>", file=sys.stderr)
        sys.exit(1)

    if not patch_elf(sys.argv[1]):
        sys.exit(1)
