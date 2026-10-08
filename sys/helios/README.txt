NetHack 5.0 for Helios (the transputer operating system)
=========================================================

This is NetHack 5.0 (the 3.7 line with Lua) as a Helios program with the
tty interface: it runs in a Helios terminal (the console window of the
I/O Server, or an xterm under Helios X) and draws with termcap.  It is a
plain Helios program: no Atari-side host program is needed.

Built with clang's Helios mode (--target=t800-unknown-helios) and linked
by the Helios assembler/linker against the HEAD (Helios-NG, "1.31") C
library, Posix, FpLib and termcap.lib.  Code is generic transputer code:
the same program runs on a T425 (floating point through FpLib's software
routines) and on a T800/T805 (FpLib uses the FPU).

Files (copy LOCAL into the Helios tree, i.e. next to BIN, ETC, LIB):

  LOCAL/GAMES/NETHACK/NETHACK      the program
  LOCAL/GAMES/NETHACK/NHDAT        game data (dungeon, levels, texts)
  LOCAL/GAMES/NETHACK/SYSCONF      system options (wizard mode, players)
  LOCAL/GAMES/NETHACK/NETHACK.CNF  default user options
  LOCAL/GAMES/NETHACK/SYMBOLS      symbol sets
  LOCAL/GAMES/NETHACK/LICENSE      NetHack General Public License
  LOCAL/GAMES/NETHACK/PERM, RECORD, LOGFILE, XLOGFILE  (empty, must exist)
  LOCAL/GAMES/NETHACK/SAVE/        saved games
  README.TXT                       this file

The playground is fixed at /helios/local/games/nethack (compiled in);
NETHACKDIR=<dir> in the environment or `nethack -d <dir>` selects
another one.  The directory and its files must be writable.

All files NetHack creates have 8.3 names, for the GEMDOS file system
behind the Atari I/O Server: save/0<name> (the first 7 letters of the
name), alock.<n> level and lock files (MAXPLAYERS=4 in SYSCONF),
perm.lck, record.lck, logfile.lck, xlogfile.lck while a score is written,
bones files bon<x>.<n>, paniclog.  Names are opened in lower case;
GEMDOS does not care.

Running
-------

From the Helios shell (console window, 80x25):

  % nethack                     if /helios/local/games/nethack is on PATH
  % /helios/local/games/nethack/nethack
  % nethack -u Ingo             skip the "Who are you?" question
  % nethack -s                  list the high scores
  % nethack -D                  wizard (debug) mode; root only (SYSCONF)

TERM: the console window sets TERM=ansi, whose termcap entry ("helios
ansi window") works.  In an xterm under Helios X use TERM=xterm (both
entries are in /helios/etc/termcap):

  % setenv TERM xterm

NetHack needs cursor addressing (cm) and clear-to-end-of-line (ce); the
vt100, xterm, ansi and csi entries have them.  There is no colour
capability in those entries, so the map is monochrome; the status lines
are below the map, 24 lines are enough, the console's 25 are fine.

Options: NetHack reads $HOME/nethack.cnf (e.g. /helios/users/root/
nethack.cnf) if it exists, otherwise NETHACK.CNF in the playground.
The shipped file turns the new-player tutorial off; delete that line to
get the tutorial question.

Keys: move with h/j/k/l/y/u/b/n (number_pad off).

Memory
------

The program image is 2.9 MB (code; the Helios loader keeps it in
memory), its static data 0.6 MB, its stack 128 KB, and NetHack's heap
grows to between 0.3 and 0.8 MB depending on the level.  Measured on the
t4 emulator with the same Helios HEAD tree and a 5 MB T425 root:
4.7 MB free after boot; while playing, at least 0.99 MB stayed free (see
NH_MEMSTAT below).

  /00 (T425, 5 MB): fits.  Run it from the console window (no X):
      the X server takes too much of /00's memory.
  /01 (T805, 4 MB): does NOT fit.  With 3.67 MB free after boot the
      program fails at start ("Memory allocation failure").  It would
      need about 0.6 MB less code (or data).  So: do not use
      `remote 01 nethack`.

With NH_MEMSTAT set (setenv NH_MEMSTAT 1) the game prints at exit the
largest size the C heap reached and the least free system memory seen.

Not available on Helios
-----------------------

  - shell escape (!) and job control (^Z): Helios has no fork.
  - mail, compressed save files, crash reports and panic back traces.
  - colour (the termcap entries have none).
  - file locking is "create <file>.lck exclusively": if the I/O Server
    does not honour O_EXCL, two games at once could clash.  A game that
    was killed leaves alock.<n> files: delete them (or recover the game
    with NetHack's own self-recovery, which runs at the next start).

Building
--------

On a Linux host with the llvm-t800 toolchain (clang with Helios mode),
the 64-bit Helios assembler (helios-asm) and a Helios tree:

  git worktree / checkout of the NetHack branch helios-port
  sh sys/unix/setup.sh sys/unix/hints/linux.501
  make CROSS_TO_HELIOS=1 fetch-regex
  make CROSS_TO_HELIOS=1 all          host tools, nhdat, targets/helios/nethack
  make CROSS_TO_HELIOS=1 package      LOCAL/... tree in targets/helios/pkg

Variables (make command line): LLVMBIN, HELIOS_ROOT, HELIOS_ASM,
HELIOS_RTLIB, HELIOS_HACKDIR, HELIOS_OPT (-Os; -Oz is 76 KB smaller and
slower), HELIOS_STACK, HELIOS_HEAP.  See sys/unix/hints/include/
cross-pre2.501 (CROSS_TO_HELIOS).
