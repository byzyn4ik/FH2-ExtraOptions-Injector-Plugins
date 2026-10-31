// Custom Radio for Forza Horizon 2 Extra Options: the station's logo on the HUD.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CUSTOM_RADIO_STATION_LOGO_H
#define CUSTOM_RADIO_STATION_LOGO_H
#include "config.h"

// The plugin's media folder (mod.json "media" and FH2PluginMediaDirectory):
// <plugins>\CustomRadio\Media, a game media tree; the logos are in
// UI\Textures\Anthem\HUD beside the game's own station logos.
#define LOGO_MEDIA_FOLDER L"CustomRadio\\Media"
#define LOGO_TEXTURE_FOLDER L"UI\\Textures\\Anthem\\HUD"
#define LOGO_TEXTURE_URI "/ForzaUI;component/Textures/Anthem/HUD/"
#define LOGO_MARK "<!-- Custom Radio: the custom station's logo -->"

// The station names and the logo each shows: the built-in logos (MY MUSIC,
// MOJA MUZYKA, or the badge alone for another Name=), or Logo= (a PNG made
// into HUD_RadioIcon_CustomRadio_User in the media folder). Logs what it
// uses and why a Logo= file is refused. Call before station_logo_install.
// `media_folder`: the folder the game reads the mod's media from (Forza Horizon
// 2: <plugins>\CustomRadio\Media; Fast & Furious: <mod>\media, which Language
// Support mounts there).
void station_logo_prepare(const wchar_t *plugins_directory, const wchar_t *media_folder, const RadioConfig *config);
// Chains the XAML resource call: the HUD's radio control gets one trigger per
// station name that shows that name's logo (else the game draws a white box).
// 0 when the call or the functions it needs are not as expected.
int station_logo_install(void);
// The media folder once prepared (it exists), else NULL.
const wchar_t *station_logo_media_directory(void);

// The HUD radio control's XAML with the triggers added: its new length in
// `output`, or 0 (`why` NULL: another file; else what did not match).
size_t station_logo_edit(const char *xaml, size_t length, char *output, size_t capacity, const char **why);
#endif
