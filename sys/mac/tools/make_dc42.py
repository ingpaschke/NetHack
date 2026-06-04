#!/usr/bin/env python3
"""Create a Disk Copy 4.2 image from a raw HFS disk image.

Usage: make_dc42.py <hfs_image> <output.dsk> [volume_name]
"""
import struct
import sys

def dc42_checksum(data):
    """Compute Disk Copy 4.2 checksum (32-bit rotating add)."""
    cksum = 0
    for i in range(0, len(data), 2):  # data is validated 512-aligned, so always even
        cksum += (data[i] << 8) | data[i + 1]
        cksum = ((cksum >> 1) | (cksum << 31)) & 0xFFFFFFFF
    return cksum

def make_dc42(name, data):
    """Build a Disk Copy 4.2 file."""
    name_bytes = name.encode('mac_roman')[:63]
    data_cksum = dc42_checksum(data)

    header = bytearray(84)
    # Pascal string name
    header[0] = len(name_bytes)
    header[1:1+len(name_bytes)] = name_bytes
    # Data size
    struct.pack_into('>I', header, 64, len(data))
    # Tag size (0)
    struct.pack_into('>I', header, 68, 0)
    # Data checksum
    struct.pack_into('>I', header, 72, data_cksum)
    # Tag checksum
    struct.pack_into('>I', header, 76, 0)
    # Disk format: the spec only defines 0-3 (400K/800K/720K/1440K
    # floppies); this is a hard-disk-sized image, and consumers of
    # non-floppy DC42 files ignore the byte.  5 = "none of those".
    header[80] = 5
    # Format byte: 0x24 = HFS
    header[81] = 0x24
    # Magic: 0x0100
    header[82] = 0x01
    header[83] = 0x00

    return bytes(header) + data

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <hfs_image> <output.dsk> [volume_name]")
        sys.exit(1)

    hfs_path = sys.argv[1]
    out_path = sys.argv[2]
    vol_name = sys.argv[3] if len(sys.argv) > 3 else "NetHack 5.0"

    with open(hfs_path, 'rb') as f:
        data = f.read()

    if len(data) % 512 != 0:
        print(f"Error: HFS image size {len(data)} is not a multiple of 512",
              file=sys.stderr)
        sys.exit(1)

    dc42 = make_dc42(vol_name, data)

    with open(out_path, 'wb') as f:
        f.write(dc42)

    print(f"Created Disk Copy 4.2: {out_path}")
    print(f"  Volume: {vol_name}")
    print(f"  Data:   {len(data)} bytes")
    print(f"  Total:  {len(dc42)} bytes")

if __name__ == '__main__':
    main()
