// =============================================================================
//  hmi/HmiTemplates.hpp - ecrans modeles, en-tetes et pieds de page
// -----------------------------------------------------------------------------
//  UNE VUE PEUT HERITER D'UN ECRAN MODELE (View::templateView), qui peut lui-
//  meme heriter d'un autre. EN MARCHE, le modele passe en premier : il se
//  dessine dessous, ses scripts (OnOpen, OnCycle, OnClose) et ses actions de vue
//  s'executent avant ceux de la vue. Ses objets repondent aux clics comme ceux
//  de la vue.
//
//  UNE VUE PEUT AVOIR UN EN-TETE ET UN PIED DE PAGE (deux cases a cocher), DANS
//  la vue : la bande du modele d'en-tete se pose en haut, celle du pied en bas,
//  par-dessus le contenu de la vue. Leurs scripts passent apres ceux du modele
//  et avant ceux de la vue.
//
//  compose() rend la vue TELLE QU'ELLE TOURNE : une seule View, que le moteur,
//  l'evaluation des expressions et le dessin prennent sans rien savoir des
//  modeles. Les objets empruntes gardent leur identifiant (unique dans le
//  projet) ; ceux du pied sont descendus de (hauteur de la vue - hauteur du pied).
//
//  RIEN NE BOUCLE : une chaine qui revient sur elle-meme s'arrete (et Generer le
//  dit), comme un modele introuvable ou qui n'est pas un ecran modele.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

inline constexpr std::string_view kViewRoles[] = {"vue", "modele", "entete", "pied", "popup", "symbole"};   // popup : lot 8 ; symbole : lot 10
//  En ligne (lot 9) : l'arbre du projet (Variables d'instances) les montre sans
//  lier la bibliotheque de l'IHM.
inline constexpr std::pair<std::string_view, std::string_view> kRoleLabels[] = {
    {"vue", "Vue"},
    {"modele", "\xC3\x89" "cran mod\xC3\xA8le"},
    {"entete", "Mod\xC3\xA8le d'en-t\xC3\xAAte"},
    {"pied", "Mod\xC3\xA8le de pied de page"},
    {"popup", "Popup"},
    {"symbole", "Symbole"},
};
// "Vue", "Ecran modele"...
[[nodiscard]] inline std::string_view viewRoleLabel(std::string_view role) noexcept {
    for (const auto& [key, label] : kRoleLabels) if (key == role) return label;
    return "Vue";
}
// L'inverse ; "vue" sinon.
[[nodiscard]] inline std::string_view viewRoleFromLabel(std::string_view label) noexcept {
    for (const auto& [key, l] : kRoleLabels) if (l == label || key == label) return key;
    return "vue";
}
// modele, entete, pied
[[nodiscard]] inline bool isTemplateRole(std::string_view role) noexcept { return role == "modele" || role == "entete" || role == "pied"; }

// Lot 8 : LES DOSSIERS DE L'ARBRE. Vues > Modeles (ecrans modeles, en-tetes,
// pieds de page), Vues, Popups : le dossier d'une vue suit son role. Un role
// inconnu tombe dans Vues (Generer le signale). Lot 10 : les symboles ont leur
// dossier a eux, IHM > Symboles (a cote de Vues).
//  En ligne (dans l'en-tete) : l'arbre du projet (app/ViewModels.cpp) s'en sert
//  sans lier la bibliotheque de l'IHM.
enum class ViewFolder : std::uint8_t { Templates, Views, Popups, Symbols };
[[nodiscard]] inline ViewFolder viewFolderOf(std::string_view role) noexcept {
    if (role == "modele" || role == "entete" || role == "pied") return ViewFolder::Templates;
    if (role == "popup") return ViewFolder::Popups;
    if (role == "symbole") return ViewFolder::Symbols;
    return ViewFolder::Views;
}
[[nodiscard]] inline std::vector<const View*> viewsWithRole(const Project& p, std::string_view role) {
    std::vector<const View*> out;
    for (const auto& v : p.views) if (v.role == role) out.push_back(&v);
    return out;
}
[[nodiscard]] inline std::vector<const View*> viewsInFolder(const Project& p, ViewFolder f) {
    std::vector<const View*> out;
    // 1.11.10 : une popup d'un symbole est rangee sous son symbole, pas dans IHM > Popups.
    for (const auto& v : p.views)
        if (viewFolderOf(v.role) == f && !(f == ViewFolder::Popups && v.ownerSymbol != kNoId)) out.push_back(&v);
    return out;
}

// Les ecrans modeles d'une vue, du plus lointain au plus proche (sans la vue).
// `broken` : la chaine s'arrete sur un modele introuvable, qui n'en est pas un,
// ou un cycle.
[[nodiscard]] std::vector<const View*> templateChain(const Project&, const View&, std::string* broken = nullptr);
// Le modele d'en-tete / de pied montre par cette vue (case cochee), ou nul.
[[nodiscard]] const View* headerOf(const Project&, const View&);
[[nodiscard]] const View* footerOf(const Project&, const View&);
// La vue emprunte-t-elle quelque chose (modele, en-tete, pied) ?
[[nodiscard]] bool inherits(const Project&, const View&);
// La vue telle qu'elle tourne. Sans heritage : une copie de la vue.
[[nodiscard]] View compose(const Project&, const View&);
// D'ou vient chaque action de vue de compose(v), dans le meme ordre : l'id
// de la vue qui la porte (un modele, l'en-tete, le pied, ou v elle-meme).
[[nodiscard]] std::vector<Id> actionOrigins(const Project&, const View&);
// Les vues qui heritent de `templ` (directement ou par un modele), ou qui
// l'utilisent comme en-tete ou pied.
[[nodiscard]] std::vector<const View*> viewsUsing(const Project&, Id templ);

} // namespace hmi
