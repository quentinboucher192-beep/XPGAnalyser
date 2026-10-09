#include "EditionMigration.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace app::edition {
namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path pathOf(std::string_view utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

std::string nowText() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// Le manifeste de la copie : la ligne edition = ... posee (remplacee si elle existait).
bool markEdition(const fs::path& manifest, std::string_view key) {
    std::string text;
    if (!readAll(manifest, text)) return false;
    std::string out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string_view l(line);
        while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
        if (startsWith(l, "edition")) {
            const auto rest = l.substr(7);
            if (!rest.empty() && (rest.front() == ' ' || rest.front() == '=' || rest.front() == '\t')) continue;
        }
        out += line;
        out += '\n';
    }
    out += "edition = " + std::string(key) + "\n";
    std::ofstream o(manifest, std::ios::binary | std::ios::trunc);
    o << out;
    return static_cast<bool>(o);
}

// Ce qu'une application garde d'un dossier de la 1.11 (le premier niveau).
bool belongs(const std::string& top, core::Edition edition) {
    if (top == "project.xpgproj" || top == "project.lock" || top == "donnees" || top == "versions") return true;
    if (top == "ihm") return edition == core::Edition::Ihm;
    return edition == core::Edition::Api;     // config, vars, ddt, dfb, units, sections, tables, exports, src...
}

} // namespace

bool hmiWorthKeeping(const fs::path& folder) {
    std::string index;
    if (!readAll(folder / "ihm" / "ihm.txt", index)) return false;
    int views = 0;
    std::istringstream in(index);
    std::string line;
    while (std::getline(in, line)) {
        if (startsWith(line, "vue ")) ++views;
        else if (startsWith(line, "variable ") || startsWith(line, "script") || startsWith(line, "fonction")
                 || startsWith(line, "alarme ") || startsWith(line, "equipement ") || startsWith(line, "recette "))
            return true;
    }
    if (views >= 2) return true;
    std::error_code ec;
    for (fs::directory_iterator it(folder / "ihm" / "vues", ec), end; !ec && it != end; it.increment(ec)) {
        std::string view;
        if (!readAll(it->path(), view)) continue;
        if (view.find("\nobjet ") != std::string::npos) return true;
    }
    return false;
}

MigrationReport migrateLegacyProjects(const fs::path& legacyRoot, const fs::path& editionRoot, core::Edition edition) {
    MigrationReport r;
    if (edition == core::Edition::Both) return r;
    std::error_code ec;
    if (fs::exists(editionRoot / kMarker, ec)) return r;                 // deja fait
    fs::create_directories(editionRoot, ec);
    const std::string key = edition == core::Edition::Api ? "api" : "ihm";
    std::vector<fs::path> legacy;
    for (fs::directory_iterator it(legacyRoot, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::string name = it->path().filename().string();
        if (name == "api" || name == "ihm") continue;                   // les rangements de la 1.12
        if (!fs::is_regular_file(it->path() / "project.xpgproj", e2)) continue;
        legacy.push_back(it->path());
    }
    std::sort(legacy.begin(), legacy.end());
    r.ran = true;
    for (const auto& from : legacy) {
        const std::string name = utf8(from.filename());
        // Un projet deja marque (un dossier de la 1.12 range a la main au mauvais endroit) : laisse.
        std::string manifest;
        if (readAll(from / "project.xpgproj", manifest) && manifest.find("\nedition =") != std::string::npos) {
            r.skipped.push_back(name + " : d\xC3\xA9j\xC3\xA0 un projet de la 1.12");
            continue;
        }
        if (edition == core::Edition::Ihm && !hmiWorthKeeping(from)) {
            r.skipped.push_back(name + " : son IHM est vide");
            continue;
        }
        const fs::path to = editionRoot / from.filename();
        std::error_code e3;
        if (fs::exists(to, e3)) {
            r.skipped.push_back(name + " : " + key + "\\" + name + " existe d\xC3\xA9j\xC3\xA0");
            continue;
        }
        fs::create_directories(to, e3);
        bool ok = !e3;
        for (fs::directory_iterator it(from, e3), end; ok && !e3 && it != end; it.increment(e3)) {
            const std::string top = it->path().filename().string();
            if (!belongs(top, edition)) continue;
            std::error_code one;
            fs::copy(it->path(), to / it->path().filename(), fs::copy_options::recursive, one);
            if (one) {
                r.problems.push_back(name + " : " + one.message());
                ok = false;
            }
        }
        if (ok && !markEdition(to / "project.xpgproj", key)) {
            r.problems.push_back(name + " : le manifeste de la copie n'a pas pu \xC3\xAAtre \xC3\xA9" "crit");
            ok = false;
        }
        if (!ok) {
            std::error_code gone;
            fs::remove_all(to, gone);                                     // pas de copie a moitie faite
            continue;
        }
        r.copied.emplace_back(utf8(from), utf8(to));
    }
    // Le marqueur : fait, et ce qui l'a ete (pour qui se demande d'ou viennent ces copies).
    std::ofstream o(editionRoot / kMarker, std::ios::binary | std::ios::trunc);
    o << "# XPGAnalyser 1.12 - les projets de la 1.11 recopies ici le " << nowText() << "\n";
    o << "# (les originaux sont restes dans le dossier parent ; ce fichier dit que c'est fait)\n";
    for (const auto& [a, b] : r.copied) o << "copie : " << a << " -> " << b << "\n";
    for (const auto& s : r.skipped) o << "laisse : " << s << "\n";
    for (const auto& s : r.problems) o << "probleme : " << s << "\n";
    return r;
}

void remapRecent(std::vector<std::string>& recent, const MigrationReport& report) {
    for (auto& path : recent)
        for (const auto& [from, to] : report.copied) {
            std::error_code ec;
            if (path == from || fs::weakly_canonical(pathOf(path), ec) == fs::weakly_canonical(pathOf(from), ec)) {
                path = to;
                break;
            }
        }
}

} // namespace app::edition
