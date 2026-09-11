/* t4con.c - console output and exit for the ATW800 port.
 *
 * Linked before libc.a, this displaces libt800's console.o and its exit.
 * SP_PUTS is the one console spelling every host implements; the server
 * appends a newline to each record, so ours is dropped.
 */
#include "t8wire.h"
#include "t8iserver.h"

extern int __t800_host_puts(int stream, const void *buf, int len);
extern void __t800_host_exit(int status);

void __t800_write(const void *buf, int len)
{
    const unsigned char *p = buf;

    if (len <= 0)
        return;
    if (p[len - 1] == '\n')
        len--;                          /* the server adds its own */
    (void) __t800_host_puts(SP_STDOUT, p, len);
}

/* dump a memfs file to the console (post-mortem: paniclog) */
static void dumpfile(const char *name)
{
    extern int __sys_open(const char *, int);
    extern long __sys_read(int, void *, long);
    extern int __sys_close(int);
    static char dbuf[512];
    int fd = __sys_open(name, 0);
    long n;

    if (fd < 0)
        return;
    __t800_write("--- ", 4);
    __t800_write(name, t8_strlen(name));
    __t800_write(" ---\n", 5);
    while ((n = __sys_read(fd, dbuf, sizeof dbuf)) > 0)
        __t800_write(dbuf, (int) n);
    __sys_close(fd);
}

void exit(int status)
{
    {
        extern long __malloc_hiwater;
        extern int sprintf(char *, const char *, ...);
        static char hw[48];
        int l = sprintf(hw, "[heap high-water %ld KB]\n",
                        __malloc_hiwater >> 10);
        __t800_write(hw, l);
    }
    dumpfile("paniclog");
    __t800_host_exit(status);           /* the host stops serving */
    for (;;)
        ;
}

void abort(void)
{
    exit(134);
}
