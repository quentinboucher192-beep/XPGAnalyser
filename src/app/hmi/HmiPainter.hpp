// =============================================================================
//  app/hmi/HmiPainter.hpp — dessiner une vue IHM
// -----------------------------------------------------------------------------
//  UN SEUL DESSIN POUR L'EDITEUR ET POUR LA SIMULATION. Ce qui change entre
//  les deux, c'est d'ou viennent les valeurs : la valeur statique de chaque
//  propriete dans l'editeur, le resultat de son expression en marche.
//  `PropertySource` est ce point de passage ; le dessin ne sait pas lequel des
//  deux il a en face.
//
//  Les formes passent par hmi::edit::toView (miroir puis rotation autour du
//  pivot, exactement comme le choix a la souris) puis par le zoom et le
//  defilement de l'ecran : ce qu'on voit est ce qu'on clique.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../platform/Renderer.hpp"
#include "../../ui/Theme.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace hmi { class Runtime; struct History; struct DisplayOptions; }

namespace app {

// Ou vont les coordonnees de la vue a l'ecran.
struct HmiViewport {
    float originX{0}, originY{0};   // l'ecran du point (0, 0) de la vue
    float zoom{1};
    // Toute la vue tournee (une transition "rotation") : de `angle` degres, sens
    // horaire, autour de (pivotX, pivotY) a l'ecran. 0 dans l'editeur.
    float angle{0}, pivotX{0}, pivotY{0};
    [[nodiscard]] gfx::Point toScreen(double x, double y) const noexcept {
        gfx::Point p{originX + static_cast<float>(x) * zoom, originY + static_cast<float>(y) * zoom};
        if (angle != 0.f) {
            const float rad = angle * 3.14159265f / 180.f, c = std::cos(rad), s = std::sin(rad);
            const float dx = p.x - pivotX, dy = p.y - pivotY;
            p = {pivotX + dx * c - dy * s, pivotY + dx * s + dy * c};
        }
        return p;
    }
    // L'inverse, sans rotation (l'editeur, et les clics hors transition).
    [[nodiscard]] double toViewX(float sx) const noexcept { return (sx - originX) / zoom; }
    [[nodiscard]] double toViewY(float sy) const noexcept { return (sy - originY) / zoom; }
};

class HmiPropertySource {
public:
    virtual ~HmiPropertySource() = default;
    // La valeur a dessiner, en texte. Par defaut : la valeur statique.
    [[nodiscard]] virtual std::string text(const hmi::Object& o, std::string_view key,
                                           std::string_view fallback = {}) const {
        return o.text(key, fallback);
    }
    [[nodiscard]] double number(const hmi::Object& o, std::string_view key, double fallback = 0) const;
    [[nodiscard]] bool   flag(const hmi::Object& o, std::string_view key, bool fallback = false) const;
};

struct HmiPaintOptions {
    bool editor{true};            // montrer les trous "{...}" tels quels, le cadre des objets vides
    bool showEditorHidden{false}; // dessiner aussi les objets caches dans l'editeur (estompes)
    hmi::Id insideGroup{hmi::kNoId};   // estomper ce qui est hors du groupe edite
    double time{0};               // en marche : l'heure, pour le clignotement
    // Les ressources et fichiers externes du projet : images, polices, videos,
    // donnees des tableaux. Nul : cadres de remplacement (tests, apercus).
    const hmi::Assets* assets{nullptr};
    // Toute la vue plus ou moins transparente (une transition en fondu).
    float alpha{1.f};
    // EN MARCHE (lot 4) : le moteur IHM - les mesures des courbes en temps reel,
    // les alarmes actives, les evenements, le journal - et l'historique garde
    // (courbes en mode historique, alarmes terminees). Nuls dans l'editeur :
    // une courbe y montre un apercu, un historique son en-tete.
    const hmi::Runtime* runtime{nullptr};
    const hmi::History* history{nullptr};
    // LOT 6 : le projet - les recettes du gestionnaire de recettes. Nul : un
    // gestionnaire montre son cadre et ses boutons, sans jeux.
    const hmi::Project* project{nullptr};
    // LOT 13 : en marche, les couleurs telles qu'on les voit (daltonien, jour).
    // La vue preparee a deja change celles des objets ; le dessin passe par la
    // les siennes (le fond d'un champ, une fleche, une couleur par defaut).
    // Nul : telles quelles (l'editeur, la nuit sans daltonien).
    const hmi::DisplayOptions* display{nullptr};
};

// Les couleurs des plumes d'une courbe, dans l'ordre, quand "colors" n'en dit
// pas assez ; et les sources d'un objet Historique.
[[nodiscard]] const std::vector<std::string>& hmiTrendPalette();
[[nodiscard]] const std::vector<std::string>& hmiHistorySources();   // alarmes, acquittees, historique, evenements, systeme

[[nodiscard]] gfx::Color parseColor(std::string_view text, gfx::Color fallback = {0, 0, 0, 0});
// Vrai : une couleur lue (#RRGGBB, #RRGGBBAA, 16#RRGGBB).
[[nodiscard]] bool tryParseColor(std::string_view text, gfx::Color& out);
// Une couleur fixe du dessin telle qu'on la voit (lot 13) ; nul : telle quelle.
// `onColored` : une encre posee sur une couleur franche ; `isText` : un texte.
[[nodiscard]] gfx::Color hmiSeenColor(gfx::Color, const hmi::DisplayOptions*, bool onColored = false, bool isText = false);

void paintHmiView(gfx::IRenderer&, const hmi::View&, const HmiViewport&, const HmiPropertySource&,
                  const ui::Theme&, const HmiPaintOptions& = {});
void paintHmiObject(gfx::IRenderer&, const hmi::View&, const hmi::Object&, const HmiViewport&,
                    const HmiPropertySource&, const ui::Theme&, const HmiPaintOptions& = {});
// Lot 10 : le dessin d'un symbole (ses objets, tels que dans son editeur),
// ajuste dans `area` : les tuiles et l'apercu de la bibliotheque.
void paintHmiSymbolPreview(gfx::IRenderer&, const hmi::Project&, const hmi::View& symbol, const gfx::Rect& area,
                           const ui::Theme&);
// Lot 12 : la vignette d'une vue (son fond, ses objets - et ceux de l'ecran
// modele dont elle herite), ajustee dans `area` : l'apercu au survol.
void paintHmiViewPreview(gfx::IRenderer&, const hmi::Project&, const hmi::View& view, const gfx::Rect& area, const ui::Theme&);

} // namespace app
