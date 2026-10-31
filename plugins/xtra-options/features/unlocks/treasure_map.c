// Xtra Options, Unlocks.dll: the Treasure Map's board callback, ported from
// Forza Horizon 2 Extra Options 0.4.4 (features/treasure_map.c, d66c85d,
// GPL-3.0, Arthur (RokkuDayo)); the same RVAs, offsets and slot bytes.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TREASURE_MAP's byte patches (features/unlocks/patches.json) point the
// board-setup binding at an 8-byte slot in unused padding (0x4028f8); the slot
// gets the address of treasure_map_setup_board, which runs the game's board
// setup and then discovers a board the game left undiscovered.
#include "forza_patch.h"

#include "feature_code.h"

#define BOARD_SETUP_RVA 0x50b7c0u
#define BOARD_DISCOVER_RVA 0x4fe910u
#define BOARD_CALLBACK_SLOT_RVA 0x4028f8u
#define BOARD_STATE_OFFSET 0x100u
#define BOARD_POSITION_OFFSET 0x128u

typedef void (*BoardSetupFn)(void *board);
typedef void (*BoardDiscoverFn)(void *board, int state, const void *position);

static BoardSetupFn original_board_setup;
static BoardDiscoverFn discover_board;
static unsigned char callback_pointer[8];
static const unsigned char unused_slot[8] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_slot[] = {unused_slot, callback_pointer};

static void treasure_map_setup_board(void *board) {
    original_board_setup(board);
    unsigned char *object = (unsigned char *)board;
    if (*(int *)(object + BOARD_STATE_OFFSET) == 0) {
        discover_board(board, 1, object + BOARD_POSITION_OFFSET);
    }
}

static const ForzaPatch treasure_map_hook_patch = {"treasure_map_board_setup_callback", BOARD_CALLBACK_SLOT_RVA, 8,
                                                   callback_pointer, accepted_slot, 2};

void feature_prepare(unsigned char *game_image) {
    original_board_setup = (BoardSetupFn)(game_image + BOARD_SETUP_RVA);
    discover_board = (BoardDiscoverFn)(game_image + BOARD_DISCOVER_RVA);
    uintptr_t callback = (uintptr_t)treasure_map_setup_board;
    for (unsigned i = 0; i < sizeof(callback); ++i) callback_pointer[i] = (unsigned char)(callback >> (8 * i));
}

const ForzaPatch *feature_extra(const char *group, uint32_t *count) {
    const char *name = "TREASURE_MAP";
    unsigned i = 0;
    while (group[i] && group[i] == name[i]) ++i;
    if (group[i] || name[i]) {
        *count = 0;
        return 0;
    }
    *count = 1;
    return &treasure_map_hook_patch;
}
