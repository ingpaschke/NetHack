/* pcstub.c - pcsys.c replacements for the ATW800 port.
 *
 * The PC/TOS config base expects these from pcsys.c.  The transputer has
 * no console, disk or shell, so they are stubs here. */
#include "hack.h"

extern long __sys_time(void); /* libt800: seconds since epoch, host clock */

/* no key polling; input comes via the windowport */
int kbhit(void) { return 0; }

/* animation pacing is host-side (NW_DELAY) */
void msleep(unsigned ms) { (void) ms; }

/* the link file protocol is always binary */
int setmode(int fd, int mode) { (void) fd; (void) mode; return 0; }

/* MICRO wants a real nethack_exit (elsewhere it is #defined to exit) */
void nethack_exit(int code) { exit(code); }

/* no shell to spawn */
int dosh(void) { return 0; }

/* seed from the host clock */
unsigned long sys_random_seed(void)
{
    return (unsigned long) __sys_time() ^ 0x9e3779b9UL;
}

/* wizard-mode tile viewer; not applicable */
void tileview(boolean on) { (void) on; }

/* per-game UUID unused on this port */
void get_nhuuid(void) { }
void free_nhuuid(void) { }

/* append the path separator if the name lacks one */
void append_slash(char *name)
{
    char *p;
    if (!*name)
        return;
    p = name + (strlen(name) - 1);
    if (*p != '/' && *p != '\\' && *p != ':') {
        *++p = '/';
        *++p = '\0';
    }
}
