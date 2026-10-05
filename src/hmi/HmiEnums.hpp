// =============================================================================
//  hmi/HmiEnums.hpp - les enumerations IHM (1.10, decision 15, chantier E)
// -----------------------------------------------------------------------------
//  UNE ENUMERATION IHM est un type IHM (HmiType, kind = Enumeration) fait de
//  valeurs ordonnees (HmiEnumValue : nom ST, nombre DINT, texte affiche,
//  description) : T_MODE = Arret (0), Auto (1), Manu (2), Defaut (9).
//  Dans les scripts (S1) : le litteral T_MODE#Auto, CASE, FOR EACH v IN T_MODE.
//  SON toString ET SON fromString sont deux operateurs de conversion (S2,
//  HmiOperators.hpp) crees avec elle, preremplis, modifiables comme les autres :
//      TO_STRING(T_MODE) : STRING     rend le texte affiche (CASE sur les valeurs)
//      TO_T_MODE(STRING) : T_MODE     accepte le nom ou le texte ; sinon la premiere valeur
//  Ici : trouver une enumeration et ses valeurs, les textes, les controles, la
//  valeur suivante proposee, les scripts preremplis, le renommage dans les scripts.
//  Les noms (d'enumeration, de valeur) se comparent sans casse, comme en ST.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ------------------------------------------------------------- les genres ---
[[nodiscard]] bool isEnumeration(const HmiType&) noexcept;
// Pour l'enregistrement ("structure", "enumeration") et retour (inconnu : Structure).
[[nodiscard]] std::string_view typeKindKey(HmiTypeKind) noexcept;
[[nodiscard]] HmiTypeKind      typeKindFromKey(std::string_view key) noexcept;
// En mots, pour l'ecran : "structure", "\xC3\xA9num\xC3\xA9ration".
[[nodiscard]] std::string_view typeKindLabel(HmiTypeKind) noexcept;

// ---------------------------------------------------------------- trouver ---
// L'enumeration de ce nom (sans casse) ; nul : aucune (ou c'est une structure).
[[nodiscard]] const HmiType* findEnumeration(const Project&, std::string_view name);
[[nodiscard]] HmiType*       findEnumeration(Project&, std::string_view name);
// Les enumerations du projet, dans l'ordre de la liste des types.
[[nodiscard]] std::vector<const HmiType*> enumerations(const Project&);
// La valeur de ce nom (sans casse) ; nul : aucune.
[[nodiscard]] const HmiEnumValue* enumValueByName(const HmiType&, std::string_view name);
// La premiere valeur de ce nombre ; nul : aucune.
[[nodiscard]] const HmiEnumValue* enumValueByNumber(const HmiType&, std::int64_t value);
// Le rang (0, 1...) de la valeur de ce nom ; -1 : aucune.
[[nodiscard]] int enumValueIndex(const HmiType&, std::string_view name);
// Le texte affiche : le texte de la valeur, ou son nom s'il est vide.
[[nodiscard]] std::string enumText(const HmiEnumValue&);
// Le litteral ST : "T_MODE#Auto".
[[nodiscard]] std::string enumLiteral(const HmiType&, const HmiEnumValue&);
// "T_MODE#Auto" (sans casse, espaces autour admis) -> l'enumeration et la valeur.
// Faux : pas un litteral d'une enumeration du projet. `type`, `value` : peuvent etre nuls.
bool parseEnumLiteral(const Project&, std::string_view text, const HmiType** type, const HmiEnumValue** value);
// Ce que montre une variable de ce type : "Auto (1)" ; un nombre sans valeur : "? (5)".
[[nodiscard]] std::string enumDisplay(const HmiType&, std::int64_t value);
// Une valeur tapee - le nom ("Auto"), le litteral ("T_MODE#Auto"), le texte affiche
// ("Automatique"), sans casse, ou un nombre qui est celui d'une valeur ("1") - -> son
// nombre. Faux : aucune valeur ne correspond.
bool enumNumberOf(const HmiType&, std::string_view text, std::int64_t& out);

// ----------------------------------------------------------- les controles ---
struct EnumIssue {
    int         index{-1};        // la valeur (0, 1...) ; -1 : l'enumeration elle-meme
    std::string field;            // "nom", "valeur" ; vide : l'enumeration
    std::string message;          // en francais, pour l'ecran
};
// Un identifiant ST : une lettre ou _, puis lettres, chiffres, _ ; pas de __, pas de _
// a la fin ; pas un mot reserve (IF, CASE, TRUE, AND...).
[[nodiscard]] bool validEnumName(std::string_view name);
// Ce qui ne va pas : enumeration sans valeur ; nom vide, invalide ou en double (sans
// casse) ; nombre en double ; nombre hors des DINT. Vide : tout va.
[[nodiscard]] std::vector<EnumIssue> enumIssues(const HmiType&);

// ------------------------------------------------------------- proposer -----
// Le nombre suivant : le plus grand + 1 (0 pour une enumeration vide).
[[nodiscard]] std::int64_t nextEnumNumber(const HmiType&);
// Un nom libre : "<base>1", "<base>2"... (base : "Valeur").
[[nodiscard]] std::string nextEnumName(const HmiType&, std::string_view base = "Valeur");
// Une valeur neuve a ajouter en fin : un nom libre et le nombre suivant.
[[nodiscard]] HmiEnumValue nextEnumValue(const HmiType&);

// ---------------------------------------------------- toString, fromString ---
// Le script prerempli du toString, TO_STRING(T_MODE) : STRING : un CASE qui rend le
// texte affiche de chaque valeur (ELSE : le nombre).
[[nodiscard]] std::string enumToStringScript(const HmiType&);
// Le script prerempli du fromString, TO_T_MODE(STRING) : T_MODE : le nom ou le texte
// affiche, tel quel, en MAJUSCULES ou en minuscules ; sinon la premiere valeur.
[[nodiscard]] std::string enumFromStringScript(const HmiType&);
// Les deux operateurs de conversion (HmiOperator, op "TO"), preremplis, sans identifiant.
[[nodiscard]] HmiOperator enumToStringOperator(const HmiType&);
[[nodiscard]] HmiOperator enumFromStringOperator(const HmiType&);
// Cet operateur est-il le toString (TO_STRING de l'enumeration vers STRING) / le
// fromString (TO_<enumeration> depuis STRING) de cette enumeration ?
[[nodiscard]] bool isEnumToString(const HmiType&, const HmiOperator&) noexcept;
[[nodiscard]] bool isEnumFromString(const HmiType&, const HmiOperator&) noexcept;
// "toString", "fromString" ou vide : ce que l'interface ecrit a cote de la signature.
[[nodiscard]] std::string_view enumConversionRole(const HmiType&, const HmiOperator&) noexcept;
// Une enumeration neuve, prete a ajouter : identifiant neuf, nom libre tire de `name`
// ("T_MODE", "T_MODE2"...), deux valeurs d'exemple (Arret = 0, Marche = 1), son
// toString et son fromString (identifiants neufs).
[[nodiscard]] HmiType makeEnumeration(Project&, std::string_view name = "T_MODE");
// Les deux operateurs repris d'apres les valeurs d'aujourd'hui (le meme identifiant ;
// ceux qui manquent sont ajoutes avec un identifiant neuf). Pour un bouton
// "Regenerer toString / fromString". Rend le nombre d'operateurs ecrits.
std::size_t regenerateEnumConversions(Project&, HmiType&);

// ------------------------------------------------------ renommer, copier -----
// Dans un texte ST : T#<from> -> T#<to> pour l'enumeration `type` (sans casse ; ni
// dans les chaines ni dans les commentaires).
[[nodiscard]] std::string renameEnumValueInText(std::string_view code, std::string_view type, std::string_view from,
                                                std::string_view to);
// Dans un texte ST : <from>#x -> <to>#x (une enumeration renommee).
[[nodiscard]] std::string renameEnumTypeInText(std::string_view code, std::string_view from, std::string_view to);
// Une valeur renommee : T_MODE#Ancien -> T_MODE#Nouveau dans tout le projet (scripts
// generaux et des vues, fonctions IHM, operateurs, actions, expressions des objets,
// conditions des alarmes). A appeler DANS la commande qui renomme (un seul Ctrl+Z).
// Rend le nombre de textes changes.
std::size_t renameEnumValue(Project&, std::string_view type, std::string_view from, std::string_view to);
// Une enumeration renommee : T_ANCIEN#x -> T_NOUVEAU#x partout (les operateurs, eux,
// suivent par renameTypeInOperators de S2). Rend le nombre de textes changes.
std::size_t renameEnumType(Project&, std::string_view from, std::string_view to);
// Une enumeration dupliquee (Dupliquer) : dans les scripts de ses operateurs (deja
// copies et renumerotes par copyOperators de S2), <from>#x -> <copie>#x.
void copyEnumerationLiterals(HmiType& copy, std::string_view from);

} // namespace hmi
