#include "contracts_dump.h"

#include <cell/cell_fs.h>
#include <stdio.h>

#include "cshook.h"

#define CONTRACTS_DUMP_PATH SCRIPTS_PATH "/contracts.info"

// The buffer is zeroed before the inflate, but bound the scan in case the file fills it.
static uint64_t text_length(const char* text) {
    uint64_t length = 0;
    while (length < T5_ContractsFileMax && text[length]) {
        length++;
    }
    return length;
}

static void dump_contracts(const char* text) {
    uint64_t size = text_length(text);
    int fd;
    CellFsErrno err =
        cellFsOpen(CONTRACTS_DUMP_PATH,
                   CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &fd, NULL, 0);
    if (err != CELL_FS_SUCCEEDED) {
        printf(T5WARNING "Contracts: cannot open '%s' (0x%08X).", CONTRACTS_DUMP_PATH,
               err);
        return;
    }

    uint64_t written = 0;
    err = cellFsWrite(fd, text, size, &written);
    cellFsClose(fd);
    if (err != CELL_FS_SUCCEEDED || written != size) {
        printf(T5WARNING "Contracts: writing '%s' failed (0x%08X).", CONTRACTS_DUMP_PATH,
               err);
        return;
    }
    printf(T5INFO "Contracts: wrote %u bytes to '%s'.", (unsigned int)size,
           CONTRACTS_DUMP_PATH);
}

void Contracts_ParseFile_Hook(const char* text) {
    if (text) {
        dump_contracts(text);
    }
    Contracts_ParseFile_Trampoline(text);
}

int contracts_dump_init() {
    t5nmhi(Contracts_ParseFile);
    return cs_hook_install(Contracts_ParseFile, CS_HOOK_TYPE_CTR);
}
