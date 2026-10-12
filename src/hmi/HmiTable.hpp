// =============================================================================
//  hmi/HmiTable.hpp - 1.12.13 : LE TABLEAU, UN MODE A LA FOIS
// -----------------------------------------------------------------------------
//  Avant, l'inspecteur du Tableau melait trois facons de le remplir (des cases
//  fixes, un fichier externe, « Lignes depuis ») ; une priorite cachee decidait
//  laquelle se voyait. Maintenant :
//
//  UN MODE, COCHE. `tableMode` : « cases fixes », « lignes dynamiques » ou
//  « fichier externe ». Absent (un tableau d'avant) : celui que le moteur prenait
//  deja - Lignes depuis, sinon le fichier, sinon les cases ; il se dessine donc
//  a l'identique. Les reglages des autres modes restent dans le fichier, « en
//  reserve » : l'inspecteur le dit, et sait les effacer (clearMode).
//
//  LES COLONNES DYNAMIQUES. `dynColumns` : une ligne par colonne, ses champs
//  separes par des tabulations - titre, contenu, format, unite, largeur,
//  alignement, acces (R ou W), niveau, couleur, formule de couleur. Le contenu
//  d'une case, pour chaque ligne :
//    [*]           l'element de la ligne (Cuves[2]) : [*].Niveau, [*].Vanne.Ouverture
//    [*].Cle       une MAP : la cle ; [*].Valeur sa valeur (Prix[[*].Cle] : une autre MAP, par la cle)
//    [*].Membre    une structure seule : le nom du membre ; [*].Valeur sa valeur
//    [*].Nom       une enumeration : le nom ; [*].Valeur son nombre ; [*].Texte son texte
//    [#]           le numero de la ligne (1, 2...)
//    Liste[*]      sans « Lignes depuis » : chaque colonne a sa liste ; la plus longue fait les
//                  lignes, une case sans valeur montre « ... »
//    {...}  =...   un texte a trous, une formule ; sinon un texte fixe.
//  Absent : le tableau d'avant (Lignes depuis + Colonnes de la source), tel quel ; Contenu
//  le reprend en colonnes (legacyColumns) a la premiere retouche.
//
//  R/W. Une colonne W se saisit en marche : son contenu est un chemin (ou le seul trou d'un
//  texte) et s'ecrit pour la ligne (writePathOf : Cuves[2].Consigne, Stock['vis'],
//  Debits[3]). Une case fixe d'une colonne W qui montre une seule variable ({PA:0.00} bar,
//  =PA) s'ecrit aussi (fixedWritePath) ; `colAccess` : R;W;R.
//
//  LE MOTEUR (Runtime::tableModel) evalue chaque contenu compile une fois (compileContent :
//  [*] devient __TABLE_ELT__, [#] __TABLE_ROW__...) dans une portee par ligne : un alias
//  vers l'element (un tableau IHM), l'objet en place (une LIST, une MAP), des valeurs (la
//  cle, le numero). L'inspecteur, Contenu et Generer lisent l'analyse sans moteur
//  (analyzeSource, analyzeColumn) : le type attendu, le type recu, la faute et sa correction.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::table {

// ---- le mode ---------------------------------------------------------------------------
enum class Mode : std::uint8_t { Fixed, Rows, File };
inline constexpr Mode kModes[] = {Mode::Fixed, Mode::Rows, Mode::File};

inline constexpr std::string_view kModeKey = "tableMode";
inline constexpr std::string_view kColumnsKey = "dynColumns";
inline constexpr std::string_view kSortKey = "sortBy";            // le titre d'une colonne ; vide : l'ordre de la source
inline constexpr std::string_view kSortOrderKey = "sortOrder";    // « croissant » / « d\xC3\xA9" "croissant »
inline constexpr std::string_view kPickKey = "pickVariable";      // « Ligne choisie → » : la variable que le clic ecrit
inline constexpr std::string_view kEmptyKey = "emptyText";        // « Sans ligne »
inline constexpr std::string_view kAccessKey = "colAccess";       // les cases fixes : R;W;R
inline constexpr std::string_view kMissing = "...";               // une case sans valeur (une liste plus courte)

[[nodiscard]] std::string_view modeLabel(Mode) noexcept;          // « cases fixes »... (enregistre)
[[nodiscard]] std::string_view modeTitle(Mode) noexcept;          // « Cases fixes »... (montre)
[[nodiscard]] std::string_view modeHint(Mode) noexcept;           // « un nombre fixe de lignes et de colonnes »
[[nodiscard]] std::optional<Mode> modeFromLabel(std::string_view) noexcept;   // l'un ou l'autre, sans casse
// Le mode du tableau : tableMode ; absent, celui d'avant (Lignes depuis, puis le fichier, puis les cases).
[[nodiscard]] Mode modeOf(const Object&);
[[nodiscard]] bool modeExplicit(const Object&);
// Rien de regle pour ce mode (« vide ») ; sinon ce qui l'est, en peu de mots (« 4 × 3 cases, 10 remplies »).
[[nodiscard]] bool modeEmpty(const Object&, Mode);
[[nodiscard]] std::string modeSummary(const Object&, Mode);
// Efface les reglages d'un mode (ceux d'un mode en reserve).
void clearMode(Object&, Mode);

// ---- les colonnes dynamiques -----------------------------------------------------------
struct Column {
    std::string title, content, format, unit;
    double      width{1.0};
    std::string align;         // "", "gauche", "centre", "droite" (vide : un nombre a droite)
    bool        write{false};  // R/W
    int         level{0};      // le niveau pour ecrire (0 : comme l'objet)
    std::string color;         // "", "seuils", "formule"
    std::string colorExpr;     // la formule de couleur (=SEL(...))
    bool operator==(const Column&) const = default;
};
[[nodiscard]] std::vector<Column> parseColumns(std::string_view text);
[[nodiscard]] std::string         formatColumns(const std::vector<Column>&);
[[nodiscard]] bool                hasColumns(const Object&);   // dynColumns regle : les colonnes de 1.12.13
[[nodiscard]] std::vector<Column> columnsOf(const Object&);

// ---- la source des lignes (sans moteur) ------------------------------------------------
enum class SourceKind : std::uint8_t { None, Array, List, Map, Struct, Enum, Unknown, Error };
struct Source {
    SourceKind  kind{SourceKind::None};
    std::string name;          // Cuves
    std::string type;          // ARRAY[1..4] OF T_Cuve (le type ecrit) ; une enumeration : son nom
    std::string element;       // le type d'un element : T_Cuve, INT ; une MAP : sa valeur
    std::string key;           // une MAP : le type de sa cle
    long long   low{0};        // le premier indice (un tableau : sa borne ; une LIST : 0)
    long long   count{-1};     // le nombre de lignes connu sans moteur (-1 : en marche)
    bool        formula{false};
    bool        markers{false};
    std::string error, fix;    // une faute, et sa correction (Texts[0] -> Texts)
};
[[nodiscard]] Source analyzeSource(const Project&, std::string_view rowsFrom);
[[nodiscard]] std::string rowsWord(const Source&);                 // « une ligne par élément »...

// ---- une colonne : ce que montre sa case, son type, si elle s'ecrit ---------------------
enum class Content : std::uint8_t { Empty, Text, Path, RowNumber, Template, Formula };
[[nodiscard]] Content contentKind(std::string_view text);
[[nodiscard]] std::string_view contentLabel(Content) noexcept;     // « variable », « texte à trous »...
struct ColumnInfo {
    Content     kind{Content::Empty};
    std::string type;               // le type recu (REAL, T_Etat, STRING) ; vide : inconnu
    std::string unit, format;       // ceux de la variable (Unites et formats), s'il y en a
    std::string error, warning, fix;
    std::vector<std::string> lists; // les listes nommees de la colonne (Debits)
    bool        writable{false};
    std::string writeWhy;           // pourquoi elle ne s'ecrit pas
    std::string target;             // ce qu'elle ecrit, pour un humain (Cuves[i].Consigne)
    std::string path;               // le chemin ecrit, tel que dans le contenu ([*].Consigne)
};
[[nodiscard]] ColumnInfo analyzeColumn(const Project&, const Source&, const Column&);
// Les propositions pour le contenu d'une case : [*].Nom, [*].Vanne.Ouverture, [#], Debits[*]...
struct Suggestion {
    std::string text, type;
};
[[nodiscard]] std::vector<Suggestion> suggestions(const Project&, const Source&);
// « Une colonne par membre » : une colonne par membre simple de l'element (par valeur sinon).
[[nodiscard]] std::vector<Column> columnsForSource(const Project&, const Source&);
// Un tableau d'avant (Lignes depuis, Colonnes de la source, Largeurs) : ses colonnes, a l'identique.
[[nodiscard]] std::vector<Column> legacyColumns(const Project&, const Object&);
// Le titre propose pour un contenu (« [*].Vanne.Ouverture » -> « Ouverture »).
[[nodiscard]] std::string titleFor(std::string_view content);

// ---- pour le moteur ---------------------------------------------------------------------
inline constexpr std::string_view kElt = "__TABLE_ELT__";
inline constexpr std::string_view kRow = "__TABLE_ROW__";
inline constexpr std::string_view kPos = "__TABLE_POS__";
inline constexpr std::string_view kKey = "__TABLE_KEY__";
inline constexpr std::string_view kVal = "__TABLE_VAL__";
inline constexpr std::string_view kTxt = "__TABLE_TXT__";
// Le contenu d'une case, pret a compiler (sans ses $ ni son = de tete) : [*] -> __TABLE_ELT__,
// [*].Cle -> __TABLE_KEY__, Liste[*] -> Liste[__TABLE_POS__ + bas]... Les listes nommees : `lists`.
[[nodiscard]] std::string compileContent(std::string_view text, const Source&, const Project*,
                                         std::vector<std::string>* lists = nullptr);
// Les chemins d'une ligne : l'element (Cuves[2], Alarmes[0], Stock['vis']) et, pour une MAP ou une
// structure seule, sa valeur (Stock['vis'], Recette.Nom).
struct RowPaths {
    std::string element;
    std::string value;
    std::string key;           // une MAP : sa cle ecrite ('vis', 3) ; une structure : le nom du membre ('Nom')
    long long   position{0};   // le rang dans la source (0, 1...)
};
// Le chemin qu'ecrit une colonne R/W pour cette ligne (vide : rien).
[[nodiscard]] std::string writePathOf(std::string_view content, const Source&, const RowPaths&, const Project*);
// Une cle de MAP ecrite dans un chemin : 'vis' (les ' doublees), un entier tel quel.
[[nodiscard]] std::string keyLiteral(std::string_view key, bool text);

// ---- les cases fixes ---------------------------------------------------------------------
// L'acces des n colonnes (colAccess : R;W;R ; absent : toutes en lecture).
[[nodiscard]] std::vector<bool> fixedAccess(const Object&, std::size_t n);
[[nodiscard]] std::string       formatAccess(const std::vector<bool>&);
// Ce qu'ecrit une case fixe : la variable de son seul trou ({PA:0.00} bar -> PA), d'une formule
// qui n'est qu'un chemin (=PA) ; vide : elle reste en lecture.
[[nodiscard]] std::string fixedWritePath(std::string_view cell);

// ---- la ligne choisie, le tri ---------------------------------------------------------------
[[nodiscard]] bool sortDescending(const Object&);
// La colonne du tri (sortBy : son titre, ou son contenu) ; -1 : l'ordre de la source.
[[nodiscard]] int sortColumnOf(const Object&, const std::vector<Column>&);

// ---- ce que montre le tableau en marche (Runtime::tableModel) -------------------------------
struct ModelCell {
    std::string   text;
    bool          missing{false};   // « ... » : la liste de la colonne n'a pas cette ligne
    bool          error{false};     // ### : la case ne se calcule pas
    int           tone{0};          // les seuils de la variable : 0 rien, 1 seuil (jaune), 2 alarme (rouge)
    bool          colored{false};   // la formule de couleur a donne une couleur
    std::uint32_t color{0};         // 0xRRGGBB
    bool          numeric{false};   // un nombre (le tri)
    double        number{0};
};
struct ModelRow {
    std::vector<ModelCell> cells;
    long long   position{0};        // le rang dans la source (0, 1...) ; Ligne = position + 1
    std::string index;              // Index : l'indice (2) ; Cle : la cle d'une MAP, le membre d'une structure, le nom d'une valeur
    std::string element;            // le chemin de l'element (Cuves[2], Stock['vis'], Recette.Nom) ; vide : sans chemin
    bool        picked{false};      // la ligne choisie (pickVariable)
};
struct Model {
    bool        columns{false};     // les colonnes de 1.12.13 (dynColumns) ; faux : le tableau d'avant, tel quel
    SourceKind  kind{SourceKind::None};
    std::vector<std::string> headers;
    std::vector<double>      widths;     // relatives
    std::vector<std::string> aligns;     // "gauche", "centre", "droite"
    std::vector<bool>        writable;   // R/W, par colonne
    std::vector<ModelRow>    rows;       // dans l'ordre montre (trie)
    std::string emptyText;               // « Sans ligne » (vide : « (aucune ligne) »)
    std::string error;                   // la source ne se lit pas : dit a la place des lignes
    int         sortColumn{-1};
    bool        sortDescending{false};
};

} // namespace hmi::table
