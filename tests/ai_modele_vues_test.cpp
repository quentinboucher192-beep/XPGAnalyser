// =============================================================================
//  tests/ai_modele_vues_test.cpp - 1.22.8 : LES MODELES DE VUES (un PV d'approbation de vues, des captures d'ecran)
// -----------------------------------------------------------------------------
//   ai_modele_vues_test [dossier third_party/pdfium]
//
//  Sans ecran ni reseau, sur un dossier IA/ jetable, avec un PV d'approbation FABRIQUE ICI (un PDF neutre : trois pages
//  << VUE : Vue_A >>, << VUE : Vue_B >>, << VUE : Vue_C >>, une capture dessinee - un fond gris, une bande d'en-tete
//  identique, des rectangles rouges, jaunes, verts, cyan -, un cartouche APPROBATION, une page COMMENTAIRES apres chacune) et
//  des images fabriquees en memoire (aucune donnee de client) :
//  1. les images : le cadre de l'ecran dans une page (findScreen), la reduction, les couleurs dominantes et leur part, le fond,
//     les roles probables, les bandes communes (l'en-tete, le bas), l'encre d'une page de commentaires, les mots des tailles ;
//  2. le texte : la reconnaissance, les vues, l'approbation, les commentaires, le document ;
//  3. le PDF fabrique : l'analyse (les vues, le recadrage, la charte, l'en-tete commun, les images du cache bornees) ;
//  4. le JSON (aller-retour), le resume pour l'IA (court), le contexte, l'outil voir_vue_modele (l'image rendue, refaite
//     si le cache est vide), les corps Anthropic et compatible OpenAI (les images du retour d'outil), la regle 24 ;
//  5. de bout en bout avec un faux fournisseur : le PV joint et lu, la demande part avec le resume et l'outil (pas le PDF),
//     la proposition cree Vue_A avec un ventilateur relie a une variable EXISTANTE trouvee par son repere, un afficheur
//     laisse non relie et DIT (<< A relier >>), un calque de reference (l'image de la vue, verrouillee, masquee en marche) ;
//     une capture choisie comme modele, une image designee par la demande.
//  Sans PDFium (third_party/pdfium absent) : les parties du PDF sont SAUTEES et dites.
// =============================================================================
#include "../src/app/Capture.hpp"
#include "../src/app/ai/AiAnthropic.hpp"
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiAttachments.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiOpenAI.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiViewTemplates.hpp"
#include "../src/export/DocKit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPdf.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace vt = app::ai::viewtpl;
namespace at = app::ai::attach;
namespace dk = exporter::doc;
using ai::json::Value;

namespace {

int g_ok = 0, g_fail = 0, g_skip = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}
void skip(const std::string& what) {
    ++g_skip;
    std::printf("  SAUTE : %s\n", what.c_str());
}

bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path() / ("xpg_ai_modele_vues_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007));
    return p;
}

void writeFile(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---- une capture d'ecran fabriquee : 400 x 300, des rectangles de couleurs franches ---------------------------------------
struct Box {
    std::uint32_t rgb;
    int           x, y, w, h;
};
// `variant` : 0, 1, 2 - l'en-tete, les defauts et le bas sont les memes ; le corps change de place.
std::vector<Box> screenBoxes(int variant) {
    return {
        {0xE0E0E0, 0, 0, 400, 300},                        // le fond
        {0x1E3C78, 0, 0, 400, 36},                         // l'en-tete (identique)
        {0xFFFFFF, 150, 8, 100, 20},                       // son titre
        {0xFF0000, 10, 44, 110, 14},                       // deux defauts
        {0xFF0000, 280, 44, 110, 14},
        {0xA0A0A0, 20 + 120 * variant, 64, 140, 190},      // une zone du corps (elle bouge)
        {0x008000, 20, 150 + 10 * variant, 360, 6},        // les conduites
        {0x008000, 200, 70, 6, 170},
        {0x808080, 40 + 40 * variant, 100, 70, 18},        // une valeur : jaune sur gris
        {0xFFFF00, 50 + 40 * variant, 105, 50, 8},
        {0x808080, 250, 200 - 10 * variant, 70, 18},
        {0xFFFF00, 260, 205 - 10 * variant, 50, 8},
        {0x80FFFF, 250, 230, 70, 16},                      // une consigne (cyan)
        {0x000000, 0, 268, 400, 32},                       // le bandeau d'alarmes
        {0xFF0000, 30, 274, 360, 8},
        {0xFFFF00, 30, 285, 360, 8},
    };
}

hmi::Rgba blank(int w, int h, std::uint32_t rgb) {
    hmi::Rgba im;
    im.width = w;
    im.height = h;
    im.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    for (std::size_t i = 0; i < im.pixels.size(); i += 4) {
        im.pixels[i] = static_cast<std::uint8_t>(rgb >> 16);
        im.pixels[i + 1] = static_cast<std::uint8_t>(rgb >> 8);
        im.pixels[i + 2] = static_cast<std::uint8_t>(rgb);
        im.pixels[i + 3] = 255;
    }
    return im;
}
void fillRect(hmi::Rgba& im, int x, int y, int w, int h, std::uint32_t rgb) {
    for (int yy = std::max(0, y); yy < std::min(im.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(im.width, x + w); ++xx) {
            auto* p = im.pixels.data() + (static_cast<std::size_t>(yy) * static_cast<std::size_t>(im.width) + static_cast<std::size_t>(xx)) * 4u;
            p[0] = static_cast<std::uint8_t>(rgb >> 16);
            p[1] = static_cast<std::uint8_t>(rgb >> 8);
            p[2] = static_cast<std::uint8_t>(rgb);
            p[3] = 255;
        }
}
hmi::Rgba screenImage(int variant) {
    hmi::Rgba im = blank(400, 300, 0xFFFFFF);
    for (const auto& b : screenBoxes(variant)) fillRect(im, b.x, b.y, b.w, b.h, b.rgb);
    return im;
}
void paste(hmi::Rgba& dst, const hmi::Rgba& src, int x, int y) {
    for (int yy = 0; yy < src.height; ++yy)
        for (int xx = 0; xx < src.width; ++xx) {
            if (x + xx < 0 || y + yy < 0 || x + xx >= dst.width || y + yy >= dst.height) continue;
            const auto* s = src.pixels.data() + (static_cast<std::size_t>(yy) * static_cast<std::size_t>(src.width) + static_cast<std::size_t>(xx)) * 4u;
            auto* d = dst.pixels.data() + (static_cast<std::size_t>(y + yy) * static_cast<std::size_t>(dst.width) + static_cast<std::size_t>(x + xx)) * 4u;
            std::copy(s, s + 4, d);
        }
}
double shareOf(const hmi::Rgba& im, std::uint32_t rgb) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < im.pixels.size(); i += 4) {
        const std::uint32_t c = (static_cast<std::uint32_t>(im.pixels[i]) << 16) | (static_cast<std::uint32_t>(im.pixels[i + 1]) << 8) | im.pixels[i + 2];
        n += c == rgb ? 1 : 0;
    }
    return static_cast<double>(n) / (static_cast<double>(im.pixels.size()) / 4.0);
}
const vt::Swatch* swatch(const vt::Palette& p, std::uint32_t rgb, int tol = 6) {
    for (const auto& s : p.colors) {
        const int dr = static_cast<int>((s.rgb >> 16) & 0xFF) - static_cast<int>((rgb >> 16) & 0xFF);
        const int dg = static_cast<int>((s.rgb >> 8) & 0xFF) - static_cast<int>((rgb >> 8) & 0xFF);
        const int db = static_cast<int>(s.rgb & 0xFF) - static_cast<int>(rgb & 0xFF);
        if (std::abs(dr) <= tol && std::abs(dg) <= tol && std::abs(db) <= tol) return &s;
    }
    return nullptr;
}
// La taille d'un PNG (son en-tete IHDR).
std::pair<int, int> pngSize(const std::string& b) {
    if (b.size() < 24 || b.compare(12, 4, "IHDR") != 0) return {0, 0};
    const auto be = [&](std::size_t at) {
        return (static_cast<int>(static_cast<unsigned char>(b[at])) << 24) | (static_cast<int>(static_cast<unsigned char>(b[at + 1])) << 16)
               | (static_cast<int>(static_cast<unsigned char>(b[at + 2])) << 8) | static_cast<int>(static_cast<unsigned char>(b[at + 3]));
    };
    return {be(16), be(20)};
}

// ---- 1. les images -------------------------------------------------------------------------------------------------------
void images() {
    std::printf("[1] les images\n");
    const hmi::Rgba s0 = screenImage(0);
    // la charte d'une capture : les couleurs dominantes, leur part exacte (des aplats), le fond, les roles probables
    const vt::Palette p = vt::paletteOf(s0);
    check(p.background == 0xE0E0E0, "le fond : #E0E0E0 (" + vt::hex(p.background) + ")");
    const vt::Swatch* red = swatch(p, 0xFF0000, 0);
    const vt::Swatch* yellow = swatch(p, 0xFFFF00, 0);
    const vt::Swatch* green = swatch(p, 0x008000, 0);
    const vt::Swatch* cyan = swatch(p, 0x80FFFF, 0);
    const vt::Swatch* gray = swatch(p, 0x808080, 0);
    check(red && yellow && green && cyan && gray, "rouge, jaune, vert, cyan, gris : trouves");
    if (red) check(std::abs(red->share - shareOf(s0, 0xFF0000)) < 0.002, "la part du rouge : " + vt::percentText(red->share) + " %");
    if (green) check(std::abs(green->share - shareOf(s0, 0x008000)) < 0.002, "la part du vert");
    check(std::abs(p.backgroundShare - shareOf(s0, 0xE0E0E0)) < 0.002, "la part du fond : " + vt::percentText(p.backgroundShare) + " %");
    check(p.colors.front().rgb == 0xE0E0E0, "le fond est la couleur la plus etendue");
    for (std::size_t i = 1; i < p.colors.size(); ++i) check(p.colors[i - 1].share >= p.colors[i].share, "les couleurs, par part decroissante");
    if (red) check(has(red->role, "d\xC3\xA9" "fauts"), "rouge : les defauts (probable) - " + red->role);
    if (green) check(has(green->role, "conduites"), "vert : les conduites (probable)");
    if (yellow) check(has(yellow->role, "valeurs"), "jaune : les valeurs (probable)");
    if (cyan) check(has(cyan->role, "consignes"), "cyan : les consignes (probable)");
    check(vt::colorWord(0xFF0000) == "rouge" && vt::colorWord(0xE0E0E0) == "gris clair" && vt::colorWord(0x80FFFF) == "cyan clair",
          "les mots des couleurs");
    // l'anticrenelage : des pixels voisins d'une couleur se fondent dans son groupe
    hmi::Rgba aa = s0;
    fillRect(aa, 10, 58, 110, 1, 0xF20808);
    const vt::Palette pa = vt::paletteOf(aa);
    check(!swatch(pa, 0xF20808, 0) && swatch(pa, 0xFF0000, 0), "l'anticrenelage se fond dans son groupe");

    // le cadre de l'ecran dans une page : une page blanche, un titre, la capture encadree, un cartouche dessous
    hmi::Rgba page = blank(1000, 700, 0xFFFFFF);
    fillRect(page, 20, 20, 960, 2, 0x000000);   // le tableau de la page
    fillRect(page, 20, 20, 2, 660, 0x000000);
    fillRect(page, 978, 20, 2, 660, 0x000000);
    fillRect(page, 20, 678, 960, 2, 0x000000);
    fillRect(page, 30, 30, 120, 12, 0x000000);  // << VUE : ... >>
    fillRect(page, 20, 50, 960, 2, 0x000000);
    fillRect(page, 149, 79, 402, 302, 0x000000);   // le cadre de la capture (un pixel)
    paste(page, s0, 150, 80);
    fillRect(page, 20, 620, 960, 2, 0x000000);  // le cartouche
    fillRect(page, 400, 620, 2, 58, 0x000000);
    fillRect(page, 40, 640, 80, 10, 0x000000);
    const vt::Rect r = vt::findScreen(page);
    check(std::abs(r.x - 149) <= 2 && std::abs(r.y - 79) <= 2 && std::abs(r.w - 402) <= 3 && std::abs(r.h - 302) <= 3,
          "le cadre de l'ecran : " + std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + " x " + std::to_string(r.h)
              + " (attendu 149,79 402 x 302)");
    const hmi::Rgba cut = vt::crop(page, r);
    check(cut.width == r.w && cut.height == r.h, "le recadrage");
    // une capture sans marge : l'image entiere ; une page blanche : rien
    const vt::Rect whole = vt::findScreen(s0);
    check(whole.x == 0 && whole.y == 0 && whole.w == 400 && whole.h == 300, "une capture sans marge : l'image entiere");
    check(vt::findScreen(blank(500, 400, 0xFFFFFF)).empty(), "une page blanche : aucun ecran");
    // la reduction : le grand cote borne
    const hmi::Rgba big = blank(2000, 1000, 0x336699);
    const hmi::Rgba small = vt::fit(big, 1280);
    check(small.width == 1280 && small.height == 640, "reduite : 1280 x 640 (" + std::to_string(small.width) + " x " + std::to_string(small.height) + ")");
    check(vt::fit(s0, 1280).width == 400, "deja assez petite : telle quelle");

    // les bandes communes : trois vues, la meme bande du haut (en-tete et defauts : 64 px) et du bas (46 px)
    const hmi::Rgba s1 = screenImage(1), s2 = screenImage(2);
    const vt::Bands b = vt::commonBands({&s0, &s1, &s2});
    check(b.views == 3 && b.headerViews == 3 && b.footerViews == 3, "les bandes : communes aux 3 vues");
    check(std::abs(b.header - 64.0 / 300.0) < 0.02, "la bande du haut : " + vt::percentText(b.header) + " % (attendu 21 %)");
    check(std::abs(b.footer - 46.0 / 300.0) < 0.02, "la bande du bas : " + vt::percentText(b.footer) + " % (attendu 15 %)");
    check((b.headerColor == 0x1E3C78 && b.headerAccent == 0xE0E0E0) || (b.headerColor == 0xE0E0E0 && b.headerAccent == 0x1E3C78),
          "les couleurs de la bande du haut : " + vt::hex(b.headerColor) + " et " + (b.headerAccent < 0 ? std::string("-") : vt::hex(static_cast<std::uint32_t>(b.headerAccent))));
    check(b.footerColor == 0x000000, "la couleur du bas : le noir du bandeau d'alarmes (" + vt::hex(b.footerColor) + ")");
    check(vt::commonBands({&s0}).header == 0, "une seule vue : pas de bande commune");

    // l'encre d'une page de commentaires (une page vierge : aucune ; des traits a la main : oui)
    hmi::Rgba notes = blank(842, 595, 0xFFFFFF);
    fillRect(notes, 40, 40, 760, 1, 0x000000);
    fillRect(notes, 40, 40, 1, 500, 0x000000);
    fillRect(notes, 800, 40, 1, 500, 0x000000);
    fillRect(notes, 40, 520, 760, 1, 0x000000);
    check(vt::inkRatio(notes) < 0.0005, "une page de commentaires vierge : pas d'encre (" + std::to_string(vt::inkRatio(notes)) + ")");
    for (int k = 0; k < 12; ++k) fillRect(notes, 120 + 30 * k, 200 + (k % 3) * 6, 26, 3, 0x202020);
    check(vt::inkRatio(notes) > 0.001, "des traits a la main : de l'encre");

    // les mots des tailles
    check(vt::ratioText(800, 600) == "4:3" && vt::ratioText(1920, 1080) == "16:9" && vt::ratioText(1280, 800) == "16:10", "les rapports");
    check(vt::ratioText(813, 586) == "4:3", "un recadrage a quelques pixels pres : 4:3");
    const auto sz = vt::likelySize(853, 640);
    check(sz.first == 800 && sz.second == 600, "la taille probable : 800 x 600 (" + std::to_string(sz.first) + " x " + std::to_string(sz.second) + ")");
    check(vt::likelySize(1920, 1080) == std::make_pair(1920, 1080), "1920 x 1080");
}

// ---- 2. le texte ----------------------------------------------------------------------------------------------------------
std::vector<std::string> sampleText() {
    return {
        "E\nD\nA\n0 01/02/2026 A.AUTEUR B.VERIF Premiere emission\nInd Date Redaction Verification Designations Visas\n"
        "N\xC2\xB0 DE DOCUMENT : ESSAI 2026 001\nPV D\xE2\x80\x99" "APPROBATION DE VUES GRAPHIQUES\nINSTALLATION D'ESSAI\nZONE 1\ncontact@exemple.invalid\n",
        "VUE : Vue_A\nAPPROBATION :\nDATE :\nVISA :\nSIGNATURE :\nOUI X NON\n03/02/2026\n",
        "COMMENTAIRES :\nDATE : VISA : SIGNATURE :\n",
        "VUE : Vue_B\nAPPROBATION : DATE : VISA : SIGNATURE :\nOUI NON X 05/02/2026 JD\n",
        "COMMENTAIRES :\nMettre le debit en cyan, comme les consignes.\nDATE : VISA : SIGNATURE :\n",
        "VUE : Vue_C\nAPPROBATION : DATE : VISA : SIGNATURE :\nOUI NON\n",
        "COMMENTAIRES :\n\nDATE : VISA : SIGNATURE :\n",
    };
}

void text() {
    std::printf("[2] le texte\n");
    const auto pages = sampleText();
    const vt::TextRecognition rec = vt::recognizeText(pages);
    check(rec.recognized && rec.confidence >= 0.9, "un PV d'approbation de vues : reconnu (" + std::to_string(rec.confidence) + ")");
    check(!vt::recognizeText({"Notice de conduite\nLe ventilateur VE-101\n", "1. Le schema\n"}).recognized, "une notice : pas un modele de vues");
    check(!vt::recognizeText({"PV D'APPROBATION DE VUES\n(pages scannees)\n"}).recognized, "le titre seul, sans page de vue : pas reconnu");
    const vt::Template t = vt::parseText(pages);
    check(t.views.size() == 3, "trois vues (" + std::to_string(t.views.size()) + ")");
    if (t.views.size() == 3) {
        check(t.views[0].name == "Vue_A" && t.views[1].name == "Vue_B" && t.views[2].name == "Vue_C", "leurs noms");
        check(t.views[0].page == 2 && t.views[1].page == 4 && t.views[2].page == 6, "leurs pages");
        check(t.views[0].commentsPage == 3 && t.views[1].commentsPage == 5, "leurs pages de commentaires");
        check(t.views[0].approval.state == "oui" && t.views[0].approval.date == "03/02/2026", "Vue_A : approuvee OUI, le 03/02/2026");
        check(t.views[1].approval.state == "non" && t.views[1].approval.date == "05/02/2026" && t.views[1].approval.visa == "JD",
              "Vue_B : NON, le 05/02/2026, visa JD (" + t.views[1].approval.state + " " + t.views[1].approval.date + " " + t.views[1].approval.visa + ")");
        check(t.views[2].approval.state.empty(), "Vue_C : non renseignee");
        check(t.views[0].comments.empty() && t.views[2].comments.empty(), "pas de commentaire ecrit pour Vue_A et Vue_C");
        check(t.views[1].comments == "Mettre le debit en cyan, comme les consignes.", "le commentaire de Vue_B : " + t.views[1].comments);
        check(t.view("2") == &t.views[1] && t.view("vue_b") == &t.views[1] && t.view("VUE : Vue_C") == &t.views[2] && !t.view("Vue"), "retrouver une vue");
    }
    check(t.doc.number == "ESSAI 2026 001", "le numero du document : " + t.doc.number);
    check(t.doc.title == "INSTALLATION D'ESSAI - ZONE 1", "le titre : " + t.doc.title);
    check(t.doc.revision == "0" && t.doc.revisionDate == "01/02/2026" && t.doc.revisionText == "Premiere emission",
          "la revision : " + t.doc.revision + " " + t.doc.revisionDate + " " + t.doc.revisionText);
}

// ---- 3. un PV fabrique (PDF) ------------------------------------------------------------------------------------------------
struct PdfPage {
    double      w{842}, h{595};
    std::string ops;
    void color(std::uint32_t rgb, bool stroke = false) {
        ops += dk::pdfNumber(((rgb >> 16) & 0xFF) / 255.0) + " " + dk::pdfNumber(((rgb >> 8) & 0xFF) / 255.0) + " " + dk::pdfNumber((rgb & 0xFF) / 255.0)
               + (stroke ? " RG\n" : " rg\n");
    }
    void rect(double x, double y, double rw, double rh, bool fillIt = true) {
        ops += dk::pdfNumber(x) + " " + dk::pdfNumber(h - y - rh) + " " + dk::pdfNumber(rw) + " " + dk::pdfNumber(rh) + (fillIt ? " re f\n" : " re S\n");
    }
    void width(double v) { ops += dk::pdfNumber(v) + " w\n"; }
    void line(double x0, double y0, double x1, double y1) {
        ops += dk::pdfNumber(x0) + " " + dk::pdfNumber(h - y0) + " m " + dk::pdfNumber(x1) + " " + dk::pdfNumber(h - y1) + " l S\n";
    }
    void text(double x, double y, double size, std::string_view utf8, bool bold = false) {
        ops += "BT /" + std::string(bold ? "F2 " : "F1 ") + dk::pdfNumber(size) + " Tf " + dk::pdfNumber(x) + " " + dk::pdfNumber(h - y) + " Td "
               + dk::pdfLiteral(dk::toWinAnsi(utf8)) + " Tj ET\n";
    }
};

std::string pdfOf(const std::vector<PdfPage>& pages) {
    std::string out = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    const auto object = [&](const std::string& body) {
        offsets.push_back(out.size());
        out += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    object("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (std::size_t i = 0; i < pages.size(); ++i) kids += std::to_string(5 + 2 * i) + " 0 R ";
    object("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>");
    object("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    object("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    for (std::size_t i = 0; i < pages.size(); ++i) {
        object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + dk::pdfNumber(pages[i].w) + " " + dk::pdfNumber(pages[i].h)
               + "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " + std::to_string(6 + 2 * i) + " 0 R >>");
        object("<< /Length " + std::to_string(pages[i].ops.size()) + " >>\nstream\n" + pages[i].ops + "\nendstream");
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto o : offsets) {
        char b[24];
        std::snprintf(b, sizeof b, "%010zu 00000 n \n", o);
        out += b;
    }
    out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return out;
}

// La capture d'une vue : 640 x 480 points en (101, 50), son cadre d'un point.
constexpr double kShotX = 101, kShotY = 50, kShotW = 640, kShotH = 480;

PdfPage viewPage(const std::string& name, int variant, const std::string& mark, const std::string& date, const std::string& visa) {
    PdfPage pg;
    pg.color(0x000000, true);
    pg.width(1);
    pg.rect(20, 14, 802, 572, false);   // le tableau de la page
    pg.line(20, 34, 822, 34);
    pg.color(0x000000);
    pg.text(24, 28, 11, "VUE : " + name, true);
    const double s = kShotW / 400.0;
    for (const auto& b : screenBoxes(variant)) {
        pg.color(b.rgb);
        pg.rect(kShotX + b.x * s, kShotY + b.y * s, b.w * s, b.h * s);
    }
    pg.color(0x000000, true);
    pg.rect(kShotX, kShotY, kShotW, kShotH, false);   // le cadre de la capture
    pg.line(20, 548, 822, 548);                        // le cartouche
    pg.line(20, 566, 822, 566);
    pg.line(320, 548, 320, 586);
    pg.line(470, 548, 470, 586);
    pg.line(620, 548, 620, 586);
    pg.color(0x000000);
    pg.text(110, 561, 10, "APPROBATION :", true);
    pg.text(360, 561, 10, "DATE :", true);
    pg.text(520, 561, 10, "VISA :", true);
    pg.text(660, 561, 10, "SIGNATURE :", true);
    pg.text(60, 580, 10, "OUI", true);
    if (mark == "oui") pg.text(100, 580, 10, "X", true);
    pg.text(200, 580, 10, "NON", true);
    if (mark == "non") pg.text(240, 580, 10, "X", true);
    if (!date.empty()) pg.text(360, 580, 10, date);
    if (!visa.empty()) pg.text(520, 580, 10, visa);
    return pg;
}

PdfPage commentsPage(const std::string& comment, bool scribbles) {
    PdfPage pg;
    pg.color(0x000000, true);
    pg.width(1);
    pg.rect(20, 14, 802, 572, false);
    pg.line(20, 34, 822, 34);
    pg.line(20, 548, 822, 548);
    pg.color(0x000000);
    pg.text(24, 28, 11, "COMMENTAIRES :", true);
    if (!comment.empty()) pg.text(40, 80, 11, comment);
    if (scribbles) {   // des traits (une annotation a la main, sans texte)
        pg.color(0x202020, true);
        pg.width(2);
        for (int row = 0; row < 3; ++row)
            for (int k = 0; k < 10; ++k) pg.line(140 + 40 * k, 200 + 40 * row + (k % 2) * 12, 170 + 40 * k, 190 + 40 * row + (k % 3) * 10);
        pg.color(0x000000, true);
        pg.width(1);
    }
    pg.color(0x000000);
    pg.text(360, 561, 10, "DATE :", true);
    pg.text(520, 561, 10, "VISA :", true);
    pg.text(660, 561, 10, "SIGNATURE :", true);
    return pg;
}

std::string makePv() {
    std::vector<PdfPage> pages;
    PdfPage cover;
    cover.w = 595;
    cover.h = 842;
    cover.color(0x000000);
    cover.text(60, 80, 10, "0  01/02/2026  A.AUTEUR  B.VERIF  Premiere emission");
    cover.text(60, 100, 10, "Ind   Date   Redaction   Verification   Designations   Visas", true);
    cover.text(60, 300, 10, "N\xC2\xB0 DE DOCUMENT : ESSAI 2026 001", true);
    cover.text(120, 340, 14, "PV D'APPROBATION DE VUES GRAPHIQUES", true);
    cover.text(150, 420, 22, "INSTALLATION D'ESSAI", true);
    cover.text(240, 450, 22, "ZONE 1", true);
    cover.text(60, 780, 9, "contact@exemple.invalid");
    pages.push_back(std::move(cover));
    pages.push_back(viewPage("Vue_A", 0, "oui", "03/02/2026", ""));
    pages.push_back(commentsPage("", false));
    pages.push_back(viewPage("Vue_B", 1, "non", "05/02/2026", "JD"));
    pages.push_back(commentsPage("Mettre le debit en cyan, comme les consignes.", false));
    pages.push_back(viewPage("Vue_C", 2, "", "", ""));
    pages.push_back(commentsPage("", true));
    return pdfOf(pages);
}

bool pdfReady() { return hmi::pdf::library().ok && hmi::pdf::canExtractText(); }

// L'analyse du PV fabrique, dans `folder` (le cache). Faux : PDFium absent.
bool analyzePv(const std::string& pdf, const std::string& folder, vt::Template& t) {
    const at::Extraction x = at::extractBytes(pdf, "pdf");
    if (!x.ok) return false;
    return vt::analyzePdf(pdf, x.pages, folder, t);
}

void pdf(const std::string& pv) {
    std::printf("[3] le PV fabrique\n");
    if (!pdfReady()) {
        skip("le PV en PDF : PDFium absent (" + hmi::pdf::library().why + ")");
        return;
    }
    const at::Extraction x = at::extractBytes(pv, "pdf");
    check(x.ok && x.pages.size() == 7, "7 pages, leur texte page par page (" + std::to_string(x.pages.size()) + ")");
    const fs::path folder = scratch() / "cache-pv";
    vt::Template t;
    check(vt::analyzePdf(pv, x.pages, U(folder), t), "le PV est reconnu et lu");
    check(t.kind == "pv" && t.views.size() == 3, "trois vues");
    if (t.views.size() != 3) return;
    check(t.views[0].name == "Vue_A" && t.views[1].name == "Vue_B" && t.views[2].name == "Vue_C", "leurs noms");
    check(t.views[1].approval.state == "non" && t.views[1].approval.date == "05/02/2026" && t.views[1].approval.visa == "JD",
          "Vue_B : NON, le 05/02/2026, visa JD (lu : " + t.views[1].approval.state + " / " + t.views[1].approval.date + " / " + t.views[1].approval.visa + ")");
    check(t.views[0].approval.state == "oui", "Vue_A : OUI (lu : " + t.views[0].approval.state + ")");
    check(has(t.views[1].comments, "Mettre le debit en cyan"), "le commentaire de Vue_B (" + t.views[1].comments + ")");
    check(t.views[2].comments.empty() && t.views[2].handwritten, "Vue_C : des commentaires a la main, sans texte");
    check(!t.views[0].handwritten, "Vue_A : sa page de commentaires est vierge");
    check(t.doc.number == "ESSAI 2026 001", "le numero du document : " + t.doc.number);
    // le recadrage : la capture a 150 points par pouce (son cadre d'un point compris)
    const double k = 150.0 / 72.0;
    const vt::Rect r = t.views[0].screen;
    check(std::abs(r.x - kShotX * k) <= 5 && std::abs(r.y - kShotY * k) <= 5 && std::abs(r.w - kShotW * k) <= 8 && std::abs(r.h - kShotH * k) <= 8,
          "le rectangle de la capture : " + std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + " x " + std::to_string(r.h));
    check(t.views[0].screenW == 800 && t.views[0].screenH == 600, "l'ecran d'origine : environ 800 x 600 (" + std::to_string(t.views[0].screenW) + ")");
    check(t.views[0].width == 1280 && std::abs(t.views[0].height - 960) <= 2, "l'image gardee : le grand cote borne a 1280 (" + std::to_string(t.views[0].width) + " x "
                                                                                    + std::to_string(t.views[0].height) + ")");
    const std::string png = readFile(folder / t.views[0].image);
    check(pngSize(png).first == t.views[0].width && pngSize(png).second == t.views[0].height, "l'image recadree est dans le cache (un PNG)");
    // la charte
    const vt::Swatch* bg = swatch(t.palette, 0xE0E0E0, 3);
    check(t.palette.background == (bg ? bg->rgb : 1), "le fond de la charte : " + vt::hex(t.palette.background));
    check(t.palette.backgroundShare > 0.25 && t.palette.backgroundShare < 0.6, "sa part : " + vt::percentText(t.palette.backgroundShare) + " %");
    const vt::Swatch* red = swatch(t.palette, 0xFF0000, 6);
    check(red && red->share > 0.03 && has(red->role, "d\xC3\xA9" "fauts"), "le rouge, ses defauts probables");
    check(swatch(t.palette, 0xFFFF00, 6) && swatch(t.palette, 0x008000, 6) && swatch(t.palette, 0x80FFFF, 6), "jaune, vert, cyan");
    check(t.bands.views == 3 && t.bands.headerViews == 3 && t.bands.header > 0.15 && t.bands.header < 0.27,
          "l'en-tete commun aux 3 vues : " + vt::percentText(t.bands.header) + " %");
    check(t.bands.footerViews == 3 && t.bands.footer > 0.1 && t.bands.footer < 0.22, "le bas commun : " + vt::percentText(t.bands.footer) + " %");
    const std::string charte = vt::charteText(t);
    check(has(charte, "fond #E0E0E0") && has(charte, "(probable)") && has(charte, "en-t\xC3\xAAte") && has(charte, "commun \xC3\xA0 3 vues sur 3"),
          "le texte de la charte : " + charte);
    // une notice ordinaire n'est pas un modele de vues
    vt::Template none;
    check(!vt::analyzePdf(pv, {"Une notice\n", "Le schema\n"}, {}, none), "un texte sans << VUE : >> : pas un modele de vues");
}

// ---- 4. le JSON, le resume, l'outil, les corps, la regle 24 -------------------------------------------------------------------
void forTheAi(const vt::Template& t, const std::string& id) {
    std::printf("[4] ce que l'IA recoit\n");
    const Value j = vt::toJson(t);
    const vt::Template back = vt::fromJson(Value(j));
    check(ai::json::write(vt::toJson(back), false) == ai::json::write(j, false), "le JSON : aller-retour a l'identique");
    vt::Attached f{id, "PV_essai.pdf", "", t};
    const std::string sum = vt::summaryText(f, true);
    check(has(sum, "Vue_A") && has(sum, "Vue_B") && has(sum, "Vue_C") && has(sum, "COMMENTAIRES \xC3\xA0 appliquer") && has(sum, "Mettre le debit en cyan"),
          "le resume : les vues, les commentaires a appliquer");
    check(has(sum, "NON (\xC3\xA0 corriger)") && has(sum, "voir_vue_modele") && has(sum, "DONN\xC3\x89" "ES viennent du projet") && has(sum, "Charte"),
          "le resume : l'approbation, l'outil, la regle, la charte");
    check(sum.size() < 4000, "le resume est court : " + std::to_string(sum.size()) + " octets");
    check(has(vt::summaryText(f, false), "jointes au tour de la demande"), "sans appel d'outil : les images au tour de la demande");
    const std::string ctx = vt::templatesPrompt({f}, true);
    check(has(ctx, "Mod\xC3\xA8les de vues joints") && has(ctx, "3 vues") && has(ctx, "voir_vue_modele"), "le contexte de la demande : " + ctx);
    check(vt::templatesPrompt({}, true).empty(), "sans modele : rien dans le contexte");
    // l'outil voir_vue_modele
    Value def;
    check(ai::json::parse(vt::viewToolJson(), def) && def.str("name") == "voir_vue_modele" && def.at("input_schema").isObject(), "l'outil : sa definition");
    {
        const ai::LocalToolReply r = vt::runViewTool({f}, "{\"vue\": \"Vue_B\"}");
        check(has(r.text, "Vue 2 sur 3") && has(r.text, "COMMENTAIRES") && has(r.text, "800 x 600"), "l'outil : le texte de la vue - " + r.text.substr(0, 120));
        check(r.images.size() == 1 && r.images.front().mediaType == "image/png" && r.images.front().name == "Vue_B.png", "l'outil : son image");
        if (!r.images.empty()) {
            const auto sz = pngSize(readFile(fs::path(std::u8string(r.images.front().path.begin(), r.images.front().path.end()))));
            check(sz.first > 0 && std::max(sz.first, sz.second) <= vt::kMaxSide, "l'image : le grand cote borne (" + std::to_string(sz.first) + " x " + std::to_string(sz.second) + ")");
            check(r.images.front().bytes > 0 && r.images.front().bytes < 400000, "l'image : " + std::to_string(r.images.front().bytes) + " octets");
        }
        // le cache vide : l'image se refait depuis la piece (un PDF : sa page rendue et recadree)
        const ai::LocalToolReply num = vt::runViewTool({f}, "{\"vue\": \"3\"}");
        check(has(num.text, "Vue_C") && num.images.size() == 1, "par son numero");
        const ai::LocalToolReply bad = vt::runViewTool({f}, "{\"vue\": \"Vue_Z\"}");
        check(bad.images.empty() && has(bad.text, "Vue introuvable") && has(bad.text, "1. Vue_A"), "une vue inconnue : la liste des vues");
        check(vt::runViewTool({}, "{\"vue\": \"1\"}").images.empty(), "aucun modele : rien");
    }
    // les corps : Anthropic (des blocs image dans le tool_result), compatible OpenAI (le message qui suit le retour)
    const ai::LocalToolReply r = vt::runViewTool({f}, "{\"vue\": \"Vue_A\"}");
    ai::ProviderRequest q;
    q.model = "modele-essai";
    q.system = "consignes";
    ai::ProviderTurn u1;
    u1.text = "Reproduis la vue Vue_A.";
    ai::ProviderTurn a1;
    a1.role = ai::ProviderTurn::Role::Assistant;
    a1.toolUseId = "toolu_v1";
    a1.toolName = "voir_vue_modele";
    a1.toolInputJson = "{\"vue\":\"Vue_A\"}";
    ai::ProviderTurn u2;
    u2.toolResultFor = "toolu_v1";
    u2.toolResultText = r.text;
    u2.toolResultImages = r.images;
    q.turns = {u1, a1, u2};
    const std::string ab = ai::anthropicBody(q);
    Value av;
    bool imageInResult = false;
    if (ai::json::parse(ab, av))
        for (const auto& m : av.at("messages").items())
            for (const auto& b : m.at("content").items())
                if (b.str("type") == "tool_result" && b.at("content").isArray())
                    for (const auto& c : b.at("content").items())
                        if (c.str("type") == "image" && c.at("source").str("media_type") == "image/png" && !c.at("source").str("data").empty()) imageInResult = true;
    check(imageInResult, "Anthropic : l'image de la vue dans le tool_result");
    const std::string ob = ai::openAIBody(q, true, false);
    Value ov;
    bool afterTool = false;
    if (ai::json::parse(ob, ov)) {
        const auto& msgs = ov.at("messages").items();
        for (std::size_t i = 0; i + 1 < msgs.size(); ++i)
            if (msgs[i].str("role") == "tool" && msgs[i + 1].str("role") == "user" && msgs[i + 1].at("content").isArray())
                for (const auto& c : msgs[i + 1].at("content").items())
                    if (c.str("type") == "image_url" && has(c.at("image_url").str("url"), "data:image/png;base64,")) afterTool = true;
    }
    check(afterTool, "compatible OpenAI : l'image dans le message de l'utilisateur qui suit le retour d'outil");
    // la regle 24
    ai::PromptInput in;
    const ai::PromptParts parts = ai::promptParts(in);
    check(has(parts.stable, "24. Un MOD\xC3\x88LE DE VUES joint") && has(parts.stable, "NON RELI\xC3\x89") && has(parts.stable, "jamais une variable invent"),
          "la regle 24 est dans les consignes");
    check(has(sum, "Comment le reproduire") && has(sum, "Ouvrir la vue") && has(sum, "conseille une mission") && has(sum, "NON RELI\xC3\x89"),
          "le detail de la regle 24 part avec le resume du modele");
    check(vt::wantsReproduction("Reproduis ces vues avec les donn\xC3\xA9" "es du projet") && vt::wantsReproduction("m\xC3\xAAme charte que le mod\xC3\xA8le")
              && !vt::wantsReproduction("Ajoute une pompe"),
          "une demande qui parle de reproduire");
}

// ---- 5. de bout en bout ---------------------------------------------------------------------------------------------------------
struct FakeScript {
    std::vector<ai::ProviderRequest>           requests;
    std::deque<std::vector<ai::ProviderEvent>> replies;
};
class FakeProvider final : public ai::Provider {
public:
    explicit FakeProvider(FakeScript& s) : s_(s) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "anthropic"; }
    bool start(ai::ProviderRequest r, std::string*) override {
        s_.requests.push_back(std::move(r));
        if (!s_.replies.empty()) {
            pending_ = std::move(s_.replies.front());
            s_.replies.pop_front();
        }
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        busy_ = false;
        return std::exchange(pending_, {});
    }
    void cancel() override { busy_ = false; }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    FakeScript&                    s_;
    std::vector<ai::ProviderEvent> pending_;
    bool                           busy_{false};
};

ai::ProviderEvent ev(ai::ProviderEvent::Type type, std::string text = {}) {
    ai::ProviderEvent e;
    e.type = type;
    if (type == ai::ProviderEvent::Type::Done) e.stopReason = std::move(text);
    else e.text = std::move(text);
    return e;
}

struct Bench {
    fs::path                       dir;
    FakeScript                     s;
    hmi::DocumentPtr               doc = std::make_shared<hmi::Document>();
    core::CommandStack             stack;
    hmi::Id                        view{hmi::kNoId};
    std::unique_ptr<ai::Assistant> as;
    double                         t{100.0};
    explicit Bench(const std::string& name) {
        dir = scratch() / name;
        fs::create_directories(dir / "Projet");
        ai::setRoot(U(dir / "IA"));
        hmi::View v = hmi::makeView(doc->project, "Vue_Accueil");
        v.width = 1024;
        v.height = 768;
        doc->project.views.push_back(v);
        view = doc->project.views.front().id;
        const auto var = [&](const char* n, const char* type, const char* d) {
            hmi::Variable x;
            x.id = doc->project.allocate();
            x.name = n;
            x.type = type;
            x.description = d;
            doc->project.programs.variables.push_back(x);
        };
        var("VE101_Marche", "BOOL", "Marche du ventilateur VE101");
        var("VE101_Defaut", "BOOL", "D\xC3\xA9" "faut du ventilateur VE101");
        var("RG101_Ouverture", "REAL", "Ouverture du registre RG101");
        doc->project.config.created = "2026-10-01T08:00:00";
        ai::Assistant::Host h;
        h.document = [this] { return doc; };
        h.activeView = [this] { return view; };
        h.selection = [] { return std::vector<hmi::Id>{}; };
        h.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        h.projectName = [] { return std::string("Essai"); };
        h.projectPath = [this] { return U(dir / "Projet"); };
        as = std::make_unique<ai::Assistant>(std::move(h));
        as->setProviderFactory([this](std::string_view) { return std::make_unique<FakeProvider>(s); });
        as->load();
        ai::Settings st = as->settings();
        st.provider = "anthropic";
        st.model = "modele-essai";
        st.localFirst = false;
        st.buildCheck = false;
        as->setSettings(st);
        as->setApiKey("cle-essai");
    }
    void step(double dt = 0.05) {
        t += dt;
        (void)as->tick(t);
    }
    void settle() {
        for (int k = 0; k < 3000 && (as->processing() || as->analysing()); ++k) {
            pause(2);
            step(0.01);
        }
        step(0.01);
    }
};

std::string proposalJson() {
    // le calque de reference : une image verrouillee, a demi transparente, masquee en marche (le code brut, ecrit par le logiciel)
    hmi::View tmp;
    tmp.id = 1;
    tmp.name = "Vue_A";
    hmi::Object img = hmi::makeObject(hmi::Kind::Image, 2, "Calque_Vue_A", 0, 0, hmi::kNoId);
    img.set("image", "Calque_Vue_A.png");
    img.setNumber("w", 800);
    img.setNumber("h", 600);
    img.setNumber("opacity", 30);
    img.setFlag("visible", false);
    img.locked = true;
    tmp.objects.push_back(img);
    const std::string code = hmi::serializeObjectCode(tmp, {img.id});
    Value ops = Value::array();
    const auto O = [](std::initializer_list<std::pair<const char*, Value>> kv) {
        Value o = Value::object();
        for (const auto& [k, v] : kv) o.set(k, v);
        return o;
    };
    ops.push(O({{"action", "creer_vue"}, {"nom", "Vue_A"}, {"proprietes", O({{"largeur", "800"}, {"hauteur", "600"}, {"fond", "#E0E0E0"}})}}));
    ops.push(O({{"action", "creer_ressource"}, {"nom", "Calque_Vue_A.png"}, {"piece_jointe", "PV_essai.pdf > Vue_A"}}));
    ops.push(O({{"action", "code"}, {"code", code}}));
    ops.push(O({{"action", "creer"}, {"genre", "Fan"}, {"nom", "VE101"}, {"proprietes", O({{"x", "120"}, {"y", "160"}, {"variable", "VE101_Marche"}})}}));
    ops.push(O({{"action", "creer"}, {"genre", "NumericDisplay"}, {"nom", "Valeur_Debit"}, {"proprietes", O({{"x", "320"}, {"y", "160"}})}}));
    ops.push(O({{"action", "creer"}, {"genre", "Text"}, {"nom", "Titre_Vue_A"}, {"proprietes", O({{"x", "20"}, {"y", "8"}, {"text", "Vue A"}})}}));
    return ai::json::write(O({{"resume", "Vue_A du mod\xC3\xA8le"}, {"operations", ops}}), false);
}

void endToEnd(const std::string& pv) {
    std::printf("[5] de bout en bout\n");
    if (!pdfReady()) {
        skip("de bout en bout avec le PV : PDFium absent");
        return;
    }
    Bench b("bout-en-bout");
    std::string why;
    const fs::path src = b.dir / "PV_essai.pdf";
    writeFile(src, pv);
    b.as->newConversation();
    check(b.as->attach(U(src), &why), "joindre le PV (" + why + ")");
    check(b.as->processing(), "le PV joint est lu avant l'envoi (l'IA en a besoin)");
    b.settle();
    const auto models = b.as->viewTemplates();
    check(models.size() == 1 && models.front().tpl.views.size() == 3, "le PV est lu comme un modele de vues (3 vues)");
    if (models.size() != 1) return;
    check(!b.as->pending().empty() && b.as->pending().front().attachment.analyses.back().summary == "Mod\xC3\xA8le de vues : 3 vues (PV d'approbation)",
          "le resume de l'analyse");
    const std::string folder = vt::cacheFolder(models.front().id);
    check(fs::exists(fs::path(std::u8string(folder.begin(), folder.end())) / "vue-2.png"), "les images recadrees sont dans le cache");
    // la reponse de l'IA : une proposition (le faux fournisseur)
    b.s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Je reproduis Vue_A avec les donn\xC3\xA9" "es du projet : le ventilateur VE101 est reli\xC3\xA9 \xC3\xA0 "
                                                                "VE101_Marche. \xC3\x80 relier : Valeur_Debit (aucune variable de d\xC3\xA9" "bit dans le projet)."),
                           [] {
                               ai::ProviderEvent e;
                               e.type = ai::ProviderEvent::Type::ToolUse;
                               e.toolUseId = "toolu_1";
                               e.toolName = "proposer_operations";
                               e.toolInputJson = proposalJson();
                               return e;
                           }(),
                           ev(ai::ProviderEvent::Type::Done, "tool_use")});
    check(b.as->send("Reproduis la vue Vue_A du mod\xC3\xA8le avec les donn\xC3\xA9" "es du projet.", &why), "envoyer (" + why + ")");
    b.step();
    check(b.s.requests.size() == 1, "une requete");
    if (b.s.requests.empty()) return;
    const ai::ProviderRequest& r = b.s.requests.front();
    // ce qui part : le resume a la place du PDF, l'outil, le contexte
    bool summary = false, pdfSent = false;
    for (const auto& t : r.turns)
        for (const auto& a : t.attachments) {
            if (a.prepared && !a.blocks.empty() && has(a.blocks.front(), "Mod\xC3\xA8le de vues") && has(a.blocks.front(), "Vue_B")) summary = true;
            if (a.name == "PV_essai.pdf") pdfSent = true;
        }
    check(summary, "le resume du modele part dans le tour qui l'a joint");
    check(!pdfSent, "le PDF n'est pas renvoye tel quel (ses images : par l'outil, quand elles servent)");
    const auto tool = std::find_if(r.localTools.begin(), r.localTools.end(), [](const ai::LocalTool& t) { return t.name == "voir_vue_modele"; });
    check(tool != r.localTools.end() && tool->runRich, "l'outil voir_vue_modele est offert");
    check(has(r.systemDynamic, "Mod\xC3\xA8les de vues joints"), "le contexte de la demande les dit");
    const std::string body = ai::anthropicBody(r);
    check(!has(body, "\"type\":\"image\"") && !has(body, "\"type\":\"document\""), "aucune image ni PDF dans la demande (rien d'inutile)");
    if (tool != r.localTools.end() && tool->runRich) {
        std::atomic<bool> stop{false};
        std::string said;
        const ai::LocalToolReply rr = tool->runRich("{\"vue\": \"Vue_A\"}", [&said](const std::string& s) { said = s; }, stop);
        check(rr.images.size() == 1 && has(rr.text, "Vue 1 sur 3"), "l'outil rend l'image de Vue_A");
        // le cache vide : l'image se refait (la page rendue et recadree)
        std::error_code ec;
        fs::remove_all(fs::path(std::u8string(folder.begin(), folder.end())), ec);
        const ai::LocalToolReply again = tool->runRich("{\"vue\": \"Vue_A\"}", [](const std::string&) {}, stop);
        check(again.images.size() == 1 && fs::exists(fs::path(std::u8string(again.images.front().path.begin(), again.images.front().path.end()))),
              "le cache vide : l'image de la vue est refaite depuis le PV");
    }
    b.step();
    const ai::Conversation* c = b.as->current();
    check(c && !c->messages.empty() && c->messages.back().proposal.has_value(), "la proposition est arrivee");
    if (!c || !c->messages.back().proposal) return;
    const ai::Message& m = c->messages.back();
    check(has(m.note, "\xC3\x80 relier") && has(m.note, "Valeur_Debit") && !has(m.note, "VE101"),
          "le logiciel dit ce qui reste a relier : " + m.note);
    const ai::Check ck = b.as->check(static_cast<int>(c->messages.size()) - 1);
    std::string errs;
    for (const auto& e : ck.errors) errs += e + " | ";
    check(ck.errors.empty(), "la proposition est valide (" + errs + ")");
    check(b.as->apply(static_cast<int>(c->messages.size()) - 1, &why), "appliquer (" + why + ")");
    b.step();
    const hmi::View* va = b.doc->project.viewByName("Vue_A");
    check(va != nullptr, "Vue_A est creee");
    if (!va) return;
    const hmi::Object* fan = nullptr;
    const hmi::Object* disp = nullptr;
    const hmi::Object* layer = nullptr;
    for (const auto& o : va->objects) {
        if (o.name == "VE101") fan = &o;
        if (o.name == "Valeur_Debit") disp = &o;
        if (o.name == "Calque_Vue_A") layer = &o;
    }
    check(fan && fan->text("variable") == "VE101_Marche", "le ventilateur est relie a la variable EXISTANTE trouvee par son repere");
    check(disp && disp->text("variable").empty(), "l'afficheur sans donnee reste non relie");
    check(layer && layer->locked && !layer->flag("visible", true) && std::abs(layer->number("opacity", 100) - 30) < 0.01,
          "le calque de reference : verrouille, masque en marche, a demi transparent");
    check(b.doc->project.resourceByName("Calque_Vue_A.png") != nullptr, "l'image de la vue est une ressource du projet");
}

void captures() {
    std::printf("[6] des captures (PNG)\n");
    Bench b("captures");
    std::string why;
    std::vector<std::uint8_t> png;
    const hmi::Rgba s0 = screenImage(0);
    check(app::encodePng(s0.pixels, s0.width, s0.height, png), "une capture en PNG");
    const fs::path src = b.dir / "Ecran_Ventilation.png";
    writeFile(src, std::string(png.begin(), png.end()));
    b.as->newConversation();
    check(b.as->attach(U(src), &why), "joindre la capture (" + why + ")");
    b.settle();
    check(b.as->viewTemplates().empty(), "une image n'est pas un modele de vues d'elle-meme");
    const std::string id = b.as->pending().empty() ? std::string{} : b.as->pending().front().attachment.id;
    check(b.as->setViewTemplateChoice(id, true, &why), "le geste << Modele de vues >> (" + why + ")");
    b.settle();
    const auto models = b.as->viewTemplates();
    check(models.size() == 1 && models.front().tpl.kind == "captures" && models.front().tpl.views.size() == 1
              && models.front().tpl.views.front().name == "Ecran_Ventilation",
          "la capture est un modele de vues (une vue)");
    if (!models.empty()) {
        const vt::Template& t = models.front().tpl;
        check(t.palette.background == 0xE0E0E0 && t.views.front().screenW == 400 && t.views.front().screenH == 300, "sa charte, sa taille");
    }
    check(b.as->setViewTemplateChoice(id, false, &why) && b.as->viewTemplates().empty(), "le geste inverse : ce n'est plus un modele");
    // une image designee par la demande
    Bench d("demande");
    const fs::path src2 = d.dir / "Ecran_Pompes.png";
    writeFile(src2, std::string(png.begin(), png.end()));
    d.as->newConversation();
    check(d.as->attach(U(src2), &why), "joindre une autre capture");
    d.settle();
    d.s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "D'accord."), ev(ai::ProviderEvent::Type::Done, "end_turn")});
    d.s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "D'accord."), ev(ai::ProviderEvent::Type::Done, "end_turn")});
    check(d.as->send("Reproduis cette capture avec les donn\xC3\xA9" "es du projet.", &why), "une demande qui la designe (" + why + ")");
    d.step();
    d.settle();
    const auto named = d.as->viewTemplates();
    check(named.size() == 1 && named.front().name == "Ecran_Pompes.png", "la capture designee par la demande est lue comme un modele");
    check(d.as->send("Et la meme en plus petit.", &why), "la demande suivante (" + why + ")");
    d.step();
    bool sent = false, image = false;
    if (d.s.requests.size() == 2)
        for (const auto& t : d.s.requests[1].turns)
            for (const auto& a : t.attachments) {
                if (a.prepared && !a.blocks.empty() && has(a.blocks.front(), "Charte")) sent = true;
                if (a.name == "Ecran_Pompes.png") image = true;
            }
    check(sent && image, "elle part avec sa charte, et l'image elle-meme");
}

// La liste << a relier >> (pure).
void toLink() {
    std::printf("[7] a relier\n");
    ai::Proposal p;
    const auto op = [](ai::Operation::Action a, const char* kind, const char* name, std::vector<std::pair<std::string, std::string>> props) {
        ai::Operation o;
        o.action = a;
        o.kind = kind;
        o.name = name;
        o.props = std::move(props);
        return o;
    };
    p.ops.push_back(op(ai::Operation::Action::Create, "Fan", "VE1", {{"variable", "VE1_Marche"}}));
    p.ops.push_back(op(ai::Operation::Action::Create, "NumericDisplay", "Debit", {}));
    p.ops.push_back(op(ai::Operation::Action::Create, "Damper", "RG1", {}));
    p.ops.push_back(op(ai::Operation::Action::Create, "Indicator", "Defaut_1", {{"visible", "=VE1_Defaut"}}));
    p.ops.push_back(op(ai::Operation::Action::Create, "Text", "Titre", {{"text", "Titre"}}));
    p.ops.push_back(op(ai::Operation::Action::Create, "Damper", "RG2", {}));
    ai::Operation bind;
    bind.action = ai::Operation::Action::Bind;
    bind.name = "RG2";
    bind.variable = "RG2_Ouverture";
    p.ops.push_back(bind);
    const auto names = vt::toLink(p);
    check(names == std::vector<std::string>{"Debit", "RG1"}, "a relier : Debit, RG1 (une formule, un associer, un texte : non)");
    check(has(vt::toLinkNote(names), "\xC3\x80 relier (2 objets") && vt::toLinkNote({}).empty(), "la phrase");
}

} // namespace

int main(int argc, char** argv) {
    std::printf("[1.22.8 : les modeles de vues]\n");
    if (argc > 1) {
#ifdef _WIN32
        hmi::pdf::setSearchPaths({(fs::path(argv[1]) / "win-x64").string()});
#else
        hmi::pdf::setSearchPaths({(fs::path(argv[1]) / "linux-x64").string()});
#endif
    }
    fs::create_directories(scratch());
    ai::setRoot(U(scratch() / "IA"));
    images();
    text();
    const std::string pv = makePv();
    pdf(pv);
    // ce que l'IA recoit : le PV analyse (ou, sans PDFium, une capture)
    vt::Template t;
    const std::string id = "p-essai-1";
    bool ready = false;
    if (pdfReady()) {
        ready = analyzePv(pv, vt::cacheFolder(id), t);
    } else {
        skip("ce que l'IA recoit, sur le PV : PDFium absent");
    }
    if (ready) forTheAi(t, id);
    endToEnd(pv);
    captures();
    toLink();
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    std::printf("ai_modele_vues_test : %d verifications, %d echecs, %d sautees\n", g_ok + g_fail, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
