#pragma once
// =============================================================================
//  app/tutorial/TutorialOverlay.hpp - le calque des tutoriels (1.11, T1)
// -----------------------------------------------------------------------------
//  Par-dessus l'espace de travail du bac a sable (la passe du dessus des
//  widgets, comme HmiTutorial) :
//   - ce que la scene montre : le voile et l'encadre orange (encadrer), le
//     curseur, le cercle du clic, la touche dessinee, la bulle (en tirets pour
//     "A toi", verte "Bravo", jaune "Presque") ;
//   - LA BARRE DE COMMANDE, en bas : Recommencer, <<, Lecture / Pause, >>,
//     "3 / 7" et le titre de l'etape, le temps "0:42 / 1:04", la barre
//     d'avancement et un repere cliquable par etape (vert : A toi possible),
//     0,5x 1x 2x, "pause apres chaque etape", A toi, Quitter.
//  Les touches : Espace, <- ->, A, Echap. Pendant la lecture, il MANGE souris
//  et clavier ; en "A toi", il laisse tout passer sauf sa bulle et sa barre.
//
//  La mise en page de la barre est une fonction pure (layoutTutorialBar) : le
//  dessin et les clics la partagent, et les essais la verifient sans ecran.
// =============================================================================

#include "TutorialStageApp.hpp"
#include "../../help/TutorialPlayer.hpp"
#include "../../ui/Widget.hpp"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app {

struct TutorialBarLayout {
    gfx::Rect bar, restart, previous, play, next, stepLabel, clock, track, pauseEach, aTry, quit;
    std::array<gfx::Rect, 3> speeds{};          // 0,5x 1x 2x
    std::vector<gfx::Rect> markers;             // un repere par etape, sur la barre d'avancement
};
inline constexpr std::array<double, 3> kTutorialSpeeds{0.5, 1.0, 2.0};

[[nodiscard]] TutorialBarLayout layoutTutorialBar(const gfx::Rect& screen, const help::CompiledTutorial& tutorial,
                                                  float height = 44.f);
// 1.11.1 (T1, R111-18) : une partie de la barre par son nom, pour la commande de session
// clic-souris lecteur:<nom> : lecture (Lecture / Pause), precedente, suivante, recommencer,
// atoi, quitter, pause-etape, horloge, piste, vitesse-1 a vitesse-3 (0,5x 1x 2x),
// etape-<n> (le repere de l'etape n, a partir de 1). Faux : nom inconnu ou rectangle vide.
[[nodiscard]] bool tutorialBarPart(const TutorialBarLayout& layout, std::string_view name, gfx::Rect& out);

class TutorialOverlay final : public ui::Widget {
public:
    TutorialOverlay(help::TutorialPlayer& player, TutorialStageApp& stage);

    std::function<void()> onQuit;               // Quitter : l'appli jette le bac a sable

    // A chaque image : la scene joue sa file, puis l'horloge du lecteur avance.
    void frame(double dtMs);
    void setScreen(const gfx::Rect& r) { screen_ = r; }
    // Le rectangle d'Aller a dans le bandeau du haut (TopBar::partRect("aller")) : la pastille
    // du bac a sable le recouvre en entier (tranche 5 : elle mordait sur sa fin, Ctrl+K coupe).
    void setGoTo(const gfx::Rect& r) { goTo_ = r; }
    // Tranche 10 : les elements du bandeau voisins d'Aller a (TopBar::partRect : simuler, arreter,
    // cycle, etat, ihm) ; le fond sous les pastilles des variantes recouvre en entier ceux qu'il touche.
    void setBarParts(std::vector<gfx::Rect> r) { barParts_ = std::move(r); }

    [[nodiscard]] bool requestsOverlayPass() const override { return true; }
    [[nodiscard]] gfx::Rect eventBounds() const override { return screen_; }
    [[nodiscard]] bool overlayCovers(gfx::Point p) const override;
    // Un clic dans la barre (pour les sessions et les essais) : vrai s'il a servi.
    // Tranche 9 : aussi sur le choix de la variante, a cote de la pastille du bac a sable.
    bool clickBar(gfx::Point p);
    // Les pastilles des variantes (bandeau du haut), telles que le dernier dessin les a posees.
    [[nodiscard]] const std::vector<std::pair<gfx::Rect, std::string>>& variantChips() const noexcept { return chips_; }
    [[nodiscard]] TutorialBarLayout barLayout() const { return layoutTutorialBar(screen_, player_.compiled()); }

protected:
    void onPaintOverlay(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& e) override;

private:
    [[nodiscard]] gfx::Rect bubbleRect(const ui::PaintContext& ctx, const std::string& text) const;

    help::TutorialPlayer& player_;
    TutorialStageApp& stage_;
    gfx::Rect screen_{};
    gfx::Rect goTo_{};
    std::vector<gfx::Rect> barParts_;           // les voisins d'Aller a dans le bandeau (setBarParts)
    int sinceVerify_ = 0;                      // A toi : la verification, trois fois par seconde
    mutable std::vector<std::pair<gfx::Rect, std::string>> chips_;   // le choix de la variante (dessin)
};

} // namespace app
