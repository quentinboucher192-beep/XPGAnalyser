#pragma once
// =============================================================================
//  app/tutorial/TutorialStageApp.hpp - la scene reelle des tutoriels (1.11, T1)
// -----------------------------------------------------------------------------
//  Le lecteur (help::TutorialPlayer) dit QUOI jouer ; cette scene le joue dans
//  la vraie appli, par app::UiDriver (comme une session de ScriptRunner), sur le
//  bac a sable.
//
//  LES GESTES PASSENT PAR UNE FILE : l'appli a besoin d'images entre deux gestes
//  (une mise en page, la bibliotheque qui montre les variantes, une case revelee).
//  advance() met en file ; frame(), appele a chaque image, joue la tete de file :
//  un geste qui envoie de l'entree par image, ceux qui ne font que montrer
//  (encadrer, dire, attendre) d'un coup. Une cible pas encore la : on reessaie a
//  l'image suivante (kRetryFrames au plus), puis elle est comptee introuvable
//  (le verificateur en fait un echec). Tant que la file n'est pas vide, busy()
//  est vrai et l'horloge du lecteur attend : rejouer sans animation (seek) donne
//  le meme etat, quelques images plus tard.
//
//  Ce que l'appli branche (tranche 3, App) :
//   - openSandbox : jette le bac a sable, en recopie un neuf (ProjectStore::
//     duplicate depuis le projet de demonstration ou Armoire_Gaz), l'ouvre et
//     ouvre la vue de @bac ; rend false tant que ce n'est pas fait (busy) ;
//   - prepare : une commande de @avant (vue, poser, regler) sur le document ;
//   - reader : un chemin de condition (objet(V_201).valveType...).
//  Sans eux (les essais), reset ne fait que vider la file et les reperes.
// =============================================================================

#include "UiDriver.hpp"
#include "../../help/TutorialPlayer.hpp"
#include "../../help/TutorialSpot.hpp"

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace app {

class TutorialStageApp final : public help::TutorialStage {
public:
    using SandboxOpener = std::function<bool(const help::CompiledTutorial&)>;
    using Preparer      = std::function<bool(const std::vector<std::string>& args)>;
    using Reader        = std::function<std::optional<std::string>(std::string_view path)>;

    static constexpr int kRetryFrames = 45;   // 1,5 s a 30 images par seconde

    explicit TutorialStageApp(UiDriver& driver) : driver_(driver) {}

    void setSandboxOpener(SandboxOpener f) { openSandbox_ = std::move(f); }
    void setPreparer(Preparer f) { prepare_ = std::move(f); }
    void setReader(Reader f) { reader_ = std::move(f); }

    // --- help::TutorialStage ---------------------------------------------------
    void reset(const help::CompiledTutorial& tutorial) override;
    void advance(const help::Gesture& gesture, double from, double to, bool animated) override;
    void showStep(const help::CompiledTutorial& tutorial, std::size_t step) override;
    // 1.11.2 : en A toi, la tuile a glisser (TutorialATry::show) est amenee a l'ecran et encadree.
    void setInteractive(bool on) override;
    [[nodiscard]] bool busy() const override { return sandboxPending_ || settle_ > 0 || !queue_.empty(); }
    [[nodiscard]] std::optional<std::string> read(std::string_view path) override;

    // A chaque image (avant le dessin) : joue la tete de file.
    void frame();

    // --- Ce que le calque dessine ---------------------------------------------
    [[nodiscard]] gfx::Point cursor() const noexcept { return cursor_; }
    // Tranche 23 (R111-15) : l'encadre de sa cible retrouvee a cette image (help::TutorialSpot).
    [[nodiscard]] const std::optional<gfx::Rect>& spot() const noexcept { return spot_.rect(); }
    // 1.11.2 (T1, R1112-8) : la cible de cet encadre (vide : aucun), pour la commande de session tutoriel-cadre.
    [[nodiscard]] const std::string& spotTarget() const noexcept { return spot_.target(); }
    [[nodiscard]] const std::string& say() const noexcept { return say_; }
    [[nodiscard]] const std::string& keyShown() const noexcept { return key_; }     // la touche dessinee
    [[nodiscard]] int clickFlash() const noexcept { return clickFlash_; }           // images restantes du cercle
    [[nodiscard]] bool interactive() const noexcept { return interactive_; }
    // "Remise en place..." : seulement quand la scene rattrape vraiment (tranche 12, vu par I111) :
    // le bac a sable qui s'ouvre, ou des gestes rejoues sans animation (seek). Pendant la lecture,
    // la tranche du geste en cours, une cible qu'on attend ou le repere d'etape n'en sont pas.
    [[nodiscard]] bool catchingUp() const noexcept {
        if (sandboxPending_ || settle_ > 0) return true;
        for (const auto& p : queue_)
            if (!p.stepMark && !p.animated) return true;
        return false;
    }

    // --- Le verificateur --------------------------------------------------------
    [[nodiscard]] const std::vector<std::string>& missing() const noexcept { return missing_; }
    void clearMissing() { missing_.clear(); }

private:
    struct Pending {
        help::Gesture gesture;
        double from = 0, to = 1;
        bool animated = false;
        int retries = 0;
        bool stepMark = false;        // showStep, dans l'ordre de la file (sa bulle, encadre efface)
        std::string bubble;
    };
    // Rend false : a reessayer a l'image suivante (cible pas encore la).
    bool play(Pending& p);
    std::optional<gfx::Rect> find(const std::string& target, Pending& p);

    UiDriver& driver_;
    SandboxOpener openSandbox_;
    Preparer prepare_;
    Reader reader_;
    const help::CompiledTutorial* tutorial_ = nullptr;
    std::size_t shownStep_ = 0;   // 1.11.2 : l'etape de la derniere showStep (son A toi, sa tuile)
    bool sandboxPending_ = false;
    // Tranche 25 (R111-12) : les images a laisser passer apres l'ouverture du bac, avant le premier
    // geste. L'ecran d'analyse du bac (SwitchMenu, en file) n'a encore ni mise a jour ni dessin :
    // le premier geste qui envoie de l'entree s'y perdait (clic barre:? sans menu, F1 sans effet).
    static constexpr int kSettleFrames = 2;
    int settle_ = 0;
    std::deque<Pending> queue_;
    std::vector<std::string> missing_;
    gfx::Point cursor_{};
    gfx::Point dragFrom_{};
    help::TutorialSpot spot_;     // tranche 23 (R111-15) : l'encadre et sa cible, suivie a chaque image (frame())
    std::string say_, key_;
    int clickFlash_ = 0;
    bool interactive_ = false;
};

} // namespace app
