#include "ColorPalette.hpp"

#include "../Icons.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ui {

namespace {

// 1.10 : le dessin de la maquette (scene Couleurs) - un titre et la pipette en
// haut, le nuancier a gauche (et avant / apres en bas), le choix personnalise
// a droite (le carre, la teinte et la transparence en barres dessous, puis
// les champs), en bas ce que veut dire le champ actif, Annuler, Appliquer.
constexpr float kPad = 10.f;
constexpr float kHeadH = 38.f;                                       // le titre, la pipette
constexpr float kSwatch = 20.f;
constexpr float kGap = 4.f;
constexpr float kLabel = 18.f;
constexpr float kLeftW = 10 * kSwatch + 9 * kGap;                  // 236 : le nuancier
constexpr float kColGap = 18.f;
constexpr float kRightW = 240.f;                                     // le carre, les barres, les champs
constexpr float kSquareH = 150.f;
constexpr float kBarH = 14.f;                                        // teinte, transparence
constexpr float kFieldH = 24.f;
constexpr float kFieldLabel = 15.f;                                  // le libelle, au-dessus de la case
constexpr float kSmallField = 42.f;
constexpr float kFieldGap = 6.f;
constexpr float kBodyH = kSquareH + 8.f + kBarH + 6.f + kBarH + 8.f + 2 * (kFieldLabel + kFieldH) + 6.f;   // 284
constexpr float kBeforeAfterH = 38.f;
constexpr float kFootH = 46.f;
constexpr float kButtonH = 28.f;
constexpr float kWidth = kPad + kLeftW + kColGap + kRightW + kPad;  // 514
constexpr float kHeight = kHeadH + kPad + kBodyH + kPad + kFootH;    // 388
constexpr float kLoupeCells = 11.f;                                  // la loupe : 11 x 11 pixels
constexpr float kLoupeZoom = 9.f;
constexpr gfx::FontId kFont{16};
constexpr gfx::FontId kSmall{13};

std::uint8_t clampByte(double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }

std::vector<std::string> buildSwatches() {
    std::vector<std::string> out;
    for (const std::uint32_t g : {0x000000u, 0x1F1F1Fu, 0x3D3D3Du, 0x5C5C5Cu, 0x7A7A7Au, 0x999999u, 0xB8B8B8u, 0xD6D6D6u,
                                  0xEDEDEDu, 0xFFFFFFu})
        out.push_back(hexOf(gfx::Color::rgb(g)));
    // Dix teintes franches, lisibles sur fond sombre comme clair : les couleurs
    // d'etat d'un synoptique (defaut, alerte, marche...) en font partie.
    const std::uint32_t hues[] = {0xE74C3C, 0xF2994A, 0xF2C94C, 0x27AE60, 0x6FCF97,
                                  0x2D9CDB, 0x2F6FD6, 0x5B5FC7, 0x9B51E0, 0xEB5E9D};
    for (int shade = 0; shade < 3; ++shade)
        for (const auto h : hues) {
            auto c = gfx::Color::rgb(h);
            auto mix = [&](std::uint8_t ch) {
                return shade == 0 ? clampByte(ch * 0.62)                       // fonce
                     : shade == 1 ? ch                                          // franc
                                  : clampByte(ch + (255 - ch) * 0.45);         // clair
            };
            out.push_back(hexOf({mix(c.r), mix(c.g), mix(c.b), 255}));
        }
    return out;
}

bool sameColor(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Un damier : ce que montre une couleur transparente (ou a demi).
void checker(gfx::IRenderer& r, const gfx::Rect& box) {
    r.fillRect(box, gfx::Color{235, 235, 235, 255});
    const float s = 5.f;
    for (float y = box.y; y < box.bottom(); y += s)
        for (float x = box.x; x < box.right(); x += s)
            if ((static_cast<int>((x - box.x) / s) + static_cast<int>((y - box.y) / s)) % 2 == 0)
                r.fillRect({x, y, std::min(s, box.right() - x), std::min(s, box.bottom() - y)}, gfx::Color{190, 190, 190, 255});
}

// Le libelle d'un champ (au-dessus de sa case), la valeur maximale.
struct FieldInfo { const char* label; int max; };
FieldInfo infoOf(ColorPalette::Field f) {
    switch (f) {
        case ColorPalette::Field::R: return {"R", 255};
        case ColorPalette::Field::G: return {"G", 255};
        case ColorPalette::Field::B: return {"B", 255};
        case ColorPalette::Field::A: return {"Opacit\xC3\xA9 %", 100};
        case ColorPalette::Field::H: return {"T", 359};
        case ColorPalette::Field::S: return {"S", 100};
        case ColorPalette::Field::V: return {"V", 100};
        case ColorPalette::Field::Hex: break;
    }
    return {"Code", 0};
}

// Tab suit le dessin : Code T S V, puis R G B Opacite.
constexpr ColorPalette::Field kFieldOrder[] = {ColorPalette::Field::Hex, ColorPalette::Field::H, ColorPalette::Field::S,
                                               ColorPalette::Field::V,   ColorPalette::Field::R, ColorPalette::Field::G,
                                               ColorPalette::Field::B,   ColorPalette::Field::A};

// L'opacite en % <-> l'alpha (0 a 255).
int percentOf(std::uint8_t a) { return static_cast<int>(std::lround(a * 100.0 / 255.0)); }
std::uint8_t alphaOfPercent(int pct) { return clampByte(std::clamp(pct, 0, 100) * 255.0 / 100.0); }

// Lisible sur cette couleur : du blanc sur le fonce, du noir sur le clair.
gfx::Color inkOn(gfx::Color c) {
    const double lum = (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) * c.a / 255.0 + 235.0 * (255 - c.a) / 255.0;
    return lum > 150.0 ? gfx::Color{20, 20, 20, 255} : gfx::Color{255, 255, 255, 255};
}

// Une petite pipette en traits (le tube, la poire), dans la boite `b`.
void pipetteGlyph(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color ink) {
    const gfx::Point tip{b.x + 2.f, b.bottom() - 2.f}, top{b.right() - 5.f, b.y + 5.f};
    r.line(tip, top, ink, 2.f);
    r.fillRect({top.x - 1.f, top.y - 4.f, 6.f, 6.f}, ink);
    r.line({top.x - 5.f, top.y + 1.f}, {top.x + 1.f, top.y + 7.f}, ink, 2.f);
}

int& activePipettes() {
    static int n = 0;
    return n;
}

// 1.10 : devant une expression (=...), T S V se retirent et la case Code prend
// toute la rangee : le = et l'expression restent visibles.
bool fieldShown(const std::string& draft, ColorPalette::Field f) {
    const bool expression = !draft.empty() && draft.front() == '=';
    return !(expression && (f == ColorPalette::Field::H || f == ColorPalette::Field::S || f == ColorPalette::Field::V));
}

// 1.10 : la partie de la palette sous la souris (hors pastilles), pour l'aide
// du bas - ce que la maquette met en infobulles. Vide : rien a dire.
std::string partAt(const ColorPalette& p, gfx::Point pt) {
    if (p.pipetteRect().contains(pt)) return "@pipette";
    if (p.beforeRect().contains(pt)) return "@avant";
    if (p.afterRect().contains(pt)) return "@apres";
    if (p.transparentRect().contains(pt)) return "@transparent";
    if (p.squareRect().contains(pt)) return "@carre";
    if (p.hueRect().contains(pt)) return "@teinte";
    if (p.alphaRect().contains(pt)) return "@alpha";
    return {};
}

} // namespace

bool parseHexColor(std::string_view v, gfx::Color& out) noexcept {
    if ((v.size() != 7 && v.size() != 9) || v[0] != '#') return false;
    auto nibble = [](char ch, unsigned& x) {
        if (ch >= '0' && ch <= '9') { x = static_cast<unsigned>(ch - '0'); return true; }
        if (ch >= 'a' && ch <= 'f') { x = static_cast<unsigned>(ch - 'a' + 10); return true; }
        if (ch >= 'A' && ch <= 'F') { x = static_cast<unsigned>(ch - 'A' + 10); return true; }
        return false;
    };
    unsigned vals[4] = {0, 0, 0, 255};
    const std::size_t n = (v.size() - 1) / 2;
    for (std::size_t i = 0; i < n; ++i) {
        unsigned hi = 0, lo = 0;
        if (!nibble(v[1 + 2 * i], hi) || !nibble(v[2 + 2 * i], lo)) return false;
        vals[i] = hi * 16 + lo;
    }
    out = gfx::Color{static_cast<std::uint8_t>(vals[0]), static_cast<std::uint8_t>(vals[1]),
                     static_cast<std::uint8_t>(vals[2]), static_cast<std::uint8_t>(vals[3])};
    return true;
}

std::string hexOf(gfx::Color c, bool withAlpha) {
    char b[16];
    if (withAlpha) std::snprintf(b, sizeof b, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    else           std::snprintf(b, sizeof b, "#%02X%02X%02X", c.r, c.g, c.b);
    return b;
}

std::string colorCode(gfx::Color c) { return hexOf(c, c.a < 255); }

gfx::Color colorFromHsv(double h, double s, double v, std::uint8_t alpha) noexcept {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double c = v * s;
    const double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1));
    const double m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return {clampByte((r + m) * 255), clampByte((g + m) * 255), clampByte((b + m) * 255), alpha};
}

void hsvOf(gfx::Color c, double& h, double& s, double& v) noexcept {
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) return;                        // gris : la teinte reste ce qu'elle etait
    if (mx == r)      h = 60 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) h = 60 * ((b - r) / d + 2);
    else              h = 60 * ((r - g) / d + 4);
    if (h < 0) h += 360;
}

// ============================================================ ColorPalette ===
ColorPalette::ColorPalette(std::string id) : Widget(std::move(id)) { setFocusPolicy(true); }

ColorPalette::~ColorPalette() {
    if (picking_) --activePipettes();
}

std::vector<std::string>& ColorPalette::recent() {
    static std::vector<std::string> list;
    return list;
}

const std::vector<std::string>& ColorPalette::swatches() {
    static const std::vector<std::string> all = buildSwatches();
    return all;
}

int ColorPalette::pickingCount() noexcept { return activePipettes(); }

void ColorPalette::setValue(std::string value) {
    value_ = std::move(value);
    origin_ = value_;
    draft_ = value_;
    gfx::Color c;
    if (parseHexColor(value_, c)) setColor(c, false);
    draft_ = value_;                           // le code tel qu'il est ecrit (minuscules gardees)
    invalidate();
}

void ColorPalette::setTitle(std::string title, std::string subject) {
    title_ = std::move(title);
    subject_ = std::move(subject);
    invalidate();
}

void ColorPalette::setProjectColors(std::vector<std::string> colors) {
    project_.clear();
    for (auto& c : colors) {
        gfx::Color probe;
        if (!parseHexColor(c, probe)) continue;
        if (std::none_of(project_.begin(), project_.end(), [&](const std::string& p) { return sameColor(p, c); }))
            project_.push_back(std::move(c));
        if (project_.size() == 10) break;
    }
    invalidate();
}

void ColorPalette::open() {
    open_ = true;
    origin_ = value_;
    draft_ = value_;
    gfx::Color c;
    if (parseHexColor(value_, c)) setColor(c, false);
    draft_ = value_;
    field_ = Field::Hex;
    fieldEdit_.clear();
    picked_.clear();
    hover_.clear();
    replaceOnType_ = true;
    grabFocus();
    invalidate();
}

void ColorPalette::close() {
    if (!open_) return;
    if (picking_) { picking_ = false; --activePipettes(); }
    open_ = false;
    drag_ = Drag::None;
    invalidate();
    closed->emit();
}

void ColorPalette::choose(const std::string& color) {
    value_ = color;
    draft_ = color;
    gfx::Color probe;
    if (parseHexColor(color, probe)) {
        auto& r = recent();
        r.erase(std::remove_if(r.begin(), r.end(), [&](const std::string& x) { return sameColor(x, color); }), r.end());
        r.insert(r.begin(), color);
        if (r.size() > 10) r.resize(10);
    }
    if (picking_) { picking_ = false; --activePipettes(); }
    open_ = false;
    drag_ = Drag::None;
    invalidate();
    applied->emit(color);
}

// ---------------------------------------------------------------- geometrie ---
float ColorPalette::sectionY(int section) const {
    // Colonne de gauche, depuis le haut de la fenetre. 0 : nuancier (titre +
    // 4 rangees) ; 1 : du projet ; 2 : recentes ; 3 : transparent.
    float y = kHeadH + kPad;
    const float row = kSwatch + kGap;
    if (section == 0) return y;
    y += kLabel + 4 * row;
    if (section == 1) return y;
    if (!project_.empty()) y += kLabel + row;
    if (section == 2) return y;
    if (!recent().empty()) y += kLabel + row;
    return y + 4.f;
}

gfx::Rect ColorPalette::popupRect() const {
    const auto b = bounds();
    const float h = kHeight;
    float x = b.x;
    float y = b.bottom() + 2.f;
    const auto surface = surfaceSize();
    if (surface.h > 0.f && y + h > surface.h) y = std::max(0.f, b.y - h - 2.f);
    if (surface.w > 0.f && x + kWidth > surface.w) x = std::max(0.f, surface.w - kWidth - 4.f);
    return {x, y, kWidth, h};
}

gfx::Point ColorPalette::rightColumn() const {
    const auto p = popupRect();
    return {p.x + kPad + kLeftW + kColGap, p.y + kHeadH + kPad};
}

std::vector<ColorPalette::Cell> ColorPalette::cells() const {
    std::vector<Cell> out;
    const auto p = popupRect();
    auto rowOf = [&](float y, const std::vector<std::string>& colors) {
        for (std::size_t i = 0; i < colors.size() && i < 10; ++i)
            out.push_back({{p.x + kPad + static_cast<float>(i) * (kSwatch + kGap), p.y + y, kSwatch, kSwatch}, colors[i]});
    };
    const auto& all = swatches();
    for (int r = 0; r < 4; ++r) {
        std::vector<std::string> line(all.begin() + r * 10, all.begin() + r * 10 + 10);
        rowOf(sectionY(0) + kLabel + static_cast<float>(r) * (kSwatch + kGap), line);
    }
    if (!project_.empty()) rowOf(sectionY(1) + kLabel, project_);
    if (!recent().empty()) rowOf(sectionY(2) + kLabel, recent());
    return out;
}

gfx::Rect ColorPalette::transparentRect() const {
    const auto p = popupRect();
    return {p.x + kPad, p.y + sectionY(3), 120.f, kSwatch};
}
gfx::Rect ColorPalette::squareRect() const {
    const auto o = rightColumn();
    return {o.x, o.y, kRightW, kSquareH};
}
gfx::Rect ColorPalette::hueRect() const {
    const auto s = squareRect();
    return {s.x, s.bottom() + 8.f, kRightW, kBarH};
}
gfx::Rect ColorPalette::alphaRect() const {
    const auto h = hueRect();
    return {h.x, h.bottom() + 6.f, kRightW, kBarH};
}
gfx::Point ColorPalette::squarePoint(double saturation, double value) const {
    const auto s = squareRect();
    return {s.x + s.w * static_cast<float>(std::clamp(saturation, 0.002, 0.998)),
            s.y + s.h * static_cast<float>(std::clamp(1.0 - value, 0.002, 0.998))};
}
gfx::Point ColorPalette::huePoint(double degrees) const {
    const auto h = hueRect();
    const double d = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
    return {h.x + h.w * static_cast<float>(std::clamp(d / 360.0, 0.0, 0.999)), h.y + h.h * 0.5f};
}
gfx::Point ColorPalette::alphaPoint(double opacity) const {
    const auto a = alphaRect();
    return {a.x + (a.w - 1.f) * static_cast<float>(std::clamp(opacity, 0.0, 1.0)), a.y + a.h * 0.5f};
}
// Avant / apres : en bas de la colonne de gauche, une bande coupee en deux.
gfx::Rect ColorPalette::beforeRect() const {
    const auto p = popupRect();
    const float y = p.y + kHeadH + kPad + kBodyH - kBeforeAfterH;
    return {p.x + kPad, y, kLeftW * 0.5f, kBeforeAfterH};
}
gfx::Rect ColorPalette::afterRect() const {
    const auto b = beforeRect();
    return {b.right(), b.y, kLeftW * 0.5f, kBeforeAfterH};
}
gfx::Rect ColorPalette::fieldRect(Field f) const {
    // Deux rangees, le libelle au-dessus de chaque case : Code T S V, puis
    // R G B Opacite % (l'opacite sous S et V).
    const auto o = rightColumn();
    const float y0 = alphaRect().bottom() + 8.f + kFieldLabel;
    const float y1 = y0 + kFieldH + kFieldGap + kFieldLabel;
    const float step = kSmallField + kFieldGap;
    const float codeW = kRightW - 3 * step;                            // 96
    switch (f) {
        case Field::Hex: return {o.x, y0, fieldShown(draft_, Field::H) ? codeW : kRightW, kFieldH};
        case Field::H: return {o.x + codeW + kFieldGap, y0, kSmallField, kFieldH};
        case Field::S: return {o.x + codeW + kFieldGap + step, y0, kSmallField, kFieldH};
        case Field::V: return {o.x + codeW + kFieldGap + 2 * step, y0, kSmallField, kFieldH};
        case Field::R: return {o.x, y1, kSmallField, kFieldH};
        case Field::G: return {o.x + step, y1, kSmallField, kFieldH};
        case Field::B: return {o.x + 2 * step, y1, kSmallField, kFieldH};
        case Field::A: {
            const float x = o.x + codeW + kFieldGap + step;            // sous S
            return {x, y1, o.x + kRightW - x, kFieldH};
        }
    }
    return {};
}
gfx::Rect ColorPalette::pipetteRect() const {
    const auto p = popupRect();
    return {p.right() - kPad - 96.f, p.y + (kHeadH - kButtonH) * 0.5f, 96.f, kButtonH};
}
gfx::Rect ColorPalette::okRect() const {
    const auto p = popupRect();
    return {p.right() - kPad - 96.f, p.bottom() - kFootH + (kFootH - kButtonH) * 0.5f, 96.f, kButtonH};
}
gfx::Rect ColorPalette::cancelRect() const {
    const auto ok = okRect();
    return {ok.x - 8.f - 84.f, ok.y, 84.f, kButtonH};
}

gfx::Rect ColorPalette::loupeRect() const {
    const float side = kLoupeCells * kLoupeZoom;
    const auto surface = surfaceSize();
    float x = mouse_.x + 22.f, y = mouse_.y + 22.f;
    if (surface.w > 0.f && x + side > surface.w) x = mouse_.x - 22.f - side;
    if (surface.h > 0.f && y + side + 24.f > surface.h) y = mouse_.y - 22.f - side - 24.f;
    return {x, y, side, side};
}

bool ColorPalette::swatchRect(std::string_view color, gfx::Rect& out) const {
    if (!open_) return false;
    for (const auto& c : cells())
        if (sameColor(c.color, color)) { out = c.r; return true; }
    return false;
}

gfx::Rect ColorPalette::eventBounds() const {
    if (!open_) return bounds();
    if (picking_ || drag_ != Drag::None) {
        // La pipette prend toute la fenetre : un clic n'importe ou est pour elle.
        // Un glisse dans le carre ou une barre aussi : il continue (borne) hors
        // de la palette, et le bouton lache n'importe ou le termine.
        const auto surface = surfaceSize();
        if (surface.w > 0.f && surface.h > 0.f) return {0.f, 0.f, surface.w, surface.h};
        return {-100000.f, -100000.f, 200000.f, 200000.f};
    }
    const auto b = bounds(), p = popupRect();
    const float x0 = std::min(b.x, p.x), y0 = std::min(b.y, p.y);
    const float x1 = std::max(b.right(), p.right()), y1 = std::max(b.bottom(), p.bottom());
    return {x0, y0, x1 - x0, y1 - y0};
}

// ----------------------------------------------------------------- choisir ---
void ColorPalette::setColor(gfx::Color c, bool keepHue) {
    color_ = c;
    double h = hue_, s = sat_, v = val_;
    hsvOf(c, h, s, v);
    if (!keepHue || s > 0.0) hue_ = h;
    sat_ = s;
    val_ = v;
    draft_ = colorCode(c);
    invalidate();
}

void ColorPalette::syncFromHsv() {
    picked_.clear();
    color_ = colorFromHsv(hue_, sat_, val_, color_.a);
    draft_ = colorCode(color_);
    if (field_ == Field::Hex) replaceOnType_ = true;
    fieldEdit_.clear();
    invalidate();
}

bool ColorPalette::draftValid() const {
    gfx::Color c;
    return draft_.empty() || draft_.front() == '=' || parseHexColor(draft_, c);
}

bool ColorPalette::editingExpression() const {
    return field_ == Field::Hex && !replaceOnType_ && !draft_.empty() && draft_.front() == '=';
}

void ColorPalette::pickSquare(gfx::Point p) {
    const auto s = squareRect();
    sat_ = std::clamp((p.x - s.x) / std::max(1.f, s.w), 0.f, 1.f);
    val_ = 1.0 - std::clamp((p.y - s.y) / std::max(1.f, s.h), 0.f, 1.f);
    syncFromHsv();
}

void ColorPalette::pickHue(gfx::Point p) {
    const auto h = hueRect();
    hue_ = 360.0 * std::clamp((p.x - h.x) / std::max(1.f, h.w), 0.f, 0.9999f);
    syncFromHsv();
}

void ColorPalette::pickAlpha(gfx::Point p) {
    // Transparente a gauche, opaque a droite (le dernier pixel : 255).
    const auto a = alphaRect();
    color_.a = clampByte(255.0 * std::clamp((p.x - a.x) / std::max(1.f, a.w - 1.f), 0.f, 1.f));
    syncFromHsv();
}

std::string ColorPalette::fieldText(Field f) const {
    if (f == Field::Hex) return draft_;
    if (f == field_ && !fieldEdit_.empty()) return fieldEdit_;
    int n = 0;
    switch (f) {
        case Field::R: n = color_.r; break;
        case Field::G: n = color_.g; break;
        case Field::B: n = color_.b; break;
        case Field::A: n = percentOf(color_.a); break;
        case Field::H: n = static_cast<int>(std::lround(hue_)) % 360; break;
        case Field::S: n = static_cast<int>(std::lround(sat_ * 100.0)); break;
        case Field::V: n = static_cast<int>(std::lround(val_ * 100.0)); break;
        case Field::Hex: break;
    }
    return std::to_string(n);
}

void ColorPalette::focusField(Field f) {
    if (f != field_) picked_.clear();
    field_ = f;
    fieldEdit_.clear();
    replaceOnType_ = true;
    invalidate();
}

void ColorPalette::fieldEdited() {
    picked_.clear();
    if (field_ == Field::Hex) {
        gfx::Color c;
        if (parseHexColor(draft_, c)) {
            const std::string typedCode = draft_;
            setColor(c, true);
            draft_ = typedCode;                // ce qui est tape reste tel quel
        }
        return;
    }
    if (fieldEdit_.empty()) return;
    const auto info = infoOf(field_);
    const int n = std::clamp(std::atoi(fieldEdit_.c_str()), 0, info.max);
    switch (field_) {
        case Field::R: case Field::G: case Field::B: {
            gfx::Color c = color_;
            (field_ == Field::R ? c.r : field_ == Field::G ? c.g : c.b) = static_cast<std::uint8_t>(n);
            setColor(c, true);
            break;
        }
        case Field::A: {
            gfx::Color c = color_;
            c.a = alphaOfPercent(n);
            setColor(c, true);
            break;
        }
        case Field::H: hue_ = n; color_ = colorFromHsv(hue_, sat_, val_, color_.a); draft_ = colorCode(color_); break;
        case Field::S: sat_ = n / 100.0; color_ = colorFromHsv(hue_, sat_, val_, color_.a); draft_ = colorCode(color_); break;
        case Field::V: val_ = n / 100.0; color_ = colorFromHsv(hue_, sat_, val_, color_.a); draft_ = colorCode(color_); break;
        case Field::Hex: break;
    }
    invalidate();
}

void ColorPalette::typed(const std::string& text) {
    if (field_ == Field::Hex) {
        if (replaceOnType_) { draft_.clear(); replaceOnType_ = false; }
        if (draft_.size() < 256) draft_ += text;
        fieldEdited();
        invalidate();
        return;
    }
    // Un champ nombre : des chiffres seulement.
    std::string digits;
    for (const char ch : text)
        if (ch >= '0' && ch <= '9') digits += ch;
    if (digits.empty()) return;
    if (replaceOnType_) { fieldEdit_.clear(); replaceOnType_ = false; }
    if (fieldEdit_.size() < 3) fieldEdit_ += digits.substr(0, 3 - fieldEdit_.size());
    fieldEdited();
}

void ColorPalette::erase() {
    if (field_ == Field::Hex) {
        if (replaceOnType_) { draft_.clear(); replaceOnType_ = false; invalidate(); return; }
        if (!draft_.empty()) {
            std::size_t cut = draft_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(draft_[cut]) & 0xC0) == 0x80) --cut;
            draft_.erase(cut);
            fieldEdited();
            invalidate();
        }
        return;
    }
    if (replaceOnType_) { fieldEdit_.clear(); replaceOnType_ = false; invalidate(); return; }
    if (fieldEdit_.empty()) fieldEdit_ = fieldText(field_);
    if (!fieldEdit_.empty()) fieldEdit_.pop_back();
    if (!fieldEdit_.empty()) fieldEdited();
    invalidate();
}

void ColorPalette::stepField(int delta) {
    if (field_ == Field::Hex) return;
    const auto info = infoOf(field_);
    int n = std::atoi(fieldText(field_).c_str()) + delta;
    if (field_ == Field::H) n = ((n % 360) + 360) % 360;
    else n = std::clamp(n, 0, info.max);
    fieldEdit_ = std::to_string(n);
    fieldEdited();
    fieldEdit_.clear();
    replaceOnType_ = true;
}

// ----------------------------------------------------------------- pipette ---
void ColorPalette::startPipette() {
    if (!open_ || picking_) return;
    picking_ = true;
    ++activePipettes();
    drag_ = Drag::None;
    hover_.clear();                           // l'aide du bas : la pipette, puis la couleur prise
    mouse_ = {popupRect().x + popupRect().w * 0.5f, popupRect().y + popupRect().h * 0.5f};
    invalidate();
}

void ColorPalette::cancelPipette() {
    if (!picking_) return;
    picking_ = false;
    --activePipettes();
    invalidate();
}

void ColorPalette::setPipetteImage(std::vector<std::uint8_t> rgba, int w, int h, float scale) {
    shotFixed_ = !rgba.empty() && w > 0 && h > 0 && rgba.size() >= static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
    shot_ = shotFixed_ ? std::move(rgba) : std::vector<std::uint8_t>{};
    shotW_ = shotFixed_ ? w : 0;
    shotH_ = shotFixed_ ? h : 0;
    shotScale_ = scale > 0.f ? scale : 1.f;
}

bool ColorPalette::pixelAt(gfx::Point p, gfx::Color& out) const {
    if (shot_.empty() || shotW_ <= 0 || shotH_ <= 0) return false;
    const int x = static_cast<int>(std::floor(p.x * shotScale_));
    const int y = static_cast<int>(std::floor(p.y * shotScale_));
    if (x < 0 || y < 0 || x >= shotW_ || y >= shotH_) return false;
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(shotW_) + static_cast<std::size_t>(x)) * 4;
    out = gfx::Color{shot_[i], shot_[i + 1], shot_[i + 2], 255};
    return true;
}

void ColorPalette::onFocusChanged(bool gained) {
    if (!gained) close();
}

EventResult ColorPalette::onEvent(const InputEvent& ev) {
    // ---- la pipette : toute la fenetre est a elle ----
    if (picking_) {
        if (const auto* m = std::get_if<MouseMove>(&ev)) { mouse_ = m->pos; invalidate(); return EventResult::Consumed; }
        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            mouse_ = d->pos;
            gfx::Color c;
            if (d->button == MouseButton::Left && pixelAt(d->pos, c)) {
                c.a = 255;
                setColor(c, false);
                field_ = Field::Hex;
                fieldEdit_.clear();
                replaceOnType_ = true;
                picked_ = hexOf(c);           // le bas le dit : prise, a appliquer
            }
            cancelPipette();                  // la fenetre revient (avec la couleur prise, ou sans)
            return EventResult::Consumed;
        }
        if (std::get_if<MouseUp>(&ev) || std::get_if<MouseWheel>(&ev) || std::get_if<TextInput>(&ev))
            return EventResult::Consumed;
        if (const auto* k = std::get_if<KeyDown>(&ev)) {
            if (k->key == Key::Escape) cancelPipette();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!open_) {
            if (!bounds().contains(d->pos)) return EventResult::Ignored;
            open();
            return EventResult::Consumed;
        }
        const auto popup = popupRect();
        if (!popup.contains(d->pos)) {
            // Un clic dehors ferme sans rien changer ; il va ensuite a ce qu'il visait.
            const bool onCell = bounds().contains(d->pos);
            close();
            return onCell ? EventResult::Consumed : EventResult::Ignored;
        }
        for (const auto& c : cells())
            if (c.r.contains(d->pos)) { choose(c.color); return EventResult::Consumed; }
        if (transparentRect().contains(d->pos)) { choose(std::string{}); return EventResult::Consumed; }
        if (squareRect().contains(d->pos)) { drag_ = Drag::Square; pickSquare(d->pos); return EventResult::Consumed; }
        if (hueRect().contains(d->pos)) { drag_ = Drag::Hue; pickHue(d->pos); return EventResult::Consumed; }
        if (alphaRect().contains(d->pos)) { drag_ = Drag::Alpha; pickAlpha(d->pos); return EventResult::Consumed; }
        if (okRect().contains(d->pos)) {
            if (draftValid()) choose(draft_);
            return EventResult::Consumed;
        }
        if (cancelRect().contains(d->pos)) { close(); return EventResult::Consumed; }
        if (pipetteRect().contains(d->pos)) { startPipette(); return EventResult::Consumed; }
        if (beforeRect().contains(d->pos)) {
            // La couleur d'origine, remise (une expression aussi).
            gfx::Color c;
            if (parseHexColor(origin_, c)) setColor(c, false);
            draft_ = origin_;
            focusField(Field::Hex);
            picked_.clear();
            return EventResult::Consumed;
        }
        for (const auto f : kFieldOrder)
            if (fieldShown(draft_, f) && fieldRect(f).contains(d->pos)) { focusField(f); return EventResult::Consumed; }
        return EventResult::Consumed;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev); m && open_) {
        if (drag_ == Drag::Square) { pickSquare(m->pos); return EventResult::Consumed; }
        if (drag_ == Drag::Hue) { pickHue(m->pos); return EventResult::Consumed; }
        if (drag_ == Drag::Alpha) { pickAlpha(m->pos); return EventResult::Consumed; }
        // La pastille survolee (encadree), ou la partie survolee : l'aide du bas
        // dit ce qu'elle fait (la maquette : ses infobulles).
        std::string over;
        for (const auto& c : cells()) if (c.r.contains(m->pos)) { over = c.color; break; }
        if (over.empty()) over = partAt(*this, m->pos);
        if (over != hover_) { hover_ = over; invalidate(); }
        return popupRect().contains(m->pos) ? EventResult::Consumed : EventResult::Ignored;
    }
    if (std::get_if<MouseUp>(&ev) && drag_ != Drag::None) {
        drag_ = Drag::None;
        return EventResult::Consumed;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev); w && open_) {
        // La molette sur une case nombre : un de plus, un de moins (Maj : dix) ;
        // ailleurs sur la palette, elle est gardee (la grille dessous ne defile pas).
        if (!popupRect().contains(w->pos)) return EventResult::Ignored;
        for (const auto f : kFieldOrder)
            if (f != Field::Hex && fieldShown(draft_, f) && fieldRect(f).contains(w->pos) && w->dy != 0.f) {
                if (f != field_) focusField(f);
                stepField((w->dy > 0.f ? 1 : -1) * (w->mods.shift ? 10 : 1));
                return EventResult::Consumed;
            }
        return EventResult::Consumed;
    }
    if (!open_ || !focused()) return EventResult::Ignored;
    if (const auto* t = std::get_if<TextInput>(&ev)) {
        // I : la pipette (sauf dans une expression, ou c'est une lettre).
        if ((t->utf8 == "i" || t->utf8 == "I") && !editingExpression()) {
            startPipette();
            return EventResult::Consumed;
        }
        typed(t->utf8);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        switch (k->key) {
            case Key::Escape: close(); return EventResult::Consumed;
            case Key::Return: {
                // Un code lisible, vide (transparent) ou une expression : applique.
                if (draftValid()) choose(draft_);
                return EventResult::Consumed;
            }
            case Key::Tab: {
                // Le champ suivant (Maj : le precedent) ; T S V sautes devant une expression.
                std::size_t i = 0;
                while (i < std::size(kFieldOrder) && kFieldOrder[i] != field_) ++i;
                const std::size_t n = std::size(kFieldOrder);
                do i = k->mods.shift ? (i + n - 1) % n : (i + 1) % n;
                while (!fieldShown(draft_, kFieldOrder[i]));
                focusField(kFieldOrder[i]);
                return EventResult::Consumed;
            }
            case Key::Up: stepField(k->mods.shift ? 10 : 1); return EventResult::Consumed;
            case Key::Down: stepField(k->mods.shift ? -10 : -1); return EventResult::Consumed;
            case Key::A:
                if (!k->mods.ctrl) break;
                replaceOnType_ = true;                        // Ctrl+A : tout le champ
                invalidate();
                return EventResult::Consumed;
            case Key::Backspace: erase(); return EventResult::Consumed;
            default: break;
        }
    }
    return EventResult::Ignored;
}

// ---------------------------------------------------------------- dessin -----
void ColorPalette::onPaint(const PaintContext& ctx) {
    // La case elle-meme : la pastille, puis le code en cours.
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    ctx.r.fillRect(b, c.inputBg);
    ctx.r.strokeRect(b, c.accent, 1.f);
    const gfx::Rect sw{b.x + 5.f, b.y + (b.h - 14.f) * 0.5f, 14.f, 14.f};
    gfx::Color col;
    if (parseHexColor(draft_, col)) {
        if (col.a < 255) checker(ctx.r, sw);
        ctx.r.fillRect(sw, col);
    } else {
        checker(ctx.r, sw);
    }
    ctx.r.strokeRect(sw, c.border, 1.f);
    const float ty = b.y + (b.h - ctx.r.lineHeight(kFont)) * 0.5f;
    ctx.r.drawText({sw.right() + 6.f, ty}, draft_.empty() ? std::string("(transparent)") : draft_, kFont,
                   draft_.empty() ? c.textMuted : c.text);
}

std::string ColorPalette::hint() const {
    if (picking_)
        return "Pipette : clique une couleur n'importe o\xC3\xB9\ndans la fen\xC3\xAAtre. \xC3\x89" "chap : annuler.";
    // Ce que la souris survole (une pastille, une partie).
    if (hover_ == "@pipette") return "Pipette (touche I) : prendre une couleur\nn'importe o\xC3\xB9 dans la fen\xC3\xAAtre.";
    if (hover_ == "@avant")
        return "avant : la couleur d'origine (" + (origin_.empty() ? std::string("transparent") : origin_) + ").\nUn clic la remet.";
    if (hover_ == "@apres")
        return "apr\xC3\xA8s : la couleur pr\xC3\xA9par\xC3\xA9" "e (" + (draft_.empty() ? std::string("transparent") : draft_)
             + ").\nAppliquer (Entr\xC3\xA9" "e) la garde.";
    if (hover_ == "@transparent") return "Transparent : aucune couleur (valeur vide).\nUn clic l'applique.";
    if (hover_ == "@carre") return "Carr\xC3\xA9 : la saturation de gauche \xC3\xA0 droite,\nla valeur (la clart\xC3\xA9) de bas en haut.";
    if (hover_ == "@teinte") return "Teinte : de 0 \xC3\xA0 359\xC2\xB0 (rouge, jaune, vert,\ncyan, bleu, magenta). Clic ou gliss\xC3\xA9.";
    if (hover_ == "@alpha") return "Transparence : transparente \xC3\xA0 gauche,\nopaque \xC3\xA0 droite (code en #RRGGBBAA).";
    if (!hover_.empty() && hover_.front() == '#') return hover_ + "\nUn clic l'applique et ferme la palette.";
    if (!picked_.empty()) return "Pipette : " + picked_ + " prise.\nAppliquer pour la garder.";
    switch (field_) {
        case Field::Hex:
            if (!fieldShown(draft_, Field::H))
                return "Expression : Appliquer la garde telle quelle.\nUn clic dans le carr\xC3\xA9 revient \xC3\xA0 une couleur.";
            return "Code : #RRGGBB ou #RRGGBBAA ;\nvide = transparent, = pour une expression.";
        case Field::R: case Field::G: case Field::B: return "R G B : rouge, vert, bleu (0 \xC3\xA0 255).\nHaut, Bas : un de plus, de moins (Maj : 10).";
        case Field::A: return "Opacit\xC3\xA9 : 100 % opaque, 0 % transparent\n(le code passe alors en #RRGGBBAA).";
        case Field::H: case Field::S: case Field::V: return "T S V : teinte (0 \xC3\xA0 359\xC2\xB0),\nsaturation et valeur (0 \xC3\xA0 100 %).";
    }
    return {};
}

void ColorPalette::paintPipette(const PaintContext& ctx) {
    // L'image que l'application vient de dessiner (la palette comprise, pas
    // la loupe) : relue a chaque image, la vue en marche bouge sous la pipette.
    if (!shotFixed_) {
        int w = 0, h = 0;
        if (ctx.r.readPixels(shot_, w, h) && w > 0 && h > 0) {
            shotW_ = w;
            shotH_ = h;
            shotScale_ = ctx.clip.w > 0.f ? static_cast<float>(w) / ctx.clip.w : 1.f;
        } else {
            shot_.clear();
            shotW_ = shotH_ = 0;
        }
    }
    const auto& c = ctx.theme.color;
    // La croix, a la place du curseur.
    const gfx::Point m = mouse_;
    for (const auto& [col, t] : {std::pair{gfx::Color{0, 0, 0, 200}, 3.f}, std::pair{gfx::Color{255, 255, 255, 255}, 1.f}}) {
        ctx.r.line({m.x - 12.f, m.y}, {m.x - 3.f, m.y}, col, t);
        ctx.r.line({m.x + 3.f, m.y}, {m.x + 12.f, m.y}, col, t);
        ctx.r.line({m.x, m.y - 12.f}, {m.x, m.y - 3.f}, col, t);
        ctx.r.line({m.x, m.y + 3.f}, {m.x, m.y + 12.f}, col, t);
    }
    // La loupe : les pixels autour, agrandis ; celui du milieu encadre ; dessous
    // sa pastille et son code.
    const auto l = loupeRect();
    ctx.r.fillRect({l.x + 3.f, l.y + 3.f, l.w, l.h + 24.f}, gfx::Color{0, 0, 0, 90});
    ctx.r.fillRect({l.x, l.y, l.w, l.h + 24.f}, c.panelBg);
    const int half = static_cast<int>(kLoupeCells) / 2;
    for (int j = -half; j <= half; ++j)
        for (int i = -half; i <= half; ++i) {
            gfx::Color px{60, 60, 60, 255};
            (void)pixelAt({m.x + static_cast<float>(i) / shotScale_, m.y + static_cast<float>(j) / shotScale_}, px);
            ctx.r.fillRect({l.x + static_cast<float>(i + half) * kLoupeZoom, l.y + static_cast<float>(j + half) * kLoupeZoom,
                            kLoupeZoom, kLoupeZoom}, px);
        }
    const gfx::Rect mid{l.x + half * kLoupeZoom, l.y + half * kLoupeZoom, kLoupeZoom, kLoupeZoom};
    ctx.r.strokeRect({mid.x - 1.f, mid.y - 1.f, mid.w + 2.f, mid.h + 2.f}, gfx::Color{0, 0, 0, 255}, 1.f);
    ctx.r.strokeRect(mid, gfx::Color{255, 255, 255, 255}, 1.f);
    ctx.r.strokeRect({l.x, l.y, l.w, l.h + 24.f}, c.accent, 1.f);
    gfx::Color under;
    const bool ok = pixelAt(m, under);
    const std::string code = ok ? hexOf(under) : std::string("\xE2\x80\x94");
    if (ok) ctx.r.fillRect({l.x + 5.f, l.bottom() + 6.f, 12.f, 12.f}, under);
    ctx.r.drawText({l.x + 22.f, l.bottom() + (24.f - ctx.r.lineHeight(kSmall)) * 0.5f}, code, kSmall, c.text);

    // Ce qu'il faut faire : un bandeau en bas, au milieu de la fenetre.
    {
        const std::string say = "Pipette : clique une couleur n'importe o\xC3\xB9 dans la fen\xC3\xAAtre \xC2\xB7 \xC3\x89" "chap pour annuler";
        auto surface = surfaceSize();
        if (surface.w <= 0.f || surface.h <= 0.f) surface = {ctx.clip.w, ctx.clip.h};
        const float tw = ctx.r.measure(say, kSmall).width;
        const gfx::Rect bar{std::floor((surface.w - tw) * 0.5f) - 14.f, surface.h - 74.f, tw + 28.f, 32.f};
        ctx.r.fillRoundedRect(bar, c.panelBg, 4.f);
        ctx.r.strokeRect(bar, c.border, 1.f);
        ctx.r.fillRect({bar.x, bar.y, 3.f, bar.h}, c.accent);
        ctx.r.drawText({bar.x + 14.f, bar.y + (bar.h - ctx.r.lineHeight(kSmall)) * 0.5f}, say, kSmall, c.text);
    }
}

void ColorPalette::onPaintOverlay(const PaintContext& ctx) {
    if (!open_) return;
    paintBody(ctx);                         // la palette reste ouverte pendant la pipette
    if (picking_) paintPipette(ctx);        // ... qui lit l'image apres elle, avant la loupe
}

void ColorPalette::paintBody(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto p = popupRect();
    ctx.r.fillRect({p.x + 3.f, p.y + 3.f, p.w, p.h}, gfx::Color{0, 0, 0, 90});
    ctx.r.fillRect(p, c.panelBg);
    ctx.r.strokeRect(p, c.accent, 1.f);

    auto label = [&](float x, float y, std::string_view text) {
        ctx.r.drawText({x, y + 1.f}, text, kSmall, c.textMuted);
    };
    auto centered = [&](const gfx::Rect& r, std::string_view text, gfx::FontId font, gfx::Color ink) {
        const auto mt = ctx.r.measure(text, font);
        ctx.r.drawText({r.x + (r.w - mt.width) * 0.5f, r.y + (r.h - ctx.r.lineHeight(font)) * 0.5f}, text, font, ink);
    };

    // ---- le titre : la propriete, ce qu'elle colore ; la pipette a droite ----
    {
        const std::string title = title_.empty() ? std::string("Couleur") : title_;
        const float ty = p.y + (kHeadH - ctx.r.lineHeight(kFont)) * 0.5f;
        ctx.r.drawText({p.x + kPad + 2.f, ty}, title, kFont, c.text);
        if (!subject_.empty()) {
            const float x = p.x + kPad + 2.f + ctx.r.measure(title, kFont).width + 10.f;
            const auto pb = pipetteRect();
            ctx.r.pushClip({x, p.y, std::max(0.f, pb.x - 8.f - x), kHeadH});
            ctx.r.drawText({x, p.y + (kHeadH - ctx.r.lineHeight(kSmall)) * 0.5f}, subject_, kSmall, c.textMuted);
            ctx.r.popClip();
        }
        const auto b = pipetteRect();
        ctx.r.fillRoundedRect(b, picking_ ? c.accent : c.inputBg, 3.f);
        ctx.r.strokeRect(b, picking_ ? c.accent : hover_ == "@pipette" ? c.accentHover : c.border, 1.f);
        const gfx::Color ink = picking_ ? c.selectionText : c.text;
        pipetteGlyph(ctx.r, {b.x + 10.f, b.y + 6.f, 16.f, 16.f}, ink);
        ctx.r.drawText({b.x + 34.f, b.y + (b.h - ctx.r.lineHeight(kFont)) * 0.5f}, "Pipette", kFont, ink);
        ctx.r.fillRect({p.x + 1.f, p.y + kHeadH, p.w - 2.f, 1.f}, c.border);
    }

    // ---- a gauche : le nuancier, le projet, les recentes, transparent ----
    label(p.x + kPad, p.y + sectionY(0), "Nuancier");
    if (!project_.empty()) label(p.x + kPad, p.y + sectionY(1), "Dans le projet");
    if (!recent().empty()) label(p.x + kPad, p.y + sectionY(2), "R\xC3\xA9" "centes");
    for (const auto& cell : cells()) {
        gfx::Color col;
        if (!parseHexColor(cell.color, col)) continue;
        if (col.a < 255) checker(ctx.r, cell.r);
        ctx.r.fillRect(cell.r, col);
        const bool current = sameColor(cell.color, value_);
        const bool over = sameColor(cell.color, hover_);
        ctx.r.strokeRect(cell.r, current ? c.text : over ? c.accentHover : c.border, current || over ? 2.f : 1.f);
    }
    {
        // Transparent : un damier barre.
        const auto t = transparentRect();
        const gfx::Rect box{t.x, t.y, kSwatch, kSwatch};
        checker(ctx.r, box);
        ctx.r.line({box.x, box.bottom()}, {box.right(), box.y}, gfx::Color{231, 76, 60, 255}, 2.f);
        ctx.r.strokeRect(box, value_.empty() ? c.text : c.border, value_.empty() ? 2.f : 1.f);
        ctx.r.drawText({box.right() + 6.f, t.y + (kSwatch - ctx.r.lineHeight(kSmall)) * 0.5f}, "Transparent", kSmall, c.text);
    }
    {
        // Avant / apres : la couleur d'origine (un clic la remet), la couleur preparee.
        const auto was = beforeRect(), now = afterRect();
        label(was.x, was.y - kLabel, "Avant / apr\xC3\xA8s");
        checker(ctx.r, {was.x, was.y, was.w + now.w, was.h});
        gfx::Color col;
        gfx::Color wasInk{40, 40, 40, 255}, nowInk{40, 40, 40, 255};
        if (parseHexColor(origin_, col)) { ctx.r.fillRect(was, col); wasInk = inkOn(col); }
        else if (!origin_.empty() && origin_.front() == '=') centered({was.x, was.y, was.w, was.h - 14.f}, "= fx", kSmall, wasInk);
        if (parseHexColor(draft_, col)) { ctx.r.fillRect(now, col); nowInk = inkOn(col); }
        else if (!draft_.empty() && draft_.front() == '=') centered({now.x, now.y, now.w, now.h - 14.f}, "= fx", kSmall, nowInk);
        centered({was.x, was.bottom() - 17.f, was.w, 16.f}, "avant", kSmall, wasInk);
        centered({now.x, now.bottom() - 17.f, now.w, 16.f}, "apr\xC3\xA8s", kSmall, nowInk);
        ctx.r.strokeRect({was.x, was.y, was.w + now.w, was.h}, c.border, 1.f);
        if (hover_ == "@avant") ctx.r.strokeRect(was, c.accentHover, 2.f);      // un clic la remet
    }

    // La separation des deux colonnes.
    const auto o = rightColumn();
    ctx.r.fillRect({o.x - kColGap * 0.5f, o.y, 1.f, kBodyH}, c.border);

    // ---- a droite : le carre saturation (x) / valeur (y) de la teinte ----
    const auto s = squareRect();
    std::vector<gfx::Vertex> tri;
    {
        constexpr int nx = 24, ny = 12;
        tri.reserve(nx * ny * 6);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const double s0 = static_cast<double>(i) / nx, s1 = static_cast<double>(i + 1) / nx;
                const double v0 = 1.0 - static_cast<double>(j) / ny, v1 = 1.0 - static_cast<double>(j + 1) / ny;
                const float x0 = s.x + s.w * static_cast<float>(s0), x1 = s.x + s.w * static_cast<float>(s1);
                const float y0 = s.y + s.h * static_cast<float>(j) / ny, y1 = s.y + s.h * static_cast<float>(j + 1) / ny;
                const gfx::Vertex a{{x0, y0}, colorFromHsv(hue_, s0, v0)}, bb{{x1, y0}, colorFromHsv(hue_, s1, v0)};
                const gfx::Vertex cc{{x1, y1}, colorFromHsv(hue_, s1, v1)}, dd{{x0, y1}, colorFromHsv(hue_, s0, v1)};
                tri.insert(tri.end(), {a, bb, cc, a, cc, dd});
            }
        ctx.r.fillTriangles(tri.data(), tri.size());
        ctx.r.strokeRect(s, c.border, 1.f);
        // Le repere : un anneau blanc cerne de noir, la couleur dedans.
        const float mx = s.x + s.w * static_cast<float>(sat_), my = s.y + s.h * static_cast<float>(1.0 - val_);
        ctx.r.fillRoundedRect({mx - 8.f, my - 8.f, 16.f, 16.f}, gfx::Color{0, 0, 0, 170}, 8.f);
        ctx.r.fillRoundedRect({mx - 7.f, my - 7.f, 14.f, 14.f}, gfx::Color{255, 255, 255, 255}, 7.f);
        ctx.r.fillRoundedRect({mx - 5.f, my - 5.f, 10.f, 10.f}, colorFromHsv(hue_, sat_, val_), 5.f);
    }
    // La barre des teintes, de gauche (0) a droite (360) ; sa poignee.
    auto handle = [&](const gfx::Rect& bar, float x) {
        const gfx::Rect k{x - 3.f, bar.y - 2.f, 6.f, bar.h + 4.f};
        ctx.r.strokeRect({k.x - 1.f, k.y - 1.f, k.w + 2.f, k.h + 2.f}, gfx::Color{0, 0, 0, 200}, 1.f);
        ctx.r.strokeRect(k, gfx::Color{255, 255, 255, 255}, 2.f);
    };
    {
        const auto h = hueRect();
        tri.clear();
        constexpr int nh = 12;
        for (int k = 0; k < nh; ++k) {
            const double h0 = 360.0 * k / nh, h1 = 360.0 * (k + 1) / nh;
            const float x0 = h.x + h.w * static_cast<float>(k) / nh, x1 = h.x + h.w * static_cast<float>(k + 1) / nh;
            const gfx::Vertex a{{x0, h.y}, colorFromHsv(h0, 1, 1)}, bb{{x1, h.y}, colorFromHsv(h1, 1, 1)};
            const gfx::Vertex cc{{x1, h.bottom()}, colorFromHsv(h1, 1, 1)}, dd{{x0, h.bottom()}, colorFromHsv(h0, 1, 1)};
            tri.insert(tri.end(), {a, bb, cc, a, cc, dd});
        }
        ctx.r.fillTriangles(tri.data(), tri.size());
        ctx.r.strokeRect(h, c.border, 1.f);
        handle(h, h.x + h.w * static_cast<float>(hue_ / 360.0));
    }
    {
        // La transparence : la couleur, transparente a gauche, opaque a droite, sur un damier.
        const auto a = alphaRect();
        checker(ctx.r, a);
        constexpr int na = 40;
        for (int k = 0; k < na; ++k) {
            gfx::Color step = color_;
            step.a = clampByte(255.0 * (k + 0.5) / na);
            ctx.r.fillRect({a.x + a.w * static_cast<float>(k) / na, a.y, a.w / na + 0.5f, a.h}, step);
        }
        ctx.r.strokeRect(a, c.border, 1.f);
        handle(a, a.x + (a.w - 1.f) * static_cast<float>(color_.a) / 255.f);
    }

    // ---- les champs : Code T S V, R G B Opacite % (le libelle au-dessus) ----
    for (const auto f : kFieldOrder) {
        if (!fieldShown(draft_, f)) continue;         // T S V : pas devant une expression
        const auto r = fieldRect(f);
        const auto info = infoOf(f);
        const bool active = f == field_ && focused();
        const bool expression = f == Field::Hex && !fieldShown(draft_, Field::H);
        ctx.r.drawText({r.x, r.y - kFieldLabel}, expression ? "Expression" : info.label, kSmall, c.textMuted);
        ctx.r.fillRect(r, c.inputBg);
        ctx.r.strokeRect(r, active ? c.accent : c.border, 1.f);
        const std::string text = fieldText(f);
        const float ty = r.y + (r.h - ctx.r.lineHeight(kFont)) * 0.5f;
        const float tw = ctx.r.measure(text, kFont).width;
        // Un texte plus long que la case (une expression) : on en voit la fin.
        const float tx = tw > r.w - 12.f && active && !replaceOnType_ ? r.right() - 6.f - tw : r.x + 6.f;
        if (active && replaceOnType_ && !text.empty())
            ctx.r.fillRect({r.x + 4.f, r.y + 4.f, std::min(r.w - 8.f, tw + 4.f), r.h - 8.f}, c.selectionBg);
        ctx.r.pushClip({r.x + 2.f, r.y, r.w - 4.f, r.h});
        ctx.r.drawText({tx, ty}, text, kFont, f == Field::Hex && !draftValid() ? c.textMuted : c.text);
        if (f == Field::Hex && text.empty())
            ctx.r.drawText({r.x + 6.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, "transparent", kSmall, c.textDisabled);
        ctx.r.popClip();
        if (active && !replaceOnType_ && std::fmod(ctx.time, 1.0) < 0.5) {
            const float cx = std::min(r.right() - 3.f, tx + tw + 1.f);
            ctx.r.fillRect({cx, r.y + 4.f, 1.f, r.h - 8.f}, c.text);
        }
    }

    // ---- en bas : ce que veut dire le champ actif ; Annuler, Appliquer ----
    {
        const float fy = p.bottom() - kFootH;
        ctx.r.fillRect({p.x + 1.f, fy, p.w - 2.f, 1.f}, c.border);
        const auto cancel = cancelRect(), ok = okRect();
        const std::string say = hint();
        const std::size_t cut = say.find('\n');
        const float lh = ctx.r.lineHeight(kSmall);
        const float hy = fy + (kFootH - 2.f * lh) * 0.5f;
        ctx.r.pushClip({p.x + kPad, fy, std::max(0.f, cancel.x - 8.f - p.x - kPad), kFootH});
        ctx.r.drawText({p.x + kPad, hy}, say.substr(0, cut), kSmall, c.textMuted);
        if (cut != std::string::npos) ctx.r.drawText({p.x + kPad, hy + lh}, say.substr(cut + 1), kSmall, c.textMuted);
        ctx.r.popClip();
        ctx.r.fillRoundedRect(cancel, c.inputBg, 3.f);
        ctx.r.strokeRect(cancel, c.border, 1.f);
        centered(cancel, "Annuler", kFont, c.text);
        ctx.r.fillRoundedRect(ok, draftValid() ? c.accent : c.border, 3.f);
        centered(ok, "Appliquer", kFont, c.selectionText);
    }
}

} // namespace ui
