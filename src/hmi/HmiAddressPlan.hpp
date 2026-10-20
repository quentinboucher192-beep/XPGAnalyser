// =============================================================================
//  hmi/HmiAddressPlan.hpp - « Modifier l'adresse dans toutes les cases... » (1.12.18)
// -----------------------------------------------------------------------------
//  Planches 11 et 12 de la maquette du clic droit. Un tableau de structures lie a
//  un equipement (EXTRACTEURS : ARRAY[0..9] OF EXTRACTEUR, a partir de %MW550, 6
//  mots chacune) ; l'automate range un membre ailleurs (tous les Retour a partir
//  de %MW1000, de 10 en 10). Une formule de i, pas dix adresses :
//      [*].Retour = %MW(1000 + 10 * i)
//  ou les cases entieres ([*] = %MW(2000 + 8 * i) : chaque case part de son mot,
//  ses membres suivent). « A la suite d'une adresse » ecrit la formule qui range
//  les cases l'une apres l'autre (un INT : les mots qui suivent ; un BOOL en %M :
//  les bits ; un BOOL dans un mot, %MW551.0 : les bits 1, 2... puis le mot
//  suivant ; deux dimensions : ligne par ligne).
//  L'APERCU montre chaque case avant et apres, et celles qu'une autre variable a
//  deja : la case la prend quand meme, l'autre garde la sienne (rien n'est efface
//  de son cote). 1.12.19 : deux cases de la meme variable a la meme place non plus
//  ne bloquent pas - modifier pour toutes les cases accepte les chevauchements,
//  l'apercu les montre. CE QUI BLOQUE : la formule (un nom inconnu, une division
//  par zero), une adresse hors des zones. Le calcul des formules est hmi::types
//  (isFormula, formulaAddress).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::addr {

// Le motif d'un membre choisi : "[0].Retour" -> "[*].Retour" ; `wholeCases` : les cases qui le
// contiennent ("[0].Retour" -> "[*]", "[1].V[2].P" -> "[*].V[*]") ; vide : pas d'indice.
[[nodiscard]] std::string patternOf(std::string_view rel, bool wholeCases);

struct Row {
    std::string              cell;         // la case : "[2]", "[1,2]", "[1].V[5]"
    std::string              target;       // ce qui change dans la case : "[2].Retour", "[2]"
    std::string              before;       // son adresse aujourd'hui (une case entiere : son depart)
    std::string              after;        // avec la formule ; vide : `error`
    std::string              error;
    std::vector<std::string> sharedWith;   // ce qui est a la meme place : une autre variable ("POMPES[0].Etat"),
                                           // ou une autre case de la meme ("EXTRACTEURS[1].Retour")
};
struct Plan {
    std::string              pattern;      // "[*].Retour"
    std::string              formula;      // "%MW(1000 + 10 * i)"
    std::string              what;         // "Retour, INT, 1 mot" ; "les cases entieres, 8 membres, 6 mots"
    std::vector<Row>         rows;
    std::vector<std::string> errors;       // ce qui grise Appliquer (le premier dit pourquoi)
    std::size_t              shared{0};    // les cases dont la place est deja prise ailleurs
    [[nodiscard]] bool ok() const noexcept { return errors.empty() && !rows.empty(); }
};
// Ce que le motif designe, pour la fenetre.
struct Shape {
    std::string path;              // "EXTRACTEURS[i].Retour", "EXTRACTEURS[i, j]"
    std::string text;              // "INT, 1 mot" ; "BOOL, 1 bit" ; "8 membres, 6 mots"
    std::string indices;           // "i de 0 \xC3\xA0 9" ; "i de 0 \xC3\xA0 3, j de 0 \xC3\xA0 9"
    std::size_t cases{0};
    int         dims{0};           // les indices de chaque case (i, ou i et j)
    bool        leaf{false};       // un membre simple : la formule donne son adresse (sinon le depart de la case)
    bool        boolInWord{false}; // un BOOL range dans un mot : la fenetre montre la case du bit
    bool        bits{false};       // la variable est dans une zone de bits (%M, 00001)
};
[[nodiscard]] Shape shape(const Project&, const Variable&, std::string_view pattern);
// L'apercu : chaque case avec la formule, sans rien changer au projet.
[[nodiscard]] Plan preview(const Project&, const Variable&, std::string_view pattern, std::string_view formula);
// « A la suite d'une adresse » : la formule qui range les cases l'une apres l'autre a partir de
// `start` ; vide et `why` : une adresse illisible, un type qui ne tient pas, plus de deux indices.
[[nodiscard]] std::string following(const Project&, const Variable&, std::string_view pattern, std::string_view start,
                                    std::string* why = nullptr);
// La formule de la place d'aujourd'hui (pour preremplir la fenetre) : "%MW(550 + 6 * i)" ; vide : aucune.
[[nodiscard]] std::string current(const Project&, const Variable&, std::string_view pattern);
// La variable avec la formule : les adresses que le motif couvre (par case, ou une autre formule)
// retirees, puis la sienne ; `formula` vide : retirees seulement (revenir aux adresses calculees).
[[nodiscard]] Variable withFormula(const Variable&, std::string_view pattern, std::string_view formula);
// Les places de `v` que le motif couvre (pour dire ce qu'Appliquer remplace).
[[nodiscard]] std::size_t coveredPlaces(const Variable&, std::string_view pattern);

// La formule decoupee pour la fenetre : "%MW(1000 + 10 * i).0" -> "%MW", "1000 + 10 * i", "0" ;
// "%MW(551 + i / 16).(i MOD 16)" -> ..., "i MOD 16" ; faux : pas de cette forme.
bool split(std::string_view formula, std::string& prefix, std::string& expression, std::string& bit);
// Et recollee : "%MW" + "1000 + 10 * i" (+ ".0", ou ".(i MOD 16)").
[[nodiscard]] std::string join(std::string_view prefix, std::string_view expression, std::string_view bit = {});

} // namespace hmi::addr
