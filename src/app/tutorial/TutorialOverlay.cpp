#include "TutorialOverlay.hpp"

#include "../../help/TutorialKeys.hpp"
#include "../../platform/Renderer.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace app {
namespace {

bool inside(const gfx::Rect& r, gfx::Point p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

const gfx::Color kOrange = gfx::Color::rgb(0xF39C12);

// Tranche 9 : le **gras** de la bulle. Il n'y a pas de face grasse (ui/Theme.hpp) : le
// texte gras est trace deux fois, a un pixel, comme dans les autres widgets.
std::string withoutBold(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') { ++i; continue; }
        out += s[i];
    }
    return out;
}

void drawMarked(gfx::IRenderer& r, gfx::Point at, const std::string& marked, gfx::FontId font, gfx::Color ink) {
    float x = at.x;
    bool bold = false;
    std::size_t i = 0;
    while (i <= marked.size()) {
        const auto j = marked.find("**", i);
        const std::string run = marked.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!run.empty()) {
            r.drawText({x, at.y}, run, font, ink);
            if (bold) r.drawText({x + 1.f, at.y}, run, font, ink);
            x += r.measure(run, font).width + (bold ? 1.f : 0.f);
        }
        if (j == std::string::npos) break;
        bold = !bold;
        i = j + 2;
    }
}

// Tranche 11 : la bulle passe a la ligne. Elle tenait sur une seule ligne, aussi large que
// l'ecran : les textes deduits (T3) etaient coupes par « … » pour y tenir. Les mots vont a la
// ligne au-dela de `limit` pixels ; un **gras** ouvert en fin de ligne y est referme, puis
// rouvert au debut de la suivante (chaque ligne se dessine seule, par drawMarked).
float bubbleTextLimit(const gfx::Rect& screen) { return std::max(std::min(screen.w - 64.f, 820.f), 120.f) - 32.f; }

std::vector<std::string> wrapMarked(gfx::IRenderer& r, const std::string& marked, gfx::FontId font, float limit) {
    // 1.11.2 (R1112-4) : un « \n » commence une ligne (le mot du lecteur, au-dessus de la bulle).
    if (const auto nl = marked.find('\n'); nl != std::string::npos) {
        auto out = wrapMarked(r, marked.substr(0, nl), font, limit);
        const auto rest = wrapMarked(r, marked.substr(nl + 1), font, limit);
        out.insert(out.end(), rest.begin(), rest.end());
        return out;
    }
    auto openBold = [](const std::string& s) {
        int n = 0;
        for (auto p = s.find("**"); p != std::string::npos; p = s.find("**", p + 2)) ++n;
        return n % 2 == 1;
    };
    // Les espaces sont gardes tels quels (les exemples alignes : « 42   -7   16#FF ») ; on ne
    // passe a la ligne que devant un mot.
    std::vector<std::string> words;
    for (std::size_t i = 0;;) {
        const auto j = marked.find(' ', i);
        words.push_back(marked.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    std::vector<std::string> lines;
    std::string line = words.front();
    for (std::size_t k = 1; k < words.size(); ++k) {
        const auto& word = words[k];
        std::string candidate = line + " " + word;
        if (!word.empty() && !line.empty() && r.measure(withoutBold(candidate), font).width > limit) {
            const bool open = openBold(line);
            lines.push_back(open ? line + "**" : line);
            candidate = open ? "**" + word : word;
        }
        line = std::move(candidate);
    }
    lines.push_back(line);
    return lines;
}

// Tranche 11 : « 3 / 7  <titre de l'etape> » dans sa case de la barre, coupe par « … » si la
// place manque. Il debordait sur l'horloge (script-rampe, etape 7, a 1920 px : la case fait
// 280 px), et les titres des tutoriels deduits (T3) sont souvent plus longs.
std::string fitLabel(gfx::IRenderer& r, std::string text, gfx::FontId font, float room) {
    if (r.measure(text, font).width <= room) return text;
    const std::string dots = "\xE2\x80\xA6";
    while (!text.empty() && r.measure(text + dots, font).width > room) {
        // un caractere UTF-8 entier : ses octets de suite, puis son octet de tete
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text + dots;
}

} // namespace

// 1.11.1 (T1, R111-18) : une partie de la barre par son nom (clic-souris lecteur:<nom>).
bool tutorialBarPart(const TutorialBarLayout& l, std::string_view name, gfx::Rect& out) {
    const std::pair<std::string_view, const gfx::Rect*> parts[] = {
        {"lecture", &l.play},         {"precedente", &l.previous}, {"suivante", &l.next},
        {"recommencer", &l.restart},  {"atoi", &l.aTry},           {"quitter", &l.quit},
        {"pause-etape", &l.pauseEach}, {"horloge", &l.clock},      {"piste", &l.track},
        {"vitesse-1", &l.speeds[0]},  {"vitesse-2", &l.speeds[1]}, {"vitesse-3", &l.speeds[2]},
    };
    const gfx::Rect* r = nullptr;
    for (const auto& [n, p] : parts)
        if (n == name) r = p;
    if (!r && name.rfind("etape-", 0) == 0) {
        std::size_t n = 0;
        for (const char c : name.substr(6)) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + static_cast<std::size_t>(c - '0');
        }
        if (n >= 1 && n <= l.markers.size()) r = &l.markers[n - 1];
    }
    if (!r || r->w <= 0.f || r->h <= 0.f) return false;
    out = *r;
    return true;
}

TutorialBarLayout layoutTutorialBar(const gfx::Rect& screen, const help::CompiledTutorial& tutorial, float height) {
    TutorialBarLayout l;
    l.bar = {screen.x, screen.y + screen.h - height, screen.w, height};
    const float y = l.bar.y + 6.f, h = height - 12.f;
    float x = screen.x + 8.f;
    auto take = [&](float w) {
        const gfx::Rect r{x, y, w, h};
        x += w + 6.f;
        return r;
    };
    l.restart = take(104.f);
    l.previous = take(36.f);
    l.play = take(72.f);
    l.next = take(36.f);
    l.stepLabel = take(std::clamp(screen.w * 0.22f, 120.f, 280.f));
    l.clock = take(88.f);
    // A droite, de la fin vers le debut : Quitter, A toi, pause apres chaque etape, les vitesses.
    float right = screen.x + screen.w - 8.f;
    auto takeRight = [&](float w) {
        right -= w;
        const gfx::Rect r{right, y, w, h};
        right -= 6.f;
        return r;
    };
    l.quit = takeRight(76.f);
    l.aTry = takeRight(64.f);
    l.pauseEach = takeRight(190.f);
    for (int i = 2; i >= 0; --i) l.speeds[static_cast<std::size_t>(i)] = takeRight(44.f);
    l.track = {x + 4.f, y + h * 0.5f - 3.f, std::max(right - x - 8.f, 40.f), 6.f};
    const double total = std::max(tutorial.totalMs, 1);
    for (const auto& s : tutorial.steps) {
        const float mx = l.track.x + static_cast<float>(s.startMs / total) * l.track.w;
        l.markers.push_back({mx - 5.f, l.track.y - 6.f, 10.f, l.track.h + 12.f});
    }
    return l;
}

TutorialOverlay::TutorialOverlay(help::TutorialPlayer& player, TutorialStageApp& stage)
    : ui::Widget("tutoriel.calque"), player_(player), stage_(stage) {}

void TutorialOverlay::frame(double dtMs) {
    // Tranche 12 (vu par I111) : le lecteur avance d'abord, puis la scene joue ce qu'il
    // vient de mettre en file. Dans l'ordre inverse, la tranche du geste en cours restait
    // en file jusqu'au dessin : "Remise en place..." remplacait la bulle pendant la
    // lecture, et le curseur avait une image de retard. (tick ne fait rien en A toi.)
    player_.tick(dtMs);
    stage_.frame();
    // A toi : la verification suit ce que fait l'utilisateur. Juste, le lecteur
    // passe en fin d'etape (la bulle verte) : on ne reverifie plus.
    if (player_.state() == help::PlayerState::ATry && !stage_.busy() && ++sinceVerify_ >= 10) {
        sinceVerify_ = 0;
        (void)player_.verify();
    }
    invalidate();
}

bool TutorialOverlay::overlayCovers(gfx::Point p) const {
    // A toi : l'appli repond a l'utilisateur, sauf sur la barre.
    if (player_.state() == help::PlayerState::ATry) {
        for (const auto& [rect, v] : chips_)
            if (inside(rect, p)) return true;
        return inside(barLayout().bar, p);
    }
    return true;
}

bool TutorialOverlay::clickBar(gfx::Point p) {
    // Tranche 9 : une autre variante recompile et reprend au debut, sur une copie neuve.
    for (const auto& [rect, v] : chips_)
        if (inside(rect, p)) {
            if (v != player_.compiled().variant) {
                const std::string chosen = v;   // chips_ est refait au prochain dessin
                player_.setVariant(chosen, false);
            }
            invalidate();
            return true;
        }
    const auto l = barLayout();
    if (!inside(l.bar, p)) return false;
    if (inside(l.restart, p)) player_.restart();
    else if (inside(l.previous, p)) player_.previous();
    else if (inside(l.play, p)) player_.togglePlay();
    else if (inside(l.next, p)) player_.next();
    else if (inside(l.pauseEach, p)) player_.setPauseAfterEachStep(!player_.pauseAfterEachStep());
    else if (inside(l.aTry, p)) (void)player_.startATry();
    else if (inside(l.quit, p)) { if (onQuit) onQuit(); }
    else {
        bool done = false;
        for (std::size_t i = 0; i < l.speeds.size() && !done; ++i)
            if (inside(l.speeds[i], p)) { player_.setSpeed(kTutorialSpeeds[i]); done = true; }
        for (std::size_t i = 0; i < l.markers.size() && !done; ++i)
            if (inside(l.markers[i], p)) { player_.seek(i, 0); done = true; }
        if (!done && p.x >= l.track.x && p.x <= l.track.x + l.track.w) {
            const double f = (p.x - l.track.x) / std::max(l.track.w, 1.f);
            player_.seekTime(f * player_.compiled().totalMs);
        }
    }
    invalidate();
    return true;
}

ui::EventResult TutorialOverlay::onEvent(const ui::InputEvent& e) {
    const bool aTry = player_.state() == help::PlayerState::ATry;
    if (const auto* k = std::get_if<ui::KeyDown>(&e)) {
        if (k->key == ui::Key::Escape) {
            if (aTry) player_.leaveATry();
            else if (onQuit) onQuit();
            return ui::EventResult::Consumed;
        }
        if (aTry) return ui::EventResult::Ignored;          // l'utilisateur tape dans l'appli
        if (k->key == ui::Key::Space) player_.togglePlay();
        else if (k->key == ui::Key::Left) player_.previous();
        else if (k->key == ui::Key::Right) player_.next();
        else if (k->key == ui::Key::A) (void)player_.startATry();
        invalidate();
        return ui::EventResult::Consumed;                   // pendant la lecture, le clavier est au tutoriel
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&e)) {
        if (clickBar(d->pos)) return ui::EventResult::Consumed;
        return aTry ? ui::EventResult::Ignored : ui::EventResult::Consumed;
    }
    if (aTry) return ui::EventResult::Ignored;
    // Pendant la lecture, la souris et le texte sont au tutoriel (sauf le survol de la barre).
    if (std::holds_alternative<ui::MouseUp>(e) || std::holds_alternative<ui::TextInput>(e)
        || std::holds_alternative<ui::MouseWheel>(e) || std::holds_alternative<ui::KeyUp>(e))
        return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}

gfx::Rect TutorialOverlay::bubbleRect(const ui::PaintContext& ctx, const std::string& text) const {
    // `text` garde ses marques de **gras** ; la bulle a autant de lignes que wrapMarked en fait.
    const auto lines = wrapMarked(ctx.r, text, ctx.theme.font.ui, bubbleTextLimit(screen_));
    float w = 0.f;
    for (const auto& l : lines) w = std::max(w, ctx.r.measure(withoutBold(l), ctx.theme.font.ui).width);
    w = std::min(w + 32.f, std::max(screen_.w - 64.f, 120.f));
    const float n = static_cast<float>(lines.size());
    const float h = ctx.r.lineHeight(ctx.theme.font.ui) * n + 4.f * (n - 1.f) + 22.f;
    const auto bar = barLayout().bar;
    return {screen_.x + (screen_.w - w) * 0.5f, bar.y - h - 16.f, w, h};
}

void TutorialOverlay::onPaintOverlay(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    const bool aTry = player_.state() == help::PlayerState::ATry;
    // Un cadre en tirets (la maquette : la consigne d'A toi, la pastille du bac a sable).
    auto dashed = [&](const gfx::Rect& b, gfx::Color col, float width) {
        constexpr float kDash = 7.f, kGap = 4.f;
        auto run = [&](gfx::Point a, gfx::Point z) {
            const float len = std::hypot(z.x - a.x, z.y - a.y);
            if (len <= 0.f) return;
            const float ux = (z.x - a.x) / len, uy = (z.y - a.y) / len;
            for (float t = 0.f; t < len; t += kDash + kGap) {
                const float e = std::min(t + kDash, len);
                r.line({a.x + ux * t, a.y + uy * t}, {a.x + ux * e, a.y + uy * e}, col, width);
            }
        };
        run({b.x, b.y}, {b.x + b.w, b.y});
        run({b.x + b.w, b.y}, {b.x + b.w, b.y + b.h});
        run({b.x + b.w, b.y + b.h}, {b.x, b.y + b.h});
        run({b.x, b.y + b.h}, {b.x, b.y});
    };

    // Le voile et l'encadre (encadrer) ; en A toi, la cible reste encadree sans voile.
    const gfx::Color veil{0, 0, 0, 90};
    bool veiled = false;   // tranche 9 : le fond de la pastille du bac a sable le reprend
    if (const auto& spot = stage_.spot()) {
        // Au pixel pres : des bords fractionnaires laissent une ligne claire entre
        // deux morceaux du voile (vu sur la session 82).
        const float x0 = std::floor(spot->x - 4.f), y0 = std::floor(spot->y - 4.f);
        const gfx::Rect s{x0, y0, std::ceil(spot->x + spot->w + 4.f) - x0, std::ceil(spot->y + spot->h + 4.f) - y0};
        if (!aTry) {
            veiled = true;
            const auto& sc = screen_;
            r.fillRect({sc.x, sc.y, sc.w, std::max(s.y - sc.y, 0.f)}, veil);
            r.fillRect({sc.x, s.y + s.h, sc.w, std::max(sc.y + sc.h - s.y - s.h, 0.f)}, veil);
            r.fillRect({sc.x, s.y, std::max(s.x - sc.x, 0.f), s.h}, veil);
            r.fillRect({s.x + s.w, s.y, std::max(sc.x + sc.w - s.x - s.w, 0.f), s.h}, veil);
        }
        r.strokeRect(s, kOrange, 3.f);
    }

    // Le curseur anime et le cercle du clic.
    // (pas avant le premier geste : il serait dans le coin, sur le bouton de l'appli)
    if (!aTry && (stage_.cursor().x > 0.f || stage_.cursor().y > 0.f)) {
        const auto p = stage_.cursor();
        const gfx::Color ink{20, 20, 20, 255};
        r.line(p, {p.x, p.y + 18.f}, ink, 2.f);
        r.line(p, {p.x + 12.f, p.y + 12.f}, ink, 2.f);
        r.line({p.x, p.y + 18.f}, {p.x + 12.f, p.y + 12.f}, ink, 2.f);
        if (stage_.clickFlash() > 0) {
            const float rad = 6.f + (12 - stage_.clickFlash()) * 1.5f;
            r.strokeRect({p.x - rad, p.y - rad, rad * 2.f, rad * 2.f}, kOrange, 2.f);
        }
    }

    // La bulle : celle de l'etape, ou la reponse d'A toi.
    std::string text = stage_.say();
    gfx::Color edge = kOrange;
    bool instruction = false;                   // la consigne d'A toi : en tirets, comme la maquette
    if (aTry) {
        const auto& out = player_.lastOutcome();
        const auto& step = player_.compiled().steps[player_.step()];
        if (out.result == help::CheckOutcome::Result::Ok) { text = out.message; edge = c.ok; }
        else if (out.result == help::CheckOutcome::Result::Almost) { text = out.message; edge = c.warning; }
        else if (step.aTry) { text = step.aTry->instruction; instruction = true; }
    }
    if (player_.state() == help::PlayerState::StepEnd && player_.lastOutcome().result == help::CheckOutcome::Result::Ok) {
        text = player_.lastOutcome().message;   // le bravo, apres un A toi reussi
        edge = c.ok;
    }
    // 1.11.2 (R1112-4) : le mot du lecteur (« Essayer » commence a l'etape 3 ; pas d'« A toi » dans ce
    // tutoriel), au-dessus de la bulle, tant que l'etape d'ouverture dure.
    if (const auto n = player_.notice(); !n.empty() && player_.state() != help::PlayerState::StepEnd)
        text = text.empty() ? n : n + "\n" + text;
    if (stage_.catchingUp()) text = "Remise en place\xE2\x80\xA6";
    if (!text.empty()) {
        const auto b = bubbleRect(ctx, text);
        r.fillRoundedRect(b, c.panelBg, 8.f);
        if (instruction) dashed(b, kOrange, 2.f);
        else r.strokeRect(b, edge, aTry ? 1.f : 2.f);
        const auto lines = wrapMarked(r, text, f.ui, bubbleTextLimit(screen_));
        const float step = r.lineHeight(f.ui) + 4.f;
        for (std::size_t i = 0; i < lines.size(); ++i)
            drawMarked(r, {b.x + 16.f, b.y + 11.f + step * static_cast<float>(i)}, lines[i], f.ui, c.text);
    }
    if (!stage_.keyShown().empty() && !aTry) {
        // Tranche 13 : le nom francais de l'appli ("Entree", "Echap", "Maj+Fin", les fleches),
        // pas celui du .tuto ("Return", les noms de ScriptRunner).
        const std::string key = help::keyLabel(stage_.keyShown());
        const auto m = r.measure(key, f.uiBold);
        const auto b = bubbleRect(ctx, text.empty() ? std::string("x") : text);
        const gfx::Rect cap{b.x, b.y - 40.f, m.width + 24.f, 32.f};
        r.fillRoundedRect(cap, c.headerBg, 6.f);
        r.strokeRect(cap, c.borderStrong, 1.f);
        r.drawText({cap.x + 12.f, cap.y + 7.f}, key, f.uiBold, c.text);
    }

    // La barre de commande.
    const auto l = barLayout();
    r.fillRect(l.bar, c.headerBg);
    r.line({l.bar.x, l.bar.y}, {l.bar.x + l.bar.w, l.bar.y}, c.border, 1.f);
    auto button = [&](const gfx::Rect& b, const std::string& label, bool on) {
        r.fillRoundedRect(b, on ? c.accent : c.panelBg, 5.f);
        r.strokeRect(b, c.border, 1.f);
        const auto m = r.measure(label, f.smallUi);
        r.drawText({b.x + (b.w - m.width) * 0.5f, b.y + (b.h - m.height) * 0.5f}, label, f.smallUi,
                   on ? c.textInverted : c.text);
    };
    const bool playing = player_.state() == help::PlayerState::Playing;
    button(l.restart, "Recommencer", false);
    button(l.previous, "<<", false);
    button(l.play, playing ? "Pause" : "Lecture", playing);
    button(l.next, ">>", false);
    const auto& steps = player_.compiled().steps;
    const std::string stepText = player_.progressLabel()
                                 + (steps.empty() ? std::string() : "  " + steps[player_.step()].title);
    r.drawText({l.stepLabel.x, l.stepLabel.y + 6.f}, fitLabel(r, stepText, f.smallUi, l.stepLabel.w - 4.f), f.smallUi, c.text);
    r.drawText({l.clock.x, l.clock.y + 6.f}, player_.clockLabel(), f.smallUi, c.textMuted);
    r.fillRoundedRect(l.track, c.border, 3.f);
    const double total = std::max(player_.compiled().totalMs, 1);
    r.fillRoundedRect({l.track.x, l.track.y, static_cast<float>(player_.timeMs() / total) * l.track.w, l.track.h},
                      kOrange, 3.f);
    for (std::size_t i = 0; i < l.markers.size() && i < steps.size(); ++i) {
        const auto& m = l.markers[i];
        r.fillRoundedRect({m.x + 2.f, m.y + 4.f, m.w - 4.f, m.h - 8.f}, steps[i].aTry ? c.ok : c.textMuted, 2.f);
    }
    static const char* kSpeedLabels[] = {"0,5\xC3\x97", "1\xC3\x97", "2\xC3\x97"};
    for (std::size_t i = 0; i < l.speeds.size(); ++i)
        button(l.speeds[i], kSpeedLabels[i], std::abs(player_.speed() - kTutorialSpeeds[i]) < 1e-6);
    button(l.pauseEach, std::string(player_.pauseAfterEachStep() ? "[x] " : "[ ] ") + "pause apr\xC3\xA8s chaque \xC3\xA9tape",
           false);
    button(l.aTry, "\xC3\x80 toi", aTry);
    button(l.quit, "Quitter", false);

    // 1.11.2 (T1, decision 155 : « le truc bac a sable en haut faut l'enlever ») : plus de pastille
    // « BAC A SABLE · copie d'Armoire_Gaz » dans le bandeau du haut ; elle chevauchait le haut des pages
    // (dans le centre d'aide : « Tous les sujets ont leur tutoriel »). Restent, pour un objet a variantes
    // (tranche 9, CONCEPTION-T1 section 2.3), les pastilles des variantes : a droite d'Aller a, sinon (une
    // page sans bandeau du haut) a droite du bandeau. La variante jouee est pleine ; un clic recompile.
    chips_.clear();
    const auto& variants = player_.compiled().variants;
    if (variants.size() > 1) {
        constexpr float kBand = 44.f, kH = 24.f;   // TopBar::kBarHeight
        const bool onGoTo = goTo_.w > 0.f && goTo_.h > 0.f;
        float chipsW = 0.f;
        for (const auto& v : variants) chipsW += std::ceil(r.measure(v, f.smallUi).width) + 20.f;
        const float top = std::floor(onGoTo ? goTo_.y + (goTo_.h - kH) * 0.5f : screen_.y + (kBand - kH) * 0.5f);
        const float y0 = std::min(screen_.y + 2.f, onGoTo ? goTo_.y - 1.f : screen_.y + 2.f);
        const float y1 = std::max(screen_.y + kBand - 2.f, onGoTo ? goTo_.bottom() + 1.f : 0.f);
        float x = onGoTo ? std::ceil(goTo_.right()) + 12.f : std::floor(screen_.x + screen_.w - 16.f - chipsW + 4.f);
        float right = x;
        for (const auto& v : variants) {
            const auto m = r.measure(v, f.smallUi);
            chips_.emplace_back(gfx::Rect{x, top, std::ceil(m.width) + 16.f, kH}, v);
            x += std::ceil(m.width) + 20.f;
            right = x;
        }
        // Tranche 10 : le fond s'etend jusqu'au bout de chaque element du bandeau qu'il touche
        // (session 84 : il restait un bout de la pastille "IHM arretee" apres "reglante").
        float left = chips_.front().first.x - 6.f;
        for (bool grown = true; grown;) {
            grown = false;
            for (const auto& p : barParts_) {
                if (p.w <= 0.f || p.x >= right || p.right() <= left) continue;
                if (p.right() + 4.f > right) { right = p.right() + 4.f; grown = true; }
                if (p.x - 4.f < left) { left = p.x - 4.f; grown = true; }
            }
        }
        const gfx::Rect under{left, y0, right - left, y1 - y0};
        r.fillRect(under, c.headerBg);
        if (veiled) r.fillRect(under, veil);
        for (const auto& [rect, v] : chips_) {
            const bool on = v == player_.compiled().variant;
            r.fillRoundedRect(rect, on ? kOrange : c.panelBg, 5.f);
            r.strokeRect(rect, on ? kOrange : c.border, 1.f);
            const auto m = r.measure(v, f.smallUi);
            r.drawText({rect.x + (rect.w - m.width) * 0.5f, rect.y + (kH - m.height) * 0.5f}, v, f.smallUi,
                       on ? c.textInverted : c.text);
        }
    }
}

} // namespace app
