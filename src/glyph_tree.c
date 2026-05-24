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

/* Linear scan over monsdump[] for a name match, NULL or empty
 * `name` returns -1.  This is currently O(NUMMONS) per lookup; once
 * tilemap generates a sorted table we can do binary search.  At
 * config-parsing time the cost is dominated by I/O anyway. */
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
static const char *
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

/* -- forward: name (post-"G_") -> glyph id --------------------- */

/* Try to parse a living-monster name.  Returns glyph ID, or -1 if
 * the input doesn't look like a living-monster name. */
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

/* "invisible" is a singleton with its own category; treat parse
 * separately. */
staticfn int
gt_parse_invisible(const char *s)
{
    if (strcmp(s, "invisible") == 0)
        return glyph_tree_offset[GTC_INVIS];
    return -1;
}

int
glyph_tree_name_to_id(const char *name)
{
    int id;

    if (!name || name[0] != 'G' || name[1] != '_')
        return -1;
    name += 2;

    if ((id = gt_parse_monsters_living(name)) >= 0)
        return id;
    if ((id = gt_parse_invisible(name)) >= 0)
        return id;

    /* Other categories coming in subsequent commits. */
    return -1;
}

/* -- reverse: id -> name --------------------------------------- */

/* Find which category an id falls into by binary-searching the
 * offset table.  Returns the category enum value, or -1 if out of
 * range. */
staticfn int
gt_id_to_category(int id)
{
    int lo = 0, hi = GTC_NUM_CATEGORIES - 1;

    if (id < 0 || id >= MAX_GLYPH)
        return -1;
    /* find largest category whose offset is <= id */
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
gt_emit_invisible(char *buf, size_t bufsz)
{
    Snprintf(buf, bufsz, "G_invisible");
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
    case GTC_MON_MALE:
    case GTC_MON_FEM:
    case GTC_PET_MALE:
    case GTC_PET_FEM:
    case GTC_DETECT_MALE:
    case GTC_DETECT_FEM:
    case GTC_RIDDEN_MALE:
    case GTC_RIDDEN_FEM:
        gt_emit_monsters_living(category, local_idx, buf, bufsz);
        return;
    case GTC_INVIS:
        gt_emit_invisible(buf, bufsz);
        return;
    default:
        Strcpy(buf, "G_unimplemented_category");
        return;
    }
}

/* -- self-test ------------------------------------------------- */

/* For now, only round-trip the monster categories and the invisible
 * singleton.  Other categories will be added as their parsers land. */
int
glyph_tree_self_test(void)
{
    int errors = 0;
    int id;
    char name[BUFSZ];

    for (id = 0; id < MAX_GLYPH; id++) {
        int cat = gt_id_to_category(id);

        /* Only check categories that have parsers yet. */
        if (cat != GTC_MON_MALE && cat != GTC_MON_FEM
            && cat != GTC_PET_MALE && cat != GTC_PET_FEM
            && cat != GTC_DETECT_MALE && cat != GTC_DETECT_FEM
            && cat != GTC_RIDDEN_MALE && cat != GTC_RIDDEN_FEM
            && cat != GTC_INVIS)
            continue;

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
