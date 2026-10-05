// =============================================================================
//  app/screens/HmiRuntimeDialogs.cpp - ce que l'IHM en marche demande (lot 6)
// -----------------------------------------------------------------------------
//  LE GESTIONNAIRE DE RECETTES (un objet de la vue) CHANGE LE PROJET : ajouter
//  un jeu (son nom, les valeurs de l'installation proposees), le modifier, le
//  supprimer, y ranger les valeurs lues. Chaque changement est une commande de
//  projet : le volet Recettes le montre aussitot, Ctrl+Z le reprend.
//
//  "DEMANDER UNE RESSOURCE" : le fichier est choisi (filtre d'extensions),
//  copie dans le projet comme par le gestionnaire de ressources, et son nom va
//  dans la variable de l'action.
// =============================================================================
#include "Screens.hpp"
#include "../App.hpp"
#include "../ExportTarget.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiCrypto.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiPolicy.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace app {

namespace {

std::string cleanQuotes(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '"')) s.pop_back();
    return s;
}

// Lot 7 : le crochet des exports en un clic de l'IHM (ExportTarget.hpp) - l'ecran
// d'analyse, s'il est au-dessus, demande ou. Pose au chargement du programme (ce
// fichier n'est lie que dans l'application), comme celui de Renommer.
const bool kExportTargetHook = [] {
    exportTargetHook() = [](const std::string& what, const std::string& filter, std::function<void()> then) {
        auto* manager = menu::MenuManager::instance();
        auto* screen = manager ? dynamic_cast<MainAnalysisScreen*>(manager->top()) : nullptr;
        return screen && screen->askHmiExportTarget(what, filter, std::move(then));
    };
    return true;
}();

std::string utf8Of(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string uniqueRecordName(const hmi::Recipe& r) {
    for (int i = static_cast<int>(r.records.size()) + 1; i < 100000; ++i) {
        const std::string candidate = "Jeu_" + std::to_string(i);
        if (!r.record(candidate)) return candidate;
    }
    return "Jeu";
}

} // namespace

void MainAnalysisScreen::askHmiRecipeRecord(const hmi::RecipeRequest& rq) {
    auto doc = app_.hmi();
    const auto* recipe = doc ? doc->project.recipeByName(rq.recipe) : nullptr;
    if (!recipe) return;
    const hmi::Id recipeId = recipe->id, recordId = rq.record, object = rq.object;
    const auto* record = recordId != hmi::kNoId ? recipe->record(recordId) : nullptr;
    const auto refreshPane = [this] {
        if (auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) pane->refreshNow();
    };

    // Lot 11 : l'editeur de recette enregistre les valeurs qu'il montre, sans dialogue.
    if (rq.op == "enregistrer") {
        if (!record) return;
        const auto values = rq.values;
        const std::string name = record->name;
        auto cmd = hmi::changeProject(doc, "Enregistrer le jeu " + name, [&](hmi::Project& p) {
            auto* r = p.recipe(recipeId);
            auto* rec = r ? r->record(recordId) : nullptr;
            if (!rec) return;
            rec->values = values;
            rec->values.resize(r->fields.size());
            rec->modified = hmi::nowStamp();
        });
        if (cmd) {
            app_.apply(std::move(cmd), false);
            status_->setTransientMessage(rq.recipe + " / " + name + " : enregistr\xC3\xA9 (Ctrl+Z pour revenir)", 8.0,
                                         ui::StatusBar::Severity::Success);
        }
        refreshPane();
        return;
    }

    if (rq.op == "lire") {
        if (!record) return;
        const auto values = rq.values;
        const std::string name = record->name;
        auto cmd = hmi::changeProject(doc, "Lire le jeu " + name, [&](hmi::Project& p) {
            auto* r = p.recipe(recipeId);
            auto* rec = r ? r->record(recordId) : nullptr;
            if (!rec) return;
            rec->values.resize(std::max(rec->values.size(), values.size()));
            // Une variable illisible garde la valeur d'avant.
            for (std::size_t k = 0; k < values.size(); ++k)
                if (!values[k].empty()) rec->values[k] = values[k];
            rec->modified = hmi::nowStamp();
        });
        if (cmd) {
            app_.apply(std::move(cmd), false);
            status_->setTransientMessage(rq.recipe + " / " + name + " : valeurs lues dans l'installation (Ctrl+Z pour revenir)", 8.0,
                                         ui::StatusBar::Severity::Success);
        } else {
            status_->setTransientMessage(rq.recipe + " / " + name + " : d\xC3\xA9j\xC3\xA0 identique \xC3\xA0 l'installation", 6.0);
        }
        refreshPane();
        return;
    }

    if (rq.op == "supprimer") {
        if (!record) return;
        const std::string name = record->name;
        app_.menus().ShowDialog(
            std::make_unique<MessageDialog>("Supprimer le jeu ?",
                                            "Le jeu \xC2\xAB " + name + " \xC2\xBB de la recette \xC2\xAB " + rq.recipe
                                                + " \xC2\xBB est retir\xC3\xA9 du projet. Ctrl+Z le rend.",
                                            MessageDialog::Icon::Question, "Supprimer"),
            [this, recipeId, recordId, name, refreshPane](const menu::DialogResult& r) {
                auto current = app_.hmi();
                if (!r.accepted() || !current) return;
                auto cmd = hmi::changeProject(current, "Supprimer le jeu " + name, [&](hmi::Project& p) {
                    if (auto* rc = p.recipe(recipeId))
                        rc->records.erase(std::remove_if(rc->records.begin(), rc->records.end(),
                                                         [&](const hmi::RecipeRecord& x) { return x.id == recordId; }),
                                          rc->records.end());
                });
                if (cmd) app_.apply(std::move(cmd), false);
                status_->setTransientMessage("Jeu " + name + " supprim\xC3\xA9 (Ctrl+Z le rend)", 6.0);
                refreshPane();
            });
        return;
    }

    // Ajouter / modifier : le nom, puis une valeur par element (une expression :
    // 7.5, 'Azote', T#5s), les bornes rappelees.
    const bool create = rq.op == "ajouter";
    if (!create && !record) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom du jeu", create ? uniqueRecordName(*recipe) : record->name, "unique dans la recette", false, {}});
    for (std::size_t k = 0; k < recipe->fields.size(); ++k) {
        const auto& f = recipe->fields[k];
        std::string label = f.name + (f.unit.empty() ? std::string{} : " (" + f.unit + ")");
        std::string value = create ? (k < rq.values.size() ? rq.values[k] : std::string{})
                                   : (k < record->values.size() ? record->values[k] : std::string{});
        std::string hint = !f.min.empty() || !f.max.empty()
                               ? "de " + (f.min.empty() ? std::string("-") : f.min) + " \xC3\xA0 " + (f.max.empty() ? std::string("-") : f.max)
                               : std::string("7.5, 'texte', T#5s");
        fields.push_back({std::move(label), std::move(value), std::move(hint), false, {}});
    }
    auto dialog = std::make_unique<FormDialog>(
        create ? "dialog.hmiRecipeAdd" : "dialog.hmiRecipeEdit",
        create ? "Nouveau jeu : " + rq.recipe : "Modifier " + rq.recipe + " / " + record->name,
        create ? std::string("Les valeurs propos\xC3\xA9" "es sont celles de l'installation en ce moment (le gestionnaire les a lues). "
                             "Le jeu est ajout\xC3\xA9 au projet : le volet Recettes le montre, Ctrl+Z le retire.")
               : std::string("Le jeu est modifi\xC3\xA9 dans le projet ; Appliquer l'\xC3\xA9" "crira dans l'installation. Ctrl+Z reprend."),
        std::move(fields), create ? "Ajouter" : "Enregistrer");
    const std::vector<std::string> taken = [&] {
        std::vector<std::string> names;
        for (const auto& rec : recipe->records)
            if (rec.id != recordId) names.push_back(rec.name);
        return names;
    }();
    dialog->setRules([taken](const std::vector<std::string>& values, std::vector<FormDialog::FieldState>& state) {
        if (values.empty() || state.empty()) return;
        const bool clash = std::find(taken.begin(), taken.end(), values[0]) != taken.end();
        state[0].hint = values[0].empty() ? std::string("un jeu a un nom") : clash ? std::string("d\xC3\xA9j\xC3\xA0 pris dans la recette") : std::string{};
    });
    app_.menus().ShowDialog(std::move(dialog), [this, create, recipeId, recordId, object, taken, refreshPane](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto v = FormDialog::split(r.payload);
        if (v.empty()) return;
        const std::string name = v[0];
        if (name.empty() || std::find(taken.begin(), taken.end(), name) != taken.end()) {
            status_->setTransientMessage(name.empty() ? std::string("Un jeu a un nom : rien n'est chang\xC3\xA9")
                                                      : "Le jeu " + name + " existe d\xC3\xA9j\xC3\xA0 : rien n'est chang\xC3\xA9",
                                         8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        hmi::Id made = hmi::kNoId;
        auto cmd = hmi::changeProject(current, create ? "Ajouter le jeu " + name : "Modifier le jeu " + name, [&](hmi::Project& p) {
            auto* rc = p.recipe(recipeId);
            if (!rc) return;
            std::vector<std::string> values(v.begin() + 1, v.end());
            values.resize(rc->fields.size());
            if (create) {
                hmi::RecipeRecord rec;
                rec.id = p.allocate();
                rec.name = name;
                rec.values = std::move(values);
                rec.modified = hmi::nowStamp();
                made = rec.id;
                rc->records.push_back(std::move(rec));
            } else if (auto* rec = rc->record(recordId)) {
                rec->name = name;
                rec->values = std::move(values);
                rec->modified = hmi::nowStamp();
            }
        });
        if (cmd) app_.apply(std::move(cmd), false);
        // Le nouveau jeu devient le jeu choisi du gestionnaire.
        if (auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); pane && made != hmi::kNoId)
            pane->runtime().selectRecipeRecord(object, made);
        status_->setTransientMessage((create ? "Jeu ajout\xC3\xA9 : " : "Jeu modifi\xC3\xA9 : ") + name + " (Ctrl+Z pour revenir)", 6.0,
                                     ui::StatusBar::Severity::Success);
        refreshPane();
    });
}

bool MainAnalysisScreen::writeHmiExport(const hmi::ExportRequest& rq, std::string* where) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string projectFolder = app_.projectFolder();
    const fs::path folder = projectFolder.empty() ? fs::temp_directory_path(ec) / "xpg-exports" : fs::path(projectFolder) / "exports";
    fs::create_directories(folder, ec);
    // Lot 7 : l'endroit choisi dans le dialogue d'un export en un clic
    // (askHmiExportTarget) - un dossier : le nom habituel dedans ; un fichier : ce
    // nom-la (l'extension du format ajoutee s'il n'en a pas) ; un nom seul : dans exports/.
    // Lot API 8 : la regle est celle du poste aussi (ExportTarget.hpp, exportFileFor).
    const fs::path file = exportFileFor(folder, rq.fileName, exportTargetOverride());
    const fs::path usual = exportPathOf(rq.fileName);
    const bool elsewhere = file.parent_path().lexically_normal() != folder.lexically_normal() || file.filename() != usual.filename();
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out || !rq.data) {
        if (where) *where = "\xC3\xA9" "criture impossible : " + utf8Of(file);
        return false;
    }
    out.write(reinterpret_cast<const char*>(rq.data->data()), static_cast<std::streamsize>(rq.data->size()));
    out.close();
    if (!out) {
        if (where) *where = "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + utf8Of(file);
        return false;
    }
    // Un chemin complet (Exporter les vues, choisi dans l'explorateur ; ailleurs
    // que dans exports/ ou sous un autre nom) : dit tel quel.
    if (where) *where = projectFolder.empty() || usual.is_absolute() || elsewhere ? utf8Of(file) : "exports/" + rq.fileName;
    return true;
}

// ---- Lot API 8 : les exports qui demandent ou (la fin) ----
namespace {

// Le dialogue du lot 7 : "Dossier ou fichier" (`proposed` ; vide : exports/ du
// projet), le bouton ... (l'explorateur, "Enregistrer sous"), Exporter. Accepte :
// `then` exporte, la cible posee le temps de l'appel (exportTargetOverride) ;
// Annuler, Echap : `cancelled`. Faux : pas de dossier de projet (personne n'a
// ou proposer exports/) - l'appelant exporte alors tout de suite, comme avant.
bool showExportTarget(App& app, const std::string& what, const std::string& filter, const std::string& proposed,
                      std::function<void()> then, std::function<void()> cancelled) {
    namespace fs = std::filesystem;
    if (app.projectFolder().empty() || !then) return false;       // pas de dossier : exports/ du dossier temporaire, comme avant
    const fs::path folder = fs::path(app.projectFolder()) / "exports";
    const std::string shown = !proposed.empty() ? proposed : utf8Of(folder) + std::string(1, static_cast<char>(fs::path::preferred_separator));
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Dossier ou fichier", shown, "", false, {}});
    app.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiExportTarget", "Exporter " + what,
                                   "Par d\xC3\xA9" "faut dans exports/ du projet, sous son nom habituel (Entr\xC3\xA9" "e). Le bouton \xE2\x80\xA6 "
                                   "choisit un autre dossier, ou un autre nom ; un dossier tap\xC3\xA9 garde le nom habituel.",
                                   std::move(fields), "Exporter"),
                               0, ui::saveFile(filter, proposed.empty() ? utf8Of(folder) : proposed, "Exporter " + what)),
        [then = std::move(then), cancelled = std::move(cancelled)](const menu::DialogResult& r) {
            if (!r.accepted()) {
                if (cancelled) cancelled();
                return;
            }
            const auto v = FormDialog::split(r.payload);
            exportTargetOverride() = v.empty() ? std::string{} : v.front();
            then();                             // le volet exporte : writeHmiExport prend la cible
            exportTargetOverride().clear();
        });
    return true;
}

} // namespace

bool MainAnalysisScreen::askHmiExportTarget(const std::string& what, const std::string& filter, std::function<void()> then) {
    return showExportTarget(app_, what, filter, {}, std::move(then), {});
}

bool MainAnalysisScreen::askHmiRuntimeExport(const hmi::ExportRequest& rq, std::function<void(bool, const std::string&)> done) {
    namespace fs = std::filesystem;
    if (app_.projectFolder().empty() || !done) return false;
    // Le fichier propose en entier (exports/<nom habituel>) : on voit son nom.
    const std::string proposed = exportUtf8Of(fs::path(app_.projectFolder()) / "exports" / exportPathOf(rq.fileName));
    const auto request = std::make_shared<const hmi::ExportRequest>(rq);
    const auto answer = std::make_shared<std::function<void(bool, const std::string&)>>(std::move(done));
    return showExportTarget(
        app_, exportWhatOf(rq.source, rq.format), exportFilterOf(rq.format), proposed,
        [this, request, answer] {
            std::string where;
            const bool ok = writeHmiExport(*request, &where);     // la cible choisie : exportTargetOverride
            (*answer)(ok, where);
        },
        [answer] { (*answer)(false, {}); });
}
// ---- fin Lot API 8 ----

void MainAnalysisScreen::askHmiRuntimeResource(const hmi::ResourceRequest& rq) {
    auto doc = app_.hmi();
    if (!doc) return;
    std::string filter;
    for (std::size_t k = 0; k < rq.extensions.size(); ++k) filter += (k ? ", " : "") + ("*." + rq.extensions[k]);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier", "", filter.empty() ? std::string("C:\\Images\\photo.png") : filter, false, {}});
    fields.push_back({"Nom dans le projet", "", "vide : le nom du fichier", false, {}});
    auto dialog = std::make_unique<FormDialog>(
        "dialog.hmiRuntimeResource", "Ajouter une ressource",
        (filter.empty() ? std::string("Tout fichier que l'IHM sait lire.") : "Extensions accept\xC3\xA9" "es : " + filter + ".")
            + " Le fichier est copi\xC3\xA9 dans le projet (ihm/ressources)"
            + (rq.variable.empty() ? std::string(".") : " et son nom va dans " + rq.variable + ".") + " Ctrl+Z retire l'ajout.",
        std::move(fields), "Ajouter");
    // Le bouton ... : l'explorateur, filtre sur les extensions de l'action.
    {
        std::string patterns;
        for (const auto& e : rq.extensions) patterns += (patterns.empty() ? "*." : ";*.") + e;
        dialog->setFieldBrowse(0, ui::openFile(patterns.empty() ? std::string("*") : "Ressources accept\xC3\xA9" "es|" + patterns,
                                               app_.projectFolder(), "Ajouter une ressource"));
    }
    const auto extensions = rq.extensions;
    // Un fichier hors du filtre se voit tout de suite : le second champ s'eteint
    // et dit pourquoi (Ajouter le refuserait).
    dialog->setRules([extensions, filter](const std::vector<std::string>& values, std::vector<FormDialog::FieldState>& state) {
        if (values.empty() || state.size() < 2) return;
        const std::string path = cleanQuotes(values[0]);
        const bool refused = !path.empty() && !hmi::extensionAccepted(extensions, path);
        state[1].enabled = !refused;
        state[1].placeholder = refused ? "extension refus\xC3\xA9" "e par le filtre (" + filter + ")" : std::string("vide : le nom du fichier");
    });
    app_.menus().ShowDialog(std::move(dialog), [this, rq](const menu::DialogResult& r) {
        auto current = app_.hmi();
        auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (!current || !pane) return;
        auto& rt = pane->runtime();
        const auto v = FormDialog::split(r.payload);
        const std::string path = v.empty() ? std::string{} : cleanQuotes(v[0]);
        if (!r.accepted() || path.empty()) {
            rt.resourceProvided(rq, {}, rt.now());
            pane->refreshNow();
            return;
        }
        if (!hmi::extensionAccepted(rq.extensions, path)) {
            status_->setTransientMessage("Ressource refus\xC3\xA9" "e : l'extension n'est pas dans le filtre de l'action", 8.0,
                                         ui::StatusBar::Severity::Warning);
            rt.resourceProvided(rq, {}, rt.now());
            pane->refreshNow();
            return;
        }
        auto res = hmi::readResource(current->project, path, v.size() > 1 ? v[1] : std::string{});
        if (!res) {
            status_->setTransientMessage("Ressource impossible \xC3\xA0 lire : " + res.error().message(), 8.0, ui::StatusBar::Severity::Warning);
            rt.resourceProvided(rq, {}, rt.now());
            pane->refreshNow();
            return;
        }
        const std::string name = res->name;
        auto cmd = hmi::changeProject(current, "Ajouter " + name, [&](hmi::Project& p) { p.assets.resources.push_back(*res); });
        if (cmd) app_.apply(std::move(cmd), false);
        rt.resourceProvided(rq, name, rt.now());
        status_->setTransientMessage("Ressource ajout\xC3\xA9" "e : " + name + (rq.variable.empty() ? std::string{} : " \xE2\x86\x92 " + rq.variable),
                                     6.0, ui::StatusBar::Severity::Success);
        pane->refreshNow();
    });
}

// ---- lot 8 : la gestion des utilisateurs, en marche ---------------------------------
//  Comme les recettes : chaque changement est une commande de projet (le volet
//  Utilisateurs le montre, Ctrl+Z le reprend). Un mot de passe n'est jamais
//  garde en clair : son empreinte salee.
void MainAnalysisScreen::askHmiUserRecord(const hmi::UserRequest& rq) {
    auto doc = app_.hmi();
    if (!doc) return;
    const hmi::Id userId = rq.user;
    const auto* user = userId != hmi::kNoId ? doc->project.user(userId) : nullptr;
    const auto refreshPane = [this] {
        if (auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) pane->refreshNow();
    };
    const auto say = [this](const std::string& m, bool ok = true) {
        status_->setTransientMessage(m, 8.0, ok ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
    };

    // Lot 13 : la date du jour, pour la peremption des mots de passe.
    const auto today = [this] {
        if (auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")))
            return hmi::dayOf(pane->runtime().dateStampOf(pane->runtime().now()));
        return hmi::dayOf(hmi::wallStamp());
    };
    // Lot 13 : une ligne du journal d'audit, par le moteur (sa chaine).
    const auto audit = [this](const std::string& kind, const std::string& target, const std::string& after) {
        if (auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) pane->runtime().recordAudit(kind, "dialogue", target, {}, after);
    };

    if (rq.op == "changer" || rq.op == "renouveler") {
        // Le mot de passe de l'utilisateur connecte (ou a renouveler), verifie par
        // le moteur (ancien, politique, confirmation) et deja chiffre. Lot 13 : il
        // rejoint l'historique des mots de passe, avec sa date.
        if (!user) return;
        const std::string login = user->login, salt = rq.salt, hash = rq.hash, day = today();
        auto cmd = hmi::changeProject(doc, "Mot de passe de " + login, [&](hmi::Project& p) {
            if (auto* u = p.user(userId)) hmi::storePassword(p.security, *u, salt, hash, day, false);
        });
        if (cmd) app_.apply(std::move(cmd), false);
        say("Mot de passe de " + login + (rq.op == "renouveler" ? " renouvel\xC3\xA9" : " chang\xC3\xA9") + " (Ctrl+Z pour revenir)");
        refreshPane();
        return;
    }
    if (rq.op == "activer") {
        if (!user) return;
        const std::string login = user->login;
        const bool enabled = rq.enabled;
        auto cmd = hmi::changeProject(doc, std::string(enabled ? "Activer " : "D\xC3\xA9sactiver ") + login, [&](hmi::Project& p) {
            if (auto* u = p.user(userId)) u->enabled = enabled;
        });
        if (cmd) app_.apply(std::move(cmd), false);
        say(login + (enabled ? " activ\xC3\xA9" : " d\xC3\xA9sactiv\xC3\xA9") + " (Ctrl+Z pour revenir)");
        refreshPane();
        return;
    }
    // Lot 12 : le menu de connexion - le groupe d'un compte, les roles d'un groupe,
    // la deconnexion automatique du projet ; des commandes annulables aussi.
    if (rq.op == "groupe") {
        if (!user) return;
        const auto* group = doc->project.group(rq.group);
        if (!group) return;
        const std::string login = user->login, groupName = group->name;
        const hmi::Id groupId = rq.group;
        auto cmd = hmi::changeProject(doc, "Groupe de " + login, [&](hmi::Project& p) {
            if (auto* u = p.user(userId)) u->group = groupId;
        });
        if (cmd) app_.apply(std::move(cmd), false);
        say(login + " passe dans le groupe " + groupName + " (Ctrl+Z pour revenir)");
        refreshPane();
        return;
    }
    if (rq.op == "role") {
        const auto* group = doc->project.group(rq.group);
        if (!group || !doc->project.role(rq.role)) return;
        const std::string groupName = group->name, role = rq.role;
        const hmi::Id groupId = rq.group;
        const bool give = rq.enabled;
        auto cmd = hmi::changeProject(doc, std::string(give ? "Donner le r\xC3\xB4le " : "Retirer le r\xC3\xB4le ") + role + " \xC3\xA0 " + groupName,
                                      [&](hmi::Project& p) {
            for (auto& g : p.security.groups) {
                if (g.id != groupId) continue;
                std::erase(g.roles, role);
                if (!give) continue;
                // Dans l'ordre des roles du projet.
                g.roles.push_back(role);
                const auto rank = [&](const std::string& name) {
                    for (std::size_t i = 0; i < p.security.roles.size(); ++i) if (p.security.roles[i].name == name) return i;
                    return p.security.roles.size();
                };
                std::stable_sort(g.roles.begin(), g.roles.end(), [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
            }
        });
        if (cmd) app_.apply(std::move(cmd), false);
        say(groupName + (give ? " re\xC3\xA7oit le r\xC3\xB4le " : " perd le r\xC3\xB4le ") + role + " (Ctrl+Z pour revenir)");
        refreshPane();
        return;
    }
    if (rq.op == "deconnexion") {
        const int minutes = std::max(0, rq.minutes);
        auto cmd = hmi::changeProject(doc, "D\xC3\xA9" "connexion automatique", [&](hmi::Project& p) { p.security.autoLogoutMin = minutes; });
        if (cmd) app_.apply(std::move(cmd), false);
        say("D\xC3\xA9" "connexion automatique : " + (minutes == 0 ? std::string("jamais") : std::to_string(minutes) + " min") + " (Ctrl+Z pour revenir)");
        refreshPane();
        return;
    }
    if (rq.op == "supprimer") {
        if (!user) return;
        const std::string login = user->login;
        app_.menus().ShowDialog(
            std::make_unique<MessageDialog>("Supprimer l'utilisateur ?",
                                            "L'utilisateur \xC2\xAB " + login + " \xC2\xBB est retir\xC3\xA9 du projet IHM. Ctrl+Z le rend.",
                                            MessageDialog::Icon::Question, "Supprimer"),
            [this, userId, login, refreshPane](const menu::DialogResult& r) {
                auto current = app_.hmi();
                if (!r.accepted() || !current) return;
                auto cmd = hmi::changeProject(current, "Supprimer l'utilisateur " + login, [&](hmi::Project& p) {
                    std::erase_if(p.security.users, [&](const hmi::User& u) { return u.id == userId; });
                });
                if (cmd) app_.apply(std::move(cmd), false);
                status_->setTransientMessage("Utilisateur " + login + " supprim\xC3\xA9 (Ctrl+Z le rend)", 6.0);
                refreshPane();
            });
        return;
    }
    std::vector<std::string> groups;
    for (const auto& g : doc->project.security.groups) groups.push_back(g.name);
    const std::string rules = hmi::passwordRules(doc->project.security, 4);        // lot 13
    if (rq.op == "motdepasse") {
        if (!user) return;
        const std::string login = user->login;
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Nouveau mot de passe", "", rules, true, {}});
        fields.push_back({"Confirmation", "", "le m\xC3\xAAme", true, {}});
        app_.menus().ShowDialog(
            std::make_unique<FormDialog>("dialog.hmiUserPassword", "Mot de passe de " + login,
                                         "L'administrateur donne un nouveau mot de passe. Il est gard\xC3\xA9 sous forme "
                                         "d'empreinte sal\xC3\xA9" "e, jamais en clair. Ctrl+Z reprend.",
                                         std::move(fields), "Enregistrer"),
            [this, userId, login, refreshPane, today, audit](const menu::DialogResult& r) {
                auto current = app_.hmi();
                if (!r.accepted() || !current) return;
                const auto v = FormDialog::split(r.payload);
                const std::string pwd = v.size() > 0 ? v[0] : std::string{};
                // Lot 13 : la politique des mots de passe du projet (4 caracteres au moins ici).
                const auto* target = current->project.user(userId);
                std::string problem = pwd.size() < 4 ? std::string("mot de passe trop court : 4 caract\xC3\xA8res au moins")
                                                     : hmi::passwordProblem(current->project.security, target, pwd, 4);
                if (problem.empty() && (v.size() < 2 || v[1] != pwd)) problem = "la confirmation ne correspond pas";
                if (!problem.empty()) {
                    status_->setTransientMessage("Mot de passe de " + login + " : " + problem + " (rien n'est chang\xC3\xA9)", 8.0,
                                                 ui::StatusBar::Severity::Warning);
                    return;
                }
                const std::string salt = hmi::randomHex(16), hash = hmi::passwordHash(salt, pwd), day = today();
                auto cmd = hmi::changeProject(current, "Mot de passe de " + login, [&](hmi::Project& p) {
                    if (auto* u = p.user(userId)) hmi::storePassword(p.security, *u, salt, hash, day, true);
                });
                if (cmd) app_.apply(std::move(cmd), false);
                const bool first = current->project.security.pwChangeFirst;
                audit("Mot de passe", login, std::string("donn\xC3\xA9 par un administrateur") + (first ? " (\xC3\xA0 changer \xC3\xA0 la connexion)" : ""));
                status_->setTransientMessage("Mot de passe de " + login + " d\xC3\xA9" "fini"
                                                 + (first ? std::string(" : il le changera \xC3\xA0 sa premi\xC3\xA8re connexion") : std::string{})
                                                 + " (Ctrl+Z pour revenir)",
                                             8.0, ui::StatusBar::Severity::Success);
                refreshPane();
            });
        return;
    }
    // Ajouter / modifier : l'identifiant (a la creation), le nom, le groupe,
    // actif ; a la creation, le mot de passe.
    const bool create = rq.op == "ajouter";
    if (!create && !user) return;
    std::vector<FormDialog::Field> fields;
    if (create) fields.push_back({"Identifiant", hmi::uniqueLogin(doc->project, "operateur"), "lettres, chiffres, _", false, {}});
    fields.push_back({"Nom complet", create ? std::string{} : user->fullName, "Pr\xC3\xA9nom Nom", false, {}});
    std::string groupName;
    if (!create)
        if (const auto* g = doc->project.group(user->group)) groupName = g->name;
    if (groupName.empty() && !groups.empty()) groupName = groups.front();
    fields.push_back({"Groupe", groupName, "", false, groups});
    fields.push_back({"Actif", create || user->enabled ? "Oui" : "Non", "", false, {"Oui", "Non"}});
    if (create) {
        fields.push_back({"Mot de passe", "", rules, true, {}});
        fields.push_back({"Confirmation", "", "le m\xC3\xAAme", true, {}});
    }
    auto dialog = std::make_unique<FormDialog>(
        create ? "dialog.hmiUserAdd" : "dialog.hmiUserEdit", create ? std::string("Nouvel utilisateur") : "Modifier " + user->login,
        create ? std::string("L'utilisateur est ajout\xC3\xA9 au projet IHM (Configuration > Utilisateurs), avec un mot de passe "
                             "gard\xC3\xA9 sous forme d'empreinte sal\xC3\xA9" "e. Ctrl+Z le retire.")
               : std::string("Le changement est fait dans le projet ; le volet Utilisateurs le montre. Ctrl+Z reprend."),
        std::move(fields), create ? "Ajouter" : "Enregistrer");
    app_.menus().ShowDialog(std::move(dialog), [this, create, userId, refreshPane, today, audit](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto v = FormDialog::split(r.payload);
        std::size_t k = 0;
        const auto next = [&] { return k < v.size() ? v[k++] : std::string{}; };
        const std::string login = create ? next() : std::string{};
        const std::string fullName = next(), chosenGroup = next(), active = next();
        const std::string pwd = create ? next() : std::string{}, confirm = create ? next() : std::string{};
        std::string problem;
        if (create) {
            if (!hmi::isIdentifier(login)) problem = "identifiant invalide : lettres, chiffres et _ (pas de chiffre en t\xC3\xAAte)";
            else if (current->project.userByLogin(login)) problem = "l'identifiant " + login + " existe d\xC3\xA9j\xC3\xA0";
            else if (pwd.size() < 4) problem = "mot de passe trop court : 4 caract\xC3\xA8res au moins";
            else if (const std::string rule = hmi::passwordProblem(current->project.security, nullptr, pwd, 4); !rule.empty())
                problem = "mot de passe : " + rule;                    // lot 13 : la politique du projet
            else if (pwd != confirm) problem = "la confirmation ne correspond pas";
        }
        if (!problem.empty()) {
            status_->setTransientMessage("Utilisateur : " + problem + " (rien n'est chang\xC3\xA9)", 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        const auto* group = current->project.groupByName(chosenGroup);
        const hmi::Id groupId = group ? group->id : hmi::kNoId;
        const bool enabled = active != "Non";
        const std::string salt = create ? hmi::randomHex(16) : std::string{};
        const std::string hash = create ? hmi::passwordHash(salt, pwd) : std::string{};
        const std::string day = today();
        std::string label;
        auto cmd = hmi::changeProject(current, create ? "Nouvel utilisateur " + login : std::string("Modifier l'utilisateur"),
                                      [&](hmi::Project& p) {
            if (create) {
                hmi::User u;
                u.id = p.allocate();
                u.login = login;
                u.fullName = fullName;
                u.group = groupId;
                u.enabled = enabled;
                hmi::storePassword(p.security, u, salt, hash, day, true);     // lot 13 : date, premiere connexion
                label = login;
                p.security.users.push_back(std::move(u));
            } else if (auto* u = p.user(userId)) {
                u->fullName = fullName;
                if (groupId != hmi::kNoId) u->group = groupId;
                u->enabled = enabled;
                label = u->login;
            }
        });
        if (cmd) app_.apply(std::move(cmd), false);
        if (create) audit("Compte", label, "cr\xC3\xA9\xC3\xA9 (mot de passe donn\xC3\xA9 par un administrateur)");     // lot 13
        status_->setTransientMessage((create ? "Utilisateur " + label + " ajout\xC3\xA9" : label + " modifi\xC3\xA9") + " (Ctrl+Z pour revenir)",
                                     8.0, ui::StatusBar::Severity::Success);
        refreshPane();
    });
}

} // namespace app
