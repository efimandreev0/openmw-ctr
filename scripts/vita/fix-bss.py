#!/usr/bin/env python3
"""
Expand BSS in a VELF by padding the data segment with zeros.

The Vita module loader does NOT zero BSS (the memsz > filesz gap).
Running objcopy --set-section-flags .bss=alloc,load,contents on the
INPUT ELF breaks vita-elf-create's SCE relocation generation.

This script works on the OUTPUT VELF instead:
1. Find the RW LOAD segment (data + bss)
2. If filesz < memsz, append zeros so filesz == memsz
3. Adjust all file offsets for segments/sections after the expansion point

Run AFTER vita-elf-create, BEFORE vita-make-fself.
"""

import struct
import sys


def fix_bss_in_velf(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    # Parse ELF header (32-bit LE)
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        print("Error: not a 32-bit little-endian ELF", file=sys.stderr)
        return False

    e_phoff = struct.unpack_from('<I', data, 28)[0]
    e_shoff = struct.unpack_from('<I', data, 32)[0]
    e_phentsize = struct.unpack_from('<H', data, 42)[0]
    e_phnum = struct.unpack_from('<H', data, 44)[0]
    e_shentsize = struct.unpack_from('<H', data, 46)[0]
    e_shnum = struct.unpack_from('<H', data, 48)[0]

    # Find the RW LOAD segment (data segment)
    data_seg_idx = None
    data_seg_offset = 0
    data_seg_filesz = 0
    data_seg_memsz = 0
    expansion = 0

    for i in range(e_phnum):
        pos = e_phoff + i * e_phentsize
        p_type = struct.unpack_from('<I', data, pos)[0]
        p_offset = struct.unpack_from('<I', data, pos + 4)[0]
        p_filesz = struct.unpack_from('<I', data, pos + 16)[0]
        p_memsz = struct.unpack_from('<I', data, pos + 20)[0]
        p_flags = struct.unpack_from('<I', data, pos + 24)[0]

        # PT_LOAD with RW flags (typically flags == 6: RW)
        if p_type == 1 and (p_flags & 0x2):  # PF_W
            if p_filesz < p_memsz:
                data_seg_idx = i
                data_seg_offset = p_offset
                data_seg_filesz = p_filesz
                data_seg_memsz = p_memsz
                expansion = p_memsz - p_filesz
                break

    if data_seg_idx is None:
        print("No RW LOAD segment with filesz < memsz found — nothing to fix")
        return True

    if expansion == 0:
        print("Data segment already has filesz == memsz — nothing to fix")
        return True

    insert_point = data_seg_offset + data_seg_filesz
    print(f"Data segment [{data_seg_idx}]: offset=0x{data_seg_offset:x} "
          f"filesz=0x{data_seg_filesz:x} memsz=0x{data_seg_memsz:x}")
    print(f"Expanding BSS: inserting {expansion} zero bytes at file offset 0x{insert_point:x}")

    # Insert zeros at the insertion point
    zeros = bytearray(expansion)
    data = data[:insert_point] + zeros + data[insert_point:]

    # Update the data segment's filesz to equal memsz
    phdr_pos = e_phoff + data_seg_idx * e_phentsize
    struct.pack_into('<I', data, phdr_pos + 16, data_seg_memsz)  # p_filesz = p_memsz

    # Adjust file offsets for everything after the insertion point

    # 1. Program headers: adjust p_offset for segments after the insertion
    for i in range(e_phnum):
        pos = e_phoff + i * e_phentsize
        p_offset = struct.unpack_from('<I', data, pos + 4)[0]
        if i != data_seg_idx and p_offset >= insert_point:
            struct.pack_into('<I', data, pos + 4, p_offset + expansion)

    # 2. Section header table offset
    if e_shoff >= insert_point:
        struct.pack_into('<I', data, 32, e_shoff + expansion)

    # 3. Section headers: adjust sh_offset and convert NOBITS → PROGBITS
    SHT_NOBITS = 8
    SHT_PROGBITS = 1
    new_e_shoff = struct.unpack_from('<I', data, 32)[0]
    bss_converted = 0
    for i in range(e_shnum):
        pos = new_e_shoff + i * e_shentsize
        if pos + 40 > len(data):
            break
        sh_type = struct.unpack_from('<I', data, pos + 4)[0]
        sh_offset = struct.unpack_from('<I', data, pos + 16)[0]
        sh_size = struct.unpack_from('<I', data, pos + 20)[0]

        if sh_offset >= insert_point:
            struct.pack_into('<I', data, pos + 16, sh_offset + expansion)

        # Convert NOBITS (BSS) sections to PROGBITS so vita-make-fself
        # includes the zeroed data in the SELF.  Without this, the SELF
        # may omit BSS and the loader leaves it uninitialised.
        if sh_type == SHT_NOBITS and sh_size > 0:
            struct.pack_into('<I', data, pos + 4, SHT_PROGBITS)
            bss_converted += 1
            print(f"Section [{i}]: converted NOBITS → PROGBITS (size=0x{sh_size:x})")

    if bss_converted:
        print(f"Converted {bss_converted} NOBITS section(s) to PROGBITS")

    with open(path, 'wb') as f:
        f.write(data)

    print(f"BSS expanded: filesz 0x{data_seg_filesz:x} → 0x{data_seg_memsz:x} "
          f"(+{expansion} bytes = +{expansion // 1024}KB)")
    return True


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <velf-file>", file=sys.stderr)
        sys.exit(1)
    if not fix_bss_in_velf(sys.argv[1]):
        sys.exit(1)
