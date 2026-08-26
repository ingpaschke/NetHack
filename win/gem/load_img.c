/*
 * $NHDT-Date: 1432512809 2015/05/25 00:13:29 $  $NHDT-Branch: master $:$NHDT-Revision: 1.5 $
 */
#define __TCC_COMPAT__
#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include <gem.h>
#include <e_gem.h>
#include "load_img.h"

#ifndef FALSE
#define FALSE 0
#define TRUE !FALSE
#endif

/* VDI <-> Device palette order conversion matrixes: */
/* Four-plane vdi-device */
short vdi2dev4[] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
/* Two-plane vdi-device */
short vdi2dev2[] = { 0, 3, 1, 2 };

void
get_colors(int handle, short *palette, int col)
{
    int i, idx;

    /* get current color palette */
    for (i = 0; i < col; i++) {
        /* device->vdi->device palette order */
        switch (planes) {
        case 1:
            idx = i;
            break;
        case 2:
            idx = vdi2dev2[i];
            break;
        case 4:
            idx = vdi2dev4[i];
            break;
        default:
            if (i < 16)
                idx = vdi2dev4[i];
            else
                idx = i == 255 ? 1 : i;
        }
        vq_color(handle, i, 0, palette + idx * 3);
    }
}

void
img_set_colors_ex(int handle, short *palette, int col, int preserve_sys)
{
    int i, idx, end;

    end = min(1 << col, 1 << planes);
    /* preserve system colours (0-15) for tile palette on 256-colour displays,
       but allow full palette override for images like RIP.IMG */
    for (i = (preserve_sys && planes >= 8 && end > 16) ? 16 : 0; i < end; i++) {
        switch (planes) {
        case 1:
            idx = i;
            break;
        case 2:
            idx = vdi2dev2[i];
            break;
        case 4:
            idx = vdi2dev4[i];
            break;
        default:
            if (i < 16)
                idx = vdi2dev4[i];
            else
                idx = i == 255 ? 1 : i;
        }
        vs_color(handle, i, palette + idx * 3);
    }
}

void
img_set_colors(int handle, short *palette, int col)
{
    img_set_colors_ex(handle, palette, col, 1); /* preserve system by default */
}

/*
 * Convert a loaded (standard-format) image to the screen's device format.
 *
 * Instead of building a whole padded standard sheet AND a whole device
 * sheet at once (two full rasters -> a large scratch peak that overflows
 * a driver-loaded 4 MB machine at 8 planes), transform it a strip at a
 * time into a single pre-allocated device buffer: only one small
 * standard strip is materialised at any moment.  vr_trnfm() still does
 * the actual format conversion, so the result matches the screen's real
 * device format (planar or otherwise) exactly.
 */
int
convert(MFDB *image, long size)
{
    int plane, mplanes, sp;
    long new_size, line_bytes, dev_line_bytes, strip_bytes;
    long y0, sh;
    int strip_h = 16;              /* one tile row per strip */
    char *dev, *strip;
    MFDB src, dst;

    /* convert size from words to bytes (bytes per plane for whole image) */
    size <<= 1;

    if (image->fd_nplanes < 1)
        return FALSE;

    line_bytes = (long) image->fd_wdwidth << 1; /* bytes/plane/line */
    new_size = size * (long) planes;            /* full device raster */
    dev_line_bytes = new_size / (long) image->fd_h; /* device bytes/line */
    mplanes = min(image->fd_nplanes, planes);

    if ((dev = (char *) calloc(1, new_size)) == NULL)
        return (FALSE);

    strip_bytes = line_bytes * (long) strip_h * (long) planes;
    if ((strip = (char *) calloc(1, strip_bytes)) == NULL) {
        free(dev);
        return (FALSE);
    }

    for (y0 = 0; y0 < image->fd_h; y0 += strip_h) {
        sh = image->fd_h - y0;
        if (sh > strip_h)
            sh = strip_h;

        /* Build a standard-format strip: plane-sequential, one plane's
           sh lines after another, padded/replicated to `planes'. */
        memset(strip, 0, strip_bytes);
        for (plane = 0; plane < planes; plane++) {
            if (mplanes > 1) {
                if (plane >= image->fd_nplanes)
                    continue;       /* extra planes stay zero */
                sp = plane;
            } else {
                sp = 0;             /* mono: replicate plane 0 everywhere */
            }
            memcpy(strip + (long) plane * sh * line_bytes,
                   image->fd_addr
                       + ((long) y0 + (long) sp * image->fd_h) * line_bytes,
                   (size_t) (sh * line_bytes));
        }

        src = *image;
        src.fd_addr = (short *) strip;
        src.fd_h = (int) sh;
        src.fd_nplanes = planes;
        src.fd_stand = 1;                       /* standard */
        dst = src;
        dst.fd_addr = (short *) (dev + y0 * dev_line_bytes);
        dst.fd_stand = 0;                       /* device */

        vr_trnfm(x_handle, &src, &dst);
    }

    free(strip);
    free(image->fd_addr);

    image->fd_addr = dev;
    image->fd_stand = 0; /* device format */
    image->fd_nplanes = planes;
    return (TRUE);
}

int
transform_img(MFDB *image)
{ /* return FALSE if transform_img fails */
    int success;
    long size;

    if (!image->fd_addr)
        return (FALSE);

    size = (long) ((long) image->fd_wdwidth * (long) image->fd_h);
    success = convert(
        image, size); /* Use vr_trfm(), which needs quite a lot memory. */
    if (success)
        return (TRUE);
    /*	else				show_error(ERR_ALLOC);	*/
    return (FALSE);
}

/* Decode one RLE-packed scanline of one bitplane into `to' (which must
 * have room for `width' bytes).  Sets *scan_repeat if the stream asks for
 * this scanline to be repeated.  Returns 0 or an ERR_ code. */
static int
decode_scanline(FILE *fp, char *to, int width, int patt_len,
                int *scan_repeat)
{
    char *endline = to + width;
    char *pattern;
    int opcode, byte_repeat, patt_repeat;
    char sol_pat;

    do {
        opcode = fgetc(fp);
        if (opcode == EOF)
            return ERR_DEPACK;
        switch (opcode) {
        case 0: /* pattern or scan repeat */
            patt_repeat = fgetc(fp);
            if (patt_repeat == EOF)
                return ERR_DEPACK;
            if (patt_repeat) {
                if (to + (long) patt_len * patt_repeat > endline
                    || fread(to, patt_len, 1, fp) != 1)
                    return ERR_DEPACK;
                pattern = to;
                to += patt_len;
                while (--patt_repeat) {
                    memcpy(to, pattern, patt_len);
                    to += patt_len;
                }
            } else {
                if (fgetc(fp) != 0xFF)
                    return ERR_DEPACK;
                *scan_repeat = fgetc(fp);
                if (*scan_repeat == EOF || *scan_repeat == 0)
                    return ERR_DEPACK; /* 0 = no progress: malformed */
            }
            break;
        case 0x80: /* literal */
            byte_repeat = fgetc(fp);
            if (byte_repeat == EOF || to + byte_repeat > endline
                || fread(to, byte_repeat, 1, fp) != 1)
                return ERR_DEPACK;
            to += byte_repeat;
            break;
        default: /* solid run */
            byte_repeat = opcode & 0x7F;
            if (to + byte_repeat > endline)
                return ERR_DEPACK;
            sol_pat = (opcode & 0x80) ? (char) 0xFF : (char) 0x00;
            while (byte_repeat--)
                *to++ = sol_pat;
        }
    } while (to < endline);

    return (to == endline) ? 0 : ERR_DEPACK;
}

/*
 * Streaming loader for <=8-plane images (no palette reorder -- callers
 * that need reorder_tile_palette (4-plane tiles) must use depack_img +
 * transform_img instead).
 *
 * Decodes the packed IMG a line at a time and transforms it into the
 * device format 16 lines at a time, so the full standard-format sheet is
 * never held in RAM.  Peak use is one device sheet plus one small strip
 * -- roughly half of depack_img()+convert(), which matters on a
 * driver-loaded 4 MB machine at 8 planes.  On success `pic->addr' holds
 * the device raster (fd_stand implied 0) and the palette is loaded.
 */
int
load_img_streamed(char *name, IMG_header *pic)
{
    int word_aligned, width, pal_size, srcplanes, mplanes, patt_len;
    int strip_h = 16, sl, plane, r, scan_repeat, err = 0;
    long new_size, line_bytes, dev_line_bytes, out_line, strip_start;
    char *dev = NULL, *strip = NULL, *linebuf = NULL;
    MFDB src, dst;
    FILE *fp;

    if ((fp = fopen(name, "rb")) == NULL)
        return (ERR_FILE);
    setvbuf(fp, NULL, _IOFBF, BUFSIZ);

    if (fread((char *) &(pic->version), 2, 8 + 3, fp) != 8 + 3) {
        fclose(fp);
        return (ERR_HEADER);
    }
    if (pic->planes < 1 || pic->planes > 8 || pic->img_w <= 0
        || pic->img_h <= 0 || pic->pat_len <= 0 || pic->length <= 7) {
        fclose(fp);
        return (ERR_HEADER);
    }

    pic->palette = NULL;
    if (pic->magic == XIMG && pic->paltype == 0) {
        pal_size = (1 << pic->planes) * 3 * 2;
        if ((pic->palette = (short *) calloc(1, pal_size))) {
            if (fread((char *) pic->palette, 1, pal_size, fp)
                != (size_t) pal_size) {
                free(pic->palette);
                pic->palette = NULL;
                fclose(fp);
                return (ERR_FILE);
            }
        }
    }

    word_aligned = ((pic->img_w + 15) >> 4) << 1;
    width = (pic->img_w + 7) >> 3;
    line_bytes = word_aligned;
    patt_len = pic->pat_len;
    srcplanes = pic->planes;
    mplanes = min(srcplanes, planes);

    new_size = (long) word_aligned * (long) pic->img_h * (long) planes;
    dev_line_bytes = new_size / (long) pic->img_h;

    dev = (char *) calloc(1, new_size);
    strip = (char *) calloc(1, line_bytes * (long) strip_h * (long) planes);
    linebuf = (char *) calloc(1, line_bytes * (long) planes);
    if (!dev || !strip || !linebuf) {
        err = ERR_ALLOC;
        goto fail;
    }

    fseek(fp, (long) pic->length * 2L, SEEK_SET);

    out_line = 0;
    strip_start = 0;
    sl = 0;
    while (out_line < pic->img_h) {
        scan_repeat = 1;
        memset(linebuf, 0, line_bytes * (long) planes);
        for (plane = 0; plane < srcplanes; plane++) {
            err = decode_scanline(fp, linebuf + (long) plane * line_bytes,
                                  width, patt_len, &scan_repeat);
            if (err)
                goto fail;
        }
        if (mplanes == 1) { /* replicate the single b&w plane */
            for (plane = 1; plane < planes; plane++)
                memcpy(linebuf + (long) plane * line_bytes, linebuf,
                       line_bytes);
        }
        if (out_line + scan_repeat > pic->img_h)
            scan_repeat = (int) (pic->img_h - out_line);

        for (r = 0; r < scan_repeat; r++) {
            for (plane = 0; plane < planes; plane++)
                memcpy(strip + ((long) plane * strip_h + sl) * line_bytes,
                       linebuf + (long) plane * line_bytes, line_bytes);
            sl++;
            out_line++;
            if (sl == strip_h) {
                /* fill MFDB fields explicitly */
                src.fd_addr = (short *) strip;
                src.fd_w = (pic->img_w + 15) & ~15;
                src.fd_h = strip_h;
                src.fd_wdwidth = word_aligned >> 1;
                src.fd_nplanes = planes;
                src.fd_stand = 1;
                dst = src;
                dst.fd_addr = (short *) (dev + strip_start * dev_line_bytes);
                dst.fd_stand = 0;
                vr_trnfm(x_handle, &src, &dst);
                strip_start += strip_h;
                sl = 0;
            }
        }
    }

    if (sl > 0) { /* final partial strip: compact to sl-line plane stride */
        for (plane = 1; plane < planes; plane++)
            memmove(strip + (long) plane * sl * line_bytes,
                    strip + (long) plane * strip_h * line_bytes,
                    (size_t) (sl * line_bytes));
        /* fill MFDB fields explicitly */
        src.fd_addr = (short *) strip;
        src.fd_w = (pic->img_w + 15) & ~15;
        src.fd_h = sl;
        src.fd_wdwidth = word_aligned >> 1;
        src.fd_nplanes = planes;
        src.fd_stand = 1;
        dst = src;
        dst.fd_addr = (short *) (dev + strip_start * dev_line_bytes);
        dst.fd_stand = 0;
        vr_trnfm(x_handle, &src, &dst);
    }

    free(strip);
    free(linebuf);
    fclose(fp);

    free(pic->addr);
    pic->addr = dev;
    pic->planes = planes; /* now device-format at screen depth */
    return (0);

fail:
    free(dev);
    free(strip);
    free(linebuf);
    free(pic->palette);
    pic->palette = NULL;
    fclose(fp);
    return (err ? err : ERR_DEPACK);
}

/* Loads & depacks IMG (0 if succeded, else error). */
/* Bitplanes are one after another in address IMG_HEADER.addr. */
int
depack_img(char *name, IMG_header *pic)
{
    int b, line, plane, width, word_aligned, opcode, patt_len, pal_size,
        byte_repeat, patt_repeat, scan_repeat, error = FALSE;
    char *pattern, *to, *endline, *puffer, sol_pat;
    long size;
    FILE *fp;

    if ((fp = fopen(name, "rb")) == NULL)
        return (ERR_FILE);

    setvbuf(fp, NULL, _IOFBF, BUFSIZ);

    /* read header info (bw & ximg) into image structure */
    if (fread((char *) &(pic->version), 2, 8 + 3, fp) != 8 + 3) {
        error = ERR_HEADER;
        goto end_depack;
    }

    /* only 2-256 color imgs */
    if (pic->planes < 1 || pic->planes > 8) {
        error = ERR_COLOR;
        goto end_depack;
    }

    /* if XIMG, read info */
    pic->palette = NULL;
    if (pic->magic == XIMG && pic->paltype == 0) {
        pal_size = (1 << pic->planes) * 3 * 2;
        if ((pic->palette = (short *) calloc(1, pal_size))) {
            if (fread((char *) pic->palette, 1, pal_size, fp)
                != (size_t) pal_size) {
                error = ERR_FILE;
                goto end_depack;
            }
        }
    }

    /* width in bytes word aliged */
    word_aligned = (pic->img_w + 15) >> 4;
    word_aligned <<= 1;

    /* width byte aligned */
    width = (pic->img_w + 7) >> 3;

    /* allocate memory for the picture */
    free(pic->addr);
    pic->addr = NULL;
    size = (long) ((long) word_aligned * (long) pic->img_h
                   * (long) pic->planes); /*MAR*/

    /* check for header validity & malloc long... */
    if (pic->length > 7 && pic->planes < 33 && pic->img_w > 0
        && pic->img_h > 0 && pic->pat_len > 0) {
        if (!(pic->addr = (char *) calloc(1, size))) {
            error = ERR_ALLOC;
            goto end_depack;
        }
    } else {
        error = ERR_HEADER;
        goto end_depack;
    }

    patt_len = pic->pat_len;

    /* jump over the header and possible (XIMG) info */
    fseek(fp, (long) pic->length * 2L, SEEK_SET);

    for (line = 0, to = pic->addr; line < pic->img_h;
         line += scan_repeat) { /* depack whole img */
        for (plane = 0, scan_repeat = 1; plane < pic->planes;
             plane++) { /* depack one scan line */
            puffer = to =
                pic->addr
                + (long) (line + plane * pic->img_h) * (long) word_aligned;
            endline = puffer + width;
            do { /* depack one line in one bitplane */
                opcode = fgetc(fp);
                if (opcode == EOF) {
                    error = ERR_DEPACK;
                    goto end_depack;
                }
                switch (opcode) {
                case 0: /* pattern or scan repeat */
                    patt_repeat = fgetc(fp);
                    if (patt_repeat == EOF) {
                        error = ERR_DEPACK;
                        goto end_depack;
                    }
                    if (patt_repeat) { /* repeat a pattern */
                        if (to + (long) patt_len * patt_repeat > endline
                            || fread(to, patt_len, 1, fp) != 1) {
                            error = ERR_DEPACK;
                            goto end_depack;
                        }
                        pattern = to;
                        to += patt_len;
                        while (--patt_repeat) { /* copy pattern */
                            memcpy(to, pattern, patt_len);
                            to += patt_len;
                        }
                    } else { /* repeat a line */
                        if (fgetc(fp) != 0xFF) {
                            error = ERR_DEPACK;
                            goto end_depack;
                        }
                        scan_repeat = fgetc(fp);
                        if (scan_repeat == EOF || scan_repeat == 0) {
                            error = ERR_DEPACK; /* 0 = no progress */
                            goto end_depack;
                        }
                    }
                    break;
                case 0x80: /* Literal */
                    byte_repeat = fgetc(fp);
                    if (byte_repeat == EOF
                        || to + byte_repeat > endline
                        || fread(to, byte_repeat, 1, fp) != 1) {
                        error = ERR_DEPACK;
                        goto end_depack;
                    }
                    to += byte_repeat;
                    break;
                default: /* Solid run */
                    byte_repeat = opcode & 0x7F;
                    if (to + byte_repeat > endline) {
                        error = ERR_DEPACK;
                        goto end_depack;
                    }
                    sol_pat = opcode & 0x80 ? 0xFF : 0x00;
                    while (byte_repeat--)
                        *to++ = sol_pat;
                }
            } while (to < endline);

            if (to == endline) {
                /* ensure that lines aren't repeated past the end of the img
                 */
                if (line + scan_repeat > pic->img_h)
                    scan_repeat = pic->img_h - line;
                /* copy line to image buffer */
                if (scan_repeat > 1) {
                    /* calculate address of a current line in a current
                     * bitplane */
                    /*					to=pic->addr+(long)(line+1+plane*pic->img_h)*(long)word_aligned;*/
                    for (b = scan_repeat - 1; b; --b) {
                        memcpy(to, puffer, width);
                        to += word_aligned;
                    }
                }
            } else {
                error = ERR_DEPACK;
                goto end_depack;
            }
        }
    }

end_depack:
    if (error) {
        free(pic->palette);
        pic->palette = NULL;
        free(pic->addr);
        pic->addr = NULL;
    }
    fclose(fp);
    return (error);
}
