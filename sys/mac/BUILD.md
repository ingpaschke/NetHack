# Building NetHack for Classic Mac OS (System 7, 68k)

Cross-compilation using [Retro68](https://github.com/autc04/Retro68) GCC
toolchain targeting Motorola 68k Macs in 32-bit addressing mode.

## Prerequisites

### Retro68 Toolchain

Install Retro68 to `/opt/retro68` (or set `RETRO68=` to your path).

Three patches are required. Apply `sys/mac/tools/retro68_elf2mac.patch`
to the Retro68 source tree and rebuild Elf2Mac. Additionally, rebuild
`libretrocrt.a` with `StripAddress24` redefined as a no-op for 32-bit
mode — see comments in the patch file for details.

### Apple Universal Interfaces 3.4

Retro68's bundled "Multiversal" headers are incomplete. Install Apple's
original headers from the MPW-GM (Macintosh Programmer's Workshop Golden
Master) disk image:

    # Mount the MPW-GM image and copy CIncludes
    mkdir -p /opt/retro68/universal/CIncludes
    cp -r <mpw-gm>/Interfaces/CIncludes/* /opt/retro68/universal/CIncludes/

    # Wrap the Interface library (Pascal-calling-convention Toolbox glue)
    # in a proper ar archive so -lInterface resolves it
    /opt/retro68/bin/m68k-apple-macos-ar rcs \
       /opt/retro68/m68k-apple-macos/lib/libInterface.a \
       <mpw-gm>/Libraries/Libraries/Interface.o

### Host Tools

- `hfsutils` — `hformat`, `hmount`, `hcopy`, `hattrib`, `humount`, `hmkdir`
- Python 3 — for resource fork and disk image tools
- `qemu-system-m68k` (QEMU 8.0+) — optional, for testing

---

## Building

From a fresh checkout, generate the Makefiles and fetch Lua first:

    cd NetHack
    sys/unix/setup.sh sys/unix/hints/linux.500
    make fetch-lua

Then build:

    make CROSS_TO_MAC68K=1 all

This produces:
- `targets/mac68k/NetHack` — data fork (tiny text stub)
- `targets/mac68k/.rsrc/NetHack` — resource fork (CODE/DATA/RELA segments)
- `targets/mac68k/NetHack.gdb` — ELF with debug symbols

## Packaging

    make -C src CROSS_TO_MAC68K=1 mac68kpkg

(Run from `src/`; the top-level Makefile's generated Lua paths break this
target when invoked from the repository root.)

This runs the full packaging pipeline:
1. Compile SIZE resource with Rez
2. Merge SIZE + NHrsrc UI resources into the resource fork
3. Create MacBinary II file
4. Build HFS disk image with all data files, `save/` and `levels/`
   directories, and `nethack.cnf`
5. Wrap with Apple Partition Map for SCSI emulators

Output:
- `targets/mac68k/NetHack.img` — ready for QEMU or BlueSCSI
- `targets/mac68k/NetHack.bin` — MacBinary for `hcopy -m` to existing disks
- `targets/mac68k/NetHack.dsk` — Disk Copy 4.2 image for real floppies
- `targets/mac68k/NetHack.sit` — StuffIt archive for upload/distribution

### Updating an existing disk (e.g. BlueSCSI)

    hmount /path/to/disk.hda
    hdel NetHack
    hcopy -m targets/mac68k/NetHack.bin :
    humount

**Never recreate a BlueSCSI disk image from scratch** if it has a working
SilverLining driver — use `hmount`/`hcopy`/`humount` to update files in place.

### Resource merging note

**Do NOT use `Rez --copy`** to merge resources. Rez reorganizes the data
section and shifts CODE/DATA/RELA resources from their original offsets,
which causes Bus Errors when the Retro68 runtime tries to load them.
The build uses `append_rsrc.py` which appends new resources at the END
of the data section, preserving all existing offsets.

---

## Testing with QEMU

    qemu-system-m68k -M q800 -m 128 \
        -bios "<path-to-quadra-800-rom>" \
        -drive file=pram.img,format=raw,if=mtd \
        -drive file=boot.hda,format=raw,media=disk \
        -drive file=NetHack.img,format=raw,media=disk \
        -g 800x600x8

- `pram.img` with `if=mtd` persists PRAM settings (32-bit mode, etc.)
- The boot disk must have System 7.x installed with 32-bit mode enabled
  (Memory control panel → 32-Bit Addressing: On)

---

## Tools (sys/mac/tools/)

| Script | Purpose |
|--------|---------|
| `append_rsrc.py` | Merge resources into a resource fork preserving existing offsets |
| `make_macbin.py` | Create MacBinary II files from data + resource forks |
| `make_scsi_image2.py` | Wrap an HFS image with Apple Partition Map for SCSI |
| `make_dc42.py` | Create Disk Copy 4.2 images (`NetHack.dsk` in the packaging step) |
| `decode_hqx.py` | Decode BinHex 4.0 (.hqx) files to data + resource forks |
| `dump_rsrc.py` | Dump resource fork contents (types, IDs, sizes) |
| `verify_rela.py` | Verify RELA relocations by replaying them |

## Historical files

`sys/mac/README`, `Install.mw`, and `NHrsrc.hqx`/`NHsound.hqx` date from the
original 1990s Macintosh port (Metrowerks/MPW).  They are retained for
reference; only this file describes the Retro68 cross-compile.
