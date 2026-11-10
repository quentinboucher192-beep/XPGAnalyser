// =============================================================================
//  app/SessionWindow.cpp - 1.47.0 : voir SessionWindow.hpp
// =============================================================================
#include "SessionWindow.hpp"

#include "../ui/DialogFrame.hpp"
#include "../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app::sessionwin {

namespace {

constexpr gfx::FontId kUi{16}, kUiBold{16, 1}, kSmall{13}, kMono{16};
constexpr float kBand = ui::dialogframe::kTitleBand;
constexpr float kBtnH = 26.f;
constexpr float kRowText = 42.f;    // la pastille (32) et l'espace
constexpr float kRowMin = 52.f;     // une ligne d'equipier sans boutons dessous
constexpr float kReadable = 150.f;  // ce qu'il faut au nom pour etre lu, a cote de ses boutons

// Le libelle court d'un bouton de la fenetre (quand l'entier ne tient pas).
std::string briefOf(const std::string& full) {
    if (full == "Arr\xC3\xAAter la session") return "Arr\xC3\xAAter";
    if (full == "Quitter la session") return "Quitter";
    if (full == "Copier le code") return "Copier";
    if (full == "Retirer de la session\xE2\x80\xA6") return "Retirer\xE2\x80\xA6";
    if (full == "Regarder seulement") return "Regarder";
    if (full == "Laisser modifier") return "Modifier";
    if (full == "Faire entrer") return "Entrer";
    if (full == "Exporter (CSV)") return "CSV";
    return {};
}

float step(gfx::FontId f) { return std::ceil(ui::lineHeight(f)) + 4.f; }

} // namespace

Body::Body(std::string id) : ui::Widget(std::move(id)) {
    lines_ = &static_cast<sessionui::LineList&>(addChild(std::make_unique<sessionui::LineList>(this->id() + ".lignes")));
}

void Body::setSnapshot(Snapshot s) {
    snap_ = std::move(s);
    lines_->setEmptyText(snap_.showJournal ? "Aucun changement encore : chaque changement de la session s'inscrit ici (qui, quand, quoi)."
                                           : "Aucun message encore : \xC3\xA9" "cris \xC3\xA0 l'\xC3\xA9quipe dans le champ du bas (Entr\xC3\xA9" "e).");
    lines_->setRows(snap_.showJournal ? snap_.journal : snap_.chat);
    invalidateLayout();
    invalidate();
}

const std::string& Body::fullOf(ui::Button* b) {
    for (const auto& [k, t] : fullText_)
        if (k == b) return t;
    fullText_.emplace_back(b, b->text());
    return fullText_.back().second;
}

std::vector<std::string> Body::cutTexts() const {
    std::vector<std::string> out;
    for (const auto& l : labels_)
        if (!l.full.empty()) out.push_back(l.full);
    return out;
}

float Body::addLine(gfx::Point at, const std::string& full, gfx::FontId font, float width, int tone, bool team) {
    if (full.empty()) return 0.f;
    Label l;
    l.at = at;
    l.text = sessionui::elide(full, font, std::max(0.f, width));
    l.font = font;
    l.tone = tone;
    l.team = team;
    if (l.text != full) l.full = full;
    const float w = l.text.empty() ? 0.f : ui::measureWidth(l.text, font);
    if (!l.text.empty() || !l.full.empty()) labels_.push_back(std::move(l));
    return w;
}

float Body::addWrapped(float x, float y, const std::string& text, gfx::FontId font, float width, int tone, std::size_t maxLines) {
    auto lines = sessionui::wrap(text, font, std::max(10.f, width));
    const float st = step(font);
    std::string full;
    if (maxLines && lines.size() > maxLines) {
        // La derniere ligne gardee finit par << ... >> ; le texte entier dans l'infobulle.
        std::string rest = lines[maxLines - 1];
        for (std::size_t i = maxLines; i < lines.size(); ++i) rest += " " + lines[i];
        lines.resize(maxLines);
        lines.back() = sessionui::elide(rest, font, std::max(10.f, width));
        full = text;
    }
    float ly = y;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        Label l;
        l.at = {x, ly};
        l.text = lines[i];
        l.font = font;
        l.tone = tone;
        if (i + 1 == lines.size()) l.full = full;
        labels_.push_back(std::move(l));
        ly += st;
    }
    return static_cast<float>(lines.size()) * st;
}

// ------------------------------------------------------------------------------------------- l'equipe ---
//  Pose dans le contenu de `view` (le decalage a 0 : applyTeamOffset le retire ensuite) ; `rowW` : la largeur des lignes
//  (sans la barre de defilement, quand elle est la).
float Body::teamPass(const gfx::Rect& view, float rowW) {
    labels_.erase(std::remove_if(labels_.begin(), labels_.end(), [](const Label& l) { return l.team; }), labels_.end());
    dots_.clear();
    teamButtons_.clear();
    const float x = view.x;
    float y = view.y;
    const auto note = [this](std::vector<sessionui::ButtonSpec>& specs) {
        for (const auto& s : specs)
            if (s.button) teamButtons_.push_back({s.button, s.button->bounds()});
    };
    // Le code (l'hote), en tete du contenu qui defile : une fenetre tres basse le garde atteignable.
    codeBox_ = {};
    if (snap_.code) {
        codeBox_ = {x, y, rowW, 42.f};
        float room = codeBox_.w - 20.f;
        if (copy) {
            copy->setVisibility(ui::Visibility::Visible);
            std::vector<sessionui::ButtonSpec> one{{copy, fullOf(copy), briefOf(fullOf(copy)), 44.f, {}}};
            const float full = sessionui::buttonWidth(fullOf(copy));
            const float bw = codeBox_.w - full - 28.f >= 140.f ? full : std::min(sessionui::buttonWidth(briefOf(fullOf(copy))), std::max(44.f, codeBox_.w * 0.4f));
            (void)sessionui::fitButtons(one, codeBox_.right() - 8.f - bw, codeBox_.y + 8.f, bw, kBtnH, 6.f);
            note(one);
            room = codeBox_.w - 28.f - bw;
        }
        addLine({codeBox_.x + 10.f, codeBox_.y + 3.f}, snap_.codeLabel, kSmall, room, 1, true);
        addLine({codeBox_.x + 10.f, codeBox_.y + 19.f}, snap_.codeText, kMono, room, 0, true);
        y += 50.f;
    } else if (copy) {
        copy->setVisibility(ui::Visibility::Hidden);
    }
    for (std::size_t i = 0; i < snap_.people.size(); ++i) {
        const auto& p = snap_.people[i];
        std::vector<sessionui::ButtonSpec> specs;
        if (i < rows.size())
            for (auto* b : {rows[i].follow, rows[i].role, rows[i].remove})
                if (b) specs.push_back({b, fullOf(b), briefOf(fullOf(b)), 44.f, {}});
        float natural = 0.f;
        for (const auto& s : specs) natural += sessionui::buttonWidth(s.full);
        if (!specs.empty()) natural += 6.f * static_cast<float>(specs.size() - 1);
        const bool beside = !specs.empty() && rowW - kRowText - kReadable - 8.f >= natural;
        const float textW = std::max(0.f, (beside ? rowW - natural - 8.f : rowW) - kRowText);
        dots_.push_back({{x, y + 8.f, 32.f, 32.f}, p.color});
        if (!p.initials.empty()) {
            const float iw = ui::measureWidth(p.initials, kSmall);
            addLine({x + (32.f - iw) * 0.5f, y + 8.f + (32.f - ui::lineHeight(kSmall)) * 0.5f}, p.initials, kSmall, 30.f, 4, true);
        }
        // Le nom (raccourci avant son etiquette << (toi) . hote >>, qui reste lisible), puis ou il est.
        const float tagW = p.tag.empty() ? 0.f : ui::measureWidth(p.tag, kSmall) + 6.f;
        if (tagW > 0.f && textW - tagW >= 60.f) {
            const float nw = addLine({x + kRowText, y + 5.f}, p.name, kUi, textW - tagW, 0, true);
            addLine({x + kRowText + nw + 6.f, y + 5.f + (ui::lineHeight(kUi) - ui::lineHeight(kSmall)) * 0.5f + 1.f}, p.tag, kSmall, tagW, 1, true);
        } else {
            addLine({x + kRowText, y + 5.f}, p.tag.empty() ? p.name : p.name + " " + p.tag, kUi, textW, 0, true);
        }
        addLine({x + kRowText, y + 27.f}, p.where, kSmall, textW, 1, true);
        float rowH = kRowMin;
        if (beside) {
            (void)sessionui::fitButtons(specs, x + rowW - natural, y + 12.f, natural, kBtnH, 6.f);
        } else if (!specs.empty()) {
            // Dessous : toute la largeur de la ligne, sur deux lignes plutot que raccourcis.
            const float bh = sessionui::fitButtons(specs, x, y + 50.f, rowW, kBtnH, 6.f, false, true);
            rowH = 50.f + bh + 10.f;
        }
        note(specs);
        y += rowH;
    }
    if (!snap_.knocks.empty()) {
        y += 6.f;
        addLine({x, y}, "Ils veulent entrer", kUiBold, rowW, 2, true);
        y += 24.f;
        for (std::size_t i = 0; i < snap_.knocks.size(); ++i) {
            const auto& k = snap_.knocks[i];
            std::vector<sessionui::ButtonSpec> specs;
            if (i < knockRows.size())
                for (auto* b : {knockRows[i].yes, knockRows[i].no})
                    if (b) specs.push_back({b, fullOf(b), briefOf(fullOf(b)), 44.f, {}});
            float natural = 0.f;
            for (const auto& s : specs) natural += sessionui::buttonWidth(s.full);
            if (!specs.empty()) natural += 6.f * static_cast<float>(specs.size() - 1);
            const bool beside = !specs.empty() && rowW - kReadable - 8.f >= natural;
            const float textW = beside ? rowW - natural - 8.f : rowW;
            addLine({x, y + 4.f}, k.name, kUi, textW, 0, true);
            addLine({x, y + 25.f}, k.detail, kSmall, textW, 1, true);
            float rowH = 48.f;
            if (beside) (void)sessionui::fitButtons(specs, x + rowW - natural, y + 10.f, natural, kBtnH, 6.f);
            else if (!specs.empty()) rowH = 46.f + sessionui::fitButtons(specs, x, y + 46.f, rowW, kBtnH, 6.f, false, true) + 10.f;
            note(specs);
            y += rowH;
        }
    }
    return y - view.y;
}

void Body::applyTeamOffset() {
    for (auto& tb : teamButtons_) {
        const gfx::Rect at{tb.at.x, tb.at.y - teamOffset_, tb.at.w, tb.at.h};
        tb.b->setBounds(at);
        // Un bouton a moitie hors de la zone qui defile se cache (il n'y serait pas coupe : il passerait dessus).
        const bool inside = at.y >= teamView_.y - 0.5f && at.bottom() <= teamView_.bottom() + 0.5f;
        tb.b->setVisibility(inside ? ui::Visibility::Visible : ui::Visibility::Hidden);
    }
}

// ----------------------------------------------------------------------------------- la discussion ---
void Body::layoutChat(const gfx::Rect& area) {
    const std::string title = snap_.showJournal ? "Le journal" : "La discussion";
    std::vector<sessionui::ButtonSpec> specs;
    for (auto* b : {chatView, journalView, exportJournal})
        if (b) specs.push_back({b, fullOf(b), briefOf(fullOf(b)), 40.f, {}});
    float natural = 0.f;
    for (const auto& s : specs) natural += sessionui::buttonWidth(s.full);
    if (!specs.empty()) natural += 4.f * static_cast<float>(specs.size() - 1);
    const float titleW = ui::measureWidth(title, kUiBold);
    float headerH = 26.f;
    // Le titre et les vues sur une ligne s'ils tiennent ; sinon les vues sous le titre, s'il reste de quoi lire la
    // discussion ; sinon une ligne, les vues serrees a droite et le titre raccourci (ou rien) a leur gauche.
    const float twoLinesH = 24.f + (specs.empty() ? 0.f : sessionui::fitButtons(specs, area.x, area.y + 24.f, area.w, 24.f, 4.f, true) + 2.f);
    if (titleW + 12.f + natural <= area.w) {
        addLine({area.x, area.y + 3.f}, title, kUiBold, area.w - natural - 12.f, 0);
        (void)sessionui::fitButtons(specs, area.x, area.y, area.w, 24.f, 4.f, true);
    } else if (area.h - twoLinesH - 6.f - 8.f - 30.f >= 60.f) {
        addLine({area.x, area.y}, title, kUiBold, area.w, 0);
        headerH = twoLinesH;
    } else {
        (void)sessionui::fitButtons(specs, area.x, area.y, area.w, 24.f, 4.f, true);
        float left = area.right();
        for (const auto& sp : specs) left = std::min(left, sp.button->bounds().x);
        if (left - area.x - 12.f >= 60.f) addLine({area.x, area.y + 3.f}, title, kUiBold, left - area.x - 12.f, 0);
    }
    // Le champ et Envoyer en bas ; la liste entre les deux.
    const float fy = std::max(area.y + headerH + 6.f, area.bottom() - 30.f);
    if (send) {
        const float sw = std::clamp(std::floor(area.w * 0.3f), 60.f, 96.f);
        send->setBounds({area.right() - sw, fy, sw, 30.f});
        sessionui::fitButton(*send, fullOf(send), sw, {}, "Envoyer \xC3\xA0 l'\xC3\xA9quipe (Entr\xC3\xA9" "e)");
        if (field) field->setBounds({area.x, fy, std::max(0.f, area.w - sw - 8.f), 30.f});
    } else if (field) {
        field->setBounds({area.x, fy, area.w, 30.f});
    }
    if (field) sessionui::fitPlaceholder(*field, field->bounds().w, sessionui::messagePlaceholders());
    const float top = area.y + headerH + 6.f;
    chatBox_ = {area.x, top, area.w, std::max(0.f, fy - 8.f - top)};
    const bool shown = chatBox_.h >= 28.f;
    lines_->setVisibility(shown ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (shown) lines_->setBounds(chatBox_.inset(3.f, 3.f));
    else chatBox_ = {};
}

// ------------------------------------------------------------------------------------- la fenetre ---
void Body::onLayout() {
    const auto r = bounds();
    // Le cadre tient dans la fenetre : 900 x 640 au plus, la fenetre moins ses marges (30, ou 8 si elle est petite).
    const float mx = r.w >= 760.f ? 30.f : 8.f, my = r.h >= 560.f ? 30.f : 8.f;
    const float w = std::max(0.f, std::min(900.f, r.w - 2.f * mx));
    const float h = std::max(0.f, std::min(640.f, r.h - 2.f * my));
    panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
    labels_.clear();
    dots_.clear();
    teamButtons_.clear();
    codeBox_ = chatBox_ = teamArea_ = teamView_ = {};
    addLine({panel_.x + 12.f, panel_.y + (kBand - ui::lineHeight(kUiBold)) * 0.5f}, snap_.title, kUiBold, panel_.w - 24.f, 0);
    const float cx = panel_.x + 16.f, cw = std::max(0.f, panel_.w - 32.f);
    const float top = panel_.y + kBand + 12.f;

    // ---- le pied : Arreter / Quitter et Fermer a droite, le rappel a gauche (ou dessus, s'il n'y a pas la place) ----
    std::vector<sessionui::ButtonSpec> foot;
    for (auto* b : {stop, close})
        if (b) foot.push_back({b, fullOf(b), briefOf(fullOf(b)), 50.f, {}});
    float natural = 0.f;
    for (const auto& s : foot) natural += sessionui::buttonWidth(s.full);
    if (!foot.empty()) natural += 10.f * static_cast<float>(foot.size() - 1);
    const float bottom = panel_.bottom() - 14.f;
    float footTop = bottom;
    if (!snap_.hint.empty() && cw - natural - 16.f >= 220.f) {
        const float hintW = cw - natural - 16.f;
        const auto n = sessionui::wrap(snap_.hint, kSmall, hintW).size();
        const std::size_t keep = std::min<std::size_t>(n, panel_.h < 420.f ? 2 : 3);
        const float hintH = static_cast<float>(keep) * step(kSmall);
        const float footH = std::max(30.f, hintH);
        footTop = bottom - footH;
        (void)addWrapped(cx, footTop + std::max(0.f, (footH - hintH) * 0.5f), snap_.hint, kSmall, hintW, 1, keep);
        (void)sessionui::fitButtons(foot, cx, bottom - 30.f, cw, 30.f, 10.f, true);
    } else {
        const float bh = foot.empty() ? 0.f : sessionui::fitButtons(foot, cx, 0.f, cw, 30.f, 10.f, true);
        (void)sessionui::fitButtons(foot, cx, bottom - bh, cw, 30.f, 10.f, true);
        footTop = bottom - bh;
        if (!snap_.hint.empty()) {
            const auto n = sessionui::wrap(snap_.hint, kSmall, cw).size();
            const std::size_t keep = std::min<std::size_t>(n, panel_.h < 420.f ? 1 : cw < 300.f ? 2 : 3);
            const float hintH = static_cast<float>(keep) * step(kSmall);
            footTop -= hintH + (foot.empty() ? 0.f : 8.f);
            (void)addWrapped(cx, footTop, snap_.hint, kSmall, cw, 1, keep);
        }
    }
    const gfx::Rect content{cx, top, cw, std::max(0.f, footTop - 12.f - top)};

    if (!snap_.active) {
        // Hors session (on entre, un refus) : ce qui se passe, retourne a la ligne ; rien d'autre.
        lines_->setVisibility(ui::Visibility::Collapsed);
        const std::size_t fit = static_cast<std::size_t>(std::max(1.f, std::floor(content.h / step(kUi))));
        (void)addWrapped(cx, top, snap_.progress, kUi, cw, 0, fit);
        two_ = false;
        return;
    }

    // ---- deux colonnes (l'equipe, la discussion) ou l'une sous l'autre ----
    two_ = cw >= 600.f;
    gfx::Rect team, chat;
    if (two_) {
        const float lw = std::floor(cw * 0.5f) - 8.f;
        team = {cx, top, lw, content.h};
        chat = {cx + lw + 16.f, top, cw - lw - 16.f, content.h};
    } else {
        // L'equipe d'abord, a sa hauteur, sans prendre plus de la moitie (la discussion garde de quoi se lire).
        const float wanted = 24.f + (snap_.code ? 50.f : 0.f) + kRowMin * static_cast<float>(std::max<std::size_t>(1, snap_.people.size()))
                           + (snap_.knocks.empty() ? 0.f : 30.f + 48.f * static_cast<float>(snap_.knocks.size()));
        // La discussion garde au moins son en-tete, quelques lignes et le champ (l'equipe defile).
        const float chatMin = std::min(content.h * 0.55f, 26.f + 6.f + 60.f + 8.f + 30.f);
        const float th = std::floor(std::max(0.f, std::min({wanted + 40.f, std::max(content.h * 0.5f, std::min(content.h, 120.f)), content.h - 12.f - chatMin})));
        team = {cx, top, cw, th};
        chat = {cx, top + th + 12.f, cw, std::max(0.f, content.h - th - 12.f)};
    }
    teamArea_ = team;
    // L'en-tete de l'equipe ; dessous, le code et les equipiers defilent.
    float y = team.y;
    addLine({team.x, y}, snap_.teamTitle, kUiBold, team.w, 0);
    y += 26.f;
    teamView_ = {team.x, y, team.w, std::max(0.f, team.bottom() - y)};
    teamContent_ = teamPass(teamView_, teamView_.w);
    if (teamContent_ > teamView_.h + 0.5f) teamContent_ = teamPass(teamView_, std::max(0.f, teamView_.w - ui::kScrollBarThickness - 4.f));
    teamScroll_.place(teamView_, teamContent_, teamView_.h);
    teamOffset_ = std::clamp(teamOffset_, 0.f, std::max(0.f, teamContent_ - teamView_.h));
    applyTeamOffset();
    layoutChat(chat);
}

void Body::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    auto& r = ctx.r;
    r.fillRect(panel_, c.panelBg);
    r.strokeRect(panel_, c.borderStrong, 1.f);
    r.fillRect({panel_.x, panel_.y, panel_.w, kBand}, c.headerBg);
    if (chatBox_.w > 0.f) r.fillRoundedRect(chatBox_, c.inputBg, 6.f);
    const auto tone = [&](int t) {
        switch (t) {
            case 1: return c.textMuted;
            case 2: return c.warning;
            case 4: return gfx::Color::rgb(0xFFFFFF);
            default: return c.text;
        }
    };
    r.pushClip(panel_);
    for (const auto& l : labels_)
        if (!l.team && !l.text.empty()) r.drawText(l.at, l.text, l.font, tone(l.tone));
    if (teamView_.w > 0.f && teamView_.h > 0.f) {
        r.pushClip(teamView_);
        if (codeBox_.w > 0.f) r.fillRoundedRect({codeBox_.x, codeBox_.y - teamOffset_, codeBox_.w, codeBox_.h}, c.inputBg, 6.f);
        for (const auto& d : dots_) {
            const gfx::Rect at{d.rect.x, d.rect.y - teamOffset_, d.rect.w, d.rect.h};
            if (at.bottom() >= teamView_.y && at.y <= teamView_.bottom()) r.fillRoundedRect(at, gfx::Color::rgb(d.color ? d.color : 0x808080), 16.f);
        }
        for (const auto& l : labels_)
            if (l.team && !l.text.empty()) {
                const gfx::Point at{l.at.x, l.at.y - teamOffset_};
                if (at.y + 30.f >= teamView_.y && at.y <= teamView_.bottom()) r.drawText(at, l.text, l.font, tone(l.tone));
            }
        r.popClip();
        teamScroll_.paint(ctx, teamOffset_);
    }
    r.popClip();
}

ui::EventResult Body::onEvent(const ui::InputEvent& ev) {
    bool changed = false;
    if (teamView_.h > 0.f && teamScroll_.handle(*this, ev, teamOffset_, &changed)) {
        if (changed) applyTeamOffset();
        return ui::EventResult::Consumed;
    }
    return ui::Widget::onEvent(ev);
}

std::string Body::liveTooltip(gfx::Point mouse) const {
    for (const auto& l : labels_) {
        if (l.full.empty()) continue;
        const float y = l.team ? l.at.y - teamOffset_ : l.at.y;
        const gfx::Rect at{l.at.x, y, std::max(20.f, ui::measureWidth(l.text, l.font)), ui::lineHeight(l.font)};
        if (l.team && !teamView_.contains(mouse)) continue;
        if (at.contains(mouse)) return l.full;
    }
    return {};
}

} // namespace app::sessionwin
