#include "HmiNativesCards.hpp"

#include "HmiPanels.hpp"                // 1.12.1 : le libelle d'une propriete (hmiPropertyInfo)
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiNatives.hpp"
#include "../../hmi/HmiTypes.hpp"     // 1.12.17 : les proprietes d'un conteneur

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>

namespace app::natives {
namespace hn = hmi::natives;
using K = ui::HelpBlockKind;

namespace {

const std::string kDot{"  \xC2\xB7  "};

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

ui::HelpBlock block(K kind, std::string text) {
    ui::HelpBlock b;
    b.kind = kind;
    b.text = std::move(text);
    return b;
}

std::string availText(hn::Avail a) {
    switch (a) {
        case hn::Avail::Same: return "\xE2\x9C\x93 m\xC3\xAAme nom";
        case hn::Avail::Equivalent: return "~ autre \xC3\xA9" "criture";
        case hn::Avail::None: break;
    }
    return "\xE2\x80\x94 aucune";
}
std::string availShort(hn::Avail a) { return a == hn::Avail::Same ? "\xE2\x9C\x93" : a == hn::Avail::Equivalent ? "~" : "\xE2\x80\x94"; }

// L'en-tete d'une fiche : son fil (Natives > ... > elle), ses etiquettes.
ui::HelpBlock hero(const std::string& titleText, std::vector<std::pair<std::string, std::string>> crumbs, std::vector<std::string> pills,
                   ui::Icon icon) {
    auto h = block(K::Hero, titleText);
    h.links.push_back({"Natives", "native:natives", "Le sommaire des natives"});
    for (auto& [text, key] : crumbs) h.links.push_back({text, key.empty() ? std::string{} : "native:" + key, {}});
    h.links.push_back({titleText, {}, {}});
    for (auto& p : pills) h.pills.push_back({std::move(p)});
    h.pills.push_back({"natif \xC2\xB7 verrouill\xC3\xA9", ui::kNoColor, ui::Tone::Muted});
    h.tone = ui::Tone::Accent;
    h.icon = icon;
    return h;
}

// Un exemple en ST, en C, en C++ : un seul cadre, la notation choisie, le selecteur.
ui::HelpBlock notations(const std::string& st, const std::string& c, const std::string& cpp, std::string_view notation) {
    struct N { const char* label; const std::string* text; };
    std::vector<N> shown;
    if (!st.empty()) shown.push_back({"ST", &st});
    if (!c.empty()) shown.push_back({"C", &c});
    if (!cpp.empty()) shown.push_back({"C++", &cpp});
    if (shown.empty()) return block(K::Paragraph, "\xE2\x80\x94");
    const N* pick = &shown.front();
    for (const auto& n : shown)
        if (notation == n.label) pick = &n;
    auto code = block(K::Code, *pick->text);
    code.label = pick->label;
    if (shown.size() > 1)
        for (const auto& n : shown) {
            ui::HelpLink link{n.label, std::string("notation:") + n.label, std::string("En ") + n.label + " (le choix est gard\xC3\xA9 pour toutes les fiches)"};
            if (&n == pick) link.tone = ui::Tone::Accent;
            code.links.push_back(std::move(link));
        }
    return code;
}

ui::HelpBlock links(std::string label, std::vector<ui::HelpLink> items) {
    ui::HelpBlock b;
    b.kind = K::Links;
    b.label = std::move(label);
    b.links = std::move(items);
    return b;
}

std::string exampleLine(const hn::Example& e) {
    std::string code;
    if (!e.prelude.empty()) code += std::string(e.prelude) + "\n";
    code += "x := " + std::string(e.expression) + ";";
    return code;
}

// ---- 1.12.17 : le rangement - les dossiers des fonctions, des types, des enumerations ----
constexpr std::size_t kNoShelf = static_cast<std::size_t>(-1);

std::string shelfKey(std::size_t s) {
    return s == 0 || s >= hn::shelves().size() ? std::string("fonctions") : "dossier:" + hn::shelves()[s].key;
}
std::string shelfTitle(std::size_t s) { return s < hn::shelves().size() ? hn::shelves()[s].title : std::string{}; }

// Le fil d'un dossier des fonctions : Fonctions, puis ses parents (lui-meme s'il le faut).
std::vector<std::pair<std::string, std::string>> shelfCrumbs(std::size_t s, bool self) {
    std::vector<std::size_t> chain;
    for (std::size_t at = s; at != kNoShelf && at < hn::shelves().size();) {
        chain.push_back(at);
        if (at == 0) break;
        at = hn::shelves()[at].parent;
    }
    std::reverse(chain.begin(), chain.end());
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto c : chain)
        if (self || c != s) out.push_back({c == 0 ? std::string("Fonctions") : shelfTitle(c), shelfKey(c)});
    return out;
}

std::string countText(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// La ligne d'une fonction dans un tableau de dossier.
std::string functionRow(const hn::Function& f) {
    return std::string(f.name) + "\t" + hn::shortSignature(f) + "\t" + (f.returns.empty() ? std::string("\xE2\x80\x94") : std::string(f.returns)) + "\t"
         + availShort(f.inC) + "\t" + availShort(f.inCpp) + "\t" + (f.expression ? "\xE2\x9C\x93" : "\xE2\x80\x94");
}
const char* const kFunctionHeader = "Fonction\tSignature\tRend\tC\tC++\t\xC6\x92x";

ui::HelpBlock functionLegend() {
    auto legend = block(K::Paragraph, "C, C++ : \xE2\x9C\x93 le m\xC3\xAAme nom \xC2\xB7 ~ une autre \xC3\xA9" "criture \xC2\xB7 \xE2\x80\x94 pas d'\xC3\xA9quivalent. "
                                      "\xC6\x92x : s'\xC3\xA9" "crit dans une expression de propri\xC3\xA9t\xC3\xA9, un texte \xC3\xA0 trous, une condition.");
    legend.muted = true;
    return legend;
}

// ------------------------------------------------------------------- les fiches ---
ui::HelpArticle functionCard(const hn::Function& f, std::string_view notation) {
    ui::HelpArticle a;
    // 1.12.17 : son dossier (Fonctions > Conteneurs > Listes > ...), l'en-tete C++ en etiquette.
    std::size_t rank = 0;
    while (rank < hn::functions().size() && &hn::functions()[rank] != &f) ++rank;
    const auto home = hn::homeOf(rank);
    std::vector<std::string> pills{f.returns.empty() ? std::string("sans retour") : "rend " + std::string(f.returns), shelfTitle(home)};
    if (const auto header = hn::cppHeader(f); !header.empty()) pills.push_back("C++ " + std::string(header));
    a.blocks.push_back(hero(std::string(f.name), shelfCrumbs(home, true), std::move(pills), ui::Icon::Code));
    a.blocks.push_back(block(K::Lead, std::string(f.summary)));
    a.blocks.push_back(block(K::Heading, "Signature"));
    {
        auto code = block(K::Code, hn::signature(f));
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(block(K::Table, "Langage\tDisponible\tCe qui s'\xC3\xA9" "crit\n"
                                       "ST\t\xE2\x9C\x93 ex\xC3\xA9" "cut\xC3\xA9" "e en simulation\t" + std::string(f.st) + "\n"
                                       "C\t" + availText(f.inC) + "\t" + (f.inC == hn::Avail::None ? std::string("\xE2\x80\x94") : std::string(f.c)) + "\n"
                                       "C++\t" + availText(f.inCpp) + "\t" + (f.inCpp == hn::Avail::None ? std::string("\xE2\x80\x94") : std::string(f.cpp))));
    a.blocks.push_back(notations(std::string(f.st), f.inC == hn::Avail::None ? std::string{} : std::string(f.c),
                                 f.inCpp == hn::Avail::None ? std::string{} : std::string(f.cpp), notation));
    a.blocks.push_back(block(K::Heading, "O\xC3\xB9 l'\xC3\xA9" "crire"));
    const std::string yes = "\xE2\x9C\x93";
    const std::string inExpr = f.expression ? yes : std::string("non : elle agit (une action, un script)");
    a.blocks.push_back(block(K::Table, "Script, fonction IHM\t" + yes + "\nExpression fx d'une propri\xC3\xA9t\xC3\xA9\t" + inExpr + "\nTexte \xC3\xA0 trous {...}\t" + inExpr
                                           + "\nCondition d'alarme, d'historique\t" + inExpr));
    if (!f.params.empty()) {
        a.blocks.push_back(block(K::Heading, "Param\xC3\xA8tres"));
        for (const auto& p : f.params) {
            auto t = block(K::Term, std::string(p.role));
            t.label = std::string(p.name);
            t.detail = std::string(p.type);
            t.pills.push_back({std::string(p.type)});
            if (p.optional) t.pills.push_back({"facultatif", ui::kNoColor, ui::Tone::Muted});
            a.blocks.push_back(std::move(t));
        }
    }
    if (!f.example.empty()) {
        a.blocks.push_back(block(K::Heading, "Exemple"));
        auto code = block(K::Code, exampleLine(f.example));
        code.label = "ST";
        a.blocks.push_back(std::move(code));
        auto res = block(K::Callout, "x vaut " + std::string(f.example.expected) + " (" + std::string(f.example.type)
                                         + ") : v\xC3\xA9rifi\xC3\xA9 dans le moteur de l'IHM (l'essai natives l'ex\xC3\xA9" "cute \xC3\xA0 chaque version).");
        res.label = "R\xC3\xA9sultat";
        a.blocks.push_back(std::move(res));
    }
    for (const auto& n : f.notes) {
        auto c = block(K::Callout, std::string(n));
        c.label = "\xC3\x80 savoir";
        a.blocks.push_back(std::move(c));
    }
    std::vector<ui::HelpLink> see;
    if (home < hn::shelves().size())
        for (const auto i : hn::shelves()[home].functions) {
            const auto& g = hn::functions()[i];
            if (g.name != f.name && see.size() < 10) see.push_back({std::string(g.name), "native:fonction:" + std::string(g.name), std::string(g.summary)});
        }
    if (!see.empty()) a.blocks.push_back(links("Voir aussi", std::move(see)));
    return a;
}

ui::HelpArticle shelfCard(std::size_t s) {
    const auto& sh = hn::shelves()[s];
    ui::HelpArticle a;
    a.blocks.push_back(hero(sh.title, shelfCrumbs(s, false), {countText(sh.total, "fonction", "fonctions")}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, sh.summary));
    if (!sh.folders.empty() || sh.conversions) {
        std::string table = "Dossier\tFonctions\tCe qu'on y trouve";
        std::vector<ui::HelpLink> go;
        for (const auto f : sh.folders) {
            const auto& sub = hn::shelves()[f];
            table += "\n" + sub.title + "\t" + std::to_string(sub.total) + "\t" + sub.summary;
            go.push_back({sub.title, "native:" + shelfKey(f), sub.summary});
        }
        if (sh.conversions) {
            table += "\nD'un type \xC3\xA0 un autre : X_TO_Y\t" + std::to_string(hn::conversions().size()) + "\tDe chaque type de base vers chaque autre (INT_TO_REAL\xE2\x80\xA6).";
            go.push_back({"D'un type \xC3\xA0 un autre : X_TO_Y", "native:conversions", {}});
        }
        a.blocks.push_back(block(K::Table, table));
        a.blocks.push_back(links("Les dossiers", std::move(go)));
    }
    if (!sh.functions.empty()) {
        std::string table = kFunctionHeader;
        std::vector<ui::HelpLink> go;
        for (const auto i : sh.functions) {
            const auto& f = hn::functions()[i];
            table += "\n" + functionRow(f);
            go.push_back({std::string(f.name), "native:fonction:" + std::string(f.name), std::string(f.summary)});
        }
        a.blocks.push_back(block(K::Table, table));
        a.blocks.push_back(functionLegend());
        a.blocks.push_back(links("Les fiches", std::move(go)));
    }
    return a;
}

ui::HelpArticle functionsCard() {
    const auto& root = hn::shelves().front();
    ui::HelpArticle a;
    a.blocks.push_back(hero("Fonctions", {}, {countText(root.total, "fonction", "fonctions"), countText(root.folders.size(), "th\xC3\xA8me", "th\xC3\xA8mes")},
                            ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, "Toutes les fonctions du langage de l'IHM, sans rien d\xC3\xA9" "clarer, rang\xC3\xA9" "es par ce qu'on veut faire. Un dossier "
                                      "de plus de 20 fonctions se range par geste (Listes : Ajouter et retirer, Lire et chercher\xE2\x80\xA6). "
                                      "De A \xC3\xA0 Z les montre toutes par leur nom. Une fonction du projet de m\xC3\xAAme nom passe avant."));
    std::string table = "Th\xC3\xA8me\tFonctions\tCe qu'on y trouve";
    std::vector<ui::HelpLink> go;
    for (const auto f : root.folders) {
        const auto& sub = hn::shelves()[f];
        table += "\n" + sub.title + "\t" + std::to_string(sub.total) + "\t" + sub.summary;
        go.push_back({sub.title, "native:" + shelfKey(f), sub.summary});
    }
    a.blocks.push_back(block(K::Table, table));
    go.push_back({"De A \xC3\xA0 Z", "native:index", "Toutes les fonctions, par leur nom"});
    a.blocks.push_back(links("Les th\xC3\xA8mes", std::move(go)));
    return a;
}

std::vector<std::pair<std::string, std::string>> conversionCrumbs() {
    auto c = shelfCrumbs(hn::shelfIndex("conversions"), true);
    return c;
}

ui::HelpArticle conversionsCard() {
    ui::HelpArticle a;
    const auto& types = hn::conversionTypes();
    a.blocks.push_back(hero("D'un type \xC3\xA0 un autre : X_TO_Y", conversionCrumbs(), {std::to_string(hn::conversions().size()) + " conversions"}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, "De chaque type de base vers chaque autre : " + std::to_string(types.size()) + " types de d\xC3\xA9part, "
                                      + std::to_string(types.size() - 1) + " d'arriv\xC3\xA9" "e chacun. Un nombre trop grand pour le type d'arriv\xC3\xA9" "e est "
                                        "tronqu\xC3\xA9 (ses bits de poids fort tombent), un r\xC3\xA9" "el vers un entier est arrondi."));
    std::string table = "Depuis\tVers";
    std::vector<ui::HelpLink> go;
    for (const auto& t : types) {
        std::string to;
        for (const auto& u : types)
            if (u != t) to += (to.empty() ? "" : ", ") + std::string(u);
        table += "\n" + std::string(t) + "\t" + to;
        go.push_back({std::string(t) + "_TO_\xE2\x80\xA6", "native:conv-source:" + std::string(t), {}});
    }
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Par type de d\xC3\xA9part", std::move(go)));
    return a;
}

ui::HelpArticle convSourceCard(std::string_view from) {
    ui::HelpArticle a;
    std::string table = "Conversion\tCe qu'elle fait";
    std::vector<ui::HelpLink> go;
    std::size_t n = 0;
    for (const auto& c : hn::conversions()) {
        if (upper(c.from) != upper(from)) continue;
        ++n;
        table += "\n" + c.name + "\t" + c.behaviour;
        go.push_back({c.name, "native:conversion:" + c.name, c.behaviour});
    }
    auto crumbs = conversionCrumbs();
    crumbs.push_back({"X_TO_Y", "conversions"});
    a.blocks.push_back(hero(std::string(from) + "_TO_\xE2\x80\xA6", crumbs, {std::to_string(n) + " conversions"}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, "D'un " + std::string(from) + " vers chaque autre type de base."));
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle conversionCard(const hn::Conversion& c, std::string_view notation) {
    ui::HelpArticle a;
    auto crumbs = conversionCrumbs();
    crumbs.push_back({"X_TO_Y", "conversions"});
    crumbs.push_back({c.from + "_TO_\xE2\x80\xA6", "conv-source:" + c.from});
    a.blocks.push_back(hero(c.name, crumbs, {c.from + " \xE2\x86\x92 " + c.to}, ui::Icon::Code));
    a.blocks.push_back(block(K::Lead, c.behaviour));
    a.blocks.push_back(block(K::Heading, "Signature"));
    {
        auto code = block(K::Code, c.name + "(IN : " + c.from + ") : " + c.to);
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(notations(c.st, c.c, c.cpp, notation));
    a.blocks.push_back(block(K::Table, "Script, fonction IHM\t\xE2\x9C\x93\nExpression \xC6\x92x, texte \xC3\xA0 trous, condition\t\xE2\x9C\x93"));
    std::vector<ui::HelpLink> see{{"TO_" + c.to, "native:fonction:TO_" + c.to, "La conversion g\xC3\xA9n\xC3\xA9rique vers " + c.to}};
    if (hn::conversion(c.to + "_TO_" + c.from)) see.push_back({c.to + "_TO_" + c.from, "native:conversion:" + c.to + "_TO_" + c.from, "Le chemin inverse"});
    a.blocks.push_back(links("Voir aussi", std::move(see)));
    return a;
}

// ---- les types ----
std::string typeKey(const hn::TypeEntry& e) {
    return (e.kind == hn::TypeEntry::Kind::Generic ? "generique:" : "type:") + e.name;
}
std::size_t typeShelfOf(std::string_view name, hn::TypeEntry::Kind kind) {
    const auto& s = hn::typeShelves();
    for (std::size_t i = 0; i < s.size(); ++i)
        for (const auto& e : s[i].entries)
            if (e.kind == kind && upper(e.name) == upper(name)) return i;
    return kNoShelf;
}
std::vector<std::pair<std::string, std::string>> typeCrumbs(std::size_t shelf) {
    std::vector<std::pair<std::string, std::string>> out{{"Types", "types"}};
    if (shelf < hn::typeShelves().size()) out.push_back({hn::typeShelves()[shelf].title, "types-dossier:" + hn::typeShelves()[shelf].key});
    return out;
}
std::size_t typeCount() {
    std::size_t n = 0;
    for (const auto& s : hn::typeShelves()) n += s.entries.size();
    return n;
}
std::string entrySummary(const hn::TypeEntry& e) {
    switch (e.kind) {
        case hn::TypeEntry::Kind::Base: return e.index < hn::tree().types.size() ? hn::tree().types[e.index].summary : std::string{};
        case hn::TypeEntry::Kind::Constructed:
            if (const auto* g = hn::typeGuide(e.name)) return std::string(g->lead);
            return e.index < hn::constructed().size() ? std::string(hn::constructed()[e.index].summary) : std::string{};
        case hn::TypeEntry::Kind::Generic: return e.index < hn::generics().size() ? std::string(hn::generics()[e.index].members) : std::string{};
    }
    return {};
}

ui::HelpArticle typesCard() {
    ui::HelpArticle a;
    a.blocks.push_back(hero("Types", {}, {countText(typeCount(), "fiche", "fiches")}, ui::Icon::DerivedType));
    a.blocks.push_back(block(K::Lead, "Les types des variables, des param\xC3\xA8tres et des membres, en cinq dossiers : les nombres et les bits, les textes "
                                      "et les dates, les conteneurs (plusieurs valeurs dans une variable), les r\xC3\xA9" "f\xC3\xA9rences et les g\xC3\xA9n\xC3\xA9riques "
                                      "qu'on lit dans les signatures."));
    std::string table = "Dossier\tFiches\tCe qu'on y trouve";
    std::vector<ui::HelpLink> go;
    for (const auto& s : hn::typeShelves()) {
        table += "\n" + s.title + "\t" + std::to_string(s.entries.size()) + "\t" + s.summary;
        go.push_back({s.title, "native:types-dossier:" + s.key, s.summary});
    }
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les dossiers", std::move(go)));
    return a;
}

ui::HelpArticle typeShelfCard(std::size_t i) {
    const auto& s = hn::typeShelves()[i];
    ui::HelpArticle a;
    a.blocks.push_back(hero(s.title, {{"Types", "types"}}, {countText(s.entries.size(), "fiche", "fiches")}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, s.summary));
    if (s.key == "conteneurs") {
        a.blocks.push_back(block(K::Heading, "Lequel choisir ?"));
        a.blocks.push_back(block(K::Table,
            "Type\tTaille\tLire un \xC3\xA9l\xC3\xA9ment\tOrdre\tDoublons\tSur un \xC3\xA9quipement\tPour\xE2\x80\xA6"
            "\nARRAY[a..b] OF T\tfixe\tT[i], de a \xC3\xA0 b\tcelui des indices\toui\toui, une adresse par case\tun plan m\xC3\xA9moire, des mesures \xC3\xA0 place fixe"
            "\nLIST OF T\tvariable\tL[i], depuis 0\tcelui des ajouts\toui\tnon (m\xC3\xA9moire de l'IHM)\tune suite qui grandit"
            "\nVECTOR OF T\tvariable\tV[i], VECTOR_POP\tcelui des ajouts\toui\tnon\tune pile : le dernier ajout\xC3\xA9 sort le premier"
            "\nRING[N] OF T\tjusqu'\xC3\xA0 N\tFOR EACH, LIST_AVG\xE2\x80\xA6\tde la plus ancienne \xC3\xA0 la plus r\xC3\xA9" "cente\toui\tnon\tles N derni\xC3\xA8res mesures, une moyenne glissante"
            "\nSET OF T\tvariable\tSET_HAS(S, x)\trang\xC3\xA9\tnon\tnon\tdes choix sans doublon : les postes actifs"
            "\nMAP[K] OF T\tvariable\tm[cl\xC3\xA9]\trang\xC3\xA9 par cl\xC3\xA9\tune cl\xC3\xA9, une fois\tnon\tune valeur par nom : un stock par gaz"
            "\nTUPLE(T1, T2\xE2\x80\xA6)\tfixe\tt.Item1, t.Item2\tcelui des champs\t\xE2\x80\x94\tnon (une structure IHM pour un \xC3\xA9quipement)\tdeux ou trois valeurs li\xC3\xA9" "es"));
    }
    std::string table = "Type\tCe que c'est";
    std::vector<ui::HelpLink> go;
    for (const auto& e : s.entries) {
        table += "\n" + e.name + "\t" + entrySummary(e);
        go.push_back({e.name, "native:" + typeKey(e), entrySummary(e)});
    }
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle typeCardArticle(const hn::TypeCard& t) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(t.name, typeCrumbs(typeShelfOf(t.name, hn::TypeEntry::Kind::Base)), {t.category, t.size}, ui::Icon::DerivedType));
    a.blocks.push_back(block(K::Lead, t.summary));
    a.blocks.push_back(block(K::Table, "MIN\t" + t.minText + "\nMAX\t" + t.maxText + "\nTaille\t" + t.size + (t.bits ? " (" + std::to_string(t.bits) + " bits)" : std::string{})
                                           + "\nPlace Modbus\t" + t.modbus + "\nEn C\t" + (t.cType.empty() ? std::string("\xE2\x80\x94") : t.cType)
                                           + "\nEn C++\t" + (t.cppType.empty() ? std::string("\xE2\x80\x94") : t.cppType)
                                           + "\nValeur par d\xC3\xA9" "faut\t" + (t.defaultValue.empty() ? std::string("\xE2\x80\x94") : t.defaultValue)));
    if (!t.literals.empty()) {
        a.blocks.push_back(block(K::Heading, "Ses litt\xC3\xA9raux"));
        std::string lits;
        for (const auto& l : t.literals) lits += (lits.empty() ? "" : "\n") + l;
        auto code = block(K::Code, lits);
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    for (const auto& n : t.notes) {
        auto c = block(K::Callout, n);
        c.label = "\xC3\x80 savoir";
        a.blocks.push_back(std::move(c));
    }
    if (!t.uses.empty()) {
        a.blocks.push_back(block(K::Heading, "O\xC3\xB9 il sert"));
        for (const auto& u : t.uses) {
            auto b = block(K::Bullet, u);
            b.label = "-";
            a.blocks.push_back(std::move(b));
        }
    }
    std::vector<ui::HelpLink> see{{"TO_" + t.name, "native:fonction:TO_" + t.name, "Convertir vers " + t.name}};
    a.blocks.push_back(links("Voir aussi", std::move(see)));
    return a;
}

ui::HelpBlock codeBlock(std::string text) {
    auto code = block(K::Code, std::move(text));
    code.label = "ST";
    return code;
}

// Un conteneur : sa fiche complete (TypeGuide).
ui::HelpArticle guideCard(const hn::TypeGuide& g, std::string_view notation) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(g.title), typeCrumbs(typeShelfOf(g.name, hn::TypeEntry::Kind::Constructed)),
                            {"conteneur", g.name == "ARRAY" || g.name == "TUPLE" ? "taille fixe" : "taille variable"}, ui::Icon::DerivedType));
    a.blocks.push_back(block(K::Lead, std::string(g.lead)));
    {
        auto c = block(K::Callout, std::string(g.when));
        c.label = "Quand le choisir";
        a.blocks.push_back(std::move(c));
    }
    a.blocks.push_back(block(K::Heading, "D\xC3\xA9" "clarer"));
    a.blocks.push_back(codeBlock(std::string(g.declare)));
    a.blocks.push_back(block(K::Heading, "Lire et \xC3\xA9" "crire"));
    a.blocks.push_back(codeBlock(std::string(g.access)));
    const auto props = hmi::types::publicProperties(g.sample);
    a.blocks.push_back(block(K::Heading, "Ses propri\xC3\xA9t\xC3\xA9s"));
    if (props.empty()) {
        a.blocks.push_back(block(K::Paragraph, "Pas de propri\xC3\xA9t\xC3\xA9 : ses champs se lisent par leur rang, Item1, Item2\xE2\x80\xA6 dans l'ordre de la d\xC3\xA9" "claration."));
    } else {
        std::string table = "Propri\xC3\xA9t\xC3\xA9\tType\tCe que c'est";
        for (const auto& p : props)
            table += "\n" + p.name + "\t" + p.type + "\t" + p.what + (p.also.empty() ? std::string{} : " (aussi " + p.also + ")");
        a.blocks.push_back(block(K::Table, table));
    }
    if (!g.iterate.empty()) {
        a.blocks.push_back(block(K::Heading, "Parcourir"));
        a.blocks.push_back(codeBlock(std::string(g.iterate)));
    }
    if (const auto s = hn::shelfIndex(g.shelf); s != kNoShelf) {
        const auto& sh = hn::shelves()[s];
        a.blocks.push_back(block(K::Heading, "Ses fonctions (" + std::to_string(sh.total) + ")"));
        std::vector<ui::HelpLink> go;
        if (!sh.folders.empty()) {
            go.push_back({sh.title, "native:" + shelfKey(s), sh.summary});
            for (const auto f : sh.folders) go.push_back({hn::shelves()[f].title, "native:" + shelfKey(f), hn::shelves()[f].summary});
        } else {
            for (const auto i : sh.functions)
                go.push_back({std::string(hn::functions()[i].name), "native:fonction:" + std::string(hn::functions()[i].name),
                              std::string(hn::functions()[i].summary)});
        }
        a.blocks.push_back(links({}, std::move(go)));
    }
    a.blocks.push_back(block(K::Heading, "O\xC3\xB9 il vit"));
    a.blocks.push_back(block(K::Paragraph, std::string(g.lives)));
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(notations(std::string(g.declare), std::string(g.c), std::string(g.cpp), notation));
    a.blocks.push_back(block(K::Heading, "L'exemple, jou\xC3\xA9 par le moteur"));
    a.blocks.push_back(codeBlock(std::string(g.example)));
    std::string after = "Apr\xC3\xA8s l'exemple\tVaut";
    for (const auto& c : g.checks) after += "\n" + std::string(c.expression) + "\t" + std::string(c.expected);
    a.blocks.push_back(block(K::Table, after));
    auto res = block(K::Callout, "V\xC3\xA9rifi\xC3\xA9 dans le moteur de l'IHM : l'essai natives joue cet exemple \xC3\xA0 chaque version.");
    res.label = "R\xC3\xA9sultat";
    a.blocks.push_back(std::move(res));
    return a;
}

// Une reference, STRING[n] : la fiche du catalogue (Constructed).
ui::HelpArticle constructedCard(const hn::Constructed& c, std::string_view notation) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(c.name), typeCrumbs(typeShelfOf(c.name, hn::TypeEntry::Kind::Constructed)), {std::string(c.example)},
                            ui::Icon::DerivedType));
    a.blocks.push_back(block(K::Lead, std::string(c.summary)));
    a.blocks.push_back(block(K::Heading, "D\xC3\xA9" "clarer"));
    a.blocks.push_back(codeBlock(std::string(c.st)));
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(notations(std::string(c.st), std::string(c.c), std::string(c.cpp), notation));
    return a;
}

// Un generique : ses types, et les natives qui le prennent.
ui::HelpArticle genericCard(const hn::Generic& g) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(g.name), typeCrumbs(typeShelfOf(g.name, hn::TypeEntry::Kind::Generic)), {"g\xC3\xA9n\xC3\xA9rique"}, ui::Icon::DerivedType));
    a.blocks.push_back(block(K::Lead, std::string(g.summary)));
    a.blocks.push_back(block(K::Table, "Ses types\t" + std::string(g.members)));
    a.blocks.push_back(block(K::Paragraph, "Il ne se d\xC3\xA9" "clare pas : dans la signature d'une native, il dit ce qu'un param\xC3\xA8tre accepte. "
                                           "ABS(IN : ANY_NUM) prend tout nombre et rend le m\xC3\xAAme type."));
    std::vector<ui::HelpLink> go;
    std::size_t n = 0;
    for (const auto& f : hn::functions()) {
        const bool uses = std::any_of(f.params.begin(), f.params.end(), [&g](const hn::Param& p) { return upper(p.type) == upper(g.name); });
        if (!uses) continue;
        ++n;
        if (go.size() < 12) go.push_back({std::string(f.name), "native:fonction:" + std::string(f.name), hn::signature(f)});
    }
    if (n > 0) {
        a.blocks.push_back(block(K::Heading, countText(n, "native le prend", "natives le prennent")));
        a.blocks.push_back(links({}, std::move(go)));
    }
    return a;
}

ui::HelpArticle operatorsCard() {
    ui::HelpArticle a;
    std::string table = "Symbole\tNom\tCe qu'il fait";
    std::vector<ui::HelpLink> go;
    for (std::size_t i = 0; i < hn::operators().size(); ++i) {
        const auto& o = hn::operators()[i];
        table += "\n" + std::string(o.symbol) + "\t" + std::string(o.name) + "\t" + std::string(o.summary);
        go.push_back({std::string(o.symbol), "native:operateur:" + std::to_string(i), std::string(o.name)});
    }
    a.blocks.push_back(hero("Op\xC3\xA9rateurs", {}, {std::to_string(hn::operators().size()) + " op\xC3\xA9rateurs"}, ui::Icon::Code));
    a.blocks.push_back(block(K::Lead, "Les op\xC3\xA9rateurs du langage, du plus fort au plus faible : ce qui se calcule d'abord est en haut."));
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle operatorCard(std::size_t i, std::string_view notation) {
    const auto& o = hn::operators()[i];
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(o.symbol), {{"Op\xC3\xA9rateurs", "operateurs"}}, {std::string(o.name)}, ui::Icon::Code));
    a.blocks.push_back(block(K::Lead, std::string(o.summary)));
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(notations(std::string(o.st), std::string(o.c), std::string(o.cpp), notation));
    if (!o.example.empty()) {
        a.blocks.push_back(block(K::Heading, "Exemple"));
        auto code = block(K::Code, exampleLine(o.example));
        code.label = "ST";
        a.blocks.push_back(std::move(code));
        auto res = block(K::Callout, "x vaut " + std::string(o.example.expected) + " (" + std::string(o.example.type) + ")");
        res.label = "R\xC3\xA9sultat";
        a.blocks.push_back(std::move(res));
    }
    return a;
}

ui::HelpArticle instructionsCard() {
    ui::HelpArticle a;
    std::string table = "Mot\tNom\tForme";
    std::vector<ui::HelpLink> go;
    for (std::size_t i = 0; i < hn::instructions().size(); ++i) {
        const auto& s = hn::instructions()[i];
        std::string form(s.form);
        std::replace(form.begin(), form.end(), '\n', ' ');
        table += "\n" + std::string(s.keyword) + "\t" + std::string(s.name) + "\t" + form;
        go.push_back({std::string(s.keyword), "native:instruction:" + std::to_string(i), std::string(s.name)});
    }
    a.blocks.push_back(hero("Instructions", {}, {std::to_string(hn::instructions().size()) + " instructions"}, ui::Icon::Section));
    a.blocks.push_back(block(K::Lead, "Les instructions des scripts et des fonctions IHM (pas des expressions de propri\xC3\xA9t\xC3\xA9 : elles rendent une valeur, sans instruction)."));
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle instructionCard(std::size_t i, std::string_view notation) {
    const auto& s = hn::instructions()[i];
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(s.keyword), {{"Instructions", "instructions"}}, {std::string(s.name)}, ui::Icon::Section));
    a.blocks.push_back(block(K::Heading, "Sa forme"));
    {
        auto code = block(K::Code, std::string(s.form));
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(notations(std::string(s.st), std::string(s.c), std::string(s.cpp), notation));
    return a;
}

// 1.12.1 : "Selecteur (style)", "tout objet (align)" ; le libelle de l'inspecteur devant.
std::string useText(const hn::PropertyUse& u) {
    std::string label, help;
    std::string kind = "tout objet";
    if (u.kind != "*")
        if (const auto k = hmi::kindFromKey(u.kind)) kind = std::string(hmi::kindLabel(*k));
    if (app::hmiPropertyInfo(u.key, label, help) && !label.empty()) return kind + " \xC2\xB7 " + label + " (" + std::string(u.key) + ")";
    return kind + " \xC2\xB7 " + std::string(u.key);
}

std::string usesText(const hn::NativeEnum& e) {
    std::string out;
    for (const auto& u : e.uses) out += (out.empty() ? "" : ", ") + useText(u);
    return out;
}

// ---- 1.12.17 : les enumerations par usage ----
std::string enumShelfKey(std::size_t s) {
    return s == 0 || s >= hn::enumShelves().size() ? std::string("enumerations") : "enums-dossier:" + hn::enumShelves()[s].key;
}
std::vector<std::pair<std::string, std::string>> enumCrumbs(std::size_t s, bool self) {
    std::vector<std::size_t> chain;
    for (std::size_t at = s; at != kNoShelf && at < hn::enumShelves().size();) {
        chain.push_back(at);
        if (at == 0) break;
        at = hn::enumShelves()[at].parent;
    }
    std::reverse(chain.begin(), chain.end());
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto c : chain)
        if (self || c != s) out.push_back({c == 0 ? std::string("\xC3\x89num\xC3\xA9rations") : hn::enumShelves()[c].title, enumShelfKey(c)});
    return out;
}

std::string enumRow(const hn::NativeEnum& e) {
    std::string values;
    for (const auto& v : e.values) values += (values.empty() ? "" : ", ") + std::string(v.name);
    std::string serves = std::string(e.function);
    if (!e.uses.empty()) serves += (serves.empty() ? "" : ", ") + usesText(e);
    return std::string(e.name) + "\t" + values + "\t" + (serves.empty() ? std::string(e.summary) : serves);
}

ui::HelpArticle enumShelfCard(std::size_t s) {
    const auto& sh = hn::enumShelves()[s];
    ui::HelpArticle a;
    a.blocks.push_back(hero(s == 0 ? std::string("\xC3\x89num\xC3\xA9rations") : sh.title, enumCrumbs(s, false), {countText(sh.total, "\xC3\xA9num\xC3\xA9ration", "\xC3\xA9num\xC3\xA9rations")},
                            s == 0 ? ui::Icon::Constant : ui::Icon::Folder));
    if (s == 0) {
        a.blocks.push_back(block(K::Lead, "Les valeurs nomm\xC3\xA9" "es que les fonctions IHM_ attendent et que les propri\xC3\xA9t\xC3\xA9s des objets prennent : "
                                          "NOM#Valeur vaut son nombre (un DINT) ; la fonction, ou la propri\xC3\xA9t\xC3\xA9 pilot\xC3\xA9" "e par une expression (\xC6\x92), "
                                          "re\xC3\xA7oit le mot qu'elle lisait d\xC3\xA9j\xC3\xA0. La v\xC3\xA9rification les conna\xC3\xAEt : une valeur mal \xC3\xA9" "crite est une erreur."));
    } else if (!sh.summary.empty()) {
        a.blocks.push_back(block(K::Lead, sh.summary));
    }
    if (!sh.folders.empty()) {
        std::string table = "Dossier\t\xC3\x89num\xC3\xA9rations\tCe qu'on y trouve";
        std::vector<ui::HelpLink> go;
        for (const auto f : sh.folders) {
            const auto& sub = hn::enumShelves()[f];
            std::string names;
            for (const auto e : sub.enums) names += (names.empty() ? "" : ", ") + std::string(hn::enums()[e].name);
            table += "\n" + sub.title + "\t" + std::to_string(sub.total) + "\t" + (sub.summary.empty() ? names : sub.summary);
            go.push_back({sub.title, "native:" + enumShelfKey(f), sub.summary});
        }
        a.blocks.push_back(block(K::Table, table));
        a.blocks.push_back(links("Les dossiers", std::move(go)));
    }
    if (!sh.enums.empty()) {
        std::string table = "\xC3\x89num\xC3\xA9ration\tValeurs\tSert \xC3\xA0";
        std::vector<ui::HelpLink> go;
        for (const auto e : sh.enums) {
            const auto& en = hn::enums()[e];
            table += "\n" + enumRow(en);
            go.push_back({std::string(en.name), "native:enum:" + std::string(en.name), std::string(en.summary)});
        }
        a.blocks.push_back(block(K::Table, table));
        a.blocks.push_back(links("Les fiches", std::move(go)));
    }
    return a;
}

std::size_t enumRank(const hn::NativeEnum& e) {
    for (std::size_t i = 0; i < hn::enums().size(); ++i)
        if (&hn::enums()[i] == &e) return i;
    return kNoShelf;
}

// ---- De A a Z ----
ui::HelpArticle indexCard() {
    ui::HelpArticle a;
    const auto& alpha = hn::alphabetical();
    a.blocks.push_back(hero("De A \xC3\xA0 Z", {}, {countText(alpha.size(), "fonction", "fonctions")}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, "Toutes les fonctions, par leur nom : quand on conna\xC3\xAEt le nom, sans savoir o\xC3\xB9 elle est rang\xC3\xA9" "e. "
                                      "Chaque fiche dit son dossier ; les conversions X_TO_Y ont le leur, dans Conversions."));
    std::string table = "Lettre\tFonctions\tDe \xE2\x80\xA6 \xC3\xA0 \xE2\x80\xA6";
    char letter = 0;
    std::size_t count = 0;
    std::string first, last;
    const auto flush = [&] {
        if (count) table += "\n" + std::string(1, letter) + "\t" + std::to_string(count) + "\t" + first + (first == last ? std::string{} : " \xE2\x80\xA6 " + last);
    };
    for (const auto i : alpha) {
        const std::string name(hn::functions()[i].name);
        const char l = static_cast<char>(std::toupper(static_cast<unsigned char>(name.empty() ? '?' : name[0])));
        if (l != letter) {
            flush();
            letter = l;
            count = 0;
            first = name;
        }
        ++count;
        last = name;
    }
    flush();
    a.blocks.push_back(block(K::Table, table));
    return a;
}

ui::HelpArticle enumCard(const hn::NativeEnum& e, std::string_view valueName) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(e.name), enumCrumbs(hn::enumShelfOf(enumRank(e)), true),
                            {std::to_string(e.values.size()) + " valeurs", !e.function.empty() ? "pour " + std::string(e.function)
                                                                             : e.uses.empty() ? std::string{} : std::string("pour les objets")}, ui::Icon::Constant));
    a.blocks.push_back(block(K::Lead, std::string(e.summary)));
    std::string table = "Valeur\tNombre\tCe qu'elle dit\tLe mot re\xC3\xA7u";
    for (const auto& v : e.values) {
        const bool here = !valueName.empty() && upper(v.name) == upper(valueName);
        table += "\n" + std::string(here ? "\xE2\x96\xB6 " : "") + std::string(e.name) + "#" + std::string(v.name) + "\t" + std::to_string(v.number) + "\t"
               + std::string(v.text) + "\t" + (v.argument.empty() ? std::string("\xE2\x80\x94") : "'" + std::string(v.argument) + "'");
    }
    a.blocks.push_back(block(K::Table, table));
    if (!e.function.empty() && !e.values.empty()) {
        a.blocks.push_back(block(K::Heading, "Exemple"));
        auto code = block(K::Code, "// " + std::string(e.function) + " : l'argument " + std::to_string(e.argument + 1) + "\nx := " + std::string(e.name) + "#"
                                       + std::string(e.values.size() > 1 ? e.values[1].name : e.values[0].name) + ";   // " + std::to_string(e.values.size() > 1 ? e.values[1].number : e.values[0].number));
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    if (!e.uses.empty() && !e.values.empty()) {
        // 1.12.1 : les proprietes des objets qui la prennent - dans leur case \xC6\x92.
        a.blocks.push_back(block(K::Heading, "O\xC3\xB9 elle sert"));
        std::string where = "Objet\tPropri\xC3\xA9t\xC3\xA9\tCl\xC3\xA9";
        for (const auto& u : e.uses) {
            std::string label, help;
            std::string kind = "tout objet qui l'a";
            if (u.kind != "*")
                if (const auto k = hmi::kindFromKey(u.kind)) kind = std::string(hmi::kindLabel(*k));
            if (!app::hmiPropertyInfo(u.key, label, help) || label.empty()) label = std::string(u.key);
            where += "\n" + kind + "\t" + label + "\t" + std::string(u.key);
        }
        a.blocks.push_back(block(K::Table, where));
        a.blocks.push_back(block(K::Paragraph, "Dans l'inspecteur, la case \xC6\x92 de la propri\xC3\xA9t\xC3\xA9 : l'expression rend une valeur de "
                                                   + std::string(e.name) + " (son nombre), l'objet re\xC3\xA7oit le mot de la liste ; un mot entre quotes marche aussi."));
        const auto& first = e.values.front();
        const auto& other = e.values.size() > 1 ? e.values[1] : e.values.front();
        auto code = block(K::Code, "// " + std::string(e.uses.front().key) + " : '" + std::string(first.argument) + "' en marche, '" + std::string(other.argument)
                                       + "' sinon\nSEL(Marche, " + std::string(e.name) + "#" + std::string(other.name) + ", " + std::string(e.name) + "#"
                                       + std::string(first.name) + ")");
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    if (!e.function.empty())
        a.blocks.push_back(links("Voir aussi", {{std::string(e.function), "native:fonction:" + std::string(e.function), "La fonction qui l'attend"}}));
    return a;
}

ui::HelpArticle rootCard() {
    ui::HelpArticle a;
    std::size_t functions = hn::functions().size() + hn::conversions().size();
    a.blocks.push_back(hero("Natives", {}, {std::to_string(functions) + " fonctions", std::to_string(typeCount()) + " types"}, ui::Icon::Lock));
    a.blocks.back().links.erase(a.blocks.back().links.begin());   // pas de « Natives » devant Natives
    a.blocks.push_back(block(K::Lead, "Tout ce que le langage de l'IHM conna\xC3\xAEt sans rien d\xC3\xA9" "clarer : ses fonctions, ses types, ses op\xC3\xA9rateurs, "
                                      "ses instructions, ses \xC3\xA9num\xC3\xA9rations. Verrouill\xC3\xA9" "es : elles se lisent, s'ins\xC3\xA8rent, ne se modifient pas. "
                                      "Chaque fiche dit o\xC3\xB9 l'\xC3\xA9" "crire et comment en ST (ex\xC3\xA9" "cut\xC3\xA9 en simulation), en C et en C++."));
    a.blocks.push_back(block(K::Table, "Fonctions\t" + std::to_string(functions) + " (dont " + std::to_string(hn::conversions().size()) + " X_TO_Y)"
                                       "\nTypes\t" + std::to_string(typeCount()) + "\nOp\xC3\xA9rateurs\t" + std::to_string(hn::operators().size())
                                       + "\nInstructions\t" + std::to_string(hn::instructions().size()) + "\n\xC3\x89num\xC3\xA9rations\t" + std::to_string(hn::enums().size())));
    auto tip = block(K::Callout, "Double-clic sur une native de l'arbre (ou Ins\xC3\xA9rer) : son appel dans le script montr\xC3\xA9. "
                                 "F1 sur un nom dans un script : sa fiche.");
    tip.label = "Astuce";
    a.blocks.push_back(std::move(tip));
    a.blocks.push_back(links("Les rubriques", {{"Types", "native:types", {}}, {"Fonctions", "native:fonctions", {}}, {"Op\xC3\xA9rateurs", "native:operateurs", {}},
                                                {"Instructions", "native:instructions", {}}, {"\xC3\x89num\xC3\xA9rations", "native:enumerations", {}},
                                                {"De A \xC3\xA0 Z", "native:index", {}}}));
    return a;
}

std::string after(std::string_view key, std::string_view prefix) {
    return key.rfind(prefix, 0) == 0 ? std::string(key.substr(prefix.size())) : std::string{};
}

} // namespace

std::string canonicalKey(std::string_view key) {
    std::string k(key);
    const auto byName = [&k](std::string_view prefix, std::size_t count, const auto& nameAt) {
        if (k.rfind(prefix, 0) != 0) return;
        const std::string n = k.substr(prefix.size());
        if (n.empty() || std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) return;
        for (std::size_t i = 0; i < count; ++i)
            if (upper(nameAt(i)) == upper(n)) {
                k = std::string(prefix) + std::to_string(i);
                return;
            }
    };
    byName("operateur:", hn::operators().size(), [](std::size_t i) { return hn::operators()[i].symbol; });
    byName("instruction:", hn::instructions().size(), [](std::size_t i) { return hn::instructions()[i].keyword; });
    // 1.12.17 : une categorie du catalogue (les liens d'avant) -> son dossier.
    if (const auto id = after(k, "categorie:"); !id.empty())
        if (const auto s = hn::categoryShelf(id); s != kNoShelf) k = shelfKey(s);
    return k;
}

ui::HelpArticle article(std::string_view key0, std::string_view notation) {
    const std::string canonical = canonicalKey(key0);
    const std::string_view key = canonical;
    if (key == "natives") return rootCard();
    if (key == "fonctions") return functionsCard();
    if (key == "conversions") return conversionsCard();
    if (key == "types") return typesCard();
    if (key == "operateurs") return operatorsCard();
    if (key == "instructions") return instructionsCard();
    if (key == "enumerations") return enumShelfCard(0);
    if (key == "index") return indexCard();
    if (const auto id = after(key, "dossier:"); !id.empty())
        if (const auto s = hn::shelfIndex(id); s != kNoShelf && s != 0) return shelfCard(s);
    if (const auto n = after(key, "fonction:"); !n.empty())
        if (const auto* f = hn::function(n)) return functionCard(*f, notation);
    if (const auto n = after(key, "index:"); !n.empty())
        if (const auto* f = hn::function(n)) return functionCard(*f, notation);
    if (const auto t = after(key, "conv-source:"); !t.empty()) return convSourceCard(t);
    if (const auto n = after(key, "conversion:"); !n.empty())
        if (const auto c = hn::conversion(n)) return conversionCard(*c, notation);
    if (const auto id = after(key, "types-dossier:"); !id.empty())
        for (std::size_t i = 0; i < hn::typeShelves().size(); ++i)
            if (hn::typeShelves()[i].key == id) return typeShelfCard(i);
    if (const auto n = after(key, "type:"); !n.empty()) {
        if (const auto t = hn::typeCard(n)) return typeCardArticle(*t);
        if (const auto* g = hn::typeGuide(n)) return guideCard(*g, notation);
        for (const auto& c : hn::constructed())
            if (upper(c.name) == upper(n)) return constructedCard(c, notation);
    }
    if (const auto n = after(key, "generique:"); !n.empty())
        for (const auto& g : hn::generics())
            if (upper(g.name) == upper(n)) return genericCard(g);
    if (const auto n = after(key, "operateur:"); !n.empty()) {
        const auto i = static_cast<std::size_t>(std::atoi(n.c_str()));
        if (i < hn::operators().size()) return operatorCard(i, notation);
    }
    if (const auto n = after(key, "instruction:"); !n.empty()) {
        const auto i = static_cast<std::size_t>(std::atoi(n.c_str()));
        if (i < hn::instructions().size()) return instructionCard(i, notation);
    }
    if (const auto id = after(key, "enums-dossier:"); !id.empty())
        for (std::size_t i = 1; i < hn::enumShelves().size(); ++i)
            if (hn::enumShelves()[i].key == id) return enumShelfCard(i);
    if (const auto n = after(key, "enum:"); !n.empty())
        if (const auto* e = hn::nativeEnum(n)) return enumCard(*e, {});
    if (const auto n = after(key, "enum-valeur:"); !n.empty()) {
        const auto hash = n.find('#');
        if (const auto* e = hn::nativeEnum(n.substr(0, hash)); e && hash != std::string::npos) return enumCard(*e, n.substr(hash + 1));
    }
    return {};
}

std::string title(std::string_view key0) {
    const std::string canonical = canonicalKey(key0);
    const std::string_view key = canonical;
    std::string t;
    if (const auto colon = key.find(':'); colon != std::string_view::npos) t = std::string(key.substr(colon + 1));
    else if (key == "natives") return "IHM \xC2\xB7 Natives";
    else if (key == "index") t = "De A \xC3\xA0 Z";
    else t = std::string(key);
    if (const auto id = after(key, "dossier:"); !id.empty())
        if (const auto* s = hn::shelf(id)) t = s->title;
    if (const auto id = after(key, "types-dossier:"); !id.empty())
        for (const auto& s : hn::typeShelves())
            if (s.key == id) t = s.title;
    if (const auto id = after(key, "enums-dossier:"); !id.empty())
        for (const auto& s : hn::enumShelves())
            if (s.key == id) t = s.title;
    if (key.rfind("operateur:", 0) == 0) {
        const auto i = static_cast<std::size_t>(std::atoi(t.c_str()));
        if (i < hn::operators().size()) t = std::string(hn::operators()[i].symbol);
    }
    if (key.rfind("instruction:", 0) == 0) {
        const auto i = static_cast<std::size_t>(std::atoi(t.c_str()));
        if (i < hn::instructions().size()) t = std::string(hn::instructions()[i].keyword);
    }
    return "Natives \xC2\xB7 " + t;
}

std::string insertText(std::string_view key0) {
    const std::string canonical = canonicalKey(key0);
    const std::string_view key = canonical;
    if (const auto n = after(key, "fonction:"); !n.empty())
        if (const auto* f = hn::function(n)) return std::string(f->name) + "()";
    if (const auto n = after(key, "index:"); !n.empty())
        if (const auto* f = hn::function(n)) return std::string(f->name) + "()";
    if (const auto n = after(key, "conversion:"); !n.empty()) return n + "()";
    if (const auto n = after(key, "enum-valeur:"); !n.empty()) return n;
    if (const auto n = after(key, "enum:"); !n.empty())
        if (const auto* e = hn::nativeEnum(n); e && !e->values.empty()) return std::string(e->name) + "#" + std::string(e->values.front().name);
    if (const auto n = after(key, "instruction:"); !n.empty()) {
        const auto i = static_cast<std::size_t>(std::atoi(n.c_str()));
        if (i < hn::instructions().size()) return std::string(hn::instructions()[i].form);
    }
    if (const auto n = after(key, "operateur:"); !n.empty()) {
        const auto i = static_cast<std::size_t>(std::atoi(n.c_str()));
        if (i < hn::operators().size()) return " " + std::string(hn::operators()[i].symbol) + " ";
    }
    if (const auto n = after(key, "type:"); !n.empty()) {
        // 1.12.17 : un conteneur, une reference : sa declaration (" Inserer la declaration ").
        if (const auto* g = hn::typeGuide(n)) return std::string(g->declare);
        for (const auto& c : hn::constructed())
            if (upper(c.name) == upper(n)) return std::string(c.st);
        return n;
    }
    return {};
}

std::vector<std::string> allKeys() {
    std::vector<std::string> out{"natives", "types"};
    for (const auto& s : hn::typeShelves()) {
        out.push_back("types-dossier:" + s.key);
        for (const auto& e : s.entries) out.push_back(typeKey(e));
    }
    out.push_back("fonctions");
    std::function<void(std::size_t)> walk = [&](std::size_t s) {
        const auto& sh = hn::shelves()[s];
        if (s != 0) out.push_back(shelfKey(s));
        for (const auto f : sh.folders) walk(f);
        for (const auto i : sh.functions) out.push_back("fonction:" + std::string(hn::functions()[i].name));
        if (sh.conversions) {
            out.push_back("conversions");
            for (const auto& t : hn::conversionTypes()) {
                out.push_back("conv-source:" + std::string(t));
                for (const auto& c : hn::conversions())
                    if (c.from == t) out.push_back("conversion:" + c.name);
            }
        }
    };
    walk(0);
    out.push_back("operateurs");
    for (std::size_t i = 0; i < hn::operators().size(); ++i) out.push_back("operateur:" + std::to_string(i));
    out.push_back("instructions");
    for (std::size_t i = 0; i < hn::instructions().size(); ++i) out.push_back("instruction:" + std::to_string(i));
    std::function<void(std::size_t)> walkEnums = [&](std::size_t s) {
        const auto& sh = hn::enumShelves()[s];
        out.push_back(enumShelfKey(s));
        for (const auto f : sh.folders) walkEnums(f);
        for (const auto e : sh.enums) {
            const auto& en = hn::enums()[e];
            out.push_back("enum:" + std::string(en.name));
            for (const auto& v : en.values) out.push_back("enum-valeur:" + std::string(en.name) + "#" + std::string(v.name));
        }
    };
    walkEnums(0);
    out.push_back("index");
    for (const auto i : hn::alphabetical()) out.push_back("index:" + std::string(hn::functions()[i].name));
    return out;
}

std::string keyOfWord(std::string_view word) {
    if (word.empty()) return {};
    if (const auto hash = word.find('#'); hash != std::string_view::npos) {
        const hn::NativeEnum* e = nullptr;
        const hn::EnumValue* v = nullptr;
        if (hn::parseEnumLiteral(word, &e, &v) && e && v) return "enum-valeur:" + std::string(e->name) + "#" + std::string(v->name);
        return {};
    }
    if (const auto* f = hn::function(word)) return "fonction:" + std::string(f->name);
    if (const auto c = hn::conversion(word)) return "conversion:" + c->name;
    if (const auto* e = hn::nativeEnum(word)) return "enum:" + std::string(e->name);
    if (const auto t = hn::typeCard(word)) return "type:" + t->name;
    // 1.12.17 : F1 sur LIST, RING, MAP, POINTER... : la fiche du conteneur, de la reference.
    const std::string w = upper(word);
    static const std::pair<const char*, const char*> kWords[] = {
        {"ARRAY", "ARRAY"}, {"LIST", "LIST OF"}, {"VECTOR", "VECTOR OF"}, {"MAP", "MAP"}, {"SET", "SET OF"}, {"RING", "RING"},
        {"TUPLE", "TUPLE"}, {"REF_TO", "REF_TO"}, {"POINTER", "POINTER TO"}, {"MAP_ITERATOR", "MAP_ITERATOR"}};
    for (const auto& [word1, name] : kWords)
        if (w == word1) return std::string("type:") + name;
    for (const auto& g : hn::generics())                    // 1.12.1 ; 1.12.17 : sa propre fiche
        if (upper(g.name) == w) return "generique:" + std::string(g.name);
    return {};
}

} // namespace app::natives
