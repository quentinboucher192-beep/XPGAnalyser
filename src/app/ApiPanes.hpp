// =============================================================================
//  app/ApiPanes.hpp - lot API 2 : les volets de l'API
// -----------------------------------------------------------------------------
//  Chaque entree du dossier API de l'arbre ouvre son onglet, fait comme un volet
//  de l'IHM : une barre a libelles (HmiToolStrip, la meme), le contenu, une
//  ligne d'aide en bas. Ce lot pose le cadre (ApiFrame), le tableau de bord
//  (un clic sur « API ») et l'etat vide des sous-routines ; les onglets des
//  types, des blocs, des variables et des tables d'animation viennent aux lots
//  suivants, dans ce meme cadre.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../project/ApiChecks.hpp"
#include "../ui/Theme.hpp"
#include "../ui/Widget.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace app {

class HmiToolStrip;

// ---------------------------------------------------------------- le cadre ----
class ApiFrame final : public ui::Widget {
public:
    ApiFrame(std::string id, ui::WidgetPtr content);
    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }
    [[nodiscard]] ui::Widget&   content() noexcept { return *content_; }
    void setHint(std::string text, ui::Tone tone = ui::Tone::None);
    [[nodiscard]] const std::string& hint() const noexcept { return hint_; }
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    HmiToolStrip* tools_{nullptr};
    ui::Widget*   content_{nullptr};
    std::string   hint_;
    ui::Tone      tone_{ui::Tone::None};
};

// ------------------------------------------------------- le tableau de bord ----
//  Les cartes (l'automate, la tache, les types, les variables), « A regarder »
//  et le poids de chaque entree de la tache. Chaque carte et chaque bouton
//  demandent un onglet par sa cle : "configuration", "ordre", "types",
//  "variables", "bibliotheque", "import-xhw", "macros".
class ApiDashboard final : public ui::Widget {
public:
    explicit ApiDashboard(std::string id);
    void setSummary(project::api::Summary summary, std::size_t macros);
    [[nodiscard]] const project::api::Summary& summary() const noexcept { return s_; }
    // Pour les scripts et les tests : ou est la carte ou le bouton de cette cle
    // (apres un premier dessin), et le texte des lignes de « A regarder ».
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> todoTitles() const;
    const core::SignalPtr<std::string> openRequested = core::Signal<std::string>::create();
    [[nodiscard]] ui::SizeHint sizeHint() const override;
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    struct Todo { ui::Tone tone; std::string mark, title, detail, button, key; };
    [[nodiscard]] std::vector<Todo> todos() const;
    struct Hit { gfx::Rect rect; std::string key; bool button; };
    project::api::Summary s_;
    std::size_t           macros_{0};
    mutable std::vector<Hit> hits_;
    int                   hover_{-1};
    int                   pressed_{-1};     // la carte ou le bouton sous l'appui
};

// ------------------------------------------------------------ l'etat vide ----
//  Un onglet meme quand il n'y a rien : ce que c'est, et comment en creer.
class ApiEmptyState final : public ui::Widget {
public:
    struct Way { std::string title, text, button, key; };
    ApiEmptyState(std::string id, std::string title, std::string text, std::vector<Way> ways);
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;
    const core::SignalPtr<std::string> openRequested = core::Signal<std::string>::create();
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    std::string       title_, text_;
    std::vector<Way>  ways_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> hits_;
    int               hover_{-1};
    int               pressed_{-1};
};

} // namespace app
