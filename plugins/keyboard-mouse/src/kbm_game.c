// Keyboard & Mouse: which game build the game-level features run on (see kbm_game.h).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "kbm_game.h"

#include "ff_sites.inc"

const KbmGame kbm_game_fh2 = {
    "Forza Horizon 2 1.0.0.64", 0x3a18000u, 0x565501edu, 1, NULL, 0, NULL, 0x1300u, 0, 0xffffu, 0,
};

/* Fast & Furious 1.0.0.6: CControllerManager's controllers are 0x1480 bytes
   (0x1300 in FH2), so its fields after the eight controllers and the any
   controller sit 9 * 0x180 = 0xd80 later (update 0x109b950: imul 0x1480,
   [r13+0xb918]; queries: [rax+0xb890] action tables, [rax+0xb8c0/0xb8c4]
   repeat timing). Its action registration (0xf9e6c0) is FH2's (0xf78330)
   with one racing action added at 26 (trigger type 4), so FH2's actions 26
   and up are one higher. */
const KbmGame kbm_game_ff = {
    "Forza Horizon 2 Presents Fast & Furious 1.0.0.6", 0x3a89000u, 0x55b62fdeu, 0,
    ff_site_pairs, ARRAY_COUNT(ff_site_pairs), ff_site_masks, 0x1480u, 0xd80u, 26, 1,
};

const KbmGame *kbm_game = &kbm_game_fh2;

const KbmGame *kbm_game_find(uint32_t image_size, uint32_t stamp) {
    if (image_size == kbm_game_fh2.image_size) return &kbm_game_fh2;  // as before: the size decides
    if (image_size == kbm_game_ff.image_size && stamp == kbm_game_ff.stamp) return &kbm_game_ff;
    return NULL;
}

void kbm_game_select(const KbmGame *game) {
    kbm_game = game ? game : &kbm_game_fh2;
}

static const KbmSitePair *pair_of(uint32_t fh2_rva) {
    unsigned low = 0, high = kbm_game->pair_count;
    while (low < high) {
        unsigned middle = (low + high) / 2;
        if (kbm_game->pairs[middle].fh2 < fh2_rva) low = middle + 1;
        else high = middle;
    }
    return low < kbm_game->pair_count && kbm_game->pairs[low].fh2 == fh2_rva ? &kbm_game->pairs[low] : NULL;
}

uint32_t kbm_rva(uint32_t fh2_rva) {
    if (kbm_game->is_fh2) return fh2_rva;
    const KbmSitePair *pair = pair_of(fh2_rva);
    return pair ? pair->game : 0;
}

int kbm_site_equal(const unsigned char *image, uint32_t rva, const void *expected, size_t size) {
    if (!rva) return 0;
    if (kbm_game->is_fh2) return memory_equal(image + rva, expected, size);
    /* The ported site at or closest before rva (a guard may start inside one). */
    const KbmSitePair *pair = NULL;
    for (unsigned i = 0; i < kbm_game->pair_count; ++i) {
        const KbmSitePair *p = &kbm_game->pairs[i];
        if (p->game <= rva && rva - p->game < 256 && (!pair || p->game > pair->game)) pair = p;
    }
    unsigned delta = pair ? rva - pair->game : 0;
    if (!pair || size + delta > 256) return 0;
    unsigned char actual[256];
    if (!memory_read(image + rva, actual, size)) return 0;
    const unsigned char *want = (const unsigned char *)expected;
    const unsigned char *mask = kbm_game->masks + pair->mask_first;
    unsigned next = 0;
    for (size_t k = 0; k < size; ++k) {
        while (next < pair->mask_count && mask[next] < k + delta) ++next;
        if (next < pair->mask_count && mask[next] == k + delta) continue;
        if (actual[k] != want[k]) return 0;
    }
    return 1;
}

unsigned kbm_action_from_game(unsigned action) {
    unsigned first = kbm_game->first_inserted_action, count = kbm_game->inserted_actions;
    if (!count || action < first) return action;
    return action < first + count ? KBM_NO_ACTION : action - count;
}

unsigned kbm_action_to_game(unsigned action) {
    unsigned first = kbm_game->first_inserted_action, count = kbm_game->inserted_actions;
    return !count || action < first ? action : action + count;
}

#ifdef KBM_TEST
/* Offline: the build the plugin-level code assumes (0 Forza Horizon 2, 1 Fast & Furious). */
__declspec(dllexport) void KbmTestSelectGame(int fast_and_furious) {
    kbm_game_select(fast_and_furious ? &kbm_game_ff : &kbm_game_fh2);
}
#endif
