// =============================================================================
//  platform/SdlRenderer.cpp — the SDL3 backend
// -----------------------------------------------------------------------------
//  Text is drawn with SDL_RenderDebugText, the 8x8 bitmap font built into SDL3.
//  That is a deliberate first-milestone choice: it removes the SDL_ttf
//  dependency entirely, so the project builds with nothing but SDL3 on the
//  include path. FontId carries a pixel size and the renderer scales the debug
//  font to match, which keeps the metrics honest — measure(), lineHeight() and
//  fitCharacters() all agree with what is actually drawn. Swapping in a real
//  glyph atlas later touches this file and nothing else.
// =============================================================================
#include "Renderer.hpp"

#include <SDL3/SDL.h>

#include "FontAtlas.hpp"
#include "RoundedCorner.hpp"

#include <map>
#include <vector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace gfx {

    namespace {
        // Lot API 7 : LE NUMERO D'UNE IMAGE EST UNIQUE DANS TOUT LE PROGRAMME, et
        // plus seulement dans son renderer. Un onglet detache se dessine avec le
        // renderer de SA fenetre : chaque renderer comptant depuis 1, un widget qui
        // garde le numero d'une texture de l'autre fenetre (un cache par renderer,
        // l'adresse d'un renderer detruit reprise par un nouveau) dessinait - ou
        // rendait - l'image d'un autre. Maintenant il ne trouve rien, simplement.
        // Le fil de l'interface seul cree des images (un renderer SDL ne se
        // partage pas entre fils).
        std::uint32_t g_nextImage = 1;
    } // namespace

    struct SdlRenderer::Impl {
        SDL_Window* window{ nullptr };
        SDL_Renderer* renderer{ nullptr };
        std::vector<Rect>  clipStack;

        // Une fonte et une texture par taille en pixels. La map retient aussi
        // les ECHECS - un atlas non pret - pour ne pas rouvrir huit fichiers a
        // chaque appel sur une machine qui n'a aucune police.
        std::map<int, FontAtlas>    faces;
        std::map<int, SDL_Texture*> textures;
        // Les images (createImage) : un numero, jamais reutilise - ni par ce
        // renderer, ni par un autre (voir g_nextImage).
        std::map<std::uint32_t, SDL_Texture*> images;
        // Lot 13 : le dessin hors de l'ecran - la texture cible, ce qu'on
        // retrouve apres (la cible d'avant, la pile de decoupe).
        SDL_Texture*                          offscreen{nullptr};
        SDL_Texture*                          savedTarget{nullptr};
        std::vector<Rect>                     savedClips;

        // faceFor n'est PAS const, et il est pourtant appele depuis measure() et
        // fitCharacters() qui le sont. C'est legal et voulu : operator-> sur un
        // unique_ptr const rend un pointeur NON const vers l'objet pointe. La
        // constance de SdlRenderer porte sur le pointeur, pas sur ce qu'il
        // designe - et rasteriser un glyphe a la demande est bien une
        // modification de l'atlas.
        [[nodiscard]] FontAtlas*  faceFor(FontId);
        void                      uploadIfDirty(FontId, FontAtlas&);
        [[nodiscard]] SDL_Texture* atlasTexture(FontId);

        // FontId::v is the pixel height we want; the debug font is 8 px tall.
        //
        // The scale is rounded to a whole number. A fractional scale makes SDL
        // filter the glyph bitmap, and 8x8 pixel glyphs scaled by 1.625 come out
        // smeared and appear to overlap. Snapping to an integer keeps every glyph
        // crisp, and because measure()/lineHeight()/fitCharacters() all use this
        // same function, the layout agrees with what is actually drawn.
        [[nodiscard]] static float scaleFor(FontId f) {
            const float px = f.v ? static_cast<float>(f.v) : 16.f;
            const float raw = px / static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
            return std::max(1.f, std::round(raw));
        }

        void applyClip(float scale) const {
            if (clipStack.empty()) {
                SDL_SetRenderClipRect(renderer, nullptr);
                return;
            }
            const Rect& c = clipStack.back();
            SDL_Rect r{ static_cast<int>(std::floor(c.x / scale)),
                       static_cast<int>(std::floor(c.y / scale)),
                       static_cast<int>(std::ceil(c.w / scale)),
                       static_cast<int>(std::ceil(c.h / scale)) };
            SDL_SetRenderClipRect(renderer, &r);
        }

        void setColor(Color c) const {
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        }
    };

    core::Result<std::unique_ptr<SdlRenderer>> SdlRenderer::create(SDL_Window* window) {
        if (!window) return core::fail(core::ErrorCode::InvalidArgument, "null window");

        auto impl = std::make_unique<Impl>();
        impl->window = window;
        impl->renderer = SDL_CreateRenderer(window, nullptr);
        if (!impl->renderer)
            return core::fail(core::ErrorCode::SdlRenderer, SDL_GetError());

        SDL_SetRenderVSync(impl->renderer, 1);
        SDL_SetRenderDrawBlendMode(impl->renderer, SDL_BLENDMODE_BLEND);

        return std::unique_ptr<SdlRenderer>(new SdlRenderer(std::move(impl)));
    }

    SdlRenderer::SdlRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

    SdlRenderer::~SdlRenderer() {
        if (impl_ && impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
    }

    // ------------------------------------------------------------------ frame ---
    void SdlRenderer::beginFrame(Color clear) {
        impl_->clipStack.clear();
        SDL_SetRenderClipRect(impl_->renderer, nullptr);
        SDL_SetRenderDrawColor(impl_->renderer, clear.r, clear.g, clear.b, 255);
        SDL_RenderClear(impl_->renderer);
    }

    void SdlRenderer::endFrame() { SDL_RenderPresent(impl_->renderer); }

    void SdlRenderer::pushClip(const Rect& r) {
        // Nested clips intersect: a child can never paint outside its parent.
        const Rect effective = impl_->clipStack.empty() ? r : impl_->clipStack.back().intersect(r);
        impl_->clipStack.push_back(effective);
        impl_->applyClip(1.f);
    }

    void SdlRenderer::popClip() {
        if (!impl_->clipStack.empty()) impl_->clipStack.pop_back();
        impl_->applyClip(1.f);
    }

    // ------------------------------------------------------------ les fontes ---
    //
    // UNE FONTE PAR TAILLE. FontId porte une hauteur en pixels ; deux tailles
    // sont deux atlas, parce qu'une police rasterisee a 16 puis etiree a 22 est
    // une police floue - c'est exactement le defaut qu'on vient de corriger.
    //
    // LA RECHERCHE N'A LIEU QU'UNE FOIS PAR TAILLE, et son echec est retenu :
    // sans ca, une machine sans fonte reessaierait d'ouvrir huit fichiers a
    // chaque appel de drawText, soit plusieurs milliers de fois par seconde.
    FontAtlas* SdlRenderer::Impl::faceFor(FontId font) {
        const int px = std::max(1, static_cast<int>(font.v));
        if (auto at = faces.find(px); at != faces.end())
            return at->second.ready() ? &at->second : nullptr;

        FontAtlas atlas;
        for (const auto& chemin : defaultUiFontCandidates())
            if (atlas.load(chemin, static_cast<float>(px))) break;

        auto& stocke = faces.emplace(px, std::move(atlas)).first->second;
        return stocke.ready() ? &stocke : nullptr;
    }

    void SdlRenderer::Impl::uploadIfDirty(FontId font, FontAtlas& face) {
        if (!face.dirty()) return;
        const int px = std::max(1, static_cast<int>(font.v));

        SDL_Texture*& tex = textures[px];
        if (!tex) {
            tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                    SDL_TEXTUREACCESS_STATIC,
                                    face.width(), face.height());
            if (!tex) return;
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        }

        // L'atlas est une couverture d'un octet par pixel ; la texture veut du
        // RGBA. On met du blanc partout et la couverture dans l'alpha : la
        // couleur vient ensuite du ColorMod, donc un seul atlas sert toutes les
        // couleurs du theme.
        const auto& src = face.pixels();
        std::vector<std::uint32_t> rgba(src.size());
        for (std::size_t i = 0; i < src.size(); ++i)
            rgba[i] = 0x00FFFFFFu | (static_cast<std::uint32_t>(src[i]) << 24);

        SDL_UpdateTexture(tex, nullptr, rgba.data(),
                          static_cast<int>(face.width()) * 4);
        face.markUploaded();
    }

    SDL_Texture* SdlRenderer::Impl::atlasTexture(FontId font) {
        const auto at = textures.find(std::max(1, static_cast<int>(font.v)));
        return at == textures.end() ? nullptr : at->second;
    }

    // --------------------------------------------------------------- drawing ---
    void SdlRenderer::fillRect(const Rect& r, Color c) {
        if (r.empty()) return;
        impl_->setColor(c);
        const SDL_FRect f{ r.x, r.y, r.w, r.h };
        SDL_RenderFillRect(impl_->renderer, &f);
    }

    void SdlRenderer::strokeRect(const Rect& r, Color c, float thickness) {
        if (r.empty()) return;
        impl_->setColor(c);
        const int t = std::max(1, static_cast<int>(thickness));
        for (int i = 0; i < t; ++i) {
            const float o = static_cast<float>(i);
            const SDL_FRect f{ r.x + o, r.y + o, r.w - 2 * o, r.h - 2 * o };
            if (f.w <= 0.f || f.h <= 0.f) break;
            SDL_RenderRect(impl_->renderer, &f);
        }
    }

    void SdlRenderer::fillRoundedRect(const Rect& r, Color c, float radius) {
        if (r.empty()) return;
        const float rad = std::min({ radius, r.w * 0.5f, r.h * 0.5f });
        if (rad <= 1.f) { fillRect(r, c); return; }

        // LES COINS SONT MAINTENANT LISSES.
        //
        // L'ancienne version posait un rectangle d'une ligne par scanline, a une
        // abscisse ARRONDIE : chaque coin etait donc un escalier de trois ou
        // quatre marches, visible sur chaque bouton et chaque champ de l'ecran.
        //
        // Le principe : le corps du coin commence au pixel entier suivant, et le
        // pixel partiel qui reste est peint avec une opacite egale a sa
        // COUVERTURE - la fraction de sa surface reellement dans la forme. C'est
        // tout ce qu'est un antialiasing, et ca ne coute qu'une multiplication.
        impl_->setColor(c);
        // ---- Lot API 8 : sessions de capture ---- une pastille (rayon = demi-hauteur) n'a pas de
        // corps : le rendu logiciel de SDL peint un rectangle vide sur 1 px (SDL_max(h, 1)), d'ou un
        // trait au milieu de chaque pastille translucide. Rien sous 1 px ; la rangee du milieu d'une
        // hauteur impaire, une seule fois.
        const SDL_FRect body{ r.x, r.y + rad, r.w, r.h - 2 * rad };
        if (body.h >= 1.f) SDL_RenderFillRect(impl_->renderer, &body);

        for (float y = 0.f; y < rad; y += 1.f) {
            const float inset = gfx::cornerInset(rad, y + 0.5f);
            const float plein = std::ceil(inset);
            const float couverture = plein - inset;    // 0 a 1
            const float yBas = r.bottom() - y - 1.f;
            const int rangees = yBas < r.y + y + 0.5f ? 1 : 2;   // lot API 8 : la rangee du milieu, seule

            // La partie franche.
            if (r.w - 2 * plein > 0.f) {
                const SDL_FRect haut{ r.x + plein, r.y + y, r.w - 2 * plein, 1.f };
                const SDL_FRect bas { r.x + plein, yBas, r.w - 2 * plein, 1.f };
                SDL_RenderFillRect(impl_->renderer, &haut);
                if (rangees == 2) SDL_RenderFillRect(impl_->renderer, &bas);
            }

            // Et le pixel de bord, de chaque cote, a son opacite propre.
            if (couverture > 0.01f && plein >= 1.f) {
                Color bord = c;
                bord.a = static_cast<std::uint8_t>(
                    std::lround(static_cast<float>(c.a) * couverture));
                impl_->setColor(bord);
                const float gx = r.x + plein - 1.f;
                const float dx2 = r.right() - plein;
                const float ys[2] = { r.y + y, yBas };
                for (int k = 0; k < rangees; ++k) {
                    const float yy = ys[k];
                    const SDL_FRect g{ gx, yy, 1.f, 1.f };
                    const SDL_FRect d{ dx2, yy, 1.f, 1.f };
                    SDL_RenderFillRect(impl_->renderer, &g);
                    SDL_RenderFillRect(impl_->renderer, &d);
                }
                impl_->setColor(c);
            }
        }
    }

    void SdlRenderer::line(Point a, Point b, Color c, float thickness) {
        impl_->setColor(c);
        const int t = std::max(1, static_cast<int>(thickness));
        const bool steep = std::abs(b.y - a.y) > std::abs(b.x - a.x);
        for (int i = 0; i < t; ++i) {
            const float o = static_cast<float>(i) - static_cast<float>(t - 1) * 0.5f;
            if (steep) SDL_RenderLine(impl_->renderer, a.x + o, a.y, b.x + o, b.y);
            else       SDL_RenderLine(impl_->renderer, a.x, a.y + o, b.x, b.y + o);
        }
    }

    void SdlRenderer::drawText(Point origin, std::string_view utf8, FontId font, Color c) {
        if (utf8.empty()) return;

        // LE CHEMIN NORMAL : une vraie police, glyphe par glyphe.
        if (auto* face = impl_->faceFor(font)) {
            // 1.11.13 : LES GLYPHES NEUFS D'ABORD, L'ENVOI ENSUITE. Un caractere
            // dessine pour la premiere fois etait rasterise pendant la boucle,
            // apres l'envoi de la texture : il manquait une image (la fenetre du
            // build s'ouvrait sur "%" au lieu de "8 %").
            for (std::size_t at = 0; at < utf8.size();) {
                const char32_t code = FontAtlas::decode(utf8, at);
                if (code == 0) break;
                (void)face->glyph(code);
            }
            impl_->uploadIfDirty(font, *face);
            SDL_Texture* atlas = impl_->atlasTexture(font);
            if (atlas) {
                SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
                SDL_SetTextureAlphaMod(atlas, c.a);

                // origin est le coin HAUT GAUCHE de la ligne, comme partout
                // ailleurs dans cette interface. La plume, elle, court sur la
                // ligne de base : d'ou l'ascendante ajoutee une fois.
                float penX = origin.x;
                const float baseline = origin.y + face->ascent();

                std::size_t at = 0;
                while (at < utf8.size()) {
                    const char32_t code = FontAtlas::decode(utf8, at);
                    if (code == 0) break;
                    const auto& g = face->glyph(code);
                    if (g.w > 0 && g.h > 0) {
                        const SDL_FRect src{ float(g.x), float(g.y), float(g.w), float(g.h) };
                        const SDL_FRect dst{ std::round(penX + g.bearingX),
                                             std::round(baseline + g.bearingY),
                                             float(g.w), float(g.h) };
                        SDL_RenderTexture(impl_->renderer, atlas, &src, &dst);
                    }
                    penX += g.advance;
                }
                return;
            }
        }

        // LE REPLI : la police 8x8 de SDL, quand aucune fonte n'a ete trouvee.
        // Il reste parce qu'une machine sans fonte doit afficher du texte laid
        // plutot que pas de texte du tout - et parce qu'il permet de demarrer
        // avant d'avoir livre les fichiers de police.
        const float s = Impl::scaleFor(font);
        SDL_SetRenderScale(impl_->renderer, s, s);
        impl_->applyClip(s);                       // the clip must follow the scale
        impl_->setColor(c);

        const std::string z(utf8);                 // SDL needs a NUL-terminated string
        SDL_RenderDebugText(impl_->renderer, origin.x / s, origin.y / s, z.c_str());

        SDL_SetRenderScale(impl_->renderer, 1.f, 1.f);
        impl_->applyClip(1.f);
    }

    void SdlRenderer::drawTexture(const Rect& dst, TextureId id, Color tint) {
        // Icons arrive with the glyph atlas; until then this is a coloured box so
        // that layouts that reserve icon space still look right.
        if (id.v == 0) return;
        fillRect(dst, tint);
    }

    // --------------------------------------------------------------- metrics ---
    TextMetrics SdlRenderer::measure(std::string_view utf8, FontId font) const {
        // La vraie mesure, quand il y a une vraie police. Compter les glyphes et
        // multiplier par une largeur fixe etait juste pour la fonte 8x8 et faux
        // pour toute autre : un "W" et un "i" n'occupent pas la meme place.
        if (auto* face = impl_->faceFor(font)) {
            TextMetrics m;
            m.width = face->measure(utf8);
            m.height = face->lineHeight();
            return m;
        }
        const float s = Impl::scaleFor(font);
        const float advance = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * s;

        // The debug font is fixed-pitch, so counting code points is exact. Counting
        // *bytes* would over-measure every accented character in a French comment.
        std::size_t glyphs = 0;
        for (char ch : utf8)
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++glyphs;

        return TextMetrics{ static_cast<float>(glyphs) * advance, advance, advance * 0.8f, advance * 0.2f };
    }

    float SdlRenderer::lineHeight(FontId font) const {
        if (auto* face = impl_->faceFor(font)) return face->lineHeight();
        return static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * Impl::scaleFor(font);
    }

    Size SdlRenderer::surfaceSize() const {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(impl_->renderer, &w, &h);
        return { static_cast<float>(w), static_cast<float>(h) };
    }

    float SdlRenderer::dpiScale() const {
        const float s = SDL_GetWindowDisplayScale(impl_->window);
        return s > 0.f ? s : 1.f;
    }

    std::size_t SdlRenderer::fitCharacters(std::string_view utf8, FontId font, float maxWidth) const {
        if (maxWidth <= 0.f) return 0;
        // Meme raison, et un piege de plus : la coupe doit tomber sur une
        // frontiere de sequence UTF-8. Couper au milieu d'un caractere accentue
        // dessine un carre et casse la chaine pour tout ce qui la relit.
        if (auto* face = impl_->faceFor(font)) return face->fitBytes(utf8, maxWidth);
        const float advance = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * Impl::scaleFor(font);
        if (advance <= 0.f) return utf8.size();

        const auto budget = static_cast<std::size_t>(maxWidth / advance);
        std::size_t glyphs = 0, bytes = 0;
        for (std::size_t i = 0; i < utf8.size(); ++i) {
            if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) {
                if (glyphs == budget) return bytes;
                ++glyphs;
            }
            bytes = i + 1;
        }
        return utf8.size();
    }

    // --------------------------------------------- formes libres (IHM) ---
    //
    //  SDL_RenderGeometry : des triangles colores, sans texture. C'est ce qui
    //  dessine une forme tournee, une ellipse ou un polygone de l'editeur de
    //  vues ; le reste de l'interface ne s'en sert pas.
    // ----------------------------------------------------------------- images ---
    TextureId SdlRenderer::createImage(const std::uint8_t* rgba, int w, int h) {
        if (!rgba || w <= 0 || h <= 0) return {};
        SDL_Texture* tex = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) return {};
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        SDL_UpdateTexture(tex, nullptr, rgba, w * 4);
        const std::uint32_t id = g_nextImage++;
        impl_->images[id] = tex;
        return TextureId{id};
    }

    void SdlRenderer::releaseImage(TextureId id) {
        const auto it = impl_->images.find(id.v);
        if (it == impl_->images.end()) return;
        SDL_DestroyTexture(it->second);
        impl_->images.erase(it);
    }

    void SdlRenderer::drawImage(TextureId id, const Rect& dst, float angleDeg, bool flipH, bool flipV, Color tint) {
        const auto it = impl_->images.find(id.v);
        if (it == impl_->images.end() || dst.w <= 0.f || dst.h <= 0.f) return;
        SDL_SetTextureColorMod(it->second, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(it->second, tint.a);
        const SDL_FRect f{dst.x, dst.y, dst.w, dst.h};
        const SDL_FPoint centre{dst.w / 2.f, dst.h / 2.f};
        const int flip = (flipH ? SDL_FLIP_HORIZONTAL : 0) | (flipV ? SDL_FLIP_VERTICAL : 0);
        SDL_RenderTextureRotated(impl_->renderer, it->second, nullptr, &f, angleDeg, &centre, static_cast<SDL_FlipMode>(flip));
    }

    void SdlRenderer::fillTriangles(const Vertex* v, std::size_t n) {
        if (!v || n < 3) return;
        std::vector<SDL_Vertex> out;
        out.reserve(n);
        for (std::size_t i = 0; i + 2 < n + 2 && i < n; ++i) {
            const Color c = v[i].color;
            out.push_back(SDL_Vertex{{v[i].pos.x, v[i].pos.y},
                                     {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f},
                                     {0.f, 0.f}});
        }
        const int count = static_cast<int>(out.size() - out.size() % 3);
        if (count >= 3) SDL_RenderGeometry(impl_->renderer, nullptr, out.data(), count, nullptr, 0);
    }

    //  Le texte d'un objet tourne : chaque glyphe devient un quadrilatere de
    //  l'atlas, tourne autour du pivot. Les glyphes sont d'abord demandes a
    //  l'atlas, PUIS l'atlas est envoye : un caractere vu pour la premiere fois
    //  est dessine des cette image, pas a la suivante.
    void SdlRenderer::drawTextRotated(Point origin, std::string_view utf8, FontId font, Color c,
                                      float angleDeg, Point pivot) {
        if (utf8.empty()) return;
        if (std::fabs(angleDeg) < 0.01f) { drawText(origin, utf8, font, c); return; }
        auto* face = impl_->faceFor(font);
        if (!face) { drawText(origin, utf8, font, c); return; }

        struct Placed { Glyph g; float x, y; };
        std::vector<Placed> glyphs;
        float penX = origin.x;
        const float baseline = origin.y + face->ascent();
        std::size_t at = 0;
        while (at < utf8.size()) {
            const char32_t code = FontAtlas::decode(utf8, at);
            if (code == 0) break;
            const Glyph g = face->glyph(code);
            if (g.w > 0 && g.h > 0) glyphs.push_back({g, penX + g.bearingX, baseline + g.bearingY});
            penX += g.advance;
        }
        impl_->uploadIfDirty(font, *face);
        SDL_Texture* atlas = impl_->atlasTexture(font);
        if (!atlas || glyphs.empty()) return;

        const float a = angleDeg * 3.14159265f / 180.f, cs = std::cos(a), sn = std::sin(a);
        auto turn = [&](float x, float y) {
            const float dx = x - pivot.x, dy = y - pivot.y;
            return SDL_FPoint{pivot.x + dx * cs - dy * sn, pivot.y + dx * sn + dy * cs};
        };
        const SDL_FColor col{c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        const float tw = static_cast<float>(face->width()), th = static_cast<float>(face->height());
        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
        vertices.reserve(glyphs.size() * 4);
        indices.reserve(glyphs.size() * 6);
        for (const auto& p : glyphs) {
            const float u0 = p.g.x / tw, v0 = p.g.y / th;
            const float u1 = (p.g.x + p.g.w) / tw, v1 = (p.g.y + p.g.h) / th;
            const int base = static_cast<int>(vertices.size());
            vertices.push_back({turn(p.x, p.y), col, {u0, v0}});
            vertices.push_back({turn(p.x + p.g.w, p.y), col, {u1, v0}});
            vertices.push_back({turn(p.x + p.g.w, p.y + p.g.h), col, {u1, v1}});
            vertices.push_back({turn(p.x, p.y + p.g.h), col, {u0, v1}});
            for (int k : {0, 1, 2, 0, 2, 3}) indices.push_back(base + k);
        }
        SDL_SetTextureColorMod(atlas, 255, 255, 255);
        SDL_SetTextureAlphaMod(atlas, 255);
        SDL_RenderGeometry(impl_->renderer, atlas, vertices.data(), static_cast<int>(vertices.size()),
                           indices.data(), static_cast<int>(indices.size()));
    }

    //  Lot 13 : dessiner dans une texture cible, puis la relire.
    bool SdlRenderer::beginOffscreen(int w, int h, Color clear) {
        if (impl_->offscreen || w <= 0 || h <= 0) return false;
        SDL_Texture* t = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t) return false;
        impl_->savedTarget = SDL_GetRenderTarget(impl_->renderer);
        if (!SDL_SetRenderTarget(impl_->renderer, t)) {
            SDL_DestroyTexture(t);
            return false;
        }
        impl_->offscreen = t;
        impl_->savedClips = impl_->clipStack;
        impl_->clipStack.clear();
        SDL_SetRenderClipRect(impl_->renderer, nullptr);
        SDL_SetRenderDrawColor(impl_->renderer, clear.r, clear.g, clear.b, 255);
        SDL_RenderClear(impl_->renderer);
        return true;
    }

    bool SdlRenderer::endOffscreen(std::vector<std::uint8_t>& rgba, int& w, int& h) {
        w = h = 0;
        if (!impl_->offscreen) return false;
        const bool ok = readPixels(rgba, w, h);
        SDL_SetRenderTarget(impl_->renderer, impl_->savedTarget);
        SDL_DestroyTexture(impl_->offscreen);
        impl_->offscreen = nullptr;
        impl_->savedTarget = nullptr;
        impl_->clipStack = impl_->savedClips;
        impl_->savedClips.clear();
        impl_->applyClip(1.f);
        return ok;
    }

    //  L'image de la trame en cours, avant SDL_RenderPresent : apres, le
    //  contenu du tampon n'est plus garanti.
    bool SdlRenderer::readPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) {
        w = h = 0;
        SDL_Surface* raw = SDL_RenderReadPixels(impl_->renderer, nullptr);
        if (!raw) return false;
        SDL_Surface* conv = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(raw);
        if (!conv) return false;
        w = conv->w;
        h = conv->h;
        rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        for (int y = 0; y < h; ++y)
            std::memcpy(rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                        static_cast<const std::uint8_t*>(conv->pixels) + static_cast<std::size_t>(y) * static_cast<std::size_t>(conv->pitch),
                        static_cast<std::size_t>(w) * 4);
        SDL_DestroySurface(conv);
        return true;
    }

} // namespace gfx
