/* mactile.h — runtime tile rendering for the Mac 68k port. */
#ifndef MACTILE_H
#define MACTILE_H

#include "macwin.h"

extern Boolean mactile_available(void);            /* depth >= 4bpp */
extern Boolean mactile_init(void);                 /* load PICTs into GWorlds */
extern void    mactile_shutdown(void);
extern void    mactile_set_mode(NhWindow *map, Boolean on);
extern void    mactile_draw_cell(NhWindow *map, int col, int row,
                                 int tileidx);
extern void    mactile_redraw_viewport(NhWindow *map);
extern void    mactile_center_on(NhWindow *map, int col, int row);
extern void    mactile_pixel_to_cell(NhWindow *map, Point pt,
                                     int *col, int *row);
extern void    mactile_set_player(NhWindow *map, int col, int row);
extern void    mactile_resize(NhWindow *map);

#endif /* MACTILE_H */
