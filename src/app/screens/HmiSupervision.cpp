// =============================================================================
//  app/screens/HmiSupervision.cpp - les dialogues du lot 4 de l'IHM
// -----------------------------------------------------------------------------
//  Alarmes, recettes, utilisateurs, historiques, exporter / importer, et la
//  connexion d'un utilisateur en simulation. Comme ailleurs dans l'IHM, le
//  volet est RETROUVE a la reponse (l'onglet a pu etre ferme pendant que le
//  dialogue etait ouvert) et les actions visent un identifiant.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../hmi/HmiDisplayPanes.hpp"
#include "../hmi/HmiCommPanes.hpp"     // lot 14
#include "../hmi/HmiStationPanes.hpp"  // lot 14 : le poste
#include "../hmi/HmiNotifyPanes.hpp"   // lot 14 : les notifications
#include "../hmi/HmiReportPanes.hpp"   // lot 14 : les rapports
#include "../hmi/HmiSimulation.hpp"
#include "../hmi/HmiSupervisionPanes.hpp"
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiHistory.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace app {

namespace {

hmi::Id asId(std::uint64_t v) { return static_cast<hmi::Id>(v); }

std::string cleanPath(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    s.erase(0, i);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// Un chemin propose : dans le dossier du projet s'il y en a un.
std::string suggested(const std::string& folder, const std::string& file) {
    if (folder.empty()) return file;
    return (std::filesystem::path(folder) / file).string();
}

// "Armoire A" -> "Armoire_A" : un nom de fichier sans surprise.
std::string fileSafe(std::string s) {
    for (auto& c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80)) c = '_';
    return s.empty() ? std::string("ihm") : s;
}

std::string cited(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size() && i < 3; ++i) out += (i ? ", " : "") + items[i];
    if (items.size() > 3) out += " (+" + std::to_string(items.size() - 3) + ")";
    return out;
}

std::vector<std::string> usesOf(const hmi::Project& p, hmi::Operation op, const std::string& target) {
    std::vector<std::string> out;
    auto low = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    for (const auto& v : p.views) {
        for (const auto& a : v.actions)
            if (a.operation == op && low(a.target) == low(target)) out.push_back(v.name);
        for (const auto& o : v.objects)
            for (const auto& a : o.actions)
                if (a.operation == op && low(a.target) == low(target)) out.push_back(v.name + "/" + o.name);
    }
    return out;
}

} // namespace

// ------------------------------------------------------------------ alarmes --
void MainAnalysisScreen::askHmiDeleteAlarm(std::uint64_t alarmId) {
    auto doc = app_.hmi();
    const auto* a = doc ? doc->project.alarm(asId(alarmId)) : nullptr;
    auto* pane = dynamic_cast<HmiAlarmsPane*>(hmiTab("alarmes"));
    if (!a || !pane) return;
    const auto uses = usesOf(doc->project, hmi::Operation::AckAlarm, a->name);
    if (uses.empty()) { (void)pane->deleteAlarm(asId(alarmId)); return; }
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer l'alarme ?",
            "\xC2\xAB " + a->name + " \xC2\xBB est acquitt\xC3\xA9" "e par " + std::to_string(uses.size()) + " action(s) : " + cited(uses)
                + ". G\xC3\xA9n\xC3\xA9rer les signalera. Ctrl+Z la rend.",
            MessageDialog::Icon::Question, "Supprimer"),
        [this, alarmId](const menu::DialogResult& r) {
            auto* current = dynamic_cast<HmiAlarmsPane*>(hmiTab("alarmes"));
            if (r.accepted() && current) (void)current->deleteAlarm(asId(alarmId));
        });
}

// ----------------------------------------------------------------- recettes --
void MainAnalysisScreen::askHmiRecipeCsv(std::uint64_t recipeId, bool import) {
    auto doc = app_.hmi();
    const auto* r = doc ? doc->project.recipe(asId(recipeId)) : nullptr;
    if (!r) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier CSV", suggested(app_.projectFolder(), fileSafe(r->name) + ".csv"), "C:\\Recettes\\gaz.csv", false, {}});
    const std::string text = import
        ? "Une ligne d'en-t\xC3\xAAte (Jeu;\xC3\xA9l\xC3\xA9ment (unit\xC3\xA9);...), puis un jeu par ligne. Les colonnes sont "
          "reconnues par le nom de l'\xC3\xA9l\xC3\xA9ment ; un jeu de m\xC3\xAAme nom est remplac\xC3\xA9, les autres sont ajout\xC3\xA9s. "
          "S\xC3\xA9parateur ; , ou tabulation. Ctrl+Z reprend tout l'import."
        : "Les " + std::to_string(r->records.size()) + " jeu(x) de \xC2\xAB " + r->name + " \xC2\xBB, une ligne chacun, "
          "s\xC3\xA9parateur ; (Excel le relit tel quel).";
    // Le bouton ... : l'explorateur (ouvrir un CSV, ou l'enregistrer sous).
    const std::string csvTitle = (import ? "Importer des jeux dans " : "Exporter ") + r->name;
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>(import ? "dialog.hmiRecipeImport" : "dialog.hmiRecipeExport",
                                                            csvTitle, text, std::move(fields), import ? "Importer" : "Exporter"),
                               0, import ? ui::openFile("Fichiers CSV|*.csv;*.txt", app_.projectFolder(), csvTitle)
                                         : ui::saveFile("Fichiers CSV|*.csv", app_.projectFolder(), csvTitle)),
        [this, recipeId, import](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"));
            if (!res.accepted() || !pane) return;
            const auto v = FormDialog::split(res.payload);
            if (v.empty() || cleanPath(v[0]).empty()) return;
            std::string why;
            const bool ok = import ? pane->importCsv(asId(recipeId), cleanPath(v[0]), &why)
                                   : pane->exportCsv(asId(recipeId), cleanPath(v[0]), &why);
            if (!ok) status_->setTransientMessage((import ? "Import impossible : " : "Export impossible : ") + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiCompareRecords(std::uint64_t recipeId) {
    auto doc = app_.hmi();
    const auto* r = doc ? doc->project.recipe(asId(recipeId)) : nullptr;
    auto* pane = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"));
    if (!r || !pane || r->records.size() < 2) return;
    std::vector<std::string> names;
    for (const auto& rec : r->records) names.push_back(rec.name);
    std::string left = names[0], right = names[1];
    if (const auto* sel = r->record(pane->selectedRecord())) {
        left = sel->name;
        right = left == names[0] ? names[1] : names[0];
    }
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Jeu A", left, "", false, names});
    fields.push_back({"Jeu B", right, "", false, names});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiRecipeCompare", "Comparer deux jeux de " + r->name,
            "Les valeurs des deux jeux, \xC3\xA9l\xC3\xA9ment par \xC3\xA9l\xC3\xA9ment ; celles qui diff\xC3\xA8rent sont "
            "marqu\xC3\xA9" "es (7 et 7.0 sont \xC3\xA9gales).",
            std::move(fields), "Comparer"),
        [this, recipeId](const menu::DialogResult& res) {
            auto* current = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"));
            auto d = app_.hmi();
            const auto* rr = d ? d->project.recipe(asId(recipeId)) : nullptr;
            if (!res.accepted() || !current || !rr) return;
            const auto v = FormDialog::split(res.payload);
            if (v.size() < 2) return;
            const auto* a = rr->record(std::string_view(v[0]));
            const auto* b = rr->record(std::string_view(v[1]));
            if (a && b) (void)current->compare(asId(recipeId), a->id, b->id);
        });
}

void MainAnalysisScreen::askHmiDeleteRecipe(std::uint64_t recipeId) {
    auto doc = app_.hmi();
    const auto* r = doc ? doc->project.recipe(asId(recipeId)) : nullptr;
    auto* pane = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"));
    if (!r || !pane) return;
    const auto uses = usesOf(doc->project, hmi::Operation::LoadRecipe, r->name);
    if (uses.empty() && r->records.empty()) { (void)pane->deleteRecipe(asId(recipeId)); return; }
    std::string message = "\xC2\xAB " + r->name + " \xC2\xBB et ses " + std::to_string(r->records.size()) + " jeu(x) de valeurs sont retir\xC3\xA9s.";
    if (!uses.empty()) message += " " + std::to_string(uses.size()) + " action(s) la chargent : " + cited(uses) + ".";
    message += " Ctrl+Z la rend.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer la recette ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, recipeId](const menu::DialogResult& res) {
            auto* current = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"));
            if (res.accepted() && current) (void)current->deleteRecipe(asId(recipeId));
        });
}

// ------------------------------------------------------------- utilisateurs --
void MainAnalysisScreen::askHmiPassword(std::uint64_t userId) {
    auto doc = app_.hmi();
    const auto* u = doc ? doc->project.user(asId(userId)) : nullptr;
    if (!u) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Mot de passe", "", "4 caract\xC3\xA8res au moins", true, {}});
    fields.push_back({"Confirmation", "", "le m\xC3\xAAme, encore", true, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiPassword", "Mot de passe de " + u->login,
            "Le mot de passe n'est jamais gard\xC3\xA9 : seule son empreinte (SHA-256 sal\xC3\xA9, 4096 tours) est "
            "enregistr\xC3\xA9" "e dans ihm.txt. Oubli\xC3\xA9, il se red\xC3\xA9" "finit ici. Ctrl+Z rend l'ancien.",
            std::move(fields), "D\xC3\xA9" "finir"),
        [this, userId](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"));
            if (!res.accepted() || !pane) return;
            const auto v = FormDialog::split(res.payload);
            if (v.size() < 2) return;
            if (v[0] != v[1]) {
                status_->setTransientMessage("Les deux saisies diff\xC3\xA8rent : mot de passe inchang\xC3\xA9", 8.0);
                return;
            }
            std::string why;
            if (!pane->setPassword(asId(userId), v[0], &why)) status_->setTransientMessage("Mot de passe refus\xC3\xA9 : " + why, 8.0);
        });
}

// Lot 14 : le mot de passe de sortie du poste d'exploitation.
void MainAnalysisScreen::askHmiStationPassword() {
    auto doc = app_.hmi();
    if (!doc) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Mot de passe de sortie", "", "4 caract\xC3\xA8res au moins ; vide : sortie libre", true, {}});
    fields.push_back({"Confirmation", "", "le m\xC3\xAAme, encore", true, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiStationPassword", "Mot de passe de sortie du poste",
            "Le poste d'exploitation le demande avant de rendre la main (Ctrl+Alt+Q, ou cinq touchers du coin haut droit). "
            "Seule son empreinte (SHA-256 sal\xC3\xA9, 4096 tours) est gard\xC3\xA9" "e dans le projet. Les deux champs vides : "
            "la sortie est libre. Ctrl+Z rend l'ancien.",
            std::move(fields), "D\xC3\xA9" "finir"),
        [this](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiStationPane*>(hmiTab("poste"));
            if (!res.accepted() || !pane) return;
            const auto v = FormDialog::split(res.payload);
            if (v.size() < 2) return;
            if (v[0] != v[1]) {
                status_->setTransientMessage("Les deux saisies diff\xC3\xA8rent : mot de passe de sortie inchang\xC3\xA9", 8.0);
                return;
            }
            std::string why;
            if (!pane->setExitPassword(v[0], &why)) status_->setTransientMessage("Mot de passe de sortie refus\xC3\xA9 : " + why, 8.0);
        });
}

// Lot 14 : le mot de passe du relais SMTP (garde masque dans le projet).
void MainAnalysisScreen::askHmiSmtpPassword() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Mot de passe du relais", "", "vide : sans authentification", true, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiSmtpPassword", "Mot de passe du relais SMTP",
            "Le relais demande un utilisateur (r\xC3\xA9glage Utilisateur) et ce mot de passe (AUTH LOGIN, sans TLS : sur le r\xC3\xA9seau local). "
            "Il est gard\xC3\xA9 masqu\xC3\xA9 dans le projet, pas en clair - mais qui a le programme peut le retrouver : un relais interne "
            "sans authentification est pr\xC3\xA9" "f\xC3\xA9rable.",
            std::move(fields), "D\xC3\xA9" "finir"),
        [this](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiNotifyPane*>(hmiTab("notifications"));
            if (!res.accepted() || !pane) return;
            const auto v = FormDialog::split(res.payload);
            (void)pane->setSmtpPassword(v.empty() ? std::string{} : v[0]);
        });
}

void MainAnalysisScreen::askHmiDeleteUser(std::uint64_t userId) {
    auto doc = app_.hmi();
    const auto* u = doc ? doc->project.user(asId(userId)) : nullptr;
    auto* pane = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"));
    if (!u || !pane) return;
    const auto uses = usesOf(doc->project, hmi::Operation::ChangeUser, u->login);
    std::string message = "L'utilisateur \xC2\xAB " + u->login + " \xC2\xBB ne pourra plus se connecter.";
    if (doc->project.security.startUser == u->login) message += " C'est l'utilisateur de d\xC3\xA9marrage : personne ne le sera plus.";
    if (!uses.empty()) message += " " + std::to_string(uses.size()) + " action(s) le citent : " + cited(uses) + ".";
    message += " Ctrl+Z le rend.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer l'utilisateur ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, userId](const menu::DialogResult& res) {
            auto* current = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"));
            if (res.accepted() && current) (void)current->deleteUser(asId(userId));
        });
}

void MainAnalysisScreen::askHmiDeleteGroup(std::uint64_t groupId) {
    auto* pane = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"));
    if (!pane) return;
    std::string why;
    if (!pane->deleteGroup(asId(groupId), &why)) status_->setTransientMessage("Groupe gard\xC3\xA9 : " + why, 8.0);
}

// -------------------------------------------------------------- historiques --
void MainAnalysisScreen::askHmiHistoryExport(int tab) {
    static const char* names[] = {"alarmes", "evenements", "systeme", "mesures"};
    if (tab < 0 || tab > 3) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier CSV", suggested(app_.projectFolder(), std::string("historique_") + names[tab] + ".csv"), "", false, {}});
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiHistoryExport", std::string("Exporter l'historique : ") + names[tab],
            "Le m\xC3\xAAme format que ihm/historique/ : s\xC3\xA9parateur ;, une ligne d'en-t\xC3\xAAte, l'heure au format "
            "2026-09-22 07:40:12.350.",
            std::move(fields), "Exporter"), 0,
            ui::saveFile("Fichiers CSV|*.csv", app_.projectFolder(), std::string("Exporter l'historique : ") + names[tab])),
        [this, tab](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiHistoryPane*>(hmiTab("historiques"));
            if (!res.accepted() || !pane) return;
            const auto v = FormDialog::split(res.payload);
            if (v.empty() || cleanPath(v[0]).empty()) return;
            std::string why;
            if (!pane->exportCsv(tab, cleanPath(v[0]), &why)) status_->setTransientMessage("Export impossible : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiClearHistory() {
    auto doc = app_.hmi();
    if (!doc) return;
    const auto& h = doc->history;
    const std::size_t n = h.alarms.size() + h.events.size() + h.system.size() + h.samples.size();
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Vider l'historique ?",
            std::to_string(n) + " entr\xC3\xA9" "e(s) seront effac\xC3\xA9" "es (alarmes termin\xC3\xA9" "es, \xC3\xA9v\xC3\xA9nements, "
            "journal syst\xC3\xA8me, mesures). Ce n'est pas une modification du projet : Ctrl+Z ne les rendra pas. "
            "Exporte-les d'abord si tu veux les garder.",
            MessageDialog::Icon::Warning, "Vider"),
        [this](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiHistoryPane*>(hmiTab("historiques"));
            if (res.accepted() && pane) (void)pane->clearHistory();
        });
}

// ------------------------------------------------------- exporter / importer --
void MainAnalysisScreen::askHmiArchive(bool import) {
    auto doc = app_.hmi();
    if (!doc) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Archive", suggested(app_.projectFolder(), fileSafe(doc->project.config.name) + "_ihm.zip"),
                      "C:\\Sauvegardes\\ligne_gaz_ihm.zip", false, {}});
    const std::string text = import
        ? "Le projet IHM est RECONSTRUIT depuis l'archive : configuration, vues, ressources, scripts, variables, alarmes, "
          "recettes, utilisateurs, historiques. Puis le contr\xC3\xB4le : chaque compte du manifeste est retrouv\xC3\xA9, "
          "chaque fichier a son empreinte SHA-256, et G\xC3\xA9n\xC3\xA9rer passe. Ctrl+Z rend le projet d'avant."
        : "Tout le projet IHM dans un .zip : un manifeste (comptes par section, empreinte SHA-256 de chaque fichier) et "
          "le dossier ihm/ tel qu'il est enregistr\xC3\xA9, historiques compris.";
    const std::string zipTitle = import ? "Importer une archive IHM" : "Exporter le projet IHM";
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>(import ? "dialog.hmiImportArchive" : "dialog.hmiExportArchive",
                                                            zipTitle, text, std::move(fields), import ? "Importer" : "Exporter"),
                               0, import ? ui::openFile("Archives IHM (.zip)|*.zip", app_.projectFolder(), zipTitle)
                                         : ui::saveFile("Archives IHM (.zip)|*.zip", app_.projectFolder(), zipTitle)),
        [this, import](const menu::DialogResult& res) {
            auto* pane = dynamic_cast<HmiExchangePane*>(hmiTab("echange"));
            if (!res.accepted() || !pane) return;
            const auto v = FormDialog::split(res.payload);
            if (v.empty() || cleanPath(v[0]).empty()) return;
            std::string why;
            const bool ok = import ? pane->importFrom(cleanPath(v[0]), &why) : pane->exportTo(cleanPath(v[0]), &why);
            if (!ok) status_->setTransientMessage((import ? "Import impossible : " : "Export impossible : ") + why, 10.0);
        });
}

// ------------------------------------------------------ connexion (simulation) --
void MainAnalysisScreen::askHmiLogin(std::string login) {
    auto doc = app_.hmi();
    if (!doc) return;
    const auto& sec = doc->project.security;
    std::vector<std::string> logins;
    for (const auto& u : sec.users) if (u.enabled) logins.push_back(u.login);
    if (logins.empty()) {
        status_->setTransientMessage("Aucun utilisateur : Configuration > Utilisateurs", 8.0);
        return;
    }
    if (login.empty() || std::find(logins.begin(), logins.end(), login) == logins.end()) login = logins.front();
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Utilisateur", login, "", false, logins});
    fields.push_back({"Mot de passe ou code", "", "le mot de passe, ou le code \xC3\xA0 6 chiffres du moment", true, {}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiLogin", "Changer d'utilisateur",
        "Mot de passe : celui d\xC3\xA9" "fini dans Configuration > Utilisateurs. Code dynamique : celui qu'affiche le volet "
        "Utilisateurs (il change toutes les " + std::to_string(sec.dynamicPeriodS) + " s). Autorisation par expression : "
        "rien \xC3\xA0 saisir, l'expression d\xC3\xA9" "cide.",
        std::move(fields), "Se connecter");
    // L'autorisation par expression n'a pas de secret : le champ s'eteint.
    auto project = doc;
    dialog->setRules([project](const std::vector<std::string>& values, std::vector<FormDialog::FieldState>& state) {
        if (values.empty() || state.size() < 2) return;
        const auto* u = project->project.userByLogin(values[0]);
        const bool expr = u && u->protection == "expression";
        state[1].enabled = !expr;
        state[1].hint = expr ? "autorisation par expression : " + u->expression : std::string{};
        state[1].placeholder = u && u->protection == "dynamique" ? "le code \xC3\xA0 " + std::to_string(project->project.security.dynamicDigits)
                                                                       + " chiffres du moment"
                                                                 : "le mot de passe";
    });
    app_.menus().ShowDialog(std::move(dialog), [this](const menu::DialogResult& res) {
        auto* pane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (!res.accepted() || !pane) return;
        const auto v = FormDialog::split(res.payload);
        if (v.empty()) return;
        std::string why;
        auto& rt = pane->runtime();
        rt.setLoginSource("dialogue Changer d'utilisateur");         // lot 13 : le journal d'audit
        if (rt.login(v[0], v.size() > 1 ? v[1] : std::string{}, rt.now(), &why))
            status_->setTransientMessage(v[0] + " connect\xC3\xA9 (niveau " + std::to_string(rt.level()) + ")", 6.0);
        else
            status_->setTransientMessage("Connexion refus\xC3\xA9" "e : " + why, 8.0);
        pane->refreshNow();
    });
}

// -------------------------------------------------------------- constats ----
bool MainAnalysisScreen::openHmiSupervisionIssue(const hmi::Issue& issue) {
    auto doc = app_.hmi();
    if (!doc) return false;
    const auto& p = doc->project;
    const bool security = issue.category == "S\xC3\xA9" "curit\xC3\xA9";
    if (issue.category == "Alarme" && issue.view == hmi::kNoId) {
        openHmiPane("alarmes");
        if (auto* pane = dynamic_cast<HmiAlarmsPane*>(hmiTab("alarmes"))) {
            pane->setPriorityFilter(0);
            pane->setSearch({});
            pane->selectAlarm(issue.item);
        }
        return true;
    }
    if (issue.category == "Recette" && issue.view == hmi::kNoId) {
        openHmiPane("recettes");
        if (auto* pane = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"))) pane->selectRecipe(issue.item);
        return true;
    }
    if ((issue.category == "Utilisateur" || security) && issue.view == hmi::kNoId) {
        openHmiPane("utilisateurs");
        auto* pane = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"));
        if (!pane) return true;
        if (p.user(issue.item)) { pane->showTab(HmiUsersPane::Users); pane->selectUser(issue.item); }
        else if (p.group(issue.item)) { pane->showTab(HmiUsersPane::Groups); pane->selectGroup(issue.item); }
        else if (p.role(issue.property)) { pane->showTab(HmiUsersPane::Roles); pane->selectRole(issue.property); }
        else if (const auto* u = p.userByLogin(issue.property)) { pane->showTab(HmiUsersPane::Users); pane->selectUser(u->id); }
        return true;
    }
    if (issue.category == "Historique" && issue.view == hmi::kNoId) {
        openHmiPane("historiques");
        return true;
    }
    // Lot 13 : l'unite et le format d'une variable (son chemin dans `property`).
    if (issue.category == "Unit\xC3\xA9" && issue.view == hmi::kNoId) {
        openHmiPane("unites");
        if (auto* pane = dynamic_cast<HmiUnitsPane*>(hmiTab("unites"))) pane->selectDisplay(issue.property);
        return true;
    }
    // Lot 14 : la communication (la variable dans `property`) - sa ligne de la
    // table des adresses, si elle en a une.
    if (issue.category == "Communication" && issue.view == hmi::kNoId) {
        openHmiPane("communication");
        if (auto* pane = dynamic_cast<HmiCommPane*>(hmiTab("communication"))) pane->selectAddress(issue.property);
        return true;
    }
    // Lot 14 : le poste d'exploitation (l'ecran dans `property` : "ecran 2").
    if (issue.category == "Poste d'exploitation" && issue.view == hmi::kNoId) {
        openHmiPane("poste");
        if (auto* pane = dynamic_cast<HmiStationPane*>(hmiTab("poste")); pane && issue.property.rfind("ecran ", 0) == 0)
            pane->selectScreen(std::atoi(issue.property.c_str() + 6));
        return true;
    }
    // Lot 14 : les notifications (le destinataire dans `property`), les rapports (le rapport).
    if (issue.category == "Notifications" && issue.view == hmi::kNoId) {
        openHmiPane("notifications");
        if (auto* pane = dynamic_cast<HmiNotifyPane*>(hmiTab("notifications")); pane && !issue.property.empty()) pane->selectRecipient(issue.property);
        return true;
    }
    if (issue.category == "Rapport" && issue.view == hmi::kNoId) {
        openHmiPane("rapports");
        if (auto* pane = dynamic_cast<HmiReportsPane*>(hmiTab("rapports")); pane && !issue.property.empty()) pane->selectReport(issue.property);
        return true;
    }
    if (issue.category == "Acc\xC3\xA8s web" && issue.view == hmi::kNoId) {
        openHmiPane("web");
        return true;
    }
    // Lot 13 : une langue (son code dans `property`), une traduction.
    if (issue.category == "Langue" && issue.view == hmi::kNoId) {
        openHmiPane("langues");
        if (auto* pane = dynamic_cast<HmiLanguagesPane*>(hmiTab("langues"))) {
            if (p.languages.find(issue.property)) pane->selectLanguage(issue.property);
            // "<< texte >> en en : ..." : le texte d'origine entre les guillemets.
            const std::string open = "\xC2\xAB ", close = " \xC2\xBB";
            if (const auto a = issue.message.find(open); a == 0)
                if (const auto b = issue.message.find(close + " en ", a + open.size()); b != std::string::npos)
                    pane->selectText(issue.message.substr(a + open.size(), b - a - open.size()));
        }
        return true;
    }
    return false;
}

} // namespace app
