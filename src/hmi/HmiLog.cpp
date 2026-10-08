// =============================================================================
//  hmi/HmiLog.cpp - 1.11.14 : les niveaux de IHM_LOG et de la Console
// =============================================================================
#include "HmiLog.hpp"

#include <array>
#include <cctype>
#include <string>

namespace hmi {

namespace {
constexpr std::array<std::string_view, kLogLevelCount> kNames = {"TRACE", "DEBUG", "INFO", "SUCCESS", "WARNING", "ERROR", "CRITICAL"};
constexpr std::array<std::string_view, kLogLevelCount> kLabels = {
    "Trace", "D\xC3\xA9" "bogage", "Info", "Succ\xC3\xA8s", "Avertissement", "Erreur", "Critique"};

bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
} // namespace

std::string_view logLevelName(LogLevel l) noexcept {
    const auto i = static_cast<std::size_t>(l);
    return i < kNames.size() ? kNames[i] : std::string_view("INFO");
}

std::string_view logLevelLabel(LogLevel l) noexcept {
    const auto i = static_cast<std::size_t>(l);
    return i < kLabels.size() ? kLabels[i] : std::string_view("Info");
}

std::optional<LogLevel> logLevelByName(std::string_view text) noexcept {
    text = trim(text);
    if (const auto hash = text.find('#'); hash != std::string_view::npos) {
        if (!same(trim(text.substr(0, hash)), kLogLevelType)) return std::nullopt;
        text = trim(text.substr(hash + 1));
    }
    for (std::size_t i = 0; i < kNames.size(); ++i)
        if (same(text, kNames[i])) return static_cast<LogLevel>(i);
    return std::nullopt;
}

std::optional<LogLevel> logLevelOf(long long value) noexcept {
    if (value < 0 || value >= kLogLevelCount) return std::nullopt;
    return static_cast<LogLevel>(value);
}

LogLevel logLevelOfKind(std::string_view kind) noexcept {
    return same(kind, "Erreur") ? LogLevel::Error : LogLevel::Info;
}

} // namespace hmi
