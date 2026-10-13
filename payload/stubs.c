/*
 * SwanStationPS5 disc daemon - the parts of the app that src/discimport.c calls and the payload does not have: the
 * game database (read straight from the index the app keeps), covers and textures (the notification has no picture).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "covers.h"
#include "disc.h"
#include "gamedb.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAMEDB_INDEX "/data/SwanStationPS5/cache/gamedb-3.txt"

/* The app's compact index: one line per serial, tab separated, the serial first and the name second. */
const GameInfo *gamedb_get(const char *serial)
{
    static GameInfo found;
    char want[16];
    if (!serial || !serial[0] || !disc_format_serial(serial, want, sizeof(want)))
        return NULL;
    FILE *f = fopen(GAMEDB_INDEX, "r");
    if (!f)
        return NULL;
    char line[600];
    size_t n = strlen(want);
    bool ok = false;
    while (!ok && fgets(line, sizeof(line), f))
    {
        if (strncmp(line, want, n) != 0 || line[n] != '\t')
            continue;
        char *name = line + n + 1;
        name[strcspn(name, "\t\r\n")] = '\0';
        memset(&found, 0, sizeof(found));
        str_copy(found.serial, sizeof(found.serial), want);
        str_copy(found.name, sizeof(found.name), name);
        ok = true;
    }
    fclose(f);
    return ok ? &found : NULL;
}

uint8_t *covers_for_serial(const char *serial, int *w, int *h)
{
    (void)serial, (void)w, (void)h;
    return NULL;
}

PlatTexture *plat_texture_create(const uint8_t *rgba, int width, int height, bool smooth)
{
    (void)rgba, (void)width, (void)height, (void)smooth;
    return NULL;
}

void plat_texture_free(PlatTexture *texture)
{
    (void)texture;
}
