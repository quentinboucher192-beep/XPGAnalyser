// =============================================================================
//  app/ImportMastDialog.hpp - lot 7 : le recapitulatif d'un nouveau MAST
// -----------------------------------------------------------------------------
//  AVANT DE REMPLACER LE PROGRAMME, CE QUI VA SE PASSER. Un bandeau rouge dit
//  que le cote API du projet est detruit et remplace par le .XPG, une ligne dit
//  quelle version garde l'etat d'avant ; cinq onglets comptent et montrent en
//  arbre ce qui est garde, change, supprime, nouveau, et les liens de l'IHM
//  qui se perdent (Variables globales par genre, Instances de DFB/DDT, Types,
//  Sections et unites, Tables d'animation, IHM par vue et par variable).
//
//  La case "Garder les variables identiques et leurs liens" (cochee) choisit
//  entre les deux plans que l'ecran a calcules : les onglets suivent. Annuler
//  (ou Echap) ne fait rien ; "Importer et remplacer" rend la reponse, et c'est
//  l'ecran qui cree la version puis importe en une commande.
// =============================================================================
#pragma once

#include "../menu/IMenu.hpp"
#include "../project/MastImport.hpp"
#include "../ui/widgets/Controls.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace ui { class TabControl; class TreeView; }

namespace app {

class ImportMastDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string         fileName;       // "MAST.XPG", "MAST.XPG + CONFIG.XHW"
        std::string         projectName;    // le projet ouvert (le bandeau dit ce qui est remplace)
        std::string         versionLine;    // ce qui se passe avant l'import (la version), une ligne
        project::mast::Plan keep;           // la case cochee (recommande)
        project::mast::Plan replace;        // decochee : tout remplacer
        bool                keepIdentical{true};
    };
    explicit ImportMastDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // La reponse (DialogResult::payload) : "garder=1" ou "garder=0".
    [[nodiscard]] static bool keepFrom(const std::string& payload);

    // ---- pour les scripts et les tests ------------------------------------------
    // Un onglet par le debut de son titre, sans la casse ni les accents
    // ("Supprime", "liens", "Nouveau") ; faux : aucun.
    bool selectTab(const std::string& titlePrefix);
    [[nodiscard]] std::size_t currentTab() const;
    [[nodiscard]] std::string currentTabTitle() const;
    void setKeepIdentical(bool on);
    [[nodiscard]] bool keepIdentical() const noexcept { return keep_; }
    [[nodiscard]] const project::mast::Plan& plan() const noexcept { return keep_ ? spec_.keep : spec_.replace; }
    [[nodiscard]] const Spec& spec() const noexcept { return spec_; }
    void confirm();                 // "Importer et remplacer"
    void cancel();                  // "Annuler", Echap

protected:
    core::Status buildUi() override;

private:
    friend class MastRecapBody;
    void refreshTrees();
    void finish(bool ok);

    Spec                                                    spec_;
    bool                                                    keep_{true};
    bool                                                    done_{false};
    ui::TabControl*                                         tabs_{nullptr};
    std::array<ui::TreeView*, project::mast::kStatusCount>  trees_{};
    ui::Checkbox*                                           keepBox_{nullptr};
    ui::Button*                                             ok_{nullptr};
    ui::Button*                                             cancel_{nullptr};
    core::ConnectionScope                                   links_;
};

} // namespace app
