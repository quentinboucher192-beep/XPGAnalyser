// =============================================================================
//  ui/widgets/SearchField.cpp - lot API 8 : le champ de recherche d'un volet
//  (voir SearchField.hpp).
// =============================================================================
#include "SearchField.hpp"

#include "Controls.hpp"

#include <algorithm>

namespace ui {

namespace {

const gfx::FontId kCount{13};

std::string thousands(std::size_t n) {
    const std::string d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

} // namespace

SearchField::SearchField(const std::string& fieldId, std::string placeholder, std::string tooltip) : Widget(fieldId + ".zone") {
    field_ = &static_cast<InputText&>(addChild(std::make_unique<InputText>(fieldId)));
    field_->setPlaceholder(std::move(placeholder));
    field_->setEscapeClears(true);          // lot API 8 : finitions - Echap efface la recherche
    field_->setTooltip(tooltip.empty() ? std::string("Chaque mot est cherch\xC3\xA9 dans le nom et la description (ET), sans casse ni accents ; "
                                                     "\"une phrase\" entre guillemets ; -mot : l'exclure.")
                                       : std::move(tooltip));
    links_ += field_->textChanged->connect([this](const std::string& t) {
        query_ = SearchQuery(t);
        changed->emit();
    });
}

SearchField::~SearchField() = default;

const std::string& SearchField::text() const noexcept { return field_->text(); }

void SearchField::setText(const std::string& text) { field_->setText(text); }

void SearchField::setCount(std::size_t shown, std::size_t total) {
    if (counted_ && shown == shown_ && total == total_) return;
    counted_ = true;
    shown_ = shown;
    total_ = total;
    invalidate();
}

std::string SearchField::countText() const {
    if (!counted_ || query_.empty()) return {};
    return thousands(shown_) + " sur " + thousands(total_);
}

void SearchField::recall() {
    recalled_ = true;
    // La premiere fois : branche (chaque frappe est retenue) et relu.
    if (!memory_.bound()) memory_.bindField(*field_);
    else memory_.recall();
}

SizeHint SearchField::sizeHint() const {
    SizeHint h;
    h.preferred = {540.f, 28.f};
    h.minimum = {200.f, 28.f};
    h.stretchX = 1.f;
    return h;
}

void SearchField::onLayout() {
    // Filet : un volet qui n'a pas demande sa recherche la retrouve au premier placement.
    if (!recalled_) recall();
    const auto b = bounds();
    const float w = std::clamp(b.w - 130.f, std::min(160.f, b.w), 420.f);
    field_->setBounds({b.x, b.y, w, b.h});
}

void SearchField::onPaint(const PaintContext& ctx) {
    const auto count = countText();
    if (count.empty()) return;
    const auto fb = field_->bounds();
    const float lh = ctx.r.lineHeight(kCount);
    const float room = bounds().right() - (fb.right() + 10.f);
    if (room < ctx.r.measure(count, kCount).width) return;
    const auto colour = shown_ == 0 ? ctx.theme.onSurface(ctx.theme.color.warning) : ctx.theme.color.textMuted;
    ctx.r.drawText({fb.right() + 10.f, fb.y + (fb.h - lh) * 0.5f}, count, kCount, colour);
}

} // namespace ui
