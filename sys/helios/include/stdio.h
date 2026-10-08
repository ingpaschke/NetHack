/* stdio.h wrapper for Helios: Helios's <stdio.h> declares size_t as a macro
   (#define size_t unsigned int), which breaks token pasting such as
   NetHack's SF_A(size_t).  Take the typedef from <stddef.h> instead. */
#include <stddef.h>
#include_next <stdio.h>
#undef size_t
