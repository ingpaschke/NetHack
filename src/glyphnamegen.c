/* NetHack 5.0  glyphnamegen.c */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Canonical glyph-name construction, factored out of glyphs.c so the
 * host-side build tool util/mkglyphhash can call into it without dragging
 * the rest of glyphs.c (and its customization/runtime deps) into its
 * link.  The runtime in glyphs.c reaches these via extern decls in
 * include/extern.h.
 */

#include "hack.h"

extern const struct symparse loadsyms[];
extern struct enum_dump monsdump[];

/* Prototypes are also in extern.h, but the host tool that links this file
   does not include extern.h.  Provide local prototypes so -Wmissing-prototypes
   is satisfied in both contexts. */
extern char *fix_glyphname(char *str);
extern uint32 glyph_name_hash(const char *id);
extern int compose_glyph_name(int glyph, char *buf, size_t bufsz);

char *
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

uint32
glyph_name_hash(const char *id)
{
    uint32 hash = 0;
    size_t i;

    for (i = 0; id[i] != '\0'; ++i) {
        char ch = id[i];
        if ('A' <= ch && ch <= 'Z') {
            ch += 'a' - 'A';
        }
        hash = (hash << 1) | (hash >> 31);
        hash ^= ch;
    }
    return hash;
}

int
compose_glyph_name(int glyph, char *buf, size_t bufsz)
{
    int i, j, mnum, cmap_offset = 0;
    boolean skip_base = FALSE;
    const char *buf2, *buf3, *buf4;
    char tmpbuf[4][QBUFSZ];

    if (bufsz < 2)
        return 0;
    buf[0] = '\0';
    tmpbuf[0][0] = tmpbuf[1][0] = tmpbuf[2][0] = tmpbuf[3][0] = '\0';

    /* loadsyms[] is indexed by an offset that depends on how many SYM_CONTROL
       entries precede SYM_PCHAR; recompute on each call so the helper can
       be used standalone by util/mkglyphhash without cached state. */
    i = 0;
    while (loadsyms[i].range) {
        if (!cmap_offset && loadsyms[i].range == SYM_PCHAR)
            cmap_offset = i;
        i++;
    }

    if (glyph_is_monster(glyph)) {
        buf2 = "";
        buf3 = monsdump[glyph_to_mon(glyph)].nm;
        if (glyph_is_normal_male_monster(glyph)) {
            buf2 = "male_";
        } else if (glyph_is_normal_female_monster(glyph)) {
            buf2 = "female_";
        } else if (glyph_is_ridden_male_monster(glyph)) {
            buf2 = "ridden_male_";
        } else if (glyph_is_ridden_female_monster(glyph)) {
            buf2 = "ridden_female_";
        } else if (glyph_is_detected_male_monster(glyph)) {
            buf2 = "detected_male_";
        } else if (glyph_is_detected_female_monster(glyph)) {
            buf2 = "detected_female_";
        } else if (glyph_is_male_pet(glyph)) {
            buf2 = "pet_male_";
        } else if (glyph_is_female_pet(glyph)) {
            buf2 = "pet_female_";
        }
        Strcpy(buf, "G_");
        Strcat(buf, buf2);
        Strcat(buf, buf3);
    } else if (glyph_is_body(glyph)) {
        buf2 = glyph_is_body_piletop(glyph) ? "piletop_body_" : "body_";
        buf3 = monsdump[glyph_to_body_corpsenm(glyph)].nm;
        Strcpy(buf, "G_");
        Strcat(buf, buf2);
        Strcat(buf, buf3);
    } else if (glyph_is_statue(glyph)) {
        buf2 = glyph_is_fem_statue_piletop(glyph)
               ? "piletop_statue_of_female_"
               : glyph_is_fem_statue(glyph)
                 ? "statue_of_female_"
                 : glyph_is_male_statue_piletop(glyph)
                   ? "piletop_statue_of_male_"
                   : glyph_is_male_statue(glyph)
                     ? "statue_of_male_"
                     : "";
        buf3 = monsdump[glyph_to_statue_corpsenm(glyph)].nm;
        Strcpy(buf, "G_");
        Strcat(buf, buf2);
        Strcat(buf, buf3);
    } else if (glyph_is_object(glyph)) {
        i = glyph_to_obj(glyph);
        if (((i > SCR_STINKING_CLOUD) && (i < SCR_MAIL))
            || ((i > WAN_LIGHTNING) && (i < GOLD_PIECE))) {
            return 0;
        }
        if ((i >= WAN_LIGHT) && (i <= WAN_LIGHTNING))
            buf2 = "wand of ";
        else if ((i >= SPE_DIG) && (i < SPE_BLANK_PAPER))
            buf2 = "spellbook of ";
        else if ((i >= SCR_ENCHANT_ARMOR) && (i <= SCR_STINKING_CLOUD))
            buf2 = "scroll of ";
        else if ((i >= POT_GAIN_ABILITY) && (i <= POT_WATER))
            buf2 = (i == POT_WATER) ? "flask of n" : "potion of ";
        else if ((i >= RIN_ADORNMENT) && (i <= RIN_PROTECTION_FROM_SHAPE_CHAN))
            buf2 = "ring of ";
        else if (i == LAND_MINE)
            buf2 = "unset ";
        else
            buf2 = "";
        buf3 = (i == SCR_BLANK_PAPER) ? "blank scroll"
               : (i == SPE_BLANK_PAPER) ? "blank spellbook"
                 : (i == SLIME_MOLD) ? "slime mold"
                   : obj_descr[i].oc_name
                     ? obj_descr[i].oc_name
                     : obj_descr[i].oc_descr;
        Strcpy(buf, "G_");
        if (glyph_is_normal_piletop_obj(glyph))
            Strcat(buf, "piletop_");
        Strcat(buf, buf2);
        Strcat(buf, buf3);
    } else if (glyph_is_cmap(glyph) || glyph_is_cmap_zap(glyph)
               || glyph_is_swallow(glyph) || glyph_is_explosion(glyph)) {
        int cmap = -1;

        buf2 = "";
        buf3 = "";
        buf4 = "";
        if (glyph == GLYPH_CMAP_OFF) {
            cmap = S_stone;
            buf3 = "stone substrate";
            skip_base = TRUE;
        } else if (glyph_is_cmap_gehennom(glyph)) {
            cmap = (glyph - GLYPH_CMAP_GEH_OFF) + S_vwall;
            buf4 = "_gehennom";
        } else if (glyph_is_cmap_knox(glyph)) {
            cmap = (glyph - GLYPH_CMAP_KNOX_OFF) + S_vwall;
            buf4 = "_knox";
        } else if (glyph_is_cmap_main(glyph)) {
            cmap = (glyph - GLYPH_CMAP_MAIN_OFF) + S_vwall;
            buf4 = "_main";
        } else if (glyph_is_cmap_mines(glyph)) {
            cmap = (glyph - GLYPH_CMAP_MINES_OFF) + S_vwall;
            buf4 = "_mines";
        } else if (glyph_is_cmap_sokoban(glyph)) {
            cmap = (glyph - GLYPH_CMAP_SOKO_OFF) + S_vwall;
            buf4 = "_sokoban";
        } else if (glyph_is_cmap_a(glyph)) {
            cmap = (glyph - GLYPH_CMAP_A_OFF) + S_ndoor;
        } else if (glyph_is_cmap_altar(glyph)) {
            static const char *const altar_text[] = {
                "unaligned", "chaotic", "neutral",
                "lawful",    "other",
            };

            j = (glyph - GLYPH_ALTAR_OFF);
            cmap = S_altar;
            if (j != altar_other) {
                Snprintf(tmpbuf[2], sizeof tmpbuf[2], "%s_", altar_text[j]);
                buf2 = tmpbuf[2];
            } else {
                buf3 = "altar other";
                skip_base = TRUE;
            }
        } else if (glyph_is_cmap_b(glyph)) {
            cmap = (glyph - GLYPH_CMAP_B_OFF) + S_grave;
        } else if (glyph_is_cmap_zap(glyph)) {
            static const char *const zap_texts[] = {
                "missile", "fire",      "frost",      "sleep",
                "death",   "lightning", "poison gas", "acid"
            };

            j = (glyph - GLYPH_ZAP_OFF);
            cmap = (j % 4) + S_vbeam;
            Snprintf(tmpbuf[2], sizeof tmpbuf[2], "%s",
                     loadsyms[cmap + cmap_offset].name + 2);
            Snprintf(tmpbuf[3], sizeof tmpbuf[3], "%s zap %s",
                     zap_texts[j / 4], fix_glyphname(tmpbuf[2]));
            buf3 = tmpbuf[3];
            buf2 = "";
            skip_base = TRUE;
        } else if (glyph_is_cmap_c(glyph)) {
            cmap = (glyph - GLYPH_CMAP_C_OFF) + S_digbeam;
        } else if (glyph_is_swallow(glyph)) {
            static const char *const swallow_texts[] = {
                "top left",      "top center",   "top right",
                "middle left",   "middle right", "bottom left",
                "bottom center", "bottom right",
            };

            j = glyph - GLYPH_SWALLOW_OFF;
            cmap = glyph_to_swallow(glyph);
            mnum = j / ((S_sw_br - S_sw_tl) + 1);
            Strcpy(tmpbuf[3], "swallow ");
            Strcat(tmpbuf[3], monsdump[mnum].nm);
            Strcat(tmpbuf[3], " ");
            Strcat(tmpbuf[3], swallow_texts[cmap]);
            buf3 = tmpbuf[3];
            skip_base = TRUE;
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
            cmap = glyph_to_explosion(glyph) + S_expl_tl;
            i = cmap - S_expl_tl;
            Snprintf(tmpbuf[2], sizeof tmpbuf[2], "%s ", expl_type_texts[expl]);
            buf2 = tmpbuf[2];
            Snprintf(tmpbuf[3], sizeof tmpbuf[3], "%s%s", "expl_", expl_texts[i]);
            buf3 = tmpbuf[3];
            skip_base = TRUE;
        }
        if (!skip_base) {
            if (cmap >= 0 && cmap < MAXPCHARS)
                buf3 = loadsyms[cmap + cmap_offset].name + 2;
        }
        Strcpy(buf, "G_");
        Strcat(buf, buf2);
        Strcat(buf, buf3);
        Strcat(buf, buf4);
    } else if (glyph_is_invisible(glyph)) {
        Strcpy(buf, "G_invisible");
    } else if (glyph_is_nothing(glyph)) {
        Strcpy(buf, "G_nothing");
    } else if (glyph_is_unexplored(glyph)) {
        Strcpy(buf, "G_unexplored");
    } else if (glyph_is_warning(glyph)) {
        j = glyph - GLYPH_WARNING_OFF;
        Snprintf(buf, bufsz, "G_%s%d", "warning", j);
    }

    if (buf[0] == '\0')
        return 0;

    if (memchr(buf, '\0', bufsz) == NULL) {
        /* caller passed too-small buffer; refuse rather than scribble */
        buf[bufsz - 1] = '\0';
        return 0;
    }

    fix_glyphname(buf + 2);
    nhUse(mnum);
    return 1;
}

/*glyphnamegen.c*/
