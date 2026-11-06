#include "anti_god_class.h"

#include <stdio.h>

#include "cshook.h"
#include "host_menu.h"
#include "host_menu_ui.h"
#include "t5.h"

#define CLIENT(i) (*(int*)T5_SvsClients + (i) * T5_ClientSize)
#define CLIENT_STATE(client) (*(int*)((client) + T5_ClientStateOffset))
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

// The prestige classes of a player who never prestiged may still be zero (unverified).
// Zero is reset as well, since Prestige Unlocks lets such a player pick these classes,
// but it doesn't count as a godmode class. Returns whether a godmode body was fixed.
static bool fix_class_body(uint8_t* stats, int slot) {
    int bit = class_body_bit(slot);
    int body = read_bits(stats, bit, T5_Stats_BodyItemBits);
    if (is_body_item(body)) {
        return false;
    }
    write_bits(stats, bit, T5_Stats_BodyItemBits, T5_BodyItemDefault);
    return slot < CLASS_COUNT || body != 0;
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
    dvar_s* maxClients = Dvar_FindVar("sv_maxclients");
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

// A free or dropped slot keeps the stats and mask of its last client until
// SV_DirectConnect clears the whole client_t (memset at 0x3E66D4), so only the server
// state tells whether a player is still there. It is the same gate getdstat uses.
static bool client_in_use(int clientNum) {
    return CLIENT_STATE(CLIENT(clientNum)) >= T5_ClientStateConnected;
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
// them, and again after every later packet: the game only skips a packet when the mask
// bits below the client's chunk count are set, so a count above 33 still overwrites a
// chunk after the fix. All class bodies are in chunk 0 and a clean class is silent, so
// the re-check is cheap. The transition catches every new client, the connect clears the
// mask of the slot (memset at 0x3E66D4). A mask with a bit above the 33rd is not complete
// for getdstat either (exact compare), so such a client needs no check.
void SV_ReceiveStats_Hook(uint64_t from0, uint64_t from1, uint64_t from2, void* msg) {
    if (!hostMenu.antiGodClasses || !*(int*)T5_SvsClients) {
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
        // When the state is set relative to the first stats packet is unverified, but a
        // mask that just became complete belongs to a live sender found by its address.
        bool isComplete = CLIENT_STATS_MASK(CLIENT(i)) == T5_ClientStatsMaskComplete;
        if (isComplete && (!complete[i] || client_in_use(i))) {
            check_client_classes(i, CLIENT(i));
        }
    }
}

// The fixed body only reaches later getdstat reads, a player who already spawned with the
// class probably keeps it until the next spawn or class change (unverified).
void anti_god_class_check_clients() {
    if (!hostMenu.antiGodClasses || !*(int*)T5_SvsClients) {
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

int anti_god_class_init() {
    t5nmhi(SV_ReceiveStats);
    return cs_hook_install(SV_ReceiveStats, CS_HOOK_TYPE_CTR);
}
