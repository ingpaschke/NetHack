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
