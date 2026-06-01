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
static short         gSheetRows     = 0;   /* tiles down in the sheet */
static short         gCursorClutIdx = -2;  /* cached farlook-cursor color index;
                                              -2 = unset, -1 = none, >=0 = index */

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
    NoPurgePixels(pm);   /* tile sheet stays resident */
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
    gSheetRows  = (frame.bottom - frame.top) / 16;
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
    gCursorClutIdx = -2;   /* recompute against a freshly reloaded sheet/ctable */
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

/* Index of a bright, NON-white tile-palette color for the farlook cursor.
   Scored R+G-B so it favors bright/warm (yellow) and deprioritizes white and
   blue; near-white entries are skipped outright (the white CLUT slot triggers
   a reorg).  Cached.  Callers use PmForeColor(index) — index-direct, no render.
   Returns -1 if no sheet/ctable or no usable color. */
short
mactile_cursor_clut_index(void)
{
    if (gCursorClutIdx == -2) {            /* -2 = not yet computed */
        gCursorClutIdx = -1;
        CTabHandle ct = mactile_sheet_ctable();
        if (ct && *ct) {
            short n = (**ct).ctSize;      /* ctSize is (count - 1) */
            long  bestscore = -0x7FFFFFFFL;
            short i;
            for (i = 0; i <= n; i++) {
                RGBColor c = (**ct).ctTable[i].rgb;
                if (c.red > 0xC000 && c.green > 0xC000 && c.blue > 0xC000)
                    continue;             /* skip near-white */
                long score = (long) c.red + (long) c.green - (long) c.blue;
                if (score > bestscore) { bestscore = score; gCursorClutIdx = i; }
            }
        }
    }
    return gCursorClutIdx;
}

void
mactile_blit_to(GWorldPtr dst, int tile_idx, short dst_x, short dst_y)
{
    if (!gTileSheet || !dst) return;
    /* Guard against generated tile.c expecting more tiles than the sheet
       actually contains (e.g., a divergence between tile2pict's output and
       tilemap.c's emitted indices). Out-of-bounds source rect would read
       stray PixMap memory. */
    if (tile_idx < 0 || tile_idx >= (int) gSheetCols * (int) gSheetRows) {
        mac_dprintf("mactile: tile_idx %d out of sheet (max %d)\n",
                    tile_idx, (int) gSheetCols * (int) gSheetRows - 1);
        return;
    }
    short sx = (tile_idx % gSheetCols) * 16;
    short sy = (tile_idx / gSheetCols) * 16;
    Rect src = { sy, sx, sy + 16, sx + 16 };
    Rect dr  = { dst_y, dst_x, dst_y + 16, dst_x + 16 };

    PixMapHandle spm = GetGWorldPixMap(gTileSheet);
    PixMapHandle dpm = GetGWorldPixMap(dst);
    if (!LockPixels(spm)) {
        mac_dprintf("mactile: LockPixels(sheet) failed (purged?)\n");
        return;
    }
    if (!LockPixels(dpm)) {
        mac_dprintf("mactile: LockPixels(dst) failed (purged?)\n");
        UnlockPixels(spm);
        return;
    }
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
    if (tile_idx < 0 || tile_idx >= (int) gSheetCols * (int) gSheetRows) {
        mac_dprintf("mactile: tile_idx %d out of sheet (max %d)\n",
                    tile_idx, (int) gSheetCols * (int) gSheetRows - 1);
        return;
    }
    short sx = (tile_idx % gSheetCols) * 16;
    short sy = (tile_idx / gSheetCols) * 16;
    Rect src = { sy, sx, sy + 16, sx + 16 };
    Rect dr  = { dst_y, dst_x, dst_y + 16, dst_x + 16 };

    PixMapHandle spm = GetGWorldPixMap(gTileSheet);
    if (!LockPixels(spm)) {
        mac_dprintf("mactile: LockPixels(sheet) failed (purged?)\n");
        return;
    }
    GrafPtr saveP; GetPort(&saveP);
    SetPort(dst);
    CopyBits((BitMap *) *spm,
             GetPortBitMapForCopyBits(GetWindowPort(dst)),
             &src, &dr, srcCopy, NULL);
    SetPort(saveP);
    UnlockPixels(spm);
}
