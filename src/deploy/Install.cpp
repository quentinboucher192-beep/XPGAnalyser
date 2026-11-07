// =============================================================================
//  deploy/Install.cpp - 1.29.0 : les applications installees (voir Install.hpp)
// =============================================================================
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "Install.hpp"

#include "Package.hpp"
#include "Paths.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace deploy::install {

namespace fs = std::filesystem;

namespace {

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

VersionInfo versionFrom(const core::json::Value& v) {
    VersionInfo i;
    i.id = v.str("id");
    i.label = v.str("label");
    i.sha256 = v.str("sha256");
    i.installedAt = v.integer("installedAt");
    i.from = v.str("from");
    i.publisher = v.str("publisher");
    i.launcher = v.str("launcher");
    i.files = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("files")));
    i.bytes = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("bytes")));
    return i;
}

} // namespace

bool validName(std::string_view name, std::string* why) {
    if (name.empty()) return fail(why, "le nom est vide");
    if (name.size() > 100) return fail(why, "le nom d\xC3\xA9passe 100 caract\xC3\xA8res");
    if (name.find('/') != std::string_view::npos) return fail(why, "le nom contient /");
    std::string w;
    if (!safePackagePath("app/" + std::string(name), &w)) return fail(why, w);
    return true;
}

bool atomicWrite(const std::string& path, std::string_view text, std::string* why) {
    const fs::path target = P(path);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::path tmp = target;
    tmp += u8".ecriture";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail(why, "impossible d'\xC3\xA9" "crire " + U(tmp));
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) return fail(why, "\xC3\xA9" "criture incompl\xC3\xA8te : " + U(tmp) + " (disque plein ?)");
    }
#if defined(_WIN32)
    // Une seule operation : MoveFileEx remplace la cible.
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(tmp, ec);
        return fail(why, "impossible de remplacer " + path + " (erreur " + std::to_string(GetLastError()) + ")");
    }
#else
    fs::rename(tmp, target, ec);   // rename(2) remplace la cible d'un coup
    if (ec) {
        fs::remove(tmp, ec);
        return fail(why, "impossible de remplacer " + path + " (" + ec.message() + ")");
    }
#endif
    return true;
}

bool readJson(const std::string& path, core::json::Value& out, std::string* why) {
    std::ifstream in(P(path), std::ios::binary);
    if (!in) return fail(why, "impossible de lire " + path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err;
    if (!core::json::parse(text, out, &err) || !out.isObject()) return fail(why, path + " illisible (" + err + ")");
    return true;
}

const VersionInfo* AppInfo::find(const std::string& id) const {
    for (const auto& v : versions)
        if (v.id == id) return &v;
    return nullptr;
}

// ================================================================ AppStore ===
AppStore::AppStore(std::string agentDir) : dir_(std::move(agentDir)) {}

std::string AppStore::root() const { return joinPath(dir_, "Applications"); }
std::string AppStore::appDir(const std::string& app) const { return joinPath(root(), app); }
std::string AppStore::versionsDir(const std::string& app) const { return joinPath(appDir(app), "versions"); }
std::string AppStore::versionDir(const std::string& app, const std::string& id) const { return joinPath(versionsDir(app), id); }

bool AppStore::load(const std::string& app, AppInfo& out) const {
    if (!validName(app, nullptr)) return false;
    std::error_code ec;
    if (!fs::is_directory(P(appDir(app)), ec)) return false;
    AppInfo a;
    a.name = app;
    core::json::Value act;
    if (readJson(joinPath(appDir(app), "active.json"), act, nullptr)) {
        a.active = act.str("active");
        a.previous = act.str("previous");
        a.autostart = act.flag("autostart");
        a.activatedAt = act.integer("activatedAt");
    }
    // Une version : un dossier ET sa fiche (un dossier sans fiche est une installation coupee).
    for (fs::directory_iterator it(P(versionsDir(app)), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".json") continue;
        core::json::Value v;
        if (!readJson(U(p), v, nullptr)) continue;
        VersionInfo info = versionFrom(v);
        std::error_code e2;
        if (info.id.empty() || U(p.stem()) != info.id || !fs::is_directory(P(versionDir(app, info.id)), e2)) continue;
        a.versions.push_back(std::move(info));
    }
    std::sort(a.versions.begin(), a.versions.end(), [](const VersionInfo& x, const VersionInfo& y) {
        return x.installedAt != y.installedAt ? x.installedAt < y.installedAt : x.id < y.id;
    });
    out = std::move(a);
    return true;
}

std::vector<AppInfo> AppStore::list() const {
    std::vector<AppInfo> out;
    std::error_code ec;
    for (fs::directory_iterator it(P(root()), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        AppInfo a;
        if (load(U(it->path().filename()), a)) out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const AppInfo& x, const AppInfo& y) { return x.name < y.name; });
    return out;
}

std::string AppStore::freeId(const std::string& app, const std::string& label) const {
    std::error_code ec;
    for (int n = 1; n < 1000; ++n) {
        const std::string id = n == 1 ? label : label + "-" + std::to_string(n);
        if (!fs::exists(P(versionDir(app, id)), ec) && !fs::exists(P(versionDir(app, id) + ".json"), ec)) return id;
    }
    return {};
}

bool AppStore::writeVersion(const std::string& app, const VersionInfo& v, std::string* why) {
    core::json::Value j = core::json::Value::object();
    j.set("format", "xpg-application-version");
    j.set("id", v.id);
    j.set("label", v.label);
    j.set("sha256", v.sha256);
    j.set("installedAt", v.installedAt);
    j.set("from", v.from);
    j.set("publisher", v.publisher);
    j.set("launcher", v.launcher);
    j.set("files", v.files);
    j.set("bytes", v.bytes);
    return atomicWrite(versionDir(app, v.id) + ".json", core::json::write(j, true) + "\n", why);
}

bool AppStore::setActive(const std::string& app, const std::string& id, const std::string& previous, bool autostart, std::string* why) {
    core::json::Value j = core::json::Value::object();
    j.set("format", "xpg-application-active");
    j.set("version", 1);
    j.set("application", app);
    j.set("active", id);
    j.set("previous", previous);
    j.set("activatedAt", static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::system_clock::now().time_since_epoch())
                                                       .count()));
    j.set("autostart", autostart);
    return atomicWrite(joinPath(appDir(app), "active.json"), core::json::write(j, true) + "\n", why);
}

std::string AppStore::olderThanActive(const AppInfo& app) const {
    const VersionInfo* active = app.find(app.active);
    if (!active) return app.versions.empty() ? std::string() : app.versions.back().id;
    std::string best;
    for (const auto& v : app.versions) {
        if (v.id == active->id) break;   // triees : les plus anciennes d'abord
        best = v.id;
    }
    return best;
}

std::vector<std::string> AppStore::prune(const std::string& app, int keep, const std::vector<std::string>& protect) {
    std::vector<std::string> removed;
    AppInfo a;
    if (!load(app, a)) return removed;
    keep = std::max(1, keep);
    std::set<std::string> kept(protect.begin(), protect.end());
    if (!a.active.empty()) kept.insert(a.active);
    // Les plus recentes d'abord, jusqu'a `keep` (les protegees comptent).
    std::size_t count = 0;
    for (const auto& id : kept) count += a.find(id) ? 1 : 0;
    for (auto it = a.versions.rbegin(); it != a.versions.rend() && count < static_cast<std::size_t>(keep); ++it)
        if (kept.insert(it->id).second) ++count;
    for (const auto& v : a.versions) {
        if (kept.count(v.id)) continue;
        std::error_code ec;
        fs::remove(P(versionDir(app, v.id) + ".json"), ec);   // la fiche d'abord : un dossier sans fiche n'est plus une version
        fs::remove_all(P(versionDir(app, v.id)), ec);
        removed.push_back(v.id);
    }
    return removed;
}

// ================================================================= History ===
History::History(std::string agentDir) : dir_(std::move(agentDir)) {}

std::string History::path() const { return joinPath(dir_, "historique.jsonl"); }

// 1.32.0 : l'historique est borne. Au-dela de kHistoryMaxBytes, historique.jsonl devient historique-1.jsonl
// (l'ancien -1 est efface) et un nouveau commence : deux fichiers au plus, 2 x 1 Mio.
void History::append(const core::json::Value& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    const auto size = fs::file_size(P(path()), ec);
    if (!ec && size >= kHistoryMaxBytes) {
        const std::string older = joinPath(dir_, "historique-1.jsonl");
        fs::remove(P(older), ec);
        fs::rename(P(path()), P(older), ec);
    }
    std::ofstream out(P(path()), std::ios::binary | std::ios::app);
    out << core::json::write(entry, false) << "\n";
}

std::vector<core::json::Value> History::tail(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<core::json::Value> all;
    std::string line;
    for (const std::string& file : {joinPath(dir_, "historique-1.jsonl"), path()}) {
        std::ifstream in(P(file), std::ios::binary);
        while (std::getline(in, line)) {
            core::json::Value v;
            if (!line.empty() && core::json::parse(line, v, nullptr) && v.isObject()) all.push_back(std::move(v));
        }
    }
    if (all.size() > count) all.erase(all.begin(), all.end() - static_cast<std::ptrdiff_t>(count));
    return all;
}

} // namespace deploy::install
