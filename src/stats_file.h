#ifndef STATS_FILE_H
#define STATS_FILE_H

#include <stdbool.h>
#include <stdint.h>

// Saved stats in SCRIPTS_PATH "/stats/<name>.bin" (MP only, local stats buffer).

#define STATS_FILE_NAME_MAX 24
#define STATS_FILE_CLAN_MAX 32           // clan tag buffer, NUL included
#define STATS_FILE_PRESTIGE_UNLOCKS 0x1  // flags: Prestige Unlocks was on when saved

// Errors besides the negative CellFsErrno codes.
#define STATS_FILE_ERROR_INVALID_NAME -1
#define STATS_FILE_ERROR_FORMAT -2  // not a stats file of this version and size

// Turns text into a file name: lowercase, anything but a-z 0-9 _ - becomes _, at most
// STATS_FILE_NAME_MAX characters. out must hold STATS_FILE_NAME_MAX + 1 bytes.
void stats_file_sanitize_name(const char* text, char* out);
// Writes the stats buffer with the clan tag and flags. 0 or an error.
int stats_file_save(const char* name, const char* clanName, uint32_t flags);
// Reads and checks a file without touching the stats buffer yet. The clan tag is only
// returned (clanName holds STATS_FILE_CLAN_MAX bytes). 0 or an error, clanName and *flags
// are set on success.
int stats_file_load(const char* name, char* clanName, uint32_t* flags);
// Copies the stats from the last successful load into the stats buffer, keeping the live
// account and console fields, and marks them dirty. On the main thread, where the game's
// stats code runs; no save or load may run until it returned, they share the buffer.
void stats_file_apply_loaded();
int stats_file_delete(const char* name);
// Fills names with up to max saved names (without .bin), returns how many.
int stats_file_list(char (*names)[STATS_FILE_NAME_MAX + 1], int max);
// Short reason for an error, for the menu.
const char* stats_file_error_text(int error);

#endif /* STATS_FILE_H */
