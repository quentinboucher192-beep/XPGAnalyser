// =============================================================================
//  license_admin/gui/PcPicker.cpp - 1.51.0 : chercher le PC d'un futur admin (voir PcPicker.hpp)
// =============================================================================
#include "PcPicker.hpp"

#include "AdminsKit.hpp"

#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace license_admin::gui {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr FontId kF12{12}, kF13{13}, kF14{14};
constexpr float  kFieldH = 34.f;
constexpr float  kCountW = 120.f;
constexpr char   kSep = '\x1F';   // entre les textes d'un PC : un terme ne chevauche jamais deux champs

std::string dayText(Seconds t) {
    const std::tm tm = kit::localTm(t);
    char b[40];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return b;
}

// La deuxieme ligne d'un PC : le client, le Studio, le debut de son identifiant.
std::string secondLine(const MessagePc& pc) {
    std::string s = pc.customer.empty() ? std::string("client inconnu") : pc.customer;
    if (!pc.studio.empty()) s += " \xC2\xB7 " + pc.studio;
    else if (!pc.studioVersion.empty()) s += " \xC2\xB7 Studio " + pc.studioVersion;
    if (!pc.deviceId.empty()) s += " \xC2\xB7 " + pc.deviceId.substr(0, 8);
    return s;
}

// Le texte, ses plages surlignees (fond pale), coupe a `w` (les plages au-dela de la coupe ne se dessinent pas).
void drawMarked(gfx::IRenderer& r, float x, float y, const std::string& text, FontId f, Color ink, Color mark, float w,
                const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
    const std::string shown = kit::fit(r, text, f, w);
    std::size_t keep = 0;   // les octets du texte d'origine que montre `shown`
    while (keep < shown.size() && keep < text.size() && shown[keep] == text[keep]) ++keep;
    const float h = r.lineHeight(f);
    for (const auto& [a, b] : ranges) {
        if (a >= keep) continue;
        const std::size_t e = std::min(b, keep);
        const float x0 = x + r.measure(text.substr(0, a), f).width;
        const float x1 = x + r.measure(text.substr(0, e), f).width;
        r.fillRoundedRect({x0 - 1.f, y - 1.f, x1 - x0 + 2.f, h + 2.f}, mark, 3.f);
    }
    r.drawText({x, y}, shown, f, ink);
}

} // namespace

PcPicker::PcPicker(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);
    auto field = std::make_unique<ui::InputText>(this->id() + ".recherche");
    field->setPlaceholder("Chercher : nom du PC, client, version, identifiant, aujourd'hui\xE2\x80\xA6");
    field->setMaxLength(120);
    field->setEscapeClears(true);
    field_ = &static_cast<ui::InputText&>(addChild(std::move(field)));
    links_ += field_->textChanged->connect([this](const std::string&) {
        refilter();
        invalidate();
    });
    links_ += field_->editingDone->connect([this](const std::string&) { pick(); });
}

const std::string& PcPicker::query() const noexcept { return field_->text(); }

void PcPicker::setQuery(const std::string& text) {
    field_->setText(text);   // textChanged : refilter
    refilter();
    invalidate();
}

std::string PcPicker::labelOf(const MessagePc& pc) { return pc.name.empty() ? "PC " + pc.deviceId.substr(0, 12) : pc.name; }

std::string PcPicker::lastSeenText(const MessagePc& pc) const {
    const Seconds t = pc.lastSeen;
    if (t <= 0) return "jamais vu";
    if (kit::sameDay(now_, t)) return "aujourd'hui";
    if (kit::sameDay(now_ - 86400, t)) return "hier";
    if (now_ > t && now_ - t < 7 * 86400) return "il y a " + std::to_string(std::max<Seconds>(2, (now_ - t) / 86400)) + " jours";
    return "le " + dayText(t);
}

void PcPicker::setPcs(std::vector<MessagePc> pcs, Seconds now) {
    const std::string keep = selectedValue();
    pcs_ = std::move(pcs);
    now_ = now;
    folded_.clear();
    folded_.reserve(pcs_.size());
    order_.clear();
    for (std::size_t i = 0; i < pcs_.size(); ++i) {
        const MessagePc& p = pcs_[i];
        std::string all;
        for (const std::string& t : {labelOf(p), p.deviceId, p.customer, p.licenseId, p.studio, p.studioVersion, lastSeenText(p),
                                     p.lastSeen > 0 ? dayText(p.lastSeen) : std::string()}) {
            all += t;
            all += kSep;
        }
        folded_.push_back(ui::foldForSearch(all));
        order_.push_back(static_cast<int>(i));
    }
    // Les plus recemment actifs d'abord ; a egalite, par nom.
    std::stable_sort(order_.begin(), order_.end(), [this](int a, int b) {
        const MessagePc& x = pcs_[static_cast<std::size_t>(a)];
        const MessagePc& y = pcs_[static_cast<std::size_t>(b)];
        if (x.lastSeen != y.lastSeen) return x.lastSeen > y.lastSeen;
        return labelOf(x) < labelOf(y);
    });
    current_ = -1;
    refilter();
    if (!keep.empty()) (void)select(keep);
    invalidate();
}

void PcPicker::refilter() {
    const std::string before = selectedValue();
    const ui::SearchQuery q(field_->text());
    shown_.clear();
    for (const int i : order_) {
        const std::string& hay = folded_[static_cast<std::size_t>(i)];
        bool ok = true;
        for (const auto& t : q.terms())
            if (hay.find(t) == std::string::npos) {
                ok = false;
                break;
            }
        if (ok)
            for (const auto& t : q.excluded())
                if (hay.find(t) != std::string::npos) {
                    ok = false;
                    break;
                }
        if (ok) shown_.push_back(i);
    }
    // Le choix : celui d'avant s'il est encore montre, sinon le premier.
    current_ = shown_.empty() ? -1 : 0;
    for (std::size_t k = 0; k < shown_.size(); ++k) {
        const MessagePc& p = pcs_[static_cast<std::size_t>(shown_[k])];
        if (!before.empty() && (p.deviceId == before || p.deviceKey == before)) current_ = static_cast<int>(k);
    }
    first_ = 0;
    ensureVisible();
    selectionChanged->emit();
}

const MessagePc* PcPicker::selected() const {
    if (current_ < 0 || static_cast<std::size_t>(current_) >= shown_.size()) return nullptr;
    return &pcs_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(current_)])];
}

std::string PcPicker::selectedValue() const {
    const MessagePc* p = selected();
    if (!p) return {};
    return p->deviceId.empty() ? p->deviceKey : p->deviceId;
}

bool PcPicker::select(const std::string& value) {
    for (std::size_t k = 0; k < shown_.size(); ++k) {
        const MessagePc& p = pcs_[static_cast<std::size_t>(shown_[k])];
        if (!value.empty() && (p.deviceId == value || p.deviceKey == value)) {
            current_ = static_cast<int>(k);
            ensureVisible();
            selectionChanged->emit();
            invalidate();
            return true;
        }
    }
    return false;
}

bool PcPicker::selectShown(int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return false;
    current_ = row;
    ensureVisible();
    selectionChanged->emit();
    invalidate();
    return true;
}

void PcPicker::moveSelection(int delta) {
    if (shown_.empty()) return;
    const int last = static_cast<int>(shown_.size()) - 1;
    current_ = std::clamp(current_ < 0 ? 0 : current_ + delta, 0, last);
    ensureVisible();
    selectionChanged->emit();
    invalidate();
}

void PcPicker::pick() {
    if (selected()) picked->emit();
}

int PcPicker::visibleRows() const noexcept { return std::max(1, static_cast<int>(std::floor(listRect_.h / rowH_))); }

void PcPicker::ensureVisible() {
    const int rows = visibleRows();
    const int n = static_cast<int>(shown_.size());
    if (current_ >= 0) {
        if (current_ < first_) first_ = current_;
        if (current_ >= first_ + rows) first_ = current_ - rows + 1;
    }
    first_ = std::clamp(first_, 0, std::max(0, n - rows));
}

std::string PcPicker::countText() const {
    if (pcs_.empty()) return "aucun PC connu";
    const std::size_t n = shown_.size(), all = pcs_.size();
    if (field_->text().empty() || n == all) return std::to_string(all) + " PC";
    if (n == 0) return "aucun sur " + std::to_string(all);
    return std::to_string(n) + " sur " + std::to_string(all) + " PC";
}

std::vector<std::pair<std::size_t, std::size_t>> PcPicker::highlights(const std::string& text) const {
    const ui::SearchQuery q(field_->text());
    if (q.terms().empty()) return {};
    return q.ranges(text);
}

int PcPicker::rowAt(gfx::Point p) const {
    if (!listRect_.contains(p)) return -1;
    const int k = first_ + static_cast<int>((p.y - listRect_.y) / rowH_);
    return k >= 0 && k < static_cast<int>(shown_.size()) ? k : -1;
}

void PcPicker::onLayout() {
    const Rect b = bounds();
    field_->setBounds({b.x, b.y, std::max(80.f, b.w - kCountW - 10.f), kFieldH});
    listRect_ = {b.x, b.y + kFieldH + 8.f, b.w, std::max(rowH_, b.h - kFieldH - 8.f)};
    ensureVisible();
}

void PcPicker::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const Rect b = bounds();
    // le compte, a droite du champ
    {
        const std::string n = countText();
        const bool none = !pcs_.empty() && shown_.empty();
        r.drawText({b.right() - r.measure(n, kF13).width, b.y + (kFieldH - r.lineHeight(kF13)) * 0.5f}, n, kF13, none ? c.warning : c.textMuted);
    }
    ui::shapes::fillFramedRect(r, listRect_, c.panelBg, c.border, 8.f);
    if (pcs_.empty() || shown_.empty()) {
        const std::string t = pcs_.empty() ? std::string("Aucun PC connu sur ce poste (ni activation, ni demande) : choisis \xC2\xAB Par un lien \xC2\xBB.")
                                           : "Aucun PC ne correspond \xC3\xA0 \xC2\xAB " + field_->text() + " \xC2\xBB (Echap : tout voir).";
        (void)kit::drawWrapped(r, listRect_.x + 14.f, listRect_.y + 14.f, listRect_.w - 28.f, t, kF13, c.textMuted, 18.f, 3);
        return;
    }
    const Color mark = c.accent.withAlpha(46);
    const int rows = visibleRows();
    r.pushClip(listRect_.inset(1.f, 1.f));
    for (int v = 0; v < rows; ++v) {
        const int k = first_ + v;
        if (k >= static_cast<int>(shown_.size())) break;
        const MessagePc& p = pcs_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(k)])];
        const Rect row{listRect_.x + 1.f, listRect_.y + static_cast<float>(v) * rowH_, listRect_.w - 2.f, rowH_};
        if (k == current_) {
            r.fillRect(row, c.accent.withAlpha(30));
            r.fillRect({row.x, row.y, 3.f, row.h}, c.accent);
        }
        if (v + 1 < rows) r.fillRect({row.x + 10.f, row.bottom() - 1.f, row.w - 20.f, 1.f}, c.gridLine);
        const std::string when = lastSeenText(p);
        const float ww = r.measure(when, kF12).width;
        const FontId fn = gfx::semibold(kF14);
        const std::string label = labelOf(p);
        drawMarked(r, row.x + 14.f, row.y + 6.f, label, fn, c.text, mark, row.w - 40.f - ww, highlights(label));
        drawMarked(r, row.right() - 14.f - ww, row.y + 8.f, when, kF12, p.lastSeen > 0 && kit::sameDay(now_, p.lastSeen) ? c.ok : c.textMuted, mark, ww + 2.f,
                   highlights(when));
        const std::string sub = secondLine(p);
        drawMarked(r, row.x + 14.f, row.y + 26.f, sub, kF12, c.textMuted, mark, row.w - 28.f, highlights(sub));
    }
    r.popClip();
    // la barre de defilement (sa place seulement : la molette, le clavier)
    if (static_cast<int>(shown_.size()) > rows) {
        const float th = std::max(24.f, listRect_.h * static_cast<float>(rows) / static_cast<float>(shown_.size()));
        const float range = static_cast<float>(shown_.size() - static_cast<std::size_t>(rows));
        const float ty = listRect_.y + 2.f + (listRect_.h - 4.f - th) * (range > 0.f ? static_cast<float>(first_) / range : 0.f);
        r.fillRoundedRect({listRect_.right() - 7.f, ty, 4.f, th}, c.textDisabled, 2.f);
    }
}

ui::EventResult PcPicker::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const int k = rowAt(d->pos);
        if (k < 0) return ui::EventResult::Ignored;
        grabFocus();   // le clavier suit (Haut, Bas, Entree)
        current_ = k;
        selectionChanged->emit();
        if (d->clickCount >= 2) pick();
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && listRect_.contains(w->pos)) {
        const int rows = visibleRows();
        first_ = std::clamp(first_ + (w->dy > 0 ? -3 : 3), 0, std::max(0, static_cast<int>(shown_.size()) - rows));
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && (focused() || field_->focused())) {
        switch (k->key) {
            case ui::Key::Down: moveSelection(1); return ui::EventResult::Consumed;
            case ui::Key::Up: moveSelection(-1); return ui::EventResult::Consumed;
            case ui::Key::PageDown: moveSelection(visibleRows()); return ui::EventResult::Consumed;
            case ui::Key::PageUp: moveSelection(-visibleRows()); return ui::EventResult::Consumed;
            case ui::Key::Return:
                pick();
                return ui::EventResult::Consumed;
            case ui::Key::Escape:
                if (!field_->text().empty()) {
                    setQuery({});
                    return ui::EventResult::Consumed;
                }
                return ui::EventResult::Ignored;
            default: break;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace license_admin::gui
