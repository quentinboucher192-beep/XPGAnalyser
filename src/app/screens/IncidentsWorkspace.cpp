// =============================================================================
//  app/screens/IncidentsWorkspace.cpp - 1.20.0 : IHM > Ressources > Incidents
// -----------------------------------------------------------------------------
//  Le volet (HmiIncidentsPane) ne demande rien lui-meme : ses Hosts sont ici.
//    - les dialogues : Ajouter un commentaire (le texte), Valider (une note
//      facultative), Supprimer (la confirmation, en attente seulement) et les
//      reglages de la surveillance (un formulaire) ;
//    - ouvrir un element de la chaine de dependances : le chemin de l'Assistant
//      IA (revealHmiElement), plus les scripts et fonctions d'une vue
//      ("Vue_Station.Init") ; introuvable : le volet dit qu'il n'existe plus ;
//    - ouvrir un dossier (l'Explorateur), la video (le lecteur du systeme).
//  L'auteur d'un commentaire ou d'une validation : l'utilisateur du poste et
//  le nom du poste (hmi::ver::defaultAuthor, comme les versions).
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../Dossiers.hpp"
#include "../FilePreview.hpp"
#include "../hmi/HmiIncidentsPane.hpp"
#include "../../hmi/HmiVersions.hpp"
#include "../../menu/MenuManager.hpp"

#include <memory>
#include <string>
#include <vector>

namespace app {

namespace {
HmiIncidentsPane* incidentsPane(ui::Widget* page) { return dynamic_cast<HmiIncidentsPane*>(page); }
} // namespace

ui::WidgetPtr MainAnalysisScreen::buildHmiIncidentsPane() {
    auto pane = std::make_unique<HmiIncidentsPane>("hmi.incidents", app_.hmi(), &app_.incidents());
    HmiIncidentsPane::Hosts hosts;
    hosts.author = [] { return hmi::ver::defaultAuthor(); };
    hosts.comment = [this](const std::string& folder, const std::string& name) {
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Commentaire", "", "ce que tu as constat\xC3\xA9, v\xC3\xA9rifi\xC3\xA9 ou fait", false, {}});
        auto dialog = std::make_unique<FormDialog>(
            "dialog.hmiIncidentComment", "Commenter \xC2\xAB " + name + " \xC2\xBB",
            "Le commentaire s'ajoute au dossier de l'incident (un fichier de plus, avec la date et ton nom) : rien de ce qui "
            "y est d\xC3\xA9j\xC3\xA0 n'est r\xC3\xA9\xC3\xA9" "crit.",
            std::move(fields), "Ajouter");
        dialog->setValidator([](const std::vector<std::string>& v, std::size_t& field) -> std::string {
            field = 0;
            return v.empty() || v[0].find_first_not_of(" \t") == std::string::npos ? std::string("Le commentaire est vide.") : std::string{};
        });
        app_.menus().ShowDialog(std::move(dialog), [this, folder, name](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            std::string why;
            auto* pane = incidentsPane(hmiTab("incidents"));
            if (pane && pane->select(name)) {
                if (!pane->commentSelected(v[0], &why)) status_->setTransientMessage("Commentaire : " + why, 8.0, ui::StatusBar::Severity::Warning);
                return;
            }
            app_.incidents().comment(folder, v[0], hmi::ver::defaultAuthor());   // le volet ferme entre-temps
        });
    };
    hosts.validate = [this](const std::string& folder, const std::string& name) {
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Note (facultative)", "", "ex. : capteur remplac\xC3\xA9, d\xC3\xA9" "faut confirm\xC3\xA9", false, {}});
        app_.menus().ShowDialog(
            std::make_unique<FormDialog>(
                "dialog.hmiIncidentValidate", "Valider \xC2\xAB " + name + " \xC2\xBB",
                "Une copie du dossier va dans \xC2\xAB valides \xC2\xBB ; chacun de ses fichiers est v\xC3\xA9rifi\xC3\xA9 (empreintes SHA-256) avant "
                "que l'original ne soit retir\xC3\xA9 d'\xC2\xAB en attente \xC2\xBB. La date, ton nom et la note sont gard\xC3\xA9s avec lui. "
                "Un incident qui n'est pas intact n'est pas valid\xC3\xA9.",
                std::move(fields), "Valider"),
            [this, folder, name](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                const auto v = FormDialog::split(r.payload);
                const std::string note = v.empty() ? std::string{} : v[0];
                std::string why;
                auto* pane = incidentsPane(hmiTab("incidents"));
                if (pane && pane->select(name)) {
                    if (!pane->validateSelected(note, &why)) status_->setTransientMessage("Valider : " + why, 8.0, ui::StatusBar::Severity::Warning);
                    return;
                }
                app_.incidents().validate(folder, note, hmi::ver::defaultAuthor());
            });
    };
    hosts.remove = [this](const std::string& folder, const std::string& name) {
        app_.menus().ShowDialog(
            std::make_unique<MessageDialog>("Supprimer l'incident ?",
                                            "\xC2\xAB " + name + " \xC2\xBB et tout son dossier (la capture, la vid\xC3\xA9o, les analyses, les "
                                            "commentaires) sont supprim\xC3\xA9s du disque. Valider le garderait plut\xC3\xB4t dans \xC2\xAB valides \xC2\xBB.",
                                            MessageDialog::Icon::Question, "Supprimer"),
            [this, folder, name](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                std::string why;
                auto* pane = incidentsPane(hmiTab("incidents"));
                if (pane && pane->select(name)) {
                    if (!pane->removeSelected(&why)) status_->setTransientMessage("Supprimer : " + why, 8.0, ui::StatusBar::Severity::Warning);
                    return;
                }
                app_.incidents().remove(folder);
            });
    };
    hosts.settings = [this] { askHmiIncidentSettings(); };
    hosts.reveal = [this](const std::string& kind, const std::string& name, const std::string& view) {
        if (revealHmiElement(kind, name, view)) return true;
        // un script ou une fonction d'une vue ("Vue_Station.Init") : sa vue, a ce script ou a cette fonction
        const auto doc = app_.hmi();
        const hmi::View* v = doc && !view.empty() ? doc->project.viewByName(view) : nullptr;
        if (!v) return false;
        const auto dot = name.find('.');
        const std::string rest = dot == std::string::npos ? name : name.substr(dot + 1);
        if (kind == "script") {
            for (const auto& sc : v->scripts)
                if (sc.event == rest || sc.name == rest) {
                    openHmiScripts(v->id, sc.id);
                    return true;
                }
            openHmiScripts(v->id);
            return true;
        }
        if (kind == "fonction")
            for (const auto& f : v->functions)
                if (f.name == rest) {
                    openHmiFunctionDefinition(v->id, f.id);
                    return true;
                }
        openHmiView(v->id);
        return true;
    };
    hosts.openFolder = [this](const std::string& folder) {
        if (const std::string why = dossiers::ouvrirDansLeSysteme(folder); !why.empty()) status_->setTransientMessage(why, 8.0);
    };
    hosts.openFile = [](const std::string& file) { return preview::openWithSystem(file); };
    pane->setHosts(std::move(hosts));
    return pane;
}

void MainAnalysisScreen::askHmiIncidentSettings() {
    const auto labels = incidentsui::settingsLabels();
    const auto values = incidentsui::settingsValues(app_.incidents().settings());
    std::vector<FormDialog::Field> fields;
    for (std::size_t i = 0; i < labels.size() && i < values.size(); ++i) {
        FormDialog::Field f{labels[i], values[i], {}, false, {}};
        switch (i) {
            case 0: f.choices = {"Activ\xC3\xA9" "e", "D\xC3\xA9sactiv\xC3\xA9" "e"}; break;
            case 1: f.placeholder = "vide : toutes ; POMPE_*, NIVEAU_HAUT"; break;
            case 2: f.placeholder = "aucune ; TEST_*"; break;
            case 3: f.choices = {"Oui", "Non"}; break;
            case 4: f.placeholder = "0 \xC3\xA0 120 (images et chronologie gard\xC3\xA9" "es avant)"; break;
            case 5: f.placeholder = "0 \xC3\xA0 120 (la vid\xC3\xA9o continue apr\xC3\xA8s)"; break;
            case 6: f.placeholder = "1 \xC3\xA0 10"; break;
            case 7: f.placeholder = "0 : jamais"; break;
            case 8: f.placeholder = "une r\xC3\xA9" "apparition dans ce d\xC3\xA9lai : le m\xC3\xAAme incident"; break;
            default: break;
        }
        fields.push_back(std::move(f));
    }
    auto dialog = std::make_unique<FormDialog>(
        "dialog.hmiIncidentSettings", "R\xC3\xA9glages de la surveillance des incidents",
        "Quand une alarme surveill\xC3\xA9" "e appara\xC3\xAEt (simulation IHM, poste d'exploitation), un dossier d'incident se cr\xC3\xA9" "e tout "
        "seul : la capture, la vid\xC3\xA9o des secondes avant et apr\xC3\xA8s, le contexte copi\xC3\xA9 et l'analyse. Les listes acceptent * "
        "(POMPE_*) et se s\xC3\xA9parent par des virgules ; une alarme ignor\xC3\xA9" "e ne fait jamais d'incident. Gard\xC3\xA9s avec le projet.",
        std::move(fields), "Enregistrer");
    dialog->setValidator([](const std::vector<std::string>& v, std::size_t& field) -> std::string {
        hmi::incident::Settings s;
        std::string why;
        std::size_t at = 0;
        if (incidentsui::settingsFrom(v, s, &why, &at)) return {};
        field = at;
        return why;
    });
    app_.menus().ShowDialog(std::move(dialog), [this](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        auto s = app_.incidents().settings();
        std::string why;
        if (!incidentsui::settingsFrom(FormDialog::split(r.payload), s, &why)) {
            status_->setTransientMessage("R\xC3\xA9glages : " + why, 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        if (!app_.incidents().setSettings(s, &why)) {
            status_->setTransientMessage("R\xC3\xA9glages de la surveillance : " + why, 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        status_->setTransientMessage(s.enabled ? "Surveillance des incidents : r\xC3\xA9glages enregistr\xC3\xA9s."
                                               : "Surveillance des incidents d\xC3\xA9sactiv\xC3\xA9" "e : aucune alarme ne fera d'incident.",
                                     6.0, ui::StatusBar::Severity::Success);
    });
}

} // namespace app
