// =============================================================================
//  app/ai/AiLearning.cpp - voir AiLearning.hpp
// -----------------------------------------------------------------------------
//  LE CHAMP "donnees" d'une connaissance :
//    modeles     : { "cle", "nom", "libelle", "description", "mots": [..],
//                    "membres": [ { "nom", "type", "description" } ],
//                    "corrige": true (un modele du logiciel, corrige) }
//    vocabulaire : { "mot": "bache", "sens": "genre" | "modele" | "ignorer", "cible": "Tank" }
//  Le champ "actif" de la connaissance : le modele (ou le mot) est actif.
// =============================================================================
#include "AiLearning.hpp"

#include "AiDemo.hpp"          // fold
#include "AiKnowledge.hpp"
#include "AiMemory.hpp"
#include "AiMemoryJson.hpp"
#include "AiPaths.hpp"
#include "AiStore.hpp"         // isoDate, now
#include "../Dossiers.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace app::ai::learning {

namespace fs = std::filesystem;
using memory::Json;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Un mot replie, et son singulier simple (pompes -> pompe).
std::string singular(const std::string& f) {
    return f.size() > 3 && (f.back() == 's' || f.back() == 'x') ? f.substr(0, f.size() - 1) : f;
}

// Un morceau d'identifiant de fichier sur : lettres, chiffres ; le reste devient _.
std::string idPart(std::string_view folded) {
    std::string out;
    for (const char c : folded) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out.substr(0, 60);
}

std::string meaningKey(Meaning m) { return m == Meaning::Kind ? "genre" : m == Meaning::Model ? "modele" : "ignorer"; }
Meaning meaningOf(std::string_view s) { return s == "modele" ? Meaning::Model : s == "ignorer" ? Meaning::Ignore : Meaning::Kind; }

// ---- le cache : relu quand le dossier des connaissances change ----
struct Cache {
    std::string                signature;
    std::vector<BusinessModel> models;   // tous (actifs ou non)
    std::vector<WordEntry>     words;    // tous (actifs ou non)
};
std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}
Cache& cache() {
    static Cache c;
    return c;
}
int& revision() {
    static int r = 0;
    return r;
}

// La signature du dossier : chaque fichier, sa taille, sa date - plus les ecritures faites ici.
std::string signature() {
    std::string sig = root() + "#" + std::to_string(revision());
    const std::string dir = knowledgeDir();
    if (dir.empty()) return sig;
    std::error_code ec;
    for (fs::directory_iterator it(dossiers::cheminDe(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        sig += "|" + dossiers::utf8De(it->path().filename()) + ":" + std::to_string(it->file_size(e2)) + ":"
             + std::to_string(static_cast<long long>(it->last_write_time(e2).time_since_epoch().count()));
    }
    return sig;
}

ModelMember memberFrom(const Json& j) { return {j.str("nom"), j.str("type"), j.str("description")}; }

void load(Cache& c) {
    c.models = builtInModels();
    c.words = builtInWords();
    if (root().empty()) return;
    memory::Registry reg;
    reg.load();
    for (const auto& k : reg.all()) {
        if (k.builtIn || !k.raw.isObject()) continue;
        const Json* d = k.raw.get("donnees");
        if (!d || !d->isObject()) continue;
        if (k.category == "modeles") {
            BusinessModel m;
            m.key = lower(d->str("cle"));
            if (m.key.empty()) continue;
            m.name = d->str("nom");
            m.label = d->str("libelle", m.key);
            m.description = d->str("description");
            if (const Json* w = d->get("mots"); w && w->isArray())
                for (const auto& x : w->items)
                    if (x.isString() && !x.text.empty()) m.words.push_back(fold(x.text));
            if (const Json* ms = d->get("membres"); ms && ms->isArray())
                for (const auto& x : ms->items)
                    if (x.isObject() && !x.str("nom").empty()) m.members.push_back(memberFrom(x));
            m.active = k.active;
            m.source = k.source;
            m.added = k.added;
            m.modified = k.modified;
            auto it = std::find_if(c.models.begin(), c.models.end(), [&](const BusinessModel& b) { return b.builtIn && b.key == m.key; });
            if (it != c.models.end()) {
                // la correction d'un modele du logiciel (ou seulement son etat)
                if (d->flag("corrige", false)) {
                    if (!m.name.empty()) it->name = m.name;
                    it->label = m.label;
                    it->description = m.description;
                    if (!m.words.empty()) it->words = m.words;
                    if (!m.members.empty()) it->members = m.members;
                    it->corrected = true;
                }
                it->active = m.active;
                it->modified = m.modified;
                it->added = m.added;
                it->source = d->flag("corrige", false) ? k.source : it->source;
            } else {
                m.builtIn = false;
                c.models.push_back(std::move(m));
            }
        } else if (k.category == "vocabulaire") {
            WordEntry w;
            w.word = d->str("mot");
            if (w.word.empty()) continue;
            w.means = meaningOf(d->str("sens"));
            w.target = d->str("cible");
            w.active = k.active;
            w.source = k.source;
            w.added = k.added;
            w.modified = k.modified;
            // un mot appris passe avant le meme mot du logiciel
            const std::string f = fold(w.word);
            c.words.erase(std::remove_if(c.words.begin(), c.words.end(), [&](const WordEntry& b) { return b.builtIn && fold(b.word) == f; }),
                          c.words.end());
            c.words.push_back(std::move(w));
        }
    }
}

// Lire (sous le verrou) ce qui vaut maintenant.
template <typename F>
auto withCache(F&& f) {
    std::lock_guard<std::mutex> lock(cacheMutex());
    Cache& c = cache();
    const std::string sig = signature();
    if (sig != c.signature) {
        load(c);
        c.signature = sig;
    }
    return f(c);
}

// Ecrire une connaissance structuree (son fichier, l'activite) ; `existing` : celle d'avant (ses dates).
bool writeKnowledge(const std::string& id, const std::string& name, const std::string& category, const std::string& content, Json data,
                    bool active, std::string_view source, std::string* why, const std::string& activity) {
    const std::string dir = knowledgeDir();
    if (dir.empty()) return fail(why, "pas de dossier pour l'Assistant IA");
    std::error_code ec;
    fs::create_directories(dossiers::cheminDe(dir), ec);
    const std::string file = dir + "/" + id + ".json";
    Json j = Json::makeObject();
    std::string added = isoDate(now());
    std::string text;
    if (memory::readText(file, text)) {
        Json old;
        if (memory::parseJson(text, old) && old.isObject()) {
            added = old.str("ajoute", added);
            j = old;   // les champs inconnus restent
        }
    }
    j.set("format", Json::makeNumber(1));
    j.set("id", Json::makeString(id));
    j.set("nom", Json::makeString(name));
    j.set("description", Json::makeString(category == "modeles" ? "Mod\xC3\xA8le m\xC3\xA9tier de l'IA int\xC3\xA9gr\xC3\xA9" "e"
                                                                 : "Vocabulaire de l'IA int\xC3\xA9gr\xC3\xA9" "e"));
    j.set("categorie", Json::makeString(category));
    j.set("source", Json::makeString(std::string(source)));
    j.set("projet", Json::makeString(""));
    j.set("actif", Json::makeBool(active));
    j.set("ajoute", Json::makeString(added));
    j.set("modifie", Json::makeString(isoDate(now())));
    j.set("contenu", Json::makeString(content));
    j.set("donnees", std::move(data));
    std::string err;
    if (!memory::writeText(file, memory::writeJson(j, true), &err)) return fail(why, err.empty() ? "\xC3\xA9" "criture impossible" : err);
    ++revision();
    if (!activity.empty()) (void)memory::appendActivity("connaissance", activity);
    return true;
}

bool removeFile(const std::string& id, std::string* why) {
    const std::string dir = knowledgeDir();
    if (dir.empty()) return fail(why, "pas de dossier pour l'Assistant IA");
    std::error_code ec;
    const fs::path p = dossiers::cheminDe(dir + "/" + id + ".json");
    if (!fs::exists(p, ec)) return fail(why, "rien \xC3\xA0 retirer");
    if (!fs::remove(p, ec) || ec) return fail(why, "suppression impossible");
    ++revision();
    return true;
}

Json modelData(const BusinessModel& m, bool corrected) {
    Json d = Json::makeObject();
    d.set("cle", Json::makeString(m.key));
    d.set("nom", Json::makeString(m.name));
    d.set("libelle", Json::makeString(m.label));
    d.set("description", Json::makeString(m.description));
    Json w = Json::makeArray();
    for (const auto& x : m.words) w.items.push_back(Json::makeString(x));
    d.set("mots", std::move(w));
    Json ms = Json::makeArray();
    for (const auto& x : m.members) {
        Json o = Json::makeObject();
        o.set("nom", Json::makeString(x.name));
        o.set("type", Json::makeString(x.type));
        o.set("description", Json::makeString(x.description));
        ms.items.push_back(std::move(o));
    }
    d.set("membres", std::move(ms));
    if (corrected) d.set("corrige", Json::makeBool(true));
    return d;
}

std::optional<BusinessModel> find(const std::vector<BusinessModel>& all, std::string_view key) {
    for (const auto& m : all)
        if (m.key == key) return m;
    return std::nullopt;
}

} // namespace

// --------------------------------------------------------------------- lire --
std::vector<BusinessModel> models(bool withInactive) {
    return withCache([&](const Cache& c) {
        std::vector<BusinessModel> out;
        for (const auto& m : c.models)
            if (withInactive || m.active) out.push_back(m);
        return out;
    });
}

std::optional<BusinessModel> model(std::string_view key) {
    return withCache([&](const Cache& c) -> std::optional<BusinessModel> {
        for (const auto& m : c.models)
            if (m.key == key && m.active) return m;
        return std::nullopt;
    });
}

std::vector<WordEntry> words(bool withInactive) {
    return withCache([&](const Cache& c) {
        std::vector<WordEntry> out;
        for (const auto& w : c.words)
            if (withInactive || w.active) out.push_back(w);
        return out;
    });
}

std::string modelForWord(std::string_view folded) {
    const std::string f(folded), s = singular(f);
    return withCache([&](const Cache& c) -> std::string {
        // un mot appris d'abord (un mot qui designe un modele, ou a ignorer)
        for (const auto& w : c.words) {
            if (!w.active || w.builtIn) continue;
            const std::string wf = fold(w.word);
            if (wf != f && wf != s) continue;
            if (w.means == Meaning::Ignore) return {};
            if (w.means == Meaning::Model) {
                for (const auto& m : c.models)
                    if (m.key == w.target && m.active) return m.key;
                return {};
            }
        }
        for (const auto& m : c.models) {
            if (!m.active) continue;
            for (const auto& w : m.words)
                if (w == f || w == s) return m.key;
        }
        return {};
    });
}

bool ignoredWord(std::string_view folded) {
    const std::string f(folded);
    return withCache([&](const Cache& c) {
        for (const auto& w : c.words)
            if (w.active && !w.builtIn && w.means == Meaning::Ignore && fold(w.word) == f) return true;
        return false;
    });
}

std::vector<WordEntry> learnedKindWords() {
    return withCache([&](const Cache& c) {
        std::vector<WordEntry> out;
        for (const auto& w : c.words)
            if (w.active && !w.builtIn && w.means == Meaning::Kind && !w.target.empty()) out.push_back(w);
        return out;
    });
}

void invalidate() {
    std::lock_guard<std::mutex> lock(cacheMutex());
    ++revision();
}

// ----------------------------------------------------------------- textes --
std::string describe(const BusinessModel& m) {
    std::string t = "Mod\xC3\xA8le m\xC3\xA9tier " + m.name + " (" + m.label + ")";
    if (!m.description.empty()) t += " : " + m.description;
    if (!m.words.empty()) {
        t += ". Mots qui l'appellent : ";
        for (std::size_t i = 0; i < m.words.size(); ++i) t += (i ? ", " : "") + m.words[i];
    }
    t += ". Membres : ";
    for (std::size_t i = 0; i < m.members.size(); ++i)
        t += (i ? " ; " : "") + m.members[i].name + " (" + m.members[i].type + ")"
           + (m.members[i].description.empty() ? std::string{} : " - " + m.members[i].description);
    return t + ".";
}

std::string describe(const WordEntry& w) {
    const std::string q = "\xC2\xAB " + w.word + " \xC2\xBB";
    if (w.means == Meaning::Ignore) return q + " est un mot \xC3\xA0 ignorer (il ne nomme rien).";
    if (w.means == Meaning::Model) return q + " d\xC3\xA9signe le mod\xC3\xA8le m\xC3\xA9tier " + w.target + ".";
    std::string label = w.target;
    if (const auto k = hmi::kindFromKey(w.target)) label = std::string(hmi::kindLabel(*k));
    return q + " d\xC3\xA9signe un objet " + label + ".";
}

bool validMemberType(std::string_view type, std::string* why) {
    namespace ty = hmi::types;
    const std::string t = ty::normalized(type);
    if (t.empty()) return fail(why, "un membre a un type");
    ty::Spec s;
    std::string err;
    if (!ty::parseSpec(t, s, &err)) return fail(why, "type illisible : " + std::string(type) + (err.empty() ? std::string{} : " (" + err + ")"));
    if (ty::isElementary(s.element) || hmi::isIdentifier(s.element)) return true;
    return fail(why, "type inconnu : " + std::string(type));
}

// ----------------------------------------------------------------- ecrire --
bool saveModel(const BusinessModel& in, std::string* why, std::string_view source) {
    BusinessModel m = in;
    m.key = lower(fold(m.key));
    m.key.erase(std::remove_if(m.key.begin(), m.key.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '_'; }),
                m.key.end());
    if (m.key.empty()) return fail(why, "un mod\xC3\xA8le a une cl\xC3\xA9 (un mot : compresseur)");
    if (m.name.empty() || !hmi::isIdentifier(m.name)) return fail(why, "le nom du type n'est pas un identifiant : " + m.name);
    if (m.members.empty()) return fail(why, "un mod\xC3\xA8le a au moins un membre");
    for (const auto& x : m.members) {
        if (x.name.empty() || !hmi::isIdentifier(x.name)) return fail(why, "membre : \xC2\xAB " + x.name + " \xC2\xBB n'est pas un nom");
        if (!validMemberType(x.type, why)) return false;
    }
    for (std::size_t i = 0; i < m.members.size(); ++i)
        for (std::size_t j = i + 1; j < m.members.size(); ++j)
            if (lower(m.members[i].name) == lower(m.members[j].name)) return fail(why, "deux membres " + m.members[i].name);
    for (auto& w : m.words) w = fold(w);
    m.words.erase(std::remove(m.words.begin(), m.words.end(), std::string{}), m.words.end());
    if (m.label.empty()) m.label = m.key;
    const auto all = models(true);
    const auto builtIn = std::find_if(builtInModels().begin(), builtInModels().end(), [&](const BusinessModel& b) { return b.key == m.key; });
    const bool isBuiltIn = builtIn != builtInModels().end();
    const auto current = find(all, m.key);
    const bool active = current ? current->active : true;
    const std::string what = isBuiltIn ? "Mod\xC3\xA8le m\xC3\xA9tier corrig\xC3\xA9 : " : current ? "Mod\xC3\xA8le m\xC3\xA9tier modifi\xC3\xA9 : "
                                                                                                  : "Mod\xC3\xA8le m\xC3\xA9tier ajout\xC3\xA9 : ";
    return writeKnowledge("modele." + idPart(m.key), "Mod\xC3\xA8le " + m.name, "modeles", describe(m), modelData(m, isBuiltIn), active, source, why,
                          what + m.name + " (" + std::to_string(m.members.size()) + " membres)");
}

bool revertModel(std::string_view key, std::string* why) {
    const std::string k = lower(key);
    const auto it = std::find_if(builtInModels().begin(), builtInModels().end(), [&](const BusinessModel& b) { return b.key == k; });
    if (it == builtInModels().end()) return fail(why, "ce n'est pas un mod\xC3\xA8le du logiciel");
    if (!removeFile("modele." + idPart(k), why)) return false;
    (void)memory::appendActivity("connaissance", "Mod\xC3\xA8le m\xC3\xA9tier revenu \xC3\xA0 l'original : " + it->name);
    return true;
}

bool setModelActive(std::string_view key, bool on, std::string* why) {
    const std::string k = lower(key);
    const auto cur = find(models(true), k);
    if (!cur) return fail(why, "mod\xC3\xA8le inconnu : " + k);
    if (cur->active == on) return true;
    const bool isBuiltIn = cur->builtIn;
    const bool corrected = cur->corrected;
    if (isBuiltIn && !corrected && on) return revertModel(k, why);   // l'original, actif : plus rien a garder
    return writeKnowledge("modele." + idPart(k), "Mod\xC3\xA8le " + cur->name, "modeles", describe(*cur), modelData(*cur, corrected), on,
                          isBuiltIn && !corrected ? std::string_view("utilisateur") : std::string_view(cur->source), why,
                          std::string(on ? "Mod\xC3\xA8le m\xC3\xA9tier activ\xC3\xA9 : " : "Mod\xC3\xA8le m\xC3\xA9tier d\xC3\xA9sactiv\xC3\xA9 : ")
                              + cur->name);
}

bool removeModel(std::string_view key, std::string* why) {
    const std::string k = lower(key);
    const auto cur = find(models(true), k);
    if (!cur) return fail(why, "mod\xC3\xA8le inconnu : " + k);
    if (cur->builtIn) return fail(why, "un mod\xC3\xA8le du logiciel ne se supprime pas (D\xC3\xA9sactiver, ou Revenir \xC3\xA0 l'original)");
    if (!removeFile("modele." + idPart(k), why)) return false;
    (void)memory::appendActivity("connaissance", "Mod\xC3\xA8le m\xC3\xA9tier supprim\xC3\xA9 : " + cur->name);
    return true;
}

bool learnWord(const WordEntry& in, std::string* why, std::string_view source) {
    WordEntry w = in;
    while (!w.word.empty() && std::isspace(static_cast<unsigned char>(w.word.front()))) w.word.erase(w.word.begin());
    while (!w.word.empty() && std::isspace(static_cast<unsigned char>(w.word.back()))) w.word.pop_back();
    const std::string f = fold(w.word);
    if (f.empty()) return fail(why, "un mot vide");
    if (f.size() > 40) return fail(why, "un mot de 40 lettres au plus");
    if (w.means == Meaning::Kind) {
        const auto k = hmi::kindFromKey(w.target);
        if (!k) return fail(why, "genre d'objet inconnu : " + w.target);
    } else if (w.means == Meaning::Model) {
        if (!find(models(true), lower(w.target))) return fail(why, "mod\xC3\xA8le m\xC3\xA9tier inconnu : " + w.target);
        w.target = lower(w.target);
    } else {
        w.target.clear();
    }
    Json d = Json::makeObject();
    d.set("mot", Json::makeString(w.word));
    d.set("sens", Json::makeString(meaningKey(w.means)));
    d.set("cible", Json::makeString(w.target));
    return writeKnowledge("mot." + idPart(f), "Mot \xC2\xAB " + w.word + " \xC2\xBB", "vocabulaire", describe(w), std::move(d), true, source, why,
                          "Mot appris : " + describe(w));
}

bool setWordActive(std::string_view word, bool on, std::string* why) {
    const std::string f = fold(word);
    for (const auto& w : words(true)) {
        if (w.builtIn || fold(w.word) != f) continue;
        if (w.active == on) return true;
        Json d = Json::makeObject();
        d.set("mot", Json::makeString(w.word));
        d.set("sens", Json::makeString(meaningKey(w.means)));
        d.set("cible", Json::makeString(w.target));
        return writeKnowledge("mot." + idPart(f), "Mot \xC2\xAB " + w.word + " \xC2\xBB", "vocabulaire", describe(w), std::move(d), on, w.source, why,
                              std::string(on ? "Mot activ\xC3\xA9 : " : "Mot d\xC3\xA9sactiv\xC3\xA9 : ") + w.word);
    }
    return fail(why, "mot appris inconnu : " + std::string(word));
}

bool forgetWord(std::string_view word, std::string* why) {
    const std::string f = fold(word);
    if (!removeFile("mot." + idPart(f), why)) return false;
    (void)memory::appendActivity("connaissance", "Mot oubli\xC3\xA9 : " + std::string(word));
    return true;
}

} // namespace app::ai::learning
