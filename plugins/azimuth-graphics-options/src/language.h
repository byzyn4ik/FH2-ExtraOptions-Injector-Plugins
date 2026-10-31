// Azimuth Graphics Options for Forza Horizon 2 (Extra Options plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The language of the plugin's captions:
//   - Polish while the game's text is Polish (its text tables' code "PL");
//   - Ukrainian while a Language Support pack with id "uk" fills the game's
//     text slot (the Ukrainian pack in ru-RU): fh2_text_language in
//     common/include/fh2_game.h, shared by every plugin;
//   - English otherwise.
// The plugin's INI can force one: [Captions] Language=en, pl or uk.
#pragma once
#include <string>

#include "rows.h"

namespace azg {
namespace language {

enum Choice { kAuto, kForceEnglish, kForcePolish, kForceUkrainian };

// "auto", "en", "pl", "uk"; anything else is kAuto.
Choice parse(const std::wstring &text);

// The [Captions] Language choice (logged when it forces one).
void configure(Choice choice);

// The language for the game's current text language.
Language current(const unsigned char *game_image);
// The captions' language code ("PL", "RU", "UK" with the Ukrainian pack, ...),
// empty when unreadable.
std::string game_code(const unsigned char *game_image);

#ifdef AZG_TEST_EXPORTS
void test_set(Choice choice);
#endif

} // namespace language
} // namespace azg
