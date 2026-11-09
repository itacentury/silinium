#ifndef OFFSETS_H
#define OFFSETS_H

// clang-format off: tables of offsets with trailing comments
// Natives functions
#define T5_Dvar_FindVar 0x4C55A0
#define T5_Dvar_FindVar_ZM 0x452AA0
#define T5_Scr_GetChecksum 0x5BA818
#define T5_Scr_GetChecksum_ZM 0x5A4248
#define T5_Scr_GetFunction 0x310FD8
#define T5_Scr_GetFunction_ZM 0x2BC3E0
#define T5_Scr_GetNumParam 0x5CB210
#define T5_Scr_GetNumParam_ZM 0x5B5088
#define T5_Scr_GetString 0x5D0D68
#define T5_Scr_GetString_ZM 0x5BBCC0
#define T5_Scr_GetInt 0x5D16C0
#define T5_Scr_GetInt_ZM 0x5BCB60
#define T5_Scr_AddInt 0x5DF2A0
#define T5_Scr_AddInt_ZM 0x5C8BF0
#define T5_Scr_AddString 0x5DE750
#define T5_Scr_AddString_ZM 0x5C8840
#define T5_Scr_LoadRawFile_FastFile 0x5AB578
#define T5_Scr_LoadRawFile_FastFile_ZM 0x595110
#define T5_Hunk_AllocateTempMemoryHigh 0x4BD4E0
#define T5_Hunk_AllocateTempMemoryHigh_ZM 0x449E98

// Patches
#define T5_Hunk_AllocateTempMemoryHigh_ErrorCheck 0x4BD584 // bgt to Com_Error on oversized allocations
#define T5_Hunk_AllocateTempMemoryHigh_ErrorCheck_ZM 0x449F3C
#define T5_Dvar_SetVariant_ReadOnlyCheck 0x4C8EC4 // flag 0x40
#define T5_Dvar_SetVariant_ReadOnlyCheck_ZM 0x45690C
#define T5_Dvar_SetVariant_WriteProtectedCheck 0x4C8ED0 // flag 0x10
#define T5_Dvar_SetVariant_WriteProtectedCheck_ZM 0x456918
#define T5_Dvar_SetVariant_CheatCheck 0x4C8EDC // beq before the cheat (0x80) check, replaced by an unconditional b
#define T5_Dvar_SetVariant_CheatCheck_ZM 0x456924
#define T5_Dvar_SetVariant_CheatCheckTarget 0x4C8D4C // TODO: describe the code at this address (per disassembly)
#define T5_Dvar_SetVariant_CheatCheckTarget_ZM 0x456794

// Host menu (MP only), names of the game functions are inferred from usage
#define T5_Cbuf_AddText 0x399CC8
#define T5_Menus_OpenByName 0x45EC08
#define T5_R_AddCmdDrawStretchPic 0x751940
#define T5_R_AddCmdDrawText 0x754410
#define T5_SV_Cmd_ArgvBuffer 0x3997C8
#define T5_I_stricmp 0x4DD8C0 // ClientCommand matches command names with it, folds only A-Z
#define T5_UI_OpenKeyboard 0x56A118
#define T5_Dvar_GetBool 0x4C7BF0
#define T5_R_TextHeight 0x734340
#define T5_R_TextWidth 0x734570
#define T5_SL_GetString 0x5B9A28
#define T5_R_RegisterFont 0x734350
// DB_FindXAssetHeader(material, name, errorIfMissing 1, wait forever): a miss creates a default entry, only for fixed
// names.
#define T5_Material_RegisterHandle 0x7458D8
// (party, memberIndex, reason), name inferred. The stock kick: used by the kickPlayer UI script (0x44ADF8) and wager
// kicks. Sends "%ikicked %s" to the member, then removes every member at its netadr (guests on the same console) with
// 0x208BD8 (party, memberIndex, notify 0, reason). 0x208BD8 with notify 1 would send "endparty <reason>" instead, and
// on LobbyParty also removes the member's XUID from the linked GameParty (LobbyParty + 0x57E0, set at 0x1E8B04).
#define T5_Party_KickMember 0x20DA18
// (party), the xupdatepartystate command (0x1D02C0, with LobbyParty). Returns unless the party is active and the
// local player hosts it (0x1E7068), then copies ui_mapname / ui_gametype into the party (0x1EAE70 / 0x1EAE38) and
// sends partystate, which carries both to the members. Name inferred.
#define T5_Party_UpdateState 0x207770
#define T5_Party_BanName 0x1E7338 // (name), adds to the ban list at LobbyParty + 0x53CC (16 x 0x24) that party joins check
                                  // (0x1E7830 at 0x2134CC), skipped for party id 1 (GameParty). One
                                  // list for both parties, cleared by the party reset 0x1F98B0
                                  // (memset at 0x1F9A48) for either party. Takes the first entry with an empty
                                  // name, in a full list the one with the smallest time, I_strncpyz(.., 0x20)
#define T5_Party_IsBanned 0x1E7830 // (name), 1 if the name is in the ban list Party_BanName fills, 0 for a null or
                                   // empty name. r3 at the join check 0x2134CC is a 0x20 copy of the joining name,
                                   // compared case-sensitively (0x4DCF50)
#define T5_PartyBanListOffset 0x53CC // on LobbyParty, 16 entries: +0x00 name (0x20), +0x20 int from 0x169528C
                                     // (a time, unverified)
#define T5_PartyBanEntrySize 0x24
// (feature, clan, name, plain, out): for the "tagcolor" feature writes "[^%i%s^7]%s" to out, for "tagrot" the same with a
// color that changes over time, otherwise leaves out as it is. The caller fills out with the plain text first, the lobby
// list (0x222A90) passes name "" and plain "[clan]", out is 0x80 bytes.
#define T5_ClanTag_Format 0x21B560
// (rankId, prestige, Material** out): looks up mp/rankIconTable.csv (row rankId, column prestige + 1) and registers the
// material, out is 0 for the default material. Called by cgame with clientinfo + 0x38/0x3C (0x16ABA0) and by the lobby
// with the party member's rank and prestige (0x143240).
#define T5_RankIcon_Lookup 0x1DAB40

// Both are tail calls of CL_ConsolePrint (0x1AF228) with channel 2 / 3
#define T5_CG_GameMessage 0x113550 // (localClientNum, msg)
#define T5_Sys_IsMainThread 0x3D2CF8 // compares the current thread id with the saved one
#define T5_Scr_Notify 0x354F08 // (ent, stringValue, paramcount), Scr_NotifyNum on ent->s.number
#define T5_GEntities 0x12AB290 // g_entities
#define T5_GEntitySize 0x2F8
// FindEntityId(inst, entnum, classnum, clientNum), name inferred from CoD4: Scr_NotifyNumInternal (0x5D34F8) calls it
// at 0x5D3678 and passes the result to VM_Notify as the owner id, skipping the notify if it is 0. Leaf function that
// looks up entnum + 0x800000 in the class map at 0xB62DD0 and never allocates (unlike Scr_GetEntityId at 0x5C2B00).
// MP-only (host menu), so no _ZM define; the ZM counterpart would be 0x5AFDA0 (bo1-re match, unverified).
#define T5_FindEntityId 0x5C60B8
#define T5_ClServerId 0xD25EF0 // cl.serverId, UIScript_scriptmenuresponse (0x44BB18) sends it as "cmd mr %i %i %s"

#define T5_PadGetData 0x8C6A98 // import stub of cellPadGetData
#define T5_VM_Notify 0x5CD370
#define T5_Menu_PaintAll 0x465368
#define T5_ClientCommand 0x2D6140
#define T5_ClientEndFrame 0x2BE130

#define T5_ForceHost_Patch 0x20EE5C // original: beq (0x4182FD0C)
#define T5_Bounce_Patch1 0x92838 // original: beq (0x41820084)
#define T5_Bounce_Patch2 0x92844 // original: bne cr6 (0x409A0018)

// Built-in keyboards: the clan tag callback (0x3FEB30, editclanname) and the ui_keyboard callback (0x3FEFC0) the class
// names are entered with. The character check 0x46B3B0 rejects ^ { } @ % \ ; " and is also used for the email entry.
#define T5_ClanTagInvalidChars_Patch 0x3FEDE4 // character check failed -> MENU_INVALIDCHARS, original: beq cr4 (0x4192011C)
#define T5_ClanTagOffensive_Patch 0x3FEE0C // profanity check (0x227C90) passed, original: beq cr6 (0x419A009C)
#define T5_ClanTagOffensive_Target 0x3FEEA8
#define T5_ClanTagCodNotAllowed_Patch 0x3FEEC4 // "3arc"/"COD" check (0x42CE40) passed, original: beq (0x418200AC)
#define T5_ClanTagCodNotAllowed_Target 0x3FEF70
// clanName sanitizer (^ becomes a space, other characters are dropped), also run when the profile is loaded and saved.
// Takes no arguments and returns nothing, it reads the dvar and writes it back.
#define T5_ClanName_Sanitize 0x1C5B18
// Gamer profile, one per controller at 0x1B12B70 + controller * 0xDD8. It holds the clan tag (+0x27C, 5 bytes) and the
// class names (+0xC81, 10 names of 16 bytes each: the dvars customclass1-5 and prestigeclass1-5, dvar table
// 0xD24C8C; order inferred from the copy at 0x563950). The class name keyboard callback (0x3FEFC0) runs: controller =
// CL_ControllerIndexFromClientNum(localClientNum), UpdateDvarsFromProfile(controller), set the dvar,
// UpdateProfileFromDvars(controller, 1). UpdateProfileFromDvars is named by its own log string; the other two names
// and the meaning of its second argument are inferred, unverified.
#define T5_CL_ControllerIndexFromClientNum 0x39D640
#define T5_GamerProfile_UpdateDvarsFromProfile 0x563080 // also sets clanName from the profile
#define T5_GamerProfile_UpdateProfileFromDvars 0x562090
#define T5_Dvar_GetString 0x4C7370
#define T5_Dvar_SetStringByName 0x4CF170
#define T5_KeyboardInvalidChars_Patch 0x3FF264 // character check passed, original: bne cr1 (0x40860048)
#define T5_KeyboardInvalidChars_Target 0x3FF2AC

// Prestige (plevel) compares of the unlock checks, from a static analysis. Each is one branch on plevel < required, the
// rank compares stay. Items cover the FEATURE_PRESTIGE_CLASS_1..5 rows (create-a-class slots 6-10).
#define T5_PrestigeUnlock_ItemLocked 0x9A02C // IsItemLocked (0x99F88), original: blt (0x41800008)
#define T5_PrestigeUnlock_ItemParentLocked 0x9A180 // same check on the parent item (0x9A088), original: blt (0x41800008)
#define T5_PrestigeUnlock_ServerItemLocked 0x97F7C // BG_IsItemLocked (0x97EE8), GSC isItemLocked, original: bgt cr6 (0x41990008)
#define T5_PrestigeUnlock_WeaponOptionLocked 0x97EB8 // IsWeaponOptionLocked (0x97E28), camos etc., original: blt cr1 (0x41840008)
// GetWeaponOptionUnlockPLevel (0xBE530): the camo list compares it with PLEVEL itself (OPTION_LIST_ACTION in
// cac_weapon.menu). Takes the branch to its return 0 that private matches take, original: bne cr7 (0x409E01FC)
#define T5_PrestigeUnlock_WeaponOptionPlevel 0xBE554
// Items such as face paints: the purchase state and the purchase itself compare plevel again, IsItemLocked alone isn't enough.
#define T5_PrestigeUnlock_ItemPurchased 0x9D280 // IsItemPurchased (0x9D190), original: bgt cr7 (0x419DFF50)
#define T5_PrestigeUnlock_ItemPurchase 0xA26E4 // purchase (0xA25C8, purchaseItem / purchasecurrentitem), original: bgt cr7 (0x419D02A4)
// "Purchase N more ... to unlock this" (classified items such as face paints)
#define T5_PrestigeUnlock_ItemPurchaseClassified 0xA2970 // same function, purchased count of the group, original: bgt cr4 (0x41910018)
#define T5_PrestigeUnlock_ItemClassified 0x98678 // IsItemClassified (0x98620), original: ble cr1 (0x40850010)
#define T5_PrestigeUnlock_ClanTagLocked 0x9C484 // BG_IsClanTagFeatureLocked (0x9C310), original: ble cr6 (0x409900A4)
// Calling cards: emblem backgrounds and icons from the emblemset asset.
#define T5_PrestigeUnlock_EmblemBgLocked1 0x6DF3C // IsEmblemBackgroundLocked (0x6DC98), original: blt cr7 (0x419C0088)
#define T5_PrestigeUnlock_EmblemBgLocked2 0x6DF90 // same function, original: bgt cr6 (0x41990030)
#define T5_PrestigeUnlock_EmblemBgLockedUi 0x6D51C // background lock shown in the UI (0x6D280), original: bgt cr6 (0x4199FFB8)
#define T5_PrestigeUnlock_EmblemIconLocked 0x6CCC0 // IsEmblemIconLocked (0x6CC78), original: ble cr6 (0x40990080)
#define T5_PrestigeUnlock_EmblemIconPurchased 0x6E9F0 // IsEmblemIconPurchased (0x6E870), original: bge cr6 (0x40980038)
#define T5_PrestigeUnlock_EmblemIconPurchase 0x6EEE4 // PurchaseEmblemIcon (0x6EE88), original: bge cr6 (0x4098016C)
#define T5_PrestigeUnlock_EmblemSaveCheck 0x6EC18 // emblem check before it's saved (0x6EA70), original: bge cr1 (0x40840028)
#define T5_PrestigeUnlock_EmblemIconList1 0x6F42C // icon list flags (0x6F1A0), original: bge (0x40800054)
#define T5_PrestigeUnlock_EmblemIconList2 0x6F454 // same function, original: bge cr7 (0x409C008C)
#define T5_PrestigeUnlock_EmblemIconLockedUi 0x6F8E0 // icon lock shown in the UI (0x6F808), original: bgt cr6 (0x4199FFC4)

#define T5_PartyData 0x2023500
#define T5_CgClientInfoBase 0xC3DFB8 // pointer, + i * 0x5D0 + 0x5DB04 = client name, + 0x5DB50 = clan tag (team message
                                     // popup at 0x1727CC)
#define T5_CgClientClanSize 8 // clan tag at clientinfo + 0x5DB50: filled from the snapshot by I_strncpyz(.., 8) at
                              // 0x1698B0, copied to the scoreboard with size 8 at 0x13C738, another field at + 0x5DB58
#define T5_CgClientRankOffset 0x5DB30 // int, rankId (level - 1) at clientinfo + 0x38, copied from clientState + 0x64 at
                                      // 0x16982C, the server's gclient + 0x282C that setRank writes
#define T5_CgClientPrestigeOffset 0x5DB34 // int, clientinfo + 0x3C, from clientState + 0x68 (gclient + 0x2830)
#define T5_CgClientNumOffset 0x0 // cg_t.clientNum, the local player's server client slot. 0x116D30 compares it in each of the
                                 // 4 local cg_t (stride 0x6B100), 0x155F7C indexes the 0x5D0 client info array with it
#define T5_LobbyParty 0xD53910 // party struct (id 0 at +0x57B8), checked by Party_IsHost (0x698490) (static, unverified),
                               // the pregame lobby (verified in game)
#define T5_GameParty 0xD5A090 // second party struct Party_IsHost checks (id 1), set up at 0x1E8B38, the private party
                              // (verified in game)
#define T5_PartyHostIndexOffset 0x5668
#define T5_PartyActiveOffset 0x5714
#define T5_PartyIsHostOffset 0x570C // int, local player is host
#define T5_PartyMembersOffset 0x11B0 // 18 members of 0x220: +0x00 status byte (slot used if > 2), +0x18 name, +0x5C clan tag
#define T5_PartyMemberSize 0x220
#define T5_PartyMaxMembers 18 // members of T5_PartyMemberSize at T5_PartyMembersOffset
#define T5_PartyMemberNameOffset 0x18
#define T5_PartyMemberNameSize 0x20 // next field at +0x38
#define T5_PartyMemberClanOffset 0x5C // clan tag, read by the lobby list (0x222A90) and 0x21C130. T5_PartyMemberClanSize
                                      // bytes, the COD points follow at +0x64. I_strncpyz'd with 5 by NetMsg_clan
                                      // (0x212918), 4 bytes are sent
#define T5_PartyMemberClanSize 8
#define T5_PartyMemberClanFeatureOffset 0xF8 // int, clan tag feature index (mp/clantagfeatures.csv), read at 0x222AA4
// Set from the party message "rank <rankId> <prestige> <codpoints>" (sent at 0x1FB768, handled at 0x2122F0)
#define T5_PartyMemberRankOffset 0xC8 // int, rankId (level - 1)
#define T5_PartyMemberPrestigeOffset 0xCC // int
// (party, ?, firstMember, targetMember, msg), writes the members from firstMember on into a partystate message until
// it's full: name with MSG_WriteString, clan tag with MSG_WriteData. Only called by the host's partystate sender
// (0x208DB0, "%ipartystate"), at 0x209404, 0x209538 and 0x2096FC.
#define T5_Party_WriteMembers 0x204820
// Local player 0. PlayerName1 is the SceNpId the userinfo sends as "npid" (online id first, 0x24 bytes per local
// client); PlayerName2 is the name the userinfo builder (0x3A73C0, via 0x68BF48) sends as "name" and the party copies
// into the local member when it's added (0x207BD0).
#define T5_PlayerName1 0x2000934
#define T5_PlayerName2 0x2000A14
// dvar_modifiedFlags: Dvar_SetVariant ORs the flags of a changed dvar in (0x4C8788). While connected the client frame
// (0x1CDE70) sends its userinfo to the server when T5_DvarFlagUserinfo is set, then clears it (0x1C7B48).
#define T5_DvarModifiedFlags 0x18F019C
#define T5_DvarFlagUserinfo 0x2
#define T5_PlaylistEntries 0x18950B8 // 64 entries of 0x98: +0x00 rule string, +0x58 int
#define T5_DvarPtr_BBA19C 0xBBA19C // bool dvar, default true (probably jump_slowdownEnable)
#define T5_DvarPtr_BBA1A4 0xBBA1A4 // float dvar, default 300 (probably bg_fallDamageMaxHeight)

#define T5_Stats_Base 0x208B600 // start of the local stats buffer (T5_StatsSize bytes), used 50x by the live stats code from 0x6AF784
#define T5_Stats_CodPoints 0x20942D1 // little endian
#define T5_Stats_RankXp 0x20946E5 // little endian, the rank follows from it
#define T5_Stats_Prestige 0x20946DD
#define T5_Stats_Rank 0x20946E1 // little endian, rank id (level - 1), what the menus show
#define T5_StatsSize 0x9968
// The DDL data starts at T5_Stats_Base + 0x28, the layout is in data/mp/stats_ddl.txt in bo1-re. itemstats: 256 items of 671 bits from bit 4, the first 3 bits of an item
// are isproversionunlocked (then new, purchased), bits packed from the lowest one.
#define T5_Stats_ItemStats 0x208D898
#define T5_Stats_ItemStatsBit 4
#define T5_Stats_ItemStatsCount 256
#define T5_Stats_ItemStatsBits 671
// Fields a menu script sets back to 0 when joining a lobby (bits from T5_Stats_Base + 0x28, see data/mp/stats_ddl.txt in bo1-re)
#define T5_Stats_CustomClassCamoBit 0x16AC // cacloadouts.customclass1.primarycamo (0x2D5 bit 4)
#define T5_Stats_PrestigeClassCamoBit 0x1C45 // cacloadouts.prestigeclass1.primarycamo (0x388 bit 5)
#define T5_Stats_ClassBits 280 // from one class to the next, 5 custom and 5 prestige classes
#define T5_Stats_SecondaryCamoBits 104 // from primarycamo to secondarycamo
#define T5_Stats_ClanTagSelectedBit 0x3B14 // clantagstats.selectedfeature (0x762 bit 4)
// (state, value string, buffer), *(int*)state = bit offset in the DDL. statWriteDDL (handler 0x6A36B8) writes with it, a
// menu script runs "statWriteDDL ... 0" for prestige camos and the clan tag color when joining a lobby.
#define T5_DDL_SetFromString 0x5E74E8
// Save/Load Stats: the buffer starts with a 0x28-byte header (bo1-re re-journal 33 and 44, static analysis only).
// +0 is a CRC-32 of the rest of the buffer, recalculated by the upload function 0x6B97D8 before every save and only
// checked on fetched buffers, so the stale one Load Stats leaves in the live header is harmless. +4 is the DDL version
// that DDL_ValidateBuffer checks, the same for every 1.13 buffer. +8 bit 0 is the dirty flag: DDL_SetInt sets it, the
// upload clears it; that the game saves a buffer marked like this is unverified. The other header bytes are unknown.
#define T5_StatsHeaderSize 0x28 // the DDL data follows
#define T5_Stats_DdlBit (T5_StatsHeaderSize * 8) // bit offset of the DDL data in the stats buffer
#define T5_StatsDirtyOffset 0x8
#define T5_StatsDirtyFlag 0x1
// Fields of the account, the console and the game's moderation, kept when stats are loaded (DDL bits from
// data/mp/stats_ddl.txt in bo1-re)
#define T5_Stats_ConnectionIdBit 0x3B1C // connectionid (0x763 bit 4), 64 bits
#define T5_Stats_ConsoleInfoBit 0x3B5C // consoleinfo (0x76B bit 4), 10 x 296 bits
#define T5_Stats_ConsoleInfoBits 2960
#define T5_Stats_FriendsListFingerprintBit 0xDBD8 // friendslistfingerprint (0x1B7B bit 0), 64 bits
#define T5_Stats_PlayerXuidBit 0x489E8 // playerxuid (0x913D bit 0), 64 bits
#define T5_Stats_StatResetNotificationBit 0x49B3C // statresetnotification (0x9367 bit 4), 64 bits
#define T5_Stats_WebTokenBit 0x4BF5E // webtoken (0x97EB bit 6), 128 bits
#define T5_Stats_IAmACheaterBit 0x1BCC // cacloadouts.iamacheater (0x379 bit 4), 1 bit
#define T5_Stats_ResetWarningDisplayedBit 0x215D // cacloadouts.resetwarningdislayed (0x42B bit 5), 1 bit
// lastconsolesave (0x7650 bit 4), 4 bits, likely the index into the 10 consoleinfo entries (unverified)
#define T5_Stats_LastConsoleSaveBit 0x3B284
#define T5_Stats_UploadBandwidthBit 0x49B7E // uploadbandwidth (0x936F bit 6), 32 bits
#define T5_Stats_PlayerStatsListBit 0x462E8 // playerstatslist (0x8C5D bit 0), 312 x 32 bits, enum globalstats_e
#define T5_Stats_PlayerStatBits 32
#define T5_Stats_PlayerStatBit(index) (T5_Stats_PlayerStatsListBit + (index) * T5_Stats_PlayerStatBits)
// globalstats_e indices of the ban, host and version entries (meaning inferred from the names, unverified)
#define T5_PlayerStat_BadHostCount 5
#define T5_PlayerStat_GametypeBan 50
#define T5_PlayerStat_MonthlyTimestamp 230
#define T5_PlayerStat_SessionBans 286
#define T5_PlayerStat_StatsVersion 287 // compared with the stats_version_check dvar
#define T5_PlayerStat_StatsBackupVersion 288
#define T5_PlayerStat_TimeWhenNextHost 300
#define T5_PlayerStat_Version 308
#define T5_PlayerStat_WeeklyTimestamp 309

// Import stubs of the game's sys_net functions (data/mp/imports.csv in bo1-re), for the webMAN notification, so the
// plugin doesn't import sys_net itself. Not called directly: notify_init resolves the functions from their import slots.
#define T5_Net_socket 0x8C62B8
#define T5_Net_connect 0x8C6130
#define T5_Net_send 0x8C63D0
#define T5_Net_recv 0x8C7D68
#define T5_Net_setsockopt 0x8C6210
#define T5_Net_socketclose 0x8C6168

// Anti-Freeze (MP only)
// (localClientNum, serverCommandNumber), tokenizes the reliable server command and returns 0 if CG_ServerCommand
// (0x164BB8) shouldn't run it. Called by CG_ExecuteNewServerCommands (0x166DF0) for each new command.
#define T5_CL_CGameNeedsServerCommand 0x1A6F00
#define T5_ClientConnection 0xD223C8 // pointer, + localClientNum * T5_ClientConnectionSize
#define T5_ClientConnectionSize 0x35890
#define T5_ServerCommandSequenceOffset 0x4348 // int, last received reliable command
#define T5_LastExecutedServerCommandOffset 0x434C // int, set by CL_CGameNeedsServerCommand
#define T5_ServerCommandsOffset 0x4350 // T5_MaxReliableCommands commands of T5_ServerCommandSize chars
#define T5_MaxReliableCommands 128
#define T5_ServerCommandSize 0x400
#define T5_MaxConfigstrings 0xAC2 // "d <index> <value>": CL_CGameNeedsServerCommand calls Com_Error above 0xAC1
#define T5_BG_GetNumWeapons 0xC0A68 // *(int*)0xBD8778 + 1, BG_GetWeaponDef (0xC0A98) indexes 0xBD877C unchecked
// The client side tokenizer. CL_CGameNeedsServerCommand tokenizes with Cmd_TokenizeString, "d" ends it and tokenizes
// again with Cmd_TokenizeStringWithLimit(text, 3). CG_ExecuteNewServerCommands ends it only when 1 was returned.
#define T5_Cmd_TokenizeString 0x39B538
#define T5_Cmd_TokenizeStringWithLimit 0x39B828
#define T5_Cmd_EndTokenizedString 0x39A150
// CmdArgs layout of both tokenizers (T5_CmdArgsSlot, T5_SvCmdArgs): +0x00 nesting, argc[nesting] at
// T5_CmdArgsArgcOffset, argv[nesting] at T5_CmdArgsArgvOffset.
#define T5_CmdArgsArgcOffset 0x84
#define T5_CmdArgsArgvOffset 0xA4
// (index), thread local values. T5_CmdArgsSlot is the client side CmdArgs.
#define T5_Sys_GetValue 0x3D2C40
#define T5_CmdArgsSlot 4
#define T5_game_strtol 0x44824 // the game's strtol, CG_ServerCommand and "d" read argv1 with (argv1, NULL, 10)
// Every 2D text draw (R_AddCmdDrawText and the *WithEffects variants) copies its text into the render command here.
#define T5_AddBaseDrawTextCmd 0x7538E8
// (textPool, poolSize, firstChar, charCount, font, x, y, xScale, yScale, color, style), the other builder of the same
// draw-text command (id 16): copies charCount bytes of a ring buffer from firstChar on. Only called by three wrappers
// (0x753610, 0x753768, 0x753860) from the console's message windows. Name and prototype inferred from CoD4 and the
// register use.
#define T5_R_AddCmdDrawConsoleText 0x7533C0
// sv_cmd_args, the CmdArgs read by SV_Cmd_ArgvBuffer. SV_Cmd_Argc is inlined, there is no function to call.
#define T5_SvCmdArgs 0x1461F74
// (client_t* cl), copies name and clanAbbrev from the userinfo at T5_ClientUserinfoOffset into the client_t. Called on
// connect and for each "userinfo" from the client.
#define T5_SV_UserinfoChanged 0x3E49D0
// (index, buffer, size), the only way the game (ClientUserinfoChanged 0x2C2AE8, ClientConnect 0x2C2FB8) reads userinfo.
// 0x3E5EF8 calls ClientUserinfoChanged before SV_UserinfoChanged.
#define T5_SV_GetUserinfo 0x3EAB20
#define T5_ClientUserinfoOffset 0x720 // char[], userinfo string in client_t
// client_t *, the client array (svs.clients, svs 0x1695280 + 0x1C) with stride T5_ClientSize. 0 while no server runs
// (zeroed at 0x3EAA0C on shutdown).
#define T5_SvClients 0x169529C
#define T5_ClientSize 0x651C8
// (XZoneInfo* zones, count, sync), XZoneInfo = {name, allocFlags, freeFlags}. Every zone load and unload goes through it:
// Cmd_loadzone_f (0x266018) via 0x265F60, and the level / UI code (e.g. 0x3A3298 frees with {NULL, 0, 0x40}). The
// unload runs on the calling thread, before the load is handed off: a loop over the loaded zones calls the zone free
// 0x25E2C8 (no other caller) for each zone matching freeFlags, under critical section 68. Only then are the names
// queued and the DB thread signaled (0x3D4410, 0x3D3FD8, 0x3D6388, no other caller). sync only selects the wait at
// the end, the DB thread only loads. Names inferred from the code; quit exits the process.
#define T5_DB_LoadXAssets 0x2615C0
// DB_FindXAssetHeader (0x2601A0, not called): a miss waits while a zone loads, then links a default entry under the
// name (0x25E918), Com_Error once the entry pool is full. The anti-freeze reads the hash table itself instead.
#define T5_XAssetHashTable 0xF3E97C // uint16_t[T5_XAssetHashTableSize] entry indices, 0 ends a chain
#define T5_XAssetHashTableSize 30000
// 16-byte entries: type, header, zoneIndex (u8), inuse (u8), nextHash (u16), nextOverride (u16). Layout from the
// lookup code, field names inferred from the IW engine (unverified).
#define T5_XAssetEntryPool 0xEC8C74
#define T5_XAssetEntrySize 0x10
#define T5_XAssetEntryPoolSize 30000 // entries, index 0 is never used (DB_LoadXAssets builds the free list from 1, 0x26183c)
#define T5_XAssetEntryTypeOffset 0x0
#define T5_XAssetEntryHeaderOffset 0x4
#define T5_XAssetEntryNextHashOffset 0xA
#define T5_AssetTypeMaterial 6

// Client stats on the host (MP only, see docs/mp/05-network.md in bo1-re). SV_ReceiveStats handles the OOB "stats"
// packets: chunk i of 0x4C6 bytes goes to client + T5_ClientStatsOffset + i * 0x4C6 (client_t at T5_SvClients) and
// sets bit i of the mask. getdstat reads the buffer only once the mask is complete.
#define T5_SV_ReceiveStats 0x3E2828
// int at client_t + 0: SV_DropClient stores 1 (0x3E4224) or 0 (0x3E45C4), SV_GetClientStatInt reads stats only
// above 1. The names of the individual states above 1 are unverified.
#define T5_ClientStateOffset 0x0
#define T5_ClientStateConnected 2 // lowest state of a connected client
// 0x9968 bytes, 32 full chunks of 0x4C6 and a partial 33rd. Same layout as T5_Stats_Base, no checksum over the data.
#define T5_ClientStatsOffset 0x5A4F8
#define T5_ClientStatsMaskOffset 0x651B8 // uint64_t, received chunks
// 33 chunks. SV_GetClientStatInt (0x3E2700) requires the mask to equal this exactly (cmpd at 0x3E2754). The mask
// can't get more bits: SV_ReceiveStats drops a chunk index above 0x20 (cmplwi at 0x3E2A78) and sets only bit index.
#define T5_ClientStatsMaskComplete 0x1FFFFFFFFull
// Godmode classes (Sakura Class Editor) set cacloadouts.<class>.body to 0x72. The class script only asserts the
// range, and level.tbl_armor holds the statsTable rows of group body and head, so tbl_armor[0x72] is probably a
// head reference instead of a body, for which _armor.gsc has no functions (inferred from the PC _class.gsc, not
// confirmed which row 0x72 is). The head field is not checked: custom classes get the default head, and Sakura's
// primary writes leave it at 0x01.
#define T5_Stats_CustomClassBodyBit 0x164C // cacloadouts.customclass1.body (0x2C9 bit 4)
#define T5_Stats_PrestigeClassBodyBit 0x1BE5 // cacloadouts.prestigeclass1.body (0x37C bit 5)
#define T5_Stats_BodyItemBits 8
// Body item ids: the statsTable rows of group "body" (stock PC raw mp/statsTable.csv, assumed identical on PS3).
// 0x61 is armor_null, which _armor.gsc replaces with standard_mp, so only 0x62-0x66 are real bodies.
#define T5_BodyItemFirst 0x62 // standard_mp (Lightweight)
#define T5_BodyItemLast 0x66 // hardened_mp (Hardline)
#define T5_BodyItemDefault 0x62 // standard_mp, the body of every class in the 11 dumps
// Killing a player like the GSC builtin suicide() does (PlayerCmd_suicide 0x2CA338): it clears flags & 3, zeroes the
// health of the entity and the client, then calls player_die(ent, ent, ent, 100000, 13, 0, NULL, 0, 0). The field names
// are inferred from that function and the IW engines (unverified).
#define T5_player_die 0x2D85E0 // (self, inflictor, attacker, damage, meansOfDeath, weapon, dir, hitLoc, psTimeOffset)
#define T5_GEntityClientOffset 0x144 // gclient_t*, 0 for entities that aren't players
#define T5_GEntityFlagsOffset 0x174
#define T5_GEntityGodmodeFlags 0x3 // FL_GODMODE | FL_DEMI_GODMODE in the IW engines (unverified)
#define T5_GEntityHealthOffset 0x194
#define T5_GClientHealthOffset 0x1C4 // probably ps.stats[STAT_HEALTH] (unverified)
#define T5_ModSuicide 13 // meansOfDeath PlayerCmd_suicide passes
#define T5_SuicideDamage 100000
// player_die returns with health 1 instead of killing when the player has no server DObj (0x2D86C8 -> 0x2D8AA8). A
// godmode class never gets its setModel, so the player is invisible and can't die. Names from the IW engines (unverified).
#define T5_Com_GetServerDObj 0x3A55E8 // (entnum) -> DObj*, 0 without a model: 1024 shorts at 0x157D61C index a pool
                                      // of 3072 DObjs of 0x7C at 0x151C9F0 (cleared at 0x3A56B0)
// The DObj of a player is only built in a later server frame: setmodel's G_DObjUpdate (0x373DA0) returns for clients,
// the frame code copies the model name into the clientinfo (0x2B9810), frees the DObj when it changed (0x36F880) and
// BG_UpdatePlayerDObj (0x5A390) creates it again through the bgs callback 0x2EC160 (unverified).
#define T5_GEntityModelOffset 0x15C // uint16_t model index, G_SetModel (0x3701D8) stores G_ModelIndex(name) there
// _class.gsc cac_getdata() reads the classes from the stats only once per connection, guarded by self.cac_initialized,
// so a class fixed after the first spawn needs that field removed. Script variable functions, names and argument order
// from CoD4's scr_variable with the T5 instance first (unverified): both hash (name + parentId) % 0xFFFD + 1 into the
// variable list *(0x1E8B100 + inst * 0x80) and walk the chain comparing status >> 8 with the name. The returned id
// indexes the server entries at list + (0x6000 + id) * 0x10.
#define T5_FindVariable 0x5BD240 // (inst, parentId, name) -> variable id, 0 if there is none; 62 callers
// (inst, parentId, name): no check whether the variable exists, a missing one goes on with index 0, so only call it
// after FindVariable found it. Makes the variable external (0x5BF060) and frees it with its value (tail call 0x5BE608).
#define T5_RemoveVariable 0x5BF908
// Variable entries as FindVariable and FreeValue (0x5BE608) read them: status at +8, its low 5 bits are the type
// (clrlwi at 0x5BE688), the value at +4. The child offset 0x6000 is the server instance's, the client instance uses
// another. Undefined is type 0, as isdefined (0x30AB20) treats it. FreeValue passes the value of types 2 and 3 to
// 0x5B7FD8 (probably SL_RemoveRefToString), so they hold a string id: VAR_STRING and VAR_ISTRING as in CoD4. Names
// from CoD4's scrVarGlob (unverified).
#define T5_ScrVarGlob 0x1E8B100 // VariableValueInternal* per instance
#define T5_ScrVarGlobStride 0x80
#define T5_VariableChildBegin 0x6000
#define T5_VariableSize 0x10
#define T5_VariableValueOffset 0x4
#define T5_VariableStatusOffset 0x8
#define T5_VariableTypeMask 0x1F
#define T5_VarString 2
// Object fields are keyed by canonical strings (as in CoD4), the 16-bit names VM_Execute reads from the bytecode.
// The compiler gets them from SL_TransferToCanonicalString(inst, sv) and stores the result in the bytecode (callers
// such as 0x5977D0, 0x5A30A8): it moves the string's ref to user 2 (0x5B7310), then returns the ushort
// canonicalStrings[sv] at 0x1BE8870 + 0x1C + 2 * (inst * 0x1081C + sv), or a new id from the counter at
// 0x1E8B200 + inst * 0x58 + 4. Scr_BeginLoadScripts clears the table (memset at 0x5A9628). The user 2 strings seem to
// be freed after the load (CoD4's SL_ShutdownSystem(2)), so a lookup by name afterwards (SL_GetCanonicalString
// 0x5A9700) creates a new id instead. Names from CoD4 (unverified).
#define T5_SL_TransferToCanonicalString 0x5A9178
#define T5_SL_ConvertToString 0x5B6D10 // (sv, inst) -> string

#define PPC_NOP 0x60000000
#define PPC_BLR 0x4E800020
#define PPC_B(from, to) (0x48000000 | (((to) - (from)) & 0x03FFFFFC)) // b to (relative, no link)

// Structs / Pointers
#define T5_TOC 0x00B576E8
#define T5_TOC_ZM 0x00A544B0

// clang-format on

#endif
