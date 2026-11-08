#include "stats_file.h"

#include <cell/cell_fs.h>
#include <stdio.h>
#include <string.h>
#include <sys/fs_external.h>

#include "defines.h"
#include "offsets.h"
#include "utils.h"

#define STATS_DIR SCRIPTS_PATH "/stats"
#define STATS_FILE_EXTENSION ".bin"
#define STATS_FILE_TEMP_EXTENSION ".tmp"
#define STATS_FILE_VERSION 1

// Big endian like the PPU writes it. A later version can append the emblem after the
// stats.
typedef struct StatsFileHeader {
    char magic[4];  // "SLST"
    uint32_t version;
    uint32_t statsSize;  // T5_StatsSize
    uint32_t flags;      // STATS_FILE_*
    char clanName[STATS_FILE_CLAN_MAX];
} StatsFileHeader;

static const char statsFileMagic[4] = {'S', 'L', 'S', 'T'};

typedef struct StatsFile {
    StatsFileHeader header;
    uint8_t stats[T5_StatsSize];
} StatsFile;

// Too large for the menu thread's stack. Only the menu thread saves and loads, the main
// thread reads it in stats_file_apply_loaded while a load waits to be applied.
static StatsFile file;

typedef struct DdlField {
    int bit;  // from the DDL start, like the T5_Stats_*Bit offsets
    int bits;
} DdlField;

// Account, console and moderation fields: loading stats saved on another account or
// console must not take over its XUID, addresses, tokens, bans, cheater flag or version
// stamps.
static const DdlField protectedFields[] = {
    {T5_Stats_ConnectionIdBit, 64},
    {T5_Stats_ConsoleInfoBit, T5_Stats_ConsoleInfoBits},
    {T5_Stats_LastConsoleSaveBit, 4},
    {T5_Stats_FriendsListFingerprintBit, 64},
    {T5_Stats_PlayerXuidBit, 64},
    {T5_Stats_StatResetNotificationBit, 64},
    {T5_Stats_ResetWarningDisplayedBit, 1},
    {T5_Stats_UploadBandwidthBit, 32},
    {T5_Stats_WebTokenBit, 128},
    {T5_Stats_IAmACheaterBit, 1},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_BadHostCount), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_GametypeBan), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_MonthlyTimestamp), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_SessionBans), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_StatsVersion), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_StatsBackupVersion), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_TimeWhenNextHost), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_Version), T5_Stats_PlayerStatBits},
    {T5_Stats_PlayerStatBit(T5_PlayerStat_WeeklyTimestamp), T5_Stats_PlayerStatBits},
};

static uint8_t* stats_buffer() { return (uint8_t*)T5_Stats_Base; }

void stats_file_sanitize_name(const char* text, char* out) {
    int length = 0;
    for (; *text && length < STATS_FILE_NAME_MAX; text++) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        out[length++] = allowed ? c : '_';
    }
    out[length] = 0;
}

// Names come from the keyboard or the folder listing, so anything sanitize_name would
// change is rejected instead of opening a path outside the folder.
static bool valid_name(const char* name) {
    char sanitized[STATS_FILE_NAME_MAX + 1];
    stats_file_sanitize_name(name, sanitized);
    return *name && strcmp(name, sanitized) == 0;
}

static void file_path(const char* name, char* out) {
    sprintf(out, STATS_DIR "/%s" STATS_FILE_EXTENSION, name);
}

static void temp_file_path(const char* name, char* out) {
    sprintf(out, STATS_DIR "/%s" STATS_FILE_TEMP_EXTENSION, name);
}

static int make_dir(const char* path) {
    CellFsErrno err = cellFsMkdir(path, CELL_FS_DEFAULT_CREATE_MODE_1);
    return err == CELL_FS_SUCCEEDED || err == CELL_FS_EEXIST ? 0 : err;
}

static int write_all(int fd, const void* data, uint64_t size) {
    const uint8_t* bytes = data;
    while (size > 0) {
        uint64_t written;
        CellFsErrno err = cellFsWrite(fd, bytes, size, &written);
        if (err != CELL_FS_SUCCEEDED) {
            return err;
        }
        if (!written) {
            return CELL_FS_EIO;
        }
        bytes += written;
        size -= written;
    }
    return 0;
}

static int read_all(int fd, void* data, uint64_t size) {
    uint8_t* bytes = data;
    while (size > 0) {
        uint64_t read;
        CellFsErrno err = cellFsRead(fd, bytes, size, &read);
        if (err != CELL_FS_SUCCEEDED) {
            return err;
        }
        if (!read) {
            return STATS_FILE_ERROR_FORMAT;  // shorter than a stats file
        }
        bytes += read;
        size -= read;
    }
    return 0;
}

static void copy_bits(uint8_t* to, const uint8_t* from, int bit, int count) {
    // ddl_read_bits returns an int, so in chunks that stay clear of the sign bit.
    while (count > 0) {
        int chunk = count < 16 ? count : 16;
        ddl_write_bits(to, bit, chunk, ddl_read_bits(from, bit, chunk));
        bit += chunk;
        count -= chunk;
    }
}

static void clear_bits(uint8_t* buffer, int bit, int count) {
    // ddl_write_bits shifts an int by up to count - 1, so in chunks like copy_bits.
    while (count > 0) {
        int chunk = count < 16 ? count : 16;
        ddl_write_bits(buffer, bit, chunk, 0);
        bit += chunk;
        count -= chunk;
    }
}

int stats_file_save(const char* name, const char* clanName, uint32_t flags) {
    if (!valid_name(name)) {
        return STATS_FILE_ERROR_INVALID_NAME;
    }
    int err;
    if ((err = make_dir(SCRIPTS_PATH)) || (err = make_dir(STATS_DIR))) {
        printf(T5ERROR "Save Stats: cannot create '%s' (0x%08X).", STATS_DIR, err);
        return err;
    }

    // A snapshot first, so the file holds one state even if the game writes the stats
    // meanwhile.
    memset(&file.header, 0, sizeof(file.header));
    memcpy(file.header.magic, statsFileMagic, sizeof(file.header.magic));
    file.header.version = STATS_FILE_VERSION;
    file.header.statsSize = T5_StatsSize;
    file.header.flags = flags;
    snprintf(file.header.clanName, sizeof(file.header.clanName), "%s",
             clanName ? clanName : "");
    memcpy(file.stats, stats_buffer(), sizeof(file.stats));
    // Files get shared and load restores these fields from the live stats anyway, so the
    // saver's XUID, console info and tokens never end up in a file.
    for (int i = 0; i < sizeof(protectedFields) / sizeof(*protectedFields); i++) {
        clear_bits(file.stats, T5_Stats_DdlBit + protectedFields[i].bit,
                   protectedFields[i].bits);
    }

    // Written to a temporary file first, so a failed write (HDD full, power loss) leaves
    // an existing save with this name intact.
    char tempPath[CELL_FS_MAX_FS_PATH_LENGTH];
    temp_file_path(name, tempPath);
    int fd;
    err = cellFsOpen(tempPath, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &fd,
                     NULL, 0);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Save Stats: cannot open '%s' (0x%08X).", tempPath, err);
        return err;
    }
    err = write_all(fd, &file, sizeof(file));
    if (!err) {
        // On disk before the rename, so the renamed file is never a partial one.
        err = cellFsFsync(fd);
    }
    cellFsClose(fd);
    if (err) {
        printf(T5ERROR "Save Stats: cannot write '%s' (0x%08X).", tempPath, err);
        cellFsUnlink(tempPath);
        return err;
    }

    char path[CELL_FS_MAX_FS_PATH_LENGTH];
    file_path(name, path);
    err = cellFsRename(tempPath, path);
    if (err == CELL_FS_EEXIST) {
        // cellFsRename doesn't replace an existing file. Between unlink and rename only
        // the complete temporary file exists.
        err = cellFsUnlink(path);
        if (err == CELL_FS_SUCCEEDED) {
            err = cellFsRename(tempPath, path);
        }
    }
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Save Stats: cannot replace '%s' (0x%08X).", path, err);
        return err;
    }
    printf(T5INFO "Save Stats: saved '%s'.", path);
    return 0;
}

static bool valid_header(const StatsFileHeader* header) {
    return memcmp(header->magic, statsFileMagic, sizeof(header->magic)) == 0 &&
           header->version == STATS_FILE_VERSION && header->statsSize == T5_StatsSize;
}

int stats_file_load(const char* name, char* clanName, uint32_t* flags) {
    if (!valid_name(name)) {
        return STATS_FILE_ERROR_INVALID_NAME;
    }
    char path[CELL_FS_MAX_FS_PATH_LENGTH];
    file_path(name, path);
    int fd;
    int err = cellFsOpen(path, CELL_FS_O_RDONLY, &fd, NULL, 0);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Load Stats: cannot open '%s' (0x%08X).", path, err);
        return err;
    }
    err = read_all(fd, &file.header, sizeof(file.header));
    if (!err && !valid_header(&file.header)) {
        err = STATS_FILE_ERROR_FORMAT;
    }
    if (!err) {
        err = read_all(fd, file.stats, sizeof(file.stats));
    }
    cellFsClose(fd);
    if (err) {
        printf(T5ERROR "Load Stats: '%s' is not a valid stats file (0x%08X).", path, err);
        return err;
    }

    memcpy(clanName, file.header.clanName, STATS_FILE_CLAN_MAX);
    clanName[STATS_FILE_CLAN_MAX - 1] = 0;
    *flags = file.header.flags;
    printf(T5INFO "Load Stats: loaded '%s'.", path);
    return 0;
}

void stats_file_apply_loaded() {
    // The game's header stays, only the DDL data is taken over.
    uint8_t* stats = stats_buffer();
    // The file's own protected fields are replaced with the live ones right before the
    // copy, on the thread the game updates them on, so no value changed since the load is
    // written back and the live stats change in a single copy.
    for (int i = 0; i < sizeof(protectedFields) / sizeof(*protectedFields); i++) {
        copy_bits(file.stats, stats, T5_Stats_DdlBit + protectedFields[i].bit,
                  protectedFields[i].bits);
    }
    memcpy(stats + T5_StatsHeaderSize, file.stats + T5_StatsHeaderSize,
           T5_StatsSize - T5_StatsHeaderSize);
    stats[T5_StatsDirtyOffset] |= T5_StatsDirtyFlag;
}

int stats_file_delete(const char* name) {
    if (!valid_name(name)) {
        return STATS_FILE_ERROR_INVALID_NAME;
    }
    char path[CELL_FS_MAX_FS_PATH_LENGTH];
    file_path(name, path);
    CellFsErrno err = cellFsUnlink(path);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Delete Stats: cannot delete '%s' (0x%08X).", path, err);
        return err;
    }
    printf(T5INFO "Delete Stats: deleted '%s'.", path);
    return 0;
}

// Inserted in order, so the list doesn't depend on the order the HDD returns.
static int insert_name(char (*names)[STATS_FILE_NAME_MAX + 1], int count, int max,
                       const char* name) {
    int i = count;
    while (i > 0 && strcmp(names[i - 1], name) > 0) {
        if (i < max) {
            strcpy(names[i], names[i - 1]);
        }
        i--;
    }
    if (i >= max) {
        return count;
    }
    strcpy(names[i], name);
    return count < max ? count + 1 : count;
}

int stats_file_list(char (*names)[STATS_FILE_NAME_MAX + 1], int max) {
    int fd;
    if (cellFsOpendir(STATS_DIR, &fd) != CELL_FS_SUCCEEDED) {
        return 0;  // nothing saved yet
    }
    int count = 0;
    CellFsDirent entry;
    uint64_t read;
    while (cellFsReaddir(fd, &entry, &read) == CELL_FS_SUCCEEDED && read) {
        size_t length = strlen(entry.d_name);
        size_t extension = sizeof(STATS_FILE_EXTENSION) - 1;
        if (entry.d_type != CELL_FS_TYPE_REGULAR || length <= extension ||
            length - extension > STATS_FILE_NAME_MAX ||
            strcmp(entry.d_name + length - extension, STATS_FILE_EXTENSION) != 0) {
            continue;
        }
        entry.d_name[length - extension] = 0;
        // Files copied over FTP with other names can't be loaded, so they aren't listed.
        if (valid_name(entry.d_name)) {
            count = insert_name(names, count, max, entry.d_name);
        }
    }
    cellFsClosedir(fd);
    return count;
}

const char* stats_file_error_text(int error) {
    switch (error) {
        case 0:
            return "";
        case STATS_FILE_ERROR_INVALID_NAME:
            return "invalid name";
        case STATS_FILE_ERROR_FORMAT:
            return "not a stats file";
        case CELL_FS_ENOENT:
            return "file not found";
        case CELL_FS_ENOSPC:
            return "HDD full";
        default:
            return "file error";
    }
}
