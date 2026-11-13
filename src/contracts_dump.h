#ifndef CONTRACTS_DUMP_H
#define CONTRACTS_DUMP_H

#include "defines.h"
#include "offsets.h"

t5nhd(void, Contracts_ParseFile, (const char* text));

// Installs the hook that writes the contracts.info text from the online WAD to
// SCRIPTS_PATH "/contracts.info" whenever the game parses it (MP only). The Stats Viewer
// takes the contract names from that file.
int contracts_dump_init();

#endif
