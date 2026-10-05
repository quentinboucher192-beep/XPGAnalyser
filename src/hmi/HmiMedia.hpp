// =============================================================================
//  hmi/HmiMedia.hpp — ce qu'un fichier de ressource contient, lu dans ses octets
// -----------------------------------------------------------------------------
//  Le gestionnaire de ressources montre, pour chaque fichier importe : un
//  apercu, les dimensions (images, videos), la duree (sons, videos), le poids,
//  la famille (polices). Tout vient DES OCTETS, pas de l'extension : une image
//  renommee en .png qui est un JPEG se lit comme un JPEG, et un fichier coupe
//  le dit au lieu d'afficher des dimensions inventees.
//
//  Bibliotheques d'un seul fichier (third_party/) : stb_image (PNG, JPEG, BMP),
//  nanosvg (SVG), minimp3 (MP3), stb_truetype (polices). ICO, WAV, MP4 et WEBM
//  sont lus ici : leurs en-tetes sont courts et documentes.
//
//  LES VIDEOS NE SONT PAS DECODEES : H.264, VP8, VP9 demandent un vrai
//  decodeur, qu'aucune bibliotheque d'un seul fichier ne fournit. On en lit le
//  conteneur (duree, dimensions, codec) ; l'apercu est une image d'attente.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

using Bytes   = std::vector<std::uint8_t>;
using BlobPtr = std::shared_ptr<const Bytes>;

// ---- Lot API 8 : glisser de fichiers, 2e partie ----
//  Document (en fin : rien ne se renumerote) : tout autre fichier (PDF, DOCX,
//  ZIP, TXT...) garde tel quel dans les Ressources, son extension comme format.
enum class MediaKind : std::uint8_t { Image, Sound, Video, Font, Unknown, Document };
[[nodiscard]] std::string_view mediaKindLabel(MediaKind) noexcept;   // "Image", "Son"..., "Document"

struct MediaInfo {
    MediaKind   kind{MediaKind::Unknown};
    std::string format;              // "PNG", "JPEG", "BMP", "SVG", "ICO", "GIF", "WAV", "MP3", "MP4", "WEBM", "TTF", "OTF"
    int         width{0}, height{0}; // images et videos, en pixels
    double      seconds{0};          // sons et videos
    int         channels{0}, sampleRate{0}, bitsPerSample{0}, bitrateKbps{0};
    std::string codec;               // "PCM", "MPEG-1 couche III", "H.264 (avc1)", "VP9"...
    std::string family, style;       // polices
    int         glyphs{0};
    int         images{0};           // ICO, GIF (lot 16) : nombre d'images dans le fichier
    int         loop{-1};            // GIF : la boucle du fichier (0 sans fin, N fois, -1 une fois)
    std::string error;               // non vide : les octets ne disent pas ce que l'extension annonce
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

// "logo.PNG" -> "PNG", "photo.jpg" -> "JPEG", "police.otf" -> "OTF". Vide : inconnu.
[[nodiscard]] std::string formatFromExtension(std::string_view fileName);
// Un format de media connu : son genre ; vide : Unknown ; tout autre (lot API 8) : Document.
[[nodiscard]] MediaKind   kindOfFormat(std::string_view format) noexcept;
// Les extensions lues comme medias (les autres fichiers : des documents).
[[nodiscard]] std::string_view acceptedExtensions() noexcept;
// Lot API 8 : le format d'un document, son extension en majuscules ("PDF",
// "DOCX", "ZIP") ; sans extension lisible (lettres et chiffres, 8 au plus) : "FICHIER".
[[nodiscard]] std::string documentFormat(std::string_view fileName);

// Tout ce qu'on sait dire du fichier. `fileName` sert seulement a departager.
[[nodiscard]] MediaInfo inspectMedia(const Bytes& data, std::string_view fileName);

// Une image RGBA, 4 octets par pixel, ligne par ligne.
struct Rgba {
    int width{0}, height{0};
    std::vector<std::uint8_t> pixels;
};
// Decoder une image : PNG, JPEG, BMP ; ICO (sa plus grande image) ; SVG
// (rasterise, le plus grand cote ramene a `maxSide`).
[[nodiscard]] bool decodeImage(const Bytes& data, std::string_view format, Rgba& out, int maxSide = 1024,
                               std::string* error = nullptr);

// ---- lot 16 : le GIF anime ------------------------------------------------------
//  Un GIF est une image : il s'importe, se montre dans un objet Image (en boucle)
//  et dans l'objet GIF anime (une fois, N fois, en boucle, a sa vitesse). Ses
//  images et leurs durees se lisent dans le fichier (les blocs, sans decoder) ;
//  les pixels se decodent a part (stb_image), toutes les images a la taille du GIF.
//  Une duree de 0 ou 10 ms vaut 100 ms, comme dans les navigateurs.
struct GifTiming {
    int              width{0}, height{0};
    std::vector<int> delaysMs;        // une duree par image
    int              totalMs{0};      // un tour
    int              fileLoop{-1};    // la boucle ecrite dans le fichier : 0 sans fin, N tours en tout ; -1 : une fois (rien d'ecrit)
    [[nodiscard]] std::size_t frames() const noexcept { return delaysMs.size(); }
    // L'image a montrer `ms` apres le debut d'un tour (0 <= ms < totalMs).
    [[nodiscard]] std::size_t frameAt(double ms) const noexcept;
};
[[nodiscard]] bool isGif(const Bytes& data) noexcept;
[[nodiscard]] bool gifTiming(const Bytes& data, GifTiming& out, std::string* error = nullptr);
struct GifFrames {
    GifTiming         timing;
    std::vector<Rgba> frames;
};
// Toutes les images (au plus `maxFrames`, et 256 Mo de pixels).
[[nodiscard]] bool decodeGif(const Bytes& data, GifFrames& out, std::string* error = nullptr, int maxFrames = 600);
// Ecrire un GIF anime (les exemples, les essais, le didacticiel) : des images en
// couleurs indexees (`palette` : de 2 a 256 couleurs 0xRRGGBB), une duree par
// image (ms, au centieme), la boucle (0 : sans fin ; N : N tours ; -1 : rien
// d'ecrit), l'index transparent (-1 : aucun).
struct GifImage {
    std::vector<std::uint8_t> indices;     // width * height, ligne par ligne
    int                       delayMs{100};
};
[[nodiscard]] Bytes encodeGif(int width, int height, const std::vector<std::uint32_t>& palette, const std::vector<GifImage>& images,
                              int loop = 0, int transparent = -1);

// ---- lot 10 : le SVG net a toutes les tailles, recolorable ---------------------
//  Un SVG se rasterise A LA TAILLE OU IL S'AFFICHE (en pixels d'ecran) : net au
//  zoom 400 % comme dans une vignette. On peut le recolorier : une couleur
//  (#RRGGBB) remplace celles de ses formes - toutes, les remplissages, les
//  contours, ou une seule couleur (`from`). Une expression la choisit en marche.
struct SvgRecolor {
    bool          active{false};
    std::uint32_t rgb{0};             // 0xRRGGBB
    std::string   mode{"tout"};       // "tout", "remplissages", "contours", "une couleur"
    std::uint32_t from{0};            // 0xRRGGBB : la couleur remplacee (mode "une couleur")
};
[[nodiscard]] bool isSvgImage(const Bytes& data, std::string_view format);
// `width` x `height` pixels exactement (etire : l'appelant a choisi les proportions).
[[nodiscard]] bool rasterizeSvg(const Bytes& data, int width, int height, Rgba& out, const SvgRecolor* recolor = nullptr,
                                std::string* error = nullptr);

// L'enveloppe d'un son, pour la dessiner : `buckets` paires (min, max) entre
// -1 et 1, dans l'ordre du temps. WAV (PCM entier ou flottant) et MP3.
[[nodiscard]] bool soundEnvelope(const Bytes& data, std::string_view format, int buckets, std::vector<float>& minMax);

// Un son decode, pour l'ecouter : entiers 16 bits entrelaces (gauche, droite,
// gauche...). WAV (PCM 8, 16, 24, 32 bits ou flottant) et MP3.
struct Pcm {
    int rate{0}, channels{0};
    std::vector<std::int16_t> samples;
    [[nodiscard]] double seconds() const noexcept {
        return rate > 0 && channels > 0 ? static_cast<double>(samples.size()) / channels / rate : 0.0;
    }
};
[[nodiscard]] bool decodePcm(const Bytes& data, std::string_view format, Pcm& out);
// Lot 10 : le volume - chaque echantillon multiplie par `gain` (0..1, borne).
void applyGain(Pcm&, double gain);

// Du texte ecrit avec une police TTF/OTF : blanc, la couverture dans l'alpha.
// C'est l'apercu d'une police importee, et ce qui dessine un texte qui l'utilise.
[[nodiscard]] bool renderFontText(const Bytes& font, std::string_view utf8, float pixelHeight, Rgba& out);

// "12,4 Ko", "3,1 Mo" : le poids d'un fichier, lisible.
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);
// "2 min 05 s", "4,2 s".
[[nodiscard]] std::string formatDuration(double seconds);

} // namespace hmi
