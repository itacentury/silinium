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
#define STATS_FILE_RECOVER_MAX 8  // orphaned temporary files handled per listing

// Big endian like the PPU writes it. A later version can append the emblem after the
// stats.
typedef struct StatsFileHeader {
    char magic[4];  // "SLST"
    uint32_t version;
    uint32_t statsSize;  // T5_StatsSize
    uint32_t flags;      // STATS_FILE_*
    uint32_t crc;        // CRC-32 of the header with crc 0, then the stats
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

// Standard CRC-32 (zlib), chainable: pass the previous result to continue over more data.
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size) {
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// The header part of the file CRC; continue it over the stats with crc32_update.
static uint32_t header_crc(const StatsFileHeader* header) {
    StatsFileHeader copy = *header;
    copy.crc = 0;
    return crc32_update(0, (const uint8_t*)&copy, sizeof(copy));
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

// Moves a complete temporary file to the save's path, replacing an existing save.
static CellFsErrno replace_file(const char* tempPath, const char* path) {
    CellFsErrno err = cellFsRename(tempPath, path);
    if (err != CELL_FS_EEXIST) {
        return err;
    }
    // cellFsRename doesn't replace an existing file. Between unlink and rename only the
    // complete temporary file exists.
    err = cellFsUnlink(path);
    if (err != CELL_FS_SUCCEEDED) {
        return err;
    }
    return cellFsRename(tempPath, path);
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
    file.header.crc =
        crc32_update(header_crc(&file.header), file.stats, sizeof(file.stats));

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
    err = replace_file(tempPath, path);
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
    if (!err && crc32_update(header_crc(&file.header), file.stats, sizeof(file.stats)) !=
                    file.header.crc) {
        err = STATS_FILE_ERROR_FORMAT;
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

// Cuts the extension off a regular file's name and returns whether it is a stats name
// with that extension. Files copied over FTP with other names can't be loaded.
static bool strip_extension(CellFsDirent* entry, const char* extension) {
    size_t length = strlen(entry->d_name);
    size_t extensionLength = strlen(extension);
    if (entry->d_type != CELL_FS_TYPE_REGULAR || length <= extensionLength ||
        length - extensionLength > STATS_FILE_NAME_MAX ||
        strcmp(entry->d_name + length - extensionLength, extension) != 0) {
        return false;
    }
    entry->d_name[length - extensionLength] = 0;
    return valid_name(entry->d_name);
}

// Streams the stats through a stack buffer instead of the shared file buffer, which may
// still hold a load waiting for stats_file_apply_loaded. Returns 0 if the file is intact,
// STATS_FILE_ERROR_FORMAT if it is not a complete stats file, otherwise the file system
// error.
static int check_file(const char* path) {
    int fd;
    int err = cellFsOpen(path, CELL_FS_O_RDONLY, &fd, NULL, 0);
    if (err != CELL_FS_SUCCEEDED) {
        return err;
    }
    StatsFileHeader header;
    err = read_all(fd, &header, sizeof(header));
    if (!err && !valid_header(&header)) {
        err = STATS_FILE_ERROR_FORMAT;
    }
    uint32_t crc = err ? 0 : header_crc(&header);
    uint8_t chunk[1024];
    for (size_t left = sizeof(file.stats); !err && left > 0;) {
        size_t size = left < sizeof(chunk) ? left : sizeof(chunk);
        err = read_all(fd, chunk, size);
        crc = crc32_update(crc, chunk, size);
        left -= size;
    }
    cellFsClose(fd);
    if (!err && crc != header.crc) {
        err = STATS_FILE_ERROR_FORMAT;
    }
    return err;
}

// Removed instead of kept, so it is logged once and doesn't take one of the
// STATS_FILE_RECOVER_MAX slots on every listing.
static void remove_temp_file(const char* tempPath, const char* reason) {
    CellFsErrno err = cellFsUnlink(tempPath);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Stats: cannot remove '%s' (0x%08X).", tempPath, err);
        return;
    }
    printf(T5WARNING "Stats: removed leftover '%s' (%s).", tempPath, reason);
}

static void recover_temp_file(const char* name) {
    char path[CELL_FS_MAX_FS_PATH_LENGTH];
    char tempPath[CELL_FS_MAX_FS_PATH_LENGTH];
    file_path(name, path);
    temp_file_path(name, tempPath);
    CellFsStat status;
    if (cellFsStat(tempPath, &status) != CELL_FS_SUCCEEDED) {
        return;
    }
    // Power lost before the fsync in stats_file_save. The size can be on disk before all
    // of the data, so only the CRC shows the file is complete.
    int err = status.st_size == sizeof(StatsFile) ? check_file(tempPath)
                                                  : STATS_FILE_ERROR_FORMAT;
    if (err == STATS_FILE_ERROR_FORMAT) {
        remove_temp_file(tempPath, "incomplete");
        return;
    }
    // A read error says nothing about the file, and it may be the only copy of the save:
    // kept for the next listing.
    if (err) {
        printf(T5ERROR "Stats: cannot read '%s' (0x%08X).", tempPath, err);
        return;
    }
    // Complete, so the fsync finished: power was lost before the rename, or the rename or
    // unlink failed. Finished like stats_file_save does, replacing an older save with
    // this name, so the user gets the save they asked for. That save may never have
    // reported an error.
    err = replace_file(tempPath, path);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Stats: cannot recover '%s' (0x%08X).", tempPath, err);
        return;
    }
    printf(T5INFO "Stats: recovered '%s'.", path);
}

// A save interrupted after its temporary file was complete (power lost before the rename
// or between unlink and rename, or a failed rename) is finished on the next listing.
static void recover_temp_files() {
    int fd;
    if (cellFsOpendir(STATS_DIR, &fd) != CELL_FS_SUCCEEDED) {
        return;
    }
    // Renamed only after the directory is closed, so the listing isn't changed while it
    // is read. More files are recovered on the next listing.
    char names[STATS_FILE_RECOVER_MAX][STATS_FILE_NAME_MAX + 1];
    int count = 0;
    CellFsDirent entry;
    uint64_t read;
    while (count < STATS_FILE_RECOVER_MAX &&
           cellFsReaddir(fd, &entry, &read) == CELL_FS_SUCCEEDED && read) {
        if (strip_extension(&entry, STATS_FILE_TEMP_EXTENSION)) {
            strcpy(names[count++], entry.d_name);
        }
    }
    cellFsClosedir(fd);
    for (int i = 0; i < count; i++) {
        recover_temp_file(names[i]);
    }
}

int stats_file_list(char (*names)[STATS_FILE_NAME_MAX + 1], int max) {
    recover_temp_files();
    int fd;
    if (cellFsOpendir(STATS_DIR, &fd) != CELL_FS_SUCCEEDED) {
        return 0;  // nothing saved yet
    }
    int count = 0;
    CellFsDirent entry;
    uint64_t read;
    while (cellFsReaddir(fd, &entry, &read) == CELL_FS_SUCCEEDED && read) {
        if (strip_extension(&entry, STATS_FILE_EXTENSION)) {
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
