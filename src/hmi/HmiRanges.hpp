// =============================================================================
//  hmi/HmiRanges.hpp - 1.12.10 (lot F de l'audit des objets) : LA PLAGE ET LES
//                      SEUILS DE LA VARIABLE
// -----------------------------------------------------------------------------
//  Avant, la plage 0..10 bar se retapait sur chaque jauge, bargraphe, curseur,
//  cuve et courbe ; un seuil hors de la plage disparaissait en silence ; un nom
//  de variable tape dans un seuil etait ignore (ou refuse).
//
//  LA VARIABLE PORTE SA PLAGE. Configuration > Unites et formats donne a une
//  variable, avec son unite et son format : Min, Max, Seuil bas, Seuil haut,
//  Alarme basse, Alarme haute (VariableDisplay). Pour une variable liee a un
//  equipement, la mise a l'echelle (engMin, engMax) donne Min et Max tant que la
//  ligne ne les dit pas.
//
//  LES OBJETS EN HERITENT comme de l'unite et du format (la case « Unite, format
//  et plage de la variable », varFormat) - LA REGLE :
//   - une cle LAISSEE VIDE sur l'objet prend la valeur de sa variable (sinon celle
//     d'un objet neuf de son genre : 0..100, -20..60...) ; ce qu'on regle a la
//     main sur l'objet l'emporte ;
//   - un objet pose depuis la palette nait avec Min et Max vides (followVariable) ;
//     un objet d'un projet existant garde ses valeurs ecrites : il ne change pas ;
//   - la mise a l'echelle ne sert qu'aux cles qui avaient une valeur par defaut
//     (Min, Max d'une jauge, d'un curseur...) : les bornes vides d'un champ de
//     saisie (« sans borne ») et l'ouverture d'une vanne ne suivent qu'une ligne
//     d'Unites et formats, reglee par l'utilisateur ;
//   - un objet qui decoche la case garde tout ce qui est a lui.
//
//  UN NOM SANS = (Consigne_Max) dans Min, Max, un seuil, la consigne : la
//  variable, lue comme une formule (comme l'axe X de la courbe XY). Sur l'objet
//  comme dans Unites et formats ; un nom inconnu : Generer le dit.
//
//  OU : le moteur compose la vue (Runtime::viewOf) puis apply() - le dessin, le
//  clic et le moteur lisent les memes bornes ; l'editeur dessine l'objet ainsi
//  (preview) ; l'inspecteur montre la valeur heritee en gris (hint).
// =============================================================================
#pragma once

#include "HmiCheck.hpp"
#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::ranges {

// Les six cles d'une plage, dans l'ordre des colonnes d'Unites et formats.
inline constexpr std::string_view kKeys[] = {"min", "max", "low", "high", "lowAlarm", "highAlarm"};
// Les quatre seuils, dans leur ordre : alarme basse < seuil bas < seuil haut < alarme haute.
inline constexpr std::string_view kThresholds[] = {"lowAlarm", "low", "high", "highAlarm"};

// Le champ de la ligne pour cette cle (une autre cle : un texte vide).
[[nodiscard]] const std::string& field(const VariableDisplay&, std::string_view key);
[[nodiscard]] std::string* fieldOf(VariableDisplay&, std::string_view key);
// Son libelle : Min, Max, Seuil bas, Seuil haut, Alarme basse, Alarme haute.
[[nodiscard]] std::string label(std::string_view key);

// Vrai : le texte est un nom de variable (un chemin, ses reperes $...$ retires), pas un nombre, ni
// TRUE / FALSE, ni un calcul.
[[nodiscard]] bool isName(std::string_view text);
// Une valeur de plage tapee : vide, un nombre (la virgule acceptee) ou un nom de variable.
[[nodiscard]] bool validValue(std::string_view text);

// ---- ce que donne la variable ------------------------------------------------------------
struct Inherited {
    std::string value;          // "10", "Consigne_Max" ; vide : rien
    std::string origin;         // "Pression_Ligne" (sa ligne), "mise \xC3\xA0 l'\xC3\xA9" "chelle de Pression_Ligne"
    bool        scaling{false}; // vient de la mise a l'echelle
};
// La mise a l'echelle de la variable liee a un equipement que ce chemin designe (sa racine) ;
// faux : aucune.
[[nodiscard]] bool scalingOf(const Project&, std::string_view path, double& lo, double& hi);
// Ce que la variable donne pour une cle : sa ligne d'Unites et formats, sinon (Min, Max) la mise a
// l'echelle si `scaling`. `in` : la vue (un parametre de popup se lit comme ce qu'il designe).
[[nodiscard]] Inherited inheritedOf(const Project&, std::string_view path, std::string_view key, const View* in = nullptr,
                                    bool scaling = true);

// ---- ce qu'un objet en prend -------------------------------------------------------------
// Les cles qu'un objet de ce genre peut prendre a sa variable (vide : aucune). La vanne : son
// ouverture (openingMin, openingMax) ; la courbe : son axe Y (ymin, ymax).
[[nodiscard]] std::vector<std::string_view> inheritedKeys(Kind);
// Le genre a-t-il une plage ou des seuils a prendre a sa variable ?
[[nodiscard]] bool inherits(Kind);
// Une cle de ce genre accepte-t-elle un nom de variable sans = ? (Les cles de inheritedKeys et la
// consigne d'un bargraphe.)
[[nodiscard]] bool takesName(Kind, std::string_view key);
// La valeur d'un objet neuf de ce genre pour cette cle ("0", "100", "" : aucune).
[[nodiscard]] std::string defaultOf(Kind, std::string_view key);
// La variable dont l'objet prend la plage : celle qu'il montre (sa Valeur, sa variable ; une Valeur
// de symbole tapee sans =), sans ses reperes ; la vanne : celle de son ouverture ; "" : aucune.
[[nodiscard]] std::string variableOf(const Object&);
// Ce qu'une cle vide de l'objet prendrait : la valeur de sa variable et d'ou elle vient ; rien :
// value vide (la courbe : l'union des plages de ses plumes).
[[nodiscard]] Inherited inheritedFor(const Project&, const Object&, std::string_view key, const View* in = nullptr);

// L'OBJET TEL QU'IL TOURNE : chaque cle heritee laissee vide (et sans formule) prend la valeur de sa
// variable, sinon celle d'un objet neuf ; une valeur qui est un nom de variable devient une formule
// (sa valeur fixe : celle d'un objet neuf, si la formule ne se lit pas) ; la vanne lit son ouverture
// en % de sa plage. Vrai : l'objet a change.
bool apply(Object&, const Project&, const View* in = nullptr);
// Toute la vue (ses objets). Vrai : un objet a change.
bool apply(View&, const Project&);
// La vue a-t-elle un objet que apply changerait ? (Le moteur ne compose qu'alors.)
[[nodiscard]] bool needed(const Project&, const View&);
// L'editeur : l'objet a dessiner (vrai), ses cles vides remplies comme en marche.
[[nodiscard]] bool preview(const Project&, const View&, const Object&, Object& out);

// L'inspecteur : le texte gris d'une cle vide - « 10 (de Pression_Ligne) », « 0 (mise a l'echelle de
// Debit) », « 100 (par defaut) » ; vide : rien a dire.
[[nodiscard]] std::string hint(const Project&, const View&, const Object&, std::string_view key);

// Un objet pose depuis la palette : ses Min et Max vides - il suit la plage de sa variable (et, sans
// elle, montre celle d'un objet neuf). Les autres genres : rien.
void followVariable(Object&);

// GENERER (des avertissements, rien ne bloque) : sur chaque objet, ses bornes et seuils tels qu'il les
// lira (heritees comprises) - Min >= Max, un seuil hors de [Min, Max], des seuils dans le desordre,
// un nom inconnu ; dans Unites et formats, les memes sur chaque ligne.
[[nodiscard]] std::vector<Issue> check(const Project&, const NameExists& plc);

} // namespace hmi::ranges
