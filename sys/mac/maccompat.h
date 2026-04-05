/* maccompat.h - compatibility definitions for Retro68 cross-compilation.
 * With Apple Universal Interfaces + OPAQUE_TOOLBOX_STRUCTS=0 +
 * ACCESSOR_CALLS_ARE_FUNCTIONS=0, accessor functions are not declared
 * by the Apple headers.  This file provides them as macros using direct
 * struct field access, maintaining Carbon source compatibility.
 *
 * With TARGET_OS_MAC=1 and TARGET_CPU_68K=1 in CFLAGS, the Apple headers
 * generate proper inline trap code for Toolbox calls.  No manual trap
 * declarations are needed here.
 */

#ifndef MACCOMPAT_H
#define MACCOMPAT_H

#ifdef CROSS_TO_MAC68K

/* --- Mac OS 8.5+ Window Manager APIs (not in System 7 Interface.o) ---
 * These are provided as macros for System 7 compatibility.
 * On Carbon/Mac OS 8.5+, they are real functions in WindowsLib.
 */
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

/* ConstrainWindowToScreen — Carbon only, no-op on classic */
#ifndef ConstrainWindowToScreen
#define ConstrainWindowToScreen(win, rgn, opts, rect, delta) (noErr)
#endif

/* --- Accessor functions ---
 * Not declared by Apple headers when ACCESSOR_CALLS_ARE_FUNCTIONS=0.
 * On Carbon, these are real functions in CarbonAccessors.o.
 */
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
#undef GetControlOwner
#define GetControlOwner(ctrl)     ((WindowPtr)(**(ctrl)).contrlOwner)
#undef SetMenuID
#define SetMenuID(menu, id)       ((**(menu)).menuID = (id))

/* --- Menu Manager: redirect Mac OS 8.5 APIs to System 7 equivalents --- */
#undef EnableMenuItem
#define EnableMenuItem(menu, item)   EnableItem(menu, item)
#undef DisableMenuItem
#define DisableMenuItem(menu, item)  DisableItem(menu, item)

/* --- HasDepth: in Palette Manager (Palettes.h), available System 7+ --- */
#include <Palettes.h>

/* --- Scrollbar part codes (Appearance Manager constants) --- */
#ifndef kControlUpButtonPart
#define kControlUpButtonPart     20
#define kControlDownButtonPart   21
#define kControlPageUpPart       22
#define kControlPageDownPart     23
#endif

#endif /* CROSS_TO_MAC68K */
#endif /* MACCOMPAT_H */
