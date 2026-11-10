// =============================================================================
//  app/hmi/HmiSessionPane.cpp - 1.45.0 : voir HmiSessionPane.hpp
//  1.47.0 : le texte dans son cadre a toute taille (la liste de SessionText, un
//  en-tete et un pied qui tiennent dans la largeur et la hauteur du moment).
// =============================================================================
#include "HmiSessionPane.hpp"

#include "../SessionMarks.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace {

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

const std::string kExportFull = "Exporter le journal (CSV)";
const std::string kExportMid = "Exporter (CSV)";
const std::string kExportShort = "CSV";
const std::string kExportTip = "Le journal de la session en CSV, dans exports/ du projet (l'h\xC3\xB4te)";
constexpr float kHeaderH = 26.f, kFooterH = 30.f, kPad = 4.f;

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
    export_ = button(kExportFull, "exporter", ui::Button::Style::Flat);
    links_ += all_->clicked->connect([this] { setFilter(Filter::All); });
    links_ += chat_->clicked->connect([this] { setFilter(Filter::Chat); });
    links_ += journal_->clicked->connect([this] { setFilter(Filter::Journal); });
    links_ += export_->clicked->connect([this] {
        std::string why;
        const std::string path = sessionmarks::exportJournal(&why);
        if (said) said(path.empty() ? "Le journal ne s'exporte pas : " + why + "." : "Journal export\xC3\xA9 : " + path, !path.empty());
    });
    list_ = &static_cast<sessionui::LineList&>(addChild(std::make_unique<sessionui::LineList>(this->id() + ".lignes")));
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

std::string HmiSessionPane::rowText(std::size_t i) const { return i < rows_.size() ? sessionui::rowText(rows_[i]) : std::string{}; }

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
    host_ = b.host;
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
    if (list_) {
        list_->setEmptyText(filter_ == Filter::Chat      ? "Aucun message encore : \xC3\xA9" "cris \xC3\xA0 l'\xC3\xA9quipe dans le champ du bas (Entr\xC3\xA9" "e)."
                            : filter_ == Filter::Journal ? "Aucun changement encore : chaque changement de la session s'inscrit ici (qui, quand, quoi)."
                                                         : "Rien encore : la discussion et le journal de la session s'inscrivent ici.");
        list_->setRows(rows_);
    }
    invalidateLayout();
    invalidate();
}

void HmiSessionPane::onLayout() {
    const auto r = bounds();
    const float w = std::max(0.f, r.w - 2.f * kPad);
    const float x = r.x + kPad;
    // Ce qui reste a un panneau tres bas : le champ et Envoyer d'abord, puis la liste, puis l'en-tete.
    const bool header = r.h >= kPad + kHeaderH + kPad + kFooterH + kPad;
    const bool footer = r.h >= kFooterH + 2.f * kPad;
    // ---- l'en-tete : les filtres a gauche, Exporter a droite (l'hote), le titre entre les deux ----
    for (auto* b : {all_, chat_, journal_}) b->setVisibility(header ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    export_->setVisibility(header && host_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    titleRect_ = {};
    titleShown_.clear();
    float headerBottom = r.y;
    if (header) {
        const float y = r.y + kPad;
        std::vector<sessionui::ButtonSpec> filters{{all_, "Tout", {}, 36.f, {}},
                                                   {chat_, "Discussion", {}, 36.f, "Les messages seuls"},
                                                   {journal_, "Journal", {}, 36.f, "Les changements seuls (qui, quand, quoi)"}};
        float filtersW = 8.f;
        for (const auto& f : filters) filtersW += sessionui::buttonWidth(f.full);
        float exportW = 0.f;
        std::string exportLabel;
        if (host_) {
            // Le libelle le plus long qui laisse les filtres entiers et un peu de titre ; sinon les filtres entiers ;
            // sinon << CSV >>, et les filtres se serrent (3 x 36 px au moins).
            const float titleMin = title_.empty() ? 0.f : 80.f;
            for (const float extra : {titleMin, 0.f}) {
                for (const auto* label : {&kExportFull, &kExportMid, &kExportShort})
                    if (filtersW + 8.f + sessionui::buttonWidth(*label) + extra <= w) {
                        exportLabel = *label;
                        break;
                    }
                if (!exportLabel.empty()) break;
            }
            if (exportLabel.empty()) {
                exportLabel = kExportShort;
                exportW = std::min(sessionui::buttonWidth(kExportShort), std::max(36.f, w - 8.f - 3.f * 36.f - 8.f));
            } else {
                exportW = sessionui::buttonWidth(exportLabel);
            }
            if (exportW < 36.f) {   // trop etroit pour les filtres et Exporter : Exporter reste dans la fenetre de la session
                export_->setVisibility(ui::Visibility::Collapsed);
                exportW = 0.f;
            } else {
                export_->setBounds({x + w - exportW, y, exportW, kHeaderH});
                sessionui::fitButton(*export_, kExportFull, exportW, exportLabel == kExportFull ? std::string{} : exportLabel, kExportTip);
            }
        }
        const bool exportShown = host_ && exportW > 0.f;
        const float filtersRoom = std::max(0.f, w - (exportShown ? exportW + 8.f : 0.f));
        (void)sessionui::fitButtons(filters, x, y, filtersRoom, kHeaderH, 4.f);
        const float titleX = journal_->bounds().right() + 10.f;
        const float titleRight = exportShown ? export_->bounds().x - 10.f : x + w;
        if (!title_.empty() && titleRight - titleX >= 40.f) {
            titleRect_ = {titleX, y, titleRight - titleX, kHeaderH};
            titleShown_ = sessionui::elide(title_, gfx::FontId{13}, titleRect_.w);
        }
        headerBottom = y + kHeaderH;
    }
    // ---- le pied : le champ et Envoyer ----
    field_->setVisibility(footer ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    sendButton_->setVisibility(footer ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    float footerTop = r.bottom();
    if (footer) {
        footerTop = r.bottom() - kPad - kFooterH;
        const float sw = std::clamp(std::floor(w * 0.3f), 44.f, 96.f);
        sendButton_->setBounds({x + w - sw, footerTop, sw, kFooterH});
        sessionui::fitButton(*sendButton_, "Envoyer", sw, {}, "Envoyer \xC3\xA0 l'\xC3\xA9quipe (Entr\xC3\xA9" "e)");
        field_->setBounds({x, footerTop, std::max(0.f, w - sw - 6.f), kFooterH});
    }
    // ---- la liste entre les deux ----
    const float top = headerBottom + kPad, bottom = footerTop - kPad;
    const bool listShown = bottom - top >= 16.f;
    list_->setVisibility(listShown ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (listShown) list_->setBounds({r.x, top, r.w, bottom - top});
}

void HmiSessionPane::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.panelBg);
    if (!titleShown_.empty() && titleRect_.w > 0.f) {
        const gfx::FontId f{13};
        ctx.r.drawText({titleRect_.x, titleRect_.y + (titleRect_.h - ctx.r.lineHeight(f)) * 0.5f}, titleShown_, f, c.textMuted);
    }
}

std::string HmiSessionPane::liveTooltip(gfx::Point mouse) const {
    if (titleRect_.w > 0.f && titleRect_.contains(mouse)) return title_;
    return {};
}

} // namespace app
