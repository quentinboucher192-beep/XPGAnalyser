// =============================================================================
//  app/screens/DropFilesWorkspace.cpp - lot API 8 : glisser n'importe quel fichier
// -----------------------------------------------------------------------------
//  Des fichiers laches sur la fenetre (projet ouvert) qui ne sont pas tous des
//  .XPG / .XHW : filesDropped (ImportWorkspace.cpp) les donne ici. Le plan
//  (DropFilesPlan : ce qu'on peut faire de chacun, lu dans son contenu) est
//  montre par DropFilesDialog ; Faire le fait, par les chemins qui existent
//  deja, dans cet ordre :
//    1. les imports, une commande chacun (un Ctrl+Z) : les variables IHM (le
//       collage des tableaux, comme Ctrl+V dans la table), les traductions,
//       les jeux d'une recette, les lignes d'une table d'animation ;
//    2. les ajouts : Ressources, Fichiers externes (comme leurs volets ; deja
//       la : remplacer, garder les deux, ignorer) ; le classeur des macros ;
//    3. les macros : leur formulaire dans l'onglet Macros, le fichier deja
//       choisi, jusqu'a l'apercu - jamais Appliquer. Plusieurs : l'une apres
//       l'autre (Fermer ou Annuler dans l'onglet ouvre la suivante) ;
//    4. enfin le .XPG / .XHW, le choix du lot 7 (son recapitulatif d'abord).
//  Ce qui a ete fait, une ligne par chose : dropReport() (le script depot-bilan).
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../DropFilesDialog.hpp"
#include "../DropFilesPlan.hpp"
#include "../MacrosPane.hpp"
#include "../hmi/HmiAssetPanes.hpp"
#include "../hmi/HmiImages.hpp"
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiVariablePanes.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiLanguages.hpp"
#include "../../hmi/HmiRecipes.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../project/ApiCommands.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../xls/MacroXls.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace df = dropfiles;
using ui::StatusBar;

namespace {

std::string leafName(const std::string& path) { return std::filesystem::path(path).filename().string(); }

// Le nom du projet, comme le dit la question du lot 7.
std::string dropProjectName(App& app) {
    if (!app.projectFolder().empty())
        return app.manifest().name.empty() ? leafName(app.projectFolder()) : app.manifest().name;
    if (const auto p = app.project(); p && !p->header.projectName.empty()) return p->header.projectName;
    return "le projet";
}

// Les choix du .XPG / .XHW (ProgramOption::code) ; -1 : ne pas les importer.
enum ProgramCode : int { kProgramNone = -1, kProgramBoth = 0, kProgramMast = 1, kProgramHardware = 2, kProgramSeparate = 3 };

std::string joinLines(const std::vector<std::string>& lines, const char* sep) {
    std::string out;
    for (const auto& l : lines) out += (out.empty() ? "" : sep) + l;
    return out;
}

} // namespace

// ------------------------------------------------------------- le dialogue ----
void MainAnalysisScreen::askDroppedFiles(std::vector<std::string> paths) {
    if (!app_.project() || !app_.document() || paths.empty()) return;
    df::Context ctx;
    ctx.libsRoot = project::SharedLibrary::defaultRoot();
    ctx.plc = app_.project().get();
    const auto doc = app_.hmi();
    if (doc) ctx.hmi = &doc->project;
    ctx.projectFolder = app_.projectFolder();
    ctx.macroWorkbook = xls::MacroXls::instance().loadedWorkbook();
    auto plan = std::make_shared<df::Plan>(df::analyse(paths, ctx));
    plan->projectName = dropProjectName(app_);

    // Rien d'autre qu'un .XPG / .XHW de lisible (le reste : un dossier, un
    // fichier absent) : la question du lot 7, telle quelle.
    if (plan->workbooks.empty() && plan->files.empty()) {
        if (!plan->programs.empty() || !plan->hardware.empty()) {
            std::vector<std::string> known = plan->programs;
            known.insert(known.end(), plan->hardware.begin(), plan->hardware.end());
            filesDropped(std::move(known));
        } else if (status_) {
            status_->setTransientMessage("Rien \xC3\xA0 faire de " + joinLines(plan->ignored, ", ") + " (un dossier, un fichier introuvable).",
                                         8.0, StatusBar::Severity::Warning);
        }
        return;
    }

    // Le .XPG / .XHW : les choix du lot 7, dans leur section.
    const std::string xpg = plan->programs.empty() ? std::string{} : plan->programs.front();
    const std::string xhw = plan->hardware.empty() ? std::string{} : plan->hardware.front();
    const std::string name = plan->projectName;
    const auto option = [&](int code, std::string label, std::string detail) {
        df::ProgramOption o;
        o.code = code;
        o.label = std::move(label);
        o.detail = std::move(detail);
        plan->programOptions.push_back(std::move(o));
    };
    if (!xpg.empty() && !xhw.empty())
        option(kProgramBoth, "Les deux : le MAST puis sa configuration",
               "Le programme de " + leafName(xpg) + " et les racks de " + leafName(xhw) + ", en une seule commande ; un r\xC3\xA9"
               "capitulatif d'abord.");
    if (!xpg.empty())
        option(kProgramMast, "Importer comme nouveau MAST (remplace le programme API)",
               "Un r\xC3\xA9" "capitulatif montre d'abord ce qui est gard\xC3\xA9, chang\xC3\xA9, supprim\xC3\xA9 ; l'IHM, les versions et le "
               "mat\xC3\xA9riel restent. Ctrl+Z annule l'import.");
    if (!xhw.empty())
        option(kProgramHardware, "Importer la configuration mat\xC3\xA9rielle (.XHW : racks, modules, voies)",
               "Remplace les racks et les modules du projet ; le programme ne change pas. Ctrl+Z la retire.");
    if (!xpg.empty())
        option(kProgramSeparate, "Ouvrir comme un projet s\xC3\xA9par\xC3\xA9",
               "Ferme " + name + " (ses modifications non enregistr\xC3\xA9" "es se demandent d'abord) et ouvre " + leafName(xpg)
                   + (xhw.empty() ? std::string{} : " avec " + leafName(xhw)) + " ; les macros coch\xC3\xA9" "es ne sont pas lanc\xC3\xA9" "es.");
    if (!plan->programOptions.empty())
        option(kProgramNone, std::string("Ne pas ") + (!xpg.empty() && !xhw.empty() ? "les importer" : "l'importer"),
               "Le reste de ce qui est coch\xC3\xA9 se fait ; " + std::string(!xpg.empty() && !xhw.empty() ? "ces fichiers restent" : "ce fichier reste")
                   + " o\xC3\xB9 " + std::string(!xpg.empty() && !xhw.empty() ? "ils sont." : "il est."));
    plan->programChoice = 0;

    // ---- Lot API 8 : glisser de fichiers, 2e partie ---- ce qu'on lache ensuite sur le
    // dialogue s'y ajoute, lu avec le meme contexte (le projet ne change pas sous un dialogue).
    auto dialog = std::make_unique<DropFilesDialog>(plan);
    dialog->setContext(ctx);
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
    app_.menus().ShowDialog(std::move(dialog), [this, plan](const menu::DialogResult& r) {
        if (!r.accepted()) {
            dropReport_.assign(1, "Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 fait.");
            if (status_) status_->setTransientMessage("D\xC3\xA9p\xC3\xB4t annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 fait.", 6.0);
            return;
        }
        runDropPlan(*plan);
    });
}

// --------------------------------------------------------------------- Faire ----
void MainAnalysisScreen::runDropPlan(const df::Plan& plan) {
    dropReport_.clear();
    dropMacros_.clear();
    dropMacroRunning_.clear();
    const auto doc = app_.hmi();
    setHmiProjectFolder(app_.projectFolder());     // les liens relatifs partent de la
    const auto report = [this](std::string line) { dropReport_.push_back(std::move(line)); };
    std::string lastPane;      // ce qu'on montre a la fin : le volet de la derniere chose faite
    bool apiTables = false;

    // ---- 1. les imports, une commande chacun --------------------------------------
    const auto runImport = [&](const df::WorkbookPlan& w, const df::Action& a) {
        const std::string from = a.sheet.empty() ? w.name : w.name + " / " + a.sheet;
        if (a.kind == df::ActionKind::AnimationTable) {
            auto plc = app_.document();
            if (!plc || a.items.empty()) return report("Table d'animation : rien \xC3\xA0 ajouter (" + from + ")");
            std::vector<project::AnimationLine> lines;
            for (std::size_t i = 0; i < a.items.size(); ++i)
                lines.push_back({a.items[i], i < a.itemHmi.size() && a.itemHmi[i]});
            const std::size_t t = a.targets.empty() ? 0 : static_cast<std::size_t>(std::max(0, a.target));
            const std::string target = t < a.targets.size() ? a.targets[t].value : std::string("+");
            if (!target.empty() && target.front() == '+') {
                const std::string tableName = target.size() > 1 ? target.substr(1) : std::string("Table");
                app_.apply(std::make_unique<project::AddAnimationTableCommand>(plc, tableName, project::defaultAnimationTableOwner(*plc),
                                                                               std::move(lines)));
                report("Table d'animation \xC2\xAB " + tableName + " \xC2\xBB cr\xC3\xA9\xC3\xA9" "e : " + std::to_string(a.items.size())
                       + " ligne(s) (" + from + ", Ctrl+Z la retire)");
            } else {
                const auto index = static_cast<std::size_t>(std::strtoull(target.c_str(), nullptr, 10));
                if (index >= plc->animationTables.size()) return report("Table d'animation introuvable (" + from + ")");
                const std::string tableName(plc->strings.text(plc->animationTables[index].name));
                app_.apply(std::make_unique<project::AddAnimationLinesCommand>(plc, index, std::move(lines)));
                report("Table d'animation \xC2\xAB " + tableName + " \xC2\xBB : " + std::to_string(a.items.size())
                       + " ligne(s) propos\xC3\xA9" "e(s), celles d\xC3\xA9j\xC3\xA0 l\xC3\xA0 ignor\xC3\xA9" "es (" + from + ", Ctrl+Z)");
            }
            apiTables = true;
            return;
        }
        if (!doc) return report(a.label + " : le projet n'a pas d'IHM");
        df::SheetData data;
        std::string why;
        if (!df::readSheet(w.path, a.sheet, data, &why)) return report(a.label + " : " + (why.empty() ? std::string("illisible") : why));
        switch (a.kind) {
            case df::ActionKind::HmiVariables: {
                // Le collage des tableaux (TablePaste) : la table des variables IHM
                // recoit le texte comme un Ctrl+V depuis Excel (un Ctrl+Z).
                openHmiPane("scripts");
                auto* scripts = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
                if (scripts) scripts->showTab(HmiScriptsPane::TabVariables);
                auto* vars = scripts ? scripts->variablesPane() : nullptr;
                if (!vars || !vars->table().pasteText(df::tabText(data), true))
                    return report("Variables IHM : la table ne prend pas de collage (" + from + ")");
                report("Variables IHM : " + std::to_string(data.rows.size()) + " ligne(s) coll\xC3\xA9" "e(s) depuis " + from
                       + " (le bandeau de la table dit ce qui est pris ; Ctrl+Z)");
                lastPane = "scripts";
                return;
            }
            case df::ActionKind::Translations: {
                hmi::Languages next = doc->project.languages;
                const auto im = hmi::importTranslations(doc->project, next, data.columns, data.rows);
                if (next == doc->project.languages)
                    return report("Traductions : rien de nouveau dans " + from
                                  + (im.warnings.empty() ? std::string{} : " (" + im.warnings.front() + ")"));
                if (auto cmd = hmi::changeProject(doc, "Importer les traductions", [&](hmi::Project& p) { p.languages = next; }))
                    app_.apply(std::move(cmd));
                std::string m = "Traductions : " + std::to_string(im.updated) + " pos\xC3\xA9" "e(s)";
                if (im.cleared) m += ", " + std::to_string(im.cleared) + " retir\xC3\xA9" "e(s)";
                if (!im.languagesAdded.empty()) m += " ; langue(s) ajout\xC3\xA9" "e(s) : " + joinLines(im.languagesAdded, ", ");
                report(m + " (" + from + ", Ctrl+Z)");
                lastPane = "langues";
                return;
            }
            case df::ActionKind::Recipe: {
                const std::size_t t = static_cast<std::size_t>(std::max(0, a.target));
                if (t >= a.targets.size()) return report("Recette : aucune recette choisie (" + from + ")");
                const auto id = static_cast<hmi::Id>(std::strtoul(a.targets[t].value.c_str(), nullptr, 10));
                const auto* recipe = doc->project.recipe(id);
                if (!recipe) return report("Recette introuvable (" + from + ")");
                const std::string text = df::csvText(data);
                hmi::Project trial = doc->project;         // un essai : un refus ne laisse pas de commande vide
                hmi::RecipeImport rep;
                std::string error;
                if (!hmi::importRecipeCsv(trial, id, text, &rep, &error)) return report("Recette " + recipe->name + " : " + error);
                const std::string recipeName = recipe->name;
                bool ok = true;
                auto cmd = hmi::changeProject(doc, "Importer des jeux dans " + recipeName, [&](hmi::Project& p) {
                    ok = hmi::importRecipeCsv(p, id, text, nullptr, nullptr);
                });
                if (!ok) return report("Recette " + recipeName + " : import refus\xC3\xA9");
                if (cmd) app_.apply(std::move(cmd));
                report("Recette \xC2\xAB " + recipeName + " \xC2\xBB : " + std::to_string(rep.added) + " jeu(x) ajout\xC3\xA9(s), "
                       + std::to_string(rep.replaced) + " remplac\xC3\xA9(s) (" + from + ", Ctrl+Z)");
                lastPane = "recettes";
                return;
            }
            default: return;
        }
    };
    for (const auto& w : plan.workbooks)
        for (const auto& a : w.actions)
            if (a.checked && a.possible && df::stageOf(a.kind) == df::Stage::Import) runImport(w, a);

    // ---- 2. les ajouts ----------------------------------------------------------------
    bool resources = false, files = false;
    const auto addExternal = [&](const std::string& path, hmi::Id existing, bool sameFile, df::Conflict conflict) {
        const std::string leaf = leafName(path);
        if (!doc) return report("Fichiers externes : le projet n'a pas d'IHM (" + leaf + ")");
        auto kind = hmi::externalKindFromPath(path);
        if (std::filesystem::path(path).extension() == ".tsv") kind = hmi::ExternalKind::Csv;   // lot API 8 : (sinon, un document)
        if (!kind) return report("Fichiers externes : " + leaf + " n'est pas un genre que l'IHM sait lier");
        const std::string folder = hmiProjectFolder();
        if (existing != hmi::kNoId && doc->project.externalFile(existing)) {
            if (sameFile || conflict == df::Conflict::Ignore)
                return report("Fichiers externes : " + leaf + " d\xC3\xA9j\xC3\xA0 li\xC3\xA9, laiss\xC3\xA9 tel quel");
            if (conflict == df::Conflict::Replace) {
                std::string linkName;
                auto cmd = hmi::changeProject(doc, "Relier le fichier externe", [&](hmi::Project& p) {
                    auto* f = p.externalFile(existing);
                    if (!f) return;
                    const auto fresh = hmi::linkExternal(p, f->name, *kind, path, f->part, folder);
                    f->kind = fresh.kind;
                    f->path = fresh.path;
                    hmi::relinkExternal(*f, folder);
                    linkName = f->name;
                });
                if (cmd) app_.apply(std::move(cmd));
                files = true;
                return report("Fichiers externes : \xC2\xAB " + linkName + " \xC2\xBB vise maintenant " + leaf + " (Ctrl+Z)");
            }
        }
        std::string made;
        auto cmd = hmi::changeProject(doc, "Lier un fichier externe", [&](hmi::Project& p) {
            auto f = hmi::linkExternal(p, {}, *kind, path, {}, folder);
            made = f.name;
            p.assets.files.push_back(std::move(f));
        });
        if (cmd) app_.apply(std::move(cmd));
        files = true;
        report("Fichiers externes : " + leaf + " li\xC3\xA9 sous le nom \xC2\xAB " + made + " \xC2\xBB (Ctrl+Z le retire)");
    };
    const auto addResource = [&](const df::FileRow& f) {
        if (!doc) return report("Ressources : le projet n'a pas d'IHM (" + f.name + ")");
        const auto* current = f.resourceId != hmi::kNoId ? doc->project.resource(f.resourceId) : nullptr;
        if (current && f.conflict == df::Conflict::Ignore)
            return report("Ressources : " + f.name + " d\xC3\xA9j\xC3\xA0 l\xC3\xA0 (\xC2\xAB " + current->name + " \xC2\xBB), laiss\xC3\xA9" "e telle quelle");
        if (current && f.conflict == df::Conflict::Replace) {
            hmi::Project scratch;
            scratch.nextId = doc->project.nextId;
            auto res = hmi::readResource(scratch, f.path, current->name);
            if (!res) return report("Ressources : " + f.name + " illisible (" + res.error().message() + ")");
            if (res->kind() != current->kind())
                return report("Ressources : \xC2\xAB " + current->name + " \xC2\xBB (" + std::string(hmi::mediaKindLabel(current->kind()))
                              + ") se remplace par une ressource du m\xC3\xAAme genre - " + f.name + " n'a pas \xC3\xA9t\xC3\xA9 pris");
            const hmi::Id id = f.resourceId;
            const std::string label = current->name;
            auto cmd = hmi::changeProject(doc, "Remplacer " + label, [&](hmi::Project& p) {
                if (auto* r = p.resource(id)) {
                    r->data = res->data;
                    r->origin = res->origin;
                    r->added = hmi::nowStamp();
                    hmi::describeResource(*r);
                }
            });
            if (cmd) app_.apply(std::move(cmd));
            resources = true;
            return report("Ressources : \xC2\xAB " + label + " \xC2\xBB remplac\xC3\xA9" "e par " + f.name + " (Ctrl+Z)");
        }
        // Nouvelle, ou "garder les deux" : un nom libre (readResource le choisit).
        auto res = hmi::readResource(doc->project, f.path);
        if (!res) return report("Ressources : " + f.name + " illisible (" + res.error().message() + ")");
        const std::string label = res->name;
        auto cmd = hmi::changeProject(doc, "Importer " + label, [&](hmi::Project& p) { p.assets.resources.push_back(*res); });
        if (cmd) app_.apply(std::move(cmd));
        resources = true;
        report("Ressources : " + f.name + " ajout\xC3\xA9" "e sous le nom \xC2\xAB " + label + " \xC2\xBB (Ctrl+Z la retire)");
    };
    for (const auto& w : plan.workbooks)
        for (const auto& a : w.actions) {
            if (!a.checked || !a.possible) continue;
            if (a.kind == df::ActionKind::ExternalFile) addExternal(w.path, hmi::kNoId, false, df::Conflict::KeepBoth);
            // Resource : jamais possible pour un classeur (grisee par le plan).
        }
    for (const auto& f : plan.files) {
        if (f.resource && f.resourceOk) addResource(f);
        if (f.file && f.fileOk) addExternal(f.path, f.fileId, f.sameFile, f.conflict);
    }
    if (resources) {
        if (auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"))) pane->refresh();
        lastPane = "ressources";
    }
    if (files) {
        if (auto* pane = dynamic_cast<HmiFilesPane*>(hmiTab("fichiers"))) pane->refresh();
        lastPane = "fichiers";
    }

    // ---- le choix du .XPG / .XHW (fait en dernier) -------------------------------------
    int program = kProgramNone;
    if ((!plan.programs.empty() || !plan.hardware.empty()) && plan.programChoice >= 0
        && static_cast<std::size_t>(plan.programChoice) < plan.programOptions.size())
        program = plan.programOptions[static_cast<std::size_t>(plan.programChoice)].code;
    const std::string xpg = plan.programs.empty() ? std::string{} : plan.programs.front();
    const std::string xhw = plan.hardware.empty() ? std::string{} : plan.hardware.front();

    // ---- 3. les macros : le classeur, puis les formulaires ------------------------------
    for (const auto& w : plan.workbooks)
        for (const auto& a : w.actions) {
            if (!a.checked || !a.possible) continue;
            if (a.kind == df::ActionKind::MacroWorkbook) {
                xls::MacroXls::instance().setLoadedWorkbook(w.path);
                report("Classeur des macros : " + w.name + " (un champ Classeur laiss\xC3\xA9 vide le lit)");
            } else if (a.kind == df::ActionKind::Macro) {
                DropMacro m;
                m.macro = a.macro;
                m.field = a.field;
                m.path = w.path;
                m.workbook = w.genre == df::Genre::Workbook;
                dropMacros_.push_back(std::move(m));
            }
        }
    if (program == kProgramSeparate && !dropMacros_.empty()) {
        report(std::to_string(dropMacros_.size()) + " macro(s) non lanc\xC3\xA9" "e(s) : le projet se ferme pour ouvrir " + leafName(xpg));
        dropMacros_.clear();
    }
    if (!lastPane.empty()) openHmiPane(lastPane);
    if (apiTables) openApiPane("tables");
    if (!dropMacros_.empty()) (void)nextDroppedMacro();

    // ---- 4. le .XPG / .XHW ------------------------------------------------------------------
    switch (program) {
        case kProgramBoth:
            report("MAST " + leafName(xpg) + " et configuration " + leafName(xhw) + " : le r\xC3\xA9" "capitulatif s'ouvre");
            importMastIntoProject(xpg, xhw);
            break;
        case kProgramMast:
            report("MAST " + leafName(xpg) + " : le r\xC3\xA9" "capitulatif s'ouvre");
            importMastIntoProject(xpg);
            break;
        case kProgramHardware:
            report("Configuration " + leafName(xhw) + " import\xC3\xA9" "e (Ctrl+Z)");
            importHardwareFromPath(xhw);
            break;
        case kProgramSeparate: {
            report("Projet s\xC3\xA9par\xC3\xA9 : " + leafName(xpg) + " s'ouvre");
            std::vector<std::string> both{xpg};
            if (!xhw.empty()) both.push_back(xhw);
            if (status_) status_->setMessage("D\xC3\xA9p\xC3\xB4t : " + joinLines(dropReport_, " \xC2\xB7 "));
            openDroppedAsProject(std::move(both));
            return;                                    // l'ecran peut partir : plus rien ici
        }
        default: break;
    }
    if (dropReport_.empty()) report("Rien n'a \xC3\xA9t\xC3\xA9 fait.");
    if (status_) status_->setMessage("D\xC3\xA9p\xC3\xB4t : " + joinLines(dropReport_, " \xC2\xB7 "));
}

// ------------------------------------------------------ les macros, une a une ----
bool MainAnalysisScreen::launchDroppedMacro(const DropMacro& m) {
    // Un .xlsx / .xlsm devient le classeur des macros : un champ Classeur vide
    // le lit, et l'OpenWorkbook('') de la macro aussi.
    if (m.workbook) xls::MacroXls::instance().setLoadedWorkbook(m.path);
    openMacros(m.macro, /*launch=*/true);
    auto* pane = macrosPane();
    if (!pane || pane->step() == 0) {
        dropReport_.push_back("Macro " + m.macro + " : son formulaire ne s'ouvre pas (" + (pane ? pane->lastMessage() : std::string("pas d'onglet Macros")) + ")");
        return false;
    }
    std::string said;
    if (!m.field.empty() && pane->step() == 1 && !pane->answer(m.field, m.path))
        said = " ; le champ \xC2\xAB " + m.field + " \xC2\xBB n'a pas pris le fichier : choisis-le dans le formulaire";
    if (pane->step() == 1) pane->goPreview();
    dropMacroRunning_ = m.macro;
    const std::string waiting = dropMacros_.empty() ? std::string{}
                                                    : " Fermer ou Annuler ouvre la suivante (" + std::to_string(dropMacros_.size()) + " en attente).";
    dropReport_.push_back("Macro " + m.macro + " : formulaire ouvert, " + leafName(m.path) + " choisi, "
                          + (pane->step() == 2 ? std::string("aper\xC3\xA7u pr\xC3\xAAt") : std::string("des questions attendent")) + said);
    if (status_)
        status_->setMessage("Macro " + m.macro + " sur " + leafName(m.path) + " : "
                            + (pane->step() == 2 ? std::string("l'aper\xC3\xA7u est l\xC3\xA0, Appliquer quand tu veux.")
                                                 : std::string("r\xC3\xA9ponds aux questions, puis Aper\xC3\xA7u."))
                            + said + waiting);
    return true;
}

bool MainAnalysisScreen::nextDroppedMacro() {
    dropMacroRunning_.clear();
    while (!dropMacros_.empty()) {
        const DropMacro m = dropMacros_.front();
        dropMacros_.erase(dropMacros_.begin());
        if (launchDroppedMacro(m)) return true;
    }
    return false;
}

void MainAnalysisScreen::tickDroppedMacros() {
    if (dropMacroRunning_.empty()) return;
    auto* pane = macrosPane();
    if (!pane) {
        // L'onglet Macros ferme : la suite s'arrete (pas d'onglet qui revient seul).
        if (!dropMacros_.empty() && status_)
            status_->setTransientMessage(std::to_string(dropMacros_.size()) + " macro(s) du d\xC3\xA9p\xC3\xB4t abandonn\xC3\xA9" "e(s) : l'onglet Macros est ferm\xC3\xA9.",
                                         8.0, StatusBar::Severity::Warning);
        dropMacros_.clear();
        dropMacroRunning_.clear();
        return;
    }
    if (pane->step() != 0) return;             // son formulaire est encore ouvert
    dropMacroRunning_.clear();
    if (!dropMacros_.empty()) (void)nextDroppedMacro();
}

} // namespace app
