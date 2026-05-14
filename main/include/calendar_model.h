#ifndef CALENDAR_MODEL_H
#define CALENDAR_MODEL_H

#include <cstdint>
#include <string>
#include <vector>

namespace calendar {

enum class Language {
    Auto,
    English,
    Chinese,
    Japanese,
};

enum class TextKey {
    AppTitle,
    All,
    Default,
    Manage,
    Language,
    Lunar,
    Sync,
    AddIcs,
    IcsUrl,
    Enable,
    Enabled,
    Disabled,
    Today,
    NoEvents,
    Saved,
    Cached,
    Online,
    Error,
    Auto,
    English,
    Chinese,
    Japanese,
    ChinaHolidays,
    JapanHolidays,
    UsHolidays,
    UkHolidays,
    GermanyHolidays,
    FranceHolidays,
    Almanac,
    Weather,
    Subscriptions,
    Edit,
    Delete,
    Border,
    BorderColor,
    Background,
    BackgroundColor,
    Loading,
};

struct Date {
    int year;
    int month;
    int day;
};

struct CalendarSource {
    std::string id;
    std::string name;
    std::string url;
    std::string kind;
    Language language;
    bool enabled;
    bool border_enabled;
    bool background_enabled;
    uint32_t border_color;
    uint32_t background_color;
};

struct Settings {
    Language language;
    bool lunar_enabled;
    std::vector<CalendarSource> sources;
    std::vector<std::string> removed_builtin_ids;
};

struct Event {
    std::string source_id;
    std::string source_name;
    std::string title;
    std::string location;
    std::string description;
    Date start;
    Date end;
    bool all_day;
    std::string time_text;
};

struct DayInfo {
    Date date;
    bool in_month;
    bool today;
    bool selected;
    int event_count;
    std::string lunar;
};

struct LoadProgress {
    int current;
    int total;
    std::string source_name;
};

typedef void (*LoadProgressCallback)(const LoadProgress &progress, void *user_data);

bool operator==(const Date &a, const Date &b);
bool operator!=(const Date &a, const Date &b);
bool operator<(const Date &a, const Date &b);
bool operator<=(const Date &a, const Date &b);

Date today_local();
Date add_days(Date date, int days);
Date add_months(Date date, int months);
int days_in_month(int year, int month);
int weekday_monday0(Date date);
int date_diff_days(Date start, Date end);
std::string date_key(Date date);
std::string month_key(Date date);

Language resolve_language(Language setting, const char *locale_text);
Language detect_system_language();
const char *language_code(Language language);
const char *language_label(Language language, Language ui_language);
const char *tr(Language language, TextKey key);

Settings default_settings();
std::string serialize_settings(const Settings &settings);
Settings parse_settings(const std::string &text);
std::string config_path();
bool load_settings(Settings *settings);
bool save_settings(const Settings &settings);

std::vector<Event> default_events(const Settings &settings, Date window_start, Date window_end,
                                  Language language);
std::vector<Event> parse_ics_events(const std::string &ics, const CalendarSource &source,
                                    Date window_start, Date window_end);
std::string fetch_url_to_string(const std::string &url, bool *ok);
std::vector<Event> load_events_with_progress(const Settings &settings, Date focus_month,
                                             Language language, std::string *status,
                                             LoadProgressCallback callback,
                                             void *user_data);
std::vector<Event> load_events(const Settings &settings, Date focus_month, Language language,
                               std::string *status);

std::vector<DayInfo> build_month_grid(Date month, Date selected,
                                      const std::vector<Event> &events,
                                      bool lunar_enabled, Language language);
std::vector<Event> events_for_date(const std::vector<Event> &events, Date date,
                                   const std::string &source_filter);
std::string lunar_label(Date date, Language language);

std::string trim_copy(const std::string &value);
std::string sanitize_id(const std::string &text);

}  // namespace calendar

#endif
