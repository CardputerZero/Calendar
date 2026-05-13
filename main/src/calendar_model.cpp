#include "calendar_model.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace calendar {
namespace {

const char *kDefaultSourceId = "default";

bool starts_with(const std::string &text, const std::string &prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::string upper_copy(std::string value)
{
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
    }
    return value;
}

std::vector<std::string> split(const std::string &text, char delim)
{
    std::vector<std::string> parts;
    std::string item;
    std::stringstream ss(text);
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

std::string replace_all(std::string value, const std::string &from, const std::string &to)
{
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

std::string encode_field(std::string value)
{
    value = replace_all(value, "\\", "\\\\");
    value = replace_all(value, "|", "\\p");
    value = replace_all(value, "\n", "\\n");
    return value;
}

std::string decode_field(std::string value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            if (next == 'n') out.push_back('\n');
            else if (next == 'p') out.push_back('|');
            else out.push_back(next);
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string ics_unescape(std::string value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            if (next == 'n' || next == 'N') out.push_back(' ');
            else out.push_back(next);
        } else {
            out.push_back(value[i]);
        }
    }
    return trim_copy(out);
}

bool parse_int(const std::string &text, int *out)
{
    if (text.empty()) return false;
    char *end = NULL;
    long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    *out = static_cast<int>(value);
    return true;
}

bool parse_bool(const std::string &text, bool fallback)
{
    std::string value = upper_copy(trim_copy(text));
    if (value == "1" || value == "TRUE" || value == "YES" || value == "ON") return true;
    if (value == "0" || value == "FALSE" || value == "NO" || value == "OFF") return false;
    return fallback;
}

Language parse_language(const std::string &text)
{
    std::string value = upper_copy(trim_copy(text));
    if (value == "ZH" || value == "ZH-CN" || value == "CHINESE") return Language::Chinese;
    if (value == "JA" || value == "JP" || value == "JAPANESE") return Language::Japanese;
    if (value == "EN" || value == "ENGLISH") return Language::English;
    return Language::Auto;
}

std::tm to_tm(Date date)
{
    std::tm tm_value;
    std::memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = date.year - 1900;
    tm_value.tm_mon = date.month - 1;
    tm_value.tm_mday = date.day;
    tm_value.tm_isdst = -1;
    return tm_value;
}

Date from_time_t(std::time_t value)
{
    std::tm tm_value;
#if defined(_WIN32)
    localtime_s(&tm_value, &value);
#else
    localtime_r(&value, &tm_value);
#endif
    Date date = {tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday};
    return date;
}

std::time_t to_time_t(Date date)
{
    std::tm tm_value = to_tm(date);
    return std::mktime(&tm_value);
}

bool parse_ical_date(const std::string &value, Date *date, std::string *time_text, bool *all_day)
{
    std::string digits;
    for (size_t i = 0; i < value.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(value[i]))) digits.push_back(value[i]);
        if (digits.size() >= 14) break;
    }
    if (digits.size() < 8) return false;

    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_int(digits.substr(0, 4), &year) ||
        !parse_int(digits.substr(4, 2), &month) ||
        !parse_int(digits.substr(6, 2), &day)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;

    date->year = year;
    date->month = month;
    date->day = day;
    if (all_day) *all_day = digits.size() < 12;
    if (time_text) {
        if (digits.size() >= 12) {
            *time_text = digits.substr(8, 2) + ":" + digits.substr(10, 2);
        } else {
            time_text->clear();
        }
    }
    return true;
}

std::vector<std::string> unfold_ics_lines(const std::string &ics)
{
    std::vector<std::string> lines;
    std::stringstream ss(ics);
    std::string raw;
    while (std::getline(ss, raw)) {
        if (!raw.empty() && raw[raw.size() - 1] == '\r') raw.resize(raw.size() - 1);
        if (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t') && !lines.empty()) {
            lines.back() += raw.substr(1);
        } else {
            lines.push_back(raw);
        }
    }
    return lines;
}

struct RawEvent {
    std::string title;
    std::string location;
    std::string description;
    Date start;
    Date end;
    bool has_start;
    bool has_end;
    bool all_day;
    std::string time_text;
    std::string rrule;
};

bool intersects(Date a_start, Date a_end, Date b_start, Date b_end)
{
    return a_start <= b_end && b_start <= a_end;
}

Date add_frequency(Date date, const std::string &freq, int interval)
{
    if (interval < 1) interval = 1;
    if (freq == "DAILY") return add_days(date, interval);
    if (freq == "WEEKLY") return add_days(date, interval * 7);
    if (freq == "MONTHLY") return add_months(date, interval);
    if (freq == "YEARLY") return add_months(date, interval * 12);
    return add_days(date, 1);
}

std::map<std::string, std::string> parse_rrule(const std::string &rrule)
{
    std::map<std::string, std::string> out;
    std::vector<std::string> parts = split(rrule, ';');
    for (size_t i = 0; i < parts.size(); ++i) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        out[upper_copy(parts[i].substr(0, eq))] = upper_copy(parts[i].substr(eq + 1));
    }
    return out;
}

Event make_event(const RawEvent &raw, const CalendarSource &source, Date start, Date end)
{
    Event event;
    event.source_id = source.id;
    event.source_name = source.name;
    event.title = raw.title.empty() ? source.name : raw.title;
    event.location = raw.location;
    event.description = raw.description;
    event.start = start;
    event.end = end;
    event.all_day = raw.all_day;
    event.time_text = raw.time_text;
    return event;
}

void append_expanded_event(std::vector<Event> *events, const RawEvent &raw,
                           const CalendarSource &source, Date window_start, Date window_end)
{
    if (!raw.has_start) return;
    Date raw_end = raw.has_end ? raw.end : raw.start;
    if (raw.all_day && raw.has_end && raw.end != raw.start) {
        raw_end = add_days(raw.end, -1);
    }
    int duration = std::max(0, date_diff_days(raw.start, raw_end));

    if (raw.rrule.empty()) {
        if (intersects(raw.start, raw_end, window_start, window_end)) {
            events->push_back(make_event(raw, source, raw.start, raw_end));
        }
        return;
    }

    std::map<std::string, std::string> rule = parse_rrule(raw.rrule);
    std::string freq = rule["FREQ"];
    if (freq.empty()) {
        if (intersects(raw.start, raw_end, window_start, window_end)) {
            events->push_back(make_event(raw, source, raw.start, raw_end));
        }
        return;
    }

    int interval = 1;
    int count = 0;
    parse_int(rule["INTERVAL"], &interval);
    parse_int(rule["COUNT"], &count);
    Date until = window_end;
    bool has_until = parse_ical_date(rule["UNTIL"], &until, NULL, NULL);
    Date occurrence = raw.start;
    for (int index = 0; index < 500; ++index) {
        if (count > 0 && index >= count) break;
        if (has_until && until < occurrence) break;
        Date occurrence_end = add_days(occurrence, duration);
        if (intersects(occurrence, occurrence_end, window_start, window_end)) {
            events->push_back(make_event(raw, source, occurrence, occurrence_end));
        }
        if (window_end < occurrence && date_diff_days(window_end, occurrence) > 370) break;
        Date next = add_frequency(occurrence, freq, interval);
        if (next == occurrence) break;
        occurrence = next;
    }
}

bool ensure_dir(const std::string &path)
{
    if (path.empty()) return false;
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] != '/' && i + 1 != path.size()) continue;
        if (current.size() <= 1) continue;
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

std::string dirname_of(const std::string &path)
{
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string read_file(const std::string &path)
{
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) return std::string();
    std::string out;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        out.append(buffer, got);
    }
    std::fclose(fp);
    return out;
}

bool write_file(const std::string &path, const std::string &text)
{
    ensure_dir(dirname_of(path));
    FILE *fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), fp) == text.size();
    std::fclose(fp);
    return ok;
}

std::string shell_quote(const std::string &value)
{
    std::string out = "'";
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\'') out += "'\\''";
        else out.push_back(value[i]);
    }
    out.push_back('\'');
    return out;
}

std::string cache_path_for_url(const std::string &url)
{
    const char *home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/.cache" : "/tmp";
    base += "/cardputerzero-calendar";
    return base + "/" + sanitize_id(url) + ".ics";
}

std::string normalize_url(std::string url)
{
    url = trim_copy(url);
    if (starts_with(url, "webcal://")) {
        url = "https://" + url.substr(9);
    }
    return url;
}

}  // namespace

bool operator==(const Date &a, const Date &b)
{
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

bool operator!=(const Date &a, const Date &b)
{
    return !(a == b);
}

bool operator<(const Date &a, const Date &b)
{
    if (a.year != b.year) return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    return a.day < b.day;
}

bool operator<=(const Date &a, const Date &b)
{
    return a < b || a == b;
}

Date today_local()
{
    return from_time_t(std::time(NULL));
}

Date add_days(Date date, int days)
{
    std::time_t value = to_time_t(date);
    value += static_cast<std::time_t>(days) * 24 * 60 * 60;
    return from_time_t(value);
}

Date add_months(Date date, int months)
{
    int month_index = (date.year * 12 + (date.month - 1)) + months;
    Date out = {month_index / 12, month_index % 12 + 1, date.day};
    int dim = days_in_month(out.year, out.month);
    if (out.day > dim) out.day = dim;
    return out;
}

int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

int weekday_monday0(Date date)
{
    std::tm tm_value = to_tm(date);
    std::mktime(&tm_value);
    return (tm_value.tm_wday + 6) % 7;
}

int date_diff_days(Date start, Date end)
{
    std::time_t a = to_time_t(start);
    std::time_t b = to_time_t(end);
    return static_cast<int>((b - a) / (24 * 60 * 60));
}

std::string date_key(Date date)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", date.year, date.month, date.day);
    return buf;
}

std::string month_key(Date date)
{
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%04d-%02d", date.year, date.month);
    return buf;
}

Language resolve_language(Language setting, const char *locale_text)
{
    if (setting != Language::Auto) return setting;
    std::string locale = upper_copy(locale_text ? locale_text : "");
    if (starts_with(locale, "ZH")) return Language::Chinese;
    if (starts_with(locale, "JA") || starts_with(locale, "JP")) return Language::Japanese;
    return Language::English;
}

Language detect_system_language()
{
    const char *locale = std::getenv("LC_ALL");
    if (!locale || !locale[0]) locale = std::getenv("LC_MESSAGES");
    if (!locale || !locale[0]) locale = std::getenv("LANG");
    return resolve_language(Language::Auto, locale);
}

const char *language_code(Language language)
{
    switch (language) {
        case Language::Chinese: return "zh";
        case Language::Japanese: return "ja";
        case Language::English: return "en";
        case Language::Auto:
        default: return "auto";
    }
}

const char *language_label(Language language, Language ui_language)
{
    if (language == Language::Auto) return tr(ui_language, TextKey::Auto);
    if (language == Language::Chinese) return tr(ui_language, TextKey::Chinese);
    if (language == Language::Japanese) return tr(ui_language, TextKey::Japanese);
    return tr(ui_language, TextKey::English);
}

const char *tr(Language language, TextKey key)
{
    Language effective = language == Language::Auto ? Language::English : language;
    if (effective == Language::Chinese) {
        switch (key) {
            case TextKey::AppTitle: return "日历";
            case TextKey::All: return "全部";
            case TextKey::Default: return "默认";
            case TextKey::Manage: return "管理";
            case TextKey::Language: return "语言";
            case TextKey::Lunar: return "农历";
            case TextKey::Sync: return "同步";
            case TextKey::AddIcs: return "+ 订阅";
            case TextKey::IcsUrl: return "ICS 地址";
            case TextKey::Enabled: return "开";
            case TextKey::Disabled: return "关";
            case TextKey::Today: return "今天";
            case TextKey::NoEvents: return "无日程";
            case TextKey::Saved: return "已保存";
            case TextKey::Cached: return "缓存";
            case TextKey::Online: return "在线";
            case TextKey::Error: return "错误";
            case TextKey::Auto: return "自动";
            case TextKey::English: return "英语";
            case TextKey::Chinese: return "中文";
            case TextKey::Japanese: return "日语";
        }
    }
    if (effective == Language::Japanese) {
        switch (key) {
            case TextKey::AppTitle: return "カレンダー";
            case TextKey::All: return "すべて";
            case TextKey::Default: return "標準";
            case TextKey::Manage: return "管理";
            case TextKey::Language: return "言語";
            case TextKey::Lunar: return "旧暦";
            case TextKey::Sync: return "同期";
            case TextKey::AddIcs: return "+ ICS";
            case TextKey::IcsUrl: return "ICS URL";
            case TextKey::Enabled: return "オン";
            case TextKey::Disabled: return "オフ";
            case TextKey::Today: return "今日";
            case TextKey::NoEvents: return "予定なし";
            case TextKey::Saved: return "保存済み";
            case TextKey::Cached: return "キャッシュ";
            case TextKey::Online: return "オンライン";
            case TextKey::Error: return "エラー";
            case TextKey::Auto: return "自動";
            case TextKey::English: return "英語";
            case TextKey::Chinese: return "中国語";
            case TextKey::Japanese: return "日本語";
        }
    }
    switch (key) {
        case TextKey::AppTitle: return "Calendar";
        case TextKey::All: return "All";
        case TextKey::Default: return "Default";
        case TextKey::Manage: return "Manage";
        case TextKey::Language: return "Language";
        case TextKey::Lunar: return "Lunar";
        case TextKey::Sync: return "Sync";
        case TextKey::AddIcs: return "+ ICS";
        case TextKey::IcsUrl: return "ICS URL";
        case TextKey::Enabled: return "On";
        case TextKey::Disabled: return "Off";
        case TextKey::Today: return "Today";
        case TextKey::NoEvents: return "No events";
        case TextKey::Saved: return "Saved";
        case TextKey::Cached: return "Cached";
        case TextKey::Online: return "Online";
        case TextKey::Error: return "Error";
        case TextKey::Auto: return "Auto";
        case TextKey::English: return "English";
        case TextKey::Chinese: return "Chinese";
        case TextKey::Japanese: return "Japanese";
    }
    return "";
}

Settings default_settings()
{
    Settings settings;
    settings.language = Language::Auto;
    settings.lunar_enabled = true;
    CalendarSource source;
    source.id = kDefaultSourceId;
    source.name = "Default";
    source.url.clear();
    source.kind = "default";
    source.enabled = true;
    settings.sources.push_back(source);
    return settings;
}

std::string serialize_settings(const Settings &settings)
{
    std::stringstream out;
    out << "language=" << language_code(settings.language) << "\n";
    out << "lunar=" << (settings.lunar_enabled ? "1" : "0") << "\n";
    for (size_t i = 0; i < settings.sources.size(); ++i) {
        const CalendarSource &source = settings.sources[i];
        out << "calendar=" << encode_field(source.id) << "|"
            << encode_field(source.name) << "|"
            << (source.enabled ? "1" : "0") << "|"
            << encode_field(source.kind) << "|"
            << encode_field(source.url) << "\n";
    }
    return out.str();
}

Settings parse_settings(const std::string &text)
{
    Settings settings = default_settings();
    settings.sources.clear();
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_copy(line.substr(0, eq));
        std::string value = trim_copy(line.substr(eq + 1));
        if (key == "language") {
            settings.language = parse_language(value);
        } else if (key == "lunar") {
            settings.lunar_enabled = parse_bool(value, true);
        } else if (key == "calendar") {
            std::vector<std::string> parts = split(value, '|');
            if (parts.size() < 5) continue;
            CalendarSource source;
            source.id = sanitize_id(decode_field(parts[0]));
            source.name = decode_field(parts[1]);
            source.enabled = parse_bool(parts[2], true);
            source.kind = decode_field(parts[3]);
            source.url = decode_field(parts[4]);
            if (source.id.empty()) source.id = sanitize_id(source.name);
            if (source.name.empty()) source.name = source.id;
            if (source.kind.empty()) source.kind = source.url.empty() ? "default" : "ics";
            settings.sources.push_back(source);
        }
    }
    bool has_default = false;
    for (size_t i = 0; i < settings.sources.size(); ++i) {
        if (settings.sources[i].id == kDefaultSourceId) has_default = true;
    }
    if (!has_default) {
        CalendarSource source;
        source.id = kDefaultSourceId;
        source.name = "Default";
        source.kind = "default";
        source.enabled = true;
        settings.sources.insert(settings.sources.begin(), source);
    }
    return settings;
}

std::string config_path()
{
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    const char *home = std::getenv("HOME");
    std::string base = xdg && xdg[0] ? xdg : (home ? std::string(home) + "/.config" : "/tmp");
    return base + "/cardputerzero-calendar/config.txt";
}

bool load_settings(Settings *settings)
{
    if (!settings) return false;
    std::string text = read_file(config_path());
    if (text.empty()) {
        *settings = default_settings();
        return false;
    }
    *settings = parse_settings(text);
    return true;
}

bool save_settings(const Settings &settings)
{
    return write_file(config_path(), serialize_settings(settings));
}

std::vector<Event> default_events(const Settings &settings, Date window_start, Date window_end,
                                  Language language)
{
    std::vector<Event> events;
    bool enabled = false;
    std::string source_name = tr(language, TextKey::Default);
    for (size_t i = 0; i < settings.sources.size(); ++i) {
        if (settings.sources[i].id == kDefaultSourceId) {
            enabled = settings.sources[i].enabled;
            source_name = settings.sources[i].name.empty() ? source_name : settings.sources[i].name;
        }
    }
    if (!enabled) return events;
    Date today = today_local();
    if (intersects(today, today, window_start, window_end)) {
        Event event;
        event.source_id = kDefaultSourceId;
        event.source_name = source_name;
        event.title = tr(language, TextKey::Today);
        event.start = today;
        event.end = today;
        event.all_day = true;
        events.push_back(event);
    }
    return events;
}

std::vector<Event> parse_ics_events(const std::string &ics, const CalendarSource &source,
                                    Date window_start, Date window_end)
{
    std::vector<Event> events;
    std::vector<std::string> lines = unfold_ics_lines(ics);
    RawEvent raw;
    std::memset(&raw.start, 0, sizeof(raw.start));
    std::memset(&raw.end, 0, sizeof(raw.end));
    raw.has_start = false;
    raw.has_end = false;
    raw.all_day = true;
    bool in_event = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (upper_copy(line) == "BEGIN:VEVENT") {
            raw = RawEvent();
            raw.has_start = false;
            raw.has_end = false;
            raw.all_day = true;
            in_event = true;
            continue;
        }
        if (upper_copy(line) == "END:VEVENT") {
            append_expanded_event(&events, raw, source, window_start, window_end);
            in_event = false;
            continue;
        }
        if (!in_event) continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string property = upper_copy(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        size_t semi = property.find(';');
        std::string name = semi == std::string::npos ? property : property.substr(0, semi);

        if (name == "SUMMARY") raw.title = ics_unescape(value);
        else if (name == "LOCATION") raw.location = ics_unescape(value);
        else if (name == "DESCRIPTION") raw.description = ics_unescape(value);
        else if (name == "DTSTART") {
            bool all_day = true;
            if (parse_ical_date(value, &raw.start, &raw.time_text, &all_day)) {
                raw.has_start = true;
                raw.all_day = all_day || property.find("VALUE=DATE") != std::string::npos;
            }
        } else if (name == "DTEND") {
            bool all_day = true;
            std::string ignored_time;
            if (parse_ical_date(value, &raw.end, &ignored_time, &all_day)) raw.has_end = true;
        } else if (name == "RRULE") {
            raw.rrule = value;
        }
    }

    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.time_text != b.time_text) return a.time_text < b.time_text;
        return a.title < b.title;
    });
    return events;
}

std::string fetch_url_to_string(const std::string &input_url, bool *ok)
{
    if (ok) *ok = false;
    std::string url = normalize_url(input_url);
    if (!(starts_with(url, "https://") || starts_with(url, "http://"))) return std::string();

    std::string command = "curl -LfsS --max-time 12 " + shell_quote(url) + " 2>/dev/null";
    FILE *pipe = popen(command.c_str(), "r");
    std::string out;
    if (pipe) {
        char buffer[2048];
        while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
        int rc = pclose(pipe);
        if (rc == 0 && !out.empty()) {
            if (ok) *ok = true;
            return out;
        }
    }

    command = "wget -q -T 12 -O - " + shell_quote(url) + " 2>/dev/null";
    pipe = popen(command.c_str(), "r");
    if (pipe) {
        out.clear();
        char buffer[2048];
        while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
        int rc = pclose(pipe);
        if (rc == 0 && !out.empty()) {
            if (ok) *ok = true;
            return out;
        }
    }
    return std::string();
}

std::vector<Event> load_events(const Settings &settings, Date focus_month, Language language,
                               std::string *status)
{
    Date window_start = add_days({focus_month.year, focus_month.month, 1}, -45);
    Date window_end = add_days(add_months({focus_month.year, focus_month.month, 1}, 1), 45);
    std::vector<Event> events = default_events(settings, window_start, window_end, language);
    int online_count = 0;
    int cached_count = 0;
    int error_count = 0;

    for (size_t i = 0; i < settings.sources.size(); ++i) {
        const CalendarSource &source = settings.sources[i];
        if (!source.enabled || source.url.empty()) continue;
        bool ok = false;
        std::string ics = fetch_url_to_string(source.url, &ok);
        std::string cache_path = cache_path_for_url(source.url);
        if (ok) {
            write_file(cache_path, ics);
            ++online_count;
        } else {
            ics = read_file(cache_path);
            if (!ics.empty()) ++cached_count;
            else ++error_count;
        }
        if (ics.empty()) continue;
        std::vector<Event> parsed = parse_ics_events(ics, source, window_start, window_end);
        events.insert(events.end(), parsed.begin(), parsed.end());
    }

    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.time_text != b.time_text) return a.time_text < b.time_text;
        return a.title < b.title;
    });

    if (status) {
        std::stringstream ss;
        ss << tr(language, TextKey::Online) << ":" << online_count
           << " " << tr(language, TextKey::Cached) << ":" << cached_count;
        if (error_count) ss << " " << tr(language, TextKey::Error) << ":" << error_count;
        *status = ss.str();
    }
    return events;
}

std::vector<DayInfo> build_month_grid(Date month, Date selected,
                                      const std::vector<Event> &events,
                                      bool lunar_enabled, Language language)
{
    Date first = {month.year, month.month, 1};
    Date start = add_days(first, -weekday_monday0(first));
    Date today = today_local();
    std::vector<DayInfo> days;
    for (int i = 0; i < 42; ++i) {
        Date date = add_days(start, i);
        DayInfo info;
        info.date = date;
        info.in_month = date.month == month.month;
        info.today = date == today;
        info.selected = date == selected;
        info.event_count = static_cast<int>(events_for_date(events, date, "").size());
        info.lunar = lunar_enabled ? lunar_label(date, language) : "";
        days.push_back(info);
    }
    return days;
}

std::vector<Event> events_for_date(const std::vector<Event> &events, Date date,
                                   const std::string &source_filter)
{
    std::vector<Event> out;
    for (size_t i = 0; i < events.size(); ++i) {
        const Event &event = events[i];
        if (!source_filter.empty() && event.source_id != source_filter) continue;
        if (event.start <= date && date <= event.end) out.push_back(event);
    }
    return out;
}

std::string lunar_label(Date date, Language language)
{
    static const char *zh_months[] = {"正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"};
    static const char *zh_days[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};
    Date base = {2024, 2, 10};
    int days = date_diff_days(base, date);
    int cycle = days >= 0 ? days : days - 353;
    int lunar_day_index = ((cycle % 354) + 354) % 354;
    int lunar_month = lunar_day_index / 29 + 1;
    if (lunar_month > 12) lunar_month = 12;
    int lunar_day = lunar_day_index % 29;
    if (language == Language::Japanese) {
        std::stringstream ss;
        ss << "旧" << lunar_month << "/" << (lunar_day + 1);
        return ss.str();
    }
    if (language == Language::English) {
        std::stringstream ss;
        ss << "L" << lunar_month << "/" << (lunar_day + 1);
        return ss.str();
    }
    std::string label = "农";
    label += zh_months[lunar_month - 1];
    label += zh_days[lunar_day];
    return label;
}

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string sanitize_id(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isalnum(ch)) out.push_back(static_cast<char>(std::tolower(ch)));
        else if (!out.empty() && out[out.size() - 1] != '-') out.push_back('-');
    }
    while (!out.empty() && out[out.size() - 1] == '-') out.resize(out.size() - 1);
    if (out.empty()) out = "calendar";
    return out;
}

}  // namespace calendar
