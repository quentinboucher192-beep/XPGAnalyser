// =============================================================================
//  ui/widgets/Controls.cpp
// =============================================================================
#include "Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ui {
    namespace {

        // sizeHint() runs outside a paint pass and has no Theme to consult. These must
        // match ui::Theme::Fonts; they are the only duplication of that fact, and both
        // sides are one line long.
        constexpr gfx::FontId kFontUi{ 16 };
        constexpr gfx::FontId kFontMono{ 16 };

        constexpr bool identChar(char c) noexcept {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9') || c == '_';
        }


        // UTF-8 aware caret movement: never land inside a multi-byte sequence.
        bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

        std::size_t prevCodepoint(const std::string& s, std::size_t i) {
            if (i == 0) return 0;
            --i;
            while (i > 0 && isContinuation(s[i])) --i;
            return i;
        }
        std::size_t nextCodepoint(const std::string& s, std::size_t i) {
            if (i >= s.size()) return s.size();
            ++i;
            while (i < s.size() && isContinuation(s[i])) ++i;
            return i;
        }

        void drawFocusRing(const PaintContext& ctx, const gfx::Rect& r) {
            ctx.r.strokeRect(r.inset(1.f, 1.f), ctx.theme.color.accent, 1.f);
        }

        // 1.10 (chantier K) : l'aide a la saisie selon le type du champ. Une
        // proposition qui insere une couleur (#RRGGBB, entre quotes ou non) montre
        // sa pastille ; celle dont le detail dit "ne convient pas" (le mauvais
        // type : HmiAssist, arguments des popups) est grisee, sous un trait.
        int hexDigit(char ch) noexcept {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        }
        bool suggestionColor(const InputText::Suggestion& s, gfx::Color& out) {
            std::string_view v = s.insert.empty() ? std::string_view(s.text) : std::string_view(s.insert);
            if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
            if ((v.size() != 7 && v.size() != 9) || v.front() != '#') return false;
            std::uint8_t ch[4]{ 0, 0, 0, 255 };
            for (std::size_t i = 1, k = 0; i < v.size(); i += 2, ++k) {
                const int hi = hexDigit(v[i]), lo = hexDigit(v[i + 1]);
                if (hi < 0 || lo < 0) return false;
                ch[k] = static_cast<std::uint8_t>(hi * 16 + lo);
            }
            out = { ch[0], ch[1], ch[2], ch[3] };
            return true;
        }
        bool suggestionUnfit(const InputText::Suggestion& s) noexcept {
            return s.detail.find("ne convient pas") != std::string::npos;
        }

        // 1.11.2 (API-V, decisions 161 et 163) : le pied de la liste, comme la scene 4 de la
        // maquette de MQ5 : « La couleur dit d'ou ca vient : » et une pastille par
        // provenance (la cle posee par l'appli) ; a droite, ce qu'est la ligne choisie
        // (son icone et ses mots : « methode · objet »). Etroit : la cle cede la place.
        void drawBadgeLegend(gfx::IRenderer& r, const Theme& theme, const KindBadge& b, const gfx::Rect& strip) {
            const auto& c = theme.color;
            r.line({ strip.x + 4.f, strip.y }, { strip.right() - 4.f, strip.y }, c.border, 1.f);
            const auto f = theme.font.smallUi;
            const float ty = strip.y + (strip.h - r.lineHeight(f)) * 0.5f;
            const float side = std::min(strip.h - 6.f, 12.f);
            const float right = side + 6.f + r.measure(b.legend, f).width;
            static constexpr std::string_view kHead = "La couleur dit d'o\xC3\xB9" " \xC3\xA7" "a vient :";
            const std::size_t n = badgeKeyCount();
            const BadgeKeyEntry* key = badgeKey();
            float keyW = 0.f;
            for (std::size_t i = 0; i < n; ++i) keyW += 10.f + 4.f + r.measure(key[i].label, f).width + 10.f;
            const float headW = r.measure(kHead, f).width + 10.f;
            const float room = strip.w - 12.f - right - 12.f;
            float x = strip.x + 8.f;
            if (n > 0 && keyW <= room) {
                if (headW + keyW <= room) {
                    r.drawText({ x, ty }, kHead, f, c.textMuted);
                    x += headW;
                }
                for (std::size_t i = 0; i < n; ++i) {
                    const float d = 8.f;
                    r.fillRoundedRect({ x + 1.f, strip.y + (strip.h - d) * 0.5f, d, d }, key[i].color, d * 0.5f);
                    x += 10.f + 4.f;
                    r.drawText({ x, ty }, key[i].label, f, c.textMuted);
                    x += r.measure(key[i].label, f).width + 10.f;
                }
            }
            // Ce qu'est la ligne choisie : a droite (seule quand la cle ne tient pas).
            const float rx = std::max(x, strip.right() - 8.f - right);
            drawKindBadge(r, b, { rx, strip.y + (strip.h - side) * 0.5f, side, side });
            r.drawText({ rx + side + 6.f, ty }, b.legend, f, c.textMuted);
        }

        const BadgeKeyEntry* gBadgeKey = nullptr;
        std::size_t gBadgeKeyCount = 0;

    } // namespace

    // ============================================================ KindBadge ====
    // 1.11.2 (API-V, decisions 161 et 163) : le pictogramme de la nature, au trait,
    // dans la couleur de la provenance (la scene 4 de la maquette : 16 x 16, un
    // trait de 1,3, des points pleins).
    void setBadgeKey(const BadgeKeyEntry* entries, std::size_t count) noexcept {
        gBadgeKey = entries;
        gBadgeKeyCount = entries ? count : 0;
    }
    std::size_t badgeKeyCount() noexcept { return gBadgeKeyCount; }
    const BadgeKeyEntry* badgeKey() noexcept { return gBadgeKey; }

    void drawKindBadge(gfx::IRenderer& r, const KindBadge& b, const gfx::Rect& box) {
        if (!b.set()) return;
        const float side = std::min(box.w, box.h);
        const float s = side / 16.f;
        const float ox = box.x + (box.w - side) * 0.5f, oy = box.y + (box.h - side) * 0.5f;
        const float t = std::max(1.f, 1.3f * s);
        const auto at = [&](float x, float y) { return gfx::Point{ ox + x * s, oy + y * s }; };
        const auto& p = *b.picto;
        for (std::size_t i = 0; i < p.segCount; ++i) {
            const auto& g = p.segs[i];
            r.line(at(g.x0, g.y0), at(g.x1, g.y1), b.color, t);
        }
        for (std::size_t i = 0; i < p.dotCount; ++i) {
            const auto& d = p.dots[i];
            const float rr = d.r * s;
            switch (d.kind) {
                case KindBadge::DotKind::Disc:
                    r.fillRoundedRect({ ox + d.x * s - rr, oy + d.y * s - rr, 2.f * rr, 2.f * rr }, b.color, rr);
                    break;
                case KindBadge::DotKind::Square:
                    r.fillRect({ ox + d.x * s - rr, oy + d.y * s - rr, 2.f * rr, 2.f * rr }, b.color);
                    break;
                case KindBadge::DotKind::Ring: {
                    constexpr int kSteps = 16;
                    gfx::Point prev = at(d.x + d.r, d.y);
                    for (int k = 1; k <= kSteps; ++k) {
                        const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(kSteps);
                        const gfx::Point next = at(d.x + d.r * std::cos(a), d.y + d.r * std::sin(a));
                        r.line(prev, next, b.color, t);
                        prev = next;
                    }
                    break;
                }
            }
        }
    }

    // ================================================================ Button ====
    Button::Button(std::string text, std::string id)
        : Widget(std::move(id)), text_(std::move(text)) {
        setFocusPolicy(true);
        setPadding({ 4.f, 10.f, 4.f, 10.f });
    }

    void Button::setText(std::string t) { text_ = std::move(t); invalidateLayout(); }

    void Button::setCompact(bool compact) {
        if (compact_ == compact) return;
        compact_ = compact;
        invalidateLayout();
    }

    SizeHint Button::sizeHint() const {
        // Measured, not estimated. The previous constant (7.2 px per character) was
        // inherited from a proportional face; with the 16 px fixed-pitch face in use
        // the label was twice as wide as the button reserved, so adjacent buttons
        // ran into each other.
        const float line = lineHeight(kFontUi);
        const float icon = icon_ == Icon::None ? 0.f : line + 6.f;
        SizeHint h;
        if (compact()) {
            // Square, just the icon. The label lives in the tooltip.
            h.preferred = { line + 16.f, line + 12.f };
            h.minimum = h.preferred;
            return h;
        }
        const float text = measureWidth(text_, kFontUi);
        h.preferred = { text + icon + 24.f, line + 12.f };
        h.minimum = { std::min(text, 40.f) + 16.f, line + 8.f };
        return h;
    }

    void Button::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();

        gfx::Color fill = c.panelBg;
        gfx::Color text = enabled() ? c.text : c.textDisabled;
        switch (style_) {
        case Style::Primary: fill = pressed_ ? c.accentPressed : hovered() ? c.accentHover : c.accent;
            text = c.textInverted; break;
        case Style::Danger:  fill = c.error; text = c.textInverted; break;
        case Style::Flat:    fill = hovered() ? c.headerBg : gfx::Color{ 0, 0, 0, 0 }; break;
        case Style::Default: fill = pressed_ ? c.headerBg : hovered() ? c.rowAltBg : c.panelBg; break;
        }
        if (!enabled()) { fill = c.panelBg; text = c.textDisabled; }

        if (fill.a > 0) ctx.r.fillRoundedRect(r, fill, ctx.theme.metric.radius);
        if (style_ == Style::Default) ctx.r.strokeRect(r, c.border, 1.f);
        if (focused()) drawFocusRing(ctx, r);

        const float iconSide = icon_ == Icon::None ? 0.f : std::min(r.h - 8.f, 18.f);

        if (compact()) {
            drawIcon(ctx.r, icon_,
                { r.x + (r.w - iconSide) * 0.5f, r.y + (r.h - iconSide) * 0.5f,
                 iconSide, iconSide }, text);
            return;
        }

        const auto  m = ctx.r.measure(text_, ctx.theme.font.ui);
        const float total = m.width + (iconSide > 0.f ? iconSide + 6.f : 0.f);
        float x = r.x + (r.w - total) * 0.5f;

        if (iconSide > 0.f) {
            drawIcon(ctx.r, icon_, { x, r.y + (r.h - iconSide) * 0.5f, iconSide, iconSide }, text);
            x += iconSide + 6.f;
        }
        ctx.r.drawText({ x, r.y + (r.h - m.height) * 0.5f }, text_, ctx.theme.font.ui, text);
    }

    EventResult Button::onEvent(const InputEvent& ev) {
        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (d->button == MouseButton::Left && bounds().contains(d->pos)) {
                pressed_ = true;
                grabFocus();
                invalidate();
                return EventResult::Consumed;
            }
        }
        else if (const auto* u = std::get_if<MouseUp>(&ev)) {
            if (pressed_ && u->button == MouseButton::Left) {
                pressed_ = false;
                invalidate();
                if (bounds().contains(u->pos)) clicked->emit();
                return EventResult::Consumed;
            }
        }
        else if (const auto* k = std::get_if<KeyDown>(&ev)) {
            if (focused() && (k->key == Key::Return || k->key == Key::Space)) {
                clicked->emit();
                return EventResult::Consumed;
            }
        }
        return EventResult::Ignored;
    }

    // ============================================================= InputText ====
    InputText::InputText(std::string id) : Widget(std::move(id)) {
        setFocusPolicy(true);
        setPadding({ 3.f, 6.f, 3.f, 6.f });
    }

    void InputText::setText(std::string t) {
        if (t == text_) return;
        text_ = std::move(t);
        caret_ = selAnchor_ = text_.size();
        invalidate();
        textChanged->emit(text_);
    }

    SizeHint InputText::sizeHint() const {
        SizeHint h;
        // Fixed height, elastic width. Without this the default hint claims
        // stretchY = 1 and a text field in a vertical box swells to fill the whole
        // dialog — which is exactly what the Open-project box did.
        h.preferred = { 240.f, lineHeight(kFontUi) + 10.f };
        h.minimum = { 80.f, lineHeight(kFontUi) + 8.f };
        h.stretchX = 1.f;
        h.stretchY = 0.f;
        return h;
    }

    void InputText::onFocusChanged(bool gained) {
        if (gained) undoText_ = text_;
        if (!gained) {
            suggestions_.clear();
            editingDone->emit(text_);
        }
        invalidate();
    }

    // ------------------------------------------------------- aide a la saisie ---
    void InputText::updateSuggestions(bool forced) {
        if (!assist_ || readOnly_ || masked_ || !focused()) { closeSuggestions(); return; }
        std::vector<Suggestion> found;
        std::size_t from = caret_;
        assist_(std::string_view(text_).substr(0, caret_), from, found);
        // Le mot tape est DEJA un nom de la liste (a la casse pres) : elle se
        // ferme. Elle ne sert qu'a finir un mot ; ouverte sur un nom complet,
        // elle cacherait le champ du dessous et prendrait son clic. Ctrl+Espace
        // la montre quand meme.
        if (!forced && from <= caret_) {
            const std::string typed = text_.substr(from, caret_ - from);
            for (const auto& sug : found)
                if ((sug.insert.empty() ? sug.text : sug.insert) == typed) { found.clear(); break; }
        }
        // Ce qui est tape est deja un nom de la liste : il passe en tete, pour
        // qu'Entree le garde tel quel au lieu de prendre un nom plus long.
        if (from <= caret_) {
            const std::string typed = text_.substr(from, caret_ - from);
            auto same = [&](const Suggestion& s) {
                const std::string& put = s.insert.empty() ? s.text : s.insert;
                if (put.size() != typed.size()) return false;
                for (std::size_t i = 0; i < put.size(); ++i)
                    if (std::tolower(static_cast<unsigned char>(put[i])) != std::tolower(static_cast<unsigned char>(typed[i])))
                        return false;
                return true;
            };
            if (const auto it = std::find_if(found.begin(), found.end(), same); it != found.end() && it != found.begin())
                std::rotate(found.begin(), it, it + 1);
        }
        if (found.size() > 200) found.resize(200);
        suggestions_ = std::move(found);
        suggestionFrom_ = std::min(from, caret_);
        suggestionIndex_ = 0;
        suggestionFirst_ = 0;
        invalidate();
    }

    void InputText::openSuggestions() { updateSuggestions(true); }

    void InputText::closeSuggestions() {
        if (suggestions_.empty()) return;
        suggestions_.clear();
        invalidate();
    }

    bool InputText::acceptSuggestion(std::size_t index) {
        if (index >= suggestions_.size()) return false;
        const auto chosen = suggestions_[index];
        suggestions_.clear();
        const std::string& put = chosen.insert.empty() ? chosen.text : chosen.insert;
        const auto from = std::min(suggestionFrom_, caret_);
        std::string candidate = text_;
        candidate.replace(from, caret_ - from, put);
        if (candidate.size() > maxLength_) return false;
        if (validator_ && !validator_(candidate)) return false;
        text_ = std::move(candidate);
        caret_ = selAnchor_ = from + put.size();
        invalidate();
        textChanged->emit(text_);
        if (chosen.chain) updateSuggestions(false);
        return true;
    }

    gfx::Rect InputText::eventBounds() const {
        if (suggestions_.empty()) return bounds();
        const auto b = bounds();
        const auto& s = suggestionBox_;
        const float x0 = std::min(b.x, s.x), y0 = std::min(b.y, s.y);
        const float x1 = std::max(b.right(), s.right()), y1 = std::max(b.bottom(), s.bottom());
        return { x0, y0, x1 - x0, y1 - y0 };
    }

    void InputText::onPaintOverlay(const PaintContext& ctx) {
        if (suggestions_.empty()) return;
        const auto& c = ctx.theme.color;
        const auto  f = ctx.theme.font.ui;
        const float rowH = ctx.r.lineHeight(f) + 6.f;
        constexpr std::size_t kRows = 10;
        const std::size_t shownRows = std::min(kRows, suggestions_.size());
        // 1.11.2 (API-V, decision 161) : une ligne de legende au pied, des qu'une proposition a sa pastille.
        const bool badges = std::any_of(suggestions_.begin(), suggestions_.end(), [](const Suggestion& s) { return s.badge.set(); });
        suggestionLegendH_ = badges ? ctx.r.lineHeight(ctx.theme.font.smallUi) + 8.f : 0.f;

        float width = std::max(bounds().w, 220.f);
        for (const auto& item : suggestions_) {
            gfx::Color swatch{};
            const float w = ctx.r.measure(item.text, f).width + ctx.r.measure(item.detail, ctx.theme.font.smallUi).width
                + (item.icon != Icon::None || item.badge.set() || suggestionColor(item, swatch) ? 26.f : 8.f) + 30.f;
            width = std::max(width, w);
            if (item.badge.set())
                width = std::max(width, ctx.r.measure(item.badge.legend, ctx.theme.font.smallUi).width + 40.f);
        }
        const auto surface = ctx.r.surfaceSize();
        width = std::min(width, std::max(bounds().w, surface.w > 0.f ? surface.w - 16.f : width));
        const float height = rowH * static_cast<float>(shownRows) + 4.f + suggestionLegendH_;

        // Sous le champ ; au-dessus quand il n'y a pas la place en bas de la fenetre.
        float x = bounds().x;
        float y = bounds().bottom() + 1.f;
        if (surface.h > 0.f && y + height > surface.h) y = std::max(0.f, bounds().y - height - 1.f);
        if (surface.w > 0.f && x + width > surface.w) x = std::max(0.f, surface.w - width - 4.f);
        suggestionBox_ = { x, y, width, height };

        // La ligne choisie reste visible.
        if (suggestionIndex_ < suggestionFirst_) suggestionFirst_ = suggestionIndex_;
        if (suggestionIndex_ >= suggestionFirst_ + shownRows) suggestionFirst_ = suggestionIndex_ + 1 - shownRows;

        ctx.r.fillRect({ x + 2.f, y + 2.f, width, height }, gfx::Color{ 0, 0, 0, 90 });
        ctx.r.fillRect(suggestionBox_, c.panelBg);
        ctx.r.strokeRect(suggestionBox_, c.accent, 1.f);
        for (std::size_t k = 0; k < shownRows; ++k) {
            const std::size_t i = suggestionFirst_ + k;
            if (i >= suggestions_.size()) break;
            const auto& item = suggestions_[i];
            const gfx::Rect row{ x + 2.f, y + 2.f + static_cast<float>(k) * rowH, width - 4.f, rowH };
            if (i == suggestionIndex_) ctx.r.fillRect(row, c.selectionBg);
            // 1.10 (chantier K) : ce qui ne convient pas au champ, grise sous un trait.
            const bool unfit = suggestionUnfit(item);
            if (unfit && i > 0 && !suggestionUnfit(suggestions_[i - 1]))
                ctx.r.line({ row.x + 4.f, row.y }, { row.right() - 4.f, row.y }, c.textDisabled, 1.f);
            float tx = row.x + 6.f;
            gfx::Color swatch{};
            if (item.badge.set()) {
                // 1.11.2 (API-V, decision 161) : la nature et la provenance, tracees.
                drawKindBadge(ctx.r, item.badge, { tx, row.y + (rowH - 14.f) * 0.5f, 14.f, 14.f });
                tx += 20.f;
            } else if (item.icon != Icon::None) {
                drawIcon(ctx.r, item.icon, { tx, row.y + (rowH - 14.f) * 0.5f, 14.f, 14.f }, unfit ? c.textDisabled : c.textMuted);
                tx += 20.f;
            } else if (suggestionColor(item, swatch)) {
                // 1.10 (chantier K) : une couleur montre sa pastille.
                const gfx::Rect sw{ tx, row.y + (rowH - 13.f) * 0.5f, 13.f, 13.f };
                ctx.r.fillRect(sw, swatch);
                ctx.r.strokeRect(sw, c.textMuted, 1.f);
                tx += 20.f;
            }
            const float ty = row.y + (rowH - ctx.r.lineHeight(f)) * 0.5f;
            ctx.r.drawText({ tx, ty }, item.text, f, unfit ? c.textDisabled : item.badge.accent ? item.badge.color : c.text);
            if (!item.detail.empty()) {
                const auto dm = ctx.r.measure(item.detail, ctx.theme.font.smallUi);
                const float dx = std::max(tx + ctx.r.measure(item.text, f).width + 12.f, row.right() - dm.width - 8.f);
                ctx.r.drawText({ dx, row.y + (rowH - ctx.r.lineHeight(ctx.theme.font.smallUi)) * 0.5f },
                    item.detail, ctx.theme.font.smallUi, unfit ? c.textDisabled : c.textMuted);
            }
        }
        // 1.11.2 (API-V, decision 161) : la legende de la ligne choisie, au pied.
        if (suggestionLegendH_ > 0.f && suggestionIndex_ < suggestions_.size() && suggestions_[suggestionIndex_].badge.set())
            drawBadgeLegend(ctx.r, ctx.theme, suggestions_[suggestionIndex_].badge,
                            { x + 2.f, y + height - 2.f - suggestionLegendH_, width - 4.f, suggestionLegendH_ });
        // Plus de lignes que la liste n'en montre : une barre a droite.
        if (suggestions_.size() > shownRows) {
            const float track = height - 4.f - suggestionLegendH_;
            const float thumb = std::max(12.f, track * static_cast<float>(shownRows) / static_cast<float>(suggestions_.size()));
            const float t = static_cast<float>(suggestionFirst_) / static_cast<float>(suggestions_.size() - shownRows);
            ctx.r.fillRoundedRect({ x + width - 6.f, y + 2.f + t * (track - thumb), 4.f, thumb }, c.scrollbar, 2.f);
        }
    }

    void InputText::eraseSelection() {
        if (caret_ == selAnchor_) return;
        const auto lo = std::min(caret_, selAnchor_);
        const auto hi = std::max(caret_, selAnchor_);
        text_.erase(lo, hi - lo);
        caret_ = selAnchor_ = lo;
    }

    void InputText::insert(std::string_view utf8) {
        if (readOnly_) return;
        std::string candidate = text_;
        const auto lo = std::min(caret_, selAnchor_);
        const auto hi = std::max(caret_, selAnchor_);
        candidate.erase(lo, hi - lo);
        candidate.insert(lo, utf8);
        if (candidate.size() > maxLength_) return;
        if (validator_ && !validator_(candidate)) return;

        text_ = std::move(candidate);
        caret_ = selAnchor_ = lo + utf8.size();
        invalidate();
        textChanged->emit(text_);
    }

    std::string InputText::shown(std::size_t upto) const {
        upto = std::min(upto, text_.size());
        if (!masked_) return text_.substr(0, upto);
        std::string dots;
        for (std::size_t i = 0; i < upto; i = nextCodepoint(text_, i)) dots += "\xE2\x80\xA2";   // U+2022
        return dots;
    }

    std::size_t InputText::caretFromLocalX(float localX) const {
        const float target = localX - padding().l + scrollX_;
        std::size_t best = 0;
        float bestDelta = std::abs(target);
        for (std::size_t i = 0; i <= text_.size(); i = nextCodepoint(text_, i)) {
            const float w = measureWidth(shown(i), kFontUi);
            const float d = std::abs(target - w);
            if (d < bestDelta) { bestDelta = d; best = i; }
            if (i == text_.size()) break;
        }
        return best;
    }

    void InputText::copySelection() const {
        if (masked_) return;                       // un mot de passe ne se copie pas
        if (caret_ == selAnchor_) { setClipboardText(text_); return; }
        const auto lo = std::min(caret_, selAnchor_);
        const auto hi = std::max(caret_, selAnchor_);
        setClipboardText(std::string_view(text_).substr(lo, hi - lo));
    }

    void InputText::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();
        const auto  f = ctx.theme.font.ui;

        ctx.r.fillRect(r, enabled() ? c.inputBg : c.panelBg);
        ctx.r.strokeRect(r, focused() ? c.accent : c.border, 1.f);

        const auto inner = contentRect();
        ctx.r.pushClip(inner);

        // Keep the caret in view.
        const float caretX = ctx.r.measure(shown(caret_), f).width;
        if (caretX - scrollX_ > inner.w - 2.f) scrollX_ = caretX - inner.w + 2.f;
        if (caretX - scrollX_ < 0.f)           scrollX_ = caretX;
        // 1.11 (R111, recette T3-6) : un texte plus court que le precedent
        // (setText) ne reste pas decale a gauche : le defilement ne laisse
        // jamais de vide apres la fin du texte (0 quand tout tient).
        const float full = ctx.r.measure(shown(text_.size()), f).width;
        if (scrollX_ > 0.f && full - scrollX_ < inner.w - 2.f) scrollX_ = std::max(0.f, full - inner.w + 2.f);

        const float baseY = inner.y + (inner.h - ctx.r.lineHeight(f)) * 0.5f;

        if (caret_ != selAnchor_ && focused()) {
            const float a = ctx.r.measure(shown(std::min(caret_, selAnchor_)), f).width;
            const float b = ctx.r.measure(shown(std::max(caret_, selAnchor_)), f).width;
            ctx.r.fillRect({ inner.x + a - scrollX_, inner.y, b - a, inner.h }, c.selectionBg);
        }

        if (text_.empty() && !focused())
            ctx.r.drawText({ inner.x, baseY }, placeholder_, f, c.textDisabled);
        else
            ctx.r.drawText({ inner.x - scrollX_, baseY }, shown(text_.size()), f, enabled() ? c.text : c.textDisabled);

        if (focused() && std::fmod(ctx.time, 1.0) < 0.5)
            ctx.r.fillRect({ inner.x + caretX - scrollX_, inner.y + 2.f, 1.f, inner.h - 4.f }, c.text);

        ctx.r.popClip();
    }

    EventResult InputText::onEvent(const InputEvent& ev) {
        // La liste ouverte d'abord : elle deborde du champ, et ses touches sont
        // les siennes (fleches, Entree, Tab, Echap).
        if (!suggestions_.empty()) {
            if (const auto* d = std::get_if<MouseDown>(&ev)) {
                if (suggestionBox_.contains(d->pos)) {
                    // 1.11.2 : la legende du pied n'est pas une ligne (un clic dessus ne choisit rien).
                    const std::size_t shown = std::max<std::size_t>(1, std::min<std::size_t>(10, suggestions_.size()));
                    const float rowH = (suggestionBox_.h - 4.f - suggestionLegendH_) / static_cast<float>(shown);
                    const auto k = static_cast<std::size_t>(std::max(0.f, (d->pos.y - suggestionBox_.y - 2.f) / rowH));
                    if (k < shown) (void)acceptSuggestion(suggestionFirst_ + k);
                    return EventResult::Consumed;
                }
                if (!bounds().contains(d->pos)) closeSuggestions();
            }
            if (const auto* w = std::get_if<MouseWheel>(&ev); w && suggestionBox_.contains(w->pos)) {
                const auto n = suggestions_.size();
                if (w->dy < 0 && suggestionIndex_ + 1 < n) suggestionIndex_ = std::min(n - 1, suggestionIndex_ + 3);
                else if (w->dy > 0) suggestionIndex_ = suggestionIndex_ > 3 ? suggestionIndex_ - 3 : 0;
                invalidate();
                return EventResult::Consumed;
            }
            if (const auto* k = std::get_if<KeyDown>(&ev); k && focused()) {
                switch (k->key) {
                case Key::Down:
                    suggestionIndex_ = (suggestionIndex_ + 1) % suggestions_.size();
                    invalidate();
                    return EventResult::Consumed;
                case Key::Up:
                    suggestionIndex_ = suggestionIndex_ == 0 ? suggestions_.size() - 1 : suggestionIndex_ - 1;
                    invalidate();
                    return EventResult::Consumed;
                case Key::PageDown:
                    suggestionIndex_ = std::min(suggestions_.size() - 1, suggestionIndex_ + 10);
                    invalidate();
                    return EventResult::Consumed;
                case Key::PageUp:
                    suggestionIndex_ = suggestionIndex_ > 10 ? suggestionIndex_ - 10 : 0;
                    invalidate();
                    return EventResult::Consumed;
                case Key::Return: {
                    // Ce qui est tape EST deja le nom choisi : Entree fait ce
                    // qu'elle fait d'habitude (valider le champ), sans un second appui.
                    const auto& chosen = suggestions_[suggestionIndex_];
                    const std::string& put = chosen.insert.empty() ? chosen.text : chosen.insert;
                    const auto from = std::min(suggestionFrom_, caret_);
                    if (text_.compare(from, caret_ - from, put) == 0) { closeSuggestions(); break; }
                    (void)acceptSuggestion(suggestionIndex_);
                    return EventResult::Consumed;
                }
                case Key::Tab:
                    (void)acceptSuggestion(suggestionIndex_);
                    return EventResult::Consumed;
                case Key::Escape:
                    closeSuggestions();
                    return EventResult::Consumed;
                default: break;
                }
            }
        }
        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && assist_ && k->mods.ctrl && k->key == Key::Space) {
            openSuggestions();
            return EventResult::Consumed;
        }

        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (!bounds().contains(d->pos)) return EventResult::Ignored;
            grabFocus();
            // Put the caret where the user clicked instead of always at the end.
            caret_ = caretFromLocalX(toLocal(d->pos).x);
            if (!d->mods.shift) selAnchor_ = caret_;
            invalidate();
            return EventResult::Consumed;
        }
        if (!focused()) return EventResult::Ignored;

        if (const auto* t = std::get_if<TextInput>(&ev)) {
            insert(t->utf8);
            if (assist_) updateSuggestions(false);
            return EventResult::Consumed;
        }
        if (const auto* k = std::get_if<KeyDown>(&ev)) {
            switch (k->key) {
            case Key::Backspace:
                if (caret_ != selAnchor_) eraseSelection();
                else if (caret_ > 0) {
                    const auto p = prevCodepoint(text_, caret_);
                    text_.erase(p, caret_ - p);
                    caret_ = selAnchor_ = p;
                }
                textChanged->emit(text_);
                invalidate();
                if (!suggestions_.empty()) updateSuggestions(false);
                return EventResult::Consumed;
            case Key::Delete:
                if (caret_ != selAnchor_) eraseSelection();
                else if (caret_ < text_.size()) {
                    const auto n = nextCodepoint(text_, caret_);
                    text_.erase(caret_, n - caret_);
                }
                textChanged->emit(text_);
                invalidate();
                return EventResult::Consumed;
            case Key::Left:
                caret_ = prevCodepoint(text_, caret_);
                if (!k->mods.shift) selAnchor_ = caret_;
                closeSuggestions();
                invalidate();
                return EventResult::Consumed;
            case Key::Right:
                caret_ = nextCodepoint(text_, caret_);
                if (!k->mods.shift) selAnchor_ = caret_;
                closeSuggestions();
                invalidate();
                return EventResult::Consumed;
            case Key::Home:
                caret_ = 0;
                if (!k->mods.shift) selAnchor_ = 0;
                invalidate();
                return EventResult::Consumed;
            case Key::End:
                caret_ = text_.size();
                if (!k->mods.shift) selAnchor_ = caret_;
                invalidate();
                return EventResult::Consumed;
            case Key::A:
                if (k->mods.ctrl) { selAnchor_ = 0; caret_ = text_.size(); invalidate(); return EventResult::Consumed; }
                break;
            case Key::C:
                if (k->mods.ctrl) { copySelection(); return EventResult::Consumed; }
                break;
            case Key::X:
                if (k->mods.ctrl) {
                    copySelection();
                    eraseSelection();
                    textChanged->emit(text_);
                    invalidate();
                    return EventResult::Consumed;
                }
                break;
            case Key::V:
                if (k->mods.ctrl) {
                    auto paste = clipboardText();
                    // Paths pasted from Explorer arrive quoted, and a stray
                    // newline would end up inside the field.
                    std::erase_if(paste, [](char c) { return c == '\n' || c == '\r'; });
                    if (paste.size() >= 2 && paste.front() == '"' && paste.back() == '"')
                        paste = paste.substr(1, paste.size() - 2);
                    if (!paste.empty()) insert(paste);
                    return EventResult::Consumed;
                }
                break;
            case Key::Z:
                // Lot 19 : la saisie du champ d'abord ; rien a y defaire, et
                // Ctrl+Z passe a l'historique du projet (l'ecran le prend).
                if (k->mods.ctrl && !k->mods.shift && !readOnly_ && text_ != undoText_) {
                    setText(undoText_);
                    return EventResult::Consumed;
                }
                break;
            case Key::Return:
                editingDone->emit(text_);
                undoText_ = text_;
                return EventResult::Consumed;
            case Key::Escape:
                // Lot API 8 : finitions - une recherche s'efface d'abord (le curseur reste).
                if (escapeClears_ && !readOnly_ && !text_.empty()) {
                    setText({});
                    return EventResult::Consumed;
                }
                releaseFocus();
                return EventResult::Consumed;
            default: break;
            }
        }
        return EventResult::Ignored;
    }

    // ---- Lot API 8 : finitions (Ctrl+F) ----
    bool InputText::focusAndSelectAll() {
        if (!acceptsFocus()) return false;
        for (const Widget* p = parent(); p; p = p->parent())
            if (!p->visible()) return false;
        grabFocus();
        closeSuggestions();
        selAnchor_ = 0;
        caret_ = text_.size();
        invalidate();
        return focused();
    }
    // ---- fin Lot API 8 : finitions ----

    // ========================================================= MultiLineText ====
    MultiLineText::MultiLineText(std::string id) : Widget(std::move(id)) {
        setFocusPolicy(true);
        setPadding({ 2.f, 4.f, 2.f, 4.f });
    }

    SizeHint MultiLineText::sizeHint() const {
        SizeHint h;
        h.preferred = { 320.f, 120.f };
        h.minimum = { 120.f, lineHeight(kFontMono) * 3.f };
        h.stretchX = h.stretchY = 1.f;
        return h;
    }

    gfx::FontId MultiLineText::scaledFont(gfx::FontId base) const {
        // The SDL backend rounds the glyph scale to a whole number, so stepping the
        // pixel size in multiples of 8 is what actually changes anything on screen.
        const float px = static_cast<float>(base.v ? base.v : 16) * zoom_;
        const auto snapped = static_cast<std::uint16_t>(std::max(8.f, std::round(px / 8.f) * 8.f));
        return gfx::FontId{ snapped };
    }

    void MultiLineText::setZoom(float zoom) {
        const float z = std::clamp(zoom, 0.5f, 3.0f);
        if (z == zoom_) return;
        zoom_ = z;
        invalidate();
    }

    // ------------------------------------------------------------- structure ---
    void MultiLineText::rebuildLines() {
        lines_.clear();
        std::vector<Token> scratch;
        bool inBlock = false;
        std::size_t begin = 0;

        for (std::size_t i = 0; i <= buffer_.size(); ++i) {
            if (i != buffer_.size() && buffer_[i] != '\n') continue;
            std::size_t end = i;
            if (end > begin && buffer_[end - 1] == '\r') --end;   // CRLF exports

            Line line;
            line.begin = static_cast<std::uint32_t>(begin);
            line.end = static_cast<std::uint32_t>(end);
            line.inBlockComment = inBlock;

            // Indentation in columns, tabs expanded to four. A line that is only
            // whitespace has no indentation of its own and must not break a region.
            std::uint16_t column = 0;
            std::size_t   k = begin;
            for (; k < end; ++k) {
                if (buffer_[k] == ' ')       ++column;
                else if (buffer_[k] == '\t') column = static_cast<std::uint16_t>((column / 4 + 1) * 4);
                else break;
            }
            line.blank = (k == end);
            line.indent = column;
            line.foldEnd = static_cast<std::uint32_t>(lines_.size());
            lines_.push_back(line);

            if (language_ != Language::PlainText)
                tokenizeLine(std::string_view(buffer_).substr(begin, end - begin),
                    language_, scratch, inBlock);
            begin = i + 1;
        }

        // Single pass with a stack of open regions: O(lines), which matters because
        // a Control Expert section can be 20 000 lines long.
        struct Open { std::uint16_t indent; std::uint32_t line; };
        std::vector<Open> open;
        std::uint32_t lastContent = 0;
        maxDepth_ = 0;

        for (std::uint32_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].blank) continue;
            const auto indent = lines_[i].indent;

            while (!open.empty() && open.back().indent >= indent) {
                lines_[open.back().line].foldEnd = lastContent;
                open.pop_back();
            }
            lines_[i].depth = static_cast<std::uint16_t>(open.size());
            maxDepth_ = std::max(maxDepth_, static_cast<int>(open.size()));
            open.push_back(Open{ indent, i });
            lastContent = i;
        }
        while (!open.empty()) {
            lines_[open.back().line].foldEnd = lastContent;
            open.pop_back();
        }

        folded_.assign(lines_.size(), false);
        firstVisible_ = 0;
        scrollX_ = 0.f;
        caret_ = anchor_ = Caret{};
        rebuildVisible();
    }

    void MultiLineText::rebuildVisible() {
        visible_.clear();
        visible_.reserve(lines_.size());
        for (std::uint32_t i = 0; i < lines_.size();) {
            visible_.push_back(i);
            if (i < folded_.size() && folded_[i] && lines_[i].foldEnd > i)
                i = lines_[i].foldEnd + 1;          // skip the collapsed body
            else
                ++i;
        }
        if (firstVisible_ >= visible_.size() && !visible_.empty())
            firstVisible_ = visible_.size() - 1;
        invalidate();
    }

    bool MultiLineText::isFoldable(std::size_t line) const {
        return line < lines_.size() && lines_[line].foldEnd > line;
    }
    bool MultiLineText::isFolded(std::size_t line) const {
        return line < folded_.size() && folded_[line];
    }

    void MultiLineText::toggleFold(std::size_t line) {
        if (!isFoldable(line)) return;
        folded_[line] = !folded_[line];
        rebuildVisible();
    }

    void MultiLineText::foldAll() {
        for (std::size_t i = 0; i < lines_.size(); ++i) folded_[i] = isFoldable(i);
        rebuildVisible();
    }

    void MultiLineText::unfoldAll() {
        std::fill(folded_.begin(), folded_.end(), false);
        rebuildVisible();
    }

    void MultiLineText::foldToDepth(int depth) {
        if (depth < 0) { unfoldAll(); return; }
        for (std::size_t i = 0; i < lines_.size(); ++i)
            folded_[i] = isFoldable(i) && static_cast<int>(lines_[i].depth) >= depth;
        rebuildVisible();
    }

    void MultiLineText::setText(std::string t) {
        buffer_ = std::move(t);
        rebuildLines();
        invalidate();
    }

    std::string MultiLineText::text() const { return buffer_; }

    void MultiLineText::setLanguage(Language l) {
        if (language_ == l) return;
        language_ = l;
        rebuildLines();
        invalidate();
    }

    // --------------------------------------------------------------- editing ---
    std::size_t MultiLineText::offsetOf(Caret c) const {
        if (lines_.empty()) return 0;
        const auto line = std::min<std::size_t>(c.line, lines_.size() - 1);
        const auto len = lines_[line].end - lines_[line].begin;
        return lines_[line].begin + std::min(c.column, len);
    }

    MultiLineText::Caret MultiLineText::caretFromOffset(std::size_t offset) const {
        Caret c;
        for (std::uint32_t i = 0; i < lines_.size(); ++i) {
            if (offset <= lines_[i].end || i + 1 == lines_.size()) {
                c.line = i;
                c.column = static_cast<std::uint32_t>(
                    offset > lines_[i].begin ? std::min<std::size_t>(offset - lines_[i].begin,
                        lines_[i].end - lines_[i].begin)
                    : 0);
                return c;
            }
        }
        return c;
    }

    void MultiLineText::applyEdit(std::size_t from, std::size_t to, std::string_view replacement) {
        if (readOnly_) return;
        from = std::min(from, buffer_.size());
        to = std::clamp(to, from, buffer_.size());

        buffer_.replace(from, to - from, replacement);
        const auto caretOffset = from + replacement.size();

        // The whole index is rebuilt: one linear scan, no per-line allocation.
        // Measured well under a millisecond on the largest section in the reference
        // project, which is the only reason this is not a piece table.
        rebuildLines();
        caret_ = anchor_ = caretFromOffset(caretOffset);
        ensureCaretVisible();
        modified_ = true;
        invalidate();
        textChanged->emit(buffer_);
        updateSignature();
    }

    void MultiLineText::deleteSelection() {
        if (readOnly_ || !hasSelection()) return;
        const auto [a, b] = orderedSelection();
        applyEdit(offsetOf(a), offsetOf(b), {});
        dismissCompletion();
    }

    void MultiLineText::insertText(std::string_view utf8) {
        if (readOnly_) return;
        const auto [a, b] = orderedSelection();
        const auto from = hasSelection() ? offsetOf(a) : offsetOf(caret_);
        const auto to = hasSelection() ? offsetOf(b) : from;
        applyEdit(from, to, utf8);
        updateCompletion();
    }

    void MultiLineText::deleteBefore() {
        if (readOnly_) return;
        if (hasSelection()) { deleteSelection(); return; }
        const auto at = offsetOf(caret_);
        if (at == 0) return;
        // Step back over a whole UTF-8 sequence, never into the middle of one.
        std::size_t from = at - 1;
        while (from > 0 && isContinuation(buffer_[from])) --from;
        applyEdit(from, at, {});
        updateCompletion();
    }

    void MultiLineText::deleteAfter() {
        if (readOnly_) return;
        if (hasSelection()) { deleteSelection(); return; }
        const auto at = offsetOf(caret_);
        if (at >= buffer_.size()) return;
        std::size_t to = at + 1;
        while (to < buffer_.size() && isContinuation(buffer_[to])) ++to;
        applyEdit(at, to, {});
        dismissCompletion();
    }

    void MultiLineText::insertNewline() {
        if (readOnly_) return;
        // Carry the current line's indentation to the new line. Indentation is what
        // the folding is derived from, so losing it on Enter would quietly flatten
        // the structure of everything typed.
        std::string insert = "\n";
        if (caret_.line < lines_.size()) {
            const auto& l = lines_[caret_.line];
            for (std::size_t i = l.begin; i < l.end && i < l.begin + caret_.column; ++i) {
                if (buffer_[i] != ' ' && buffer_[i] != '\t') break;
                insert.push_back(buffer_[i]);
            }
        }
        const auto [a, b] = orderedSelection();
        const auto from = hasSelection() ? offsetOf(a) : offsetOf(caret_);
        const auto to = hasSelection() ? offsetOf(b) : from;
        applyEdit(from, to, insert);
        dismissCompletion();
    }

    // ------------------------------------------------------------ completion ---
    void MultiLineText::setCompletionProvider(CompletionProvider provider) {
        completionProvider_ = std::move(provider);
    }

    std::string MultiLineText::completionPrefix() const {
        const auto at = offsetOf(caret_);
        std::size_t begin = at;
        while (begin > 0 && identChar(buffer_[begin - 1])) --begin;
        return buffer_.substr(begin, at - begin);
    }

    void MultiLineText::dismissCompletion() {
        if (completions_.empty()) return;
        completions_.clear();
        completionIndex_ = 0;
        invalidate();
    }

    void MultiLineText::updateCompletion() {
        if (!completionProvider_ || readOnly_) { dismissCompletion(); return; }

        const auto prefix = completionPrefix();
        // Just after "name." : the members, when the host asked for it. The
        // provider answers nothing when the name is not a structure, and the
        // list then stays closed.
        if (completeAfterDot_ && prefix.empty()) {
            const auto at = offsetOf(caret_);
            if (at > 0 && buffer_[at - 1] == '.') { requestCompletion(); return; }
        }
        // 1.10 (chantier K2) : "T_MODE#" et la premiere lettre d'une valeur - les valeurs de
        // l'enumeration (apres T#, 16#..., le fournisseur ne repond rien : la liste reste fermee).
        if (completeAfterDot_ && prefix.size() < 2) {
            const auto start = offsetOf(caret_) - prefix.size();
            if (start > 1 && buffer_[start - 1] == '#' && identChar(buffer_[start - 2])) { requestCompletion(); return; }
        }
        // Two characters before offering anything: one character matches half the
        // dictionary and the list becomes noise that hides the code behind it.
        if (prefix.size() < 2) { dismissCompletion(); return; }
        requestCompletion();
    }

    void MultiLineText::requestCompletion() {
        if (!completionProvider_ || readOnly_) return;

        const auto prefix = completionPrefix();
        completionStart_ = offsetOf(caret_) - prefix.size();

        completions_.clear();
        completionProvider_(prefix, completions_);

        // Prefix matches before substring matches, then alphabetical. Ranking is the
        // difference between a list and a useful list.
        std::stable_sort(completions_.begin(), completions_.end(),
            [](const Completion& a, const Completion& b) {
                if (a.rank != b.rank) return a.rank < b.rank;
                return a.text < b.text;
            });
        if (completions_.size() > 12) completions_.resize(12);
        completionIndex_ = 0;
        invalidate();
    }

    void MultiLineText::acceptCompletion() {
        if (completions_.empty()) return;
        const auto& chosen = completions_[std::min(completionIndex_, completions_.size() - 1)];
        const auto  at = offsetOf(caret_);

        // The indentation of the line the snippet lands on. Continuation lines get
        // the same, so an IF block typed at four spaces stays at four spaces - and
        // the folding, which is derived from indentation, stays right.
        std::string indent;
        if (caret_.line < lines_.size()) {
            const auto& l = lines_[caret_.line];
            for (std::size_t i = l.begin; i < l.end; ++i) {
                if (buffer_[i] != ' ' && buffer_[i] != '\t') break;
                indent.push_back(buffer_[i]);
            }
        }

        const std::string& source = chosen.insert.empty() ? chosen.text : chosen.insert;
        std::string expanded;
        expanded.reserve(source.size() + 32);
        std::size_t caretInExpanded = std::string::npos;

        for (std::size_t i = 0; i <= source.size(); ++i) {
            if (i == chosen.caret) caretInExpanded = expanded.size();
            if (i == source.size()) break;
            expanded.push_back(source[i]);
            if (source[i] == '\n') expanded.append(indent);
        }
        if (caretInExpanded == std::string::npos) caretInExpanded = expanded.size();

        const auto start = completionStart_ - std::min(chosen.extend, completionStart_);
        const bool chain = chosen.chain;
        completions_.clear();
        applyEdit(start, at, expanded);

        // applyEdit leaves the caret at the end of what was inserted; a snippet
        // wants it wherever the marker said.
        caret_ = anchor_ = caretFromOffset(start + caretInExpanded);
        ensureCaretVisible();
        updateSignature();
        if (chain) requestCompletion();
        invalidate();
    }

    // -------------------------------------------------------- signature help ---
    void MultiLineText::setValueProvider(ValueProvider provider) {
        valueProvider_ = std::move(provider);
        if (!valueProvider_) { hoverSymbol_.clear(); setTooltip({}); }
    }

    // Lot 7 : l'infobulle ouverte relit la valeur du nom survole a chaque image
    // (le survol ne la demandait qu'en changeant de nom : elle restait figee).
    std::string MultiLineText::liveTooltip(gfx::Point mouse) const {
        // Lot API 8 : la colonne des points d'arret dit ce qu'un clic y fait.
        if (inBreakColumn(mouse) && !lines_.empty()) {
            const auto line = lineAt(mouse.y);
            const std::string n = std::to_string(line + 1);
            for (const auto& b : breaks_)
                if (b.line == line)
                    return std::string("Point d'arr\xC3\xAAt ligne ") + n + (b.enabled ? "" : " (d\xC3\xA9sactiv\xC3\xA9)")
                         + " : clic pour l'enlever \xC2\xB7 Maj+clic pour " + (b.enabled ? "le d\xC3\xA9sactiver" : "le r\xC3\xA9" "activer")
                         + " \xC2\xB7 clic droit : sa condition";
            return "Clic : un point d'arr\xC3\xAAt ligne " + n + " (F9 sur la ligne du curseur) \xC2\xB7 clic droit : avec une condition";
        }
        // 1.10 : une faute soulignee dit son message (avant la valeur du nom).
        if (const auto* s = squiggleAt(mouse))
            return std::string(s->tone == Tone::Warning ? "Avertissement : " : "Erreur : ") + s->message;
        if (valueProvider_ && !hoverSymbol_.empty()) {
            // Plus de valeur (la simulation s'est arretee) : plus d'infobulle,
            // comme le survol le decidait.
            std::string text;
            return valueProvider_(hoverSymbol_, text) ? text : std::string{};
        }
        return Widget::liveTooltip(mouse);
    }

    // The whole dotted, indexed name under the pointer: Armoires[0].ana.PT1 rather
    // than just the word, because that is what names a value.
    std::string MultiLineText::symbolAt(gfx::Point global) const {
        if (lines_.empty() || !bounds().contains(global)) return {};
        return symbolAtCaretPosition(caretAt(global));
    }

    // The same extraction the hover tooltip uses, from a caret rather than a point.
    // One implementation, so the bar and the tooltip can never disagree about what
    // the word under the cursor is.
    std::string MultiLineText::symbolAtCaretPosition(Caret caret) const {
        if (lines_.empty()) return {};
        if (caret.line >= lines_.size()) return {};
        const auto& l = lines_[caret.line];
        const std::string_view line(buffer_.data() + l.begin, l.end - l.begin);
        if (caret.column > line.size()) return {};

        auto isPart = [&](std::size_t i) {
            if (i >= line.size()) return false;
            return identChar(line[i]) || line[i] == '.' || line[i] == '[' || line[i] == ']'
                || line[i] == '%';
            };
        if (!isPart(caret.column) && (caret.column == 0 || !isPart(caret.column - 1))) return {};

        std::size_t begin = caret.column;
        while (begin > 0 && isPart(begin - 1)) --begin;
        std::size_t end = caret.column;
        while (end < line.size() && isPart(end)) ++end;

        auto symbol = std::string(line.substr(begin, end - begin));
        // A trailing dot or bracket is punctuation, not part of the name.
        while (!symbol.empty() && (symbol.back() == '.' || symbol.back() == '[')) symbol.pop_back();
        while (!symbol.empty() && (symbol.front() == '.' || symbol.front() == ']')) symbol.erase(symbol.begin());
        if (symbol.empty() || std::isdigit(static_cast<unsigned char>(symbol.front()))) return {};
        return symbol;
    }

    std::string MultiLineText::symbolAtCaret() const {
        if (hasSelection()) {
            auto picked = selectedText();
            // Only a selection that IS a name counts. Selecting three lines is not
            // asking about a symbol, and describing the first word of it would be
            // answering a question nobody asked.
            //
            // The first draft also tested for a newline and for a space. A mutation
            // run could not make either matter, and it was right: the all_of below
            // admits nothing but identifier characters, a dot, a bracket and a
            // percent, so a newline and a space are already excluded. Two guards
            // that no input can falsify are not belt and braces, they are dead code
            // that reads like a safeguard - and the next person has to work out, as
            // I did, that they never fire.
            const bool oneName = !picked.empty()
                && !std::isdigit(static_cast<unsigned char>(picked.front()))
                && std::all_of(picked.begin(), picked.end(), [](unsigned char c) {
                return identChar(static_cast<char>(c)) || c == '.' || c == '['
                    || c == ']' || c == '%';
                    });
            if (oneName) return picked;
        }
        return symbolAtCaretPosition(caret_);
    }

    void MultiLineText::notifyCaretSymbol() {
        auto now = symbolAtCaret();
        if (now == lastCaretSymbol_) return;
        lastCaretSymbol_ = std::move(now);
        caretSymbolChanged->emit(lastCaretSymbol_);
    }

    void MultiLineText::setSignatureProvider(SignatureProvider provider) {
        signatureProvider_ = std::move(provider);
    }

    void MultiLineText::updateSignature() {
        signatureActive_ = false;
        if (!signatureProvider_ || lines_.empty()) return;
        if (caret_.line >= lines_.size()) return;

        // The search is confined to the caret's line. A call spanning lines is legal
        // and rare; searching further would mean re-deriving the comment state for
        // every line above, and getting it subtly wrong is worse than not offering
        // help on the rare case.
        const auto& line = lines_[caret_.line];
        const auto  text = std::string_view(buffer_).substr(line.begin, line.end - line.begin);
        const auto  upTo = std::min<std::size_t>(caret_.column, text.size());

        // Which characters are code. Derived by scanning forward from the start of
        // the line with the block-comment state recorded when the line index was
        // built - the same state the highlighter uses, so the two always agree.
        std::vector<char> isCode(upTo, 0);
        bool inBlock = line.inBlockComment;
        bool inString = false;
        for (std::size_t i = 0; i < upTo; ++i) {
            if (inBlock) {
                if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == ')') { inBlock = false; ++i; }
                continue;
            }
            if (inString) {
                if (text[i] == '\'') inString = false;
                continue;
            }
            if (text[i] == '(' && i + 1 < text.size() && text[i + 1] == '*') { inBlock = true; ++i; continue; }
            if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') break;   // rest is comment
            if (text[i] == '\'') { inString = true; continue; }
            isCode[i] = 1;
        }

        // Innermost unclosed '(' among the code characters, and how many arguments
        // have been separated since it.
        int         depth = 0;
        std::size_t open = std::string_view::npos;
        std::size_t commas = 0;
        for (std::size_t i = upTo; i-- > 0;) {
            if (!isCode[i]) continue;
            const char c = text[i];
            if (c == ')') { ++depth; continue; }
            if (c == '(') {
                if (depth == 0) { open = i; break; }
                --depth;
                continue;
            }
            if (c == ',' && depth == 0) ++commas;
        }
        if (open == std::string_view::npos) return;

        std::size_t end = open;
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
        std::size_t begin = end;
        while (begin > 0 && identChar(text[begin - 1])) --begin;
        if (begin == end) return;

        Signature found;
        if (!signatureProvider_(text.substr(begin, end - begin), found)) return;

        signature_ = std::move(found);
        activeParameter_ = commas;
        signatureActive_ = !signature_.parameters.empty();
        invalidate();
    }

    void MultiLineText::drawSignature(const PaintContext& ctx) const {
        if (!signatureActive_) return;
        const auto& c = ctx.theme.color;
        const auto  f = scaledFont(ctx.theme.font.ui);

        // One line: name, then the parameters with the one being typed picked out.
        std::vector<std::string> pieces;
        pieces.push_back(signature_.name + "(");
        for (std::size_t i = 0; i < signature_.parameters.size(); ++i)
            pieces.push_back(signature_.parameters[i]
                + (i + 1 < signature_.parameters.size() ? ", " : ""));
        pieces.push_back(")");
        if (!signature_.returns.empty()) pieces.push_back(" : " + signature_.returns);

        float width = 16.f;
        for (const auto& p : pieces) width += ctx.r.measure(p, f).width;
        const float height = ctx.r.lineHeight(f) + 8.f;

        const auto inner = contentRect();
        float x = inner.x + paintGutter_ + columnX(caret_.line, caret_.column) - scrollX_;
        float y = inner.y + static_cast<float>(caretRow()) * paintLineHeight_ - height - 2.f;
        if (y < bounds().y) y = inner.y + static_cast<float>(caretRow() + 1) * paintLineHeight_ + 2.f;
        if (x + width > bounds().right()) x = std::max(bounds().x, bounds().right() - width - 4.f);

        const gfx::Rect box{ x, y, width, height };
        ctx.r.fillRect({ box.x + 2.f, box.y + 2.f, box.w, box.h }, gfx::Color{ 0, 0, 0, 90 });
        ctx.r.fillRect(box, c.headerBg);
        ctx.r.strokeRect(box, c.borderStrong, 1.f);

        float tx = box.x + 8.f;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            // pieces[0] is the name, so parameter n is at index n + 1.
            const bool active = i >= 1 && i - 1 == activeParameter_
                && i - 1 < signature_.parameters.size();
            ctx.r.drawText({ tx, box.y + 4.f }, pieces[i],
                active ? ctx.theme.font.uiBold : f,
                active ? c.accent : c.textMuted);
            tx += ctx.r.measure(pieces[i], f).width;
        }
    }

    // ------------------------------------------------------------- selection ---
    std::pair<MultiLineText::Caret, MultiLineText::Caret> MultiLineText::orderedSelection() const {
        if (anchor_.line < caret_.line
            || (anchor_.line == caret_.line && anchor_.column <= caret_.column))
            return { anchor_, caret_ };
        return { caret_, anchor_ };
    }

    bool MultiLineText::hasSelection() const noexcept {
        return anchor_.line != caret_.line || anchor_.column != caret_.column;
    }

    void MultiLineText::clearSelection() {
        anchor_ = caret_;
        invalidate();
    }

    void MultiLineText::selectAll() {
        if (lines_.empty()) return;
        anchor_ = Caret{ 0, 0 };
        caret_ = Caret{ static_cast<std::uint32_t>(lines_.size() - 1),
                        lines_.back().end - lines_.back().begin };
        invalidate();
    }

    std::string MultiLineText::selectedText() const {
        if (lines_.empty()) return {};
        if (!hasSelection()) return buffer_;

        const auto [from, to] = orderedSelection();
        std::string out;
        for (std::uint32_t i = from.line; i <= to.line && i < lines_.size(); ++i) {
            const auto& l = lines_[i];
            const auto len = l.end - l.begin;
            const auto a = (i == from.line) ? std::min<std::uint32_t>(from.column, len) : 0u;
            const auto b = (i == to.line) ? std::min<std::uint32_t>(to.column, len) : len;
            if (b > a) out.append(buffer_, l.begin + a, b - a);
            if (i != to.line) out.push_back('\n');
        }
        return out;
    }

    void MultiLineText::copySelection() const {
        const auto text = selectedText();
        if (!text.empty()) setClipboardText(text);
    }

    // ------------------------------------------------------------- geometry ---
    float MultiLineText::gutterWidth(const gfx::IRenderer& r, gfx::FontId f) const {
        const float numbers = lineNumbers_
            ? r.measure(std::to_string(std::max<std::size_t>(1, lines_.size())), f).width + 12.f
            : 0.f;
        return numbers + 18.f             // + the fold-marker column
            + (breakGutter_ ? 18.f : 0.f);   // Lot API 8 : + la colonne des points d'arret
    }

    std::size_t MultiLineText::caretRow() const {
        for (std::size_t row = firstVisible_; row < visible_.size(); ++row)
            if (visible_[row] == caret_.line) return row - firstVisible_;
        return 0;
    }

    std::size_t MultiLineText::lineAt(float globalY) const {
        if (visible_.empty()) return 0;
        const auto inner = contentRect();
        const float lh = paintLineHeight_;          // exactly what the last paint used
        if (lh <= 0.f) return visible_[firstVisible_];
        const auto offset = static_cast<std::size_t>(std::max(0.f, (globalY - inner.y) / lh));
        const auto index = std::min(firstVisible_ + offset, visible_.size() - 1);
        return visible_[index];
    }

    MultiLineText::Caret MultiLineText::caretAt(gfx::Point global) const {
        Caret c;
        if (lines_.empty()) return c;
        c.line = static_cast<std::uint32_t>(lineAt(global.y));

        // Measured from the same origin the painter drew from: the full gutter
        // (line numbers + fold markers), not the marker column alone. The face is
        // proportional, so the column comes from the measured glyph widths, not
        // from one division by the width of "M".
        const auto  inner = contentRect();
        const float x = global.x - (inner.x + paintGutter_) + scrollX_;
        c.column = columnAtX(c.line, x);
        return c;
    }

    float MultiLineText::columnX(std::size_t line, std::uint32_t column) const {
        if (line >= lines_.size()) return 0.f;
        const auto& l = lines_[line];
        const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
        return measureWidth(text.substr(0, std::min<std::size_t>(column, text.size())), paintFont_);
    }

    std::uint32_t MultiLineText::columnAtX(std::size_t line, float x) const {
        if (line >= lines_.size() || x <= 0.f) return 0;
        const auto& l = lines_[line];
        const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
        // Round to the nearest boundary: clicking the left half of a glyph puts
        // the caret before it, the right half after it, which is what every
        // editor does and what makes click-and-drag feel right. A boundary is
        // always between two whole UTF-8 sequences.
        float left = 0.f;
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t next = at + 1;
            while (next < text.size() && isContinuation(text[next])) ++next;
            const float w = measureWidth(text.substr(at, next - at), paintFont_);
            if (x < left + w * 0.5f) return static_cast<std::uint32_t>(at);
            left += w;
            at = next;
        }
        return static_cast<std::uint32_t>(text.size());
    }

    void MultiLineText::scrollToLine(std::size_t line) {
        for (std::size_t i = 0; i < visible_.size(); ++i)
            if (visible_[i] >= line) { firstVisible_ = i; break; }
        invalidate();
    }

    void MultiLineText::goToLine(std::size_t line) {
        if (lines_.empty()) return;
        line = std::min(line, lines_.size() - 1);
        // Une ligne repliee ne se montre pas : on deplie tout plutot que de
        // chercher la region qui la cache.
        bool hidden = true;
        for (const auto v : visible_) if (v == line) { hidden = false; break; }
        if (hidden) unfoldAll();
        caret_.line = static_cast<std::uint32_t>(line);
        caret_.column = 0;
        // Lot API 8 : corrections des captures - l'ancre suit le curseur APRES le
        // saut (avant, elle restait a l'ancien curseur : une selection bleue
        // parasite de l'ancienne position jusqu'a la ligne, vue dans le debogage).
        clearSelection();
        scrollToLine(line > 3 ? line - 3 : 0);
        notifyCaretSymbol();
        invalidate();
    }

    void MultiLineText::setMarkedLines(std::vector<std::pair<std::size_t, gfx::Color>> lines) {
        marked_ = std::move(lines);
        invalidate();
    }

    // ---- Lot API 8 : la marge des points d'arret, la ligne d'arret, les notes ----
    void MultiLineText::setBreakpointGutter(bool on) {
        if (breakGutter_ == on) return;
        breakGutter_ = on;
        breakHover_ = std::string::npos;
        invalidateLayout();
        invalidate();
    }

    void MultiLineText::setBreakpoints(std::vector<BreakMark> marks) {
        const bool same = marks.size() == breaks_.size()
            && std::equal(marks.begin(), marks.end(), breaks_.begin(), [](const BreakMark& a, const BreakMark& b) {
                   return a.line == b.line && a.enabled == b.enabled && a.conditional == b.conditional;
               });
        if (same) return;
        breaks_ = std::move(marks);
        invalidate();
    }

    void MultiLineText::setExecutionLine(std::size_t line) {
        if (execLine_ == line) return;
        execLine_ = line;
        invalidate();
    }

    void MultiLineText::setLineNotes(std::vector<LineNote> notes) {
        const bool same = notes.size() == notes_.size()
            && std::equal(notes.begin(), notes.end(), notes_.begin(), [](const LineNote& a, const LineNote& b) {
                   return a.line == b.line && a.text == b.text && a.tone == b.tone;
               });
        if (same) return;
        notes_ = std::move(notes);
        invalidate();
    }

    // La colonne des points d'arret : le bord gauche de la marge (paintBreak_ vaut
    // 0 sans elle, ou avant le premier dessin).
    bool MultiLineText::inBreakColumn(gfx::Point global) const {
        if (!breakGutter_ || paintBreak_ <= 0.f || !bounds().contains(global)) return false;
        const auto inner = contentRect();
        return global.x >= inner.x - 2.f && global.x < inner.x + paintBreak_
            && global.y >= inner.y && global.y < inner.bottom();
    }

    bool MultiLineText::breakpointMarginPoint(std::size_t line, gfx::Point& out) const {
        if (!breakGutter_ || paintBreak_ <= 0.f || paintLineHeight_ <= 0.f) return false;
        const auto inner = contentRect();
        const auto rows = static_cast<std::size_t>(std::max(1.f, inner.h / paintLineHeight_));
        for (std::size_t row = firstVisible_; row < visible_.size() && row < firstVisible_ + rows; ++row)
            if (visible_[row] == line) {
                out = { inner.x + paintBreak_ * 0.5f,
                        inner.y + (static_cast<float>(row - firstVisible_) + 0.5f) * paintLineHeight_ };
                return true;
            }
        return false;
    }

    bool MultiLineText::positionRect(std::size_t line, std::uint32_t column, gfx::Rect& out) const {
        if (paintLineHeight_ <= 0.f || line >= lines_.size()) return false;
        const auto inner = contentRect();
        const auto rows = static_cast<std::size_t>(std::max(1.f, inner.h / paintLineHeight_));
        for (std::size_t row = firstVisible_; row < visible_.size() && row < firstVisible_ + rows; ++row)
            if (visible_[row] == line) {
                const auto& l = lines_[line];
                const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
                const auto at = std::min<std::size_t>(column, text.size());
                std::size_t next = at + 1;
                while (next < text.size() && isContinuation(text[next])) ++next;
                const float a = columnX(line, static_cast<std::uint32_t>(at));
                const float b = at < text.size() ? columnX(line, static_cast<std::uint32_t>(next))
                                                 : a + paintLineHeight_ * 0.5f;
                out = { inner.x + paintGutter_ - scrollX_ + a,
                        inner.y + static_cast<float>(row - firstVisible_) * paintLineHeight_,
                        std::max(1.f, b - a), paintLineHeight_ };
                return true;
            }
        return false;
    }

    bool MultiLineText::completionRect(std::string_view text, gfx::Rect& out) const {
        if (completions_.empty() || completionBox_.w <= 0.f || completionBox_.h <= 4.f) return false;
        if (text.empty()) { out = completionBox_; return true; }
        const auto same = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        };
        const std::size_t none = completions_.size();
        std::size_t found = none;
        for (std::size_t i = 0; i < completions_.size() && found == none; ++i)
            if (same(completions_[i].text, text)) found = i;
        for (std::size_t i = 0; i < completions_.size() && found == none; ++i)
            if (same(std::string_view(completions_[i].text).substr(0, text.size()), text)) found = i;
        if (found == none) return false;
        // drawCompletion : toutes les lignes, de meme hauteur, 2 px sous le haut de la boite.
        const float rowH = (completionBox_.h - 4.f - completionLegendH_) / static_cast<float>(completions_.size());
        out = { completionBox_.x + 2.f, completionBox_.y + 2.f + static_cast<float>(found) * rowH,
                completionBox_.w - 4.f, rowH };
        return true;
    }

    bool MultiLineText::hasTooltip() const { return Widget::hasTooltip() || breakGutter_ || !squiggles_.empty(); }
    // ---- fin Lot API 8 ----

    // ---- 1.10 : les fautes soulignees ----
    void MultiLineText::setSquiggles(std::vector<Squiggle> squiggles) {
        const bool same = squiggles.size() == squiggles_.size()
            && std::equal(squiggles.begin(), squiggles.end(), squiggles_.begin(), [](const Squiggle& a, const Squiggle& b) {
                   return a.line == b.line && a.column == b.column && a.length == b.length && a.tone == b.tone
                       && a.message == b.message;
               });
        if (same) return;
        squiggles_ = std::move(squiggles);
        invalidate();
    }

    // La faute sous la souris. La ligne n'est pas ramenee a la derniere comme
    // dans lineAt : sous le texte, rien n'est souligne.
    const MultiLineText::Squiggle* MultiLineText::squiggleAt(gfx::Point global) const {
        if (squiggles_.empty() || lines_.empty() || visible_.empty() || paintLineHeight_ <= 0.f || !bounds().contains(global))
            return nullptr;
        const auto inner = contentRect();
        if (global.y < inner.y || global.y >= inner.bottom()) return nullptr;
        const auto row = firstVisible_ + static_cast<std::size_t>((global.y - inner.y) / paintLineHeight_);
        if (row >= visible_.size()) return nullptr;
        const std::size_t line = visible_[row];
        // Dans la marge : le numero teinte dit la premiere faute de sa ligne.
        if (global.x < inner.x + paintGutter_) {
            if (global.x < inner.x + paintBreak_) return nullptr;      // la colonne des points d'arret
            for (const auto& s : squiggles_)
                if (s.line == line) return &s;
            return nullptr;
        }
        const float x = global.x - (inner.x + paintGutter_) + scrollX_;
        for (const auto& s : squiggles_) {
            if (s.line != line) continue;
            const float xa = columnX(s.line, s.column);
            const float xb = std::max(columnX(s.line, s.column + std::max<std::uint32_t>(1, s.length)), xa + 6.f);
            if (x >= xa - 1.f && x <= xb + 1.f) return &s;
        }
        return nullptr;
    }

    bool MultiLineText::squigglePoint(std::size_t index, gfx::Point& out) const {
        if (index >= squiggles_.size() || paintLineHeight_ <= 0.f) return false;
        const auto& s = squiggles_[index];
        const auto inner = contentRect();
        const auto rows = static_cast<std::size_t>(std::max(1.f, inner.h / paintLineHeight_));
        for (std::size_t row = firstVisible_; row < visible_.size() && row < firstVisible_ + rows; ++row)
            if (visible_[row] == s.line) {
                const float xa = columnX(s.line, s.column);
                const float xb = std::max(columnX(s.line, s.column + std::max<std::uint32_t>(1, s.length)), xa + 6.f);
                out = { inner.x + paintGutter_ - scrollX_ + (xa + xb) * 0.5f,
                        inner.y + (static_cast<float>(row - firstVisible_) + 0.5f) * paintLineHeight_ };
                return true;
            }
        return false;
    }

    void MultiLineText::selectRange(std::size_t line, std::uint32_t column, std::uint32_t length) {
        if (lines_.empty()) return;
        goToLine(line);                      // deplie, met la ligne en vue, efface la selection
        const auto& l = lines_[caret_.line];
        const auto len = l.end - l.begin;
        anchor_ = Caret{ caret_.line, std::min<std::uint32_t>(column, len) };
        caret_.column = std::min<std::uint32_t>(column + length, len);
        ensureCaretVisible();
        notifyCaretSymbol();
        invalidate();
    }
    // ---- fin 1.10 ----

    void MultiLineText::ensureCaretVisible() {
        // scrollToLine() puts the requested line at the TOP of the view. Calling it
        // after every edit meant the text jumped up on each keystroke - most
        // obviously on Tab, where nothing else changes and the whole page moves.
        // Scrolling should only happen when the caret would otherwise be off screen.
        if (visible_.empty() || paintLineHeight_ <= 0.f) return;

        std::size_t row = 0;
        bool found = false;
        for (std::size_t i = 0; i < visible_.size(); ++i)
            if (visible_[i] == caret_.line) { row = i; found = true; break; }
        if (!found) { scrollToLine(caret_.line); return; }   // the line is folded away

        const auto area = contentRect();
        const auto rows = static_cast<std::size_t>(std::max(1.f, area.h / paintLineHeight_));
        if (row < firstVisible_)                       firstVisible_ = row;
        else if (row >= firstVisible_ + rows)          firstVisible_ = row - rows + 1;

        // Sideways, for the same reason: a caret past the right edge is invisible,
        // but a caret already on screen must not drag the view around.
        {
            const float caretX = columnX(caret_.line, caret_.column);
            const float visibleWidth = std::max(0.f, area.w - paintGutter_ - 14.f);
            if (caretX < scrollX_)                       scrollX_ = caretX;
            else if (caretX > scrollX_ + visibleWidth)   scrollX_ = caretX - visibleWidth;
        }
        invalidate();
    }

    // -------------------------------------------------------------- painting ---
    void MultiLineText::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();
        const auto  f = scaledFont(ctx.theme.font.mono);

        ctx.r.fillRect(r, c.inputBg);
        ctx.r.strokeRect(r, focused() ? c.accent : c.border, 1.f);

        const auto inner = contentRect();
        const float lh = ctx.r.lineHeight(f);
        if (lh <= 0.f || visible_.empty()) return;

        const float numbers = lineNumbers_
            ? ctx.r.measure(std::to_string(std::max<std::size_t>(1, lines_.size())), f).width + 12.f
            : 0.f;
        const float markers = 18.f;                 // the +/- column
        const float breakW = breakGutter_ ? 18.f : 0.f;   // Lot API 8 : les points d'arret, a gauche
        const float gutter = breakW + numbers + markers;

        // Hit testing reads these back, so a click always resolves against the
        // geometry actually on screen rather than a second, slightly different
        // calculation.
        paintGutter_ = gutter;
        paintNumbers_ = numbers;
        paintBreak_ = breakW;
        paintLineHeight_ = lh;
        paintFont_ = f;

        ctx.r.fillRect({ inner.x, inner.y, gutter, inner.h }, c.panelBg);
        ctx.r.line({ inner.x + gutter - 1.f, inner.y }, { inner.x + gutter - 1.f, inner.bottom() },
            c.border, 1.f);

        const auto rows = static_cast<std::size_t>(inner.h / lh) + 1;
        const auto last = std::min(visible_.size(), firstVisible_ + rows);
        const auto [selFrom, selTo] = orderedSelection();
        float widest = 0.f;

        // Two passes. The TEXT first, clipped to the text area: scrolled
        // sideways, a line used to run left over the line numbers and the fold
        // markers ("ELSE" drawn across its own marker). Then the GUTTER, on top.
        const gfx::Rect textArea{ inner.x + gutter, inner.y, std::max(0.f, inner.w - gutter), inner.h };
        ctx.r.pushClip(textArea);
        for (std::size_t row = firstVisible_; row < last; ++row) {
            const auto i = visible_[row];
            const float y = inner.y + static_cast<float>(row - firstVisible_) * lh;
            const auto& l = lines_[i];
            const std::string_view line(buffer_.data() + l.begin, l.end - l.begin);
            widest = std::max(widest, ctx.r.measure(line, f).width);

            // --- lignes marquees (erreur de compilation...) --------------------
            for (const auto& [markedLine, color] : marked_)
                if (markedLine == i) {
                    gfx::Color band = color;
                    band.a = 48;
                    ctx.r.fillRect({ inner.x + gutter, y, inner.w - gutter, lh }, band);
                    break;
                }

            // --- Lot API 8 : une ligne a point d'arret (rouge pale), la ligne d'arret (ambre)
            if (breakGutter_) {
                for (const auto& b : breaks_)
                    if (b.line == i && b.enabled) {
                        ctx.r.fillRect({ inner.x + gutter, y, inner.w - gutter, lh }, c.error.withAlpha(30));
                        break;
                    }
                if (i == execLine_) ctx.r.fillRect({ inner.x + gutter, y, inner.w - gutter, lh }, c.warning.withAlpha(78));
            }

            // --- selection band, character accurate -------------------------
            if (hasSelection() && i >= selFrom.line && i <= selTo.line) {
                const auto len = static_cast<std::uint32_t>(line.size());
                const auto a = (i == selFrom.line) ? std::min(selFrom.column, len) : 0u;
                const bool toEnd = i != selTo.line;        // the line break is selected too
                const auto b = toEnd ? len : std::min(selTo.column, len);
                const float xa = ctx.r.measure(line.substr(0, a), f).width;
                const float xb = ctx.r.measure(line.substr(0, b), f).width
                    + (toEnd ? ctx.r.measure(" ", f).width : 0.f);
                if (xb > xa)
                    ctx.r.fillRect({ inner.x + gutter + xa - scrollX_, y, xb - xa, lh }, c.selectionBg);
            }
            else if (i == caret_.line && focused()) {
                ctx.r.fillRect({ inner.x + gutter, y, inner.w - gutter, lh }, c.rowAltBg);
            }

            // --- the text -----------------------------------------------------
            float x = inner.x + gutter - scrollX_;
            if (language_ == Language::PlainText) {
                ctx.r.drawText({ x, y }, line, f, c.text);
            }
            else {
                bool inBlock = l.inBlockComment;
                tokenizeLine(line, language_, tokens_, inBlock);
                for (const auto& t : tokens_) {
                    const auto piece = line.substr(t.begin, t.end - t.begin);
                    ctx.r.drawText({ x, y }, piece, f, colorFor(t.cls, c));
                    x += ctx.r.measure(piece, f).width;
                    if (x > inner.right()) break;
                }
            }

            // --- 1.10 : les fautes soulignees, un trait ondule sous leurs caracteres
            for (const auto& s : squiggles_) {
                if (s.line != i) continue;
                const auto len = static_cast<std::uint32_t>(line.size());
                const auto a = std::min(s.column, len);
                const auto b = std::min(s.column + std::max<std::uint32_t>(1, s.length), len);
                const float xa = ctx.r.measure(line.substr(0, a), f).width;
                float xb = ctx.r.measure(line.substr(0, b), f).width;
                if (xb < xa + 6.f) xb = xa + 6.f;                    // au bout de la ligne : un petit trait
                const gfx::Color ink = ctx.theme.tone(s.tone, c.error);
                const float x0 = inner.x + gutter + xa - scrollX_, x1 = inner.x + gutter + xb - scrollX_;
                const float base = y + lh - 2.5f;
                bool high = true;
                for (float wx = x0; wx < x1; wx += 2.f) {
                    const float we = std::min(wx + 2.f, x1);
                    ctx.r.line({ wx, high ? base - 1.5f : base + 1.f }, { we, high ? base + 1.f : base - 1.5f }, ink, 1.f);
                    high = !high;
                }
            }

            // --- collapsed marker ---------------------------------------------
            if (isFoldable(i) && folded_[i]) {
                const float mx = inner.x + gutter - scrollX_ + ctx.r.measure(line, f).width + 8.f;
                const auto hidden = lines_[i].foldEnd - i;
                char badge[40];
                std::snprintf(badge, sizeof badge, "... %u lines", hidden);
                const auto bm = ctx.r.measure(badge, f);
                ctx.r.fillRoundedRect({ mx - 3.f, y + 1.f, bm.width + 6.f, lh - 2.f },
                    c.headerBg, 3.f);
                ctx.r.drawText({ mx, y }, badge, f, c.textMuted);
            }

            // --- Lot API 8 : le texte a droite de la ligne (les valeurs au passage)
            for (const auto& note : notes_) {
                if (note.line != i || note.text.empty()) continue;
                const auto sf = ctx.theme.font.smallUi;
                float nx = inner.x + gutter - scrollX_ + ctx.r.measure(line, f).width + 28.f;
                if (isFoldable(i) && folded_[i]) nx += 110.f;       // apres le badge des lignes repliees
                const float nw = ctx.r.measure(note.text, sf).width;
                const float nh = ctx.r.lineHeight(sf);
                const gfx::Color ink = ctx.theme.tone(note.tone, c.textMuted);
                ctx.r.fillRoundedRect({ nx - 6.f, y + 2.f, nw + 12.f, lh - 4.f }, c.headerBg, 4.f);
                ctx.r.fillRect({ nx - 6.f, y + 2.f, 2.f, lh - 4.f }, ink.withAlpha(160));
                ctx.r.drawText({ nx, y + (lh - nh) * 0.5f }, note.text, sf, ink);
                break;
            }
        }

        // --- caret ------------------------------------------------------------
        // Its x is the measured width of the line up to the caret - the very sum
        // the text above was drawn with, so the bar sits exactly between glyphs.
        for (std::size_t row = firstVisible_; row < last; ++row)
            if (visible_[row] == caret_.line) {
                const auto& l = lines_[caret_.line];
                const std::string_view line(buffer_.data() + l.begin, l.end - l.begin);
                paintCaretX_ = ctx.r.measure(line.substr(0, std::min<std::size_t>(caret_.column,
                    line.size())), f).width;
                if (blinkRestart_) { blinkOrigin_ = ctx.time; blinkRestart_ = false; }
                if (focused() && !hasSelection() && std::fmod(ctx.time - blinkOrigin_, 1.0) < 0.5) {
                    const float y = inner.y + static_cast<float>(row - firstVisible_) * lh;
                    const float x = inner.x + gutter + paintCaretX_ - scrollX_;
                    ctx.r.fillRect({ x, y + 1.f, 2.f, lh - 2.f }, c.text);
                }
                break;
            }
        ctx.r.popClip();

        // --- the gutter: line numbers, fold markers, marks ------------------------
        for (std::size_t row = firstVisible_; row < last; ++row) {
            const auto i = visible_[row];
            const float y = inner.y + static_cast<float>(row - firstVisible_) * lh;

            for (const auto& [markedLine, color] : marked_)
                if (markedLine == i) { ctx.r.fillRect({ inner.x, y, 3.f, lh }, color); break; }

            // Lot API 8 : la colonne des points d'arret (breakW = 0 sans elle).
            if (breakGutter_) {
                const float cx = inner.x + breakW * 0.5f + 1.f, cy = y + lh * 0.5f;
                const BreakMark* mark = nullptr;
                for (const auto& b : breaks_)
                    if (b.line == i) { mark = &b; break; }
                if (mark) {
                    ctx.r.fillRoundedRect({ cx - 6.f, cy - 6.f, 12.f, 12.f }, c.error, 6.f);
                    if (!mark->enabled)          // desactive : un anneau
                        ctx.r.fillRoundedRect({ cx - 4.f, cy - 4.f, 8.f, 8.f }, c.panelBg, 4.f);
                    else if (mark->conditional)  // a condition : un point clair au milieu
                        ctx.r.fillRoundedRect({ cx - 2.f, cy - 2.f, 4.f, 4.f }, gfx::Color{ 255, 255, 255, 230 }, 2.f);
                } else if (i == breakHover_ && hovered()) {
                    ctx.r.fillRoundedRect({ cx - 6.f, cy - 6.f, 12.f, 12.f }, c.error.withAlpha(90), 6.f);
                }
                if (i == execLine_) {
                    // La fleche de la ligne d'arret, par-dessus le rond.
                    const gfx::Color amber = c.warning;
                    ctx.r.fillRect({ inner.x + 2.f, cy - 2.5f, breakW * 0.45f, 5.f }, amber);
                    const gfx::Vertex head[3] = { { { inner.x + breakW * 0.45f, cy - 6.5f }, amber },
                                                  { { inner.x + breakW * 0.45f, cy + 6.5f }, amber },
                                                  { { inner.x + breakW - 1.f, cy }, amber } };
                    ctx.r.fillTriangles(head, 3);
                }
            }

            // 1.10 : le numero d'une ligne qui a une faute soulignee, sur fond teinte
            // (rouge s'il y a une erreur, orange pour des avertissements seuls).
            bool faulty = false;
            Tone worst = Tone::Warning;
            for (const auto& s : squiggles_)
                if (s.line == i) { faulty = true; if (s.tone == Tone::Error) worst = Tone::Error; }
            if (faulty && lineNumbers_)
                ctx.r.fillRect({ inner.x + breakW, y, numbers, lh }, ctx.theme.tone(worst, c.error).withAlpha(110));

            if (lineNumbers_) {
                const auto num = std::to_string(i + 1);
                const auto m = ctx.r.measure(num, f);
                ctx.r.drawText({ inner.x + breakW + numbers - m.width - 6.f, y }, num, f,
                    (breakGutter_ && i == execLine_) || faulty ? c.text : c.textDisabled);
            }

            if (isFoldable(i)) {
                const float bx = inner.x + breakW + numbers + 3.f;
                const float by = y + (lh - 11.f) * 0.5f;
                const gfx::Rect box{ bx, by, 11.f, 11.f };
                ctx.r.strokeRect(box, c.textMuted, 1.f);
                ctx.r.line({ box.x + 2.5f, box.y + 5.5f }, { box.right() - 2.5f, box.y + 5.5f },
                    c.text, 1.f);
                if (folded_[i])
                    ctx.r.line({ box.x + 5.5f, box.y + 2.5f }, { box.x + 5.5f, box.bottom() - 2.5f },
                        c.text, 1.f);
            }
        }

        // --- scrollbars --------------------------------------------------------
        const float contentH = static_cast<float>(visible_.size()) * lh;
        if (contentH > inner.h) {
            const float thumbH = std::max(24.f, inner.h * (inner.h / contentH));
            const float maxFirst = static_cast<float>(visible_.size()) - inner.h / lh;
            const float t = maxFirst > 0.f ? static_cast<float>(firstVisible_) / maxFirst : 0.f;
            ctx.r.fillRect({ r.right() - 12.f, inner.y, 12.f, inner.h }, c.panelBg);
            ctx.r.fillRoundedRect({ r.right() - 10.f, inner.y + t * (inner.h - thumbH), 7.f, thumbH },
                vDrag_.active() ? c.scrollbarHover : c.scrollbar, 3.f);
        }
        const float contentW = widest + gutter;
        paintContentW_ = contentW;   // 1.11.4 : la barre du bas se tire
        if (contentW > inner.w) {
            const float thumbW = std::max(24.f, inner.w * (inner.w / contentW));
            const float t = std::min(1.f, scrollX_ / std::max(1.f, contentW - inner.w));
            ctx.r.fillRect({ inner.x, r.bottom() - 12.f, inner.w, 12.f }, c.panelBg);
            ctx.r.fillRoundedRect({ inner.x + t * (inner.w - thumbW), r.bottom() - 10.f, thumbW, 7.f },
                hDrag_.active() ? c.scrollbarHover : c.scrollbar, 3.f);
        }

        // 1.10.1 : le mot (lecture seule, modifie) ne passe plus sur le code. En
        // haut a droite quand les lignes du haut lui laissent la place ; sinon en
        // bas a droite ; sinon dans la barre de defilement du bas (le code n'y est
        // pas lisible) ; sans elle (un editeur etroit et plein), il n'est pas ecrit.
        if (readOnly_ || modified_) {
            const std::string_view tag = readOnly_ ? std::string_view("lecture seule") : std::string_view("modifi\xC3\xA9");
            const auto& tagFont = ctx.theme.font.smallUi;
            const auto m = ctx.r.measure(tag, tagFont);
            const float tagX = r.right() - m.width - 90.f;
            // Le code des lignes qui croisent la bande [top, top + m.height] arrive-t-il sous le mot ?
            const auto clear = [&](float top) {
                for (std::size_t row = firstVisible_; row < last; ++row) {
                    const float y = inner.y + static_cast<float>(row - firstVisible_) * lh;
                    if (y + lh <= top || y >= top + m.height) continue;
                    const auto& l = lines_[visible_[row]];
                    const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
                    if (inner.x + gutter - scrollX_ + ctx.r.measure(text, f).width > tagX - 8.f) return false;
                }
                return true;
            };
            float top = r.y + 6.f;
            bool inBar = false, shown = true;
            if (!clear(top)) {
                const bool hbar = contentW > inner.w;
                const float bottom = r.bottom() - m.height - (hbar ? 18.f : 6.f);
                if (bottom > top + lh && clear(bottom)) top = bottom;
                else if (hbar) { inBar = true; top = r.bottom() - 6.f - m.height * 0.5f; }
                else shown = false;
            }
            // Dans la barre : sur son fond, par-dessus le curseur de defilement.
            if (inBar) ctx.r.fillRect({ tagX - 4.f, r.bottom() - 12.f, m.width + 8.f, 12.f }, c.panelBg);
            if (shown) ctx.r.drawText({ tagX, top }, tag, tagFont, readOnly_ ? c.textDisabled : c.warning);
        }

        if (zoom_ != 1.f) {
            char badge[24];
            std::snprintf(badge, sizeof badge, "%d %%", static_cast<int>(zoom_ * 100.f));
            const auto bm = ctx.r.measure(badge, ctx.theme.font.smallUi);
            ctx.r.fillRect({ r.right() - bm.width - 22.f, r.y + 4.f, bm.width + 10.f, bm.height + 4.f },
                c.headerBg);
            ctx.r.drawText({ r.right() - bm.width - 17.f, r.y + 6.f }, badge,
                ctx.theme.font.smallUi, c.textMuted);
        }
    }

    void MultiLineText::drawCompletion(const PaintContext& ctx) const {
        if (completions_.empty()) return;
        const auto& c = ctx.theme.color;
        const auto  f = scaledFont(ctx.theme.font.ui);
        const float rowH = ctx.r.lineHeight(f) + 6.f;
        // 1.11.2 (API-V, decision 161) : a legend line at the foot as soon as one entry has its badge.
        const bool badges = std::any_of(completions_.begin(), completions_.end(), [](const Completion& k) { return k.badge.set(); });
        completionLegendH_ = badges ? ctx.r.lineHeight(ctx.theme.font.smallUi) + 8.f : 0.f;

        // Widest entry, so the list is not a column of truncated names. 1.10.1 :
        // painted on top of everything, the list may be wider than a narrow
        // editor (up to the window) ; a detail that still does not fit is cut
        // short with an ellipsis, it no longer runs left over the name.
        float width = 160.f;
        for (const auto& item : completions_) {
            const float w = ctx.r.measure(item.text, f).width
                + ctx.r.measure(item.detail, f).width + 60.f;
            width = std::max(width, w);
            if (item.badge.set())
                width = std::max(width, ctx.r.measure(item.badge.legend, ctx.theme.font.smallUi).width + 40.f);
        }
        const auto surface = ctx.r.surfaceSize();
        width = std::min(width, std::max(bounds().w - 20.f, surface.w > 0.f ? surface.w - 16.f : 0.f));

        // Anchored under the caret, flipped above when it would fall off the bottom.
        const auto inner = contentRect();
        float x = inner.x + paintGutter_ + columnX(caret_.line, caret_.column) - scrollX_;
        float y = inner.y + static_cast<float>(caretRow()) * paintLineHeight_ + paintLineHeight_;
        const float height = rowH * static_cast<float>(completions_.size()) + 4.f + completionLegendH_;

        if (x + width > bounds().right()) x = std::max(bounds().x, bounds().right() - width - 4.f);
        if (surface.w > 0.f && x + width > surface.w - 4.f) x = std::max(0.f, surface.w - width - 4.f);
        if (y + height > bounds().bottom())
            y = std::max(bounds().y, y - height - paintLineHeight_);

        completionBox_ = { x, y, width, height };
        ctx.r.fillRect({ completionBox_.x + 2.f, completionBox_.y + 2.f, width, height },
            gfx::Color{ 0, 0, 0, 90 });
        ctx.r.fillRect(completionBox_, c.panelBg);
        ctx.r.strokeRect(completionBox_, c.accent, 1.f);

        for (std::size_t i = 0; i < completions_.size(); ++i) {
            const gfx::Rect row{ completionBox_.x + 2.f,
                                completionBox_.y + 2.f + static_cast<float>(i) * rowH,
                                width - 4.f, rowH };
            const bool selected = i == completionIndex_;
            if (selected) ctx.r.fillRect(row, c.selectionBg);

            float tx = row.x + 6.f;
            if (completions_[i].badge.set()) {
                // 1.11.2 (API-V, decision 161) : what it is and where it comes from, drawn.
                const float side = std::min(rowH - 6.f, 16.f);
                drawKindBadge(ctx.r, completions_[i].badge, { tx, row.y + (rowH - side) * 0.5f, side, side });
                tx += side + 6.f;
            } else if (completions_[i].icon != Icon::None) {
                const float side = std::min(rowH - 6.f, 16.f);
                drawIcon(ctx.r, completions_[i].icon,
                    { tx, row.y + (rowH - side) * 0.5f, side, side },
                    selected ? c.selectionText : c.textMuted);
                tx += side + 6.f;
            }
            ctx.r.drawText({ tx, row.y + 3.f }, completions_[i].text, f,
                selected ? c.selectionText : completions_[i].badge.accent ? completions_[i].badge.color : c.text);
            if (!completions_[i].detail.empty()) {
                // A droite, apres le nom : coupe avec "..." si la place manque.
                const float from = tx + ctx.r.measure(completions_[i].text, f).width + 12.f;
                const float room = row.right() - 8.f - from;
                std::string detail = completions_[i].detail;
                if (ctx.r.measure(detail, f).width > room) {
                    const std::string_view dots = "\xE2\x80\xA6";
                    while (!detail.empty() && ctx.r.measure(detail + std::string(dots), f).width > room) {
                        // un caractere UTF-8 entier : ses octets de suite, puis son octet de tete
                        while (!detail.empty() && (static_cast<unsigned char>(detail.back()) & 0xC0) == 0x80) detail.pop_back();
                        if (!detail.empty()) detail.pop_back();
                    }
                    detail = detail.empty() ? std::string{} : detail + std::string(dots);
                }
                if (!detail.empty()) {
                    const auto m = ctx.r.measure(detail, f);
                    ctx.r.drawText({ std::max(from, row.right() - m.width - 8.f), row.y + 3.f }, detail, f,
                        selected ? c.selectionText : c.textDisabled);
                }
            }
        }
        // 1.11.2 (API-V, decision 161) : the legend of the chosen entry, at the foot.
        if (completionLegendH_ > 0.f && completionIndex_ < completions_.size() && completions_[completionIndex_].badge.set())
            drawBadgeLegend(ctx.r, ctx.theme, completions_[completionIndex_].badge,
                            { completionBox_.x + 2.f, completionBox_.bottom() - 2.f - completionLegendH_, width - 4.f, completionLegendH_ });
    }

    void MultiLineText::onPaintOverlay(const PaintContext& ctx) {
        // Painted in the top-most pass so neither is clipped by the pane the editor
        // sits in - which is exactly where they would land otherwise.
        drawSignature(ctx);
        drawCompletion(ctx);
    }

    gfx::Rect MultiLineText::eventBounds() const {
        if (completions_.empty() || completionBox_.empty()) return bounds();
        const auto b = bounds();
        const float left = std::min(b.x, completionBox_.x);
        const float top = std::min(b.y, completionBox_.y);
        const float right = std::max(b.right(), completionBox_.right());
        const float bottom = std::max(b.bottom(), completionBox_.bottom());
        return { left, top, right - left, bottom - top };
    }

    // ---------------------------------------------------------------- events ---
    // A thin wrapper, because the caret is moved from a dozen different places in
    // the body below - clicks, arrows, typing, selection, completion - and adding
    // the notification to each of them would mean adding it to the next one too.
    // One place that asks "did the symbol change?" after the fact cannot be
    // forgotten.
    EventResult MultiLineText::onEvent(const InputEvent& ev) {
        const auto result = handleEvent(ev);
        notifyCaretSymbol();
        if (result == EventResult::Consumed
            && (std::holds_alternative<KeyDown>(ev) || std::holds_alternative<TextInput>(ev)
                || std::holds_alternative<MouseDown>(ev))) {
            blinkRestart_ = true;
            invalidate();
        }

        // After the body has run, so the double-click has already selected the word
        // and symbolAtCaret() reports what the reader is now looking at rather than
        // what they were looking at before.
        if (const auto* d = std::get_if<MouseDown>(&ev);
            d && d->clickCount >= 2 && bounds().contains(d->pos)
            && !inBreakColumn(d->pos)) {                  // Lot API 8 : deux clics dans la marge n'ouvrent rien
            if (auto symbol = symbolAtCaret(); !symbol.empty()) symbolActivated->emit(symbol);
        }
        return result;
    }

    EventResult MultiLineText::handleEvent(const InputEvent& ev) {
        // 1.11.4 : les barres de defilement (dessinees plus bas dans onPaint) se tirent ; un
        // clic dans la gouttiere avance d'une page. Avant : un appui sur la barre placait le curseur.
        if (paintLineHeight_ > 0.f && !visible_.empty()) {
            const auto r = bounds();
            const auto inner = contentRect();
            const float lh = paintLineHeight_;
            ScrollAxis v;
            v.track = { r.right() - 12.f, inner.y, 12.f, inner.h };
            v.content = static_cast<float>(visible_.size()) * lh;
            v.viewport = inner.h;
            float off = static_cast<float>(firstVisible_) * lh;
            if (vDrag_.handle(*this, v, ev, off)) {
                firstVisible_ = std::min(visible_.size() - 1, static_cast<std::size_t>(std::lround(off / lh)));
                invalidate();
                return EventResult::Consumed;
            }
            ScrollAxis h;
            h.horizontal = true;
            h.track = { inner.x, r.bottom() - 12.f, inner.w, 12.f };
            h.content = paintContentW_;
            h.viewport = inner.w;
            float x = scrollX_;
            if (hDrag_.handle(*this, h, ev, x)) {
                scrollX_ = x;
                invalidate();
                return EventResult::Consumed;
            }
        }
        const auto inner = contentRect();

        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            // The completion list is drawn outside the widget, so it is tested first
            // and against its own rectangle.
            if (completionOpen() && completionBox_.contains(d->pos)) {
                const float rowH = (completionBox_.h - 4.f - completionLegendH_)
                    / static_cast<float>(std::max<std::size_t>(1, completions_.size()));
                const auto i = static_cast<std::size_t>(std::max(0.f, (d->pos.y - completionBox_.y - 2.f) / rowH));
                // 1.11.2 : the legend at the foot is not an entry (a click on it chooses nothing).
                if (i >= completions_.size()) return EventResult::Consumed;
                completionIndex_ = i;
                acceptCompletion();
                return EventResult::Consumed;
            }
            if (!bounds().contains(d->pos)) { dismissCompletion(); return EventResult::Ignored; }
            grabFocus();
            dismissCompletion();

            // Lot API 8 : un clic dans la colonne des points d'arret les demande
            // (Maj : activer / desactiver) ; le curseur ne bouge pas.
            if (inBreakColumn(d->pos)) {
                if (d->button == MouseButton::Left && !lines_.empty()) {
                    const auto line = lineAt(d->pos.y);
                    if (d->mods.shift) breakpointEnableToggled->emit(line);
                    else breakpointToggled->emit(line);
                } else if (d->button == MouseButton::Right && !lines_.empty()) {
                    breakpointConditionRequested->emit(lineAt(d->pos.y));   // Lot API 8 (2e partie) : sa condition
                }
                return EventResult::Consumed;
            }

            // A click in the marker column folds instead of moving the caret,
            // using the same column the marker was drawn in.
            if (d->pos.x >= inner.x + paintBreak_ + paintNumbers_ && d->pos.x < inner.x + paintGutter_) {
                toggleFold(lineAt(d->pos.y));
                return EventResult::Consumed;
            }

            caret_ = caretAt(d->pos);
            if (!d->mods.shift) anchor_ = caret_;
            selecting_ = true;
            updateSignature();
            if (d->clickCount >= 2 && isFoldable(caret_.line)) toggleFold(caret_.line);
            invalidate();
            return EventResult::Consumed;
        }

        // Lot API 8 : la colonne des points d'arret sous la souris - le rond
        // fantome ; la marge n'est pas un nom (pas de valeur au survol).
        if (const auto* m = std::get_if<MouseMove>(&ev); m && breakGutter_) {
            const auto over = inBreakColumn(m->pos) && !lines_.empty() ? lineAt(m->pos.y) : std::string::npos;
            if (over != breakHover_) {
                breakHover_ = over;
                invalidate();
            }
            if (over != std::string::npos && !selecting_) {
                if (!hoverSymbol_.empty()) {
                    hoverSymbol_.clear();
                    setTooltip({});
                }
                return EventResult::Ignored;
            }
        }

        // Hovering asks the provider what the symbol holds. Only when the symbol
        // changes, so a value that updates every scan does not fight the tooltip's
        // dwell timer.
        if (const auto* m = std::get_if<MouseMove>(&ev); m && valueProvider_ && !selecting_) {
            const auto symbol = symbolAt(m->pos);
            if (symbol != hoverSymbol_) {
                hoverSymbol_ = symbol;
                std::string text;
                setTooltip(!symbol.empty() && valueProvider_(symbol, text) ? text : std::string{});
            }
        }

        if (const auto* m = std::get_if<MouseMove>(&ev); m && selecting_) {
            caret_ = caretAt(m->pos);
            invalidate();
            return EventResult::Consumed;
        }
        if (std::get_if<MouseUp>(&ev) && selecting_) {
            selecting_ = false;
            return EventResult::Consumed;
        }

        if (const auto* w = std::get_if<MouseWheel>(&ev)) {
            if (!bounds().contains(w->pos)) return EventResult::Ignored;
            if (w->mods.ctrl) { setZoom(zoom_ + (w->dy > 0 ? 0.25f : -0.25f)); return EventResult::Consumed; }
            if (w->mods.shift) {
                scrollX_ = std::max(0.f, scrollX_ - w->dy * 48.f);
                invalidate();
                return EventResult::Consumed;
            }
            const std::size_t step = 3;
            if (w->dy > 0) firstVisible_ = firstVisible_ > step ? firstVisible_ - step : 0;
            else if (w->dy < 0 && !visible_.empty())
                firstVisible_ = std::min(visible_.size() - 1, firstVisible_ + step);
            invalidate();
            return EventResult::Consumed;
        }

        // Typed characters, once the widget is editable.
        if (const auto* t = std::get_if<TextInput>(&ev); t && focused() && !readOnly_) {
            insertText(t->utf8);
            return EventResult::Consumed;
        }

        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && !lines_.empty()) {
            // The completion list owns the keys that drive it, and only those.
            if (completionOpen()) {
                switch (k->key) {
                case Key::Down:
                    completionIndex_ = (completionIndex_ + 1) % completions_.size();
                    invalidate();
                    return EventResult::Consumed;
                case Key::Up:
                    completionIndex_ = completionIndex_ == 0 ? completions_.size() - 1
                        : completionIndex_ - 1;
                    invalidate();
                    return EventResult::Consumed;
                case Key::Return: {
                    // Le mot tape est deja le mot propose (THEN, END_IF...) et ce
                    // n'est pas une structure a deployer : Entree va a la ligne.
                    const auto& chosen = completions_[std::min(completionIndex_, completions_.size() - 1)];
                    if ((chosen.insert.empty() || chosen.insert == chosen.text) && chosen.extend == 0
                        && completionPrefix() == chosen.text) {
                        dismissCompletion();
                        break;                                  // la suite : insertNewline
                    }
                    acceptCompletion();
                    return EventResult::Consumed;
                }
                case Key::Tab:
                    acceptCompletion();
                    return EventResult::Consumed;
                case Key::Escape:
                    dismissCompletion();
                    return EventResult::Consumed;
                default: break;
                }
            }
            if (k->mods.ctrl && k->key == Key::Space) { requestCompletion(); return EventResult::Consumed; }
            // Lot API 8 : F9 (Ctrl+F9 : activer / desactiver) sur la ligne du
            // curseur - seulement avec la colonne des points d'arret ; sinon F9
            // passe (l'ecran y ouvre la simulation).
            if (breakGutter_ && k->key == Key::F9 && !k->mods.alt && !k->mods.shift && !k->repeat) {
                if (k->mods.ctrl) breakpointEnableToggled->emit(caret_.line);
                else breakpointToggled->emit(caret_.line);
                return EventResult::Consumed;
            }

            if (!readOnly_) {
                switch (k->key) {
                case Key::Backspace: deleteBefore();  return EventResult::Consumed;
                case Key::Delete:    deleteAfter();   return EventResult::Consumed;
                case Key::Return:    insertNewline(); return EventResult::Consumed;
                case Key::Tab:
                    insertText(tabSpaces_ > 0 ? std::string(static_cast<std::size_t>(tabSpaces_), ' ')
                        : std::string("\t"));
                    return EventResult::Consumed;
                default: break;
                }
            }

            auto moveCaret = [&](std::int64_t deltaLines) {
                // Up and Down keep the caret's x, not its byte count: the glyphs
                // are not all as wide, and a byte count can land inside an
                // accented character.
                const float wanted = columnX(caret_.line, caret_.column);
                const auto target = static_cast<std::int64_t>(caret_.line) + deltaLines;
                caret_.line = static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(target, 0, static_cast<std::int64_t>(lines_.size()) - 1));
                caret_.column = columnAtX(caret_.line, wanted);
                if (!k->mods.shift) anchor_ = caret_;
                ensureCaretVisible();
                invalidate();
                };

            if (k->mods.ctrl && k->key == Key::A) { selectAll(); return EventResult::Consumed; }
            if (k->mods.ctrl && k->key == Key::C) { copySelection(); return EventResult::Consumed; }
            if (k->mods.ctrl && k->key == Key::V && !readOnly_) {
                auto paste = clipboardText();
                std::erase(paste, '\r');
                if (!paste.empty()) insertText(paste);
                return EventResult::Consumed;
            }
            if (k->mods.ctrl && k->key == Key::X && !readOnly_ && hasSelection()) {
                copySelection();
                deleteSelection();
                return EventResult::Consumed;
            }

            switch (k->key) {
            case Key::Down:     moveCaret(1);   return EventResult::Consumed;
            case Key::Up:       moveCaret(-1);  return EventResult::Consumed;
            case Key::PageDown: moveCaret(20);  return EventResult::Consumed;
            case Key::PageUp:   moveCaret(-20); return EventResult::Consumed;
            case Key::Home:
                // Ctrl+Debut : le debut du texte (la premiere ligne, colonne 0).
                if (k->mods.ctrl) moveCaret(-static_cast<std::int64_t>(lines_.size()));
                caret_.column = 0;
                if (!k->mods.shift) anchor_ = caret_;
                ensureCaretVisible();
                invalidate();
                return EventResult::Consumed;
            case Key::End:
                // Ctrl+Fin : la fin du texte (la derniere ligne, au bout) - elle gardait la colonne.
                if (k->mods.ctrl) moveCaret(static_cast<std::int64_t>(lines_.size()));
                caret_.column = lines_[caret_.line].end - lines_[caret_.line].begin;
                if (!k->mods.shift) anchor_ = caret_;
                ensureCaretVisible();
                invalidate();
                return EventResult::Consumed;
            case Key::Left:
                // One CHARACTER back, never into the middle of a UTF-8 sequence:
                // an accented letter is two bytes, and typing between them broke it.
                if (caret_.column) {
                    const auto begin = lines_[caret_.line].begin;
                    std::uint32_t column = caret_.column - 1;
                    while (column > 0 && isContinuation(buffer_[begin + column])) --column;
                    caret_.column = column;
                }
                else if (caret_.line) { --caret_.line; caret_.column = lines_[caret_.line].end - lines_[caret_.line].begin; }
                if (!k->mods.shift) anchor_ = caret_;
                ensureCaretVisible();
                invalidate();
                return EventResult::Consumed;
            case Key::Right: {
                const auto begin = lines_[caret_.line].begin;
                const auto len = lines_[caret_.line].end - begin;
                if (caret_.column < len) {
                    std::uint32_t column = caret_.column + 1;
                    while (column < len && isContinuation(buffer_[begin + column])) ++column;
                    caret_.column = column;
                }
                else if (caret_.line + 1 < lines_.size()) { ++caret_.line; caret_.column = 0; }
                if (!k->mods.shift) anchor_ = caret_;
                ensureCaretVisible();
                invalidate();
                return EventResult::Consumed;
            }
            case Key::Space:
                if (k->mods.ctrl) { toggleFold(caret_.line); return EventResult::Consumed; }
                break;
            case Key::Escape:
                if (hasSelection()) { clearSelection(); return EventResult::Consumed; }
                break;
            default: break;
            }
        }
        return EventResult::Ignored;
    }

    // ============================================================== Checkbox ====
    Checkbox::Checkbox(std::string label, std::string id)
        : Widget(std::move(id)), label_(std::move(label)) {
        setFocusPolicy(true);
    }

    void Checkbox::setState(State s) {
        if (state_ == s) return;
        state_ = s;
        invalidate();
        stateChanged->emit(s);
    }

    SizeHint Checkbox::sizeHint() const {
        const float line = lineHeight(kFontUi);
        SizeHint h;
        h.preferred = { measureWidth(label_, kFontUi) + 28.f, line + 8.f };
        h.minimum = { 24.f, line + 6.f };
        return h;
    }

    void Checkbox::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();
        const float box = 13.f;
        const gfx::Rect boxRect{ r.x + 2.f, r.y + (r.h - box) * 0.5f, box, box };

        ctx.r.fillRect(boxRect, enabled() ? c.inputBg : c.panelBg);
        ctx.r.strokeRect(boxRect, hovered() ? c.accentHover : c.border, 1.f);
        if (focused()) drawFocusRing(ctx, r);

        if (state_ == State::Checked) {
            // Two strokes rather than a glyph: no font dependency for the tick.
            const gfx::Color tick = enabled() ? c.accent : c.textDisabled;
            ctx.r.line({ boxRect.x + 3.f, boxRect.y + 6.5f }, { boxRect.x + 5.5f, boxRect.y + 9.5f }, tick, 2.f);
            ctx.r.line({ boxRect.x + 5.5f, boxRect.y + 9.5f }, { boxRect.x + 10.f, boxRect.y + 3.5f }, tick, 2.f);
        }
        else if (state_ == State::Mixed) {
            ctx.r.fillRect(boxRect.inset(3.f, 5.f), c.accent);
        }

        ctx.r.drawText({ boxRect.right() + 6.f, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f },
            label_, ctx.theme.font.ui, enabled() ? c.text : c.textDisabled);
    }

    EventResult Checkbox::onEvent(const InputEvent& ev) {
        auto cycle = [this] {
            if (!tristate_) setState(state_ == State::Checked ? State::Unchecked : State::Checked);
            else setState(state_ == State::Unchecked ? State::Checked
                : state_ == State::Checked ? State::Mixed : State::Unchecked);
            };
        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (d->button == MouseButton::Left && bounds().contains(d->pos)) {
                grabFocus();
                cycle();
                return EventResult::Consumed;
            }
        }
        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && k->key == Key::Space) {
            cycle();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }

    // =========================================================== RadioButton ====
    void RadioGroup::attach(RadioButton* b) { members_.push_back(b); }
    void RadioGroup::detach(RadioButton* b) {
        members_.erase(std::remove(members_.begin(), members_.end(), b), members_.end());
    }
    void RadioGroup::setValue(int v) {
        if (value_ == v) return;
        value_ = v;
        for (auto* m : members_) m->invalidate();
        valueChanged->emit(v);
    }

    RadioButton::RadioButton(std::string label, std::shared_ptr<RadioGroup> group, int value, std::string id)
        : Widget(std::move(id)), label_(std::move(label)), group_(std::move(group)), value_(value) {
        setFocusPolicy(true);
        if (group_) group_->attach(this);
    }

    RadioButton::~RadioButton() { if (group_) group_->detach(this); }

    bool RadioButton::selected() const noexcept { return group_ && group_->value() == value_; }
    void RadioButton::select() { if (group_) group_->setValue(value_); }

    void RadioButton::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();
        const float box = 13.f;
        const gfx::Rect dot{ r.x + 2.f, r.y + (r.h - box) * 0.5f, box, box };

        ctx.r.fillRoundedRect(dot, enabled() ? c.inputBg : c.panelBg, box * 0.5f);
        ctx.r.strokeRect(dot, hovered() ? c.accentHover : c.border, 1.f);
        if (selected()) ctx.r.fillRoundedRect(dot.inset(3.5f, 3.5f), c.accent, 3.f);
        if (focused()) drawFocusRing(ctx, r);

        ctx.r.drawText({ dot.right() + 6.f, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f },
            label_, ctx.theme.font.ui, enabled() ? c.text : c.textDisabled);
    }

    EventResult RadioButton::onEvent(const InputEvent& ev) {
        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (d->button == MouseButton::Left && bounds().contains(d->pos)) {
                grabFocus();
                select();
                return EventResult::Consumed;
            }
        }
        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && k->key == Key::Space) {
            select();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }

    // ========================================================== ToggleButton ====
    ToggleButton::ToggleButton(std::string text, std::string id)
        : Widget(std::move(id)), text_(std::move(text)) {
        setFocusPolicy(true);
    }

    SizeHint ToggleButton::sizeHint() const {
        const float line = lineHeight(kFontUi);
        SizeHint h;
        h.preferred = { measureWidth(text_, kFontUi) + 24.f, line + 12.f };
        h.minimum = { 40.f, line + 8.f };
        return h;
    }

    void ToggleButton::setChecked(bool c) {
        if (checked_ == c) return;
        checked_ = c;
        invalidate();
        toggled->emit(c);
    }

    void ToggleButton::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();
        const gfx::Color fill = checked_ ? c.accent : hovered() ? c.rowAltBg : c.panelBg;
        ctx.r.fillRoundedRect(r, fill, ctx.theme.metric.radius);
        ctx.r.strokeRect(r, c.border, 1.f);
        if (focused()) drawFocusRing(ctx, r);

        const auto m = ctx.r.measure(text_, ctx.theme.font.ui);
        ctx.r.drawText({ r.x + (r.w - m.width) * 0.5f, r.y + (r.h - m.height) * 0.5f },
            text_, ctx.theme.font.ui, checked_ ? c.textInverted : c.text);
    }

    EventResult ToggleButton::onEvent(const InputEvent& ev) {
        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (d->button == MouseButton::Left && bounds().contains(d->pos)) {
                grabFocus();
                setChecked(!checked_);
                return EventResult::Consumed;
            }
        }
        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && k->key == Key::Space) {
            setChecked(!checked_);
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }

    // ============================================================== DropDown ====
    DropDown::DropDown(std::string id) : Widget(std::move(id)) {
        setFocusPolicy(true);
        setPadding({ 3.f, 8.f, 3.f, 8.f });
    }

    SizeHint DropDown::sizeHint() const {
        float widest = 0.f;
        for (const auto& i : items_) widest = std::max(widest, measureWidth(i.label, kFontUi));
        const float line = lineHeight(kFontUi);
        SizeHint h;
        h.preferred = { widest + 40.f, line + 10.f };   // +chevron
        h.minimum = { 80.f, line + 8.f };
        h.stretchY = 0.f;
        return h;
    }

    void DropDown::setItems(std::vector<Item> items) {
        items_ = std::move(items);
        if (selected_ >= static_cast<int>(items_.size())) selected_ = items_.empty() ? -1 : 0;
        invalidate();
    }

    void DropDown::setSelectedIndex(int i) {
        if (i == selected_ || i < -1 || i >= static_cast<int>(items_.size())) return;
        selected_ = i;
        invalidate();
        selectionChanged->emit(i);
    }

    const DropDown::Item* DropDown::selectedItem() const {
        return (selected_ >= 0 && selected_ < static_cast<int>(items_.size())) ? &items_[static_cast<std::size_t>(selected_)]
            : nullptr;
    }

    // ---------------------------------------------------------------------------
    //  THE POPUP LIST.
    //
    //  What was here before opened straight down, with a height of one row per item
    //  and no upper bound. With eight elementary types that is fine. With a project
    //  that has sixty derived types it is a column that leaves the bottom of the
    //  screen, and the entries past the edge cannot be reached at all - there was no
    //  scrolling either, and the widget did not know how big the window was.
    //
    //  Three things fix it, and all three are needed:
    //   * a CAP on how many rows are shown, so the list is scannable;
    //   * SCROLLING, so the rest is still reachable;
    //   * FLIPPING above the field when there is more room up there than down,
    //     because a combo near the bottom of a dialog is the common case, not the
    //     exception.
    //
    //  And while it is open it OWNS THE POINTER. eventBounds() covers the surface,
    //  so a click anywhere dismisses it and a wheel anywhere scrolls the list rather
    //  than the panel underneath. Before, eventBounds() was the field plus the
    //  popup, Widget::dispatch rejected everything else before onEvent ever ran, and
    //  the dismiss branch inside onEvent was unreachable for exactly the case it was
    //  written for.
    // ---------------------------------------------------------------------------
    namespace {
        constexpr float kListPadding = 1.f;
        constexpr float kScrollbarWidth = 8.f;
    }

    void DropDown::setMaxVisibleRows(int rows) {
        maxVisibleRows_ = rows < 1 ? 1 : rows;
        invalidate();
    }

    int DropDown::visibleRows() const {
        const int total = static_cast<int>(items_.size());
        if (total == 0) return 0;

        int cap = std::min(total, maxVisibleRows_);

        // However tall the cap says, it cannot be taller than the room available on
        // the side it will open. Below first, then above, and the popup goes to
        // whichever holds more.
        if (const auto surface = surfaceSize(); surface.h > 0.f) {
            const auto  b = bounds();
            const float below = surface.h - b.bottom() - 4.f;
            const float above = b.y - 4.f;
            const float room = std::max(below, above);
            const int   fits = static_cast<int>((room - 2.f * kListPadding) / rowHeight_);
            cap = std::max(1, std::min(cap, fits));
        }
        return cap;
    }

    float DropDown::maxScroll() const {
        const float content = rowHeight_ * static_cast<float>(items_.size());
        const float shown = rowHeight_ * static_cast<float>(visibleRows());
        return std::max(0.f, content - shown);
    }

    gfx::Rect DropDown::popupRect() const {
        const auto  b = bounds();
        const float height = rowHeight_ * static_cast<float>(visibleRows()) + 2.f * kListPadding;

        float y = b.bottom();
        flipped_ = false;
        if (const auto surface = surfaceSize(); surface.h > 0.f && y + height > surface.h) {
            // Not enough room below. Above, if that is better; otherwise pin it to
            // the bottom edge rather than let it hang off.
            if (b.y - height >= 0.f) { y = b.y - height; flipped_ = true; }
            else { y = std::max(0.f, surface.h - height); }
        }

        float x = b.x;
        float w = b.w;
        if (const auto surface = surfaceSize(); surface.w > 0.f) {
            w = std::min(w, surface.w);
            if (x + w > surface.w) x = std::max(0.f, surface.w - w);
        }
        return { x, y, w, height };
    }

    gfx::Rect DropDown::scrollbarRect() const {
        const auto popup = popupRect();
        return { popup.right() - kScrollbarWidth - kListPadding, popup.y + kListPadding,
                kScrollbarWidth, popup.h - 2.f * kListPadding };
    }

    gfx::Rect DropDown::eventBounds() const {
        if (!popupOpen_) return bounds();
        if (const auto surface = surfaceSize(); surface.w > 0.f && surface.h > 0.f)
            return { 0.f, 0.f, surface.w, surface.h };
        // No surface to claim: fall back to the field plus the list, which is the
        // old behaviour and still better than nothing.
        const auto b = bounds(), pr = popupRect();
        const float top = std::min(b.y, pr.y), bot = std::max(b.bottom(), pr.bottom());
        return { std::min(b.x, pr.x), top, std::max(b.w, pr.w), bot - top };
    }

    void DropDown::openPopup() {
        popupOpen_ = true;
        highlighted_ = selected_;
        scrollTo(selected_);          // open showing what is currently chosen
        invalidate();
    }

    void DropDown::closePopup() {
        popupOpen_ = false;
        highlighted_ = -1;
        invalidate();
    }

    void DropDown::scrollTo(int row) {
        if (row < 0 || items_.empty()) { scroll_ = 0.f; return; }
        const float top = rowHeight_ * static_cast<float>(row);
        const float bottom = top + rowHeight_;
        const float window = rowHeight_ * static_cast<float>(visibleRows());
        if (top < scroll_)             scroll_ = top;
        else if (bottom > scroll_ + window) scroll_ = bottom - window;
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    int DropDown::rowAt(gfx::Point global) const {
        const auto popup = popupRect();
        if (!popup.contains(global)) return -1;
        const int i = static_cast<int>((global.y - popup.y - kListPadding + scroll_) / rowHeight_);
        return (i >= 0 && i < static_cast<int>(items_.size())) ? i : -1;
    }

    void DropDown::moveHighlight(int delta) {
        if (items_.empty()) return;
        const int n = static_cast<int>(items_.size());
        int i = highlighted_ < 0 ? selected_ : highlighted_;
        for (int step = 0; step < n; ++step) {
            i = (i < 0) ? (delta > 0 ? 0 : n - 1) : (i + delta + n) % n;
            if (items_[static_cast<std::size_t>(i)].enabled) {
                highlighted_ = i;
                scrollTo(i);
                invalidate();
                return;
            }
        }
    }

    void DropDown::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  r = bounds();

        ctx.r.fillRect(r, enabled() ? c.inputBg : c.panelBg);
        ctx.r.strokeRect(r, (focused() || popupOpen_) ? c.accent : c.border, 1.f);

        const auto inner = contentRect();
        const auto* item = selectedItem();
        // The chevron owns the right-hand end; clipping the label there stops a long
        // type name from running underneath it.
        const float textRoom = std::max(0.f, inner.w - 16.f);
        std::string label = item ? item->label : std::string{};
        if (!label.empty() && measureWidth(label, kFontUi) > textRoom) {
            const auto fit = ctx.r.fitCharacters(label, kFontUi, std::max(0.f, textRoom - 12.f));
            label = label.substr(0, fit) + "...";
        }
        ctx.r.drawText({ inner.x, inner.y + (inner.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f },
            label, ctx.theme.font.ui, enabled() ? c.text : c.textDisabled);

        // Chevron, drawn rather than glyphed. It points the way the list will open.
        const float cx = r.right() - 12.f, cy = r.y + r.h * 0.5f;
        const gfx::Color arrow = enabled() ? c.textMuted : c.textDisabled;
        const float dir = (popupOpen_ && flipped_) ? -1.f : 1.f;
        ctx.r.line({ cx - 4.f, cy - 2.f * dir }, { cx, cy + 2.f * dir }, arrow, 1.5f);
        ctx.r.line({ cx, cy + 2.f * dir }, { cx + 4.f, cy - 2.f * dir }, arrow, 1.5f);
    }

    void DropDown::onPaintOverlay(const PaintContext& ctx) {
        if (!popupOpen_ || items_.empty()) return;
        const auto& c = ctx.theme.color;
        rowHeight_ = ctx.theme.metric.rowHeight;
        const gfx::Rect popup = popupRect();
        const bool scrolls = maxScroll() > 0.f;

        ctx.r.fillRect({ popup.x + 2.f, popup.y + 2.f, popup.w, popup.h }, gfx::Color{ 0, 0, 0, 90 });
        ctx.r.fillRect(popup, c.panelBg);
        ctx.r.strokeRect(popup, c.borderStrong, 1.f);

        // Rows are clipped to the list, so a partially scrolled row is cut rather
        // than drawn over the frame.
        const gfx::Rect view{ popup.x + kListPadding, popup.y + kListPadding,
                             popup.w - 2.f * kListPadding - (scrolls ? kScrollbarWidth : 0.f),
                             popup.h - 2.f * kListPadding };
        ctx.r.pushClip(view);

        const int first = static_cast<int>(scroll_ / rowHeight_);
        const int last = std::min(static_cast<int>(items_.size()),
            first + visibleRows() + 1);
        const float lh = ctx.r.lineHeight(ctx.theme.font.ui);

        for (int i = std::max(0, first); i < last; ++i) {
            const auto  u = static_cast<std::size_t>(i);
            const gfx::Rect row{ view.x, view.y + static_cast<float>(i) * rowHeight_ - scroll_,
                                view.w, rowHeight_ };
            if (i == highlighted_ && items_[u].enabled) ctx.r.fillRect(row, c.selectionBg);
            else if (i == selected_)                    ctx.r.fillRect(row, c.rowAltBg);

            ctx.r.drawText({ row.x + 6.f, row.y + (rowHeight_ - lh) * 0.5f }, items_[u].label,
                ctx.theme.font.ui,
                !items_[u].enabled ? c.textDisabled
                : (i == highlighted_ ? c.selectionText : c.text));
        }
        ctx.r.popClip();

        if (!scrolls) return;
        // A list that scrolls must look like one. Without the thumb there is nothing
        // on screen to say the twelve rows are not all of them.
        const auto bar = scrollbarRect();
        ctx.r.fillRect(bar, c.panelBg);
        const float content = rowHeight_ * static_cast<float>(items_.size());
        const float fraction = (content > 0.f) ? (bar.h / content) * bar.h : bar.h;
        const float thumbH = std::max(18.f, fraction);
        const float travel = bar.h - thumbH;
        const float thumbY = bar.y + travel * (scroll_ / maxScroll());
        ctx.r.fillRoundedRect({ bar.x + 1.f, thumbY, bar.w - 2.f, thumbH }, barDrag_.active() ? c.scrollbarHover : c.border, 3.f);
    }

    EventResult DropDown::onEvent(const InputEvent& ev) {
        if (popupOpen_) {
            // Everything is consumed while the list is up. A wheel that reaches the
            // panel underneath scrolls the dialog out from under its own dropdown,
            // and a move that reaches it highlights rows in a table nobody is
            // looking at.
            if (const auto* w = std::get_if<MouseWheel>(&ev)) {
                const float step = rowHeight_ * 3.f;
                scroll_ = std::clamp(scroll_ - w->dy * step, 0.f, maxScroll());
                invalidate();
                return EventResult::Consumed;
            }
            // 1.11.4 : le pouce se prend et se tire ; un clic dans la gouttiere avance d'une page.
            if (maxScroll() > 0.f) {
                ScrollAxis a;
                a.track = scrollbarRect();
                a.content = rowHeight_ * static_cast<float>(items_.size());
                a.viewport = rowHeight_ * static_cast<float>(visibleRows());
                a.minThumb = 18.f;
                float off = scroll_;
                if (barDrag_.handle(*this, a, ev, off)) {
                    scroll_ = std::clamp(off, 0.f, maxScroll());
                    invalidate();
                    return EventResult::Consumed;
                }
            }
            if (const auto* m = std::get_if<MouseMove>(&ev)) {
                const int i = rowAt(m->pos);
                const int next = (i >= 0 && items_[static_cast<std::size_t>(i)].enabled) ? i : -1;
                if (next != highlighted_) { highlighted_ = next; invalidate(); }
                return EventResult::Consumed;
            }
            if (std::get_if<MouseUp>(&ev)) return EventResult::Consumed;

            if (const auto* d = std::get_if<MouseDown>(&ev)) {
                if (maxScroll() > 0.f && scrollbarRect().contains(d->pos)) {
                    const auto bar = scrollbarRect();
                    const float t = (d->pos.y - bar.y) / std::max(1.f, bar.h);
                    scroll_ = std::clamp(t * maxScroll(), 0.f, maxScroll());
                    invalidate();
                    return EventResult::Consumed;
                }
                const int i = rowAt(d->pos);
                if (i >= 0) {
                    if (items_[static_cast<std::size_t>(i)].enabled) {
                        setSelectedIndex(i);
                        closePopup();
                    }
                    return EventResult::Consumed;   // a disabled row eats its click
                }
                // Anywhere else dismisses, and the click goes no further: the first
                // click closes the list, the second does whatever it was aimed at.
                closePopup();
                return EventResult::Consumed;
            }
            if (const auto* k = std::get_if<KeyDown>(&ev)) {
                switch (k->key) {
                case Key::Escape: closePopup();      return EventResult::Consumed;
                case Key::Down:   moveHighlight(1);  return EventResult::Consumed;
                case Key::Up:     moveHighlight(-1); return EventResult::Consumed;
                case Key::Home:   highlighted_ = 0; scrollTo(0); invalidate();
                    return EventResult::Consumed;
                case Key::End:    highlighted_ = static_cast<int>(items_.size()) - 1;
                    scrollTo(highlighted_); invalidate();
                    return EventResult::Consumed;
                case Key::Space:
                case Key::Return:
                    if (highlighted_ >= 0) setSelectedIndex(highlighted_);
                    closePopup();
                    return EventResult::Consumed;
                default:
                    // An open list does not let Ctrl+S through to the window
                    // behind it.
                    return EventResult::Consumed;
                }
            }
            return EventResult::Consumed;
        }

        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (bounds().contains(d->pos)) {
                grabFocus();
                openPopup();
                return EventResult::Consumed;
            }
        }
        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused()) {
            // Closed, the arrows step the selection, which is what a combo box does
            // and what keyboard-only entry depends on.
            if (k->key == Key::Down) {
                setSelectedIndex(std::min<int>(selected_ + 1, static_cast<int>(items_.size()) - 1));
                return EventResult::Consumed;
            }
            if (k->key == Key::Up) {
                setSelectedIndex(std::max(selected_ - 1, 0));
                return EventResult::Consumed;
            }
            if (k->key == Key::Space || k->key == Key::Return) {
                openPopup();
                return EventResult::Consumed;
            }
        }
        return EventResult::Ignored;
    }

    // ============================================================= PopupMenu ====
    //
    //  A NOTE ON DropDown, FOUND WHILE BUILDING THIS.
    //
    //  DropDown::eventBounds() returns its button plus its popup. Widget::dispatch
    //  tests that rectangle before calling onEvent(), so a click anywhere else on
    //  the window never reaches the DropDown at all and the popup stays open on top
    //  of a window that has already reacted to the click. The dismiss branch inside
    //  DropDown::onEvent is unreachable for exactly the case it was written for.
    //
    //  PopupMenu does not repeat it: while open, its event rectangle is the whole
    //  surface. That is not a trick, it is what a context menu is - it owns the
    //  pointer until it is dismissed. The same one-line change would fix DropDown,
    //  but it is a behaviour change to a widget with its own tests, so it is
    //  written down here rather than slipped in alongside an unrelated feature.

    namespace {
        constexpr float kMenuPadding = 4.f;    // between the frame and the first item
        constexpr float kMenuIconGutter = 22.f;   // reserved even when an item has no icon,
        // so labels line up down the column
        constexpr float kSeparatorHeight = 7.f;
        constexpr float kSubmenuArrow = 16.f;     // lot 7 : la fleche d'une entree a sous-menu

        float menuRowHeight(const PopupMenu::Item& it, float rowHeight) {
            return it.separator ? kSeparatorHeight : rowHeight;
        }

        // Lot 7 : la largeur d'une colonne d'entrees - le menu, ou un sous-menu.
        float menuWidth(const std::vector<PopupMenu::Item>& items) {
            float widest = 0.f;
            for (const auto& it : items) {
                if (it.separator) continue;
                float w = measureWidth(it.label, kFontUi) + (it.heading ? 0.f : kMenuIconGutter);
                if (!it.shortcut.empty())       w += 18.f + measureWidth(it.shortcut, kFontUi);
                if (!it.enabled && !it.disabledReason.empty())
                    w += 12.f + measureWidth(it.disabledReason, kFontUi);
                if (!it.children.empty())       w += kSubmenuArrow;
                widest = std::max(widest, w);
            }
            return widest + 2.f * kMenuPadding + 12.f;
        }

        float menuHeight(const std::vector<PopupMenu::Item>& items, float rowHeight) {
            float height = 2.f * kMenuPadding;
            for (const auto& it : items) height += menuRowHeight(it, rowHeight);
            return height;
        }

        // Une entree qui se choisit : ni regle, ni titre, ni grisee.
        bool choosable(const PopupMenu::Item& it) { return !it.separator && !it.heading && it.enabled; }
    } // namespace

    PopupMenu::PopupMenu(std::string id) : Widget(std::move(id)) {
        setFocusPolicy(true);
        setVisibility(Visibility::Collapsed);   // it occupies no space in a layout
    }

    void PopupMenu::setItems(std::vector<Item> items) {
        items_ = std::move(items);
        highlighted_ = -1;
        submenu_ = subHighlighted_ = -1;
        subKeyboard_ = false;
        invalidate();
    }

    SizeHint PopupMenu::sizeHint() const {
        // It is never laid out; it draws over everything. Claiming no space keeps it
        // out of a BoxLayout's arithmetic, which would otherwise shrink its siblings
        // to make room for something that is not there.
        return SizeHint{};
    }

    float PopupMenu::itemHeight(std::size_t i) const {
        return (i < items_.size() && items_[i].separator) ? kSeparatorHeight : rowHeight_;
    }

    gfx::Rect PopupMenu::popupRect() const {
        if (items_.empty()) return {};

        const float width = menuWidth(items_);
        const float height = menuHeight(items_, rowHeight_);

        // Keep it on screen. Flipping about the cursor is what every desktop does,
        // and it beats clamping: a clamped menu ends up under the pointer with the
        // wrong entry highlighted.
        float x = anchor_.x;
        float y = anchor_.y;
        if (surface_.w > 0.f && x + width > surface_.w) x = std::max(0.f, anchor_.x - width);
        if (surface_.h > 0.f && y + height > surface_.h) y = std::max(0.f, anchor_.y - height);
        return { x, y, width, height };
    }

    // ---- lot 7 : le sous-menu ---------------------------------------------------
    //  A droite du menu, sa premiere entree en face de celle qui l'ouvre ; a
    //  gauche quand il ne tient pas a droite, remonte quand il deborde en bas.
    bool PopupMenu::hasSubmenu(int index) const noexcept {
        return index >= 0 && static_cast<std::size_t>(index) < items_.size()
            && !items_[static_cast<std::size_t>(index)].children.empty();
    }

    gfx::Rect PopupMenu::submenuRect() const {
        if (!open_ || !hasSubmenu(submenu_)) return {};
        const auto& kids = items_[static_cast<std::size_t>(submenu_)].children;
        const gfx::Rect box = popupRect();
        const gfx::Rect parent = itemRect(static_cast<std::size_t>(submenu_));
        const float width = menuWidth(kids);
        const float height = menuHeight(kids, rowHeight_);
        float x = box.right() - 3.f;
        float y = parent.y - kMenuPadding;
        if (surface_.w > 0.f && x + width > surface_.w) x = std::max(0.f, box.x - width + 3.f);
        if (surface_.h > 0.f && y + height > surface_.h) y = std::max(0.f, surface_.h - height);
        return { x, y, width, height };
    }

    gfx::Rect PopupMenu::subItemRect(std::size_t index) const {
        const gfx::Rect box = submenuRect();
        if (box.empty()) return {};
        const auto& kids = items_[static_cast<std::size_t>(submenu_)].children;
        float y = box.y + kMenuPadding;
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const float h = menuRowHeight(kids[i], rowHeight_);
            if (i == index) return { box.x, y, box.w, h };
            y += h;
        }
        return {};
    }

    int PopupMenu::subItemAt(gfx::Point global) const {
        const gfx::Rect box = submenuRect();
        if (box.empty() || !box.contains(global)) return -1;
        const auto& kids = items_[static_cast<std::size_t>(submenu_)].children;
        float y = box.y + kMenuPadding;
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const float h = menuRowHeight(kids[i], rowHeight_);
            if (global.y >= y && global.y < y + h) return kids[i].separator ? -1 : static_cast<int>(i);
            y += h;
        }
        return -1;
    }

    bool PopupMenu::openSubmenu(std::size_t index) {
        const int i = static_cast<int>(index);
        if (!open_ || !hasSubmenu(i) || !items_[index].enabled || items_[index].heading) return false;
        if (submenu_ != i) {
            submenu_ = i;
            subHighlighted_ = -1;
            subKeyboard_ = false;
        }
        highlighted_ = i;
        invalidate();
        return true;
    }

    void PopupMenu::closeSubmenu() {
        if (submenu_ < 0) return;
        submenu_ = subHighlighted_ = -1;
        subKeyboard_ = false;
        invalidate();
    }

    void PopupMenu::moveSubHighlight(int delta) {
        if (!hasSubmenu(submenu_)) return;
        const auto& kids = items_[static_cast<std::size_t>(submenu_)].children;
        const int n = static_cast<int>(kids.size());
        int i = subHighlighted_;
        for (int step = 0; step < n; ++step) {
            i = (i < 0) ? (delta > 0 ? 0 : n - 1) : (i + delta + n) % n;
            if (choosable(kids[static_cast<std::size_t>(i)])) {
                subHighlighted_ = i;
                invalidate();
                return;
            }
        }
    }

    gfx::Rect PopupMenu::eventBounds() const {
        if (!open_) return {};                       // closed: invisible to the pointer
        if (surface_.w > 0.f && surface_.h > 0.f) return { 0.f, 0.f, surface_.w, surface_.h };
        // Sans surface (les tests) : le menu et son sous-menu ouvert.
        const gfx::Rect box = popupRect();
        const gfx::Rect sub = submenuRect();
        if (sub.empty()) return box;
        const float l = std::min(box.x, sub.x), t = std::min(box.y, sub.y);
        return { l, t, std::max(box.right(), sub.right()) - l, std::max(box.bottom(), sub.bottom()) - t };
    }

    void PopupMenu::openAt(gfx::Point where, gfx::Size surface) {
        anchor_ = where;
        surface_ = surface;
        highlighted_ = -1;
        submenu_ = subHighlighted_ = -1;
        subKeyboard_ = false;
        open_ = true;
        setVisibility(Visibility::Visible);
        grabFocus();
        invalidate();
    }

    void PopupMenu::close() {
        if (!open_) return;
        open_ = false;
        highlighted_ = -1;
        submenu_ = subHighlighted_ = -1;
        subKeyboard_ = false;
        setVisibility(Visibility::Collapsed);
        releaseFocus();
        invalidate();
    }

    gfx::Rect PopupMenu::itemRect(std::size_t index) const {
        const auto box = popupRect();
        float y = box.y + kMenuPadding;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const float h = itemHeight(i);
            if (i == index) return {box.x, y, box.w, h};
            y += h;
        }
        return {};
    }

    int PopupMenu::itemAt(gfx::Point global) const {
        const auto box = popupRect();
        if (!box.contains(global)) return -1;
        float y = box.y + kMenuPadding;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const float h = itemHeight(i);
            if (global.y >= y && global.y < y + h)
                return items_[i].separator ? -1 : static_cast<int>(i);
            y += h;
        }
        return -1;
    }

    void PopupMenu::moveHighlight(int delta) {
        if (items_.empty()) return;
        const int n = static_cast<int>(items_.size());
        int i = highlighted_;
        // Skip separators and disabled entries: arrowing onto something that cannot
        // be chosen is a dead keypress.
        for (int step = 0; step < n; ++step) {
            i = (i < 0) ? (delta > 0 ? 0 : n - 1) : (i + delta + n) % n;
            if (choosable(items_[static_cast<std::size_t>(i)])) {
                highlighted_ = i;
                closeSubmenu();          // lot 7 : quitter l'entree referme son sous-menu
                invalidate();
                return;
            }
        }
    }

    namespace {
        // Une colonne d'entrees : le menu, ou (lot 7) un sous-menu. `hotIndex` :
        // l'entree survolee ; `openIndex` : celle dont le sous-menu est ouvert
        // (surlignee aussi, pour qu'on voie d'ou il sort).
        void paintMenuColumn(const PaintContext& ctx, const gfx::Rect& box, const std::vector<PopupMenu::Item>& items,
                             float rowHeight, int hotIndex, int openIndex) {
            const auto& c = ctx.theme.color;
            ctx.r.fillRect({ box.x + 2.f, box.y + 2.f, box.w, box.h }, gfx::Color{ 0, 0, 0, 90 });   // shadow
            ctx.r.fillRect(box, c.panelBg);
            ctx.r.strokeRect(box, c.borderStrong, 1.f);

            const float lh = ctx.r.lineHeight(ctx.theme.font.ui);
            float y = box.y + kMenuPadding;

            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                const float h = menuRowHeight(item, rowHeight);

                if (item.separator) {
                    const float my = y + h * 0.5f;
                    ctx.r.line({ box.x + 6.f, my }, { box.right() - 6.f, my }, c.border, 1.f);
                    y += h;
                    continue;
                }

                const gfx::Rect row{ box.x + 1.f, y, box.w - 2.f, h };
                if (item.heading) {
                    // Lot API 6 : une ligne d'en-tete - le premier en clair, les autres en gris.
                    const bool first = i == 0 || !items[i - 1].heading;
                    ctx.r.fillRect(row, c.headerBg);
                    const gfx::FontId f = first ? ctx.theme.font.ui : ctx.theme.font.smallUi;
                    const float fh = ctx.r.lineHeight(f);
                    ctx.r.drawText({ row.x + kMenuPadding, row.y + (h - fh) * 0.5f }, item.label, f, first ? c.text : c.textMuted);
                    if (first) ctx.r.drawText({ row.x + kMenuPadding + 0.6f, row.y + (h - fh) * 0.5f }, item.label, f, c.text);
                    if (!item.shortcut.empty()) {
                        const float w = measureWidth(item.shortcut, kFontUi);
                        ctx.r.drawText({ row.right() - kMenuPadding - w, row.y + (h - lh) * 0.5f }, item.shortcut, ctx.theme.font.ui, c.textMuted);
                    }
                    y += h;
                    continue;
                }
                const bool hot = (static_cast<int>(i) == hotIndex || static_cast<int>(i) == openIndex) && item.enabled;
                if (hot) ctx.r.fillRect(row, c.selectionBg);

                const gfx::Color fg = !item.enabled ? c.textDisabled
                    : (hot ? c.selectionText : c.text);

                if (item.paintIcon) {
                    const float side = std::min(h - 4.f, 18.f);
                    item.paintIcon(ctx, { row.x + kMenuPadding - 1.f, row.y + (h - side) * 0.5f, side, side });
                } else if (item.icon != Icon::None) {
                    const float side = std::min(h - 6.f, 16.f);
                    drawIcon(ctx.r, item.icon,
                        { row.x + kMenuPadding, row.y + (h - side) * 0.5f, side, side }, fg);
                }
                ctx.r.drawText({ row.x + kMenuPadding + kMenuIconGutter, row.y + (h - lh) * 0.5f },
                    item.label, ctx.theme.font.ui, fg);

                float rightEdge = row.right() - kMenuPadding;
                // Lot 7 : une entree a sous-menu - sa fleche, au bord droit.
                if (!item.children.empty()) {
                    const float ax = rightEdge - 5.f, ay = row.y + h * 0.5f;
                    const gfx::Color arrow = hot ? c.selectionText : c.textMuted;
                    ctx.r.line({ ax - 2.f, ay - 4.f }, { ax + 2.f, ay }, arrow, 1.5f);
                    ctx.r.line({ ax + 2.f, ay }, { ax - 2.f, ay + 4.f }, arrow, 1.5f);
                    rightEdge -= kSubmenuArrow;
                }
                if (!item.shortcut.empty()) {
                    const float w = measureWidth(item.shortcut, kFontUi);
                    ctx.r.drawText({ rightEdge - w, row.y + (h - lh) * 0.5f }, item.shortcut,
                        ctx.theme.font.ui, hot ? c.selectionText : c.textMuted);
                    rightEdge -= w + 12.f;
                }
                // Why it is greyed, next to the entry rather than in a tooltip nobody
                // hovers long enough to see.
                if (!item.enabled && !item.disabledReason.empty()) {
                    const float w = measureWidth(item.disabledReason, kFontUi);
                    ctx.r.drawText({ rightEdge - w, row.y + (h - lh) * 0.5f }, item.disabledReason,
                        ctx.theme.font.ui, c.textDisabled);
                }
                y += h;
            }
        }
    } // namespace

    void PopupMenu::onPaintOverlay(const PaintContext& ctx) {
        if (!open_ || items_.empty()) return;
        rowHeight_ = ctx.theme.metric.rowHeight;
        paintMenuColumn(ctx, popupRect(), items_, rowHeight_, highlighted_, submenu_);
        // Lot 7 : le sous-menu ouvert, par-dessus.
        if (hasSubmenu(submenu_))
            paintMenuColumn(ctx, submenuRect(), items_[static_cast<std::size_t>(submenu_)].children, rowHeight_,
                            subHighlighted_, -1);
    }

    EventResult PopupMenu::onEvent(const InputEvent& ev) {
        if (!open_) return EventResult::Ignored;

        if (const auto* m = std::get_if<MouseMove>(&ev)) {
            // Lot 7 : dans le sous-menu ouvert, son entree ; il reste ouvert.
            if (submenu_ >= 0 && submenuRect().contains(m->pos)) {
                const int j = subItemAt(m->pos);
                const auto& kids = items_[static_cast<std::size_t>(submenu_)].children;
                const int hot = (j >= 0 && choosable(kids[static_cast<std::size_t>(j)])) ? j : -1;
                if (hot != subHighlighted_ || highlighted_ != submenu_) {
                    subHighlighted_ = hot;
                    highlighted_ = submenu_;
                    invalidate();
                }
                subKeyboard_ = true;       // les fleches suivent la souris
                return EventResult::Consumed;
            }
            const int i = itemAt(m->pos);
            if (i >= 0 && hasSubmenu(i) && choosable(items_[static_cast<std::size_t>(i)])) {
                // Une entree a sous-menu : il s'ouvre au survol.
                (void)openSubmenu(static_cast<std::size_t>(i));
                return EventResult::Consumed;
            }
            if (i >= 0) {
                // Une autre entree : le sous-menu se referme. Ailleurs (hors du
                // menu, sur une regle), il reste : on passe peut-etre de l'entree
                // a son sous-menu en biais.
                closeSubmenu();
                const int hot = choosable(items_[static_cast<std::size_t>(i)]) ? i : -1;
                if (hot != highlighted_) {
                    highlighted_ = hot;
                    invalidate();
                }
            } else if (highlighted_ != submenu_) {
                highlighted_ = submenu_;
                invalidate();
            }
            return EventResult::Consumed;
        }

        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            // Lot 7 : un clic dans le sous-menu ouvert choisit son entree.
            if (submenu_ >= 0 && submenuRect().contains(d->pos)) {
                const int j = subItemAt(d->pos);
                if (j < 0) return EventResult::Consumed;
                const auto& item = items_[static_cast<std::size_t>(submenu_)].children[static_cast<std::size_t>(j)];
                if (!choosable(item)) return EventResult::Consumed;   // a dead entry eats the click
                const int chosen = item.id;
                close();
                itemChosen->emit(chosen);
                return EventResult::Consumed;
            }
            const int i = itemAt(d->pos);
            if (i < 0) {
                // Anywhere outside dismisses. The click is consumed rather than
                // passed through: the first click closes the menu, the second one
                // does whatever it was aimed at. Passing it through would make a
                // dismissing click also delete something.
                close();
                dismissed->emit();
                return EventResult::Consumed;
            }
            const auto& item = items_[static_cast<std::size_t>(i)];
            if (!item.enabled || item.heading) return EventResult::Consumed;   // a dead entry eats the click
            // Lot 7 : une entree a sous-menu l'ouvre (sans le refermer : le
            // survol l'a peut-etre deja ouvert).
            if (hasSubmenu(i)) {
                (void)openSubmenu(static_cast<std::size_t>(i));
                return EventResult::Consumed;
            }
            const int chosen = item.id;
            close();
            itemChosen->emit(chosen);
            return EventResult::Consumed;
        }

        if (const auto* k = std::get_if<KeyDown>(&ev)) {
            // Lot 7 : le clavier dans un sous-menu ouvert - haut / bas s'y
            // promenent, Entree choisit, gauche ou Echap le referment seul.
            const bool inSub = submenu_ >= 0 && subKeyboard_;
            switch (k->key) {
            case Key::Escape:
                if (submenu_ >= 0) { closeSubmenu(); return EventResult::Consumed; }
                close(); dismissed->emit(); return EventResult::Consumed;
            case Key::Left:
                closeSubmenu();
                return EventResult::Consumed;
            case Key::Right:
                if (hasSubmenu(highlighted_) && openSubmenu(static_cast<std::size_t>(highlighted_))) {
                    subKeyboard_ = true;
                    if (subHighlighted_ < 0) moveSubHighlight(1);
                }
                return EventResult::Consumed;
            case Key::Down:   if (inSub) moveSubHighlight(1);  else moveHighlight(1);  return EventResult::Consumed;
            case Key::Up:     if (inSub) moveSubHighlight(-1); else moveHighlight(-1); return EventResult::Consumed;
            case Key::Return:
                if (submenu_ >= 0 && subHighlighted_ >= 0) {
                    const int chosen = items_[static_cast<std::size_t>(submenu_)].children[static_cast<std::size_t>(subHighlighted_)].id;
                    close();
                    itemChosen->emit(chosen);
                    return EventResult::Consumed;
                }
                if (hasSubmenu(highlighted_)) {
                    if (openSubmenu(static_cast<std::size_t>(highlighted_))) {
                        subKeyboard_ = true;
                        moveSubHighlight(1);
                    }
                    return EventResult::Consumed;
                }
                if (highlighted_ >= 0) {
                    const int chosen = items_[static_cast<std::size_t>(highlighted_)].id;
                    close();
                    itemChosen->emit(chosen);
                }
                return EventResult::Consumed;
            default:
                // Everything else is swallowed too. A menu that lets Ctrl+S
                // through while it is open is a menu you can save from by
                // accident.
                return EventResult::Consumed;
            }
        }
        return EventResult::Ignored;
    }


} // namespace ui