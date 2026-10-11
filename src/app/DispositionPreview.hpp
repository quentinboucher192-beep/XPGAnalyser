// =============================================================================
//  app/DispositionPreview.hpp - 1.12.4 : l'apercu de la fenetre Disposition
// -----------------------------------------------------------------------------
//  La fenetre de l'application dessinee en boites (disposition::sketch) avec le
//  brouillon de la fenetre Disposition : chaque choix s'y voit tout de suite,
//  avant Appliquer. En edition ou en simulation (les deux boutons au-dessus) : un
//  element « en simulation » apparait dans l'apercu de la simulation, liseré de
//  vert ; un element « en edition », liseré de bleu dans celui de l'edition.
// =============================================================================
#pragma once

#include "Disposition.hpp"
#include "../ui/Widget.hpp"

#include <string>
#include <vector>

namespace app {

class DispositionPreview final : public ui::Widget {
public:
    // `layout` : le brouillon de la fenetre Disposition (relu a chaque image).
    explicit DispositionPreview(const disposition::Layout& layout, std::string id = {});

    void setSimulating(bool on) noexcept { simulating_ = on; }
    [[nodiscard]] bool simulating() const noexcept { return simulating_; }
    // La fenetre dessinee : la plus grande a la proportion d'un ecran 16:10, centree dans les bornes.
    [[nodiscard]] gfx::Rect frame() const;
    // Ce qui est dessine (pour les essais) ; une boite en pixels.
    [[nodiscard]] std::vector<disposition::SketchBox> boxes() const { return disposition::sketch(layout_, simulating_); }
    [[nodiscard]] gfx::Rect rectOf(const disposition::SketchBox&) const;

protected:
    void onPaint(const ui::PaintContext&) override;

private:
    const disposition::Layout& layout_;
    bool                       simulating_{false};
};

} // namespace app
