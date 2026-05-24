/* NetHack 5.0    glyph_tree.c    $NHDT-Date$ */
/* Copyright (c) 2026.  NetHack may be freely redistributed.  See license. */

/* Tree-encoded glyph name parser/emitter.
 *
 * Forward (name -> ID) and reverse (ID -> name) lookups for the
 * G_xxx symbolic glyph identifiers.  Replaces the previous cache
 * mechanism (init_glyph_cache + fill_glyphid_cache + parse_id
 * iteration) with a direct grammar parser.
 *
 * Layout matches the existing GLYPH_*_OFF enum in display.h so save
 * files from the cache-era binaries remain readable.
 */

#include "hack.h"
#include "glyph_tree.h"

extern struct enum_dump monsdump[];     /* from earlyarg.c, NUMMONS+5 long */
extern struct enum_dump objdump[];      /* from earlyarg.c, NUM_OBJECTS+1 */
extern const struct symparse loadsyms[]; /* from symbols.c */

/* -- name normalization & matching ----------------------------- */

/* Local copy of fix_glyphname() — keeps glyph_tree.c self-contained
 * so the legacy parse_id/cache machinery can eventually be removed
 * without leaving a dangling reference. */
staticfn void
gt_fix_glyphname(char *str)
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
}

/* Compare a user-supplied identifier (already in G_-canonical form
 * with lowercased letters and underscores for non-alphanumerics)
 * against a raw enum-dump name (uppercase with spaces/hyphens).
 * Returns 0 on match, non-zero on mismatch.  Mimics fix_glyphname()'s
 * normalization on the `raw` side in a single pass.
 */
staticfn int
gt_strcmp_fixed(const char *user, const char *raw)
{
    while (*user || *raw) {
        unsigned char u = (unsigned char) *user;
        unsigned char r = (unsigned char) *raw;
        if (!u && !r)
            return 0;
        if (!u)
            return -(int) r;
        if (!r)
            return (int) u;
        if (u >= 'A' && u <= 'Z')
            u = (unsigned char) (u + ('a' - 'A'));
        if (r >= 'A' && r <= 'Z')
            r = (unsigned char) (r + ('a' - 'A'));
        if ((r >= 'a' && r <= 'z') || (r >= '0' && r <= '9')) {
            /* keep r as-is */
        } else {
            r = '_';
        }
        if (u != r)
            return (int) u - (int) r;
        user++;
        raw++;
    }
    return 0;
}

/* Linear scan over monsdump[] for a name match. */
staticfn int
gt_lookup_monster(const char *name)
{
    int i;

    if (!name || !*name)
        return -1;
    for (i = 0; i < NUMMONS; i++) {
        if (gt_strcmp_fixed(name, monsdump[i].nm) == 0)
            return i;
    }
    return -1;
}

/* Lazy-init: walk loadsyms[] once to find the start of the SYM_PCHAR
 * range.  The defsym.h-included S_xxxx entries are contiguous; we
 * cache the offset on first use. */
static int gt_cmap_offset_cached = -1;

staticfn int
gt_cmap_offset(void)
{
    int i;

    if (gt_cmap_offset_cached >= 0)
        return gt_cmap_offset_cached;
    for (i = 0; loadsyms[i].range; i++) {
        if (loadsyms[i].range == SYM_PCHAR) {
            gt_cmap_offset_cached = i;
            return i;
        }
    }
    return 0;
}

/* Look up a base S_ name (without the "S_" prefix), case-insensitive.
 * Returns its PCHAR index (0..MAXPCHARS-1), or -1 if not found. */
staticfn int
gt_lookup_pchar(const char *name)
{
    int i, off;

    if (!name || !*name)
        return -1;
    off = gt_cmap_offset();
    for (i = 0; i < MAXPCHARS; i++) {
        const char *n = loadsyms[i + off].name;

        if (!n || n[0] != 'S' || n[1] != '_')
            continue;
        if (gt_strcmp_fixed(name, n + 2) == 0)
            return i;
    }
    return -1;
}

/* -- form-prefix dispatch table -------------------------------- */

/* Maps user-facing form prefix string -> category for monsters_living.
 * Ordered longest-first so "pet_male_" is tried before "male_". */
struct gt_form_prefix {
    const char *prefix;
    unsigned short len;
    int category;       /* one of GTC_MON_MALE..GTC_RIDDEN_FEM */
};

static const struct gt_form_prefix gt_mon_form_prefixes[] = {
    { "detected_female_", 16, GTC_DETECT_FEM   },
    { "detected_male_",   14, GTC_DETECT_MALE  },
    { "ridden_female_",   14, GTC_RIDDEN_FEM   },
    { "ridden_male_",     12, GTC_RIDDEN_MALE  },
    { "pet_female_",      11, GTC_PET_FEM      },
    { "pet_male_",         9, GTC_PET_MALE     },
    { "female_",           7, GTC_MON_FEM      },
    { "male_",             5, GTC_MON_MALE     },
};

/* Reverse direction: category -> form prefix string. */
staticfn const char *
gt_mon_form_prefix_string(int category)
{
    switch (category) {
    case GTC_MON_MALE:    return "male_";
    case GTC_MON_FEM:     return "female_";
    case GTC_PET_MALE:    return "pet_male_";
    case GTC_PET_FEM:     return "pet_female_";
    case GTC_DETECT_MALE: return "detected_male_";
    case GTC_DETECT_FEM:  return "detected_female_";
    case GTC_RIDDEN_MALE: return "ridden_male_";
    case GTC_RIDDEN_FEM:  return "ridden_female_";
    default:              return "";
    }
}

/* -- object class prefix dispatch ------------------------------ */

/* Mirrors parse_id's class-prefix logic.  Each entry is a contiguous
 * range of object enum values that share a canonical prefix.  Order
 * doesn't matter for forward lookup (ranges don't overlap) but is
 * preserved for clarity. */
struct gt_obj_class {
    const char *prefix;      /* canonical underscored form */
    int lo;                  /* inclusive */
    int hi;                  /* inclusive */
};

static const struct gt_obj_class gt_obj_classes[] = {
    { "wand_of_",      WAN_LIGHT,         WAN_LIGHTNING },
    /* SPE_BLANK_PAPER itself is special-cased below (becomes
     * "blank_spellbook"), so the range stops at SPE_BLANK_PAPER - 1. */
    { "spellbook_of_", SPE_DIG,           SPE_BLANK_PAPER - 1 },
    { "scroll_of_",    SCR_ENCHANT_ARMOR, SCR_STINKING_CLOUD },
    /* POT_WATER is special inside the range — gets "flask_of_n" via
     * an in-flow check rather than as its own table entry. */
    { "potion_of_",    POT_GAIN_ABILITY,  POT_WATER },
    { "ring_of_",      RIN_ADORNMENT,     RIN_PROTECTION_FROM_SHAPE_CHAN },
    { "unset_",        LAND_MINE,         LAND_MINE },
};

/* Return the canonical class prefix for an object index, mirroring
 * parse_id's switch.  Returns "" if no prefix. */
staticfn const char *
gt_obj_class_prefix(int obj_idx)
{
    int i;

    if (obj_idx == POT_WATER)
        return "flask_of_n";
    for (i = 0; i < (int) SIZE(gt_obj_classes); i++) {
        if (obj_idx >= gt_obj_classes[i].lo && obj_idx <= gt_obj_classes[i].hi)
            return gt_obj_classes[i].prefix;
    }
    return "";
}

/* Return the canonical base name for an object index, mirroring
 * parse_id's switch. */
staticfn const char *
gt_obj_base_name(int obj_idx)
{
    if (obj_idx == SCR_BLANK_PAPER)
        return "blank scroll";
    if (obj_idx == SPE_BLANK_PAPER)
        return "blank spellbook";
    if (obj_idx == SLIME_MOLD)
        return "slime mold";
    return obj_descr[obj_idx].oc_name
           ? obj_descr[obj_idx].oc_name
           : obj_descr[obj_idx].oc_descr;
}

/* Some objects are skipped by parse_id (returning skip_this_one); we
 * mirror that here so we don't claim a matching glyph for them. */
staticfn boolean
gt_obj_is_skipped(int obj_idx)
{
    if ((obj_idx > SCR_STINKING_CLOUD) && (obj_idx < SCR_MAIL))
        return TRUE;
    if ((obj_idx > WAN_LIGHTNING) && (obj_idx < GOLD_PIECE))
        return TRUE;
    return FALSE;
}

/* Look up an object by its canonical name (the form that would be
 * emitted by emit_object).  Returns object index, or -1 if no match.
 * Linear scan over NUM_OBJECTS — fine for the rare config-parse case. */
staticfn int
gt_lookup_object(const char *name)
{
    int i;
    char canonical[BUFSZ];
    const char *prefix, *base;

    if (!name || !*name)
        return -1;
    for (i = 0; i < NUM_OBJECTS; i++) {
        if (gt_obj_is_skipped(i))
            continue;
        prefix = gt_obj_class_prefix(i);
        base = gt_obj_base_name(i);
        Snprintf(canonical, sizeof canonical, "%s%s", prefix, base);
        gt_fix_glyphname(canonical);
        if (gt_strcmp_fixed(name, canonical) == 0)
            return i;
    }
    return -1;
}

/* -- zap, swallow, explosion text tables ----------------------- */

static const char *const gt_zap_texts[] = {
    "missile", "fire",      "frost",      "sleep",
    "death",   "lightning", "poison gas", "acid",
};

static const char *const gt_swallow_texts[] = {
    "top left",      "top center",   "top right",
    "middle left",   "middle right", "bottom left",
    "bottom center", "bottom right",
};

static const char *const gt_expl_type_texts[] = {
    "dark",    "noxious", "muddy",  "wet",
    "magical", "fiery",   "frosty",
};

static const char *const gt_expl_pos_texts[] = {
    "tl", "tc", "tr", "ml", "mc",
    "mr", "bl", "bc", "br",
};

static const char *const gt_altar_texts[] = {
    "unaligned", "chaotic", "neutral",
    "lawful",    "other",
};

/* -- per-category forward parsers ------------------------------ */

staticfn int
gt_parse_monsters_living(const char *s)
{
    int i, monster;

    for (i = 0; i < (int) SIZE(gt_mon_form_prefixes); i++) {
        const struct gt_form_prefix *fp = &gt_mon_form_prefixes[i];

        if (strncmp(s, fp->prefix, fp->len) != 0)
            continue;
        monster = gt_lookup_monster(s + fp->len);
        if (monster < 0)
            return -1;
        return glyph_tree_offset[fp->category] + monster;
    }
    return -1;
}

staticfn int
gt_parse_singleton(const char *s, const char *expected, int category)
{
    if (strcmp(s, expected) == 0)
        return glyph_tree_offset[category];
    return -1;
}

staticfn int
gt_parse_warning(const char *s)
{
    int n, used;

    /* Expected: "warningN" with 0 <= N < WARNCOUNT. */
    if (strncmp(s, "warning", 7) != 0)
        return -1;
    s += 7;
    if (sscanf(s, "%d%n", &n, &used) != 1 || used != (int) strlen(s))
        return -1;
    if (n < 0 || n >= WARNCOUNT)
        return -1;
    return glyph_tree_offset[GTC_WARNING] + n;
}

staticfn int
gt_parse_body_kind(const char *s, const char *prefix, int prefix_len,
                   int category)
{
    int monster;

    if (strncmp(s, prefix, prefix_len) != 0)
        return -1;
    monster = gt_lookup_monster(s + prefix_len);
    if (monster < 0)
        return -1;
    return glyph_tree_offset[category] + monster;
}

staticfn int
gt_parse_body(const char *s)
{
    return gt_parse_body_kind(s, "body_", 5, GTC_BODY);
}

staticfn int
gt_parse_body_piletop(const char *s)
{
    return gt_parse_body_kind(s, "piletop_body_", 13, GTC_BODY_PILETOP);
}

staticfn int
gt_parse_statue(const char *s)
{
    int monster;

    /* Order: piletop variants first (longer prefix). */
    if (strncmp(s, "piletop_statue_of_male_", 23) == 0) {
        monster = gt_lookup_monster(s + 23);
        if (monster < 0) return -1;
        return glyph_tree_offset[GTC_STATUE_MALE_PILETOP] + monster;
    }
    if (strncmp(s, "piletop_statue_of_female_", 25) == 0) {
        monster = gt_lookup_monster(s + 25);
        if (monster < 0) return -1;
        return glyph_tree_offset[GTC_STATUE_FEM_PILETOP] + monster;
    }
    if (strncmp(s, "statue_of_male_", 15) == 0) {
        monster = gt_lookup_monster(s + 15);
        if (monster < 0) return -1;
        return glyph_tree_offset[GTC_STATUE_MALE] + monster;
    }
    if (strncmp(s, "statue_of_female_", 17) == 0) {
        monster = gt_lookup_monster(s + 17);
        if (monster < 0) return -1;
        return glyph_tree_offset[GTC_STATUE_FEM] + monster;
    }
    return -1;
}

/* CMAP main / branch / a / b / c.  parse_id uses one of:
 *   "G_<pchar>_main"      cmap_main
 *   "G_<pchar>_mines"     cmap_mines
 *   "G_<pchar>_gehennom"  cmap_geh
 *   "G_<pchar>_knox"      cmap_knox
 *   "G_<pchar>_sokoban"   cmap_soko
 *   "G_<pchar>"           cmap_a / b / c (no suffix)
 */
struct gt_cmap_branch_suffix {
    const char *suffix;
    int suffix_len;
    int category;
    int s_base;            /* the S_ symbol this category's range starts at */
};

/* These suffixes only apply to glyphs whose underlying pchar is in
 * the wall range (S_vwall .. S_trwall).  Categories cmap_a/b/c use
 * non-wall pchars and have no suffix. */
static const struct gt_cmap_branch_suffix gt_cmap_branch_suffixes[] = {
    { "_gehennom", 9, GTC_CMAP_GEH,   S_vwall },
    { "_sokoban",  8, GTC_CMAP_SOKO,  S_vwall },
    { "_mines",    6, GTC_CMAP_MINES, S_vwall },
    { "_knox",     5, GTC_CMAP_KNOX,  S_vwall },
    { "_main",     5, GTC_CMAP_MAIN,  S_vwall },
};

staticfn int
gt_parse_cmap(const char *s)
{
    int i, pchar;
    char base[BUFSZ];
    size_t base_len;

    /* Try suffixed branch forms first.  If the input ends with one
     * of the known suffixes, strip it and look up the base pchar. */
    for (i = 0; i < (int) SIZE(gt_cmap_branch_suffixes); i++) {
        const struct gt_cmap_branch_suffix *bs =
            &gt_cmap_branch_suffixes[i];
        size_t slen = strlen(s);

        if (slen <= (size_t) bs->suffix_len)
            continue;
        if (strcmp(s + slen - bs->suffix_len, bs->suffix) != 0)
            continue;
        base_len = slen - bs->suffix_len;
        if (base_len >= sizeof base)
            return -1;
        memcpy(base, s, base_len);
        base[base_len] = '\0';
        pchar = gt_lookup_pchar(base);
        if (pchar < 0)
            return -1;
        /* The branch ranges only cover wall pchars S_vwall..S_trwall. */
        if (bs->category == GTC_CMAP_MAIN) {
            /* main covers walls only too */
            if (pchar < S_vwall || pchar > S_trwall)
                return -1;
            return glyph_tree_offset[GTC_CMAP_MAIN] + (pchar - S_vwall);
        }
        if (pchar < S_vwall || pchar > S_trwall)
            return -1;
        return glyph_tree_offset[bs->category] + (pchar - S_vwall);
    }

    /* No branch suffix — must be cmap_a, cmap_b, or cmap_c (or one of
     * the special singletons handled elsewhere). */
    pchar = gt_lookup_pchar(s);
    if (pchar < 0)
        return -1;
    /* cmap_a covers S_ndoor..S_brdnladder */
    if (pchar >= S_ndoor && pchar <= S_brdnladder)
        return glyph_tree_offset[GTC_CMAP_A] + (pchar - S_ndoor);
    /* cmap_b covers S_grave..S_grave + 4 */
    if (pchar >= S_grave && pchar < S_grave + 5)
        return glyph_tree_offset[GTC_CMAP_B] + (pchar - S_grave);
    /* cmap_c covers S_digbeam..S_goodpos */
    if (pchar >= S_digbeam && pchar <= S_goodpos)
        return glyph_tree_offset[GTC_CMAP_C] + (pchar - S_digbeam);
    return -1;
}

staticfn int
gt_parse_altar(const char *s)
{
    int i;
    size_t slen;
    size_t alen = strlen("_altar");

    /* "altar_other" is the special-cased "other" alignment. */
    if (strcmp(s, "altar_other") == 0)
        return glyph_tree_offset[GTC_ALTAR] + 4;
    /* Other forms: "<alignment>_altar" where alignment is one of
     * unaligned/chaotic/neutral/lawful. */
    slen = strlen(s);
    if (slen <= alen)
        return -1;
    if (strcmp(s + slen - alen, "_altar") != 0)
        return -1;
    for (i = 0; i < 4; i++) {        /* skip index 4 = "other" */
        size_t plen = strlen(gt_altar_texts[i]);

        if (slen - alen == plen
            && strncmp(s, gt_altar_texts[i], plen) == 0) {
            return glyph_tree_offset[GTC_ALTAR] + i;
        }
    }
    return -1;
}

staticfn int
gt_parse_zap(const char *s)
{
    int t, pchar;
    const char *zap_kw = "_zap_";
    const char *pos;

    /* Expected: "<type>_zap_<dir>" where dir is one of
     * vbeam/hbeam/lslant/rslant. */
    pos = strstr(s, zap_kw);
    if (!pos)
        return -1;
    for (t = 0; t < NUM_ZAP; t++) {
        size_t tlen;
        char type_canon[BUFSZ];

        Snprintf(type_canon, sizeof type_canon, "%s", gt_zap_texts[t]);
        gt_fix_glyphname(type_canon);
        tlen = strlen(type_canon);
        if ((size_t) (pos - s) != tlen)
            continue;
        if (strncmp(s, type_canon, tlen) != 0)
            continue;
        /* Type matches; look up direction. */
        pchar = gt_lookup_pchar(pos + 5);
        if (pchar < S_vbeam || pchar > S_vbeam + 3)
            return -1;
        return glyph_tree_offset[GTC_ZAP] + t * 4 + (pchar - S_vbeam);
    }
    return -1;
}

staticfn int
gt_parse_explosion(const char *s)
{
    int t, p;
    size_t slen, prefix_len, pos_len;
    const char *expl_kw = "_expl_";
    const char *pos;

    pos = strstr(s, expl_kw);
    if (!pos)
        return -1;
    prefix_len = (size_t) (pos - s);
    pos += 6;       /* past "_expl_" */
    pos_len = strlen(pos);
    slen = strlen(s);
    (void) slen;
    for (t = 0; t < 7; t++) {
        char type_canon[BUFSZ];

        Snprintf(type_canon, sizeof type_canon, "%s", gt_expl_type_texts[t]);
        gt_fix_glyphname(type_canon);
        if (strlen(type_canon) != prefix_len)
            continue;
        if (strncmp(s, type_canon, prefix_len) != 0)
            continue;
        for (p = 0; p < (int) MAXEXPCHARS; p++) {
            if (strlen(gt_expl_pos_texts[p]) == pos_len
                && strncmp(pos, gt_expl_pos_texts[p], pos_len) == 0)
                return glyph_tree_offset[GTC_EXPLODE]
                       + t * (int) MAXEXPCHARS + p;
        }
        return -1;
    }
    return -1;
}

staticfn int
gt_parse_swallow(const char *s)
{
    int p, monster;
    size_t slen, mon_len;
    char mon_name[BUFSZ];

    /* "swallow_<monster>_<pos>" where pos is one of swallow_texts
     * (with spaces -> underscores).  Find pos as a suffix. */
    if (strncmp(s, "swallow_", 8) != 0)
        return -1;
    s += 8;
    slen = strlen(s);
    for (p = 0; p < 8; p++) {
        char pos_canon[BUFSZ];
        size_t plen;

        Snprintf(pos_canon, sizeof pos_canon, "%s", gt_swallow_texts[p]);
        gt_fix_glyphname(pos_canon);
        plen = strlen(pos_canon);
        if (slen <= plen + 1)        /* need at least "_" + pos */
            continue;
        if (s[slen - plen - 1] != '_')
            continue;
        if (strcmp(s + slen - plen, pos_canon) != 0)
            continue;
        /* Position matches; monster name is everything before. */
        mon_len = slen - plen - 1;
        if (mon_len >= sizeof mon_name)
            return -1;
        memcpy(mon_name, s, mon_len);
        mon_name[mon_len] = '\0';
        monster = gt_lookup_monster(mon_name);
        if (monster < 0)
            return -1;
        return glyph_tree_offset[GTC_SWALLOW] + monster * 8 + p;
    }
    return -1;
}

staticfn int
gt_parse_object_kind(const char *s, int category)
{
    int obj;

    obj = gt_lookup_object(s);
    if (obj < 0)
        return -1;
    return glyph_tree_offset[category] + obj;
}

staticfn int
gt_parse_object(const char *s)
{
    /* Piletop form first (longer prefix). */
    if (strncmp(s, "piletop_", 8) == 0) {
        int obj = gt_lookup_object(s + 8);
        if (obj >= 0)
            return glyph_tree_offset[GTC_OBJ_PILETOP] + obj;
        return -1;
    }
    return gt_parse_object_kind(s, GTC_OBJ);
}

/* -- top-level forward dispatcher ------------------------------ */

int
glyph_tree_name_to_id(const char *name)
{
    int id;

    if (!name || name[0] != 'G' || name[1] != '_')
        return -1;
    name += 2;

    /* Order matters where prefixes overlap.  Try the more specific
     * (longer-prefix or fixed-pattern) categories first. */

    /* Singletons */
    if ((id = gt_parse_singleton(name, "invisible",       GTC_INVIS))       >= 0) return id;
    if ((id = gt_parse_singleton(name, "stone_substrate", GTC_CMAP_STONE))  >= 0) return id;
    if ((id = gt_parse_singleton(name, "unexplored",      GTC_UNEXPLORED))  >= 0) return id;
    if ((id = gt_parse_singleton(name, "nothing",         GTC_NOTHING))     >= 0) return id;

    /* Warnings */
    if ((id = gt_parse_warning(name)) >= 0) return id;

    /* Statues (4 sub-categories handled inside) */
    if ((id = gt_parse_statue(name)) >= 0) return id;

    /* Bodies */
    if ((id = gt_parse_body_piletop(name)) >= 0) return id;
    if ((id = gt_parse_body(name)) >= 0) return id;

    /* Swallow */
    if ((id = gt_parse_swallow(name)) >= 0) return id;

    /* Living monsters */
    if ((id = gt_parse_monsters_living(name)) >= 0) return id;

    /* Zap */
    if ((id = gt_parse_zap(name)) >= 0) return id;

    /* Explosion */
    if ((id = gt_parse_explosion(name)) >= 0) return id;

    /* Altar */
    if ((id = gt_parse_altar(name)) >= 0) return id;

    /* CMAP branch / a / b / c */
    if ((id = gt_parse_cmap(name)) >= 0) return id;

    /* Objects (regular + piletop).  Last because object names are
     * very freeform and could easily match an unrelated grammar. */
    if ((id = gt_parse_object(name)) >= 0) return id;

    return -1;
}

/* -- reverse: id -> name --------------------------------------- */

/* Find which category an id falls into by binary-searching the
 * offset table. */
staticfn int
gt_id_to_category(int id)
{
    int lo = 0, hi = GTC_NUM_CATEGORIES - 1;

    if (id < 0 || id >= MAX_GLYPH)
        return -1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if (glyph_tree_offset[mid] <= id)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

staticfn void
gt_emit_monsters_living(int category, int local_idx, char *buf, size_t bufsz)
{
    Snprintf(buf, bufsz, "G_%s%s",
             gt_mon_form_prefix_string(category),
             monsdump[local_idx].nm);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_body(int local_idx, char *buf, size_t bufsz, boolean piletop)
{
    Snprintf(buf, bufsz, "G_%sbody_%s",
             piletop ? "piletop_" : "",
             monsdump[local_idx].nm);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_statue(int category, int local_idx, char *buf, size_t bufsz)
{
    const char *prefix;

    switch (category) {
    case GTC_STATUE_MALE:         prefix = "statue_of_male_"; break;
    case GTC_STATUE_FEM:          prefix = "statue_of_female_"; break;
    case GTC_STATUE_MALE_PILETOP: prefix = "piletop_statue_of_male_"; break;
    case GTC_STATUE_FEM_PILETOP:  prefix = "piletop_statue_of_female_"; break;
    default:                      prefix = "statue_of_male_"; break;
    }
    Snprintf(buf, bufsz, "G_%s%s", prefix, monsdump[local_idx].nm);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_object(int local_idx, char *buf, size_t bufsz, boolean piletop)
{
    const char *prefix = gt_obj_class_prefix(local_idx);
    const char *base = gt_obj_base_name(local_idx);

    Snprintf(buf, bufsz, "G_%s%s%s",
             piletop ? "piletop_" : "",
             prefix, base);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_cmap_main(int local_idx, char *buf, size_t bufsz, const char *suffix)
{
    int pchar = S_vwall + local_idx;
    const char *name = loadsyms[pchar + gt_cmap_offset()].name + 2;

    Snprintf(buf, bufsz, "G_%s%s", name, suffix);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_cmap_a(int local_idx, char *buf, size_t bufsz)
{
    int pchar = S_ndoor + local_idx;
    const char *name = loadsyms[pchar + gt_cmap_offset()].name + 2;

    Snprintf(buf, bufsz, "G_%s", name);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_cmap_b(int local_idx, char *buf, size_t bufsz)
{
    int pchar = S_grave + local_idx;
    const char *name = loadsyms[pchar + gt_cmap_offset()].name + 2;

    Snprintf(buf, bufsz, "G_%s", name);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_cmap_c(int local_idx, char *buf, size_t bufsz)
{
    int pchar = S_digbeam + local_idx;
    const char *name = loadsyms[pchar + gt_cmap_offset()].name + 2;

    Snprintf(buf, bufsz, "G_%s", name);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_altar(int local_idx, char *buf, size_t bufsz)
{
    if (local_idx == 4) {
        Snprintf(buf, bufsz, "G_altar_other");
    } else {
        Snprintf(buf, bufsz, "G_%s_altar", gt_altar_texts[local_idx]);
    }
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_zap(int local_idx, char *buf, size_t bufsz)
{
    int t = local_idx >> 2;
    int dir = local_idx & 3;
    int pchar = S_vbeam + dir;
    const char *dir_name = loadsyms[pchar + gt_cmap_offset()].name + 2;

    Snprintf(buf, bufsz, "G_%s zap %s", gt_zap_texts[t], dir_name);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_explosion(int local_idx, char *buf, size_t bufsz)
{
    int t = local_idx / MAXEXPCHARS;
    int p = local_idx % MAXEXPCHARS;

    Snprintf(buf, bufsz, "G_%s expl_%s",
             gt_expl_type_texts[t], gt_expl_pos_texts[p]);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_swallow(int local_idx, char *buf, size_t bufsz)
{
    int monster = local_idx / 8;
    int pos = local_idx % 8;

    Snprintf(buf, bufsz, "G_swallow %s %s",
             monsdump[monster].nm, gt_swallow_texts[pos]);
    gt_fix_glyphname(buf + 2);
}

staticfn void
gt_emit_warning(int local_idx, char *buf, size_t bufsz)
{
    Snprintf(buf, bufsz, "G_warning%d", local_idx);
}

void
glyph_tree_id_to_name(int id, char *buf, size_t bufsz)
{
    int category, local_idx;

    if (!buf || !bufsz)
        return;
    category = gt_id_to_category(id);
    if (category < 0) {
        Strcpy(buf, "G_invalid");
        return;
    }
    local_idx = id - glyph_tree_offset[category];

    switch (category) {
    case GTC_MON_MALE: case GTC_MON_FEM:
    case GTC_PET_MALE: case GTC_PET_FEM:
    case GTC_DETECT_MALE: case GTC_DETECT_FEM:
    case GTC_RIDDEN_MALE: case GTC_RIDDEN_FEM:
        gt_emit_monsters_living(category, local_idx, buf, bufsz);
        return;
    case GTC_INVIS:
        Strcpy(buf, "G_invisible");
        return;
    case GTC_CMAP_STONE:
        Strcpy(buf, "G_stone_substrate");
        return;
    case GTC_UNEXPLORED:
        Strcpy(buf, "G_unexplored");
        return;
    case GTC_NOTHING:
        Strcpy(buf, "G_nothing");
        return;
    case GTC_BODY:
        gt_emit_body(local_idx, buf, bufsz, FALSE);
        return;
    case GTC_BODY_PILETOP:
        gt_emit_body(local_idx, buf, bufsz, TRUE);
        return;
    case GTC_STATUE_MALE: case GTC_STATUE_FEM:
    case GTC_STATUE_MALE_PILETOP: case GTC_STATUE_FEM_PILETOP:
        gt_emit_statue(category, local_idx, buf, bufsz);
        return;
    case GTC_OBJ:
        gt_emit_object(local_idx, buf, bufsz, FALSE);
        return;
    case GTC_OBJ_PILETOP:
        gt_emit_object(local_idx, buf, bufsz, TRUE);
        return;
    case GTC_CMAP_MAIN:  gt_emit_cmap_main(local_idx, buf, bufsz, "_main");     return;
    case GTC_CMAP_MINES: gt_emit_cmap_main(local_idx, buf, bufsz, "_mines");    return;
    case GTC_CMAP_GEH:   gt_emit_cmap_main(local_idx, buf, bufsz, "_gehennom"); return;
    case GTC_CMAP_KNOX:  gt_emit_cmap_main(local_idx, buf, bufsz, "_knox");     return;
    case GTC_CMAP_SOKO:  gt_emit_cmap_main(local_idx, buf, bufsz, "_sokoban");  return;
    case GTC_CMAP_A:
        gt_emit_cmap_a(local_idx, buf, bufsz);
        return;
    case GTC_CMAP_B:
        gt_emit_cmap_b(local_idx, buf, bufsz);
        return;
    case GTC_CMAP_C:
        gt_emit_cmap_c(local_idx, buf, bufsz);
        return;
    case GTC_ALTAR:
        gt_emit_altar(local_idx, buf, bufsz);
        return;
    case GTC_ZAP:
        gt_emit_zap(local_idx, buf, bufsz);
        return;
    case GTC_EXPLODE:
        gt_emit_explosion(local_idx, buf, bufsz);
        return;
    case GTC_SWALLOW:
        gt_emit_swallow(local_idx, buf, bufsz);
        return;
    case GTC_WARNING:
        gt_emit_warning(local_idx, buf, bufsz);
        return;
    default:
        Strcpy(buf, "G_unimplemented_category");
        return;
    }
}

/* -- self-test ------------------------------------------------- */

int
glyph_tree_self_test(void)
{
    int errors = 0;
    int id;
    char name[BUFSZ];

    for (id = 0; id < MAX_GLYPH; id++) {
        glyph_tree_id_to_name(id, name, sizeof name);
        if (glyph_tree_name_to_id(name) != id) {
            raw_printf("glyph_tree round-trip fail: id=%d name=%s "
                       "back=%d",
                       id, name, glyph_tree_name_to_id(name));
            errors++;
        }
    }
    return errors;
}

/*glyph_tree.c*/
