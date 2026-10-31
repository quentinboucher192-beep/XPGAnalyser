// =============================================================================
//  app/ai/AiKeysDialog.hpp - l'Assistant IA : la fenetre << Cles API >> (1.18.0)
// -----------------------------------------------------------------------------
//  Le gestionnaire des cles (AiKeys) a l'ecran, ouvert par << Gerer les cles... >>
//  des Parametres de l'Assistant :
//
//    Mes cles      la liste : nom, fournisseur, la cle masquee (son debut connu
//                  et ses 4 derniers caracteres, jamais plus), ajoutee, dernier
//                  essai (acceptee, refusee), utilisee par l'Assistant (l'etoile) ;
//    les gestes    Ajouter une cle, Utiliser (l'active de son fournisseur), Tester
//                  (la liste des modeles du fournisseur : gratuite, sur un fil -
//                  l'ecran ne gele pas), Renommer (le detail), Remplacer la cle,
//                  Supprimer (une confirmation) ;
//    la cle choisie  nom, fournisseur, adresse et modele preferes, la cle (masquee,
//                  Afficher), << Memoriser sur ce PC (protegee par Windows) >> -
//                  COCHEE PAR DEFAUT - ou pour cette seance seulement.
//
//  Le pied dit le dossier (IA/cles, dans les reglages de l'utilisateur : une
//  reinstallation n'y touche pas). Fermer, Echap : la fenetre se ferme (chaque
//  geste est deja enregistre).
// =============================================================================
#pragma once

#include "AiKeys.hpp"
#include "../../core/Signal.hpp"
#include "../../menu/IMenu.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ui { class Button; class Checkbox; class DropDown; class InputText; class TableView; class ToggleButton; }

namespace app::ai {

class AiPanel;

class AiKeysDialog final : public menu::WidgetMenu {
public:
    explicit AiKeysDialog(AiPanel& panel);
    ~AiKeysDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Cl\xC3\xA9s API"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;
    void                           Update(const menu::FrameContext& fc) override;

    // Les gestes (l'ecran, les essais).
    bool beginNew();                               // le detail se vide : une cle neuve a saisir
    bool select(const std::string& id);            // la cle choisie (son detail)
    bool saveDetail();                             // ajoute (cle neuve) ou enregistre le detail (nom, fournisseur...)
    bool use();                                    // la cle choisie devient l'active de son fournisseur
    bool replaceKey();                             // le secret saisi remplace celui de la cle choisie
    bool removeSelected();                         // apres la confirmation (askRemove)
    void askRemove();
    bool test();                                   // la liste des modeles, sur un fil
    bool pollTest();                               // le resultat (Update l'appelle a chaque image)
    void close();
    [[nodiscard]] std::string selectedId() const { return selected_; }
    [[nodiscard]] std::size_t rowCount() const;
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] bool               messageOk() const noexcept { return messageOk_; }
    [[nodiscard]] bool               testing() const;
    // Les champs, par leur nom (essais) : "nom", "fournisseur", "adresse", "modele", "cle", "memoriser", "seance".
    [[nodiscard]] ui::Widget* field(const std::string& name) const;

protected:
    core::Status buildUi() override;

private:
    class Body;
    class Model;
    void refresh();
    void fillDetail();
    void say(std::string text, bool ok);
    AiPanel&               panel_;
    std::shared_ptr<Model> model_;
    ui::TableView*         table_{nullptr};
    ui::InputText*         name_{nullptr};
    ui::DropDown*          provider_{nullptr};
    ui::InputText*         endpoint_{nullptr};
    ui::InputText*         modelName_{nullptr};
    ui::InputText*         key_{nullptr};
    ui::ToggleButton*      show_{nullptr};
    ui::Checkbox*          remember_{nullptr};
    ui::Checkbox*          sessionOnly_{nullptr};
    ui::Button*            add_{nullptr};
    ui::Button*            use_{nullptr};
    ui::Button*            test_{nullptr};
    ui::Button*            save_{nullptr};
    ui::Button*            replace_{nullptr};
    ui::Button*            remove_{nullptr};
    ui::Button*            close_{nullptr};
    std::string            selected_;          // vide : une cle neuve
    std::string            testing_;           // la cle en essai
    std::uint64_t          seenModels_{0};
    std::string            message_;
    bool                   messageOk_{true};
    bool                   confirmRemove_{false};
    bool                   syncing_{false};
    bool                   done_{false};
    core::ConnectionScope  links_;
};

} // namespace app::ai
