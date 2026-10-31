// =============================================================================
//  app/ai/AiSettingsDialog.hpp - l'Assistant IA : la fenetre de ses parametres
// -----------------------------------------------------------------------------
//  Une fenetre du logiciel (deplacable, redimensionnable), deux colonnes :
//
//    General         activer l'Assistant ; le fournisseur (Demonstration, Anthropic
//                    (Claude) ; ceux a venir, grises) ;
//    Connexion       le modele (une case libre, VIDE par defaut : rien en dur ;
//                    "Lister les modeles" donne ceux du fournisseur), l'effort
//                    (Auto, Bas, Moyen, Haut), l'adresse (vide : celle du
//                    fournisseur ; sinon un relais d'entreprise), le delai ;
//    Cle API         masquee, Afficher, Effacer, "Memoriser la cle (protegee par
//                    Windows)" - DPAPI, IA/cle.bin ; sous Linux, gardee pour la
//                    seance seulement (la case est grisee, l'ecran le dit) ;
//                    jamais en clair sur le disque ;
//                    "Tester la connexion" (la liste des modeles : gratuite) ;
//    Fonctionnement  le mode par defaut (Previsualisation, Direct) ;
//    Historique      combien de temps, combien de conversations ;
//    Confidentialite ce qui serait envoye (la vue active, la selection, les
//                    variables, les pieces jointes) - chacun se decoche ;
//    Memoire et apprentissage...  la commande ia.memoire (grisee si elle n'existe pas).
//
//  1.16.0 (FOURNISSEURS) : le fournisseur "Compatible OpenAI" montre la liste
//  Service (Ollama, LM Studio, OpenRouter, Groq, Google Gemini, Mistral, Autre :
//  choisir remplit l'Adresse) a la place de l'Effort ; la cle y est facultative
//  pour un modele sur ce PC (la note le dit). Fonctionnement : la Fenetre de
//  contexte (vide : Auto, celle que le service dit), l'Outil (Auto, Appel
//  d'outil, JSON dans le texte), le Budget (jetons, par jour ou par mois ; 0 :
//  aucun) - pour tous les fournisseurs relies.
//
//  Enregistrer ecrit IA/parametres.json (sans la cle) ; Annuler, Echap : rien.
//  Lister et Tester partent sur un fil (ai::ModelQuery) : la fenetre lit leur
//  resultat a chaque image (Update), l'interface ne gele jamais.
// =============================================================================
#pragma once

#include "AiStore.hpp"
#include "../../menu/IMenu.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class Button; class Checkbox; class DropDown; class InputText; class PopupMenu; class ToggleButton; }

namespace app::ai {

class AiPanel;

class AiSettingsDialog final : public menu::WidgetMenu {
public:
    explicit AiSettingsDialog(AiPanel& panel);

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Param\xC3\xA8tres de l'Assistant IA"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;
    void                           Update(const menu::FrameContext& fc) override;

    // Les gestes (essais, scripts).
    void save();
    void cancel();
    void showKey(bool on);
    void clearKey();
    void openMemory();
    // 1.15.0 : la liste des modeles (une liste a choisir s'ouvre), tester la connexion.
    bool listModels();
    bool testConnection();
    // Le resultat d'une demande (Update l'appelle a chaque image). Vrai : il vient d'arriver.
    bool pollQuery();
    bool chooseModel(std::size_t index);   // dans la liste ouverte
    [[nodiscard]] const std::string& resultText() const noexcept { return result_; }
    [[nodiscard]] bool               resultOk() const noexcept { return resultOk_; }
    [[nodiscard]] bool               querying() const;
    [[nodiscard]] ui::PopupMenu*     modelsMenu() const noexcept { return modelsMenu_; }
    // Ce que la fenetre enregistrerait maintenant.
    [[nodiscard]] Settings    draft() const;
    [[nodiscard]] std::string keyText() const;

    // Les champs, par leur nom (essais) : "actif", "fournisseur", "modele", "lister", "effort",
    // "adresse", "delai", "cle", "memoriser", "tester", "mode", "jours", "nombre", "vue",
    // "selection", "variables", "pieces", "memoire" ; 1.16.0 : "service", "contexte", "outil",
    // "budget", "periode".
    // 1.16.0 : choisir un service (sa cle) remplit l'adresse ; la fenetre suit le fournisseur choisi.
    bool chooseService(std::string_view key);
    // 1.18.0 : LES CLES ENREGISTREES (le gestionnaire, AiKeys) - la liste des cles du fournisseur choisi ("cles") et
    // << Gerer les cles... >> ("gerer", la fenetre Cles API). Choisir une cle la pose dans la case ; la derniere ligne,
    // << Nouvelle cle... >>, vide la case : Enregistrer l'ajoute (memorisee par defaut). Vrai : la cle est connue.
    bool chooseKey(const std::string& id);
    void manageKeys();
    [[nodiscard]] const std::string& chosenKey() const noexcept { return keyId_; }
    [[nodiscard]] bool openAIShown() const;
    [[nodiscard]] ui::Widget* field(const std::string& name) const;

protected:
    core::Status buildUi() override;

private:
    friend class AiSettingsBody;
    void finish(bool ok);
    bool startQuery(bool listing);

    AiPanel&          panel_;
    ui::Checkbox*     enabled_{nullptr};
    ui::Checkbox*     notify_{nullptr};       // 1.16.1 : m'avertir des reponses
    ui::DropDown*     provider_{nullptr};
    ui::InputText*    model_{nullptr};
    ui::Button*       listModels_{nullptr};
    ui::PopupMenu*    modelsMenu_{nullptr};
    ui::DropDown*     effort_{nullptr};
    ui::InputText*    endpoint_{nullptr};
    ui::InputText*    timeout_{nullptr};
    ui::InputText*    key_{nullptr};
    ui::ToggleButton* showKey_{nullptr};
    ui::Button*       clearKey_{nullptr};
    ui::Checkbox*     remember_{nullptr};
    ui::DropDown*     keys_{nullptr};         // 1.18.0 : les cles enregistrees du fournisseur
    ui::Button*       manageKeys_{nullptr};   // 1.18.0 : Gerer les cles...
    std::string       keyId_;                 // la cle choisie dans la liste (vide : une cle neuve)
    std::string       keyProvider_;           // le fournisseur dont la liste montre les cles
    void              refreshKeys();
    ui::Button*       test_{nullptr};
    ui::DropDown*     mode_{nullptr};
    ui::DropDown*     days_{nullptr};
    ui::DropDown*     count_{nullptr};
    ui::Checkbox*     sendView_{nullptr};
    ui::Checkbox*     sendSelection_{nullptr};
    ui::Checkbox*     sendVariables_{nullptr};
    ui::Checkbox*     sendAttachments_{nullptr};
    ui::Button*       memory_{nullptr};
    // 1.16.0 : le service compatible OpenAI, la fenetre de contexte, l'outil, le budget.
    ui::DropDown*     service_{nullptr};
    ui::InputText*    context_{nullptr};
    ui::DropDown*     tool_{nullptr};
    ui::InputText*    budget_{nullptr};
    ui::DropDown*     period_{nullptr};
    ui::Checkbox*     localFirst_{nullptr};   // 1.16.1 : l'IA integree d'abord
    ui::Checkbox*     anyway_{nullptr};       // 1.16.1 : appliquer malgre les erreurs
    void              providerChanged();
    ui::Button*       cancel_{nullptr};
    ui::Button*       ok_{nullptr};
    bool              done_{false};
    // 1.15.0 : la demande en cours (lister, tester), son resultat, la liste rendue.
    enum class Query : std::uint8_t { None, Listing, Testing };
    Query                  query_{Query::None};
    std::uint64_t          seenModels_{0};
    std::string            result_;
    bool                   resultOk_{false};
    std::vector<std::string> modelIds_;
    core::ConnectionScope links_;
};

} // namespace app::ai
