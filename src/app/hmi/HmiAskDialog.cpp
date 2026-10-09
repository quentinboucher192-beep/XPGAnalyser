// app/hmi/HmiAskDialog.cpp - un dialogue qui demande avant d'agir (lot 17).
#include "HmiAskDialog.hpp"

#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};

// Un texte coupe a la largeur (les \n comptent).
std::vector<std::string> wrapText(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : s) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// Un paragraphe (en direct si `live`).
class TextBlock final : public ui::Widget {
public:
    TextBlock(std::string text, std::function<std::string()> live, gfx::FontId font, bool muted)
        : text_(std::move(text)), live_(std::move(live)), font_(font), muted_(muted) {}
    [[nodiscard]] float heightFor(float w) const {
        const auto lines = wrapText(live_ ? live_() : text_, font_, w - 4);
        return static_cast<float>(lines.size()) * (ui::lineHeight(font_) + 3.f);
    }
    void poll() {
        if (live_) invalidate();
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        float y = b.y;
        ctx.r.pushClip(b);
        for (const auto& l : wrapText(live_ ? live_() : text_, font_, b.w - 4)) {
            ctx.r.drawText({b.x, y}, l, font_, muted_ ? ctx.theme.color.textMuted : ctx.theme.color.text);
            y += ctx.r.lineHeight(font_) + 3.f;
        }
        ctx.r.popClip();
    }
private:
    std::string                  text_;
    std::function<std::string()> live_;
    gfx::FontId                  font_;
    bool                         muted_;
};

// La liste des cases : une case par ligne, son detail en gris a droite.
class ItemList final : public ui::Widget {
public:
    ItemList() = default;
    void add(ui::Checkbox* box, std::string detail) {
        boxes_.push_back(box);
        details_.push_back(std::move(detail));
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {400.f, static_cast<float>(boxes_.size()) * 28.f + 4.f};
        h.minimum = h.preferred;
        h.stretchX = 1.f;
        return h;
    }
    bool plain{false};      // 1.11.22 : les libelles seuls, sans cases (les cases sont cachees)
protected:
    void onLayout() override {
        const auto b = bounds();
        for (std::size_t i = 0; i < boxes_.size(); ++i) {
            const float w = std::min(b.w - 20.f, ui::measureWidth(boxes_[i]->label(), gfx::FontId{16}) + (plain ? 8.f : 40.f));
            boxes_[i]->setBounds({b.x + 8.f, b.y + 2.f + 28.f * static_cast<float>(i), w, 26.f});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        ctx.r.fillRect(b, ctx.theme.color.inputBg);
        for (std::size_t i = 0; i < boxes_.size(); ++i) {
            const auto r = boxes_[i]->bounds();
            if (i % 2 == 1) ctx.r.fillRect({b.x, r.y - 1.f, b.w, 28.f}, gfx::Color{128, 128, 128, 18});
            if (plain)
                ctx.r.drawText({r.x, r.y + (r.h - ctx.r.lineHeight(gfx::FontId{16})) * 0.5f}, boxes_[i]->label(), gfx::FontId{16}, ctx.theme.color.text);
            const float x = r.x + r.w + 10.f;
            if (x < b.x + b.w - 20.f && !details_[i].empty())
                ctx.r.drawText({x, r.y + (r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, details_[i], kSmall, ctx.theme.color.textMuted);
        }
    }
private:
    std::vector<ui::Checkbox*> boxes_;
    std::vector<std::string>   details_;
};

// Le corps : tout, place a la main de haut en bas.
class AskBody final : public ui::Widget {
public:
    explicit AskBody(std::string title) : title_(std::move(title)) {}
    float width{640.f}, height{0.f};
    bool                       liveText{false};     // le paragraphe grandit (une detection) : toute la place
    TextBlock*                 text{nullptr};
    std::string                listTitle;
    ui::ScrollablePanel*       list{nullptr};
    ItemList*                  listContent{nullptr};
    int                        firstChecked{-1};    // lot 20 : la premiere case cochee, montree a l'ouverture
    std::vector<ui::Checkbox*> extras;
    std::vector<ui::RadioButton*> radios;
    std::vector<std::string>   radioDetails;
    std::vector<ui::InputText*> fields;         // un par choix (nul : aucun)
    std::vector<ui::Widget*>    browses;        // le bouton ... d'un champ de chemin (nul : aucun)
    TextBlock*                 note{nullptr};
    std::vector<ui::Button*>   buttons;         // de gauche a droite (le principal en dernier)

    [[nodiscard]] float contentHeight(float w) const {
        float h = 0;
        if (text) h += text->heightFor(w) + 12;
        if (listContent) h += 24 + std::min(8.f * 28.f + 6.f, listContent->sizeHint().preferred.h + 4.f) + 10;
        h += static_cast<float>(extras.size()) * 30.f + (extras.empty() ? 0.f : 6.f);
        for (std::size_t i = 0; i < radios.size(); ++i) {
            h += 30.f;
            if (!radioDetails[i].empty()) h += static_cast<float>(wrapText(radioDetails[i], kSmall, w - 40).size()) * 18.f;
            if (i < fields.size() && fields[i]) h += 34.f;
            h += 6;
        }
        if (note) h += note->heightFor(w) + 8;
        return h + 48;
    }
protected:
    void onLayout() override {
        const auto r = bounds();
        const float h = height > 0 ? height : std::min(r.h - 40.f, contentHeight(width - 28.f) + 44.f);
        panel_ = {std::floor((r.w - width) * 0.5f), std::floor((r.h - h) * 0.5f), width, h};
        const float x = panel_.x + 16, w = panel_.w - 32;
        float y = panel_.y + 44;
        if (text) {
            float th = text->heightFor(w);
            if (liveText) th = std::max(40.f, panel_.h - 44.f - (note ? note->heightFor(w) + 16.f : 0.f) - 64.f);
            text->setBounds({x, y, w, th});
            y += th + 12;
        }
        if (list && listContent) {
            listTitleAt_ = y;
            y += 24;
            const float lh = std::min(8.f * 28.f + 6.f, listContent->sizeHint().preferred.h + 4.f);
            list->setBounds({x, y, w, lh});
            // Lot 20 : une case cochee au bas d'une longue liste (la vue choisie,
            // exportee) - la liste s'ouvre sur elle, deux lignes au-dessus.
            if (!revealed_ && lh > 0.f) {
                revealed_ = true;
                if (firstChecked >= 3) list->scrollTo({0.f, 28.f * static_cast<float>(firstChecked - 2)});
            }
            // Le contenu suit le defilement de la liste (une nouvelle mise en page
            // du dialogue ne la ramene pas en haut).
            listContent->setBounds({x, y - list->scrollOffset().y, w - 12, listContent->sizeHint().preferred.h});
            y += lh + 10;
        }
        for (auto* e : extras) {
            e->setBounds({x, y, w, 26});
            y += 30;
        }
        if (!extras.empty()) y += 6;
        radioAt_.clear();
        for (std::size_t i = 0; i < radios.size(); ++i) {
            radios[i]->setBounds({x, y, w, 26});
            y += 28;
            radioAt_.push_back(y);
            if (!radioDetails[i].empty()) y += static_cast<float>(wrapText(radioDetails[i], kSmall, w - 40).size()) * 18.f;
            if (i < fields.size() && fields[i]) {
                // Un champ de chemin prend la largeur, son bouton ... a droite.
                ui::Widget* browse = i < browses.size() ? browses[i] : nullptr;
                const float fw = browse ? std::max(120.f, w - 40 - 40) : std::min(320.f, w - 40);
                fields[i]->setBounds({x + 28, y + 2, fw, 28});
                if (browse) browse->setBounds({x + 28 + fw + 6, y + 2, 34, 28});
                y += 34;
            }
            y += 6;
        }
        if (note) note->setBounds({x, y, w, note->heightFor(w)});
        // Les boutons, en bas a droite.
        float bx = panel_.x + panel_.w - 16;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(96.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel_.y + panel_.h - 44, bw, 30});
            bx -= 10;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        ctx.r.drawText({panel_.x + 12.f, panel_.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, title_, ctx.theme.font.uiBold, c.text);
        if (list && !listTitle.empty()) ctx.r.drawText({panel_.x + 16, listTitleAt_ + 2}, listTitle, ctx.theme.font.uiBold, c.text);
        for (std::size_t i = 0; i < radios.size() && i < radioAt_.size(); ++i) {
            float y = radioAt_[i];
            for (const auto& l : wrapText(radioDetails[i], kSmall, panel_.w - 72)) {
                ctx.r.drawText({panel_.x + 44, y}, l, kSmall, c.textMuted);
                y += 18;
            }
        }
    }
private:
    std::string        title_;
    gfx::Rect          panel_{};
    float              listTitleAt_{0};
    std::vector<float> radioAt_;
    bool               revealed_{false};
};

std::string joinBits(const std::vector<bool>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::string(v[i] ? "1" : "0");
    return s;
}

std::vector<bool> splitBits(const std::string& s) {
    std::vector<bool> out;
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, ',')) out.push_back(part == "1");
    return out;
}

} // namespace

HmiAskDialog::HmiAskDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {}

menu::MenuTraits HmiAskDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;       // une detection, un scan : la carte continue de bouger dessous
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status HmiAskDialog::buildUi() {
    auto body = std::make_unique<AskBody>(spec_.title);
    body->width = spec_.width;
    body->height = spec_.height;
    body->liveText = static_cast<bool>(spec_.live) && spec_.height > 0;
    const std::string base = spec_.id;
    if (!spec_.text.empty() || spec_.live) {
        body->text = &static_cast<TextBlock&>(body->addChild(std::make_unique<TextBlock>(spec_.text, spec_.live, gfx::FontId{15}, false)));
        text_ = body->text;
    }
    if (!spec_.items.empty()) {
        body->listTitle = spec_.listTitle;
        auto panel = std::make_unique<ui::ScrollablePanel>(base + ".list");
        panel->setScrollPolicy(false, true);
        auto content = std::make_unique<ItemList>();
        auto* list = content.get();
        list->plain = spec_.plainItems;
        for (std::size_t i = 0; i < spec_.items.size(); ++i) {
            auto box = std::make_unique<ui::Checkbox>(spec_.items[i].label, base + ".item" + std::to_string(i));
            box->setState(spec_.items[i].checked ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            if (spec_.plainItems) box->setVisibility(ui::Visibility::Collapsed);   // la ligne reste (sa place), sans case
            auto* raw = box.get();
            list->addChild(std::move(box));
            list->add(raw, spec_.items[i].detail);
            items_.push_back(raw);
            links_ += raw->stateChanged->connect([this](ui::Checkbox::State) { sync(); });
            if (spec_.items[i].checked && body->firstChecked < 0) body->firstChecked = static_cast<int>(i);
        }
        body->listContent = list;
        panel->setContent(std::move(content));
        body->list = &static_cast<ui::ScrollablePanel&>(body->addChild(std::move(panel)));
    }
    for (std::size_t i = 0; i < spec_.extras.size(); ++i) {
        auto box = std::make_unique<ui::Checkbox>(spec_.extras[i].label, base + ".extra" + std::to_string(i));
        box->setState(spec_.extras[i].checked ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        auto* raw = &static_cast<ui::Checkbox&>(body->addChild(std::move(box)));
        extras_.push_back(raw);
        body->extras.push_back(raw);
        links_ += raw->stateChanged->connect([this](ui::Checkbox::State) { sync(); });
    }
    if (!spec_.options.empty()) {
        group_ = std::make_shared<ui::RadioGroup>();
        for (std::size_t i = 0; i < spec_.options.size(); ++i) {
            const auto& o = spec_.options[i];
            auto* rb = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>(o.label, group_, static_cast<int>(i), base + ".option" + std::to_string(i))));
            radios_.push_back(rb);
            body->radios.push_back(rb);
            body->radioDetails.push_back(o.detail);
            ui::InputText* field = nullptr;
            ui::Widget* browse = nullptr;
            if (o.hasField) {
                auto f = std::make_unique<ui::InputText>(base + ".field" + std::to_string(i));
                f->setText(o.field);
                f->setPlaceholder(o.placeholder);
                field = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
                links_ += field->textChanged->connect([this](const std::string&) { sync(); });
                // Un champ de chemin : le bouton ... (l'explorateur de fichiers).
                if (o.browse.active()) {
                    auto b = std::make_unique<ui::BrowseButton>(*field, o.browse, base + ".parcourir" + std::to_string(i));
                    b->setFieldLabel(o.label);
                    // Le chemin choisi : le champ reprend le focus.
                    links_ += b->chosen->connect([this, field](const std::string&) {
                        focus().clear();
                        focus().focus(field);
                    });
                    browse = &body->addChild(std::move(b));
                }
            }
            fields_.push_back(field);
            browses_.push_back(browse);
            body->fields.push_back(field);
            body->browses.push_back(browse);
        }
        group_->setValue(std::clamp(spec_.option, 0, static_cast<int>(spec_.options.size()) - 1));
        links_ += group_->valueChanged->connect([this](int) { sync(); });
    }
    if (!spec_.note.empty()) body->note = &static_cast<TextBlock&>(body->addChild(std::make_unique<TextBlock>(spec_.note, nullptr, kSmall, true)));
    // Les boutons : Annuler, (Arreter), le principal.
    auto* cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(spec_.cancel, base + ".cancel")));
    body->buttons.push_back(cancel);
    links_ += cancel->clicked->connect([this] { finish(false); });
    if (!spec_.stopLabel.empty()) {
        stop_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(spec_.stopLabel, base + ".stop")));
        body->buttons.push_back(stop_);
        links_ += stop_->clicked->connect([this] {
            if (spec_.onStop) spec_.onStop();
        });
    }
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(spec_.confirm, base + ".ok")));
    ok_->setStyle(spec_.danger ? ui::Button::Style::Danger : ui::Button::Style::Primary);
    body->buttons.push_back(ok_);
    links_ += ok_->clicked->connect([this] { finish(true); });
    setRoot(std::move(body));
    sync();
    return core::ok();
}

void HmiAskDialog::sync() {
    if (!ok_) return;
    if (spec_.confirmLabel) {
        std::vector<bool> a, b;
        for (auto* x : items_) a.push_back(x->isChecked());
        for (auto* x : extras_) b.push_back(x->isChecked());
        const std::string label = spec_.confirmLabel(a, b, group_ ? group_->value() : 0);
        if (!label.empty() && label != ok_->text()) {
            ok_->setText(label);
            root().invalidateLayout();
        }
    }
    // Un champ sous un choix : actif seulement quand son choix l'est.
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const bool on = !group_ || group_->value() == static_cast<int>(i);
        if (fields_[i]) fields_[i]->setEnabled(on);
        if (i < browses_.size() && browses_[i]) browses_[i]->setEnabled(on);
    }
    if (spec_.ready) ok_->setEnabled(spec_.ready());
}

void HmiAskDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    if (spec_.live && text_) static_cast<TextBlock*>(text_)->poll();
    if (spec_.ready && ok_) {
        const bool ready = spec_.ready();
        if (ready != ok_->enabled()) ok_->setEnabled(ready);
        if (stop_) stop_->setEnabled(!ready);
    }
}

ui::EventResult HmiAskDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        // Entree : le bouton principal (pas dans un champ qui l'attend).
        if (k->key == ui::Key::Return && ok_ && ok_->enabled()) {
            finish(true);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

std::string HmiAskDialog::payload() const {
    std::vector<bool> a, b;
    for (auto* x : items_) a.push_back(x->isChecked());
    for (auto* x : extras_) b.push_back(x->isChecked());
    const int option = group_ ? group_->value() : 0;
    std::string field;
    if (option >= 0 && static_cast<std::size_t>(option) < fields_.size() && fields_[static_cast<std::size_t>(option)])
        field = fields_[static_cast<std::size_t>(option)]->text();
    return "items=" + joinBits(a) + "\nextras=" + joinBits(b) + "\noption=" + std::to_string(option) + "\nfield=" + field;
}

void HmiAskDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload()});
}

HmiAskDialog::Answer HmiAskDialog::parse(const std::string& payload) {
    Answer a;
    std::stringstream in(payload);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "items") a.items = splitBits(value);
        else if (key == "extras") a.extras = splitBits(value);
        else if (key == "option") a.option = std::atoi(value.c_str());
        else if (key == "field") a.field = value;
    }
    return a;
}

} // namespace app
