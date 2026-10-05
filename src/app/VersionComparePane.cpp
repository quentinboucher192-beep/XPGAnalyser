// =============================================================================
//  app/VersionComparePane.cpp - comparer deux versions (lot 21)
// =============================================================================
#include "VersionComparePane.hpp"

#include "hmi/HmiIcons.hpp"
#include "hmi/HmiPaneKit.hpp"
#include "hmi/HmiPainter.hpp"
#include "hmi/HmiPanels.hpp"

#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace app {

namespace ver = hmi::ver;

namespace {

const gfx::FontId kSmall{13};

std::string sideLabel(ver::Side s) {
    return s == ver::Side::Api ? "API" : s == ver::Side::Ihm ? "IHM" : "Donn\xC3\xA9" "es";
}

std::string mark(ver::Change c) {
    return c == ver::Change::Added ? "+ " : c == ver::Change::Removed ? "\xE2\x88\x92 " : "\xE2\x9C\x8E ";
}

gfx::Color tint(gfx::Color c, std::uint8_t alpha) { return {c.r, c.g, c.b, alpha}; }

// L'identifiant apres "prefixe:" dans une cle ("vue:12" -> 12).
hmi::Id idOf(const std::string& key) {
    const auto colon = key.find(':');
    if (colon == std::string::npos) return hmi::kNoId;
    try { return static_cast<hmi::Id>(std::stoul(key.substr(colon + 1))); } catch (...) { return hmi::kNoId; }
}

}   // namespace

// ---- le corps : le texte cote a cote, la vue, ou le reste en clair -------------------
class CompareBody final : public ui::Widget {
public:
    explicit CompareBody(VersionComparePane& pane) : ui::Widget(pane.id() + ".body"), pane_(pane) {}

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        const auto* e = pane_.current();
        if (!e) {
            const std::string none = pane_.comp_.elements.empty() ? "Aucune diff\xC3\xA9rence : les deux versions sont identiques."
                                                                  : "Choisis une diff\xC3\xA9rence \xC3\xA0 gauche.";
            ctx.r.drawText({b.x + 20.f, b.y + 20.f}, none, ctx.theme.font.ui, c.textMuted);
            return;
        }
        ctx.r.pushClip(b);
        if (!pane_.diff_.empty() || !pane_.left_.empty() || !pane_.right_.empty()) paintText(ctx, *e);
        else if (e->kind == "vue") paintView(ctx, *e);
        else paintDetails(ctx, *e);
        ctx.r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
            const float rows = static_cast<float>(pane_.diff_.size() + 2);
            pane_.scroll_ = std::clamp(pane_.scroll_ - w->dy * 3.f, 0.f, std::max(0.f, rows - 5.f));
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void header(const ui::PaintContext& ctx, gfx::Rect r, const std::string& text) {
        ctx.r.fillRect(r, ctx.theme.color.headerBg);
        std::string t = text;
        while (t.size() > 4 && ctx.r.measure(t, ctx.theme.font.ui).width > r.w - 16.f) t = t.substr(0, t.size() - 4) + "\xE2\x80\xA6";
        ctx.r.drawText({r.x + 8.f, r.y + 5.f}, t, ctx.theme.font.ui, ctx.theme.color.text);
    }

    // Une ligne de code, sa partie changee surlignee (le debut et la fin communs
    // avec la ligne d'en face ne le sont pas).
    void codeLine(const ui::PaintContext& ctx, float x, float y, float w, int number, const std::string& text,
                  const std::string& other, gfx::Color bg, gfx::Color strong, bool changed) {
        const auto& c = ctx.theme.color;
        const float lineH = 20.f;
        if (bg.a) ctx.r.fillRect({x, y, w, lineH}, bg);
        const std::string num = number >= 0 ? std::to_string(number + 1) : std::string{};
        ctx.r.drawText({x + 34.f - ctx.r.measure(num, kSmall).width, y + 3.f}, num, kSmall, c.textMuted);
        const float tx = x + 44.f;
        if (changed && !text.empty()) {
            std::size_t pre = 0;
            while (pre < text.size() && pre < other.size() && text[pre] == other[pre]) ++pre;
            std::size_t suf = 0;
            while (suf < text.size() - pre && suf < other.size() - pre && text[text.size() - 1 - suf] == other[other.size() - 1 - suf]) ++suf;
            // pas au milieu d'un caractere UTF-8
            while (pre > 0 && (static_cast<unsigned char>(text[pre]) & 0xC0) == 0x80) --pre;
            const float x0 = tx + ctx.r.measure(text.substr(0, pre), ctx.theme.font.mono).width;
            const float x1 = tx + ctx.r.measure(text.substr(0, text.size() - suf), ctx.theme.font.mono).width;
            if (x1 > x0) ctx.r.fillRect({x0, y + 1.f, x1 - x0, lineH - 2.f}, strong);
        }
        ctx.r.drawText({tx, y + 2.f}, text, ctx.theme.font.mono, c.text);
    }

    void paintText(const ui::PaintContext& ctx, const ver::Element& e) {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const float lineH = 20.f;
        const std::string what = e.fileA.empty() ? (e.fileB.empty() ? e.name : e.fileB) : e.fileA;
        const auto& diff = pane_.diff_;
        const auto& L = pane_.left_;
        const auto& R = pane_.right_;
        const std::size_t first = static_cast<std::size_t>(std::max(0.f, pane_.scroll_));
        if (!pane_.unified_) {
            const float colW = b.w / 2.f;
            header(ctx, {b.x, b.y, colW - 1.f, 28.f}, pane_.versionLabel(pane_.a_) + " \xE2\x80\x94 " + what);
            header(ctx, {b.x + colW, b.y, colW, 28.f}, pane_.versionLabel(pane_.b_) + " \xE2\x80\x94 " + what);
            float y = b.y + 30.f;
            for (std::size_t i = first; i < diff.size() && y < b.bottom(); ++i, y += lineH) {
                const auto& d = diff[i];
                const std::string lt = d.left >= 0 && static_cast<std::size_t>(d.left) < L.size() ? L[static_cast<std::size_t>(d.left)] : std::string{};
                const std::string rt = d.right >= 0 && static_cast<std::size_t>(d.right) < R.size() ? R[static_cast<std::size_t>(d.right)] : std::string{};
                const bool ch = d.kind == ver::DiffLine::Changed;
                gfx::Color lb{0, 0, 0, 0}, rb{0, 0, 0, 0};
                if (d.kind == ver::DiffLine::Removed || ch) lb = tint(c.error, 46);
                if (d.kind == ver::DiffLine::Added || ch) rb = tint(c.ok, 46);
                if (d.left < 0) lb = tint(c.textMuted, 14);
                if (d.right < 0) rb = tint(c.textMuted, 14);
                codeLine(ctx, b.x, y, colW - 1.f, d.left, lt, rt, lb, tint(c.error, 110), ch);
                codeLine(ctx, b.x + colW, y, colW, d.right, rt, lt, rb, tint(c.ok, 110), ch);
            }
            ctx.r.line({b.x + colW, b.y}, {b.x + colW, b.bottom()}, c.border, 1.f);
        } else {
            header(ctx, {b.x, b.y, b.w, 28.f}, pane_.versionLabel(pane_.a_) + " \xE2\x86\x92 " + pane_.versionLabel(pane_.b_) + " \xE2\x80\x94 " + what);
            // Les lignes a plat : une modifiee donne deux lignes, - puis +.
            struct Row { int number; std::string text, other; char sign; };
            std::vector<Row> rows;
            for (const auto& d : diff) {
                const std::string lt = d.left >= 0 && static_cast<std::size_t>(d.left) < L.size() ? L[static_cast<std::size_t>(d.left)] : std::string{};
                const std::string rt = d.right >= 0 && static_cast<std::size_t>(d.right) < R.size() ? R[static_cast<std::size_t>(d.right)] : std::string{};
                if (d.kind == ver::DiffLine::Same) rows.push_back({d.right, rt, rt, ' '});
                if (d.kind == ver::DiffLine::Removed || d.kind == ver::DiffLine::Changed) rows.push_back({d.left, lt, rt, '-'});
                if (d.kind == ver::DiffLine::Added || d.kind == ver::DiffLine::Changed) rows.push_back({d.right, rt, lt, '+'});
            }
            float y = b.y + 30.f;
            for (std::size_t i = first; i < rows.size() && y < b.bottom(); ++i, y += lineH) {
                const auto& r = rows[i];
                const gfx::Color bg = r.sign == '-' ? tint(c.error, 46) : r.sign == '+' ? tint(c.ok, 46) : gfx::Color{0, 0, 0, 0};
                codeLine(ctx, b.x, y, b.w, r.number, std::string(1, r.sign) + " " + r.text, std::string(1, r.sign) + " " + r.other, bg,
                         tint(r.sign == '-' ? c.error : c.ok, 110), r.sign != ' ' && r.text != r.other);
            }
        }
    }

    void paintView(const ui::PaintContext& ctx, const ver::Element& e) {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const hmi::Id id = idOf(e.key);
        const hmi::View* va = pane_.pa_ ? pane_.pa_->view(id) : nullptr;
        const hmi::View* vb = pane_.pb_ ? pane_.pb_->view(id) : nullptr;
        const float colW = b.w / 2.f;
        const float thumbH = std::max(120.f, b.h * 0.58f);
        header(ctx, {b.x, b.y, colW - 1.f, 28.f}, pane_.versionLabel(pane_.a_) + (va ? " \xE2\x80\x94 " + va->name : std::string(" \xE2\x80\x94 (absente)")));
        header(ctx, {b.x + colW, b.y, colW, 28.f}, pane_.versionLabel(pane_.b_) + (vb ? " \xE2\x80\x94 " + vb->name : std::string(" \xE2\x80\x94 (absente)")));
        const auto side = [&](const hmi::Project* p, const hmi::View* v, gfx::Rect area, bool left) {
            if (!p || !v) {
                ctx.r.drawText({area.x + 12.f, area.y + 12.f}, "La vue n'existe pas dans cette version.", ctx.theme.font.ui, c.textMuted);
                return;
            }
            paintHmiViewPreview(ctx.r, *p, *v, area, ctx.theme);
            if (v->width <= 0 || v->height <= 0) return;
            const float z = std::min(area.w / static_cast<float>(v->width), area.h / static_cast<float>(v->height));
            const float ox = area.x + (area.w - static_cast<float>(v->width) * z) / 2.f;
            const float oy = area.y + (area.h - static_cast<float>(v->height) * z) / 2.f;
            for (const auto& ch : pane_.viewChanges_) {
                if (left && ch.change == ver::Change::Added) continue;
                if (!left && ch.change == ver::Change::Removed) continue;
                const auto* o = v->object(ch.id);
                if (!o) continue;
                const auto box = o->box();
                const gfx::Rect r{ox + static_cast<float>(box.x) * z - 3.f, oy + static_cast<float>(box.y) * z - 3.f,
                                  static_cast<float>(box.w) * z + 6.f, static_cast<float>(box.h) * z + 6.f};
                const gfx::Color col = ch.change == ver::Change::Added ? c.ok : ch.change == ver::Change::Removed ? c.error : c.warning;
                ctx.r.strokeRect(r, col, 2.f);
            }
        };
        side(pane_.pa_.get(), va, {b.x + 10.f, b.y + 38.f, colW - 20.f, thumbH - 16.f}, true);
        side(pane_.pb_.get(), vb, {b.x + colW + 10.f, b.y + 38.f, colW - 20.f, thumbH - 16.f}, false);
        // Ce qui a change, en clair.
        float y = b.y + 30.f + thumbH;
        ctx.r.fillRect({b.x, y - 4.f, b.w, 1.f}, c.border);
        if (pane_.viewChanges_.empty()) {
            ctx.r.drawText({b.x + 12.f, y + 4.f}, "Les objets n'ont pas chang\xC3\xA9 : ses r\xC3\xA9glages (taille, calques, scripts, actions de la vue).", kSmall,
                           c.textMuted);
            return;
        }
        for (const auto& ch : pane_.viewChanges_) {
            if (y > b.bottom() - 18.f) break;
            std::string line = mark(ch.change) + ch.name;
            if (ch.change == ver::Change::Added) line += " : ajout\xC3\xA9";
            else if (ch.change == ver::Change::Removed) line += " : retir\xC3\xA9";
            else {
                line += " : ";
                for (std::size_t i = 0; i < ch.props.size(); ++i) line += (i ? " \xC2\xB7 " : "") + ch.props[i];
            }
            while (line.size() > 8 && ctx.r.measure(line, kSmall).width > b.w - 24.f) line = line.substr(0, line.size() - 4) + "\xE2\x80\xA6";
            const gfx::Color col = ch.change == ver::Change::Added ? c.ok : ch.change == ver::Change::Removed ? c.error : c.text;
            ctx.r.drawText({b.x + 12.f, y + 2.f}, line, kSmall, col);
            y += 19.f;
        }
    }

    void paintDetails(const ui::PaintContext& ctx, const ver::Element& e) {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        header(ctx, {b.x, b.y, b.w, 28.f}, e.where);
        float y = b.y + 40.f;
        ctx.r.drawText({b.x + 12.f, y}, mark(e.change) + e.detail, ctx.theme.font.ui, c.text);
        y += 30.f;
        for (const auto& line : pane_.details_) {
            if (y > b.bottom() - 18.f) break;
            const gfx::Color col = line.rfind("+", 0) == 0 ? c.ok : line.rfind("\xE2\x88\x92", 0) == 0 ? c.error : c.text;
            ctx.r.drawText({b.x + 12.f, y}, line, kSmall, col);
            y += 19.f;
        }
    }

    VersionComparePane& pane_;
};

// ---- le volet ------------------------------------------------------------------------
VersionComparePane::VersionComparePane(std::string id, std::string folder, int a, int b)
    : ui::Widget(std::move(id)), folder_(std::move(folder)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(APrev, HmiGlyph::Up, "Diff\xC3\xA9rence pr\xC3\xA9" "c\xC3\xA9" "dente", "Diff\xC3\xA9rence pr\xC3\xA9" "c\xC3\xA9" "dente");
    tools->add(ANext, HmiGlyph::Down, "Diff\xC3\xA9rence suivante", "Suivante");
    tools->separator();
    tools->add(ASide, HmiGlyph::Compare, "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te : les deux versions en face", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te");
    tools->add(AUnified, HmiGlyph::List, "Unifi\xC3\xA9 : une seule colonne, - retir\xC3\xA9" "e, + ajout\xC3\xA9" "e", "Unifi\xC3\xA9");
    tools->separator();
    tools->add(ARestore, HmiGlyph::Refresh, "Restaurer cet \xC3\xA9l\xC3\xA9ment depuis la version de gauche : lui seul revient, en une commande (Ctrl+Z l'annule)",
               "Restaurer cet \xC3\xA9l\xC3\xA9ment");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setCheckedWhen(ASide, [this] { return !unified_; });
    tools_->setCheckedWhen(AUnified, [this] { return unified_; });
    tools_->setEnabledWhen(APrev, [this] { return current_ > 0; });
    tools_->setEnabledWhen(ANext, [this] { return current_ + 1 < static_cast<int>(comp_.elements.size()); });
    tools_->setEnabledWhen(ARestore, [this] { const auto* e = current(); return e && a_ > 0 && restorable(*e); });
    boxA_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".a")));
    boxB_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".b")));
    list_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(base + ".list")));
    list_->setColumns({{"Diff\xC3\xA9rences", 360.f}});
    list_->setSelectionMode(ui::SelectionMode::Single);
    auto body = std::make_unique<CompareBody>(*this);
    body_ = &addChild(std::move(body));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int act) {
        if (act == APrev) step(-1);
        else if (act == ANext) step(+1);
        else if (act == ASide) setUnified(false);
        else if (act == AUnified) setUnified(true);
        else if (act == ARestore) {
            std::string why;
            (void)restoreCurrent(&why);
        }
    });
    links_ += list_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (filling_ || rows.empty() || rows.front() >= rowElement_.size()) return;
        const int e = rowElement_[rows.front()];
        if (e < 0) return;
        current_ = e;
        showCurrent();
    });
    const auto picked = [this] {
        if (filling_) return;
        const auto* ia = boxA_->selectedItem();
        const auto* ib = boxB_->selectedItem();
        if (!ia || !ib) return;
        compare(std::atoi(ia->value.c_str()), std::atoi(ib->value.c_str()));
        if (hosts_.retitle) hosts_.retitle(title());
    };
    links_ += boxA_->selectionChanged->connect([picked](int) { picked(); });
    links_ += boxB_->selectionChanged->connect([picked](int) { picked(); });
    compare(a, b);
}

std::string VersionComparePane::titleFor(int a, int b) {
    const auto name = [](int n) { return n == 0 ? std::string("en cours") : "V" + std::to_string(n); };
    return "Comparer " + name(a) + " \xE2\x86\x94 " + name(b);
}

std::string VersionComparePane::title() const { return titleFor(a_, b_); }

std::string VersionComparePane::versionLabel(int n) const {
    if (n == 0) return "Travail en cours";
    const auto* v = store_.find(n);
    if (!v) return "V" + std::to_string(n);
    std::string when = v->date.size() >= 16 ? v->date.substr(8, 2) + "/" + v->date.substr(5, 2) + " " + v->date.substr(11, 5) : v->date;
    return v->label() + " \xC2\xB7 " + when;
}

void VersionComparePane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void VersionComparePane::compare(int a, int b) {
    a_ = a;
    b_ = b;
    auto s = ver::open(folder_);
    store_ = s ? std::move(*s) : ver::Store{};
    store_.folder = folder_;
    comp_ = {};
    if (auto c = ver::compare(store_, a, b)) comp_ = std::move(*c);
    else say("Comparer : " + c.error().context, true);
    loaded_ = false;
    pa_.reset();
    pb_.reset();
    // Les deux listes de versions.
    std::vector<ui::DropDown::Item> items;
    items.push_back({"Travail en cours", "0", {}, true});
    for (auto it = store_.versions.rbegin(); it != store_.versions.rend(); ++it) items.push_back({it->label(), std::to_string(it->number), {}, true});
    filling_ = true;
    boxA_->setItems(items);
    boxB_->setItems(items);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].value == std::to_string(a)) boxA_->setSelectedIndex(static_cast<int>(i));
        if (items[i].value == std::to_string(b)) boxB_->setSelectedIndex(static_cast<int>(i));
    }
    filling_ = false;
    tools_->setText(ARestore, "Restaurer cet \xC3\xA9l\xC3\xA9ment depuis " + (a == 0 ? std::string("le travail en cours") : "V" + std::to_string(a))
                                  + " : lui seul revient, en une commande (Ctrl+Z l'annule)",
                    "Restaurer cet \xC3\xA9l\xC3\xA9ment depuis " + (a == 0 ? std::string("en cours") : "V" + std::to_string(a)));
    current_ = comp_.elements.empty() ? -1 : 0;
    rebuildList();
    showCurrent();
    if (message_.empty() || message_.rfind("Comparer", 0) == 0) {
        message_.clear();
        status_->setMessage(title() + " : " + std::to_string(comp_.elements.size()) + " diff\xC3\xA9rence(s) (API "
                            + std::to_string(comp_.count(ver::Side::Api)) + ", IHM " + std::to_string(comp_.count(ver::Side::Ihm))
                            + ") \xC2\xB7 les fichiers identiques ne sont pas list\xC3\xA9s");
    }
}

void VersionComparePane::rebuildList() {
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles;
    rowElement_.clear();
    for (ver::Side side : {ver::Side::Api, ver::Side::Ihm, ver::Side::Data}) {
        const std::size_t n = comp_.count(side);
        if (n == 0) continue;
        rows.push_back({sideLabel(side) + " \xC2\xB7 " + std::to_string(n)});
        ui::CellStyle head;
        head.bold = true;
        head.spanRow = true;
        styles.push_back(head);
        rowElement_.push_back(-1);
        std::string category;
        for (std::size_t i = 0; i < comp_.elements.size(); ++i) {
            const auto& e = comp_.elements[i];
            if (e.side != side) continue;
            if (e.category != category) {
                category = e.category;
                rows.push_back({"\xE2\x96\xBE " + category});
                ui::CellStyle cs;
                cs.indent = 10.f;
                cs.fgTone = ui::Tone::Muted;
                styles.push_back(cs);
                rowElement_.push_back(-1);
            }
            std::string text = mark(e.change) + e.name;
            if (e.kind == "texte" && (e.added || e.removed)) text += "   +" + std::to_string(e.added) + " \xE2\x88\x92" + std::to_string(e.removed);
            rows.push_back({text});
            ui::CellStyle cs;
            cs.indent = 24.f;
            cs.fgTone = e.change == ver::Change::Added ? ui::Tone::Ok : e.change == ver::Change::Removed ? ui::Tone::Error : ui::Tone::None;
            styles.push_back(cs);
            rowElement_.push_back(static_cast<int>(i));
        }
    }
    auto shared = std::make_shared<std::vector<ui::CellStyle>>(std::move(styles));
    filling_ = true;
    list_->setModel(std::make_shared<hmikit::Rows>(std::vector<std::string>{"Diff\xC3\xA9rences"}, std::move(rows),
                                                   [shared](ui::RowIndex r, std::size_t) {
                                                       return r < shared->size() ? (*shared)[r] : ui::CellStyle{};
                                                   }));
    for (std::size_t r = 0; r < rowElement_.size(); ++r)
        if (rowElement_[r] == current_ && current_ >= 0) list_->selectModelRows({static_cast<ui::RowIndex>(r)}, false);
    filling_ = false;
}

const ver::Element* VersionComparePane::current() const {
    return current_ >= 0 && static_cast<std::size_t>(current_) < comp_.elements.size() ? &comp_.elements[static_cast<std::size_t>(current_)] : nullptr;
}

void VersionComparePane::loadProjects() {
    if (loaded_) return;
    loaded_ = true;
    if (auto p = ver::hmiOf(store_, a_)) pa_ = std::make_shared<hmi::Project>(std::move(*p));
    if (auto p = ver::hmiOf(store_, b_)) pb_ = std::make_shared<hmi::Project>(std::move(*p));
}

void VersionComparePane::showCurrent() {
    left_.clear();
    right_.clear();
    diff_.clear();
    viewChanges_.clear();
    details_.clear();
    scroll_ = 0.f;
    const auto* e = current();
    if (e) {
        const std::string& key = e->key;
        if (e->side != ver::Side::Ihm && e->kind != "fichier") {
            // Un fichier de l'API ou des donnees : son texte de chaque cote.
            if (!e->fileA.empty()) if (auto t = ver::contentOf(store_, a_, e->fileA)) left_ = ver::splitLines(*t);
            if (!e->fileB.empty()) if (auto t = ver::contentOf(store_, b_, e->fileB)) right_ = ver::splitLines(*t);
            diff_ = ver::diffLines(left_, right_);
        } else if (e->kind == "texte") {
            loadProjects();
            const hmi::Id id = idOf(key);
            const auto bodyOf = [&](const std::shared_ptr<hmi::Project>& p) -> std::string {
                if (!p) return {};
                if (key.rfind("script:", 0) == 0) { const auto* s = p->script(id); return s ? s->body : std::string{}; }
                const auto* f = p->function(id);
                return f ? f->body : std::string{};
            };
            left_ = ver::splitLines(bodyOf(pa_));
            right_ = ver::splitLines(bodyOf(pb_));
            diff_ = ver::diffLines(left_, right_);
        } else if (e->kind == "vue") {
            loadProjects();
            const hmi::Id id = idOf(key);
            const hmi::View* va = pa_ ? pa_->view(id) : nullptr;
            const hmi::View* vb = pb_ ? pb_->view(id) : nullptr;
            viewChanges_ = ver::compareViews(va ? *va : hmi::View{}, vb ? *vb : hmi::View{});
        } else if (key == "variables") {
            loadProjects();
            std::map<std::string, const hmi::Variable*> ma, mb;
            if (pa_) for (const auto& v : pa_->programs.variables) ma[hmikit::lower(v.name)] = &v;
            if (pb_) for (const auto& v : pb_->programs.variables) mb[hmikit::lower(v.name)] = &v;
            for (const auto& [k, v] : mb)
                if (!ma.count(k)) details_.push_back("+ " + v->name + " : " + v->type + (v->equipment.empty() ? std::string{} : " \xC2\xB7 " + v->equipment + " " + v->address));
            for (const auto& [k, v] : mb)
                if (ma.count(k) && !(*ma[k] == *v)) {
                    const auto* o = ma[k];
                    std::string what;
                    if (o->type != v->type) what += " type " + o->type + " \xE2\x86\x92 " + v->type;
                    if (o->initial != v->initial) what += " initiale " + o->initial + " \xE2\x86\x92 " + v->initial;
                    if (o->equipment != v->equipment || o->address != v->address) what += " liaison " + o->equipment + " " + o->address + " \xE2\x86\x92 " + v->equipment + " " + v->address;
                    if (what.empty()) what = " description, dossier ou acc\xC3\xA8s";
                    details_.push_back("\xE2\x9C\x8E " + v->name + " :" + what);
                }
            for (const auto& [k, v] : ma)
                if (!mb.count(k)) details_.push_back("\xE2\x88\x92 " + v->name + " : " + v->type);
        }
        if (!diff_.empty()) {
            // Au debut de la premiere difference, trois lignes au-dessus.
            for (std::size_t i = 0; i < diff_.size(); ++i)
                if (diff_[i].kind != ver::DiffLine::Same) {
                    scroll_ = static_cast<float>(i > 3 ? i - 3 : 0);
                    break;
                }
        }
    }
    invalidate();
}

void VersionComparePane::showElement(const std::string& key) {
    for (std::size_t i = 0; i < comp_.elements.size(); ++i)
        if (key.empty() || comp_.elements[i].key == key) {
            current_ = static_cast<int>(i);
            break;
        }
    rebuildList();
    showCurrent();
}

void VersionComparePane::step(int delta) {
    if (comp_.elements.empty()) return;
    current_ = std::clamp(current_ + delta, 0, static_cast<int>(comp_.elements.size()) - 1);
    rebuildList();
    showCurrent();
}

void VersionComparePane::setUnified(bool u) {
    unified_ = u;
    scroll_ = 0.f;
    showCurrent();
}

bool VersionComparePane::restorable(const ver::Element& e) {
    static const char* const kPrefixes[] = {"vue:", "script:", "fonction:", "alarme:", "recette:", "style:", "essai:",
                                            "rapport:", "equipement:", "externe:", "ressource:", "section:"};
    for (const char* p : kPrefixes) if (e.key.rfind(p, 0) == 0) return true;
    static const char* const kWhole[] = {"variables", "types", "config", "alarmes-reglages", "securite", "historiques",
                                         "langues", "unites", "communication", "poste", "notifications", "web", "reseau-simule"};
    for (const char* k : kWhole) if (e.key == k) return true;
    return false;
}

bool VersionComparePane::restoreCurrent(std::string* why) {
    const auto* e = current();
    std::string reason;
    if (!e) reason = "choisis d'abord une diff\xC3\xA9rence";
    else if (a_ == 0) reason = "la version de gauche est le travail en cours : rien \xC3\xA0 restaurer";
    else if (!restorable(*e)) reason = "cet \xC3\xA9l\xC3\xA9ment ne se restaure pas seul : restaure la version enti\xC3\xA8re (Versions \xE2\x80\xBA Restaurer)";
    else if (!hosts_.restoreElement) reason = "impossible ici";
    if (reason.empty() && !hosts_.restoreElement(*e, a_, &reason)) {
        if (reason.empty()) reason = "refus\xC3\xA9";
    } else if (reason.empty()) {
        say(e->name + " : restaur\xC3\xA9 depuis V" + std::to_string(a_) + " (Ctrl+Z l'annule)");
        return true;
    }
    say("Restaurer " + (e ? e->name : std::string("?")) + " : " + reason, true);
    if (why) *why = reason;
    return false;
}

void VersionComparePane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, std::max(200.f, b.w - 560.f), 38.f});
    boxA_->setBounds({b.x + b.w - 550.f, b.y + 5.f, 250.f, 28.f});
    boxB_->setBounds({b.x + b.w - 270.f, b.y + 5.f, 260.f, 28.f});
    status_->setBounds({b.x, b.y + b.h - 24.f, b.w, 24.f});
    const float listW = std::min(360.f, b.w * 0.26f);
    list_->setBounds({b.x, b.y + 40.f, listW, std::max(0.f, b.h - 64.f)});
    body_->setBounds({b.x + listW + 4.f, b.y + 40.f, std::max(0.f, b.w - listW - 4.f), std::max(0.f, b.h - 64.f)});
}

} // namespace app
