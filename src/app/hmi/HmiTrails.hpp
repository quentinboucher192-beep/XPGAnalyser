// =============================================================================
//  app/hmi/HmiTrails.hpp - Didacticiel : les parcours (lot 21)
// -----------------------------------------------------------------------------
//  LES PARCOURS AU CHOIX, EN CARTES. Chacune : son nom (et NOUVEAU), sa sorte
//  (une visite qui montre, ou un parcours interactif qui fait faire), son
//  nombre d'etapes et sa duree, sa progression (une barre, "en cours 4 / 9",
//  "fait"), et son bouton : Commencer, Reprendre (la ou on s'est arrete) ou
//  Refaire. Elles se rangent en une, deux ou trois colonnes selon la largeur.
//
//  Le widget ne lance rien : il demande (startRequested). La page "Le
//  didacticiel" de l'aide de l'IHM le montre au-dessus de son texte.
//
//  Lot API 7 : LE MEME WIDGET POUR LES PARCOURS DE L'API (l'onglet
//  "API . Didacticiel", app/ApiTrails.cpp). Ce qui change se regle par le
//  style (setStyle) : le titre, le resume sur plusieurs lignes, la note en
//  encadre ("Comment ca marche."), le compte des parcours commences ; et par
//  carte : une icone, la date du parcours fait ("fait le 28/09"). Sans style,
//  la page de l'IHM est celle du lot 21.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Widget.hpp"

#include <string>
#include <vector>

namespace app {

struct TrailCard {
    std::string key;
    std::string title;
    std::string kind{"interactif"};   // "visite" ou "interactif"
    std::string summary;              // ce qu'on y fait, en une ligne
    int         steps{0};
    int         minutes{0};
    bool        isNew{false};         // NOUVEAU
    int         reached{0};           // l'etape atteinte la derniere fois (0 : pas commence, ou fini)
    bool        done{false};          // fait au moins une fois jusqu'au bout
    bool        running{false};       // ouvert en ce moment
    // ---- lot API 7 (a la fin : les cartes du lot 21 restent completes) --------
    ui::Icon    icon{ui::Icon::None}; // devant le nom ; None : pas d'icone
    std::string doneOn{};             // "28/09" : le jour ou il a ete fait ; vide : "fait"
    [[nodiscard]] bool operator==(const TrailCard&) const = default;
};

class HmiTrailCards final : public ui::Widget {
public:
    // Lot API 7 : ce qui distingue une page de parcours d'une autre.
    struct Style {
        std::string title{"Didacticiel : les parcours"};
        // Le compte en haut a droite dit aussi les parcours commences
        // ("6 parcours . 1 fait . 1 commence").
        bool        countStarted{false};
        int         summaryLines{1};  // le resume, coupe a ce nombre de lignes
        // La note sous les cartes. Un titre non vide : un encadre, le titre en
        // gras puis le texte, sur plusieurs lignes ; vide : une ligne discrete.
        // Un texte vide : celle du lot 21.
        std::string noteTitle{};
        std::string note{};
    };

    explicit HmiTrailCards(std::string id);
    void setCards(std::vector<TrailCard> cards);
    [[nodiscard]] const std::vector<TrailCard>& cards() const noexcept { return cards_; }
    void setStyle(Style style);
    [[nodiscard]] const Style& style() const noexcept { return style_; }
    // (cle, reprendre) : reprendre = la ou on s'etait arrete.
    const core::SignalPtr<const std::string&, bool> startRequested = core::Signal<const std::string&, bool>::create();
    // "Commencer", "Reprendre" ou "Refaire".
    [[nodiscard]] static std::string buttonLabel(const TrailCard&);
    // "en cours 4 / 9", "fait" (ou "fait le 28/09"), "" ; "ouvert en ce moment"
    // (ou "ouvert . etape 3") quand il tourne.
    [[nodiscard]] static std::string statusText(const TrailCard&);
    // Ou est le bouton d'une carte (apres une mise en page) ; faux : inconnue.
    [[nodiscard]] bool buttonRect(const std::string& key, gfx::Rect& out) const;
    // La hauteur voulue pour cette largeur (le titre, les cartes, la note).
    [[nodiscard]] float preferredHeight(float width) const;
    // Pour un panneau qui defile : la hauteur voulue a la largeur du moment.
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    // Pour les tests et les scripts : le geste du bouton d'une carte.
    bool press(const std::string& key);

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] int   columnsFor(float width) const noexcept;
    [[nodiscard]] float cardHeight() const;
    [[nodiscard]] float noteHeight(float width) const;
    [[nodiscard]] std::string noteText() const;
    std::vector<TrailCard> cards_;
    std::vector<gfx::Rect> cardRects_, buttonRects_;
    Style                  style_;
    int                    hover_{-1};
};

} // namespace app
