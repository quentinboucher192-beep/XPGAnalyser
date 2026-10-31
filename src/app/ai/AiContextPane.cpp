// =============================================================================
//  app/ai/AiContextPane.cpp - voir AiContextPane.hpp
// =============================================================================
#include "AiContextPane.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>

namespace app::ai {

namespace {

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
const gfx::FontId kCaption{11};
constexpr float kHeadH = 28.f;      // la ligne du haut : le titre, le compteur
constexpr float kSectionH = 26.f;   // << FICHIERS JOINTS >>, la recherche
constexpr float kRowH = 38.f;       // un fichier : deux lignes
constexpr float kEmptyH = 26.f;     // << aucun fichier >>
constexpr std::size_t kMaxRows = 6;
constexpr float kIcon = 22.f;       // un bouton de geste

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

ui::Icon iconFor(const std::string& ext) {
    if (ext == "png" || ext == "jpg" || ext == "jpeg") return ui::Icon::Image;
    if (ext == "xlsx" || ext == "csv") return ui::Icon::Chart;
    if (ext == "st" || ext == "c" || ext == "cpp" || ext == "h" || ext == "hpp" || ext == "json" || ext == "xml") return ui::Icon::Code;
    if (ext == "xihm" || ext == "xapi" || ext == "xpgproj" || ext == "xef" || ext == "xpg" || ext == "zef") return ui::Icon::Project;
    return ui::Icon::Document;
}

// L'etat d'analyse, en mots, et sa couleur (0 attente, 1 ok, 3 erreur, 4 info).
std::pair<std::string, int> analysisLook(const attach::ContextFile& f) {
    const auto& s = f.attachment.analysis;
    if (s == "analysee") return {"Analys\xC3\xA9", 1};
    if (s == "en-cours") return {"En cours", 4};
    if (s == "echec") return {"\xC3\x89" "chec", 3};
    return {"Non analys\xC3\xA9", 0};
}

std::pair<std::string, int> availabilityLook(const attach::ContextFile& f) {
    if (f.attachment.removed) return {"Retir\xC3\xA9", 5};
    switch (f.availability) {
        case attach::Availability::Present: return {"Disponible", 1};
        case attach::Availability::Moved: return {"D\xC3\xA9plac\xC3\xA9", 2};
        case attach::Availability::Missing: return {"Manquant", 3};
    }
    return {};
}

gfx::Color toneColor(const ui::Theme& th, int tone) {
    const auto& c = th.color;
    switch (tone) {
        case 1: return c.ok;
        case 2: return c.warning;
        case 3: return c.error;
        case 4: return c.info;
        case 5: return c.textDisabled;
        default: return c.textMuted;
    }
}

std::string actionTip(AiContextPane::Action a, bool removed) {
    switch (a) {
        case AiContextPane::Action::Open: return "Ouvrir (pr\xC3\xA9visualiser) le fichier";
        case AiContextPane::Action::Details: return "Les informations d\xC3\xA9taill\xC3\xA9" "es";
        case AiContextPane::Action::Analyses: return "Les analyses pr\xC3\xA9" "c\xC3\xA9" "dentes";
        case AiContextPane::Action::Remove:
            return removed ? std::string("Remettre dans la discussion") : std::string("Retirer de la discussion (sa copie reste, selon les r\xC3\xA8gles de conservation)");
    }
    return {};
}

ui::Icon actionIcon(AiContextPane::Action a, bool removed) {
    switch (a) {
        case AiContextPane::Action::Open: return ui::Icon::Eye;
        case AiContextPane::Action::Details: return ui::Icon::Info;
        case AiContextPane::Action::Analyses: return ui::Icon::History;
        case AiContextPane::Action::Remove: return removed ? ui::Icon::Undo : ui::Icon::Close;
    }
    return ui::Icon::None;
}

constexpr AiContextPane::Action kActions[] = {AiContextPane::Action::Open, AiContextPane::Action::Details, AiContextPane::Action::Analyses,
                                              AiContextPane::Action::Remove};

} // namespace

AiContextPane::AiContextPane(std::string id, Callbacks callbacks) : ui::Widget(std::move(id)), cb_(std::move(callbacks)) {
    auto box = std::make_unique<ui::InputText>(this->id() + ".recherche");
    box->setPlaceholder("Chercher un fichier");
    searchBox_ = &static_cast<ui::InputText&>(addChild(std::move(box)));
    searchBox_->setVisibility(ui::Visibility::Collapsed);
    links_ += searchBox_->textChanged->connect([this](const std::string& t) {
        if (t != search_) setSearch(t);
    });
}

void AiContextPane::setContext(attach::DiscussionContext ctx) {
    const bool other = ctx.conversation != ctx_.conversation;
    ctx_ = std::move(ctx);
    if (other) {
        scroll_ = 0.f;
        hover_ = -1;
    }
    const float maxFirst = static_cast<float>(shown().size() > visibleRows() ? shown().size() - visibleRows() : 0);
    scroll_ = std::clamp(scroll_, 0.f, maxFirst);
    if (parent()) parent()->invalidateLayout();
    invalidateLayout();
    invalidate();
}

float AiContextPane::heightFor(float /*width*/, float maxH) const {
    float h = kHeadH;
    if (open_) {
        const std::size_t n = shown().size();
        h += kSectionH + (n == 0 ? kEmptyH : static_cast<float>(std::min(n, kMaxRows)) * kRowH) + 6.f;
    }
    return std::min(h, std::max(kHeadH, maxH));
}

void AiContextPane::setExpanded(bool open) {
    if (open_ == open) return;
    open_ = open;
    scroll_ = 0.f;
    hover_ = -1;
    if (cb_.toggled) cb_.toggled(open_);
    if (parent()) parent()->invalidateLayout();
    invalidateLayout();
    invalidate();
}

void AiContextPane::setSearch(std::string text) {
    if (text == search_) return;
    search_ = std::move(text);
    if (searchBox_ && searchBox_->text() != search_) searchBox_->setText(search_);
    scroll_ = 0.f;
    hover_ = -1;
    if (cb_.search) cb_.search(search_);
    if (parent()) parent()->invalidateLayout();
    invalidateLayout();
    invalidate();
}

void AiContextPane::setShowRemoved(bool on) {
    if (showRemoved_ == on) return;
    showRemoved_ = on;
    scroll_ = 0.f;
    if (parent()) parent()->invalidateLayout();
    invalidate();
}

std::vector<const attach::ContextFile*> AiContextPane::shown() const {
    std::vector<const attach::ContextFile*> out;
    for (const auto* f : attach::searchFiles(ctx_, search_))
        if (showRemoved_ || !f->attachment.removed) out.push_back(f);
    return out;
}

std::string AiContextPane::counterText() const {
    std::string s = "Fichiers joints : " + std::to_string(ctx_.count());
    if (const std::size_t miss = ctx_.unavailable()) s += " \xC2\xB7 " + std::to_string(miss) + (miss > 1 ? " manquants" : " manquant");
    return s;
}

std::vector<std::string> AiContextPane::lines() const {
    std::vector<std::string> out;
    out.push_back("Contexte de la discussion");
    out.push_back(counterText());
    if (!open_) return out;
    out.push_back("Fichiers joints");
    const auto list = shown();
    if (list.empty()) out.push_back(ctx_.count() == 0 ? "Aucun fichier joint dans cette discussion." : "Aucun fichier ne correspond.");
    for (const auto* f : list) out.push_back(f->line());
    return out;
}

gfx::Rect AiContextPane::headRect() const {
    const auto b = bounds();
    return {b.x, b.y, b.w, kHeadH};
}

float AiContextPane::listTop() const { return bounds().y + kHeadH + kSectionH; }

std::size_t AiContextPane::visibleRows() const {
    const float room = bounds().bottom() - listTop() - 4.f;
    return room <= 0.f ? 0 : static_cast<std::size_t>(room / kRowH);
}

bool AiContextPane::searchVisible() const { return open_ && (ctx_.files.size() >= 3 || !search_.empty()); }

gfx::Rect AiContextPane::rowRect(std::size_t index) const {
    if (!open_) return {};
    const std::size_t first = static_cast<std::size_t>(scroll_);
    if (index < first || index >= first + visibleRows() || index >= shown().size()) return {};
    const auto b = bounds();
    return {b.x + 6.f, listTop() + static_cast<float>(index - first) * kRowH, b.w - 12.f, kRowH - 2.f};
}

gfx::Rect AiContextPane::actionRect(std::size_t index, Action a) const {
    const gfx::Rect row = rowRect(index);
    if (row.w <= 0.f) return {};
    const auto k = static_cast<float>(3 - static_cast<int>(a));   // de droite a gauche : retirer, analyses, detail, ouvrir
    return {row.right() - 8.f - kIcon - k * (kIcon + 4.f), row.y + (row.h - kIcon) * 0.5f, kIcon, kIcon};
}

void AiContextPane::onLayout() {
    if (!searchBox_) return;
    const bool show = searchVisible();
    searchBox_->setVisibility(show ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (show) {
        const auto b = bounds();
        const float w = std::min(220.f, std::max(120.f, b.w * 0.4f));
        searchBox_->setBounds({b.right() - 10.f - w, b.y + kHeadH + 2.f, w, kSectionH - 4.f});
    }
}

std::string AiContextPane::liveTooltip(gfx::Point mouse) const {
    for (auto it = tips_.rbegin(); it != tips_.rend(); ++it)
        if (it->first.contains(mouse)) return it->second;
    return {};
}

void AiContextPane::onPaint(const ui::PaintContext& ctx) {
    tips_.clear();
    const auto b = bounds();
    if (b.h <= 0.f || b.w <= 0.f) return;
    const auto& c = ctx.theme.color;
    const float meta = ctx.r.lineHeight(kMeta);
    ctx.r.pushClip(b);
    ctx.r.fillRect(b, ctx.theme.brand.card.a ? ctx.theme.brand.card : c.inputBg);
    ctx.r.fillRect({b.x, b.y, b.w, kHeadH}, alpha(c.info, 22));
    ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
    // La ligne du haut : deplier, le titre, le compteur.
    const gfx::Rect head = headRect();
    ui::drawIcon(ctx.r, open_ ? ui::Icon::Collapse : ui::Icon::Expand, {head.x + 8.f, head.y + 7.f, 14.f, 14.f}, c.textMuted);
    const std::string title = "Contexte de la discussion";
    const float ty = head.y + (kHeadH - ctx.r.lineHeight(kBody)) / 2.f;
    const float tw = ctx.r.measure(title, kBody).width;
    ctx.r.drawText({head.x + 28.f, ty}, elide(ctx.r, title, kBody, head.w * 0.5f), kBody, c.text);
    ctx.r.drawText({head.x + 28.8f, ty}, elide(ctx.r, title, kBody, head.w * 0.5f), kBody, c.text);   // le gras du logiciel
    {
        const std::string count = counterText();
        const bool missing = ctx_.unavailable() > 0;
        const gfx::Color tone = missing ? c.error : ctx_.count() ? c.info : c.textMuted;
        const float room = head.right() - 10.f - (head.x + 28.f + tw + 12.f);
        const std::string label = elide(ctx.r, count, kMeta, std::max(0.f, room - 14.f));
        const float cw = ctx.r.measure(label, kMeta).width + 14.f;
        if (!label.empty()) {
            const gfx::Rect chip{head.right() - 10.f - cw, head.y + 5.f, cw, 18.f};
            ctx.r.fillRoundedRect(chip, alpha(tone, 46), 9.f);
            ctx.r.drawText({chip.x + 7.f, chip.y + (chip.h - meta) / 2.f}, label, kMeta, tone);
            tips_.emplace_back(chip, count + (missing ? "\nUn fichier manquant n'est plus sur le poste : l'Assistant ne peut pas le lire." : std::string{}));
        }
    }
    tips_.emplace_back(gfx::Rect{head.x, head.y, head.w * 0.6f, kHeadH},
                       open_ ? std::string("Replier le contexte (la conversation ne change pas)")
                             : std::string("D\xC3\xA9plier : les fichiers joints de cette discussion, leur \xC3\xA9tat, leurs analyses"));
    if (!open_) {
        ctx.r.popClip();
        return;
    }
    // La section : << FICHIERS JOINTS >>, les retirees, la recherche (un enfant).
    const float sy = b.y + kHeadH;
    ctx.r.drawText({b.x + 10.f, sy + (kSectionH - ctx.r.lineHeight(kCaption)) / 2.f}, "FICHIERS JOINTS", kCaption, c.textMuted);
    const std::size_t removed = ctx_.files.size() - ctx_.count();
    if (removed > 0) {
        const std::string toggle = showRemoved_ ? "Cacher les retir\xC3\xA9s" : "Voir les retir\xC3\xA9s (" + std::to_string(removed) + ")";
        const float x = b.x + 10.f + ctx.r.measure("FICHIERS JOINTS", kCaption).width + 14.f;
        const float limit = searchVisible() && searchBox_ ? searchBox_->bounds().x - 8.f : b.right() - 10.f;
        const std::string t = elide(ctx.r, toggle, kMeta, limit - x);
        ctx.r.drawText({x, sy + (kSectionH - meta) / 2.f}, t, kMeta, c.accent);
        tips_.emplace_back(gfx::Rect{x, sy, ctx.r.measure(t, kMeta).width, kSectionH}, "Les fichiers retir\xC3\xA9s de la discussion (ils ne partent plus avec les demandes).");
    }
    // Les fichiers.
    const auto list = shown();
    if (list.empty()) {
        const std::string empty = ctx_.count() == 0 && removed == 0 ? "Aucun fichier joint dans cette discussion."
                                                                    : "Aucun fichier ne correspond.";
        ctx.r.drawText({b.x + 12.f, listTop() + (kEmptyH - meta) / 2.f}, elide(ctx.r, empty, kMeta, b.w - 24.f), kMeta, c.textMuted);
        ctx.r.popClip();
        return;
    }
    const std::size_t first = std::min(static_cast<std::size_t>(scroll_), list.size());
    const std::size_t n = std::min(visibleRows(), list.size() - first);
    for (std::size_t i = first; i < first + n; ++i) {
        const auto& f = *list[i];
        const gfx::Rect row = rowRect(i);
        const bool hot = static_cast<int>(i) == hover_;
        if (hot) ctx.r.fillRoundedRect(row, ctx.theme.brand.hover.a ? ctx.theme.brand.hover : alpha(c.selectionBg, 60), 4.f);
        const gfx::Color ink = f.attachment.removed ? c.textDisabled : c.text;
        ui::drawIcon(ctx.r, iconFor(f.attachment.ext), {row.x + 6.f, row.y + 4.f, 16.f, 16.f},
                     f.availability == attach::Availability::Missing && !f.attachment.removed ? c.error : c.textMuted);
        // a droite : les puces d'etat, ou les gestes au survol
        float right = row.right() - 8.f;
        if (hot) {
            for (const Action a : kActions) {
                const gfx::Rect r = actionRect(i, a);
                ctx.r.fillRoundedRect(r, alpha(c.info, 30), 4.f);
                ui::drawIcon(ctx.r, actionIcon(a, f.attachment.removed), {r.x + 3.f, r.y + 3.f, r.w - 6.f, r.h - 6.f},
                             a == Action::Remove && !f.attachment.removed ? c.error : c.text);
                tips_.emplace_back(r, actionTip(a, f.attachment.removed));
            }
            right = actionRect(i, Action::Open).x - 6.f;
        } else {
            std::vector<std::pair<std::string, int>> looks;
            if (!f.attachment.removed) looks.push_back(analysisLook(f));
            looks.push_back(availabilityLook(f));   // une piece retiree : << Retire >> seul
            for (const auto& look : looks) {
                const gfx::Color tone = toneColor(ctx.theme, look.second);
                const float cw = ctx.r.measure(look.first, kMeta).width + 14.f;
                if (right - cw < row.x + 120.f) break;
                const gfx::Rect chip{right - cw, row.y + 3.f, cw, 18.f};
                ctx.r.fillRoundedRect(chip, alpha(tone, 40), 9.f);
                ctx.r.drawText({chip.x + 7.f, chip.y + (chip.h - meta) / 2.f}, look.first, kMeta, tone);
                right = chip.x - 6.f;
            }
        }
        // le nom, puis le detail
        const float nx = row.x + 28.f;
        const std::string name = elide(ctx.r, f.attachment.name, kBody, right - nx);
        ctx.r.drawText({nx, row.y + 3.f}, name, kBody, ink);
        std::string detail = f.kindLabel + " \xC2\xB7 " + f.detail();
        ctx.r.drawText({nx, row.y + 20.f}, elide(ctx.r, detail, kMeta, row.right() - 8.f - nx), kMeta, c.textMuted);
        // l'infobulle : tout
        std::string tip = f.attachment.name + "\n" + f.kindLabel + " \xC2\xB7 " + f.detail();
        if (!f.path.empty() && f.availability == attach::Availability::Moved) tip += "\nRetrouv\xC3\xA9 ici : " + f.path;
        if (!f.attachment.analyses.empty()) {
            const auto& an = f.attachment.analyses.back();
            if (!an.error.empty()) tip += "\n\xC3\x89" "chec : " + an.error;
            for (const auto& d : an.details) tip += "\n\xE2\x80\xA2 " + d;
        }
        if (f.textMissing) tip += "\nLe texte extrait a \xC3\xA9t\xC3\xA9 effac\xC3\xA9 avec le cache : il sera relu depuis l'original.";
        tip += "\nClic : le d\xC3\xA9tail ; double-clic : ouvrir.";
        tips_.emplace_back(gfx::Rect{row.x, row.y, right - row.x, row.h}, tip);
    }
    if (list.size() > n) {
        const std::string more = std::to_string(first + 1) + "\xE2\x80\x93" + std::to_string(first + n) + " sur " + std::to_string(list.size());
        tips_.emplace_back(gfx::Rect{b.x, listTop(), 6.f, b.bottom() - listTop()}, "La molette fait d\xC3\xA9" "filer les fichiers (" + more + ").");
        // une barre discrete a droite
        const float trackH = b.bottom() - listTop() - 6.f;
        const float frac = static_cast<float>(n) / static_cast<float>(list.size());
        const float pos = static_cast<float>(first) / static_cast<float>(list.size());
        ctx.r.fillRoundedRect({b.right() - 4.f, listTop() + trackH * pos, 3.f, std::max(12.f, trackH * frac)}, alpha(c.textMuted, 90), 1.5f);
    }
    ctx.r.popClip();
}

void AiContextPane::fire(const std::function<void(const std::string&)>& f, std::size_t index) const {
    const auto list = shown();
    if (f && index < list.size()) f(list[index]->attachment.id);
}

ui::EventResult AiContextPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        int h = -1;
        if (open_ && bounds().contains(m->pos))
            for (std::size_t i = 0; i < shown().size(); ++i)
                if (rowRect(i).contains(m->pos)) h = static_cast<int>(i);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && open_ && bounds().contains(w->pos)) {
        const std::size_t total = shown().size();
        const float maxFirst = static_cast<float>(total > visibleRows() ? total - visibleRows() : 0);
        scroll_ = std::floor(std::clamp(scroll_ - w->dy * 2.f, 0.f, maxFirst));
        invalidate();
        return ui::EventResult::Consumed;
    }
    const auto* d = std::get_if<ui::MouseDown>(&ev);
    if (!d || d->button != ui::MouseButton::Left || !bounds().contains(d->pos)) return ui::EventResult::Ignored;
    if (headRect().contains(d->pos)) {
        setExpanded(!open_);
        return ui::EventResult::Consumed;
    }
    if (!open_) return ui::EventResult::Consumed;
    // << Voir les retires >>
    if (d->pos.y < listTop() && ctx_.files.size() > ctx_.count()) {
        if (!searchBox_ || !searchVisible() || d->pos.x < searchBox_->bounds().x) setShowRemoved(!showRemoved_);
        return ui::EventResult::Consumed;
    }
    const auto list = shown();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!rowRect(i).contains(d->pos)) continue;
        const bool removed = list[i]->attachment.removed;
        for (const Action a : kActions) {
            if (!actionRect(i, a).contains(d->pos)) continue;
            switch (a) {
                case Action::Open: fire(cb_.open, i); break;
                case Action::Details: fire(cb_.details, i); break;
                case Action::Analyses: fire(cb_.analyses, i); break;
                case Action::Remove: fire(removed ? cb_.restore : cb_.remove, i); break;
            }
            return ui::EventResult::Consumed;
        }
        if (d->clickCount >= 2) fire(cb_.open, i);
        else fire(cb_.details, i);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Consumed;
}

} // namespace app::ai
