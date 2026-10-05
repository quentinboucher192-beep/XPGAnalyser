// =============================================================================
//  hmi/HmiViewPaths.hpp - 1.11.6 : les variables qu'une vue lit ou ecrit
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : dans les pages Esclaves simules, Variables IHM
//  et Variables API de la simulation, « ajouter une option 'Sur la vue actuelle' en
//  reperant les profondeurs des symboles d'instances ».
//
//  LA VUE COMPOSEE (Runtime::composedView) : les instances de symboles y sont deja
//  developpees, a toute profondeur, leurs parametres remplaces par leurs arguments
//  (Value := UINTS : Value[1] s'y lit UINTS[1]). On y lit tout ce que la marche lit :
//  les proprietes pilotees, les textes a trous, les cases des tableaux, les listes
//  des graphiques et des courbes, les etats des images animees, la variable des
//  commandes et des champs, les actions (cible, valeur, condition, surveillee,
//  parametres), les scripts de la vue. Les parametres de la vue (une popup ouverte
//  pour Pompes[3]) se resolvent par sa portee : Moteur.Marche -> Pompes[3].Marche.
//
//  UN INDEX CALCULE (V[i].Pos) se garde en joker : il couvre V[0].Pos, V[1].Pos...
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

class Scope;

namespace viewpaths {

// Les chemins d'un code ou d'une expression : "Four1.Vannes[1].Position", "V[*].Pos".
[[nodiscard]] std::vector<std::string> inCode(std::string_view code);
// ... ceux des trous d'un texte a trous ("Pression : {PT1:0.0} bar" -> PT1).
[[nodiscard]] std::vector<std::string> inTemplate(std::string_view text);
// Tous ceux d'une vue composee (sans doublon) ; `scope` : sa portee (ses parametres), ou nulle.
[[nodiscard]] std::vector<std::string> ofView(const View& composed, const Scope* scope = nullptr);

// LE FILTRE : un chemin de variable est-il lu par la vue ? Oui s'il commence par
// l'un des chemins de la vue (Four1 couvre Four1.Temperature), ou si l'un d'eux
// commence par lui ; un [*] vaut n'importe quel index ; ni la casse ni le prefixe
// API. ne comptent.
class Filter {
public:
    void set(const std::vector<std::string>& paths);
    [[nodiscard]] bool covers(std::string_view path) const;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

private:
    using Segments = std::vector<std::string>;
    std::map<std::string, std::vector<Segments>, std::less<>> byRoot_;   // par premier morceau
    std::size_t count_{0};
};

// "Four1.Vannes[1].Position" -> {"FOUR1", "VANNES", "[1]", "POSITION"} ; un index calcule : "[*]".
[[nodiscard]] std::vector<std::string> segments(std::string_view path);

} // namespace viewpaths
} // namespace hmi
