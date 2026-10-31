#include "checksums.h"

#include <stddef.h>
#include <stdint.h>

// Bob Jenkins' one-at-a-time hash (signed arithmetic shifts, as in InfinityOps).
// Wrapping math is done in uint32_t to avoid signed overflow UB; only the right shifts are signed.
int jenkins_hash(const char *key)
{
    uint32_t hash = 0;
    for (; *key; key++)
    {
        hash += (uint32_t)(signed char)*key;
        hash += hash << 10;
        hash ^= (uint32_t)((int32_t)hash >> 6);
    }
    hash += hash << 3;
    hash ^= (uint32_t)((int32_t)hash >> 11);
    hash += hash << 15;
    return (int)hash;
}

// Precomputed GSC opcode checksums of the unmodified scripts, keyed by
// jenkins_hash(mapname) / jenkins_hash(gametype). Names were recovered by hashing known
// map and gametype names; every entry resolved.
static const ChecksumEntry Table_SP[11] = {
    { (int)0x50E4D67A, (int)0x67EA3F3C, 0x07112B77, 0x000AEC07 }, // zombie_theater / zom
    { (int)0x07C2B3D3, (int)0x67EA3F3C, 0x4189326F, 0x000B8D53 }, // zombie_pentagon / zom
    { (int)0x09D292AA, (int)0x67EA3F3C, 0xD11281E6, 0x00085DEF }, // zombietron / zom
    { (int)0xD738D6FB, (int)0x67EA3F3C, 0xF58FD825, 0x000AB933 }, // zombie_cod5_prototype / zom
    { (int)0x97D91C04, (int)0x67EA3F3C, 0xCD1F5B37, 0x000AB32E }, // zombie_cod5_asylum / zom
    { (int)0xCC12CF54, (int)0x67EA3F3C, 0xC63D19EF, 0x000AF39A }, // zombie_cod5_sumpf / zom
    { (int)0x715D1555, (int)0x67EA3F3C, 0x1DF0415F, 0x000ADEC2 }, // zombie_cod5_factory / zom
    { (int)0x215F6F98, (int)0x67EA3F3C, 0xFB80507B, 0x000B6C9E }, // zombie_cosmodrome / zom
    { (int)0x5CC5210D, (int)0x67EA3F3C, 0x9E79CE21, 0x000BD166 }, // zombie_coast / zom
    { (int)0x6399D364, (int)0x67EA3F3C, 0xD37A5899, 0x000D46BF }, // zombie_temple / zom
    { (int)0x50DA358D, (int)0x67EA3F3C, 0xA5AB275F, 0x000C77D2 }, // zombie_moon / zom
};

static const ChecksumEntry Table_MP[312] = {
    { (int)0x15DCDABF, (int)0xA79BB2BD, 0x3005DF05, 0x0007E725 }, // mp_array / tdm
    { (int)0x15DCDABF, (int)0xDD6F4A20, 0x3E48D832, 0x0007E5F1 }, // mp_array / dm
    { (int)0x15DCDABF, (int)0x1148A15E, 0xCB3D9BE4, 0x0007FF8D }, // mp_array / sd
    { (int)0x15DCDABF, (int)0x9912D511, 0x4DC241B9, 0x000805DD }, // mp_array / dom
    { (int)0x15DCDABF, (int)0x487AF09E, 0xBA9F141F, 0x0007FFBD }, // mp_array / koth
    { (int)0x15DCDABF, (int)0xBA5C3457, 0xE345993E, 0x000804ED }, // mp_array / dem
    { (int)0x15DCDABF, (int)0xFB399121, 0xB389E6E7, 0x000809FD }, // mp_array / ctf
    { (int)0x15DCDABF, (int)0xA26E4A60, 0x220F26B3, 0x0007FD41 }, // mp_array / sab
    { (int)0x15DCDABF, (int)0xEB51AAE5, 0x20FCC30B, 0x0007EAC1 }, // mp_array / gun
    { (int)0x15DCDABF, (int)0x72613F6D, 0xB19409C6, 0x0007E8CD }, // mp_array / hlnd
    { (int)0x15DCDABF, (int)0xCD71DB69, 0x3ADBE825, 0x0007EAA5 }, // mp_array / oic
    { (int)0x15DCDABF, (int)0x7901E0EE, 0x153ACDB0, 0x0007F0F1 }, // mp_array / shrp
    { (int)0x8C8EB7F9, (int)0xA79BB2BD, 0x5707BCF1, 0x0007E2CE }, // mp_cracked / tdm
    { (int)0x8C8EB7F9, (int)0xDD6F4A20, 0xB8E4441E, 0x0007E19A }, // mp_cracked / dm
    { (int)0x8C8EB7F9, (int)0x1148A15E, 0x224033D0, 0x0007FB36 }, // mp_cracked / sd
    { (int)0x8C8EB7F9, (int)0x9912D511, 0x6D1E57A5, 0x00080186 }, // mp_cracked / dom
    { (int)0x8C8EB7F9, (int)0x487AF09E, 0x5AC48E0B, 0x0007FB66 }, // mp_cracked / koth
    { (int)0x8C8EB7F9, (int)0xBA5C3457, 0xE73D4D2A, 0x00080096 }, // mp_cracked / dem
    { (int)0x8C8EB7F9, (int)0xFB399121, 0xA5E410D3, 0x000805A6 }, // mp_cracked / ctf
    { (int)0x8C8EB7F9, (int)0xA26E4A60, 0x5D1E189F, 0x0007F8EA }, // mp_cracked / sab
    { (int)0x8C8EB7F9, (int)0xEB51AAE5, 0xE381C4F7, 0x0007E66A }, // mp_cracked / gun
    { (int)0x8C8EB7F9, (int)0x72613F6D, 0x9D52EDB2, 0x0007E476 }, // mp_cracked / hlnd
    { (int)0x8C8EB7F9, (int)0xCD71DB69, 0x52248611, 0x0007E64E }, // mp_cracked / oic
    { (int)0x8C8EB7F9, (int)0x7901E0EE, 0x759E2D9C, 0x0007EC9A }, // mp_cracked / shrp
    { (int)0xA75B5EC6, (int)0xA79BB2BD, 0xC489B2BF, 0x0007B83D }, // mp_crisis / tdm
    { (int)0xA75B5EC6, (int)0xDD6F4A20, 0x23C4262C, 0x0007B709 }, // mp_crisis / dm
    { (int)0xA75B5EC6, (int)0x1148A15E, 0x5BA8FC5E, 0x0007D0A5 }, // mp_crisis / sd
    { (int)0xA75B5EC6, (int)0x9912D511, 0x0D2D7E73, 0x0007D6F5 }, // mp_crisis / dom
    { (int)0xA75B5EC6, (int)0x487AF09E, 0xA230CC59, 0x0007D0D5 }, // mp_crisis / koth
    { (int)0xA75B5EC6, (int)0xBA5C3457, 0x2E5BAE38, 0x0007D605 }, // mp_crisis / dem
    { (int)0xA75B5EC6, (int)0xFB399121, 0x3738E921, 0x0007DB15 }, // mp_crisis / ctf
    { (int)0xA75B5EC6, (int)0xA26E4A60, 0x3E5ADFED, 0x0007CE59 }, // mp_crisis / sab
    { (int)0xA75B5EC6, (int)0xEB51AAE5, 0xEA755A45, 0x0007BBD9 }, // mp_crisis / gun
    { (int)0xA75B5EC6, (int)0x72613F6D, 0xD7A618C0, 0x0007B9E5 }, // mp_crisis / hlnd
    { (int)0xA75B5EC6, (int)0xCD71DB69, 0xD62E63DF, 0x0007BBBD }, // mp_crisis / oic
    { (int)0xA75B5EC6, (int)0x7901E0EE, 0xEA25652A, 0x0007C209 }, // mp_crisis / shrp
    { (int)0x63B3AC1F, (int)0xA79BB2BD, 0x0790C3B1, 0x00079F78 }, // mp_firingrange / tdm
    { (int)0x63B3AC1F, (int)0xDD6F4A20, 0x23B5285E, 0x00079E44 }, // mp_firingrange / dm
    { (int)0x63B3AC1F, (int)0x1148A15E, 0x3F0F8710, 0x0007B7E0 }, // mp_firingrange / sd
    { (int)0x63B3AC1F, (int)0x9912D511, 0x62FFD465, 0x0007BE30 }, // mp_firingrange / dom
    { (int)0x63B3AC1F, (int)0x487AF09E, 0xE9D6EFCB, 0x0007B810 }, // mp_firingrange / koth
    { (int)0x63B3AC1F, (int)0xBA5C3457, 0x1A02DB6A, 0x0007BD40 }, // mp_firingrange / dem
    { (int)0x63B3AC1F, (int)0xFB399121, 0xFB1E2E93, 0x0007C250 }, // mp_firingrange / ctf
    { (int)0x63B3AC1F, (int)0xA26E4A60, 0x9398805F, 0x0007B594 }, // mp_firingrange / sab
    { (int)0x63B3AC1F, (int)0xEB51AAE5, 0x7D7160B7, 0x0007A314 }, // mp_firingrange / gun
    { (int)0x63B3AC1F, (int)0x72613F6D, 0x8FB857F2, 0x0007A120 }, // mp_firingrange / hlnd
    { (int)0x63B3AC1F, (int)0xCD71DB69, 0x53657CD1, 0x0007A2F8 }, // mp_firingrange / oic
    { (int)0x63B3AC1F, (int)0x7901E0EE, 0x98FCCADC, 0x0007A944 }, // mp_firingrange / shrp
    { (int)0x162DE73C, (int)0xA79BB2BD, 0x78E076D4, 0x0007F259 }, // mp_duga / tdm
    { (int)0x162DE73C, (int)0xDD6F4A20, 0x9094C8C7, 0x0007F125 }, // mp_duga / dm
    { (int)0x162DE73C, (int)0x1148A15E, 0xFDB33A55, 0x00080AC1 }, // mp_duga / sd
    { (int)0x162DE73C, (int)0x9912D511, 0x3C7A00A0, 0x00081111 }, // mp_duga / dom
    { (int)0x162DE73C, (int)0x487AF09E, 0x3835CFFA, 0x00080AF1 }, // mp_duga / koth
    { (int)0x162DE73C, (int)0xBA5C3457, 0x11B9B33B, 0x00081021 }, // mp_duga / dem
    { (int)0x162DE73C, (int)0xFB399121, 0xA85E5232, 0x00081531 }, // mp_duga / ctf
    { (int)0x162DE73C, (int)0xA26E4A60, 0xE45435E6, 0x00080875 }, // mp_duga / sab
    { (int)0x162DE73C, (int)0xEB51AAE5, 0x85E3188E, 0x0007F5F5 }, // mp_duga / gun
    { (int)0x162DE73C, (int)0x72613F6D, 0x23006FB3, 0x0007F401 }, // mp_duga / hlnd
    { (int)0x162DE73C, (int)0xCD71DB69, 0xB0F881B4, 0x0007F5D9 }, // mp_duga / oic
    { (int)0x162DE73C, (int)0x7901E0EE, 0x28CE6C09, 0x0007FC25 }, // mp_duga / shrp
    { (int)0x331F89FC, (int)0xA79BB2BD, 0x25270207, 0x0007ED68 }, // mp_hanoi / tdm
    { (int)0x331F89FC, (int)0xDD6F4A20, 0xDB3083BA, 0x0007EC34 }, // mp_hanoi / dm
    { (int)0x331F89FC, (int)0x1148A15E, 0x579466C8, 0x000805D0 }, // mp_hanoi / sd
    { (int)0x331F89FC, (int)0x9912D511, 0x78E7CAD3, 0x00080C20 }, // mp_hanoi / dom
    { (int)0x331F89FC, (int)0x487AF09E, 0xC5D94AAD, 0x00080600 }, // mp_hanoi / koth
    { (int)0x331F89FC, (int)0xBA5C3457, 0x20CF7F2E, 0x00080B30 }, // mp_hanoi / dem
    { (int)0x331F89FC, (int)0xFB399121, 0x248292E5, 0x00081040 }, // mp_hanoi / ctf
    { (int)0x331F89FC, (int)0xA26E4A60, 0x0FFA1799, 0x00080384 }, // mp_hanoi / sab
    { (int)0x331F89FC, (int)0xEB51AAE5, 0x56BE4C41, 0x0007F104 }, // mp_hanoi / gun
    { (int)0x331F89FC, (int)0x72613F6D, 0x026FD1A6, 0x0007EF10 }, // mp_hanoi / hlnd
    { (int)0x331F89FC, (int)0xCD71DB69, 0x7744A4E7, 0x0007F0E8 }, // mp_hanoi / oic
    { (int)0x331F89FC, (int)0x7901E0EE, 0xC852B97C, 0x0007F734 }, // mp_hanoi / shrp
    { (int)0xC4D2ED63, (int)0xA79BB2BD, 0xD0C37436, 0x0007D123 }, // mp_cairo / tdm
    { (int)0xC4D2ED63, (int)0xDD6F4A20, 0x854B59E9, 0x0007CFEF }, // mp_cairo / dm
    { (int)0xC4D2ED63, (int)0x1148A15E, 0xF757E4F7, 0x0007E98B }, // mp_cairo / sd
    { (int)0xC4D2ED63, (int)0x9912D511, 0xBB95CD02, 0x0007EFDB }, // mp_cairo / dom
    { (int)0xC4D2ED63, (int)0x487AF09E, 0x473184DC, 0x0007E9BB }, // mp_cairo / koth
    { (int)0xC4D2ED63, (int)0xBA5C3457, 0x248BC55D, 0x0007EEEB }, // mp_cairo / dem
    { (int)0xC4D2ED63, (int)0xFB399121, 0x794D6D14, 0x0007F3FB }, // mp_cairo / ctf
    { (int)0xC4D2ED63, (int)0xA26E4A60, 0x7E5561C8, 0x0007E73F }, // mp_cairo / sab
    { (int)0xC4D2ED63, (int)0xEB51AAE5, 0x2BC17670, 0x0007D4BF }, // mp_cairo / gun
    { (int)0xC4D2ED63, (int)0x72613F6D, 0xF599B7D5, 0x0007D2CB }, // mp_cairo / hlnd
    { (int)0xC4D2ED63, (int)0xCD71DB69, 0xF2C39716, 0x0007D4A3 }, // mp_cairo / oic
    { (int)0xC4D2ED63, (int)0x7901E0EE, 0x574EA7AB, 0x0007DAEF }, // mp_cairo / shrp
    { (int)0x97A999D5, (int)0xA79BB2BD, 0x6D240475, 0x0007B430 }, // mp_havoc / tdm
    { (int)0x97A999D5, (int)0xDD6F4A20, 0x9F8C91E2, 0x0007B2FC }, // mp_havoc / dm
    { (int)0x97A999D5, (int)0x1148A15E, 0x1B224C14, 0x0007CC98 }, // mp_havoc / sd
    { (int)0x97A999D5, (int)0x9912D511, 0xBDFC3829, 0x0007D2E8 }, // mp_havoc / dom
    { (int)0x97A999D5, (int)0x487AF09E, 0x0F3CD20F, 0x0007CCC8 }, // mp_havoc / koth
    { (int)0x97A999D5, (int)0xBA5C3457, 0x368731EE, 0x0007D1F8 }, // mp_havoc / dem
    { (int)0x97A999D5, (int)0xFB399121, 0x3D907ED7, 0x0007D708 }, // mp_havoc / ctf
    { (int)0x97A999D5, (int)0xA26E4A60, 0x3EA30DA3, 0x0007CA4C }, // mp_havoc / sab
    { (int)0x97A999D5, (int)0xEB51AAE5, 0x33E837FB, 0x0007B7CC }, // mp_havoc / gun
    { (int)0x97A999D5, (int)0x72613F6D, 0x3228AC76, 0x0007B5D8 }, // mp_havoc / hlnd
    { (int)0x97A999D5, (int)0xCD71DB69, 0xC3F2F595, 0x0007B7B0 }, // mp_havoc / oic
    { (int)0x97A999D5, (int)0x7901E0EE, 0x6CF34CE0, 0x0007BDFC }, // mp_havoc / shrp
    { (int)0x9164BC40, (int)0xA79BB2BD, 0x3CC86166, 0x0007F65C }, // mp_cosmodrome / tdm
    { (int)0x9164BC40, (int)0xDD6F4A20, 0x372AA213, 0x0007F528 }, // mp_cosmodrome / dm
    { (int)0x9164BC40, (int)0x1148A15E, 0x98E2D8C5, 0x00080EC4 }, // mp_cosmodrome / sd
    { (int)0x9164BC40, (int)0x9912D511, 0x6996E21A, 0x00081514 }, // mp_cosmodrome / dom
    { (int)0x9164BC40, (int)0x487AF09E, 0x5D3B4580, 0x00080EF4 }, // mp_cosmodrome / koth
    { (int)0x9164BC40, (int)0xBA5C3457, 0x7315E51F, 0x00081424 }, // mp_cosmodrome / dem
    { (int)0x9164BC40, (int)0xFB399121, 0x9029E448, 0x00081934 }, // mp_cosmodrome / ctf
    { (int)0x9164BC40, (int)0xA26E4A60, 0x43C2C614, 0x00080C78 }, // mp_cosmodrome / sab
    { (int)0x9164BC40, (int)0xEB51AAE5, 0xE341C66C, 0x0007F9F8 }, // mp_cosmodrome / gun
    { (int)0x9164BC40, (int)0x72613F6D, 0x16A7C1A7, 0x0007F804 }, // mp_cosmodrome / hlnd
    { (int)0x9164BC40, (int)0xCD71DB69, 0x52A29A86, 0x0007F9DC }, // mp_cosmodrome / oic
    { (int)0x9164BC40, (int)0x7901E0EE, 0x8EC6AC91, 0x00080028 }, // mp_cosmodrome / shrp
    { (int)0xFACAEC27, (int)0xA79BB2BD, 0xB2EA9189, 0x0007985B }, // mp_nuked / tdm
    { (int)0xFACAEC27, (int)0xDD6F4A20, 0x0EBDF9B6, 0x00079727 }, // mp_nuked / dm
    { (int)0xFACAEC27, (int)0x1148A15E, 0x85166368, 0x0007B0C3 }, // mp_nuked / sd
    { (int)0xFACAEC27, (int)0x9912D511, 0x6952B03D, 0x0007B713 }, // mp_nuked / dom
    { (int)0xFACAEC27, (int)0x487AF09E, 0x5AA2E4A3, 0x0007B0F3 }, // mp_nuked / koth
    { (int)0xFACAEC27, (int)0xBA5C3457, 0x07383EC2, 0x0007B623 }, // mp_nuked / dem
    { (int)0xFACAEC27, (int)0xFB399121, 0x84884F6B, 0x0007BB33 }, // mp_nuked / ctf
    { (int)0xFACAEC27, (int)0xA26E4A60, 0xCC1D5337, 0x0007AE77 }, // mp_nuked / sab
    { (int)0xFACAEC27, (int)0xEB51AAE5, 0xE486B78F, 0x00079BF7 }, // mp_nuked / gun
    { (int)0xFACAEC27, (int)0x72613F6D, 0x2CE8874A, 0x00079A03 }, // mp_nuked / hlnd
    { (int)0xFACAEC27, (int)0xCD71DB69, 0x84D4FAA9, 0x00079BDB }, // mp_nuked / oic
    { (int)0xFACAEC27, (int)0x7901E0EE, 0x2A895934, 0x0007A227 }, // mp_nuked / shrp
    { (int)0x34504D14, (int)0xA79BB2BD, 0x451857AD, 0x00080111 }, // mp_radiation / tdm
    { (int)0x34504D14, (int)0xDD6F4A20, 0xD1BF28DA, 0x0007FFDD }, // mp_radiation / dm
    { (int)0x34504D14, (int)0x1148A15E, 0x9DB6DC8C, 0x00081979 }, // mp_radiation / sd
    { (int)0x34504D14, (int)0x9912D511, 0xA3F41A61, 0x00081FC9 }, // mp_radiation / dom
    { (int)0x34504D14, (int)0x487AF09E, 0x56933CC7, 0x000819A9 }, // mp_radiation / koth
    { (int)0x34504D14, (int)0xBA5C3457, 0x72EE89E6, 0x00081ED9 }, // mp_radiation / dem
    { (int)0x34504D14, (int)0xFB399121, 0xA669CF8F, 0x000823E9 }, // mp_radiation / ctf
    { (int)0x34504D14, (int)0xA26E4A60, 0x185BAF5B, 0x0008172D }, // mp_radiation / sab
    { (int)0x34504D14, (int)0xEB51AAE5, 0xE3EC8BB3, 0x000804AD }, // mp_radiation / gun
    { (int)0x34504D14, (int)0x72613F6D, 0x242ABA6E, 0x000802B9 }, // mp_radiation / hlnd
    { (int)0x34504D14, (int)0xCD71DB69, 0x312D60CD, 0x00080491 }, // mp_radiation / oic
    { (int)0x34504D14, (int)0x7901E0EE, 0xDC10AE58, 0x00080ADD }, // mp_radiation / shrp
    { (int)0x1DC2EE8B, (int)0xA79BB2BD, 0x9063C2EE, 0x0007C1A9 }, // mp_mountain / tdm
    { (int)0x1DC2EE8B, (int)0xDD6F4A20, 0x9EC645E1, 0x0007C075 }, // mp_mountain / dm
    { (int)0x1DC2EE8B, (int)0x1148A15E, 0x2421516F, 0x0007DA11 }, // mp_mountain / sd
    { (int)0x1DC2EE8B, (int)0x9912D511, 0x220C10BA, 0x0007E061 }, // mp_mountain / dom
    { (int)0x1DC2EE8B, (int)0x487AF09E, 0xF32F3E14, 0x0007DA41 }, // mp_mountain / koth
    { (int)0x1DC2EE8B, (int)0xBA5C3457, 0xD6EB2C55, 0x0007DF71 }, // mp_mountain / dem
    { (int)0x1DC2EE8B, (int)0xFB399121, 0x655E284C, 0x0007E481 }, // mp_mountain / ctf
    { (int)0x1DC2EE8B, (int)0xA26E4A60, 0x7E59C800, 0x0007D7C5 }, // mp_mountain / sab
    { (int)0x1DC2EE8B, (int)0xEB51AAE5, 0x57EFE2A8, 0x0007C545 }, // mp_mountain / gun
    { (int)0x1DC2EE8B, (int)0x72613F6D, 0x564910CD, 0x0007C351 }, // mp_mountain / hlnd
    { (int)0x1DC2EE8B, (int)0xCD71DB69, 0xAB2B6DCE, 0x0007C529 }, // mp_mountain / oic
    { (int)0x1DC2EE8B, (int)0x7901E0EE, 0x121C3F23, 0x0007CB75 }, // mp_mountain / shrp
    { (int)0xC12CA53C, (int)0xA79BB2BD, 0xEBA41CD3, 0x0007B351 }, // mp_villa / tdm
    { (int)0xC12CA53C, (int)0xDD6F4A20, 0x8CAB7246, 0x0007B21D }, // mp_villa / dm
    { (int)0xC12CA53C, (int)0x1148A15E, 0xE9EFEED4, 0x0007CBB9 }, // mp_villa / sd
    { (int)0xC12CA53C, (int)0x9912D511, 0xC8C6B49F, 0x0007D209 }, // mp_villa / dom
    { (int)0xC12CA53C, (int)0x487AF09E, 0x72B39CF9, 0x0007CBE9 }, // mp_villa / koth
    { (int)0xC12CA53C, (int)0xBA5C3457, 0xF46CEEBA, 0x0007D119 }, // mp_villa / dem
    { (int)0xC12CA53C, (int)0xFB399121, 0x201A4B31, 0x0007D629 }, // mp_villa / ctf
    { (int)0xC12CA53C, (int)0xA26E4A60, 0x8E9AE0E5, 0x0007C96D }, // mp_villa / sab
    { (int)0xC12CA53C, (int)0xEB51AAE5, 0x1C9A478D, 0x0007B6ED }, // mp_villa / gun
    { (int)0xC12CA53C, (int)0x72613F6D, 0x454E7732, 0x0007B4F9 }, // mp_villa / hlnd
    { (int)0xC12CA53C, (int)0xCD71DB69, 0xD451D7B3, 0x0007B6D1 }, // mp_villa / oic
    { (int)0xC12CA53C, (int)0x7901E0EE, 0x0C00D288, 0x0007BD1D }, // mp_villa / shrp
    { (int)0x18F695FE, (int)0xA79BB2BD, 0x77644968, 0x0007D779 }, // mp_russianbase / tdm
    { (int)0x18F695FE, (int)0xDD6F4A20, 0x436D2695, 0x0007D645 }, // mp_russianbase / dm
    { (int)0x18F695FE, (int)0x1148A15E, 0xC97D9247, 0x0007EFE1 }, // mp_russianbase / sd
    { (int)0x18F695FE, (int)0x9912D511, 0xBE503C1C, 0x0007F631 }, // mp_russianbase / dom
    { (int)0x18F695FE, (int)0x487AF09E, 0xF1684682, 0x0007F011 }, // mp_russianbase / koth
    { (int)0x18F695FE, (int)0xBA5C3457, 0xB08D57A1, 0x0007F541 }, // mp_russianbase / dem
    { (int)0x18F695FE, (int)0xFB399121, 0xC191B94A, 0x0007FA51 }, // mp_russianbase / ctf
    { (int)0x18F695FE, (int)0xA26E4A60, 0xDD496916, 0x0007ED95 }, // mp_russianbase / sab
    { (int)0x18F695FE, (int)0xEB51AAE5, 0xD442E56E, 0x0007DB15 }, // mp_russianbase / gun
    { (int)0x18F695FE, (int)0x72613F6D, 0xB1B56829, 0x0007D921 }, // mp_russianbase / hlnd
    { (int)0x18F695FE, (int)0xCD71DB69, 0x564CD288, 0x0007DAF9 }, // mp_russianbase / oic
    { (int)0x18F695FE, (int)0x7901E0EE, 0x69553413, 0x0007E145 }, // mp_russianbase / shrp
    { (int)0xC7796344, (int)0xA79BB2BD, 0xC585FA74, 0x0007EE35 }, // mp_berlinwall2 / tdm
    { (int)0xC7796344, (int)0xDD6F4A20, 0xF275CEE1, 0x0007ED01 }, // mp_berlinwall2 / dm
    { (int)0xC7796344, (int)0x1148A15E, 0x74401F13, 0x0008069D }, // mp_berlinwall2 / sd
    { (int)0xC7796344, (int)0x9912D511, 0xAC754A28, 0x00080CED }, // mp_berlinwall2 / dom
    { (int)0xC7796344, (int)0x487AF09E, 0xB605960E, 0x000806CD }, // mp_berlinwall2 / koth
    { (int)0xC7796344, (int)0xBA5C3457, 0x491492ED, 0x00080BFD }, // mp_berlinwall2 / dem
    { (int)0xC7796344, (int)0xFB399121, 0x1F779AD6, 0x0008110D }, // mp_berlinwall2 / ctf
    { (int)0xC7796344, (int)0xA26E4A60, 0x7E3A8DA2, 0x00080451 }, // mp_berlinwall2 / sab
    { (int)0xC7796344, (int)0xEB51AAE5, 0x4F46BFFA, 0x0007F1D1 }, // mp_berlinwall2 / gun
    { (int)0xC7796344, (int)0x72613F6D, 0x647DA575, 0x0007EFDD }, // mp_berlinwall2 / hlnd
    { (int)0xC7796344, (int)0xCD71DB69, 0x53884B94, 0x0007F1B5 }, // mp_berlinwall2 / oic
    { (int)0xC7796344, (int)0x7901E0EE, 0x251783DF, 0x0007F801 }, // mp_berlinwall2 / shrp
    { (int)0x12EDAFE8, (int)0xA79BB2BD, 0xBCE10E82, 0x0007F78B }, // mp_discovery / tdm
    { (int)0x12EDAFE8, (int)0xDD6F4A20, 0xC10ECBEF, 0x0007F657 }, // mp_discovery / dm
    { (int)0x12EDAFE8, (int)0x1148A15E, 0xE2596621, 0x00080FF3 }, // mp_discovery / sd
    { (int)0x12EDAFE8, (int)0x9912D511, 0xDCEE0236, 0x00081643 }, // mp_discovery / dom
    { (int)0x12EDAFE8, (int)0x487AF09E, 0x88583C1C, 0x00081023 }, // mp_discovery / koth
    { (int)0x12EDAFE8, (int)0xBA5C3457, 0x6898ABFB, 0x00081553 }, // mp_discovery / dem
    { (int)0x12EDAFE8, (int)0xFB399121, 0xB13F68E4, 0x00081A63 }, // mp_discovery / ctf
    { (int)0x12EDAFE8, (int)0xA26E4A60, 0x14F537B0, 0x00080DA7 }, // mp_discovery / sab
    { (int)0x12EDAFE8, (int)0xEB51AAE5, 0x07B4E208, 0x0007FB27 }, // mp_discovery / gun
    { (int)0x12EDAFE8, (int)0x72613F6D, 0x46F1A683, 0x0007F933 }, // mp_discovery / hlnd
    { (int)0x12EDAFE8, (int)0xCD71DB69, 0xE21DFFA2, 0x0007FB0B }, // mp_discovery / oic
    { (int)0x12EDAFE8, (int)0x7901E0EE, 0x07ADA6ED, 0x00080157 }, // mp_discovery / shrp
    { (int)0x99443F20, (int)0xA79BB2BD, 0xE38B2911, 0x00083EEF }, // mp_kowloon / tdm
    { (int)0x99443F20, (int)0xDD6F4A20, 0xFE640F04, 0x00083DBB }, // mp_kowloon / dm
    { (int)0x99443F20, (int)0x1148A15E, 0x9B5A0892, 0x00085757 }, // mp_kowloon / sd
    { (int)0x99443F20, (int)0x9912D511, 0x4EEF02DD, 0x00085DA7 }, // mp_kowloon / dom
    { (int)0x99443F20, (int)0x487AF09E, 0x157CAA37, 0x00085787 }, // mp_kowloon / koth
    { (int)0x99443F20, (int)0xBA5C3457, 0x1AF5A978, 0x00085CB7 }, // mp_kowloon / dem
    { (int)0x99443F20, (int)0xFB399121, 0x79934C6F, 0x000861C7 }, // mp_kowloon / ctf
    { (int)0x99443F20, (int)0xA26E4A60, 0x9CB8E023, 0x0008550B }, // mp_kowloon / sab
    { (int)0x99443F20, (int)0xEB51AAE5, 0xC82222CB, 0x0008428B }, // mp_kowloon / gun
    { (int)0x99443F20, (int)0x72613F6D, 0xF90985F0, 0x00084097 }, // mp_kowloon / hlnd
    { (int)0x99443F20, (int)0xCD71DB69, 0x0793B3F1, 0x0008426F }, // mp_kowloon / oic
    { (int)0x99443F20, (int)0x7901E0EE, 0xAE2CEA46, 0x000848BB }, // mp_kowloon / shrp
    { (int)0xA874DF3D, (int)0xA79BB2BD, 0x8BBBAAD9, 0x0007C551 }, // mp_stadium / tdm
    { (int)0xA874DF3D, (int)0xDD6F4A20, 0x4008B5CC, 0x0007C41D }, // mp_stadium / dm
    { (int)0xA874DF3D, (int)0x1148A15E, 0x1432915A, 0x0007DDB9 }, // mp_stadium / sd
    { (int)0xA874DF3D, (int)0x9912D511, 0xD5BE18A5, 0x0007E409 }, // mp_stadium / dom
    { (int)0xA874DF3D, (int)0x487AF09E, 0x22EA35FF, 0x0007DDE9 }, // mp_stadium / koth
    { (int)0xA874DF3D, (int)0xBA5C3457, 0x7D097C40, 0x0007E319 }, // mp_stadium / dem
    { (int)0xA874DF3D, (int)0xFB399121, 0x8CF46037, 0x0007E829 }, // mp_stadium / ctf
    { (int)0xA874DF3D, (int)0xA26E4A60, 0x5EB9DFEB, 0x0007DB6D }, // mp_stadium / sab
    { (int)0xA874DF3D, (int)0xEB51AAE5, 0x4381BA93, 0x0007C8ED }, // mp_stadium / gun
    { (int)0xA874DF3D, (int)0x72613F6D, 0x7B88A0B8, 0x0007C6F9 }, // mp_stadium / hlnd
    { (int)0xA874DF3D, (int)0xCD71DB69, 0x20A055B9, 0x0007C8D1 }, // mp_stadium / oic
    { (int)0xA874DF3D, (int)0x7901E0EE, 0x13C75F0E, 0x0007CF1D }, // mp_stadium / shrp
    { (int)0xC1E098E0, (int)0xA79BB2BD, 0xA725F766, 0x0007F02A }, // mp_gridlock / tdm
    { (int)0xC1E098E0, (int)0xDD6F4A20, 0x0A4A5893, 0x0007EEF6 }, // mp_gridlock / dm
    { (int)0xC1E098E0, (int)0x1148A15E, 0x58CCAC45, 0x00080892 }, // mp_gridlock / sd
    { (int)0xC1E098E0, (int)0x9912D511, 0xAC57FA1A, 0x00080EE2 }, // mp_gridlock / dom
    { (int)0xC1E098E0, (int)0x487AF09E, 0xBF97FC80, 0x000808C2 }, // mp_gridlock / koth
    { (int)0xC1E098E0, (int)0xBA5C3457, 0x52EF799F, 0x00080DF2 }, // mp_gridlock / dem
    { (int)0xC1E098E0, (int)0xFB399121, 0x5CD90F48, 0x00081302 }, // mp_gridlock / ctf
    { (int)0xC1E098E0, (int)0xA26E4A60, 0x1CFCAF14, 0x00080646 }, // mp_gridlock / sab
    { (int)0xC1E098E0, (int)0xEB51AAE5, 0x520D0B6C, 0x0007F3C6 }, // mp_gridlock / gun
    { (int)0xC1E098E0, (int)0x72613F6D, 0xD4822A27, 0x0007F1D2 }, // mp_gridlock / hlnd
    { (int)0xC1E098E0, (int)0xCD71DB69, 0xA9450086, 0x0007F3AA }, // mp_gridlock / oic
    { (int)0xC1E098E0, (int)0x7901E0EE, 0x23F83E11, 0x0007F9F6 }, // mp_gridlock / shrp
    { (int)0x6979D00F, (int)0xA79BB2BD, 0x529A90A9, 0x000826A3 }, // mp_hotel / tdm
    { (int)0x6979D00F, (int)0xDD6F4A20, 0xC329615C, 0x0008256F }, // mp_hotel / dm
    { (int)0x6979D00F, (int)0x1148A15E, 0x2C3BAA6A, 0x00083F0B }, // mp_hotel / sd
    { (int)0x6979D00F, (int)0x9912D511, 0xFA1A9575, 0x0008455B }, // mp_hotel / dom
    { (int)0x6979D00F, (int)0x487AF09E, 0x5A5DB74F, 0x00083F3B }, // mp_hotel / koth
    { (int)0x6979D00F, (int)0xBA5C3457, 0xD99A60D0, 0x0008446B }, // mp_hotel / dem
    { (int)0x6979D00F, (int)0xFB399121, 0x30AC9787, 0x0008497B }, // mp_hotel / ctf
    { (int)0x6979D00F, (int)0xA26E4A60, 0x8910603B, 0x00083CBF }, // mp_hotel / sab
    { (int)0x6979D00F, (int)0xEB51AAE5, 0x7AD15CE3, 0x00082A3F }, // mp_hotel / gun
    { (int)0x6979D00F, (int)0x72613F6D, 0x076F8B48, 0x0008284B }, // mp_hotel / hlnd
    { (int)0x6979D00F, (int)0xCD71DB69, 0x65B89389, 0x00082A23 }, // mp_hotel / oic
    { (int)0x6979D00F, (int)0x7901E0EE, 0x6F0C411E, 0x0008306F }, // mp_hotel / shrp
    { (int)0x61430EDD, (int)0xA79BB2BD, 0xAF86F6E3, 0x00081AB1 }, // mp_outskirts / tdm
    { (int)0x61430EDD, (int)0xDD6F4A20, 0xBD180B10, 0x0008197D }, // mp_outskirts / dm
    { (int)0x61430EDD, (int)0x1148A15E, 0x593B6CC2, 0x00083319 }, // mp_outskirts / sd
    { (int)0x61430EDD, (int)0x9912D511, 0x8B2DC597, 0x00083969 }, // mp_outskirts / dom
    { (int)0x61430EDD, (int)0x487AF09E, 0x8357A1FD, 0x00083349 }, // mp_outskirts / koth
    { (int)0x61430EDD, (int)0xBA5C3457, 0xED4CA01C, 0x00083879 }, // mp_outskirts / dem
    { (int)0x61430EDD, (int)0xFB399121, 0x8CD8ECC5, 0x00083D89 }, // mp_outskirts / ctf
    { (int)0x61430EDD, (int)0xA26E4A60, 0xB8554091, 0x000830CD }, // mp_outskirts / sab
    { (int)0x61430EDD, (int)0xEB51AAE5, 0xB68244E9, 0x00081E4D }, // mp_outskirts / gun
    { (int)0x61430EDD, (int)0x72613F6D, 0xE669C8A4, 0x00081C59 }, // mp_outskirts / hlnd
    { (int)0x61430EDD, (int)0xCD71DB69, 0x2D28E003, 0x00081E31 }, // mp_outskirts / oic
    { (int)0x61430EDD, (int)0x7901E0EE, 0x548DB28E, 0x0008247D }, // mp_outskirts / shrp
    { (int)0x0444CB52, (int)0xA79BB2BD, 0xE1E90FFB, 0x0007D9E9 }, // mp_zoo / tdm
    { (int)0x0444CB52, (int)0xDD6F4A20, 0x8F8BC42E, 0x0007D8B5 }, // mp_zoo / dm
    { (int)0x0444CB52, (int)0x1148A15E, 0xF846D83C, 0x0007F251 }, // mp_zoo / sd
    { (int)0x0444CB52, (int)0x9912D511, 0x8135A2C7, 0x0007F8A1 }, // mp_zoo / dom
    { (int)0x0444CB52, (int)0x487AF09E, 0x9DF01DA1, 0x0007F281 }, // mp_zoo / koth
    { (int)0x0444CB52, (int)0xBA5C3457, 0xF7BDD5A2, 0x0007F7B1 }, // mp_zoo / dem
    { (int)0x0444CB52, (int)0xFB399121, 0x190829D9, 0x0007FCC1 }, // mp_zoo / ctf
    { (int)0x0444CB52, (int)0xA26E4A60, 0x4AA3248D, 0x0007F005 }, // mp_zoo / sab
    { (int)0x0444CB52, (int)0xEB51AAE5, 0x8CF5A535, 0x0007DD85 }, // mp_zoo / gun
    { (int)0x0444CB52, (int)0x72613F6D, 0xA3E0CC1A, 0x0007DB91 }, // mp_zoo / hlnd
    { (int)0x0444CB52, (int)0xCD71DB69, 0x2B08C2DB, 0x0007DD69 }, // mp_zoo / oic
    { (int)0x0444CB52, (int)0x7901E0EE, 0xF379A0F0, 0x0007E3B5 }, // mp_zoo / shrp
    { (int)0x427094CA, (int)0xA79BB2BD, 0x0C5E69C8, 0x0007F96A }, // mp_drivein / tdm
    { (int)0x427094CA, (int)0xDD6F4A20, 0x532D193B, 0x0007F836 }, // mp_drivein / dm
    { (int)0x427094CA, (int)0x1148A15E, 0x7BBAF9C9, 0x000811D2 }, // mp_drivein / sd
    { (int)0x427094CA, (int)0x9912D511, 0xCF9A6994, 0x00081822 }, // mp_drivein / dom
    { (int)0x427094CA, (int)0x487AF09E, 0x8E421DEE, 0x00081202 }, // mp_drivein / koth
    { (int)0x427094CA, (int)0xBA5C3457, 0xF8DCADAF, 0x00081732 }, // mp_drivein / dem
    { (int)0x427094CA, (int)0xFB399121, 0x7C565C26, 0x00081C42 }, // mp_drivein / ctf
    { (int)0x427094CA, (int)0xA26E4A60, 0xDE8289DA, 0x00080F86 }, // mp_drivein / sab
    { (int)0x427094CA, (int)0xEB51AAE5, 0xCCD2A082, 0x0007FD06 }, // mp_drivein / gun
    { (int)0x427094CA, (int)0x72613F6D, 0xCD674627, 0x0007FB12 }, // mp_drivein / hlnd
    { (int)0x427094CA, (int)0xCD71DB69, 0x7B3E64A8, 0x0007FCEA }, // mp_drivein / oic
    { (int)0x427094CA, (int)0x7901E0EE, 0x835B757D, 0x00080336 }, // mp_drivein / shrp
    { (int)0x5F684D19, (int)0xA79BB2BD, 0x4C0F1D5C, 0x00082334 }, // mp_area51 / tdm
    { (int)0x5F684D19, (int)0xDD6F4A20, 0xE209814F, 0x00082200 }, // mp_area51 / dm
    { (int)0x5F684D19, (int)0x1148A15E, 0xB85B06DD, 0x00083B9C }, // mp_area51 / sd
    { (int)0x5F684D19, (int)0x9912D511, 0xAA8CEF28, 0x000841EC }, // mp_area51 / dom
    { (int)0x5F684D19, (int)0x487AF09E, 0x4BD81A82, 0x00083BCC }, // mp_area51 / koth
    { (int)0x5F684D19, (int)0xBA5C3457, 0xBA8BA3C3, 0x000840FC }, // mp_area51 / dem
    { (int)0x5F684D19, (int)0xFB399121, 0x80B4ECBA, 0x0008460C }, // mp_area51 / ctf
    { (int)0x5F684D19, (int)0xA26E4A60, 0x0A07886E, 0x00083950 }, // mp_area51 / sab
    { (int)0x5F684D19, (int)0xEB51AAE5, 0xD55B5B16, 0x000826D0 }, // mp_area51 / gun
    { (int)0x5F684D19, (int)0x72613F6D, 0x722C303B, 0x000824DC }, // mp_area51 / hlnd
    { (int)0x5F684D19, (int)0xCD71DB69, 0x0344683C, 0x000826B4 }, // mp_area51 / oic
    { (int)0x5F684D19, (int)0x7901E0EE, 0x1266F091, 0x00082D00 }, // mp_area51 / shrp
    { (int)0xECC6B487, (int)0xA79BB2BD, 0x87FC8839, 0x0007B40C }, // mp_golfcourse / tdm
    { (int)0xECC6B487, (int)0xDD6F4A20, 0xC123E066, 0x0007B2D8 }, // mp_golfcourse / dm
    { (int)0xECC6B487, (int)0x1148A15E, 0x46A8AA18, 0x0007CC74 }, // mp_golfcourse / sd
    { (int)0xECC6B487, (int)0x9912D511, 0x185466ED, 0x0007D2C4 }, // mp_golfcourse / dom
    { (int)0xECC6B487, (int)0x487AF09E, 0x4500BB53, 0x0007CCA4 }, // mp_golfcourse / koth
    { (int)0xECC6B487, (int)0xBA5C3457, 0x5E026572, 0x0007D1D4 }, // mp_golfcourse / dem
    { (int)0xECC6B487, (int)0xFB399121, 0xA657A61B, 0x0007D6E4 }, // mp_golfcourse / ctf
    { (int)0xECC6B487, (int)0xA26E4A60, 0xE234E9E7, 0x0007CA28 }, // mp_golfcourse / sab
    { (int)0xECC6B487, (int)0xEB51AAE5, 0xADB2CE3F, 0x0007B7A8 }, // mp_golfcourse / gun
    { (int)0xECC6B487, (int)0x72613F6D, 0x97582DFA, 0x0007B5B4 }, // mp_golfcourse / hlnd
    { (int)0xECC6B487, (int)0xCD71DB69, 0xE84CF159, 0x0007B78C }, // mp_golfcourse / oic
    { (int)0xECC6B487, (int)0x7901E0EE, 0x38C3DFE4, 0x0007BDD8 }, // mp_golfcourse / shrp
    { (int)0xEA52B87A, (int)0xA79BB2BD, 0x81DBBBF5, 0x0007DA08 }, // mp_silo / tdm
    { (int)0xEA52B87A, (int)0xDD6F4A20, 0x689B9728, 0x0007D8D4 }, // mp_silo / dm
    { (int)0xEA52B87A, (int)0x1148A15E, 0x86D00136, 0x0007F270 }, // mp_silo / sd
    { (int)0xEA52B87A, (int)0x9912D511, 0xAED6EAC1, 0x0007F8C0 }, // mp_silo / dom
    { (int)0xEA52B87A, (int)0x487AF09E, 0x573D579B, 0x0007F2A0 }, // mp_silo / koth
    { (int)0xEA52B87A, (int)0xBA5C3457, 0xD4024C9C, 0x0007F7D0 }, // mp_silo / dem
    { (int)0xEA52B87A, (int)0xFB399121, 0xD806BBD3, 0x0007FCE0 }, // mp_silo / ctf
    { (int)0xEA52B87A, (int)0xA26E4A60, 0xCB2A9A87, 0x0007F024 }, // mp_silo / sab
    { (int)0xEA52B87A, (int)0xEB51AAE5, 0xBA7D232F, 0x0007DDA4 }, // mp_silo / gun
    { (int)0xEA52B87A, (int)0x72613F6D, 0xFD67DB14, 0x0007DBB0 }, // mp_silo / hlnd
    { (int)0xEA52B87A, (int)0xCD71DB69, 0x41BACED5, 0x0007DD88 }, // mp_silo / oic
    { (int)0xEA52B87A, (int)0x7901E0EE, 0x61A1ADEA, 0x0007E3D4 }, // mp_silo / shrp
};

const ChecksumEntry *checksum_lookup(bool multiplayer, const char *mapname, const char *gametype)
{
    const ChecksumEntry *table = multiplayer ? Table_MP : Table_SP;
    unsigned int count = multiplayer ? sizeof(Table_MP) / sizeof(Table_MP[0]) : sizeof(Table_SP) / sizeof(Table_SP[0]);
    int mapHash = jenkins_hash(mapname);
    int gametypeHash = jenkins_hash(gametype);

    for (unsigned int i = 0; i < count; i++)
    {
        if (table[i].mapHash == mapHash && table[i].gametypeHash == gametypeHash)
            return &table[i];
    }
    return NULL;
}
