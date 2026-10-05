// =============================================================================
//  app/GoToPanel.hpp - Aller a... (Ctrl+K) (lot 20 ; lot recherche : partout)
// -----------------------------------------------------------------------------
//  TAPER QUELQUES LETTRES ET Y ALLER. Une variable de l'automate ou de l'IHM,
//  un membre (armoires[0].sorties.V3), un type, une unite, une section, une
//  ligne de code, une tache, une table d'animation, une vue, un objet, une
//  alarme, une recette, un script, une macro, une version, un sujet d'aide, un
//  volet ou une ACTION (Simuler, Vers Control Expert...) : les resultats par
//  categorie, avec leur nombre ; les mots tapes surlignes.
//
//    +------------------------------------------------------------------------+
//    | pompe|                     fleches - Entree : aller - Tab : categorie  |
//    | [Tout 245] [Variables API 37] [Code 120] [Alarmes 4] [Actions 1] ...   |
//    | VARIABLES API  .  37 (les 8 premieres)                                 |
//    | [=] P_Pompe1        BOOL . globale . Commande de la pompe 1   Entree   |
//    | CODE  .  120                                                           |
//    | [ ] Gestion : ligne 42   IF P_Pompe1 THEN ...                          |
//    +------------------------------------------------------------------------+
//
//  Fleches (PgPrec / PgSuiv) pour choisir, Entree pour y aller, Tab / Maj+Tab
//  pour passer d'une categorie a l'autre (la categorie seule en montre plus),
//  Echap pour fermer. La recherche est celle de toutes les listes (mots ET,
//  "phrase", -exclu, sans casse ni accents).
//
//  Le panneau ne sait rien du projet : l'ecran lui donne une fonction qui
//  cherche (setSearchAll : par categorie, avec les totaux ; setSearch : la
//  forme d'avant, une liste) et recoit le resultat choisi (chosen). Il se
//  dessine sous le champ "Aller a..." de la barre d'outils (GoToBox).
//  setDebounce : la recherche attend que la frappe s'arrete (0 : tout de suite).
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../ui/Icons.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/ScrollBar.hpp"   // 1.11.4 : la barre de defilement qu'on tire

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace app {

class GoToPanel final : public ui::Widget {
public:
    struct Result {
        int         group{0};        // la categorie (l'ordre des groupes : 0 en haut)
        std::string groupTitle;      // "VARIABLES IHM"
        std::string title;           // "Pression_Entree"
        std::string subtitle;        // "REAL - Centrale PM5560 - %MW3010"
        std::string hint;            // a droite : "Ctrl+Alt+S", "Volet"
        std::string key;             // ce que l'ecran en fait ("hmivar:Pression_Entree"...)
        ui::Icon    icon{ui::Icon::None};
        int         score{0};        // plus petit : mieux (le tri dans le groupe)
    };
    // Lot recherche : une categorie trouvee - son titre de groupe, sa pastille,
    // combien de resultats en tout (au-dela de ceux montres).
    struct Category {
        int         group{0};
        std::string title;           // "VARIABLES API"
        std::string chip;            // "Variables API"
        std::size_t total{0};
    };
    struct Outcome {
        std::vector<Result>   results;      // dans l'ordre de l'ecran (categorie, puis score)
        std::vector<Category> categories;   // dans l'ordre de l'ecran ; la choisie meme vide
    };
    using Search = std::function<std::vector<Result>(const std::string& text)>;
    // `group` : la categorie choisie (-1 : toutes).
    using SearchAll = std::function<Outcome(const std::string& text, int group)>;

    explicit GoToPanel(std::string id = {});
    void setSearch(Search s) { search_ = std::move(s); }
    void setSearchAll(SearchAll s) { searchAll_ = std::move(s); }
    void setDebounce(double seconds) noexcept { debounce_ = seconds; }
    // Ouvert sous `anchor` (le champ de la barre), le texte vide ; le clavier est a lui.
    void open(gfx::Rect anchor);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void setText(const std::string& text);          // cherche tout de suite (sans delai)
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] const std::vector<Result>& results() const noexcept { return results_; }
    [[nodiscard]] const std::vector<Category>& categories() const noexcept { return categories_; }
    [[nodiscard]] int current() const noexcept { return current_; }
    void moveCurrent(int delta);
    void activate(int index);          // aller au resultat i (le panneau se ferme)
    [[nodiscard]] gfx::Rect resultRect(int index) const;
    [[nodiscard]] gfx::Rect boxRect() const noexcept { return box_; }
    // Lot recherche : la categorie montree (-1 : toutes), la suivante (Tab).
    [[nodiscard]] int category() const noexcept { return category_; }
    void setCategory(int group);
    void nextCategory(int step);
    [[nodiscard]] gfx::Rect chipRect(int group) const;   // -1 : "Tout" ; vide avant le premier dessin
    // Une recherche attend la fin de la frappe ; flush() la fait tout de suite.
    [[nodiscard]] bool pending() const noexcept { return pending_; }
    void flush();

    const core::SignalPtr<const Result&> chosen = core::Signal<const Result&>::create();

    [[nodiscard]] bool      requestsOverlayPass() const override { return open_; }
    [[nodiscard]] gfx::Rect eventBounds() const override { return open_ ? box_ : gfx::Rect{}; }

    // La recherche, sans accents ni casse : ou `needle` se trouve dans `hay`
    // (-1 : absent), pour surligner et classer. Publique pour les tests.
    [[nodiscard]] static int matchAt(const std::string& hay, const std::string& needle);

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void refresh();
    void textEdited();                 // une frappe : maintenant, ou apres le delai
    [[nodiscard]] float contentHeight() const;
    [[nodiscard]] float rowTop(int index) const;     // dans la liste (0 : son haut)
    void ensureVisible(int index);
    Search                 search_;
    SearchAll              searchAll_;
    bool                   open_{false};
    std::string            text_;
    std::vector<Result>    results_;
    std::vector<Category>  categories_;
    int                    current_{0};
    int                    category_{-1};
    double                 debounce_{0.0};
    bool                   pending_{false};
    double                 pendingSince_{-1.0};
    float                  scroll_{0.f};
    ui::PaintedScrollBar   sbar_;   // 1.11.4 : la barre se tire
    float                  listH_{0.f};              // la hauteur de la liste au dernier dessin
    gfx::Rect              anchor_{}, box_{};
    std::vector<std::pair<int, gfx::Rect>> rowRects_;
    std::vector<std::pair<int, gfx::Rect>> chipRects_;   // (categorie, pastille) ; -1 : Tout
};

// Le champ "Aller a...  Ctrl+K" de la barre d'outils : un clic ouvre le panneau.
class GoToBox final : public ui::Widget {
public:
    explicit GoToBox(std::string id = {});
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    const core::SignalPtr<> clicked = core::Signal<>::create();
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
};

} // namespace app
