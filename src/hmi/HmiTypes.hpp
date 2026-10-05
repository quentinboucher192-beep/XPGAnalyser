// =============================================================================
//  hmi/HmiTypes.hpp - les types IHM, les tableaux, et leur place Modbus (lot 16)
// -----------------------------------------------------------------------------
//  UNE VARIABLE IHM PEUT ETRE COMPOSEE :
//    - d'un type IHM, une structure (Programs::types) : Four1 : T_Four ;
//    - un tableau a une ou deux dimensions, bornes libres :
//          ARRAY[0..9] OF REAL        ARRAY[0..3, 0..9] OF INT
//          ARRAY[1..4] OF T_Four      (un tableau de structures)
//
//  DANS LE MOTEUR, UNE VALEUR PAR CASE. Comme les variables composees de
//  l'automate, une variable composee est DEPLIEE en cases simples, chacune sous
//  son chemin : Four1.Temperature, Four1.Vannes[2].Position, Consignes[3],
//  Matrice[2,7]. Le ST les ecrit ainsi (indices calcules compris) ; le moteur
//  n'a rien d'autre a savoir.
//
//  LES PROPRIETES PUBLIQUES d'un tableau (et d'une structure pour le poids) se
//  lisent comme des membres : Consignes.Length, Matrice.Rows, Four1.Words.
//  Elles sont en lecture seule ; un membre du meme nom passe avant.
//
//  LA PLACE MODBUS d'une variable composee liee a un equipement : elle part de
//  son adresse, et ses cases suivent dans l'ordre (INT, UINT, WORD 1 mot ; DINT,
//  UDINT, DWORD, REAL, LREAL, TIME 2 mots ; STRING 16 mots). Les BOOL a la
//  suite se rangent 16 par mot (bits 0 a 15), sinon un mot chacun. Une
//  structure, un tableau et chaque element d'un tableau de structures
//  commencent sur un mot neuf : tous les elements ont la meme taille. Le poids
//  (Bytes, Words) est celui de cette place.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../sim/Value.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::types {

// ------------------------------------------------------------------ types ---
struct Spec {
    std::string element;           // "REAL", "T_Four" (un type elementaire en majuscules)
    int         dims{0};           // 0 : pas un tableau ; 1 ou 2
    long long   low[2]{0, 0};
    long long   high[2]{-1, -1};
    [[nodiscard]] bool      array() const noexcept { return dims > 0; }
    [[nodiscard]] long long count(int d) const noexcept { return d < dims ? high[d] - low[d] + 1 : 1; }
    [[nodiscard]] long long length() const noexcept { return dims == 0 ? 1 : count(0) * count(1); }
};

// "ARRAY[0..9] OF REAL", "ARRAY [ 0 .. 3 , 0..9 ] of int", "T_Four", "real" -> Spec.
bool parseSpec(std::string_view text, Spec& out, std::string* why = nullptr);
// La forme ecrite : "ARRAY[0..9] OF REAL", "ARRAY[0..3, 0..9] OF INT", "T_Four".
[[nodiscard]] std::string specText(const Spec&);
// Le texte d'un type, remis en forme (ou tel quel s'il ne se lit pas).
[[nodiscard]] std::string normalized(std::string_view type);
[[nodiscard]] bool isElementary(std::string_view name) noexcept;     // BOOL, INT... (sans casse)
[[nodiscard]] bool isComposite(std::string_view type) noexcept;      // un tableau ou un autre nom qu'un elementaire
// Connu : elementaire, type IHM du projet, ou tableau de l'un d'eux, bornes lisibles.
[[nodiscard]] bool validType(const Project&, std::string_view type, std::string* why = nullptr);
// Un type IHM qui se contient lui-meme : le chemin du cycle ("T_A -> T_B -> T_A") ; vide : aucun.
[[nodiscard]] std::string cycleOf(const Project&, std::string_view typeName);
// Les variables et les types qui emploient ce type IHM (ses noms).
[[nodiscard]] std::vector<std::string> usersOf(const Project&, std::string_view typeName);

inline constexpr long long kMaxElements = 100000;    // les cases d'une variable, au plus

// ------------------------------------------------------------ la place ------
// Les mots Modbus d'un type elementaire (un BOOL seul : 1).
[[nodiscard]] long long wordsOf(std::string_view elementary) noexcept;
struct Weight {
    long long words{0};            // la place Modbus, en mots de 16 bits
    long long bytes{0};            // la meme, en octets (2 par mot)
};
[[nodiscard]] Weight weightOf(const Project&, std::string_view type, bool packBools = true);

// ------------------------------------------------------- le depliage --------
struct Leaf {
    std::string path;              // "Four1.Vannes[2].Position"
    std::string rel;               // relatif a la variable : "Vannes[2].Position"
    std::string type;              // elementaire : "INT"
    std::string initial;           // sa valeur initiale (vide : celle du type)
    std::string description;
    long long   word{0};           // son premier mot, depuis le debut de la variable
    int         bit{-1};           // un BOOL range dans un mot : son bit (0..15)
    long long   words{0};          // les mots qu'elle ajoute (0 : un BOOL dans un mot deja compte)
};
struct Aggregate {
    std::string path;              // "Four1", "Four1.Vannes", "Fours[2]"
    bool        array{false};
    Spec        spec;              // un tableau : ses bornes ; une structure : son type
    std::string typeName;          // "T_Four", "ARRAY[1..3] OF T_Vanne"
    Weight      weight;
    long long   elementBytes{0};   // un tableau : le poids d'une case
};
struct Flat {
    std::vector<Leaf>      leaves;
    std::vector<Aggregate> aggregates;
    std::string            error;  // type inconnu, circulaire, trop de cases ; vide : bon
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};
// Deplie `type` sous le nom `root`. `initial` : pour un tableau de cases
// simples, une valeur pour toutes ("0") ou une liste ("1.5, 2, 3") ; pour une
// structure, ce sont les valeurs du type.
[[nodiscard]] Flat flatten(const Project&, std::string_view root, std::string_view type, std::string_view initial = {},
                           bool packBools = true);

// LES VARIABLES IHM, DEPLIEES : une variable simple reste elle-meme ; une
// structure ou un tableau donne une variable par case, sous son chemin, avec
// l'id de sa variable. Liee, chaque case a son adresse (le depart + sa place,
// ecrite comme le depart - 43001, %MW3000, HR3000 -, ou l'adresse corrigee de
// `places`). `why` (facultatif) : pourquoi une case n'a pas d'adresse, a son
// rang ; `aggregates` : les structures et tableaux rencontres ; `error` : le
// type ne se deplie pas. 1.11.8 : un membre interne (Variable::internal) n'est
// pas lie (ni equipement, ni adresse, `why` vide) ; `places` peut donner le depart
// d'un membre compose ("Vannes[2]" -> "%MW3050" : ses cases le suivent) ;
// Variable::compact rend les mots que n'occupent que des membres internes.
[[nodiscard]] std::vector<Variable> leafVariables(const Project&, const Variable&, std::vector<Aggregate>* aggregates = nullptr,
                                                  std::vector<std::string>* why = nullptr, std::string* error = nullptr);
[[nodiscard]] std::vector<Variable> flatVariables(const Project&);

// 1.11.8 : un chemin de membre (relatif a la variable) en couvre-t-il un autre ? Lui-meme,
// et tout ce qui est dessous ("Vannes[2]" couvre "Vannes[2].Position") ; un indice [*]
// couvre tous les indices ("[*].NOM" couvre "[0].NOM", "[63].NOM"). Sans casse, sans blancs.
[[nodiscard]] bool memberCovers(std::string_view pattern, std::string_view rel);
// Le membre `rel` de `v` est-il interne (Variable::internal) ? Une variable non liee : non.
[[nodiscard]] bool isInternalMember(const Variable& v, std::string_view rel);
// L'entree de Variable::internal qui rend `rel` interne (vide : aucune).
[[nodiscard]] std::string internalEntryOf(const Variable& v, std::string_view rel);

// L'adresse d'une case a `word` mots (et `bit`) du depart, ecrite comme le depart.
// Une zone de bits (%M, 00017) : `word` est le rang du BOOL.
[[nodiscard]] std::string memberAddress(std::string_view start, long long word, int bit, std::string_view type,
                                        std::string* why = nullptr);
// Le depart est-il une zone de bits (bobines, entrees TOR) ?
[[nodiscard]] bool bitArea(std::string_view start) noexcept;
// La premiere et la derniere place (mots, ou bits) d'une variable liee, pour
// dire "registres 43001 a 43021" ; faux sans adresse lisible.
struct Span {
    long long first{0}, last{-1};
    bool      bits{false};
};
[[nodiscard]] bool spanOf(const Project&, const Variable&, Span& out);
// "registres 43001 a 43013", "mots 3000 a 3012", "bobines 01001 a 01008".
[[nodiscard]] std::string spanText(const Project&, const Variable&);
// La meme chose pour une partie des cases (un membre compose : Four1.Vannes),
// ecrite comme `startAddress` (Modicon ou Schneider).
[[nodiscard]] std::string spanTextOf(const std::vector<Variable>& leaves, std::string_view startAddress);

// -------------------------------------------------- les proprietes ----------
struct PropertyInfo {
    std::string_view name;         // "Length"
    std::string_view type;         // "DINT"
    std::string_view help;         // ce qu'elle dit
    bool             arrayOnly;    // faux : aussi pour une structure (Bytes, Words)
    bool             twoD;         // seulement pour un tableau a deux dimensions
};
[[nodiscard]] const std::vector<PropertyInfo>& properties();
[[nodiscard]] const PropertyInfo* property(std::string_view name) noexcept;   // sans casse
// La valeur de la propriete `name` de ce tableau ou de cette structure.
bool propertyValue(const Aggregate&, std::string_view name, sim::Value& out);

// ------------------------------------------------------ les chemins ---------
// Le type de ce qu'un chemin designe : "Four1" -> "T_Four", "Fours[f]" ->
// "T_Four", "Four1.Vannes" -> "ARRAY[1..3] OF T_Vanne", "Consignes[2]" ->
// "REAL" (les indices ne sont pas evalues) ; vide : pas une variable IHM.
[[nodiscard]] std::string typeOfPath(const Project&, std::string_view path);
// Les membres d'un type IHM (vide : pas une structure).
[[nodiscard]] std::vector<TypeMember> membersOf(const Project&, std::string_view typeName);
// "structure T_Four : 6 membres, 13 mots", "tableau de 10 REAL (0..9), 20 mots".
[[nodiscard]] std::string summary(const Project&, std::string_view type, bool packBools = true);

// ------------------------------------------------- la verification ---------
// Les chemins d'un code ST (script, fonction, expression) qui designent une
// structure ou un tableau IHM, verifies sans l'executer : un membre inconnu, un
// indice constant hors des bornes (Consignes[12]), le mauvais nombre d'indices,
// une propriete ecrite (Consignes.Length := 3). La ligne part de 1.
struct PathProblem {
    int         line{0};
    std::string message;
};
[[nodiscard]] std::vector<PathProblem> pathProblems(const Project&, std::string_view code);

// ------------------------------------------------------ les dossiers --------
// "Ligne/Convoyeur" -> {"Ligne", "Ligne/Convoyeur"} : le dossier et ses parents.
[[nodiscard]] std::vector<std::string> folderChain(std::string_view folder);
// Le dernier nom : "Convoyeur".
[[nodiscard]] std::string folderLeaf(std::string_view folder);
// Le parent : "Ligne" ; vide a la racine.
[[nodiscard]] std::string folderParent(std::string_view folder);
// Un nom de dossier : ni vide, ni '/', ni guillemet ; un chemin : ses noms.
[[nodiscard]] bool validFolder(std::string_view folder, std::string* why = nullptr);
// Tous les dossiers : ceux de la liste et ceux des variables (et leurs parents), tries.
[[nodiscard]] std::vector<std::string> allFolders(const Project&);

} // namespace hmi::types
