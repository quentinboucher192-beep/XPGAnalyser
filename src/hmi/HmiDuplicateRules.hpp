#pragma once
// =============================================================================
//  hmi/HmiDuplicateRules.hpp - 1.50.0 (maquette, planche 04) : DUPLIQUER POUR 500 REPERES
// -----------------------------------------------------------------------------
//  Le client (retour 1.50) : « dans dupliquer si j'ai bcp de choses a dupliquer c'est tronque, mieux gerer
//  l'organisation pour que meme si j'ai 500 placeholders ce soit facile a faire, optimiser les remplacements multi
//  profondeur, pouvoir faire varier que certaines profondeurs, utiliser le meme principe que les formules sur les
//  adressages %MW %M ». Ses reponses (12/11) : i = toujours la 1re profondeur, j la 2e, k la 3e ; une adresse ou un
//  nom deja pris AVERTIT (pas de blocage) ; un indice hors des bornes BLOQUE.
//
//  Le principe, celui de « Modifier l'adresse dans toutes les cases... » (%MW(1000 + 10 * i)) :
//   - des COMPTEURS rangent les copies : une suite (n de 1 a N) ou une grille i x j (x k), j variant le plus vite ;
//     la place 0 est l'original ;
//   - les reperes se rangent en GROUPES : une profondeur d'indice (API.Armoires[.], ...Departs[.], ...Voies[.] - le
//     chemin, ses indices d'avant en [*]), les adresses (%MW, %M), les textes, les variables, les noms des objets ;
//   - chaque groupe a UNE REGLE : fixe (garder), ou une formule de i, j, k, n (le rang), o (la valeur de l'original) -
//     le calcul de hmi::types (formulaValue, namedFormulaAddress : + - * / MOD, parentheses) ; un texte : {i+1},
//     {j:02} (deux chiffres) ; un repere peut avoir la sienne (« a part ») ou ses cases (les Remplir d'avant) ;
//   - le plan qui en sort est celui d'avant (hmi::dup::Plan) : validate et apply ne changent pas, Ctrl+Z retire tout.
//  Sans rien regler (une suite), les regles d'office redonnent EXACTEMENT defaultPlan : les indices hors repere
//  suivis si aucun repere n'en porte et que ca tient (o + n), les morceaux a indices decales (o + n), les autres
//  reperes a remplir, les noms en serie.
// =============================================================================

#include "HmiDuplicate.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::dup {

// ------------------------------------------------------------- compteurs ---
enum class CountMode : std::uint8_t { Series, Grid };
struct Range {
    long long from{0}, to{0};
    [[nodiscard]] long long size() const noexcept { return to >= from ? to - from + 1 : 0; }
};
inline constexpr int kMaxCopies = 500;   // comme avant (setCopies)
struct Counters {
    CountMode          mode{CountMode::Series};
    int                copies{3};   // Series : n = 1..copies (0 : l'original)
    std::vector<Range> grid;        // Grid : i, j (, k) - 1 a 3 compteurs
    // Les places, l'original compris (Series : copies + 1 ; Grid : le produit des tailles, 0 si un compteur est vide).
    [[nodiscard]] long long places() const noexcept;
};
// La place `p` (0 : l'original) : n = p ; une suite : i = n ; une grille : le DERNIER compteur varie le plus vite.
struct Place {
    long long n{0}, i{0}, j{0}, k{0};
};
[[nodiscard]] Place placeAt(const Counters&, long long p);
// Ce que dit la place : "i = 3, j = 7" (une suite : "n = 5").
[[nodiscard]] std::string placeText(const Counters&, const Place&);

// -------------------------------------------------------------- formules ---
// Les noms d'une formule : i, j, k (les compteurs : ceux de la grille ; une suite : i = n), n, o.
struct Vars {
    Place     at;
    long long o{0};
};
[[nodiscard]] std::vector<std::pair<std::string, long long>> namesOf(const Counters&, const Vars&);
// Un entier : "o + n", "i", "5 + 4 * i" ; faux et `why` (un nom inconnu, une division par zero).
bool evalInteger(std::string_view formula, const Counters&, const Vars&, long long& out, std::string* why = nullptr);
// Une adresse ecrite : "%MW1000" -> "%MW", 1000, "" ; "%MW10.3" -> "%MW", 10, ".3" ; nullopt : pas une adresse.
struct AddressText {
    std::string area;     // "%MW", "%M", "%IW"
    long long   number{0};
    std::string rest;     // ".3"
};
[[nodiscard]] std::optional<AddressText> addressOf(std::string_view text);
// Une adresse par formule : "%MW(1000 + 10 * i + j)" ; sans zone, "(o + n)" garde celle de l'original ("%M200" ->
// "%M201") ; vide et `why` : la formule est fausse.
[[nodiscard]] std::string evalAddress(std::string_view formula, std::string_view original, const Counters&, const Vars&,
                                      std::string* why = nullptr);
// Un texte : "'Depart {i+1}.{j+1}'" -> "Depart 4.8" ; "{j:02}" sur deux chiffres ; les apostrophes du tour sont
// facultatives ; {{ et }} : une accolade. Vide et `why` : une formule fausse.
[[nodiscard]] std::string evalText(std::string_view tmpl, const Counters&, const Vars&, std::string* why = nullptr);

// -------------------------------------------------------------- groupes ---
enum class GroupKind : std::uint8_t { Index, Address, Text, Variable, Name };
struct Group {
    GroupKind                kind{GroupKind::Index};
    std::string              key;         // la cle de sa regle : "I:API.ARMOIRES[*].DEPARTS" (dim), "A", "T", "V", "N"
    std::string              label;       // "API.Armoires[\xC2\xB7].Departs[\xC2\xB7]" ; "Adresses %MW, %M"...
    std::string              tail;        // le dernier morceau : "Departs[\xC2\xB7]" (la colonne, en court)
    int                      depth{0};    // Index : 1, 2, 3... (M[i, j] : 1 et 2)
    int                      dim{0}, dims{1};
    bool                     marked{true};   // Index : dans un repere ; faux : hors repere (la case « Suivre » d'avant)
    std::vector<long long>   values;      // Index : les valeurs de l'original (triees, sans doublon)
    std::string              original;    // "5", "0..3" ; "%MW1000" ; "Depart 1.1" ; "V_101"
    std::optional<Bounds>    bounds;
    std::vector<std::size_t> markers;     // les reperes (rangs dans Scan::markers)
    std::vector<std::size_t> indices;     // hors repere : les IndexInfo (rangs dans Scan::indices)
    std::size_t              spots{0};    // les endroits
};
// Un repere tel que les regles le voient.
struct MarkerModel {
    GroupKind                kind{GroupKind::Variable};
    std::size_t              group{0};    // son groupe (un repere a indices : sa profondeur la plus grande)
    struct Slot {
        std::size_t group{0};
        IndexUse    use;
    };
    std::vector<Slot>        slots;       // Index : chaque indice litteral du morceau, dans l'ordre
    std::optional<AddressText> address;
};
struct Model {
    std::vector<Group>       groups;      // les profondeurs (1, 2, 3...), puis adresses, textes, variables, noms
    std::vector<MarkerModel> markers;     // dans l'ordre de Scan::markers
    std::vector<std::size_t> indexGroup;  // Scan::indices[i] -> son groupe
    [[nodiscard]] const Group* group(std::string_view key) const;
    [[nodiscard]] int          groupIndex(std::string_view key) const;
};
// La cle d'une profondeur : le chemin et ses indices d'avant en [*] ("API.Armoires[0].Departs" -> "API.ARMOIRES[*].DEPARTS").
[[nodiscard]] std::string depthKey(std::string_view path, int dim, int dims, bool marked);
// `bounds` : celles d'une profondeur dans un repere (Scan ne les donne qu'aux indices hors repere).
[[nodiscard]] Model modelOf(const Scan&, std::string_view originalName, const BoundsFn& bounds = {});

// --------------------------------------------------------------- regles ---
enum class RuleKind : std::uint8_t {
    Fixed,     // garder : l'indice, l'adresse, le repere ; un nom : un nom libre
    Formula,   // la formule
    Empty,     // a remplir (les cases : Serie, Tableau..., Coller, a la main)
    Auto       // les noms : la serie d'aujourd'hui (V_101 -> V_102)
};
struct Rule {
    RuleKind    kind{RuleKind::Fixed};
    std::string formula;
};
// Un repere « a part ».
enum class OwnKind : std::uint8_t {
    Group,     // comme sa profondeur (son groupe)
    Formula,   // sa formule (un repere a indices : pour la profondeur `group`)
    Cells,     // ses cases (Serie, Tableau..., Coller (Excel), a la main) : les regles n'y touchent pas
    Keep       // garder le repere
};
struct Own {
    OwnKind     kind{OwnKind::Group};
    std::string formula;
    std::string group;   // OwnKind::Formula d'un repere a indices : la cle de la profondeur visee
    std::string how;     // ce que la liste en dit : "S\xC3\xA9rie V1{n}", "Coller (Excel)", "\xC3\xA0 la main"
};
struct Rules {
    Counters                    counters;
    std::map<std::string, Rule> groups;   // Group::key -> sa regle
    std::map<std::string, Own>  own;      // markerKey -> a part (absent : comme son groupe)
    bool                        namesByHand{false};   // les noms tapes (la colonne Nom) : les regles n'y touchent pas
    [[nodiscard]] const Rule& rule(const Group&) const;
    [[nodiscard]] Own         ownOf(std::string_view markerKey) const;
};
// Les regles d'office. Une suite : celles d'aujourd'hui (defaultPlan). Une grille : la profondeur d prend le compteur
// d (i, j, k), les autres restent fixes ; les adresses : leur zone (o + n) ; les textes, les variables : a remplir.
[[nodiscard]] Rules defaultRules(const Scan&, const Model&, const Counters&);
// « Preremplir depuis les bornes » : les profondeurs 1, 2 (, 3) qui varient - de l'original a la borne haute.
[[nodiscard]] std::vector<Range> gridFromBounds(const Model&, int counters);

// Une regle fausse (une formule) : le groupe ou le repere, la premiere place, la raison.
struct RuleError {
    std::string where;    // "API.Armoires[\xC2\xB7].Departs[\xC2\xB7]", "$%M200$"
    std::string why;
    long long   place{0};
};
// Les lignes du plan d'apres les regles : `plan.rows` mis a `places` lignes ; les noms (sauf namesByHand), les indices
// hors repere suivis, chaque repere (sauf ses cases : OwnKind::Cells - gardees, une ligne ajoutee part vide).
// `keep` recoit les reperes a garder (Fixed, Keep). `names` : la serie d'aujourd'hui (seriesNames), si elle sert.
void computeRows(const Scan&, const Model&, const Rules&, const std::vector<std::string>& names, Plan& plan,
                 std::vector<std::string>* keep = nullptr, std::vector<RuleError>* errors = nullptr);
// La valeur d'un repere a une place d'apres son groupe (ou sa regle a part) - pour remplir une case vide.
[[nodiscard]] std::string ruleValue(const Scan&, const Model&, const Rules&, std::size_t marker, long long place, std::string* why = nullptr);

// --------------------------------------------------------- les adresses ---
// Les adresses des variables du projet (et de leurs cases) : "%MW1030" (en majuscules) -> "Compteurs[0]".
[[nodiscard]] std::map<std::string, std::string> projectAddresses(const Project&);
// Une adresse deja prise (par une variable, ou par une autre copie) : un AVERTISSEMENT (comme « Modifier
// l'adresse... ») - CellState::Ok, blocks faux, le message dit par qui.
[[nodiscard]] std::vector<CellCheck> addressWarnings(const Scan&, const Model&, const Plan&,
                                                     const std::map<std::string, std::string>& owners);

// ------------------------------------------------------------ l'apercu ---
// Les remplacements que fera le plan : pour chaque copie, chaque repere qui change, ses occurrences.
[[nodiscard]] std::size_t replacements(const Scan&, const Plan&);

} // namespace hmi::dup
