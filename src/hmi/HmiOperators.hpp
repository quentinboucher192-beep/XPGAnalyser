// =============================================================================
//  hmi/HmiOperators.hpp - les operateurs des symboles et des types IHM (1.10)
// -----------------------------------------------------------------------------
//  UN SYMBOLE ET UN TYPE IHM PORTENT DES OPERATEURS (HmiOperator, HmiModel.hpp),
//  chacun defini par un script ST du dialecte IHM :
//    - les CONVERSIONS : TO_REAL(v), TO_STRING(Pompe_1), TO_T_RESUME(v) ;
//    - + - * / (un resultat), += -= *= /= (modifient la gauche), = <> < > <= >=
//      (un BOOL), entre le porteur et lui-meme ou un type externe.
//
//  LE SCRIPT EST LE CORPS D'UNE FONCTION (comme la maquette 1.10, scene 13). Ses
//  operandes s'appellent a (la gauche, ou le seul d'une conversion) et b (la
//  droite) ; il rend son resultat par Resultat := ... (Resultat.x := ... pour une
//  structure), par le nom de la conversion (TO_REAL := ...) ou par RETURN ... ;
//  += -= *= /= ne rendent rien : a est passe par reference (VAR_IN_OUT) et le
//  script le modifie. Le nom de fonction d'un operateur (la norme CEI 61131-3 :
//  a + b est la forme operateur de ADD(a, b)) :
//      +  ADD    -  SUB    *  MUL    /  DIV
//      =  EQ     <> NE     <  LT     >  GT     <= LE     >= GE
//      += ADD_ASSIGN   -= SUB_ASSIGN   *= MUL_ASSIGN   /= DIV_ASSIGN
//  operatorFunctionText() en fait une FUNCTION complete, que l'interpreteur des
//  scripts de l'IHM lit et appelle (resolveOperator : l'interface avec lui).
//
//  L'ORDRE COMPTE : T_VECTEUR * REAL et REAL * T_VECTEUR sont deux operateurs ;
//  l'un des deux operandes (ou la source, ou la cible d'une conversion) est le
//  porteur. Le type d'une instance de symbole est le nom de son symbole ; ses
//  parametres se lisent comme des membres (A.Debit).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ------------------------------------------------------------ les genres ---
inline constexpr std::string_view kConversionOp = "TO";
inline constexpr std::string_view kResultName = "Resultat";   // le resultat, dans le script
// Les operateurs, dans l'ordre des listes.
inline constexpr std::string_view kOperatorSymbols[] = {"+", "-", "*", "/", "+=", "-=", "*=", "/=",
                                                         "=", "<>", "<", ">", "<=", ">="};
enum class OperatorFamily : std::uint8_t { Conversion, Arithmetic, Assignment, Comparison, Unknown };
[[nodiscard]] OperatorFamily operatorFamily(std::string_view op) noexcept;
// "+" -> "ADD", "+=" -> "ADD_ASSIGN", "=" -> "EQ" ; vide : inconnu (ou "TO").
[[nodiscard]] std::string_view operatorIecName(std::string_view op) noexcept;
// "addition", "ajouter a", "egalite"... (pour les listes)
[[nodiscard]] std::string_view operatorLabel(std::string_view op) noexcept;
// Le nom de la fonction de l'operateur : "TO_STRING", "ADD", "ADD_ASSIGN", "EQ".
[[nodiscard]] std::string operatorFunctionName(const HmiOperator&);
// "TO_STRING(Sym_Pompe) : STRING", "T_VECTEUR + T_VECTEUR : T_VECTEUR", "T_VECTEUR += REAL".
[[nodiscard]] std::string operatorSignature(const HmiOperator&);
// Le genre en mots : "conversion", "arithmetique", "compose", "comparaison" (accentues).
[[nodiscard]] std::string_view operatorKindLabel(std::string_view op) noexcept;
// Les equivalents (l'editeur les montre au-dessus du script, l'aide F1 aussi), pour
// le porteur `owner` : C++ (T_VECTEUR operator+(const T_VECTEUR& a, REAL b),
// explicit T_VECTEUR::operator REAL() const, un constructeur, bool operator==...) et
// C (T_VECTEUR t_vecteur_ajouter(T_VECTEUR a, REAL b), void t_vecteur_ajouter_a(...)).
[[nodiscard]] std::string operatorCppSignature(const HmiOperator&, std::string_view owner);
[[nodiscard]] std::string operatorCSignature(const HmiOperator&, std::string_view owner);
// Deux noms de type egaux (sans casse, elementaires en majuscules).
[[nodiscard]] bool sameTypeName(std::string_view a, std::string_view b) noexcept;

// ----------------------------------------------------------- les porteurs ---
struct OperatorOwner {
    enum class Kind : std::uint8_t { None, Symbol, Type } kind{Kind::None};
    Id          id{kNoId};         // la vue du symbole, ou le type IHM
    std::string name;              // "Sym_Pompe", "T_VECTEUR"
    [[nodiscard]] bool valid() const noexcept { return kind != Kind::None; }
    // "type T_VECTEUR", "symbole Sym_Pompe"
    [[nodiscard]] std::string label() const;
};
// Le porteur de ce nom (un type IHM passe avant un symbole du meme nom ; sans casse).
[[nodiscard]] OperatorOwner ownerByName(const Project&, std::string_view name);
[[nodiscard]] OperatorOwner ownerOfView(const View&);
[[nodiscard]] OperatorOwner ownerOfType(const HmiType&);
// Les operateurs d'un porteur (nul : introuvable).
[[nodiscard]] const std::vector<HmiOperator>* operatorsOf(const Project&, std::string_view ownerName);
[[nodiscard]] const std::vector<HmiOperator>* operatorsOf(const Project&, const OperatorOwner&);
[[nodiscard]] std::vector<HmiOperator>*       operatorsOf(Project&, const OperatorOwner&);
// L'operateur `id` (dans tout le projet) et son porteur.
[[nodiscard]] const HmiOperator* operatorById(const Project&, Id id, OperatorOwner* owner = nullptr);
[[nodiscard]] HmiOperator*       operatorById(Project&, Id id, OperatorOwner* owner = nullptr);
// Tous les operateurs du projet, avec leur porteur (types puis symboles, dans l'ordre du projet).
struct OwnedOperator {
    OperatorOwner      owner;
    const HmiOperator* op{nullptr};
};
[[nodiscard]] std::vector<OwnedOperator> allOperators(const Project&);
// 1.10 (decision 15) : "toString" / "fromString" pour les deux conversions d'une
// enumeration (HmiEnums.hpp) ; vide sinon. Ce que l'interface ecrit a cote du genre.
[[nodiscard]] std::string_view operatorRole(const Project&, const OperatorOwner&, const HmiOperator&);

// ------------------------------------------------------------- l'emploi ----
// L'operateur `op` ("+", "+=", "<"...) entre `left` et `right` : ceux du type de
// gauche puis ceux du type de droite ; le meilleur : exact, puis avec promotion
// des nombres (un entier va a un entier plus large ou a REAL / LREAL, REAL a LREAL).
[[nodiscard]] const HmiOperator* findOperator(const Project&, std::string_view op, std::string_view left,
                                              std::string_view right, OperatorOwner* owner = nullptr);
// La conversion `function` ("TO_REAL") de `source` : definie sur la source (vers
// REAL) ou sur la cible (un TO_T_VECTEUR depuis REAL, defini sur T_VECTEUR).
[[nodiscard]] const HmiOperator* findConversion(const Project&, std::string_view function, std::string_view source,
                                                OperatorOwner* owner = nullptr);

// Pour l'interpreteur des scripts de l'IHM : la FUNCTION complete de l'operateur,
// sous un nom interne (Resultat et TO_REAL, non suivis d'une parenthese, le
// deviennent : le resultat de la fonction ; le corps commence a la ligne 2).
//   FUNCTION OPERATEUR_ADD(a : T_VECTEUR; b : REAL) : T_VECTEUR
//   FUNCTION OPERATEUR_ADD_ASSIGN(VAR_IN_OUT a : T_VECTEUR; b : REAL)
//   <le corps>
//   END_FUNCTION
[[nodiscard]] std::string operatorFunctionText(const HmiOperator&);
struct OperatorCall {
    std::string key;               // porteur|op|gauche|droite|empreinte du corps : change avec le script
    std::string function;          // operatorFunctionText
    std::string owner;             // "type T_VECTEUR, op\xC3\xA9rateur +" (pour les messages)
};
// `op` : "+", "+=", "=", ... ou "TO_REAL" (alors `right` est vide). Faux : aucun.
bool resolveOperator(const Project&, std::string_view op, std::string_view left, std::string_view right,
                     OperatorCall& out);
// `name` est-il une conversion : TO_<type de base> (les conversions standard du
// dialecte : TO_STRING, TO_REAL...) ou le nom d'une conversion du projet (TO_T_RESUME) ?
[[nodiscard]] bool isConversion(const Project&, std::string_view name);
// Le type d'un nom pour les operateurs : une variable IHM ("v" -> "T_VECTEUR",
// "Fours[2]" -> "T_Four"), une instance de symbole de la vue `view` ("Pompe_1",
// ou "Vue.Pompe_1" dans tout le projet) -> le nom de son symbole ; vide : inconnu.
[[nodiscard]] std::string operandTypeOf(const Project&, const View* view, std::string_view path);

// ------------------------------------------ 1.10.1 (U2) : a, b, Resultat ---
// Les noms que le script d'un operateur connait, avec leurs types : a (la gauche,
// ou l'objet converti), b (la droite), Resultat (le type rendu) et, pour une
// conversion, son nom (TO_REAL := ...). L'aide a la saisie les propose (a. : les
// membres), la legende les ecrit au-dessus du script, Compiler controle leurs
// membres. `owner` : le porteur (il dit lequel est "l'objet").
struct OperatorName {
    std::string name;              // "a", "b", "Resultat", "TO_REAL"
    std::string type;              // "T_VECTEUR", "REAL"
    std::string role;              // "l'objet de type T_VECTEUR (a gauche de +)"
};
[[nodiscard]] std::vector<OperatorName> operatorNames(const HmiOperator&, std::string_view owner = {});
// La legende, sur une ligne : "a : l'objet de type T_VECTEUR (a gauche de +) . b :
// l'autre operande, REAL . Resultat : T_VECTEUR" ; une conversion : "a : l'objet
// converti, T_VECTEUR . le resultat (REAL) : TO_REAL := ... ou Resultat := ..." ;
// += -= *= /= : a est modifie en place, pas de resultat.
[[nodiscard]] std::string operatorLegend(const HmiOperator&, std::string_view owner = {});
// Un exemple d'une ligne, tire des membres : "Resultat.x := a.x + b.x;",
// "TO_REAL := a.x;", "a.x := a.x + b;", "Resultat := a.x = b.x;".
[[nodiscard]] std::string operatorExample(const Project&, const HmiOperator&);
// Les membres d'un operande de ce type : un type IHM (ses membres), un symbole
// (ses parametres, puis ses proprietes : Visible, X...). Vide : un type de base,
// un DDT de l'automate, un type inconnu.
struct OperandMember {
    std::string name, type, help;
    bool        parameter{false};  // un parametre du symbole (avant ses proprietes)
};
[[nodiscard]] std::vector<OperandMember> operandMembers(const Project&, std::string_view type);

// ------------------------------------------------------------- l'edition ---
// Le script prerempli : la signature en commentaire, ce que sont A et B, et un
// corps d'exemple tire des membres du type (ou des parametres du symbole).
[[nodiscard]] std::string operatorTemplate(const Project&, const HmiOperator&);
// Un nouvel operateur (un identifiant neuf, son script prerempli). Une conversion :
// `op` = "TO", `right` vide, `result` = la cible. += -= *= /= : `result` vide.
[[nodiscard]] HmiOperator makeOperator(Project&, std::string_view op, std::string_view left, std::string_view right,
                                       std::string_view result);
// Le meme sans identifiant (kNoId) : pour essayer une signature avant de l'ajouter.
[[nodiscard]] HmiOperator draftOperator(const Project&, std::string_view op, std::string_view left, std::string_view right,
                                        std::string_view result);
// Pourquoi cet operateur ne va pas sur ce porteur (vide : il va) : genre inconnu,
// type inconnu, aucun operande n'est le porteur, retour du mauvais type, en double
// (sur ce porteur ou un autre ; `ignore` : l'operateur qu'on modifie).
// `plcType` : les DDT de l'automate (un type que l'IHM ne connait pas).
[[nodiscard]] std::string operatorProblem(const Project&, const OperatorOwner&, const HmiOperator&, Id ignore = kNoId,
                                          const std::function<bool(std::string_view)>& plcType = {});
// Des identifiants neufs.
void renumberOperators(Project&, std::vector<HmiOperator>&);
// La copie d'un symbole ou d'un type (Dupliquer) : des identifiants neufs, et le
// nom du porteur copie remplace par celui de la copie (operandes, resultats,
// cibles ; TO_<ancien> dans les scripts).
void copyOperators(Project&, std::vector<HmiOperator>&, std::string_view fromOwner, std::string_view toOwner);
// Un type IHM ou un symbole renomme : les operandes, les resultats et les cibles
// des operateurs suivent, et TO_<ancien> devient TO_<nouveau> dans leurs scripts.
// Rend le nombre d'operateurs changes.
std::size_t renameTypeInOperators(Project&, std::string_view from, std::string_view to);
// Un operateur dont la signature change (genre, operandes, cible) : `after`, dont
// le script suit - la signature en tete (si c'est celle d'avant), le nom de
// fonction affecte (TO_REAL -> TO_LREAL, ADD -> SUB).
[[nodiscard]] HmiOperator retitled(const HmiOperator& before, HmiOperator after);

// --------------------------------------------------------------- Compiler ---
struct OperatorIssue {
    OperatorOwner owner;
    Id            id{kNoId};       // l'operateur
    std::string   signature;
    int           line{0};         // dans son script (1 = la premiere) ; 0 : l'operateur lui-meme
    int           column{0};       // 1 = le premier octet ; 0 : inconnue (la ligne entiere)
    int           length{0};       // en octets, sur la ligne ; 0 : inconnue
    std::string   message;
    bool          error{true};     // faux : un avertissement
};
// Chaque operateur du projet : ce qui ne va pas (operatorProblem), et son script
// (la syntaxe ; un operateur qui rend une valeur et ne la donne jamais ; 1.10.1 :
// un membre qui n'existe pas dans le type de a, b ou Resultat, "veux-tu dire ?").
[[nodiscard]] std::vector<OperatorIssue> operatorIssues(const Project&,
                                                        const std::function<bool(std::string_view)>& plcType = {});

// ---------------------------------------------------- l'aide a la saisie ----
struct OperatorSuggestion {
    std::string insert;            // "TO_STRING(" ; un operateur : "+ "
    std::string label;             // "TO_STRING(T_VECTEUR) : STRING", "T_VECTEUR + REAL : T_VECTEUR"
    std::string help;              // sa description, ou ce qu'il fait
};
// Apres un nom de ce type : ses conversions et ses operateurs (ceux du type et
// ceux d'autres porteurs qui le prennent en operande).
[[nodiscard]] std::vector<OperatorSuggestion> operatorSuggestions(const Project&, std::string_view typeName);
// Toutes les conversions TO_xxx du projet (pour completer "TO_" sans type connu).
[[nodiscard]] std::vector<OperatorSuggestion> allConversions(const Project&);

} // namespace hmi
