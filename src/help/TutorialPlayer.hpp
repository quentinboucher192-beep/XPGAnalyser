#pragma once
// L'automate du lecteur de tutoriels (1.11, D2) : lecture, pause, vitesse,
// etapes, "pause apres chaque etape", rejouer sans animation, "A toi".
//
// Il ne dessine rien : il parle a une SCENE (TutorialStage). La scene reelle
// (tranche 2) rejoue l'editeur dans l'appli sur le bac a sable ; les essais lui
// donnent une scene factice. L'horloge vient de l'appelant (tick), donc une
// session de captures, a 1/30 s par image, est reproductible.
//
// REVENIR EN ARRIERE REMET L'ETAT EXACT : seek() part d'une copie neuve du bac
// a sable (reset) et rejoue sans animation tout ce qui precede l'instant vise.
// Precedent, repere de la barre, Recommencer, "Montre-moi" passent tous par la.

#include "help/Tutorial.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace help {

class TutorialStage {
public:
    virtual ~TutorialStage() = default;
    // Une copie NEUVE du bac a sable, la vue de @bac ouverte.
    virtual void reset(const CompiledTutorial& tutorial) = 0;
    // Joue la portion [from, to] (0..1) du geste ; animated = faux pour rejouer
    // d'un trait (seek). Un texte tape les lettres de from a to ; un glisser
    // relache a 1. Rejouer [0, f] d'un coup doit donner le meme etat que
    // l'avoir joue en plusieurs morceaux.
    virtual void advance(const Gesture& gesture, double from, double to, bool animated) = 0;
    // L'etape commence (sa bulle, son titre, son numero).
    virtual void showStep(const CompiledTutorial& tutorial, std::size_t step) { (void)tutorial; (void)step; }
    // "A toi" : l'appli repond a l'utilisateur (vrai) ou au tutoriel (faux).
    virtual void setInteractive(bool on) { (void)on; }
    // La scene reelle rejoue dans l'appli, qui a besoin d'images entre deux gestes
    // (une mise en page, un panneau qui s'ouvre) : tant qu'elle est occupee (une
    // remise a neuf, des gestes en file), l'horloge du lecteur attend.
    [[nodiscard]] virtual bool busy() const { return false; }
    // Lit un chemin de condition (objet(V_201).valveType, simulation.ihm...) sur le bac a sable.
    [[nodiscard]] virtual std::optional<std::string> read(std::string_view path) { (void)path; return std::nullopt; }
};

enum class PlayerState { Playing, Paused, StepEnd, ATry, Finished };

class TutorialPlayer {
public:
    TutorialPlayer(const Tutorial& tutorial, TutorialStage& stage, std::string_view variant = {});

    // --- Ce que l'ecran affiche ---------------------------------------------
    [[nodiscard]] const CompiledTutorial& compiled() const { return compiled_; }
    [[nodiscard]] PlayerState state() const { return state_; }
    [[nodiscard]] std::size_t step() const { return step_; }
    [[nodiscard]] double stepTimeMs() const { return stepTime_; }
    [[nodiscard]] double timeMs() const;
    [[nodiscard]] double speed() const { return speed_; }
    [[nodiscard]] bool pauseAfterEachStep() const { return pauseAfterEachStep_; }
    [[nodiscard]] const CheckOutcome& lastOutcome() const { return outcome_; }
    [[nodiscard]] bool canATry() const;
    [[nodiscard]] std::string progressLabel() const;   // "3 / 7"
    [[nodiscard]] std::string clockLabel() const;      // "0:42 / 1:04"

    // --- Les commandes ------------------------------------------------------
    // Ouvre le tutoriel a une etape (le centre d'aide : une etape cliquee, "Me montrer").
    void start(std::size_t step = 0, bool paused = false);
    // Avance l'horloge de dtMs (temps reel) : dtMs x vitesse de tutoriel.
    void tick(double dtMs);
    void play();
    void pause();
    void togglePlay();                 // Espace
    void seek(std::size_t step, double ms = 0);
    void seekTime(double ms);          // un clic dans la barre d'avancement
    void previous();                   // <- : recommence l'etape apres 1,5 s, sinon celle d'avant
    void next();                       // ->
    void restart();                    // Recommencer
    void setSpeed(double speed);       // 0,5x 1x 2x
    void setPauseAfterEachStep(bool on);
    // Recompile pour une autre variante ; keepStep : garde l'etape (un @onok).
    void setVariant(std::string_view variant, bool keepStep = false);

    // --- A toi ----------------------------------------------------------------
    [[nodiscard]] bool startATry();    // A : le debut de l'etape, l'appli repond a l'utilisateur
    CheckOutcome verify();             // apres chaque geste de l'utilisateur
    void showMe();                     // Montre-moi : rejoue l'etape
    void retryATry();                  // Recommencer l'etape, toujours en A toi
    void leaveATry();                  // Echap

    // Le nombre de remises a neuf du bac a sable (pour les essais et le verificateur).
    [[nodiscard]] int resets() const { return resets_; }

    // 1.11.2 (R1112-4) : le mot du lecteur a l'ouverture (TutorialStart::notice : « Essayer » ouvre
    // ailleurs que demande, ou le tutoriel n'a aucun A toi) ; dit tant que l'etape d'ouverture dure.
    void setNotice(std::string text) { notice_ = std::move(text); noticeStep_ = step_; }
    [[nodiscard]] std::string notice() const {
        return step_ == noticeStep_ && state_ != PlayerState::Finished ? notice_ : std::string();
    }

private:
    void enterStep(std::size_t step);
    void playRange(double from, double to, bool animated);
    void finishStep();

    const Tutorial& tutorial_;
    TutorialStage& stage_;
    CompiledTutorial compiled_;
    PlayerState state_ = PlayerState::Paused;
    std::size_t step_ = 0;
    double stepTime_ = 0;
    double speed_ = 1.0;
    bool pauseAfterEachStep_ = false;
    CheckOutcome outcome_;
    int resets_ = 0;
    std::string notice_;
    std::size_t noticeStep_ = 0;
};

} // namespace help
