// =============================================================================
//  app/ApiPanes.cpp - lot API 2 : les volets de l'API
// =============================================================================
#include "ApiPanes.hpp"

#include "hmi/HmiPanels.hpp"
#include "../ui/Icons.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace {

const gfx::FontId kBody{14};
const gfx::FontId kSmall{12};
const gfx::FontId kTitle{13};
const gfx::FontId kBig{28};

// 1.11 (R111, recette T3-11) : un nombre et son nom, accordes (« 1 section »).
std::string compte(std::size_t n, std::string_view un, std::string_view plusieurs) {
    return std::to_string(n) + " " + std::string(n == 1 ? un : plusieurs);
}

std::string thousands(std::size_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

std::string upper(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

// Le texte coupe a la largeur (mot a mot), au plus `maxLines` lignes ; la
// derniere se termine par ... si le reste ne tient pas.
std::vector<std::string> wrap(const ui::PaintContext& ctx, const std::string& text, gfx::FontId f, float width, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        auto j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && ctx.r.measure(candidate, f).width > width) {
            lines.push_back(line);
            line = word;
            if (lines.size() == maxLines) break;
        } else {
            line = candidate;
        }
        i = j + 1;
    }
    if (lines.size() < maxLines && !line.empty()) lines.push_back(line);
    else if (lines.size() == maxLines && i < text.size()) {
        auto& last = lines.back();
        const auto n = ctx.r.fitCharacters(last + "\xE2\x80\xA6", f, width);
        if (n < last.size() + 3) last = last.substr(0, n > 3 ? n - 3 : 0);
        last += "\xE2\x80\xA6";
    }
    return lines;
}

void drawBold(const ui::PaintContext& ctx, gfx::Point at, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText(at, s, f, c);
    ctx.r.drawText({at.x + 0.6f, at.y}, s, f, c);
}

gfx::Rect button(const ui::PaintContext& ctx, float right, float cy, const std::string& label, bool hot, bool primary) {
    const float w = ctx.r.measure(label, kSmall).width + 22.f;
    const gfx::Rect r{right - w, cy - 14.f, w, 28.f};
    const auto& c = ctx.theme.color;
    if (primary) ctx.r.fillRoundedRect(r, hot ? c.accentHover : c.accent, 5.f);
    else {
        ctx.r.fillRoundedRect(r, hot ? c.borderStrong : c.border, 5.f);
        ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, hot ? c.rowAltBg : c.panelBg, 4.f);
    }
    ctx.r.drawText({r.x + 11.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall,
                   primary ? c.selectionText : c.text);
    return r;
}

void card(const ui::PaintContext& ctx, const gfx::Rect& r, bool hot) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRoundedRect(r, hot ? c.borderStrong : c.border, 9.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 8.f);
}

} // namespace

// ================================================================ le cadre ====
ApiFrame::ApiFrame(std::string id, ui::WidgetPtr content) : ui::Widget(std::move(id)) {
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::make_unique<HmiToolStrip>(this->id() + ".barre")));
    content_ = &addChild(std::move(content));
}

void ApiFrame::setHint(std::string text, ui::Tone tone) {
    hint_ = std::move(text);
    tone_ = tone;
    invalidate();
}

void ApiFrame::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 40.f});
    const float hintH = hint_.empty() ? 0.f : 26.f;
    content_->setBounds({b.x, b.y + 40.f, b.w, std::max(0.f, b.h - 40.f - hintH)});
}

void ApiFrame::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    if (hint_.empty()) return;
    const gfx::Rect h{b.x, b.bottom() - 26.f, b.w, 26.f};
    ctx.r.fillRect(h, c.headerBg);
    ctx.r.fillRect({h.x, h.y, h.w, 1.f}, c.border);
    const auto col = tone_ == ui::Tone::None ? c.textMuted : ctx.theme.onSurface(ctx.theme.tone(tone_, c.textMuted));
    ui::drawIcon(ctx.r, tone_ == ui::Tone::Warning ? ui::Icon::Warning : tone_ == ui::Tone::Ok ? ui::Icon::Ok : ui::Icon::Info,
                 {h.x + 10.f, h.y + 6.f, 14.f, 14.f}, col);
    const auto n = ctx.r.fitCharacters(hint_, kSmall, h.w - 44.f);
    ctx.r.drawText({h.x + 30.f, h.y + (h.h - ctx.r.lineHeight(kSmall)) * 0.5f},
                   n >= hint_.size() ? hint_ : hint_.substr(0, n), kSmall, col);
}

// ======================================================= le tableau de bord ====
ApiDashboard::ApiDashboard(std::string id) : ui::Widget(std::move(id)) {}

ui::SizeHint ApiDashboard::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {900.f, 600.f};
    h.minimum = {300.f, 200.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

void ApiDashboard::setSummary(project::api::Summary summary, std::size_t macros) {
    s_ = std::move(summary);
    macros_ = macros;
    invalidate();
}

std::vector<ApiDashboard::Todo> ApiDashboard::todos() const {
    std::vector<Todo> out;
    if (!s_.outdated.empty()) {
        std::string d;
        std::size_t blocks = 0, types = 0;
        for (const auto& n : s_.outdated) {
            if (n.kind == project::LibraryItemKind::DerivedType) ++types;
            else ++blocks;
            if (!d.empty()) d += " \xC2\xB7 ";
            d += n.name + " " + n.projectVersion + " \xE2\x86\x92 " + n.libraryVersion;
        }
        std::string title = std::to_string(s_.outdated.size())
            + (s_.outdated.size() > 1 ? " \xC3\xA9l\xC3\xA9ments ont" : " \xC3\xA9l\xC3\xA9ment a")
            + " une version plus r\xC3\xA9" "cente dans la biblioth\xC3\xA8que";
        if (blocks && types) title += " (" + std::to_string(blocks) + " bloc" + (blocks > 1 ? "s" : "") + ", " + std::to_string(types) + " type" + (types > 1 ? "s" : "") + ")";
        out.push_back({ui::Tone::Warning, "\xE2\x86\x91", std::move(title), std::move(d), "Mettre \xC3\xA0 jour\xE2\x80\xA6", "bibliotheque"});
    }
    if (!s_.unused.empty()) {
        std::string d = "Pas utilis\xC3\xA9" "e ne veut pas dire \xC3\xA0 supprimer : l'IHM ou le syst\xC3\xA8me peut la lire. ";
        for (std::size_t i = 0; i < s_.unused.size() && i < 6; ++i) d += (i ? ", " : "") + s_.unused[i];
        if (s_.unused.size() > 6) d += "\xE2\x80\xA6";
        out.push_back({ui::Tone::Warning, "!", std::to_string(s_.unused.size()) + " variable" + (s_.unused.size() > 1 ? "s ne servent" : " ne sert")
                                                    + " pas au programme", std::move(d), "Voir", "variables-inutilisees"});
    }
    if (!s_.late.empty()) {
        std::string d;
        for (std::size_t i = 0; i < s_.late.size() && i < 3; ++i) {
            const auto& l = s_.late[i];
            if (!d.empty()) d += " \xC2\xB7 ";
            d += l.reader + " (rang " + std::to_string(l.readerRank) + ") lit " + l.variable + ", \xC3\xA9" "crite plus loin par "
                 + l.writer + " (rang " + std::to_string(l.writerRank) + ")";
        }
        if (s_.late.size() > 3) d += "\xE2\x80\xA6";
        out.push_back({ui::Tone::Info, "\xE2\x9F\xB2", std::to_string(s_.late.size()) + (s_.late.size() > 1 ? " lectures" : " lecture")
                                                    + " avant l'\xC3\xA9" "criture, dans l'ordre de " + (s_.mainTask.empty() ? std::string("la t\xC3\xA2" "che") : s_.mainTask),
                       std::move(d), "Ordre", "ordre"});
    }
    if (!s_.hasHardware)
        out.push_back({ui::Tone::Info, "i", "Pas de configuration mat\xC3\xA9rielle dans ce projet",
                       "L'export .XPG porte le processeur, pas le rack : importe le .XHW pour les racks, les modules et leurs voies.",
                       "Importer le .XHW\xE2\x80\xA6", "import-xhw"});
    out.push_back({ui::Tone::Ok, "\xE2\x9C\x93", std::to_string(s_.animationTables) + " tables d'animation (" + std::to_string(s_.animationEntries)
                                                + " lignes), " + std::to_string(macros_) + " macros",
                   "Les tables d'animation se modifient ; les macros ont leurs ic\xC3\xB4nes et leur l\xC3\xA9gende.",
                   "Macros", "macros"});
    // Lot API 7 : le didacticiel de l'API - la visite et cinq parcours guides.
    out.push_back({ui::Tone::Info, "?", "D\xC3\xA9" "couvrir l'API : la visite et cinq parcours guid\xC3\xA9s",
                   "Une bulle montre chaque endroit ; les parcours font faire (une variable suivie, un bloc mis \xC3\xA0 jour, l'ordre, une macro).",
                   "Didacticiel", "didacticiel"});
    return out;
}

std::vector<std::string> ApiDashboard::todoTitles() const {
    std::vector<std::string> out;
    for (const auto& t : todos()) out.push_back(t.title);
    return out;
}

gfx::Rect ApiDashboard::partRect(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key && h.button) return h.rect;
    for (const auto& h : hits_)
        if (h.key == key) return h.rect;
    return {};
}

void ApiDashboard::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    hits_.clear();
    if (!hovered()) hover_ = -1;
    int hitIndex = 0;
    const auto hotNext = [&] { return hover_ == hitIndex++; };
    ctx.r.pushClip(b);

    const float pad = 16.f;
    const float w = b.w - 2.f * pad;
    const int cols = w >= 1000.f ? 4 : 2;
    const float gap = 12.f;
    const float cw = (w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float ch = 150.f;
    float y = b.y + pad;

    // ---- 1. les cartes ------------------------------------------------------
    struct Card { std::string key, title, big, bigUnit; ui::Icon icon; std::vector<std::pair<std::string, std::string>> rows; ui::Tone warnRow{ui::Tone::None}; };
    std::vector<Card> cards;
    cards.push_back({"configuration", "Automate", s_.cpu.empty() ? std::string("\xE2\x80\x94") : s_.cpu, "", ui::Icon::Cpu,
                     {{"Famille", s_.family}, {"Syst\xC3\xA8me", s_.firmware}, {"Ressource", s_.resource},
                      {"Racks", s_.hasHardware ? std::to_string(s_.racks) + " (" + compte(s_.modules, "module", "modules") + ")" : std::string("pas de .XHW")}}});
    cards.push_back({"ordre", "Programme (" + (s_.mainTask.empty() ? std::string("t\xC3\xA2" "che") : s_.mainTask) + ")", std::to_string(s_.order.size()), s_.order.size() == 1 ? "entr\xC3\xA9" "e" : "entr\xC3\xA9" "es",
                     ui::Icon::Task,
                     {{"Sections", std::to_string(s_.taskSections)}, {"Unit\xC3\xA9s de programme", std::to_string(s_.units) + " (" + compte(s_.unitSections, "section", "sections") + ")"},
                      {"Lignes de code", thousands(s_.lines)}, {"Sous-routines", std::to_string(s_.subroutines)}}});
    cards.push_back({"types", "Types", std::to_string(s_.ddt + s_.dfb), "", ui::Icon::DerivedType,
                     {{"Types d\xC3\xA9riv\xC3\xA9s (DDT)", std::to_string(s_.ddt)}, {"dont de la biblioth\xC3\xA8que", std::to_string(s_.ddtFromLibrary)},
                      {"Blocs DFB", std::to_string(s_.dfb)}, {"plus r\xC3\xA9" "cents en biblioth\xC3\xA8que", std::to_string(s_.outdated.size())}},
                     s_.outdated.empty() ? ui::Tone::None : ui::Tone::Warning});
    cards.push_back({"variables", "Variables", thousands(s_.variables), "", ui::Icon::Variable,
                     {{"Situ\xC3\xA9" "es (adresse)", std::to_string(s_.located)}, {"Instances de blocs", std::to_string(s_.blockInstances)},
                      {"Pas utilis\xC3\xA9" "es par le programme", std::to_string(s_.unused.size())}, {"Globales", thousands(s_.variables)}},
                     s_.unused.empty() ? ui::Tone::None : ui::Tone::Warning});
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const auto& cd = cards[i];
        const float cx = b.x + pad + static_cast<float>(static_cast<int>(i) % cols) * (cw + gap);
        const float cy = y + static_cast<float>(static_cast<int>(i) / cols) * (ch + gap);
        const gfx::Rect r{cx, cy, cw, ch};
        const bool hot = hotNext();
        hits_.push_back({r, cd.key, false});
        card(ctx, r, hot);
        ui::drawIcon(ctx.r, cd.icon, {r.x + 14.f, r.y + 13.f, 16.f, 16.f}, c.textMuted);
        ctx.r.drawText({r.x + 38.f, r.y + 12.f}, upper(cd.title), kTitle, c.textMuted);
        const auto bigFit = ctx.r.fitCharacters(cd.big, kBig, r.w - 30.f);
        const std::string big = bigFit >= cd.big.size() ? cd.big : cd.big.substr(0, bigFit);
        drawBold(ctx, {r.x + 14.f, r.y + 32.f}, big, kBig, c.text);
        if (!cd.bigUnit.empty())
            ctx.r.drawText({r.x + 22.f + ctx.r.measure(big, kBig).width, r.y + 44.f}, cd.bigUnit, kBody, c.textMuted);
        float ry = r.y + 74.f;
        for (std::size_t k = 0; k < cd.rows.size(); ++k) {
            const auto& [key, val] = cd.rows[k];
            ctx.r.drawText({r.x + 14.f, ry}, key, kSmall, c.textMuted);
            const bool warn = cd.warnRow != ui::Tone::None && k == 3 && val != "0";
            const auto vcol = warn ? ctx.theme.onSurface(c.warning) : c.text;
            const float vw = ctx.r.measure(val, kSmall).width;
            ctx.r.drawText({r.right() - 14.f - vw, ry}, val, kSmall, vcol);
            ry += 18.f;
        }
    }
    y += static_cast<float>((cards.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols)) * (ch + gap) + 4.f;

    // ---- 2. a regarder, et le poids de chaque entree --------------------------
    const bool twoCols = w >= 900.f;
    const float leftW = twoCols ? w * 0.58f : w;
    const auto list = todos();
    const float rowH = 58.f;
    const gfx::Rect todoBox{b.x + pad, y, leftW, 44.f + static_cast<float>(list.size()) * rowH};
    card(ctx, todoBox, false);
    ui::drawIcon(ctx.r, ui::Icon::Warning, {todoBox.x + 14.f, todoBox.y + 13.f, 16.f, 16.f}, c.textMuted);
    ctx.r.drawText({todoBox.x + 38.f, todoBox.y + 12.f}, "\xC3\x80 REGARDER", kTitle, c.textMuted);
    float ty = todoBox.y + 40.f;
    for (const auto& t : list) {
        const gfx::Rect row{todoBox.x + 10.f, ty, todoBox.w - 20.f, rowH - 4.f};
        const auto tone = ctx.theme.tone(t.tone, c.textMuted);
        const gfx::Rect mark{row.x + 4.f, row.y + 8.f, 22.f, 22.f};
        ctx.r.fillRoundedRect(mark, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 11.f);
        const float mw = ctx.r.measure(t.mark, kSmall).width;
        ctx.r.drawText({mark.x + (mark.w - mw) * 0.5f, mark.y + (mark.h - ctx.r.lineHeight(kSmall)) * 0.5f}, t.mark, kSmall, ctx.theme.onSurface(tone));
        const bool hot = hotNext();
        const auto br = button(ctx, row.right() - 4.f, row.y + 20.f, t.button, hot, false);
        hits_.push_back({br, t.key, true});
        const float textW = br.x - (row.x + 36.f) - 10.f;
        const auto title = wrap(ctx, t.title, kBody, textW, 1);
        if (!title.empty()) ctx.r.drawText({row.x + 36.f, row.y + 4.f}, title.front(), kBody, c.text);
        const auto det = wrap(ctx, t.detail, kSmall, textW, 2);
        for (std::size_t k = 0; k < det.size(); ++k)
            ctx.r.drawText({row.x + 36.f, row.y + 23.f + static_cast<float>(k) * 15.f}, det[k], kSmall, c.textMuted);
        ty += rowH;
        if (&t != &list.back()) ctx.r.fillRect({row.x, ty - 3.f, row.w, 1.f}, c.border);
    }

    // le poids de chaque entree de la tache
    const float wx = twoCols ? todoBox.right() + gap : b.x + pad;
    const float wy = twoCols ? y : todoBox.bottom() + gap;
    const float ww = twoCols ? w - leftW - gap : w;
    auto sorted = s_.order;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& z) { return a.lines > z.lines; });
    if (sorted.size() > 8) sorted.resize(8);
    const gfx::Rect wb{wx, wy, ww, 64.f + static_cast<float>(sorted.size()) * 22.f + 22.f};
    card(ctx, wb, false);
    ui::drawIcon(ctx.r, ui::Icon::Chart, {wb.x + 14.f, wb.y + 13.f, 16.f, 16.f}, c.textMuted);
    ctx.r.drawText({wb.x + 38.f, wb.y + 12.f}, "LE POIDS DE CHAQUE ENTR\xC3\x89" "E", kTitle, c.textMuted);
    ctx.r.drawText({wb.x + 38.f + ctx.r.measure("LE POIDS DE CHAQUE ENTR\xC3\x89" "E", kTitle).width + 8.f, wb.y + 12.f}, "en lignes", kSmall, c.textMuted);
    std::size_t maxL = 1, unitLines = 0;
    for (const auto& e : s_.order) {
        maxL = std::max(maxL, e.lines);
        if (e.unit) unitLines += e.lines;
    }
    float by = wb.y + 42.f;
    const float nameW = std::min(190.f, ww * 0.38f);
    for (const auto& e : sorted) {
        const auto col = e.unit ? ctx.theme.tone(ui::Tone::Family3, c.accent) : c.accent;
        if (e.unit) ui::drawIcon(ctx.r, ui::Icon::Program, {wb.x + 14.f, by + 2.f, 12.f, 12.f}, col);
        const auto n = ctx.r.fitCharacters(e.name, kSmall, nameW - 20.f);
        ctx.r.drawText({wb.x + 30.f, by}, n >= e.name.size() ? e.name : e.name.substr(0, n), kSmall, c.text);
        const float bx = wb.x + 14.f + nameW, bw = std::max(20.f, ww - nameW - 80.f);
        ctx.r.fillRoundedRect({bx, by + 5.f, bw, 8.f}, c.border, 4.f);
        ctx.r.fillRoundedRect({bx, by + 5.f, std::max(3.f, bw * static_cast<float>(e.lines) / static_cast<float>(maxL)), 8.f}, col, 4.f);
        const auto lt = thousands(e.lines);
        ctx.r.drawText({wb.right() - 14.f - ctx.r.measure(lt, kSmall).width, by}, lt, kSmall, c.textMuted);
        by += 22.f;
    }
    if (s_.lines > 0 && s_.units > 0) {
        const auto pct = static_cast<int>(std::lround(100.0 * static_cast<double>(unitLines) / static_cast<double>(s_.lines)));
        // 1.11 (R111, recette T3-11) : « L'unite de programme : 5 lignes sur 5 »,
        // et non « Les 1 unites de programme ».
        const std::string qui = s_.units == 1 ? std::string("L'unit\xC3\xA9 de programme")
                                              : "Les " + std::to_string(s_.units) + " unit\xC3\xA9s de programme";
        ctx.r.drawText({wb.x + 14.f, by + 6.f}, qui + " : " + thousands(unitLines) + (unitLines == 1 ? " ligne sur " : " lignes sur ")
                                                    + thousands(s_.lines) + " (" + std::to_string(pct) + " %).", kSmall, c.textMuted);
    }
    ctx.r.popClip();
}

ui::EventResult ApiDashboard::onEvent(const ui::InputEvent& ev) {
    const auto at = [&](gfx::Point p) {
        // les boutons d'abord, puis les cartes
        for (std::size_t i = 0; i < hits_.size(); ++i)
            if (hits_[i].button && hits_[i].rect.contains(p)) return static_cast<int>(i);
        for (std::size_t i = 0; i < hits_.size(); ++i)
            if (!hits_[i].button && hits_[i].rect.contains(p)) return static_cast<int>(i);
        return -1;
    };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = at(m->pos);
        if (h != hover_) { hover_ = h; invalidate(); }
        return ui::EventResult::Ignored;
    }
    // Un clic, c'est l'appui ET le relacher sur la meme carte : le relacher seul
    // est celui du bouton d'un dialogue qui vient de se fermer au-dessus (Creer),
    // et il ouvrait la carte qui se trouvait dessous.
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left) {
        const int h = at(m->pos);
        const bool click = h >= 0 && h == pressed_;
        pressed_ = -1;
        if (click) {
            openRequested->emit(hits_[static_cast<std::size_t>(h)].key);
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
        pressed_ = at(m->pos);
        if (pressed_ >= 0) return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================================ l'etat vide ====
ApiEmptyState::ApiEmptyState(std::string id, std::string title, std::string text, std::vector<Way> ways)
    : ui::Widget(std::move(id)), title_(std::move(title)), text_(std::move(text)), ways_(std::move(ways)) {}

gfx::Rect ApiEmptyState::partRect(std::string_view key) const {
    for (const auto& [r, k] : hits_) if (k == key) return r;
    return {};
}

void ApiEmptyState::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    hits_.clear();
    if (!hovered()) hover_ = -1;
    const float w = std::min(760.f, b.w - 40.f);
    const float x = b.x + (b.w - w) * 0.5f;
    float y = b.y + std::max(24.f, b.h * 0.18f);
    const gfx::Rect icon{x, y, 48.f, 48.f};
    ctx.r.fillRoundedRect(icon, c.accent.withAlpha(ctx.theme.isDark() ? 60 : 36), 11.f);
    ui::drawIcon(ctx.r, ui::Icon::Section, {icon.x + 12.f, icon.y + 12.f, 24.f, 24.f}, ctx.theme.onSurface(c.accent));
    drawBold(ctx, {x + 64.f, y + 2.f}, title_, gfx::FontId{20}, c.text);
    const auto lines = wrap(ctx, text_, kBody, w - 64.f, 3);
    for (std::size_t k = 0; k < lines.size(); ++k)
        ctx.r.drawText({x + 64.f, y + 30.f + static_cast<float>(k) * 19.f}, lines[k], kBody, c.textMuted);
    y += 30.f + static_cast<float>(std::max<std::size_t>(lines.size(), 1)) * 19.f + 24.f;
    const float gap = 12.f;
    const auto n = std::max<std::size_t>(ways_.size(), 1);
    const float cw = (w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
    for (std::size_t i = 0; i < ways_.size(); ++i) {
        const auto& way = ways_[i];
        const gfx::Rect r{x + static_cast<float>(i) * (cw + gap), y, cw, 136.f};
        card(ctx, r, false);
        drawBold(ctx, {r.x + 14.f, r.y + 12.f}, way.title, kBody, c.text);
        const auto t = wrap(ctx, way.text, kSmall, r.w - 28.f, 3);
        for (std::size_t k = 0; k < t.size(); ++k)
            ctx.r.drawText({r.x + 14.f, r.y + 36.f + static_cast<float>(k) * 16.f}, t[k], kSmall, c.textMuted);
        const bool hot = hover_ == static_cast<int>(i);
        const float bw = ctx.r.measure(way.button, kSmall).width + 22.f;
        const auto br = button(ctx, r.x + 14.f + bw, r.bottom() - 26.f, way.button, hot, i == 0);
        hits_.emplace_back(br, way.key);
    }
}

ui::EventResult ApiEmptyState::onEvent(const ui::InputEvent& ev) {
    const auto at = [&](gfx::Point p) {
        for (std::size_t i = 0; i < hits_.size(); ++i) if (hits_[i].first.contains(p)) return static_cast<int>(i);
        return -1;
    };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = at(m->pos);
        if (h != hover_) { hover_ = h; invalidate(); }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left) {
        const int h = at(m->pos);
        const bool click = h >= 0 && h == pressed_;      // l'appui et le relacher, ici
        pressed_ = -1;
        if (click) { openRequested->emit(hits_[static_cast<std::size_t>(h)].second); return ui::EventResult::Consumed; }
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
        pressed_ = at(m->pos);
        if (pressed_ >= 0) return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
