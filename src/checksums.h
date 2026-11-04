#ifndef CHECKSUMS_H
#define CHECKSUMS_H

#include <stdbool.h>

/*
    Precomputed GSC checksums of the unmodified scripts, taken from InfinityOps.
*/

typedef struct ChecksumEntry {
    int mapHash;
    int gametypeHash;
    // Written to scrChecksum_t; substract is set to 0 (as InfinityOps sends it).
    unsigned int checksum;
    unsigned int programLen;
} ChecksumEntry;

int jenkins_hash(const char* key);
const ChecksumEntry* checksum_lookup(bool multiplayer, const char* mapname,
                                     const char* gametype);

#endif
