// =============================================================================
//  app/SectionComparePane.hpp - 1.8.0 : le comparateur de sections (un onglet)
// -----------------------------------------------------------------------------
//  API > Unites de programme (ou l'arbre) : des sections choisies (Ctrl+clic),
//  "Comparer". En haut : la section de gauche, celle de droite (les choisies
//  d'abord, toutes ensuite) ; les noms rapproches (A <-> B deduit des noms,
//  [0] <-> [1], d'autres a la main "X=Y ; U=V") ; ignorer les espaces, la casse,
//  les commentaires. Puis les chiffres (le % de lignes semblables, identiques,
//  modifiees, ajoutees, retirees) et, pour trois sections ou plus, qui ressemble
//  a qui (un clic montre la paire). Dessous, les deux sections cote a cote : les
//  lignes changees face a face, les mots differents surlignes, les longues
//  suites identiques repliees (un clic les deplie) ; ou "unifie". Precedente /
//  Suivante vont de difference en difference ; Copier le rapport met le texte
//  des differences dans le presse-papiers. Le calcul : project/SectionCompare.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/SectionCompare.hpp"
#include "../ui/Widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {
class Checkbox;
class DropDown;
class InputText;
}

namespace app {

class HmiToolStrip;
class SectionCompareBody;

class SectionComparePane final : public ui::Widget {
public:
    enum Action : int { APrev = 1, ANext, ASide, AUnified, ASwap, ACopy, AOpenLeft, AOpenRight };
    struct Hosts {
        std::function<std::shared_ptr<const domain::Project>()> project;
        std::function<void(domain::Index section, int line)>   openSection;   // ouvrir son code (ligne 0...)
        std::function<void(const std::string& title)>          retitle;       // le titre de l'onglet suit
        std::function<void(const std::string& message)>        status;
    };

    explicit SectionComparePane(std::string id);
    ~SectionComparePane() override;
    void setHosts(Hosts h);
    // Les sections choisies (2 ou plus) : la premiere paire se montre.
    void setSections(std::vector<domain::Index> chosen);
    void setPair(domain::Index left, domain::Index right);
    void recompute();                          // le projet a change, ou une option
    void step(int delta);                      // difference suivante (+1) / precedente (-1)
    void setUnified(bool on);
    void swapSides();
    [[nodiscard]] std::string reportText() const;
    [[nodiscard]] std::string title() const;   // "Comparer SFC_ManuA <-> SFC_ManuB"

    // Pour les scripts et les tests.
    [[nodiscard]] const project::compare::Result& result() const noexcept { return result_; }
    [[nodiscard]] const project::compare::Options& options() const noexcept { return options_; }
    [[nodiscard]] domain::Index left() const noexcept { return left_; }
    [[nodiscard]] domain::Index right() const noexcept { return right_; }
    [[nodiscard]] int currentBlock() const noexcept { return current_; }
    [[nodiscard]] bool unified() const noexcept { return unified_; }
    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }
    void setOption(const std::string& name, bool on);     // "noms", "indices", "espaces", "casse", "commentaires"
    void setExtraEquivalences(const std::string& text);   // "X=Y ; U=V"

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    friend class SectionCompareBody;
    void rebuildChoices();
    void readOptions();
    void buildVisual();
    [[nodiscard]] std::string sectionLabel(domain::Index s) const;
    [[nodiscard]] std::string ownerOf(domain::Index s) const;

    Hosts                              hosts_;
    HmiToolStrip*                      tools_{nullptr};
    ui::DropDown*                      boxLeft_{nullptr};
    ui::DropDown*                      boxRight_{nullptr};
    ui::Checkbox*                      optNames_{nullptr};
    ui::Checkbox*                      optIndex_{nullptr};
    ui::Checkbox*                      optSpaces_{nullptr};
    ui::Checkbox*                      optCase_{nullptr};
    ui::Checkbox*                      optComments_{nullptr};
    ui::InputText*                     extra_{nullptr};
    SectionCompareBody*                body_{nullptr};

    std::vector<domain::Index>         chosen_;
    std::vector<domain::Index>         choices_;       // l'ordre des listes deroulantes
    domain::Index                      left_{domain::kNoIndex}, right_{domain::kNoIndex};
    std::vector<std::string>           leftLines_, rightLines_;
    project::compare::Options          options_;
    project::compare::Result           result_;
    std::string                        namesPair_;     // "A <-> B" (le libelle de la case)
    int                                current_{0};
    bool                               unified_{false};
    bool                               syncing_{false};
    // Qui ressemble a qui (trois sections ou plus) : (gauche, droite, %).
    struct Pair { domain::Index a, b; int similarity; mutable gfx::Rect rect{}; };
    std::vector<Pair>                  pairs_;
    gfx::Rect                          summary_{};
    core::ConnectionScope              links_;
};

} // namespace app
