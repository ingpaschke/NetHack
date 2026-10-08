/* signal.h wrapper for Helios: C89's sig_atomic_t is missing (Lua's
   lstate.h uses it) */
#include_next <signal.h>
#ifndef HELIOS_SIG_ATOMIC_T
#define HELIOS_SIG_ATOMIC_T
typedef int sig_atomic_t;
#endif
