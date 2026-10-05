// =============================================================================
//  app/NoveltyCenter.hpp - 1.10 (chantier P) : les nouveautes dans l'application
// -----------------------------------------------------------------------------
//  Le registre (help/Novelties) dit QUOI est nouveau ; ce centre le MONTRE :
//   - la fenetre "Nouveautes de la <version>" (NoveltyDialog : une carte par
//     nouveaute, ui::novelty::Board) au premier lancement d'une version plus
//     recente, et du menu Aide (action help.news) ; l'accueil la propose ;
//   - "Me montrer" : l'endroit ouvert (Item::go), l'element encadre en orange
//     et la bulle (ui::novelty::Spotlight), Suivante, Terminer ;
//   - les reperes orange (ui::novelty::Marks) sur l'ecran du dessus, retires au
//     premier clic sur l'element ; "Masquer les reperes des nouveautes"
//     (action help.newsMarks, et la case de la fenetre) ;
//   - l'etat dans les reglages (cles nouveautes.*), ecrit des qu'il change
//     (l'aide le change aussi : Tout marquer comme lu, le filtre).
//  EN MODE SCRIPT (--script) : rien ne s'ouvre de soi-meme et les reperes sont
//  masques - une session de captures n'a pas d'orange par surprise ; les
//  commandes nouveautes-* les demandent (ScriptRunner).
//  Un seul centre par application (noveltyCenter()) : App.cpp l'appelle a
//  chaque image (tick, paint, handle) ; la barre du haut, l'accueil et les
//  scripts passent par lui.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../ui/NoveltyMarks.hpp"

#include <string>
#include <vector>

namespace app {

class App;

class NoveltyCenter {
public:
    // Au lancement (App::create, apres les reglages) : l'etat, la version
    // lancee ; la fenetre attend l'accueil (windowPending).
    void start(App& app, bool existingProfile);
    [[nodiscard]] bool windowPending() const noexcept { return pending_; }
    // La fenetre des nouveautes (automatic : celle du premier lancement).
    void openBoard(bool automatic = false);
    // Me montrer : cette nouveaute ; `tour` : les suivantes (Suivante).
    void showMe(const std::string& id, std::vector<std::string> tour = {});
    void showAll();                   // "Me montrer tout, une a une"
    void next();
    void endTour();
    [[nodiscard]] bool touring() const noexcept { return spot_.active(); }
    [[nodiscard]] const std::string& current() const noexcept { return currentId_; }
    [[nodiscard]] std::size_t tourIndex() const noexcept { return tourIndex_; }
    [[nodiscard]] std::size_t tourSize() const noexcept { return tour_.size(); }
    // Les reperes : masques (le reglage) ; en mode script, seulement sur demande.
    void setMarksHidden(bool hidden);
    [[nodiscard]] bool marksHidden() const;
    void setScriptMarks(bool on) { scriptMarks_ = on; }
    // Chaque image : la fenetre a ouvrir, l'endroit a ouvrir, les reglages.
    void tick();
    // Apres tout le reste : les reperes, puis la bulle.
    void paint(gfx::IRenderer& r, const ui::Theme& t, gfx::Size surface);
    // Avant les ecrans : la bulle prend ses clics ; un clic sur un element
    // marque l'utilise (son repere s'en va) et passe dessous.
    [[nodiscard]] ui::EventResult handle(const ui::InputEvent& ev);
    [[nodiscard]] ui::novelty::Marks& marks() noexcept { return marks_; }
    [[nodiscard]] ui::novelty::Spotlight& spotlight() noexcept { return spot_; }
    // L'etat, en une ligne (scripts : nouveautes-etat).
    [[nodiscard]] std::string describe() const;
    // Les essais : relancer comme la version `current`, venu de `previous`
    // ("" : un profil neuf) - l'etat repart de zero, la fenetre est due.
    void relaunch(const std::string& current, const std::string& previous);
    // Le dernier endroit ouvert par Me montrer ("" : aucun) et s'il a ete trouve.
    [[nodiscard]] const std::string& lastGo() const noexcept { return lastGo_; }
    [[nodiscard]] bool targetFound() const noexcept { return targetFound_; }

private:
    void saveIfChanged(bool force = false);
    void go(const std::string& where);
    bool stepGo();
    void present();
    [[nodiscard]] ui::Widget* topRoot() const;

    App*                     app_{nullptr};
    bool                     pending_{false};
    bool                     scripted_{false};
    bool                     scriptMarks_{false};
    std::string              saved_;
    std::string              currentId_;
    std::vector<std::string> tour_;
    std::size_t              tourIndex_{0};
    std::string              goPending_, lastGo_;
    int                      goTries_{0};
    int                      frames_{0};
    bool                     targetFound_{false};
    ui::novelty::Marks       marks_;
    ui::novelty::Spotlight   spot_;
    core::ConnectionScope    links_;
};

// Le centre de l'application (un par processus).
[[nodiscard]] NoveltyCenter& noveltyCenter();

} // namespace app
