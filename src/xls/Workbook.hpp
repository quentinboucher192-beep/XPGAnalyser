// =============================================================================
//  xls/Workbook.hpp — lire un .xlsx ou un .xlsm
// -----------------------------------------------------------------------------
//  POURQUOI CE N'EST PAS UN NOUVEAU TYPE DE TABLEAU. Le lecteur rend un
//  project::Table, exactement celui que Table::parse rend pour un CSV. Tout ce
//  qui suit - ImportES, LierAlarmes, les macros, la recherche de colonne par son
//  nom - fonctionne alors sans changer une ligne. Un second type de tableau
//  aurait double chaque chemin de code qui en consomme un.
//
//  CE QU'EST UN .XLSX. Une archive ZIP de fichiers XML :
//
//     xl/workbook.xml            les noms des feuilles, et leur rId
//     xl/_rels/workbook.xml.rels le rId vers le chemin reel de la feuille
//     xl/sharedStrings.xml       TOUTES les chaines du classeur, une fois
//     xl/worksheets/sheetN.xml   les cellules, qui renvoient a l'index ci-dessus
//
//  Un .xlsm est le meme format avec des macros en plus, dans un vbaProject.bin
//  qu'on ignore. Les deux se lisent pareil, et c'est pourquoi une seule
//  fonction suffit.
//
//  LE PIEGE DES CHAINES PARTAGEES. Une cellule de texte ne contient pas son
//  texte : elle contient un NUMERO dans la table commune. Lire une feuille sans
//  avoir lu cette table rend des nombres a la place des designations - et ces
//  nombres ont l'air de donnees valides.
//
//  LE PIEGE DES FORMULES. Une cellule calculee porte sa formule ET son dernier
//  resultat. C'est le RESULTAT qu'on veut - c'est aussi ce qu'un export CSV
//  donne - donc <v> et jamais <f>. Un classeur jamais recalcule apres une
//  modification a des resultats perimes : le lecteur le signale plutot que de
//  faire semblant.
//
//  AUCUNE DEPENDANCE A INSTALLER. miniz est un fichier .c pose dans
//  third_party/, domaine public, compile avec le reste. Voir INSTALL.md.
// =============================================================================
#pragma once

#include "../project/Table.hpp"

#include <string>
#include <vector>

namespace xls {

struct SheetInfo {
    std::string name;       // ce que l'onglet affiche
    std::string path;       // xl/worksheets/sheet3.xml
    bool        hidden{false};
};

struct WorkbookInfo {
    std::vector<SheetInfo> sheets;
    std::size_t sharedStrings{0};
    bool        hasMacros{false};      // un vbaProject.bin est present
    std::vector<std::string> warnings;
};

// Ce que le classeur contient, sans lire une seule feuille.
//
// LA LISTE DES ONGLETS SE DEMANDE AVANT DE CHOISIR. Un CSV n'a qu'une feuille et
// l'appelant le sait ; un classeur en a douze, et lui demander "quel onglet ?"
// sans pouvoir lui montrer la liste revient a lui demander de deviner.
[[nodiscard]] WorkbookInfo inspect(const std::string& path);

struct ReadOptions {
    // L'onglet voulu, par son nom. Vide : le premier qui n'est pas masque.
    std::string sheet;

    // Les memes options que pour un CSV : les colonnes d'ancrage qui reperent
    // la ligne d'en-tete, et le nombre de lignes de description a sauter.
    project::TableOptions table;

    // Au-dela, on s'arrete. Une feuille Excel declare souvent un millier de
    // lignes formatees et vides ; les lire toutes coute du temps pour rien.
    std::size_t maxRows{20000};
};

struct ReadResult {
    project::Table table;
    std::string    sheetUsed;
    bool           ok{false};
    std::vector<std::string> warnings;
};

// Lit une feuille et la rend sous la forme d'un project::Table.
[[nodiscard]] ReadResult read(const std::string& path, const ReadOptions& = {});

// Le numero de colonne d'une reference de cellule : "A1" -> 0, "BC12" -> 54.
// Expose parce que c'est la seule arithmetique non evidente du format, et
// qu'elle merite d'etre verifiee toute seule.
[[nodiscard]] std::size_t columnOf(std::string_view ref);
[[nodiscard]] std::size_t rowOf(std::string_view ref);

// ---------------------------------------------------------------------------
//  Deux morceaux sortis de read() pour une seule raison : ILS SONT INVERIFIABLES
//  AUTREMENT.
//
//  Un classeur reel ne contient pas forcement le cas qui revele l'erreur.
//  Celui de l'affaire n'a aucune cellule manquante, donc aucune lecture de ce
//  fichier ne peut montrer qu'on empile les cellules au lieu de les placer ; et
//  son premier onglet est visible, donc aucune ne peut montrer qu'on choisit un
//  onglet masque par defaut. Une passe de mutations l'a demontre : les deux
//  fautes passaient inapercues.
// ---------------------------------------------------------------------------

// Transforme le XML d'une feuille en lignes. sharedStrings est la table commune.
[[nodiscard]] std::vector<std::vector<std::string>>
parseSheet(std::string_view xml, const std::vector<std::string>& sharedStrings,
           std::size_t maxRows = 20000, bool* formulasSeen = nullptr);

// Quel onglet lire. Nom vide : le premier VISIBLE.
[[nodiscard]] const SheetInfo* chooseSheet(const std::vector<SheetInfo>& sheets,
                                           std::string_view wanted);

} // namespace xls
