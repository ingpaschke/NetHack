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
    return 0;
}
