#!/usr/bin/env python3
"""Verify RELA relocations by replaying them and checking for bad addresses.

Simulates Retro68ApplyRelocations for each CODE segment and checks
if the resulting addresses are within valid ranges.
"""
import struct
import sys

def read_resource(data, rtype_want, rid_want):
    """Read a specific resource from a resource fork."""
    doff = struct.unpack('>I', data[0:4])[0]
    moff = struct.unpack('>I', data[4:8])[0]
    tls = moff + struct.unpack('>H', data[moff+24:moff+26])[0]
    ntypes = struct.unpack('>H', data[tls:tls+2])[0] + 1
    pos = tls + 2
    for i in range(ntypes):
        rtype = data[pos:pos+4]
        count = struct.unpack('>H', data[pos+4:pos+6])[0] + 1
        roff = struct.unpack('>H', data[pos+6:pos+8])[0]
        rstart = tls + roff
        for j in range(count):
            rp = rstart + j * 12
            rid = struct.unpack('>h', data[rp:rp+2])[0]
            ao = struct.unpack('>I', data[rp+4:rp+8])[0]
            rdoff = ao & 0x00FFFFFF
            abs_off = doff + rdoff
            rlen = struct.unpack('>I', data[abs_off:abs_off+4])[0]
            rdata = data[abs_off+4:abs_off+4+rlen]
            if rtype == rtype_want and rid == rid_want:
                return rdata
        pos += 8
    return None

def replay_rela(code_data, rela_data, code_base, a5, verbose=False):
    """Replay RELA relocations and check for bad addresses.

    code_data: the CODE resource data (including 40-byte header)
    rela_data: the RELA resource data
    code_base: simulated load address of CODE resource
    a5: simulated A5 value
    """
    # CODEHeader: loadAddress at offset 32, currentA5 at offset 24
    load_addr = struct.unpack('>I', code_data[32:36])[0]
    current_a5 = struct.unpack('>I', code_data[24:28])[0]

    # For first load, these are 0
    displacements = [
        code_base - load_addr,   # code displacement
        a5 - current_a5,         # data displacement
        a5 - current_a5,         # bss displacement
        a5 - current_a5,         # jt displacement
    ]

    if verbose:
        print(f"  loadAddress=0x{load_addr:08x} currentA5=0x{current_a5:08x}")
        print(f"  Displacements: code={displacements[0]:#x} data={displacements[1]:#x}")

    # Make a mutable copy of code data
    code = bytearray(code_data)

    # Apply relocations (same logic as Retro68ApplyRelocations)
    reloc_idx = 0
    total_relocs = 0
    bad_relocs = []

    for pass_num in range(2):
        addr_ptr = 40 - 1  # base + 40 - 1 (skip header)
        while reloc_idx < len(rela_data) and rela_data[reloc_idx] != 0:
            # Read uleb128
            val = 0
            shift = 0
            while reloc_idx < len(rela_data):
                b = rela_data[reloc_idx]
                reloc_idx += 1
                val |= (b & 0x7F) << shift
                shift += 7
                if not (b & 0x80):
                    break

            addr_ptr += val >> 2
            kind = val & 3

            if addr_ptr < 40 or addr_ptr + 3 >= len(code):
                bad_relocs.append((total_relocs, addr_ptr, kind, "OUT OF BOUNDS"))
                total_relocs += 1
                continue

            # Read current value
            orig = (code[addr_ptr] << 24) | (code[addr_ptr+1] << 16) | \
                   (code[addr_ptr+2] << 8) | code[addr_ptr+3]

            # Apply displacement
            new_val = (orig + displacements[kind]) & 0xFFFFFFFF
            if pass_num == 1:
                new_val = (new_val - (code_base + addr_ptr)) & 0xFFFFFFFF

            # Check if result is valid
            # Valid ranges: 0-0x08000000 for 128MB RAM, 0x40800000-0x40900000 for ROM
            is_valid = (new_val < 0x08000000) or (0x40800000 <= new_val < 0x40900000)

            if not is_valid and pass_num == 0:
                bad_relocs.append((total_relocs, addr_ptr, kind,
                    f"BAD: orig=0x{orig:08x} + disp[{kind}]=0x{displacements[kind]:08x} = 0x{new_val:08x}"))

            # Write back
            code[addr_ptr] = (new_val >> 24) & 0xFF
            code[addr_ptr+1] = (new_val >> 16) & 0xFF
            code[addr_ptr+2] = (new_val >> 8) & 0xFF
            code[addr_ptr+3] = new_val & 0xFF

            total_relocs += 1

        reloc_idx += 1  # skip terminator

    return total_relocs, bad_relocs

def main():
    rsrc_path = sys.argv[1] if len(sys.argv) > 1 else 'targets/mac68k/.rsrc/NetHack'

    with open(rsrc_path, 'rb') as f:
        data = f.read()

    # Simulate realistic addresses
    # CODE 1 at ~0x07744e10 (from earlier debug), A5 at ~0x07fa9a88
    # These don't need to be exact — we just check if results are in valid range
    a5 = 0x07fa9a88

    for seg_id in range(1, 9):
        code = read_resource(data, b'CODE', seg_id)
        rela = read_resource(data, b'RELA', seg_id)
        if not code or not rela:
            continue

        # Simulate CODE base address (varies by segment)
        code_base = 0x07744e10 + (seg_id - 1) * 0x200000  # rough estimate

        print(f"\n=== CODE {seg_id}: {len(code)} bytes, RELA: {len(rela)} bytes ===")
        total, bad = replay_rela(code, rela, code_base, a5, verbose=True)
        print(f"  Total relocations: {total}")
        print(f"  Bad relocations: {len(bad)}")
        for idx, offset, kind, msg in bad[:20]:
            print(f"    #{idx}: offset=0x{offset:06x} kind={kind} {msg}")
        if len(bad) > 20:
            print(f"    ... and {len(bad)-20} more")

if __name__ == '__main__':
    main()
