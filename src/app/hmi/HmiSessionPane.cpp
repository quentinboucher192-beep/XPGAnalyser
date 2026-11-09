// =============================================================================
//  app/hmi/HmiSessionPane.cpp - 1.45.0 : voir HmiSessionPane.hpp
// =============================================================================
#include "HmiSessionPane.hpp"

#include "../SessionMarks.hpp"

#include <algorithm>

namespace app {

namespace {

// Les lignes montrees : l'heure, qui, le texte (le journal : son genre en icone).
class SessionRows final : public ui::ITableModel {
public:
    explicit SessionRows(const std::vector<HmiSessionPane::Row>& rows) : rows_(rows) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? "Heure" : c == 1 ? "Qui" : "Message ou changement";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_.size()) return {};
        const auto& x = rows_[r];
        if (c == 0) return x.time;
        if (c == 1) return x.system ? std::string("Session") : x.who;
        return x.count > 1 ? x.text + "  (\xC3\x97" + std::to_string(x.count) + ")" : x.text;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= rows_.size()) return st;
        const auto& x = rows_[r];
        if (c == 0) st.monospace = true;
        if (c == 1 && !x.system && x.color) {
            // Les initiales dans la couleur du membre, devant son nom.
            ui::ColorPill p;
            std::string ini;
            for (std::size_t i = 0; i < x.who.size() && ini.size() < 2; ++i)
                if ((i == 0 || x.who[i - 1] == ' ') && static_cast<unsigned char>(x.who[i]) < 0x80 && x.who[i] != ' ') ini.push_back(x.who[i]);
            if (ini.empty()) ini = "?";
            for (auto& ch : ini) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
            p.text = ini;
            p.fill = gfx::Color::rgb(x.color);
            p.fillTo = gfx::Color::rgb(x.color);
            p.border = gfx::Color::rgb(x.color);
            p.ink = gfx::Color::rgb(0xFFFFFF);
            st.colorLead = p;
        }
        if (c == 2) {
            st.icon = x.journal ? ui::Icon::History : x.system ? ui::Icon::Info : ui::Icon::User;
            st.iconTone = x.journal ? ui::Tone::Accent : x.system ? ui::Tone::Muted : ui::Tone::None;
            if (x.system) st.fgTone = ui::Tone::Muted;
        }
        if (x.mine && c == 1) st.bold = true;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= rows_.size()) return {};
        const auto& x = rows_[r];
        return x.journal ? "Journal de la session \xC2\xB7 " + x.who + " \xC2\xB7 " + x.time
                         : x.system ? std::string("La session") : "Discussion \xC2\xB7 " + x.who + " \xC2\xB7 " + x.time;
    }

private:
    std::vector<HmiSessionPane::Row> rows_;
};

// Le champ : Entree envoie (pas la perte du focus).
class SessionField final : public ui::InputText {
public:
    using ui::InputText::InputText;
    std::function<void()> onSubmit;

protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Return && !suggestionsOpen() && !text().empty()) {
            if (onSubmit) onSubmit();
            return ui::EventResult::Consumed;
        }
        return ui::InputText::onEvent(ev);
    }
};

} // namespace

HmiSessionPane::HmiSessionPane(std::string id) : ui::Widget(std::move(id)) {
    const auto button = [this](const std::string& text, const std::string& key, ui::Button::Style style) {
        auto* b = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(text, this->id() + "." + key)));
        b->setStyle(style);
        return b;
    };
    all_ = button("Tout", "tout", ui::Button::Style::Primary);
    chat_ = button("Discussion", "discussion", ui::Button::Style::Flat);
    journal_ = button("Journal", "journal", ui::Button::Style::Flat);
    export_ = button("Exporter le journal (CSV)", "exporter", ui::Button::Style::Flat);
    links_ += all_->clicked->connect([this] { setFilter(Filter::All); });
    links_ += chat_->clicked->connect([this] { setFilter(Filter::Chat); });
    links_ += journal_->clicked->connect([this] { setFilter(Filter::Journal); });
    links_ += export_->clicked->connect([this] {
        std::string why;
        const std::string path = sessionmarks::exportJournal(&why);
        if (said) said(path.empty() ? "Le journal ne s'exporte pas : " + why + "." : "Journal export\xC3\xA9 : " + path, !path.empty());
    });
    auto table = std::make_unique<ui::TableView>(this->id() + ".lignes");
    table->setColumns({{"Heure", 64.f}, {"Qui", 150.f}, {"Message ou changement", 900.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    auto field = std::make_unique<SessionField>(this->id() + ".message");
    field->setPlaceholder("\xC3\x89" "cris \xC3\xA0 l'\xC3\xA9quipe\xE2\x80\xA6 (Entr\xC3\xA9" "e)");
    field->setMaxLength(2000);
    auto* raw = field.get();
    field_ = &static_cast<ui::InputText&>(addChild(std::move(field)));
    raw->onSubmit = [this] { (void)send(field_->text()); };
    sendButton_ = button("Envoyer", "envoyer", ui::Button::Style::Primary);
    links_ += sendButton_->clicked->connect([this] { (void)send(field_->text()); });
    refresh();
}

void HmiSessionPane::setFilter(Filter f) {
    if (f == filter_) return;
    filter_ = f;
    all_->setStyle(f == Filter::All ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    chat_->setStyle(f == Filter::Chat ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    journal_->setStyle(f == Filter::Journal ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    rebuild();
}

std::string HmiSessionPane::rowText(std::size_t i) const {
    if (i >= rows_.size()) return {};
    const auto& r = rows_[i];
    std::string s = r.time + " " + (r.system ? std::string("Session") : r.who) + " : " + r.text;
    if (r.count > 1) s += " (\xC3\x97" + std::to_string(r.count) + ")";
    return s;
}

bool HmiSessionPane::send(const std::string& text) {
    std::string t = text;
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
    if (t.empty() || !sessionmarks::active()) return false;
    sessionmarks::say(t);
    if (field_) field_->setText({});
    return true;
}

void HmiSessionPane::refresh() {
    if (sessionmarks::boardRevision() == seen_) return;
    seen_ = sessionmarks::boardRevision();
    rebuild();
}

void HmiSessionPane::rebuild() {
    const auto& b = sessionmarks::board();
    title_ = b.sessionName.empty() ? std::string{}
                                   : "\xC2\xAB " + b.sessionName + " \xC2\xBB \xC2\xB7 " + std::to_string(b.people.size()) + " personne"
                                         + (b.people.size() > 1 ? "s" : "") + " \xC2\xB7 h\xC3\xB4te : " + b.hostName;
    if (export_) export_->setVisibility(b.host ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    rows_.clear();
    std::vector<Row> chat, journal;
    if (filter_ != Filter::Journal)
        for (const auto& l : b.chat) chat.push_back(Row{l.time, l.from, l.text, l.color, false, l.system, l.mine, 1});
    if (filter_ != Filter::Chat)
        for (const auto& l : b.journal) journal.push_back(Row{l.time, l.from, l.text, l.color, true, false, l.mine, l.count});
    // Meles dans l'ordre des heures (chacun l'est deja) ; a la meme heure, la discussion d'abord.
    std::size_t i = 0, j = 0;
    while (i < chat.size() || j < journal.size()) {
        if (j >= journal.size() || (i < chat.size() && chat[i].time <= journal[j].time)) rows_.push_back(std::move(chat[i++]));
        else rows_.push_back(std::move(journal[j++]));
    }
    model_ = std::make_shared<SessionRows>(rows_);
    if (table_) {
        table_->setModel(model_);
        table_->setScrollOffset(1e9f);   // la derniere ligne
    }
    invalidateLayout();
    invalidate();
}

void HmiSessionPane::onLayout() {
    const auto r = bounds();
    float x = r.x + 4.f;
    for (auto* b : {all_, chat_, journal_}) {
        const float w = ui::measureWidth(b->text(), gfx::FontId{16}) + 26.f;
        b->setBounds({x, r.y + 4.f, w, 26.f});
        x += w + 4.f;
    }
    if (export_ && export_->visible()) {
        const float w = ui::measureWidth(export_->text(), gfx::FontId{16}) + 26.f;
        export_->setBounds({r.right() - w - 4.f, r.y + 4.f, w, 26.f});
    }
    titleAt_ = x + 8.f;
    const float fh = 30.f;
    if (table_) table_->setBounds({r.x, r.y + 34.f, r.w, std::max(0.f, r.h - 34.f - fh - 8.f)});
    const float bw = 96.f;
    if (sendButton_) sendButton_->setBounds({r.right() - bw - 4.f, r.bottom() - fh - 4.f, bw, fh});
    if (field_) field_->setBounds({r.x + 4.f, r.bottom() - fh - 4.f, std::max(40.f, r.w - bw - 16.f), fh});
}

void HmiSessionPane::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.panelBg);
    if (!title_.empty() && titleAt_ > 0.f) {
        const float right = export_ && export_->visible() ? export_->bounds().x - 8.f : bounds().right() - 8.f;
        const auto f = ctx.theme.font.smallUi;
        const auto n = ctx.r.fitCharacters(title_, f, std::max(0.f, right - titleAt_));
        if (n > 0)
            ctx.r.drawText({titleAt_, bounds().y + 4.f + (26.f - ctx.r.lineHeight(f)) * 0.5f}, n >= title_.size() ? title_ : title_.substr(0, n) + "\xE2\x80\xA6", f,
                           c.textMuted);
    }
}

} // namespace app
