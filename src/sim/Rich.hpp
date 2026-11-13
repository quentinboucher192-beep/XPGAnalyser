// =============================================================================
//  sim/Rich.hpp - 1.10 : les valeurs riches du DIALECTE IHM
// -----------------------------------------------------------------------------
//  Le ST de l'automate ne connait que des cases simples (sim::Value) sous des
//  noms ; c'est l'environnement qui range les structures et les tableaux. Les
//  scripts de l'IHM (et eux seuls : sim::ParseOptions::hmiDialect) ont en plus
//  des variables A EUX, gardees par l'interpreteur :
//
//    - un tableau a N dimensions, bornes libres : ARRAY[0..3, 0..9, 0..1] OF REAL
//      (taille fixe, comme std::array<X, N> : copie entiere, comparaison, .Length) ;
//    - une structure (un type IHM, un DDT : ses membres viennent de l'environnement) ;
//    - une MAP : MAP[STRING] OF T, MAP[DINT] OF T (cles en ordre croissant) ;
//    - une reference (REF_TO T) ou un pointeur (POINTER TO T) : vers un objet de
//      l'interpreteur (weak_ptr : une variable disparue se voit) ou vers un nom de
//      l'environnement (une variable IHM, de l'automate) ;
//    - un iterateur de MAP (MAP_BEGIN / MAP_NEXT / MAP_END, it.Key, it.Value) ;
//    - 1.12.2 : une LISTE (LIST OF T) ou un VECTEUR (VECTOR OF T) : une suite de
//      taille variable, indicee a partir de 0 (L[0]), L.Count, LIST_ADD, VECTOR_PUSH... ;
//    - 1.12.2 : un TUPLE (TUPLE(INT, REAL, STRING)) : des valeurs de types fixes,
//      t.Item1, t.Item2... (taille fixe, comme std::tuple).
//    - 1.12.19 : un RAPPEL (CALLBACK (x : REAL) : BOOL) : une fonction donnee comme une valeur - une
//      fonction du projet, d'un script, d'une instance de symbole, par son nom, ou une LAMBDA (son code
//      et les locales copiees a sa creation) ; NULL : vide (l'appeler ne fait rien et le dit une fois).
//      En memoire de l'IHM seulement (dynamic()) : jamais dans un equipement.
//    - 1.12.7 : un ENSEMBLE (SET OF T, std::set) : une liste rangee, chaque valeur une
//      seule fois ; un TAMPON CIRCULAIRE (RING[N] OF T) : une liste des N dernieres
//      valeurs (pleine, la plus ancienne part quand on en ajoute une). Ce sont des
//      listes (Kind::List, `name` "SET" ou "RING", `capacity` N) : l'affichage, FOR EACH,
//      .Count, les natives LIST_ qui lisent marchent ; chaque ecriture rend a la liste sa
//      forme (normalizeList).
//
//  LES LITTERAUX (1.12.2) : [1, 2, 3] (une liste, un tableau rempli dans l'ordre :
//  la derniere dimension varie le plus vite), ['a' := 1, 'b' := 2] (une MAP), (1, 'x')
//  (un tuple). Leur type (`literal`) se convertit dans celui de la destination.
//
//  TAILLE VARIABLE (dynamic()) : une MAP, une liste, un vecteur (ou ce qui en
//  contient) n'a pas de place fixe : jamais dans la memoire d'un equipement.
//
//  Chaque case d'un tableau, chaque membre, chaque valeur d'une MAP est un objet a
//  part (ObjRef) : une reference peut viser une case, et la garder tant qu'elle
//  existe.
// =============================================================================
#pragma once

#include "Value.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim {

struct TypeDesc;
using TypeRef = std::shared_ptr<const TypeDesc>;

struct TypeDesc {
    enum class Kind : std::uint8_t { Scalar, Array, Struct, Map, Ref, Pointer, Iterator, List, Tuple,
                                     Callback, /* 1.12.19 : members = parametres, element = retour (nul : aucun) */
                                     Any /* 1.13.0 : une valeur JSON / dynamique - voir anyType() */,
                                     Handle /* 1.27.0 : un objet ou une ressource de l'IHM - voir handleType() */ };
    Kind        kind{Kind::Scalar};
    Type        scalar{Type::Unknown};                              // Scalar
    std::vector<std::pair<std::int64_t, std::int64_t>> bounds;      // Array : [bas, haut] par dimension
    TypeRef     element;                                            // Array, Map (valeur), List, Ref et Pointer (cible)
    Type        key{Type::String};                                  // Map : STRING ou un entier
    std::string name;                                               // Struct : le nom du type ; Scalar : "LREAL"... ; List : "LIST", "VECTOR", "SET" ou "RING"
    std::int64_t capacity{0};                                       // 1.12.7 : List "RING" : N, le nombre de valeurs gardees
    std::vector<std::pair<std::string, TypeRef>> members;           // Struct (resolue), Tuple (Item1, Item2...)
    bool        literal{false};                                     // 1.12.2 : le type d'un litteral [..], (..) : il se convertit

    [[nodiscard]] std::string  text() const;                        // "ARRAY[0..9] OF REAL", "MAP[STRING] OF T_FOUR", "LIST OF INT"
    [[nodiscard]] std::int64_t count() const noexcept;              // Array : les cases (toutes dimensions)
    [[nodiscard]] bool         aggregate() const noexcept { return kind != Kind::Scalar; }
    // 1.12.2 : une MAP, une liste, un vecteur, ou un type qui en contient (un tableau de listes...).
    [[nodiscard]] bool         dynamic() const noexcept;
};

// 1.12.19 : CALLBACK (x : REAL; y : INT) : BOOL - ses parametres (nom, type ; un nom vide : par position)
// et son retour (nul : aucun).
[[nodiscard]] TypeRef callbackType(std::vector<std::pair<std::string, TypeRef>> params, TypeRef result);
// 1.12.19 : un rappel de cette forme accepte-t-il une fonction de celle-ci ? Le meme nombre de
// parametres, des types identiques (ou deux nombres), un retour des deux cotes ou d'aucun. Faux : `why`.
[[nodiscard]] bool callbackFits(const TypeDesc& want, const TypeDesc& got, std::string* why = nullptr);

// 1.12.2 : LIST OF T, VECTOR OF T (`vector` : VECTOR) ; TUPLE(T1, T2...) (Item1, Item2...).
[[nodiscard]] TypeRef listType(TypeRef element, bool vector = false);
[[nodiscard]] TypeRef tupleType(std::vector<TypeRef> items);
// 1.12.7 : SET OF T (rangee, chaque valeur une fois) ; RING[N] OF T (les N dernieres valeurs).
[[nodiscard]] TypeRef setType(TypeRef element);
[[nodiscard]] TypeRef ringType(TypeRef element, std::int64_t capacity);
[[nodiscard]] bool    isSetType(const TypeDesc& t) noexcept;
[[nodiscard]] bool    isRingType(const TypeDesc& t) noexcept;
// La plus grande capacite d'un RING (RING[100000] OF REAL).
inline constexpr std::int64_t kRingMax = 100000;

[[nodiscard]] TypeRef scalarType(Type t, std::string_view name = {});
// 1.13.0 : ANY - une valeur JSON / dynamique. C'est un joker de compilation (le type statique) : a
// l'execution, un Obj de Kind::Any est une BOITE qui porte une valeur de son vrai type -
//   - items vide : pas de charge. `bound` vrai : JSON null ; `bound` faux : JSON absent (une cle qui manque) ;
//   - items a un element : la charge concrete (un scalaire STRING/LREAL/LINT/BOOL, une MAP[STRING] OF ANY,
//     une LIST OF ANY, un ARRAY, une structure...). Jamais une boite dans une boite.
// Un objet JSON est une MAP[STRING] OF ANY, un tableau une LIST OF ANY : leurs cases sont des boites ANY.
// Reserve au dialecte IHM (hmiDialect), jamais au ST de l'automate, comme LIST / MAP. Pas de place Modbus
// (dynamic() est vrai) : en memoire de script / de l'IHM seulement.
[[nodiscard]] TypeRef anyType();
// 1.27.0 : LES TYPES DE L'IHM - Object (un objet d'une vue), Font, Texture, Sound, Style (une ressource du projet).
// Une valeur de ces types est une POIGNEE : elle designe la chose, elle ne la copie pas. `name` : le type ("Object"...) ;
// l'Obj porte `bound` (faux : NULL) et `targetName`, le chemin que l'environnement connait : "Vue_Accueil.Pompe_1" pour
// un objet, "@Font:Carlito Gras" pour une ressource (prefixe @, le type, deux-points, le nom). Ses membres se lisent et
// s'ecrivent dans l'environnement : h.Fill est "<chemin>.Fill". Un type du projet du meme nom passe avant (resolve).
// Reserves au dialecte IHM ; jamais dans une memoire d'equipement (dynamic() est vrai).
[[nodiscard]] TypeRef handleType(std::string_view name);
// "OBJECT", "font"... : le nom du type de l'IHM ("Object", "Font", "Texture", "Sound", "Style") ; vide : ce n'en est pas un.
[[nodiscard]] std::string handleTypeName(std::string_view name);
// Le chemin d'une ressource : "@Font:Carlito Gras" ; et l'inverse (faux : pas un chemin de ressource).
[[nodiscard]] std::string resourcePath(std::string_view type, std::string_view name);
bool splitResourcePath(std::string_view path, std::string& type, std::string& name);

// Meme forme : memes dimensions et bornes, meme element, meme structure (par son nom).
[[nodiscard]] bool sameType(const TypeDesc& a, const TypeDesc& b) noexcept;

struct Obj;
using ObjRef = std::shared_ptr<Obj>;

// ---- 1.50.1 : LES GRANDS TABLEAUX RANGES SERRES ------------------------------------------------------------------
//  Un tableau de valeurs simples - ou de tableaux de valeurs simples, a tous les niveaux (ARRAY[0..999] OF
//  ARRAY[0..9999] OF BOOL) - d'au moins packFrom() cases en tout garde ses valeurs dans UN SEUL BLOC : un BOOL = 1 bit,
//  un BYTE 1 octet, un INT / WORD 2, un DINT / DWORD / TIME 4, un LINT / une date 8, un REAL / LREAL 8, un STRING son
//  texte. Ses `items` restent vides (10 000 000 de BOOL : 1,2 Mo, et non 4 Go d'objets).
//  - UNE CASE SORTIE EN OBJET (cellAt : a[i] := x, une reference, FOR EACH...) vit dans `live` le temps qu'on la tient,
//    et fait foi ; lachee (plus personne ne la tient, pas de reference vers elle : `pinned`), sa valeur rentre dans le
//    bloc. Une lecture simple (cellValue) ne sort rien.
//  - UNE LIGNE, UNE COUCHE (a[i] sur plusieurs dimensions, une case d'un tableau de tableaux) est une VUE du meme bloc
//    (packedFrom : sa premiere case) : l'ecrire ecrit le tableau.
//  - unpack : le tableau sort toutes ses cases (items) - pour une fonction qui parcourt `items` (LIST_SORT...) ; le bloc
//    reste et les cases sorties font foi (les memes objets).
struct PackedCells {
    Type        scalar{Type::Unknown};
    TypeRef     cell;                                // le type d'une case (une valeur simple)
    std::size_t count{0};                            // les cases du bloc entier
    int         width{0};                            // octets par case (entiers, durees, dates) ; 0 : bits, reels, textes
    std::vector<std::uint64_t> bits;                 // BOOL
    std::vector<unsigned char> bytes;                // les entiers, les durees, les dates (`width` octets chacun)
    std::vector<double>        reals;                // REAL, LREAL
    std::vector<std::string>   texts;                // STRING
    std::unordered_map<std::size_t, ObjRef> live;    // les cases sorties en objet (elles font foi)
    std::map<std::tuple<std::size_t, std::size_t, const TypeDesc*>, ObjRef> views;   // les vues tenues (premiere case, nombre, type)
    std::size_t settleAt{256};
    [[nodiscard]] Value raw(std::size_t k) const;    // la valeur du bloc (sans `live`)
    void put(std::size_t k, const Value& v);         // `v` deja du type de la case
    [[nodiscard]] std::size_t memory() const noexcept;
};
// Les cases a partir desquelles un tableau neuf se range serre (65 536) ; les essais le baissent.
[[nodiscard]] std::int64_t packFrom() noexcept;
void setPackFrom(std::int64_t cells) noexcept;
// Ce tableau (et ses tableaux de tableaux) se range-t-il serre : des valeurs simples au fond ?
[[nodiscard]] bool packable(const TypeDesc& array) noexcept;
// LA MEMOIRE : une case rangee serree (BOOL : 0,125 octet ; STRING : 32 et son texte), un objet (une case non serree,
// un membre), une valeur neuve de ce type (estimee, en octets) ; la limite par valeur (1 Gio) ; un nombre d'octets
// ecrit ("1,2 Mo", "476 Mo", "1,5 Go") ; le message d'un type trop grand.
[[nodiscard]] double       cellBytes(Type) noexcept;
[[nodiscard]] std::int64_t objectBytes() noexcept;
[[nodiscard]] std::int64_t memoryEstimate(const TypeDesc&);
inline constexpr std::int64_t kMaxValueBytes = std::int64_t{1} << 30;
[[nodiscard]] std::string  memoryText(std::int64_t bytes);
[[nodiscard]] std::string  tooLargeText(const TypeDesc&, std::int64_t bytes);
// Les cases d'un niveau de tableau (items ou bloc) ; la case k (sortie en objet si le tableau est serre ; une vue
// pour un tableau de tableaux) ; la valeur simple de la case k, sans la sortir ; l'ecrire (assignFrom).
[[nodiscard]] std::size_t cellCount(const Obj& array) noexcept;
[[nodiscard]] ObjRef      cellAt(const Obj& array, std::size_t k);
[[nodiscard]] Value       cellValue(const Obj& array, std::size_t k);
void                      setCellValue(const Obj& array, std::size_t k, const Value& v);
// Une vue de `span` cases de ce tableau a partir de `first` (une ligne : a[i] sur deux dimensions), du type `type` ; un
// tableau non serre : un tableau des memes cases (les memes objets).
[[nodiscard]] ObjRef      subView(const Obj& array, std::size_t first, std::size_t span, TypeRef type);
// Toutes les cases sorties (items) ; faux et `why` au-dela de `maxCells`.
bool                      unpack(Obj& array, std::int64_t maxCells, std::string* why);
// Une valeur pour toutes les cases simples d'un tableau (a tous les niveaux) : la valeur initiale "0", "FALSE" d'un grand
// tableau. Faux : des cases qui ne sont pas des valeurs simples (des structures).
bool                      fillCells(const Obj& array, const Value& v);
// La case visee par une reference : elle reste un objet (une case d'un tableau serre ne rentre pas dans le bloc).
void                      pin(const ObjRef& o) noexcept;
// La memoire tenue par un objet et ce qu'il contient (estimee, en octets) - les essais.
[[nodiscard]] std::size_t heldMemory(const Obj& o);

struct Obj {
    TypeRef type;
    Value   value;                                   // Scalar
    std::vector<ObjRef> items;                       // Array (a plat, la derniere dimension varie le plus vite), Struct, Tuple (membres), List
    std::map<std::string, ObjRef>  textKeys;         // Map a cle STRING
    std::map<std::int64_t, ObjRef> intKeys;          // Map a cle entiere
    // Ref, Pointer : la cible ; `bound` faux : NULL.
    bool                bound{false};
    std::weak_ptr<Obj>  target;                      // un objet de l'interpreteur...
    std::string         targetName;                  // ... ou un nom de l'environnement (vide : objet)
    // Iterator : la MAP parcourue et la cle en cours (`atEnd` : apres la derniere).
    std::weak_ptr<Obj>  map;
    std::string         textKey;
    std::int64_t        intKey{0};
    bool                atEnd{true};
    // 1.12.19 : un rappel (Callback) - la fonction visee par son nom ("Est_Valide", "Vue_A.Pompe1.Dans_Plage"),
    // ou une LAMBDA (`lambda` : son code et ses locales copiees, gardes par l'interpreteur ; `callee` vide).
    // `bound` faux : NULL.
    std::string                 callee;
    std::shared_ptr<const void> lambda;
    std::string                 lambdaWhere;          // « Script_Tri, ligne 12 » (l'affichage, la Console)
    // 1.50.1 : un grand tableau range serre (voir PackedCells) : son bloc et sa premiere case dedans (une vue) ;
    // `pinned` : une case visee par une reference reste un objet.
    std::shared_ptr<PackedCells> packed;
    std::size_t                  packedFrom{0};
    bool                         pinned{false};
};

// 1.27.0 : le nom d'une poignee a montrer (et a donner a une fonction qui attend un STRING) : le nom de la ressource,
// le chemin de l'objet ; "NULL" si elle ne designe rien.
[[nodiscard]] std::string handleText(const Obj& o);

// Un objet neuf de ce type, a sa valeur par defaut (une structure doit etre resolue).
[[nodiscard]] ObjRef makeObj(const TypeRef& type);
// La copie profonde (une reference est copiee comme reference : elle vise la meme chose).
[[nodiscard]] ObjRef deepCopy(const Obj& o);
// Copie `from` dans `into`, de meme forme ; faux (et `why`) sinon.
bool assignObj(Obj& into, const Obj& from, std::string* why);
// 1.12.7 : rend a un SET sa forme (rangee, sans doublon : la premiere de chaque valeur reste) et a
// un RING la sienne (ses `capacity` dernieres valeurs : les plus anciennes partent) ; le nombre de
// valeurs retirees. Une autre liste ne change pas.
std::size_t normalizeList(Obj& o);
// L'egalite profonde (tableaux, structures, MAP ; references : meme cible).
[[nodiscard]] bool deepEquals(const Obj& a, const Obj& b);
// Pour le journal : "[1.5, 2, 3]", "{Temp: 20.5, Marche: TRUE}", "{'a': 1}", "(1, 'x')".
[[nodiscard]] std::string display(const Obj& o);
// 1.12.2 : la valeur ecrite comme un litteral du dialecte, qui se relit : [1.5, 2, 3],
// ['a' := 1], (1, 'x'), un texte entre apostrophes ('' : vide). Faux si l'objet ne
// s'ecrit pas ainsi (une reference, un iterateur, une structure).
bool literalText(const Obj& o, std::string& out);
// Le rang a plat d'une case ; faux (et `why`) hors des bornes ou mauvais nombre d'indices.
bool flatIndex(const TypeDesc& array, const std::vector<std::int64_t>& indices, std::size_t& out, std::string* why);

// LES VARIABLES D'UN SCRIPT, gardees par l'appelant (RunLimits::locals) d'une
// execution a l'autre : les VAR y restent, les VAR_TEMP sont redeclarees.
class Locals {
public:
    void   set(std::string_view name, ObjRef obj);          // sans casse
    [[nodiscard]] ObjRef find(std::string_view name) const;
    void   erase(std::string_view name);
    void   clear() noexcept { vars_.clear(); }
    [[nodiscard]] const std::map<std::string, ObjRef, std::less<>>& all() const noexcept { return vars_; }
private:
    std::map<std::string, ObjRef, std::less<>> vars_;     // nom en majuscules
};

} // namespace sim
