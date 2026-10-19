// =============================================================================
//  sim/DialectLib.hpp - 1.12.7 : LA BIBLIOTHEQUE DU DIALECTE IHM
// -----------------------------------------------------------------------------
//  Ce que le dialecte de l'IHM sait de la bibliotheque standard du C++, sans
//  rien declarer, et que l'interpreteur, Compiler et l'aide a la saisie lisent
//  tous les trois au meme endroit :
//
//    - les templates : NUMERIC_LIMITS<T>.MEMBRE (std::numeric_limits, 15 types,
//      32 membres : 9 fonctions appelees avec (), 23 constantes sans), NUMBERS[<T>].C
//      (std::numbers, REAL ou LREAL, LREAL sans <T> ; 13 constantes), SATURATE_CAST<T>(x)
//      (T obligatoire, un entier), ADD_SAT, SUB_SAT, MUL_SAT, DIV_SAT et MIDPOINT avec
//      un <T> facultatif (sinon le type du premier argument) ;
//    - les enumerations natives des fonctions : COMPARAISON (Egal, Different, Inferieur,
//      InferieurOuEgal, Superieur, SuperieurOuEgal), OPERATION (Somme, Produit, Min, Max,
//      Et, Ou), TRANSFORMATION (Identite, Abs, Carre, Racine, Neg, Inverse) - un litteral
//      NOM#Valeur vaut le rang de la valeur (un DINT) ;
//    - les natives de la 1.12.7 (conteneurs, std::numeric, <bit>) : leur nombre
//      d'arguments, leur signature, ce qu'elles font, si elles ecrivent leur premier
//      argument, si elles changent sa taille (un ARRAY est alors refuse) ;
//    - 1.12.16 : les natives std:: (<cmath>, <string>, <format>, <chrono>, <algorithm>,
//      les statistiques, <bit>, <utility>) - SWAP et EXCHANGE ecrivent leurs arguments
//      (SWAP les deux, EXCHANGE le premier), LIST_TRANSFORM sa destination ;
//    - "veux-tu dire ... ?" (la distance d'edition, comme la maquette).
//
//  Les valeurs de NUMERIC_LIMITS sont celles du C++ (libstdc++, x86) ; elles ne
//  dependent pas de la machine qui fait tourner l'IHM.
// =============================================================================
#pragma once

#include "Value.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sim::lib {

// ------------------------------------------------------------------ les templates ---
struct TemplateInfo {
    std::string_view name;              // NUMERIC_LIMITS
    bool             isClass{false};    // NOM<T>.MEMBRE (une classe) ; sinon NOM<T>(...) (une fonction)
    bool             required{false};   // le <T> est obligatoire
    std::string_view dflt;              // le type sans <T> (NUMBERS : LREAL) ; vide : le premier argument
    std::vector<std::string_view> types;   // les types acceptes
    std::string_view want;              // "un type numerique est attendu (...)"
    std::string_view example;           // "NUMERIC_LIMITS<UINT>.MAX()"
    std::string_view hint;              // "NUMERIC_LIMITS attend son type : NUMERIC_LIMITS<UINT>.MAX()"
    std::string_view library;           // "std::numeric_limits"
    std::string_view summary;           // une phrase
};
[[nodiscard]] const std::vector<TemplateInfo>& templates();
[[nodiscard]] const TemplateInfo* templateInfo(std::string_view name) noexcept;     // sans casse
[[nodiscard]] bool templateAccepts(const TemplateInfo& t, std::string_view type) noexcept;
// Les 15 types de NUMERIC_LIMITS, dans l'ordre de l'aide a la saisie.
[[nodiscard]] const std::vector<std::string_view>& limitsTypes();
// Ce qu'un type est : "entier non signe, 16 bits", et son nom en C ("uint16_t").
[[nodiscard]] std::string_view typeSummary(std::string_view type) noexcept;
[[nodiscard]] std::string_view cTypeName(std::string_view type) noexcept;
// Un nom de type du langage (REAL, STRING, TIME, LIST...), sans casse : NUMERIC_LIMITS<STRING> dit le type
// attendu ; NUMERIC_LIMITS<UNIT>, un nom qui n'en est pas un, « UNIT n'est pas un type » (la maquette).
[[nodiscard]] bool elementaryTypeName(std::string_view name) noexcept;

struct MemberInfo {
    std::string_view name;      // MAX
    std::string_view cpp;       // max
    bool             function{false};
    std::string_view summary;
    std::string_view c;         // NUMBERS : la macro du C (M_PI) ; vide : aucune
    double           value{0};  // NUMBERS : la valeur (en LREAL)
};
[[nodiscard]] const std::vector<MemberInfo>& limitsMembers();     // 32
[[nodiscard]] const std::vector<MemberInfo>& numbersMembers();    // 13
[[nodiscard]] const MemberInfo* templateMember(std::string_view templ, std::string_view member) noexcept;

// La valeur d'un membre, comme le C++ la donne : NUMERIC_LIMITS<UINT>.MAX() = 65535 (un UINT),
// NUMERIC_LIMITS<REAL>.DIGITS = 24 (un DINT), IS_SIGNED (un BOOL). Faux : type ou membre inconnu.
bool limitsValue(std::string_view type, std::string_view member, Value& out);
// Son texte (l'aide a la saisie) : "65535", "1.17549435E-38", "+INF", "TRUE".
[[nodiscard]] std::string limitsText(std::string_view type, std::string_view member);
// Son type : T pour une fonction, BOOL ou DINT pour une constante ("" : inconnu).
[[nodiscard]] std::string limitsType(std::string_view type, std::string_view member);
// NUMBERS<REAL>.PI (arrondi au float), NUMBERS.PI (LREAL).
bool numbersValue(std::string_view type, std::string_view member, Value& out);
[[nodiscard]] std::string numbersText(std::string_view type, std::string_view member);
// Le C++ du membre : "std::numeric_limits<UINT>::max()", "std::numbers::pi_v<REAL>".
[[nodiscard]] std::string cppMember(std::string_view templ, std::string_view type, const MemberInfo& m);
// Le C : "UINT16_MAX", "FLT_EPSILON" ; vide : rien de tel en C.
[[nodiscard]] std::string cMember(std::string_view templ, std::string_view type, const MemberInfo& m);

// ------------------------------------------------- les entiers : bornes et calcul exact ---
// Les bornes d'un type entier (SINT ... ULINT, BYTE ... LWORD) et le type du moteur qui le porte.
struct IntType {
    bool          isSigned{true};
    int           bits{16};
    Type          type{Type::Int};
    std::int64_t  lo{0};            // les bornes (ULINT, LWORD : hi est 2^64 - 1, ecrit en bits dans hiBits)
    std::uint64_t hiBits{0};
};
bool intType(std::string_view name, IntType& out) noexcept;
// Un entier exact sur 65 bits et plus (sans __int128 : le projet se construit aussi sous MSVC).
struct Wide {
    bool          neg{false};
    std::uint64_t mag{0};
    bool          huge{false};      // au-dela de 2^64 : plus grand que toute borne
};
[[nodiscard]] Wide wideOf(const Value& v) noexcept;
[[nodiscard]] Wide wideAdd(Wide a, Wide b) noexcept;
[[nodiscard]] Wide wideSub(Wide a, Wide b) noexcept;
[[nodiscard]] Wide wideMul(Wide a, Wide b) noexcept;
[[nodiscard]] Wide wideDiv(Wide a, Wide b) noexcept;      // vers zero ; b non nul
[[nodiscard]] Wide wideHalf(Wide a) noexcept;             // vers zero
[[nodiscard]] int  wideCompare(Wide a, Wide b) noexcept;
// La valeur bornee au type : la plus proche borne si elle en sort.
[[nodiscard]] Value saturate(Wide v, const IntType& t);
// Elle tient dans le type ?
[[nodiscard]] bool fits(Wide v, const IntType& t) noexcept;

// ------------------------------------------------------ les enumerations natives ---
struct EnumInfo {
    std::string_view name;
    std::vector<std::string_view> values;     // dans l'ordre : leur rang est leur nombre
    std::string_view summary;
};
[[nodiscard]] const std::vector<EnumInfo>& dialectEnums();       // COMPARAISON, OPERATION, TRANSFORMATION
// "COMPARAISON#Superieur" (sans casse, espaces autour admis) : 4. Faux : pas une de ces valeurs.
bool dialectEnumValue(std::string_view literal, std::int64_t& out) noexcept;

// --------------------------------------------------------- les natives de la 1.12.7 ---
struct NativeInfo {
    std::string_view name;
    int              min{1}, max{1};        // le nombre d'arguments (max -1 : sans limite)
    bool             writes{false};         // elle ecrit son premier argument (une variable)
    bool             resizes{false};        // elle change sa taille : un ARRAY est refuse
    bool             arrayOk{false};        // elle lit aussi un ARRAY
    bool             member{false};         // son dernier argument facultatif est un membre ('Poids')
    std::string_view group;                 // "liste", "map", "tuple", "numeric", "bits" ; 1.12.7 : "set", "ring" ;
                                            // 1.12.16 : "math", "texte", "date", "utility"
    std::string_view signature;             // "LIST_SET(L, i, x) : BOOL"
    std::string_view summary;
};
[[nodiscard]] const std::vector<NativeInfo>& natives();
[[nodiscard]] const NativeInfo* native(std::string_view name) noexcept;     // sans casse
// Les 10 natives LIST_ d'avant qui lisent aussi un ARRAY depuis la 1.12.7.
[[nodiscard]] bool oldListAcceptsArray(std::string_view name) noexcept;
// Les natives LIST_ et VECTOR_ d'avant qui changent la taille (un ARRAY : refuse).
[[nodiscard]] bool oldListResizes(std::string_view name) noexcept;

// ---------------------------------------------------------------- veux-tu dire ? ---
// " \xE2\x80\x94 veux-tu dire MAX ?" (le plus proche, a 2 ou 3 lettres pres) ; "" : aucun.
[[nodiscard]] std::string closest(std::string_view name, const std::vector<std::string>& pool);

} // namespace sim::lib
