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
#define T5_I_stricmp 0x4DD8C0 // ClientCommand matches command names with it
#define T5_UI_OpenKeyboard 0x56A118
#define T5_Dvar_GetBool 0x4C7BF0
#define T5_R_TextHeight 0x734340
#define T5_R_TextWidth 0x734570
#define T5_SL_GetString 0x5B9A28
#define T5_R_RegisterFont 0x734350
#define T5_Material_RegisterHandle 0x7458D8
#define T5_Party_RemoveMember 0x208BD8 // (party, memberIndex, notify, reason), notify sends "endparty <reason>"
#define T5_Party_BanName 0x1E7338 // (name), adds to the ban list at LobbyParty + 0x53CC (16 x 0x24) that party joins check
                                  // (0x1E7830 at 0x2134CC), skipped for party id 1 (GameParty)
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
#define T5_LobbyParty 0xD53910 // party struct (id 0 at +0x57B8), checked by Party_IsHost (0x698490) (static, unverified)
#define T5_GameParty 0xD5A090 // second party struct Party_IsHost checks (id 1), set up at 0x1E8B38
#define T5_PartyHostIndexOffset 0x5668
#define T5_PartyActiveOffset 0x5714
#define T5_PartyIsHostOffset 0x570C // int, local player is host
#define T5_PartyMembersOffset 0x11B0 // 18 members of 0x220: +0x00 status byte (slot used if > 2), +0x18 name
#define T5_PartyMemberSize 0x220
#define T5_PartyMemberNameOffset 0x18
#define T5_PartyMemberClanOffset 0x5C // clan tag, read by the lobby list (0x222A90) and 0x21C130. T5_PartyMemberClanSize
                                      // bytes, the COD points follow at +0x64
#define T5_PartyMemberClanSize 8
#define T5_PartyMemberClanFeatureOffset 0xF8 // int, clan tag feature index (mp/clantagfeatures.csv), read at 0x222AA4
// Set from the party message "rank <rankId> <prestige> <codpoints>" (sent at 0x1FB768, handled at 0x2122F0)
#define T5_PartyMemberRankOffset 0xC8 // int, rankId (level - 1)
#define T5_PartyMemberPrestigeOffset 0xCC // int
#define T5_PlayerName1 0x2000934
#define T5_PlayerName2 0x2000A14
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

#define PPC_NOP 0x60000000
#define PPC_BLR 0x4E800020
#define PPC_B(from, to) (0x48000000 | (((to) - (from)) & 0x03FFFFFC)) // b to (relative, no link)

// Structs / Pointers
#define T5_TOC 0x00B576E8
#define T5_TOC_ZM 0x00A544B0

// clang-format on

#endif
