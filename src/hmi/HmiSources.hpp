// =============================================================================
//  hmi/HmiSources.hpp - 1.12.7 (lot E de l'audit des objets, § 2.3) : LES TAILLES
//                       DYNAMIQUES, LE JOKER [*]
// -----------------------------------------------------------------------------
//  Pour 8 cuves, on tapait 8 plumes, 8 noms, 8 couleurs. Le JOKER [*] dans une
//  liste en fait un element par indice de sa SOURCE : `Cuves[*].Niveau` donne
//  une plume, une barre, une ligne par cuve ; `Cuves[*].Nom` leurs noms.
//
//  LA SOURCE (ce qui precede [*]) : un tableau de l'IHM (ARRAY[1..8] : les
//  indices 1 a 8 ; a deux dimensions, toutes les cases, ligne par ligne), une
//  LIST ou un VECTOR (0 a n-1, le nombre suit la liste en marche), une MAP
//  (ses cles), une enumeration du projet (ses valeurs : un onglet par valeur).
//  Le moteur la connait (Runtime::jokerSource) ; ce module deplie, sans moteur.
//
//  CE QUI SE DEPLIE (expandObject) :
//   - les listes de FORMULES (les plumes d'une courbe, les valeurs des
//     graphiques, les lignes d'un chronogramme et d'un tableau de variables, la
//     consigne d'un radar) : une formule par indice ; les listes paralleles
//     suivent - un nom a joker se calcule pour chaque element (Cuves[*].Nom), un
//     nom simple prend le libelle de l'element (Cuve 1, Cuve 2...), la couleur
//     tourne dans la palette, l'unite et le format se repetent ;
//   - les listes de NOMS ET DE TEXTES (les vues d'une barre de navigation, les
//     zones d'un resume, les onglets d'un conteneur) : le texte de chaque element
//     (Vues[*] : les noms des vues ; une enumeration : ses textes).
//  Une source inconnue ou vide ne donne aucun element (l'objet dessine « (aucun
//  element) »), jamais une erreur de marche.
//
//  LES REPERES : `$Cuves$[*].Niveau` - le repere varie a la duplication (Dupliquer
//  remplace $Cuves$ par $Ballons$), le joker parcourt la variable ; les $ sont
//  transparents pour trouver la source.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::sources {

// Le premier joker d'un texte : `before` (Cuves, $Cuves$, Lignes[2].Cuves), `after` (.Niveau). Un [*]
// dans une chaine '...' n'en est pas un. nullopt : pas de joker.
struct Joker {
    std::string before, after;
};
[[nodiscard]] std::optional<Joker> jokerOf(std::string_view text);
[[nodiscard]] inline bool hasJoker(std::string_view text) { return jokerOf(text).has_value(); }
// La source d'un joker, sans les $ de ses reperes ni blancs : "Cuves".
[[nodiscard]] std::string sourceName(const Joker&);
// L'element `index` : before + "[" + index + "]" + after - le repere est garde ($Cuves$[2].Niveau).
// Une source enumeration (`enumValue`) : la valeur elle-meme (T_MODE#Auto), suivie de `after`.
[[nodiscard]] std::string itemAt(const Joker&, std::string_view index, bool enumValue = false);

// Ce que le moteur sait d'une source.
struct Source {
    std::vector<std::string> indices;   // 1, 2... ; 'a' (une cle de MAP) ; 1,2 (deux dimensions) ; T_MODE#Auto (une enumeration)
    std::vector<std::string> labels;    // ce que chaque element dit de lui : l'indice, la cle, le texte de la valeur
    bool                     enumeration{false};
};
using SourceFn = std::function<std::optional<Source>(std::string_view name)>;
// Le texte d'une formule (le nom d'un element : Cuves[2].Nom) ; nullopt : illisible.
using TextFn = std::function<std::optional<std::string>(std::string_view formula)>;

// LES SOURCES QUE LE PROJET CONNAIT SANS TOURNER (Generer, les alarmes des instances repetees, l'editeur) :
// une enumeration (ses valeurs), une variable de l'IHM ARRAY (ses bornes, ecrites dans son type). Une LIST,
// un VECTOR ou une MAP n'ont de taille qu'en marche : nullopt (`dynamic` vrai : c'en est une).
[[nodiscard]] SourceFn projectSources(const Project& p);
// Ce que Generer dit d'une source : "" si elle convient ; sinon la phrase. `first` : un indice pour verifier
// la formule d'un element type (la borne basse d'un ARRAY, 0 d'une LIST, la premiere valeur d'une enumeration).
[[nodiscard]] std::string sourceProblem(const Project& p, const View* view, std::string_view name, std::string& first);

// Les cles que le joker deplie pour ce genre d'objet : des formules (variables...) ou des textes (views...).
[[nodiscard]] bool expandsFormulas(Kind k, std::string_view key) noexcept;
[[nodiscard]] bool expandsTexts(Kind k, std::string_view key) noexcept;
// L'objet a-t-il un joker a deplier (une valeur fixe d'une de ces cles) ?
[[nodiscard]] bool objectHasJoker(const Object& o);
[[nodiscard]] bool viewHasJoker(const View& v);
// Les sources que l'objet cite (pour la signature : le moteur refait le depliage quand l'une change).
[[nodiscard]] std::vector<std::string> sourcesOf(const Object& o);

// Deplie les jokers de l'objet ; faux : il n'y en avait pas (l'objet reste tel quel).
bool expandObject(Object& o, const SourceFn& source, const TextFn& text);
// Tous les objets d'une vue ; le nombre d'objets deplies.
std::size_t expandView(View& v, const SourceFn& source, const TextFn& text);

// Pour Generer : la formule d'un element type (le joker remplace par `index`, le premier indice).
[[nodiscard]] std::string sampleFormula(std::string_view text, std::string_view index = "0");
// Chaque [*] (hors chaines) remplace par [index] : les arguments d'une instance repetee (Cuve := Cuves[*]).
[[nodiscard]] std::string replaceJokers(std::string_view text, std::string_view index);

// ---- « REPETER DEPUIS » : une instance de symbole par element (§ 3.8) ---------------------
//  Une instance dont « Repeter depuis » nomme une source (Cuves) se pose une fois par element :
//  ses arguments qui citent [*] (Cuve := Cuves[*]) recoivent l'element (Cuves[3]) ; les copies se
//  suivent sur l'axe choisi (horizontal, vertical), a l'ecart donne. La premiere garde le nom et
//  l'identifiant de l'instance (ce qui la cite marche toujours) ; les autres : Nom_2, Nom_3...
//  Une source vide : aucune instance. Rien a repeter : faux.
inline constexpr std::string_view kRepeatFromKey = "repeatFrom";
inline constexpr std::string_view kRepeatAxisKey = "repeatAxis";
inline constexpr std::string_view kRepeatGapKey = "repeatGap";
[[nodiscard]] bool repeats(const Object& o);
bool repeatInstances(View& v, const SourceFn& source);

} // namespace hmi::sources
