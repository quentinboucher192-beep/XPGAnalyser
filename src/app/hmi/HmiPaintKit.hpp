// =============================================================================
//  app/hmi/HmiPaintKit.hpp - la boite a outils du dessin des objets IHM
// -----------------------------------------------------------------------------
//  Ce que HmiPainter.cpp utilise pour dessiner chaque objet, partage avec les
//  fichiers qui en dessinent d'autres (lot 9 : HmiControlsPainter.cpp, les
//  commandes et les afficheurs) : le contexte d'un objet (ses coordonnees
//  locales passees par sa rotation, son miroir, le zoom), ses couleurs estompees
//  par son opacite, les textes alignes et tournes avec lui, les champs et les
//  boutons dessines.
//
//  Tout est en coordonnees LOCALES de l'objet (0,0 en haut a gauche, w x h) :
//  Ctx::map les met a l'ecran. Interne au dessin : rien ici n'est une API.
// =============================================================================
#pragma once

#include "HmiPainter.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../ui/Shapes.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi { struct FormState; class Runtime; struct LiveAlarm; }

namespace app::paint {

namespace shapes = ui::shapes;

[[nodiscard]] gfx::Color fade(gfx::Color c, float f);

// Un objet a dessiner : son rendu, sa vue, ses valeurs (statiques ou en marche).
struct Ctx {
    gfx::IRenderer&              r;
    const hmi::View&             view;
    const hmi::Object&           o;
    const HmiViewport&           vp;
    const HmiPropertySource&     src;
    const ui::Theme&             theme;
    const HmiPaintOptions&       opt;
    float                        alpha;     // opacite de l'objet (0..1)

    [[nodiscard]] gfx::Point map(double lx, double ly) const {
        const hmi::edit::Pt p = hmi::edit::toView(o, {lx, ly});
        return vp.toScreen(p.x, p.y);
    }
    [[nodiscard]] std::vector<gfx::Point> map(const std::vector<gfx::Point>& local) const {
        std::vector<gfx::Point> out;
        out.reserve(local.size());
        for (const auto& p : local) out.push_back(map(p.x, p.y));
        return out;
    }
    // La couleur `key` de l'objet (celle de la vue preparee), sinon `fallback`
    // telle qu'on la voit ; estompee par l'opacite.
    [[nodiscard]] gfx::Color color(std::string_view key, gfx::Color fallback = {0, 0, 0, 0}) const {
        gfx::Color col;
        return fade(tryParseColor(src.text(o, key), col) ? col : seen(fallback), alpha);
    }
    // Une couleur fixe du dessin (0xRRGGBB) telle qu'on la voit en marche (lot 13 :
    // daltonien, jour), estompee.
    [[nodiscard]] gfx::Color fixed(std::uint32_t rgb) const { return fade(seen(gfx::Color::rgb(rgb)), alpha); }
    // ... celle d'un texte (au jour : plus foncee qu'un fond, lisible) ;
    [[nodiscard]] gfx::Color fixedText(std::uint32_t rgb) const { return fade(hmiSeenColor(gfx::Color::rgb(rgb), opt.display, false, true), alpha); }
    // ... celle d'une encre posee sur une couleur franche (le blanc d'un bouton bleu : il le reste).
    [[nodiscard]] gfx::Color fixedOn(std::uint32_t rgb) const { return fade(hmiSeenColor(gfx::Color::rgb(rgb), opt.display, true), alpha); }
    [[nodiscard]] gfx::Color seen(gfx::Color col) const { return hmiSeenColor(col, opt.display); }
    [[nodiscard]] float stroke() const {
        return static_cast<float>(std::max(0.0, src.number(o, "strokeWidth", 1))) * vp.zoom;
    }
    [[nodiscard]] double w() const { return src.number(o, "w"); }
    [[nodiscard]] double h() const { return src.number(o, "h"); }
};

[[nodiscard]] std::vector<gfx::Point> rectLocal(double w, double h, double radius = 0);
// Rempli ("fill") et entoure ("stroke", "strokeWidth") : les couleurs de l'objet.
void fillAndStroke(const Ctx& c, const std::vector<gfx::Point>& local, bool closed = true);
// Un texte dans une boite locale : aligne ("gauche", "centre", "droite"), centre
// verticalement, tourne avec l'objet ; `sizePx` en pixels de vue.
void text(const Ctx& c, std::string_view raw, double bx, double by, double bw, double bh, gfx::Color col,
          double sizePx, std::string_view align, bool wrap = false);
// Ce qui tient dans la largeur (en pixels de vue), avec des points de suite.
[[nodiscard]] std::string fitted(const Ctx& c, const std::string& s, double maxLocal, double sizePx);
void placeholder(const Ctx& c, std::string_view label);
[[nodiscard]] std::vector<std::string> split(const std::string& s, char sep);
[[nodiscard]] gfx::Color accentOf(const Ctx& c);
void drawField(const Ctx& c, const hmi::Box& b, const std::string& shown, const std::string* beforeCaret, double fs,
               std::string_view align, gfx::Color textCol, const std::string& hint = {});
void drawFormMessage(const Ctx& c, const hmi::Box& b, const hmi::FormState* f, double fs);
void drawButtonBox(const Ctx& c, const hmi::Box& b, const std::string& label, gfx::Color fill, gfx::Color txt, double fs);

// ---- lot 9 (HmiControlsPainter.cpp) ----------------------------------------------
// Les commandes et les afficheurs ; faux : l'objet n'en est pas un.
bool drawLot9(const Ctx& c);
// Le panneau d'une liste deroulante ouverte, dessine par-dessus toute la vue.
void drawComboList(const Ctx& c);

// ---- lot 10 (HmiSynopticPainter.cpp) ----------------------------------------------
// Les symboles de synoptique ; faux : l'objet n'en est pas un.
bool drawSynoptic(const Ctx& c);

// ---- lot 11 ---------------------------------------------------------------------------
// La couleur d'une priorite d'alarme (1 rouge, 2 orange, 3 jaune, 4 bleu).
[[nodiscard]] gfx::Color priorityColor(int priority);
// 1.10.2 (AL) : la couleur d'une alarme a l'execution - celle de son groupe d'alarmes
// (IHM > Alarmes > Groupes : active, acquittee, disparue) quand elle est reglee, sinon
// celle de sa priorite.
[[nodiscard]] gfx::Color alarmColor(const hmi::Runtime* rt, const hmi::LiveAlarm& a);
// Un texte coupe a la largeur `maxPx` (pixels ecran), mot a mot.
[[nodiscard]] std::string wrapText(const Ctx& c, std::string_view s, gfx::FontId f, float maxPx);
// Les graphiques (HmiChartsPainter.cpp) ; les objets des alarmes et de la
// production (HmiLot11Painter.cpp). Faux : l'objet n'en est pas un.
bool drawCharts(const Ctx& c);
bool drawLot11(const Ctx& c);

// ---- lot 12 (HmiNavPainter.cpp) -------------------------------------------------------
// La navigation et la structure (barre de navigation, fil d'Ariane, conteneur a
// onglets, cadre, panneaux, plan a zones). Faux : l'objet n'en est pas un.
bool drawLot12(const Ctx& c);
// Une image des ressources, par son nom, dans une boite locale ("ajuster",
// "etirer", "aucun") ; faux : pas d'image de ce nom.
bool drawNamedImage(const Ctx& c, std::string_view name, std::string_view mode, double lx, double ly, double lw, double lh);

// ---- lot 13 (HmiLot13Painter.cpp) -----------------------------------------------------
// Le selecteur de langue, le selecteur de theme. Faux : l'objet n'en est pas un.
bool drawLot13(const Ctx& c);
// L'accessibilite : un symbole dans un voyant (1 coche, 2 croix, 3 point
// d'exclamation, 4 tiret), centre en (cx, cy), de cote `size` (repere de
// l'objet), clair sur une couleur sombre, sombre sur une claire.
void drawStatusSymbol(const Ctx& c, int symbol, double cx, double cy, double size, gfx::Color on);
// Un petit triangle "!" en haut a droite : un defaut qui se lit sans la couleur.
void drawFaultBadge(const Ctx& c, double right, double top, double size);

// ---- lot 14 (HmiLot14Painter.cpp) -----------------------------------------------------
// L'etat de la communication, le diagnostic automate. Faux : l'objet n'en est pas un.
bool drawLot14(const Ctx& c);

} // namespace app::paint
