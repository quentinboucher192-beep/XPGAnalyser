#include "Settings.hpp"

#include "../core/Edition.hpp"   // 1.12.0 : les reglages de chaque application

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {
namespace {

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

// std::stof and std::ostringstream honour the global C locale, and SDL calls
// setlocale() on start-up. On a French system that turns "0.2200" into "0,2200"
// on the way out and mis-parses it on the way back in - and since the list
// separator here is a comma, a saved splitter ratio came back as twice as many
// bogus numbers. std::to_chars / std::from_chars are locale-independent by
// specification, which is exactly what a settings file needs.
bool parseFloat(std::string_view text, float& out) {
    const auto* first = text.data();
    const auto* last  = text.data() + text.size();
    while (first < last && (*first == ' ' || *first == '\t')) ++first;
    return std::from_chars(first, last, out).ec == std::errc{};
}

std::string formatFloat(float value) {
    std::array<char, 32> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value,
                                   std::chars_format::fixed, 4);
    return res.ec == std::errc{} ? std::string(buf.data(), res.ptr) : std::string("0");
}

std::string envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

} // namespace

std::string Settings::sharedFolder() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const auto base = envOr("APPDATA", ".");
    const auto dir  = fs::path(base) / "XpgAnalyzer";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const auto base = (xdg && *xdg) ? fs::path(xdg) : fs::path(envOr("HOME", ".")) / ".config";
    const auto dir  = base / "xpg-analyzer";
#endif
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

std::string Settings::defaultPath() {
    namespace fs = std::filesystem;
    const fs::path shared(sharedFolder());
    const auto label = core::editionLabel();
    if (label.empty()) return (shared / "settings.txt").string();
    const fs::path dir = shared / std::string(label);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path mine = dir / "settings.txt";
    // 1.12.0 : le premier lancement d'une edition reprend les reglages de la 1.11
    // (le theme, les dispositions, les projets recents) ; ensuite, chacune les siens.
    if (!fs::exists(mine, ec) && fs::is_regular_file(shared / "settings.txt", ec))
        fs::copy_file(shared / "settings.txt", mine, fs::copy_options::skip_existing, ec);
    return mine.string();
}

bool Settings::load(std::string path) {
    path_ = std::move(path);
    values_.clear();
    dirty_ = false;

    std::ifstream in(path_);
    if (!in) return false;                 // first run: not an error

    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        values_[trim(trimmed.substr(0, eq))] = trim(trimmed.substr(eq + 1));
    }
    return true;
}

bool Settings::save() const {
    if (path_.empty()) return false;
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return false;

    out << "# XpgAnalyzer workspace settings. Safe to edit by hand.\n";
    for (const auto& [k, v] : values_) out << k << " = " << v << '\n';
    dirty_ = false;
    return static_cast<bool>(out);
}

// ------------------------------------------------------------------ getters --
std::string Settings::getString(const std::string& key, std::string fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::move(fallback) : it->second;
}

bool Settings::getBool(const std::string& key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    const auto& v = it->second;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

int Settings::getInt(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    int out = fallback;
    const auto* first = it->second.data();
    std::from_chars(first, first + it->second.size(), out);
    return out;
}

float Settings::getFloat(const std::string& key, float fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    float out = fallback;
    return parseFloat(it->second, out) ? out : fallback;
}

std::vector<float> Settings::getFloats(const std::string& key) const {
    std::vector<float> out;
    std::istringstream in(getString(key));
    std::string token;
    while (std::getline(in, token, ',')) {
        float v = 0.f;
        if (parseFloat(token, v)) out.push_back(v);
    }
    return out;
}

std::vector<std::string> Settings::getList(const std::string& key) const {
    std::vector<std::string> out;
    // Paths may contain commas, so the separator is a pipe: an illegal
    // character in a Windows path and vanishingly rare elsewhere.
    std::istringstream in(getString(key));
    std::string token;
    while (std::getline(in, token, '|'))
        if (!token.empty()) out.push_back(trim(token));
    return out;
}

// ------------------------------------------------------------------ setters --
void Settings::set(const std::string& key, bool value)  { values_[key] = value ? "true" : "false"; dirty_ = true; }
void Settings::set(const std::string& key, int value)   { values_[key] = std::to_string(value); dirty_ = true; }
void Settings::set(const std::string& key, std::string value) { values_[key] = std::move(value); dirty_ = true; }

void Settings::set(const std::string& key, float value) {
    values_[key] = formatFloat(value);
    dirty_ = true;
}

void Settings::setFloats(const std::string& key, const std::vector<float>& values) {
    std::string joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) joined += ',';
        joined += formatFloat(values[i]);
    }
    values_[key] = std::move(joined);
    dirty_ = true;
}

void Settings::setList(const std::string& key, const std::vector<std::string>& values) {
    std::string joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) joined += '|';
        joined += values[i];
    }
    values_[key] = std::move(joined);
    dirty_ = true;
}

// ---- Lot API 8 : oublier (les filtres retenus : "Tout effacer") ----
void Settings::remove(const std::string& key) {
    if (values_.erase(key) > 0) dirty_ = true;
}

std::size_t Settings::removePrefix(const std::string& prefix) {
    std::size_t n = 0;
    // La map est triee : les cles de ce prefixe se suivent.
    for (auto it = values_.lower_bound(prefix); it != values_.end() && it->first.compare(0, prefix.size(), prefix) == 0;) {
        it = values_.erase(it);
        ++n;
    }
    if (n) dirty_ = true;
    return n;
}

std::vector<std::string> Settings::keysWithPrefix(const std::string& prefix) const {
    std::vector<std::string> out;
    for (auto it = values_.lower_bound(prefix); it != values_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        out.push_back(it->first);
    return out;
}
// ---- fin Lot API 8 ----

} // namespace app
