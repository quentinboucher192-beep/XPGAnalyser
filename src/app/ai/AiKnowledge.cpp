// =============================================================================
//  app/ai/AiKnowledge.cpp - les connaissances de l'IA : le registre
//  (voir AiKnowledge.hpp).
// =============================================================================
#include "AiKnowledge.hpp"
#include "AiMemory.hpp"
#include "AiMemoryManager.hpp"
#include "AiPaths.hpp"
#include "AiRegistry.hpp"
#include "../Dossiers.hpp"
#include "../hmi/HmiPanels.hpp"           // hmiPaletteFamily, hmiPaletteWords, hmiPropertyLabel
#include "../../core/Version.hpp"
#include "../../hmi/HmiDesign.hpp"        // kindForVariable, viewTemplates
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiRoles.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>

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

// ---------------------------------------------------- 1.21.0 : portees, statuts ---
std::string scopeLabel(std::string_view key) {
    for (const auto& c : kScopes)
        if (c.key == key) return std::string(c.label);
    return std::string(key);
}

std::string statusLabel(std::string_view key) {
    for (const auto& c : kStatuses)
        if (c.key == key) return std::string(c.label);
    return std::string(key);
}

bool knownScope(std::string_view key) noexcept {
    for (const auto& c : kScopes)
        if (c.key == key) return true;
    return false;
}

bool knownStatus(std::string_view key) noexcept {
    for (const auto& c : kStatuses)
        if (c.key == key) return true;
    return false;
}

std::string scopeFor(std::string_view category, std::string_view project) {
    if (!project.empty() || category == "projet") return "projet";
    if (category == "preferences") return "utilisateur";
    return "technique";
}

std::string Knowledge::provenance() const {
    if (builtIn) return "Logiciel (tir\xC3\xA9" "e de ses tables)";
    std::string who = source == "assistant" ? "Propos\xC3\xA9" "e par l'Assistant" : source == "fichier" ? "Tir\xC3\xA9" "e d'un fichier analys\xC3\xA9" : "Saisie par l'utilisateur";
    if (!conversation.empty()) who += " \xE2\x80\x94 conversation " + conversation + (message.empty() ? std::string{} : ", message " + message);
    return who;
}

std::string normalizedText(std::string_view s) {
    const std::string f = ui::foldForSearch(s);
    std::string out;
    out.reserve(f.size());
    bool space = false;
    for (const char ch : f) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c >= 0x80) {
            if (space && !out.empty()) out += ' ';
            space = false;
            out += ch;
        } else {
            space = true;
        }
    }
    return out;
}

void addHistory(Knowledge& k, std::string text) {
    k.history.push_back({nowIso(), std::move(text)});
    constexpr std::size_t kMax = 50;
    if (k.history.size() > kMax) k.history.erase(k.history.begin(), k.history.begin() + static_cast<std::ptrdiff_t>(k.history.size() - kMax));
}

Here hereState(const Knowledge& k, const Context& ctx) noexcept {
    if (!k.active) return Here::Inactive;
    if (k.status == "provisoire") return Here::Pending;
    if (k.scope == "conversation" && (k.conversation.empty() || k.conversation != ctx.conversationId)) return Here::OtherConversation;
    // L'identifiant fiable d'abord : un souvenir de projet ne part qu'avec les demandes de CE projet.
    if (!k.projectId.empty()) return k.projectId == ctx.projectId ? Here::Active : Here::OtherProject;
    if (!k.project.empty()) {
        if (k.project != ctx.projectName) return Here::OtherProject;
        // Lie par le nom seul (avant la 1.21) : pas si deux projets connus portent ce nom.
        return ctx.nameAmbiguous ? Here::Unbound : Here::Active;
    }
    if (k.scope == "projet") return Here::Unbound;   // un souvenir de projet sans projet : a associer
    return Here::Active;
}

Here hereState(const Knowledge& k, std::string_view project) noexcept {
    Context ctx;
    ctx.projectName = std::string(project);
    return hereState(k, ctx);
}

std::string hereLabel(Here h) {
    switch (h) {
        case Here::Active: return "Active ici";
        case Here::Inactive: return "Inactive";
        case Here::OtherProject: return "Autre projet";
        case Here::Pending: return "\xC3\x80 valider";
        case Here::OtherConversation: return "Autre conversation";
        case Here::Unbound: return "Projet \xC3\xA0 associer";
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
        // 1.21.0 : le souvenir ; un fichier du format 1 : confirme, sa portee deduite (la migration le reecrit)
        k.format = static_cast<int>(j.num("format", 1));
        k.scope = j.str("portee");
        if (!knownScope(k.scope)) k.scope = scopeFor(k.category, k.project);
        k.status = j.str("statut", "confirme");
        if (!knownStatus(k.status)) k.status = "confirme";
        k.confidence = std::clamp(j.num("confiance", 1.0), 0.0, 1.0);
        k.pinned = j.flag("epingle");
        k.projectId = j.str("projetId");
        k.conversation = j.str("conversation");
        k.message = j.str("message");
        k.subject = j.str("sujet");
        if (const Json* c = j.get("conflits"); c && c->isArray())
            for (const auto& v : c->items)
                if (v.isString() && !v.text.empty()) k.conflicts.push_back(v.text);
        if (const Json* h = j.get("historique"); h && h->isArray())
            for (const auto& v : h->items)
                if (v.isObject()) k.history.push_back({v.str("date"), v.str("texte")});
        added.push_back(std::move(k));
    }
    std::stable_sort(added.begin(), added.end(), [](const Knowledge& a, const Knowledge& b) { return a.added > b.added; });
    for (auto& k : added) items_.push_back(std::move(k));
    migrate();
}

// 1.21.0 : LA MIGRATION du format 1 au format 2. Une sauvegarde de toute la memoire d'abord (sauvegardes/) ; sans
// elle, rien n'est reecrit (les souvenirs se lisent quand meme, en memoire). Deux fils qui chargent en meme temps : le
// verrou, et le format relu sur le disque, font qu'un seul migre.
void Registry::migrate() {
    std::vector<std::size_t> old;
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (!items_[i].builtIn && items_[i].format < kKnowledgeFormat) old.push_back(i);
    if (old.empty()) return;
    static std::mutex m;
    const std::lock_guard<std::mutex> lock(m);
    // relu sur le disque : un autre fil a peut-etre deja migre
    old.erase(std::remove_if(old.begin(), old.end(),
                             [&](std::size_t i) {
                                 std::string text;
                                 Json j;
                                 return !readText(items_[i].file, text) || !parseJson(text, j) || j.num("format", 1) >= kKnowledgeFormat;
                             }),
              old.end());
    if (old.empty()) return;
    std::string name, why;
    if (!backupMemory("migration du format 1 au format 2", &name, &why)) {
        problems_.push_back("migration remise \xC3\xA0 plus tard : la sauvegarde a \xC3\xA9" "chou\xC3\xA9 (" + why + ")");
        return;
    }
    std::size_t done = 0;
    for (const std::size_t i : old) {
        Knowledge next = items_[i];
        addHistory(next, "Migr\xC3\xA9" "e du format 1 (avant la 1.21) : statut confirm\xC3\xA9, port\xC3\xA9" "e " + scopeLabel(next.scope));
        next.format = kKnowledgeFormat;
        if (write(next, &why)) {
            items_[i] = std::move(next);
            ++done;
        } else {
            problems_.push_back(items_[i].name + " : migration impossible (" + why + ")");
        }
    }
    migration_ = std::to_string(done) + (done > 1 ? " souvenirs migr\xC3\xA9s" : " souvenir migr\xC3\xA9") + " au format 2 (sauvegarde " + name + ")";
    (void)appendActivity("connaissance", "M\xC3\xA9moire migr\xC3\xA9" "e : " + migration_);
}

const Knowledge* Registry::find(std::string_view id) const noexcept {
    for (const auto& k : items_)
        if (k.id == id) return &k;
    return nullptr;
}

bool Registry::write(Knowledge& k, std::string* why) {
    Json j = k.raw.isObject() ? k.raw : Json::makeObject();
    j.set("format", Json::makeNumber(kKnowledgeFormat));
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
    // 1.21.0 : le souvenir
    if (k.scope.empty()) k.scope = scopeFor(k.category, k.project);
    j.set("portee", Json::makeString(k.scope));
    j.set("statut", Json::makeString(k.status.empty() ? std::string("confirme") : k.status));
    j.set("confiance", Json::makeNumber(std::clamp(k.confidence, 0.0, 1.0)));
    j.set("epingle", Json::makeBool(k.pinned));
    j.set("projetId", Json::makeString(k.projectId));
    j.set("conversation", Json::makeString(k.conversation));
    j.set("message", Json::makeString(k.message));
    j.set("sujet", Json::makeString(k.subject));
    Json conflicts = Json::makeArray();
    for (const auto& c : k.conflicts) conflicts.items.push_back(Json::makeString(c));
    j.set("conflits", std::move(conflicts));
    Json history = Json::makeArray();
    for (const auto& h : k.history) {
        Json line = Json::makeObject();
        line.set("date", Json::makeString(h.date));
        line.set("texte", Json::makeString(h.text));
        history.items.push_back(std::move(line));
    }
    j.set("historique", std::move(history));
    if (!writeText(k.file, writeJson(j, true), why)) return false;
    k.raw = std::move(j);
    k.format = kKnowledgeFormat;
    return true;
}

Knowledge* Registry::mut(std::string_view id) noexcept {
    for (auto& k : items_)
        if (k.id == id) return &k;
    return nullptr;
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
    // 1.21.0 : la portee, le statut, la provenance
    if (!knownScope(k.scope)) k.scope = scopeFor(k.category, k.project);
    if (!knownStatus(k.status)) k.status = "confirme";
    k.confidence = std::clamp(k.confidence, 0.0, 1.0);
    {
        // la creation d'abord (ce que l'appelant a deja note - un conflit - vient apres)
        std::vector<HistoryLine> before = std::move(k.history);
        k.history.clear();
        addHistory(k, (k.source == "assistant" ? std::string("Propos\xC3\xA9" "e par l'Assistant") : std::string("Enregistr\xC3\xA9" "e"))
                          + " (" + statusLabel(k.status) + ", port\xC3\xA9" "e " + scopeLabel(k.scope)
                          + (k.conversation.empty() ? std::string{} : ", conversation " + k.conversation) + ")");
        for (auto& h : before) k.history.push_back(std::move(h));
    }
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
        // 1.21.0 : le souvenir (ce que la fenetre modifie aussi)
        // la portee : celle qu'on donne ; sinon, un projet change la rededuit (une portee conversation reste)
        if (knownScope(k.scope) && k.scope != cur.scope) next.scope = k.scope;
        else if (k.project != cur.project && cur.scope != "conversation") next.scope = scopeFor(next.category, next.project);
        if (knownStatus(k.status)) next.status = k.status;
        next.confidence = std::clamp(k.confidence, 0.0, 1.0);
        next.pinned = k.pinned;
        // l'identifiant suit ce qui l'a change ; un projet renomme a la main (le nom seul) : lie par le nom de nouveau
        if (k.projectId != cur.projectId) next.projectId = k.projectId;
        else if (k.project != cur.project) next.projectId.clear();
        next.subject = k.subject;
        {
            std::string what;
            if (next.content != cur.content) what += "le contenu";
            if (next.name != cur.name) what += std::string(what.empty() ? "" : ", ") + "le nom";
            if (next.project != cur.project || next.projectId != cur.projectId) what += std::string(what.empty() ? "" : ", ") + "le projet";
            if (next.status != cur.status) what += std::string(what.empty() ? "" : ", ") + "le statut (" + statusLabel(next.status) + ")";
            if (next.scope != cur.scope) what += std::string(what.empty() ? "" : ", ") + "la port\xC3\xA9" "e (" + scopeLabel(next.scope) + ")";
            if (!what.empty()) addHistory(next, "Modifi\xC3\xA9" "e : " + what);
        }
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
        const std::string name = it->name, gone = it->id;
        items_.erase(it);
        // 1.21.0 : ceux qu'elle contredisait ne sont plus en conflit avec elle
        for (auto& o : items_) {
            if (o.builtIn || std::find(o.conflicts.begin(), o.conflicts.end(), gone) == o.conflicts.end()) continue;
            Knowledge next = o;
            next.conflicts.erase(std::remove(next.conflicts.begin(), next.conflicts.end(), gone), next.conflicts.end());
            addHistory(next, "Le souvenir qui la contredisait (" + name + ") a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9");
            std::string w;
            if (write(next, &w)) o = std::move(next);
        }
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

// ---------------------------------------------------------- 1.21.0 : les souvenirs ---
namespace {
// Le meme << lieu >> : meme portee, meme projet (l'identifiant, sinon le nom), meme conversation (portee conversation).
bool samePlace(const Knowledge& a, const Knowledge& b) {
    if (a.builtIn || b.builtIn) return false;
    if (a.scopeKey() != b.scopeKey()) return false;
    if (!a.projectId.empty() || !b.projectId.empty()) {
        if (a.projectId != b.projectId) return false;
    } else if (a.project != b.project) {
        return false;
    }
    if (a.scopeKey() == "conversation" && a.conversation != b.conversation) return false;
    return true;
}
std::string subjectKeyOf(const Knowledge& k) { return normalizedText(k.subject.empty() ? k.name : k.subject); }
std::string firstLine(const std::string& s, std::size_t max) {
    std::size_t e = s.find('\n');
    std::string l = s.substr(0, e == std::string::npos ? s.size() : e);
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    if (l.size() > max) {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(l[cut]) & 0xC0) == 0x80) --cut;
        l = l.substr(0, cut) + "\xE2\x80\xA6";
    }
    return l;
}
} // namespace

RememberResult Registry::remember(Knowledge k) {
    RememberResult r;
    const auto refuse = [&](std::string why) {
        r.kind = RememberResult::Kind::Refused;
        r.why = std::move(why);
        return r;
    };
    if (k.content.find_first_not_of(" \t\r\n") == std::string::npos) return refuse("un souvenir a un contenu");
    if (k.name.empty()) k.name = firstLine(k.content, 70);
    if (k.category.empty()) k.category = k.scope == "utilisateur" ? "preferences" : k.scope == "projet" ? "projet" : "metier";
    if (!knownScope(k.scope)) k.scope = scopeFor(k.category, k.project);
    if (!knownStatus(k.status)) k.status = "confirme";
    if (k.scope == "projet" && k.projectId.empty() && k.project.empty())
        return refuse("un souvenir de projet est associ\xC3\xA9 \xC3\xA0 un projet (ouvre-le, ou choisis-le)");
    if (k.scope == "conversation" && k.conversation.empty()) return refuse("un souvenir de conversation dit sa conversation");
    // Une solution propre a un projet ne devient pas une regle generale d'elle-meme : un souvenir qui porte un projet le
    // garde, quelle que soit sa portee (seule l'association, demandee, l'en libere).
    const std::string body = normalizedText(k.content);
    const std::string subject = subjectKeyOf(k);
    std::vector<std::string> contradicted;
    for (const auto& cur : items_) {
        if (!samePlace(cur, k)) continue;
        if (normalizedText(cur.content) == body) {   // le meme contenu : un doublon
            r.kind = RememberResult::Kind::Duplicate;
            r.id = cur.id;
            if (k.status == "confirme" && cur.status != "confirme") (void)setStatus(cur.id, "confirme");
            (void)appendActivity("connaissance", "Souvenir d\xC3\xA9j\xC3\xA0 connu, pas ajout\xC3\xA9 une seconde fois : " + cur.name);
            return r;
        }
        if (!subject.empty() && subjectKeyOf(cur) == subject) contradicted.push_back(cur.id);
    }
    if (!contradicted.empty()) {
        k.conflicts = contradicted;
        std::string names;
        for (const auto& id : contradicted)
            if (const auto* o = find(id)) names += (names.empty() ? "" : ", ") + o->name + " (" + o->id + ")";
        addHistory(k, "En conflit avec " + names + " : les deux sont gard\xC3\xA9" "es, rien n'est \xC3\xA9" "cras\xC3\xA9");
    }
    std::string why;
    if (!add(k, &why)) return refuse(why);
    r.id = k.id;
    if (contradicted.empty()) {
        r.kind = RememberResult::Kind::Added;
        return r;
    }
    r.kind = RememberResult::Kind::Conflict;
    r.conflictsWith = contradicted;
    for (const auto& id : contradicted) {
        Knowledge* o = mut(id);
        if (!o) continue;
        Knowledge next = *o;
        if (std::find(next.conflicts.begin(), next.conflicts.end(), k.id) == next.conflicts.end()) next.conflicts.push_back(k.id);
        addHistory(next, "Contredite par " + k.name + " (" + k.id + ", " + k.provenance() + ")");
        if (write(next, &why)) *o = std::move(next);
    }
    (void)appendActivity("connaissance", "Conflit signal\xC3\xA9 : " + k.name + " contredit un souvenir existant (les deux sont gard\xC3\xA9" "s)");
    return r;
}

bool Registry::setPinned(std::string_view id, bool on, std::string* why) {
    Knowledge* k = mut(id);
    if (!k || k->builtIn) {
        if (why) *why = k ? "une connaissance int\xC3\xA9gr\xC3\xA9" "e ne s'\xC3\xA9pingle pas" : "souvenir introuvable : " + std::string(id);
        return false;
    }
    if (k->pinned == on) return true;
    Knowledge next = *k;
    next.pinned = on;
    addHistory(next, on ? "\xC3\x89pingl\xC3\xA9" "e" : "D\xC3\xA9s\xC3\xA9pingl\xC3\xA9" "e");
    if (!write(next, why)) return false;
    *k = std::move(next);
    (void)appendActivity("connaissance", std::string(on ? "Souvenir \xC3\xA9pingl\xC3\xA9 : " : "Souvenir d\xC3\xA9s\xC3\xA9pingl\xC3\xA9 : ") + k->name);
    return true;
}

bool Registry::setStatus(std::string_view id, std::string_view status, std::string* why) {
    Knowledge* k = mut(id);
    if (!k || k->builtIn || !knownStatus(status)) {
        if (why) *why = !k ? "souvenir introuvable : " + std::string(id) : k->builtIn ? "une connaissance int\xC3\xA9gr\xC3\xA9" "e n'a pas de statut" : "statut inconnu : " + std::string(status);
        return false;
    }
    if (k->status == status) return true;
    Knowledge next = *k;
    const std::string before = next.status;
    next.status = std::string(status);
    next.modified = nowIso();
    addHistory(next, "Statut : " + statusLabel(before) + " \xE2\x86\x92 " + statusLabel(next.status));
    if (!write(next, why)) return false;
    *k = std::move(next);
    (void)appendActivity("connaissance", "Souvenir " + statusLabel(k->status) + " : " + k->name);
    return true;
}

bool Registry::setConfidence(std::string_view id, double confidence, std::string* why) {
    Knowledge* k = mut(id);
    if (!k || k->builtIn) {
        if (why) *why = "souvenir introuvable : " + std::string(id);
        return false;
    }
    Knowledge next = *k;
    next.confidence = std::clamp(confidence, 0.0, 1.0);
    if (next.confidence == k->confidence) return true;
    addHistory(next, "Confiance : " + std::to_string(static_cast<int>(next.confidence * 100.0 + 0.5)) + " %");
    if (!write(next, why)) return false;
    *k = std::move(next);
    return true;
}

bool Registry::associate(std::string_view id, const std::string& projectId, const std::string& projectName, std::string* why) {
    Knowledge* k = mut(id);
    if (!k || k->builtIn) {
        if (why) *why = !k ? "souvenir introuvable : " + std::string(id) : "une connaissance int\xC3\xA9gr\xC3\xA9" "e vaut pour tous les projets";
        return false;
    }
    Knowledge next = *k;
    next.projectId = projectId;
    next.project = projectName;
    if (projectId.empty() && projectName.empty()) {
        if (next.scope == "projet") next.scope = "technique";
        addHistory(next, "Associ\xC3\xA9" "e \xC3\xA0 tous les projets (port\xC3\xA9" "e " + scopeLabel(next.scope) + ")");
    } else {
        if (next.scope != "conversation") next.scope = "projet";
        addHistory(next, "Associ\xC3\xA9" "e au projet " + projectName + (projectId.empty() ? std::string{} : " (" + projectId + ")"));
    }
    next.modified = nowIso();
    if (!write(next, why)) return false;
    *k = std::move(next);
    (void)appendActivity("connaissance", "Souvenir associ\xC3\xA9 : " + k->name + " \xE2\x86\x92 " + (projectName.empty() ? std::string("tous les projets") : "projet " + projectName));
    return true;
}

bool Registry::resolveConflict(std::string_view keep, std::string_view other, bool removeOther, std::string* why) {
    Knowledge* a = mut(keep);
    Knowledge* b = mut(other);
    if (!a || !b || a == b) {
        if (why) *why = "souvenirs introuvables";
        return false;
    }
    const std::string otherId(other), keepId(keep), otherName = b->name;
    Knowledge na = *a;
    na.conflicts.erase(std::remove(na.conflicts.begin(), na.conflicts.end(), otherId), na.conflicts.end());
    na.status = "confirme";
    addHistory(na, "Conflit r\xC3\xA9solu : gard\xC3\xA9" "e, " + otherName + (removeOther ? " supprim\xC3\xA9" "e" : " retir\xC3\xA9" "e"));
    if (!write(na, why)) return false;
    *a = std::move(na);
    if (removeOther) return remove(otherId, why);
    b = mut(otherId);
    Knowledge nb = *b;
    nb.conflicts.erase(std::remove(nb.conflicts.begin(), nb.conflicts.end(), keepId), nb.conflicts.end());
    nb.active = false;
    addHistory(nb, "Remplac\xC3\xA9" "e par " + a->name + " (" + keepId + ") : d\xC3\xA9sactiv\xC3\xA9" "e, gard\xC3\xA9" "e pour m\xC3\xA9moire");
    if (!write(nb, why)) return false;
    *b = std::move(nb);
    (void)appendActivity("connaissance", "Conflit r\xC3\xA9solu : " + a->name + " gard\xC3\xA9" "e, " + otherName + " d\xC3\xA9sactiv\xC3\xA9" "e");
    return true;
}

std::size_t Registry::bindLegacy(const Context& ctx) {
    if (ctx.projectId.empty() || ctx.projectName.empty() || ctx.nameAmbiguous) return 0;
    std::size_t n = 0;
    for (auto& k : items_) {
        if (k.builtIn || !k.projectId.empty() || k.project != ctx.projectName) continue;
        Knowledge next = k;
        next.projectId = ctx.projectId;
        addHistory(next, "Li\xC3\xA9" "e au projet " + ctx.projectName + " par son identifiant (" + ctx.projectId + ")");
        std::string why;
        if (!write(next, &why)) continue;
        k = std::move(next);
        ++n;
    }
    if (n)
        (void)appendActivity("connaissance", std::to_string(n) + " souvenir(s) du projet " + ctx.projectName + " li\xC3\xA9(s) \xC3\xA0 son identifiant");
    return n;
}

std::vector<const Knowledge*> Registry::conflictsOf(std::string_view id) const {
    std::vector<const Knowledge*> out;
    if (const auto* k = find(id))
        for (const auto& c : k->conflicts)
            if (const auto* o = find(c)) out.push_back(o);
    return out;
}

std::vector<const Knowledge*> Registry::pendingHere(const Context& ctx) const {
    std::vector<const Knowledge*> out;
    for (const auto& k : items_) {
        if (k.builtIn || k.status != "provisoire" || !k.active) continue;
        if (!k.projectId.empty() && k.projectId != ctx.projectId) continue;
        if (k.projectId.empty() && !k.project.empty() && k.project != ctx.projectName) continue;
        if (k.scope == "conversation" && !ctx.conversationId.empty() && k.conversation != ctx.conversationId) continue;
        out.push_back(&k);
    }
    std::stable_sort(out.begin(), out.end(), [](const Knowledge* a, const Knowledge* b) { return a->added > b->added; });
    return out;
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

std::uintmax_t Registry::bytesHere(const Context& ctx) const noexcept {
    std::uintmax_t n = 0;
    for (const auto& k : items_)
        if (hereState(k, ctx) == Here::Active) n += k.size();
    return n;
}

} // namespace app::ai::memory
