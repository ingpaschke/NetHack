/* macmap.c — separate map window for the Mac 68k port. See macmap.h. */
#include "hack.h"
#include "macwin.h"
#include "mactty.h"
#include "macmap.h"
#include "mactile.h"
#include <Resources.h>
#include <QDOffscreen.h>
#include <Palettes.h>
#include <Controls.h>

/* From src/tile.c (generated). NetHack 3.7 with default config (no
   STATUES_DONT_LOOK_LIKE_MONSTERS) reserves a per-monster tile slot for
   each statue beyond `maxothtile`, but real NetHack tile data only
   provides one generic statue tile in objects.txt. We don't generate
   per-monster statue tiles into our PICT sheet (it bloats the sheet to
   the point that classic-Mac DrawPicture paths balloon memory through
   QEMU). Instead we remap any out-of-sheet tile index to the OBJ_STATUE
   tile at runtime — see remap_tile_idx below. */
extern int maxothtile;
extern glyph_map glyphmap[MAX_GLYPH];

static short
remap_tile_idx(int idx)
{
    if (idx <= maxothtile) return (short) idx;
    /* Cache the OBJ_STATUE tile index from glyphmap on first use. */
    static short statue_tile = -1;
    if (statue_tile < 0)
        statue_tile = glyphmap[GLYPH_OBJ_OFF + STATUE].tileidx;
    return statue_tile;
}

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
    /* Software cursor for getpos/farlook: NetHack calls curs(WIN_MAP,x,y)
       between cells. We track the inverted cell so the next call can
       un-invert it before highlighting the new one. */
    Boolean        cursor_on;
    short          cursor_x, cursor_y;
    ControlHandle  vscroll, hscroll;   /* decorative (inert) scrollbars */
    Boolean        decorated;          /* documentProc with chrome/strips */
    short          inset_r, inset_b;   /* reserved strip widths (0 = borderless) */
} MacMapState;

static MacMapState gMap = {0};

static void repaint_full_viewport(void);   /* forward declaration */

/* Drawable map area inside the window = port bounds minus the decorated
   scrollbar strips (0 insets when borderless). */
static void
map_content_bounds(Rect *out)
{
    if (!gMap.owner || !gMap.owner->its_window) { SetRect(out, 0, 0, 0, 0); return; }
    GetWindowPortBounds(gMap.owner->its_window, out);
    out->right  -= gMap.inset_r;
    out->bottom -= gMap.inset_b;
}

/* Position the inert scrollbar controls into the right/bottom strips.
   Call after any SizeWindow on the map window (no-op when borderless).
   The vscroll bottom / hscroll right stop at b.* - 14 (not -15) so the two
   bars don't overlap; that 1px seam is the grow-box corner DrawGrowIcon fills. */
static void
layout_scroll_controls(void)
{
    Rect b;
    if (!gMap.decorated || !gMap.vscroll || !gMap.hscroll || !gMap.owner
        || !gMap.owner->its_window)
        return;
    GetWindowPortBounds(gMap.owner->its_window, &b);
    HideControl(gMap.vscroll); HideControl(gMap.hscroll);
    MoveControl(gMap.vscroll, b.right - 15, b.top - 1);
    SizeControl(gMap.vscroll, 16, (b.bottom - 14) - (b.top - 1));
    MoveControl(gMap.hscroll, b.left - 1, b.bottom - 15);
    SizeControl(gMap.hscroll, (b.right - 14) - (b.left - 1), 16);
    ShowControl(gMap.vscroll); ShowControl(gMap.hscroll);
}

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
    /* The map background is black (NetHack's colors are designed for a dark
       terminal); pure-black CLR_BLACK glyphs would be invisible, so render
       color 0 as a dark gray, the way terminals show "black" on black. */
    if (color == 0) { c.red = c.green = c.blue = R16(0x55); }
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

    /* Pin the backing so the Memory Manager can't purge it from under CopyBits. */
    {
        PixMapHandle pm = GetGWorldPixMap(gMap.backing);
        NoPurgePixels(pm);   /* backing stays resident; reallocated on grow */
    }
    /* Initialize the backing port: black foreground on WHITE background, and
       the SAME font/size the map window uses, so DrawChar in draw_cell_text
       produces actual glyphs (not "missing-glyph" rects).  fg=black/bg=white
       is REQUIRED: CopyBits (tile blits) colorizes the image unless the
       foreground is black and the background white.  The text path flips to a
       black background only locally, per cell, and restores it. */
    GWorldPtr saveW; GDHandle saveD;
    GetGWorld(&saveW, &saveD);
    SetGWorld(gMap.backing, NULL);
    PixMapHandle pm = GetGWorldPixMap(gMap.backing);
    if (!LockPixels(pm)) {
        /* Base address invalid (pixels purged) — drawing here would crash
           on real hardware. Tear the half-built GWorld down and fail. */
        mac_dprintf("macmap: LockPixels failed in allocate_backing\n");
        SetGWorld(saveW, saveD);
        DisposeGWorld(gMap.backing);
        gMap.backing = NULL;
        return false;
    }
    {
        RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
        RGBColor black = {0, 0, 0};
        RGBBackColor(&white);
        RGBForeColor(&black);
        {
            short fn = (gMap.owner && gMap.owner->font_number > 0)
                       ? gMap.owner->font_number : kFontIDMonaco;
            short fs = (gMap.owner && gMap.owner->font_size > 0)
                       ? gMap.owner->font_size : 9;
            TextFont(fn);
            TextSize(fs);
            TextFace(0);
            TextMode(srcCopy);
        }
    }
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
    if (!LockPixels(pm)) {
        /* Pixels purged from under us — skip the blit rather than read
           stale/garbage memory through CopyBits. */
        mac_dprintf("macmap: blit_backing_to_window: LockPixels failed\n");
        return;
    }
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

    short wind_id = small_screen ? kWindMapBorderless : kWindMapDocument;

    WindowPtr w = (WindowPtr) GetNewCWindow(wind_id, NULL, (WindowPtr) -1L);
    if (!w) {
        mac_dprintf("macmap: GetNewCWindow(%d) returned NULL\n", (int) wind_id);
        return false;
    }
    SetWRefCon(w, MACMAP_REFCON);
    SetWindowKind(w, WIN_BASE_KIND + NHW_MAP);
    map->its_window = w;
    ShowWindow(w);

    gMap.owner     = map;   /* set early so map_content_bounds is usable */
    gMap.decorated = !small_screen;
    gMap.inset_r   = gMap.decorated ? 15 : 0;
    gMap.inset_b   = gMap.decorated ? 15 : 0;
    if (gMap.decorated) {
        Rect b, vr, hr;
        GetWindowPortBounds(w, &b);
        SetRect(&vr, b.right - 15, b.top - 1,  b.right + 1, b.bottom - 14);
        SetRect(&hr, b.left - 1,  b.bottom - 15, b.right - 14, b.bottom + 1);
        /* nominal range so a thumb draws; never TrackControl'd (inert).
           procID 16 == scrollBarProc (matches macwin.c's literal usage). */
        gMap.vscroll = NewControl(w, &vr, "\p", true, 0, 0, 1, 16, 0);
        gMap.hscroll = NewControl(w, &hr, "\p", true, 0, 0, 1, 16, 0);
    } else {
        gMap.vscroll = gMap.hscroll = NULL;
    }

    /* placement is owned by SanePositions() */
    /* Apply saved text-mode size from NHDeflts (iflags); position deferred. */
    if (iflags.mac_map_text_w && iflags.mac_map_text_h) {
        SizeWindow(w, iflags.mac_map_text_w, iflags.mac_map_text_h, false);
    }
    layout_scroll_controls();   /* match controls to the (possibly resized) window */

    gMap.tile_mode   = false;
    gMap.backing     = NULL;
    gMap.palette     = NULL;
    /* Cell metrics fall back to safe defaults; macmap_finalize re-derives
       them from the NhWindow after get_tty_metrics has populated it. */
    gMap.cell_w      = 6;
    gMap.cell_h      = 14;
    gMap.vis_cols    = 80;
    gMap.vis_rows    = 21;
    gMap.scroll_col  = 1;   /* NetHack col 0 is unused */
    gMap.scroll_row  = 0;
    /* Backing + tile-mode init deferred to macmap_finalize. */

    return true;
}

/* Called from mac_create_nhwindow AFTER get_tty_metrics has populated
   aWin->char_width / row_height / font_number. Re-derive cell metrics,
   recompute the viewport from actual window size, allocate the backing
   GWorld, and apply tile mode if NHDeflts asks. */
void
macmap_finalize(NhWindow *map)
{
    if (!map || gMap.owner != map || !map->its_window) return;
    if (map->char_width  > 0) gMap.cell_w = map->char_width;
    if (map->row_height  > 0) gMap.cell_h = map->row_height;
    {
        Rect cr; map_content_bounds(&cr);
        gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
        gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
        if (gMap.vis_cols < 1) gMap.vis_cols = 1;
        if (gMap.vis_rows < 1) gMap.vis_rows = 1;
    }
    if (!allocate_backing()) {
        mac_dprintf("macmap: backing alloc failed at finalize\n");
    }
    if (iflags.wc_tiled_map && mactile_available()) {
        macmap_set_mode(map, true);
    }
    /* Centering happens lazily inside macmap_print_glyph when the player
       glyph is drawn — at finalize time u.ux/u.uy may not be set yet. */
}

void
macmap_destroy(NhWindow *map)
{
    if (!map || gMap.owner != map) return;
    if (gMap.backing) { DisposeGWorld(gMap.backing); gMap.backing = NULL; }
    if (gMap.palette) { DisposePalette(gMap.palette); gMap.palette = NULL; }
    if (map->its_window) {
        /* DisposeWindow disposes attached controls; just drop our handles. */
        gMap.vscroll = gMap.hscroll = NULL;
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
        /* Restore saved tile-mode window size if persisted. */
        if (iflags.mac_map_tile_w && iflags.mac_map_tile_h
            && map->its_window) {
            SizeWindow(map->its_window,
                       iflags.mac_map_tile_w, iflags.mac_map_tile_h, false);
        }
        layout_scroll_controls();   /* keep controls matched to window size */
        /* Always derive vis_cols/vis_rows from the actual window size — the
           edge-margin and scroll math depend on these matching what the user
           can really see, not a hardcoded "30x21". */
        if (map->its_window) {
            Rect cr; map_content_bounds(&cr);
            gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
            gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
        }
        if (gMap.vis_cols < 1) gMap.vis_cols = 1;
        if (gMap.vis_rows < 1) gMap.vis_rows = 1;
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
        /* Restore saved text-mode window size if persisted. */
        if (iflags.mac_map_text_w && iflags.mac_map_text_h
            && map->its_window) {
            SizeWindow(map->its_window,
                       iflags.mac_map_text_w, iflags.mac_map_text_h, false);
        }
        layout_scroll_controls();   /* keep controls matched to window size */
        if (map->its_window) {
            Rect cr; map_content_bounds(&cr);
            gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
            gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
        }
        if (gMap.vis_cols < 1) gMap.vis_cols = 1;
        if (gMap.vis_rows < 1) gMap.vis_rows = 1;
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
    /* Repopulate the backing for the new mode, then mark the map window dirty
       so it redraws on the NEXT update pass.  A SYNCHRONOUS blit from here
       does NOT take: macmap_set_mode runs in the menu-handler context, where
       the window's port/visRgn isn't settled, so the pixels are clipped away
       (this is why a manual ^R was still needed).  InvalWindowRect defers the
       redraw to HandleUpdate -> macmap_update_event in the settled command
       loop, which works — same as what ^R/docrt does.  (The old code used
       InvalRect, which acts on the current, wrong port.) */
    repaint_full_viewport();
    if (map->its_window) {
        Rect b; GetWindowPortBounds(map->its_window, &b);
        InvalWindowRect(map->its_window, &b);
    }
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

    RGBColor black = {0, 0, 0};
    RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};

    if (gMap.backing) {
        /* Paint into backing first, then blit cell to window. */
        PixMapHandle pm = GetGWorldPixMap(gMap.backing);
        if (!LockPixels(pm)) {
            mac_dprintf("macmap: draw_cell_text: LockPixels failed\n");
            return;
        }
        GWorldPtr saveW; GDHandle saveD;
        GetGWorld(&saveW, &saveD);
        SetGWorld(gMap.backing, NULL);

        FontInfo fi; GetFontInfo(&fi);
        Rect cell = { dy, dx, dy + gMap.cell_h, dx + gMap.cell_w };
        RGBBackColor(&black);          /* NetHack's colors want a dark bg */
        EraseRect(&cell);
        set_nh_color(color);
        /* Baseline = top + ascent so the glyph's TOP row aligns with the cell
           top and isn't clipped (the old fixed cell_h-4 cut tall glyphs). */
        MoveTo(dx, dy + fi.ascent);
        DrawChar(ch);
        RGBForeColor(&black);          /* restore fg=black/bg=white — required */
        RGBBackColor(&white);          /* so the tile-blit CopyBits isn't tinted */

        SetGWorld(saveW, saveD);
        UnlockPixels(pm);
        blit_backing_to_window(&cell, &cell);
    } else {
        /* Fallback: direct to window (slower). */
        GrafPtr saveP; GetPort(&saveP);
        SetPort(gMap.owner->its_window);
        FontInfo fi; GetFontInfo(&fi);
        Rect cell = { dy, dx, dy + gMap.cell_h, dx + gMap.cell_w };
        RGBBackColor(&black);
        EraseRect(&cell);
        set_nh_color(color);
        MoveTo(dx, dy + fi.ascent);
        DrawChar(ch);
        RGBForeColor(&black);
        RGBBackColor(&white);
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

/* Cell cursor for getpos/farlook (and an always-on hero highlight on every
   curs call).  TILE mode: a black outer ring (visible on light tiles) plus an
   inner ring in a bright non-white tile-CLUT color selected by INDEX via
   PmForeColor — index-direct, so the Palette Manager never renders a new color
   / reorganizes the device CLUT (requesting a non-exact color, or routing
   through the system-CLUT backing, is what recolored the whole map in earlier
   attempts).  TEXT mode: no tile palette is attached, so RGBForeColor is safe;
   the map bg is black there, so a single white frame is used. */
static void
draw_cursor_border(int col, int row)
{
    if (!gMap.owner || !gMap.owner->its_window) return;
    if (col < 1 || col >= COLNO || row < 0 || row >= ROWNO) return;
    short dx = (col - gMap.scroll_col) * gMap.cell_w;
    short dy = (row - gMap.scroll_row) * gMap.cell_h;
    if (dx < 0 || dy < 0
        || dx >= gMap.vis_cols * gMap.cell_w
        || dy >= gMap.vis_rows * gMap.cell_h) return;
    Rect cell = { dy, dx, dy + gMap.cell_h, dx + gMap.cell_w };
    GrafPtr saveP; GetPort(&saveP);
    SetPortWindowPort(gMap.owner->its_window);
    PenState savePen; GetPenState(&savePen);
    PenSize(1, 1);
    PenMode(srcCopy);
    RGBColor black = {0, 0, 0};
    if (gMap.palette) {
        /* TILE mode: inner ring in a bright non-white tile-CLUT color chosen
           by INDEX via PmForeColor (index-direct — no RGB match, no render),
           visible on dark tiles; then a black outer ring (exact palette entry,
           safe) for light tiles.  Ending on black leaves a sane foreground. */
        short cidx = mactile_cursor_clut_index();
        if (cidx >= 0) {
            Rect inner = cell;
            InsetRect(&inner, 1, 1);
            if (inner.right > inner.left && inner.bottom > inner.top) {
                PmForeColor(cidx);
                FrameRect(&inner);
            }
        }
        RGBForeColor(&black);
        FrameRect(&cell);
    } else {
        /* TEXT mode: no tile palette is attached, so RGBForeColor is safe
           (system palette, no reorg) — and the map background is black, so a
           black frame would be invisible.  Draw a WHITE frame instead. */
        RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
        RGBForeColor(&white);
        FrameRect(&cell);
        RGBForeColor(&black);   /* restore a sane foreground */
    }
    SetPenState(&savePen);
    SetPort(saveP);
}

/* Repaint a single cell from cache — removes the cursor border by drawing
   the underlying tile/glyph back over it. */
static void
redraw_cell_from_cache(int col, int row)
{
    if (col < 1 || col >= COLNO || row < 0 || row >= ROWNO) return;
    if (gMap.tile_mode) {
        short idx = gMap.tile_cache[row][col];
        draw_cell_tile(col, row, (int) idx);
    } else {
        char ch  = (char) gMap.text_cache[row][col];
        int  color = (int) gMap.text_color[row][col];
        if (ch == 0) ch = ' ';
        draw_cell_text(col, row, ch, color);
    }
}

void
macmap_curs(NhWindow *map, int x, int y)
{
    if (!map || gMap.owner != map) return;
    /* Remove the old cursor border by repainting its cell from cache. */
    if (gMap.cursor_on
        && (gMap.cursor_x != x || gMap.cursor_y != y)) {
        redraw_cell_from_cache(gMap.cursor_x, gMap.cursor_y);
    }
    /* Frame the new cell. The cell's underlying tile/glyph is already on
       screen (from print_glyph or a repaint); we only add the border. */
    draw_cursor_border(x, y);
    gMap.cursor_x  = (short) x;
    gMap.cursor_y  = (short) y;
    gMap.cursor_on = true;
}

void
macmap_print_glyph(NhWindow *map, int x, int y,
                    const glyph_info *gi)
{
    if (!map || gMap.owner != map) return;
    if (!gi) return;
    if (x < 0 || x >= COLNO || y < 0 || y >= ROWNO) return;

    /* Auto-center the viewport on the hero. NetHack core only calls
       cliparound() between turns from moveloop; the very first frame
       (and the frame after a resize) doesn't get one, so without this
       the viewport stays at (0,0) and the visible area is unexplored
       stone tiles — looks like a black window. */
    if ((int) x == (int) u.ux && (int) y == (int) u.uy) {
        macmap_cliparound(map, x, y);
    }

    char ch = gi->ttychar;
    int  color = gi->gm.sym.color;
    int  idx = remap_tile_idx(gi->gm.tileidx);

    /* Update both caches so a mode toggle has data to repaint with. */
    gMap.text_cache[y][x] = (unsigned char) ch;
    gMap.text_color[y][x] = (unsigned char) color;
    gMap.tile_cache[y][x] = (short) idx;

    if (gMap.tile_mode)
        draw_cell_tile(x, y, idx);
    else
        draw_cell_text(x, y, ch, color);
}

void
macmap_update_event(NhWindow *map)
{
    /* Called from HandleUpdate, INSIDE its BeginUpdate/EndUpdate — visRgn is
       the damaged region and the blit is clipped to it.  Do NOT call
       BeginUpdate here: HandleUpdate already did, and a second call consumes
       the invalid region, leaving an empty visRgn that clips out every draw.
       (Mode switches don't call this directly — they InvalWindowRect and let
       the resulting update event reach here.) */
    if (!map || gMap.owner != map || !map->its_window) return;

    /* Window was damaged and we're about to repaint the whole content;
       any inverted-cell software cursor is being wiped. */
    gMap.cursor_on = false;

    GrafPtr saveP; GetPort(&saveP);
    SetPort(map->its_window);

    if (gMap.backing) {
        Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
        Rect dst = bbox;
        blit_backing_to_window(&bbox, &dst);
    } else {
        /* Fallback: cache-replay redraw. */
        Rect content; map_content_bounds(&content);
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

    /* Decorated windows: draw the inert scrollbar controls and grow box on
       top of the blit, inside the chrome strips reserved by the insets. */
    if (gMap.decorated) {
        DrawControls(map->its_window);
        DrawGrowIcon(map->its_window);
    }

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
    /* Reset scroll so the next print_glyph for the hero forces a recenter
       (otherwise the prior level's viewport may still happen to contain the
       new hero position, and the edge-margin check skips the recenter).
       scroll_col=1 since col 0 is unused. */
    gMap.scroll_col = 1;
    gMap.scroll_row = 0;
    gMap.cursor_on  = false;
    if (map->its_window) {
        GrafPtr saveP; GetPort(&saveP);
        SetPort(map->its_window);
        Rect content; map_content_bounds(&content);
        EraseRect(&content);
        SetPort(saveP);
    }
    /* Clear the backing too — otherwise the next damage event blits stale
       pixels back onto the window. */
    if (gMap.backing) {
        PixMapHandle pm = GetGWorldPixMap(gMap.backing);
        if (LockPixels(pm)) {
            GWorldPtr saveW; GDHandle saveD;
            GetGWorld(&saveW, &saveD);
            SetGWorld(gMap.backing, NULL);
            Rect bb; GetPortBounds((CGrafPtr) gMap.backing, &bb);
            EraseRect(&bb);
            SetGWorld(saveW, saveD);
            UnlockPixels(pm);
        } else {
            mac_dprintf("macmap: macmap_clear: LockPixels failed\n");
        }
    }
}

#define MT_EDGE_MARGIN 3

static void
recompute_scroll_for_center(int x, int y, short *new_col, short *new_row)
{
    short c = (short) x - gMap.vis_cols / 2;
    short r = (short) y - gMap.vis_rows / 2;
    /* NetHack uses cols [1, COLNO-1]; clamp scroll so col 0 never enters
       the viewport. Rows use [0, ROWNO). */
    if (c < 1) c = 1;
    if (r < 0) r = 0;
    if (c + gMap.vis_cols > COLNO) c = COLNO - gMap.vis_cols;
    if (r + gMap.vis_rows > ROWNO) r = ROWNO - gMap.vis_rows;
    if (c < 1) c = 1;
    if (r < 0) r = 0;
    *new_col = c;
    *new_row = r;
}

static void
repaint_full_viewport(void)
{
    if (!gMap.owner) return;
    gMap.cursor_on = false;   /* full repaint wipes the inverted-cell cursor */
    if (gMap.backing) {
        PixMapHandle pm = GetGWorldPixMap(gMap.backing);
        if (LockPixels(pm)) {
            Rect bbox; GetPortBounds((CGrafPtr) gMap.backing, &bbox);
            GWorldPtr saveW; GDHandle saveD;
            GetGWorld(&saveW, &saveD);
            SetGWorld(gMap.backing, NULL);
            EraseRect(&bbox);
            SetGWorld(saveW, saveD);
            UnlockPixels(pm);
        }
    }
    int r, c;
    /* NetHack only uses cols [1, COLNO-1]; col 0 is unused (display.c uses
       `for (x=1; x<COLNO; ++x)`). Skip col 0 so we don't paint stale cache
       (idx==0 → tile 0, which is a real glyph, not "blank"). */
    int c_first = gMap.scroll_col < 1 ? 1 : gMap.scroll_col;
    int c_last  = gMap.scroll_col + gMap.vis_cols;
    if (c_last > COLNO) c_last = COLNO;
    for (r = gMap.scroll_row; r < gMap.scroll_row + gMap.vis_rows && r < ROWNO; ++r)
        for (c = c_first; c < c_last; ++c)
            redraw_cell_from_cache(c, r);
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
    if (!LockPixels(pm)) {
        /* Purged pixels: CopyBits here reads AND writes *pm, so a stale
           baseAddr would corrupt the backing. Skip the scroll. */
        mac_dprintf("macmap: backing_self_scroll: LockPixels failed\n");
        return;
    }
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
    /* NetHack uses cols [1, COLNO-1]; col 0 is unused. */
    if (col_start < 1) col_start = 1;
    for (r = row_start; r < row_end; ++r)
        for (c = col_start; c < col_end; ++c)
            redraw_cell_from_cache(c, r);
}

void
macmap_cliparound(NhWindow *map, int x, int y)
{
    if (!map || gMap.owner != map)
        return;

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

    /* The repaint paths below blit the backing over the window, which
       wipes any software-cursor InvertRect. Mark the cursor as no longer
       drawn so the next macmap_curs call doesn't try to "erase" pixels
       that are already clean. */
    gMap.cursor_on = false;

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
    /* Reposition the inert scrollbar controls into the new strips before
       deriving content bounds. */
    layout_scroll_controls();
    Rect full; GetWindowPortBounds(map->its_window, &full);
    Rect cr; map_content_bounds(&cr);
    gMap.vis_cols = (cr.right - cr.left) / gMap.cell_w;
    gMap.vis_rows = (cr.bottom - cr.top) / gMap.cell_h;
    if (gMap.vis_cols < 1) gMap.vis_cols = 1;
    if (gMap.vis_rows < 1) gMap.vis_rows = 1;
    /* Persist the FULL new window size to iflags so NHDeflts can save it. */
    if (gMap.tile_mode) {
        iflags.mac_map_tile_w = (short)(full.right - full.left);
        iflags.mac_map_tile_h = (short)(full.bottom - full.top);
    } else {
        iflags.mac_map_text_w = (short)(full.right - full.left);
        iflags.mac_map_text_h = (short)(full.bottom - full.top);
    }
    if (!allocate_backing()) {
        mac_dprintf("macmap: backing realloc failed on grow\n");
    }
    /* Recenter on the hero, then repaint from cache. */
    if (u.ux > 0 || u.uy > 0) {
        gMap.scroll_col = 1;     /* col 0 is unused */
        gMap.scroll_row = 0;
        macmap_cliparound(map, (int) u.ux, (int) u.uy);
    } else {
        repaint_full_viewport();
    }
}

/* Size the map window to show as much of the map as fits in avail_w x avail_h,
   snapped to whole cells (no dead space, last row/col not clipped) and never
   larger than the full ROWNO x COLNO map. Reuses the grow path for the resize +
   viewport/backing update. Placement is still owned by SanePositions(). */
void
macmap_fit(short avail_w, short avail_h)
{
    long full_w, full_h;
    short w, h, cols, rows;
    /* NetHack map column 0 is unused (the viewport starts at scroll_col=1), so
       only COLNO-1 columns hold content; sizing for the full COLNO leaves one
       blank column on the right. ROWNO rows are all used. */
    short map_cols = COLNO - 1;
    if (!gMap.owner || !gMap.owner->its_window) return;
    if (gMap.cell_w < 1 || gMap.cell_h < 1) return;
    if (avail_w < gMap.cell_w + gMap.inset_r) avail_w = gMap.cell_w + gMap.inset_r;
    if (avail_h < gMap.cell_h + gMap.inset_b) avail_h = gMap.cell_h + gMap.inset_b;
    full_w = (long) map_cols * gMap.cell_w + gMap.inset_r;
    full_h = (long) ROWNO * gMap.cell_h + gMap.inset_b;
    w = (full_w < (long) avail_w) ? (short) full_w : avail_w;
    h = (full_h < (long) avail_h) ? (short) full_h : avail_h;
    cols = (short) ((w - gMap.inset_r) / gMap.cell_w);
    rows = (short) ((h - gMap.inset_b) / gMap.cell_h);
    if (cols < 1) cols = 1;
    if (cols > map_cols) cols = map_cols;
    if (rows < 1) rows = 1;
    if (rows > ROWNO) rows = ROWNO;
    w = (short) (cols * gMap.cell_w + gMap.inset_r);
    h = (short) (rows * gMap.cell_h + gMap.inset_b);
    macmap_grow_event(gMap.owner, ((long) h << 16) | ((long) w & 0xffffL));
}

/* Returns true if a window-LOCAL click landed on the decorative chrome
   (either scrollbar control, or the reserved right/bottom strips + grow
   corner) and should be swallowed.  Called from BaseClick so a click on the
   inert scrollbars doesn't get interpreted as a click-to-move on the map.
   Returns false for clicks in the real map area (let click-to-move proceed). */
Boolean
macmap_click(NhWindow *map, Point pt, UInt32 mod UNUSED)
{
    if (!gMap.decorated || !map || !map->its_window)
        return false;
    {
        ControlHandle c; short part;
        part = FindControl(pt, map->its_window, &c);
        if (part && (c == gMap.vscroll || c == gMap.hscroll))
            return true;   /* on a scrollbar control */
    }
    {   /* the reserved strips (incl. the grow-box corner) */
        Rect b;
        GetWindowPortBounds(map->its_window, &b);
        if (pt.h >= b.right - gMap.inset_r || pt.v >= b.bottom - gMap.inset_b)
            return true;
    }
    return false;
}

void
macmap_pixel_to_cell(NhWindow *map, Point pt, int *col, int *row)
{
    if (col) *col = (pt.h / gMap.cell_w) + gMap.scroll_col;
    if (row) *row = (pt.v / gMap.cell_h) + gMap.scroll_row;
    (void) map;
}
