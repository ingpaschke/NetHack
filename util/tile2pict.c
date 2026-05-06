/* NetHack 5.0 tile2pict.c
 * Host tool: emit a Rez source file containing PICT v2 resources for the
 * Mac 68k port. Sibling of tile2bmp.c, sharing tiletext.c.
 */

#include "config.h"
#include "hacklib.h"
#include "tile.h"

extern void monst_globals_init(void);
extern void objects_globals_init(void);

static const char *const relative_tiledir = "../win/share/";
static const char *const tilefilenames[3] = {
    "monsters.txt", "objects.txt", "other.txt"
};

#define TILES_PER_ROW 30   /* 30 * 16 = 480 px wide sheet */

static pixel tilepixels[TILE_Y][TILE_X];

static int sheet_w, sheet_h;
static unsigned char *sheet;     /* 8bpp pixels, row-major top-down */

static void
paste_tile(int tile_index)
{
    int sx = (tile_index % TILES_PER_ROW) * TILE_X;
    int sy = (tile_index / TILES_PER_ROW) * TILE_Y;
    int x, y, c;
    for (y = 0; y < TILE_Y; ++y) {
        for (x = 0; x < TILE_X; ++x) {
            for (c = 0; c < colorsinmap; ++c) {
                if (ColorMap[CM_RED][c] == tilepixels[y][x].r
                    && ColorMap[CM_GREEN][c] == tilepixels[y][x].g
                    && ColorMap[CM_BLUE][c] == tilepixels[y][x].b)
                    break;
            }
            if (c >= colorsinmap) c = 0; /* fallback */
            sheet[(sy + y) * sheet_w + (sx + x)] = (unsigned char) c;
        }
    }
}

int
main(int argc, char *argv[])
{
    int total_tiles = 0, i;
    char path[256];

    if (argc != 2) {
        fprintf(stderr, "usage: %s outfile.r\n", argv[0]);
        return 1;
    }
    objects_globals_init();
    monst_globals_init();

    /* Pass 1: count tiles. */
    for (i = 0; i < 3; ++i) {
        snprintf(path, sizeof path, "%s%s", relative_tiledir, tilefilenames[i]);
        if (!fopen_text_file(path, RDTMODE)) {
            fprintf(stderr, "cannot open %s\n", path);
            return 1;
        }
        while (read_text_tile(tilepixels))
            ++total_tiles;
        fclose_text_file();
    }
    fprintf(stderr, "tile2pict: %d tiles total\n", total_tiles);

    /* Pass 2: allocate sheet and paste tiles. */
    sheet_w = TILES_PER_ROW * TILE_X;
    sheet_h = ((total_tiles + TILES_PER_ROW - 1) / TILES_PER_ROW) * TILE_Y;
    sheet = calloc((size_t) sheet_w * sheet_h, 1);
    if (!sheet) { fprintf(stderr, "out of memory\n"); return 1; }

    {
        int placed = 0;
        for (i = 0; i < 3; ++i) {
            snprintf(path, sizeof path, "%s%s", relative_tiledir, tilefilenames[i]);
            if (!fopen_text_file(path, RDTMODE)) {
                fprintf(stderr, "cannot open %s on second pass\n", path);
                return 1;
            }
            while (read_text_tile(tilepixels))
                paste_tile(placed++);
            fclose_text_file();
        }
    }
    fprintf(stderr, "tile2pict: built %dx%d 8bpp sheet (%d colors)\n",
            sheet_w, sheet_h, colorsinmap);
    return 0;
}
