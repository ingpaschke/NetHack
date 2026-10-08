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

  LOCAL/GAMES/NETHACK/NETHACK      the program (default build)
  LOCAL/GAMES/NETHACK/NETHACKS     the program, small build for 4 MB
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

Memory and the two builds
-------------------------

There are two builds of the same program.  The package holds both:

  NETHACK   default build: each source file compiled to its own Helios
            module (-Os), stack overflow checks and procedure names on.
            Image 2.9 MB (2,897,444 bytes).
  NETHACKS  small build (make ... HELIOS_SMALL=1): the whole program as
            one module (all of NetHack and Lua linked as LLVM bitcode,
            everything main does not reach removed, equal strings
            merged), -Oz, no stack checks, no procedure names.  Image
            2.4 MB (2,388,592 bytes).
Both grow the C heap in 8 KB steps (HELIOS_HEAP): with 64 KB steps
NETHACKS ran out on level 1 at 4 MB ("cannot extend to 68 bytes": no
contiguous 64 KB block left); for NETHACK at 5 MB the step size made no
measurable difference.

Both need 0.6 MB of static data and a 128 KB stack on top of the image
(the Helios loader keeps the whole image in memory).  Levels you are not
on live in level files (alock.<n>), not in memory.

Measured on the t4 emulator with the same Helios HEAD tree: a wizard-mode
game that visits levels 1, 5, 2 and 7 by level teleport and saves
(least free system memory during the game, NH_MEMSTAT; the levels are
random, so runs differ by some 10-30 KB):

                                  NETHACK        NETHACKS
  5 MB T425 root (4.7 MB free)    0.64 MB free   1.18 MB free
  4 MB T805 root (3.67 MB free)   does not start 0.16 MB free

  /00 (T425, 5 MB): either build fits.  Prefer the console window: X
      and other servers started on /00 take from the margin (check
      with `mem` before starting).
  /01 (T805, 4 MB): only NETHACKS fits, with about 160 KB to spare;
      NETHACK fails at start ("Memory allocation failure").  Run it
      from the shell on /00 with

        % remote 01 /helios/local/games/nethack/nethacks

      (or put it on /01 with a CDL/domain placement).  Keep /01 free of
      other programs.  Big special levels and a large inventory need
      more memory; if it runs short, save (S) and restart the game.

The trade-offs of NETHACKS:
  - no stack overflow detection: a too deep call chain overwrites
    memory instead of stopping with a stack error (the 128 KB stack
    covers the deepest chains seen, Lua's parser and the level
    generator);
  - no procedure names in the image: Helios debuggers and the stack
    error message show addresses, not function names;
  - -Oz code is somewhat slower than -Os.
Use NETHACK where it fits (the 5 MB /00), NETHACKS on the 4 MB /01.

With NH_MEMSTAT set (setenv NH_MEMSTAT 1) the game prints at exit the
largest size the C heap reached and the least free system memory seen.

Known messages
--------------

"untrustworthy null shkp; level_status.shkready is FALSE" (an
impossible() of NetHack 5.0.1's work-in-progress status, written to
PANICLOG too) can appear when a level with a shop is made.  The Linux
build of the same sources prints it as well; it is harmless.

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

The small build lives in targets/helios-small, beside the default one;
give HELIOS_SMALL=1 on every make command for it:

  make CROSS_TO_HELIOS=1 HELIOS_SMALL=1 all    targets/helios-small/nethack
  make CROSS_TO_HELIOS=1 HELIOS_SMALL=1 package

`package` (either way) puts every build that exists into the one tree
targets/helios/pkg: targets/helios/nethack as NETHACK and
targets/helios-small/nethack as NETHACKS.  So build both, then package.

The small build compiles each file with the same flags as the default
one plus -emit-llvm (and -Oz), links the bitcode with llvm-link, runs
opt (internalize + globaldce, then constmerge) and generates code for
the one module with clang.  It needs a clang from llvm-t800 commit
3665a0f8c357 or later (before it, re-optimising the linked module
failed in the verifier).

Variables (make command line): LLVMBIN, HELIOS_ROOT, HELIOS_ASM,
HELIOS_RTLIB, HELIOS_HACKDIR, HELIOS_OPT (-Os; -Oz is 76 KB smaller and
slower; HELIOS_SMALL uses -Oz), HELIOS_STACK (128 KB), HELIOS_HEAP
(8 KB), LLVM_LINK, OPT.  See sys/unix/hints/include/
cross-pre2.501 (CROSS_TO_HELIOS).
