// =============================================================================
//  app/screens/HmiExprScreen.hpp - 1.11 (chantier T3, D5) : la page des
//                                  expressions, seule (action help.expressions)
// -----------------------------------------------------------------------------
//  Tant que le centre d'aide de T2 ne l'accroche pas, la page s'ouvre seule :
//  Aide > Les expressions, Aller a... « Page des expressions ». En haut : Retour
//  (Echap) et le titre ; a gauche, les 11 types (glyphe et titre) ; au centre,
//  la page (app::HmiExprPageView : l'article du type, « Essaie ici »).
//
//  POUR T2 : quand son centre a son chapitre « Expressions », la fabrique
//  "help.expressions" (App.cpp) peut ouvrir le centre sur expr-bool ; cet ecran
//  n'est alors plus qu'un repli. La page elle-meme est la meme des deux cotes :
//  addChild(make_unique<HmiExprPageView>(id)), show("expr-<cle>"), openTopic.
//
//  Dans ce fichier (et pas dans src/app/hmi/) : hmi_editor_test prend tous les
//  src/app/hmi/*.cpp, et cet ecran a besoin de l'appli (App, MenuManager).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../menu/IMenu.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ui { class Button; }

namespace app {

class App;
class HmiExprPageView;

class HmiExprScreen final : public menu::WidgetMenu {
public:
    // `key` : le type montre d'abord ("couleur" ou "expr-couleur") ; vide : BOOL.
    // `firstTarget` (integration I111, lien 4) : une puce de l'article jouee a l'ouverture
    // ("essayer:<source>", "inserer:<texte>") : le centre d'aide ouvre la page par elle.
    explicit HmiExprScreen(App& app, std::string key = {}, std::string firstTarget = {});

    // Montrer un type ; faux : type inconnu (rien ne change).
    bool show(std::string_view key);
    [[nodiscard]] HmiExprPageView* page() noexcept { return page_; }

    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;   // Echap : Retour

protected:
    core::Status buildUi() override;

private:
    void markCurrent();
    // La bascule "Variables : l'exemple / le projet ouvert" (le champ d'essai).
    void toggleSource();
    void refreshSource();

    App&                     app_;
    std::string              first_;
    std::string              firstTarget_;
    HmiExprPageView*         page_{nullptr};
    std::vector<ui::Button*> types_;        // dans l'ordre de hmi::exprguide::all()
    ui::Button*              source_{nullptr};   // la bascule des variables
    core::ConnectionScope    links_;
};

} // namespace app
