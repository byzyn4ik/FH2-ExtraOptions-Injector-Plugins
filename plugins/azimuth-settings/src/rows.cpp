// Azimuth Settings (Extra Options / forza_injector plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Values, captions (English, Polish and Ukrainian) and keys of the rows.
#include "rows.h"

#include "common.h"

namespace azs {
namespace rows {

namespace {

enum Kind { kFixed, kRate, kLines, kDetail, kPresetKind, kSlider, kCode };

const wchar_t *const off_on[] = {L"off", L"on"};
const wchar_t *const resolutions[] = {L"540", L"720", L"900", L"1080", L"1260",
                                      L"1440", L"1620", L"1800", L"1980", L"2160"};
const int resolution_lines[] = {540, 720, 900, 1080, 1260, 1440, 1620, 1800, 1980, 2160};
const wchar_t *const aspects[] = {L"16:9", L"display"};
const wchar_t *const displays[] = {L"windowed", L"fullscreen"};
const wchar_t *const presets[] = {L"low", L"default", L"high", L"ultra", L"custom"};
const wchar_t *const antialiasing[] = {L"off", L"msaax2", L"msaax4", L"coverage"};
// Azimuth's audio values (its reader takes exactly these three).
const wchar_t *const audio_outputs[] = {L"stereo", L"system", L"multichannel"};

const wchar_t *const off_on_en[] = {L"Off", L"On"}, *const off_on_pl[] = {L"Wył.", L"Wł."},
                     *const off_on_uk[] = {L"Вимк", L"Увімк"};
const wchar_t *const resolution_labels[] = {L"960 x 540",   L"1280 x 720",  L"1600 x 900",  L"1920 x 1080",
                                            L"2240 x 1260", L"2560 x 1440", L"2880 x 1620", L"3200 x 1800",
                                            L"3520 x 1980", L"3840 x 2160 (4K)"};
const wchar_t *const aspect_en[] = {L"16:9", L"As the display"}, *const aspect_pl[] = {L"16:9", L"Jak ekran"},
                     *const aspect_uk[] = {L"16:9", L"Як екран"};
const wchar_t *const display_en[] = {L"Windowed", L"Fullscreen"}, *const display_pl[] = {L"Okno", L"Pełny ekran"},
                     *const display_uk[] = {L"У вікні", L"Повний екран"};
const wchar_t *const preset_en[] = {L"Low", L"Default", L"High", L"Ultra", L"Custom"},
                     *const preset_pl[] = {L"Niska", L"Domyślna", L"Wysoka", L"Ultra", L"Własna"},
                     *const preset_uk[] = {L"Низька", L"Стандартна", L"Висока", L"Ультра", L"Власна"};
const wchar_t *const aa_en[] = {L"Off", L"2x MSAA", L"4x MSAA", L"Console (coverage)"},
                     *const aa_pl[] = {L"Wył.", L"2x MSAA", L"4x MSAA", L"Konsola (pokrycie)"},
                     *const aa_uk[] = {L"Вимк", L"2x MSAA", L"4x MSAA", L"Консоль (покриття)"};
const wchar_t *const audio_en[] = {L"Stereo", L"System (Windows)", L"Multichannel"},
                     *const audio_pl[] = {L"Stereo", L"System (Windows)", L"Wielokanałowe"},
                     *const audio_uk[] = {L"Стерео", L"Система (Windows)", L"Багатоканальний"};

// The details' levels (0 low, 1 default, 2 high, 3 ultra), as Azimuth's
// detail tables (azimuth.exe 0x3ef0) list them, and their values (0x40e0).
struct DetailDef {
    unsigned levels[4], count;
};
const DetailDef details[kDetailCount] = {
    {{0, 1, 2, 3}, 4}, // shadow_detail
    {{1, 2, 3}, 3},    // shadow_distance
    {{1, 2, 3}, 3},    // shadow_redraw
    {{0, 1, 2, 3}, 4}, // reflections
    {{0, 1, 2, 3}, 4}, // scenery_detail
    {{0, 1, 2, 3}, 4}, // car_detail
    {{0, 1}, 2},       // particles
    {{0, 1, 2, 3}, 4}, // motion_blur (0 is "off")
    {{1, 2}, 2},       // texture_filtering (2 is "16x")
};
const wchar_t *const detail_keys[kDetailCount] = {L"shadow_detail",  L"shadow_distance", L"shadow_redraw",
                                                  L"reflections",    L"scenery_detail",  L"car_detail",
                                                  L"particles",      L"motion_blur",     L"texture_filtering"};

const wchar_t *level_value(unsigned detail, unsigned level) {
    switch (level) {
    case 0: return detail == 7 ? L"off" : L"low";
    case 1: return L"default";
    case 2: return detail == 8 ? L"16x" : L"high";
    default: return L"ultra";
    }
}

const wchar_t *level_label(unsigned detail, unsigned level, Language language) {
    if (level == 0 && detail == 7)
        return (language == kPolish ? off_on_pl : language == kUkrainian ? off_on_uk : off_on_en)[0];
    if (level == 2 && detail == 8) return L"16x";
    const wchar_t *const *labels = language == kPolish ? preset_pl : language == kUkrainian ? preset_uk : preset_en;
    return labels[level < 4 ? level : 1];
}

// The language rows: absent (index 0), then these codes, then a saved code
// that is none of them.
struct Code {
    const wchar_t *code;
    const wchar_t *labels[3];
};
const Code codes[] = {
    {L"en-US", {L"English (US)", L"Angielski (USA)", L"Англійська (США)"}},
    {L"en-GB", {L"English (UK)", L"Angielski (Wielka Brytania)", L"Англійська (Велика Британія)"}},
    {L"fr-FR", {L"French", L"Francuski", L"Французька"}},
    {L"de-DE", {L"German", L"Niemiecki", L"Німецька"}},
    {L"it-IT", {L"Italian", L"Włoski", L"Італійська"}},
    {L"es-ES", {L"Spanish (Spain)", L"Hiszpański (Hiszpania)", L"Іспанська (Іспанія)"}},
    {L"es-MX", {L"Spanish (Mexico)", L"Hiszpański (Meksyk)", L"Іспанська (Мексика)"}},
    {L"pl-PL", {L"Polish", L"Polski", L"Польська"}},
    {L"pt-BR", {L"Portuguese (Brazil)", L"Portugalski (Brazylia)", L"Португальська (Бразилія)"}},
    {L"ru-RU", {L"Russian", L"Rosyjski", L"Російська"}},
    {L"ja-JP", {L"Japanese", L"Japoński", L"Японська"}},
    {L"zh-TW", {L"Chinese (Traditional)", L"Chiński tradycyjny", L"Китайська (традиційна)"}},
};
constexpr unsigned kCodes = sizeof(codes) / sizeof(codes[0]);
const wchar_t *const speech_absent[] = {L"Same as text", L"Jak tekst", L"Як текст"};
const wchar_t *const language_absent[] = {L"Windows language", L"Język Windows", L"Мова Windows"};

#define RESTART_EN " Restart the game to apply."
#define RESTART_PL " Uruchom grę ponownie, aby zastosować."
#define RESTART_UK " Перезапустіть гру, щоб застосувати."

struct RowDef {
    const char *property, *collection;
    const wchar_t *key;
    bool launcher; // in [launcher], else [title-<id>]
    Kind kind;
    const wchar_t *const *values;
    unsigned count, fallback;
    const wchar_t *const *labels[3];
    const char *names[3];
};

const RowDef table[kRowCount] = {
    {"AzsFrameRate", "AzsFrameRateChoices", L"frame_rate", false, kRate, nullptr, 0, 0, {nullptr, nullptr, nullptr},
     {"Frame rate", "Liczba klatek", "Частота кадрів"}},
    {"AzsVSync", "AzsVSyncChoices", L"vsync", false, kFixed, off_on, 2, 1, {off_on_en, off_on_pl, off_on_uk},
     {"V-sync", "Synchronizacja pionowa", "Вертикальна синхронізація"}},
    {"AzsResolution", "AzsResolutionChoices", L"resolution", false, kLines, resolutions, 10, 3,
     {resolution_labels, resolution_labels, resolution_labels},
     {"Base resolution", "Bazowa rozdzielczość", "Базова роздільність"}},
    {"AzsAspect", "AzsAspectChoices", L"aspect", false, kFixed, aspects, 2, 0, {aspect_en, aspect_pl, aspect_uk},
     {"Aspect ratio", "Proporcje obrazu", "Співвідношення сторін"}},
    {"AzsDisplay", "AzsDisplayChoices", L"display", true, kFixed, displays, 2, 1, {display_en, display_pl, display_uk},
     {"Display mode", "Tryb wyświetlania", "Режим екрана"}},
    {"AzsHdr", "AzsHdrChoices", L"hdr", false, kFixed, off_on, 2, 0, {off_on_en, off_on_pl, off_on_uk},
     {"HDR", "HDR", "HDR"}},
    {"AzsPreset", "AzsPresetChoices", L"graphics", false, kPresetKind, presets, 5, 1,
     {preset_en, preset_pl, preset_uk}, {"Graphics", "Grafika", "Графіка"}},
    {"AzsShadowDetail", "AzsShadowDetailChoices", L"shadow_detail", false, kDetail, nullptr, 0, 0, {},
     {"Shadow detail", "Szczegółowość cieni", "Деталізація тіней"}},
    {"AzsShadowDistance", "AzsShadowDistanceChoices", L"shadow_distance", false, kDetail, nullptr, 0, 0, {},
     {"Shadow distance", "Zasięg cieni", "Дальність тіней"}},
    {"AzsShadowRedraw", "AzsShadowRedrawChoices", L"shadow_redraw", false, kDetail, nullptr, 0, 0, {},
     {"Shadow redraw", "Odświeżanie cieni", "Оновлення тіней"}},
    {"AzsReflections", "AzsReflectionsChoices", L"reflections", false, kDetail, nullptr, 0, 0, {},
     {"Reflections", "Odbicia", "Відображення"}},
    {"AzsSceneryDetail", "AzsSceneryDetailChoices", L"scenery_detail", false, kDetail, nullptr, 0, 0, {},
     {"Scenery detail", "Szczegółowość otoczenia", "Деталізація оточення"}},
    {"AzsCarDetail", "AzsCarDetailChoices", L"car_detail", false, kDetail, nullptr, 0, 0, {},
     {"Car detail", "Szczegółowość samochodów", "Деталізація автомобілів"}},
    {"AzsParticles", "AzsParticlesChoices", L"particles", false, kDetail, nullptr, 0, 0, {},
     {"Particles", "Cząsteczki", "Частинки"}},
    {"AzsMotionBlur", "AzsMotionBlurChoices", L"motion_blur", false, kDetail, nullptr, 0, 0, {},
     {"Motion blur", "Rozmycie ruchu", "Розмиття в русі"}},
    {"AzsTextureFiltering", "AzsTextureFilteringChoices", L"texture_filtering", false, kDetail, nullptr, 0, 0, {},
     {"Texture filtering", "Filtrowanie tekstur", "Фільтрація текстур"}},
    {"AzsAntiAliasing", "AzsAntiAliasingChoices", L"antialiasing", false, kFixed, antialiasing, 4, 2,
     {aa_en, aa_pl, aa_uk}, {"Anti-aliasing", "Wygładzanie krawędzi", "Згладжування"}},
    {"AzsAudioOutput", "AzsAudioOutputChoices", L"audio", false, kFixed, audio_outputs, 3, 0,
     {audio_en, audio_pl, audio_uk}, {"Audio output (Azimuth)", "Wyjście audio (Azimuth)", "Аудіовихід (Azimuth)"}},
    {"AzsVolume", nullptr, L"volume", false, kSlider, nullptr, 0, 0, {nullptr, nullptr, nullptr},
     {"Volume (Azimuth)", "Głośność (Azimuth)", "Гучність (Azimuth)"}},
    {"AzsSpeech", "AzsSpeechChoices", L"speech", false, kCode, nullptr, 0, 0, {nullptr, nullptr, nullptr},
     {"Speech language (Azimuth)", "Język mowy (Azimuth)", "Мова озвучення (Azimuth)"}},
    {"AzsLanguage", "AzsLanguageChoices", L"language", false, kCode, nullptr, 0, 0, {nullptr, nullptr, nullptr},
     {"Text language (Azimuth)", "Język tekstu (Azimuth)", "Мова тексту (Azimuth)"}},
};

const wchar_t kLauncher[] = L"launcher";
constexpr int kDefaultVolume = 100;

std::wstring ini, title = L"title-1a96545b";
int title_rate = 30;
std::vector<int> rates = {30, 60};
std::vector<std::wstring> rate_labels = {L"30 FPS", L"60 FPS"};
bool rates_set;
bool page_on[2] = {true, true};
// A saved language code that is none of `codes` (speech, language), else empty.
std::wstring extra_code[2];

// As Azimuth's launcher: the highest choice at or below the saved value.
int lower_index(const int *values, size_t count, int saved) {
    int index = 0;
    for (size_t i = 0; i < count; ++i)
        if (values[i] <= saved) index = (int)i;
    return index;
}

const wchar_t *section_of(unsigned row) { return table[row].launcher ? kLauncher : title.c_str(); }

bool has_section(const std::wstring &names, const wchar_t *wanted) {
    for (size_t at = 0; at < names.size() && names[at];) {
        size_t end = names.find(L'\0', at);
        if (end == std::wstring::npos) end = names.size();
        if (!lstrcmpiW(names.substr(at, end - at).c_str(), wanted)) return true;
        at = end + 1;
    }
    return false;
}

// The key's value, trimmed; false when absent or empty.
bool read_text(const wchar_t *section, const wchar_t *key, std::wstring &out) {
    wchar_t value[128];
    out.clear();
    if (ini.empty() || !GetPrivateProfileStringW(section, key, L"", value, 128, ini.c_str()) || !value[0])
        return false;
    out = value;
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
    return !out.empty();
}

// A whole decimal number of up to 4 digits (as Azimuth reads frame_rate,
// volume and resolution), else -1.
int read_number(const wchar_t *key) {
    std::wstring value;
    if (!read_text(title.c_str(), key, value)) return -1;
    int number = 0, digits = 0;
    for (wchar_t c : value) {
        if (c < L'0' || c > L'9' || digits >= 4) return -1;
        number = number * 10 + (c - L'0');
        ++digits;
    }
    return number;
}

int saved_rate() {
    int saved = read_number(L"frame_rate");
    return saved >= 30 && saved <= 1000 ? saved : title_rate;
}

int saved_volume() {
    int saved = read_number(L"volume");
    return saved >= 0 && saved <= 100 ? saved : kDefaultVolume;
}

int saved_lines() {
    int saved = read_number(L"resolution");
    return saved >= 360 && saved <= 4320 ? saved : 1080;
}

// The level a detail value names (as Azimuth compares: exactly, any case), or -1.
int level_of(unsigned detail, const std::wstring &value) {
    for (unsigned i = 0; i < details[detail].count; ++i) {
        unsigned level = details[detail].levels[i];
        if (!lstrcmpiW(value.c_str(), level_value(detail, level))) return (int)level;
    }
    return -1;
}

// The preset the graphics key names (0..3), or -1.
int named_preset() {
    std::wstring value;
    if (!read_text(title.c_str(), L"graphics", value)) return -1;
    for (int i = 0; i < 4; ++i)
        if (!lstrcmpiW(value.c_str(), presets[i])) return i;
    return -1;
}

// The preset whose levels the details all have, else -1.
int matching_preset() {
    int levels[kDetailCount];
    for (unsigned d = 0; d < kDetailCount; ++d) levels[d] = detail_level(d);
    for (unsigned preset = 0; preset < 4; ++preset) {
        bool all = true;
        for (unsigned d = 0; d < kDetailCount && all; ++d) all = levels[d] == preset_detail(preset, d);
        if (all) return (int)preset;
    }
    return -1;
}

unsigned code_count(unsigned row) { return 1 + kCodes + (extra_code[row == kLanguage].empty() ? 0 : 1); }

int code_index(unsigned row) {
    std::wstring value;
    if (!read_text(title.c_str(), table[row].key, value)) return 0;
    for (unsigned i = 0; i < kCodes; ++i)
        if (!lstrcmpiW(value.c_str(), codes[i].code)) return (int)i + 1;
    return extra_code[row == kLanguage].empty() ? 0 : (int)kCodes + 1;
}

bool write(const wchar_t *section, const wchar_t *key, const wchar_t *value) {
    return WritePrivateProfileStringW(section, key, value, ini.c_str()) != 0;
}

} // namespace

void set_title(const std::wstring &title_id) {
    std::wstring lower = title_id;
    for (wchar_t &c : lower)
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    title = L"title-" + lower;
}

const std::wstring &section() { return title; }

void set_title_rate(int rate) { title_rate = rate >= 30 && rate <= 1000 ? rate : 30; }

void set_file(const std::wstring &azimuth_ini) {
    ini = azimuth_ini;
    for (unsigned which = 0; which < 2; ++which) {
        extra_code[which].clear();
        std::wstring value;
        if (!read_text(title.c_str(), which ? L"language" : L"speech", value)) continue;
        bool known = false;
        for (unsigned i = 0; i < kCodes && !known; ++i) known = !lstrcmpiW(value.c_str(), codes[i].code);
        if (!known && value.size() < 32) extra_code[which] = value;
    }
}

bool frame_rates_set() { return rates_set; }

void set_frame_rates(const std::vector<int> &list) {
    if (list.empty()) return;
    rates_set = true;
    rates = list;
    rate_labels.clear();
    for (int rate : rates) rate_labels.push_back(std::to_wstring(rate) + L" FPS");
}

const std::vector<int> &frame_rates() { return rates; }

int frame_rate_value(int index) { return index >= 0 && (size_t)index < rates.size() ? rates[(size_t)index] : 0; }

const std::wstring &file() { return ini; }

std::string check_layout() {
    if (ini.empty()) return "no data\\azimuth.ini path";
    DWORD attributes = GetFileAttributesW(ini.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return narrow(ini) + " does not exist";
    std::wstring names(32768, L'\0');
    DWORD n = GetPrivateProfileSectionNamesW(&names[0], (DWORD)names.size(), ini.c_str());
    names.resize(n);
    if (!has_section(names, kLauncher)) return narrow(ini) + " has no [launcher] section";
    if (!has_section(names, title.c_str()))
        return narrow(ini) + " has no [" + narrow(title) + "] section (Azimuth 1.x keeps the game's settings there)";
    return std::string();
}

void enable_page(Page which, bool on) { page_on[which] = on; }

Page page(unsigned row) { return row >= kAudioOutput ? kAudioPage : kVideoPage; }

bool shown(unsigned row) { return row < kRowCount && page_on[page(row)]; }

bool slider(unsigned row) { return row == kVolume; }

unsigned count(unsigned row) {
    if (row >= kRowCount) return 0;
    switch (table[row].kind) {
    case kRate: return (unsigned)rates.size();
    case kSlider: return 101;
    case kDetail: return details[row - kShadowDetail].count;
    case kCode: return code_count(row);
    default: return table[row].count;
    }
}

int preset_detail(unsigned level, unsigned detail) {
    if (detail >= kDetailCount) return 1;
    int x = level > 3 ? 1 : (int)level;
    for (int step = 0; step < 4; ++step) {
        for (unsigned i = 0; i < details[detail].count; ++i)
            if ((int)details[detail].levels[i] == x) return x;
        if (x == 1) break;
        x += x < 1 ? 1 : -1;
    }
    return 1;
}

int detail_level(unsigned detail) {
    if (detail >= kDetailCount) return 1;
    std::wstring value;
    if (read_text(title.c_str(), detail_keys[detail], value)) {
        int level = level_of(detail, value);
        if (level >= 0) return level;
    }
    int preset = named_preset();
    return preset >= 0 ? preset_detail((unsigned)preset, detail) : 1;
}

int get(unsigned row) {
    if (!shown(row)) return 0;
    const RowDef &r = table[row];
    switch (r.kind) {
    case kRate: return lower_index(rates.data(), rates.size(), saved_rate());
    case kSlider: return saved_volume();
    case kLines: return lower_index(resolution_lines, 10, saved_lines());
    case kCode: return code_index(row);
    case kDetail: {
        unsigned detail = row - kShadowDetail;
        int level = detail_level(detail);
        for (unsigned i = 0; i < details[detail].count; ++i)
            if ((int)details[detail].levels[i] == level) return (int)i;
        return 0;
    }
    case kPresetKind: {
        std::wstring value;
        if (read_text(title.c_str(), L"graphics", value) && !lstrcmpiW(value.c_str(), L"custom")) return 4;
        int preset = matching_preset();
        return preset >= 0 ? preset : 4;
    }
    case kFixed: break;
    }
    std::wstring value;
    if (!read_text(section_of(row), r.key, value)) return (int)r.fallback;
    for (unsigned i = 0; i < r.count; ++i)
        if (!lstrcmpiW(value.c_str(), r.values[i])) return (int)i;
    return (int)r.fallback;
}

bool set(unsigned row, int value) {
    if (!shown(row) || value < 0 || (unsigned)value >= count(row) || ini.empty()) return false;
    const RowDef &r = table[row];
    switch (r.kind) {
    case kRate: {
        // Unchanged only when the saved rate is exactly this choice (a saved
        // rate between choices shows as the lower one).
        if (read_number(L"frame_rate") == rates[(size_t)value]) return false;
        return write(title.c_str(), r.key, std::to_wstring(rates[(size_t)value]).c_str());
    }
    case kSlider:
        if (read_number(L"volume") == value) return false;
        return write(title.c_str(), r.key, std::to_wstring(value).c_str());
    case kLines:
        if (read_number(L"resolution") == resolution_lines[value]) return false;
        return write(title.c_str(), r.key, r.values[value]);
    case kCode: {
        if (get(row) == value) return false;
        // 0: no key (speech as the text, the text as Windows' language).
        const wchar_t *code = value == 0                 ? nullptr
                              : (unsigned)value <= kCodes ? codes[value - 1].code
                                                          : extra_code[row == kLanguage].c_str();
        return write(title.c_str(), r.key, code);
    }
    case kDetail: {
        if (get(row) == value) return false;
        unsigned detail = row - kShadowDetail;
        if (!write(title.c_str(), r.key, level_value(detail, details[detail].levels[value]))) return false;
        // As Azimuth's launcher: graphics names the preset the details now
        // match, else custom.
        int preset = matching_preset();
        write(title.c_str(), L"graphics", preset >= 0 ? presets[preset] : L"custom");
        return true;
    }
    case kPresetKind: {
        if (get(row) == value) return false;
        if (!write(title.c_str(), r.key, presets[value])) return false;
        if (value < 4) // the preset's level for every detail, as Azimuth's launcher writes them
            for (unsigned d = 0; d < kDetailCount; ++d)
                write(title.c_str(), detail_keys[d], level_value(d, (unsigned)preset_detail((unsigned)value, d)));
        return true;
    }
    case kFixed: break;
    }
    if (get(row) == value) return false;
    return write(section_of(row), r.key, r.values[value]);
}

const char *property(unsigned row) { return row < kRowCount ? table[row].property : nullptr; }

const char *collection(unsigned row) { return row < kRowCount ? table[row].collection : nullptr; }

const wchar_t *key(unsigned row) { return row < kRowCount ? table[row].key : nullptr; }

const wchar_t *choice(unsigned row, unsigned index, Language language) {
    if (row >= kRowCount || slider(row) || index >= count(row)) return nullptr;
    switch (table[row].kind) {
    case kRate: return rate_labels[index].c_str();
    case kDetail: {
        unsigned detail = row - kShadowDetail;
        return level_label(detail, details[detail].levels[index], language);
    }
    case kCode:
        if (index == 0) return row == kSpeech ? speech_absent[language] : language_absent[language];
        if (index <= kCodes) return codes[index - 1].labels[language];
        return extra_code[row == kLanguage].c_str();
    default: return table[row].labels[language][index];
    }
}

const char *name(unsigned row, Language language) { return row < kRowCount ? table[row].names[language] : nullptr; }

std::string description(unsigned row, Language language, const LiveState &live) {
    // English, Polish, Ukrainian.
    auto pick = [language](const char *en, const char *pl, const char *uk) {
        return std::string(language == kPolish ? pl : language == kUkrainian ? uk : en);
    };
    switch (row) {
    case kFrameRate:
        return live.frame_rate ? pick("Target frame rate. Applies at once.", "Docelowa liczba klatek. Działa od razu.",
                                      "Цільова частота кадрів. Діє одразу.")
                               : pick("Target frame rate." RESTART_EN, "Docelowa liczba klatek." RESTART_PL,
                                      "Цільова частота кадрів." RESTART_UK);
    case kVSync:
        return pick("Wait for the display's refresh. Off allows 120 FPS on any screen." RESTART_EN,
                    "Czekaj na odświeżenie ekranu. Wyłączona pozwala na 120 FPS na każdym ekranie." RESTART_PL,
                    "Чекати на оновлення екрана. Вимк дозволяє 120 FPS на будь-якому екрані." RESTART_UK);
    case kResolution:
        return pick("Base rendering resolution. Higher resolution increases GPU load. Includes the interface." RESTART_EN,
                    "Bazowa rozdzielczość renderowania. Wyższa zwiększa obciążenie GPU. Obejmuje interfejs." RESTART_PL,
                    "Базова роздільність рендерингу. Вища роздільність більше навантажує GPU. Включає "
                    "інтерфейс." RESTART_UK);
    case kAspect:
        return pick("The picture's shape: 16:9, or the display's own (for example 21:9)." RESTART_EN,
                    "Proporcje obrazu: 16:9 lub jak ekran (na przykład 21:9)." RESTART_PL,
                    "Пропорції зображення: 16:9 або як екран (наприклад, 21:9)." RESTART_UK);
    case kDisplay:
        return live.display ? pick("Window or fullscreen. Applies at once, as Alt+Enter does.",
                                   "Okno lub pełny ekran. Działa od razu, jak Alt+Enter.",
                                   "У вікні або на весь екран. Діє одразу, як Alt+Enter.")
                            : pick("Window or fullscreen." RESTART_EN, "Okno lub pełny ekran." RESTART_PL,
                                   "У вікні або на весь екран." RESTART_UK);
    case kHdr:
        return pick("High dynamic range output on an HDR display." RESTART_EN,
                    "Obraz HDR na ekranie z obsługą HDR." RESTART_PL,
                    "Зображення з високим динамічним діапазоном на HDR-екрані." RESTART_UK);
    case kPreset:
        if (live.shadow_distance && live.shadow_rate)
            return pick("Sets every detail below. Custom: the details as set. Shadow distance and shadow redraw "
                        "apply at once, the rest after a restart.",
                        "Ustawia każdy szczegół poniżej. Własna: szczegóły jak ustawione. Zasięg i odświeżanie cieni "
                        "działają od razu, reszta po ponownym uruchomieniu.",
                        "Встановлює всі параметри нижче. Власна: параметри як налаштовано. Дальність і оновлення "
                        "тіней діють одразу, решта після перезапуску.");
        if (live.shadow_distance)
            return pick("Sets every detail below. Custom: the details as set. Shadow distance applies at once, the "
                        "rest after a restart.",
                        "Ustawia każdy szczegół poniżej. Własna: szczegóły jak ustawione. Zasięg cieni działa od "
                        "razu, reszta po ponownym uruchomieniu.",
                        "Встановлює всі параметри нижче. Власна: параметри як налаштовано. Дальність тіней діє "
                        "одразу, решта після перезапуску.");
        return pick("Sets every detail below. Custom: the details as set." RESTART_EN,
                    "Ustawia każdy szczegół poniżej. Własna: szczegóły jak ustawione." RESTART_PL,
                    "Встановлює всі параметри нижче. Власна: параметри як налаштовано." RESTART_UK);
    case kShadowDetail:
        return pick("Shadow sharpness (shadow map size)." RESTART_EN,
                    "Ostrość cieni (rozmiar mapy cieni)." RESTART_PL,
                    "Чіткість тіней (розмір карти тіней)." RESTART_UK);
    case kShadowDistance:
        return live.shadow_distance ? pick("How far shadows are drawn. Applies at once.",
                                           "Jak daleko rysowane są cienie. Działa od razu.",
                                           "Як далеко малюються тіні. Діє одразу.")
                                    : pick("How far shadows are drawn." RESTART_EN,
                                           "Jak daleko rysowane są cienie." RESTART_PL,
                                           "Як далеко малюються тіні." RESTART_UK);
    case kShadowRedraw:
        return live.shadow_rate ? pick("How many shadow maps are redrawn each frame. Applies at once.",
                                       "Ile map cieni jest odświeżanych w każdej klatce. Działa od razu.",
                                       "Скільки карт тіней оновлюється щокадру. Діє одразу.")
                                : pick("How many shadow maps are redrawn each frame." RESTART_EN,
                                       "Ile map cieni jest odświeżanych w każdej klatce." RESTART_PL,
                                       "Скільки карт тіней оновлюється щокадру." RESTART_UK);
    case kReflections:
        return pick("Quality of reflections." RESTART_EN, "Jakość odbić." RESTART_PL,
                    "Якість відображень." RESTART_UK);
    case kSceneryDetail:
        return pick("Detail of the world around the road." RESTART_EN, "Szczegółowość świata wokół drogi." RESTART_PL,
                    "Деталізація світу довкола дороги." RESTART_UK);
    case kCarDetail:
        return pick("Detail of the cars." RESTART_EN, "Szczegółowość samochodów." RESTART_PL,
                    "Деталізація автомобілів." RESTART_UK);
    case kParticles:
        return pick("Smoke, dust and spray." RESTART_EN, "Dym, kurz i rozbryzgi." RESTART_PL,
                    "Дим, пил і бризки." RESTART_UK);
    case kMotionBlur:
        return pick("Blur of fast movement." RESTART_EN, "Rozmycie szybkiego ruchu." RESTART_PL,
                    "Розмиття швидкого руху." RESTART_UK);
    case kTextureFiltering:
        return pick("Anisotropic filtering: the game's own, or 16x." RESTART_EN,
                    "Filtrowanie anizotropowe: domyślne gry lub 16x." RESTART_PL,
                    "Анізотропна фільтрація: стандартна гри або 16x." RESTART_UK);
    case kAntiAliasing:
        return pick("Multisample anti-aliasing. Console: the console's coverage sampling." RESTART_EN,
                    "Wygładzanie wielopróbkowe. Konsola: próbkowanie pokrycia jak na konsoli." RESTART_PL,
                    "Багатовибіркове згладжування. Консоль: вибірка покриття, як на консолі." RESTART_UK);
    case kAudioOutput:
        return pick("Stereo: the game mixes 2 channels. System: as the Windows output (5.1 or 7.1 when it has them). "
                    "Multichannel: 5.1/7.1 even on a stereo output." RESTART_EN,
                    "Stereo: gra miksuje 2 kanały. System: jak wyjście Windows (5.1 lub 7.1, jeśli je ma). "
                    "Wielokanałowe: 5.1/7.1 nawet na wyjściu stereo." RESTART_PL,
                    "Стерео: гра зводить 2 канали. Система: як вихід Windows (5.1 або 7.1, якщо він їх має). "
                    "Багатоканальний: 5.1/7.1 навіть на стереовиході." RESTART_UK);
    case kVolume:
        return live.volume ? pick("Azimuth's output volume, on top of the game's own volumes. Applies at once.",
                                  "Głośność wyjścia Azimuth, niezależna od głośności gry. Działa od razu.",
                                  "Гучність виходу Azimuth, окремо від гучностей гри. Діє одразу.")
                           : pick("Azimuth's output volume, on top of the game's own volumes." RESTART_EN,
                                  "Głośność wyjścia Azimuth, niezależna od głośności gry." RESTART_PL,
                                  "Гучність виходу Azimuth, окремо від гучностей гри." RESTART_UK);
    case kSpeech:
        return pick("Language of the game's voices. Same as text: the text language's, when the game has it." RESTART_EN,
                    "Język głosów w grze. Jak tekst: język tekstu, jeśli gra go ma." RESTART_PL,
                    "Мова голосів у грі. Як текст: мова тексту, якщо гра її має." RESTART_UK);
    case kLanguage:
        return pick("Language of the game's text. Windows language: as Windows is set." RESTART_EN,
                    "Język tekstu gry. Język Windows: jak ustawiono w Windows." RESTART_PL,
                    "Мова тексту гри. Мова Windows: як налаштовано у Windows." RESTART_UK);
    }
    return std::string();
}

} // namespace rows
} // namespace azs
