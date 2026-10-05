// =============================================================================
//  app/hmi/HmiActionDialogs.hpp - 1.11.7 : les petites fenetres des actions
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : « executer un script -> pouvoir ouvrir un petit
//  modal pour editer le script, avoir tous les memes principes que les autres
//  scripts, acces a toutes les variables + aide a la saisie + references » ;
//  « maths -> aide aux formules avec mini modal si on edite les params, pouvoir
//  ajouter n param qui seront des references obligatoirement avec un mode test
//  pour tester la formule ».
//
//  LE SCRIPT D'UNE ACTION (Executer un script) :
//
//    +-- Script de l'action · Bouton_1 · n° 2 ----------------------------- x --+
//    | +-- l'editeur (ST, numeros, couleurs) ------+ +-- Variables et refs ---+ |
//    | | Compteur := Compteur + 1;                 | | [Chercher...........]  | |
//    | | IHM_JOURNAL('remis');                     | | R Moteur  (parametre)  | |
//    | +-------------------------------------------+ | I Compteur INT         | |
//    | Compteur : variable IHM · INT = 12            | A Armoires ARRAY...    | |
//    | 1 erreur : ligne 2, ...                       +------------------------+ |
//    |                                      [Annuler] [Valider (Ctrl+Entree)]  |
//    +--------------------------------------------------------------------------+
//
//  - l'editeur des scripts de l'application : la couleur du ST, les numeros de
//    ligne, l'aide a la saisie (les noms, les membres, les signatures, l'infobulle) ;
//  - la ligne du nom sous le curseur (ce qu'il est, son type, sa valeur en marche) ;
//  - les fautes, a leur ligne (les memes que Compiler, avec la vue : ses parametres) ;
//  - a droite, tout ce que le script peut lire : les REFERENCES (les parametres de la
//    vue), les variables IHM, l'automate, SYS. ; un double-clic insere le nom.
//
//  MATHS :
//
//    +-- Maths · la formule de l'action -------------------------------- x --+
//    | Resultat dans  [Sortie_Four___________________________]                 |
//    | References (des variables)                          [+ Ajouter]         |
//    |   Nom        Reference                    Valeur de test                |
//    |   [Mesure ]  [Armoires[0].ana.PT1.mes ]   [12,5 ]   x                    |
//    | Formule        [(Mesure - Consigne) * Gain___________________]          |
//    | Fonctions : ABS SQRT MIN MAX LIMIT SIN COS EXP LN TRUNC (un clic insere)|
//    | (les fautes : une reference qui n'est pas une variable, un nom double...) |
//    | [Tester]  = 5   ((12.5 - 10) * 2)                                       |
//    |                                                  [Annuler] [Valider]    |
//    +-------------------------------------------------------------------------+
//
//  1.11.9 : L'OPERATION EN ARBRE (« trier les actions disponibles avec un treeview
//  utile et efficace ») - les familles (Variables, Navigation, Popups...), leurs
//  operations dessous ; la recherche garde ce qu'elle trouve (le nom, la famille,
//  ce que l'operation fait) ; en bas, la phrase de l'operation choisie ; un
//  double-clic (ou Entree) la prend.
// =============================================================================
#pragma once

#include "../../hmi/HmiActionKinds.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../menu/IMenu.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace domain { class Project; }

namespace app {

class HmiActionScriptDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string      where;      // « Bouton_1 · action n° 2 »
        std::string      code;
        hmi::DocumentPtr doc;
        hmi::Id          view{hmi::kNoId};
        std::shared_ptr<const domain::Project> plc;
        // 1.11.10 : la redefinition d'une fonction de symbole dans une instance - le corps est
        // controle comme une fonction (ses VAR_INPUT, son resultat) ; `title` : le titre.
        std::optional<hmi::HmiFunction> function{};
        std::string      title{};
    };
    explicit HmiActionScriptDialog(Spec spec);
    ~HmiActionScriptDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Pour les tests et les scripts.
    [[nodiscard]] std::string code() const;
    void setCode(const std::string& code);
    [[nodiscard]] std::size_t errorCount() const;
    [[nodiscard]] std::vector<std::string> names() const;      // la liste de droite, montree
    void setSearch(const std::string& text);
    bool insertName(const std::string& name);                  // comme un double-clic
    void finish(bool ok);

protected:
    core::Status buildUi() override;
    void         onEnter() override;

private:
    class Body;
    Spec  spec_;
    Body* body_{nullptr};
    bool  done_{false};
};

class HmiOperationDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        hmi::Operation current{hmi::Operation::Set};
        std::string    where;        // « Bouton_1 · n° 1 »
    };
    explicit HmiOperationDialog(Spec spec);
    ~HmiOperationDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    // La reponse : le libelle de l'operation (operationFromLabel le relit).
    [[nodiscard]] static std::optional<hmi::Operation> parse(const std::string& payload);

    // Pour les tests et les scripts.
    void setSearch(const std::string& text);
    [[nodiscard]] std::vector<std::string> groupsShown() const;            // les familles montrees
    [[nodiscard]] std::vector<std::string> operationsShown() const;        // les operations montrees, dans l'ordre
    bool choose(hmi::Operation);                                          // la ligne choisie
    [[nodiscard]] hmi::Operation chosen() const;
    [[nodiscard]] std::string helpLine() const;                           // la phrase du bas
    void finish(bool ok);

protected:
    core::Status buildUi() override;
    void         onEnter() override;

private:
    class Body;
    Spec  spec_;
    Body* body_{nullptr};
    bool  done_{false};
};

class HmiMathsDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string                    where;
        std::string                    target;
        std::string                    formula;
        hmi::actionkinds::Params       refs;
        hmi::DocumentPtr               doc;
        hmi::Id                        view{hmi::kNoId};
    };
    struct Answer {
        std::string              target, formula;
        hmi::actionkinds::Params refs;
    };
    explicit HmiMathsDialog(Spec spec);
    ~HmiMathsDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    [[nodiscard]] static Answer parse(const std::string& payload);

    // Pour les tests et les scripts.
    void setTarget(const std::string& t);
    void setFormula(const std::string& f);
    std::size_t addReference(const std::string& name = {}, const std::string& path = {}, const std::string& test = {});
    bool removeReference(std::size_t i);
    void setTestValue(std::size_t i, const std::string& v);
    [[nodiscard]] std::size_t referenceCount() const;
    [[nodiscard]] Answer answer() const;
    [[nodiscard]] std::vector<hmi::actionkinds::MathsIssue> issues() const;
    hmi::actionkinds::MathsTest test();                       // le bouton Tester
    bool insertFunction(const std::string& name);             // un clic sur une fonction
    void validate();                                          // le bouton Valider (refuse s'il reste une faute)
    void finish(bool ok);

protected:
    core::Status buildUi() override;
    void         onEnter() override;

private:
    class Body;
    Spec  spec_;
    Body* body_{nullptr};
    bool  done_{false};
};

} // namespace app
