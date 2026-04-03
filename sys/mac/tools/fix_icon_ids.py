#!/usr/bin/env python3
"""Remap icon resource IDs from 1000-1004 to 128-132 in a resource fork.

The NHrsrc from NetHack 3.4 uses ICN#/icl4/ics#/ics4 at IDs 1000-1004.
Some Finder versions expect the app icon at ID 128. This script duplicates
the icon resources at the standard IDs and updates the BNDL to match.
"""
import struct
import sys

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <resource_fork>")
        sys.exit(1)

    path = sys.argv[1]
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    doff = struct.unpack('>I', data[0:4])[0]
    moff = struct.unpack('>I', data[4:8])[0]
    dlen = struct.unpack('>I', data[8:12])[0]
    tl_off = struct.unpack('>H', data[moff+24:moff+26])[0]
    nl_off = struct.unpack('>H', data[moff+26:moff+28])[0]
    nl_start = moff + nl_off
    tl_start = moff + tl_off
    ntypes = struct.unpack('>H', data[tl_start:tl_start+2])[0] + 1

    # Parse all resources
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

    # Icon types to remap
    icon_types = {b'ICN#', b'icl4', b'icl8', b'ics#', b'ics4', b'ics8'}
    existing = set((bytes(r[0]), r[1]) for r in resources)

    # Add copies at 128-132 for any icon at 1000-1004
    added = []
    for rtype, rid, rname, attrs, rdoff, rdata in resources:
        if bytes(rtype) in icon_types and 1000 <= rid <= 1004:
            new_id = rid - 1000 + 128
            if (bytes(rtype), new_id) not in existing:
                added.append((rtype, new_id, rname, attrs, rdata))
                existing.add((bytes(rtype), new_id))

    if not added:
        print("No icon remapping needed")
        return

    # Update BNDL to reference 128-132 instead of 1000-1004
    for idx, (rtype, rid, rname, attrs, rdoff, rdata) in enumerate(resources):
        if bytes(rtype) == b'BNDL':
            rdata = bytearray(rdata)
            num_assoc = struct.unpack('>H', rdata[6:8])[0] + 1
            off = 8
            for a in range(num_assoc):
                atype = rdata[off:off+4]
                acount = struct.unpack('>H', rdata[off+4:off+6])[0] + 1
                off += 6
                for e in range(acount):
                    rsrc_id = struct.unpack('>H', rdata[off+2:off+4])[0]
                    if atype == b'ICN#' and 1000 <= rsrc_id <= 1004:
                        struct.pack_into('>H', rdata, off+2, rsrc_id - 1000 + 128)
                    off += 4
            resources[idx] = (rtype, rid, rname, attrs, rdoff, bytes(rdata))

    # Rebuild resource fork with added resources
    new_data = bytearray(data[doff:doff + dlen])

    all_resources = []
    for rtype, rid, rname, attrs, rdoff, rdata in resources:
        all_resources.append((rtype, rid, rname, attrs, rdoff))
    for rtype, rid, rname, attrs, rdata in added:
        new_rdoff = len(new_data)
        new_data += struct.pack('>I', len(rdata)) + rdata
        all_resources.append((rtype, rid, rname, attrs, new_rdoff))

    # Build resource map
    by_type = {}
    for rtype, rid, rname, attrs, rdoff in all_resources:
        key = bytes(rtype)
        if key not in by_type:
            by_type[key] = []
        by_type[key].append((rid, b'', attrs, rdoff))

    num_types = len(by_type)
    type_list = struct.pack('>H', num_types - 1)
    ref_lists = bytearray()
    type_entries = []
    name_list = bytearray()

    for rtype in sorted(by_type.keys()):
        items = by_type[rtype]
        ref_offset = 2 + num_types * 8 + len(ref_lists)
        type_entries.append((rtype, len(items), ref_offset))
        for rid, rname, attrs, rdoff in sorted(items, key=lambda x: x[0]):
            noff = -1
            attrs_and_off = ((attrs & 0xFF) << 24) | (rdoff & 0x00FFFFFF)
            ref_lists += struct.pack('>h', rid)
            ref_lists += struct.pack('>h', noff)
            ref_lists += struct.pack('>I', attrs_and_off)
            ref_lists += b'\x00\x00\x00\x00'

    for rtype, count, ref_off in type_entries:
        type_list += rtype
        type_list += struct.pack('>H', count - 1)
        type_list += struct.pack('>H', ref_off)

    map_header_size = 28
    tl_offset = map_header_size
    nl_offset = map_header_size + len(type_list) + len(ref_lists)
    map_body = type_list + ref_lists + name_list
    map_total = bytearray(map_header_size) + map_body
    struct.pack_into('>H', map_total, 24, tl_offset)
    struct.pack_into('>H', map_total, 26, nl_offset)

    data_start = 256
    new_data_len = len(new_data)
    map_start = data_start + new_data_len
    map_len = len(map_total)
    header = struct.pack('>IIII', data_start, map_start, new_data_len, map_len)
    struct.pack_into('>IIII', map_total, 0, data_start, map_start, new_data_len, map_len)

    fork = bytearray(256)
    fork[:16] = header
    fork += new_data
    fork += map_total

    with open(path, 'wb') as f:
        f.write(fork)

    for rtype, rid, rname, attrs, rdata in added:
        print(f"  Added {rtype.decode()} id={rid}")
    print(f"  Updated BNDL icon references: 1000-1004 -> 128-132")
    print(f"  Wrote {path}: {len(all_resources)} resources, {len(fork)} bytes")

if __name__ == '__main__':
    main()
