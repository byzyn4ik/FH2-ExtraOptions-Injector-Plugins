// Keyboard & Mouse: host-level pad emulation for games other than Forza
// Horizon 2 1.0.0.64 (Fast & Furious and any other title on Azimuth).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Only Azimuth's own sites are used (azimuth.exe 1.0.4 / 1.0.2): its
// Windows.Xbox.Input.IGamepad implementation's GetCurrentReading and
// GetRawCurrentReading vtable slots are chained, and the keys held while the
// game window has the focus are merged into every reading the game gets, as
// the pad buttons, triggers and left stick the bindings name. No game code is
// patched, so the game's own menus, prompts and rewind behave as with a pad.
#ifndef KBM_HOST_PAD_H
#define KBM_HOST_PAD_H
#include "kbm_config.h"

// 1 when installed. Logs why not otherwise.
int host_pad_install(const KbmSettings *settings);

#ifdef KBM_TEST
// Merges the keys `down` (256 scan codes, 1 = held) into a 0x28-byte reading
// as the hook does, `driving` forcing the shared keys' group; dt in seconds.
__declspec(dllexport) void host_pad_test_merge(const KbmSettings *settings, const unsigned char *down, int driving, float dt,
                         unsigned char *reading);
#endif
#endif
