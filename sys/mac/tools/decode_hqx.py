#!/usr/bin/env python3
"""Decode BinHex 4.0 (.hqx) files into data fork + resource fork."""

import sys
import struct
import os

# BinHex 4.0 character set
CHARS = '!"#$%&\'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr'
TABLE = {c: i for i, c in enumerate(CHARS)}

def decode_hqx_stream(data):
    """Decode BinHex 4.0 encoded data to raw bytes."""
    # Strip header line and find content between colons
    lines = data.split('\n')
    content = ''
    in_data = False
    for line in lines:
        line = line.strip()
        if not in_data:
            if line.startswith(':'):
                in_data = True
                content += line[1:]  # skip leading colon
            continue
        if line.endswith(':'):
            content += line[:-1]  # skip trailing colon
            break
        content += line

    # Decode 6-bit characters to bytes
    bits = 0
    nbits = 0
    raw = bytearray()
    for c in content:
        if c in TABLE:
            bits = (bits << 6) | TABLE[c]
            nbits += 6
            while nbits >= 8:
                nbits -= 8
                raw.append((bits >> nbits) & 0xFF)

    # RLE decode: 0x90 is escape, 0x90 0x00 = literal 0x90
    decoded = bytearray()
    i = 0
    while i < len(raw):
        if raw[i] == 0x90:
            i += 1
            if i >= len(raw):
                break
            if raw[i] == 0x00:
                decoded.append(0x90)
            else:
                count = raw[i] - 1
                if decoded:
                    last = decoded[-1]
                    decoded.extend([last] * count)
            i += 1
        else:
            decoded.append(raw[i])
            i += 1

    return bytes(decoded)

def parse_hqx_header(data):
    """Parse BinHex header: name, type, creator, flags, data len, rsrc len."""
    pos = 0
    namelen = data[pos]
    pos += 1
    name = data[pos:pos+namelen].decode('mac_roman', errors='replace')
    pos += namelen
    pos += 1  # version byte

    ftype = data[pos:pos+4].decode('ascii', errors='replace')
    pos += 4
    creator = data[pos:pos+4].decode('ascii', errors='replace')
    pos += 4
    flags = struct.unpack('>H', data[pos:pos+2])[0]
    pos += 2
    datalen = struct.unpack('>l', data[pos:pos+4])[0]
    pos += 4
    rsrclen = struct.unpack('>l', data[pos:pos+4])[0]
    pos += 4
    pos += 2  # header CRC

    return name, ftype, creator, flags, datalen, rsrclen, pos

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.hqx> [output_dir]")
        sys.exit(1)

    hqx_file = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else '.'

    with open(hqx_file, 'r') as f:
        raw_data = f.read()

    decoded = decode_hqx_stream(raw_data)
    name, ftype, creator, flags, datalen, rsrclen, hdr_end = parse_hqx_header(decoded)

    print(f"Name:    {name}")
    print(f"Type:    {ftype}")
    print(f"Creator: {creator}")
    print(f"Flags:   0x{flags:04x}")
    print(f"Data:    {datalen} bytes")
    print(f"Rsrc:    {rsrclen} bytes")

    data_start = hdr_end
    data_end = data_start + datalen
    rsrc_start = data_end + 2  # skip data CRC
    rsrc_end = rsrc_start + rsrclen

    data_fork = decoded[data_start:data_end]
    rsrc_fork = decoded[rsrc_start:rsrc_end]

    os.makedirs(outdir, exist_ok=True)

    safe_name = name.replace('/', '_').replace(':', '_')
    if datalen > 0:
        data_path = os.path.join(outdir, safe_name + '.data')
        with open(data_path, 'wb') as f:
            f.write(data_fork)
        print(f"Wrote:   {data_path} ({len(data_fork)} bytes)")

    if rsrclen > 0:
        rsrc_path = os.path.join(outdir, safe_name + '.rsrc')
        with open(rsrc_path, 'wb') as f:
            f.write(rsrc_fork)
        print(f"Wrote:   {rsrc_path} ({len(rsrc_fork)} bytes)")

if __name__ == '__main__':
    main()
