// =============================================================================
//  ui/NoveltyMarks.hpp - 1.10 (chantier P) : les reperes orange des nouveautes
// -----------------------------------------------------------------------------
//  UN CALQUE PAR-DESSUS L'INTERFACE, QUI NE PREND RIEN. Chaque widget dont
//  l'identifiant (Widget::id()) est dans la liste des cibles et qui est a
//  l'ecran (lui et tous ses parents montres, dans leurs bornes) est encadre en
//  orange, avec une petite pastille "NOUVEAU". Le calque ne dessine que ce
//  qu'il a trouve a la derniere image (collect) : il ne connait ni l'IHM ni
//  l'automate - c'est l'application qui lui dit quoi marquer (le registre
//  help/Novelties) et ce que fait un clic sur un element marque (hit : la
//  nouveaute est utilisee, son repere s'en va). Un clic passe toujours dessous.
//
//  LA BULLE DE "ME MONTRER" (Spotlight, la maquette scene 8) : l'ecran voile
//  sauf l'element, encadre en orange, et une bulle a cote - l'etiquette
//  "NOUVEAU . 1.10", le titre, la phrase, "2 / 9", Terminer et Suivante (a la
//  derniere : Revoir la liste). Seuls les clics sur la bulle sont pris ;
//  Echap la ferme.
//
//  Les deux se dessinent APRES tout le reste (App::frame) : au-dessus des
//  ecrans, des dialogues et des listes ouvertes.
// =============================================================================
#pragma once

#include "Theme.hpp"
#include "Widget.hpp"
#include "../core/Signal.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui::novelty {

// L'orange des nouveautes (le meme dans l'aide, les cartes et les reperes).
[[nodiscard]] gfx::Color orange(const Theme& t) noexcept;
// Un cadre orange (epaisseur en pixels) autour de `r`, un peu au large.
void drawFrame(gfx::IRenderer& r, const Theme& t, const gfx::Rect& rect, float thickness = 2.f);
// La pastille "NOUVEAU" (ou un autre texte) : sa taille, et la dessiner.
[[nodiscard]] gfx::Size pillSize(const gfx::IRenderer& r, const Theme& t, std::string_view text);
void drawPill(gfx::IRenderer& r, const Theme& t, gfx::Point topLeft, std::string_view text);
inline constexpr const char* kPillText = "NOUVEAU";

// Le rectangle a l'ecran d'un widget : lui et tous ses parents montres,
// coupe par leurs bornes. Faux : il n'est pas a l'ecran (cache, replie, defile
// hors de son panneau, de taille nulle).
[[nodiscard]] bool visibleRect(const Widget& w, gfx::Rect& out);
// Un identifiant de widget et un motif du registre : '*' vaut n'importe quelle
// suite ("hmi.view.*.props", "*.palette") ; sans '*' : egaux.
[[nodiscard]] bool idMatches(std::string_view id, std::string_view pattern) noexcept;
// Le premier widget de cet identifiant (ou motif) qui est a l'ecran (nul : aucun).
[[nodiscard]] Widget* findVisible(Widget& root, std::string_view id);
// UNE PARTIE D'UN WIDGET : "help.hmi.pane.tools#Nouveautes seulement" (un bouton
// d'une barre d'outils), "hmi.simulation.bar#zoom"... L'application dit ou est
// la partie (ui ne connait pas ses barres) ; faux : pas de partie de ce nom.
using PartFinder = std::function<bool(Widget& w, std::string_view part, gfx::Rect& out)>;
void setPartFinder(PartFinder f);
// 1.10 (H) : l'etiquette d'une version pour le lecteur - "NOUVEAU . 1.9" si elle
// est plus recente que la derniere qu'il a vue, "" sinon. L'application la donne
// (help::news) ; sans elle : "". L'onglet Aide general s'en sert pour encadrer
// ses sections des nouveautes (ancres "nouveautes-1-9", "nouveautes-1-10").
using SinceLabeler = std::function<std::string(std::string_view since)>;
void setSinceLabeler(SinceLabeler f);
[[nodiscard]] std::string sinceLabel(std::string_view since);
// Le rectangle a l'ecran d'une cible du registre : "id", "motif*" ou "id#partie".
[[nodiscard]] bool targetRect(Widget& root, std::string_view spec, gfx::Rect& out);

class Marks {
public:
    struct Target {
        std::string widget;    // l'identifiant du widget
        std::string key;       // la nouveaute (son identifiant dans le registre)
    };
    struct Mark {
        std::string key, widget;
        gfx::Rect   rect{};
    };
    void setTargets(std::vector<Target> targets) { targets_ = std::move(targets); }
    [[nodiscard]] const std::vector<Target>& targets() const noexcept { return targets_; }
    // Les reperes a l'ecran dans l'arbre `root` (a refaire a chaque image ;
    // nul : aucun). Un widget marque deux fois n'a qu'un repere.
    const std::vector<Mark>& collect(Widget* root);
    [[nodiscard]] const std::vector<Mark>& marks() const noexcept { return marks_; }
    // La nouveaute dont le repere est sous ce point ("" : aucune). Le widget
    // lui-meme compte (un clic dessus l'utilise), pas la pastille seule.
    [[nodiscard]] std::string hit(gfx::Point p) const;
    void clear() { marks_.clear(); }
    void paint(gfx::IRenderer& r, const Theme& t) const;

private:
    std::vector<Target> targets_;
    std::vector<Mark>   marks_;
};

class Spotlight {
public:
    // Montrer une nouveaute : son titre, sa phrase, son rang (index sur count),
    // et ce qui la mene ("Version 1.10"). `target` : le rectangle de l'element
    // (rien : la bulle au centre, l'element n'est pas a l'ecran).
    void show(std::string title, std::string text, std::size_t index, std::size_t count, std::string caption = {});
    void setTarget(std::optional<gfx::Rect> target) { target_ = target; }
    [[nodiscard]] const std::optional<gfx::Rect>& target() const noexcept { return target_; }
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    // Une phrase sous le texte quand l'element n'a pas ete trouve ("Ouvre un projet...").
    void setNote(std::string note) { note_ = std::move(note); }

    // Dessine la bulle (et le cadre de l'element) dans une surface de cette taille.
    void paint(gfx::IRenderer& r, const Theme& t, gfx::Size surface);
    // Un evenement : Consumed s'il est pour la bulle (un clic sur elle, Echap).
    [[nodiscard]] EventResult handle(const InputEvent& ev);

    // Next : Suivante (a la derniere : "Revoir la liste", qui rouvre la
    // fenetre) ; End : Terminer ; Close : la croix ; Help : Voir l'aide.
    enum class Part : std::uint8_t { Next, Close, Help, End };
    [[nodiscard]] gfx::Rect partRect(Part p) const noexcept;
    [[nodiscard]] gfx::Rect bubbleRect() const noexcept { return bubble_; }
    // Les gestes des boutons, sans clic (scripts, tests).
    void press(Part p);

    // Suivante (ou Terminer, a la derniere) ; la croix ou Echap ; "Voir l'aide".
    const core::SignalPtr<> nextRequested  = core::Signal<>::create();
    const core::SignalPtr<> closeRequested = core::Signal<>::create();
    const core::SignalPtr<> helpRequested  = core::Signal<>::create();
    const core::SignalPtr<> boardRequested = core::Signal<>::create();   // "Revoir la liste"
    // Le bouton "Voir l'aide" n'est la que si la nouveaute a un sujet.
    void setHelpOffered(bool on) { helpOffered_ = on; }

private:
    void layout(const gfx::IRenderer& r, const Theme& t, gfx::Size surface);

    bool                     active_{false};
    std::string              title_, text_, caption_, note_;
    std::size_t              index_{0}, count_{0};
    std::optional<gfx::Rect> target_;
    bool                     helpOffered_{false};
    gfx::Rect                bubble_{}, next_{}, close_{}, help_{}, end_{};
    int                      hover_{-1};
};

} // namespace ui::novelty
