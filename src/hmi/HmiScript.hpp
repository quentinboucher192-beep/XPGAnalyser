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
};

// 1.10 : un nom de type IHM (structure) connu du projet ? (vide : aucun, comme en 1.9)
using TypeKnown = std::function<bool(std::string_view)>;
[[nodiscard]] std::vector<ScriptDiagnostic> checkScript(ScriptLang, std::string_view body, std::string_view name = {},
                                                        const TypeKnown& knownType = {});
// 1.11.18 (refonte, lot 3) : un script et ses declarations du modele (constantes, variables) :
// celles-ci controlees d'abord (decl::checkDeclarations, ligne 0), puis le code, avec celles
// qui sont justes reconstruites sur sa ligne 1 (decl::codeOf ; les lignes ne bougent pas).
[[nodiscard]] std::vector<ScriptDiagnostic> checkScript(const Script&, const TypeKnown& knownType = {});
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
    enum class Section : std::uint8_t { Var, Temp, Input };
    std::string name, type, initial;
    Section     section{Section::Var};
    int         line{0};
};
struct ScriptParts {
    std::string                   body;     // le code, les blocs de declaration blanchis (les lignes restent)
    std::vector<LocalVar>         locals;   // dans l'ordre des declarations
    std::vector<ScriptDiagnostic> errors;
    [[nodiscard]] const LocalVar* local(std::string_view name) const noexcept;   // sans casse
    [[nodiscard]] std::vector<const LocalVar*> inputs() const;                   // VAR_INPUT, dans l'ordre
};
// `function` : VAR_INPUT est permis (les parametres d'une fonction IHM).
// 1.10 : `knownType` dit si un nom est un type IHM (une locale de ce type est permise).
[[nodiscard]] ScriptParts splitDeclarations(std::string_view code, bool function = false, const TypeKnown& knownType = {});
// Les types d'une variable locale : elementaires (pas de tableau ni de structure).
inline constexpr std::string_view kLocalTypes[] = {"BOOL", "INT", "DINT", "UINT", "UDINT", "SINT", "USINT", "REAL",
                                                   "LREAL", "WORD", "DWORD", "BYTE", "TIME", "STRING"};
[[nodiscard]] bool localTypeSupported(std::string_view type) noexcept;
// 1.10 (dialecte IHM) : un type riche pour une locale de script : ARRAY[..] (N
// dimensions), MAP[STRING] OF T, REF_TO T, POINTER TO T, MAP_ITERATOR, une
// structure IHM (son nom, si `knownType` le connait). Le reste du texte n'est pas
// verifie ici (le simulateur le lit).
[[nodiscard]] bool richLocalType(std::string_view type, const std::function<bool(std::string_view)>& knownType = {});
// 1.10 : les options du simulateur pour un code de l'IHM (le dialecte IHM).
[[nodiscard]] inline sim::ParseOptions dialectOptions() noexcept { return sim::ParseOptions{true}; }

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

// RENOMMER UNE FONCTION : ses appels (Nom(...)) dans un code ST ou une
// expression, hors chaines et commentaires. `bare` : aussi le nom seul - le
// corps de la fonction elle-meme, ou "Nom := ..." rend la valeur.
[[nodiscard]] std::string renameCalls(std::string_view code, std::string_view from, std::string_view to, bool bare = false);
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
    std::string        where;           // "script Calcul", "Vue_A/Btn (action)", "fonction Pompe.Ouvrir"...
    std::string        group{};         // "Appelee par" ne cite un groupe qu'une fois ; "" : `where`
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
