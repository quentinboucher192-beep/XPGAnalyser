#include "HmiNativesCards.hpp"

#include "HmiPanels.hpp"                // 1.12.1 : le libelle d'une propriete (hmiPropertyInfo)
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiNatives.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

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

const hn::Category* categoryOf(const hn::Function& f) { return hn::category(f.category); }

// Les categories qui ont des fonctions (op et instr sont les operateurs et les instructions).
std::vector<const hn::Category*> functionCategories() {
    std::vector<const hn::Category*> out;
    for (const auto& c : hn::categories()) {
        const bool any = std::any_of(hn::functions().begin(), hn::functions().end(), [&](const hn::Function& f) { return f.category == c.id; });
        if (any) out.push_back(&c);
    }
    return out;
}

std::string exampleLine(const hn::Example& e) {
    std::string code;
    if (!e.prelude.empty()) code += std::string(e.prelude) + "\n";
    code += "x := " + std::string(e.expression) + ";";
    return code;
}

// ------------------------------------------------------------------- les fiches ---
ui::HelpArticle functionCard(const hn::Function& f, std::string_view notation) {
    ui::HelpArticle a;
    const auto* cat = categoryOf(f);
    a.blocks.push_back(hero(std::string(f.name), {{cat ? std::string(cat->title) : std::string(f.category), "categorie:" + std::string(f.category)}},
                            {f.returns.empty() ? std::string("sans retour") : "rend " + std::string(f.returns), cat ? std::string(cat->title) : std::string{}},
                            ui::Icon::Code));
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
    for (const auto& g : hn::functions())
        if (g.category == f.category && g.name != f.name && see.size() < 10) see.push_back({std::string(g.name), "native:fonction:" + std::string(g.name), std::string(g.summary)});
    if (!see.empty()) a.blocks.push_back(links("Voir aussi", std::move(see)));
    return a;
}

ui::HelpArticle categoryCard(const hn::Category& c) {
    ui::HelpArticle a;
    std::size_t n = 0;
    std::string table = "Fonction\tSignature\tRend\tC\tC++\tfx";
    std::vector<ui::HelpLink> go;
    for (const auto& f : hn::functions()) {
        if (f.category != c.id) continue;
        ++n;
        table += "\n" + std::string(f.name) + "\t" + hn::shortSignature(f) + "\t" + (f.returns.empty() ? std::string("\xE2\x80\x94") : std::string(f.returns)) + "\t"
               + availShort(f.inC) + "\t" + availShort(f.inCpp) + "\t" + (f.expression ? "\xE2\x9C\x93" : "\xE2\x80\x94");
        go.push_back({std::string(f.name), "native:fonction:" + std::string(f.name), std::string(f.summary)});
    }
    a.blocks.push_back(hero(std::string(c.title), {{"Fonctions", "fonctions"}}, {std::to_string(n) + " fonction" + (n > 1 ? "s" : ""), std::string(c.group)},
                            ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, std::string(c.summary)));
    a.blocks.push_back(block(K::Table, table));
    auto legend = block(K::Paragraph, "C, C++ : \xE2\x9C\x93 le m\xC3\xAAme nom \xC2\xB7 ~ une autre \xC3\xA9" "criture \xC2\xB7 \xE2\x80\x94 pas d'\xC3\xA9quivalent. "
                                      "fx : s'\xC3\xA9" "crit dans une expression de propri\xC3\xA9t\xC3\xA9, un texte \xC3\xA0 trous, une condition.");
    legend.muted = true;
    a.blocks.push_back(std::move(legend));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle functionsCard() {
    ui::HelpArticle a;
    std::string table = "Cat\xC3\xA9gorie\tFonctions\tCe qu'elles font";
    std::vector<ui::HelpLink> go;
    std::size_t total = 0;
    for (const auto* c : functionCategories()) {
        std::size_t n = 0;
        for (const auto& f : hn::functions()) n += f.category == c->id ? 1 : 0;
        total += n;
        table += "\n" + std::string(c->title) + "\t" + std::to_string(n) + "\t" + std::string(c->summary);
        go.push_back({std::string(c->title), "native:categorie:" + std::string(c->id), std::string(c->summary)});
    }
    table += "\nConversions X_TO_Y\t" + std::to_string(hn::conversions().size()) + "\tDe chaque type de base vers chaque autre (INT_TO_REAL...)";
    go.push_back({"Conversions X_TO_Y", "native:conversions", {}});
    a.blocks.push_back(hero("Fonctions", {}, {std::to_string(total + hn::conversions().size()) + " fonctions"}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, "Toutes les fonctions du langage de l'IHM, sans rien d\xC3\xA9" "clarer : celles de la norme, du dialecte, les IHM_, "
                                      "les couleurs et l'al\xC3\xA9" "atoire. Une fonction du projet de m\xC3\xAAme nom passe avant."));
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les cat\xC3\xA9gories", std::move(go)));
    return a;
}

ui::HelpArticle conversionsCard() {
    ui::HelpArticle a;
    const auto& types = hn::conversionTypes();
    a.blocks.push_back(hero("Conversions X_TO_Y", {{"Fonctions", "fonctions"}}, {std::to_string(hn::conversions().size()) + " conversions"}, ui::Icon::Folder));
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
    a.blocks.push_back(hero(std::string(from) + "_TO_\xE2\x80\xA6", {{"Fonctions", "fonctions"}, {"Conversions X_TO_Y", "conversions"}},
                            {std::to_string(n) + " conversions"}, ui::Icon::Folder));
    a.blocks.push_back(block(K::Lead, "D'un " + std::string(from) + " vers chaque autre type de base."));
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle conversionCard(const hn::Conversion& c, std::string_view notation) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(c.name, {{"Fonctions", "fonctions"}, {"Conversions X_TO_Y", "conversions"}, {c.from + "_TO_\xE2\x80\xA6", "conv-source:" + c.from}},
                            {c.from + " \xE2\x86\x92 " + c.to}, ui::Icon::Code));
    a.blocks.push_back(block(K::Lead, c.behaviour));
    a.blocks.push_back(block(K::Heading, "Signature"));
    {
        auto code = block(K::Code, c.name + "(IN : " + c.from + ") : " + c.to);
        code.label = "ST";
        a.blocks.push_back(std::move(code));
    }
    a.blocks.push_back(block(K::Heading, "Dans chaque langage"));
    a.blocks.push_back(notations(c.st, c.c, c.cpp, notation));
    a.blocks.push_back(block(K::Table, "Script, fonction IHM\t\xE2\x9C\x93\nExpression fx, texte \xC3\xA0 trous, condition\t\xE2\x9C\x93"));
    std::vector<ui::HelpLink> see{{"TO_" + c.to, "native:fonction:TO_" + c.to, "La conversion g\xC3\xA9n\xC3\xA9rique vers " + c.to}};
    if (hn::conversion(c.to + "_TO_" + c.from)) see.push_back({c.to + "_TO_" + c.from, "native:conversion:" + c.to + "_TO_" + c.from, "Le chemin inverse"});
    a.blocks.push_back(links("Voir aussi", std::move(see)));
    return a;
}

ui::HelpArticle typesCard() {
    ui::HelpArticle a;
    const auto cards = hn::typeCards();
    std::string table = "Type\tMIN\tMAX\tTaille\tModbus";
    std::vector<ui::HelpLink> go;
    for (const auto& t : cards) {
        table += "\n" + t.name + "\t" + t.minText + "\t" + t.maxText + "\t" + t.size + "\t" + t.modbus;
        go.push_back({t.name, "native:type:" + t.name, t.summary});
    }
    a.blocks.push_back(hero("Types", {}, {std::to_string(cards.size()) + " types de base"}, ui::Icon::DerivedType));
    a.blocks.push_back(block(K::Lead, "Les types de base des variables, des param\xC3\xA8tres et des membres : leur plage, leur taille, "
                                      "leur place dans un \xC3\xA9quipement Modbus, leur \xC3\xA9" "criture en C et en C++."));
    a.blocks.push_back(block(K::Table, table));
    if (!hn::constructed().empty()) {
        a.blocks.push_back(block(K::Heading, "Les types construits"));
        std::string t2 = "Forme\tExemple\tCe que c'est";
        for (const auto& c : hn::constructed()) t2 += "\n" + std::string(c.name) + "\t" + std::string(c.example) + "\t" + std::string(c.summary);
        a.blocks.push_back(block(K::Table, t2));
    }
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle typeCardArticle(const hn::TypeCard& t) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(t.name, {{"Types", "types"}}, {t.category, t.size}, ui::Icon::DerivedType));
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

ui::HelpArticle enumsCard() {
    ui::HelpArticle a;
    std::string table = "\xC3\x89num\xC3\xA9ration\tValeurs\tSert \xC3\xA0";
    std::vector<ui::HelpLink> go;
    for (const auto& e : hn::enums()) {
        std::string values;
        for (const auto& v : e.values) values += (values.empty() ? "" : ", ") + std::string(v.name);
        std::string serves = std::string(e.function);
        if (!e.uses.empty()) serves += (serves.empty() ? "" : ", ") + usesText(e);
        table += "\n" + std::string(e.name) + "\t" + values + "\t" + (serves.empty() ? std::string(e.summary) : serves);
        go.push_back({std::string(e.name), "native:enum:" + std::string(e.name), std::string(e.summary)});
    }
    a.blocks.push_back(hero("\xC3\x89num\xC3\xA9rations", {}, {std::to_string(hn::enums().size()) + " \xC3\xA9num\xC3\xA9rations natives"}, ui::Icon::Constant));
    a.blocks.push_back(block(K::Lead, "Les valeurs nomm\xC3\xA9" "es que les fonctions IHM_ attendent et que les propri\xC3\xA9t\xC3\xA9s des objets prennent : "
                                      "NOM#Valeur vaut son nombre (un DINT) ; la fonction, ou la propri\xC3\xA9t\xC3\xA9 pilot\xC3\xA9" "e par une expression (\xC6\x92), "
                                      "re\xC3\xA7oit le mot qu'elle lisait d\xC3\xA9j\xC3\xA0. La v\xC3\xA9rification les conna\xC3\xAEt : une valeur mal \xC3\xA9" "crite est une erreur."));
    a.blocks.push_back(block(K::Table, table));
    a.blocks.push_back(links("Les fiches", std::move(go)));
    return a;
}

ui::HelpArticle enumCard(const hn::NativeEnum& e, std::string_view valueName) {
    ui::HelpArticle a;
    a.blocks.push_back(hero(std::string(e.name), {{"\xC3\x89num\xC3\xA9rations", "enumerations"}},
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
    a.blocks.push_back(hero("Natives", {}, {std::to_string(functions) + " fonctions", std::to_string(hn::typeCards().size()) + " types"}, ui::Icon::Lock));
    a.blocks.back().links.erase(a.blocks.back().links.begin());   // pas de « Natives » devant Natives
    a.blocks.push_back(block(K::Lead, "Tout ce que le langage de l'IHM conna\xC3\xAEt sans rien d\xC3\xA9" "clarer : ses fonctions, ses types, ses op\xC3\xA9rateurs, "
                                      "ses instructions, ses \xC3\xA9num\xC3\xA9rations. Verrouill\xC3\xA9" "es : elles se lisent, s'ins\xC3\xA8rent, ne se modifient pas. "
                                      "Chaque fiche dit o\xC3\xB9 l'\xC3\xA9" "crire et comment en ST (ex\xC3\xA9" "cut\xC3\xA9 en simulation), en C et en C++."));
    a.blocks.push_back(block(K::Table, "Fonctions\t" + std::to_string(functions) + " (dont " + std::to_string(hn::conversions().size()) + " X_TO_Y)"
                                       "\nTypes\t" + std::to_string(hn::typeCards().size()) + "\nOp\xC3\xA9rateurs\t" + std::to_string(hn::operators().size())
                                       + "\nInstructions\t" + std::to_string(hn::instructions().size()) + "\n\xC3\x89num\xC3\xA9rations\t" + std::to_string(hn::enums().size())));
    auto tip = block(K::Callout, "Double-clic sur une native de l'arbre (ou Ins\xC3\xA9rer) : son appel dans le script montr\xC3\xA9. "
                                 "F1 sur un nom dans un script : sa fiche.");
    tip.label = "Astuce";
    a.blocks.push_back(std::move(tip));
    a.blocks.push_back(links("Les rubriques", {{"Fonctions", "native:fonctions", {}}, {"Types", "native:types", {}}, {"Op\xC3\xA9rateurs", "native:operateurs", {}},
                                                {"Instructions", "native:instructions", {}}, {"\xC3\x89num\xC3\xA9rations", "native:enumerations", {}}}));
    return a;
}

std::string after(std::string_view key, std::string_view prefix) {
    return key.rfind(prefix, 0) == 0 ? std::string(key.substr(prefix.size())) : std::string{};
}

} // namespace

ui::HelpArticle article(std::string_view key, std::string_view notation) {
    if (key == "natives") return rootCard();
    if (key == "fonctions") return functionsCard();
    if (key == "conversions") return conversionsCard();
    if (key == "types") return typesCard();
    if (key == "operateurs") return operatorsCard();
    if (key == "instructions") return instructionsCard();
    if (key == "enumerations") return enumsCard();
    if (const auto id = after(key, "categorie:"); !id.empty())
        if (const auto* c = hn::category(id)) return categoryCard(*c);
    if (const auto n = after(key, "fonction:"); !n.empty())
        if (const auto* f = hn::function(n)) return functionCard(*f, notation);
    if (const auto t = after(key, "conv-source:"); !t.empty()) return convSourceCard(t);
    if (const auto n = after(key, "conversion:"); !n.empty())
        if (const auto c = hn::conversion(n)) return conversionCard(*c, notation);
    if (const auto n = after(key, "type:"); !n.empty())
        if (const auto t = hn::typeCard(n)) return typeCardArticle(*t);
    if (const auto n = after(key, "operateur:"); !n.empty()) {
        const auto i = static_cast<std::size_t>(std::atoi(n.c_str()));
        if (i < hn::operators().size()) return operatorCard(i, notation);
    }
    if (const auto n = after(key, "instruction:"); !n.empty()) {
        const auto i = static_cast<std::size_t>(std::atoi(n.c_str()));
        if (i < hn::instructions().size()) return instructionCard(i, notation);
    }
    if (const auto n = after(key, "enum:"); !n.empty())
        if (const auto* e = hn::nativeEnum(n)) return enumCard(*e, {});
    if (const auto n = after(key, "enum-valeur:"); !n.empty()) {
        const auto hash = n.find('#');
        if (const auto* e = hn::nativeEnum(n.substr(0, hash)); e && hash != std::string::npos) return enumCard(*e, n.substr(hash + 1));
    }
    return {};
}

std::string title(std::string_view key) {
    std::string t;
    if (const auto colon = key.find(':'); colon != std::string_view::npos) t = std::string(key.substr(colon + 1));
    else if (key == "natives") return "IHM \xC2\xB7 Natives";
    else t = std::string(key);
    if (key.rfind("categorie:", 0) == 0)
        if (const auto* c = hn::category(t)) t = std::string(c->title);
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

std::string insertText(std::string_view key) {
    if (const auto n = after(key, "fonction:"); !n.empty())
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
    if (const auto n = after(key, "type:"); !n.empty()) return n;
    return {};
}

std::vector<std::string> allKeys() {
    std::vector<std::string> out{"natives", "fonctions"};
    for (const auto* c : functionCategories()) {
        out.push_back("categorie:" + std::string(c->id));
        for (const auto& f : hn::functions())
            if (f.category == c->id) out.push_back("fonction:" + std::string(f.name));
    }
    out.push_back("conversions");
    for (const auto& t : hn::conversionTypes()) {
        out.push_back("conv-source:" + std::string(t));
        for (const auto& c : hn::conversions())
            if (c.from == t) out.push_back("conversion:" + c.name);
    }
    out.push_back("types");
    for (const auto& t : hn::typeCards()) out.push_back("type:" + t.name);
    out.push_back("operateurs");
    for (std::size_t i = 0; i < hn::operators().size(); ++i) out.push_back("operateur:" + std::to_string(i));
    out.push_back("instructions");
    for (std::size_t i = 0; i < hn::instructions().size(); ++i) out.push_back("instruction:" + std::to_string(i));
    out.push_back("enumerations");
    for (const auto& e : hn::enums()) {
        out.push_back("enum:" + std::string(e.name));
        for (const auto& v : e.values) out.push_back("enum-valeur:" + std::string(e.name) + "#" + std::string(v.name));
    }
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
    return {};
}

} // namespace app::natives
