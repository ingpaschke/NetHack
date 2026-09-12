/* t8gem - GEM host for transputer NetHack: boot the .btl over the
 * ATW link, then serve the iserver protocol with the windowproc RPC
 * rendered through the GEM engine (rpcgem.c + win/gem/wingem1.c).
 *
 * Usage:  t8gem [-a HEXADDR] [-v] image.btl
 * Run it from the game directory (dat files, save/).
 */

#include <stdio.h>
#include <stdlib.h>
#include <mint/osbind.h>
#include <gem.h>
#include <e_gem.h>
#include "host.h"
#include "spserver.h"
#include "tbios.h"
#include "t8boot_mint.h"

extern void gemsrv_register(void);
extern void gemsrv_set_log(void (*fn)(const char *));
extern void rpcgem_register(void);      /* rpcgem.c: the windowport handlers */
#include "t8call_srv.h"

static void log_line(const char *line)
{
    FILE *lg = T8LOG_OPEN("a");
    if (lg) { fprintf(lg, "%s\n", line); fclose(lg); }
}

/* ---- GEM boot progress bar ---------------------------------------
   plain VDI on a form_dial-reserved rectangle; FMD_FINISH hands the
   area back to the desktop before the game's windows open */

static void crumb(const char *what, long v)
{
    FILE *lg = T8LOG_OPEN("a");
    if (lg) { fprintf(lg, "%s %ld\n", what, v); fclose(lg); }
}
static GRECT pb_box;
static int pb_on;
static int pb_v;                        /* Valkyrie tile position 0..N-3 */
static int pb_done;                     /* upload finished -> payoff frame */
static int pb_tiles_ok;                 /* tile sheet loaded */

/* Sokoban push: the Valkyrie shoves the boulder rightward into the pit;
   at 100% the pit is filled and she stands on the downstairs (game boots).
   Tile indices into NH16.IMG (20 tiles/row, 16x16). */
#define T_VALK     699
#define T_BOULDER  1266
#define T_FLOOR    1291
#define T_STAIRS   1298
#define T_PIT      1335
#define STRIP_N    16
#define TW         16                   /* NH16 tile pixel size */

extern int  mar_boot_tiles(void);       /* wingem1.c: load the tile sheet */
extern void mar_boot_blit_tile(int idx, int dx, int dy);
extern void mar_boot_blit_tile2(int idx, int dx, int dy, MFDB *dst);

#define STRIP_W (STRIP_N * TW)
#define PB_W    (STRIP_W + 24)
#define PB_H    70

static int strip_x(void) { return pb_box.g_x + (PB_W - STRIP_W) / 2; }
static int strip_y(void) { return pb_box.g_y + PB_H - TW - 12; }

/* draw the strip for the current progress into `dst' at pixel (ox,oy);
   floor cells overwrite the Valkyrie/boulder's previous positions */
static void pb_draw_strip_to(MFDB *dst, int ox, int oy)
{
    int i, t;
    if (!pb_tiles_ok)
        return;
    for (i = 0; i < STRIP_N; i++) {
        t = T_FLOOR;
        if (pb_done) {
            if (i == STRIP_N - 1)      t = T_VALK;    /* on the downstairs */
            /* the pit (N-2) is filled now, so it stays floor */
        } else {
            if (i == STRIP_N - 1)      t = T_STAIRS;
            else if (i == STRIP_N - 2) t = T_PIT;
            else if (i == pb_v)        t = T_VALK;
            else if (i == pb_v + 1)    t = T_BOULDER;
        }
        mar_boot_blit_tile2(t, ox + i * TW, oy, dst);
    }
}

static void pb_draw_strip(void)
{
    pb_draw_strip_to((MFDB *) 0, strip_x(), strip_y()); /* NULL dst => screen */
}

static void pb_frame(void);
static long read_hz200(void);

static void prog_open(void)
{
    static char rscname[] = "gem_rsc.rsc", appname[] = "NetHack";
    short xy[10];
    int tries = 0, iret = 0;
    long t0, t1, t2;
    t0 = Supexec(read_hz200);
    /* open_rsc() brings GEM up (workstation, x_handle, planes, screen
       MFDB); mar_gem_init's later open_rsc() then passes straight through */
    if (x_handle == 0)
        open_rsc(rscname, appname, appname, appname, appname, 0, 0, 0);
    {
        FILE *lg = T8LOG_OPEN("a");
        if (lg) {
            fprintf(lg, "prog_open: tries=%d iret=%d ap_id=%d handle=%d "
                    "planes=%d screen=%ld desk=%d,%d %dx%d\n", tries, iret,
                    ap_id, x_handle, planes, (long) screen,
                    desk.g_x, desk.g_y, desk.g_w, desk.g_h);
            fclose(lg);
        }
    }
    if (x_handle == 0)
        return;
    t1 = Supexec(read_hz200);
    {
        extern int mar_boot_planes(void), mar_boot_tpl(void);
        /* the game's own tile load pulled forward; load_tile_image caches it */
        int r = mar_boot_tiles();
        FILE *lg = T8LOG_OPEN("a");
        pb_tiles_ok = (r == 0);
        t2 = Supexec(read_hz200);
        if (lg) {
            fprintf(lg, "boot tiles: r=%d planes=%d tpl=%d\n",
                    r, mar_boot_planes(), mar_boot_tpl());
            fprintf(lg, "startup timing: gem %ld.%lds tiles %ld.%lds (200Hz)\n",
                    (t1 - t0) / 200, ((t1 - t0) % 200) / 20,
                    (t2 - t1) / 200, ((t2 - t1) % 200) / 20);
            fclose(lg);
        }
    }
    {
        GRECT dw = desk;
        if (dw.g_w < PB_W || dw.g_h < PB_H)
            wind_get(0, WF_WORKXYWH, &dw.g_x, &dw.g_y, &dw.g_w, &dw.g_h);
        if (dw.g_w < PB_W || dw.g_h < PB_H) {
            dw.g_x = 0; dw.g_y = 0; dw.g_w = 640; dw.g_h = 400;
        }
        pb_box.g_w = PB_W;
        pb_box.g_h = PB_H;
        pb_box.g_x = dw.g_x + (dw.g_w - PB_W) / 2;
        pb_box.g_y = dw.g_y + (dw.g_h - PB_H) / 2;
    }
    form_dial(FMD_START, pb_box.g_x, pb_box.g_y, pb_box.g_w, pb_box.g_h,
              pb_box.g_x, pb_box.g_y, pb_box.g_w, pb_box.g_h);
    graf_mouse(BUSYBEE, NULL);
    pb_v = 0;
    pb_done = 0;
    pb_on = 1;
    pb_frame();
}

/* ---- offscreen double buffer ------------------------------------
   pb_base holds the static panel grabbed from the screen once; pb_work
   is composed each frame (base + tile strip) and blitted whole, so the
   text survives desktop repaints and nothing flickers */
static MFDB pb_base, pb_work;
static int pb_buf_ok;

static void pb_setup_mfdb(MFDB *m)
{
    short wdw = (PB_W + 15) >> 4;        /* words per scanline */
    m->fd_addr = (short *) malloc((long) wdw * 2 * PB_H * planes);
    m->fd_w = wdw << 4;
    m->fd_h = PB_H;
    m->fd_wdwidth = wdw;
    m->fd_stand = 0;                    /* device-specific format */
    m->fd_nplanes = planes;
    m->fd_r1 = m->fd_r2 = m->fd_r3 = 0;
}

/* draw the static panel (bg + frame + text, no strip) straight to screen */
static void pb_paint_static(void)
{
    short xy[10];
    xy[0] = 0; xy[1] = 0; xy[2] = 32767; xy[3] = 32767;
    vs_clip(x_handle, 0, xy);           /* no clipping */
    vsf_interior(x_handle, FIS_SOLID);
    vsf_color(x_handle, G_WHITE);
    xy[0] = pb_box.g_x; xy[1] = pb_box.g_y;
    xy[2] = pb_box.g_x + PB_W - 1; xy[3] = pb_box.g_y + PB_H - 1;
    v_bar(x_handle, xy);
    vsf_color(x_handle, G_BLACK);
    xy[0] = pb_box.g_x; xy[1] = pb_box.g_y;
    xy[2] = pb_box.g_x + PB_W - 1; xy[3] = pb_box.g_y;
    xy[4] = pb_box.g_x + PB_W - 1; xy[5] = pb_box.g_y + PB_H - 1;
    xy[6] = pb_box.g_x; xy[7] = pb_box.g_y + PB_H - 1;
    xy[8] = pb_box.g_x; xy[9] = pb_box.g_y;
    v_pline(x_handle, 5, xy);
    {   /* system font at its native 13pt (8x16), h-centered at the top;
           the native point size keeps VDI from scaling */
        static char msg[] = "Pushing the Transputer executable";
        short sad, cw, ch, cellw, cellh;
        vst_font(x_handle, 1);
        vst_point(x_handle, 13, &cw, &ch, &cellw, &cellh);
        vst_alignment(x_handle, 1, 5, &sad, &sad);   /* h-center, v-top */
        vst_color(x_handle, G_BLACK);
        v_gtext(x_handle, pb_box.g_x + PB_W / 2, pb_box.g_y + 3, msg);
    }
}

/* draw the static panel once and grab it into pb_base */
static void pb_build_bufs(void)
{
    short pxy[8];
    if (!pb_base.fd_addr)
        pb_setup_mfdb(&pb_base);
    if (!pb_work.fd_addr)
        pb_setup_mfdb(&pb_work);
    if (!pb_base.fd_addr || !pb_work.fd_addr)
        return;                         /* alloc failed -> direct-draw path */
    graf_mouse(M_OFF, NULL);
    pb_paint_static();
    pxy[0] = pb_box.g_x; pxy[1] = pb_box.g_y;
    pxy[2] = pb_box.g_x + PB_W - 1; pxy[3] = pb_box.g_y + PB_H - 1;
    pxy[4] = 0; pxy[5] = 0; pxy[6] = PB_W - 1; pxy[7] = PB_H - 1;
    vro_cpyfm(x_handle, S_ONLY, pxy, screen, &pb_base);   /* screen -> base */
    graf_mouse(M_ON, NULL);
    pb_buf_ok = 1;
}

/* compose base + current strip in pb_work, blit the whole panel to screen */
static void pb_frame(void)
{
    short pxy[8];
    if (!pb_buf_ok)
        pb_build_bufs();
    if (!pb_buf_ok) {                   /* no buffers: draw straight */
        pb_paint_static();
        pb_draw_strip();
        return;
    }
    pxy[0] = 0; pxy[1] = 0; pxy[2] = PB_W - 1; pxy[3] = PB_H - 1;
    pxy[4] = 0; pxy[5] = 0; pxy[6] = PB_W - 1; pxy[7] = PB_H - 1;
    vro_cpyfm(x_handle, S_ONLY, pxy, &pb_base, &pb_work);   /* base -> work */
    pb_draw_strip_to(&pb_work, (PB_W - STRIP_W) / 2, PB_H - TW - 12);
    pxy[4] = pb_box.g_x; pxy[5] = pb_box.g_y;
    pxy[6] = pb_box.g_x + PB_W - 1; pxy[7] = pb_box.g_y + PB_H - 1;
    vro_cpyfm(x_handle, S_ONLY, pxy, &pb_work, screen);     /* work -> screen */
}

static void prog_step(long done, long total)
{
    int v;
    if (!pb_on || total <= 0)
        return;
    v = (int) ((done * (long) (STRIP_N - 3)) / total);   /* 0 .. N-3 */
    if (v < 0)
        v = 0;
    if (v > STRIP_N - 3)
        v = STRIP_N - 3;
    if (v == pb_v)              /* blit only when the Valkyrie advances */
        return;
    pb_v = v;
    pb_frame();
}

/* upload done: fill the pit, put the Valkyrie on the downstairs, and
   hold the payoff frame briefly before the game's windows take over */
static void prog_finish(void)
{
    volatile long d;
    if (!pb_on)
        return;
    pb_done = 1;
    pb_frame();
    for (d = 0; d < 1200000L; d++)
        ;
}

static void prog_close(void)
{
    if (!pb_on)
        return;
    graf_mouse(ARROW, NULL);
    form_dial(FMD_FINISH, pb_box.g_x, pb_box.g_y, pb_box.g_w, pb_box.g_h,
              pb_box.g_x, pb_box.g_y, pb_box.g_w, pb_box.g_h);
    pb_on = 0;
}

/* pre-GEM failures: the buffered console never shows, so shout on
   the TOS text screen, log, and wait for a key */
static void fail(const char *msg)
{
    FILE *lg = T8LOG_OPEN("a");
    if (lg) {
        fprintf(lg, "FAIL: %s\n", msg);
        fclose(lg);
    }
    (void)Cconws("t8gem: ");
    (void)Cconws(msg);
    (void)Cconws("\r\n-- key --\r\n");
    (void)Crawcin();
}

static long read_hz200(void)
{
    return *(volatile long *)0x4BAL;    /* _hz_200, supervisor only */
}

/* the loaders draw garbage on a missing tile/resource file; check up
   front.  Returns the missing name or 0. */
static const char *missing_asset(void)
{
    static const char *req[] = {
        "GEM_RSC.RSC", "NH16.IMG", "NH32.IMG", "NH2.IMG", 0
    };
    int i;
    for (i = 0; req[i]; i++) {
        FILE *f = fopen(req[i], "rb");
        if (!f)
            return req[i];
        fclose(f);
    }
    return 0;
}

static void asset_alert(const char *name)
{
    char buf[128];
    FILE *lg = T8LOG_OPEN("a");
    if (lg) { fprintf(lg, "FATAL: missing host file %s\n", name); fclose(lg); }
    /* [3] = stop icon; keep lines <= 30 chars (AES limit) */
    sprintf(buf, "[3][NetHack GEM host:|missing file  %s|Copy the .IMG and .RSC|files next to NETHACK.PRG][ Quit ]",
            name);
    (void)form_alert(1, buf);
}

int main(int argc, char **argv)
{
    unsigned long base = T8BOOT_BASE_AUTO;
    const char *path = 0;
    long i;
    int rc, fpga_init = 0;
    int memboot = 0;                    /* -m or a MEMBOOT flag: memory boot */

#ifdef T8_RELEASE
    t8log_name = 0;                     /* no t8log.txt in a release build */
#endif
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n') {
            sp_no_ring = 1;             /* -noring */
        } else if (argv[i][0] == '-' && argv[i][1] == 'v' && !argv[i][2]) {
            base = T8BOOT_BASE_VIRTUAL;
        } else if (argv[i][0] == '-' && argv[i][1] == 'm' && !argv[i][2]) {
            memboot = 1;             /* mkboot -Z image via shared memory */
        } else if (argv[i][0] == '-' && argv[i][1] == 'L' && !argv[i][2]) {
            fpga_init = 1;              /* -L: fpgabios +0x20 link-adapter init */
        } else if (argv[i][0] == '-' && argv[i][1] == 'l' && !argv[i][2]) {
            memboot = 0;             /* force link boot (A/B) */
        } else if (argv[i][0] == '-' && argv[i][1] == 'a' && i + 1 < argc) {
            base = strtoul(argv[++i], 0, 16);
        } else if (argv[i][0] != '-' && !path) {
            path = argv[i];
        } else {
            host_puts("usage: NETHACK [-a HEXADDR] [-m] image\r\n");
            return 1;
        }
    }
    if (!path)
        path = memboot ? "nethack.btz"   /* desktop launch: packed image */
                       : "nethack.btl"; /* -l with no image: link image */

    {   /* flag files next to the PRG for desktop launches without args */
        FILE *nf = fopen("NORING", "r");
        if (nf) {
            fclose(nf);
            sp_no_ring = 1;
        }
        nf = fopen("LINKBOOT", "r");
        if (nf) {
            fclose(nf);
            memboot = 0;             /* desktop opt-out of memory boot */
            path = "nethack.btl";    /* link boot needs a -z/link image */
        }
        nf = fopen("MEMBOOT", "r");
        if (nf) {
            fclose(nf);
            memboot = 1;             /* T425/FPGA: shared-memory boot */
            path = "nethack.btz";
        }
    }

    {   /* fail legibly on a missing host file */
        const char *miss = missing_asset();
        if (miss) {
            asset_alert(miss);
            return 1;
        }
    }

    {   /* breadcrumb log for headless debugging */
        FILE *lg = T8LOG_OPEN("w");
        if (lg) {
            fprintf(lg, "t8gem build 20260831-rel%s\nstart path=%s\n",
                    sp_no_ring ? " (noring)" : "", path);
            fclose(lg);
        }
    }
    {
        struct t8boot_opts o;
        const char *msg;
        o.base = base;
        o.memboot = memboot;
        o.fpga_init = fpga_init;
        prog_open();
        if (!pb_on)
            (void)Cconws("t8gem: booting transputer...\r\n");
        if (t8host_boot(path, &o, prog_step, &msg) != 0) {
            prog_close();
            fail(msg);
            return 1;
        }
    }
    /* payoff frame, then tear the panel down before NetHack draws its title */
    prog_finish();
    prog_close();
    t8call_set_log(log_line);           /* dropped posts: into t8log.txt */
    gemsrv_set_log(log_line);
    rpcgem_register();                  /* the windowport, as t8call handlers */
    /* gemrpc: serve a transputer program's own AES/VDI calls */
    gemsrv_register();
    rc = sp_serve();
    {
        FILE *lg = T8LOG_OPEN("a");
        if (lg) {
            fprintf(lg, "serve rc=%d\n", rc);
            if (rc < 0) {   /* what did the first request look like? */
                extern struct t8rpc_srv sp_srv;
                unsigned char *q = sp_srv.req;
                fprintf(lg, "recv req[0..7]= %02x %02x %02x %02x %02x %02x %02x %02x  (len16=%d)\n",
                        q[0],q[1],q[2],q[3],q[4],q[5],q[6],q[7], q[0] | (q[1]<<8));
                if (tl_driver)
                    fprintf(lg, "tstdma now=%lx  get_calls=%ld get_fail=%ld\n",
                            tbios_tstdma(), tl_get_calls, tl_get_fail);
            }
            fclose(lg);
        }
    }
    if (rc < 0) {
        fail("link error while serving");
        return 1;
    }
    (void)Cconws("t8gem: game exited\r\n-- key --\r\n");
    (void)Crawcin();
    return rc;
}
