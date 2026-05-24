/* NetHack 5.0    glyph_tree_data.c    $NHDT-Date$ */
/* Copyright (c) 2026.  NetHack may be freely redistributed.  See license. */

/* Data tables for the tree-encoded glyph parser/emitter.
 *
 * For now this file is hand-written; a future commit will have
 * util/tilemap regenerate it from the source enum tables.  The hand
 * version keeps category_offset[] symbolically tied to the existing
 * GLYPH_*_OFF macros in display.h so they stay in sync without
 * explicit numeric values.
 */

#include "hack.h"
#include "glyph_tree.h"

const int glyph_tree_offset[GTC_NUM_CATEGORIES] = {
    [GTC_MON_MALE]             = GLYPH_MON_MALE_OFF,
    [GTC_MON_FEM]              = GLYPH_MON_FEM_OFF,
    [GTC_PET_MALE]             = GLYPH_PET_MALE_OFF,
    [GTC_PET_FEM]              = GLYPH_PET_FEM_OFF,
    [GTC_INVIS]                = GLYPH_INVIS_OFF,
    [GTC_DETECT_MALE]          = GLYPH_DETECT_MALE_OFF,
    [GTC_DETECT_FEM]           = GLYPH_DETECT_FEM_OFF,
    [GTC_BODY]                 = GLYPH_BODY_OFF,
    [GTC_RIDDEN_MALE]          = GLYPH_RIDDEN_MALE_OFF,
    [GTC_RIDDEN_FEM]           = GLYPH_RIDDEN_FEM_OFF,
    [GTC_OBJ]                  = GLYPH_OBJ_OFF,
    [GTC_CMAP_STONE]           = GLYPH_CMAP_STONE_OFF,
    [GTC_CMAP_MAIN]            = GLYPH_CMAP_MAIN_OFF,
    [GTC_CMAP_MINES]           = GLYPH_CMAP_MINES_OFF,
    [GTC_CMAP_GEH]             = GLYPH_CMAP_GEH_OFF,
    [GTC_CMAP_KNOX]            = GLYPH_CMAP_KNOX_OFF,
    [GTC_CMAP_SOKO]            = GLYPH_CMAP_SOKO_OFF,
    [GTC_CMAP_A]               = GLYPH_CMAP_A_OFF,
    [GTC_ALTAR]                = GLYPH_ALTAR_OFF,
    [GTC_CMAP_B]               = GLYPH_CMAP_B_OFF,
    [GTC_ZAP]                  = GLYPH_ZAP_OFF,
    [GTC_CMAP_C]               = GLYPH_CMAP_C_OFF,
    [GTC_SWALLOW]              = GLYPH_SWALLOW_OFF,
    [GTC_EXPLODE]              = GLYPH_EXPLODE_OFF,
    [GTC_WARNING]              = GLYPH_WARNING_OFF,
    [GTC_STATUE_MALE]          = GLYPH_STATUE_MALE_OFF,
    [GTC_STATUE_FEM]           = GLYPH_STATUE_FEM_OFF,
    [GTC_OBJ_PILETOP]          = GLYPH_OBJ_PILETOP_OFF,
    [GTC_BODY_PILETOP]         = GLYPH_BODY_PILETOP_OFF,
    [GTC_STATUE_MALE_PILETOP]  = GLYPH_STATUE_MALE_PILETOP_OFF,
    [GTC_STATUE_FEM_PILETOP]   = GLYPH_STATUE_FEM_PILETOP_OFF,
    [GTC_UNEXPLORED]           = GLYPH_UNEXPLORED_OFF,
    [GTC_NOTHING]              = GLYPH_NOTHING_OFF,
};

/*glyph_tree_data.c*/
