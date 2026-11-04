#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <sys/prx.h>
#include <types.h>

// Fixing the define only to remove the annoying intellisense issue..
#ifdef SYS_MODULE_INFO_SECTION
#undef SYS_MODULE_INFO_SECTION
#define SYS_MODULE_INFO_SECTION \
    __attribute__((section(SYS_MODINFO_SECTION_NAME ", \"a\",@progbits#")))
#endif

int sys_dbg_process_write(uint64_t address, const void* data, size_t size);

typedef struct InstructionPatch {
    int address;
    int original;
    int patched;
} InstructionPatch;

// Writes the patched (apply) or the original instruction, but only over the other one of
// the two. Anything else at the address (wrong offset, other game version) is left alone
// and logged. Returns the negative sys_dbg_process_write error on a failed write,
// otherwise 0: a written, already-applied and skipped patch all return 0, so a < 0 check
// (t5.c) only catches failed writes, not skips.
int apply_instruction_patch(const InstructionPatch* patch, bool apply);
size_t get_file_size(char* filePath);
void hex_str_to_padded_hex_str(char* out, char* hexStr);
void hex_str_to_buffer(char* out, char* hexStr, size_t hexLen);
int hex_str_to_int32(char* hexStr, size_t hexLen);

#endif /* UTILS_H */