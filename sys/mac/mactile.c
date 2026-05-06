/* mactile.c — runtime tile rendering. See mactile.h. */
#include "hack.h"
#include "macwin.h"
#include "mactile.h"
#include <Gestalt.h>
#include <QDOffscreen.h>
#include <Palettes.h>
#include <Resources.h>

/* --- module state --- */
static GWorldPtr     gTileSheet     = NULL;
static PaletteHandle gTilePalette   = NULL;
static short         gSheetDepth    = 0;
static short         gSheetCols     = 0;   /* tiles across in the sheet */

short gTileMenuNeedsUpdate = 0;

#define MT_TILE_SIZE 16
#define MT_EDGE_MARGIN 3

static short        gScrollCol = 0, gScrollRow = 0;
static short        gVisCols   = 0, gVisRows   = 0;
static short        gTileCache[ROWNO][COLNO];

/* --- internal coord helpers --- */
static void
tileidx_to_src_rect(int idx, Rect *r)
{
    short sx = (idx % gSheetCols) * MT_TILE_SIZE;
    short sy = (idx / gSheetCols) * MT_TILE_SIZE;
    SetRect(r, sx, sy, sx + MT_TILE_SIZE, sy + MT_TILE_SIZE);
}

static void
cell_to_dst_rect(int col, int row, Rect *r)
{
    short dx = (col - gScrollCol) * MT_TILE_SIZE;
    short dy = (row - gScrollRow) * MT_TILE_SIZE;
    SetRect(r, dx, dy, dx + MT_TILE_SIZE, dy + MT_TILE_SIZE);
}

/* --- helper: load a PICT resource into an offscreen GWorld --- */
static Boolean
load_tile_pict(short pict_id, short depth)
{
    PicHandle ph = GetPicture(pict_id);
    if (!ph) {
        mac_dprintf("mactile: GetPicture(%d) returned NULL\n", (int) pict_id);
        return false;
    }
    HLock((Handle) ph);
    Rect frame = (**ph).picFrame;
    HUnlock((Handle) ph);
    OffsetRect(&frame, -frame.left, -frame.top);

    QDErr err = NewGWorld(&gTileSheet, depth, &frame, NULL, NULL, 0);
    if (err != noErr || !gTileSheet) {
        mac_dprintf("mactile: NewGWorld failed err=%d\n", (int) err);
        ReleaseResource((Handle) ph);
        return false;
    }
    PixMapHandle pm = GetGWorldPixMap(gTileSheet);
    LockPixels(pm);
    GWorldPtr saveW; GDHandle saveD;
    GetGWorld(&saveW, &saveD);
    SetGWorld(gTileSheet, NULL);
    EraseRect(&frame);
    DrawPicture(ph, &frame);
    QDErr draw_err = QDError();           /* capture while gTileSheet is current */
    SetGWorld(saveW, saveD);
    UnlockPixels(pm);
    ReleaseResource((Handle) ph);

    if (draw_err != noErr) {
        mac_dprintf("mactile: DrawPicture err=%d\n", (int) draw_err);
        DisposeGWorld(gTileSheet);
        gTileSheet = NULL;
        return false;
    }
    gSheetDepth = depth;
    gSheetCols  = (frame.right - frame.left) / 16;
    return true;
}

Boolean
mactile_available(void)
{
    GDHandle gd = GetMainDevice();
    if (!gd) return false;
    short depth = (*(*gd)->gdPMap)->pixelSize;
    return depth >= 4;
}

Boolean
mactile_init(void)
{
    if (gTileSheet) return true;        /* idempotent */
    if (!mactile_available()) return false;

    GDHandle gd = GetMainDevice();
    short screen_depth = (*(*gd)->gdPMap)->pixelSize;
    short pict_id = (screen_depth >= 8) ? 1001 : 1000;
    short depth   = (screen_depth >= 8) ? 8    : 4;

    if (!load_tile_pict(pict_id, depth)) return false;

    /* On 8bpp screens, the Palette is attached later via mactile_set_mode. */
    {
        int x, y;
        for (y = 0; y < ROWNO; ++y)
            for (x = 0; x < COLNO; ++x)
                gTileCache[y][x] = 0;
    }
    return true;
}

void
mactile_shutdown(void)
{
    if (gTileSheet)   { DisposeGWorld(gTileSheet);    gTileSheet   = NULL; }
    if (gTilePalette) { DisposePalette(gTilePalette); gTilePalette = NULL; }
}
void
mactile_set_mode(NhWindow *map, Boolean on)
{
    if (!map) return;

    if (on && !gTileSheet) {
        if (!mactile_init()) return;   /* silent fallback */
    }
    map->tile_mode = on;

    if (on) {
        Rect content;
        GetWindowPortBounds(map->its_window, &content);
        gVisCols = (content.right - content.left) / MT_TILE_SIZE;
        gVisRows = (content.bottom - content.top) / MT_TILE_SIZE;
        if (gVisCols < 1) gVisCols = 1;
        if (gVisRows < 1) gVisRows = 1;
        gScrollCol = 0;
        gScrollRow = 0;

        /* On 8bpp screens, attach a 32-entry pmTolerant Palette so the
           system tries to allocate close colors without trashing
           the reserved system slots. */
        if (gSheetDepth == 8 && !gTilePalette) {
            CTabHandle ct = (**GetGWorldPixMap(gTileSheet)).pmTable;
            gTilePalette = NewPalette(32, ct, pmTolerant, 0x0000);
            if (gTilePalette) {
                SetPalette(map->its_window, gTilePalette, true);
                ActivatePalette(map->its_window);
            }
        }
        /* If a game is in progress, center on the hero so the first frame doesn't
         * flash the upper-left of the map before the next print_glyph cycle. */
        if (u.ux || u.uy)
            mactile_center_on(map, (int) u.ux, (int) u.uy);
        mactile_redraw_viewport(map);
    } else {
        /* Caller (macwin) is responsible for re-rendering the map via mactty. */
        if (gTilePalette) {
            SetPalette(map->its_window, NULL, false);
        }
    }
}

void
mactile_draw_cell(NhWindow *map, int col, int row, int tileidx)
{
    if (!map || !map->tile_mode || !gTileSheet) return;
    if (col < 0 || col >= COLNO || row < 0 || row >= ROWNO) return;

    gTileCache[row][col] = (short) tileidx;

    if (col < gScrollCol || col >= gScrollCol + gVisCols
        || row < gScrollRow || row >= gScrollRow + gVisRows)
        return;   /* off-screen, cache only */

    Rect src, dst;
    tileidx_to_src_rect(tileidx, &src);
    cell_to_dst_rect(col, row, &dst);

    PixMapHandle pm = GetGWorldPixMap(gTileSheet);
    if (!LockPixels(pm)) {
        UnlockPixels(pm);                  /* pair per IM */
        if (!LockPixels(pm))
            return;                        /* drop this cell; cache holds the index */
    }
    GrafPtr saveP; GetPort(&saveP);
    SetPort(map->its_window);
    CopyBits((BitMap *) *pm,
             GetPortBitMapForCopyBits(GetWindowPort(map->its_window)),
             &src, &dst, srcCopy, NULL);
    SetPort(saveP);
    UnlockPixels(pm);
}

void
mactile_redraw_viewport(NhWindow *map)
{
    if (!map || !map->tile_mode || !gTileSheet) return;

    Rect content;
    GetWindowPortBounds(map->its_window, &content);
    GrafPtr saveP; GetPort(&saveP);
    SetPort(map->its_window);
    EraseRect(&content);
    SetPort(saveP);

    int r, c;
    for (r = gScrollRow; r < gScrollRow + gVisRows && r < ROWNO; ++r)
        for (c = gScrollCol; c < gScrollCol + gVisCols && c < COLNO; ++c)
            mactile_draw_cell(map, c, r, gTileCache[r][c]);
}

void
mactile_center_on(NhWindow *map, int col, int row)
{
    if (!map) return;
    short new_col = col - gVisCols / 2;
    short new_row = row - gVisRows / 2;
    if (new_col < 0) new_col = 0;
    if (new_row < 0) new_row = 0;
    if (new_col + gVisCols > COLNO) new_col = COLNO - gVisCols;
    if (new_row + gVisRows > ROWNO) new_row = ROWNO - gVisRows;
    if (new_col < 0) new_col = 0;   /* clamp again if window > map */
    if (new_row < 0) new_row = 0;
    if (new_col == gScrollCol && new_row == gScrollRow) return;
    gScrollCol = new_col;
    gScrollRow = new_row;
    mactile_redraw_viewport(map);
}

void
mactile_set_player(NhWindow *map, int col, int row)
{
    if (!map || !map->tile_mode) return;
    if (col < gScrollCol + MT_EDGE_MARGIN
        || col >= gScrollCol + gVisCols - MT_EDGE_MARGIN
        || row < gScrollRow + MT_EDGE_MARGIN
        || row >= gScrollRow + gVisRows - MT_EDGE_MARGIN)
        mactile_center_on(map, col, row);
}

void
mactile_pixel_to_cell(NhWindow *map, Point pt, int *col, int *row)
{
    (void) map;
    if (col) *col = (pt.h / MT_TILE_SIZE) + gScrollCol + 1;
    if (row) *row = (pt.v / MT_TILE_SIZE) + gScrollRow;
}

void
mactile_resize(NhWindow *map)
{
    if (!map || !map->tile_mode) return;
    Rect cr;
    GetWindowPortBounds(map->its_window, &cr);
    gVisCols = (cr.right - cr.left) / MT_TILE_SIZE;
    gVisRows = (cr.bottom - cr.top) / MT_TILE_SIZE;
    if (gVisCols < 1) gVisCols = 1;
    if (gVisRows < 1) gVisRows = 1;
    mactile_redraw_viewport(map);
}
