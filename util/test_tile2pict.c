/* NetHack 5.0	test_tile2pict.c	*/
/* Copyright (c) Ingo Paschke, 2026. */
/* NetHack may be freely redistributed.  See license for details. */
/* test_tile2pict.c -- host regression harness for util/tile2pict.
 * Runs tile2pict against the synthetic fixture; asserts both PICT 1000 and
 * 1001 are emitted.  Exit 0 on pass; nonzero with a diagnostic on failure.
 *
 * Absolute paths are intentional: this test is pinned to the single dev
 * workstation at /home/ipaschke/Source/nh37-macos/NetHack.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_UTIL \
    "/home/ipaschke/Source/nh37-macos/NetHack/util/test_tile2pict_fixture/util"
#define TILE2PICT_BIN \
    "/home/ipaschke/Source/nh37-macos/NetHack/util/tile2pict"
#define OUTFILE "/tmp/test_tile2pict_out.r"

int
main(void)
{
    int rc;
    FILE *f;
    int saw_1000 = 0, saw_1001 = 0;
    char line[512];

    /* Run tile2pict from the fixture's util/ subdirectory.
     * tile2pict uses relative_tiledir = "../win/share/", so CWD must be
     * one level above a win/share/ tree — that is, the fixture root.
     * The fixture/util/ directory satisfies that requirement. */
    rc = system(
        "cd " FIXTURE_UTIL
        " && " TILE2PICT_BIN " " OUTFILE);
    if (rc != 0) {
        fprintf(stderr, "FAIL: tile2pict exited with %d\n", rc);
        return 1;
    }

    f = fopen(OUTFILE, "r");
    if (!f) {
        fprintf(stderr, "FAIL: output file %s missing\n", OUTFILE);
        return 1;
    }

    while (fgets(line, (int) sizeof line, f)) {
        if (strstr(line, "'PICT' (1000)"))
            saw_1000 = 1;
        if (strstr(line, "'PICT' (1001)"))
            saw_1001 = 1;
    }
    fclose(f);

    if (!saw_1000 || !saw_1001) {
        fprintf(stderr, "FAIL: missing PICT resource(s) (1000=%d 1001=%d)\n",
                saw_1000, saw_1001);
        return 1;
    }

    printf("OK\n");
    return 0;
}
