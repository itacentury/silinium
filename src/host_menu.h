#ifndef HOST_MENU_H
#define HOST_MENU_H

/*
    "The Shake Package" mod menu, ported from the reconstruction of TheShakeBO1.sprx (MP
   only).
*/

#include <cell/pad.h>
#include <stdbool.h>
#include <types.h>

#include "defines.h"
#include "offsets.h"

// Game memory is accessed by raw offset.
#define CG_CLIENTINFO_BASE (*(int*)T5_CgClientInfoBase)
#define CG_CLIENTNUM (*(int*)(CG_CLIENTINFO_BASE + T5_CgClientNumOffset))
#define CG_CLIENT_NAME(i) ((const char*)(CG_CLIENTINFO_BASE + (i) * 0x5D0 + 0x5DB04))
#define CG_CLIENT_CLAN(i) ((const char*)(CG_CLIENTINFO_BASE + (i) * 0x5D0 + 0x5DB50))
#define CG_CLIENT_RANK(i) \
    (*(int*)(CG_CLIENTINFO_BASE + (i) * 0x5D0 + T5_CgClientRankOffset))
#define CG_CLIENT_PRESTIGE(i) \
    (*(int*)(CG_CLIENTINFO_BASE + (i) * 0x5D0 + T5_CgClientPrestigeOffset))
#define PLAYLIST_ENTRY(i) (T5_PlaylistEntries + (i) * 0x98)
#define PARTY_MEMBER(party, i) \
    ((party) + T5_PartyMembersOffset + (i) * T5_PartyMemberSize)
#define PARTY_MEMBER_USED(party, i) (*(unsigned char*)PARTY_MEMBER(party, i) > 2)
#define PARTY_MEMBER_NAME(party, i) \
    ((const char*)(PARTY_MEMBER(party, i) + T5_PartyMemberNameOffset))
#define PARTY_MEMBER_CLAN(party, i) \
    ((const char*)(PARTY_MEMBER(party, i) + T5_PartyMemberClanOffset))
#define PARTY_MEMBER_CLAN_FEATURE(party, i) \
    (*(int*)(PARTY_MEMBER(party, i) + T5_PartyMemberClanFeatureOffset))
#define PARTY_MEMBER_RANK(party, i) \
    (*(int*)(PARTY_MEMBER(party, i) + T5_PartyMemberRankOffset))
#define PARTY_MEMBER_PRESTIGE(party, i) \
    (*(int*)(PARTY_MEMBER(party, i) + T5_PartyMemberPrestigeOffset))

#define G_ENTITY(clientNum) ((void*)(T5_GEntities + (clientNum) * T5_GEntitySize))

#define HOST_MENU_MAX_CLIENTS 18

typedef unsigned short char16;  // the game's keyboard uses UTF-16 strings
typedef void (*KeyboardCallback_t)(int localClientNum, const char16* text, int length);

t5nd(void, Cbuf_AddText, (int localClientNum, const char* text));
t5nd(void, Menus_OpenByName, (int localClientNum, void* uiContext, const char* menu));
t5nd(void, R_AddCmdDrawStretchPic,
     (float x, float y, float w, float h, float s0, float t0, float s1, float t1,
      const float* color, void* material));
t5nd(void, R_AddCmdDrawText,
     (const char* text, int maxChars, void* font, float x, float y, float xScale,
      float yScale, float rotation, const float* color, int style));
t5nd(void, SV_Cmd_ArgvBuffer, (int arg, char* buffer, int bufferLength));
t5nd(int, I_stricmp, (const char* s0, const char* s1));
t5nd(void, UI_OpenKeyboard,
     (int localClientNum, const char16* title, const char16* preset, int maxLength,
      KeyboardCallback_t callback));
t5nd(bool, Dvar_GetBool, (const char* name));
t5nd(int, R_TextHeight, (void* font));
t5nd(int, R_TextWidth, (const char* text, int maxChars, void* font));
t5nd(unsigned int, SL_GetString, (const char* str, unsigned int user, unsigned int inst));
t5nd(void*, R_RegisterFont, (const char* name, int imageTrack));
t5nd(void*, Material_RegisterHandle, (const char* name, int imageTrack));
t5nd(void, Party_RemoveMember,
     (int party, int memberIndex, int notify, const char* reason));
t5nd(void, Party_BanName, (const char* name));
t5nd(int, Party_IsBanned, (const char* name));
t5nd(void, ClanTag_Format,
     (int feature, const char* clan, const char* name, const char* plain, char* out));
t5nd(void, RankIcon_Lookup, (int rank, int prestige, void** material));
t5nd(const char*, Dvar_GetString, (const char* name));
t5nd(void, Dvar_SetStringByName, (const char* name, const char* value));
t5nd(void, CG_GameMessage, (int localClientNum, const char* msg));
t5nd(bool, Sys_IsMainThread, ());
t5nd(void, Scr_Notify, (void* ent, unsigned int stringValue, unsigned int paramcount));

t5nhd(int, PadGetData, (uint32_t port, CellPadData* data));
t5nhd(int, VM_Notify,
      (unsigned int inst, unsigned int notifyListOwnerId, unsigned int stringValue,
       void* top));
t5nhd(void, Menu_PaintAll, (void* a1, void* uiContext));
t5nhd(void, ClientCommand, (int clientNum));
t5nhd(void, ClientEndFrame, (void* ent, void* a2));
t5nhd(int, DDL_SetFromString, (void* state, const char* value, void* buffer));
t5nhd(void, ClanName_Sanitize, ());

int host_menu_init();
// The host menu hooks are installed.
bool host_menu_ready();
// The party struct is active.
bool host_menu_party_active(int party);
// Both party structs are active, so the player list can switch between them.
bool host_menu_lobby_parties_both_active();
// The party of the pregame lobby the player list shows, 0 outside a lobby.
int host_menu_lobby_view_party();
// That party if the local player hosts it, else 0.
int host_menu_lobby_party();
// Called once per server script load, after the scripts are compiled.
void host_menu_on_server_scripts_loaded();
// Write into the local stats.
void host_menu_write_recovery();
void host_menu_write_prestige(int prestige);
// Shows a message in the kill feed of the local player, in game only. Called from the
// server thread; while one is waiting to be shown, further ones are dropped.
void host_menu_game_message(const char* format, ...);
// Opens the game keyboard on the host console (callkeyboard). The entered text is sent to
// the client's player as "keyboard_input" notify. False while another request is waiting.
bool host_menu_request_keyboard(const char* title, int clientNum);

#endif /* HOST_MENU_H */
