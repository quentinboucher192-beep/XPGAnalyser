// =============================================================================
//  ui/TextSearch.hpp - chercher comme on parle (lot recherche)
// -----------------------------------------------------------------------------
//  UNE SEULE FACON DE CHERCHER, pour toutes les recherches de l'application :
//  les tableaux de l'API, les listes de l'IHM, la simulation, le choix des
//  variables d'une table d'animation, Aller a... (Ctrl+K).
//
//    pompe vanne        les DEUX mots, dans n'importe quelle colonne (ET) ;
//    "pompe 2"          la phrase exacte (entre guillemets) ;
//    pompe -secours     pompe, mais pas secours (-mot : exclure) ;
//    Equipement         trouve aussi "equipement" et "EQUIPEMENTS" : ni la
//                       casse ni les accents ne comptent (e = e = E...).
//
//  Chaque terme doit se trouver dans l'UN des textes donnes (le nom, le type,
//  le commentaire...) ; un terme exclu dans AUCUN. ranges() dit ou les termes
//  sont dans un texte : les tableaux s'en servent pour les surligner.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {

// Le texte plie pour chercher : minuscules, sans accents (les lettres latines
// accentuees de l'UTF-8 ; oe et ae lies deviennent "oe", "ae"). `starts`
// (facultatif) recoit, pour chaque octet plie, l'octet de l'original ou sa
// lettre commence, puis la taille de l'original (une entree de plus).
[[nodiscard]] std::string foldForSearch(std::string_view text, std::vector<std::size_t>* starts = nullptr);

// `needle` (deja plie) est-il dans `hay` (pas encore plie) ? Sans allocation
// quand `hay` est de l'ASCII - le cas de presque tous les noms.
[[nodiscard]] bool containsFolded(std::string_view hay, std::string_view foldedNeedle);

class SearchQuery {
public:
    SearchQuery() = default;
    explicit SearchQuery(std::string_view text);

    [[nodiscard]] bool empty() const noexcept { return include_.empty() && exclude_.empty(); }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    // Les termes cherches, plies, dans l'ordre tape (les exclus a part).
    [[nodiscard]] const std::vector<std::string>& terms() const noexcept { return include_; }
    [[nodiscard]] const std::vector<std::string>& excluded() const noexcept { return exclude_; }

    // Chaque terme dans l'un des textes, aucun exclu dans aucun. Vide : vrai.
    [[nodiscard]] bool matches(std::initializer_list<std::string_view> texts) const;
    [[nodiscard]] bool matches(const std::vector<std::string>& texts) const;
    [[nodiscard]] bool matches(const std::vector<std::string_view>& texts) const;

    // UNE RECHERCHE EN PLUSIEURS TEMPS : les textes d'un element donnes un par
    // un (le nom, puis - s'il le faut - les commentaires de ses champs).
    struct Progress {
        std::uint64_t found{0};        // un bit par terme deja trouve (les 64 premiers)
        bool          excluded{false};
    };
    void feed(Progress& p, std::string_view text) const;
    [[nodiscard]] bool satisfied(const Progress& p) const noexcept;   // tout trouve, rien d'exclu
    [[nodiscard]] bool hopeless(const Progress& p) const noexcept { return p.excluded; }

    // Ou sont les termes dans `text` : des plages [debut, fin) d'octets de
    // l'original, triees, fusionnees quand elles se touchent.
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> ranges(std::string_view text) const;

    // Pour classer : 0 le texte commence par le premier terme, 1 un mot du
    // texte commence par lui, 2 il le contient ailleurs, -1 absent (ou pas de terme).
    [[nodiscard]] int rank(std::string_view text) const;

private:
    std::string              text_;
    std::vector<std::string> include_, exclude_;
};

} // namespace ui
