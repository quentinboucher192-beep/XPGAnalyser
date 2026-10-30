// =============================================================================
//  hmi/HmiScript.hpp — verifier un script : ou est la faute, et en francais
// -----------------------------------------------------------------------------
//  ST : analyse par le simulateur (sim::parse) - ce qui passe ici s'executera.
//  Ses messages sont en anglais ("line 3: expected THEN") : ils sont traduits,
//  et la LIGNE est gardee a part pour que Compiler y mene d'un double-clic.
//
//  C ET C++ : ni compiles ni executes. On relit ce qui fait la moitie des
//  fautes de frappe : accolades, parentheses et crochets, chaines et
//  commentaires non fermes - chacun avec sa ligne.
//
//  LES NOMS : ce qu'un script ST lit ou ecrit (pour "variable inexistante"),
//  les scripts qu'il appelle et les vues qu'il ouvre (pour les references
//  circulaires). Lus dans le texte, sans l'executer.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../sim/Interpreter.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct ScriptDiagnostic {
    enum class Severity : std::uint8_t { Info, Warning, Error } severity{Severity::Error};
    int         line{0};          // 1 = la premiere ; 0 = le script entier
    std::string message;
    // 1.10 : ou souligner (1 = le premier octet de la ligne ; 0 : la ligne).
    int         column{0};
    int         length{0};
    // 1.13.0 (lot F3) : la correction d'un clic que le controle a donnee (scriptcheck::Finding::fixFrom) - la ligne
    // Saisie du panneau Diagnostics la porte comme celle de Validation ; vide : aucune.
    std::string fixFrom{}, fixTo{};
    int         fixColumn{0};
};

// 1.10 : un nom de type IHM (structure) connu du projet ? (vide : aucun, comme en 1.9)
using TypeKnown = std::function<bool(std::string_view)>;
[[nodiscard]] std::vector<ScriptDiagnostic> checkScript(ScriptLang, std::string_view body, std::string_view name = {},
                                                        const TypeKnown& knownType = {});
// 1.11.18 (refonte, lot 3) : un script et ses declarations du modele (constantes, variables) :
// celles-ci controlees d'abord (decl::checkDeclarations, ligne 0), puis le code, avec celles
// qui sont justes reconstruites sur sa ligne 1 (decl::codeOf ; les lignes ne bougent pas).
[[nodiscard]] std::vector<ScriptDiagnostic> checkScript(const Script&, const TypeKnown& knownType = {});
// 1.12.20 (lot C) : un script C ou C++ et le projet - un nom de fonction du projet mal ecrit (Arreter_Pomp :
// "veux-tu dire Arreter_Pompe ?"), un avertissement ; `view` : la vue du script (ses fonctions aussi). Les noms que le
// script declare, ceux du C, du C++ et des natives ne sont pas lus.
[[nodiscard]] std::vector<ScriptDiagnostic> checkCNames(const Project&, const View* view, std::string_view code);
// 1.12.20 (lot C) : L'EN-TETE DU PROJET, en C ou en C++ (lecture seule) : ce qu'un script C ou C++ peut employer - les
// types IHM (enumerations, structures, rappels : typedef bool (*T_Filtre)(float x) ; using T_Filtre =
// std::function<BOOL(REAL)>), les fonctions IHM (et celles de `view`), les variables IHM (extern). En C++, les types du ST
// gardent leur nom (REAL, BOOL...) : l'en-tete les definit ; en C, un type sans equivalent (une LIST) est dit en commentaire.
[[nodiscard]] std::string projectHeader(const Project&, ScriptLang lang, const View* view = nullptr);
// Les messages du simulateur, en francais ("line 3: expected THEN" -> "THEN attendu").
[[nodiscard]] std::string frenchSimMessage(std::string_view english);
// "line 3: ..." : la ligne annoncee (0 sinon), et le message sans elle.
[[nodiscard]] int splitLine(std::string_view message, std::string* rest = nullptr);

struct NameUse {
    std::string name;
    int         line{0};
};
// Les racines des noms d'un script ST (hors mots-cles, litteraux, appels,
// chaines et commentaires), une fois chacune, avec la ligne de leur premiere
// apparition.
[[nodiscard]] std::vector<NameUse> scriptNames(std::string_view body);
// Lot 9 : les chemins pointes d'un script ou d'une expression (SYS.UserName,
// Vue_1.Obj.Visible), a chaque apparition, avec leur ligne et s'ils sont ecrits
// (suivis de :=). Un index coupe le chemin (Armoires[0].x : "Armoires").
struct PathUse {
    std::string path;
    int         line{0};
    bool        assigned{false};
};
[[nodiscard]] std::vector<PathUse> scriptPaths(std::string_view body);
// IHM_APPELER('X') : les scripts appeles. IHM_NAVIGUER('V') / IHM_POPUP('V') :
// les vues ouvertes.
[[nodiscard]] std::vector<std::string> scriptCalls(std::string_view body);
[[nodiscard]] std::vector<std::string> scriptViews(std::string_view body);
// Les fonctions IHM_ connues (pour ne pas les prendre pour des variables).
[[nodiscard]] bool isHmiFunction(std::string_view name) noexcept;
// 1.11.18 (refonte, lot 3) : un mot reserve du ST (IF, END_FOR, AND, VAR, END_VAR...) - pas un nom.
[[nodiscard]] bool isReservedWord(std::string_view name);

// ============================================================== lot 7 ======
//  LES VARIABLES LOCALES. Un script ST peut commencer par des blocs de
//  declaration, comme une section de l'automate :
//
//      VAR                       (* gardees d'une execution a l'autre *)
//          Compteur : INT := 0;
//          Dernier  : STRING;
//      END_VAR
//      VAR_TEMP                  (* remises a leur valeur initiale a chaque fois *)
//          i : INT;
//      END_VAR
//
//  Elles ne sont vues que du script qui les declare, et passent avant les
//  variables IHM et celles de l'automate du meme nom. Une FONCTION IHM declare
//  en plus ses parametres (VAR_INPUT) ; ses VAR ne sont pas gardees (une
//  fonction n'a pas de memoire, comme une FUNCTION de l'automate).
struct LocalVar {
    // 1.11.20 : InOut (VAR_IN_OUT, par reference) et Output (VAR_OUTPUT) sont des parametres
    // comme Input : un appel les donne (une variable), dans l'ordre de leur declaration.
    enum class Section : std::uint8_t { Var, Temp, Input, InOut, Output };
    std::string name, type, initial;
    Section     section{Section::Var};
    int         line{0};
    [[nodiscard]] bool isParameter() const noexcept {
        return section == Section::Input || section == Section::InOut || section == Section::Output;
    }
};
struct ScriptParts {
    std::string                   body;     // le code, les blocs de declaration blanchis (les lignes restent)
    std::vector<LocalVar>         locals;   // dans l'ordre des declarations
    std::vector<ScriptDiagnostic> errors;
    [[nodiscard]] const LocalVar* local(std::string_view name) const noexcept;   // sans casse
    [[nodiscard]] std::vector<const LocalVar*> inputs() const;                   // VAR_INPUT seuls, dans l'ordre
    // 1.11.20 : TOUS les parametres - VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT - dans l'ordre de leur
    // declaration : ceux qu'un appel donne par position (le nombre d'arguments, la signature).
    [[nodiscard]] std::vector<const LocalVar*> parameters() const;
};
// `function` : VAR_INPUT, VAR_IN_OUT et VAR_OUTPUT sont permis (les parametres d'une fonction IHM).
// 1.10 : `knownType` dit si un nom est un type IHM (une locale de ce type est permise).
[[nodiscard]] ScriptParts splitDeclarations(std::string_view code, bool function = false, const TypeKnown& knownType = {});
// Les types elementaires d'une variable locale (1.11.19 : ceux que le registre des types propose
// a une declaration, hmi::typereg::UseDeclaration - BOOL, SINT ... STRING, TIME).
[[nodiscard]] bool localTypeSupported(std::string_view type) noexcept;
// 1.10 (dialecte IHM) : un type riche pour une locale de script : ARRAY[..] (N
// dimensions), MAP[STRING] OF T, REF_TO T, POINTER TO T, MAP_ITERATOR, une
// structure IHM (son nom, si `knownType` le connait). Le reste du texte n'est pas
// verifie ici (le simulateur le lit).
[[nodiscard]] bool richLocalType(std::string_view type, const std::function<bool(std::string_view)>& knownType = {});
// 1.10 : les options du simulateur pour un code de l'IHM (le dialecte IHM).
[[nodiscard]] inline sim::ParseOptions dialectOptions() noexcept { return sim::ParseOptions{true}; }
// 1.11.18 (refonte, lot 5) : la valeur d'une declaration de ce type se lit-elle (une expression,
// une liste [1, 2, 3] pour un tableau, T#5s, 'texte', E_Mode#Auto) ? Faux et `why` (en francais) :
// la grille la refuse avant qu'elle ne rende tout le code illisible.
[[nodiscard]] bool declarationValueReadable(std::string_view type, std::string_view value, std::string* why = nullptr);

//  LES FONCTIONS IHM (Programmation generale > Fonctions). Leur signature :
//  "Moyenne(a : REAL, b : REAL) : REAL" ; sans retour : "Tracer(Message : STRING)".
[[nodiscard]] std::string functionSignature(const HmiFunction&);
// 1.10 : le texte complet d'une fonction IHM pour le dialecte :
// "FUNCTION Nom : Retour <corps>\nEND_FUNCTION" (les lignes du corps restent les memes).
[[nodiscard]] std::string functionText(const HmiFunction&);
// Le corps d'une fonction verifie : ses declarations, son code, son retour.
// 1.10 : `knownType` : un type IHM du projet est un type de retour permis.
[[nodiscard]] std::vector<ScriptDiagnostic> checkFunction(const HmiFunction&, const TypeKnown& knownType = {});
// Les appels d'un code ST : les noms suivis d'une parenthese (Moyenne(...)).
[[nodiscard]] std::vector<NameUse> scriptCallees(std::string_view body);
// 1.12.20 : CE QUE FAIT UNE FONCTION hors de ses locales - les variables IHM qu'elle ecrit, les fonctions IHM_ qui
// agissent (IHM_LOG, IHM_NAVIGUER... ; pas celles qui ne font que lire), elle et celles qu'elle appelle (du projet,
// de `view`). Une expression de propriete ne modifie rien : une LAMBDA qui en appelle une y est refusee.
struct Effects {
    std::vector<std::string> writes, acts;
    [[nodiscard]] bool none() const noexcept { return writes.empty() && acts.empty(); }
};
[[nodiscard]] Effects functionEffects(const Project&, const HmiFunction&, const View* view = nullptr);
// ... une fonction IHM_ qui agit (pas une qui ne fait que lire : IHM_TEMPS, IHM_UTILISATEUR...).
[[nodiscard]] bool hmiFunctionActs(std::string_view name);
// 1.12.20 : ce code donne-t-il une LAMBDA a `name` (name := LAMBDA ...) ? Hors chaines et commentaires.
[[nodiscard]] bool assignsLambda(std::string_view code, std::string_view name);

// RENOMMER UNE FONCTION : ses appels (Nom(...)) dans un code ST ou une
// expression, hors chaines et commentaires. `bare` : aussi le nom seul - le
// corps de la fonction elle-meme, ou "Nom := ..." rend la valeur.
// 1.12.19 (R5) : `values` - aussi le nom pose seul la ou va une valeur (une fonction passee en rappel) : apres := ( ou ,
// et avant ; ) , ou la fin (f := Valider; LIST_FILTER(L, Est_Valide)).
[[nodiscard]] std::string renameCalls(std::string_view code, std::string_view from, std::string_view to, bool bare = false,
                                      bool values = false);
// Un texte a trous ("Moyenne : {Moyenne(a, b):0.0}") ou "=expression" : les
// appels dans les accolades, ou dans toute l'expression.
[[nodiscard]] std::string renameCallsInText(std::string_view text, std::string_view from, std::string_view to);
// 1.11.17 : la meme lecture pour toute reecriture : `f` recoit chaque expression
// du texte a trous (le contenu des accolades) ou toute l'expression ("=...").
[[nodiscard]] std::string rewriteInText(std::string_view text, const std::function<std::string(std::string_view)>& f);

// ---- 1.11.17 (refonte des scripts, lot 0) : CHAQUE TEXTE DU PROJET QUI PEUT CITER
//  UNE FONCTION, UNE VALEUR D'ENUMERATION... - une seule liste, pour que Renommer et
//  "Appelee par" ne s'ecartent plus : les scripts (generaux, de vue, de popup, de
//  symbole), les fonctions (IHM et de symbole), les redefinitions des instances, les
//  operateurs (de symbole, de type), les actions et les proprietes des objets (lues
//  comme rewriteNames les lit : expressions, listes, etats, cellules, arguments,
//  cibles), les titres et parametres des vues, les alarmes (du projet, de symbole,
//  surchargees : condition, message, consigne), les recettes, les historiques et les
//  autorisations par expression.
enum class CodeForm : std::uint8_t {
    Code,         // du ST : des instructions (un script, une fonction, un operateur)
    Expression,   // une expression (une condition, une propriete, un argument)
    Template,     // un texte a trous : rien hors des accolades (ou "=expression")
};
struct CodeSite {
    // Le symbole ou un appel court cherche d'abord sa propre fonction : le code d'un
    // symbole, d'une de ses popups, d'une redefinition (HmiSymbols.hpp,
    // qualifySymbolCalls) ; nullptr : un appel court vise une fonction IHM.
    const View*        scope{nullptr};
    // La vue (ou le symbole) dont un chemin relatif nomme les instances : Vanne_3.Ouvrir()
    // dans son code (qualifyViewCalls, qualifySymbolCalls) ; nullptr : seulement Vue.Instance.
    const View*        view{nullptr};
    const HmiFunction* own{nullptr};    // le corps d'une fonction IHM : son nom y est aussi son retour
    // 1.13.0 : le corps d'une methode - son type (ses membres et ses methodes s'y lisent par leur nom nu).
    const HmiType*     ownerType{nullptr};
    // 1.13.0 : le corps lui-meme de cette methode (pas ses declarations) - ses noms nus qui visent SELF ne sont ni des
    // appels ni des valeurs d'une fonction du projet (methodSelfWords).
    const HmiFunction* method{nullptr};
    std::string        where;           // "script Calcul", "Vue_A/Btn (action)", "fonction Pompe.Ouvrir"...
    std::string        group{};         // "Appelee par" ne cite un groupe qu'une fois ; "" : `where`
    // 1.13.0 : l'espace de noms du code (un script, une fonction, une methode d'un type de Gaz) : un nom court y vise
    // d'abord son espace. Vide : la racine (une vue n'a pas d'espace).
    std::string        ns{};
    // ---- 1.14.0 (lot M1) : OU EST LE TEXTE - pour compter ses references et y aller (la colonne Utilisations,
    //  Voir les usages : hmi::decledit::usages) ; les autres visiteurs les ignorent.
    const View*        host{nullptr};       // la vue qui le porte (aussi le titre d'une popup, une redefinition)
    const Object*      object{nullptr};     // l'objet qui le porte ; nul : la vue, un script, une fonction...
    std::string        key{};               // la propriete de l'objet ("text", "fill")
    int                action{-1};          // le rang de l'action (de l'objet ; sans objet : de la vue) ; -1 : pas une action
    const std::vector<Declaration>* decls{nullptr};   // les declarations du code qui le porte (ses noms y cachent les autres)
    const Declaration* declaration{nullptr};          // la declaration dont c'est la valeur ou le calcul
    Id                 script{kNoId};       // le script (general ou de la vue) qui le porte
    Id                 function{kNoId};     // la fonction (IHM ou de la vue) qui le porte
    // Vrai : un texte deja passe en morceaux au meme endroit (la valeur d'une propriete, d'une action, que rewriteNames
    // lit deja) - un renommage le relit sans dommage, un compte le saute (sinon : compte deux fois).
    bool               again{false};
};
using CodeVisit = std::function<void(std::string& text, CodeForm, const CodeSite&)>;
using ConstCodeVisit = std::function<void(const std::string& text, CodeForm, const CodeSite&)>;
// Un texte vide est aussi passe (au visiteur de l'ignorer). Ecrire dans `text`
// change le projet ; un texte d'objet n'est reecrit que si un morceau a change.
void forEachCode(Project&, const CodeVisit&);
void forEachCode(const Project&, const ConstCodeVisit&);

// Partout dans le projet (forEachCode). Dans un symbole qui a sa propre fonction
// `from`, un appel court la vise, elle : il ne change pas. Rend le nombre de textes changes.
std::size_t renameFunctionEverywhere(Project&, std::string_view from, std::string_view to);
// 1.13.0 : la fonction `from` de l'espace `ns` (vide : la racine), renommee `to` partout, PAR IDENTITE : Util.Moyenne(a, b)
// suit, un appel court de Gaz qui vise Gaz.Moyenne ne change pas (names::renameItemInCode). Sans aucun espace dans le
// projet : comme renameFunctionEverywhere.
std::size_t renameFunctionEverywhere(Project&, std::string_view ns, std::string_view from, std::string_view to);
// Qui appelle cette fonction : "script Demarrage", "fonction Moyenne",
// "Vue_A.OnOpen", "Vue_A/Btn (action)", "Vue_A/Texte_1 (text)", "alarme Haute_P",
// "fonction Pompe.Ouvrir", "Vue_A/Pompe_1.Ouvrir (redefinition)"...
[[nodiscard]] std::vector<std::string> functionCallers(const Project&, std::string_view name);
// 1.11.17 : renommer `from` en `to` detournerait des appels courts - ou ils seraient :
//  - `symbol` nul (une fonction IHM) : un symbole qui appelle `from` et a deja sa
//    propre fonction `to` (l'appel renomme viserait la sienne) ;
//  - une fonction de `symbol` : un appel court de `to` dans le symbole, qui vise
//    aujourd'hui une fonction IHM (il viserait la fonction renommee).
// Vide : le renommage ne detourne rien.
[[nodiscard]] std::vector<std::string> renameCaptures(const Project&, const View* symbol, std::string_view from,
                                                      std::string_view to);
// Le corps d'une nouvelle fonction : un en-tete, un parametre d'exemple et,
// si elle rend une valeur, l'affectation de son nom.
// 1.11.18 (refonte, lot 5) : SANS BLOC VAR - le parametre d'exemple (Entree, du type du
// retour ; Message : STRING pour une procedure) et la locale Resultat sont des
// declarations du modele (functionTemplateDecls), dans les onglets de la fonction.
[[nodiscard]] std::string functionTemplate(std::string_view name, std::string_view returnType,
                                           std::string_view description);
[[nodiscard]] std::vector<Declaration> functionTemplateDecls(std::string_view returnType);

} // namespace hmi
