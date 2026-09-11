/* t4win.c - the windowport over t8call: the shim callback
 * cb(name, ret_ptr, fmt, ...) becomes calls on the host by name.
 *
 * The call table is nhcall.h; each row is looked up once
 * and its signature checked against the host's.  Local here: the
 * glyph-row batcher, the menu identifier table, the extended-command
 * table shipment and the player selection menus.
 */
#include "hack.h"
#include "func_tab.h"
#include "nhcall.h"
#include "t8call.h"
#include "t800host.h"

/* ---- the rows, looked up on first use ------------------------------ */

static const char *nhc_name[NHC_N] = {
#define NHC_NAME(name, sig) "nh_" #name,
    NHCALL_TABLE(NHC_NAME)
#undef NHC_NAME
};
static const char *nhc_sig[NHC_N] = {
#define NHC_SIG(name, sig) sig,
    NHCALL_TABLE(NHC_SIG)
#undef NHC_SIG
};
static int nhc_id[NHC_N];
static int host_ver = 1;

/* row id, looked up on first use; a missing or mismatched row is fatal */
static int nh(enum nhc_id which)
{
    if (nhc_id[which] <= 0) {
        int id = t8call_lookup(nhc_name[which], nhc_sig[which]);
        if (id < 0) {
            printf("t800 nethack: host lacks %s %s (errno %d, host says '%s')\n",
                   nhc_name[which], nhc_sig[which], t8call_errno, t8call_hostsig);
            exit(3);
        }
        nhc_id[which] = id;
    }
    return nhc_id[which];
}

/* ---- row-batched glyphs (nh_glyphrow) ----------------------------
   consecutive print_glyph cells of one row go out as one nh_glyphrow
   post of NHC_CELL bytes per cell; any other RPC flushes the pending
   row first, so ordering is preserved */
static unsigned char grow[COLNO * NHC_CELL];
static long grow_win = -1, grow_y, grow_x0;
static int grow_n;

static void grow_flush(void)
{
    if (grow_n <= 0)
        return;
    t8call_post(nh(NHC_glyphrow), (int) grow_win, (int) grow_y, (int) grow_x0,
                grow, grow_n * NHC_CELL);
    grow_n = 0;
}

static void grow_add(long win, long x, long y, const glyph_info *gi,
                     const glyph_info *bk)
{
    int maxn = (__t800_host_chunk() - 40) / NHC_CELL;
    long tile = (gi && gi->glyph != NO_GLYPH) ? gi->gm.tileidx : -1;
    long bktile = (bk && bk->glyph != NO_GLYPH) ? bk->gm.tileidx : -1;
    unsigned char *c;

    if (maxn > COLNO)
        maxn = COLNO;
    if (grow_n > 0
        && (win != grow_win || y != grow_y || x != grow_x0 + grow_n
            || grow_n >= maxn))
        grow_flush();
    if (grow_n == 0) {
        grow_win = win;
        grow_y = y;
        grow_x0 = x;
    }
    c = grow + grow_n * NHC_CELL;
    c[0] = (unsigned char) (gi ? gi->ttychar : ' ');
    c[1] = (unsigned char) (gi ? gi->gm.sym.color : 0);
    t8_st16(c + 2, (unsigned) (gi ? gi->gm.glyphflags : 0));
    c[4] = (unsigned char) (bk ? bk->ttychar : 0);
    t8_st16(c + 5, (unsigned) (tile & 0xffffL));
    t8_st16(c + 7, (unsigned) (bktile & 0xffffL));
    grow_n++;
}

/* ---- menu identifier table -------------------------------------- */

#define NHC_MAXMENU 512

static anything mi_id[NHC_MAXMENU];
static unsigned mi_flags[NHC_MAXMENU];
static int mi_n;
static winid mi_win = WIN_ERR;

/* ---- the callback ----------------------------------------------- */

union nwarg { long i; void *p; };

/* Player selection after the GEM windowport's Gem_player_selection:
   role/race/gender/alignment menus with the monster glyph per choice,
   driven through the standard windowport calls. */
static void
t800_player_selection(void)
{
    short i, k, n;
    char pick4u = 'n', pbuf[QBUFSZ], lastch = 0, currch;
    winid win;
    anything any;
    menu_item *selected = NULL;

    rigid_role_checks();

    if (!flags.randomall
        && (flags.initrole == ROLE_NONE || flags.initrace == ROLE_NONE
            || flags.initgend == ROLE_NONE || flags.initalign == ROLE_NONE)) {
        pick4u = yn_function(build_plselection_prompt(
                                 pbuf, QBUFSZ, flags.initrole, flags.initrace,
                                 flags.initgend, flags.initalign),
                             ynqchars, 'n', TRUE);
        if (pick4u == 'q') {
        give_up:
            if (selected)
                free((genericptr_t) selected);
            nh_terminate(EXIT_SUCCESS);
            /*NOTREACHED*/
            return;
        }
    }

    /* role */
    if (flags.initrole < 0) {
        if (pick4u == 'y' || flags.initrole == ROLE_RANDOM
            || flags.randomall) {
            flags.initrole = pick_role(flags.initrace, flags.initgend,
                                       flags.initalign, PICK_RANDOM);
            if (flags.initrole < 0) {
                pline("Incompatible role!");
                flags.initrole = randrole(FALSE);
            }
        } else {
            win = create_nhwindow(NHW_MENU);
            start_menu(win, MENU_BEHAVE_STANDARD);
            any.a_void = 0;
            for (i = 0; roles[i].name.m; i++) {
                if (ok_role(i, flags.initrace, flags.initgend,
                            flags.initalign)) {
                    glyph_info gi;
                    int glyph = monnum_to_glyph(roles[i].mnum, MALE);
                    map_glyphinfo(0, 0, glyph, 0, &gi);
                    any.a_int = i + 1;
                    currch = lowc(roles[i].name.m[0]);
                    if (currch == lastch)
                        currch = highc(currch);
                    add_menu(win, &gi, &any, currch, 0, ATR_NONE,
                             NO_COLOR, an(roles[i].name.m), MENU_ITEMFLAGS_NONE);
                    lastch = currch;
                }
            }
            any.a_int = pick_role(flags.initrace, flags.initgend,
                                  flags.initalign, PICK_RANDOM) + 1;
            if (any.a_int == 0)
                any.a_int = randrole(FALSE) + 1;
            add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR,
                     "Random", MENU_ITEMFLAGS_NONE);
            any.a_int = i + 1;
            add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR,
                     "Quit", MENU_ITEMFLAGS_NONE);
            end_menu(win, "Pick a role");
            n = select_menu(win, PICK_ONE, &selected);
            destroy_nhwindow(win);
            if (n != 1 || selected[0].item.a_int == any.a_int)
                goto give_up;
            flags.initrole = selected[0].item.a_int - 1;
            free((genericptr_t) selected), selected = 0;
        }
    }

    /* race */
    if (flags.initrace < 0 || !validrace(flags.initrole, flags.initrace)) {
        if (pick4u == 'y' || flags.initrace == ROLE_RANDOM
            || flags.randomall) {
            flags.initrace = pick_race(flags.initrole, flags.initgend,
                                       flags.initalign, PICK_RANDOM);
            if (flags.initrace < 0) {
                pline("Incompatible race!");
                flags.initrace = randrace(flags.initrole);
            }
        } else {
            n = 0; k = 0;
            for (i = 0; races[i].noun; i++)
                if (ok_race(flags.initrole, i, flags.initgend,
                            flags.initalign)) { n++; k = i; }
            if (n == 0)
                for (i = 0; races[i].noun; i++)
                    if (validrace(flags.initrole, i)) { n++; k = i; }
            if (n > 1) {
                win = create_nhwindow(NHW_MENU);
                start_menu(win, MENU_BEHAVE_STANDARD);
                any.a_void = 0;
                for (i = 0; races[i].noun; i++)
                    if (ok_race(flags.initrole, i, flags.initgend,
                                flags.initalign)) {
                        glyph_info gi;
                        int glyph = monnum_to_glyph(races[i].mnum, MALE);
                        map_glyphinfo(0, 0, glyph, 0, &gi);
                        any.a_int = i + 1;
                        add_menu(win, &gi, &any, races[i].noun[0], 0,
                                 ATR_NONE, NO_COLOR, races[i].noun, MENU_ITEMFLAGS_NONE);
                    }
                any.a_int = pick_race(flags.initrole, flags.initgend,
                                      flags.initalign, PICK_RANDOM) + 1;
                if (any.a_int == 0)
                    any.a_int = randrace(flags.initrole) + 1;
                add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR,
                         "Random", MENU_ITEMFLAGS_NONE);
                any.a_int = i + 1;
                add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR,
                         "Quit", MENU_ITEMFLAGS_NONE);
                Sprintf(pbuf, "Pick the race of your %s",
                        roles[flags.initrole].name.m);
                end_menu(win, pbuf);
                n = select_menu(win, PICK_ONE, &selected);
                destroy_nhwindow(win);
                if (n != 1 || selected[0].item.a_int == any.a_int)
                    goto give_up;
                k = selected[0].item.a_int - 1;
                free((genericptr_t) selected), selected = 0;
            }
            flags.initrace = k;
        }
    }

    /* gender */
    if (flags.initgend < 0
        || !validgend(flags.initrole, flags.initrace, flags.initgend)) {
        if (pick4u == 'y' || flags.initgend == ROLE_RANDOM
            || flags.randomall) {
            flags.initgend = pick_gend(flags.initrole, flags.initrace,
                                       flags.initalign, PICK_RANDOM);
            if (flags.initgend < 0) {
                pline("Incompatible gender!");
                flags.initgend = randgend(flags.initrole, flags.initrace);
            }
        } else {
            n = 0; k = 0;
            for (i = 0; i < ROLE_GENDERS; i++)
                if (ok_gend(flags.initrole, flags.initrace, i,
                            flags.initalign)) { n++; k = i; }
            if (n == 0)
                for (i = 0; i < ROLE_GENDERS; i++)
                    if (validgend(flags.initrole, flags.initrace, i)) { n++; k = i; }
            if (n > 1) {
                win = create_nhwindow(NHW_MENU);
                start_menu(win, MENU_BEHAVE_STANDARD);
                any.a_void = 0;
                for (i = 0; i < ROLE_GENDERS; i++)
                    if (ok_gend(flags.initrole, flags.initrace, i,
                                flags.initalign)) {
                        glyph_info gi;
                        int glyph = monnum_to_glyph(roles[flags.initrole].mnum,
                                                    i == 0 ? MALE : FEMALE);
                        map_glyphinfo(0, 0, glyph, 0, &gi);
                        any.a_int = i + 1;
                        add_menu(win, &gi, &any, genders[i].adj[0], 0,
                                 ATR_NONE, NO_COLOR, genders[i].adj, MENU_ITEMFLAGS_NONE);
                    }
                any.a_int = pick_gend(flags.initrole, flags.initrace,
                                      flags.initalign, PICK_RANDOM) + 1;
                if (any.a_int == 0)
                    any.a_int = randgend(flags.initrole, flags.initrace) + 1;
                add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR,
                         "Random", MENU_ITEMFLAGS_NONE);
                any.a_int = i + 1;
                add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR,
                         "Quit", MENU_ITEMFLAGS_NONE);
                Sprintf(pbuf, "Pick the gender of your %s %s",
                        races[flags.initrace].adj,
                        roles[flags.initrole].name.m);
                end_menu(win, pbuf);
                n = select_menu(win, PICK_ONE, &selected);
                destroy_nhwindow(win);
                if (n != 1 || selected[0].item.a_int == any.a_int)
                    goto give_up;
                k = selected[0].item.a_int - 1;
                free((genericptr_t) selected), selected = 0;
            }
            flags.initgend = k;
        }
    }

    /* alignment */
    if (flags.initalign < 0
        || !validalign(flags.initrole, flags.initrace, flags.initalign)) {
        if (pick4u == 'y' || flags.initalign == ROLE_RANDOM
            || flags.randomall) {
            flags.initalign = pick_align(flags.initrole, flags.initrace,
                                         flags.initgend, PICK_RANDOM);
            if (flags.initalign < 0) {
                pline("Incompatible alignment!");
                flags.initalign = randalign(flags.initrole, flags.initrace);
            }
        } else {
            n = 0; k = 0;
            for (i = 0; i < ROLE_ALIGNS; i++)
                if (ok_align(flags.initrole, flags.initrace, flags.initgend,
                             i)) { n++; k = i; }
            if (n == 0)
                for (i = 0; i < ROLE_ALIGNS; i++)
                    if (validalign(flags.initrole, flags.initrace, i)) { n++; k = i; }
            if (n > 1) {
                win = create_nhwindow(NHW_MENU);
                start_menu(win, MENU_BEHAVE_STANDARD);
                any.a_void = 0;
                for (i = 0; i < ROLE_ALIGNS; i++)
                    if (ok_align(flags.initrole, flags.initrace,
                                 flags.initgend, i)) {
                        glyph_info gi;
                        int glyph = monnum_to_glyph(roles[flags.initrole].mnum,
                                                    flags.initgend > 0 ? FEMALE : MALE);
                        map_glyphinfo(0, 0, glyph, 0, &gi);
                        any.a_int = i + 1;
                        add_menu(win, &gi, &any, aligns[i].adj[0], 0,
                                 ATR_NONE, NO_COLOR, aligns[i].adj, MENU_ITEMFLAGS_NONE);
                    }
                any.a_int = pick_align(flags.initrole, flags.initrace,
                                       flags.initgend, PICK_RANDOM) + 1;
                if (any.a_int == 0)
                    any.a_int = randalign(flags.initrole, flags.initrace) + 1;
                add_menu(win, &nul_glyphinfo, &any, '*', 0, ATR_NONE, NO_COLOR,
                         "Random", MENU_ITEMFLAGS_NONE);
                any.a_int = i + 1;
                add_menu(win, &nul_glyphinfo, &any, 'q', 0, ATR_NONE, NO_COLOR,
                         "Quit", MENU_ITEMFLAGS_NONE);
                Sprintf(pbuf, "Pick the alignment of your %s %s %s",
                        genders[flags.initgend].adj,
                        races[flags.initrace].adj,
                        (flags.initgend && roles[flags.initrole].name.f)
                            ? roles[flags.initrole].name.f
                            : roles[flags.initrole].name.m);
                end_menu(win, pbuf);
                n = select_menu(win, PICK_ONE, &selected);
                destroy_nhwindow(win);
                if (n != 1 || selected[0].item.a_int == any.a_int)
                    goto give_up;
                k = selected[0].item.a_int - 1;
                free((genericptr_t) selected), selected = 0;
            }
            flags.initalign = k;
        }
    }
}

void t800_rpc_cb(const char *name, void *ret_ptr, const char *fmt, ...)
{
    va_list ap;
    union nwarg a[10];
    const char *f = fmt + 1;
    int n = 0;

    va_start(ap, fmt);
    for (; *f && n < 10; f++, n++) {
        if (*f == 's' || *f == 'p')
            a[n].p = va_arg(ap, void *);
        else
            a[n].i = va_arg(ap, int);   /* i b c 0 1 2: promoted */
    }
    va_end(ap);

    /* default returns */
    if (ret_ptr) {
        switch (fmt[0]) {
        case 'i': *(int *)ret_ptr = 0; break;
        case 'c': *(char *)ret_ptr = 0; break;
        case 'b': *(int *)ret_ptr = 0; break;
        case '2': *(short *)ret_ptr = 0; break;
        case 's': case 'p': *(void **)ret_ptr = 0; break;
        default: break;
        }
    }
    name += 5;                          /* skip "shim_" */

    if (grow_n > 0 && strcmp(name, "print_glyph") != 0)
        grow_flush();                   /* keep RPC order */

    if (!strcmp(name, "print_glyph")) {
        grow_add(a[0].i, a[1].i, a[2].i,
                 (const glyph_info *)a[3].p, (const glyph_info *)a[4].p);
    } else if (!strcmp(name, "putstr")) {
        t8call_post(nh(NHC_putstr), (int)a[0].i, (int)a[1].i, (const char *)a[2].p);
    } else if (!strcmp(name, "curs")) {
        t8call_post(nh(NHC_curs), (int)a[0].i, (int)a[1].i, (int)a[2].i);
    } else if (!strcmp(name, "create_nhwindow")) {
        long r = t8call(nh(NHC_create), (int)a[0].i);
        if (ret_ptr) *(int *)ret_ptr = (int)r;
    } else if (!strcmp(name, "clear_nhwindow")) {
        t8call_post(nh(NHC_clear), (int)a[0].i);
    } else if (!strcmp(name, "display_nhwindow")) {
        /* blocking (--More--) needs the host's ack/key; a plain flush
           is void and pipelines */
        if (a[1].i)
            t8call(nh(NHC_display), (int)a[0].i, 1);
        else
            t8call_post(nh(NHC_display), (int)a[0].i, 0);
    } else if (!strcmp(name, "destroy_nhwindow")) {
        t8call(nh(NHC_destroy), (int)a[0].i);
    } else if (!strcmp(name, "nhgetch")) {
        long r = t8call(nh(NHC_getch), (int)iflags.num_pad);
        if (ret_ptr) *(int *)ret_ptr = (int)r;
    } else if (!strcmp(name, "nh_poskey")) {
        short x = 0, y = 0;
        int mod = 0;
        long key = t8call(nh(NHC_poskey), (int)iflags.num_pad, &x, &y, &mod);
        if (ret_ptr) *(int *)ret_ptr = (int)key;
        if (a[0].p) *(coordxy *)a[0].p = (coordxy)x;
        if (a[1].p) *(coordxy *)a[1].p = (coordxy)y;
        if (a[2].p) *(int *)a[2].p = mod;
    } else if (!strcmp(name, "yn_function")) {
        long r = t8call(nh(NHC_ynfn), (int)a[2].i,
                        (const char *)a[0].p, (const char *)a[1].p);
        if (ret_ptr) *(char *)ret_ptr = t8call_errno ? '\033' : (char)r;
    } else if (!strcmp(name, "getlin")) {
        if (a[1].p) {
            ((char *)a[1].p)[0] = 0;
            t8call(nh(NHC_getlin), (const char *)a[0].p, (char *)a[1].p);
        }
    } else if (!strcmp(name, "start_menu")) {
        mi_n = 0;
        mi_win = (winid)a[0].i;
        t8call(nh(NHC_menu_start), (int)a[0].i, (int)a[1].i);
    } else if (!strcmp(name, "add_menu")) {
        /* args: win glyphinfo identifier ch gch attr clr str flags */
        const glyph_info *gi = (const glyph_info *)a[1].p;
        const anything *id = (const anything *)a[2].p;
        int idx = -1, fl = 0;
        if (mi_n < NHC_MAXMENU) {
            idx = mi_n++;
            mi_id[idx] = id ? *id : cg.zeroany;
            mi_flags[idx] = (unsigned)a[8].i;
        }
        /* bit0 preselected, bit1 selectable (zero id = header line) */
        if (a[8].i & MENU_ITEMFLAGS_SELECTED) fl |= 1;
        if (id && id->a_void) fl |= 2;
        t8call(nh(NHC_menu_add), (int)a[0].i, idx, (int)a[3].i, (int)a[4].i,
               (int)a[5].i, (int)a[6].i, gi ? (int)gi->ttychar : 0, fl,
               (gi && gi->glyph != NO_GLYPH) ? (int)gi->gm.tileidx : -1,
               (const char *)a[7].p);
    } else if (!strcmp(name, "end_menu")) {
        t8call(nh(NHC_menu_end), (int)a[0].i, (const char *)a[1].p);
    } else if (!strcmp(name, "select_menu")) {
        static long sel[2 * NHC_MAXMENU];
        int nsel = 2 * NHC_MAXMENU;
        long cnt = t8call(nh(NHC_menu_sel), (int)a[0].i, (int)a[1].i, sel, &nsel);
        if (t8call_errno)
            cnt = -1;
        /* clamp the host's count to what the reply carries */
        if (cnt > nsel / 2) cnt = nsel / 2;
        if (cnt > NHC_MAXMENU) cnt = NHC_MAXMENU;
        if (cnt > 0 && a[2].p) {
            menu_item *mi = (menu_item *) alloc((unsigned)cnt * sizeof(menu_item));
            long k;
            for (k = 0; k < cnt; k++) {
                long idx = sel[2 * k];
                if (idx >= 0 && idx < mi_n) {
                    mi[k].item = mi_id[idx];
                    mi[k].itemflags = mi_flags[idx];
                } else {
                    mi[k].item = cg.zeroany;
                    mi[k].itemflags = 0;
                }
                mi[k].count = sel[2 * k + 1];
            }
            *(menu_item **)a[2].p = mi;
        }
        if (ret_ptr) *(int *)ret_ptr = (int)cnt;
    } else if (!strcmp(name, "raw_print") || !strcmp(name, "raw_print_bold")) {
        t8call(nh(NHC_rawprint), name[9] != 0, (const char *)a[0].p);
    } else if (!strcmp(name, "exit_nhwindows")) {
        t8call(nh(NHC_exit), (const char *)a[0].p);
    } else if (!strcmp(name, "status_update")) {
        /* args: fld ptr chg pct color colormasks */
        long fld = a[0].i;
        const void *payload = a[1].p;
        int n;
        if (fld == BL_CONDITION && payload)
            n = 8;                      /* raw condition bit words */
        else {
            payload = payload ? payload : "";
            n = (int)strlen((const char *)payload);
        }
        t8call_post(nh(NHC_status), (int)fld, (int)a[2].i, (int)a[3].i, (int)a[4].i,
                    payload, n);
    } else if (!strcmp(name, "cliparound")) {
        t8call_post(nh(NHC_cliparound), (int)a[0].i, (int)a[1].i);
    } else if (!strcmp(name, "get_ext_cmd")) {
        /* ship the extended-command table once (chunked), then let the
           host run its completing input and return the extcmdlist index */
        static int extlist_sent;
        if (!extlist_sent) {
            static unsigned char tab[440];
            int k = 0, first = 1;
            while (extcmdlist[k].ef_txt) {
                int at = 0, cnt = 0;
                while (extcmdlist[k].ef_txt) {
                    const struct ext_func_tab *e = &extcmdlist[k];
                    int l = (int)strlen(e->ef_txt);
                    if (at + 10 + l > (int)sizeof tab)
                        break;
                    if (!(e->flags & INTERNALCMD)
                        && (!(e->flags & WIZMODECMD) || wizard)) {
                        t8_st32(tab + at, (t8_u32)k);
                        t8_st32(tab + at + 4, (e->flags & AUTOCOMPLETE) ? 1 : 0);
                        t8_st16(tab + at + 8, (unsigned)l);
                        memcpy(tab + at + 10, e->ef_txt, (size_t)l);
                        at += 10 + l;
                        cnt++;
                    }
                    k++;
                }
                t8call_post(nh(NHC_extlist), first, cnt, tab, at);
                first = 0;
            }
            extlist_sent = 1;
        }
        {
            long r = t8call(nh(NHC_extcmd));
            if (ret_ptr) *(int *)ret_ptr = t8call_errno ? -1 : (int)r;
        }
    } else if (!strcmp(name, "mark_synch") || !strcmp(name, "wait_synch")) {
        t8call(nh(NHC_sync));
    } else if (!strcmp(name, "nhbell")) {
        t8call(nh(NHC_bell));
    } else if (!strcmp(name, "delay_output")) {
        /* post, not call: the host paces the frame delay itself, and
           posts are FIFO so it lands between consecutive frames */
        t8call_post(nh(NHC_delay));
    } else if (!strcmp(name, "askname")) {
        /* the host prompts for the name in its own style */
        char nm[PL_NSIZ];
        nm[0] = 0;
        t8call(nh(NHC_askname), nm);
        if (nm[0])
            strcpy(svp.plname, nm);
    } else if (!strcmp(name, "putmsghistory")) {
        /* restore-time message replay into the message window */
        if (iflags.window_inited && WIN_MESSAGE != WIN_ERR && a[0].p)
            t8call(nh(NHC_putstr), (int)WIN_MESSAGE, 0, (const char *)a[0].p);
    } else if (!strcmp(name, "display_file")) {
        /* the file is on the host disk but only our stdio reaches it;
           feed a host text window */
        FILE *df = fopen((const char *)a[0].p, "r");
        if (!df) {
            if (a[1].i)
                t8call(nh(NHC_rawprint), 0, "Cannot open file.");
        } else {
            char line[200];
            long wid = t8call(nh(NHC_create), (int)NHW_TEXT);
            if (wid >= 0 && !t8call_errno) {
                while (fgets(line, sizeof line, df)) {
                    int l = (int)strlen(line);
                    while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                        line[--l] = 0;
                    t8call(nh(NHC_putstr), (int)wid, 0, line);
                }
                t8call(nh(NHC_display), (int)wid, 1);
                t8call(nh(NHC_destroy), (int)wid);
            }
            fclose(df);
        }
    } else if (!strcmp(name, "init_nhwindows")) {
        /* informational: signatures are checked per row */
        host_ver = (int)t8call(nh(NHC_init), NHC_VERSION);
        printf("t800 nethack: host windowproc v%d\n", host_ver);
    } else if (!strcmp(name, "player_selection_or_tty")) {
        if (ret_ptr) *(int *)ret_ptr = 1;
    } else if (!strcmp(name, "player_selection")) {
        /* the shim reduces win_player_selection() to this callback */
        t800_player_selection();
    } else if (!strcmp(name, "message_menu")) {
        if (ret_ptr) *(char *)ret_ptr = (char)a[0].i;
    } else if (!strcmp(name, "doprev_message")) {
        t8call_post(nh(NHC_doprev));    /* Ctrl-P: recall previous message */
    }
    /* everything else: local no-op with the default return */
}
