#include "HmiNativesPane.hpp"

#include "HmiIcons.hpp"
#include "HmiNativesCards.hpp"
#include "HmiPanels.hpp"
#include "../../help/Novelties.hpp"     // la notation des exemples (aide.notation)
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/HelpArticleView.hpp"

namespace app {

namespace {
enum Action : int { ABack = 1, AInsert, ACopy, AHelp };
} // namespace

HmiNativesPane::HmiNativesPane(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ABack, HmiGlyph::Undo, "Revenir \xC3\xA0 la fiche d'avant", "Pr\xC3\xA9" "c\xC3\xA9" "dente");
    tools->separator();
    tools->add(AInsert, HmiGlyph::Plus, "Ins\xC3\xA9rer dans le script en cours d'\xC3\xA9" "dition (son appel, sa valeur, son mot)", "Ins\xC3\xA9rer dans le script");
    tools->add(ACopy, HmiGlyph::Copy, "Copier l'appel (ou la valeur, le mot) dans le presse-papiers", "Copier");
    tools->separator();
    tools->add(AHelp, HmiGlyph::Help, "L'aide des natives (le langage de l'IHM)", "Aide");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(ABack, [this] { return !history_.empty(); });
    tools_->setEnabledWhen(AInsert, [this] { return !natives::insertText(key_).empty(); });
    tools_->setEnabledWhen(ACopy, [this] { return !natives::insertText(key_).empty(); });

    view_ = &static_cast<ui::HelpArticleView&>(addChild(std::make_unique<ui::HelpArticleView>(base + ".article")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case ABack: (void)back(); break;
            case AInsert: (void)insertCurrent(); break;
            case ACopy: (void)copyCurrent(); break;
            case AHelp:
                if (hosts_.help) hosts_.help("reference");
                break;
            default: break;
        }
    });
    links_ += view_->linkActivated->connect([this](const std::string& target) { (void)follow(target); });
    show("natives");
    history_.clear();
}

void HmiNativesPane::setHosts(Hosts h) { hosts_ = std::move(h); }

std::string HmiNativesPane::notation() {
    const auto& n = help::news::session().helpNotation;
    return n == "C" || n == "C++" ? n : std::string("ST");
}

void HmiNativesPane::compose(bool keepScroll) {
    auto a = natives::article(key_, notation());
    if (keepScroll) view_->replaceArticle(std::move(a));
    else view_->setArticle(std::move(a));
}

bool HmiNativesPane::show(const std::string& key) {
    if (key == key_) return true;
    if (natives::article(key, notation()).empty()) return false;
    if (!key_.empty()) {
        history_.push_back(key_);
        if (history_.size() > 50) history_.erase(history_.begin());
    }
    key_ = key;
    compose(false);
    const auto what = natives::insertText(key_);
    say(what.empty() ? natives::title(key_) + " \xC2\xB7 natif, en lecture seule"
                     : natives::title(key_) + " \xC2\xB7 Ins\xC3\xA9rer pose \xC2\xAB " + what + " \xC2\xBB dans le script en cours d'\xC3\xA9" "dition");
    invalidate();
    return true;
}

bool HmiNativesPane::follow(const std::string& target) {
    if (target.rfind("notation:", 0) == 0) {
        // Le selecteur ST | C | C++ : le choix vaut pour toutes les fiches (et l'aide), et se garde.
        const std::string n = target.substr(9);
        if ((n != "ST" && n != "C" && n != "C++") || n == notation()) return false;
        help::news::session().helpNotation = n;
        compose(true);
        say("Les exemples en " + n + " (le choix est gard\xC3\xA9 pour toutes les fiches)");
        return true;
    }
    if (target.rfind("native:", 0) == 0) {
        const std::string key = target.substr(7);
        if (!show(key)) return false;
        if (hosts_.shown) hosts_.shown(key_);
        return true;
    }
    return false;
}

bool HmiNativesPane::back() {
    if (history_.empty()) return false;
    const std::string previous = history_.back();
    history_.pop_back();
    key_ = previous;
    compose(false);
    say(natives::title(key_));
    if (hosts_.shown) hosts_.shown(key_);
    invalidate();
    return true;
}

bool HmiNativesPane::insertCurrent() {
    const auto what = natives::insertText(key_);
    if (what.empty()) return false;
    if (hosts_.insert && hosts_.insert(what)) {
        say("Ins\xC3\xA9r\xC3\xA9 : " + what);
        return true;
    }
    ui::setClipboardText(what);
    say("Aucun script en cours d'\xC3\xA9" "dition : \xC2\xAB " + what + " \xC2\xBB est copi\xC3\xA9 (\xC3\xA0 coller)");
    return false;
}

bool HmiNativesPane::copyCurrent() {
    const auto what = natives::insertText(key_);
    if (what.empty()) return false;
    ui::setClipboardText(what);
    say("Copi\xC3\xA9 : " + what);
    return true;
}

void HmiNativesPane::say(std::string text) {
    message_ = std::move(text);
    if (status_) status_->setMessage(message_);
}

void HmiNativesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    view_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

void HmiNativesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
