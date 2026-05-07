/* macmap.c — separate map window for the Mac 68k port. See macmap.h. */
#include "hack.h"
#include "macwin.h"
#include "macmap.h"
#include "mactile.h"
#include <Resources.h>
#include <QDOffscreen.h>
#include <Palettes.h>

typedef struct {
    NhWindow      *owner;
    Boolean        tile_mode;
    GWorldPtr      backing;
    PaletteHandle  palette;
    short          cell_w, cell_h;
    short          vis_cols, vis_rows;
    short          scroll_col, scroll_row;
    short          tile_cache[ROWNO][COLNO];
    unsigned char  text_cache[ROWNO][COLNO];
    unsigned char  text_color[ROWNO][COLNO];
    short          saved_text_w, saved_text_h;
    short          saved_tile_w, saved_tile_h;
    Point          saved_position;
} MacMapState;

static MacMapState gMap = {0};

Boolean macmap_create(NhWindow *map UNUSED)             { return false; }
void    macmap_destroy(NhWindow *map UNUSED)            { }
void    macmap_show(NhWindow *map UNUSED)               { }
Boolean macmap_set_mode(NhWindow *m UNUSED, Boolean t UNUSED) { return false; }
Boolean macmap_get_mode(NhWindow *m UNUSED)             { return false; }
void    macmap_print_glyph(NhWindow *m UNUSED, int x UNUSED, int y UNUSED,
                            const struct glyph_info *gi UNUSED) { }
void    macmap_clear(NhWindow *m UNUSED)                { }
void    macmap_cliparound(NhWindow *m UNUSED, int x UNUSED, int y UNUSED) { }
void    macmap_update_event(NhWindow *m UNUSED)         { }
void    macmap_grow_event(NhWindow *m UNUSED, long s UNUSED) { }
void    macmap_click(NhWindow *m UNUSED, Point p UNUSED, UInt32 mod UNUSED) { }
void    macmap_pixel_to_cell(NhWindow *m UNUSED, Point p UNUSED,
                              int *c UNUSED, int *r UNUSED) { }
