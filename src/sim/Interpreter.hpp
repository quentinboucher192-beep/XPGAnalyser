// =============================================================================
//  sim/Interpreter.hpp — parsing and executing Structured Text
// -----------------------------------------------------------------------------
//  SCOPE, stated up front because a simulator that quietly does the wrong thing
//  is worse than one that refuses:
//
//    supported   assignment, IF/ELSIF/ELSE, CASE, FOR, WHILE, REPEAT, EXIT,
//                RETURN, arithmetic and comparison, AND/OR/XOR/NOT, MOD,
//                function calls, function-block instance calls with named
//                parameters, array indexing, structure members, direct
//                addresses (%M, %MW, %I, %Q), typed literals (16#FF, T#500ms).
//
//    refused     anything else. An unsupported construct produces a diagnostic
//                naming the line and stops that section, rather than being
//                skipped silently. A simulation you cannot trust is not worth
//                running.
//
//  The parse is done once per section and cached; a scan re-walks the tree.
//  Every loop carries an iteration budget and every scan a statement budget, so
//  a program with a runaway loop reports it instead of hanging the application -
//  which is one of the things the user asked the simulator to catch.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "Rich.hpp"
#include "Value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sim {

struct Diagnostic {
    enum class Severity : std::uint8_t { Info, Warning, Error } severity{Severity::Error};
    std::string message;
    std::uint32_t line{0};
    std::string   section;
};

class LineProbe;   // lot API 8 : plus bas
class Function;           // 1.10 : une FUNCTION du dialecte IHM (plus bas)
struct OperatorSource;    // 1.10 : un operateur du projet (plus bas)

// 1.11.20 : UN ARGUMENT D'UN APPEL, VU SANS L'EVALUER (le choix d'une surcharge, le
// controle) : son nom (a := x, q => y ; vide : par position), son type deduit ("" :
// inconnu), une variable (un nom, un membre, une case, p^) ?, un litteral entier sans
// type ecrit (5, 16#FF : son type se choisit) et sa valeur, son texte (les messages).
struct ArgShape {
    std::string  name;
    bool         output{false};
    std::string  type;
    bool         designator{false};
    bool         literal{false};
    std::int64_t value{0};
    std::string  text;
};

// What the interpreter needs from the world: reading and writing named things.
// Keeping it an interface means the expression evaluator can be tested without
// a project, and the runtime can add forcing without the evaluator knowing.
class Environment {
public:
    virtual ~Environment() = default;

    virtual bool  read(std::string_view name, Value& out) = 0;
    virtual bool  write(std::string_view name, const Value& v) = 0;
    [[nodiscard]] virtual bool exists(std::string_view name) = 0;

    // A call: either a library function returning a value, or a function-block
    // instance whose outputs are then read as members. `instance` is empty for a
    // plain function call.
    virtual bool call(std::string_view name, std::string_view instance,
                      const std::vector<std::pair<std::string, Value>>& arguments,
                      Value& result) = 0;

    // A whole array or structure assigned in one statement:
    //   Tempon_OUT := Tempon_IN;
    // There is no single Value for an aggregate, so the environment copies it
    // member by member. Returning false means "not an aggregate", and the
    // caller falls back to reporting an undeclared name.
    virtual bool assignAggregate(std::string_view target, std::string_view source) {
        (void)target; (void)source;
        return false;
    }

    virtual void report(Diagnostic) = 0;
    // 1.12.1 : le message d'une erreur gardee par TRY ... CATCH Erreur, tel que le lit
    // l'utilisateur (l'IHM : en francais) ; par defaut, tel quel.
    [[nodiscard]] virtual std::string caughtMessage(std::string_view message) { return std::string(message); }

    // Lot API 7 : UNE FONCTION QUE LE SIMULATEUR NE CONNAIT PAS. Vrai : l'appel
    // rend 0 et le cycle continue (l'environnement le note, pour le dire) ;
    // faux : le cycle s'arrete dessus, comme avant. Par defaut : s'arreter.
    virtual bool tolerateUnknownCall(std::string_view name, std::uint32_t line) {
        (void)name; (void)line;
        return false;
    }

    // Lot API 8 : UNE LIGNE MARQUEE D'UN POINT D'ARRET vient d'etre atteinte
    // (setBreakLines) : `tag` est le numero de la section (setProgramTag), `line`
    // la ligne de l'instruction qui va s'executer. `probe` lit les variables de
    // cette ligne - sans effet de bord - si l'environnement les veut (une fois).
    // Par defaut : rien. N'est jamais appele pour une section sans marque.
    virtual void breakpointReached(std::uint32_t tag, std::uint32_t line, LineProbe& probe) {
        (void)tag; (void)line; (void)probe;
    }

    // ---- 1.10 : le dialecte IHM (jamais appele pour le ST de l'automate) ----
    // Le type declare d'un nom ("REAL", "T_FOUR", "ARRAY[0..9] OF REAL") ; vide : inconnu.
    virtual std::string declaredType(std::string_view name) {
        (void)name;
        return {};
    }
    // Les membres d'un type structure (IHM ou DDT), dans l'ordre : (nom, type ecrit).
    virtual bool structMembers(std::string_view typeName, std::vector<std::pair<std::string, std::string>>& out) {
        (void)typeName; (void)out;
        return false;
    }
    // 1.12.2 : UNE VARIABLE DU PROJET A TAILLE VARIABLE (LIST OF T, VECTOR OF T, MAP[K] OF V, ou
    // un type qui en contient) : son objet, garde par l'environnement - le dialecte la lit et
    // l'ecrit en place (L[0], L.Count, LIST_ADD(L, x)). Nul : pas une telle variable.
    virtual ObjRef richVariable(std::string_view name) {
        (void)name;
        return nullptr;
    }
    // Une fonction IHM du projet, lue dans le dialecte (nul : pas une fonction
    // du projet ; l'appel passe alors par call()).
    virtual std::shared_ptr<const Function> dialectFunction(std::string_view name) {
        (void)name;
        return nullptr;
    }
    // ---- 1.11.20 : LES SURCHARGES (plusieurs fonctions du meme nom) ----
    // Combien de fonctions du projet (ou d'un symbole : "Vue.Instance.Ouvrir") portent ce
    // nom : 0 ou 1, rien a choisir (le chemin d'avant).
    virtual int overloads(std::string_view name) {
        (void)name;
        return 0;
    }
    // La surcharge de `name` qui convient a ces arguments : vrai et `key`, le nom a passer
    // ensuite a dialectFunction() et call() ("Convertir#615") ; faux et `why` (aucune ne
    // convient, l'appel est ambigu).
    virtual bool chooseOverload(std::string_view name, const std::vector<ArgShape>& args, std::string& key, std::string& why) {
        (void)name; (void)args; (void)key; (void)why;
        return false;
    }
    // Des fonctions internes du script de meme nom : l'indice de la sienne, ou -1 et `why`.
    // Par defaut : la premiere dont le nombre de parametres convient.
    virtual int chooseInner(const std::vector<const Function*>& candidates, const std::vector<ArgShape>& args, std::string& why);
    // Le type rendu par l'appel d'une fonction du projet ("" : inconnu) : le type d'un
    // argument calcule (Convertir(Moyenne(a, b))).
    virtual std::string resultType(std::string_view name, const std::vector<ArgShape>& args) {
        (void)name; (void)args;
        return {};
    }
    // Une fonction du projet commence / finit (les journaux, la profondeur).
    virtual void enterFunction(std::string_view name) { (void)name; }
    virtual void leaveFunction(std::string_view name) { (void)name; }
    // Un operateur du projet (S2) : `op` "+", "+=", "=", "TO_REAL"... ; les types
    // des operandes (`right` vide pour une conversion).
    virtual bool findOperator(std::string_view op, std::string_view left, std::string_view right, OperatorSource& out) {
        (void)op; (void)left; (void)right; (void)out;
        return false;
    }
    // Le nom canonique d'une variable de l'environnement (un parametre de vue
    // -> son chemin) : une reference le garde, pour viser la meme chose ailleurs.
    virtual std::string canonicalName(std::string_view name) { return std::string(name); }
};

// Lot API 8 : les variables d'une ligne, lues au passage (point d'arret) : le nom
// tel que le code l'ecrit, indices evalues ("Armoires[2].etat"), et sa valeur
// (vide : pas de valeur a lui - une structure, un tableau, un nom inconnu).
class LineProbe {
public:
    virtual ~LineProbe() = default;
    [[nodiscard]] virtual std::vector<std::pair<std::string, std::string>> lineValues() = 0;
};

class Program;   // a parsed section

// Parses one section body. Errors are returned rather than thrown, and a failed
// parse names the line.
[[nodiscard]] core::Result<std::shared_ptr<Program>> parse(std::string_view source,
                                                           std::string sectionName);

// 1.10 : LE DIALECTE IHM. Les scripts de l'IHM (et eux seuls) lisent un ST
// enrichi : fonctions internes (FUNCTION ... END_FUNCTION, RETURN expr),
// parametres par valeur, par reference (VAR_IN_OUT, REF_TO) et par pointeur
// (POINTER TO, ADR, p^), tableaux a N dimensions, MAP, FOR EACH, NULL, +=.
// Sans l'option, c'est le ST de l'automate, inchange (memes jetons, memes
// erreurs) : `parse(source, nom)`.
struct ParseOptions {
    bool hmiDialect{false};
};
[[nodiscard]] core::Result<std::shared_ptr<Program>> parse(std::string_view source, std::string sectionName,
                                                           const ParseOptions& options);
[[nodiscard]] bool isDialect(const Program&) noexcept;

// 1.12.2 : UN OBJET NEUF DU TYPE ECRIT `type` (LIST OF REAL, MAP[STRING] OF INT, TUPLE(INT, STRING),
// ARRAY[1..3] OF REAL...), a la valeur du litteral `initial` ([1, 2.5], ['a' := 1], (1, 'x') ; vide :
// celle du type) - une variable de l'IHM d'un type objet, la valeur d'une constante a verifier. Nul,
// et `why`, si le type ou la valeur ne se lisent pas, ou si la valeur ne va pas dans le type.
[[nodiscard]] ObjRef makeRichValue(std::string_view type, std::string_view initial, Environment& env, std::string* why = nullptr);

// 1.10 : UNE FONCTION DU DIALECTE, lue seule (une fonction IHM du projet, un
// operateur) : "FUNCTION Nom(A : T; VAR_IN_OUT B : U) : R  ...  END_FUNCTION".
[[nodiscard]] core::Result<std::shared_ptr<const Function>> parseFunction(std::string_view text, std::string name);
[[nodiscard]] const std::string& functionName(const Function&) noexcept;
// Sa signature lisible : "Moyenne(t : ARRAY[0..9] OF REAL) : REAL".
[[nodiscard]] std::string functionSignature(const Function&);
// Vrai si elle n'emploie que des valeurs simples (parametres VAR_INPUT et
// retour elementaires, locales elementaires) : elle peut tourner comme avant.
[[nodiscard]] bool functionIsSimple(const Function&) noexcept;

// 1.11.20 : SES PARAMETRES (dans l'ordre : leur nom, leur type ecrit, leur mode, une valeur
// par defaut ?), son type rendu ("" : aucun), sa ligne (FUNCTION).
struct ParamInfo {
    enum class Mode : std::uint8_t { Input, InOut, Output } mode{Mode::Input};
    std::string name, type;
    bool        hasDefault{false};
};
[[nodiscard]] std::vector<ParamInfo> functionParams(const Function&);
[[nodiscard]] std::string            functionResult(const Function&);
[[nodiscard]] std::uint32_t          functionLine(const Function&) noexcept;

// 1.11.20 : LES APPELS D'UNE SECTION, dans l'ordre du texte - pour le controle des
// surcharges, "Appelee par" et renommer une surcharge. `rank` : le rang de cet appel
// parmi les appels du meme nom sur sa ligne (1, 2...) ; `within` : la fonction interne
// qui le contient ("" : le corps du script) ; `args` : ses arguments, vus sans les
// evaluer. `typing` repond aux types des noms (declaredType), aux membres
// (structMembers) et aux fonctions du projet (overloads, chooseOverload, resultType) ;
// rien n'y est lu ni ecrit. Les appels d'un bloc (TON...) et les fonctions standard y
// sont aussi : a l'appelant de trier.
struct CallSite {
    std::string           callee;    // tel qu'ecrit : "Ouvrir", "Vue_A.Vanne_3.Ouvrir"
    std::uint32_t         line{0};
    std::uint32_t         rank{1};
    std::string           within;
    std::vector<ArgShape> args;
};
[[nodiscard]] std::vector<CallSite> callSites(const Program&, Environment& typing);
// Les fonctions internes d'une section du dialecte, dans l'ordre.
[[nodiscard]] std::vector<std::shared_ptr<const Function>> innerFunctions(const Program&);

// 1.10 : un operateur du projet (chantier S2), sous forme d'une FUNCTION du
// dialecte : `key` stable tant que son texte ne change pas, `function` le texte
// complet (en-tete compris), `owner` pour les messages.
struct OperatorSource {
    std::string key, function, owner;
};

// Lot API 8 : OU EN EST L'EXECUTION. Le runner y ecrit le numero de la section
// qu'il execute (setProgramTag) et la ligne de chaque instruction ; en sortant,
// il rend ceux de l'appelant (l'appel d'un DFB revient sur la ligne de l'appel).
// Le runtime les lit a chaque ecriture : c'est « qui a ecrit ».
struct ExecTrace {
    std::uint32_t tag{0};
    std::uint32_t line{0};
};

// Lot API 8 : ce que le runtime demande a une section preparee.
void                                   setProgramTag(Program&, std::uint32_t tag) noexcept;
[[nodiscard]] std::uint32_t            programTag(const Program&) noexcept;
[[nodiscard]] const std::string&       programName(const Program&) noexcept;
// Les lignes ou commence une instruction (triees, sans doublon) : un point
// d'arret sur une autre ligne se decale sur la suivante de cette liste.
[[nodiscard]] std::vector<std::uint32_t> statementLines(const Program&);
// Les lignes marquees d'un point d'arret ; vide : aucune (le runner ne teste
// alors qu'un vecteur vide par instruction - le cout d'une section sans point
// d'arret est nul).
void setBreakLines(Program&, const std::vector<std::uint32_t>& lines);

// Lot API 8 : UNE EXPRESSION SEULE (la condition d'un point d'arret :
// "Armoires[0].etat = 3"), lue une fois et evaluee a chaque passage. L'evaluation
// n'ecrit rien et ne dit rien au journal (report) : une erreur revient en
// valeur, pas en halte du cycle.
class Expression;
[[nodiscard]] core::Result<std::shared_ptr<const Expression>> parseExpression(std::string_view source);
[[nodiscard]] core::Result<Value> evaluate(const Expression&, Environment&);
// 1.11.20 : les appels d'une expression seule (voir callSites d'une section).
[[nodiscard]] std::vector<CallSite> callSites(const Expression&, Environment& typing);

struct RunLimits {
    std::uint32_t maxIterationsPerLoop{1000000};
    std::uint32_t maxStatementsPerScan{5000000};
    // Lot API 7 : LE BUDGET DU CYCLE ENTIER. Chaque appel de bloc lance son
    // propre `execute`, avec son propre compte : une boucle sans fin dans un bloc
    // appele mille fois par cycle figeait l'ecran des minutes. Le runtime passe
    // ici un compteur partage par tout le cycle, son plafond, et une heure
    // limite ; au-dela, le cycle s'arrete et dit ou.
    std::uint64_t* scanStatements{nullptr};
    std::uint64_t  maxScanStatements{0};
    std::int64_t   deadlineMicros{0};     // sur l'horloge de sim::monotonicMicros()
    bool           hasDeadline{false};
    // Lot API 8 : la section et la ligne en cours (voir ExecTrace) ; nul : rien.
    ExecTrace*     trace{nullptr};
    // 1.10 (dialecte IHM) : les variables du script (VAR ... END_VAR au premier
    // niveau), gardees par l'appelant d'une execution a l'autre ; nul : celles
    // de cette execution seulement.
    Locals*        locals{nullptr};
    // 1.10 : la profondeur d'appel des fonctions du dialecte, au plus.
    std::uint32_t  maxCallDepth{64};
};

// Lot API 7 : une horloge monotone, en microsecondes (le budget du cycle).
[[nodiscard]] std::int64_t monotonicMicros() noexcept;

struct RunResult {
    std::uint32_t statements{0};
    bool          completed{true};   // false when a limit or an error stopped it
    // RETURN a ete execute. Dans un DFB, Control Expert rend alors la main a
    // l'appelant : les sections SUIVANTES du bloc ne s'executent pas. Sans ce
    // drapeau, le simulateur enchainait la section suivante comme si de rien
    // n'etait - un moteur desactive (Enable faux, RETURN en tete) continuait
    // de tourner.
    bool          returned{false};
};

[[nodiscard]] RunResult execute(const Program&, Environment&, const RunLimits& = {});

// 1.11.17 : PENDANT UN APPEL DE L'ENVIRONNEMENT (Environment::call), les variables locales
// du code qui appelle - VAR, VAR_TEMP, les parametres d'une fonction du dialecte, la
// variable d'un FOR EACH - de valeur simple. L'IHM en remplit les trous d'un texte :
// IHM_JOURNAL('mini {Mini:0.0}') dans un script qui declare Mini. Faux : pas d'appel en
// cours, pas de locale de ce nom, ou une locale qui n'est pas une valeur simple.
[[nodiscard]] bool readCallerLocal(std::string_view name, Value& out);

// Exposed for the tests and for the editor's own use later.
[[nodiscard]] core::Result<Value> evaluateExpression(std::string_view source, Environment&);

} // namespace sim
