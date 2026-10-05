// =============================================================================
//  ui/HelpDocument.hpp — un document consultable, sans SDL
// -----------------------------------------------------------------------------
//  POURQUOI LE DOCUMENT EST SEPARE DE SON AFFICHAGE.
//
//  Un menu d'aide avec sommaire, index et recherche demande surtout des
//  decisions qui n'ont rien de graphique : ou mene une entree de sommaire, quel
//  titre est actif quand on s'arrete entre deux, que rend une recherche sans
//  resultat, comment un index se trie quand les mots portent des accents.
//
//  Toutes ces decisions vivent ici, et sont verifiees sans ecran. Le widget se
//  contente de les dessiner. La separation n'est pas de la mise en ordre : sur
//  ce projet, les machines de test n'ont pas SDL, et ce qui vit dans le
//  renderer n'y est jamais compile.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ui {

// Ce dont un document a besoin, et rien de plus. Un moteur de rendu de texte
// riche complet serait une bibliotheque ; ici il faut des titres, des
// paragraphes, des listes, du code et des tableaux.
enum class BlockKind { Heading, Paragraph, Bullet, Code, TableRow, Separator };

struct Block {
    BlockKind   kind{BlockKind::Paragraph};
    int         level{0};             // 1, 2, 3 pour un titre
    std::string text;
    std::vector<std::string> cells;   // TableRow seulement
    bool        header{false};        // premiere ligne d'un tableau
    std::string anchor;               // ancre stable, pour les liens
};

struct TocEntry {
    std::string title;
    std::string anchor;
    int         level{1};
    std::size_t block{0};             // ou sauter dans le document
};

struct IndexEntry {
    std::string term;
    std::vector<std::string> anchors; // ou ce terme est defini ou explique
};

struct SearchHit {
    std::size_t block{0};
    std::string anchor;               // le titre sous lequel on se trouve
    std::string section;              // son libelle, pour l'afficher
    std::string excerpt;              // le passage, autour du mot trouve
};

class HelpDocument {
public:
    // ---- construction ----------------------------------------------------
    HelpDocument& heading(int level, std::string text, std::string anchor = {});
    HelpDocument& paragraph(std::string text);
    HelpDocument& bullet(std::string text);
    HelpDocument& code(std::string text);
    HelpDocument& tableRow(std::vector<std::string> cells, bool header = false);
    HelpDocument& separator();

    // Un terme d'index, rattache au titre courant. Declare a l'endroit ou le
    // terme est EXPLIQUE, pas a chaque fois qu'il apparait : un index qui
    // renvoie a vingt endroits ne renvoie nulle part.
    HelpDocument& indexTerm(std::string term);

    // ---- consultation ----------------------------------------------------
    [[nodiscard]] const std::vector<Block>& blocks() const noexcept { return blocks_; }
    [[nodiscard]] const std::vector<TocEntry>& toc() const noexcept { return toc_; }

    // L'index, trie. Les accents ne comptent pas dans le tri : "Echelle" et
    // "Ecran" doivent se suivre, et un lecteur francais ne cherche pas "Échelle"
    // apres "Zone".
    [[nodiscard]] std::vector<IndexEntry> index() const;

    // Le bloc ou mene une ancre. size() si elle n'existe pas - le widget doit
    // pouvoir distinguer "debut du document" de "introuvable".
    [[nodiscard]] std::size_t blockOf(std::string_view anchor) const;

    // Sous quel titre se trouve ce bloc. Un bloc avant tout titre rend "".
    [[nodiscard]] std::string sectionOf(std::size_t block) const;

    // La recherche. Insensible a la casse ET aux accents : personne ne tape
    // "périmé" avec son accent dans un champ de recherche.
    [[nodiscard]] std::vector<SearchHit> search(std::string_view needle,
                                                std::size_t limit = 50) const;

    [[nodiscard]] bool empty() const noexcept { return blocks_.empty(); }

    // Normalise pour comparer : minuscules, sans accents. Exposee parce que le
    // tri de l'index et la recherche doivent utiliser EXACTEMENT la meme, sinon
    // un terme se trouve dans l'un et pas dans l'autre.
    [[nodiscard]] static std::string fold(std::string_view s);

private:
    std::vector<Block>    blocks_;
    std::vector<TocEntry> toc_;
    std::vector<std::pair<std::string, std::string>> terms_;  // terme, ancre
    std::string currentAnchor_;

    [[nodiscard]] static std::string slug(std::string_view title);
};

// Le document de l'aide de l'application, construit depuis les formats
// declares dans ImportSchema : la documentation d'un format et le format
// lui-meme sont le meme objet, et ne peuvent pas diverger.
[[nodiscard]] HelpDocument buildHelp();

} // namespace ui
