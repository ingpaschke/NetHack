/* txt2iff_host.c - host-side .txt -> Amiga BMAP IFF tile converter.
 *
 * Used during the m68k-AmigaOS cross-build to produce tiles/monsters.iff,
 * tiles/objects.iff, and tiles/other.iff for the amitile (WINVERS_AMIV)
 * window port.
 *
 * This file replaces sys/amiga/txt2iff.c for the host-side build.
 * txt2iff.c uses AmigaOS IFFParse library calls (AllocIFF, PushChunk,
 * WriteChunkBytes, etc.); this version writes the same BMAP IFF format
 * using POSIX file I/O with explicit big-endian output.
 *
 * The BMAP IFF format is a custom variant of ILBM used by the Amiga
 * NetHack tile loader (winchar.c).  Chunk layout:
 *
 *   FORM <size> BMAP
 *     BMHD <20>            - bitmap header (dimensions, depth)
 *     CAMG <4>             - AmigaOS view mode (HIRES|LACE)
 *     CMAP <colors*3>      - RGB palette
 *     PDAT <28>            - NetHack-specific tile layout descriptor
 *     PLNE <planes*pbytes> - uncompressed, deinterleaved bitplane data
 *
 * Linked with: win/share/tiletext.c, win/share/tilemap.c (as tiletxt),
 *              src/drawing.c, src/decl.c, src/monst.c, src/objects.c,
 *              src/alloc.c, util/panic.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "tile.h"

/* fopen_text_file, read_text_tile, fclose_text_file, ColorMap, colorsinmap
 * are all declared in tile.h (included above via config.h). */

/* Required by tiletext.c / tilemap.c */
void
panic(const char *msg)
{
    fprintf(stderr, "PANIC: %s\n", msg);
    exit(1);
}

/* ------------------------------------------------------------------
 * Tile layout
 * ------------------------------------------------------------------ */

#define COLS 20  /* tiles per row in the output image */

/* ------------------------------------------------------------------
 * Color reordering — identical to sys/amiga/txt2iff.c
 * ------------------------------------------------------------------ */

/* Color reorder table — MAXCOLORMAPSIZE entries.
 * Indices 0-15: map each source tile color to the Amiga pen that matches its
 * semantic role, so that UI windows (which use C_BLACK=0, C_WHITE=1, etc.)
 * display with the correct palette entries after the IFF CMAP is loaded.
 *
 * Source palette order (monsters/objects/other.txt):
 *   0 .  (71,108,108) floor/bg  -> pen 12 C_GREYBLUE
 *   1 A  (  0,  0,  0) black    -> pen  0 C_BLACK
 *   2 B  (  0,182,255) cyan     -> pen  2 C_CYAN
 *   3 C  (255,108,  0) orange   -> pen  3 C_ORANGE
 *   4 D  (255,  0,  0) red      -> pen  7 C_RED
 *   5 E  (  0,  0,255) blue     -> pen  4 C_BLUE
 *   6 F  (  0,145,  0) green    -> pen  5 C_GREEN
 *   7 G  (108,255,  0) ltgreen  -> pen  8 C_LTGREEN
 *   8 H  (255,255,  0) yellow   -> pen  9 C_YELLOW
 *   9 I  (255,  0,255) magenta  -> pen 10 C_MAGENTA
 *  10 J  (145, 71,  0) brown    -> pen 11 C_BROWN
 *  11 K  (204, 79,  0) ltbrown  -> pen 13 C_LTBROWN
 *  12 L  (255,182,145) peach    -> pen 15 C_PEACH
 *  13 M  (237,237,237) ltgrey   -> pen 14 C_LTGREY
 *  14 N  (255,255,255) white    -> pen  1 C_WHITE
 *  15 O  (215,215,215) grey     -> pen  6 C_GREY
 * Indices >= 16: identity mapping for the extra colours in 5-plane images. */
static int colrmap[MAXCOLORMAPSIZE];

static void
map_colors(void)
{
    static const int tmpmap[16] = { 12, 0, 2, 3, 7, 4, 5, 8, 9, 10, 11, 13, 15, 14, 1, 6 };
    int x;
    for (x = 0; x < 16; x++)
        colrmap[x] = tmpmap[x];
    for (x = 16; x < MAXCOLORMAPSIZE; x++)
        colrmap[x] = x;
}

static int
findcolor(pixel *pix)
{
    int i;
    /* Search only the colours actually defined in this file */
    for (i = 0; i < colorsinmap; ++i)
        if (pix->r == ColorMap[CM_RED][i]
         && pix->g == ColorMap[CM_GREEN][i]
         && pix->b == ColorMap[CM_BLUE][i])
            return i;
    return -1;
}

/* Pack one tile into the (already zero-initialised) bitplane buffers. */
static void
packbody(pixel (*tile)[TILE_X], char **planes,
         int tileno, int nplanes, int rowbytes)
{
    int i, j, k;
    int yoff = ((tileno / COLS) * TILE_Y) * rowbytes;
    int xoff =  (tileno % COLS) * (TILE_X / 8);

    for (i = 0; i < TILE_Y; ++i) {
        for (k = 0; k < nplanes; ++k) {
            int mask = 1 << k;
            char *buf = planes[k] + yoff + xoff + i * rowbytes;
            for (j = 0; j < TILE_X; j++) {
                int col = findcolor(&tile[i][j]);
                if (col < 0) {
                    fprintf(stderr,
                            "error: unmapped pixel color at tile %d, "
                            "row %d, col %d\n", tileno, i, j);
                    return;
                }
                col = colrmap[col];
                buf[j / 8] |= (((col & mask) != 0) << (7 - (j % 8)));
            }
        }
    }
}

/* ------------------------------------------------------------------
 * Big-endian IFF output helpers
 * ------------------------------------------------------------------ */

static FILE *iff_out;

static void
wr32(uint32_t v)
{
    fputc((v >> 24) & 0xff, iff_out);
    fputc((v >> 16) & 0xff, iff_out);
    fputc((v >>  8) & 0xff, iff_out);
    fputc( v        & 0xff, iff_out);
}

/* Write a complete IFF chunk: 4-char ID, 4-byte big-endian size, data.
 * IFF requires chunk data to be padded to an even byte count; all chunk
 * sizes produced here are already even, so no padding byte is needed. */
static void
write_chunk(const char *id, const void *data, uint32_t size)
{
    fwrite(id,   1, 4,    iff_out);
    wr32(size);
    fwrite(data, 1, size, iff_out);
    if (size & 1)
        fputc(0, iff_out);
}

/* ------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------ */

int
main(int argc, char **argv)
{
    pixel    pixels[TILE_Y][TILE_X];
    int      i, ntiles, nplanes, colors;
    int      rows, rowbytes;
    uint32_t pbytes, plne_size, form_size;
    char   **planes;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s source.txt destination.iff\n", argv[0]);
        return 1;
    }

    /* ---- Pass 1: count tiles; this also populates ColorMap ---- */
    if (fopen_text_file(argv[1], "r") != TRUE) {
        perror(argv[1]);
        return 1;
    }
    ntiles = 0;
    while (read_text_tile(pixels) == TRUE)
        ++ntiles;
    fclose_text_file();

    if (ntiles == 0) {
        fprintf(stderr, "No tiles found in %s\n", argv[1]);
        return 1;
    }

    /* nplanes = ceil(log2(colorsinmap)) */
    nplanes = 0;
    i = colorsinmap - 1;
    while (i > 0) { nplanes++; i >>= 1; }

    map_colors();
    colors   = 1 << nplanes;
    rows     = (ntiles + COLS - 1) / COLS;
    rowbytes = ((COLS * TILE_X + 15) / 16) * 2;  /* scanline, word-aligned */
    pbytes   = (uint32_t)rowbytes * (uint32_t)(rows * TILE_Y);

    /* Allocate zero-initialised bitplane buffers */
    planes = malloc(nplanes * sizeof(char *));
    if (!planes) { perror("malloc"); return 1; }
    for (i = 0; i < nplanes; ++i) {
        planes[i] = calloc(1, pbytes);
        if (!planes[i]) { perror("calloc"); return 1; }
    }

    /* ---- Pass 2: pack tiles into bitplane buffers ---- */
    if (fopen_text_file(argv[1], "r") != TRUE) {
        perror(argv[1]);
        return 1;
    }
    i = 0;
    while (read_text_tile(pixels) == TRUE) {
        packbody(pixels, planes, i, nplanes, rowbytes);
        if (i % 20 == 0) { printf("%d..", i); fflush(stdout); }
        ++i;
    }
    fclose_text_file();
    printf("\n%d tiles converted\n", ntiles);

    /* ---- Write BMAP IFF file ---- */
    iff_out = fopen(argv[2], "wb");
    if (!iff_out) { perror(argv[2]); return 1; }

    /* Pre-compute FORM size.
     * FORM size = everything after FORM's own size field:
     *   4  (BMAP type tag)
     *   8 + 20  (BMHD chunk header + data)
     *   8 +  4  (CAMG)
     *   8 + colors*3  (CMAP; colors is always a power of 2 >= 2, so
     *                  colors*3 is always even — no pad byte needed)
     *   8 + 28  (PDAT: 7 x uint32_t)
     *   8 + nplanes*pbytes  (PLNE; pbytes is always even)
     */
    plne_size = (uint32_t)nplanes * pbytes;
    form_size = 4
              + (8 + 20)
              + (8 +  4)
              + (8 + (uint32_t)colors * 3)
              + (8 + 28)
              + (8 + plne_size);

    /* FORM header */
    fwrite("FORM", 1, 4, iff_out);
    wr32(form_size);
    fwrite("BMAP", 1, 4, iff_out);

    /* BMHD chunk */
    {
        uint8_t bmhd[20];
        uint8_t *p = bmhd;
        uint16_t w = (uint16_t)(COLS * TILE_X);
        uint16_t h = (uint16_t)(rows * TILE_Y);

        p[0] = w >> 8;  p[1] = w & 0xff;  p += 2;  /* w */
        p[0] = h >> 8;  p[1] = h & 0xff;  p += 2;  /* h */
        memset(p, 0, 4);                   p += 4;  /* x=0, y=0 */
        *p++ = (uint8_t)nplanes;                    /* nPlanes */
        *p++ = 0;                                    /* masking: none */
        *p++ = 0;                                    /* compression: none */
        *p++ = 0;                                    /* reserved1 */
        p[0] = 0; p[1] = 0;               p += 2;  /* transparentColor */
        *p++ = 100;                                  /* xAspect */
        *p++ = 100;                                  /* yAspect */
        p[0] = 0; p[1] = TILE_X;          p += 2;  /* pageWidth */
        p[0] = 0; p[1] = TILE_Y;          p += 2;  /* pageHeight */

        write_chunk("BMHD", bmhd, 20);
    }

    /* CAMG chunk: HIRES | LACE = 0x00008004 */
    {
        uint8_t camg[4] = { 0x00, 0x00, 0x80, 0x04 };
        write_chunk("CAMG", camg, 4);
    }

    /* CMAP chunk */
    {
        uint8_t *cmap = calloc(colors, 3);
        if (!cmap) { perror("calloc"); return 1; }
        for (i = 0; i < colors; ++i) {
            cmap[colrmap[i] * 3 + 0] = (uint8_t)ColorMap[CM_RED][i];
            cmap[colrmap[i] * 3 + 1] = (uint8_t)ColorMap[CM_GREEN][i];
            cmap[colrmap[i] * 3 + 2] = (uint8_t)ColorMap[CM_BLUE][i];
        }
        write_chunk("CMAP", cmap, (uint32_t)colors * 3);
        free(cmap);
    }

    /* PDAT chunk: 7 x uint32_t big-endian
     * Matches the PDAT struct in sys/amiga/windefs.h:
     *   nplanes, pbytes, across, down, npics, xsize, ysize */
    {
        uint32_t vals[7];
        uint8_t  pdat[28];
        uint8_t *p = pdat;

        vals[0] = (uint32_t)nplanes;
        vals[1] = pbytes;
        vals[2] = COLS;
        vals[3] = (uint32_t)rows;
        vals[4] = (uint32_t)ntiles;
        vals[5] = TILE_X;
        vals[6] = TILE_Y;

        for (i = 0; i < 7; i++) {
            p[0] = (vals[i] >> 24) & 0xff;
            p[1] = (vals[i] >> 16) & 0xff;
            p[2] = (vals[i] >>  8) & 0xff;
            p[3] =  vals[i]        & 0xff;
            p += 4;
        }
        write_chunk("PDAT", pdat, 28);
    }

    /* PLNE chunk: concatenated bitplane data */
    fwrite("PLNE", 1, 4, iff_out);
    wr32(plne_size);
    for (i = 0; i < nplanes; ++i)
        fwrite(planes[i], 1, pbytes, iff_out);
    if (plne_size & 1)
        fputc(0, iff_out);

    fclose(iff_out);

    for (i = 0; i < nplanes; ++i) free(planes[i]);
    free(planes);
    return 0;
}
