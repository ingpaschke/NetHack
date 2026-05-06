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

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s outfile.r\n", argv[0]);
        return 1;
    }
    objects_globals_init();
    monst_globals_init();
    /* Real work in later tasks. */
    fprintf(stderr, "tile2pict: skeleton (writes nothing yet)\n");
    return 0;
}
