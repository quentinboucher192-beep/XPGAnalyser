#include "GrafcetCodePanel.hpp"

#include <variant>

namespace app {

    // ---------------------------------------------------------------------------
    ui::EventResult GrafcetCodeEditor::onEvent(const ui::InputEvent& ev) {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev);
            k && focused() && k->key == ui::Key::Return && k->mods.ctrl && !k->mods.alt) {
            if (completionOpen()) dismissCompletion();
            submitted->emit();
            return ui::EventResult::Consumed;
        }
        return ui::MultiLineText::onEvent(ev);
    }

    // ---------------------------------------------------------------------------
    GrafcetCodePanel::GrafcetCodePanel(std::string id) : ui::Widget(std::move(id)) {
        auto title = std::make_unique<ui::StatusBar>(this->id() + ".title");
        title->setTooltip("Le code de l'\xC3\xA9l\xC3\xA9ment choisi dans le dessin, tel qu'il est dans le programme");
        title_ = &static_cast<ui::StatusBar&>(addChild(std::move(title)));

        auto apply = std::make_unique<ui::Button>("Appliquer");
        apply->setCompact(true);
        apply->setTooltip("R\xC3\xA9\xC3\xA9" "crit le programme (Ctrl+Entr\xC3\xA9" "e) ; Ctrl+Z d\xC3\xA9" "fait");
        apply_ = &static_cast<ui::Button&>(addChild(std::move(apply)));

        auto back = std::make_unique<ui::Button>("R\xC3\xA9tablir");
        back->setCompact(true);
        back->setStyle(ui::Button::Style::Flat);
        back->setTooltip("Remet le texte du programme (rien n'est \xC3\xA9" "crit)");
        revert_ = &static_cast<ui::Button&>(addChild(std::move(back)));

        auto editor = std::make_unique<GrafcetCodeEditor>(this->id() + ".code");
        editor->setShowLineNumbers(true);
        editor->setLanguage(ui::Language::StructuredText);
        editor->setReadOnly(true);
        editor_ = &static_cast<GrafcetCodeEditor&>(addChild(std::move(editor)));

        auto status = std::make_unique<ui::StatusBar>(this->id() + ".status");
        status_ = &static_cast<ui::StatusBar&>(addChild(std::move(status)));

        links_ += apply_->clicked->connect([this] { if (dirty()) applyRequested->emit(); });
        links_ += revert_->clicked->connect([this] { revert(); });
        links_ += editor_->submitted->connect([this] { if (dirty()) applyRequested->emit(); });
        // Quitter le volet avec un changement l'applique : un texte modifie qu'on
        // laisse derriere soi sans le savoir est un texte perdu au prochain clic.
        links_ += editor_->focusLost->connect([this] { if (dirty()) applyRequested->emit(); });
        links_ += editor_->textChanged->connect([this](const std::string&) { refreshTitle(); });

        showNothing("Choisis une transition ou une action dans le dessin : son code s'\xC3\xA9" "crit ici.");
    }

    void GrafcetCodePanel::show(Target target, int id, std::string title, std::string text,
                                std::string hint) {
        target_ = target;
        targetId_ = id;
        titleText_ = std::move(title);
        baseline_ = text;
        editor_->setReadOnly(target == Target::None);
        editor_->setText(std::move(text));
        editor_->clearModified();
        refreshTitle();
        setStatus(std::move(hint));
    }

    void GrafcetCodePanel::showNothing(std::string hint) {
        show(Target::None, -1, "Code", {}, std::move(hint));
    }

    std::string GrafcetCodePanel::text() const { return editor_->text(); }

    bool GrafcetCodePanel::dirty() const {
        return target_ != Target::None && editor_->text() != baseline_;
    }

    void GrafcetCodePanel::markApplied(std::string text) {
        baseline_ = std::move(text);
        refreshTitle();
    }

    void GrafcetCodePanel::revert() {
        if (target_ == Target::None) return;
        editor_->setText(baseline_);
        refreshTitle();
        setStatus("Texte du programme remis : rien n'a \xC3\xA9t\xC3\xA9 \xC3\xA9" "crit.");
    }

    void GrafcetCodePanel::setStatus(std::string message, ui::StatusBar::Severity severity) {
        status_->setMessage(std::move(message), severity);
    }

    void GrafcetCodePanel::refreshTitle() {
        const bool changed = dirty();
        title_->setMessage(titleText_ + (changed ? "   \xE2\x80\xA2 modifi\xC3\xA9 (Ctrl+Entr\xC3\xA9" "e applique)" : std::string{}),
                           changed ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::None);
        apply_->setEnabled(changed);
        revert_->setEnabled(changed);
    }

    void GrafcetCodePanel::onLayout() {
        const auto area = contentRect();
        const float barH = 26.f, statusH = 22.f, applyW = 100.f, revertW = 86.f;
        title_->setBounds({ area.x, area.y, std::max(0.f, area.w - applyW - revertW - 8.f), barH });
        apply_->setBounds({ area.right() - applyW - revertW - 4.f, area.y + 1.f, applyW, barH - 2.f });
        revert_->setBounds({ area.right() - revertW, area.y + 1.f, revertW, barH - 2.f });
        editor_->setBounds({ area.x, area.y + barH + 2.f, area.w,
                             std::max(0.f, area.h - barH - statusH - 4.f) });
        status_->setBounds({ area.x, area.bottom() - statusH, area.w, statusH });
    }

} // namespace app
