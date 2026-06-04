/* NetHack 5.0	macconf.h	$NHDT-Date: 1432512782 2015/05/25 00:13:02 $  $NHDT-Branch: master $:$NHDT-Revision: 1.12 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Kevin Hugo, 2004. */
/* NetHack may be freely redistributed.  See license for details. */

#ifdef MACOS9
#ifndef MACCONF_H
#define MACCONF_H

/*
 * Compiler selection is based on the following symbols:
 *
 *  __GNUC__        Retro68 GCC cross-compiler
 *  __SC__          sc, a MPW 68k compiler
 *  __MRC__         mrc, a MPW PowerPC compiler
 *  THINK_C         Think C compiler
 *  __MWERKS__      Metrowerks' Codewarrior compiler
 */

#ifndef __powerc
#define MAC68K /* 68K mac (non-powerpc) */
#endif

/* No system-wide config file on classic Mac OS */
#undef STATUS_HILITES  /* Mac port doesn't support terminal-based hilites;
                          with it defined, WIN_STATUS is never displayed */

/* Lua: use 32-bit integers and 32-bit floats.
   Default 64-bit types are emulated in software on 68k and extremely slow. */
#define LUA_32BITS
#ifndef TARGET_API_MAC_OS8
#define TARGET_API_MAC_OS8 1
#endif
#ifndef TARGET_API_MAC_CARBON
#define TARGET_API_MAC_CARBON 0
#endif
/* Use classic (non-opaque) toolbox structs and direct field access */
#ifndef OPAQUE_TOOLBOX_STRUCTS
#define OPAQUE_TOOLBOX_STRUCTS 0
#endif
#ifndef ACCESSOR_CALLS_ARE_FUNCTIONS
#define ACCESSOR_CALLS_ARE_FUNCTIONS 0
#endif

#if defined(__GNUC__) && defined(CROSSCOMPILE)
/* Retro68 GCC cross-compiler — random() is provided */
#else
#ifndef __MACH__
#define RANDOM
#endif
#endif
#define NO_SIGNAL /* You wouldn't believe our signals ... */
#define FILENAME 256
#define NO_TERMS /* For tty port (see wintty.h) */
#ifndef NO_CHANGE_COLOR
#define CHANGE_COLOR
#endif

/* Use these two includes instead of system.h. */
#include <string.h>
#include <stdlib.h>

/* Uncomment this line if your headers don't already define off_t */
/*typedef long off_t;*/
#include <time.h> /* for time_t */

/*
 * Try and keep the number of files here to an ABSOLUTE minimum !
 * include the relevant files in the relevant .c files instead !
 */
#if TARGET_API_MAC_CARBON
# ifdef __GNUC__
#  define __FP__
#  include <Carbon/Carbon.h>
# else
#  define __FENV__
#  include <machine/types.h>
#  include <Carbon.h>
# endif
#else
# include <MacTypes.h>
#endif

/*
 * We could use the PSN under sys 7 here ...
 * ...but it wouldn't matter...
 */
#define getpid() 1
#define getuid() 1
#define index strchr
#define rindex strrchr

#define Rand random
extern void error(const char *, ...);
/* macwin.c; called from options.c under #ifdef MACOS9 */
extern short set_font_name(int, char *);
/* macmain.c; called from options.c (other ports declare these in their
   *conf.h as well) */
extern boolean authorize_wizard_mode(void);
extern boolean authorize_explore_mode(void);

#if !defined(O_WRONLY)
#if defined(__MWERKS__) && !TARGET_API_MAC_CARBON
#include <unix.h>
#endif
#include <fcntl.h>
#endif

/*
 * Don't redefine these Unix IO functions when making LevComp or DgnComp for
 * MPW.  With MPW, we make them into MPW tools, which use unix IO.  SPEC_LEV
 * and DGN_COMP are defined when compiling for LevComp and DgnComp
 * respectively.
 */
#if !((defined(__SC__) || defined(__MRC__) || defined(__MACH__)) \
      && (defined(SPEC_LEV) || defined(DGN_COMP)))
#ifndef O_BINARY
#define O_BINARY 0
#endif
/* implemented in sys/mac/macunix.c */
extern void regularize(char *);

/* implemented in sys/mac/macfile.c */
extern int maccreat(const char *, long);
extern int macopen(const char *, int, long);
extern int macclose(int);
extern int macread(int, void *, unsigned);
extern int macwrite(int, void *, unsigned);
extern long macseek(int, long, short);
extern int macunlink(const char *);
#define creat maccreat
#define open macopen
#define close macclose
#define read macread
#define write macwrite
#define lseek macseek
#ifdef __MWERKS__
#define unlink _unlink
#endif
#endif

#define YY_NEVER_INTERACTIVE 1

#define TEXT_TYPE 'TEXT'
#define LEVL_TYPE 'LEVL'
#define BONE_TYPE 'BONE'
#define SAVE_TYPE 'SAVE'
#define PREF_TYPE 'PREF'
#define DATA_TYPE 'DATA'
#define MAC_CREATOR 'nh37'  /* this port's creator code; existing saves/disks use it */
#define TEXT_CREATOR 'ttxt' /* Something the user can actually edit */

/*
 * Define PORT_HELP to be the name of the port-specfic help file.
 * This file is included into the resource fork of the application.
 */
#define PORT_HELP "MacHelp"

#define MAC_GRAPHICS_ENV

#endif /* ! MACCONF_H */
#endif /* MAC */
