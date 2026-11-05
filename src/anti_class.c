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
// Returns whether the body was invalid and has been reset.
static bool fix_class_body(uint8_t* stats, int slot) {
    int bit = class_body_bit(slot);
    int body = read_bits(stats, bit, T5_Stats_BodyItemBits);
    if (is_body_item(body) || (slot >= CLASS_COUNT && body == 0)) {
        return false;
    }
    write_bits(stats, bit, T5_Stats_BodyItemBits, T5_BodyItemDefault);
    return true;
}

// Fixes every class, returns whether any was fixed.
static bool fix_classes(uint8_t* stats) {
    bool fixed = false;
    for (int slot = 0; slot < 2 * CLASS_COUNT; slot++) {
        if (fix_class_body(stats, slot)) {
            fixed = true;
        }
    }
    return fixed;
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

// A free slot keeps the stats of its last client.
static bool client_in_use(int clientNum) {
    return CG_CLIENTINFO_BASE && *CG_CLIENT_NAME(clientNum);
}

static void check_client_classes(int clientNum, int client) {
    if (!fix_classes(CLIENT_STATS(client))) {
        return;
    }

    char name[40];
    format_client_name(name, sizeof(name), clientNum);
    printf(T5INFO "Fixed a godmode class of %s (client %d).", name, clientNum);
    host_menu_game_message("^1%s^7: godmode class fixed", name);
}

// Checks a client's classes once its last stats chunk is in, before getdstat can read
// them, and again after every later packet: a client might re-send a chunk after the fix
// (unverified whether the game accepts it once the mask is complete). All class bodies
// are in chunk 0 and a clean class is silent, so the re-check is cheap.
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
        // The name can still be empty when the mask just became complete.
        bool isComplete = CLIENT_STATS_MASK(CLIENT(i)) == T5_ClientStatsMaskComplete;
        if (isComplete && (!complete[i] || client_in_use(i))) {
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
        bool isComplete = CLIENT_STATS_MASK(CLIENT(i)) == T5_ClientStatsMaskComplete;
        if (isComplete && client_in_use(i)) {
            check_client_classes(i, CLIENT(i));
        }
    }
}

int anti_class_init() {
    t5nmhi(SV_ReceiveStats);
    return cs_hook_install(SV_ReceiveStats, CS_HOOK_TYPE_CTR);
}
