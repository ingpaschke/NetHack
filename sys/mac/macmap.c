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

/* NetHack color indices to RGB. Values are 16-bit per channel (Mac
   QuickDraw convention; 8-bit values multiplied by 257 for full range). */
#define R16(v) ((unsigned short)((v) * 257))
static const RGBColor gNhColorRGB[16] = {
    {R16(0x00), R16(0x00), R16(0x00)},   /* 0  CLR_BLACK   */
    {R16(0xC0), R16(0x00), R16(0x00)},   /* 1  CLR_RED     */
    {R16(0x00), R16(0x80), R16(0x00)},   /* 2  CLR_GREEN   */
    {R16(0x80), R16(0x80), R16(0x00)},   /* 3  CLR_BROWN   */
    {R16(0x00), R16(0x00), R16(0xC0)},   /* 4  CLR_BLUE    */
    {R16(0x80), R16(0x00), R16(0x80)},   /* 5  CLR_MAGENTA */
    {R16(0x00), R16(0x80), R16(0x80)},   /* 6  CLR_CYAN    */
    {R16(0xC0), R16(0xC0), R16(0xC0)},   /* 7  CLR_GRAY    */
    {R16(0x80), R16(0x80), R16(0x80)},   /* 8  NO_COLOR    */
    {R16(0xFF), R16(0x80), R16(0x00)},   /* 9  CLR_ORANGE  */
    {R16(0x00), R16(0xFF), R16(0x00)},   /* 10 CLR_BRIGHT_GREEN */
    {R16(0xFF), R16(0xFF), R16(0x00)},   /* 11 CLR_YELLOW  */
    {R16(0x00), R16(0x80), R16(0xFF)},   /* 12 CLR_BRIGHT_BLUE */
    {R16(0xFF), R16(0x00), R16(0xFF)},   /* 13 CLR_BRIGHT_MAGENTA */
    {R16(0x00), R16(0xFF), R16(0xFF)},   /* 14 CLR_BRIGHT_CYAN */
    {R16(0xFF), R16(0xFF), R16(0xFF)}    /* 15 CLR_WHITE   */
};

static void
set_nh_color(int color)
{
    if (color < 0 || color >= 16) color = 8;   /* NO_COLOR */
    RGBColor c = gNhColorRGB[color];
    RGBForeColor(&c);
}

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

static void
draw_cell_text(int col, int row, char ch, int color)
{
    if (!gMap.owner || !gMap.owner->its_window) return;
    if (col < 0 || col >= COLNO || row < 0 || row >= ROWNO) return;

    short dx = (col - gMap.scroll_col) * gMap.cell_w;
    short dy = (row - gMap.scroll_row) * gMap.cell_h;
    if (dx < 0 || dy < 0
        || dx >= gMap.vis_cols * gMap.cell_w
        || dy >= gMap.vis_rows * gMap.cell_h) {
        return;   /* off-viewport, cache only */
    }

    GrafPtr saveP; GetPort(&saveP);
    SetPort(gMap.owner->its_window);
    /* Clear the cell with background color before drawing. */
    Rect cell = { dy, dx, dy + gMap.cell_h, dx + gMap.cell_w };
    EraseRect(&cell);
    set_nh_color(color);
    /* Baseline = top + (cell_h - descent). For typical Monaco 9 (cell_h=14,
       descent ~3), this puts the baseline at cell_h - 4 = 10, which leaves
       a 1-pixel descender room below the cell. */
    MoveTo(dx, dy + gMap.cell_h - 4);
    DrawChar(ch);
    SetPort(saveP);
}

void
macmap_print_glyph(NhWindow *map, int x, int y,
                    const glyph_info *gi)
{
    if (!map || gMap.owner != map) return;
    if (!gi) return;
    if (x < 0 || x >= COLNO || y < 0 || y >= ROWNO) return;

    if (gMap.tile_mode) {
        /* Tile mode wired in Phase 4; for now, no-op. */
        return;
    }

    char ch = gi->ttychar;
    int  color = gi->gm.sym.color;
    gMap.text_cache[y][x] = (unsigned char) ch;
    gMap.text_color[y][x] = (unsigned char) color;
    draw_cell_text(x, y, ch, color);
}

void
macmap_update_event(NhWindow *map)
{
    if (!map || gMap.owner != map || !map->its_window) return;

    GrafPtr saveP; GetPort(&saveP);
    SetPort(map->its_window);
    BeginUpdate(map->its_window);

    Rect content;
    GetWindowPortBounds(map->its_window, &content);
    EraseRect(&content);

    /* Re-blit every visible cell from cache. */
    int r, c;
    for (r = gMap.scroll_row; r < gMap.scroll_row + gMap.vis_rows && r < ROWNO; ++r)
        for (c = gMap.scroll_col; c < gMap.scroll_col + gMap.vis_cols && c < COLNO; ++c) {
            char ch  = (char) gMap.text_cache[r][c];
            int  col = (int)  gMap.text_color[r][c];
            if (ch != 0)
                draw_cell_text(c, r, ch, col);
        }

    EndUpdate(map->its_window);
    SetPort(saveP);
}

void
macmap_clear(NhWindow *map)
{
    if (!map || gMap.owner != map) return;
    int r, c;
    for (r = 0; r < ROWNO; ++r)
        for (c = 0; c < COLNO; ++c) {
            gMap.text_cache[r][c] = ' ';
            gMap.text_color[r][c] = 8; /* NO_COLOR */
            gMap.tile_cache[r][c] = 0;
        }
    if (map->its_window) {
        GrafPtr saveP; GetPort(&saveP);
        SetPort(map->its_window);
        Rect content; GetWindowPortBounds(map->its_window, &content);
        EraseRect(&content);
        SetPort(saveP);
    }
}

void    macmap_cliparound(NhWindow *m UNUSED, int x UNUSED, int y UNUSED) { }
void    macmap_grow_event(NhWindow *m UNUSED, long s UNUSED) { }
void    macmap_click(NhWindow *m UNUSED, Point p UNUSED, UInt32 mod UNUSED) { }
void    macmap_pixel_to_cell(NhWindow *m UNUSED, Point p UNUSED,
                              int *c UNUSED, int *r UNUSED) { }
