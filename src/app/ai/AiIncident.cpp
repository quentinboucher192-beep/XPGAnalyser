// =============================================================================
//  app/ai/AiIncident.cpp - 1.20.0 : les dossiers d'incident (voir l'en-tete)
// =============================================================================
#include "AiIncident.hpp"

#include "../../core/AtomicFile.hpp"
#include "../../hmi/HmiCrypto.hpp"
#include "../../hmi/HmiVideo.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <system_error>

namespace app::ai::incidents {

namespace fs = std::filesystem;
namespace inc = hmi::incident;

namespace {

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string join(const std::string& a, std::string_view b) { return U(P(a) / P(std::string(b))); }
std::string relOf(const fs::path& root, const fs::path& p) {
    std::string r = U(p.lexically_relative(root));
    for (auto& c : r)
        if (c == '\\') c = '/';
    return r;
}
bool readAll(const std::string& path, std::string& out) { return core::readFileAll(P(path), out); }
std::string hashOf(std::string_view data) { return hmi::toHex(hmi::sha256(data)); }

json::Value strings(const std::vector<std::string>& v) {
    json::Value a = json::Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}
std::vector<std::string> stringsOf(const json::Value& v) {
    std::vector<std::string> out;
    for (const auto& x : v.items()) out.push_back(x.asString());
    return out;
}

json::Value eventJson(const inc::Event& e) {
    json::Value o = json::Value::object();
    o.set("type", std::string(inc::eventKey(e.kind)));
    o.set("libelle", std::string(inc::eventLabel(e.kind)));
    o.set("nom", e.name);
    if (e.alarm != hmi::kNoId) o.set("alarme_id", static_cast<std::int64_t>(e.alarm));
    o.set("horodatage", e.at);
    o.set("t", e.t);
    if (!e.detail.empty()) o.set("detail", e.detail);
    o.set("priorite", e.priority);
    return o;
}
inc::Event eventOf(const json::Value& o) {
    inc::Event e;
    e.kind = inc::eventFromKey(o.str("type"));
    e.name = o.str("nom");
    if (o.has("alarme_id")) e.alarm = static_cast<hmi::Id>(o.integer("alarme_id"));
    e.at = o.str("horodatage");
    e.t = o.num("t");
    e.detail = o.str("detail");
    e.priority = static_cast<int>(o.integer("priorite", 3));
    return e;
}
std::string_view relatedKey(inc::Related::Kind k) {
    switch (k) {
        case inc::Related::Kind::Shared: return "dependance_commune";
        case inc::Related::Kind::Simultaneous: return "simultanee";
        case inc::Related::Kind::Secondary: return "secondaire";
        case inc::Related::Kind::Independent: return "independante";
    }
    return "independante";
}
inc::Related::Kind relatedOf(std::string_view k) {
    if (k == "dependance_commune") return inc::Related::Kind::Shared;
    if (k == "simultanee") return inc::Related::Kind::Simultaneous;
    if (k == "secondaire") return inc::Related::Kind::Secondary;
    return inc::Related::Kind::Independent;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Les fichiers d'un dossier, chemins relatifs, sans les temporaires de l'ecriture atomique.
std::vector<std::string> filesOf(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path root = P(folder);
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = relOf(root, it->path());
        if (rel.size() > 4 && (rel.ends_with(".tmp") || rel.ends_with(".bak"))) continue;
        out.push_back(rel);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::map<std::string, std::string> parseManifest(std::string_view text, std::vector<std::string>* order = nullptr) {
    std::map<std::string, std::string> m;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 67 || line[64] != ' ') continue;
        std::string hash(line.substr(0, 64));
        std::string_view rest = line.substr(65);
        if (!rest.empty() && (rest.front() == ' ' || rest.front() == '*')) rest.remove_prefix(1);
        const std::string path(rest);
        if (!m.count(path) && order) order->push_back(path);
        m[path] = lowerAscii(hash);
    }
    return m;
}

std::string uniqueFolder(const std::string& parent, const std::string& name) {
    std::error_code ec;
    std::string candidate = join(parent, name);
    for (int i = 2; fs::exists(P(candidate), ec) || fs::exists(P(candidate + ".partiel"), ec); ++i)
        candidate = join(parent, name + "-" + std::to_string(i));
    return candidate;
}

int nextNumber(const std::string& folder) {
    int best = -1;
    std::error_code ec;
    for (fs::directory_iterator it(P(folder), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = U(it->path().filename());
        if (n.size() < 3 || !std::isdigit(static_cast<unsigned char>(n[0])) || !std::isdigit(static_cast<unsigned char>(n[1]))
            || !std::isdigit(static_cast<unsigned char>(n[2])))
            continue;
        best = std::max(best, std::stoi(n.substr(0, 3)));
    }
    return best + 1;
}

std::string three(int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%03d", n);
    return buf;
}

std::string slug(std::string_view s) {
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
        else if (!out.empty() && out.back() != '-') out += '-';
        if (out.size() >= 30) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("analyse") : out;
}

} // namespace

// ======================================================================= le JSON ==
json::Value toJson(const inc::Snapshot& s) {
    json::Value o = json::Value::object();
    o.set("format", s.format);
    o.set("genre", "incident XPGAnalyser");
    o.set("id", s.id);
    o.set("cree_le", s.createdAt);
    o.set("logiciel", s.software);
    json::Value pr = json::Value::object();
    pr.set("nom", s.project);
    pr.set("version", s.projectVersion);
    o.set("projet", pr);
    o.set("declencheur", eventJson(s.trigger));
    json::Value a = json::Value::object();
    a.set("connue", s.alarm.known);
    if (s.alarm.id != hmi::kNoId) a.set("id", static_cast<std::int64_t>(s.alarm.id));
    a.set("nom", s.alarm.name);
    a.set("description", s.alarm.description);
    a.set("condition", s.alarm.condition);
    a.set("message", s.alarm.message);
    a.set("groupe", s.alarm.group);
    a.set("categorie", s.alarm.category);
    a.set("consigne", s.alarm.instruction);
    a.set("priorite", s.alarm.priority);
    a.set("delai_ms", s.alarm.delayMs);
    a.set("acquittement", s.alarm.ackRequired);
    o.set("alarme", a);
    o.set("variables_condition", strings(s.conditionVariables));
    json::Value chain = json::Value::array();
    for (const auto& l : s.chain) {
        json::Value x = json::Value::object();
        x.set("genre", l.kind);
        x.set("nom", l.name);
        x.set("relation", l.relation);
        if (!l.where.empty()) x.set("ou", l.where);
        if (!l.detail.empty()) x.set("detail", l.detail);
        x.set("profondeur", l.depth);
        x.set("certitude", std::string(inc::certaintyKey(l.certainty)));
        chain.push(x);
    }
    o.set("chaine", chain);
    json::Value values = json::Value::array();
    for (const auto& v : s.values) {
        json::Value x = json::Value::object();
        x.set("chemin", v.path);
        x.set("disponible", v.available);
        if (v.available) {
            x.set("valeur", v.value);
            if (!v.quality.empty()) x.set("qualite", v.quality);
        }
        values.push(x);
    }
    o.set("valeurs", values);
    o.set("alarmes_actives", strings(s.activeAlarms));
    json::Value tl = json::Value::array();
    for (const auto& e : s.timeline) tl.push(eventJson(e));
    o.set("chronologie", tl);
    json::Value rel = json::Value::array();
    for (const auto& r : s.related) {
        json::Value x = json::Value::object();
        x.set("nom", r.name);
        x.set("lien", std::string(relatedKey(r.kind)));
        x.set("libelle", std::string(inc::relatedLabel(r.kind)));
        x.set("pourquoi", r.why);
        if (!r.at.empty()) x.set("horodatage", r.at);
        x.set("active", r.active);
        rel.push(x);
    }
    o.set("alarmes_liees", rel);
    json::Value hyp = json::Value::array();
    for (const auto& h : s.hypotheses) {
        json::Value x = json::Value::object();
        x.set("texte", h.text);
        x.set("certitude", std::string(inc::certaintyKey(h.certainty)));
        x.set("appuis", strings(h.evidence));
        x.set("a_verifier", strings(h.check));
        hyp.push(x);
    }
    o.set("pistes", hyp);
    o.set("equipements", strings(s.equipmentStates));
    o.set("a_examiner", strings(s.toCheck));
    o.set("non_disponible", strings(s.missing));
    o.set("vue_affichee", s.openView);
    o.set("objets_visibles", strings(s.visibleObjects));
    o.set("resume", s.summary);
    json::Value prev = json::Value::array();
    for (const auto& p : s.previous) {
        json::Value x = json::Value::object();
        x.set("id", p.id);
        x.set("horodatage", p.at);
        x.set("dossier", p.folder);
        x.set("etat", p.state);
        x.set("resume", p.summary);
        prev.push(x);
    }
    json::Value hist = json::Value::object();
    hist.set("precedents", prev);
    o.set("historique", hist);
    return o;
}

bool fromJson(const json::Value& o, inc::Snapshot& s, std::string* why) {
    if (!o.isObject() || !o.has("declencheur")) {
        if (why) *why = "ce n'est pas un incident (pas de d\xC3\xA9" "clencheur)";
        return false;
    }
    s = {};
    s.format = static_cast<int>(o.integer("format", 1));
    s.id = o.str("id");
    s.createdAt = o.str("cree_le");
    s.software = o.str("logiciel");
    s.project = o.at("projet").str("nom");
    s.projectVersion = o.at("projet").str("version");
    s.trigger = eventOf(o.at("declencheur"));
    const auto& a = o.at("alarme");
    s.alarm.known = a.flag("connue");
    if (a.has("id")) s.alarm.id = static_cast<hmi::Id>(a.integer("id"));
    s.alarm.name = a.str("nom");
    s.alarm.description = a.str("description");
    s.alarm.condition = a.str("condition");
    s.alarm.message = a.str("message");
    s.alarm.group = a.str("groupe");
    s.alarm.category = a.str("categorie");
    s.alarm.instruction = a.str("consigne");
    s.alarm.priority = static_cast<int>(a.integer("priorite", 3));
    s.alarm.delayMs = static_cast<int>(a.integer("delai_ms"));
    s.alarm.ackRequired = a.flag("acquittement", true);
    s.conditionVariables = stringsOf(o.at("variables_condition"));
    for (const auto& x : o.at("chaine").items()) {
        inc::Link l;
        l.kind = x.str("genre");
        l.name = x.str("nom");
        l.relation = x.str("relation");
        l.where = x.str("ou");
        l.detail = x.str("detail");
        l.depth = static_cast<int>(x.integer("profondeur", 1));
        l.certainty = inc::certaintyFromKey(x.str("certitude"));
        s.chain.push_back(std::move(l));
    }
    for (const auto& x : o.at("valeurs").items()) {
        inc::ValueAt v;
        v.path = x.str("chemin");
        v.available = x.flag("disponible");
        v.value = x.str("valeur");
        v.quality = x.str("qualite");
        s.values.push_back(std::move(v));
    }
    s.activeAlarms = stringsOf(o.at("alarmes_actives"));
    for (const auto& x : o.at("chronologie").items()) s.timeline.push_back(eventOf(x));
    for (const auto& x : o.at("alarmes_liees").items()) {
        inc::Related r;
        r.name = x.str("nom");
        r.kind = relatedOf(x.str("lien"));
        r.why = x.str("pourquoi");
        r.at = x.str("horodatage");
        r.active = x.flag("active");
        s.related.push_back(std::move(r));
    }
    for (const auto& x : o.at("pistes").items()) {
        inc::Hypothesis h;
        h.text = x.str("texte");
        h.certainty = inc::certaintyFromKey(x.str("certitude"));
        h.evidence = stringsOf(x.at("appuis"));
        h.check = stringsOf(x.at("a_verifier"));
        s.hypotheses.push_back(std::move(h));
    }
    s.equipmentStates = stringsOf(o.at("equipements"));
    s.toCheck = stringsOf(o.at("a_examiner"));
    s.missing = stringsOf(o.at("non_disponible"));
    s.openView = o.str("vue_affichee");
    s.visibleObjects = stringsOf(o.at("objets_visibles"));
    s.summary = o.str("resume");
    for (const auto& x : o.at("historique").at("precedents").items())
        s.previous.push_back({x.str("id"), x.str("horodatage"), x.str("dossier"), x.str("etat"), x.str("resume")});
    return true;
}

json::Value toJson(const Analysis& a) {
    json::Value o = json::Value::object();
    o.set("numero", a.number);
    o.set("genre", a.kind);
    o.set("cree_le", a.createdAt);
    o.set("source", a.source);
    o.set("etat", a.state);
    if (!a.certainty.empty()) o.set("certitude", a.certainty);
    if (!a.contextHash.empty()) o.set("empreinte_contexte", a.contextHash);
    if (!a.error.empty()) o.set("erreur", a.error);
    o.set("texte", a.text);
    return o;
}
bool fromJson(const json::Value& o, Analysis& a) {
    if (!o.isObject()) return false;
    a = {};
    a.number = static_cast<int>(o.integer("numero"));
    a.kind = o.str("genre");
    a.createdAt = o.str("cree_le");
    a.source = o.str("source");
    a.state = o.str("etat");
    a.certainty = o.str("certitude");
    a.contextHash = o.str("empreinte_contexte");
    a.error = o.str("erreur");
    a.text = o.str("texte");
    return true;
}

// ================================================================== l'analyse ==
Analysis initialAnalysis(const inc::Snapshot& s, std::string_view createdAt) {
    Analysis a;
    a.number = 0;
    a.kind = "initiale";
    a.createdAt = std::string(createdAt);
    a.source = "IA int\xC3\xA9gr\xC3\xA9" "e (d\xC3\xA9terministe, sans r\xC3\xA9seau)";
    a.state = "faite";
    a.text = inc::diagnosis(s);
    a.certainty = s.hypotheses.empty() ? std::string{} : std::string(inc::certaintyKey(s.hypotheses.front().certainty));
    return a;
}

std::string analysisSystemPrompt() {
    return "Tu es l'assistant de diagnostic d'XPGAnalyser (IHM Studio). On te donne la fiche d'un incident : une alarme, "
           "la cha\xC3\xAEne de d\xC3\xA9pendances lue dans le projet, les valeurs disponibles, la chronologie et les autres "
           "alarmes. R\xC3\xA8gles :\n"
           "- appuie-toi UNIQUEMENT sur la fiche ; n'invente jamais une variable, un \xC3\xA9quipement, une relation ou une valeur ;\n"
           "- distingue les faits v\xC3\xA9rifi\xC3\xA9s (lus dans le projet), les relations indirectes et les hypoth\xC3\xA8ses ;\n"
           "- l'ordre d'apparition n'est pas une preuve de causalit\xC3\xA9 : dis \xC2\xAB peut expliquer \xC2\xBB, jamais \xC2\xAB a caus\xC3\xA9 \xC2\xBB ;\n"
           "- dis ce qui manque pour conclure ;\n"
           "- XPGAnalyser n'est pas un syst\xC3\xA8me de s\xC3\xA9" "curit\xC3\xA9 : ne donne jamais d'ordre de commande \xC3\xA0 l'installation.\n"
           "R\xC3\xA9ponds en fran\xC3\xA7" "ais, au tutoiement, en markdown court : une phrase de synth\xC3\xA8se, puis les causes "
           "possibles class\xC3\xA9" "es (chacune avec ce qui l'appuie), puis ce qu'il faut v\xC3\xA9rifier en premier. Termine par une "
           "ligne \xC2\xAB Certitude : fait v\xC3\xA9rifi\xC3\xA9 \xC2\xBB, \xC2\xAB Certitude : relation indirecte \xC2\xBB ou "
           "\xC2\xAB Certitude : hypoth\xC3\xA8se \xC2\xBB.";
}

std::string analysisRequest(const inc::Snapshot& s, std::size_t maxBytes, const std::vector<Analysis>& earlier) {
    std::string t = "Analyse cet incident.\n\n";
    std::string context = inc::aiContext(s, maxBytes);
    t += context;
    // approfondir : les analyses deja faites, en bref (le modele les compare, il ne les repete pas)
    std::size_t room = maxBytes > context.size() ? maxBytes - context.size() : 0;
    for (const auto& a : earlier) {
        if (a.state != "faite" || a.number == 0 || room < 200) continue;
        std::string part = "\n\n## Analyse pr\xC3\xA9" "c\xC3\xA9" "dente (" + a.source + ", " + a.createdAt + ")\n" + a.text;
        if (part.size() > room) part.resize(room);
        t += part;
        room -= part.size();
    }
    if (!s.previous.empty()) {
        t += "\n\n## Incidents pr\xC3\xA9" "c\xC3\xA9" "dents de cette alarme\n";
        for (const auto& p : s.previous) t += "- " + p.at + " (" + p.state + ") : " + p.summary + "\n";
    }
    return t;
}

std::string certaintyOf(std::string_view text) {
    const std::string low = lowerAscii(text);
    const std::size_t at = low.rfind("certitude");
    if (at == std::string::npos) return {};
    const std::string tail = low.substr(at, 80);
    if (tail.find("fait") != std::string::npos) return "fait";
    if (tail.find("indirect") != std::string::npos) return "indirect";
    if (tail.find("hypoth") != std::string::npos) return "hypothese";
    return {};
}

std::string contextHash(std::string_view system, std::string_view request, std::string_view model) {
    std::string all;
    all.reserve(system.size() + request.size() + model.size() + 2);
    all.append(system).append("\n").append(model).append("\n").append(request);
    return hashOf(all);
}

std::string newId() {
    static std::atomic<std::uint64_t> counter{0};
    std::random_device rd;
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string seed = std::to_string(now) + "/" + std::to_string(counter.fetch_add(1)) + "/" + std::to_string(rd()) + "/"
                             + std::to_string(rd());
    return hashOf(seed).substr(0, 32);
}

std::string nowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto msPart = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[96];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                  tm.tm_sec, static_cast<int>(msPart));
    return buf;
}

// ======================================================================= l'item ==
std::string Item::equipment() const {
    for (const auto& l : snapshot.chain)
        if (l.kind == "equipement") return l.name;
    return {};
}
const Analysis* Item::latest() const {
    for (auto it = analyses.rbegin(); it != analyses.rend(); ++it)
        if (it->state == "faite") return &*it;
    return nullptr;
}
std::string Item::synthesis() const {
    const Analysis* a = latest();
    if (a && a->number > 0) {
        // la premiere ligne de texte de la reponse du modele
        std::size_t pos = 0;
        while (pos < a->text.size()) {
            std::size_t end = a->text.find('\n', pos);
            if (end == std::string::npos) end = a->text.size();
            std::string line = a->text.substr(pos, end - pos);
            pos = end + 1;
            while (!line.empty() && (line.front() == '#' || line.front() == ' ' || line.front() == '*' || line.front() == '-')) line.erase(0, 1);
            if (line.size() > 3) return line.size() > 220 ? line.substr(0, 220) + "..." : line;
        }
    }
    if (!snapshot.hypotheses.empty()) return snapshot.hypotheses.front().text;
    return snapshot.summary;
}
std::string Item::certainty() const {
    const Analysis* a = latest();
    const std::string key = a ? a->certainty : std::string{};
    if (key.empty()) return snapshot.hypotheses.empty() ? std::string("aucune piste") : std::string(inc::certaintyLabel(snapshot.hypotheses.front().certainty));
    return std::string(inc::certaintyLabel(inc::certaintyFromKey(key)));
}
std::string Item::analysisState() const {
    bool remoteDone = false, failed = false;
    for (const auto& a : analyses) {
        if (a.number == 0) continue;
        if (a.state == "faite") remoteDone = true;
        else failed = true;
    }
    if (remoteDone) return "faite";
    if (failed) return "en attente (\xC3\xA9" "chec de l'IA distante)";
    return analyses.empty() ? std::string("en attente") : std::string("initiale");
}

// =================================================================== le stockage ==
Store::Store(std::string root) : root_(std::move(root)) {}
std::string Store::pendingFolder() const { return join(root_, kPendingFolder); }
std::string Store::validatedFolder() const { return join(root_, kValidatedFolder); }

bool Store::write(const std::string& path, std::string_view content, std::string* why) const {
    if (fail_ && fail_(path)) {
        if (why) *why = "\xC3\xA9" "criture impossible : " + path + " (disque plein)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    const core::Status st = core::writeFileAtomic(P(path), content, how);
    if (!st) {
        if (why) *why = "\xC3\xA9" "criture impossible : " + path + " (" + st.error().message() + ")";
        return false;
    }
    return true;
}

bool Store::appendManifest(const std::string& folder, const std::vector<std::string>& relPaths, std::string* why) const {
    const std::string mp = join(folder, kManifest);
    std::string old;
    (void)readAll(mp, old);
    std::vector<std::string> order;
    const auto known = parseManifest(old, &order);
    std::string add;
    for (const auto& rel : relPaths) {
        if (known.count(rel)) {
            if (why) *why = rel + " a d\xC3\xA9j\xC3\xA0 une empreinte : il ne se r\xC3\xA9" "\xC3\xA9" "crit pas";
            return false;
        }
        std::string data;
        if (!readAll(join(folder, rel), data)) {
            if (why) *why = rel + " illisible apr\xC3\xA8s l'\xC3\xA9" "criture";
            return false;
        }
        add += hashOf(data) + "  " + rel + "\n";
    }
    return write(mp, old + add, why);
}

bool Store::create(const inc::Snapshot& snap, const Capture& capture, const Analysis& initial, std::string& folder, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(pendingFolder()), ec);
    if (ec) {
        if (why) *why = "dossier impossible \xC3\xA0 cr\xC3\xA9" "er : " + pendingFolder() + " (" + ec.message() + ")";
        return false;
    }
    const std::string final = uniqueFolder(pendingFolder(), inc::folderName(snap));
    const std::string part = final + ".partiel";
    fs::create_directories(P(part), ec);
    if (ec) {
        if (why) *why = "dossier impossible \xC3\xA0 cr\xC3\xA9" "er : " + part + " (" + ec.message() + ")";
        return false;
    }
    std::vector<std::string> written;
    json::Value doc = toJson(snap);
    json::Value cap = json::Value::object();
    if (!capture.jpeg.empty()) {
        cap.set("fichier", "capture.jpg");
        cap.set("nature", "capture d'\xC3\xA9" "cran unique (une image, pas une vid\xC3\xA9o)");
        cap.set("largeur", capture.width);
        cap.set("hauteur", capture.height);
        cap.set("horodatage", capture.at);
    } else {
        cap.set("fichier", "");
        cap.set("note", capture.note.empty() ? std::string("aucune image") : capture.note);
    }
    doc.set("capture", cap);
    const auto fail = [&](const std::string& w) {
        if (why) *why = w;
        fs::remove_all(P(part), ec);
        return false;
    };
    std::string w;
    if (!capture.jpeg.empty()) {
        if (!write(join(part, "capture.jpg"), std::string_view(reinterpret_cast<const char*>(capture.jpeg.data()), capture.jpeg.size()), &w))
            return fail(w);
        written.push_back("capture.jpg");
    }
    Analysis a = initial;
    a.number = 0;
    if (!write(join(part, "analyses/000-initiale.json"), json::write(toJson(a)), &w)) return fail(w);
    written.push_back("analyses/000-initiale.json");
    // incident.json en dernier : sa presence dit que le dossier est complet (la reprise apres un arret brutal)
    if (!write(join(part, "incident.json"), json::write(doc), &w)) return fail(w);
    written.insert(written.begin(), "incident.json");
    if (!appendManifest(part, written, &w)) return fail(w);
    fs::rename(P(part), P(final), ec);
    if (ec) return fail("renommage impossible : " + part + " (" + ec.message() + ")");
    folder = final;
    return true;
}

bool Store::addVideo(const std::string& folder, const Video& v, std::string* why) {
    std::error_code ec;
    if (fs::exists(P(join(folder, "video.avi")), ec) || fs::exists(P(join(folder, "video.json")), ec)) {
        if (why) *why = "la vid\xC3\xA9o de cet incident existe d\xC3\xA9j\xC3\xA0 : elle ne se r\xC3\xA9" "\xC3\xA9" "crit pas";
        return false;
    }
    json::Value meta = json::Value::object();
    std::vector<std::string> written;
    std::string w;
    if (v.frames.size() >= 2 && v.width > 0 && v.height > 0 && v.fps > 0) {
        std::vector<hmi::Bytes> frames;
        frames.reserve(v.frames.size());
        for (const auto& f : v.frames) frames.emplace_back(f.begin(), f.end());
        const hmi::Bytes avi = hmi::video::writeMjpegAvi(v.width, v.height, v.fps, frames);
        if (!write(join(folder, "video.avi"), std::string_view(reinterpret_cast<const char*>(avi.data()), avi.size()), &w)) {
            if (why) *why = w;
            return false;
        }
        written.push_back("video.avi");
        meta.set("fichier", "video.avi");
        meta.set("nature", "vid\xC3\xA9o Motion-JPEG (AVI), " + std::to_string(v.frames.size()) + " images");
    } else {
        meta.set("fichier", "");
        meta.set("nature", "pas de vid\xC3\xA9o");
    }
    meta.set("images", static_cast<std::int64_t>(v.frames.size()));
    meta.set("images_par_seconde", v.fps);
    meta.set("avant_s", v.before);
    meta.set("apres_s", v.after);
    meta.set("largeur", v.width);
    meta.set("hauteur", v.height);
    json::Value times = json::Value::array();
    for (const double t : v.times) times.push(t);
    meta.set("instants_s", times);
    if (!v.note.empty()) meta.set("note", v.note);
    if (!write(join(folder, "video.json"), json::write(meta), &w)) {
        if (why) *why = w;
        fs::remove(P(join(folder, "video.avi")), ec);
        return false;
    }
    written.push_back("video.json");
    return appendManifest(folder, written, why);
}

bool Store::addAnalysis(const std::string& folder, Analysis& a, std::string* why) {
    const std::string dir = join(folder, "analyses");
    a.number = std::max(1, nextNumber(dir));
    if (a.kind.empty()) a.kind = "complementaire";
    const std::string rel = "analyses/" + three(a.number) + "-" + (a.state == "faite" ? slug(a.source) : std::string("echec")) + ".json";
    std::error_code ec;
    if (fs::exists(P(join(folder, rel)), ec)) {
        if (why) *why = rel + " existe d\xC3\xA9j\xC3\xA0";
        return false;
    }
    if (!write(join(folder, rel), json::write(toJson(a)), why)) return false;
    return appendManifest(folder, {rel}, why);
}

bool Store::addComment(const std::string& folder, Comment& c, std::string* why) {
    const std::string dir = join(folder, "commentaires");
    c.number = std::max(1, nextNumber(dir));
    const std::string rel = "commentaires/" + three(c.number) + ".json";
    json::Value o = json::Value::object();
    o.set("numero", c.number);
    o.set("cree_le", c.createdAt);
    o.set("auteur", c.author);
    o.set("texte", c.text);
    if (!write(join(folder, rel), json::write(o), why)) return false;
    return appendManifest(folder, {rel}, why);
}

Integrity Store::verify(const std::string& folder) const {
    Integrity r;
    std::string text;
    if (!readAll(join(folder, kManifest), text)) {
        r.why = "pas de liste d'empreintes";
        return r;
    }
    const auto m = parseManifest(text);
    if (m.empty()) {
        r.why = "liste d'empreintes vide";
        return r;
    }
    for (const auto& [rel, hash] : m) {
        std::string data;
        if (!readAll(join(folder, rel), data)) {
            r.missing.push_back(rel);
            continue;
        }
        ++r.checked;
        if (hashOf(data) != hash) r.altered.push_back(rel);
    }
    for (const auto& f : filesOf(folder))
        if (f != kManifest && !m.count(f)) r.extra.push_back(f);
    r.ok = r.missing.empty() && r.altered.empty() && r.extra.empty();
    return r;
}

bool Store::validate(const std::string& folder, const std::string& author, const std::string& note, std::string& validated, std::string* why) {
    std::error_code ec;
    const Integrity before = verify(folder);
    if (!before.ok) {
        if (why)
            *why = "l'incident n'est pas intact, il n'est pas valid\xC3\xA9 : "
                   + (before.why.empty() ? std::to_string(before.missing.size()) + " manquant(s), " + std::to_string(before.altered.size())
                                               + " modifi\xC3\xA9(s), " + std::to_string(before.extra.size()) + " en trop"
                                         : before.why);
        return false;
    }
    fs::create_directories(P(validatedFolder()), ec);
    const std::string name = U(P(folder).filename());
    const std::string final = uniqueFolder(validatedFolder(), name);
    const std::string part = final + ".partiel";
    fs::copy(P(folder), P(part), fs::copy_options::recursive, ec);
    const auto fail = [&](const std::string& w) {
        if (why) *why = w;
        std::error_code e2;
        fs::remove_all(P(part), e2);
        return false;
    };
    if (ec) return fail("copie impossible vers " + part + " (" + ec.message() + ")");
    if (fail_ && fail_(part)) return fail("copie impossible vers " + part + " (disque plein)");
    // la copie verifiee fichier par fichier, avant d'ajouter la validation
    const Integrity copy = verify(part);
    if (!copy.ok) return fail("la copie n'est pas identique \xC3\xA0 l'original : la validation est annul\xC3\xA9" "e");
    std::string manifest;
    (void)readAll(join(part, kManifest), manifest);
    json::Value v = json::Value::object();
    v.set("valide_le", nowStamp());
    v.set("auteur", author);
    v.set("note", note);
    v.set("empreinte_liste", hashOf(manifest));
    v.set("depuis", std::string(kPendingFolder) + "/" + name);
    std::string w;
    if (!write(join(part, "validation.json"), json::write(v), &w)) return fail(w);
    if (!appendManifest(part, {"validation.json"}, &w)) return fail(w);
    fs::rename(P(part), P(final), ec);
    if (ec) return fail("renommage impossible : " + part + " (" + ec.message() + ")");
    fs::remove_all(P(folder), ec);   // apres seulement ; une erreur ici : la reprise retire le doublon
    validated = final;
    return true;
}

bool Store::remove(const std::string& folder, std::string* why) {
    const fs::path f = P(folder).lexically_normal();
    const fs::path pending = P(pendingFolder()).lexically_normal();
    if (f.parent_path() != pending || U(f.filename()).empty()) {
        if (why) *why = "seul un incident en attente se supprime";
        return false;
    }
    std::error_code ec;
    fs::remove_all(f, ec);
    if (ec) {
        if (why) *why = "suppression impossible : " + folder + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

Item Store::read(const std::string& folder) const {
    Item it;
    it.folder = folder;
    it.name = U(P(folder).filename());
    const fs::path parent = P(folder).lexically_normal().parent_path();
    it.state = parent == P(validatedFolder()).lexically_normal() ? State::Validated : State::Pending;
    std::string text, err;
    json::Value doc;
    if (!readAll(join(folder, "incident.json"), text)) {
        it.why = "incident.json absent";
        return it;
    }
    if (!json::parse(text, doc, &err) || !fromJson(doc, it.snapshot, &err)) {
        it.why = "incident.json illisible : " + err;
        return it;
    }
    it.readable = true;
    const auto& cap = doc.at("capture");
    if (!cap.str("fichier").empty()) {
        it.hasCapture = true;
        it.capturePath = join(folder, cap.str("fichier"));
        it.captureWidth = static_cast<int>(cap.integer("largeur"));
        it.captureHeight = static_cast<int>(cap.integer("hauteur"));
    } else {
        it.captureNote = cap.str("note");
    }
    std::string vtext;
    json::Value vdoc;
    if (readAll(join(folder, "video.json"), vtext) && json::parse(vtext, vdoc)) {
        if (!vdoc.str("fichier").empty()) {
            it.hasVideo = true;
            it.videoPath = join(folder, vdoc.str("fichier"));
        }
        it.videoFrames = static_cast<int>(vdoc.integer("images"));
        it.videoFps = vdoc.num("images_par_seconde");
        it.videoSeconds = it.videoFps > 0 ? it.videoFrames / it.videoFps : 0;
        it.videoNote = vdoc.str("note");
    }
    for (const auto& rel : filesOf(folder)) {
        if (rel.rfind("analyses/", 0) == 0 && rel.ends_with(".json")) {
            std::string t;
            json::Value v;
            Analysis a;
            if (readAll(join(folder, rel), t) && json::parse(t, v) && fromJson(v, a)) it.analyses.push_back(std::move(a));
        } else if (rel.rfind("commentaires/", 0) == 0 && rel.ends_with(".json")) {
            std::string t;
            json::Value v;
            if (readAll(join(folder, rel), t) && json::parse(t, v))
                it.comments.push_back({static_cast<int>(v.integer("numero")), v.str("cree_le"), v.str("auteur"), v.str("texte")});
        }
    }
    std::sort(it.analyses.begin(), it.analyses.end(), [](const Analysis& a, const Analysis& b) { return a.number < b.number; });
    std::sort(it.comments.begin(), it.comments.end(), [](const Comment& a, const Comment& b) { return a.number < b.number; });
    std::string vt;
    json::Value val;
    if (readAll(join(folder, "validation.json"), vt) && json::parse(vt, val))
        it.validation = Validation{val.str("valide_le"), val.str("auteur"), val.str("note"), val.str("empreinte_liste"), val.str("depuis")};
    return it;
}

std::vector<Item> Store::list(State state) const {
    std::vector<Item> out;
    const std::string dir = state == State::Pending ? pendingFolder() : validatedFolder();
    std::error_code ec;
    std::vector<std::string> folders;
    for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string n = U(it->path().filename());
        if (n.ends_with(".partiel") || n.empty() || n.front() == '.') continue;
        folders.push_back(U(it->path()));
    }
    std::sort(folders.begin(), folders.end(), std::greater<>());   // les noms commencent par la date : le plus recent d'abord
    for (const auto& f : folders) out.push_back(read(f));
    return out;
}

std::size_t Store::pendingCount() const {
    std::size_t n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(P(pendingFolder()), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = U(it->path().filename());
        if (it->is_directory(ec) && !name.ends_with(".partiel") && !name.empty() && name.front() != '.') ++n;
    }
    return n;
}

std::vector<inc::Snapshot::Previous> Store::history(std::string_view alarm, std::string_view except, std::size_t max) const {
    std::vector<inc::Snapshot::Previous> out;
    const std::string want = lowerAscii(alarm);
    for (const State st : {State::Pending, State::Validated})
        for (const auto& it : list(st)) {
            if (!it.readable || it.snapshot.id == except || lowerAscii(it.snapshot.trigger.name) != want) continue;
            out.push_back({it.snapshot.id, it.snapshot.trigger.at.empty() ? it.snapshot.createdAt : it.snapshot.trigger.at, it.name,
                           st == State::Pending ? "en attente" : "valid\xC3\xA9", it.synthesis()});
        }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.at > b.at; });
    if (out.size() > max) out.resize(max);
    return out;
}

std::vector<std::string> Store::recover() {
    std::vector<std::string> notes;
    std::error_code ec;
    // 1. les copies de validation interrompues : l'original est encore en attente
    for (fs::directory_iterator it(P(validatedFolder()), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = U(it->path().filename());
        if (!n.ends_with(".partiel")) continue;
        std::error_code e2;
        fs::remove_all(it->path(), e2);
        notes.push_back("validation interrompue de " + n.substr(0, n.size() - 8) + " : la copie partielle est retir\xC3\xA9" "e, l'incident reste en attente");
    }
    // 2. les creations interrompues
    ec.clear();
    std::vector<fs::path> parts;
    for (fs::directory_iterator it(P(pendingFolder()), ec), end; !ec && it != end; it.increment(ec))
        if (U(it->path().filename()).ends_with(".partiel")) parts.push_back(it->path());
    for (const auto& p : parts) {
        const std::string n = U(p.filename());
        const std::string base = n.substr(0, n.size() - 8);
        std::string text;
        json::Value doc;
        inc::Snapshot snap;
        std::error_code e2;
        if (readAll(U(p / "incident.json"), text) && json::parse(text, doc) && fromJson(doc, snap)) {
            // complet : les empreintes de ce qui manque, puis le nom final
            std::string old;
            (void)readAll(U(p / std::string(kManifest)), old);
            const auto known = parseManifest(old);
            std::vector<std::string> add;
            for (const auto& f : filesOf(U(p)))
                if (f != kManifest && !known.count(f)) add.push_back(f);
            std::string w;
            if (!add.empty() && !appendManifest(U(p), add, &w)) {
                notes.push_back("reprise impossible de " + base + " : " + w);
                continue;
            }
            // son propre nom (le .partiel est le sien) ; un autre dossier de ce nom : un nom libre
            std::string final = join(pendingFolder(), base);
            if (fs::exists(P(final), e2)) final = uniqueFolder(pendingFolder(), base);
            fs::rename(p, P(final), e2);
            notes.push_back(e2 ? "reprise impossible de " + base + " : " + e2.message()
                               : "incident " + base + " repris apr\xC3\xA8s un arr\xC3\xAAt brutal");
        } else {
            fs::rename(p, P(join(pendingFolder(), "." + base + ".incomplet")), e2);
            notes.push_back("incident " + base + " incomplet (arr\xC3\xAAt brutal avant la fin) : gard\xC3\xA9 \xC3\xA0 part, en ." + base + ".incomplet");
        }
    }
    // 3. une validation finie dont l'original n'a pas ete retire
    std::set<std::string> validatedIds;
    for (const auto& it : list(State::Validated))
        if (it.readable && verify(it.folder).ok) validatedIds.insert(it.snapshot.id);
    for (const auto& it : list(State::Pending))
        if (it.readable && validatedIds.count(it.snapshot.id)) {
            std::error_code e2;
            fs::remove_all(P(it.folder), e2);
            notes.push_back("incident " + it.name + " d\xC3\xA9j\xC3\xA0 valid\xC3\xA9 : le doublon en attente est retir\xC3\xA9");
        }
    return notes;
}

} // namespace app::ai::incidents
