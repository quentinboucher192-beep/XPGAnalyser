// =============================================================================
//  tools/help_preview.cpp — une capture de l'onglet Macros, sans SDL
// -----------------------------------------------------------------------------
//  POURQUOI CET OUTIL EXISTE.
//
//  Une maquette dessinee a la main ment toujours un peu : elle montre ce qu'on
//  voudrait, pas ce que le code produit. Ici, le renderer SVG implemente
//  gfx::IRenderer et on lui fait peindre LE VRAI arbre de widgets, avec le vrai
//  theme et les vraies donnees de libs/. Ce qui sort est donc ce que SDL
//  afficherait, au rendu des glyphes pres.
//
//  C'est aussi un test : un widget qui deborde de son parent, un panneau qui
//  reste a zero pixel, un texte qui sort de sa colonne se voient ici sans
//  ouvrir de fenetre - et les machines qui compilent ce projet n'en ont pas.
//
//  LA MESURE DU TEXTE EST APPROCHEE. On ne charge pas de police : la largeur
//  d'une chaine est estimee a partir de la taille demandee. Les positions sont
//  donc justes a quelques pixels pres sur les textes proportionnels, exactes
//  sur les blocs et les separateurs.
//
//  Compilation : voir tools/README-apercu.txt
// =============================================================================
#include "../src/app/screens/LibraryHelpScreen.hpp"
#include "../src/ui/Theme.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '"': out += "&quot;"; break;
            default:  out += c;
        }
    }
    return out;
}

std::string hex(gfx::Color c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

class SvgRenderer final : public gfx::IRenderer {
public:
    SvgRenderer(float w, float h) : size_{w, h} {}

    void beginFrame() override { body_.clear(); clips_.clear(); clipId_ = 0; }
    void endFrame() override {}

    void pushClip(const gfx::Rect& r) override {
        const auto id = "c" + std::to_string(++clipId_);
        defs_ << "<clipPath id=\"" << id << "\"><rect x=\"" << r.x << "\" y=\"" << r.y
              << "\" width=\"" << r.w << "\" height=\"" << r.h << "\"/></clipPath>";
        body_ += "<g clip-path=\"url(#" + id + ")\">";
        clips_.push_back(id);
    }
    void popClip() override {
        if (clips_.empty()) return;
        clips_.pop_back();
        body_ += "</g>";
    }

    void fillRect(const gfx::Rect& r, gfx::Color c) override {
        if (r.w <= 0.f || r.h <= 0.f || c.a == 0) return;
        body_ += rect(r, c, {}, 0.f, 0.f);
    }
    void strokeRect(const gfx::Rect& r, gfx::Color c, float t) override {
        if (r.w <= 0.f || r.h <= 0.f || c.a == 0) return;
        body_ += rect({r.x + t / 2, r.y + t / 2, r.w - t, r.h - t}, {}, c, t, 0.f);
    }
    void fillRoundedRect(const gfx::Rect& r, gfx::Color c, float radius) override {
        if (r.w <= 0.f || r.h <= 0.f || c.a == 0) return;
        body_ += rect(r, c, {}, 0.f, radius);
    }
    void line(gfx::Point a, gfx::Point b, gfx::Color c, float t) override {
        std::ostringstream o;
        o << "<line x1=\"" << a.x << "\" y1=\"" << a.y << "\" x2=\"" << b.x
          << "\" y2=\"" << b.y << "\" stroke=\"" << hex(c) << "\" stroke-opacity=\""
          << c.a / 255.f << "\" stroke-width=\"" << t << "\"/>";
        body_ += o.str();
    }
    void drawText(gfx::Point p, std::string_view utf8, gfx::FontId f, gfx::Color c) override {
        if (utf8.empty() || c.a == 0) return;
        const float px = fontPx(f);
        std::ostringstream o;
        o << "<text x=\"" << p.x << "\" y=\"" << p.y + px * 0.80f
          << "\" font-family=\"" << (isMono(f) ? "ui-monospace,Menlo,Consolas,monospace"
                                               : "Inter,Segoe UI,system-ui,sans-serif")
          << "\" font-size=\"" << px << "\" font-weight=\"" << (isBold(f) ? 600 : 400)
          << "\" fill=\"" << hex(c) << "\" fill-opacity=\"" << c.a / 255.f
          << "\" xml:space=\"preserve\">" << escape(utf8) << "</text>";
        body_ += o.str();
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}

    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float px = fontPx(f);
        const float adv = isMono(f) ? px * 0.60f : px * 0.52f;
        return {static_cast<float>(s.size()) * adv, px, px * 0.80f, px * 0.20f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return fontPx(f) * 1.35f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size_; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f,
                                            float maxWidth) const override {
        const float adv = measure("x", f).width;
        if (adv <= 0.f) return s.size();
        const auto n = static_cast<std::size_t>(maxWidth / adv);
        return n < s.size() ? n : s.size();
    }

    // Les polices sont identifiees par leur taille en pixels dans ce projet.
    // L'apercu a besoin de distinguer gras et fixe : on reserve deux bits
    // hauts, que le backend SDL n'utilise pas.
    static gfx::FontId tag(gfx::FontId base, bool bold, bool mono) {
        return {static_cast<std::uint16_t>(base.v | (bold ? 0x4000 : 0) | (mono ? 0x8000 : 0))};
    }

    [[nodiscard]] std::string document(gfx::Color background) const {
        std::ostringstream o;
        o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << size_.w
          << "\" height=\"" << size_.h << "\" viewBox=\"0 0 " << size_.w << " "
          << size_.h << "\"><defs>" << defs_.str() << "</defs>"
          << "<rect width=\"100%\" height=\"100%\" fill=\"" << hex(background) << "\"/>"
          << body_;
        for (std::size_t i = 0; i < clips_.size(); ++i) o << "</g>";
        o << "</svg>";
        return o.str();
    }

private:
    static bool  isBold(gfx::FontId f) { return (f.v & 0x4000) != 0; }
    static bool  isMono(gfx::FontId f) { return (f.v & 0x8000) != 0; }
    static float fontPx(gfx::FontId f) {
        const float v = static_cast<float>(f.v & 0x3FFF);
        return v > 0.f ? v : 13.f;
    }
    static std::string rect(const gfx::Rect& r, gfx::Color fill, gfx::Color stroke,
                            float sw, float radius) {
        std::ostringstream o;
        o << "<rect x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << r.w
          << "\" height=\"" << r.h << "\"";
        if (radius > 0.f) o << " rx=\"" << radius << "\"";
        if (sw > 0.f) o << " fill=\"none\" stroke=\"" << hex(stroke) << "\" stroke-opacity=\""
                        << stroke.a / 255.f << "\" stroke-width=\"" << sw << "\"";
        else o << " fill=\"" << hex(fill) << "\" fill-opacity=\"" << fill.a / 255.f << "\"";
        o << "/>";
        return o.str();
    }

    gfx::Size                size_;
    std::string              body_;
    std::ostringstream       defs_;
    std::vector<std::string> clips_;
    int                      clipId_{0};
};

// Le theme, avec des tailles de police lisibles et les marqueurs gras / fixe.
// Le theme du depot vise la police 8x8 de SDL ; l'apercu dessine du vrai texte.
ui::Theme prepare(ui::Theme t) {
    t.font.ui      = SvgRenderer::tag({13}, false, false);
    t.font.uiBold  = SvgRenderer::tag({13}, true,  false);
    t.font.mono    = SvgRenderer::tag({12}, false, true);
    t.font.smallUi = SvgRenderer::tag({11}, false, false);
    return t;
}

void shoot(const std::string& libs, const ui::Theme& theme, const std::string& out,
           float w, float h, std::size_t tab = 0, std::size_t entry = 0,
           std::size_t field = 0) {
    SvgRenderer r(w, h);
    ui::installPlatformServices(ui::PlatformServices{
        [&r](std::string_view t, gfx::FontId f) { return r.measure(t, f).width; },
        [&r](gfx::FontId f) { return r.lineHeight(f); },
        [] { return std::string{}; },
        [](std::string_view) {},
    });

    app::LibraryHelpPage page(libs, "help.macros");
    page.selectEntryForTest(entry);
    page.selectFieldForTest(field);
    page.selectTabForTest(tab);
    page.setBounds({0.f, 0.f, w, h});
    page.layout();

    r.beginFrame();
    std::vector<ui::Widget*> overlays;
    const ui::PaintContext ctx{r, theme, {0.f, 0.f, w, h}, 0.0, &overlays};
    page.render(ctx);
    for (auto* o : overlays) o->paintTopMost(ctx);
    r.endFrame();

    std::ofstream f(out);
    f << r.document(theme.color.windowBg);
    std::printf("%s  (%zu items)\n", out.c_str(), page.entryCount());
}

} // namespace

int main(int argc, char** argv) {
    const std::string libs = argc > 1 ? argv[1] : "libs";
    const std::string dir  = argc > 2 ? argv[2] : ".";
    const auto clair = prepare(ui::Theme::light());
    const auto sombre = prepare(ui::Theme::dark());
    // 16 = ST_EQ_Motor dans la bibliotheque de reference ; 12 = son parametre
    // Thermal, celui dont l'aide longue explique le contact NF.
    shoot(libs, clair,  dir + "/1-aide-clair.svg",     1280.f, 800.f, 0, 16);
    shoot(libs, sombre, dir + "/2-aide-sombre.svg",    1280.f, 800.f, 0, 16);
    shoot(libs, clair,  dir + "/3-modifier.svg",       1280.f, 800.f, 1, 16, 12);
    shoot(libs, sombre, dir + "/4-modifier-sombre.svg",1280.f, 800.f, 1, 16, 12);
    shoot(libs, clair,  dir + "/5-source.svg",         1280.f, 800.f, 2, 16);
    shoot(libs, clair,  dir + "/6-etroit.svg",          860.f, 640.f, 0, 16);
    return 0;
}
