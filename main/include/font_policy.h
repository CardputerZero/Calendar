#pragma once

#include <string>
#include <vector>

namespace calendar {

enum class FontProfile {
    UiSans,
    TechnicalMono,
    CjkSimplifiedChinese,
    CjkTraditionalChinese,
    CjkHongKong,
    CjkJapanese,
    CjkKorean,
};

const char *font_profile_name(FontProfile profile);
const char *font_profile_environment(FontProfile profile);

std::vector<std::string> font_candidates(
    FontProfile profile,
    const std::string &executable_dir,
    const std::string &profile_override = std::string(),
    const std::string &legacy_cjk_override = std::string());

}  // namespace calendar
