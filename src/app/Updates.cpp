// =============================================================================
//  app/Updates.cpp - 1.44.0 : les messages a ce PC et les mises a jour dans le
//  Studio (voir Updates.hpp ; le contrat : docs/ADMINS.md, 7)
// =============================================================================
#include "Updates.hpp"

#include "App.hpp"
#include "FileDownload.hpp"
#include "Licence.hpp"
#include "Settings.hpp"

#include "../core/Edition.hpp"
#include "../core/Json.hpp"
#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/DevKey.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Licensing.hpp"
#include "../ui/widgets/PathBrowse.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace app::updates {

namespace {

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = licensing::admins;
namespace json = core::json;
namespace crypto = deploy::crypto;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ============================================================ l'etat (le fil de l'interface) ==
struct State {
    std::mutex                mutex;
    std::optional<Offer>      offer;
    std::set<std::string>     announcedHere;    // les versions annoncees par CE lancement
    bool                      chip{false};      // la pastille (<< Plus tard >>, ou annoncee a un lancement d'avant)
    std::optional<Toast>      toast;
    std::string               lastLook;         // "1.44.1", "" (aucune publiee), "?" (pas encore regardee)
    Install                   install;
    std::vector<ls::AnnouncedFile> installing;  // les fichiers telecharges pour l'installation
    // un fichier d'une notification (Installer, Enregistrer..., fichier-telecharger)
    std::string               fetchNotice;
    std::string               fetchFile, fetchName;
    bool                      fetchThenInstall{false}, fetchThenSave{false};
    std::string               lastFetch, lastFetchPath;
    bool                      lastFetchOk{false};
    // le lancement des installateurs (un fil : l'Admin est attendu)
    std::thread               launcher;
    std::atomic<bool>         launchDone{false};
    bool                      launchOk{false};
    std::string               launchWhy;
    std::string               adminFolder;      // les essais
    ~State() {
        if (launcher.joinable()) launcher.join();
    }
};
State& st() {
    static State s;
    return s;
}

std::string productLabel() { return core::productName(); }

// ============================================================ les donnees ==
json::Value fileValue(const NoticeFile& f) {
    json::Value o = json::Value::object();
    o.set("id", f.id);
    o.set("nom", f.name);
    o.set("taille", f.size);
    o.set("sha256", f.sha256);
    if (!f.version.empty()) o.set("version", f.version);
    if (!f.kind.empty()) o.set("sorte", f.kind);
    if (!f.audience.empty()) o.set("pour", f.audience);
    if (!f.token.empty()) o.set("jeton", f.token);
    if (f.manifestOk) o.set("manifeste", true);
    if (f.hashOk) o.set("verifie", true);
    if (!f.refused.empty()) o.set("refuse", f.refused);
    return o;
}

NoticeFile noticeFileOf(const ls::AnnouncedFile& a) {
    NoticeFile f;
    f.id = a.id;
    f.name = a.name;
    f.sha256 = a.sha256;
    f.size = a.size;
    f.version = a.version;
    f.kind = a.kind;
    f.audience = a.audience;
    f.token = a.token;
    f.manifestOk = a.manifestOk;
    f.refused = a.refused;
    return f;
}

filedl::Want wantOf(const NoticeFile& f) {
    filedl::Want w;
    w.id = f.id;
    w.name = f.name;
    w.sha256 = f.sha256;
    w.size = f.size;
    w.version = f.version;
    w.kind = f.kind;
    w.token = f.token;
    return w;
}
filedl::Want wantOf(const ls::AnnouncedFile& a) { return wantOf(noticeFileOf(a)); }

std::string firstLines(const std::string& text, std::size_t max) {
    std::string out;
    for (char c : text) {
        if (out.size() >= max) break;
        out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    if (out.size() >= max) {
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
        out += "\xE2\x80\xA6";
    }
    return out;
}

std::string countFiles(std::size_t n) { return std::to_string(n) + (n > 1 ? " fichiers" : " fichier"); }

void postRefused(const NoticeFile& f) {
    notices::Item n;
    n.category = "Fichier";
    n.title = "Fichier refus\xC3\xA9";
    n.detail = f.name + " : la signature ne correspond pas. Il n'a pas \xC3\xA9t\xC3\xA9 propos\xC3\xA9.";
    n.tone = "error";
    n.key = "fichier-refuse:" + f.id;
    (void)notices::post(std::move(n));
}

// ============================================================ le jeton d'admin ==
std::string adminFolderDefault() {
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    return deploy::joinPath(deploy::joinPath(la && *la ? la : ".", "XPG"), "LicenseAdmin");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return deploy::joinPath(xdg, "xpg-license-admin");
    const char* home = std::getenv("HOME");
    return deploy::joinPath(deploy::joinPath(home && *home ? home : ".", ".config"), "xpg-license-admin");
#endif
}

Offer makeOffer(const ls::UpdateOffer& u) {
    Offer o;
    o.offer = u;
    o.pcName = deploy::DevKey::computerName();
    if (o.admin()) {
        const AdminToken t = readAdminToken();
        if (t.ok) {
            o.adminPc = true;
            o.token = t.token;
            o.adminOutdated = t.token.version.empty() || adm::compareSemver(t.token.version, u.version) < 0;
        }
    }
    return o;
}

NoticeData updateData(const Offer& o) {
    NoticeData d;
    d.kind = "maj";
    d.version = o.offer.version;
    d.issued = o.offer.published;
    d.important = o.offer.important;
    d.notes = o.offer.notes;
    for (const auto& f : o.files()) d.files.push_back(noticeFileOf(f));
    return d;
}

// ============================================================ lancer ==
#ifdef _WIN32
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
#endif

// Lancer un programme (un installateur) ; `wait` : attendre sa fin (et son code). Faux : `why`.
bool run(const std::string& exe, const std::string& args, bool wait, std::string& why) {
#ifdef _WIN32
    const std::wstring file = wide(exe), params = wide(args);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        why = "Windows refuse de le lancer (erreur " + std::to_string(GetLastError()) + ")";
        return false;
    }
    bool ok = true;
    if (wait && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 0;
        if (GetExitCodeProcess(sei.hProcess, &code) && code != 0) {
            why = "il s'est termin\xC3\xA9 avec le code " + std::to_string(code);
            ok = false;
        }
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return ok;
#else
    (void)exe;
    (void)args;
    (void)wait;
    why = "les installateurs ne se lancent que sous Windows";
    return false;
#endif
}

bool canLaunch() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

std::string projectName(App& app) {
    const auto& f = app.projectFolder();
    if (f.empty() || !app.project()) return {};
    if (!app.manifest().name.empty()) return app.manifest().name;
    const auto u8 = P(f).filename().u8string();
    return std::string(u8.begin(), u8.end());
}

bool modified(App& app) { return app.project() && !app.projectFolder().empty() && app.commands().isModified(); }

} // namespace

// ============================================================ les donnees (public) ==
std::optional<NoticeData> dataOf(std::string_view text) {
    json::Value v;
    if (text.empty() || !json::parse(text, v, nullptr) || !v.isObject()) return std::nullopt;
    const std::string kind = v.str("genre");
    if (kind != "message" && kind != "maj") return std::nullopt;
    NoticeData d;
    d.kind = kind;
    d.from = v.str("de");
    d.text = v.str("texte");
    d.action = v.str("action");
    d.issued = static_cast<lic::Seconds>(v.integer("emis"));
    d.version = v.str("version");
    d.important = v.flag("importante");
    if (const auto* n = v.find("notes"); n && n->isArray())
        for (const auto& x : n->items())
            if (x.isString()) d.notes.push_back(x.asString());
    if (const auto* fs = v.find("fichiers"); fs && fs->isArray())
        for (const auto& x : fs->items()) {
            if (!x.isObject()) continue;
            NoticeFile f;
            f.id = x.str("id");
            f.name = x.str("nom");
            f.size = static_cast<std::uint64_t>(std::max<std::int64_t>(0, x.integer("taille")));
            f.sha256 = x.str("sha256");
            f.version = x.str("version");
            f.kind = x.str("sorte");
            f.audience = x.str("pour");
            f.token = x.str("jeton");
            f.manifestOk = x.flag("manifeste");
            f.hashOk = x.flag("verifie");
            f.refused = x.str("refuse");
            d.files.push_back(std::move(f));
        }
    return d;
}

std::string writeData(const NoticeData& d) {
    json::Value v = json::Value::object();
    v.set("genre", d.kind);
    if (!d.from.empty()) v.set("de", d.from);
    if (!d.text.empty()) v.set("texte", d.text);
    if (!d.action.empty()) v.set("action", d.action);
    if (d.issued) v.set("emis", static_cast<std::int64_t>(d.issued));
    if (!d.version.empty()) v.set("version", d.version);
    if (d.important) v.set("importante", true);
    if (!d.notes.empty()) {
        json::Value n = json::Value::array();
        for (const auto& x : d.notes) n.push(x);
        v.set("notes", std::move(n));
    }
    json::Value fs = json::Value::array();
    for (const auto& f : d.files) fs.push(fileValue(f));
    v.set("fichiers", std::move(fs));
    return json::write(v, false);
}

bool hasFiles(const notices::Item& n) {
    if (n.data.find("\"fichiers\"") == std::string::npos) return false;
    const auto d = dataOf(n.data);
    return d && !d->files.empty();
}

std::string filesLine(const notices::Item& n) {
    if (n.data.find("\"fichiers\"") == std::string::npos) return {};
    const auto d = dataOf(n.data);
    if (!d || d->files.empty()) return {};
    std::uint64_t total = 0;
    std::size_t refused = 0;
    for (const auto& f : d->files) {
        total += f.size;
        refused += f.refused.empty() ? 0 : 1;
    }
    std::string s = countFiles(d->files.size()) + " \xC2\xB7 " + filedl::sizeText(total);
    if (refused) s += " \xC2\xB7 " + std::to_string(refused) + (refused > 1 ? " refus\xC3\xA9s" : " refus\xC3\xA9");
    return s;
}

std::string toMessage(const std::string& key) { return "fichiers:" + key; }
std::string toUpdate(const std::string& version) { return "maj:" + version; }

std::optional<notices::Item> notice(const std::string& key) {
    if (key.empty()) return std::nullopt;
    for (auto& n : notices::list())
        if (n.key == key) return n;
    return std::nullopt;
}

void markFile(const std::string& noticeKey, const std::string& fileId, bool ok, const std::string& refused) {
    const auto n = notice(noticeKey);
    if (!n) return;
    auto d = dataOf(n->data);
    if (!d) return;
    for (auto& f : d->files) {
        if (f.id != fileId) continue;
        if (ok) {
            f.hashOk = true;
            f.manifestOk = true;
            f.refused.clear();
        } else if (!refused.empty()) {
            f.hashOk = false;
            f.refused = refused;
            postRefused(f);
        }
    }
    (void)notices::setData(n->id, writeData(*d));
}

// ============================================================ recu du fil du service ==
void received(const std::vector<ls::InboxMessage>& messages, const std::optional<ls::UpdateOffer>& update, bool versionLooked) {
    State& s = st();
    // personne devant l'ecran (le poste d'exploitation qui tourne) : ni notification, ni toast, ni pastille
    if (!licence::noticesShown()) return;
    for (const auto& m : messages) {
        NoticeData d;
        d.kind = "message";
        d.from = m.from;
        d.text = m.text;
        d.action = m.action;
        d.issued = m.issued;
        for (const auto& f : m.files) d.files.push_back(noticeFileOf(f));
        notices::Item n;
        n.category = m.action == "admin" ? "Admin" : "Message";
        n.title = m.title.empty() ? std::string("Message de l'\xC3\xA9" "diteur") : m.title;
        n.detail = m.text.empty() ? (m.files.empty() ? std::string() : countFiles(m.files.size()) + " joint" + (m.files.size() > 1 ? "s" : ""))
                                  : firstLines(m.text, 240);
        n.tone = "info";
        n.key = "message:" + std::to_string(m.number) + ":" + std::to_string(m.issued);
        n.button = d.files.empty() ? "Voir le message" : "Voir le message et ses fichiers";
        n.action = toMessage(n.key);   // sa fenetre (planche 05)
        n.data = writeData(d);
        (void)notices::post(std::move(n));
        for (const auto& f : d.files)
            if (!f.refused.empty()) postRefused(f);
    }
    if (update) {
        Offer o = makeOffer(*update);
        std::lock_guard<std::mutex> lock(s.mutex);
        s.lastLook = update->version;
        const bool here = s.announcedHere.count(update->version) > 0;
        if (update->announce) {
            s.announcedHere.insert(update->version);
            s.chip = false;
            NoticeData d = updateData(o);
            notices::Item n;
            n.category = "Mise \xC3\xA0 jour";
            n.title = "Mise \xC3\xA0 jour " + update->version + " disponible";
            std::string detail;
            if (o.adminPc && o.adminOutdated) detail = adminLine(o) + ". ";
            for (std::size_t i = 0; i < update->notes.size() && i < 2; ++i) detail += (i ? " \xC2\xB7 " : "") + update->notes[i];
            if (detail.empty())
                if (const auto* f = o.studios()) detail = filedl::sizeText(f->size) + " \xC2\xB7 sign\xC3\xA9" "e par l'\xC3\xA9" "diteur";
            n.detail = detail;
            n.button = o.adminPc ? "Tout installer\xE2\x80\xA6" : "Voir la mise \xC3\xA0 jour";
            n.action = toUpdate(update->version);
            n.tone = update->important ? "warning" : "info";
            n.key = "maj:" + update->version;
            n.data = writeData(d);
            // << importante >> : a chaque lancement, de nouveau non lue (le meme evenement garderait son etat lu)
            if (update->important)
                for (const auto& old : notices::list())
                    if (old.key == n.key) (void)notices::remove(old.id);
            (void)notices::post(std::move(n));
            for (const auto& f : d.files)
                if (!f.refused.empty()) postRefused(f);
            Toast t;
            t.title = "Mise \xC3\xA0 jour " + update->version + " disponible";
            std::string sub = o.adminPc ? std::string("Studios et Admin") : std::string("Studios");
            if (const auto* f = o.studios()) sub += " \xC2\xB7 " + filedl::sizeText(f->size);
            sub += " \xC2\xB7 sign\xC3\xA9" "e par l'\xC3\xA9" "diteur \xE2\x9C\x93";
            t.text = sub;
            t.shownAt = steadyNow();
            s.toast = t;
        } else if (!here) {
            s.chip = true;   // annoncee a un lancement d'avant : la pastille seulement
        }
        s.offer = std::move(o);
    } else if (versionLooked) {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.install.phase == Phase::Idle || s.install.phase == Phase::Done || s.install.phase == Phase::Failed ||
            s.install.phase == Phase::Cancelled) {
            s.offer.reset();
            s.chip = false;
        }
        s.lastLook = "";
    }
}

// ============================================================ le jeton d'admin ==
std::string adminFolder() {
    {
        std::lock_guard<std::mutex> lock(st().mutex);
        if (!st().adminFolder.empty()) return st().adminFolder;
    }
    return adminFolderDefault();
}

void setAdminFolderForTests(std::string folder) {
    std::lock_guard<std::mutex> lock(st().mutex);
    st().adminFolder = std::move(folder);
}

AdminToken checkAdminToken(std::string_view text, lic::Seconds now) {
    AdminToken r;
    std::vector<lic::SignedBlock> blocks;
    std::string w;
    if (!lic::readBlocks(text, blocks, &w) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::AdminToken) {
        r.why = "le jeton d'admin est illisible";
        return r;
    }
    adm::AdminToken t;
    if (!adm::parseAdminToken(blocks[0].document, t, &w)) {
        r.why = w;
        return r;
    }
    crypto::Bytes raw;
    crypto::PublicKey pk{};
    if (!crypto::unhex(t.key, raw, pk.size())) {
        r.why = "le jeton d'admin n'a pas de cl\xC3\xA9 lisible";
        return r;
    }
    std::copy(raw.begin(), raw.end(), pk.begin());
    if (lic::keyIdOf(pk) != blocks[0].keyId) {
        r.why = "le jeton d'admin n'est pas sign\xC3\xA9 par sa cl\xC3\xA9";
        return r;
    }
    // la cle de l'editeur, ou un admin ACTIF de la liste en vigueur
    bool trusted = false;
    for (const auto& k : lic::trustedPublisherKeys())
        if (k.key == pk) trusted = true;
    if (!trusted)
        if (const auto list = lic::getAdminList())
            if (const auto* a = list->byKey(t.key); a && a->active()) trusted = true;
    if (!trusted) {
        r.why = "le jeton d'admin n'est pas celui d'un admin de la liste";
        return r;
    }
    if (!lic::verifyBlock(blocks[0], pk)) {
        r.why = "la signature du jeton d'admin est fausse";
        return r;
    }
    if (t.expires <= now) {
        r.why = "le jeton d'admin a expir\xC3\xA9";
        return r;
    }
    r.ok = true;
    r.token = std::move(t);
    r.base64 = crypto::base64(text);
    return r;
}

AdminToken readAdminToken() {
    const std::string path = deploy::joinPath(adminFolder(), kAdminTokenFile);
    std::error_code ec;
    if (!fs::exists(P(path), ec)) return {};
    crypto::Bytes bytes;
    std::string w;
    AdminToken r;
    if (!crypto::readFile(path, bytes, &w, 64u * 1024u)) {
        r.why = "le jeton d'admin ne se lit pas : " + w;
        return r;
    }
    return checkAdminToken(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), lic::systemNow());
}

// ============================================================ la mise a jour disponible ==
const ls::AnnouncedFile* Offer::studios() const {
    for (const auto& f : offer.files)
        if (f.kind == "studios") return &f;
    return nullptr;
}
const ls::AnnouncedFile* Offer::admin() const {
    for (const auto& f : offer.files)
        if (f.kind == "admin") return &f;
    return nullptr;
}
std::vector<ls::AnnouncedFile> Offer::files() const {
    std::vector<ls::AnnouncedFile> out;
    if (const auto* s = studios()) out.push_back(*s);
    if (adminPc)
        if (const auto* a = admin()) out.push_back(*a);
    return out;
}

std::optional<Offer> available() {
    std::lock_guard<std::mutex> lock(st().mutex);
    return st().offer;
}

std::string adminLine(const Offer& o) {
    if (!o.adminPc || !o.adminOutdated) return {};
    std::string who = o.token.name;
    if (!o.pcName.empty()) who += (who.empty() ? "" : ", ") + o.pcName;
    return "Ton Admin " + (o.token.version.empty() ? std::string("d'avant la 1.44.0") : o.token.version) + " n'est plus \xC3\xA0 jour" +
           (who.empty() ? std::string() : " (" + who + ")");
}

std::string chipText() {
    std::lock_guard<std::mutex> lock(st().mutex);
    if (!st().offer || !st().chip) return {};
    return st().offer->offer.version + " disponible";
}

void later() {
    std::lock_guard<std::mutex> lock(st().mutex);
    if (st().offer) st().chip = true;
    st().toast.reset();
}

std::string lastLook() {
    std::lock_guard<std::mutex> lock(st().mutex);
    return st().lastLook;
}

std::optional<Toast> toast() {
    std::lock_guard<std::mutex> lock(st().mutex);
    if (st().toast && steadyNow() - st().toast->shownAt > 20.0) st().toast.reset();   // 20 s : il s'en va de lui-meme
    return st().toast;
}

void dismissToast() {
    std::lock_guard<std::mutex> lock(st().mutex);
    st().toast.reset();
}

// ============================================================ l'installation ==
std::string selfPath() {
#ifdef _WIN32
    std::wstring exe(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (n == 0 || n >= exe.size()) return {};
    exe.resize(n);
    const int len = WideCharToMultiByte(CP_UTF8, 0, exe.data(), static_cast<int>(exe.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, exe.data(), static_cast<int>(exe.size()), out.data(), len, nullptr, nullptr);
    return out;
#else
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
#endif
}

std::string silentArguments(const std::string& studioExe, const std::string& project) {
    std::string a = "/SILENT /SUPPRESSMSGBOXES /NORESTART /RELANCER=" + quoted(studioExe);
    if (!project.empty()) a += " /RELANCERPROJET=" + quoted(project);
    return a;
}

std::string adminArguments() { return "/SILENT /SUPPRESSMSGBOXES /NORESTART"; }

bool beginInstall(App& app, bool all) {
    State& s = st();
    std::vector<filedl::Want> wants;
    const AdminToken token = all ? readAdminToken() : AdminToken{};   // avant le verrou (adminFolder le prend)
    const std::string project = projectName(app);
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.offer) return false;
        const Phase p = s.install.phase;
        if (p == Phase::Downloading || p == Phase::Saving || p == Phase::Countdown || p == Phase::Launching) return false;
        Install in;
        in.version = s.offer->offer.version;
        in.all = all && s.offer->adminPc;
        s.installing.clear();
        const auto* studios = s.offer->studios();
        if (!studios) {
            in.phase = Phase::Failed;
            in.message = "La version " + in.version + " n'a pas d'installateur des Studios : rien \xC3\xA0 installer.";
            s.install = in;
            return false;
        }
        s.installing.push_back(*studios);
        if (in.all)
            if (const auto* a = s.offer->admin()) s.installing.push_back(*a);
        for (const auto& f : s.installing) {
            if (!f.refused.empty()) {
                in.phase = Phase::Failed;
                in.message = filedl::refusedText(f.name);
                s.install = in;
                return false;
            }
            filedl::Want w = wantOf(f);
            if (f.kind == "admin") {
                const AdminToken& t = token;
                if (!t.ok) {
                    in.phase = Phase::Failed;
                    in.message = "L'installateur de l'Admin est r\xC3\xA9serv\xC3\xA9 aux admins : " + (t.why.empty() ? std::string("le jeton d'admin de ce PC manque") : t.why) + ".";
                    s.install = in;
                    return false;
                }
                w.adminToken = t.base64;
            }
            in.total += f.size;
            wants.push_back(std::move(w));
        }
        in.project = project;
        in.phase = Phase::Downloading;
        in.name = wants.front().name;
        s.install = in;
        s.toast.reset();
    }
    if (!filedl::start(std::move(wants), "maj")) {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.install.phase = Phase::Failed;
        s.install.message = "Un t\xC3\xA9l\xC3\xA9" "chargement est d\xC3\xA9j\xC3\xA0 en cours : r\xC3\xA9" "essaie dans un instant.";
        return false;
    }
    return true;
}

void cancelInstall() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.mutex);
    switch (s.install.phase) {
        case Phase::Downloading:
            filedl::cancel();
            [[fallthrough]];
        case Phase::Saving:
        case Phase::Countdown:
            s.install.phase = Phase::Cancelled;
            s.install.message = "L'installation de la " + s.install.version + " est annul\xC3\xA9" "e : rien n'est install\xC3\xA9.";
            s.chip = true;
            break;
        default: break;
    }
}

Install install() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.mutex);
    Install in = s.install;
    if (in.phase == Phase::Countdown) in.secondsLeft = std::max(0, static_cast<int>(std::ceil(in.closeAt - steadyNow())));
    return in;
}

bool fetchNoticeFile(const std::string& noticeId, const std::string& fileId, std::string* why) {
    const auto n = notice(noticeId);
    const auto d = n ? dataOf(n->data) : std::nullopt;
    const NoticeFile* f = nullptr;
    if (d)
        for (const auto& x : d->files)
            if (x.id == fileId) f = &x;
    if (!f) {
        if (why) *why = "ce fichier n'est plus dans la notification";
        return false;
    }
    if (!f->refused.empty()) {
        if (why) *why = filedl::refusedText(f->name);
        return false;
    }
    filedl::Want w = wantOf(*f);
    if (f->kind == "admin" && f->token.empty()) w.adminToken = readAdminToken().base64;
    if (!filedl::start({w}, "fichier")) {
        if (why) *why = "un t\xC3\xA9l\xC3\xA9" "chargement est d\xC3\xA9j\xC3\xA0 en cours";
        return false;
    }
    std::lock_guard<std::mutex> lock(st().mutex);
    st().fetchNotice = noticeId;
    st().fetchFile = fileId;
    st().fetchName = f->name;
    st().fetchThenInstall = st().fetchThenSave = false;
    st().lastFetch.clear();
    return true;
}

std::string fetching(float* fraction) {
    std::lock_guard<std::mutex> lock(st().mutex);
    if (st().fetchNotice.empty() || st().fetchFile.empty()) return {};
    if (fraction) {
        const auto p = filedl::progress();
        *fraction = p.total ? static_cast<float>(static_cast<double>(p.done) / static_cast<double>(p.total)) : 0.f;
    }
    return st().fetchFile;
}

std::string lastFetch(bool* ok, std::string* path) {
    std::lock_guard<std::mutex> lock(st().mutex);
    if (ok) *ok = st().lastFetchOk;
    if (path) *path = st().lastFetchPath;
    return st().lastFetch;
}

namespace {

// Lancer l'installateur d'un fichier d'un message, normalement (pas en silencieux).
void launchNormally(const std::string& path, const std::string& name) {
    std::string why;
    if (!canLaunch()) {
        std::lock_guard<std::mutex> lock(st().mutex);
        st().lastFetch = name + " est v\xC3\xA9rifi\xC3\xA9 ; sous Windows, il serait lanc\xC3\xA9 ainsi : " + quoted(path) + ". Ici, rien n'est lanc\xC3\xA9.";
        return;
    }
    if (!run(path, {}, false, why)) {
        std::lock_guard<std::mutex> lock(st().mutex);
        st().lastFetch = name + " n'a pas pu \xC3\xAAtre lanc\xC3\xA9 : " + why + ".";
        st().lastFetchOk = false;
        return;
    }
    std::lock_guard<std::mutex> lock(st().mutex);
    st().lastFetch = name + " est lanc\xC3\xA9 (v\xC3\xA9rifi\xC3\xA9 : sign\xC3\xA9 par l'\xC3\xA9" "diteur, empreinte SHA-256).";
}

void askSave(const std::string& path, const std::string& name) {
    const std::string start = ui::pathIn(Settings::sharedFolder(), name);
    (void)ui::browsePath(ui::saveFile("Tous les fichiers|*.*", start, "Enregistrer " + name), start, [path, name](std::string to) {
        std::error_code ec;
        fs::copy_file(P(path), P(to), fs::copy_options::overwrite_existing, ec);
        std::lock_guard<std::mutex> lock(st().mutex);
        st().lastFetch = ec ? name + " n'a pas pu \xC3\xAAtre enregistr\xC3\xA9 : " + ec.message() + "." : name + " est enregistr\xC3\xA9 : " + to + ".";
        st().lastFetchOk = !ec;
    });
}

} // namespace

bool installMessageFile(const std::string& noticeId, const std::string& fileId, std::string* why) {
    const auto n = notice(noticeId);
    const auto d = n ? dataOf(n->data) : std::nullopt;
    if (d)
        for (const auto& f : d->files)
            if (f.id == fileId && f.refused.empty())
                if (auto p = filedl::verifiedCopy(wantOf(f))) {
                    launchNormally(*p, f.name);
                    return true;
                }
    if (!fetchNoticeFile(noticeId, fileId, why)) return false;
    std::lock_guard<std::mutex> lock(st().mutex);
    st().fetchThenInstall = true;
    return true;
}

bool saveMessageFile(const std::string& noticeId, const std::string& fileId, std::string* why) {
    const auto n = notice(noticeId);
    const auto d = n ? dataOf(n->data) : std::nullopt;
    if (d)
        for (const auto& f : d->files)
            if (f.id == fileId && f.refused.empty())
                if (auto p = filedl::verifiedCopy(wantOf(f))) {
                    askSave(*p, f.name);
                    return true;
                }
    if (!fetchNoticeFile(noticeId, fileId, why)) return false;
    std::lock_guard<std::mutex> lock(st().mutex);
    st().fetchThenSave = true;
    return true;
}

void tick(App& app) {
    State& s = st();
    // ---- un telechargement fini
    std::string tag;
    if (auto outs = filedl::take(&tag)) {
        if (tag == "fichier") {
            std::string id, file, name;
            bool thenInstall = false, thenSave = false;
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                id = s.fetchNotice;
                file = s.fetchFile;
                name = s.fetchName;
                thenInstall = s.fetchThenInstall;
                thenSave = s.fetchThenSave;
                s.fetchNotice.clear();
                s.fetchFile.clear();
            }
            const filedl::Outcome o = outs->empty() ? filedl::Outcome{} : outs->front();
            if (o.ok) markFile(id, file, true, {});
            else if (o.refused) markFile(id, file, false, o.message);
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                s.lastFetch = o.message.empty() ? name + " : rien n'a \xC3\xA9t\xC3\xA9 t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9." : o.message;
                s.lastFetchOk = o.ok;
                s.lastFetchPath = o.path;
            }
            if (o.ok && thenInstall) launchNormally(o.path, name);
            if (o.ok && thenSave) askSave(o.path, name);
        } else if (tag == "maj") {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (s.install.phase == Phase::Downloading) {
                bool ok = !outs->empty() && outs->size() == s.installing.size();
                std::string why;
                for (std::size_t i = 0; i < outs->size(); ++i) {
                    const auto& o = (*outs)[i];
                    if (!o.ok) {
                        ok = false;
                        why = o.message;
                        if (o.refused && i < s.installing.size()) s.installing[i].refused = o.message;
                    }
                }
                s.install.downloaded = ok;
                s.install.signature = ok;
                s.install.hash = ok;
                s.install.fraction = ok ? 1.f : s.install.fraction;
                if (ok) {
                    s.install.phase = Phase::Saving;
                } else {
                    s.install.phase = outs->empty() || !outs->back().cancelled ? Phase::Failed : Phase::Cancelled;
                    s.install.message = why.empty() ? std::string("Le t\xC3\xA9l\xC3\xA9" "chargement n'a pas abouti.") : why;
                }
            }
            // les fichiers verifies (ou refuses) : la notification de la mise a jour le dit
            for (const auto& n : notices::list()) {
                if (n.key != "maj:" + s.install.version) continue;
                auto d = dataOf(n.data);
                if (!d) break;
                for (std::size_t i = 0; i < outs->size() && i < s.installing.size(); ++i)
                    for (auto& f : d->files)
                        if (f.id == s.installing[i].id) {
                            if ((*outs)[i].ok) f.hashOk = f.manifestOk = true;
                            else if ((*outs)[i].refused) f.refused = (*outs)[i].message;
                        }
                (void)notices::setData(n.id, writeData(*d));
                break;
            }
        }
    }
    Phase phase;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        phase = s.install.phase;
        if (phase == Phase::Downloading) {
            const auto p = filedl::progress();
            s.install.done = p.done;
            s.install.total = p.total;
            s.install.name = p.name;
            s.install.fraction = p.total ? static_cast<float>(static_cast<double>(p.done) / static_cast<double>(p.total)) : 0.f;
        }
    }
    // ---- le projet enregistre (seulement s'il a change), puis 5 s avant de lancer et de fermer
    if (phase == Phase::Saving) {
        std::string error;
        bool saved = false;
        if (modified(app)) {
            if (auto r = app.saveProject(); !r) error = r.error().message();
            else saved = true;
        }
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!error.empty()) {
            s.install.phase = Phase::Failed;
            s.install.message = "Le projet n'a pas pu \xC3\xAAtre enregistr\xC3\xA9 (" + error + ") : la mise \xC3\xA0 jour est arr\xC3\xAAt\xC3\xA9" "e, rien n'est install\xC3\xA9.";
            return;
        }
        s.install.saved = saved || !s.install.project.empty();
        const std::string exe = selfPath();
        const std::string project = app.project() ? app.projectFolder() : std::string();
        s.install.command = quoted(s.installing.empty() ? std::string() : filedl::finalPath(wantOf(s.installing.front()))) + " " + silentArguments(exe, project);
        s.install.phase = Phase::Countdown;
        s.install.closeAt = steadyNow() + 5.0;
        return;
    }
    if (phase == Phase::Countdown) {
        double closeAt;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            closeAt = s.install.closeAt;
        }
        if (steadyNow() < closeAt) return;
        // le dernier mot : une modification faite pendant les 5 s est enregistree aussi
        if (modified(app))
            if (auto r = app.saveProject(); !r) {
                std::lock_guard<std::mutex> lock(s.mutex);
                s.install.phase = Phase::Failed;
                s.install.message = "Le projet n'a pas pu \xC3\xAAtre enregistr\xC3\xA9 (" + r.error().message() + ") : la mise \xC3\xA0 jour est arr\xC3\xAAt\xC3\xA9" "e.";
                return;
            }
        std::vector<ls::AnnouncedFile> files;
        std::string exe = selfPath(), project = app.project() ? app.projectFolder() : std::string();
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            files = s.installing;
            if (!canLaunch()) {
                s.install.phase = Phase::Done;
                std::string what = "Sous Windows, ";
                if (files.size() > 1) what += "l'installateur de l'Admin serait lanc\xC3\xA9 (" + quoted(filedl::finalPath(wantOf(files[1]))) + " " + adminArguments() + "), puis ";
                what += "l'installateur des Studios serait lanc\xC3\xA9 ainsi : " + s.install.command + " ; puis ce Studio se fermerait. Ici, rien n'est lanc\xC3\xA9.";
                s.install.message = what;
                return;
            }
            s.install.phase = Phase::Launching;
            s.launchDone = false;
        }
        if (s.launcher.joinable()) s.launcher.join();
        s.launcher = std::thread([files, exe, project] {
            State& t = st();
            std::string why;
            bool ok = true;
            // l'Admin d'abord, attendu (silencieux) ; puis les Studios (silencieux, ils relancent ce Studio)
            if (files.size() > 1) {
                ok = run(filedl::finalPath(wantOf(files[1])), adminArguments(), true, why);
                if (!ok) why = "l'installateur de l'Admin : " + why;
            }
            if (ok) {
                ok = run(filedl::finalPath(wantOf(files[0])), silentArguments(exe, project), false, why);
                if (!ok) why = "l'installateur des Studios : " + why;
            }
            {
                std::lock_guard<std::mutex> lock(t.mutex);
                t.launchOk = ok;
                t.launchWhy = why;
            }
            t.launchDone = true;
        });
        return;
    }
    if (phase == Phase::Launching && s.launchDone) {
        if (s.launcher.joinable()) s.launcher.join();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.launchOk) {
            s.install.phase = Phase::Done;
            s.install.message = "L'installateur de la " + s.install.version + " est lanc\xC3\xA9 : " + productLabel() + " se ferme, puis se rouvre.";
            app.requestQuit();
        } else {
            s.install.phase = Phase::Failed;
            s.install.message = "La mise \xC3\xA0 jour n'a pas pu \xC3\xAAtre lanc\xC3\xA9" "e (" + s.launchWhy + ") : " + productLabel() + " reste ouvert.";
        }
    }
}

void shutdown() {
    filedl::shutdown();
    State& s = st();
    if (s.launcher.joinable()) s.launcher.join();
}

} // namespace app::updates
