/* mactile.h — tile sheet asset + per-tile blit. See macmap.h for the
   map window owner. */
#ifndef MACTILE_H
#define MACTILE_H

#include "macwin.h"
#include <Palettes.h>
#include <QDOffscreen.h>

extern Boolean mactile_available(void);    /* depth >= 4bpp */
extern Boolean mactile_init(void);         /* load PICT 1000/1001 into GWorld */
extern void    mactile_shutdown(void);

/* Where the tilesheet was loaded — needed by macmap to attach a Palette. */
extern short      mactile_sheet_depth(void);
extern CTabHandle mactile_sheet_ctable(void);

/* Blit a single tile into a destination GWorld at (dst_x, dst_y).
   The function manages SetGWorld save/restore internally. */
extern void    mactile_blit_to(GWorldPtr dst,
                                int tile_idx,
                                short dst_x, short dst_y);

/* Blit a single tile into a destination Window at (dst_x, dst_y).
   The function manages SetPort save/restore internally. */
extern void    mactile_blit_to_window(WindowPtr dst,
                                       int tile_idx,
                                       short dst_x, short dst_y);

/* CLUT index of a bright, NON-white tile-palette color (yellow-ish) for the
   farlook cursor.  Use it with PmForeColor(index): that sets the foreground
   straight to that CLUT slot — no RGB, no color matching, no Palette Manager
   render — so it can't recolor the map.  (White is deliberately excluded: the
   white slot reorganizes the CLUT.)  Returns -1 if no sheet/ctable. */
extern short   mactile_cursor_clut_index(void);

#endif /* MACTILE_H */
