/* mactile.c — tile sheet asset + per-tile blit. See mactile.h. */
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

    return true;
}

void
mactile_shutdown(void)
{
    if (gTileSheet)   { DisposeGWorld(gTileSheet);    gTileSheet   = NULL; }
    if (gTilePalette) { DisposePalette(gTilePalette); gTilePalette = NULL; }
}

short
mactile_sheet_depth(void)
{
    return gSheetDepth;
}

CTabHandle
mactile_sheet_ctable(void)
{
    if (!gTileSheet) return NULL;
    return (**GetGWorldPixMap(gTileSheet)).pmTable;
}

void
mactile_blit_to(GWorldPtr dst, int tile_idx, short dst_x, short dst_y)
{
    if (!gTileSheet || !dst) return;
    short sx = (tile_idx % gSheetCols) * 16;
    short sy = (tile_idx / gSheetCols) * 16;
    Rect src = { sy, sx, sy + 16, sx + 16 };
    Rect dr  = { dst_y, dst_x, dst_y + 16, dst_x + 16 };

    PixMapHandle spm = GetGWorldPixMap(gTileSheet);
    PixMapHandle dpm = GetGWorldPixMap(dst);
    LockPixels(spm); LockPixels(dpm);
    GWorldPtr saveW; GDHandle saveD;
    GetGWorld(&saveW, &saveD);
    SetGWorld(dst, NULL);
    CopyBits((BitMap *) *spm, (BitMap *) *dpm,
             &src, &dr, srcCopy, NULL);
    SetGWorld(saveW, saveD);
    UnlockPixels(dpm); UnlockPixels(spm);
}

void
mactile_blit_to_window(WindowPtr dst, int tile_idx, short dst_x, short dst_y)
{
    if (!gTileSheet || !dst) return;
    short sx = (tile_idx % gSheetCols) * 16;
    short sy = (tile_idx / gSheetCols) * 16;
    Rect src = { sy, sx, sy + 16, sx + 16 };
    Rect dr  = { dst_y, dst_x, dst_y + 16, dst_x + 16 };

    PixMapHandle spm = GetGWorldPixMap(gTileSheet);
    LockPixels(spm);
    GrafPtr saveP; GetPort(&saveP);
    SetPort(dst);
    CopyBits((BitMap *) *spm,
             GetPortBitMapForCopyBits(GetWindowPort(dst)),
             &src, &dr, srcCopy, NULL);
    SetPort(saveP);
    UnlockPixels(spm);
}
