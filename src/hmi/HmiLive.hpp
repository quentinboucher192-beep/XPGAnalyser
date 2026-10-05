// =============================================================================
//  hmi/HmiLive.hpp - une vue en marche : ses expressions evaluees contre
//                    l'automate simule, a chaque cycle
// -----------------------------------------------------------------------------
//  bind() compile, une fois par version de la vue, chaque propriete pilotee par
//  une expression et chaque texte a trous ("Temperature : {Temperature} degC").
//  evaluate() rend la vue TELLE QU'ON LA VOIT EN MARCHE : une copie ou chaque
//  propriete pilotee porte la valeur de son expression. Le dessin n'a donc rien
//  a savoir : il dessine une vue, avec les memes regles que dans l'editeur -
//  position, taille, rotation, couleur, visibilite, texte, valeur d'une jauge.
//
//  UNE EXPRESSION FAUSSE NE CASSE PAS LA VUE. Sa propriete garde la valeur
//  statique, et l'erreur est rapportee (LiveValue::error) pour etre montree.
//
//  Sans ecran : les tests (hmi_test) l'evaluent contre le vrai simulateur.
// =============================================================================
#pragma once

#include "HmiExpr.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hmi {

struct LiveValue {
    Id          object{kNoId};
    std::string objectName;
    std::string key;           // la propriete ("visible", "value", "text"...)
    std::string expression;    // la source : "Armoires[i].prete" ou le texte a trous
    std::string value;         // le resultat, en texte ; le message si erreur
    bool        error{false};
    // Lot 14 : relie a un automate reel, la pire qualite des variables lues
    // (0 bonne, 1 ancienne, 2 mauvaise) et pourquoi.
    std::uint8_t quality{0};
    std::string  qualityWhy;
};

// Lot 9 : ce que montre la "value" d'un objet sans expression - la variable du
// champ de saisie, le retour d'etat (ou la variable, le voyant du bouton
// lumineux) d'une commande, la variable d'un afficheur ; "" : sa valeur statique.
// LiveView la relie ainsi, et les variables d'instances (Vue.Objet.Value) la lisent.
[[nodiscard]] std::string autoValueSource(const Object&);

class LiveView {
public:
    void bind(const View& view);
    [[nodiscard]] Id viewId() const noexcept { return source_.id; }
    // La vue liee (pour savoir s'il faut relier : elle a change, ou son modele).
    [[nodiscard]] const View& source() const noexcept { return source_; }
    [[nodiscard]] std::size_t expressionCount() const noexcept { return bound_.size(); }

    // `scope` : les noms propres a la vue (un index `i`...), lus avant l'automate.
    // `seconds` (lot 6) : l'instant, pour le defilement des images animees.
    [[nodiscard]] View evaluate(sim::Environment& plc, std::vector<LiveValue>* values = nullptr,
                                const Scope* scope = nullptr, double seconds = 0.0) const;

private:
    struct Bound {
        Id           object{kNoId};
        std::string  key;
        bool         isText{false};
        Expression   expr;
        TextTemplate text;
        // lot 6 : une case de tableau (ligne, colonne) ; un etat d'image animee.
        int          row{-1}, col{-1};
        int          state{-1};
        // lot 11 : un element d'une liste (barres, parts, axes d'un radar, lignes
        // d'un tableau de variables) : son rang ; la valeur va dans "liveValues"
        // (ou "liveReferences" pour la consigne d'un radar).
        int          item{-1};
    };
    View               source_;
    std::vector<Bound> bound_;
};

} // namespace hmi
