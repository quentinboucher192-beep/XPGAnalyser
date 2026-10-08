// =============================================================================
//  app/hmi/HmiConsole.cpp - 1.11.14 : la Console du panneau du bas (ses lignes)
// =============================================================================
#include "HmiConsole.hpp"

#include "../../hmi/HmiRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace app {

namespace {
std::string today() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}
// L'heure du poste, a la milliseconde : "19:12:03.250" (celle des Sorties, sans le decalage
// d'horloge que la simulation peut avoir ; et juste avant le premier demarrage aussi).
std::string clockMillis() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Un champ CSV : entre guillemets s'il le faut (les guillemets doubles).
std::string csv(const std::string& s) {
    if (s.find_first_of(";\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += "\"\"";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out + "\"";
}
std::string padded(std::string s, std::size_t width) {
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}
std::string filterText(const HmiConsole::Filter& f) {
    std::string levels;
    bool all = true;
    for (int k = 0; k < hmi::kLogLevelCount; ++k) {
        if (!f.levels[static_cast<std::size_t>(k)]) { all = false; continue; }
        if (!levels.empty()) levels += ", ";
        levels += std::string(hmi::logLevelName(static_cast<hmi::LogLevel>(k)));
    }
    std::string out = all ? std::string("tous les niveaux") : levels.empty() ? std::string("aucun niveau") : "niveaux " + levels;
    if (!f.search.empty()) out += ", recherche \xC2\xAB " + f.search + " \xC2\xBB";
    return out;
}
} // namespace

std::string ConsoleEntry::where() const {
    std::string out = !source.empty() ? source : code;
    if (line > 0 && !out.empty()) out += ", ligne " + std::to_string(line);
    return out;
}

void HmiConsole::addRuntime(const hmi::JournalEntry& e) {
    ConsoleEntry c;
    c.date = today();
    c.time = clockMillis();
    c.level = e.level;
    c.category = e.kind;
    c.source = e.source;
    c.code = e.code;
    c.message = e.message;
    c.line = e.line;
    c.cycle = e.cycle;
    c.session = e.session;
    c.view = e.view;
    c.object = e.object;
    c.script = e.script;
    c.function = e.function;
    add(std::move(c));
}

void HmiConsole::add(ConsoleEntry e) {
    e.seq = ++seq_;
    if (e.date.empty()) e.date = today();
    if (e.session > session_) session_ = e.session;
    ++counts_[static_cast<std::size_t>(e.level)];
    entries_.push_back(std::move(e));
    drop();
    ++revision_;
}

void HmiConsole::drop() {
    while (entries_.size() > retention_) {
        --counts_[static_cast<std::size_t>(entries_.front().level)];
        entries_.pop_front();
        ++dropped_;
    }
}

void HmiConsole::clear() {
    entries_.clear();
    counts_.fill(0);
    dropped_ = 0;
    ++revision_;
}

void HmiConsole::setRetention(std::size_t lines) {
    retention_ = std::clamp(lines, kMinRetention, kMaxRetention);
    drop();
    ++revision_;
}

std::size_t HmiConsole::nextRetention(std::size_t current) noexcept {
    for (const std::size_t step : {std::size_t{500}, std::size_t{1000}, std::size_t{5000}, std::size_t{20000}, std::size_t{100000}})
        if (step > current) return step;
    return kMinRetention;
}

int HmiConsole::errors() const noexcept { return count(hmi::LogLevel::Error) + count(hmi::LogLevel::Critical); }

bool HmiConsole::matches(const ConsoleEntry& e, const Filter& f) {
    if (!f.levels[static_cast<std::size_t>(e.level)]) return false;
    if (f.search.empty()) return true;
    const std::string q = lower(f.search);
    for (const std::string* field : {&e.message, &e.source, &e.code, &e.category})
        if (lower(*field).find(q) != std::string::npos) return true;
    return false;
}

std::string HmiConsole::lineOf(const ConsoleEntry& e) {
    std::string where = !e.source.empty() ? e.source : e.code;
    if (e.line > 0) where += ":" + std::to_string(e.line);
    return e.time + "  " + padded(std::string(hmi::logLevelName(e.level)), 9) + padded(where, 28) + " " + e.message;
}

std::string HmiConsole::exportText(const Filter& f) const {
    std::string out = "# XPGAnalyser - Console de la simulation IHM (" + today() + ")\n";
    out += "# " + std::to_string(entries_.size()) + " ligne(s) gard\xC3\xA9" "e(s)" + (dropped_ ? ", " + std::to_string(dropped_) + " plus ancienne(s) tomb\xC3\xA9" "e(s)" : std::string{})
         + " ; " + filterText(f) + "\n";
    int session = -1;
    for (const auto& e : entries_) {
        if (!matches(e, f)) continue;
        if (e.session != session) {
            session = e.session;
            out += "# --- session " + std::to_string(session) + " ---\n";
        }
        out += lineOf(e) + "\n";
    }
    return out;
}

std::string HmiConsole::exportCsv(const Filter& f) const {
    std::string out = "Date;Heure;Niveau;Cat\xC3\xA9gorie;Source;Ligne;Session;Cycle;Message\n";
    for (const auto& e : entries_) {
        if (!matches(e, f)) continue;
        out += csv(e.date) + ";" + csv(e.time) + ";" + std::string(hmi::logLevelName(e.level)) + ";" + csv(e.category) + ";"
             + csv(!e.source.empty() ? e.source : e.code) + ";" + (e.line > 0 ? std::to_string(e.line) : std::string{}) + ";"
             + std::to_string(e.session) + ";" + std::to_string(e.cycle) + ";" + csv(e.message) + "\n";
    }
    return out;
}

} // namespace app
