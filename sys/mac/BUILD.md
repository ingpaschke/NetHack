# Building NetHack 3.7 for Classic Mac OS (System 7, 68k)

Cross-compilation using [Retro68](https://github.com/autc04/Retro68) GCC
toolchain targeting Motorola 68k Macs in 32-bit addressing mode.

## Prerequisites

### Retro68 Toolchain

Install Retro68 to `/opt/retro68` (or set `RETRO68=` to your path).

**Two patches are required** — see "Retro68 Modifications" below.

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

The build uses `-I/opt/retro68/universal/CIncludes` (via the sys/mac include
path) and `-lInterface`.

### Host Tools

- `hfsutils` — `hformat`, `hmount`, `hcopy`, `hattrib`, `humount`
- `qemu-system-m68k` (QEMU 8.0+) — for testing
- `gdb-multiarch` — for debugging
- Python 3 — for resource fork and disk image tools

---

## Building

### Quick Build

    cd NetHack
    make CROSS_TO_MAC68K=1 all

This produces:
- `targets/mac68k/NetHack` — data fork (tiny text stub)
- `targets/mac68k/.rsrc/NetHack` — resource fork (CODE/DATA/RELA segments)
- `targets/mac68k/NetHack.gdb` — ELF with debug symbols (for GDB)

### Manual Build (when `make` doesn't detect changes)

    cd NetHack/src

    # Compile a single file
    /opt/retro68/bin/m68k-apple-macos-gcc -c -Os \
        -I../include -I../sys/mac -I../lib/lua-5.4.8/src \
        -DMAC -DMAC_CROSS -DNO_TERMS -DNO_SIGNAL -DNO_CHANGE_COLOR \
        -DOPAQUE_TOOLBOX_STRUCTS=0 -DACCESSOR_CALLS_ARE_FUNCTIONS=0 \
        -DCROSSCOMPILE -DCROSSCOMPILE_TARGET -DCROSS_TO_MAC68K \
        -ffunction-sections -fdata-sections \
        -o ../targets/mac68k/<file>.o <file>.c

    # Link (output name MUST be "NetHack", not "NetHack.gdb")
    /opt/retro68/bin/m68k-apple-macos-gcc \
        -Wl,--gc-sections \
        -o ../targets/mac68k/NetHack \
        ../targets/mac68k/*.o \
        ../targets/mac68k/lua548.a \
        ../targets/mac68k/hacklib.a \
        -lm -lInterface

**Important**: The `-o` argument must be `NetHack`, not `NetHack.gdb`.
Elf2Mac (which acts as the linker) creates three files:
- `NetHack` — data fork
- `.rsrc/NetHack` — resource fork with CODE/DATA/RELA resources
- `NetHack.gdb` — ELF binary with debug symbols

If you pass `-o NetHack.gdb`, the resource fork goes to `.rsrc/NetHack.gdb`
and the ELF debug file is lost.

---

## Packaging

### 1. Compile the SIZE resource

    /opt/retro68/bin/Rez sys/mac/nhsize.r -o /tmp/nhsize.rsrc

Rez puts the output resource fork in `/tmp/.rsrc/nhsize.rsrc` (not the
file path you specify — that's the data fork).

### 2. Merge resources into the resource fork

    # Append SIZE resource (preserves CODE/RELA offsets)
    python3 sys/mac/tools/append_rsrc.py \
        targets/mac68k/.rsrc/NetHack \
        /tmp/.rsrc/nhsize.rsrc

    # Append NHrsrc UI resources (menus, windows, dialogs, fonts)
    python3 sys/mac/tools/append_rsrc.py \
        targets/mac68k/.rsrc/NetHack \
        targets/mac68k/resources/NetHack.rsrc.rsrc

**Do NOT use `Rez --copy`** to merge resources. Rez reorganizes the data
section and shifts CODE/DATA/RELA resources from their original offsets,
which causes Bus Errors when the Retro68 runtime tries to load them.
`append_rsrc.py` appends new resources at the END of the data section,
preserving all existing offsets.

### 3. Create MacBinary

    python3 sys/mac/tools/make_macbin.py \
        targets/mac68k/NetHack \
        targets/mac68k/.rsrc/NetHack \
        targets/mac68k/NetHack.bin \
        APPL NHck

Arguments: `<data_fork> <rsrc_fork> <output> [type] [creator]`

### 4. Create HFS disk image

    # Create and format
    dd if=/dev/zero of=/tmp/nethack_hfs.img bs=1M count=14
    hformat -l "NetHack 3.7" /tmp/nethack_hfs.img
    hmount /tmp/nethack_hfs.img

    # Copy NetHack (MacBinary preserves resource fork)
    hcopy -m targets/mac68k/NetHack.bin :

    # Copy data files (raw copy, then set type/creator)
    hcopy -r dat/nhdat :nhdat
    hcopy -r dat/license :license
    hcopy -r sys/mac/NHDeflts :NHDeflts
    hcopy -r /dev/null :record
    hcopy -r dat/symbols :symbols

    hattrib -t DATA -c NHck nhdat
    hattrib -t TEXT -c NHck license
    hattrib -t TEXT -c NHck NHDeflts
    hattrib -t TEXT -c NHck record
    hattrib -t TEXT -c NHck symbols

    humount

### 5. Wrap with Apple Partition Map (for SCSI emulators)

    python3 sys/mac/tools/make_scsi_image2.py \
        /tmp/nethack_hfs.img /tmp/nethack_scsi.img

For BlueSCSI or QEMU, you may need to patch in a SilverLining SCSI driver
at the start of the image (first 96 blocks = 49152 bytes). If you have a
driver backup:

    python3 -c "
    with open('driver_backup.bin', 'rb') as f:
        driver = f.read()
    with open('/tmp/nethack_scsi.img', 'r+b') as f:
        f.write(driver)
    "

**Never recreate a BlueSCSI disk image from scratch** if it has a working
SilverLining driver — use `hmount`/`hcopy`/`humount` to update files in place.

---

## Testing with QEMU

    qemu-system-m68k -M q800 -m 128 \
        -bios "<path-to-quadra-800-rom>" \
        -drive file=pram.img,format=raw,if=mtd \
        -drive file=boot.hda,format=raw,media=disk \
        -drive file=nethack.img,format=raw,media=disk \
        -g 800x600x8 \
        -s \
        -monitor tcp:127.0.0.1:4444,server,nowait

- `-s` enables GDB stub on port 1234
- `-monitor tcp:...` enables QEMU monitor on port 4444
- `pram.img` with `if=mtd` persists PRAM settings (32-bit mode, etc.)
- The boot disk must have System 7.x installed with 32-bit mode enabled
  (Memory control panel → 32-Bit Addressing: On)

### Debugging with GDB

    gdb-multiarch -ex "set architecture m68k" -ex "target remote :1234"

The code includes `volatile int _dbg_spin = 1; while(_dbg_spin);` as a
breakpoint at the start of `main()`. To release:

    # Stop CPU via QEMU monitor first
    echo "stop" | nc 127.0.0.1 4444

    # Then in GDB:
    set *(int*)<spin_var_address> = 0
    detach

Find the spin variable address from the QEMU monitor register dump:
the variable is at `A6 - 20` (frame pointer minus 20).

**Never write to Mac OS low memory (0x0000–0x2000) from GDB.** This
corrupts exception vectors and system globals. Install any traps or
diagnostic code from C code within the application.

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

**Files**: `libretro/relocate.c`, `libretro/MultiSegApp.c` (binary patch)
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

---

## NHrsrc UI Resources

The UI resources (menus, windows, dialogs, fonts, strings) are in
`targets/mac68k/resources/NetHack.rsrc.rsrc`. This was decoded from the
original `NHrsrc.hqx` using `decode_hqx.py`.

To re-decode from scratch:

    python3 sys/mac/tools/decode_hqx.py sys/mac/NHrsrc.hqx /tmp/nhrsrc
    # Resource fork is at /tmp/nhrsrc.rsrc (or /tmp/.rsrc/nhrsrc)
