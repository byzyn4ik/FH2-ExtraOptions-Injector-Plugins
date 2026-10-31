// Azimuth Settings (Extra Options / forza_injector plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.h"

#include "common.h"
#include "fh2_game.h"

namespace azs {
namespace language {

namespace {

Choice forced = kAuto;

} // namespace

Choice parse(const std::wstring &text) {
    if (!lstrcmpiW(text.c_str(), L"en")) return kForceEnglish;
    if (!lstrcmpiW(text.c_str(), L"pl")) return kForcePolish;
    if (!lstrcmpiW(text.c_str(), L"uk")) return kForceUkrainian;
    return kAuto;
}

void configure(Choice choice) {
    forced = choice;
    if (choice != kAuto)
        say(std::string("captions: ") + (choice == kForcePolish ? "Polish" : choice == kForceUkrainian ? "Ukrainian" : "English") +
            " ([Captions] Language)");
}

std::string game_code(const unsigned char *game) {
    char code[4];
    return std::string(code, fh2_text_language(game, code));
}

Language current(const unsigned char *game) {
    switch (forced) {
    case kForceEnglish: return kEnglish;
    case kForcePolish: return kPolish;
    case kForceUkrainian: return kUkrainian;
    case kAuto: break;
    }
    std::string code = game_code(game);
    if (code == "PL") return kPolish;
    if (code == "UK") return kUkrainian;
    return kEnglish;
}

#ifdef AZS_TEST_EXPORTS
void test_set(Choice choice) { forced = choice; }
#endif

} // namespace language
} // namespace azs
