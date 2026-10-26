// =============================================================================
//  hmi/HmiMethods.hpp - les methodes, l'heritage et les interfaces des types (1.13.0, lot J1)
// -----------------------------------------------------------------------------
//  UN TYPE IHM (une structure) PORTE DES METHODES (HmiType::methods, HmiModel.hpp),
//  chacune une HmiFunction - comme une fonction de symbole (HmiSymbols.hpp), mais
//  portee par le TYPE. Une methode voit SELF (l'instance, de type le porteur ;
//  THIS en est l'alias), les membres par leur nom (SELF.X pour lever l'ambiguite),
//  ses parametres, ses locales et les autres methodes du type.
//
//  COMPILATION. Une methode devient une FUNCTION interne, sous un nom stable
//      FUNCTION METHODE_T_Pompe_Demarrer(VAR_IN_OUT SELF : T_Pompe [; parametres]) [: Retour]
//  que l'interpreteur des scripts de l'IHM lit et appelle, exactement comme
//  operatorFunctionText / resolveOperator pour un operateur (HmiOperators.hpp).
//  SELF est passe en VAR_IN_OUT : une methode qui ecrit un membre le modifie pour
//  de bon. Dans le corps, un membre ecrit bare (Debit) est prefixe de SELF.
//  (SELF.Debit) ; THIS devient SELF ; SUPER.M(...) appelle la methode de meme nom
//  du type parent.
//
//  HERITAGE. EXTENDS (HmiType::base) n'existe qu'entre STRUCTURES : le sous-type a
//  tous les membres du parent (en tete, la place Modbus du parent d'abord) et ses
//  methodes ; une methode VIRTUAL du parent se redefinit par une OVERRIDE de meme
//  signature ; SUPER.M() rappelle le corps du parent. Un sous-type passe la ou le
//  parent est attendu (montee) ; l'inverse demande DYNAMIC_CAST.
//
//  INTERFACES. Un quatrieme genre de type IHM (HmiTypeKind::Interface) : des
//  signatures de methodes (HmiFunction a corps vide), ni membre ni donnee. Un type
//  l'implemente (HmiType::interfaces) ; Generer verifie que chaque signature y est.
//  Une interface ne s'emploie que par REF_TO I_xxx (la liste heterogene).
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiOperators.hpp"   // OperatorName (reutilise pour la legende SELF)

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

// ---------------------------------------------------- noms et texte interne ---
// Le nom interne stable de la methode `m` du type `ownerType` : "METHODE_T_POMPE_DEMARRER". Ce nom est
// celui du type qui DEFINIT la methode : un SUPER l'emploie, et un sous-type reutilise la meme fonction.
[[nodiscard]] std::string methodFunctionName(std::string_view ownerType, const HmiFunction& m);
// La FUNCTION complete que l'interpreteur appelle : VAR_IN_OUT SELF en tete (de type `ownerType`, celui
// qui definit la methode), le corps avec ses membres prefixes de SELF., THIS -> SELF, SUPER.M(...) resolu
// vers METHODE_<parent>_M(SELF). Un sous-type passe la ou le parent est attendu (montee du VAR_IN_OUT).
[[nodiscard]] std::string methodFunctionText(const Project&, std::string_view ownerType, const HmiFunction& m);
// La signature lisible : "T_Pompe.Demarrer()", "T_Pompe.EnDefaut() : BOOL".
[[nodiscard]] std::string methodSignature(std::string_view typeName, const HmiFunction& m);
// 1.13.0 : les mots nus de `body` (le corps de `m`, porte par `ownerType`) que la methode lit sur SELF - un membre lu
// comme valeur (Debit), une methode du type appelee (Journal()), son propre nom (le retour) - [debut, fin) dans `body`,
// dans l'ordre. Ce ne sont ni des appels ni des valeurs d'une fonction du projet : renommer une fonction, "Appelee
// par" les sautent (methodFunctionText les ecrit SELF.X).
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> methodSelfWords(const Project&, std::string_view ownerType,
                                                                               const HmiFunction& m, std::string_view body);
// 1.13.0 (le debogueur) : une expression ecrite dans la methode `m` (la condition d'un point d'arret, un espion) lue
// comme le moteur lit son corps (methodFunctionText) : THIS -> SELF, un membre -> SELF.X, son propre nom (le retour) ->
// son nom interne, une autre methode du type -> SELF.M(...), SUPER.M(...) -> la methode du parent.
[[nodiscard]] std::string methodInternalText(const Project&, std::string_view ownerType, const HmiFunction& m, std::string_view text);

// ---------------------------------------------------------------- heritage ---
// La chaine de base, du type (exclu) vers la racine : T_PompeDoublee -> [T_Pompe].
// S'arrete sur un cycle ou un parent inconnu (pas de boucle infinie).
[[nodiscard]] std::vector<const HmiType*> baseChain(const Project&, std::string_view typeName);
// Tous les membres du type, la tranche du parent D'ABORD (puis les siens), dans
// l'ordre de declaration. Equivaut a types::membersOf (qui suit l'heritage).
[[nodiscard]] std::vector<TypeMember> allMembers(const Project&, std::string_view typeName);
// Une methode portee par un type, et le type qui la porte reellement.
struct OwnedMethod {
    const HmiType*     owner{nullptr};
    const HmiFunction* method{nullptr};
};
// Toutes les methodes en vigueur pour `typeName` : celles du parent, puis les
// siennes ; une OVERRIDE (meme nom, meme signature) remplace la virtuelle du parent.
[[nodiscard]] std::vector<OwnedMethod> allMethods(const Project&, std::string_view typeName);
// La methode propre de ce type (sans remonter), par son nom (sans casse) ; nulle : aucune.
[[nodiscard]] const HmiFunction* ownMethod(const HmiType&, std::string_view name);
// La methode EN VIGUEUR de ce nom pour `typeName` : la plus derivee (une OVERRIDE,
// sinon celle du parent) ; `owner` recoit le type qui la porte. Nulle : aucune.
[[nodiscard]] const HmiFunction* effectiveMethod(const Project&, std::string_view typeName, std::string_view name,
                                                 const HmiType** owner = nullptr);

// 1.13.0 (J1b) : la methode d'identifiant `id` (d'un type du projet) et le type qui la porte ; nulle : aucune.
[[nodiscard]] const HmiFunction* methodById(const Project&, Id id, const HmiType** owner = nullptr);

// ------------------------------------------------------------- predicats ----
// `sub` est-il `base`, ou en descend par EXTENDS (transitif, sans casse) ?
[[nodiscard]] bool isA(const Project&, std::string_view sub, std::string_view base);
// `type` implemente-t-il l'interface `iface` (lui ou un de ses parents) ?
[[nodiscard]] bool implementsInterface(const Project&, std::string_view type, std::string_view iface);
// Le nom est-il celui d'une interface du projet ?
[[nodiscard]] bool isInterface(const Project&, std::string_view name);

// ------------------------------------------------------------- interfaces ---
// Les signatures des interfaces de `type` qu'il ne satisfait pas, nommees :
//   "T_Vanne declare I_Demarrable mais n'a pas EnDefaut() : BOOL"
// 1.13.0 : une methode privee ne remplit pas une signature ("Demarrer est privee : elle ne remplit pas l'interface ...").
// Vide : conforme (ou `type` n'implemente rien).
[[nodiscard]] std::vector<std::string> interfaceMisses(const Project&, const HmiType& type);
// Deux methodes ont-elles la MEME signature (retour + parametres : nom, type, mode) ?
[[nodiscard]] bool sameMethodSignature(const HmiFunction& a, const HmiFunction& b);

// -------------------------------------------- l'interface avec l'interpreteur ---
// Comme OperatorCall / resolveOperator : ce que l'interpreteur appelle.
struct MethodCall {
    std::string key;         // porteur|methode|empreinte du corps : change avec le script
    std::string function;    // methodFunctionText (en-tete VAR_IN_OUT SELF compris)
    std::string owner;       // "type T_Pompe, m\xC3\xA9thode Demarrer" (pour les messages)
    std::string selfType;    // le type de SELF (le porteur reel)
    bool        isVirtual{false};
    Id          method{kNoId};   // 1.13.0 : l'id de la methode appelee (celle du type qui la definit : ses points d'arret)
    // 1.13.0 : une methode PRIVEE - le message (vide : publique). Le moteur ne l'appelle que d'une methode de `selfType`
    // (ou d'un type qui en herite).
    std::string privateWhy;
};
// La methode `method` (sans casse) de `selfType` (ou d'un de ses parents) : vrai et
// `out`. Faux : aucune (ou `selfType` n'est pas un type a methodes).
[[nodiscard]] bool resolveMethod(const Project&, std::string_view selfType, std::string_view method, MethodCall& out);
// La methode d'un NOM INTERNE (METHODE_T_POMPE_DEMARRER) : ce que SUPER emet dans un corps. Vrai et
// `out` (la fonction du type qui la definit) ; faux : aucune.
[[nodiscard]] bool resolveMethodInternal(const Project&, std::string_view internalName, MethodCall& out);

// --------------------------------------------------------------- l'edition ---
// Les noms que le corps d'une methode connait, avec leurs types : SELF (et THIS),
// le retour (par le nom de la methode), et ses parametres. `typeName` : le porteur.
[[nodiscard]] std::vector<OperatorName> methodNames(const Project&, std::string_view typeName, const HmiFunction& m);
// La legende, sur une ligne : "SELF : l'instance de T_Pompe (THIS vaut SELF) . ses
// membres se lisent par leur nom ou SELF.X . rends par EnDefaut := ...".
[[nodiscard]] std::string methodLegend(const Project&, std::string_view typeName, const HmiFunction& m);
// Le corps prerempli d'une methode neuve : la legende en commentaire et un squelette.
[[nodiscard]] std::string methodTemplate(const Project&, std::string_view typeName, const HmiFunction& m);
// Le corps prerempli d'une OVERRIDE : SUPER.M(...) rappele (une virtuelle du parent).
[[nodiscard]] std::string overrideTemplate(const Project&, std::string_view typeName, const HmiFunction& parent);

// ------------------------------------------------------- Generer (le modele) ---
// Pourquoi la methode `m` ne va pas sur le type `owner` (vide : elle va) : nom
// invalide, redefinition (meme nom qu'une methode d'un parent) d'une methode non
// virtuelle ou de signature differente, ou privee d'une methode publique (1.13.0), privee et virtuelle, type de retour inconnu,
// un parametre illisible. Une methode n'a pas de drapeau OVERRIDE : un nom mal tape
// fait une methode nouvelle (rien a refuser), pas une redefinition.
[[nodiscard]] std::string methodProblem(const Project&, const HmiType& owner, const HmiFunction& m);
// Pourquoi le type `t` ne peut pas EXTENDS son parent (vide : il peut) : parent
// inconnu, hors structure, cycle d'heritage.
[[nodiscard]] std::string extendsProblem(const Project&, const HmiType& t);

// ------------------------------------------- renommage et duplication (UI) ---
// 1.13.0 : RENOMMER UN TYPE IHM, PARTOUT - son nom, et tout ce qui le nomme : les membres, les variables, les
// declarations, les retours des fonctions, les parametres des popups et symboles, les signatures des methodes,
// le parent (EXTENDS) et les interfaces (IMPLEMENTS) des types qui le visent (par identite : un homonyme d'un autre
// espace ne suit pas), les operateurs, T_ANCIEN#x, la liaison Equipement, DYNAMIC_CAST<T> dans le code. Rend le
// nombre de textes changes (0 : rien, type introuvable ou meme nom). La commande de Types IHM l'emploie.
std::size_t renameTypeEverywhere(Project&, Id type, std::string_view newName);
// Des identifiants neufs pour une liste de methodes (Dupliquer un type).
void renumberMethods(Project&, std::vector<HmiFunction>&);

// ------------------------------------------------ l'ecran des objets (J1b) ---
// Les parametres d'une methode en une ligne : "Entree : REAL; VAR_IN_OUT Cible : T_Pompe; VAR_OUTPUT Etat : INT"
// (dans l'ordre ; vide : aucun). Ce que montre la colonne Parametres, ce que la case attend.
[[nodiscard]] std::string methodParamsText(const HmiFunction&);
// Le contraire : "a : REAL; b : INT" (un ; ou une , entre deux ; VAR_IN_OUT ou VAR_OUTPUT devant un nom : son mode) -
// les parametres de `m` remplaces (ses locales et constantes gardees ; un nom deja la garde son id ; un neuf en recoit
// un de `allocate`). Faux et `why` : illisible (un nom invalide, un type manquant, un nom en double) ; `m` inchange.
bool setMethodParams(HmiFunction& m, std::string_view text, const std::function<Id()>& allocate, std::string* why = nullptr);
// RENOMMER UNE METHODE (la commande Renommer des Methodes) : son nom ; dans son corps, son nom nu (le retour, un appel
// recursif) ; ses redefinitions dans les sous-types (meme nom) et leur corps ; dans les methodes du type et de ses
// sous-types, SELF.X( / THIS.X( / SUPER.X( et l'appel nu X(. Le reste du projet (Pompe1.X() dans un script) n'est pas
// recrit : Compiler signale l'ancien nom. Rend le nombre de textes changes (0 : introuvable ou meme nom).
std::size_t renameMethodEverywhere(Project&, Id method, std::string_view newName);
// Les types qui implementent l'interface `iface` (eux-memes, ou par un parent), par leur nom - la colonne de droite de
// la page d'une interface ("Types qui l'implementent").
[[nodiscard]] std::vector<std::string> implementersOf(const Project&, const HmiType& iface);
// L'apercu d'une interface : "TYPE I_Demarrable INTERFACE\n    METHOD Demarrer\n    METHOD EnDefaut : BOOL\nEND_TYPE".
[[nodiscard]] std::string interfaceDeclaration(const HmiType&);
// Les parents qu'on peut choisir pour `t` (Herite de... : des structures, pas lui-meme ni un de ses descendants - un
// cycle), par leur nom complet ; le premier est "(aucun)".
[[nodiscard]] std::vector<std::string> baseChoices(const Project&, const HmiType& t);

} // namespace hmi
