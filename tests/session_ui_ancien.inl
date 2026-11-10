// =============================================================================
//  tests/session_ui_ancien.inl - 1.47.0 : la fenetre de la session TELLE QU'EN 1.46 (pour la capture << avant >>)
// -----------------------------------------------------------------------------
//  Le code de mise en page et de dessin de la 1.46.0 (app/SessionDialogs.cpp,
//  PanelBody, commit 7a4dea5), recopie tel quel ; seules ses lectures du service
//  des sessions passent par le meme Snapshot que la fenetre de la 1.47 (les
//  memes noms, les memes messages). Il ne sert qu'a l'image << avant >> de la
//  capture du client (--captures ... --prefixe avant_) : il n'est verifie par rien.
// =============================================================================

namespace ancien {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

float buttonWidth(const std::string& text) { return std::max(88.f, ui::measureWidth(text, kButton) + 32.f); }

std::vector<std::string> wrapWords(const std::string& s, gfx::FontId f, float w) {
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
        } else word.push_back(c);
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// ---- l'onglet Session tel qu'en 1.46 (app/hmi/HmiSessionPane.cpp, commit 7a4dea5 : un tableau, une ligne par message) ----
struct OldRow {
    std::string time, who, text;
    std::uint32_t color{0};
    bool journal{false}, system{false}, mine{false};
    int count{1};
};
class OldSessionPane final : public ui::Widget {
public:
    enum class Filter { All, Chat, Journal };
    explicit OldSessionPane(std::string id);
    void refresh();
    void setFilter(Filter f);
    bool send(const std::string& text);
    std::function<void(const std::string&, bool ok)> said;
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    void rebuild();
    std::vector<OldRow> rows_;
    Filter filter_{Filter::All};
    std::uint64_t seen_{~0ull};
    ui::Button* all_{nullptr};
    ui::Button* chat_{nullptr};
    ui::Button* journal_{nullptr};
    ui::Button* export_{nullptr};
    ui::Button* sendButton_{nullptr};
    ui::InputText* field_{nullptr};
    ui::TableView* table_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::string title_;
    float titleAt_{0.f};
    core::ConnectionScope links_;
};
// Les lignes montrees : l'heure, qui, le texte (le journal : son genre en icone).
class SessionRows final : public ui::ITableModel {
public:
    explicit SessionRows(const std::vector<OldRow>& rows) : rows_(rows) {}
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
    std::vector<OldRow> rows_;
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


OldSessionPane::OldSessionPane(std::string id) : ui::Widget(std::move(id)) {
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
        const std::string path = app::sessionmarks::exportJournal(&why);
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

void OldSessionPane::setFilter(Filter f) {
    if (f == filter_) return;
    filter_ = f;
    all_->setStyle(f == Filter::All ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    chat_->setStyle(f == Filter::Chat ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    journal_->setStyle(f == Filter::Journal ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    rebuild();
}

bool OldSessionPane::send(const std::string& text) {
    std::string t = text;
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
    if (t.empty() || !app::sessionmarks::active()) return false;
    app::sessionmarks::say(t);
    if (field_) field_->setText({});
    return true;
}

void OldSessionPane::refresh() {
    if (app::sessionmarks::boardRevision() == seen_) return;
    seen_ = app::sessionmarks::boardRevision();
    rebuild();
}

void OldSessionPane::rebuild() {
    const auto& b = app::sessionmarks::board();
    title_ = b.sessionName.empty() ? std::string{}
                                   : "\xC2\xAB " + b.sessionName + " \xC2\xBB \xC2\xB7 " + std::to_string(b.people.size()) + " personne"
                                         + (b.people.size() > 1 ? "s" : "") + " \xC2\xB7 h\xC3\xB4te : " + b.hostName;
    if (export_) export_->setVisibility(b.host ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    rows_.clear();
    std::vector<OldRow> chat, journal;
    if (filter_ != Filter::Journal)
        for (const auto& l : b.chat) chat.push_back(OldRow{l.time, l.from, l.text, l.color, false, l.system, l.mine, 1});
    if (filter_ != Filter::Chat)
        for (const auto& l : b.journal) journal.push_back(OldRow{l.time, l.from, l.text, l.color, true, false, l.mine, l.count});
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

void OldSessionPane::onLayout() {
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

void OldSessionPane::onPaint(const ui::PaintContext& ctx) {
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


// ---- la fenetre de la session telle qu'en 1.46 ----
class PanelBody final : public ui::Widget {
public:
    explicit PanelBody(app::sessionwin::Snapshot s) : snap_(std::move(s)) {}
    struct Row { ui::Button* follow{nullptr}; ui::Button* role{nullptr}; ui::Button* remove{nullptr}; };
    struct KnockRow { ui::Button* yes{nullptr}; ui::Button* no{nullptr}; };
    std::vector<Row> rows;
    std::vector<KnockRow> knockRows;
    ui::Button* copy{nullptr};
    ui::InputText* field{nullptr};
    ui::Button* send{nullptr};
    ui::Button* stop{nullptr};
    ui::Button* close{nullptr};
    ui::Button* chatView{nullptr};
    ui::Button* journalView{nullptr};
    ui::Button* exportJournal{nullptr};
    bool showJournal{false};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(900.f, std::max(560.f, r.w - 60.f));
        const float h = std::min(620.f, std::max(420.f, r.h - 60.f));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        left_ = {panel_.x + 16.f, panel_.y + 34.f + 12.f, std::floor(w * 0.5f) - 24.f, panel_.h - 34.f - 12.f - 60.f};
        right_ = {left_.right() + 16.f, left_.y, panel_.right() - 16.f - (left_.right() + 16.f), left_.h};
        float y = left_.y + 24.f;
        codeAt_ = -1.f;
        if (snap_.code) {
            codeAt_ = y;
            if (copy) copy->setBounds({left_.right() - buttonWidth(copy->text()), y + 6.f, buttonWidth(copy->text()), 28.f});
            y += 44.f;
        }
        rowsAt_ = y;
        textRight_.clear();
        for (auto& row : rows) {
            float bx = left_.right();
            const bool two = row.remove != nullptr;
            for (auto* b : {row.follow, row.role}) {
                if (!b) continue;
                const float bw = std::min(160.f, buttonWidth(b->text()) - 10.f);
                bx -= bw;
                b->setBounds({bx, two ? y + 2.f : y + 12.f, bw, two ? 23.f : 26.f});
                bx -= 6.f;
            }
            float left = bx;
            if (row.remove) {
                const float bw = std::min(200.f, buttonWidth(row.remove->text()) - 10.f);
                row.remove->setBounds({left_.right() - bw, y + 27.f, bw, 23.f});
                left = std::min(left, left_.right() - bw - 6.f);
            }
            textRight_.push_back(left - 6.f);
            y += 52.f;
        }
        knocksAt_ = y + 8.f;
        y = knocksAt_ + 22.f;
        for (auto& k : knockRows) {
            float bx = left_.right();
            for (auto* b : {k.no, k.yes}) {
                const float bw = buttonWidth(b->text()) - 10.f;
                bx -= bw;
                b->setBounds({bx, y + 10.f, bw, 26.f});
                bx -= 6.f;
            }
            y += 48.f;
        }
        {
            float hx = right_.right();
            for (auto* b : {exportJournal, journalView, chatView}) {
                if (!b) continue;
                const float bw = buttonWidth(b->text()) - 16.f;
                hx -= bw;
                b->setBounds({hx, right_.y - 4.f, bw, 24.f});
                hx -= 4.f;
            }
        }
        const float fy = right_.bottom() - 32.f;
        if (send) send->setBounds({right_.right() - 96.f, fy, 96.f, 30.f});
        if (field) field->setBounds({right_.x, fy, right_.w - 104.f, 30.f});
        const float by = panel_.bottom() - 44.f;
        float bx = panel_.right() - 16.f;
        for (auto* b : {close, stop}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 10.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = 34.f;
        r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold, ui = ctx.theme.font.ui;
        r.drawText({panel_.x + 12.f, panel_.y + (titleH - r.lineHeight(bold)) * 0.5f}, snap_.title, bold, c.text);
        r.drawText({left_.x, left_.y}, snap_.teamTitle, bold, c.text);
        if (codeAt_ >= 0.f) {
            const gfx::Rect box{left_.x, codeAt_, left_.w, 38.f};
            r.fillRoundedRect(box, c.inputBg, 6.f);
            r.drawText({box.x + 10.f, box.y + 3.f}, snap_.codeLabel, kSmall, c.textMuted);
            r.drawText({box.x + 10.f, box.y + 18.f}, snap_.codeText, ctx.theme.font.mono, c.text);
        }
        float y = rowsAt_;
        std::size_t rowIndex = 0;
        for (const auto& m : snap_.people) {
            const float textRight = rowIndex < textRight_.size() ? textRight_[rowIndex] : left_.right();
            ++rowIndex;
            const auto col = gfx::Color::rgb(m.color);
            const gfx::Rect dot{left_.x, y + 8.f, 32.f, 32.f};
            r.fillRoundedRect(dot, col, 16.f);
            r.drawText({dot.x + (32.f - r.measure(m.initials, kSmall).width) * 0.5f, dot.y + (32.f - r.lineHeight(kSmall)) * 0.5f}, m.initials, kSmall,
                       gfx::Color::rgb(0xFFFFFF));
            const std::string who = m.tag.empty() ? m.name : m.name + " " + m.tag;
            {
                const auto nw = r.fitCharacters(who, ui, std::max(60.f, textRight - (left_.x + 42.f)));
                r.drawText({left_.x + 42.f, y + 6.f}, nw >= who.size() ? who : who.substr(0, nw) + "\xE2\x80\xA6", ui, c.text);
            }
            const float maxW = textRight - (left_.x + 42.f);
            const auto n = r.fitCharacters(m.where, kSmall, std::max(60.f, maxW - 12.f));
            r.drawText({left_.x + 42.f, y + 27.f}, n >= m.where.size() ? m.where : m.where.substr(0, n) + "\xE2\x80\xA6", kSmall, c.textMuted);
            y += 52.f;
        }
        if (!snap_.knocks.empty()) {
            r.drawText({left_.x, knocksAt_}, "Ils veulent entrer", bold, c.warning);
            float ky = knocksAt_ + 22.f;
            for (const auto& k : snap_.knocks) {
                r.drawText({left_.x, ky + 6.f}, k.name, ui, c.text);
                r.drawText({left_.x, ky + 26.f}, k.detail, kSmall, c.textMuted);
                ky += 48.f;
            }
        }
        r.drawText({right_.x, right_.y}, showJournal ? "Le journal" : "La discussion", bold, c.text);
        const gfx::Rect box{right_.x, right_.y + 24.f, right_.w, right_.h - 24.f - 40.f};
        r.fillRoundedRect(box, c.inputBg, 6.f);
        std::vector<std::pair<std::string, gfx::Color>> lines;
        if (showJournal) {
            if (snap_.journal.empty())
                lines.emplace_back("Aucun changement encore : chaque changement de la session s'inscrit ici (qui, quand, quoi).", c.textMuted);
            for (const auto& e : snap_.journal) {
                bool first = true;
                for (const auto& w : wrapWords(app::sessionui::rowText(e), kSmall, box.w - 20.f)) {
                    lines.emplace_back(w, first && e.color ? gfx::Color::rgb(e.color) : c.text);
                    first = false;
                }
            }
        }
        for (const auto& l : showJournal ? std::vector<app::sessionui::Row>{} : snap_.chat) {
            const std::string text = l.system ? l.text : l.who + " : " + l.text;
            const auto col = l.system ? c.textMuted : c.text;
            bool first = true;
            for (const auto& w : wrapWords((l.time.empty() ? std::string{} : l.time + "  ") + text, kSmall, box.w - 20.f)) {
                lines.emplace_back(w, first && !l.system && l.color ? gfx::Color::rgb(l.color) : col);
                first = false;
            }
        }
        const float lh = 18.f;
        const std::size_t fit = static_cast<std::size_t>(std::max(1.f, std::floor((box.h - 12.f) / lh)));
        const std::size_t from = lines.size() > fit ? lines.size() - fit : 0;
        float ly = box.y + 6.f;
        for (std::size_t i = from; i < lines.size(); ++i) {
            r.drawText({box.x + 10.f, ly}, lines[i].first, kSmall, lines[i].second);
            ly += lh;
        }
        r.drawText({panel_.x + 16.f, panel_.bottom() - 36.f}, snap_.hint, kSmall, c.textMuted);
    }

private:
    app::sessionwin::Snapshot snap_;
    gfx::Rect panel_{}, left_{}, right_{};
    float codeAt_{-1.f}, rowsAt_{0.f}, knocksAt_{0.f};
    std::vector<float> textRight_;
};

// La fenetre de l'hote, comme en 1.46 (les memes boutons que hostWindow).
std::unique_ptr<PanelBody> hostWindow(bool journal, bool journalEmpty) {
    auto snap = hostSnapshot(journal, journalEmpty);
    auto body = std::make_unique<PanelBody>(snap);
    auto& b = *body;
    auto button = [&](const std::string& text, ui::Button::Style st) {
        auto* x = &static_cast<ui::Button&>(b.addChild(std::make_unique<ui::Button>(text, "ancien")));
        x->setStyle(st);
        return x;
    };
    b.copy = button("Copier le code", ui::Button::Style::Flat);
    for (const auto& p : snap.people) {
        PanelBody::Row row;
        if (p.number != 0) {
            row.follow = button("Suivre", ui::Button::Style::Default);
            row.role = button(p.number == 2 ? "Laisser modifier" : "Regarder seulement", ui::Button::Style::Default);
            row.remove = button("Retirer de la session\xE2\x80\xA6", ui::Button::Style::Danger);
        }
        b.rows.push_back(row);
    }
    b.knockRows.push_back({button("Faire entrer", ui::Button::Style::Primary), button("Refuser", ui::Button::Style::Default)});
    b.chatView = button("Discussion", journal ? ui::Button::Style::Flat : ui::Button::Style::Primary);
    b.journalView = button("Journal \xC2\xB7 " + std::to_string(snap.journal.size()), journal ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    if (journal) b.exportJournal = button("Exporter (CSV)", ui::Button::Style::Flat);
    b.field = &static_cast<ui::InputText&>(b.addChild(std::make_unique<ui::InputText>("ancien.message")));
    b.field->setPlaceholder("\xC3\x89" "cris \xC3\xA0 l'\xC3\xA9quipe\xE2\x80\xA6 (Entr\xC3\xA9" "e)");
    b.send = button("Envoyer", ui::Button::Style::Primary);
    b.stop = button("Arr\xC3\xAAter la session", ui::Button::Style::Danger);
    b.close = button("Fermer", ui::Button::Style::Default);
    b.showJournal = journal;
    return body;
}

} // namespace ancien

// Les captures << avant >> (--ancien) : la fenetre de la session et l'onglet Session de la 1.46, aux memes tailles et
// avec les memes donnees que celles d'apres.
void fenetreSessionAvant() {
    if (g_captures.empty()) return;
    {
        namespace sm = app::sessionmarks;
        sm::setActive(true);
        sm::setBoard(board());
        ancien::OldSessionPane pane("sortie.session");
        for (const auto& [w, h] : std::vector<std::pair<float, float>>{{320.f, 96.f}, {640.f, 220.f}, {1400.f, 300.f}}) {
            pane.setBounds({0.f, 0.f, w, h});
            capture(pane, {w, h}, "onglet_session_" + std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h)) + ".png");
        }
        sm::setActive(false);
        sm::setBoard({});
    }
    for (const auto& [w, h] : std::vector<std::pair<float, float>>{{480.f, 520.f}, {943.f, 609.f}, {1600.f, 1000.f}}) {
        const std::string size = std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h));
        auto j = ancien::hostWindow(true, true);
        j->setBounds({0.f, 0.f, w, h});
        capture(*j, {w, h}, "fenetre_session_" + size + "_journal_vide.png");
        auto d = ancien::hostWindow(false, false);
        d->setBounds({0.f, 0.f, w, h});
        capture(*d, {w, h}, "fenetre_session_" + size + "_discussion.png");
    }
}
