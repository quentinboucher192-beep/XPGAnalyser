// =============================================================================
//  hmi/HmiDocument.hpp - 1.12.30 : LES DOCUMENTS DE LA VISIONNEUSE
// -----------------------------------------------------------------------------
//  Une procedure, un schema, une notice a lire sur le poste d'exploitation : un
//  PDF (PDFium, HmiPdf.hpp), une image (PNG, JPEG, BMP, GIF, SVG) ou un texte
//  (TXT). Le document vient d'une ressource du projet (par son nom) ou d'un
//  fichier (un chemin absolu, ou relatif au dossier du projet) : une variable
//  STRING qui donne le chemin change de document en marche.
//
//  CE QUI SE FAIT SUR LE FIL DES DOCUMENTS (un seul fil pour tout PDFium, qui
//  n'est pas sur entre fils) : ouvrir (lire le fichier, compter les pages, leurs
//  tailles), rendre une page a l'echelle demandee, ses vignettes, chercher dans
//  le texte. L'ecran ne bloque jamais : il demande, et dessine la meilleure image
//  qu'il a (celle d'une autre echelle en attendant). Les demandes perimees (une
//  echelle depassee, un document ferme) tombent avant d'etre faites ; ce qui est
//  visible passe devant le reste.
//
//  LE CACHE est borne : les pages rendues d'un document tiennent dans un budget
//  (160 Mo par defaut), les moins recemment demandees partent les premieres ; les
//  vignettes, en basse definition, ont le leur. Une page plus grande que 16
//  millions de pixels se rend moins finement.
//
//  LA TAILLE A 100 % d'une page, en pixels de la vue : un PDF, 1 point = 96/72 de
//  pixel (sa taille vraie sur un ecran a 96 ppp) ; une image, ses pixels ; un
//  texte, une page A4 (794 x 1123) de 50 lignes de 88 caracteres au plus.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::doc {

enum class Type : std::uint8_t { None, Pdf, Image, Svg, Text };
// Le genre d'un document d'apres son format (PDF, PNG, JPEG, JPG, BMP, GIF, SVG, TXT...) ou son extension.
[[nodiscard]] Type typeOfFormat(std::string_view formatOrExtension) noexcept;
[[nodiscard]] Type typeOfName(std::string_view fileName) noexcept;
[[nodiscard]] std::string_view typeLabel(Type) noexcept;     // "PDF", "image", "texte"

struct Size {
    double w{0}, h{0};
};

// Un texte mis en pages.
inline constexpr double kTextPageW = 794, kTextPageH = 1123, kTextMargin = 56, kTextFont = 14, kTextLine = 20;
inline constexpr int    kTextChars = 88, kTextLines = 50;
// Les lignes d'un texte (UTF-8, ou Windows-1252 s'il n'est pas UTF-8), coupees aux mots a kTextChars, par pages.
[[nodiscard]] std::vector<std::vector<std::string>> paginateText(const Bytes& data);

// Une occurrence trouvee : sa page (0..) et ses rectangles (pixels a 100 %, page non tournee, origine en haut a
// gauche) ; un texte : sa ligne, sa colonne et sa longueur (en caracteres), pour la dessiner a la mesure de l'ecran.
struct Hit {
    int              page{0};
    std::vector<Box> rects;
    int              line{-1}, col{0}, len{0};
};

class Document {
public:
    enum class State : std::uint8_t { Loading, Ready, Failed };
    struct Picture {
        std::shared_ptr<const Rgba> image;
        bool                        exact{false};    // a l'echelle demandee (sinon : la plus proche deja faite)
    };
    struct Search {
        std::string      query;
        bool             running{false};
        std::vector<Hit> hits;
    };

    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] Type               type() const noexcept { return type_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] State              state() const;
    [[nodiscard]] std::string        error() const;
    // Un PDF que PDFium ne peut pas lire faute d'etre la (ou d'une version qui convienne).
    [[nodiscard]] bool               needsEngine() const;
    [[nodiscard]] int                pageCount() const;
    [[nodiscard]] Size               pageSize(int index) const;      // 0.., non tournee ; {0,0} hors bornes
    [[nodiscard]] std::vector<Size>  pageSizes() const;              // toutes (vide : pas encore lu)
    [[nodiscard]] std::vector<std::string> textLines(int index) const;   // un texte : les lignes de la page

    // L'image de la page `index` (0..) a `scale` pixels d'ecran par pixel a 100 % (non tournee : l'ecran la tourne) :
    // la meilleure tout de suite ; celle de cette echelle est demandee si elle manque. `wanted` faux : regarder
    // seulement (rien n'est demande). Un texte n'a pas d'image (l'ecran ecrit ses lignes).
    [[nodiscard]] Picture page(int index, double scale, bool wanted = true);
    // Une vignette (basse definition, 120 pixels de large).
    [[nodiscard]] Picture thumbnail(int index);
    // Attendre (les essais, l'apercu de l'editeur), au plus `ms` : le document lu, une page a cette echelle.
    bool waitReady(int ms) const;
    bool waitPage(int index, double scale, int ms);
    bool waitThumbnail(int index, int ms);

    // Chercher un texte (sans casse) : un PDF sur le fil des documents, page par page ; un texte, tout de suite.
    void search(const std::string& query);
    [[nodiscard]] Search searchState() const;
    bool waitSearch(int ms) const;

    // Le cache (les essais, Generer) : les images gardees, leurs octets, les rendus faits depuis l'ouverture.
    [[nodiscard]] std::size_t cachedPages() const;
    [[nodiscard]] std::size_t cachedBytes() const;
    [[nodiscard]] std::size_t renderCount() const;

    // L'echelle reellement rendue pour une echelle demandee (des paliers de 1/6 d'octave, vers le haut).
    [[nodiscard]] static double renderScale(double scale) noexcept;

    struct Impl;
    explicit Document(std::shared_ptr<Impl>);   // interne (open)

private:
    std::shared_ptr<Impl> impl_;
    Type                  type_{Type::None};
    std::string           name_, key_;
};

// ---- ou est le document ---------------------------------------------------------------
struct Resolved {
    Type            type{Type::None};
    const Resource* resource{nullptr};   // une ressource du projet ...
    std::string     path;                // ... ou un fichier (chemin absolu)
    std::string     shown;               // le nom montre (la ressource, le nom du fichier)
    std::string     error;               // vide : trouve
};
// `spec` : le nom d'une ressource du projet (`assets`), sinon un chemin (absolu, ou relatif a `folder`, le dossier du
// projet ; vide : le projet n'est pas enregistre, seuls les chemins complets).
[[nodiscard]] Resolved resolve(const Assets& assets, std::string_view spec, const std::string& folder);
// Le document de `spec`, ouvert une fois (deux visionneuses du meme document le partagent ; un fichier change sur le
// disque se rouvre). Nul : `why` dit pourquoi.
[[nodiscard]] std::shared_ptr<Document> open(const Assets& assets, std::string_view spec, const std::string& folder,
                                             std::string* why = nullptr);
[[nodiscard]] std::shared_ptr<Document> openBytes(BlobPtr data, Type type, std::string name, std::string key);

// Le budget du cache des pages rendues, par document (octets) ; 160 Mo par defaut.
void setCacheBudget(std::size_t bytes);
// Change a chaque travail fini sur le fil des documents (une page rendue, un document lu) : l'ecran se redessine.
[[nodiscard]] std::uint64_t serial() noexcept;

// La notice d'exemple (l'aide, les essais, les captures) : un PDF de quatre pages - le sommaire, le schema (le
// ventilateur VE-101 et son variateur VAR-101), la mise en route, les defauts et remedes. En haut de la premiere
// page, un bandeau bleu (#2F6FD6) de 70 points ; dessous, a droite, un carre rouge (#E5534B) de 40 points en (500, 90)
// (en points, depuis le coin haut gauche de la page A4 de 595 x 842).
[[nodiscard]] Bytes sampleNotice();

} // namespace hmi::doc
