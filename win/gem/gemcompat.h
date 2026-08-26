/* NetHack 3.6.7	gemcompat.h	Atari GEM backport compatibility shim */
/* Bridges the NetHack 5.0 (3.7) GEM windowport onto the 3.6.7 window
 * API.  The 5.0 wingem sources are used verbatim; this header supplies
 * the handful of 3.7-era types, globals and helpers they expect, plus
 * macros that redirect the 5.0-style core menu calls onto 3.6.7's
 * add_menu()/start_menu() ABI.
 *
 * Include order matters: in wingem.c this header is pulled in (via
 * wingem.h) AFTER hack.h, so the real 3.6.7 add_menu()/start_menu()
 * prototypes and NO_GLYPH are already visible when the redirect macros
 * below are defined.
 */
#ifndef GEMCOMPAT_H
#define GEMCOMPAT_H

#include <stdint.h>

/* 3.7 renamed xchar map coordinates to coordxy.  wingem1.c also
 * typedefs this identically; C11 permits the repeat as long as the
 * underlying type matches. */
typedef int16_t coordxy;

/* --- glyph_info and friends (new in 3.7, absent from 3.6.7 wintype.h) ---
 * wingem only ever reads: glyph, ttychar, gm.sym.color, gm.tileidx. */
struct gem_classic_representation {
    int color;
    int symidx;
};
typedef struct gem_glyph_map_entry {
    unsigned glyphflags;
    struct gem_classic_representation sym;
    short int tileidx;
} glyph_map;
typedef struct gem_glyphinfo {
    int glyph;    /* the display entity */
    int ttychar;
    unsigned framecolor;
    glyph_map gm;
} glyph_info;

/* ctrl_nhwindow is a 3.7-only windowproc; 3.6.7 has no such slot.  The
 * 5.0 wingem.c still defines Gem_ctrl_nhwindow(), so give it a type to
 * return.  It is never installed in the 3.6.7 Gem_procs table. */
typedef struct gem_win_request_info {
    int dummy;
} win_request_info;

/* 3.7 menu item flag bits used by the 5.0 wingem sources. */
#ifndef MENU_ITEMFLAGS_NONE
#define MENU_ITEMFLAGS_NONE 0U
#endif
#ifndef MENU_ITEMFLAGS_SELECTED
#define MENU_ITEMFLAGS_SELECTED 0x01U
#endif

/* 3.7 start_menu() behaviour flags; 3.6.7 start_menu() takes no flags. */
#ifndef MENU_BEHAVE_STANDARD
#define MENU_BEHAVE_STANDARD 0UL
#endif

/* Provided (defined) in wingem.c, where hack.h exposes mapglyph()/
 * glyph2tile[]/NO_GLYPH. */
extern glyph_info nul_glyphinfo;
extern void map_glyphinfo(coordxy, coordxy, int, unsigned, glyph_info *);

/* 3.7 set_wc_option_mod_status() status names -> 3.6.7 constants (from
 * hack.h; this header is included after hack.h in wingem.c). */
#ifndef set_gameview
#define set_gameview DISP_IN_GAME
#endif
#ifndef set_in_game
#define set_in_game SET_IN_GAME
#endif

/* 3.7 extended-command match flags.  extcmds_match() is provided in
 * wingem.c over 3.6.7's extcmdlist[].  3.6.7 has no INTERNALCMD flag, so
 * filtering by it is a harmless no-op (no entry sets it). */
#ifndef ECM_NOFLAGS
#define ECM_NOFLAGS    0x00
#endif
#ifndef ECM_IGNOREAC
#define ECM_IGNOREAC   0x01
#endif
#ifndef ECM_EXACTMATCH
#define ECM_EXACTMATCH 0x02
#endif
#ifndef INTERNALCMD
#define INTERNALCMD    0x0040
#endif

/* --- core menu-call ABI redirects ---------------------------------------
 * The 5.0 wingem.c builds its player-selection menus with 3.7-signature
 * calls:
 *   start_menu(win, flags)
 *   add_menu(win, glyph_info*, any, ch, gch, attr, color, str, itemflags)
 * Rewrite them to the 3.6.7 core ABI:
 *   start_menu(win)
 *   add_menu(win, int glyph, any, ch, gch, attr, str, boolean preselected)
 * Parenthesising the real function names blocks recursive macro
 * expansion so the genuine core routines are called. */
#define GEM_GI_GLYPH(giptr) \
    ((giptr) ? ((const glyph_info *) (giptr))->glyph : NO_GLYPH)

/* In 3.6.7 start_menu/add_menu are themselves object-like macros that
 * expand to (*windowprocs.win_*); dispatch through the windowproc
 * pointer directly so we control the argument list (3.6.7 ABI: no
 * per-item colour, a boolean preselect instead of itemflags). */
#undef start_menu
#undef add_menu
#define start_menu(win, flags) (*windowprocs.win_start_menu)((win))

#define add_menu(win, gi, any, ch, gch, attr, clr, str, itemflags)         \
    (*windowprocs.win_add_menu)((win), GEM_GI_GLYPH(gi), (any), (ch),      \
                                (gch), (attr), (str),                      \
                                ((itemflags) & MENU_ITEMFLAGS_SELECTED)    \
                                    ? TRUE : FALSE)

#endif /* GEMCOMPAT_H */
