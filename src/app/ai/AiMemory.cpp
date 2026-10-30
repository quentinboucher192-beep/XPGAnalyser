// =============================================================================
//  app/ai/AiMemory.cpp - le centre "Memoire et apprentissage de l'IA" : le moteur
//  (voir AiMemory.hpp).
// =============================================================================
#include "AiMemory.hpp"
#include "AiKnowledge.hpp"
#include "AiPaths.hpp"
#include "../Dossiers.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace app::ai::memory {

namespace fs = std::filesystem;

namespace {

fs::path pathOf(std::string_view utf8) { return dossiers::cheminDe(utf8); }
std::string utf8Of(const fs::path& p) { return dossiers::utf8De(p); }

// Les fichiers d'un dossier (ou un fichier seul), sans suivre les liens.
Usage usageOf(const fs::path& p) {
    Usage u;
    std::error_code ec;
    const auto st = fs::symlink_status(p, ec);
    if (ec) return u;
    if (fs::is_regular_file(st)) {
        u.files = 1;
        const auto n = fs::file_size(p, ec);
        if (!ec) u.bytes = n;
        return u;
    }
    if (!fs::is_directory(st)) return u;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->is_symlink(e2)) continue;
        ++u.files;
        const auto n = it->file_size(e2);
        if (!e2) u.bytes += n;
    }
    return u;
}

// Ce qui est sous `base` sans etre un lien : la seule chose qu'on accepte de supprimer.
bool inside(const fs::path& base, const fs::path& p) {
    std::error_code ec;
    const fs::path b = fs::weakly_canonical(base, ec);
    if (ec) return false;
    const fs::path q = fs::weakly_canonical(p, ec);
    if (ec) return false;
    const auto bs = b.native(), qs = q.native();
    if (qs.size() <= bs.size() || qs.compare(0, bs.size(), bs) != 0) return false;
    const auto sep = qs[bs.size()];
    return sep == '/' || sep == '\\';
}

// Vide un dossier (garde le dossier) : ce qui part et ce qui reste.
void emptyFolder(const fs::path& folder, const fs::path& guard, CleanResult& r) {
    std::error_code ec;
    if (!fs::is_directory(fs::symlink_status(folder, ec))) return;   // absent, ou un lien : on n'y touche pas
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path child = it->path();
        if (!inside(guard, child)) {
            ++r.failures;
            if (r.firstError.empty()) r.firstError = "hors du dossier permis : " + utf8Of(child);
            continue;
        }
        const Usage u = usageOf(child);
        std::error_code e2;
        fs::remove_all(child, e2);
        if (e2 || fs::exists(fs::symlink_status(child, e2))) {
            ++r.failures;
            if (r.firstError.empty()) r.firstError = utf8Of(child) + " : " + (e2 ? e2.message() : std::string("toujours l\xC3\xA0"));
            continue;
        }
        r.freed += u;
    }
}

std::string lowered(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

std::tm localNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// Un extrait autour de la premiere plage trouvee, sur une ligne.
std::string excerptOf(const std::string& text, const ui::SearchQuery& q) {
    const auto ranges = q.ranges(text);
    std::size_t at = ranges.empty() ? 0 : ranges.front().first;
    std::size_t from = at > 40 ? at - 40 : 0;
    while (from > 0 && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80) --from;
    // au debut d'un mot, si un espace est proche
    if (from > 0)
        if (const auto sp = text.find(' ', from); sp != std::string::npos && sp < at && sp - from < 20) from = sp + 1;
    std::size_t to = std::min(text.size(), at + 90);
    while (to < text.size() && (static_cast<unsigned char>(text[to]) & 0xC0) == 0x80) ++to;
    std::string s = text.substr(from, to - from);
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (from > 0) s = "\xE2\x80\xA6" + s;
    if (to < text.size()) s += "\xE2\x80\xA6";
    return s;
}

} // namespace

// ---------------------------------------------------------------- les dates ---
std::string nowIso() {
    const std::tm tm = localNow();
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string displayDate(std::string_view iso) {
    if (iso.size() < 16 || iso[4] != '-' || iso[7] != '-' || (iso[10] != 'T' && iso[10] != ' ')) return std::string(iso);
    std::string s;
    s += iso.substr(8, 2);
    s += '/';
    s += iso.substr(5, 2);
    s += '/';
    s += iso.substr(0, 4);
    s += ' ';
    s += iso.substr(11, 5);
    return s;
}

std::string sizeText(std::uintmax_t bytes) { return dossiers::tailleLisible(bytes); }

// ------------------------------------------------- les categories du disque ---
namespace {
// L'ordre suit l'enum : partInfo y lit directement.
constexpr PartInfo kParts[] = {
    {Part::Conversations, "conversations", "Conversations", "conversations/", false, true},
    {Part::Attachments, "pieces-jointes", "Pi\xC3\xA8" "ces jointes", "pieces-jointes/", false, true},
    {Part::Knowledge, "connaissances", "Connaissances ajout\xC3\xA9" "es", "connaissances/", false, true},
    {Part::Index, "index", "Index de recherche", "index/", false, true},
    {Part::Logs, "journaux", "Journaux techniques", "journaux/", false, true},
    {Part::Activity, "activite", "Journal d'activit\xC3\xA9", "activite.jsonl", false, true},
    {Part::Settings, "parametres", "Param\xC3\xA8tres", "parametres.json, memoire.json", false, false},
    {Part::CacheRequests, "cache.requetes", "Cache temporaire des requ\xC3\xAA" "tes", "cache/requetes/", true, false},
    {Part::CacheProcessing, "cache.traitement", "Fichiers temporaires de traitement", "cache/traitement/", true, false},
    {Part::CacheImages, "cache.images", "Images temporaires", "cache/images/", true, false},
    {Part::CacheDocuments, "cache.documents", "Documents temporaires", "cache/documents/", true, false},
    {Part::CachePreviews, "cache.apercus", "Donn\xC3\xA9" "es de pr\xC3\xA9visualisation", "cache/apercus/", true, false},
    {Part::CacheDownloads, "cache.telechargements", "Fichiers t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9s r\xC3\xA9g\xC3\xA9n\xC3\xA9rables", "cache/telechargements/", true,
     false},
    {Part::CacheOther, "cache.autres", "Autres fichiers du cache", "cache/", true, false},
    {Part::Other, "autres", "Autres fichiers", "IA/", false, false},
};
static_assert(sizeof(kParts) / sizeof(kParts[0]) == kPartCount, "une ligne par categorie, dans l'ordre");

// Les six sous-dossiers du cache, dans l'ordre du contrat (AiPaths.hpp) et de l'enum.
constexpr Part kCacheParts[] = {Part::CacheRequests, Part::CacheProcessing, Part::CacheImages,
                                Part::CacheDocuments, Part::CachePreviews, Part::CacheDownloads};
} // namespace

const PartInfo& partInfo(Part p) noexcept {
    const auto i = static_cast<std::size_t>(p);
    return i < kPartCount ? kParts[i] : kParts[kPartCount - 1];
}

const PartInfo* partByKey(std::string_view key) noexcept {
    for (const auto& p : kParts)
        if (p.key == key) return &p;
    return nullptr;
}

Usage Measure::cache() const noexcept {
    Usage u;
    for (const auto& p : kParts)
        if (p.cache) u += of(p.part);
    return u;
}

Usage Measure::technical() const noexcept {
    Usage u;
    for (const Part p : {Part::Index, Part::Logs, Part::Activity, Part::Settings, Part::Other}) u += of(p);
    return u;
}

Measure measure() {
    Measure m;
    m.root = root();
    if (m.root.empty()) return m;
    const fs::path base = pathOf(m.root);
    std::error_code ec;
    // L'espace du disque qui porte (ou portera) IA/ : le plus proche dossier qui existe.
    fs::path probe = base;
    while (!probe.empty() && !fs::exists(probe, ec)) {
        const auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    if (!probe.empty()) {
        const auto sp = fs::space(probe, ec);
        if (!ec) {
            m.disk.known = true;
            m.disk.capacity = sp.capacity;
            m.disk.free = sp.free;
            m.disk.available = sp.available;
        }
    }
    m.lastCleaning = readState().lastCleaning;
    m.exists = fs::is_directory(base, ec);
    if (!m.exists) return m;
    const auto add = [&](Part p, const Usage& u) { m.parts[static_cast<std::size_t>(p)] += u; };
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        const std::string name = lowered(utf8Of(p.filename()));
        std::error_code e2;
        const bool dir = it->is_directory(e2) && !it->is_symlink(e2);
        if (dir && name == "conversations") add(Part::Conversations, usageOf(p));
        else if (dir && name == "pieces-jointes") add(Part::Attachments, usageOf(p));
        else if (dir && name == "connaissances") add(Part::Knowledge, usageOf(p));
        else if (dir && name == "index") add(Part::Index, usageOf(p));
        else if (dir && name == "journaux") add(Part::Logs, usageOf(p));
        else if (!dir && startsWith(name, "activite.jsonl")) add(Part::Activity, usageOf(p));
        else if (!dir && (startsWith(name, "parametres.json") || startsWith(name, "memoire.json"))) add(Part::Settings, usageOf(p));
        else if (dir && name == "cache") {
            for (fs::directory_iterator c(p, e2), cend; !e2 && c != cend; c.increment(e2)) {
                const std::string sub = lowered(utf8Of(c->path().filename()));
                Part part = Part::CacheOther;
                std::error_code e3;
                if (c->is_directory(e3) && !c->is_symlink(e3))
                    for (std::size_t k = 0; k < std::size(kCacheParts); ++k)
                        if (sub == kCacheCategories[k].folder) part = kCacheParts[k];
                add(part, usageOf(c->path()));
            }
        } else {
            add(Part::Other, usageOf(p));
        }
    }
    if (ec) m.problems.push_back(m.root + " : " + ec.message());
    for (const auto& u : m.parts) m.total += u;
    return m;
}

// ------------------------------------------------------------- la memoire vive ---
Ram readRam() {
    Ram r;
#if defined(_WIN32)
    // K32GetProcessMemoryInfo est dans kernel32 depuis Windows 7 ; avant, dans psapi.dll : cherche a la
    // demande, rien de plus a lier.
    using GetInfo = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    GetInfo getInfo = nullptr;
    if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        getInfo = reinterpret_cast<GetInfo>(reinterpret_cast<void*>(GetProcAddress(k, "K32GetProcessMemoryInfo")));
    if (!getInfo)
        if (HMODULE p = LoadLibraryW(L"psapi.dll"))
            getInfo = reinterpret_cast<GetInfo>(reinterpret_cast<void*>(GetProcAddress(p, "GetProcessMemoryInfo")));
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof pmc;
    if (getInfo && getInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
        r.processKnown = true;
        r.process = pmc.WorkingSetSize;
        r.peak = pmc.PeakWorkingSetSize;
    }
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        r.systemKnown = true;
        r.total = ms.ullTotalPhys;
        r.available = ms.ullAvailPhys;
    }
    r.source = "GetProcessMemoryInfo, GlobalMemoryStatusEx";
#else
    const auto kb = [](const std::string& file, const char* key, std::uint64_t& out) {
        std::ifstream in(file);
        std::string line;
        const std::size_t n = std::strlen(key);
        while (std::getline(in, line)) {
            if (line.compare(0, n, key) != 0) continue;
            unsigned long long v = 0;
            if (std::sscanf(line.c_str() + n, " %llu", &v) == 1) {
                out = static_cast<std::uint64_t>(v) * 1024u;
                return true;
            }
        }
        return false;
    };
    r.processKnown = kb("/proc/self/status", "VmRSS:", r.process);
    if (!kb("/proc/self/status", "VmHWM:", r.peak)) r.peak = r.process;
    r.systemKnown = kb("/proc/meminfo", "MemTotal:", r.total) && kb("/proc/meminfo", "MemAvailable:", r.available);
    r.source = "/proc/self/status, /proc/meminfo";
#endif
    if (r.peak < r.process) r.peak = r.process;
    r.localAi = 0;   // aucun modele ni traitement IA ne tourne sur ce PC
    return r;
}

// ----------------------------------------------------------- les conversations ---
bool readText(const std::string& utf8Path, std::string& out) {
    std::ifstream in(pathOf(utf8Path), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool writeText(const std::string& utf8Path, std::string_view content, std::string* why) {
    const fs::path target = pathOf(utf8Path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (why) *why = "\xC3\xA9" "criture impossible : " + utf8Of(tmp);
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            if (why) *why = "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + utf8Of(tmp) + " (disque plein ?)";
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (why) *why = "remplacement impossible : " + utf8Path;
            return false;
        }
    }
    return true;
}

std::vector<Conversation> loadConversations() {
    std::vector<Conversation> out;
    const std::string dir = conversationsDir();
    if (dir.empty()) return out;
    const fs::path base = pathOf(dir);
    const fs::path ia = pathOf(root());
    std::error_code ec;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || lowered(utf8Of(it->path().extension())) != ".json") continue;
        Conversation c;
        c.file = utf8Of(it->path());
        c.id = utf8Of(it->path().stem());
        c.title = c.id;
        c.bytes = it->file_size(e2);
        std::string text;
        Json j;
        std::string why;
        if (!readText(c.file, text) || !parseJson(text, j, &why) || !j.isObject()) {
            c.readable = false;
            c.why = why.empty() ? std::string("illisible") : why;
            out.push_back(std::move(c));
            continue;
        }
        c.id = j.str("id", c.id);
        c.title = j.str("titre", c.id);
        c.created = j.str("cree");
        c.modified = j.str("modifie", c.created);
        c.demo = j.flag("demo");
        if (const Json* p = j.get("projet"); p && p->isObject()) {
            c.project = p->str("nom");
            c.projectPath = p->str("chemin");
            c.view = p->str("vue");
        } else if (p && p->isString()) {
            c.project = p->text;
        }
        if (const Json* msgs = j.get("messages"); msgs && msgs->isArray()) {
            for (const auto& mj : msgs->items) {
                if (!mj.isObject()) continue;
                Message m;
                m.id = mj.str("id");
                m.role = mj.str("role");
                m.date = mj.str("date");
                m.text = mj.str("texte");
                if (const Json* pieces = mj.get("pieces"); pieces && pieces->isArray())
                    for (const auto& pj : pieces->items) {
                        if (!pj.isObject()) continue;
                        Attachment a;
                        a.id = pj.str("id");
                        a.name = pj.str("nom");
                        a.type = pj.str("type");
                        a.file = pj.str("fichier");
                        a.declared = static_cast<std::uintmax_t>(std::max(0.0, pj.num("octets")));
                        if (!a.file.empty()) {
                            const fs::path f = ia / pathOf(a.file);
                            std::error_code e3;
                            if (fs::is_regular_file(f, e3)) {
                                a.present = true;
                                a.bytes = fs::file_size(f, e3);
                            }
                        }
                        a.conversation = c.id;
                        a.conversationTitle = c.title;
                        c.attachments.push_back(std::move(a));
                        ++m.attachments;
                    }
                c.messages.push_back(std::move(m));
            }
        }
        out.push_back(std::move(c));
    }
    std::stable_sort(out.begin(), out.end(), [](const Conversation& a, const Conversation& b) { return a.modified > b.modified; });
    return out;
}

std::vector<AttachmentFolder> attachmentFolders(const std::vector<Conversation>& conversations) {
    std::vector<AttachmentFolder> out;
    const std::string dir = attachmentsDir();
    if (dir.empty()) return out;
    // Le dossier d'une piece : "pieces-jointes/<id>/<nom>" -> <id>.
    std::map<std::string, std::pair<std::string, std::string>> owner;
    for (const auto& c : conversations)
        for (const auto& a : c.attachments) {
            std::string f = a.file;
            std::replace(f.begin(), f.end(), '\\', '/');
            const auto slash = f.find('/');
            const auto next = slash == std::string::npos ? std::string::npos : f.find('/', slash + 1);
            if (startsWith(f, "pieces-jointes/") && next != std::string::npos) owner[f.substr(slash + 1, next - slash - 1)] = {c.id, c.title};
            if (!a.id.empty() && !owner.count(a.id)) owner[a.id] = {c.id, c.title};
        }
    std::error_code ec;
    for (fs::directory_iterator it(pathOf(dir), ec), end; !ec && it != end; it.increment(ec)) {
        AttachmentFolder f;
        f.id = utf8Of(it->path().filename());
        f.folder = utf8Of(it->path());
        f.usage = usageOf(it->path());
        std::error_code e2;
        if (it->is_directory(e2)) {
            for (fs::directory_iterator c(it->path(), e2), cend; !e2 && c != cend; c.increment(e2))
                f.names += (f.names.empty() ? "" : ", ") + utf8Of(c->path().filename());
        } else {
            f.names = f.id;
        }
        if (const auto o = owner.find(f.id); o != owner.end()) {
            f.conversation = o->second.first;
            f.conversationTitle = o->second.second;
        }
        out.push_back(std::move(f));
    }
    std::sort(out.begin(), out.end(), [](const AttachmentFolder& a, const AttachmentFolder& b) { return a.id < b.id; });
    return out;
}

// -------------------------------------------------------------------- l'activite ---
std::string activityTypeLabel(std::string_view type) {
    if (type == "conversation") return "Conversation";
    if (type == "connaissance") return "Connaissance";
    if (type == "fichier") return "Fichier analys\xC3\xA9";
    if (type == "regle") return "R\xC3\xA8gle";
    if (type == "nettoyage") return "Nettoyage";
    if (type == "suppression") return "Suppression";
    return std::string(type);
}

std::vector<Activity> readActivity() {
    std::vector<Activity> out;
    const std::string file = activityFile();
    if (file.empty()) return out;
    std::string text;
    if (!readText(file, text)) return out;
    std::size_t from = 0;
    while (from < text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        std::string_view line(text.data() + from, nl - from);
        from = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty()) continue;
        Json j;
        if (!parseJson(line, j) || !j.isObject()) continue;
        out.push_back({j.str("date"), j.str("type"), j.str("texte")});
    }
    return out;
}

bool appendActivity(std::string_view type, std::string_view text) {
    const std::string file = activityFile();
    if (file.empty()) return false;
    std::error_code ec;
    fs::create_directories(pathOf(root()), ec);
    Json j = Json::makeObject();
    j.set("date", Json::makeString(nowIso()));
    j.set("type", Json::makeString(std::string(type)));
    j.set("texte", Json::makeString(std::string(text)));
    std::ofstream out(pathOf(file), std::ios::binary | std::ios::app);
    if (!out) return false;
    const std::string line = writeJson(j, false) + "\n";
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    return static_cast<bool>(out);
}

// ----------------------------------------------------------- l'etat du centre ---
std::string stateFile() { return detail::join(root(), "memoire.json"); }

State readState() {
    State s;
    const std::string file = stateFile();
    if (file.empty()) return s;
    std::string text;
    if (!readText(file, text)) return s;
    Json j;
    if (!parseJson(text, j) || !j.isObject()) return s;
    s.lastCleaning = j.str("dernierNettoyage");
    if (const Json* off = j.get("integreesInactives"); off && off->isArray())
        for (const auto& v : off->items)
            if (v.isString()) s.builtInOff.push_back(v.text);
    s.raw = std::move(j);
    return s;
}

bool writeState(const State& s) {
    const std::string file = stateFile();
    if (file.empty()) return false;
    Json j = s.raw.isObject() ? s.raw : Json::makeObject();
    if (!j.get("format")) j.set("format", Json::makeNumber(1));
    j.set("dernierNettoyage", Json::makeString(s.lastCleaning));
    Json off = Json::makeArray();
    for (const auto& id : s.builtInOff) off.items.push_back(Json::makeString(id));
    j.set("integreesInactives", std::move(off));
    return writeText(file, writeJson(j, true));
}

// ----------------------------------------------------------------- le nettoyage ---
std::string_view cacheConsequence(Part p) {
    switch (p) {
        case Part::CacheRequests:
            return "Les r\xC3\xA9ponses gard\xC3\xA9" "es pour ne pas redemander : la prochaine demande identique repart chez le fournisseur.";
        case Part::CacheProcessing: return "Les fichiers interm\xC3\xA9" "diaires d'un traitement : refaits si un traitement en a besoin.";
        case Part::CacheImages: return "Les images r\xC3\xA9" "duites ou converties : refaites \xC3\xA0 la prochaine utilisation.";
        case Part::CacheDocuments: return "Les textes extraits des documents joints : relus depuis la pi\xC3\xA8" "ce jointe originale.";
        case Part::CachePreviews: return "Les aper\xC3\xA7us des propositions : redessin\xC3\xA9s quand on rouvre une proposition.";
        case Part::CacheDownloads: return "Les fichiers t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9s qui se r\xC3\xA9g\xC3\xA9n\xC3\xA8rent : t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9s de nouveau au besoin.";
        case Part::CacheOther: return "Des fichiers pos\xC3\xA9s dans cache/ hors des six cat\xC3\xA9gories : rien ne s'en sert.";
        default: return {};
    }
}

std::vector<CacheLine> cachePlan(const Measure& m) {
    std::vector<CacheLine> out;
    for (std::size_t k = 0; k < std::size(kCacheParts); ++k)
        out.push_back({kCacheParts[k], cacheDir(kCacheCategories[k].folder), m.of(kCacheParts[k]), std::string(cacheConsequence(kCacheParts[k]))});
    if (m.of(Part::CacheOther).files > 0)
        out.push_back({Part::CacheOther, cacheDir(), m.of(Part::CacheOther), std::string(cacheConsequence(Part::CacheOther))});
    return out;
}

CleanResult clearCache(const std::vector<Part>& parts) {
    CleanResult r;
    const std::string dir = cacheDir();
    if (dir.empty()) return r;
    const fs::path cache = pathOf(dir);
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(cache, ec))) {
        r.failures = 1;
        r.firstError = "cache/ est un lien : rien n'est vid\xC3\xA9";
        return r;
    }
    std::vector<std::string> names;
    for (const Part p : parts) {
        if (!partInfo(p).cache) continue;
        if (std::find(names.begin(), names.end(), std::string(partInfo(p).label)) != names.end()) continue;
        names.emplace_back(partInfo(p).label);
        if (p == Part::CacheOther) {
            // ce qui est dans cache/ hors des six dossiers
            for (fs::directory_iterator it(cache, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string sub = lowered(utf8Of(it->path().filename()));
                bool known = false;
                for (const auto& c : kCacheCategories) known = known || sub == c.folder;
                std::error_code e2;
                if (known && it->is_directory(e2) && !it->is_symlink(e2)) continue;
                if (!inside(cache, it->path())) continue;
                const Usage u = usageOf(it->path());
                fs::remove_all(it->path(), e2);
                if (e2) {
                    ++r.failures;
                    if (r.firstError.empty()) r.firstError = utf8Of(it->path()) + " : " + e2.message();
                } else {
                    r.freed += u;
                }
            }
            continue;
        }
        for (std::size_t k = 0; k < std::size(kCacheParts); ++k)
            if (kCacheParts[k] == p) emptyFolder(cache / pathOf(kCacheCategories[k].folder), cache, r);
    }
    State s = readState();
    s.lastCleaning = nowIso();
    (void)writeState(s);
    std::string what;
    for (const auto& n : names) what += (what.empty() ? "" : ", ") + n;
    (void)appendActivity("nettoyage", "Cache vid\xC3\xA9 (" + (what.empty() ? std::string("aucune cat\xC3\xA9gorie") : what) + ") : "
                                         + sizeText(r.freed.bytes) + " lib\xC3\xA9r\xC3\xA9s, " + std::to_string(r.freed.files)
                                         + (r.freed.files > 1 ? " fichiers" : " fichier")
                                         + (r.failures ? ", " + std::to_string(r.failures) + " non supprim\xC3\xA9(s)" : std::string{}));
    return r;
}

std::string_view persistentKey(Persistent p) {
    switch (p) {
        case Persistent::Conversations: return "conversations";
        case Persistent::Attachments: return "pieces-jointes";
        case Persistent::Knowledge: return "connaissances";
        case Persistent::Activity: return "activite";
        case Persistent::Index: return "index";
        case Persistent::Logs: return "journaux";
    }
    return {};
}

Part persistentPart(Persistent p) {
    switch (p) {
        case Persistent::Conversations: return Part::Conversations;
        case Persistent::Attachments: return Part::Attachments;
        case Persistent::Knowledge: return Part::Knowledge;
        case Persistent::Activity: return Part::Activity;
        case Persistent::Index: return Part::Index;
        case Persistent::Logs: return Part::Logs;
    }
    return Part::Other;
}

namespace {
std::string_view persistentConsequence(Persistent p) {
    switch (p) {
        case Persistent::Conversations: return "Toutes les conversations et leurs messages : l'Assistant ne pourra plus les reprendre.";
        case Persistent::Attachments: return "Les copies des fichiers joints : les conversations les citent encore, mais ils ne se rouvrent plus.";
        case Persistent::Knowledge:
            return "Les connaissances ajout\xC3\xA9" "es (utilisateur, projets, r\xC3\xA8gles, pr\xC3\xA9" "f\xC3\xA9rences). Celles du logiciel restent.";
        case Persistent::Activity: return "L'historique de ce qui a \xC3\xA9t\xC3\xA9 appris, nettoy\xC3\xA9 et supprim\xC3\xA9.";
        case Persistent::Index: return "Les index de recherche : la recherche les refera.";
        case Persistent::Logs: return "Les journaux techniques de l'Assistant.";
    }
    return {};
}
} // namespace

std::vector<PersistentLine> persistentPlan(const Measure& m) {
    std::vector<PersistentLine> out;
    for (const Persistent p : kPersistentAll)
        out.push_back({p, persistentPart(p), std::string(partInfo(persistentPart(p)).label), m.of(persistentPart(p)), std::string(persistentConsequence(p))});
    return out;
}

CleanResult deletePersistent(const std::vector<Persistent>& what, bool confirmed) {
    CleanResult r;
    if (!confirmed) {
        r.failures = 1;
        r.firstError = "non confirm\xC3\xA9 : rien n'est supprim\xC3\xA9";
        return r;
    }
    if (root().empty()) return r;
    const fs::path ia = pathOf(root());
    std::string done;
    for (const Persistent p : kPersistentAll) {
        if (std::find(what.begin(), what.end(), p) == what.end()) continue;
        CleanResult part;
        if (p == Persistent::Activity) {
            const fs::path f = pathOf(activityFile());
            std::error_code ec;
            if (fs::is_regular_file(fs::symlink_status(f, ec))) {
                const Usage u = usageOf(f);
                fs::remove(f, ec);
                if (ec) {
                    ++part.failures;
                    part.firstError = utf8Of(f) + " : " + ec.message();
                } else {
                    part.freed += u;
                }
            }
        } else {
            std::string dir;
            switch (p) {
                case Persistent::Conversations: dir = conversationsDir(); break;
                case Persistent::Attachments: dir = attachmentsDir(); break;
                case Persistent::Knowledge: dir = knowledgeDir(); break;
                case Persistent::Index: dir = indexDir(); break;
                case Persistent::Logs: dir = logsDir(); break;
                case Persistent::Activity: break;
            }
            emptyFolder(pathOf(dir), ia, part);
        }
        r.freed += part.freed;
        r.failures += part.failures;
        if (r.firstError.empty()) r.firstError = part.firstError;
        done += (done.empty() ? "" : ", ") + std::string(partInfo(persistentPart(p)).label) + " (" + sizeText(part.freed.bytes) + ")";
    }
    (void)appendActivity("suppression", "Donn\xC3\xA9" "es persistantes supprim\xC3\xA9" "es : " + (done.empty() ? std::string("aucune") : done));
    return r;
}

bool deleteConversation(std::string_view id, std::string* why) {
    for (const auto& c : loadConversations()) {
        if (c.id != id) continue;
        const fs::path f = pathOf(c.file);
        if (!inside(pathOf(conversationsDir()), f)) {
            if (why) *why = "hors du dossier des conversations";
            return false;
        }
        std::error_code ec;
        fs::remove(f, ec);
        if (ec) {
            if (why) *why = ec.message();
            return false;
        }
        (void)appendActivity("suppression", "Conversation supprim\xC3\xA9" "e : " + c.title + " (" + std::to_string(c.messages.size())
                                                + (c.messages.size() > 1 ? " messages)" : " message)"));
        return true;
    }
    if (why) *why = "conversation introuvable : " + std::string(id);
    return false;
}

bool deleteAttachmentFolder(std::string_view id, std::string* why) {
    if (id.empty() || id.find('/') != std::string_view::npos || id.find('\\') != std::string_view::npos || id == "." || id == "..") {
        if (why) *why = "nom refus\xC3\xA9";
        return false;
    }
    const fs::path base = pathOf(attachmentsDir());
    const fs::path f = base / pathOf(id);
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(f, ec)) || !inside(base, f)) {
        if (why) *why = "pi\xC3\xA8" "ce jointe introuvable : " + std::string(id);
        return false;
    }
    std::string names;
    if (fs::is_directory(fs::symlink_status(f, ec)))
        for (fs::directory_iterator c(f, ec), end; !ec && c != end; c.increment(ec)) names += (names.empty() ? "" : ", ") + utf8Of(c->path().filename());
    const Usage u = usageOf(f);
    fs::remove_all(f, ec);
    if (ec) {
        if (why) *why = ec.message();
        return false;
    }
    (void)appendActivity("suppression", "Pi\xC3\xA8" "ce jointe supprim\xC3\xA9" "e : " + (names.empty() ? std::string(id) : names) + " (" + sizeText(u.bytes) + ")");
    return true;
}

// ------------------------------------------------------------------ la recherche ---
std::vector<Hit> search(std::string_view query, const std::vector<Conversation>& conversations, const std::vector<Knowledge>& knowledge) {
    std::vector<Hit> out;
    const ui::SearchQuery q(query);
    if (q.empty()) return out;
    constexpr std::size_t kMax = 500;
    for (const auto& c : conversations) {
        if (out.size() >= kMax) break;
        if (q.matches({c.title, c.project}))
            out.push_back({Hit::Kind::Conversation, c.title, excerptOf(c.title + (c.project.empty() ? std::string{} : " \xC2\xB7 " + c.project), q),
                           "Conversation", c.id});
        for (const auto& m : c.messages)
            if (out.size() < kMax && q.matches({m.text}))
                out.push_back({Hit::Kind::Message, c.title, excerptOf(m.text, q),
                               m.role == "assistant" ? "R\xC3\xA9ponse de l'Assistant" : "Message de l'utilisateur", c.id});
        for (const auto& a : c.attachments)
            if (out.size() < kMax && q.matches({a.name, a.type}))
                out.push_back({Hit::Kind::Attachment, a.name, c.title + (a.present ? std::string{} : " (fichier absent)"), "Pi\xC3\xA8" "ce jointe",
                               a.file});
    }
    for (const auto& k : knowledge) {
        if (out.size() >= kMax) break;
        if (!q.matches({k.name, k.description, k.content, k.project})) continue;
        const std::string ex = q.matches({k.name}) || q.matches({k.description}) ? k.description : excerptOf(k.content, q);
        out.push_back({Hit::Kind::Knowledge, k.name, ex, "Connaissance" + std::string(k.builtIn ? " du logiciel" : ""), k.id});
    }
    return out;
}

} // namespace app::ai::memory
