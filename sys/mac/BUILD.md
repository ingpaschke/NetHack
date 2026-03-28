# Building NetHack 3.7 for Classic Mac OS (System 7, 68k)

Cross-compilation using [Retro68](https://github.com/autc04/Retro68) GCC
toolchain targeting Motorola 68k Macs in 32-bit addressing mode.

## Prerequisites

### Retro68 Toolchain

Install Retro68 to `/opt/retro68` (or set `RETRO68=` to your path).

**Three patches are required** — see "Retro68 Modifications" below.

### Apple Universal Interfaces 3.4

Retro68's bundled "Multiversal" headers are incomplete. Install Apple's
original headers from the MPW-GM (Macintosh Programmer's Workshop Golden
Master) disk image:

    # Mount the MPW-GM image and copy CIncludes
    mkdir -p /opt/retro68/universal/CIncludes
    cp -r <mpw-gm>/Interfaces/CIncludes/* /opt/retro68/universal/CIncludes/

    # Copy the Interface library (Pascal-calling-convention Toolbox glue)
    cp <mpw-gm>/Libraries/Libraries/Interface.o \
       /opt/retro68/m68k-apple-macos/lib/libInterface.a

### Host Tools

- `hfsutils` — `hformat`, `hmount`, `hcopy`, `hattrib`, `humount`, `hmkdir`
- Python 3 — for resource fork and disk image tools
- `qemu-system-m68k` (QEMU 8.0+) — optional, for testing

---

## Building

    cd NetHack
    make CROSS_TO_MAC68K=1 all

This produces:
- `targets/mac68k/NetHack` — data fork (tiny text stub)
- `targets/mac68k/.rsrc/NetHack` — resource fork (CODE/DATA/RELA segments)
- `targets/mac68k/NetHack.gdb` — ELF with debug symbols

## Packaging

    make CROSS_TO_MAC68K=1 mac68kpkg

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

## Retro68 Modifications

Two source changes and one binary rebuild are required.

### 1. Elf2Mac: Add --emit-relocs (CRITICAL)

**File**: `Elf2Mac/Elf2Mac.cc`
**What**: Add `--emit-relocs` to the linker flags passed to `ld`.
**Why**: Without this, Elf2Mac cannot see cross-section relocations in
the linked ELF. The RELA resources it generates are incomplete, causing
Bus Errors at runtime when the Retro68 loader applies relocations in
32-bit mode.

```diff
         args2.push_back("--no-warn-rwx-segments");
+        args2.push_back("--emit-relocs");
         args2.push_back("-o");
```

### 2. Elf2Mac: Handle cross-section JT refs (CRITICAL)

**File**: `Elf2Mac/Section.cc`
**What**: Replace the assertion that fails on cross-section jump table
references with a fallback to `RelocBase::jumptable`.
**Why**: NetHack has function pointers in `.data` that reference functions
in `.code`. The original code asserts that all JT-relative references are
within the same section, which is not true for initialized function pointer
arrays (like `windowprocs`). The fix treats these as regular JT relocations.

```diff
-                            std::cerr << "Invalid ref from " ...
-                            assert(sym.section.get() == this);
+                            /* Cross-section JT ref with addend */
+                            relocBase = RelocBase::jumptable;
```

A combined patch is at `sys/mac/tools/retro68_elf2mac.patch`. Apply with:

    cd /path/to/Retro68
    git apply /path/to/retro68_elf2mac.patch

Then rebuild Elf2Mac:

    cd build
    cmake --build . --target Elf2Mac

### 3. libretrocrt.a: Remove StripAddress24 masking (32-BIT MODE)

**Files**: `libretro/relocate.c`, `libretro/MultiSegApp.c`
**What**: Recompile these two files with `StripAddress24` redefined as an
identity function (no 24-bit address masking).
**Why**: The default `StripAddress24` macro masks addresses to 24 bits
(`addr & 0x00FFFFFF`). In 32-bit addressing mode, this corrupts pointers.
The runtime checks `relocState.hasStripAddr` to decide whether to use
`StripAddress` (32-bit safe) or `StripAddress24` (24-bit only), but the
initial relocation in `Retro68Relocate()` happens before the Gestalt
check, so it always uses `StripAddress24` first.

To rebuild:

    cd /path/to/Retro68/libretro

    # Backup original
    cp /opt/retro68/m68k-apple-macos/lib/libretrocrt.a \
       /opt/retro68/m68k-apple-macos/lib/libretrocrt.a.bak

    # Recompile with StripAddress24 as identity
    /opt/retro68/bin/m68k-apple-macos-gcc -c -Os \
        -I. -I/opt/retro68/universal/CIncludes \
        -DStripAddress24='(x) ((char*)(x))' \
        -o /tmp/relocate.c.obj relocate.c

    /opt/retro68/bin/m68k-apple-macos-gcc -c -Os \
        -I. -I/opt/retro68/universal/CIncludes \
        -DStripAddress24='(x) ((char*)(x))' \
        -o /tmp/MultiSegApp.c.obj MultiSegApp.c

    # Replace in archive
    /opt/retro68/bin/m68k-apple-macos-ar r \
        /opt/retro68/m68k-apple-macos/lib/libretrocrt.a \
        /tmp/relocate.c.obj /tmp/MultiSegApp.c.obj

---

## Tools (sys/mac/tools/)

| Script | Purpose |
|--------|---------|
| `append_rsrc.py` | Merge resources into a resource fork preserving existing offsets |
| `make_macbin.py` | Create MacBinary II files from data + resource forks |
| `make_scsi_image2.py` | Wrap an HFS image with Apple Partition Map for SCSI |
| `decode_hqx.py` | Decode BinHex 4.0 (.hqx) files to data + resource forks |
| `dump_rsrc.py` | Dump resource fork contents (types, IDs, sizes) |
| `verify_rela.py` | Verify RELA relocations by replaying them |
