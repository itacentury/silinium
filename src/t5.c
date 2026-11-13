#include "t5.h"

#include <cell/error.h>
#include <cell/fs/cell_fs_file_api.h>
#include <sys/memory.h>
#include <sys/timer.h>

#include "anti_freeze.h"
#include "client_command.h"
#include "cshook.h"
#include "host_menu.h"
#include "notify.h"
#include "offsets.h"
#include "string.h"
#include "utils.h"

void init_offsets() {
    // Set native offsets
    t5ni(Dvar_FindVar);
    t5ni(Scr_GetNumParam);
    t5ni(Scr_GetString);
    t5ni(Scr_GetInt);
    t5ni(Scr_AddInt);
    t5ni(Scr_AddString);
    t5ni(Hunk_AllocateTempMemoryHigh);
}

// MP natives used by both the anti-freeze and the host menu, set before either inits.
static void init_mp_offsets() {
    t5nmi(SV_Cmd_ArgvBuffer);
    t5nmi(Material_RegisterHandle);
    t5nmi(I_stricmp);
}

int init_hooks() {
    // Set hooks offsets
    t5nhi(Scr_LoadRawFile_FastFile);
    t5nhi(Scr_GetChecksum);
    t5nhi(Scr_GetFunction);

    // Create and enable hooks
    int res;
    if ((res = cs_hook_install(Scr_LoadRawFile_FastFile, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(Scr_GetChecksum, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }
    if ((res = cs_hook_install(Scr_GetFunction, CS_HOOK_TYPE_CTR)) < 0) {
        return res;
    }

    return 0;
}

int apply_patches() {
    // No error on oversized temp allocations (used for the replaced files), and every
    // dvar becomes settable.
    const InstructionPatch patches[] = {
        {t5o(Hunk_AllocateTempMemoryHigh_ErrorCheck), 0x419D00C4, PPC_NOP},
        {t5o(Dvar_SetVariant_ReadOnlyCheck), isMultiplayer ? 0x409A0058 : 0x409A005C,
         PPC_NOP},
        {t5o(Dvar_SetVariant_WriteProtectedCheck),
         isMultiplayer ? 0x409E0090 : 0x409E0094, PPC_NOP},
        {t5o(Dvar_SetVariant_CheatCheck), 0x4182FE70,
         PPC_B(t5o(Dvar_SetVariant_CheatCheck), t5o(Dvar_SetVariant_CheatCheckTarget))},
    };

    int res;
    for (int i = 0; i < sizeof(patches) / sizeof(*patches); i++) {
        if ((res = apply_instruction_patch(&patches[i], true)) < 0) {
            return res;
        }
    }

    return 0;
}

void get_or_create_mod_path(char* path) {
    int fd;
    CellFsErrno err;
    char pathMods[CELL_FS_MAX_FS_PATH_LENGTH];
    sprintf(pathMods, SCRIPTS_PATH "/%s", isMultiplayer ? "mp" : "zm");

    *path = 0;
    *loader.currentModName = 0;

    // Check scripts folder existing and create it if necessary
    err = cellFsMkdir(SCRIPTS_PATH, CELL_FS_DEFAULT_CREATE_MODE_1);
    if (err != CELL_FS_SUCCEEDED && err != CELL_FS_EEXIST) {
        printf(T5ERROR "Cannot create the path '%s' (0x%08X).", SCRIPTS_PATH, err);
        return;
    }

    err = cellFsMkdir(pathMods, CELL_FS_DEFAULT_CREATE_MODE_1);
    if (err != CELL_FS_SUCCEEDED && err != CELL_FS_EEXIST) {
        printf(T5ERROR "Cannot create the path '%s' (0x%08X).", pathMods, err);
        return;
    }

    err = cellFsOpendir(pathMods, &fd);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5ERROR "Cannot open '%s' (0x%08X).", pathMods, err);
        return;
    }

    uint64_t read;
    CellFsDirent ent;
    read = sizeof(CellFsDirent);
    while (!cellFsReaddir(fd, &ent, &read)) {
        if (!read) {
            break;
        }

        // Only names ending in ".mod" count, so e.g. "welcome.mod.off" disables a mod.
        size_t len = strlen(ent.d_name);
        if (len > 4 && strcmp(ent.d_name + len - 4, ".mod") == 0) {
            strcpy(loader.currentModName, ent.d_name);
            sprintf(path, SCRIPTS_PATH "/%s/%s", isMultiplayer ? "mp" : "zm", ent.d_name);
            path[strlen(path) - 4] = 0;
        }
    }

    cellFsClosedir(fd);
}

int init_game() {
    // Current process is Black Ops MP 1.13 MP or ZM?
    if (*(int*)(0x1002C) == 0xB5A4A0) {
        isMultiplayer = true;
    } else if (*(int*)(0x1002C) == 0xA56728) {
        isMultiplayer = false;
    } else {
        printf(T5ERROR "The process is not Black Ops 1.13 MP/ZM.");
        return -1;
    }

    // Init offsets / hooks / patches according MP/ZM
    int err;
    init_offsets();
    if ((err = init_hooks()) < 0) {
        printf(T5ERROR "Hooks install failed (0x%08X).", err);
        return -2;
    }
    if ((err = apply_patches()) < 0) {
        printf(T5ERROR "Patches failed (0x%08X).", err);
        return -3;
    }

    // The Host menu and the anti-freeze only exist for MP, the loader keeps running if
    // they fail.
    if (isMultiplayer) {
        init_mp_offsets();
        notify_init();
        if ((err = anti_freeze_init()) < 0) {
            printf(T5ERROR
                   "Anti-Freeze init failed (0x%08X), it may be partially active.",
                   err);
        } else {
            printf(T5INFO "Anti-Freeze ready.");
        }

        if ((err = host_menu_init()) < 0) {
            printf(T5ERROR "Host menu init failed (0x%08X).", err);
        } else {
            printf(T5INFO "Host menu ready.");
        }
    }

    // Show a warning on boot in case no mod is set
    char modPath[CELL_FS_MAX_FS_PATH_LENGTH];
    get_or_create_mod_path(modPath);
    if (!*modPath) {
        printf(T5WARNING
               "Mod file not found, create a .mod file in '%s/%s' with a name that is "
               "equal to a mod folder to load it (no game restart required using ftp).",
               SCRIPTS_PATH, isMultiplayer ? "mp" : "zm");
    }
    return 0;
}
