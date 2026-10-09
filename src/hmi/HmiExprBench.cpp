// hmi/HmiExprBench.cpp - 1.11 (chantier T3, D5) : le champ d'essai, sans ecran.
// Le vrai moteur (Expression, TextTemplate, exprcheck) sur un petit projet de
// demonstration ; devant lui, les regles de la page (les 20 erreurs de la
// maquette), chacune avec sa raison et, quand elle est sure, sa correction.
#include "HmiExprBench.hpp"

#include "HmiEnums.hpp"
#include "HmiExpr.hpp"
#include "HmiExprCheck.hpp"
#include "HmiModel.hpp"
#include "HmiOperators.hpp"
#include "HmiRuntime.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>

namespace hmi::exprguide {

namespace {

std::string upper(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}
std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool pathChar(char c) { return identChar(c) || c == '.' || c == '[' || c == ']'; }
// 1.11 (tranche 25, avec REP) : le texte sans les $ de ses reperes, pour lire un type (jamais pour une position ni une
// correction, qui gardent les $). Apres le moteur seulement : hors d'une chaine, un $ qui l'a passe est celui d'un
// repere ; dans une chaine, $', $N, $$ sont des echappements, gardes tels quels.
std::string withoutMarkers(std::string_view s) {
    std::string out;
    char quote = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote != 0) {
            out += c;
            if (c == '$' && i + 1 < s.size()) out += s[++i];
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') quote = c;
        if (c != '$') out += c;
    }
    return out;
}
std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}
// La distance d'edition, sans casse : pour "veux-tu dire".
std::size_t distance(std::string_view a, std::string_view b) {
    const std::string x = upper(a), y = upper(b);
    std::vector<std::size_t> row(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= x.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= y.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (x[i - 1] == y[j - 1] ? 0u : 1u)});
            diag = up;
        }
    }
    return row[y.size()];
}
// Le plus proche : un prefixe d'abord (Automatique -> Auto), sinon la distance (<= 3).
std::string nearest(std::string_view name, const std::vector<std::string>& candidates) {
    const std::string u = upper(name);
    for (const auto& c : candidates)
        if (!c.empty() && (u.rfind(upper(c), 0) == 0 || upper(c).rfind(u, 0) == 0)) return c;
    std::string best;
    std::size_t bestD = 4;
    for (const auto& c : candidates)
        if (const auto d = distance(name, c); d < bestD) { bestD = d; best = c; }
    return best;
}

// Les bornes d'un "ARRAY[0..3] OF T" (faux : pas un tableau).
bool arrayBounds(std::string_view type, long& lo, long& hi, std::string& of) {
    const std::string u = upper(type);
    if (u.rfind("ARRAY[", 0) != 0) return false;
    const auto dots = u.find("..");
    const auto close = u.find(']');
    if (dots == std::string::npos || close == std::string::npos) return false;
    lo = std::strtol(u.c_str() + 6, nullptr, 10);
    hi = std::strtol(u.c_str() + dots + 2, nullptr, 10);
    const auto ofAt = u.find(" OF ", close);
    of = ofAt == std::string::npos ? std::string{} : trim(std::string_view(type).substr(ofAt + 4));
    return true;
}

Project demoProject() {
    Project p;
    // T_MODE : Defaut vaut 3 (MUX(TO_INT(Mode), quatre couleurs) reste dans la liste).
    HmiType e = makeEnumeration(p, "T_MODE");
    e.values = {{"Arret", 0, "\xC3\x80 l'arr\xC3\xAAt", ""}, {"Auto", 1, "Automatique", ""}, {"Manu", 2, "Manuel", ""},
                {"Defaut", 3, "En d\xC3\xA9" "faut", ""}};
    p.programs.types.push_back(std::move(e));
    (void)regenerateEnumConversions(p, p.programs.types.back());
    HmiType v;
    v.id = p.allocate();
    v.name = "T_VANNE";
    v.members = {{"Pos", "REAL", "", "l'ouverture, en %"}, {"Defaut", "BOOL", "", ""}, {"Bouge", "BOOL", "", ""},
                 {"Cmd", "BOOL", "", ""}};
    p.programs.types.push_back(v);
    const auto var = [&p](std::string name, std::string type, std::string initial) {
        Variable x;
        x.id = p.allocate();
        x.name = std::move(name);
        x.type = std::move(type);
        x.initial = std::move(initial);
        p.programs.variables.push_back(std::move(x));
    };
    var("V", "ARRAY[0..3] OF T_VANNE", "");
    var("Pression", "REAL", "3.8");
    var("Mode", "T_MODE", "Auto");
    var("Pompe_Marche", "BOOL", "TRUE");
    var("Debit_P3", "REAL", "12.5");
    var("Niveau_Cuve", "REAL", "64.0");
    var("Vue_Demandee", "STRING", "'Vue_Synoptique'");
    for (const char* n : {"Vue_Accueil", "Vue_Armoire_A", "Vue_Synoptique", "Vue_Commandes"}) p.views.push_back(makeView(p, n));
    // Recette 1.11 (T3, tranche 14) : SYS.UserLevel vaut 2, comme le disent les legendes
    // des exemples (« un calcul : 20 », « V[2] »). Sans securite, il vaudrait 99 : un
    // utilisateur de depart du groupe Maintenance (niveau 2) est connecte au demarrage.
    UserGroup g;
    g.id = p.allocate();
    g.name = "Maintenance";
    g.level = 2;
    p.security.groups.push_back(g);
    User u;
    u.id = p.allocate();
    u.login = "exemple";
    u.group = g.id;
    p.security.users.push_back(u);
    p.security.startUser = u.login;
    p.security.enabled = true;
    return p;
}

// La liste de choix de la page (l'alignement d'un texte) et ses synonymes surs.
const std::vector<std::string>& choiceList() {
    static const std::vector<std::string> k = {"gauche", "centre", "droite"};
    return k;
}
std::string choiceSynonym(std::string_view s) {
    const std::string u = upper(s);
    if (u == "MILIEU" || u == "CENTER" || u == "CENTRE") return "centre";
    if (u == "LEFT") return "gauche";
    if (u == "RIGHT") return "droite";
    return {};
}
std::string colorByName(std::string_view s) {
    const std::string u = upper(s);
    if (u == "ROUGE" || u == "RED") return "#E53935";
    if (u == "VERT" || u == "GREEN") return "#43A047";
    if (u == "ORANGE") return "#FB8C00";
    if (u == "BLEU" || u == "BLUE") return "#1E88E5";
    if (u == "GRIS" || u == "GREY" || u == "GRAY") return "#9E9E9E";
    if (u == "NOIR" || u == "BLACK") return "#000000";
    if (u == "BLANC" || u == "WHITE") return "#FFFFFF";
    return {};
}

exprcheck::Want wantOf(const TypeEntry& t) {
    switch (t.kind) {
        case Kind::Bool: return exprcheck::Want::Bool;
        case Kind::Integer:
        case Kind::Real: return exprcheck::Want::Number;
        case Kind::Text: return exprcheck::Want::Text;
        case Kind::Color: return exprcheck::Want::Color;
        default: return exprcheck::Want::Any;
    }
}

} // namespace

struct Bench::State {
    Project                     demo = demoProject();
    const Project*              project{nullptr};
    sim::Environment*           live{nullptr};
    std::unique_ptr<Runtime>    rt;

    void bindDemo() {
        project = &demo;
        live = nullptr;
        rt = std::make_unique<Runtime>();
        rt->bind(&demo, nullptr);     // l'IHM seule : les fonctions standard restent la
        rt->start(0.0);
        auto& env = rt->environment();
        (void)env.write("V[0].Pos", sim::Value::real(42.0));
        (void)env.write("V[1].Pos", sim::Value::real(85.0));
        (void)env.write("V[2].Pos", sim::Value::real(12.0));
        (void)env.write("V[3].Pos", sim::Value::real(100.0));
    }
    sim::Environment& env() { return live ? *live : rt->environment(); }
};

Bench::Bench() : s_(std::make_unique<State>()) { s_->bindDemo(); }
Bench::~Bench() = default;

void Bench::onProject(const Project& project, sim::Environment* live) {
    s_->project = &project;
    s_->live = live;
    if (!live) {
        s_->rt = std::make_unique<Runtime>();
        s_->rt->bind(&project, nullptr);
        // Tranche 8 (decision du chef) : prime, pas start. start jouait les scripts
        // Demarrage et ouvrait la vue de demarrage (ses OnOpen, ses actions) : un
        // compteur incremente a l'ouverture valait deja 1 dans le champ d'essai.
        s_->rt->prime(0.0);
    }
}
void Bench::onSamples() { s_->bindDemo(); }
bool Bench::usesSamples() const noexcept { return s_->project == &s_->demo; }
std::string Bench::sourceNote() const {
    if (usesSamples()) return {};
    if (s_->live)
        return "Projet ouvert : les valeurs de la simulation en marche, en lecture seule.";
    return "Projet ouvert, IHM non d\xC3\xA9marr\xC3\xA9" "e : les variables IHM valent leur valeur initiale "
           "(aucun script D\xC3\xA9marrage, aucune vue ouverte) ; celles de l'automate ne sont pas lues.";
}

bool Bench::setSample(std::string_view name, const sim::Value& value) {
    if (!usesSamples()) return false;
    const std::string u = upper(name);
    static const char* kSamples[] = {"V[0].POS", "PRESSION", "MODE", "POMPE_MARCHE", "V[0].DEFAUT", "DEBIT_P3", "NIVEAU_CUVE"};
    if (std::none_of(std::begin(kSamples), std::end(kSamples), [&](const char* k) { return u == k; })) return false;
    return s_->rt->environment().write(name, value);
}
sim::Value Bench::sample(std::string_view name) const {
    sim::Value v;
    (void)s_->env().read(name, v);
    return v;
}

Outcome Bench::evaluate(std::string_view field, const TypeEntry& as) const {
    Outcome o;
    const Project& p = *s_->project;
    const bool eq = as.keepEquals;
    std::string body(field);
    std::size_t lead = 0;
    if (eq) {
        while (lead < body.size() && std::isspace(static_cast<unsigned char>(body[lead]))) ++lead;
        if (lead < body.size() && body[lead] == '=') ++lead;
        body = body.substr(lead);
    }
    const auto fail = [&](std::size_t b, std::size_t e, std::string reason, std::string fixedBody = {}) {
        o.ok = false;
        o.errBegin = std::min(field.size(), lead + b);
        o.errEnd = std::min(field.size(), lead + std::max(e, b + 1));
        o.reason = std::move(reason);
        o.replacement = fixedBody.empty() ? std::string{} : (eq ? "=" : "") + fixedBody;
        return o;
    };
    const auto whole = [&](std::string reason, std::string fixedBody = {}) {
        return fail(0, body.size(), std::move(reason), std::move(fixedBody));
    };
    // ---- le texte a trous : tel quel, sans "=" ; une accolade fermee ----
    if (as.kind == Kind::Template) {
        const std::string t = trim(body);
        if (!t.empty() && t[0] == '=')
            return whole("Un texte \xC3\xA0 trous ne commence pas par = : le texte s'\xC3\xA9" "crit tel quel, les trous entre accolades.",
                         trim(std::string_view(t).substr(1)));
        int depth = 0;
        std::size_t open = std::string::npos;
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '{') { ++depth; open = i; }
            else if (body[i] == '}') --depth;
        }
        if (depth > 0 && open != std::string::npos) {
            std::size_t end = open + 1;
            while (end < body.size() && (pathChar(body[end]) || body[end] == ':')) ++end;
            std::string fixed = body;
            fixed.insert(end, "}");
            return fail(open, end, "L'accolade n'est pas ferm\xC3\xA9" "e.", fixed);
        }
        const TextTemplate tt = TextTemplate::compile(body);
        if (const auto errs = tt.errors(); !errs.empty()) return whole(errs.front());
        o.ok = true;
        o.value = tt.render(s_->env());
        o.type = "STRING";
        o.fits = "convient \xC3\xA0 une case \xC2\xAB texte \xC2\xBB";
        return o;
    }
    const std::string s = trim(body);
    if (s.empty()) return whole("\xC3\x89" "cris une expression : un nom, un nombre, une condition\xE2\x80\xA6");

    // ---- les fautes lexicales, a correction sure ----
    {   // #RRGGBB sans apostrophes
        std::string fixed = body;
        bool seen = false;
        std::size_t first = 0;
        for (std::size_t i = 0; i < fixed.size(); ++i) {
            if (fixed[i] != '#' || (i > 0 && (identChar(fixed[i - 1]) || fixed[i - 1] == '\''))) continue;
            std::size_t n = 0;
            while (i + 1 + n < fixed.size() && std::isxdigit(static_cast<unsigned char>(fixed[i + 1 + n]))) ++n;
            if (n != 6) continue;
            if (!seen) first = i;
            seen = true;
            fixed.insert(i + 7, "'");
            fixed.insert(i, "'");
            i += 8;
        }
        if (seen) return fail(first, first + 7, "Caract\xC3\xA8re inattendu # : dans une expression, une couleur est une cha\xC3\xAEne, entre apostrophes.", trim(fixed));
    }
    if (const auto q = body.find('"'); q != std::string::npos)
        return fail(q, body.find('"', q + 1) == std::string::npos ? q + 1 : body.find('"', q + 1) + 1,
                    "En ST, un texte s'\xC3\xA9" "crit entre apostrophes, pas entre guillemets.", trim(replaceAll(body, "\"", "'")));
    {   // la virgule decimale
        int depth = 0;
        bool quoted = false;
        for (std::size_t i = 0; i < body.size(); ++i) {
            const char c = body[i];
            if (c == '\'') quoted = !quoted;
            if (quoted) continue;
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (c == ',' && i > 0 && i + 1 < body.size() && std::isdigit(static_cast<unsigned char>(body[i - 1]))
                && std::isdigit(static_cast<unsigned char>(body[i + 1]))) {
                std::string fixed = body;
                fixed[i] = '.';
                return fail(i, i + 1, "En ST, la virgule s\xC3\xA9pare les arguments : un nombre d\xC3\xA9" "cimal s'\xC3\xA9" "crit avec un point.",
                            depth == 0 ? trim(fixed) : std::string{});
            }
        }
    }
    if (const auto a = body.find(":="); a != std::string::npos)
        return fail(a, a + 2, "Une case d'expression lit, elle n'\xC3\xA9" "crit pas : := est pour une action ou un script. Pour comparer, \xC3\xA9" "cris =.",
                    trim(body.substr(0, a) + "=" + body.substr(a + 2)));
    if (const auto sc = body.find(';'); sc != std::string::npos && body.find('\'') == std::string::npos)
        return fail(sc, sc + 1, "Une expression n'a pas de ; : elle tient en une seule formule.", trim(replaceAll(body, ";", "")));
    {   // les operateurs du C
        std::string fixed = replaceAll(replaceAll(replaceAll(replaceAll(body, "&&", "AND"), "||", "OR"), "!=", "<>"), "==", "=");
        for (std::size_t i = 0; i < fixed.size(); ++i)
            if (fixed[i] == '!') { fixed.replace(i, 1, "NOT "); }
        fixed = replaceAll(fixed, "NOT  ", "NOT ");
        if (fixed != body) {
            std::size_t at = body.find_first_of("&|!=");
            if (body.find("&&") != std::string::npos) at = body.find("&&");
            else if (body.find("||") != std::string::npos) at = body.find("||");
            return fail(at, at + 2, "&& et ! sont du C : en ST, on \xC3\xA9" "crit AND, OR, NOT, <> et =.", trim(fixed));
        }
    }
    // T#5 : une duree sans unite
    for (std::size_t at = upper(s).find("T#"); at != std::string::npos; at = upper(s).find("T#", at + 2)) {
        if (at > 0 && identChar(s[at - 1])) continue;
        std::size_t i = at + 2;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
        if (i > at + 2 && (i == s.size() || !std::isalpha(static_cast<unsigned char>(s[i]))))
            return fail(at, i, "Une dur\xC3\xA9" "e a toujours son unit\xC3\xA9 : ms, s, m, h ou d.", s.substr(0, i) + "s" + s.substr(i));
    }
    // ---- la syntaxe : le vrai moteur ----
    const Expression ex = Expression::compile(s);
    if (!ex.valid()) {
        const long open = std::count(s.begin(), s.end(), '(') - std::count(s.begin(), s.end(), ')');
        if (open > 0) return fail(body.size(), body.size() + 1, "Une parenth\xC3\xA8se est ouverte et pas ferm\xC3\xA9" "e : ajoute ).", s + std::string(static_cast<std::size_t>(open), ')'));
        // 5s : une duree sans T#
        if (s.size() > 1 && std::isdigit(static_cast<unsigned char>(s[0])) && std::string("smhd").find(static_cast<char>(std::tolower(static_cast<unsigned char>(s.back())))) != std::string::npos)
            return whole("Une dur\xC3\xA9" "e commence par T# (ou TIME#).", "T#" + s);
        return whole(ex.error().empty() ? "Cette expression ne se lit pas : regarde l'\xC3\xA9l\xC3\xA9ment soulign\xC3\xA9." : ex.error());
    }
    // ---- les regles de type de la page ----
    // T_MODE#Inconnue : la valeur la plus proche
    for (const auto* en : enumerations(p)) {
        const std::string prefix = upper(en->name) + "#";
        for (std::size_t at = upper(s).find(prefix); at != std::string::npos; at = upper(s).find(prefix, at + 1)) {
            std::size_t i = at + prefix.size();
            while (i < s.size() && identChar(s[i])) ++i;
            const std::string value = s.substr(at + prefix.size(), i - at - prefix.size());
            // 1.11 (tranche 25, avec REP) : T_MODE#$Manu$, un repere juste apres le # : la valeur est dans le repere,
            // que le moteur a lu. Sans repere, un # sans valeur ne passe pas le moteur (ci-dessus).
            if (value.empty()) continue;
            if (enumValueByName(*en, value)) continue;
            std::vector<std::string> names;
            for (const auto& v : en->values) names.push_back(v.name);
            const std::string near = nearest(value, names);
            return fail(at, i, "Cette valeur n'existe pas" + (near.empty() ? std::string(".") : " : veux-tu dire " + en->name + "#" + near + " ?"),
                        near.empty() ? std::string{} : s.substr(0, at) + en->name + "#" + near + s.substr(i));
        }
    }
    // Les chemins : la racine, l'indice, le membre ; les comparaisons et MOD.
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\'') { const auto q = s.find('\'', i + 1); i = q == std::string::npos ? s.size() : q + 1; continue; }
        // 1.11 (tranche 25, avec REP) : apres un '.', c'est un membre, pas une racine ($V[0]$.Mode ne lit pas la
        // variable Mode). Sans repere, un '.' est toujours pris dans le chemin qui le precede.
        if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '#' || s[i - 1] == '.'))) { ++i; continue; }
        std::size_t j = i;
        while (j < s.size() && pathChar(s[j])) ++j;
        // 1.11 (tranche 25, avec REP) : un repere peut couper un chemin (V[0].$Pos$, $V[0]$.Nom). Un
        // morceau colle par un $ a d'autres lettres n'est pas le chemin entier : ses regles se taisent (le moteur a lu
        // l'expression sans les $, et le jugement de l'inspecteur, plus bas, la lit de meme). Une aide en moins, jamais
        // une fausse erreur. Un chemin entoure ($V[9].Pos$) garde ses regles : leurs corrections restent entre ses $.
        // Sans repere, aucun $ ne passe le moteur (ci-dessus) : rien ne change.
        if ((i > 1 && s[i - 1] == '$' && pathChar(s[i - 2])) || (j + 1 < s.size() && s[j] == '$' && pathChar(s[j + 1]))) { i = j; continue; }
        const std::string path = s.substr(i, j - i);
        std::size_t rootEnd = 0;
        while (rootEnd < path.size() && identChar(path[rootEnd])) ++rootEnd;
        const std::string root = path.substr(0, rootEnd);
        const Variable* var = p.variable(root);
        if (var) {
            long lo = 0, hi = 0;
            std::string of;
            if (arrayBounds(var->type, lo, hi, of)) {
                if (rootEnd >= path.size() || path[rootEnd] != '[')
                    return fail(i, j, root + " est un tableau : dis lequel, de " + root + "[" + std::to_string(lo) + "] \xC3\xA0 " + root + "["
                                          + std::to_string(hi) + "].",
                                s.substr(0, i) + root + "[" + std::to_string(lo) + "]" + s.substr(i + rootEnd));
                const auto close = path.find(']', rootEnd);
                const std::string idx = path.substr(rootEnd + 1, close == std::string::npos ? std::string::npos : close - rootEnd - 1);
                if (!idx.empty() && std::all_of(idx.begin(), idx.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
                    const long n = std::strtol(idx.c_str(), nullptr, 10);
                    if (n < lo || n > hi)
                        return fail(i + rootEnd + 1, i + rootEnd + 1 + idx.size(),
                                    "Indice hors bornes : " + root + " va de " + std::to_string(lo) + " \xC3\xA0 " + std::to_string(hi) + ".",
                                    s.substr(0, i + rootEnd + 1) + std::to_string(n < lo ? lo : hi) + s.substr(i + rootEnd + 1 + idx.size()));
                }
                if (close != std::string::npos && close + 1 < path.size() && path[close + 1] == '.') {
                    std::size_t m = close + 2, me = m;
                    while (me < path.size() && identChar(path[me])) ++me;
                    const std::string member = path.substr(m, me - m);
                    std::vector<std::string> names;
                    bool found = false;
                    for (const auto& tm : types::membersOf(p, of)) {
                        names.push_back(tm.name);
                        found = found || upper(tm.name) == upper(member);
                    }
                    if (!names.empty() && !found) {
                        const std::string near = nearest(member, names);
                        return fail(i + m, i + me, "Le membre " + member + " n'existe pas dans " + of
                                                       + (near.empty() ? std::string(".") : " : veux-tu dire " + near + " ?"),
                                    near.empty() ? std::string{} : s.substr(0, i + m) + near + s.substr(i + me));
                    }
                }
            }
            const std::string type = upper(types::typeOfPath(p, path));
            // la suite : "= 1", "MOD n"
            // 1.11 (tranche 25, avec REP) : pas apres le $ qui ferme un repere ($V[0].Defaut$ = 1) : la correction
            // passerait par-dessus ce $. Elle se tait.
            if (j < s.size() && s[j] == '$') { i = j; continue; }
            std::size_t k = j;
            while (k < s.size() && s[k] == ' ') ++k;
            std::string after = upper(s.substr(k));
            const bool cmp = after.rfind("=", 0) == 0 || after.rfind("<>", 0) == 0;
            std::size_t numAt = k + (after.rfind("<>", 0) == 0 ? 2 : 1);
            while (numAt < s.size() && s[numAt] == ' ') ++numAt;
            std::size_t numEnd = numAt;
            while (numEnd < s.size() && std::isdigit(static_cast<unsigned char>(s[numEnd]))) ++numEnd;
            const bool intAfter = cmp && numEnd > numAt && (numEnd == s.size() || !pathChar(s[numEnd]));
            if (intAfter && type == "BOOL") {
                const bool one = s.substr(numAt, numEnd - numAt) != "0";
                const bool neg = after.rfind("<>", 0) == 0;
                return fail(i, numEnd, "Un BOOL ne se compare pas \xC3\xA0 un entier : \xC3\xA9" "cris son nom seul (ou = TRUE).",
                            s.substr(0, i) + ((one != neg) ? path : "NOT " + path) + s.substr(numEnd));
            }
            if (intAfter)
                if (const auto* en = findEnumeration(p, type)) {
                    const long n = std::strtol(s.c_str() + numAt, nullptr, 10);
                    for (const auto& v : en->values)
                        if (v.value == n)
                            return fail(i, numEnd, en->name + " et INT ne se comparent pas : \xC3\xA9" "cris " + en->name + "#" + v.name + " (ou TO_INT("
                                                       + path + ") = " + std::to_string(n) + ").",
                                        s.substr(0, numAt) + en->name + "#" + v.name + s.substr(numEnd));
                    return fail(i, numEnd, en->name + " et INT ne se comparent pas : compare \xC3\xA0 une valeur " + en->name + "#\xE2\x80\xA6");
                }
            if (after.rfind("MOD", 0) == 0 && (type == "REAL" || type == "LREAL"))
                return fail(i, k + 3, "MOD ne se fait que sur des entiers : " + path + " est un REAL, convertis-le d'abord.",
                            s.substr(0, i) + "REAL_TO_INT(" + path + ")" + s.substr(j));
        }
        i = j;
    }
    // ---- une vue, une liste de choix, un nom de couleur ----
    const bool quotedText = s.size() >= 2 && s.front() == '\'' && s.back() == '\'' && s.find('\'', 1) == s.size() - 1;
    const std::string inner = quotedText ? s.substr(1, s.size() - 2) : std::string{};
    if (as.kind == Kind::View) {
        std::vector<std::string> views;
        for (const auto& v : p.views) views.push_back(v.name);
        const auto isView = [&](std::string_view n) { return std::any_of(views.begin(), views.end(), [&](const std::string& v) { return upper(v) == upper(n); }); };
        if (!quotedText && isView(s)) return whole("Sans apostrophes, " + s + " est lu comme une variable : le nom d'une vue est un texte.", "'" + s + "'");
        if (quotedText && !isView(inner)) {
            const std::string near = nearest(inner, views);
            return whole("Cette vue n'existe pas" + (near.empty() ? std::string(".") : " : veux-tu dire " + near + " ?"), near.empty() ? std::string{} : "'" + near + "'");
        }
    }
    if (as.kind == Kind::Choice && quotedText) {
        const auto& list = choiceList();
        if (std::none_of(list.begin(), list.end(), [&](const std::string& c) { return upper(c) == upper(inner); })) {
            const std::string near = !choiceSynonym(inner).empty() ? choiceSynonym(inner) : nearest(inner, list);
            return whole("Cette valeur n'est pas dans la liste : gauche, centre ou droite.", near.empty() ? std::string{} : "'" + near + "'");
        }
    }
    if (as.kind == Kind::Color && quotedText && inner.rfind("#", 0) != 0) {
        const std::string code = colorByName(inner);
        return whole("Un nom de couleur se choisit dans la palette (elle ins\xC3\xA8re le code) : dans une expression, '#RRGGBB'.",
                     code.empty() ? std::string{} : "'" + code + "'");
    }
    // '+' sur deux textes
    if (const auto plus = s.find(" + "); plus != std::string::npos) {
        const std::string l = trim(s.substr(0, plus)), r = trim(s.substr(plus + 3));
        const auto isText = [&](const std::string& x) {
            return (x.size() >= 2 && x.front() == '\'' && x.back() == '\'') || upper(types::typeOfPath(p, x)) == "STRING";
        };
        if (isText(l) && isText(r)) return fail(plus + 1, plus + 2, "+ ne colle pas deux textes : CONCAT le fait.", "CONCAT(" + l + ", " + r + ")");
    }
    // ---- le jugement de l'inspecteur ----
    exprcheck::Context ctx;
    ctx.project = &p;
    const Project* pp = &p;
    ctx.known = [pp](std::string_view root) {
        const std::string u = upper(root);
        if (u == "SYS" || u == "TRUE" || u == "FALSE" || pp->variable(root) || pp->viewByName(root)) return true;
        return findEnumeration(*pp, root) != nullptr;
    };
    std::vector<exprcheck::Problem> problems;
    // Tranche 15 : le jugement de la case par l'inspecteur (« un nombre est attendu… ») ne dit pas
    // la valeur. Il passe apres le tableau des croisements, qui la garde (« ne convient pas : … ») ;
    // il ne sert que si le tableau laisse passer (une autre branche de SEL n'est pas une couleur).
    std::string wantProblem;
    const auto plainProblems = exprcheck::check(ctx, s, exprcheck::Want::Any);
    for (auto& pb : exprcheck::check(ctx, s, wantOf(as))) {
        if (pb.warning) continue;                 // 1.11.21 : un avertissement - le moteur sert l'expression
        // TO_STRING(Mode), TO_T_MODE(2) : des conversions du dialecte ou du projet, que le moteur sert.
        const std::string unknownFn = "fonction inconnue : ";
        if (pb.message.rfind(unknownFn, 0) == 0) {
            std::size_t e = unknownFn.size();
            while (e < pb.message.size() && identChar(pb.message[e])) ++e;
            if (isConversion(p, pb.message.substr(unknownFn.size(), e - unknownFn.size()))) continue;
        }
        if (pb.unknownName.empty()
            && std::none_of(plainProblems.begin(), plainProblems.end(), [&](const exprcheck::Problem& q) { return q.message == pb.message; })) {
            if (wantProblem.empty()) wantProblem = pb.message;
            continue;
        }
        problems.push_back(std::move(pb));
    }
    if (!problems.empty()) {
        const auto& pb = problems.front();
        if (!pb.unknownName.empty()) {
            const auto at = s.find(pb.unknownName);
            std::string fixed;
            std::string message = pb.message;
            std::string suggestion = pb.suggestion;
            // Recette 1.11 (tranche 14) : « =Mode = Auto » - une valeur d'enumeration ecrite sans
            // son type. Si le nom est une valeur d'une enumeration du projet : T_MODE#Auto.
            if (suggestion.empty()) {
                const std::string u = upper(pb.unknownName);
                for (const auto& ty : p.programs.types) {
                    if (ty.kind != HmiTypeKind::Enumeration) continue;
                    const auto v = std::find_if(ty.values.begin(), ty.values.end(), [&](const HmiEnumValue& x) { return upper(x.name) == u; });
                    if (v == ty.values.end()) continue;
                    suggestion = ty.name + "#" + v->name;
                    message = pb.unknownName + " n'est pas une variable : une valeur d'\xC3\xA9num\xC3\xA9ration s'\xC3\xA9" "crit avec son type, "
                              + suggestion + ".";
                    break;
                }
            }
            if (!suggestion.empty() && at != std::string::npos) fixed = s.substr(0, at) + suggestion + s.substr(at + pb.unknownName.size());
            return at == std::string::npos ? whole(message, fixed) : fail(at, at + pb.unknownName.size(), message, fixed);
        }
        return whole(pb.message);
    }
    // ---- la valeur ----
    auto r = ex.evaluate(s_->env());
    if (!r) return whole(r.error().context.empty() ? std::string(r.error().message()) : r.error().context);
    const sim::Value& v = *r;
    o.ok = true;
    o.type = std::string(sim::toString(v.type()));
    o.value = v.display();
    if (v.type() == sim::Type::Time) {
        const long long ms = v.asInteger();
        o.value = ms % 1000 == 0 ? "T#" + std::to_string(ms / 1000) + "s" : "T#" + std::to_string(ms) + "ms";
    }
    // une enumeration : son nom de type et sa valeur (la variable, le litteral, TO_T_MODE).
    // Recette 1.11 (tranche 14) : jamais pour un BOOL - « =Mode = T_MODE#Auto » est une
    // comparaison (TRUE), qui s'affichait « T_MODE#Auto [T_MODE] ».
    // Tranche 15 : SEL / MUX de valeurs T_MODE# est une valeur T_MODE dans toute case (pas
    // seulement Enumeration) ; TO_INT(T_MODE#Manu) reste un entier.
    // Tranche 25 (avec REP) : lu sans les $ des reperes ($Mode$ est un T_MODE, comme Mode).
    const std::string bare = withoutMarkers(s);
    std::string enumType = v.type() == sim::Type::Bool ? std::string{} : upper(types::typeOfPath(p, bare));
    for (const auto* en : enumerations(p)) {
        const std::string u = upper(bare);
        if (v.type() == sim::Type::Bool) break;
        if (u.rfind(upper(en->name) + "#", 0) == 0 || u.rfind("TO_" + upper(en->name) + "(", 0) == 0) enumType = upper(en->name);
    }
    if (enumType.empty() && v.type() != sim::Type::Bool && (upper(bare).rfind("SEL(", 0) == 0 || upper(bare).rfind("MUX(", 0) == 0))
        for (const auto* en : enumerations(p))
            if (upper(bare).find(upper(en->name) + "#") != std::string::npos) enumType = upper(en->name);
    bool isEnum = false;
    if (const auto* en = enumType.empty() ? nullptr : findEnumeration(p, enumType))
        for (const auto& ev : en->values)
            if (ev.value == v.asInteger()) {
                o.type = en->name;
                o.value = en->name + "#" + ev.name;
                isEnum = true;
            }
    if (v.type() == sim::Type::String) {
        const std::string t = v.asString();
        if ((t.size() == 7 || t.size() == 9) && t[0] == '#'
            && std::all_of(t.begin() + 1, t.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
            const auto rgb = static_cast<std::uint32_t>(std::strtoul(t.c_str() + 1, nullptr, 16));
            o.swatch = t.size() == 7 ? (rgb << 8) | 0xFFu : rgb;
        }
    }
    // ---- la case : ce resultat lui convient-il ? ----
    // Tranche 15 (decision du chef, 03/10) : chaque genre de valeur contre chaque genre de
    // case, le tableau des croisements (essai croisements111) :
    //               BOOL  entier  REAL  TIME  texte  T_MODE
    //   BOOL         oui    -      -     -      -      -
    //   Entier        -    oui     -     -      -      -
    //   Reel          -    oui    oui    -      -      -     (un entier s'elargit : rien ne se perd)
    //   Texte        oui   oui    oui   oui    oui     -     (la case montre la valeur ; un T_MODE, son numero)
    //   Couleur       -     -      -     -   '#RRGGBB'  -
    //   Duree         -     -      -    oui     -      -
    //   Vue           -     -      -     -   une vue   -
    //   Enumeration   -     -      -     -      -     oui    (« ne convient pas : BOOL pour une enumeration (T_MODE) »)
    //   Membre       oui   oui    oui   oui    oui    oui
    //   Liste         -     -      -     -   un choix  -
    // Ce qui ne convient pas garde sa valeur et son type (la page les montre au-dessus de la
    // raison) ; la raison commence par « ne convient pas : <type> pour <la case> » et propose
    // la conversion quand elle est sure.
    enum class G { Bool, Integer, Real, Time, Text, Enum, Other };
    const G g = isEnum                              ? G::Enum
              : v.type() == sim::Type::Bool         ? G::Bool
              : v.type() == sim::Type::Time         ? G::Time
              : v.type() == sim::Type::Real         ? G::Real
              : sim::isInteger(v.type())            ? G::Integer
              : v.type() == sim::Type::String       ? G::Text
                                                    : G::Other;
    const auto misfit = [&](const std::string& forWhat, const std::string& advice, std::string fixedBody = {}) {
        o.fits.clear();
        return whole("ne convient pas : " + o.type + " pour " + forWhat + (advice.empty() ? std::string(".") : ". " + advice),
                     std::move(fixedBody));
    };
    // Un nom seul (Mode, V[0].Pos) ou un litteral (T_MODE#Manu) : TO_STRING en donne le texte ;
    // pas un appel (le dialecte ne connait pas le type de son resultat).
    // 1.11 (tranche 25, avec REP) : avec ses reperes aussi ($Mode$ -> TO_STRING($Mode$), qui les garde pour
    // Dupliquer). Sans apostrophe, un $ qui a passe le moteur est celui d'un repere ; sans REP, aucun ne passe.
    const bool plain = std::all_of(s.begin(), s.end(), [](char c) { return pathChar(c) || c == '#' || c == '$'; });
    switch (as.kind) {
        case Kind::Bool:
            if (g != G::Bool) {
                std::string fix;
                if (g == G::Integer || g == G::Real) fix = s + " > 0";
                else if (g == G::Time) fix = s + " > T#0s";
                else if (g == G::Enum) fix = s + " = " + o.value;
                return misfit("une condition (BOOL)",
                              "Compare-le : une case BOOL attend une condition" + (fix.empty() ? std::string(".") : ", par exemple " + fix + "."), fix);
            }
            break;
        case Kind::Integer:
            if (g == G::Integer) break;
            if (g == G::Real) return misfit("un entier (INT)", "REAL_TO_INT l'arrondit \xC3\xA0 l'entier.", "REAL_TO_INT(" + s + ")");
            if (g == G::Time) return misfit("un entier (INT)", "TIME_TO_DINT en donne les millisecondes.", "TIME_TO_DINT(" + s + ")");
            if (g == G::Enum) return misfit("un entier (INT)", "TO_INT en donne le num\xC3\xA9ro.", "TO_INT(" + s + ")");
            if (g == G::Bool) return misfit("un entier (INT)", "SEL(" + s + ", 0, 1) donne 0 ou 1.", "SEL(" + s + ", 0, 1)");
            return misfit("un entier (INT)", "Une case Entier attend un nombre.");
        case Kind::Real:
            if (g == G::Real || g == G::Integer) break;
            if (g == G::Time) return misfit("un r\xC3\xA9" "el (REAL)", "TIME_TO_DINT en donne les millisecondes.", "TIME_TO_DINT(" + s + ")");
            if (g == G::Enum) return misfit("un r\xC3\xA9" "el (REAL)", "TO_INT en donne le num\xC3\xA9ro.", "TO_INT(" + s + ")");
            if (g == G::Bool) return misfit("un r\xC3\xA9" "el (REAL)", "SEL(" + s + ", 0.0, 1.0) donne 0.0 ou 1.0.", "SEL(" + s + ", 0.0, 1.0)");
            return misfit("un r\xC3\xA9" "el (REAL)", "Une case R\xC3\xA9" "el attend un nombre.");
        case Kind::Text:
            if (g == G::Enum)
                return misfit("un texte (STRING)", "La case montrerait son num\xC3\xA9ro (" + std::to_string(v.asInteger()) + ") : TO_STRING en donne le texte.",
                              plain ? "TO_STRING(" + s + ")" : std::string{});
            if (g == G::Other) return misfit("un texte (STRING)", "Une case Texte attend un texte, entre apostrophes.");
            break;   // un nombre, un BOOL, une duree : la case montre leur valeur
        case Kind::Color:
            if (g == G::Text && o.swatch != 0) break;
            return misfit("une couleur ('#RRGGBB')", g == G::Text ? "Ce texte n'est pas une couleur : \xC3\xA9" "cris '#RRGGBB' (ou SEL / MUX qui en choisit une)."
                                                                 : "Une case couleur attend '#RRGGBB' (ou SEL / MUX qui en choisit une).");
        case Kind::Time:
            if (g == G::Time) break;
            if (g == G::Integer) return misfit("une dur\xC3\xA9" "e (TIME)", "DINT_TO_TIME lit des millisecondes.", "DINT_TO_TIME(" + s + ")");
            return misfit("une dur\xC3\xA9" "e (TIME)", "Une case Dur\xC3\xA9" "e attend T#\xE2\x80\xA6 (par exemple T#5s).");
        case Kind::View: {
            if (g == G::Text) {
                const std::string name = v.asString();
                if (std::any_of(p.views.begin(), p.views.end(), [&](const auto& vw) { return upper(vw.name) == upper(name); })) break;
                return misfit("une vue (son nom)", "\xC2\xAB " + name + " \xC2\xBB n'est le nom d'aucune vue du projet.");
            }
            return misfit("une vue (son nom)", "Une case Vue attend le nom d'une vue, un texte entre apostrophes.");
        }
        case Kind::Enum: {
            const auto* want = findEnumeration(p, std::string(as.wanted));   // T_MODE (le projet d'exemple)
            if (g == G::Enum && (!want || upper(o.type) == upper(want->name))) break;
            const std::string target = want ? want->name : std::string(as.wanted);
            const std::string first = want && !want->values.empty() ? want->name + "#" + want->values.front().name : target + "#\xE2\x80\xA6";
            std::string fix;
            if (g == G::Integer && want)
                for (const auto& ev : want->values)
                    if (ev.value == v.asInteger()) fix = "TO_" + want->name + "(" + s + ")";
            if (g == G::Bool)
                return misfit("une \xC3\xA9num\xC3\xA9ration (" + target + ")",
                              "Une condition (vrai ou faux) va dans une case BOOL ; ici, \xC3\xA9" "cris une valeur : " + first + ", ou SEL(condition, "
                                  + first + ", \xE2\x80\xA6).");
            return misfit("une \xC3\xA9num\xC3\xA9ration (" + target + ")",
                          fix.empty() ? "Une case \xC3\x89num\xC3\xA9ration attend une valeur de ce type : " + first + "."
                                      : "TO_" + target + " la convertit (contr\xC3\xB4l\xC3\xA9).",
                          fix);
        }
        case Kind::Choice: {
            const auto& list = choiceList();
            if (g == G::Text && std::any_of(list.begin(), list.end(), [&](const std::string& c) { return upper(c) == upper(v.asString()); })) break;
            return misfit("une liste de choix (gauche, centre ou droite)",
                          g == G::Text ? "\xC2\xAB " + v.asString() + " \xC2\xBB n'est pas dans la liste."
                                       : "Une case Liste attend une de ses valeurs, entre apostrophes : 'centre'.");
        }
        case Kind::Member:
        case Kind::Template:
            break;   // Membre et element : le type du membre, quel qu'il soit
    }
    if (!wantProblem.empty()) return whole(wantProblem);
    o.fits = "convient \xC3\xA0 une case \xC2\xAB " + std::string(as.title) + " \xC2\xBB";
    return o;
}

} // namespace hmi::exprguide
