/* sys_t800.c - transputer startup.
 *
 * pcmain.c is compiled with -Dmain=nh_main.  main() here runs the host
 * handshake, registers the windowport callback and calls nh_main.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "hack.h"
#include "t800host.h"

/* no job control, no interrupt masking on the boot link */
int dosuspend(void) { return 0; }
void intron(void) {}
void introff(void) {}

/* fatal startup error (files.c, unixmain.c expect it) */
void error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    exit(1);
}

typedef void (*shim_callback_t)(const char *name, void *ret_ptr,
                                const char *fmt, ...);
void shim_graphics_set_callback(shim_callback_t cb);
extern void t800_rpc_cb(const char *, void *, const char *, ...);
extern int nh_main(int argc, char **argv);

/* At exit, on the console (the host's log and message window): the heap's
   high-water mark, and the paniclog if there is one.  libc's exit() runs
   this before it flushes stdout. */
static void exit_report(void)
{
    extern long __malloc_hiwater;
    FILE *f;
    char buf[256];

    printf("[heap high-water %ld KB]\n", __malloc_hiwater >> 10);
    if ((f = fopen("paniclog", "r")) != 0) {
        printf("--- paniclog ---\n");
        while (fgets(buf, sizeof buf, f))
            fputs(buf, stdout);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    static char *av[] = { (char *)"nethack", 0 };  /* normal player selection */
    (void)argc; (void)argv;
    printf("t800 nethack: boot\n");
    /* host capability handshake: shared-memory polling, packet size */
    __t800_host_probe();
    printf("t800 nethack: %d-byte file chunks\n", __t800_host_chunk());
    atexit(exit_report);
    shim_graphics_set_callback(t800_rpc_cb);
    printf("t800 nethack: entering nh_main\n");
    return nh_main(1, av);
}
