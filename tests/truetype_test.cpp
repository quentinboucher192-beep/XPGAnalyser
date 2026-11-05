// =============================================================================
//  tests/truetype_test.cpp - 1.26.0 : lire, modifier et ecrire une TrueType (hmi::ttf)
// -----------------------------------------------------------------------------
//  Les seize polices fournies (resources/polices) : chacune se lit, s'ecrit, se
//  relit a l'identique ; le fichier ecrit se dessine (stb_truetype, ce que l'IHM et
//  le PDF emploient) avec les memes chasses. Une nouvelle police, une copie (les
//  noms reserves refuses), les composes qui suivent leur glyphe de base, ajouter un
//  point sans changer la forme, un rectangle, une ellipse, le dessin rempli, les
//  fichiers refuses.
// =============================================================================
#include "../src/hmi/HmiMedia.hpp"
#include "../src/hmi/HmiTrueType.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

namespace fs = std::filesystem;
namespace tt = hmi::ttf;

hmi::Bytes readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return hmi::Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

long coverage(const hmi::Rgba& img) {
    long sum = 0;
    for (std::size_t i = 3; i < img.pixels.size(); i += 4) sum += img.pixels[i];
    return sum;
}

long area(const std::vector<std::uint8_t>& r) {
    long s = 0;
    for (auto v : r) s += v;
    return s;
}

void roundTrip(const fs::path& file) {
    const std::string name = file.filename().string();
    const hmi::Bytes original = readFile(file);
    tt::Font f;
    std::string why;
    check(tt::read(original, f, &why), name + " : se lit (" + why + ")");
    if (f.glyphs.empty()) return;
    check(!f.family.empty() && f.unitsPerEm >= 1000, name + " : famille et unites lues");
    check(tt::glyphOf(f, 'A') > 0 && tt::glyphOf(f, 0xE9) > 0, name + " : A et e accent aigu presents");
    const hmi::Bytes written = tt::write(f);
    if (const char* out = std::getenv("TRUETYPE_OUT")) {   // pour un controle externe des fichiers ecrits
        std::ofstream o(fs::path(out) / name, std::ios::binary);
        o.write(reinterpret_cast<const char*>(written.data()), static_cast<std::streamsize>(written.size()));
    }
    tt::Font again;
    check(tt::read(written, again, &why), name + " : le fichier ecrit se relit (" + why + ")");
    check(again.glyphs.size() == f.glyphs.size(), name + " : autant de glyphes");
    std::size_t same = 0, sameAdv = 0;
    for (std::size_t i = 0; i < std::min(f.glyphs.size(), again.glyphs.size()); ++i) {
        const auto& a = f.glyphs[i];
        const auto& b = again.glyphs[i];
        // Un contour d'un seul point (rare) part ; le reste est identique.
        same += a.codes == b.codes && a.components == b.components && (a.contours == b.contours || a.composite());
        sameAdv += tt::advanceOf(f, static_cast<int>(i)) == tt::advanceOf(again, static_cast<int>(i));
    }
    check(same + 2 >= f.glyphs.size(), name + " : glyphes identiques apres relecture (" + std::to_string(same) + "/" + std::to_string(f.glyphs.size()) + ")");
    check(sameAdv == f.glyphs.size(), name + " : chasses identiques");
    check(again.family == f.family && again.bold == f.bold && again.italic == f.italic, name + " : famille et style relus");
    check(again.ascent == f.ascent && again.descent == f.descent, name + " : hauteurs relues");
    check(tt::write(again) == written, name + " : ecrire deux fois donne les memes octets");
    // stb_truetype (l'IHM, le PDF) : memes chasses, le texte se dessine.
    hmi::FontMetrics m0, m1;
    const std::vector<std::uint32_t> codes = {'A', 'a', 'W', '0', 0xE9, 0x20AC, ' '};
    check(hmi::fontMetrics(original, codes, m0) && hmi::fontMetrics(written, codes, m1), name + " : mesures lues par stb");
    check(m0.widths == m1.widths, name + " : stb lit les memes chasses");
    check(m1.family == f.family || m1.family.empty(), name + " : stb lit la famille");
    hmi::Rgba a, b;
    check(hmi::renderFontText(original, "Pompe \xC3\xA0 eau 12,4", 32.f, a) && hmi::renderFontText(written, "Pompe \xC3\xA0 eau 12,4", 32.f, b),
          name + " : le texte se dessine");
    const long ca = coverage(a), cb = coverage(b);
    check(a.width == b.width && a.height == b.height && std::labs(ca - cb) <= ca / 50, name + " : le meme dessin (a 2 %)");
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path("resources/polices");
    int fonts = 0;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".ttf") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        roundTrip(f);
        ++fonts;
    }
    check(fonts == 16, "les 16 polices fournies essayees (" + std::to_string(fonts) + ")");

    // ---- les composes suivent leur glyphe de base ; decomposer garde la forme ----
    {
        tt::Font f;
        check(tt::read(readFile(dir / "Carlito-Regular.ttf"), f), "Carlito se lit");
        const int e = tt::glyphOf(f, 'e'), ea = tt::glyphOf(f, 0xE9);
        check(e > 0 && ea > 0 && f.glyphs[static_cast<std::size_t>(ea)].composite(), "e accent aigu : un compose");
        const auto users = tt::usersOf(f, e);
        check(std::find(users.begin(), users.end(), ea) != users.end(), "e accent aigu emploie e");
        const tt::Box b0 = tt::bounds(f, ea), e0 = tt::bounds(f, e);
        for (auto& c : f.glyphs[static_cast<std::size_t>(e)].contours)
            for (auto& p : c.points) p.y -= 40;
        const tt::Box b1 = tt::bounds(f, ea);
        check(b1.yMin == b0.yMin - 40 && tt::bounds(f, e).yMax == e0.yMax - 40, "modifier e change e accent aigu (le compose suit)");
        const auto shape = tt::outline(f, ea);
        tt::decompose(f, ea);
        check(!f.glyphs[static_cast<std::size_t>(ea)].composite() && tt::outline(f, ea) == shape, "decomposer garde la forme");
        // Le fichier ecrit garde la modification.
        tt::Font g;
        check(tt::read(tt::write(f), g) && tt::bounds(g, tt::glyphOf(g, 0xE9)).yMin == b1.yMin, "la modification s'ecrit");
    }

    // ---- ajouter un point sur le trait : la forme ne change pas ----
    {
        tt::Font f;
        check(tt::read(readFile(dir / "LiberationSans-Regular.ttf"), f), "Liberation Sans se lit");
        const int o = tt::glyphOf(f, 'o');
        auto& g = f.glyphs[static_cast<std::size_t>(o)];
        check(!g.composite() && !g.contours.empty(), "o : un glyphe simple");
        const double s = 0.1;
        const auto r0 = tt::raster(g.contours, s, 10, 160, 140, 200);
        const std::size_t n0 = g.contours[0].points.size();
        const auto segs = tt::segments(g.contours[0]);
        check(segs.size() >= 4, "o : des segments");
        int curve = -1, straight = -1;
        for (std::size_t i = 0; i < segs.size(); ++i) (segs[i].curve ? curve : straight) = static_cast<int>(i);
        check(curve >= 0, "o : une courbe");
        const int at = tt::insertPoint(g.contours[0], curve, 0.5);
        check(at >= 0 && g.contours[0].points.size() >= n0 + 2 && g.contours[0].points[static_cast<std::size_t>(at)].on,
              "un point sur la courbe ajoute (avec ses deux controles)");
        const auto r1 = tt::raster(g.contours, s, 10, 160, 140, 200);
        long diff = 0;
        for (std::size_t i = 0; i < r0.size(); ++i) diff += std::abs(r0[i] - r1[i]);
        check(area(r0) > 0 && diff < area(r0) / 100, "la forme ne change pas (" + std::to_string(diff) + ")");
        // Le point se retrouve et se retire.
        const auto& p = g.contours[0].points[static_cast<std::size_t>(at)];
        const tt::Hit h = tt::hitPoint(g.contours, p.x + 3, p.y - 2, 20);
        check(h.contour == 0 && h.point == at, "le point se retrouve sous la souris");
        const tt::Hit hs = tt::hitSegment(g.contours, p.x, p.y, 10);
        check(hs.contour == 0 && hs.segment >= 0, "le trait se retrouve sous la souris");
    }

    // ---- une nouvelle police ----
    {
        tt::Font f = tt::blank("Ma police");
        check(f.glyphs.size() > 190 && tt::glyphOf(f, 'a') > 0 && tt::glyphOf(f, 0x20AC) > 0 && tt::glyphOf(f, 0xE9) > 0,
              "nouvelle police : les lettres, les accentuees, l'euro");
        const int a = tt::glyphOf(f, 'a');
        tt::addRectangle(f.glyphs[static_cast<std::size_t>(a)], 50, 0, 450, 500);
        tt::addEllipse(f.glyphs[static_cast<std::size_t>(a)], 250, 250, 120, 120);
        const int o = tt::glyphOf(f, 'o');
        tt::addEllipse(f.glyphs[static_cast<std::size_t>(o)], 250, 250, 200, 200);
        const auto r = tt::raster(f.glyphs[static_cast<std::size_t>(o)].contours, 1.0, 0, 500, 500, 500);
        const double disc = 3.14159265358979 * 200 * 200 * 255;
        check(std::fabs(area(r) - disc) < disc * 0.02, "une ellipse : l'aire du disque (a 2 %)");
        // Le rectangle et l'ellipse tournent dans le meme sens : l'ellipse ne perce pas (regle non nulle).
        const auto ra = tt::raster(f.glyphs[static_cast<std::size_t>(a)].contours, 1.0, 0, 600, 500, 700);
        check(ra[static_cast<std::size_t>(600 - 250) * 500 + 250] == 255, "regle non nulle : le centre reste plein");
        tt::reverse(f.glyphs[static_cast<std::size_t>(a)].contours[1]);
        const auto rb = tt::raster(f.glyphs[static_cast<std::size_t>(a)].contours, 1.0, 0, 600, 500, 700);
        check(rb[static_cast<std::size_t>(600 - 250) * 500 + 250] == 0, "un contour retourne perce (un trou)");
        const hmi::Bytes bytes = tt::write(f);
        if (const char* out = std::getenv("TRUETYPE_OUT")) {
            std::ofstream o(fs::path(out) / "nouvelle.ttf", std::ios::binary);
            o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        tt::Font back;
        std::string why;
        check(tt::read(bytes, back, &why) && back.family == "Ma police" && back.glyphs.size() == f.glyphs.size(),
              "nouvelle police : ecrite et relue (" + why + ")");
        hmi::Rgba img;
        check(hmi::renderFontText(bytes, "ao", 40.f, img) && coverage(img) > 0, "nouvelle police : stb la dessine");
        hmi::FontMetrics m;
        check(hmi::fontMetrics(bytes, {'a', ' '}, m) && m.family == "Ma police" && std::lround(m.widths[0]) == 500,
              "nouvelle police : famille et chasse lues par stb");
        check(tt::postScriptName(back) == "Mapolice-Regular", "nom PostScript");
        // Copier le dessin d'un glyphe d'une autre police (mis a l'echelle).
        tt::Font carlito;
        check(tt::read(readFile(dir / "Carlito-Regular.ttf"), carlito), "Carlito se relit");
        const int b = tt::glyphOf(f, 'b');
        tt::copyGlyph(f, b, carlito, tt::glyphOf(carlito, 'b'));
        const tt::Box bb = tt::bounds(f, b), cb = tt::bounds(carlito, tt::glyphOf(carlito, 'b'));
        check(!bb.empty && std::abs(bb.yMax - cb.yMax * 1000 / 2048) <= 1, "copier un glyphe d'une autre police : a l'echelle");
    }

    // ---- dupliquer : les noms reserves ----
    {
        tt::Font carlito, dejavu, liberation, jet;
        check(tt::read(readFile(dir / "Carlito-Regular.ttf"), carlito) && tt::read(readFile(dir / "DejaVuSans.ttf"), dejavu)
                  && tt::read(readFile(dir / "LiberationSans-Regular.ttf"), liberation) && tt::read(readFile(dir / "JetBrainsMono-Regular.ttf"), jet),
              "quatre sources se lisent");
        std::string why;
        check(!tt::nameAllowed(carlito, "Carlito", &why), "le nom de la source : refuse");
        check(!tt::nameAllowed(carlito, "Carlito Station", &why) && why.find("Carlito") != std::string::npos, "Carlito : nom reserve");
        check(!tt::nameAllowed(dejavu, "Vera Neue", &why) && !tt::nameAllowed(dejavu, "Ma Bitstream", &why), "DejaVu : Bitstream et Vera reserves");
        check(!tt::nameAllowed(liberation, "Liberation Plus", &why), "Liberation : nom reserve");
        check(!tt::nameAllowed(jet, "  ", &why), "un nom vide : refuse");
        check(tt::nameAllowed(carlito, "Ma police Station", &why) && tt::nameAllowed(dejavu, "Pupitre", &why), "un autre nom : accepte");
        const tt::Font copy = tt::duplicate(carlito, "Ma police Station");
        check(copy.family == "Ma police Station" && copy.license == carlito.license && copy.glyphs.size() == carlito.glyphs.size(),
              "la copie : son nom, la licence suit, tous les glyphes");
        check(!tt::nameAllowed(copy, "Carlito 2", &why), "la copie d'une copie garde les noms reserves");
        tt::Font back;
        check(tt::read(tt::write(copy), back) && back.family == "Ma police Station" && back.copyright.find("Carlito") != std::string::npos,
              "la copie ecrite : son nom, les droits d'origine cites");
        check(tt::styleLabel(back) == "Normal", "style : Normal");
    }

    // ---- les fichiers refuses ----
    {
        tt::Font f;
        std::string why;
        hmi::Bytes otf = {'O', 'T', 'T', 'O', 0, 0, 0, 0, 0, 0, 0, 0};
        check(!tt::read(otf, f, &why) && why.find("CFF") != std::string::npos, "une OTF CFF : refusee, et dit pourquoi");
        hmi::Bytes ttc = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 0};
        check(!tt::read(ttc, f, &why) && why.find("TTC") != std::string::npos, "une collection TTC : refusee");
        hmi::Bytes cut = readFile(dir / "Caladea-Regular.ttf");
        cut.resize(cut.size() / 3);
        check(!tt::read(cut, f, &why), "une police tronquee : refusee");
        check(!tt::read(hmi::Bytes{1, 2, 3}, f, &why), "trois octets : refuses");
    }

    std::printf("truetype_test : %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
