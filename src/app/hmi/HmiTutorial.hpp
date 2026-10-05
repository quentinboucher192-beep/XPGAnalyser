// =============================================================================
//  app/hmi/HmiTutorial.hpp - le didacticiel de l'IHM : visites et parcours
// -----------------------------------------------------------------------------
//  PAR-DESSUS L'ECRAN, UNE ETAPE A LA FOIS. La zone decrite (une entree de
//  l'arbre, un volet, un bouton, un objet de la vue) est entouree d'un trait ;
//  une bulle a cote dit ce qu'elle fait.
//
//  DEUX SORTES D'ETAPES.
//   - Une etape de VISITE montre : tout s'assombrit sauf la zone, Suivant /
//     Precedent / Passer, au clic ou au clavier (Entree ou fleche droite,
//     fleche gauche, Echap). Elle prend tous les evenements : une visite
//     qu'un clic de travers ferme ou contourne ne guide plus.
//   - Lot 21 : une etape INTERACTIVE attend un geste et le verifie (`done`,
//     interroge quelques fois par seconde par poll()). Rien ne s'assombrit et
//     tout passe dessous, sauf ce qui tombe sur la bulle : c'est l'utilisateur
//     qui fait. Des que le geste est fait, l'etape suivante vient, et sa bulle
//     dit "Etape 3 reussie : 5 objets choisis". "Montre-moi" le fait a sa
//     place (`showMe`). Une etape deja faite en y arrivant (Precedent, un
//     parcours repris) le dit, et attend Suivant.
//  Au bout d'un parcours interactif : "Garder" ce qu'il a cree, ou "Tout
//  defaire" (l'historique revient a l'etat d'avant) - si l'ecran a donne de
//  quoi defaire (setUndo).
//
//  LE WIDGET NE CONNAIT PAS L'IHM : il recoit des etapes (un titre, un texte,
//  une fonction qui rend le rectangle a eclairer - calcule a chaque image, la
//  mise en page peut bouger - et une fonction a lancer en arrivant sur
//  l'etape). C'est l'ecran qui les compose (HmiWorkspace.cpp, pour la visite ;
//  TutorialWorkspace.cpp, pour les parcours).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app {

class HmiTutorial final : public ui::Widget {
public:
    struct Step {
        std::string                    title;
        std::string                    text;
        std::function<bool(gfx::Rect&)> target;   // vide ou faux : la bulle au centre
        std::function<void()>          enter;     // en arrivant sur l'etape (deplier, ouvrir)
        // ---- lot 21 : une etape interactive (done non vide) ----------------------
        std::function<bool(std::string& what)> done{};   // le geste est-il fait ? (what : ce qui l'a ete)
        std::function<void()>          showMe{};  // Montre-moi : le faire a la place de l'utilisateur
        std::string                    waiting{}; // "J'attends le clic sur << Creer un symbole >>..."
        bool                           aside{false};   // la bulle en haut a droite (un dialogue au centre)
    };

    explicit HmiTutorial(std::string id = {});

    // `title` : le nom du parcours ("Cr\xC3\xA9" "er un symbole de projet") ; vide : "didacticiel de l'IHM".
    // `from` : l'etape de depart (un parcours repris).
    void start(std::vector<Step> steps, std::string title = {}, std::size_t from = 0);
    void stop(bool finished);
    void next();
    void previous();
    // Lot 21 : ce que "Tout defaire" fait (vide : pas de bouton) ; `pending`
    // dit s'il reste quelque chose a defaire.
    void setUndo(std::function<void()> undoAll, std::function<bool()> pending = {});
    // Lot API 7 : la bulle d'une etape interactive montre toujours ou l'on va -
    // Passer et Montre-moi a gauche, Precedent et Suivant a droite, Suivant
    // grise tant que le geste attendu n'est pas fait (la maquette des parcours
    // de l'API). Faux, le defaut : la bulle du lot 21 (Precedent et Passer a
    // gauche, Montre-moi ou Suivant a droite). start() ne le change pas :
    // l'ecran le pose a chaque lancement.
    void setFullNavigation(bool on) { fullNav_ = on; invalidate(); }
    [[nodiscard]] bool fullNavigation() const noexcept { return fullNav_; }
    // Lot 21 : verifier le geste de l'etape interactive (l'ecran l'appelle a
    // chaque image ; la verification elle-meme n'a lieu que tous les 0,2 s).
    void poll(double nowSeconds);
    // Montre-moi, Garder, Tout defaire : les gestes des boutons (scripts, tests).
    void showMe();
    void keep();
    void undoAll();

    [[nodiscard]] bool        active() const noexcept { return active_; }
    [[nodiscard]] std::size_t step() const noexcept { return index_; }
    [[nodiscard]] std::size_t stepCount() const noexcept { return steps_.size(); }
    [[nodiscard]] const Step* current() const noexcept { return active_ && index_ < steps_.size() ? &steps_[index_] : nullptr; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    // L'etape montree attend-elle un geste ?
    [[nodiscard]] bool interactive() const noexcept;
    // Prend-il tout (une visite) ? Faux : les raccourcis de l'ecran passent.
    [[nodiscard]] bool blocking() const noexcept { return active_ && !interactive(); }
    // "Etape 3 reussie : ..." (la bulle de l'etape suivante le dit).
    [[nodiscard]] const std::string& success() const noexcept { return success_; }
    // L'etape montree etait deja faite en y arrivant.
    [[nodiscard]] bool alreadyDone() const noexcept { return already_; }
    // Le parcours est au bout, reussi : il attend Garder / Tout defaire (ou Terminer).
    [[nodiscard]] bool completed() const noexcept { return completed_; }

    // Fermee : au bout (vrai) ou passee (faux).
    const core::SignalPtr<bool> closed = core::Signal<bool>::create();
    // Lot 21 : l'etape montree a change (son rang) - la progression s'enregistre.
    const core::SignalPtr<std::size_t> stepped = core::Signal<std::size_t>::create();

    [[nodiscard]] bool requestsOverlayPass() const override { return active_; }
    // Une etape interactive ne couvre que sa bulle (les infobulles du dessous passent).
    [[nodiscard]] bool overlayCovers(gfx::Point p) const override { return !(interactive() || completed_) || bubble_.contains(p); }

    // Pour les tests : ou sont les boutons de la bulle (apres un dessin).
    enum class Part : std::uint8_t { Next, Previous, Skip, ShowMe, Keep, UndoAll };
    [[nodiscard]] gfx::Rect partRect(Part) const noexcept;
    [[nodiscard]] gfx::Rect bubbleRect() const noexcept { return bubble_; }

protected:
    void            onPaintOverlay(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void enterStep();
    // Un evenement qui n'est pas pour la bulle, a l'endroit ou il serait alle
    // sans elle : une liste deroulante ouverte d'abord (comme WidgetMenu le fait).
    ui::EventResult passThrough(const ui::InputEvent&);
    [[nodiscard]] bool undoOffered() const;

    std::vector<Step> steps_;
    std::string       title_;
    std::size_t       index_{0};
    bool              active_{false};
    bool              already_{false};     // l'etape etait faite en y arrivant
    bool              completed_{false};   // la derniere etape interactive est faite
    bool              fullNav_{false};     // lot API 7 : Precedent et Suivant toujours la
    std::string       success_;            // "Etape 3 reussie : ..."
    std::string       alreadyWhat_;        // ce qui etait deja fait
    double            lastPoll_{-1.0};
    std::function<void()> undo_;
    std::function<bool()> pending_;
    gfx::Rect         next_{}, previous_{}, skip_{}, bubble_{}, showMe_{}, keep_{}, undoAll_{};
    int               hover_{-1};          // 0 suivant, 1 precedent, 2 passer, 3 montre-moi, 4 garder, 5 tout defaire
    bool              pressedOutside_{false};   // un appui passe dessous : son glisser et son relacher aussi
};

} // namespace app
