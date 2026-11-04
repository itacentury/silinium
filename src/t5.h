#ifndef T5_H
#define T5_H

#include <stdarg.h>
#include <stdbool.h>
#include <types.h>

#include "defines.h"

bool isMultiplayer;

GSCLoader loader;

t5nd(dvar_s*, Dvar_FindVar, (const char* name));
t5nd(int, Scr_GetNumParam, (int scriptInstance));
t5nd(char*, Scr_GetString, (unsigned int index, scriptInstance_t scriptInstance));
t5nd(int, Scr_GetInt, (unsigned int index, scriptInstance_t scriptInstance));
t5nd(void, Scr_AddInt, (int value, scriptInstance_t scriptInstance));
t5nd(void, Scr_AddString, (const char* value, scriptInstance_t scriptInstance));
t5nd(void*, Hunk_AllocateTempMemoryHigh, (int size));

t5nhd(char*, Scr_LoadRawFile_FastFile,
      (scriptInstance_t inst, const char* filename, const char* extFilename,
       const char* codePos, bool archive));
t5nhd(void, Scr_GetChecksum, (scrChecksum_t * vmChecksum, scriptInstance_t inst));
t5nhd(popd32, Scr_GetFunction, (const char** pName, int* type));

void get_or_create_mod_path(char* path);
int apply_patches();
int init_game();

#endif
