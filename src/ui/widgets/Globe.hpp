// =============================================================================
//  ui/widgets/Globe.hpp - 1.47.0 : LE GLOBE POINTILLE QUI TOURNE (planche 11)
// -----------------------------------------------------------------------------
//  La maquette : docs/maquettes/1.47.0/src-11-admin-globe.html, globe.js, globe.css
//  (le PNG : hd/11-admin-globe.png). Un globe en projection orthographique ; les
//  terres en points (le masque de globe.js, porte en table : GlobeLand.inc) ; les
//  villes en marques, une couleur ET une forme par calque, la taille selon le
//  nombre. Il tourne seul, doucement ; on le tourne a la souris (et on le lance :
//  il garde un peu d'elan), la molette zoome (de 1 a 3,2) ; il s'arrete sous la
//  souris et quand une ville est choisie.
//
//  LES DONNEES (independantes de leur source : GET /admin/globe, ou des villes
//  d'essai) : une liste de globe::Place - la ville, le pays (code a deux lettres),
//  la latitude et la longitude, un nombre par calque - et l'ensemble des calques
//  montres (globe::LayerSet). Elles suivent docs/STATISTIQUES.md, par. 8 : une
//  ville de la reponse (stats::GlobeCity) donne une Place champ pour champ (id
//  << FR|Lyon >>, ville, region, pays, lat, lon ; clients, actifs, pas_a_jour,
//  admins, decouvertes, commandes, demandes ; pc) ; les cles des calques sont
//  celles du JSON (LayerStyle::key). En direct, une connexion (GET
//  /admin/globe/direct : << ville >> : l'id) fait pulser sa ville : pulse(findPlace(id)).
//  Les calques, dans l'ordre de la legende :
//    Clients            bleu    rond          la ville de leurs PC
//    PC actifs          vert    rond + point  vus il y a moins de 15 min (un halo)
//    Clients pas a jour orange  triangle
//    Admins             violet  etoile
//    Decouvertes        vert    anneau        l'essai, pas encore achete
//    Commandes          jaune   carre         payees dans la periode
//    Demandes           rose    losange       recues, pas encore traitees
//  Le rayon d'une marque suit la racine du nombre (l'aire suit le nombre) : 4 px
//  pour 1, 5,5 pour 4, 7 pour 9, 9 a partir de 18 (markerRadius). Le premier calque
//  coche d'une ville est au centre, les autres a une place fixe autour.
//  Les villes trop proches a ce zoom se groupent (<< Paris +6 >>) : la plus
//  grande porte le groupe, les nombres s'additionnent ; un clic sur un groupe
//  zoome dessus ; en zoomant, elles se separent. La ville choisie reste seule.
//
//  L'API (GlobeView) :
//    setPlaces, setLayers / setLayerVisible        les donnees, les calques
//    setView(lat, lon, zoom)                       la vue, tout de suite
//    turnTo(lat, lon, zoom, secondes)              tourner vers une ville (le plus
//                                                  court chemin, 1,1 s par defaut)
//    zoomBy, setZoom, recenter (setHome)           les boutons + / - / Recentrer
//    setAutoRotate                                 Pause / reprise
//    setSelected(i)                                la ville choisie : un anneau ;
//                                                  setLeaderTarget : le trait vers
//                                                  sa carte (dessinee par la page)
//    pulse(i)                                      En direct : un PC de la ville i
//                                                  vient de se connecter (une
//                                                  bouffee de 1,6 s)
//    setReservedAreas                              ou les noms ne vont pas (les
//                                                  boutons, la carte de la page)
//    setCentreShift                                le disque decale (une page
//                                                  etroite : la ville choisie a
//                                                  gauche de sa carte)
//    setCompact                                    la petite carte du tableau de
//                                                  bord (points espaces, marques
//                                                  0,55, sans noms ni souris)
//    setFrozen                                     LES CAPTURES : rien ne bouge
//                                                  avec le temps (#fige de la
//                                                  maquette) ; turnTo va d'un coup
//    placeClicked(i)                               le signal d'un clic sur une
//                                                  ville (-1 : sur le globe, a
//                                                  cote) ; hoverPlaceChanged(i)
//    placeAt, screenOf, hoverAt                    les essais, les captures
//  L'infobulle (survol d'une marque, tout de suite) : << Lyon, FR >> et le nombre
//  de chaque calque montre ; un groupe dit ses autres villes (trois noms, puis
//  << et 6 autres >>). Elle est dessinee dans la passe du dessus (au-dessus de ce
//  que la page pose sur le globe : la carte d'une ville).
//
//  CE QUE CA COUTE. L'Admin redessine toute la fenetre a chaque image (vsync) ;
//  le globe ne refait son image (le disque et les points des terres, rasterises
//  ici avec un anticrenelage, une texture) que quand la vue change, et la vue
//  n'avance (rotation, elan, turnTo) qu'a 30 images par seconde au plus. Arrete
//  (survole, une ville choisie, en pause, fige), il ne recalcule rien : une image
//  posee, quelques marques (des textures en cache). Une page cachee n'est pas
//  dessinee : le temps ne s'y accumule pas (un pas vaut 0,1 s au plus).
//  Les marques, les anneaux et les glyphes sont des petites textures calculees
//  une fois (sur-echantillonnees 4 x 4), gardees par renderer.
// =============================================================================
#pragma once

#include "../Widget.hpp"
#include "../../core/Signal.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

namespace globe {

    // Les calques, dans l'ordre de la legende (planche 11).
    enum class Layer : std::uint8_t { Clients, ActivePcs, Outdated, Admins, Discoveries, Orders, Requests };
    inline constexpr std::size_t kLayerCount = 7;
    // Les formes (globe.js : rond, pouls, tri, etoile, anneau, carre, losange).
    enum class Shape : std::uint8_t { Disc, Pulse, Triangle, Star, Ring, Square, Diamond };

    struct LayerStyle {
        Layer       layer;
        const char* key;        // la cle du JSON : "clients", "actifs", "pas_a_jour", "admins", "decouvertes", "commandes", "demandes"
        const char* name;       // << PC actifs maintenant >> (UTF-8)
        const char* shortName;  // << PC actifs >> (la carte d'une ville)
        const char* hint;       // << vus il y a moins de 15 min >> (la legende)
        gfx::Color  color;
        Shape       shape;
    };
    [[nodiscard]] const std::array<LayerStyle, kLayerCount>& styles() noexcept;
    [[nodiscard]] const LayerStyle& style(Layer l) noexcept;
    [[nodiscard]] constexpr std::size_t index(Layer l) noexcept { return static_cast<std::size_t>(l); }
    // "clients" -> Layer::Clients ; faux : inconnu.
    [[nodiscard]] bool layerFromKey(std::string_view key, Layer& out) noexcept;

    // Un ensemble de calques.
    struct LayerSet {
        std::uint8_t bits{0};
        [[nodiscard]] static LayerSet all() noexcept { return LayerSet{0x7F}; }
        [[nodiscard]] static LayerSet of(std::initializer_list<Layer> ls) noexcept {
            LayerSet s;
            for (Layer l : ls) s.set(l, true);
            return s;
        }
        [[nodiscard]] bool has(Layer l) const noexcept { return (bits >> index(l)) & 1u; }
        void set(Layer l, bool on) noexcept {
            const auto m = static_cast<std::uint8_t>(1u << index(l));
            bits = on ? static_cast<std::uint8_t>(bits | m) : static_cast<std::uint8_t>(bits & ~m);
        }
        [[nodiscard]] int count() const noexcept {
            int n = 0;
            for (std::size_t i = 0; i < kLayerCount; ++i) n += (bits >> i) & 1u;
            return n;
        }
        [[nodiscard]] bool empty() const noexcept { return bits == 0; }
        friend bool operator==(LayerSet a, LayerSet b) noexcept { return a.bits == b.bits; }
    };

    // Un point du globe : une ville et ses nombres.
    struct Place {
        std::string id;        // << FR|Lyon >> (GET /admin/globe ; les connexions en direct le nomment)
        std::string city;      // << Lyon >> (UTF-8)
        std::string country;   // << FR >> (ISO 3166, deux lettres)
        std::string region;    // << Auvergne-Rhone-Alpes >> (facultatif : la carte d'une ville)
        double      lat{0.0};  // degres, nord positif
        double      lon{0.0};  // degres, est positif
        std::array<int, kLayerCount> counts{};   // par calque (index(Layer))
        int         pcs{0};    // tous ses PC (<< 9 sur 64 PC >>) ; 0 : inconnu
        [[nodiscard]] int count(Layer l) const noexcept { return counts[index(l)]; }
        void setCount(Layer l, int n) noexcept { counts[index(l)] = n; }
        [[nodiscard]] std::string label() const { return country.empty() ? city : city + ", " + country; }   // << Lyon, FR >>
    };

    // ---- les calculs, sans ecran (les essais) ----
    // La vue : le point du globe au centre du disque, et le zoom (1 : le globe entier).
    struct View {
        double lat{0.0}, lon{0.0};
        float  zoom{1.f};
    };
    inline constexpr float kMinZoom = 1.f, kMaxZoom = 3.2f;
    inline constexpr double kMinLat = -70.0, kMaxLat = 80.0;    // la souris ne passe pas les poles
    // Un point projete : (x, y) dans le disque unite (x vers la droite, y vers le HAUT), z > 0 : face visible.
    struct Projected {
        double x{}, y{}, z{};
    };
    [[nodiscard]] Projected project(const View& v, double lat, double lon) noexcept;
    // Le point du globe sous (x, y) du disque unite (y vers le haut) ; faux : hors du disque.
    [[nodiscard]] bool unproject(const View& v, double x, double y, double& lat, double& lon) noexcept;
    // La longitude ramenee dans [-180, 180[ ; le plus court ecart de `from` a `to` (-180 a 180).
    [[nodiscard]] double normalizeLon(double lon) noexcept;
    [[nodiscard]] double shortestTurn(double fromLon, double toLon) noexcept;
    // Le masque des terres (globe.js, une case de 0,25 degre) : vrai sur la terre.
    [[nodiscard]] bool isLand(double lat, double lon) noexcept;
    // Les cases de terre decodees, et celles que la table annonce (les essais : les memes).
    [[nodiscard]] int landCellCount() noexcept;
    [[nodiscard]] int landCellCountExpected() noexcept;
    // Les points des terres (x, y, z sur la sphere unite ; x vers lon 90 E, y le nord, z lon 0) d'une grille de
    // `stepDeg` degres (autant de points par degre carre a toutes les latitudes ; une rangee sur deux decalee).
    [[nodiscard]] const std::vector<float>& landDots(float stepDeg);
    // Le rayon d'une marque (px, a 100 %, hors zoom) pour `count` : 0 sous 1 ; la racine (l'aire suit le nombre).
    [[nodiscard]] float markerRadius(int count) noexcept;
    // Le nombre court (<< 1,2 k >> a partir de 1000).
    [[nodiscard]] std::string formatCount(int n);
    // Les 21 villes d'essai de la maquette (globe.js, V : Lyon, Paris, Toulouse, Bruxelles, Montreal, Geneve...),
    // leurs nombres par calque et leurs PC - la page cachee << globe-essai >> et les essais. Rien du service.
    [[nodiscard]] std::vector<Place> samplePlaces();

} // namespace globe

class GlobeView : public Widget {
public:
    explicit GlobeView(std::string id = {});
    ~GlobeView() override;

    // ---- les donnees ----
    void setPlaces(std::vector<globe::Place> places);
    [[nodiscard]] const std::vector<globe::Place>& places() const noexcept;
    [[nodiscard]] int findPlace(std::string_view id) const noexcept;   // par Place::id ; -1 : aucune
    void setLayers(globe::LayerSet layers);
    [[nodiscard]] globe::LayerSet layers() const noexcept;
    void setLayerVisible(globe::Layer l, bool on);

    // ---- la vue ----
    void setView(double lat, double lon, float zoom);                  // tout de suite (arrete un turnTo, l'elan)
    [[nodiscard]] globe::View view() const noexcept;
    // Tourner vers (lat, lon) par le plus court chemin, en `seconds` ; `zoom` <= 0 : garde le zoom. La latitude
    // visee est gardee entre -60 et 70 (la ville reste lisible). Fige : tout de suite.
    void turnTo(double lat, double lon, float zoom = 0.f, double seconds = 1.1);
    void setZoom(float zoom);                                          // de kMinZoom a kMaxZoom
    void zoomBy(float factor);
    void setHome(double lat, double lon);                              // la vue de Recentrer (zoom 1)
    void recenter();
    void setAutoRotate(bool on);                                       // Pause / reprise
    [[nodiscard]] bool autoRotate() const noexcept;
    void setRotationSpeed(float degreesPerSecond);                     // 4 (la carte), 9 (la petite)
    // Vrai : il bouge en ce moment (rotation seule, elan, turnTo).
    [[nodiscard]] bool moving() const noexcept;
    void setFrozen(bool on);
    [[nodiscard]] bool frozen() const noexcept;
    void setCompact(bool on);
    [[nodiscard]] bool compact() const noexcept;

    // ---- choisir, survoler, en direct ----
    void setSelected(int placeIndex);                                  // -1 : aucune
    [[nodiscard]] int selected() const noexcept;
    // Le trait de la ville choisie vers sa carte : un point dans le repere du widget (celui de bounds()).
    void setLeaderTarget(std::optional<gfx::Point> target);
    // Des zones (repere de bounds()) ou les noms des villes ne se posent pas.
    void setReservedAreas(std::vector<gfx::Rect> areas);
    // Le centre du disque decale de `dx` pixels (glisse en une demi-seconde ; fige : tout de suite) : une page etroite
    // dont la carte d'une ville couvre la droite du globe y garde la ville choisie visible, a gauche de la carte.
    void setCentreShift(float dx);
    [[nodiscard]] float centreShift() const noexcept;
    void pulse(int placeIndex);
    // Le temps qui passe (la peinture l'appelle, a 30 images par seconde au plus ; les essais aussi).
    void advance(double seconds);

    // ---- les essais, les captures ----
    // La ville (l'index de celle qui porte le groupe) sous `p` (repere de bounds()) ; -1 : aucune.
    [[nodiscard]] int placeAt(gfx::Point p) const;
    // Les autres villes du groupe de `placeIndex` (vide : seule, ou cachee).
    [[nodiscard]] std::vector<int> groupOf(int placeIndex) const;
    // Ou la ville est dessinee (repere de bounds()) ; vide : sur la face cachee, ou sans calque montre.
    [[nodiscard]] std::optional<gfx::Point> screenOf(int placeIndex) const;
    // Comme un mouvement de souris en `p` (repere de bounds()) : le survol et son infobulle (les captures).
    void hoverAt(gfx::Point p);
    [[nodiscard]] int hoveredPlace() const noexcept;
    // Le texte de l'infobulle de la ville survolee (les lignes separees par '\n') ; vide : aucune.
    [[nodiscard]] std::string hoverText() const;
    // Le centre et le rayon du disque (repere de bounds()).
    [[nodiscard]] gfx::Point centre() const noexcept;
    [[nodiscard]] float radius() const noexcept;

    // ---- le dessin d'un glyphe de calque (la legende, la carte d'une ville, l'infobulle) ----
    // Dans le carre `box` (14 px : la taille de la maquette) ; `on` faux : gris (un calque decoche).
    static void drawGlyph(gfx::IRenderer& r, globe::Layer l, const gfx::Rect& box, bool on = true);

    // ---- les signaux ----
    const core::SignalPtr<int> placeClicked = core::Signal<int>::create();        // -1 : a cote des villes
    const core::SignalPtr<int> hoverPlaceChanged = core::Signal<int>::create();   // -1 : plus rien

    [[nodiscard]] SizeHint sizeHint() const override;
    [[nodiscard]] std::optional<cursor::Shape> cursorAt(gfx::Point local) const override;
    [[nodiscard]] bool requestsOverlayPass() const override;   // vrai pendant une infobulle

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext& ctx) override;
    void        onPaintOverlay(const PaintContext& ctx) override;   // l'infobulle (la passe du dessus)
    EventResult onEvent(const InputEvent& ev) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ui
