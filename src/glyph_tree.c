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
 *
 * Skeleton only: parser/emitter implementations follow in later
 * commits.  This file is currently dead code; glyphs.c still uses
 * the legacy parse_id path.
 */

#include "hack.h"
#include "glyph_tree.h"

/* Forward declarations (per-category implementations to come). */

int
glyph_tree_name_to_id(const char *name)
{
    /* TODO: dispatch by category. */
    (void) name;
    return -1;
}

void
glyph_tree_id_to_name(int id, char *buf, size_t bufsz)
{
    /* TODO: dispatch by glyph_tree_offset[] range. */
    (void) id;
    if (buf && bufsz)
        Strcpy(buf, "G_invalid");
}

int
glyph_tree_self_test(void)
{
    /* TODO: walk MAX_GLYPH, round-trip via legacy + tree, count
     * mismatches.  Returns 0 until parser is implemented. */
    return 0;
}

/*glyph_tree.c*/
