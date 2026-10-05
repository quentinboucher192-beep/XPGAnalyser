// =============================================================================
//  project/MacroMemory.cpp - reponses, profils, recents, favorites
// =============================================================================
#include "MacroMemory.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace project::macro {

namespace {

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool same(std::string_view a, std::string_view b) { return lowered(a) == lowered(b); }

std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
            continue;
        }
        out += s[i];
    }
    return out;
}

std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= line.size()) {
        const auto tab = line.find('\t', from);
        out.push_back(unescape(line.substr(from, (tab == std::string_view::npos ? line.size() : tab) - from)));
        if (tab == std::string_view::npos) break;
        from = tab + 1;
    }
    return out;
}

// Deux chemins de fichier sont le meme sous Windows sans la casse, et avec \ ou /.
std::string pathKey(std::string_view p) {
    std::string out = lowered(p);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

} // namespace

bool MacroMemory::load() {
    answers_.clear();
    profiles_.clear();
    files_.clear();
    favourites_.clear();
    runs_.clear();
    if (path_.empty()) return true;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return true;
    std::ostringstream ss;
    ss << in.rdbuf();
    parse(ss.str());
    return true;
}

void MacroMemory::parse(std::string_view text) {
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        auto line = text.substr(from, (nl == std::string_view::npos ? text.size() : nl) - from);
        from = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto f = fields(line);
        if (f.empty()) continue;
        if (f[0] == "reponse" && f.size() >= 5) answers_.push_back({f[1], f[2], f[3], f[4]});
        else if (f[0] == "profil" && f.size() >= 5) profiles_.push_back({f[2], f[1], f[3], f[4]});
        else if (f[0] == "fichier" && f.size() >= 2) files_.push_back(f[1]);
        else if (f[0] == "favori" && f.size() >= 2) favourites_.push_back(f[1]);
        else if (f[0] == "lancee" && f.size() >= 3) runs_.push_back({f[1], f[2], f.size() >= 4 ? f[3] : std::string{}});
    }
}

std::string MacroMemory::render() const {
    std::ostringstream out;
    out << "# Ce que l'onglet Macros retient : reponses, profils, fichiers recents, favorites.\n"
           "# Une ligne par fait, les champs separes par des tabulations.\n";
    for (const auto& f : files_) out << "fichier\t" << escape(f) << '\n';
    for (const auto& f : favourites_) out << "favori\t" << escape(f) << '\n';
    for (const auto& r : runs_) out << "lancee\t" << escape(r.macro) << '\t' << escape(r.when) << '\t' << escape(r.summary) << '\n';
    for (const auto& a : answers_)
        out << "reponse\t" << escape(a.owner) << '\t' << escape(a.macro) << '\t' << escape(a.key) << '\t' << escape(a.value) << '\n';
    for (const auto& p : profiles_)
        out << "profil\t" << escape(p.macro) << '\t' << escape(p.owner) << '\t' << escape(p.key) << '\t' << escape(p.value) << '\n';
    return out.str();
}

bool MacroMemory::save() const {
    if (path_.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << render();
    return static_cast<bool>(out);
}

MacroMemory::Answers MacroMemory::answers(std::string_view project, std::string_view macro) const {
    Answers out;
    for (const auto& a : answers_)
        if (a.owner == project && same(a.macro, macro)) out[a.key] = a.value;
    return out;
}

void MacroMemory::remember(std::string_view project, std::string_view macro, const Answers& answers) {
    std::erase_if(answers_, [&](const Stored& a) { return a.owner == project && same(a.macro, macro); });
    for (const auto& [k, v] : answers) answers_.push_back({std::string(project), std::string(macro), k, v});
}

std::vector<std::string> MacroMemory::profiles(std::string_view macro) const {
    std::vector<std::string> out;
    for (const auto& p : profiles_)
        if (same(p.macro, macro) && std::find(out.begin(), out.end(), p.owner) == out.end()) out.push_back(p.owner);
    return out;
}

MacroMemory::Answers MacroMemory::profile(std::string_view macro, std::string_view name) const {
    Answers out;
    for (const auto& p : profiles_)
        if (same(p.macro, macro) && p.owner == name) out[p.key] = p.value;
    return out;
}

void MacroMemory::saveProfile(std::string_view macro, std::string_view name, const Answers& answers) {
    removeProfile(macro, name);
    for (const auto& [k, v] : answers) profiles_.push_back({std::string(name), std::string(macro), k, v});
}

void MacroMemory::removeProfile(std::string_view macro, std::string_view name) {
    std::erase_if(profiles_, [&](const Stored& p) { return same(p.macro, macro) && p.owner == name; });
}

void MacroMemory::touchFile(std::string_view path) {
    if (path.empty()) return;
    const auto key = pathKey(path);
    std::erase_if(files_, [&](const std::string& f) { return pathKey(f) == key; });
    files_.insert(files_.begin(), std::string(path));
    if (files_.size() > 8) files_.resize(8);
}

bool MacroMemory::favourite(std::string_view macro) const {
    return std::any_of(favourites_.begin(), favourites_.end(), [&](const std::string& f) { return same(f, macro); });
}

void MacroMemory::setFavourite(std::string_view macro, bool on) {
    std::erase_if(favourites_, [&](const std::string& f) { return same(f, macro); });
    if (on) favourites_.push_back(std::string(macro));
}

void MacroMemory::noteRun(std::string_view macro, std::string_view when, std::string_view summary) {
    runs_.insert(runs_.begin(), Run{std::string(macro), std::string(when), std::string(summary)});
    if (runs_.size() > 20) runs_.resize(20);
}

const MacroMemory::Run* MacroMemory::lastRun(std::string_view macro) const {
    for (const auto& r : runs_)
        if (same(r.macro, macro)) return &r;
    return nullptr;
}

void MacroMemory::renameMacro(std::string_view from, std::string_view to) {
    for (auto& a : answers_)
        if (same(a.macro, from)) a.macro = std::string(to);
    for (auto& p : profiles_)
        if (same(p.macro, from)) p.macro = std::string(to);
    for (auto& f : favourites_)
        if (same(f, from)) f = std::string(to);
    for (auto& r : runs_)
        if (same(r.macro, from)) r.macro = std::string(to);
}

} // namespace project::macro
