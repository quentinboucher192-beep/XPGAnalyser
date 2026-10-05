// =============================================================================
//  ui/NoveltyBoard.hpp - 1.10 (chantier P) : la fenetre "Nouveautes de la 1.10"
// -----------------------------------------------------------------------------
//  LA MAQUETTE (scene 8) : en tete "Nouveautes de la 1.10" et ce qui l'ouvre
//  ("premiere ouverture de cette version"), la croix. Une carte par nouveaute
//  de la version lancee, sur trois colonnes : une vignette (un petit dessin
//  en texte : "fx", "- 150 % +"...), le titre, une phrase, "Me montrer" (plein
//  tant qu'elle n'est pas vue, puis "vu"). Pour qui vient de plus loin, chaque
//  version manquee est une ligne repliee - "Ce que tu as manque : la 1.9 - tu
//  viens de la 1.8 . 11 nouveautes" - qui se deplie sur ses cartes. En bas :
//  la case "Montrer les reperes orange NOUVEAU dans l'appli", ou la revoir
//  (menu Aide), "Plus tard", "Tout vu" et "Me montrer tout, une a une".
//
//  Le widget ne sait pas ce qu'est une nouveaute : il recoit des cartes et
//  rend des gestes (signaux). L'application (app/NoveltyCenter) fait le reste :
//  ouvrir l'endroit, encadrer l'element, noter ce qui a ete vu.
// =============================================================================
#pragma once

#include "Widget.hpp"
#include "../core/Signal.hpp"

#include <string>
#include <vector>

namespace ui::novelty {

class Board final : public Widget {
public:
    struct Card {
        std::string id, version, title, text, image;
        std::string picto;             // la vignette, en texte ("fx  fx", "- 150 % +") ; vide : la version
        bool        seen{false};
        bool        canShow{true};    // "Me montrer" a un endroit ou aller
    };
    // Previous (1.10, H) : la ligne "Versions precedentes" au bout de la liste
    // (l'API, lot 8 : ses parcours) - le menu Aide n'a plus d'entree pour elle.
    enum class Button : std::uint8_t { Later, AllSeen, HideMarks, ShowAll, Close, Previous };

    explicit Board(std::string id = "nouveautes.cartes");

    // Les cartes, deja dans l'ordre (la plus recente version d'abord) ;
    // `current` : la version lancee ("1.10.0") ; `from` : la derniere vue
    // ("1.8" : "tu viens de la 1.8") ; `caption` : ce qui ouvre la fenetre.
    void setCards(std::vector<Card> cards, std::string current, std::string from = {}, std::string caption = {});
    [[nodiscard]] const std::vector<Card>& cards() const noexcept { return cards_; }
    void setSeen(const std::string& id, bool seen);
    void setMarksHidden(bool hidden) { marksHidden_ = hidden; invalidate(); }
    [[nodiscard]] bool marksHidden() const noexcept { return marksHidden_; }
    // "Nouveautes de la 1.10" ; les groupes de version ("1.10", "1.9").
    [[nodiscard]] std::string heading() const;
    [[nodiscard]] std::vector<std::string> groups() const;
    // Une version manquee : repliee au depart (la version lancee est toujours ouverte).
    void setGroupOpen(const std::string& shortVersion, bool open);
    [[nodiscard]] bool groupOpen(const std::string& shortVersion) const;
    // Le texte de la ligne d'une version manquee ("Ce que tu as manque : la 1.9 ...").
    [[nodiscard]] std::string groupLine(const std::string& shortVersion) const;

    const core::SignalPtr<const std::string&> showMeRequested = core::Signal<const std::string&>::create();
    const core::SignalPtr<>                   laterRequested     = core::Signal<>::create();
    const core::SignalPtr<>                   allSeenRequested   = core::Signal<>::create();
    const core::SignalPtr<>                   showAllRequested   = core::Signal<>::create();
    const core::SignalPtr<bool>               hideMarksRequested = core::Signal<bool>::create();   // vrai : masquer
    const core::SignalPtr<>                   previousRequested  = core::Signal<>::create();       // Versions precedentes

    // Les gestes, sans clic (scripts, tests) ; ou sont les boutons (apres un dessin).
    void press(Button b);
    bool pressShowMe(const std::string& id);
    [[nodiscard]] gfx::Rect buttonRect(Button b) const noexcept;
    [[nodiscard]] gfx::Rect showMeRect(std::size_t card) const noexcept;
    [[nodiscard]] gfx::Rect groupRect(const std::string& shortVersion) const noexcept;
    [[nodiscard]] float     contentHeight() const noexcept { return contentH_; }
    void scrollBy(float dy);

    [[nodiscard]] SizeHint sizeHint() const override;

protected:
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    struct Placed { gfx::Rect card, showMe; bool shown{false}; };
    struct Group  { std::string version; gfx::Rect row; bool fold{false}; };
    void place(const gfx::IRenderer& r, const Theme& t);

    std::vector<Card>   cards_;
    std::string         current_, from_, caption_;
    bool                marksHidden_{false};
    std::string         reveal_;    // 1.10 : le groupe qu'on vient de deplier - la mise en page le fait voir
    std::vector<Placed> placed_;
    std::vector<Group>  groups_;                // les lignes (y du contenu) ; fold : une version manquee
    std::vector<std::string> open_;             // les versions manquees depliees
    gfx::Rect           later_{}, allSeen_{}, showAll_{}, hide_{}, close_{}, body_{};
    gfx::Rect           previous_{};    // 1.10 : la ligne des versions precedentes (y du contenu)
    float               scroll_{0.f}, contentH_{0.f};
    int                 hover_{-1};       // 0.. une carte (Me montrer) ; -2 Plus tard ; -3 Tout vu ; -4 la case ; -5 tout ; -6 la croix
};

} // namespace ui::novelty
