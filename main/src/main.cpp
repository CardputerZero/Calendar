#include "calendar_model.h"
#include "compat/input_keys.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/tiny_ttf/lv_tiny_ttf.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if LV_USE_SDL
#include <SDL.h>
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#endif

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kLeftX = 5;
constexpr int kLeftY = 23;
constexpr int kLeftW = 180;
constexpr int kLeftH = 142;
constexpr int kRightX = 191;
constexpr int kRightY = 23;
constexpr int kRightW = 124;
constexpr int kRightH = 142;
constexpr int kCellW = 24;
constexpr int kCellH = 19;

enum class ScreenMode {
    Month,
    Manager,
    Subscriptions,
    SubscriptionEdit,
    IcsInput,
    Loading,
};

volatile sig_atomic_t g_quit_requested = 0;
lv_obj_t *g_root = nullptr;
lv_indev_t *g_keyboard_indev = nullptr;
lv_group_t *g_group = nullptr;

calendar::Settings g_settings;
calendar::Language g_ui_language = calendar::Language::English;
calendar::Date g_selected = {2026, 5, 13};
calendar::Date g_focus_month = {2026, 5, 1};
std::vector<calendar::Event> g_events;
std::string g_status;
ScreenMode g_mode = ScreenMode::Month;
int g_filter_index = 0;
int g_manager_row = 0;
int g_subscription_row = 0;
int g_subscription_edit_row = 0;
std::string g_edit_source_id;
std::string g_input_url;
uint32_t g_esc_down_tick = 0;
struct RuntimeFontSet {
    lv_font_t *font_12;
    lv_font_t *font_14;
    bool initialized;
    std::vector<unsigned char> data;

    RuntimeFontSet() : font_12(nullptr), font_14(nullptr), initialized(false) {}
};

RuntimeFontSet g_runtime_font_zh;
RuntimeFontSet g_runtime_font_ja;
lv_obj_t *g_detail_scroll_panel = nullptr;
lv_obj_t *g_detail_scroll_content = nullptr;
lv_timer_t *g_detail_scroll_timer = nullptr;
int g_detail_scroll_direction = 1;
int g_detail_scroll_offset = 0;
int g_detail_scroll_max = 0;
int g_detail_scroll_hold = 0;
ScreenMode g_after_loading_mode = ScreenMode::Month;
pthread_t g_loading_thread;
pthread_mutex_t g_loading_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_loading_active = false;
bool g_loading_done = false;
int g_loading_current = 0;
int g_loading_total = 0;
std::string g_loading_source;
std::string g_loading_status;
calendar::Settings g_loading_settings;
calendar::Date g_loading_focus = {2026, 5, 1};
calendar::Language g_loading_language = calendar::Language::English;
std::vector<calendar::Event> g_loaded_events;
std::string g_loaded_status;
int g_last_loading_current = -1;
int g_last_loading_total = -1;
std::string g_last_loading_source;

const char *getenv_default(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value ? value : fallback;
}

bool env_enabled(const char *name, bool fallback)
{
    const char *value = std::getenv(name);
    if (!value || !value[0]) return fallback;
    return std::strcmp(value, "0") != 0 &&
           std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "False") != 0 &&
           std::strcmp(value, "off") != 0;
}

void request_quit()
{
    g_quit_requested = 1;
    LVGL_RUN_FLAGE = 0;
}

void handle_signal(int)
{
    request_quit();
}

RuntimeFontSet *runtime_font_set_for_language(calendar::Language language)
{
    if (language == calendar::Language::Japanese) return &g_runtime_font_ja;
    if (language == calendar::Language::Chinese) return &g_runtime_font_zh;
    return nullptr;
}

const lv_font_t *font_text()
{
    if (g_ui_language == calendar::Language::English) return &lv_font_montserrat_10;
    RuntimeFontSet *font_set = runtime_font_set_for_language(g_ui_language);
    if (font_set && font_set->font_12) return font_set->font_12;
#if LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    return &lv_font_source_han_sans_sc_14_cjk;
#else
    return &lv_font_montserrat_12;
#endif
}

const lv_font_t *font_text_large()
{
    if (g_ui_language == calendar::Language::English) return &lv_font_montserrat_14;
    RuntimeFontSet *font_set = runtime_font_set_for_language(g_ui_language);
    if (font_set && font_set->font_14) return font_set->font_14;
#if LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    return &lv_font_source_han_sans_sc_14_cjk;
#else
    return &lv_font_montserrat_14;
#endif
}

std::string dirname_of(std::string path)
{
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string executable_dir()
{
    char path[PATH_MAX] = {};
#if defined(__APPLE__)
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) return dirname_of(path);
#else
    ssize_t got = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (got > 0) {
        path[got] = '\0';
        return dirname_of(path);
    }
#endif
    return ".";
}

void push_unique(std::vector<std::string> *paths, const std::string &path)
{
    if (path.empty()) return;
    if (std::find(paths->begin(), paths->end(), path) == paths->end()) paths->push_back(path);
}

void push_named_font_candidates(std::vector<std::string> *paths, const std::string &name,
                                const std::string &exe_dir)
{
    push_unique(paths, "fonts/" + name);
    push_unique(paths, name);
    push_unique(paths, exe_dir + "/fonts/" + name);
    push_unique(paths, exe_dir + "/../fonts/" + name);
    push_unique(paths, exe_dir + "/../share/font/" + name);
    push_unique(paths, exe_dir + "/../Resources/fonts/" + name);
    push_unique(paths, "/usr/share/APPLaunch/fonts/" + name);
    push_unique(paths, "/usr/share/APPLaunch/share/font/" + name);
}

std::vector<std::string> candidate_font_paths(calendar::Language language)
{
    std::vector<std::string> paths;
    const char *language_override = nullptr;
    if (language == calendar::Language::Japanese) language_override = std::getenv("M5_CALENDAR_FONT_JA");
    else if (language == calendar::Language::Chinese) language_override = std::getenv("M5_CALENDAR_FONT_ZH");
    if (language_override && *language_override) push_unique(&paths, language_override);

    const char *override_path = std::getenv("M5_CALENDAR_FONT");
    if (override_path && *override_path) push_unique(&paths, override_path);

    std::string exe_dir = executable_dir();
    if (language == calendar::Language::Japanese) {
        push_named_font_candidates(&paths, "NotoSansJP-Regular.ttf", exe_dir);
        push_named_font_candidates(&paths, "NotoSansCJK-Regular.ttc", exe_dir);
        push_unique(&paths, "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
        push_unique(&paths, "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf");
        push_unique(&paths, "/usr/share/fonts/truetype/noto/NotoSansJP-Regular.ttf");
    }

    push_named_font_candidates(&paths, "NotoSansSC-Regular.ttf", exe_dir);
    push_unique(&paths, "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.ttf");
    if (language != calendar::Language::Japanese) {
        push_named_font_candidates(&paths, "NotoSansCJK-Regular.ttc", exe_dir);
        push_unique(&paths, "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    }
    push_unique(&paths, "/System/Library/Fonts/Hiragino Sans GB.ttc");
    return paths;
}

bool read_binary_file(const std::string &path, std::vector<unsigned char> *out)
{
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    long size = std::ftell(fp);
    if (size <= 0) {
        std::fclose(fp);
        return false;
    }
    std::rewind(fp);
    out->assign(static_cast<size_t>(size), 0);
    bool ok = std::fread(out->data(), 1, out->size(), fp) == out->size();
    std::fclose(fp);
    if (!ok) out->clear();
    return ok;
}

void init_runtime_font_for_language(calendar::Language language)
{
    RuntimeFontSet *font_set = runtime_font_set_for_language(language);
    if (!font_set || font_set->initialized) return;
    font_set->initialized = true;
#if LV_USE_TINY_TTF
    std::vector<std::string> paths = candidate_font_paths(language);
    for (size_t i = 0; i < paths.size(); ++i) {
        std::vector<unsigned char> data;
        if (!read_binary_file(paths[i], &data)) continue;
        lv_font_t *font_12 = lv_tiny_ttf_create_data(data.data(), data.size(), 12);
        if (!font_12) continue;
        lv_font_t *font_14 = lv_tiny_ttf_create_data(data.data(), data.size(), 14);
        if (!font_14) {
            lv_tiny_ttf_destroy(font_12);
            continue;
        }
        font_set->data.swap(data);
        font_set->font_12 = font_12;
        font_set->font_14 = font_14;
        std::fprintf(stderr, "Calendar font loaded (%s): %s\n",
                     calendar::language_code(language), paths[i].c_str());
        return;
    }
#endif
    std::fprintf(stderr, "Calendar font fallback (%s): built-in CJK\n",
                 calendar::language_code(language));
}

void init_runtime_fonts()
{
    init_runtime_font_for_language(g_ui_language);
}

std::string clipped(std::string text, size_t max_chars)
{
    if (text.size() <= max_chars) return text;
    if (max_chars < 4) return text.substr(0, max_chars);
    return text.substr(0, max_chars - 3) + "...";
}

int utf8_char_units(const std::string &text, size_t *index)
{
    unsigned char ch = static_cast<unsigned char>(text[*index]);
    if (ch < 0x80) {
        ++(*index);
        return 1;
    }
    int bytes = 1;
    if ((ch & 0xE0) == 0xC0) bytes = 2;
    else if ((ch & 0xF0) == 0xE0) bytes = 3;
    else if ((ch & 0xF8) == 0xF0) bytes = 4;
    *index = std::min(text.size(), *index + static_cast<size_t>(bytes));
    return 2;
}

bool read_utf8_codepoint(const std::string &text, size_t *index, uint32_t *codepoint,
                         std::string *bytes)
{
    if (*index >= text.size()) return false;
    size_t start = *index;
    unsigned char ch = static_cast<unsigned char>(text[start]);
    int len = 1;
    uint32_t cp = ch;
    if ((ch & 0xE0) == 0xC0) {
        len = 2;
        cp = ch & 0x1F;
    } else if ((ch & 0xF0) == 0xE0) {
        len = 3;
        cp = ch & 0x0F;
    } else if ((ch & 0xF8) == 0xF0) {
        len = 4;
        cp = ch & 0x07;
    }
    if (start + static_cast<size_t>(len) > text.size()) {
        *index = text.size();
        if (codepoint) *codepoint = ch;
        if (bytes) *bytes = text.substr(start, 1);
        return true;
    }
    for (int i = 1; i < len; ++i) {
        unsigned char cont = static_cast<unsigned char>(text[start + static_cast<size_t>(i)]);
        if ((cont & 0xC0) != 0x80) {
            len = 1;
            cp = ch;
            break;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    *index = start + static_cast<size_t>(len);
    if (codepoint) *codepoint = cp;
    if (bytes) *bytes = text.substr(start, static_cast<size_t>(len));
    return true;
}

bool unsupported_display_symbol(uint32_t cp)
{
    if (cp == 0x200D || cp == 0xFE0F) return true;
    if (cp >= 0x1F000) return true;
    if (cp >= 0x2190 && cp <= 0x21FF) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    return false;
}

std::string trim_display_lines(const std::string &text)
{
    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t next = text.find('\n', pos);
        std::string line = next == std::string::npos
                               ? text.substr(pos)
                               : text.substr(pos, next - pos);
        line = calendar::trim_copy(line);
        if (!out.empty()) out.push_back('\n');
        out += line;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return out;
}

std::string display_text(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = 0;
        std::string bytes;
        if (!read_utf8_codepoint(text, &i, &cp, &bytes)) break;
        if (unsupported_display_symbol(cp)) continue;
        out += bytes;
    }
    return trim_display_lines(out);
}

int wrapped_text_height(const std::string &text, int width, int min_height)
{
    int max_units = std::max(6, width / 7);
    int lines = 1;
    int units = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\r') {
            ++i;
            continue;
        }
        if (text[i] == '\n') {
            ++lines;
            units = 0;
            ++i;
            continue;
        }
        int char_units = utf8_char_units(text, &i);
        if (units > 0 && units + char_units > max_units) {
            ++lines;
            units = 0;
        }
        units += char_units;
    }
    return std::max(min_height, lines * 16 + 2);
}

bool is_china_holidays_source(const calendar::CalendarSource &source)
{
    return source.id == "china-holidays";
}

bool is_japan_holidays_source(const calendar::CalendarSource &source)
{
    return source.id == "japan-holidays";
}

bool is_us_holidays_source(const calendar::CalendarSource &source)
{
    return source.id == "us-holidays";
}

bool is_uk_holidays_source(const calendar::CalendarSource &source)
{
    return source.id == "uk-holidays";
}

bool is_germany_holidays_source(const calendar::CalendarSource &source)
{
    return source.id == "germany-holidays";
}

bool is_france_holidays_source(const calendar::CalendarSource &source)
{
    return source.id == "france-holidays";
}

bool is_lunar_source(const calendar::CalendarSource &source)
{
    return source.id == "lunar";
}

bool is_almanac_source(const calendar::CalendarSource &source)
{
    return source.id == "almanac";
}

bool is_builtin_source(const calendar::CalendarSource &source)
{
    return source.id == "default" || is_lunar_source(source) || is_china_holidays_source(source) ||
           is_japan_holidays_source(source) || is_us_holidays_source(source) ||
           is_uk_holidays_source(source) || is_germany_holidays_source(source) ||
           is_france_holidays_source(source) || is_almanac_source(source);
}

bool is_deletable_source(const calendar::CalendarSource &source)
{
    return source.kind == "ics";
}

void remember_removed_builtin(const calendar::CalendarSource &source)
{
    if (!is_builtin_source(source)) return;
    if (std::find(g_settings.removed_builtin_ids.begin(), g_settings.removed_builtin_ids.end(),
                  source.id) == g_settings.removed_builtin_ids.end()) {
        g_settings.removed_builtin_ids.push_back(source.id);
    }
}

std::string source_display_name(const calendar::CalendarSource &source)
{
    if (source.id == "default") return calendar::tr(g_ui_language, calendar::TextKey::Default);
    if (is_lunar_source(source)) return calendar::tr(g_ui_language, calendar::TextKey::Lunar);
    if (is_china_holidays_source(source)) {
        return calendar::tr(g_ui_language, calendar::TextKey::ChinaHolidays);
    }
    if (is_japan_holidays_source(source)) return calendar::tr(g_ui_language, calendar::TextKey::JapanHolidays);
    if (is_us_holidays_source(source)) return calendar::tr(g_ui_language, calendar::TextKey::UsHolidays);
    if (is_uk_holidays_source(source)) return calendar::tr(g_ui_language, calendar::TextKey::UkHolidays);
    if (is_germany_holidays_source(source)) {
        return calendar::tr(g_ui_language, calendar::TextKey::GermanyHolidays);
    }
    if (is_france_holidays_source(source)) {
        return calendar::tr(g_ui_language, calendar::TextKey::FranceHolidays);
    }
    if (is_almanac_source(source)) return calendar::tr(g_ui_language, calendar::TextKey::Almanac);
    return source.name;
}

calendar::CalendarSource *source_by_id(const std::string &id)
{
    for (size_t i = 0; i < g_settings.sources.size(); ++i) {
        if (g_settings.sources[i].id == id) return &g_settings.sources[i];
    }
    return nullptr;
}

const calendar::CalendarSource *source_by_id_const(const std::string &id)
{
    for (size_t i = 0; i < g_settings.sources.size(); ++i) {
        if (g_settings.sources[i].id == id) return &g_settings.sources[i];
    }
    return nullptr;
}

bool source_index_by_id(const std::string &id, size_t *index)
{
    for (size_t i = 0; i < g_settings.sources.size(); ++i) {
        if (g_settings.sources[i].id != id) continue;
        if (index) *index = i;
        return true;
    }
    return false;
}

bool source_enabled(const std::string &id)
{
    calendar::CalendarSource *source = source_by_id(id);
    return source && source->enabled;
}

void sync_lunar_enabled_from_source()
{
    calendar::CalendarSource *source = source_by_id("lunar");
    g_settings.lunar_enabled = source && source->enabled;
}

int source_language_priority(const calendar::CalendarSource &source)
{
    if (source.language == g_ui_language) return 0;
    if (source.language == calendar::Language::Auto) return 1;
    return 2;
}

std::vector<size_t> subscription_source_indexes()
{
    std::vector<size_t> indexes;
    for (size_t i = 0; i < g_settings.sources.size(); ++i) indexes.push_back(i);
    std::stable_sort(indexes.begin(), indexes.end(), [](size_t a, size_t b) {
        const calendar::CalendarSource &left = g_settings.sources[a];
        const calendar::CalendarSource &right = g_settings.sources[b];
        int lp = source_language_priority(left);
        int rp = source_language_priority(right);
        if (lp != rp) return lp < rp;
        if (left.enabled != right.enabled) return left.enabled && !right.enabled;
        return source_display_name(left) < source_display_name(right);
    });
    return indexes;
}

std::vector<size_t> enabled_source_indexes()
{
    std::vector<size_t> indexes;
    std::vector<size_t> ordered = subscription_source_indexes();
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (g_settings.sources[ordered[i]].enabled) indexes.push_back(ordered[i]);
    }
    return indexes;
}

int filter_index_for_source_id(const std::string &id)
{
    int filter_index = 1;
    for (size_t i = 0; i < g_settings.sources.size(); ++i) {
        if (!g_settings.sources[i].enabled) continue;
        if (g_settings.sources[i].id == id) return filter_index;
        ++filter_index;
    }
    return 0;
}

void normalize_filter_index()
{
    int count = static_cast<int>(enabled_source_indexes().size()) + 1;
    if (g_filter_index < 0 || g_filter_index >= count) g_filter_index = 0;
}

std::string filter_id()
{
    if (g_filter_index <= 0) return "";
    std::vector<size_t> indexes = enabled_source_indexes();
    int source_index = g_filter_index - 1;
    if (source_index < 0 || source_index >= static_cast<int>(indexes.size())) return "";
    return g_settings.sources[indexes[static_cast<size_t>(source_index)]].id;
}

std::string filter_label()
{
    if (g_filter_index <= 0) return calendar::tr(g_ui_language, calendar::TextKey::All);
    std::vector<size_t> indexes = enabled_source_indexes();
    int source_index = g_filter_index - 1;
    if (source_index < 0 || source_index >= static_cast<int>(indexes.size())) {
        return calendar::tr(g_ui_language, calendar::TextKey::All);
    }
    return source_display_name(g_settings.sources[indexes[static_cast<size_t>(source_index)]]);
}

std::vector<calendar::Event> visible_events()
{
    std::string id = filter_id();
    if (id.empty()) return g_events;
    std::vector<calendar::Event> out;
    for (size_t i = 0; i < g_events.size(); ++i) {
        if (g_events[i].source_id == id) out.push_back(g_events[i]);
    }
    return out;
}

lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
               uint32_t border = 0, int radius = 3)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    if (border) {
        lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    }
    return obj;
}

lv_obj_t *label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                const lv_font_t *font, uint32_t color, lv_label_long_mode_t mode = LV_LABEL_LONG_DOT)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, mode);
    lv_label_set_text(obj, text.c_str());
    return obj;
}

lv_obj_t *center_label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = label(parent, text, x, y, w, h, font, color);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

void reload_events()
{
    g_ui_language = calendar::resolve_language(g_settings.language, std::getenv("LANG"));
    sync_lunar_enabled_from_source();
    g_events = calendar::load_events(g_settings, g_focus_month, g_ui_language, &g_status);
}

constexpr int kManagerRowLanguage = 0;
constexpr int kManagerRowSubscriptions = 1;
constexpr int kManagerRowSync = 2;
constexpr int kManagerRowCount = 3;

void stop_detail_scroll()
{
    if (g_detail_scroll_timer) {
        lv_timer_delete(g_detail_scroll_timer);
        g_detail_scroll_timer = nullptr;
    }
    g_detail_scroll_panel = nullptr;
    g_detail_scroll_content = nullptr;
    g_detail_scroll_direction = 1;
    g_detail_scroll_offset = 0;
    g_detail_scroll_max = 0;
    g_detail_scroll_hold = 0;
}

void detail_scroll_timer_cb(lv_timer_t *)
{
    if (!g_detail_scroll_content || g_detail_scroll_max <= 0) return;
    if (g_detail_scroll_hold > 0) {
        --g_detail_scroll_hold;
        return;
    }

    if (g_detail_scroll_direction > 0) {
        if (g_detail_scroll_offset >= g_detail_scroll_max) {
            g_detail_scroll_direction = -1;
            g_detail_scroll_hold = 8;
            return;
        }
        ++g_detail_scroll_offset;
    } else {
        if (g_detail_scroll_offset <= 0) {
            g_detail_scroll_direction = 1;
            g_detail_scroll_hold = 8;
            return;
        }
        --g_detail_scroll_offset;
    }
    lv_obj_set_y(g_detail_scroll_content, -g_detail_scroll_offset);
}

void start_detail_scroll_if_needed(int content_bottom, int panel_height)
{
    if (!g_detail_scroll_content) return;
    g_detail_scroll_max = std::max(0, content_bottom - panel_height + 2);
    if (g_detail_scroll_max <= 0) return;
    g_detail_scroll_timer = lv_timer_create(detail_scroll_timer_cb, 140, nullptr);
}

void save_and_reload()
{
    sync_lunar_enabled_from_source();
    calendar::save_settings(g_settings);
    reload_events();
}

void render();
void redraw_all();
void render_loading();

void loading_progress_cb(const calendar::LoadProgress &progress, void *)
{
    pthread_mutex_lock(&g_loading_mutex);
    g_loading_current = progress.current;
    g_loading_total = progress.total;
    g_loading_source = progress.source_name;
    pthread_mutex_unlock(&g_loading_mutex);
}

void *loading_thread_main(void *)
{
    calendar::Settings settings = g_loading_settings;
    calendar::Date focus = g_loading_focus;
    calendar::Language language = g_loading_language;
    std::string status;
    std::vector<calendar::Event> events = calendar::load_events_with_progress(
        settings, focus, language, &status, loading_progress_cb, nullptr);

    pthread_mutex_lock(&g_loading_mutex);
    g_loaded_events = events;
    g_loaded_status = status;
    g_loading_done = true;
    pthread_mutex_unlock(&g_loading_mutex);
    return nullptr;
}

void start_loading_reload(ScreenMode after_mode)
{
    if (g_loading_active) return;
    sync_lunar_enabled_from_source();
    calendar::save_settings(g_settings);
    g_ui_language = calendar::resolve_language(g_settings.language, std::getenv("LANG"));
    g_after_loading_mode = after_mode;
    g_loading_settings = g_settings;
    g_loading_focus = g_focus_month;
    g_loading_language = g_ui_language;
    g_loaded_events.clear();
    g_loaded_status.clear();
    g_loading_current = 0;
    g_loading_total = 0;
    g_loading_source.clear();
    g_loading_status.clear();
    g_loading_done = false;
    g_loading_active = true;
    g_last_loading_current = -1;
    g_last_loading_total = -1;
    g_last_loading_source.clear();
    g_mode = ScreenMode::Loading;
    render();

    if (pthread_create(&g_loading_thread, nullptr, loading_thread_main, nullptr) != 0) {
        g_loading_active = false;
        save_and_reload();
        g_mode = after_mode;
        render();
    }
}

void poll_loading()
{
    if (!g_loading_active) return;

    pthread_mutex_lock(&g_loading_mutex);
    bool done = g_loading_done;
    int current = g_loading_current;
    int total = g_loading_total;
    std::string source = g_loading_source;
    pthread_mutex_unlock(&g_loading_mutex);

    if (!done) {
        if (current != g_last_loading_current || total != g_last_loading_total ||
            source != g_last_loading_source) {
            g_last_loading_current = current;
            g_last_loading_total = total;
            g_last_loading_source = source;
            render_loading();
        }
        return;
    }

    pthread_join(g_loading_thread, nullptr);
    pthread_mutex_lock(&g_loading_mutex);
    g_events = g_loaded_events;
    g_status = g_loaded_status;
    g_loading_done = false;
    g_loading_active = false;
    pthread_mutex_unlock(&g_loading_mutex);
    normalize_filter_index();
    g_mode = g_after_loading_mode;
    render();
}

void sync_focus_month()
{
    g_focus_month.year = g_selected.year;
    g_focus_month.month = g_selected.month;
    g_focus_month.day = 1;
}

void set_selected(calendar::Date date)
{
    g_selected = date;
    sync_focus_month();
    render();
}

void cycle_filter()
{
    int count = static_cast<int>(enabled_source_indexes().size()) + 1;
    if (count <= 0) count = 1;
    g_filter_index = (g_filter_index + 1) % count;
    render();
}

void cycle_language()
{
    if (g_settings.language == calendar::Language::Auto) g_settings.language = calendar::Language::Chinese;
    else if (g_settings.language == calendar::Language::Chinese) g_settings.language = calendar::Language::Japanese;
    else if (g_settings.language == calendar::Language::Japanese) g_settings.language = calendar::Language::English;
    else g_settings.language = calendar::Language::Auto;
    start_loading_reload(ScreenMode::Manager);
}

calendar::Language next_language(calendar::Language language)
{
    if (language == calendar::Language::Auto) return calendar::Language::Chinese;
    if (language == calendar::Language::Chinese) return calendar::Language::Japanese;
    if (language == calendar::Language::Japanese) return calendar::Language::English;
    return calendar::Language::Auto;
}

const uint32_t *border_palette()
{
    static const uint32_t colors[] = {
        0x65D47E, 0x75B7FF, 0xF5D06F, 0xEA4335, 0xD5B8FF, 0x57D7C9
    };
    return colors;
}

constexpr int kStylePaletteCount = 6;

const uint32_t *background_palette()
{
    static const uint32_t colors[] = {
        0x1F3A2A, 0x203449, 0x3A3321, 0x3A211F, 0x2B2438, 0x1E3C3A
    };
    return colors;
}

const uint32_t *style_palette(bool background)
{
    return background ? background_palette() : border_palette();
}

int palette_index(uint32_t color, bool background)
{
    const uint32_t *colors = style_palette(background);
    for (int i = 0; i < kStylePaletteCount; ++i) {
        if ((color & 0xFFFFFF) == colors[i]) return i;
    }
    return -1;
}

uint32_t next_palette_color(uint32_t color, bool background)
{
    int index = palette_index(color, background);
    int next = index < 0 ? 0 : (index + 1) % kStylePaletteCount;
    return style_palette(background)[next];
}

void ensure_border_color(calendar::CalendarSource &source)
{
    if (!source.border_color) source.border_color = border_palette()[0];
}

void ensure_background_color(calendar::CalendarSource &source)
{
    if (!source.background_color) source.background_color = background_palette()[0];
}

void cycle_border_style(calendar::CalendarSource &source)
{
    source.border_enabled = !source.border_enabled;
    ensure_border_color(source);
}

void cycle_border_color(calendar::CalendarSource &source)
{
    source.border_color = next_palette_color(source.border_color, false);
    source.border_enabled = true;
}

void cycle_background_style(calendar::CalendarSource &source)
{
    source.background_enabled = !source.background_enabled;
    ensure_background_color(source);
}

void cycle_background_color(calendar::CalendarSource &source)
{
    source.background_color = next_palette_color(source.background_color, true);
    source.background_enabled = true;
}

std::string palette_color_label(uint32_t color, bool background)
{
    int index = palette_index(color, background);
    if (index < 0) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned int>(color & 0xFFFFFF));
        return buf;
    }
    static const char *en[] = {"Green", "Blue", "Yellow", "Red", "Purple", "Teal"};
    static const char *zh[] = {"绿色", "蓝色", "黄色", "红色", "紫色", "青色"};
    static const char *ja[] = {"緑", "青", "黄", "赤", "紫", "青緑"};
    if (g_ui_language == calendar::Language::Chinese) return zh[index];
    if (g_ui_language == calendar::Language::Japanese) return ja[index];
    return en[index];
}

std::string short_language_label(calendar::Language language)
{
    if (language == calendar::Language::Chinese) return "zh";
    if (language == calendar::Language::Japanese) return "ja";
    if (language == calendar::Language::English) return "en";
    return "auto";
}

void add_ics_url(const std::string &url)
{
    std::string clean = calendar::trim_copy(url);
    if (clean.empty()) return;
    calendar::CalendarSource source;
    source.url = clean;
    source.kind = "ics";
    source.language = calendar::Language::Auto;
    source.enabled = true;
    source.border_enabled = true;
    source.background_enabled = false;
    source.border_color = border_palette()[0];
    source.background_color = background_palette()[0];
    std::string base = clean;
    size_t scheme = base.find("://");
    if (scheme != std::string::npos) base = base.substr(scheme + 3);
    size_t slash = base.find('/');
    if (slash != std::string::npos) base = base.substr(0, slash);
    if (base.empty()) base = "ICS";
    source.name = clipped(base, 18);
    source.id = calendar::sanitize_id(source.name);
    int suffix = 2;
    bool unique = false;
    while (!unique) {
        unique = true;
        for (size_t i = 0; i < g_settings.sources.size(); ++i) {
            if (g_settings.sources[i].id == source.id) unique = false;
        }
        if (!unique) source.id = calendar::sanitize_id(source.name) + "-" + std::to_string(suffix++);
    }
    g_settings.sources.push_back(source);
    g_filter_index = filter_index_for_source_id(source.id);
    start_loading_reload(ScreenMode::Subscriptions);
}

void append_input_text(const char *text)
{
    if (!text) return;
    for (const char *p = text; *p && g_input_url.size() < 240; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch == '\r' || ch == '\n') break;
        if (ch >= 32) g_input_url.push_back(static_cast<char>(ch));
    }
}

void paste_input_text()
{
#if LV_USE_SDL
    char *clip = SDL_GetClipboardText();
    if (clip) {
        append_input_text(clip);
        SDL_free(clip);
        render();
    }
#endif
}

std::string weekday_name(int index)
{
    static const char *en[] = {"M", "T", "W", "T", "F", "S", "S"};
    static const char *zh[] = {"一", "二", "三", "四", "五", "六", "日"};
    static const char *ja[] = {"月", "火", "水", "木", "金", "土", "日"};
    if (g_ui_language == calendar::Language::Chinese) return zh[index];
    if (g_ui_language == calendar::Language::Japanese) return ja[index];
    return en[index];
}

void render_top_bar()
{
    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    std::string title = calendar::tr(g_ui_language, calendar::TextKey::AppTitle);
    title += " ";
    title += calendar::month_key(g_focus_month);
    label(g_root, title, 7, 3, 144, 16, font_text_large(), 0xF2F6F8);
    center_label(g_root, clipped(filter_label(), 15), 154, 4, 95, 14, font_text(), 0x9AD0FF);
    center_label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Manage),
                 258, 4, 55, 14, font_text(), 0xFFE08A);
}

struct DayStyleColors {
    std::vector<uint32_t> borders;
    std::vector<uint32_t> backgrounds;
};

void add_source_style(DayStyleColors *style, const calendar::CalendarSource &source)
{
    if (!source.enabled) return;
    if (source.border_enabled) style->borders.push_back(source.border_color);
    if (source.background_enabled) style->backgrounds.push_back(source.background_color);
}

DayStyleColors day_style_colors(calendar::Date date, const std::vector<calendar::Event> &events,
                                const std::string &source_filter)
{
    DayStyleColors style;
    std::vector<std::string> seen;
    for (size_t i = 0; i < events.size(); ++i) {
        const calendar::Event &event = events[i];
        if (!(event.start <= date && date <= event.end)) continue;
        if (std::find(seen.begin(), seen.end(), event.source_id) != seen.end()) continue;
        const calendar::CalendarSource *source = source_by_id_const(event.source_id);
        if (!source) continue;
        add_source_style(&style, *source);
        seen.push_back(event.source_id);
    }
    const calendar::CalendarSource *lunar = source_by_id_const("lunar");
    if (lunar && (source_filter.empty() || source_filter == "lunar")) {
        add_source_style(&style, *lunar);
    }
    return style;
}

void render_split_background(lv_obj_t *parent, int x, int y, int w, int h,
                             const std::vector<uint32_t> &colors)
{
    if (colors.empty()) return;
    int inner_x = x + 1;
    int inner_y = y + 1;
    int inner_w = std::max(1, w - 2);
    int inner_h = std::max(1, h - 2);
    int used = 0;
    for (size_t i = 0; i < colors.size(); ++i) {
        int segment_w = (i + 1 == colors.size())
            ? inner_w - used
            : std::max(1, inner_w / static_cast<int>(colors.size()));
        rect(parent, inner_x + used, inner_y, segment_w, inner_h, colors[i], 0, 2);
        used += segment_w;
    }
}

void render_split_border(lv_obj_t *parent, int x, int y, int w, int h,
                         const std::vector<uint32_t> &colors)
{
    if (colors.empty()) return;
    int used = 0;
    for (size_t i = 0; i < colors.size(); ++i) {
        int segment_w = (i + 1 == colors.size())
            ? w - used
            : std::max(1, w / static_cast<int>(colors.size()));
        rect(parent, x + used, y, segment_w, 1, colors[i], 0, 0);
        rect(parent, x + used, y + h - 1, segment_w, 1, colors[i], 0, 0);
        used += segment_w;
    }
    rect(parent, x, y, 1, h, colors.front(), 0, 0);
    rect(parent, x + w - 1, y, 1, h, colors.back(), 0, 0);
}

void render_month()
{
    stop_detail_scroll();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    render_top_bar();
    rect(g_root, kLeftX, kLeftY, kLeftW, kLeftH, 0x17222C, 0x253545, 4);
    rect(g_root, kRightX, kRightY, kRightW, kRightH, 0x19242E, 0x2D3C48, 4);

    for (int col = 0; col < 7; ++col) {
        center_label(g_root, weekday_name(col), kLeftX + 4 + col * kCellW, kLeftY + 3,
                     kCellW - 2, 14, font_text(), col >= 5 ? 0xF2B36C : 0x8EA3B0);
    }

    std::string current_filter = filter_id();
    std::vector<calendar::Event> events = visible_events();
    std::vector<calendar::DayInfo> days = calendar::build_month_grid(
        g_focus_month, g_selected, events, g_settings.lunar_enabled, g_ui_language);
    int grid_y = kLeftY + 24;
    for (int i = 0; i < 42; ++i) {
        int row = i / 7;
        int col = i % 7;
        const calendar::DayInfo &day = days[static_cast<size_t>(i)];
        int x = kLeftX + 4 + col * kCellW;
        int y = grid_y + row * kCellH;
        DayStyleColors style = day_style_colors(day.date, events, current_filter);
        uint32_t bg = day.selected ? 0x2F80ED : (day.today ? 0x294659 : 0x17222C);
        uint32_t fg = day.in_month ? 0xF4F7F9 : 0x5F6C76;
        if (day.selected) fg = 0xFFFFFF;
        rect(g_root, x, y, kCellW - 2, kCellH - 1, bg, 0, 3);
        if (!day.selected) {
            render_split_background(g_root, x, y, kCellW - 2, kCellH - 1, style.backgrounds);
        }
        render_split_border(g_root, x, y, kCellW - 2, kCellH - 1, style.borders);
        center_label(g_root, std::to_string(day.date.day), x + 1, y + 2, kCellW - 4, 12,
                     &lv_font_montserrat_12, fg);
        if (day.event_count > 0) {
            std::string count = day.event_count > 9 ? "9+" : std::to_string(day.event_count);
            center_label(g_root, count, x + kCellW - 11, y + 10, 8, 8,
                         &lv_font_montserrat_8, day.selected ? 0xE9FF8A : 0x7BEE91);
        }
    }

    std::vector<calendar::Event> selected_events = calendar::events_for_date(
        g_events, g_selected, current_filter);
    std::string date = calendar::date_key(g_selected);
    label(g_root, date, kRightX + 7, kRightY + 7, kRightW - 14, 14,
          &lv_font_montserrat_12, 0xE9F0F5);
    int panel_x = kRightX + 7;
    int panel_y = kRightY + 25;
    int panel_w = kRightW - 14;
    int panel_h = kRightH - 32;
    g_detail_scroll_panel = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_detail_scroll_panel);
    lv_obj_set_pos(g_detail_scroll_panel, panel_x, panel_y);
    lv_obj_set_size(g_detail_scroll_panel, panel_w, panel_h);
    lv_obj_set_style_bg_opa(g_detail_scroll_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_detail_scroll_panel, 0, 0);
    lv_obj_clear_flag(g_detail_scroll_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_detail_scroll_panel, LV_SCROLLBAR_MODE_OFF);
    g_detail_scroll_content = lv_obj_create(g_detail_scroll_panel);
    lv_obj_remove_style_all(g_detail_scroll_content);
    lv_obj_set_pos(g_detail_scroll_content, 0, 0);
    lv_obj_set_size(g_detail_scroll_content, panel_w, panel_h);
    lv_obj_clear_flag(g_detail_scroll_content, LV_OBJ_FLAG_SCROLLABLE);

    int detail_y = 0;
    if (g_settings.lunar_enabled && (current_filter.empty() || current_filter == "lunar")) {
        label(g_detail_scroll_content, calendar::lunar_label(g_selected, g_ui_language),
              0, detail_y, panel_w, 15, font_text(), 0xF5D06F);
        detail_y += 17;
    }
    if (selected_events.empty()) {
        label(g_detail_scroll_content, calendar::tr(g_ui_language, calendar::TextKey::NoEvents),
              0, detail_y, panel_w, 15, font_text(), 0x8597A4);
        detail_y += 17;
    } else {
        for (size_t i = 0; i < selected_events.size(); ++i) {
            const calendar::Event &event = selected_events[i];
            std::string line = event.all_day ? "" : event.time_text + " ";
            line += display_text(event.title);
            int title_h = wrapped_text_height(line, panel_w, 14);
            label(g_detail_scroll_content, line, 0, detail_y, panel_w, title_h,
                  font_text(), i == 0 ? 0xFFFFFF : 0xC8D3DA, LV_LABEL_LONG_WRAP);
            detail_y += title_h + 1;
            label(g_detail_scroll_content, clipped(event.source_name, 20), 3, detail_y,
                  panel_w - 4, 13, font_text(), 0x75B7FF);
            detail_y += 13;
            if (!event.description.empty()) {
                std::string description = display_text(event.description);
                int desc_h = wrapped_text_height(description, panel_w - 4, 14);
                label(g_detail_scroll_content, description, 3, detail_y, panel_w - 4, desc_h,
                      font_text(), 0xAAB6BD, LV_LABEL_LONG_WRAP);
                detail_y += desc_h + 3;
            }
            if (i + 1 < selected_events.size()) {
                rect(g_detail_scroll_content, 0, detail_y, panel_w, 1, 0x2D3C48, 0, 0);
                detail_y += 5;
            }
        }
    }
    lv_obj_set_height(g_detail_scroll_content, std::max(detail_y, panel_h));
    start_detail_scroll_if_needed(detail_y, panel_h);
}

void render_manager_row(int row, const std::string &left, const std::string &right,
                        uint32_t right_color = 0xCFE6F2)
{
    bool cjk_layout = g_ui_language == calendar::Language::Chinese ||
                      g_ui_language == calendar::Language::Japanese;
    int row_step = cjk_layout ? 22 : 20;
    int row_height = cjk_layout ? 19 : 17;
    int text_height = cjk_layout ? 17 : 14;
    int y = 24 + row * row_step;
    int text_y = y + (cjk_layout ? 1 : 2);
    bool selected = row == g_manager_row;
    rect(g_root, 8, y, 304, row_height, selected ? 0x24496B : 0x16212A,
         selected ? 0x5AA8F2 : 0x243542, 3);
    label(g_root, left, 13, text_y, 165, text_height, font_text(), 0xEEF5F8);
    label(g_root, right, 184, text_y, 122, text_height, font_text(), right_color);
}

void render_manager()
{
    stop_detail_scroll();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Manage),
          8, 3, 170, 16, font_text_large(), 0xF2F6F8);

    render_manager_row(kManagerRowLanguage,
                       calendar::tr(g_ui_language, calendar::TextKey::Language),
                       calendar::language_label(g_settings.language, g_ui_language), 0xF5D06F);
    render_manager_row(kManagerRowSubscriptions,
                       calendar::tr(g_ui_language, calendar::TextKey::Subscriptions),
                       std::to_string(g_settings.sources.size()), 0x8BC9FF);
    render_manager_row(kManagerRowSync, calendar::tr(g_ui_language, calendar::TextKey::Sync),
                       g_status, 0x8BC9FF);
}

int subscription_row_count()
{
    return 1 + static_cast<int>(g_settings.sources.size());
}

constexpr int kSubscriptionEditRowEnabled = 0;
constexpr int kSubscriptionEditRowLanguage = 1;
constexpr int kSubscriptionEditRowBorder = 2;
constexpr int kSubscriptionEditRowBorderColor = 3;
constexpr int kSubscriptionEditRowBackground = 4;
constexpr int kSubscriptionEditRowBackgroundColor = 5;
constexpr int kSubscriptionEditRowCount = 6;

int subscription_first_visible_row()
{
    constexpr int visible_rows = 6;
    int first = std::max(0, g_subscription_row - visible_rows + 1);
    int max_first = std::max(0, subscription_row_count() - visible_rows);
    return std::min(first, max_first);
}

std::string subscription_style_text(const calendar::CalendarSource &source)
{
    std::string text = source.enabled ? calendar::tr(g_ui_language, calendar::TextKey::Enabled)
                                      : calendar::tr(g_ui_language, calendar::TextKey::Disabled);
    text += " ";
    text += short_language_label(source.language);
    text += " ";
    text += source.border_enabled ? "B" : "-";
    text += "/";
    text += source.background_enabled ? "BG" : "-";
    return text;
}

bool selected_subscription_index(size_t *index);

calendar::CalendarSource *editing_subscription()
{
    calendar::CalendarSource *source = source_by_id(g_edit_source_id);
    if (source) return source;
    size_t index = 0;
    if (selected_subscription_index(&index)) {
        g_edit_source_id = g_settings.sources[index].id;
        return &g_settings.sources[index];
    }
    return nullptr;
}

std::string on_off_text(bool enabled)
{
    return calendar::tr(g_ui_language, enabled ? calendar::TextKey::Enabled
                                               : calendar::TextKey::Disabled);
}

void render_subscription_row(int screen_row, int logical_row)
{
    constexpr int row_step = 19;
    constexpr int row_height = 18;
    constexpr int text_height = 16;
    int y = 24 + screen_row * row_step;
    int text_y = y + 1;
    bool selected = logical_row == g_subscription_row;
    rect(g_root, 8, y, 304, row_height, selected ? 0x24496B : 0x16212A,
         selected ? 0x5AA8F2 : 0x243542, 3);
    if (logical_row == 0) {
        label(g_root, calendar::tr(g_ui_language, calendar::TextKey::AddIcs),
              13, text_y, 150, text_height, font_text(), 0xEEF5F8);
        label(g_root, "ICS", 230, text_y, 72, text_height, font_text(), 0x8BC9FF);
        return;
    }

    std::vector<size_t> order = subscription_source_indexes();
    int source_row = logical_row - 1;
    if (source_row < 0 || source_row >= static_cast<int>(order.size())) return;
    const calendar::CalendarSource &source = g_settings.sources[order[static_cast<size_t>(source_row)]];
    uint32_t name_color = source.enabled ? 0xEEF5F8 : 0x87939B;
    label(g_root, clipped(source_display_name(source), 18), 13, text_y, 150, text_height,
          font_text(), name_color);
    label(g_root, subscription_style_text(source), 170, text_y, 136, text_height,
          font_text(), source.enabled ? 0x7FEB92 : 0x9CA7AE);
    if (source.border_enabled) rect(g_root, 152, y + 5, 7, 7, source.border_color, 0, 1);
    if (source.background_enabled) rect(g_root, 161, y + 5, 7, 7, source.background_color, 0, 1);
}

void render_subscriptions()
{
    stop_detail_scroll();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Subscriptions),
          8, 3, 170, 16, font_text_large(), 0xF2F6F8);

    int first = subscription_first_visible_row();
    int count = subscription_row_count();
    for (int screen_row = 0; screen_row < 6; ++screen_row) {
        int logical_row = first + screen_row;
        if (logical_row >= count) break;
        render_subscription_row(screen_row, logical_row);
    }

    std::string footer = "Ent ";
    footer += calendar::tr(g_ui_language, calendar::TextKey::Edit);
    footer += "  A ";
    footer += calendar::tr(g_ui_language, calendar::TextKey::AddIcs);
    footer += "  D ";
    footer += calendar::tr(g_ui_language, calendar::TextKey::Delete);
    label(g_root, clipped(footer, 44), 9, 151, 302, 17, font_text(), 0x617483);
}

void render_subscription_edit_row(int row, const std::string &left, const std::string &right,
                                  uint32_t right_color = 0xCFE6F2,
                                  bool show_swatch = false, uint32_t swatch_color = 0)
{
    bool cjk_layout = g_ui_language == calendar::Language::Chinese ||
                      g_ui_language == calendar::Language::Japanese;
    constexpr int row_step = 20;
    constexpr int row_height = 18;
    int text_height = cjk_layout ? 17 : 14;
    int y = 24 + row * row_step;
    int text_y = y + (cjk_layout ? 1 : 2);
    bool selected = row == g_subscription_edit_row;
    rect(g_root, 8, y, 304, row_height, selected ? 0x24496B : 0x16212A,
         selected ? 0x5AA8F2 : 0x243542, 3);
    label(g_root, left, 13, text_y, 126, text_height, font_text(), 0xEEF5F8);
    int right_x = 166;
    int right_w = 140;
    if (show_swatch) {
        rect(g_root, 148, y + 5, 10, 8, swatch_color, 0, 1);
    }
    label(g_root, right, right_x, text_y, right_w, text_height, font_text(), right_color);
}

void render_subscription_edit()
{
    stop_detail_scroll();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    calendar::CalendarSource *source = editing_subscription();
    if (!source) {
        g_mode = ScreenMode::Subscriptions;
        render_subscriptions();
        return;
    }

    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    label(g_root, clipped(source_display_name(*source), 22),
          8, 3, 196, 16, font_text_large(), 0xF2F6F8);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Edit),
          228, 4, 84, 14, font_text(), 0x9AD0FF);

    uint32_t border_color = source->border_color ? source->border_color : border_palette()[0];
    uint32_t background_color = source->background_color ? source->background_color : background_palette()[0];
    render_subscription_edit_row(kSubscriptionEditRowEnabled,
                                 calendar::tr(g_ui_language, calendar::TextKey::Enable),
                                 on_off_text(source->enabled),
                                 source->enabled ? 0x7FEB92 : 0x9CA7AE);
    render_subscription_edit_row(kSubscriptionEditRowLanguage,
                                 calendar::tr(g_ui_language, calendar::TextKey::Language),
                                 calendar::language_label(source->language, g_ui_language), 0xF5D06F);
    render_subscription_edit_row(kSubscriptionEditRowBorder,
                                 calendar::tr(g_ui_language, calendar::TextKey::Border),
                                 on_off_text(source->border_enabled),
                                 source->border_enabled ? 0x7FEB92 : 0x9CA7AE,
                                 true, border_color);
    render_subscription_edit_row(kSubscriptionEditRowBorderColor,
                                 calendar::tr(g_ui_language, calendar::TextKey::BorderColor),
                                 palette_color_label(border_color, false),
                                 source->border_enabled ? 0xCFE6F2 : 0x6F7B83,
                                 true, border_color);
    render_subscription_edit_row(kSubscriptionEditRowBackground,
                                 calendar::tr(g_ui_language, calendar::TextKey::Background),
                                 on_off_text(source->background_enabled),
                                 source->background_enabled ? 0x7FEB92 : 0x9CA7AE,
                                 true, background_color);
    render_subscription_edit_row(kSubscriptionEditRowBackgroundColor,
                                 calendar::tr(g_ui_language, calendar::TextKey::BackgroundColor),
                                 palette_color_label(background_color, true),
                                 source->background_enabled ? 0xCFE6F2 : 0x6F7B83,
                                 true, background_color);

    std::string footer = "Ent ";
    footer += calendar::tr(g_ui_language, calendar::TextKey::Edit);
    if (is_deletable_source(*source)) {
        footer += "  D ";
        footer += calendar::tr(g_ui_language, calendar::TextKey::Delete);
    }
    label(g_root, clipped(footer, 44), 9, 151, 302, 17, font_text(), 0x617483);
}

void render_ics_input()
{
    stop_detail_scroll();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::IcsUrl),
          8, 3, 150, 16, font_text_large(), 0xF2F6F8);
    rect(g_root, 10, 42, 300, 64, 0x17222C, 0x4B6A7D, 4);
    label(g_root, clipped(g_input_url, 96), 17, 49, 286, 48, &lv_font_montserrat_12,
          0xE9F0F5, LV_LABEL_LONG_WRAP);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Saved),
          10, 132, 300, 16, font_text(), 0x617483);
}

void render_loading()
{
    stop_detail_scroll();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Loading),
          8, 3, 170, 16, font_text_large(), 0xF2F6F8);

    pthread_mutex_lock(&g_loading_mutex);
    int current = g_loading_current;
    int total = g_loading_total;
    std::string source = g_loading_source;
    bool done = g_loading_done;
    pthread_mutex_unlock(&g_loading_mutex);

    int percent = total > 0 ? std::min(100, (current * 100) / total) : 10;
    if (done) percent = 100;
    rect(g_root, 26, 72, 268, 16, 0x17222C, 0x365063, 4);
    int fill_w = std::max(8, (268 * percent) / 100);
    rect(g_root, 27, 73, std::min(266, fill_w), 14, 0x2F80ED, 0, 3);
    std::string progress = std::to_string(percent) + "%";
    center_label(g_root, progress, 122, 96, 76, 14, &lv_font_montserrat_12, 0xD8E8F2);
    if (!source.empty()) {
        center_label(g_root, clipped(source, 32), 24, 118, 272, 14, font_text(), 0x8BC9FF);
    }
}

void render()
{
    if (!g_root) return;
    init_runtime_fonts();
    if (g_mode == ScreenMode::Manager) render_manager();
    else if (g_mode == ScreenMode::Subscriptions) render_subscriptions();
    else if (g_mode == ScreenMode::SubscriptionEdit) render_subscription_edit();
    else if (g_mode == ScreenMode::IcsInput) render_ics_input();
    else if (g_mode == ScreenMode::Loading) render_loading();
    else render_month();
}

void redraw_all()
{
    render();
    if (g_root) {
        lv_obj_invalidate(g_root);
        lv_refr_now(nullptr);
    }
}

void handle_short_back()
{
    if (g_mode == ScreenMode::IcsInput) {
        g_mode = ScreenMode::Subscriptions;
        render();
    } else if (g_mode == ScreenMode::SubscriptionEdit) {
        g_mode = ScreenMode::Subscriptions;
        render();
    } else if (g_mode == ScreenMode::Subscriptions) {
        g_mode = ScreenMode::Manager;
        render();
    } else if (g_mode == ScreenMode::Manager) {
        g_mode = ScreenMode::Month;
        render();
    }
}

bool is_press(const key_item *item)
{
    return item && item->key_state != KBD_KEY_RELEASED;
}

char ascii_char(const key_item *item)
{
    if (!item || !item->utf8[0]) return 0;
    unsigned char ch = static_cast<unsigned char>(item->utf8[0]);
    if (ch < 32 || ch > 126 || item->utf8[1] != '\0') return 0;
    return static_cast<char>(std::tolower(ch));
}

int nav_delta_days(const key_item *item)
{
    if (!item) return 0;
    switch (item->key_code) {
        case KEY_LEFT: return -1;
        case KEY_RIGHT: return 1;
        case KEY_UP: return -7;
        case KEY_DOWN: return 7;
        default: break;
    }
    switch (ascii_char(item)) {
        case 'z': return -1;
        case 'c': return 1;
        case 'f': return -7;
        case 'x': return 7;
        default: return 0;
    }
}

int nav_delta_rows(const key_item *item)
{
    if (!item) return 0;
    switch (item->key_code) {
        case KEY_UP: return -1;
        case KEY_DOWN: return 1;
        default: break;
    }
    switch (ascii_char(item)) {
        case 'f': return -1;
        case 'x': return 1;
        default: return 0;
    }
}

int modified_month_delta(const key_item *item)
{
    if (!item) return 0;
    char ch = ascii_char(item);
    if (ch != '<' && ch != '>') return 0;
    int direction = ch == '<' ? -1 : 1;
    if (item->mods & KBD_MOD_ALT) return direction * 12;
    if (item->mods & KBD_MOD_CTRL) return direction;
    return 0;
}

void handle_esc(const key_item *item)
{
    if (!item || item->key_code != KEY_ESC) return;
    if (item->key_state == KBD_KEY_RELEASED) {
        uint32_t elapsed = g_esc_down_tick ? lv_tick_elaps(g_esc_down_tick) : 0;
        g_esc_down_tick = 0;
        if (elapsed < 850) handle_short_back();
        return;
    }
    if (g_esc_down_tick == 0) g_esc_down_tick = lv_tick_get();
    if (lv_tick_elaps(g_esc_down_tick) >= 850) request_quit();
}

void handle_month_key(const key_item *item)
{
    if (!is_press(item)) return;
    char ch = ascii_char(item);
    int month_delta = modified_month_delta(item);
    if (month_delta != 0) {
        set_selected(calendar::add_months(g_selected, month_delta));
        return;
    }
    int day_delta = nav_delta_days(item);
    if (day_delta != 0) set_selected(calendar::add_days(g_selected, day_delta));
    else if (item->key_code == KEY_PAGEUP || item->key_code == KEY_PREVIOUS) {
        set_selected(calendar::add_months(g_selected, -1));
    } else if (item->key_code == KEY_PAGEDOWN || item->key_code == KEY_NEXT) {
        set_selected(calendar::add_months(g_selected, 1));
    } else if (item->key_code == KEY_TAB || ch == 't') {
        cycle_filter();
    } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER || ch == 'm') {
        g_mode = ScreenMode::Manager;
        g_manager_row = 0;
        render();
    } else if (ch == 'r') {
        start_loading_reload(ScreenMode::Month);
    }
}

int manager_row_count()
{
    return kManagerRowCount;
}

void toggle_source_id(const std::string &source_id, ScreenMode after_mode = ScreenMode::Subscriptions)
{
    calendar::CalendarSource *source = source_by_id(source_id);
    if (!source) return;
    source->enabled = !source->enabled;
    bool enabled = source->enabled;
    sync_lunar_enabled_from_source();
    g_filter_index = enabled ? filter_index_for_source_id(source_id) : 0;
    normalize_filter_index();
    start_loading_reload(after_mode);
}

void handle_manager_activate()
{
    if (g_manager_row == kManagerRowLanguage) cycle_language();
    else if (g_manager_row == kManagerRowSubscriptions) {
        g_subscription_row = 0;
        g_mode = ScreenMode::Subscriptions;
        render();
    } else if (g_manager_row == kManagerRowSync) {
        start_loading_reload(ScreenMode::Manager);
    }
}

void handle_manager_key(const key_item *item)
{
    if (!is_press(item)) return;
    char ch = ascii_char(item);
    int row_delta = nav_delta_rows(item);
    if (row_delta != 0) {
        g_manager_row = std::max(0, std::min(manager_row_count() - 1, g_manager_row + row_delta));
        render();
    } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
        handle_manager_activate();
    } else if (ch == 'a') {
        g_input_url.clear();
        g_mode = ScreenMode::IcsInput;
        render();
    } else if (ch == 'r') {
        reload_events();
        render();
    }
}

bool selected_subscription_index(size_t *index)
{
    if (g_subscription_row <= 0) return false;
    std::vector<size_t> order = subscription_source_indexes();
    int source_row = g_subscription_row - 1;
    if (source_row < 0 || source_row >= static_cast<int>(order.size())) return false;
    if (index) *index = order[static_cast<size_t>(source_row)];
    return true;
}

int subscription_row_for_source_id(const std::string &id)
{
    std::vector<size_t> order = subscription_source_indexes();
    for (size_t i = 0; i < order.size(); ++i) {
        if (g_settings.sources[order[i]].id == id) return static_cast<int>(i) + 1;
    }
    return 0;
}

void open_subscription_edit(size_t index)
{
    if (index >= g_settings.sources.size()) return;
    g_edit_source_id = g_settings.sources[index].id;
    g_subscription_edit_row = 0;
    g_mode = ScreenMode::SubscriptionEdit;
    render();
}

void save_subscription_style_only()
{
    sync_lunar_enabled_from_source();
    calendar::save_settings(g_settings);
    render();
}

void delete_subscription_at_index(size_t index)
{
    if (index >= g_settings.sources.size()) return;
    if (!is_deletable_source(g_settings.sources[index])) return;
    remember_removed_builtin(g_settings.sources[index]);
    g_settings.sources.erase(g_settings.sources.begin() + static_cast<long>(index));
    g_edit_source_id.clear();
    g_subscription_row = std::min(g_subscription_row, subscription_row_count() - 1);
    g_filter_index = 0;
    start_loading_reload(ScreenMode::Subscriptions);
}

void delete_selected_subscription()
{
    size_t index = 0;
    if (!selected_subscription_index(&index)) return;
    delete_subscription_at_index(index);
}

void delete_editing_subscription()
{
    size_t index = 0;
    if (!source_index_by_id(g_edit_source_id, &index)) return;
    delete_subscription_at_index(index);
}

void handle_subscription_key(const key_item *item)
{
    if (!is_press(item)) return;
    char ch = ascii_char(item);
    int row_delta = nav_delta_rows(item);
    if (row_delta != 0) {
        g_subscription_row = std::max(0, std::min(subscription_row_count() - 1,
                                                  g_subscription_row + row_delta));
        render();
        return;
    }

    if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
        if (g_subscription_row == 0) {
            g_input_url.clear();
            g_mode = ScreenMode::IcsInput;
            render();
            return;
        }
        size_t index = 0;
        if (selected_subscription_index(&index)) open_subscription_edit(index);
        return;
    }

    if (ch == 'a') {
        g_input_url.clear();
        g_mode = ScreenMode::IcsInput;
        render();
        return;
    }
    if (ch == 'd') {
        delete_selected_subscription();
        return;
    }

    size_t index = 0;
    if (!selected_subscription_index(&index)) return;
    std::string source_id = g_settings.sources[index].id;
    if (ch == 'b') {
        cycle_border_style(g_settings.sources[index]);
        g_subscription_row = subscription_row_for_source_id(source_id);
        save_subscription_style_only();
    } else if (ch == 'g') {
        cycle_background_style(g_settings.sources[index]);
        g_subscription_row = subscription_row_for_source_id(source_id);
        save_subscription_style_only();
    } else if (ch == 'l') {
        g_settings.sources[index].language = next_language(g_settings.sources[index].language);
        g_subscription_row = subscription_row_for_source_id(source_id);
        save_subscription_style_only();
    } else if (ch == 'r') {
        start_loading_reload(ScreenMode::Subscriptions);
    }
}

void handle_subscription_edit_activate()
{
    calendar::CalendarSource *source = editing_subscription();
    if (!source) return;
    std::string source_id = source->id;
    if (g_subscription_edit_row == kSubscriptionEditRowEnabled) {
        toggle_source_id(source_id, ScreenMode::SubscriptionEdit);
    } else if (g_subscription_edit_row == kSubscriptionEditRowLanguage) {
        source->language = next_language(source->language);
        g_subscription_row = subscription_row_for_source_id(source_id);
        save_subscription_style_only();
    } else if (g_subscription_edit_row == kSubscriptionEditRowBorder) {
        cycle_border_style(*source);
        save_subscription_style_only();
    } else if (g_subscription_edit_row == kSubscriptionEditRowBorderColor) {
        cycle_border_color(*source);
        save_subscription_style_only();
    } else if (g_subscription_edit_row == kSubscriptionEditRowBackground) {
        cycle_background_style(*source);
        save_subscription_style_only();
    } else if (g_subscription_edit_row == kSubscriptionEditRowBackgroundColor) {
        cycle_background_color(*source);
        save_subscription_style_only();
    }
}

void handle_subscription_edit_key(const key_item *item)
{
    if (!is_press(item)) return;
    char ch = ascii_char(item);
    int row_delta = nav_delta_rows(item);
    if (row_delta != 0) {
        g_subscription_edit_row = std::max(0, std::min(kSubscriptionEditRowCount - 1,
                                                       g_subscription_edit_row + row_delta));
        render();
        return;
    }
    if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
        handle_subscription_edit_activate();
        return;
    }
    calendar::CalendarSource *source = editing_subscription();
    if (!source) return;
    if (ch == 'd') {
        delete_editing_subscription();
    } else if (ch == 'b') {
        cycle_border_style(*source);
        save_subscription_style_only();
    } else if (ch == 'g') {
        cycle_background_style(*source);
        save_subscription_style_only();
    } else if (ch == 'l') {
        source->language = next_language(source->language);
        save_subscription_style_only();
    } else if (ch == 'r') {
        start_loading_reload(ScreenMode::SubscriptionEdit);
    }
}

void handle_input_key(const key_item *item)
{
    if (!is_press(item)) return;
    char ch = ascii_char(item);
    if (item->mods & (KBD_MOD_CTRL | KBD_MOD_LOGO)) {
        if (ch == 'v') paste_input_text();
        return;
    }
    if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
        add_ics_url(g_input_url);
        return;
    }
    if (item->key_code == KEY_BACKSPACE) {
        if (!g_input_url.empty()) g_input_url.resize(g_input_url.size() - 1);
        render();
        return;
    }
    if (item->utf8[0]) {
        append_input_text(item->utf8);
        render();
    }
}

void keyboard_event_cb(lv_event_t *event)
{
    key_item *item = static_cast<key_item *>(lv_event_get_param(event));
    if (!item) return;
    if (item->key_code == KEY_ESC) {
        handle_esc(item);
        return;
    }
    if (g_mode == ScreenMode::Month) handle_month_key(item);
    else if (g_mode == ScreenMode::Manager) handle_manager_key(item);
    else if (g_mode == ScreenMode::Subscriptions) handle_subscription_key(item);
    else if (g_mode == ScreenMode::SubscriptionEdit) handle_subscription_edit_key(item);
    else if (g_mode == ScreenMode::IcsInput) handle_input_key(item);
}

int get_st7789v_fbdev(char *dev_path, size_t buf_size)
{
    if (!dev_path || buf_size == 0) return -1;
    FILE *fp = std::fopen("/proc/fb", "r");
    if (!fp) return -1;
    char line[256];
    int fb_num = -1;
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strstr(line, "fb_st7789v") && std::sscanf(line, "%d", &fb_num) == 1) break;
    }
    std::fclose(fp);
    if (fb_num < 0) return -1;
    std::snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
    return 0;
}

#if LV_USE_EVDEV
int evdev_to_lv_key(uint16_t code)
{
    switch (code) {
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_DELETE: return LV_KEY_DEL;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_ENTER: return LV_KEY_ENTER;
        case KEY_TAB: return KEY_TAB;
        case KEY_HOME: return LV_KEY_HOME;
        case KEY_END: return LV_KEY_END;
        default: return code;
    }
}

void keypad_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
    pthread_mutex_lock(&keyboard_mutex);
    if (!STAILQ_EMPTY(&keyboard_queue)) {
        key_item *elm = STAILQ_FIRST(&keyboard_queue);
        STAILQ_REMOVE_HEAD(&keyboard_queue, entries);
        if (g_root) lv_obj_send_event(g_root, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), elm);
        data->key = evdev_to_lv_key(elm->key_code);
        data->state = static_cast<lv_indev_state_t>(elm->key_state);
        data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
        std::free(elm);
    }
    pthread_mutex_unlock(&keyboard_mutex);
}

void lv_linux_indev_init()
{
    const char *keyboard_device = getenv_default(
        "LV_LINUX_KEYBOARD_DEVICE", "/dev/input/by-path/platform-3f804000.i2c-event");
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, keyboard_read_thread, const_cast<char *>(keyboard_device));
    pthread_detach(thread_id);
    g_keyboard_indev = lv_indev_create();
    lv_indev_set_type(g_keyboard_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(g_keyboard_indev, keypad_read_cb);
}
#endif

#if LV_USE_LINUX_FBDEV
void lv_linux_disp_init()
{
    char fbdev[64] = {};
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", NULL);
    if (!device && get_st7789v_fbdev(fbdev, sizeof(fbdev)) == 0) device = fbdev;
    if (!device) device = "/dev/fb0";
    lv_display_t *disp = lv_linux_fbdev_create();
    if (disp) lv_linux_fbdev_set_file(disp, device);
}

#if !LV_USE_EVDEV && !LV_USE_LIBINPUT
void lv_linux_indev_init() {}
#endif

#elif LV_USE_SDL
void lv_linux_disp_init()
{
    lv_display_t *disp = lv_sdl_window_create(kScreenWidth, kScreenHeight);
    lv_sdl_window_set_title(disp, "Calendar");
}

void lv_linux_indev_init()
{
    lv_sdl_mouse_create();
    g_keyboard_indev = lv_sdl_keyboard_create();
}
#else
#error Unsupported display configuration
#endif

void build_ui()
{
    g_root = lv_screen_active();
    lv_obj_remove_style_all(g_root);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_root, keyboard_event_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), NULL);
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_CLICKABLE);

    g_group = lv_group_create();
    lv_group_add_obj(g_group, g_root);
    lv_group_focus_obj(g_root);
    if (g_keyboard_indev) lv_indev_set_group(g_keyboard_indev, g_group);
    render();
}

bool parse_initial_date(const char *text, calendar::Date *date)
{
    if (!text || !text[0] || !date) return false;
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(text, "%d-%d-%d", &year, &month, &day) != 3) return false;
    if (year < 1970 || month < 1 || month > 12 || day < 1) return false;
    if (day > calendar::days_in_month(year, month)) return false;
    date->year = year;
    date->month = month;
    date->day = day;
    return true;
}

void apply_initial_screen(const char *screen)
{
    if (!screen || !screen[0]) return;
    if (std::strcmp(screen, "manager") == 0) {
        g_mode = ScreenMode::Manager;
        g_manager_row = 0;
    } else if (std::strcmp(screen, "subscriptions") == 0) {
        g_mode = ScreenMode::Subscriptions;
        g_subscription_row = 0;
    } else if (std::strcmp(screen, "subscription-edit") == 0) {
        const char *source_id = std::getenv("M5_CALENDAR_SOURCE");
        if (source_id && source_id[0] && source_by_id(source_id)) {
            g_edit_source_id = source_id;
        } else if (!g_settings.sources.empty()) {
            g_edit_source_id = g_settings.sources[0].id;
        }
        g_mode = g_edit_source_id.empty() ? ScreenMode::Subscriptions : ScreenMode::SubscriptionEdit;
        g_subscription_row = subscription_row_for_source_id(g_edit_source_id);
        g_subscription_edit_row = 0;
    } else if (std::strcmp(screen, "input") == 0) {
        g_mode = ScreenMode::IcsInput;
    }
}

}  // namespace

int main()
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    calendar::load_settings(&g_settings);
    g_selected = calendar::today_local();
    parse_initial_date(std::getenv("M5_CALENDAR_DATE"), &g_selected);
    apply_initial_screen(std::getenv("M5_CALENDAR_SCREEN"));
    sync_focus_month();
    g_ui_language = calendar::resolve_language(g_settings.language, std::getenv("LANG"));
    reload_events();

    lv_init();
    init_runtime_fonts();
    lv_linux_disp_init();
    LV_EVENT_KEYBOARD = lv_event_register_id();
    lv_linux_indev_init();
    build_ui();

    while (!g_quit_requested) {
        lv_timer_handler();
        poll_loading();
        usleep(1000);
    }

    if (g_loading_active) pthread_join(g_loading_thread, nullptr);

    return 0;
}
