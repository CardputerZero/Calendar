#include "calendar_model.h"

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
    settings.language = Language::Japanese;
    settings.lunar_enabled = false;
    CalendarSource source;
    source.id = "work";
    source.name = "Work";
    source.url = "https://example.com/work.ics";
    source.kind = "ics";
    source.enabled = true;
    settings.sources.push_back(source);

    Settings parsed = calendar::parse_settings(calendar::serialize_settings(settings));
    assert(parsed.language == Language::Japanese);
    assert(!parsed.lunar_enabled);
    assert(parsed.sources.size() == 2);
    assert(parsed.sources[1].id == "work");
    assert(parsed.sources[1].url == "https://example.com/work.ics");
}

void test_ics_parser()
{
    CalendarSource source;
    source.id = "team";
    source.name = "Team";
    source.kind = "ics";
    source.enabled = true;
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
    Event event;
    event.source_id = "default";
    event.source_name = "Default";
    event.title = "Today";
    event.start = Date{2026, 5, 13};
    event.end = Date{2026, 5, 13};
    event.all_day = true;
    events.push_back(event);

    std::vector<calendar::DayInfo> grid = calendar::build_month_grid(
        Date{2026, 5, 1}, Date{2026, 5, 13}, events, true, Language::English);
    assert(grid.size() == 42);
    bool found_selected = false;
    for (size_t i = 0; i < grid.size(); ++i) {
        if (grid[i].selected) {
            found_selected = true;
            assert(grid[i].event_count == 1);
            assert(!grid[i].lunar.empty());
        }
    }
    assert(found_selected);

    assert(calendar::events_for_date(events, Date{2026, 5, 13}, "default").size() == 1);
    assert(calendar::events_for_date(events, Date{2026, 5, 13}, "missing").empty());
}

}  // namespace

int main()
{
    test_i18n();
    test_settings_roundtrip();
    test_ics_parser();
    test_grid_and_filter();
    std::cout << "calendar model tests passed\n";
    return 0;
}
