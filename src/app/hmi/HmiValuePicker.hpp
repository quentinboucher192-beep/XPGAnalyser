// =============================================================================
//  app/hmi/HmiValuePicker.hpp - 1.11.3 : le selecteur de valeur d'une case
// -----------------------------------------------------------------------------
//  Un clic sur le carre de legende d'une case de l'inspecteur ouvre la liste
//  des carres (HmiEditor), puis ce selecteur :
//
//    +-- Choisir la valeur de « Value » ------------ attendu : ARRAY[0..9] OF UINT --+
//    | [Tout] [A API] [I IHM] [S Systeme] [V Symbole/vue] [C Constante]              |
//    | [x] Type attendu : ARRAY[0..9] OF UINT   3 sur 41 conviennent   [Chercher...] |
//    | +-- l'arbre ------------------------------+ +-- le detail -----------------+ |
//    | | I IHM                                   | | I UINTS                      | |
//    | |   I UINTS          ARRAY[0..9] OF UINT ✓| | Zone : IHM  Type : ...       | |
//    | +-----------------------------------------+ | [Remplacer] [Inserer] [$..$] | |
//    | Resultat  [fx] [UINTS_________________________________________] [I]         |
//    | Variable IHM : UINTS · ARRAY[0..9] OF UINT                                   |
//    | (les erreurs et leurs corrections, d'un clic)                                |
//    |                                      [Inserer un repere $..$] [Annuler] [Valider] |
//    +-------------------------------------------------------------------------------+
//
//  - L'ARBRE : tout ce qu'une valeur peut lire - l'automate (API), l'IHM, SYS.,
//    le symbole ou la vue (ses parametres, ses variables publiques) -, FILTRE
//    d'office sur le type attendu (decocher, ou « Tout montrer ») ; la recherche
//    filtre par le chemin. Un tableau ou une structure montre ses cases.
//  - LE RESULTAT est un champ : on y modifie ce qu'on a pris (une formule, un
//    repere $...$), avec l'aide a la saisie ; il est classe a chaque frappe (le
//    carre, la ligne d'information, les erreurs et leurs corrections).
//  - VALIDER : un nom inconnu -> la creation de la variable (l'hote, avec le type
//    et la zone) ; une autre erreur -> « Valider quand meme » ; sinon la case
//    initialement cliquee recoit la valeur (une commande, Ctrl+Z).
// =============================================================================
#pragma once

#include "HmiValueKind.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../menu/IMenu.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace domain { class Project; }

namespace app {

class HmiValuePicker final : public menu::WidgetMenu {
public:
    using Style = ui::PropertyGrid::LegendStyle;
    struct Spec {
        std::string      field;                      // le libelle de la case : « Value »
        std::string      expected;                   // le type attendu (BOOL, ARRAY[0..9] OF UINT, TEXTE...)
        std::string      text;                       // la valeur actuelle (sans =)
        bool             fx{true};                   // formule (vrai) ou constante
        Style            source{Style::Empty};       // Empty : Tout ; Api, Hmi, System, Local ; Constant ; Markers : le mode repere
        hmi::DocumentPtr doc;
        hmi::Id          view{hmi::kNoId};           // la vue de la case (ses parametres)
        std::shared_ptr<const domain::Project> plc;
    };
    struct Answer {
        std::string text;
        bool        fx{true};
        std::string create;                          // non vide : creer cette variable d'abord
        bool        forced{false};                   // « Valider quand meme » (une erreur restait)
    };

    explicit HmiValuePicker(Spec spec);
    ~HmiValuePicker() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    [[nodiscard]] static Answer parse(const std::string& payload);

    // Pour les scripts et les tests.
    void setSearch(const std::string& text);
    void setTypeFilter(bool on);
    void setSource(Style s);
    bool pick(std::string_view path);                // choisir un noeud de l'arbre : il va dans Resultat
    void setResult(const std::string& text, bool fx);
    [[nodiscard]] std::string result() const;
    [[nodiscard]] bool resultFx() const;
    [[nodiscard]] std::size_t shownCount() const;    // les variables montrees (sans les groupes)
    [[nodiscard]] const valuekind::Result& classification() const;
    bool applyFix(std::size_t diag, std::size_t fix);
    void validate(bool force = false);
    void finish(bool ok);

protected:
    core::Status buildUi() override;
    void         onEnter() override;

private:
    class Body;
    Spec   spec_;
    Body*  body_{nullptr};
    bool   done_{false};
    Answer answer_;
};

} // namespace app
