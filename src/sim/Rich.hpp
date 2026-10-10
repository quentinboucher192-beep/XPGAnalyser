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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim {

struct TypeDesc;
using TypeRef = std::shared_ptr<const TypeDesc>;

struct TypeDesc {
    enum class Kind : std::uint8_t { Scalar, Array, Struct, Map, Ref, Pointer, Iterator, List, Tuple };
    Kind        kind{Kind::Scalar};
    Type        scalar{Type::Unknown};                              // Scalar
    std::vector<std::pair<std::int64_t, std::int64_t>> bounds;      // Array : [bas, haut] par dimension
    TypeRef     element;                                            // Array, Map (valeur), List, Ref et Pointer (cible)
    Type        key{Type::String};                                  // Map : STRING ou un entier
    std::string name;                                               // Struct : le nom du type ; Scalar : "LREAL"... ; List : "LIST" ou "VECTOR"
    std::vector<std::pair<std::string, TypeRef>> members;           // Struct (resolue), Tuple (Item1, Item2...)
    bool        literal{false};                                     // 1.12.2 : le type d'un litteral [..], (..) : il se convertit

    [[nodiscard]] std::string  text() const;                        // "ARRAY[0..9] OF REAL", "MAP[STRING] OF T_FOUR", "LIST OF INT"
    [[nodiscard]] std::int64_t count() const noexcept;              // Array : les cases (toutes dimensions)
    [[nodiscard]] bool         aggregate() const noexcept { return kind != Kind::Scalar; }
    // 1.12.2 : une MAP, une liste, un vecteur, ou un type qui en contient (un tableau de listes...).
    [[nodiscard]] bool         dynamic() const noexcept;
};

// 1.12.2 : LIST OF T, VECTOR OF T (`vector` : VECTOR) ; TUPLE(T1, T2...) (Item1, Item2...).
[[nodiscard]] TypeRef listType(TypeRef element, bool vector = false);
[[nodiscard]] TypeRef tupleType(std::vector<TypeRef> items);

[[nodiscard]] TypeRef scalarType(Type t, std::string_view name = {});
// Meme forme : memes dimensions et bornes, meme element, meme structure (par son nom).
[[nodiscard]] bool sameType(const TypeDesc& a, const TypeDesc& b) noexcept;

struct Obj;
using ObjRef = std::shared_ptr<Obj>;

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
};

// Un objet neuf de ce type, a sa valeur par defaut (une structure doit etre resolue).
[[nodiscard]] ObjRef makeObj(const TypeRef& type);
// La copie profonde (une reference est copiee comme reference : elle vise la meme chose).
[[nodiscard]] ObjRef deepCopy(const Obj& o);
// Copie `from` dans `into`, de meme forme ; faux (et `why`) sinon.
bool assignObj(Obj& into, const Obj& from, std::string* why);
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
