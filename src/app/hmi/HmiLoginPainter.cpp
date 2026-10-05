#include "HmiLoginPainter.hpp"

#include "HmiIcons.hpp"
#include "../../hmi/HmiPolicy.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

namespace {

namespace shapes = ui::shapes;

const gfx::Color kText = gfx::Color::rgb(0xE6EAF0), kMuted = gfx::Color::rgb(0x9AA6B8), kDim = gfx::Color::rgb(0x6B7686);
const gfx::Color kLabel = gfx::Color::rgb(0xC8D0DC);
const gfx::Color kField = gfx::Color::rgb(0x141820), kButton = gfx::Color::rgb(0x2A313C), kButtonEdge = gfx::Color::rgb(0x3A4556);
const gfx::Color kOrange = gfx::Color::rgb(0xF2994A), kRed = gfx::Color::rgb(0xE5534B), kGreen = gfx::Color::rgb(0x2ECC71);
const gfx::Color kBlue = gfx::Color::rgb(0x5DA9E9);

gfx::FontId fontOf(double px) { return gfx::FontId{static_cast<std::uint16_t>(std::clamp(px, 8.0, 40.0))}; }
gfx::Color faded(gfx::Color c, bool on) { return on ? c : c.withAlpha(static_cast<std::uint8_t>(c.a * 0.42f)); }

struct Painter {
    gfx::IRenderer& r;
    const ui::Theme& th;
    float ox, oy;

    [[nodiscard]] gfx::Rect at(const hmi::Box& b) const {
        return {ox + static_cast<float>(b.x), oy + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    }
    // Un texte dans une case : 0 a gauche, 1 au centre, 2 a droite ; coupe s'il deborde.
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
    // Un triangle : vers le haut (0), le bas (1), la gauche (2), la droite (3).
    void arrow(const gfx::Rect& b, int dir, gfx::Color c) const {
        const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.24f;
        std::vector<gfx::Point> p;
        switch (dir) {
            case 0: p = {{cx - s, cy + s * 0.5f}, {cx + s, cy + s * 0.5f}, {cx, cy - s * 0.6f}}; break;
            case 1: p = {{cx - s, cy - s * 0.5f}, {cx + s, cy - s * 0.5f}, {cx, cy + s * 0.6f}}; break;
            case 2: p = {{cx + s * 0.5f, cy - s}, {cx + s * 0.5f, cy + s}, {cx - s * 0.6f, cy}}; break;
            default: p = {{cx - s * 0.5f, cy - s}, {cx - s * 0.5f, cy + s}, {cx + s * 0.6f, cy}}; break;
        }
        shapes::fillPolygon(r, p, c);
    }
    void plusMinus(const gfx::Rect& b, bool plus, gfx::Color c) const {
        const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.22f;
        const float t = std::max(1.5f, std::min(b.w, b.h) * 0.08f);
        r.line({cx - s, cy}, {cx + s, cy}, c, t);
        if (plus) r.line({cx, cy - s}, {cx, cy + s}, c, t);
    }
    void lock(const gfx::Rect& box, float size) const {
        const float g = std::min(size, box.h * 0.6f);
        drawHmiGlyph(r, HmiGlyph::Lock, {box.x, box.y + (box.h - g) / 2.f, g, g}, kOrange);
    }
    void dot(float cx, float cy, float rad, gfx::Color c) const { shapes::fillPolygon(r, shapes::ellipse({cx, cy}, rad, rad, 20), c); }
};

std::size_t codepoints(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// Un champ masque : des points, le curseur quand il a le focus.
void secretField(const Painter& p, const gfx::Rect& box, const std::string& typed, std::size_t caret, bool focused, bool enabled,
                 std::string_view placeholder, gfx::FontId f) {
    p.r.fillRoundedRect(box, faded(kField, enabled), 3.f);
    p.r.strokeRect(box, focused ? p.th.color.accent : faded(kButtonEdge, enabled), focused ? 1.8f : 1.f);
    const gfx::Rect in = box.inset(10.f, 0.f);
    std::string dots;
    for (std::size_t i = 0; i < codepoints(typed); ++i) dots += "\xE2\x80\xA2";
    if (dots.empty() && !focused) {
        p.text(in, placeholder, f, faded(kDim, enabled));
        return;
    }
    const float w = p.text(in, dots, f, kText);
    if (focused) {
        // Le curseur : apres les points qui le precedent.
        std::size_t before = 0;
        for (std::size_t i = 0; i < std::min(caret, typed.size()); ++i)
            if ((static_cast<unsigned char>(typed[i]) & 0xC0) != 0x80) ++before;
        const float cw = codepoints(typed) ? w / static_cast<float>(codepoints(typed)) : 0.f;
        const float x = in.x + cw * static_cast<float>(before) + 1.f;
        p.r.line({x, box.y + box.h * 0.22f}, {x, box.bottom() - box.h * 0.22f}, p.th.color.accent, 1.6f);
    }
}

std::string clockOf(const hmi::Runtime& rt, double t) {
    const std::string stamp = rt.dateStampOf(t);
    return stamp.size() >= 19 ? stamp.substr(11, 8) : stamp;
}

std::string durationText(double seconds) {
    const long long s = static_cast<long long>(std::floor(std::max(0.0, seconds)));
    char b[48];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min", s / 3600, (s / 60) % 60);
    else if (s >= 60) std::snprintf(b, sizeof b, "%lld min %02lld s", s / 60, s % 60);
    else std::snprintf(b, sizeof b, "%lld s", s);
    return b;
}

const hmi::UserGroup* groupOf(const hmi::Project* p, hmi::Id id) {
    if (!p) return nullptr;
    for (const auto& g : p->security.groups) if (g.id == id) return &g;
    return nullptr;
}

HmiGlyph tabGlyph(hmi::LoginTab t) {
    switch (t) {
        case hmi::LoginTab::Connexion: return HmiGlyph::Login;
        case hmi::LoginTab::Compte:    return HmiGlyph::UserInfo;
        case hmi::LoginTab::Comptes:   return HmiGlyph::Users;
        case hmi::LoginTab::Acces:     return HmiGlyph::Key;
        case hmi::LoginTab::Journal:   return HmiGlyph::History;
    }
    return HmiGlyph::User;
}

// ---- Lot 13 : le renouvellement du mot de passe ------------------------------------------
void paintRenewal(const Painter& p, const hmi::Runtime& rt, const hmi::LoginMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto* project = rt.project();
    const auto& form = rt.loginForm();
    const std::string who = rt.renewalLogin();
    const hmi::User* account = project ? project->userByLogin(who) : nullptr;
    {
        const gfx::Rect box = p.at(l.who);
        p.r.fillRoundedRect(box, kOrange.withAlpha(28), 4.f);
        p.r.strokeRect(box, kOrange.withAlpha(120), 1.f);
        const float g = std::min(22.f, box.h * 0.42f);
        drawHmiGlyph(p.r, HmiGlyph::Password, {box.x + 10.f, box.y + (box.h - g) / 2.f, g, g}, kOrange);
        const std::string name = account && !account->fullName.empty() ? account->fullName + " (" + who + ")" : who;
        p.text({box.x + g + 20.f, box.y + 2.f, box.w - g - 28.f, box.h / 2.f}, "Nouveau mot de passe pour " + name, f, kText);
        p.text({box.x + g + 20.f, box.y + box.h / 2.f - 2.f, box.w - g - 28.f, box.h / 2.f}, rt.renewalReason(), small, kOrange);
    }
    const float labelX = static_cast<float>(l.body.x) + p.ox;
    const float labelW = p.at(l.newField).x - labelX - 12.f;
    const std::string rules = project ? hmi::passwordRules(project->security, hmi::kMenuPasswordMin) : std::string("6 caract\xC3\xA8res au moins");
    struct Row { const hmi::Box* box; const char* key; const char* label; std::string hint; };
    const Row rows[] = {{&l.newField, "nouveau", "Nouveau", rules}, {&l.confirmField, "confirmation", "Confirmation", "le m\xC3\xAAme"}};
    for (const auto& row : rows) {
        const gfx::Rect box = p.at(*row.box);
        p.text({labelX, box.y, labelW, box.h}, row.label, f, kLabel);
        const auto it = form.text.find(row.key);
        const auto ct = form.caret.find(row.key);
        const std::string typed = it == form.text.end() ? std::string{} : it->second;
        secretField(p, box, typed, ct == form.caret.end() ? typed.size() : ct->second, form.focus == row.key, true, row.hint, f);
    }
    const gfx::Rect ok = p.at(l.login), cancel = p.at(l.logout);
    p.button(ok, p.th.color.accent, true);
    const float g = std::min(18.f, ok.h * 0.55f);
    drawHmiGlyph(p.r, HmiGlyph::Password, {ok.x + 10.f, ok.y + (ok.h - g) / 2.f, g, g}, gfx::Color::rgb(0xFFFFFF));
    p.text({ok.x + g + 16.f, ok.y, ok.w - g - 22.f, ok.h}, "Renouveler", f, gfx::Color::rgb(0xFFFFFF), 1);
    p.button(cancel, gfx::Color::rgb(0x3A3040), true);
    p.text(cancel.inset(8.f, 0.f), "Annuler", f, kText, 1);
}

// ---- Connexion ------------------------------------------------------------------------
void paintConnexion(const Painter& p, const hmi::Runtime& rt, const hmi::LoginMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    if (l.renewal) { paintRenewal(p, rt, l, f, small); return; }     // lot 13
    const auto* project = rt.project();
    const auto& form = rt.loginForm();
    const hmi::User* me = rt.user();
    // Qui est connecte.
    {
        const gfx::Rect who = p.at(l.who);
        p.r.fillRoundedRect(who, gfx::Color{255, 255, 255, 10}, 4.f);
        p.dot(who.x + 14.f, who.y + who.h / 2.f, std::min(6.f, who.h * 0.2f), me ? kGreen : kDim);
        std::string line;
        if (me) {
            const auto* g = groupOf(project, me->group);
            line = "Connect\xC3\xA9 : " + (me->fullName.empty() ? me->login : me->fullName + " (" + me->login + ")")
                 + (g ? " \xE2\x80\x94 " + g->name + ", niveau " + std::to_string(g->level) : std::string{});
        } else {
            line = project && project->security.enabled ? "Personne n'est connect\xC3\xA9 (niveau 0)" : "S\xC3\xA9" "curit\xC3\xA9 inactive : tout est permis";
        }
        p.text({who.x + 28.f, who.y, who.w - 36.f, who.h}, line, f, me ? kText : kMuted);
    }
    const float labelX = static_cast<float>(l.body.x) + p.ox;
    const float labelW = p.at(l.prev).x - labelX - 12.f;
    // Le compte, aux fleches.
    const hmi::User* account = rt.loginAccount();
    {
        const gfx::Rect prev = p.at(l.prev), next = p.at(l.next), box = p.at(l.account);
        p.text({labelX, box.y, labelW, box.h}, "Compte", f, kLabel);
        const bool several = project && std::count_if(project->security.users.begin(), project->security.users.end(),
                                                      [](const hmi::User& u) { return u.enabled; }) > 1;
        p.button(prev, kButton, several);
        p.button(next, kButton, several);
        p.arrow(prev, 2, faded(kText, several));
        p.arrow(next, 3, faded(kText, several));
        p.r.fillRoundedRect(box, kField, 3.f);
        p.r.strokeRect(box, kButtonEdge, 1.f);
        if (account) {
            const float g = std::min(18.f, box.h * 0.6f);
            drawHmiGlyph(p.r, HmiGlyph::User, {box.x + 8.f, box.y + (box.h - g) / 2.f, g, g}, p.th.color.accent);
            const float w = p.text({box.x + g + 16.f, box.y, box.w - g - 22.f, box.h}, account->login, f, kText);
            if (!account->fullName.empty())
                p.text({box.x + g + 26.f + w, box.y, box.w - g - 32.f - w, box.h}, account->fullName, small, kMuted);
        } else {
            p.text(box.inset(10.f, 0.f), "aucun compte actif", f, kDim);
        }
    }
    // Le mot de passe (ou le code, ou rien : l'autorisation par expression).
    {
        const gfx::Rect box = p.at(l.secret);
        const std::string protection = account ? account->protection : std::string("classique");
        const bool expression = protection == "expression";
        const std::string label = protection == "dynamique" ? "Code" : expression ? "Autorisation" : "Mot de passe";
        p.text({labelX, box.y, labelW, box.h}, label, f, kLabel);
        if (expression) {
            p.r.fillRoundedRect(box, faded(kField, false), 3.f);
            p.r.strokeRect(box, faded(kButtonEdge, false), 1.f);
            p.text(box.inset(10.f, 0.f), "par expression : " + (account ? account->expression : std::string{}), small, kMuted);
        } else {
            const auto it = form.text.find("secret");
            const auto ct = form.caret.find("secret");
            const std::string typed = it == form.text.end() ? std::string{} : it->second;
            const std::size_t caret = ct == form.caret.end() ? typed.size() : ct->second;
            const std::string placeholder = protection == "dynamique"
                                              ? std::to_string(project ? project->security.dynamicDigits : 6) + " chiffres, ceux de l'application"
                                              : std::string("touche ici pour le taper");
            secretField(p, box, typed, caret, form.focus == "secret", account != nullptr, placeholder, f);
        }
    }
    // Se connecter, se deconnecter.
    {
        const gfx::Rect in = p.at(l.login), out = p.at(l.logout);
        p.button(in, p.th.color.accent, account != nullptr);
        const float g = std::min(18.f, in.h * 0.55f);
        drawHmiGlyph(p.r, HmiGlyph::Login, {in.x + 10.f, in.y + (in.h - g) / 2.f, g, g}, faded(gfx::Color::rgb(0xFFFFFF), account != nullptr));
        p.text({in.x + g + 16.f, in.y, in.w - g - 22.f, in.h}, "Se connecter", f, faded(gfx::Color::rgb(0xFFFFFF), account != nullptr), 1);
        p.button(out, gfx::Color::rgb(0x3A3040), me != nullptr);
        drawHmiGlyph(p.r, HmiGlyph::Logout, {out.x + 10.f, out.y + (out.h - g) / 2.f, g, g}, faded(kText, me != nullptr));
        p.text({out.x + g + 16.f, out.y, out.w - g - 22.f, out.h}, "Se d\xC3\xA9" "connecter", f, faded(kText, me != nullptr), 1);
    }
}

// ---- Mon compte -------------------------------------------------------------------------
void paintAccount(const Painter& p, const hmi::Runtime& rt, const hmi::LoginMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto* project = rt.project();
    const auto& form = rt.loginForm();
    const hmi::User* me = rt.user();
    const gfx::Rect info = p.at(l.info);
    p.r.fillRoundedRect(info, gfx::Color{255, 255, 255, 10}, 4.f);
    const float lh = info.h / 3.f;
    const bool classic = me && me->protection == "classique";
    if (!me) {
        p.text({info.x + 12.f, info.y, info.w - 24.f, lh}, "Personne n'est connect\xC3\xA9.", f, kMuted);
        p.text({info.x + 12.f, info.y + lh, info.w - 24.f, lh}, "Connecte-toi dans l'onglet Connexion pour voir ton compte.", small, kDim);
    } else {
        const auto* g = groupOf(project, me->group);
        const float g0 = std::min(26.f, lh * 0.9f);
        drawHmiGlyph(p.r, HmiGlyph::User, {info.x + 10.f, info.y + (lh - g0) / 2.f + 2.f, g0, g0}, p.th.color.accent);
        std::string head = me->login + (me->fullName.empty() ? std::string{} : "  \xC2\xB7  " + me->fullName);
        if (g) head += "  \xC2\xB7  " + g->name + ", niveau " + std::to_string(g->level);
        p.text({info.x + g0 + 20.f, info.y, info.w - g0 - 30.f, lh}, head, f, kText);
        std::string perms;
        if (project)
            for (const auto& perm : hmi::permissionNames())
                if (hmi::userHas(*project, *me, perm)) perms += (perms.empty() ? "" : ", ") + perm;
        std::string roles;
        if (g) for (const auto& r : g->roles) roles += (roles.empty() ? "" : ", ") + r;
        p.text({info.x + g0 + 20.f, info.y + lh, info.w - g0 - 30.f, lh},
               "R\xC3\xB4les : " + (roles.empty() ? std::string("aucun") : roles) + "   \xE2\x80\x94   permissions : " + (perms.empty() ? std::string("aucune") : perms),
               small, kMuted);
        std::string session = "Connect\xC3\xA9 depuis " + clockOf(rt, rt.loggedInAt()) + " (" + durationText(rt.now() - rt.loggedInAt()) + ")";
        // Lot 13 : la peremption du mot de passe.
        if (project)
            if (const auto left = hmi::passwordDaysLeft(project->security, *me, hmi::dayOf(rt.dateStampOf(rt.now())))) {
                session += "   \xE2\x80\x94   mot de passe : " + (*left > 0 ? "expire dans " + std::to_string(*left) + " jour(s)"
                                                                              : std::string("expire aujourd'hui"));
            }
        const double left = rt.autoLogoutRemaining(rt.now());
        session += left >= 0 ? "   \xE2\x80\x94   d\xC3\xA9" "connexion automatique dans " + durationText(left)
                             : std::string("   \xE2\x80\x94   pas de d\xC3\xA9" "connexion automatique");
        p.text({info.x + g0 + 20.f, info.y + 2.f * lh, info.w - g0 - 30.f, lh}, session, small, kMuted);
    }
    const float labelX = static_cast<float>(l.body.x) + p.ox;
    const float labelW = p.at(l.oldField).x - labelX - 12.f;
    struct Row { const hmi::Box* box; const char* key; const char* label; const char* hint; };
    // Lot 13 : ce que demande la politique des mots de passe du projet.
    const std::string rules = project ? hmi::passwordRules(project->security, hmi::kMenuPasswordMin) : std::string("6 caract\xC3\xA8res au moins");
    const Row rows[] = {{&l.oldField, "ancien", "Ancien mot de passe", "celui d'aujourd'hui"},
                        {&l.newField, "nouveau", "Nouveau", rules.c_str()},
                        {&l.confirmField, "confirmation", "Confirmation", "le m\xC3\xAAme"}};
    for (const auto& row : rows) {
        const gfx::Rect box = p.at(*row.box);
        p.text({labelX, box.y, labelW, box.h}, row.label, f, classic ? kLabel : kDim);
        const auto it = form.text.find(row.key);
        const auto ct = form.caret.find(row.key);
        const std::string typed = it == form.text.end() ? std::string{} : it->second;
        secretField(p, box, typed, ct == form.caret.end() ? typed.size() : ct->second, form.focus == row.key, classic,
                    classic ? row.hint : (me ? "ce compte n'a pas de mot de passe" : ""), f);
    }
    const gfx::Rect ch = p.at(l.change);
    p.button(ch, p.th.color.accent, classic);
    const float g = std::min(16.f, ch.h * 0.55f);
    drawHmiGlyph(p.r, HmiGlyph::Password, {ch.x + 10.f, ch.y + (ch.h - g) / 2.f, g, g}, faded(gfx::Color::rgb(0xFFFFFF), classic));
    p.text({ch.x + g + 16.f, ch.y, ch.w - g - 22.f, ch.h}, "Changer le mot de passe", f, faded(gfx::Color::rgb(0xFFFFFF), classic), 1);
}

// ---- Comptes ------------------------------------------------------------------------------
void paintAccounts(const Painter& p, const hmi::Runtime& rt, const hmi::LoginMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto* project = rt.project();
    if (!project) return;
    const auto& users = project->security.users;
    const auto& form = rt.loginForm();
    const bool admin = rt.permitted("Administrer");
    const gfx::Rect head = p.at(l.header);
    // Les colonnes, en fractions de la largeur.
    const float fr[] = {0.f, 0.19f, 0.43f, 0.66f, 0.76f, 0.9f, 1.f};
    const char* titles[] = {"Identifiant", "Nom complet", "Groupe", "Niveau", "Protection", "\xC3\x89tat"};
    p.r.fillRect(head, gfx::Color::rgb(0x273142));
    for (int c = 0; c < 6; ++c)
        p.text({head.x + head.w * fr[c] + 8.f, head.y, head.w * (fr[c + 1] - fr[c]) - 12.f, head.h}, titles[c], small, kMuted);
    const std::size_t first = rt.loginScroll();
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        const std::size_t k = first + i;
        const gfx::Rect row = p.at(l.rows[i]);
        if (k >= users.size()) break;
        const auto& u = users[k];
        const bool selected = form.selected == u.id;
        const bool self = !rt.userLogin().empty() && u.login == rt.userLogin();
        if (selected) p.r.fillRect(row, p.th.color.accent.withAlpha(70));
        else if (i % 2 == 1) p.r.fillRect(row, gfx::Color{255, 255, 255, 8});
        const auto* g = groupOf(project, u.group);
        const gfx::Color tc = u.enabled ? kText : kDim;
        std::string login = u.login + (self ? "  (toi)" : "");
        p.text({row.x + head.w * fr[0] + 8.f, row.y, head.w * (fr[1] - fr[0]) - 12.f, row.h}, login, f, self ? kGreen : tc);
        p.text({row.x + head.w * fr[1] + 8.f, row.y, head.w * (fr[2] - fr[1]) - 12.f, row.h}, u.fullName, f, tc);
        p.text({row.x + head.w * fr[2] + 8.f, row.y, head.w * (fr[3] - fr[2]) - 12.f, row.h}, g ? g->name : std::string("aucun"), f, tc);
        p.text({row.x + head.w * fr[3] + 8.f, row.y, head.w * (fr[4] - fr[3]) - 12.f, row.h}, g ? std::to_string(g->level) : std::string("0"), f, tc);
        p.text({row.x + head.w * fr[4] + 8.f, row.y, head.w * (fr[5] - fr[4]) - 12.f, row.h}, u.protection, small, kMuted);
        const bool locked = rt.accountLocked(u.login);          // lot 13
        p.text({row.x + head.w * fr[5] + 8.f, row.y, head.w * (fr[6] - fr[5]) - 12.f, row.h},
               locked ? "verrouill\xC3\xA9" : u.enabled ? "actif" : "d\xC3\xA9sactiv\xC3\xA9", small, locked ? kRed : u.enabled ? kGreen : kOrange);
    }
    if (users.empty() && !l.rows.empty())
        p.text(p.at(l.rows.front()).inset(8.f, 0.f), "Aucun compte : Ajouter en cr\xC3\xA9" "e un.", f, kDim);
    if (l.up.w > 0) {
        const std::size_t maxScroll = loginMaxScroll(rt, l);
        const gfx::Rect up = p.at(l.up), down = p.at(l.down);
        p.button(up, kButton, first > 0);
        p.button(down, kButton, first < maxScroll);
        p.arrow(up, 0, faded(kText, first > 0));
        p.arrow(down, 1, faded(kText, first < maxScroll));
    }
    const hmi::User* sel = form.selected != hmi::kNoId ? project->user(form.selected) : nullptr;
    for (const auto& b : l.buttons) {
        const gfx::Rect box = p.at(b.box);
        const bool needsRow = b.key != "ajouter";
        const bool on = admin && (!needsRow || sel);
        std::string label = b.key == "ajouter" ? "Ajouter" : b.key == "motdepasse" ? "Mot de passe"
                          : b.key == "activer" ? (sel && rt.accountLocked(sel->login) ? "D\xC3\xA9verrouiller"       // lot 13
                                                  : sel && sel->enabled ? "D\xC3\xA9sactiver" : "Activer")
                          : b.key == "groupe:precedent" ? "\xE2\x97\x80 Groupe" : b.key == "groupe:suivant" ? "Groupe \xE2\x96\xB6" : "Supprimer";
        const gfx::Color fill = b.key == "ajouter" ? gfx::Color::rgb(0x2A4A7A) : b.key == "supprimer" ? gfx::Color::rgb(0x5A2E2E) : kButton;
        p.button(box, fill, on);
        gfx::Rect in = box.inset(4.f, 0.f);
        if (!admin) {
            p.lock(in, 14.f);
            in.x += 16.f;
            in.w -= 16.f;
        }
        p.text(in, label, small, faded(kText, on), 1);
    }
    (void)small;
}

// ---- Acces ---------------------------------------------------------------------------------
void paintAccess(const Painter& p, const hmi::Runtime& rt, const hmi::LoginMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto* project = rt.project();
    if (!project) return;
    const auto& sec = project->security;
    const bool admin = rt.permitted("Administrer");
    const gfx::Rect head = p.at(l.header);
    p.r.fillRect(head, gfx::Color::rgb(0x273142));
    const float groupW = l.roleColumns.empty() ? head.w : p.at(l.roleColumns.front()).x - head.x;
    p.text({head.x + 8.f, head.y, groupW - 12.f, head.h}, "Groupe (niveau)", small, kMuted);
    for (std::size_t r = 0; r < l.roleColumns.size() && r < sec.roles.size(); ++r)
        p.text(p.at(l.roleColumns[r]).inset(4.f, 0.f), sec.roles[r].name, small, kMuted, 1);
    const hmi::User* me = rt.user();
    for (std::size_t gi = 0; gi < l.groupRows.size() && gi < sec.groups.size(); ++gi) {
        const auto& g = sec.groups[gi];
        const gfx::Rect row = p.at(l.groupRows[gi]);
        if (gi % 2 == 1) p.r.fillRect(row, gfx::Color{255, 255, 255, 8});
        const bool mine = me && me->group == g.id;
        p.text({row.x + 8.f, row.y, groupW - 12.f, row.h}, g.name + " (" + std::to_string(g.level) + ")" + (mine ? "  \xE2\x80\xA2" : ""), f,
               mine ? kGreen : kText);
        for (std::size_t r = 0; r < l.roleColumns.size() && r < sec.roles.size(); ++r) {
            const gfx::Rect col = p.at(l.roleColumns[r]);
            const bool has = std::any_of(g.roles.begin(), g.roles.end(), [&](const std::string& n) { return n == sec.roles[r].name; });
            const float s = std::min(18.f, row.h * 0.62f);
            const gfx::Rect box{col.x + (col.w - s) / 2.f, row.y + (row.h - s) / 2.f, s, s};
            p.r.fillRoundedRect(box, has ? faded(p.th.color.accent, admin) : faded(kField, admin), 3.f);
            p.r.strokeRect(box, faded(has ? p.th.color.accent : kButtonEdge, admin), 1.f);
            if (has) {
                p.r.line({box.x + s * 0.22f, box.y + s * 0.52f}, {box.x + s * 0.42f, box.y + s * 0.74f}, faded(gfx::Color::rgb(0xFFFFFF), admin), 2.f);
                p.r.line({box.x + s * 0.42f, box.y + s * 0.74f}, {box.x + s * 0.8f, box.y + s * 0.28f}, faded(gfx::Color::rgb(0xFFFFFF), admin), 2.f);
            }
        }
    }
    // La deconnexion automatique (celle du projet ; le poste peut l'imposer).
    const gfx::Rect row = p.at(l.logoutRow);
    const gfx::Rect minus = p.at(l.logoutMinus), plus = p.at(l.logoutPlus);
    gfx::Rect label{row.x + 8.f, row.y, minus.x - row.x - 16.f, row.h};
    if (!admin) {
        p.lock(label, 16.f);
        label.x += 20.f;
        label.w -= 20.f;
    }
    p.text(label, "D\xC3\xA9" "connexion automatique", f, admin ? kLabel : kDim);
    p.button(minus, kButton, admin);
    p.button(plus, kButton, admin);
    p.plusMinus(minus, false, faded(kText, admin));
    p.plusMinus(plus, true, faded(kText, admin));
    const gfx::Rect value{minus.right() + 6.f, row.y + 2.f, plus.x - minus.right() - 12.f, row.h - 4.f};
    p.r.fillRoundedRect(value, faded(kField, admin), 3.f);
    p.r.strokeRect(value, faded(kButtonEdge, admin), 1.f);
    p.text(value, sec.autoLogoutMin <= 0 ? std::string("jamais") : std::to_string(sec.autoLogoutMin) + " min", f, faded(kText, admin), 1);
    const int poste = rt.settings().autoLogoutMin;
    std::string note = poste >= 0 ? "le poste impose " + (poste == 0 ? std::string("jamais") : std::to_string(poste) + " min") + " (Param\xC3\xA8tres syst\xC3\xA8me)"
                                  : std::string("pour tous les postes (le projet)");
    p.text({plus.right() + 14.f, row.y, row.right() - plus.right() - 20.f, row.h}, note, small, poste >= 0 ? kOrange : kMuted);
    // Les onglets du menu, selon le niveau.
    const gfx::Rect levels{row.x + 8.f, row.bottom() + 6.f, row.w - 16.f, row.h};
    p.text(levels,
           "Onglets de ce menu : Comptes d\xC3\xA8s le niveau " + std::to_string(sec.menuLevelAccounts) + ", Acc\xC3\xA8s d\xC3\xA8s le niveau "
               + std::to_string(sec.menuLevelAccess) + ", Journal d\xC3\xA8s le niveau " + std::to_string(sec.menuLevelJournal)
               + " (Administrer : tous).",
           small, kMuted);
}

// ---- Journal --------------------------------------------------------------------------------
void paintJournal(const Painter& p, const hmi::Runtime& rt, const hmi::LoginMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto lines = rt.loginJournal();
    const gfx::Rect head = p.at(l.header);
    // Le compte (ou le groupe) vise, et qui etait connecte quand c'est arrive.
    const float fr[] = {0.f, 0.17f, 0.35f, 0.5f, 0.62f, 1.f};
    const char* titles[] = {"Heure", "\xC3\x89v\xC3\xA9nement", "Compte / groupe", "Par", "D\xC3\xA9tail"};
    p.r.fillRect(head, gfx::Color::rgb(0x273142));
    for (int c = 0; c < 5; ++c)
        p.text({head.x + head.w * fr[c] + 8.f, head.y, head.w * (fr[c + 1] - fr[c]) - 12.f, head.h}, titles[c], small, kMuted);
    const std::size_t first = rt.loginScroll();
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        const std::size_t k = first + i;
        if (k >= lines.size()) break;
        const auto& e = lines[k];
        const gfx::Rect row = p.at(l.rows[i]);
        if (i % 2 == 1) p.r.fillRect(row, gfx::Color{255, 255, 255, 8});
        gfx::Color kc = kText;
        if (e.kind == "Connexion") kc = kGreen;
        else if (e.kind.find("refus") != std::string::npos) kc = kRed;
        else if (e.kind.find("modifi") != std::string::npos || e.kind.find("chang") != std::string::npos) kc = kBlue;
        else kc = kMuted;
        const std::string stamp = e.stamp.size() >= 19 ? e.stamp.substr(5, 14) : e.stamp;    // 09-24 14:05:12
        // "(menu de connexion)" : le menu le dit deja.
        std::string detail = e.message;
        if (const auto at = detail.find(" (menu de connexion)"); at != std::string::npos) detail.erase(at, 20);
        p.text({row.x + head.w * fr[0] + 8.f, row.y, head.w * (fr[1] - fr[0]) - 12.f, row.h}, stamp, small, kMuted);
        p.text({row.x + head.w * fr[1] + 8.f, row.y, head.w * (fr[2] - fr[1]) - 12.f, row.h}, e.kind, f, kc);
        p.text({row.x + head.w * fr[2] + 8.f, row.y, head.w * (fr[3] - fr[2]) - 12.f, row.h}, e.source, f, kText);
        p.text({row.x + head.w * fr[3] + 8.f, row.y, head.w * (fr[4] - fr[3]) - 12.f, row.h}, e.user.empty() ? std::string("\xE2\x80\x94") : e.user,
               small, kMuted);
        p.text({row.x + head.w * fr[4] + 8.f, row.y, head.w * (fr[5] - fr[4]) - 12.f, row.h}, detail, small, kMuted);
    }
    if (lines.empty() && !l.rows.empty())
        p.text(p.at(l.rows.front()).inset(8.f, 0.f), "Aucune connexion depuis le lancement.", f, kDim);
    if (l.up.w > 0) {
        const std::size_t maxScroll = loginMaxScroll(rt, l);
        const gfx::Rect up = p.at(l.up), down = p.at(l.down);
        p.button(up, kButton, first > 0);
        p.button(down, kButton, first < maxScroll);
        p.arrow(up, 0, faded(kText, first > 0));
        p.arrow(down, 1, faded(kText, first < maxScroll));
    }
}

} // namespace

hmi::LoginMenuLayout loginMenuLayoutFor(const hmi::Runtime& rt, float w, float areaH) {
    const auto tabs = rt.loginTabs();
    const auto* project = rt.project();
    const std::size_t groups = project ? project->security.groups.size() : 0, roles = project ? project->security.roles.size() : 0;
    return hmi::loginMenuLayout(w, areaH, tabs, rt.loginTab(), rt.loginRows(), groups, roles, rt.renewalPending());
}

std::size_t loginMaxScroll(const hmi::Runtime& rt, const hmi::LoginMenuLayout& l) {
    const std::size_t rows = rt.loginRows(), visible = l.rows.size();
    return rows > visible ? rows - visible : 0;
}

hmi::LoginMenuLayout paintLoginMenu(gfx::IRenderer& r, const ui::Theme& th, const hmi::Runtime& rt, const gfx::Rect& screen, float areaH) {
    const auto l = loginMenuLayoutFor(rt, screen.w, std::min(screen.h, areaH));
    const hmi::LoginTab tab = rt.loginTab();
    const auto* project = rt.project();
    const Painter p{r, th, screen.x, screen.y};
    const gfx::FontId f = fontOf(l.fontSize), small = fontOf(l.fontSize * 0.86), title = fontOf(l.fontSize * 1.22);
    r.pushClip(screen);
    r.fillRect(screen, gfx::Color{0, 0, 0, 125});
    const gfx::Rect panel = p.at(l.panel);
    r.fillRect({panel.x + 6.f, panel.y + 8.f, panel.w, panel.h}, gfx::Color{0, 0, 0, 100});
    r.fillRoundedRect(panel, gfx::Color::rgb(0x1B222C), 6.f);
    // La barre de titre : la silhouette, le titre, qui est connecte, la croix.
    const gfx::Rect bar = p.at(l.title);
    r.fillRect(bar, gfx::Color::rgb(0x273142));
    const float g = std::min(22.f, bar.h * 0.56f);
    drawHmiGlyph(r, HmiGlyph::User, {bar.x + 12.f, bar.y + (bar.h - g) / 2.f, g, g}, th.color.accent);
    const char* heading = "Connexion et comptes";
    const float titleW = r.measure(heading, title).width;
    p.text({bar.x + g + 22.f, bar.y, bar.w * 0.6f, bar.h}, heading, title, kText);
    const gfx::Rect close = p.at(l.close);
    {
        std::string who = rt.userLogin().empty() ? std::string("personne n'est connect\xC3\xA9")
                                                 : "connect\xC3\xA9 : " + rt.userLogin() + " (niveau " + std::to_string(rt.level()) + ")";
        const double left = rt.autoLogoutRemaining(rt.now());
        if (left >= 0) who += " \xC2\xB7 d\xC3\xA9" "connexion dans " + durationText(left);
        const float lft = bar.x + g + 34.f + titleW;
        p.text({lft, bar.y, std::max(0.f, close.x - lft - 10.f), bar.h}, who, small, kMuted, 2);
        const float m = close.h * 0.34f;
        r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, kLabel, 1.8f);
        r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, kLabel, 1.8f);
    }
    // Les onglets : ceux que l'utilisateur connecte voit.
    float tabsBottom = bar.bottom() + 4.f;
    for (const auto& [t, box] : l.tabs) {
        const gfx::Rect tb = p.at(box);
        tabsBottom = tb.bottom();
        const bool on = t == tab;
        if (on) r.fillRoundedRect(tb, gfx::Color::rgb(0x2C3646), 4.f);
        if (on) r.fillRect({tb.x + 4.f, tb.bottom() - 3.f, tb.w - 8.f, 3.f}, th.color.accent);
        const float gs = std::min(16.f, tb.h * 0.55f);
        drawHmiGlyph(r, tabGlyph(t), {tb.x + 8.f, tb.y + (tb.h - gs) / 2.f, gs, gs}, on ? th.color.accent : kMuted);
        p.text({tb.x + gs + 14.f, tb.y, tb.w - gs - 18.f, tb.h}, hmi::loginTabLabel(t), f, on ? kText : kMuted);
    }
    r.line({panel.x + 8.f, tabsBottom + 1.f}, {panel.right() - 8.f, tabsBottom + 1.f}, gfx::Color::rgb(0x2E3643), 1.f);
    switch (tab) {
        case hmi::LoginTab::Connexion: paintConnexion(p, rt, l, f, small); break;
        case hmi::LoginTab::Compte:    paintAccount(p, rt, l, f, small); break;
        case hmi::LoginTab::Comptes:   paintAccounts(p, rt, l, f, small); break;
        case hmi::LoginTab::Acces:     paintAccess(p, rt, l, f, small); break;
        case hmi::LoginTab::Journal:   paintJournal(p, rt, l, f, small); break;
    }
    // Le message de l'onglet (une reponse, un refus), sinon un conseil.
    const auto& form = rt.loginForm();
    const gfx::Rect status = p.at(l.status);
    r.fillRect(status, gfx::Color::rgb(0x151A22));
    // Connexion et Mon compte montrent leur message dans le corps : la ligne
    // d'etat garde le conseil.
    const bool inBody = tab == hmi::LoginTab::Connexion || tab == hmi::LoginTab::Compte;
    std::string msg = inBody ? std::string{} : form.message;
    gfx::Color mc = form.error ? kRed : kGreen;
    if (msg.empty()) {
        mc = gfx::Color::rgb(0x7F8A9A);
        switch (tab) {
            case hmi::LoginTab::Connexion:
                msg = rt.renewalPending() ? std::string("Le nouveau mot de passe, deux fois ; Entr\xC3\xA9" "e. Annuler : personne n'est connect\xC3\xA9.")
                    : project && project->security.badgeLogin
                        ? std::string("Choisis ton compte aux fl\xC3\xA8" "ches et tape ton mot de passe, ou passe ton badge.")
                        : std::string("Choisis ton compte aux fl\xC3\xA8" "ches, tape ton mot de passe, Entr\xC3\xA9" "e. Les onglets suivent ton niveau.");
                break;
            case hmi::LoginTab::Compte: msg = "Le mot de passe est gard\xC3\xA9 sous forme d'empreinte sal\xC3\xA9" "e, jamais en clair."; break;
            case hmi::LoginTab::Comptes:
                msg = rt.permitted("Administrer") ? "Touche une ligne, puis un bouton. Chaque changement va dans le projet (Ctrl+Z le reprend)."
                                                  : "En lecture : changer un compte demande la permission Administrer.";
                break;
            case hmi::LoginTab::Acces:
                msg = rt.permitted("Administrer") ? "Une case : le groupe re\xC3\xA7oit ou perd le r\xC3\xB4le. Tu ne peux pas te retirer Administrer."
                                                  : "En lecture : changer les acc\xC3\xA8s demande la permission Administrer.";
                break;
            case hmi::LoginTab::Journal: msg = "Les connexions, les refus et les comptes chang\xC3\xA9s, les plus r\xC3\xA9" "cents d'abord."; break;
        }
    }
    p.text(status.inset(12.f, 0.f), msg, small, mc);
    // Le message de l'onglet se lit aussi dans le corps (la ligne "message").
    if (!form.message.empty() && l.message.w > 0 && (tab == hmi::LoginTab::Connexion || tab == hmi::LoginTab::Compte)) {
        const gfx::Rect m = p.at(l.message);
        const float gs = std::min(16.f, m.h * 0.6f);
        p.dot(m.x + gs / 2.f + 2.f, m.y + m.h / 2.f, gs * 0.35f, form.error ? kRed : kGreen);
        p.text({m.x + gs + 8.f, m.y, m.w - gs - 8.f, m.h}, form.message, f, form.error ? kRed : kGreen);
    }
    r.strokeRect(panel, th.color.accent, 1.5f);
    r.popClip();
    return l;
}

} // namespace app
