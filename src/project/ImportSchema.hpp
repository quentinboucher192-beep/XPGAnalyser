// =============================================================================
//  project/ImportSchema.hpp — ce qu'un fichier importable doit contenir
// -----------------------------------------------------------------------------
//  UNE SEULE DECLARATION, TROIS LECTEURS.
//
//  Les colonnes attendues etaient ecrites dans le corps des macros, en clair,
//  au moment de les lire : Cell(t, i, 'Tableau'). La documentation, elle,
//  aurait ete ecrite a cote. Elle aurait dit la verite le premier jour et menti
//  au premier changement de colonne - et personne ne s'en apercoit, parce que
//  personne ne relit une documentation qu'il croit juste.
//
//  Ici la liste est declaree UNE fois. L'importateur la lit pour verifier le
//  fichier, la page d'aide la lit pour l'afficher, et le generateur d'exemples
//  la lit pour fabriquer un fichier vide a remplir. Le format ne peut plus
//  diverger de sa documentation : ils sont le meme objet.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace project {

struct ColumnSpec {
    std::string name;        // le titre exact, tel qu'il doit figurer
    bool        required{false};
    std::string example;     // ce qu'on y met, pour le fichier d'exemple
    std::string help;        // pourquoi elle existe
};

// Les familles d'E/S. Elles partagent la moitie de leurs colonnes et different
// sur le reste : une voie TOR n'a pas d'echelle, une voie ANA n'a pas
// d'anti-rebond.
enum class Family { Digital, Analog };

struct ImportFormat {
    std::string id;          // "es.di", "equipements", "reports"
    std::string title;
    std::string purpose;     // une phrase : ce que l'import fabrique
    std::vector<ColumnSpec> columns;

    // Les colonnes d'ancrage : celles dont la presence simultanee designe la
    // ligne d'en-tete. Toujours un sous-ensemble des obligatoires.
    [[nodiscard]] std::vector<std::string> anchors() const;

    [[nodiscard]] const ColumnSpec* find(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> requiredNames() const;

    // Un fichier vide a remplir : la ligne de titres, la ligne d'aide, et une
    // ligne d'exemple. Celui que l'onglet d'aide propose d'enregistrer.
    [[nodiscard]] std::string exampleCsv(char separator = ';') const;

    // 1.11 (T2, tranche 13) : le meme fichier tel qu'il s'enregistre, en UTF-8
    // avec le BOM devant. Sans lui, Excel lit la ligne d'aide (accentuee) en
    // Windows-1252 ; l'import le saute (project::Table). exampleCsv(), lui, reste
    // sans BOM : c'est le texte que l'aide montre et que "Copier" emporte.
    [[nodiscard]] std::string exampleCsvFile(char separator = ';') const;
};

// Les formats connus. L'ordre est celui dans lequel on s'en sert.
[[nodiscard]] const std::vector<ImportFormat>& formats();
[[nodiscard]] const ImportFormat* format(std::string_view id);

// 1.11 (T2, tranche 13) : ecrit le fichier d'exemple de chaque format dans `dir`
// (cree au besoin) : exemple-<id>.csv, les points de l'id en tirets
// (exemple-es-di.csv), en UTF-8 avec le BOM. Rend les chemins ecrits (UTF-8) ;
// vide, avec la raison dans *why, si un fichier n'a pas pu s'ecrire.
[[nodiscard]] std::vector<std::string> writeExampleCsvs(const std::string& dir, std::string* why = nullptr);

// ---------------------------------------------------------------------------
//  L'ORDRE DES SECTIONS, qui n'est pas une question de gout.
//
//  Une commande vient de l'IHM et doit etre lue AVANT que les blocs ne
//  s'executent ; une information part vers l'IHM et doit etre ecrite APRES.
//  L'inverser fait remonter ce que le bloc avait calcule au cycle PRECEDENT -
//  un cycle de retard que personne ne cherche, parce que personne ne le
//  soupconne.
//
//  Les alarmes sont un cas a part : elles ne se recopient pas dans un mot, elles
//  passent par DFB_ALM_MANAGER, qui les horodate et tient l'historique. Une
//  alarme ecrite a la main dans un %MX perd sa date d'apparition.
// ---------------------------------------------------------------------------
enum class ReportKind { Command, Measure, Alarm };   // TC, TM, TA

[[nodiscard]] ReportKind reportKindOf(std::string_view code);
[[nodiscard]] std::string_view reportCode(ReportKind);

// Le rang d'une section dans le programme. Plus petit = plus tot.
[[nodiscard]] int sectionOrder(ReportKind);

// Le nom de section ou va un report de ce genre.
[[nodiscard]] std::string sectionFor(ReportKind);

} // namespace project
