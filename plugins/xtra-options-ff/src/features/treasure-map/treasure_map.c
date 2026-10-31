// Xtra Options (Fast & Furious), TreasureMap.dll: the boards and barn finds on
// the world map, as Forza Horizon 2's Treasure Map shows them.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Xtra Options' TREASURE_MAP callback (xtra-options/features/unlocks/
// treasure_map.c, from Extra Options 0.4.4, GPL-3.0, Arthur (RokkuDayo)) at
// Fast & Furious 1.0.0.6's sites (forza_durango_release_final.exe
// 0x55b62fde). Fast & Furious sells no Treasure Map, but its code is FH2's:
//
//   FH2                 Fast & Furious
//   0x50b7c0            0x5172d0       board setup (the board-setup binding
//                                      at 0x5056c1 stores it)
//   0x4fe910            0x50c670       board discover(board, state, position)
//   0x4028f8            0x40fca8       8 bytes of padding after the mainland
//                                      purchased getter, the callback slot
//
// The board object keeps its state at +0x100 and its position at +0x128 in
// both games (the same reads and writes in both setup and discover). The
// binding is patched to load its function from the slot (patches.json); the
// slot holds treasure_map_setup_board, which runs the game's board setup and
// then discovers a board the game left undiscovered.
#include "forza_patch.h"

#include "feature_code.h"

#define BOARD_SETUP_RVA 0x5172d0u
#define BOARD_DISCOVER_RVA 0x50c670u
#define BOARD_CALLBACK_SLOT_RVA 0x40fca8u
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
