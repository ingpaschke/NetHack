/*
 * aes_compat.c - Compatibility shim for E_GEM with modern gemlib
 *
 * Provides __aes__() function and INTIN/INTOUT/ADDRIN arrays that E_GEM
 * expects from the old MiNT GEM library.  Modern gemlib (gem.h) uses
 * a different internal calling convention, so we bridge the gap here.
 *
 * The old __aes__(packed) interface encodes:
 *   bits 31-24: AES opcode
 *   bits 23-16: number of intin values
 *   bits 15-8:  number of intout values
 *   bits 7-0:   number of addrin values
 *   (addrout is always 0 — no calls in E_GEM need it)
 */

/* Include e_gem.h but avoid the _aes macro — we need the real _aes() here */
#include "e_gem.h"
/* The raw AES trap function in modern gemlib.
   At the asm level, the symbol is '_aes'.  With the m68k-atari-mint
   convention (C names get leading underscore), we use __asm__ to
   force the exact symbol name. */
#undef _aes
extern void _real_aes(long) __asm__("_aes");

/* Static arrays used by E_GEM code via INTIN/INTOUT/ADDRIN/GLOBAL macros */
short _egem_intin[128];
short _egem_intout[128];
void *_egem_addrin[16];
void *_egem_addrout[16];

/* The _aes() function in modern gemlib: void _aes(long aespb_ptr) */
extern void _aes(long);

short __aes__(unsigned long packed)
{
    short control[5];
    short *aespb[6];

    control[0] = (short)((packed >> 24) & 0xFF);  /* opcode */
    control[1] = (short)((packed >> 16) & 0xFF);  /* num intin */
    control[2] = (short)((packed >> 8) & 0xFF);   /* num intout */
    control[3] = (short)(packed & 0xFF);             /* num addrin */
    control[4] = 0;                                /* num addrout */

    aespb[0] = control;
    aespb[1] = aes_global;
    aespb[2] = _egem_intin;
    aespb[3] = _egem_intout;
    aespb[4] = (short *)_egem_addrin;
    aespb[5] = (short *)_egem_addrout;

    _real_aes((long)aespb);

    return _egem_intout[0];
}

#ifdef E_GEM_VQ_GDOS_WRAPPER
/* E_GEM expects vq_gdos() returning non-zero if GDOS is available.
   vq_vgdos() returns -2 (0xFFFFFFFE) when NO GDOS is present,
   or a positive value identifying the GDOS type.
   Return 0 for "no GDOS", positive for "GDOS available". */
short vq_gdos(void)
{
    long v = vq_vgdos();
    /* -2 = no GDOS; some systems also return 0 for no GDOS */
    if (v == -2L || v == 0L)
        return 0;
    return (short)v;
}
#endif
