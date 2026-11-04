#include "utils.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stdio.h>
#include <string.h>
#include <sys/process.h>
#include <sys/tty.h>

#include "defines.h"

int sys_dbg_process_write(uint64_t address, const void* data, size_t size) {
    system_call_4(905, sys_process_getpid(), address, size, (uintptr_t)data);
    return_to_user_prog(int);
}

int sys_dbg_process_read(uint64_t address, void* data, size_t size) {
    system_call_4(904, sys_process_getpid(), address, size, (uintptr_t)data);
    return_to_user_prog(int);
}

int apply_instruction_patch(const InstructionPatch* patch, bool apply) {
    int from = apply ? patch->original : patch->patched;
    int to = apply ? patch->patched : patch->original;
    int current = *(int*)patch->address;
    if (current == to) {
        return 0;
    }
    if (current != from) {
        printf(T5WARNING "Skipped patch at 0x%X: found 0x%08X, expected 0x%08X.",
               patch->address, current, from);
        return 0;
    }
    return sys_dbg_process_write(patch->address, &to, 4);
}

size_t get_file_size(char* filePath) {
    int size = 0;
    CellFsStat fstat;
    CellFsErrno err = cellFsStat(filePath, &fstat);
    if (err != CELL_FS_SUCCEEDED) {
        return err;
    }
    return fstat.st_size;
}

void hex_str_to_padded_hex_str(char* out, char* hexStr) {
    char* outPtr;
    char* tmp = hexStr;

    if (!tmp || !out) {
        return;
    }

    if (tmp[1] == 'x') {
        tmp += 2;
    }

    int hexLen = strlen(tmp);

    if ((hexLen % 2) != 0)  // must be even
    {
        hexLen++;
        out[0] = '0';
        outPtr = &out[1];
    } else {
        outPtr = out;
    }

    strcpy(outPtr, tmp);
}

void hex_str_to_buffer(char* out, char* hexStr, size_t hexLen) {
    if (!out || !hexStr || hexLen < 1) {
        return;
    }

    size_t index = 0;
    while (index < (hexLen * 2)) {
        char c = hexStr[index];
        int value = 0;
        if (c >= '0' && c <= '9') {
            value = (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            value = (10 + (c - 'A'));
        } else if (c >= 'a' && c <= 'f') {
            value = (10 + (c - 'a'));
        } else {
            return;
        }

        out[(index / 2)] += (value << (((index + 1) % 2) * 4));
        index++;
    }
}

int hex_str_to_int32(char* hexStr, size_t hexLen) {
    char out[4];
    memset(out, 0, 4);
    int start = (4 - hexLen);
    if (start < 0) {
        start = 0;
    }

    hex_str_to_buffer((char*)(out + start), hexStr, hexLen);

    return *(int*)(&out);
}
