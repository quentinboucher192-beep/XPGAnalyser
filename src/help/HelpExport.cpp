// =============================================================================
//  help/HelpExport.cpp
// =============================================================================
#include "HelpExport.hpp"

#include "HelpCodes.hpp"
#include "HelpIndex.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace help {
namespace {

// ---------------------------------------------------------------- outillage --

bool endsSentence(char c) noexcept {
    return c == '.' || c == '!' || c == '?' || c == ':' || c == ';';
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= ' ') ++a;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= ' ') --b;
    return std::string(s.substr(a, b - a));
}

// Recompose une prose pre-coupee. C'est la meme regle que app::reflow, et elle
// est ici plutot que partagee parce que HelpExport ne doit rien devoir a app/ :
// une ligne qui ne finit pas par une ponctuation forte est la suite de la
// suivante ; une ligne vide separe deux paragraphes ; une ligne indentee est du
// code et reste telle quelle.
std::vector<std::string> paragraphs(std::string_view text) {
    std::vector<std::string> out;
    std::string              current;
    std::size_t              i = 0;
    while (i <= text.size()) {
        const std::size_t nl   = text.find('\n', i);
        const bool        last = nl == std::string_view::npos;
        std::string_view  raw  = text.substr(i, (last ? text.size() : nl) - i);
        const std::string line = trimmed(raw);

        if (line.empty()) {
            if (!current.empty()) { out.push_back(current); current.clear(); }
        } else {
            if (!current.empty()) {
                const char prev = current.back();
                // Une puce commence toujours un paragraphe : sinon la liste des
                // etapes d'une macro se recolle en un bloc illisible.
                if (line[0] == '-' || line[0] == '*' || endsSentence(prev)) {
                    out.push_back(current);
                    current = line;
                } else {
                    current += ' ';
                    current += line;
                }
            } else {
                current = line;
            }
        }
        if (last) break;
        i = nl + 1;
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string today() {
    const std::time_t t  = std::time(nullptr);
    std::tm           tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

// La feuille de style. Elle est en dur, en un bloc, et c'est le prix a payer
// pour un fichier qui s'ouvre partout sans rien aller chercher.
//
// Les choix qui comptent sont tous dans @media print : les couleurs de fond
// disparaissent (une imprimante a jet vide une cartouche sur un a-plat gris),
// un chapitre commence sur une page neuve, et une entree ne se coupe pas en
// deux si elle tient sur une page - un parametre separe de son tableau est un
// parametre qu'on relit trois fois.
constexpr const char* kStyle = R"CSS(
:root{--txt:#1b1e23;--muted:#666f7a;--line:#dde2e8;--accent:#1f6feb;--warn:#8a5a00;
      --code:#f5f7fa;--codeline:#e3e8ee;--bg:#ffffff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);
     font:15px/1.55 -apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif}
.wrap{max-width:52rem;margin:0 auto;padding:2.2rem 1.4rem 5rem}
h1{font-size:1.9rem;line-height:1.2;margin:.2rem 0 .4rem}
h2{font-size:1.35rem;margin:2.4rem 0 .2rem;padding-bottom:.25rem;border-bottom:1px solid var(--line)}
h3{font-size:1.05rem;margin:1.5rem 0 .35rem;color:var(--muted);
   text-transform:uppercase;letter-spacing:.06em;font-weight:600}
p{margin:.55rem 0}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}
.cover{border-bottom:2px solid var(--line);padding-bottom:1.4rem;margin-bottom:1.2rem}
.cover .sub{color:var(--muted);font-size:1.05rem}
.meta{margin-top:.9rem;font-size:.9rem;color:var(--muted)}
.meta b{color:var(--txt);font-weight:600}
.lead{font-size:1.05rem;color:#2c3138;margin:.5rem 0 .2rem}
.sub{color:var(--muted);font-size:.9rem;margin:.1rem 0 .6rem}
pre{background:var(--code);border:1px solid var(--codeline);border-radius:5px;
    padding:.7rem .85rem;overflow-x:auto;
    font:13px/1.45 "Cascadia Mono",Consolas,"DejaVu Sans Mono",monospace;margin:.5rem 0}
code{font:13px/1.4 "Cascadia Mono",Consolas,"DejaVu Sans Mono",monospace;
     background:var(--code);padding:.05rem .3rem;border-radius:3px}
table{border-collapse:collapse;width:100%;margin:.5rem 0;font-size:.92rem}
th,td{text-align:left;vertical-align:top;padding:.34rem .5rem;border-bottom:1px solid var(--line)}
th{color:var(--muted);font-weight:600;font-size:.82rem;text-transform:uppercase;letter-spacing:.04em}
td.n{white-space:nowrap;font-family:"Cascadia Mono",Consolas,monospace}
td.t{white-space:nowrap;color:var(--muted);font-family:"Cascadia Mono",Consolas,monospace}
td.s{white-space:nowrap;color:var(--muted);font-size:.85rem}
.entry{margin-bottom:.6rem}
.chips{margin:.45rem 0;font-size:.9rem;color:var(--muted)}
.chips a{margin-right:.15rem}
.note{border-left:3px solid #e0a72c;background:#fdf7e8;padding:.5rem .8rem;margin:.6rem 0;
      color:var(--warn)}
.todo{color:var(--muted);font-style:italic}
.hero{border-left:4px solid var(--accent);background:#f4f7fb;border-radius:10px;
      padding:.9rem 1.1rem;margin:.2rem 0 1rem}
.hero h1{margin:.1rem 0 .2rem}
.crumb{font-size:.85rem;color:var(--muted)}
.pill{display:inline-block;font-size:.78rem;padding:.08rem .5rem;border-radius:1rem;
      border:1px solid var(--line);color:var(--muted);margin:0 .25rem .2rem 0;vertical-align:.15em}
.card{border:1px solid var(--line);border-radius:8px;padding:.55rem .8rem;margin:.5rem 0;
      break-inside:avoid}
.callout{border-left:3px solid #1f6feb;background:#eef4fd;padding:.5rem .8rem;margin:.6rem 0;border-radius:4px}
.callout.warn{border-color:#e0a72c;background:#fdf7e8;color:var(--warn)}
.callout.err{border-color:#c42b1c;background:#fdeceb;color:#8f1d12}
ul.items{margin:.4rem 0 .6rem;padding-left:1.4rem}
ul.items li{margin:.25rem 0}
.ring{font-weight:600}
.toc{columns:2;column-gap:2.2rem;font-size:.93rem;margin:.6rem 0 0}
.toc h3{column-span:all}
.toc ul{list-style:none;margin:.2rem 0 1rem;padding:0;break-inside:avoid}
.toc li{padding:.08rem 0}
.sev{display:inline-block;font-size:.72rem;font-weight:700;letter-spacing:.05em;
     padding:.08rem .38rem;border-radius:3px;vertical-align:.08em;margin-right:.4rem}
.sev.err{background:#fdecec;color:#a11}
.sev.warn{background:#fdf3e2;color:#8a5a00}
.sev.info{background:#eaf2fd;color:#1f5bb5}
dl.glo dt{font-weight:600;margin-top:.7rem}
dl.glo dd{margin:.1rem 0 0;color:#2c3138}
footer{margin-top:3rem;padding-top:.8rem;border-top:1px solid var(--line);
       color:var(--muted);font-size:.82rem}
@media print{
  :root{--code:#fff;--bg:#fff}
  @page{margin:16mm 14mm}
  body{font-size:10.5pt}
  .wrap{max-width:none;padding:0}
  a{color:inherit;text-decoration:none}
  .chapter{break-before:page}
  .entry,tr,.note{break-inside:avoid}
  h2,h3{break-after:avoid}
  pre{background:#fff;border:1px solid #bbb}
  .noprint{display:none}
}
)CSS";

// ------------------------------------------------------------ les fragments --

void openDoc(std::ostream& o, std::string_view title) {
    o << "<!doctype html>\n<html lang=\"fr\">\n<head>\n<meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
      << "<title>" << escapeHtml(title) << "</title>\n<style>" << kStyle << "</style>\n"
      << "</head>\n<body>\n<div class=\"wrap\">\n";
}

void closeDoc(std::ostream& o) { o << "</div>\n</body>\n</html>\n"; }

void writeParagraphs(std::ostream& o, std::string_view text, const char* cls = nullptr) {
    for (const auto& p : paragraphs(text)) {
        o << "<p";
        if (cls) o << " class=\"" << cls << '"';
        o << '>' << escapeHtml(p) << "</p>\n";
    }
}

// La decoupe d'un `#! see` est celle de HelpIndex (seeNames) : les liens du
// document et les renvois calcules doivent designer les memes choses.
std::vector<std::string> splitSee(const std::vector<std::string>& see) {
    project::LibraryHelp h;
    h.see = see;
    return seeNames(h);
}

// Les renvois, en liant ce qui existe et en laissant le reste en clair. Un
// `#! see` peut nommer une chose qui n'est pas dans ce dossier - un bloc
// d'une autre bibliotheque, une macro non exportee - et la reponse n'est pas
// de la taire, c'est de ne pas la rendre cliquable.
void writeChips(std::ostream& o, std::string_view label,
                const std::vector<std::string>& names,
                const std::vector<std::string>& known) {
    if (names.empty()) return;
    o << "<p class=\"chips\"><b>" << escapeHtml(label) << "</b> &nbsp;";
    bool first = true;
    for (const auto& n : names) {
        if (!first) o << " &middot; ";
        first = false;
        const std::string a = anchorFor(n);
        if (std::find(known.begin(), known.end(), a) != known.end())
            o << "<a href=\"#" << a << "\">" << escapeHtml(n) << "</a>";
        else
            o << escapeHtml(n);
    }
    o << "</p>\n";
}

std::string_view severityClass(Severity s) {
    switch (s) {
        case Severity::Error:   return "err";
        case Severity::Warning: return "warn";
        default:                return "info";
    }
}
std::string_view severityLabel(Severity s) {
    switch (s) {
        case Severity::Error:   return "ERREUR";
        case Severity::Warning: return "ALERTE";
        default:                return "INFO";
    }
}

// ------------------------------------------------- une entree, pour le papier --

void writeEntry(std::ostream& o, const project::CatalogEntry& e,
                const std::vector<project::CatalogEntry>& library,
                const DossierOptions& opt, const std::vector<std::string>& known) {
    o << "<div class=\"entry\">\n";
    o << "<h2 id=\"" << anchorFor(e.name) << "\">" << escapeHtml(e.name) << "</h2>\n";

    {
        std::ostringstream sub;
        sub << project::kindLabel(e.kind) << " &middot; " << escapeHtml(e.category);
        if (!e.version.empty()) sub << " &middot; version " << escapeHtml(e.version);
        if (!e.fileName.empty()) sub << " &middot; " << escapeHtml(e.fileName);
        o << "<p class=\"sub\">" << sub.str() << "</p>\n";
    }

    if (!e.help.summary.empty())
        writeParagraphs(o, e.help.summary, "lead");
    else
        o << "<p class=\"todo\">Pas encore de resume.</p>\n";

    if (!e.help.usage.empty()) {
        o << "<h3>Utilisation</h3>\n";
        writeParagraphs(o, e.help.usage);
    }

    if (!e.help.example.empty()) {
        o << "<h3>Exemple</h3>\n<pre>" << escapeHtml(e.help.example) << "</pre>\n";
    }

    // Les parametres en tableau. Sur papier c'est la forme qui se consulte :
    // on cherche UNE ligne, on ne lit pas la page.
    std::vector<const project::Declaration*> publics;
    for (const auto& d : e.declarations)
        if (!d.isLocal()) publics.push_back(&d);

    if (!publics.empty()) {
        const bool macro = e.kind == project::CatalogKind::Macro;
        o << "<h3>" << (macro ? "Questions posees" : "Parametres") << "</h3>\n"
          << "<table><thead><tr><th>Nom</th><th>Type</th><th>"
          << (macro ? "Defaut" : "Portee") << "</th><th>Description</th></tr></thead><tbody>\n";
        for (const auto* d : publics) {
            const std::string* longHelp = e.help.param(d->name);
            o << "<tr><td class=\"n\">" << escapeHtml(d->name) << "</td>"
              << "<td class=\"t\">" << escapeHtml(d->type) << "</td>"
              << "<td class=\"s\">"
              << escapeHtml(macro ? d->initial : d->scope) << "</td><td>";
            if (!d->comment.empty()) o << escapeHtml(d->comment);
            if (longHelp && !longHelp->empty()) {
                if (!d->comment.empty()) o << "<br>";
                bool first = true;
                for (const auto& p : paragraphs(*longHelp)) {
                    if (!first) o << "<br>";
                    o << escapeHtml(p);
                    first = false;
                }
            } else if (d->comment.empty()) {
                o << "<span class=\"todo\">a documenter</span>";
            }
            o << "</td></tr>\n";
        }
        o << "</tbody></table>\n";
    }

    if (!e.faultTable.empty()) {
        o << "<h3>Codes de defaut</h3>\n"
          << "<table><thead><tr><th>Code</th><th>Ce que ca veut dire</th></tr></thead><tbody>\n";
        for (const auto& f : e.faultTable) {
            o << "<tr><td class=\"n\">" << escapeHtml(f.code) << "</td><td>"
              << escapeHtml(f.text);
            if (const std::string* h = e.help.fault(f.code); h && !h->empty()) {
                o << "<br>";
                bool first = true;
                for (const auto& p : paragraphs(*h)) {
                    if (!first) o << "<br>";
                    o << escapeHtml(p);
                    first = false;
                }
            }
            o << "</td></tr>\n";
        }
        o << "</tbody></table>\n";
    }

    writeChips(o, "Voir aussi", splitSee(e.help.see), known);

    // « Appele par », calcule. C'est ce que le papier ne peut pas obtenir en
    // cliquant, donc c'est ce qu'il faut y imprimer.
    if (opt.includeBacklinks) {
        std::vector<std::string> names;
        for (const auto& b : backlinks(library, e.name)) names.push_back(b.name);
        writeChips(o, "Utilise par", names, known);
    }

    if (opt.includeSource && !e.declarations.empty()) {
        o << "<h3>Declarations</h3>\n<pre>";
        for (const auto& d : e.declarations) {
            o << escapeHtml(d.name) << " ; " << escapeHtml(d.type) << " ; "
              << escapeHtml(d.scope);
            if (!d.initial.empty()) o << " ; " << escapeHtml(d.initial);
            if (!d.comment.empty()) o << " ; " << escapeHtml(d.comment);
            o << '\n';
        }
        o << "</pre>\n";
    }

    if (!e.warnings.empty()) {
        o << "<div class=\"note\"><b>A verifier</b><br>";
        bool first = true;
        for (const auto& w : e.warnings) {
            if (!first) o << "<br>";
            o << escapeHtml(w);
            first = false;
        }
        o << "</div>\n";
    }

    o << "</div>\n";
}

} // namespace

// ------------------------------------------------------------ l'echappement --
std::string escapeHtml(std::string_view s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (const char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string anchorFor(std::string_view name) {
    std::string out = "e-";
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))      out += static_cast<char>(std::tolower(u));
        else if (c == '_' || c == '-') out += c;
        else                      out += '-';
    }
    return out;
}

// --------------------------------------------------------------- une page ----
std::string renderArticleHtml(const ui::HelpArticle& a, std::string_view title) {
    std::ostringstream o;
    openDoc(o, title);

    for (const auto& b : a.blocks) {
        using K = ui::HelpBlockKind;
        switch (b.kind) {
            case K::Title:     o << "<h1>" << escapeHtml(b.text) << "</h1>\n"; break;
            case K::Subtitle:  o << "<p class=\"sub\">" << escapeHtml(b.text) << "</p>\n"; break;
            case K::Lead:      writeParagraphs(o, b.text, "lead"); break;
            case K::Heading:   o << "<h3>" << escapeHtml(b.text) << "</h3>\n"; break;
            case K::Paragraph: writeParagraphs(o, b.text); break;
            case K::Code:      o << "<pre>" << escapeHtml(b.text) << "</pre>\n"; break;
            case K::Term:
                o << (b.pills.empty() ? "<p>" : "<div class=\"card\">")
                  << "<b><code>" << escapeHtml(b.label) << "</code></b>";
                if (!b.pills.empty()) {
                    for (const auto& p : b.pills)
                        o << " <span class=\"pill\">" << escapeHtml(p.text) << "</span>";
                } else if (!b.detail.empty()) {
                    o << " <span class=\"sub\">" << escapeHtml(b.detail) << "</span>";
                }
                o << "<br>";
                if (!b.extra.empty()) o << "<span class=\"sub\">" << escapeHtml(b.extra) << "</span><br>";
                if (b.text.empty()) o << "<span class=\"todo\">à documenter</span>";
                else                o << escapeHtml(b.text);
                o << (b.pills.empty() ? "</p>\n" : "</div>\n");
                break;
            case K::Fault:
                o << "<p><b><code>" << escapeHtml(b.label) << "</code></b> &nbsp;"
                  << escapeHtml(b.text) << "</p>\n";
                break;
            case K::Chips: {
                o << "<p class=\"chips\">";
                if (!b.label.empty()) o << "<b>" << escapeHtml(b.label) << "</b> &nbsp;";
                o << escapeHtml(b.text) << "</p>\n";
                break;
            }
            case K::Note:
                o << "<div class=\"note\">" << escapeHtml(b.text) << "</div>\n";
                break;
            case K::Meter: {
                const int pct = static_cast<int>(b.value * 100.f + .5f);
                o << "<p class=\"sub\">" << escapeHtml(b.label);
                if (!b.label.empty()) o << " &middot; ";
                o << pct << "&nbsp;%</p>\n";
                break;
            }
            case K::Separator: o << "<hr>\n"; break;

            // --- les blocs de la page refaite ----------------------------------
            // Un bloc que l'export ne connaitrait pas disparaitrait de la page
            // imprimee sans que rien ne le dise : l'en-tete porte le NOM, et
            // une impression sans titre est une impression ratee.
            case K::Hero: {
                o << "<div class=\"hero\">";
                if (!b.links.empty()) {
                    o << "<div class=\"crumb\">";
                    for (std::size_t i = 0; i < b.links.size(); ++i) {
                        if (i) o << " / ";
                        o << escapeHtml(b.links[i].text);
                    }
                    o << "</div>";
                }
                o << "<h1>" << escapeHtml(b.text);
                for (const auto& p : b.pills) o << " <span class=\"pill\">" << escapeHtml(p.text) << "</span>";
                o << "</h1>";
                if (!b.label.empty()) o << "<div class=\"sub\">" << escapeHtml(b.label) << "</div>";
                o << "</div>\n";
                break;
            }
            case K::Callout: {
                const char* cls = b.severity >= 2 ? "callout err" : b.severity == 1 ? "callout warn" : "callout";
                o << "<div class=\"" << cls << "\">";
                if (!b.label.empty()) o << "<b>" << escapeHtml(b.label) << "</b><br>";
                o << escapeHtml(b.text) << "</div>\n";
                break;
            }
            case K::Bullet:
                o << "<ul class=\"items\"><li>";
                if (!b.label.empty() && b.label != "-") o << "<b>" << escapeHtml(b.label) << "</b> ";
                if (!b.detail.empty()) o << "<b>" << escapeHtml(b.detail) << "</b> : ";
                o << escapeHtml(b.text) << "</li></ul>\n";
                break;
            case K::Links: {
                o << "<p class=\"chips\">";
                if (!b.label.empty()) o << "<b>" << escapeHtml(b.label) << "</b> &nbsp;";
                for (std::size_t i = 0; i < b.links.size(); ++i) {
                    if (i) o << " &middot; ";
                    o << escapeHtml(b.links[i].text);
                }
                o << "</p>\n";
                break;
            }
            case K::Ring: {
                const int pct = static_cast<int>(std::clamp(b.value, 0.f, 1.f) * 100.f + .5f);
                o << "<p><span class=\"ring\">" << escapeHtml(b.label) << " &middot; " << pct
                  << "&nbsp;%</span> &nbsp;" << escapeHtml(b.text);
                for (std::size_t i = 0; i < b.links.size(); ++i)
                    o << (i ? ", " : " ") << "<code>" << escapeHtml(b.links[i].text) << "</code>";
                o << "</p>\n";
                break;
            }
            case K::Empty:
                o << "<p class=\"sub\">" << escapeHtml(b.text) << "</p>\n";
                break;
            case K::Table: {
                o << "<table>";
                std::size_t start = 0;
                const auto& t = b.text;
                for (std::size_t i = 0; i <= t.size(); ++i) {
                    if (i != t.size() && t[i] != '\n') continue;
                    const std::string_view row(t.data() + start, i - start);
                    start = i + 1;
                    if (row.empty()) continue;
                    o << "<tr>";
                    std::size_t cs = 0;
                    bool firstCell = true;
                    for (std::size_t j = 0; j <= row.size(); ++j) {
                        if (j != row.size() && row[j] != '\t') continue;
                        o << (firstCell ? "<td class=\"n\">" : "<td>")
                          << escapeHtml(row.substr(cs, j - cs)) << "</td>";
                        firstCell = false;
                        cs = j + 1;
                    }
                    o << "</tr>";
                }
                o << "</table>\n";
                break;
            }
        }
    }

    o << "<footer>Imprimé depuis XpgAnalyzer &middot; " << escapeHtml(today())
      << "</footer>\n";
    closeDoc(o);
    return o.str();
}

// ---------------------------------------------------------------- le dossier --
std::string renderDossierHtml(const std::vector<project::CatalogEntry>& library,
                              const DossierInfo& infoIn, const DossierOptions& opt) {
    DossierInfo info = infoIn;
    if (info.date.empty()) info.date = today();

    // Les entrees a sortir, rangees par categorie. `scanLibrary` rend deja trie
    // par categorie puis par nom : on garde cet ordre, parce qu'un sommaire qui
    // ne suit pas l'arbre de l'ecran oblige a chercher deux fois.
    std::vector<std::string>                            categories;
    std::map<std::string, std::vector<const project::CatalogEntry*>> byCategory;
    std::size_t documented = 0, total = 0;

    for (const auto& e : library) {
        if (opt.onlyDocumented && !e.hasHelp) continue;
        const std::string cat = e.category.empty() ? "Divers" : e.category;
        if (byCategory.find(cat) == byCategory.end()) categories.push_back(cat);
        byCategory[cat].push_back(&e);
        total += e.documentableCount();
        documented += e.documentedCount();
    }

    // Tout ce vers quoi on aura le droit de pointer. Un lien qui ne mene nulle
    // part dans un document imprime est un cul-de-sac muet : on le fabrique ici
    // une fois, plutot que d'esperer que chaque `#! see` designe quelque chose.
    std::vector<std::string> known;
    for (const auto& cat : categories) {
        known.push_back("c-" + anchorFor(cat));
        for (const auto* e : byCategory[cat]) known.push_back(anchorFor(e->name));
    }
    if (opt.includeDiagnostics) {
        known.push_back("c-diagnostics");
        for (const auto& c : allCodes()) known.push_back(anchorFor(c.id));
    }
    if (opt.includeGlossary) {
        known.push_back("c-glossaire");
        for (const auto& t : glossary()) known.push_back(anchorFor(t.word));
    }

    const std::string title =
        info.affaire.empty() ? "Aide de la bibliotheque"
                             : "Aide de la bibliotheque - " + info.affaire;

    std::ostringstream o;
    openDoc(o, title);

    // ---- couverture ---------------------------------------------------------
    o << "<section class=\"cover\">\n<h1>Aide de la bibliotheque</h1>\n";
    if (!info.affaire.empty())
        o << "<p class=\"sub\" style=\"font-size:1.1rem\">" << escapeHtml(info.affaire)
          << "</p>\n";
    o << "<p class=\"meta\">";
    std::size_t entries = 0;
    for (const auto& c : categories) entries += byCategory[c].size();
    o << "<b>" << entries << "</b> elements dans <b>" << categories.size()
      << "</b> categories";
    if (total > 0) {
        const int pct = static_cast<int>((100.0 * double(documented) / double(total)) + .5);
        o << " &middot; <b>" << pct << "&nbsp;%</b> des parametres documentes";
    }
    o << "<br>";
    if (!info.projet.empty()) o << "Projet <b>" << escapeHtml(info.projet) << "</b><br>";
    if (!info.libsRoot.empty()) o << "Bibliotheque " << escapeHtml(info.libsRoot) << "<br>";
    if (!info.auteur.empty()) o << escapeHtml(info.auteur) << " &middot; ";
    o << escapeHtml(info.date) << "</p>\n";
    o << "<p class=\"sub noprint\">Ce fichier est complet et autonome : il s'ouvre "
         "sans reseau, se cherche au Ctrl+F, et s'imprime tel quel.</p>\n";
    o << "</section>\n";

    // ---- sommaire -----------------------------------------------------------
    o << "<div class=\"toc\">\n<h3>Sommaire</h3>\n";
    for (const auto& cat : categories) {
        o << "<ul><li><b><a href=\"#c-" << anchorFor(cat) << "\">" << escapeHtml(cat)
          << "</a></b></li>\n";
        for (const auto* e : byCategory[cat])
            o << "<li><a href=\"#" << anchorFor(e->name) << "\">" << escapeHtml(e->name)
              << "</a></li>\n";
        o << "</ul>\n";
    }
    o << "<ul>";
    if (opt.includeDiagnostics)
        o << "<li><b><a href=\"#c-diagnostics\">Codes de diagnostic</a></b></li>";
    if (opt.includeGlossary)
        o << "<li><b><a href=\"#c-glossaire\">Glossaire</a></b></li>";
    o << "</ul>\n</div>\n";

    // ---- les chapitres ------------------------------------------------------
    for (const auto& cat : categories) {
        o << "<section class=\"chapter\">\n<h1 id=\"c-" << anchorFor(cat) << "\">"
          << escapeHtml(cat) << "</h1>\n";
        for (const auto* e : byCategory[cat]) writeEntry(o, *e, library, opt, known);
        o << "</section>\n";
    }

    // ---- les diagnostics ----------------------------------------------------
    if (opt.includeDiagnostics) {
        o << "<section class=\"chapter\">\n<h1 id=\"c-diagnostics\">Codes de "
             "diagnostic</h1>\n"
          << "<p class=\"sub\">Le numero affiche a cote d'un message renvoie ici. "
             "Un numero n'est jamais reattribue.</p>\n";
        for (const auto& c : allCodes()) {
            o << "<div class=\"entry\">\n<h2 id=\"" << anchorFor(c.id) << "\">"
              << "<span class=\"sev " << severityClass(c.severity) << "\">"
              << severityLabel(c.severity) << "</span>" << escapeHtml(c.id) << "</h2>\n"
              << "<p class=\"lead\">" << escapeHtml(c.title) << "</p>\n"
              << "<h3>Pourquoi</h3>\n";
            writeParagraphs(o, c.cause);
            o << "<h3>Ce que ca change</h3>\n";
            writeParagraphs(o, c.effect);
            if (!c.fixes.empty()) {
                o << "<h3>Quoi faire</h3>\n<ol>\n";
                for (const auto& f : c.fixes) o << "<li>" << escapeHtml(f) << "</li>\n";
                o << "</ol>\n";
            }
            {
                std::vector<std::string> names;
                for (const auto& s : c.see) names.emplace_back(s);
                writeChips(o, "Voir aussi", splitSee(names), known);
            }
            o << "</div>\n";
        }
        o << "</section>\n";
    }

    // ---- le glossaire -------------------------------------------------------
    if (opt.includeGlossary) {
        o << "<section class=\"chapter\">\n<h1 id=\"c-glossaire\">Glossaire</h1>\n"
          << "<p class=\"sub\">Le vocabulaire que personne ne demande.</p>\n<dl class=\"glo\">\n";
        for (const auto& t : glossary()) {
            o << "<dt id=\"" << anchorFor(t.word) << "\">" << escapeHtml(t.word)
              << "</dt>\n<dd>" << escapeHtml(t.long_.empty() ? t.short_ : t.long_)
              << "</dd>\n";
        }
        o << "</dl>\n</section>\n";
    }

    o << "<footer>Dossier produit par XpgAnalyzer le " << escapeHtml(info.date)
      << " &middot; " << entries << " elements</footer>\n";
    closeDoc(o);
    return o.str();
}

// ---------------------------------------------------------------- l'ecriture --
std::string writeHtmlFile(const std::string& path, const std::string& html) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return "impossible d'ecrire " + path;
    f.write(html.data(), static_cast<std::streamsize>(html.size()));
    if (!f) return "ecriture interrompue : " + path;
    f.close();
    // On relit ce qu'on croit avoir ecrit : la meme regle que saveHelp. Un
    // disque plein rend un fichier plausible et court, et on s'en apercevrait
    // en l'ouvrant chez le client.
    std::ifstream back(path, std::ios::binary | std::ios::ate);
    if (!back) return "le fichier ecrit ne se relit pas : " + path;
    if (static_cast<std::size_t>(back.tellg()) != html.size())
        return "le fichier ecrit est incomplet : " + path;
    return {};
}

std::string suggestedFileName(const DossierInfo& info) {
    std::string base = "Aide";
    const std::string subject = !info.affaire.empty() ? info.affaire : info.projet;
    if (!subject.empty()) {
        base += '-';
        for (const char c : subject) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u)) base += c;
            else if (!base.empty() && base.back() != '_') base += '_';
        }
        while (!base.empty() && base.back() == '_') base.pop_back();
    }
    base += '-';
    base += info.date.empty() ? today() : info.date;
    base += ".html";
    return base;
}

} // namespace help
