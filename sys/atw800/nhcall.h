/* nhcall.h - the windowport over t8call: the rows both ends share.
 *
 * One line per function the game calls on the host, name and t8call
 * signature.  t4win.c looks each up by name on first use; every host
 * renderer (gem/rpcgem.c, the toolchain's nhterm.c) registers handlers
 * for the same rows.  The lookup compares signatures, so a one-sided
 * edit fails at first use.
 *
 * Wire names carry an "nh_" prefix.  Argument orders:
 *
 *   glyphrow   win y x0 cells         cells: 9 bytes each, see NHC_CELL
 *   poskey     numpad -> key, x y mod
 *   ynfn       def query choices -> ch
 *   menu_add   win idx accel gaccel attr color ch flags tile str
 *              flags: bit0 preselected, bit1 selectable
 *   menu_sel   win how -> n (-1 cancelled); list of n x (idx, count)
 *   status     fld chg pct color payload   payload: the text, or for
 *              BL_CONDITION (22) 8 raw bytes of condition bits
 *   extlist    first n table            table: n x (idx32 ac32 [len16][name])
 */
#ifndef NHCALL_H
#define NHCALL_H

#define NHCALL_TABLE(X) \
    X(init,       "i:i") \
    X(create,     "i:i") \
    X(clear,      "v:i") \
    X(display,    "v:ii") \
    X(destroy,    "v:i") \
    X(curs,       "v:iii") \
    X(putstr,     "v:iis") \
    X(glyphrow,   "v:iiiV") \
    X(getch,      "i:i") \
    X(poskey,     "i:iHHI") \
    X(ynfn,       "c:iss") \
    X(getlin,     "v:sT256") \
    X(askname,    "v:T32") \
    X(doprev,     "v:") \
    X(menu_start, "v:ii") \
    X(menu_add,   "v:iiiiiiiiis") \
    X(menu_end,   "v:is") \
    X(menu_sel,   "i:iiL") \
    X(exit,       "v:s") \
    X(rawprint,   "v:is") \
    X(status,     "v:iiiiV") \
    X(cliparound, "v:ii") \
    X(extlist,    "v:iiV") \
    X(extcmd,     "i:") \
    X(sync,       "v:") \
    X(bell,       "v:") \
    X(delay,      "v:")

enum nhc_id {
#define NHC_ENUM(name, sig) NHC_##name,
    NHCALL_TABLE(NHC_ENUM)
#undef NHC_ENUM
    NHC_N
};

/* a glyph cell in glyphrow's payload: ch8 color8 flags16 bkch8 tile16
   bktile16, little-endian, tile 0xFFFF = none */
#define NHC_CELL 9

/* window types (mirror of NHW_* so a host needs no NetHack headers) */
#define NHC_W_MESSAGE 1
#define NHC_W_STATUS  2
#define NHC_W_MAP     3
#define NHC_W_MENU    4
#define NHC_W_TEXT    5

/* the host's protocol version, answered by nh_init */
#define NHC_VERSION 5

#endif
