// =============================================================================
//  app/ai/AiKnowledge.cpp - les connaissances de l'IA : le registre
//  (voir AiKnowledge.hpp).
// =============================================================================
#include "AiKnowledge.hpp"
#include "AiMemory.hpp"
#include "AiPaths.hpp"
#include "AiRegistry.hpp"
#include "../Dossiers.hpp"
#include "../hmi/HmiPanels.hpp"           // hmiPaletteFamily, hmiPaletteWords, hmiPropertyLabel
#include "../../core/Version.hpp"
#include "../../hmi/HmiDesign.hpp"        // kindForVariable, viewTemplates
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiRoles.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace app::ai::memory {

namespace fs = std::filesystem;

// ------------------------------------------------------- categories, sources ---
std::string categoryLabel(std::string_view key) {
    for (const auto& c : kCategories)
        if (c.key == key) return std::string(c.label);
    return std::string(key);
}

std::string sourceLabel(std::string_view key) {
    for (const auto& s : kSources)
        if (s.key == key) return std::string(s.label);
    return std::string(key);
}

bool knownCategory(std::string_view key) noexcept {
    for (const auto& c : kCategories)
        if (c.key == key) return true;
    return false;
}

Here hereState(const Knowledge& k, std::string_view project) noexcept {
    if (!k.active) return Here::Inactive;
    if (!k.project.empty() && k.project != project) return Here::OtherProject;
    return Here::Active;
}

std::string hereLabel(Here h) {
    switch (h) {
        case Here::Active: return "Active ici";
        case Here::Inactive: return "Inactive";
        case Here::OtherProject: return "Autre projet";
    }
    return {};
}

// ------------------------------------------------- les connaissances integrees ---
//  Le texte de chacune est CE QUE L'ON ENVERRAIT A L'IA : tire des tables du
//  logiciel, une ligne par element, en francais.
namespace {

std::size_t g_kinds = 0;   // les genres que porte la connaissance des genres

Knowledge builtIn(std::string id, std::string name, std::string description, std::string category, std::string content) {
    Knowledge k;
    k.id = std::move(id);
    k.name = std::move(name);
    k.description = std::move(description);
    k.category = std::move(category);
    k.source = "logiciel";
    k.content = std::move(content);
    k.builtIn = true;
    k.active = true;
    return k;
}

// 1.15.0 : LE REGISTRE DES GENRES (AiRegistry.hpp) les ecrit - le meme texte que celui envoye au
// modele ; le compte des genres connus est celui des genres que le registre decrit.
std::string kindsText(std::size_t& count) {
    count = registry().kinds().size();
    return registry().kindsText();
}

std::string propertiesText() { return registry().propertiesText(); }

std::string actionsText() {
    std::string s = "Actions des objets et des vues : un d\xC3\xA9" "clencheur et une op\xC3\xA9ration, ex\xC3\xA9" "cut\xC3\xA9" "es dans l'ordre de la liste.\n";
    const auto list = [&](const char* title, const auto& items, auto label) {
        s += title;
        bool first = true;
        for (const auto& t : items) {
            s += first ? " " : ", ";
            first = false;
            s += std::string(label(t));
        }
        s += "\n";
    };
    list("D\xC3\xA9" "clencheurs d'un objet :", hmi::kTriggers, [](hmi::Trigger t) { return hmi::triggerLabel(t); });
    list("D\xC3\xA9" "clencheurs d'un tableau :", hmi::kTableTriggers, [](hmi::Trigger t) { return hmi::triggerLabel(t); });
    list("D\xC3\xA9" "clencheurs d'un raccourci de vue :", hmi::kKeyTriggers, [](hmi::Trigger t) { return hmi::triggerLabel(t); });
    list("Op\xC3\xA9rations :", hmi::kOperations, [](hmi::Operation o) { return hmi::operationLabel(o); });
    return s;
}

std::string vocabularyText() {
    std::string s = "Vocabulaire et synonymes : les mots qui trouvent chaque genre dans la recherche de la biblioth\xC3\xA8que d'objets. "
                    "Une ligne par genre : libell\xC3\xA9 (cl\xC3\xA9) : mots.\n";
    for (const auto& info : hmi::kKindInfos) {
        const auto words = hmiPaletteWords(info.kind);
        if (words.empty()) continue;
        s += std::string(info.label) + " (" + std::string(info.key) + ") :";
        bool first = true;
        for (const auto w : words) {
            s += first ? " " : ", ";
            first = false;
            s += std::string(w);
        }
        s += "\n";
    }
    return s;
}

std::string variablesText() {
    std::string s = "R\xC3\xA8gle de cr\xC3\xA9" "ation : l'objet pos\xC3\xA9 pour une variable gliss\xC3\xA9" "e dans la vue (hmi::design::kindForVariable), "
                    "selon son type.\n";
    struct Sample {
        const char* type;
        const char* what;
        bool        structure;
    };
    const Sample samples[] = {
        {"BOOL", "un bool\xC3\xA9" "en", false}, {"INT", "un entier", false},   {"DINT", "un entier long", false},
        {"REAL", "un r\xC3\xA9" "el", false},   {"TIME", "une dur\xC3\xA9" "e", false}, {"STRING", "un texte", false},
        {"T_ANA", "une structure (DDT, DFB)", true},
    };
    for (const auto& sm : samples) {
        hmi::design::VarInfo v;
        v.name = "Exemple";
        v.type = sm.type;
        if (sm.structure) v.members.push_back({"mes", "REAL", "mesure", true, {}});
        const hmi::Kind k = hmi::design::kindForVariable(v);
        s += std::string(sm.type) + " (" + sm.what + ") -> " + std::string(hmi::kindLabel(k)) + " (" + std::string(hmi::kindKey(k)) + ")";
        if (sm.structure) s += " qui ouvre la popup d'\xC3\xA9quipement du type (" + hmi::design::equipmentPopupName(sm.type) + "), param\xC3\xA8tre Equipement";
        s += "\n";
    }
    return s;
}

std::string templatesText() {
    std::string s = "Mod\xC3\xA8les de vues : \xC2\xAB Nouvelle vue \xC2\xBB part d'un de ces mod\xC3\xA8les (hmi::design::viewTemplates). "
                    "Une ligne par mod\xC3\xA8le : cl\xC3\xA9 | libell\xC3\xA9 | r\xC3\xB4le impos\xC3\xA9 | description.\n";
    for (const auto& t : hmi::design::viewTemplates())
        s += std::string(t.key) + " | " + std::string(t.label) + " | " + (t.role.empty() ? std::string("au choix") : std::string(t.role)) + " | "
             + std::string(t.description) + "\n";
    return s;
}

} // namespace

const std::vector<Knowledge>& builtIns() {
    static const std::vector<Knowledge> all = [] {
        std::vector<Knowledge> v;
        std::size_t kinds = 0;
        std::string kindsContent = kindsText(kinds);
        g_kinds = kinds;
        v.push_back(builtIn(std::string(kKindsKnowledge), "Genres d'objets IHM",
                            "Les " + std::to_string(kinds) + " genres d'objets des vues : cl\xC3\xA9, libell\xC3\xA9, famille de la biblioth\xC3\xA8que (le registre des genres).",
                            "objets", std::move(kindsContent)));
        v.push_back(builtIn("logiciel.proprietes", "Propri\xC3\xA9t\xC3\xA9s de chaque genre",
                            "Les propri\xC3\xA9t\xC3\xA9s pos\xC3\xA9" "es par la cr\xC3\xA9" "ation par d\xC3\xA9" "faut, leur libell\xC3\xA9 et leur r\xC3\xB4le.",
                            "proprietes", propertiesText()));
        v.push_back(builtIn("logiciel.actions", "Actions des objets",
                            "Les d\xC3\xA9" "clencheurs et les op\xC3\xA9rations des actions (objets, tableaux, raccourcis).", "proprietes", actionsText()));
        v.push_back(builtIn("logiciel.vocabulaire", "Vocabulaire et synonymes",
                            "Les mots-cl\xC3\xA9s de recherche de la biblioth\xC3\xA8que d'objets : synonymes et termes industriels.",
                            "vocabulaire", vocabularyText()));
        v.push_back(builtIn("logiciel.variables", "Objet pos\xC3\xA9 pour une variable",
                            "R\xC3\xA8gle de cr\xC3\xA9" "ation : le genre d'objet qui montre une variable selon son type.", "regles", variablesText()));
        v.push_back(builtIn("logiciel.modeles", "Mod\xC3\xA8les de vues et de popups",
                            "R\xC3\xA8gles de cr\xC3\xA9" "ation des synoptiques : les mod\xC3\xA8les de \xC2\xAB Nouvelle vue \xC2\xBB.", "regles",
                            templatesText()));
        return v;
    }();
    return all;
}

// ------------------------------------------------------------------ le registre ---
void Registry::load() {
    items_.clear();
    problems_.clear();
    const State st = readState();
    for (Knowledge k : builtIns()) {
        k.active = std::find(st.builtInOff.begin(), st.builtInOff.end(), k.id) == st.builtInOff.end();
        items_.push_back(std::move(k));
    }
    const std::string dir = knowledgeDir();
    if (dir.empty()) return;
    std::vector<Knowledge> added;
    std::error_code ec;
    for (fs::directory_iterator it(dossiers::cheminDe(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || dossiers::utf8De(it->path().extension()) != ".json") continue;
        Knowledge k;
        k.file = dossiers::utf8De(it->path());
        std::string text, why;
        if (!readText(k.file, text) || !parseJson(text, k.raw, &why) || !k.raw.isObject()) {
            problems_.push_back(dossiers::utf8De(it->path().filename()) + " : " + (why.empty() ? std::string("illisible") : why));
            continue;
        }
        const Json& j = k.raw;
        k.id = j.str("id", dossiers::utf8De(it->path().stem()));
        k.name = j.str("nom", k.id);
        k.description = j.str("description");
        k.category = j.str("categorie", "metier");
        k.source = j.str("source", "utilisateur");
        k.project = j.str("projet");
        k.active = j.flag("actif", true);
        k.added = j.str("ajoute");
        k.modified = j.str("modifie", k.added);
        k.content = j.str("contenu");
        added.push_back(std::move(k));
    }
    std::stable_sort(added.begin(), added.end(), [](const Knowledge& a, const Knowledge& b) { return a.added > b.added; });
    for (auto& k : added) items_.push_back(std::move(k));
}

const Knowledge* Registry::find(std::string_view id) const noexcept {
    for (const auto& k : items_)
        if (k.id == id) return &k;
    return nullptr;
}

bool Registry::write(Knowledge& k, std::string* why) {
    Json j = k.raw.isObject() ? k.raw : Json::makeObject();
    j.set("format", Json::makeNumber(1));
    j.set("id", Json::makeString(k.id));
    j.set("nom", Json::makeString(k.name));
    j.set("description", Json::makeString(k.description));
    j.set("categorie", Json::makeString(k.category));
    j.set("source", Json::makeString(k.source));
    j.set("projet", Json::makeString(k.project));
    j.set("actif", Json::makeBool(k.active));
    j.set("ajoute", Json::makeString(k.added));
    j.set("modifie", Json::makeString(k.modified));
    j.set("contenu", Json::makeString(k.content));
    if (!writeText(k.file, writeJson(j, true), why)) return false;
    k.raw = std::move(j);
    return true;
}

bool Registry::add(Knowledge& k, std::string* why) {
    const std::string dir = knowledgeDir();
    if (dir.empty()) {
        if (why) *why = "aucun dossier IA";
        return false;
    }
    if (k.name.empty()) {
        if (why) *why = "une connaissance a un nom";
        return false;
    }
    const std::string now = nowIso();
    // k-AAAAMMJJ-HHMMSS-n, comme les conversations (c-...)
    std::string stamp = now;
    stamp.erase(std::remove(stamp.begin(), stamp.end(), '-'), stamp.end());
    stamp.erase(std::remove(stamp.begin(), stamp.end(), ':'), stamp.end());
    std::replace(stamp.begin(), stamp.end(), 'T', '-');
    std::error_code ec;
    for (int n = 1;; ++n) {
        k.id = "k-" + stamp + "-" + std::to_string(n);
        k.file = detail::join(dir, k.id + ".json");
        if (!find(k.id) && !fs::exists(dossiers::cheminDe(k.file), ec)) break;
    }
    k.builtIn = false;
    k.added = now;
    k.modified = now;
    if (k.source.empty() || k.source == "logiciel") k.source = "utilisateur";
    if (k.category.empty()) k.category = "metier";
    k.raw = Json{};
    if (!write(k, why)) return false;
    items_.push_back(k);
    const bool rule = k.category == "regles";
    const bool file = k.source == "fichier";
    (void)appendActivity(rule ? "regle" : file ? "fichier" : "connaissance",
                         std::string(rule ? "Nouvelle r\xC3\xA8gle enregistr\xC3\xA9" "e : " : file ? "Fichier analys\xC3\xA9 : " : "Nouvelle connaissance ajout\xC3\xA9" "e : ")
                             + k.name + (k.project.empty() ? std::string{} : " (projet " + k.project + ")"));
    return true;
}

bool Registry::update(const Knowledge& k, std::string* why) {
    for (auto& cur : items_) {
        if (cur.id != k.id) continue;
        if (cur.builtIn) {
            if (why) *why = "int\xC3\xA9gr\xC3\xA9" "e au logiciel : elle ne se modifie pas (elle se d\xC3\xA9sactive)";
            return false;
        }
        if (k.name.empty()) {
            if (why) *why = "une connaissance a un nom";
            return false;
        }
        Knowledge next = cur;
        next.name = k.name;
        next.description = k.description;
        next.category = k.category.empty() ? cur.category : k.category;
        next.project = k.project;
        next.content = k.content;
        next.active = k.active;
        next.modified = nowIso();
        if (!write(next, why)) return false;
        cur = std::move(next);
        (void)appendActivity(cur.category == "regles" ? "regle" : "connaissance",
                             std::string(cur.category == "regles" ? "R\xC3\xA8gle actualis\xC3\xA9" "e : " : "Connaissance actualis\xC3\xA9" "e : ") + cur.name);
        return true;
    }
    if (why) *why = "connaissance introuvable : " + k.id;
    return false;
}

bool Registry::setActive(std::string_view id, bool on, std::string* why) {
    for (auto& cur : items_) {
        if (cur.id != id) continue;
        if (cur.active == on) return true;
        if (cur.builtIn) {
            State st = readState();
            auto& off = st.builtInOff;
            off.erase(std::remove(off.begin(), off.end(), cur.id), off.end());
            if (!on) off.push_back(cur.id);
            if (!writeState(st)) {
                if (why) *why = "memoire.json ne s'\xC3\xA9" "crit pas";
                return false;
            }
            cur.active = on;
        } else {
            Knowledge next = cur;
            next.active = on;
            if (!write(next, why)) return false;
            cur = std::move(next);
        }
        (void)appendActivity("connaissance", std::string(on ? "Connaissance activ\xC3\xA9" "e : " : "Connaissance d\xC3\xA9sactiv\xC3\xA9" "e : ") + cur.name);
        return true;
    }
    if (why) *why = "connaissance introuvable : " + std::string(id);
    return false;
}

bool Registry::remove(std::string_view id, std::string* why) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if (it->id != id) continue;
        if (it->builtIn) {
            if (why) *why = "int\xC3\xA9gr\xC3\xA9" "e au logiciel : elle se d\xC3\xA9sactive, elle ne se supprime pas";
            return false;
        }
        std::error_code ec;
        fs::remove(dossiers::cheminDe(it->file), ec);
        if (ec) {
            if (why) *why = ec.message();
            return false;
        }
        const std::string name = it->name;
        items_.erase(it);
        (void)appendActivity("suppression", "Connaissance supprim\xC3\xA9" "e : " + name);
        return true;
    }
    if (why) *why = "connaissance introuvable : " + std::string(id);
    return false;
}

std::size_t Registry::removeProject(std::string_view project) {
    if (project.empty()) return 0;
    std::size_t n = 0;
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->builtIn || it->project != project) {
            ++it;
            continue;
        }
        std::error_code ec;
        fs::remove(dossiers::cheminDe(it->file), ec);
        if (ec) {
            ++it;
            continue;
        }
        it = items_.erase(it);
        ++n;
    }
    if (n)
        (void)appendActivity("suppression", "Connaissances du projet " + std::string(project) + " supprim\xC3\xA9" "es (" + std::to_string(n) + ")");
    return n;
}

std::size_t Registry::kindsKnown() const noexcept {
    (void)builtIns();
    const Knowledge* k = find(kKindsKnowledge);
    return k && k->active ? g_kinds : 0;
}

std::size_t Registry::kindsTotal() noexcept { return static_cast<std::size_t>(hmi::Kind::FileExplorer) + 1; }

std::vector<std::string> Registry::projects() const {
    std::vector<std::string> out;
    for (const auto& k : items_)
        if (!k.project.empty() && std::find(out.begin(), out.end(), k.project) == out.end()) out.push_back(k.project);
    std::sort(out.begin(), out.end());
    return out;
}

std::size_t Registry::count(bool activeOnly) const noexcept {
    std::size_t n = 0;
    for (const auto& k : items_)
        if (!activeOnly || k.active) ++n;
    return n;
}

std::uintmax_t Registry::bytes(bool activeOnly) const noexcept {
    std::uintmax_t n = 0;
    for (const auto& k : items_)
        if (!activeOnly || k.active) n += k.size();
    return n;
}

std::uintmax_t Registry::bytesHere(std::string_view project) const noexcept {
    std::uintmax_t n = 0;
    for (const auto& k : items_)
        if (hereState(k, project) == Here::Active) n += k.size();
    return n;
}

} // namespace app::ai::memory
