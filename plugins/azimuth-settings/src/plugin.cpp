// Azimuth Settings: a mod-loader plugin for Extra Options and forza_injector.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installed as mods\Azimuth Settings\plugins\AzimuthSettings.dll. From
// FH2PluginInitialize (Extra Options' plugin API, which forza_injector calls
// the same way) it identifies the host (Azimuth 1.0.4 or 1.0.2) and the game
// (Forza Horizon 2 1.0.0.64 or Fast & Furious 1.0.0.6, by image size and PE
// TimeDateStamp), checks every site and Azimuth's data\azimuth.ini layout;
// then it adds Azimuth's per-title settings as rows of the game's Video page
// (frame rate, V-sync, resolution, aspect ratio, display mode, HDR, graphics
// preset and its nine details, anti-aliasing) and Audio page (audio output,
// volume, speech and text language), writes them to data\azimuth.ini under
// [title-<the game's title>] and applies what it can at once (see live.h).
// Azimuth reads the INI itself at the next start. With any mismatch it
// installs nothing and logs why; with Azimuth Graphics Options' DLL in the
// process it refuses (both would add rows for the same keys). Other games
// (Forza Motorsport 5 and 6) have no page port: it logs so and does nothing.
// Settings are in AzimuthSettings.ini beside the DLL.
#include <string>

#include "common.h"
#include "fh2_game.h"
#include "fh2_plugin.h"
#include "language.h"
#include "live.h"
#include "rows.h"
#include "settings_pages.h"

#ifdef AZS_TEST_EXPORTS
#include "call_chain.h"
#endif

#define PLUGIN_VERSION "0.0.1"
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)

#ifdef AZS_TEST_EXPORTS
static unsigned char *test_host; // the host image the test build checks instead of its own process
#endif

namespace {

std::wstring ini_path;

std::wstring folder_of(const std::wstring &path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool absolute(const std::wstring &path) {
    return (path.size() > 1 && path[1] == L':') || (!path.empty() && (path[0] == L'\\' || path[0] == L'/'));
}

std::wstring trimmed(const wchar_t *value) {
    std::wstring text = value;
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) text.pop_back();
    return text;
}

std::wstring ini_text(const wchar_t *section, const wchar_t *key, const wchar_t *fallback) {
    wchar_t value[1024];
    GetPrivateProfileStringW(section, key, fallback, value, 1024, ini_path.c_str());
    return trimmed(value);
}

bool ini_flag(const wchar_t *section, const wchar_t *key, bool fallback) {
    std::wstring value = ini_text(section, key, fallback ? L"1" : L"0");
    return !(value == L"0" || value == L"false" || value == L"False" || value == L"off" || value == L"no");
}

unsigned char *host_image() {
#ifdef AZS_TEST_EXPORTS
    if (test_host) return test_host;
#endif
    return (unsigned char *)GetModuleHandleW(nullptr);
}

std::wstring module_folder(HMODULE module) {
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(module, path, MAX_PATH * 4);
    return n && n < MAX_PATH * 4 ? folder_of(std::wstring(path, n)) : std::wstring();
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

PLUGIN_EXPORT BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    using namespace azs;
    if (!context || context->size < sizeof(FH2PluginContext) || context->api_version < 1) return FALSE;
    if (!context->plugin_path || !context->game_image) {
        set_log(context->log_utf8, L"");
        say("the loader gave no plugin path or game image; nothing is installed");
        return FALSE;
    }
    std::wstring dll = context->plugin_path;
    std::wstring ini(dll.size() + 5, L' ');
    ini_path = fh2_plugin_ini_path(dll.c_str(), &ini[0], ini.size()) ? ini.c_str() : dll + L".ini";
    set_log(context->log_utf8,
            ini_flag(L"Logging", L"Enabled", true) ? folder_of(dll) + L"\\AzimuthSettings.log" : std::wstring());
    say("plugin " PLUGIN_VERSION " under " +
        std::string(context->extra_options_version ? context->extra_options_version : "?"));
    if (pages::other_plugin_loaded()) {
        say("AzimuthGraphicsOptions.dll (Azimuth Graphics Options) is loaded: it shows rows for the same settings; "
            "install only one of the two. Nothing is installed");
        return TRUE;
    }
    bool video = ini_flag(L"Video", L"Enabled", true), audio = ini_flag(L"Audio", L"Enabled", true);
    if (!video && !audio) {
        say("[Video] and [Audio] Enabled=0; nothing is installed");
        return TRUE;
    }
    rows::enable_page(kVideoPage, video);
    rows::enable_page(kAudioPage, audio);
    if (context->adapter != FH2_PLUGIN_AZIMUTH) {
        say("the game does not run on Azimuth; nothing is installed");
        return TRUE;
    }
    unsigned char *host = host_image();
    const char *problem = nullptr;
    const live::Host *build = live::identify(host, &problem);
    if (!build) {
        say(std::string(problem) + "; nothing is installed");
        return TRUE;
    }
    say(std::string("host: Azimuth ") + live::host_name(build));
    unsigned char *game = (unsigned char *)context->game_image;
    const char *target = nullptr;
    if (const char *mismatch = pages::check_game(game, context->game_image_size, &target)) {
        say(std::string(mismatch) + "; nothing is installed and the game is left as it is");
        return TRUE;
    }
    say(std::string("game: ") + pages::game_name());

    std::wstring azimuth_ini = ini_text(L"Azimuth", L"Ini", L"");
    std::wstring launcher = module_folder(nullptr);
    if (azimuth_ini.empty())
        azimuth_ini = launcher.empty() ? std::wstring() : launcher + L"\\data\\azimuth.ini";
    else if (!absolute(azimuth_ini) && !launcher.empty())
        azimuth_ini = launcher + L"\\" + azimuth_ini;
    // The running game's own title, not [launcher] game (the game Azimuth's
    // launcher had selected; --game= can start another one).
    rows::set_title(pages::title_id());
    int rate = live::title_rate(build, host);
    rows::set_title_rate(rate ? rate : 30);
    rows::set_file(azimuth_ini);
    std::string layout = rows::check_layout();
    if (!layout.empty()) {
        say(layout + "; nothing is installed");
        return TRUE;
    }
    if (!azimuth_ini.empty()) {
        wchar_t selected[32];
        GetPrivateProfileStringW(L"launcher", L"game", L"", selected, 32, azimuth_ini.c_str());
        if (selected[0] && lstrcmpiW(selected, pages::title_id()))
            say("[launcher] game is " + narrow(selected) + "; the rows write [" + narrow(rows::section()) +
                "], the running game's");
    }
    say("settings in [" + narrow(rows::section()) + "]; the title's own frame rate " +
        std::to_string(rate ? rate : 30) + (rate ? " FPS (Azimuth's)" : " FPS (Azimuth's rate setter not read)"));
    language::configure(language::parse(ini_text(L"Captions", L"Language", L"auto")));

    bool apply_live = ini_flag(L"Azimuth", L"LiveApply", true);
    LiveState text = {};
    if (apply_live) {
        live::prepare(build, host, game, pages::shadows());
        text = live::state();
    } else {
        say("[Azimuth] LiveApply=0: every change applies after a restart");
    }
    if (!pages::install(game, text, apply_live)) return TRUE;
    say(std::string(video && audio ? "Video and Audio page rows" : video ? "Video page rows" : "Audio page rows") +
        " ready; settings in " + narrow(azimuth_ini) + "; the resource call at " + hex(pages::resource_call()) +
        " is chained to " + (target ? target : "?"));
    return TRUE;
}

#ifdef AZS_TEST_EXPORTS
// Test build only (out\tests\AzimuthSettingsTest.dll): the same code with
// entry points for tests\run_tests.py and tests\plugin_test.cpp.
PLUGIN_EXPORT void AzsTestUseHost(unsigned char *host) { test_host = host; }

// nullptr when `host` is a known build, else why not.
PLUGIN_EXPORT const char *AzsTestCheckHost(const unsigned char *host) {
    const char *why = nullptr;
    return azs::live::identify(host, &why) ? nullptr : why;
}
PLUGIN_EXPORT const char *AzsTestHostName(const unsigned char *host) {
    const char *why = nullptr;
    const azs::live::Host *build = azs::live::identify(host, &why);
    return build ? azs::live::host_name(build) : nullptr;
}
PLUGIN_EXPORT uint32_t AzsTestHostSite(const unsigned char *host, unsigned which) {
    const char *why = nullptr;
    return azs::live::test_site(azs::live::identify(host, &why), which);
}

PLUGIN_EXPORT const char *AzsTestCheckGame(const unsigned char *game, uint32_t size) {
    const char *target = nullptr;
    const char *problem = azs::pages::check_game(game, size, &target);
    return problem ? problem : target;
}

// live: bit 0 frame rate, 1 display, 2 shadow distance, 3 shadow rate, 4 volume.
PLUGIN_EXPORT size_t AzsTestExtendXaml(int page, const char *text, size_t length, char *output, size_t capacity,
                                       int language, int live) {
    azs::LiveState state = {(live & 1) != 0, (live & 2) != 0, (live & 4) != 0, (live & 8) != 0, (live & 16) != 0};
    const char *why = nullptr;
    azs::Language lang = (azs::Language)(language >= 0 && language <= 2 ? language : 0);
    return page ? azs::pages::extend_audio(text, length, output, capacity, lang, state, &why)
                : azs::pages::extend_video(text, length, output, capacity, lang, state, &why);
}

PLUGIN_EXPORT const char *AzsTestGameId() { return azs::pages::game_id(); }
PLUGIN_EXPORT const wchar_t *AzsTestTitleId() { return azs::pages::title_id(); }
// The build's shadow sites: 0 cascades, 1 renderer store, 2 renderer pointer,
// 3 renderer vtable, 4 rate read.
PLUGIN_EXPORT uint32_t AzsTestShadowSite(unsigned which) {
    azs::live::GameShadows s = azs::pages::shadows();
    const uint32_t sites[] = {s.cascades, s.renderer_store, s.renderer_pointer, s.renderer_vtable, s.rate_read};
    return which < 5 ? sites[which] : 0;
}
PLUGIN_EXPORT uint32_t AzsTestBuildSite(unsigned kind, unsigned index) {
    return azs::pages::test_build_site(kind, index);
}
PLUGIN_EXPORT int AzsTestHostTitleRate(const unsigned char *host) {
    const char *why = nullptr;
    return azs::live::title_rate(azs::live::identify(host, &why), host);
}
PLUGIN_EXPORT void AzsTestOtherPlugin(int loaded) { azs::pages::test_other_plugin(loaded); }

PLUGIN_EXPORT void AzsTestRowsTitle(const wchar_t *title, int rate) {
    azs::rows::set_title(title);
    azs::rows::set_title_rate(rate);
}
PLUGIN_EXPORT void AzsTestRowsFile(const wchar_t *path) { azs::rows::set_file(path); }
PLUGIN_EXPORT int AzsTestDetailLevel(unsigned detail) { return azs::rows::detail_level(detail); }
PLUGIN_EXPORT int AzsTestPresetDetail(unsigned level, unsigned detail) { return azs::rows::preset_detail(level, detail); }
PLUGIN_EXPORT const wchar_t *AzsTestRowKey(unsigned row) { return azs::rows::key(row); }
PLUGIN_EXPORT const char *AzsTestRowsLayout() {
    static std::string text;
    text = azs::rows::check_layout();
    return text.c_str();
}
PLUGIN_EXPORT int AzsTestRowGet(unsigned row) { return azs::rows::get(row); }
PLUGIN_EXPORT int AzsTestRowSet(unsigned row, int value) { return azs::rows::set(row, value); }
PLUGIN_EXPORT int AzsTestRowShown(unsigned row) { return azs::rows::shown(row); }

PLUGIN_EXPORT int AzsTestFrameRates(unsigned refresh, int *out, int capacity) {
    std::vector<int> rates = azs::live::frame_rates(refresh);
    for (size_t i = 0; i < rates.size() && (int)i < capacity; ++i) out[i] = rates[i];
    return (int)rates.size();
}
PLUGIN_EXPORT void AzsTestSetFrameRates(const int *rates, int count) {
    azs::rows::set_frame_rates(std::vector<int>(rates, rates + count));
}
PLUGIN_EXPORT const wchar_t *AzsTestRowChoice(unsigned row, unsigned index, int language) {
    return azs::rows::choice(row, index, (azs::Language)(language >= 0 && language <= 2 ? language : 0));
}
PLUGIN_EXPORT unsigned AzsTestRowCount(unsigned row) { return azs::rows::count(row); }

// choice ([Captions] Language): 0 auto, 1 en, 2 pl, 3 uk.
PLUGIN_EXPORT void AzsTestLanguage(int choice) { azs::language::test_set((azs::language::Choice)choice); }
PLUGIN_EXPORT int AzsTestCurrentLanguage(const unsigned char *game) {
    return (int)azs::language::current(azs::pages::text_base(game));
}

PLUGIN_EXPORT int AzsTestChain(unsigned char *site, void *handler, void **previous) {
    return (int)azs::chain_call(site, handler, previous);
}
#endif
