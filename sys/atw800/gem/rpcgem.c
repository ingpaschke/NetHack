/* rpcgem.c - GEM windowport adapter for the NetHack windowport RPC.
 *
 * The game core runs on the ATW800/2; this program runs on the ST and
 * renders through the win/gem engine (wingem1.c, e_gem).  It replaces
 * wingem.c: the same mar_* calls, fed from the wire (nhcall.h over
 * t8call) instead of NetHack's globals.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mint/osbind.h>

#include "spserver.h"
#include "host.h"
#include "nhcall.h"            /* the rows both ends share */
#include "t8call_srv.h"

/* loud failure for pre-window paths: TOS text screen + log + key */
static void rpc_fail(const char *msg)
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

static void breadcrumb(const char *msg)
{
    FILE *lg = T8LOG_OPEN("a");
    if (lg) {
        fprintf(lg, "%s\n", msg);
        fclose(lg);
    }
}

/* ---- NetHack-side constants (values fixed by the protocol) ------- */

#define NHW_MESSAGE 1
#define NHW_STATUS  2
#define NHW_MAP     3
#define NHW_MENU    4
#define NHW_TEXT    5

#define NO_COLOR    8
#define RPC_NO_GLYPH 30000      /* sentinel handed to mar_set_no_glyph */

typedef int winid;
typedef short coordxy;

/* menu item structure shared with wingem1.c (win/gem/wingem.h) */
typedef struct Gmi {
    struct Gmi *Gmi_next;
    short Gmi_glyph;
    long Gmi_identifier;
    char Gmi_accelerator, Gmi_groupacc;
    short Gmi_attr;
    short Gmi_color;
    char *Gmi_str;
    long Gmi_count;
    short Gmi_selected;
    unsigned short Gmi_itemflags;
} Gem_menu_item;

/* ---- wingem1.c API ----------------------------------------------- */

extern int mar_gem_init(void);
extern int mar_create_window(short);
extern void mar_destroy_nhwindow(int);
extern void mar_display_nhwindow(winid);
extern int mar_hol_win_type(int);
extern void mar_clear_messagewin(void);
extern void mar_clear_map(void);
extern void mar_curs(short, short);
extern void mar_more(void);
extern void mar_add_message(const char *);
extern char *mar_ask_name(void);
extern void mar_add_status_str(const char *, short);
extern void mar_add_status_line(const char *, const char *, const char *,
                                short);

/* no crash dump on the host; checkpoints are no-ops */
void crash_checkpoint(const char *tag) { (void)tag; }
extern void mar_status_dirty(void);
extern void mar_putstr_text(winid, short, const char *, short);
extern void mar_print_char(winid, coordxy, coordxy, char, short);
extern void mar_add_menu(winid, Gem_menu_item *);
extern void mar_reverse_menu(void);
extern void mar_set_menu_title(const char *);
extern void mar_set_accelerators(void);
extern void mar_set_menu_type(short);
extern short mar_menu_cancelled(void);
extern Gem_menu_item *mar_hol_inv(void);
extern void mar_change_menu_2_text(winid);
extern void mar_update_value(void);
extern short mar_nh_poskey(short *, short *, short *);
extern int Gem_doprev_message(void);
extern void mar_raw_print(const char *);
extern void mar_raw_print_bold(const char *);
extern void mar_exit_nhwindows(void);
extern void mar_set_no_glyph(short);
extern short mar_set_tile_mode(short);
extern void mar_set_tilex(short);
extern void mar_set_tiley(short);
extern void mar_set_msg_align(short);
extern void mar_set_status_align(short);
extern void mar_map_curs_weiter(void);
extern void mar_print_glyph(winid, short, short, short, short);
extern void mar_add_pet_sign(winid, short, short);
extern void mar_cliparound(void);
extern void Gem_start_menu(winid, unsigned long);
extern char Gem_yn_function(const char *, const char *, char);
extern void Gem_getlin(const char *, char *);
extern int  gem_ext_cmd_getlin(char *);   /* native ext-cmd input (TAB) */

/* winid globals wingem1.c uses (normally NetHack's decl.c) */
winid WIN_MESSAGE = -1, WIN_STATUS = -1, WIN_MAP = -1, WIN_INVEN = -1;

/* engine font metrics (wingem1.c globals), logged after init */
typedef struct { short id, size, cw, ch, prop; } NHGEM_FONT;
extern NHGEM_FONT msg_font, map_font, status_font;
extern short msg_width;

extern int tl_replay_mode;      /* replay host: skip modal UI waits */

static int gem_up;              /* mar_gem_init done */
static int map_shown;           /* map window displayed at least once */
static short curr_status_line;

/* messages arriving before the AES is up */
static char preinit[1024];
static int preinit_len;

static void msg_or_buffer(const char *s)
{
    if (gem_up) {
        mar_add_message(s);
        return;
    }
    if (preinit_len + (int)strlen(s) + 2 < (int)sizeof preinit) {
        strcpy(preinit + preinit_len, s);
        preinit_len += strlen(s) + 1;
    }
}

/* host_write from spserver console output lands here, line-buffered */
void rpcgem_console(const char *s, int n)
{
    static char line[256];
    static int len;
    int i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n' || len == (int)sizeof line - 1) {
            line[len] = 0;
            /* drop the transputer's boot diagnostics ("t800 nethack: ...") */
            if (len && !strncmp(line, "t800 nethack:",
                              sizeof("t800 nethack:") - 1))
                len = 0;
            if (len) {
                FILE *lg = T8LOG_OPEN("a");
                if (lg) {
                    fprintf(lg, "con: %s\n", line);
                    fclose(lg);
                }
                msg_or_buffer(line);
            }
            len = 0;
        } else if (c != '\r')
            line[len++] = c;
    }
}

static short numpad_state;          /* iflags.num_pad, see key_common */

/* ---- status line composition (fields -> two lines) --------------- */

#define NFLD 27
static char stf[NFLD][64];
static short stfcolor[NFLD];        /* per-field CLR_ (low byte of wire color) */
static short stfattr[NFLD];         /* per-field HL_ attrs (high byte) */
static unsigned long conds;
static int hpbar_pct = -1;          /* -1 == no bar yet */
static short hpbar_color = 8;       /* 8 == NO_COLOR */

#define RG_INV 0x40                 /* HL_INVERSE bit draw_status renders */
/* conditions NetHack hilites in red+inverse by default; the rest yellow */
#define COND_MAJOR (0x00100000L | 0x00040000L | 0x00200000L \
                    | 0x00000080L | 0x01000000L)

/* append s to line l at *pp, filling parallel color/attr cells */
static void seg(char *l, char *c, char *a, int *pp,
                const char *s, int col, int att)
{
    int i;
    for (i = 0; s[i] && *pp < 252; i++, (*pp)++) {
        l[*pp] = s[i];
        c[*pp] = (char)col;
        a[*pp] = (char)att;
    }
}

static const char *fldname[NFLD] = {
    "", "St:", "Dx:", "Co:", "In:", "Wi:", "Ch:", "", "S:", "", "",
    "Pw:", "/", "Xp:", "AC:", "HD:", "T:", "", "HP:", "/",
    "", "/", "", "Wp:", "Ar:", "Tn:", ""
};

/* BL_CONDITION bit -> displayed name, in NetHack's status order */
static const struct { unsigned long m; const char *nm; } cond_tab[] = {
    { 0x00100000L, "Stone" },  { 0x00040000L, "Slime" },
    { 0x00200000L, "Strngl" }, { 0x00000080L, "FoodPois" },
    { 0x01000000L, "TermIll" },{ 0x00002000L, "InLava" },
    { 0x00000002L, "Blind" },  { 0x00000010L, "Deaf" },
    { 0x00400000L, "Stun" },   { 0x00000008L, "Conf" },
    { 0x00000400L, "Hallu" },  { 0x00020000L, "Sleep" },
    { 0x00008000L, "Parlyz" }, { 0x08000000L, "Uncon" },
    { 0x00000100L, "Glow" },   { 0x00004000L, "Lev" },
    { 0x00000040L, "Fly" },    { 0x00010000L, "Ride" },
    { 0x00000800L, "Held" },   { 0x20000000L, "Holding" },
    { 0x00000200L, "Grab" },   { 0x04000000L, "Trap" },
};

/* both status lines with per-cell colour and attribute arrays for
   draw_status */
static void status_lines(char *l1, char *c1, char *a1,
                         char *l2, char *c2, char *a2, int max)
{
    /* line 2 includes BL_CAP (9, encumbrance e.g. "Stressed") */
    static const int o1[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const int o2[] = { 20, 10, 18, 19, 11, 12, 14, 13, 21, 16, 17, 9 };
    unsigned int i;
    int p, f, k, start, len, bar;

    for (p = 0, i = 0; i < sizeof o1 / sizeof o1[0]; i++) {
        f = o1[i];
        if (!stf[f][0] || p >= max - 80)
            continue;
        if (p && fldname[f][0] != '/')
            seg(l1, c1, a1, &p, " ", 8, 0);
        start = p;
        if (fldname[f][0])
            seg(l1, c1, a1, &p, fldname[f], stfcolor[f], stfattr[f]);
        seg(l1, c1, a1, &p, stf[f], stfcolor[f], stfattr[f]);
        /* hitpointbar: inverse strip over the leading part of the title */
        if (f == 0 && hpbar_pct >= 0) {
            len = p - start;
            bar = (len * hpbar_pct) / 100;
            if (bar < 1 && hpbar_pct > 0)
                bar = 1;
            if (bar >= len && hpbar_pct < 100)
                bar = len - 1;
            for (k = 0; k < bar; k++) {
                c1[start + k] = (char)hpbar_color;
                a1[start + k] |= RG_INV;
            }
        }
    }
    l1[p] = 0;

    for (p = 0, i = 0; i < sizeof o2 / sizeof o2[0]; i++) {
        f = o2[i];
        if (!stf[f][0] || p >= max - 80)
            continue;
        if (p && fldname[f][0] != '/')
            seg(l2, c2, a2, &p, " ", 8, 0);
        if (fldname[f][0])
            seg(l2, c2, a2, &p, fldname[f], stfcolor[f], stfattr[f]);
        seg(l2, c2, a2, &p, stf[f], stfcolor[f], stfattr[f]);
    }
    /* active conditions (Conf, Stun, Blind, ...) with default hilites */
    for (i = 0; i < sizeof cond_tab / sizeof cond_tab[0]; i++)
        if ((conds & cond_tab[i].m) && p < max - 12) {
            int major = (cond_tab[i].m & COND_MAJOR) != 0;
            seg(l2, c2, a2, &p, " ", 8, 0);
            seg(l2, c2, a2, &p, cond_tab[i].nm,
                major ? 1 /*CLR_RED*/ : 11 /*CLR_YELLOW*/,
                major ? RG_INV : 0);
        }
    l2[p] = 0;
}

static int st_dirty;

/* BL_GOLD arrives as \GXXXXXXXX:<amount> (encoded glyph); show '$' */
static void strip_encglyph(char *s)
{
    char *p = s;
    while (*p) {
        if (p[0] == '\\' && p[1] == 'G') {
            int i, hex = 1;
            for (i = 2; i < 10; i++)
                if (!((p[i] >= '0' && p[i] <= '9')
                      || (p[i] >= 'A' && p[i] <= 'F')
                      || (p[i] >= 'a' && p[i] <= 'f'))) {
                    hex = 0;
                    break;
                }
            if (hex) {
                char *q = p + 10;
                *p++ = '$';
                while ((*p++ = *q++) != 0)
                    ;
                p = s;
                continue;
            }
        }
        p++;
    }
    while (p > s && p[-1] == ' ')
        *--p = 0;
}


static void push_status(void)
{
    char l1[256], c1[256], a1[256], l2[256], c2[256], a2[256];
    st_dirty = 0;
    if (WIN_STATUS < 0)
        return;
    status_lines(l1, c1, a1, l2, c2, a2, 250);
    mar_status_dirty();
    mar_add_status_line(l1, c1, a1, 0);
    mar_add_status_line(l2, c2, a2, 1);
    mar_display_nhwindow(WIN_STATUS);
}

/* ---- extended-command autocomplete (table shipped via nh_extlist) - */

#define EXT_MAX 256
static struct { int idx; int ac; char name[36]; } ext_tab[EXT_MAX];
static int ext_n;

static int ext_eq(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] == b[i]; i++)
        if (!a[i]) return 1;
    return 0;
}
static int ext_pre(const char *pfx, const char *s)
{
    int i;
    for (i = 0; pfx[i]; i++)
        if (pfx[i] != s[i]) return 0;
    return 1;
}
/* one map cell of nh_glyphrow; out-of-range cells are rejected and
   logged.  valid map: x 1..79, y 0..20 */
static void glyph_cell(long w, long x, long y, long ch, long color,
                       long flags, long tile, long bktile)
{
    if (x < 1 || x > 79 || y < 0 || y > 20) {
        FILE *lg = T8LOG_OPEN("a");
        if (lg) {
            fprintf(lg, "GLYPH REJECT x=%ld y=%ld ch=%ld tile=%ld\n",
                    x, y, ch, tile);
            fclose(lg);
        }
        return;
    }
    mar_curs((short)(x - 1), (short)y);
    if (tile >= 0 && mar_set_tile_mode(-1)) {
        mar_print_glyph((winid)w, (short)(x - 1), (short)y,
                        (short)tile, (short)(bktile >= 0 ? bktile : 0));
        if (flags & 0x10)           /* MG_PET */
            mar_add_pet_sign((winid)w, (short)(x - 1), (short)y);
    } else {
        color &= 0xff;
        if (color >= 16)
            color = NO_COLOR;
        mar_print_char((winid)w, (coordxy)(x - 1), (coordxy)y,
                       (char)ch, (short)color);
    }
}

/* typed line -> extcmdlist index: exact name, else unique autocomplete
   prefix, else -1 */
static int ext_resolve(const char *line)
{
    int i, hit = -1, cnt = 0;
    if (!line[0]) return -1;
    for (i = 0; i < ext_n; i++)
        if (ext_eq(ext_tab[i].name, line)) return ext_tab[i].idx;
    for (i = 0; i < ext_n; i++)
        if (ext_tab[i].ac && ext_pre(line, ext_tab[i].name)) {
            hit = ext_tab[i].idx;
            cnt++;
        }
    return cnt == 1 ? hit : -1;
}

/* '?' at the ext-command prompt: PICK_ONE menu of the shipped ext_tab,
   returns the extcmdlist index or -1 */
static int ext_menu_pick(void)
{
    int w, i, chosen = -1;
    Gem_menu_item *g;

    w = mar_create_window(NHW_MENU);
    if (w < 0 || w >= 20)
        return -1;
    Gem_start_menu((winid)w, 0UL);
    for (i = 0; i < ext_n; i++) {
        Gem_menu_item *it = (Gem_menu_item *)malloc(sizeof *it);
        char line[64];
        if (!it)
            break;
        sprintf(line, "? - %s", ext_tab[i].name); /* '?' -> accel key */
        it->Gmi_next = 0;
        it->Gmi_glyph = RPC_NO_GLYPH;
        it->Gmi_identifier = ext_tab[i].idx + 1;   /* +1: non-zero = selectable */
        it->Gmi_accelerator = 0;                   /* auto-assigned */
        it->Gmi_groupacc = 0;
        it->Gmi_attr = 0;
        it->Gmi_color = NO_COLOR;
        it->Gmi_count = -1L;
        it->Gmi_selected = 0;
        it->Gmi_itemflags = 0;
        it->Gmi_str = strdup(line);
        if (!it->Gmi_str) {
            free(it);
            break;
        }
        mar_add_menu((winid)w, it);
    }
    mar_reverse_menu();
    mar_set_menu_title("Extended commands");
    mar_set_accelerators();
    mar_set_menu_type(1);                          /* PICK_ONE */
    mar_display_nhwindow((winid)w);                /* modal */
    for (g = mar_hol_inv(); g; g = g->Gmi_next)
        if (g->Gmi_selected) {
            chosen = (int)(g->Gmi_identifier - 1);
            break;
        }
    mar_destroy_nhwindow((int)w);
    return chosen;
}

/* ---- the windowport, as registered t8call handlers ------------------
   one function per row of nhcall.h, over the GEM engine's mar_* calls;
   arguments arrive unpacked by position */
#define A(k) t8call_i(c, k)
#define S(k) t8call_s(c, k)

#ifdef RPC_TRACE
static void trace(const char *what)
{
    static FILE *tf;
    if (!tf) tf = fopen("t8trace.txt", "w");
    if (tf) { fprintf(tf, "%s\n", what); fflush(tf); }
}
#else
#define trace(what) ((void) 0)
#endif

static void h_init(struct t8call_ctx *c)
{
    trace("init");
    (void) A(0);
    mar_set_tile_mode(1);           /* tiles (NH16.IMG etc.) */
    mar_set_tilex(16);
    mar_set_tiley(16);
    mar_set_msg_align(1);           /* top */
    mar_set_status_align(0);        /* bottom */
    if (!gem_up) {
        breadcrumb("nh_init: calling mar_gem_init");
        if (mar_gem_init() == 0) {
            rpc_fail("GEM init failed (RSC/IMG missing? screen?)");
            exit(1);
        }
        breadcrumb("nh_init: GEM up");
        gem_up = 1;
        {
            /* enable the RPC-only wingem1.c behaviour */
            extern int wingem_rpc;
            wingem_rpc = 1;
        }
        mar_set_no_glyph(RPC_NO_GLYPH);
        {
            FILE *lg = T8LOG_OPEN("a");
            if (lg) {
                fprintf(lg, "font msg cw=%d ch=%d width=%d | "
                        "map cw=%d ch=%d | status cw=%d ch=%d\n",
                        msg_font.cw, msg_font.ch, msg_width,
                        map_font.cw, map_font.ch,
                        status_font.cw, status_font.ch);
                fclose(lg);
            }
        }
        /* never let a bad font metric collapse the message wrap */
        if (msg_width < 20)
            msg_width = 60;
        /* the game never creates a status window; the GEM layout wants one */
        WIN_STATUS = mar_create_window(NHW_STATUS);
    }
    /* pre-init console lines are flushed in h_create once WIN_MESSAGE exists */
    t8call_ret_i(c, NHC_VERSION);
}

static void h_create(struct t8call_ctx *c)
{
    long type = A(0);
    int id = mar_create_window((short)type);
    if (id < 0 || id >= 20)         /* MAXWIN sentinel = table full */
        id = -1;                    /* let the game handle WIN_ERR */
    switch (type) {
    case NHW_MESSAGE:
        if (WIN_MESSAGE < 0) {
            WIN_MESSAGE = id;
            /* flush console lines that arrived before the message window */
            if (preinit_len) {
                int off = 0;
                while (off < preinit_len) {
                    mar_add_message(preinit + off);
                    off += strlen(preinit + off) + 1;
                }
                preinit_len = 0;
            }
        }
        break;
    case NHW_STATUS:  if (WIN_STATUS < 0) WIN_STATUS = id; break;
    case NHW_MAP:     if (WIN_MAP < 0) WIN_MAP = id; break;
    }
    t8call_ret_i(c, id);
}

static void h_clear(struct t8call_ctx *c)
{
    long w = A(0);
    switch (mar_hol_win_type((int)w)) {
    case NHW_MESSAGE: mar_clear_messagewin(); break;
    case NHW_MAP:     mar_clear_map(); break;
    }
}

static void h_display(struct t8call_ctx *c)
{
    long w = A(0), blocking = A(1);
    if (tl_replay_mode) {
        int t = mar_hol_win_type((int)w);
        if (t != NHW_MENU && t != NHW_TEXT)
            mar_display_nhwindow((winid)w);
        return;
    }
    mar_display_nhwindow((winid)w);
    if (w == WIN_MAP)
        map_shown = 1;
    if (blocking) {
        switch (mar_hol_win_type((int)w)) {
        case NHW_MESSAGE:
            mar_more();
            break;
        case NHW_MAP:
            mar_display_nhwindow(WIN_MESSAGE);
            mar_more();
            break;
        }
    }
}

static void h_destroy(struct t8call_ctx *c)
{
    mar_destroy_nhwindow((int)A(0));
}

static void h_curs(struct t8call_ctx *c)
{
    long w = A(0), x = A(1), y = A(2);
    if (w == WIN_MAP)
        mar_curs((short)(x - 1), (short)y);
    else if (w == WIN_STATUS)
        curr_status_line = (short)y;
}

static void h_putstr(struct t8call_ctx *c)
{
    long w = A(0), attr = A(1);
    static char buf[SP_BUFMAX];
    strncpy(buf, S(2), sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    switch (mar_hol_win_type((int)w)) {
    case NHW_MESSAGE:
        mar_add_message(buf);
        break;
    case NHW_STATUS:
        mar_status_dirty();
        mar_add_status_str(buf, curr_status_line);
        if (curr_status_line)
            mar_display_nhwindow((winid)w);
        break;
    case NHW_MENU:
        mar_change_menu_2_text((winid)w);
        /* fallthrough */
    case NHW_TEXT:
        mar_putstr_text((winid)w, (short)attr, buf, (short)RPC_NO_GLYPH);
        break;
    default:
        msg_or_buffer(buf);
        break;
    }
}

static void h_glyphrow(struct t8call_ctx *c)
{
    long w = A(0), y = A(1), x0 = A(2), i;
    int n;
    const unsigned char *cells = t8call_blob(c, 3, &n);
    for (i = 0; i < n / NHC_CELL; i++) {
        const unsigned char *cell = cells + i * NHC_CELL;
        long tile = t8_ld16(cell + 5), bktile = t8_ld16(cell + 7);
        glyph_cell(w, x0 + i, y, cell[0], cell[1], (long)t8_ld16(cell + 2),
                   tile == 0xffff ? -1 : tile,
                   bktile == 0xffff ? -1 : bktile);
    }
}

/* getch and poskey share everything but the three out words */
static short key_common(struct t8call_ctx *c, short *x, short *y, short *mod)
{
    short k;
    /* iflags.num_pad comes with every key request */
    numpad_state = (short) A(0);
    if (st_dirty)
        push_status();
    if (WIN_MESSAGE >= 0)           /* flush pending messages */
        mar_display_nhwindow(WIN_MESSAGE);
    mar_update_value();
    *x = *y = *mod = 0;
    if (tl_replay_mode)
        return 27;
    k = mar_nh_poskey(x, y, mod);
    /* mar_nh_poskey may hand back x==80 / y==21, one past the map:
       drop out-of-range clicks, and never return a zero key without
       a modifier (the game reads that as a click) */
    if (*x < 1 || *x >= 80 || *y < 0 || *y >= 21 || (k == 0 && *mod == 0)) {
        *x = 0; *y = 0; *mod = 0;
        if (k == 0)
            k = 27;                 /* ESC, not a click */
    }
    /* CR->LF, and mask to a byte so meta keys arrive as 128..255 */
    k &= 0xff;
    if (k == '\r')
        k = '\n';
    return k;
}

static void h_getch(struct t8call_ctx *c)
{
    short x, y, mod;
    short k = key_common(c, &x, &y, &mod);
    if (!k)
        k = 27;
    t8call_ret_i(c, k);
}

static void h_poskey(struct t8call_ctx *c)
{
    short x, y, mod;
    short k = key_common(c, &x, &y, &mod);
    t8call_ret_i(c, k);
    t8call_out_i(c, 1, x);
    t8call_out_i(c, 2, y);
    t8call_out_i(c, 3, mod);
}

static void h_ynfn(struct t8call_ctx *c)
{
    long def = A(0);
    char q[400], choices[64];
    char r;
    strncpy(q, S(1), sizeof q - 1); q[sizeof q - 1] = 0;
    strncpy(choices, S(2), sizeof choices - 1); choices[sizeof choices - 1] = 0;
    r = Gem_yn_function(q, choices[0] ? choices : (char *)0, (char)def);
    t8call_ret_i(c, r);
}

static void h_getlin(struct t8call_ctx *c)
{
    char q[400];
    static char buf[SP_BUFMAX];
    strncpy(q, S(0), sizeof q - 1); q[sizeof q - 1] = 0;
    buf[0] = 0;
    Gem_getlin(q, buf);
    t8call_out_s(c, 1, buf);
}

static void h_askname(struct t8call_ctx *c)     /* native title + name */
{
    const char *nm = mar_ask_name();   /* draws TITLE.IMG + prompt */
    t8call_out_s(c, 0, nm ? nm : "");
}

static void h_doprev(struct t8call_ctx *c)      /* Ctrl-P prev message */
{
    (void) c;
    Gem_doprev_message();
}

static void h_menu_start(struct t8call_ctx *c)
{
    Gem_start_menu((winid)A(0), (unsigned long)A(1));
}

static void h_menu_add(struct t8call_ctx *c)
{
    Gem_menu_item *it;
    long w = A(0), idx = A(1), accel = A(2), gaccel = A(3), attr = A(4);
    long color = A(5), flags = A(7), tile = A(8);
    char line[200];
    if (flags & 2)                  /* selectable: "c - text" */
        sprintf(line, "%c - %.190s", accel ? (char)accel : '?', S(9));
    else
        sprintf(line, "%.198s", S(9));
    it = (Gem_menu_item *)malloc(sizeof *it);
    if (!it)
        return;
    it->Gmi_identifier = (flags & 2) ? idx + 1 : 0;
    it->Gmi_glyph = (tile >= 0) ? (short)tile : (short)RPC_NO_GLYPH;
    it->Gmi_count = -1L;
    it->Gmi_selected = (flags & 1) ? 1 : 0;
    it->Gmi_accelerator = (char)accel;
    it->Gmi_groupacc = (char)gaccel;
    it->Gmi_attr = (short)attr;
    it->Gmi_color = (short)(color & 0xff) < 16 ? (short)(color & 0xff) : NO_COLOR;
    it->Gmi_itemflags = 0;
    it->Gmi_str = strdup(line);
    if (!it->Gmi_str) {             /* renderer can't take a NULL str */
        free(it);
        return;
    }
    mar_add_menu((winid)w, it);
}

static void h_menu_end(struct t8call_ctx *c)
{
    static char buf[SP_BUFMAX];
    strncpy(buf, S(1), sizeof buf - 1); buf[sizeof buf - 1] = 0;
    mar_reverse_menu();
    mar_set_menu_title(buf[0] ? buf : (char *)0);
    mar_set_accelerators();
}

static void h_menu_sel(struct t8call_ctx *c)
{
    long w = A(0), how = A(1);
    static long sel[2 * 512];
    Gem_menu_item *g;
    int n = 0, cap = t8call_listcap(c, 2) / 2, sent = 0;
    mar_set_menu_type((short)how);
    mar_display_nhwindow((winid)w); /* modal: runs its own loop */
    for (g = mar_hol_inv(); g; g = g->Gmi_next)
        if (g->Gmi_selected)
            n++;
    if (n == 0 && mar_menu_cancelled()) {
        t8call_ret_i(c, -1);
        return;
    }
    if (n > cap) n = cap;
    if (n > 512) n = 512;
    for (g = mar_hol_inv(); g && sent < n; g = g->Gmi_next)
        if (g->Gmi_selected) {
            sel[2 * sent] = g->Gmi_identifier - 1;
            sel[2 * sent + 1] = g->Gmi_count;
            sent++;
        }
    t8call_out_list(c, 2, sel, 2 * sent);
    t8call_ret_i(c, sent);
}

static void h_exit(struct t8call_ctx *c)
{
    const char *s = S(0);
    if (s[0])
        mar_raw_print((char *)s);
    if (gem_up) {
        mar_exit_nhwindows();
        gem_up = 0;
    }
}

static void h_rawprint(struct t8call_ctx *c)
{
    long bold = A(0);
    static char buf[SP_BUFMAX];
    strncpy(buf, S(1), sizeof buf - 1); buf[sizeof buf - 1] = 0;
    if (!gem_up)
        msg_or_buffer(buf);
    else if (bold)
        mar_raw_print_bold(buf);
    else
        mar_raw_print(buf);
}

static void h_status(struct t8call_ctx *c)
{
    long fld = A(0), pct = A(2), color = A(3);
    int n;
    const unsigned char *p = t8call_blob(c, 4, &n);
    if (fld == 22) {                /* BL_CONDITION bits */
        conds = 0;
        if (p && n >= 4)
            conds = t8_ld32(p);
    } else if (fld >= 0 && fld < NFLD) {
        int k = n < 63 ? n : 63;
        memcpy(stf[fld], p, (size_t)k);
        stf[fld][k] = 0;
        strip_encglyph(stf[fld]);
        stfcolor[fld] = (short)(color & 0xff);
        stfattr[fld] = (short)((color >> 8) & 0xff);
        if (fld == 18) {            /* BL_HP: drives the hitpointbar */
            hpbar_pct = (int)pct;
            hpbar_color = (short)(color & 0xff);
        }
    }
    st_dirty = 1;
    if ((fld == -1 || fld == -2) && gem_up)  /* FLUSH/RESET */
        push_status();
}

static void h_cliparound(struct t8call_ctx *c)
{
    long x = A(0), y = A(1);
    if (gem_up) {
        mar_curs((short)(x - 1), (short)y);
        if (map_shown)
            mar_cliparound();
    }
}

static void h_extlist(struct t8call_ctx *c)      /* cache the table */
{
    long first = A(0), m = A(1), i;
    int n;
    const unsigned char *p = t8call_blob(c, 2, &n);
    const unsigned char *end = p + n;
    if (first) ext_n = 0;
    for (i = 0; i < m && p + 10 <= end; i++) {
        long idx = (long)(t8_i32)t8_ld32(p);
        long ac = (long)(t8_i32)t8_ld32(p + 4);
        int len = (int)t8_ld16(p + 8), j;
        p += 10;
        if (p + len > end)
            break;
        if (ext_n < EXT_MAX) {
            ext_tab[ext_n].idx = (int)idx;
            ext_tab[ext_n].ac = (int)ac;
            for (j = 0; j < (int)sizeof ext_tab[0].name - 1 && j < len; j++)
                ext_tab[ext_n].name[j] = (char)p[j];
            ext_tab[ext_n].name[j] = 0;
            ext_n++;
        }
        p += len;
    }
    {
        FILE *lg = T8LOG_OPEN("a");
        if (lg) { fprintf(lg, "EXTLIST first=%ld n=%ld total=%d\n",
                          first, m, ext_n); fclose(lg); }
    }
}

static void h_extcmd(struct t8call_ctx *c)
{
    char q[256];
    int r, idx;
    q[0] = 0;
    r = gem_ext_cmd_getlin(q);      /* native input with TAB complete */
    if (r == 1)                     /* '?' typed: full command menu */
        idx = ext_menu_pick();
    else
        idx = (r == 0) ? ext_resolve(q) : -1;
    {
        FILE *lg = T8LOG_OPEN("a");
        if (lg) { fprintf(lg, "EXTCMD cached=%d r=%d q='%s' idx=%d\n",
                          ext_n, r, q, idx); fclose(lg); }
    }
    t8call_ret_i(c, idx);
}

static void h_bell(struct t8call_ctx *c)
{
    (void) c;
    (void)Cconws("\007");
}

static void h_delay(struct t8call_ctx *c)      /* ~50ms: three vblanks */
{
    (void) c;
    Vsync();
    Vsync();
    Vsync();
}

static void h_sync(struct t8call_ctx *c)
{
    (void) c;
    if (gem_up) {
        if (st_dirty)
            push_status();
        if (WIN_MESSAGE >= 0)
            mar_display_nhwindow(WIN_MESSAGE);
        if (WIN_MAP >= 0)
            mar_display_nhwindow(WIN_MAP);
    }
}

#undef A
#undef S

#define H(name, sig) { "nh_" #name, sig, h_##name },
static const struct t8call_fn rpcgem_table[] = {
    NHCALL_TABLE(H)
};
#undef H

void rpcgem_register(void)
{
    host_set_console(rpcgem_console);   /* console text into the message window */
    t8call_attach();
    t8call_register(rpcgem_table, (int)(sizeof rpcgem_table / sizeof rpcgem_table[0]));
}

/* ---- stubs wingem1.c links against ------------------------------- */

long yn_number = 0;
char **rip_line = 0;
int total_tiles_used = 2303;    /* matches the tile sheets */

void panic(const char *fmt, ...)
{
    if (gem_up)
        mar_exit_nhwindows();
    fprintf(stderr, "t8gem panic: %s\n", fmt ? fmt : "");
    exit(2);
}

void done2(void)
{
    if (gem_up)
        mar_exit_nhwindows();
    exit(0);
}

void Gem_nhbell(void)
{
    putchar('\007');
    fflush(stdout);
}

short mar_iflags_numpad(void)
{
    return numpad_state;
}

short mar_hp_query(void)
{
    return -1;                          /* no hitpoint bar data */
}

short mar_get_msg_history(void)
{
    return 20;
}

short mar_get_msg_visible(void)
{
    return 3;
}

void mar_get_font(short type, char **name, short *size)
{
    (void)type;
    *name = 0;                          /* system font */
    *size = 0;
}

/* TAB completion for gem_ext_cmd_getlin: cycle through the
   autocompleting commands starting with prefix */
int gem_ext_complete_next(const char *prefix, int which, char *out,
                          int outsz)
{
    int i, ml[EXT_MAX], mc = 0, k, j;
    const char *s;

    for (i = 0; i < ext_n; i++)
        if (ext_tab[i].ac && ext_pre(prefix, ext_tab[i].name))
            ml[mc++] = i;
    if (mc == 0)
        return -1;
    k = which % mc;
    if (k < 0)
        k += mc;
    s = ext_tab[ml[k]].name;
    for (j = 0; j < outsz - 1 && s[j]; j++)
        out[j] = s[j];
    out[j] = 0;
    return 0;
}

/* core windows.c helper: without menuinvertmode config, invert all */
int menuitem_invert_test(int mode, unsigned itemflags, int is_selected)
{
    (void)mode; (void)itemflags; (void)is_selected;
    return 1;
}
