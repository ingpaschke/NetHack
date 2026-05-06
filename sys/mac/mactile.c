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
    SetGWorld(saveW, saveD);
    UnlockPixels(pm);
    ReleaseResource((Handle) ph);

    if (QDError() != noErr) {
        mac_dprintf("mactile: DrawPicture err=%d\n", (int) QDError());
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
void    mactile_set_mode(NhWindow *m, Boolean on)
                                            { (void) m; (void) on; }

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
    LockPixels(pm);
    GrafPtr saveP; GetPort(&saveP);
    SetPort(map->its_window);
    CopyBits((BitMap *) *pm,
             GetPortBitMapForCopyBits(GetWindowPort(map->its_window)),
             &src, &dst, srcCopy, NULL);
    SetPort(saveP);
    UnlockPixels(pm);
}

void    mactile_redraw_viewport(NhWindow *m){ (void) m; }
void    mactile_center_on(NhWindow *m, int c, int r)
                                            { (void) m; (void) c; (void) r; }
void    mactile_pixel_to_cell(NhWindow *m, Point p, int *c, int *r)
                                            { (void) m; (void) p; (void) c; (void) r; }
void    mactile_set_player(NhWindow *m, int c, int r)
                                            { (void) m; (void) c; (void) r; }
void    mactile_resize(NhWindow *m)         { (void) m; }
