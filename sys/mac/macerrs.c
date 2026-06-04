/* NetHack 3.6	macerrs.c	$NHDT-Date: 1432512797 2015/05/25 00:13:17 $  $NHDT-Branch: master $:$NHDT-Revision: 1.10 $ */
/* Copyright (c) Michael Hamel, 1991 */
/* NetHack may be freely redistributed.  See license for details. */

#if defined(macintosh) && defined(__SC__) && !defined(__FAR_CODE__)
/* this needs to be resident always */
#pragma segment Main
#endif

#include "hack.h"
#include "macwin.h"
#include <Dialogs.h>
#include <TextUtils.h>
#include <Resources.h>

void
error(const char *format, ...)
{
    char cbuf[512];
    int len;
    va_list ap;

    va_start(ap, format);
    vsnprintf(cbuf, sizeof cbuf, format, ap);
    va_end(ap);
    len = strlen(cbuf);

    /* Show error and wait for click before exiting */
    {
        WindowPtr w;
        Rect r = {80, 40, 200, 472};

        w = NewWindow(NULL, &r, P_STRING_CONV("NetHack Error"), true,
                      dBoxProc, (WindowPtr)-1, false, 0);
        if (w) {
            SetPortWindowPort(w);
            MoveTo(10, 30);
            if (len > 0)
                DrawText(cbuf, 0, len);
            MoveTo(10, 60);
            DrawString(P_STRING_CONV("Click to exit."));
            while (!Button())
                ;
            DisposeWindow(w);
        }
    }
    ExitToShell();
}
