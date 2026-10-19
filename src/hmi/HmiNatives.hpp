// =============================================================================
//  hmi/HmiNatives.hpp - 1.12.0 : LES NATIVES DE L'IHM
// -----------------------------------------------------------------------------
//  Tout ce que le langage de l'IHM connait sans rien declarer : les fonctions
//  (standard, du dialecte, IHM_, couleurs, aleatoire), les conversions X_TO_Y,
//  les types, les operateurs, les instructions, les enumerations natives. La
//  branche Natives (IHM > Programmation generale) les montre, verrouillees ;
//  l'essai natives execute chaque exemple dans le moteur.
//
//  LE CATALOGUE (HmiNativesData.cpp) est ecrit par outils/natives/generer.py
//  depuis outils/natives/catalogue.py. Chaque fonction y a sa syntaxe en ST, en C
//  et en C++ : en C et en C++, le meme nom (les fonctions IHM_ : l'API de la
//  cible), une autre ecriture (std::clamp pour LIMIT), ou rien. Seul le ST
//  s'execute en simulation ; les scripts C et C++ sont edites et verifies.
//
//  LES FONCTIONS PROPRES A L'IHM (call) : les couleurs (RGB, RGBA, HSL,
//  COULEUR_...) et l'aleatoire (RANDOM, RANDOM_INT, RANDOM_REAL, RANDOM_SEED).
//  Une couleur est un texte '#RRGGBB' ou '#RRGGBBAA' (l'opacite en dernier),
//  comme dans les proprietes des objets. L'aleatoire est un Mersenne Twister
//  (std::mt19937), le meme d'une machine a l'autre : RANDOM_SEED(n) rejoue.
//
//  LES ENUMERATIONS NATIVES : NIVEAU_LOG, TRANSITION, POSITION_POPUP...
//  NOM#Valeur vaut son nombre (un DINT), comme une enumeration du projet ; la
//  fonction IHM_ qui l'attend recoit le mot qu'elle lisait deja (enumArgument).
// =============================================================================
#pragma once

#include "../sim/Value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::natives {

// ------------------------------------------------------------------ le catalogue ---
enum class Avail : std::uint8_t { Same, Equivalent, None };   // en C, en C++

struct Param {
    std::string_view name, type, role;
    bool             optional{false};
};
// Un exemple execute par l'essai : le type du resultat, l'expression, ce qu'elle
// vaut, et ce qui la precede dans un script (des declarations) - vide : aucun.
struct Example {
    std::string_view type, expression, expected, prelude;
    [[nodiscard]] bool empty() const noexcept { return expression.empty(); }
};
struct Function {
    std::string_view   name, category;
    std::vector<Param> params;
    std::string_view   returns, summary;
    std::string_view   st, c, cpp;              // la syntaxe, un exemple dans chaque langage
    Avail              inC{Avail::Equivalent}, inCpp{Avail::Equivalent};
    bool               expression{true};        // dans une expression fx, un texte a trous, une condition
    bool               readOnlyIhm{false};      // une fonction IHM_ qui ne fait que lire
    Example            example{};
    std::vector<std::string_view> notes;
};
struct Category { std::string_view id, title, summary, group; };
struct Operator {
    std::string_view symbol, name, summary, st, c, cpp;
    Example          example{};
};
struct Instruction { std::string_view keyword, name, form, st, c, cpp; };
struct EnumValue {
    std::string_view name;
    std::int64_t     number{0};
    std::string_view text, argument;            // le texte montre ; le mot que recoit la fonction
};
// 1.12.1 : une propriete d'objet qui prend une enumeration native ("Selector", "style") ;
// kind "*" : tout objet qui a cette propriete (Text, Button... : "align").
struct PropertyUse { std::string_view kind, key; };
struct NativeEnum {
    std::string_view         name, summary, function;   // function : la fonction IHM_ qui l'attend ("" : aucune)
    int                      argument{-1};               // son rang
    std::vector<EnumValue>   values;
    std::vector<PropertyUse> uses;                       // 1.12.1 : les proprietes des objets qui la prennent
};
struct TypeExtra {
    std::string_view name, cType, cppType, defaultValue, modbus;
    std::vector<std::string_view> literals, notes;
};
struct Constructed { std::string_view name, example, summary, st, c, cpp; };
// 1.12.1 : un generique (ANY_NUM...) : ses types, ce qu'il dit dans une signature.
struct Generic { std::string_view name, members, summary; };

[[nodiscard]] const std::vector<Category>&    categories();
[[nodiscard]] const std::vector<Function>&    functions();      // sans les X_TO_Y (conversions())
[[nodiscard]] const std::vector<Operator>&    operators();
[[nodiscard]] const std::vector<Instruction>& instructions();
[[nodiscard]] const std::vector<NativeEnum>&  enums();
[[nodiscard]] const std::vector<TypeExtra>&   typeExtras();
[[nodiscard]] const std::vector<Constructed>& constructed();
[[nodiscard]] const std::vector<Generic>&     generics();       // 1.12.1

// ------------------------------------------------------------- autour du catalogue ---
[[nodiscard]] const Function*  function(std::string_view name) noexcept;     // sans casse
[[nodiscard]] const Category*  category(std::string_view id) noexcept;
[[nodiscard]] const TypeExtra* typeExtra(std::string_view name) noexcept;
// La signature ST : "LIMIT(MN : ANY; IN : ANY; MX : ANY) : comme IN" ; courte : "(MN, IN, MX)".
[[nodiscard]] std::string signature(const Function&);
[[nodiscard]] std::string shortSignature(const Function&);
// Le nombre d'arguments (max -1 : sans limite) d'une native - fonction du catalogue
// ou X_TO_Y. Faux : pas une native.
bool arity(std::string_view name, int& min, int& max) noexcept;

// Les conversions X_TO_Y : les 17 types de depart et d'arrivee, 272 fonctions.
struct Conversion {
    std::string name, from, to;
    std::string behaviour;                       // ce qu'elle fait, une phrase
    std::string st, c, cpp;                      // un exemple dans chaque langage
};
[[nodiscard]] const std::vector<std::string_view>& conversionTypes();
[[nodiscard]] std::vector<Conversion>               conversions();
[[nodiscard]] std::optional<Conversion>             conversion(std::string_view name);   // "INT_TO_REAL", sans casse

// -------------------------------------------------------------- enumerations natives ---
[[nodiscard]] const NativeEnum* nativeEnum(std::string_view name) noexcept;   // sans casse
// "TRANSITION#Fondu", "transition#1" (espaces autour admis). Faux : pas un litteral natif.
bool parseEnumLiteral(std::string_view text, const NativeEnum** e, const EnumValue** v) noexcept;
// L'argument `index` de la fonction `function` (IHM_NAVIGUER, IHM_POPUP...) donne en
// nombre (TRANSITION#Fondu) : le mot qu'elle attend ("Fondu"). Rien : pas un tel argument.
[[nodiscard]] std::optional<std::string> enumArgument(std::string_view function, std::size_t index, const sim::Value& v);
// 1.12.1 - LES PROPRIETES DES OBJETS. L'enumeration native que prend la propriete `key`
// d'un objet `kind` (hmi::kindKey : "Selector", "Text"...) ; le genre precis d'abord,
// puis "*". nullptr : une propriete sans enumeration.
[[nodiscard]] const NativeEnum* propertyEnum(std::string_view kind, std::string_view key) noexcept;
// La valeur d'une expression liee a une telle propriete : un nombre de l'enumeration
// (STYLE_SELECTEUR#boutons, 1) donne le mot de l'inspecteur ("boutons"). Rien : pas
// une telle propriete, un texte (le mot deja), ou un nombre hors de l'enumeration.
[[nodiscard]] std::optional<std::string> propertyWord(std::string_view kind, std::string_view key, const sim::Value& v);
// La liste d'une propriete dans l'inspecteur (ses choix) : l'enumeration native qui sert
// a des proprietes et dont les mots sont ces choix, dans n'importe quel ordre - suivis,
// au plus, de choix "recette:...", "objet:..." (le bouton d'export). nullptr : aucune.
[[nodiscard]] const NativeEnum* enumForWords(const std::vector<std::string>& words) noexcept;
// L'expression d'une telle propriete est une constante qui n'aurait aucun effet - un
// litteral d'une autre enumeration (TRANSITION#Fondu dans align), un nombre hors de la
// sienne (7) : le message de Compiler et de l'inspecteur. "" : rien a dire.
[[nodiscard]] std::string propertyProblem(std::string_view kind, std::string_view key, std::string_view expression);
// Les litteraux d'enumerations natives d'une expression ("ALIGNEMENT#centre", dans
// l'ordre, sans les chaines ni les commentaires) : leur enumeration et leur texte.
struct EnumLiteralUse { const NativeEnum* enumeration{nullptr}; std::string text; };
[[nodiscard]] std::vector<EnumLiteralUse> enumLiteralsIn(std::string_view expression);

// ------------------------------------------------------- les fonctions propres a l'IHM ---
// Les couleurs et l'aleatoire : RGB, RGBA, HSL, COULEUR_..., RANDOM, RANDOM_INT...
[[nodiscard]] bool isOwnFunction(std::string_view name) noexcept;
// Faux : pas une de ces fonctions, ou des arguments en trop ou en moins (`why`).
bool call(std::string_view name, const std::vector<sim::Value>& args, sim::Value& out, std::string* why = nullptr);
// '#RRGGBB', '#RRGGBBAA', '16#RRGGBB', 'RRGGBB' (sans casse, espaces autour admis).
bool parseColor(std::string_view text, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) noexcept;
// La suite aleatoire repart (RANDOM_SEED, les essais).
void seedRandom(std::uint32_t seed) noexcept;

// ----------------------------------------------------------------------- les types ---
// La fiche d'un type de base : le registre (sa plage, sa taille, ses usages) et le
// catalogue (C, C++, le defaut, la place Modbus, les litteraux, les notes).
struct TypeCard {
    std::string name, category, summary;
    std::string minText, maxText, size, modbus, cType, cppType, defaultValue;
    int         bits{0};
    std::vector<std::string> literals, notes, uses;
};
[[nodiscard]] std::vector<TypeCard>   typeCards();
[[nodiscard]] std::optional<TypeCard> typeCard(std::string_view name);

// ------------------------------------------------------------------------ l'arbre ---
// 1.12.0 : la branche Natives de l'arbre (IHM > Programmation generale) : ses listes,
// gardees - conversions() et typeCards() se recomposent a chaque appel, l'arbre les
// lit plusieurs fois par ligne et par image.
struct Tree {
    std::vector<const Category*>          categories;     // celles qui ont des fonctions, dans l'ordre du catalogue
    std::vector<std::vector<std::size_t>> functionsOf;    // par categorie : les rangs dans functions()
    std::vector<TypeCard>                 types;
    std::size_t                           conversionCount{0};
    std::size_t                           functionCount{0};   // les fonctions et les conversions
};
[[nodiscard]] const Tree& tree();
// Du type de rang `from`, la conversion vers le type de rang `to` (rangs de
// conversionTypes) ; vide : hors bornes, ou le meme type.
[[nodiscard]] std::string conversionName(std::size_t from, std::size_t to);

// ------------------------------------------------------------ 1.12.17 : le rangement ---
// (HmiNativesShelf.cpp) L'arbre Natives et le selecteur de fonctions rangent les
// fonctions par ce qu'on veut faire : huit themes (Nombres, Textes, Dates et heures,
// Couleurs, Conteneurs, Conversions, L'IHM, References et echanges), des sous-dossiers
// par geste au-dela de 20 fonctions (Listes : Ajouter et retirer, Lire et chercher...).
// Une fonction a une place, et une seule ; De A a Z (alphabetical) les montre toutes.
struct Shelf {
    std::string              key;                 // "conteneurs/listes/ajouter" ; "" : Fonctions (la racine)
    std::string              title, summary;
    std::vector<std::size_t> folders;             // les sous-dossiers (rangs dans shelves())
    std::vector<std::size_t> functions;           // ses fonctions (rangs dans functions())
    bool                     conversions{false};  // Conversions : les X_TO_Y apres ses sous-dossiers
    std::size_t              total{0};            // les fonctions dessous (X_TO_Y compris)
    std::size_t              parent{static_cast<std::size_t>(-1)};
};
[[nodiscard]] const std::vector<Shelf>& shelves();                       // [0] : Fonctions
[[nodiscard]] std::size_t   shelfIndex(std::string_view key) noexcept;   // -1 : aucun
[[nodiscard]] const Shelf*  shelf(std::string_view key) noexcept;
[[nodiscard]] std::size_t   homeOf(std::size_t function) noexcept;       // son dossier (le plus profond)
// Le chemin d'un dossier sous Fonctions : "Conteneurs > Listes" (le separateur donne).
[[nodiscard]] std::string   shelfPath(std::size_t shelf, std::string_view separator);
// Le plus proche dossier commun des fonctions d'une categorie du catalogue (les anciennes
// cles "categorie:<id>") ; -1 : aucune fonction.
[[nodiscard]] std::size_t   categoryShelf(std::string_view category);
[[nodiscard]] const std::vector<std::size_t>& alphabetical();            // De A a Z
// L'en-tete C++ d'une fonction ("<numeric>"), une etiquette de sa fiche ; vide : aucun.
[[nodiscard]] std::string_view cppHeader(const Function&) noexcept;

// Les types en cinq dossiers : Nombres et bits, Textes durees et dates (avec STRING[n]),
// Conteneurs, References, Generiques.
struct TypeEntry {
    enum class Kind : std::uint8_t { Base, Constructed, Generic };
    Kind        kind{Kind::Base};
    std::size_t index{0};                         // tree().types, constructed(), generics()
    std::string name;                             // "INT", "LIST OF", "ANY_NUM"
};
struct TypeShelf { std::string key, title, summary; std::vector<TypeEntry> entries; };
[[nodiscard]] const std::vector<TypeShelf>& typeShelves();
// Un conteneur explique (sa fiche) ; l'essai natives joue son exemple et compare ce
// qu'il vaut (type de R, expression, valeur montree).
struct TypeCheck { std::string_view type, expression, expected; };
struct TypeGuide {
    std::string_view       name, title, lead, when, declare, access, iterate, lives, c, cpp;
    std::string_view       shelf;                 // le dossier de ses fonctions ("conteneurs/listes")
    std::string_view       sample;                // un type pour lire ses proprietes ("LIST OF REAL")
    std::string_view       example;               // le script de l'exemple
    std::vector<TypeCheck> checks;                // ce qu'il vaut apres
};
[[nodiscard]] const std::vector<TypeGuide>& typeGuides();
[[nodiscard]] const TypeGuide* typeGuide(std::string_view name) noexcept;

// Les enumerations par usage : pour les IHM_, pour les listes, les dates, vos
// variables, les proprietes des objets (en neuf familles).
struct EnumShelf {
    std::string              key, title, summary;
    std::vector<std::size_t> folders, enums;      // enums : rangs dans enums()
    std::size_t              total{0};
    std::size_t              parent{static_cast<std::size_t>(-1)};
};
[[nodiscard]] const std::vector<EnumShelf>& enumShelves();               // [0] : Enumerations
[[nodiscard]] std::size_t enumShelfOf(std::size_t enumeration) noexcept;

} // namespace hmi::natives
