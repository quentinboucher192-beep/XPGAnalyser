// =============================================================================
//  app/Corbeille.cpp - 1.25.1 : la corbeille commune (Corbeille.hpp)
// =============================================================================
#include "Corbeille.hpp"

#include "ai/AiPaths.hpp"                 // IA/corbeille (des fonctions en ligne : rien a lier)
#include "../hmi/HmiCommands.hpp"         // 1.26.1 : le projet IHM ouvert (sa corbeille, ses commandes)
#include "../hmi/HmiGuide.hpp"            // guide::fold : sans casse ni accents
#include "../hmi/HmiStore.hpp"            // 1.26.1 : forgetSaved (supprimer pour de bon, sur le disque aussi)
#include "../hmi/HmiTrash.hpp"            // 1.26.1 : la corbeille du projet IHM
#include "../hmi/HmiRecording.hpp"        // le dossier des enregistrements (et des incidents)
#include "../hmi/HmiVersions.hpp"         // versions/corbeille
#include "../project/SharedLibrary.hpp"   // libs/Macros/_corbeille

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <system_error>

namespace app::corbeille {
namespace {

namespace fs = std::filesystem;

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::map<std::string, Source>& registry() {
    static std::map<std::string, Source> r;
    return r;
}
std::uint64_t& revisionRef() {
    static std::uint64_t r = 1;
    return r;
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::uint64_t sizeOf(const fs::path& p) {
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return static_cast<std::uint64_t>(fs::file_size(p, ec));
    std::uint64_t total = 0;
    for (fs::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += static_cast<std::uint64_t>(it->file_size(e2));
    }
    return total;
}

std::int64_t mtimeOf(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::time_point_cast<std::chrono::seconds>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return sys.time_since_epoch().count();
}

// Une valeur texte d'un fichier JSON (le titre d'une conversation, le nom d'une connaissance) ; vide : absente.
std::string jsonText(const fs::path& file, const char* key) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const std::string want = std::string("\"") + key + "\"";
    auto at = text.find(want);
    if (at == std::string::npos) return {};
    at = text.find(':', at + want.size());
    if (at == std::string::npos) return {};
    at = text.find('"', at);
    if (at == std::string::npos) return {};
    std::string out;
    for (std::size_t i = at + 1; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            out += text[i] == 'n' ? ' ' : text[i];
            continue;
        }
        out += text[i];
    }
    return out;
}

std::string stemOf(const std::string& name) {
    const auto dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

// Les elements d'une corbeille core::trash (datees : <date>~<nom>) ; `undated` : aussi les autres (la date du fichier,
// sans purge - la corbeille des enregistrements d'avant la 1.25.1).
struct Raw {
    fs::path    path;
    std::string original;
    std::int64_t at{0};
    bool        dated{true};
};
std::vector<Raw> rawOf(const fs::path& dir, bool undated, bool folders, bool files) {
    std::vector<Raw> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const bool isDir = it->is_directory(e2);
        if ((isDir && !folders) || (!isDir && !files)) continue;
        const std::string name = U(it->path().filename());
        if (name.empty() || name.front() == '.') continue;
        const auto at = core::trash::trashedAt(name);
        if (at < 0 && !undated) continue;
        Raw r;
        r.path = it->path();
        r.original = core::trash::originalName(name);
        r.at = at >= 0 ? at : mtimeOf(it->path());
        r.dated = at >= 0;
        out.push_back(std::move(r));
    }
    return out;
}

// "2026-11-05 17:30" (l'heure du poste, la corbeille des macros) -> secondes.
std::int64_t localStamp(const std::string& text) {
    std::tm tm{};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) < 3) return 0;
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    return t < 0 ? 0 : static_cast<std::int64_t>(t);
}

void changedAll(const std::vector<std::string>& keys) {
    ++revisionRef();
    for (const auto& k : keys) {
        const auto it = registry().find(k);
        if (it != registry().end() && it->second.changed) it->second.changed();
    }
}

bool foldedStartsWord(const std::string& hay, const std::string& word) {
    for (std::size_t at = hay.find(word); at != std::string::npos; at = hay.find(word, at + 1)) {
        const char before = at == 0 ? ' ' : hay[at - 1];
        if (before == ' ' || before == '-' || before == '(' || before == '/' || before == '\\' || before == '_' || before == '.' || before == '\'')
            return true;
    }
    return false;
}

constexpr std::string_view kPending = "en attente";   // app/ai/AiIncident.hpp : kPendingFolder

} // namespace

std::string kindLabel(Kind k, bool plural) {
    switch (k) {
        case Kind::View: return plural ? "Vues" : "Vue";
        case Kind::Resource: return plural ? "Ressources" : "Ressource";
        case Kind::External: return plural ? "Fichiers externes" : "Fichier externe";
        case Kind::Script: return plural ? "Scripts g\xC3\xA9n\xC3\xA9raux" : "Script g\xC3\xA9n\xC3\xA9ral";
        case Kind::Function: return plural ? "Fonctions IHM" : "Fonction IHM";
        case Kind::Variable: return plural ? "Variables IHM" : "Variable IHM";
        case Kind::HmiType: return plural ? "Types IHM" : "Type IHM";
        case Kind::Alarm: return plural ? "Alarmes" : "Alarme";
        case Kind::Recipe: return plural ? "Recettes" : "Recette";
        case Kind::Style: return plural ? "Styles" : "Style";
        case Kind::Recording: return plural ? "Enregistrements" : "Enregistrement";
        case Kind::Incident: return plural ? "Incidents" : "Incident";
        case Kind::Version: return plural ? "Versions" : "Version";
        case Kind::Macro: return plural ? "Macros" : "Macro";
        case Kind::AiConversation: return plural ? "Conversations IA" : "Conversation IA";
        case Kind::AiAttachment: return plural ? "Pi\xC3\xA8" "ces jointes IA" : "Pi\xC3\xA8" "ce jointe IA";
        case Kind::AiKnowledge: return plural ? "Connaissances et souvenirs IA" : "Connaissance ou souvenir IA";
        case Kind::Count: break;
    }
    return {};
}

std::string Entry::originFolder() const {
    if (kind == Kind::Version || !onDisk) return origin;   // versions/ lui-meme ; 1.26.1 : la liste du projet IHM
    const fs::path p = P(origin);
    return p.has_parent_path() ? U(p.parent_path()) : origin;
}

void registerSource(const std::string& key, Source source) {
    registry()[key] = std::move(source);
    ++revisionRef();
}
void unregisterSource(const std::string& key) {
    registry().erase(key);
    ++revisionRef();
}
bool hasSource(const std::string& key) { return registry().count(key) != 0; }
std::vector<std::string> sources() {
    std::vector<std::string> out;
    for (const auto& [k, s] : registry()) out.push_back(k);
    return out;
}

std::vector<Entry> entries() {
    std::vector<Entry> out;
    for (const auto& [key, src] : registry()) {
        if (!src.list) continue;
        for (auto e : src.list()) {
            e.source = key;
            out.push_back(std::move(e));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.deletedAt > b.deletedAt; });
    return out;
}

std::uint64_t revision() {
    // 1.26.1 : et ce que les sources disent d'elles-memes (une suppression dans le projet IHM, sans geste d'ici).
    std::uint64_t r = revisionRef();
    for (const auto& [key, src] : registry())
        if (src.stamp) r = r * 1099511628211ull ^ src.stamp();
    return r;
}
void touch() { ++revisionRef(); }

std::size_t count() {
    static std::uint64_t seen = 0;
    static std::size_t n = 0;
    static auto at = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (const auto rev = revision(); seen != rev || now - at > std::chrono::seconds(2)) {
        seen = rev;
        at = now;
        n = entries().size();
    }
    return n;
}

std::size_t restore(const std::vector<Entry>& items, std::vector<std::pair<std::string, std::string>>* refused,
                    std::vector<Entry>* restored) {
    auto done = std::make_shared<std::vector<Entry>>();
    std::vector<std::string> touched;
    for (const auto& item : items) {
        const auto it = registry().find(item.source);
        std::string why;
        Entry e = item;
        if (it == registry().end() || !it->second.restore) why = "sa corbeille n'est pas ouverte ici";
        else if (it->second.restore(e, &why)) {
            done->push_back(std::move(e));
            if (std::find(touched.begin(), touched.end(), item.source) == touched.end()) touched.push_back(item.source);
            continue;
        }
        if (refused) refused->emplace_back(item.name, why.empty() ? std::string("refus\xC3\xA9") : why);
    }
    if (!done->empty()) {
        const std::size_t n = done->size();
        const auto again = [touched] { changedAll(touched); };
        auto cmd = std::make_unique<core::trash::LambdaCommand>(
            n > 1 ? "Restaurer " + std::to_string(n) + " \xC3\xA9l\xC3\xA9ments de la corbeille" : "Restaurer " + done->front().name,
            [done]() -> core::Status {   // Ctrl+Y : de nouveau restaures
                std::string said;
                for (auto& e : *done) {
                    const auto it = registry().find(e.source);
                    std::string why;
                    if (it == registry().end() || !it->second.restore || !it->second.restore(e, &why))
                        if (said.empty()) said = e.name + " : " + (why.empty() ? std::string("refus\xC3\xA9") : why);
                }
                if (!said.empty()) return core::fail(core::ErrorCode::FileUnreadable, said);
                return core::ok();
            },
            [done]() -> core::Status {   // Ctrl+Z : de nouveau dans la corbeille
                std::string said;
                // 1.26.1 : a rebours (le dernier rendu repart le premier) - le projet IHM rejoue ses commandes en ordre.
                for (auto back = done->rbegin(); back != done->rend(); ++back) {
                    auto& e = *back;
                    const auto it = registry().find(e.source);
                    std::string why;
                    if (it == registry().end() || !it->second.retrash || !it->second.retrash(e, &why))
                        if (said.empty()) said = e.name + " : " + (why.empty() ? std::string("refus\xC3\xA9") : why);
                }
                if (!said.empty()) return core::fail(core::ErrorCode::FileUnreadable, said);
                return core::ok();
            },
            again, true);
        (void)core::trash::apply(std::move(cmd));
    }
    changedAll(touched);
    if (restored) *restored = *done;
    return done->size();
}

std::size_t purge(const std::vector<Entry>& items, std::vector<std::pair<std::string, std::string>>* refused) {
    std::size_t n = 0;
    std::vector<std::string> touched;
    for (const auto& e : items) {
        const auto it = registry().find(e.source);
        std::string why;
        if (it != registry().end() && it->second.purge && it->second.purge(e, &why)) {
            ++n;
            if (std::find(touched.begin(), touched.end(), e.source) == touched.end()) touched.push_back(e.source);
        } else if (refused) {
            refused->emplace_back(e.name, why.empty() ? std::string("sa corbeille n'est pas ouverte ici") : why);
        }
    }
    changedAll(touched);
    return n;
}

std::size_t purgeOld(const std::string& sourceKey, int days, std::int64_t now) {
    if (now <= 0) now = nowSeconds();
    const std::int64_t limit = now - static_cast<std::int64_t>(days) * 86400;
    std::vector<Entry> old;
    for (const auto& e : entries())
        if ((sourceKey.empty() || e.source == sourceKey) && e.autoPurge && e.deletedAt > 0 && e.deletedAt < limit) old.push_back(e);
    return old.empty() ? 0 : purge(old);
}

bool matches(const Entry& e, std::string_view query) {
    const std::string hay = " " + hmi::guide::fold(e.name + " " + kindLabel(e.kind) + " " + kindLabel(e.kind, true) + " " + e.origin) + " ";
    const std::string q = hmi::guide::fold(query);
    std::string word;
    for (std::size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') {
            if (!word.empty() && !foldedStartsWord(hay, word)) return false;
            word.clear();
        } else {
            word += q[i];
        }
    }
    return true;
}

std::string dateText(std::int64_t seconds) {
    if (seconds <= 0) return "date inconnue";
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return buf;
}

std::string sizeText(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " octets";
    const char* units[] = {"Ko", "Mo", "Go"};
    double v = static_cast<double>(bytes) / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 2) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    for (char* c = buf; *c; ++c)
        if (*c == '.') *c = ',';
    return buf;
}

std::string summaryText(std::size_t n, std::uint64_t bytes) {
    if (n == 0) return "La corbeille est vide";
    return std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments, " : " \xC3\xA9l\xC3\xA9ment, ") + sizeText(bytes);
}

// ================================================================ les sources ==
Source projectSource(std::string projectFolder, std::function<std::string()> recordingsFolder) {
    const std::string folder = std::move(projectFolder);
    const auto recFolder = [folder, recordingsFolder] {
        std::string r = recordingsFolder ? recordingsFolder() : std::string{};
        return r.empty() ? U(P(folder) / P(std::string(hmi::rec::kNativeFolder))) : r;
    };
    const std::string recBin = U(P(folder) / "ihm" / "corbeille");
    const std::string pending = U(P(folder) / P(std::string(hmi::rec::kNativeFolder)) / P(std::string(kPending)));
    const std::string incBin = U(P(pending) / ".corbeille");
    Source s;
    s.list = [folder, recFolder, recBin, pending, incBin] {
        std::vector<Entry> out;
        if (folder.empty()) return out;
        // Les enregistrements (ceux d'avant la 1.25.1, sans date : la date du fichier, sans purge). Les seuls .xrec :
        // l'enregistrement de l'IHM range aussi la ce que son index ne cite plus (une vue, un script) - ni un
        // enregistrement, ni rien qu'on puisse rendre a sa place (le projet ne le cite plus).
        const std::string rf = recFolder();
        const auto isRecording = [](const std::string& name) {
            const std::string_view ext = hmi::rec::kExtension;
            if (name.size() <= ext.size()) return false;
            for (std::size_t i = 0; i < ext.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(name[name.size() - ext.size() + i])) != ext[i]) return false;
            return true;
        };
        for (const auto& r : rawOf(P(recBin), true, false, true)) {
            if (!isRecording(r.original)) continue;
            Entry e;
            e.kind = Kind::Recording;
            e.name = stemOf(r.original);
            e.origin = U(P(rf) / P(r.original));
            e.trashed = U(r.path);
            e.deletedAt = r.at;
            e.bytes = sizeOf(r.path);
            e.autoPurge = r.dated;
            out.push_back(std::move(e));
        }
        // Les incidents (en attente/.corbeille).
        for (const auto& r : rawOf(P(incBin), false, true, false)) {
            Entry e;
            e.kind = Kind::Incident;
            e.name = r.original;
            e.origin = U(P(pending) / P(r.original));
            e.trashed = U(r.path);
            e.deletedAt = r.at;
            e.bytes = sizeOf(r.path);
            out.push_back(std::move(e));
        }
        // Les versions (versions/corbeille : leur ligne de l'index, version.txt).
        for (const auto& r : rawOf(P(hmi::ver::trashFolder(folder)), false, true, false)) {
            Entry e;
            e.kind = Kind::Version;
            const auto t = hmi::ver::readTrashed(U(r.path));
            e.name = t ? t->version.label() : r.original;
            e.handle = t ? std::to_string(t->version.number) : std::string{};
            e.origin = U(P(folder) / "versions");
            e.trashed = U(r.path);
            e.deletedAt = r.at;
            e.bytes = sizeOf(r.path);
            out.push_back(std::move(e));
        }
        return out;
    };
    s.restore = [folder](Entry& e, std::string* why) {
        if (e.kind == Kind::Version) {
            auto store = hmi::ver::open(folder);
            auto t = hmi::ver::readTrashed(e.trashed);
            if (!store || !t) {
                if (why) *why = t ? std::string("pas de versions ici") : t.error().context;
                return false;
            }
            if (auto st = hmi::ver::untrash(*store, *t); !st) {
                if (why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
                return false;
            }
            e.handle = std::to_string(t->version.number);
            e.trashed.clear();
            return true;
        }
        if (!core::trash::takeBack(e.trashed, e.origin, why)) return false;
        e.trashed.clear();
        return true;
    };
    s.retrash = [folder, recBin, incBin](Entry& e, std::string* why) {
        if (e.kind == Kind::Version) {
            auto store = hmi::ver::open(folder);
            auto t = store ? hmi::ver::trash(*store, std::atoi(e.handle.c_str())) : core::Result<hmi::ver::Trashed>(core::fail(core::ErrorCode::FileNotFound));
            if (!t) {
                if (why) *why = "V" + e.handle + " ne repart pas";
                return false;
            }
            e.trashed = t->folder;
            return true;
        }
        std::string at;
        if (!core::trash::put(e.origin, e.kind == Kind::Incident ? incBin : recBin, at, why)) return false;
        e.trashed = at;
        return true;
    };
    s.purge = [](const Entry& e, std::string* why) {
        std::error_code ec;
        fs::remove_all(P(e.trashed), ec);
        if (ec && why) *why = ec.message();
        return !ec;
    };
    return s;
}

Source aiSource() {
    Source s;
    s.list = [] {
        std::vector<Entry> out;
        if (app::ai::root().empty()) return out;
        const struct { const char* sub; Kind kind; } parts[] = {
            {"conversations", Kind::AiConversation}, {"pieces-jointes", Kind::AiAttachment}, {"connaissances", Kind::AiKnowledge}};
        for (const auto& part : parts)
            for (const auto& r : rawOf(P(app::ai::trashDir()) / part.sub, false, true, true)) {
                Entry e;
                e.kind = part.kind;
                std::string shown;
                if (part.kind == Kind::AiConversation) shown = jsonText(r.path, "titre");
                else if (part.kind == Kind::AiKnowledge) shown = jsonText(r.path, "nom");
                else {   // une piece jointe : les fichiers de son dossier
                    std::error_code ec;
                    for (fs::directory_iterator it(r.path, ec), end; !ec && it != end; it.increment(ec))
                        shown += (shown.empty() ? "" : ", ") + U(it->path().filename());
                }
                e.name = shown.empty() ? r.original : shown;
                e.origin = U(P(app::ai::root()) / part.sub / P(r.original));
                e.trashed = U(r.path);
                e.deletedAt = r.at;
                e.bytes = sizeOf(r.path);
                out.push_back(std::move(e));
            }
        return out;
    };
    s.restore = [](Entry& e, std::string* why) {
        if (!core::trash::takeBack(e.trashed, e.origin, why)) return false;
        e.trashed.clear();
        return true;
    };
    s.retrash = [](Entry& e, std::string* why) {
        std::string at;
        if (!core::trash::put(e.origin, app::ai::trashFor(e.origin), at, why)) return false;
        e.trashed = at;
        return true;
    };
    s.purge = [](const Entry& e, std::string* why) {
        std::error_code ec;
        fs::remove_all(P(e.trashed), ec);
        if (ec && why) *why = ec.message();
        return !ec;
    };
    return s;
}

Source macroSource(std::string libraryRoot) {
    const std::string root = std::move(libraryRoot);
    Source s;
    s.list = [root] {
        std::vector<Entry> out;
        if (root.empty()) return out;
        project::SharedLibrary lib(root);
        const fs::path bin = P(lib.macrosFolder()) / "_corbeille";
        for (const auto& t : lib.trashedMacros()) {
            Entry e;
            e.kind = Kind::Macro;
            e.name = t.name;
            e.handle = t.name;
            e.origin = U(P(lib.macrosFolder()) / P(t.name + ".mac"));
            e.trashed = U(bin / P(t.file));
            e.deletedAt = localStamp(t.date);
            e.bytes = sizeOf(bin / P(t.file));
            e.autoPurge = false;   // la corbeille des macros n'a jamais eu de purge : elle les garde
            out.push_back(std::move(e));
        }
        return out;
    };
    const auto status = [](const core::Status& st, std::string* why) {
        if (!st && why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
        return static_cast<bool>(st);
    };
    s.restore = [root, status](Entry& e, std::string* why) {
        project::SharedLibrary lib(root);
        (void)lib.scan();
        if (!status(lib.restoreMacro(e.handle), why)) return false;
        e.trashed.clear();
        return true;
    };
    s.retrash = [root, status](Entry& e, std::string* why) {
        project::SharedLibrary lib(root);
        (void)lib.scan();
        if (!status(lib.deleteMacro(e.handle), why)) return false;
        for (const auto& t : lib.trashedMacros())
            if (t.name == e.handle) {
                e.trashed = U(P(lib.macrosFolder()) / "_corbeille" / P(t.file));
                break;
            }
        return true;
    };
    s.purge = [root, status](const Entry& e, std::string* why) {
        project::SharedLibrary lib(root);
        return status(lib.purgeMacro(e.handle), why);
    };
    return s;
}

// ================================================================ 1.26.1 : le projet IHM ==
namespace {

static_assert(static_cast<int>(Kind::View) == static_cast<int>(hmi::bin::Kind::View)
                  && static_cast<int>(Kind::Style) == static_cast<int>(hmi::bin::Kind::Style),
              "les genres du projet IHM, dans le meme ordre");

Kind kindOfItem(const hmi::TrashItem& t) {
    if (const auto k = hmi::bin::kindOf(t.kind)) return static_cast<Kind>(static_cast<int>(*k));
    return Kind::View;   // un genre d'une version plus recente : avec les vues
}

// Les restaurations faites, par leur jeton (Entry::handle) : Ctrl+Y rejoue la meme commande, Ctrl+Z l'annule - le
// projet revient exactement ou il etait (le nom donne, les identifiants). Gardees ici : le volet reinscrit la source.
std::map<std::string, std::shared_ptr<core::ICommand>>& restoredCommands() {
    static std::map<std::string, std::shared_ptr<core::ICommand>> m;
    return m;
}
std::uint64_t& restoredSerial() {
    static std::uint64_t n = 0;
    return n;
}
// Le document dont la corbeille a ete purgee (une fois, a la premiere lecture : son ouverture).
std::weak_ptr<hmi::Document>& purgedDoc() {
    static std::weak_ptr<hmi::Document> d;
    return d;
}

std::uint64_t mixed(std::uint64_t x) {   // splitmix64 : deux ensembles differents, deux empreintes differentes
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::string statusText(const core::Status& st) {
    if (st) return {};
    return st.error().context.empty() ? st.error().message() : st.error().context;
}

} // namespace

Source hmiSource(std::function<std::shared_ptr<hmi::Document>()> docOf, std::function<std::string()> folderOf) {
    const auto doc = [docOf]() -> std::shared_ptr<hmi::Document> { return docOf ? docOf() : nullptr; };
    const auto folder = [folderOf]() -> std::string { return folderOf ? folderOf() : std::string{}; };
    Source s;
    s.list = [doc, folder] {
        std::vector<Entry> out;
        const auto d = doc();
        if (!d) return out;
        // A l'ouverture du projet : ce qui y est depuis plus de kKeepDays jours part pour de bon (comme le reste).
        if (purgedDoc().lock() != d) {
            purgedDoc() = d;
            for (const auto id : hmi::bin::older(d->project, kKeepDays)) {
                (void)hmi::bin::forget(d->project, id);
                (void)hmi::forgetSaved(folder(), id);
            }
        }
        for (const auto& t : d->project.trash) {
            Entry e;
            e.kind = kindOfItem(t);
            e.name = t.name;
            e.origin = t.origin;
            e.trashed = "ihm:" + std::to_string(t.id);
            e.handle = std::to_string(t.id);
            e.deletedAt = t.deletedAt;
            e.bytes = t.bytes;
            e.onDisk = false;
            out.push_back(std::move(e));
        }
        return out;
    };
    s.restore = [doc](Entry& e, std::string* why) {
        const auto d = doc();
        if (!d) {
            if (why) *why = "le projet IHM n'est pas ouvert";
            return false;
        }
        if (const auto c = restoredCommands().find(e.handle); c != restoredCommands().end()) {   // Ctrl+Y
            if (const auto* pc = dynamic_cast<const hmi::ProjectCommand*>(c->second.get()))
                for (const auto& t : pc->untrashed())
                    if (!hmi::bin::find(d->project, t.id)) {
                        if (why) *why = "supprim\xC3\xA9 d\xC3\xA9" "finitivement entre-temps";
                        return false;
                    }
            if (const auto st = c->second->execute(); !st) {
                if (why) *why = statusText(st);
                return false;
            }
            e.trashed.clear();
            return true;
        }
        const auto id = static_cast<hmi::Id>(std::strtoull(e.handle.c_str(), nullptr, 10));
        std::string shown;
        auto cmd = hmi::restoreFromTrash(d, id, &shown, why);
        if (!cmd) return false;
        if (const auto st = cmd->execute(); !st) {
            if (why) *why = statusText(st);
            return false;
        }
        const std::string token = "r" + std::to_string(++restoredSerial());
        restoredCommands()[token] = std::shared_ptr<core::ICommand>(std::move(cmd));
        e.handle = token;
        if (!shown.empty()) e.name = shown;   // "Vue_Accueil -> Vue_Accueil_2" : un nom deja pris
        e.trashed.clear();
        return true;
    };
    s.retrash = [](Entry& e, std::string* why) {   // Ctrl+Z d'une restauration
        const auto c = restoredCommands().find(e.handle);
        if (c == restoredCommands().end()) {
            if (why) *why = "sa restauration est introuvable";
            return false;
        }
        if (const auto st = c->second->undo(); !st) {
            if (why) *why = statusText(st);
            return false;
        }
        if (const auto* pc = dynamic_cast<const hmi::ProjectCommand*>(c->second.get()); pc && !pc->untrashed().empty())
            e.trashed = "ihm:" + std::to_string(pc->untrashed().front().id);
        return true;
    };
    s.purge = [doc, folder](const Entry& e, std::string* why) {
        const auto d = doc();
        if (!d) {
            if (why) *why = "le projet IHM n'est pas ouvert";
            return false;
        }
        const auto id = static_cast<hmi::Id>(std::strtoull(e.handle.c_str(), nullptr, 10));
        if (!hmi::bin::forget(d->project, id)) {
            if (why) *why = "il n'est plus dans la corbeille";
            return false;
        }
        // Sur le disque aussi, tout de suite : sans retour, meme sans enregistrer.
        if (const auto st = hmi::forgetSaved(folder(), id); !st && why) *why = statusText(st);
        return true;
    };
    s.stamp = [doc]() -> std::uint64_t {
        const auto d = doc();
        if (!d) return 0;
        std::uint64_t h = mixed(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(d.get())) ^ d->project.trash.size());
        for (const auto& t : d->project.trash) h += mixed(t.id);
        return h;
    };
    return s;
}

} // namespace app::corbeille
