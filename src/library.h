/*
 * SwanStationPS5 - game library scanning.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_LIBRARY_H
#define SwanStationPS5_LIBRARY_H

#include "SwanStationPS5.h"

typedef struct
{
    char title[96];
    char serial[16];               /* e.g. SLUS-00421 when the name carries it */
    char path[SwanStationPS5_PATH_MAX];     /* what the core loads: .m3u/.cue/.chd/.pbp/... */
    char id[64];                   /* stable key for save states */
    char disc_name[160];           /* first disc file name without extension, for .cht lookup */
    char folder[SwanStationPS5_PATH_MAX];   /* directory holding the game files */
    int discs;
} Game;

/* The console's BIOS booted with no disc (Settings > About > Start BIOS): a Game with no path */
static inline bool game_is_bios(const Game *g)
{
    return g && !g->path[0];
}

typedef struct
{
    Game *games;
    int count;
    int capacity;
} Library;

/* Scans every existing root. Each root holds one game per folder, or loose
 * images. Multi-disc folders without an .m3u get one written for them. */
void library_scan(Library *lib, const char *const *roots, int root_count);
/* Add scannable roots to a library already loaded from an index. */
void library_scan_append(Library *lib, const char *const *roots, int root_count);

/* For a sandboxed SwanStationPS5 (folders can't be listed): reads the index written by
 * tools/SwanStationPS5_sync.py, one game per line:
 *   title <TAB> serial <TAB> discs <TAB> /data/SwanStationPS5/games/.../file.m3u <TAB> disc name
 * Returns false when the index can't be opened. */
bool library_load_index(Library *lib, const char *index_path);
void library_free(Library *lib);

/* Deletes the files that make the game appear on the shelf: the file the core loads (.cue, .m3u, .chd, .pbp, .iso...),
 * what a .cue or .m3u lists (its .bin, its discs), and the .sbi beside it. Only files inside the game's folder;
 * the folder goes too when it is one SwanStationPS5 made ("Title [serial]") and nothing else is left in it. Saves,
 * states and covers are not touched. Returns how many files were deleted. */
int library_delete_game_files(const Game *g);

#endif
