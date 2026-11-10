// =============================================================================
//  ui/widgets/UnfoldCard.cpp - 1.47.0 (planches 01, 07) : ce qu'une ligne depliee
//  montre (voir UnfoldCard.hpp)
// =============================================================================
#include "UnfoldCard.hpp"

#include "../Icons.hpp"
#include "../Shapes.hpp"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

constexpr float kPadX = 18.f, kPadTop = 12.f, kPadBottom = 12.f, kGap = 12.f;
constexpr float kLineH = 26.f, kTitleH = 30.f, kBoxPad = 12.f, kTableRowH = 30.f, kActionsH = 34.f;
const gfx::FontId kText{13}, kSmall{12}, kTitle{11};

struct Segment {
    std::string text;
    bool        bold{false};
};

// << Licence **XPG-2026-00391** >> -> les morceaux, le gras entre deux **.
std::vector<Segment> segmentsOf(const std::string& s) {
    std::vector<Segment> out;
    bool bold = false;
    std::size_t a = 0;
    for (;;) {
        const std::size_t b = s.find("**", a);
        const std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty()) out.push_back({part, bold});
        if (b == std::string::npos) break;
        bold = !bold;
        a = b + 2;
    }
    return out;
}

std::string plainOf(const std::string& s) {
    std::string t;
    for (const auto& seg : segmentsOf(s)) t += seg.text;
    return t;
}

std::string elide(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 6.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

std::string initialOf(const std::string& name) {
    if (name.empty()) return "?";
    const unsigned char c = static_cast<unsigned char>(name[0]);
    if (c < 0x80) return std::string(1, static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    std::size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    return name.substr(0, std::min(n, name.size()));
}

void tick(gfx::IRenderer& r, float x, float y, gfx::Color c) {
    r.line({x, y + 5.f}, {x + 3.5f, y + 8.5f}, c, 1.8f);
    r.line({x + 3.5f, y + 8.5f}, {x + 10.f, y + 1.5f}, c, 1.8f);
}

void arrowOut(gfx::IRenderer& r, float x, float y, gfx::Color c) {   // le petit << lien >> (une fleche en biais)
    r.line({x + 2.f, y + 9.f}, {x + 9.f, y + 2.f}, c, 1.4f);
    r.line({x + 4.f, y + 2.f}, {x + 9.f, y + 2.f}, c, 1.4f);
    r.line({x + 9.f, y + 2.f}, {x + 9.f, y + 7.f}, c, 1.4f);
}

} // namespace

// ============================================================================ la pastille d'un admin
// 1.47.0 : la place d'une pastille autour de son nom (l'initiale, la fleche).
constexpr float kPersonMin = 4.f + 18.f + 6.f + 8.f;

float personWidth(const UnfoldCard::Person& p, gfx::FontId font, float maxW) {
    std::string label = p.name;
    if (!p.count.empty()) label += " \xC2\xB7 " + p.count;
    return std::min(maxW, kPersonMin + measureWidth(label, font) + (p.open ? 14.f : 0.f));
}

float paintPerson(const PaintContext& ctx, float x, float y, float h, const UnfoldCard::Person& p, bool hovered, float maxW, bool* cut) {
    const auto& c = ctx.theme.color;
    const gfx::FontId f = gfx::semibold(kSmall);
    std::string label = p.name;
    if (!p.count.empty()) label += " \xC2\xB7 " + p.count;
    const float extra = kPersonMin + (p.open ? 14.f : 0.f);
    float w = extra + ctx.r.measure(label, f).width;
    if (cut) *cut = false;
    if (w > maxW) {   // 1.47.0 : le nom raccourci, la pastille tient dans sa place
        const std::string full = label;
        label = elide(ctx.r, label, f, std::max(8.f, maxW - extra));
        if (cut) *cut = label != full;
        w = std::min(maxW, extra + ctx.r.measure(label, f).width);
    }
    const gfx::Rect box{x, y, w, h};
    ctx.r.fillRoundedRect(box, c.accent.withAlpha(hovered ? 40 : 22), 7.f);
    const gfx::Rect av{x + 4.f, y + (h - 18.f) * 0.5f, 18.f, 18.f};
    ctx.r.fillRoundedRect(av, p.muted ? p.color.withAlpha(130) : p.color, 5.f);
    const std::string ini = initialOf(p.name);
    const gfx::FontId fi = gfx::semibold(gfx::FontId{10});
    ctx.r.drawText({av.x + (av.w - ctx.r.measure(ini, fi).width) * 0.5f, av.y + (av.h - ctx.r.lineHeight(fi)) * 0.5f}, ini, fi, gfx::Color::rgb(0xFFFFFF));
    const float tx = av.right() + 6.f;
    const gfx::Color ink = p.muted ? c.textMuted : c.accentPressed;
    ctx.r.drawText({tx, y + (h - ctx.r.lineHeight(f)) * 0.5f}, label, f, ink);
    if (p.open) {
        const float ax = tx + ctx.r.measure(label, f).width + 5.f, ay = y + h * 0.5f;
        ctx.r.line({ax, ay - 3.5f}, {ax + 3.5f, ay}, ink, 1.4f);
        ctx.r.line({ax + 3.5f, ay}, {ax, ay + 3.5f}, ink, 1.4f);
    }
    return w;
}

PersonChip::PersonChip(UnfoldCard::Person person, std::string id) : Button(person.name, std::move(id)), person_(std::move(person)) {
    setTooltip(person_.open ? "Sa fiche" : std::string());
}

std::string PersonChip::liveTooltip(gfx::Point mouse) const {
    const std::string t = Widget::liveTooltip(mouse);
    if (!cut_) return t;
    return t.empty() ? person_.name : person_.name + " \xE2\x80\x94 " + t;
}

SizeHint PersonChip::sizeHint() const {
    SizeHint h = Button::sizeHint();
    h.preferred.w = personWidth(person_, gfx::semibold(kSmall));
    h.preferred.h = 26.f;
    return h;
}

void PersonChip::onPaint(const PaintContext& ctx) {
    const gfx::Rect b = bounds();
    bool cut = false;
    (void)paintPerson(ctx, b.x, b.y + (b.h - 26.f) * 0.5f, 26.f, person_, hovered(), b.w, &cut);
    cut_ = cut;
}

float UnfoldCard::drawFitted(const PaintContext& ctx, gfx::Point at, const std::string& text, gfx::FontId f, gfx::Color ink, float w) const {
    if (w <= 6.f || text.empty()) return 0.f;
    const std::string shown = elide(ctx.r, text, f, w);
    ctx.r.drawText(at, shown, f, ink);
    const float sw = ctx.r.measure(shown, f).width;
    if (shown != text) tips_.push_back({{at.x, at.y - 2.f, sw, ctx.r.lineHeight(f) + 4.f}, text});
    return sw;
}

std::string UnfoldCard::liveTooltip(gfx::Point mouse) const {
    for (const auto& t : tips_)
        if (t.rect.contains(mouse)) return t.text;
    return Widget::liveTooltip(mouse);
}

// ============================================================================ la carte
UnfoldCard::UnfoldCard(std::string id) : Widget(std::move(id)) {}

void UnfoldCard::addBox(Box box) {
    boxes_.push_back(std::move(box));
    invalidateLayout();
    invalidate();
}

void UnfoldCard::setCurve(std::string title, std::function<void(const PaintContext&, gfx::Rect)> paint, float height, float weight) {
    if (!paint) return;
    Box b;
    b.title = std::move(title);
    b.paint = std::move(paint);
    b.height = height;
    b.weight = weight;
    addBox(std::move(b));
}

Button& UnfoldCard::addButton(std::string label, std::string id, Icon icon, std::function<void()> run, std::string why, bool right) {
    auto b = std::make_unique<Button>(std::move(label), std::move(id));
    b->setIcon(icon);
    // Le premier bouton (a gauche) : cadre ; les suivants a plat (planche 07 : << Ouvrir >>, puis Prolonger...).
    bool first = true;
    for (const auto& it : items_)
        if (it.kind == 0 && !it.right) first = false;
    b->setStyle(first && !right ? Button::Style::Default : Button::Style::Flat);
    if (!why.empty()) {
        b->setEnabled(false);
        b->setTooltip(why);
    }
    Button& ref = static_cast<Button&>(addChild(std::move(b)));
    if (run) ref.clicked->connect(std::move(run)).release();
    items_.push_back({0, &ref, {}, right, {}});
    invalidateLayout();
    return ref;
}

Button& UnfoldCard::addPerson(Person person, std::string id, bool right) {
    auto open = person.open;
    auto chip = std::make_unique<PersonChip>(std::move(person), std::move(id));
    Button& ref = static_cast<Button&>(addChild(std::move(chip)));
    if (open) ref.clicked->connect(std::move(open)).release();
    items_.push_back({1, &ref, {}, right, {}});
    invalidateLayout();
    return ref;
}

void UnfoldCard::addText(std::string text, bool right) {
    items_.push_back({2, nullptr, std::move(text), right, {}});
    invalidateLayout();
}

float UnfoldCard::boxHeight(const Box& b) const {
    float h = b.title.empty() ? 0.f : kTitleH;
    if (b.paint) h += b.height;
    else if (!b.table.empty() || !b.head.empty()) h += (b.head.empty() ? 0.f : 24.f) + kTableRowH * static_cast<float>(b.table.size());
    else h += kLineH * static_cast<float>(b.lines.size());
    if (!b.title.empty()) h += kBoxPad - 4.f;
    return h;
}

float UnfoldCard::actionsHeight() const { return items_.empty() ? 0.f : kActionsH + 10.f; }

float UnfoldCard::heightFor(float) const {
    float boxes = 0.f;
    for (const auto& b : boxes_) boxes = std::max(boxes, boxHeight(b));
    return kPadTop + boxes + actionsHeight() + kPadBottom;
}

SizeHint UnfoldCard::sizeHint() const {
    SizeHint h;
    h.preferred = {bounds().w, heightFor(bounds().w)};
    h.minimum = h.preferred;
    return h;
}

std::vector<gfx::Rect> UnfoldCard::boxRects(float width, float top) const {
    std::vector<gfx::Rect> out;
    float weights = 0.f;
    for (const auto& b : boxes_) weights += std::max(0.1f, b.weight);
    const float inner = std::max(60.f, width - 2.f * kPadX - kGap * static_cast<float>(boxes_.empty() ? 0 : boxes_.size() - 1));
    float x = bounds().x + kPadX;
    for (const auto& b : boxes_) {
        const float w = std::floor(inner * std::max(0.1f, b.weight) / std::max(0.1f, weights));
        out.push_back({x, top, w, boxHeight(b)});
        x += w + kGap;
    }
    return out;
}

void UnfoldCard::onLayout() {
    const gfx::Rect b = bounds();
    float boxes = 0.f;
    for (const auto& bx : boxes_) boxes = std::max(boxes, boxHeight(bx));
    const float y = b.y + kPadTop + boxes + (boxes > 0.f ? 10.f : 0.f);
    float left = b.x + kPadX, right = b.right() - kPadX;
    // 1.47.0 : la rangee tient dans la carte - trop longue, chaque morceau cede de sa largeur (jusqu'a un minimum), en
    // proportion de ce qu'il a de trop ; un bouton, une pastille, des mots raccourcis le disent en infobulle.
    std::vector<float> want, least;
    float total = 0.f, slack = 0.f;
    for (const auto& it : items_) {
        float w = 0.f;
        if (it.widget) w = std::max(30.f, it.widget->sizeHint().preferred.w + (it.kind == 0 ? 6.f : 0.f));
        else w = measureWidth(it.text, kSmall) + 4.f;
        const float lo = std::min(w, it.kind == 2 ? 40.f : 64.f);
        want.push_back(w);
        least.push_back(lo);
        total += w + 8.f;
        slack += w - lo;
    }
    const float over = total - 8.f - (right - left);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        auto& it = items_[i];
        float w = want[i];
        if (over > 0.f && slack > 0.f) w = std::max(least[i], w - over * (want[i] - least[i]) / slack);
        if (it.right) {
            right -= w;
            it.rect = {right, y, w, kActionsH - 2.f};
            right -= 8.f;
        } else {
            it.rect = {left, y, w, kActionsH - 2.f};
            left += w + 8.f;
        }
        if (it.widget) {
            const float h = it.kind == 1 ? 26.f : kActionsH - 2.f;
            it.widget->setBounds({it.rect.x, it.rect.y + (kActionsH - 2.f - h) * 0.5f, it.rect.w, h});
        }
    }
}

float UnfoldCard::paintLine(const PaintContext& ctx, const Line& l, float x, float y, float w, bool timeline) const {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const float cy = y + kLineH * 0.5f;
    const float textY = y + (kLineH - r.lineHeight(kText)) * 0.5f;
    const float x0 = x;
    const float right = x + w - (l.link ? 18.f : 0.f);
    if (timeline) {
        r.fillRoundedRect({x + 1.f, cy - 3.f, 6.f, 6.f}, c.accent, 3.f);
        x += 14.f;
    }
    if (!l.time.empty()) {
        const float tw = std::min(r.measure(l.time, kSmall).width, std::max(20.f, (right - x) * 0.35f));
        (void)drawFitted(ctx, {x, y + (kLineH - r.lineHeight(kSmall)) * 0.5f}, l.time, kSmall, c.textMuted, tw + 1.f);
        x += std::max(38.f, tw + 8.f);
    }
    if (l.icon != Icon::None) {
        drawIcon(r, l.icon, {x, cy - 7.f, 14.f, 14.f}, c.textMuted);
        x += 22.f;
    }
    // Ce qui suit le texte (la coche, les pastilles, le texte d'apres) : sa place est gardee.
    std::vector<Person> people = l.people;
    if (l.person) people.insert(people.begin(), *l.person);
    // 1.47.0 : les pastilles ne prennent jamais plus que la moitie de la ligne (a parts egales), le texte d'apres un quart.
    const float avail = std::max(20.f, right - x);
    const float personMax = people.empty() ? 0.f : std::max(kPersonMin + 24.f, avail * 0.5f / static_cast<float>(people.size()) - 6.f);
    const float afterW = l.after.empty() ? 0.f : std::min(r.measure(l.after, kText).width, avail * 0.25f) + 8.f;
    float tail = (l.check ? 18.f : 0.f) + afterW;
    for (const auto& p : people) tail += personWidth(p, gfx::semibold(kSmall), personMax) + 6.f;
    const gfx::Color ink = ctx.theme.tone(l.tone, c.text);
    float tx = x;
    const float textRoom = std::max(20.f, right - x - tail);
    bool textCut = false;
    for (const auto& seg : segmentsOf(l.text)) {
        const gfx::FontId f = l.mono ? ctx.theme.font.mono : seg.bold ? gfx::semibold(kText) : kText;
        const float room = x + textRoom - tx;
        if (room <= 8.f) {
            textCut = true;
            break;
        }
        const std::string shown = elide(r, seg.text, f, room);
        textCut = textCut || shown != seg.text;
        r.drawText({tx, textY}, shown, f, ink);
        tx += r.measure(shown, f).width;
    }
    if (textCut) tips_.push_back({{x, y, std::max(10.f, tx - x), kLineH}, plainOf(l.text)});
    if (l.check) {
        tick(r, tx + 6.f, cy - 5.f, c.ok);
        tx += 18.f;
    } else if (!l.text.empty()) {
        tx += 6.f;
    }
    for (const auto& p : people) {
        bool cut = false;
        const float pw = paintPerson(ctx, tx, cy - 11.f, 22.f, p, false, std::min(personMax, std::max(kPersonMin + 8.f, right - tx)), &cut);
        if (p.open) hits_.push_back({{tx, cy - 11.f, pw, 22.f}, p.open});
        if (cut) tips_.push_back({{tx, cy - 11.f, pw, 22.f}, p.name});
        tx += pw + 6.f;
    }
    if (!l.after.empty() && tx < right - 8.f) (void)drawFitted(ctx, {tx, textY}, l.after, kText, c.textMuted, right - tx);
    if (l.link) {
        arrowOut(r, x0 + w - 12.f, cy - 6.f, c.accent);
        hits_.push_back({{x0 - 2.f, y, std::max(10.f, w + 4.f), kLineH}, l.link});
    }
    return kLineH;
}

void UnfoldCard::onPaint(const PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const gfx::Rect b = bounds();
    hits_.clear();
    tips_.clear();
    if (ownBackground_) {
        r.fillRect(b, c.accent.withAlpha(8));
        r.fillRect({b.x, b.y, 3.f, b.h}, c.accent);
    }
    const auto rects = boxRects(b.w, b.y + kPadTop);
    for (std::size_t i = 0; i < boxes_.size() && i < rects.size(); ++i) {
        const Box& bx = boxes_[i];
        const gfx::Rect rc = rects[i];
        float x = rc.x, y = rc.y, w = rc.w;
        if (!bx.title.empty()) {
            shapes::fillFramedRect(r, rc, c.panelBg, c.border, 10.f);
            x += 14.f;
            w -= 28.f;
            (void)drawFitted(ctx, {x, y + 11.f}, bx.title, gfx::semibold(kTitle), c.textMuted, w);
            y += kTitleH;
        }
        if (bx.paint) {
            bx.paint(ctx, {x, y, w, bx.height});
            continue;
        }
        if (!bx.head.empty() || !bx.table.empty()) {
            const std::size_t cols = std::max<std::size_t>(bx.head.size(), bx.table.empty() ? 0 : bx.table.front().size());
            if (cols == 0) continue;
            const float c0 = std::floor(w * (cols == 3 ? 0.38f : 1.f / static_cast<float>(cols)));
            const float rest = cols > 1 ? (w - c0) / static_cast<float>(cols - 1) : 0.f;
            const auto colX = [&](std::size_t k) { return k == 0 ? x : x + c0 + rest * static_cast<float>(k - 1); };
            const auto colW = [&](std::size_t k) { return (k == 0 ? c0 : rest) - 8.f; };
            if (!bx.head.empty()) {
                for (std::size_t k = 0; k < bx.head.size(); ++k)
                    (void)drawFitted(ctx, {colX(k), y + 4.f}, bx.head[k], gfx::semibold(kTitle), c.textMuted, colW(k));
                y += 24.f;
            }
            for (const auto& row : bx.table) {
                r.fillRect({rc.x + 1.f, y, rc.w - 2.f, 1.f}, c.gridLine);
                for (std::size_t k = 0; k < row.size() && k < cols; ++k) {
                    const gfx::FontId f = k + 1 == cols && cols > 1 ? gfx::semibold(kText) : kText;
                    const gfx::Color ink = k == 0 ? c.text : (k + 1 == cols ? c.text : c.textMuted);
                    (void)drawFitted(ctx, {colX(k), y + (kTableRowH - r.lineHeight(f)) * 0.5f}, row[k], f, ink, colW(k));
                }
                y += kTableRowH;
            }
            continue;
        }
        for (const auto& l : bx.lines) {
            y += paintLine(ctx, l, x, y, w, bx.timeline);
        }
    }
    // Les mots de la rangee d'actions (les boutons se dessinent seuls) : raccourcis a leur place.
    for (const auto& it : items_)
        if (it.kind == 2) (void)drawFitted(ctx, {it.rect.x, it.rect.y + (it.rect.h - r.lineHeight(kSmall)) * 0.5f}, it.text, kSmall, c.textMuted, it.rect.w);
}

EventResult UnfoldCard::onEvent(const InputEvent& ev) {
    if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left && bounds().contains(d->pos)) {
        for (const auto& h : hits_)
            if (h.rect.contains(d->pos) && h.run) {
                const auto run = h.run;   // une copie : l'action peut refaire la carte
                run();
                return EventResult::Consumed;
            }
    }
    return EventResult::Ignored;
}

bool UnfoldCard::clickLink(std::size_t box, std::size_t line) {
    if (box >= boxes_.size() || line >= boxes_[box].lines.size()) return false;
    const auto& l = boxes_[box].lines[line];
    const auto run = l.link ? l.link : l.person && l.person->open ? l.person->open : std::function<void()>{};
    if (!run) return false;
    run();
    return true;
}

std::string UnfoldCard::text() const {
    std::string t;
    for (const auto& b : boxes_) {
        if (!b.title.empty()) t += b.title + "\n";
        for (const auto& h : b.head) t += h + "\t";
        if (!b.head.empty()) t += "\n";
        for (const auto& row : b.table) {
            for (const auto& cell : row) t += cell + "\t";
            t += "\n";
        }
        for (const auto& l : b.lines) {
            std::string line = (l.time.empty() ? "" : l.time + " ") + plainOf(l.text);
            if (l.person) line += " " + l.person->name;
            for (const auto& p : l.people) line += " " + p.name + (p.count.empty() ? "" : " \xC2\xB7 " + p.count);
            if (!l.after.empty()) line += " " + l.after;
            t += line + "\n";
        }
    }
    for (const auto& it : items_) {
        if (it.kind == 2) t += it.text + "\n";
        else if (const auto* btn = dynamic_cast<const Button*>(it.widget)) t += btn->text() + "\n";
    }
    return t;
}

} // namespace ui
