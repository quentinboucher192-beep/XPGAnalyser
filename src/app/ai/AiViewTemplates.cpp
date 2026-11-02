// =============================================================================
//  app/ai/AiViewTemplates.cpp - voir AiViewTemplates.hpp : LE MODELE DE VUES (le document, l'analyse, ce que l'IA recoit)
// -----------------------------------------------------------------------------
//  LE JSON de l'analyse d'une piece (extra "modeleVues") :
//    { "format": 1, "genre": "pv" | "captures", "confiance": 0.9,
//      "document": { "numero", "titre", "revision", "dateRevision", "designation" },
//      "vues": [ { "nom", "page", "pageCommentaires", "approbation" ("oui"|"non"|""), "date", "visa", "commentaires",
//                  "aLaMain", "ecran": [x, y, l, h], "largeur", "hauteur", "ecranLargeur", "ecranHauteur", "image" ("vue-2.png"),
//                  "couleurs": [ ["#FF0000", 0.06, "defauts, alarmes"] ], "fond": "#E0E0E0" } ],
//      "couleurs": [...], "fond": "#E0E0E0", "partFond": 0.58,
//      "bandes": { "haut": 0.09, "bas": 0.06, "vuesHaut": 12, "vuesBas": 12, "vues": 13, "couleurHaut", "couleurBas" },
//      "notes": [ ... ] }
//  Le choix de la piece (son extra "modeleVues") : { "actif": true|false, "par": "utilisateur"|"demande", "date" }.
//  Les images recadrees : IA/cache/images/modeles-vues/<id de la piece>/vue-<page>.png (elles se refont : renderView).
// =============================================================================
#include "AiViewTemplates.hpp"

#include "AiJson.hpp"
#include "AiOperations.hpp"
#include "AiPaths.hpp"
#include "AiRegistry.hpp"
#include "../Capture.hpp"
#include "../../hmi/HmiPdf.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace app::ai::viewtpl {

namespace fs = std::filesystem;

namespace {

constexpr double kRenderDpi = 150.0;   // les pages de vues rendues a 150 points par pouce (A4 paysage : 1754 x 1240)
constexpr double kNotesDpi = 72.0;     // une page de commentaires (l'encre)

std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) != 0)) ++a;
    while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) != 0)) --b;
    return std::string(s.substr(a, b - a));
}

// Le texte plie : minuscules, sans accents ; l'apostrophe typographique devient '.
std::string fold(std::string_view s) {
    std::string t(s);
    for (std::size_t at = t.find("\xE2\x80\x99"); at != std::string::npos; at = t.find("\xE2\x80\x99", at)) t.replace(at, 3, "'");
    return ui::foldForSearch(t);
}

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::vector<std::string> linesOf(std::string_view page) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= page.size(); ++i) {
        if (i < page.size() && page[i] != '\n' && page[i] != '\r') continue;
        std::string l = trim(page.substr(start, i - start));
        if (!l.empty()) out.push_back(std::move(l));
        start = i + 1;
    }
    return out;
}

std::vector<std::string> tokensOf(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == ':') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
            if (c == ':') out.emplace_back(":");
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool isDate(std::string_view t) {
    // 16/07/2025, 1/7/25, 16.07.2025
    int parts = 0, digits = 0;
    for (const char c : t) {
        if (c >= '0' && c <= '9') {
            ++digits;
        } else if (c == '/' || c == '.') {
            if (digits == 0) return false;
            ++parts;
            digits = 0;
        } else {
            return false;
        }
    }
    return parts == 2 && digits >= 2;
}

// << VUE : <NOM> >> : le nom (vide : ce n'est pas une ligne de vue).
std::string viewNameOf(const std::string& line) {
    const std::string f = fold(line);
    if (f.rfind("vue", 0) != 0) return {};
    std::size_t i = 3;
    while (i < f.size() && f[i] == ' ') ++i;
    if (i >= f.size() || f[i] != ':') return {};
    const std::size_t colon = line.find(':');
    return colon == std::string::npos ? std::string{} : trim(std::string_view(line).substr(colon + 1));
}

// Une croix, une case cochee (U+2611, U+2612, U+2713, U+2714, U+2717, U+2718 : comparees en octets - l'essai des glyphes lit
// chaque chaine des sources, et l'atlas n'a pas toutes ces coches).
bool markToken(const std::string& t) {
    if (t == "X" || t == "x") return true;
    if (t.size() != 3 || static_cast<unsigned char>(t[0]) != 0xE2) return false;
    const unsigned b1 = static_cast<unsigned char>(t[1]), b2 = static_cast<unsigned char>(t[2]);
    return (b1 == 0x98 && (b2 == 0x91 || b2 == 0x92)) || (b1 == 0x9C && (b2 == 0x93 || b2 == 0x94 || b2 == 0x97 || b2 == 0x98));
}

// Le cartouche APPROBATION : OUI / NON (une croix lue a cote), DATE, VISA - ce qui se lit dans le texte de la page.
Approval approvalOf(const std::vector<std::string>& lines) {
    Approval a;
    std::size_t from = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (contains(fold(lines[i]), "approbation")) {
            from = i;
            break;
        }
    if (from == lines.size()) return a;
    std::vector<std::string> tok, low;
    for (std::size_t i = from; i < lines.size(); ++i)
        for (auto& t : tokensOf(lines[i])) {
            low.push_back(fold(t));
            tok.push_back(std::move(t));
        }
    const auto stateAt = [&](std::size_t k) -> std::string { return low[k] == "oui" || low[k] == "non" ? low[k] : std::string{}; };
    for (std::size_t k = 0; k < tok.size() && a.state.empty(); ++k) {
        if (!markToken(tok[k])) continue;
        for (std::size_t d = 1; d <= 3 && a.state.empty(); ++d) {
            if (k >= d) a.state = stateAt(k - d);
            if (a.state.empty() && k + d < tok.size()) a.state = stateAt(k + d);
        }
    }
    for (std::size_t k = 0; k < tok.size(); ++k) {
        if (a.date.empty() && isDate(tok[k])) a.date = tok[k];
        if (a.visa.empty() && low[k] == "visa") {
            std::size_t j = k + 1;
            while (j < tok.size() && tok[j] == ":") ++j;
            if (j < tok.size()) {
                const std::string& v = low[j];
                const bool label = v == "signature" || v == "date" || v == "oui" || v == "non" || v == "approbation" || v == "visa";
                if (!label && !isDate(tok[j]) && tok[j].size() <= 16 && !markToken(tok[j])) a.visa = tok[j];
            }
        }
    }
    // Un cartouche lu ligne par ligne (les intitules, puis les valeurs dans le meme ordre : OUI, NON, la date, le visa) : le
    // visa est la premiere valeur courte apres SIGNATURE qui n'est ni OUI, ni NON, ni une croix, ni une date.
    if (a.visa.empty())
        for (std::size_t k = 0; k < tok.size(); ++k) {
            if (low[k] != "signature") continue;
            for (std::size_t j = k + 1; j < tok.size() && j <= k + 12; ++j) {
                const std::string& v = low[j];
                if (tok[j] == ":" || v == "oui" || v == "non" || markToken(tok[j]) || isDate(tok[j])) continue;
                if (tok[j].size() <= 16 && std::all_of(tok[j].begin(), tok[j].end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.'; }))
                    a.visa = tok[j];
                break;
            }
            break;
        }
    return a;
}

// Le texte d'une page COMMENTAIRES, sans ses intitules (COMMENTAIRES :, le cartouche DATE / VISA / SIGNATURE).
std::string commentsOf(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) {
        const std::string f = fold(l);
        std::string keep = l;
        if (f.rfind("commentaire", 0) == 0) {
            const std::size_t colon = l.find(':');
            keep = colon == std::string::npos ? std::string{} : trim(std::string_view(l).substr(colon + 1));
        } else if (contains(f, "signature") || (contains(f, "visa") && contains(f, "date"))) {
            continue;   // le cartouche
        } else if (f == "date :" || f == "date:" || f == "visa :" || f == "visa:") {
            continue;
        }
        if (keep.empty()) continue;
        out += (out.empty() ? "" : "\n") + keep;
    }
    return out;
}

bool readFileBytes(const std::string& path, std::string& out) {
    std::ifstream in(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool writeFileBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    const fs::path p(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(fs::path(std::u8string(path.begin(), path.end())), ec);
}

std::uintmax_t sizeOf(const std::string& path) {
    std::error_code ec;
    const auto n = fs::file_size(fs::path(std::u8string(path.begin(), path.end())), ec);
    return ec ? 0 : n;
}

std::string extOf(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos) return {};
    std::string e(name.substr(dot + 1));
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

std::string stemOf(std::string_view name) {
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name = name.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

bool savePng(const hmi::Rgba& img, const std::string& path) {
    std::vector<std::uint8_t> png;
    std::vector<std::uint8_t> rgba = img.pixels;
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;   // opaque (une image sur fond blanc)
    return app::encodePng(rgba, img.width, img.height, png) && writeFileBytes(path, png);
}

// Une page du PDF rendue a `dpi` (sous le verrou de PDFium, pris par l'appelant).
bool renderPage(hmi::pdf::DocHandle d, int index, double dpi, hmi::Rgba& out, std::string* why) {
    double w = 0, h = 0;
    if (!hmi::pdf::pageSize(d, index, w, h)) {
        if (why) *why = "la taille de la page " + std::to_string(index + 1) + " ne se lit pas";
        return false;
    }
    const int pw = std::max(1, static_cast<int>(std::lround(w * dpi / 72.0)));
    const int ph = std::max(1, static_cast<int>(std::lround(h * dpi / 72.0)));
    return hmi::pdf::render(d, index, pw, ph, 0, out, why);
}

json::Value colorsJson(const Palette& p) {
    json::Value a = json::Value::array();
    for (const auto& s : p.colors) {
        json::Value c = json::Value::array();
        c.push(hex(s.rgb));
        c.push(std::round(s.share * 10000.0) / 10000.0);
        c.push(s.role);
        a.push(std::move(c));
    }
    return a;
}

std::uint32_t parseHex(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return 0;
    return static_cast<std::uint32_t>(std::strtoul(s.c_str() + 1, nullptr, 16)) & 0xFFFFFFu;
}

Palette paletteFrom(const json::Value& colors, const std::string& background, double share) {
    Palette p;
    for (const auto& c : colors.items()) {
        if (!c.isArray() || c.items().size() < 2) continue;
        Swatch s;
        s.rgb = parseHex(c.items()[0].asString());
        s.share = c.items()[1].asNumber(0);
        if (c.items().size() > 2) s.role = c.items()[2].asString();
        p.colors.push_back(std::move(s));
    }
    p.background = parseHex(background);
    p.backgroundShare = share;
    return p;
}

std::string approvalText(const Approval& a) {
    if (a.state.empty()) return a.date.empty() && a.visa.empty() ? std::string("non renseign\xC3\xA9" "e") : "non coch\xC3\xA9" "e" + std::string(a.date.empty() ? "" : ", dat\xC3\xA9" "e du " + a.date);
    std::string s = a.state == "oui" ? "OUI (approuv\xC3\xA9" "e)" : "NON (\xC3\xA0 corriger)";
    if (!a.date.empty()) s += ", le " + a.date;
    if (!a.visa.empty()) s += ", visa " + a.visa;
    return s;
}

std::string oneLine(std::string s, std::size_t max) {
    for (auto& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (s.size() > max) {
        std::size_t n = max;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
        s = s.substr(0, n) + "\xE2\x80\xA6";
    }
    return s;
}

// Les vues qui demandent un changement : une approbation NON, des commentaires (ecrits ou a la main).
bool toFix(const View& v) { return v.approval.state == "non" || !v.comments.empty() || v.handwritten; }

const Attached* fileFor(const std::vector<Attached>& files, const std::string& piece) {
    if (files.empty()) return nullptr;
    if (piece.empty()) return &files.front();
    const std::string want = fold(trim(piece));
    for (const auto& f : files)
        if (fold(f.name) == want || f.id == piece) return &f;
    for (const auto& f : files)
        if (contains(fold(f.name), want)) return &f;
    return nullptr;
}

std::string viewsList(const Template& t) {
    std::string s;
    for (std::size_t i = 0; i < t.views.size(); ++i) s += (i ? ", " : "") + std::to_string(i + 1) + ". " + t.views[i].name;
    return s;
}

} // namespace

const View* Template::view(std::string_view nameOrNumber) const noexcept {
    const std::string want = trim(nameOrNumber);
    if (want.empty()) return nullptr;
    if (std::all_of(want.begin(), want.end(), [](char c) { return c >= '0' && c <= '9'; }) && want.size() < 5) {
        const int n = std::atoi(want.c_str());
        if (n >= 1 && static_cast<std::size_t>(n) <= views.size()) return &views[static_cast<std::size_t>(n - 1)];
    }
    std::string w = fold(want);
    if (const std::string named = viewNameOf(want); !named.empty()) w = fold(named);
    for (const auto& v : views)
        if (fold(v.name) == w) return &v;
    const View* only = nullptr;
    for (const auto& v : views)
        if (contains(fold(v.name), w)) {
            if (only) return nullptr;   // plusieurs : ambigu
            only = &v;
        }
    return only;
}

// ------------------------------------------------------------------------ le texte ---
TextRecognition recognizeText(const std::vector<std::string>& pages) {
    TextRecognition r;
    bool title = false, cartouche = false, comments = false;
    int viewPages = 0;
    for (const auto& p : pages) {
        const std::string f = fold(p);
        title = title || contains(f, "approbation de vues") || contains(f, "approbation des vues");
        bool isView = false;
        for (const auto& l : linesOf(p))
            if (!viewNameOf(l).empty()) isView = true;
        viewPages += isView ? 1 : 0;
        cartouche = cartouche || (contains(f, "approbation") && (contains(f, "visa") || contains(f, "signature")));
        for (const auto& l : linesOf(p))
            if (fold(l).rfind("commentaire", 0) == 0) comments = true;
    }
    double c = 0;
    if (title) {
        c += 0.6;
        r.reasons.push_back("le titre << approbation de vues >>");
    }
    if (viewPages > 0) {
        c += viewPages > 1 ? 0.4 : 0.3;
        r.reasons.push_back(std::to_string(viewPages) + (viewPages > 1 ? " pages << VUE : >>" : " page << VUE : >>"));
    }
    if (cartouche) {
        c += 0.2;
        r.reasons.push_back("le cartouche APPROBATION, VISA");
    }
    if (comments) {
        c += 0.1;
        r.reasons.push_back("des pages COMMENTAIRES");
    }
    r.confidence = std::min(1.0, c);
    r.recognized = viewPages > 0 && r.confidence >= 0.6;
    return r;
}

Template parseText(const std::vector<std::string>& pages) {
    Template t;
    t.kind = "pv";
    std::vector<int> viewPage(pages.size(), 0);
    std::vector<std::string> names(pages.size());
    for (std::size_t i = 0; i < pages.size(); ++i)
        for (const auto& l : linesOf(pages[i]))
            if (std::string n = viewNameOf(l); !n.empty() && names[i].empty()) {
                names[i] = std::move(n);
                viewPage[i] = 1;
            }
    const std::size_t firstView = static_cast<std::size_t>(std::find(viewPage.begin(), viewPage.end(), 1) - viewPage.begin());
    // le document : avant la premiere vue (la page de garde). Le titre : les lignes qui suivent << PV d'approbation de vues >>, ou
    // a defaut (un intitule dessine, absent du texte) celles qui suivent le numero du document.
    const auto titleAfter = [](const std::vector<std::string>& lines, std::size_t k) {
        std::string title;
        for (std::size_t j = k + 1, n = 0; j < lines.size() && n < 3; ++j, ++n) {
            const std::string fj = fold(lines[j]);
            if (lines[j].find_first_of(":@") != std::string::npos || lines[j].size() > 80 || contains(fj, "tel ") || contains(fj, "siret")
                || contains(fj, "fax") || contains(fj, "approbation"))
                break;
            title += (title.empty() ? "" : " - ") + trim(lines[j]);
        }
        return title;
    };
    std::string afterNumber;
    for (std::size_t i = 0; i < std::min(firstView, pages.size()); ++i) {
        const auto lines = linesOf(pages[i]);
        for (std::size_t k = 0; k < lines.size(); ++k) {
            const std::string f = fold(lines[k]);
            if (t.doc.number.empty() && contains(f, "document") && lines[k].find(':') != std::string::npos && !contains(f, "approbation")) {
                t.doc.number = trim(std::string_view(lines[k]).substr(lines[k].rfind(':') + 1));
                afterNumber = titleAfter(lines, k);
            }
            if (t.doc.title.empty() && (contains(f, "approbation de vues") || contains(f, "approbation des vues"))) t.doc.title = titleAfter(lines, k);
            if (t.doc.revision.empty()) {
                const auto tok = tokensOf(lines[k]);
                if (tok.size() >= 2 && tok[0].size() <= 2 && std::all_of(tok[0].begin(), tok[0].end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; })
                    && isDate(tok[1])) {
                    t.doc.revision = tok[0];
                    t.doc.revisionDate = tok[1];
                    std::string rest;
                    for (std::size_t j = tok.size() >= 5 ? 4 : 2; j < tok.size(); ++j) rest += (rest.empty() ? "" : " ") + tok[j];
                    t.doc.revisionText = rest;
                }
            }
        }
    }
    if (t.doc.title.empty()) t.doc.title = afterNumber;
    // les vues, et la page de commentaires qui suit chacune
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (!viewPage[i]) continue;
        View v;
        v.name = names[i];
        v.page = static_cast<int>(i + 1);
        v.approval = approvalOf(linesOf(pages[i]));
        if (i + 1 < pages.size() && !viewPage[i + 1]) {
            const auto lines = linesOf(pages[i + 1]);
            const bool isComments = std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return fold(l).rfind("commentaire", 0) == 0; });
            if (isComments) {
                v.commentsPage = static_cast<int>(i + 2);
                v.comments = commentsOf(lines);
            }
        }
        t.views.push_back(std::move(v));
    }
    return t;
}

// ---------------------------------------------------------------------- l'analyse ---
std::string cacheFolder(std::string_view attachmentId) {
    if (root().empty() || attachmentId.empty()) return {};
    return detail::join(detail::join(cacheDir("images"), "modeles-vues"), attachmentId);
}

namespace {

// La charte de l'ensemble : les couleurs de toutes les vues, les bandes communes.
void finishCharte(Template& t, const std::vector<hmi::Rgba>& small) {
    std::vector<const hmi::Rgba*> all;
    for (const auto& s : small)
        if (s.width > 0) all.push_back(&s);
    t.palette = paletteOf(all, 20);
    t.bands = commonBands(all);
}

} // namespace

bool analyzePdf(std::string_view bytes, const std::vector<std::string>& pagesText, const std::string& cacheDir, Template& out, std::string* why) {
    const TextRecognition rec = recognizeText(pagesText);
    if (!rec.recognized) {
        if (why) *why = "pas un mod\xC3\xA8le de vues (ni << VUE : >>, ni cartouche d'approbation)";
        return false;
    }
    out = parseText(pagesText);
    out.confidence = rec.confidence;
    const auto lock = hmi::pdf::callLock();
    std::string err;
    hmi::pdf::DocHandle d = hmi::pdf::open(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &err);
    if (!d) {
        out.notes.push_back("les images des vues ne sont pas lues (" + err + ") : seul le texte du PV l'est");
        return true;
    }
    std::vector<hmi::Rgba> small;   // chaque vue reduite (la charte)
    for (auto& v : out.views) {
        hmi::Rgba page;
        std::string e;
        if (!renderPage(d, v.page - 1, kRenderDpi, page, &e)) {
            out.notes.push_back("page " + std::to_string(v.page) + " : pas rendue (" + e + ")");
            continue;
        }
        Rect r = findScreen(page);
        if (r.empty()) {
            r = {0, 0, page.width, page.height};
            out.notes.push_back("page " + std::to_string(v.page) + " : la capture n'est pas trouv\xC3\xA9" "e, la page enti\xC3\xA8re est gard\xC3\xA9" "e");
        }
        v.screen = r;
        const hmi::Rgba shot = fit(crop(page, r), kMaxSide);
        v.width = shot.width;
        v.height = shot.height;
        const auto size = likelySize(r.w * 96.0 / kRenderDpi, r.h * 96.0 / kRenderDpi);
        v.screenW = size.first;
        v.screenH = size.second;
        v.image = "vue-" + std::to_string(v.page) + ".png";
        if (!cacheDir.empty() && !savePng(shot, detail::join(cacheDir, v.image))) out.notes.push_back("page " + std::to_string(v.page) + " : l'image n'a pas pu \xC3\xAAtre gard\xC3\xA9" "e dans le cache");
        small.push_back(fit(shot, 640));
        v.palette = paletteOf(small.back(), 12);
        // une page de commentaires sans texte : annotee a la main ?
        if (v.commentsPage > 0 && v.comments.empty()) {
            hmi::Rgba notes;
            if (renderPage(d, v.commentsPage - 1, kNotesDpi, notes, nullptr) && inkRatio(notes) > 0.001) v.handwritten = true;
        }
    }
    hmi::pdf::close(d);
    finishCharte(out, small);
    return true;
}

bool analyzeImage(std::string_view bytes, std::string_view ext, const std::string& fileName, const std::string& cacheDir, Template& out, std::string* why) {
    hmi::Rgba img;
    std::string err;
    const hmi::Bytes data(bytes.begin(), bytes.end());
    if (!hmi::decodeImage(data, ext, img, 4096, &err) || img.width <= 0) {
        if (why) *why = "image illisible" + (err.empty() ? std::string{} : " (" + err + ")");
        return false;
    }
    out = Template{};
    out.kind = "captures";
    out.confidence = 1.0;
    View v;
    v.name = stemOf(fileName);
    Rect r = findScreen(img);
    if (r.empty() || static_cast<double>(r.w) * r.h > 0.9 * img.width * img.height) r = {0, 0, img.width, img.height};
    v.screen = r;
    const hmi::Rgba shot = fit(crop(img, r), kMaxSide);
    v.width = shot.width;
    v.height = shot.height;
    v.screenW = r.w;
    v.screenH = r.h;
    v.image = "vue-1.png";
    if (!cacheDir.empty() && !savePng(shot, detail::join(cacheDir, v.image))) out.notes.push_back("l'image n'a pas pu \xC3\xAAtre gard\xC3\xA9" "e dans le cache");
    std::vector<hmi::Rgba> small{fit(shot, 640)};
    v.palette = paletteOf(small.front(), 12);
    out.views.push_back(std::move(v));
    finishCharte(out, small);
    return true;
}

bool renderView(const std::string& sourcePath, std::string_view ext, const View& v, const std::string& outPath, std::string* why) {
    std::string bytes;
    if (!readFileBytes(sourcePath, bytes)) {
        if (why) *why = "la pi\xC3\xA8" "ce jointe ne se lit plus";
        return false;
    }
    hmi::Rgba img;
    if (ext == "pdf") {
        const auto lock = hmi::pdf::callLock();
        std::string err;
        hmi::pdf::DocHandle d = hmi::pdf::open(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &err);
        if (!d) {
            if (why) *why = err;
            return false;
        }
        const bool ok = renderPage(d, v.page - 1, kRenderDpi, img, why);
        hmi::pdf::close(d);
        if (!ok) return false;
    } else {
        std::string err;
        if (!hmi::decodeImage(hmi::Bytes(bytes.begin(), bytes.end()), ext, img, 4096, &err)) {
            if (why) *why = err;
            return false;
        }
    }
    const hmi::Rgba shot = fit(v.screen.empty() ? img : crop(img, v.screen), kMaxSide);
    if (!savePng(shot, outPath)) {
        if (why) *why = "l'image ne s'\xC3\xA9" "crit pas dans le cache";
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------- le JSON ---
json::Value toJson(const Template& t) {
    json::Value o = json::Value::object();
    o.set("format", 1);
    o.set("genre", t.kind);
    o.set("confiance", std::round(t.confidence * 100.0) / 100.0);
    json::Value d = json::Value::object();
    d.set("numero", t.doc.number);
    d.set("titre", t.doc.title);
    d.set("revision", t.doc.revision);
    d.set("dateRevision", t.doc.revisionDate);
    d.set("designation", t.doc.revisionText);
    o.set("document", std::move(d));
    json::Value views = json::Value::array();
    for (const auto& v : t.views) {
        json::Value j = json::Value::object();
        j.set("nom", v.name);
        j.set("page", v.page);
        j.set("pageCommentaires", v.commentsPage);
        j.set("approbation", v.approval.state);
        j.set("date", v.approval.date);
        j.set("visa", v.approval.visa);
        j.set("commentaires", v.comments);
        j.set("aLaMain", v.handwritten);
        json::Value r = json::Value::array();
        for (const int n : {v.screen.x, v.screen.y, v.screen.w, v.screen.h}) r.push(n);
        j.set("ecran", std::move(r));
        j.set("largeur", v.width);
        j.set("hauteur", v.height);
        j.set("ecranLargeur", v.screenW);
        j.set("ecranHauteur", v.screenH);
        j.set("image", v.image);
        j.set("couleurs", colorsJson(v.palette));
        j.set("fond", hex(v.palette.background));
        views.push(std::move(j));
    }
    o.set("vues", std::move(views));
    o.set("couleurs", colorsJson(t.palette));
    o.set("fond", hex(t.palette.background));
    o.set("partFond", std::round(t.palette.backgroundShare * 10000.0) / 10000.0);
    json::Value b = json::Value::object();
    b.set("haut", std::round(t.bands.header * 10000.0) / 10000.0);
    b.set("bas", std::round(t.bands.footer * 10000.0) / 10000.0);
    b.set("vuesHaut", t.bands.headerViews);
    b.set("vuesBas", t.bands.footerViews);
    b.set("vues", t.bands.views);
    b.set("couleurHaut", hex(t.bands.headerColor));
    b.set("couleurBas", hex(t.bands.footerColor));
    b.set("accentHaut", t.bands.headerAccent < 0 ? std::string{} : hex(static_cast<std::uint32_t>(t.bands.headerAccent)));
    b.set("accentBas", t.bands.footerAccent < 0 ? std::string{} : hex(static_cast<std::uint32_t>(t.bands.footerAccent)));
    o.set("bandes", std::move(b));
    json::Value notes = json::Value::array();
    for (const auto& n : t.notes) notes.push(n);
    o.set("notes", std::move(notes));
    return o;
}

Template fromJson(const json::Value& o) {
    Template t;
    if (!o.isObject()) return t;
    t.kind = o.str("genre", "pv");
    t.confidence = o.num("confiance", 0);
    const json::Value& d = o.at("document");
    t.doc.number = d.str("numero");
    t.doc.title = d.str("titre");
    t.doc.revision = d.str("revision");
    t.doc.revisionDate = d.str("dateRevision");
    t.doc.revisionText = d.str("designation");
    for (const auto& j : o.at("vues").items()) {
        if (!j.isObject()) continue;
        View v;
        v.name = j.str("nom");
        v.page = static_cast<int>(j.integer("page", 0));
        v.commentsPage = static_cast<int>(j.integer("pageCommentaires", 0));
        v.approval.state = j.str("approbation");
        v.approval.date = j.str("date");
        v.approval.visa = j.str("visa");
        v.comments = j.str("commentaires");
        v.handwritten = j.flag("aLaMain", false);
        const auto& r = j.at("ecran").items();
        if (r.size() == 4)
            v.screen = {static_cast<int>(r[0].asInt()), static_cast<int>(r[1].asInt()), static_cast<int>(r[2].asInt()), static_cast<int>(r[3].asInt())};
        v.width = static_cast<int>(j.integer("largeur", 0));
        v.height = static_cast<int>(j.integer("hauteur", 0));
        v.screenW = static_cast<int>(j.integer("ecranLargeur", 0));
        v.screenH = static_cast<int>(j.integer("ecranHauteur", 0));
        v.image = j.str("image");
        v.palette = paletteFrom(j.at("couleurs"), j.str("fond"), 0);
        t.views.push_back(std::move(v));
    }
    t.palette = paletteFrom(o.at("couleurs"), o.str("fond"), o.num("partFond", 0));
    const json::Value& b = o.at("bandes");
    t.bands.header = b.num("haut", 0);
    t.bands.footer = b.num("bas", 0);
    t.bands.headerViews = static_cast<int>(b.integer("vuesHaut", 0));
    t.bands.footerViews = static_cast<int>(b.integer("vuesBas", 0));
    t.bands.views = static_cast<int>(b.integer("vues", 0));
    t.bands.headerColor = parseHex(b.str("couleurHaut"));
    t.bands.footerColor = parseHex(b.str("couleurBas"));
    t.bands.headerAccent = b.str("accentHaut").empty() ? -1 : static_cast<int>(parseHex(b.str("accentHaut")));
    t.bands.footerAccent = b.str("accentBas").empty() ? -1 : static_cast<int>(parseHex(b.str("accentBas")));
    for (const auto& n : o.at("notes").items()) t.notes.push_back(n.asString());
    return t;
}

void applyTemplate(AttachmentAnalysis& an, const Template& t) {
    if (!an.extra.isObject()) an.extra = json::Value::object();
    an.extra.set("modeleVues", toJson(t));
    if (!an.summary.empty()) an.details.insert(an.details.begin(), an.summary);
    const std::size_t n = t.views.size();
    an.summary = "Mod\xC3\xA8le de vues : " + std::to_string(n) + (n > 1 ? " vues" : " vue")
                 + (t.kind == "pv" ? std::string(" (PV d'approbation)") : std::string(" (capture)"));
    std::vector<std::string> lines;
    if (!t.doc.number.empty() || !t.doc.title.empty())
        lines.push_back("document : " + (t.doc.number.empty() ? std::string{} : "n\xC2\xB0 " + t.doc.number) + (t.doc.title.empty() ? std::string{} : " \xE2\x80\x94 " + t.doc.title));
    if (n > 0) lines.push_back("vues : " + oneLine(viewsList(t), 400));
    if (const std::string c = charteText(t); !c.empty()) lines.push_back("charte : " + oneLine(c, 400));
    for (const auto& note : t.notes) lines.push_back(note);
    an.details.insert(an.details.begin() + (an.details.empty() ? 0 : 1), lines.begin(), lines.end());
}

bool isImageExt(std::string_view ext) noexcept { return ext == "png" || ext == "jpg" || ext == "jpeg"; }

bool candidate(const Attachment& a) noexcept {
    if (a.removed) return false;
    const std::string_view e = a.ext.empty() ? std::string_view(a.type) : std::string_view(a.ext);
    return e == "pdf" || isImageExt(e);
}

std::optional<bool> choiceOf(const Attachment& a) {
    if (const json::Value* c = a.extra.isObject() ? a.extra.find("modeleVues") : nullptr; c && c->isObject() && c->has("actif")) return c->flag("actif", false);
    return std::nullopt;
}

void setChoice(Attachment& a, bool on, std::string_view by) {
    json::Value o = json::Value::object();
    o.set("actif", on);
    o.set("par", std::string(by));
    o.set("date", isoDate(now()));
    if (!a.extra.isObject()) a.extra = json::Value::object();
    a.extra.set("modeleVues", std::move(o));
}

bool wanted(const Attachment& a) {
    if (!candidate(a)) return false;
    const auto c = choiceOf(a);
    const std::string_view e = a.ext.empty() ? std::string_view(a.type) : std::string_view(a.ext);
    return e == "pdf" ? c.value_or(true) : c.value_or(false);
}

std::optional<Template> templateOf(const Attachment& a) {
    if (!candidate(a) || choiceOf(a).value_or(true) == false) return std::nullopt;
    for (auto it = a.analyses.rbegin(); it != a.analyses.rend(); ++it) {
        if (it->kind != "extraction") continue;
        if (it->state != "analysee" || !it->extra.isObject()) return std::nullopt;
        const json::Value* v = it->extra.find("modeleVues");
        if (!v || !v->isObject()) return std::nullopt;
        Template t = fromJson(*v);
        if (t.views.empty()) return std::nullopt;
        return t;
    }
    return std::nullopt;
}

// ----------------------------------------------------------------------- pour l'IA ---
std::string charteText(const Template& t) {
    std::string s;
    const auto add = [&](const std::string& part) { s += (s.empty() ? "" : " ; ") + part; };
    // les 6 plus etendues, puis toutes les couleurs franches (pas un gris : un role probable - defauts, valeurs, conduites,
    // consignes...), 12 au plus, par part decroissante
    std::vector<const Swatch*> shown;
    for (const auto& c : t.palette.colors)
        if (shown.size() < 6) shown.push_back(&c);
    for (const auto& c : t.palette.colors) {
        const std::string w = colorWord(c.rgb);
        const bool neutral = w.rfind("gris", 0) == 0 || w == "noir" || w == "blanc";
        if (shown.size() < 12 && !neutral && std::find(shown.begin(), shown.end(), &c) == shown.end()) shown.push_back(&c);
    }
    std::sort(shown.begin(), shown.end(), [](const Swatch* a, const Swatch* b) { return a->share > b->share; });
    for (const Swatch* pc : shown) {
        const Swatch& c = *pc;
        std::string part = c.rgb == t.palette.background ? "fond " + hex(c.rgb) : colorWord(c.rgb) + " " + hex(c.rgb);
        part += " (" + percentText(c.share) + " %)";
        if (!c.role.empty() && c.role != "fond") part += " : " + c.role + " (probable)";
        add(part);
    }
    const int refH = !t.views.empty() && t.views.front().screenH > 0 ? t.views.front().screenH : 0;
    const auto band = [&](double frac, int shared, std::uint32_t color, int accent, const char* what) {
        if (frac <= 0) return;
        std::string part = std::string(what) + " " + percentText(frac) + " % de la hauteur";
        if (refH > 0) part += " (environ " + std::to_string(static_cast<int>(std::lround(frac * refH))) + " px sur " + std::to_string(refH) + ")";
        part += ", " + hex(color) + (accent < 0 ? std::string{} : " et " + hex(static_cast<std::uint32_t>(accent)));
        if (t.bands.views > 1) part += ", commun \xC3\xA0 " + std::to_string(shared) + " vues sur " + std::to_string(t.bands.views);
        add(part);
    };
    band(t.bands.header, t.bands.headerViews, t.bands.headerColor, t.bands.headerAccent, "en-t\xC3\xAAte");
    band(t.bands.footer, t.bands.footerViews, t.bands.footerColor, t.bands.footerAccent, "bandeau du bas");
    return s;
}

std::string summaryText(const Attached& f, bool tool) {
    const Template& t = f.tpl;
    std::string s = "[Mod\xC3\xA8le de vues " + guill(f.name) + (t.kind == "pv" ? std::string(" (un PV d'approbation de vues)") : std::string(" (une capture d'\xC3\xA9" "cran)"))
                    + ", lu par le logiciel (r\xC3\xA8gle 24) : il donne la FORME \xC3\xA0 reproduire (vues, disposition, couleurs, symboles, "
                      "en-t\xC3\xAAte, pied, navigation) ; les DONN\xC3\x89" "ES viennent du projet (variables IHM, automate lu, reports, types), "
                      "jamais invent\xC3\xA9" "es.]\n";
    if (!t.doc.number.empty() || !t.doc.title.empty() || !t.doc.revision.empty()) {
        s += "Document :";
        if (!t.doc.number.empty()) s += " n\xC2\xB0 " + t.doc.number;
        if (!t.doc.title.empty()) s += " \xE2\x80\x94 " + t.doc.title;
        if (!t.doc.revision.empty())
            s += " \xE2\x80\x94 r\xC3\xA9vision " + t.doc.revision + (t.doc.revisionDate.empty() ? std::string{} : " du " + t.doc.revisionDate)
                 + (t.doc.revisionText.empty() ? std::string{} : " (" + t.doc.revisionText + ")");
        s += ".\n";
    }
    std::size_t fix = 0;
    for (const auto& v : t.views) fix += toFix(v) ? 1 : 0;
    s += "Vues (" + std::to_string(t.views.size()) + (fix ? ", dont " + std::to_string(fix) + " \xC3\xA0 corriger ou comment\xC3\xA9" "es" : std::string{}) + ") :\n";
    for (std::size_t i = 0; i < t.views.size(); ++i) {
        const View& v = t.views[i];
        s += std::to_string(i + 1) + ". " + v.name;
        std::string where;
        if (v.page > 0) where = "page " + std::to_string(v.page);
        if (v.screenW > 0) where += (where.empty() ? "" : ", ") + ratioText(v.screenW, v.screenH) + ", environ " + std::to_string(v.screenW) + " x " + std::to_string(v.screenH);
        if (!where.empty()) s += " (" + where + ")";
        if (t.kind == "pv") s += " ; approbation : " + approvalText(v.approval);
        if (!v.comments.empty()) s += " ; COMMENTAIRES \xC3\xA0 appliquer : " + guill(oneLine(v.comments, 300));
        else if (v.handwritten) s += " ; commentaires annot\xC3\xA9s \xC3\xA0 la main (regarde leur page : voir_vue_modele, commentaires)";
        s += "\n";
    }
    if (const std::string c = charteText(t); !c.empty())
        s += "Charte (calcul\xC3\xA9" "e sur les images ; les r\xC3\xB4les sont PROBABLES, confirme-les sur l'image) : " + c + ".\n";
    // COMMENT LE REPRODUIRE (la regle 24 en detail : seulement quand un modele est joint)
    s += "Comment le reproduire : chaque vue du mod\xC3\xA8le devient une vue du projet (creer_vue, son nom ; ou la vue du m\xC3\xAAme nom "
         "compl\xC3\xA9t\xC3\xA9" "e), mise \xC3\xA0 l'\xC3\xA9" "chelle des vues du projet. Chaque objet (un ventilateur VE101, une valeur 42.0 %, un "
         "registre, un bouton Inhib\xC3\xA9, un d\xC3\xA9" "faut) se relie \xC3\xA0 la variable dont le nom ou la d\xC3\xA9signation porte son rep\xC3\xA8re "
         "(VE101, RG101) ; sans elle : NON RELI\xC3\x89, et << \xC3\xA0 relier >> dans ta r\xC3\xA9ponse (sa variable cr\xC3\xA9\xC3\xA9" "e seulement si la "
         "demande le veut). Les boutons entre vues : l'action Ouvrir la vue ; les couleurs de la charte dans les objets. Les COMMENTAIRES "
         "sont des demandes de changement : applique-les d'abord et dis-les ; une vue NON se corrige d'apr\xC3\xA8s eux. 10 vues et plus : "
         "conseille une mission (l'en-t\xC3\xAAte, le pied et la charte, puis une \xC3\xA9tape par vue).\n";
    if (t.bands.header > 0 && t.bands.headerViews > 1)
        s += "L'en-t\xC3\xAAte commun se fait UNE fois, en vue en-t\xC3\xAAte (et le bandeau du bas en pied), coch\xC3\xA9" "e sur les vues "
             "(creer_vue role entete / pied, puis modifier_vue entete, pied) - pas dans chaque vue.\n";
    for (const auto& n : t.notes) s += "Note : " + n + ".\n";
    if (tool)
        s += "L'image recadr\xC3\xA9" "e d'une vue : l'outil voir_vue_modele (vue : son nom ou son num\xC3\xA9ro) - seulement celle que tu "
             "reproduis.";
    else
        s += "Les images des vues sont jointes au tour de la demande qui reproduit.";
    s += " Calque de r\xC3\xA9" "f\xC3\xA9rence (si l'utilisateur le veut) : creer_ressource piece_jointe \"" + f.name + " > <vue>\""
         + ", puis un objet Image de la vue en code brut (verrou=1, opacity 30, visible FALSE : vu dans l'\xC3\xA9" "diteur, masqu\xC3\xA9 en marche).";
    return s;
}

std::string templatesPrompt(const std::vector<Attached>& files, bool tool) {
    if (files.empty()) return {};
    std::string s = "## Mod\xC3\xA8les de vues joints (r\xC3\xA8gle 24)\n";
    for (const auto& f : files) {
        std::size_t fix = 0;
        for (const auto& v : f.tpl.views) fix += toFix(v) ? 1 : 0;
        s += "- " + guill(f.name) + " : " + std::to_string(f.tpl.views.size()) + (f.tpl.views.size() > 1 ? " vues" : " vue")
             + (fix ? " (dont " + std::to_string(fix) + " \xC3\xA0 corriger)" : std::string{}) + "\n";
    }
    s += "Leur r\xC3\xA9sum\xC3\xA9 et leur charte sont dans le message qui les a joints. "
         + std::string(tool ? "Une vue en image : voir_vue_modele." : "Leurs images partent avec la demande qui reproduit.") + "\n";
    return s;
}

std::string viewToolJson() {
    return "{\"name\":\"voir_vue_modele\",\"description\":\"Montre l'IMAGE recadr\xC3\xA9" "e d'une vue d'un mod\xC3\xA8le de vues joint (un PV "
           "d'approbation, une capture) : pour la reproduire (disposition, couleurs, symboles) avec les donn\xC3\xA9" "es du projet. Une vue "
           "\xC3\xA0 la fois, seulement celle que tu reproduis ou corriges.\",\"input_schema\":{\"type\":\"object\",\"properties\":{\"vue\":{"
           "\"type\":\"string\",\"description\":\"le nom de la vue (VUE : <nom>) ou son num\xC3\xA9ro (1, 2...)\"},\"piece\":{\"type\":\"string\","
           "\"description\":\"le nom du fichier joint, s'il y a plusieurs mod\xC3\xA8les\"},\"commentaires\":{\"type\":\"boolean\",\"description\":"
           "\"true : l'image de sa page COMMENTAIRES (annot\xC3\xA9" "e \xC3\xA0 la main)\"}},\"required\":[\"vue\"]}}";
}

LocalToolReply runViewTool(const std::vector<Attached>& files, std::string_view inputJson) {
    LocalToolReply out;
    json::Value in;
    if (!json::parse(inputJson, in) || !in.isObject()) {
        out.text = "Entr\xC3\xA9" "e illisible : {\"vue\": \"<nom ou num\xC3\xA9ro>\"}.";
        return out;
    }
    const Attached* f = fileFor(files, in.str("piece"));
    if (!f) {
        out.text = files.empty() ? std::string("Aucun mod\xC3\xA8le de vues n'est joint \xC3\xA0 cette conversation.")
                                 : "Pi\xC3\xA8" "ce introuvable : " + guill(in.str("piece")) + ".";
        return out;
    }
    std::string want = in.str("vue");
    if (want.empty())
        if (const json::Value* n = in.find("vue"); n && n->isNumber()) want = std::to_string(n->asInt());
    const View* v = f->tpl.view(want);
    if (!v) {
        out.text = "Vue introuvable dans " + guill(f->name) + " : " + guill(want) + ". Les vues : " + viewsList(f->tpl) + ".";
        return out;
    }
    const std::size_t index = static_cast<std::size_t>(v - f->tpl.views.data());
    const std::string ext = extOf(f->name);
    const std::string folder = cacheFolder(f->id);
    std::string path, why;
    std::string name = v->name;
    for (auto& c : name)
        if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) c = '_';
    if (in.flag("commentaires", false)) {
        if (v->commentsPage <= 0 || ext != "pdf") {
            out.text = "La vue " + guill(v->name) + " n'a pas de page de commentaires.";
            return out;
        }
        path = folder.empty() ? std::string{} : detail::join(folder, "commentaires-" + std::to_string(v->commentsPage) + ".png");
        if (!path.empty() && !exists(path)) {
            View page = *v;
            page.page = v->commentsPage;
            page.screen = {};
            (void)renderView(f->path, ext, page, path, &why);
        }
        name += " (commentaires)";
    } else {
        path = folder.empty() || v->image.empty() ? std::string{} : detail::join(folder, v->image);
        if (!path.empty() && !exists(path)) (void)renderView(f->path, ext, *v, path, &why);
    }
    out.text = "Vue " + std::to_string(index + 1) + " sur " + std::to_string(f->tpl.views.size()) + " : " + guill(v->name);
    std::string where;
    if (v->page > 0) where = "page " + std::to_string(v->page) + " de " + guill(f->name);
    if (v->screenW > 0) where += (where.empty() ? "" : " ; ") + std::string("l'\xC3\xA9" "cran d'origine environ ") + std::to_string(v->screenW) + " x " + std::to_string(v->screenH) + ", "
                                 + ratioText(v->screenW, v->screenH);
    if (!where.empty()) out.text += " (" + where + ")";
    out.text += ".";
    if (f->tpl.kind == "pv") out.text += " Approbation : " + approvalText(v->approval) + ".";
    if (!v->comments.empty()) out.text += " COMMENTAIRES \xC3\xA0 appliquer en priorit\xC3\xA9 : " + guill(v->comments) + ".";
    if (!v->palette.empty()) {
        std::string cols;
        for (std::size_t i = 0; i < v->palette.colors.size() && i < 8; ++i)
            cols += (i ? ", " : "") + hex(v->palette.colors[i].rgb) + " " + percentText(v->palette.colors[i].share) + " %";
        out.text += " Ses couleurs : " + cols + ".";
    }
    if (path.empty() || !exists(path)) {
        out.text += " L'image n'est pas disponible" + (why.empty() ? std::string{} : " (" + why + ")") + " : demande \xC3\xA0 l'utilisateur de rejoindre le fichier.";
        return out;
    }
    out.text += " L'image suit (" + std::to_string(v->width) + " x " + std::to_string(v->height)
                + " px) : reproduis la FORME avec les DONN\xC3\x89" "ES du projet (r\xC3\xA8gle 24) ; ce qui n'a pas de donn\xC3\xA9" "e reste non reli\xC3\xA9 et se dit.";
    ProviderAttachment img;
    img.name = name + ".png";
    img.mediaType = "image/png";
    img.path = path;
    img.bytes = sizeOf(path);
    out.images.push_back(std::move(img));
    return out;
}

std::vector<ProviderAttachment> imagesFor(const Attached& f, std::size_t max) {
    std::vector<ProviderAttachment> out;
    const std::string folder = cacheFolder(f.id);
    for (const auto& v : f.tpl.views) {
        if (out.size() >= max) break;
        if (folder.empty() || v.image.empty()) continue;
        const std::string path = detail::join(folder, v.image);
        if (!exists(path)) continue;
        ProviderAttachment a;
        a.name = v.name + ".png";
        for (auto& c : a.name)
            if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) c = '_';
        a.mediaType = "image/png";
        a.path = path;
        a.bytes = sizeOf(path);
        out.push_back(std::move(a));
    }
    return out;
}

bool wantsReproduction(std::string_view request) {
    const std::string f = fold(request);
    static constexpr std::string_view kWords[] = {"reprodui", "repliqu", "modele de vue", "modeles de vue", "modele des vues", "a l'identique",
                                                  "meme charte", "meme style", "memes couleurs", "approbation de vue", "approbation des vues",
                                                  "d'apres le modele", "d'apres la capture", "comme la capture", "comme sur la capture",
                                                  "calque de reference", "charte graphique"};
    return std::any_of(std::begin(kWords), std::end(kWords), [&](std::string_view w) { return contains(f, w); });
}

std::string imagePath(const Attached& f, std::string_view reference) {
    const std::size_t gt = reference.rfind('>');
    if (gt == std::string_view::npos) return {};
    if (fold(trim(reference.substr(0, gt))) != fold(f.name)) return {};
    const View* v = f.tpl.view(trim(reference.substr(gt + 1)));
    const std::string folder = cacheFolder(f.id);
    if (!v || v->image.empty() || folder.empty()) return {};
    const std::string path = detail::join(folder, v->image);
    return exists(path) ? path : std::string{};
}

std::vector<std::pair<std::string, std::string>> imageNames(const Attached& f) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string folder = cacheFolder(f.id);
    if (folder.empty()) return out;
    for (std::size_t i = 0; i < f.tpl.views.size(); ++i) {
        const View& v = f.tpl.views[i];
        if (v.image.empty()) continue;
        const std::string path = detail::join(folder, v.image);
        if (!exists(path)) continue;
        out.emplace_back(f.name + " > " + v.name, path);
        out.emplace_back(f.name + " > " + std::to_string(i + 1), path);
    }
    return out;
}

std::vector<std::string> toLink(const Proposal& p) {
    std::vector<std::string> out;
    std::set<std::string> bound;
    for (const auto& op : p.ops) {
        if (op.action == Operation::Action::Bind && !trim(op.variable).empty()) bound.insert(op.name);
        if (op.action == Operation::Action::Modify)
            for (const auto& [k, val] : op.props)
                if ((k == "variable" && !trim(val).empty()) || (!val.empty() && val.front() == '=')) bound.insert(op.name);
    }
    for (const auto& op : p.ops) {
        if (op.action != Operation::Action::Create || op.name.empty() || bound.count(op.name)) continue;
        const KindEntry* e = registry().find(op.kind);
        if (!e || !e->prop("variable") || !(e->shows || e->writes || e->synoptic)) continue;
        bool linked = false;
        for (const auto& [k, val] : op.props)
            if ((k == "variable" && !trim(val).empty()) || (!val.empty() && val.front() == '=')) linked = true;
        if (!linked && std::find(out.begin(), out.end(), op.name) == out.end()) out.push_back(op.name);
    }
    return out;
}

std::string toLinkNote(const std::vector<std::string>& names) {
    if (names.empty()) return {};
    std::string list;
    for (std::size_t i = 0; i < names.size() && i < 12; ++i) list += (i ? ", " : "") + names[i];
    if (names.size() > 12) list += ", \xE2\x80\xA6";
    return "\xC3\x80 relier (" + std::to_string(names.size()) + (names.size() > 1 ? " objets" : " objet")
           + " sans donn\xC3\xA9" "e du projet, rien n'est invent\xC3\xA9) : " + list
           + ". Associe-les \xC3\xA0 des variables existantes, ou demande leur cr\xC3\xA9" "ation.";
}

} // namespace app::ai::viewtpl
