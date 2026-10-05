#include "HelpArticleView.hpp"
#include "../NoveltyMarks.hpp"      // 1.10 : l'orange des nouveautes

#include "../Syntax.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace ui {
namespace {

// Le rythme. Tout est un multiple de quatre, y compris les creux entre
// sections : c'est ce qui fait qu'une page dont les blocs changent d'ordre
// reste la meme page, au lieu de respirer differemment a chaque fois.
constexpr float kPadX        = 20.f;
constexpr float kPadTop      = 16.f;
constexpr float kPadBottom   = 24.f;
constexpr float kTermIndent  = 16.f;
constexpr float kCodePad     = 10.f;
constexpr float kBadgeW      = 28.f;
constexpr float kMarkerSize  = 6.f;

// Les nouveaux blocs. Memes regles : des multiples de quatre, et un creux de
// fin de bloc qui ne depend que du genre de bloc.
constexpr float kCardPad     = 16.f;
constexpr float kCardGap     = 12.f;
constexpr float kPillPadX    = 10.f;
constexpr float kPillPadY    = 4.f;
constexpr float kPillGap     = 8.f;
constexpr float kRingSize    = 64.f;
constexpr float kGutterW     = 32.f;
constexpr float kTocMin      = 1000.f;   // en dessous, pas de sommaire flottant
constexpr float kTocW        = 220.f;
// La longueur de ligne. Au-dela de quatre-vingt-dix caracteres environ, l'oeil
// perd la ligne suivante au retour : les pages de documentation serieuses
// plafonnent toutes la colonne, et le blanc a droite est celui du sommaire.
constexpr float kMaxMeasure  = 820.f;
// La largeur sous laquelle une colonne de tableau ne descend pas tant que la
// page le permet (une quinzaine de caracteres) : en dessous, chaque mot fait
// sa ligne et la cellule ne se lit plus.
constexpr float kTableMinCol = 120.f;

// L'echelle typographique quand le renderer ne sait pas la mesurer lui-meme :
// par proportion de la taille courante. Les rapports sont ceux du theme
// (26 / 18 / 16 / 13 pour un corps de 16).
float scaleOf(HelpFont f) {
    switch (f) {
        case HelpFont::Title:   return 26.f / 16.f;
        case HelpFont::Lead:    return 18.f / 16.f;
        case HelpFont::Caption: return 13.f / 16.f;
        default:                return 1.f;
    }
}

float measureF(const HelpMetrics& m, std::string_view t, HelpFont f) {
    if (m.measureFont) return m.measureFont(t, f);
    return m.measure(t, f == HelpFont::Title || f == HelpFont::Heading, f == HelpFont::Mono)
         * scaleOf(f);
}

float lineF(const HelpMetrics& m, HelpFont f) {
    if (m.lineHeightFont) return m.lineHeightFont(f);
    return (f == HelpFont::Mono ? m.monoLineHeight : m.lineHeight) * scaleOf(f);
}

// Decoupe un paragraphe en lignes qui tiennent dans `width`.
//
// La coupe se fait entre deux mots. Un mot plus large que la colonne entiere -
// un type comme ARRAY[0..15] OF ST_EQ_Conveyor dans une fenetre etroite - est
// pose seul sur sa ligne et deborde, plutot que d'etre coupe en plein milieu :
// un identifiant coupe n'est plus un identifiant, et c'est justement ce qu'on
// vient recopier.
template <class Measure>
std::vector<std::string> wrapWith(std::string_view text, float width, Measure&& measure) {
    std::vector<std::string> out;
    // Garde de precondition, pas une branche : une passe de mutations ne peut
    // pas la tuer, parce que tous les appelants ecartent deja le texte vide.
    // Elle reste parce que la fin de cette fonction lit `text.back()`, qui n'est
    // defini que sur un texte non vide.
    if (text.empty()) return out;
    if (width <= 0.f) width = 1.f;

    std::size_t lineStart = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const bool end = (i == text.size());
        if (!end && text[i] != '\n') continue;

        std::string_view para = text.substr(lineStart, i - lineStart);
        lineStart = i + 1;

        if (para.empty()) { out.emplace_back(); continue; }

        std::string current;
        std::size_t wordStart = 0;
        while (wordStart <= para.size()) {
            auto sp = para.find(' ', wordStart);
            if (sp == std::string_view::npos) sp = para.size();
            const auto word = para.substr(wordStart, sp - wordStart);

            std::string candidate = current.empty() ? std::string(word)
                                                    : current + " " + std::string(word);
            if (!current.empty() && measure(candidate) > width) {
                out.push_back(current);
                current = std::string(word);
            } else {
                current = std::move(candidate);
            }
            if (sp == para.size()) break;
            wordStart = sp + 1;
        }
        out.push_back(current);
    }
    // Un texte qui finit par un retour a la ligne produit un dernier
    // paragraphe vide : c'est l'artefact du decoupage, pas une ligne blanche
    // voulue. La garder ajouterait un creux au bas de chaque bloc.
    if (!out.empty() && out.back().empty() && text.back() == '\n') out.pop_back();
    return out;
}

std::vector<std::string> wrap(std::string_view text, float width,
                              const HelpMetrics& m, bool bold, bool mono) {
    return wrapWith(text, width, [&](std::string_view s) { return m.measure(s, bold, mono); });
}

// Une cellule de tableau se replie comme un paragraphe ; mais un mot plus large
// que sa colonne y est coupe (entre deux caracteres, pas dans un caractere) :
// dans un tableau, deborder, c'est ecrire sur la colonne voisine ou hors de la
// page. Les largeurs de colonnes evitent ce cas tant que la place le permet.
std::vector<std::string> wrapCell(std::string_view text, float width, const HelpMetrics& m,
                                  bool bold) {
    std::vector<std::string> out;
    for (auto& l : wrap(text, width, m, bold, false)) {
        if (l.empty() || m.measure(l, bold, false) <= width) { out.push_back(std::move(l)); continue; }
        std::string piece;
        for (std::size_t i = 0; i < l.size();) {
            std::size_t n = 1;
            while (i + n < l.size() && (static_cast<unsigned char>(l[i + n]) & 0xC0) == 0x80) ++n;
            std::string next = piece + l.substr(i, n);
            if (!piece.empty() && m.measure(next, bold, false) > width) {
                out.push_back(std::move(piece));
                piece = l.substr(i, n);
            } else {
                piece = std::move(next);
            }
            i += n;
        }
        if (!piece.empty()) out.push_back(std::move(piece));
    }
    return out;
}

// LE CODE NE SE REPLIE PAS COMME DE LA PROSE. wrapWith() recolle les mots
// avec une espace : l'indentation d'une ligne d'exemple disparaissait, et un
// corps de IF se lisait au meme niveau que le IF. Ici chaque ligne source garde
// ses espaces de tete ; une ligne trop longue est coupee a une espace, et la
// suite reprend sous la meme indentation, plus deux. Seule la premiere piece
// d'une ligne source porte un numero : c'est la ligne du fichier qu'on compte,
// pas la ligne d'ecran.
struct CodeLine {
    std::string text;
    int         number{0};    // 0 : suite d'une ligne coupee
};

template <class Measure>
std::vector<CodeLine> wrapCode(std::string_view text, float width, Measure&& measure) {
    std::vector<CodeLine> out;
    if (text.empty()) return out;
    if (width <= 0.f) width = 1.f;

    int n = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view raw = text.substr(start, nl - start);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        ++n;

        // Les tabulations deviennent des espaces : la police n'a pas de glyphe
        // pour elles, et une boite a la place d'un retrait ressemble a un bug.
        std::string src;
        for (const char ch : raw) {
            if (ch == '\t') src.append(4 - src.size() % 4, ' ');
            else            src.push_back(ch);
        }
        const std::size_t firstInk = src.find_first_not_of(' ');
        const std::string indent = src.substr(0, firstInk == std::string::npos ? 0 : firstInk);

        std::string_view rest = src;
        std::string prefix;          // vide pour la premiere piece
        bool first = true;
        for (;;) {
            const std::string whole = prefix + std::string(rest);
            if (rest.empty() || measure(whole) <= width) {
                out.push_back({whole, first ? n : 0});
                break;
            }
            // La derniere espace qui tient, hors de l'indentation de tete :
            // couper dans le retrait ne ferait qu'une ligne blanche.
            const std::size_t from = first ? indent.size() : 0;
            std::size_t cut = std::string_view::npos;
            for (std::size_t sp = rest.find(' ', from); sp != std::string_view::npos;
                 sp = rest.find(' ', sp + 1)) {
                if (sp == 0) continue;
                if (measure(prefix + std::string(rest.substr(0, sp))) <= width) cut = sp;
                else break;
            }
            // Rien ne tient : on deborde jusqu'a la premiere espace plutot que
            // de couper un identifiant, qu'on ne pourrait plus recopier.
            if (cut == std::string_view::npos) cut = rest.find(' ', std::max<std::size_t>(from, 1));
            if (cut == std::string_view::npos) { out.push_back({whole, first ? n : 0}); break; }

            out.push_back({prefix + std::string(rest.substr(0, cut)), first ? n : 0});
            rest.remove_prefix(cut);
            while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
            prefix = indent + "  ";
            first = false;
            if (rest.empty()) break;
        }

        if (nl == text.size()) break;
        start = nl + 1;
    }
    // Le retour a la ligne final n'est pas une ligne vide voulue.
    if (!out.empty() && out.back().text.empty() && text.back() == '\n') out.pop_back();
    return out;
}

struct Cursor {
    HelpLayout* out;
    const HelpMetrics* m;
    float y{0.f};
    float width{0.f};

    void run(float x, std::string t, HelpRole role, bool bold = false, bool mono = false) {
        HelpRun r;
        r.x = x; r.y = y; r.text = std::move(t); r.role = role; r.bold = bold; r.mono = mono;
        r.font = mono ? HelpFont::Mono : HelpFont::Body;
        out->runs.push_back(std::move(r));
    }
    void runF(float x, float yy, std::string t, HelpRole role, HelpFont f,
              gfx::Color color = kNoColor, bool faux = false, Tone tone = Tone::None) {
        HelpRun r;
        r.x = x; r.y = yy; r.text = std::move(t); r.role = role;
        r.bold = faux; r.mono = f == HelpFont::Mono; r.font = f; r.color = color;
        r.fauxBold = faux; r.tone = tone;
        out->runs.push_back(std::move(r));
    }
    void advance(float dy) { y += dy; }
    [[nodiscard]] float lh(bool mono) const { return mono ? m->monoLineHeight : m->lineHeight; }

    // Pose un paragraphe et rend la hauteur consommee.
    float paragraph(float x, float w, std::string_view text, HelpRole role,
                    bool bold = false, bool mono = false) {
        const auto lines = wrap(text, w, *m, bold, mono);
        const float step = lh(mono);
        for (const auto& l : lines) {
            if (!l.empty()) run(x, l, role, bold, mono);
            advance(step);
        }
        return static_cast<float>(lines.size()) * step;
    }

    // La meme chose dans une taille de l'echelle.
    float paragraphF(float x, float w, std::string_view text, HelpRole role, HelpFont f,
                     gfx::Color color = kNoColor, bool faux = false) {
        const auto lines = wrapWith(text, w, [&](std::string_view s) { return measureF(*m, s, f); });
        const float step = lineF(*m, f);
        for (const auto& l : lines) {
            if (!l.empty()) runF(x, y, l, role, f, color, faux);
            advance(step);
        }
        return static_cast<float>(lines.size()) * step;
    }

    void decor(HelpDecorKind k, gfx::Rect r, bool muted = false, gfx::Color c = kNoColor,
               float value = 0.f, Tone tone = Tone::None) {
        // Une fenetre repliee au maximum ne doit pas produire de rectangle de
        // largeur negative : selon le renderer, ca ne dessine rien, ca dessine
        // a l'envers, ou ca s'arrete.
        r.w = std::max(0.f, r.w);
        r.h = std::max(0.f, r.h);
        out->decor.push_back({k, r, muted, c, value, tone});
    }
};

// Une rangee de puces qui passe a la ligne quand elle deborde. Rend la hauteur
// consommee. `clickable` pose une zone cliquable sur chacune.
float pillRow(Cursor& c, float x0, float width, const std::vector<HelpLink>& links,
              bool clickable) {
    const auto& m = *c.m;
    const float h = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
    float x = x0;
    float top = c.y;
    for (const auto& l : links) {
        const float w = measureF(m, l.text, HelpFont::Caption) + 2 * kPillPadX;
        if (x > x0 && x + w > x0 + width) { x = x0; top += h + kPillGap; }
        c.decor(HelpDecorKind::Pill, {x, top, w, h}, false, l.color, 0.f, l.tone);
        c.runF(x + kPillPadX, top + kPillPadY, l.text, HelpRole::Custom, HelpFont::Caption,
               l.color, false, l.tone);
        if (clickable && !l.target.empty())
            c.out->hotspots.push_back({{x, top, w, h}, l.target, l.tip});
        x += w + kPillGap;
    }
    const float used = links.empty() ? 0.f : (top - c.y) + h;
    c.advance(used);
    return used;
}

} // namespace

HelpLayout layoutArticle(const HelpArticle& a, float width, const HelpMetrics& m) {
    HelpLayout out;
    Cursor c{&out, &m, kPadTop, width};
    const float content = std::min(kMaxMeasure, std::max(24.f, width - 2 * kPadX));

    // 1.10 (chantier P) : le cadre orange des blocs nouveaux, ouvert sur le
    // premier, ferme apres le dernier d'une suite de meme etiquette.
    std::string frameLabel;
    float       frameTop = 0.f;
    const auto closeFrame = [&] {
        if (frameLabel.empty()) return;
        const float x = kPadX - 10.f, w = content + 20.f;
        const float bottom = std::max(c.y - 2.f, frameTop + 8.f);
        c.decor(HelpDecorKind::NoveltyFrame, {x, frameTop, w, bottom - frameTop});
        const float pw = measureF(m, frameLabel, HelpFont::Caption) + 2 * kPillPadX;
        const float ph = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
        const float px = x + w - pw - 10.f, py = frameTop - ph / 2.f;
        c.decor(HelpDecorKind::NoveltyPill, {px, py, pw, ph});
        c.runF(px + kPillPadX, py + kPillPadY, frameLabel, HelpRole::Novelty, HelpFont::Caption, kNoColor, true);
        frameLabel.clear();
    };

    for (std::size_t bi = 0; bi < a.blocks.size(); ++bi) {
        const auto& b = a.blocks[bi];
        if (b.novelty != frameLabel) {
            closeFrame();
            if (!b.novelty.empty()) {
                frameLabel = b.novelty;
                frameTop = c.y - 6.f;
                c.advance(8.f);          // la place de l'etiquette, a cheval sur le cadre
            }
        }
        // UN BLOC SANS CONTENU NE PREND PAS DE PLACE. Un resume pas encore
        // ecrit, un exemple absent : le compositeur d'article les pose sans
        // savoir qu'ils sont vides, et chacun laisserait derriere lui son creux
        // de fin de bloc.
        const bool porteurDeTexte =
            b.kind != HelpBlockKind::Separator && b.kind != HelpBlockKind::Meter &&
            b.kind != HelpBlockKind::Term      && b.kind != HelpBlockKind::Fault &&
            b.kind != HelpBlockKind::Ring      && b.kind != HelpBlockKind::Links;
        if (porteurDeTexte && b.text.empty() && b.label.empty()) continue;

        switch (b.kind) {
            case HelpBlockKind::Title:
                c.paragraphF(kPadX, content, b.text, HelpRole::Title, HelpFont::Title, kNoColor, true);
                c.advance(2.f);
                break;

            case HelpBlockKind::Subtitle:
                c.paragraph(kPadX, content, b.text, HelpRole::Subtitle);
                c.advance(12.f);
                break;

            case HelpBlockKind::Lead:
                c.paragraphF(kPadX, content, b.text, HelpRole::Lead, HelpFont::Lead);
                c.advance(16.f);
                break;

            case HelpBlockKind::Heading: {
                c.advance(16.f);
                const float top = c.y;
                out.sections.push_back({top, b.text});
                c.paragraphF(kPadX, content, b.text, HelpRole::Heading, HelpFont::Heading,
                             kNoColor, true);
                // Un filet sous l'intertitre plutot qu'un cadre autour : il
                // separe sans enfermer, et il tient sur une ligne de pixels.
                c.decor(HelpDecorKind::HeadingRule, {kPadX, c.y + 4.f, content, 1.f});
                if (!b.label.empty()) {
                    // Le compteur de droite ("11 / 24 documentes") est pose sur
                    // la meme ligne que l'intertitre, aligne a droite.
                    const float w = m.measure(b.label, false, false);
                    HelpRun r;
                    r.x = kPadX + content - w; r.y = top; r.text = b.label;
                    r.role = HelpRole::Muted;
                    out.runs.push_back(std::move(r));
                }
                c.advance(14.f);
                break;
            }

            case HelpBlockKind::Paragraph:
                c.paragraph(kPadX, content, b.text, HelpRole::Body);
                c.advance(8.f);
                break;

            case HelpBlockKind::Code: {
                // UNE CARTE DE CODE, COLOREE, NUMEROTEE, COPIABLE. Le lecteur
                // vient la recopier : les numeros disent ou il en est, les
                // couleurs lui disent ce qui est un mot-cle, et le bouton lui
                // evite la selection a la souris.
                // 1.11 (T2) : "texte" (l'apercu de Signaler) : ni numeros, ni coloration.
                const bool plain = b.label == "texte";
                const float gutter = plain ? 0.f : std::min(kGutterW, std::max(0.f, content / 4.f));
                const float inner  = std::max(1.f, content - 2 * kCodePad - gutter);
                const auto mono = [&m](std::string_view s) { return m.measure(s, false, true); };
                const auto lines = wrapCode(b.text, inner, mono);
                const std::string copier = "Copier";
                const float cw = measureF(m, copier, HelpFont::Caption) + 2 * kPillPadX;
                const float ch = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
                // 1.10 (chantier P) : un exemple a plusieurs notations - les
                // puces du bloc sont ses notations (ST, C, C++) ; le selecteur
                // tient une bande en tete du cadre, au-dessus du code (pose sur
                // la premiere ligne, il la cacherait).
                const float head = b.links.size() > 1 ? ch + 12.f : 0.f;
                const float h = static_cast<float>(lines.size()) * m.monoLineHeight
                              + 2 * kCodePad + head;
                c.decor(HelpDecorKind::CodePanel, {kPadX, c.y, content, h});
                c.decor(HelpDecorKind::Gutter, {kPadX, c.y + head, gutter, h - head});
                if (head > 0.f) {
                    float sx = kPadX + 8.f;
                    for (const auto& n : b.links) {
                        const bool on = n.tone == Tone::Accent;
                        const float sw = measureF(m, n.text, HelpFont::Caption) + 2 * kPillPadX + 6.f;
                        const gfx::Rect seg{sx, c.y + 6.f, sw, ch};
                        c.decor(HelpDecorKind::Pill, seg, !on, kNoColor, 0.f, on ? Tone::Accent : Tone::None);
                        c.runF(seg.x + kPillPadX + 3.f, seg.y + kPillPadY, n.text, on ? HelpRole::Accent : HelpRole::Muted,
                               HelpFont::Caption, kNoColor, on);
                        out.hotspots.push_back({seg, n.target, n.tip});
                        sx += sw + 4.f;
                    }
                }

                // Le bouton "Copier", en haut a droite, cliquable. Il ne vient
                // que s'il laisse de la place au code : sur une colonne etroite,
                // un bouton pose sur la premiere ligne la cacherait.
                if (content > cw + gutter + 40.f) {
                    const gfx::Rect btn{kPadX + content - cw - 8.f, c.y + (head > 0.f ? 6.f : 8.f), cw, ch};
                    c.decor(HelpDecorKind::Pill, btn, true);
                    c.runF(btn.x + kPillPadX, btn.y + kPillPadY, copier, HelpRole::Muted,
                           HelpFont::Caption);
                    out.hotspots.push_back({btn, "copy:" + std::to_string(bi),
                                            "Copier l'exemple dans le presse-papier"});
                }

                c.advance(kCodePad + head);
                // La coloration suit la notation (le bloc la porte dans `label`).
                const Language lang = b.label == "C" ? Language::C : b.label == "C++" ? Language::Cpp
                                    : plain ? Language::PlainText : Language::StructuredText;
                bool inComment = false;
                std::vector<Token> tokens;
                const float x0 = kPadX + gutter + kCodePad;
                for (const auto& cl : lines) {
                    const std::string& l = cl.text;
                    // Le numero, a droite dans la gouttiere.
                    if (cl.number > 0 && gutter >= 12.f) {
                        const auto num = std::to_string(cl.number);
                        const float nw = measureF(m, num, HelpFont::Caption);
                        c.runF(kPadX + std::max(2.f, gutter - nw - 8.f),
                               c.y + (m.monoLineHeight - lineF(m, HelpFont::Caption)) / 2.f,
                               num, HelpRole::Muted, HelpFont::Caption);
                    }
                    if (!l.empty()) {
                        tokens.clear();
                        if (!plain) tokenizeLine(l, lang, tokens, inComment);
                        // Un run par jeton, et un run pour le texte entre les
                        // jetons : sinon les espaces et les operateurs non
                        // reconnus disparaitraient. Les espaces de tete ne font
                        // pas de run : elles sont dans l'abscisse du suivant.
                        std::size_t at = 0;
                        auto piece = [&](std::size_t from, std::size_t to, std::uint8_t cls) {
                            // Les espaces aux bords d'un jeton ne font pas de
                            // texte : elles sont dans l'abscisse.
                            while (from < to && l[from] == ' ') ++from;
                            while (to > from && l[to - 1] == ' ') --to;
                            if (to <= from) return;
                            const auto t = std::string_view(l).substr(from, to - from);
                            HelpRun r;
                            r.x = x0 + mono(std::string_view(l).substr(0, from));
                            r.y = c.y; r.text = std::string(t); r.role = HelpRole::Code;
                            r.mono = true; r.font = HelpFont::Mono; r.syntax = cls;
                            out.runs.push_back(std::move(r));
                        };
                        for (const auto& t : tokens) {
                            const std::size_t b0 = std::min<std::size_t>(t.begin, l.size());
                            const std::size_t e0 = std::min<std::size_t>(t.end, l.size());
                            if (b0 < at) continue;           // jeton chevauchant : deja pose
                            piece(at, b0, 0xFF);
                            piece(b0, e0, static_cast<std::uint8_t>(t.cls));
                            at = e0;
                        }
                        piece(at, l.size(), 0xFF);
                    }
                    c.advance(m.monoLineHeight);
                }
                c.advance(kCodePad + 12.f);
                break;
            }

            case HelpBlockKind::Term: {
                if (!b.pills.empty()) {
                    // LA CARTE D'UN PARAMETRE. Nom en fixe, puis ses etiquettes
                    // - type, portee, defaut - puis le resume court et l'aide
                    // longue. Un parametre non documente garde sa carte, en
                    // creux : la liste des trous est ce qu'on vient chercher
                    // quand on documente.
                    const float x = kPadX + kCardPad;
                    const float w = std::max(1.f, content - 2 * kCardPad);
                    const float top = c.y;
                    const std::size_t decorAt = out.decor.size();
                    c.advance(kCardPad - 4.f);

                    c.decor(HelpDecorKind::TermMarker,
                            {x - 10.f, c.y + (m.lineHeight - kMarkerSize) / 2.f,
                             kMarkerSize, kMarkerSize}, b.muted);
                    const float nameW = m.measure(b.label, true, true);
                    c.run(x + 4.f, b.label, HelpRole::Term, true, true);
                    out.runs.back().fauxBold = true;

                    // Les etiquettes a droite du nom, sur la meme ligne tant
                    // qu'elles tiennent, puis a la ligne sous le nom : une
                    // etiquette qui ne tient pas n'est pas une etiquette en
                    // trop, c'est une information qu'on perdait.
                    const float firstX = x + 4.f + nameW + 12.f;
                    float px = firstX;
                    const float ph = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
                    float py = c.y + (m.lineHeight - ph) / 2.f;
                    float bottom = c.y + m.lineHeight;
                    for (const auto& p : b.pills) {
                        const float pw = measureF(m, p.text, HelpFont::Caption) + 2 * kPillPadX;
                        if (px + pw > x + w && px > x + 4.5f) { px = x + 4.f; py = bottom + 6.f; }
                        c.decor(HelpDecorKind::Pill, {px, py, pw, ph}, false, p.color, 0.f,
                                p.tone);
                        c.runF(px + kPillPadX, py + kPillPadY, p.text, HelpRole::Custom,
                               HelpFont::Caption, p.color, false, p.tone);
                        if (!p.target.empty())
                            out.hotspots.push_back({{px, py, pw, ph}, p.target, p.tip});
                        bottom = std::max(bottom, py + ph);
                        px += pw + 6.f;
                    }
                    c.y = bottom;
                    c.advance(8.f);

                    if (!b.extra.empty()) {
                        c.paragraph(x + 4.f, w - 4.f, b.extra, HelpRole::Muted);
                        c.advance(2.f);
                    }
                    if (!b.text.empty()) c.paragraph(x + 4.f, w - 4.f, b.text, HelpRole::Body);
                    else {
                        c.runF(x + 4.f, c.y, "\xC3\x80 documenter", HelpRole::Warning, HelpFont::Caption);
                        c.advance(lineF(m, HelpFont::Caption));
                    }
                    c.advance(kCardPad - 6.f);
                    // La carte est posee APRES son contenu (sa hauteur n'est
                    // connue qu'a la fin) mais doit etre peinte AVANT : on
                    // l'insere a l'endroit ou le contenu a commence.
                    out.decor.insert(out.decor.begin() + static_cast<std::ptrdiff_t>(decorAt),
                                     HelpDecor{HelpDecorKind::Card,
                                               {kPadX, top, content, c.y - top}, b.muted});
                    // Une carte qui designe quelque chose (un resultat de
                    // recherche, un code de diagnostic) se clique en entier.
                    if (!b.links.empty() && !b.links.front().target.empty())
                        out.hotspots.push_back({{kPadX, top, content, c.y - top},
                                                b.links.front().target, b.links.front().tip});
                    c.advance(kCardGap);
                    break;
                }

                const float x = kPadX + kTermIndent;
                const float w = content - kTermIndent;
                // Le point de gauche dit d'un coup d'oeil ce qui est documente
                // et ce qui ne l'est pas.
                c.decor(HelpDecorKind::TermMarker,
                        {kPadX + 2.f, c.y + (m.lineHeight - kMarkerSize) / 2.f,
                         kMarkerSize, kMarkerSize}, b.muted);

                // Le type est pose dans une COLONNE, pas juste apres le nom.
                constexpr float kTypeColumn = 132.f;
                const float nameW = m.measure(b.label, true, true);
                c.run(x, b.label, HelpRole::Term, true, true);
                if (!b.detail.empty())
                    c.run(x + std::max(nameW + 12.f, kTypeColumn), b.detail, HelpRole::Muted);
                c.advance(m.lineHeight + 2.f);

                if (!b.extra.empty()) {
                    c.paragraph(x, w, b.extra, HelpRole::Muted);
                    c.advance(2.f);
                }
                if (!b.text.empty()) c.paragraph(x, w, b.text, HelpRole::Body);
                c.advance(10.f);
                break;
            }

            case HelpBlockKind::Fault: {
                const float x = kPadX + kBadgeW + 10.f;
                const float w = content - kBadgeW - 10.f;
                c.decor(HelpDecorKind::Badge, {kPadX, c.y, kBadgeW, m.lineHeight}, b.muted);
                const float bw = m.measure(b.label, true, false);
                HelpRun r;
                r.x = kPadX + (kBadgeW - bw) / 2.f; r.y = c.y; r.text = b.label;
                r.role = b.muted ? HelpRole::Muted : HelpRole::Warning; r.bold = true;
                r.fauxBold = true;
                out.runs.push_back(std::move(r));
                const float used = c.paragraph(x, w, b.text, HelpRole::Body);
                if (used < m.lineHeight) c.advance(m.lineHeight - used);
                c.advance(8.f);
                break;
            }

            case HelpBlockKind::Chips:
                c.paragraph(kPadX, content, b.text, HelpRole::Accent);
                c.advance(10.f);
                break;

            case HelpBlockKind::Note:
            case HelpBlockKind::Callout: {
                // L'ENCADRE. Un fond teinte, une barre a gauche, un
                // pictogramme : trois indices pour la meme chose, parce qu'un
                // avertissement doit se voir avant d'etre lu.
                const int sev = b.kind == HelpBlockKind::Note ? 1 : std::clamp(b.severity, 0, 2);
                const HelpRole role = sev == 2 ? HelpRole::Error
                                    : sev == 1 ? HelpRole::Warning : HelpRole::Accent;
                const float top = c.y;
                const std::size_t decorAt = out.decor.size();
                const float icon = 18.f;
                const float x = kPadX + 14.f + icon + 10.f;
                const float w = std::max(1.f, content - (x - kPadX) - 12.f);
                c.advance(10.f);
                const Icon glyph = b.icon != Icon::None ? b.icon
                                 : sev == 2 ? Icon::Error : sev == 1 ? Icon::Warning : Icon::Info;
                c.decor(HelpDecorKind::Glyph, {kPadX + 14.f, c.y + (m.lineHeight - icon) / 2.f,
                                               icon, icon}, false, kNoColor,
                        static_cast<float>(glyph),
                        sev == 2 ? Tone::Error : sev == 1 ? Tone::Warning : Tone::Info);
                if (!b.label.empty()) {
                    const std::size_t from = out.runs.size();
                    c.paragraph(x, w, b.label, role, true);
                    for (std::size_t i = from; i < out.runs.size(); ++i) out.runs[i].fauxBold = true;
                    c.advance(2.f);
                }
                c.paragraph(x, w, b.text, b.kind == HelpBlockKind::Note ? HelpRole::Warning
                                                                         : HelpRole::Body);
                c.advance(10.f);
                out.decor.insert(out.decor.begin() + static_cast<std::ptrdiff_t>(decorAt),
                                 HelpDecor{HelpDecorKind::CalloutBox,
                                           {kPadX, top, content, c.y - top}, false, kNoColor,
                                           static_cast<float>(sev)});
                c.advance(12.f);
                break;
            }

            case HelpBlockKind::Meter: {
                if (!b.label.empty()) {
                    c.run(kPadX, b.label, HelpRole::Muted);
                    c.advance(m.lineHeight + 2.f);
                }
                const float h = 6.f;
                const float v = std::clamp(b.value, 0.f, 1.f);
                c.decor(HelpDecorKind::MeterTrack, {kPadX, c.y, content, h});
                c.decor(HelpDecorKind::MeterFill, {kPadX, c.y, content * v, h}, v >= 0.999f);
                c.advance(h + 14.f);
                break;
            }

            case HelpBlockKind::Separator:
                c.advance(6.f);
                c.decor(HelpDecorKind::Separator, {kPadX, c.y, content, 1.f});
                c.advance(10.f);
                break;

            case HelpBlockKind::Hero: {
                // L'EN-TETE. Un bandeau teinte de la couleur de famille, un
                // lisere a gauche, le fil d'Ariane au-dessus du nom, le nom en
                // grand, et a sa droite le genre et la version. Tout ce qui
                // situe la page, en deux lignes et sans lire.
                const float top = c.y;
                const std::size_t decorAt = out.decor.size();
                const float x = kPadX + 22.f;
                const float w = std::max(1.f, content - 40.f);
                c.advance(14.f);

                // Le fil d'Ariane : chaque etape est cliquable, la derniere
                // non - c'est la page ou l'on est. "/" et non un chevron : il
                // est dans la police de secours.
                if (!b.links.empty()) {
                    float bx = x;
                    const float ch = lineF(m, HelpFont::Caption);
                    for (std::size_t i = 0; i < b.links.size(); ++i) {
                        const auto& l = b.links[i];
                        const float lw = measureF(m, l.text, HelpFont::Caption);
                        const bool last = i + 1 == b.links.size();
                        c.runF(bx, c.y, l.text, last ? HelpRole::Subtitle : HelpRole::Accent,
                               HelpFont::Caption);
                        if (!last && !l.target.empty())
                            out.hotspots.push_back({{bx - 2.f, c.y - 1.f, lw + 4.f, ch + 2.f},
                                                    l.target, l.tip});
                        bx += lw;
                        if (!last) {
                            c.runF(bx + 6.f, c.y, "/", HelpRole::Muted, HelpFont::Caption);
                            bx += 6.f + measureF(m, "/", HelpFont::Caption) + 6.f;
                        }
                    }
                    c.advance(ch + 4.f);
                }

                const float titleH = lineF(m, HelpFont::Title);
                c.runF(x, c.y, b.text, HelpRole::Title, HelpFont::Title, kNoColor, true);
                // Les etiquettes : a droite du nom tant qu'elles tiennent,
                // dessous sinon. Aucune n'est perdue.
                const float ph = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
                const float firstX = x + measureF(m, b.text, HelpFont::Title) + 14.f;
                float px = firstX;
                float py = c.y + (titleH - ph) / 2.f;
                float bottom = c.y + titleH;
                for (const auto& p : b.pills) {
                    const float pw = measureF(m, p.text, HelpFont::Caption) + 2 * kPillPadX;
                    if (px + pw > x + w && px > x + 0.5f) { px = x; py = bottom + 6.f; }
                    c.decor(HelpDecorKind::Pill, {px, py, pw, ph}, false, p.color, 0.f, p.tone);
                    c.runF(px + kPillPadX, py + kPillPadY, p.text, HelpRole::Custom,
                           HelpFont::Caption, p.color, false, p.tone);
                    if (!p.target.empty())
                        out.hotspots.push_back({{px, py, pw, ph}, p.target, p.tip});
                    bottom = std::max(bottom, py + ph);
                    px += pw + 6.f;
                }
                c.y = bottom;
                if (!b.label.empty()) {
                    c.advance(4.f);
                    c.paragraphF(x, w, b.label, HelpRole::Subtitle, HelpFont::Caption);
                }
                c.advance(14.f);

                // Le bandeau et le lisere sont connus a la fin, peints en
                // premier : on les insere la ou l'en-tete a commence.
                const float h = c.y - top;
                out.decor.insert(out.decor.begin() + static_cast<std::ptrdiff_t>(decorAt),
                                 {HelpDecor{HelpDecorKind::HeroBand, {kPadX, top, content, h},
                                            false, b.accent, 0.f, b.tone},
                                  HelpDecor{HelpDecorKind::Stripe, {kPadX, top, 4.f, h},
                                            false, b.accent, 0.f, b.tone}});
                c.advance(18.f);
                break;
            }

            case HelpBlockKind::Bullet: {
                // UNE LISTE EST UNE LISTE. "0 ordre fixe / 1 carrousel / 2 moins
                // usee" ecrit en prose se lisait comme un paragraphe coupe au
                // hasard ; avec des puces, les trois choix se voient d'un coup.
                const float ph = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
                // LE TEXTE COMMENCE APRES LA PLUS LARGE PUCE DE LA LISTE. Un
                // retrait fixe de 36 px faisait passer " apres 3 cycles " sous
                // le texte ; et un retrait par ligne ne s'alignerait plus.
                float indent = 36.f;
                {
                    std::size_t first = bi;
                    while (first > 0 && a.blocks[first - 1].kind == HelpBlockKind::Bullet) --first;
                    for (std::size_t k = first; k < a.blocks.size() && a.blocks[k].kind == HelpBlockKind::Bullet; ++k) {
                        const auto& lab = a.blocks[k].label;
                        if (lab.empty() || lab == "-") continue;
                        const float w = std::max(ph, measureF(m, lab, HelpFont::Caption) + 2 * kPillPadX);
                        indent = std::max(indent, 4.f + w + 10.f);
                    }
                    indent = std::min(indent, 180.f);
                }
                if (!b.label.empty() && b.label != "-") {
                    const float pw = std::max(ph, measureF(m, b.label, HelpFont::Caption)
                                                      + 2 * kPillPadX);
                    const float py = c.y + (m.lineHeight - ph) / 2.f;
                    c.decor(HelpDecorKind::Pill, {kPadX + 4.f, py, pw, ph}, false, b.accent, 0.f,
                            b.tone);
                    c.runF(kPadX + 4.f + (pw - measureF(m, b.label, HelpFont::Caption)) / 2.f,
                           py + kPillPadY, b.label, HelpRole::Custom, HelpFont::Caption, b.accent,
                           false, b.tone);
                } else {
                    c.decor(HelpDecorKind::TermMarker,
                            {kPadX + 12.f, c.y + (m.lineHeight - kMarkerSize) / 2.f,
                             kMarkerSize, kMarkerSize}, false);
                }
                // Une liste de definitions : le terme en gras sur sa ligne, sa
                // definition dessous. "0  ordre fixe  les premiers..." aligne
                // en colonnes dans le fichier ne s'alignait plus en police
                // proportionnelle ; en deux lignes, il n'a plus besoin de l'etre.
                if (!b.detail.empty()) {
                    const std::size_t from = out.runs.size();
                    c.paragraph(kPadX + indent, std::max(1.f, content - indent), b.detail,
                                HelpRole::Term, true);
                    for (std::size_t i = from; i < out.runs.size(); ++i) out.runs[i].fauxBold = true;
                    c.advance(2.f);
                }
                c.paragraph(kPadX + indent, std::max(1.f, content - indent), b.text,
                            HelpRole::Body);
                c.advance(6.f);
                break;
            }

            case HelpBlockKind::Table: {
                // UN TABLEAU, PAS DES ESPACES. Le fichier aligne ses colonnes a
                // coups d'espaces, ce qui ne tient qu'en police fixe. Ici chaque
                // colonne a une largeur tiree de son contenu et s'y replie, et une
                // ligne sur deux est teintee pour que l'oeil suive une ligne de
                // bout en bout.
                std::vector<std::vector<std::string>> rows;
                {
                    std::size_t start = 0;
                    for (std::size_t i = 0; i <= b.text.size(); ++i) {
                        if (i != b.text.size() && b.text[i] != '\n') continue;
                        const std::string_view line(b.text.data() + start, i - start);
                        start = i + 1;
                        if (line.empty()) continue;
                        std::vector<std::string> cells;
                        std::size_t cs = 0;
                        for (std::size_t j = 0; j <= line.size(); ++j) {
                            if (j != line.size() && line[j] != '\t') continue;
                            cells.emplace_back(line.substr(cs, j - cs));
                            cs = j + 1;
                        }
                        rows.push_back(std::move(cells));
                    }
                }
                if (rows.empty()) break;
                std::size_t ncol = 0;
                for (const auto& r : rows) ncol = std::max(ncol, r.size());

                constexpr float kGap = 20.f, kIn = 14.f;
                // 1.10.2 : LES LARGEURS VIENNENT DU CONTENU, ET CHAQUE COLONNE SE
                // REPLIE. Avant, seule la derniere colonne se repliait : une cellule
                // longue au milieu (Etape | Ce qu'elle fait | Le reglage) sortait du
                // tableau et etait coupee au bord du volet. Maintenant une colonne
                // courte garde sa largeur (sa plus longue cellule) ; les longues se
                // partagent le reste au prorata de leur longueur, jamais sous un
                // minimum lisible ni, quand la place le permet, sous leur plus long
                // mot ; la derniere va jusqu'au bord. Rien ne sort de la carte.
                const float avail = std::max(1.f, content - 2 * kIn
                                                      - kGap * static_cast<float>(ncol - 1));
                // La derniere cellule d'une ligne courte s'etend jusqu'au bord : elle
                // ne compte pas dans la largeur de sa colonne.
                const auto spans = [&](const std::vector<std::string>& r, std::size_t k) {
                    return r.size() < ncol && k + 1 == r.size();
                };
                std::vector<float> natural(ncol, 0.f), minW(ncol, 0.f);
                for (const auto& r : rows)
                    for (std::size_t k = 0; k < r.size(); ++k) {
                        if (spans(r, k)) continue;
                        const bool head = k == 0;
                        const std::string_view cell = r[k];
                        natural[k] = std::max(natural[k], m.measure(cell, head, false));
                        std::size_t ws = 0;
                        while (ws <= cell.size()) {
                            std::size_t sp = cell.find(' ', ws);
                            if (sp == std::string_view::npos) sp = cell.size();
                            if (sp > ws)
                                minW[k] = std::max(minW[k], m.measure(cell.substr(ws, sp - ws), head, false));
                            if (sp == cell.size()) break;
                            ws = sp + 1;
                        }
                    }
                for (std::size_t k = 0; k < ncol; ++k)
                    minW[k] = std::max(minW[k], std::min(natural[k], kTableMinCol));

                std::vector<float> colW(ncol, 0.f);
                std::vector<char> done(ncol, 0);
                float rest = avail;
                std::size_t left = ncol;
                // Les courtes d'abord : une colonne qui tient dans sa part du reste
                // garde sa largeur, et sa part non prise revient aux autres.
                for (bool again = true; again && left > 0;) {
                    again = false;
                    const float share = rest / static_cast<float>(left);
                    for (std::size_t k = 0; k < ncol; ++k)
                        if (!done[k] && natural[k] <= share) {
                            colW[k] = natural[k]; rest -= natural[k]; done[k] = 1; --left; again = true;
                        }
                }
                // Les longues : au prorata de leur longueur, chacune a son minimum.
                const auto longSum = [&] {
                    float s = 0.f;
                    for (std::size_t k = 0; k < ncol; ++k) if (!done[k]) s += natural[k];
                    return std::max(1.f, s);
                };
                for (bool again = true; again && left > 0;) {
                    again = false;
                    const float sum = longSum(), pool = std::max(0.f, rest);
                    for (std::size_t k = 0; k < ncol; ++k)
                        if (!done[k] && pool * natural[k] / sum < minW[k]) {
                            colW[k] = minW[k]; rest -= minW[k]; done[k] = 1; --left; again = true;
                        }
                }
                if (left > 0) {
                    const float sum = longSum(), pool = std::max(0.f, rest);
                    for (std::size_t k = 0; k < ncol; ++k)
                        if (!done[k]) colW[k] = pool * natural[k] / sum;
                }
                float total = 0.f;
                for (const float w : colW) total += w;
                if (total > avail) {
                    // Trop etroit pour les minimums : tout se resserre, et un mot plus
                    // large que sa colonne y est coupe plutot que de deborder.
                    for (auto& w : colW) w *= avail / total;
                } else {
                    colW[ncol - 1] += avail - total;
                }
                std::vector<float> xs(ncol, kPadX + kIn);
                for (std::size_t k = 1; k < ncol; ++k) xs[k] = xs[k - 1] + colW[k - 1] + kGap;

                const float top = c.y;
                const std::size_t decorAt = out.decor.size();
                // 1.11 (T2) : `label` "touches" (la page des raccourcis) : dans la premiere
                // colonne, chaque [touche] est dessinee en touche de clavier ; le reste
                // ("+", "ou") reste du texte, en gris.
                const bool keyCaps = b.label == "touches";
                const float capH = lineF(m, HelpFont::Caption) + 2 * kPillPadY;
                c.advance(8.f);
                for (std::size_t ri = 0; ri < rows.size(); ++ri) {
                    const auto& r = rows[ri];
                    const float rowTop = c.y;
                    const std::size_t rowDecorAt = out.decor.size();
                    float bottom = rowTop + m.lineHeight;
                    for (std::size_t k = 0; k < r.size(); ++k) {
                        if (r[k].empty()) continue;
                        c.y = rowTop;
                        const bool head = k == 0;
                        if (head && keyCaps) {
                            const std::string_view cell = r[k];
                            const float y0 = rowTop + (m.lineHeight - capH) / 2.f;
                            const float ty = y0 + kPillPadY;
                            float x = xs[k];
                            std::size_t at = 0;
                            while (at < cell.size()) {
                                const std::size_t open = cell.find('[', at);
                                const std::size_t close = open == std::string_view::npos ? open : cell.find(']', open + 1);
                                const std::size_t textEnd = close == std::string_view::npos ? cell.size() : open;
                                if (textEnd > at) {
                                    // Le texte entre deux touches : ses espaces ne font que de la place.
                                    std::string_view t = cell.substr(at, textEnd - at);
                                    const std::size_t a0 = t.find_first_not_of(' ');
                                    if (a0 != std::string_view::npos) {
                                        const std::size_t a1 = t.find_last_not_of(' ');
                                        x += 4.f;
                                        const std::string mid(t.substr(a0, a1 - a0 + 1));
                                        c.runF(x, ty, mid, HelpRole::Muted, HelpFont::Caption);
                                        x += measureF(m, mid, HelpFont::Caption) + 4.f;
                                    }
                                }
                                if (close == std::string_view::npos) break;
                                const std::string key(cell.substr(open + 1, close - open - 1));
                                const float kw = std::max(capH, measureF(m, key, HelpFont::Caption) + 2 * 7.f);
                                c.decor(HelpDecorKind::Pill, {x, y0, kw, capH}, true);
                                c.runF(x + (kw - measureF(m, key, HelpFont::Caption)) / 2.f, ty, key, HelpRole::Body,
                                       HelpFont::Caption);
                                x += kw;
                                at = close + 1;
                            }
                            bottom = std::max(bottom, y0 + capH + 2.f);
                            continue;
                        }
                        const float w = spans(r, k) ? kPadX + content - kIn - xs[k] : colW[k];
                        for (const auto& l : wrapCell(r[k], w, m, head)) {
                            if (!l.empty()) {
                                c.run(xs[k], l, head ? HelpRole::Term : HelpRole::Body, head, false);
                                if (head) out.runs.back().fauxBold = true;
                            }
                            c.advance(m.lineHeight);
                        }
                        bottom = std::max(bottom, c.y);
                    }
                    c.y = bottom;
                    if (ri % 2 == 1)
                        out.decor.insert(out.decor.begin() + static_cast<std::ptrdiff_t>(rowDecorAt),
                                         HelpDecor{HelpDecorKind::TableStripe,
                                                   {kPadX + 1.f, rowTop - 4.f,
                                                    std::max(0.f, content - 2.f),
                                                    bottom - rowTop + 8.f}});
                    c.advance(8.f);
                }
                out.decor.insert(out.decor.begin() + static_cast<std::ptrdiff_t>(decorAt),
                                 HelpDecor{HelpDecorKind::Card, {kPadX, top, content, c.y - top}});
                c.advance(kCardGap + 4.f);
                break;
            }

            case HelpBlockKind::Links: {
                if (b.links.empty()) break;
                if (!b.label.empty()) {
                    c.runF(kPadX, c.y, b.label, HelpRole::Muted, HelpFont::Caption);
                    c.advance(lineF(m, HelpFont::Caption) + 6.f);
                }
                pillRow(c, kPadX, content, b.links, true);
                c.advance(14.f);
                break;
            }

            case HelpBlockKind::Ring: {
                // LA COUVERTURE EN ANNEAU. La barre disait "combien" ; l'anneau
                // le dit aussi, et libere la ligne pour dire LESQUELS : chaque
                // parametre sans aide est une puce, et la puce mene a son champ.
                const float top = c.y;
                const float size = std::min(kRingSize, std::max(8.f, content / 4.f));
                c.decor(HelpDecorKind::RingTrack, {kPadX, top, size, size});
                c.decor(HelpDecorKind::RingFill, {kPadX, top, size, size},
                        b.value >= 0.999f, kNoColor, std::clamp(b.value, 0.f, 1.f));
                // La fraction, au centre de l'anneau.
                if (!b.label.empty()) {
                    const float lw = measureF(m, b.label, HelpFont::Caption);
                    c.runF(kPadX + (size - lw) / 2.f,
                           top + (size - lineF(m, HelpFont::Caption)) / 2.f,
                           b.label, HelpRole::Title, HelpFont::Caption, kNoColor, true);
                }
                const float x = kPadX + size + 20.f;
                const float w = std::max(1.f, content - size - 20.f);
                c.advance(2.f);
                if (!b.text.empty()) c.paragraph(x, w, b.text, HelpRole::Body);
                c.advance(6.f);
                if (!b.links.empty()) pillRow(c, x, w, b.links, true);
                c.y = std::max(c.y, top + size);
                c.advance(16.f);
                break;
            }

            case HelpBlockKind::Empty: {
                // L'ETAT VIDE. Un grand pictogramme pale, une phrase, et le
                // bouton qui resout - jamais un blanc qu'on prend pour un bug.
                c.advance(40.f);
                const float icon = 64.f;
                c.decor(HelpDecorKind::Glyph, {kPadX + (content - icon) / 2.f, c.y, icon, icon},
                        true, kNoColor, static_cast<float>(b.icon == Icon::None ? Icon::Info : b.icon));
                c.advance(icon + 16.f);
                const auto lines = wrapWith(b.text, std::min(content, 480.f),
                    [&](std::string_view s) { return measureF(m, s, HelpFont::Lead); });
                for (const auto& l : lines) {
                    const float lw = measureF(m, l, HelpFont::Lead);
                    c.runF(kPadX + (content - lw) / 2.f, c.y, l, HelpRole::Subtitle, HelpFont::Lead);
                    c.advance(lineF(m, HelpFont::Lead));
                }
                if (!b.links.empty()) {
                    c.advance(16.f);
                    float total = 0.f;
                    for (const auto& l : b.links)
                        total += measureF(m, l.text, HelpFont::Caption) + 2 * kPillPadX + kPillGap;
                    pillRow(c, kPadX + std::max(0.f, (content - total) / 2.f), content, b.links, true);
                }
                c.advance(24.f);
                break;
            }
        }
    }

    closeFrame();
    out.height = c.y + kPadBottom;
    return out;
}

// ---------------------------------------------------------------------------
HelpArticleView::HelpArticleView(std::string id) : Widget(std::move(id)) {
    setFocusPolicy(true);
}

void HelpArticleView::setArticle(HelpArticle a) {
    article_     = std::move(a);
    scrollY_     = 0.f;
    scrollTarget_ = 0.f;
    dirtyLayout_ = true;
    builtWidth_  = -1.f;
    hoveredSpot_     = -1;
    shownAt_     = -1.0;       // le fondu part de la premiere image qui le peint
    invalidate();
}

void HelpArticleView::replaceArticle(HelpArticle a) {
    // 1.10 : le meme article, autrement (une autre notation des exemples) : ni
    // retour en haut, ni fondu - le lecteur reste ou il lisait.
    article_     = std::move(a);
    dirtyLayout_ = true;
    builtWidth_  = -1.f;
    hoveredSpot_ = -1;
    invalidate();
}

void HelpArticleView::scrollToTop() { scrollY_ = scrollTarget_ = 0.f; invalidate(); }

void HelpArticleView::scrollTo(float y) {
    scrollTarget_ = std::clamp(y, 0.f, maxScroll());
    invalidate();
}

void HelpArticleView::onLayout() { dirtyLayout_ = true; }

SizeHint HelpArticleView::sizeHint() const {
    SizeHint h;
    h.minimum   = {240.f, 120.f};
    h.preferred = {520.f, 360.f};
    h.stretchX = h.stretchY = 1.f;
    return h;
}

float HelpArticleView::maxScroll() const noexcept {
    // 1.11 (T2) : si seule la marge du bas (kPadBottom, du blanc) depasse, il
    // n'y a rien a montrer dessous : ni defilement, ni barre. C'est ce qui laisse
    // le centre d'aide couper son en-tete juste sous la carte du tutoriel.
    const float h = contentRect().h;
    if (layout_.height - kPadBottom <= h + 1.f) return 0.f;
    return std::max(0.f, layout_.height - h);
}

float HelpArticleView::tocWidth(float width) const noexcept {
    // Le sommaire ne vient que s'il a de la place ET quelque chose a dire :
    // deux intertitres au moins. Un sommaire d'une ligne est une decoration.
    std::size_t headings = 0;
    for (const auto& b : article_.blocks) headings += b.kind == HelpBlockKind::Heading;
    return (width >= kTocMin && headings >= 2) ? kTocW + 16.f : 0.f;
}

void HelpArticleView::rebuild(const PaintContext* ctx) {
    const float full = contentRect().w;
    const float w = full - tocWidth(full);
    // Une mise en page faite sur l'ESTIMATION (sept pixels par caractere, avant
    // la premiere image) est refaite des qu'un renderer est la : sinon un coup
    // de molette entre setArticle() et la peinture figeait des lignes coupees
    // au mauvais endroit jusqu'au prochain redimensionnement.
    const bool upgrade = ctx != nullptr && estimated_;
    if (!dirtyLayout_ && !upgrade && std::abs(w - builtWidth_) < 0.5f) return;

    HelpMetrics m;
    if (ctx) {
        const auto& r  = ctx->r;
        const auto& th = ctx->theme;
        const auto fontOf = [&th](HelpFont f) {
            switch (f) {
                case HelpFont::Title:   return th.font.title;
                case HelpFont::Lead:    return th.font.lead;
                case HelpFont::Heading: return th.font.heading;
                case HelpFont::Caption: return th.font.caption;
                case HelpFont::Mono:    return th.font.mono;
                default:                return th.font.ui;
            }
        };
        m.measure = [&r, &th](std::string_view t, bool bold, bool mono) {
            return r.measure(t, mono ? th.font.mono : (bold ? th.font.uiBold : th.font.ui)).width;
        };
        m.measureFont = [&r, fontOf](std::string_view t, HelpFont f) {
            return r.measure(t, fontOf(f)).width;
        };
        m.lineHeightFont = [&r, fontOf](HelpFont f) { return r.lineHeight(fontOf(f)) + 2.f; };
        m.lineHeight     = r.lineHeight(th.font.ui) + 2.f;
        m.monoLineHeight = r.lineHeight(th.font.mono) + 2.f;
        layout_ = layoutArticle(article_, w, m);
        estimated_ = false;
    } else {
        // Hors peinture, on se rabat sur une estimation : approximative mais
        // jamais absurde, elle permet a sizeHint() et au clamp du defilement de
        // repondre avant la premiere image.
        m.measure = [](std::string_view t, bool, bool) {
            return static_cast<float>(t.size()) * 7.f;
        };
        layout_ = layoutArticle(article_, w, m);
        estimated_ = true;
    }
    builtWidth_  = w;
    dirtyLayout_ = false;
    scrollY_      = std::clamp(scrollY_, 0.f, maxScroll());
    scrollTarget_ = std::clamp(scrollTarget_, 0.f, maxScroll());
}

void HelpArticleView::onPaint(const PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto  r  = contentRect();
    ctx.r.fillRect(bounds(), th.color.panelBg);
    now_ = ctx.time;

    rebuild(&ctx);
    if (layout_.runs.empty() && layout_.decor.empty()) return;

    // LE DEFILEMENT DOUX. La molette pose une cible, chaque image en parcourt
    // une fraction. Sauter de cinq cents pixels d'un coup fait perdre la ligne
    // qu'on lisait ; glisser la laisse suivre.
    if (std::abs(scrollTarget_ - scrollY_) > 0.5f) {
        scrollY_ += (scrollTarget_ - scrollY_) * th.motion.scrollEase;
        invalidate();
    } else {
        scrollY_ = scrollTarget_;
    }

    const float top = r.y - scrollY_;
    const bool  dark = th.isDark();

    auto roleColor = [&th](HelpRole role) {
        switch (role) {
            case HelpRole::Title:    return th.color.text;
            case HelpRole::Subtitle: return th.color.textMuted;
            case HelpRole::Lead:     return th.color.text;
            case HelpRole::Heading:  return th.color.accent;
            case HelpRole::Muted:    return th.color.textMuted;
            case HelpRole::Mono:     return th.color.text;
            case HelpRole::Accent:   return th.color.accent;
            case HelpRole::Warning:  return th.onSurface(th.color.warning);
            case HelpRole::Error:    return th.onSurface(th.color.error);
            case HelpRole::Term:     return th.color.text;
            // Le bloc de code se distingue par son fond et sa coloration, pas
            // par une couleur de texte : du rouge sur une page d'aide veut dire
            // "probleme", et un exemple qui marche n'en est pas un.
            case HelpRole::Code:     return th.color.text;
            case HelpRole::Custom:   return th.color.text;
            case HelpRole::Novelty:  return gfx::Color{255, 255, 255, 255};
            case HelpRole::Body:     break;
        }
        return th.color.text;
    };
    auto severityColor = [&th](int sev) {
        return sev == 2 ? th.color.error : sev == 1 ? th.color.warning : th.color.info;
    };

    ctx.r.pushClip(r);
    for (const auto& d : layout_.decor) {
        const gfx::Rect rc{r.x + d.rect.x, top + d.rect.y, d.rect.w, d.rect.h};
        if (rc.bottom() < r.y || rc.y > r.bottom()) continue;
        switch (d.kind) {
            case HelpDecorKind::CodePanel:
                ctx.r.fillRoundedRect(rc, th.brand.cardBorder, 8.f);
                ctx.r.fillRoundedRect({rc.x + 1.f, rc.y + 1.f, rc.w - 2.f, rc.h - 2.f},
                                      th.brand.codeBg, 7.f);
                break;
            case HelpDecorKind::Gutter:
                ctx.r.line({rc.right(), rc.y + 8.f}, {rc.right(), rc.bottom() - 8.f},
                           th.brand.cardBorder, 1.f);
                break;
            case HelpDecorKind::Separator:
                ctx.r.fillRect(rc, d.muted ? th.color.warning : th.color.border);
                break;
            case HelpDecorKind::HeadingRule:
                ctx.r.fillRect(rc, th.color.border);
                break;
            case HelpDecorKind::Badge:
                ctx.r.fillRoundedRect(rc, d.muted ? th.color.rowAltBg
                                                  : th.color.warning.withAlpha(48),
                                      rc.h / 2.f);
                break;
            case HelpDecorKind::MeterTrack:
                ctx.r.fillRoundedRect(rc, th.color.rowAltBg, rc.h / 2.f);
                break;
            case HelpDecorKind::MeterFill:
                if (rc.w > 0.5f)
                    ctx.r.fillRoundedRect(rc, d.muted ? th.color.ok : th.color.accent,
                                          rc.h / 2.f);
                break;
            case HelpDecorKind::TermMarker:
                ctx.r.fillRoundedRect(rc, d.muted ? th.color.borderStrong : th.color.ok,
                                      rc.h / 2.f);
                break;
            case HelpDecorKind::Card: {
                // L'elevation, dessinee a la main : une ombre decalee de deux
                // pixels, une bordure, une surface. Le renderer ne sait pas
                // flouter, et a cette taille ca ne se voit pas.
                ctx.r.fillRoundedRect({rc.x, rc.y + 2.f, rc.w, rc.h}, th.brand.cardShadow, 10.f);
                ctx.r.fillRoundedRect(rc, d.muted ? th.color.border : th.brand.cardBorder, 10.f);
                ctx.r.fillRoundedRect({rc.x + 1.f, rc.y + 1.f, rc.w - 2.f, rc.h - 2.f},
                                      th.brand.card, 9.f);
                break;
            }
            case HelpDecorKind::Pill: {
                if (d.color.a == 0 && d.tone == Tone::None) {
                    ctx.r.fillRoundedRect(rc, th.color.border, rc.h / 2.f);
                    ctx.r.fillRoundedRect({rc.x + 1.f, rc.y + 1.f, rc.w - 2.f, rc.h - 2.f},
                                          d.muted ? th.brand.card : th.color.rowAltBg,
                                          rc.h / 2.f - 1.f);
                } else {
                    ctx.r.fillRoundedRect(rc, th.tone(d.tone, d.color).withAlpha(dark ? 46 : 30),
                                          rc.h / 2.f);
                }
                break;
            }
            case HelpDecorKind::Stripe:
                ctx.r.fillRoundedRect({rc.x + 1.f, rc.y + 10.f, rc.w, std::max(0.f, rc.h - 20.f)},
                                      th.tone(d.tone, d.color.a == 0 ? th.color.accent : d.color),
                                      2.f);
                break;
            case HelpDecorKind::CalloutBox: {
                const auto c = severityColor(static_cast<int>(d.value));
                ctx.r.fillRoundedRect(rc, c.withAlpha(dark ? 30 : 22), 8.f);
                ctx.r.fillRoundedRect({rc.x, rc.y, 3.f, rc.h}, c, 1.5f);
                break;
            }
            case HelpDecorKind::RingTrack:
            case HelpDecorKind::RingFill: {
                // Un anneau en segments : le renderer n'a pas d'arc, et
                // soixante-quatre segments suffisent a ce qu'on ne les voie pas.
                const float cx = rc.x + rc.w / 2.f, cy = rc.y + rc.h / 2.f;
                const float rad = rc.w / 2.f - 4.f;
                const bool fill = d.kind == HelpDecorKind::RingFill;
                const float frac = fill ? d.value : 1.f;
                if (frac <= 0.f) break;            // rien de fait : pas meme un point
                const int segs = std::max(1, static_cast<int>(64.f * frac));
                const float n = static_cast<float>(segs);
                // L'anneau prend la couleur de son palier, comme la pastille de
                // l'arbre : vert complet, ambre a moitie, rouge en dessous.
                const auto colour = fill ? th.coverage(d.value) : th.color.rowAltBg;
                constexpr float kTau = 6.2831853f;
                for (int i = 0; i < segs; ++i) {
                    const float a0 = -kTau / 4.f + kTau * frac * static_cast<float>(i) / n;
                    const float a1 = -kTau / 4.f + kTau * frac * static_cast<float>(i + 1) / n;
                    ctx.r.line({cx + rad * std::cos(a0), cy + rad * std::sin(a0)},
                               {cx + rad * std::cos(a1), cy + rad * std::sin(a1)}, colour, 6.f);
                }
                break;
            }
            case HelpDecorKind::HeroBand: {
                const auto c = th.tone(d.tone, d.color.a == 0 ? th.color.accent : d.color);
                ctx.r.fillRoundedRect(rc, c.withAlpha(dark ? 70 : 60), 12.f);
                ctx.r.fillRoundedRect({rc.x + 1.f, rc.y + 1.f, rc.w - 2.f, rc.h - 2.f},
                                      th.brand.card, 11.f);
                ctx.r.fillRoundedRect({rc.x + 1.f, rc.y + 1.f, rc.w - 2.f, rc.h - 2.f},
                                      c.withAlpha(dark ? 30 : 18), 11.f);
                break;
            }
            case HelpDecorKind::TableStripe:
                ctx.r.fillRect(rc, th.color.rowAltBg);
                break;
            case HelpDecorKind::NoveltyFrame: {
                // 1.10 : l'orange des nouveautes - un voile pale et un trait.
                const auto o = novelty::orange(th);
                ctx.r.fillRoundedRect(rc, o.withAlpha(dark ? 26 : 16), 8.f);
                ctx.r.strokeRect(rc, o, 2.f);
                break;
            }
            case HelpDecorKind::NoveltyPill:
                ctx.r.fillRoundedRect(rc, novelty::orange(th), rc.h / 2.f);
                break;
            case HelpDecorKind::Glyph: {
                const auto icon = static_cast<Icon>(static_cast<int>(d.value));
                // Le pictogramme d'un encadre prend la couleur de sa gravite ;
                // celui d'un etat vide reste pale.
                const auto colour = d.tone != Tone::None ? th.onSurface(th.tone(d.tone, th.color.textMuted))
                                  : d.muted ? th.color.textDisabled : th.color.textMuted;
                drawIcon(ctx.r, icon, rc, colour);
                break;
            }
        }
    }

    // Le survol d'une zone cliquable, sous le texte : un fond, pas un cadre.
    if (hoveredSpot_ >= 0 && hoveredSpot_ < static_cast<int>(layout_.hotspots.size())) {
        const auto& h = layout_.hotspots[static_cast<std::size_t>(hoveredSpot_)];
        const gfx::Rect rc{r.x + h.rect.x - 2.f, top + h.rect.y - 1.f, h.rect.w + 4.f, h.rect.h + 2.f};
        ctx.r.fillRoundedRect(rc, th.color.accent.withAlpha(dark ? 60 : 40), rc.h / 2.f);
    }

    for (const auto& run : layout_.runs) {
        const float y = top + run.y;
        if (y + 40.f < r.y || y > r.bottom()) continue;
        gfx::FontId font = th.font.ui;
        switch (run.font) {
            case HelpFont::Title:   font = th.font.title; break;
            case HelpFont::Lead:    font = th.font.lead; break;
            case HelpFont::Heading: font = th.font.heading; break;
            case HelpFont::Caption: font = th.font.caption; break;
            case HelpFont::Mono:    font = th.font.mono; break;
            default: font = run.mono ? th.font.mono : (run.bold ? th.font.uiBold : th.font.ui);
        }
        gfx::Color colour = roleColor(run.role);
        if (run.syntax != 0xFF) colour = ui::colorFor(static_cast<TokenClass>(run.syntax), th.color);
        else if (run.role == HelpRole::Custom && (run.color.a != 0 || run.tone != Tone::None))
            colour = th.onSurface(th.tone(run.tone, run.color));
        ctx.r.drawText({r.x + run.x, y}, run.text, font, colour);
        // Le faux gras : le meme texte, un pixel plus loin. Il n'y a pas de
        // face grasse, et un titre qui n'a que sa taille pour se distinguer
        // se confond avec un paragraphe au premier coup d'oeil.
        if (run.fauxBold) ctx.r.drawText({r.x + run.x + 0.7f, y}, run.text, font, colour);
    }

    // "Copie" : le bouton repond sur place pendant un peu plus d'une seconde.
    // Un presse-papier ne se voit pas ; sans ce retour, on clique deux fois.
    if (!copiedTarget_.empty() && ctx.time - copiedAt_ < 1.4) {
        for (const auto& h : layout_.hotspots) {
            if (h.target != copiedTarget_) continue;
            const float ic0 = std::min(12.f, h.rect.h - 8.f);
            const float need = 8.f + ic0 + 4.f
                             + ctx.r.measure("Copi\xC3\xA9", th.font.caption).width + 10.f;
            const float bw = std::max(h.rect.w, need);
            const gfx::Rect rc{r.x + h.rect.x + h.rect.w - bw, top + h.rect.y, bw, h.rect.h};
            ctx.r.fillRoundedRect(rc, th.brand.card, rc.h / 2.f);
            ctx.r.fillRoundedRect(rc, th.color.ok.withAlpha(dark ? 60 : 40), rc.h / 2.f);
            const float ic = std::min(12.f, rc.h - 8.f);
            drawIcon(ctx.r, Icon::Ok, {rc.x + 8.f, rc.y + (rc.h - ic) / 2.f, ic, ic},
                     th.onSurface(th.color.ok));
            ctx.r.drawText({rc.x + 8.f + ic + 4.f,
                            rc.y + (rc.h - ctx.r.lineHeight(th.font.caption)) / 2.f},
                           "Copi\xC3\xA9", th.font.caption, th.onSurface(th.color.ok));
        }
        invalidate();
    }
    ctx.r.popClip();

    // ---- le sommaire flottant -------------------------------------------------
    tocRects_.clear();
    const float tw = tocWidth(r.w);
    if (tw > 0.f && !layout_.sections.empty()) {
        const float x = r.right() - tw + 8.f;
        float y = r.y + 16.f;
        ctx.r.drawText({x, y}, "Sur cette page", th.font.caption, th.color.textMuted);
        y += ctx.r.lineHeight(th.font.caption) + 10.f;
        // La section courante : la derniere dont l'intertitre est passe sous le
        // haut de la vue. C'est ce qui dit ou l'on est dans une longue page.
        std::size_t current = 0;
        for (std::size_t i = 0; i < layout_.sections.size(); ++i)
            if (layout_.sections[i].y <= scrollY_ + 40.f) current = i;
        const float lh = ctx.r.lineHeight(th.font.caption) + 10.f;
        ctx.r.fillRect({x - 8.f, y, 2.f, lh * static_cast<float>(layout_.sections.size())},
                       th.color.border);
        for (std::size_t i = 0; i < layout_.sections.size(); ++i) {
            const gfx::Rect row{x - 8.f, y, tw - 16.f, lh};
            tocRects_.push_back(row);
            const bool on = i == current;
            if (on) ctx.r.fillRect({x - 8.f, y, 2.f, lh}, th.color.accent);
            if (static_cast<int>(i) == hoveredToc_)
                ctx.r.fillRoundedRect({row.x + 4.f, row.y, row.w - 4.f, row.h}, th.brand.hover, 4.f);
            ctx.r.drawText({x + 4.f, y + 5.f}, layout_.sections[i].title, th.font.caption,
                           on ? th.color.accent : th.color.textMuted);
            y += lh;
        }
    }

    // La barre de defilement. Sans elle, rien ne dit qu'un article continue
    // sous le bord : on croit avoir tout lu, et la moitie des parametres est
    // en dessous. 1.11 (T2) : pas pour la seule marge du bas (maxScroll).
    // 1.11.4 : elle se tire.
    if (maxScroll() > 0.f) sbar_.paint(ctx, {r.x, r.y, r.w - 2.f, r.h}, r.h + maxScroll(), r.h, scrollY_);

    // L'infobulle d'une puce : ce que designe un nom, sans avoir a l'ouvrir.
    if (hoveredSpot_ >= 0 && hoveredSpot_ < static_cast<int>(layout_.hotspots.size())) {
        const auto& tip = layout_.hotspots[static_cast<std::size_t>(hoveredSpot_)].tip;
        if (!tip.empty()) {
            const float maxW = 360.f;
            const auto lines = wrapWith(tip, maxW - 20.f, [&](std::string_view s) {
                return ctx.r.measure(s, th.font.caption).width;
            });
            float w = 0.f;
            for (const auto& l : lines) w = std::max(w, ctx.r.measure(l, th.font.caption).width);
            const float lh = ctx.r.lineHeight(th.font.caption) + 2.f;
            const float h = lh * static_cast<float>(lines.size()) + 16.f;
            float bx = std::min(mouse_.x + 14.f, r.right() - w - 28.f);
            float by = mouse_.y + 20.f;
            if (by + h > r.bottom()) by = mouse_.y - h - 8.f;
            ctx.r.fillRoundedRect({bx, by + 2.f, w + 20.f, h}, th.brand.cardShadow, 6.f);
            ctx.r.fillRoundedRect({bx, by, w + 20.f, h}, th.brand.tooltipBg, 6.f);
            float ly = by + 8.f;
            for (const auto& l : lines) {
                ctx.r.drawText({bx + 10.f, ly}, l, th.font.caption, th.brand.tooltipText);
                ly += lh;
            }
        }
    }

    // LE FONDU D'ENTREE. 140 ms : assez pour que le changement de page se
    // voie comme un changement, trop peu pour qu'on l'attende.
    if (shownAt_ < 0.0) shownAt_ = ctx.time;
    const float since = static_cast<float>((ctx.time - shownAt_) * 1000.0);
    if (since < th.motion.pageFadeMs) {
        const float a = 1.f - std::clamp(since / th.motion.pageFadeMs, 0.f, 1.f);
        ctx.r.fillRect(r, th.color.panelBg.withAlpha(static_cast<std::uint8_t>(a * 200.f)));
        invalidate();
    }
}

gfx::Rect HelpArticleView::hotspotsRect(std::string_view prefix) const {
    const auto r = contentRect();
    const float top = r.y - scrollY_;
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    bool any = false;
    for (const auto& h : layout_.hotspots) {
        if (h.target.compare(0, prefix.size(), prefix) != 0) {
            if (any) break;                 // la fin du groupe
            continue;
        }
        const gfx::Rect rc{r.x + h.rect.x, top + h.rect.y, h.rect.w, h.rect.h};
        if (!any && (rc.bottom() < r.y || rc.y > r.bottom())) continue;   // hors de la vue : le suivant
        if (!any) { x0 = rc.x; y0 = rc.y; x1 = rc.right(); y1 = rc.bottom(); }
        x0 = std::min(x0, rc.x); y0 = std::min(y0, rc.y);
        x1 = std::max(x1, rc.right()); y1 = std::max(y1, rc.bottom());
        any = true;
    }
    if (!any) return {};
    return gfx::Rect{x0, y0, x1 - x0, y1 - y0}.intersect(r);
}

bool HelpArticleView::revealHotspots(std::string_view prefix) {
    if (dirtyLayout_) return false;         // pas encore mise en page : a l'image suivante
    for (const auto& h : layout_.hotspots)
        if (h.target.compare(0, prefix.size(), prefix) == 0) {
            scrollTo(h.rect.y - 80.f);
            scrollY_ = scrollTarget_;       // tout de suite : la bulle la cherche a cette image
            invalidate();
            return true;
        }
    return false;
}

int HelpArticleView::hotspotAt(gfx::Point p) const noexcept {
    const auto r = contentRect();
    const float top = r.y - scrollY_;
    for (std::size_t i = 0; i < layout_.hotspots.size(); ++i) {
        const auto& h = layout_.hotspots[i].rect;
        const gfx::Rect rc{r.x + h.x, top + h.y, h.w, h.h};
        if (rc.contains(p) && r.contains(p)) return static_cast<int>(i);
    }
    return -1;
}

int HelpArticleView::tocEntryAt(gfx::Point p) const noexcept {
    for (std::size_t i = 0; i < tocRects_.size(); ++i)
        if (tocRects_[i].contains(p)) return static_cast<int>(i);
    return -1;
}

void HelpArticleView::activate(const std::string& target) {
    // Le presse-papier est traite ICI : c'est le widget qui connait le texte du
    // bloc, et l'ecran n'a pas a le rechercher pour le copier.
    if (target.rfind("copy:", 0) == 0) {
        const auto index = static_cast<std::size_t>(std::atoi(target.c_str() + 5));
        if (index < article_.blocks.size()) {
            setClipboardText(article_.blocks[index].text);
            copiedTarget_ = target;
            copiedAt_     = now_;
            invalidate();
        }
    }
    linkActivated->emit(target);
}

EventResult HelpArticleView::onEvent(const InputEvent& ev) {
    {
        float off = scrollY_;   // 1.11.4 : la barre de defilement se tire
        if (maxScroll() > 0.f && sbar_.handle(*this, ev, off)) {
            scrollY_ = std::clamp(off, 0.f, maxScroll());
            invalidate();
            return EventResult::Consumed;
        }
    }
    if (const auto* mv = std::get_if<MouseMove>(&ev)) {
        mouse_ = mv->pos;
        const int h = bounds().contains(mv->pos) ? hotspotAt(mv->pos) : -1;
        const int t = bounds().contains(mv->pos) ? tocEntryAt(mv->pos) : -1;
        if (h != hoveredSpot_ || t != hoveredToc_) {
            hoveredSpot_ = h;
            hoveredToc_ = t;
            invalidate();
        }
        return EventResult::Ignored;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bounds().contains(d->pos) || d->button != MouseButton::Left)
            return EventResult::Ignored;
        grabFocus();
        rebuild(nullptr);      // sans effet apres une peinture ; utile avant
        if (const int t = tocEntryAt(d->pos); t >= 0
                && static_cast<std::size_t>(t) < layout_.sections.size()) {
            scrollTo(layout_.sections[static_cast<std::size_t>(t)].y - 12.f);
            return EventResult::Consumed;
        }
        if (const int h = hotspotAt(d->pos); h >= 0) {
            activate(layout_.hotspots[static_cast<std::size_t>(h)].target);
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        rebuild(nullptr);
        const float before = scrollTarget_;
        scrollTarget_ = std::clamp(scrollTarget_ - w->dy * 72.f, 0.f, maxScroll());
        if (scrollTarget_ != before) { invalidate(); return EventResult::Consumed; }
        // Arrive en butee, on laisse l'evenement remonter : sinon la page
        // d'aide avalerait le defilement du panneau qui la contient.
        return EventResult::Ignored;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        rebuild(nullptr);
        const float page = std::max(48.f, contentRect().h - 32.f);
        float target = scrollTarget_;
        switch (k->key) {
            case Key::PageDown: target += page; break;
            case Key::PageUp:   target -= page; break;
            case Key::Down:     target += 48.f; break;
            case Key::Up:       target -= 48.f; break;
            case Key::Home:     target = 0.f; break;
            case Key::End:      target = maxScroll(); break;
            default:            return EventResult::Ignored;
        }
        scrollTo(target);
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

} // namespace ui
