#include "t5.h"

#include "stdio.h"
#include "string.h"
#include "utils.h"
#include "scrfct.h"
#include "checksums.h"
#include "host_menu.h"

#include <cell/cell_fs.h>
#include <sys/fs_external.h>

// Active mod path, resolved on the first script of a load. Scr_GetChecksum_Hook only flags it for re-resolution.
static char modPath[CELL_FS_MAX_FS_PATH_LENGTH];
static bool modPathResolved = false;

char *Scr_LoadRawFile_FastFile_Hook(scriptInstance_t inst, const char *filename, const char *extFilename, const char *codePos, bool archive)
{
    // Resolve the active mod once per load, so all files of a load come from the same mod.
    if (!modPathResolved)
    {
        get_or_create_mod_path(modPath);
        modPathResolved = true;

        if (*modPath)
            printf(T5INFO "Active mod: '%s'.", loader.currentModName);
        else
            printf(T5INFO "No mod active.");
    }

    // Any file of the active mod with the same path as the game file replaces the fastfile version.
    if (*modPath)
    {
        char filePath[CELL_FS_MAX_FS_PATH_LENGTH];
        sprintf(filePath, "%s/%s", modPath, extFilename);

        int fileSize = get_file_size(filePath);

        // get_file_size returns a negative error code if the file doesn't exist.
        if (fileSize > 0)
        {
            char *buffer = (char*)Hunk_AllocateTempMemoryHigh(fileSize + 1);

            if (buffer)
            {
                int fd;
                CellFsErrno err = cellFsOpen(filePath, CELL_FS_O_RDONLY, &fd, NULL, 0);
                if (err == CELL_FS_SUCCEEDED)
                {
                    uint64_t read;
                    err = cellFsRead(fd, buffer, fileSize, &read);
                    cellFsClose(fd);

                    if (err == CELL_FS_SUCCEEDED && read == fileSize)
                    {
                        buffer[fileSize] = 0; // GSC data should be null-terminated.
                        printf(T5INFO "Replacing '%s'.", extFilename);
                        return buffer;
                    }
                    printf(T5ERROR "Failed to read '%s' file.", filePath);
                }
                else
                    printf(T5ERROR "Cannot open '%s' file.", filePath);
            }
            else
                printf(T5ERROR "Cannot allocate %i bytes for '%s'.", fileSize + 1, extFilename);
        }
        else if (fileSize == 0)
            printf(T5WARNING "Ignoring empty '%s' file.", filePath);
    }

    return Scr_LoadRawFile_FastFile_Trampoline(inst, filename, extFilename, codePos, archive);
}

void Scr_GetChecksum_Hook(scrChecksum_t *checksum, scriptInstance_t inst)
{
    // Fill all three values from the real compile first; they stay as is when nothing is patched.
    Scr_GetChecksum_Trampoline(checksum, inst);

    // Runs once per load right after the compile, before the VM sends any notify.
    if (inst == SCRIPTINSTANCE_SERVER)
        host_menu_on_server_scripts_loaded();

    // modPath keeps the last resolved mod until the next load re-resolves it, so every call of
    // this load (any instance, any count) sees the same value. If a load aborts before this runs
    // (e.g. a script compile error), the next load reuses the old mod.
    bool modActive = *modPath != 0;
    modPathResolved = false; // re-resolve on the next script load

    // Only server checksums are verified, and vanilla scripts already have the right one.
    if (inst != SCRIPTINSTANCE_SERVER || !modActive)
        return;

    // Report the checksum of the unmodified scripts for the current map/gametype.
    dvar_s *mapname = Dvar_FindVar("mapname");
    dvar_s *gametype = Dvar_FindVar("g_gametype");

    if (mapname && gametype)
    {
        const ChecksumEntry *entry = checksum_lookup(isMultiplayer, mapname->current.string, gametype->current.string);
        if (entry)
        {
            checksum->checksum = entry->checksum;
            checksum->programLen = entry->programLen;
            // The caller passes a zeroed buffer and InfinityOps leaves substract at 0. The value
            // from the modded compile makes clients drop the connection while loading the map.
            checksum->substract = 0;
            printf(T5INFO "Checksum patched (0x%08X)", checksum->checksum);
        }
        else
            printf(T5WARNING "No checksum entry for '%s' / '%s'.", mapname->current.string, gametype->current.string);
    }
}

popd32 Scr_GetFunction_Hook(const char **pName, int *type)
{
    popd32 opd = Scr_GetFunction_Trampoline(pName, type);
    if (opd == 0)
    {
        // Function name are always in lowercase, we should return an opd pointer.
        if (strcmp(*pName, "setmemory") == 0)
        {
            printf(T5INFO "Function 'setmemory' found.");
            return (popd32)&scrfct_setmemory;
        }
        return 0;
    }
    return opd;
}
