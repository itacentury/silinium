#include "anti_god_class.h"

#include <stdio.h>
#include <string.h>

#include "cshook.h"
#include "host_menu.h"
#include "host_menu_ui.h"
#include "t5.h"
#include "utils.h"

#define CLIENT_STATE(client) (*(int*)((client) + T5_ClientStateOffset))
#define CLIENT_STATS(client) ((uint8_t*)((client) + T5_ClientStatsOffset))
#define CLIENT_STATS_MASK(client) (*(uint64_t*)((client) + T5_ClientStatsMaskOffset))
#define CLIENT_USERINFO(client) ((const char*)((client) + T5_ClientUserinfoOffset))

#define ENT_CLIENT(ent) (*(int*)((char*)(ent) + T5_GEntityClientOffset))
#define ENT_FLAGS(ent) (*(int*)((char*)(ent) + T5_GEntityFlagsOffset))
#define ENT_HEALTH(ent) (*(int*)((char*)(ent) + T5_GEntityHealthOffset))
#define ENT_MODEL(ent) (*(uint16_t*)((char*)(ent) + T5_GEntityModelOffset))
#define GCLIENT_HEALTH(gclient) (*(int*)((gclient) + T5_GClientHealthOffset))

// Only for the server instance, the child offset differs for the client one.
#define SERVER_VARIABLE(id) \
    (*(uint32_t*)T5_ScrVarGlob + (T5_VariableChildBegin + (id)) * T5_VariableSize)
#define SERVER_VARIABLE_VALUE(id) \
    (*(uint32_t*)(SERVER_VARIABLE(id) + T5_VariableValueOffset))
#define SERVER_VARIABLE_TYPE(id) \
    (*(uint32_t*)(SERVER_VARIABLE(id) + T5_VariableStatusOffset) & T5_VariableTypeMask)

// Custom classes, then the same number of prestige classes.
#define CLASS_COUNT 5

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
// but there it doesn't count as a godmode class; in a custom class it does.
// Returns whether a godmode body was fixed.
static bool fix_class_body(uint8_t* stats, int slot) {
    int bit = class_body_bit(slot);
    int body = ddl_read_bits(stats, bit, T5_Stats_BodyItemBits);
    if (is_body_item(body)) {
        return false;
    }
    ddl_write_bits(stats, bit, T5_Stats_BodyItemBits, T5_BodyItemDefault);
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

// From the userinfo: the check mostly runs on connect, when the host's cgame client info
// doesn't hold this client yet, it's empty or still has the previous one's name.
static void format_client_name(char* out, int size, int clientNum, int client) {
    userinfo_name(CLIENT_USERINFO(client), out, size);
    if (!*out) {
        snprintf(out, size, "Client %d", clientNum);
    }
}

// A free or dropped slot keeps the stats and mask of its last client until
// SV_DirectConnect clears the whole client_t (memset at 0x3E66D4), so only the server
// state tells whether a player is still there. It is the same gate getdstat uses.
static bool client_in_use(int clientNum) {
    return CLIENT_STATE(SV_CLIENT(clientNum)) >= T5_ClientStateConnected;
}

// Returns whether a class was fixed.
static bool check_client_classes(int clientNum, int client) {
    if (!fix_classes(CLIENT_STATS(client))) {
        return false;
    }

    char name[40];
    format_client_name(name, sizeof(name), clientNum, client);
    printf(T5INFO "Fixed a godmode class of %s (client %d).", name, clientNum);
    host_menu_report_client(clientNum, name, HOST_MENU_REPORT_GODMODE_CLASS);
    return true;
}

// Dead and spectating players have no health left (inferred from PlayerCmd_suicide, which
// zeroes it before player_die).
static bool client_alive(void* ent) { return ENT_CLIENT(ent) && ENT_HEALTH(ent) > 0; }

// The same as the GSC builtin suicide() (PlayerCmd_suicide 0x2CA338): the flags are
// cleared first, so player_die isn't stopped by godmode.
static void kill_client(void* ent) {
    ENT_FLAGS(ent) &= ~T5_GEntityGodmodeFlags;
    ENT_HEALTH(ent) = 0;
    GCLIENT_HEALTH(ENT_CLIENT(ent)) = 0;
    player_die(ent, ent, ent, T5_SuicideDamage, T5_ModSuicide, 0, NULL, 0, 0);
}

static void kill_and_log(int clientNum, void* ent) {
    kill_client(ent);
    if (ENT_HEALTH(ent) > 0) {
        printf(T5WARNING "player_die didn't kill client %d.", clientNum);
        return;
    }
    printf(T5INFO "Killed client %d to remove the godmode class.", clientNum);
    char name[40];
    format_client_name(name, sizeof(name), clientNum, SV_CLIENT(clientNum));
    host_menu_report_client(clientNum, name, HOST_MENU_REPORT_GODMODE_KILL);
}

// player_die needs a DObj, which a godmode class never gets. A model name could be one
// that isn't precached, which G_ModelIndex turns into a Com_Error, so the model index of
// a player who has a DObj is copied. The team may not match, the player dies right after.
// The game builds the DObj from it in a later frame. Returns whether a model was copied.
static bool copy_model(int clientNum, void* ent, int count) {
    for (int i = 0; i < count; i++) {
        void* other = G_ENTITY(i);
        if (i == clientNum || !client_in_use(i) || !ENT_CLIENT(other) ||
            !ENT_MODEL(other) || !Com_GetServerDObj(i)) {
            continue;
        }
        ENT_MODEL(ent) = ENT_MODEL(other);
        return true;
    }
    return false;
}

// _class.gsc caches the classes it read at the first spawn in self.custom_class and only
// reads the stats again while self.cac_initialized is undefined. Without this, every
// respawn keeps the undefined body, so get_default_head stops the loadout before the
// model is set and get_armor_damage fails on body shots (half god mode). Object fields
// are keyed by canonical strings, which can't be looked up by name after the load, so
// the id is taken from the compile. Scr_BeginLoadScripts clears the canonical strings, so
// the id is only valid for the load that compiled it.
typedef struct {
    const char* string;
    unsigned int name;
    bool seen;
} ScriptField;

enum { FIELD_CAC_INITIALIZED, FIELD_CAC_BODY_TYPE, FIELD_COUNT };

static ScriptField scriptFields[FIELD_COUNT] = {
    [FIELD_CAC_INITIALIZED] = {"cac_initialized"},
    [FIELD_CAC_BODY_TYPE] = {"cac_body_type"},
};

static ScriptField* find_script_field(unsigned int inst, const char* str) {
    if (inst != SCRIPTINSTANCE_SERVER || !str) {
        return NULL;
    }
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (strcmp(str, scriptFields[i].string) == 0) {
            return &scriptFields[i];
        }
    }
    return NULL;
}

unsigned int SL_TransferToCanonicalString_Hook(unsigned int inst,
                                               unsigned int stringValue) {
    // Before the trampoline, while the compiler still holds its ref.
    ScriptField* field = find_script_field(inst, SL_ConvertToString(stringValue, inst));
    unsigned int name = SL_TransferToCanonicalString_Trampoline(inst, stringValue);
    if (field) {
        field->name = name;
        field->seen = true;
    }
    return name;
}

// The player's script object, 0 before the first spawn.
static unsigned int client_object(int clientNum) {
    return FindEntityId(SCRIPTINSTANCE_SERVER, clientNum, CLASS_NUM_ENTITY, 0);
}

static void reset_class_cache(int clientNum) {
    unsigned int name = scriptFields[FIELD_CAC_INITIALIZED].name;
    if (!name) {
        printf(T5WARNING "cac_initialized wasn't compiled in this script load.");
        return;
    }
    unsigned int id = client_object(clientNum);
    if (!id) {
        return;
    }
    if (!FindVariable(SCRIPTINSTANCE_SERVER, id, name)) {
        printf(T5WARNING "Client %d has no class cache to reset.", clientNum);
        return;
    }
    RemoveVariable(SCRIPTINSTANCE_SERVER, id, name);
    printf(T5INFO "Reset the class cache of client %d.", clientNum);
}

// The bodies that register their level.cac_functions in the PC mpbody/*.gsc.
static const char* const bodyTypes[] = {
    "standard_mp",          "camo_mp",     "utility_mp",
    "ordnance_disposal_mp", "hardened_mp", "body_default_mp",
};

static bool is_body_type(const char* body) {
    for (size_t i = 0; i < sizeof(bodyTypes) / sizeof(*bodyTypes); i++) {
        if (strcmp(body, bodyTypes[i]) == 0) {
            return true;
        }
    }
    return false;
}

// self.cac_body_type as a string, NULL when it is missing or not a string.
static const char* active_body_type(unsigned int object, unsigned int name) {
    unsigned int field = FindVariable(SCRIPTINSTANCE_SERVER, object, name);
    if (!field || SERVER_VARIABLE_TYPE(field) != T5_VarString) {
        return NULL;
    }
    return SL_ConvertToString(SERVER_VARIABLE_VALUE(field), SCRIPTINSTANCE_SERVER);
}

// The loadout sets self.cac_body_type = self.custom_class[n]["body"], which comes from
// level.tbl_armor[body]. That table also holds the heads, so a godmode body is a defined
// head string rather than undefined (on the console the field was found with a defined
// type; which row 0x72 is, is inferred, unverified). get_default_head then has no
// function for it and the loadout stops before the model is set. So a living player whose
// body is not one of the registered bodies plays a godmode class right now. A DObj
// doesn't tell: a half god mode player is visible. Without the field id every fixed
// player counts as godmode, so nobody who needs the kill is skipped.
static bool active_class_godmode(int clientNum) {
    unsigned int name = scriptFields[FIELD_CAC_BODY_TYPE].name;
    if (!name) {
        printf(T5WARNING "cac_body_type wasn't compiled in this script load.");
        return true;
    }
    unsigned int object = client_object(clientNum);
    if (!object) {
        return true;
    }
    const char* body = active_body_type(object, name);
    printf(T5INFO "Client %d has the body %s.", clientNum, body ? body : "(none)");
    return !body || !is_body_type(body);
}

// Server frames a queued kill waits for the DObj, about a second at sv_fps 20.
#define KILL_WAIT_FRAMES 20
static int killWait[HOST_MENU_MAX_CLIENTS];

void anti_god_class_reset() {
    memset(killWait, 0, sizeof(killWait));
    // A load that didn't compile a field leaves an id that may name another string now.
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (!scriptFields[i].seen) {
            scriptFields[i].name = 0;
        }
        scriptFields[i].seen = false;
    }
}

void anti_god_class_client_frame(void* ent) {
    int clientNum = G_ENTITY_NUM(ent);
    if (clientNum < 0 || clientNum >= HOST_MENU_MAX_CLIENTS || !killWait[clientNum]) {
        return;
    }
    // With the option off the stats hook skips the connect transition below, so a kill
    // queued before turning it off is dropped here instead of reaching the next client.
    if (!hostMenu.antiGodClasses || !SV_CLIENTS || !client_in_use(clientNum) ||
        !client_alive(ent)) {
        killWait[clientNum] = 0;
        return;
    }
    if (Com_GetServerDObj(clientNum)) {
        killWait[clientNum] = 0;
        kill_and_log(clientNum, ent);
        return;
    }
    if (--killWait[clientNum] == 0) {
        printf(T5WARNING "Client %d got no DObj, keeps the godmode until the next spawn.",
               clientNum);
    }
}

// Checks a client's classes once its last stats chunk is in, before getdstat can read
// them, and again after every later packet: the game only skips a packet when the mask
// bits below the client's chunk count are set, so a count above 33 still overwrites a
// chunk after the fix. All class bodies are in chunk 0 and a clean class is silent, so
// the re-check is cheap. Every re-check fixes, but host_menu_report_client limits the
// kill feed report, so a client resending godmode chunks can't flood the message queue.
// The transition catches every new client, the connect clears the mask of the slot
// (memset at 0x3E66D4). The count only bypasses the skip, the chunk index stays at 32 or
// below, so the mask can't grow past complete.
void SV_ReceiveStats_Hook(uint64_t from0, uint64_t from1, uint64_t from2, void* msg) {
    if (!hostMenu.antiGodClasses || !SV_CLIENTS) {
        SV_ReceiveStats_Trampoline(from0, from1, from2, msg);
        return;
    }

    int count = max_clients();
    bool complete[HOST_MENU_MAX_CLIENTS];
    for (int i = 0; i < count; i++) {
        complete[i] = CLIENT_STATS_MASK(SV_CLIENT(i)) == T5_ClientStatsMaskComplete;
    }

    SV_ReceiveStats_Trampoline(from0, from1, from2, msg);

    for (int i = 0; i < count; i++) {
        // When the state is set relative to the first stats packet is unverified, but a
        // mask that just became complete belongs to a live sender found by its address.
        bool isComplete = CLIENT_STATS_MASK(SV_CLIENT(i)) == T5_ClientStatsMaskComplete;
        // A new client in the slot: a kill queued for the previous one, who left during
        // the wait, would otherwise hit this one on the first spawn.
        if (isComplete && !complete[i]) {
            killWait[i] = 0;
        }
        if (isComplete && (!complete[i] || client_in_use(i))) {
            check_client_classes(i, SV_CLIENT(i));
        }
    }
}

// The fixed body only reaches later getdstat reads: the loadout is read on spawn, so a
// player who already spawned with the class keeps it until the next spawn. Such a player
// is killed to respawn with the fixed class; one who plays a clean class and only had a
// godmode class in another slot is left alive. Only here, the stats hook fixes before
// the spawn. Calling player_die from ClientEndFrame of another client is assumed to be as
// safe as the script's suicide() in the same game frame (unverified).
void anti_god_class_check_clients() {
    if (!hostMenu.antiGodClasses || !SV_CLIENTS) {
        return;
    }

    int count = max_clients();
    for (int i = 0; i < count; i++) {
        bool isComplete = CLIENT_STATS_MASK(SV_CLIENT(i)) == T5_ClientStatsMaskComplete;
        if (!isComplete || !client_in_use(i) || !check_client_classes(i, SV_CLIENT(i))) {
            continue;
        }
        // Dead and spectating players respawn from the cache as well.
        reset_class_cache(i);
        void* ent = G_ENTITY(i);
        if (!client_alive(ent)) {
            continue;
        }
        if (!active_class_godmode(i)) {
            printf(T5INFO "Client %d plays a clean class, fixed for the next spawn.", i);
            continue;
        }
        if (Com_GetServerDObj(i)) {
            kill_and_log(i, ent);
        } else if (copy_model(i, ent, count)) {
            killWait[i] = KILL_WAIT_FRAMES;
            printf(T5INFO "Gave client %d a model, killing it once it has a DObj.", i);
        } else {
            printf(T5WARNING
                   "No model to copy, client %d keeps the godmode until the next "
                   "spawn.",
                   i);
        }
    }
}

static bool installed;

bool anti_god_class_available() { return installed; }

int anti_god_class_init() {
    t5nmi(player_die);
    t5nmi(Com_GetServerDObj);
    t5nmi(FindVariable);
    t5nmi(RemoveVariable);
    t5nmi(SL_ConvertToString);
    t5nmhi(SL_TransferToCanonicalString);
    t5nmhi(SV_ReceiveStats);
    int err = cs_hook_install(SL_TransferToCanonicalString, CS_HOOK_TYPE_CTR);
    if (err) {
        return err;
    }
    if ((err = cs_hook_install(SV_ReceiveStats, CS_HOOK_TYPE_CTR))) {
        return err;
    }
    installed = true;
    return 0;
}
