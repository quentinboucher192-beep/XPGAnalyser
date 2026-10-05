// =============================================================================
//  export/ProgramBookPdf.cpp - 1.8.0 : le PDF de l'export lisible
// -----------------------------------------------------------------------------
//  A4 portrait. Page 1 : la couverture (le projet, l'automate, et le cycle en un
//  coup d'oeil : les sections a l'echelle de leur nombre de lignes). Puis le
//  guide de lecture et le sommaire (numeros de page, lignes cliquables), puis
//  chaque section dans l'ordre d'execution : son bandeau (rang, icone, nom,
//  unite, lignes, role), sa condition, ce qu'elle ecrit, lit, appelle, et son
//  code numerote et colore (Courier ; une ligne trop longue continue en
//  dessous, marquee). Une unite de programme ouvre une page avec ses
//  parametres. Les annexes : les blocs DFB, les variables globales. Des
//  signets (le panneau de gauche des lecteurs), en-tete et pied sur chaque
//  page. Polices standard (Helvetica, Courier) : rien a embarquer ; flux
//  compresses quand miniz est la.
// =============================================================================
#include "ProgramBook.hpp"

#include "../core/CodeIcons.hpp"
#include "DocKit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace exporter::book {

namespace {

using doc::courierWidth;
using doc::helveticaWidth;
using doc::pdfLiteral;
using doc::pdfNumber;
using doc::toWinAnsi;

constexpr double kW = 595.28, kH = 841.89;
constexpr double kLeft = 42, kRight = 42, kTop = 58, kBottom = 52;
constexpr double kContent = kW - kLeft - kRight;

struct Rgb { double r, g, b; };
constexpr Rgb kInk{0.106, 0.106, 0.106}, kBlue{0.114, 0.231, 0.361}, kTeal{0.059, 0.431, 0.431}, kPurple{0.353, 0.243, 0.549},
              kGray{0.47, 0.47, 0.47}, kLight{0.62, 0.62, 0.62}, kRed{0.769, 0.192, 0.169}, kCond{0.541, 0.353, 0.0},
              kComment{0.416, 0.451, 0.49}, kString{0.639, 0.082, 0.082}, kWhite{1, 1, 1}, kRule{0.82, 0.82, 0.82}, kHead{0.906, 0.929, 0.953};

enum Font : int { Helv = 1, HelvBold, HelvOblique, Cour, CourBold, CourOblique };

std::string rgb(const Rgb& c, bool stroke) {
    return pdfNumber(c.r) + " " + pdfNumber(c.g) + " " + pdfNumber(c.b) + (stroke ? " RG " : " rg ");
}

Rgb iconRgb(int icon) {
    const auto v = core::codeicons::info(static_cast<std::size_t>(icon < 0 ? 0 : icon)).rgb;
    // Sur papier : la teinte assombrie (le catalogue est pense pour un fond sombre).
    return {((v >> 16) & 0xFF) / 255.0 * 0.72, ((v >> 8) & 0xFF) / 255.0 * 0.72, (v & 0xFF) / 255.0 * 0.72};
}

struct Link { double x, y, w, h; std::size_t targetPage; double targetY; };

struct Page {
    std::string       ops;
    std::vector<Link> links;
    std::string       continued;      // "SFC_ManuA (suite)" : repris en haut de la page suivante
};

struct Mark {
    std::string title;                // Windows-1252
    int         level{0};
    std::size_t page{0};
    double      y{0};
};

class Layout {
public:
    std::vector<Page> pages;
    std::vector<Mark> marks;
    double            y{kTop};        // depuis le haut
    std::size_t       base{0};        // numero (0...) de la premiere page de ce morceau dans le document

    void newPage() {
        pages.push_back({});
        y = kTop;
    }
    [[nodiscard]] std::size_t page() const { return base + pages.size() - 1; }
    [[nodiscard]] bool fits(double h) const { return y + h <= kH - kBottom; }
    void need(double h) {
        if (pages.empty() || !fits(h)) newPage();
    }
    std::string& ops() { return pages.back().ops; }

    void text(double x, double baseline, std::string_view win, Font f, double size, const Rgb& c) {
        if (win.empty()) return;
        ops() += "BT /F" + std::to_string(static_cast<int>(f)) + " " + pdfNumber(size) + " Tf " + rgb(c, false) + pdfNumber(x) + " "
               + pdfNumber(kH - baseline) + " Td " + pdfLiteral(win) + " Tj ET\n";
    }
    void rect(double x, double top, double w, double h, const Rgb& fill) {
        ops() += rgb(fill, false) + pdfNumber(x) + " " + pdfNumber(kH - top - h) + " " + pdfNumber(w) + " " + pdfNumber(h) + " re f\n";
    }
    void line(double x1, double y1, double x2, double y2, const Rgb& c, double width) {
        ops() += pdfNumber(width) + " w " + rgb(c, true) + pdfNumber(x1) + " " + pdfNumber(kH - y1) + " m " + pdfNumber(x2) + " " + pdfNumber(kH - y2) + " l S\n";
    }
    void glyph(int icon, double x, double top, double size, const Rgb& c) {
        if (icon < 0) return;
        const double k = size / 16.0;
        std::string& o = ops();
        o += "q 1 J 1 j " + pdfNumber(std::max(0.4, 1.35 * k)) + " w " + rgb(c, true) + rgb(c, false);
        for (const auto& path : core::codeicons::glyph(static_cast<std::size_t>(icon))) {
            if (path.points.size() < 2) continue;
            for (std::size_t i = 0; i < path.points.size(); ++i) {
                const auto& p = path.points[i];
                o += pdfNumber(x + p.x * k) + " " + pdfNumber(kH - (top + p.y * k)) + (i == 0 ? " m " : " l ");
            }
            if (path.closed || path.filled) o += "h ";
            o += path.filled ? "f " : "S ";
        }
        o += "Q\n";
    }
    void mark(std::string title, int level) { marks.push_back({toWinAnsi(title), level, page(), y}); }
};

// Un texte (Windows-1252) coupe en lignes de `width` points.
std::vector<std::string> wrap(const std::string& win, double size, bool bold, double width) {
    std::vector<std::string> out;
    std::string cur;
    std::size_t i = 0;
    while (i < win.size()) {
        std::size_t j = win.find(' ', i);
        if (j == std::string::npos) j = win.size();
        const std::string word = win.substr(i, j - i);
        const std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && helveticaWidth(tryLine, size, bold) > width) {
            out.push_back(cur);
            cur = word;
        } else {
            cur = tryLine;
        }
        i = j + 1;
    }
    if (!cur.empty() || out.empty()) out.push_back(cur);
    return out;
}

std::string fit(const std::string& win, double size, bool bold, double width) {
    if (helveticaWidth(win, size, bold) <= width) return win;
    std::string out;
    for (const char c : win) {
        if (helveticaWidth(out + c + "\x85", size, bold) > width) break;
        out += c;
    }
    return out + "\x85";
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
}

// ---- le code : Courier, colore, coupe a `maxChars` -------------------------------
constexpr double kCodeSize = 7.6, kCodeLead = 9.7;

void codeLine(Layout& L, const std::vector<Run>& runs, double x, double baseline) {
    double cx = x;
    for (const auto& r : runs) {
        const auto win = toWinAnsi(r.text);
        Font f = Cour;
        Rgb c = kInk;
        switch (r.kind) {
            case Run::Keyword: f = CourBold; c = kBlue; break;
            case Run::Comment: f = CourOblique; c = kComment; break;
            case Run::String: c = kString; break;
            case Run::Address: c = kCond; break;
            default: break;
        }
        L.text(cx, baseline, win, f, kCodeSize, c);
        cx += courierWidth(win.size(), kCodeSize);
    }
}

// Coupe une ligne coloree en morceaux de `maxChars` caracteres (Windows-1252).
std::vector<std::vector<Run>> splitRuns(const std::vector<Run>& runs, std::size_t maxChars) {
    std::vector<std::vector<Run>> out(1);
    std::size_t used = 0;
    for (const auto& r : runs) {
        // On garde le texte UTF-8 de chaque morceau (un caractere = une colonne).
        std::string utf;
        std::size_t k = 0;
        while (k < r.text.size()) {
            std::size_t len = 1;
            const auto c = static_cast<unsigned char>(r.text[k]);
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            if (used == maxChars) {
                if (!utf.empty()) out.back().push_back({utf, r.kind});
                utf.clear();
                out.emplace_back();
                used = 0;
            }
            utf += r.text.substr(k, len);
            k += len;
            ++used;
        }
        if (!utf.empty()) out.back().push_back({utf, r.kind});
    }
    return out;
}

void pageHeaders(std::vector<Page>& pages, const Book& b, std::size_t total) {
    for (std::size_t i = 1; i < pages.size(); ++i) {
        Layout L;
        L.pages.push_back({});
        const auto head = toWinAnsi(b.project + " \xC2\xB7 programme de l'automate" + (b.scopeLabel == "Tout le programme" ? std::string{} : " (" + b.scopeLabel + ")"));
        L.text(kLeft, 36, head, Helv, 7.5, kGray);
        const auto app = toWinAnsi(b.app);
        L.text(kW - kRight - helveticaWidth(app, 7.5, false), 36, app, Helv, 7.5, kGray);
        L.line(kLeft, 40, kW - kRight, 40, kRule, 0.5);
        const auto when = toWinAnsi("Export\xC3\xA9 le " + b.exportedAt);
        L.text(kLeft, kH - 30, when, Helv, 7.5, kGray);
        const auto num = toWinAnsi("Page " + std::to_string(i + 1) + " / " + std::to_string(total));
        L.text(kW - kRight - helveticaWidth(num, 7.5, false), kH - 30, num, Helv, 7.5, kGray);
        if (!pages[i].continued.empty()) {
            // Un morceau repris : le rappeler en haut, discret.
            L.text(kLeft, 51, toWinAnsi(pages[i].continued), HelvOblique, 7, kLight);
        }
        pages[i].ops = L.pages.front().ops + pages[i].ops;
    }
}

struct Where { std::size_t page{0}; double y{0}; };

std::string ownerColorKey(const Section& s) { return s.inUnit ? s.owner : std::string{}; }

} // namespace

std::vector<std::uint8_t> toPdf(const Book& b, const Options& o) {
    const double wText = kContent;
    // ---- les couleurs du ruban : la tache en bleu, chaque unite la sienne ----
    static const Rgb kUnitColors[] = {{0.059, 0.431, 0.431}, {0.18, 0.545, 0.341}, {0.71, 0.396, 0.114}, {0.482, 0.369, 0.655}, {0.6, 0.2, 0.3}, {0.3, 0.45, 0.2}};
    std::map<std::string, Rgb> ownerColor;
    for (const auto& u : b.units) ownerColor.emplace(u.name, kUnitColors[ownerColor.size() % 6]);

    // =============================================================== le corps
    Layout body;
    std::map<std::size_t, Where> sectionAt, unitAt, taskAt;
    Where dfbAt, varsAt;
    body.newPage();
    bool firstItem = true;
    const std::size_t maxChars = static_cast<std::size_t>((wText - 30) / (0.6 * kCodeSize));
    for (const auto& it : b.order) {
        if (it.kind == Item::Task) {
            const auto& t = b.tasks[it.index];
            if (!firstItem) body.newPage();
            taskAt[it.index] = {body.page(), body.y};
            body.mark("T\xC3\xA2" "che " + t.name, 0);
            body.rect(kLeft, body.y, wText, 22, kPurple);
            body.text(kLeft + 8, body.y + 15, toWinAnsi("T\xC3\xA2" "che " + t.name), HelvBold, 11, kWhite);
            const auto right = toWinAnsi(t.kind + " \xC2\xB7 " + std::to_string(t.sections) + " sections \xC2\xB7 " + doc::thousands(t.lines) + " lignes");
            body.text(kW - kRight - 8 - helveticaWidth(right, 8, false), body.y + 14.5, right, Helv, 8, kWhite);
            body.y += 30;
            firstItem = false;
            continue;
        }
        if (it.kind == Item::Unit) {
            const auto& u = b.units[it.index];
            if (!firstItem) body.newPage();
            unitAt[it.index] = {body.page(), body.y};
            body.mark("Unit\xC3\xA9 " + u.name, 0);
            body.rect(kLeft, body.y, wText, 34, kTeal);
            double tx = kLeft + 9;
            if (o.roles && u.icon >= 0) {
                body.glyph(u.icon, tx, body.y + 9, 16, kWhite);
                tx += 22;
            }
            body.text(tx, body.y + 15, toWinAnsi("Unit\xC3\xA9 de programme " + u.name), HelvBold, 12, kWhite);
            body.text(tx, body.y + 27, toWinAnsi("Rang " + std::to_string(u.rank) + " dans " + u.task + " \xC2\xB7 " + std::to_string(u.sections) + " sections \xC2\xB7 "
                                                 + doc::thousands(u.lines) + " lignes \xC2\xB7 " + std::to_string(u.params.size()) + " param\xC3\xA8tres \xC2\xB7 "
                                                 + std::to_string(u.locals) + " variables locales"),
                      Helv, 8, kWhite);
            body.y += 42;
            if (o.unitParams && !u.params.empty()) {
                const double cols[] = {kLeft, kLeft + 46, kLeft + 176, kLeft + 330};
                body.rect(kLeft, body.y, wText, 12, kHead);
                const char* heads[] = {"Sens", "Nom", "Type", "Re\xC3\xA7oit (variable du projet)"};
                for (int c = 0; c < 4; ++c) body.text(cols[c] + 3, body.y + 8.8, toWinAnsi(heads[c]), HelvBold, 7.5, kBlue);
                body.y += 12;
                for (const auto& p : u.params) {
                    body.need(10);
                    body.text(cols[0] + 3, body.y + 7.6, toWinAnsi(p.direction), Helv, 7.2, kInk);
                    body.text(cols[1] + 3, body.y + 7.6, fit(toWinAnsi(p.name), 7.2, false, 126), Helv, 7.2, kInk);
                    body.text(cols[2] + 3, body.y + 7.6, fit(toWinAnsi(p.type), 7.2, false, 150), Helv, 7.2, kInk);
                    body.text(cols[3] + 3, body.y + 7.6, fit(toWinAnsi(p.receives), 7.2, false, wText - 336), Helv, 7.2, kInk);
                    body.line(kLeft, body.y + 10, kW - kRight, body.y + 10, {0.9, 0.9, 0.9}, 0.4);
                    body.y += 10;
                }
                body.y += 10;
            }
            firstItem = false;
            continue;
        }
        const auto& s = b.sections[it.index];
        // Le bandeau et les premieres lignes ensemble : pas de bandeau seul en bas de page.
        const double metaH = 12 + (s.comment.empty() ? 0 : 10) + (o.access ? 30 : 0) + (s.calls.empty() ? 0 : 10);
        body.need(22 + metaH + 4 * kCodeLead);
        sectionAt[it.index] = {body.page(), body.y};
        body.mark(std::to_string(s.rank) + " \xC2\xB7 " + s.name, s.inUnit ? 1 : 0);
        body.rect(kLeft, body.y, wText, 18, kBlue);
        const auto rank = toWinAnsi(std::to_string(s.rank));
        const double rw = helveticaWidth(rank, 8, true) + 7;
        body.rect(kLeft + 5, body.y + 3.5, rw, 11, kWhite);
        body.text(kLeft + 8.5, body.y + 11.8, rank, HelvBold, 8, kBlue);
        double tx = kLeft + 10 + rw;
        if (o.roles && s.icon >= 0) {
            body.glyph(s.icon, tx, body.y + 3.5, 11, kWhite);
            tx += 15;
        }
        body.text(tx, body.y + 12.5, toWinAnsi(s.name), HelvBold, 10, kWhite);
        const auto right = toWinAnsi(s.owner + " \xC2\xB7 " + doc::thousands(s.lineCount) + " lignes" + (o.roles && s.icon >= 0 ? " \xC2\xB7 " + roleName(s.icon) : std::string{}));
        body.text(kW - kRight - 7 - helveticaWidth(right, 7.6, false), body.y + 12, right, Helv, 7.6, kWhite);
        body.y += 22;
        const auto meta = [&](const std::string& label, const std::string& value, const Rgb& color, std::size_t maxLines) {
            const auto lines = wrap(toWinAnsi(value), 7.8, false, wText - 62);
            for (std::size_t i = 0; i < lines.size() && i < maxLines; ++i) {
                body.need(10);
                if (i == 0) body.text(kLeft, body.y + 7.5, toWinAnsi(label), Helv, 7.8, kGray);
                std::string l = lines[i];
                if (i + 1 == maxLines && lines.size() > maxLines) l += " \x85";
                body.text(kLeft + 62, body.y + 7.5, l, Helv, 7.8, color);
                body.y += 10;
            }
        };
        meta("Condition", s.condition.empty() ? std::string("aucune : \xC3\xA0 chaque cycle") : "si " + s.condition, s.condition.empty() ? kInk : kCond, 2);
        if (!s.comment.empty()) meta("Commentaire", s.comment, kInk, 2);
        if (o.access) {
            meta("\xC3\x89" "crit (" + std::to_string(s.writes.size()) + ")", s.writes.empty() ? std::string("\xE2\x80\x94") : join(s.writes), kInk, 3);
            meta("Lit (" + std::to_string(s.reads.size()) + ")", s.reads.empty() ? std::string("\xE2\x80\x94") : join(s.reads), kInk, 3);
            if (!s.calls.empty()) meta("Appelle", join(s.calls), kInk, 2);
        }
        body.line(kLeft, body.y + 2, kW - kRight, body.y + 2, kRule, 0.5);
        body.y += 6;
        bool inComment = false;
        for (std::size_t i = 0; i < s.lines.size(); ++i) {
            const auto parts = splitRuns(colorize(s.lines[i], inComment), maxChars);
            for (std::size_t k = 0; k < parts.size(); ++k) {
                if (!body.fits(kCodeLead)) {
                    body.newPage();
                    body.pages.back().continued = "\xC2\xBB " + std::to_string(s.rank) + " \xC2\xB7 " + s.name + " (suite)";
                }
                const double baseline = body.y + 7;
                if (k == 0 && o.lineNumbers) {
                    const auto n = std::to_string(i + 1);
                    body.text(kLeft + 22 - courierWidth(n.size(), 7), baseline, n, Cour, 7, kLight);
                } else if (k > 0) {
                    body.text(kLeft + 16, baseline, "\xBB", Cour, 7, kLight);
                }
                codeLine(body, parts[k], kLeft + 30 + (k > 0 ? courierWidth(2, kCodeSize) : 0), baseline);
                body.y += kCodeLead;
            }
        }
        body.y += 8;
        firstItem = false;
    }
    // ---- les annexes ----
    char letter = 'A';
    if (o.dfbAppendix && !b.dfbs.empty()) {
        body.newPage();
        dfbAt = {body.page(), body.y};
        body.mark(std::string("Annexe ") + letter + " \xC2\xB7 Blocs DFB", 0);
        body.text(kLeft, body.y + 14, toWinAnsi(std::string("Annexe ") + letter++ + " : les blocs DFB (" + std::to_string(b.dfbs.size()) + ")"), HelvBold, 14, kBlue);
        body.y += 24;
        for (const auto& d : b.dfbs) {
            body.need(40 + 3 * kCodeLead);
            body.mark(d.name, 1);
            body.rect(kLeft, body.y, wText, 18, kBlue);
            double tx = kLeft + 8;
            if (o.roles && d.icon >= 0) {
                body.glyph(d.icon, tx, body.y + 3.5, 11, kWhite);
                tx += 15;
            }
            body.text(tx, body.y + 12.5, toWinAnsi(d.name + (d.version.empty() ? std::string{} : "  version " + d.version)), HelvBold, 10, kWhite);
            const auto right = toWinAnsi(std::to_string(d.inputs) + " entr\xC3\xA9" "es \xC2\xB7 " + std::to_string(d.outputs) + " sorties \xC2\xB7 " + std::to_string(d.inOuts)
                                         + " E/S \xC2\xB7 " + doc::thousands(d.lines) + " lignes");
            body.text(kW - kRight - 7 - helveticaWidth(right, 7.6, false), body.y + 12, right, Helv, 7.6, kWhite);
            body.y += 22;
            for (const auto& p : d.params) {
                body.need(10);
                body.text(kLeft, body.y + 7.5, toWinAnsi(p.direction), Helv, 7.4, kGray);
                body.text(kLeft + 46, body.y + 7.5, fit(toWinAnsi(p.name), 7.4, false, 150), Helv, 7.4, kInk);
                body.text(kLeft + 200, body.y + 7.5, fit(toWinAnsi(p.type + (p.comment.empty() ? std::string{} : "   " + p.comment)), 7.4, false, wText - 200), Helv, 7.4, kInk);
                body.y += 9.5;
            }
            for (const auto& bd : d.bodies) {
                body.need(14 + 2 * kCodeLead);
                body.text(kLeft, body.y + 10, toWinAnsi("Section " + bd.name), HelvBold, 8.5, kBlue);
                body.y += 14;
                bool inComment = false;
                for (std::size_t i = 0; i < bd.lines.size(); ++i) {
                    const auto parts = splitRuns(colorize(bd.lines[i], inComment), maxChars);
                    for (std::size_t k = 0; k < parts.size(); ++k) {
                        if (!body.fits(kCodeLead)) {
                            body.newPage();
                            body.pages.back().continued = "\xC2\xBB " + d.name + " / " + bd.name + " (suite)";
                        }
                        const double baseline = body.y + 7;
                        if (k == 0 && o.lineNumbers) {
                            const auto n = std::to_string(i + 1);
                            body.text(kLeft + 22 - courierWidth(n.size(), 7), baseline, n, Cour, 7, kLight);
                        }
                        codeLine(body, parts[k], kLeft + 30 + (k > 0 ? courierWidth(2, kCodeSize) : 0), baseline);
                        body.y += kCodeLead;
                    }
                }
                body.y += 6;
            }
            body.y += 8;
        }
    }
    if (o.variablesAppendix && !b.variables.empty()) {
        body.newPage();
        varsAt = {body.page(), body.y};
        body.mark(std::string("Annexe ") + letter + " \xC2\xB7 Variables globales", 0);
        body.text(kLeft, body.y + 14, toWinAnsi(std::string("Annexe ") + letter + " : les variables globales (" + std::to_string(b.variables.size()) + ")"), HelvBold, 14, kBlue);
        body.y += 24;
        const double cols[] = {kLeft, kLeft + 150, kLeft + 290, kLeft + 350, kLeft + 410};
        const auto head = [&] {
            body.rect(kLeft, body.y, wText, 12, kHead);
            const char* hs[] = {"Nom", "Type", "Adresse", "Valeur init.", "Commentaire"};
            for (int c = 0; c < 5; ++c) body.text(cols[c] + 3, body.y + 8.8, toWinAnsi(hs[c]), HelvBold, 7.5, kBlue);
            body.y += 12;
        };
        head();
        for (const auto& v : b.variables) {
            if (!body.fits(10)) {
                body.newPage();
                head();
            }
            body.text(cols[0] + 3, body.y + 7.6, fit(toWinAnsi(v.name), 7.2, false, 144), Helv, 7.2, kInk);
            body.text(cols[1] + 3, body.y + 7.6, fit(toWinAnsi(v.type), 7.2, false, 134), Helv, 7.2, kInk);
            body.text(cols[2] + 3, body.y + 7.6, fit(toWinAnsi(v.address), 7.2, false, 56), Helv, 7.2, kCond);
            body.text(cols[3] + 3, body.y + 7.6, fit(toWinAnsi(v.initial), 7.2, false, 56), Helv, 7.2, kInk);
            body.text(cols[4] + 3, body.y + 7.6, fit(toWinAnsi(v.comment), 7.2, false, wText - 414), Helv, 7.2, kInk);
            body.line(kLeft, body.y + 10, kW - kRight, body.y + 10, {0.92, 0.92, 0.92}, 0.4);
            body.y += 10;
        }
    }

    // ======================================================= le guide et le sommaire
    //  Deux passes : la premiere compte les pages du sommaire (sa mise en page ne
    //  depend pas des numeros), la seconde l'ecrit avec les vrais numeros.
    struct TocLine { int kind{0}; std::size_t index{0}; };      // 0 section, 1 unite, 2 tache, 3 annexe DFB, 4 annexe variables
    std::vector<TocLine> toc;
    for (const auto& it : b.order) toc.push_back({it.kind == Item::Section ? 0 : it.kind == Item::Unit ? 1 : 2, it.index});
    if (o.dfbAppendix && !b.dfbs.empty()) toc.push_back({3, 0});
    if (o.variablesAppendix && !b.variables.empty()) toc.push_back({4, 0});
    const std::string mainTask = b.tasks.empty() ? std::string("MAST") : b.tasks.front().name;
    const auto layoutToc = [&](Layout& L, std::size_t bodyFirst, bool real) {
        L.newPage();
        if (o.guide) {
            L.mark("Comment lire ce document", 0);
            L.text(kLeft, L.y + 14, toWinAnsi("Comment lire ce document"), HelvBold, 14, kBlue);
            L.y += 24;
            std::vector<std::string> paras = {
                "L'automate ex\xC3\xA9" "cute la t\xC3\xA2" "che " + mainTask + " en boucle. \xC3\x80 chaque cycle, il ex\xC3\xA9" "cute les sections dans l'ordre du sommaire, de 1 \xC3\xA0 "
                    + std::to_string(b.totalSections) + ", puis recommence.",
                "\xC2\xAB si X \xC2\xBB : la section ne s'ex\xC3\xA9" "cute que pendant les cycles o\xC3\xB9 X est vrai. Une unit\xC3\xA9 de programme regroupe des sections qui "
                "s'ex\xC3\xA9" "cutent en bloc, \xC3\xA0 son rang ; ses param\xC3\xA8tres relient ses noms \xC3\xA0 ceux du projet (armoires -> Armoires).",
                "Le code est du texte structur\xC3\xA9 (ST, norme CEI 61131-3) : := affecte une valeur, (* ... *) est un commentaire, IF ... THEN ... END_IF une condition."};
            if (o.access) paras.push_back("\xC3\x89" "crit et Lit : les variables du projet que la section modifie et celles qu'elle consulte. Appelle : les blocs qu'elle fait tourner (DFB, temporisations).");
            if (o.roles) paras.push_back("L'ic\xC3\xB4ne d'une section : le r\xC3\xB4le choisi dans " + o.appName + ".");
            for (const auto& p : paras) {
                for (const auto& l : wrap(toWinAnsi(p), 9, false, wText)) {
                    L.text(kLeft, L.y + 9, l, Helv, 9, kInk);
                    L.y += 12;
                }
                L.y += 4;
            }
            L.y += 8;
        }
        L.mark("Sommaire", 0);
        L.text(kLeft, L.y + 14, toWinAnsi("Sommaire"), HelvBold, 14, kBlue);
        L.y += 24;
        for (const auto& t : toc) {
            L.need(12);
            std::string label;
            Font f = Helv;
            Rgb color = kInk;
            Where target;
            int icon = -1;
            std::string num, cond;
            switch (t.kind) {
                case 0: {
                    const auto& s = b.sections[t.index];
                    num = std::to_string(s.rank);
                    label = s.name;
                    cond = s.condition;
                    icon = o.roles ? s.icon : -1;
                    if (const auto w = sectionAt.find(t.index); w != sectionAt.end()) target = w->second;
                    break;
                }
                case 1: {
                    const auto& u = b.units[t.index];
                    label = "Unit\xC3\xA9 de programme " + u.name + "  \xC2\xB7  rang " + std::to_string(u.rank) + "  \xC2\xB7  " + std::to_string(u.sections) + " sections";
                    f = HelvBold;
                    color = kTeal;
                    icon = o.roles ? u.icon : -1;
                    if (const auto w = unitAt.find(t.index); w != unitAt.end()) target = w->second;
                    L.y += 3;
                    break;
                }
                case 2: {
                    label = "T\xC3\xA2" "che " + b.tasks[t.index].name;
                    f = HelvBold;
                    color = kPurple;
                    if (const auto w = taskAt.find(t.index); w != taskAt.end()) target = w->second;
                    L.y += 3;
                    break;
                }
                case 3: label = "Annexe A \xC2\xB7 Blocs DFB (" + std::to_string(b.dfbs.size()) + ")"; f = HelvBold; color = kBlue; target = dfbAt; L.y += 3; break;
                default:
                    label = std::string("Annexe ") + (o.dfbAppendix && !b.dfbs.empty() ? "B" : "A") + " \xC2\xB7 Variables globales (" + std::to_string(b.variables.size()) + ")";
                    f = HelvBold;
                    color = kBlue;
                    target = varsAt;
                    L.y += 3;
                    break;
            }
            const double baseline = L.y + 8.5;
            if (!num.empty()) {
                const auto n = toWinAnsi(num);
                L.text(kLeft + 16 - helveticaWidth(n, 8.2, false), baseline, n, Helv, 8.2, kGray);
            }
            double tx = kLeft + 22;
            if (icon >= 0) {
                L.glyph(icon, tx, L.y + 1.5, 8.5, iconRgb(icon));
                tx += 12;
            }
            const auto win = toWinAnsi(label);
            L.text(tx, baseline, win, f, 8.4, color);
            double after = tx + helveticaWidth(win, 8.4, f == HelvBold);
            if (!cond.empty()) {
                const auto c = fit(toWinAnsi("  si " + cond), 7.2, false, 200);
                L.text(after, baseline, c, Helv, 7.2, kCond);
                after += helveticaWidth(c, 7.2, false);
            }
            const std::size_t pageNumber = (real ? bodyFirst + target.page : 0) + 1;
            const auto pn = toWinAnsi(std::to_string(pageNumber));
            const double pw = helveticaWidth(pn, 8.4, false);
            // Les points de suite.
            const double dotsFrom = after + 4, dotsTo = kW - kRight - pw - 4;
            if (dotsTo > dotsFrom) {
                std::string dots;
                const double dw = helveticaWidth(".", 7, false) * 1.6;
                for (double x = dotsFrom; x + dw <= dotsTo; x += dw) dots += ". ";
                L.text(dotsFrom, baseline, fit(dots, 7, false, dotsTo - dotsFrom), Helv, 7, kLight);
            }
            L.text(kW - kRight - pw, baseline, pn, Helv, 8.4, kInk);
            if (real) L.pages.back().links.push_back({kLeft, L.y, wText, 11.5, bodyFirst + target.page, target.y});
            L.y += 11.5;
        }
    };
    Layout dry;
    layoutToc(dry, 0, false);
    const std::size_t tocPages = dry.pages.size();
    const std::size_t bodyFirst = 1 + tocPages;     // la couverture, puis le sommaire
    Layout toc2;
    toc2.base = 1;
    layoutToc(toc2, bodyFirst, true);

    // ================================================================ la couverture
    Layout cover;
    cover.newPage();
    cover.mark("Couverture", 0);
    {
        double y = 150;
        cover.text(kLeft, y, toWinAnsi("PROGRAMME DE L'AUTOMATE"), HelvBold, 9, kRed);
        y += 36;
        const bool all = o.scope == Options::Scope::All;
        cover.text(kLeft, y, toWinAnsi(all ? "Lu dans l'ordre" : "Extrait du programme"), HelvBold, 30, kBlue);
        y += 34;
        cover.text(kLeft, y, toWinAnsi(all ? "d'ex\xC3\xA9" "cution" : b.scopeLabel), HelvBold, all ? 30 : 18, kBlue);
        y += 30;
        cover.text(kLeft, y, toWinAnsi(b.project + (b.version.empty() ? std::string{} : " \xC2\xB7 version " + b.version)), Helv, 16, {0.2, 0.2, 0.2});
        y += 22;
        cover.line(kLeft, y, kW - kRight, y, kBlue, 1);
        y += 16;
        std::vector<std::pair<std::string, std::string>> facts;
        if (!b.cpu.empty()) facts.emplace_back("Automate", b.cpu);
        if (!b.product.empty()) facts.emplace_back("Logiciel", b.product);
        for (const auto& t : b.tasks) {
            if (t.name == "Sous-routines") continue;
            std::string v = t.name + ", " + t.kind;
            if (t.period) v += ", p\xC3\xA9riode " + std::to_string(t.period) + " ms";
            if (t.watchdog) v += ", chien de garde " + std::to_string(t.watchdog) + " ms";
            facts.emplace_back("T\xC3\xA2" "che", v);
        }
        facts.emplace_back("Contenu", std::to_string(b.totalSections) + " sections \xC2\xB7 " + doc::thousands(b.totalLines) + " lignes de ST \xC2\xB7 "
                                          + std::to_string(b.programUnits) + " unit\xC3\xA9s de programme \xC2\xB7 " + std::to_string(b.dfbs.size()) + " blocs DFB");
        if (!all) facts.emplace_back("Export\xC3\xA9", b.scopeLabel + " : " + std::to_string(b.sections.size()) + " sections, " + doc::thousands(b.scopeLines) + " lignes");
        if (!b.source.empty()) facts.emplace_back("Source", b.source + " (Control Expert)");
        facts.emplace_back("Export\xC3\xA9 le", b.exportedAt);
        for (const auto& [k, v] : facts) {
            cover.text(kLeft, y, toWinAnsi(k), Helv, 9, kGray);
            const auto lines = wrap(toWinAnsi(v), 10, false, wText - 90);
            for (const auto& l : lines) {
                cover.text(kLeft + 90, y, l, Helv, 10, kInk);
                y += 13;
            }
            y += 2;
        }
        // Le cycle en un coup d'oeil : toutes les sections, a l'echelle de leurs lignes.
        y += 20;
        cover.text(kLeft, y, toWinAnsi("UN CYCLE DE L'AUTOMATE, SECTION PAR SECTION (LARGEUR = NOMBRE DE LIGNES)"), HelvBold, 7.5, kGray);
        y += 8;
        std::size_t total = 0;
        for (const auto& s : b.sections) total += std::max<std::size_t>(1, s.lineCount);
        double x = kLeft;
        for (const auto& s : b.sections) {
            const double w = wText * static_cast<double>(std::max<std::size_t>(1, s.lineCount)) / static_cast<double>(std::max<std::size_t>(1, total));
            const auto key = ownerColorKey(s);
            const Rgb c = key.empty() ? kBlue : ownerColor[key];
            cover.rect(x, y, std::max(0.4, w - 0.5), 24, c);
            x += w;
        }
        y += 34;
        double lx = kLeft;
        const auto legend = [&](const Rgb& c, const std::string& t) {
            cover.rect(lx, y - 7, 8, 8, c);
            const auto win = toWinAnsi(t);
            cover.text(lx + 11, y, win, Helv, 7.6, kGray);
            lx += 11 + helveticaWidth(win, 7.6, false) + 14;
            if (lx > kW - kRight - 100) {
                lx = kLeft;
                y += 12;
            }
        };
        legend(kBlue, "sections de la t\xC3\xA2" "che");
        for (const auto& u : b.units) legend(ownerColor[u.name], "unit\xC3\xA9 " + u.name);
        cover.text(kLeft, kH - 60, toWinAnsi("G\xC3\xA9n\xC3\xA9r\xC3\xA9 par " + b.app + " le " + b.exportedAt + ". Le code est celui du fichier source, sans modification."),
                   Helv, 7.6, kGray);
    }

    // ================================================================ l'assemblage
    std::vector<Page> pages;
    for (auto& p : cover.pages) pages.push_back(std::move(p));
    for (auto& p : toc2.pages) pages.push_back(std::move(p));
    for (auto& p : body.pages) pages.push_back(std::move(p));
    const std::size_t total = pages.size();
    pageHeaders(pages, b, total);

    std::vector<Mark> marks;
    for (auto m : cover.marks) marks.push_back(m);
    for (auto m : toc2.marks) marks.push_back(m);          // base 1 : deja decales
    for (auto m : body.marks) {
        m.page += bodyFirst;
        marks.push_back(m);
    }

    // Les objets : 1 catalogue, 2 pages, 3-8 polices, puis page / contenu par page,
    // puis les signets, puis les infos.
    std::vector<std::string> objects(8);
    const char* fonts[] = {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Courier", "Courier-Bold", "Courier-Oblique"};
    for (int i = 0; i < 6; ++i)
        objects[static_cast<std::size_t>(2 + i)] = std::string("<< /Type /Font /Subtype /Type1 /BaseFont /") + fonts[i] + " /Encoding /WinAnsiEncoding >>";
    const std::size_t firstPageObj = 9;       // numero PDF (1...) du premier objet page
    const auto pageObj = [&](std::size_t page) { return firstPageObj + 2 * page; };
    std::string kids;
    for (std::size_t i = 0; i < total; ++i) {
        auto& pg = pages[i];
        std::string annots;
        for (const auto& l : pg.links) {
            const std::size_t tp = std::min(l.targetPage, total - 1);
            annots += "<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [" + pdfNumber(l.x) + " " + pdfNumber(kH - l.y - l.h) + " " + pdfNumber(l.x + l.w) + " "
                    + pdfNumber(kH - l.y) + "] /Dest [" + std::to_string(pageObj(tp)) + " 0 R /XYZ 0 " + pdfNumber(kH - l.targetY + 8) + " 0] >> ";
        }
        std::string page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + pdfNumber(kW) + " " + pdfNumber(kH) + "] /Resources << /Font << ";
        for (int f = 1; f <= 6; ++f) page += "/F" + std::to_string(f) + " " + std::to_string(2 + f) + " 0 R ";
        page += ">> >> /Contents " + std::to_string(pageObj(i) + 1) + " 0 R";
        if (!annots.empty()) page += " /Annots [" + annots + "]";
        page += " >>";
        objects.push_back(page);
        const auto packed = doc::zlibCompress(pg.ops);
        if (!packed.empty() && packed.size() < pg.ops.size())
            objects.push_back("<< /Length " + std::to_string(packed.size()) + " /Filter /FlateDecode >>\nstream\n" + packed + "\nendstream");
        else
            objects.push_back("<< /Length " + std::to_string(pg.ops.size()) + " >>\nstream\n" + pg.ops + "\nendstream");
        kids += std::to_string(pageObj(i)) + " 0 R ";
    }
    objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(total) + " >>";

    // ---- les signets : une liste a deux niveaux ----
    const std::size_t outlineRoot = objects.size() + 1;
    objects.push_back({});
    struct Node { std::size_t obj{0}; std::size_t parent{0}; std::vector<std::size_t> kids; std::size_t mark{0}; };
    std::vector<Node> nodes;
    std::vector<std::size_t> top;
    for (std::size_t i = 0; i < marks.size(); ++i) {
        Node n;
        n.obj = objects.size() + 1 + i;
        n.mark = i;
        if (marks[i].level > 0 && !top.empty()) {
            n.parent = top.back();
            nodes[top.back()].kids.push_back(nodes.size());
        } else {
            top.push_back(nodes.size());
        }
        nodes.push_back(std::move(n));
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        const auto& m = marks[n.mark];
        const bool child = m.level > 0 && std::find(top.begin(), top.end(), i) == top.end();
        const auto& siblings = child ? nodes[n.parent].kids : top;
        const auto pos = static_cast<std::size_t>(std::find(siblings.begin(), siblings.end(), i) - siblings.begin());
        std::string o2 = "<< /Title " + pdfLiteral(m.title) + " /Parent " + std::to_string(child ? nodes[n.parent].obj : outlineRoot) + " 0 R";
        if (pos > 0) o2 += " /Prev " + std::to_string(nodes[siblings[pos - 1]].obj) + " 0 R";
        if (pos + 1 < siblings.size()) o2 += " /Next " + std::to_string(nodes[siblings[pos + 1]].obj) + " 0 R";
        if (!n.kids.empty())
            o2 += " /First " + std::to_string(nodes[n.kids.front()].obj) + " 0 R /Last " + std::to_string(nodes[n.kids.back()].obj) + " 0 R /Count -"
                + std::to_string(n.kids.size());
        o2 += " /Dest [" + std::to_string(pageObj(std::min(m.page, total - 1))) + " 0 R /XYZ 0 " + pdfNumber(kH - m.y + 8) + " 0] >>";
        objects.push_back(o2);
    }
    if (!top.empty())
        objects[outlineRoot - 1] = "<< /Type /Outlines /First " + std::to_string(nodes[top.front()].obj) + " 0 R /Last " + std::to_string(nodes[top.back()].obj)
                                 + " 0 R /Count " + std::to_string(top.size()) + " >>";
    else
        objects[outlineRoot - 1] = "<< /Type /Outlines /Count 0 >>";
    objects[0] = "<< /Type /Catalog /Pages 2 0 R /Outlines " + std::to_string(outlineRoot) + " 0 R /PageMode /UseOutlines >>";
    objects.push_back("<< /Title " + pdfLiteral(toWinAnsi("Programme de l'automate : " + b.project)) + " /Creator " + pdfLiteral(toWinAnsi(b.app))
                      + " /Producer " + pdfLiteral(toWinAnsi(b.app)) + " >>");
    const std::size_t infoObj = objects.size();

    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        out += buf;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info " + std::to_string(infoObj) + " 0 R >>\nstartxref\n" + std::to_string(xref)
         + "\n%%EOF\n";
    return std::vector<std::uint8_t>(out.begin(), out.end());
}

} // namespace exporter::book
