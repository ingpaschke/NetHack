/* NetHack 5.0    glyph_tree.h    $NHDT-Date$ */
/* Copyright (c) 2026.  NetHack may be freely redistributed.  See license. */

/* Tree-encoded glyph name parser/emitter.
 *
 * Replaces the previous fill_glyphid_cache + parse_id machinery.
 * Forward lookup (name -> ID) and reverse lookup (ID -> name) operate
 * directly against the category layout in display.h's GLYPH_*_OFF
 * enums; the cache machinery is no longer needed.
 *
 * See src/glyph_tree.c for the parser/emitter; see
 * src/glyph_tree_data.c for the tables that drive them.
 */

#ifndef GLYPH_TREE_H
#define GLYPH_TREE_H

/* One enum value per contiguous glyph range in display.h's
 * GLYPH_*_OFF layout.  Each range is a "category"; within each
 * category the ID is (offset + local_index). */
enum glyph_tree_category {
    GTC_MON_MALE = 0,           /* GLYPH_MON_MALE_OFF: NUMMONS */
    GTC_MON_FEM,                /* GLYPH_MON_FEM_OFF:  NUMMONS */
    GTC_PET_MALE,               /* GLYPH_PET_MALE_OFF: NUMMONS */
    GTC_PET_FEM,                /* GLYPH_PET_FEM_OFF:  NUMMONS */
    GTC_INVIS,                  /* GLYPH_INVIS_OFF:    1 */
    GTC_DETECT_MALE,            /* GLYPH_DETECT_MALE_OFF: NUMMONS */
    GTC_DETECT_FEM,             /* GLYPH_DETECT_FEM_OFF:  NUMMONS */
    GTC_BODY,                   /* GLYPH_BODY_OFF:     NUMMONS */
    GTC_RIDDEN_MALE,            /* GLYPH_RIDDEN_MALE_OFF: NUMMONS */
    GTC_RIDDEN_FEM,             /* GLYPH_RIDDEN_FEM_OFF:  NUMMONS */
    GTC_OBJ,                    /* GLYPH_OBJ_OFF:      NUM_OBJECTS */
    GTC_CMAP_STONE,             /* GLYPH_CMAP_STONE_OFF: 1 */
    GTC_CMAP_MAIN,              /* GLYPH_CMAP_MAIN_OFF:  cmap_wallrange */
    GTC_CMAP_MINES,             /* GLYPH_CMAP_MINES_OFF: cmap_wallrange */
    GTC_CMAP_GEH,               /* GLYPH_CMAP_GEH_OFF:   cmap_wallrange */
    GTC_CMAP_KNOX,              /* GLYPH_CMAP_KNOX_OFF:  cmap_wallrange */
    GTC_CMAP_SOKO,              /* GLYPH_CMAP_SOKO_OFF:  cmap_wallrange */
    GTC_CMAP_A,                 /* GLYPH_CMAP_A_OFF:     cmap_a_range */
    GTC_ALTAR,                  /* GLYPH_ALTAR_OFF:      5 alignments */
    GTC_CMAP_B,                 /* GLYPH_CMAP_B_OFF:     cmap_b_range */
    GTC_ZAP,                    /* GLYPH_ZAP_OFF:        NUM_ZAP * 4 */
    GTC_CMAP_C,                 /* GLYPH_CMAP_C_OFF:     cmap_c_range */
    GTC_SWALLOW,                /* GLYPH_SWALLOW_OFF:    NUMMONS * 8 */
    GTC_EXPLODE,                /* GLYPH_EXPLODE_OFF:    7 * MAXEXPCHARS */
    GTC_WARNING,                /* GLYPH_WARNING_OFF:    WARNCOUNT */
    GTC_STATUE_MALE,            /* GLYPH_STATUE_MALE_OFF: NUMMONS */
    GTC_STATUE_FEM,             /* GLYPH_STATUE_FEM_OFF:  NUMMONS */
    GTC_OBJ_PILETOP,            /* GLYPH_OBJ_PILETOP_OFF: NUM_OBJECTS */
    GTC_BODY_PILETOP,           /* GLYPH_BODY_PILETOP_OFF: NUMMONS */
    GTC_STATUE_MALE_PILETOP,    /* GLYPH_STATUE_MALE_PILETOP_OFF: NUMMONS */
    GTC_STATUE_FEM_PILETOP,     /* GLYPH_STATUE_FEM_PILETOP_OFF: NUMMONS */
    GTC_UNEXPLORED,             /* GLYPH_UNEXPLORED_OFF: 1 */
    GTC_NOTHING,                /* GLYPH_NOTHING_OFF:    1 */
    GTC_NUM_CATEGORIES
};

/* Each category's starting glyph ID. */
extern const int glyph_tree_offset[GTC_NUM_CATEGORIES];

/* Public entry points (replace parse_id-based lookups). */

/* Forward: parse a G_xxx name, return the glyph ID or -1 on failure. */
extern int glyph_tree_name_to_id(const char *name);

/* Reverse: write the canonical G_xxx name for the given ID into buf. */
extern void glyph_tree_id_to_name(int id, char *buf, size_t bufsz);

/* Self-test: walk every glyph and confirm round-trip via the tree
 * parser matches what the legacy parse_id machinery produces.
 * Returns 0 on success, count of mismatches on failure. */
extern int glyph_tree_self_test(void);

#endif /* GLYPH_TREE_H */
