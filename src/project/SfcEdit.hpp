// =============================================================================
//  project/SfcEdit.hpp - 1.51.0 (S) : modifier un GRAFCET natif (SFC)
// -----------------------------------------------------------------------------
//  Chaque operation change le modele (sfc::Chart) ; l'ecran en ecrit le XML
//  (sfc::write) dans la section par UNE commande SfcEditCommand : un Ctrl+Z
//  defait le geste entier, quoi qu'il ait touche (l'element, ses voisins
//  decales, sa section de transition creee).
//
//  LA GRILLE. Ajouter une etape et sa transition sous un objet insere deux
//  rangees dans tout le graphe (comme on insere des lignes dans un tableur) :
//  ce qui etait dessous descend d'un bloc, chaque colonne garde ses voisins, et
//  les liens et leurs coudes suivent. Une convergence dont l'objet etait une
//  entree descend avec lui ; les autres entrees l'y rejoignent par un lien.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "Sfc.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sfc {

    struct EditResult {
        bool        ok{ false };
        std::string message;      // en francais : ce qui a ete fait, ou pourquoi rien
        int         select{ -1 }; // l'element a choisir ensuite (indice dans items)
    };

    // Un nom libre : prefix + numero, ni dans le graphe ni dans `taken` (en majuscules).
    [[nodiscard]] std::string freeStepName(const Chart&, const std::set<std::string>& takenUpper,
                                           const std::string& prefix = "S_");
    [[nodiscard]] std::string freeSourceName(const Chart&, const std::string& base,
                                             const std::set<std::string>& takenUpper);
    // Un nom d'etape valable (lettre ou _, puis lettres, chiffres, _).
    [[nodiscard]] bool validName(std::string_view name);
    // Une receptivite qui est une variable (A, B.C, T[2].X, %M10, TRUE) - sinon une expression.
    [[nodiscard]] bool plainVariable(std::string_view text);

    // Sous une etape : une transition puis une etape ; sous une transition : une
    // etape puis une transition. `condition` : la receptivite de la nouvelle
    // transition (vide : a ecrire).
    EditResult addStepBelow(Chart&, int item, const std::string& stepName, const std::string& condition = {},
                            const std::string& sectionForExpression = {});
    // Une divergence en OU sous l'etape (ou une branche de plus a celle qui y est) :
    // une transition, une etape, une transition et un saut vers l'etape qui suit.
    EditResult addAlternative(Chart&, int step, const std::string& stepName);
    // Une divergence en ET sous la transition (ou une branche de plus) : une etape
    // de plus, rejointe par une convergence en ET avant la transition suivante.
    EditResult addSimultaneous(Chart&, int transition, const std::string& stepName);
    // Un saut vers `target` sous la transition (sous une etape : une transition puis le saut).
    EditResult addJump(Chart&, int item, const std::string& target);
    // La receptivite : une variable (NOT X : inversee), ou une expression - elle
    // part alors dans une section de transition ST (`sectionName`, creee au besoin).
    EditResult setCondition(Chart&, int transition, const std::string& text, const std::string& sectionName);
    // Une section de transition existante (ST, LD...) devient la receptivite.
    EditResult useTransitionSection(Chart&, int transition, const std::string& sectionName);
    EditResult setActions(Chart&, int step, std::vector<Action> actions);
    EditResult renameStep(Chart&, int step, const std::string& name);
    EditResult setStepProperties(Chart&, int step, bool initial, const std::string& tmin,
                                 const std::string& tmax, const std::string& delay, const std::string& comment);
    EditResult removeItem(Chart&, int item);
    EditResult moveItem(Chart&, int item, int x, int y);
    // Le texte d'une section de transition ou d'action ST (le volet Code).
    EditResult setSourceBody(Chart&, const std::string& tag, const std::string& name, const std::string& body);

} // namespace sfc

namespace project {

    using SfcProjectPtr = std::shared_ptr<domain::Project>;

    // Le corps d'une section SFC remplace par un autre, en un pas : jamais fusionne
    // avec le suivant (chaque geste de l'editeur est un Ctrl+Z).
    class SfcEditCommand final : public core::ICommand {
    public:
        SfcEditCommand(SfcProjectPtr project, domain::Index section, std::string body, std::string label);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override { return label_; }

        [[nodiscard]] domain::Index section() const noexcept { return section_; }

    private:
        SfcProjectPtr project_;
        domain::Index section_;
        std::string   newBody_, oldBody_;
        std::string   label_;
        bool          captured_{ false };
    };

} // namespace project
