/* NetHack 5.0	heliosconf.h */
/* Copyright (c) 2026 by Ingo Paschke */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Helios (the transputer operating system) on top of unixconf.h.
 *
 * Helios has a Posix library (open/read/stat/termios/signals, no fork
 * and no job control), a termcap library, and Clib, an ANSI C89 library.
 * Its files live on the host's file system through the I/O Server; on
 * the ATW800 that is GEMDOS: 8.3 names, case-insensitive.  Every file
 * NetHack creates is kept 8.3 (see the HELIOS blocks in files.c and
 * unixunix.c).  The build is CROSS_TO_HELIOS in sys/unix/hints.
 */
#ifndef HELIOSCONF_H
#define HELIOSCONF_H

/* no fork/exec worth having, no mail, no job control */
#undef SHELL
#undef MAIL
#undef SUSPEND
#undef POSIX_JOB_CONTROL
#undef COMPRESS
#undef COMPRESS_EXTENSION
#undef DEF_PAGER

/* termcap, not terminfo */
#undef TERMINFO

/* Locking: fcntl() locks and link() are not available on the I/O
   Server's file systems.  Lock by creating <file>.lck exclusively in
   the playground. */
#undef USE_FCNTL
#define NO_FILE_LINKS
#ifndef LOCKDIR
#define LOCKDIR "."
#endif

/* <termios.h>: Helios spells ICANON "ICANNON" and has no speed_t */
#define ICANON ICANNON
typedef int speed_t;

/* keyboard input goes through helios.c, which also keeps the memory
   statistics that NH_MEMSTAT=1 prints at exit */
#undef tgetch
#define tgetch helios_getchar
extern int helios_getchar(void);

/* Clib has no snprintf/vsnprintf; sys/helios/helios.c provides them */
#include <stdarg.h>
#include <stddef.h>
extern int snprintf(char *, size_t, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);

#endif /* HELIOSCONF_H */
