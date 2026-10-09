// =============================================================================
//  app/screens/ImportWorkspace.cpp - lot 7 : un .XPG dans le projet ouvert,
//  et les fichiers deposes dans la fenetre
// -----------------------------------------------------------------------------
//  IMPORTER UN NOUVEAU MAST. "Importer le .XHW" posait deja le materiel d'un
//  export dans le projet ouvert ; un .XPG, lui, ne pouvait que REMPLACER le
//  projet (l'IHM, les versions, le dossier avec). Ici le .XPG remplace le
//  programme - le cote API - et le projet garde le reste. Avant d'agir, le
//  recapitulatif (ImportMastDialog) dit ce qui est garde, change, supprime,
//  nouveau, et ce que l'IHM ne retrouvera plus ; puis la version "Avant import
//  du MAST" garde l'etat d'avant, et UNE commande importe (Ctrl+Z la reprend).
//
//  DEPOSER N'IMPORTE OU. Un .XPG, un .XHW (ou les deux) lache sur la fenetre
//  pendant qu'un projet est ouvert : App les rassemble (un depot, une
//  question) et les donne a filesDropped, qui demande quoi en faire.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../BackgroundTasks.hpp"
#include "../ImportJob.hpp"
#include "../ImportMastDialog.hpp"
#include "../ImportJobDialog.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiImportDialog.hpp"   // 1.11.2 (decision 188) : un paquet depose ouvre la fenetre d'import
#include "../hmi/HmiTreeData.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiVersions.hpp"
#include "../../project/ApiCommands.hpp"
#include "../../project/MastImport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>
#include <system_error>

namespace app {

namespace mast = project::mast;
using ui::StatusBar;

namespace {

std::string leafOf(const std::string& path) { return std::filesystem::path(path).filename().string(); }

// Un chemin colle depuis l'Explorateur arrive souvent entre guillemets.
std::string cleanPath(std::string s) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && blank(s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && blank(s[i])) ++i;
    s = s.substr(i);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

std::string projectNameOf(App& app) {
    if (!app.projectFolder().empty()) return app.manifest().name.empty() ? leafOf(app.projectFolder()) : app.manifest().name;
    if (const auto p = app.project(); p && !p->header.projectName.empty()) return p->header.projectName;
    return "le projet";
}

// ---------------------------------------------- ce que l'IHM lit de l'automate --
//  LES MEMES ENDROITS QUE hmitree::usedVariables (l'arbre, l'onglet Variables
//  employees), mais chacun avec SON LIEU : la vue et l'objet, le script,
//  l'alarme... - le recapitulatif les range ainsi (IHM > vues / variables).
//  Plus les variables IHM liees a une adresse (%MW200) et les equipements
//  simules qui "suivent l'automate". Ni les variables IHM, ni SYS.X, ni
//  Vue.Objet.Propriete, ni les locales d'un script : pas l'automate.
std::vector<mast::HmiRef> hmiReferences(const hmi::Project& p) {
    std::vector<mast::HmiRef> out;
    std::set<std::string> seen;
    std::set<std::string> locals;                 // celles du script en cours, en majuscules
    const auto rootOf = [](std::string_view path) {
        std::size_t end = 0;
        while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
        return path.substr(0, end);
    };
    // Les noms en minuscules, une fois : une IHM de mille variables et de
    // milliers d'expressions ne se relit pas nom par nom a chaque chemin.
    std::set<std::string> hmiNames, viewNames;
    for (const auto& v : p.programs.variables) hmiNames.insert(hmikit::lower(v.name));
    for (const auto& v : p.views) viewNames.insert(hmikit::lower(v.name));
    const auto isHmiVariable = [&hmiNames](std::string_view root) { return hmiNames.count(hmikit::lower(std::string(root))) > 0; };
    const auto isPublic = [&viewNames](std::string_view path) {
        const auto dot = path.find('.');
        if (dot == std::string_view::npos || path.substr(0, dot).find('[') != std::string_view::npos) return false;
        const auto root = path.substr(0, dot);
        return hmi::pub::isSysRoot(root) || viewNames.count(hmikit::lower(std::string(root))) > 0;
    };
    const auto note = [&](const std::string& where, const std::string& what, std::string_view expr) {
        for (const auto& path : hmikit::variablePaths(expr)) {
            if (hmitree::stKeyword(path) || isPublic(path)) continue;
            const auto root = rootOf(path);
            std::string upper(root);
            for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (locals.count(upper) || isHmiVariable(root)) continue;
            if (!seen.insert(where + '\n' + what + '\n' + hmikit::lower(path)).second) continue;
            out.push_back(mast::HmiRef{where, what, path, {}});
        }
    };
    const auto noteTemplate = [&](const std::string& where, const std::string& what, std::string_view text) {
        std::size_t at = 0;
        while ((at = text.find('{', at)) != std::string_view::npos) {
            const auto close = text.find('}', at);
            if (close == std::string_view::npos) break;
            auto inside = text.substr(at + 1, close - at - 1);
            if (const auto colon = inside.find(':'); colon != std::string_view::npos) inside = inside.substr(0, colon);
            note(where, what, inside);
            at = close + 1;
        }
    };
    const auto noteAction = [&](const std::string& where, const std::string& what, const hmi::Action& a) {
        note(where, what, a.watch);
        note(where, what, a.guard);
        switch (a.operation) {
            case hmi::Operation::Toggle: case hmi::Operation::Set: case hmi::Operation::Reset:
            case hmi::Operation::Increment: case hmi::Operation::Decrement: case hmi::Operation::Assign:
            case hmi::Operation::RequestResource:
                note(where, what, a.target);
                break;
            default: break;
        }
        if (a.operation == hmi::Operation::Assign || a.operation == hmi::Operation::Increment
            || a.operation == hmi::Operation::Decrement)
            note(where, what, a.value);
        if (a.operation == hmi::Operation::Log) noteTemplate(where, what, a.value);
        if (a.operation == hmi::Operation::RunScript) note(where, what, hmitree::withoutComments(a.value));
    };
    const auto noteScript = [&](const std::string& where, const hmi::Script& s) {
        if (s.lang == hmi::ScriptLang::ST) {
            const auto decl = hmitree::withoutDeclarations(hmitree::withoutComments(hmi::decl::codeOf(s)));   // 1.11.18 : le modele aussi
            locals = decl.locals;
            note(where, s.name, decl.code);
            // Les textes a trous des chaines : IHM_JOURNAL('Niveau {Cuve.niveau:0.0}').
            std::size_t at = 0;
            while ((at = decl.code.find('\'', at)) != std::string::npos) {
                const auto close = decl.code.find('\'', at + 1);
                if (close == std::string::npos) break;
                noteTemplate(where, s.name, std::string_view(decl.code).substr(at + 1, close - at - 1));
                at = close + 1;
            }
            locals.clear();
        }
        note(where, s.name, s.watch);
    };

    // Les variables IHM liees a une adresse de l'automate (ou a un nom).
    for (const auto& v : p.programs.variables)
        if (v.bound() && !v.address.empty()) out.push_back(mast::HmiRef{"Variables IHM", v.name, {}, v.address});
    for (const auto& v : p.views) {
        const std::string where = (v.role == "popup" ? "Popup " : v.role == "modele" ? "Mod\xC3\xA8le " : "Vue ") + v.name;
        for (const auto& o : v.objects) {
            const std::string what = o.name.empty() ? "objet " + std::to_string(o.id) : o.name;
            for (const auto& pr : o.props) {
                if (!pr.expr.empty()) note(where, what, pr.expr);
                else if (pr.key == "text") noteTemplate(where, what, pr.value);
                else if (pr.key == "variable" || pr.key == "auth") note(where, what, pr.value);
                else if (pr.key == "variables")
                    for (const auto& item : hmikit::splitList(pr.value)) note(where, what, item);
                else if (pr.key == "cells")
                    for (const auto& d : hmitree::drivenCells(pr.value)) {
                        if (!d.text.empty() && d.text.front() == '=') note(where, what, std::string_view(d.text).substr(1));
                        else noteTemplate(where, what, d.text);
                    }
                else if (pr.key == "states")
                    for (const auto& st : hmitree::imageStatesOf(pr.value)) note(where, what, st.first);
            }
            for (const auto& a : o.actions) noteAction(where, what, a);
        }
        for (const auto& a : v.actions) noteAction(where, "la vue", a);
        for (const auto& s : v.scripts) noteScript(where, s);
    }
    for (const auto& s : p.programs.scripts) noteScript("Scripts g\xC3\xA9n\xC3\xA9raux", s);
    for (const auto& f : p.programs.functions) {
        const auto decl = hmitree::withoutDeclarations(hmitree::withoutComments(hmi::decl::codeOf(f)));   // 1.11.18 : le modele aussi
        locals = decl.locals;
        std::string name = f.name;
        for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        locals.insert(name);
        note("Fonctions IHM", f.name, decl.code);
        locals.clear();
    }
    for (const auto& a : p.alarms) {
        note("Alarmes", a.name, a.condition);
        noteTemplate("Alarmes", a.name, a.message);
    }
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields) note("Recettes", r.name + " \xC2\xB7 " + f.name, f.variable);
    for (const auto& item : p.history.archived) note("Historiques", item, item);
    for (const auto& u : p.security.users)
        if (u.protection == "expression") note("Utilisateurs", u.login, u.expression);
    for (const auto& e : p.equipments)
        for (const auto& b : e.behaviors)
            if (b.kind == hmi::BehaviorKind::FollowPlc && !b.source.empty())
                note("\xC3\x89quipements", e.name + " (suit l'automate)", b.source);
    return out;
}

// "240 variables gardees, 3 changees, 2 supprimees, 5 nouvelles".
std::string variableCounts(const mast::Plan& plan) {
    std::array<std::size_t, mast::kStatusCount> n{};
    for (const auto& it : plan.items)
        if (it.group == mast::Group::Variables || it.group == mast::Group::Instances) ++n[static_cast<std::size_t>(it.status)];
    const auto part = [](std::size_t k, const char* one, const char* many) { return std::to_string(k) + " " + (k > 1 ? many : one); };
    return part(n[0], "variable gard\xC3\xA9" "e", "variables gard\xC3\xA9" "es") + ", "
         + part(n[1], "chang\xC3\xA9" "e", "chang\xC3\xA9" "es") + ", "
         + part(n[2], "supprim\xC3\xA9" "e", "supprim\xC3\xA9" "es") + ", "
         + part(n[3], "nouvelle", "nouvelles");
}

} // namespace

bool MainAnalysisScreen::importableFile(const std::string& path) {
    const int kind = mast::sniffFile(path);
    return kind == 1 || kind == 2;
}

// ------------------------------------------------------ Importer un .XPG... ----
void MainAnalysisScreen::askImportMast() {
    if (!app_.project() || !app_.document()) {
        if (status_) status_->setTransientMessage("Importer un .XPG : ouvre d'abord un projet.", 8.0, StatusBar::Severity::Warning);
        return;
    }
    // Le champ propose le .XPG d'ou vient le projet ; l'explorateur s'ouvre a
    // cote de lui, sinon dans le dossier du projet.
    std::string guess, start;
    for (const auto& s : app_.sourcePaths())
        if (mast::sniffFile(s) == 1) {
            guess = s;
            break;
        }
    if (!guess.empty()) start = std::filesystem::path(guess).parent_path().string();
    else if (!app_.projectFolder().empty()) start = app_.projectFolder();
    HmiAskDialog::Spec spec;
    spec.id = "dialog.importMastFile";
    spec.title = "Importer un .XPG (nouveau MAST)";
    spec.text = "Le programme export\xC3\xA9 de Control Expert (.XPG) remplace le programme API de " + projectNameOf(app_)
              + ". Un r\xC3\xA9" "capitulatif montre d'abord ce qui est gard\xC3\xA9, chang\xC3\xA9, supprim\xC3\xA9, nouveau, et les liens de "
                "l'IHM qui se perdent : rien ne change avant de confirmer.";
    HmiAskDialog::Option file;
    file.label = "Le fichier .XPG";
    file.hasField = true;
    file.field = guess;
    file.placeholder = "Le chemin du .XPG (Ctrl+V), ou le bouton \xE2\x80\xA6";
    file.browse = ui::openFile("Programme Control Expert|*.xpg;*.XPG", start, "Importer un .XPG (nouveau MAST)");
    spec.options.push_back(std::move(file));
    spec.note = "Glisser un .XPG sur la fen\xC3\xAAtre fait la m\xC3\xAAme chose.";
    spec.confirm = "Suivant";
    spec.width = 720.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto path = cleanPath(HmiAskDialog::parse(r.payload).field);
        if (path.empty()) {
            if (status_) status_->setTransientMessage("Importer un .XPG : aucun fichier choisi.", 6.0, StatusBar::Severity::Warning);
            return;
        }
        importMastIntoProject(path);
    });
}

void MainAnalysisScreen::importMastIntoProject(const std::string& path, const std::string& hardwarePath) {
    const auto current = app_.project();
    if (!current || !app_.document()) {
        if (status_) status_->setTransientMessage("Importer un .XPG : ouvre d'abord un projet.", 8.0, StatusBar::Severity::Warning);
        return;
    }
    // LOCK : dit AVANT de lire et de comparer (la commande serait refusee de toute facon).
    if (app_.manifest().state == project::State::Lock && !app_.projectFolder().empty()) {
        app_.menus().ShowDialog(std::make_unique<MessageDialog>(
                                    "Projet verrouill\xC3\xA9",
                                    "Ce projet est LOCK : personne ne le modifie, un nouveau MAST non plus.\n\n"
                                    "Projet > D\xC3\xA9verrouiller : le mot de passe, ou la cl\xC3\xA9 du PC ma\xC3\xAEtre.",
                                    MessageDialog::Icon::Warning),
                                [](const menu::DialogResult&) {});
        return;
    }
    std::vector<std::string> files{path};
    if (!hardwarePath.empty()) files.push_back(hardwarePath);
    // 1.8.0 : LA LECTURE PART A COTE, suivie par une fenetre (ImportJob,
    // ImportJobDialog) ; le recapitulatif vient quand elle a fini
    // (continueMastImport). Les verifications d'avant restent ici.
    for (std::size_t i = 0; i < files.size(); ++i) {
        const int kind = mast::sniffFile(files[i]);
        std::string why;
        if (kind < 0) why = leafOf(files[i]) + " : introuvable ou illisible";
        else if (i == 0 && kind != 1) why = leafOf(files[i]) + " n'est pas un programme export\xC3\xA9 de Control Expert (.XPG)";
        else if (i > 0 && kind != 2) why = leafOf(files[i]) + " n'est pas une configuration mat\xC3\xA9rielle (.XHW)";
        if (!why.empty()) {
            app_.menus().ShowDialog(std::make_unique<MessageDialog>("Import du MAST impossible", why + "\n\nRien n'a chang\xC3\xA9 dans le projet.",
                                                                    MessageDialog::Icon::Error),
                                    [](const menu::DialogResult&) {});
            return;
        }
    }
    startImportJob(true, std::move(files));
}

// 1.8.0 : la suite de l'import d'un .XPG, quand la lecture a cote a fini : le
// recapitulatif (les plans viennent du fil, calcules sur une copie du projet).
void MainAnalysisScreen::continueMastImport(std::shared_ptr<const domain::Project> imported, const std::string& source,
                                            std::optional<mast::Plan> keepPlan, std::optional<mast::Plan> replacePlan) {
    const auto current = app_.project();
    if (!current || !app_.document() || !imported) return;
    const auto refs = app_.hmi() ? hmiReferences(app_.hmi()->project) : std::vector<mast::HmiRef>{};

    ImportMastDialog::Spec spec;
    spec.fileName = source;
    spec.projectName = projectNameOf(app_);
    spec.keep = keepPlan ? std::move(*keepPlan) : mast::makePlan(*current, *imported, refs, mast::Options{true}, source);
    spec.replace = replacePlan ? std::move(*replacePlan) : mast::makePlan(*current, *imported, refs, mast::Options{false}, source);
    // Ce qui se passe avant l'import : la version, si le projet a un dossier.
    const std::string folder = app_.projectFolder();
    if (folder.empty()) {
        spec.versionLine = "Le projet n'a pas encore de dossier : pas de version avant l'import (Ctrl+Z l'annule tant que le projet est ouvert).";
    } else {
        const auto unsaved = app_.pendingChanges();
        auto store = hmi::ver::open(folder);
        // Enregistrer peut creer la version automatique : le numero n'est sur
        // que si rien n'est a enregistrer, ou si le projet n'en fait pas.
        const bool numbered = store && (unsaved == 0 || store->autoMode == hmi::ver::AutoMode::Never);
        spec.versionLine = std::string("Avant l'import, la version ")
                         + (numbered ? "V" + std::to_string(store->nextNumber()) + " " : std::string{})
                         + "\xC2\xAB Avant import du MAST \xC2\xBB garde le projet tel qu'il est"
                         + (unsaved > 0 ? " (enregistr\xC3\xA9 d'abord : " + std::to_string(unsaved)
                                              + (unsaved > 1 ? " modifications)" : " modification)")
                                        : std::string{})
                         + " ; Ctrl+Z annule aussi l'import.";
    }
    app_.menus().ShowDialog(std::make_unique<ImportMastDialog>(std::move(spec)),
                            [this, imported, source](const menu::DialogResult& r) {
                                if (!r.accepted()) {
                                    if (status_)
                                        status_->setTransientMessage("Import de " + source + " annul\xC3\xA9 : le projet n'a pas chang\xC3\xA9.", 6.0);
                                    return;
                                }
                                applyMastImport(imported, source, ImportMastDialog::keepFrom(r.payload));
                            });
}

// La version d'abord (le projet enregistre s'il le faut), puis l'import.
void MainAnalysisScreen::applyMastImport(std::shared_ptr<const domain::Project> imported, const std::string& source, bool keep) {
    const std::string folder = app_.projectFolder();
    if (folder.empty()) {
        runMastImport(std::move(imported), source, keep, {});
        return;
    }
    // Ce qui n'est pas enregistre doit entrer dans la version : enregistrer.
    if (app_.commands().isModified()) {
        if (auto st = app_.saveProject(); !st) {
            const auto& e = st.error();
            app_.menus().ShowDialog(std::make_unique<MessageDialog>(
                                        "Import du MAST non fait",
                                        "Le projet n'a pas pu \xC3\xAAtre enregistr\xC3\xA9 avant l'import : "
                                            + (e.context.empty() ? e.message() : e.context)
                                            + "\n\nRien n'a chang\xC3\xA9 : sans enregistrement, la version d'avant l'import ne garderait pas "
                                              "tes derni\xC3\xA8res modifications.",
                                        MessageDialog::Icon::Error),
                                    [](const menu::DialogResult&) {});
            return;
        }
    }
    std::string said, failed;
    if (auto store = hmi::ver::open(folder); !store) {
        failed = store.error().context.empty() ? store.error().message() : store.error().context;
    } else if (auto made = hmi::ver::create(*store, "Avant import du MAST", hmi::ver::State::Draft,
                                            "Cr\xC3\xA9\xC3\xA9" "e avant l'import de " + source + " (nouveau MAST)",
                                            hmi::ver::defaultAuthor());
               !made) {
        failed = made.error().context.empty() ? made.error().message() : made.error().context;
    } else {
        said = made->label();
    }
    versionWatch_.checkedAt = -100.0;          // la barre du haut relit les versions
    if (!failed.empty()) {
        auto ask = std::make_unique<MessageDialog>(
            "Version non cr\xC3\xA9\xC3\xA9" "e",
            "La version \xC2\xAB Avant import du MAST \xC2\xBB n'a pas pu \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e : " + failed
                + "\n\nImporter quand m\xC3\xAAme ? Ctrl+Z annulera l'import tant que le projet reste ouvert.",
            MessageDialog::Icon::Question, "Importer quand m\xC3\xAAme");
        app_.menus().ShowDialog(std::move(ask), [this, imported, source, keep](const menu::DialogResult& r) {
            if (r.accepted()) runMastImport(imported, source, keep, {});
        });
        return;
    }
    runMastImport(std::move(imported), source, keep, said);
}

void MainAnalysisScreen::runMastImport(std::shared_ptr<const domain::Project> imported, const std::string& source, bool keep,
                                       const std::string& versionSaid) {
    auto doc = app_.document();
    if (!doc || !imported) return;
    // Le compte rendu : le plan de ce qui va se faire, avant de le faire.
    const auto refs = app_.hmi() ? hmiReferences(app_.hmi()->project) : std::vector<mast::HmiRef>{};
    const auto plan = mast::makePlan(*doc, *imported, refs, mast::Options{keep}, source);
    // ---- Lot API 8 : le moteur de simulation (plus de retour au cycle 0) ----
    //  Le projet est modifie EN PLACE (une commande, Ctrl+Z la reprend) : la
    //  simulation n'est plus lachee. Elle prend le nouveau programme tout de
    //  suite : en marche ou en pause, la modification en ligne (le cycle, les
    //  valeurs de meme nom et de meme type, les forcages restent) ; arretee, le
    //  nouveau code. Seul un AUTRE projet (ouvrir, une version restauree) repart
    //  du cycle 0.
    const bool simulated = app_.simulation().attached();
    const bool online = simulated && (app_.simulation().state() == SimulationHost::State::Running
                                      || app_.simulation().state() == SimulationHost::State::Paused);
    const auto revision = app_.commands().revision();
    app_.apply(std::make_unique<mast::ImportMastCommand>(doc, imported, mast::Options{keep}, source));
    if (app_.commands().revision() == revision) return;      // refuse (LOCK...) : App l'a dit
    if (simulated)
        if (auto p = app_.project()) (void)app_.simulation().attach(p);
    // LES VUES SUIVENT TOUT DE SUITE. ProjectOpened n'arrive qu'a l'image
    // suivante : celle-ci se dessinerait avec les lignes du programme d'avant
    // (des indices de variables qui n'existent plus).
    if (auto p = app_.project()) bindProject(p, app_.report());
    const std::size_t lost = plan.count(mast::Status::LinkLost);
    std::string said = source + " import\xC3\xA9 (nouveau MAST) : " + variableCounts(plan);
    if (lost > 0) said += " ; " + std::to_string(lost) + (lost > 1 ? " liens IHM perdus" : " lien IHM perdu");
    if (!versionSaid.empty()) said += " ; " + versionSaid + " garde l'\xC3\xA9tat d'avant";
    said += ". Ctrl+Z annule l'import.";
    if (online && !app_.simulation().stale())
        said += " La simulation continue sur le nouveau programme, sans repartir du cycle 0.";
    else if (online)
        said += " La simulation garde l'ancien programme pour l'instant (Simulation \xE2\x80\xBA Automate dit pourquoi).";
    app_.events().publish(StatusNotice{said, 14.0});
}

// Le .XHW d'un depot : comme Configuration > Importer le .XHW, sans redemander le fichier.
void MainAnalysisScreen::importHardwareFromPath(const std::string& path) {
    if (!app_.document()) return;
    // 1.8.0 : lu a cote, suivi par sa fenetre ; pose ensuite (applyHardwareImport).
    startImportJob(false, {path});
}

void MainAnalysisScreen::applyHardwareImport(std::shared_ptr<const domain::Project> imported, const std::string& path) {
    auto doc = app_.document();
    if (!doc || !imported) return;
    const auto revision = app_.commands().revision();
    app_.apply(std::make_unique<project::ReplaceHardwareCommand>(doc, imported->hardware, leafOf(path)));
    if (app_.commands().revision() == revision) return;
    if (auto p = app_.project()) bindProject(p, app_.report());   // les racks d'avant ne se dessinent plus
    openApiPane("configuration");
    if (status_)
        status_->setMessage("Le mat\xC3\xA9riel de " + leafOf(path)
                            + " est dans le projet (Ctrl+Z le retire) ; Voies et adresses compare le code aux modules.");
}

// ------------------------------------------------ 1.8.0 : l'import suivi ----
//  La lecture, l'analyse et la comparaison partent sur un fil (ImportJob) avec
//  une COPIE du projet ouvert ; une fenetre suit (ImportJobDialog). Fini :
//  le recapitulatif (.XPG) ou la pose (.XHW), ici, sur le fil de l'interface.
//  "Continuer en arriere-plan" : la barre du haut suit ; fini, la cloche
//  previent et le recapitulatif attend qu'on le demande (rien n'est pose seul).
void MainAnalysisScreen::startImportJob(bool mast, std::vector<std::string> paths) {
    if (paths.empty()) return;
    if (importJob_) {
        if (status_)
            status_->setTransientMessage("Un import est d\xC3\xA9j\xC3\xA0 en cours (barre du haut) : attends qu'il finisse, ou annule-le.", 8.0,
                                         StatusBar::Severity::Warning);
        return;
    }
    const auto current = app_.project();
    // La copie : le fil compare avec elle, jamais avec le projet ouvert.
    std::shared_ptr<const domain::Project> snapshot = current ? std::make_shared<const domain::Project>(*current) : nullptr;
    auto refs = (mast && app_.hmi()) ? hmiReferences(app_.hmi()->project) : std::vector<mast::HmiRef>{};
    const std::string source = leafOf(paths.front());
    auto job = std::make_shared<ImportJob>(mast ? ImportJob::Kind::Mast : ImportJob::Kind::Hardware, std::move(paths), std::move(snapshot),
                                           std::move(refs), source);
    importJob_ = job;
    importBackground_ = false;
    importRevision_ = app_.commands().revision();
    importDocument_ = app_.document().get();
    importShown_ = -1.f;
    if (importTask_) bgtasks::end(importTask_);
    importTask_ = bgtasks::begin("Import de " + source, "import.cancel");
    job->start();
    importDialogOpen_ = true;
    app_.menus().ShowDialog(std::make_unique<ImportJobDialog>(job, "dans " + projectNameOf(app_)),
                            [this, job](const menu::DialogResult& r) {
                                importDialogOpen_ = false;
                                if (importJob_ != job) return;          // deja fini ou annule ailleurs
                                if (r.payload == "background") {
                                    importBackground_ = true;
                                    if (status_)
                                        status_->setTransientMessage("L'import de " + job->source()
                                                                         + " continue en arri\xC3\xA8re-plan : la barre du haut le suit, la cloche pr\xC3\xA9viendra.",
                                                                     8.0);
                                    return;
                                }
                                if (r.payload == "cancelled" && !job->done()) {
                                    job->cancel();
                                    droppedImports_.push_back(job);     // lache une fois fini, sans bloquer l'image
                                    importJob_.reset();
                                    if (importTask_) { bgtasks::end(importTask_); importTask_ = 0; }
                                    if (status_) status_->setTransientMessage("Import de " + job->source() + " annul\xC3\xA9 : le projet n'a pas chang\xC3\xA9.", 6.0);
                                    return;
                                }
                                finishImportJob();
                            });
}

void MainAnalysisScreen::finishImportJob() {
    auto job = importJob_;
    if (!job || !job->done()) return;
    importJob_.reset();
    if (importTask_) { bgtasks::end(importTask_); importTask_ = 0; }
    const bool background = importBackground_;
    importBackground_ = false;
    const auto s = job->state();
    const bool mast = job->kind() == ImportJob::Kind::Mast;
    if (s.cancelled) {
        if (status_) status_->setTransientMessage("Import de " + job->source() + " annul\xC3\xA9 : le projet n'a pas chang\xC3\xA9.", 6.0);
        return;
    }
    if (s.failed || !job->imported) {
        const std::string why = s.error.empty() ? std::string("la lecture n'a pas abouti") : s.error;
        if (background) {
            // La fenetre etait fermee : l'erreur n'a pas encore ete dite.
            bgtasks::post({"import:erreur", "Projet", "Import de " + job->source() + " impossible", why, {}, {}, "error"});
            app_.menus().ShowDialog(std::make_unique<MessageDialog>(mast ? "Import du MAST impossible" : "Import du mat\xC3\xA9riel impossible",
                                                                    why + "\n\nRien n'a chang\xC3\xA9 dans le projet.", MessageDialog::Icon::Error),
                                    [](const menu::DialogResult&) {});
        } else if (status_) {
            status_->setTransientMessage("Import de " + job->source() + " impossible : " + why, 10.0, StatusBar::Severity::Error);
        }
        return;
    }
    if (background) {
        // Rien ne se pose sans toi : le recapitulatif attend (la cloche, son bouton).
        if (importReady_) bgtasks::withdraw("import:pret");
        importReady_ = job;
        bgtasks::post({"import:pret", "Projet", "Import de " + job->source() + " pr\xC3\xAAt",
                       mast ? std::string("Lu, analys\xC3\xA9 et compar\xC3\xA9 en ") + std::to_string(static_cast<int>(s.elapsed + 0.5))
                                  + " s. Rien n'est pos\xC3\xA9 dans le projet tant que tu n'as pas valid\xC3\xA9 le r\xC3\xA9" "capitulatif."
                            : std::string("Le mat\xC3\xA9riel est lu. Il remplace la configuration du projet quand tu le poses (Ctrl+Z le retire)."),
                       mast ? "Voir le r\xC3\xA9" "capitulatif" : "Poser dans le projet", "import.recap", "ok"});
        return;
    }
    deliverImport(job);
}

void MainAnalysisScreen::deliverImport(const std::shared_ptr<ImportJob>& job) {
    if (!job || !job->imported) return;
    if (!app_.document() || app_.document().get() != importDocument_) {
        if (status_)
            status_->setTransientMessage("Un autre projet est ouvert depuis le d\xC3\xA9" "but de l'import de " + job->source()
                                             + " : il n'est pas pos\xC3\xA9. Relance-le dans ce projet.",
                                         10.0, StatusBar::Severity::Warning);
        return;
    }
    if (job->kind() == ImportJob::Kind::Mast) {
        // Les plans du fil valent si le projet n'a pas bouge depuis la copie ; sinon, recalcules.
        const bool fresh = app_.commands().revision() == importRevision_;
        continueMastImport(job->imported, job->source(), fresh ? std::move(job->keepPlan) : std::nullopt,
                           fresh ? std::move(job->replacePlan) : std::nullopt);
    } else {
        applyHardwareImport(job->imported, job->paths().front());
    }
}

void MainAnalysisScreen::pollLisibleJobs() {
    // Les imports annules : laches quand leur fil a fini (le detruire avant l'attendrait).
    std::erase_if(droppedImports_, [](const std::shared_ptr<ImportJob>& j) { return !j || j->done(); });
    if (importJob_) {
        const auto s = importJob_->state();
        if (importTask_ && std::abs(s.fraction - importShown_) >= 0.01f) {
            importShown_ = s.fraction;
            bgtasks::progress(importTask_, s.fraction);
        }
        if (importBackground_ && !importDialogOpen_ && importJob_->done()) finishImportJob();
    }
    pollExportJob();
}

// ----------------------------------------------------------- les depots ----
void MainAnalysisScreen::filesDropped(std::vector<std::string> paths) {
    if (!app_.project() || !app_.document()) return;
    // 1.11.2 (decision 188) : un paquet (.xpgvues, .xpgsymboles, .xpgtypes, .xpgfonctions, .xpgscripts) ouvre
    // la fenetre d'import (Annuler n'y fait rien) ; plusieurs : l'un apres l'autre ; le reste du depot ensuite.
    if (app_.hmi()) {
        auto packages = HmiImportDialog::takePackages(paths);
        if (!packages.empty()) {
            std::vector<std::string> rest(packages.begin() + 1, packages.end());
            rest.insert(rest.end(), paths.begin(), paths.end());
            askHmiImport(packages.front(), [this, rest]() mutable {
                if (!rest.empty()) filesDropped(std::move(rest));
            });
            return;
        }
    }
    std::vector<std::string> programs, configs, others;
    for (auto& p : paths) {
        if (p.empty()) continue;
        switch (mast::sniffFile(p)) {
            case 1:  programs.push_back(std::move(p)); break;
            case 2:  configs.push_back(std::move(p)); break;
            default: others.push_back(std::move(p)); break;
        }
    }
    // ---- Lot API 8 : glisser n'importe quel fichier ----
    //  Autre chose qu'un .XPG / .XHW dans le depot : un seul dialogue, une
    //  section par genre (DropFilesWorkspace.cpp) ; le .XPG / .XHW y a la sienne.
    if (!others.empty()) {
        std::vector<std::string> all = programs;
        all.insert(all.end(), configs.begin(), configs.end());
        all.insert(all.end(), others.begin(), others.end());
        askDroppedFiles(std::move(all));
        return;
    }
    // ---- fin Lot API 8 : glisser n'importe quel fichier ----
    if (programs.empty() && configs.empty()) {
        if (status_ && !others.empty())
            status_->setTransientMessage("D\xC3\xA9poser ici : un .XPG (le programme) ou un .XHW (la configuration mat\xC3\xA9rielle) ; "
                                             + leafOf(others.front()) + " n'en est pas un.",
                                         8.0, StatusBar::Severity::Warning);
        return;
    }
    const std::string xpg = programs.empty() ? std::string{} : programs.front();
    const std::string xhw = configs.empty() ? std::string{} : configs.front();
    const std::string name = projectNameOf(app_);

    enum class Choice : std::uint8_t { Both, Mast, Hardware, Separate };
    std::vector<Choice> choices;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.dropImport";
    spec.title = !xpg.empty() && !xhw.empty() ? "Fichiers d\xC3\xA9pos\xC3\xA9s : " + leafOf(xpg) + " et " + leafOf(xhw)
                                              : "Fichier d\xC3\xA9pos\xC3\xA9 : " + leafOf(xpg.empty() ? xhw : xpg);
    spec.text = std::string(!xpg.empty() && !xhw.empty() ? "Que faire de ces fichiers" : "Que faire de ce fichier")
              + " avec le projet ouvert (" + name + ") ?";
    const auto option = [&](Choice c, std::string label, std::string detail) {
        HmiAskDialog::Option o;
        o.label = std::move(label);
        o.detail = std::move(detail);
        spec.options.push_back(std::move(o));
        choices.push_back(c);
    };
    if (!xpg.empty() && !xhw.empty())
        option(Choice::Both, "Les deux : le MAST puis sa configuration",
               "Le programme de " + leafOf(xpg) + " et les racks de " + leafOf(xhw) + ", en une seule commande ; un r\xC3\xA9"
               "capitulatif d'abord.");
    if (!xpg.empty())
        option(Choice::Mast, "Importer comme nouveau MAST (remplace le programme API)",
               "Un r\xC3\xA9" "capitulatif montre d'abord ce qui est gard\xC3\xA9, chang\xC3\xA9, supprim\xC3\xA9 et les liens de l'IHM perdus ; "
               "l'IHM, les versions et le mat\xC3\xA9riel restent. Ctrl+Z annule l'import.");
    if (!xhw.empty())
        option(Choice::Hardware, "Importer la configuration mat\xC3\xA9rielle (.XHW : racks, modules, voies)",
               "Remplace les racks et les modules du projet ; le programme ne change pas. Ctrl+Z la retire.");
    if (!xpg.empty())
        option(Choice::Separate, "Ouvrir comme un projet s\xC3\xA9par\xC3\xA9",
               "Ferme " + name + " (ses modifications non enregistr\xC3\xA9" "es se demandent d'abord) et ouvre " + leafOf(xpg)
                   + (xhw.empty() ? std::string{} : " avec " + leafOf(xhw)) + ".");
    std::string ignored;
    const auto skip = [&](const std::vector<std::string>& list, std::size_t from) {
        for (std::size_t i = from; i < list.size(); ++i) ignored += (ignored.empty() ? "" : ", ") + leafOf(list[i]);
    };
    skip(programs, 1);
    skip(configs, 1);
    skip(others, 0);
    if (!ignored.empty()) spec.note = "Ignor\xC3\xA9" "s (un seul .XPG et un seul .XHW \xC3\xA0 la fois) : " + ignored;
    spec.confirm = "Continuer";
    spec.width = 700.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)),
                            [this, choices, xpg, xhw](const menu::DialogResult& r) {
                                if (!r.accepted()) return;
                                const auto a = HmiAskDialog::parse(r.payload);
                                if (a.option < 0 || static_cast<std::size_t>(a.option) >= choices.size()) return;
                                switch (choices[static_cast<std::size_t>(a.option)]) {
                                    case Choice::Both:     importMastIntoProject(xpg, xhw); break;
                                    case Choice::Mast:     importMastIntoProject(xpg); break;
                                    case Choice::Hardware: importHardwareFromPath(xhw); break;
                                    case Choice::Separate: {
                                        std::vector<std::string> files{xpg};
                                        if (!xhw.empty()) files.push_back(xhw);
                                        openDroppedAsProject(std::move(files));
                                        break;
                                    }
                                }
                            });
}

// Un projet a part : le projet ouvert se ferme d'abord (sinon son dossier
// resterait celui du nouveau, et Enregistrer y ecrirait l'autre programme).
void MainAnalysisScreen::openDroppedAsProject(std::vector<std::string> files) {
    if (files.empty()) return;
    App* app = &app_;
    // Ni `this` ni l'ecran : fermer le projet ramene l'accueil, et l'ecran part.
    const auto go = [app, files] {
        (void)app->actions().trigger("file.close", app->commands());
        if (files.size() == 1) app->openPath(files.front());
        else app->importer().importAsync(files);
    };
    const bool unsaved = app_.project() && !app_.projectFolder().empty() && app_.commands().isModified();
    if (!unsaved) {
        go();
        return;
    }
    const std::string name = projectNameOf(app_);
    HmiAskDialog::Spec spec;
    spec.id = "dialog.dropLeave";
    spec.title = "Modifications non enregistr\xC3\xA9" "es";
    spec.text = name + " a des modifications qui ne sont pas encore enregistr\xC3\xA9" "es. Ouvrir " + leafOf(files.front())
              + " ferme le projet : que deviennent-elles ?";
    HmiAskDialog::Option keep;
    keep.label = "Les enregistrer d'abord";
    spec.options.push_back(std::move(keep));
    HmiAskDialog::Option drop;
    drop.label = "Les abandonner";
    spec.options.push_back(std::move(drop));
    spec.confirm = "Ouvrir";
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [app, go](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        if (HmiAskDialog::parse(r.payload).option == 0) {
            if (auto st = app->saveProject(); !st) {
                app->menus().ShowDialog(std::make_unique<MessageDialog>(
                                            "Enregistrement impossible",
                                            st.error().message() + "\n\nRien n'a \xC3\xA9t\xC3\xA9 ouvert : les modifications sont toujours en m\xC3\xA9moire.",
                                            MessageDialog::Icon::Error),
                                        [](const menu::DialogResult&) {});
                return;
            }
        }
        go();
    });
}

} // namespace app
