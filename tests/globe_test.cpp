// 1.47.0 : LE GLOBE (ui/widgets/Globe) - sans ecran. Le masque des terres (Lyon est une terre, le milieu de
// l'Atlantique une mer, la mer Noire et la Caspienne des trous), la projection (le centre, la face visible et la face
// cachee, la rotation, l'aller-retour), le plus court chemin, turnTo (anime, fige, la latitude gardee), la rotation
// seule (et ce qui l'arrete : le survol, une ville choisie, la pause, fige), l'elan, la molette, la taille des marques
// (la racine du nombre), les groupes de villes, le survol et le clic (les signaux), les calques (ce qui se voit, ce que
// dit l'infobulle), et ce que coute le dessin (un faux renderer compte les images : rien quand il est arrete, 30 par
// seconde au plus quand il tourne).
#include "../src/ui/widgets/Globe.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using ui::GlobeView;
using namespace ui::globe;

static int fails = 0;
static int checks = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        ++checks;                                                         \
        if (!(c)) {                                                       \
            std::printf("ECHEC ligne %d : %s\n", __LINE__, #c);           \
            ++fails;                                                      \
        }                                                                 \
    } while (0)

static bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// Un faux renderer : il compte les images creees, rendues, dessinees, et garde le texte.
class CountingRenderer final : public gfx::IRenderer {
public:
    int created{0}, released{0}, images{0};
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override { texts.clear(); }
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view s, gfx::FontId, gfx::Color) override { texts.emplace_back(s); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return {static_cast<float>(s.size()) * h * 0.5f, h, h * 0.8f, h * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return f.v ? static_cast<float>(f.v) : 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1373.f, 917.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId, float) const override { return s.size(); }
    [[nodiscard]] gfx::TextureId createImage(const std::uint8_t*, int w, int h) override {
        if (w <= 0 || h <= 0) return {};
        ++created;
        return gfx::TextureId{static_cast<std::uint32_t>(created)};
    }
    void releaseImage(gfx::TextureId) override { ++released; }
    void drawImage(gfx::TextureId, const gfx::Rect&, float, bool, bool, gfx::Color) override { ++images; }
};

static int findCity(const std::vector<Place>& v, const std::string& city) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].city == city) return static_cast<int>(i);
    return -1;
}

static void paintAt(GlobeView& g, CountingRenderer& r, const ui::Theme& theme, double t) {
    r.beginFrame({});
    g.render(ui::PaintContext{r, theme, {0.f, 0.f, 1373.f, 917.f}, t, nullptr});
}

int main() {
    // ------------------------------------------------------------ 1. le masque des terres
    CHECK(landCellCount() > 0);
    CHECK(landCellCount() == landCellCountExpected());
    CHECK(isLand(45.76, 4.85));        // Lyon
    CHECK(isLand(48.86, 2.35));        // Paris
    CHECK(isLand(40.42, -3.70));       // Madrid
    CHECK(isLand(55.75, 37.62));       // Moscou
    CHECK(isLand(-15.8, -47.9));       // Brasilia
    CHECK(isLand(45.5, -73.57));       // Montreal
    CHECK(isLand(-25.0, 134.0));       // le centre de l'Australie
    CHECK(isLand(-80.0, 0.0));         // l'Antarctique
    CHECK(!isLand(30.0, -40.0));       // le milieu de l'Atlantique
    CHECK(!isLand(0.0, -150.0));       // le Pacifique
    CHECK(!isLand(-30.0, 80.0));       // l'ocean Indien
    CHECK(!isLand(43.5, 34.0));        // la mer Noire (un trou dans l'Eurasie)
    CHECK(!isLand(42.0, 51.0));        // la Caspienne
    CHECK(!isLand(35.0, 18.0));        // la Mediterranee
    CHECK(isLand(45.76, 4.85 + 360.0) && isLand(45.76, 4.85 - 360.0));   // la longitude ramenee
    {
        const auto& dots = landDots(1.1f);
        const std::size_t n = dots.size() / 3;
        std::printf("points des terres (pas 1,1) : %zu ; (pas 4) : %zu ; cases de terre : %d\n", n, landDots(4.f).size() / 3, landCellCount());
        CHECK(n > 6000 && n < 16000);
        CHECK(landDots(4.f).size() / 3 < n / 8);
        bool unit = true, land = true;
        for (std::size_t i = 0; i + 2 < dots.size(); i += 3) {
            const double x = dots[i], y = dots[i + 1], z = dots[i + 2];
            if (!near(x * x + y * y + z * z, 1.0, 1e-4)) unit = false;
            const double lat = std::asin(y) * 180.0 / 3.14159265358979, lon = std::atan2(x, z) * 180.0 / 3.14159265358979;
            // (un point tombe parfois sur le bord de deux cases : le float le fait passer de l'une a l'autre)
            bool any = false;
            for (double e : {0.0, -0.002, 0.002})
                for (double f : {0.0, -0.002, 0.002}) any = any || isLand(lat + e, lon + f);
            if (!any) land = false;
        }
        CHECK(unit);
        CHECK(land);
        CHECK(&landDots(1.1f) == &dots);   // en cache
    }

    // ------------------------------------------------------------ 2. la projection
    {
        const View lyon{45.76, 4.85, 1.f};
        const auto c = project(lyon, 45.76, 4.85);
        CHECK(near(c.x, 0.0, 1e-9) && near(c.y, 0.0, 1e-9) && near(c.z, 1.0, 1e-9));   // le centre
        const auto paris = project(lyon, 48.86, 2.35);
        CHECK(paris.z > 0.99 && paris.y > 0.0 && paris.x < 0.0);   // devant, au nord, a l'ouest
        const auto turin = project(lyon, 45.07, 7.69);
        CHECK(turin.x > 0.0);                                       // a l'est : a droite
        const auto anti = project(lyon, -45.76, 4.85 - 180.0);
        CHECK(near(anti.z, -1.0, 1e-9));                            // l'antipode : derriere
        const auto sydney = project(lyon, -33.87, 151.21);
        CHECK(sydney.z < 0.0);                                      // la face cachee
        const auto montreal = project(lyon, 45.5, -73.57);
        CHECK(montreal.z > 0.5 && montreal.z < 0.7 && montreal.x < -0.6);   // a gauche, encore devant (78 degres a l'ouest)
        // 90 degres plus loin : sur le bord
        const auto edge = project(View{0.0, 0.0, 1.f}, 0.0, 90.0);
        CHECK(near(edge.z, 0.0, 1e-9) && near(edge.x, 1.0, 1e-9));
        const auto pole = project(View{0.0, 0.0, 1.f}, 90.0, 0.0);
        CHECK(near(pole.y, 1.0, 1e-9) && near(pole.z, 0.0, 1e-9));   // le nord en haut
        // La rotation : 10 degres de plus a la vue, la ville a 10 degres vient au centre.
        CHECK(near(project(View{0.0, 10.0, 1.f}, 0.0, 10.0).x, 0.0, 1e-9));
        CHECK(project(View{0.0, 10.0, 1.f}, 0.0, 0.0).x < 0.0);
        // L'aller-retour
        double la = 0, lo = 0;
        CHECK(unproject(lyon, paris.x, paris.y, la, lo) && near(la, 48.86, 1e-6) && near(lo, 2.35, 1e-6));
        CHECK(unproject(lyon, 0.0, 0.0, la, lo) && near(la, 45.76, 1e-6) && near(lo, 4.85, 1e-6));
        CHECK(!unproject(lyon, 0.9, 0.9, la, lo));   // hors du disque
        // Le plus court chemin
        CHECK(near(shortestTurn(170.0, -170.0), 20.0, 1e-9));
        CHECK(near(shortestTurn(-170.0, 170.0), -20.0, 1e-9));
        CHECK(near(shortestTurn(10.0, 30.0), 20.0, 1e-9));
        CHECK(near(normalizeLon(190.0), -170.0, 1e-9) && near(normalizeLon(-190.0), 170.0, 1e-9) && near(normalizeLon(180.0), -180.0, 1e-9));
    }

    // ------------------------------------------------------------ 3. la taille des marques, les nombres, les calques
    CHECK(markerRadius(0) == 0.f && markerRadius(-3) == 0.f);
    CHECK(near(markerRadius(1), 3.95, 0.01));     // la legende : 1 -> 8 px de large
    CHECK(near(markerRadius(4), 5.5, 0.01));      // 4 -> 11
    CHECK(near(markerRadius(9), 7.05, 0.01));     // 9 -> 14
    CHECK(near(markerRadius(18), 8.98, 0.02));    // 18 et plus -> 18
    CHECK(markerRadius(100) == 9.f && markerRadius(100000) == 9.f);
    {
        bool grows = true;
        for (int n = 1; n < 18; ++n)
            if (!(markerRadius(n + 1) > markerRadius(n))) grows = false;
        CHECK(grows);
        // La racine, pas le nombre : 4 fois plus de PC, la partie qui varie du rayon double (l'aire suit le nombre).
        CHECK(near(markerRadius(8) - 2.4f, 2.f * (markerRadius(2) - 2.4f), 1e-4));
        CHECK(markerRadius(16) < 2.f * markerRadius(4));
    }
    CHECK(formatCount(999) == "999" && formatCount(1000) == "1 k" && formatCount(1234) == "1,2 k");
    {
        Layer l{};
        CHECK(layerFromKey("pas_a_jour", l) && l == Layer::Outdated);
        CHECK(layerFromKey("decouvertes", l) && l == Layer::Discoveries);
        CHECK(!layerFromKey("inconnu", l));
        CHECK(styles().size() == kLayerCount && style(Layer::Admins).shape == Shape::Star);
        LayerSet s = LayerSet::of({Layer::Clients, Layer::Admins});
        CHECK(s.has(Layer::Clients) && s.has(Layer::Admins) && !s.has(Layer::Orders) && s.count() == 2);
        s.set(Layer::Clients, false);
        CHECK(!s.has(Layer::Clients) && s.count() == 1);
        CHECK(LayerSet::all().count() == 7);
    }

    // ------------------------------------------------------------ 4. le widget : turnTo, la rotation seule, l'elan
    const auto sample = samplePlaces();
    CHECK(sample.size() == 21);
    const int iLyon = findCity(sample, "Lyon"), iParis = findCity(sample, "Paris"), iGrenoble = findCity(sample, "Grenoble");
    const int iToulouse = findCity(sample, "Toulouse");
    CHECK(iLyon >= 0 && iParis >= 0 && iGrenoble >= 0 && iToulouse >= 0);
    CHECK(sample[static_cast<std::size_t>(iLyon)].id == "FR|Lyon" && sample[static_cast<std::size_t>(iLyon)].label() == "Lyon, FR");
    {
        GlobeView g("globe");
        g.setBounds({0.f, 0.f, 600.f, 600.f});
        g.setPlaces(sample);
        CHECK(g.findPlace("FR|Paris") == iParis && g.findPlace("XX|Nulle") == -1);
        g.setView(0.0, 0.0, 1.f);
        g.setAutoRotate(false);
        g.turnTo(45.76, 4.85, 1.45f, 1.0);
        CHECK(g.moving());
        for (int i = 0; i < 5; ++i) g.advance(0.1);   // a mi-chemin
        CHECK(g.view().lat > 1.0 && g.view().lat < 45.0 && g.view().zoom > 1.f && g.view().zoom < 1.45f);
        for (int i = 0; i < 7; ++i) g.advance(0.1);
        CHECK(near(g.view().lat, 45.76, 1e-6) && near(g.view().lon, 4.85, 1e-6) && near(g.view().zoom, 1.45, 1e-5));
        CHECK(!g.moving());
        // Le plus court chemin : de 170 E a 170 O par l'antimeridien.
        g.setView(0.0, 170.0, 1.f);
        g.turnTo(0.0, -170.0, 0.f, 1.0);
        bool viaDateLine = true;
        for (int i = 0; i < 12; ++i) {
            g.advance(0.1);
            if (std::fabs(g.view().lon) < 169.0) viaDateLine = false;
        }
        CHECK(viaDateLine && near(g.view().lon, -170.0, 1e-6));
        // La latitude visee est gardee (-60 a 70)
        g.turnTo(85.0, 0.0, 0.f, 0.5);
        for (int i = 0; i < 8; ++i) g.advance(0.1);
        CHECK(near(g.view().lat, 70.0, 1e-6));
        // Fige : tout de suite ; rien ne bouge avec le temps.
        g.setFrozen(true);
        g.setAutoRotate(true);
        g.turnTo(45.76, 4.85, 1.45f, 1.1);
        CHECK(near(g.view().lat, 45.76, 1e-9) && near(g.view().lon, 4.85, 1e-9) && !g.moving());
        for (int i = 0; i < 10; ++i) g.advance(0.1);
        CHECK(near(g.view().lon, 4.85, 1e-9));
        g.setFrozen(false);
        // La rotation seule : 4 degres par seconde vers l'ouest (la vue), un pas de temps vaut 0,1 s au plus.
        g.setView(0.0, 10.0, 1.f);
        for (int i = 0; i < 10; ++i) g.advance(0.1);
        CHECK(near(g.view().lon, 6.0, 1e-6));
        g.advance(5.0);   // une page revenue : 0,1 s, pas 5
        CHECK(near(g.view().lon, 5.6, 1e-6));
        // ... arretee : en pause, sous la souris, une ville choisie.
        g.setAutoRotate(false);
        g.advance(0.1);
        CHECK(near(g.view().lon, 5.6, 1e-6) && !g.moving());
        g.setAutoRotate(true);
        g.hoverAt({5.f, 5.f});   // un coin, hors du globe : la souris est la quand meme (une capture)
        g.advance(0.1);
        CHECK(near(g.view().lon, 5.6, 1e-6) && !g.moving());
        g.dispatch(ui::MouseMove{{20.f, 20.f}, {}, {}});     // une vraie souris sur le widget
        g.advance(0.1);
        CHECK(near(g.view().lon, 5.6, 1e-6) && g.hovered());
        g.dispatch(ui::MouseMove{{900.f, 900.f}, {}, {}});   // partie : le survol s'oublie
        CHECK(!g.hovered() && g.hoveredPlace() == -1);
        g.setSelected(iLyon);
        g.advance(0.1);
        CHECK(near(g.view().lon, 5.6, 1e-6));
        g.setSelected(-1);
        g.advance(0.1);
        CHECK(near(g.view().lon, 5.2, 1e-6));
        // La molette : un cran, 1,15 ; de 1 a 3,2.
        g.setView(45.76, 4.85, 1.f);
        g.dispatch(ui::MouseWheel{{300.f, 300.f}, 0.f, 1.f, {}});
        CHECK(near(g.view().zoom, 1.15, 1e-5));
        for (int i = 0; i < 20; ++i) g.dispatch(ui::MouseWheel{{300.f, 300.f}, 0.f, 1.f, {}});
        CHECK(near(g.view().zoom, kMaxZoom, 1e-6));
        for (int i = 0; i < 30; ++i) g.dispatch(ui::MouseWheel{{300.f, 300.f}, 0.f, -1.f, {}});
        CHECK(near(g.view().zoom, kMinZoom, 1e-6));
        // Glisser : le globe suit la souris (vers la droite : la vue part a l'ouest), puis garde un peu d'elan.
        g.setAutoRotate(false);
        g.setView(20.0, 0.0, 1.f);
        g.dispatch(ui::MouseDown{{300.f, 300.f}, ui::MouseButton::Left, 1, {}});
        g.dispatch(ui::MouseMove{{320.f, 300.f}, {}, {}});
        g.dispatch(ui::MouseMove{{340.f, 310.f}, {}, {}});
        const double afterDrag = g.view().lon;
        CHECK(afterDrag < -5.0 && g.view().lat > 20.0);   // 40 px a droite, 10 vers le bas
        g.dispatch(ui::MouseUp{{340.f, 310.f}, ui::MouseButton::Left, {}});
        CHECK(g.moving());
        g.advance(0.1);
        CHECK(g.view().lon < afterDrag);   // l'elan
        for (int i = 0; i < 80; ++i) g.advance(0.1);
        const double settled = g.view().lon;
        g.advance(0.1);
        CHECK(near(g.view().lon, settled, 1e-9) && !g.moving());   // il s'arrete
    }

    // ------------------------------------------------------------ 5. les groupes, le survol, le clic, les calques
    {
        GlobeView g("globe");
        g.setBounds({0.f, 0.f, 600.f, 600.f});
        g.setPlaces(sample);
        g.setFrozen(true);
        g.setView(45.76, 4.85, 1.f);
        CHECK(near(g.radius(), 274.f, 1e-3) && near(g.centre().x, 300.f, 1e-3));
        // Lyon au centre ; Paris (a 17 px a ce zoom) et Grenoble se groupent avec elle (la plus grande porte le groupe).
        const auto lyonAt = g.screenOf(iLyon);
        CHECK(lyonAt && near(lyonAt->x, 300.f, 1e-3) && near(lyonAt->y, 300.f, 1e-3));
        CHECK(g.placeAt({300.f, 300.f}) == iLyon && g.placeAt({303.f, 302.f}) == iLyon);
        CHECK(g.placeAt({20.f, 20.f}) == -1);
        const auto grp = g.groupOf(iLyon);
        bool hasParis = false, hasGrenoble = false;
        for (int m : grp) {
            hasParis = hasParis || m == iParis;
            hasGrenoble = hasGrenoble || m == iGrenoble;
        }
        CHECK(hasParis && hasGrenoble);
        // Plus pres (zoom 3,2), Paris se separe ; Grenoble (13 px) reste avec Lyon.
        g.setView(45.76, 4.85, 3.2f);
        const auto parisAt = g.screenOf(iParis);
        CHECK(parisAt && parisAt->y < 300.f - 40.f);   // au nord : plus haut
        CHECK(g.placeAt(*parisAt) == iParis);
        const auto close = g.groupOf(iLyon);
        bool withGrenoble = false, withParis = false;
        for (int m : close) {
            withGrenoble = withGrenoble || m == iGrenoble;
            withParis = withParis || m == iParis;
        }
        CHECK(withGrenoble && !withParis);
        // La ville choisie reste seule.
        g.setSelected(iGrenoble);
        CHECK(g.groupOf(iGrenoble).empty());
        for (int m : g.groupOf(iLyon)) CHECK(m != iGrenoble);
        CHECK(g.screenOf(iGrenoble).has_value());
        g.setSelected(-1);
        // La face cachee : Montreal derriere quand on regarde l'Asie.
        g.setView(30.0, 100.0, 1.f);
        const int iMontreal = findCity(sample, "Montr\xC3\xA9" "al");
        CHECK(!g.screenOf(iMontreal).has_value() && g.placeAt({300.f, 300.f}) != iMontreal);
        CHECK(project(g.view(), 45.76, 4.85).z > 0.0 && g.screenOf(iLyon).has_value());
        // Le survol : << Lyon, FR >> et les nombres des calques montres (Lyon + Grenoble, groupees a ce zoom).
        g.setView(45.76, 4.85, 3.2f);
        int hovered = -2;
        auto c1 = g.hoverPlaceChanged->connect([&](int i) { hovered = i; });
        g.hoverAt({300.f, 300.f});
        CHECK(hovered == iLyon && g.hoveredPlace() == iLyon);
        const std::string tip = g.hoverText();
        std::printf("infobulle :\n%s\n", tip.c_str());
        // Les nombres du groupe : ceux de Lyon et de ses voisines groupees a ce zoom.
        int sumClients = sample[static_cast<std::size_t>(iLyon)].count(Layer::Clients);
        int sumActive = sample[static_cast<std::size_t>(iLyon)].count(Layer::ActivePcs);
        int sumOutdated = sample[static_cast<std::size_t>(iLyon)].count(Layer::Outdated);
        for (int m : close) {
            sumClients += sample[static_cast<std::size_t>(m)].count(Layer::Clients);
            sumActive += sample[static_cast<std::size_t>(m)].count(Layer::ActivePcs);
            sumOutdated += sample[static_cast<std::size_t>(m)].count(Layer::Outdated);
        }
        const std::string nearby = close.size() == 1 ? "+ 1 ville proche" : "+ " + std::to_string(close.size()) + " villes proches";
        CHECK(tip.rfind("Lyon, FR " + nearby, 0) == 0);
        CHECK(tip.find("\nClients " + std::to_string(sumClients) + "\n") != std::string::npos);
        CHECK(tip.find("\nPC actifs maintenant " + std::to_string(sumActive) + "\n") != std::string::npos);
        CHECK(tip.find("\nClients pas \xC3\xA0 jour " + std::to_string(sumOutdated)) != std::string::npos);
        CHECK(tip.find("Admins") == std::string::npos);                       // calque decoche
        CHECK(tip.find("Grenoble") != std::string::npos && tip.find(" \xC2\xB7 un clic pour zoomer") != std::string::npos);
        // Les calques : seulement les admins -> Toulouse (0 admin) disparait, Lyon dit << Admins 2 >>.
        g.setLayers(LayerSet::of({Layer::Admins}));
        CHECK(!g.screenOf(iToulouse).has_value());
        CHECK(g.screenOf(iLyon).has_value());
        g.hoverAt({300.f, 300.f});
        const std::string tip2 = g.hoverText();
        CHECK(tip2.find("\nAdmins 2") != std::string::npos && tip2.find("Clients") == std::string::npos);
        g.setLayerVisible(Layer::Admins, false);
        CHECK(!g.screenOf(iLyon).has_value() && g.placeAt({300.f, 300.f}) == -1);   // aucun calque : rien
        g.setLayers(LayerSet::of({Layer::Clients, Layer::ActivePcs, Layer::Outdated}));
        // Le clic : une ville seule -> placeClicked(i) ; a cote -> -1 ; un groupe -> il zoome dessus.
        int clicked = -2;
        auto c2 = g.placeClicked->connect([&](int i) { clicked = i; });
        g.setFrozen(false);
        g.setAutoRotate(false);
        g.setView(45.76, 4.85, 3.2f);
        const auto p = *g.screenOf(iParis);
        g.dispatch(ui::MouseDown{p, ui::MouseButton::Left, 1, {}});
        g.dispatch(ui::MouseUp{p, ui::MouseButton::Left, {}});
        CHECK(clicked == iParis);
        g.dispatch(ui::MouseDown{{40.f, 300.f}, ui::MouseButton::Left, 1, {}});
        g.dispatch(ui::MouseUp{{40.f, 300.f}, ui::MouseButton::Left, {}});
        CHECK(clicked == -1);
        clicked = -2;
        g.setView(45.76, 4.85, 1.f);
        g.dispatch(ui::MouseDown{{300.f, 300.f}, ui::MouseButton::Left, 1, {}});
        g.dispatch(ui::MouseUp{{300.f, 300.f}, ui::MouseButton::Left, {}});
        CHECK(clicked == -2 && g.moving());   // un groupe : pas de signal, il zoome
        for (int i = 0; i < 12; ++i) g.advance(0.1);
        CHECK(near(g.view().zoom, 1.9, 1e-4));
        // En direct : une bouffee (rien ne casse, la ville inconnue est ignoree).
        g.pulse(g.findPlace("FR|Lyon"));
        g.pulse(-1);
        g.pulse(999);
    }

    // ------------------------------------------------------------ 6. ce que coute le dessin
    {
        const ui::Theme theme = ui::Theme::byName("XPG");
        CountingRenderer r;
        GlobeView g("globe");
        g.setBounds({200.f, 100.f, 700.f, 640.f});
        g.setPlaces(sample);
        g.setView(38.0, 4.85, 1.f);
        paintAt(g, r, theme, 0.0);
        const int first = r.created;
        CHECK(first >= 2);           // le disque, les points (et les marques)
        CHECK(r.images > 0);
        // Il tourne : 1 seconde peinte a 100 images par seconde -> 30 vues au plus (et les points refaits autant).
        const int before = r.created;
        double t = 0.0;
        for (int i = 1; i <= 100; ++i) {
            t = i * 0.01;
            paintAt(g, r, theme, t);
        }
        const int rebuilt = r.created - before;
        std::printf("en tournant : %d images refaites pour 100 dessins en 1 s\n", rebuilt);
        CHECK(rebuilt >= 20 && rebuilt <= 40);
        CHECK(near(g.view().lon, 4.85 - 4.0, 0.15));
        // Arrete (sous la souris) : plus rien ne se refait, meme en 2 s de dessins.
        g.hoverAt({1300.f, 900.f});
        paintAt(g, r, theme, t + 0.05);
        const int idle = r.created;
        for (int i = 1; i <= 200; ++i) paintAt(g, r, theme, t + 0.05 + i * 0.01);
        CHECK(r.created == idle);
        // Une ville survolee : l'infobulle est dessinee.
        const auto at = g.screenOf(iLyon);
        CHECK(at.has_value());
        if (at) {
            g.hoverAt(*at);
            paintAt(g, r, theme, t + 3.0);
            bool title = false;
            for (const auto& s : r.texts) title = title || s == "Lyon, FR";
            CHECK(title);
        }
        // Les noms des villes : << Lyon +N >> ou << Paris >>, jamais la ville choisie.
        bool label = false;
        for (const auto& s : r.texts) label = label || s.rfind("Lyon", 0) == 0;
        CHECK(label);
        // La petite carte (compacte) : ni noms, ni infobulle ; elle tourne plus vite.
        GlobeView m("mini");
        m.setBounds({0.f, 0.f, 116.f, 116.f});
        m.setCompact(true);
        m.setPlaces(sample);
        m.setLayers(LayerSet::of({Layer::ActivePcs}));
        m.setView(36.0, 0.0, 1.f);
        CountingRenderer rm;
        for (int i = 0; i <= 50; ++i) paintAt(m, rm, theme, i * 0.02);
        CHECK(rm.texts.empty());
        CHECK(near(m.view().lon, -9.0, 0.4));   // 9 degres par seconde
        // Un widget detruit : ses textures sont rendues au prochain dessin d'un globe sur le meme renderer.
        const int releasedBefore = r.released;
        {
            GlobeView tmp("tmp");
            tmp.setBounds({0.f, 0.f, 300.f, 300.f});
            paintAt(tmp, r, theme, 10.0);
        }
        paintAt(g, r, theme, 10.1);
        CHECK(r.released >= releasedBefore + 2);
    }

    std::printf("%d verifications, %d echec(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
