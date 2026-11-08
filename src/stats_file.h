#ifndef STATS_FILE_H
#define STATS_FILE_H

#include <stdbool.h>
#include <stdint.h>

// Saved stats in SCRIPTS_PATH "/stats/<name>.bin" (MP only, local stats buffer).

#define STATS_FILE_NAME_MAX 24
#define STATS_FILE_CLAN_MAX 32           // clan tag buffer, NUL included
#define STATS_FILE_PRESTIGE_UNLOCKS 0x1  // flags: Prestige Unlocks was on when saved
#define STATS_FILE_CLASS_COUNT 10        // customclass1-5, prestigeclass1-5
// Class name buffer, NUL included, as in the profile.
#define STATS_FILE_CLASS_NAME_MAX 16

// Saved with the stats, but kept by the game in the gamer profile, not the stats buffer.
// The strings are NUL-terminated.
typedef struct StatsFileProfile {
    char clanName[STATS_FILE_CLAN_MAX];
    char classNames[STATS_FILE_CLASS_COUNT][STATS_FILE_CLASS_NAME_MAX];
} StatsFileProfile;

// Errors besides the negative CellFsErrno codes.
#define STATS_FILE_ERROR_INVALID_NAME -1
#define STATS_FILE_ERROR_FORMAT -2  // not an intact stats file of this version and size

// Turns text into a file name: lowercase, anything but a-z 0-9 _ - becomes _, at most
// STATS_FILE_NAME_MAX characters. out must hold STATS_FILE_NAME_MAX + 1 bytes.
void stats_file_sanitize_name(const char* text, char* out);
// Writes the stats buffer with the profile names and flags. 0 or an error.
int stats_file_save(const char* name, const StatsFileProfile* profile, uint32_t flags);
// Reads and checks a file without touching the stats buffer yet. The profile names are
// only returned. 0 or an error, *profile and *flags are set on success.
int stats_file_load(const char* name, StatsFileProfile* profile, uint32_t* flags);
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
