// =============================================================================
//  app/ai/AiSettingsDialog.hpp - l'Assistant IA : la fenetre de ses parametres
// -----------------------------------------------------------------------------
//  Une fenetre du logiciel (deplacable, redimensionnable), deux colonnes :
//
//    General         activer l'Assistant ; le fournisseur (Demonstration, et ceux
//                    a venir, grises) ;
//    Connexion       le modele (une case libre : rien en dur), l'adresse, le delai ;
//    Cle API         masquee, Afficher, Effacer - GARDEE EN MEMOIRE pour la seance,
//                    jamais ecrite : l'ecran le dit, et qu'elle sera protegee par
//                    Windows (DPAPI) quand l'IA sera branchee ;
//    Fonctionnement  le mode par defaut (Previsualisation, Direct) ;
//    Historique      combien de temps, combien de conversations ;
//    Confidentialite ce qui serait envoye (la vue active, la selection, les
//                    variables, les pieces jointes) - chacun se decoche ;
//    Memoire et apprentissage...  la commande ia.memoire (grisee si elle n'existe pas).
//
//  Enregistrer ecrit IA/parametres.json (sans la cle) ; Annuler, Echap : rien.
// =============================================================================
#pragma once

#include "AiStore.hpp"
#include "../../menu/IMenu.hpp"

#include <string>

namespace ui { class Button; class Checkbox; class DropDown; class InputText; class ToggleButton; }

namespace app::ai {

class AiPanel;

class AiSettingsDialog final : public menu::WidgetMenu {
public:
    explicit AiSettingsDialog(AiPanel& panel);

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Param\xC3\xA8tres de l'Assistant IA"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;

    // Les gestes (essais, scripts).
    void save();
    void cancel();
    void showKey(bool on);
    void clearKey();
    void openMemory();
    // Ce que la fenetre enregistrerait maintenant.
    [[nodiscard]] Settings    draft() const;
    [[nodiscard]] std::string keyText() const;

    // Les champs, par leur nom (essais) : "actif", "fournisseur", "modele", "adresse", "delai",
    // "cle", "mode", "jours", "nombre", "vue", "selection", "variables", "pieces", "memoire".
    [[nodiscard]] ui::Widget* field(const std::string& name) const;

protected:
    core::Status buildUi() override;

private:
    friend class AiSettingsBody;
    void finish(bool ok);

    AiPanel&          panel_;
    ui::Checkbox*     enabled_{nullptr};
    ui::DropDown*     provider_{nullptr};
    ui::InputText*    model_{nullptr};
    ui::InputText*    endpoint_{nullptr};
    ui::InputText*    timeout_{nullptr};
    ui::InputText*    key_{nullptr};
    ui::ToggleButton* showKey_{nullptr};
    ui::Button*       clearKey_{nullptr};
    ui::DropDown*     mode_{nullptr};
    ui::DropDown*     days_{nullptr};
    ui::DropDown*     count_{nullptr};
    ui::Checkbox*     sendView_{nullptr};
    ui::Checkbox*     sendSelection_{nullptr};
    ui::Checkbox*     sendVariables_{nullptr};
    ui::Checkbox*     sendAttachments_{nullptr};
    ui::Button*       memory_{nullptr};
    ui::Button*       cancel_{nullptr};
    ui::Button*       ok_{nullptr};
    bool              done_{false};
    core::ConnectionScope links_;
};

} // namespace app::ai
