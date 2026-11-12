// =============================================================================
//  tests/session_ui_test.cpp - 1.47.0 : LE TEXTE DANS SON CADRE, A TOUTES LES TAILLES
// -----------------------------------------------------------------------------
//  Le client (10/11), sur une capture de la fenetre de la session (vue Journal,
//  une fenetre de 943 x 609) : << bug a corriger, le texte sort du cadre,
//  attention au redimensionnement >>. Puis (le lead) : << Mon profil... >>
//  coupe en << Mon pr... >> dans l'encart de la licence de l'accueil.
//
//  Ce que l'essai dessine (sans fenetre, sans SDL) avec un renderer logiciel
//  qui mesure avec la VRAIE police du logiciel (resources/fonts, FontAtlas) et
//  note chaque texte dessine, son rectangle et la decoupe en cours :
//   - l'onglet << Session >> du panneau du bas (app/hmi/HmiSessionPane), de la
//     plus petite taille que le panneau permet a tres grand ;
//   - la fenetre de la session (app/SessionWindow : l'equipe, la discussion, le
//     journal, les boutons), d'une fenetre etroite a une tres grande ;
//   - l'encart de la licence de l'accueil (app/StartPage : << Mon profil... >>
//     et le bouton de la licence).
//  A chaque taille : aucun texte hors de son cadre (ni a droite, ni dessous),
//  aucun bouton hors du cadre, deux boutons ne se chevauchent pas, le libelle de
//  chaque bouton tient dans le bouton ; les mots ne sont coupes que s'ils sont
//  plus longs que la ligne.
//
//  1.50.0 : la discussion - repondre (l'extrait cite au-dessus de la reponse, la
//  barre << En reponse a ... >> au-dessus du champ), reduire / deballer (un
//  message de huit lignes), copier : tout cela aussi dans le cadre a chaque
//  taille (l'onglet, la fenetre, une reponse en cours) ; puis, entre deux membres
//  (les messages passes par le protocole : writeChat / readChat), la reference
//  d'une reponse, un message a l'ancienne, l'etat reduit qui suit le message,
//  le texte copie (session_ui_discussion.inl).
//
//  --captures <dossier> [--prefixe P] [--ancien] : en plus, des images PNG (le meme dessin
//  logiciel, rasterise : ce n'est PAS une capture de la fenetre de l'application)
//  aux tailles etroite, moyenne et large, avec des noms et des messages longs
//  (fictifs : Camille Essai, Lea Essai...). --ancien : les memes images avec le code de la 1.46 (la fenetre de la
//  session et l'onglet Session, recopies dans session_ui_ancien.inl) - les images << avant >>.
// =============================================================================
#include "../src/app/Capture.hpp"
#include "../src/app/SessionMarks.hpp"
#include "../src/app/StartPage.hpp"
#include "../src/app/hmi/HmiSessionPane.hpp"
#include "../src/platform/FontAtlas.hpp"
#include "../src/session/SessionProtocol.hpp"   // 1.50.0 : les messages passes par le protocole
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"   // le tableau de l'onglet Session de la 1.46 (les images << avant >>)
#if __has_include("../src/app/SessionWindow.hpp")
#include "../src/app/SessionWindow.hpp"
#define XPG_SESSION_WINDOW 1
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0, g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("ECHEC : %s\n", what.c_str());
    }
}

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
    return b;
}

std::string rectText(const gfx::Rect& r) { return "{" + num(r.x) + "," + num(r.y) + " " + num(r.w) + "x" + num(r.h) + "}"; }

// ------------------------------------------------------------------ le renderer logiciel ----
//  Les mesures de la vraie police (une FontAtlas par taille et graisse, comme SdlRenderer) ; chaque texte dessine
//  est note (son rectangle, la decoupe) ; avec une image (capture), tout est aussi rasterise en RGBA.
class SoftRenderer final : public gfx::IRenderer {
public:
    struct Text {
        gfx::Rect   rect;   // l'origine, la largeur mesuree, la hauteur de ligne
        gfx::Rect   clip;   // la decoupe en cours
        std::string text;
    };

    SoftRenderer(int w, int h, bool raster) : w_(w), h_(h), raster_(raster) {
        if (raster_) px_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) * 4u, 0);
    }

    void beginFrame(gfx::Color clear) override {
        texts.clear();
        clips_.clear();
        if (raster_)
            for (std::size_t i = 0; i < px_.size(); i += 4) {
                px_[i] = clear.r;
                px_[i + 1] = clear.g;
                px_[i + 2] = clear.b;
                px_[i + 3] = 255;
            }
    }
    void endFrame() override {}
    void pushClip(const gfx::Rect& r) override { clips_.push_back(clips_.empty() ? r : clips_.back().intersect(r)); }
    void popClip() override {
        if (!clips_.empty()) clips_.pop_back();
    }

    void fillRect(const gfx::Rect& r, gfx::Color c) override {
        if (!raster_ || c.a == 0) return;
        const gfx::Rect a = clipped(r);
        for (int y = lo(a.y); y < hi(a.bottom()); ++y)
            for (int x = lo(a.x); x < hi(a.right()); ++x) blend(x, y, c, 1.f);
    }
    void strokeRect(const gfx::Rect& r, gfx::Color c, float t) override {
        fillRect({r.x, r.y, r.w, t}, c);
        fillRect({r.x, r.bottom() - t, r.w, t}, c);
        fillRect({r.x, r.y + t, t, r.h - 2 * t}, c);
        fillRect({r.right() - t, r.y + t, t, r.h - 2 * t}, c);
    }
    void fillRoundedRect(const gfx::Rect& r, gfx::Color c, float radius) override {
        if (!raster_ || c.a == 0) return;
        const float rad = std::max(0.f, std::min(radius, std::min(r.w, r.h) * 0.5f));
        const gfx::Rect a = clipped(r);
        for (int y = lo(a.y); y < hi(a.bottom()); ++y)
            for (int x = lo(a.x); x < hi(a.right()); ++x) {
                const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                float cover = 1.f;
                const float cx = px < r.x + rad ? r.x + rad : px > r.right() - rad ? r.right() - rad : px;
                const float cy = py < r.y + rad ? r.y + rad : py > r.bottom() - rad ? r.bottom() - rad : py;
                if (cx != px && cy != py) {
                    const float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
                    cover = std::clamp(rad - d + 0.5f, 0.f, 1.f);
                }
                if (cover > 0.f) blend(x, y, c, cover);
            }
    }
    void line(gfx::Point a, gfx::Point b, gfx::Color c, float t) override {
        if (!raster_) return;
        const float dx = b.x - a.x, dy = b.y - a.y;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
        for (int i = 0; i <= steps; ++i) {
            const float x = a.x + dx * static_cast<float>(i) / static_cast<float>(steps);
            const float y = a.y + dy * static_cast<float>(i) / static_cast<float>(steps);
            fillRect({x - t * 0.5f, y - t * 0.5f, std::max(1.f, t), std::max(1.f, t)}, c);
        }
    }
    void fillTriangles(const gfx::Vertex* v, std::size_t count) override {
        if (!raster_) return;
        for (std::size_t i = 0; i + 2 < count; i += 3) triangle(v[i], v[i + 1], v[i + 2]);
    }
    void drawText(gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color c) override {
        if (s.empty()) return;
        const gfx::Rect clip = clips_.empty() ? gfx::Rect{-1e6f, -1e6f, 2e6f, 2e6f} : clips_.back();
        texts.push_back({{at.x, at.y, measure(s, f).width, lineHeight(f)}, clip, std::string(s)});
        if (!raster_) return;
        gfx::FontAtlas* face = faceFor(f);
        if (!face) return;
        float pen = at.x;
        const float base = at.y + face->ascent();
        for (std::size_t i = 0; i < s.size();) {
            const char32_t code = gfx::FontAtlas::decode(s, i);
            if (code == 0) break;
            const gfx::Glyph& g = face->glyph(code);
            const auto& bits = face->pixels();
            const int gx = static_cast<int>(std::round(pen + g.bearingX)), gy = static_cast<int>(std::round(base + g.bearingY));
            for (int y = 0; y < g.h; ++y)
                for (int x = 0; x < g.w; ++x) {
                    const std::uint8_t cov = bits[static_cast<std::size_t>(g.y + y) * face->width() + static_cast<std::size_t>(g.x + x)];
                    const int X = gx + x, Y = gy + y;
                    if (cov && inside(X, Y)) blend(X, Y, c, static_cast<float>(cov) / 255.f);
                }
            pen += g.advance;
        }
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}

    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        gfx::FontAtlas* face = faceFor(f);
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        if (!face) return {static_cast<float>(s.size()) * h * 0.5f, h, h * 0.8f, h * 0.2f};
        gfx::TextMetrics m;
        m.width = face->measure(s);
        m.height = face->lineHeight();
        return m;
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override {
        gfx::FontAtlas* face = faceFor(f);
        return face ? face->lineHeight() : (f.v ? static_cast<float>(f.v) : 16.f);
    }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {static_cast<float>(w_), static_cast<float>(h_)}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float maxWidth) const override {
        if (maxWidth <= 0.f) return 0;
        gfx::FontAtlas* face = faceFor(f);
        return face ? face->fitBytes(s, maxWidth) : std::min(s.size(), static_cast<std::size_t>(maxWidth / 8.f));
    }

    bool savePng(const std::string& path) const {
        std::vector<std::uint8_t> png;
        if (!app::encodePng(px_, w_, h_, png)) return false;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return static_cast<bool>(out);
    }
    [[nodiscard]] bool fontsReady() const { return faceFor(gfx::FontId{16}) != nullptr; }

    std::vector<Text> texts;

private:
    gfx::FontAtlas* faceFor(gfx::FontId f) const {
        const int px = std::max(1, static_cast<int>(f.v ? f.v : 16));
        const int key = px * 4 + f.w;
        if (auto it = faces_.find(key); it != faces_.end()) return it->second.ready() ? &it->second : nullptr;
        gfx::FontAtlas atlas;
        for (const auto& path : gfx::interfaceFontCandidates(f.w))
            if (atlas.load(path, static_cast<float>(px))) break;
        auto& kept = faces_.emplace(key, std::move(atlas)).first->second;
        return kept.ready() ? &kept : nullptr;
    }
    gfx::Rect clipped(const gfx::Rect& r) const {
        gfx::Rect a = r.intersect({0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_)});
        if (!clips_.empty()) a = a.intersect(clips_.back());
        return a;
    }
    static int lo(float v) { return static_cast<int>(std::floor(v + 0.5f)); }
    static int hi(float v) { return static_cast<int>(std::floor(v + 0.5f)); }
    bool inside(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return false;
        if (clips_.empty()) return true;
        const auto& c = clips_.back();
        return static_cast<float>(x) + 0.5f >= c.x && static_cast<float>(x) + 0.5f < c.right() && static_cast<float>(y) + 0.5f >= c.y &&
               static_cast<float>(y) + 0.5f < c.bottom();
    }
    void blend(int x, int y, gfx::Color c, float cover) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        const float a = cover * static_cast<float>(c.a) / 255.f;
        std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)) * 4u];
        p[0] = static_cast<std::uint8_t>(std::lround(p[0] * (1.f - a) + c.r * a));
        p[1] = static_cast<std::uint8_t>(std::lround(p[1] * (1.f - a) + c.g * a));
        p[2] = static_cast<std::uint8_t>(std::lround(p[2] * (1.f - a) + c.b * a));
        p[3] = 255;
    }
    void triangle(const gfx::Vertex& a, const gfx::Vertex& b, const gfx::Vertex& c) {
        const float minX = std::min({a.pos.x, b.pos.x, c.pos.x}), maxX = std::max({a.pos.x, b.pos.x, c.pos.x});
        const float minY = std::min({a.pos.y, b.pos.y, c.pos.y}), maxY = std::max({a.pos.y, b.pos.y, c.pos.y});
        const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
        if (std::fabs(area) < 1e-6f) return;
        for (int y = static_cast<int>(std::floor(minY)); y <= static_cast<int>(std::ceil(maxY)); ++y)
            for (int x = static_cast<int>(std::floor(minX)); x <= static_cast<int>(std::ceil(maxX)); ++x) {
                const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                const float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
                const float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
                const float w2 = 1.f - w0 - w1;
                if (w0 >= 0.f && w1 >= 0.f && w2 >= 0.f && inside(x, y)) blend(x, y, a.color, 1.f);
            }
    }

    int w_, h_;
    bool raster_;
    std::vector<std::uint8_t> px_;
    std::vector<gfx::Rect> clips_;
    mutable std::map<int, gfx::FontAtlas> faces_;
};

// Les mesures de la mise en page (ui::measureWidth) sont celles du dessin. 1.50.0 : le presse-papiers est note (Copier).
SoftRenderer* g_metrics = nullptr;
std::string g_clipboard;
void installMetrics(SoftRenderer& r) {
    g_metrics = &r;
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return g_metrics->measure(t, f).width; };
    s.lineHeight = [](gfx::FontId f) { return g_metrics->lineHeight(f); };
    s.surfaceSize = [] { return g_metrics->surfaceSize(); };
    s.clipboardGet = [] { return g_clipboard; };
    s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
    ui::installPlatformServices(std::move(s));
}

std::string g_captures, g_prefix;
bool g_ancien = false;   // --ancien : seulement les images << avant >> (le code de la 1.46, session_ui_ancien.inl)

void paintInto(SoftRenderer& r, ui::Widget& w, const ui::Theme& theme, gfx::Rect surface) {
    r.beginFrame(theme.color.windowBg);
    w.layout();
    std::vector<ui::Widget*> overlays;
    w.render(ui::PaintContext{r, theme, surface, 0.0, &overlays});
}

// Une capture : le widget pose a `frame` dans une image de `size`.
void capture(ui::Widget& w, gfx::Size size, const std::string& name) {
    if (g_captures.empty()) return;
    SoftRenderer img(static_cast<int>(size.w), static_cast<int>(size.h), true);
    SoftRenderer* before = g_metrics;
    installMetrics(img);
    w.invalidateLayout();
    const auto theme = ui::Theme::dark();
    paintInto(img, w, theme, {0.f, 0.f, size.w, size.h});
    const std::string path = (std::filesystem::path(g_captures) / (g_prefix + name)).string();
    check(img.savePng(path), "capture " + path);
    std::printf("capture %s\n", path.c_str());
    if (before) installMetrics(*before);
    w.invalidateLayout();
}

// ---------------------------------------------------------------- les regles du cadre ----
//  `frame` : le cadre (le panneau, le widget) ; chaque texte dessine y tient en largeur, et sa partie visible (la
//  decoupe : une liste qui defile) y tient en hauteur ; un texte n'est jamais coupe a droite par une decoupe.
int textsOutside(const SoftRenderer& r, const gfx::Rect& frame, const std::string& where, int maxReports = 4) {
    int bad = 0;
    for (const auto& t : r.texts) {
        const gfx::Rect seen = t.rect.intersect(t.clip);
        if (seen.empty()) continue;
        const bool xOk = t.rect.x >= frame.x - 0.5f && t.rect.right() <= frame.right() + 0.5f;
        const bool yOk = seen.y >= frame.y - 0.5f && seen.bottom() <= frame.bottom() + 0.5f;
        const bool clipOk = t.rect.x >= t.clip.x - 0.5f && t.rect.right() <= t.clip.right() + 0.5f;
        if (!xOk || !yOk || !clipOk) {
            if (bad < maxReports)
                std::printf("   %s : << %s >> %s hors de %s (decoupe %s)\n", where.c_str(), t.text.c_str(), rectText(t.rect).c_str(), rectText(frame).c_str(),
                            rectText(t.clip).c_str());
            ++bad;
        }
    }
    return bad;
}

void collectButtons(ui::Widget& w, std::vector<ui::Button*>& out) {
    for (const auto& c : w.children()) {
        if (!c->visible()) continue;
        if (auto* b = dynamic_cast<ui::Button*>(c.get())) out.push_back(b);
        collectButtons(*c, out);
    }
}

// Les boutons visibles : dans le cadre, sans se chevaucher, leur libelle tient (16 px de marge).
int buttonProblems(ui::Widget& root, const gfx::Rect& frame, const std::string& where) {
    std::vector<ui::Button*> bs;
    collectButtons(root, bs);
    int bad = 0;
    for (std::size_t i = 0; i < bs.size(); ++i) {
        const auto b = bs[i]->bounds();
        if (b.w <= 0.f || b.h <= 0.f) continue;
        if (b.x < frame.x - 0.5f || b.y < frame.y - 0.5f || b.right() > frame.right() + 0.5f || b.bottom() > frame.bottom() + 0.5f) {
            std::printf("   %s : le bouton << %s >> %s hors de %s\n", where.c_str(), bs[i]->text().c_str(), rectText(b).c_str(), rectText(frame).c_str());
            ++bad;
        }
        const float tw = ui::measureWidth(bs[i]->text(), gfx::FontId{16});
        if (tw + 12.f > b.w + 0.5f && !bs[i]->compact()) {
            std::printf("   %s : le libelle << %s >> (%s px) deborde du bouton (%s px)\n", where.c_str(), bs[i]->text().c_str(), num(tw).c_str(), num(b.w).c_str());
            ++bad;
        }
        for (std::size_t j = i + 1; j < bs.size(); ++j) {
            const auto o = bs[j]->bounds();
            if (o.w <= 0.f || o.h <= 0.f) continue;
            if (!b.intersect(o).inset(0.5f, 0.5f).empty()) {
                std::printf("   %s : << %s >> et << %s >> se chevauchent\n", where.c_str(), bs[i]->text().c_str(), bs[j]->text().c_str());
                ++bad;
            }
        }
    }
    return bad;
}

// ---------------------------------------------------------------- les donnees (fictives) ----
const std::string kCamille = "Camille Essai-Longuenom de la Soci\xC3\xA9t\xC3\xA9 d'essai";
const std::string kLea = "L\xC3\xA9" "a Essai-Longuenom de l'Atelier Exemple";
const std::string kHugo = "Hugo Essai";
const std::string kSessionName = "Mise en service de l'armoire gaz du b\xC3\xA2timent B, deuxi\xC3\xA8me \xC3\xA9tage (essai de longueur des titres)";
const std::string kLongMessage = "Bonjour L\xC3\xA9" "a ! Peux-tu d\xC3\xA9placer le texte d'\xC3\xA9tat de la vue d'accueil sous les compteurs, puis v\xC3\xA9rifier que "
                                 "rien ne d\xC3\xA9" "borde du cadre quand la fen\xC3\xAAtre est \xC3\xA9troite ?";
const std::string kLongWord = "Vue_Accueil_Texte_Etat_Principal_Avec_Un_Nom_Tres_Tres_Long_Pour_Voir_La_Coupure_Des_Mots_Trop_Longs";
// 1.50.0 : un message de huit lignes (reduit a toute largeur) et une reponse.
const std::string kReport = "Compte rendu de l'essai de 14 h :\n1. Vue_Accueil : le texte d'\xC3\xA9tat est sous les compteurs.\n2. Les alarmes de l'armoire "
                            "gaz s'affichent dans l'ordre.\n3. Le voyant de d\xC3\xA9" "faut clignote \xC3\xA0 1 Hz.\n4. La fen\xC3\xAAtre \xC3\xA9troite "
                            "garde tout dans son cadre.\n5. Les boutons ne se chevauchent plus.\n6. Le journal s'exporte en CSV.\n7. Reste : relire les "
                            "libell\xC3\xA9s avec l'atelier.";
const std::string kReplyText = "Merci ! Je relis les libell\xC3\xA9s avec l'atelier demain matin.";

app::sessionmarks::Board board() {
    namespace sm = app::sessionmarks;
    sm::Board b;
    b.sessionName = kSessionName;
    b.hostName = kCamille;
    b.host = true;
    b.people.push_back({0, kCamille, "CE", "PC-BUREAU-ETUDES-01", "M-1A2B", "Vue_Accueil", 1, 0xE67E22, true, true, false});
    b.people.push_back({1, kLea, "LE", "PC-ATELIER-02", "M-7F21", "Vue_Alarmes", 2, 0x2E86DE, false, false, false});
    b.chat.push_back({"14:30", "", kLea + " est entr\xC3\xA9" "e dans la session (PC-ATELIER-02 \xC2\xB7 M-7F21) ; elle peut modifier.", 0x2E86DE, true, false, 1});
    b.chat.push_back({"14:31", kCamille, kLongMessage, 0xE67E22, false, true, 1});
    b.chat.push_back({"14:31", kCamille, kLongWord, 0xE67E22, false, true, 1});
    b.chat.push_back({"14:33", kLea, "C'est fait : j'ai d\xC3\xA9plac\xC3\xA9 le texte d'\xC3\xA9tat. Dans la fen\xC3\xAAtre \xC3\xA9troite, la colonne de droite coupait "
                                     "les phrases au milieu des mots.", 0x2E86DE, false, false, 1});
    // 1.50.0 : les identifiants (donnes par l'hote) ; un compte rendu de huit lignes (reduit) ; une reponse a ce compte rendu.
    b.chat[1].id = "m1";
    b.chat[2].id = "m2";
    b.chat[3].id = "m3";
    b.chat.push_back({"14:36", kLea, kReport, 0x2E86DE, false, false, 1});
    b.chat.back().id = "m4";
    b.chat.push_back({"14:37", kCamille, kReplyText, 0xE67E22, false, true, 1});
    b.chat.back().id = "m5";
    b.chat.back().replyId = "m4";
    b.chat.back().replyFrom = kLea;
    b.chat.back().replyText = session::chatExcerpt(kReport);
    b.journal.push_back({"14:32", kLea, "Vue_Accueil \xE2\x80\xBA Texte_Etat d\xC3\xA9plac\xC3\xA9", 0x2E86DE, false, false, 12});
    b.journal.push_back({"14:34", kCamille, "ressource logo_de_la_societe_d_essai_en_haute_definition.png ajout\xC3\xA9" "e", 0xE67E22, false, true, 1});
    b.messages = 2;
    return b;
}

// ------------------------------------------------------------------ l'onglet Session ----
void ongletSession(SoftRenderer& r) {
    std::printf("-- l'onglet Session du panneau du bas, de 200 x 60 a 2400 x 600\n");
    namespace sm = app::sessionmarks;
    sm::setActive(true);
    sm::setBoard(board());
    const auto theme = ui::Theme::dark();
    app::HmiSessionPane pane("sortie.session");
    int bad = 0;
    for (const float w : {200.f, 240.f, 320.f, 400.f, 520.f, 700.f, 943.f, 1280.f, 1600.f, 2400.f})
        for (const float h : {60.f, 90.f, 140.f, 240.f, 600.f}) {
            const gfx::Rect frame{10.f, 20.f, w, h};
            pane.setBounds(frame);
            pane.invalidateLayout();
            paintInto(r, pane, theme, {0.f, 0.f, 4000.f, 4000.f});
            const std::string where = "onglet " + num(w) + "x" + num(h);
            bad += textsOutside(r, frame, where) + buttonProblems(pane, frame, where);
        }
    check(bad == 0, "l'onglet Session : tout tient dans son cadre, de 200 x 60 a 2400 x 600 (" + std::to_string(bad) + " ecart(s))");
    // 1.50.0 : une reponse en cours (<< En reponse a Lea Essai-Longuenom... : << Compte rendu... >> >> au-dessus du champ),
    // l'extrait cite, << ... voir tout >>, et les pictogrammes du survol de chaque ligne : dans le cadre a chaque taille.
    {
        app::sessionui::Row target{"14:36", kLea, kReport, 0x2E86DE, false, false, false, 1};
        target.id = "m4";
        pane.replyTo(target);
        int bad150 = 0, replySeen = 0, quoteSeen = 0, toggleSeen = 0;
        for (const float w : {200.f, 240.f, 320.f, 400.f, 520.f, 700.f, 943.f, 1280.f, 1600.f, 2400.f})
            for (const float h : {60.f, 90.f, 140.f, 240.f, 600.f}) {
                const gfx::Rect frame{10.f, 20.f, w, h};
                pane.setBounds(frame);
                pane.invalidateLayout();
                paintInto(r, pane, theme, {0.f, 0.f, 4000.f, 4000.f});
                const std::string where = "onglet (une reponse en cours) " + num(w) + "x" + num(h);
                bad150 += textsOutside(r, frame, where) + buttonProblems(pane, frame, where);
                auto* bar = pane.replyBar();
                if (bar->visible()) {
                    ++replySeen;
                    const auto rb = bar->bounds();
                    for (const auto& t : r.texts)
                        if (t.text == bar->shown() && (t.rect.x < rb.x - 0.5f || t.rect.right() > rb.right() + 0.5f)) {
                            std::printf("   %s : << %s >> hors de la barre %s\n", where.c_str(), t.text.c_str(), rectText(rb).c_str());
                            ++bad150;
                        }
                    if (bar->cancelButton()->visible() && !rb.contains({bar->cancelButton()->bounds().x + 1.f, bar->cancelButton()->bounds().y + 1.f})) ++bad150;
                }
                auto* list = pane.list();
                if (!list->visible()) continue;
                const auto lb = list->bounds();
                for (std::size_t i = 0; i < list->rows().size(); ++i) {
                    for (int k = 0; k < 2; ++k) {
                        const auto hb = list->hoverButton(i, k);
                        if (hb.w > 0.f && (hb.x < lb.x - 0.5f || hb.right() > lb.right() + 0.5f)) {
                            std::printf("   %s : le pictogramme %d de la ligne %zu %s hors de la liste %s\n", where.c_str(), k, i, rectText(hb).c_str(), rectText(lb).c_str());
                            ++bad150;
                        }
                    }
                    const auto q = list->quoteRect(i), t = list->toggleRect(i);
                    if (q.w > 0.f) ++quoteSeen;
                    if (t.w > 0.f) ++toggleSeen;
                    for (const auto& x : {q, t})
                        if (x.w > 0.f && (x.x < lb.x - 0.5f || x.right() > lb.right() + 0.5f)) {
                            std::printf("   %s : l'extrait ou << voir tout >> de la ligne %zu %s hors de la liste %s\n", where.c_str(), i, rectText(x).c_str(), rectText(lb).c_str());
                            ++bad150;
                        }
                }
            }
        check(bad150 == 0, "l'onglet Session (1.50) : la reponse en cours, l'extrait cite, << voir tout >>, le survol - dans le cadre a chaque taille ("
                               + std::to_string(bad150) + " ecart(s))");
        check(replySeen > 0 && quoteSeen > 0 && toggleSeen > 0, "l'onglet Session (1.50) : la barre de reponse, l'extrait et << voir tout >> bien montres");
        pane.setBounds({0.f, 0.f, 640.f, 300.f});
        capture(pane, {640.f, 300.f}, "onglet_session_640x300_reponse.png");
        pane.cancelReply();
    }
    pane.setBounds({0.f, 0.f, 320.f, 96.f});
    capture(pane, {320.f, 96.f}, "onglet_session_320x96.png");
    pane.setBounds({0.f, 0.f, 640.f, 220.f});
    capture(pane, {640.f, 220.f}, "onglet_session_640x220.png");
    pane.setBounds({0.f, 0.f, 1400.f, 300.f});
    capture(pane, {1400.f, 300.f}, "onglet_session_1400x300.png");
    sm::setActive(false);
    sm::setBoard({});
}

// ---------------------------------------------------------- l'encart de la licence de l'accueil ----
void encartLicence(SoftRenderer& r) {
    std::printf("-- l'accueil : l'encart de la licence (Mon profil..., Demander ma licence...)\n");
    const auto theme = ui::Theme::dark();
    app::start::StartRail rail("startup.rail");
    rail.setLicenceCard("D\xC3\xA9" "couverte : il te reste 23 jours (jusqu'au 03/12/2026)", "Ensuite : les fonctions de l'\xC3\xA9" "dition sans licence.", 4,
                        "Demander ma licence\xE2\x80\xA6");
    int bad = 0, hidden = 0, cut = 0;
    for (float w = 240.f; w <= 640.f; w += 20.f) {
        const gfx::Rect frame{0.f, 0.f, w, 980.f};
        rail.setBounds(frame);
        rail.invalidateLayout();
        paintInto(r, rail, theme, frame);
        auto& prof = rail.profileButton();
        auto& lic = rail.licenceButton();
        if (!prof.visible() || !lic.visible()) ++hidden;
        // Le libelle entier : le bouton a sa largeur naturelle, et son texte est dessine tel quel.
        for (auto* b : {&prof, &lic}) {
            if (!b->visible()) continue;
            bool whole = false;
            for (const auto& t : r.texts)
                if (t.text == b->text() && t.rect.x >= b->bounds().x - 0.5f && t.rect.right() <= b->bounds().right() + 0.5f) whole = true;
            if (!whole || b->bounds().w + 0.5f < b->naturalWidth()) {
                if (cut < 4)
                    std::printf("   largeur %s : << %s >> coupe (bouton %s px, naturel %s px)\n", num(w).c_str(), b->text().c_str(), num(b->bounds().w).c_str(),
                                num(b->naturalWidth()).c_str());
                ++cut;
            }
        }
        if (prof.visible() && lic.visible() && !prof.bounds().intersect(lic.bounds()).inset(0.5f, 0.5f).empty()) ++bad;
    }
    check(hidden == 0, "l'encart : << Mon profil... >> et le bouton de la licence toujours montres, de 240 a 640 px (" + std::to_string(hidden) + " absent(s))");
    check(cut == 0, "l'encart : leurs libelles entiers, jamais << Mon pr... >> (" + std::to_string(cut) + " coupe(s))");
    check(bad == 0, "l'encart : les deux boutons ne se chevauchent pas");
    // Un lien sans pictogramme : sa largeur naturelle montre tout le libelle (le dessin ne garde plus la place du pictogramme).
    app::start::StartButton link("Mon profil\xE2\x80\xA6", "essai.lien", app::start::StartButton::Look::Link);
    link.setBounds({0.f, 0.f, link.naturalWidth(), 30.f});
    paintInto(r, link, theme, {0.f, 0.f, 400.f, 40.f});
    bool whole = false;
    for (const auto& t : r.texts) whole = whole || (t.text == link.text() && t.rect.right() <= link.bounds().right() + 0.5f);
    check(whole, "un lien a sa largeur naturelle : << Mon profil... >> entier");
    app::start::StartButton glyphLink("Rejoindre une session\xE2\x80\xA6", "essai.lien2", app::start::StartButton::Look::Link, app::start::StartButton::Glyph::Team);
    glyphLink.setHint(">");
    glyphLink.setBounds({0.f, 0.f, glyphLink.naturalWidth(), 30.f});
    paintInto(r, glyphLink, theme, {0.f, 0.f, 400.f, 40.f});
    whole = false;
    for (const auto& t : r.texts) whole = whole || t.text == glyphLink.text();
    check(whole, "un lien avec pictogramme et chevron, a sa largeur naturelle : son libelle entier");
    for (const float w : {280.f, 340.f, 420.f}) {
        rail.setBounds({0.f, 0.f, w, 560.f});
        capture(rail, {w, 560.f}, "accueil_encart_licence_" + std::to_string(static_cast<int>(w)) + ".png");
    }
}

#ifdef XPG_SESSION_WINDOW
#include "session_ui_window.inl"
#include "session_ui_ancien.inl"   // la fenetre telle qu'en 1.46 : l'image << avant >> seulement
#endif
#include "session_ui_discussion.inl"   // 1.50.0 : repondre, reduire, copier (deux membres)

} // namespace

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : ".";
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--captures" && i + 1 < argc) g_captures = argv[++i];
        else if (a == "--prefixe" && i + 1 < argc) g_prefix = argv[++i];
        else if (a == "--ancien") g_ancien = true;
    }
    gfx::setInterfaceFontFolder(root + "/resources/fonts");
    SoftRenderer r(4000, 4000, false);
    if (!r.fontsReady()) {
        std::printf("SAUTE : pas de police (%s/resources/fonts)\n", root.c_str());
        return 0;
    }
    installMetrics(r);
    if (!g_captures.empty()) std::filesystem::create_directories(g_captures);
#ifdef XPG_SESSION_WINDOW
    if (g_ancien) {
        fenetreSessionAvant();
        return g_failures == 0 ? 0 : 1;
    }
#endif
    ongletSession(r);
    encartLicence(r);
    discussion150(r);   // 1.50.0
#ifdef XPG_SESSION_WINDOW
    fenetreSession(r);
    fenetresDemarrerRejoindre(r);
#endif
    std::printf("%d verification(s), %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
