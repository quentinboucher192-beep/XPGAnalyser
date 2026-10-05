// =============================================================================
//  app/screens/StationExportDialog.hpp - Lot API 8 : ou enregistrer, au poste
// -----------------------------------------------------------------------------
//  LE POSTE D'EXPLOITATION DEMANDE OU ENREGISTRER un export de l'IHM qu'un geste
//  de l'operateur a lance (le bouton d'export, l'action Exporter au clic,
//  IHM_EXPORTER dans le script d'un clic). L'IHM y est seule, en plein ecran,
//  souvent tactile : un dialogue a lui, lisible au doigt - le titre en grand, le
//  chemin propose (exports/ du projet, sous le nom habituel) dans un grand champ,
//  le bouton ... (l'explorateur de fichiers) a sa droite, Annuler et Exporter en
//  grands boutons ; Entree exporte, Echap annule.
//
//  Le chemin reste un champ (un clavier branche le corrige) ; un dossier : le
//  fichier y va sous son nom habituel (ExportTarget.hpp, exportFileFor).
//  KIOSQUE : l'explorateur ouvre tout le disque ; hors de l'administrateur du
//  poste (le niveau le plus haut, comme F1), le bouton ... est grise et le dit
//  (Spec::browseDenied).
//
//  La reponse : DialogResult::payload - le chemin (Exporter) ; Annuler, Echap :
//  Cancel. Scripts : bouton "Exporter" | bouton "Annuler" | touche entree |
//  touche echap ; explorateur "D:/x/y.csv" puis parcourir "Dossier ou fichier" ;
//  champ 1 "D:/x/" (un dossier : le nom habituel dedans).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/PathBrowse.hpp"

#include <string>

namespace ui { class Button; class InputText; class BrowseButton; }

namespace app {

class StationExportDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string    what;           // "les alarmes (CSV)" : le titre dit "Exporter les alarmes (CSV)"
        std::string    proposed;       // le chemin propose (le fichier, dans exports/ du projet)
        ui::PathBrowse browse;         // ce que le bouton ... ouvre (ui::saveFile)
        std::string    browseDenied;   // non vide : le bouton ... est grise, et pourquoi
        bool           fixedPath{false};   // le chemin ne se tape pas non plus (le kiosque)
    };
    explicit StationExportDialog(Spec spec);

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override;
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;

    // Pour les essais et les scripts : le champ, les boutons.
    [[nodiscard]] ui::InputText*    field() const noexcept { return field_; }
    [[nodiscard]] ui::BrowseButton* browseButton() const noexcept { return browse_; }
    [[nodiscard]] ui::Button*       exportButton() const noexcept { return ok_; }
    [[nodiscard]] ui::Button*       cancelButton() const noexcept { return cancel_; }

    // Le libelle du champ : ce que `parcourir` cherche (le meme que le dialogue
    // de l'editeur, pour que les sessions disent la meme chose).
    static constexpr const char* kFieldLabel = "Dossier ou fichier";

protected:
    core::Status buildUi() override;

private:
    void finish(bool ok);

    Spec                  spec_;
    ui::InputText*        field_{nullptr};
    ui::BrowseButton*     browse_{nullptr};
    ui::Button*           ok_{nullptr};
    ui::Button*           cancel_{nullptr};
    bool                  done_{false};
    core::ConnectionScope links_;
};

} // namespace app
