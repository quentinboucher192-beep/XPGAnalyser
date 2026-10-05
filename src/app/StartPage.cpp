// =============================================================================
//  app/StartPage.cpp - l'accueil, repense (lot API 7)
// -----------------------------------------------------------------------------
//  Tout se dessine ici, a la main, avec les couleurs du theme du moment ; les
//  seuls widgets "standard" sont ceux qui apportent le clavier : les boutons,
//  les deux champs (chercher, coller un chemin) et la liste du tri.
// =============================================================================
#include "StartPage.hpp"

#include "Brand.hpp"
#include "../project/ProjectIcon.hpp"

#include <algorithm>
#include <cmath>

namespace app::start {

namespace {

// Les tailles de texte de l'accueil. Le theme n'a pas de face grasse : le gras
// est simule, le texte trace deux fois a 0,6 pixel (comme la barre du haut).
const gfx::FontId kTiny{11};
const gfx::FontId kSmall{12};
const gfx::FontId kBody{13};
const gfx::FontId kLabel{14};
const gfx::FontId kName{16};
const gfx::FontId kDetailName{19};
const gfx::FontId kHeading{20};
const gfx::FontId kBrand{23};

const char* const kEllipsis = "\xE2\x80\xA6";

constexpr float kCardH = 116.f;   // une carte de projet
constexpr float kGap = 14.f;      // entre deux cartes
constexpr float kPadX = 26.f;     // les marges de la grille
constexpr float kPadTop = 4.f;
constexpr float kPadBottom = 20.f;
constexpr float kMessageH = 70.f; // le message au-dessus de la carte "Nouveau projet"

// ---------------------------------------------------------------- couleurs ----
// Deux couleurs melangees : t = 0 donne a, t = 1 donne b (color-mix de la maquette).
gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
    const auto channel = [t](std::uint8_t x, std::uint8_t y) {
        const float v = static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t;
        return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
    };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}

// La couleur d'un etat : celle de la pastille de la barre du haut (TopBar) -
// DEV l'accent, FINISH le vert, LOCK le rouge, NEW et export le gris.
gfx::Color badgeColor(const ui::Theme& th, const std::string& badge) {
    const auto& c = th.color;
    if (badge == "LOCK" || badge == "introuvable") return c.error;
    if (badge == "DEV") return c.accent;
    if (badge == "FINISH") return c.ok;
    return c.textMuted;
}

// ------------------------------------------------------------------ texte ----
void drawBold(gfx::IRenderer& r, gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color c) {
    r.drawText(at, s, f, c);
    r.drawText({at.x + 0.6f, at.y}, s, f, c);
}

// Le texte a la largeur, termine par ... s'il ne tient pas.
std::string elide(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float width) {
    if (width <= 0.f) return {};
    if (r.measure(s, f).width <= width) return s;
    const float room = width - r.measure(kEllipsis, f).width;
    if (room <= 0.f) return {};
    std::string out = s.substr(0, std::min(r.fitCharacters(s, f, room), s.size()));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + kEllipsis;
}

// Un chemin trop long garde son debut ET SA FIN, qui est le dossier du projet :
// "D:\Affaires\...\Armoire_Gaz" dit plus que "D:\Affaires\2026\Arm...".
std::string elideMiddle(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float width) {
    if (width <= 0.f) return {};
    if (r.measure(s, f).width <= width) return s;
    const float room = width - r.measure(kEllipsis, f).width;
    if (room <= 0.f) return {};
    std::vector<std::size_t> starts;            // le debut de chaque caractere
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) starts.push_back(i);
    // La plus longue fin qui tient dans 60 % de la place (la mesure croit avec
    // la longueur : une dichotomie suffit).
    std::size_t lo = 0, hi = starts.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (r.measure(std::string_view(s).substr(starts[mid]), f).width <= room * 0.6f) hi = mid;
        else lo = mid + 1;
    }
    const std::string tail = lo < starts.size() ? s.substr(starts[lo]) : std::string{};
    const float headRoom = room - r.measure(tail, f).width;
    return s.substr(0, std::min(r.fitCharacters(s, f, headRoom), s.size())) + kEllipsis + tail;
}

// Le texte coupe aux espaces pour tenir dans `width`, `maxLines` lignes au plus ;
// la derniere finit par ... si le reste ne tient pas. Mesure par les services de
// la plateforme : sert dans onLayout comme dans onPaint.
std::vector<std::string> wrap(std::string_view text, gfx::FontId f, float width, std::size_t maxLines) {
    std::vector<std::string> lines;
    if (text.empty() || width <= 0.f || maxLines == 0) return lines;
    std::string line;
    bool more = false;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = text.find(' ', i);
        if (j == std::string_view::npos) j = text.size();
        std::string word(text.substr(i, j - i));
        i = j + 1;
        if (word.empty()) continue;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(candidate, f) > width) {
            lines.push_back(std::move(line));
            line = std::move(word);
            if (lines.size() == maxLines) { more = true; break; }
        } else {
            line = std::move(candidate);
        }
    }
    if (!more && !line.empty()) lines.push_back(std::move(line));
    if (more && !lines.empty()) {
        std::string& last = lines.back();
        while (!last.empty() && ui::measureWidth(last + kEllipsis, f) > width) {
            std::size_t cut = last.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(last[cut]) & 0xC0) == 0x80) --cut;
            last.erase(cut);
        }
        while (!last.empty() && last.back() == ' ') last.pop_back();
        last += kEllipsis;
    }
    return lines;
}

// Une pastille d'etat, centree sur `cy` ; rend sa largeur.
float drawPill(const ui::PaintContext& ctx, float x, float cy, const std::string& text, gfx::Color tone) {
    if (text.empty()) return 0.f;
    // Au pixel pres : un cadre arrondi pose a une abscisse fractionnaire
    // dedouble son bord droit.
    const float w = std::round(ctx.r.measure(text, kTiny).width + 11.f);
    const float h = std::round(ctx.r.lineHeight(kTiny) + 3.f);
    const gfx::Rect pill{std::round(x), std::round(cy - h * 0.5f), w, h};
    ctx.r.fillRoundedRect(pill, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 3.f);
    drawBold(ctx.r, {pill.x + 5.f, pill.y + (pill.h - ctx.r.lineHeight(kTiny)) * 0.5f}, text, kTiny, ctx.theme.onSurface(tone));
    return w;
}

// La largeur ou le libelle d'un bouton tient encore entier (8 points de marge de
// chaque cote au lieu de 14) : deux boutons cote a cote, ou l'un sous l'autre.
float tightWidth(const StartButton& b) { return b.naturalWidth() - 12.f; }

// ------------------------------------------------------------- les dessins ----
// Ce que ui::Icon n'a pas, trace sur une grille de 16 comme ses icones.
struct Pen {
    gfx::IRenderer& r;
    gfx::Rect       box;
    gfx::Color      colour;
    float           thick;
    [[nodiscard]] gfx::Point at(float x, float y) const { return {box.x + x * box.w / 16.f, box.y + y * box.h / 16.f}; }
    void stroke(float x0, float y0, float x1, float y1) const { r.line(at(x0, y0), at(x1, y1), colour, thick); }
};

void ring(gfx::IRenderer& r, gfx::Point centre, float radius, gfx::Color c, float thick) {
    constexpr int kSteps = 20;
    gfx::Point prev{centre.x + radius, centre.y};
    for (int k = 1; k <= kSteps; ++k) {
        const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(kSteps);
        const gfx::Point next{centre.x + radius * std::cos(a), centre.y + radius * std::sin(a)};
        r.line(prev, next, c, thick);
        prev = next;
    }
}

void chevronRight(gfx::IRenderer& r, float x, float cy, gfx::Color c) {
    r.line({x - 2.f, cy - 4.f}, {x + 2.f, cy}, c, 1.5f);
    r.line({x + 2.f, cy}, {x - 2.f, cy + 4.f}, c, 1.5f);
}

// Une fleche qui descend dans un bac : deposer.
void dropGlyph(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const Pen p{r, b, c, 1.5f};
    p.stroke(8.f, 1.5f, 8.f, 10.f);
    p.stroke(4.5f, 6.8f, 8.f, 10.2f);
    p.stroke(8.f, 10.2f, 11.5f, 6.8f);
    p.stroke(2.f, 9.5f, 2.f, 14.f);
    p.stroke(2.f, 14.f, 14.f, 14.f);
    p.stroke(14.f, 14.f, 14.f, 9.5f);
}

void drawGlyph(gfx::IRenderer& r, StartButton::Glyph g, const gfx::Rect& area, gfx::Color c) {
    using G = StartButton::Glyph;
    const float side = std::min(area.w, area.h);
    const gfx::Rect b{area.x + (area.w - side) * 0.5f, area.y + (area.h - side) * 0.5f, side, side};
    const Pen p{r, b, c, std::max(1.3f, side / 10.f)};
    switch (g) {
        case G::None: break;
        case G::Plus:
            p.stroke(8.f, 2.5f, 8.f, 13.5f);
            p.stroke(2.5f, 8.f, 13.5f, 8.f);
            break;
        case G::Open: ui::drawIcon(r, ui::Icon::Open, b, c); break;
        case G::Tutorial:           // une toque de diplome : le didacticiel
            p.stroke(8.f, 3.f, 15.f, 6.5f);
            p.stroke(15.f, 6.5f, 8.f, 10.f);
            p.stroke(8.f, 10.f, 1.f, 6.5f);
            p.stroke(1.f, 6.5f, 8.f, 3.f);
            p.stroke(4.f, 8.3f, 4.f, 11.5f);
            p.stroke(12.f, 8.3f, 12.f, 11.5f);
            p.stroke(4.f, 11.5f, 8.f, 13.5f);
            p.stroke(8.f, 13.5f, 12.f, 11.5f);
            p.stroke(15.f, 6.5f, 15.f, 11.f);
            break;
        case G::Help: {             // un point d'interrogation dans un cercle
            ring(r, p.at(8.f, 8.f), side * 0.43f, c, 1.3f);
            const gfx::FontId f = side >= 15.f ? kSmall : kTiny;
            const float tw = r.measure("?", f).width;
            drawBold(r, {b.x + (b.w - tw) * 0.5f, b.y + (b.h - r.lineHeight(f)) * 0.5f}, "?", f, c);
            break;
        }
        case G::Settings: ui::drawIcon(r, ui::Icon::Settings, b, c); break;
        case G::Station: ui::drawIcon(r, ui::Icon::Station, b, c); break;
        case G::Copy: {             // deux feuilles decalees
            const float s9 = side * 9.f / 16.f;
            r.strokeRect({p.at(5.f, 2.f).x, p.at(5.f, 2.f).y, s9, s9}, c, 1.2f);
            r.strokeRect({p.at(2.f, 5.f).x, p.at(2.f, 5.f).y, s9, s9}, c, 1.2f);
            break;
        }
        case G::Rename:             // un crayon
            p.stroke(3.f, 11.f, 11.f, 3.f);
            p.stroke(5.f, 13.f, 13.f, 5.f);
            p.stroke(11.f, 3.f, 13.f, 5.f);
            p.stroke(3.f, 11.f, 2.5f, 13.5f);
            p.stroke(2.5f, 13.5f, 5.f, 13.f);
            break;
        case G::Trash:              // une corbeille
            p.stroke(2.5f, 4.f, 13.5f, 4.f);
            p.stroke(6.5f, 4.f, 6.5f, 2.3f);
            p.stroke(6.5f, 2.3f, 9.5f, 2.3f);
            p.stroke(9.5f, 2.3f, 9.5f, 4.f);
            p.stroke(4.f, 4.f, 5.f, 14.f);
            p.stroke(5.f, 14.f, 11.f, 14.f);
            p.stroke(11.f, 14.f, 12.f, 4.f);
            p.stroke(8.f, 6.5f, 8.f, 11.5f);
            break;
        case G::Remove: ui::drawIcon(r, ui::Icon::Close, b, c); break;
    }
}

// Un cadre en pointilles aux coins arrondis (la zone ou deposer, la carte
// "Nouveau projet") : le renderer ne sait tracer que des segments.
void dashedFrame(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c, float radius) {
    constexpr float kDash = 5.f, kSpace = 4.f;
    const float x0 = b.x + 0.5f, y0 = b.y + 0.5f, x1 = b.right() - 0.5f, y1 = b.bottom() - 0.5f;
    const auto across = [&](float from, float to, float at) {
        for (float x = from; x < to; x += kDash + kSpace) r.line({x, at}, {std::min(x + kDash, to), at}, c, 1.f);
    };
    const auto down = [&](float from, float to, float at) {
        for (float y = from; y < to; y += kDash + kSpace) r.line({at, y}, {at, std::min(y + kDash, to)}, c, 1.f);
    };
    across(x0 + radius, x1 - radius, y0);
    across(x0 + radius, x1 - radius, y1);
    down(y0 + radius, y1 - radius, x0);
    down(y0 + radius, y1 - radius, x1);
    // Les coins : un quart de cercle en trois traits (y vers le bas : 180 -> 270
    // degres va de la gauche vers le haut).
    const auto corner = [&](float cx, float cy, float a0) {
        gfx::Point prev{cx + radius * std::cos(a0), cy + radius * std::sin(a0)};
        for (int k = 1; k <= 3; ++k) {
            const float a = a0 + 1.5707963f * static_cast<float>(k) / 3.f;
            const gfx::Point next{cx + radius * std::cos(a), cy + radius * std::sin(a)};
            r.line(prev, next, c, 1.f);
            prev = next;
        }
    };
    corner(x0 + radius, y0 + radius, 3.1415927f);
    corner(x1 - radius, y0 + radius, 4.7123890f);
    corner(x1 - radius, y1 - radius, 0.f);
    corner(x0 + radius, y1 - radius, 1.5707963f);
}

// --------------------------------------------------- l'icone d'un projet ----
// Les textures des icones de projet, pour tout le programme. L'ecran garde les
// pixels d'une icone tant que config/icone.txt ne change pas ; la texture suit :
// elle est rendue au renderer des que plus personne ne tient ses pixels. Sans
// ce cache commun, chaque retour a l'accueil (un nouvel ecran) en creait douze
// de plus, jamais rendues.
struct IconTexture {
    const gfx::IRenderer*                          owner{nullptr};
    std::weak_ptr<const std::vector<std::uint8_t>> source;
    gfx::TextureId                                 tex{};
};

std::vector<IconTexture>& iconTextures() {
    static std::vector<IconTexture> cache;
    return cache;
}

gfx::TextureId iconTexture(gfx::IRenderer& r, const std::shared_ptr<const std::vector<std::uint8_t>>& px) {
    auto& cache = iconTextures();
    for (auto it = cache.begin(); it != cache.end();) {
        if (it->source.expired()) {
            if (it->owner == &r && it->tex.v != 0) r.releaseImage(it->tex);
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& t : cache)
        if (t.owner == &r && t.source.lock() == px) return t.tex;
    const auto tex = r.createImage(px->data(), kIconPixels, kIconPixels);
    if (tex.v != 0) cache.push_back(IconTexture{&r, px, tex});
    return tex;
}

// L'image d'un projet dans sa tuile : son icone (config/icone.txt), une feuille
// pour un export, un triangle pour un chemin disparu, sinon ses initiales.
void drawPicture(const ui::PaintContext& ctx, const ProjectEntry& e, const gfx::Rect& tile, float side) {
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const float radius = std::round(tile.w * 0.2f);
    ctx.r.fillRoundedRect(tile, c.border, radius);
    ctx.r.fillRoundedRect(tile.inset(1.f, 1.f), c.headerBg, radius - 1.f);
    const gfx::Rect in{std::round(tile.x + (tile.w - side) * 0.5f), std::round(tile.y + (tile.h - side) * 0.5f), side, side};
    if (e.icon && e.icon->size() == static_cast<std::size_t>(kIconPixels) * kIconPixels * 4u) {
        if (const auto tex = iconTexture(ctx.r, e.icon); tex.v != 0) {
            ctx.r.drawImage(tex, in);
            return;
        }
        // Un renderer sans images (les essais) : un point sur quatre.
        const auto& px = *e.icon;
        constexpr int kStep = 8;
        const float cell = side * static_cast<float>(kStep) / static_cast<float>(kIconPixels);
        for (int y = 0; y < kIconPixels; y += kStep)
            for (int x = 0; x < kIconPixels; x += kStep) {
                const auto at = static_cast<std::size_t>((y * kIconPixels + x) * 4);
                if (px[at + 3] == 0) continue;
                ctx.r.fillRect({in.x + static_cast<float>(x / kStep) * cell, in.y + static_cast<float>(y / kStep) * cell, cell, cell},
                               gfx::Color{px[at], px[at + 1], px[at + 2], px[at + 3]});
            }
        return;
    }
    if (e.missing) {
        ui::drawIcon(ctx.r, ui::Icon::Warning, in.inset(side * 0.22f, side * 0.22f), th.onSurface(c.warning));
        return;
    }
    if (!e.isFolder) {
        ui::drawIcon(ctx.r, ui::Icon::Document, in.inset(side * 0.22f, side * 0.22f), c.textMuted);
        return;
    }
    // Pas d'icone : les initiales du projet, sur une teinte tiree de son nom
    // (toujours la meme pour le meme nom).
    std::uint32_t h = 2166136261u;
    for (const char ch : e.name) h = (h ^ static_cast<std::uint8_t>(ch)) * 16777619u;
    const auto tone = th.brand.family[h % 6u];
    const gfx::Rect sq = in.inset(side * 0.06f, side * 0.06f);
    ctx.r.fillRoundedRect(sq, tone.withAlpha(th.isDark() ? 110 : 80), std::round(sq.w * 0.18f));
    const std::string initials = project::icon::initialsOf(e.name);
    const gfx::FontId f{static_cast<std::uint16_t>(std::clamp(std::lround(sq.h * 0.42f), 11L, 30L))};
    const float tw = ctx.r.measure(initials, f).width;
    drawBold(ctx.r, {sq.x + (sq.w - tw) * 0.5f, sq.y + (sq.h - ctx.r.lineHeight(f)) * 0.5f}, initials, f, th.onSurface(tone));
}

// ---------------------------------------------------- la barre d'etat ----
// Un texte a droite de la barre d'etat : les raccourcis, le nom du programme.
// `shown` faux : il ne prend plus de place (une fenetre etroite garde la place
// au message de la barre).
class StatusText final : public ui::Widget {
public:
    StatusText(std::string id, std::string text) : ui::Widget(std::move(id)), text_(std::move(text)) {}
    void setText(std::string t) {
        text_ = std::move(t);
        invalidateLayout();
    }
    void setShown(bool shown) {
        if (shown == shown_) return;
        shown_ = shown;
        invalidateLayout();
    }
    [[nodiscard]] float textWidth() const { return text_.empty() ? 0.f : ui::measureWidth(text_, kStatus) + 4.f; }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {shown_ ? textWidth() : 0.f, 20.f};
        h.minimum = h.preferred;
        return h;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        if (!shown_) return;
        const auto b = bounds();
        ctx.r.drawText({b.x, b.y + (b.h - ctx.r.lineHeight(kStatus)) * 0.5f}, text_, kStatus, ctx.theme.color.textMuted);
    }
private:
    // La taille du message de la barre (Theme::font.smallUi) : une ligne, une taille.
    static constexpr gfx::FontId kStatus{13};
    std::string text_;
    bool        shown_{true};
};

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

// L'infobulle d'une carte : le chemin entier, ce que fait l'ouvrir, et ce que
// la carte ne montre qu'en abrege quand elle est etroite.
std::string cardTip(const ProjectEntry& e) {
    std::string tip = e.path;
    if (e.missing)
        tip += "\nIntrouvable : d\xC3\xA9plac\xC3\xA9, renomm\xC3\xA9, ou sur un lecteur absent (Suppr le retire de la liste)";
    else if (e.isFolder)
        tip += "\nDouble clic ou Entr\xC3\xA9" "e : l'ouvrir";
    else
        tip += "\nDouble clic ou Entr\xC3\xA9" "e : le lire, puis en faire un projet";
    if (e.open)
        tip += e.modified ? "\nOuvert en ce moment, avec des modifications non enregistr\xC3\xA9" "es" : "\nOuvert en ce moment";
    if (e.autostart) tip += "\nLe PC le lance \xC3\xA0 son d\xC3\xA9marrage, en poste d'exploitation";
    return tip;
}

} // namespace

// =============================================================== les regles ====
const char* filterLabel(Filter f) noexcept {
    switch (f) {
        case Filter::All: return "Tous";
        case Filter::Projects: return "Projets";
        case Filter::Exports: return "Exports";
        case Filter::Finish: return "FINISH";
        case Filter::Lock: return "LOCK";
    }
    return "Tous";
}

bool passes(const ProjectEntry& e, Filter f) noexcept {
    switch (f) {
        case Filter::All: return true;
        case Filter::Projects: return e.isFolder;
        case Filter::Exports: return !e.isFolder && !e.missing;
        case Filter::Finish: return e.isFolder && e.state == project::State::Finish;
        case Filter::Lock: return e.isFolder && e.state == project::State::Lock;
    }
    return true;
}

std::string foldText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto ch = static_cast<unsigned char>(s[i]);
        if (ch == 0xC3 && i + 1 < s.size()) {
            // Le Latin-1 accentue : les majuscules (80..9E) comme les minuscules.
            auto d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0x80 && d <= 0x9E) d = static_cast<unsigned char>(d + 0x20);
            char base = 0;
            if (d >= 0xA0 && d <= 0xA5) base = 'a';
            else if (d == 0xA7) base = 'c';
            else if (d >= 0xA8 && d <= 0xAB) base = 'e';
            else if (d >= 0xAC && d <= 0xAF) base = 'i';
            else if (d >= 0xB2 && d <= 0xB6) base = 'o';
            else if (d >= 0xB9 && d <= 0xBC) base = 'u';
            if (base != 0) {
                out += base;
                ++i;
                continue;
            }
        }
        out += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : static_cast<char>(ch);
    }
    return out;
}

bool matches(const ProjectEntry& e, const std::string& query) {
    const std::string q = foldText(query);
    if (q.find_first_not_of(' ') == std::string::npos) return true;
    std::string compact = e.cpu;          // "BMXP342020" trouve "BMX P34 2020"
    compact.erase(std::remove(compact.begin(), compact.end(), ' '), compact.end());
    const std::string hay = foldText(e.name + " " + e.cpu + " " + compact + " " + e.path + " " + e.badge);
    std::size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && q[i] == ' ') ++i;
        std::size_t j = q.find(' ', i);
        if (j == std::string::npos) j = q.size();
        if (j > i && hay.find(q.substr(i, j - i)) == std::string::npos) return false;
        i = j;
    }
    return true;
}

// ============================================================= StartButton ====
StartButton::StartButton(std::string text, std::string id, Look look, Glyph glyph)
    : ui::Button(std::move(text), std::move(id)), look_(look), glyph_(glyph) {}

void StartButton::setSubtitle(std::string s) {
    subtitle_ = std::move(s);
    invalidate();
}

void StartButton::setHint(std::string s) {
    hint_ = std::move(s);
    invalidate();
}

void StartButton::setGlyph(Glyph g) {
    glyph_ = g;
    invalidate();
}

void StartButton::setLook(Look l) {
    if (look_ == l) return;
    look_ = l;
    invalidateLayout();
}

float StartButton::naturalWidth() const {
    const gfx::FontId f = look_ == Look::Ghost ? kSmall : kLabel;
    float w = ui::measureWidth(text(), f) + 28.f;
    if (glyph_ != Glyph::None) w += (look_ == Look::Ghost ? 12.f : 15.f) + 7.f;
    if (!hint_.empty()) w += ui::measureWidth(hint_, kTiny) + 8.f;
    return std::ceil(w);
}

ui::SizeHint StartButton::sizeHint() const {
    ui::SizeHint h;
    float height = 30.f;
    switch (look_) {
        case Look::CardPrimary:
        case Look::Card: height = 66.f; break;
        case Look::Link:
        case Look::Primary: height = 34.f; break;
        case Look::Ghost: height = 26.f; break;
        case Look::Secondary:
        case Look::Danger: break;
    }
    h.preferred = {naturalWidth(), height};
    h.minimum = {std::min(naturalWidth(), 60.f), height};
    return h;
}

ui::EventResult StartButton::onEvent(const ui::InputEvent& ev) {
    const bool hadFocus = focused();
    // L'appui, pour le dessin : celui de ui::Button est prive.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        down_ = true;
        invalidate();
    } else if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && u->button == ui::MouseButton::Left && down_) {
        down_ = false;
        invalidate();
    }
    const auto result = ui::Button::onEvent(ev);
    // UN CLIC NE LAISSE PAS LE FOCUS DU CLAVIER sur le bouton. ui::Button le
    // prend a l'appui ; ici, Entree veut dire "ouvrir le projet choisi" (l'ecran
    // la prend quand aucun widget ne la garde) : apres un dialogue ouvert d'un
    // clic sur "Renommer...", Entree le rouvrait, et l'anneau du focus restait
    // dessine autour du bouton. Tab y mene toujours, pour qui vit au clavier.
    if (!hadFocus && result == ui::EventResult::Consumed && std::holds_alternative<ui::MouseDown>(ev)) releaseFocus();
    return result;
}

void StartButton::onPaint(const ui::PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto b = bounds();
    const bool on = enabled();
    const bool hot = on && hovered();
    const bool press = hot && down_;
    const std::string& label = text();
    // Le focus du clavier : un anneau autour, dessine avant le bouton.
    const auto focusRing = [&](float radius) {
        if (focused() && on) ctx.r.fillRoundedRect({b.x - 2.f, b.y - 2.f, b.w + 4.f, b.h + 4.f}, th.brand.focusRing, radius + 2.f);
    };

    if (look_ == Look::CardPrimary || look_ == Look::Card) {
        const bool primary = look_ == Look::CardPrimary;
        focusRing(10.f);
        const gfx::Color fill = primary ? (press ? c.accentPressed : hot ? c.accentHover : c.accent)
                                        : (press ? mix(th.brand.card, c.text, 0.08f) : hot ? mix(th.brand.card, c.text, 0.04f) : th.brand.card);
        const gfx::Color edge = primary ? fill : (hot ? c.borderStrong : th.brand.cardBorder);
        ctx.r.fillRoundedRect(b, edge, 10.f);
        ctx.r.fillRoundedRect(b.inset(1.f, 1.f), fill, 9.f);
        const gfx::Color ink = primary ? c.textInverted : c.text;
        const gfx::Color soft = primary ? ink.withAlpha(215) : c.textMuted;
        const gfx::Rect tile{b.x + 14.f, b.y + std::round((b.h - 38.f) * 0.5f), 38.f, 38.f};
        ctx.r.fillRoundedRect(tile, primary ? ink.withAlpha(40) : c.accent.withAlpha(th.isDark() ? 50 : 34), 9.f);
        drawGlyph(ctx.r, glyph_, tile.inset(10.f, 10.f), primary ? ink : th.onSurface(c.accent));
        const float x = tile.right() + 14.f;
        float right = b.right() - 14.f;
        if (!hint_.empty()) {
            const float hw = ctx.r.measure(hint_, kTiny).width + 12.f;
            // Le raccourci s'efface dans une colonne etroite (1280 x 720), pour
            // les deux grandes actions a la fois, ou quand il prendrait la place
            // du titre : l'infobulle le dit aussi.
            if (b.w >= 300.f && right - hw - 10.f - x >= ctx.r.measure(label, kName).width + 1.f) {
                const gfx::Rect key{std::round(right - hw), b.y + std::round((b.h - 20.f) * 0.5f), std::round(hw), 20.f};
                ctx.r.fillRoundedRect(key, primary ? ink.withAlpha(120) : c.border, 4.f);
                ctx.r.fillRoundedRect(key.inset(1.f, 1.f), fill, 3.f);
                ctx.r.drawText({key.x + 6.f, key.y + (key.h - ctx.r.lineHeight(kTiny)) * 0.5f}, hint_, kTiny, soft);
                right = key.x - 10.f;
            }
        }
        const float lineT = ctx.r.lineHeight(kName), lineS = ctx.r.lineHeight(kSmall);
        const auto sub = wrap(subtitle_, kSmall, right - x, 2);
        const float blockH = lineT + 1.f + lineS * static_cast<float>(sub.size());
        float y = b.y + std::round((b.h - blockH) * 0.5f);
        drawBold(ctx.r, {x, y}, elide(ctx.r, label, kName, right - x), kName, ink);
        y += lineT + 1.f;
        for (const auto& l : sub) {
            ctx.r.drawText({x, y}, l, kSmall, soft);
            y += lineS;
        }
        return;
    }

    if (look_ == Look::Link) {
        focusRing(6.f);
        if (hot) ctx.r.fillRoundedRect(b, c.text.withAlpha(th.isDark() ? 20 : 14), 6.f);
        const gfx::Rect g{b.x + 8.f, b.y + std::round((b.h - 16.f) * 0.5f), 16.f, 16.f};
        drawGlyph(ctx.r, glyph_, g, on ? c.textMuted : c.textDisabled);
        float right = b.right() - 10.f;
        if (hint_ == ">") {
            chevronRight(ctx.r, right - 3.f, b.y + b.h * 0.5f, c.textMuted);
            right -= 16.f;
        } else if (!hint_.empty()) {
            const float hw = ctx.r.measure(hint_, kSmall).width;
            ctx.r.drawText({right - hw, b.y + (b.h - ctx.r.lineHeight(kSmall)) * 0.5f}, hint_, kSmall, c.textMuted);
            right -= hw + 10.f;
        }
        const float x = g.right() + 12.f;
        const std::string shown = elide(ctx.r, label, kLabel, right - x);
        ctx.r.drawText({x, b.y + (b.h - ctx.r.lineHeight(kLabel)) * 0.5f}, shown, kLabel, on ? c.text : c.textDisabled);
        if (!subtitle_.empty() && shown == label) {
            // Tout entier ou pas du tout : "le didact..." ne dit rien.
            const float sx = x + ctx.r.measure(shown, kLabel).width + 6.f;
            const std::string sub = "\xC2\xB7 " + subtitle_;
            if (ctx.r.measure(sub, kSmall).width <= right - sx)
                ctx.r.drawText({sx, b.y + (b.h - ctx.r.lineHeight(kSmall)) * 0.5f + 1.f}, sub, kSmall, c.textMuted);
        }
        return;
    }

    // Les boutons : plein (Primary), encadre (Secondary, Ghost), rouge (Danger).
    const bool primary = look_ == Look::Primary;
    const bool danger = look_ == Look::Danger;
    const bool ghost = look_ == Look::Ghost;
    const float radius = ghost ? 5.f : 6.f;
    focusRing(radius);
    gfx::Color fill = th.brand.card, edge = c.border, ink = c.text;
    gfx::Color tint{0, 0, 0, 0};
    if (!on) {
        fill = c.panelBg;
        edge = c.border;
        ink = c.textDisabled;
    } else if (primary) {
        fill = press ? c.accentPressed : hot ? c.accentHover : c.accent;
        edge = fill;
        ink = c.textInverted;
    } else if (danger) {
        const auto red = th.onSurface(c.error);
        fill = c.panelBg;
        edge = red;
        ink = red;
        if (hot) tint = c.error.withAlpha(press ? 64 : 34);
    } else {
        fill = press ? mix(th.brand.card, c.text, 0.10f) : hot ? mix(th.brand.card, c.text, 0.05f) : th.brand.card;
        edge = hot ? c.borderStrong : c.border;
    }
    ctx.r.fillRoundedRect(b, edge, radius);
    ctx.r.fillRoundedRect(b.inset(1.f, 1.f), fill, radius - 1.f);
    if (tint.a > 0) ctx.r.fillRoundedRect(b.inset(1.f, 1.f), tint, radius - 1.f);
    const gfx::FontId f = ghost ? kSmall : kLabel;
    const float gs = glyph_ == Glyph::None ? 0.f : (ghost ? 12.f : 15.f);
    const float lw = ctx.r.measure(label, f).width;
    const float hw = hint_.empty() ? 0.f : ctx.r.measure(hint_, kTiny).width + 8.f;
    const float total = gs + (gs > 0.f ? 7.f : 0.f) + lw + hw;
    float x = b.x + std::max(8.f, std::round((b.w - total) * 0.5f));
    if (gs > 0.f) {
        drawGlyph(ctx.r, glyph_, {x, b.y + std::round((b.h - gs) * 0.5f), gs, gs}, ink);
        x += gs + 7.f;
    }
    const float avail = b.right() - 8.f - x - hw;
    const std::string shown = elide(ctx.r, label, f, avail);
    ctx.r.drawText({x, b.y + (b.h - ctx.r.lineHeight(f)) * 0.5f}, shown, f, ink);
    if (!hint_.empty())
        ctx.r.drawText({x + ctx.r.measure(shown, f).width + 8.f, b.y + (b.h - ctx.r.lineHeight(kTiny)) * 0.5f + 1.f}, hint_, kTiny,
                       primary && on ? ink.withAlpha(200) : c.textMuted);
}

// ============================================================== StartField ====
StartField::StartField(std::string id, ui::Icon icon) : ui::InputText(std::move(id)), icon_(icon) {
    setPadding({3.f, 8.f, 3.f, icon == ui::Icon::None ? 8.f : 30.f});
}

void StartField::onPaint(const ui::PaintContext& ctx) {
    ui::InputText::onPaint(ctx);
    if (icon_ == ui::Icon::None) return;
    const auto b = bounds();
    ui::drawIcon(ctx.r, icon_, {b.x + 9.f, b.y + std::round((b.h - 14.f) * 0.5f), 14.f, 14.f},
                 focused() ? ctx.theme.color.accent : ctx.theme.color.textMuted);
}

// ============================================================ ThemeSwatches ====
ThemeSwatches::ThemeSwatches(std::string id) : ui::Widget(std::move(id)) {
    // Chaque pastille avec les couleurs de SON theme, lues une fois.
    // ---- Lot API 8 : themes ----
    // Quarante-trois themes ne tiennent pas sur une ligne : les neuf premiers
    // (ceux du lot API 6), puis quatre des tiens ; la galerie (Affichage >
    // Theme...) a tous les autres.
    std::size_t builtIn = 0, mine = 0;
    for (const auto& e : ui::Theme::all()) {
        if (e.user ? ++mine > 4 : ++builtIn > 9) continue;
        const auto t = ui::Theme::byName(e.key);
        swatches_.push_back({e.key, e.label, e.description, t.color.windowBg, t.color.panelBg, t.color.accent, t.color.text});
    }
    // ---- fin Lot API 8 ----
}

const std::string& ThemeSwatches::keyAt(std::size_t i) const {
    static const std::string kNone;
    return i < swatches_.size() ? swatches_[i].key : kNone;
}

gfx::Rect ThemeSwatches::swatchRect(std::size_t i) const {
    const auto b = bounds();
    const auto n = static_cast<float>(std::max<std::size_t>(1, swatches_.size()));
    constexpr float kSpace = 6.f;
    const float w = std::max(8.f, (b.w - 4.f - kSpace * (n - 1.f)) / n);
    return {std::round(b.x + 2.f + static_cast<float>(i) * (w + kSpace)), b.y + 2.f, std::round(w), std::max(0.f, b.h - 4.f)};
}

int ThemeSwatches::at(gfx::Point p) const {
    for (std::size_t i = 0; i < swatches_.size(); ++i)
        if (swatchRect(i).contains(p)) return static_cast<int>(i);
    return -1;
}

void ThemeSwatches::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    if (!hovered()) hover_ = -1;
    for (std::size_t i = 0; i < swatches_.size(); ++i) {
        const auto& s = swatches_[i];
        const auto r = swatchRect(i);
        const bool current = ctx.theme.name == s.key;
        const bool hot = hover_ == static_cast<int>(i);
        if (current) ctx.r.fillRoundedRect({r.x - 2.f, r.y - 2.f, r.w + 4.f, r.h + 4.f}, c.accent, 8.f);
        ctx.r.fillRoundedRect(r, current ? c.accent : hot ? c.borderStrong : c.border, 6.f);
        const gfx::Rect in = r.inset(1.f, 1.f);
        ctx.r.fillRoundedRect(in, s.window, 5.f);
        // un trait de texte et un panneau : on devine le theme avant de le choisir
        ctx.r.fillRect({in.x + 5.f, in.y + 6.f, std::max(4.f, in.w * 0.42f), 2.f}, s.text.withAlpha(170));
        ctx.r.fillRect({in.x + 5.f, in.y + 11.f, std::max(4.f, in.w * 0.28f), 2.f}, s.text.withAlpha(90));
        const gfx::Rect strip{in.x, in.bottom() - 8.f, in.w, 8.f};
        ctx.r.fillRoundedRect(strip, s.accent, 4.f);
        ctx.r.fillRect({strip.x, strip.y, strip.w, 4.f}, s.accent);
    }
}

ui::EventResult ThemeSwatches::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = at(m->pos);
        if (h != hover_) {
            hover_ = h;
            if (h >= 0) {
                const auto& s = swatches_[static_cast<std::size_t>(h)];
                setTooltip("Th\xC3\xA8me " + s.label + " : " + s.description);
            } else {
                setTooltip({});
            }
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    // Un choix, c'est l'appui ET le relacher sur la meme pastille : le relacher
    // seul est celui d'un dialogue qui vient de se fermer au-dessus.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        pressed_ = at(d->pos);
        return pressed_ >= 0 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && u->button == ui::MouseButton::Left) {
        const int h = at(u->pos);
        const bool click = h >= 0 && h == pressed_;
        pressed_ = -1;
        if (click) {
            const std::string key = swatches_[static_cast<std::size_t>(h)].key;   // une copie : le signal peut tout changer
            chosen->emit(key);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ============================================================== FilterChips ====
FilterChips::FilterChips(std::string id) : ui::Widget(std::move(id)) {}

void FilterChips::setCount(Filter f, std::size_t n) {
    const auto i = static_cast<std::size_t>(f);
    if (i >= kFilterCount || counts_[i] == n) return;
    counts_[i] = n;
    invalidateLayout();
}

void FilterChips::setCurrent(Filter f) {
    if (current_ == f) return;
    current_ = f;
    invalidate();
}

std::string FilterChips::text(std::size_t i) const { return filterLabel(static_cast<Filter>(i)); }

float FilterChips::naturalWidth() const {
    float w = 0.f;
    for (std::size_t i = 0; i < kFilterCount; ++i)
        w += ui::measureWidth(text(i), kBody) + 6.f + ui::measureWidth(std::to_string(counts_[i]), kBody) + 22.f + (i ? 6.f : 0.f);
    return std::ceil(w);
}

gfx::Rect FilterChips::chipRect(Filter f) const {
    const auto b = bounds();
    float x = b.x;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const float w = ui::measureWidth(text(i), kBody) + 6.f + ui::measureWidth(std::to_string(counts_[i]), kBody) + 22.f;
        if (i == static_cast<std::size_t>(f)) return {std::round(x), b.y, std::round(w), b.h};
        x += w + 6.f;
    }
    return {};
}

int FilterChips::at(gfx::Point p) const {
    for (std::size_t i = 0; i < kFilterCount; ++i)
        if (chipRect(static_cast<Filter>(i)).contains(p)) return static_cast<int>(i);
    return -1;
}

void FilterChips::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    if (!hovered()) hover_ = -1;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const auto r = chipRect(static_cast<Filter>(i));
        const bool on = static_cast<std::size_t>(current_) == i;
        const bool hot = hover_ == static_cast<int>(i);
        const float radius = std::floor(r.h * 0.5f);
        ctx.r.fillRoundedRect(r, on ? c.accent : hot ? c.borderStrong : c.border, radius);
        ctx.r.fillRoundedRect(r.inset(1.f, 1.f), on ? c.selectionBg : hot ? mix(c.windowBg, c.text, 0.05f) : c.windowBg, radius - 1.f);
        const std::string label = text(i), count = std::to_string(counts_[i]);
        const float ty = r.y + (r.h - ctx.r.lineHeight(kBody)) * 0.5f;
        ctx.r.drawText({r.x + 11.f, ty}, label, kBody, on ? c.selectionText : c.textMuted);
        ctx.r.drawText({r.x + 11.f + ctx.r.measure(label, kBody).width + 6.f, ty}, count, kBody, on ? c.selectionText : c.text);
    }
}

ui::EventResult FilterChips::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = at(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        pressed_ = at(d->pos);
        return pressed_ >= 0 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && u->button == ui::MouseButton::Left) {
        const int h = at(u->pos);
        const bool click = h >= 0 && h == pressed_;
        pressed_ = -1;
        if (click) {
            chosen->emit(h);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ============================================================== ProjectGrid ====
ProjectGrid::ProjectGrid(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

void ProjectGrid::setItems(std::vector<ProjectEntry> items, int selected) {
    items_ = std::move(items);
    selected_ = selected >= 0 && selected < count() ? selected : -1;
    hover_ = pressed_ = lastDown_ = -1;
    measure();
    clampScroll();
    if (selected_ >= 0) reveal(selected_);
    invalidateLayout();
}

void ProjectGrid::setEmptyMessage(std::string title, std::string text) {
    if (title == emptyTitle_ && text == emptyText_) return;
    emptyTitle_ = std::move(title);
    emptyText_ = std::move(text);
    invalidateLayout();
}

void ProjectGrid::setSelected(int index, bool revealIt) {
    const int next = index >= 0 && index < count() ? index : -1;
    if (next == selected_) return;
    selected_ = next;
    if (revealIt && selected_ >= 0) reveal(selected_);
    invalidate();
}

bool ProjectGrid::moveSelection(int delta) {
    if (items_.empty()) return false;
    const int next = selected_ < 0 ? 0 : std::clamp(selected_ + delta, 0, count() - 1);
    if (next == selected_) return false;
    setSelected(next);
    selectionChanged->emit(next);
    return true;
}

bool ProjectGrid::pageSelection(int pages) {
    const int rows = std::max(1, static_cast<int>(std::floor(bounds().h / (kCardH + kGap))));
    return moveSelection(pages * rows * std::max(1, columns_));
}

bool ProjectGrid::selectEdge(bool last) {
    if (items_.empty()) return false;
    const int target = last ? count() - 1 : 0;
    if (target == selected_) return false;
    setSelected(target);
    selectionChanged->emit(target);
    return true;
}

float ProjectGrid::top() const {
    return kPadTop + (items_.empty() && !emptyTitle_.empty() ? kMessageH : 0.f);
}

void ProjectGrid::measure() {
    const auto b = bounds();
    const float inner = std::max(0.f, b.w - 2.f * kPadX);
    columns_ = inner >= 640.f ? 2 : 1;
    cardW_ = columns_ == 2 ? std::floor((inner - kGap) * 0.5f) : inner;
    const int cells = count() + 1;                         // + "Nouveau projet"
    const int rows = (cells + columns_ - 1) / columns_;
    contentH_ = top() + static_cast<float>(rows) * kCardH + static_cast<float>(std::max(0, rows - 1)) * kGap + kPadBottom;
}

void ProjectGrid::clampScroll() {
    const float most = std::max(0.f, contentH_ - bounds().h);
    scroll_ = std::clamp(scroll_, 0.f, most);
}

gfx::Rect ProjectGrid::cardRect(int index) const {
    if (index < 0 || index > count()) return {};
    const auto b = bounds();
    const int row = index / std::max(1, columns_), col = index % std::max(1, columns_);
    return {b.x + kPadX + static_cast<float>(col) * (cardW_ + kGap),
            std::round(b.y + top() + static_cast<float>(row) * (kCardH + kGap) - scroll_), cardW_, kCardH};
}

void ProjectGrid::reveal(int index) {
    const auto b = bounds();
    if (b.h <= 0.f || index < 0) return;
    const auto r = cardRect(index);
    if (r.y < b.y + 4.f) scroll_ -= (b.y + 4.f) - r.y;
    else if (r.bottom() > b.bottom() - 4.f) scroll_ += r.bottom() - (b.bottom() - 4.f);
    clampScroll();
}

int ProjectGrid::cardAt(gfx::Point p) const {
    if (!bounds().contains(p)) return -1;
    for (int i = 0; i <= count(); ++i)
        if (cardRect(i).contains(p)) return i;
    return -1;
}

gfx::Rect ProjectGrid::trackRect() const {
    const auto b = bounds();
    return {b.right() - 10.f, b.y + 4.f, 6.f, std::max(0.f, b.h - 8.f)};
}

gfx::Rect ProjectGrid::thumbRect() const {
    const auto t = trackRect();
    if (contentH_ <= bounds().h || contentH_ <= 0.f) return {};
    const float h = std::max(24.f, t.h * bounds().h / contentH_);
    const float most = std::max(1.f, contentH_ - bounds().h);
    return {t.x, t.y + (t.h - h) * (scroll_ / most), t.w, h};
}

void ProjectGrid::onLayout() {
    measure();
    clampScroll();
    if (selected_ >= 0) reveal(selected_);
}

ui::EventResult ProjectGrid::onEvent(const ui::InputEvent& ev) {
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (contentH_ <= bounds().h) return ui::EventResult::Ignored;
        scroll_ -= w->dy * 64.f;
        clampScroll();
        hover_ = cardAt(w->pos);
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (dragging_) {
            const auto t = trackRect();
            const float h = thumbRect().h;
            const float along = (m->pos.y - dragGrab_ - t.y) / std::max(1.f, t.h - h);
            scroll_ = along * std::max(0.f, contentH_ - bounds().h);
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
        const int h = cardAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            if (h >= 0 && h < count()) setTooltip(cardTip(items_[static_cast<std::size_t>(h)]));
            else if (h == count()) setTooltip("Cr\xC3\xA9" "e un projet (Ctrl+N) ; un dossier ou un export gliss\xC3\xA9 dans la fen\xC3\xAAtre s'ouvre");
            else setTooltip({});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        dragging_ = false;
        if (const auto thumb = thumbRect(); thumb.h > 0.f && trackRect().contains(d->pos)) {
            dragging_ = true;
            dragGrab_ = thumb.contains(d->pos) ? d->pos.y - thumb.y : thumb.h * 0.5f;
            const auto t = trackRect();
            const float along = (d->pos.y - dragGrab_ - t.y) / std::max(1.f, t.h - thumb.h);
            scroll_ = along * std::max(0.f, contentH_ - bounds().h);
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
        const int hit = cardAt(d->pos);
        if (hit < 0) return ui::EventResult::Ignored;
        grabFocus();
        pressed_ = hit;
        if (hit < count()) {
            // Un double clic n'ouvre que si son premier clic est tombe ici, sur
            // la meme carte.
            const bool chained = d->clickCount >= 2 && lastDown_ == hit;
            lastDown_ = chained ? -1 : hit;
            if (selected_ != hit) {
                setSelected(hit, false);
                selectionChanged->emit(hit);
            }
            if (chained) activated->emit(hit);
        } else {
            lastDown_ = -1;
        }
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && u->button == ui::MouseButton::Left) {
        if (dragging_) {
            dragging_ = false;
            return ui::EventResult::Consumed;
        }
        const int hit = cardAt(u->pos);
        const bool click = hit >= 0 && hit == count() && hit == pressed_;
        pressed_ = -1;
        if (click) {
            newRequested->emit();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

void ProjectGrid::paintCard(const ui::PaintContext& ctx, const ProjectEntry& e, const gfx::Rect& r, bool hot, bool sel) const {
    const auto& th = ctx.theme;
    const auto& c = th.color;
    if (sel) ctx.r.fillRoundedRect({r.x - 3.f, r.y - 3.f, r.w + 6.f, r.h + 6.f}, c.accent.withAlpha(focused() ? 95 : 60), 13.f);
    ctx.r.fillRoundedRect(r, sel ? c.accent : hot ? c.borderStrong : th.brand.cardBorder, 10.f);
    ctx.r.fillRoundedRect(r.inset(1.f, 1.f), hot && !sel ? mix(th.brand.card, c.text, 0.03f) : th.brand.card, 9.f);

    const gfx::Rect tile{r.x + 14.f, r.y + 14.f, 56.f, 56.f};
    drawPicture(ctx, e, tile, 44.f);
    const float x0 = tile.right() + 14.f;
    const float right = r.right() - 14.f;

    // 1. le nom ; a droite, le poste lance au demarrage du PC. Le nom passe
    // d'abord : la marque se raccourcit ("au demarrage"), puis ne garde que son
    // triangle (l'infobulle de la carte le dit en entier).
    float nameRight = right;
    const float y1 = r.y + 12.f;
    float openW = 0.f;
    if (e.open) openW = ctx.r.measure("ouvert", kTiny).width + 12.f + (e.modified ? 14.f : 0.f) + 8.f;
    if (e.autostart) {
        const float room = right - x0 - openW - ctx.r.measure(e.name, kName).width - 16.f;
        std::string tag = "poste au d\xC3\xA9marrage du PC";
        if (ctx.r.measure(tag, kTiny).width + 13.f > room) tag = "au d\xC3\xA9marrage";
        if (ctx.r.measure(tag, kTiny).width + 13.f > room) tag.clear();
        const float tw = tag.empty() ? 0.f : ctx.r.measure(tag, kTiny).width;
        const float tx = right - tw;
        const auto green = th.onSurface(c.ok);
        ui::drawIcon(ctx.r, ui::Icon::Play, {tx - 13.f, y1 + 3.f, 9.f, 9.f}, green);
        if (!tag.empty()) ctx.r.drawText({tx, y1 + 2.f}, tag, kTiny, green);
        nameRight = tx - 13.f - 12.f;
    }
    const std::string name = elide(ctx.r, e.name, kName, nameRight - x0 - openW);
    drawBold(ctx.r, {x0, y1}, name, kName, c.text);
    if (e.open) {
        // Le projet charge en ce moment : l'ouvrir y revient, rien n'est relu.
        const float ox = std::round(x0 + ctx.r.measure(name, kName).width + 8.f);
        const float oh = std::round(ctx.r.lineHeight(kTiny) + 3.f);
        const gfx::Rect pill{ox, std::round(y1 + (ctx.r.lineHeight(kName) - oh) * 0.5f), std::round(ctx.r.measure("ouvert", kTiny).width + 12.f), oh};
        ctx.r.fillRoundedRect(pill, c.accent, 3.f);
        ctx.r.fillRoundedRect(pill.inset(1.f, 1.f), th.brand.card, 2.f);
        ctx.r.drawText({pill.x + 6.f, pill.y + (pill.h - ctx.r.lineHeight(kTiny)) * 0.5f}, "ouvert", kTiny, th.onSurface(c.accent));
        if (e.modified) ctx.r.fillRoundedRect({pill.right() + 6.f, pill.y + pill.h * 0.5f - 3.5f, 7.f, 7.f}, c.warning, 3.5f);
    }

    // 2. l'etat et la version
    const float cy2 = r.y + 47.f;
    float x = x0 + drawPill(ctx, x0, cy2, e.badge, badgeColor(th, e.badge)) + 8.f;
    const float ty2 = cy2 - ctx.r.lineHeight(kBody) * 0.5f;
    if (!e.versionTitle.empty()) {
        const std::string title = elide(ctx.r, e.versionTitle, kBody, right - x);
        drawBold(ctx.r, {x, ty2}, title, kBody, c.text);
        x += ctx.r.measure(title, kBody).width + 7.f;
    }
    if (!e.versionSubtitle.empty() && x < right)
        ctx.r.drawText({x, ty2}, elide(ctx.r, e.versionSubtitle, kBody, right - x), kBody, c.textMuted);

    // 3. l'automate et la date
    const float y3 = r.y + 64.f;
    x = x0;
    if (!e.cpu.empty()) {
        ui::drawIcon(ctx.r, ui::Icon::Cpu, {x, y3 + 2.f, 12.f, 12.f}, c.textMuted);
        const std::string cpu = elide(ctx.r, e.cpu, kSmall, (right - x) * 0.5f);
        ctx.r.drawText({x + 16.f, y3}, cpu, kSmall, c.textMuted);
        x += 16.f + ctx.r.measure(cpu, kSmall).width + 14.f;
    }
    if (!e.when.empty() && x + 30.f < right) {
        ui::drawIcon(ctx.r, ui::Icon::History, {x, y3 + 2.f, 12.f, 12.f}, c.textMuted);
        ctx.r.drawText({x + 16.f, y3}, elide(ctx.r, e.when, kSmall, right - x - 16.f), kSmall, c.textMuted);
    }

    // 4. le chemin, en discret (a la place de la ligne 3 quand elle est vide :
    // un chemin disparu n'a ni automate ni date)
    const float y4 = e.cpu.empty() && e.when.empty() ? y3 : r.y + 84.f;
    ctx.r.drawText({x0, y4}, elideMiddle(ctx.r, e.path, kSmall, right - x0), kSmall, mix(c.textMuted, th.brand.card, 0.28f));
}

void ProjectGrid::paintNewCard(const ui::PaintContext& ctx, const gfx::Rect& r, bool hot) const {
    const auto& c = ctx.theme.color;
    if (hot) ctx.r.fillRoundedRect(r, c.text.withAlpha(ctx.theme.isDark() ? 12 : 9), 10.f);
    dashedFrame(ctx.r, r, hot ? c.accent : c.borderStrong, 10.f);
    const std::string label = "Nouveau projet \xC2\xB7 ou glisse un dossier ici";
    const std::string shown = elide(ctx.r, label, kBody, r.w - 24.f);
    const float lh = ctx.r.lineHeight(kBody);
    const float blockH = 22.f + 8.f + lh;
    const float y = r.y + std::round((r.h - blockH) * 0.5f);
    drawGlyph(ctx.r, StartButton::Glyph::Plus, {r.x + (r.w - 22.f) * 0.5f, y, 22.f, 22.f}, hot ? ctx.theme.onSurface(c.accent) : c.textMuted);
    ctx.r.drawText({r.x + (r.w - ctx.r.measure(shown, kBody).width) * 0.5f, y + 30.f}, shown, kBody, hot ? c.text : c.textMuted);
}

void ProjectGrid::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    if (!hovered()) hover_ = -1;
    if (items_.empty() && !emptyTitle_.empty()) {
        const float x = b.x + kPadX;
        float y = std::round(b.y + kPadTop + 8.f - scroll_);
        drawBold(ctx.r, {x, y}, elide(ctx.r, emptyTitle_, kName, b.w - 2.f * kPadX), kName, c.text);
        y += ctx.r.lineHeight(kName) + 6.f;
        for (const auto& l : wrap(emptyText_, kBody, b.w - 2.f * kPadX, 2)) {
            ctx.r.drawText({x, y}, l, kBody, c.textMuted);
            y += ctx.r.lineHeight(kBody);
        }
    }
    for (int i = 0; i <= count(); ++i) {
        const auto r = cardRect(i);
        if (r.bottom() + 4.f < b.y || r.y - 4.f > b.bottom()) continue;
        if (i == count()) paintNewCard(ctx, r, hover_ == i);
        else paintCard(ctx, items_[static_cast<std::size_t>(i)], r, hover_ == i, selected_ == i);
    }
    if (const auto thumb = thumbRect(); thumb.h > 0.f)
        ctx.r.fillRoundedRect(thumb, dragging_ ? c.scrollbarHover : c.scrollbar, 3.f);
}

// ============================================================ ProjectDetail ====
ProjectDetail::ProjectDetail(std::string id) : ui::Widget(std::move(id)) {
    using L = StartButton::Look;
    using G = StartButton::Glyph;
    const auto make = [this](std::string text, std::string wid, L look, G glyph) {
        return &static_cast<StartButton&>(addChild(std::make_unique<StartButton>(std::move(text), std::move(wid), look, glyph)));
    };
    open_ = make("Ouvrir", "startup.openSelected", L::Primary, G::Open);
    open_->setTooltip("Ouvre le projet choisi (Entr\xC3\xA9" "e, ou un double clic sur sa carte)");
    station_ = make("Poste d'exploitation", "startup.station", L::Secondary, G::Station);
    duplicate_ = make("Dupliquer\xE2\x80\xA6", "startup.duplicate", L::Secondary, G::Copy);
    duplicate_->setTooltip("Copie le projet dans un autre dossier ; la copie revient en DEV, sans verrou et sans src/");
    rename_ = make("Renommer\xE2\x80\xA6", "startup.rename", L::Secondary, G::Rename);
    rename_->setHint("F2");
    remove_ = make("Supprimer\xE2\x80\xA6", "startup.delete", L::Danger, G::Trash);
    setEntry(nullptr);
}

void ProjectDetail::setEntry(const ProjectEntry* e) {
    if (e) entry_ = *e;
    else entry_.reset();
    using V = ui::Visibility;
    const bool any = e != nullptr;
    const bool folder = any && e->isFolder && !e->missing;
    const bool file = any && !e->isFolder && !e->missing;
    open_->setVisibility(folder || file ? V::Visible : V::Collapsed);
    station_->setVisibility(folder ? V::Visible : V::Collapsed);
    duplicate_->setVisibility(folder ? V::Visible : V::Collapsed);
    rename_->setVisibility(folder ? V::Visible : V::Collapsed);
    remove_->setVisibility(any ? V::Visible : V::Collapsed);
    if (folder) {
        const bool finish = e->state == project::State::Finish;
        station_->setEnabled(finish);
        station_->setTooltip(finish ? "Ouvre le projet directement en poste d'exploitation : l'IHM seule, en plein \xC3\xA9" "cran"
                                    : "Le poste d'exploitation n'ouvre qu'un projet FINISH (une version termin\xC3\xA9" "e)");
        const bool locked = e->state == project::State::Lock;
        rename_->setEnabled(!locked);
        rename_->setTooltip(locked ? "LOCK : personne ne modifie ce projet ; ouvre-le, puis Projet \xE2\x80\xBA D\xC3\xA9verrouiller"
                                   : "Change le nom du projet, pas celui de son dossier sur le disque (F2)");
        remove_->setText("Supprimer\xE2\x80\xA6");
        remove_->setGlyph(StartButton::Glyph::Trash);
        remove_->setLook(StartButton::Look::Danger);
        remove_->setTooltip("Efface le dossier du projet et tout ce qu'il contient, apr\xC3\xA8s confirmation (Suppr)");
        open_->setTooltip(e->open ? "Il est d\xC3\xA9j\xC3\xA0 ouvert : tu y reviens tel que tu l'as laiss\xC3\xA9, rien n'est relu du disque (Entr\xC3\xA9" "e)"
                                  : "Ouvre le projet (Entr\xC3\xA9" "e, ou un double clic sur sa carte)");
    } else if (any) {
        // Retirer de la liste n'efface rien : encadre, pas rouge.
        remove_->setText("Retirer de la liste");
        remove_->setGlyph(StartButton::Glyph::Remove);
        remove_->setLook(StartButton::Look::Secondary);
        remove_->setTooltip("Retire ce chemin des projets r\xC3\xA9" "cents ; rien n'est touch\xC3\xA9 sur le disque (Suppr)");
        open_->setTooltip("Lit l'export, puis propose d'en faire un projet (un dossier) (Entr\xC3\xA9" "e)");
    }
    invalidateLayout();
}

std::string ProjectDetail::note() const {
    if (!entry_) return {};
    const auto& e = *entry_;
    if (e.missing)
        return "Ce chemin n'existe plus : le dossier a \xC3\xA9t\xC3\xA9 d\xC3\xA9plac\xC3\xA9 ou renomm\xC3\xA9, ou le lecteur n'est pas branch\xC3\xA9. "
               "Retirer de la liste ne touche \xC3\xA0 rien sur le disque.";
    if (!e.isFolder)
        return "Un export n'est pas un projet : l'ouvrir le lit, puis propose d'en faire un projet (un dossier). "
               "Retirer de la liste ne touche jamais au fichier.";
    std::string s;
    if (e.state != project::State::Finish)
        s = "Le poste d'exploitation n'ouvre qu'un projet FINISH (une version termin\xC3\xA9" "e) ; celui-ci est "
            + std::string(project::toString(e.state)) + ".";
    // (L'en-tete dit deja "ouvert", et s'il est modifie.)
    if (e.open) s += std::string(s.empty() ? "" : " ") + "Ouvrir y revient tel que tu l'as laiss\xC3\xA9, sans rien relire du disque.";
    return s;
}

void ProjectDetail::onLayout() {
    const auto b = bounds();
    const float pad = b.w >= 320.f ? 20.f : 14.f;
    const float x0 = b.x + pad, w = std::max(40.f, b.w - 2.f * pad);
    headerBottom_ = b.y + 20.f + 76.f + 18.f;
    noteLines_.clear();
    if (!entry_) {
        noteY_ = actionsBottom_ = headerBottom_;
        return;
    }
    const auto& e = *entry_;
    const bool folder = e.isFolder && !e.missing;
    constexpr float kTall = 34.f, kShort = 30.f, kSpace = 8.f;
    const float half = std::floor((w - kSpace) * 0.5f);
    float y = headerBottom_ + 14.f;
    if (folder) {
        // Ouvrir | Poste d'exploitation, cote a cote quand leurs libelles y
        // tiennent entiers ; l'un sous l'autre dans une colonne etroite.
        if (std::max(tightWidth(*open_), tightWidth(*station_)) <= half) {
            open_->setBounds({x0, y, half, kTall});
            station_->setBounds({x0 + half + kSpace, y, w - half - kSpace, kTall});
        } else {
            open_->setBounds({x0, y, w, kTall});
            y += kTall + kSpace;
            station_->setBounds({x0, y, w, kTall});
        }
        y += kTall + 8.f;
    } else if (!e.missing) {
        open_->setBounds({x0, y, w, kTall});
        y += kTall + 8.f;
    }
    noteLines_ = wrap(note(), kSmall, w, 5);
    noteY_ = y;
    if (!noteLines_.empty()) y += ui::lineHeight(kSmall) * static_cast<float>(noteLines_.size()) + 10.f;
    if (folder) {
        rename_->setHint("F2");
        const float dw = duplicate_->naturalWidth(), sw = remove_->naturalWidth();
        float rw = rename_->naturalWidth();
        // Trop etroit pour "Renommer... F2" a cote de "Dupliquer..." : F2 s'efface
        // (l'infobulle le dit), avant que les deux boutons passent l'un sous l'autre.
        if (dw + rw + kSpace > w && tightWidth(*rename_) > half) {
            rename_->setHint({});
            rw = rename_->naturalWidth();
        }
        if (dw + rw + sw + 2.f * kSpace <= w) {
            // Tout sur une ligne, Supprimer a l'ecart, a droite.
            duplicate_->setBounds({x0, y, dw, kShort});
            rename_->setBounds({x0 + dw + kSpace, y, rw, kShort});
            remove_->setBounds({x0 + w - sw, y, sw, kShort});
            y += kShort;
        } else {
            if (dw + rw + kSpace <= w) {
                duplicate_->setBounds({x0, y, dw, kShort});
                rename_->setBounds({x0 + dw + kSpace, y, rw, kShort});
            } else if (std::max(tightWidth(*duplicate_), tightWidth(*rename_)) <= half) {
                duplicate_->setBounds({x0, y, half, kShort});
                rename_->setBounds({x0 + half + kSpace, y, w - half - kSpace, kShort});
            } else {
                duplicate_->setBounds({x0, y, w, kShort});
                y += kShort + kSpace;
                rename_->setBounds({x0, y, w, kShort});
            }
            y += kShort + kSpace;
            const float rw2 = std::min(w, sw);
            remove_->setBounds({x0 + w - rw2, y, rw2, kShort});
            y += kShort;
        }
    } else {
        remove_->setBounds({x0, y, std::min(w, remove_->naturalWidth()), kShort});
        y += kShort;
    }
    actionsBottom_ = y + 16.f;
}

void ProjectDetail::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& th = ctx.theme;
    const auto& c = th.color;
    ctx.r.fillRect(b, c.panelBg);
    ctx.r.fillRect({b.x, b.y, 1.f, b.h}, c.border);
    const float pad = b.w >= 320.f ? 20.f : 14.f;
    const float x0 = b.x + pad, w = std::max(40.f, b.w - 2.f * pad);

    if (!entry_) {
        // Rien de choisi (pas encore de projet, ou un filtre qui ne laisse rien) :
        // les premiers pas.
        float y = b.y + 26.f;
        drawBold(ctx.r, {x0, y}, "Premiers pas", kDetailName, c.text);
        y += ctx.r.lineHeight(kDetailName) + 14.f;
        struct Tip { StartButton::Glyph glyph; const char* title; const char* text; };
        static const Tip kTips[] = {
            {StartButton::Glyph::Plus, "Nouveau projet  (Ctrl+N)", "un dossier et la t\xC3\xA2" "che MAST, pr\xC3\xAAts \xC3\xA0 remplir"},
            {StartButton::Glyph::Open, "Ouvrir\xE2\x80\xA6  (Ctrl+O)", "un dossier de projet, ou un export .XPG, .XHW ou .XDB de Control Expert"},
            {StartButton::Glyph::None, "Glisser-d\xC3\xA9poser", "un dossier ou un export, n'importe o\xC3\xB9 dans cette fen\xC3\xAAtre"},
            {StartButton::Glyph::Tutorial, "D\xC3\xA9" "couvrir l'application", "le didacticiel, sur un projet ouvert ; F1 ouvre l'aide depuis n'importe quel \xC3\xA9" "cran"},
        };
        for (const auto& t : kTips) {
            const gfx::Rect g{x0, y + 1.f, 16.f, 16.f};
            if (t.glyph == StartButton::Glyph::None) dropGlyph(ctx.r, g, c.textMuted);
            else drawGlyph(ctx.r, t.glyph, g, c.textMuted);
            ctx.r.drawText({x0 + 26.f, y}, elide(ctx.r, t.title, kLabel, w - 26.f), kLabel, c.text);
            float ty = y + ctx.r.lineHeight(kLabel) + 2.f;
            for (const auto& l : wrap(t.text, kSmall, w - 26.f, 3)) {
                ctx.r.drawText({x0 + 26.f, ty}, l, kSmall, c.textMuted);
                ty += ctx.r.lineHeight(kSmall);
            }
            y = ty + 14.f;
        }
        return;
    }

    const auto& e = *entry_;
    // ---- l'en-tete : la grande image, le nom, l'etat et la version ----------
    const gfx::Rect tile{x0, b.y + 20.f, 76.f, 76.f};
    drawPicture(ctx, e, tile, 64.f);
    const float tx = tile.right() + 14.f, tw = std::max(10.f, b.right() - pad - tx);
    float y = tile.y + 2.f;
    drawBold(ctx.r, {tx, y}, elide(ctx.r, e.name, kDetailName, tw), kDetailName, c.text);
    y += ctx.r.lineHeight(kDetailName) + 5.f;
    const float cy = y + 8.f;
    float x = tx + drawPill(ctx, tx, cy, e.badge, badgeColor(th, e.badge)) + 8.f;
    if (!e.versionTitle.empty())
        drawBold(ctx.r, {x, cy - ctx.r.lineHeight(kBody) * 0.5f}, elide(ctx.r, e.versionTitle, kBody, b.right() - pad - x), kBody, c.text);
    y += 22.f;
    if (!e.versionSubtitle.empty()) {
        ctx.r.drawText({tx, y}, elide(ctx.r, e.versionSubtitle, kSmall, tw), kSmall, c.textMuted);
        y += ctx.r.lineHeight(kSmall) + 3.f;
    }
    if (e.open) {
        const auto tone = e.modified ? c.warning : c.accent;
        ctx.r.fillRoundedRect({tx, y + ctx.r.lineHeight(kSmall) * 0.5f - 3.5f, 7.f, 7.f}, tone, 3.5f);
        const std::string open = e.modified ? "ouvert \xC2\xB7 modifi\xC3\xA9, pas encore enregistr\xC3\xA9" : "ouvert en ce moment";
        ctx.r.drawText({tx + 12.f, y}, elide(ctx.r, open, kSmall, tw - 12.f), kSmall, th.onSurface(tone));
    }
    ctx.r.fillRect({b.x + 1.f, headerBottom_, b.w - 1.f, 1.f}, c.border);

    // ---- les actions : les boutons sont des enfants ; ici, la phrase --------
    float ny = noteY_;
    for (const auto& l : noteLines_) {
        ctx.r.drawText({x0, ny}, l, kSmall, c.textMuted);
        ny += ctx.r.lineHeight(kSmall);
    }
    ctx.r.fillRect({b.x + 1.f, actionsBottom_, b.w - 1.f, 1.f}, c.border);

    // ---- ce qu'il contient ---------------------------------------------------
    y = actionsBottom_ + 12.f;
    const float labelW = std::min(118.f, std::floor(w * 0.36f));
    const float lh = ctx.r.lineHeight(kBody);
    constexpr float kRow = 27.f;
    for (const auto& [key, value] : e.facts) {
        // Un chemin garde son debut et sa fin sur une ligne ; le reste passe a
        // la ligne (deux au plus) plutot que d'etre coupe en son milieu.
        std::vector<std::string> lines;
        if (key == "Dossier" || key == "Chemin") lines.push_back(elideMiddle(ctx.r, value, kBody, w - labelW));
        else lines = wrap(value, kBody, w - labelW, 2);
        if (lines.empty()) lines.emplace_back();
        const float rowH = std::max(kRow, lh * static_cast<float>(lines.size()) + 10.f);
        float ty = y + std::round((rowH - lh * static_cast<float>(lines.size())) * 0.5f);
        ctx.r.drawText({x0, ty}, elide(ctx.r, key, kBody, labelW - 6.f), kBody, c.textMuted);
        for (const auto& l : lines) {
            ctx.r.drawText({x0 + labelW, ty}, elide(ctx.r, l, kBody, w - labelW), kBody, c.text);
            ty += lh;
        }
        y += rowH;
        ctx.r.fillRect({x0, y - 1.f, w, 1.f}, c.gridLine);
    }
    if (!e.factsLoaded && e.facts.empty()) {
        ctx.r.drawText({x0, y + 6.f}, "\xE2\x80\xA6", kBody, c.textMuted);
        y += kRow;
    }

    // ---- les dernieres versions (celles qui tiennent : 720 points de haut) ---
    if (!e.versions.empty() && y + 18.f + 20.f + 22.f <= b.bottom()) {
        y += 18.f;
        ctx.r.drawText({x0, y}, "LES DERNI\xC3\x88RES VERSIONS", kTiny, c.textMuted);
        y += 20.f;
        for (const auto& ver : e.versions) {
            if (y + 22.f > b.bottom()) break;
            const float mid = y + 11.f;
            const float ty = mid - ctx.r.lineHeight(kSmall) * 0.5f;
            ctx.r.fillRoundedRect({x0 + 1.f, mid - 4.f, 8.f, 8.f}, th.tone(ver.tone, c.textMuted), 4.f);
            drawBold(ctx.r, {x0 + 16.f, ty}, ver.number, kSmall, c.text);
            const float dw = ctx.r.measure(ver.date, kSmall).width;
            ctx.r.drawText({x0 + w - dw, ty}, ver.date, kSmall, c.textMuted);
            ctx.r.drawText({x0 + 64.f, ty}, elide(ctx.r, ver.label, kSmall, w - 64.f - dw - 10.f), kSmall, c.text);
            y += 22.f;
        }
    }
}

// =============================================================== StartRail ====
StartRail::StartRail(std::string id) : ui::Widget(std::move(id)) {
    using L = StartButton::Look;
    using G = StartButton::Glyph;
    const auto make = [this](std::string text, std::string wid, L look, G glyph) {
        return &static_cast<StartButton&>(addChild(std::make_unique<StartButton>(std::move(text), std::move(wid), look, glyph)));
    };
    new_ = make("Nouveau projet", "startup.new", L::CardPrimary, G::Plus);
    new_->setSubtitle("un dossier, la t\xC3\xA2" "che MAST, pr\xC3\xAAt \xC3\xA0 remplir");
    new_->setHint("Ctrl+N");
    new_->setTooltip("Cr\xC3\xA9" "e un dossier de projet avec la t\xC3\xA2" "che MAST ; les sections, les types et les blocs s'ajoutent ensuite (Ctrl+N)");
    open_ = make("Ouvrir\xE2\x80\xA6", "startup.open", L::Card, G::Open);
    open_->setSubtitle("un dossier de projet, ou un export .XPG / .XHW / .XDB");
    open_->setHint("Ctrl+O");
    open_->setTooltip("Ouvre un dossier de projet ou un export Control Expert (Ctrl+O) ; un .XHW ou un .XDB compl\xC3\xA8te le projet ouvert");
    path_ = &static_cast<StartField&>(addChild(std::make_unique<StartField>("startup.path", ui::Icon::Folder)));
    path_->setPlaceholder("chemin d'un dossier ou d'un export, puis Entr\xC3\xA9" "e");
    path_->setTooltip("Un dossier de projet ou un export, puis Entr\xC3\xA9" "e. Plusieurs fichiers s\xC3\xA9par\xC3\xA9s par ';' (un .XPG et son .XHW) "
                      "s'importent ensemble. Ctrl+V colle un chemin copi\xC3\xA9.");
    // Le bouton ... : l'explorateur de fichiers. Un export, ou le project.xpgproj
    // d'un dossier de projet (l'ecran ouvre alors le dossier).
    browse_ = &static_cast<ui::BrowseButton&>(addChild(std::make_unique<ui::BrowseButton>(
        *path_, ui::openFile("Projets et exports|*.xpgproj;*.xpg;*.xhw;*.xdb;*.xef|Projets (project.xpgproj)|*.xpgproj"
                             "|Exports Control Expert|*.xpg;*.xhw;*.xdb;*.xef", {}, "Ouvrir un projet ou un export"),
        "startup.parcourir")));
    browse_->setFieldLabel("Un dossier de projet ou un export");
    tutorial_ = make("D\xC3\xA9" "couvrir l'application", "startup.tutorial", L::Link, G::Tutorial);
    tutorial_->setSubtitle("le didacticiel, 5 min");
    tutorial_->setHint(">");
    tutorial_->setTooltip("La visite \xC2\xAB D\xC3\xA9" "couvrir l'API \xC2\xBB (14 \xC3\xA9tapes, 5 min), sur le projet ouvert ou sur celui que tu choisis ici ; "
                          "les parcours guid\xC3\xA9s sont dans l'onglet API \xC2\xB7 Didacticiel");
    help_ = make("Aide", "startup.help", L::Link, G::Help);
    help_->setHint("F1");
    help_->setTooltip("L'aide de l'application (F1, depuis n'importe quel \xC3\xA9" "cran)");
    settings_ = make("R\xC3\xA9glages", "startup.settings", L::Link, G::Settings);
    settings_->setHint(">");
    settings_->setTooltip("Les r\xC3\xA9glages de l'application : son th\xC3\xA8me, chacun montr\xC3\xA9 en miniature (Affichage \xE2\x80\xBA Th\xC3\xA8me\xE2\x80\xA6 depuis un projet)");
    // 1.8.0 : les dossiers de l'application - tes projets, ta bibliotheque, tes
    // captures, tes reglages (XPGAnalyser.ini) : les voir, les ouvrir, les changer.
    folders_ = make("Dossiers", "startup.folders", L::Link, G::Open);
    folders_->setHint(">");
    folders_->setTooltip("Les dossiers de l'application : tes projets, ta biblioth\xC3\xA8que, tes captures, tes r\xC3\xA9glages "
                         "(XPGAnalyser.ini) ; les ouvrir dans l'Explorateur, les changer");
    swatches_ = &static_cast<ThemeSwatches&>(addChild(std::make_unique<ThemeSwatches>("startup.themes")));
    autostart_ = make("Choisir\xE2\x80\xA6", "startup.autostart", L::Ghost, G::None);
    autostart_->setTooltip("Le poste d'exploitation d'un projet FINISH, lanc\xC3\xA9 au d\xC3\xA9marrage du PC (un seul projet par PC)");
}

void StartRail::setAutostart(std::string projectName) {
    if (projectName == autostartName_ && !autostart_->text().empty()) return;
    autostartName_ = std::move(projectName);
    autostart_->setText(autostartName_.empty() ? "Choisir\xE2\x80\xA6" : "Changer\xE2\x80\xA6");
    invalidateLayout();
}

void StartRail::setCodeStats(std::string text) {
    codeStats_ = std::move(text);
    invalidateLayout();
    invalidate();
}

void StartRail::setRelease(std::string tag, std::string date) {
    tag_ = std::move(tag);
    date_ = std::move(date);
    invalidate();
}

float StartRail::placeBody(int level) {
    const auto b = bounds();
    const bool roomy = b.w >= 330.f;
    const float pad = roomy ? 24.f : 18.f;
    const float x0 = b.x + pad, w = std::max(40.f, b.w - 2.f * pad);
    // L'espace de chaque niveau : 0 celui d'une fenetre haute (1080), 1 plus
    // serre, 2 aussi avec la phrase et le texte de la zone en deux lignes, 3
    // sans eux (le champ dit encore ce qu'il attend).
    const bool tall = level == 0 && b.h >= 760.f;
    const bool easy = level == 0;
    float y = b.y + (tall ? 28.f : easy ? 20.f : 14.f);
    const std::size_t taglineLines = level <= 1 ? 4u : level == 2 ? 2u : 0u;
    const std::size_t dropLines = level <= 1 ? 3u : level == 2 ? 2u : 0u;

    // ---- la marque : le logo ; a cote, le nom, la phrase, la pastille -------
    const float logoSide = level == 3 ? 52.f : roomy ? 72.f : 58.f;
    textX_ = x0 + logoSide + 16.f;
    const float textW = std::max(40.f, b.right() - pad - textX_);
    title_ = wrap("PLC Project Analyzer", kBrand, textW, 2);
    tagline_ = wrap("L'atelier de tes projets Control Expert : l'API, l'IHM, les macros et les versions.", kBody, textW, taglineLines);
    const float titleH = ui::lineHeight(kBrand) * static_cast<float>(title_.size());
    const float tagH = ui::lineHeight(kBody) * static_cast<float>(tagline_.size());
    // 1.11.7 : les lignes de code, sous la pastille (pas dans une fenetre tres basse).
    codeLines_ = level <= 2 && !codeStats_.empty() ? wrap(codeStats_, kSmall, textW, 2) : std::vector<std::string>{};
    const float codeH = codeLines_.empty() ? 0.f : 4.f + ui::lineHeight(kSmall) * static_cast<float>(codeLines_.size());
    const float blockH = titleH + (tagline_.empty() ? 0.f : 6.f + tagH) + 8.f + 20.f + codeH;
    const float brandH = std::max(logoSide, blockH);
    logo_ = {x0, std::round(y + (brandH - logoSide) * 0.5f), logoSide, logoSide};
    titleY_ = std::round(y + (brandH - blockH) * 0.5f);
    taglineY_ = titleY_ + titleH + 6.f;
    chipY_ = titleY_ + titleH + (tagline_.empty() ? 0.f : 6.f + tagH) + 8.f;
    y += brandH + (tall ? 24.f : easy ? 16.f : 10.f);

    // ---- les deux grandes actions -------------------------------------------
    const float cardH = tall ? 66.f : easy ? 58.f : 54.f;
    new_->setBounds({x0, y, w, cardH});
    y += cardH + (easy ? 10.f : 8.f);
    open_->setBounds({x0, y, w, cardH});
    y += cardH + (tall ? 18.f : easy ? 12.f : 10.f);

    // ---- la zone ou glisser un dossier (et son chemin a coller) --------------
    // Sans son texte (niveau 3), le pictogramme passe a gauche du champ.
    dropText_ = wrap("Glisse ici un dossier de projet ou un export \xE2\x80\x94 ou colle son chemin et Entr\xC3\xA9" "e.", kBody, w - 28.f, dropLines);
    const float lineT = ui::lineHeight(kBody);
    const float dropH = dropText_.empty() ? 12.f + 30.f + 12.f
                                          : 14.f + 18.f + 6.f + lineT * static_cast<float>(dropText_.size()) + 10.f + 30.f + 14.f;
    drop_ = {x0, y, w, dropH};
    dropTextY_ = y + 14.f + 18.f + 6.f;
    const float fieldX = dropText_.empty() ? x0 + 12.f + 18.f + 8.f : x0 + 14.f;
    // Le champ, et au bout son bouton ... (l'explorateur).
    const float fieldY = drop_.bottom() - (dropText_.empty() ? 12.f : 14.f) - 30.f;
    path_->setBounds({fieldX, fieldY, std::max(40.f, drop_.right() - 14.f - fieldX - 34.f - 6.f), 30.f});
    browse_->setBounds({drop_.right() - 14.f - 34.f, fieldY, 34.f, 30.f});
    // Le texte d'attente du champ, a sa largeur : InputText le coupe net, sans
    // points de suspension (la police des champs : 16 points ; les marges de
    // StartField : 30 a gauche, 8 a droite).
    {
        const std::string wide = dropText_.empty() ? "Glisse ou colle un chemin" : "Colle un chemin, puis Entr\xC3\xA9" "e";
        path_->setPlaceholder(ui::measureWidth(wide, gfx::FontId{16}) <= path_->bounds().w - 38.f ? wide
                                                                                                  : std::string("Chemin, puis Entr\xC3\xA9" "e"));
    }
    y += dropH + (tall ? 16.f : easy ? 10.f : 8.f);

    // ---- les liens ------------------------------------------------------------
    const float linkH = tall ? 34.f : easy ? 30.f : 28.f;
    for (auto* link : {tutorial_, help_, settings_, folders_}) {
        link->setBounds({x0 - 8.f, y, w + 16.f, linkH});
        y += linkH;
    }
    y += tall ? 16.f : easy ? 10.f : 8.f;

    // ---- les themes -----------------------------------------------------------
    themeLabel_ = {x0, y, w, 16.f};
    y += 20.f;
    swatches_->setBounds({x0 - 2.f, y, w + 4.f, 34.f});
    return y + 34.f;
}

void StartRail::onLayout() {
    const auto b = bounds();
    const float pad = b.w >= 330.f ? 24.f : 18.f;
    const float x0 = b.x + pad, w = std::max(40.f, b.w - 2.f * pad);

    // ---- en bas : le poste lance au demarrage du PC -----------------------------
    // Le texte a gauche et le bouton a droite quand il reste au texte de quoi
    // dire "Au demarrage du PC : Armoire_Gaz" ; sinon (une colonne etroite) le
    // texte sur toute la largeur, en trois lignes, et le bouton dessous.
    const float lh = ui::lineHeight(kSmall);
    const float bw = std::min(autostart_->naturalWidth(), w - 24.f);
    stationStacked_ = w - 12.f - 22.f - 10.f - bw - 10.f < 180.f;
    const float lines = autostartName_.empty() ? 2.f : 3.f;
    const float stationH = stationStacked_ ? std::round(10.f + lines * lh + 8.f + 26.f + 10.f) : 52.f;

    // LA HAUTEUR DECIDE CE QUI SE MONTRE : le premier niveau dont tout tient au-
    // dessus du poste (1080 : le niveau 0 ; 1280 x 720 : 1 ou 2 ; plus bas : 3).
    float y = 0.f, margin = 20.f;
    for (int level = 0; level <= 3; ++level) {
        y = placeBody(level);
        margin = level == 0 ? 20.f : 12.f;
        if (y + 12.f + stationH + margin <= b.bottom()) break;
    }
    station_ = {x0, std::max(y + 12.f, b.bottom() - margin - stationH), w, stationH};
    stationTextY_ = station_.y + 10.f;
    if (stationStacked_)
        autostart_->setBounds({station_.right() - 10.f - bw, station_.bottom() - 10.f - 26.f, bw, 26.f});
    else
        autostart_->setBounds({station_.right() - 10.f - bw, station_.y + std::round((stationH - 26.f) * 0.5f), bw, 26.f});
}

void StartRail::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& th = ctx.theme;
    const auto& c = th.color;
    // Le fond : le panneau, teinte d'accent en haut et qui s'efface vers le bas.
    ctx.r.fillRect(b, c.panelBg);
    const gfx::Color topTint = mix(c.panelBg, c.accent, th.isDark() ? 0.09f : 0.06f);
    constexpr int kBands = 24;
    const float fadeH = b.h * 0.46f;
    for (int k = 0; k < kBands; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(kBands);
        ctx.r.fillRect({b.x, std::floor(b.y + fadeH * t), b.w, std::ceil(fadeH / static_cast<float>(kBands)) + 1.f}, mix(topTint, c.panelBg, t));
    }
    ctx.r.fillRect({b.right() - 1.f, b.y, 1.f, b.h}, c.border);

    // ---- la marque ----------------------------------------------------------------
    brand::drawLogo(ctx.r, logo_);
    float y = titleY_;
    for (const auto& l : title_) {
        drawBold(ctx.r, {textX_, y}, l, kBrand, c.text);
        y += ctx.r.lineHeight(kBrand);
    }
    y = taglineY_;
    for (const auto& l : tagline_) {
        ctx.r.drawText({textX_, y}, l, kBody, c.textMuted);
        y += ctx.r.lineHeight(kBody);
    }
    if (!tag_.empty() || !date_.empty()) {
        float x = textX_;
        if (!tag_.empty()) x += drawPill(ctx, x, chipY_ + 10.f, tag_, c.accent) + 8.f;
        ctx.r.drawText({x, chipY_ + 10.f - ctx.r.lineHeight(kSmall) * 0.5f}, date_, kSmall, c.textMuted);
    }
    // 1.11.7 : les lignes de code de l'application.
    {
        float cy = chipY_ + 24.f;
        for (const auto& l : codeLines_) {
            ctx.r.drawText({textX_, cy}, l, kSmall, c.textMuted);
            cy += ctx.r.lineHeight(kSmall);
        }
    }

    // ---- la zone ou glisser -------------------------------------------------------
    dashedFrame(ctx.r, drop_, c.borderStrong, 10.f);
    if (dropText_.empty())      // une fenetre basse : le pictogramme devant le champ
        dropGlyph(ctx.r, {drop_.x + 12.f, drop_.y + std::round((drop_.h - 18.f) * 0.5f), 18.f, 18.f}, c.textMuted);
    else
        dropGlyph(ctx.r, {drop_.x + 14.f, drop_.y + 14.f, 18.f, 18.f}, c.textMuted);
    y = dropTextY_;
    for (const auto& l : dropText_) {
        ctx.r.drawText({drop_.x + 14.f, y}, l, kBody, c.textMuted);
        y += ctx.r.lineHeight(kBody);
    }

    // ---- le theme -------------------------------------------------------------------
    ctx.r.drawText({themeLabel_.x, themeLabel_.y}, "TH\xC3\x88ME \xC2\xB7 " + ui::Theme::labelOf(th.name), kTiny, c.textMuted);

    // ---- le poste au demarrage du PC ----------------------------------------------
    const auto green = th.onSurface(c.ok);
    const bool none = autostartName_.empty();
    ctx.r.fillRoundedRect(station_, c.border, 8.f);
    ctx.r.fillRoundedRect(station_.inset(1.f, 1.f), mix(c.panelBg, c.ok, none ? 0.f : 0.07f), 7.f);
    const float lh = ctx.r.lineHeight(kSmall);
    const float left = station_.x + 12.f, tx = left + 22.f;
    const float room = stationStacked_ ? station_.right() - 12.f - tx : autostart_->bounds().x - 10.f - tx;
    const float ty = stationStacked_ ? stationTextY_ : station_.y + std::round((station_.h - 2.f * lh) * 0.5f);
    const float iconY = stationStacked_ ? ty + std::round((lh - 14.f) * 0.5f) : station_.y + std::round((station_.h - 14.f) * 0.5f);
    ui::drawIcon(ctx.r, none ? ui::Icon::Station : ui::Icon::Play, {left, iconY, 14.f, 14.f}, none ? c.textMuted : green);
    if (none) {
        ctx.r.drawText({tx, ty}, elide(ctx.r, "Aucun poste d'exploitation", kSmall, room), kSmall, c.text);
        ctx.r.drawText({tx, ty + lh}, elide(ctx.r, "au d\xC3\xA9marrage du PC", kSmall, room), kSmall, c.textMuted);
    } else if (stationStacked_) {
        ctx.r.drawText({tx, ty}, elide(ctx.r, "Au d\xC3\xA9marrage du PC :", kSmall, room), kSmall, c.textMuted);
        drawBold(ctx.r, {tx, ty + lh}, elide(ctx.r, autostartName_, kSmall, room), kSmall, c.text);
        ctx.r.drawText({tx, ty + 2.f * lh}, elide(ctx.r, "en poste d'exploitation", kSmall, room), kSmall, c.text);
    } else {
        ctx.r.drawText({tx, ty}, elide(ctx.r, "Au d\xC3\xA9marrage du PC :", kSmall, room), kSmall, c.textMuted);
        const std::string tail = " en poste d'exploitation";
        const std::string name = elide(ctx.r, autostartName_, kSmall, std::max(20.f, room - ctx.r.measure(tail, kSmall).width));
        drawBold(ctx.r, {tx, ty + lh}, name, kSmall, c.text);
        const float nx = tx + ctx.r.measure(name, kSmall).width + 1.f;
        ctx.r.drawText({nx, ty + lh}, elide(ctx.r, tail, kSmall, tx + room - nx), kSmall, c.text);
    }
}

// =============================================================== StartPage ====
StartPage::StartPage(std::string id) : ui::Widget(std::move(id)) {
    rail_ = &static_cast<StartRail&>(addChild(std::make_unique<StartRail>("startup.rail")));
    grid_ = &static_cast<ProjectGrid&>(addChild(std::make_unique<ProjectGrid>("startup.projects")));
    detail_ = &static_cast<ProjectDetail&>(addChild(std::make_unique<ProjectDetail>("startup.detail")));
    search_ = &static_cast<StartField&>(addChild(std::make_unique<StartField>("startup.search", ui::Icon::Search)));
    search_->setPlaceholder("Chercher un projet, un automate, un dossier\xE2\x80\xA6");
    search_->setTooltip("Le nom, l'automate ou le chemin, sans souci des accents (Ctrl+F) ; \xC3\x89" "chap efface");
    chips_ = &static_cast<FilterChips&>(addChild(std::make_unique<FilterChips>("startup.filters")));
    auto sort = std::make_unique<ui::DropDown>("startup.sort");
    sort->setItems({{"Les plus r\xC3\xA9" "cents", "recent", {}, true}, {"Par nom", "nom", {}, true}, {"Par \xC3\xA9tat", "etat", {}, true}});
    sort->setSelectedIndex(0);
    sort->setTooltip("L'ordre des cartes : le dernier ouvert d'abord, par nom, ou par \xC3\xA9tat (DEV, NEW, FINISH, LOCK, exports)");
    sortBox_ = &static_cast<ui::DropDown&>(addChild(std::move(sort)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>("startup.status")));
    hints_ = &status_->addIndicator(std::make_unique<StatusText>("startup.status.keys", std::string{}), ui::StatusBar::Slot::Right);
    program_ = &status_->addIndicator(std::make_unique<StatusText>("startup.status.program", std::string{}), ui::StatusBar::Slot::Right);

    // La page tient elle-meme la recherche, les filtres, le tri et le choix ;
    // l'ecran n'en voit que le resultat (selectionChanged, et les boutons).
    links_ += search_->textChanged->connect([this](const std::string& t) {
        if (syncing_ || t == query_) return;
        query_ = t;
        refilter(0);
    });
    links_ += chips_->chosen->connect([this](int f) {
        if (f >= 0 && static_cast<std::size_t>(f) < kFilterCount) setFilter(static_cast<Filter>(f));
    });
    links_ += sortBox_->selectionChanged->connect([this](int i) {
        if (!syncing_ && i >= 0) setSort(static_cast<Sort>(std::min(i, 2)));
    });
    links_ += grid_->selectionChanged->connect([this](int i) {
        if (i < 0 || static_cast<std::size_t>(i) >= visible_.size()) return;
        selected_ = visible_[static_cast<std::size_t>(i)];
        showSelection();
    });
}

void StartPage::setStatusRight(std::string hints, std::string program) {
    static_cast<StatusText*>(hints_)->setText(std::move(hints));
    static_cast<StatusText*>(program_)->setText(std::move(program));
    status_->invalidateLayout();
}

const ProjectEntry* StartPage::selected() const noexcept {
    return selected_ < entries_.size() ? &entries_[selected_] : nullptr;
}

std::vector<const ProjectEntry*> StartPage::visibleEntries() const {
    std::vector<const ProjectEntry*> out;
    out.reserve(visible_.size());
    for (const auto i : visible_) out.push_back(&entries_[i]);
    return out;
}

void StartPage::setEntries(std::vector<ProjectEntry> entries, const std::string& keepPath) {
    int oldPos = -1;
    for (std::size_t k = 0; k < visible_.size(); ++k)
        if (visible_[k] == selected_) oldPos = static_cast<int>(k);
    entries_ = std::move(entries);
    selected_ = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (!keepPath.empty() && entries_[i].path == keepPath) selected_ = i;
    std::size_t projects = 0, exports = 0, missing = 0;
    for (const auto& e : entries_) {
        if (e.isFolder) ++projects;
        else if (e.missing) ++missing;
        else ++exports;
    }
    // "5 projets . 1 export . 1 introuvable" : ce que "Tous" compte, detaille.
    counts_.clear();
    const auto add = [this](std::string part) { counts_ += (counts_.empty() ? "" : " \xC2\xB7 ") + part; };
    if (projects) add(plural(projects, "projet", "projets"));
    if (exports) add(plural(exports, "export", "exports"));
    if (missing) add(std::to_string(missing) + " introuvable" + (missing > 1 ? "s" : ""));
    refilter(std::max(0, oldPos));
    invalidateLayout();
}

bool StartPage::select(const std::string& path) {
    for (std::size_t k = 0; k < visible_.size(); ++k)
        if (entries_[visible_[k]].path == path) return selectVisible(static_cast<int>(k));
    return false;
}

bool StartPage::selectVisible(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size()) return false;
    selected_ = visible_[static_cast<std::size_t>(index)];
    grid_->setSelected(index);
    showSelection();
    return true;
}

void StartPage::setFilter(Filter f) {
    chips_->setCurrent(f);
    if (f == filter_) return;
    filter_ = f;
    refilter(0);
}

void StartPage::setSort(Sort s) {
    if (s != sort_) {
        sort_ = s;
        refilter(0);
    }
    if (sortBox_->selectedIndex() != static_cast<int>(s)) {
        syncing_ = true;
        sortBox_->setSelectedIndex(static_cast<int>(s));
        syncing_ = false;
    }
}

void StartPage::setQuery(const std::string& text) {
    if (search_->text() != text) {
        syncing_ = true;
        search_->setText(text);
        syncing_ = false;
    }
    if (text == query_) return;
    query_ = text;
    refilter(0);
}

void StartPage::refilter(int fallback) {
    // Les nombres des filtres comptent ce que la recherche laisse passer : ils
    // disent ou sont les resultats.
    std::size_t counts[kFilterCount]{};
    visible_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!matches(e, query_)) continue;
        for (std::size_t f = 0; f < kFilterCount; ++f)
            if (passes(e, static_cast<Filter>(f))) ++counts[f];
        if (passes(e, filter_)) visible_.push_back(i);
    }
    for (std::size_t f = 0; f < kFilterCount; ++f) chips_->setCount(static_cast<Filter>(f), counts[f]);

    // Par etat : ce qu'on modifie d'abord (DEV, NEW), puis ce qui est termine,
    // livre, les exports, et les chemins disparus.
    const auto rank = [](const ProjectEntry& e) {
        if (e.missing) return 5;
        if (!e.isFolder) return 4;
        switch (e.state) {
            case project::State::Dev: return 0;
            case project::State::New: return 1;
            case project::State::Finish: return 2;
            case project::State::Lock: return 3;
        }
        return 4;
    };
    std::stable_sort(visible_.begin(), visible_.end(), [&](std::size_t lhs, std::size_t rhs) {
        const auto& p = entries_[lhs];
        const auto& q = entries_[rhs];
        if (sort_ == Sort::Name) {
            const auto fp = foldText(p.name), fq = foldText(q.name);
            if (fp != fq) return fp < fq;
        } else if (sort_ == Sort::State) {
            const int rp = rank(p), rq = rank(q);
            if (rp != rq) return rp < rq;
        }
        return p.recentRank < q.recentRank;
    });

    // Le choix reste le meme s'il est encore montre ; sinon la carte a la meme
    // place (apres une suppression : la suivante), ou la premiere.
    if (std::find(visible_.begin(), visible_.end(), selected_) == visible_.end()) {
        selected_ = static_cast<std::size_t>(-1);
        if (!visible_.empty())
            selected_ = visible_[std::min(static_cast<std::size_t>(std::max(0, fallback)), visible_.size() - 1)];
    }

    if (entries_.empty())
        grid_->setEmptyMessage("Aucun projet r\xC3\xA9" "cent pour l'instant",
                               "Cr\xC3\xA9" "e un projet, ouvre un dossier ou un export Control Expert \xE2\x80\x94 ou glisse-le dans cette fen\xC3\xAAtre.");
    else if (visible_.empty())
        grid_->setEmptyMessage(query_.empty() ? std::string("Aucun projet dans \xC2\xAB ") + filterLabel(filter_) + " \xC2\xBB"
                                              : "Aucun projet ne correspond \xC3\xA0 \xC2\xAB " + query_ + " \xC2\xBB",
                               query_.empty() ? std::string("Choisis \xC2\xAB Tous \xC2\xBB pour revoir toute la liste.")
                                              : std::string("\xC3\x89" "chap efface la recherche ; \xC2\xAB Tous \xC2\xBB montre toute la liste."));
    else
        grid_->setEmptyMessage({}, {});

    std::vector<ProjectEntry> items;
    items.reserve(visible_.size());
    int sel = -1;
    for (std::size_t k = 0; k < visible_.size(); ++k) {
        items.push_back(entries_[visible_[k]]);
        if (visible_[k] == selected_) sel = static_cast<int>(k);
    }
    grid_->setItems(std::move(items), sel);
    showSelection();
}

void StartPage::showSelection() {
    ProjectEntry* e = selected_ < entries_.size() ? &entries_[selected_] : nullptr;
    if (e && !e->factsLoaded && loader_) loader_(*e);
    detail_->setEntry(e);
    int sel = -1;
    for (std::size_t k = 0; k < visible_.size(); ++k)
        if (visible_[k] == selected_) sel = static_cast<int>(k);
    if (grid_->selected() != sel) grid_->setSelected(sel);
    selectionChanged->emit();
}

void StartPage::onLayout() {
    const auto b = bounds();
    constexpr float kStatusH = 26.f;
    const float railW = std::clamp(std::round(b.w * 0.2f), 270.f, 400.f);
    float detailW = std::clamp(std::round(b.w * 0.2f), 270.f, 420.f);
    if (b.w - railW - detailW < 460.f) detailW = std::max(230.f, b.w - railW - 460.f);
    rail_->setBounds({b.x, b.y, railW, b.h});
    center_ = {b.x + railW, b.y, std::max(0.f, b.w - railW - detailW), std::max(0.f, b.h - kStatusH)};
    detail_->setBounds({b.right() - detailW, b.y, detailW, std::max(0.f, b.h - kStatusH)});
    status_->setBounds({b.x + railW, b.bottom() - kStatusH, std::max(0.f, b.w - railW), kStatusH});
    // La barre d'etat : son message (l'import en cours, ce qu'un geste a fait)
    // garde au moins 360 points ; sinon le nom du programme s'efface, puis les
    // raccourcis.
    {
        auto* hints = static_cast<StatusText*>(hints_);
        auto* program = static_cast<StatusText*>(program_);
        const float room = std::max(0.f, b.w - railW) - 24.f - 360.f;
        program->setShown(hints->textWidth() + program->textWidth() + 24.f <= room);
        hints->setShown(hints->textWidth() + 12.f <= room);
    }

    // L'en-tete du centre : le titre et les nombres ; la recherche a droite (ou
    // dessous quand la place manque) ; puis les filtres et le tri.
    const float padX = center_.w >= 760.f ? 28.f : 20.f;
    const float x0 = center_.x + padX, w = std::max(0.f, center_.w - 2.f * padX);
    float y = center_.y + (b.h >= 760.f ? 22.f : 16.f);
    header_ = {x0, y, w, 34.f};
    const float titleW = ui::measureWidth("Projets r\xC3\xA9" "cents", kHeading) + 12.f + ui::measureWidth(counts_, kBody);
    const float searchW = std::min(420.f, w - titleW - 24.f);
    if (searchW >= 220.f) {
        search_->setBounds({x0 + w - searchW, y + 1.f, searchW, 32.f});
        y += 34.f + 12.f;
    } else {
        y += 34.f + 4.f;
        search_->setBounds({x0, y, w, 32.f});
        y += 32.f + 12.f;
    }
    // Le texte d'attente de la recherche, a sa largeur (InputText le coupe net).
    {
        const std::string wide = "Chercher un projet, un automate, un dossier\xE2\x80\xA6";
        search_->setPlaceholder(ui::measureWidth(wide, gfx::FontId{16}) <= search_->bounds().w - 38.f ? wide
                                                                                                        : std::string("Chercher\xE2\x80\xA6  (nom, automate, dossier)"));
    }
    // Rien a filtrer ni a trier tant qu'il n'y a aucun projet recent : les
    // filtres (tous a 0) et le tri s'effacent, et la carte "Nouveau projet" monte.
    const bool any = !entries_.empty();
    chips_->setVisibility(any ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    sortBox_->setVisibility(any ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (any) {
        const float sortW = std::clamp(std::round(w * 0.28f), 150.f, 200.f);
        chips_->setBounds({x0, y, std::min(chips_->naturalWidth(), std::max(0.f, w - sortW - 12.f)), 30.f});
        sortBox_->setBounds({x0 + w - sortW, y, sortW, 30.f});
        y += 30.f + 12.f;
    }
    grid_->setBounds({center_.x, y, center_.w, std::max(0.f, center_.bottom() - y)});
}

void StartPage::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(center_, c.windowBg);
    const std::string title = "Projets r\xC3\xA9" "cents";
    const float ty = header_.y + (header_.h - ctx.r.lineHeight(kHeading)) * 0.5f;
    drawBold(ctx.r, {header_.x, ty}, title, kHeading, c.text);
    if (!counts_.empty()) {
        const float x = header_.x + ctx.r.measure(title, kHeading).width + 12.f;
        ctx.r.drawText({x, ty + ctx.r.lineHeight(kHeading) - ctx.r.lineHeight(kBody) - 1.f}, counts_, kBody, c.textMuted);
    }
}

} // namespace app::start
