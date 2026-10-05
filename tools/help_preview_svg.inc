// =============================================================================
//  tools/help_preview_svg.inc — un gfx::IRenderer qui ecrit du SVG
// -----------------------------------------------------------------------------
//  Partage par help_preview.cpp et helpscreen_patch_check.cpp. Inclus plutot
//  que compile a part : c'est un outil de developpement, il n'a pas a exister
//  dans le binaire livre, et deux copies du meme renderer divergeraient.
//
//  A inclure DANS un namespace anonyme, apres les en-tetes de ui/ et gfx/.
// =============================================================================
//  Les en-tetes standard sont volontairement ABSENTS d'ici : ce fichier
//  s'inclut a l'interieur d'un namespace anonyme, et un <fstream> ouvert la
//  deplace toute la bibliotheque standard dans ce namespace. L'appelant les
//  inclut au niveau du fichier, comme il le ferait de toute facon.
#pragma once

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
        // Estimation VOLONTAIREMENT large : un apercu qui sous-estime la
        // largeur du texte montre des lignes qui tiennent alors qu'elles
        // ne tiendront pas. Mieux vaut qu'il coupe un peu tot.
        const float adv = isMono(f) ? px * 0.62f : px * 0.56f;
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

