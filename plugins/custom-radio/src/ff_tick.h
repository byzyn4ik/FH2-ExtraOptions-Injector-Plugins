// Custom Radio: the key tick on Forza Horizon 2 Presents Fast & Furious.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CUSTOM_RADIO_FF_TICK_H
#define CUSTOM_RADIO_FF_TICK_H
#include "support.h"

// Called on the game thread once per local-car update with the selected pad's buttons.
typedef void (*FfTickFn)(unsigned buttons);

// Hooks Fast & Furious' ForzaCar update slot (checked first); 0 and a log line when it does not match.
int ff_tick_install(unsigned char *image, FfTickFn callback);
// The selected controller's buttons (Extra Options' FH2PluginVehicleFrame::buttons), 0 when unreadable.
unsigned ff_tick_pad_buttons(const unsigned char *image);
#endif
