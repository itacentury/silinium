#include <cell/cell_fs.h>
#include <sys/fs_external.h>

#include "checksums.h"
#include "host_menu.h"
#include "scrfct.h"
#include "stdio.h"
#include "string.h"
#include "t5.h"
#include "utils.h"

// Active mod path, resolved on the first script of a load. Scr_GetChecksum_Hook only
// flags it for re-resolution.
static char modPath[CELL_FS_MAX_FS_PATH_LENGTH];
static bool modPathResolved = false;

// The host sets sv_running in SV_SpawnServer before it compiles the server scripts. A
// non-host compiles them only for the checksum check, and only while sv_running is false
// (CG_RegisterGraphics). Traced in MP, inferred for ZM. A host through migration takes
// the same path (NetMsg_migrateto queues "map <mapname> 1 1"); traced in MP, untested on
// console.
static bool is_server_running() {
    // Dvars are never moved or freed once registered.
    static dvar_s* svRunning = NULL;
    if (!svRunning) {
        svRunning = Dvar_FindVar("sv_running");
    }
    return svRunning && svRunning->current.enabled;
}

char* Scr_LoadRawFile_FastFile_Hook(scriptInstance_t inst, const char* filename,
                                    const char* extFilename, const char* codePos,
                                    bool archive) {
    // Resolve the active mod once per load, so all files of a load come from the same
    // mod.
    if (!modPathResolved) {
        get_or_create_mod_path(modPath);
        modPathResolved = true;

        if (*modPath) {
            printf(T5INFO "Active mod: '%s'.", loader.currentModName);
            // Assumes the first script of a load comes after SV_SpawnServer on the host;
            // a CSC loaded earlier would log this wrongly. Only the log is affected,
            // every file checks sv_running again.
            if (!is_server_running()) {
                printf(T5INFO "Not hosting, server scripts are not replaced.");
            }
        } else {
            printf(T5INFO "No mod active.");
        }
    }

    // A non-host never runs the server scripts; stock ones keep its checksum valid.
    if (inst == SCRIPTINSTANCE_SERVER && !is_server_running()) {
        return Scr_LoadRawFile_FastFile_Trampoline(inst, filename, extFilename, codePos,
                                                   archive);
    }

    // Any file of the active mod with the same path as the game file replaces the
    // fastfile version.
    if (*modPath) {
        char filePath[CELL_FS_MAX_FS_PATH_LENGTH];
        sprintf(filePath, "%s/%s", modPath, extFilename);

        int fileSize = get_file_size(filePath);

        // get_file_size returns a negative error code if the file doesn't exist.
        if (fileSize > 0) {
            char* buffer = (char*)Hunk_AllocateTempMemoryHigh(fileSize + 1);

            if (buffer) {
                int fd;
                CellFsErrno err = cellFsOpen(filePath, CELL_FS_O_RDONLY, &fd, NULL, 0);
                if (err == CELL_FS_SUCCEEDED) {
                    uint64_t read;
                    err = cellFsRead(fd, buffer, fileSize, &read);
                    cellFsClose(fd);

                    if (err == CELL_FS_SUCCEEDED && read == fileSize) {
                        buffer[fileSize] = 0;  // GSC data should be null-terminated.
                        printf(T5INFO "Replacing '%s'.", extFilename);
                        return buffer;
                    }
                    printf(T5ERROR "Failed to read '%s' file.", filePath);
                } else {
                    printf(T5ERROR "Cannot open '%s' file.", filePath);
                }
            } else {
                printf(T5ERROR "Cannot allocate %i bytes for '%s'.", fileSize + 1,
                       extFilename);
            }
        } else if (fileSize == 0) {
            printf(T5WARNING "Ignoring empty '%s' file.", filePath);
        }
    }

    return Scr_LoadRawFile_FastFile_Trampoline(inst, filename, extFilename, codePos,
                                               archive);
}

void Scr_GetChecksum_Hook(scrChecksum_t* checksum, scriptInstance_t inst) {
    // Fill all three values from the real compile first; they stay as is when nothing is
    // patched.
    Scr_GetChecksum_Trampoline(checksum, inst);

    // Runs once per load right after the compile, before the VM sends any notify.
    if (inst == SCRIPTINSTANCE_SERVER) {
        host_menu_on_server_scripts_loaded();
    }

    // modPath keeps the last resolved mod until the next load re-resolves it, so every
    // call of this load (any instance, any count) sees the same value. If a load aborts
    // before this runs (e.g. a script compile error), the next load reuses the old mod.
    bool modActive = *modPath != 0;
    modPathResolved = false;  // re-resolve on the next script load

    // Only server checksums are verified, and vanilla scripts already have the right one
    // (a non-host compiles them unmodified).
    if (inst != SCRIPTINSTANCE_SERVER || !modActive || !is_server_running()) {
        return;
    }

    // Report the checksum of the unmodified scripts for the current map/gametype.
    dvar_s* mapname = Dvar_FindVar("mapname");
    dvar_s* gametype = Dvar_FindVar("g_gametype");

    if (mapname && gametype) {
        const ChecksumEntry* entry = checksum_lookup(
            isMultiplayer, mapname->current.string, gametype->current.string);
        if (entry) {
            checksum->checksum = entry->checksum;
            checksum->programLen = entry->programLen;
            // The caller passes a zeroed buffer and InfinityOps leaves substract at 0.
            // The value from the modded compile makes clients drop the connection while
            // loading the map.
            checksum->substract = 0;
            printf(T5INFO "Checksum patched (0x%08X)", checksum->checksum);
        } else {
            printf(T5WARNING "No checksum entry for '%s' / '%s'.",
                   mapname->current.string, gametype->current.string);
        }
    }
}

popd32 Scr_GetFunction_Hook(const char** pName, int* type) {
    popd32 opd = Scr_GetFunction_Trampoline(pName, type);
    if (opd == 0) {
        // Function name are always in lowercase, we should return an opd pointer.
        if (strcmp(*pName, "setmemory") == 0) {
            printf(T5INFO "Function 'setmemory' found.");
            return (popd32)&scrfct_setmemory;
        }
        // Needs the host menu hooks, which only exist in MP.
        if (strcmp(*pName, "callkeyboard") == 0 && host_menu_ready()) {
            printf(T5INFO "Function 'callkeyboard' found.");
            return (popd32)&scrfct_callkeyboard;
        }
        return 0;
    }
    return opd;
}
