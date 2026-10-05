#include "HmiActionKinds.hpp"

#include "HmiExpr.hpp"
#include "HmiRuntime.hpp"     // parseArguments, isVariablePath

#include <algorithm>
#include <cctype>

namespace hmi::actionkinds {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) != 0 || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) != 0 || c == '_'; }

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}
bool same(std::string_view a, std::string_view b) { return upper(a) == upper(b); }

bool keyword(std::string_view name) {
    static const char* const k[] = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE"};
    const std::string u = upper(name);
    return std::any_of(std::begin(k), std::end(k), [&](const char* w) { return u == w; });
}

// Un texte entre apostrophes ('Consigne du four', '' pour une apostrophe) <-> le texte.
std::string quote(std::string_view text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}
std::string unquote(std::string_view text) {
    const std::string t = trimmed(text);
    if (t.size() < 2 || t.front() != '\'' || t.back() != '\'') return t;
    std::string out;
    for (std::size_t i = 1; i + 1 < t.size(); ++i) {
        if (t[i] == '\'' && i + 2 < t.size() && t[i + 1] == '\'') ++i;
        out += t[i];
    }
    return out;
}

// Les noms a la racine d'une expression (pas apres un point, pas un appel de
// fonction, hors des chaines) : `f(debut, fin)` pour chacun.
template <class F>
void forEachRoot(std::string_view s, F&& f) {
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            i = end == std::string_view::npos ? s.size() : end + 1;
            continue;
        }
        if (identStart(c) && (i == 0 || (s[i - 1] != '.' && s[i - 1] != '#' && !identChar(s[i - 1])))) {
            std::size_t j = i;
            while (j < s.size() && identChar(s[j])) ++j;
            std::size_t k = j;
            while (k < s.size() && std::isspace(uc(s[k]))) ++k;
            if (k >= s.size() || s[k] != '(') f(i, j);
            i = j;
            continue;
        }
        if (std::isdigit(uc(c))) {
            // 16#FF, 2.5E3, T#5s : un litteral, pas un nom.
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        ++i;
    }
}

// La formule ou chaque nom de `names` est remplace par `values` (dans l'ordre).
std::string substitute(std::string_view formula, const std::vector<std::string>& names, const std::vector<std::string>& values) {
    std::string out;
    std::size_t last = 0;
    forEachRoot(formula, [&](std::size_t a, std::size_t b) {
        const auto word = formula.substr(a, b - a);
        for (std::size_t k = 0; k < names.size() && k < values.size(); ++k)
            if (same(word, names[k])) {
                out.append(formula.substr(last, a - last));
                out.append(values[k]);
                last = b;
                return;
            }
    });
    out.append(formula.substr(last));
    return out;
}

// Les noms qu'une formule lit a sa racine (sans doublon, sans les mots-cles).
std::vector<std::string> rootsOf(std::string_view formula) {
    std::vector<std::string> out;
    forEachRoot(formula, [&](std::size_t a, std::size_t b) {
        const std::string w(formula.substr(a, b - a));
        if (keyword(w)) return;
        if (std::none_of(out.begin(), out.end(), [&](const std::string& o) { return same(o, w); })) out.push_back(w);
    });
    return out;
}

// Rien a lire : le mode test ne calcule qu'avec les valeurs qu'on lui donne.
class TestEnv final : public sim::Environment {
public:
    std::string unknown;
    bool read(std::string_view n, sim::Value&) override {
        if (unknown.empty()) unknown = std::string(n);
        return false;
    }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view) override { return false; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

} // namespace

// ================================================================ l'arbre ====
const std::vector<Group>& groups() {
    static const std::vector<Group> g = {
        {"Variables", "\xC3\x89" "crire, calculer ou faire saisir une variable",
         {Operation::Set, Operation::Reset, Operation::Toggle, Operation::Assign, Operation::Increment, Operation::Decrement,
          Operation::Maths, Operation::Keyboard}},
        {"Navigation", "Changer de vue", {Operation::Navigate, Operation::NavigateBack, Operation::NavigateForward, Operation::NavigateHome}},
        {"Popups", "Ouvrir, changer, fermer une popup",
         {Operation::Popup, Operation::ChangePopup, Operation::ClosePopup, Operation::CloseAllPopups, Operation::PreviousPopup,
          Operation::CenterPopup, Operation::ApplyCopy}},
        {"Scripts", "Ex\xC3\xA9" "cuter du code ST", {Operation::RunScript, Operation::CallScript}},
        {"Alarmes", "Acquitter, mettre de c\xC3\xB4t\xC3\xA9, faire taire",
         {Operation::AckAlarm, Operation::ShelveAlarm, Operation::UnshelveAlarm, Operation::SilenceAlarms}},
        {"Donn\xC3\xA9" "es", "Journal, recettes, exports, fichiers",
         {Operation::Log, Operation::LoadRecipe, Operation::Export, Operation::BindTable, Operation::RequestResource}},
        {"M\xC3\xA9" "dias", "Sons et GIF anim\xC3\xA9s",
         {Operation::PlaySound, Operation::GifPlay, Operation::GifPause, Operation::GifStop, Operation::GifReplay}},
        {"Utilisateur et poste", "Connexion, menus natifs, langue, th\xC3\xA8me",
         {Operation::ChangeUser, Operation::Logout, Operation::ShowLogin, Operation::ShowSystem, Operation::SetLanguage,
          Operation::SetTheme}},
    };
    return g;
}

std::string_view groupOf(Operation o) noexcept {
    for (const auto& g : groups())
        if (std::find(g.operations.begin(), g.operations.end(), o) != g.operations.end()) return g.name;
    return {};
}

std::string_view help(Operation o) noexcept {
    switch (o) {
        case Operation::Toggle: return "Passe une variable BOOL \xC3\xA0 l'inverse (TRUE \xE2\x86\x94 FALSE).";
        case Operation::Set: return "Met une variable BOOL \xC3\xA0 TRUE.";
        case Operation::Reset: return "Met une variable BOOL \xC3\xA0 FALSE.";
        case Operation::Increment: return "Ajoute un pas \xC3\xA0 une variable (1, 0,5, ou une expression).";
        case Operation::Decrement: return "Retire un pas \xC3\xA0 une variable.";
        case Operation::Assign: return "\xC3\x89" "crit le r\xC3\xA9sultat d'une expression dans une variable.";
        case Operation::Maths:
            return "Calcule une formule sur des r\xC3\xA9" "f\xC3\xA9rences nomm\xC3\xA9" "es (Mesure, Consigne...) et \xC3\xA9" "crit le r\xC3\xA9sultat ; une "
                   "petite fen\xC3\xAAtre aide \xC3\xA0 l'\xC3\xA9" "crire et la teste.";
        case Operation::Keyboard:
            return "Ouvre un champ de saisie avec le clavier virtuel ; la valeur tap\xC3\xA9" "e va dans la variable (titre, "
                   "clavier, minimum, maximum, unit\xC3\xA9, masqu\xC3\xA9).";
        case Operation::Navigate: return "Ouvre une autre vue (avec ses param\xC3\xA8tres et une transition).";
        case Operation::NavigateBack: return "Revient \xC3\xA0 la vue d'avant (l'historique).";
        case Operation::NavigateForward: return "Repart vers la vue quitt\xC3\xA9" "e par Vue pr\xC3\xA9" "c\xC3\xA9" "dente.";
        case Operation::NavigateHome: return "Ouvre la vue d'accueil de l'utilisateur connect\xC3\xA9.";
        case Operation::Popup: return "Ouvre une popup par-dessus la vue (param\xC3\xA8tres, position, transition).";
        case Operation::ChangePopup: return "Remplace la popup du dessus par une autre (Popup pr\xC3\xA9" "c\xC3\xA9" "dente y revient).";
        case Operation::ClosePopup: return "Ferme la popup qui porte l'objet.";
        case Operation::CloseAllPopups: return "Ferme toutes les popups ouvertes.";
        case Operation::PreviousPopup: return "Revient \xC3\xA0 la popup remplac\xC3\xA9" "e par Changer de popup.";
        case Operation::CenterPopup: return "Ram\xC3\xA8ne une popup au milieu de la vue.";
        case Operation::ApplyCopy: return "\xC3\x89" "crit la copie d'un param\xC3\xA8tre (mode Les deux) dans la variable de l'appelant.";
        case Operation::RunScript: return "Ex\xC3\xA9" "cute du ST \xC3\xA9" "crit ici (une petite fen\xC3\xAAtre l'\xC3\xA9" "dite comme les autres scripts).";
        case Operation::CallScript: return "Appelle un script g\xC3\xA9n\xC3\xA9ral du projet.";
        case Operation::AckAlarm: return "Acquitte toutes les alarmes, un groupe ou une alarme.";
        case Operation::ShelveAlarm: return "Met une alarme de c\xC3\xB4t\xC3\xA9 pour un temps, avec une raison.";
        case Operation::UnshelveAlarm: return "Remet en service une alarme mise de c\xC3\xB4t\xC3\xA9.";
        case Operation::SilenceAlarms: return "Fait taire le son des alarmes jusqu'\xC3\xA0 la prochaine.";
        case Operation::Log: return "\xC3\x89" "crit un message (texte \xC3\xA0 trous) dans le journal.";
        case Operation::LoadRecipe: return "\xC3\x89" "crit un jeu de valeurs d'une recette.";
        case Operation::Export: return "Exporte des donn\xC3\xA9" "es en CSV, Excel ou PDF.";
        case Operation::BindTable: return "Montre un fichier externe (SQLite, Excel, CSV...) dans un tableau de la vue.";
        case Operation::RequestResource: return "Fait choisir un fichier qui devient une ressource du projet.";
        case Operation::PlaySound: return "Joue un son des ressources.";
        case Operation::GifPlay: return "Fait jouer un GIF anim\xC3\xA9 de la vue.";
        case Operation::GifPause: return "Met un GIF anim\xC3\xA9 en pause.";
        case Operation::GifStop: return "Arr\xC3\xAAte un GIF anim\xC3\xA9 sur sa premi\xC3\xA8re image.";
        case Operation::GifReplay: return "Rejoue un GIF anim\xC3\xA9 N fois.";
        case Operation::ChangeUser: return "Connecte un utilisateur (son mot de passe ou son code est demand\xC3\xA9).";
        case Operation::Logout: return "D\xC3\xA9" "connecte l'utilisateur.";
        case Operation::ShowLogin: return "Ouvre le menu natif de connexion.";
        case Operation::ShowSystem: return "Ouvre le menu natif Param\xC3\xA8tres syst\xC3\xA8me.";
        case Operation::SetLanguage: return "Change la langue des textes.";
        case Operation::SetTheme: return "Passe au th\xC3\xA8me jour ou nuit.";
    }
    return {};
}

// ============================================================ les reglages ====
Params params(const Action& a) { return parseArguments(a.params); }

std::string formatParams(const Params& p) {
    std::string out;
    for (const auto& [name, value] : p) {
        if (trimmed(name).empty()) continue;
        out += (out.empty() ? "" : "; ") + trimmed(name) + " := " + trimmed(value);
    }
    return out;
}

std::string param(const Action& a, std::string_view name) {
    for (const auto& [n, v] : params(a))
        if (same(n, name)) return v;
    return {};
}

void setParam(Action& a, std::string_view name, std::string value) {
    auto p = params(a);
    const auto it = std::find_if(p.begin(), p.end(), [&](const auto& kv) { return same(kv.first, name); });
    if (trimmed(value).empty()) {
        if (it != p.end()) p.erase(it);
    } else if (it != p.end()) {
        it->second = std::move(value);
    } else {
        p.emplace_back(std::string(name), std::move(value));
    }
    a.params = formatParams(p);
}

// ================================================================== Maths ====
std::string mathsExpression(std::string_view formula, const Params& refs) {
    std::vector<std::string> names, paths;
    for (const auto& [n, v] : refs) {
        names.push_back(trimmed(n));
        paths.push_back(trimmed(v));
    }
    return substitute(formula, names, paths);
}

std::vector<MathsIssue> checkMaths(const Params& refs, std::string_view formula) {
    std::vector<MathsIssue> out;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const std::string name = trimmed(refs[i].first), path = trimmed(refs[i].second);
        const int at = static_cast<int>(i);
        if (name.empty() || !identStart(name[0]) || !std::all_of(name.begin(), name.end(), identChar)) {
            out.push_back({"le nom \xC2\xAB " + name + " \xC2\xBB n'est pas un nom (lettres, chiffres, _)", at});
            continue;
        }
        if (keyword(name)) {
            out.push_back({name + " est un mot r\xC3\xA9serv\xC3\xA9 : choisir un autre nom", at});
            continue;
        }
        for (std::size_t k = 0; k < i; ++k)
            if (same(trimmed(refs[k].first), name)) {
                out.push_back({name + " est nomm\xC3\xA9 deux fois", at});
                break;
            }
        if (path.empty()) out.push_back({name + " : la r\xC3\xA9" "f\xC3\xA9rence est vide (une variable : Armoires[0].ana.PT1.mes)", at});
        else if (!isVariablePath(path))
            out.push_back({name + " := " + path + " : une r\xC3\xA9" "f\xC3\xA9rence est un chemin de variable, pas un calcul "
                           "(le calcul va dans la formule)",
                           at});
    }
    const std::string f = trimmed(formula);
    if (f.empty()) {
        out.push_back({"la formule est vide", -1});
        return out;
    }
    const auto e = Expression::compile(mathsExpression(f, refs));
    if (!e.valid()) out.push_back({"la formule ne se lit pas : " + e.error(), -1});
    // Une reference que la formule ne cite pas : rien ne la lit (sans doute un oubli).
    const auto roots = rootsOf(f);
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const std::string name = trimmed(refs[i].first);
        if (!name.empty() && std::none_of(roots.begin(), roots.end(), [&](const std::string& r) { return same(r, name); }))
            out.push_back({name + " n'est pas dans la formule", static_cast<int>(i), true});
    }
    return out;
}

MathsTest testMaths(const Params& refs, const std::vector<std::string>& values, std::string_view formula) {
    MathsTest t;
    std::vector<std::string> names, literal;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        names.push_back(trimmed(refs[i].first));
        std::string v = i < values.size() ? trimmed(values[i]) : std::string{};
        if (v.empty()) v = "0";
        double d = 0;
        std::string dotted = v;
        std::replace(dotted.begin(), dotted.end(), ',', '.');
        if (parseNumber(dotted, d)) v = dotted;
        else if (const std::string u = upper(v); u == "VRAI") v = "TRUE";
        else if (u == "FAUX") v = "FALSE";
        literal.push_back("(" + v + ")");
    }
    t.expression = substitute(trimmed(formula), names, literal);
    if (trimmed(formula).empty()) {
        t.why = "la formule est vide";
        return t;
    }
    const auto e = Expression::compile(t.expression);
    if (!e.valid()) {
        t.why = "la formule ne se lit pas : " + e.error();
        return t;
    }
    TestEnv env;
    const auto v = e.evaluate(env);
    if (!v) {
        t.why = env.unknown.empty() ? v.error().message()
                                    : env.unknown + " n'est pas une r\xC3\xA9" "f\xC3\xA9rence : l'ajouter aux param\xC3\xA8tres pour la tester";
        return t;
    }
    t.ok = true;
    t.result = formatValue(*v);
    return t;
}

// ======================================================= le clavier virtuel ====
KeyboardSpec keyboardSpec(const Action& a) {
    KeyboardSpec k;
    k.title = unquote(param(a, "titre"));
    k.keyboard = trimmed(param(a, "clavier"));
    if (k.keyboard.empty()) k.keyboard = "auto";
    k.min = param(a, "min");
    k.max = param(a, "max");
    k.unit = unquote(param(a, "unite"));
    k.mask = parseBool(param(a, "masque"), false);
    return k;
}

void setKeyboardSpec(Action& a, const KeyboardSpec& k) {
    Params p;
    if (!trimmed(k.title).empty()) p.emplace_back("titre", quote(trimmed(k.title)));
    if (!k.keyboard.empty() && k.keyboard != "auto") p.emplace_back("clavier", k.keyboard);
    if (!trimmed(k.min).empty()) p.emplace_back("min", trimmed(k.min));
    if (!trimmed(k.max).empty()) p.emplace_back("max", trimmed(k.max));
    if (!trimmed(k.unit).empty()) p.emplace_back("unite", quote(trimmed(k.unit)));
    if (k.mask) p.emplace_back("masque", "TRUE");
    a.params = formatParams(p);
}

std::string_view keyboardFor(std::string_view type, std::string_view chosen) {
    if (chosen == "numerique" || chosen == "complet") return chosen;
    const std::string t = upper(trimmed(type));
    static const char* const numbers[] = {"INT", "DINT", "SINT", "LINT", "UINT", "UDINT", "USINT", "ULINT",
                                          "WORD", "DWORD", "BYTE", "LWORD", "REAL", "LREAL"};
    for (const char* n : numbers)
        if (t == n) return "numerique";
    return "complet";
}

} // namespace hmi::actionkinds
