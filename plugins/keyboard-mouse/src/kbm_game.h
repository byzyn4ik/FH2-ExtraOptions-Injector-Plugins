// Keyboard & Mouse: which game build the game-level features run on.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game-side sources name Forza Horizon 2 1.0.0.64's addresses (RVAs of
// forza_durango_release_final.exe) and struct layout. Every address goes
// through KBM_RVA(): on Forza Horizon 2 it is the address itself; on another
// supported build (Forza Horizon 2 Presents Fast & Furious 1.0.0.6) it is the
// same site found there by tools/port_sites.py (src/ff_sites.inc), or 0 when
// it was not found (the guard of whatever needs it then refuses, and the log
// says so). Guards compare code through kbm_site_equal(): the Forza Horizon 2
// bytes, except where a field is relocated (rip displacements, rel32
// targets), which may differ in the other build. The few layout and action
// numbers that differ are in KbmGame.
#ifndef KBM_GAME_H
#define KBM_GAME_H
#include "common.h"

typedef struct {
    uint32_t fh2, game;                 // the site in Forza Horizon 2 and in this game
    uint16_t mask_first, mask_count;    // its relocated byte offsets in masks[]
} KbmSitePair;

typedef struct {
    const char *name;
    uint32_t image_size, stamp;
    int is_fh2;
    const KbmSitePair *pairs;           // sorted by fh2; NULL on Forza Horizon 2
    unsigned pair_count;
    const unsigned char *masks;
    // CControllerManager: the controller objects' size, and how far the
    // manager's fields after them (any controller, action tables, selected
    // controller, repeat timing, pad activity) moved.
    uint32_t controller_stride, manager_shift;
    // Game action numbers: this game's numbering inserts `inserted_actions`
    // new actions at `first_inserted_action` (Fast & Furious: one at 26).
    unsigned first_inserted_action, inserted_actions;
} KbmGame;

extern const KbmGame *kbm_game;
extern const KbmGame kbm_game_fh2, kbm_game_ff;

// The supported build with this image size and TimeDateStamp, or NULL.
const KbmGame *kbm_game_find(uint32_t image_size, uint32_t stamp);
void kbm_game_select(const KbmGame *game);

// The game's address of a Forza Horizon 2 site (0: not found in this game).
uint32_t kbm_rva(uint32_t fh2_rva);
#define KBM_RVA(rva) kbm_rva(rva)
#define KBM_IMAGE_SIZE (kbm_game->image_size)
#define KBM_SHIFT(offset) ((offset) + kbm_game->manager_shift)

// The code at image + rva (an address KBM_RVA gave) is the Forza Horizon 2
// code `expected` (the same instructions; relocated fields may differ).
int kbm_site_equal(const unsigned char *image, uint32_t rva, const void *expected, size_t size);

// Action numbers: Forza Horizon 2's for this game's (KBM_NO_ACTION for an
// action Forza Horizon 2 does not have), and back.
#define KBM_NO_ACTION 0xffffu
unsigned kbm_action_from_game(unsigned action);
unsigned kbm_action_to_game(unsigned action);
#endif
