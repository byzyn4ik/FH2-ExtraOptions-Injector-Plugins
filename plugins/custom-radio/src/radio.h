// Custom Radio for Forza Horizon 2 Extra Options: the radio station.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CUSTOM_RADIO_RADIO_H
#define CUSTOM_RADIO_RADIO_H
#include "config.h"

// Checks and installs the hooks for `config` (CustomRadio.ini). 0 when the
// station's game code did not match (nothing of the station is installed).
int radio_install(unsigned char *image, const RadioConfig *config);
// 1 once anything is installed (the station or only the station-lock hook).
int radio_installed(void);
// Game thread, once per local-car update. `pressed`: the actions whose key
// was just pressed (bit KEY_*); `enabled`: gameplay input and window focus.
// RadioStation tunes in here; RadioNext/RadioRestart are handed to the
// radio thread, which acts on them while the custom station is playing.
void radio_game_tick(unsigned pressed, int enabled);
// The default station names by language: MY MUSIC, MOJA MUZYKA (Polish game
// text), МОЯ МУЗИКА (Language Support's Ukrainian pack in the ru-RU slot).
enum { RADIO_NAME_ENGLISH, RADIO_NAME_POLISH, RADIO_NAME_UKRAINIAN, RADIO_NAME_LANGUAGES };
// The station's name as it is written into RadioInfo (and so the name the HUD
// binds to): `configured` (Name=), else the default of `language`
// (RADIO_NAME_*), with '"', '<' and '>' replaced and control characters
// dropped. Empty when it does not fit.
void radio_station_name(const char *configured, int language, char *output, size_t capacity);
// The name the station was given in RadioInfo (XML-escaped); empty before.
const char *radio_given_name(void);
#endif
