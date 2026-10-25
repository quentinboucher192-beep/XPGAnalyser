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
#include <string>
#include <string_view>
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
// invalide/en double, OVERRIDE sans virtuelle au parent / de signature differente /
// d'une methode inexistante, privee et virtuelle, type de retour inconnu, un
// parametre illisible - chacun avec son message et, s'il y a lieu, "veux-tu dire ?".
[[nodiscard]] std::string methodProblem(const Project&, const HmiType& owner, const HmiFunction& m);
// Pourquoi le type `t` ne peut pas EXTENDS son parent (vide : il peut) : parent
// inconnu, hors structure, cycle d'heritage.
[[nodiscard]] std::string extendsProblem(const Project&, const HmiType& t);

// ------------------------------------------- renommage et duplication (UI) ---
// Un type renomme : base, interfaces, les SUPER et les appels .Methode() dans les
// corps suivent. Rend le nombre de textes changes.
std::size_t renameTypeInMethods(Project&, std::string_view from, std::string_view to);
// Des identifiants neufs pour une liste de methodes (Dupliquer un type).
void renumberMethods(Project&, std::vector<HmiFunction>&);

} // namespace hmi
