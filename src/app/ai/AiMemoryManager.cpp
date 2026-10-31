// =============================================================================
//  app/ai/AiMemoryManager.cpp - voir AiMemoryManager.hpp
// =============================================================================
#include "AiMemoryManager.hpp"

#include "AiMemory.hpp"
#include "AiPaths.hpp"
#include "../Dossiers.hpp"
#include "../../hmi/HmiCrypto.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace app::ai::memory {

namespace fs = std::filesystem;

namespace {

fs::path P(std::string_view utf8) { return dossiers::cheminDe(utf8); }
std::string U(const fs::path& p) { return dossiers::utf8De(p); }

std::mutex& projectsMutex() {
    static std::mutex m;
    return m;
}

std::string compactStamp() {
    std::string s = nowIso();   // 2026-10-31T17:45:12
    s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
    s.erase(std::remove(s.begin(), s.end(), ':'), s.end());
    std::replace(s.begin(), s.end(), 'T', '-');
    return s;   // 20261031-174512
}

std::string hashHex(std::string_view text, std::size_t chars) { return hmi::toHex(hmi::sha256(text)).substr(0, chars); }

struct ProjectsFile {
    std::vector<ProjectIdentity> list;
    Json                         raw;
};

ProjectsFile readProjects() {
    ProjectsFile f;
    std::string text;
    if (projectsFile().empty() || !readText(projectsFile(), text)) return f;
    if (!parseJson(text, f.raw) || !f.raw.isObject()) {
        f.raw = Json{};
        return f;
    }
    if (const Json* list = f.raw.get("projets"); list && list->isArray())
        for (const auto& j : list->items) {
            if (!j.isObject() || j.str("id").empty()) continue;
            ProjectIdentity p;
            p.id = j.str("id");
            p.name = j.str("nom");
            p.path = j.str("chemin");
            p.created = j.str("cree");
            p.seen = j.str("vu");
            f.list.push_back(std::move(p));
        }
    return f;
}

bool writeProjects(const ProjectsFile& f) {
    Json j = f.raw.isObject() ? f.raw : Json::makeObject();
    j.set("format", Json::makeNumber(1));
    Json list = Json::makeArray();
    for (const auto& p : f.list) {
        Json o = Json::makeObject();
        o.set("id", Json::makeString(p.id));
        o.set("nom", Json::makeString(p.name));
        o.set("chemin", Json::makeString(p.path));
        o.set("cree", Json::makeString(p.created));
        o.set("vu", Json::makeString(p.seen));
        list.items.push_back(std::move(o));
    }
    j.set("projets", std::move(list));
    return writeText(projectsFile(), writeJson(j, true));
}

void markAmbiguous(std::vector<ProjectIdentity>& list) {
    for (auto& p : list) {
        p.ambiguous = false;
        for (const auto& o : list)
            if (&o != &p && !p.name.empty() && o.name == p.name) p.ambiguous = true;
    }
}

} // namespace

// ---------------------------------------------------------------- les projets ---
std::string normalizedProjectPath(std::string_view path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path p = P(path);
    fs::path q = fs::weakly_canonical(p, ec);
    if (ec || q.empty()) {
        ec.clear();
        q = fs::absolute(p, ec);
        if (ec) q = p;
    }
    std::string s = U(q.lexically_normal());
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return s;
}

ProjectIdentity identifyProject(std::string_view name, std::string_view path, std::string_view created) {
    ProjectIdentity out;
    out.name = std::string(name);
    out.created = std::string(created);
    if (path.empty()) return out;
    out.path = normalizedProjectPath(path);
    if (out.name.empty()) out.name = U(P(out.path).filename());
    if (root().empty()) {   // rien ne s'ecrit : l'identifiant du chemin seul
        out.id = "prj-" + hashHex(out.path, 16);
        return out;
    }
    const std::lock_guard<std::mutex> lock(projectsMutex());
    ProjectsFile f = readProjects();
    const std::string now = nowIso();
    ProjectIdentity* found = nullptr;
    for (auto& p : f.list)
        if (p.path == out.path) found = &p;
    // Deplace : meme nom, meme date de creation, et l'ancien dossier n'existe plus (une copie, elle, le laisse en place).
    bool relinked = false;
    if (!found && !out.created.empty())
        for (auto& p : f.list) {
            std::error_code ec;
            if (p.name == out.name && p.created == out.created && !fs::exists(P(p.path), ec)) {
                found = &p;
                relinked = true;
                break;
            }
        }
    bool isNew = false;
    if (found) {
        found->path = out.path;
        if (!out.name.empty()) found->name = out.name;
        if (found->created.empty()) found->created = out.created;
        found->seen = now;
    } else {
        static int counter = 0;
        ProjectIdentity p;
        for (;;) {
            p.id = "prj-" + hashHex(out.path + "|" + out.created + "|" + now + "|" + std::to_string(++counter), 16);
            if (std::none_of(f.list.begin(), f.list.end(), [&](const ProjectIdentity& o) { return o.id == p.id; })) break;
        }
        p.name = out.name;
        p.path = out.path;
        p.created = out.created;
        p.seen = now;
        f.list.push_back(std::move(p));
        found = &f.list.back();
        isNew = true;
    }
    out = *found;
    out.relinked = relinked;
    out.isNew = isNew;
    markAmbiguous(f.list);
    for (const auto& p : f.list)
        if (p.id == out.id) out.ambiguous = p.ambiguous;
    (void)writeProjects(f);
    if (relinked) (void)appendActivity("connaissance", "Projet " + out.name + " retrouv\xC3\xA9 apr\xC3\xA8s un d\xC3\xA9placement : ses souvenirs le suivent");
    return out;
}

std::vector<ProjectIdentity> knownProjects() {
    std::vector<ProjectIdentity> out;
    if (root().empty()) return out;
    {
        const std::lock_guard<std::mutex> lock(projectsMutex());
        out = readProjects().list;
    }
    markAmbiguous(out);
    std::stable_sort(out.begin(), out.end(), [](const ProjectIdentity& a, const ProjectIdentity& b) { return a.seen > b.seen; });
    return out;
}

std::string projectNameOf(std::string_view id) {
    if (id.empty()) return {};
    for (const auto& p : knownProjects())
        if (p.id == id) return p.name;
    return {};
}

Context contextFor(const ProjectIdentity& p, std::string conversationId) {
    Context c;
    c.projectId = p.id;
    c.projectName = p.name;
    c.nameAmbiguous = p.ambiguous;
    c.conversationId = std::move(conversationId);
    return c;
}

// --------------------------------------------------------------- les sauvegardes ---
namespace {

bool copyOne(const fs::path& from, const fs::path& to, std::string* why) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (why) *why = U(from) + " : " + ec.message();
        return false;
    }
    return true;
}

// Les fichiers .json d'un dossier (pas les sous-dossiers, pas les liens).
std::vector<fs::path> jsonFiles(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2) || !it->is_regular_file(e2)) continue;
        if (U(it->path().extension()) == ".json") out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool validBackupName(std::string_view name) {
    return name.rfind("memoire-", 0) == 0 && name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos
           && name.find("..") == std::string_view::npos;
}

} // namespace

bool backupMemory(std::string_view reason, std::string* name, std::string* why) {
    if (root().empty()) {
        if (why) *why = "aucun dossier IA";
        return false;
    }
    const fs::path base = P(backupsDir());
    const std::string stamp = compactStamp();
    fs::path dir;
    std::string folderName;
    std::error_code ec;
    for (int n = 1;; ++n) {
        char seq[16];
        std::snprintf(seq, sizeof seq, "%03d", n);   // trois chiffres : le nom se trie comme la date
        folderName = "memoire-" + stamp + "-" + seq;
        dir = base / P(folderName);
        if (!fs::exists(dir, ec)) break;
    }
    fs::create_directories(dir / "connaissances", ec);
    if (ec) {
        if (why) *why = "dossier de sauvegarde impossible : " + ec.message();
        return false;
    }
    std::size_t files = 0;
    std::uintmax_t bytes = 0;
    const auto take = [&](const fs::path& from, const fs::path& to) {
        if (!copyOne(from, to, why)) return false;
        ++files;
        std::error_code e2;
        bytes += fs::file_size(to, e2);
        return true;
    };
    for (const auto& f : jsonFiles(P(knowledgeDir())))
        if (!take(f, dir / "connaissances" / f.filename())) {
            fs::remove_all(dir, ec);
            return false;
        }
    for (const std::string& f : {stateFile(), projectsFile()}) {
        const fs::path src = P(f);
        if (fs::is_regular_file(src, ec) && !take(src, dir / src.filename())) {
            fs::remove_all(dir, ec);
            return false;
        }
    }
    Json info = Json::makeObject();
    info.set("format", Json::makeNumber(1));
    info.set("date", Json::makeString(nowIso()));
    info.set("raison", Json::makeString(std::string(reason)));
    info.set("fichiers", Json::makeNumber(static_cast<double>(files)));
    info.set("octets", Json::makeNumber(static_cast<double>(bytes)));
    if (!writeText(U(dir / "sauvegarde.json"), writeJson(info, true), why)) {
        fs::remove_all(dir, ec);
        return false;
    }
    if (name) *name = folderName;
    // Les dix plus recentes restent.
    const auto all = listBackups();
    for (std::size_t i = kKeepBackups; i < all.size(); ++i) {
        const fs::path old = P(all[i].folder);
        if (validBackupName(all[i].name) && old.parent_path() == base) fs::remove_all(old, ec);
    }
    (void)appendActivity("connaissance", "M\xC3\xA9moire sauvegard\xC3\xA9" "e (" + std::string(reason) + ") : " + folderName);
    return true;
}

std::vector<Backup> listBackups() {
    std::vector<Backup> out;
    if (root().empty()) return out;
    std::error_code ec;
    for (fs::directory_iterator it(P(backupsDir()), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2) || !it->is_directory(e2)) continue;
        Backup b;
        b.name = U(it->path().filename());
        if (!validBackupName(b.name)) continue;
        b.folder = U(it->path());
        std::string text;
        Json j;
        if (readText(U(it->path() / "sauvegarde.json"), text) && parseJson(text, j) && j.isObject()) {
            b.date = j.str("date");
            b.reason = j.str("raison");
            b.files = static_cast<std::size_t>(std::max(0.0, j.num("fichiers")));
            b.bytes = static_cast<std::uintmax_t>(std::max(0.0, j.num("octets")));
        } else {
            continue;   // une sauvegarde sans sa fiche : incomplete, jamais restauree
        }
        out.push_back(std::move(b));
    }
    std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) { return a.name > b.name; });
    return out;
}

bool restoreBackup(std::string_view name, std::string* why) {
    if (root().empty() || !validBackupName(name)) {
        if (why) *why = "sauvegarde inconnue : " + std::string(name);
        return false;
    }
    const auto all = listBackups();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Backup& b) { return b.name == name; });
    if (it == all.end()) {
        if (why) *why = "sauvegarde introuvable ou incompl\xC3\xA8te : " + std::string(name);
        return false;
    }
    const fs::path from = P(it->folder);
    std::string before;
    if (!backupMemory("avant restauration de " + std::string(name), &before, why)) return false;
    // Les connaissances : exactement celles de la sauvegarde.
    const fs::path dir = P(knowledgeDir());
    std::error_code ec;
    for (const auto& f : jsonFiles(dir)) fs::remove(f, ec);
    for (const auto& f : jsonFiles(from / "connaissances"))
        if (!copyOne(f, dir / f.filename(), why)) return false;
    for (const std::string& f : {stateFile(), projectsFile()}) {
        const fs::path target = P(f);
        const fs::path src = from / target.filename();
        if (fs::is_regular_file(src, ec) && !copyOne(src, target, why)) return false;
    }
    (void)appendActivity("connaissance", "M\xC3\xA9moire restaur\xC3\xA9" "e depuis " + std::string(name) + " (l'\xC3\xA9tat d'avant : " + before + ")");
    return true;
}

// --------------------------------------------------------- l'ecrivain << assistant >> ---
RememberResult proposeMemory(Registry& reg, const MemoryProposal& p, const Context& ctx) {
    Knowledge k;
    k.content = p.content;
    k.name = p.name;
    k.scope = knownScope(p.scope) ? p.scope : std::string("technique");
    k.subject = p.subject;
    k.category = p.category;
    k.confidence = std::clamp(p.confidence, 0.0, 1.0);
    k.status = "provisoire";
    k.source = "assistant";
    k.conversation = p.conversation;
    k.message = p.message;
    if (k.scope == "projet") {
        if (ctx.projectId.empty()) {
            RememberResult r;
            r.why = "aucun projet enregistr\xC3\xA9 n'est ouvert : un souvenir de projet ne peut pas lui \xC3\xAAtre associ\xC3\xA9";
            return r;
        }
        k.projectId = ctx.projectId;
        k.project = ctx.projectName;
    }
    if (k.scope == "conversation") {
        if (k.conversation.empty()) k.conversation = ctx.conversationId;
        // une decision de conversation reste aussi dans son projet
        if (!ctx.projectId.empty()) {
            k.projectId = ctx.projectId;
            k.project = ctx.projectName;
        }
    }
    return reg.remember(std::move(k));
}

bool validateMemory(Registry& reg, std::string_view id, std::string_view status, std::string* why) {
    const auto* k = reg.find(id);
    if (!k) {
        if (why) *why = "proposition introuvable : " + std::string(id);
        return false;
    }
    if (status != "confirme" && status != "hypothese") {
        if (why) *why = "une proposition se valide comme confirm\xC3\xA9" "e ou comme hypoth\xC3\xA8se";
        return false;
    }
    return reg.setStatus(id, status, why);
}

bool rejectMemory(Registry& reg, std::string_view id, std::string* why) {
    const auto* k = reg.find(id);
    if (!k || k->builtIn) {
        if (why) *why = "proposition introuvable : " + std::string(id);
        return false;
    }
    const std::string name = k->name;
    if (!reg.remove(id, why)) return false;
    (void)appendActivity("suppression", "Proposition de l'Assistant refus\xC3\xA9" "e : " + name);
    return true;
}

// --------------------------------------------------------------- la recherche ---
namespace {
std::string excerptAround(const std::string& text, const ui::SearchQuery& q, std::size_t width) {
    const auto ranges = q.ranges(text);
    std::size_t at = ranges.empty() ? 0 : ranges.front().first;
    std::size_t from = at > width / 3 ? at - width / 3 : 0;
    while (from > 0 && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80) --from;
    std::size_t to = std::min(text.size(), from + width);
    while (to < text.size() && (static_cast<unsigned char>(text[to]) & 0xC0) == 0x80) ++to;
    std::string s = text.substr(from, to - from);
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (from > 0) s = "\xE2\x80\xA6" + s;
    if (to < text.size()) s += "\xE2\x80\xA6";
    return s;
}
} // namespace

std::vector<MemoryHit> searchMemory(const Registry& reg, std::string_view query, const Context& ctx, const MemoryFilter& filter) {
    std::vector<MemoryHit> out;
    const ui::SearchQuery q(query);
    for (const auto& k : reg.all()) {
        if (k.builtIn && !filter.builtIns) continue;
        if (!filter.scope.empty() && k.scopeKey() != filter.scope) continue;
        if (!filter.status.empty() && k.status != filter.status) continue;
        if (filter.projectId == "*" && (!k.projectId.empty() || !k.project.empty())) continue;
        if (!filter.projectId.empty() && filter.projectId != "*" && k.projectId != filter.projectId) continue;
        if (filter.pinnedOnly && !k.pinned) continue;
        if (filter.conflictsOnly && !k.conflicted()) continue;
        const Here here = hereState(k, ctx);
        if (filter.hereOnly && here != Here::Active) continue;
        if (!q.empty() && !q.matches({k.name, k.content, k.description, k.subject, k.project})) continue;
        MemoryHit h;
        h.k = &k;
        h.here = here;
        int score = 0;
        if (k.pinned) score += 100;
        if (here == Here::Active) score += 20;
        if (k.status == "confirme") score += 5;
        if (!q.empty()) {
            const int r = q.rank(k.name);
            if (r >= 0) score += 30 - 5 * r;
            h.excerpt = excerptAround(k.content, q, 160);
        } else {
            h.excerpt = excerptAround(k.content, q, 160);
        }
        h.score = score;
        out.push_back(std::move(h));
    }
    std::stable_sort(out.begin(), out.end(), [](const MemoryHit& a, const MemoryHit& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.k->modified > b.k->modified;
    });
    return out;
}

// ------------------------------------------------------------------- les outils ---
std::string rememberToolJson() {
    return "{\"name\": \"proposer_souvenir\", \"description\": \"Propose a l'utilisateur de RETENIR une information utile aux prochaines "
           "conversations : une preference de travail, une convention du projet, une decision, une solution validee, une tache en "
           "attente. La proposition reste PROVISOIRE : elle n'est gardee et reutilisee qu'apres sa validation par l'utilisateur, dans "
           "le gestionnaire de memoire. Ne propose que ce que l'utilisateur a dit ou confirme ; n'invente rien. Tu n'es pas reentraine : "
           "ces souvenirs sont relus sur son PC.\", \"input_schema\": {\"type\": \"object\", \"properties\": {"
           "\"contenu\": {\"type\": \"string\", \"description\": \"ce qu'il faut retenir, en une ou quelques phrases\"}, "
           "\"titre\": {\"type\": \"string\", \"description\": \"un titre court\"}, "
           "\"portee\": {\"type\": \"string\", \"enum\": [\"utilisateur\", \"projet\", \"conversation\", \"technique\"], \"description\": "
           "\"utilisateur : preference generale ; projet : propre au projet ouvert ; conversation : decision ou tache de cette "
           "conversation ; technique : solution reutilisable\"}, "
           "\"sujet\": {\"type\": \"string\", \"description\": \"le sujet (deux informations de meme sujet qui different sont signalees en conflit)\"}, "
           "\"confiance\": {\"type\": \"number\", \"minimum\": 0, \"maximum\": 1}}, "
           "\"required\": [\"contenu\", \"portee\"]}}";
}

std::string searchMemoryToolJson() {
    return "{\"name\": \"chercher_souvenirs\", \"description\": \"Cherche dans la memoire locale de l'utilisateur (ses preferences, les "
           "conventions et decisions du projet ouvert, les solutions techniques retenues). Rend les souvenirs avec leur statut "
           "(confirme, hypothese, a valider), leur provenance et leurs conflits. Si rien ne repond, dis-le : n'invente jamais un "
           "souvenir.\", \"input_schema\": {\"type\": \"object\", \"properties\": {"
           "\"requete\": {\"type\": \"string\", \"description\": \"les mots cherches (vide : tout ce qui vaut ici)\"}, "
           "\"portee\": {\"type\": \"string\", \"enum\": [\"utilisateur\", \"projet\", \"conversation\", \"technique\"]}, "
           "\"autres_projets\": {\"type\": \"boolean\", \"description\": \"inclure les souvenirs des AUTRES projets (seulement si "
           "l'utilisateur le demande, pour comparer)\"}}, \"required\": [\"requete\"]}}";
}

namespace {
std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

std::string clip(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\xE2\x80\xA6";
}

std::string statusWord(const Knowledge& k) {
    if (k.status == "provisoire") return "propos\xC3\xA9, pas encore valid\xC3\xA9";
    if (k.status == "hypothese") return "hypoth\xC3\xA8se";
    return "confirm\xC3\xA9";
}
} // namespace

std::string runRememberTool(Registry& reg, std::string_view inputJson, const Context& ctx, std::string_view conversation, std::string_view message,
                            bool* isError) {
    if (isError) *isError = true;
    Json in;
    std::string why;
    if (!parseJson(inputJson, in, &why) || !in.isObject()) return "Entr\xC3\xA9" "e illisible (" + why + ") : rien n'est propos\xC3\xA9.";
    MemoryProposal p;
    p.content = in.str("contenu");
    p.name = in.str("titre");
    p.scope = in.str("portee");
    p.subject = in.str("sujet");
    p.confidence = std::clamp(in.num("confiance", 0.6), 0.0, 1.0);
    p.conversation = std::string(conversation);
    p.message = std::string(message);
    if (!knownScope(p.scope)) return "Port\xC3\xA9" "e inconnue (" + p.scope + ") : utilisateur, projet, conversation ou technique.";
    const RememberResult r = proposeMemory(reg, p, ctx);
    switch (r.kind) {
        case RememberResult::Kind::Refused: return "Proposition refus\xC3\xA9" "e : " + r.why + ".";
        case RememberResult::Kind::Duplicate: {
            if (isError) *isError = false;
            const auto* k = reg.find(r.id);
            return "D\xC3\xA9j\xC3\xA0 dans la m\xC3\xA9moire : " + guill(k ? k->name : r.id) + " (" + (k ? statusWord(*k) : std::string{}) + ", " + r.id
                   + "). Rien n'a \xC3\xA9t\xC3\xA9 ajout\xC3\xA9.";
        }
        case RememberResult::Kind::Added:
            if (isError) *isError = false;
            return "Proposition enregistr\xC3\xA9" "e (provisoire, " + r.id + ") : l'utilisateur la validera ou la refusera dans le gestionnaire "
                   "de m\xC3\xA9moire ; elle ne sert pas avant. Dis-lui que tu as PROPOS\xC3\x89 de la retenir, pas que tu t'en souviendras.";
        case RememberResult::Kind::Conflict: {
            if (isError) *isError = false;
            std::string list;
            for (const auto& id : r.conflictsWith)
                if (const auto* o = reg.find(id))
                    list += "\n- " + guill(o->name) + " (" + statusWord(*o) + ", " + o->provenance() + ") : " + clip(o->content, 300);
            return "Proposition enregistr\xC3\xA9" "e (provisoire, " + r.id + "), mais elle CONTREDIT ce qui est d\xC3\xA9j\xC3\xA0 retenu :" + list
                   + "\nLes deux sont gard\xC3\xA9" "es. Signale la contradiction \xC3\xA0 l'utilisateur et demande-lui laquelle est juste.";
        }
    }
    return {};
}

std::string runSearchMemoryTool(const Registry& reg, std::string_view inputJson, const Context& ctx, bool* isError) {
    if (isError) *isError = true;
    Json in;
    std::string why;
    if (!parseJson(inputJson, in, &why) || !in.isObject()) return "Entr\xC3\xA9" "e illisible (" + why + ").";
    if (isError) *isError = false;
    MemoryFilter f;
    f.scope = knownScope(in.str("portee")) ? in.str("portee") : std::string{};
    const bool others = in.flag("autres_projets");
    std::vector<MemoryHit> hits;
    for (auto& h : searchMemory(reg, in.str("requete"), ctx, f)) {
        // jamais les souvenirs d'un autre projet ou d'une autre conversation sans qu'on les demande
        if (!others && (h.here == Here::OtherProject || h.here == Here::Unbound)) continue;
        if (h.here == Here::OtherConversation && !others) continue;
        if (h.here == Here::Inactive) continue;
        hits.push_back(h);
        if (hits.size() >= 12) break;
    }
    if (hits.empty())
        return "Aucun souvenir ne r\xC3\xA9pond \xC3\xA0 " + guill(in.str("requete")) + " dans la m\xC3\xA9moire locale"
               + (others ? std::string{} : std::string(" (ce projet et les souvenirs g\xC3\xA9n\xC3\xA9raux)"))
               + ". Dis-le \xC3\xA0 l'utilisateur : n'invente pas de souvenir.";
    std::string out = std::to_string(hits.size()) + (hits.size() > 1 ? " souvenirs" : " souvenir") + " (m\xC3\xA9moire locale ; " + std::string(kNotRetrained) + ") :";
    for (const auto& h : hits) {
        const Knowledge& k = *h.k;
        out += "\n- [" + statusWord(k) + (k.pinned ? ", \xC3\xA9pingl\xC3\xA9" : "") + "] " + guill(k.name) + " (" + scopeLabel(k.scopeKey())
               + (k.project.empty() ? std::string{} : ", projet " + k.project) + ", du " + displayDate(k.added) + ", " + k.provenance()
               + (k.confidence < 0.999 ? ", confiance " + std::to_string(static_cast<int>(k.confidence * 100.0 + 0.5)) + " %" : std::string{}) + ") : "
               + clip(k.content, 600);
        if (k.conflicted()) {
            std::string names;
            for (const auto* o : reg.conflictsOf(k.id)) names += (names.empty() ? "" : ", ") + guill(o->name);
            out += "\n  EN CONFLIT avec " + names + " : demande \xC3\xA0 l'utilisateur laquelle est juste.";
        }
    }
    return out;
}

} // namespace app::ai::memory
