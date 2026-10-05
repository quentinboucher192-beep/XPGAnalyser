#pragma once
// 1.10.2 (chantier D) : DUPLIQUER AVEC DES REPERES.
//
// Un objet peut porter des REPERES : `$Vanne$` dans une expression
// (`=$Vanne$.OUV`), un texte, une action, une alarme... "Dupliquer..." fait
// N copies en remplacant chaque repere par la valeur de sa colonne (la liste
// avant / apres), SUIT les indices litteraux des tableaux (`V[0]` -> `V[1]`,
// `V[2]`...) et POSE les copies sur X, sur Y ou en grille.
//
// La syntaxe : `$Nom$`, Nom = une lettre ou `_`, puis lettres, chiffres, `_`,
// AU MOINS DEUX caracteres (les echappements des chaines ST `$N`, `$R`, `$0D`
// gardent leur sens) ; `$$` ecrit un vrai `$`. Les noms se comparent sans la
// casse et s'ecrivent comme la premiere fois.
//
// 1.11 (chantier REP, demande du client du 03/10) : un repere entoure un MORCEAU
// d'expression, n'importe lequel (`$V[1].Ouv$`, `$V[0]$.Nom`, `$V[0].Pos > 10$`),
// lu par hmi::markers (HmiMarkers.hpp) ; ses `$` sont transparents pour le calcul.
// Dupliquer... preremplit la colonne d'un morceau a indices litteraux (copie k :
// ses indices + k), les copies GARDENT les `$` autour du morceau remplace (elles
// se dupliquent a leur tour) et ce qui n'est pas marque ne varie pas (un objet a
// repere ne suit plus ses indices hors repere).
//
// Ce module est le moteur, sans ecran : la fenetre (src/app/hmi/) s'en sert, et
// ses essais sont dans hmi_test. `apply` fait tout dans le projet ; l'appelant
// l'enveloppe dans UNE commande (hmi::changeProject) : Ctrl+Z retire tout.

#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::dup {

// ---------------------------------------------------------------- reperes ---
struct MarkerUse {
    std::string name;            // le morceau, tel qu'ecrit (sans les $) : "Vanne", "V[1].Ouv"
    std::size_t at{0};           // le premier $
    std::size_t length{0};       // `$Nom$` entier
};
[[nodiscard]] bool validMarkerName(std::string_view name) noexcept;   // 1.11 : markers::validContent
// Les reperes d'un texte, dans l'ordre ; `$$` n'en est pas un (et ne commence rien).
// `expression` : un `$` entre apostrophes n'est pas un repere (un texte : hors de ses trous {...}, si).
[[nodiscard]] std::vector<MarkerUse> markersIn(std::string_view text, bool expression = true);
[[nodiscard]] bool hasMarker(std::string_view text, bool expression = true);
// La cle d'un repere (son nom en majuscules) : celle des valeurs de `replaceMarkers`.
[[nodiscard]] std::string markerKey(std::string_view name);
// Le texte, chaque repere remplace par sa valeur (cle : markerKey) ; un repere
// sans valeur, ou de valeur vide, reste tel quel. `count` : les remplacements.
// 1.11 : la valeur garde ses `$` (`$V[1].Ouv$` -> `$V[2].Ouv$`) ; une valeur egale
// au morceau ne compte pas.
[[nodiscard]] std::string replaceMarkers(std::string_view text, const std::map<std::string, std::string>& values,
                                         std::size_t* count = nullptr, std::map<std::string, std::size_t>* perMarker = nullptr,
                                         bool expression = true);

// ---------------------------------------------------------------- indices ---
// `Nom[entier]` hors repere : `Armoires[0]`, `V[0].Pos`, `Four1.Vannes[2]`. Un
// indice non litteral (`Tab[k]`) n'en est pas un ; `M[1, 2]` non plus (a faire).
// 1.11 : ceux d'un repere ($V[0]$, $V[1].Ouv$) n'en sont pas (le repere varie seul).
struct IndexUse {
    std::string path;            // ce qui precede `[` : "Armoires", "Four1.Vannes"
    long long   value{0};
    std::size_t at{0};           // l'entier (son signe compris), dans le texte
    std::size_t length{0};
};
[[nodiscard]] std::vector<IndexUse> indicesIn(std::string_view text, bool expression = true);
// La cle d'un indice : "ARMOIRES[0]" (chemin en majuscules).
[[nodiscard]] std::string indexKey(std::string_view path, long long value);
// Le texte, chaque indice dont la cle a une nouvelle valeur la recoit.
[[nodiscard]] std::string followIndices(std::string_view text, const std::map<std::string, long long>& values,
                                        std::size_t* count = nullptr);
// 1.11 : le morceau d'un repere, ses indices litteraux decales de `by` :
// ("V[1].Ouv", 2) -> "V[3].Ouv" ; ("V[0].Pos > 10", 1) -> "V[1].Pos > 10" ; sans indice : tel quel.
[[nodiscard]] std::string shiftIndices(std::string_view piece, long long by);
[[nodiscard]] bool hasIndices(std::string_view piece);

// Les bornes d'un tableau d'apres son chemin ("Armoires" -> 0..1) ; nullopt : inconnues.
using Bounds   = std::pair<long long, long long>;
using BoundsFn = std::function<std::optional<Bounds>(std::string_view path)>;
// Les bornes lues dans les variables IHM du projet (leur type `ARRAY[a..b] OF ...`).
[[nodiscard]] BoundsFn projectBounds(const Project&);
// 1.10.4 : les tableaux du projet (variables IHM a une dimension), dans l'ordre :
// ce que propose Remplir > Tableau... (nom, bornes).
[[nodiscard]] std::vector<std::pair<std::string, Bounds>> projectArrays(const Project&);

// ---- 1.11.2 (SYM, decision 240 ; la capture du client « Value[0] n'existe pas ») ----
// Dans l'editeur d'un SYMBOLE (une vue de role "symbole"), ses PARAMETRES sont des
// chemins qui existent : `Name`, `Value`, `Value[0]`..`Value[9]` pour un parametre
// `Value : ARRAY[0..9] OF UINT` (ses bornes sont celles du parametre : `Value[10]`
// reste rouge, « hors des bornes de Value (0..9) », comme pour une variable), et les
// membres d'un parametre de type structure quand `members` les donne (type -> (nom,
// type) ; sans rien : rien de plus). Hors d'un symbole, ou sans parametre, les
// enveloppes rendent exactement ce que rend l'interieur (`inner`) : rien ne change.
// Les parametres passent AVANT l'interieur : dans son symbole, `Value` est le
// parametre, meme si le projet a une variable du meme nom.
// Le chemin existe-t-il ("Name", "Value[3]", "Armoire.ana.PT1") ? Un parametre, puis `inner`.
using TypeMembers = std::function<std::vector<std::pair<std::string, std::string>>(std::string_view type)>;
using PathExists  = std::function<bool(std::string_view path)>;
[[nodiscard]] PathExists symbolParamExists(const View& symbol, PathExists inner = {}, TypeMembers members = {});
// Les bornes ("Value" -> 0..9 ; "Armoire.tab" si le membre est un tableau) : un parametre, puis `inner`.
[[nodiscard]] BoundsFn symbolParamBounds(const View& symbol, BoundsFn inner = {}, TypeMembers members = {});
// Les parametres tableaux (a une dimension) du symbole, dans l'ordre : ce que
// Remplir > Tableau... propose en tete, dans un symbole (nom, bornes). Vide ailleurs.
[[nodiscard]] std::vector<std::pair<std::string, Bounds>> symbolParamArrays(const View& symbol);
// Les noms des parametres du symbole (pour « veux-tu dire ») ; vide hors d'un symbole.
[[nodiscard]] std::vector<std::string> symbolParamNames(const View& symbol);

// 1.10.4 : un element de tableau ecrit dans une case - "V[3]", "V[3].Ouv",
// "Four1.Vannes[2]" : le tableau, l'indice (tel qu'ecrit), sa valeur, la suite.
// nullopt : pas un element (un nom, `M[1, 2]`, `Tab[k]`).
struct ElementRef {
    std::string array;           // "V", "Four1.Vannes"
    std::string index;           // "3", "02", "-1" (tel qu'ecrit)
    long long   value{0};
    std::string rest;            // ".Ouv" ; "" : l'element lui-meme
    bool        leadingZero{false};   // "02" : refuse, "veux-tu dire V[2] ?"
    [[nodiscard]] std::string canonical() const { return array + "[" + std::to_string(value) + "]" + rest; }
};
[[nodiscard]] std::optional<ElementRef> elementRef(std::string_view value);

// ------------------------------------------------------- ce qu'on a trouve ---
// Un endroit : un champ d'un objet de la selection (ou d'un de ses elements).
struct Spot {
    Id          object{kNoId};
    std::string objectName;
    std::string where;           // "text", "fill (expression)", "action 1 (valeur)", "alarme Defaut (message)"
    std::string text;            // le champ entier
    bool        expression{false};
};
struct MarkerInfo {
    std::string       name;      // ecrit comme la premiere fois
    std::size_t       uses{0};   // les occurrences
    bool              variable{false};   // suivi de `.`, apres `:=` ou dans une expression ; sinon un texte
    std::vector<Spot> spots;     // un par champ
    std::vector<std::string> members;    // ce qui suit le repere : "OUV", "DEF", "sorties.V2" (sans doublon)
};
struct IndexInfo {
    std::string           path;
    long long             value{0};
    std::size_t           uses{0};
    std::optional<Bounds> bounds;
    std::vector<Spot>     spots;
    [[nodiscard]] std::string label() const { return path + "[" + std::to_string(value) + "]"; }
};
// Suivre par defaut : les `copies` copies (value + 1 ... value + copies*step)
// tiennent dans les bornes connues.
[[nodiscard]] bool followFits(const IndexInfo&, int copies, long long step = 1);

struct Scan {
    std::vector<Id>         units;      // la selection, objets de premier niveau
    std::vector<Id>         objects;    // les unites et leurs elements
    Box                     box;        // la boite de la selection (pour la pose)
    std::vector<MarkerInfo> markers;    // dans l'ordre de la premiere rencontre
    std::vector<IndexInfo>  indices;
    [[nodiscard]] const MarkerInfo* marker(std::string_view name) const;
    [[nodiscard]] std::size_t       markerUses() const;
};
[[nodiscard]] Scan scan(const View&, const std::vector<Id>& selection, const BoundsFn& bounds = {});

// Les champs de texte d'un objet (propriete, expression, actions, alarmes) :
// `fn(champ, ou, estUneExpression)` ; la version non const peut les changer.
void forEachField(const Object&, const std::function<void(const std::string&, const std::string&, bool)>& fn);
void forEachField(Object&, const std::function<void(std::string&, const std::string&, bool)>& fn);
// Les reperes que porte un objet et ses elements (pour Compiler et l'inspecteur).
[[nodiscard]] std::vector<MarkerInfo> markersOf(const View&, Id object);
// Les reperes de l'objet seul (sans ses elements).
[[nodiscard]] std::vector<MarkerInfo> ownMarkers(const Object&);

// "MODELES D'OBJETS" : un dossier de vues du projet. Un objet a reperes y est
// range comme modele ; le glisser sur une vue ouvre "Dupliquer...".
inline constexpr std::string_view kTemplatesFolder = "Mod\xC3\xA8les d'objets";
[[nodiscard]] bool isTemplatesFolder(std::string_view folder) noexcept;   // le dossier ou un sous-dossier
// 1.11 (REP) : plus de "repere non remplace" - un repere se calcule comme le reste
// (ses `$` sont transparents), Compiler n'avertit plus, Generer ne bloque plus.
// Seul l'ancien `$Vanne$` dont le nom n'est pas une variable garde une erreur :
// celle de l'analyse ("variable inexistante : Vanne"), suivie de cette phrase.
inline constexpr std::string_view kMarkerHint = "un rep\xC3\xA8re se remplit par Dupliquer\xE2\x80\xA6 (Ctrl+D)";
// " - un repere se remplit par Dupliquer... (Ctrl+D)" si `unknown` (le nom que
// l'analyse ne connait pas) est la racine d'un repere de `text` ; vide sinon.
[[nodiscard]] std::string markerHint(std::string_view text, std::string_view unknown, bool expression = true);
// Un constat qui porte cette phrase : "Remplacer..." de Compiler l'ouvre.
[[nodiscard]] bool isUnreplacedIssueMessage(std::string_view message) noexcept;
// Le libelle d'un champ dans l'inspecteur : "value (expression)" -> "Valeur",
// "action 1 (valeur)" -> "action 1", "alarme Defaut (message)" -> "alarme Defaut".
[[nodiscard]] std::string fieldLabel(std::string_view where);

// ------------------------------------------------------------ remplissages ---
// Une valeur par ligne (une colonne d'Excel) : une tabulation coupe la ligne
// (la premiere colonne compte), les lignes vides de la fin sont ignorees.
[[nodiscard]] std::vector<std::string> pastedList(std::string_view clipboard);
// Une serie : `pattern` avec `{n}` (chaque `{n}` recoit le meme numero) ; n de
// `from` a `to`, pas `step` ; la largeur de `from` fixe les zeros ("01" -> 01, 02...).
// Sans `{n}`, le numero s'ajoute a la fin.
[[nodiscard]] std::vector<std::string> series(std::string_view pattern, std::string_view from, long long to, long long step = 1);
// `count` valeurs de la serie (pour une serie "vivante" qu'on prolonge).
[[nodiscard]] std::vector<std::string> seriesN(std::string_view pattern, std::string_view from, std::size_t count, long long step = 1);
// Les elements d'un tableau : `Armoires[0].sorties.V2`, `Armoires[1].sorties.V2`...
[[nodiscard]] std::vector<std::string> arrayElements(std::string_view array, long long from, long long to,
                                                     std::string_view suffix = {});
// Les instances d'un type : les variables IHM de ce type (sans la casse), dans
// l'ordre de declaration ; `plcVariables` : celles de l'automate (nom, type), a la suite.
[[nodiscard]] std::vector<std::string> typeInstances(const Project&, std::string_view type,
                                                     const std::vector<std::pair<std::string, std::string>>& plcVariables = {});
// Les instances posees d'un symbole (Kind::SymbolInstance, propriete "symbol"),
// vue par vue dans l'ordre du projet ; `qualified` : "Vue.Instance".
[[nodiscard]] std::vector<std::string> symbolInstances(const Project&, std::string_view symbol, bool qualified = false);
// "veux-tu dire ... ?" : les candidats a une distance d'edition <= 2 (sans la
// casse), une permutation de deux caracteres voisins comptant 1 ; les plus proches d'abord.
[[nodiscard]] std::vector<std::string> didYouMean(std::string_view value, const std::vector<std::string>& candidates,
                                                  std::size_t max = 3);

// ------------------------------------------------------------------- pose ---
enum class Axis : std::uint8_t { X, Y, Grid };
struct Layout {
    Axis   axis{Axis::X};
    int    columns{4};         // la grille : l'original compte pour une case
    double spacing{16};        // en px, entre deux boites
};
struct Offset { double dx{0}, dy{0}; };
// Le decalage de chaque copie (1..copies) par rapport a l'original.
[[nodiscard]] std::vector<Offset> offsets(const Box& selection, int copies, const Layout&);
// Le pas mesure : la largeur (X, grille) ou la hauteur (Y) de la selection + l'espacement.
[[nodiscard]] double stepOf(const Box& selection, const Layout&);
// Les copies (1..copies) qui sortent de la vue.
[[nodiscard]] std::vector<int> overflowing(const View&, const Box& selection, int copies, const Layout&);
// Le plus grand espacement >= 0 (entier) ou tout tient ; nullopt : rien ne tient.
[[nodiscard]] std::optional<double> fittingSpacing(const View&, const Box& selection, int copies, const Layout&);
// Les copies qui chevauchent un autre objet de la vue (hors selection).
[[nodiscard]] std::vector<int> overlapping(const View&, const std::vector<Id>& selection, int copies, const Layout&);

// --------------------------------------------------------------- le plan ---
// Une ligne du tableau des copies ; la ligne 0 est l'original.
struct Row {
    std::string                        name;      // vide : un nom libre (comme Dupliquer tel quel)
    std::map<std::string, std::string> markers;   // markerKey -> valeur ; vide : on garde le repere
    std::map<std::string, long long>   indices;   // indexKey -> nouvelle valeur (seulement les suivis)
};
struct Plan {
    std::vector<Id>  selection;
    std::vector<Row> rows;                  // rows[0] : l'original ; une copie par ligne suivante
    bool             replaceOriginal{true}; // "Remplacer aussi dans l'original"
    Layout           layout;
    [[nodiscard]] int copies() const noexcept { return rows.empty() ? 0 : static_cast<int>(rows.size()) - 1; }
};
// 1.11 (REP-4, decision 72 du 03/10) : les indices HORS REPERE se suivent-ils par
// defaut ? Oui si aucun repere de la selection ne porte d'indice ($Vanne$, ou pas
// de repere : les projets de la 1.10.x se dupliquent comme avant) ; non des qu'un
// repere en porte un ($V[1].Ouv$ : seul le marque varie). La case "Suivre V[0]" de
// la fenetre en part, et le client la change.
[[nodiscard]] bool followByDefault(const Scan&);
// Le plan de depart d'un scan : `copies` lignes, noms en serie, indices suivis
// si ca tient (sinon gardes), reperes vides.
// 1.11 : un repere a indices litteraux est prerempli (ligne 0 : le morceau ;
// copie k : shiftIndices(morceau, k)) ; les indices hors repere ne se suivent que
// si followByDefault (sinon, ce qui n'est pas marque ne varie pas).
[[nodiscard]] Plan defaultPlan(const View&, const Scan&, int copies);
// 1.10.4 (le cas du client) : l'objet suit V[0] ailleurs (Libelle, Ouverture...)
// et SON repere de type variable est vide - la colonne se preremplit avec V[0],
// V[1], V[2]... (ligne 0 : l'original). Le repere, le tableau, l'indice de
// l'original ; nullopt : rien a proposer (aucun indice suivi d'un tableau aux
// bornes connues, ou plusieurs reperes de type variable vides).
struct Prefill {
    std::string marker;          // tel qu'ecrit (sans les $)
    std::string array;           // "V"
    long long   from{0};         // l'indice de l'original
    std::string rest{};          // 1.11 : la suite du morceau (".Ouv" de $V[1].Ouv$), gardee par Tableau...
};
[[nodiscard]] std::optional<Prefill> prefill(const Scan&, const Plan&);
// Remplir > Tableau... : `rows` cases, `array[from]`, `array[from + 1]`...
[[nodiscard]] std::vector<std::string> arrayColumn(std::string_view array, long long from, std::size_t rows);

// -------------------------------------------------------- les cases ---
// Une case du tableau : vide (ambre ; bloque sauf colonne "garder le repere"),
// rouge (ne mene a rien : "veux-tu dire ... ?"), un indice hors bornes, un nom
// invalide ou deja pris. "Dupliquer" reste gris tant qu'une case bloque.
enum class CellState : std::uint8_t { Ok, Empty, Unknown, OutOfBounds, BadName };
struct CellCheck {
    int                      row{0};
    std::string              column;       // "Nom", "$Vanne$", "V[0]"
    CellState                state{CellState::Ok};
    std::string              message;
    std::vector<std::string> suggestions;  // "veux-tu dire ... ?"
    bool                     blocks{true};
};
struct Validation {
    std::vector<CellCheck> cells;          // les cases a signaler (pas les bonnes)
    [[nodiscard]] bool blocked() const noexcept;
    [[nodiscard]] std::size_t count(CellState) const noexcept;
};
// Le chemin existe-t-il ("V104", "V104.OUV", "Armoires[1].sorties.V2") ? PathExists :
// declare plus haut (avec les bornes), depuis la 1.11.2 (SYM).
struct ValidateOptions {
    PathExists               exists;        // vide : les valeurs ne se verifient pas
    BoundsFn                 bounds;        // 1.10.4 : V[64] -> "hors des bornes de V (0..63)" ; vide : non verifie
    std::vector<std::string> candidates;    // pour "veux-tu dire" (les variables du projet et de l'automate)
    std::vector<std::string> keepColumns;   // les reperes (markerKey) regles sur "garder le repere"
};
[[nodiscard]] Validation validate(const View&, const Scan&, const Plan&, const ValidateOptions& = {});

struct Result {
    std::vector<Id>                    created;          // les racines des copies (la nouvelle selection)
    std::size_t                        objects{0};       // les objets crees (elements compris)
    std::size_t                        markersReplaced{0};
    std::map<std::string, std::size_t> perMarker;        // nom ecrit -> remplacements
    std::size_t                        indicesFollowed{0};
    bool                               originalChanged{false};
};
// Fait les copies dans la vue (et change l'original si demande). A envelopper
// dans une seule commande.
Result apply(Project&, View&, const Plan&);
// "7 objets crees, 64 reperes remplaces ($Vanne$ 40, $Repere$ 24), 7 indices suivis"
[[nodiscard]] std::string summary(const Result&);

} // namespace hmi::dup
