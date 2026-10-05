#include "HmiObjectTutorial.hpp"

#include "HmiIcons.hpp"
#include "../../hmi/HmiGuide.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

namespace {

constexpr float kListW = 290.f;       // la colonne des chapitres
constexpr float kListHead = 34.f;
constexpr float kRowH = 46.f;
constexpr float kBarH = 54.f;         // la barre de lecture
constexpr double kSpeeds[3] = {0.5, 1.0, 2.0};

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f s", std::max(0.0, s));
    std::string out(buf);
    if (const auto dot = out.find('.'); dot != std::string::npos) out[dot] = ',';
    return out;
}

bool inside(const gfx::Rect& r, gfx::Point p) { return r.w > 0 && r.h > 0 && r.contains(p); }

// Un texte coupe a la largeur, mot a mot.
std::vector<std::string> wrap(gfx::IRenderer& r, std::string_view text, gfx::FontId f, float maxW) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto sp = text.find(' ', at);
        const std::string word(text.substr(at, sp == std::string_view::npos ? std::string_view::npos : sp - at));
        at = sp == std::string_view::npos ? text.size() : sp + 1;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(trial, f).width > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void pill(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color fill, gfx::Color stroke) {
    r.fillRoundedRect(b, fill, 6.f);
    if (stroke.a) r.strokeRect(b, stroke, 1.f);
}

} // namespace

HmiObjectTutorial::HmiObjectTutorial(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);
    auto stage = std::make_unique<HmiExampleView>(this->id() + ".stage");
    stage_ = &static_cast<HmiExampleView&>(addChild(std::move(stage)));
    stage_->setTitle("Tutoriel \xC2\xB7 jou\xC3\xA9 par le vrai moteur");
}

void HmiObjectTutorial::setKind(std::optional<hmi::Kind> kind) {
    if (kind == kind_ && stage_->manual()) return;
    kind_ = kind;
    chapters_.clear();
    stage_->setKind(kind);
    if (kind && stage_->hasExample()) {
        stage_->setManual(true);
        if (const auto* ex = stage_->example()) chapters_ = hmi::examples::tutorialFor(*kind, *ex);
        if (const auto* t = hmi::guide::topicForKind(hmi::kindKey(*kind))) stage_->setCaption(t->title);
    }
    dragging_ = false;
    invalidateLayout();
    invalidate();
}

void HmiObjectTutorial::play() {
    if (!hasTutorial()) return;
    if (!stage_->interactive() && stage_->position() >= stage_->period() - 1e-6) stage_->seek(0.0);
    stage_->setPlaying(true);
    invalidate();
}

void HmiObjectTutorial::pause() {
    stage_->setPlaying(false);
    invalidate();
}

void HmiObjectTutorial::togglePlay() {
    if (stage_->playing()) pause();
    else play();
}

void HmiObjectTutorial::stop() {
    if (!hasTutorial()) return;
    stage_->setInteractive(false);
    stage_->seek(0.0);
    stage_->setPlaying(false);
    invalidate();
}

void HmiObjectTutorial::setSpeed(double factor) {
    stage_->setSpeed(factor);
    invalidate();
}

void HmiObjectTutorial::seek(double s) {
    if (!hasTutorial()) return;
    stage_->seek(s);
    invalidate();
}

void HmiObjectTutorial::goToChapter(std::size_t index) {
    if (!hasTutorial() || index >= chapters_.size()) return;
    const auto& c = chapters_[index];
    stage_->seek(c.at);
    if (c.yourTurn) yourTurn();
    else stage_->setPlaying(true);
    invalidate();
}

void HmiObjectTutorial::yourTurn() {
    if (!hasTutorial()) return;
    // La ou en est l'exemple : le moteur continue, le scenario se tait.
    stage_->setInteractive(true);
    grabFocus();
    invalidate();
}

void HmiObjectTutorial::guided() {
    if (!hasTutorial()) return;
    stage_->setInteractive(false);
    stage_->seek(0.0);
    stage_->setPlaying(false);
    invalidate();
}

std::size_t HmiObjectTutorial::currentChapter() const {
    if (chapters_.empty()) return 0;
    if (stage_->interactive()) {
        for (std::size_t i = chapters_.size(); i-- > 0;)
            if (chapters_[i].yourTurn) return i;
        return chapters_.size() - 1;
    }
    std::size_t cur = 0;
    const double t = stage_->position();
    for (std::size_t i = 0; i < chapters_.size(); ++i)
        if (chapters_[i].at <= t + 1e-6 && !chapters_[i].yourTurn) cur = i;
    // A la fin du tour : "A toi" (s'il vient apres).
    if (t >= stage_->period() - 1e-6)
        for (std::size_t i = cur; i < chapters_.size(); ++i)
            if (chapters_[i].yourTurn) cur = i;
    return cur;
}

bool HmiObjectTutorial::controlRect(std::string_view name, gfx::Rect& out) const {
    const auto give = [&](const gfx::Rect& r) {
        out = r;
        return r.w > 0 && r.h > 0;
    };
    if (name == "debut") return give(start_);
    if (name == "lecture") return give(playBtn_);
    if (name == "arret") return give(stopBtn_);
    if (name == "frise") return give(frise_);
    if (name == "a-toi") return give(yourTurn_);
    if (name == "vitesse:0.5") return give(speeds_[0]);
    if (name == "vitesse:1") return give(speeds_[1]);
    if (name == "vitesse:2") return give(speeds_[2]);
    if (name.rfind("chapitre:", 0) == 0) {
        const auto n = static_cast<std::size_t>(std::max(1, std::atoi(std::string(name.substr(9)).c_str())));
        return n <= rows_.size() && give(rows_[n - 1]);
    }
    return false;
}

double HmiObjectTutorial::timeAtX(float x) const {
    const double period = stage_->period();
    if (frise_.w <= 0 || period <= 0) return 0.0;
    return std::clamp(static_cast<double>((x - frise_.x) / frise_.w), 0.0, 1.0) * period;
}

float HmiObjectTutorial::xAtTime(double t) const {
    const double period = stage_->period();
    if (period <= 0) return frise_.x;
    return frise_.x + frise_.w * static_cast<float>(std::clamp(t / period, 0.0, 1.0));
}

void HmiObjectTutorial::onLayout() {
    const auto b = bounds();
    list_ = {b.x, b.y, std::min(kListW, b.w * 0.34f), b.h};
    const gfx::Rect right{list_.right(), b.y, std::max(0.f, b.w - list_.w), b.h};
    stage_->setBounds({right.x, right.y, right.w, std::max(0.f, right.h - kBarH)});
    bar_ = {right.x, right.bottom() - kBarH, right.w, kBarH};
    // La barre : |< > [] | x0,5 x1 x2 | frise | temps | A toi
    const float cy = bar_.y + (kBarH - 34.f) / 2.f;
    float x = bar_.x + 10.f;
    start_ = {x, cy, 34.f, 34.f};
    x += 38.f;
    playBtn_ = {x, cy, 42.f, 34.f};
    x += 46.f;
    stopBtn_ = {x, cy, 34.f, 34.f};
    x += 46.f;
    for (int k = 0; k < 3; ++k) {
        speeds_[k] = {x, cy + 3.f, 46.f, 28.f};
        x += 50.f;
    }
    x += 10.f;
    const float turnW = 118.f, timeW = 118.f;
    yourTurn_ = {bar_.right() - turnW - 10.f, cy, turnW, 34.f};
    const float friseEnd = yourTurn_.x - timeW - 8.f;
    frise_ = {x + 8.f, bar_.y + kBarH / 2.f - 3.f, std::max(40.f, friseEnd - x - 16.f), 6.f};
    // Les chapitres.
    rows_.clear();
    float y = list_.y + kListHead;
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        rows_.push_back({list_.x + 6.f, y, list_.w - 12.f, kRowH - 4.f});
        y += kRowH;
    }
}

void HmiObjectTutorial::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    auto& r = ctx.r;
    r.fillRect(bounds(), c.panelBg);
    if (!hasTutorial()) {
        r.drawText({bounds().x + 16.f, bounds().y + 16.f}, "Cet objet n'a pas de tutoriel.", f.ui, c.textMuted);
        return;
    }
    // ---- les chapitres
    r.fillRect(list_, c.windowBg);
    r.drawText({list_.x + 12.f, list_.y + (kListHead - r.lineHeight(f.smallUi)) / 2.f}, "CHAPITRES", f.smallUi, c.textMuted);
    const std::size_t cur = currentChapter();
    for (std::size_t i = 0; i < chapters_.size() && i < rows_.size(); ++i) {
        const auto& ch = chapters_[i];
        const auto& row = rows_[i];
        if (row.bottom() > list_.bottom()) break;
        const bool isCur = i == cur;
        const bool done = !stage_->interactive() && i < cur;
        if (isCur) r.fillRoundedRect(row, c.accent.withAlpha(60), 6.f);
        else if (static_cast<int>(i) == hoverRow_) r.fillRoundedRect(row, c.text.withAlpha(18), 6.f);
        // Le numero dans un rond (une main pour "A toi").
        const gfx::Point centre{row.x + 18.f, row.y + row.h / 2.f};
        const gfx::Color dot = ch.yourTurn ? c.ok : isCur || done ? c.accent : c.textMuted;
        const auto circle = ui::shapes::ellipse(centre, 11.f, 11.f, 24);
        if (isCur || done) ui::shapes::fillPolygon(r, circle, dot);
        else ui::shapes::strokePolyline(r, circle, true, dot, 1.5f);
        const std::string num = ch.yourTurn ? std::string("\xE2\x98\x85") : std::to_string(i + 1);
        const float nw = r.measure(num, f.smallUi).width;
        r.drawText({centre.x - nw / 2.f, centre.y - r.lineHeight(f.smallUi) / 2.f}, num, f.smallUi,
                   isCur || done ? gfx::Color::rgb(0xFFFFFF) : c.textMuted);
        const float tx = row.x + 38.f, room = row.w - 38.f - 46.f;
        const auto n = r.fitCharacters(ch.title, isCur ? f.uiBold : f.ui, room);
        r.drawText({tx, row.y + (row.h - r.lineHeight(f.ui)) / 2.f}, std::string_view(ch.title).substr(0, n), isCur ? f.uiBold : f.ui,
                   isCur ? c.text : c.textMuted);
        const std::string at = ch.yourTurn ? std::string("\xC3\xA0 toi") : seconds(ch.at);
        r.drawText({row.right() - 6.f - r.measure(at, f.smallUi).width, row.y + (row.h - r.lineHeight(f.smallUi)) / 2.f}, at, f.smallUi,
                   c.textMuted);
    }
    // ---- la barre de lecture
    r.fillRect(bar_, c.windowBg);
    r.line({bar_.x, bar_.y}, {bar_.right(), bar_.y}, c.border, 1.f);
    const gfx::Color btn = c.headerBg, ink = c.text;
    // Au debut : une barre et un triangle vers la gauche.
    pill(r, start_, btn, c.border);
    {
        const float mx = start_.x + start_.w / 2.f, my = start_.y + start_.h / 2.f;
        r.fillRect({mx - 7.f, my - 7.f, 2.5f, 14.f}, ink);
        ui::shapes::fillPolygon(r, {{mx + 6.f, my - 7.f}, {mx + 6.f, my + 7.f}, {mx - 4.f, my}}, ink);
    }
    // Lecture / pause.
    pill(r, playBtn_, stage_->playing() && !stage_->interactive() ? c.accent : btn, c.border);
    {
        const float mx = playBtn_.x + playBtn_.w / 2.f, my = playBtn_.y + playBtn_.h / 2.f;
        const gfx::Color pinc = stage_->playing() && !stage_->interactive() ? gfx::Color::rgb(0xFFFFFF) : ink;
        if (stage_->playing() && !stage_->interactive()) {
            r.fillRect({mx - 6.f, my - 7.f, 4.f, 14.f}, pinc);
            r.fillRect({mx + 2.f, my - 7.f, 4.f, 14.f}, pinc);
        } else {
            ui::shapes::fillPolygon(r, {{mx - 5.f, my - 8.f}, {mx - 5.f, my + 8.f}, {mx + 8.f, my}}, pinc);
        }
    }
    // Arret : un carre.
    pill(r, stopBtn_, btn, c.border);
    r.fillRect({stopBtn_.x + stopBtn_.w / 2.f - 6.f, stopBtn_.y + stopBtn_.h / 2.f - 6.f, 12.f, 12.f}, ink);
    // Les vitesses.
    for (int k = 0; k < 3; ++k) {
        const bool on = std::fabs(stage_->speed() - kSpeeds[k]) < 1e-6;
        pill(r, speeds_[k], on ? c.accent.withAlpha(90) : btn, on ? c.accent : c.border);
        const std::string label = k == 0 ? std::string("\xC3\x97" "0,5") : k == 1 ? std::string("\xC3\x97" "1") : std::string("\xC3\x97" "2");
        const float lw = r.measure(label, f.smallUi).width;
        r.drawText({speeds_[k].x + (speeds_[k].w - lw) / 2.f, speeds_[k].y + (speeds_[k].h - r.lineHeight(f.smallUi)) / 2.f}, label, f.smallUi,
                   on ? c.text : c.textMuted);
    }
    // La frise : le tour, les chapitres, la position.
    r.fillRoundedRect(frise_, c.border, 3.f);
    const float px = xAtTime(stage_->position());
    r.fillRoundedRect({frise_.x, frise_.y, std::max(0.f, px - frise_.x), frise_.h}, c.accent, 3.f);
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        const float cx = xAtTime(chapters_[i].at);
        const gfx::Color col = chapters_[i].yourTurn ? c.ok : chapters_[i].at <= stage_->position() + 1e-6 ? c.accent : c.textMuted;
        r.fillRect({cx - 1.f, frise_.y - 7.f, 2.f, frise_.h + 14.f}, col);
        const std::string n = chapters_[i].yourTurn ? std::string("\xE2\x98\x85") : std::to_string(i + 1);
        r.drawText({cx - r.measure(n, f.smallUi).width / 2.f, frise_.y - 9.f - r.lineHeight(f.smallUi)}, n, f.smallUi, col);
    }
    ui::shapes::fillPolygon(r, ui::shapes::ellipse({px, frise_.y + frise_.h / 2.f}, 8.f, 8.f, 20), stage_->interactive() ? c.ok : c.accent);
    ui::shapes::strokePolyline(r, ui::shapes::ellipse({px, frise_.y + frise_.h / 2.f}, 8.f, 8.f, 20), true, gfx::Color::rgb(0xFFFFFF), 1.5f);
    // Le temps.
    const std::string time = stage_->interactive() ? std::string("\xC3\xA0 toi \xC2\xB7 ") + seconds(stage_->position())
                                                   : seconds(stage_->position()) + " / " + seconds(stage_->period());
    r.drawText({yourTurn_.x - 8.f - r.measure(time, f.smallUi).width, bar_.y + (kBarH - r.lineHeight(f.smallUi)) / 2.f}, time, f.smallUi,
               c.textMuted);
    // A toi / Guide.
    const bool mine = stage_->interactive();
    pill(r, yourTurn_, mine ? btn : c.ok, mine ? c.border : gfx::Color{0, 0, 0, 0});
    const std::string turn = mine ? std::string("Tutoriel guid\xC3\xA9") : std::string("\xC3\x80 toi \xE2\x96\xB8");
    const float tw = r.measure(turn, f.uiBold).width;
    r.drawText({yourTurn_.x + (yourTurn_.w - tw) / 2.f, yourTurn_.y + (yourTurn_.h - r.lineHeight(f.uiBold)) / 2.f}, turn, f.uiBold,
               mine ? c.text : gfx::Color::rgb(0xFFFFFF));
    invalidate();   // la position avance a chaque image
}

void HmiObjectTutorial::onPaintOverlay(const ui::PaintContext& ctx) {
    bubble_ = {};
    if (!hasTutorial()) return;
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    auto& r = ctx.r;
    const std::size_t cur = currentChapter();
    if (cur >= chapters_.size()) return;
    const auto& ch = chapters_[cur];
    const gfx::Rect frame = stage_->exampleRect();
    const gfx::Rect scene = stage_->canvas().bounds();
    if (frame.w <= 1.f || scene.w <= 1.f) return;
    gfx::Rect target{};
    // "#clavier" : le clavier virtuel ouvert ; sinon un objet de la vue.
    bool pointed = false;
    if (ch.target == "#clavier") {
        target = stage_->canvas().keyboardRect();
        pointed = target.w > 0 && target.h > 0;
    } else {
        pointed = !ch.target.empty() && stage_->canvas().objectRect(ch.target, target) && target.w > 0;
    }
    // Le contour de l'objet dont parle la bulle : il respire.
    if (pointed) {
        const float pulse = 0.55f + 0.45f * static_cast<float>(std::sin(ctx.time * 5.0));
        const gfx::Rect ring{target.x - 5.f, target.y - 5.f, target.w + 10.f, target.h + 10.f};
        const gfx::Color col = (ch.yourTurn ? c.ok : c.accent).withAlpha(static_cast<std::uint8_t>(110 + 120 * pulse));
        r.strokeRect(ring, col, 2.f);
    }
    // La bulle : titre, texte, chapitre.
    const float maxW = std::clamp(scene.w * 0.42f, 220.f, 380.f);
    const float pad = 12.f;
    const auto lines = wrap(r, ch.text, f.ui, maxW - 2 * pad);
    const float lh = r.lineHeight(f.ui);
    const std::string foot = "chapitre " + std::to_string(cur + 1) + " / " + std::to_string(chapters_.size())
                           + (ch.yourTurn ? std::string(" \xC2\xB7 le moteur r\xC3\xA9pond \xC3\xA0 tes clics") : " \xC2\xB7 " + seconds(ch.at));
    float textW = r.measure(ch.title, f.uiBold).width;
    for (const auto& l : lines) textW = std::max(textW, r.measure(l, f.ui).width);
    textW = std::max(textW, r.measure(foot, f.smallUi).width);
    const float bw = std::min(maxW, textW + 2 * pad);
    const float bh = pad + r.lineHeight(f.uiBold) + 6.f + lh * static_cast<float>(lines.size()) + 8.f + r.lineHeight(f.smallUi) + pad;
    float bx = scene.x + 16.f, by = scene.y + 16.f;
    int side = 0;                                    // 0 : sans fleche ; 1 : a droite de l'objet ; 2 : a gauche ; 3 : dessous ; 4 : dessus
    if (pointed) {
        const float gap = 18.f;
        if (target.right() + gap + bw <= scene.right() - 6.f) {
            bx = target.right() + gap;
            side = 1;
        } else if (target.x - gap - bw >= scene.x + 6.f) {
            bx = target.x - gap - bw;
            side = 2;
        } else if (target.bottom() + gap + bh <= scene.bottom() - 6.f) {
            by = target.bottom() + gap;
            bx = target.x + target.w / 2.f - bw / 2.f;
            side = 3;
        } else {
            by = target.y - gap - bh;
            bx = target.x + target.w / 2.f - bw / 2.f;
            side = 4;
        }
        if (side == 1 || side == 2) by = target.y + target.h / 2.f - bh / 2.f;
        bx = std::clamp(bx, scene.x + 6.f, std::max(scene.x + 6.f, scene.right() - bw - 6.f));
        by = std::clamp(by, scene.y + 6.f, std::max(scene.y + 6.f, scene.bottom() - bh - 6.f));
    }
    const gfx::Rect box{bx, by, bw, bh};
    bubble_ = box;
    const gfx::Color back{24, 29, 37, 244};
    const gfx::Color edge = ch.yourTurn ? c.ok : c.accent;
    r.fillRect({box.x + 3.f, box.y + 4.f, box.w, box.h}, gfx::Color{0, 0, 0, 90});
    r.fillRoundedRect(box, back, 8.f);
    r.strokeRect(box, edge, 1.5f);
    // La fleche vers l'objet.
    if (side != 0) {
        const float tcx = target.x + target.w / 2.f, tcy = target.y + target.h / 2.f;
        std::vector<gfx::Point> tri;
        if (side == 1) {
            const float y = std::clamp(tcy, box.y + 12.f, box.bottom() - 12.f);
            tri = {{box.x, y - 8.f}, {box.x, y + 8.f}, {box.x - 12.f, y}};
        } else if (side == 2) {
            const float y = std::clamp(tcy, box.y + 12.f, box.bottom() - 12.f);
            tri = {{box.right(), y - 8.f}, {box.right(), y + 8.f}, {box.right() + 12.f, y}};
        } else if (side == 3) {
            const float x = std::clamp(tcx, box.x + 12.f, box.right() - 12.f);
            tri = {{x - 8.f, box.y}, {x + 8.f, box.y}, {x, box.y - 12.f}};
        } else {
            const float x = std::clamp(tcx, box.x + 12.f, box.right() - 12.f);
            tri = {{x - 8.f, box.bottom()}, {x + 8.f, box.bottom()}, {x, box.bottom() + 12.f}};
        }
        ui::shapes::fillPolygon(r, tri, edge);
    }
    float y = box.y + pad;
    r.drawText({box.x + pad, y}, ch.title, f.uiBold, edge);
    y += r.lineHeight(f.uiBold) + 6.f;
    for (const auto& l : lines) {
        r.drawText({box.x + pad, y}, l, f.ui, gfx::Color::rgb(0xE6EAF0));
        y += lh;
    }
    y += 8.f;
    r.drawText({box.x + pad, y}, foot, f.smallUi, gfx::Color::rgb(0x9AA6B8));
}

ui::EventResult HmiObjectTutorial::onEvent(const ui::InputEvent& ev) {
    using ui::EventResult;
    if (!hasTutorial()) return EventResult::Ignored;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const gfx::Point p = d->pos;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (inside(rows_[i], p) && rows_[i].bottom() <= list_.bottom()) {
                goToChapter(i);
                return EventResult::Consumed;
            }
        if (inside(start_, p)) { stop(); return EventResult::Consumed; }
        if (inside(playBtn_, p)) {
            if (stage_->interactive()) guided();
            togglePlay();
            return EventResult::Consumed;
        }
        if (inside(stopBtn_, p)) { stop(); return EventResult::Consumed; }
        for (int k = 0; k < 3; ++k)
            if (inside(speeds_[k], p)) { setSpeed(kSpeeds[k]); return EventResult::Consumed; }
        if (inside(yourTurn_, p)) {
            if (stage_->interactive()) guided();
            else yourTurn();
            return EventResult::Consumed;
        }
        const gfx::Rect grab{frise_.x - 10.f, bar_.y, frise_.w + 20.f, bar_.h};
        if (inside(grab, p)) {
            dragging_ = true;
            seek(timeAtX(p.x));
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (dragging_) {
            seek(timeAtX(m->pos.x));
            return EventResult::Consumed;
        }
        int h = -1;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (inside(rows_[i], m->pos)) h = static_cast<int>(i);
        if (h != hoverRow_) {
            hoverRow_ = h;
            invalidate();
        }
        return EventResult::Ignored;
    }
    if (std::get_if<ui::MouseUp>(&ev) && dragging_) {
        dragging_ = false;
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !stage_->interactive()) {
        if (k->key == ui::Key::Space) { togglePlay(); return EventResult::Consumed; }
        const std::size_t cur = currentChapter();
        if (k->key == ui::Key::Right && cur + 1 < chapters_.size()) { goToChapter(cur + 1); return EventResult::Consumed; }
        if (k->key == ui::Key::Left) { goToChapter(cur > 0 ? cur - 1 : 0); return EventResult::Consumed; }
        if (k->key == ui::Key::Home) { stop(); return EventResult::Consumed; }
    }
    return EventResult::Ignored;
}

} // namespace app
