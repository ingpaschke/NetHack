#!/usr/bin/env python3
"""Create a MacBinary II file from an AppleDouble file pair.

Usage: make_macbin.py <data_fork> <rsrc_fork> <output.bin> [type] [creator]
"""
import struct
import sys

def crc_macbin(data):
    """Calculate MacBinary II CRC (CRC-CCITT with 0 init)."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

def make_macbinary(name, ftype, creator, data_fork, rsrc_fork):
    """Build a MacBinary II file."""
    name_bytes = name.encode('mac_roman')[:63]

    header = bytearray(128)
    # Byte 0: old version (0)
    header[0] = 0
    # Byte 1: filename length
    header[1] = len(name_bytes)
    # Bytes 2-64: filename
    header[2:2+len(name_bytes)] = name_bytes
    # Bytes 65-68: file type
    header[65:69] = ftype.encode('ascii')[:4]
    # Bytes 69-72: file creator
    header[69:73] = creator.encode('ascii')[:4]
    # Byte 73: Finder flags high byte
    header[73] = 0
    # Byte 74: zero
    header[74] = 0
    # Bytes 75-76: vertical position (0)
    # Bytes 77-78: horizontal position (0)
    # Bytes 79-80: window/folder ID (0)
    # Byte 81: protected flag
    header[81] = 0
    # Byte 82: zero
    header[82] = 0
    # Bytes 83-86: data fork length
    struct.pack_into('>I', header, 83, len(data_fork))
    # Bytes 87-90: resource fork length
    struct.pack_into('>I', header, 87, len(rsrc_fork))
    # Bytes 91-94: creation date (seconds since 1904-01-01)
    # Bytes 95-98: modification date
    import time
    mac_epoch = 2082844800  # seconds between 1904-01-01 and 1970-01-01
    now = int(time.time()) + mac_epoch
    struct.pack_into('>I', header, 91, now)
    struct.pack_into('>I', header, 95, now)
    # Bytes 99-100: Get Info comment length (0)
    # Byte 101: Finder flags low byte
    header[101] = 0
    # Bytes 102-115: reserved
    # Bytes 116-119: unpacked total length (0)
    # Bytes 120-121: secondary header length (0)
    # Byte 122: version (MacBinary II = 129)
    header[122] = 129
    # Byte 123: minimum version to read (129)
    header[123] = 129
    # Bytes 124-125: CRC of header bytes 0-123
    crc = crc_macbin(header[:124])
    struct.pack_into('>H', header, 124, crc)

    # Build file: header + data fork (padded to 128) + rsrc fork (padded to 128)
    out = bytes(header)
    out += data_fork
    if len(data_fork) % 128:
        out += b'\x00' * (128 - len(data_fork) % 128)
    out += rsrc_fork
    if len(rsrc_fork) % 128:
        out += b'\x00' * (128 - len(rsrc_fork) % 128)

    return out

def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <data_fork> <rsrc_fork> <output.bin> [type] [creator]")
        sys.exit(1)

    data_path = sys.argv[1]
    rsrc_path = sys.argv[2]
    out_path = sys.argv[3]
    ftype = sys.argv[4] if len(sys.argv) > 4 else 'APPL'
    creator = sys.argv[5] if len(sys.argv) > 5 else 'nh31'

    with open(data_path, 'rb') as f:
        data_fork = f.read()
    with open(rsrc_path, 'rb') as f:
        rsrc_fork = f.read()

    name = out_path.rsplit('/', 1)[-1]
    if name.endswith('.bin'):
        name = name[:-4]

    macbin = make_macbinary(name, ftype, creator, data_fork, rsrc_fork)

    with open(out_path, 'wb') as f:
        f.write(macbin)

    print(f"Created MacBinary: {out_path}")
    print(f"  Name:     {name}")
    print(f"  Type:     {ftype}")
    print(f"  Creator:  {creator}")
    print(f"  Data:     {len(data_fork)} bytes")
    print(f"  Resource: {len(rsrc_fork)} bytes")
    print(f"  Total:    {len(macbin)} bytes")

if __name__ == '__main__':
    main()
