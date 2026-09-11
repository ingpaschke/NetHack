/* NetHack 5.0	atw800conf.h	$NHDT-Date$  $NHDT-Branch: NetHack-5.0 $ */
/* Copyright (c) 2026. */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Configuration for the Atari Transputer Workstation (ATW800/2) port.
 *
 * NetHack runs on the card's transputer, cross-built with the LLVM t800
 * backend on the PC/TOS (pcconf.h) config base.  Rendering and input go
 * to the host Atari ST through the windowport RPC (sys/atw800/t4win.c);
 * data, save and bones files are served by the ST over the same link.
 * Included from global.h after the base config when ATW800 is defined.
 */

#ifndef ATW800CONF_H
#define ATW800CONF_H

/* the ST host owns all I/O */
#ifndef NOTTYGRAPHICS
#define NOTTYGRAPHICS
#endif

/* no shell or maildemon on the transputer */
#undef SHELL
#undef MAIL

/* one flat directory served over the link; FAT has no .nethackrc and a
   boot image has no exepath */
#undef EXEPATH
#undef HACKDIR
#define HACKDIR "."
#undef CONFIG_FILE
#define CONFIG_FILE "nethack.cnf"

/* colours render on the GEM host */
#define TEXTCOLOR
#define MENUCOLORS

/* dat/ packed into one nhdat: only nhdat, config, sysconf, record and
   the save/level/bones files are opened on the FAT host */
#define DLB
#define DLBFILE "nhdat"

/* 16KB stdio buffer for level/save files; each flush is a link round trip */
#define SFSTRUCT_BUFFERING

/* wins the #ifndef PORT_ID block in global.h */
#ifdef PORT_ID
#undef PORT_ID
#endif
#define PORT_ID "ATW800"

/* pcmain.c gets these from <direct.h> on DOS; libt800 provides them */
char *getcwd(char *, long);
int chdir(const char *);

/* open() mode bits used by files.c; the link file protocol ignores them */
#ifndef S_IREAD
#define S_IREAD  0400
#endif
#ifndef S_IWRITE
#define S_IWRITE 0200
#endif

#endif /* ATW800CONF_H */
