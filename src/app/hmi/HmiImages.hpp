// =============================================================================
//  app/hmi/HmiImages.hpp — les ressources IHM a l'ecran : textures et donnees
// -----------------------------------------------------------------------------
//  UNE TEXTURE PAR CONTENU, PAS PAR OBJET. Vingt images citant le meme logo se
//  dessinent avec une seule texture ; elle est faite au premier dessin (un PNG
//  se decode une fois) et rendue au renderer quand plus personne ne garde ce
//  contenu (la ressource a ete remplacee ou retiree, et la pile d'annulation
//  l'a oublie).
//
//  LES TEXTES ECRITS AVEC UNE POLICE IMPORTEE passent par le meme chemin : le
//  texte est rasterise avec la police (hmi::renderFontText), blanc, puis teinte
//  a la couleur du texte au dessin. Un texte dynamique change en simulation :
//  on garde les 512 derniers.
//
//  LES FICHIERS EXTERNES montres par un Tableau sont relus quand leur date
//  change, pas a chaque image.
// =============================================================================
#pragma once

#include "../../hmi/HmiExternal.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../platform/Renderer.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace app {

class HmiImageCache {
public:
    struct Image {
        gfx::TextureId tex{};
        int width{0}, height{0};
        [[nodiscard]] bool valid() const noexcept { return tex.v != 0 && width > 0 && height > 0; }
    };
    static HmiImageCache& instance();
    // Lot 14 : UN AUTRE ECRAN, UN AUTRE RENDERER. Les textures d'un renderer ne
    // servent pas a un autre : un ecran secondaire du poste d'exploitation a son
    // cache, et le dessin qui y va le prend pour instance() le temps de dessiner.
    class Scope {
    public:
        explicit Scope(HmiImageCache& cache) noexcept;
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        HmiImageCache* previous_;
    };

    // L'image d'une ressource image (PNG, JPEG, BMP, SVG, ICO). Invalide si ce
    // n'en est pas une, si son contenu manque, ou si le renderer n'a pas d'images.
    [[nodiscard]] Image resource(gfx::IRenderer&, const hmi::Resource&);
    // Lot 10 : un SVG rasterise a la taille ou il s'affiche (pixels d'ecran),
    // recolorie au besoin : net a tous les zooms. Les tailles se gardent
    // (les 192 dernieres) : un zoom qui revient ne redessine rien.
    [[nodiscard]] Image svg(gfx::IRenderer&, const hmi::Resource&, int width, int height, const hmi::SvgRecolor&);
    // Combien de SVG ont ete rasterises depuis le lancement (les tests).
    [[nodiscard]] std::size_t svgRasterized() const noexcept { return svgRasterized_; }
    // Une ligne de texte ecrite avec une police ressource, blanche.
    [[nodiscard]] Image fontText(gfx::IRenderer&, const hmi::Resource& font, std::string_view text, float pixelHeight);
    // L'enveloppe d'un son (apercu), calculee une fois par contenu.
    [[nodiscard]] const std::vector<float>& envelope(const hmi::Resource&, int buckets);
    // Lot 16 : UN GIF ANIME, toutes ses images (une texture chacune) et leurs
    // durees ; nul si ce n'est pas un GIF, ou s'il ne se decode pas.
    struct Gif {
        std::vector<Image> frames;
        hmi::GifTiming     timing;
        [[nodiscard]] bool valid() const noexcept { return !frames.empty(); }
        // L'image d'un GIF qui tourne en boucle depuis `seconds`.
        [[nodiscard]] const Image& looping(double seconds) const;
    };
    [[nodiscard]] const Gif* gif(gfx::IRenderer&, const hmi::Resource&);

private:
    struct Entry {
        std::weak_ptr<const hmi::Bytes> blob;
        Image                          image;
        std::uint64_t                  used{0};
        bool                           failed{false};
    };
    void adopt(gfx::IRenderer&);
    void purge(gfx::IRenderer&);
    gfx::IRenderer*                          renderer_{nullptr};
    std::map<const void*, Entry>             images_;
    std::map<std::string, Entry>             texts_;
    std::map<std::string, Entry>             svgs_;             // lot 10 : "contenu|l|h|couleur"
    struct GifEntry {
        std::weak_ptr<const hmi::Bytes> blob;
        Gif                             gif;
        bool                            failed{false};
    };
    std::map<const void*, GifEntry>          gifs_;             // lot 16
    std::size_t                              svgRasterized_{0};
    struct Envelope { std::weak_ptr<const hmi::Bytes> blob; std::vector<float> minMax; };
    std::map<const void*, Envelope>          envelopes_;
    std::uint64_t                            tick_{0};
};

// Lot 13 : la largeur d'un texte comme l'ecran l'ecrit - la police de
// l'interface ("Sans"), ou une police ressource du projet. Pour Generer (les
// textes qui debordent). `project` doit vivre pendant l'appel.
[[nodiscard]] std::function<double(std::string_view, std::string_view, double)> hmiTextMeasure(const hmi::Project& project);

// Le dossier du projet ouvert : les chemins relatifs des fichiers externes
// partent de la. Pose par l'espace de travail a chaque ouverture de projet.
void setHmiProjectFolder(std::string folder);
[[nodiscard]] const std::string& hmiProjectFolder();

// Le contenu d'un fichier externe, relu seulement quand sa date change.
[[nodiscard]] std::shared_ptr<const hmi::ExternalData> hmiExternalData(const hmi::ExternalFile&, std::size_t maxRows = 200);

} // namespace app
