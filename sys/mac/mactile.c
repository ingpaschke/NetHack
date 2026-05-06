/* mactile.c — runtime tile rendering. See mactile.h. */
#include "hack.h"
#include "macwin.h"
#include "mactile.h"
#include <Gestalt.h>
#include <QDOffscreen.h>
#include <Palettes.h>

Boolean mactile_available(void)             { return false; }
Boolean mactile_init(void)                  { return false; }
void    mactile_shutdown(void)              { }
void    mactile_set_mode(NhWindow *m, Boolean on)
                                            { (void) m; (void) on; }
void    mactile_draw_cell(NhWindow *m, int c, int r, int t)
                                            { (void) m; (void) c; (void) r; (void) t; }
void    mactile_redraw_viewport(NhWindow *m){ (void) m; }
void    mactile_center_on(NhWindow *m, int c, int r)
                                            { (void) m; (void) c; (void) r; }
void    mactile_pixel_to_cell(NhWindow *m, Point p, int *c, int *r)
                                            { (void) m; (void) p; (void) c; (void) r; }
void    mactile_set_player(NhWindow *m, int c, int r)
                                            { (void) m; (void) c; (void) r; }
void    mactile_resize(NhWindow *m)         { (void) m; }
