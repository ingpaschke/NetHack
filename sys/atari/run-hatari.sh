#!/bin/sh
# run-hatari.sh -- launch the backported NetHack 3.6.7 GEM build in Hatari.
#
# Defaults to a STOCK Mega ST: 68000 @ 8 MHz, 4 MB RAM, EmuTOS 1.4,
# Hatari extended VDI 640x480 in 16 colours (VGA monitor) so the GEM
# tile interface is usable.  Auto-launches nethack.prg.
#
# Env overrides:
#   HATARI        hatari binary            (default: PATH)
#   EMUTOS        EmuTOS ROM               (default: ~/.cache/emutos/etos512us.img)
#   MACHINE       st/megast/ste/tt/falcon  (default: megast)
#   MEMSIZE       RAM in MiB               (default: 4)
#   CPUCLOCK      MHz 8/16/32              (default: 8)
#   PLANES        VDI depth 1/2/4          (default: 4 = 16 colours; 1 = mono)
#   VDI_W, VDI_H  VDI screen size          (default: 640x480)
#   MONITOR       mono/rgb/vga/tv          (default: vga)
#   FAST          1 = fast-forward boot    (default: 1; unset once in-game)
#   NO_AUTO       1 = don't auto-run prg
#   ZOOM          window scale             (default: 2)
#
# For true stock ST-High mono: PLANES=1 MONITOR=mono VDI_W=640 VDI_H=400 ./run-hatari.sh
set -eu

HD_DIR="$(cd "$(dirname "$0")" && pwd)"
[ -f "$HD_DIR/nethack.prg" ] || {
    echo "nethack.prg not found in $HD_DIR" >&2
    echo "Build it first: make -f sys/atari/Makefile.cross package (or link)." >&2
    exit 1
}

HATARI="${HATARI:-hatari}"
command -v "$HATARI" >/dev/null 2>&1 || {
    echo "hatari not found in PATH (or set HATARI=/path/to/hatari)" >&2
    exit 1
}

EMUTOS="${EMUTOS:-$HOME/.cache/emutos/etos512us.img}"
[ -f "$EMUTOS" ] || {
    echo "EmuTOS ROM not found: $EMUTOS" >&2
    echo "Set EMUTOS=/path/to/etos512us.img (EmuTOS 1.4, 512k US)." >&2
    exit 1
}

MACHINE="${MACHINE:-megast}"
MEMSIZE="${MEMSIZE:-4}"
CPUCLOCK="${CPUCLOCK:-8}"
PLANES="${PLANES:-4}"
VDI_W="${VDI_W:-640}"
VDI_H="${VDI_H:-480}"
MONITOR="${MONITOR:-vga}"
ZOOM="${ZOOM:-2}"

FAST_FLAG=""
[ "${FAST:-1}" = "1" ] && FAST_FLAG="--fast-forward on"

AUTO_FLAG=""
[ -z "${NO_AUTO:-}" ] && AUTO_FLAG="--auto C:\\nethack.prg"

exec "$HATARI" \
    --tos "$EMUTOS" \
    --machine "$MACHINE" --memsize "$MEMSIZE" --cpuclock "$CPUCLOCK" \
    --monitor "$MONITOR" \
    --vdi on --vdi-planes "$PLANES" --vdi-width "$VDI_W" --vdi-height "$VDI_H" \
    --sound off \
    --zoom "$ZOOM" \
    $FAST_FLAG \
    --harddrive "$HD_DIR" \
    $AUTO_FLAG \
    "$@"
