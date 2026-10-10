// =============================================================================
//  app/DispositionDialog.hpp - 1.12.3 : la fenetre Projet > Disposition...
// -----------------------------------------------------------------------------
//  LA FENETRE DE LA DISPOSITION (le modele : Disposition.hpp). En tete, les
//  dispositions toutes faites (Par defaut, Dessin des vues, Mise au point, Ecran
//  large, Ma disposition) et « Garder comme Ma disposition ». Au milieu, une
//  section par groupe - la fenetre, le panneau du bas, l'inspecteur de l'editeur
//  de vue, la simulation, la programmation generale, les equipements, les pages
//  du centre - et pour chaque element : quand le montrer (toujours, en edition,
//  en simulation, cache ; une page : a la demande, a l'ouverture du projet, au
//  demarrage de la simulation), ou, et comment au depart (ouvert ou replie, le
//  sous-onglet choisi). En bas, ce que la disposition cache, ce qui n'est pas
//  encore applique, et « Retablir la disposition d'origine », Annuler, Appliquer,
//  OK. Appliquer montre tout de suite ; OK applique et ferme ; Echap annule.
//
//  L'hote (la fenetre de l'application) applique et garde (les reglages de
//  l'edition : disposition.*) ; la fenetre ne fait que proposer.
// =============================================================================
#pragma once

#include "Disposition.hpp"
#include "../core/Signal.hpp"
#include "../menu/IMenu.hpp"

#include <functional>
#include <string>

namespace ui { class PropertyGrid; class DropDown; class Button; }

namespace app {

class DispositionDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        disposition::Layout applied;           // la disposition en vigueur
        disposition::Layout mine;              // « Ma disposition » (gardee)
        std::string         edition;           // "XPGAnalyser IHM"
        std::function<void(const disposition::Layout&)> apply;      // Appliquer, OK
        std::function<void(const disposition::Layout&)> keepMine;   // Garder comme « Ma disposition »
        std::function<void()>                           reset;      // Retablir : efface les cles disposition.*
    };
    explicit DispositionDialog(Spec spec);

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Disposition"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;

    // Pour les essais et les scripts.
    [[nodiscard]] const disposition::Layout& draft() const noexcept { return draft_; }
    [[nodiscard]] const disposition::Layout& applied() const noexcept { return spec_.applied; }
    // Une ligne : "quand" (toujours, edition, simulation, cache ; une page : demande, projet,
    // simulation), "ou", "depart" (un panneau) ; "choisi" sur un groupe de sous-onglets.
    bool set(const std::string& id, const std::string& field, const std::string& key);
    void choosePreset(const std::string& key);
    void applyNow();
    void accept();
    void cancel();
    void keepMine();
    void resetOrigin();
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] ui::PropertyGrid* grid() const noexcept { return grid_; }

protected:
    core::Status buildUi() override;

private:
    friend class DispositionBody;
    void rebuild();
    void finish(bool ok);

    Spec                spec_;
    disposition::Layout draft_;
    ui::PropertyGrid*   grid_{nullptr};
    ui::DropDown*       presets_{nullptr};
    ui::Button*         mine_{nullptr};
    ui::Button*         reset_{nullptr};
    ui::Button*         cancel_{nullptr};
    ui::Button*         apply_{nullptr};
    ui::Button*         ok_{nullptr};
    bool                done_{false};
    core::ConnectionScope links_;
};

} // namespace app
