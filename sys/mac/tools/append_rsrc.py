#!/usr/bin/env python3
"""Append resources from a source resource fork to a destination resource fork.

Unlike Rez --copy, this NEVER moves existing resources in the data section.
New resources are appended to the END of the data section. The resource map
is rebuilt to include both old and new resources.

This preserves the exact offsets of CODE/DATA/RELA resources that Retro68
depends on.
"""
import struct
import sys

def read_rsrc_fork(data):
    """Parse a resource fork into a list of (type, id, name, attrs, data_offset, rdata) tuples."""
    doff = struct.unpack('>I', data[0:4])[0]
    moff = struct.unpack('>I', data[4:8])[0]
    dlen = struct.unpack('>I', data[8:12])[0]

    tl_off = struct.unpack('>H', data[moff+24:moff+26])[0]
    nl_off = struct.unpack('>H', data[moff+26:moff+28])[0]
    nl_start = moff + nl_off
    tl_start = moff + tl_off
    ntypes = struct.unpack('>H', data[tl_start:tl_start+2])[0] + 1

    resources = []
    pos = tl_start + 2
    for i in range(ntypes):
        rtype = data[pos:pos+4]
        count = struct.unpack('>H', data[pos+4:pos+6])[0] + 1
        roff = struct.unpack('>H', data[pos+6:pos+8])[0]
        ref_start = tl_start + roff
        for j in range(count):
            rp = ref_start + j * 12
            rid = struct.unpack('>h', data[rp:rp+2])[0]
            name_off = struct.unpack('>h', data[rp+2:rp+4])[0]
            ao = struct.unpack('>I', data[rp+4:rp+8])[0]
            attrs = (ao >> 24) & 0xFF
            rdoff = ao & 0x00FFFFFF
            abs_off = doff + rdoff
            rlen = struct.unpack('>I', data[abs_off:abs_off+4])[0]
            rdata = data[abs_off+4:abs_off+4+rlen]

            rname = b''
            if name_off >= 0:
                npos = nl_start + name_off
                nlen = data[npos]
                rname = data[npos+1:npos+1+nlen]

            resources.append((rtype, rid, rname, attrs, rdoff, rdata))
        pos += 8

    return resources, doff, dlen

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <dest_rsrc_fork> <source.rsrc> [source2.rsrc ...]")
        sys.exit(1)

    dest_path = sys.argv[1]
    source_paths = sys.argv[2:]

    with open(dest_path, 'rb') as f:
        dest_data = f.read()

    dest_resources, dest_doff, dest_dlen = read_rsrc_fork(dest_data)

    # Keep track of existing resource keys
    existing = set()
    for rtype, rid, rname, attrs, rdoff, rdata in dest_resources:
        existing.add((bytes(rtype), rid))

    print(f"Destination: {len(dest_resources)} resources, data section: {dest_dlen} bytes")

    # Collect new resources from sources
    new_resources = []
    for src_path in source_paths:
        with open(src_path, 'rb') as f:
            src_data = f.read()
        src_resources, _, _ = read_rsrc_fork(src_data)
        for rtype, rid, rname, attrs, rdoff, rdata in src_resources:
            key = (bytes(rtype), rid)
            if key not in existing:
                new_resources.append((rtype, rid, rname, attrs, rdata))
                existing.add(key)

    print(f"Adding {len(new_resources)} new resources")

    # Build new resource fork:
    # 1. Keep existing data section EXACTLY as-is
    # 2. Append new resource data at the end
    # 3. Rebuild the resource map with all resources

    # Copy existing data section verbatim
    new_data = bytearray(dest_data[dest_doff:dest_doff + dest_dlen])

    # Append new resources to data section
    new_resource_entries = []
    for rtype, rid, rname, attrs, rdata in new_resources:
        new_rdoff = len(new_data)
        new_data += struct.pack('>I', len(rdata))
        new_data += rdata
        new_resource_entries.append((rtype, rid, rname, attrs, new_rdoff))

    # Combine all resources (old with original offsets + new with appended offsets)
    all_resources = []
    for rtype, rid, rname, attrs, rdoff, rdata in dest_resources:
        all_resources.append((rtype, rid, rname, attrs, rdoff))
    for rtype, rid, rname, attrs, rdoff in new_resource_entries:
        all_resources.append((rtype, rid, rname, attrs, rdoff))

    # Build resource map
    by_type = {}
    for rtype, rid, rname, attrs, rdoff in all_resources:
        key = bytes(rtype)
        if key not in by_type:
            by_type[key] = []
        by_type[key].append((rid, rname, attrs, rdoff))

    # Type list
    num_types = len(by_type)
    type_list = struct.pack('>H', num_types - 1)

    # Calculate reference list positions
    ref_lists = bytearray()
    type_entries = []
    name_list = bytearray()

    for rtype in sorted(by_type.keys()):
        items = by_type[rtype]
        ref_offset = 2 + num_types * 8 + len(ref_lists)  # relative to type list start
        type_entries.append((rtype, len(items), ref_offset))
        for rid, rname, attrs, rdoff in sorted(items, key=lambda x: x[0]):
            if rname:
                noff = len(name_list)
                name_list += bytes([len(rname)]) + rname
            else:
                noff = -1
            attrs_and_off = ((attrs & 0xFF) << 24) | (rdoff & 0x00FFFFFF)
            ref_lists += struct.pack('>h', rid)
            ref_lists += struct.pack('>h', noff)
            ref_lists += struct.pack('>I', attrs_and_off)
            ref_lists += b'\x00\x00\x00\x00'

    # Build type list entries
    for rtype, count, ref_off in type_entries:
        type_list += rtype
        type_list += struct.pack('>H', count - 1)
        type_list += struct.pack('>H', ref_off)

    # Map header (28 bytes) + type list + ref lists + name list
    map_header_size = 28
    tl_offset = map_header_size
    nl_offset = map_header_size + len(type_list) + len(ref_lists)

    map_body = type_list + ref_lists + name_list
    map_total = bytearray(map_header_size) + map_body

    # Set type list and name list offsets in map header
    struct.pack_into('>H', map_total, 24, tl_offset)
    struct.pack_into('>H', map_total, 26, nl_offset)

    # Build final fork
    data_start = 256  # standard
    new_data_len = len(new_data)
    map_start = data_start + new_data_len
    map_len = len(map_total)

    # Header (16 bytes at offset 0 of the 256-byte header area)
    header = struct.pack('>IIII', data_start, map_start, new_data_len, map_len)

    # Copy header into map too (first 16 bytes of map = copy of header)
    struct.pack_into('>IIII', map_total, 0, data_start, map_start, new_data_len, map_len)

    # Assemble
    fork = bytearray(256)
    fork[:16] = header
    fork += new_data
    fork += map_total

    with open(dest_path, 'wb') as f:
        f.write(fork)

    print(f"Wrote {dest_path}: {len(all_resources)} resources, {len(fork)} bytes")
    print(f"CODE/DATA/RELA offsets PRESERVED at original positions")

if __name__ == '__main__':
    main()
