// =============================================================================
//  hmi/HmiPopupParams.hpp - 1.9 : les parametres des popups
// -----------------------------------------------------------------------------
//  UNE POPUP DECLARE SES PARAMETRES DANS UNE LISTE : un nom, un type (BOOL,
//  INT... un type IHM du projet, un DDT du programme de l'API, ARRAY[a..b] OF
//  <type> ; vide : ANY), un mode (Reference, Copie, Les deux), une valeur par
//  defaut, une description. Ce module dit :
//   - les noms affiches des modes et leurs pastilles (REF, COPIE, LES DEUX) ;
//   - les types proposes pour un projet (base, types IHM, DDT du programme) ;
//   - si un type en accepte un autre (la regle de compatibilite) ;
//   - l'analyse des arguments d'une action Ouvrir une popup contre la liste
//     de la popup (inconnu, manquant, type incompatible, pas une variable) ;
//   - les infos : qui ouvre une popup, ou chaque parametre est employe.
//
//  LA REGLE DE COMPATIBILITE (typeAccepts) : ANY (ou un type vide) accepte
//  tout et se donne a tout ; un type identique (sans casse) passe ; un entier
//  accepte un entier dont toutes les valeurs tiennent dedans (INT accepte
//  SINT, BYTE ; DINT accepte INT, UINT, WORD ; UDINT accepte UINT, WORD...) ;
//  un REAL accepte un entier de 16 bits au plus, un LREAL tout entier et un
//  REAL ; WORD et DWORD (des mots de bits) acceptent un entier non signe de
//  leur taille au plus ; BOOL n'accepte que BOOL ; STRING que STRING (un
//  STRING[n] compte pour STRING) ; TIME que TIME ; un type IHM ou un DDT
//  n'accepte que lui-meme ; ARRAY[a..b] OF T accepte un tableau de memes
//  bornes dont l'element est exactement T. Un type inconnu de l'appelant
//  (une variable de l'automate sans le projet) : accepte (pas d'erreur sure).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::params {

// ---- les modes ----------------------------------------------------------------
inline constexpr ParamMode kParamModes[] = {ParamMode::Reference, ParamMode::Copy, ParamMode::Both};
[[nodiscard]] std::string_view paramModeLabel(ParamMode) noexcept;     // "R\xC3\xA9" "f\xC3\xA9rence", "Copie", "Les deux"
[[nodiscard]] std::string_view paramModeBadge(ParamMode) noexcept;     // "REF", "COPIE", "LES DEUX"
[[nodiscard]] std::string_view paramModeKey(ParamMode) noexcept;       // enregistre : "reference", "copie", "les_deux"
[[nodiscard]] ParamMode        paramModeFrom(std::string_view) noexcept;   // une cle ou un libelle ; inconnu : Reference
// Une phrase pour l'infobulle du mode.
[[nodiscard]] std::string_view paramModeHelp(ParamMode) noexcept;

// ---- les types ------------------------------------------------------------------
// Les types de base proposes : BOOL, INT, UINT, WORD, DINT, UDINT, DWORD, REAL,
// LREAL, STRING, TIME, ANY.
[[nodiscard]] const std::vector<std::string>& baseTypes();
// Les types de l'AUTOMATE (les DDT) : fournis par qui a le projet automate
// (l'appli, les tests) ; vides : le programme n'est pas connu.
struct PlcTypes {
    // Les noms des DDT du programme.
    std::function<std::vector<std::string>()> names{};
    // Les membres d'un DDT : (nom, type) ; vide : pas un DDT connu.
    std::function<std::vector<std::pair<std::string, std::string>>(std::string_view type)> members{};
    // Le type d'une variable de l'automate (sa racine : "Pompes") ; "" : inconnue.
    std::function<std::string(std::string_view root)> rootType{};
    // (Facultatifs, quand la liste des membres n'est pas connue - la
    // verification de Generer) : le type d'un membre d'un DDT ("" : pas de tel
    // membre) ; ce nom est-il un DDT du programme ?
    std::function<std::string(std::string_view type, std::string_view member)> memberType{};
    std::function<bool(std::string_view type)> isType{};
};
// Les types proposes pour un projet : la base, puis les types IHM du projet
// (structures et tableaux), puis les DDT du programme (quand il est connu).
struct TypeChoice {
    std::string name;
    std::string group;   // "Base", "Types IHM", "DDT de l'API"
};
[[nodiscard]] std::vector<TypeChoice> proposedTypes(const Project&, const PlcTypes& = {});
// Le type est-il connu (base, type IHM, DDT, ARRAY[a..b] OF connu, STRING[n]) ?
// Vide : ANY, connu.
[[nodiscard]] bool typeKnown(const Project&, std::string_view type, const PlcTypes& = {});
// Le type normalise pour comparer : majuscules, sans espaces, STRING[n] -> STRING,
// vide -> ANY.
[[nodiscard]] std::string normalizedType(std::string_view type);
// Le type `declared` (celui du parametre) accepte-t-il une valeur de type `given` ?
// (la regle en tete de fichier). `given` vide : inconnu, accepte.
[[nodiscard]] bool typeAccepts(std::string_view declared, std::string_view given);
// DECISION DU 01/10 (11 h 55) : la compatibilite depend du mode. En Reference et
// en Les deux (on ecrit la variable de l'appelant), une VARIABLE donnee doit avoir
// EXACTEMENT le type declare (ANY accepte tout ; STRING[n] vaut STRING ; un type
// inconnu - automate pas charge, calcul - est accepte) ; en Copie, l'elargissement
// de typeAccepts s'ajoute (INT dans DINT ou REAL...). Une valeur qui n'est pas
// une variable (un litteral, un calcul : `variable` faux) n'est jamais reecrite :
// l'elargissement vaut alors dans les trois modes.
[[nodiscard]] bool typeExact(std::string_view declared, std::string_view given);
[[nodiscard]] bool typeAcceptsFor(ParamMode, std::string_view declared, std::string_view given, bool variable = true);
// Les membres d'un type (un type IHM du projet ou un DDT) : (nom, type) ; vide :
// un scalaire ou un type inconnu.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> typeMembers(const Project&, std::string_view type,
                                                                          const PlcTypes& = {});

// ---- le type d'une expression donnee en argument ----------------------------------
// Le type d'un argument : un litteral (TRUE -> BOOL, 3 -> INT, 3.5 -> REAL,
// 'texte' -> STRING, T#5s -> TIME), une variable IHM (son type, ses membres,
// ses cases), une variable de l'automate (par `plc`), un parametre de la vue
// appelante (son type declare). "" : inconnu (accepte).
[[nodiscard]] std::string expressionType(const Project&, const View* caller, std::string_view expression,
                                         const PlcTypes& = {});

// ---- les arguments d'une action Ouvrir une popup ---------------------------------
struct ArgumentProblem {
    enum class Kind : std::uint8_t { UnknownParam, Missing, TypeMismatch, NotWritable };
    Kind        kind{Kind::UnknownParam};
    bool        error{true};     // false : un avertissement
    std::string param;           // le parametre en cause
    std::string message;         // en francais, tutoie, dit quoi faire
};
// Les arguments `arguments` ("Moteur := Pompes[3]; Nom := 'P3'") donnes a la
// popup `popup` par une action de la vue `caller` : un parametre inconnu de la
// popup (erreur) ; un parametre manquant (sa valeur par defaut : rien ; sans
// valeur par defaut : erreur) ; un type incompatible (erreur) ; un argument
// qui n'est pas une variable pour un parametre Reference ou Les deux que la
// popup ecrit (avertissement : on ne pourra pas l'ecrire).
[[nodiscard]] std::vector<ArgumentProblem> checkArguments(const Project&, const View* caller, const View& popup,
                                                          std::string_view arguments, const PlcTypes& = {});
// La popup ecrit-elle ce parametre (une action qui l'ecrit, un champ de saisie
// relie a lui, une affectation dans un script) ?
[[nodiscard]] bool popupWritesParam(const View& popup, std::string_view param);

// ---- l'aide a la saisie ------------------------------------------------------------
struct Suggestion {
    std::string insert;      // ce qui s'ecrit ("Moteur", "Vitesse")
    std::string type;        // son type ("T_Moteur", "REAL" ; vide : ANY)
    std::string detail;      // "param\xC3\xA8tre \xC2\xB7 LES DEUX \xC2\xB7 le moteur", "membre de T_Moteur"
    int         rank{0};     // 0 : le bon type ; 1 : un type accepte ; 2 : inconnu ; 3 : un autre type
};
// Dans une vue (une popup) : `typed` est ce qui precede le curseur - "Mo" propose
// les parametres qui commencent ainsi (avec leur type et leur mode) ;
// "Moteur." ou "Moteur.Vi" propose les membres du type declare (type IHM ou DDT).
[[nodiscard]] std::vector<Suggestion> paramSuggestions(const Project&, const View&, std::string_view typed,
                                                       const PlcTypes& = {});
// Dans l'argument d'une action Ouvrir une popup pour le parametre `prm` : les
// noms candidats (variables IHM, de l'automate, parametres de l'appelant),
// LES VARIABLES DU BON TYPE EN PREMIER (puis les types acceptes, les inconnus,
// les autres), chacune avec son type.
[[nodiscard]] std::vector<Suggestion> argumentSuggestions(const Project&, const View* caller, const ViewParam& prm,
                                                          const std::vector<std::string>& candidates, const PlcTypes& = {});

// ---- les infos ----------------------------------------------------------------
// Qui ouvre la popup `popup` : chaque action Ouvrir une popup / Changer de popup
// du projet qui la nomme.
struct Opener {
    std::string view;        // la vue qui porte l'action
    std::string object;      // l'objet (vide : la vue elle-meme)
    std::string gesture;     // le declencheur ("Clic"...)
    std::string operation;   // "Ouvrir une popup", "Changer de popup"
    std::string arguments;   // tels que donnes
};
[[nodiscard]] std::vector<Opener> popupOpeners(const Project&, const View& popup);
// Ou un parametre est employe dans sa vue : objets et proprietes, textes,
// actions, scripts.
struct ParamUse {
    std::string object;      // l'objet (vide : la vue)
    std::string where;       // "propri\xC3\xA9t\xC3\xA9 text", "action Clic", "script OnOpen"...
    std::string text;        // l'expression ou la ligne
};
[[nodiscard]] std::vector<ParamUse> paramUses(const View& view, std::string_view param);
// Le nom est-il employe comme racine dans ce texte (Moteur, Moteur.Marche,
// Moteur[2]) ? Sans casse, hors chaines et commentaires.
[[nodiscard]] bool mentionsName(std::string_view text, std::string_view name);
// Renomme `from` en `to` partout ou c'est une racine dans ce texte.
[[nodiscard]] std::string renameRoot(std::string_view text, std::string_view from, std::string_view to);
// "Moteur := M1; Nom := 'P3'" : renomme l'argument `from` en `to` (le reste tel quel).
[[nodiscard]] std::string renameArgument(std::string_view arguments, std::string_view from, std::string_view to);
// RENOMMER UN PARAMETRE de la vue `view` : sa declaration, chaque emploi dans la
// vue (expressions, textes a trous, actions, Appliquer copie, scripts) et le
// nom de l'argument dans chaque action du projet qui ouvre cette vue (Ouvrir
// une popup, Changer de popup, Naviguer). A appeler DANS une commande
// annulable (hmi::changeProject). Rend le nombre d'endroits changes ; `where`
// (non nul) : lesquels ("Base / Btn_Ouvrir").
std::size_t renameParam(Project&, std::string_view view, std::string_view from, std::string_view to,
                        std::vector<std::string>* where = nullptr);
// 1.11.10 (defaut du 05/10 soir : « quand je modifie les parametres d'un popup ou d'un
// symbole, il faut mettre a jour les instances ») : renommer suit aussi dans les
// instances d'un symbole (l'argument nomme, dans toutes les vues). 1.11.22 : renommer
// suit PARTOUT (chaque propriete, le titre, les defauts, et pour un symbole son code :
// fonctions, alarmes, popups, redefinitions). SUPPRIMER le parametre `index` : 1.11.22 -
// les arguments qui lui sont donnes (instances, actions qui ouvrent la vue) RESTENT et
// deviennent des fautes de compilation ; `where` les liste. LE DEPLACER (Monter, Descendre) : les instances ecrites en
// positionnels ("Voiture;50") passent en nommes avant, pour garder leur sens. Dans
// une commande annulable, comme renameParam. Faux : rien a faire.
bool removeParam(Project&, std::string_view view, std::size_t index, std::vector<std::string>* where = nullptr);
bool moveParam(Project&, std::string_view view, std::size_t index, std::size_t to, std::vector<std::string>* where = nullptr);
// "Four := F1; Zone := 2" sans l'argument `name` : "Four := F1" (le reste tel quel).
[[nodiscard]] std::string withoutArgument(std::string_view arguments, std::string_view name);

} // namespace hmi::params
