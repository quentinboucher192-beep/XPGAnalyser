// =============================================================================
//  hmi/HmiTypeRegistry.hpp - 1.11.19 (refonte des scripts, lot 6) : LE REGISTRE
//  DES TYPES ET LA REGLE DE CONVERSION
// -----------------------------------------------------------------------------
//  UN SEUL CATALOGUE DES TYPES, UNE SEULE REGLE. Avant lui, des listes ecrites en
//  dur disaient chacune les types de leur usage (kVariableTypes, kLocalTypes, les
//  types de base des parametres de popup, ceux des operandes...) et trois regles
//  disaient chacune ce qui passe ou (params::typeAccepts pour les parametres,
//  matchCost pour les operateurs, simdata::convert pour les valeurs gardees).
//
//  LE REGISTRE se construit a partir d'un projet (et des DDT du programme de
//  l'automate, quand il est connu), puis ne change plus : le build le lit dans un
//  autre fil (immuable, comme la copie du projet). Chaque type y a :
//    - une CLE STABLE, qui ne depend pas de son nom affiche : base:REAL ; ihm:615
//      (un type IHM, structure ou enumeration : son identifiant dans le projet) ;
//      api:T_ANA (un DDT du programme) ; any (ANY) ; void (Aucun) ;
//    - son nom, sa categorie, sa provenance, une phrase de documentation ;
//    - les USAGES ou il est permis : une variable IHM (et un membre d'un type IHM :
//      une place Modbus connue), une declaration d'un code, un parametre de popup
//      ou de symbole, le retour d'une fonction, un operande d'operateur ;
//    - s'il est PROPOSE d'office dans les listes (les autres : le selecteur).
//  Un type construit (ARRAY[0..9] OF REAL, ARRAY[1..3] OF T_Four, MAP[STRING] OF
//  T, REF_TO T) se lit par resolve : sa cle se deduit de celle de son element
//  (array[0..9]:base:REAL), et un nom inconnu y est dit, avec sa raison.
//
//  LA REGLE (conversion) : passer une valeur de type `from` la ou `to` est attendu.
//    - Exact : le meme type (sans casse, sans blancs ; STRING[n] vaut STRING), ou
//      ANY d'un cote, ou un type inconnu de l'appelant (aucune erreur sure).
//    - Widening, sans perte : un entier vers un entier qui contient toutes ses
//      valeurs (INT vers DINT, USINT vers INT, UINT vers UDINT ou WORD...) ; un
//      entier de 16 bits au plus vers REAL ; un entier vers LREAL ; REAL vers LREAL.
//    - Lossy, une perte possible, permise la seulement ou elle l'a toujours ete (un
//      operateur) : un entier vers un entier au moins aussi large qui ne contient
//      pas toutes ses valeurs (UINT vers INT, INT vers UDINT) ; un entier de 32
//      bits ou plus vers REAL (24 bits de mantisse sur l'automate) ; LREAL vers REAL.
//    - Narrowing : un entier vers un plus petit (DINT vers INT) - refuse ; une
//      valeur gardee (la remanence) passe pourtant si elle tient (valueFits).
//    - Forbidden : un reel vers un entier ; un nombre, un texte, un booleen, une
//      duree l'un vers l'autre ; deux structures, deux enumerations, deux tableaux
//      differents. La raison est dite, en francais, avec la conversion a ecrire.
//  Le cout, pour choisir entre plusieurs operateurs : 0 exact, 1 entier elargi,
//  2 entier vers reel ou reel elargi, 4 avec perte ; -1 refuse.
//  Les politiques : un parametre en Copie accepte Exact et Widening ; en Reference,
//  une variable donnee doit etre Exact ; un operateur accepte aussi Lossy.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../sim/Value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::params { struct PlcTypes; }

namespace hmi::typereg {

// ---- les categories (l'ordre est celui du selecteur) -------------------------------
enum class Category : std::uint8_t {
    Elementary,     // BOOL, les entiers, les reels, les mots de bits
    TextTime,       // STRING, TIME
    Collection,     // ARRAY, MAP (construits)
    Structure,      // les structures IHM du projet
    Enumeration,    // les enumerations IHM du projet
    PlcType,        // les DDT du programme de l'automate
    Reference,      // REF_TO, POINTER TO (construits)
    Generic,        // ANY
    Void,           // Aucun (le retour d'une procedure)
};
inline constexpr Category kCategories[] = {Category::Elementary, Category::TextTime,    Category::Collection,
                                           Category::Structure,  Category::Enumeration, Category::PlcType,
                                           Category::Reference,  Category::Generic,     Category::Void};
[[nodiscard]] std::string_view categoryLabel(Category) noexcept;   // "\xC3\x89l\xC3\xA9mentaires", "Structures IHM"...

// ---- les usages (des drapeaux) -----------------------------------------------------
enum Use : unsigned {
    UseVariable    = 1u << 0,   // une variable IHM, un membre d'un type IHM (une place Modbus)
    UseDeclaration = 1u << 1,   // une constante, une variable, une locale, un parametre d'un code
    UseParameter   = 1u << 2,   // un parametre de popup ou de symbole (ANY, les DDT)
    UseReturn      = 1u << 3,   // le retour d'une fonction (Aucun)
    UseOperand     = 1u << 4,   // un operande ou le resultat d'un operateur
    UseAll         = 0x1Fu,
};

// ---- un nombre -----------------------------------------------------------------------
enum class Family : std::uint8_t { None, Bool, Integer, Real, String, Time };
struct Numeric {
    Family      family{Family::None};
    int         bits{0};            // entier : 8, 16, 32, 64 ; reel : 32 (REAL), 64 (LREAL)
    bool        isSigned{false};
    bool        bitString{false};   // BYTE, WORD, DWORD, LWORD
    long double low{0}, high{0};    // entier : sa plage
};

// ---- une entree du registre ------------------------------------------------------------
struct Entry {
    std::string key;                // "base:REAL", "ihm:615", "api:T_ANA", "any", "void"
    std::string name;               // le nom ecrit et affiche : "REAL", "T_Four", "Aucun"
    Category    category{Category::Elementary};
    std::string provenance;         // "IEC 61131-3", "Projet \xC2\xB7 Types IHM", "API \xC2\xB7 DDT du programme"...
    std::string doc;                // une phrase
    Id          id{kNoId};          // un type IHM : son identifiant
    unsigned    uses{0};            // les usages permis (Use)
    unsigned    proposed{0};        // les usages ou il est propose d'office dans une liste
    Numeric     numeric{};          // un type elementaire
    std::vector<std::pair<std::string, std::string>> members{};   // une structure, un DDT : (nom, type)
    std::vector<std::string> values{};                            // une enumeration : ses valeurs
    [[nodiscard]] bool usable(unsigned use) const noexcept { return (uses & use) != 0; }
};

// ---- un texte de type, lu ---------------------------------------------------------------
struct Resolved {
    bool         ok{false};         // connu et permis pour l'usage demande
    std::string  text;              // remis en forme : "ARRAY[0..9] OF REAL", "T_Four"
    std::string  key;               // "base:REAL", "array[0..9]:base:REAL", "ref:ihm:615"
    Category     category{Category::Elementary};
    const Entry* entry{nullptr};    // le type nomme (l'element d'un tableau, la cible d'une reference)
    bool         missing{false};    // un nom qui n'est (plus) dans le projet : un type supprime, mal ecrit
    std::string  unknown;           // missing : ce nom-la (l'element d'un tableau, la cible d'une reference...)
    std::string  why;               // pas ok : pourquoi, en francais
};

class Registry {
public:
    Registry();                     // les types de base seuls (sans projet)
    // Le registre d'un projet : la base, puis ses types IHM (structures et enumerations,
    // dans l'ordre du projet), puis les DDT du programme (`plc` nul ou vide : inconnus).
    [[nodiscard]] static std::shared_ptr<const Registry> build(const Project&, const params::PlcTypes* plc = nullptr);

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const Entry* byKey(std::string_view key) const noexcept;
    [[nodiscard]] const Entry* byName(std::string_view name) const noexcept;   // sans casse ; STRING[n] : STRING
    // Les entrees permises pour cet usage, dans l'ordre du registre ; `proposedOnly` : celles
    // qu'une liste propose d'office.
    [[nodiscard]] std::vector<const Entry*> usable(unsigned use, bool proposedOnly = false) const;
    [[nodiscard]] std::vector<std::string>  names(unsigned use, bool proposedOnly = true) const;
    // Un texte de type : un nom, ou un type construit (ARRAY, MAP, REF_TO, POINTER TO,
    // MAP_ITERATOR, STRING[n]). `use` : l'usage demande (un type connu mais pas permis
    // ici n'est pas ok, et `why` le dit).
    [[nodiscard]] Resolved resolve(std::string_view text, unsigned use = UseAll) const;
    // Le nom d'aujourd'hui d'une cle (un type IHM renomme) ; vide : la cle n'est plus la.
    [[nodiscard]] std::string nameOfKey(std::string_view key) const;

private:
    std::vector<Entry> entries_;
};

// Le registre des types de base (sans projet), partage.
[[nodiscard]] const Registry& baseRegistry();
// Un type de base : sa famille numerique ; rien (Family::None) : un autre type.
[[nodiscard]] Numeric numericOf(std::string_view type) noexcept;
// Le texte d'un type pour comparer : majuscules, sans blancs, STRING[n] -> STRING, vide -> ANY.
[[nodiscard]] std::string comparable(std::string_view type);
// Un nombre (entier ou reel) ? un entier ? (les 14 types de la norme : SINT ... LWORD, REAL, LREAL)
[[nodiscard]] bool isNumber(std::string_view type) noexcept;
[[nodiscard]] bool isInteger(std::string_view type) noexcept;
// Le type du moteur (sim) d'un type de base, sans casse : le moteur n'a ni LREAL, ni SINT, ni LINT...
// (LREAL -> Real, SINT -> Int, LINT -> DInt, USINT -> UInt, ULINT -> UDInt, LWORD -> DWord,
// EBOOL -> Bool, STRING[n] -> String) ; un autre type : Unknown. Remplace les rustines
// "LREAL devient REAL" ecrites a la main.
[[nodiscard]] sim::Type simTypeOf(std::string_view type) noexcept;

// ---- la regle ---------------------------------------------------------------------------
enum class Conversion : std::uint8_t { Exact, Widening, Lossy, Narrowing, Forbidden };
struct Verdict {
    Conversion  kind{Conversion::Forbidden};
    int         cost{-1};           // 0, 1, 2, 4 ; -1 : refuse
    std::string why;                // une perte ou un refus : la raison, en francais
    [[nodiscard]] bool safe() const noexcept { return kind == Conversion::Exact || kind == Conversion::Widening; }
    [[nodiscard]] bool exact() const noexcept { return kind == Conversion::Exact; }
    [[nodiscard]] bool lenient() const noexcept { return safe() || kind == Conversion::Lossy; }
};
// `from` vide : inconnu (Exact, aucune erreur sure).
[[nodiscard]] Verdict conversion(std::string_view from, std::string_view to);
[[nodiscard]] std::string_view conversionLabel(Conversion) noexcept;   // "exact", "\xC3\xA9largi", "avec perte"...
// Une VALEUR entiere gardee (la remanence) tient-elle dans le type `to` ? (un entier
// vers un entier plus petit passe s'il tient : rien n'est tronque en cachette).
[[nodiscard]] bool valueFits(long long value, std::string_view to) noexcept;

// ---- la cle d'un type, au chargement ---------------------------------------------------
// Chaque declaration qui porte une cle de type lue du fichier (Declaration::typeKey, type_cle) :
// si son type ne se lit plus (un type IHM renomme hors de l'application) et que la cle nomme
// un type IHM d'aujourd'hui, son texte prend ce nom. Toutes les cles sont ensuite videes (la
// memoire n'en garde pas : l'enregistrement les recalcule). Rend une phrase par type suivi.
std::vector<std::string> followTypeKeys(Project&);

} // namespace hmi::typereg
