#include "calendar_model.h"
#include "compat/input_keys.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#if LV_USE_SDL
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#endif

#if LV_USE_EVDEV
#include <pthread.h>
#endif

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kLeftX = 5;
constexpr int kLeftY = 23;
constexpr int kLeftW = 190;
constexpr int kLeftH = 142;
constexpr int kRightX = 202;
constexpr int kRightY = 23;
constexpr int kRightW = 113;
constexpr int kRightH = 142;
constexpr int kCellW = 26;
constexpr int kCellH = 19;

enum class ScreenMode {
    Month,
    Manager,
    IcsInput,
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
std::string g_input_url;
uint32_t g_esc_down_tick = 0;

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

const lv_font_t *font_text()
{
#if LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    return &lv_font_source_han_sans_sc_14_cjk;
#else
    return &lv_font_montserrat_12;
#endif
}

const lv_font_t *font_text_large()
{
#if LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    return &lv_font_source_han_sans_sc_16_cjk;
#else
    return &lv_font_montserrat_14;
#endif
}

std::string clipped(std::string text, size_t max_chars)
{
    if (text.size() <= max_chars) return text;
    if (max_chars < 4) return text.substr(0, max_chars);
    return text.substr(0, max_chars - 3) + "...";
}

std::string filter_id()
{
    if (g_filter_index <= 0) return "";
    int source_index = g_filter_index - 1;
    if (source_index < 0 || source_index >= static_cast<int>(g_settings.sources.size())) return "";
    return g_settings.sources[static_cast<size_t>(source_index)].id;
}

std::string filter_label()
{
    if (g_filter_index <= 0) return calendar::tr(g_ui_language, calendar::TextKey::All);
    int source_index = g_filter_index - 1;
    if (source_index < 0 || source_index >= static_cast<int>(g_settings.sources.size())) {
        return calendar::tr(g_ui_language, calendar::TextKey::All);
    }
    return g_settings.sources[static_cast<size_t>(source_index)].name;
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
    g_events = calendar::load_events(g_settings, g_focus_month, g_ui_language, &g_status);
}

void save_and_reload()
{
    calendar::save_settings(g_settings);
    reload_events();
}

void render();

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
    int count = static_cast<int>(g_settings.sources.size()) + 1;
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
    save_and_reload();
    render();
}

void add_ics_url(const std::string &url)
{
    std::string clean = calendar::trim_copy(url);
    if (clean.empty()) return;
    calendar::CalendarSource source;
    source.url = clean;
    source.kind = "ics";
    source.enabled = true;
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
    g_filter_index = static_cast<int>(g_settings.sources.size());
    save_and_reload();
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

void render_month()
{
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    render_top_bar();
    rect(g_root, kLeftX, kLeftY, kLeftW, kLeftH, 0x17222C, 0x253545, 4);
    rect(g_root, kRightX, kRightY, kRightW, kRightH, 0x19242E, 0x2D3C48, 4);

    for (int col = 0; col < 7; ++col) {
        center_label(g_root, weekday_name(col), kLeftX + 4 + col * kCellW, kLeftY + 5,
                     kCellW - 2, 12, font_text(), col >= 5 ? 0xF2B36C : 0x8EA3B0);
    }

    std::vector<calendar::Event> events = visible_events();
    std::vector<calendar::DayInfo> days = calendar::build_month_grid(
        g_focus_month, g_selected, events, g_settings.lunar_enabled, g_ui_language);
    int grid_y = kLeftY + 20;
    for (int i = 0; i < 42; ++i) {
        int row = i / 7;
        int col = i % 7;
        const calendar::DayInfo &day = days[static_cast<size_t>(i)];
        int x = kLeftX + 4 + col * kCellW;
        int y = grid_y + row * kCellH;
        uint32_t bg = day.selected ? 0x2F80ED : (day.today ? 0x294659 : 0x17222C);
        uint32_t fg = day.in_month ? 0xF4F7F9 : 0x5F6C76;
        if (day.selected) fg = 0xFFFFFF;
        rect(g_root, x, y, kCellW - 2, kCellH - 1, bg, day.event_count ? 0x65D47E : 0, 3);
        center_label(g_root, std::to_string(day.date.day), x + 1, y + 2, kCellW - 4, 12,
                     &lv_font_montserrat_12, fg);
        if (day.event_count > 0) {
            std::string count = day.event_count > 9 ? "9+" : std::to_string(day.event_count);
            center_label(g_root, count, x + kCellW - 11, y + 10, 8, 8,
                         &lv_font_montserrat_8, day.selected ? 0xE9FF8A : 0x7BEE91);
        }
    }

    std::vector<calendar::Event> selected_events = calendar::events_for_date(
        g_events, g_selected, filter_id());
    std::string date = calendar::date_key(g_selected);
    label(g_root, date, kRightX + 7, kRightY + 7, kRightW - 14, 14,
          &lv_font_montserrat_12, 0xE9F0F5);
    int detail_y = kRightY + 25;
    if (g_settings.lunar_enabled) {
        label(g_root, calendar::lunar_label(g_selected, g_ui_language),
              kRightX + 7, detail_y, kRightW - 14, 15, font_text(), 0xF5D06F);
        detail_y += 17;
    }
    if (selected_events.empty()) {
        label(g_root, calendar::tr(g_ui_language, calendar::TextKey::NoEvents),
              kRightX + 7, detail_y, kRightW - 14, 15, font_text(), 0x8597A4);
        detail_y += 17;
    } else {
        for (size_t i = 0; i < selected_events.size() && i < 5; ++i) {
            const calendar::Event &event = selected_events[i];
            std::string line = event.all_day ? "" : event.time_text + " ";
            line += event.title;
            label(g_root, clipped(line, 26), kRightX + 7, detail_y, kRightW - 14, 14,
                  font_text(), i == 0 ? 0xFFFFFF : 0xC8D3DA);
            detail_y += 15;
            label(g_root, clipped(event.source_name, 20), kRightX + 10, detail_y,
                  kRightW - 18, 11, &lv_font_montserrat_10, 0x75B7FF);
            detail_y += 13;
            if (detail_y > kRightY + kRightH - 18) break;
        }
    }
    label(g_root, clipped(g_status, 28), kRightX + 7, kRightY + kRightH - 14,
          kRightW - 14, 11, &lv_font_montserrat_10, 0x607583);
}

void render_manager_row(int row, const std::string &left, const std::string &right,
                        uint32_t right_color = 0xCFE6F2)
{
    int y = 25 + row * 18;
    bool selected = row == g_manager_row;
    rect(g_root, 8, y, 304, 16, selected ? 0x24496B : 0x16212A,
         selected ? 0x5AA8F2 : 0x243542, 3);
    label(g_root, clipped(left, 28), 13, y + 2, 198, 12, font_text(), 0xEEF5F8);
    label(g_root, clipped(right, 16), 214, y + 2, 92, 12, font_text(), right_color);
}

void render_manager()
{
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    rect(g_root, 0, 0, kScreenWidth, 20, 0x1B2733, 0, 0);
    label(g_root, calendar::tr(g_ui_language, calendar::TextKey::Manage),
          8, 3, 170, 16, font_text_large(), 0xF2F6F8);
    label(g_root, calendar::config_path(), 152, 5, 160, 12,
          &lv_font_montserrat_10, 0x718491);

    render_manager_row(0, calendar::tr(g_ui_language, calendar::TextKey::Language),
                       calendar::language_label(g_settings.language, g_ui_language), 0xF5D06F);
    render_manager_row(1, calendar::tr(g_ui_language, calendar::TextKey::Lunar),
                       g_settings.lunar_enabled ? calendar::tr(g_ui_language, calendar::TextKey::Enabled)
                                                : calendar::tr(g_ui_language, calendar::TextKey::Disabled),
                       g_settings.lunar_enabled ? 0x7FEB92 : 0x9CA7AE);
    render_manager_row(2, calendar::tr(g_ui_language, calendar::TextKey::Sync),
                       clipped(g_status, 16), 0x8BC9FF);
    render_manager_row(3, calendar::tr(g_ui_language, calendar::TextKey::AddIcs), "", 0x8BC9FF);

    int max_rows = 8;
    for (size_t i = 0; i < g_settings.sources.size() && static_cast<int>(i) < max_rows; ++i) {
        const calendar::CalendarSource &source = g_settings.sources[i];
        std::string left = source.name;
        if (source.kind == "ics" && !source.url.empty()) left += " ics";
        std::string right = source.enabled ? calendar::tr(g_ui_language, calendar::TextKey::Enabled)
                                           : calendar::tr(g_ui_language, calendar::TextKey::Disabled);
        render_manager_row(4 + static_cast<int>(i), left, right,
                           source.enabled ? 0x7FEB92 : 0x9CA7AE);
    }
}

void render_ics_input()
{
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

void render()
{
    if (!g_root) return;
    if (g_mode == ScreenMode::Manager) render_manager();
    else if (g_mode == ScreenMode::IcsInput) render_ics_input();
    else render_month();
}

void handle_short_back()
{
    if (g_mode == ScreenMode::IcsInput) {
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
    if (item->key_code == KEY_LEFT) set_selected(calendar::add_days(g_selected, -1));
    else if (item->key_code == KEY_RIGHT) set_selected(calendar::add_days(g_selected, 1));
    else if (item->key_code == KEY_UP) set_selected(calendar::add_days(g_selected, -7));
    else if (item->key_code == KEY_DOWN) set_selected(calendar::add_days(g_selected, 7));
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
        reload_events();
        render();
    }
}

int manager_row_count()
{
    return 4 + static_cast<int>(std::min<size_t>(g_settings.sources.size(), 8));
}

void handle_manager_activate()
{
    if (g_manager_row == 0) cycle_language();
    else if (g_manager_row == 1) {
        g_settings.lunar_enabled = !g_settings.lunar_enabled;
        save_and_reload();
        render();
    } else if (g_manager_row == 2) {
        reload_events();
        render();
    } else if (g_manager_row == 3) {
        g_input_url.clear();
        g_mode = ScreenMode::IcsInput;
        render();
    } else {
        int source_index = g_manager_row - 4;
        if (source_index >= 0 && source_index < static_cast<int>(g_settings.sources.size())) {
            g_settings.sources[static_cast<size_t>(source_index)].enabled =
                !g_settings.sources[static_cast<size_t>(source_index)].enabled;
            save_and_reload();
            render();
        }
    }
}

void handle_manager_key(const key_item *item)
{
    if (!is_press(item)) return;
    char ch = ascii_char(item);
    if (item->key_code == KEY_UP) {
        g_manager_row = std::max(0, g_manager_row - 1);
        render();
    } else if (item->key_code == KEY_DOWN) {
        g_manager_row = std::min(manager_row_count() - 1, g_manager_row + 1);
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

void handle_input_key(const key_item *item)
{
    if (!is_press(item)) return;
    if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
        add_ics_url(g_input_url);
        g_mode = ScreenMode::Month;
        render();
        return;
    }
    if (item->key_code == KEY_BACKSPACE) {
        if (!g_input_url.empty()) g_input_url.resize(g_input_url.size() - 1);
        render();
        return;
    }
    if (item->utf8[0]) {
        unsigned char ch = static_cast<unsigned char>(item->utf8[0]);
        if (ch >= 32 && g_input_url.size() < 240) {
            g_input_url += item->utf8;
            render();
        }
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

}  // namespace

int main()
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    calendar::load_settings(&g_settings);
    g_selected = calendar::today_local();
    sync_focus_month();
    g_ui_language = calendar::resolve_language(g_settings.language, std::getenv("LANG"));
    reload_events();

    lv_init();
    lv_linux_disp_init();
    LV_EVENT_KEYBOARD = lv_event_register_id();
    lv_linux_indev_init();
    build_ui();

    while (!g_quit_requested) {
        lv_timer_handler();
        usleep(1000);
    }

    return 0;
}
