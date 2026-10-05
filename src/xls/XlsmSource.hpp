// =============================================================================
//  xls/XlsmSource.hpp — lire un .xlsx / .xlsm sans rien installer
// -----------------------------------------------------------------------------
//  POURQUOI CE FICHIER EXISTE
//
//  Les macros d'import ne savent pas ouvrir un fichier : `Ask`, `Cell`,
//  `AddVariable` sont des fonctions de l'hote. Il manquait donc, cote C++, de
//  quoi transformer `automation.xlsm` en quelque chose qui ressemble a une
//  table CSV deja chargee. C'est tout ce que fait ce fichier.
//
//  Il est volontairement AUTONOME : un .hpp, un .cpp, aucune dependance en
//  dehors de core/Result.hpp et de la bibliotheque standard. Pas de miniz, pas
//  de zlib, pas de libxml. Un .xlsx est un zip de XML ; le zip et le XML dont
//  on a besoin tiennent dans le .cpp. C'est deux fichiers a ajouter au projet
//  Visual Studio, pas une bibliotheque a integrer.
//
//  CE QU'IL FAIT
//    - ouvre le zip, y compris un .xlsm dont le vbaProject.bin est ignore ;
//    - resout les noms d'onglets (exacts, espaces compris) vers leurs XML ;
//    - rend les valeurs EN TEXTE, comme un CSV : `16` et non `16.0`, cellule
//      vide = chaine vide, jamais NULL ;
//    - CHERCHE la ligne d'en-tete au lieu de la supposer, et saute la ligne
//      d'exemples quand elle est la. Voir `Sheet::headerLine`.
//
//  CE QU'IL NE FAIT PAS, ET QU'IL FAUT SAVOIR
//    - Les dates sortent en numero de serie Excel (`46281`), pas en `13/09/2026`.
//      Aucun des cinq onglets lus par les macros n'a de date ; le bandeau en a
//      une, et le bandeau n'est pas lu. Le jour ou une date compte, c'est
//      `styles.xml` qu'il faudra lire, et c'est un autre chantier.
//    - Il ne recalcule rien. Une formule dont Excel n'a pas enregistre le
//      resultat sort vide. Excel enregistre toujours ; LibreOffice aussi.
//    - Il ne sait pas ecrire. L'import va du classeur vers le programme.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xls {

// -----------------------------------------------------------------------------
// Un onglet, reduit a ce dont une macro a besoin : des colonnes nommees et des
// lignes de donnees. La grille brute reste accessible pour l'onglet `Config`,
// qui est un formulaire et non un tableau.
// -----------------------------------------------------------------------------
class Sheet {
public:
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // ---- la table, telle que les macros la voient -------------------------
    [[nodiscard]] const std::vector<std::string>& columns() const noexcept { return columns_; }
    [[nodiscard]] bool        hasColumn(std::string_view column) const noexcept;
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::string cell(std::size_t row, std::string_view column) const;
    [[nodiscard]] std::string cell(std::size_t row, std::size_t column) const;

    // ---- ce que la recherche d'en-tete a decide ---------------------------
    // En numeros de ligne Excel (1 = la premiere), pour que le message d'erreur
    // d'une macro designe la meme ligne que celle qu'on voit a l'ecran.
    // `headerLine() == 0` veut dire : aucun en-tete trouve, l'onglet n'est pas
    // un tableau. C'est le cas de `Config`.
    [[nodiscard]] std::uint32_t headerLine() const noexcept { return headerLine_; }
    [[nodiscard]] std::uint32_t firstDataLine() const noexcept { return firstDataLine_; }
    [[nodiscard]] bool          hintLineSkipped() const noexcept { return hintSkipped_; }
    [[nodiscard]] const std::string& witnessColumn() const noexcept { return witness_; }

    // Combien de lignes ont ete laissees APRES la fin des donnees. Un onglet
    // genere par Excel traine souvent une note en clair vingt lignes plus bas ;
    // lue comme une donnee, elle fabrique un equipement nomme "Une ligne par
    // report. Les onglets Reports sont DERIVES...". On s'arrete avant, et on
    // dit combien on a laisse pour que ca ne passe pas inapercu.
    [[nodiscard]] std::size_t linesIgnoredAfterData() const noexcept { return ignoredAfter_; }

    // ---- la grille brute, 0-based, sans aucune interpretation --------------
    [[nodiscard]] std::string  raw(std::size_t line, std::size_t column) const;
    [[nodiscard]] std::size_t  rawLineCount() const noexcept { return grid_.size(); }

private:
    friend class Workbook;

    std::string                            name_;
    std::vector<std::vector<std::string>>  grid_;        // [ligne][colonne], 0-based
    std::vector<std::uint32_t>             mergedMask_;  // lignes portant une cellule fusionnee
    std::vector<std::string>               columns_;
    std::vector<std::size_t>               rows_;        // indices dans grid_
    std::string                            witness_;
    std::uint32_t                          headerLine_{0};
    std::uint32_t                          firstDataLine_{0};
    std::size_t                            ignoredAfter_{0};
    bool                                   hintSkipped_{false};

    void   buildTable();
    [[nodiscard]] bool lineHasMergedCell(std::size_t line) const noexcept;
    [[nodiscard]] std::size_t columnIndex(std::string_view column) const noexcept;
};

// -----------------------------------------------------------------------------
class Workbook {
public:
    [[nodiscard]] static core::Result<Workbook> open(const std::string& path);

    [[nodiscard]] std::vector<std::string> sheetNames() const;

    // Comparaison EXACTE, espaces compris : `Cartes API` a une espace, et deux
    // onglets peuvent ne differer que par elle. Rend nullptr si l'onglet
    // n'existe pas — c'est le `-1` de `OpenSheet`.
    [[nodiscard]] const Sheet* sheet(std::string_view name) const;

    // L'onglet `Config` n'est pas un tableau, c'est un formulaire : une cle
    // dans une colonne, sa valeur dans la premiere cellule non vide a sa
    // droite. Cle absente = chaine vide ; toutes les macros ont un repli.
    [[nodiscard]] std::string setting(std::string_view key) const;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string        path_;
    std::vector<Sheet> sheets_;
};

} // namespace xls
