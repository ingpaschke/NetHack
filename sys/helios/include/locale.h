/* locale.h wrapper for Helios: LC_MONETARY is missing (Lua's os.setlocale
   lists it).  Helios has only the "C" locale, so any value works. */
#include_next <locale.h>
#ifndef LC_MONETARY
#define LC_MONETARY 16
#endif
