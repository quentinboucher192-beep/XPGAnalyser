// =============================================================================
//  app/screens/HmiExprScreen.cpp - 1.11 (chantier T3, D5) : voir HmiExprScreen.hpp
// =============================================================================
#include "HmiExprScreen.hpp"

#include "../App.hpp"
#include "../hmi/HmiExprPage.hpp"
#include "../hmi/HmiExprPageView.hpp"
#include "../../hmi/HmiCommands.hpp"   // hmi::Document : le projet ouvert
#include "../../hmi/HmiExprGuide.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <utility>

namespace app {

namespace {

namespace eg = hmi::exprguide;

// Le titre de la barre (l'etiquette des ecrans est locale a Screens.cpp).
class ScreenTitle final : public ui::Widget {
public:
    explicit ScreenTitle(std::string text) : ui::Widget("help.expressions.titre"), text_(std::move(text)) {}
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        const float line = ui::lineHeight(gfx::FontId{16});
        return ui::SizeHint{{ui::measureWidth(text_, gfx::FontId{16}) + 16.f, line + 6.f}, {40.f, line + 4.f}, 1.f, 0.f};
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto r = contentRect();
        const auto f = ctx.theme.font.uiBold;
        ctx.r.drawText({r.x + 8.f, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f}, text_, f, ctx.theme.color.text);
    }
private:
    std::string text_;
};

// Le bouton d'un type dans la colonne de gauche : « #   Couleur ».
std::string typeLabel(const eg::TypeEntry& t) {
    return std::string(t.glyph) + "   " + std::string(t.title);
}

} // namespace

HmiExprScreen::HmiExprScreen(App& app, std::string key, std::string firstTarget)
    : menu::WidgetMenu("help.expressions"), app_(app), first_(std::move(key)), firstTarget_(std::move(firstTarget)) {}

core::Status HmiExprScreen::buildUi() {
    auto root = std::make_unique<ui::DockLayout>("help.expressions.root");

    // En haut : Retour (Echap) et le titre.
    auto bar = std::make_unique<ui::ToolBar>("help.expressions.barre");
    auto& back = bar->addButton("Retour", "help.expressions.retour", ui::Icon::Collapse);
    back.setTooltip("Revenir o\xC3\xB9 tu \xC3\xA9tais (\xC3\x89" "chap)");
    bar->addSeparator();
    bar->addCustom(std::make_unique<ScreenTitle>(
        "Les expressions \xE2\x80\x94 choisis un type, essaie ses exemples, corrige ses erreurs courantes"));
    links_ += back.clicked->connect([this] { app_.menus().PopMenu(); });
    // La bascule : le champ d'essai juge sur les variables d'exemple (V[0].Pos = 42, Pression = 3.8,
    // Mode = T_MODE#Auto, Pompe_Marche = TRUE) ou sur le projet ouvert, en lecture seule.
    bar->addSeparator();
    auto& source = bar->addButton("Variables : l'exemple", "help.expressions.variables", ui::Icon::None);
    source_ = &source;
    links_ += source.clicked->connect([this] { toggleSource(); });
    root->dock(std::move(bar), ui::DockLayout::Side::Top, 42.f);

    // A gauche : les 11 types, dans l'ordre de la maquette.
    auto list = std::make_unique<ui::BoxLayout>(ui::Orientation::Vertical, "help.expressions.types");
    list->setSpacing(2.f);
    types_.clear();
    for (const auto& t : eg::all()) {
        auto b = std::make_unique<ui::Button>(typeLabel(t), "help.expressions.type." + std::string(t.key));
        b->setStyle(ui::Button::Style::Flat);
        b->setTooltip(std::string(t.summary));
        const std::string key(t.key);
        links_ += b->clicked->connect([this, key] { (void)show(key); });
        types_.push_back(&static_cast<ui::Button&>(list->addChild(std::move(b))));
    }
    root->dock(std::move(list), ui::DockLayout::Side::Left, 220.f);

    // Au centre : la page.
    auto page = std::make_unique<HmiExprPageView>("help.expressions.page");
    page_ = page.get();
    // Un lien que la page ne traite pas (un autre type "expr-<cle>", un sujet du guide) : le
    // centre d'aide, sur ce sujet (integration I111, lien 4 : App::setHelpTopic, lu par
    // HelpCenterScreen::onEnter ; un sujet inconnu ouvre le centre sur son premier sujet).
    links_ += page_->openTopic->connect([this](const std::string& target) {
        std::string key = target;
        if (key.rfind("sujet:", 0) == 0) key = key.substr(6);
        app_.setHelpTopic(std::move(key));
        app_.menus().PushMenu("help");
    });
    root->dock(std::move(page), ui::DockLayout::Side::Center, 0.f);
    setRoot(std::move(root));

    if (first_.empty() || !show(first_)) (void)show(eg::all().front().key);
    // La puce du centre d'aide qui a ouvert la page (lien 4) : dans le champ d'essai.
    if (const auto act = exprpage::parseTarget(firstTarget_)) {
        if (act->kind == exprpage::Action::Kind::Try) page_->tryIt(act->text, act->typeKey);
        else if (act->kind == exprpage::Action::Kind::Insert) page_->tryIt(act->text);
    }
    refreshSource();
    return core::ok();
}

void HmiExprScreen::toggleSource() {
    if (!page_) return;
    const auto doc = app_.hmi();
    if (page_->usesSamples() && doc) page_->onProject(doc->project);
    else page_->onSamples();
    refreshSource();
}

void HmiExprScreen::refreshSource() {
    if (!source_ || !page_) return;
    const bool samples = page_->usesSamples();
    const bool open = app_.hmi() != nullptr;
    source_->setText(samples ? "Variables : l'exemple" : "Variables : le projet ouvert");
    source_->setEnabled(open || !samples);
    if (!open)
        source_->setTooltip("Aucune IHM ouverte : le champ d'essai juge sur les variables d'exemple "
                            "(V[0].Pos = 42, Pression = 3.8, Mode = T_MODE#Auto, Pompe_Marche = TRUE).");
    else if (samples)
        source_->setTooltip("Clique : le champ d'essai juge sur les variables du projet ouvert, en lecture seule et sans d\xC3\xA9marrer "
                            "son IHM (valeurs initiales, aucun script D\xC3\xA9marrage, aucune vue ouverte). "
                            "Les exemples de la page nomment les variables d'exemple : sur le projet, ils peuvent dire \xC2\xAB nom inconnu \xC2\xBB.");
    else
        source_->setTooltip("Clique : revenir aux variables d'exemple (V[0].Pos = 42, Pression = 3.8, Mode = T_MODE#Auto, Pompe_Marche = TRUE).");
}

bool HmiExprScreen::show(std::string_view key) {
    if (!page_ || !page_->show(key)) return false;
    markCurrent();
    return true;
}

void HmiExprScreen::markCurrent() {
    const auto* cur = page_ ? page_->current() : nullptr;
    const auto& all = eg::all();
    for (std::size_t i = 0; i < types_.size() && i < all.size(); ++i)
        types_[i]->setStyle(cur && cur->key == all[i].key ? ui::Button::Style::Primary : ui::Button::Style::Flat);
}

ui::EventResult HmiExprScreen::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && k->mods.none()) {
        app_.menus().PopMenu();
        return ui::EventResult::Consumed;
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app
