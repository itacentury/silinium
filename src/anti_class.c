#include "anti_class.h"

#include <stdio.h>

#include "cshook.h"
#include "host_menu.h"
#include "host_menu_ui.h"
#include "t5.h"

#define CLIENT(i) (*(int*)T5_SvsClients + (i) * T5_ClientSize)
#define CLIENT_STATS(client) ((uint8_t*)((client) + T5_ClientStatsOffset))
#define CLIENT_STATS_MASK(client) (*(uint64_t*)((client) + T5_ClientStatsMaskOffset))

// Custom classes, then the same number of prestige classes.
#define CLASS_COUNT 5

// Ordered by severity, so the result of several fields is the highest one.
typedef enum ClassCheck { CLASS_CLEAN, CLASS_FIXED, CLASS_UNFIXABLE } ClassCheck;

// DDL fields are packed from the lowest bit of each byte up.
static int read_bits(const uint8_t* buffer, int bit, int count) {
    int value = 0;
    for (int i = 0; i < count; i++, bit++) {
        value |= ((buffer[bit >> 3] >> (bit & 7)) & 1) << i;
    }
    return value;
}

static void write_bits(uint8_t* buffer, int bit, int count, int value) {
    for (int i = 0; i < count; i++, bit++) {
        uint8_t mask = 1 << (bit & 7);
        if ((value >> i) & 1) {
            buffer[bit >> 3] |= mask;
        } else {
            buffer[bit >> 3] &= ~mask;
        }
    }
}

static bool is_body_item(int item) {
    return item >= T5_BodyItemFirst && item <= T5_BodyItemLast;
}

static int class_body_bit(int slot) {
    if (slot < CLASS_COUNT) {
        return T5_Stats_DdlBit + T5_Stats_CustomClassBodyBit + slot * T5_Stats_ClassBits;
    }
    return T5_Stats_DdlBit + T5_Stats_PrestigeClassBodyBit +
           (slot - CLASS_COUNT) * T5_Stats_ClassBits;
}

// The custom classes are set in every dump, the prestige classes of a player who never
// prestiged may still be zero (unverified), so 0 only counts there as unset.
static ClassCheck fix_class_body(uint8_t* stats, int slot) {
    int bit = class_body_bit(slot);
    int body = read_bits(stats, bit, T5_Stats_BodyItemBits);
    if (is_body_item(body) || (slot >= CLASS_COUNT && body == 0)) {
        return CLASS_CLEAN;
    }
    write_bits(stats, bit, T5_Stats_BodyItemBits, T5_BodyItemDefault);
    return is_body_item(read_bits(stats, bit, T5_Stats_BodyItemBits)) ? CLASS_FIXED
                                                                      : CLASS_UNFIXABLE;
}

static ClassCheck fix_classes(uint8_t* stats) {
    ClassCheck result = CLASS_CLEAN;
    for (int slot = 0; slot < 2 * CLASS_COUNT; slot++) {
        ClassCheck check = fix_class_body(stats, slot);
        if (check > result) {
            result = check;
        }
    }
    return result;
}

static int max_clients() {
    dvar_s* maxClients = *(dvar_s**)T5_SvMaxClients;
    if (!maxClients) {
        return 0;
    }
    int count = maxClients->current.integer;
    if (count < 0) {
        return 0;
    }
    return count < HOST_MENU_MAX_CLIENTS ? count : HOST_MENU_MAX_CLIENTS;
}

// The client info can still be empty while a client connects.
static void format_client_name(char* out, int size, int clientNum) {
    if (CG_CLIENTINFO_BASE && *CG_CLIENT_NAME(clientNum)) {
        snprintf(out, size, "%s", CG_CLIENT_NAME(clientNum));
        return;
    }
    snprintf(out, size, "Client %d", clientNum);
}

static void check_client_classes(int clientNum, int client) {
    ClassCheck result = fix_classes(CLIENT_STATS(client));
    if (result == CLASS_CLEAN) {
        return;
    }

    char name[40];
    format_client_name(name, sizeof(name), clientNum);
    if (result == CLASS_FIXED) {
        printf(T5INFO "Fixed a godmode class of %s (client %d).", name, clientNum);
        host_menu_game_message("^1%s^7: godmode class fixed", name);
        return;
    }

    printf(T5WARNING "Could not fix the godmode class of %s (client %d), kicking.", name,
           clientNum);
    host_menu_game_message("^1%s^7: godmode class, kicked", name);
    char command[32];
    snprintf(command, sizeof(command), "clientkick %i\n", clientNum);
    Cbuf_AddText(0, command);
}

// Checks a client's classes once its last stats chunk is in, before getdstat can read
// them.
void SV_ReceiveStats_Hook(uint64_t from0, uint64_t from1, uint64_t from2, void* msg) {
    if (!hostMenu.antiGodmodeClasses || !*(int*)T5_SvsClients) {
        SV_ReceiveStats_Trampoline(from0, from1, from2, msg);
        return;
    }

    int count = max_clients();
    bool complete[HOST_MENU_MAX_CLIENTS];
    for (int i = 0; i < count; i++) {
        complete[i] = CLIENT_STATS_MASK(CLIENT(i)) == T5_ClientStatsMaskComplete;
    }

    SV_ReceiveStats_Trampoline(from0, from1, from2, msg);

    for (int i = 0; i < count; i++) {
        if (!complete[i] && CLIENT_STATS_MASK(CLIENT(i)) == T5_ClientStatsMaskComplete) {
            check_client_classes(i, CLIENT(i));
        }
    }
}

// The fixed body only reaches later getdstat reads, a player who already spawned with the
// class probably keeps it until the next spawn or class change (unverified).
void anti_class_check_clients() {
    if (!hostMenu.antiGodmodeClasses || !*(int*)T5_SvsClients) {
        return;
    }

    int count = max_clients();
    for (int i = 0; i < count; i++) {
        // A free slot keeps the stats of its last client.
        bool inUse = CG_CLIENTINFO_BASE && *CG_CLIENT_NAME(i);
        if (inUse && CLIENT_STATS_MASK(CLIENT(i)) == T5_ClientStatsMaskComplete) {
            check_client_classes(i, CLIENT(i));
        }
    }
}

int anti_class_init() {
    t5nmhi(SV_ReceiveStats);
    return cs_hook_install(SV_ReceiveStats, CS_HOOK_TYPE_CTR);
}
