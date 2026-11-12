// app/hmi/HmiStretchDialog.cpp - 1.50.0 : la fenetre « Etirer... » (voir HmiStretchDialog.hpp).
#include "HmiStretchDialog.hpp"
#include "../../ui/DialogFrame.hpp"   // la fenetre se deplace

#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace app {

namespace st = hmi::stretch;

namespace {
const gfx::FontId kSmall{13};
constexpr unsigned kEdges[4] = {st::kLeft, st::kRight, st::kTop, st::kBottom};
constexpr st::To kTargets[4] = {st::To::View, st::To::Selection, st::To::Neighbour, st::To::Guide};
const char* const kTargetLabels[4] = {"la vue", "s\xC3\xA9lection", "voisin", "rep\xC3\xA8re"};
const char* const kEdgeArrows[4] = {"\xE2\x86\x90", "\xE2\x86\x92", "\xE2\x86\x91", "\xE2\x86\x93"};
const char* const kEdgeTips[4] = {"Le bord gauche", "Le bord droit", "Le bord du haut", "Le bord du bas"};
} // namespace

// ------------------------------------------------------------------ Body ---
// La carte de l'objet et ses quatre fleches (a gauche), Jusqu'a / Marge / Taille (a droite), la note et les
// boutons (en bas).
class HmiStretchDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiStretchDialog& d) : d_(d) {}
    gfx::Rect panel{}, card{};
    std::array<ui::Button*, 2> buttons{};   // Annuler, Etirer
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(640.f, r.w - 40.f), h = std::min(300.f, r.h - 40.f);
        // A droite de la fenetre : la vue reste visible (l'apercu suit chaque changement).
        panel = ui::dialogframe::place(*this, {std::floor(std::max(20.f, r.w - w - 40.f)), std::floor((r.h - h) * 0.5f), w, h});
        const float top = panel.y + 44.f;
        const float cx = panel.x + 110.f, cy = top + 86.f;
        card = {cx - 60.f, cy - 32.f, 120.f, 64.f};
        const gfx::Rect at[4] = {{card.x - 36.f, cy - 12.f, 28.f, 24.f}, {card.right() + 8.f, cy - 12.f, 28.f, 24.f},
                                 {cx - 14.f, card.y - 32.f, 28.f, 24.f}, {cx - 14.f, card.bottom() + 8.f, 28.f, 24.f}};
        for (std::size_t i = 0; i < 4; ++i)
            if (d_.edgeButtons_[i]) d_.edgeButtons_[i]->setBounds(at[i]);
        const float x0 = panel.x + 240.f + 76.f;
        float x = x0;
        for (auto* b : d_.targetButtons_) {
            if (!b) continue;
            const float bw = ui::measureWidth(b->text(), gfx::FontId{16}) + 22.f;
            b->setBounds({x, top + 10.f, bw, 28.f});
            x += bw + 2.f;
        }
        if (d_.marginField_) d_.marginField_->setBounds({x0, top + 54.f, 70.f, 28.f});
        float bx = panel.x + panel.w - 16.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            if (!*it) continue;
            const float bw = std::max(100.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.y + panel.h - 44.f, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        // La carte de l'objet.
        ctx.r.fillRect(card, c.inputBg);
        ctx.r.strokeRect(card, c.accent, 1.5f);
        ctx.r.pushClip(card);
        const float tw = ctx.r.measure(d_.spec_.object, ctx.theme.font.ui).width;
        ctx.r.drawText({card.x + std::max(4.f, (card.w - tw) * 0.5f), card.y + (card.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f},
                       d_.spec_.object, ctx.theme.font.ui, c.text);
        ctx.r.popClip();
        const float top = panel.y + 44.f, lx = panel.x + 240.f;
        const float lh = ctx.r.lineHeight(ctx.theme.font.ui);
        ctx.r.drawText({lx, top + 10.f + (28.f - lh) * 0.5f}, "Jusqu'\xC3\xA0", ctx.theme.font.ui, c.textMuted);
        ctx.r.drawText({lx, top + 54.f + (28.f - lh) * 0.5f}, "Marge", ctx.theme.font.ui, c.textMuted);
        if (d_.marginField_) {
            const auto mr = d_.marginField_->bounds();
            const bool viewOne = std::fabs(d_.request_.margin - d_.spec_.viewMargin) < 0.01;
            const std::string after = d_.request_.margin <= 0 ? std::string("px \xE2\x80\x94 l'objet touche")
                                    : viewOne ? std::string("px \xE2\x80\x94 celle de la vue") : std::string("px");
            ctx.r.drawText({mr.right() + 10.f, mr.y + (28.f - lh) * 0.5f}, after, ctx.theme.font.ui, c.textMuted);
        }
        ctx.r.drawText({lx, top + 98.f}, "Taille", ctx.theme.font.ui, c.textMuted);
        // La taille obtenue (ou pourquoi rien), sur deux lignes au besoin.
        const float x0 = lx + 76.f, room = panel.right() - 16.f - x0;
        const auto col = d_.possible_ ? c.text : c.warning;
        std::string rest = d_.size_;
        float y = top + 98.f;
        for (int line = 0; line < 3 && !rest.empty(); ++line, y += 18.f) {
            std::size_t n = ctx.r.fitCharacters(rest, kSmall, room);
            if (n < rest.size()) {
                const auto cut = rest.rfind(' ', n);
                if (cut != std::string::npos && cut > 0) n = cut;
            }
            ctx.r.drawText({x0, y}, rest.substr(0, n), kSmall, col);
            rest = n < rest.size() ? rest.substr(n + (rest[n] == ' ' ? 1 : 0)) : std::string{};
        }
        ctx.r.fillRect({panel.x, panel.y + panel.h - 56.f, panel.w, 1.f}, c.border);
        ctx.r.drawText({panel.x + 16.f, panel.y + panel.h - 44.f + (30.f - lh) * 0.5f},
                       "L'aper\xC3\xA7u suit chaque changement sur la vue.", ctx.theme.font.ui, c.textMuted);
    }
private:
    HmiStretchDialog& d_;
};

// ===========================================================================
HmiStretchDialog::HmiStretchDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    request_ = spec_.request;
    if (request_.margin < 0) request_.margin = request_.keepMargins ? spec_.viewMargin : 0.0;
    request_.keepMargins = true;   // la marge est celle du champ (0 : l'objet touche)
    request_.index = -1;
    if (request_.to == st::To::Column) request_.to = st::To::View;
    if ((request_.edges & st::kAll) == 0) request_.edges = st::kRight;
    refresh();
}

menu::MenuTraits HmiStretchDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = false;   // l'apercu se voit sur la vue
    return t;
}

void HmiStretchDialog::refresh() {
    if (spec_.preview) {
        const auto [ok, text] = spec_.preview(request_);
        possible_ = ok;
        size_ = text;
    } else {
        possible_ = (request_.edges & st::kAll) != 0;
        size_.clear();
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (edgeButtons_[i]) edgeButtons_[i]->setStyle((request_.edges & kEdges[i]) ? ui::Button::Style::Primary : ui::Button::Style::Default);
        if (targetButtons_[i]) targetButtons_[i]->setStyle(request_.to == kTargets[i] ? ui::Button::Style::Primary : ui::Button::Style::Default);
    }
    if (ok_) ok_->setEnabled(possible_);
    if (body_) body_->invalidate();
}

void HmiStretchDialog::setEdge(unsigned edge, bool on) {
    const unsigned now = on ? (request_.edges | edge) : (request_.edges & ~edge);
    if (now == request_.edges) return;
    request_.edges = now & st::kAll;
    refresh();
}

void HmiStretchDialog::setTarget(st::To to) {
    if (to == st::To::Column || to == request_.to) return;
    request_.to = to;
    refresh();
}

void HmiStretchDialog::setMargin(double px) {
    px = std::clamp(px, 0.0, 10000.0);
    if (std::fabs(px - request_.margin) < 1e-9) return;
    request_.margin = px;
    if (marginField_ && std::atof(marginField_->text().c_str()) != px) {
        syncing_ = true;
        marginField_->setText(hmi::formatNumber(px));
        syncing_ = false;
    }
    refresh();
}

bool HmiStretchDialog::confirm() {
    if (!possible_) return false;
    finish(true);
    return true;
}

void HmiStretchDialog::cancel() { finish(false); }

std::string HmiStretchDialog::encode(const st::Request& r) {
    return std::to_string(r.edges) + ";" + std::to_string(static_cast<int>(r.to)) + ";" + hmi::formatNumber(r.margin) + ";"
           + std::to_string(r.index) + ";" + (r.keepMargins ? "1" : "0");
}

st::Request HmiStretchDialog::parse(const std::string& payload) {
    st::Request r;
    std::vector<std::string> f;
    std::size_t from = 0;
    while (true) {
        const auto at = payload.find(';', from);
        f.push_back(payload.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    if (f.size() < 5) return r;
    r.edges = static_cast<unsigned>(std::strtoul(f[0].c_str(), nullptr, 10)) & st::kAll;
    const int to = std::atoi(f[1].c_str());
    r.to = to >= 0 && to <= static_cast<int>(st::To::Column) ? static_cast<st::To>(to) : st::To::View;
    r.margin = std::atof(f[2].c_str());
    r.index = std::atoi(f[3].c_str());
    r.keepMargins = f[4] == "1";
    return r;
}

void HmiStretchDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    if (auto* m = menu::MenuManager::instance())
        m->CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                          ok ? encode(request_) : std::string{}});
}

core::Status HmiStretchDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    for (std::size_t i = 0; i < 4; ++i) {
        auto b = std::make_unique<ui::Button>(kEdgeArrows[i], base + ".bord" + std::to_string(i));
        b->setTooltip(std::string(kEdgeTips[i]) + " : il va jusqu'\xC3\xA0 la cible (un clic le prend ou le laisse)");
        edgeButtons_[i] = &static_cast<ui::Button&>(body->addChild(std::move(b)));
        const unsigned edge = kEdges[i];
        links_ += edgeButtons_[i]->clicked->connect([this, edge] { setEdge(edge, (request_.edges & edge) == 0); });
    }
    for (std::size_t i = 0; i < 4; ++i) {
        auto b = std::make_unique<ui::Button>(kTargetLabels[i], base + ".cible" + std::to_string(i));
        targetButtons_[i] = &static_cast<ui::Button&>(body->addChild(std::move(b)));
        const st::To to = kTargets[i];
        links_ += targetButtons_[i]->clicked->connect([this, to] { setTarget(to); });
    }
    {
        auto f = std::make_unique<ui::InputText>(base + ".marge");
        f->setText(hmi::formatNumber(request_.margin));
        marginField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += marginField_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            setMargin(t.empty() ? 0.0 : std::atof(t.c_str()));
        });
    }
    auto* cancelButton = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    body->buttons[0] = cancelButton;
    links_ += cancelButton->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("\xC3\x89tirer", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->buttons[1] = ok_;
    links_ += ok_->clicked->connect([this] { confirm(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

ui::EventResult HmiStretchDialog::HandleEvent(const ui::InputEvent& ev) {
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) { finish(false); return ui::EventResult::Consumed; }
        if (k->key == ui::Key::Return) { confirm(); return ui::EventResult::Consumed; }
    }
    return r;
}

} // namespace app
