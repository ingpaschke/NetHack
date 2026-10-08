/* time.h wrapper for Helios: Helios's <time.h> declares size_t as a macro
   (#define size_t unsigned int), which breaks token pasting such as
   NetHack's SF_A(size_t).  Take the typedef from <stddef.h> instead. */
#include <stddef.h>
#include_next <time.h>
#undef size_t
/* C89 spells it CLOCKS_PER_SEC; Helios's time.h has the older CLK_TCK */
#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC CLK_TCK
#endif
