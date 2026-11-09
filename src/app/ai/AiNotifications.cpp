// =============================================================================
//  app/ai/AiNotifications.cpp - 1.21.2 : le journal des notifications de l'Assistant (voir le .hpp)
// =============================================================================
#include "AiNotifications.hpp"
#include "../Notices.hpp"   // 1.38.0 : le carnet general (le meme mecanisme)

#include "AiJson.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

namespace app::ai::notifs {

namespace {

std::filesystem::path P(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

// LE JOURNAL : un mutex le tient le temps d'une copie ; le fil du journal relit puis ecrit le fichier.
struct Journal {
    std::mutex                            m;
    std::condition_variable               cv;
    std::vector<Item>                     items;   // les plus anciennes d'abord (la plus recente a la fin)
    std::uint64_t                         nextId{1};
    std::uint64_t                         rev{1};
    std::string                           file;    // vide : pas de garde sur le disque
    bool                                  dirty{false}, stop{false}, loaded{true}, writing{false};
    std::chrono::steady_clock::time_point lastWrite{};
    std::string                           error;
    std::thread                           worker;

    ~Journal() { shut(); }

    void shut() {
        std::thread t;
        {
            std::lock_guard<std::mutex> lk(m);
            if (!worker.joinable()) return;
            stop = true;
            t = std::move(worker);
        }
        cv.notify_all();
        t.join();
        std::lock_guard<std::mutex> lk(m);
        file.clear();
        stop = false;
        loaded = true;
    }
};

// 1.38.0 : deux carnets, le meme mecanisme - 0 : l'Assistant (ce module), 1 : les notifications generales du Studio
// (app/Notices.hpp : la licence, les offres, les annonces de l'editeur).
Journal& journal(int which = 0) {
    static Journal j[2];
    return j[which == 1 ? 1 : 0];
}

// Au-dela de kMax : les plus anciennes lues partent d'abord, puis les plus anciennes (sous le mutex).
void bound(std::vector<Item>& items) {
    while (items.size() > kMax) {
        auto it = std::find_if(items.begin(), items.end(), [](const Item& x) { return x.read; });
        items.erase(it != items.end() ? it : items.begin());
    }
}

// Un changement (sous le mutex) : la revision, et le fichier a reecrire.
void touched(Journal& j) {
    ++j.rev;
    if (!j.file.empty()) {
        j.dirty = true;
        j.cv.notify_all();
    }
}

bool writeItems(const std::string& file, const std::vector<Item>& items, std::string* why) {
    std::error_code ec;
    std::filesystem::create_directories(P(file).parent_path(), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    if (auto r = core::writeFileAtomic(P(file), toJson(items), how); !r) {
        if (why) *why = "\xC3\xA9" "criture impossible : " + file + " (" + r.error().message() + ")";
        return false;
    }
    return true;
}

// Le fichier relu, remis AVANT ce qui a ete poste entre-temps (plus ancien) ; une cle deja la : la plus recente garde,
// leurs apparitions s'ajoutent.
void merge(Journal& j, std::vector<Item> loaded) {
    std::vector<Item> all;
    all.reserve(loaded.size() + j.items.size());
    for (auto& x : loaded) {
        if (!x.key.empty()) {
            const auto it = std::find_if(j.items.begin(), j.items.end(), [&](const Item& y) { return y.key == x.key; });
            if (it != j.items.end()) {
                it->count += std::max(1, x.count);
                continue;
            }
        }
        x.id = j.nextId++;   // des identifiants neufs ; elles restent devant (plus anciennes)
        all.push_back(std::move(x));
    }
    for (auto& x : j.items) all.push_back(std::move(x));
    j.items = std::move(all);
    bound(j.items);
}

void run(Journal& j) {
    std::string file;
    {
        std::lock_guard<std::mutex> lk(j.m);
        file = j.file;
    }
    // 1. relire (sur ce fil : l'interface n'attend pas le disque)
    std::vector<Item> loaded;
    std::string text, why;
    if (core::readFileAll(P(file), text) && !fromJson(text, loaded, &why)) loaded.clear();
    std::unique_lock<std::mutex> lk(j.m);
    if (!why.empty()) j.error = "notifications illisibles (" + why + ") : le fichier sera r\xC3\xA9\xC3\xA9" "crit";
    if (!loaded.empty()) {
        merge(j, std::move(loaded));
        ++j.rev;
    }
    j.loaded = true;
    j.cv.notify_all();
    // 2. ecrire apres chaque changement, au plus une fois par seconde (la sortie : tout de suite)
    for (;;) {
        j.cv.wait(lk, [&] { return j.stop || j.dirty; });
        if (!j.dirty) break;   // stop, rien a ecrire
        const auto next = j.lastWrite + std::chrono::seconds(1);
        while (!j.stop && std::chrono::steady_clock::now() < next) j.cv.wait_until(lk, next);
        if (!j.dirty) {
            if (j.stop) break;
            continue;
        }
        std::vector<Item> copy = j.items;
        const std::string target = j.file;
        j.dirty = false;
        j.writing = true;
        lk.unlock();
        std::string w;
        const bool ok = writeItems(target, copy, &w);
        lk.lock();
        j.writing = false;
        j.lastWrite = std::chrono::steady_clock::now();
        j.error = ok ? std::string{} : w;
        j.cv.notify_all();
    }
}

std::tm localOf(std::int64_t at) {
    const std::time_t t = static_cast<std::time_t>(at);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// Le numero du jour local (pour comparer deux dates : aujourd'hui, hier).
long dayNumber(std::int64_t at) {
    std::tm tm = localOf(at);
    tm.tm_hour = 12;   // midi : les changements d'heure ne decalent pas le jour
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t noon = std::mktime(&tm);
    return static_cast<long>((static_cast<long long>(noon) + 43200LL) / 86400LL);
}

} // namespace

namespace {

// ============================================================================ le journal ==
// 1.44.0 : `sameEvent` (le carnet general du Studio) - la meme cle avec le meme titre et le meme detail, c'est le MEME
// evenement poste de nouveau (au lancement, a la reouverture d'un projet, a chaque connexion) : il garde sa place, son
// etat lu et son compteur ; seuls son bouton et ses donnees suivent. Un autre titre ou detail : un evenement nouveau.
std::uint64_t postIn(Journal& j, Item n, bool sameEvent = false) {
    if (n.at == 0) n.at = nowSeconds();
    if (n.tone.empty()) n.tone = "info";
    n.read = false;
    n.count = std::max(1, n.count);
    std::lock_guard<std::mutex> lk(j.m);
    if (!n.key.empty()) {
        const auto it = std::find_if(j.items.begin(), j.items.end(), [&](const Item& x) { return x.key == n.key; });
        if (it != j.items.end() && sameEvent && it->title == n.title && it->detail == n.detail) {
            if (it->button != n.button || it->action != n.action || it->data != n.data || it->category != n.category || it->tone != n.tone) {
                it->button = std::move(n.button);
                it->action = std::move(n.action);
                it->data = std::move(n.data);
                it->category = std::move(n.category);
                it->tone = std::move(n.tone);
                touched(j);
            }
            return it->id;
        }
        if (it != j.items.end()) {
            n.id = it->id;
            n.count = it->count + 1;
            j.items.erase(it);
            j.items.push_back(std::move(n));
            touched(j);
            return j.items.back().id;
        }
    }
    n.id = j.nextId++;
    const std::uint64_t id = n.id;
    j.items.push_back(std::move(n));
    bound(j.items);
    touched(j);
    return id;
}

std::vector<Item> listIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return std::vector<Item>(j.items.rbegin(), j.items.rend());
}

std::optional<Item> findIn(Journal& j, std::uint64_t id) {
    std::lock_guard<std::mutex> lk(j.m);
    for (const auto& x : j.items)
        if (x.id == id) return x;
    return std::nullopt;
}

std::size_t sizeIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return j.items.size();
}

std::size_t unreadIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return static_cast<std::size_t>(std::count_if(j.items.begin(), j.items.end(), [](const Item& x) { return !x.read; }));
}

bool unreadErrorIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return std::any_of(j.items.begin(), j.items.end(), [](const Item& x) { return !x.read && x.tone == "error"; });
}

bool markReadIn(Journal& j, std::uint64_t id, bool read) {
    std::lock_guard<std::mutex> lk(j.m);
    for (auto& x : j.items)
        if (x.id == id) {
            if (x.read == read) return false;
            x.read = read;
            touched(j);
            return true;
        }
    return false;
}

// 1.44.0 : les donnees d'une notification changent (un fichier joint verifie ou refuse) - sans la faire remonter, ni la
// rendre non lue, ni compter une apparition de plus.
bool setDataIn(Journal& j, std::uint64_t id, std::string data) {
    std::lock_guard<std::mutex> lk(j.m);
    for (auto& x : j.items)
        if (x.id == id) {
            if (x.data == data) return false;
            x.data = std::move(data);
            touched(j);
            return true;
        }
    return false;
}

std::size_t markAllReadIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    std::size_t n = 0;
    for (auto& x : j.items)
        if (!x.read) {
            x.read = true;
            ++n;
        }
    if (n) touched(j);
    return n;
}

bool removeIn(Journal& j, std::uint64_t id) {
    std::lock_guard<std::mutex> lk(j.m);
    const auto before = j.items.size();
    std::erase_if(j.items, [&](const Item& x) { return x.id == id; });
    if (j.items.size() == before) return false;
    touched(j);
    return true;
}

std::size_t clearReadIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    const auto n = std::erase_if(j.items, [](const Item& x) { return x.read; });
    if (n) touched(j);
    return static_cast<std::size_t>(n);
}

std::uint64_t revisionIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return j.rev;
}

// ======================================================================= le disque ==
void openIn(Journal& j, const std::string& file) {
    {
        std::lock_guard<std::mutex> lk(j.m);
        if (j.worker.joinable() && j.file == file) return;
    }
    j.shut();
    if (file.empty()) return;
    std::lock_guard<std::mutex> lk(j.m);
    j.file = file;
    j.loaded = false;
    j.stop = false;
    j.dirty = false;
    j.error.clear();
    j.worker = std::thread([&j] { run(j); });
}

void closeIn(Journal& j) { j.shut(); }

bool openedIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return j.worker.joinable();
}

bool waitIdleIn(Journal& j, int timeoutMs) {
    std::unique_lock<std::mutex> lk(j.m);
    return j.cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return j.loaded && !j.dirty && !j.writing; });
}

std::string lastErrorIn(Journal& j) {
    std::lock_guard<std::mutex> lk(j.m);
    return j.error;
}

void resetForTestsIn(Journal& j) {
    j.shut();
    std::lock_guard<std::mutex> lk(j.m);
    j.items.clear();
    j.nextId = 1;
    j.dirty = false;
    j.error.clear();
    ++j.rev;
}

} // namespace

// ============================================================================ l'Assistant ==
std::uint64_t post(Item n) { return postIn(journal(0), std::move(n)); }
std::vector<Item> list() { return listIn(journal(0)); }
std::optional<Item> find(std::uint64_t id) { return findIn(journal(0), id); }
std::size_t size() { return sizeIn(journal(0)); }
std::size_t unread() { return unreadIn(journal(0)); }
bool unreadError() { return unreadErrorIn(journal(0)); }
bool markRead(std::uint64_t id, bool read) { return markReadIn(journal(0), id, read); }
std::size_t markAllRead() { return markAllReadIn(journal(0)); }
bool remove(std::uint64_t id) { return removeIn(journal(0), id); }
std::size_t clearRead() { return clearReadIn(journal(0)); }
std::uint64_t revision() { return revisionIn(journal(0)); }
void open(const std::string& file) { openIn(journal(0), file); }
bool opened() { return openedIn(journal(0)); }
bool waitIdle(int timeoutMs) { return waitIdleIn(journal(0), timeoutMs); }
std::string lastError() { return lastErrorIn(journal(0)); }
void resetForTests() { resetForTestsIn(journal(0)); }
void close() { closeIn(journal(0)); }

std::string toJson(const std::vector<Item>& oldestFirst) {
    json::Value root = json::Value::object();
    root.set("format", 1);
    json::Value list = json::Value::array();
    for (const auto& x : oldestFirst) {
        json::Value o = json::Value::object();
        o.set("heure", x.at);
        if (!x.category.empty()) o.set("categorie", x.category);
        o.set("titre", x.title);
        if (!x.detail.empty()) o.set("detail", x.detail);
        if (!x.button.empty()) o.set("bouton", x.button);
        if (!x.action.empty()) o.set("action", x.action);
        o.set("ton", x.tone);
        if (!x.key.empty()) o.set("cle", x.key);
        if (x.count > 1) o.set("fois", x.count);
        o.set("lue", x.read);
        if (!x.data.empty()) {   // 1.39.0 : un objet (le tableau, l'avant / l'apres)
            json::Value d;
            if (json::parse(x.data, d, nullptr) && d.isObject()) o.set("donnees", std::move(d));
        }
        list.push(std::move(o));
    }
    root.set("notifications", std::move(list));
    return json::write(root);
}

bool fromJson(std::string_view text, std::vector<Item>& oldestFirst, std::string* why) {
    json::Value v;
    if (!json::parse(text, v, why)) return false;
    if (!v.isObject()) {
        if (why) *why = "ce n'est pas un objet";
        return false;
    }
    oldestFirst.clear();
    for (const auto& o : v.at("notifications").items()) {
        if (!o.isObject()) continue;
        Item x;
        x.at = o.integer("heure");
        x.category = o.str("categorie");
        x.title = o.str("titre");
        x.detail = o.str("detail");
        x.button = o.str("bouton");
        x.action = o.str("action");
        x.tone = o.str("ton", "info");
        x.key = o.str("cle");
        x.count = static_cast<int>(std::clamp<std::int64_t>(o.integer("fois", 1), 1, 1000000));
        x.read = o.flag("lue");
        if (const auto* d = o.find("donnees"); d && d->isObject()) x.data = json::write(*d);   // 1.39.0
        if (x.title.empty()) continue;
        oldestFirst.push_back(std::move(x));
    }
    if (oldestFirst.size() > kMax) bound(oldestFirst);
    return true;
}

// ================================================================= les heures, les jours ==
std::int64_t nowSeconds() {
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string dayLabel(std::int64_t at, std::int64_t now) {
    const long d = dayNumber(now) - dayNumber(at);
    if (d == 0) return "Aujourd'hui";
    if (d == 1) return "Hier";
    static const char* kDays[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
    static const char* kMonths[] = {"janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin",
                                    "juillet", "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre"};
    const std::tm tm = localOf(at);
    return std::string(kDays[std::clamp(tm.tm_wday, 0, 6)]) + " " + std::to_string(tm.tm_mday) + " " + kMonths[std::clamp(tm.tm_mon, 0, 11)] + " "
           + std::to_string(tm.tm_year + 1900);
}

std::string timeLabel(std::int64_t at) {
    const std::tm tm = localOf(at);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

// ============================================================================ les liens ==
std::string toConversation(const std::string& conversation, int message) {
    return "conv:" + conversation + (message >= 0 ? "#" + std::to_string(message) : std::string{});
}
std::string toCard(const std::string& conversation, int message) { return "carte:" + conversation + "#" + std::to_string(message); }
std::string toAttachment(const std::string& attachmentId) { return "piece:" + attachmentId; }
std::string toFileModel(const std::string& attachmentId) { return "modele:" + attachmentId; }
std::string toSouvenirs(const std::string& status) { return "souvenirs:" + status; }
std::string toScanReport() { return "scan"; }
std::string toIncident(const std::string& folderName) { return "incident:" + folderName; }
std::string toSettings() { return "parametres"; }
std::string toMission(const std::string& missionId) { return "mission:" + missionId; }   // 1.22.0

Target parseAction(std::string_view action) {
    Target t;
    if (action == "scan" || action == "parametres") {
        t.kind = std::string(action);
        return t;
    }
    const auto colon = action.find(':');
    if (colon == std::string_view::npos || colon == 0) return t;
    const std::string kind(action.substr(0, colon));
    std::string rest(action.substr(colon + 1));
    if (kind != "conv" && kind != "carte" && kind != "piece" && kind != "modele" && kind != "souvenirs" && kind != "incident"
        && kind != "mission")   // 1.22.0
        return t;
    if (kind == "conv" || kind == "carte") {
        if (const auto hash = rest.rfind('#'); hash != std::string::npos) {
            const std::string n = rest.substr(hash + 1);
            if (!n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                t.message = std::atoi(n.c_str());
                rest.resize(hash);
            }
        }
        if (kind == "carte" && t.message < 0) return t;
    }
    if (rest.empty()) return t;
    t.kind = kind;
    t.id = std::move(rest);
    return t;
}

// =================================================================== les sources d'ailleurs ==
void fileModelSaved(const std::string& model, const std::string& genre, const std::string& attachmentId, const std::string& file) {
    Item n;
    n.category = "Mod\xC3\xA8le de fichier";
    n.title = "Mod\xC3\xA8le \xC2\xAB " + model + " \xC2\xBB enregistr\xC3\xA9";
    n.detail = (genre.empty() ? std::string{} : genre + " \xE2\x80\x94 ")
               + "dans la m\xC3\xA9moire de l'IA : un classeur aux m\xC3\xAAmes intitul\xC3\xA9s sera lu directement"
               + (file.empty() ? std::string(".") : " (appris sur \xC2\xAB " + file + " \xC2\xBB).");
    n.tone = "ok";
    if (!attachmentId.empty()) {
        n.button = "Voir le mod\xC3\xA8le";
        n.action = toFileModel(attachmentId);
    }
    n.key = "modele:" + model;
    (void)post(std::move(n));
}

void incidentAnalysed(const std::string& alarm, const std::string& folderName, bool done, const std::string& source,
                      const std::string& error) {
    Item n;
    n.category = "Incident";
    const std::string what = alarm.empty() ? std::string("l'incident") : "l'incident \xC2\xAB " + alarm + " \xC2\xBB";
    if (done) {
        n.title = "Analyse IA termin\xC3\xA9" "e pour " + what;
        n.detail = "L'analyse" + (source.empty() ? std::string{} : " de " + source)
                   + " est ajout\xC3\xA9" "e \xC3\xA0 son dossier, \xC3\xA0 c\xC3\xB4t\xC3\xA9 de la fiche (l'analyse initiale n'est pas r\xC3\xA9\xC3\xA9" "crite).";
        n.tone = "ok";
    } else {
        n.title = "Analyse IA en attente pour " + what;
        n.detail = (error.empty() ? std::string("l'IA n'a pas r\xC3\xA9pondu") : error)
                   + " : l'incident est gard\xC3\xA9, relance l'analyse plus tard (Nouvelle analyse).";
        n.tone = "warning";
    }
    if (!folderName.empty()) {
        n.button = "Ouvrir l'incident";
        n.action = toIncident(folderName);
        n.key = "incident-analyse:" + folderName;
    }
    (void)post(std::move(n));
}

std::string oneLine(std::string_view text, std::size_t max) {
    std::string out;
    out.reserve(std::min(text.size(), max + 4));
    bool space = false;
    for (const char c : text) {
        const bool blank = c == '\n' || c == '\r' || c == '\t' || c == ' ' || c == '*' || c == '`' || c == '#';
        if (blank) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    if (out.size() > max) {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
        out = out.substr(0, cut) + "\xE2\x80\xA6";
    }
    return out;
}

} // namespace app::ai::notifs

// ============================================================================ 1.38.0 : le carnet general ==
//  Les notifications generales du Studio (app/Notices.hpp) : le meme journal, un autre carnet (journal(1)).
namespace app::notices {
namespace nf = app::ai::notifs;
std::uint64_t post(nf::Item n) { return nf::postIn(nf::journal(1), std::move(n), /*sameEvent=*/true); }   // 1.44.0
std::vector<nf::Item> list() { return nf::listIn(nf::journal(1)); }
std::optional<nf::Item> find(std::uint64_t id) { return nf::findIn(nf::journal(1), id); }
std::size_t size() { return nf::sizeIn(nf::journal(1)); }
std::size_t unread() { return nf::unreadIn(nf::journal(1)); }
bool unreadError() { return nf::unreadErrorIn(nf::journal(1)); }
bool markRead(std::uint64_t id, bool read) { return nf::markReadIn(nf::journal(1), id, read); }
bool setData(std::uint64_t id, std::string data) { return nf::setDataIn(nf::journal(1), id, std::move(data)); }   // 1.44.0
std::size_t markAllRead() { return nf::markAllReadIn(nf::journal(1)); }
bool remove(std::uint64_t id) { return nf::removeIn(nf::journal(1), id); }
std::size_t clearRead() { return nf::clearReadIn(nf::journal(1)); }
std::uint64_t revision() { return nf::revisionIn(nf::journal(1)); }
void open(const std::string& file) { nf::openIn(nf::journal(1), file); }
bool opened() { return nf::openedIn(nf::journal(1)); }
bool waitIdle(int timeoutMs) { return nf::waitIdleIn(nf::journal(1), timeoutMs); }
std::string lastError() { return nf::lastErrorIn(nf::journal(1)); }
void resetForTests() { nf::resetForTestsIn(nf::journal(1)); }
void close() { nf::closeIn(nf::journal(1)); }
} // namespace app::notices
