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

Boolean
macmap_create(NhWindow *map)
{
    if (!map) return false;
    if (gMap.owner) return true;   /* idempotent */

    GDHandle gd = GetMainDevice();
    short screen_h = (*gd)->gdRect.bottom - (*gd)->gdRect.top;
    short wind_id = (screen_h >= 480) ? kWindMapDocument : kWindMapBorderless;

    WindowPtr w = (WindowPtr) GetNewCWindow(wind_id, NULL, (WindowPtr) -1L);
    if (!w) {
        mac_dprintf("macmap: GetNewCWindow(%d) returned NULL\n", (int) wind_id);
        return false;
    }
    SetWRefCon(w, MACMAP_REFCON);
    map->its_window = w;

    gMap.owner       = map;
    gMap.tile_mode   = false;
    gMap.backing     = NULL;
    gMap.palette     = NULL;
    gMap.cell_w      = 6;
    gMap.cell_h      = 14;
    gMap.vis_cols    = 80;
    gMap.vis_rows    = 21;
    gMap.scroll_col  = 0;
    gMap.scroll_row  = 0;

    return true;
}

void
macmap_destroy(NhWindow *map)
{
    if (!map || gMap.owner != map) return;
    if (gMap.backing) { DisposeGWorld(gMap.backing); gMap.backing = NULL; }
    if (gMap.palette) { DisposePalette(gMap.palette); gMap.palette = NULL; }
    if (map->its_window) {
        DisposeWindow(map->its_window);
        map->its_window = NULL;
    }
    gMap.owner = NULL;
}

void
macmap_show(NhWindow *map)
{
    if (map && map->its_window) ShowWindow(map->its_window);
}
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
