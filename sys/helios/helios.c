/* NetHack 5.0	helios.c */
/* Copyright (c) 2026 by Ingo Paschke */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * What NetHack needs from a C library and Helios's Clib/Posix lack.
 *
 * Clib is ANSI C89: no snprintf/vsnprintf.  The ones here format into
 * the caller's buffer with the size limit honoured; integer, string,
 * character and pointer conversions (with the C99 length modifiers hh,
 * h, l, ll, j, z, t) are done here, a floating conversion is handed to
 * Clib's sprintf one conversion at a time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

struct sbuf {
    char *p;      /* output buffer */
    size_t size;  /* its size, 0 for none */
    size_t n;     /* characters produced so far (may exceed size) */
};

static void
put(struct sbuf *b, char c)
{
    if (b->size && b->n < b->size - 1)
        b->p[b->n] = c;
    b->n++;
}

static void
putn(struct sbuf *b, const char *s, size_t len)
{
    while (len--)
        put(b, *s++);
}

static void
pad(struct sbuf *b, char c, int count)
{
    while (count-- > 0)
        put(b, c);
}

#define F_MINUS 1
#define F_PLUS 2
#define F_SPACE 4
#define F_HASH 8
#define F_ZERO 16

int
vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct sbuf b;

    b.p = buf;
    b.size = size;
    b.n = 0;

    while (*fmt) {
        int flags = 0, width = -1, prec = -1, lmod = 0;
        char conv;

        if (*fmt != '%') {
            put(&b, *fmt++);
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-')
                flags |= F_MINUS;
            else if (*fmt == '+')
                flags |= F_PLUS;
            else if (*fmt == ' ')
                flags |= F_SPACE;
            else if (*fmt == '#')
                flags |= F_HASH;
            else if (*fmt == '0')
                flags |= F_ZERO;
            else
                break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= F_MINUS;
                width = -width;
            }
            fmt++;
        } else if (*fmt >= '0' && *fmt <= '9') {
            width = 0;
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        /* length: 1 = l, 2 = ll (also j), -1 = h, -2 = hh, 3 = L */
        for (;; fmt++) {
            if (*fmt == 'l')
                lmod = (lmod == 1) ? 2 : 1;
            else if (*fmt == 'h')
                lmod = (lmod == -1) ? -2 : -1;
            else if (*fmt == 'j' || *fmt == 'q')
                lmod = 2;
            else if (*fmt == 'z' || *fmt == 't')
                lmod = 0; /* size_t and ptrdiff_t are int */
            else if (*fmt == 'L')
                lmod = 3;
            else
                break;
        }
        conv = *fmt;
        if (!conv)
            break;
        fmt++;

        switch (conv) {
        case '%':
            put(&b, '%');
            break;
        case 'c': {
            char c = (char) va_arg(ap, int);

            if (!(flags & F_MINUS))
                pad(&b, ' ', width - 1);
            put(&b, c);
            if (flags & F_MINUS)
                pad(&b, ' ', width - 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t len;

            if (!s)
                s = "(null)";
            if (prec >= 0) {
                const char *e = memchr(s, '\0', (size_t) prec);

                len = e ? (size_t) (e - s) : (size_t) prec;
            } else {
                len = strlen(s);
            }
            if (!(flags & F_MINUS))
                pad(&b, ' ', width - (int) len);
            putn(&b, s, len);
            if (flags & F_MINUS)
                pad(&b, ' ', width - (int) len);
            break;
        }
        case 'n':
            if (lmod == 2)
                *va_arg(ap, long long *) = (long long) b.n;
            else if (lmod == 1)
                *va_arg(ap, long *) = (long) b.n;
            else if (lmod == -1)
                *va_arg(ap, short *) = (short) b.n;
            else if (lmod == -2)
                *va_arg(ap, signed char *) = (signed char) b.n;
            else
                *va_arg(ap, int *) = (int) b.n;
            break;
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
        case 'p': {
            unsigned long long v;
            int neg = 0, base = 10, len, zeros, plen = 0;
            char digits[32], prefix[3];
            const char *dig = (conv == 'X') ? "0123456789ABCDEF"
                                            : "0123456789abcdef";

            if (conv == 'p') {
                v = (unsigned long) va_arg(ap, void *);
                base = 16;
                flags |= F_HASH;
            } else if (conv == 'd' || conv == 'i') {
                long long sv;

                if (lmod == 2)
                    sv = va_arg(ap, long long);
                else if (lmod == 1)
                    sv = va_arg(ap, long);
                else
                    sv = va_arg(ap, int);
                if (lmod == -1)
                    sv = (short) sv;
                else if (lmod == -2)
                    sv = (signed char) sv;
                if (sv < 0) {
                    neg = 1;
                    v = (unsigned long long) -sv;
                } else {
                    v = (unsigned long long) sv;
                }
            } else {
                if (lmod == 2)
                    v = va_arg(ap, unsigned long long);
                else if (lmod == 1)
                    v = va_arg(ap, unsigned long);
                else
                    v = va_arg(ap, unsigned int);
                if (lmod == -1)
                    v = (unsigned short) v;
                else if (lmod == -2)
                    v = (unsigned char) v;
                if (conv == 'o')
                    base = 8;
                else if (conv != 'u')
                    base = 16;
            }
            len = 0;
            if (base == 10 && v <= 0xffffffffULL) {
                /* 32-bit division: no i64 helper calls in the common case */
                unsigned long w = (unsigned long) v;

                do {
                    digits[len++] = dig[w % 10];
                    w /= 10;
                } while (w);
            } else {
                do {
                    digits[len++] = dig[(unsigned) (v % (unsigned) base)];
                    v /= (unsigned) base;
                } while (v);
            }
            if (prec == 0 && len == 1 && digits[0] == '0' && conv != 'p')
                len = 0;
            if (neg)
                prefix[plen++] = '-';
            else if (flags & F_PLUS && (conv == 'd' || conv == 'i'))
                prefix[plen++] = '+';
            else if (flags & F_SPACE && (conv == 'd' || conv == 'i'))
                prefix[plen++] = ' ';
            if (flags & F_HASH) {
                if (base == 16 && (len > 0 && !(len == 1 && digits[0] == '0')
                                   || conv == 'p')) {
                    prefix[plen++] = '0';
                    prefix[plen++] = (conv == 'X') ? 'X' : 'x';
                } else if (base == 8 && (prec <= len)) {
                    if (!(len && digits[len - 1] == '0'))
                        prec = len + 1;
                }
            }
            zeros = (prec > len) ? prec - len : 0;
            if (prec < 0 && (flags & F_ZERO) && !(flags & F_MINUS)
                && width > plen + len)
                zeros = width - plen - len;
            if (!(flags & F_MINUS))
                pad(&b, ' ', width - plen - zeros - len);
            putn(&b, prefix, (size_t) plen);
            pad(&b, '0', zeros);
            while (len > 0)
                put(&b, digits[--len]);
            if (flags & F_MINUS)
                pad(&b, ' ', width - plen - zeros);
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        case 'a': case 'A': {
            /* one conversion through Clib's sprintf */
            char spec[32], out[400], *q = spec;
            double d = (lmod == 3) ? (double) va_arg(ap, long double)
                                   : va_arg(ap, double);

            *q++ = '%';
            if (flags & F_MINUS) *q++ = '-';
            if (flags & F_PLUS) *q++ = '+';
            if (flags & F_SPACE) *q++ = ' ';
            if (flags & F_HASH) *q++ = '#';
            if (flags & F_ZERO) *q++ = '0';
            if (width > 120)
                width = 120;
            if (prec > 60)
                prec = 60;
            if (width >= 0)
                q += sprintf(q, "%d", width);
            if (prec >= 0)
                q += sprintf(q, ".%d", prec);
            *q++ = (conv == 'F') ? 'f' : (conv == 'a' || conv == 'A')
                                             ? 'e' : conv;
            *q = '\0';
            if (d > 1e60 || d < -1e60) {
                /* %f of a huge value would not fit out[] */
                spec[strlen(spec) - 1] = 'e';
            }
            sprintf(out, spec, d);
            putn(&b, out, strlen(out));
            break;
        }
        default:
            /* unknown conversion: copy it */
            put(&b, '%');
            put(&b, conv);
            break;
        }
    }
    if (b.size)
        b.p[(b.n < b.size - 1) ? b.n : b.size - 1] = '\0';
    return (int) b.n;
}

int
snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/*
 * C99 math Lua's math library uses and Helios's C89 libm lacks.
 */
#include <math.h>

double
log2(double x)
{
    return log(x) / 0.69314718055994530942;
}

/*
 * Memory statistics.  With NH_MEMSTAT set in the environment, the game
 * prints at exit how much memory it used: the largest size the C heap
 * reached and the least free system memory seen (sampled at every key
 * read; blocks above half the heap granule come straight from the
 * system pool and show only in the latter).  SysLib's Malloc() with a
 * negative size returns statistics (nucleus/syslib/memory.c).
 */
#include <helios.h>
#include <syslib.h>
#include <errno.h>
#include <unistd.h>

static long mem_heap_max = 0, mem_sys_min = -1, mem_sys_total = 0;
static int mem_registered = 0;

static void
mem_sample(void)
{
    long heap = (long) Malloc(-3), sysfree = (long) Malloc(-5);

    if (heap > mem_heap_max)
        mem_heap_max = heap;
    if (mem_sys_min < 0 || sysfree < mem_sys_min)
        mem_sys_min = sysfree;
}

static void
mem_report(void)
{
    mem_sample();
    fprintf(stderr,
            "NH_MEMSTAT: heap %ld bytes (now %ld free), system memory"
            " %ld total, %ld free at least, %ld free now\n",
            mem_heap_max, (long) Malloc(-1), mem_sys_total, mem_sys_min,
            (long) Malloc(-5));
}

int
helios_getchar(void)
{
    if (!mem_registered) {
        mem_registered = 1;
        if (getenv("NH_MEMSTAT")) {
            mem_sys_total = (long) Malloc(-7);
            atexit(mem_report);
        }
    }
    if (mem_sys_total)
        mem_sample();
    /* One byte straight from the stream, not through stdio: getchar()
     * fills a BUFSIZ (1 KB) buffer, and on a Helios xterm's pseudo-
     * terminal such a read returned a single key only after about a
     * second.  frotz reads single bytes and answers at once. */
    {
        unsigned char c;
        int n;

        do
            n = read(0, (char *) &c, 1);
        while (n < 0 && errno == EINTR);
        return n == 1 ? (int) c : EOF;
    }
}
