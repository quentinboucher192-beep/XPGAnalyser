// =============================================================================
//  app/hmi/HmiEditHistoryDialog.cpp - 1.12.26 : la fenetre Historique d'un objet
//  (voir HmiEditHistoryDialog.hpp).
// =============================================================================
#include "HmiEditHistoryDialog.hpp"
#include "HmiImages.hpp"   // hmiProjectFolder()
#include "../../ui/DialogFrame.hpp"
#include "../../ui/widgets/PathBrowse.hpp"
#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <utility>

namespace app {

namespace edits = hmi::edits;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kText{15};
constexpr float kListW = 300.f;       // la liste des enregistrements
constexpr float kDayH = 26.f;         // un jour
constexpr float kRowH = 48.f;         // un enregistrement
constexpr float kLineH = 30.f;        // une ligne du detail
constexpr float kButtonsH = 52.f;     // la bande des boutons
const std::string kDot = " \xC2\xB7 ";

std::string lowered(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string csvField(const std::string& s) {
    if (s.find_first_of(";\"\n\r") == std::string::npos) return s;
    std::string q = "\"";
    for (const char ch : s) {
        if (ch == '"') q += "\"\"";
        else q += ch;
    }
    return q + "\"";
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// La largeur d'un texte, et le texte coupe a cette largeur (« ... »).
std::string fit(const std::string& s, gfx::FontId f, float w) {
    if (ui::measureWidth(s, f) <= w) return s;
    std::string t = s;
    while (!t.empty() && ui::measureWidth(t + "\xE2\x80\xA6", f) > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // un caractere entier
        if (!t.empty() && (static_cast<unsigned char>(t.back()) & 0x80)) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

} // namespace

// ============================================================================== le corps ===
class HmiEditHistoryDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiEditHistoryDialog& d) : d_(d) {}
    gfx::Rect panel{};
    void scrollListTo(int i) {
        if (i < 0) return;
        float y = 0;
        for (std::size_t k = 0; k < rows_.size(); ++k) {
            if (rows_[k].entry == i) {
                const float top = y, bottom = y + kRowH;
                if (top < listScroll_) listScroll_ = top;
                if (bottom > listScroll_ + list_.h) listScroll_ = bottom - list_.h;
                break;
            }
            y += rows_[k].entry < 0 ? kDayH : kRowH;
        }
    }
    void rebuild() {
        rows_.clear();
        std::string day;
        for (std::size_t i = 0; i < d_.shown_.size(); ++i) {
            const auto& e = d_.all_[d_.shown_[i]];
            const bool pending = d_.isPending(i);
            const std::string thisDay = pending ? std::string("pending") : edits::dateText(e.t).substr(0, 10);
            if (thisDay != day) {
                rows_.push_back({-1, pending ? std::string("Non enregistr\xC3\xA9") : thisDay});
                day = thisDay;
            }
            rows_.push_back({static_cast<int>(i), {}});
        }
        detailScroll_ = 0;
        invalidate();
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1120.f, r.w - 40.f), h = std::min(700.f, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        const float th = ui::dialogframe::kTitleBand;
        // la bande des filtres : les puces a gauche, le PC et la recherche a droite
        const float fy = panel.y + th + 10.f;
        float x = panel.x + 14.f;
        chips_.clear();
        const auto chip = [&](std::optional<edits::Sort> s, std::string label) {
            const float cw = ui::measureWidth(label, kSmall) + 22.f;
            chips_.push_back({s, std::move(label), {x, fy, cw, 26.f}});
            x += cw + 6.f;
        };
        chip(std::nullopt, "Tout");
        for (const auto s : {edits::Sort::Object, edits::Sort::Geometry, edits::Sort::Appearance, edits::Sort::Bindings, edits::Sort::Actions})
            chip(s, std::string(edits::sortLabel(s)));
        float right = panel.right() - 14.f;
        float rowY = fy;
        if (x + 420.f > right) rowY = fy + 34.f;   // etroit : le PC et la recherche dessous
        if (d_.searchField_) d_.searchField_->setBounds({right - 200.f, rowY - 2.f, 200.f, 30.f});
        right -= 210.f;
        if (d_.pcBox_) d_.pcBox_->setBounds({right - 170.f, rowY - 2.f, 170.f, 30.f});
        pcLabelX_ = right - 170.f - 30.f;
        pcLabelY_ = rowY;
        const float top = std::max(fy, rowY) + 40.f;
        const float bottom = panel.bottom() - kButtonsH - (d_.confirming() ? 44.f : 0.f);
        list_ = {panel.x + 14.f, top, std::min(kListW, panel.w * 0.36f), bottom - top - 8.f};
        detail_ = {list_.right() + 14.f, top, panel.right() - 14.f - (list_.right() + 14.f), bottom - top - 8.f};
        // les boutons
        const float by = panel.bottom() - 44.f;
        float bx = panel.x + 14.f;
        for (ui::Button* b : {d_.copy_, d_.export_}) {
            if (!b) continue;
            const float bw = std::max(96.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 32.f);
            b->setBounds({bx, by, bw, 32.f});
            bx += bw + 10.f;
        }
        messageX_ = bx + 6.f;
        float ex = panel.right() - 14.f;
        for (ui::Button* b : {d_.close_, d_.revert_}) {
            if (!b) continue;
            const float bw = std::max(104.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
            ex -= bw;
            b->setBounds({ex, by, bw, 32.f});
            ex -= 10.f;
        }
        messageW_ = ex - messageX_ - 10.f;
        // la confirmation : au-dessus des boutons
        confirm_ = {panel.x + 14.f, by - 46.f, panel.w - 28.f, 38.f};
        float cx = confirm_.right() - 8.f;
        for (ui::Button* b : {d_.no_, d_.yes_}) {
            if (!b) continue;
            const float bw = std::max(90.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 30.f);
            cx -= bw;
            b->setBounds({cx, confirm_.y + 4.f, bw, 30.f});
            cx -= 8.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ui::dialogframe::kTitleBand;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        // les puces
        for (const auto& ch : chips_) {
            const bool on = ch.sort == d_.sort_;
            ctx.r.fillRect(ch.r, on ? c.selectionBg : c.inputBg);
            ctx.r.strokeRect(ch.r, on ? c.accent : c.border, 1.f);
            ctx.r.drawText({ch.r.x + 11.f, ch.r.y + (ch.r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, ch.label, kSmall, on ? c.selectionText : c.text);
        }
        ctx.r.drawText({pcLabelX_, pcLabelY_ + (26.f - ctx.r.lineHeight(kSmall)) * 0.5f}, "PC", kSmall, c.textMuted);
        paintList(ctx);
        paintDetail(ctx);
        // le message, a cote des boutons
        if (!d_.message_.empty() && messageW_ > 40.f)
            ctx.r.drawText({messageX_, panel.bottom() - 44.f + (32.f - ctx.r.lineHeight(kSmall)) * 0.5f}, fit(d_.message_, kSmall, messageW_), kSmall,
                           d_.messageBad_ ? c.error : c.ok);
        if (d_.confirming()) {
            ctx.r.fillRect(confirm_, ctx.theme.brand.card);
            ctx.r.strokeRect(confirm_, c.warning, 1.f);
            const float tw = confirm_.w - 230.f;
            ctx.r.drawText({confirm_.x + 12.f, confirm_.y + (confirm_.h - ctx.r.lineHeight(kSmall)) * 0.5f}, fit(d_.confirm_.text, kSmall, tw), kSmall, c.text);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
            if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
            for (const auto& ch : chips_)
                if (ch.r.contains(m->pos)) {
                    d_.setSortFilter(ch.sort);
                    return ui::EventResult::Consumed;
                }
            if (list_.contains(m->pos)) {
                float y = list_.y - listScroll_;
                for (const auto& row : rows_) {
                    const float h = row.entry < 0 ? kDayH : kRowH;
                    if (row.entry >= 0 && m->pos.y >= y && m->pos.y < y + h) {
                        d_.select(static_cast<std::size_t>(row.entry));
                        return ui::EventResult::Consumed;
                    }
                    y += h;
                }
                return ui::EventResult::Consumed;
            }
            for (const auto& [rect, change] : revertRects_)
                if (rect.contains(m->pos)) {
                    (void)d_.revertChange(change);
                    return ui::EventResult::Consumed;
                }
            if (restoreRect_.w > 0 && restoreRect_.contains(m->pos)) {
                (void)d_.restoreDeleted();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (list_.contains(w->pos)) {
                listScroll_ = std::clamp(listScroll_ - w->dy * 48.f, 0.f, std::max(0.f, listHeight() - list_.h));
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (detail_.contains(w->pos)) {
                detailScroll_ = std::clamp(detailScroll_ - w->dy * 40.f, 0.f, std::max(0.f, detailHeight_ - detail_.h));
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Chip {
        std::optional<edits::Sort> sort;
        std::string label;
        gfx::Rect r;
    };
    struct Row {
        int entry{-1};          // -1 : un jour
        std::string day;
    };
    [[nodiscard]] float listHeight() const {
        float h = 0;
        for (const auto& r : rows_) h += r.entry < 0 ? kDayH : kRowH;
        return h;
    }

    void paintList(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(list_, c.inputBg);
        ctx.r.strokeRect(list_, c.border, 1.f);
        ctx.r.pushClip(list_);
        float y = list_.y - listScroll_;
        if (rows_.empty())
            ctx.r.drawText({list_.x + 12.f, list_.y + 12.f}, d_.all_.empty() ? "Rien d'enregistr\xC3\xA9 encore." : "Rien ne r\xC3\xA9pond aux filtres.",
                           kSmall, c.textMuted);
        for (const auto& row : rows_) {
            const float h = row.entry < 0 ? kDayH : kRowH;
            if (y + h >= list_.y && y <= list_.bottom()) {
                if (row.entry < 0) {
                    ctx.r.fillRect({list_.x, y, list_.w, h}, c.headerBg);
                    ctx.r.drawText({list_.x + 10.f, y + (h - ctx.r.lineHeight(kSmall)) * 0.5f}, row.day, kSmall,
                                   row.day.rfind("Non", 0) == 0 ? c.warning : c.textMuted);
                } else {
                    const auto i = static_cast<std::size_t>(row.entry);
                    const auto& e = d_.all_[d_.shown_[i]];
                    const bool sel = d_.selected_ == row.entry;
                    if (sel) ctx.r.fillRect({list_.x + 1.f, y, list_.w - 2.f, h}, c.selectionBg);
                    const gfx::Color ink = sel ? c.selectionText : c.text;
                    const gfx::Color muted = sel ? c.selectionText : c.textMuted;
                    std::string line1, line2;
                    if (d_.isPending(i)) {
                        line1 = "Non enregistr\xC3\xA9";
                        line2 = "sera sign\xC3\xA9 " + e.pc + (e.account.empty() ? std::string{} : kDot + e.account);
                    } else {
                        line1 = edits::dateText(e.t).substr(11) + "  " + e.pc + (e.account.empty() ? std::string{} : kDot + e.account);
                    }
                    std::string what;
                    if (e.what == edits::What::Created) what = "Cr\xC3\xA9" "ation";
                    else if (e.what == edits::What::Deleted) what = "Suppression";
                    else {
                        for (const auto s : edits::sortsOf(e)) what += (what.empty() ? "" : kDot) + std::string(edits::sortLabel(s));
                        if (e.what == edits::What::Merged) what = plural(e.count, "enregistrement", "enregistrements") + " regroup\xC3\xA9s";
                    }
                    if (d_.spec_.objects.size() != 1) what = e.name + (what.empty() ? std::string{} : kDot + what);
                    if (!d_.isPending(i)) line2 = what;
                    else line2 = what + kDot + line2;
                    ctx.r.drawText({list_.x + 10.f, y + 6.f}, fit(line1, kText, list_.w - 20.f), kText, d_.isPending(i) && !sel ? c.warning : ink);
                    ctx.r.drawText({list_.x + 10.f, y + 26.f}, fit(line2, kSmall, list_.w - 20.f), kSmall, muted);
                }
            }
            y += h;
        }
        ctx.r.popClip();
    }

    void paintDetail(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        revertRects_.clear();
        restoreRect_ = {};
        ctx.r.fillRect(detail_, c.panelBg);
        ctx.r.strokeRect(detail_, c.border, 1.f);
        const auto* e = d_.selected_ >= 0 ? d_.entry(static_cast<std::size_t>(d_.selected_)) : nullptr;
        const float left = detail_.x + 14.f;
        float y = detail_.y + 12.f - detailScroll_;
        ctx.r.pushClip(detail_);
        if (!e) {
            ctx.r.drawText({left, y}, "Choisis un enregistrement \xC3\xA0 gauche.", kText, c.textMuted);
            ctx.r.drawText({left, y + 24.f},
                           "L'historique commence au premier Enregistrer avec IHM Studio 1.12.25 : chaque objet chang\xC3\xA9 y est not\xC3\xA9, sign\xC3\xA9 du PC.",
                           kSmall, c.textMuted);
            ctx.r.popClip();
            detailHeight_ = 60.f;
            return;
        }
        const bool pending = d_.isPending(static_cast<std::size_t>(d_.selected_));
        // l'en-tete : quand, ou, qui
        std::string head = pending ? "Non enregistr\xC3\xA9 \xE2\x80\x94 sera sign\xC3\xA9 " + e->pc + (e->account.empty() ? std::string{} : kDot + e->account)
                                         + " au prochain Enregistrer"
                                   : edits::dateText(e->t) + kDot + e->pc + (e->account.empty() ? std::string{} : kDot + e->account)
                                         + (e->version.empty() ? std::string{} : kDot + "IHM Studio " + e->version);
        ctx.r.drawText({left, y}, fit(head, ctx.theme.font.uiBold, detail_.w - 28.f), ctx.theme.font.uiBold, pending ? c.warning : c.text);
        y += 24.f;
        std::string what = std::string(edits::whatLabel(e->what));
        if (e->what == edits::What::Modified || e->what == edits::What::Merged) {
            std::string sorts;
            for (const auto s : edits::sortsOf(*e)) sorts += (sorts.empty() ? "" : ", ") + std::string(edits::sortLabel(s));
            if (!sorts.empty()) what += kDot + sorts;
            if (edits::renamed(*e)) what += kDot + "renomm\xC3\xA9";
        }
        if (e->what == edits::What::Merged && e->since)
            what += kDot + plural(e->count, "enregistrement", "enregistrements") + " du " + edits::dateText(e->since) + " au " + edits::dateText(e->t);
        ctx.r.drawText({left, y}, fit(what, kSmall, detail_.w - 28.f), kSmall, c.textMuted);
        y += 20.f;
        ctx.r.drawText({left, y}, fit(e->name + " (" + d_.kindName(*e) + ")" + (e->note.empty() ? std::string{} : kDot + e->note), kSmall, detail_.w - 28.f),
                       kSmall, c.text);
        y += 28.f;
        const hmi::Object* now = d_.current(e->object);
        if (e->what == edits::What::Created) {
            ctx.r.drawText({left, y}, now ? "Pos\xC3\xA9 dans la vue \xC3\xA0 cet enregistrement." : "Pos\xC3\xA9 \xC3\xA0 cet enregistrement ; il n'est plus dans la vue.",
                           kText, c.text);
            y += 26.f;
        } else if (e->what == edits::What::Deleted) {
            ctx.r.drawText({left, y}, "Supprim\xC3\xA9 \xC3\xA0 cet enregistrement. Son dernier \xC3\xA9tat est gard\xC3\xA9 : il se restaure.", kText, c.text);
            y += 30.f;
            if (!now && !e->snapshot.empty()) {
                const std::string label = "Restaurer l'objet";
                restoreRect_ = {left, y, ui::measureWidth(label, kSmall) + 28.f, 28.f};
                ctx.r.fillRect(restoreRect_, c.inputBg);
                ctx.r.strokeRect(restoreRect_, c.accent, 1.f);
                ctx.r.drawText({restoreRect_.x + 14.f, restoreRect_.y + (28.f - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall, c.accent);
                y += 36.f;
            } else if (now) {
                ctx.r.drawText({left, y}, "Un objet de ce nom est de nouveau dans la vue.", kSmall, c.textMuted);
                y += 22.f;
            }
        }
        // le tableau : Propriete | Avant | Apres | Revenir
        if (!e->changes.empty()) {
            const float btnW = 132.f;
            const float colW = (detail_.w - 28.f - btnW) / 3.f;
            const float x1 = left, x2 = left + colW, x3 = left + 2.f * colW, x4 = left + 3.f * colW + 8.f;
            ctx.r.fillRect({detail_.x + 1.f, y, detail_.w - 2.f, 26.f}, c.headerBg);
            ctx.r.drawText({x1, y + 5.f}, "Propri\xC3\xA9t\xC3\xA9", kSmall, c.textMuted);
            ctx.r.drawText({x2, y + 5.f}, "Avant", kSmall, c.textMuted);
            ctx.r.drawText({x3, y + 5.f}, "Apr\xC3\xA8s", kSmall, c.textMuted);
            y += 28.f;
            const bool canRevert = now && e->what != edits::What::Deleted && e->what != edits::What::Created;
            for (std::size_t i = 0; i < e->changes.size(); ++i) {
                const auto& ch = e->changes[i];
                if (i % 2) ctx.r.fillRect({detail_.x + 1.f, y, detail_.w - 2.f, kLineH}, c.rowAltBg);
                const float ty = y + (kLineH - ctx.r.lineHeight(kSmall)) * 0.5f;
                ctx.r.drawText({x1, ty}, fit(d_.changeLabel(*e, ch), kSmall, colW - 10.f), kSmall, c.text);
                const std::string before = d_.valueText(*e, ch, true), after = d_.valueText(*e, ch, false);
                const auto value = [&](float x, const std::string& v, bool absent) {
                    if (absent) ctx.r.drawText({x, ty}, "(absente)", kSmall, c.textDisabled);
                    else if (v.empty()) ctx.r.drawText({x, ty}, "(vide)", kSmall, c.textDisabled);
                    else ctx.r.drawText({x, ty}, fit(v, kSmall, colW - 10.f), kSmall, c.text);
                };
                value(x2, before, ch.beforeAbsent);
                value(x3, after, ch.afterAbsent);
                if (canRevert) {
                    const gfx::Rect b{x4, y + 3.f, btnW - 12.f, kLineH - 6.f};
                    ctx.r.strokeRect(b, c.border, 1.f);
                    ctx.r.drawText({b.x + 10.f, b.y + (b.h - ctx.r.lineHeight(kSmall)) * 0.5f}, "Revenir \xC3\xA0 Avant", kSmall, c.accent);
                    revertRects_.push_back({b, i});
                }
                y += kLineH;
            }
        }
        detailHeight_ = y + detailScroll_ - detail_.y + 12.f;
        ctx.r.popClip();
    }

    HmiEditHistoryDialog& d_;
    std::vector<Chip> chips_;
    std::vector<Row> rows_;
    gfx::Rect list_{}, detail_{}, confirm_{}, restoreRect_{};
    std::vector<std::pair<gfx::Rect, std::size_t>> revertRects_;
    float listScroll_{0}, detailScroll_{0}, detailHeight_{0};
    float pcLabelX_{0}, pcLabelY_{0}, messageX_{0}, messageW_{0};
};

// ============================================================================ la fenetre ===
HmiEditHistoryDialog::HmiEditHistoryDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) { reload(); }

HmiEditHistoryDialog::~HmiEditHistoryDialog() = default;

menu::MenuTraits HmiEditHistoryDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiEditHistoryDialog::title() const {
    const hmi::View* v = spec_.doc ? spec_.doc->project.view(spec_.view) : nullptr;
    if (spec_.objects.empty()) return "Historique de " + (v ? v->name : std::string("la vue"));
    if (spec_.objects.size() == 1) {
        const hmi::Object* o = current(spec_.objects.front());
        if (o) return "Historique \xE2\x80\x94 " + o->name + " (" + std::string(hmi::kindLabel(o->kind)) + ")";
        return "Historique \xE2\x80\x94 " + objectName(spec_.objects.front());
    }
    return "Historique des " + std::to_string(spec_.objects.size()) + " objets";
}

const hmi::Object* HmiEditHistoryDialog::current(hmi::Id id) const {
    const hmi::View* v = spec_.doc ? spec_.doc->project.view(spec_.view) : nullptr;
    return v ? v->object(id) : nullptr;
}

std::string HmiEditHistoryDialog::objectName(hmi::Id id) const {
    if (const auto* o = current(id)) return o->name;
    for (const auto& e : all_) if (e.object == id) return e.name;
    return "objet " + std::to_string(id);
}

std::string HmiEditHistoryDialog::kindName(const edits::Entry& e) const {
    if (const auto k = hmi::kindFromKey(e.kind)) return std::string(hmi::kindLabel(*k));
    return e.kind;
}

bool HmiEditHistoryDialog::wanted(const edits::Entry& e) const {
    return spec_.objects.empty() || std::find(spec_.objects.begin(), spec_.objects.end(), e.object) != spec_.objects.end();
}

void HmiEditHistoryDialog::reload() {
    all_.clear();
    pendingCount_ = 0;
    if (!spec_.doc) return refresh();
    const std::string& folder = spec_.doc->savedFolder;
    // en tete : ce qui n'est pas encore enregistre (la vue comparee a son fichier)
    if (const hmi::View* v = spec_.doc->project.view(spec_.view)) {
        const auto saved = folder.empty() ? std::nullopt : edits::savedView(folder, spec_.view);
        auto pending = edits::compare(saved ? &*saved : nullptr, *v, edits::currentSignature(), spec_.doc->editNotes);
        for (auto& e : pending)
            if (wanted(e)) {
                all_.push_back(std::move(e));
                ++pendingCount_;
            }
    }
    const auto log = edits::load(folder, spec_.view);
    for (auto it = log.entries.rbegin(); it != log.entries.rend(); ++it)
        if (wanted(*it)) all_.push_back(*it);
    refresh();
}

bool HmiEditHistoryDialog::isPending(std::size_t i) const noexcept { return i < shown_.size() && shown_[i] < pendingCount_; }

const edits::Entry* HmiEditHistoryDialog::entry(std::size_t i) const noexcept { return i < shown_.size() ? &all_[shown_[i]] : nullptr; }

bool HmiEditHistoryDialog::matches(const edits::Entry& e) const {
    if (sort_) {
        bool any = false;
        for (const auto& c : e.changes) any = any || c.sort == *sort_;
        // une creation, une suppression : l'objet lui-meme
        if (!any && !(*sort_ == edits::Sort::Object && (e.what == edits::What::Created || e.what == edits::What::Deleted))) return false;
    }
    if (!pc_.empty() && e.pc.find(pc_) == std::string::npos) return false;
    if (!search_.empty()) {
        const std::string q = lowered(search_);
        std::string hay = e.name + " " + e.pc + " " + e.account + " " + e.note;
        for (const auto& c : e.changes) hay += " " + c.key + " " + changeLabel(e, c) + " " + valueText(e, c, true) + " " + valueText(e, c, false);
        if (lowered(hay).find(q) == std::string::npos) return false;
    }
    return true;
}

void HmiEditHistoryDialog::refresh() {
    const edits::Entry* keep = selected_ >= 0 && static_cast<std::size_t>(selected_) < shown_.size() ? &all_[shown_[static_cast<std::size_t>(selected_)]] : nullptr;
    const edits::Entry kept = keep ? *keep : edits::Entry{};
    shown_.clear();
    for (std::size_t i = 0; i < all_.size(); ++i)
        if (matches(all_[i])) shown_.push_back(i);
    selected_ = shown_.empty() ? -1 : 0;
    if (keep)
        for (std::size_t i = 0; i < shown_.size(); ++i)
            if (all_[shown_[i]] == kept) { selected_ = static_cast<int>(i); break; }
    if (pcBox_) {
        std::vector<ui::DropDown::Item> items{{"Tous les PC", "", {}, true}};
        int sel = 0;
        for (const auto& pc : pcs()) {
            if (pc == pc_) sel = static_cast<int>(items.size());
            items.push_back({pc, pc, {}, true});
        }
        pcBox_->setItems(std::move(items));
        pcBox_->setSelectedIndex(sel);
    }
    if (revert_) {
        const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
        revert_->setEnabled(e && current(e->object) && (e->what == edits::What::Modified || e->what == edits::What::Merged));
    }
    if (body_) {
        body_->rebuild();
        body_->invalidateLayout();
    }
}

void HmiEditHistoryDialog::select(std::size_t i) {
    if (i >= shown_.size()) return;
    selected_ = static_cast<int>(i);
    confirm_ = {};
    refresh();
    if (body_) body_->scrollListTo(selected_);
}

void HmiEditHistoryDialog::setSortFilter(std::optional<edits::Sort> s) {
    sort_ = s;
    refresh();
}

void HmiEditHistoryDialog::setPcFilter(const std::string& pc) {
    pc_ = pc;
    refresh();
}

void HmiEditHistoryDialog::setSearch(const std::string& text) {
    search_ = text;
    if (searchField_ && searchField_->text() != text) searchField_->setText(text);
    refresh();
}

std::vector<std::string> HmiEditHistoryDialog::pcs() const {
    std::set<std::string> seen;
    for (std::size_t i = pendingCount_; i < all_.size(); ++i) {
        const std::string& s = all_[i].pc;   // une entree regroupee : « PC-A, PC-B »
        std::size_t from = 0;
        while (from <= s.size()) {
            const auto comma = s.find(", ", from);
            const std::string part = s.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
            if (!part.empty()) seen.insert(part);
            if (comma == std::string::npos) break;
            from = comma + 2;
        }
    }
    return {seen.begin(), seen.end()};
}

std::string HmiEditHistoryDialog::changeLabel(const edits::Entry& e, const edits::Change& c) const {
    if (c.key == "nom") return "Nom";
    if (c.key == "calque") return "Calque";
    if (c.key == "groupe") return "Groupe";
    if (c.key == "verrou") return "Verrouill\xC3\xA9";
    if (c.key == "cache") return "Cach\xC3\xA9 dans l'\xC3\xA9" "diteur";
    if (c.key.rfind("action ", 0) == 0) return "Action " + c.key.substr(7);
    if (c.key.rfind("alarme ", 0) == 0) return "Alarme " + c.key.substr(7);
    if (c.key.rfind("fonction ", 0) == 0) return "Fonction " + c.key.substr(9);
    if (spec_.label)
        if (const auto k = hmi::kindFromKey(e.kind))
            if (std::string l = spec_.label(*k, c.key); !l.empty()) return l;
    return c.key;
}

std::string HmiEditHistoryDialog::valueText(const edits::Entry&, const edits::Change& c, bool before) const {
    const std::string& v = before ? c.before : c.after;
    if (c.key == "calque") {
        const hmi::View* view = spec_.doc ? spec_.doc->project.view(spec_.view) : nullptr;
        if (view)
            for (const auto& l : view->layers)
                if (std::to_string(l.id) == v) return l.name;
        return v == "0" ? std::string("aucun") : "calque " + v;
    }
    if (c.key == "groupe") {
        if (v == "0") return "la vue";
        char* end = nullptr;
        const unsigned long id = std::strtoul(v.c_str(), &end, 10);
        return end && !*end ? objectName(static_cast<hmi::Id>(id)) : v;
    }
    return edits::shown(c, before);
}

std::string HmiEditHistoryDialog::rowText(std::size_t i) const {
    const auto* e = entry(i);
    if (!e) return {};
    std::string s = isPending(i) ? std::string("Non enregistr\xC3\xA9") : edits::dateText(e->t);
    s += kDot + e->pc;
    if (!e->account.empty()) s += kDot + e->account;
    if (spec_.objects.size() != 1) s += kDot + e->name;
    s += kDot + std::string(edits::whatLabel(e->what));
    if (!e->changes.empty()) s += kDot + plural(static_cast<int>(e->changes.size()), "changement", "changements");
    return s;
}

std::string HmiEditHistoryDialog::selectedText() const {
    if (selected_ < 0) return {};
    const auto* e = entry(static_cast<std::size_t>(selected_));
    std::string s = rowText(static_cast<std::size_t>(selected_)) + "\n";
    s += e->name + " (" + kindName(*e) + ")" + (e->note.empty() ? std::string{} : kDot + e->note) + "\n";
    for (const auto& c : e->changes) {
        const std::string b = c.beforeAbsent ? std::string("(absente)") : valueText(*e, c, true);
        const std::string a = c.afterAbsent ? std::string("(absente)") : valueText(*e, c, false);
        s += "  " + changeLabel(*e, c) + " : " + (b.empty() ? std::string("(vide)") : b) + " \xE2\x86\x92 " + (a.empty() ? std::string("(vide)") : a) + "\n";
    }
    return s;
}

std::string HmiEditHistoryDialog::csv() const {
    std::string s = "Date;PC;Compte;Version;Objet;Type;Sorte;Propri\xC3\xA9t\xC3\xA9;Avant;Apr\xC3\xA8s;Note\n";
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        const auto& e = all_[shown_[i]];
        const std::string head = csvField(isPending(i) ? std::string("non enregistr\xC3\xA9") : edits::dateText(e.t)) + ";" + csvField(e.pc) + ";"
                               + csvField(e.account) + ";" + csvField(e.version) + ";" + csvField(e.name) + ";" + csvField(kindName(e)) + ";";
        if (e.changes.empty()) {
            s += head + csvField(std::string(edits::whatLabel(e.what))) + ";;;;" + csvField(e.note) + "\n";
            continue;
        }
        for (const auto& c : e.changes)
            s += head + csvField(std::string(edits::sortLabel(c.sort))) + ";" + csvField(changeLabel(e, c)) + ";" + csvField(valueText(e, c, true)) + ";"
               + csvField(valueText(e, c, false)) + ";" + csvField(e.note) + "\n";
    }
    return s;
}

bool HmiEditHistoryDialog::exportCsv(const std::string& path, std::string* why) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (why) *why = "\xC3\x89" "criture impossible : " + path;
        return false;
    }
    out << "\xEF\xBB\xBF" << csv();   // le BOM : Excel lit les accents
    return static_cast<bool>(out);
}

void HmiEditHistoryDialog::copy() {
    const std::string t = selectedText();
    if (t.empty()) return say("Rien de choisi \xC3\xA0 copier.", true);
    ui::setClipboardText(t);
    say("Copi\xC3\xA9 dans le presse-papiers.");
}

void HmiEditHistoryDialog::say(std::string text, bool bad) {
    message_ = std::move(text);
    messageBad_ = bad;
    if (body_) body_->invalidate();
}

// ================================================================================ revenir ===
bool HmiEditHistoryDialog::revertChange(std::size_t change, bool now) {
    const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
    if (!e || change >= e->changes.size()) return false;
    if (!current(e->object)) {
        say(e->name + " n'est plus dans la vue.", true);
        return false;
    }
    if (now) return doRevertChange(change);
    const auto& c = e->changes[change];
    const std::string b = c.beforeAbsent ? std::string("(absente)") : valueText(*e, c, true);
    confirm_ = {Confirm::Change, change,
                "Remettre " + changeLabel(*e, c) + " de " + e->name + " \xC3\xA0 \xC2\xAB " + (b.empty() ? std::string("(vide)") : b)
                    + " \xC2\xBB ? Ctrl+Z l'annule."};
    if (body_) body_->invalidateLayout();
    refresh();
    return true;
}

bool HmiEditHistoryDialog::revertEntry(bool now) {
    const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
    if (!e || !current(e->object) || !(e->what == edits::What::Modified || e->what == edits::What::Merged)) {
        say("Cet enregistrement ne se remet pas (une cr\xC3\xA9" "ation, une suppression, ou l'objet n'est plus l\xC3\xA0).", true);
        return false;
    }
    if (now) return doRevertEntry();
    const bool pending = isPending(static_cast<std::size_t>(selected_));
    confirm_ = {Confirm::Entry, 0,
                pending ? "Annuler les changements non enregistr\xC3\xA9s de " + e->name + " (il reprend son \xC3\xA9tat enregistr\xC3\xA9) ? Ctrl+Z l'annule."
                        : "Remettre " + e->name + " tel qu'il \xC3\xA9tait apr\xC3\xA8s l'enregistrement du " + edits::dateText(e->t) + " ? Ctrl+Z l'annule."};
    if (body_) body_->invalidateLayout();
    refresh();
    return true;
}

bool HmiEditHistoryDialog::restoreDeleted(bool now) {
    const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
    if (!e || e->what != edits::What::Deleted || e->snapshot.empty()) return false;
    if (current(e->object)) {
        say("Un objet de m\xC3\xAAme identit\xC3\xA9 est d\xC3\xA9j\xC3\xA0 dans la vue.", true);
        return false;
    }
    if (now) return doRestore();
    confirm_ = {Confirm::Restore, 0, "Remettre " + e->name + " dans la vue, tel qu'il \xC3\xA9tait avant sa suppression ? Ctrl+Z l'annule."};
    if (body_) body_->invalidateLayout();
    refresh();
    return true;
}

bool HmiEditHistoryDialog::confirmYes() {
    const Pending p = confirm_;
    confirm_ = {};
    bool ok = false;
    switch (p.kind) {
        case Confirm::Change:  ok = doRevertChange(p.change); break;
        case Confirm::Entry:   ok = doRevertEntry(); break;
        case Confirm::Restore: ok = doRestore(); break;
        case Confirm::None:    break;
    }
    if (body_) body_->invalidateLayout();
    return ok;
}

void HmiEditHistoryDialog::confirmNo() {
    confirm_ = {};
    if (body_) body_->invalidateLayout();
    refresh();
}

bool HmiEditHistoryDialog::doRevertChange(std::size_t change) {
    const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
    if (!e || change >= e->changes.size() || !spec_.edit) return false;
    const edits::Change c = e->changes[change];
    const hmi::Id id = e->object;
    const std::string when = isPending(static_cast<std::size_t>(selected_)) ? std::string("non enregistr\xC3\xA9") : edits::dateText(e->t);
    const std::string label = changeLabel(*e, c);
    const bool ok = spec_.edit("Revenir \xC3\xA0 Avant : " + label + " de " + e->name, [&](hmi::View& v) {
        hmi::Object* o = v.object(id);
        return o && edits::apply(*o, c, true);
    });
    if (!ok) {
        say("Rien n'a chang\xC3\xA9.", true);
        return false;
    }
    if (spec_.doc) spec_.doc->editNotes[id] = "retour \xC3\xA0 la valeur d'avant le " + when + " (" + label + ")";
    reload();
    say(label + " remis \xC3\xA0 sa valeur d'avant. Ctrl+Z l'annule.");
    return true;
}

bool HmiEditHistoryDialog::doRevertEntry() {
    const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
    if (!e || !spec_.edit || !spec_.doc) return false;
    const hmi::Id id = e->object;
    const bool pending = isPending(static_cast<std::size_t>(selected_));
    // L'objet a son dernier enregistrement (son fichier), puis, du plus recent a cet enregistrement
    // exclu, chaque changement remis a sa valeur d'avant : l'objet tel qu'apres cet enregistrement.
    const auto saved = spec_.doc->savedFolder.empty() ? std::nullopt : edits::savedView(spec_.doc->savedFolder, spec_.view);
    const hmi::Object* base = saved ? saved->object(id) : nullptr;
    if (!base) {
        say(e->name + " n'a pas d'\xC3\xA9tat enregistr\xC3\xA9 : rien \xC3\xA0 remettre.", true);
        return false;
    }
    hmi::Object target = *base;
    if (!pending) {
        const auto log = edits::load(spec_.doc->savedFolder, spec_.view);
        bool reached = false;
        for (auto it = log.entries.rbegin(); it != log.entries.rend(); ++it) {
            if (it->object != id) continue;
            if (*it == *e) { reached = true; break; }
            if (it->what == edits::What::Created || it->what == edits::What::Deleted) break;   // une autre vie de l'objet
            for (auto c = it->changes.rbegin(); c != it->changes.rend(); ++c) (void)edits::apply(target, *c, true);
        }
        if (!reached) {
            say("L'objet a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9 puis restaur\xC3\xA9 depuis : cette version ne se remet pas.", true);
            return false;
        }
    }
    const std::string when = pending ? std::string() : edits::dateText(e->t);
    const bool ok = spec_.edit((pending ? std::string("Annuler les changements de ") : "Revenir \xC3\xA0 la version du " + when + " : ") + e->name,
                               [&](hmi::View& v) {
                                   hmi::Object* o = v.object(id);
                                   if (!o) return false;
                                   if (*o == target) return false;
                                   *o = target;
                                   return true;
                               });
    if (!ok) {
        say("L'objet est d\xC3\xA9j\xC3\xA0 dans cet \xC3\xA9tat.", true);
        return false;
    }
    if (!pending) spec_.doc->editNotes[id] = "retour \xC3\xA0 la version du " + when;
    else spec_.doc->editNotes.erase(id);
    reload();
    say(pending ? std::string("Changements non enregistr\xC3\xA9s annul\xC3\xA9s. Ctrl+Z les remet.")
                : "Revenu \xC3\xA0 la version du " + when + ". Ctrl+Z l'annule.");
    return true;
}

bool HmiEditHistoryDialog::doRestore() {
    const auto* e = selected_ >= 0 ? entry(static_cast<std::size_t>(selected_)) : nullptr;
    if (!e || !spec_.edit || !spec_.doc) return false;
    auto obj = edits::fromSnapshot(e->snapshot);
    if (!obj) {
        say("Son \xC3\xA9tat gard\xC3\xA9 ne se relit pas.", true);
        return false;
    }
    const std::string when = edits::dateText(e->t);
    const hmi::Id id = obj->id;
    const bool ok = spec_.edit("Restaurer " + obj->name, [&](hmi::View& v) {
        if (v.object(id)) return false;
        hmi::Object o = *obj;
        if (!v.layer(o.layer) && !v.layers.empty()) o.layer = v.activeLayer != hmi::kNoId ? v.activeLayer : v.layers.front().id;
        if (o.parent != hmi::kNoId && !v.object(o.parent)) o.parent = hmi::kNoId;
        for (const auto& other : v.objects)
            if (other.name == o.name) { o.name = hmi::uniqueObjectName(v, o.name); break; }
        v.objects.push_back(std::move(o));
        return true;
    });
    if (!ok) {
        say("L'objet n'a pas pu revenir.", true);
        return false;
    }
    spec_.doc->editNotes[id] = "restaur\xC3\xA9 (supprim\xC3\xA9 le " + when + ")";
    reload();
    say(obj->name + " est de retour dans la vue. Ctrl+Z l'annule.");
    return true;
}

// ================================================================================== l'ecran ===
core::Status HmiEditHistoryDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    {
        auto f = std::make_unique<ui::InputText>(base + ".search");
        f->setPlaceholder("Rechercher (propri\xC3\xA9t\xC3\xA9, valeur, PC\xE2\x80\xA6)");
        searchField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += searchField_->textChanged->connect([this](const std::string& t) {
            search_ = t;
            refresh();
        });
    }
    {
        auto dd = std::make_unique<ui::DropDown>(base + ".pc");
        pcBox_ = &static_cast<ui::DropDown&>(body->addChild(std::move(dd)));
        links_ += pcBox_->selectionChanged->connect([this](int) {
            const auto* it = pcBox_->selectedItem();
            const std::string pc = it ? it->value : std::string{};
            if (pc != pc_) {
                pc_ = pc;
                refresh();
            }
        });
    }
    copy_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Copier", base + ".copy")));
    links_ += copy_->clicked->connect([this] { copy(); });
    export_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Exporter en CSV\xE2\x80\xA6", base + ".csv")));
    links_ += export_->clicked->connect([this] {
        const std::weak_ptr<int> alive = alive_;
        const hmi::View* v = spec_.doc ? spec_.doc->project.view(spec_.view) : nullptr;
        const std::string name = "historique_" + (v ? v->name : std::string("vue")) + ".csv";
        if (!ui::browsePath(ui::saveFile("CSV|*.csv", ui::pathIn(hmiProjectFolder(), "exports"), "Exporter l'historique"), name,
                            [this, alive](std::string path) {
                                if (alive.expired()) return;
                                std::string why;
                                if (exportCsv(path, &why)) say("Export\xC3\xA9 : " + path);
                                else say(why, true);
                            }))
            say("Pas d'explorateur de fichiers ici.", true);
    });
    revert_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Revenir \xC3\xA0 cette version", base + ".revert")));
    links_ += revert_->clicked->connect([this] { (void)revertEntry(); });
    close_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Fermer", base + ".close")));
    close_->setStyle(ui::Button::Style::Primary);
    links_ += close_->clicked->connect([this] { finish(); });
    yes_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Oui, revenir", base + ".yes")));
    yes_->setStyle(ui::Button::Style::Primary);
    links_ += yes_->clicked->connect([this] { (void)confirmYes(); });
    no_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Non", base + ".no")));
    links_ += no_->clicked->connect([this] { confirmNo(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

void HmiEditHistoryDialog::finish() {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, std::string{}});
}

ui::EventResult HmiEditHistoryDialog::HandleEvent(const ui::InputEvent& ev) {
    // la confirmation : ses deux boutons seulement
    if (yes_ && no_) {
        const auto vis = confirming() ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        if (yes_->visible() != confirming()) {
            yes_->setVisibility(vis);
            no_->setVisibility(vis);
        }
    }
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            if (confirming()) confirmNo();
            else finish();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Down || k->key == ui::Key::Up) {
            if (!shown_.empty()) {
                const int n = static_cast<int>(shown_.size());
                select(static_cast<std::size_t>(std::clamp(selected_ + (k->key == ui::Key::Down ? 1 : -1), 0, n - 1)));
            }
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && confirming()) {
            (void)confirmYes();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::C && k->mods.ctrl && !(searchField_ && searchField_->focused())) {
            copy();
            return ui::EventResult::Consumed;
        }
    }
    return r;
}

} // namespace app
