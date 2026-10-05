// =============================================================================
//  hmi/HmiQuality.hpp - Generer plus exigeant (lot 13) : ce qui se voit mal,
//                       ce qui ne s'atteint pas, ce qui se touche mal
// -----------------------------------------------------------------------------
//  GENERER DIT DEJA CE QUI NE TOURNERA PAS. Ces controles disent ce qui tourne
//  mais servira mal l'operateur :
//
//    VISIBILITE   un objet hors de la vue (en tout ou en partie), un objet
//                 toujours invisible en marche (visible a FAUX sans expression
//                 ni script, opacite nulle, taille nulle), un objet cache dans
//                 l'editeur seulement (il se verra en marche) ;
//    LISIBILITE   un texte qui deborde de sa boite (mesure avec les polices de
//                 l'ecran quand l'ecran les donne) ; un texte qui se lit mal sur
//                 son fond (le contraste de la WCAG, 1:1 a 21:1) ;
//    NAVIGATION   une vue qu'aucune navigation n'ouvre (depuis la vue de
//                 demarrage et celles des groupes : actions, barres de
//                 navigation, plans a zones, fil d'Ariane, scripts), une popup
//                 qu'aucune action n'ouvre ;
//    ERGONOMIE    une cible trop petite pour un doigt (l'objet entier, ou ses
//                 parties : choix, fleches, cases, onglets) ;
//    PERFORMANCE  une vue trop chargee.
//
//  LES SEUILS sont ceux du projet (Configuration > Projet, Qualite) : la cible
//  tactile minimale, le contraste minimal, le nombre d'objets d'une vue
//  chargee. Chacun a 0 ne se controle pas ; la case Qualite les coupe tous.
// =============================================================================
#pragma once

#include "HmiCheck.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// La largeur d'une ligne de texte (px) dans une police ("Sans" ou une police
// ressource) a une taille (px). L'ecran la donne ; vide : estimeTextWidth.
using TextMeasure = std::function<double(std::string_view text, std::string_view font, double sizePx)>;

struct QualityOptions {
    int         touchMin{32};       // la plus petite cible tactile (px) ; 0 : pas de controle
    double      contrastMin{3.0};   // le contraste minimal texte / fond ; 0 : pas de controle
    int         heavyObjects{400};  // une vue de plus d'objets : chargee ; 0 : pas de controle
    TextMeasure measure;
};
// Les reglages du projet (Config::touchMin...), la mesure fournie.
[[nodiscard]] QualityOptions qualityOptions(const Project&, TextMeasure measure = {});

// "#RRGGBB", "#RGB", "#RRGGBBAA" (l'alpha ignore) -> 0xRRGGBB ; nullopt sinon.
[[nodiscard]] std::optional<std::uint32_t> parseRgb(std::string_view);
// La luminance relative (0 noir, 1 blanc) et le rapport de contraste (1 a 21).
[[nodiscard]] double relativeLuminance(std::uint32_t rgb);
[[nodiscard]] double contrastRatio(std::uint32_t a, std::uint32_t b);
// "4.5:1" -> la forme ecrite dans les messages ("4,5:1").
[[nodiscard]] std::string contrastText(double ratio);
// Une estimation de la largeur d'un texte (police proportionnelle ordinaire).
[[nodiscard]] double estimateTextWidth(std::string_view utf8, double sizePx);

// Les vues atteintes (vues ordinaires et popups) ; `dynamic` : un script ouvre
// une vue dont le nom se calcule (IHM_NAVIGUER(Nom_Vue)) - on ne sait pas laquelle.
[[nodiscard]] std::set<Id> reachableViews(const Project&, bool* dynamic = nullptr);

// Les controles de qualite, ajoutes a `out`.
void checkQuality(const Project&, const QualityOptions&, std::vector<Issue>& out);

} // namespace hmi
