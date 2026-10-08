#!/bin/sh
# install-test.sh ROOT -- put the built game into a Helios root on a
# case-sensitive host (the t4 test root): lower-case names, as the game
# opens them.  The release package (make heliospkg) uses upper case for a
# GEMDOS host instead.
set -e
ROOT=$1
T=$(cd "$(dirname "$0")/../.." && pwd)
G=$ROOT/local/games/nethack
mkdir -p "$G/save"
cp "$T/targets/helios/nethack" "$G/nethack"
cp "$T/dat/nhdat" "$G/nhdat"
cp "$T/dat/license" "$G/license"
cp "$T/dat/symbols" "$G/symbols"
cp "$T/sys/helios/sysconf" "$G/sysconf"
cp "$T/sys/helios/nethack.cnf" "$G/nethack.cnf"
for f in perm record logfile xlogfile; do [ -f "$G/$f" ] || : > "$G/$f"; done
echo "installed into $G"
