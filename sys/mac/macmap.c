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

static Boolean
allocate_backing(void)
{
    if (gMap.backing) {
        DisposeGWorld(gMap.backing);
        gMap.backing = NULL;
    }
    Rect r;
    SetRect(&r, 0, 0,
            gMap.vis_cols * gMap.cell_w,
            gMap.vis_rows * gMap.cell_h);

    short depth;
    if (gMap.tile_mode) {
        depth = mactile_sheet_depth();
        if (depth < 4) depth = 8;
    } else {
        GDHandle gd = GetMainDevice();
        depth = (*(*gd)->gdPMap)->pixelSize;
        if (depth > 8) depth = 8;
        if (depth < 1) depth = 1;
    }

    QDErr err = NewGWorld(&gMap.backing, depth, &r, NULL, NULL, 0);
    if (err != noErr || !gMap.backing) {
        mac_dprintf("macmap: NewGWorld failed err=%d (depth=%d)\n",
                    (int) err, (int) depth);
        gMap.backing = NULL;
        return false;
    }

    /* Erase the backing to its background. */
    GWorldPtr saveW; GDHandle saveD;
    GetGWorld(&saveW, &saveD);
    SetGWorld(gMap.backing, NULL);
    PixMapHandle pm = GetGWorldPixMap(gMap.backing);
    LockPixels(pm);
    EraseRect(&r);
    UnlockPixels(pm);
    SetGWorld(saveW, saveD);
    return true;
}

static void
blit_backing_to_window(const Rect *src_rect, const Rect *dst_rect)
{
    if (!gMap.backing || !gMap.owner || !gMap.owner->its_window) return;
    PixMapHandle pm = GetGWorldPixMap(gMap.backing);
    LockPixels(pm);
    GrafPtr saveP; GetPort(&saveP);
    SetPort(gMap.owner->its_window);
    CopyBits((BitMap *) *pm,
             GetPortBitMapForCopyBits(GetWindowPort(gMap.owner->its_window)),
             src_rect, dst_rect, srcCopy, NULL);
    SetPort(saveP);
    UnlockPixels(pm);
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

    /* Apply saved position and text-mode size from NHDeflts (iflags). */
    {
        Rect screen = (*gd)->gdRect;
        if (iflags.mac_map_pos_x || iflags.mac_map_pos_y) {
            short x = iflags.mac_map_pos_x;
            short y = iflags.mac_map_pos_y;
            if (x < screen.left) x = screen.left;
            if (y < screen.top + 20) y = screen.top + 20;
            if (x > screen.right - 100) x = screen.right - 100;
            if (y > screen.bottom - 50) y = screen.bottom - 50;
            MoveWindow(w, x, y, false);
        }
        if (iflags.mac_map_text_w && iflags.mac_map_text_h) {
            SizeWindow(w, iflags.mac_map_text_w, iflags.mac_map_text_h, false);
        }
    }

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
Boolean
macmap_set_mode(NhWindow *map, Boolean tile_mode)
{
    if (!map || gMap.owner != map) return false;

    if (tile_mode && !mactile_init()) return false;
    gMap.tile_mode = tile_mode;
    map->tile_mode = tile_mode;   /* keep NhWindow field in sync for macwin/mactty */
    if (tile_mode) {
        gMap.cell_w = 16;
        gMap.cell_h = 16;
        gMap.vis_cols = 30;   /* default; resize event will refine */
        gMap.vis_rows = 21;
        /* Restore saved tile-mode window size. */
        if (iflags.mac_map_tile_w && iflags.mac_map_tile_h
            && map->its_window) {
            SizeWindow(map->its_window,
                       iflags.mac_map_tile_w, iflags.mac_map_tile_h, false);
            Rect cr; GetWindowPortBounds(map->its_window, &cr);
            gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
            gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
            if (gMap.vis_cols < 1) gMap.vis_cols = 1;
            if (gMap.vis_rows < 1) gMap.vis_rows = 1;
        }
        if (!allocate_backing()) {
            mac_dprintf("macmap: backing alloc failed in tile mode; using fallback\n");
        }
        /* On 8bpp screens, attach a 32-entry pmTolerant Palette so the
           Palette Manager can allocate close colors without trashing
           reserved system slots. */
        if (mactile_sheet_depth() == 8) {
            if (!gMap.palette) {
                CTabHandle ct = mactile_sheet_ctable();
                if (ct) gMap.palette = NewPalette(32, ct, pmTolerant, 0x1000);
            }
            if (gMap.palette) {
                SetPalette(map->its_window, gMap.palette, true);
                ActivatePalette(map->its_window);
            }
        }
    } else {
        if (gMap.owner) {
            gMap.cell_w = gMap.owner->char_width;
            gMap.cell_h = gMap.owner->row_height;
            if (gMap.cell_w < 1) gMap.cell_w = 6;
            if (gMap.cell_h < 1) gMap.cell_h = 14;
        }
        gMap.vis_cols = 80;
        gMap.vis_rows = 21;
        /* Restore saved text-mode window size. */
        if (iflags.mac_map_text_w && iflags.mac_map_text_h
            && map->its_window) {
            SizeWindow(map->its_window,
                       iflags.mac_map_text_w, iflags.mac_map_text_h, false);
            Rect cr; GetWindowPortBounds(map->its_window, &cr);
            gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
            gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
            if (gMap.vis_cols < 1) gMap.vis_cols = 1;
            if (gMap.vis_rows < 1) gMap.vis_rows = 1;
        }
        /* Detach + dispose the palette so a future re-enable rebuilds it. */
        if (gMap.palette) {
            SetPalette(map->its_window, NULL, false);
            DisposePalette(gMap.palette);
            gMap.palette = NULL;
        }
        if (!allocate_backing()) {
            mac_dprintf("macmap: backing alloc failed in text mode; using fallback\n");
        }
    }
    /* Force a full redraw via update event. */
    if (map->its_window) InvalRect(&(*map->its_window).portRect);
    return true;
}

Boolean
macmap_get_mode(NhWindow *map)
{
    return (map && gMap.owner == map) ? gMap.tile_mode : false;
}

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

    if (gMap.backing) {
        /* Paint into backing first, then blit cell to window. */
        PixMapHandle pm = GetGWorldPixMap(gMap.backing);
        LockPixels(pm);
        GWorldPtr saveW; GDHandle saveD;
        GetGWorld(&saveW, &saveD);
        SetGWorld(gMap.backing, NULL);

        Rect cell = { dy, dx, dy + gMap.cell_h, dx + gMap.cell_w };
        EraseRect(&cell);
        set_nh_color(color);
        MoveTo(dx, dy + gMap.cell_h - 4);
        DrawChar(ch);

        SetGWorld(saveW, saveD);
        UnlockPixels(pm);
        blit_backing_to_window(&cell, &cell);
    } else {
        /* Fallback: direct to window (slower). */
        GrafPtr saveP; GetPort(&saveP);
        SetPort(gMap.owner->its_window);
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
}

static void
draw_cell_tile(int col, int row, int tile_idx)
{
    if (!gMap.owner || !gMap.owner->its_window) return;
    if (col < 0 || col >= COLNO || row < 0 || row >= ROWNO) return;

    short dx = (col - gMap.scroll_col) * gMap.cell_w;
    short dy = (row - gMap.scroll_row) * gMap.cell_h;
    if (dx < 0 || dy < 0
        || dx >= gMap.vis_cols * gMap.cell_w
        || dy >= gMap.vis_rows * gMap.cell_h) {
        return;   /* off-viewport, cached only */
    }

    if (gMap.backing) {
        mactile_blit_to(gMap.backing, tile_idx, dx, dy);
        Rect cell = { dy, dx, dy + 16, dx + 16 };
        blit_backing_to_window(&cell, &cell);
    } else {
        mactile_blit_to_window(gMap.owner->its_window, tile_idx, dx, dy);
    }
}

void
macmap_print_glyph(NhWindow *map, int x, int y,
                    const glyph_info *gi)
{
    if (!map || gMap.owner != map) return;
    if (!gi) return;
    if (x < 0 || x >= COLNO || y < 0 || y >= ROWNO) return;

    if (gMap.tile_mode) {
        int idx = gi->gm.tileidx;
        gMap.tile_cache[y][x] = (short) idx;
        draw_cell_tile(x, y, idx);
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

    if (gMap.backing) {
        Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
        /* Source = backing bbox. Dest = same dims at window content top-left. */
        Rect dst = bbox;
        blit_backing_to_window(&bbox, &dst);
    } else {
        /* Fallback: cache-replay redraw. */
        Rect content; GetWindowPortBounds(map->its_window, &content);
        EraseRect(&content);
        int r, c;
        for (r = gMap.scroll_row; r < gMap.scroll_row + gMap.vis_rows && r < ROWNO; ++r)
            for (c = gMap.scroll_col; c < gMap.scroll_col + gMap.vis_cols && c < COLNO; ++c) {
                if (gMap.tile_mode) {
                    short idx = gMap.tile_cache[r][c];
                    if (idx) draw_cell_tile(c, r, (int) idx);
                } else {
                    char ch  = (char) gMap.text_cache[r][c];
                    int  col = (int)  gMap.text_color[r][c];
                    if (ch != 0) draw_cell_text(c, r, ch, col);
                }
            }
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
    /* Clear the backing too — otherwise the next damage event blits stale
       pixels back onto the window. */
    if (gMap.backing) {
        PixMapHandle pm = GetGWorldPixMap(gMap.backing);
        LockPixels(pm);
        GWorldPtr saveW; GDHandle saveD;
        GetGWorld(&saveW, &saveD);
        SetGWorld(gMap.backing, NULL);
        Rect bb; GetPortBounds((CGrafPtr) gMap.backing, &bb);
        EraseRect(&bb);
        SetGWorld(saveW, saveD);
        UnlockPixels(pm);
    }
}

#define MT_EDGE_MARGIN 3

static void
recompute_scroll_for_center(int x, int y, short *new_col, short *new_row)
{
    short c = (short) x - gMap.vis_cols / 2;
    short r = (short) y - gMap.vis_rows / 2;
    if (c < 0) c = 0;
    if (r < 0) r = 0;
    if (c + gMap.vis_cols > COLNO) c = COLNO - gMap.vis_cols;
    if (r + gMap.vis_rows > ROWNO) r = ROWNO - gMap.vis_rows;
    if (c < 0) c = 0;
    if (r < 0) r = 0;
    *new_col = c;
    *new_row = r;
}

static void
repaint_full_viewport(void)
{
    if (!gMap.owner) return;
    if (gMap.backing) {
        Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
        GWorldPtr saveW; GDHandle saveD;
        GetGWorld(&saveW, &saveD);
        SetGWorld(gMap.backing, NULL);
        EraseRect(&bbox);
        SetGWorld(saveW, saveD);
    }
    int r, c;
    for (r = gMap.scroll_row; r < gMap.scroll_row + gMap.vis_rows && r < ROWNO; ++r)
        for (c = gMap.scroll_col; c < gMap.scroll_col + gMap.vis_cols && c < COLNO; ++c) {
            if (gMap.tile_mode) {
                short idx = gMap.tile_cache[r][c];
                if (idx) draw_cell_tile(c, r, (int) idx);
            } else {
                char ch  = (char) gMap.text_cache[r][c];
                int  col = (int)  gMap.text_color[r][c];
                if (ch != 0) draw_cell_text(c, r, ch, col);
            }
        }
    /* Final viewport blit if we're using backing. */
    if (gMap.backing && gMap.owner->its_window) {
        Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
        blit_backing_to_window(&bbox, &bbox);
    }
}

static void
backing_self_scroll(int dx_cells, int dy_cells)
{
    if (!gMap.backing) return;
    PixMapHandle pm = GetGWorldPixMap(gMap.backing);
    LockPixels(pm);
    GWorldPtr saveW; GDHandle saveD;
    GetGWorld(&saveW, &saveD);
    SetGWorld(gMap.backing, NULL);

    Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
    Rect src = bbox, dst = bbox;
    OffsetRect(&dst, (short)(-dx_cells * gMap.cell_w), (short)(-dy_cells * gMap.cell_h));
    /* CopyBits same-port src/dst handles overlap correctly. */
    CopyBits((BitMap *) *pm, (BitMap *) *pm, &src, &dst, srcCopy, NULL);

    SetGWorld(saveW, saveD);
    UnlockPixels(pm);
}

static void
repaint_strip(int col_start, int row_start, int col_end, int row_end)
{
    int r, c;
    if (col_start < 0) col_start = 0;
    if (row_start < 0) row_start = 0;
    if (col_end > COLNO) col_end = COLNO;
    if (row_end > ROWNO) row_end = ROWNO;
    for (r = row_start; r < row_end; ++r)
        for (c = col_start; c < col_end; ++c) {
            if (gMap.tile_mode) {
                short idx = gMap.tile_cache[r][c];
                if (idx) draw_cell_tile(c, r, (int) idx);
            } else {
                char ch  = (char) gMap.text_cache[r][c];
                int  col = (int)  gMap.text_color[r][c];
                if (ch != 0) draw_cell_text(c, r, ch, col);
            }
        }
}

void
macmap_cliparound(NhWindow *map, int x, int y)
{
    if (!map || gMap.owner != map) return;

    /* Don't scroll unless hero is within edge margin. */
    short hero_in_view_x = (short) x - gMap.scroll_col;
    short hero_in_view_y = (short) y - gMap.scroll_row;
    if (hero_in_view_x >= MT_EDGE_MARGIN
        && hero_in_view_x <  gMap.vis_cols - MT_EDGE_MARGIN
        && hero_in_view_y >= MT_EDGE_MARGIN
        && hero_in_view_y <  gMap.vis_rows - MT_EDGE_MARGIN) {
        return;
    }

    short old_col = gMap.scroll_col, old_row = gMap.scroll_row;
    short new_col, new_row;
    recompute_scroll_for_center(x, y, &new_col, &new_row);
    if (new_col == old_col && new_row == old_row) return;

    short dx = new_col - old_col;
    short dy = new_row - old_row;

    gMap.scroll_col = new_col;
    gMap.scroll_row = new_row;

    /* Big-jump path or no-backing fallback: full redraw. */
    if (!gMap.backing
        || abs(dx) > gMap.vis_cols / 2 || abs(dy) > gMap.vis_rows / 2) {
        repaint_full_viewport();
        return;
    }

    /* Soft-scroll: shift backing pixels, re-render exposed strip. */
    backing_self_scroll(dx, dy);

    if (dx > 0)
        repaint_strip(new_col + gMap.vis_cols - dx, new_row,
                      new_col + gMap.vis_cols, new_row + gMap.vis_rows);
    else if (dx < 0)
        repaint_strip(new_col, new_row,
                      old_col, new_row + gMap.vis_rows);

    if (dy > 0)
        repaint_strip(new_col, new_row + gMap.vis_rows - dy,
                      new_col + gMap.vis_cols, new_row + gMap.vis_rows);
    else if (dy < 0)
        repaint_strip(new_col, new_row,
                      new_col + gMap.vis_cols, old_row);

    /* Single backing -> window blit. */
    if (gMap.backing && gMap.owner->its_window) {
        Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
        blit_backing_to_window(&bbox, &bbox);
    }
}

void
macmap_grow_event(NhWindow *map, long newSize)
{
    if (!map || gMap.owner != map || !map->its_window) return;
    SizeWindow(map->its_window, (short)(newSize & 0xffff), (short)(newSize >> 16), true);
    Rect cr; GetWindowPortBounds(map->its_window, &cr);
    gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
    gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
    if (gMap.vis_cols < 1) gMap.vis_cols = 1;
    if (gMap.vis_rows < 1) gMap.vis_rows = 1;
    /* Persist the new size to iflags so NHDeflts can save it. */
    if (gMap.tile_mode) {
        iflags.mac_map_tile_w = (short)(cr.right - cr.left);
        iflags.mac_map_tile_h = (short)(cr.bottom - cr.top);
    } else {
        iflags.mac_map_text_w = (short)(cr.right - cr.left);
        iflags.mac_map_text_h = (short)(cr.bottom - cr.top);
    }
    if (!allocate_backing()) {
        mac_dprintf("macmap: backing realloc failed on grow\n");
    }
    /* Repaint everything from cache. */
    InvalRect(&(*map->its_window).portRect);
    macmap_update_event(map);
}

void    macmap_click(NhWindow *m UNUSED, Point p UNUSED, UInt32 mod UNUSED) { }

void
macmap_pixel_to_cell(NhWindow *map, Point pt, int *col, int *row)
{
    if (col) *col = (pt.h / gMap.cell_w) + gMap.scroll_col + 1;
    if (row) *row = (pt.v / gMap.cell_h) + gMap.scroll_row;
    (void) map;
}
