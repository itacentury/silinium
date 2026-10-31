#ifndef DEFINES_H
#define DEFINES_H

#include <stdbool.h>

// Macros to automate naming/setting variables for natives/hooks
#define t5nd(ret_type, name, args)    \
    typedef ret_type(*name##_t) args; \
    uint64_t name##_opd;              \
    name##_t name;

#define t5ni(name)                                                                                                    \
    name##_opd = isMultiplayer ? ((uint64_t)T5_##name << 32) | T5_TOC : ((uint64_t)T5_##name##_ZM << 32) | T5_TOC_ZM; \
    name = (name##_t) & name##_opd;

#define t5nhd(ret_type, name, args)   \
    typedef ret_type(*name##_t) args; \
    uint64_t name##_opd;              \
    name##_t name;                    \
    name##_t name##_Trampoline;       \
    ret_type name##_Hook args;

#define t5nhi(name)                                                                                                   \
    name##_opd = isMultiplayer ? ((uint64_t)T5_##name << 32) | T5_TOC : ((uint64_t)T5_##name##_ZM << 32) | T5_TOC_ZM; \
    name = (name##_t) & name##_opd;                                                                                   \
    name##_Trampoline = 0;

// MP only natives/hooks (no _ZM offset)
#define t5nmi(name)                                  \
    name##_opd = ((uint64_t)T5_##name << 32) | T5_TOC; \
    name = (name##_t) & name##_opd;

#define t5nmhi(name) \
    t5nmi(name)      \
    name##_Trampoline = 0;

#define t5o(name) (isMultiplayer ? T5_##name : T5_##name##_ZM)

#define T5INFO "[T5] Info: "
#define T5WARNING "[T5] Warning: "
#define T5ERROR "[T5] ERROR: "

#define SCRIPTS_PATH "/dev_hdd0/tmp/Silinium"

// Game structs
typedef union DvarValue
{
    bool enabled;
    int integer;
    uint32_t unsignedInt;
    int64_t integer64;
    uint64_t unsignedInt64;
    float value;
    float vector[4];
    const char *string;
    char color[4];
} DvarValue;

typedef enum scriptInstance_t
{
    SCRIPTINSTANCE_SERVER = 0,
    SCRIPTINSTANCE_CLIENT = 1,
    SCRIPT_INSTANCE_MAX = 2
} scriptInstance_t;

typedef enum dvarType_t
{
    DVAR_TYPE_BOOL = 0x0,
    DVAR_TYPE_FLOAT = 0x1,
    DVAR_TYPE_FLOAT_2 = 0x2,
    DVAR_TYPE_FLOAT_3 = 0x3,
    DVAR_TYPE_FLOAT_4 = 0x4,
    DVAR_TYPE_INT = 0x5,
    DVAR_TYPE_ENUM = 0x6,
    DVAR_TYPE_STRING = 0x7,
    DVAR_TYPE_COLOR = 0x8,
    DVAR_TYPE_INT64 = 0x9,
    DVAR_TYPE_LINEAR_COLOR_RGB = 0xA,
    DVAR_TYPE_COLOR_XYZ = 0xB,
    DVAR_TYPE_COUNT = 0xC
} dvarType_t;

typedef struct dvar_s
{
    const char *name;
    const char *description;
    int hash;
    unsigned int flags;
    dvarType_t type;
    bool modified;
    bool loadedFromSaveGame;
    DvarValue current;
    DvarValue latched;
    DvarValue reset;
    DvarValue saved;
    char domain[10];
    struct dvar_s *hashNext;
} dvar_s;

// Customs
typedef struct opd32
{
    void *function;
    int toc;
} opd32;
typedef struct opd32 *popd32;

typedef struct scrChecksum_t
{
    int checksum;
    int programLen;
    int substract;
} scrChecksum_t; // 0xC (unknown struct not in pdb)

typedef struct GSCLoader
{
    char currentModName[256];
} GSCLoader;

#endif