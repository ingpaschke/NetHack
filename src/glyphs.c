/* NetHack 5.0	glyphs.c	TODO: add NHDT branch/date/revision tags */
/* Copyright (c) Michael Allison, 2021. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

extern const struct symparse loadsyms[];
extern glyph_map glyphmap[MAX_GLYPH];
extern struct enum_dump monsdump[];
extern struct enum_dump objdump[];

#define Fprintf (void) fprintf

enum reserved_activities { res_nothing, res_dump_glyphnames };
enum things_to_find { find_nothing, find_pm, find_oc, find_cmap, find_glyph };
struct find_struct {
    enum things_to_find findtype;
    int val;
    int loadsyms_offset;
    int loadsyms_count;
    int *extraval;
    uint32 color;
    const char *unicode_val; /* U+NNNN format */
    void (*callback)(int glyph, struct find_struct *);
    enum reserved_activities restype;
    genericptr_t reserved;
};
static const struct find_struct zero_find = { 0 };
/* Open-addressed hash table over the 16-bit canonical-name hash.
   See populate_glyphname_hash_indices() below for the layout / sizing
   rationale.  Bucket = (uint16 hash, uint16 glyphnum); NO_GLYPH marks
   an empty bucket. */
#define GLYPHNAME_HT_LSIZE 14
#define GLYPHNAME_HT_SIZE  (1U << GLYPHNAME_HT_LSIZE)   /* 16384 */
#define GLYPHNAME_HT_MASK  (GLYPHNAME_HT_SIZE - 1U)
/* Build-time: the table stores glyph numbers as uint16, and the empty-
   bucket sentinel is NO_GLYPH == MAX_GLYPH.  Both must fit in uint16. */
typedef char glyphname_ht_sentinel_fits[NO_GLYPH <= 65535 ? 1 : -1];
typedef char glyphname_ht_load_under_full[MAX_GLYPH < (int)GLYPHNAME_HT_SIZE ? 1 : -1];
struct glyphname_hash_index_entry_t {
    uint16 hash;
    uint16 glyphnum;
};
static struct glyphname_hash_index_entry_t *glyphname_hash_indices_ptr;
static struct find_struct to_custom_symbol_find;
static const long nonzero_black = CLR_BLACK | NH_BASIC_COLOR;

staticfn int find_glyph_in_hashtable(const char *id);
staticfn uint16 glyph_hash16(const char *id);
staticfn int compose_glyph_name(int glyph, char *buf, size_t bufsz);
staticfn int get_cmap_offset(void);
staticfn void to_custom_symset_entry_callback(int glyph,
                                            struct find_struct *findwhat);
staticfn int parse_id(const char *id, struct find_struct *findwhat);
staticfn int glyph_find_core(const char *id, struct find_struct *findwhat);
staticfn char *fix_glyphname(char *str);
staticfn void shuffle_customizations(void);
/* staticfn void purge_custom_entries(enum graphics_sets which_set); */

staticfn void
to_custom_symset_entry_callback(
    int glyph,
    struct find_struct *findwhat)
{
    int idx = gs.symset_which_set;
#ifdef ENHANCED_SYMBOLS
    uint8 utf8str[6] = { 0, 0, 0, 0, 0, 0 };
    int uval = 0;
#endif

    if (findwhat->extraval)
        *findwhat->extraval = glyph;

    assert(idx >= 0 && idx < NUM_GRAPHICS);
#ifdef ENHANCED_SYMBOLS
    if (findwhat->unicode_val)
        uval = unicode_val(findwhat->unicode_val);
    if (uval && unicodeval_to_utf8str(uval, utf8str, sizeof utf8str)) {
        /* presently the customizations are affiliated with a particular
         * symset but if we don't have any symset context, ignore it for now
         * in order to avoid a segfault.
         * FIXME:
         * One future idea might be to store the U+ entries under "UTF8"
         * and apply those customizations to any current symset if it has
         * a UTF8 handler. Similar approach for unaffiliated glyph/symbols
         * non-UTF color customizations
         */
        if (gs.symset[idx].name) {
            add_custom_urep_entry(gs.symset[idx].name, glyph, uval,
                              utf8str, gs.symset_which_set);
        } else {
            static int glyphnag = 0;

            if (!glyphnag++)
                config_error_add("Unimplemented customization feature,"
                                 " ignoring for now");
        }
    }
#endif
    if (findwhat->color) {
        if (gs.symset[idx].name) {
            add_custom_nhcolor_entry(gs.symset[idx].name, glyph,
                                 findwhat->color, gs.symset_which_set);
        } else {
            static int colornag = 0;

            if (!colornag++)
                config_error_add("Unimplemented customization feature,"
                                 " ignoring for now");
        }
    }
}

/*
 * Return value:
 *               1 = success
 *               0 = failure
 */
int
glyphrep_to_custom_map_entries(
    const char *op,
    int *glyphptr)
{
    to_custom_symbol_find = zero_find;
    char buf[BUFSZ], *c_glyphname, *c_unicode, *c_colorval, *cp;
    int reslt = 0;
    long rgb = 0L;
    boolean slash = FALSE, colon = FALSE;

    if (!glyphname_hash_indices_ptr)
        reslt = 1; /* for debugger use only; no cache available */
    nhUse(reslt);

    Snprintf(buf, sizeof buf, "%s", op);
    c_unicode = c_colorval = (char *) 0;
    c_glyphname = cp = buf;
    while (*cp) {
        if (*cp == ':' || *cp == '/') {
            if (*cp == ':') {
                colon = TRUE;
                *cp = '\0';
            }
            if (*cp == '/') {
                slash = TRUE;
                *cp = '\0';
            }
        }
        cp++;
        if (colon) {
            c_unicode = cp;
            colon = FALSE;
        }
        if (slash) {
            c_colorval = cp;
            slash = FALSE;
        }
    }
    /* some sanity checks */
    if (c_glyphname && *c_glyphname == ' ')
        c_glyphname++;
    if (c_colorval && *c_colorval == ' ')
        c_colorval++;
    if (c_unicode && *c_unicode == ' ') {
        while (*c_unicode == ' ') {
            c_unicode++;
        }
    }
    if (c_unicode && !*c_unicode)
        c_unicode = 0;

    if ((c_colorval && (rgb = rgbstr_to_int32(c_colorval)) != -1L)
        || !c_colorval) {
        /* if the color 0 is an actual color, as opposed to just "not set"
           we set a marker bit outside the 24-bit range to indicate a
           valid color value 0. That allows valid color 0, but allows a
           simple checking for 0 to detect "not set". The window port that
           implements the color switch, needs to either check that bit
           or appropriately mask colors with 0xFFFFFF. */
        to_custom_symbol_find.color = (rgb == -1 || !c_colorval) ? 0L
                                      : (rgb == 0L) ? nonzero_black
                                                    : rgb;
    }
    if (c_unicode)
        to_custom_symbol_find.unicode_val = c_unicode;
    to_custom_symbol_find.extraval = glyphptr;
    to_custom_symbol_find.callback = to_custom_symset_entry_callback;
    reslt = glyph_find_core(c_glyphname, &to_custom_symbol_find);
    return reslt;
}

staticfn char *
fix_glyphname(char *str)
{
    char *c;

    for (c = str; *c; c++) {
        if (*c >= 'A' && *c <= 'Z')
            *c += (char) ('a' - 'A');
        else if (*c >= '0' && *c <= '9')
            ;
        else if (*c < 'a' || *c > 'z')
            *c = '_';
    }
    return str;
}

/* Index into loadsyms[] of the first SYM_PCHAR entry.  Cached lazily;
   loadsyms[] is static read-only data so a one-time scan suffices. */
static int cached_cmap_offset = -1;

staticfn int
get_cmap_offset(void)
{
    if (cached_cmap_offset < 0) {
        int i;

        for (i = 0; loadsyms[i].range; i++) {
            if (loadsyms[i].range == SYM_PCHAR) {
                cached_cmap_offset = i;
                return i;
            }
        }
        cached_cmap_offset = 0; /* no SYM_PCHAR found; shouldn't happen */
    }
    return cached_cmap_offset;
}

/* Build the canonical "G_xxx" identifier for the given glyph into buf.
 * Returns 1 if a name was produced, 0 if this glyph has no canonical
 * name (the scroll/gem appearance gaps); buf[0] is set to "G_" on the
 * 0-return paths but callers check the return value before inspecting.
 *
 * The literal fragments below are written in their already-canonical
 * form (lowercase, '_' for word boundaries) so they pass through
 * fix_glyphname() unchanged.  Data-driven fragments (monsdump[].nm,
 * obj_descr[].oc_name / oc_descr) may have uppercase or spaces and rely
 * on the fix_glyphname() pass at the end to normalise.
 *
 * Uses cursor-based appends (single-pass write) rather than Strcat to
 * skip the scan-to-null-terminator that Strcat would do on each call.
 * Callers must pass a BUFSZ-sized buffer.
 *
 * Used by find_glyph_in_hashtable() to verify hash matches, by the
 * --dumpglyphnames path, by populate_glyphname_hash_indices() to fill
 * the hash table, and by wizcustom_glyphnames().
 */
staticfn int
compose_glyph_name(int glyph, char *buf, size_t bufsz)
{
    int i, j, mnum;
    int cmap_offset = get_cmap_offset();
    char *p, *end;

#define APPEND(s)                                                       \
    do {                                                                \
        const char *_q = (s);                                           \
        while (*_q && p < end)                                          \
            *p++ = *_q++;                                               \
    } while (0)
#define APPEND_FMT(fmt, n)                                              \
    do {                                                                \
        char _b[16];                                                    \
        Snprintf(_b, sizeof _b, (fmt), (n));                            \
        APPEND(_b);                                                     \
    } while (0)
#define FINISH() do { *p = '\0'; fix_glyphname(buf + 2); return 1; } while (0)

    if (bufsz < BUFSZ)
        return 0;
    p = buf;
    end = buf + bufsz - 1;
    APPEND("G_");

    if (glyph_is_monster(glyph)) {
        if (glyph_is_normal_male_monster(glyph))            APPEND("male_");
        else if (glyph_is_normal_female_monster(glyph))     APPEND("female_");
        else if (glyph_is_ridden_male_monster(glyph))       APPEND("ridden_male_");
        else if (glyph_is_ridden_female_monster(glyph))     APPEND("ridden_female_");
        else if (glyph_is_detected_male_monster(glyph))     APPEND("detected_male_");
        else if (glyph_is_detected_female_monster(glyph))   APPEND("detected_female_");
        else if (glyph_is_male_pet(glyph))                  APPEND("pet_male_");
        else if (glyph_is_female_pet(glyph))                APPEND("pet_female_");
        APPEND(monsdump[glyph_to_mon(glyph)].nm);
        FINISH();
    }
    if (glyph_is_body(glyph)) {
        APPEND(glyph_is_body_piletop(glyph) ? "piletop_body_" : "body_");
        APPEND(monsdump[glyph_to_body_corpsenm(glyph)].nm);
        FINISH();
    }
    if (glyph_is_statue(glyph)) {
        APPEND(glyph_is_fem_statue_piletop(glyph)
                                                ? "piletop_statue_of_female_"
                : glyph_is_fem_statue(glyph)    ? "statue_of_female_"
                : glyph_is_male_statue_piletop(glyph)
                                                ? "piletop_statue_of_male_"
                : glyph_is_male_statue(glyph)   ? "statue_of_male_"
                                                : "");
        APPEND(monsdump[glyph_to_statue_corpsenm(glyph)].nm);
        FINISH();
    }
    if (glyph_is_object(glyph)) {
        i = glyph_to_obj(glyph);
        if (((i > SCR_STINKING_CLOUD) && (i < SCR_MAIL))
            || ((i > WAN_LIGHTNING) && (i < GOLD_PIECE)))
            return 0;
        if (glyph_is_normal_piletop_obj(glyph)
            || glyph_is_piletop_generic_obj(glyph))
            APPEND("piletop_");

        if ((i >= WAN_LIGHT) && (i <= WAN_LIGHTNING))
            APPEND("wand_of_");
        else if ((i >= SPE_DIG) && (i < SPE_BLANK_PAPER))
            APPEND("spellbook_of_");
        else if ((i >= SCR_ENCHANT_ARMOR) && (i <= SCR_STINKING_CLOUD))
            APPEND("scroll_of_");
        else if ((i >= POT_GAIN_ABILITY) && (i <= POT_WATER))
            APPEND(i == POT_WATER ? "flask_of_n" : "potion_of_");
        else if ((i >= RIN_ADORNMENT) && (i <= RIN_PROTECTION_FROM_SHAPE_CHAN))
            APPEND("ring_of_");
        else if (i == LAND_MINE)
            APPEND("unset_");

        if (i == SCR_BLANK_PAPER)        APPEND("blank_scroll");
        else if (i == SPE_BLANK_PAPER)   APPEND("blank_spellbook");
        else if (i == SLIME_MOLD)        APPEND("slime_mold");
        else if (obj_descr[i].oc_name)   APPEND(obj_descr[i].oc_name);
        else                             APPEND(obj_descr[i].oc_descr);
        FINISH();
    }
    if (glyph_is_cmap(glyph) || glyph_is_cmap_zap(glyph)
        || glyph_is_swallow(glyph) || glyph_is_explosion(glyph)) {
        int cmap = -1;
        const char *suffix = "";

        if (glyph == GLYPH_CMAP_OFF) {
            APPEND("stone_substrate");
            FINISH();
        } else if (glyph_is_cmap_gehennom(glyph)) {
            cmap = (glyph - GLYPH_CMAP_GEH_OFF) + S_vwall;  suffix = "_gehennom";
        } else if (glyph_is_cmap_knox(glyph)) {
            cmap = (glyph - GLYPH_CMAP_KNOX_OFF) + S_vwall; suffix = "_knox";
        } else if (glyph_is_cmap_main(glyph)) {
            cmap = (glyph - GLYPH_CMAP_MAIN_OFF) + S_vwall; suffix = "_main";
        } else if (glyph_is_cmap_mines(glyph)) {
            cmap = (glyph - GLYPH_CMAP_MINES_OFF) + S_vwall; suffix = "_mines";
        } else if (glyph_is_cmap_sokoban(glyph)) {
            cmap = (glyph - GLYPH_CMAP_SOKO_OFF) + S_vwall; suffix = "_sokoban";
        } else if (glyph_is_cmap_a(glyph)) {
            cmap = (glyph - GLYPH_CMAP_A_OFF) + S_ndoor;
        } else if (glyph_is_cmap_altar(glyph)) {
            static const char *const altar_text[] = {
                "unaligned", "chaotic", "neutral", "lawful", "other",
            };

            j = glyph - GLYPH_ALTAR_OFF;
            if (j != altar_other) {
                APPEND(altar_text[j]);
                APPEND("_");
                cmap = S_altar;
            } else {
                APPEND("altar_other");
                FINISH();
            }
        } else if (glyph_is_cmap_b(glyph)) {
            cmap = (glyph - GLYPH_CMAP_B_OFF) + S_grave;
        } else if (glyph_is_cmap_zap(glyph)) {
            static const char *const zap_texts[] = {
                "missile", "fire",      "frost",     "sleep",
                "death",   "lightning", "poison_gas","acid",
            };

            j = glyph - GLYPH_ZAP_OFF;
            APPEND(zap_texts[j / 4]);
            APPEND("_zap_");
            APPEND(loadsyms[(j % 4) + S_vbeam + cmap_offset].name + 2);
            FINISH();
        } else if (glyph_is_cmap_c(glyph)) {
            cmap = (glyph - GLYPH_CMAP_C_OFF) + S_digbeam;
        } else if (glyph_is_swallow(glyph)) {
            static const char *const swallow_texts[] = {
                "top_left",      "top_center",   "top_right",
                "middle_left",   "middle_right", "bottom_left",
                "bottom_center", "bottom_right",
            };

            j = glyph - GLYPH_SWALLOW_OFF;
            cmap = glyph_to_swallow(glyph);
            mnum = j / ((S_sw_br - S_sw_tl) + 1);
            APPEND("swallow_");
            APPEND(monsdump[mnum].nm);
            APPEND("_");
            APPEND(swallow_texts[cmap]);
            FINISH();
        } else if (glyph_is_explosion(glyph)) {
            static const char *const expl_type_texts[] = {
                "dark",    "noxious", "muddy",  "wet",
                "magical", "fiery",   "frosty",
            };
            static const char *const expl_texts[] = {
                "tl", "tc", "tr", "ml", "mc",
                "mr", "bl", "bc", "br",
            };
            int expl;

            j = glyph - GLYPH_EXPLODE_OFF;
            expl = j / ((S_expl_br - S_expl_tl) + 1);
            i = glyph_to_explosion(glyph);
            APPEND(expl_type_texts[expl]);
            APPEND("_expl_");
            APPEND(expl_texts[i]);
            FINISH();
        }
        /* sub-predicates above are exhaustive by construction; if we
           reach here 'cmap' is valid. */
        if (cmap >= 0 && cmap < MAXPCHARS)
            APPEND(loadsyms[cmap + cmap_offset].name + 2);
        APPEND(suffix);
        FINISH();
    }
    if (glyph_is_invisible(glyph))  { APPEND("invisible"); FINISH(); }
    if (glyph_is_nothing(glyph))    { APPEND("nothing"); FINISH(); }
    if (glyph_is_unexplored(glyph)) { APPEND("unexplored"); FINISH(); }
    if (glyph_is_warning(glyph)) {
        APPEND("warning");
        APPEND_FMT("%d", glyph - GLYPH_WARNING_OFF);
        FINISH();
    }
    nhUse(mnum);
    return 0;
#undef APPEND
#undef APPEND_FMT
#undef FINISH
}

int
glyph_to_cmap(int glyph)
{
    if (glyph == GLYPH_CMAP_STONE_OFF)
        return S_stone;
    else if (glyph_is_cmap_main(glyph))
        return (glyph - GLYPH_CMAP_MAIN_OFF) + S_vwall;
    else if (glyph_is_cmap_mines(glyph))
        return (glyph - GLYPH_CMAP_MINES_OFF) + S_vwall;
    else if (glyph_is_cmap_gehennom(glyph))
        return (glyph - GLYPH_CMAP_GEH_OFF) + S_vwall;
    else if (glyph_is_cmap_knox(glyph))
        return (glyph - GLYPH_CMAP_KNOX_OFF) + S_vwall;
    else if (glyph_is_cmap_sokoban(glyph))
        return (glyph - GLYPH_CMAP_SOKO_OFF) + S_vwall;
    else if (glyph_is_cmap_a(glyph))
        return (glyph - GLYPH_CMAP_A_OFF) + S_ndoor;
    else if (glyph_is_cmap_altar(glyph))
        return S_altar;
    else if (glyph_is_cmap_b(glyph))
        return (glyph - GLYPH_CMAP_B_OFF) + S_grave;
    else if (glyph_is_cmap_c(glyph))
        return (glyph - GLYPH_CMAP_C_OFF) + S_digbeam;
    else if (glyph_is_cmap_zap(glyph))
        return ((glyph - GLYPH_ZAP_OFF) % 4) + S_vbeam;
    else if (glyph_is_swallow(glyph))
        return glyph_to_swallow(glyph) + S_sw_tl;
    else if (glyph_is_explosion(glyph))
        return glyph_to_explosion(glyph) + S_expl_tl;
    else
        return MAXPCHARS;    /* MAXPCHARS is legal array index because
                              * of trailing fencepost entry */
}

staticfn int
glyph_find_core(
    const char *id,
    struct find_struct *findwhat)
{
    int glyph;
    boolean do_callback, end_find = FALSE;

    if (parse_id(id, findwhat)) {
        if (findwhat->findtype == find_glyph) {
            (*findwhat->callback)(findwhat->val, findwhat);
        } else {
            for (glyph = 0; glyph < MAX_GLYPH; ++glyph) {
                do_callback = FALSE;
                switch (findwhat->findtype) {
                case find_cmap:
                    if (glyph_to_cmap(glyph) == findwhat->val)
                        do_callback = TRUE;
                    break;
                case find_pm:
                    if (glyph_is_monster(glyph)
                        && mons[glyph_to_mon(glyph)].mlet
                           == findwhat->val)
                        do_callback = TRUE;
                    break;
                case find_oc:
                    if (glyph_is_object(glyph)
                        && glyph_to_obj(glyph) == findwhat->val)
                        do_callback = TRUE;
                    break;
                case find_glyph:
                    if (glyph == findwhat->val) {
                        do_callback = TRUE;
                        end_find = TRUE;
                    }
                    break;
                case find_nothing:
                default:
                    end_find = TRUE;
                    break;
                }
                if (do_callback)
                    (findwhat->callback)(glyph, findwhat);
                if (end_find)
                    break;
            }
        }
        return 1;
    }
    return 0;
}

/*
 * glyphname_hash_indices is an open-addressed hash table over the
 * 16-bit canonical-name hash.  Each bucket holds (uint16 hash, uint16
 * glyphnum), 4 bytes; the table size is a fixed power of two (16384,
 * ~60% load for the 9577 named glyphs) so the bucket index is just
 * (hash & GLYPHNAME_HT_MASK).  An empty bucket has glyphnum == NO_GLYPH.
 *
 * Linear probing on collisions: with a 16-bit hash some names share a
 * value (~14% of named slots), and several distinct hashes can also
 * land on the same primary bucket.  The verification step
 * (reconstruct the candidate via compose_glyph_name and strcmpi) sorts
 * those out cheaply.
 *
 * Memory: 16384 * 4 = 64 KB, one allocation, no per-name strings.
 * No sort phase during populate -- big win on slow m68k.
 */

staticfn uint16
glyph_hash16(const char *id)
{
    uint16 hash = 0;
    size_t i;

    for (i = 0; id[i] != '\0'; ++i) {
        char ch = id[i];

        if ('A' <= ch && ch <= 'Z')
            ch += 'a' - 'A';
        hash = (uint16) ((hash << 5) | (hash >> 11));
        hash ^= (unsigned char) ch;
    }
    return hash;
}

void
populate_glyphname_hash_indices(void)
{
    int glyph;
    size_t i;
    char buf[BUFSZ];

    if (glyphname_hash_indices_ptr)
        return;
    glyphname_hash_indices_ptr = (struct glyphname_hash_index_entry_t *) alloc(
        GLYPHNAME_HT_SIZE * sizeof (struct glyphname_hash_index_entry_t));
    for (i = 0; i < GLYPHNAME_HT_SIZE; i++) {
        glyphname_hash_indices_ptr[i].hash = 0;
        glyphname_hash_indices_ptr[i].glyphnum = (uint16) NO_GLYPH;
    }

    for (glyph = 0; glyph < MAX_GLYPH; ++glyph) {
        uint16 h;
        size_t idx;

        if (!compose_glyph_name(glyph, buf, sizeof buf))
            continue;
        h = glyph_hash16(buf);
        idx = h & GLYPHNAME_HT_MASK;
        /* Linear probe to the first empty slot.  Load < 60% bounds the
           expected probe length to ~1.5; the load-under-full build-time
           assertion above guarantees there is always an empty slot. */
        {
            size_t probes = 0;

            while (glyphname_hash_indices_ptr[idx].glyphnum
                   != (uint16) NO_GLYPH) {
                idx = (idx + 1) & GLYPHNAME_HT_MASK;
                if (++probes >= GLYPHNAME_HT_SIZE)
                    panic("populate_glyphname_hash_indices: table full");
            }
        }
        glyphname_hash_indices_ptr[idx].hash = h;
        glyphname_hash_indices_ptr[idx].glyphnum = (uint16) glyph;
    }
}

void
empty_glyphname_hash_indices(void)
{
    if (!glyphname_hash_indices_ptr)
        return;
    free(glyphname_hash_indices_ptr);
    glyphname_hash_indices_ptr = (struct glyphname_hash_index_entry_t *) 0;
}

staticfn int
find_glyph_in_hashtable(const char *id)
{
    uint16 want = glyph_hash16(id);
    size_t idx = want & GLYPHNAME_HT_MASK;
    char buf[BUFSZ];
    size_t probes = 0;

    /* Linear probe.  Stop at an empty bucket (definitive miss) or after
       walking the whole table (defensive). */
    while (glyphname_hash_indices_ptr[idx].glyphnum != (uint16) NO_GLYPH) {
        if (glyphname_hash_indices_ptr[idx].hash == want) {
            int g = (int) glyphname_hash_indices_ptr[idx].glyphnum;

            if (compose_glyph_name(g, buf, sizeof buf) && !strcmpi(id, buf))
                return g;
        }
        idx = (idx + 1) & GLYPHNAME_HT_MASK;
        if (++probes >= GLYPHNAME_HT_SIZE)
            break;
    }
    return -1;
}

boolean
glyphname_hash_indices_loaded(void)
{
    return (glyphname_hash_indices_ptr != 0);
}

int
match_glyph(char *buf)
{
    char workbuf[BUFSZ];

    /* buf contains a G_ glyph reference, not an S_ symbol.
        There could be an R-G-B color attached too.
        Let's get a copy to work with. */
    Snprintf(workbuf, sizeof workbuf, "%s", buf); /* get a copy */
    return glyphrep(workbuf);
}

int
glyphrep(const char *op)
{
    int reslt = 0, glyph = NO_GLYPH;

    if (!glyphname_hash_indices_ptr)
        reslt = 1;      /* for debugger use only; no cache available */
    nhUse(reslt);
    reslt = glyphrep_to_custom_map_entries(op, &glyph);
    if (reslt)
        return 1;
    return 0;
}

int
add_custom_nhcolor_entry(
    const char *customization_name,
    int glyphidx,
    uint32 nhcolor,
    enum graphics_sets which_set)
{
    struct symset_customization *gdc
                        = &gs.sym_customizations[which_set][custom_nhcolor];
    struct customization_detail *details, *newdetails = 0;
#if 0
    static uint32 closecolor = 0;
    static int clridx = 0;
#endif

    if (!gdc->details) {
        gdc->customization_name = dupstr(customization_name);
        gdc->custtype = custom_nhcolor;
        gdc->details = 0;
        gdc->details_end = 0;
    }
    details = find_matching_customization(customization_name,
                                          custom_nhcolor, which_set);
    if (details) {
        while (details) {
            if (details->content.ccolor.glyphidx == glyphidx) {
                details->content.ccolor.nhcolor = nhcolor;
                return 1;
            }
            details = details->next;
        }
    }
    /* create new details entry */
    newdetails = (struct customization_detail *) alloc(sizeof *newdetails);
    newdetails->content.urep.glyphidx = glyphidx;
    newdetails->content.ccolor.nhcolor = nhcolor;
    newdetails->next = (struct customization_detail *) 0;
    if (gdc->details == NULL) {
        gdc->details = newdetails;
    } else {
        gdc->details_end->next = newdetails;
    }
    gdc->details_end = newdetails;
    gdc->count++;
    return 1;
}

void
apply_customizations(
    enum graphics_sets which_set,
    enum do_customizations docustomize)
{
    glyph_map *gmap;
    struct customization_detail *details;
    struct symset_customization *sc;
    boolean at_least_one = FALSE,
            do_colors = ((docustomize & do_custom_colors) != 0),
            do_symbols = ((docustomize & do_custom_symbols) != 0);
    int custs;

    for (custs = 0; custs < (int) custom_count; ++custs) {
        sc = &gs.sym_customizations[which_set][custs];
        if (sc->count && sc->details) {
            at_least_one = TRUE;
            /* These glyph customizations get applied to the glyphmap array,
               not to symset entries */
            details = sc->details;
            while (details) {
#ifdef ENHANCED_SYMBOLS
                if (iflags.customsymbols && do_symbols) {
                    if (sc->custtype == custom_ureps) {
                        gmap = &glyphmap[details->content.urep.glyphidx];
                        if (gs.symset[which_set].handling == H_UTF8)
                            (void) set_map_u(gmap,
                                             details->content.urep.u.utf32ch,
                                             details->content.urep.u.utf8str);
                    }
                }
#endif
                if (iflags.customcolors && do_colors) {
                    if (sc->custtype == custom_nhcolor) {
                        gmap = &glyphmap[details->content.ccolor.glyphidx];
                        (void) set_map_customcolor(gmap,
                                             details->content.ccolor.nhcolor);
                    }
                }
                details = details->next;
            }
        }
    }
    iflags.pending_customizations = at_least_one;
}

/* Shuffle the customizations to match shuffled object descriptions,
 * so a red potion isn't displayed with a blue customization, and so on.
 */

void
maybe_shuffle_customizations(void)
{
    if (iflags.pending_customizations) {
        shuffle_customizations();
        iflags.pending_customizations = 0;
    }
}

#if 0
staticfn void
shuffle_customizations(void)
{
    static const int offsets[2] = { GLYPH_OBJ_OFF, GLYPH_OBJ_PILETOP_OFF };
    int j;

    for (j = 0; j < SIZE(offsets); j++) {
        glyph_map *obj_glyphs = glyphmap + offsets[j];
        int i;
        struct unicode_representation *tmp_u[NUM_OBJECTS];
        int duplicate[NUM_OBJECTS];

        for (i = 0; i < NUM_OBJECTS; i++) {
            duplicate[i] = -1;
            tmp_u[i] = (struct unicode_representation *) 0;
        }
        for (i = 0; i < NUM_OBJECTS; i++) {
            int idx = objects[i].oc_descr_idx;

            /*
             * Shuffling gem appearances can cause the same oc_descr_idx to
             * appear more than once. Detect this condition and ensure that
             * each pointer points to a unique allocation.
             */
            if (duplicate[idx] >= 0) {
                /* Current structure already appears in tmp_u */
                struct unicode_representation *other = tmp_u[duplicate[idx]];

                tmp_u[i] = (struct unicode_representation *)
                           alloc(sizeof *tmp_u[i]);
                *tmp_u[i] = *other;
                if (other->utf8str != NULL) {
                    tmp_u[i]->utf8str = (uint8 *)
                                        dupstr((const char *) other->utf8str);
                }
            } else {
                tmp_u[i] = obj_glyphs[idx].u;
                if (obj_glyphs[idx].u != NULL)  {
                    duplicate[idx] = i;
                    obj_glyphs[idx].u = NULL;
                }
            }
        }
        for (i = 0; i < NUM_OBJECTS; i++) {
            /* Some glyphmaps may not have been transferred */
            if (obj_glyphs[i].u != NULL) {
                free(obj_glyphs[i].u->utf8str);
                free(obj_glyphs[i].u);
            }
            obj_glyphs[i].u = tmp_u[i];
        }
    }
}

#else
staticfn void
shuffle_customizations(void)
{
    static const int offsets[2] = { GLYPH_OBJ_OFF, GLYPH_OBJ_PILETOP_OFF };
    int j;

    for (j = 0; j < SIZE(offsets); j++) {
        glyph_map *obj_glyphs = glyphmap + offsets[j];
        int i;
#ifdef ENHANCED_SYMBOLS
        struct unicode_representation *tmp_u[NUM_OBJECTS];
#endif
        uint32 tmp_customcolor[NUM_OBJECTS];
        uint16 tmp_color256idx[NUM_OBJECTS];

        int duplicate[NUM_OBJECTS];

        for (i = 0; i < NUM_OBJECTS; i++) {
            duplicate[i] = -1;
#ifdef ENHANCED_SYMBOLS
            tmp_u[i] = (struct unicode_representation *) 0;
#endif
            tmp_customcolor[i] = 0;
            tmp_color256idx[i] = 0;
        }
        for (i = 0; i < NUM_OBJECTS; i++) {
            int idx = objects[i].oc_descr_idx;

            /*
             * Shuffling gem appearances can cause the same oc_descr_idx to
             * appear more than once. Detect this condition and ensure that
             * each pointer points to a unique allocation.
             */
            if (duplicate[idx] >= 0) {
#ifdef ENHANCED_SYMBOLS
                /* Current structure already appears in tmp_u */
                struct unicode_representation *other = tmp_u[duplicate[idx]];
#endif
                uint32 other_customcolor = tmp_customcolor[duplicate[idx]];
                uint16 other_color256idx = tmp_color256idx[duplicate[idx]];

                tmp_customcolor[i] = other_customcolor;
                tmp_color256idx[i] = other_color256idx;
#ifdef ENHANCED_SYMBOLS
                if (other) {
                    tmp_u[i] = (struct unicode_representation *) alloc(
                        sizeof *tmp_u[i]);
                    *tmp_u[i] = *other;
                    if (other->utf8str != NULL) {
                        tmp_u[i]->utf8str =
                            (uint8 *) dupstr((const char *) other->utf8str);
                    }
                }
#endif
            } else {
                tmp_customcolor[i] = obj_glyphs[idx].customcolor;
                tmp_color256idx[i] = obj_glyphs[idx].color256idx;
#ifdef ENHANCED_SYMBOLS
                tmp_u[i] = obj_glyphs[idx].u;
#endif
                if (
#ifdef ENHANCED_SYMBOLS
                    obj_glyphs[idx].u != NULL ||
#endif
                    obj_glyphs[idx].customcolor != 0) {
                    duplicate[idx] = i;
#ifdef ENHANCED_SYMBOLS
                    obj_glyphs[idx].u = NULL;
#endif
                    obj_glyphs[idx].customcolor = 0;
                    obj_glyphs[idx].color256idx = 0;
                }
            }
        }
        for (i = 0; i < NUM_OBJECTS; i++) {
            /* Some glyphmaps may not have been transferred */
#ifdef ENHANCED_SYMBOLS
            if (obj_glyphs[i].u != NULL) {
                free(obj_glyphs[i].u->utf8str);
                free(obj_glyphs[i].u);
            }
            obj_glyphs[i].u = tmp_u[i];
#endif
            obj_glyphs[i].customcolor = tmp_customcolor[i];
            obj_glyphs[i].color256idx = tmp_color256idx[i];
        }
    }
}
#endif

struct customization_detail *
find_matching_customization(
    const char *customization_name,
    enum customization_types custtype,
    enum graphics_sets which_set)
{
    struct symset_customization *gdc
        = &gs.sym_customizations[which_set][custtype];

    if ((gdc->custtype == custtype) && gdc->customization_name
        && (strcmp(customization_name, gdc->customization_name) == 0))
        return gdc->details;
    return (struct customization_detail *) 0;
}

void
purge_all_custom_entries(void)
{
    int i;

    for (i = 0; i < NUM_GRAPHICS + 1; ++i) {
        purge_custom_entries(i);
    }
}

void
purge_custom_entries(enum graphics_sets which_set)
{
    enum customization_types custtype;
    struct symset_customization *gdc;
    struct customization_detail *details, *next;

    for (custtype = custom_none; custtype < custom_count; ++custtype) {
        gdc = &gs.sym_customizations[which_set][custtype];
        details = gdc->details;
        while (details) {
            next = details->next;
            if (gdc->custtype == custom_ureps) {
                if (details->content.urep.u.utf8str)
                    free(details->content.urep.u.utf8str);
                details->content.urep.u.utf8str = (uint8 *) 0;
            } else if (gdc->custtype == custom_symbols) {
                details->content.sym.symparse = (struct symparse *) 0;
                details->content.sym.val = 0;
            } else if (gdc->custtype == custom_nhcolor) {
                details->content.ccolor.nhcolor = 0;
                details->content.ccolor.glyphidx = 0;
            }
            free(details);
            details = next;
        }
        gdc->details = 0;
        gdc->details_end = 0;
        if (gdc->customization_name) {
            free((genericptr_t) gdc->customization_name);
            gdc->customization_name = 0;
        }
        gdc->count = 0;
    }
}

void
dump_all_glyphnames(FILE *fp)
{
    struct find_struct dump_glyphname_find = zero_find;

    dump_glyphname_find.findtype = find_nothing;
    dump_glyphname_find.reserved = (genericptr_t) fp;
    dump_glyphname_find.restype = res_dump_glyphnames;
    (void) parse_id((char *) 0, &dump_glyphname_find);
}

void
wizcustom_glyphnames(winid win)
{
    int glyphnum;
    char buf[BUFSZ];

    for (glyphnum = 0; glyphnum < MAX_GLYPH; ++glyphnum) {
        if (compose_glyph_name(glyphnum, buf, sizeof buf))
            wizcustom_callback(win, glyphnum, buf);
    }
}

staticfn int
parse_id(
    const char *id,
    struct find_struct *findwhat)
{
    FILE *fp = (FILE *) 0;
    int i = 0, glyph,
        pm_offset = 0, oc_offset = 0, cmap_offset = 0,
        pm_count = 0, oc_count = 0, cmap_count = 0;
    boolean dump_ids = FALSE, is_S = FALSE, is_G = FALSE;
    char buf[BUFSZ];

    if (findwhat->findtype == find_nothing && findwhat->restype) {
        if (findwhat->restype == res_dump_glyphnames) {
            if (findwhat->reserved) {
                fp = (FILE *) findwhat->reserved;
                dump_ids = TRUE;
            } else {
                return 0;
            }
        }
    }

    is_G = (id && id[0] == 'G' && id[1] == '_');
    is_S = (id && id[0] == 'S' && id[1] == '_');

    if (dump_ids || is_S) {
        while (loadsyms[i].range) {
            if (!pm_offset && loadsyms[i].range == SYM_MON)
                pm_offset = i;
            if (!pm_count && pm_offset && loadsyms[i].range != SYM_MON)
                pm_count = i - pm_offset;
            if (!oc_offset && loadsyms[i].range == SYM_OC)
                oc_offset = i;
            if (!oc_count && oc_offset && loadsyms[i].range != SYM_OC)
                oc_count = i - oc_offset;
            if (!cmap_offset && loadsyms[i].range == SYM_PCHAR)
                cmap_offset = i;
            if (!cmap_count && cmap_offset && loadsyms[i].range != SYM_PCHAR)
                cmap_count = i - cmap_offset;
            i++;
        }
    }
    if (is_G && id) {
        if (glyphname_hash_indices_ptr) {
            /* Fast path: hash-table lookup with linear probing. */
            int val = find_glyph_in_hashtable(id);

            if (val >= 0) {
                findwhat->findtype = find_glyph;
                findwhat->val = val;
                findwhat->loadsyms_offset = 0;
                return 1;
            }
        } else {
            /* Slow path: caller didn't run populate_glyphname_hash_indices
               first.  Linear-scan every glyph reconstructing its
               canonical name -- matches upstream's fallback behaviour
               and keeps debugger / wizard-mode lookups working without
               surprising the populate/empty cycle. */
            for (glyph = 0; glyph < MAX_GLYPH; ++glyph) {
                if (compose_glyph_name(glyph, buf, sizeof buf)
                    && !strcmpi(id, buf)) {
                    findwhat->findtype = find_glyph;
                    findwhat->val = glyph;
                    findwhat->loadsyms_offset = 0;
                    return 1;
                }
            }
        }
        return 0;
    }
    if (dump_ids) {
        /* iterate and dump every named glyph */
        for (glyph = 0; glyph < MAX_GLYPH; ++glyph) {
            if (compose_glyph_name(glyph, buf, sizeof buf))
                Fprintf(fp, "(%04d) %s\n", glyph, buf);
        }
        return 1;
    }
    if (is_S) {
        /* cmap entries */
        for (i = 0; i < cmap_count; ++i) {
            if (!strcmpi(loadsyms[i + cmap_offset].name + 2, id + 2)) {
                findwhat->findtype = find_cmap;
                findwhat->val = i;
                findwhat->loadsyms_offset = i + cmap_offset;
                return 1;
            }
        }
        /* objclass entries */
        for (i = 0; i < oc_count; ++i) {
            if (!strcmpi(loadsyms[i + oc_offset].name + 2, id + 2)) {
                findwhat->findtype = find_oc;
                findwhat->val = i;
                findwhat->loadsyms_offset = i + oc_offset;
                return 1;
            }
        }
        /* permonst entries */
        for (i = 0; i <= pm_count; ++i) {
            if (!strcmpi(loadsyms[i + pm_offset].name + 2, id + 2)) {
                findwhat->findtype = find_pm;
                findwhat->val = i + 1; /* starts at 1 */
                findwhat->loadsyms_offset = i + pm_offset;
                return 1;
            }
        }
    }
    findwhat->findtype = find_nothing;
    findwhat->val = 0;
    findwhat->loadsyms_offset = 0;
    return 0;
}

/* extern glyph_map glyphmap[MAX_GLYPH]; */

void
clear_all_glyphmap_colors(void)
{
    int glyph;

    for (glyph = 0; glyph < MAX_GLYPH; ++glyph) {
        if (glyphmap[glyph].customcolor)
            glyphmap[glyph].customcolor = 0;
        glyphmap[glyph].color256idx = 0;
    }
}

void
reset_customcolors(void)
{
    clear_all_glyphmap_colors();
    apply_customizations(gc.currentgraphics, do_custom_colors);
}

/* not used yet */

#if 0
staticfn struct customization_detail *find_display_sym_customization(
    const char *customization_name, const struct symparse *symparse,
    enum graphics_sets which_set);
staticfn struct customization_detail *find_display_urep_customization(
    const char *customization_name, int glyphidx,
    enum graphics_sets which_set);

struct customization_detail *
find_display_sym_customization(
    const char *customization_name,
    const struct symparse *symparse,
    enum graphics_sets which_set)
{
    struct symset_customization *gdc;
    struct customization_detail *symdetails;

    gdc = &gs.sym_customizations[which_set][custom_symbols];
    if ((gdc->custtype == custom_symbols)
        && (strcmp(customization_name, gdc->customization_name) == 0)) {
        symdetails = gdc->details;
        while (symdetails) {
            if (symdetails->content.sym.symparse == symparse)
                return symdetails;
            symdetails = symdetails->next;
        }
    }
    return (struct customization_detail *) 0;
}

struct customization_detail *
find_display_urep_customization(
    const char *customization_name,
    int glyphidx,
    enum graphics_sets which_set)
{
    struct symset_customization *gdc = &gs.sym_customizations[which_set];
    struct customization_detail *urepdetails;

    if ((gdc->custtype == custom_reps)
        || (strcmp(customization_name, gdc->customization_name) == 0)) {
        urepdetails = gdc->details;
        while (urepdetails) {
            if (urepdetails->content.urep.glyphidx == glyphidx)
                return urepdetails;
            urepdetails = urepdetails->next;
        }
    }
    return (struct customization_detail *) 0;
}
#endif  /* 0 not used yet */

#ifdef TEST_GLYPHNAMES

static struct {
    int idx;
    const char *nm1;
    const char *nm2;
} cmapname[MAXPCHARS] = {
#define PCHAR_TILES
#include "defsym.h"
#undef PCHAR_TILES
};

void
test_glyphnames(void)
{
    int reslt;

    reslt = find_glyphs("G_potion_of_monster_detection");
    reslt = find_glyphs("G_piletop_body_chickatrice");
    reslt = find_glyphs("G_detected_male_homunculus");
    reslt = find_glyphs("S_pool");
    reslt = find_glyphs("S_dog");
    reslt = glyphs_to_unicode("S_dog", "U+130E6", 0L);
}

staticfn void
just_find_callback(int glyph UNUSED, struct find_struct *findwhat UNUSED)
{
    return;
}

staticfn int
find_glyphs(const char *id)
{
    struct find_struct find_only = zero_find;

    find_only.unicode_val = 0;
    find_only.callback = just_find_callback;
    return glyph_find_core(id, &find_only);
}

staticfn void
to_unicode_callback(int glyph UNUSED, struct find_struct *findwhat)
{
    int uval;
#ifdef NO_PARSING_SYMSET
    glyph_map *gm = &glyphmap[glyph];
#endif
    uint8 utf8str[6];

    if (!findwhat->unicode_val)
        return;
    uval = unicode_val(findwhat->unicode_val);
    if (unicodeval_to_utf8str(uval, utf8str, sizeof utf8str)) {
#ifdef NO_PARSING_SYMSET
        set_map_u(gm, uval, utf8str,
                  (findwhat->color != 0L) ? findwhat->color : 0L);
#else

#endif
    }
}

int
glyphs_to_unicode(const char *id, const char *unicode_val, long clr)
{
    struct find_struct to_unicode = zero_find;

    to_unicode.unicode_val = unicode_val;
    to_unicode.callback = to_unicode_callback;
    /* if the color 0 is an actual color, as opposed to just "not set"
       we set a marker bit outside the 24-bit range to indicate a
       valid color value 0. That allows valid color 0, but allows a
       simple checking for 0 to detect "not set". The window port that
       implements the color switch, needs to either check that bit
       or appropriately mask colors with 0xFFFFFF. */
    to_unicode.color = (clr == -1) ? 0L : (clr == 0L) ? nonzero_black : clr;
    return glyph_find_core(id, &to_unicode);
}

#endif /* SOME TEST STUFF */

/* glyphs.c */



