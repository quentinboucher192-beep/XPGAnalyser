// =============================================================================
//  ui/widgets/HelpView.hpp — afficher un HelpDocument
// -----------------------------------------------------------------------------
//  LA MISE EN PAGE EST CALCULEE, PAS DEVINEE A CHAQUE TRAME.
//
//  Un document d'aide a des blocs de hauteurs differentes - un titre, un
//  paragraphe qui se replie sur quatre lignes, une ligne de tableau. Un widget
//  qui recalcule ces hauteurs pendant qu'il peint ne sait pas ou il en est : il
//  ne peut ni dire quelle est la hauteur totale, ni sauter a une ancre, ni
//  savoir quel bloc se trouve sous la souris.
//
//  La disposition est donc etablie une fois - une liste de rectangles - et
//  refaite seulement quand la largeur ou le document change. Tout le reste s'en
//  deduit : le defilement, le saut a une ancre, le titre courant, le clic.
//
//  ET ELLE EST CALCULABLE SANS ECRAN, parce que mesurer un texte ne demande
//  qu'un IRenderer - les tests en ont un qui enregistre au lieu de dessiner.
// =============================================================================
#pragma once

#include "../HelpDocument.hpp"
#include "../Widget.hpp"

#include <memory>
#include <vector>

namespace ui {

struct LaidOutBlock {
    std::size_t index{0};       // dans HelpDocument::blocks()
    float       y{0.f};         // depuis le haut du document
    float       height{0.f};
    std::vector<std::string> lines;   // le texte deja replie
};

class HelpView : public Widget {
public:
    explicit HelpView(std::string id = {});

    void setDocument(std::shared_ptr<const HelpDocument> doc);
    [[nodiscard]] const HelpDocument* document() const noexcept { return doc_.get(); }

    // Saute au titre porte par cette ancre. Rend false si elle n'existe pas -
    // le widget ne doit pas defiler au hasard sur un lien casse.
    bool goToAnchor(std::string_view anchor);

    // Le titre sous lequel on se trouve, pour l'afficher en tete. Il repond
    // "ou suis-je", qui est la question qu'on se pose dans un long document.
    [[nodiscard]] std::string currentSection() const;

    // Met en valeur les occurrences d'un texte. Vide : plus rien n'est marque.
    void setHighlight(std::string term);

    [[nodiscard]] float scroll() const noexcept { return scrollY_; }
    void  setScroll(float y);
    [[nodiscard]] float contentHeight() const noexcept { return contentHeight_; }

    // La disposition, exposee pour pouvoir la verifier sans ecran.
    [[nodiscard]] const std::vector<LaidOutBlock>& layout() const noexcept { return laid_; }

    // Recalcule si la largeur a change. Appelee par onPaint, et par les tests
    // qui n'appellent pas onPaint.
    void relayout(gfx::IRenderer& r, const Theme& theme, float width);

    [[nodiscard]] SizeHint sizeHint() const override;

    const core::SignalPtr<std::string> anchorActivated = core::Signal<std::string>::create();

protected:
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    std::shared_ptr<const HelpDocument> doc_;
    std::vector<LaidOutBlock> laid_;
    std::string highlight_;
    float scrollY_{0.f};
    float contentHeight_{0.f};
    float laidWidth_{-1.f};
    std::size_t laidBlocks_{0};
    std::string pendingAnchor_;   // ---- Lot API 8 : sessions de capture ---- demandee avant la 1re disposition

    // La largeur d'une colonne de tableau, pour la disposition et la peinture.
    [[nodiscard]] static float columnWidth(float total, std::size_t cells, std::size_t at);
};

} // namespace ui
