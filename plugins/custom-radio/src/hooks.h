// Custom Radio for Forza Horizon 2 Extra Options: guarded, chaining hooks.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CUSTOM_RADIO_HOOKS_H
#define CUSTOM_RADIO_HOOKS_H
#include "sites.h"
#include "support.h"

// Every hook is checked byte for byte before anything is written:
//  - the hooked function's first bytes (sites.h) must be the game's;
//  - a virtual-table slot must hold that function, or code outside it that
//    another mod put there (then the plugin calls that code instead: chained);
//  - a rel32 call must be E8 between the game's 8 bytes before and after it,
//    calling the function or other code (chained the same way).
// A group of sites is written only when all of them pass, through a
// compare-and-swap of the exact value that was checked; a failed write puts
// back what the group already changed. Calls go through 14-byte jumps in a
// page of our own near the game image (no int3 caves of the game are used).

// The games the plugin knows: Forza Horizon 2 1.0.0.64 (sites.h; its image
// size decides, as before) and Forza Horizon 2 Presents Fast & Furious 1.0.0.6
// (ff_sites.h, image size and TimeDateStamp). Everything game-specific the
// plugin reads (sites, globals, virtual tables) comes from the game selected.
typedef struct {
    const char *name;
    uint32_t image_size, stamp;  // stamp 0: any (Forza Horizon 2: the size decides)
    int fast_furious;
    uint32_t radio_system_global, audio_manager, module_manager_global;
    uint32_t radio_system_vtable, station_vtable, playlist_vtable, player_vtable;
    uint32_t code_end;            // a playlist item's methods lie below this RVA
    uint32_t text_tables;         // the text table manager (common/include/fh2_game.h FH2_TEXT_TABLES)
    uint32_t extra_options_thunk; // Extra Options 0.4.4's resource thunk (0: none in this game)
} RadioGame;

// The game of this image (size and PE TimeDateStamp), selected for the hooks; NULL when not known.
const RadioGame *hooks_select(const unsigned char *image, uint32_t image_size);
const RadioGame *hooks_game(void);
// The image's PE TimeDateStamp (0 when unreadable).
uint32_t hooks_image_stamp(const unsigned char *image);

void hooks_bind(unsigned char *image);
unsigned char *hooks_image(void);
// The function's first bytes are the game's.
int hooks_function_ok(unsigned function);
const char *hooks_function_name(unsigned function);
void *hooks_function(unsigned function);

typedef struct {
    unsigned index;  // SLOT_* or CALL_*
    int call;        // 0 slot, 1 call
    void *current;   // the code there now (game function or another hook): the chain target
    void *replacement;
    int chained, installed;
} Hook;

// Checks the sites and fills `current`; logs the reason and returns 0 on a mismatch.
int hooks_prepare_slot(Hook *hook, unsigned slot);
int hooks_prepare_call(Hook *hook, unsigned call);
// Writes the prepared hooks (all or none).
int hooks_commit(Hook *const *hooks, unsigned count);
// One pointer at image + rva swapped from `expected` to `value` (interlocked); 0 if it was not `expected`.
int hooks_swap_pointer(uint32_t rva, void *expected, void *value);
const char *hooks_site_name(const Hook *hook);
#endif
