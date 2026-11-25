#include "friend_party.h"

#include <np.h>
#include <stdio.h>
#include <string.h>
#include <sys/sys_time.h>

#include "cshook.h"
#include "host_menu.h"
#include "host_menu_ui.h"
#include "offsets.h"
#include "utils.h"

#define FRIEND_PARTY_REFRESH_US 2000000
#define FRIEND_PARTY_MAX_FRIENDS 100  // PSN friends list limit

// The game's sceNpBasic imports, named apart from the SDK's declarations. Point to the
// resolved functions, not to the game's stubs (see resolve_import).
t5nd(int, Np_GetFriendListEntryCount, (uint32_t* count));
t5nd(int, Np_GetFriendPresenceByIndex2,
     (uint32_t index, SceNpUserInfo* user, SceNpBasicPresenceDetails2* presence,
      uint32_t options));

static bool importsResolved;
// Written on the main thread, read by the hook while drawing; a stale byte only colors
// one frame wrong.
static bool friendFlags[FRIEND_PARTY_MAX_CLIENTS];
static char friendNames[FRIEND_PARTY_MAX_FRIENDS][SCE_NET_NP_ONLINEID_MAX_LENGTH + 1];
static uint32_t friendCount;
static system_time_t lastRefresh;

int CG_IsPartyMember_Hook(int localClientNum, int clientNum) {
    if (CG_IsPartyMember_Trampoline(localClientNum, clientNum)) {
        return 1;
    }
    if (!hostMenu.friendsAsParty || clientNum < 0 ||
        clientNum >= FRIEND_PARTY_MAX_CLIENTS) {
        return 0;
    }
    return friendFlags[clientNum];
}

static void read_friend_names() {
    uint32_t count = 0;
    if (Np_GetFriendListEntryCount(&count) < 0) {
        friendCount = 0;
        return;
    }
    if (count > FRIEND_PARTY_MAX_FRIENDS) {
        count = FRIEND_PARTY_MAX_FRIENDS;
    }

    uint32_t stored = 0;
    for (uint32_t i = 0; i < count; i++) {
        SceNpUserInfo user;
        SceNpBasicPresenceDetails2 presence;
        memset(&user, 0, sizeof(user));
        memset(&presence, 0, sizeof(presence));
        presence.struct_size = sizeof(presence);
        if (Np_GetFriendPresenceByIndex2(i, &user, &presence, 0) < 0) {
            continue;
        }
        // The online id isn't terminated when it uses all 16 characters.
        memcpy(friendNames[stored], user.userId.handle.data,
               SCE_NET_NP_ONLINEID_MAX_LENGTH);
        friendNames[stored][SCE_NET_NP_ONLINEID_MAX_LENGTH] = 0;
        if (friendNames[stored][0]) {
            stored++;
        }
    }
    friendCount = stored;
}

static bool is_friend_name(const char* name) {
    for (uint32_t i = 0; i < friendCount; i++) {
        if (!strcmp(friendNames[i], name)) {
            return true;
        }
    }
    return false;
}

// Matched by name: the in-game name is the PSN online id unless it was changed (e.g. with
// Change Name), a mapping of the NpId to the game's xuid is not reverse engineered.
static void update_friend_flags() {
    for (int i = 0; i < FRIEND_PARTY_MAX_CLIENTS; i++) {
        const char* name = CG_CLIENTINFO_BASE ? CG_CLIENT_NAME(i) : "";
        friendFlags[i] = *name && is_friend_name(name);
    }
}

void friend_party_refresh(bool inGame) {
    if (!importsResolved) {
        return;
    }
    if (!inGame || !hostMenu.friendsAsParty) {
        memset(friendFlags, 0, sizeof(friendFlags));
        lastRefresh = 0;
        return;
    }

    system_time_t now = sys_time_get_system_time();
    if (lastRefresh && now - lastRefresh < FRIEND_PARTY_REFRESH_US) {
        return;
    }
    lastRefresh = now;
    read_friend_names();
    update_friend_flags();
}

int friend_party_init() {
    Np_GetFriendListEntryCount =
        (Np_GetFriendListEntryCount_t)resolve_import(T5_Np_GetFriendListEntryCount);
    Np_GetFriendPresenceByIndex2 =
        (Np_GetFriendPresenceByIndex2_t)resolve_import(T5_Np_GetFriendPresenceByIndex2);
    importsResolved = Np_GetFriendListEntryCount && Np_GetFriendPresenceByIndex2;
    if (!importsResolved) {
        printf(T5WARNING
               "sceNpBasic imports not resolved, Friends As Party is disabled.");
        return 0;
    }

    t5nmhi(CG_IsPartyMember);
    return cs_hook_install(CG_IsPartyMember, CS_HOOK_TYPE_CTR);
}
