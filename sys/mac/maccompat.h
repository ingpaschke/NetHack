/* maccompat.h - compatibility definitions for Retro68 cross-compilation.
 * With Apple Universal Interfaces + OPAQUE_TOOLBOX_STRUCTS=0,
 * most accessor macros are provided by the real headers.
 */

#ifndef MACCOMPAT_H
#define MACCOMPAT_H

#ifdef CROSS_TO_MAC68K

/* Timing — Retro68 glue may not link these */
#ifndef GetDblTime
#define GetDblTime()  (*(unsigned long *) 0x02F0)
#endif
#ifndef GetCaretTime
#define GetCaretTime() (*(unsigned long *) 0x02F4)
#endif

/* GetGrayRgn */
#ifndef GetGrayRgn
#define GetGrayRgn() LMGetGrayRgn()
#endif

/* Functions declared in Apple headers but not in Retro68 glue */
#undef GetWindowBounds
#define GetWindowBounds(win, rgn, rect) \
    (*(rect) = ((GrafPtr)(win))->portRect)
#undef InvalWindowRect
#define InvalWindowRect(win, r)   do { \
    GrafPtr _igp; GetPort(&_igp); SetPort((GrafPtr)(win)); \
    InvalRect(r); SetPort(_igp); } while(0)
#undef InvalWindowRgn
#define InvalWindowRgn(win, rgn)  do { \
    GrafPtr _igp; GetPort(&_igp); SetPort((GrafPtr)(win)); \
    InvalRgn(rgn); SetPort(_igp); } while(0)
#undef GetPortBitMapForCopyBits
#define GetPortBitMapForCopyBits(port) (&((GrafPtr)(port))->portBits)
#undef GetQDGlobalsArrow
#define GetQDGlobalsArrow(curs)   (*(curs) = qd.arrow, (curs))
#undef GetQDGlobalsScreenBits
#define GetQDGlobalsScreenBits(bm) (&qd.screenBits)
#undef GetRegionBounds
#define GetRegionBounds(rgn, r)   (*(r) = (*(rgn))->rgnBBox, (r))
#undef GetControlBounds
#define GetControlBounds(ctrl, r) (*(r) = (**(ctrl)).contrlRect, (r))
/* IsControlVisible is Carbon-only. Don't redefine here because Controls.h
   may not be included yet. Instead, define in each .c file that needs it,
   or use the struct field directly. */
#undef GetControlOwner
#define GetControlOwner(ctrl)     ((WindowPtr)(**(ctrl)).contrlOwner)
#undef SetMenuID
#define SetMenuID(menu, id)       ((**(menu)).menuID = (id))
#undef EnableMenuItem
#define EnableMenuItem(menu, item)   EnableItem(menu, item)
#undef DisableMenuItem
#define DisableMenuItem(menu, item)  DisableItem(menu, item)
/* LMGetGrayRgn declared in Apple's LowMem.h but not in Retro68 glue */
static inline RgnHandle LMGetGrayRgn(void) { return *(RgnHandle *)0x09EE; }

/* Map to uppercase Pascal names where available in libInterface.a */
#define HOpenResFile HOPENRESFILE
#define DIBadMount DIBADMOUNT

/* These Resource Manager traps are not in libInterface. Provide inline. */
#pragma parameter CloseResFile(__D0)
pascal void CloseResFile(short refNum) = {0xA99A};

#pragma parameter __A0 Get1Resource(__D0, __D1)
pascal Handle Get1Resource(ResType type, short id) = {0xA81F};

#pragma parameter DetachResource(__A0)
pascal void DetachResource(Handle h) = {0xA992};

#pragma parameter ReleaseResource(__A0)
pascal void ReleaseResource(Handle h) = {0xA9A3};

/* HasDepth is in the Palette Manager (Palettes.h), available on System 7+ */
#include <Palettes.h>

/* Resource Manager traps missing from Retro68 glue */
#pragma parameter __D0 CurResFile
pascal short CurResFile(void) = {0xA994};
#pragma parameter __D0 OpenResFile(__A0)
pascal short OpenResFile(ConstStr255Param fileName) = {0xA997};

/* ConstrainWindowToScreen — Carbon only, no-op on classic */
#ifndef ConstrainWindowToScreen
#define ConstrainWindowToScreen(win, rgn, opts, rect, delta) (noErr)
#endif

/* Scrollbar part codes (Appearance Manager) */
#ifndef kControlUpButtonPart
#define kControlUpButtonPart     20
#define kControlDownButtonPart   21
#define kControlPageUpPart       22
#define kControlPageDownPart     23
#endif

#endif /* CROSS_TO_MAC68K */
#endif /* MACCOMPAT_H */
