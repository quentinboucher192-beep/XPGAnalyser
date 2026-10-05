#include "HelpView.hpp"
#include "../NoveltyMarks.hpp"      // 1.10 (H) : les sections des nouveautes, en orange

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

// Les hauteurs, en unites de la metrique du theme. Un document dont les marges
// ne suivent pas le rythme du reste se voit tout de suite : il a l'air colle.
constexpr float kPadding = 16.f;
constexpr float kBulletIndent = 20.f;

// De combien on se pose AU-DESSUS d'un titre en sautant dessus, et de combien
// currentSection tolere qu'un titre depasse le haut de la vue.
//
// C'EST LA MEME VALEUR, ET CE N'EST PAS UN HASARD. Apres un saut, le titre vise
// se trouve exactement a cette distance sous le bord : si la tolerance etait
// plus petite, le titre ou l'on vient d'arriver ne serait pas reconnu comme le
// titre courant, et l'en-tete afficherait le precedent.
constexpr float kAnchorMargin = 8.f;

float headingSize(const Theme& t, int level) {
    switch (level) {
        case 1:  return t.metric.rowHeight * 1.6f;
        case 2:  return t.metric.rowHeight * 1.3f;
        default: return t.metric.rowHeight * 1.1f;
    }
}

// Le projet n'a pas de fonte de titre : il a ui, uiBold, mono et smallUi. Un
// titre se distingue donc par le GRAS et la couleur, pas par la taille - la
// police 8x8 ne connaissait que des multiples de huit, et l'habitude est
// restee. Avec une vraie fonte, une taille de titre serait le bon choix.
gfx::FontId headingFont(const Theme& t, int) { return t.font.uiBold; }

// Replie et dessine, comme les autres widgets de ce projet.
void drawClipped(const PaintContext& ctx, gfx::Point at, std::string_view text,
                 gfx::FontId font, gfx::Color colour, float maxWidth) {
    if (maxWidth <= 0.f || text.empty()) return;
    const auto n = ctx.r.fitCharacters(text, font, maxWidth);
    ctx.r.drawText(at, text.substr(0, n), font, colour);
}

// Replie un texte sur la largeur donnee, en coupant entre les mots.
//
// LA COUPE TOMBE SUR UN ESPACE, ET JAMAIS AU MILIEU D'UN MOT. Un mot plus long
// que la ligne est pourtant coupe de force : sinon il deborderait indefiniment,
// et une adresse ou un nom de bloc sans espace le ferait a chaque fois.
std::vector<std::string> wrap(gfx::IRenderer& r, std::string_view text,
                              gfx::FontId font, float width) {
    std::vector<std::string> lines;
    if (text.empty()) { lines.emplace_back(); return lines; }
    if (width <= 1.f) { lines.emplace_back(text); return lines; }

    std::string current;
    std::size_t at = 0;
    while (at < text.size()) {
        // Le mot suivant, avec l'espace qui le precede.
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos) end = text.size();
        const auto word = text.substr(at, end - at);

        std::string candidate = current.empty() ? std::string(word)
                                                : current + " " + std::string(word);
        if (r.measure(candidate, font).width <= width || current.empty()) {
            if (r.measure(candidate, font).width > width && current.empty()) {
                // Un seul mot plus large que la ligne : on le coupe de force,
                // sur une frontiere de caractere que le renderer connait.
                const auto keep = r.fitCharacters(word, font, width);
                if (keep > 0 && keep < word.size()) {
                    lines.emplace_back(word.substr(0, keep));
                    at += keep;
                    continue;
                }
            }
            current = std::move(candidate);
        } else {
            lines.push_back(current);
            current = std::string(word);
        }
        at = end + 1;
    }
    if (!current.empty() || lines.empty()) lines.push_back(current);
    return lines;
}

} // namespace

HelpView::HelpView(std::string id) : Widget(std::move(id)) {}

void HelpView::setDocument(std::shared_ptr<const HelpDocument> doc) {
    doc_ = std::move(doc);
    laid_.clear();
    laidWidth_ = -1.f;      // force le recalcul
    scrollY_ = 0.f;
    invalidate();
}

float HelpView::columnWidth(float total, std::size_t cells, std::size_t at) {
    if (cells == 0) return total;
    // LA DERNIERE COLONNE PREND LE RESTE. Les tableaux de l'aide finissent par
    // une explication, qui est toujours la plus longue : lui donner la meme
    // part qu'a "oui" gacherait la moitie de la largeur.
    if (cells == 1) return total;
    const float petite = std::min(140.f, total / static_cast<float>(cells + 1));
    if (at + 1 < cells) return petite;
    return total - petite * static_cast<float>(cells - 1);
}

void HelpView::relayout(gfx::IRenderer& r, const Theme& theme, float width) {
    const float inner = std::max(40.f, width - kPadding * 2.f);
    if (doc_ && laidWidth_ == inner && laidBlocks_ == doc_->blocks().size()) return;

    laid_.clear();
    contentHeight_ = 0.f;
    laidWidth_ = inner;
    laidBlocks_ = doc_ ? doc_->blocks().size() : 0;
    if (!doc_) return;

    const float line = r.lineHeight(theme.font.ui);
    float y = kPadding;

    for (std::size_t i = 0; i < doc_->blocks().size(); ++i) {
        const auto& b = doc_->blocks()[i];
        LaidOutBlock lb;
        lb.index = i;
        lb.y = y;

        switch (b.kind) {
            case BlockKind::Heading: {
                const float h = headingSize(theme, b.level);
                lb.lines = {b.text};
                // Un titre respire AVANT, pas apres : il appartient a ce qui le
                // suit, et un espace apres l'en detacherait.
                lb.height = h + (i == 0 ? 0.f : theme.metric.rowHeight * 0.8f);
                break;
            }
            case BlockKind::Paragraph:
                lb.lines = wrap(r, b.text, theme.font.ui, inner);
                lb.height = static_cast<float>(lb.lines.size()) * line + 8.f;
                break;
            case BlockKind::Bullet:
                lb.lines = wrap(r, b.text, theme.font.ui, inner - kBulletIndent);
                lb.height = static_cast<float>(lb.lines.size()) * line + 4.f;
                break;
            case BlockKind::Code: {
                // Le code ne se replie PAS : un exemple de fichier replie n'est
                // plus un exemple de fichier. Il defilera horizontalement.
                lb.lines.clear();
                std::size_t from = 0;
                while (from <= b.text.size()) {
                    auto nl = b.text.find('\n', from);
                    if (nl == std::string::npos) nl = b.text.size();
                    lb.lines.push_back(b.text.substr(from, nl - from));
                    if (nl == b.text.size()) break;
                    from = nl + 1;
                }
                lb.height = static_cast<float>(lb.lines.size())
                          * r.lineHeight(theme.font.mono) + 16.f;
                break;
            }
            case BlockKind::TableRow: {
                // Chaque cellule est repliee dans SA colonne, et la ligne prend
                // la hauteur de la plus haute.
                std::size_t plusHaute = 1;
                for (std::size_t c = 0; c < b.cells.size(); ++c) {
                    const float w = columnWidth(inner, b.cells.size(), c) - 8.f;
                    plusHaute = std::max(plusHaute,
                                         wrap(r, b.cells[c], theme.font.ui, w).size());
                }
                lb.height = static_cast<float>(plusHaute) * line + 8.f;
                break;
            }
            case BlockKind::Separator:
                lb.height = theme.metric.rowHeight;
                break;
        }

        y += lb.height;
        laid_.push_back(std::move(lb));
    }
    contentHeight_ = y + kPadding;
}

bool HelpView::goToAnchor(std::string_view anchor) {
    if (!doc_) return false;
    const auto block = doc_->blockOf(anchor);
    if (block >= doc_->blocks().size()) return false;      // lien casse
    if (block >= laid_.size()) {                           // pas encore dispose
        // ---- Lot API 8 : sessions de capture ---- F1 ouvre l'aide avant son premier
        // dessin : l'ancre attend la disposition (onPaint), ce n'est pas un lien casse.
        pendingAnchor_ = std::string(anchor);
        invalidate();
        return true;
    }

    // On se pose JUSTE AU-DESSUS du titre, pas dessus : un titre colle au bord
    // superieur donne l'impression d'avoir rate le debut.
    setScroll(laid_[block].y - kAnchorMargin);
    invalidate();
    return true;
}

void HelpView::setScroll(float y) {
    const float maxi = std::max(0.f, contentHeight_ - bounds().h);
    scrollY_ = std::clamp(y, 0.f, maxi);
}

std::string HelpView::currentSection() const {
    if (!doc_ || laid_.empty()) return {};
    // Le dernier titre AU-DESSUS du haut de la vue. Prendre le premier visible
    // laisserait l'en-tete vide tant qu'on lit le corps d'une section.
    std::string titre;
    for (const auto& lb : laid_) {
        if (lb.y > scrollY_ + kAnchorMargin + 1.f) break;
        const auto& b = doc_->blocks()[lb.index];
        if (b.kind == BlockKind::Heading) titre = b.text;
    }
    return titre;
}

void HelpView::setHighlight(std::string term) {
    highlight_ = std::move(term);
    invalidate();
}

SizeHint HelpView::sizeHint() const {
    return SizeHint{{560.f, 400.f}, {280.f, 120.f}, 1.f, 1.f};
}

void HelpView::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto area = contentRect();
    ctx.r.fillRect(bounds(), c.panelBg);

    relayout(ctx.r, ctx.theme, area.w);
    // ---- Lot API 8 : sessions de capture ---- l'ancre demandee avant la disposition.
    if (!pendingAnchor_.empty() && doc_) {
        const auto block = doc_->blockOf(pendingAnchor_);
        pendingAnchor_.clear();
        if (block < laid_.size()) scrollY_ = laid_[block].y - kAnchorMargin;
    }
    setScroll(scrollY_);      // la hauteur a pu changer, le defilement se reborne
    if (!doc_) return;

    const float line = ctx.r.lineHeight(ctx.theme.font.ui);

    for (const auto& lb : laid_) {
        const float top = area.y + lb.y - scrollY_;
        // Hors de la vue : rien a peindre. Un document de deux cents blocs les
        // dessinerait tous a chaque trame sans ce test.
        if (top + lb.height < area.y) continue;
        if (top > area.bottom()) break;

        const auto& b = doc_->blocks()[lb.index];
        const float x = area.x + kPadding;

        switch (b.kind) {
            case BlockKind::Heading: {
                const float h = headingSize(ctx.theme, b.level);
                const float baseY = top + lb.height - h;
                ctx.r.drawText({x, baseY}, b.text, headingFont(ctx.theme, b.level),
                               b.level == 1 ? c.accent : c.text);
                if (b.level <= 2)
                    ctx.r.fillRect({x, baseY + h - 2.f, area.w - kPadding * 2.f, 1.f},
                                   c.border);
                break;
            }
            case BlockKind::Paragraph:
                for (std::size_t i = 0; i < lb.lines.size(); ++i)
                    ctx.r.drawText({x, top + static_cast<float>(i) * line},
                                   lb.lines[i], ctx.theme.font.ui, c.text);
                break;
            case BlockKind::Bullet:
                ctx.r.fillRect({x + 4.f, top + line * 0.45f, 4.f, 4.f}, c.textMuted);
                for (std::size_t i = 0; i < lb.lines.size(); ++i)
                    ctx.r.drawText({x + kBulletIndent, top + static_cast<float>(i) * line},
                                   lb.lines[i], ctx.theme.font.ui, c.text);
                break;
            case BlockKind::Code: {
                const float mono = ctx.r.lineHeight(ctx.theme.font.mono);
                ctx.r.fillRect({x, top, area.w - kPadding * 2.f, lb.height - 8.f},
                               c.inputBg);
                for (std::size_t i = 0; i < lb.lines.size(); ++i)
                    ctx.r.drawText({x + 8.f, top + 8.f + static_cast<float>(i) * mono},
                                   lb.lines[i], ctx.theme.font.mono, c.textMuted);
                break;
            }
            case BlockKind::TableRow: {
                if (b.header)
                    ctx.r.fillRect({x, top, area.w - kPadding * 2.f, lb.height},
                                   c.headerBg);
                float cx = x;
                const float inner = area.w - kPadding * 2.f;
                for (std::size_t i = 0; i < b.cells.size(); ++i) {
                    const float w = columnWidth(inner, b.cells.size(), i);
                    drawClipped(ctx, {cx + 4.f, top + 4.f}, b.cells[i],
                                ctx.theme.font.ui,
                                b.header ? c.textInverted : c.text, w - 8.f);
                    cx += w;
                }
                ctx.r.fillRect({x, top + lb.height - 1.f, inner, 1.f}, c.gridLine);
                break;
            }
            case BlockKind::Separator:
                ctx.r.fillRect({x, top + lb.height * 0.5f,
                                area.w - kPadding * 2.f, 1.f}, c.borderStrong);
                break;
        }
    }

    // 1.10 (H) : LES SECTIONS DES NOUVEAUTES EN ORANGE. Un titre dont l'ancre est
    // "nouveautes-1-9" ouvre la section des nouveautes de la 1.9 ; jusqu'au titre
    // suivant de meme niveau, elle est encadree en orange avec son etiquette
    // ("NOUVEAU . 1.9") tant que le lecteur ne l'a pas vue (comme l'aide de l'IHM).
    ctx.r.pushClip(area);
    for (std::size_t i = 0; i < laid_.size(); ++i) {
        const auto& head = doc_->blocks()[laid_[i].index];
        if (head.kind != BlockKind::Heading || head.anchor.rfind("nouveautes-", 0) != 0) continue;
        std::string since = head.anchor.substr(11);
        std::replace(since.begin(), since.end(), '-', '.');
        const std::string label = novelty::sinceLabel(since);
        if (label.empty()) continue;
        std::size_t end = i + 1;
        while (end < laid_.size()) {
            const auto& b = doc_->blocks()[laid_[end].index];
            if (b.kind == BlockKind::Heading && b.level <= head.level) break;
            ++end;
        }
        const auto& last = laid_[end - 1];
        const float y0 = area.y + laid_[i].y - scrollY_ + (i == 0 ? 0.f : ctx.theme.metric.rowHeight * 0.8f) - 6.f;
        const float y1 = area.y + last.y + last.height - scrollY_ + 2.f;
        const gfx::Rect frame{area.x + kPadding - 8.f, y0, area.w - kPadding * 2.f + 16.f, std::max(0.f, y1 - y0)};
        if (frame.bottom() < area.y || frame.y > area.bottom()) continue;
        novelty::drawFrame(ctx.r, ctx.theme, frame);
        const auto ps = novelty::pillSize(ctx.r, ctx.theme, label);
        novelty::drawPill(ctx.r, ctx.theme, {frame.right() - ps.w - 4.f, frame.y - ps.h / 2.f - 3.f}, label);
    }
    ctx.r.popClip();
}

EventResult HelpView::onEvent(const InputEvent& ev) {
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        setScroll(scrollY_ - w->dy * 48.f);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (bounds().contains(d->pos)) grabFocus();
        return EventResult::Ignored;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        // La page vaut la vue MOINS une ligne : sans ce recouvrement on saute
        // par-dessus une ligne a chaque page, et c'est toujours celle qu'on
        // cherchait.
        const float page = std::max(40.f, bounds().h - 40.f);
        switch (k->key) {
            case Key::PageDown: setScroll(scrollY_ + page); break;
            case Key::PageUp:   setScroll(scrollY_ - page); break;
            case Key::Home:     setScroll(0.f);             break;
            case Key::End:      setScroll(contentHeight_);  break;
            case Key::Down:     setScroll(scrollY_ + 40.f); break;
            case Key::Up:       setScroll(scrollY_ - 40.f); break;
            default: return EventResult::Ignored;
        }
        invalidate();
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

} // namespace ui
