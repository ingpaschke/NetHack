/* nhmapwind.r — WIND resources for the dedicated map window.
 * 200 = documentProc (resizable, for Quadra+ screens >= 480px tall).
 * 201 = plainDBox    (borderless, fixed, for SE/30 screens <  480px tall).
 *
 * boundsRect coordinates are top, left, bottom, right.
 * Width = 80 cols * 6 px = 480; height_text = 21 rows * 14 px = 294.
 */

#include "Multiverse.r"

resource 'WIND' (200, "Dungeon Map (document)", purgeable) {
    {20, 0, 314, 480},          /* boundsRect: top, left, bottom, right */
    documentProc,               /* WDEF procID = 0 (doc, with grow) */
    invisible,                  /* visible flag — we ShowWindow later */
    goAway,                     /* close box */
    0x0,                        /* refCon */
    "Dungeon Map",
    noAutoCenter
};

resource 'WIND' (201, "Dungeon Map (borderless)", purgeable) {
    {20, 16, 314, 496},         /* SE/30: 480x294 area starting at y=20 */
    plainDBox,                  /* WDEF procID = 2 (no chrome) */
    invisible,
    noGoAway,
    0x0,
    "",
    noAutoCenter
};
