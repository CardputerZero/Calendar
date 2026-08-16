#include "calendar_model.h"
#include "font_policy.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using calendar::CalendarSource;
using calendar::Date;
using calendar::Event;
using calendar::Language;
using calendar::Settings;

namespace {

bool contains_fragment(const std::vector<std::string> &values, const std::string &fragment)
{
    return std::any_of(values.begin(), values.end(), [&fragment](const std::string &value) {
        return value.find(fragment) != std::string::npos;
    });
}

void test_font_policy()
{
    std::vector<std::string> latin = calendar::font_candidates(
        calendar::FontProfile::UiSans, "/opt/calendar/bin", "/custom/DejaVuSans.ttf");
    assert(!latin.empty());
    assert(latin.front() == "/custom/DejaVuSans.ttf");
    assert(contains_fragment(latin, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"));
    assert(std::string(calendar::font_profile_environment(
        calendar::FontProfile::UiSans)) == "M5_CALENDAR_FONT_LATIN");

    std::vector<std::string> mono = calendar::font_candidates(
        calendar::FontProfile::TechnicalMono, "/opt/calendar/bin");
    assert(contains_fragment(mono, "JetBrainsMono-Regular.ttf"));

    std::vector<std::string> japanese = calendar::font_candidates(
        calendar::FontProfile::CjkJapanese, "/opt/calendar/bin");
    assert(contains_fragment(japanese, "NotoSansCJKjp-Regular.otf"));
    assert(contains_fragment(japanese, "NotoSansJP-Regular.ttf"));
    assert(contains_fragment(japanese, "NotoSansCJK-Regular.ttc"));
    assert(!contains_fragment(japanese, "NotoSansSC"));

    std::vector<std::string> korean = calendar::font_candidates(
        calendar::FontProfile::CjkKorean, "/opt/calendar/bin");
    assert(contains_fragment(korean, "NotoSansCJKkr-Regular.otf"));
    assert(contains_fragment(korean, "NotoSansKR-Regular.ttf"));
    assert(!contains_fragment(korean, ".ttc"));
}

void test_i18n()
{
    assert(calendar::resolve_language(Language::Auto, "zh_CN.UTF-8") == Language::Chinese);
    assert(calendar::resolve_language(Language::Auto, "ja_JP.UTF-8") == Language::Japanese);
    assert(calendar::resolve_language(Language::Auto, "en_US.UTF-8") == Language::English);
    assert(std::string(calendar::tr(Language::Chinese, calendar::TextKey::Lunar)) == "农历");
}

void test_settings_roundtrip()
{
    Settings settings = calendar::default_settings();
    for (size_t i = 0; i < settings.sources.size(); ++i) {
        assert(settings.sources[i].id != "default");
    }
    settings.language = Language::Japanese;
    settings.lunar_enabled = false;
    CalendarSource source;
    source.id = "work";
    source.name = "Work";
    source.url = "https://example.com/work.ics";
    source.kind = "ics";
    source.language = Language::English;
    source.enabled = true;
    source.border_enabled = true;
    source.background_enabled = true;
    source.border_color = 0x75B7FF;
    source.background_color = 0x203449;
    settings.sources.push_back(source);

    Settings parsed = calendar::parse_settings(calendar::serialize_settings(settings));
    for (size_t i = 0; i < parsed.sources.size(); ++i) {
        assert(parsed.sources[i].id != "default");
    }
    assert(parsed.language == Language::Japanese);
    assert(!parsed.lunar_enabled);
    assert(parsed.sources.size() == 9);
    assert(parsed.sources[0].id == "lunar");
    assert(!parsed.sources[0].enabled);
    assert(parsed.sources[1].id == "china-holidays");
    assert(!parsed.sources[1].enabled);
    assert(parsed.sources[1].url.find("rilipro.com/HoliBack") != std::string::npos);
    assert(parsed.sources[2].id == "japan-holidays");
    assert(parsed.sources[3].id == "us-holidays");
    assert(parsed.sources[4].id == "uk-holidays");
    assert(parsed.sources[5].id == "germany-holidays");
    assert(parsed.sources[6].id == "france-holidays");
    assert(parsed.sources[7].id == "almanac");
    assert(parsed.sources[8].id == "work");
    assert(parsed.sources[8].url == "https://example.com/work.ics");
    assert(parsed.sources[8].language == Language::English);
    assert(parsed.sources[8].border_enabled);
    assert(parsed.sources[8].background_enabled);
    assert(parsed.sources[8].border_color == 0x75B7FF);

    Settings migrated = calendar::parse_settings(
        "language=en\n"
        "lunar=1\n"
        "calendar=default|Default|1|default|\n");
    assert(migrated.sources.size() == 8);
    assert(migrated.sources[0].id == "lunar");
    assert(migrated.sources[0].enabled);
    assert(migrated.lunar_enabled);
    assert(migrated.sources[1].id == "china-holidays");
    assert(!migrated.sources[1].enabled);
    assert(migrated.sources[2].id == "japan-holidays");
    assert(migrated.sources[7].id == "almanac");

    Settings legacy_weather_removed = calendar::parse_settings(
        "language=en\n"
        "calendar=weather|Weather|1|ics|https://rilipro.com/weather/calendar.php?location=Guangdong_Shenzhen_Shenzhen&days=15&title=both|auto|1|75B7FF|1|203449\n");
    assert(legacy_weather_removed.sources.size() == 8);
    for (size_t i = 0; i < legacy_weather_removed.sources.size(); ++i) {
        assert(legacy_weather_removed.sources[i].id != "weather");
    }

    Settings deleted_builtin = calendar::parse_settings(
        "language=en\n"
        "removed_builtin=china-holidays\n"
        "removed_builtin=japan-holidays\n");
    assert(deleted_builtin.sources.size() == 6);
    bool saw_removed_china = false;
    bool saw_removed_japan = false;
    for (size_t i = 0; i < deleted_builtin.sources.size(); ++i) {
        assert(deleted_builtin.sources[i].id != "china-holidays");
        assert(deleted_builtin.sources[i].id != "japan-holidays");
    }
    for (size_t i = 0; i < deleted_builtin.removed_builtin_ids.size(); ++i) {
        if (deleted_builtin.removed_builtin_ids[i] == "china-holidays") saw_removed_china = true;
        if (deleted_builtin.removed_builtin_ids[i] == "japan-holidays") saw_removed_japan = true;
    }
    assert(saw_removed_china);
    assert(saw_removed_japan);

    Date today = calendar::today_local();
    std::vector<Event> defaults = calendar::default_events(
        calendar::default_settings(), today, today, Language::Chinese);
    assert(defaults.empty());
}

void test_ics_parser()
{
    CalendarSource source;
    source.id = "team";
    source.name = "Team";
    source.kind = "ics";
    source.language = Language::Auto;
    source.enabled = true;
    source.border_enabled = false;
    source.background_enabled = false;
    source.border_color = 0x65D47E;
    source.background_color = 0x243542;
    source.url = "https://example.com/team.ics";

    const std::string ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "SUMMARY:Planning\\, Sprint\r\n"
        "DTSTART:20260513T090000Z\r\n"
        "DTEND:20260513T100000Z\r\n"
        "LOCATION:Room 1\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "SUMMARY:Daily Standup\r\n"
        "DTSTART;VALUE=DATE:20260514\r\n"
        "RRULE:FREQ=DAILY;COUNT=3\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    std::vector<Event> events = calendar::parse_ics_events(
        ics, source, Date{2026, 5, 1}, Date{2026, 5, 31});
    assert(events.size() == 4);
    assert(events[0].title == "Planning, Sprint");
    assert(events[0].time_text == "09:00");
    assert(events[1].title == "Daily Standup");
    Date expected_last = {2026, 5, 16};
    assert(events[3].start == expected_last);
}

void test_grid_and_filter()
{
    std::vector<Event> events;
    std::vector<calendar::DayInfo> grid = calendar::build_month_grid(
        Date{2026, 5, 1}, Date{2026, 5, 13}, events, false, Language::English);
    assert(grid.size() == 42);
    bool found_selected = false;
    for (size_t i = 0; i < grid.size(); ++i) {
        if (grid[i].selected) {
            found_selected = true;
            assert(grid[i].event_count == 0);
            assert(grid[i].lunar.empty());
        }
    }
    assert(found_selected);

    assert(calendar::events_for_date(events, Date{2026, 5, 13}, "").empty());
}

}  // namespace

int main()
{
    test_font_policy();
    test_i18n();
    test_settings_roundtrip();
    test_ics_parser();
    test_grid_and_filter();
    std::cout << "calendar model tests passed\n";
    return 0;
}
