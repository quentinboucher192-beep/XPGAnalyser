// hmi/HmiTypes.cpp - les types IHM, les tableaux, leur depliage en cases et leur
// place Modbus (lot 16).
#include "HmiTypes.hpp"
#include "HmiEnums.hpp"     // 1.10 (decision 15) : une enumeration est une case DINT

#include "HmiEquipment.hpp"
#include "HmiMarkers.hpp"   // 1.11.1 (REP) : pathProblems lit un script sans les $ de ses reperes
#include "HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : les types elementaires d'une variable
#include "HmiExpr.hpp"           // 1.12.10 (lot F) : la valeur d'un membre, lue comme le moteur la lira
#include "HmiTypeForms.hpp"      // 1.12.14 : les pas dans les types imbriques (la forme d'un type)
#include "HmiMethods.hpp"        // 1.13.0 (lot J1) : les methodes en vigueur (effectiveMethod), la chaine de base

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace hmi::types {

namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

bool identifier(std::string_view s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// 1.12.20 (lot G) : un nom de type - un identifiant, ou l'instance d'une structure generique (T_Mesure<REAL>).
bool typeWord(std::string_view s) {
    if (identifier(s)) return true;
    if (isNamespaceName(s)) return true;            // 1.13.0 (lot J8) : Gaz.T_Vanne, le type d'un espace de noms
    std::string base;
    std::vector<std::string> args;
    return splitGenericName(s, base, args);
}

// 1.12.4 : les types IHM que nomme un texte de type (T_Four dans LIST OF T_Four, TUPLE(INT, T_Four),
// MAP[STRING] OF T_Four, ARRAY[0..2] OF T_Four), chacun une fois, dans l'ordre.
std::vector<const HmiType*> namedTypesOf(const Project& p, std::string_view type) {
    std::vector<const HmiType*> out;
    for (std::size_t i = 0; i < type.size();) {
        const auto c = static_cast<unsigned char>(type[i]);
        if (!(std::isalpha(c) || type[i] == '_')) {
            ++i;
            continue;
        }
        std::size_t j = i;
        // 1.13.0 (lot J8) : un nom d'un espace (Gaz.T_Vanne) se lit d'un bloc.
        while (j < type.size() && (std::isalnum(static_cast<unsigned char>(type[j])) || type[j] == '_'
                                   || (type[j] == '.' && j + 1 < type.size() && (std::isalpha(static_cast<unsigned char>(type[j + 1])) || type[j + 1] == '_'))))
            ++j;
        if (const HmiType* t = p.hmiTypeByName(type.substr(i, j - i)); t && std::find(out.begin(), out.end(), t) == out.end())
            out.push_back(t);
        i = j;
    }
    return out;
}

// 1.12.4 : le type `from` contient-il `owner` (un de ses membres, ou plus bas, objets compris) ? 1.13.0 : par IDENTITE
// quand `owner` nomme un type du projet (Gaz.T_Vanne n'est pas Util.T_Vanne) ; sinon par son nom.
bool reaches(const Project& p, const HmiType& from, std::string_view owner) {
    const HmiType* target = p.hmiTypeByName(trimmed(owner));
    std::set<const HmiType*> seen;
    std::function<bool(const HmiType&)> go = [&](const HmiType& t) {
        if (!seen.insert(&t).second) return false;
        for (const auto& m : t.members)
            for (const HmiType* sub : namedTypesOf(p, m.type))
                if ((target ? sub == target : sameText(sub->name, owner)) || go(*sub)) return true;
        return false;
    };
    return go(from);
}

// Un entier signe : "0", "-5", "12".
bool integerOf(std::string_view s, long long& out) {
    const std::string t = trimmed(s);
    if (t.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (t[0] == '-' || t[0] == '+') {
        neg = t[0] == '-';
        i = 1;
    }
    if (i >= t.size() || t.size() - i > 9) return false;
    long long v = 0;
    for (; i < t.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        v = v * 10 + (t[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

// "0..9" -> 0, 9.
bool rangeOf(std::string_view s, long long& lo, long long& hi, std::string* why) {
    const auto dots = s.find("..");
    if (dots == std::string_view::npos) return fail(why, "bornes \xC2\xAB " + trimmed(s) + " \xC2\xBB : \xC3\xA9" "crire bas..haut (0..9)");
    if (!integerOf(s.substr(0, dots), lo) || !integerOf(s.substr(dots + 2), hi))
        return fail(why, "bornes \xC2\xAB " + trimmed(s) + " \xC2\xBB : deux entiers (0..9, 1..4, -5..5)");
    if (hi < lo) return fail(why, "bornes " + std::to_string(lo) + ".." + std::to_string(hi) + " : la haute avant la basse");
    return true;
}

// Le chemin d'une case : "Consignes[3]", "Matrice[2,7]".
std::string indexText(long long i) { return "[" + std::to_string(i) + "]"; }
std::string indexText(long long i, long long j) { return "[" + std::to_string(i) + "," + std::to_string(j) + "]"; }

// Une liste "1.5, 2, 'a,b', 3" -> ses valeurs (les virgules entre guillemets ou
// entre parentheses ne coupent pas). Une seule valeur : une liste d'un.
std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quoted = false;
    for (char c : s) {
        if (c == '\'' ) quoted = !quoted;
        if (!quoted && (c == '(' || c == '[')) ++depth;
        if (!quoted && (c == ')' || c == ']')) --depth;
        if (c == ',' && !quoted && depth == 0) {
            out.push_back(trimmed(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(trimmed(cur));
    return out;
}

// 1.12.2 : une liste ecrite entre crochets, [1, 2, 3] - et [[1, 2], [3, 4]] pour deux
// dimensions, ligne par ligne - comme les scripts l'ecrivent. `bracketed` : elle l'etait.
std::vector<std::string> arrayInitials(std::string_view initial, bool& bracketed) {
    const std::string t = trimmed(initial);
    bracketed = false;
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
        int depth = 0;
        bool quoted = false, whole = true;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const char c = t[i];
            if (c == '\'') quoted = !quoted;
            if (quoted) continue;
            if (c == '[') ++depth;
            else if (c == ']' && --depth == 0 && i + 1 < t.size()) whole = false;   // [1] + [2] : pas une seule liste
        }
        if (whole) {
            bracketed = true;
            std::vector<std::string> out;
            for (auto& item : splitList(std::string_view(t).substr(1, t.size() - 2))) {
                if (item.size() >= 2 && item.front() == '[' && item.back() == ']')
                    for (auto& x : splitList(std::string_view(item).substr(1, item.size() - 2))) out.push_back(std::move(x));
                else if (!item.empty())
                    out.push_back(std::move(item));
            }
            return out;
        }
    }
    return splitList(t);
}

// 1.12.10 (lot F) : la valeur `lit` peut-elle aller dans un membre de type `type` ? Faux (et `why`) : elle ne
// s'appliquera pas (le membre garde la valeur de son type). Une structure, un tableau, une liste : leur
// forme seulement (leurs elements se verifient en les depliant).
bool literalFits(const Project& p, std::string_view type, std::string_view lit, std::string* why) {
    const std::string t = trimmed(lit);
    if (t.empty() || isRich(type)) return true;
    Spec s;
    if (!parseSpec(type, s)) return true;                       // un type illisible : dit ailleurs
    if (s.array()) return true;                                 // [1, 2, 3], une valeur pour toutes
    if (!isElementary(s.element)) {
        const HmiType* ty = p.hmiTypeByName(s.element);
        if (!ty || ty->kind == HmiTypeKind::Callback) return true;      // 1.12.20 : un rappel se verifie a part
        if (ty->kind == HmiTypeKind::Enumeration) {
            std::int64_t n = 0;
            if (enumNumberOf(*ty, t, n)) return true;
            return fail(why, "pas une valeur de " + ty->name);
        }
        if (t.front() == '(' && t.back() == ')') return true;
        return fail(why, "une structure s'\xC3\xA9" "crit (Membre := valeur, ...)");
    }
    const auto e = Expression::compile(t);
    if (!e.valid()) return fail(why, "illisible (" + e.error() + ")");
    const std::string up = upperOf(s.element);
    const bool text = up == "STRING" || up == "WSTRING" || up == "CHAR" || up == "WCHAR";
    if (!text && (t.front() == '\'' || t.front() == '"')) return fail(why, "un texte pour un " + up);
    return true;
}

// La tete d'ecriture de la place Modbus.
struct Cursor {
    long long word{0};             // le prochain mot libre
    long long packWord{-1};        // le mot ou se rangent les BOOL en cours (-1 : aucun)
    int       packBit{0};
    void close() noexcept { packWord = -1; }
};

struct Walker {
    const Project& p;
    bool           pack;
    Flat&          out;
    std::vector<std::string> stack;   // les types IHM en cours (majuscules) : un cycle
    Cursor         cur;

    bool full() const noexcept { return static_cast<long long>(out.leaves.size()) > kMaxElements; }
    // 1.12.10 (lot F) : ce qu'une valeur initiale ne peut pas donner, dit une fois.
    void note(std::string text) {
        if (std::find(out.notes.begin(), out.notes.end(), text) == out.notes.end()) out.notes.push_back(std::move(text));
    }

    void leaf(const std::string& path, const std::string& rel, const std::string& type, const std::string& initial,
              const std::string& description) {
        Leaf l;
        l.path = path;
        l.rel = rel;
        l.type = type;
        l.initial = initial;
        l.description = description;
        if (type == "BOOL" && pack) {
            if (cur.packWord < 0 || cur.packBit >= 16) {
                cur.packWord = cur.word++;
                cur.packBit = 0;
                l.words = 1;
            }
            l.word = cur.packWord;
            l.bit = cur.packBit++;
        } else {
            cur.close();
            l.word = cur.word;
            l.words = wordsOf(type);
            cur.word += l.words;
        }
        out.leaves.push_back(std::move(l));
    }

    void walk(const std::string& path, const std::string& rel, std::string_view type, std::string_view initial,
              const std::string& description) {
        if (!out.error.empty()) return;
        // 1.12.4 : un membre objet (une liste, un vecteur, une MAP, un tuple, ou un tableau de l'un
        // d'eux) : ni case ni mot ; un objet entier dans la memoire de l'IHM.
        if (!rel.empty() && isRich(type)) {
            out.objects.push_back({path, rel, normalized(type), trimmed(initial), description});
            return;
        }
        Spec s;
        std::string why;
        if (!parseSpec(type, s, &why)) {
            out.error = path + " : " + why;
            return;
        }
        if (s.array()) {
            if (s.length() > kMaxElements || static_cast<long long>(out.leaves.size()) + s.length() > kMaxElements) {
                out.error = path + " : plus de " + std::to_string(kMaxElements) + " cases";
                return;
            }
            cur.close();
            const long long start = cur.word;
            Aggregate a;
            a.path = path;
            a.array = true;
            a.spec = s;
            a.typeName = specText(s);
            // Les valeurs initiales : une pour toutes, ou une liste (tableau de cases simples ;
            // 1.10 : une enumeration est une case simple, un DINT).
            const HmiType* elementType = isElementary(s.element) ? nullptr : p.hmiTypeByName(s.element);
            const bool enumElement = elementType && elementType->kind == HmiTypeKind::Enumeration;
            const bool simple = isElementary(s.element) || enumElement;
            // 1.12.10 (lot F) : un tableau de structures prend aussi un litteral par element,
            // [(Nom := 'Cuve A', Niveau := 4.2), (...)] - avant, chacun les valeurs de son type, en silence.
            bool bracketed = false;
            const auto list = arrayInitials(initial, bracketed);
            const bool perElement = list.size() > 1 || bracketed;   // 1.12.2 : [5] : la premiere case seule
            if (perElement && static_cast<long long>(list.size()) > s.length() && (!simple || !rel.empty()))
                note(path + " : " + std::to_string(list.size()) + " valeurs initiales pour " + std::to_string(s.length())
                     + " \xC3\xA9l\xC3\xA9ments (les derni\xC3\xA8res sont ignor\xC3\xA9" "es)");
            long long k = 0;
            const auto elementInit = [&]() -> std::string {
                if (!perElement) return trimmed(initial);
                return k < static_cast<long long>(list.size()) ? list[static_cast<std::size_t>(k)] : std::string{};
            };
            if (s.dims == 1) {
                for (long long i = s.low[0]; i <= s.high[0] && out.error.empty(); ++i, ++k)
                    walk(path + indexText(i), rel + indexText(i), s.element, elementInit(), description);
            } else {
                for (long long i = s.low[0]; i <= s.high[0] && out.error.empty(); ++i)
                    for (long long j = s.low[1]; j <= s.high[1] && out.error.empty(); ++j, ++k)
                        walk(path + indexText(i, j), rel + indexText(i, j), s.element, elementInit(), description);
            }
            cur.close();
            a.weight.words = cur.word - start;
            a.weight.bytes = a.weight.words * 2;
            if (simple) a.elementBytes = s.element == "BOOL" ? 1 : wordsOf(enumElement ? std::string_view("DINT") : std::string_view(s.element)) * 2;
            else a.elementBytes = s.length() > 0 ? a.weight.bytes / s.length() : 0;
            out.aggregates.push_back(std::move(a));
            return;
        }
        if (isElementary(s.element)) {
            leaf(path, rel, s.element, std::string(trimmed(initial)), description);
            return;
        }
        const HmiType* ty = p.hmiTypeByName(s.element);
        if (!ty) {
            out.error = path + " : type inconnu \xC2\xAB " + s.element + " \xC2\xBB";
            return;
        }
        // 1.13.0 (lot J1) : une interface est un contrat (des signatures), pas une donnee : ni case ni membre.
        if (ty->kind == HmiTypeKind::Interface) {
            out.error = path + " : " + interfaceAsData(p, ty->name);
            return;
        }
        // 1.12.20 : un rappel vise une fonction : il vit dans la memoire de l'IHM, il n'a pas de case.
        if (ty->kind == HmiTypeKind::Callback) {
            out.error = path + " : " + ty->name + " est un rappel (il vise une fonction) : il vit dans la m\xC3\xA9moire de l'IHM, ni \xC3\xA9quipement ni adresse";
            return;
        }
        // 1.10 (decision 15, S1) : une enumeration est une case DINT (le nombre de sa valeur) ;
        // sa valeur initiale s'ecrit Auto, T_MODE#Auto, le texte ou le nombre (vide : la premiere).
        if (ty->kind == HmiTypeKind::Enumeration) {
            std::int64_t n = ty->values.empty() ? 0 : ty->values.front().value;
            if (!trimmed(initial).empty() && !enumNumberOf(*ty, trimmed(initial), n)) {
                out.error = path + " : valeur initiale \xC2\xAB " + trimmed(initial) + " \xC2\xBB : pas une valeur de " + ty->name;
                return;
            }
            leaf(path, rel, "DINT", std::to_string(n), description);
            return;
        }
        // 1.13.0 : par le NOM COMPLET (Gaz.T_Vanne) - un type d'espace ne prend ni les membres ni la garde de
        // cycle d'un homonyme (T_Vanne de la racine, Util.T_Vanne) ; le registre fait de meme (la place Modbus).
        const std::string full = fullName(*ty);
        const std::string key = upperOf(full);
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
            out.error = path + " : le type " + full + " se contient lui-m\xC3\xAAme";
            return;
        }
        stack.push_back(key);
        cur.close();
        const long long start = cur.word;
        Aggregate a;
        a.path = path;
        a.spec = s;
        a.typeName = full;
        // 1.13.0 (lot J1) : l'heritage - la structure entiere (la tranche du parent d'abord), pour la place Modbus.
        const auto allMems = membersOf(p, full);
        // 1.12.10 (lot F) : LA VALEUR INITIALE D'UNE STRUCTURE, (Marche := TRUE, Consigne := 12.5) - les membres
        // nommes la prennent, les autres gardent celle du type. Ce qui ne s'applique pas (un membre inconnu, un
        // texte pour un REAL) se dit (notes) et garde la valeur du type. "0" (la valeur d'une variable neuve) : rien.
        std::vector<std::pair<std::string, std::string>> given;
        if (const std::string lit = trimmed(initial); !lit.empty() && lit != "0") {
            std::string form;
            if (!structLiteral(lit, given, &form)) {
                note(path + " : valeur initiale \xC2\xAB " + lit + " \xC2\xBB : " + form + " - les valeurs du type " + ty->name + " restent");
                given.clear();
            }
            for (const auto& [name, value] : given) {
                const bool member = std::any_of(allMems.begin(), allMems.end(), [&](const TypeMember& m) { return sameText(m.name, name); });
                if (member) continue;
                std::string names;
                for (std::size_t i = 0; i < allMems.size() && i < 6; ++i) names += (i ? ", " : "") + allMems[i].name;
                note(path + " : membre inconnu \xC2\xAB " + name + " \xC2\xBB (" + ty->name + " : " + names + ")");
            }
        }
        for (const auto& m : allMems) {
            if (!out.error.empty()) break;
            std::string init = m.initial;
            for (const auto& [name, value] : given) {
                if (!sameText(name, m.name)) continue;
                std::string unfit;
                if (literalFits(p, m.type, value, &unfit)) init = value;
                else note(path + "." + m.name + " : \xC2\xAB " + value + " \xC2\xBB : " + unfit + " - la valeur du type reste");
            }
            walk(path + "." + m.name, rel.empty() ? m.name : rel + "." + m.name, m.type, init,
                 m.description.empty() ? description : m.description);
        }
        cur.close();
        a.weight.words = cur.word - start;
        a.weight.bytes = a.weight.words * 2;
        out.aggregates.push_back(std::move(a));
        stack.pop_back();
    }
};

// Le depart d'une variable liee : sa zone, son premier mot (ou bit), sa facon de s'ecrire.
enum class Style { Schneider, Zero, X, Modicon };
enum class Zone { Holding, Input, Coil, Discrete };
struct Start {
    Zone      zone{Zone::Holding};
    long long offset{0};
    Style     style{Style::Schneider};
    int       digits{5};           // Modicon : 5 (40001) ou 6 (400001)
};

bool startOf(std::string_view start, Start& out, std::string* why) {
    const std::string raw = upperOf(trimmed(start));
    const std::string canon = equip::canonicalAddress(raw, why);
    if (canon.empty()) return false;
    // "%MW3000", "%M5", "%IW4", "%I7", "%MF20", "%MD20" ; un bit de mot : refuse.
    std::string letters;
    std::size_t i = 1;
    while (i < canon.size() && std::isalpha(static_cast<unsigned char>(canon[i]))) letters += canon[i++];
    const std::string number = canon.substr(i);
    if (number.find('.') != std::string::npos)
        return fail(why, "une structure ou un tableau part d'un mot, pas d'un bit (" + std::string(trimmed(start)) + ")");
    long long n = 0;
    if (!integerOf(number, n) || n < 0) return fail(why, "adresse illisible \xC2\xAB " + std::string(trimmed(start)) + " \xC2\xBB");
    out.offset = n;
    if (letters == "MW" || letters == "MD" || letters == "MF") out.zone = Zone::Holding;
    else if (letters == "IW" || letters == "ID" || letters == "IF") out.zone = Zone::Input;
    else if (letters == "M") out.zone = Zone::Coil;
    else if (letters == "I") out.zone = Zone::Discrete;
    else return fail(why, "adresse " + std::string(trimmed(start)) + " : pas lisible par Modbus");
    if (raw.front() == '%') out.style = Style::Schneider;
    else if (raw.rfind("HR", 0) == 0 || raw.rfind("IR", 0) == 0 || raw.rfind("CO", 0) == 0 || raw.rfind("DI", 0) == 0) out.style = Style::Zero;
    else if (raw.size() > 2 && raw[1] == 'X') out.style = Style::X;
    else {
        out.style = Style::Modicon;
        const auto dot = raw.find('.');
        out.digits = static_cast<int>(raw.substr(0, dot).size()) == 6 ? 6 : 5;
    }
    return true;
}

std::string modicon(char lead, long long n, int digits) {
    char b[24];
    if (digits == 6 || n > 9999) std::snprintf(b, sizeof b, "%c%05lld", lead, n);
    else std::snprintf(b, sizeof b, "%c%04lld", lead, n);
    return b;
}

std::string pathKey(std::string_view s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

// ------------------------------------------------------------------ types ---
// 1.11.19 (refonte, lot 6) : les types elementaires d'une variable IHM viennent du registre des
// types (typereg::UseVariable : ceux qui ont une place Modbus), sous leur nom exact.
bool isElementary(std::string_view name) noexcept {
    const auto* e = typereg::baseRegistry().byName(name);
    return e && e->usable(typereg::UseVariable) && e->category != typereg::Category::Generic
        && e->category != typereg::Category::Callback && sameText(e->name, name);   // 1.12.19 : un rappel est un objet de l'IHM
}

bool isComposite(std::string_view type) noexcept {
    const std::string t = trimmed(type);
    return !t.empty() && !isElementary(t);
}

namespace {
// 1.12.20 (lot G) : T_Mesure< real > -> T_Mesure<REAL> (ses types remis en forme) ; un autre nom tel quel.
std::string genericText(const std::string& name) {
    std::string base;
    std::vector<std::string> args;
    if (!splitGenericName(name, base, args)) return name;
    std::string out = base + "<";
    for (std::size_t k = 0; k < args.size(); ++k) out += (k ? ", " : "") + normalized(args[k]);
    return out + ">";
}

// 1.12.20 (lot G) : un type nomme et ses generiques - T_Mesure seul (generique) : ses types manquent ; T_Mesure<REAL> :
// autant de types que de generiques, chacun valide et dans sa contrainte (T doit etre un ANY_NUM ; STRING n'en est pas un).
bool genericUse(const Project& p, std::string_view name, std::string* why) {
    std::string base;
    std::vector<std::string> args;
    const auto example = [](const HmiType& g) {
        std::string out = g.name + "<";
        for (std::size_t k = 0; k < g.generics.size(); ++k) out += (k ? ", " : "") + genericSample(g.generics[k].constraint);
        return out + ">";
    };
    if (!splitGenericName(name, base, args)) {
        const HmiType* g = p.hmiTypeByName(trimmed(name));
        if (g && !g->generics.empty()) return fail(why, g->name + " est g\xC3\xA9n\xC3\xA9rique : \xC3\xA9" "cris ses types entre chevrons, " + example(*g));
        return true;
    }
    const HmiType* g = nullptr;
    for (const auto& ty : p.programs.types)
        if (sameText(ty.name, base)) g = &ty;
    if (!g) return fail(why, "type inconnu \xC2\xAB " + base + " \xC2\xBB");
    if (g->generics.empty()) return fail(why, g->name + " n'est pas g\xC3\xA9n\xC3\xA9rique : \xC3\xA9" "cris " + g->name + " sans <\xE2\x80\xA6>");
    if (g->kind != HmiTypeKind::Structure) return fail(why, g->name + " : seule une structure est g\xC3\xA9n\xC3\xA9rique");
    if (args.size() != g->generics.size())
        return fail(why, g->name + " attend " + std::to_string(g->generics.size()) + " type" + (g->generics.size() > 1 ? "s" : "") + " : " + example(*g));
    std::shared_ptr<const typereg::Registry> reg;
    for (std::size_t k = 0; k < args.size(); ++k) {
        std::string w;
        if (!validType(p, args[k], &w)) return fail(why, g->name + "<\xE2\x80\xA6> : " + w);
        const std::string& c = g->generics[k].constraint;
        if (c.empty()) continue;
        if (!reg) reg = typereg::Registry::build(p);
        if (!typereg::genericAccepts(c, reg->resolve(args[k], typereg::UseAll)))
            return fail(why, g->generics[k].name + " doit \xC3\xAAtre un " + c + " ; " + normalized(args[k]) + " n'en est pas un");
    }
    return true;
}
} // namespace

bool parseSpec(std::string_view text, Spec& out, std::string* why) {
    out = Spec{};
    const std::string t = trimmed(text);
    if (t.empty()) return fail(why, "type vide");
    const std::string up = upperOf(t);
    if (up.rfind("ARRAY", 0) != 0 || (up.size() > 5 && (std::isalnum(static_cast<unsigned char>(up[5])) || up[5] == '_'))) {
        if (!typeWord(t)) return fail(why, "type illisible \xC2\xAB " + t + " \xC2\xBB");
        out.element = isElementary(t) ? up : genericText(t);
        return true;
    }
    const auto open = t.find('[');
    const auto close = t.find(']');
    if (open == std::string::npos || close == std::string::npos || close < open)
        return fail(why, "tableau illisible \xC2\xAB " + t + " \xC2\xBB : ARRAY[0..9] OF REAL");
    const std::string inside = t.substr(open + 1, close - open - 1);
    const std::string rest = trimmed(std::string_view(t).substr(close + 1));
    if (upperOf(rest).rfind("OF", 0) != 0 || rest.size() < 3 || !std::isspace(static_cast<unsigned char>(rest[2])))
        return fail(why, "tableau \xC2\xAB " + t + " \xC2\xBB : il manque OF (ARRAY[0..9] OF REAL)");
    const std::string element = trimmed(std::string_view(rest).substr(2));
    if (upperOf(element).rfind("ARRAY", 0) == 0)
        return fail(why, "un tableau de tableaux : \xC3\xA9" "crire ARRAY[0..3, 0..9] OF ... (deux dimensions)");
    if (!typeWord(element)) return fail(why, "tableau \xC2\xAB " + t + " \xC2\xBB : type des cases illisible");
    const auto parts = splitList(inside);
    if (parts.size() > 2) return fail(why, "un tableau a une ou deux dimensions (ARRAY[0..3, 0..9] OF INT)");
    for (std::size_t d = 0; d < parts.size(); ++d)
        if (!rangeOf(parts[d], out.low[d], out.high[d], why)) return false;
    out.dims = static_cast<int>(parts.size());
    out.element = isElementary(element) ? upperOf(element) : genericText(element);
    if (out.length() > kMaxElements)
        return fail(why, "tableau de " + std::to_string(out.length()) + " cases : " + std::to_string(kMaxElements) + " au plus");
    return true;
}

std::string specText(const Spec& s) {
    if (!s.array()) return s.element;
    std::string out = "ARRAY[" + std::to_string(s.low[0]) + ".." + std::to_string(s.high[0]);
    if (s.dims == 2) out += ", " + std::to_string(s.low[1]) + ".." + std::to_string(s.high[1]);
    return out + "] OF " + s.element;
}

std::string normalized(std::string_view type) {
    Spec s;
    if (!parseSpec(type, s)) {
        // 1.12.2 : un type objet, remis en forme par le registre (list of int -> LIST OF INT).
        if (isRich(type)) return typereg::baseRegistry().resolve(type, typereg::UseAll).text;
        return trimmed(type);
    }
    return specText(s);
}

bool validType(const Project& p, std::string_view type, std::string* why) {
    if (isRich(type)) {                                       // 1.12.2
        const auto r = typereg::Registry::build(p)->resolve(type, typereg::UseVariable);
        if (!r.ok) return fail(why, r.why.empty() ? "type illisible \xC2\xAB " + trimmed(type) + " \xC2\xBB" : r.why);
        return true;
    }
    Spec s;
    if (!parseSpec(type, s, why)) return false;
    if (isElementary(s.element)) return true;
    if (!genericUse(p, s.element, why)) return false;          // 1.12.20 (lot G)
    if (!p.hmiTypeByName(s.element)) return fail(why, "type inconnu \xC2\xAB " + s.element + " \xC2\xBB");
    return true;
}

bool validMemberType(const Project& p, std::string_view type, std::string* why, std::string_view owner) {
    // 1.12.20 (lot G) : un membre d'une structure generique peut etre de type T (LIST OF T, T_Paire<STRING, T>) : verifie
    // avec un type qui tient dans la contrainte de T.
    if (!owner.empty())
        if (const HmiType* o = p.hmiTypeByName(owner); o && !o->generics.empty()) {
            std::vector<std::string> samples;
            for (const auto& g : o->generics) samples.push_back(genericSample(g.constraint));
            if (const std::string concrete = substituteGenerics(type, o->generics, samples); concrete != std::string(type))
                return validMemberType(p, concrete, why, owner);
        }
    if (!validType(p, type, why)) return false;
    // 1.13.0 (lot J1) : une interface ne s'emploie que derriere une reference (c'est un contrat, pas une donnee) -
    // ni directement (v : I_Demarrable), ni dans un conteneur sans REF_TO (LIST OF I_Demarrable). Chaque nom compte
    // (TUPLE(REF_TO I_A, I_B) : I_B est refuse).
    if (const std::string iface = interfaceAsData(p, type); !iface.empty()) return fail(why, iface);
    // 1.12.4 : un membre objet peut tout contenir, sauf le type qui le porte (T_Noeud.Enfants :
    // LIST OF T_Noeud) - ni directement, ni par un autre type.
    // 1.13.0 : par identite (le porteur nomme en entier : Gaz.T_Noeud ne se confond pas avec Util.T_Noeud).
    if (!owner.empty() && isRich(type)) {
        const HmiType* self = p.hmiTypeByName(trimmed(owner));
        for (const HmiType* t : namedTypesOf(p, type)) {
            const bool itself = self ? t == self : sameText(t->name, owner);
            if (itself || reaches(p, *t, owner))
                return fail(why, "un type IHM ne se contient pas lui-m\xC3\xAAme, m\xC3\xAAme dans une liste : " + std::string(owner)
                                     + " \xE2\x86\x92 " + normalized(type)
                                     + (itself ? std::string{} : " \xE2\x86\x92 " + fullName(*t) + " \xE2\x86\x92 \xE2\x80\xA6 \xE2\x86\x92 " + std::string(owner)));
        }
    }
    return true;
}

std::string interfaceAsData(const Project& p, std::string_view type, bool declaration) {
    std::string prev;                              // le mot d'avant, en majuscules (vide apres une ponctuation)
    for (std::size_t i = 0; i < type.size();) {
        const auto c = static_cast<unsigned char>(type[i]);
        if (!(std::isalpha(c) || type[i] == '_')) {
            if (!std::isspace(c)) prev.clear();
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < type.size() && (std::isalnum(static_cast<unsigned char>(type[j])) || type[j] == '_'
                                   || (type[j] == '.' && j + 1 < type.size() && (std::isalpha(static_cast<unsigned char>(type[j + 1])) || type[j + 1] == '_'))))
            ++j;
        const std::string_view word = type.substr(i, j - i);
        if (const HmiType* t = p.hmiTypeByName(word); t && t->kind == HmiTypeKind::Interface && prev != "REF_TO" && prev != "TO") {
            const std::string head = t->name + " est une interface : un contrat, pas une donn\xC3\xA9" "e";
            // Une locale, un parametre, un retour tiennent une reference ; une variable IHM, un membre, une liste.
            if (declaration) return head + " - emploie REF_TO " + t->name;
            return head + " - emploie un type qui l'impl\xC3\xA9mente, ou une liste de r\xC3\xA9" "f\xC3\xA9rences : LIST OF REF_TO " + t->name;
        }
        prev = upperOf(word);
        i = j;
    }
    return {};
}

bool isRich(std::string_view type) {
    const std::string t = trimmed(type);
    if (t.empty() || typeWord(t)) return false;                // un nom : elementaire ou type IHM (1.12.20 : T_Mesure<REAL>)
    const std::string u = upperOf(t);
    // Les mots qui ouvrent un type objet (ailleurs que dans les bornes d'un ARRAY).
    if (u.find("LIST") == std::string::npos && u.find("VECTOR") == std::string::npos && u.find("MAP") == std::string::npos
        && u.find("TUPLE") == std::string::npos && u.find("SET") == std::string::npos && u.find("RING") == std::string::npos   // 1.12.7 : SET, RING
        && u.find("CALLBACK") == std::string::npos)                                                                            // 1.12.19
        return false;
    return typereg::baseRegistry().resolve(t, typereg::UseAll).rich;
}

bool knownTypeName(const Project& p, std::string_view type) {
    if (!membersOf(p, type).empty()) return true;
    const HmiType* ty = p.hmiTypeByName(trimmed(type));
    // 1.13.0 (lot J1) : une interface est un nom de type connu (elle ne s'emploie que par REF_TO).
    return ty && (ty->kind == HmiTypeKind::Enumeration || ty->kind == HmiTypeKind::Callback
               || ty->kind == HmiTypeKind::Interface);
}

std::string callbackForm(const Project* p, std::string_view type) {
    const std::string t = trimmed(type);
    if (upperOf(t).rfind("CALLBACK", 0) == 0) return t;
    if (p && (identifier(t) || isNamespaceName(t)))         // 1.13.0 (lot J8) : Gaz.T_Filtre aussi
        if (const HmiType* ty = p->hmiTypeByName(t); ty && ty->kind == HmiTypeKind::Callback) return trimmed(ty->signature);
    return {};
}

bool isDynamic(std::string_view type) {
    return isRich(type) && typereg::baseRegistry().resolve(trimmed(type), typereg::UseAll).dynamic;
}

std::string unbindableReason(std::string_view type) {
    if (!isRich(type)) return {};
    const std::string t = normalized(type);
    if (upperOf(t).rfind("CALLBACK", 0) == 0)                 // 1.12.19 : un rappel
        return "un rappel (" + t + ") vise une fonction : il vit dans la m\xC3\xA9moire de l'IHM (ni \xC3\xA9quipement, ni adresse)";
    if (isDynamic(type))
        return "un " + t + " a une taille variable : il vit dans la m\xC3\xA9moire de l'IHM, il ne tient pas dans la m\xC3\xA9moire fixe "
               "d'un \xC3\xA9quipement (ni \xC3\xA9quipement, ni adresse)";
    return "un " + t + " n'a pas de plan m\xC3\xA9moire : il vit dans la m\xC3\xA9moire de l'IHM (pour le lier \xC3\xA0 un \xC3\xA9quipement, "
           "d\xC3\xA9" "clare un type IHM, une structure)";
}

std::string unbindableReason(const Project& p, std::string_view type) {
    if (const std::string form = callbackForm(&p, type); !form.empty() && upperOf(trimmed(type)).rfind("CALLBACK", 0) != 0)
        return "un rappel (" + trimmed(type) + " : " + form + ") vise une fonction : il vit dans la m\xC3\xA9moire de l'IHM (ni \xC3\xA9quipement, ni adresse)";
    return unbindableReason(type);
}

std::string cycleOf(const Project& p, std::string_view typeName) {
    std::vector<std::string> path;
    std::string found;
    std::function<void(const HmiType&)> visit = [&](const HmiType& ty) {
        if (!found.empty()) return;
        // 1.13.0 : par le nom complet - Gaz.T_Vanne qui contient un Util.T_Vanne ne se contient pas lui-meme.
        const std::string full = fullName(ty);
        const std::string key = upperOf(full);
        for (std::size_t i = 0; i < path.size(); ++i)
            if (upperOf(path[i]) == key) {
                for (std::size_t k = i; k < path.size(); ++k) found += path[k] + " -> ";
                found += full;
                return;
            }
        if (path.size() > 64) return;
        path.push_back(full);
        for (const auto& m : ty.members) {
            // 1.12.4 : un membre objet (LIST OF T_Noeud) : les types qu'il nomme.
            for (const HmiType* sub : namedTypesOf(p, m.type)) visit(*sub);
        }
        path.pop_back();
    };
    if (const auto* ty = p.hmiTypeByName(typeName)) visit(*ty);
    return found;
}

std::vector<std::string> usersOf(const Project& p, std::string_view typeName) {
    std::vector<std::string> out;
    // 1.13.0 : par IDENTITE - le type que nomme `typeName` (Gaz.T_Vanne, lu en entier) ; chaque type ecrit se lit comme
    // le depliage le lit (namedTypesOf) : un homonyme d'un autre espace (T_Vanne de la racine, Util.T_Vanne) n'est pas lui.
    const HmiType* target = p.hmiTypeByName(trimmed(typeName));
    const auto uses = [&](std::string_view type) {
        if (target) {
            const auto named = namedTypesOf(p, type);
            return std::find(named.begin(), named.end(), target) != named.end();
        }
        Spec s;
        // 1.12.20 (lot G) : T_Mesure<REAL> - le nom parmi ses mots (la structure generique, ses types).
        if (parseSpec(type, s) && s.element.find('<') == std::string::npos) return sameText(s.element, typeName);
        // 1.12.2 : un type objet (LIST OF T_Four, TUPLE(T_Four, INT)) : le nom parmi ses mots.
        const std::string t(type);
        for (std::size_t i = 0; i < t.size();) {
            if (std::isalpha(static_cast<unsigned char>(t[i])) || t[i] == '_') {
                std::size_t j = i;
                while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_')) ++j;
                if (sameText(std::string_view(t).substr(i, j - i), typeName)) return true;
                i = j;
            } else {
                ++i;
            }
        }
        return false;
    };
    for (const auto& ty : p.programs.types)
        for (const auto& m : ty.members)
            if (uses(m.type)) {
                out.push_back(fullName(ty) + "." + m.name);
                break;
            }
    for (const auto& v : p.programs.variables)
        if (uses(v.type)) out.push_back(v.name);
    return out;
}

// ------------------------------------------------------------ la place ------
long long wordsOf(std::string_view t) noexcept {
    if (sameText(t, "DINT") || sameText(t, "UDINT") || sameText(t, "DWORD") || sameText(t, "REAL") || sameText(t, "LREAL")
        || sameText(t, "TIME"))
        return 2;
    if (sameText(t, "STRING")) return 16;
    return 1;
}

Weight weightOf(const Project& p, std::string_view type, bool packBools) {
    Spec s;
    if (!parseSpec(type, s)) return {};
    if (!s.array() && isElementary(s.element)) {
        const long long w = wordsOf(s.element);
        return {w, s.element == "BOOL" && packBools ? 1 : w * 2};
    }
    const Flat f = flatten(p, "x", type, {}, packBools);
    if (!f.ok() || f.aggregates.empty()) return {};
    return f.aggregates.back().weight;
}

Flat flatten(const Project& p, std::string_view root, std::string_view type, std::string_view initial, bool packBools) {
    Flat out;
    if (isRich(type)) return out;                  // 1.12.2 : un objet de l'IHM n'a pas de cases
    Walker w{p, packBools, out, {}, {}};
    w.walk(std::string(root), {}, type, initial, {});
    if (!out.ok()) {
        out.leaves.clear();
        out.aggregates.clear();
        out.objects.clear();
    }
    return out;
}

std::vector<std::string> initialNotes(const Project& p, const Variable& v) {
    if (!isComposite(v.type) || isRich(v.type) || trimmed(v.initial).empty()) return {};
    return flatten(p, v.name, v.type, v.initial, v.packBools).notes;
}

bool structLiteral(std::string_view text, std::vector<std::pair<std::string, std::string>>& out, std::string* why) {
    out.clear();
    const std::string t = trimmed(text);
    const auto form = [&] { return fail(why, "une structure s'\xC3\xA9" "crit (Membre := valeur, ...)"); };
    if (t.size() < 2 || t.front() != '(' || t.back() != ')') return form();
    // La parenthese du debut ferme a la fin : (a := 1) + (b := 2) n'en est pas un.
    int depth = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '\'') quoted = !quoted;
        if (quoted) continue;
        if (c == '(' || c == '[') ++depth;
        else if ((c == ')' || c == ']') && --depth == 0 && i + 1 < t.size()) return form();
    }
    if (depth != 0 || quoted) return form();
    const std::string inner = trimmed(std::string_view(t).substr(1, t.size() - 2));
    if (inner.empty()) return true;
    for (const auto& item : splitList(inner)) {
        // Le := de premier niveau (pas dans une chaine).
        std::size_t at = std::string::npos;
        bool q = false;
        for (std::size_t i = 0; i + 1 < item.size(); ++i) {
            if (item[i] == '\'') q = !q;
            if (!q && item[i] == ':' && item[i + 1] == '=') { at = i; break; }
        }
        if (at == std::string::npos) return fail(why, "\xC2\xAB " + item + " \xC2\xBB : \xC3\xA9" "cris Membre := valeur");
        const std::string name = trimmed(std::string_view(item).substr(0, at));
        if (!identifier(name)) return fail(why, "\xC2\xAB " + name + " \xC2\xBB n'est pas un nom de membre");
        out.emplace_back(name, trimmed(std::string_view(item).substr(at + 2)));
    }
    return true;
}

bool bitArea(std::string_view start) noexcept {
    Start st;
    return startOf(start, st, nullptr) && (st.zone == Zone::Coil || st.zone == Zone::Discrete);
}

std::string memberAddress(std::string_view start, long long word, int bit, std::string_view type, std::string* why) {
    Start st;
    if (!startOf(start, st, why)) return {};
    const bool isBool = sameText(type, "BOOL");
    char b[40];
    if (st.zone == Zone::Coil || st.zone == Zone::Discrete) {
        if (!isBool) {
            fail(why, "un " + std::string(type) + " ne tient pas dans un bit : le d\xC3\xA9part " + trimmed(start)
                          + " est une bobine (partir d'un registre : 40001, %MW)");
            return {};
        }
        const long long n = st.offset + word;
        if (n > 65535) {
            fail(why, "au-del\xC3\xA0 de l'adresse 65535");
            return {};
        }
        switch (st.style) {
            case Style::Schneider: std::snprintf(b, sizeof b, "%s%lld", st.zone == Zone::Coil ? "%M" : "%I", n); return b;
            case Style::Zero: std::snprintf(b, sizeof b, "%s%lld", st.zone == Zone::Coil ? "CO" : "DI", n); return b;
            case Style::X: std::snprintf(b, sizeof b, "%cx%04lld", st.zone == Zone::Coil ? '0' : '1', n + 1); return b;
            case Style::Modicon: return modicon(st.zone == Zone::Coil ? '0' : '1', n + 1, st.digits);
        }
        return {};
    }
    const long long n = st.offset + word;
    if (n + wordsOf(type) - 1 > 65535) {
        fail(why, "au-del\xC3\xA0 de l'adresse 65535");
        return {};
    }
    const std::string bitText = isBool && bit >= 0 ? "." + std::to_string(bit) : std::string{};
    const bool holding = st.zone == Zone::Holding;
    switch (st.style) {
        case Style::Schneider: {
            const char* letters = holding ? "%MW" : "%IW";
            if (!isBool && wordsOf(type) == 2)
                letters = sameText(type, "REAL") || sameText(type, "LREAL") ? (holding ? "%MF" : "%IF") : (holding ? "%MD" : "%ID");
            std::snprintf(b, sizeof b, "%s%lld", letters, n);
            return std::string(b) + bitText;
        }
        case Style::Zero:
            std::snprintf(b, sizeof b, "%s%lld", holding ? "HR" : "IR", n);
            return std::string(b) + bitText;
        case Style::X:
            std::snprintf(b, sizeof b, "%cx%04lld", holding ? '4' : '3', n + 1);
            return std::string(b) + bitText;
        case Style::Modicon: return modicon(holding ? '4' : '3', n + 1, st.digits) + bitText;
    }
    return {};
}

bool memberCovers(std::string_view pattern, std::string_view rel) {
    const std::string pt = pathKey(pattern), r = pathKey(rel);
    if (pt.empty()) return false;
    std::size_t i = 0, j = 0;
    while (i < pt.size()) {
        if (pt.compare(i, 3, "[*]") == 0) {
            // [*] : un indice quelconque ([3], [1,2]).
            if (j >= r.size() || r[j] != '[') return false;
            const auto close = r.find(']', j);
            if (close == std::string::npos) return false;
            i += 3;
            j = close + 1;
            continue;
        }
        if (j >= r.size() || pt[i] != r[j]) return false;
        ++i;
        ++j;
    }
    return j == r.size() || r[j] == '.' || r[j] == '[';
}

// ------------------------------------------- 1.12.18 : l'adresse par formule ---
namespace {

// Un calcul entier : + - * / MOD, parentheses, i et j, des nombres.
struct IntFormula {
    std::string_view              s;
    const std::vector<long long>& idx;
    std::size_t                   p{0};
    std::string                   err{};
    static constexpr long long    kLimit = 1000000000000LL;

    bool fault(std::string m) {
        if (err.empty()) err = std::move(m);
        return false;
    }
    void blank() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    }
    bool bounded(long long v) { return v > -kLimit && v < kLimit ? true : fault("nombre trop grand"); }
    bool mod() {
        blank();
        if (p + 3 > s.size() || upperOf(s.substr(p, 3)) != "MOD") return false;
        if (p + 3 < s.size() && (std::isalnum(static_cast<unsigned char>(s[p + 3])) || s[p + 3] == '_')) return false;
        p += 3;
        return true;
    }
    bool primary(long long& v) {
        blank();
        if (p >= s.size()) return fault("formule incompl\xC3\xA8te");
        const char c = s[p];
        if (c == '(') {
            ++p;
            if (!sum(v)) return false;
            blank();
            if (p >= s.size() || s[p] != ')') return fault("il manque une parenth\xC3\xA8se fermante");
            ++p;
            return true;
        }
        if (c == '-' || c == '+') {
            ++p;
            if (!primary(v)) return false;
            if (c == '-') v = -v;
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            long long n = 0;
            while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
                n = n * 10 + (s[p++] - '0');
                if (!bounded(n)) return false;
            }
            v = n;
            return true;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::size_t from = p;
            while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '_')) ++p;
            const std::string name = upperOf(s.substr(from, p - from));
            if (name == "I" || name == "J") {
                const std::size_t k = name == "I" ? 0 : 1;
                if (k >= idx.size())
                    return fault(name == "I" ? std::string("i : pas d'indice [*] ici") : std::string("j : une seule dimension (i seulement)"));
                v = idx[k];
                return true;
            }
            return fault(std::string(s.substr(from, p - from)) + " inconnu : i et j seulement");
        }
        return fault(std::string("\xC2\xAB ") + c + " \xC2\xBB : + - * / MOD ( ) seulement");
    }
    bool product(long long& v) {
        if (!primary(v)) return false;
        while (true) {
            blank();
            const bool isMod = mod();
            if (!isMod && (p >= s.size() || (s[p] != '*' && s[p] != '/'))) return true;
            const char op = isMod ? '%' : s[p++];
            long long r = 0;
            if (!primary(r)) return false;
            if (op == '*') v *= r;
            else if (r == 0) return fault("division par z\xC3\xA9ro");
            else if (op == '/') v /= r;
            else v %= r;
            if (!bounded(v)) return false;
        }
    }
    bool sum(long long& v) {
        if (!product(v)) return false;
        while (true) {
            blank();
            if (p >= s.size() || (s[p] != '+' && s[p] != '-')) return true;
            const char op = s[p++];
            long long r = 0;
            if (!product(r)) return false;
            v = op == '+' ? v + r : v - r;
            if (!bounded(v)) return false;
        }
    }
};

} // namespace

bool isFormula(std::string_view address) noexcept { return address.find('(') != std::string_view::npos; }

bool patternIndices(std::string_view pattern, std::string_view rel, std::vector<long long>& out, std::string* cell) {
    out.clear();
    const std::string pt = pathKey(pattern);
    if (pt.empty()) return false;
    std::string r;                    // `rel` sans blancs, en majuscules ; at[k] : la place de r[k] dans `rel`
    std::vector<std::size_t> at;
    for (std::size_t k = 0; k < rel.size(); ++k)
        if (!std::isspace(static_cast<unsigned char>(rel[k]))) {
            r += static_cast<char>(std::toupper(static_cast<unsigned char>(rel[k])));
            at.push_back(k);
        }
    std::size_t i = 0, j = 0;
    while (i < pt.size()) {
        if (pt.compare(i, 3, "[*]") == 0) {
            if (j >= r.size() || r[j] != '[') return false;
            const auto close = r.find(']', j);
            if (close == std::string::npos) return false;
            for (const auto& part : splitList(r.substr(j + 1, close - j - 1))) {
                long long n = 0;
                if (!integerOf(trimmed(part), n)) return false;
                out.push_back(n);
            }
            i += 3;
            j = close + 1;
            continue;
        }
        if (j >= r.size() || pt[i] != r[j]) return false;
        ++i;
        ++j;
    }
    if (j != r.size() && r[j] != '.' && r[j] != '[') return false;
    if (cell) *cell = std::string(rel.substr(0, j < at.size() ? at[j] : rel.size()));
    return true;
}

std::string formulaAddress(std::string_view formula, const std::vector<long long>& indices, std::string* why) {
    std::string out;
    for (std::size_t k = 0; k < formula.size();) {
        const char c = formula[k];
        if (c == ')') {
            fail(why, "parenth\xC3\xA8se fermante sans ouvrante");
            return {};
        }
        if (c != '(') {
            if (!std::isspace(static_cast<unsigned char>(c))) out += c;
            ++k;
            continue;
        }
        // Un calcul entre parentheses (les siennes comprises), remplace par sa valeur.
        int depth = 0;
        std::size_t end = k;
        for (; end < formula.size(); ++end) {
            if (formula[end] == '(') ++depth;
            else if (formula[end] == ')' && --depth == 0) break;
        }
        if (end >= formula.size()) {
            fail(why, "il manque une parenth\xC3\xA8se fermante");
            return {};
        }
        IntFormula e{formula.substr(k + 1, end - k - 1), indices};
        long long v = 0;
        bool good = e.sum(v);
        if (good) {
            e.blank();
            if (e.p < e.s.size()) good = e.fault("\xC2\xAB " + std::string(e.s.substr(e.p)) + " \xC2\xBB en trop");
        }
        if (!good) {
            fail(why, e.err);
            return {};
        }
        if (v < 0) {
            fail(why, "adresse n\xC3\xA9gative (" + std::to_string(v) + ")");
            return {};
        }
        out += std::to_string(v);
        k = end + 1;
    }
    return out;
}

std::string internalEntryOf(const Variable& v, std::string_view rel) {
    if (!v.bound()) return {};
    for (const auto& e : v.internal)
        if (memberCovers(e, rel)) return e;
    return {};
}

bool isInternalMember(const Variable& v, std::string_view rel) { return !internalEntryOf(v, rel).empty(); }

std::vector<Variable> leafVariables(const Project& p, const Variable& v, std::vector<Aggregate>* aggregates,
                                    std::vector<std::string>* why, std::string* error, std::vector<ObjectMember>* objects) {
    std::vector<Variable> out;
    if (!isComposite(v.type)) {
        out.push_back(v);
        if (why) why->emplace_back();
        return out;
    }
    if (isRich(v.type)) return out;                // 1.12.2 : un objet de l'IHM, sans cases
    const Flat f = flatten(p, v.name, v.type, v.initial, v.packBools);
    if (!f.ok()) {
        if (error) *error = f.error;
        return out;
    }
    if (aggregates) aggregates->insert(aggregates->end(), f.aggregates.begin(), f.aggregates.end());
    if (objects) objects->insert(objects->end(), f.objects.begin(), f.objects.end());    // 1.12.4
    const bool bits = v.bound() && !v.address.empty() && bitArea(v.address);
    const std::size_t n = f.leaves.size();
    // 1.11.8 : les membres internes (gardes dans l'IHM).
    std::vector<char> internal(n, 0);
    if (v.bound() && !v.internal.empty())
        for (std::size_t i = 0; i < n; ++i) internal[i] = isInternalMember(v, f.leaves[i].rel) ? 1 : 0;
    // 1.11.8 (« Recalculer la place memoire ») : les mots que n'occupent que des membres
    // internes sont rendus ; chaque case recule d'autant de mots rendus avant elle. Les BOOL
    // d'une zone de bits se comptent par rang : un BOOL interne n'en prend plus.
    std::vector<long long> word(n), rank(n);
    {
        std::vector<long long> freed;
        if (v.compact && v.bound()) {
            std::map<long long, bool> onlyInternal;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& l = f.leaves[i];
                const long long count = std::max<long long>(1, wordsOf(l.type));
                for (long long w = l.word; w < l.word + count; ++w) {
                    const auto [it, inserted] = onlyInternal.emplace(w, internal[i] != 0);
                    if (!inserted) it->second = it->second && internal[i] != 0;
                }
            }
            for (const auto& [w, only] : onlyInternal)
                if (only) freed.push_back(w);
        }
        long long r = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& l = f.leaves[i];
            word[i] = l.word - static_cast<long long>(std::lower_bound(freed.begin(), freed.end(), l.word) - freed.begin());
            rank[i] = r;
            if (l.type == "BOOL" && !(v.compact && internal[i])) ++r;
        }
    }
    // 1.11.8 : le depart d'un membre compose ("Vannes[2]" -> "%MW3050") - ses cases le suivent,
    // a la meme place l'une par rapport a l'autre. 1.12.18 : une formule de i ("[*].Retour" ->
    // "%MW(1000 + 10 * i)" ; "[*]" -> "%MW(2000 + 8 * i)", le depart de chaque case). Le plus precis
    // gagne (Vannes[2].Sous avant Vannes[2], [2] avant [*]) ; l'adresse tapee sur la case elle-meme d'abord.
    struct Start {
        std::string pattern, address;
        long long   base{0};
        bool        formula{false};
        std::map<std::string, long long> bases{};   // une formule : le depart de chaque case designee (sa cle)
    };
    std::vector<Start> starts;
    if (v.bound())
        for (const auto& pl : v.places) {
            const std::string address = trimmed(pl.address);
            if (address.empty()) continue;
            Start s{pl.path, address, 0, isFormula(address)};
            bool leaf = false, any = false;
            std::vector<long long> idx;
            std::string cell;
            for (std::size_t i = 0; i < n; ++i) {
                if (pathKey(pl.path) == pathKey(f.leaves[i].rel)) leaf = true;
                if (s.formula ? !patternIndices(pl.path, f.leaves[i].rel, idx, &cell) : !memberCovers(pl.path, f.leaves[i].rel)) continue;
                if (v.compact && internal[i]) continue;
                const long long at = bits ? rank[i] : word[i];
                if (s.formula) {
                    const auto [it, inserted] = s.bases.emplace(pathKey(cell), at);
                    if (!inserted) it->second = std::min(it->second, at);
                } else if (!any || at < s.base) {
                    s.base = at;
                }
                any = true;
            }
            if (!leaf && any) starts.push_back(std::move(s));
        }
    const auto wildcards = [](const std::string& key) {
        std::size_t c = 0;
        for (auto at = key.find("[*]"); at != std::string::npos; at = key.find("[*]", at + 3)) ++c;
        return c;
    };
    std::stable_sort(starts.begin(), starts.end(), [&](const Start& a, const Start& b) {
        const std::string ka = pathKey(a.pattern), kb = pathKey(b.pattern);
        if (ka.size() != kb.size()) return ka.size() > kb.size();
        return wildcards(ka) < wildcards(kb);
    });
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& l = f.leaves[i];
        Variable x;
        x.id = v.id;
        x.name = l.path;
        x.type = l.type;
        x.initial = l.initial;
        x.description = l.description.empty() ? v.description : l.description;
        x.folder = v.folder;
        x.readOnly = v.readOnly;
        x.packBools = v.packBools;
        std::string reason;
        if (v.bound() && !internal[i]) {
            x.equipment = v.equipment;
            const MemberAddress* fixed = nullptr;
            for (const auto& pl : v.places)
                if (pathKey(pl.path) == pathKey(l.rel)) fixed = &pl;
            const Start* from = nullptr;
            std::vector<long long> idx;
            std::string cell;
            for (const auto& s : starts)
                if (s.formula ? patternIndices(s.pattern, l.rel, idx, &cell) : memberCovers(s.pattern, l.rel)) {
                    from = &s;
                    break;
                }
            if (fixed && !trimmed(fixed->address).empty()) {
                if (!isFormula(fixed->address)) x.address = trimmed(fixed->address);
                else if (std::string why2; (x.address = formulaAddress(fixed->address, {}, &why2)).empty()) reason = "formule " + trimmed(fixed->address) + " : " + why2;
            } else if (from && from->formula) {
                std::string why2;
                const std::string start = formulaAddress(from->address, idx, &why2);
                if (start.empty()) {
                    reason = "formule " + from->pattern + " = " + from->address + " : " + why2;
                } else if (pathKey(cell) == pathKey(l.rel)) {
                    x.address = start;                         // un membre simple : son adresse
                } else {                                       // une case entiere : son depart, ses membres suivent
                    const auto it = from->bases.find(pathKey(cell));
                    const long long base = it == from->bases.end() ? 0 : it->second;
                    x.address = bits ? memberAddress(start, rank[i] - base, -1, l.type, &reason)
                                     : memberAddress(start, word[i] - base, l.bit, l.type, &reason);
                }
            } else if (from) {
                // (le depart d'un membre est de la meme sorte que celui de la variable : mots, ou bits)
                x.address = bits ? memberAddress(from->address, rank[i] - from->base, -1, l.type, &reason)
                                 : memberAddress(from->address, word[i] - from->base, l.bit, l.type, &reason);
            } else if (v.address.empty()) {
                reason = "sans adresse de d\xC3\xA9part";
            } else if (bits) {
                x.address = l.type == "BOOL" ? memberAddress(v.address, rank[i], -1, l.type, &reason)
                                             : memberAddress(v.address, 0, -1, l.type, &reason);
            } else {
                x.address = memberAddress(v.address, word[i], l.bit, l.type, &reason);
            }
        }
        if (why) why->push_back(reason);
        out.push_back(std::move(x));
    }
    return out;
}

std::vector<ObjectMember> objectMembers(const Project& p, const Variable& v) {
    if (!isComposite(v.type) || isRich(v.type)) return {};
    Flat f = flatten(p, v.name, v.type, v.initial, v.packBools);
    return f.ok() ? std::move(f.objects) : std::vector<ObjectMember>{};
}

std::vector<Variable> flatVariables(const Project& p) {
    std::vector<Variable> out;
    out.reserve(p.programs.variables.size());
    for (const auto& v : p.programs.variables) {
        if (!isComposite(v.type)) {
            out.push_back(v);
            continue;
        }
        auto leaves = leafVariables(p, v);
        for (auto& l : leaves) out.push_back(std::move(l));
    }
    return out;
}

namespace {
bool spanOfLeaves(const std::vector<Variable>& leaves, Span& out) {
    out = Span{};
    bool any = false;
    for (const auto& leaf : leaves) {
        comm::Point pt;
        if (leaf.address.empty() || !equip::placeEquipmentAddress(leaf.address, equip::registerType(leaf), pt)) continue;
        const long long first = pt.offset;
        const long long last = static_cast<long long>(pt.offset) + (pt.bits() ? 0 : pt.size - 1);
        if (!any) {
            out.first = first;
            out.last = last;
            out.bits = pt.bits();
            any = true;
        } else {
            out.first = std::min(out.first, first);
            out.last = std::max(out.last, last);
        }
    }
    return any;
}
} // namespace

bool spanOf(const Project& p, const Variable& v, Span& out) {
    if (!v.bound() || v.address.empty()) return false;
    return spanOfLeaves(leafVariables(p, v), out);
}

std::string spanText(const Project& p, const Variable& v) {
    if (!v.bound() || v.address.empty()) return {};
    return spanTextOf(leafVariables(p, v), v.address);
}

std::string spanTextOf(const std::vector<Variable>& leaves, std::string_view startAddress) {
    Span s;
    if (!spanOfLeaves(leaves, s)) return {};
    const std::string raw = upperOf(trimmed(startAddress));
    const bool schneider = !raw.empty() && raw.front() == '%';
    Start st;
    (void)startOf(startAddress, st, nullptr);
    char a[24], b[24];
    if (s.bits) {
        const char lead = st.zone == Zone::Coil ? '0' : '1';
        const std::string what = st.zone == Zone::Coil ? "bobine" : "entr\xC3\xA9" "e TOR";
        if (schneider) {
            if (s.first == s.last) return "bit " + std::to_string(s.first);
            return "bits " + std::to_string(s.first) + " \xC3\xA0 " + std::to_string(s.last);
        }
        if (s.first == s.last) return what + " " + modicon(lead, s.first + 1, 5);
        return what + "s " + modicon(lead, s.first + 1, 5) + " \xC3\xA0 " + modicon(lead, s.last + 1, 5);
    }
    if (schneider) {
        if (s.first == s.last) return "mot " + std::to_string(s.first);
        return "mots " + std::to_string(s.first) + " \xC3\xA0 " + std::to_string(s.last);
    }
    const char lead = st.zone == Zone::Input ? '3' : '4';
    std::snprintf(a, sizeof a, "%s", modicon(lead, s.first + 1, st.digits).c_str());
    std::snprintf(b, sizeof b, "%s", modicon(lead, s.last + 1, st.digits).c_str());
    if (s.first == s.last) return std::string("registre ") + a;
    return std::string("registres ") + a + " \xC3\xA0 " + b;
}

// -------------------------------------------------- les proprietes ----------
const std::vector<PropertyInfo>& properties() {
    static const std::vector<PropertyInfo> all = {
        {"Length", "DINT", "le nombre de cases (toutes, pour deux dimensions : Rows \xC3\x97 Columns)", true, false},
        {"Size", "DINT", "le m\xC3\xAAme que Length, sous son autre nom", true, false},
        {"MaxSize", "DINT", "la capacit\xC3\xA9 d\xC3\xA9" "clar\xC3\xA9" "e ; un tableau a une taille fixe : \xC3\xA9gale \xC3\xA0 Length", true, false},
        {"Low", "DINT", "la borne basse (de la premi\xC3\xA8re dimension)", true, false},
        {"High", "DINT", "la borne haute (de la premi\xC3\xA8re dimension)", true, false},
        {"Low2", "DINT", "la borne basse de la deuxi\xC3\xA8me dimension", true, true},
        {"High2", "DINT", "la borne haute de la deuxi\xC3\xA8me dimension", true, true},
        {"Rows", "DINT", "le nombre de lignes (la premi\xC3\xA8re dimension)", true, false},
        {"Columns", "DINT", "le nombre de colonnes (la deuxi\xC3\xA8me dimension ; 1 pour un tableau \xC3\xA0 une dimension)", true, false},
        {"Dimensions", "INT", "1 ou 2", true, false},
        {"Bytes", "DINT", "le poids en octets (la place Modbus : 2 octets par mot)", false, false},
        {"Words", "DINT", "le poids en mots Modbus de 16 bits", false, false},
        {"ElementBytes", "DINT", "le poids d'une case, en octets (un BOOL : 1)", true, false},
    };
    return all;
}

const PropertyInfo* property(std::string_view name) noexcept {
    for (const auto& pi : properties())
        if (sameText(pi.name, name)) return &pi;
    return nullptr;
}

bool propertyValue(const Aggregate& a, std::string_view name, sim::Value& out) {
    const PropertyInfo* pi = property(name);
    if (!pi) return false;
    if (pi->arrayOnly && !a.array) return false;
    const auto dint = [&](long long v) {
        out = sim::Value::integer(sim::Type::DInt, v);
        return true;
    };
    const std::string n = upperOf(name);
    if (n == "BYTES") return dint(a.weight.bytes);
    if (n == "WORDS") return dint(a.weight.words);
    const Spec& s = a.spec;
    if (n == "LENGTH" || n == "SIZE" || n == "MAXSIZE") return dint(s.length());
    if (n == "LOW") return dint(s.low[0]);
    if (n == "HIGH") return dint(s.high[0]);
    if (n == "LOW2") return dint(s.dims == 2 ? s.low[1] : 0);
    if (n == "HIGH2") return dint(s.dims == 2 ? s.high[1] : 0);
    if (n == "ROWS") return dint(s.count(0));
    if (n == "COLUMNS") return dint(s.dims == 2 ? s.count(1) : 1);
    if (n == "DIMENSIONS") {
        out = sim::Value::integer(sim::Type::Int, s.dims);
        return true;
    }
    if (n == "ELEMENTBYTES") return dint(a.elementBytes);
    return false;
}

// ------------------------------------------------------ les chemins ---------
// 1.12.14 : les pas dans un type, a toute profondeur (voir HmiTypes.hpp).
TypeStep stepIndex(const Project& p, std::string_view type) {
    (void)p;
    TypeStep st;
    const std::string t = normalized(type);
    const auto sh = typeform::decompose(t);
    if (sh.reference) return st;                                   // une reference : on ne sait pas
    switch (sh.form) {
        case typeform::Form::Array:
        case typeform::Form::Array2D:
        case typeform::Form::List:
        case typeform::Form::Vector:
        case typeform::Form::Set:
        case typeform::Form::Ring:
        case typeform::Form::Map: st.type = normalized(sh.element); return st;
        case typeform::Form::Tuple:
            st.why = "est un " + t + " : ses \xC3\xA9l\xC3\xA9ments se lisent .Item1, .Item2\xE2\x80\xA6, pas entre crochets";
            return st;
        case typeform::Form::Simple: break;
    }
    if (t.empty()) return st;
    if (isElementary(t) || p.hmiTypeByName(t)) st.why = "est un " + t + " : pas d'indice entre crochets";
    return st;
}

TypeStep stepMember(const Project& p, std::string_view type, std::string_view member) {
    TypeStep st;
    const std::string t = normalized(type);
    const auto sh = typeform::decompose(t);
    if (sh.reference || member.empty()) return st;
    std::string m(member);
    for (auto& ch : m) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const auto dint = [&](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (m == n) {
                st.type = "DINT";
                st.property = true;
                return true;
            }
        return false;
    };
    switch (sh.form) {
        case typeform::Form::Array:
        case typeform::Form::Array2D:
            if (dint({"LENGTH", "SIZE", "COUNT", "MAXSIZE", "LOW", "HIGH", "ROWS", "COLUMNS", "COLS", "DIMENSIONS"})) return st;
            st.why = "est un tableau : ." + std::string(member) + " n'existe pas (Length, Low, High\xE2\x80\xA6 ou [i] pour une case)";
            return st;
        case typeform::Form::List:
        case typeform::Form::Vector:
        case typeform::Form::Set:
        case typeform::Form::Ring:
            if (dint({"LENGTH", "SIZE", "COUNT", "LOW", "HIGH"}) || (sh.form == typeform::Form::Ring && dint({"CAPACITY", "MAXSIZE"}))) return st;
            if (m == "FIRST" || m == "LAST") {
                st.type = normalized(sh.element);
                return st;
            }
            if (m == "EMPTY" || m == "ISEMPTY" || (sh.form == typeform::Form::Ring && m == "FULL")) {
                st.type = "BOOL";
                st.property = true;
                return st;
            }
            st.why = "est un " + t + " : ." + std::string(member) + " n'existe pas (Count, First, Last, Empty\xE2\x80\xA6 ou [i] pour un \xC3\xA9l\xC3\xA9ment)";
            return st;
        case typeform::Form::Map:
            if (dint({"LENGTH", "SIZE", "COUNT"})) return st;
            st.why = "est un " + t + " : ." + std::string(member) + " n'existe pas (Count, ou ['cl\xC3\xA9'] pour une valeur)";
            return st;
        case typeform::Form::Tuple: {
            std::vector<std::string> items{sh.element};
            if (!trimmed(sh.parameter).empty())
                for (auto& x : splitList(sh.parameter)) items.push_back(x);
            if (m.rfind("ITEM", 0) == 0 && m.size() > 4) {
                const auto n = std::strtoul(m.c_str() + 4, nullptr, 10);
                if (n >= 1 && n <= items.size()) {
                    st.type = normalized(items[n - 1]);
                    return st;
                }
            }
            st.why = "(" + t + ") n'a pas de membre " + std::string(member) + " (Item1 \xC3\xA0 Item" + std::to_string(items.size()) + ")";
            return st;
        }
        case typeform::Form::Simple: break;
    }
    if (isElementary(t)) {
        if (!std::isdigit(static_cast<unsigned char>(member[0]))) st.why = "est un " + t + " : pas de membre ." + std::string(member);
        return st;
    }
    const auto* ty = p.hmiTypeByName(t);
    if (!ty) return st;
    // 1.13.0 (lot J1) : les membres herites (parent d'abord) comptent aussi.
    const auto inherited = membersOf(p, t);
    for (const auto& mm : inherited)
        if (sameText(mm.name, member)) {
            st.type = normalized(mm.type);
            return st;
        }
    if (const PropertyInfo* pi = property(member); pi && !pi->arrayOnly) {
        st.property = true;
        return st;
    }
    // 1.13.0 (lot J1) : une methode du type (ou heritee) - L[0].Incr(), Pompes[3].EnDefaut() : son appel, pas un membre
    // (son type n'est pas suivi ici : la chaine s'arrete).
    if (effectiveMethod(p, fullName(*ty), member)) return st;
    std::string list;
    for (const auto& mm : inherited) list += (list.empty() ? "" : ", ") + mm.name;
    st.why = "(" + ty->name + ") n'a pas de membre " + std::string(member) + (list.empty() ? std::string{} : " (membres : " + list + ")");
    return st;
}

std::vector<PublicProperty> publicProperties(std::string_view type) {
    const auto sh = typeform::decompose(normalized(type));
    if (sh.reference) return {};
    const std::string e = normalized(sh.element);
    switch (sh.form) {
        case typeform::Form::Array:
            return {{"Length", "DINT", "Size, Count, MaxSize", "le nombre de cases"},
                    {"Low", "DINT", "", "le premier indice"},
                    {"High", "DINT", "", "le dernier indice"}};
        case typeform::Form::Array2D:
            return {{"Length", "DINT", "Size, Count, MaxSize", "le nombre de cases (Rows \xC3\x97 Columns)"},
                    {"Rows", "DINT", "", "le nombre de lignes (la premi\xC3\xA8re dimension)"},
                    {"Columns", "DINT", "Cols", "le nombre de colonnes (la deuxi\xC3\xA8me dimension)"},
                    {"Low", "DINT", "", "le premier indice de la premi\xC3\xA8re dimension"},
                    {"High", "DINT", "", "le dernier indice de la premi\xC3\xA8re dimension"},
                    {"Dimensions", "DINT", "", "le nombre de dimensions (2)"}};
        case typeform::Form::List:
        case typeform::Form::Vector:
        case typeform::Form::Set:
        case typeform::Form::Ring: {
            std::vector<PublicProperty> out{{"Count", "DINT", "Size, Length", "le nombre d'\xC3\xA9l\xC3\xA9ments"},
                                            {"Empty", "BOOL", "IsEmpty", "vrai : aucun \xC3\xA9l\xC3\xA9ment"},
                                            {"First", e, "", "le premier \xC3\xA9l\xC3\xA9ment"},
                                            {"Last", e, "", "le dernier \xC3\xA9l\xC3\xA9ment"},
                                            {"Low", "DINT", "", "le premier indice (0)"},
                                            {"High", "DINT", "", "le dernier indice (Count - 1)"}};
            if (sh.form == typeform::Form::Ring) {
                out.push_back({"Capacity", "DINT", "MaxSize", "la capacit\xC3\xA9 (le N de RING[N])"});
                out.push_back({"Full", "BOOL", "", "vrai : plein (Count = Capacity)"});
            }
            return out;
        }
        case typeform::Form::Map: return {{"Count", "DINT", "Size, Length", "le nombre d'entr\xC3\xA9" "es"}};
        case typeform::Form::Tuple:
        case typeform::Form::Simple: break;
    }
    return {};
}

std::string typeOfPath(const Project& p, std::string_view path) {
    // La racine : une variable IHM.
    std::size_t i = 0;
    while (i < path.size() && (std::isalnum(static_cast<unsigned char>(path[i])) || path[i] == '_')) ++i;
    const auto* v = p.variable(path.substr(0, i));
    if (!v) return {};
    std::string type = normalized(v->type);
    // 1.12.14 : chaque pas, a toute profondeur (Parc['a'][1].Niveau : MAP, VECTOR, T_Cuve, REAL).
    while (i < path.size()) {
        const char c = path[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '[') {
            // Les indices ne sont pas evalues : on passe au crochet fermant.
            int depth = 0;
            std::size_t j = i;
            for (; j < path.size(); ++j) {
                if (path[j] == '[') ++depth;
                else if (path[j] == ']' && --depth == 0) break;
            }
            if (j >= path.size()) return {};
            const auto st = stepIndex(p, type);
            if (st.type.empty()) return {};
            type = st.type;
            i = j + 1;
            continue;
        }
        if (c == '.') {
            std::size_t j = i + 1;
            while (j < path.size() && (std::isalnum(static_cast<unsigned char>(path[j])) || path[j] == '_')) ++j;
            const auto st = stepMember(p, type, path.substr(i + 1, j - i - 1));
            if (st.type.empty()) return {};
            type = st.type;
            i = j;
            continue;
        }
        return {};
    }
    return type;
}

const HmiType* typeFrom(const Project& p, std::string_view nsIn, std::string_view nameIn, const HmiType* self) {
    const std::string name = trimmed(nameIn);
    if (name.empty()) return nullptr;
    if (name.find('.') != std::string::npos) {
        const HmiType* t = p.hmiTypeByName(name);
        return t == self ? nullptr : t;
    }
    // un nom court : l'espace, puis ses parents (Gaz.Ligne voit Gaz) - jamais `self`
    for (std::string ns(nsIn); !ns.empty();) {
        if (const HmiType* t = p.hmiTypeByName(qualifiedName(ns, name)); t && t != self) return t;
        const auto dot = ns.rfind('.');
        ns = dot == std::string::npos ? std::string{} : ns.substr(0, dot);
    }
    const HmiType* t = p.hmiTypeByName(name);
    return t == self ? nullptr : t;
}

const HmiType* baseOf(const Project& p, const HmiType& t) {
    return t.base.empty() ? nullptr : typeFrom(p, t.ns, t.base, &t);
}

std::vector<TypeMember> membersOf(const Project& p, std::string_view typeName) {
    Spec s;
    if (!parseSpec(typeName, s) || s.array()) return {};
    const auto* ty = p.hmiTypeByName(s.element);
    if (!ty) return {};
    // 1.13.0 (lot J1) : l'heritage - la tranche du parent D'ABORD (la racine en tete), puis les siens.
    // La chaine de base (du parent vers la racine), avec un garde-fou contre un cycle (par NOM COMPLET :
    // Gaz.T_Vanne EXTENDS Base.T_Vanne n'est pas un cycle).
    if (ty->base.empty()) return ty->members;
    std::vector<const HmiType*> ancestors;      // [parent, grand-parent, ... racine]
    std::set<std::string> seen{upperOf(fullName(*ty))};
    for (const HmiType* b = baseOf(p, *ty); b; b = baseOf(p, *b)) {
        if (!seen.insert(upperOf(fullName(*b))).second) break;
        ancestors.push_back(b);
    }
    std::vector<TypeMember> out;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)   // racine d'abord
        out.insert(out.end(), (*it)->members.begin(), (*it)->members.end());
    out.insert(out.end(), ty->members.begin(), ty->members.end());
    return out;
}

// 1.12.17 : un conteneur, une structure - ce qui n'est pas une valeur seule (la forme la plus exterieure
// du type, typeform::decompose ; une reference REF_TO : rien de dit).
Whole wholeOf(const Project* project, std::string_view type, std::string_view shown) {
    Whole w;
    const std::string t = trimmed(type);
    if (t.empty()) return w;
    const std::string s(shown);
    const auto set = [&w](const char* what, const char* entire, std::string pick) {
        w.what = what;
        w.entire = entire;
        w.pick = std::move(pick);
    };
    const auto shape = typeform::decompose(t);
    if (shape.reference) return w;
    switch (shape.form) {
        case typeform::Form::List:
            set("une liste", "une liste enti\xC3\xA8re", "un \xC3\xA9l\xC3\xA9ment ou une propri\xC3\xA9t\xC3\xA9 : " + s + "[0] ou " + s + ".Count");
            break;
        case typeform::Form::Vector:
            set("un vecteur", "un vecteur entier", "un \xC3\xA9l\xC3\xA9ment ou une propri\xC3\xA9t\xC3\xA9 : " + s + "[0] ou " + s + ".Count");
            break;
        case typeform::Form::Set:
            set("un ensemble", "un ensemble entier", "une propri\xC3\xA9t\xC3\xA9 : " + s + ".Count");
            break;
        case typeform::Form::Ring:
            set("un tampon circulaire", "un tampon circulaire entier", "un \xC3\xA9l\xC3\xA9ment ou une propri\xC3\xA9t\xC3\xA9 : " + s + ".Last ou " + s + ".Count");
            break;
        case typeform::Form::Map:
            set("une MAP", "une MAP enti\xC3\xA8re", "une entr\xC3\xA9" "e ou une propri\xC3\xA9t\xC3\xA9 : " + s + "['cl\xC3\xA9'] ou " + s + ".Count");
            break;
        case typeform::Form::Tuple:
            set("un tuple", "un tuple entier", "un \xC3\xA9l\xC3\xA9ment : " + s + ".Item1");
            break;
        case typeform::Form::Array:
        case typeform::Form::Array2D: {
            Spec spec;
            std::string first = shape.form == typeform::Form::Array2D ? "0, 0" : "0";
            if (parseSpec(t, spec) && spec.array())
                first = std::to_string(spec.low[0]) + (spec.dims > 1 ? ", " + std::to_string(spec.low[1]) : std::string{});
            set("un tableau", "un tableau entier", "un \xC3\xA9l\xC3\xA9ment : " + s + "[" + first + "]");
            break;
        }
        case typeform::Form::Simple:
            if (project) {
                const auto members = membersOf(*project, t);
                if (!members.empty()) {
                    std::string pick;
                    for (std::size_t k = 0; k < members.size() && k < 3; ++k) pick += (k ? ", " : "") + s + "." + members[k].name;
                    set("une structure", "une structure enti\xC3\xA8re", "un membre : " + pick);
                }
            }
            break;
    }
    return w;
}

std::string summary(const Project& p, std::string_view type, bool packBools) {
    Spec s;
    if (!parseSpec(type, s)) return {};
    const Weight w = weightOf(p, type, packBools);
    const std::string words = std::to_string(w.words) + " mot" + (w.words > 1 ? "s" : "");
    if (s.array()) {
        std::string dims = std::to_string(s.low[0]) + ".." + std::to_string(s.high[0]);
        if (s.dims == 2) dims += ", " + std::to_string(s.low[1]) + ".." + std::to_string(s.high[1]);
        return "tableau de " + std::to_string(s.length()) + " " + s.element + " (" + dims + "), " + words;
    }
    if (isElementary(s.element)) return s.element + ", " + words;
    const auto* ty = p.hmiTypeByName(s.element);
    if (!ty) return "type inconnu";
    return "structure " + ty->name + " : " + std::to_string(ty->members.size()) + " membre" + (ty->members.size() > 1 ? "s" : "") + ", " + words;
}

// ------------------------------------------------- la verification ---------
namespace {
// 1.13.0 (lot J1) : un membre ou une methode PRIVE de `typeName` (ou d'un de ses parents), lu de l'exterieur (un
// script, une fonction, une fx : pathProblems ne lit jamais le corps d'une methode) : le message ; vide : permis.
std::string privateUse(const Project& p, std::string_view typeName, std::string_view member) {
    const HmiType* ty = p.hmiTypeByName(trimmed(typeName));
    if (!ty || ty->kind != HmiTypeKind::Structure) return {};
    std::vector<const HmiType*> chain = baseChain(p, fullName(*ty));     // [parent, grand-parent...]
    std::reverse(chain.begin(), chain.end());                        // la racine d'abord, comme membersOf
    chain.push_back(ty);
    for (const HmiType* t : chain)
        for (const auto& m : t->members)
            if (sameText(m.name, member))
                return m.visibility == Visibility::Private
                           ? m.name + " est priv\xC3\xA9 dans " + t->name + " : seules ses m\xC3\xA9thodes le lisent et l'\xC3\xA9" "crivent"
                           : std::string{};
    const HmiType* owner = nullptr;
    if (const HmiFunction* f = effectiveMethod(p, fullName(*ty), member, &owner); f && owner && f->visibility == Visibility::Private)
        return f->name + " est une m\xC3\xA9thode priv\xC3\xA9" "e de " + owner->name + " : seules ses m\xC3\xA9thodes l'appellent";
    return {};
}
} // namespace

std::string privateMemberUse(const Project& p, std::string_view typeName, std::string_view member) {
    return privateUse(p, typeName, member);
}

std::vector<PathProblem> pathProblems(const Project& p, std::string_view source) {
    std::vector<PathProblem> out;
    if (p.programs.variables.empty()) return out;
    // 1.11.1 (REP) : un script (ou une expression) se lit sans les $ de ses reperes ;
    // les lignes ne bougent pas.
    const std::string stripped = markers::strip(source);
    const std::string_view code = stripped;
    const auto isStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    const auto isPart = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const std::size_t n = code.size();
    int line = 1;
    std::size_t i = 0;
    char prev = ' ';          // le dernier caractere significatif : '.' devant un nom = un membre
    while (i < n) {
        const char c = code[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '(' && i + 1 < n && code[i + 1] == '*') {            // (* commentaire *)
            i += 2;
            while (i + 1 < n && !(code[i] == '*' && code[i + 1] == ')')) { if (code[i] == '\n') ++line; ++i; }
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {             // // commentaire
            while (i < n && code[i] != '\n') ++i;
            continue;
        }
        if (c == '\'' || c == '"') {                                   // une chaine
            ++i;
            while (i < n && code[i] != c) { if (code[i] == '\n') ++line; ++i; }
            ++i;
            prev = c;
            continue;
        }
        if (!isStart(c)) {
            if (!std::isspace(static_cast<unsigned char>(c))) prev = c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && isPart(code[j])) ++j;
        const std::string_view name = code.substr(i, j - i);
        const bool root = prev != '.' && prev != '#';
        prev = 'a';
        i = j;
        const Variable* v = root ? p.variable(name) : nullptr;
        if (!v || !isComposite(v->type)) continue;
        // La chaine qui suit : [..], .membre, .Propriete
        std::string type = normalized(v->type);
        std::string path(name);
        const int at = line;
        while (true) {
            // 1.12.14 : un objet (une LIST, une MAP, un tuple, a toute profondeur) : ses indices et ses
            // membres (L[0], M['cle'][2].Niveau, T.Item1, L.Count) se verifient ici aussi - ils
            // passaient en marche seulement.
            if (isRich(type)) {
                std::size_t k = i;
                while (k < n && (code[k] == ' ' || code[k] == '\t')) ++k;
                if (k < n && code[k] == '[') {
                    int depth = 0;
                    std::size_t e = k;
                    for (; e < n; ++e) {
                        if (code[e] == '[') ++depth;
                        else if (code[e] == ']' && --depth == 0) break;
                        else if (code[e] == '\n') break;
                    }
                    if (e >= n || code[e] != ']') break;
                    const std::string inside(code.substr(k + 1, e - k - 1));
                    const auto st = stepIndex(p, type);
                    i = e + 1;
                    if (!st.why.empty()) out.push_back({at, path + " " + st.why});
                    if (st.type.empty()) break;
                    path += "[" + inside + "]";
                    type = st.type;
                    continue;
                }
                if (k < n && code[k] == '.' && k + 1 < n && isStart(code[k + 1])) {
                    std::size_t e = k + 1;
                    while (e < n && isPart(code[e])) ++e;
                    const std::string member(code.substr(k + 1, e - k - 1));
                    i = e;
                    if (const std::string why = privateUse(p, type, member); !why.empty()) {   // 1.13.0 (lot J1)
                        out.push_back({at, path + "." + member + " : " + why});
                        break;
                    }
                    const auto st = stepMember(p, type, member);
                    if (!st.why.empty()) {
                        out.push_back({at, path + " " + st.why});
                        break;
                    }
                    if (st.property) {
                        std::size_t q = e;
                        while (q < n && (code[q] == ' ' || code[q] == '\t')) ++q;
                        if (q + 1 < n && code[q] == ':' && code[q + 1] == '=')
                            out.push_back({at, path + "." + member + " : une propri\xC3\xA9t\xC3\xA9 se lit seulement"});
                        break;
                    }
                    if (st.type.empty()) break;
                    path += "." + member;
                    type = st.type;
                    continue;
                }
                break;
            }
            std::size_t k = i;
            while (k < n && (code[k] == ' ' || code[k] == '\t')) ++k;
            if (k < n && code[k] == '[') {
                int depth = 0;
                std::size_t e = k;
                for (; e < n; ++e) {
                    if (code[e] == '[') ++depth;
                    else if (code[e] == ']' && --depth == 0) break;
                    else if (code[e] == '\n') break;
                }
                if (e >= n || code[e] != ']') break;
                const std::string inside(code.substr(k + 1, e - k - 1));
                Spec s;
                if (!parseSpec(type, s) || !s.array()) {
                    out.push_back({at, path + " n'est pas un tableau : pas d'indice entre crochets"});
                    i = e + 1;
                    break;
                }
                const auto parts = splitList(inside);
                if (static_cast<int>(parts.size()) != s.dims) {
                    out.push_back({at, path + " a " + std::to_string(s.dims) + " dimension" + (s.dims > 1 ? "s" : "") + " : " + std::to_string(s.dims)
                                           + " indice" + (s.dims > 1 ? "s" : "") + " entre crochets (" + specText(s) + ")"});
                } else {
                    for (int d = 0; d < s.dims; ++d) {
                        long long x = 0;
                        if (!integerOf(parts[static_cast<std::size_t>(d)], x)) continue;
                        if (x < s.low[d] || x > s.high[d])
                            out.push_back({at, path + "[" + inside + "] : l'indice " + std::to_string(x) + " est hors des bornes du tableau ("
                                                   + std::to_string(s.low[d]) + ".." + std::to_string(s.high[d]) + ")"});
                    }
                }
                path += "[" + inside + "]";
                type = s.element;
                i = e + 1;
                continue;
            }
            if (k < n && code[k] == '.' && k + 1 < n && (isStart(code[k + 1]) || std::isdigit(static_cast<unsigned char>(code[k + 1])))) {
                std::size_t e = k + 1;
                while (e < n && isPart(code[e])) ++e;
                const std::string member(code.substr(k + 1, e - k - 1));
                i = e;
                Spec s;
                if (!parseSpec(type, s)) break;
                const auto written = [&] {
                    std::size_t q = e;
                    while (q < n && (code[q] == ' ' || code[q] == '\t')) ++q;
                    return q + 1 < n && code[q] == ':' && code[q + 1] == '=';
                };
                if (s.array()) {
                    const PropertyInfo* pi = property(member);
                    if (!pi) out.push_back({at, path + " est un tableau : ." + member + " n'existe pas (Length, Low, High... ou [i] pour une case)"});
                    else if (pi->twoD && s.dims != 2) out.push_back({at, path + "." + member + " : pour un tableau \xC3\xA0 deux dimensions"});
                    else if (written()) out.push_back({at, path + "." + member + " : une propri\xC3\xA9t\xC3\xA9 se lit seulement"});
                    break;
                }
                if (isElementary(s.element)) {
                    if (!std::isdigit(static_cast<unsigned char>(member[0])))
                        out.push_back({at, path + " est un " + s.element + " : pas de membre ." + member});
                    break;
                }
                const auto* ty = p.hmiTypeByName(s.element);
                if (!ty) break;
                // 1.13.0 (lot J1) : les membres herites comptent (la tranche du parent d'abord, comme membersOf, le
                // Walker et le moteur) ; un membre ou une methode prive ne se lit pas d'ici (seulement de ses methodes).
                if (const std::string why = privateUse(p, s.element, member); !why.empty()) {
                    out.push_back({at, path + "." + member + " : " + why});
                    break;
                }
                const auto all = membersOf(p, s.element);
                const TypeMember* found = nullptr;
                for (const auto& m : all)
                    if (!found && sameText(m.name, member)) found = &m;
                if (found) {
                    path += "." + found->name;
                    type = normalized(found->type);
                    continue;
                }
                const PropertyInfo* pi = property(member);
                if (pi && !pi->arrayOnly) {
                    if (written()) out.push_back({at, path + "." + member + " : une propri\xC3\xA9t\xC3\xA9 se lit seulement"});
                    break;
                }
                // 1.13.0 (lot J1) : une methode du type (ou heritee) : Pompe1.Demarrer(), Pompe2.EnDefaut() - son appel.
                if (effectiveMethod(p, s.element, member)) break;
                std::string list;
                for (const auto& m : all) list += (list.empty() ? "" : ", ") + m.name;
                out.push_back({at, path + " (" + ty->name + ") n'a pas de membre " + member + (list.empty() ? std::string{} : " (membres : " + list + ")")});
                break;
            }
            break;
        }
    }
    return out;
}

// ------------------------------------------------------ les dossiers --------
std::vector<std::string> folderChain(std::string_view folder) {
    std::vector<std::string> out;
    const std::string f = trimmed(folder);
    if (f.empty()) return out;
    std::size_t from = 0;
    while (true) {
        const auto slash = f.find('/', from);
        out.push_back(f.substr(0, slash));
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return out;
}

std::string folderLeaf(std::string_view folder) {
    const auto slash = folder.rfind('/');
    return std::string(slash == std::string_view::npos ? folder : folder.substr(slash + 1));
}

std::string folderParent(std::string_view folder) {
    const auto slash = folder.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(folder.substr(0, slash));
}

bool validFolder(std::string_view folder, std::string* why) {
    const std::string f = trimmed(folder);
    if (f.empty()) return fail(why, "nom de dossier vide");
    std::size_t from = 0;
    while (true) {
        const auto slash = f.find('/', from);
        const std::string part = trimmed(std::string_view(f).substr(from, slash == std::string::npos ? std::string::npos : slash - from));
        if (part.empty()) return fail(why, "dossier \xC2\xAB " + f + " \xC2\xBB : un nom vide entre deux /");
        if (part.find('"') != std::string::npos || part.find('\\') != std::string::npos)
            return fail(why, "dossier \xC2\xAB " + part + " \xC2\xBB : ni guillemet ni \\");
        if (part.size() > 60) return fail(why, "dossier \xC2\xAB " + part + " \xC2\xBB : 60 caract\xC3\xA8res au plus");
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return true;
}

std::vector<std::string> allFolders(const Project& p) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    const auto add = [&](std::string_view f) {
        for (const auto& c : folderChain(f))
            if (seen.insert(upperOf(c)).second) out.push_back(c);
    };
    for (const auto& f : p.programs.folders) add(f);
    for (const auto& v : p.programs.variables) add(v.folder);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return upperOf(a) < upperOf(b); });
    return out;
}

} // namespace hmi::types
