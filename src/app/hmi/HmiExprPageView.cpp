// =============================================================================
//  app/hmi/HmiExprPageView.cpp - 1.11 (chantier T3, D5) : voir HmiExprPageView.hpp
// =============================================================================
#include "HmiExprPageView.hpp"

#include <algorithm>

namespace app {

namespace eg = hmi::exprguide;

HmiExprPageView::HmiExprPageView(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    view_ = &static_cast<ui::HelpArticleView&>(addChild(std::make_unique<ui::HelpArticleView>(base + ".article")));
    auto field = std::make_unique<ui::InputText>(base + ".essai");
    field->setPlaceholder("= une expression, ou clique sur un exemple");
    field_ = &static_cast<ui::InputText&>(addChild(std::move(field)));
    auto replace = std::make_unique<ui::Button>("Remplacer par", base + ".remplacer");
    replace->setStyle(ui::Button::Style::Primary);
    replace_ = &static_cast<ui::Button&>(addChild(std::move(replace)));
    replace_->setVisibility(ui::Visibility::Collapsed);

    links_ += view_->linkActivated->connect([this](const std::string& target) { onLink(target); });
    links_ += field_->textChanged->connect([this](const std::string&) { judge(); });
    links_ += replace_->clicked->connect([this] { (void)applyReplacement(); });
    (void)show(eg::all().front().key);
}

bool HmiExprPageView::show(std::string_view key) {
    const auto* t = eg::find(key);
    if (!t) return false;
    type_ = t;
    caseType_ = nullptr;
    view_->setArticle(exprpage::article(*t));
    // Le champ part du premier exemple : la page montre tout de suite un resultat.
    field_->setText(t->examples.empty() ? std::string{} : std::string(t->examples.front().source));
    judge();
    invalidate();
    return true;
}

void HmiExprPageView::tryIt(std::string_view source, std::string_view typeKey) {
    caseType_ = typeKey.empty() ? nullptr : eg::find(typeKey);
    field_->setText(std::string(source));
    judge();
}

std::string HmiExprPageView::trialText() const {
    // Tranche 13 : le chemin essai.texte. Un @verifier relit sa valeur mot a mot (tutorialWords) :
    // les espaces de l'utilisateur ne doivent pas faire echouer son « A toi ». Une chaine ('...' ou
    // "...") est gardee telle quelle, son echappement $ compris ('It$'s').
    const std::string& s = field_->text();
    std::string out;
    char quote = 0;
    bool escape = false, space = false;
    for (const char c : s) {
        if (quote) {
            out += c;
            if (escape) escape = false;
            else if (c == '$') escape = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        if (c == '\'' || c == '"') quote = c;
        out += c;
    }
    return out;
}

bool HmiExprPageView::applyReplacement() {
    if (outcome_.ok || outcome_.replacement.empty()) return false;
    field_->setText(outcome_.replacement);
    judge();
    return true;
}

void HmiExprPageView::judge() {
    const auto* as = caseType_ ? caseType_ : type_;
    if (!as) return;
    outcome_ = bench_.evaluate(field_->text(), *as);
    const bool offer = !outcome_.ok && !outcome_.replacement.empty();
    // Tranche 6 : le bouton dit seulement « Remplacer par » ; la correction est ecrite au-dessus
    // (onPaint), coupee a la largeur de la colonne - entiere, elle debordait du bouton.
    replace_->setText("Remplacer par");
    replace_->setVisibility(offer ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    invalidateLayout();
    invalidate();
}

void HmiExprPageView::onLink(const std::string& target) {
    const auto act = exprpage::parseTarget(target);
    if (!act) { openTopic->emit(target); return; }
    switch (act->kind) {
        case exprpage::Action::Kind::Try:    tryIt(act->text, act->typeKey); break;
        case exprpage::Action::Kind::Insert: {
            auto text = field_->text();
            if (text.empty() && type_ && type_->keepEquals) text = "=";
            if (!text.empty() && text.back() != ' ' && text.back() != '(' && text != "=") text += ' ';
            field_->setText(text + act->text);
            judge();
            break;
        }
        case exprpage::Action::Kind::Open:   (void)show(act->typeKey); break;
    }
}

void HmiExprPageView::onLayout() {
    const auto b = bounds();
    const float side = std::min(kSide, std::max(0.f, b.w * 0.45f));
    view_->setBounds({b.x, b.y, std::max(0.f, b.w - side), b.h});
    const float x = b.x + b.w - side + 14.f;
    const float w = std::max(0.f, side - 28.f);
    field_->setBounds({x, b.y + 44.f, w, 30.f});
    // « Remplacer par » sous la raison et la correction (onPaint les coupe : 6 lignes).
    replace_->setBounds({x, b.y + 44.f + 30.f + 16.f + 6.f * 18.f, std::min(w, 160.f), 28.f});
}

void HmiExprPageView::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    const float side = std::min(kSide, std::max(0.f, b.w * 0.45f));
    const gfx::Rect panel{b.x + b.w - side, b.y, side, b.h};
    ctx.r.fillRect(panel, c.panelBg);
    ctx.r.line({panel.x, panel.y}, {panel.x, panel.y + panel.h}, c.border);
    const float x = panel.x + 14.f;
    const auto& f = ctx.theme.font;
    ctx.r.drawText({x, b.y + 14.f}, "Essaie ici", f.ui, c.text);
    const auto* as = caseType_ ? caseType_ : type_;
    if (as) {
        // Recette 1.11 (tranche 14) : « case Membre et element · le type du membre » depassait
        // le bord droit ; la ligne est coupee avec « … » a la largeur du panneau.
        std::string head = "case " + std::string(as->title) + " \xC2\xB7 " + std::string(as->wanted);
        const float room = panel.x + panel.w - 10.f - (x + 110.f);
        if (room > 0.f && ctx.r.measure(head, f.smallUi).width > room) {
            while (!head.empty() && ctx.r.measure(head + "\xE2\x80\xA6", f.smallUi).width > room) {
                unsigned char last = 0;   // un caractere entier (UTF-8) : ses octets de suite, puis son premier
                do {
                    last = static_cast<unsigned char>(head.back());
                    head.pop_back();
                } while (!head.empty() && (last & 0xC0) == 0x80);
            }
            head += "\xE2\x80\xA6";
        }
        ctx.r.drawText({x + 110.f, b.y + 16.f}, head, f.smallUi, c.textMuted);
    }
    float y = b.y + 44.f + 30.f + 12.f;
    const float lh = ctx.r.lineHeight(f.ui) + 4.f;
    if (outcome_.ok) {
        float vx = x;
        if (outcome_.swatch != 0) {   // la pastille d'une couleur
            const gfx::Color sw{static_cast<std::uint8_t>(outcome_.swatch >> 24), static_cast<std::uint8_t>(outcome_.swatch >> 16),
                                static_cast<std::uint8_t>(outcome_.swatch >> 8), static_cast<std::uint8_t>(outcome_.swatch)};
            ctx.r.fillRoundedRect({vx, y + 2.f, 16.f, 16.f}, sw, 4.f);
            vx += 22.f;
        }
        ctx.r.drawText({vx, y}, outcome_.value, f.mono, c.ok);
        y += lh;
        ctx.r.drawText({x, y}, "type " + outcome_.type, f.smallUi, c.textMuted);
        y += lh;
        if (!outcome_.fits.empty()) { ctx.r.drawText({x, y}, "\xE2\x9C\x93 " + outcome_.fits, f.smallUi, c.ok); y += lh; }
        // Tranche 8 : sur quoi le champ juge, a cote du resultat (le projet ouvert : IHM non demarree).
        paintSourceNote(ctx, x, y, std::max(40.f, side - 28.f), b.y + b.h);
        return;
    }
    // Le soulignement, sous le champ : de errBegin a errEnd (octets du champ).
    const auto& text = field_->text();
    if (outcome_.errEnd > outcome_.errBegin && outcome_.errEnd <= text.size()) {
        const auto fb = field_->bounds();
        const float x0 = fb.x + 6.f + ctx.r.measure(std::string_view(text).substr(0, outcome_.errBegin), f.ui).width;
        const float x1 = fb.x + 6.f + ctx.r.measure(std::string_view(text).substr(0, outcome_.errEnd), f.ui).width;
        // Tranche 6 : juste SOUS le champ - dedans, le champ (un enfant, dessine apres) le recouvrait ;
        // mesure dans la police du champ (font.ui, comme InputText), pas en chasse fixe.
        const float uy = fb.y + fb.h + 2.f;
        ctx.r.line({x0, uy}, {std::max(x0 + 6.f, std::min(x1, fb.x + fb.w - 4.f)), uy}, c.error, 2.f);
    }
    if (text.empty() || text == "=") {
        // Recette 1.11 (tranche 14) : un champ vide ne disait rien.
        ctx.r.drawText({x, y}, "\xC3\x89" "cris une expression, ou clique \xC2\xAB Essayer \xE2\x80\xBA \xC2\xBB.", f.smallUi, c.textMuted);
        y += ctx.r.lineHeight(f.smallUi) + 3.f;
        paintSourceNote(ctx, x, y, std::max(40.f, side - 28.f), b.y + b.h);
        return;
    }
    // Tranche 6 : la raison passe a la ligne (elle etait coupee a droite), puis la correction
    // proposee, en police a chasse fixe ; le tout tient dans la place laissee au-dessus du bouton.
    const float w = std::max(40.f, side - 28.f);
    const float stop = replace_->visible() ? replace_->bounds().y - 4.f : b.y + b.h;
    const float slh = ctx.r.lineHeight(f.smallUi) + 3.f;
    auto wrapped = [&](const std::string& line, gfx::FontId font, gfx::Color colour) {
        std::string cur;
        std::size_t at = 0;
        while (at <= line.size()) {
            const auto sp = line.find(' ', at);
            const std::string word = line.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
            const std::string tryLine = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && ctx.r.measure(tryLine, font).width > w) {
                if (y + slh > stop) return;
                ctx.r.drawText({x, y}, cur, font, colour);
                y += slh;
                cur = word;
            } else {
                cur = tryLine;
            }
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
        if (!cur.empty() && y + slh <= stop) { ctx.r.drawText({x, y}, cur, font, colour); y += slh; }
    };
    // Tranche 15 : une valeur qui ne convient pas a la case (« ne convient pas : BOOL pour une
    // enumeration (T_MODE) ») reste montree, avec son type, au-dessus de la raison.
    if (!outcome_.value.empty() && !outcome_.type.empty())
        wrapped(outcome_.value + "  \xC2\xB7  type " + outcome_.type, f.smallUi, c.textMuted);
    wrapped(outcome_.reason.empty() ? std::string("Ne se lit pas.") : outcome_.reason, f.smallUi, c.error);
    if (replace_->visible()) wrapped("\xE2\x86\x92 " + outcome_.replacement, f.mono, c.text);
    // Tranche 8 : la source des valeurs, sous le bouton (ou sous la raison s'il n'y en a pas).
    const float ny = replace_->visible() ? replace_->bounds().y + replace_->bounds().h + 8.f : y + 4.f;
    paintSourceNote(ctx, x, ny, w, b.y + b.h);
}

void HmiExprPageView::paintSourceNote(const ui::PaintContext& ctx, float x, float y, float w, float stop) const {
    const std::string note = bench_.sourceNote();
    if (note.empty()) return;
    const auto& f = ctx.theme.font;
    const float slh = ctx.r.lineHeight(f.smallUi) + 3.f;
    std::string cur;
    std::size_t at = 0;
    while (at <= note.size()) {
        const auto sp = note.find(' ', at);
        const std::string word = note.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        const std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && ctx.r.measure(tryLine, f.smallUi).width > w) {
            if (y + slh > stop) return;
            ctx.r.drawText({x, y}, cur, f.smallUi, ctx.theme.color.textMuted);
            y += slh;
            cur = word;
        } else {
            cur = tryLine;
        }
        if (sp == std::string::npos) break;
        at = sp + 1;
    }
    if (!cur.empty() && y + slh <= stop) ctx.r.drawText({x, y}, cur, f.smallUi, ctx.theme.color.textMuted);
}

} // namespace app
