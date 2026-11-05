// =============================================================================
//  hmi/HmiStyles.hpp - 1.23.0 : LES STYLES PAR TYPE D'OBJET
// -----------------------------------------------------------------------------
//  Le client (05/11) : << ajoute des styles par defaut en natif avec un cadenas,
//  pouvoir faire "dupliquer a partir de" ; ne pas gerer que les boutons : une
//  liste d'objets de la bibliotheque ou de symboles utilisateur qu'on ajoute ou
//  supprime d'un style, et dans chaque objet on choisit une propriete (peu
//  importe le type) et sa valeur ; quand on applique un style a un objet ca
//  override les parametres, on le voit dans l'instance et on peut briser
//  l'override 1 a 1 pour garder l'ancienne valeur >>.
//
//  UN STYLE (hmi::Style) : un nom, et pour chaque TYPE vise (StyleTarget) les
//  proprietes qu'il IMPOSE et leur valeur. Le type : la cle d'un genre de la
//  bibliotheque ("Button", "Pump"), "symbole:<nom>" (les instances d'un
//  symbole du projet), ou "*" (tous les objets : Style::props, les styles
//  d'avant la 1.23.0). Une cle : une propriete de l'objet ("fill", "fontSize",
//  "text"...), ou "param:<Nom>" (un parametre du symbole d'une instance).
//
//  LES STYLES FOURNIS (builtins) : ISA-101, Sombre, Clair, Contraste eleve.
//  Verrouilles (jamais modifies, jamais ecrits dans le projet) : un objet peut
//  les citer ; << Dupliquer a partir de >> en fait un style du projet.
//
//  L'ETAT D'UN OBJET STYLE, dans ses proprietes (ecrites avec lui) :
//    namedStyle  le nom du style cite (lot 12) ;
//    styleBroken les cles BRISEES : le style ne les impose plus, l'objet garde
//                sa valeur (separees par des virgules) ;
//    styleWas    la valeur de chaque cle imposee AVANT le style (pour briser en
//                la reprenant) : "cle=valeur" separees par \x1E (cle et valeur
//                echappees : \x1F pour =, au besoin).
//  Une cle IMPOSEE : le style la porte pour le type de l'objet (ou pour tous
//  les objets), elle n'est pas brisee, et l'objet ne la pilote pas par une
//  expression (une expression garde la main, comme en lot 12).
//
//  Tout se fait sur le Project / l'Object passes : les commandes (Ctrl+Z) sont
//  celles des appelants (changeProject, canvas.edit).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::styles {

inline constexpr std::string_view kAnyType = "*";
inline constexpr std::string_view kSymbolPrefix = "symbole:";
inline constexpr std::string_view kParamPrefix = "param:";

// ---- les styles fournis ------------------------------------------------------------------------------------------------
[[nodiscard]] const std::vector<Style>& builtins();
[[nodiscard]] const Style* builtin(std::string_view name) noexcept;                 // sans casse ; nul : pas un style fourni
// Un style par son nom : celui du projet, sinon un style fourni (sans casse).
[[nodiscard]] const Style* find(const Project&, std::string_view name) noexcept;
// Tous les styles qu'on peut citer : ceux du projet, puis les fournis qu'aucun style du projet ne cache.
[[nodiscard]] std::vector<const Style*> all(const Project&);

// ---- les types ---------------------------------------------------------------------------------------------------------
// Le type d'un objet pour les styles : la cle de son genre ; une instance de symbole : "symbole:<son symbole>".
[[nodiscard]] std::string typeOf(const Object&);
// "Bouton", "Symbole Sym_Moteur", "Tous les objets".
[[nodiscard]] std::string typeLabel(std::string_view type);
[[nodiscard]] bool isSymbolType(std::string_view type) noexcept;
[[nodiscard]] std::string symbolName(std::string_view type);                         // "symbole:Sym_Moteur" -> "Sym_Moteur"
// Les types qu'on peut viser : les genres de la bibliotheque (dans l'ordre de kKindInfos, sans le groupe ni
// l'instance generique), puis les symboles du projet ("symbole:<nom>").
[[nodiscard]] std::vector<std::string> availableTypes(const Project&);
[[nodiscard]] bool knownType(const Project&, std::string_view type);

// Une propriete qu'on peut imposer a un type : sa cle, son libelle, sa valeur d'un objet neuf.
struct PropChoice {
    std::string key, label, defaultValue;
};
// Celles d'un objet neuf de ce genre (sans le nom, ni l'etat du style) ; un symbole : celles d'une instance, puis
// "param:<Nom>" pour chaque parametre du symbole (sa valeur par defaut). "*" : les proprietes d'apparence (styleKeys).
[[nodiscard]] std::vector<PropChoice> propertiesOf(const Project&, std::string_view type);
[[nodiscard]] std::string propertyLabel(std::string_view key);                       // "fill" -> "Remplissage", "param:Nom" -> "Parametre Nom"

// ---- ce qu'un style impose -----------------------------------------------------------------------------------------------
// La cible d'un type (nul : aucune). `make` : la cree au besoin (vide).
[[nodiscard]] const StyleTarget* target(const Style&, std::string_view type) noexcept;
StyleTarget& targetFor(Style&, std::string_view type);
// Les valeurs que le style porte pour cet objet : celles de la cible de son type, puis celles de "*" pour les cles que la
// premiere n'a pas (la cible du type gagne). Seulement les cles que l'objet a (une propriete, un parametre de son symbole).
[[nodiscard]] std::vector<Prop> valuesFor(const Project&, const Style&, const Object&);

// ---- l'etat d'un objet --------------------------------------------------------------------------------------------------
[[nodiscard]] std::string citedStyle(const Object&);                                   // "namedStyle" ; vide : aucun
[[nodiscard]] std::vector<std::string> brokenKeys(const Object&);
[[nodiscard]] std::optional<std::string> valueBefore(const Object&, std::string_view key);
// La valeur d'une cle sur l'objet (une propriete, ou un parametre de son symbole) ; nullopt : l'objet ne l'a pas.
[[nodiscard]] std::optional<std::string> valueOf(const Project&, const Object&, std::string_view key);
// L'objet pilote-t-il la cle par une expression (le style ne l'impose alors pas) ?
[[nodiscard]] bool drivenByExpression(const Object&, std::string_view key);

// La cle est-elle IMPOSEE par le style que l'objet cite ? `by` : ce style ; `value` : la valeur imposee.
[[nodiscard]] bool imposes(const Project&, const Object&, std::string_view key, const Style** by = nullptr,
                           std::string* value = nullptr);
// Les cles imposees a l'objet (dans l'ordre du style).
[[nodiscard]] std::vector<std::string> imposedKeys(const Project&, const Object&);

// ---- les gestes (sur l'objet ; l'appelant en fait une commande) ---------------------------------------------------------
// APPLIQUER : chaque cle que le style porte pour l'objet (et qu'une expression ne pilote pas) prend la valeur du style ;
// la valeur d'avant est retenue (styleWas) ; les cles brisees d'un style precedent sont oubliees ; l'objet le cite.
// Rend le nombre de valeurs changees.
std::size_t apply(const Project&, Object&, const Style&);
// Ne plus citer de style : les valeurs restent, l'etat s'efface (namedStyle, styleBroken, styleWas).
void detach(Object&);
// BRISER une cle imposee : elle reprend sa valeur d'avant le style (`keepCurrent` : garde la valeur actuelle), et le style
// ne l'impose plus. Faux : la cle n'etait pas imposee.
bool breakKey(const Project&, Object&, std::string_view key, bool keepCurrent = false);
// RETABLIR une cle brisee : elle reprend la valeur du style. Faux : pas brisee, ou le style ne la porte pas.
bool restoreKey(const Project&, Object&, std::string_view key);
// Une saisie sur une cle imposee (l'inspecteur) : la cle est brisee, la saisie gardee (appeler apres l'ecriture).
// Faux : elle n'etait pas imposee (rien ne change).
bool edited(const Project&, Object&, std::string_view key);
// Toutes les cles brisees reprennent la valeur du style.
std::size_t restoreAll(const Project&, Object&);

// LE STYLE CHANGE (ou est renomme) : les objets qui citent `before` suivent `after` - les cles imposees prennent la
// nouvelle valeur ; une cle nouvellement portee retient la valeur d'avant ; une cle que le style ne porte plus garde sa
// valeur. Une valeur que l'objet a changee lui-meme (differente de l'ancienne valeur du style) est brisee, pas ecrasee
// (comme en lot 12). Rend le nombre d'objets changes.
std::size_t propagate(Project&, const Style& before, const Style& after);
// Les objets qui citent le style (dans toutes les vues).
[[nodiscard]] std::size_t users(const Project&, std::string_view name);
// Retirer un style du projet : ses objets le perdent (leurs valeurs restent). Rend le nombre d'objets.
std::size_t forget(Project&, std::string_view name);
// UN PROJET D'AVANT LA 1.23.0 : un objet qui cite un style sans etat - une valeur qui differe de celle du style devient
// brisee (elle reste la sienne), les autres sont imposees (styleWas : leur valeur d'objet neuf). Rend le nombre d'objets.
std::size_t adopt(Project&);

// ---- faire un style ------------------------------------------------------------------------------------------------------
// Le style fait d'un objet (<< Creer un style >>) : la cible de son type, avec ses valeurs d'apparence (styleKeys) qu'une
// expression ne pilote pas. Identifiant attribue, nom unique.
[[nodiscard]] Style fromObject(Project&, const Object&, const std::string& name);
// DUPLIQUER A PARTIR DE : une copie modifiable (un style du projet : identifiant, nom unique, plus verrouillee).
[[nodiscard]] Style duplicate(Project&, const Style&, const std::string& name = {});
// Un style du projet ne peut pas porter le nom d'un style fourni (il le cacherait) : vrai si `name` est libre.
[[nodiscard]] bool nameFree(const Project&, std::string_view name, Id except = kNoId);
// Un nom libre (nameFree) pour un style du projet : `wanted`, sinon "wanted 2", "wanted 3"... ; vide : "Style".
// (1.23.0 : le Nouveau style de l'onglet Styles - jamais le nom d'un style fourni.)
[[nodiscard]] std::string uniqueName(const Project&, std::string wanted);

// ---- verifier ----------------------------------------------------------------------------------------------------------
// Ce qui ne va pas dans un style : un type inconnu (un symbole supprime), une propriete que le type n'a pas, une valeur
// vide. Une phrase par probleme.
[[nodiscard]] std::vector<std::string> problems(const Project&, const Style&);

// Les objets d'apercu d'un style pour un type (l'onglet Styles) : un objet neuf de ce genre (une instance du symbole),
// le style applique. Vide : type inconnu.
[[nodiscard]] Object previewObject(const Project&, const Style&, std::string_view type, Id id = 1);

} // namespace hmi::styles
