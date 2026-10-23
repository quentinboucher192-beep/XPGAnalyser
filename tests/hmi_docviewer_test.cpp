// =============================================================================
//  tests/hmi_docviewer_test.cpp - 1.12.30 : LA VISIONNEUSE DE DOCUMENTS
// -----------------------------------------------------------------------------
//  Sans ecran :
//   - les genres de documents, un texte mis en pages (UTF-8, Windows-1252, lignes
//     longues), sa recherche sans casse ni accent ; une image (BMP) : une page ;
//   - la barre et les gestes (HmiDocView.hpp) : des cibles de 32 pixels, ce qui
//     part quand la place manque, l'ajustement a la largeur et a la page, le zoom
//     autour du curseur, les bornes des pages, la molette, la main, le double-clic,
//     le clavier, le menu des zooms, tourner, la saisie d'un numero de page ;
//   - LE LECTEUR PDF ABSENT (avant de le charger) : rien de trouve, puis un fichier
//     qui n'en est pas un - un PDF dit pourquoi, Generer aussi ; les images et le
//     texte se lisent toujours ;
//   - PDFium (third_party/pdfium/linux-x64/libpdfium.so, que pose
//     outils/pdfium/obtenir_pdfium.py ; absent : l'essai le dit et echoue) : la
//     notice d'exemple (4 pages A4), la page 1 rendue (le bandeau bleu, le carre
//     rouge, le blanc), une echelle plus fine, le cache borne, les vignettes, la
//     recherche (les rectangles dans la page) ; un PDF de 200 pages : ouvert vite,
//     seules les pages visibles se rendent ;
//   - le moteur : la page suit sa variable et l'ecrit (sans sauter sur son echo),
//     les gestes, la navigation interdite, les variables d'instance ;
//   - Generer : un document introuvable, une page hors du document, une variable
//     de la page inconnue, une page qui ne bouge jamais.
//
//  Argument : le dossier third_party/pdfium. DOCVIEWER_OBS=1 : chaque observation.
// =============================================================================
#include "../src/hmi/HmiAssets.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiDocView.hpp"
#include "../src/hmi/HmiDocument.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPdf.hpp"
#include "../src/hmi/HmiRuntime.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#  define XPG_GETPID _getpid
#else
#  include <unistd.h>
#  define XPG_GETPID getpid
#endif

using namespace hmi;
namespace dv = hmi::docview;
namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("DOCVIEWER_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

Variable variable(Project& p, const char* name, const char* type, const char* initial) {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    return v;
}

BlobPtr blob(const Bytes& b) { return std::make_shared<const Bytes>(b); }
BlobPtr blob(const std::string& s) { return std::make_shared<const Bytes>(s.begin(), s.end()); }

// Un BMP 24 bits, d'une couleur, un carre blanc en haut a gauche.
Bytes bmpOf(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int row = (w * 3 + 3) / 4 * 4;
    const std::uint32_t size = 54u + static_cast<std::uint32_t>(row * h);
    Bytes out(size, 0);
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    out[0] = 'B';
    out[1] = 'M';
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(w));
    put32(22, static_cast<std::uint32_t>(h));
    out[26] = 1;
    out[28] = 24;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool corner = x < 4 && y >= h - 4;   // les lignes d'un BMP vont du bas vers le haut
            auto* p = &out[54 + static_cast<std::size_t>(y * row + x * 3)];
            p[0] = corner ? 255 : b;
            p[1] = corner ? 255 : g;
            p[2] = corner ? 255 : r;
        }
    return out;
}

// Un PDF de `pages` pages A4 (un titre par page), pour la tenue en charge.
Bytes bigPdf(int pages) {
    std::string out = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    const auto object = [&](const std::string& body) {
        offsets.push_back(out.size());
        out += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    object("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (int i = 0; i < pages; ++i) kids += std::to_string(4 + 2 * i) + " 0 R ";
    object("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>");
    object("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    for (int i = 0; i < pages; ++i) {
        object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents "
               + std::to_string(5 + 2 * i) + " 0 R >>");
        const std::string ops = "BT /F1 28 Tf 60 760 Td (Page " + std::to_string(i + 1) + ") Tj ET\n0.2 0.4 0.8 rg 60 100 475 500 re f\n";
        object("<< /Length " + std::to_string(ops.size()) + " >>\nstream\n" + ops + "endstream");
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    char line[32];
    for (const auto off : offsets) {
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", off);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(out.begin(), out.end());
}

// Le pixel (x, y) d'une image : proche de la couleur attendue ?
bool near(const Rgba& img, int x, int y, int r, int g, int b, int tol = 14) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return false;
    const auto* p = &img.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4];
    return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol;
}

std::string pixelText(const Rgba& img, int x, int y) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return "(hors de l'image)";
    const auto* p = &img.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4];
    char buf[48];
    std::snprintf(buf, sizeof buf, "(%d, %d, %d)", p[0], p[1], p[2]);
    return buf;
}

std::vector<doc::Size> a4(int n) { return std::vector<doc::Size>(static_cast<std::size_t>(n), doc::Size{595.0 * 96 / 72, 842.0 * 96 / 72}); }

// ---- 1. les genres, le texte, l'image ---------------------------------------------------------------
void genresTexteImage() {
    std::printf("-- les genres de documents, un texte mis en pages, une image\n");
    check(doc::typeOfFormat("PDF") == doc::Type::Pdf && doc::typeOfFormat("png") == doc::Type::Image && doc::typeOfFormat("JPG") == doc::Type::Image
              && doc::typeOfFormat("SVG") == doc::Type::Svg && doc::typeOfFormat("TXT") == doc::Type::Text
              && doc::typeOfFormat("DOCX") == doc::Type::None,
          "les genres : PDF, PNG/JPG (image), SVG, TXT ; un DOCX ne se lit pas");
    check(doc::typeOfName("notices/VE-101.pdf") == doc::Type::Pdf && doc::typeOfName("C:\\Notices.v2\\lisez") == doc::Type::None,
          "le genre d'un chemin : son extension (pas celle d'un dossier)");
    // 120 lignes, dont une de 200 caracteres : coupee aux mots, 50 lignes par page.
    std::string text;
    for (int i = 1; i <= 120; ++i) {
        if (i == 10) {
            for (int k = 0; k < 40; ++k) text += "mot" + std::to_string(k) + " ";
            text += "\n";
        } else {
            text += "Ligne " + std::to_string(i) + (i == 77 ? " : le caf\xC3\xA9 du Ventilateur" : "") + "\n";
        }
    }
    const auto pages = doc::paginateText(Bytes(text.begin(), text.end()));
    std::size_t lines = 0;
    bool fits = true;
    for (const auto& pg : pages)
        for (const auto& l : pg) {
            ++lines;
            std::size_t cps = 0;
            for (const char c : l) cps += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
            fits = fits && cps <= static_cast<std::size_t>(doc::kTextChars);
        }
    check(pages.size() == 3 && pages[0].size() == 50 && lines > 120 && fits,
          "un texte de 120 lignes (dont une longue, coupee aux mots) : 3 pages de 50 lignes au plus, 88 caracteres par ligne");
    const std::string latin = "Caf\xE9 cr\xE8me\r\n";   // Windows-1252
    const auto p2 = doc::paginateText(Bytes(latin.begin(), latin.end()));
    check(p2.size() == 1 && !p2[0].empty() && p2[0][0] == "Caf\xC3\xA9 cr\xC3\xA8me", "un texte Windows-1252 : relu en UTF-8, sans le \\r");
    auto d = doc::openBytes(blob(text), doc::Type::Text, "journal.txt", "texte-1");
    check(d && d->waitReady(3000) && d->pageCount() == 3 && d->textLines(1).size() == 50, "le texte ouvert : 3 pages");
    if (d) {
        int wantPage = -1, wantLine = -1;
        for (std::size_t pg = 0; pg < pages.size(); ++pg)
            for (std::size_t li = 0; li < pages[pg].size(); ++li)
                if (pages[pg][li].rfind("Ligne 77 ", 0) == 0) {
                    wantPage = static_cast<int>(pg);
                    wantLine = static_cast<int>(li);
                }
        d->search("CAFE du ventilateur");
        const auto s = d->searchState();
        check(!s.running && s.hits.size() == 1 && wantPage == 1 && s.hits[0].page == wantPage && s.hits[0].line == wantLine
                  && s.hits[0].col == 14 && s.hits[0].len == 19,
              "chercher \xC2\xAB CAFE du ventilateur \xC2\xBB : sans casse ni accent, la ligne 77 (page 2), colonne 14");
    }
    // Une image : une page a sa taille.
    auto img = doc::openBytes(blob(bmpOf(40, 30, 200, 30, 30)), doc::Type::Image, "plan.bmp", "image-1");
    check(img && img->waitReady(3000) && img->pageCount() == 1 && std::fabs(img->pageSize(0).w - 40) < 1e-9
              && std::fabs(img->pageSize(0).h - 30) < 1e-9,
          "une image BMP : une page de 40 x 30");
    if (img) {
        const auto pic = img->page(0, 1.0);
        check(pic.image && pic.exact && near(*pic.image, 20, 20, 200, 30, 30, 2) && near(*pic.image, 1, 1, 255, 255, 255, 2),
              "... ses pixels : rouge, le coin haut gauche blanc");
    }
}

// ---- 2. la barre et les gestes ------------------------------------------------------------------------
void barreEtGestes() {
    std::printf("-- la barre et les gestes (sans document lu : des pages A4)\n");
    dv::Settings s;
    dv::State st = dv::initialState(s);
    const auto sizes = a4(12);
    dv::Layout l = dv::layoutOf(600, 800, s, st);
    bool targets = !l.buttons.empty() && l.pageField.h >= 32 && l.zoomField.h >= 32 && l.pageField.w >= 32;
    for (const auto& b : l.buttons) targets = targets && b.box.w >= dv::kButton && b.box.h >= dv::kButton;
    check(targets && l.buttons.size() == 9, "la barre : 9 boutons et 2 champs, chacun une cible de 32 x 32 au moins (ecran tactile)");
    const auto narrow = dv::layoutOf(260, 400, s, st);
    bool keeps = narrow.pageField.w > 0;
    std::string parts;
    for (const auto& b : narrow.buttons) parts += b.part + " ";
    keeps = keeps && parts.find("doc:prev") != std::string::npos && parts.find("doc:next") != std::string::npos
         && parts.find("doc:first") == std::string::npos;
    double right = 0;
    for (const auto& b : narrow.buttons) right = std::max(right, b.box.x + b.box.w);
    check(keeps && right <= 260 && narrow.zoomField.w == 0 && parts.find("doc:zoomin") != std::string::npos,
          "etroite (260 px) : precedente, suivante, le numero, - et + restent ; premiere, derniere et le menu des zooms partent ("
              + parts + ")");
    // L'ajustement a la largeur, a la page.
    auto c = dv::contentOf(l, st, sizes);
    check(std::fabs(c.zoom - (600 - 24) / sizes[0].w) < 1e-9 && c.pages.size() == 12, "ajuste a la largeur : le zoom remplit la zone (marges comprises)");
    st.fit = dv::Fit::Page;
    c = dv::contentOf(l, st, sizes);
    check(std::fabs(c.zoom - (l.area.h - 24) / sizes[0].h) < 1e-9, "page entiere : la hauteur de la zone");
    // Les pages visibles : trois au plus a la largeur.
    st.fit = dv::Fit::Width;
    dv::settle(st, l, sizes);
    c = dv::contentOf(l, st, sizes);
    const auto shown = dv::visiblePages(l, c);
    check(shown.size() <= 2 && shown.front().index == 0, "a la largeur : seules les pages visibles (" + std::to_string(shown.size()) + ")");
    // Aller a une page ; les bornes.
    check(!dv::goToPage(st, l, sizes, 0) && !dv::goToPage(st, l, sizes, 13) && dv::goToPage(st, l, sizes, 5) && st.page == 5,
          "aller a la page 5 ; 0 et 13 refusees (12 pages)");
    check(dv::goToPage(st, l, sizes, 12) && st.page == 12, "la derniere page : elle est la page courante (le defilement bute)");
    // Les gestes.
    auto fx = dv::gesture(st, l, sizes, "doc:first", true);
    check(fx.pageChanged && st.page == 1 && st.scrollY == 0, "Premiere page");
    fx = dv::gesture(st, l, sizes, "doc:next", true);
    check(fx.pageChanged && st.page == 2, "Page suivante");
    fx = dv::gesture(st, l, sizes, "doc:key:PageDown", true);
    check(st.page == 3, "Page suivante au clavier");
    fx = dv::gesture(st, l, sizes, "doc:key:End", true);
    check(st.page == 12, "Fin : la derniere");
    fx = dv::gesture(st, l, sizes, "doc:key:Home", true);
    check(st.page == 1, "Debut : la premiere");
    fx = dv::gesture(st, l, sizes, "doc:next", false);
    check(fx.refused && st.page == 1, "navigation interdite : la page suivante refusee");
    // La molette : 60 pixels par cran ; la page courante suit.
    const double y0 = st.scrollY;
    (void)dv::gesture(st, l, sizes, "doc:wheel:60", true);
    check(std::fabs(st.scrollY - y0 - 60) < 1e-9, "la molette : 60 pixels plus bas");
    for (int i = 0; i < 30; ++i) (void)dv::gesture(st, l, sizes, "doc:wheel:60", true);
    check(st.page >= 2, "en descendant, la page courante suit (" + std::to_string(st.page) + ")");
    // Le zoom autour du curseur : le point sous la souris y reste.
    (void)dv::goToPage(st, l, sizes, 1);
    const double mx = 300, my = 300;
    auto before = dv::contentOf(l, st, sizes);
    const double u = (mx - (before.originX + (before.width - before.widths[0]) / 2)) / before.widths[0];
    const double v = (my - (before.originY + before.tops[0])) / before.heights[0];
    (void)dv::gesture(st, l, sizes, "doc:zoomat:2:300:300", true);
    auto after = dv::contentOf(l, st, sizes);
    const double u2 = (mx - (after.originX + (after.width - after.widths[0]) / 2)) / after.widths[0];
    const double v2 = (my - (after.originY + after.tops[0])) / after.heights[0];
    check(st.fit == dv::Fit::None && std::fabs(after.zoom - before.zoom * 2) < 1e-6 && std::fabs(u - u2) < 1e-6 && std::fabs(v - v2) < 1e-6,
          "Ctrl+molette : zoom x2 autour du curseur, le point sous la souris ne bouge pas");
    // La main.
    const double sx = st.scrollX, sy = st.scrollY;
    (void)dv::gesture(st, l, sizes, "doc:pan:-40:-25", true);
    check(std::fabs(st.scrollX - sx - 40) < 1e-9 && std::fabs(st.scrollY - sy - 25) < 1e-9, "glisser : la page suit la main");
    // Double-clic : 100 % <-> la largeur.
    (void)dv::gesture(st, l, sizes, "doc:dbl:300:300", true);
    check(st.fit == dv::Fit::Width, "double-clic (zoome) : ajuste a la largeur");
    (void)dv::gesture(st, l, sizes, "doc:dbl:300:300", true);
    check(st.fit == dv::Fit::None && std::fabs(st.zoom - 1.0) < 1e-9, "double-clic (ajuste) : 100 %");
    // Le zoom pas a pas, Ctrl+0.
    (void)dv::gesture(st, l, sizes, "doc:zoomin", true);
    check(std::fabs(st.zoom - 1.25) < 1e-9, "Zoom + : 125 %");
    (void)dv::gesture(st, l, sizes, "doc:key:Minus", true);
    (void)dv::gesture(st, l, sizes, "doc:key:Minus", true);
    check(std::fabs(st.zoom - 0.75) < 1e-9, "- deux fois : 75 %");
    (void)dv::gesture(st, l, sizes, "doc:key:Zero", true);
    check(st.fit == dv::Fit::Page, "Ctrl+0 : la page entiere");
    // Le menu des zooms.
    (void)dv::gesture(st, l, sizes, "doc:zoommenu", true);
    l = dv::layoutOf(600, 800, s, st);
    check(st.zoomMenu && l.zoomMenu.size() == std::size(dv::kZoomChoices), "le menu des zooms : ses 10 choix");
    check(dv::hitPart(l, st, sizes, l.zoomMenu[7].x + 5, l.zoomMenu[7].y + 5) == "doc:zoom:7", "... un clic sur 200 %");
    (void)dv::gesture(st, l, sizes, "doc:zoom:7", true);
    check(!st.zoomMenu && std::fabs(st.zoom - 2.0) < 1e-9 && st.fit == dv::Fit::None, "200 % choisi, le menu se ferme");
    dv::Fit f = dv::Fit::None;
    double z = 0;
    check(dv::parseZoom("175 %", f, z) && f == dv::Fit::None && std::fabs(z - 1.75) < 1e-9 && dv::parseZoom("Ajuster a la largeur", f, z)
              && f == dv::Fit::Width && dv::parseZoom("page enti\xC3\xA8re", f, z) && f == dv::Fit::Page && !dv::parseZoom("vite", f, z),
          "Zoom de depart : 175 %, ajuster a la largeur, page entiere ; un mot inconnu refuse");
    // Tourner : la page couchee.
    st.fit = dv::Fit::Width;
    (void)dv::gesture(st, l, sizes, "doc:rotate", true);
    c = dv::contentOf(l, st, sizes);
    check(st.quarter == 1 && c.heights[0] < c.widths[0], "tourner : un quart de tour, la page A4 couchee");
    const hmi::Box r = dv::onScreen({0, 0, 10, 20}, sizes[0], {0, {0, 0, sizes[0].h, sizes[0].w}}, 1, 1.0);
    check(std::fabs(r.x - (sizes[0].h - 20)) < 1e-9 && std::fabs(r.y) < 1e-9 && std::fabs(r.w - 20) < 1e-9 && std::fabs(r.h - 10) < 1e-9,
          "... un rectangle de la page (en haut a gauche) passe en haut a droite");
    // Taper un numero de page.
    st.quarter = 0;
    (void)dv::gesture(st, l, sizes, "doc:page", true);
    (void)dv::typeText(st, "1a1");
    check(st.editingPage && st.pageText == "11", "le champ de la page : seulement des chiffres");
    fx = dv::typeKey(st, l, sizes, "Enter");
    check(!st.editingPage && st.page == 11 && fx.pageChanged, "Entree : la page 11");
    (void)dv::gesture(st, l, sizes, "doc:page", true);
    (void)dv::typeText(st, "99");
    (void)dv::typeKey(st, l, sizes, "Enter");
    check(st.page == 12, "99 : la derniere page (12)");
    // Navigation interdite : une seule page, celle que dit la propriete.
    dv::Settings fixed = s;
    fixed.navigation = false;
    dv::State one = dv::initialState(fixed);
    one.single = 4;
    const auto lone = dv::layoutOf(600, 800, fixed, one);
    dv::settle(one, lone, sizes);
    const auto cone = dv::contentOf(lone, one, sizes);
    bool greyed = true;
    for (const auto& b : lone.buttons)
        if (b.part == "doc:next" || b.part == "doc:prev" || b.part == "doc:thumbs") greyed = greyed && !b.enabled;
    check(cone.pages.size() == 1 && cone.pages[0] == 3 && one.page == 4 && greyed, "navigation interdite : la page 4 seule, les boutons des pages grises");
    // Les vignettes : leur colonne, un clic.
    dv::State th = dv::initialState(s);
    th.thumbs = true;
    const auto lt = dv::layoutOf(700, 800, s, th);
    const auto thumbs = dv::thumbsOf(lt, th, sizes);
    check(lt.thumbs.w >= 96 && thumbs.size() == 12 && thumbs[0].cell.h >= 32, "les vignettes : une colonne, une case par page (32 px au moins)");
    check(dv::hitPart(lt, th, sizes, thumbs[1].frame.x + 4, thumbs[1].frame.y + 4) == "doc:thumb:2", "un clic sur la 2e vignette");
    (void)dv::gesture(th, lt, sizes, "doc:thumb:2", true);
    check(th.page == 2, "... la page 2");
    // La barre de recherche.
    (void)dv::gesture(th, lt, sizes, "doc:search", true);
    const auto ls = dv::layoutOf(700, 800, s, th);
    check(th.searchOpen && th.editingSearch && ls.searchBar.h == dv::kBar && ls.searchField.w > 100 && ls.area.y == 2 * dv::kBar,
          "Rechercher : sa barre sous la barre d'outils, son champ en saisie");
}

// ---- 3. sans lecteur PDF --------------------------------------------------------------------------------
void sansLecteur(const fs::path& work) {
    std::printf("-- sans lecteur PDF (avant de le charger)\n");
    const fs::path empty = work / "vide";
    fs::create_directories(empty);
    pdf::setSearchPaths({empty.string()});
    const auto lib = pdf::library();
    check(!lib.ok && !lib.found && lib.why.find("introuvable") != std::string::npos, "aucun libpdfium.so : " + lib.why);
    check(pdf::missingAdvice().find("dossier de l'application") != std::string::npos, "... et ou le mettre : " + pdf::missingAdvice());
    auto d = doc::openBytes(blob(doc::sampleNotice()), doc::Type::Pdf, "notice.pdf", "absent-1");
    const bool failed = d && !d->waitReady(3000) && d->state() == doc::Document::State::Failed;
    check(failed && d->needsEngine() && d->error().find("Lecteur PDF absent") != std::string::npos && d->pageCount() == 0,
          "un PDF sans lecteur : \xC2\xAB " + (d ? d->error() : std::string("?")) + " \xC2\xBB");
    d.reset();
    // Un fichier qui n'est pas une bibliotheque.
    const fs::path broken = work / "casse";
    fs::create_directories(broken);
    std::ofstream(broken / std::string(pdf::libraryFileName())) << "pas une bibliotheque";
    pdf::setSearchPaths({broken.string()});
    const auto lib2 = pdf::library();
    check(!lib2.ok && lib2.found && lib2.why.find("illisible ou incompatible") != std::string::npos, "un faux libpdfium.so : " + lib2.why);
    check(pdf::missingAdvice().find("remplace") != std::string::npos, "... le remplacer : " + pdf::missingAdvice());
    // Generer le dit ; l'image et le texte se lisent toujours.
    Project p;
    p.assets.resources.push_back(makeResource(p, "notice.pdf", blob(doc::sampleNotice()), "essai"));
    View v = makeView(p, "Vue_Doc");
    const Id id = edit::add(p, v, Kind::DocumentViewer, 10, 10);
    v.object(id)->set("document", "notice.pdf");
    p.views = {v};
    bool told = false;
    for (const auto& i : generate(p, [](std::string_view) { return false; }))
        told = told || (i.object == id && i.property == "document" && i.message.find("Lecteur PDF absent") != std::string::npos);
    check(told, "Generer : \xC2\xAB Lecteur PDF absent... \xC2\xBB sur la visionneuse d'un PDF");
    auto img = doc::openBytes(blob(bmpOf(8, 8, 10, 200, 10)), doc::Type::Image, "vert.bmp", "absent-image");
    check(img && img->waitReady(3000) && img->pageCount() == 1, "sans lecteur PDF, une image se lit");
}

// ---- 4. PDFium -----------------------------------------------------------------------------------------
bool avecPdfium(const fs::path& pdfiumDir) {
#if defined(_WIN32)
    const fs::path platform = pdfiumDir / "win-x64";
#else
    const fs::path platform = pdfiumDir / "linux-x64";
#endif
    std::printf("-- PDFium (%s)\n", platform.string().c_str());
    pdf::setSearchPaths({platform.string()});
    const auto lib = pdf::library();
    if (!lib.ok) {
        std::printf("  libpdfium.so absent ou illisible (%s) : python3 outils/pdfium/obtenir_pdfium.py le pose dans "
                    "third_party/pdfium/linux-x64/\n", lib.why.c_str());
        check(false, "PDFium charge depuis third_party/pdfium/linux-x64");
        return false;
    }
    check(true, "PDFium charge : " + lib.file);
    auto d = doc::openBytes(blob(doc::sampleNotice()), doc::Type::Pdf, "notice.pdf", "notice-1");
    check(d && d->waitReady(5000) && d->pageCount() == 4, "la notice d'exemple : 4 pages");
    if (!d || d->pageCount() != 4) return true;
    const auto size = d->pageSize(0);
    check(std::fabs(size.w - 595.0 * 96 / 72) < 0.5 && std::fabs(size.h - 842.0 * 96 / 72) < 0.5, "une page A4 : 793 x 1123 pixels a 100 %");
    // La page 1 a 100 % : le bandeau bleu, le carre rouge, le blanc.
    check(d->waitPage(0, 1.0, 5000), "la page 1 rendue a 100 %");
    const auto pic = d->page(0, 1.0);
    if (!pic.image) return true;
    const auto& img = *pic.image;
    const double k = 96.0 / 72.0;
    const int bx = static_cast<int>(560 * k), by = static_cast<int>(20 * k);
    const int rx = static_cast<int>(520 * k), ry = static_cast<int>(110 * k);
    const int wx = static_cast<int>(300 * k), wy = static_cast<int>(600 * k);
    check(img.width == 793 && img.height == 1123, "l'image : 793 x 1123 (" + std::to_string(img.width) + " x " + std::to_string(img.height) + ")");
    check(near(img, bx, by, 0x2F, 0x6F, 0xD6), "le bandeau bleu #2F6FD6 en haut " + pixelText(img, bx, by));
    check(near(img, rx, ry, 0xE5, 0x53, 0x4B), "le carre rouge #E5534B a droite " + pixelText(img, rx, ry));
    check(near(img, wx, wy, 255, 255, 255, 2), "le blanc sous le sommaire " + pixelText(img, wx, wy));
    // Plus fin : 200 %.
    const std::size_t renders = d->renderCount();
    check(d->waitPage(0, 2.0, 5000) && d->page(0, 2.0).image && d->page(0, 2.0).image->width == 1587, "a 200 % : 1587 pixels de large");
    (void)d->page(0, 2.0);
    (void)d->page(0, 2.0);
    check(d->renderCount() == renders + 1, "la meme echelle redemandee : rien n'est rendu de nouveau (le cache)");
    const auto pic2 = d->page(0, 1.9);
    check(pic2.image && pic2.exact && pic2.image->width == 1587, "1,9 : le palier de 2 (des sixiemes d'octave), deja la");
    // Le cache borne : 8 Mo.
    doc::setCacheBudget(8u * 1024u * 1024u);
    for (const double s : {0.5, 0.75, 1.0, 1.5}) (void)d->waitPage(1, s, 5000);
    check(d->cachedBytes() <= 8u * 1024u * 1024u || d->cachedPages() == 1,
          "le cache borne a 8 Mo : " + std::to_string(d->cachedBytes() / 1024) + " Kio en " + std::to_string(d->cachedPages()) + " images");
    doc::setCacheBudget(160u * 1024u * 1024u);
    // Les vignettes : basse definition.
    check(d->waitThumbnail(2, 5000) && d->thumbnail(2).image && d->thumbnail(2).image->width == 120, "une vignette : 120 pixels de large");
    // La recherche.
    d->search("VARIATEUR");
    check(d->waitSearch(5000), "la recherche se termine");
    const auto s = d->searchState();
    bool p0 = false, p1 = false, inside = true;
    for (const auto& h : s.hits) {
        p0 = p0 || h.page == 0;
        p1 = p1 || h.page == 1;
        for (const auto& r : h.rects)
            inside = inside && r.x >= 0 && r.y >= 0 && r.x + r.w <= size.w + 1 && r.y + r.h <= size.h + 1 && r.w > 0 && r.h > 0;
    }
    check(s.hits.size() >= 3 && p0 && p1 && inside,
          "\xC2\xAB VARIATEUR \xC2\xBB (sans casse) : " + std::to_string(s.hits.size()) + " occurrences, pages 1 et 2, dans la page");
    // La premiere occurrence de la page 1 : sous le bandeau (en haut de la page).
    bool high = false;
    for (const auto& h : s.hits)
        if (h.page == 0 && !h.rects.empty()) {
            high = h.rects[0].y > 70 * k && h.rects[0].y < 160 * k;
            break;
        }
    check(high, "... celle du titre de la page 1 est sous le bandeau");
    d->search("d\xC3\xA9" "faut");
    (void)d->waitSearch(5000);
    bool last = false;
    for (const auto& h : d->searchState().hits) last = last || h.page == 3;
    check(last, "\xC2\xAB d\xC3\xA9" "faut \xC2\xBB : jusqu'a la page 4");
    // Montrer une occurrence : sa page.
    dv::Settings set;
    dv::State st = dv::initialState(set);
    const auto sizes = d->pageSizes();
    const auto l = dv::layoutOf(600, 700, set, st);
    const auto hits = d->searchState().hits;
    (void)dv::gesture(st, l, sizes, "doc:hitnext", true, hits);
    check(st.hit == 0 && st.page == hits[0].page + 1, "occurrence suivante : la premiere, sa page");
    // Un PDF de 200 pages : ouvert vite, seules les pages visibles se rendent.
    const auto t0 = std::chrono::steady_clock::now();
    auto big = doc::openBytes(blob(bigPdf(200)), doc::Type::Pdf, "gros.pdf", "gros-1");
    const bool opened = big && big->waitReady(10000) && big->pageCount() == 200;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(opened && ms < 3000, "200 pages : ouvert en " + std::to_string(static_cast<int>(ms)) + " ms");
    if (opened) {
        const auto bs = big->pageSizes();
        dv::State bst = dv::initialState(set);
        const auto bl = dv::layoutOf(600, 800, set, bst);
        (void)dv::goToPage(bst, bl, bs, 120);
        const auto bc = dv::contentOf(bl, bst, bs);
        const auto vis = dv::visiblePages(bl, bc);
        for (const auto& pl : vis) (void)big->waitPage(pl.index, bc.zoom, 5000);
        check(!vis.empty() && vis.size() <= 3 && vis.front().index == 119 && big->renderCount() == vis.size(),
              "a la page 120 : " + std::to_string(vis.size()) + " pages visibles, autant de rendus (" + std::to_string(big->renderCount()) + ")");
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 60; ++i) {
            (void)dv::gesture(bst, bl, bs, "doc:wheel:240", true);
            for (const auto& pl : dv::visiblePages(bl, dv::contentOf(bl, bst, bs))) (void)big->page(pl.index, bc.zoom);
        }
        const double scrollMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        check(scrollMs < 200, "60 crans de molette (les pages demandees, sans attendre) : " + std::to_string(static_cast<int>(scrollMs)) + " ms");
        for (int i = 0; i < 200; ++i) (void)big->thumbnail(i);
        check(big->waitThumbnail(199, 15000), "les 200 vignettes se font sans bloquer");
    }
    return true;
}

// ---- 5. le moteur ------------------------------------------------------------------------------------------
void moteur(bool engine) {
    std::printf("-- le moteur : la page suit sa variable, les gestes, la navigation interdite\n");
    Project p;
    const std::string text = [] {
        std::string t;
        for (int i = 1; i <= 160; ++i) t += "Etape " + std::to_string(i) + "\n";
        return t;
    }();
    p.assets.resources.push_back(makeResource(p, "procedure.txt", blob(text), "essai"));
    if (engine) p.assets.resources.push_back(makeResource(p, "notice.pdf", blob(doc::sampleNotice()), "essai"));
    p.programs.variables.push_back(variable(p, "Etape", "INT", "1"));
    p.programs.variables.push_back(variable(p, "Lire", "BOOL", "TRUE"));
    View v = makeView(p, "Vue_Doc");
    p.config.cycleMs = 50;
    const Id id = edit::add(p, v, Kind::DocumentViewer, 10, 10);
    Object& o = *v.object(id);
    o.name = "Notice";
    o.set("document", "procedure.txt");
    o.set("variable", "Etape");
    o.setNumber("w", 500);
    o.setNumber("h", 700);
    const Id fixedId = edit::add(p, v, Kind::DocumentViewer, 600, 10);
    v.object(fixedId)->name = "Fixe";
    v.object(fixedId)->set("document", "procedure.txt");
    v.object(fixedId)->set("variable", "Etape");
    v.object(fixedId)->setFlag("allowNavigation", false);
    p.config.startView = v.id;
    p.views = {v};
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    double now = 0;
    const auto settle = [&](const std::function<bool()>& done) {
        for (int i = 0; i < 300; ++i) {
            now += 0.05;
            rt.tick(now);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    const auto ready = [&] {
        const auto* d = rt.docViewer(id);
        return d && d->ready;
    };
    check(settle(ready), "le texte de la procedure s'ouvre en marche");
    const auto* d = rt.docViewer(id);
    check(d && d->sizes.size() == 4 && d->state.page == 1, "4 pages ; la page 1 (Etape = 1)");
    sim::Value pc;
    check(rt.publicRead("Vue_Doc.Notice.PageCount", pc) && pc.asReal() == 4, "Vue_Doc.Notice.PageCount = 4");
    // Le programme change d'etape : la visionneuse y va.
    rt.environment().write("Etape", sim::Value::integer(sim::Type::Int, 3));
    rt.tick(now += 0.05);
    check(d->state.page == 3, "Etape := 3 : la page 3");
    sim::Value cur;
    check(rt.publicRead("Vue_Doc.Notice.CurrentPage", cur) && cur.asReal() == 3, "Vue_Doc.Notice.CurrentPage = 3");
    // L'operateur tourne la page : la variable suit, sans sauter sur son echo.
    rt.objectPart(id, "doc:next", now);
    sim::Value e;
    (void)rt.environment().read("Etape", e);
    check(d->state.page == 4 && e.asReal() == 4, "Page suivante : la page 4, ecrite dans Etape");
    rt.objectPart(id, "doc:prev", now);
    rt.objectPart(id, "doc:wheel:40", now);
    const double y = d->state.scrollY;
    rt.tick(now += 0.05);
    rt.tick(now += 0.05);
    check(d->state.page == 3 && std::fabs(d->state.scrollY - y) < 1e-9, "Page precedente puis la molette : la page 3 garde sa place (pas de saut en haut)");
    // Un clic par sa place : le bouton sous le point.
    // (la disposition se refait a chaque geste : on garde la place du bouton, pas un pointeur dans elle)
    hmi::Box next{};
    for (const auto& b : d->layout.buttons)
        if (b.part == "doc:next") next = b.box;
    check(next.w >= dv::kButton, "le bouton Page suivante dans la barre du moteur");
    check(rt.docPartAt(id, next.x + 5, next.y + 5) == "doc:next", "docPartAt : le bouton sous le point");
    rt.objectPart(id, "doc:at:" + formatNumber(next.x + 5) + ":" + formatNumber(next.y + 5), now);
    check(d->state.page == 4, "un clic sur le bouton (doc:at:x:y) : la page suivante");
    // Les vignettes s'ouvrent : la zone retrecit (le zoom ajuste change), la page lue reste en haut.
    {
        rt.objectPart(id, "doc:prev", now);   // la page 3 (la 4, derniere, ne monte pas en haut dans une zone plus etroite)
        const int pageBefore = d->state.page;
        const double zoomBefore = dv::contentOf(d->layout, d->state, d->sizes).zoom;
        rt.objectPart(id, "doc:thumbs", now);
        const auto c = dv::contentOf(d->layout, d->state, d->sizes);
        check(d->state.thumbs && d->state.page == pageBefore && c.zoom < zoomBefore
                  && std::fabs(d->state.scrollY - (c.tops[static_cast<std::size_t>(pageBefore - 1)] - dv::kMargin)) < 1.0,
              "les vignettes s'ouvrent : la zone r\xC3\xA9tr\xC3\xA9" "cit, la page lue reste en haut");
        rt.objectPart(id, "doc:thumbs", now);
        check(!d->state.thumbs && d->state.page == pageBefore, "... et se ferment : toujours elle");
    }
    // La visionneuse prend le clavier : + zoome, le numero de page se tape.
    rt.objectPart(id, "doc:area", now);
    check(rt.docFocused() && !rt.docTyping(), "un clic dans les pages : la visionneuse a le clavier");
    const double z0 = dv::contentOf(d->layout, d->state, d->sizes).zoom;
    rt.typeText("+", now);
    check(dv::contentOf(d->layout, d->state, d->sizes).zoom > z0, "+ au clavier : plus gros");
    rt.objectPart(id, "doc:page", now);
    check(rt.docTyping(), "le champ de la page en saisie");
    rt.typeText("2", now);
    rt.typeKey(EditKey::Enter, now);
    (void)rt.environment().read("Etape", e);
    check(d->state.page == 2 && e.asReal() == 2 && !rt.docTyping(), "2 et Entree : la page 2, Etape = 2");
    // Navigation interdite : la page suit Etape, un geste de page est refuse.
    rt.tick(now += 0.05);
    const auto* f = rt.docViewer(fixedId);
    check(f && f->ready && f->state.single == 2 && !f->navigation, "Navigation decochee : la seule page 2 (Etape)");
    rt.objectPart(fixedId, "doc:next", now);
    check(f->state.single == 2 && f->state.page == 2, "... la page suivante refusee");
    rt.objectPart(fixedId, "doc:zoomin", now);
    check(f->state.fit == dv::Fit::None, "... mais le zoom reste permis");
    // Un autre document par sa propriete (une formule) : la visionneuse le rouvre.
    if (engine) {
        p.views[0].object(id)->set("document", "notice.pdf");
        const auto pdfReady = [&] {
            const auto* x = rt.docViewer(id);
            return x && x->ready && x->document && x->document->type() == doc::Type::Pdf;
        };
        check(settle(pdfReady), "un autre document : la notice PDF s'ouvre");
        d = rt.docViewer(id);
        check(d && d->sizes.size() == 4 && d->state.page == 2, "... a la page d'Etape (2)");
    }
    // Un document introuvable : son pourquoi.
    p.views[0].object(id)->set("document", "absent.pdf");
    (void)settle([&] { return !rt.docViewer(id)->why.empty(); });
    sim::Value err;
    check(rt.publicRead("Vue_Doc.Notice.DocumentError", err) && err.asString().find("introuvable") != std::string::npos,
          "un document introuvable : DocumentError le dit (" + err.asString() + ")");
    rt.stop(now);
}

// ---- 6. Generer --------------------------------------------------------------------------------------------
void generer(bool engine, const fs::path& work) {
    std::printf("-- Generer\n");
    Project p;
    p.assets.resources.push_back(makeResource(p, "notice.pdf", blob(doc::sampleNotice()), "essai"));
    p.programs.variables.push_back(variable(p, "Etape", "INT", "1"));
    p.programs.variables.push_back(variable(p, "Titre", "STRING", "''"));
    View v = makeView(p, "Vue_Doc");
    const auto put = [&](const char* name, const char* document) {
        const Id id = edit::add(p, v, Kind::DocumentViewer, 10, 10);
        v.object(id)->name = name;
        v.object(id)->set("document", document);
        return id;
    };
    const Id absent = put("Absent", "nulle_part.pdf");
    const Id loin = put("Loin", "notice.pdf");
    v.object(loin)->setNumber("page", 9);
    const Id inconnue = put("Inconnue", "notice.pdf");
    v.object(inconnue)->set("variable", "Etape_Inconnue");
    const Id texte = put("Texte", "notice.pdf");
    v.object(texte)->set("variable", "Titre");
    const Id fige = put("Fige", "notice.pdf");
    v.object(fige)->setFlag("allowNavigation", false);
    const Id vide = put("Vide", "");
    // Un fichier par son chemin, relatif au dossier du projet.
    std::ofstream(work / "lisez-moi.txt") << "Bonjour\n";
    const Id fichier = put("Fichier", "lisez-moi.txt");
    p.views = {v};
    const auto issues = generate(p, [](std::string_view) { return false; }, work.string());
    const auto has = [&](Id id, const char* key, const char* part) {
        for (const auto& i : issues)
            if (i.object == id && i.property == key && i.message.find(part) != std::string::npos) return true;
        return false;
    };
    check(has(absent, "document", "introuvable"), "un document introuvable (ni une ressource, ni un fichier)");
    if (engine) check(has(loin, "page", "n'en a que 4"), "la page 9 d'un document de 4 pages");
    check(has(inconnue, "variable", "Etape_Inconnue"), "une variable de la page inconnue");
    check(!has(texte, "variable", "n'est pas un entier"), "une variable de la page STRING (un numero ecrit) : acceptee");
    check(has(fige, "allowNavigation", "toujours la m\xC3\xAAme page"), "navigation interdite sans variable : la page ne bougera jamais");
    check(has(vide, "document", "aucun document"), "aucun document choisi");
    bool fileOk = true;
    for (const auto& i : issues) if (i.object == fichier && i.property == "document") fileOk = false;
    check(fileOk, "un texte par un chemin relatif au dossier du projet : trouve, rien a dire");
}

} // namespace

int main(int argc, char** argv) {
    const fs::path pdfiumDir = argc > 1 ? fs::path(argv[1]) : fs::path("third_party/pdfium");
#if defined(_WIN32)
    _putenv_s("XPG_PDFIUM", "");
#else
    unsetenv("XPG_PDFIUM");
#endif
    const fs::path work = fs::temp_directory_path() / ("xpg_docviewer_" + std::to_string(static_cast<long long>(XPG_GETPID())));
    fs::create_directories(work);
    genresTexteImage();
    barreEtGestes();
    sansLecteur(work);
    const bool engine = avecPdfium(pdfiumDir);
    moteur(engine);
    generer(engine, work);
    std::error_code ec;
    fs::remove_all(work, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
