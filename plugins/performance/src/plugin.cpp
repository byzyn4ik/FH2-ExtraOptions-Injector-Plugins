// Performance for Forza Horizon 2: an Extra Options mod-loader plugin.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installed as mods\Performance\plugins\Performance.dll beside a stock Extra
// Options 0.4.4 on Azimuth 1.0.4. An FPS counter in the top-right corner of
// the game (FPS, GPU load and video memory; Detailed adds the frame time, the
// 1% low and the GPU frame time) and an optional performance log (one CSV
// line a second). Settings: Performance.ini beside the DLL; the Video settings
// page gets an "FPS counter" row (Off / On / Detailed) that writes ShowFps
// there and applies at once.
//
// From FH2PluginInitialize it reads the INI, chains the Video page (when the
// game image is as inspected, video_row.h) and starts the late attach on its
// own thread (attach.h), which hooks DXGI and D3D12 by vtable (monitor.h).
// Nothing here depends on Azimuth's addresses.
#include <string>

#include "attach.h"
#include "common.h"
#include "fh2_game.h"
#include "fh2_plugin.h"
#include "monitor.h"
#include "nos_gauge.h"
#include "video_row.h"

#include <algorithm>

#define PLUGIN_EXPORT extern "C" __declspec(dllexport)

namespace {

constexpr uint32_t kAzimuth104 = 0x6ae393ebu; // azimuth.exe 1.0.4 TimeDateStamp

perf::Options options;

std::wstring folder_of(const std::wstring &path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

uint32_t host_stamp() {
    const unsigned char *host = (const unsigned char *)GetModuleHandleW(nullptr);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)host;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(host + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

const char *mode_name(unsigned mode) { return mode == 0 ? "off" : mode == 1 ? "on" : "detailed"; }

// The Video page row: ShowFps written to the INI, applied at once.
void row_changed(unsigned mode) {
    if (!perf::write_show(options.ini, mode))
        perf::say("ShowFps could not be written to " + perf::narrow(options.ini) + " (error " +
                  std::to_string(GetLastError()) + ")");
    perf::monitor::set_show(mode);
    if (mode) perf::attach::start(); // nothing was hooked while it was off since the start
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

PLUGIN_EXPORT BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    using namespace perf;
    if (!context || context->size < sizeof(FH2PluginContext) || context->api_version < 1) return FALSE;
    set_log(context->log_utf8);
    if (!context->plugin_path) {
        say("the loader gave no plugin path; nothing is installed");
        return FALSE;
    }
    std::wstring dll = context->plugin_path;
    std::wstring buffer(dll.size() + 5, L' ');
    std::wstring ini = fh2_plugin_ini_path(dll.c_str(), &buffer[0], buffer.size()) ? buffer.c_str() : dll + L".ini";
    std::string warnings;
    options = read_options(ini, folder_of(dll), &warnings);
    say("plugin " PERFORMANCE_VERSION " for Extra Options " +
        std::string(context->extra_options_version ? context->extra_options_version : "?") +
        "; FPS counter " + mode_name(options.show) + ", performance log " + (options.log ? "on" : "off") +
        ", Video page row " + (options.row ? "on" : "off"));
    if (!warnings.empty()) say(narrow(ini) + ": " + warnings);
    if (context->adapter != FH2_PLUGIN_AZIMUTH) {
        say("the game does not run on Azimuth (no D3D12 swap chain to draw on); nothing is installed");
        return TRUE;
    }
    uint32_t stamp = host_stamp();
    say(stamp == kAzimuth104 ? std::string("host: Azimuth 1.0.4")
                             : "host: an Azimuth build other than 1.0.4 (TimeDateStamp " + hex(stamp) +
                                   "); the DXGI/D3D12 hooks do not depend on it");
    monitor::configure(options);
    if (options.log) say("performance log: " + narrow(options.csv));
    if (options.row) {
        const char *problem = video_row::install((unsigned char *)context->game_image, context->game_image_size,
                                                 row_changed, monitor::show);
        if (problem) say(std::string(problem) + "; no Video page row (the INI still works)");
    }
    // The Nitrous plugin's gauge: only with Nitrous installed beside this plugin.
    bool gauge = false;
    std::string nos_warnings;
    const nos::Options nos_options = nos::read_options(ini, &nos_warnings);
    if (!nos_warnings.empty()) say(narrow(ini) + ": " + nos_warnings);
    if (nos::any(nos_options) && nos::nitrous_installed(dll)) {
        wchar_t host[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, host, MAX_PATH);
        const std::wstring zip = nos::find_spire_hud(nos_options.media, host);
        nos::Texture outline;
        std::string why;
        if (nos_options.gauge && !zip.empty() && nos::load_outline(zip, &outline, &why))
            say("Nitrous gauge: Fast & Furious' nitrous bar, its outline from " + narrow(zip));
        else if (nos_options.gauge)
            say("Nitrous gauge: Fast & Furious' nitrous bar without its outline (" +
                (zip.empty() ? std::string("Fast & Furious' SpireHUD.zip was not found; [Nitrous] FastFuriousMedia names it")
                             : why) +
                ")");
        monitor::configure_nitrous(nos_options, std::move(outline));
        gauge = true;
    }
    if (options.show || options.log || gauge)
        attach::start();
    else
        say("ShowFps=0 and PerfLog=0: nothing is hooked until the counter is turned on" +
            std::string(options.row ? " on the Video page" : ""));
    return TRUE;
}

#ifdef PERFORMANCE_TEST
// Test build only (out\tests\PerformanceTest.dll): the same code with entry
// points for tests\run_tests.py and tests\d3d12_test.cpp.
#include <cstring>

PLUGIN_EXPORT void PerfTestStats(perf::monitor::Stats *out) { perf::monitor::stats(out); }

// 1: attached, 0: the attach failed, -1: it has not ended in time (or not started).
PLUGIN_EXPORT int PerfTestWaitAttach(unsigned milliseconds) {
    if (!perf::attach::wait(milliseconds)) return -1;
    return perf::attach::succeeded() ? 1 : 0;
}
PLUGIN_EXPORT const char *PerfTestReport() { return perf::attach::report(); }

struct TestOptions {
    unsigned show, log, row;
    wchar_t csv[520];
    char warnings[512];
};
PLUGIN_EXPORT void PerfTestReadOptions(const wchar_t *ini, const wchar_t *folder, TestOptions *out) {
    std::string warnings;
    perf::Options o = perf::read_options(ini, folder, &warnings);
    out->show = o.show;
    out->log = o.log;
    out->row = o.row;
    wcsncpy(out->csv, o.csv.c_str(), 519);
    out->csv[519] = 0;
    strncpy(out->warnings, warnings.c_str(), 511);
    out->warnings[511] = 0;
}
PLUGIN_EXPORT int PerfTestWriteShow(const wchar_t *ini, unsigned mode) { return perf::write_show(ini, mode); }
// As the Video page row does.
PLUGIN_EXPORT void PerfTestRowChanged(unsigned mode) { row_changed(mode); }
PLUGIN_EXPORT unsigned PerfTestShow() { return perf::monitor::show(); }

// The Video page row's guards on a game image: what differs, else what the
// resource call goes to (prefixed "target: ").
PLUGIN_EXPORT const char *PerfTestCheckGame(const unsigned char *game, uint32_t size) {
    static std::string text;
    const char *target = nullptr;
    const char *problem = perf::video_row::check_game(game, size, &target);
    text = problem ? std::string(problem) : "target: " + std::string(target ? target : "?");
    return text.c_str();
}
PLUGIN_EXPORT size_t PerfTestExtendXaml(const char *text, size_t length, char *output, size_t capacity,
                                        const char *code) {
    const char *why = nullptr;
    return perf::video_row::extend_video(text, length, output, capacity, perf::video_row::captions_for(code), &why);
}
PLUGIN_EXPORT const char *PerfTestWhy(const char *text, size_t length) {
    static char output[1 << 20];
    const char *why = nullptr;
    return perf::video_row::extend_video(text, length, output, sizeof(output), perf::video_row::captions_for("EN"),
                                         &why)
               ? nullptr
               : why;
}
PLUGIN_EXPORT unsigned PerfTestCaptionCount() { return perf::video_row::caption_count(); }
// part 0: code (as UTF-16), 1: name, 2-4: choices, 5: description.
PLUGIN_EXPORT const wchar_t *PerfTestCaption(const char *code, unsigned part) {
    static wchar_t code_text[8];
    const perf::video_row::Captions &c = perf::video_row::captions_for(code);
    if (part == 0) {
        unsigned i = 0;
        for (; c.code[i] && i < 7; ++i) code_text[i] = (wchar_t)c.code[i];
        code_text[i] = 0;
        return code_text;
    }
    return part == 1 ? c.name : part <= 4 ? c.choices[part - 2] : c.description;
}
PLUGIN_EXPORT const char *PerfTestCaptionCode(unsigned index) { return perf::video_row::caption(index).code; }
PLUGIN_EXPORT const char *PerfTestGameLanguage(const unsigned char *game) { return perf::video_row::game_language(game); }
#endif

#ifdef PERFORMANCE_TEST
// The Nitrous gauge (nos_gauge.h).
struct TestNosOptions {
    unsigned gauge, driving_only;
    float scale, x, y;
    wchar_t media[260];
    char warnings[512];
};
PLUGIN_EXPORT void PerfTestNosOptions(const wchar_t *ini, TestNosOptions *out) {
    std::string warnings;
    perf::nos::Options o = perf::nos::read_options(ini, &warnings);
    out->gauge = o.gauge;
    out->driving_only = o.driving_only;
    out->scale = o.scale;
    out->x = o.x;
    out->y = o.y;
    wcsncpy(out->media, o.media.c_str(), 259);
    out->media[259] = 0;
    strncpy(out->warnings, warnings.c_str(), 511);
    out->warnings[511] = 0;
}
// Fills `out` (perf::nos::Draw) for a status and the default options with the given scale/offsets.
PLUGIN_EXPORT void PerfTestNosLayout(unsigned width, unsigned height, const FH2NitrousStatus *status, unsigned gauge,
                                     unsigned driving_only, float scale, float x, float y,
                                     unsigned outline, perf::nos::Draw *out) {
    perf::nos::Options o;
    o.gauge = gauge;
    o.driving_only = driving_only;
    o.scale = scale;
    o.x = x;
    o.y = y;
    *out = perf::nos::layout(width, height, *status, o, outline != 0, 0.f);
}
PLUGIN_EXPORT int PerfTestInflate(const uint8_t *input, size_t length, uint8_t *output, size_t size) {
    return perf::nos::inflate(input, length, output, size);
}
// The outline from a SpireHUD.zip: its width, height and linear BC7 blocks (copied to `blocks`, up to capacity).
PLUGIN_EXPORT const char *PerfTestLoadOutline(const wchar_t *zip, unsigned *width, unsigned *height, uint8_t *blocks,
                                              size_t capacity, size_t *size) {
    static std::string why;
    perf::nos::Texture t;
    why.clear();
    if (!perf::nos::load_outline(zip, &t, &why)) return why.c_str();
    *width = t.width;
    *height = t.height;
    *size = t.blocks.size();
    memcpy(blocks, t.blocks.data(), std::min(capacity, t.blocks.size()));
    return "";
}
PLUGIN_EXPORT const wchar_t *PerfTestFindSpireHud(const wchar_t *configured, const wchar_t *host) {
    static std::wstring found;
    found = perf::nos::find_spire_hud(configured, host);
    return found.c_str();
}
PLUGIN_EXPORT int PerfTestNitrousInstalled(const wchar_t *dll) { return perf::nos::nitrous_installed(dll); }
PLUGIN_EXPORT void PerfTestNosSource(FH2NitrousStatusFn source) { perf::nos::set_source(source); }
#endif
