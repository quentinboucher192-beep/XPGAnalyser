#include "HmiLot13Painter.hpp"

#include "HmiIcons.hpp"
#include "HmiPaintKit.hpp"
#include "../../hmi/HmiDisplay.hpp"
#include "../../hmi/HmiLanguages.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {

namespace {

namespace shapes = ui::shapes;

const gfx::Color kText = gfx::Color::rgb(0xE6EAF0), kMuted = gfx::Color::rgb(0x9AA6B8), kDim = gfx::Color::rgb(0x6B7686);
const gfx::Color kLabel = gfx::Color::rgb(0xC8D0DC);
const gfx::Color kField = gfx::Color::rgb(0x141820), kButton = gfx::Color::rgb(0x2A313C), kButtonEdge = gfx::Color::rgb(0x3A4556);
const gfx::Color kOrange = gfx::Color::rgb(0xF2994A), kRed = gfx::Color::rgb(0xE5534B), kGreen = gfx::Color::rgb(0x2ECC71);
const gfx::Color kViolet = gfx::Color::rgb(0xB39DDB);

gfx::FontId fontOf(double px) { return gfx::FontId{static_cast<std::uint16_t>(std::clamp(px, 8.0, 40.0))}; }
gfx::Color faded(gfx::Color c, bool on) { return on ? c : c.withAlpha(static_cast<std::uint8_t>(c.a * 0.42f)); }

std::size_t codepoints(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

struct Painter {
    gfx::IRenderer& r;
    const ui::Theme& th;
    float ox, oy;

    [[nodiscard]] gfx::Rect at(const hmi::Box& b) const {
        return {ox + static_cast<float>(b.x), oy + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    }
    float text(const gfx::Rect& box, std::string_view s, gfx::FontId f, gfx::Color c, int align = 0) const {
        if (box.w <= 2.f || s.empty()) return 0.f;
        const auto n = r.fitCharacters(s, f, box.w);
        const std::string_view shown = s.substr(0, n);
        const float w = r.measure(shown, f).width;
        const float x = align == 0 ? box.x : align == 1 ? box.x + (box.w - w) / 2.f : box.right() - w;
        r.drawText({x, box.y + (box.h - r.lineHeight(f)) / 2.f}, shown, f, c);
        return w;
    }
    void button(const gfx::Rect& b, gfx::Color fill, bool on) const {
        r.fillRoundedRect(b, faded(fill, on), std::min(5.f, b.h / 3.f));
        r.strokeRect(b, faded(kButtonEdge, on), 1.f);
    }
    void arrow(const gfx::Rect& b, bool right, gfx::Color c) const {
        const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.24f;
        std::vector<gfx::Point> p = right ? std::vector<gfx::Point>{{cx - s * 0.5f, cy - s}, {cx - s * 0.5f, cy + s}, {cx + s * 0.6f, cy}}
                                          : std::vector<gfx::Point>{{cx + s * 0.5f, cy - s}, {cx + s * 0.5f, cy + s}, {cx - s * 0.6f, cy}};
        shapes::fillPolygon(r, p, c);
    }
    // Un champ : masque (des points) ou en clair ; le curseur quand il a le focus.
    void field(const gfx::Rect& box, const std::string& typed, std::size_t caret, bool focused, bool masked, std::string_view placeholder,
               gfx::FontId f) const {
        r.fillRoundedRect(box, kField, 3.f);
        r.strokeRect(box, focused ? th.color.accent : kButtonEdge, focused ? 1.8f : 1.f);
        const gfx::Rect in = box.inset(10.f, 0.f);
        std::string shown;
        if (masked) for (std::size_t i = 0; i < codepoints(typed); ++i) shown += "\xE2\x80\xA2";
        else shown = typed;
        if (shown.empty() && !focused) {
            text(in, placeholder, f, kDim);
            return;
        }
        const float w = text(in, shown, f, kText);
        if (!focused) return;
        std::size_t before = 0;
        for (std::size_t i = 0; i < std::min(caret, typed.size()); ++i)
            if ((static_cast<unsigned char>(typed[i]) & 0xC0) != 0x80) ++before;
        float x = in.x + 1.f;
        if (masked) {
            const float cw = codepoints(typed) ? w / static_cast<float>(codepoints(typed)) : 0.f;
            x += cw * static_cast<float>(before);
        } else {
            std::size_t bytes = 0, seen = 0;
            while (bytes < typed.size() && seen < before) {
                ++bytes;
                while (bytes < typed.size() && (static_cast<unsigned char>(typed[bytes]) & 0xC0) == 0x80) ++bytes;
                ++seen;
            }
            x += r.measure(std::string_view(typed).substr(0, bytes), f).width;
        }
        r.line({x, box.y + box.h * 0.22f}, {x, box.bottom() - box.h * 0.22f}, th.color.accent, 1.6f);
    }
    // Un choix aux fleches : < valeur >.
    void chooser(const hmi::Box& prevB, const hmi::Box& valueB, const hmi::Box& nextB, const std::string& value, const std::string& note,
                 gfx::FontId f, gfx::FontId small, HmiGlyph glyph, bool several) const {
        const gfx::Rect box = at(valueB);
        if (prevB.w > 0) {
            const gfx::Rect prev = at(prevB), next = at(nextB);
            button(prev, kButton, several);
            button(next, kButton, several);
            arrow(prev, false, faded(kText, several));
            arrow(next, true, faded(kText, several));
        }
        r.fillRoundedRect(box, kField, 3.f);
        r.strokeRect(box, kButtonEdge, 1.f);
        const float g = std::min(18.f, box.h * 0.6f);
        float x = box.x + 8.f;
        if (glyph != HmiGlyph::None) {
            drawHmiGlyph(r, glyph, {x, box.y + (box.h - g) / 2.f, g, g}, th.color.accent);
            x += g + 8.f;
        }
        const float w = text({x, box.y, box.right() - x - 6.f, box.h}, value.empty() ? std::string("(aucun)") : value, f, value.empty() ? kDim : kText);
        if (!note.empty()) text({x + w + 10.f, box.y, box.right() - x - w - 16.f, box.h}, note, small, kMuted);
    }
};

std::string typedOf(const hmi::FormState& f, const char* key) {
    const auto it = f.text.find(key);
    return it == f.text.end() ? std::string{} : it->second;
}
std::size_t caretOf(const hmi::FormState& f, const char* key) {
    const auto it = f.caret.find(key);
    return it == f.caret.end() ? typedOf(f, key).size() : it->second;
}

std::string levelNote(const hmi::Project* p, const hmi::User* u) {
    if (!p || !u) return {};
    std::string s = u->fullName;
    const int lvl = hmi::userLevel(*p, *u);
    return (s.empty() ? std::string{} : s + ", ") + "niveau " + std::to_string(lvl);
}

} // namespace

hmi::SignatureLayout signatureLayoutFor(const hmi::Runtime& rt, float w, float areaH) {
    const auto* rq = rt.signatureRequest();
    if (!rq) return {};
    return hmi::signatureLayout(w, areaH, rq->twoSigners, !rq->reasons.empty(), rt.userLogin().empty());
}

hmi::SignatureLayout paintSignaturePanel(gfx::IRenderer& r, const ui::Theme& th, const hmi::Runtime& rt, const gfx::Rect& screen,
                                         float areaH) {
    const auto l = signatureLayoutFor(rt, screen.w, std::min(screen.h, areaH));
    const auto* rq = rt.signatureRequest();
    if (!rq) return l;
    const auto* project = rt.project();
    const auto& form = rt.signatureForm();
    const Painter p{r, th, screen.x, screen.y};
    const gfx::FontId f = fontOf(l.fontSize), small = fontOf(l.fontSize * 0.86), title = fontOf(l.fontSize * 1.2);
    r.pushClip(screen);
    r.fillRect(screen, gfx::Color{0, 0, 0, 135});
    const gfx::Rect panel = p.at(l.panel);
    r.fillRect({panel.x + 6.f, panel.y + 8.f, panel.w, panel.h}, gfx::Color{0, 0, 0, 100});
    r.fillRoundedRect(panel, gfx::Color::rgb(0x1B222C), 6.f);
    // La barre de titre : le stylo, le titre, simple ou double, la croix.
    const gfx::Rect bar = p.at(l.title);
    r.fillRect(bar, gfx::Color::rgb(0x2D2640));
    const float g = std::min(22.f, bar.h * 0.56f);
    drawHmiGlyph(r, HmiGlyph::Signature, {bar.x + 12.f, bar.y + (bar.h - g) / 2.f, g, g}, kViolet);
    const float tw = p.text({bar.x + g + 22.f, bar.y, bar.w * 0.6f, bar.h}, "Signature \xC3\xA9lectronique", title, kText);
    p.text({bar.x + g + 34.f + tw, bar.y, bar.w - g - tw - 90.f, bar.h},
           rq->twoSigners ? "double : signataire et visa" : "simple : ton mot de passe et un motif", small, kViolet);
    {
        const gfx::Rect close = p.at(l.close);
        const float m = close.h * 0.34f;
        r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, kLabel, 1.8f);
        r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, kLabel, 1.8f);
    }
    // Ce qui est signe.
    {
        const gfx::Rect what = p.at(l.what);
        r.fillRoundedRect(what, kViolet.withAlpha(24), 4.f);
        r.strokeRect(what, kViolet.withAlpha(90), 1.f);
        p.text({what.x + 12.f, what.y + 2.f, what.w - 24.f, what.h * 0.45f}, "La commande : " + rq->source, small, kMuted);
        p.text({what.x + 12.f, what.y + what.h * 0.45f, what.w - 24.f, what.h * 0.55f - 2.f}, rq->what, f, kText);
    }
    const float labelX = panel.x + 18.f;
    const auto label = [&](const hmi::Box& b, const std::string& s, bool on = true) {
        const gfx::Rect box = p.at(b);
        p.text({labelX, box.y, box.x > labelX ? box.w : 150.f, box.h}, s, f, on ? kLabel : kDim);
    };
    // Le signataire, son mot de passe.
    const hmi::User* signer = rt.signatureSigner();
    const bool severalSigners = project && std::count_if(project->security.users.begin(), project->security.users.end(), [](const hmi::User& u) {
                                               return u.enabled && ((u.protection == "classique" && !u.passwordHash.empty())
                                                                    || (u.protection == "dynamique" && !u.secret.empty()));
                                           }) > 1;
    label(l.signerLabel, "Signataire");
    p.chooser(l.signerPrev, l.signer, l.signerNext, signer ? signer->login : std::string{},
              signer ? levelNote(project, signer) + (rt.userLogin().empty() ? std::string{} : "  \xC2\xB7  connect\xC3\xA9") : std::string{}, f, small,
              HmiGlyph::User, severalSigners);
    {
        const gfx::Rect box = p.at(l.password);
        const bool dyn = signer && signer->protection == "dynamique";
        p.text({labelX, box.y, box.x - labelX - 12.f, box.h}, dyn ? "Code" : "Mot de passe", f, kLabel);
        p.field(box, typedOf(form, "motdepasse"), caretOf(form, "motdepasse"), form.focus == "motdepasse", true,
                dyn ? "le code de ton application" : "le tien, \xC3\xA0 retaper", f);
    }
    // Le motif.
    if (l.reasonList) {
        label(l.reasonLabel, "Motif");
        const std::size_t k = std::min(rq->reason, rq->reasons.empty() ? std::size_t{0} : rq->reasons.size() - 1);
        p.chooser(l.reasonPrev, l.reason, l.reasonNext, rq->reasons.empty() ? std::string{} : rq->reasons[k],
                  std::to_string(k + 1) + " / " + std::to_string(rq->reasons.size()), f, small, HmiGlyph::None, rq->reasons.size() > 1);
    } else {
        label(l.reasonLabel, "Motif");
        p.field(p.at(l.reasonField), typedOf(form, "motif"), caretOf(form, "motif"), form.focus == "motif", false,
                "pourquoi (obligatoire)", f);
    }
    // Le visa.
    if (l.twoSigners) {
        const gfx::Rect vt = p.at(l.visaTitle);
        r.line({vt.x, vt.y + 2.f}, {vt.right(), vt.y + 2.f}, gfx::Color::rgb(0x2E3643), 1.f);
        p.text({vt.x, vt.y + 4.f, vt.w, vt.h - 4.f},
               "Visa d'un second compte (niveau " + std::to_string(rq->visaLevel) + " au moins) :", small, kViolet);
        const hmi::User* visa = rt.signatureVisa();
        label(l.visaLabel, "Visa de");
        const bool levelOk = visa && project && hmi::userLevel(*project, *visa) >= rq->visaLevel;
        p.chooser(l.visaPrev, l.visa, l.visaNext, visa ? visa->login : std::string{},
                  visa ? levelNote(project, visa) + (levelOk ? std::string{} : "  \xC2\xB7  niveau insuffisant") : std::string{}, f, small,
                  HmiGlyph::Users, severalSigners);
        const gfx::Rect box = p.at(l.visaPassword);
        const bool dyn = visa && visa->protection == "dynamique";
        p.text({labelX, box.y, box.x - labelX - 12.f, box.h}, dyn ? "Son code" : "Son mot de passe", f, kLabel);
        p.field(box, typedOf(form, "visa"), caretOf(form, "visa"), form.focus == "visa", true, "le sien", f);
    }
    // Signer, annuler.
    {
        const gfx::Rect ok = p.at(l.sign), cancel = p.at(l.cancel);
        p.button(ok, gfx::Color::rgb(0x5E4A9A), true);
        const float gs = std::min(18.f, ok.h * 0.55f);
        drawHmiGlyph(r, HmiGlyph::Signature, {ok.x + 10.f, ok.y + (ok.h - gs) / 2.f, gs, gs}, gfx::Color::rgb(0xFFFFFF));
        p.text({ok.x + gs + 16.f, ok.y, ok.w - gs - 22.f, ok.h}, "Signer", f, gfx::Color::rgb(0xFFFFFF), 1);
        p.button(cancel, gfx::Color::rgb(0x3A3040), true);
        p.text(cancel.inset(8.f, 0.f), "Annuler", f, kText, 1);
    }
    // La reponse, et ce que vaut la signature.
    if (!form.message.empty()) {
        const gfx::Rect m = p.at(l.message);
        const float d = std::min(6.f, m.h * 0.2f);
        shapes::fillPolygon(r, shapes::ellipse({m.x + d + 2.f, m.y + m.h / 2.f}, d, d, 16), form.error ? kRed : kGreen);
        p.text({m.x + 2.f * d + 10.f, m.y, m.w - 2.f * d - 10.f, m.h}, form.message, f, form.error ? kRed : kGreen);
    }
    const gfx::Rect status = p.at(l.status);
    r.fillRect(status, gfx::Color::rgb(0x151A22));
    p.text(status.inset(12.f, 0.f),
           "Ta signature vaut accord : le journal d'audit garde qui a sign\xC3\xA9, le motif et ce que la commande a chang\xC3\xA9.", small,
           gfx::Color::rgb(0x7F8A9A));
    r.strokeRect(panel, kViolet, 1.5f);
    r.popClip();
    return l;
}

hmi::PromptLayout paintPromptPanel(gfx::IRenderer& r, const ui::Theme& th, const hmi::Runtime& rt, const gfx::Rect& screen, float areaH) {
    const auto l = hmi::promptLayout(screen.w, std::min(screen.h, areaH));
    const auto* pr = rt.keyboardPrompt();
    if (!pr) return l;
    const auto& form = rt.promptForm();
    const Painter p{r, th, screen.x, screen.y};
    const gfx::FontId f = fontOf(l.fontSize), small = fontOf(l.fontSize * 0.82), big = fontOf(l.fontSize * 1.3), title = fontOf(l.fontSize * 1.05);
    const gfx::Color accent = gfx::Color::rgb(0x3D7BD9);
    r.pushClip(screen);
    r.fillRect(screen, gfx::Color{0, 0, 0, 135});
    const gfx::Rect panel = p.at(l.panel);
    r.fillRect({panel.x + 6.f, panel.y + 8.f, panel.w, panel.h}, gfx::Color{0, 0, 0, 100});
    r.fillRoundedRect(panel, gfx::Color::rgb(0x1B222C), 6.f);
    // Le titre, la croix.
    const gfx::Rect bar = p.at(l.title);
    r.fillRect(bar, gfx::Color::rgb(0x22314A));
    const float g = std::min(22.f, bar.h * 0.56f);
    drawHmiGlyph(r, HmiGlyph::Keyboard, {bar.x + 12.f, bar.y + (bar.h - g) / 2.f, g, g}, accent);
    p.text({bar.x + g + 22.f, bar.y, bar.w - g - 70.f, bar.h}, pr->title, title, kText);
    {
        const gfx::Rect close = p.at(l.close);
        const float m = close.h * 0.34f;
        r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, kLabel, 1.8f);
        r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, kLabel, 1.8f);
    }
    // Le champ : la valeur (choisie tant qu'on n'a pas tape), son unite a droite.
    {
        const gfx::Rect box = p.at(l.field);
        auto it = form.text.find("valeur");
        const std::string typed = it == form.text.end() ? std::string{} : it->second;
        auto ct = form.caret.find("valeur");
        const std::size_t caret = ct == form.caret.end() ? typed.size() : ct->second;
        if (form.fresh && !typed.empty()) r.fillRoundedRect(box, accent.withAlpha(40), 4.f);
        p.field(box, typed, caret, true, pr->mask, pr->mask ? "le code" : "la valeur", big);
        if (!pr->unit.empty()) p.text({box.right() - 90.f, box.y, 80.f, box.h}, pr->unit, f, kLabel, 2);
    }
    // Les limites.
    {
        std::string lim;
        if (pr->min && pr->max) lim = "de " + hmi::formatNumber(*pr->min) + " \xC3\xA0 " + hmi::formatNumber(*pr->max);
        else if (pr->min) lim = "au moins " + hmi::formatNumber(*pr->min);
        else if (pr->max) lim = "au plus " + hmi::formatNumber(*pr->max);
        const std::string kind = pr->type == sim::Type::Bool ? "TRUE ou FALSE" : pr->type == sim::Type::String ? "un texte" : "un nombre";
        p.text(p.at(l.limits), kind + (lim.empty() ? std::string{} : " \xC2\xB7 " + lim) + (pr->unit.empty() ? std::string{} : " " + pr->unit)
                                   + " \xC2\xB7 Entr\xC3\xA9" "e valide, \xC3\x89" "chap annule",
               small, kMuted);
    }
    if (!form.message.empty()) p.text(p.at(l.message), form.message, f, form.error ? kRed : kGreen);
    // Valider, Annuler.
    {
        const gfx::Rect ok = p.at(l.ok), cancel = p.at(l.cancel);
        p.button(ok, accent, true);
        p.text(ok.inset(8.f, 0.f), "Valider", f, gfx::Color::rgb(0xFFFFFF), 1);
        p.button(cancel, gfx::Color::rgb(0x3A3F4A), true);
        p.text(cancel.inset(8.f, 0.f), "Annuler", f, kText, 1);
    }
    r.strokeRect(panel, accent, 1.5f);
    r.popClip();
    return l;
}

LogoutWarningRects paintLogoutWarning(gfx::IRenderer& r, const ui::Theme& th, const hmi::Runtime& rt, const gfx::Rect& screen) {
    LogoutWarningRects out;
    const double now = rt.now();
    if (!rt.logoutWarning(now)) return out;
    const double left = std::max(0.0, rt.autoLogoutRemaining(now));
    const auto* project = rt.project();
    const double warn = project ? std::max(1, project->security.logoutWarnS) : 30.0;
    const float h = std::clamp(screen.h * 0.075f, 34.f, 64.f);
    const gfx::Rect bar{screen.x, screen.y, screen.w, h};
    r.fillRect(bar, gfx::Color::rgb(0xF2994A).withAlpha(238));
    // Le temps qui reste, en barre sous le bandeau.
    const float frac = static_cast<float>(std::clamp(left / warn, 0.0, 1.0));
    r.fillRect({bar.x, bar.bottom() - 4.f, bar.w, 4.f}, gfx::Color::rgb(0x7A3E0C));
    r.fillRect({bar.x, bar.bottom() - 4.f, bar.w * frac, 4.f}, gfx::Color::rgb(0xFFF1E0));
    const gfx::FontId f = fontOf(h * 0.36), small = fontOf(h * 0.3);
    const gfx::Color ink = gfx::Color::rgb(0x2A1604);
    const float g = h * 0.5f;
    drawHmiGlyph(r, HmiGlyph::Clock, {bar.x + 14.f, bar.y + (h - 4.f - g) / 2.f, g, g}, ink);
    const float bw = std::min(220.f, bar.w * 0.28f);
    out.button = {bar.right() - bw - 12.f, bar.y + h * 0.16f, bw, h * 0.62f};
    const int secs = static_cast<int>(std::ceil(left));
    const std::string line = "D\xC3\xA9" "connexion automatique de " + rt.userLogin() + " dans " + std::to_string(secs) + " s";
    const float x0 = bar.x + g + 26.f;
    const float room = out.button.x - x0 - 16.f;
    const auto n = r.fitCharacters(line, f, room);
    const float lw = r.measure(std::string_view(line).substr(0, n), f).width;
    r.drawText({x0, bar.y + (h - 4.f) * 0.5f - r.lineHeight(f) * 0.5f}, std::string_view(line).substr(0, n), f, ink);
    const std::string hint = "  \xE2\x80\x94  touche l'\xC3\xA9" "cran pour rester connect\xC3\xA9";
    if (room - lw > 60.f) {
        const auto m = r.fitCharacters(hint, small, room - lw);
        r.drawText({x0 + lw, bar.y + (h - 4.f) * 0.5f - r.lineHeight(small) * 0.5f}, std::string_view(hint).substr(0, m), small,
                   ink.withAlpha(200));
    }
    r.fillRoundedRect(out.button, gfx::Color::rgb(0x2A1604), 5.f);
    const std::string stay = "Rester connect\xC3\xA9";
    const auto k = r.fitCharacters(stay, f, out.button.w - 12.f);
    const float sw = r.measure(std::string_view(stay).substr(0, k), f).width;
    r.drawText({out.button.x + (out.button.w - sw) / 2.f, out.button.y + (out.button.h - r.lineHeight(f)) / 2.f},
               std::string_view(stay).substr(0, k), f, gfx::Color::rgb(0xFFF1E0));
    (void)th;
    out.bar = bar;
    return out;
}

} // namespace app

// ============================================================ la langue =====
namespace app::paint {

// ---- l'accessibilite : les symboles --------------------------------------------------------
void drawStatusSymbol(const Ctx& c, int symbol, double cx, double cy, double size, gfx::Color on) {
    if (symbol <= 0 || size < 6) return;
    // Clair sur une couleur sombre, sombre sur une claire.
    const double lum = (0.2126 * on.r + 0.7152 * on.g + 0.0722 * on.b) / 255.0;
    const gfx::Color ink = fade(lum > 0.55 ? gfx::Color::rgb(0x101418) : gfx::Color::rgb(0xFFFFFF), c.alpha);
    const double s = size / 2;
    const float width = std::max(1.5f, static_cast<float>(size * 0.14) * c.vp.zoom);
    const auto seg = [&](double x1, double y1, double x2, double y2) { c.r.line(c.map(x1, y1), c.map(x2, y2), ink, width); };
    switch (symbol) {
        case 1:   // une coche
            seg(cx - s * 0.62, cy + s * 0.02, cx - s * 0.15, cy + s * 0.5);
            seg(cx - s * 0.15, cy + s * 0.5, cx + s * 0.66, cy - s * 0.45);
            break;
        case 2:   // une croix
            seg(cx - s * 0.5, cy - s * 0.5, cx + s * 0.5, cy + s * 0.5);
            seg(cx + s * 0.5, cy - s * 0.5, cx - s * 0.5, cy + s * 0.5);
            break;
        case 3:   // un point d'exclamation
            seg(cx, cy - s * 0.62, cx, cy + s * 0.18);
            seg(cx, cy + s * 0.44, cx, cy + s * 0.62);
            break;
        default:  // un tiret : eteint
            seg(cx - s * 0.5, cy, cx + s * 0.5, cy);
            break;
    }
}

void drawFaultBadge(const Ctx& c, double right, double top, double size) {
    const double x0 = right - size, y0 = top;
    std::vector<gfx::Point> tri{{static_cast<float>(x0 + size / 2), static_cast<float>(y0)},
                                {static_cast<float>(x0 + size), static_cast<float>(y0 + size * 0.9)},
                                {static_cast<float>(x0), static_cast<float>(y0 + size * 0.9)}};
    const auto pts = c.map(tri);
    shapes::fillPolygon(c.r, pts, c.fixed(0xF2C14B));
    shapes::strokePolyline(c.r, pts, true, c.fixedOn(0x101418), std::max(1.f, c.vp.zoom));
    const float width = std::max(1.5f, static_cast<float>(size * 0.12) * c.vp.zoom);
    const gfx::Color ink = c.fixedOn(0x101418);
    c.r.line(c.map(x0 + size / 2, y0 + size * 0.3), c.map(x0 + size / 2, y0 + size * 0.6), ink, width);
    c.r.line(c.map(x0 + size / 2, y0 + size * 0.7), c.map(x0 + size / 2, y0 + size * 0.78), ink, width);
}

// ---- le selecteur de theme : Jour | Nuit -----------------------------------------------------
namespace {
void drawSun(const Ctx& c, double cx, double cy, double r, gfx::Color col) {
    shapes::fillPolygon(c.r, c.map(shapes::ellipse({static_cast<float>(cx), static_cast<float>(cy)}, static_cast<float>(r * 0.5),
                                                   static_cast<float>(r * 0.5), 20)),
                        col);
    const float width = std::max(1.f, static_cast<float>(r * 0.16) * c.vp.zoom);
    for (int k = 0; k < 8; ++k) {
        const double a = 3.14159265 * k / 4;
        c.r.line(c.map(cx + std::cos(a) * r * 0.72, cy + std::sin(a) * r * 0.72), c.map(cx + std::cos(a) * r, cy + std::sin(a) * r), col, width);
    }
}
void drawMoon(const Ctx& c, double cx, double cy, double r, gfx::Color col) {
    // Un croissant : le disque, moins un disque decale (dessine en arc).
    std::vector<gfx::Point> pts;
    for (int k = 0; k <= 24; ++k) {
        const double a = 3.14159265 * (0.3 + 1.4 * k / 24.0);
        pts.push_back({static_cast<float>(cx + std::cos(a) * r), static_cast<float>(cy + std::sin(a) * r)});
    }
    for (int k = 24; k >= 0; --k) {
        const double a = 3.14159265 * (0.42 + 1.16 * k / 24.0);
        pts.push_back({static_cast<float>(cx + r * 0.42 + std::cos(a) * r * 0.78), static_cast<float>(cy + std::sin(a) * r * 0.78)});
    }
    shapes::fillPolygon(c.r, c.map(pts), col);
}
} // namespace

// Le selecteur de langue : un bouton par langue, la langue en cours en
// surbrillance ; ou une bascule (un seul bouton, la langue en cours, et le
// globe). En marche : la langue du moteur ; dans l'editeur : celle du
// demarrage (Configuration > Langues). La geometrie est celle du clic.
bool drawLot13(const Ctx& c) {
    if (c.o.kind == hmi::Kind::ThemeSelector) {
        const double w = c.w(), h = c.h();
        const auto l = hmi::languageSelectorLayout(c.o, w, h, 2);
        const std::string theme = !c.opt.editor && c.opt.runtime ? c.opt.runtime->settings().theme
                                                                  : (c.opt.project ? c.opt.project->config.theme : std::string("nuit"));
        const auto [dayLabel, nightLabel] = hmi::themeLabels(c.o);
        const gfx::Color button = c.color("buttonColor", gfx::Color::rgb(0x2A313C));
        const gfx::Color active = c.color("activeColor", gfx::Color::rgb(0x2F6FD6));
        const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xC8D0DC));
        const gfx::Color activeTxt = c.color("activeTextColor", gfx::Color::rgb(0xFFFFFF));
        const double radius = std::max(0.0, c.src.number(c.o, "radius", 5));
        fillAndStroke(c, rectLocal(w, h, radius));
        const bool icon = c.src.flag(c.o, "icon", true);
        for (std::size_t i = 0; i < l.buttons.size() && i < 2; ++i) {
            const auto& b = l.buttons[i];
            const bool on = (i == 0) == (theme == "jour");
            auto pts = rectLocal(b.w, b.h, radius);
            for (auto& p : pts) {
                p.x += static_cast<float>(b.x);
                p.y += static_cast<float>(b.y);
            }
            shapes::fillPolygon(c.r, c.map(pts), on ? active : button);
            const gfx::Color ink = on ? activeTxt : txt;
            const double g = icon ? std::min(b.h * 0.5, 22.0) : 0.0;
            const std::string& label = i == 0 ? dayLabel : nightLabel;
            // Trop etroit pour l'icone et le libelle (un texte agrandi) : l'icone seule.
            const double full = hmi::approxTextWidth(label, l.fontSize);
            const bool showLabel = g <= 0 || fitted(c, label, b.w - g - 14, l.fontSize) == label;
            const double tw = showLabel ? std::min(b.w - g - 16, full) : 0.0;
            const double gap = g > 0 && showLabel ? 8.0 : 0.0;
            const double x0 = b.cx() - (g + gap + tw) / 2;
            if (g > 0) {
                if (i == 0) drawSun(c, x0 + g / 2, b.cy(), g / 2, ink);
                else drawMoon(c, x0 + g / 2, b.cy(), g / 2, ink);
            }
            if (showLabel) text(c, fitted(c, label, b.w - g - 14, l.fontSize), x0 + g + gap, b.y, tw + 4, b.h, ink, l.fontSize, "gauche");
        }
        return true;
    }
    if (c.o.kind != hmi::Kind::LanguageSelector) return false;
    const double w = c.w(), h = c.h();
    static const hmi::Languages kNone;
    const hmi::Languages& langs = c.opt.project ? c.opt.project->languages : kNone;
    const auto choices = hmi::languageChoices(c.o, langs);
    const auto l = hmi::languageSelectorLayout(c.o, w, h, choices.size());
    std::string current = !c.opt.editor && c.opt.runtime ? c.opt.runtime->language() : std::string{};
    if (current.empty()) current = !langs.startLanguage.empty() && langs.find(langs.startLanguage) ? langs.startLanguage : langs.source();
    const gfx::Color button = c.color("buttonColor", gfx::Color::rgb(0x2A313C));
    const gfx::Color active = c.color("activeColor", gfx::Color::rgb(0x2F6FD6));
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xC8D0DC));
    const gfx::Color activeTxt = c.color("activeTextColor", gfx::Color::rgb(0xFFFFFF));
    const double radius = std::max(0.0, c.src.number(c.o, "radius", 5));
    const auto box = [&](const hmi::Box& b, gfx::Color col) {
        if (!col.a || b.w <= 0 || b.h <= 0) return;
        auto pts = rectLocal(b.w, b.h, radius);
        for (auto& p : pts) {
            p.x += static_cast<float>(b.x);
            p.y += static_cast<float>(b.y);
        }
        shapes::fillPolygon(c.r, c.map(pts), col);
    };
    // Le fond et le contour de l'objet.
    fillAndStroke(c, rectLocal(w, h, radius));
    if (choices.empty()) {
        text(c, "S\xC3\xA9lecteur de langue", 0, 0, w, h, c.fixedText(0x8A96A8), l.fontSize, "centre");
        return true;
    }
    // La langue en cours (sans casse) ; absente du selecteur : aucun bouton allume.
    std::size_t on = choices.size();
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i].code.size() == current.size()
            && std::equal(current.begin(), current.end(), choices[i].code.begin(), [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
               }))
            on = i;
    if (l.toggle) {
        if (l.buttons.empty()) return true;
        const auto& b = l.buttons.front();
        box(b, button);
        // Le globe a gauche, la langue en cours a sa droite.
        const double g = std::min(b.h * 0.56, 26.0);
        const auto a = c.map(b.x + 8, b.cy() - g / 2), z = c.map(b.x + 8 + g, b.cy() + g / 2);
        drawHmiGlyph(c.r, HmiGlyph::Language, {std::min(a.x, z.x), std::min(a.y, z.y), std::fabs(z.x - a.x), std::fabs(z.y - a.y)}, activeTxt);
        const std::string shown = on < choices.size() ? choices[on].label : current;
        text(c, fitted(c, shown, b.w - g - 22, l.fontSize), b.x + g + 14, b.y, b.w - g - 20, b.h, activeTxt, l.fontSize, "gauche");
        return true;
    }
    for (std::size_t i = 0; i < choices.size() && i < l.buttons.size(); ++i) {
        const auto& b = l.buttons[i];
        box(b, i == on ? active : button);
        text(c, fitted(c, choices[i].label, b.w - 10, l.fontSize), b.x, b.y, b.w, b.h, i == on ? activeTxt : txt, l.fontSize, "centre");
    }
    return true;
}

} // namespace app::paint
