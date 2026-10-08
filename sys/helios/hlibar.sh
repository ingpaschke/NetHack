#!/bin/sh
# hlibar.sh -- the part of `ar` the NetHack build needs, for Helios.
#
#   sh hlibar.sh r[cusS...] lib obj...
#
# A Helios scanned library (what the Helios linker `asm -l<file>` reads)
# is its object modules concatenated.  The member list is kept beside the
# library in <lib>.members, so `r` replaces a member instead of adding a
# second copy, and the library is rebuilt from the list each time.
set -e
mode=$1; lib=$2; shift 2
case $mode in
  *r*) ;;
  *) echo "hlibar.sh: only r is supported (got $mode)" >&2; exit 2 ;;
esac
list=$lib.members
[ -f "$lib" ] || : > "$list"
touch "$list"
for o in "$@"; do
  grep -qxF "$o" "$list" || echo "$o" >> "$list"
done
tmp=$lib.tmp
: > "$tmp"
while read -r o; do cat "$o" >> "$tmp"; done < "$list"
mv "$tmp" "$lib"
