// =============================================================================
//  app/screens/GoToWorkspace.cpp - Aller a... (Ctrl+K) (lot 20 ; lot recherche :
//  partout)
// -----------------------------------------------------------------------------
//  CE QUE L'ON CHERCHE, PAR CATEGORIE : les variables de l'automate (globales,
//  locales des unites, champs des DDT, variables des DFB) et leurs membres
//  (armoires[0].sorties.V3, par le chemin tape), les variables IHM, les types
//  et les blocs, les unites, les sections, chaque LIGNE DE CODE des sections,
//  les taches, les tables d'animation et leurs lignes, les vues et les objets
//  (noms et textes), les alarmes, les recettes, les utilisateurs, les
//  ressources, les styles, les scripts et les fonctions IHM (leur texte
//  entier), les variables systeme, les equipements, les macros, les versions,
//  les sujets d'aide, les volets et les ACTIONS de l'application (Simuler,
//  Vers Control Expert, Disposition : mosaique...).
//
//  UN INDEX, PAS UNE RECHERCHE A CHAQUE FRAPPE : refait quand le projet change
//  (son adresse, celle de l'IHM, la revision de la pile des commandes) ; les
//  macros et l'aide, qui ne suivent pas le projet, au plus toutes les 30 s. La
//  frappe attend 120 ms (GoToPanel::setDebounce). La recherche est celle de
//  toutes les listes (ui::SearchQuery : mots ET, "phrase", -exclu, sans casse
//  ni accents), sur le nom, puis ce qui le decrit (type, commentaire, texte).
//  Un nom qui COMMENCE par le premier mot passe devant un mot du nom qui
//  commence par lui, puis devant le reste ; les categories se rangent par leur
//  meilleur resultat. Huit par categorie au plus quand elles sont toutes
//  montrees (Tab : une categorie seule, 200 au plus) ; les totaux sont exacts.
//
//  Y ALLER : l'onglet qui montre la chose, a sa place - la variable choisie
//  dans API > Variables (un membre deplie jusqu'a lui), la section ouverte a
//  sa ligne, la vue dans son editeur (l'objet choisi), l'alarme dans les
//  alarmes, le script a sa ligne, la macro dans l'onglet Macros ; une action
//  est faite.
// =============================================================================
#include "Screens.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : Aller a, dans chaque application

#include "../../project/CodeIconKeys.hpp"

#include "../AnimationTablesPane.hpp"
#include "../ApiPanes.hpp"
#include "../App.hpp"
#include "../GoToPanel.hpp"
#include "../GoToSearch.hpp"
#include "../TaskPanes.hpp"
#include "../TopBar.hpp"
#include "../TypePanes.hpp"
#include "../VariablesPane.hpp"
#include "../hmi/HmiAssetPanes.hpp"
#include "../hmi/HmiCommPanes.hpp"
#include "../hmi/HmiDesignPanes.hpp"
#include "../hmi/HmiPublicVarsPane.hpp"
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiSupervisionPanes.hpp"
#include "../hmi/HmiTreeData.hpp"
#include "../hmi/HmiVariablePanes.hpp"
#include "../../hmi/HmiGuide.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiVersions.hpp"
#include "../../project/MacroSpec.hpp"
#include "../../project/MemberTree.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>

namespace app {

namespace {
// 1.8.0 : l'icone au choix (la couleur suit : GoToPanel dessine une icone du catalogue dans la sienne).
ui::Icon codeIconOr(int icon, ui::Icon fallback) { return icon >= 0 ? ui::codeIcon(icon) : fallback; }
} // namespace


namespace {

using Result = GoToPanel::Result;
using namespace gotosearch;

// Le classement d'avant (lot 20) : un nom qui commence par le texte, puis un
// mot qui commence par lui, puis il le contient ; le plus court d'abord.
int scoreOf(const std::string& name, const std::string& text) {
    const int at = GoToPanel::matchAt(name, text);
    if (at < 0) return -1;
    int base = 2000;
    if (at == 0) base = 0;
    else if (name[static_cast<std::size_t>(at) - 1] == '_' || name[static_cast<std::size_t>(at) - 1] == ' '
             || name[static_cast<std::size_t>(at) - 1] == '.')
        base = 1000;
    return base + static_cast<int>(name.size());
}

const char* const kDot = " \xC2\xB7 ";

std::string languageName(domain::PouLanguage l) {
    switch (l) {
        case domain::PouLanguage::ST:  return "ST";
        case domain::PouLanguage::IL:  return "IL";
        case domain::PouLanguage::LD:  return "LD";
        case domain::PouLanguage::FBD: return "FBD";
        case domain::PouLanguage::SFC: return "SFC";
        default:                       return "?";
    }
}

std::string scopeWord(domain::VariableScope s) {
    switch (s) {
        case domain::VariableScope::Input:  return "entr\xC3\xA9" "e";
        case domain::VariableScope::Output: return "sortie";
        case domain::VariableScope::InOut:  return "entr\xC3\xA9" "e-sortie";
        case domain::VariableScope::Public: return "publique";
        default:                            return "locale";
    }
}

struct PaneEntry { const char* key; const char* title; int tab; };
// Les volets de l'IHM (et quelques sous-onglets) : ce que l'arbre ouvre.
const PaneEntry kPanes[] = {
    {"config", "IHM \xE2\x80\xBA Configuration", -1},
    {"vues", "IHM \xE2\x80\xBA Vues", -1},
    {"scripts", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Scripts g\xC3\xA9n\xC3\xA9raux", 0},
    {"scripts", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM", 1},
    {"scripts", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Types IHM", 2},
    {"fonctions", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Fonctions", -1},
    {"variables-publiques", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables syst\xC3\xA8me et d'instances", -1},
    {"natives", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Natives (fonctions, types, \xC3\xA9num\xC3\xA9rations)", -1},   // 1.12.0
    {"communication", "\xC3\x89quipements \xE2\x80\xBA R\xC3\xA9seau du PC", HmiCommPane::TNetwork},
    {"communication", "\xC3\x89quipements \xE2\x80\xBA Scanner IP", HmiCommPane::TScanner},
    {"communication", "\xC3\x89quipements \xE2\x80\xBA \xC3\x89quipements", HmiCommPane::TEquipments},
    {"communication", "\xC3\x89quipements \xE2\x80\xBA Table des adresses", HmiCommPane::TTable},
    {"communication", "\xC3\x89quipements \xE2\x80\xBA Plan d'adressage", HmiCommPane::TPlan},
    {"communication", "\xC3\x89quipements \xE2\x80\xBA Carte m\xC3\xA9moire", HmiCommPane::TMap},
    {"communication", "\xC3\x89quipements \xE2\x80\xBA Valeurs simul\xC3\xA9" "es", HmiCommPane::TValues},
    {"simulation", "Simulation \xE2\x80\xBA IHM", -1},   // Lot API 8 : Centre de simulation (etait IHM > Simulation)
    {"outil", "IHM \xE2\x80\xBA Outil Modbus", -1},
    {"generer", "IHM \xE2\x80\xBA G\xC3\xA9n\xC3\xA9rer", -1},
    {"compiler", "IHM \xE2\x80\xBA Compiler", -1},
    {"alarmes", "IHM \xE2\x80\xBA Alarmes", -1},
    {"recettes", "IHM \xE2\x80\xBA Recettes", -1},
    {"utilisateurs", "IHM \xE2\x80\xBA Utilisateurs", -1},
    {"historiques", "IHM \xE2\x80\xBA Historiques", -1},
    {"ressources", "IHM \xE2\x80\xBA Ressources", -1},
    {"fichiers", "IHM \xE2\x80\xBA Fichiers externes", -1},
    {"styles", "IHM \xE2\x80\xBA Styles", -1},
    {"essais", "IHM \xE2\x80\xBA Essais", -1},
    {"langues", "IHM \xE2\x80\xBA Langues", -1},
    {"unites", "IHM \xE2\x80\xBA Unit\xC3\xA9s et formats", -1},
    {"notifications", "IHM \xE2\x80\xBA Notifications", -1},
    {"rapports", "IHM \xE2\x80\xBA Rapports", -1},
    {"web", "IHM \xE2\x80\xBA Acc\xC3\xA8s web", -1},
    {"poste", "IHM \xE2\x80\xBA Poste d'exploitation", -1},
    {"rechercher", "IHM \xE2\x80\xBA Rechercher / remplacer", -1},
    {"echange", "IHM \xE2\x80\xBA Exporter / Importer", -1},
    {"versions", "IHM \xE2\x80\xBA Versions", -1},
    {"aide", "IHM \xE2\x80\xBA Aide", -1},
};

// Les onglets de l'API (openApiPane).
const PaneEntry kApiPanes[] = {
    {"api", "API \xE2\x80\xBA Tableau de bord", -1},
    {"configuration", "API \xE2\x80\xBA Configuration (processeur, racks, modules)", -1},
    {"taches", "API \xE2\x80\xBA T\xC3\xA2" "ches", -1},
    {"ordre", "API \xE2\x80\xBA Ordre d'ex\xC3\xA9" "cution", -1},
    {"types", "API \xE2\x80\xBA Types d\xC3\xA9riv\xC3\xA9s", -1},
    {"dfb", "API \xE2\x80\xBA Blocs DFB", -1},
    {"unites", "API \xE2\x80\xBA Unit\xC3\xA9s de programme", -1},
    {"variables", "API \xE2\x80\xBA Variables", -1},
    {"sous-routines", "API \xE2\x80\xBA Sous-routines", -1},
    {"tables", "API \xE2\x80\xBA Tables d'animation", -1},
    {"simulation", "Simulation \xE2\x80\xBA Automate", -1},   // Lot API 8 : Centre de simulation (etait API > Simulation)
    {"statistiques", "API \xE2\x80\xBA Statistiques", -1},
};

struct ActionEntry { const char* key; const char* title; const char* shortcut; };
const ActionEntry kActions[] = {
    {"act:nouvelle-vue", "Nouvelle vue (choisir un mod\xC3\xA8le)", ""},
    {"act:importer-vues", "Importer des vues", ""},
    {"act:exporter-vues", "Exporter les vues", ""},
    {"act:enregistrer", "Enregistrer le projet", "Ctrl+S"},
    {"act:historique", "Historique", "Ctrl+H"},
    {"act:annuler", "Annuler", "Ctrl+Z"},
    {"act:retablir", "R\xC3\xA9tablir", "Ctrl+Y"},
};

// Les parties de la barre du haut qui sont des actions (les menus s'y ajoutent).
const ActionEntry kBarParts[] = {
    {"sim.run", "Simuler (lancer la simulation)", "F9"},
    {"sim.pause", "Mettre la simulation en pause", ""},
    {"sim.stop", "Arr\xC3\xAAter la simulation", ""},
    {"sim.step", "Un cycle de simulation", ""},
    {"project.exportSources", "Exporter vers Control Expert (Vers Control Expert)", ""},
    {"edit.history", "Historique du projet", "Ctrl+H"},
};

// Le volet d'un onglet de l'API (nul : pas ouvert, ou pas de ce genre).
template <typename Pane>
Pane* contentOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<Pane*>(&frame->content()) : nullptr;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

// ================================================================== l'index ====
//  Ce que l'index decrit (quand l'un change, il se refait), et quand les
//  macros et l'aide ont ete relues.
struct MainAnalysisScreen::GoToIndex : gotosearch::Index {
    const void*   project{nullptr};
    const void*   hmi{nullptr};
    std::uint64_t revision{~std::uint64_t{0}};
    std::string   folder;
    bool          built{false};
    double        libraryAt{-1000.0};
};

void MainAnalysisScreen::refreshGoToIndex() {
    const auto plc = app_.project();
    const auto doc = app_.hmi();
    const auto revision = app_.commands().revision();
    if (!goToIndex_) goToIndex_ = std::make_shared<GoToIndex>();
    auto& ix = *goToIndex_;
    const double now = nowSeconds();
    const auto push = [](std::vector<Entry>& list, int group, std::string title, std::string subtitle, std::string key, ui::Icon icon,
                         std::string extra = {}, std::string hint = {}) {
        gotosearch::Index::add(list, group, std::move(title), std::move(subtitle), std::move(key), icon, std::move(extra), std::move(hint));
    };

    // ---- ce qui ne suit pas le projet : les macros, l'aide -----------------------
    if (now - ix.libraryAt > 30.0) {
        ix.libraryAt = now;
        ix.library.clear();
        project::SharedLibrary library(project::SharedLibrary::defaultRoot());
        // 1.12.0 : les macros et l'aide de la bibliotheque sont celles de l'automate ; l'aide
        // de l'IHM, celle de XPGAnalyser IHM.
        if (core::hasApi() && library.scan()) {
            for (const auto& item : library.items()) {
                if (item.kind != project::LibraryItemKind::Macro) continue;
                std::ifstream in(item.path, std::ios::binary);
                const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                const auto spec = project::macro::parseMacroSpec(source, item.name);
                std::string sub = "Macro";
                if (!spec.category.empty()) sub += kDot + spec.category;
                if (!spec.summary.empty()) sub += kDot + spec.summary;
                std::string extra = spec.applyText;
                for (const auto& x : spec.produces) extra += " " + x;
                push(ix.library, GMacro, item.name, std::move(sub), "macro:" + item.name, ui::Icon::Code, std::move(extra));
            }
        }
        static const std::vector<hmi::guide::Topic> kNoTopic;
        for (const auto& t : core::hasIhm() ? hmi::guide::topics() : kNoTopic) {
            std::string extra;
            for (const auto& w : t.words) extra += w + " ";
            push(ix.library, GHelp, t.title, "Aide de l'IHM" + std::string(kDot) + t.chapter + (t.summary.empty() ? std::string{} : kDot + t.summary),
                 "help:" + t.key, ui::Icon::Info, std::move(extra));
        }
        for (const auto& e : helpLibrary()) {
            if (!e.hasHelp || !core::hasApi()) continue;
            push(ix.library, GHelp, e.name, "Aide de la biblioth\xC3\xA8que" + std::string(kDot) + e.category
                                                + (e.help.summary.empty() ? std::string{} : kDot + e.help.summary),
                 "libhelp:" + e.name, ui::Icon::Library, e.help.usage.substr(0, std::min<std::size_t>(e.help.usage.size(), 400)));
        }
    }

    if (ix.built && ix.project == plc.get() && ix.hmi == doc.get() && ix.revision == revision && ix.folder == app_.projectFolder()) return;
    ix.built = true;
    ix.project = plc.get();
    ix.hmi = doc.get();
    ix.revision = revision;
    ix.folder = app_.projectFolder();
    ix.clearProject();
    ix.plc = plc;

    // ---- l'automate ---------------------------------------------------------------
    if (plc) {
        const auto& p = *plc;
        const auto txt = [&](domain::SymbolId id) { return std::string(p.strings.text(id)); };
        for (domain::Index i = 0; i < p.variables.size(); ++i) {
            const auto& v = p.variables[i];
            const std::string name = txt(v.name);
            if (name.empty()) continue;
            const std::string type = txt(v.type.name);
            const std::string comment = txt(v.comment);
            std::string sub = type, key;
            if (v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant) {
                sub += v.scope == domain::VariableScope::Constant ? std::string(kDot) + "constante" : std::string(kDot) + "globale";
                if (!v.address.raw.empty()) sub += kDot + v.address.raw;
                key = "plcvar:" + name;
                ix.addGlobal(name, type);
            } else if (v.scope == domain::VariableScope::DerivedMember) {
                const std::string owner = v.owner < p.derivedTypes.size() ? txt(p.derivedTypes[v.owner].name) : std::string{};
                sub += kDot + std::string("champ de ") + owner;
                key = "plcfield:" + owner + "." + name;
            } else {
                const bool known = v.owner < p.pous.size();
                const std::string owner = known ? txt(p.pous[v.owner].name) : std::string{};
                const bool block = known && p.pous[v.owner].kind == domain::PouKind::FunctionBlockType;
                sub += kDot + scopeWord(v.scope) + (block ? " du bloc " : " de l'unit\xC3\xA9 ") + owner;
                key = (block ? "dfbvar:" : "plclocal:") + owner + "/" + name;
            }
            if (!comment.empty()) sub += kDot + comment;
            push(ix.entries, GApiVar, name, std::move(sub), std::move(key), v.located ? ui::Icon::LocatedVariable : ui::Icon::Variable,
                 txt(v.initValue));
        }
        ix.finish();
        for (const auto& d : p.derivedTypes) {
            const std::string name = txt(d.name);
            std::string extra;
            for (const auto f : d.fields)
                if (f < p.variables.size()) extra += txt(p.variables[f].name) + " ";
            push(ix.entries, GType, name, "Type d\xC3\xA9riv\xC3\xA9 (DDT)" + std::string(kDot) + std::to_string(d.fields.size()) + " champs"
                                              + (d.version.empty() ? std::string{} : kDot + std::string("version ") + d.version),
                 "ddt:" + name, codeIconOr(project::codeicons::typeIcon(p, static_cast<domain::Index>(&d - p.derivedTypes.data())), ui::Icon::DerivedType),
                 std::move(extra));
        }
        for (domain::Index i = 0; i < p.pous.size(); ++i) {
            const auto& pou = p.pous[i];
            const std::string name = txt(pou.name);
            if (pou.kind == domain::PouKind::FunctionBlockType && pou.userDefined) {
                push(ix.entries, GType, name, "Bloc DFB" + std::string(kDot) + std::to_string(pou.parameters.size()) + " broches" + kDot
                                                  + std::to_string(pou.instanceCount) + " instances"
                                                  + (pou.version.empty() ? std::string{} : kDot + std::string("version ") + pou.version),
                     "dfb:" + name, codeIconOr(project::codeicons::pouIcon(p, i), ui::Icon::FunctionBlock));
            } else if (pou.kind == domain::PouKind::ProgramUnit) {
                const std::string task = txt(pou.task);
                push(ix.entries, GUnit, name, "Unit\xC3\xA9 de programme" + std::string(kDot) + std::to_string(pou.sections.size()) + " sections"
                                                  + (task.empty() ? std::string{} : kDot + std::string("t\xC3\xA2" "che ") + task),
                     "unit:" + name, codeIconOr(project::codeicons::pouIcon(p, i), ui::Icon::Program));
            }
        }
        for (domain::Index i = 0; i < p.sections.size(); ++i) {
            const auto& s = p.sections[i];
            const std::string name = txt(s.name);
            const std::string owner = s.owner < p.pous.size() ? txt(p.pous[s.owner].name) : std::string{};
            const bool inBlock = s.owner < p.pous.size() && p.pous[s.owner].kind == domain::PouKind::FunctionBlockType;
            const std::string task = txt(s.task);
            std::string sub = std::string(s.isSubroutine ? "Sous-routine" : "Section") + kDot + languageName(s.language) + kDot
                            + std::to_string(s.lineCount) + " lignes";
            if (!task.empty()) sub += kDot + std::string("t\xC3\xA2" "che ") + task;
            if (!owner.empty()) sub += kDot + std::string(inBlock ? "corps du bloc " : "de ") + owner;
            const auto icon = codeIconOr(project::codeicons::sectionIcon(p, i), ui::Icon::Section);     // 1.8.0 : l'icone au choix
            push(ix.entries, GSection, name, std::move(sub), (s.isSubroutine ? "sr:" : "section:") + std::to_string(i), icon);
            ix.addText(GCode, inBlock ? owner + "." + name : name, "code:" + std::to_string(i), s.body, icon);
        }
        for (const auto& t : p.tasks) {
            const std::string name = txt(t.name);
            push(ix.entries, GTask, name, "T\xC3\xA2" "che" + std::string(kDot) + t.type + (t.period ? kDot + std::to_string(t.period) + " ms" : std::string{})
                                              + kDot + std::to_string(t.sections.size()) + " sections",
                 "task:" + name, ui::Icon::Task);
        }
        for (std::size_t ti = 0; ti < p.animationTables.size(); ++ti) {
            const auto& t = p.animationTables[ti];
            const std::string name = txt(t.name);
            push(ix.entries, GTable, name, "Table d'animation" + std::string(kDot) + std::to_string(t.entries.size()) + " lignes",
                 "table:" + std::to_string(ti), ui::Icon::AnimationTable);
            for (const auto& e : t.entries) {
                const std::string line = txt(e.name);
                push(ix.entries, GTable, line, "Ligne de la table " + name + kDot + (e.hmi ? "variable IHM" : "variable API"),
                     "tableline:" + std::to_string(ti) + "/" + line, ui::Icon::Variable);
            }
        }
    }

    // ---- les versions (le dossier du projet) ---------------------------------------
    if (!app_.projectFolder().empty())
        if (auto store = hmi::ver::open(app_.projectFolder()); store)
            for (const auto& v : store->versions)
                push(ix.entries, GVersion, v.label(), v.date + (v.author.empty() ? std::string{} : kDot + v.author)
                                                          + (v.comment.empty() ? std::string{} : kDot + v.comment),
                     "version:" + std::to_string(v.number), ui::Icon::History, v.since);

    // ---- l'IHM -------------------------------------------------------------------
    if (doc) {
        const auto& p = doc->project;
        for (const auto& v : p.programs.variables) {
            std::string sub = v.type;
            if (v.bound()) sub += kDot + v.equipment + (v.address.empty() ? std::string{} : kDot + v.address);
            else sub += kDot + std::string("locale");
            if (!v.folder.empty()) sub += kDot + v.folder;
            if (!v.description.empty()) sub += kDot + v.description;
            push(ix.entries, GHmiVar, v.name, std::move(sub), "hmivar:" + v.name, ui::Icon::Variable);
        }
        for (const auto& v : p.views) {
            push(ix.entries, GView, v.name, std::string(hmi::viewRoleLabel(v.role)) + kDot + std::to_string(v.objects.size()) + " objets"
                                                + (v.description.empty() ? std::string{} : kDot + v.description),
                 "hmiview:" + std::to_string(v.id), ui::Icon::Screen);
            for (const auto& o : v.objects) {
                std::string text = o.text("text");
                if (text.empty()) text = o.text("label");
                if (text.empty()) text = o.text("title");
                std::string sub = std::string(hmi::kindLabel(o.kind)) + kDot + "vue " + v.name;
                if (!text.empty()) sub += kDot + text;
                const std::string variable = o.text("variable");
                if (!variable.empty()) sub += kDot + variable;
                push(ix.entries, GObject, o.name, std::move(sub), "hmiobj:" + std::to_string(v.id) + "/" + std::to_string(o.id), ui::Icon::Layers,
                     o.text("label") + " " + o.text("title") + " " + o.text("value"));
            }
            for (const auto& s : v.scripts) {
                push(ix.entries, GScript, s.name.empty() ? std::string(hmi::eventLabel(s.event)) : s.name,
                     "Script de la vue " + v.name + kDot + std::string(hmi::eventLabel(s.event))
                         + (s.description.empty() ? std::string{} : kDot + s.description),
                     "hmiscript:" + std::to_string(v.id) + "/" + std::to_string(s.id) + "/0", ui::Icon::Code);
                ix.addText(GScript, v.name + " \xE2\x80\xBA " + (s.name.empty() ? std::string(hmi::eventLabel(s.event)) : s.name),
                        "hmiscript:" + std::to_string(v.id) + "/" + std::to_string(s.id), s.body, ui::Icon::Code);
            }
        }
        for (const auto& s : p.programs.scripts) {
            push(ix.entries, GScript, s.name, "Script g\xC3\xA9n\xC3\xA9ral" + std::string(kDot) + std::string(hmi::eventLabel(s.event))
                                                  + (s.description.empty() ? std::string{} : kDot + s.description),
                 "hmiscript:0/" + std::to_string(s.id) + "/0", ui::Icon::Code);
            ix.addText(GScript, s.name, "hmiscript:0/" + std::to_string(s.id), s.body, ui::Icon::Code);
        }
        for (const auto& f : p.programs.functions) {
            push(ix.entries, GScript, f.name, "Fonction IHM" + (f.returnType.empty() ? std::string{} : kDot + f.returnType)
                                                  + (f.description.empty() ? std::string{} : kDot + f.description),
                 "hmifunc:" + std::to_string(f.id) + "/0", ui::Icon::Code);
            ix.addText(GScript, f.name, "hmifunc:" + std::to_string(f.id), f.body, ui::Icon::Code);
        }
        for (const auto& a : p.alarms)
            push(ix.entries, GAlarm, a.name, "Alarme" + std::string(kDot) + std::to_string(a.priority) + " - " + std::string(hmi::alarmPriorityLabel(a.priority))
                                                 + kDot + a.condition + (a.message.empty() ? std::string{} : kDot + a.message),
                 "hmialarm:" + std::to_string(a.id), ui::Icon::Warning, a.group + " " + a.category + " " + a.description + " " + a.instruction);
        for (const auto& r : p.recipes) {
            std::string extra;
            for (const auto& f : r.fields) extra += f.name + " " + f.variable + " ";
            for (const auto& rec : r.records) extra += rec.name + " ";
            push(ix.entries, GRecipe, r.name, "Recette" + std::string(kDot) + std::to_string(r.fields.size()) + " \xC3\xA9l\xC3\xA9ments" + kDot
                                                  + std::to_string(r.records.size()) + " jeux" + (r.description.empty() ? std::string{} : kDot + r.description),
                 "hmirecipe:" + std::to_string(r.id), ui::Icon::Document, std::move(extra));
        }
        for (const auto& u : p.security.users) {
            const auto* grp = p.group(u.group);
            push(ix.entries, GUser, u.login, "Utilisateur" + (u.fullName.empty() ? std::string{} : kDot + u.fullName)
                                                  + (grp ? kDot + grp->name : std::string{}) + (u.description.empty() ? std::string{} : kDot + u.description),
                 "hmiuser:" + std::to_string(u.id), ui::Icon::User);
        }
        for (const auto& r : p.assets.resources)
            push(ix.entries, GResource, r.name, std::string(hmi::mediaKindLabel(r.kind())) + kDot + r.format + (r.detail.empty() ? std::string{} : kDot + r.detail),
                 "hmires:" + std::to_string(r.id), ui::Icon::Image, r.origin + " " + r.folder);
        for (const auto& s : p.styles)
            push(ix.entries, GStyle, s.name, "Style" + (s.description.empty() ? std::string{} : kDot + s.description), "hmistyle:" + std::to_string(s.id),
                 ui::Icon::Layers, s.folder);
        for (const auto& e : p.equipments)
            push(ix.entries, GEquipment, e.name, "\xC3\x89quipement du r\xC3\xA9seau", "hmieq:" + e.name, ui::Icon::Network);
        for (const auto& v : hmi::pub::kSysVars)
            if (hmi::pub::sysDomainShown(v.domain))      // 1.12.0
            push(ix.entries, GSysVar, "SYS." + std::string(v.name), std::string(v.type) + kDot + std::string(hmi::pub::kSysDomains[v.domain]) + kDot
                                                                        + std::string(v.text),
                 "sysvar:" + std::string(v.name), ui::Icon::Variable);
    }

    // ---- les volets et les actions ---------------------------------------------------
    // 1.12.0 : les onglets de l'API dans XPGAnalyser API, les volets et actions de l'IHM dans XPGAnalyser IHM.
    if (core::hasApi())
        for (const auto& pe : kApiPanes) push(ix.entries, GPane, pe.title, "Onglet de l'API", std::string("apipane:") + pe.key, ui::Icon::Cpu);
    if (doc)
        for (const auto& pe : kPanes)
            push(ix.entries, GPane, pe.title, "Volet de l'IHM", std::string("pane:") + pe.key + ":" + std::to_string(pe.tab), ui::Icon::Folder);
    if (core::hasIhm())
        for (const auto& ae : kActions) push(ix.entries, GAction, ae.title, "Action", ae.key, ui::Icon::Play, {}, ae.shortcut);
    // 1.12.2 : le profil des raccourcis des editeurs de code (Visual Studio ou classique).
    push(ix.entries, GAction, "Raccourcis des \xC3\xA9" "diteurs : Visual Studio (accords Ctrl+K)", "Commande", "cmd:raccourcis:vs", ui::Icon::Code);
    push(ix.entries, GAction, "Raccourcis des \xC3\xA9" "diteurs : classique (1.12.1, sans accords)", "Commande", "cmd:raccourcis:classique",
         ui::Icon::Code);
    std::vector<std::string> seen;
    for (const auto& bp : kBarParts) {
        if (!core::hasApi()) break;          // 1.12.0 : la simulation de l'automate, Vers Control Expert
        seen.emplace_back(bp.key);
        push(ix.entries, GAction, bp.title, "Action de la barre du haut", std::string("bar:") + bp.key, ui::Icon::Play, {}, bp.shortcut);
    }
    if (topBar_) {
        using M = TopBar::Menu;
        const std::pair<M, const char*> menus[] = {{M::Project, "Projet"}, {M::New, "+ Nouveau"}, {M::View, "Affichage"}, {M::Help, "Aide"},
                                                   {M::Version, "Version"}};
        for (const auto& [m, menuName] : menus)
            for (const auto& e : topBar_->menuEntries(m)) {
                if (e.separator || e.heading || e.action.empty()) continue;
                if (std::find(seen.begin(), seen.end(), e.action) != seen.end()) continue;
                seen.push_back(e.action);
                std::string title = e.label;
                // "Section", "Variable" du menu + Nouveau : "Nouveau : Section".
                if (m == M::New) title = "Nouveau : " + title;
                push(ix.entries, GAction, std::move(title), std::string("Menu ") + menuName, "bar:" + e.action,
                     e.icon == ui::Icon::None ? ui::Icon::Play : e.icon, {}, e.shortcut);
            }
    }
}

GoToPanel::Outcome MainAnalysisScreen::goToSearchAll(const std::string& text, int group) {
    // L'index a jour (rien a refaire si le projet n'a pas change), puis la recherche.
    refreshGoToIndex();
    // ---- Lot API 8 : bandeau haut (">" : les commandes ; vide : les derniers choix) ----
    if (auto palette = topBarPalette(text, group)) return std::move(*palette);
    // ---- fin Lot API 8 : bandeau haut ----
    return gotosearch::search(*goToIndex_, text, group);
}

GoToPanel* MainAnalysisScreen::goToPanel(bool open) {
    if (open && goToPanel_ && !goToPanel_->isOpen()) openGoTo();
    return goToPanel_;
}

void MainAnalysisScreen::openGoTo() {
    if (!goToPanel_ || !goToBox_) return;
    if (goToPanel_->isOpen()) {
        goToPanel_->close();
        return;
    }
    // Lot recherche : l'index a jour (rien a refaire si le projet n'a pas
    // change), la recherche par categorie, le delai de frappe.
    refreshGoToIndex();
    goToPanel_->setSearchAll([this](const std::string& text, int group) { return goToSearchAll(text, group); });
    goToPanel_->setDebounce(0.12);
    goToPanel_->open(goToBox_->bounds());
}

// La forme d'avant (lot 20), gardee pour ce qui l'appelle encore : l'ecran
// donne maintenant setSearchAll (goToSearchAll) a l'ouverture.
std::vector<GoToPanel::Result> MainAnalysisScreen::goToSearch(const std::string& text) const {
    std::vector<Result> out;
    if (text.empty()) return out;
    std::vector<std::vector<Result>> groups(GCount);
    const auto add = [&](int g, Result r) {
        r.group = g;
        r.groupTitle = groupTitle(g);
        groups[static_cast<std::size_t>(g)].push_back(std::move(r));
    };
    if (auto doc = app_.hmi()) {
        const auto& p = doc->project;
        for (const auto& v : p.programs.variables) {
            const int s = scoreOf(v.name, text);
            if (s >= 0) add(GHmiVar, {0, {}, v.name, v.type, {}, "hmivar:" + v.name, ui::Icon::Variable, s});
        }
        for (const auto& v : p.views) {
            const int s = scoreOf(v.name, text);
            if (s >= 0) add(GView, {0, {}, v.name, std::string(hmi::viewRoleLabel(v.role)), {}, "hmiview:" + std::to_string(v.id), ui::Icon::Screen, s});
        }
        for (const auto& a : p.alarms) {
            const int s = scoreOf(a.name, text);
            if (s >= 0) add(GAlarm, {0, {}, a.name, a.condition, {}, "hmialarm:" + std::to_string(a.id), ui::Icon::Warning, s});
        }
    }
    if (auto plc = app_.project()) {
        std::size_t seen = 0;
        for (const auto& v : plc->variables) {
            if (v.scope != domain::VariableScope::Global) continue;
            const std::string name = std::string(plc->strings.text(v.name));
            const int s = scoreOf(name, text);
            if (s < 0) continue;
            if (++seen > 400) break;
            add(GApiVar, {0, {}, name, std::string(plc->strings.text(v.type.name)), {}, "plcvar:" + name, ui::Icon::Variable, s});
        }
    }
    for (const auto& pe : kPanes) {
        const int s = scoreOf(pe.title, text);
        if (s >= 0) add(GPane, {0, {}, pe.title, "Volet", {}, std::string("pane:") + pe.key + ":" + std::to_string(pe.tab), ui::Icon::Folder, s});
    }
    for (const auto& ae : kActions) {
        const int s = scoreOf(ae.title, text);
        if (s >= 0) add(GAction, {0, {}, ae.title, "Action", ae.shortcut, ae.key, ui::Icon::Play, s});
    }
    for (auto& g : groups) {
        std::stable_sort(g.begin(), g.end(), [](const Result& a, const Result& b) { return a.score < b.score; });
        for (std::size_t i = 0; i < g.size() && i < 5; ++i) out.push_back(g[i]);
    }
    return out;
}

void MainAnalysisScreen::goToResult(const GoToPanel::Result& r) {
    // ---- Lot API 8 : bandeau haut (les derniers choix ; les commandes de la palette) ----
    topBarRememberGoTo(r);
    if (r.key.rfind("cmd:", 0) == 0) { (void)topBarLot8Action(r.key); return; }
    // ---- fin Lot API 8 : bandeau haut ----
    const auto colon = r.key.find(':');
    const std::string kind = r.key.substr(0, colon);
    const std::string what = colon == std::string::npos ? std::string{} : r.key.substr(colon + 1);
    // "a/b" : les deux moitiés.
    const auto split = [](const std::string& s, std::string& a, std::string& b) {
        const auto slash = s.find('/');
        a = s.substr(0, slash);
        b = slash == std::string::npos ? std::string{} : s.substr(slash + 1);
    };
    const auto number = [](const std::string& s) { return std::strtoull(s.c_str(), nullptr, 10); };
    // Une section a sa ligne (1 : la premiere), comme goToSectionLine mais par son rang.
    const auto openLine = [this](domain::Index section, std::uint32_t line) {
        if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(ui::Checkbox::State::Checked);
        openDocument(section);
        const std::string editorId = "analysis.doc." + std::to_string(section) + ".code";
        auto* editor = widgetRoot() ? dynamic_cast<ui::MultiLineText*>(widgetRoot()->findById(editorId)) : nullptr;
        if (!editor && centre_)
            if (auto* page = centre_->page(centre_->currentIndex())) editor = dynamic_cast<ui::MultiLineText*>(page->findById(editorId));
        if (editor && line > 0) editor->goToLine(static_cast<std::size_t>(line - 1));
    };
    const auto p = app_.project();
    if (kind == "hmivar") {
        openHmiPane("scripts");
        if (auto* scripts = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
            scripts->showTab(HmiScriptsPane::TabVariables);
            if (auto* vars = scripts->variablesPane()) vars->selectPath(what);
        }
    } else if (kind == "hmiview") {
        openHmiView(std::strtoull(what.c_str(), nullptr, 10));
    } else if (kind == "hmiobj") {
        std::string view, object;
        split(what, view, object);
        openHmiView(number(view), -1, number(object));
    } else if (kind == "hmialarm") {
        openHmiPane("alarmes");
        if (auto* pane = dynamic_cast<HmiAlarmsPane*>(hmiTab("alarmes"))) pane->selectAlarm(static_cast<hmi::Id>(number(what)));
    } else if (kind == "hmirecipe") {
        openHmiPane("recettes");
        if (auto* pane = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"))) pane->selectRecipe(static_cast<hmi::Id>(number(what)));
    } else if (kind == "hmiuser") {
        openHmiPane("utilisateurs");
        if (auto* pane = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"))) pane->selectUser(static_cast<hmi::Id>(number(what)));
    } else if (kind == "hmires") {
        openHmiPane("ressources");
        if (auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"))) pane->selectResource(static_cast<hmi::Id>(number(what)));
    } else if (kind == "hmistyle") {
        openHmiPane("styles");
        if (auto* pane = dynamic_cast<HmiStylesPane*>(hmiTab("styles"))) pane->selectStyle(static_cast<hmi::Id>(number(what)));
    } else if (kind == "hmieq") {
        openHmiPane("communication");
        if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab("communication"))) {
            comm->tabs().setCurrentIndex(static_cast<std::size_t>(HmiCommPane::TEquipments));
            comm->selectEquipment(what);
        }
    } else if (kind == "hmiscript") {
        // "vue/script/ligne" (vue 0 : un script general).
        std::string view, rest, script, line;
        split(what, view, rest);
        split(rest, script, line);
        openHmiScripts(number(view), number(script), static_cast<int>(number(line)));
    } else if (kind == "hmifunc") {
        std::string function, line;
        split(what, function, line);
        openHmiFunctions(number(function), static_cast<int>(number(line)));
    } else if (kind == "sysvar") {
        openHmiPane("variables-publiques");
        if (auto* pane = dynamic_cast<HmiPublicVarsPane*>(hmiTab("variables-publiques"))) {
            pane->showTab(HmiPublicVarsPane::System);
            (void)pane->selectPath("SYS." + what);
        }
    } else if (kind == "plcvar" || kind == "member") {
        // La variable (ou le membre, deplie jusqu'a lui) choisie dans API > Variables.
        openApiPane("variables");
        if (auto* pane = contentOf<VariablesPane>(apiTab("variables"))) (void)pane->selectVariable(what);
    } else if (kind == "plclocal") {
        std::string unit, name;
        split(what, unit, name);
        openApiPane("unites");
        if (auto* pane = contentOf<UnitsPane>(apiTab("unites"))) (void)pane->selectVariable(unit, name);
    } else if (kind == "plcfield") {
        const auto dot = what.find('.');
        const std::string type = what.substr(0, dot);
        openApiPane("types");
        if (auto* pane = contentOf<DerivedTypesPane>(apiTab("types"))) {
            (void)pane->selectType(type);
            (void)pane->setExpanded("t:" + type, true);
        }
    } else if (kind == "ddt") {
        openApiPane("types");
        if (auto* pane = contentOf<DerivedTypesPane>(apiTab("types"))) (void)pane->selectType(what);
    } else if (kind == "dfb" || kind == "dfbvar") {
        std::string block, name;
        split(what, block, name);
        openApiPane("dfb");
        if (auto* pane = contentOf<DfbPane>(apiTab("dfb"))) {
            (void)pane->selectBlock(block);
            if (kind == "dfbvar") (void)pane->setExpanded("b:" + block, true);
        }
    } else if (kind == "unit") {
        openApiPane("unites");
        if (auto* pane = contentOf<UnitsPane>(apiTab("unites"))) (void)pane->selectUnit(what);
    } else if (kind == "section") {
        if (p && number(what) < p->sections.size()) openDocument(static_cast<domain::Index>(number(what)));
    } else if (kind == "sr") {
        if (p && number(what) < p->sections.size()) openSubroutine(static_cast<domain::Index>(number(what)));
    } else if (kind == "code") {
        // "section:ligne" : la section ouverte, le curseur sur la ligne.
        const auto sep = what.find(':');
        const auto section = number(what.substr(0, sep));
        const auto line = sep == std::string::npos ? 0ull : number(what.substr(sep + 1));
        if (p && section < p->sections.size()) openLine(static_cast<domain::Index>(section), static_cast<std::uint32_t>(line));
    } else if (kind == "task") {
        openApiPane("taches");
        if (auto* pane = contentOf<TasksPane>(apiTab("taches"))) (void)pane->selectTask(what);
    } else if (kind == "table" || kind == "tableline") {
        std::string table, line;
        split(what, table, line);
        openApiPane("tables");
        if (auto* pane = contentOf<AnimationTablesPane>(apiTab("tables"))) {
            (void)pane->selectTable(static_cast<std::size_t>(number(table)));
            if (!line.empty()) (void)pane->selectLine(line);
        }
    } else if (kind == "macro") {
        openMacros(what, /*launch=*/false);
    } else if (kind == "version") {
        openVersions(static_cast<int>(number(what)));
    } else if (kind == "help") {
        openHmiHelp(what);
    } else if (kind == "libhelp") {
        openHelpFor(help::Target{help::TargetKind::LibraryEntry, what, {}});
    } else if (kind == "apipane") {
        openApiPane(what);
    } else if (kind == "bar") {
        if (topBar_) topBar_->trigger(what);
        else onBarAction(what);
    } else if (kind == "pane") {
        const auto sep = what.find(':');
        const std::string key = what.substr(0, sep);
        const int tab = sep == std::string::npos ? -1 : std::atoi(what.c_str() + sep + 1);
        openHmiPane(key);
        if (tab >= 0) {
            if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab(key))) comm->tabs().setCurrentIndex(static_cast<std::size_t>(tab));
            if (auto* scripts = dynamic_cast<HmiScriptsPane*>(hmiTab(key))) scripts->showTab(static_cast<std::size_t>(tab));
        }
    } else if (kind == "act") {
        if (what == "nouvelle-vue") askNewHmiView("vue");
        else if (what == "importer-vues") askHmiImportViews();
        else if (what == "exporter-vues") askHmiExportViews(0);
        else if (what == "enregistrer") app_.saveFromKeyboard();
        else if (what == "historique") toggleHistory();
        else if (what == "annuler") app_.undo();
        else if (what == "retablir") app_.redo();
    }
    if (status_) status_->setTransientMessage("Aller \xC3\xA0 : " + r.title + (r.subtitle.empty() ? std::string{} : " (" + r.subtitle + ")"), 5.0);
}

// ---- Lot API 8 : bandeau haut ----
//  La palette Aller a / Faire... : ">texte" ne cherche que les commandes ;
//  vide, elle montre les derniers choix (TopBarWorkspace.cpp les retient).
std::optional<GoToPanel::Outcome> MainAnalysisScreen::topBarPalette(const std::string& text, int group) {
    // Vide : les derniers choix, les plus recents d'abord.
    if (text.empty()) {
        if (topBarGoToRecents_.empty() || group >= 0) return std::nullopt;
        GoToPanel::Outcome out;
        for (auto r : topBarGoToRecents_) {
            r.group = gotosearch::GAction;
            r.groupTitle = "R\xC3\xA9" "CENTS";
            out.results.push_back(std::move(r));
        }
        out.categories.push_back({gotosearch::GAction, "R\xC3\xA9" "CENTS", "R\xC3\xA9" "cents", out.results.size()});
        return out;
    }
    if (text[0] != '>' || !goToIndex_) return std::nullopt;
    // ">texte" : les commandes seules - les actions de l'index (barre, menus,
    // IHM), plus compiler, generer, chaque theme.
    gotosearch::Index cmds;
    for (const auto& e : goToIndex_->entries)
        if (e.group == gotosearch::GAction) cmds.entries.push_back(e);
    const auto add = [&](std::string title, std::string key, ui::Icon icon, std::string hint = {}) {
        gotosearch::Index::add(cmds.entries, gotosearch::GAction, std::move(title), "Commande", std::move(key), icon, {}, std::move(hint));
    };
    // 1.12.0 : les commandes de l'application.
    if (core::hasIhm()) add("Compiler l'IHM (les expressions impossibles, les liens)", "cmd:compiler", ui::Icon::Code);
    if (core::hasIhm()) add("G\xC3\xA9n\xC3\xA9rer l'IHM", "cmd:generer", ui::Icon::Code);
    if (core::hasApi()) add("Simuler (lancer la simulation)", "bar:sim.run", ui::Icon::Play, "F5");
    if (core::hasIhm() && !core::hasApi()) add("D\xC3\xA9marrer l'IHM simul\xC3\xA9" "e", "bar:hmi.start", ui::Icon::Play, "F8");
    if (core::hasApi()) add("Nouvelle variable", "bar:create.variable", ui::Icon::Variable);
    add("Notifications (la cloche du bandeau)", "cmd:cloche", ui::Icon::Info);
    add("Revenir avant\xE2\x80\xA6 (les 10 derni\xC3\xA8res actions)", "cmd:annuler-liste", ui::Icon::Undo);
    add("T\xC3\xA2" "ches de fond", "cmd:taches", ui::Icon::History);
    add("D\xC3\xA9poser un fichier\xE2\x80\xA6", "bar:files.drop", ui::Icon::Open);
    for (const auto& t : ui::Theme::all()) add("Th\xC3\xA8me " + t.label, "cmd:theme:" + t.key, ui::Icon::Layers);
    // 1.12.2 : le profil des raccourcis des editeurs de code.
    add("Raccourcis des \xC3\xA9" "diteurs : Visual Studio (accords Ctrl+K)", "cmd:raccourcis:vs", ui::Icon::Code);
    add("Raccourcis des \xC3\xA9" "diteurs : classique (1.12.1, sans accords)", "cmd:raccourcis:classique", ui::Icon::Code);
    std::size_t from = 1;
    while (from < text.size() && text[from] == ' ') ++from;
    const std::string rest = text.substr(from);
    if (!rest.empty()) return gotosearch::search(cmds, rest, gotosearch::GAction);
    // ">" seul : toutes les commandes, dans l'ordre.
    GoToPanel::Outcome out;
    for (const auto& e : cmds.entries) {
        GoToPanel::Result r;
        r.group = gotosearch::GAction;
        r.groupTitle = "COMMANDES";
        r.title = e.title;
        r.subtitle = e.subtitle;
        r.hint = e.hint;
        r.key = e.key;
        r.icon = e.icon;
        out.results.push_back(std::move(r));
    }
    out.categories.push_back({gotosearch::GAction, "COMMANDES", "Commandes", out.results.size()});
    return out;
}
// ---- fin Lot API 8 : bandeau haut ----

} // namespace app
