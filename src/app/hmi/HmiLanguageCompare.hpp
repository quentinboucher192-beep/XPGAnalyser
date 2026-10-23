// =============================================================================
//  app/hmi/HmiLanguageCompare.hpp - 1.12.30 : les langues cote a cote
// -----------------------------------------------------------------------------
//  LA VUE DESSINEE DANS CHAQUE LANGUE DU PROJET, cote a cote et a la meme
//  echelle : deux a quatre a la fois (Par 2, 3, 4), les autres en defilant
//  (les fleches, Maj + molette). La premiere est la langue du projet.
//
//  LES DEBORDEMENTS : un texte qui sort de sa boite dans une langue - le meme
//  controle que Generer (hmi::textOverflows : la largeur, la hauteur, le texte
//  coupe d'une case) - est entoure de rouge, et l'en-tete de la langue en dit
//  le nombre. Un clic sur la marque : un menu - choisir l'objet dans l'editeur,
//  aller a sa traduction (IHM > Langues), copier le message.
//
//  LA PSEUDO-LANGUE "Pseudo (+40 %)" : chaque texte allonge de 40 %, ses
//  lettres accentuees, entre crochets - ce qui va deborder, avant meme la
//  traduction. Elle n'est jamais une langue du projet (hmi::withPseudo : une
//  copie des langues).
//
//  LE ZOOM (la puce du bandeau bas, Ctrl + molette, Ctrl + plus) : l'echelle
//  commune des vues ; Ajuster : la vue entiere dans chaque colonne.
// =============================================================================
#pragma once

#include "HmiPainter.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiCommands.hpp"   // hmi::DocumentPtr
#include "../../hmi/HmiModel.hpp"
#include "../../ui/Widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class DropDown; class PopupMenu; }

namespace app {

class HmiToolStrip;

class HmiLanguageCompare final : public ui::Widget {
public:
    HmiLanguageCompare(std::string id, hmi::DocumentPtr doc);

    void setView(hmi::Id view);
    [[nodiscard]] hmi::Id viewId() const noexcept { return view_; }
    void setPseudo(bool on);
    [[nodiscard]] bool pseudo() const noexcept { return pseudo_; }
    void setPerRow(int n);                                  // 2, 3 ou 4
    [[nodiscard]] int perRow() const noexcept { return perRow_; }
    void scrollLanguages(int step);                         // la langue suivante (+1), la precedente (-1)
    [[nodiscard]] std::size_t firstShown() const noexcept { return first_; }

    // Une colonne : une langue (son code ; la pseudo-langue : hmi::kPseudoCode), la vue
    // dans cette langue, ses textes qui debordent.
    struct Column {
        std::string                code, name;
        bool                       source{false}, pseudo{false};
        hmi::View                  view;
        std::vector<hmi::Issue>    overflows;
    };
    [[nodiscard]] const std::vector<Column>& columns();
    // Une marque dessinee (au dernier dessin) : la colonne, l'objet, son cadre a l'ecran.
    struct Mark { std::size_t column{0}; hmi::Id object{hmi::kNoId}; std::string message; gfx::Rect rect; };
    [[nodiscard]] const std::vector<Mark>& marks() const noexcept { return marks_; }
    // Le menu d'une marque (scripts, essais) ; faux : pas de telle marque.
    bool openMarkMenu(std::size_t index);
    [[nodiscard]] ui::PopupMenu* markMenu() const noexcept { return menu_; }

    // Ce que l'hote fait d'une marque.
    struct Hosts {
        std::function<void(hmi::Id view, hmi::Id object)>                       selectInEditor;
        std::function<void(const std::string& code, const std::string& source)> openTranslation;
        std::function<void(const std::string& text)>                            status;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // Le zoom : l'echelle commune (Native, le contrat de Widget) ; 0 : ajustee.
    [[nodiscard]] float zoom() const override { return fit_ ? lastFit_ : scale_; }
    void setZoom(float z) override;
    [[nodiscard]] float minZoom() const override { return 0.1f; }
    [[nodiscard]] float maxZoom() const override { return 4.f; }
    void fitAll();

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    enum Tool : int { TPseudo = 1, TTwo, TThree, TFour, TPrev, TNext, TFit };
    void refreshViews();
    void rebuild();
    void chooseMark(int action);
    [[nodiscard]] gfx::Rect boardRect() const noexcept;
    [[nodiscard]] int markAt(gfx::Point p) const;

    hmi::DocumentPtr   doc_;
    hmi::Id            view_{hmi::kNoId};
    bool               pseudo_{true};
    int                perRow_{3};
    std::size_t        first_{0};
    bool               fit_{true};
    float              scale_{1.f};
    mutable float      lastFit_{1.f};
    float              scrollX_{0.f}, scrollY_{0.f};
    bool               dirty_{true};
    std::vector<Column> columns_;
    std::vector<Mark>  marks_;
    std::size_t        menuMark_{0};
    ui::DropDown*      views_{nullptr};
    HmiToolStrip*      tools_{nullptr};
    ui::PopupMenu*     menu_{nullptr};
    std::vector<hmi::Id> viewIds_;
    Hosts              hosts_;
    core::ConnectionScope links_;
};

} // namespace app
