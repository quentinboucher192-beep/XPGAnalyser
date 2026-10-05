#pragma once
// =============================================================================
//  help/TutorialSpot.hpp - l'encadre d'un tutoriel suit sa cible (1.11, T1)
// -----------------------------------------------------------------------------
//  Tranche 23 (R111-15, decision 43) : l'encadre garde SA CIBLE (son nom), pas
//  une position. A chaque image, follow() la retrouve, et l'encadre prend sa
//  place du moment. Sur api-ordre, le clic sur API > Ordre d'execution ajoute
//  "Recents" en tete de l'arbre : la ligne descend de deux rangs, et l'encadre,
//  reste a l'ancienne place, entourait Blocs DFB jusqu'a la fin (R145_08).
//   - Cible pas (ou plus) en vue, ou de taille nulle (pas encore placee) :
//     l'encadre s'efface, et revient avec elle.
//   - Cible fugace (la liste de l'aide a la saisie, un menu : tranche 11) : elle
//     se ferme d'elle-meme ; l'encadre s'efface avec elle, pour de bon.
//   - clear() (une nouvelle etape, Recommencer) : plus rien a suivre.
//  La recherche (Finder) ne doit RIEN changer (ni defiler, ni deplier) : elle
//  tourne a chaque image, meme pendant l'"A toi" de l'eleve
//  (app::UiDriver::follow dans l'appli ; une fonction factice dans les essais).
// =============================================================================

#include "../platform/Geometry.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace help {

class TutorialSpot {
public:
    using Finder = std::function<std::optional<gfx::Rect>(const std::string& target)>;

    // Le geste encadrer : sa cible, trouvee a `at`.
    void show(std::string target, const gfx::Rect& at, bool fleeting) {
        target_ = std::move(target);
        rect_ = at;
        fleeting_ = fleeting;
    }
    void clear() {
        target_.clear();
        rect_.reset();
        fleeting_ = false;
    }

    // A chaque image : la cible retrouvee, ou l'encadre efface (fugace : pour de bon).
    void follow(const Finder& find) {
        if (target_.empty()) return;
        std::optional<gfx::Rect> r;
        if (find) r = find(target_);
        if (r && r->w > 0.f && r->h > 0.f) {
            rect_ = r;
            return;
        }
        rect_.reset();
        if (fleeting_) target_.clear();
    }

    [[nodiscard]] const std::optional<gfx::Rect>& rect() const noexcept { return rect_; }
    [[nodiscard]] const std::string& target() const noexcept { return target_; }
    [[nodiscard]] bool following() const noexcept { return !target_.empty(); }

private:
    std::string target_;
    std::optional<gfx::Rect> rect_;
    bool fleeting_ = false;
};

} // namespace help
