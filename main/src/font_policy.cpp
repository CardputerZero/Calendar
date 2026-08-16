#include "font_policy.h"

#include <algorithm>

namespace calendar {
namespace {

void push_unique(std::vector<std::string> *paths, const std::string &path)
{
    if (path.empty()) return;
    if (std::find(paths->begin(), paths->end(), path) == paths->end()) paths->push_back(path);
}

void push_application_candidates(std::vector<std::string> *paths, const std::string &name,
                                 const std::string &executable_dir)
{
    push_unique(paths, "fonts/" + name);
    push_unique(paths, name);
    push_unique(paths, executable_dir + "/fonts/" + name);
    push_unique(paths, executable_dir + "/../fonts/" + name);
    push_unique(paths, executable_dir + "/../share/font/" + name);
    push_unique(paths, executable_dir + "/../Resources/fonts/" + name);
    push_unique(paths, "/usr/share/APPLaunch/fonts/" + name);
    push_unique(paths, "/usr/share/APPLaunch/share/font/" + name);
}

void push_system_candidates(std::vector<std::string> *paths, const std::string &name)
{
    push_unique(paths, "/usr/share/fonts/truetype/dejavu/" + name);
    push_unique(paths, "/usr/share/fonts/truetype/jetbrains-mono/" + name);
    push_unique(paths, "/usr/share/fonts/truetype/jetbrains/" + name);
    push_unique(paths, "/usr/share/fonts/truetype/noto/" + name);
    push_unique(paths, "/usr/share/fonts/opentype/noto/" + name);
    push_unique(paths, "/usr/share/fonts/TTF/" + name);
    push_unique(paths, "/usr/share/fonts/" + name);
    push_unique(paths, "/usr/local/share/fonts/" + name);
}

std::vector<std::string> profile_file_names(FontProfile profile)
{
    switch (profile) {
    case FontProfile::UiSans:
        return {"DejaVuSans.ttf"};
    case FontProfile::TechnicalMono:
        return {"JetBrainsMono-Regular.ttf"};
    case FontProfile::CjkSimplifiedChinese:
        return {"NotoSansSC-Regular.ttf", "AlibabaPuHuiTi-3-55-Regular.ttf",
                "NotoSansCJKsc-Regular.otf", "NotoSansCJKsc-Regular.ttf",
                "NotoSansSC-Regular.otf"};
    case FontProfile::CjkTraditionalChinese:
        return {"NotoSansCJKtc-Regular.otf", "NotoSansCJKtc-Regular.ttf",
                "NotoSansTC-Regular.otf", "NotoSansTC-Regular.ttf"};
    case FontProfile::CjkHongKong:
        return {"NotoSansCJKhk-Regular.otf", "NotoSansCJKhk-Regular.ttf",
                "NotoSansHK-Regular.otf", "NotoSansHK-Regular.ttf"};
    case FontProfile::CjkJapanese:
        return {"NotoSansCJK-Regular.ttc", "NotoSansCJKjp-Regular.otf",
                "NotoSansCJKjp-Regular.ttf", "NotoSansJP-Regular.otf",
                "NotoSansJP-Regular.ttf"};
    case FontProfile::CjkKorean:
        return {"NotoSansCJKkr-Regular.otf", "NotoSansCJKkr-Regular.ttf",
                "NotoSansKR-Regular.otf", "NotoSansKR-Regular.ttf"};
    }
    return {};
}

bool is_cjk_profile(FontProfile profile)
{
    return profile != FontProfile::UiSans && profile != FontProfile::TechnicalMono;
}

}  // namespace

const char *font_profile_name(FontProfile profile)
{
    switch (profile) {
    case FontProfile::UiSans: return "ui-sans";
    case FontProfile::TechnicalMono: return "technical-mono";
    case FontProfile::CjkSimplifiedChinese: return "cjk-sc";
    case FontProfile::CjkTraditionalChinese: return "cjk-tc";
    case FontProfile::CjkHongKong: return "cjk-hk";
    case FontProfile::CjkJapanese: return "cjk-jp";
    case FontProfile::CjkKorean: return "cjk-kr";
    }
    return "unknown";
}

const char *font_profile_environment(FontProfile profile)
{
    switch (profile) {
    case FontProfile::UiSans: return "M5_CALENDAR_FONT_LATIN";
    case FontProfile::TechnicalMono: return "M5_CALENDAR_FONT_MONO";
    case FontProfile::CjkSimplifiedChinese: return "M5_CALENDAR_FONT_ZH";
    case FontProfile::CjkTraditionalChinese: return "M5_CALENDAR_FONT_ZH_TW";
    case FontProfile::CjkHongKong: return "M5_CALENDAR_FONT_ZH_HK";
    case FontProfile::CjkJapanese: return "M5_CALENDAR_FONT_JA";
    case FontProfile::CjkKorean: return "M5_CALENDAR_FONT_KO";
    }
    return "";
}

std::vector<std::string> font_candidates(FontProfile profile,
                                         const std::string &executable_dir,
                                         const std::string &profile_override,
                                         const std::string &legacy_cjk_override)
{
    std::vector<std::string> paths;
    push_unique(&paths, profile_override);
    if (is_cjk_profile(profile)) push_unique(&paths, legacy_cjk_override);

    std::vector<std::string> names = profile_file_names(profile);
    for (const std::string &name : names) {
        push_application_candidates(&paths, name, executable_dir);
        push_system_candidates(&paths, name);
    }
    return paths;
}

}  // namespace calendar
