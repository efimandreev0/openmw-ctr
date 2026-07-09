#!/usr/bin/env python3
"""
Add R_ARM_ABS32 relocations for linker-generated veneer literal pools.

The ARM linker generates veneers (trampolines) for long-range branch calls
and ARM/Thumb interworking. Each veneer contains an embedded absolute address
in its literal pool. However, the linker does NOT generate ELF relocations
for these literal pool entries.

When vita-elf-create converts the ELF to VELF, it cannot create SCE
relocations for the veneer literals (no input ELF relocs). The Vita loader
then fails to adjust these addresses at load time, causing jumps to wrong
targets.

This script:
1. Scans executable sections for known veneer instruction patterns
2. Validates each candidate by checking that the literal pool value is
   a valid address within the binary's memory range
3. Creates R_ARM_ABS32 relocation entries with proper section symbols
   (vita-elf-create discards relocations with symbol index 0)
4. Appends them to the .rel.text section in the ELF

Veneer patterns detected:
  ARM long branch:       e51ff004 LITERAL       (ldr pc, [pc, #-4]; .word)
  Thumb-to-ARM:          4778 e7fd e59fc000 e12fff1c LITERAL
  ARM via ip:            e59fc000 e12fff1c LITERAL   (ldr ip, [pc]; bx ip; .word)
"""

import struct
import sys

R_ARM_ABS32 = 2
SHT_SYMTAB = 2
SHF_EXECINSTR = 0x4
STT_SECTION = 3

# Byte patterns for veneer detection (little-endian ARM)
# Pattern A: Thumb-to-ARM interworking veneer (12 bytes instruction + 4 literal)
#   bx pc; b.n .; ldr ip, [pc]; bx ip; .word
PAT_THUMB_ARM = b'\x78\x47\xfd\xe7\x00\xc0\x9f\xe5\x1c\xff\x2f\xe1'
PAT_THUMB_ARM_LIT_OFF = 12

# Pattern B: ARM long branch veneer (4 bytes instruction + 4 literal)
#   ldr pc, [pc, #-4]; .word
PAT_ARM_LONG = b'\x04\xf0\x1f\xe5'
PAT_ARM_LONG_LIT_OFF = 4

# Pattern C: ARM via ip veneer (8 bytes instruction + 4 literal)
#   ldr ip, [pc]; bx ip; .word
PAT_ARM_IP = b'\x00\xc0\x9f\xe5\x1c\xff\x2f\xe1'
PAT_ARM_IP_LIT_OFF = 8


def parse_elf_header(data):
    """Parse 32-bit little-endian ELF header."""
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        return None
    return {
        'e_phoff':     struct.unpack_from('<I', data, 28)[0],
        'e_shoff':     struct.unpack_from('<I', data, 32)[0],
        'e_phentsize': struct.unpack_from('<H', data, 42)[0],
        'e_phnum':     struct.unpack_from('<H', data, 44)[0],
        'e_shentsize': struct.unpack_from('<H', data, 46)[0],
        'e_shnum':     struct.unpack_from('<H', data, 48)[0],
        'e_shstrndx':  struct.unpack_from('<H', data, 50)[0],
    }


def parse_section_header(data, offset):
    """Parse a single section header at the given file offset."""
    return {
        'sh_name':      struct.unpack_from('<I', data, offset)[0],
        'sh_type':      struct.unpack_from('<I', data, offset + 4)[0],
        'sh_flags':     struct.unpack_from('<I', data, offset + 8)[0],
        'sh_addr':      struct.unpack_from('<I', data, offset + 12)[0],
        'sh_offset':    struct.unpack_from('<I', data, offset + 16)[0],
        'sh_size':      struct.unpack_from('<I', data, offset + 20)[0],
        'sh_link':      struct.unpack_from('<I', data, offset + 24)[0],
        'sh_info':      struct.unpack_from('<I', data, offset + 28)[0],
        'sh_addralign': struct.unpack_from('<I', data, offset + 32)[0],
        'sh_entsize':   struct.unpack_from('<I', data, offset + 36)[0],
    }


def get_section_name(data, shstrtab_sh, name_offset):
    """Read a null-terminated section name from .shstrtab."""
    start = shstrtab_sh['sh_offset'] + name_offset
    end = data.index(b'\x00', start)
    return data[start:end].decode('ascii')


def get_load_segments(data, ehdr):
    """Parse PT_LOAD program headers."""
    segments = []
    for i in range(ehdr['e_phnum']):
        pos = ehdr['e_phoff'] + i * ehdr['e_phentsize']
        p_type = struct.unpack_from('<I', data, pos)[0]
        if p_type == 1:  # PT_LOAD
            segments.append({
                'p_vaddr': struct.unpack_from('<I', data, pos + 8)[0],
                'p_memsz': struct.unpack_from('<I', data, pos + 20)[0],
                'p_flags': struct.unpack_from('<I', data, pos + 24)[0],
            })
    return segments


def find_segment_for_addr(segments, addr):
    """Find which PT_LOAD segment contains the given address."""
    for i, seg in enumerate(segments):
        if seg['p_vaddr'] <= addr < seg['p_vaddr'] + seg['p_memsz']:
            return i
    return -1


def build_segment_symbol_map(data, sections, segments):
    """Build a mapping from PT_LOAD segment index to a section symbol index.

    vita-elf-create uses the symbol's value to determine the target segment
    for relocations. We need a symbol whose st_value falls within each
    PT_LOAD segment and whose st_shndx is not 0/ABS/COMMON.
    """
    # Find the symbol table section
    symtab_sh = None
    for sh in sections:
        if sh['sh_type'] == SHT_SYMTAB:
            symtab_sh = sh
            break

    if symtab_sh is None:
        return {}

    entsize = symtab_sh['sh_entsize']
    if entsize == 0:
        entsize = 16  # Elf32_Sym size
    num_syms = symtab_sh['sh_size'] // entsize

    # For each segment, find the first STT_SECTION symbol within it
    seg_sym_map = {}  # segment_index -> (symbol_index, symbol_value)

    for sym_idx in range(num_syms):
        sym_off = symtab_sh['sh_offset'] + sym_idx * entsize
        st_value = struct.unpack_from('<I', data, sym_off + 4)[0]
        st_info = data[sym_off + 12]
        st_shndx = struct.unpack_from('<H', data, sym_off + 14)[0]

        # Must be STT_SECTION type with valid shndx
        sym_type = st_info & 0xF
        if sym_type != STT_SECTION:
            continue
        if st_shndx == 0 or st_shndx >= 0xFF00:  # UND, ABS, COMMON, etc.
            continue

        seg_idx = find_segment_for_addr(segments, st_value)
        if seg_idx >= 0 and seg_idx not in seg_sym_map:
            seg_sym_map[seg_idx] = (sym_idx, st_value)

    return seg_sym_map


def scan_region_for_veneers(region_data, region_vaddr, addr_min, addr_max):
    """Scan a memory region for veneer patterns.

    Returns list of (literal_vaddr, literal_value) tuples.
    """
    results = []
    # Track region-relative offsets of literals already found
    found_offsets = set()

    # Pass 1: Longest pattern first (Thumb-to-ARM, 12+4 bytes)
    pos = 0
    while True:
        idx = region_data.find(PAT_THUMB_ARM, pos)
        if idx == -1:
            break
        lit_off = idx + PAT_THUMB_ARM_LIT_OFF
        if lit_off + 4 <= len(region_data):
            val = struct.unpack_from('<I', region_data, lit_off)[0]
            if addr_min <= (val & ~1) < addr_max:
                results.append((region_vaddr + lit_off, val))
                found_offsets.add(lit_off)
                # Mark the ARM-via-ip sub-pattern's literal offset too
                found_offsets.add(idx + 4 + PAT_ARM_IP_LIT_OFF)
        pos = idx + 2

    # Pass 2: ARM long branch (4+4 bytes)
    pos = 0
    while True:
        idx = region_data.find(PAT_ARM_LONG, pos)
        if idx == -1:
            break
        lit_off = idx + PAT_ARM_LONG_LIT_OFF
        if lit_off not in found_offsets and lit_off + 4 <= len(region_data):
            val = struct.unpack_from('<I', region_data, lit_off)[0]
            if addr_min <= (val & ~1) < addr_max:
                results.append((region_vaddr + lit_off, val))
                found_offsets.add(lit_off)
        pos = idx + 4

    # Pass 3: ARM via ip (8+4 bytes)
    pos = 0
    while True:
        idx = region_data.find(PAT_ARM_IP, pos)
        if idx == -1:
            break
        lit_off = idx + PAT_ARM_IP_LIT_OFF
        if lit_off not in found_offsets and lit_off + 4 <= len(region_data):
            val = struct.unpack_from('<I', region_data, lit_off)[0]
            if addr_min <= (val & ~1) < addr_max:
                results.append((region_vaddr + lit_off, val))
                found_offsets.add(lit_off)
        pos = idx + 4

    return results


def fix_veneers(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    ehdr = parse_elf_header(data)
    if ehdr is None:
        print("Error: not a 32-bit little-endian ELF", file=sys.stderr)
        return False

    # Parse all section headers
    shstrtab_sh = parse_section_header(
        data, ehdr['e_shoff'] + ehdr['e_shstrndx'] * ehdr['e_shentsize'])

    sections = []
    rel_text_idx = None
    for i in range(ehdr['e_shnum']):
        sh_pos = ehdr['e_shoff'] + i * ehdr['e_shentsize']
        sh = parse_section_header(data, sh_pos)
        sh['_hdr_offset'] = sh_pos
        sh['_name'] = get_section_name(data, shstrtab_sh, sh['sh_name'])
        sections.append(sh)
        if sh['_name'] == '.rel.text':
            rel_text_idx = i

    if rel_text_idx is None:
        print("Error: .rel.text section not found", file=sys.stderr)
        return False

    rel_sh = sections[rel_text_idx]

    # Parse PT_LOAD segments
    segments = get_load_segments(data, ehdr)
    if not segments:
        print("Error: no PT_LOAD segments found", file=sys.stderr)
        return False

    addr_min = min(s['p_vaddr'] for s in segments)
    addr_max = max(s['p_vaddr'] + s['p_memsz'] for s in segments)

    # Build segment → section symbol mapping
    seg_sym_map = build_segment_symbol_map(data, sections, segments)
    if not seg_sym_map:
        print("Error: could not find section symbols for segments", file=sys.stderr)
        return False

    for seg_idx, (sym_idx, sym_val) in seg_sym_map.items():
        print(f"  Segment {seg_idx}: using symbol {sym_idx} (value 0x{sym_val:08x})")

    # Scan all executable sections for veneer patterns
    all_veneers = []  # list of (literal_vaddr, literal_value)
    for sh in sections:
        if not (sh['sh_flags'] & SHF_EXECINSTR):
            continue
        if sh['sh_size'] == 0:
            continue
        region = bytes(data[sh['sh_offset']:sh['sh_offset'] + sh['sh_size']])
        veneers = scan_region_for_veneers(region, sh['sh_addr'], addr_min, addr_max)
        if veneers:
            print(f"  {sh['_name']}: found {len(veneers)} veneer literal(s)")
        all_veneers.extend(veneers)

    if not all_veneers:
        print("No veneer literal pools found — nothing to fix")
        return True

    # Deduplicate by literal vaddr
    seen = set()
    unique_veneers = []
    for vaddr, val in all_veneers:
        if vaddr not in seen:
            seen.add(vaddr)
            unique_veneers.append((vaddr, val))
    unique_veneers.sort()
    print(f"Total: {len(unique_veneers)} veneer literal pools need relocations")

    # Build new R_ARM_ABS32 relocation entries (SHT_REL: 8 bytes each)
    # vita-elf-create uses the symbol to determine the target segment.
    # We must use a section symbol (STT_SECTION) whose value falls within
    # the same PT_LOAD segment as the veneer's target address.
    new_entries = bytearray()
    skipped = 0
    for vaddr, literal_val in unique_veneers:
        target_addr = literal_val & ~1  # Strip Thumb bit
        target_seg = find_segment_for_addr(segments, target_addr)
        if target_seg < 0 or target_seg not in seg_sym_map:
            skipped += 1
            continue
        sym_idx, _ = seg_sym_map[target_seg]
        r_offset = vaddr
        r_info = (sym_idx << 8) | R_ARM_ABS32
        new_entries += struct.pack('<II', r_offset, r_info)

    if skipped:
        print(f"  Warning: skipped {skipped} veneers (target outside known segments)")

    added = len(new_entries) // 8
    print(f"Adding {added} R_ARM_ABS32 relocation entries")

    # Read existing .rel.text data
    old_rel = bytes(data[rel_sh['sh_offset']:rel_sh['sh_offset'] + rel_sh['sh_size']])
    combined_rel = old_rel + bytes(new_entries)

    # Strategy: append the combined .rel.text data after the current file,
    # then append updated section headers. Update ELF header to point to
    # the new section header table.

    # 1. Append combined .rel.text data
    new_rel_offset = len(data)
    data += combined_rel

    # 2. Append new section header table
    new_shoff = len(data)
    for i in range(ehdr['e_shnum']):
        old_pos = ehdr['e_shoff'] + i * ehdr['e_shentsize']
        entry = bytearray(data[old_pos:old_pos + ehdr['e_shentsize']])
        if i == rel_text_idx:
            # Update .rel.text: new offset and size
            struct.pack_into('<I', entry, 16, new_rel_offset)
            struct.pack_into('<I', entry, 20, len(combined_rel))
        data += entry

    # 3. Update ELF header: e_shoff → new table
    struct.pack_into('<I', data, 32, new_shoff)

    with open(path, 'wb') as f:
        f.write(data)

    old_count = len(old_rel) // 8
    new_count = len(combined_rel) // 8
    print(f".rel.text: {old_count} → {new_count} entries (+{new_count - old_count})")
    print(f"Successfully added veneer literal pool relocations")
    return True


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <elf-file>", file=sys.stderr)
        sys.exit(1)
    if not fix_veneers(sys.argv[1]):
        sys.exit(1)
