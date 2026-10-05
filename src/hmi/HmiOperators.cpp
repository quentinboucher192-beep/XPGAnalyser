// =============================================================================
//  hmi/HmiOperators.cpp - les operateurs des symboles et des types IHM (1.10)
// =============================================================================
#include "HmiOperators.hpp"

#include "HmiEnums.hpp"      // 1.10 (decision 15) : toString / fromString des enumerations
#include "HmiExprCheck.hpp"  // 1.10.1 (U2) : le nom le plus proche (veux-tu dire ?)
#include "HmiPublicVars.hpp" // 1.10.1 (U2) : les proprietes d'une instance de symbole
#include "HmiScript.hpp"
#include "HmiSymbols.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>

namespace hmi {

namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmedOf(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

struct OpInfo {
    std::string_view op, iec, label;
    OperatorFamily   family;
};
constexpr OpInfo kOps[] = {
    {"+", "ADD", "addition", OperatorFamily::Arithmetic},
    {"-", "SUB", "soustraction", OperatorFamily::Arithmetic},
    {"*", "MUL", "multiplication", OperatorFamily::Arithmetic},
    {"/", "DIV", "division", OperatorFamily::Arithmetic},
    {"+=", "ADD_ASSIGN", "ajouter \xC3\xA0", OperatorFamily::Assignment},
    {"-=", "SUB_ASSIGN", "retrancher de", OperatorFamily::Assignment},
    {"*=", "MUL_ASSIGN", "multiplier par", OperatorFamily::Assignment},
    {"/=", "DIV_ASSIGN", "diviser par", OperatorFamily::Assignment},
    {"=", "EQ", "\xC3\xA9galit\xC3\xA9", OperatorFamily::Comparison},
    {"<>", "NE", "diff\xC3\xA9rence", OperatorFamily::Comparison},
    {"<", "LT", "plus petit", OperatorFamily::Comparison},
    {">", "GT", "plus grand", OperatorFamily::Comparison},
    {"<=", "LE", "plus petit ou \xC3\xA9gal", OperatorFamily::Comparison},
    {">=", "GE", "plus grand ou \xC3\xA9gal", OperatorFamily::Comparison},
};
const OpInfo* infoOf(std::string_view op) noexcept {
    for (const auto& i : kOps) if (i.op == op) return &i;
    return nullptr;
}

// Les types de base qu'un operateur peut prendre ou rendre.
constexpr std::string_view kBaseTypes[] = {"BOOL", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT",
                                           "BYTE", "WORD", "DWORD", "LWORD", "REAL", "LREAL", "TIME", "STRING"};
bool isBaseType(std::string_view t) noexcept {
    const std::string u = upperOf(trimmedOf(t));
    for (const auto b : kBaseTypes) if (u == b) return true;
    return false;
}
// Le rang d'un nombre (0 : pas un nombre) : entiers 1..4 par largeur, reels 10, 11.
int numericRank(std::string_view t) noexcept {
    const std::string u = upperOf(trimmedOf(t));
    if (u == "SINT" || u == "USINT" || u == "BYTE") return 1;
    if (u == "INT" || u == "UINT" || u == "WORD") return 2;
    if (u == "DINT" || u == "UDINT" || u == "DWORD") return 3;
    if (u == "LINT" || u == "ULINT" || u == "LWORD") return 4;
    if (u == "REAL") return 10;
    if (u == "LREAL") return 11;
    return 0;
}
// Le cout pour passer un `given` la ou on attend `wanted` (-1 : impossible) :
// 0 exact ; 1 un nombre de la meme famille, plus large (ou un reel l'un pour
// l'autre : un litteral 1.5 peut arriver en LREAL) ; 2 un entier vers un reel.
int matchCost(std::string_view wanted, std::string_view given) noexcept {
    if (sameTypeName(wanted, given)) return 0;
    const int w = numericRank(wanted), g = numericRank(given);
    if (w == 0 || g == 0) return -1;
    if (w >= 10 && g >= 10) return 1;
    if (w < 10 && g < 10) return g <= w ? 1 : -1;
    if (w >= 10 && g < 10) return 2;
    return -1;
}

std::string displayType(std::string_view t) {
    const std::string s = trimmedOf(t);
    return isBaseType(s) ? upperOf(s) : s;
}

// Une petite empreinte stable du texte (FNV-1a 32 bits), en hexadecimal.
std::string fingerprint(std::string_view text) {
    std::uint32_t h = 2166136261u;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    char b[16];
    std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(h));
    return b;
}

// Les noms qu'un exemple peut lire : les membres numeriques d'un type IHM, ou les
// parametres d'un symbole.
struct Members {
    std::vector<std::string> numeric;   // INT, REAL... (au plus 4)
    std::vector<std::string> all;       // tous (au plus 6)
    std::string              firstText; // le premier membre STRING
};
Members membersFor(const Project& p, std::string_view typeName) {
    Members m;
    if (const auto* t = p.hmiTypeByName(typeName)) {
        for (const auto& mem : t->members) {
            if (m.all.size() < 6) m.all.push_back(mem.name);
            if (numericRank(mem.type) != 0 && m.numeric.size() < 4) m.numeric.push_back(mem.name);
            if (m.firstText.empty() && sameTypeName(mem.type, "STRING")) m.firstText = mem.name;
        }
        return m;
    }
    if (const auto* v = p.viewByName(typeName); v && isSymbolView(*v)) {
        for (const auto& prm : v->params) {
            if (m.all.size() < 6) m.all.push_back(prm.name);
            if (numericRank(prm.type) != 0 && m.numeric.size() < 4) m.numeric.push_back(prm.name);
            if (m.firstText.empty() && sameTypeName(prm.type, "STRING")) m.firstText = prm.name;
        }
    }
    return m;
}

bool isOwnerName(const OperatorOwner& o, std::string_view t) { return sameTypeName(o.name, t); }

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le code sans ses commentaires ni ses chaines (remplaces par des blancs : les
// lignes restent). `keepComments` : seules les chaines sont blanchies (une
// apostrophe dans un commentaire n'ouvre pas de chaine).
std::string codeOnly(std::string_view s, bool keepComments = false) {
    std::string out(s);
    std::size_t i = 0;
    const auto blank = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to && k < out.size(); ++k) if (out[k] != '\n') out[k] = ' ';
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < s.size() && s[k] != c) k += s[k] == '$' ? 2 : 1;
            blank(i, k + 1);
            i = k + 1;
        } else if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end + 2;
            if (!keepComments) blank(i, stop);
            i = stop;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end;
            if (!keepComments) blank(i, stop);
            i = stop;
        } else {
            ++i;
        }
    }
    return out;
}

// Le mot `from` (sans casse, un nom entier, pas un membre x.from) devient `to`,
// dans le code ET dans les commentaires (la signature en tete du script), pas
// dans les chaines.
std::string renameWord(std::string_view s, std::string_view from, std::string_view to, bool inComment = false) {
    std::string out;
    out.reserve(s.size() + 8);
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        // Un commentaire : ses mots aussi (la signature en tete), ses apostrophes
        // ne sont pas des chaines ("l'objet").
        if (!inComment && ((c == '(' && i + 1 < s.size() && s[i + 1] == '*') || (c == '/' && i + 1 < s.size() && s[i + 1] == '/'))) {
            const bool block = c == '(';
            const auto end = block ? s.find("*)", i + 2) : s.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end + (block ? 2 : 0);
            out += renameWord(s.substr(i, stop - i), from, to, true);
            i = stop;
            continue;
        }
        if (!inComment && (c == '\'' || c == '"')) {
            std::size_t k = i + 1;
            while (k < s.size() && s[k] != c) k += s[k] == '$' ? 2 : 1;
            k = std::min(k + 1, s.size());
            out.append(s.substr(i, k - i));
            i = k;
            continue;
        }
        if (!identStart(c) || (i > 0 && (identPart(s[i - 1]) || s[i - 1] == '.' || s[i - 1] == '#'))) {
            out += c;
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < s.size() && identPart(s[i])) ++i;
        const auto word = s.substr(start, i - start);
        if (sameTypeName(word, from)) out.append(to);
        else out.append(word);
    }
    return out;
}

// Le nom `from` NON SUIVI d'une parenthese (pas un appel), hors commentaires et
// chaines, devient `to` : Resultat := ..., Resultat.x := ..., TO_REAL := ...
// `comments` : aussi dans les commentaires ("le resultat : TO_REAL := ...").
std::string renameBareWord(std::string_view s, std::string_view from, std::string_view to, bool comments = false) {
    const std::string code = codeOnly(s, comments);
    std::string out;
    out.reserve(s.size() + 16);
    std::size_t i = 0;
    while (i < s.size()) {
        if (code[i] == ' ' && s[i] != ' ') { out += s[i++]; continue; }   // commentaire ou chaine
        if (!identStart(s[i]) || (i > 0 && (identPart(s[i - 1]) || s[i - 1] == '.' || s[i - 1] == '#'))) {
            out += s[i++];
            continue;
        }
        const std::size_t start = i;
        while (i < s.size() && identPart(s[i])) ++i;
        const auto word = s.substr(start, i - start);
        std::size_t k = i;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        const bool call = k < s.size() && s[k] == '(';
        if (!call && sameTypeName(word, from)) out.append(to);
        else out.append(word);
    }
    return out;
}

// La cle d'unicite : le nom de fonction, les deux operandes.
std::string uniqueKey(const HmiOperator& o) {
    return upperOf(operatorFunctionName(o)) + "|" + upperOf(trimmedOf(o.left)) + "|" + upperOf(trimmedOf(o.right));
}

} // namespace

// ------------------------------------------------------------ les genres ---
OperatorFamily operatorFamily(std::string_view op) noexcept {
    if (op == kConversionOp) return OperatorFamily::Conversion;
    if (const auto* i = infoOf(op)) return i->family;
    return OperatorFamily::Unknown;
}

std::string_view operatorIecName(std::string_view op) noexcept {
    const auto* i = infoOf(op);
    return i ? i->iec : std::string_view{};
}

std::string_view operatorLabel(std::string_view op) noexcept {
    if (op == kConversionOp) return "conversion";
    const auto* i = infoOf(op);
    return i ? i->label : std::string_view{};
}

std::string operatorFunctionName(const HmiOperator& o) {
    if (o.op == kConversionOp) return "TO_" + displayType(o.result);
    return std::string(operatorIecName(o.op));
}

std::string operatorSignature(const HmiOperator& o) {
    switch (operatorFamily(o.op)) {
        case OperatorFamily::Conversion:
            return operatorFunctionName(o) + "(" + displayType(o.left) + ") : " + displayType(o.result);
        case OperatorFamily::Assignment:
            return displayType(o.left) + " " + o.op + " " + displayType(o.right);
        case OperatorFamily::Arithmetic:
        case OperatorFamily::Comparison:
            return displayType(o.left) + " " + o.op + " " + displayType(o.right)
                 + (o.result.empty() ? std::string{} : " : " + displayType(o.result));
        case OperatorFamily::Unknown: break;
    }
    return "? " + o.op;
}

std::string_view operatorKindLabel(std::string_view op) noexcept {
    switch (operatorFamily(op)) {
        case OperatorFamily::Conversion: return "conversion";
        case OperatorFamily::Arithmetic: return "arithm\xC3\xA9tique";
        case OperatorFamily::Assignment: return "compos\xC3\xA9";
        case OperatorFamily::Comparison: return "comparaison";
        case OperatorFamily::Unknown: break;
    }
    return "?";
}

namespace {
std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
// Le nom C d'un operateur (comme la maquette) : t_vecteur_ajouter, t_vecteur_ajouter_a...
std::string_view cVerb(std::string_view op) noexcept {
    if (op == "+") return "ajouter";
    if (op == "-") return "soustraire";
    if (op == "*") return "multiplier";
    if (op == "/") return "diviser";
    if (op == "+=") return "ajouter_a";
    if (op == "-=") return "soustraire_a";
    if (op == "*=") return "multiplier_a";
    if (op == "/=") return "diviser_a";
    if (op == "=") return "egal";
    if (op == "<>") return "different";
    if (op == "<") return "inferieur";
    if (op == ">") return "superieur";
    if (op == "<=") return "inferieur_ou_egal";
    if (op == ">=") return "superieur_ou_egal";
    return "op";
}
} // namespace

std::string operatorCppSignature(const HmiOperator& o, std::string_view owner) {
    const std::string L = displayType(o.left), R = displayType(o.right), T = displayType(o.result);
    const auto param = [](const std::string& t, const char* n) { return isBaseType(t) ? t + " " + n : "const " + t + "& " + n; };
    switch (operatorFamily(o.op)) {
        case OperatorFamily::Conversion:
            if (sameTypeName(T, owner) && !sameTypeName(L, owner)) return "explicit " + T + "::" + T + "(" + param(L, "a") + ")";
            if (isBaseType(T)) return "explicit " + L + "::operator " + T + "() const";
            return T + " to_" + lowerOf(T) + "(" + param(L, "a") + ")";
        case OperatorFamily::Assignment:
            return L + "& operator" + o.op + "(" + L + "& a, " + param(R, "b") + ")";
        case OperatorFamily::Comparison:
            return "bool operator" + (o.op == "=" ? std::string("==") : o.op == "<>" ? std::string("!=") : o.op) + "(" + param(L, "a")
                 + ", " + param(R, "b") + ")";
        case OperatorFamily::Arithmetic:
            return T + " operator" + o.op + "(" + param(L, "a") + ", " + param(R, "b") + ")";
        case OperatorFamily::Unknown: break;
    }
    return {};
}

std::string operatorCSignature(const HmiOperator& o, std::string_view owner) {
    const std::string L = displayType(o.left), R = displayType(o.right), T = displayType(o.result);
    const std::string prefix = lowerOf(trimmedOf(owner)) + "_";
    switch (operatorFamily(o.op)) {
        case OperatorFamily::Conversion:
            if (sameTypeName(T, owner) && !sameTypeName(L, owner)) return T + " " + prefix + "depuis_" + lowerOf(L) + "(" + L + " a)";
            return T + " " + prefix + "to_" + lowerOf(T) + "(const " + L + " *a)";
        case OperatorFamily::Assignment:
            return "void " + prefix + std::string(cVerb(o.op)) + "(" + L + " *a, " + R + " b)";
        case OperatorFamily::Comparison:
            return "int " + prefix + std::string(cVerb(o.op)) + "(" + L + " a, " + R + " b)";
        case OperatorFamily::Arithmetic:
            return T + " " + prefix + std::string(cVerb(o.op)) + "(" + L + " a, " + R + " b)";
        case OperatorFamily::Unknown: break;
    }
    return {};
}

std::string_view operatorRole(const Project& p, const OperatorOwner& owner, const HmiOperator& o) {
    if (owner.kind != OperatorOwner::Kind::Type) return {};
    const auto* t = p.hmiType(owner.id);
    return t && isEnumeration(*t) ? enumConversionRole(*t, o) : std::string_view{};
}

bool sameTypeName(std::string_view a, std::string_view b) noexcept {
    const std::string x = trimmedOf(a), y = trimmedOf(b);
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(x[i])) != std::toupper(static_cast<unsigned char>(y[i]))) return false;
    return true;
}

// ----------------------------------------------------------- les porteurs ---
std::string OperatorOwner::label() const {
    switch (kind) {
        case Kind::Symbol: return "symbole " + name;
        case Kind::Type:   return "type " + name;
        case Kind::None:   break;
    }
    return name;
}

OperatorOwner ownerOfView(const View& v) {
    OperatorOwner o;
    if (!isSymbolView(v)) return o;
    o.kind = OperatorOwner::Kind::Symbol;
    o.id = v.id;
    o.name = v.name;
    return o;
}

OperatorOwner ownerOfType(const HmiType& t) {
    OperatorOwner o;
    o.kind = OperatorOwner::Kind::Type;
    o.id = t.id;
    o.name = t.name;
    return o;
}

OperatorOwner ownerByName(const Project& p, std::string_view name) {
    const std::string n = trimmedOf(name);
    if (n.empty()) return {};
    if (const auto* t = p.hmiTypeByName(n)) return ownerOfType(*t);
    for (const auto& v : p.views)
        if (isSymbolView(v) && sameTypeName(v.name, n)) return ownerOfView(v);
    return {};
}

const std::vector<HmiOperator>* operatorsOf(const Project& p, const OperatorOwner& o) {
    switch (o.kind) {
        case OperatorOwner::Kind::Type:
            if (const auto* t = p.hmiType(o.id)) return &t->operators;
            break;
        case OperatorOwner::Kind::Symbol:
            if (const auto* v = p.view(o.id)) return &v->operators;
            break;
        case OperatorOwner::Kind::None: break;
    }
    return nullptr;
}

std::vector<HmiOperator>* operatorsOf(Project& p, const OperatorOwner& o) {
    switch (o.kind) {
        case OperatorOwner::Kind::Type:
            if (auto* t = p.hmiType(o.id)) return &t->operators;
            break;
        case OperatorOwner::Kind::Symbol:
            if (auto* v = p.view(o.id)) return &v->operators;
            break;
        case OperatorOwner::Kind::None: break;
    }
    return nullptr;
}

const std::vector<HmiOperator>* operatorsOf(const Project& p, std::string_view ownerName) {
    return operatorsOf(p, ownerByName(p, ownerName));
}

std::vector<OwnedOperator> allOperators(const Project& p) {
    std::vector<OwnedOperator> out;
    for (const auto& t : p.programs.types)
        for (const auto& o : t.operators) out.push_back({ownerOfType(t), &o});
    for (const auto& v : p.views)
        if (isSymbolView(v))
            for (const auto& o : v.operators) out.push_back({ownerOfView(v), &o});
    return out;
}

const HmiOperator* operatorById(const Project& p, Id id, OperatorOwner* owner) {
    if (id == kNoId) return nullptr;
    for (const auto& t : p.programs.types)
        for (const auto& o : t.operators)
            if (o.id == id) {
                if (owner) *owner = ownerOfType(t);
                return &o;
            }
    for (const auto& v : p.views)
        for (const auto& o : v.operators)
            if (o.id == id) {
                if (owner) *owner = ownerOfView(v);
                return &o;
            }
    return nullptr;
}

HmiOperator* operatorById(Project& p, Id id, OperatorOwner* owner) {
    return const_cast<HmiOperator*>(operatorById(static_cast<const Project&>(p), id, owner));
}

// ------------------------------------------------------------- l'emploi ----
const HmiOperator* findOperator(const Project& p, std::string_view op, std::string_view left, std::string_view right,
                                OperatorOwner* owner) {
    const auto fam = operatorFamily(op);
    if (fam == OperatorFamily::Unknown || fam == OperatorFamily::Conversion) return nullptr;
    const HmiOperator* best = nullptr;
    int bestCost = 1 << 20;
    OperatorOwner bestOwner;
    const auto scan = [&](std::string_view typeName) {
        const OperatorOwner o = ownerByName(p, typeName);
        const auto* list = operatorsOf(p, o);
        if (!list) return;
        for (const auto& x : *list) {
            if (x.op != op) continue;
            // La gauche d'un += est modifiee : elle doit etre exactement de son type.
            const int l = fam == OperatorFamily::Assignment ? (sameTypeName(x.left, left) ? 0 : -1) : matchCost(x.left, left);
            const int r = matchCost(x.right, right);
            if (l < 0 || r < 0) continue;
            if (l + r < bestCost) {
                bestCost = l + r;
                best = &x;
                bestOwner = o;
            }
        }
    };
    scan(left);
    if (!sameTypeName(left, right)) scan(right);
    if (best && owner) *owner = bestOwner;
    return best;
}

const HmiOperator* findConversion(const Project& p, std::string_view function, std::string_view source,
                                  OperatorOwner* owner) {
    const std::string fn = upperOf(trimmedOf(function));
    if (fn.size() <= 3 || fn.compare(0, 3, "TO_") != 0) return nullptr;
    const std::string target = fn.substr(3);
    const HmiOperator* best = nullptr;
    int bestCost = 1 << 20;
    OperatorOwner bestOwner;
    const auto scan = [&](std::string_view typeName) {
        const OperatorOwner o = ownerByName(p, typeName);
        const auto* list = operatorsOf(p, o);
        if (!list) return;
        for (const auto& x : *list) {
            if (x.op != kConversionOp || upperOf(trimmedOf(x.result)) != target) continue;
            const int c = matchCost(x.left, source);
            if (c >= 0 && c < bestCost) {
                bestCost = c;
                best = &x;
                bestOwner = o;
            }
        }
    };
    scan(source);
    if (!sameTypeName(source, target)) scan(target);
    if (best && owner) *owner = bestOwner;
    return best;
}

std::string operatorFunctionText(const HmiOperator& o) {
    const auto fam = operatorFamily(o.op);
    // Un nom interne : le resultat se donne par Resultat := ... (ou TO_REAL := ...,
    // le nom de la conversion), tous deux renommes ; un APPEL TO_STRING(x) dans le
    // corps reste un appel (la conversion standard, ou celle d'un autre type).
    const std::string internal = "OPERATEUR_" + upperOf(operatorFunctionName(o));
    std::string head = "FUNCTION " + internal + "(";
    if (fam == OperatorFamily::Assignment) head += "VAR_IN_OUT ";
    head += "a : " + displayType(o.left);
    if (fam != OperatorFamily::Conversion) head += "; b : " + displayType(o.right);
    head += ")";
    if (fam != OperatorFamily::Assignment && !o.result.empty()) head += " : " + displayType(o.result);
    std::string body = renameBareWord(o.body, kResultName, internal);
    if (const auto fn = operatorFunctionName(o); !fn.empty()) body = renameBareWord(body, fn, internal);
    if (!body.empty() && body.back() != '\n') body += '\n';
    return head + "\n" + body + "END_FUNCTION\n";
}

bool resolveOperator(const Project& p, std::string_view op, std::string_view left, std::string_view right,
                     OperatorCall& out) {
    OperatorOwner owner;
    const HmiOperator* found = nullptr;
    const std::string u = upperOf(trimmedOf(op));
    if (u.size() > 3 && u.compare(0, 3, "TO_") == 0) found = findConversion(p, u, left, &owner);
    else found = findOperator(p, op, left, right, &owner);
    if (!found) return false;
    out.function = operatorFunctionText(*found);
    out.key = upperOf(owner.name) + "|" + found->op + "|" + upperOf(found->left) + "|" + upperOf(found->right) + "|"
            + upperOf(found->result) + "|" + fingerprint(out.function);
    out.owner = owner.label() + ", " + (found->op == kConversionOp ? "conversion " + operatorFunctionName(*found)
                                                                    : "op\xC3\xA9rateur " + found->op);
    return true;
}

bool isConversion(const Project& p, std::string_view name) {
    const std::string u = upperOf(trimmedOf(name));
    if (u.size() <= 3 || u.compare(0, 3, "TO_") != 0) return false;
    if (isBaseType(u.substr(3))) return true;
    for (const auto& x : allOperators(p))
        if (x.op->op == kConversionOp && upperOf(operatorFunctionName(*x.op)) == u) return true;
    return false;
}

std::string operandTypeOf(const Project& p, const View* view, std::string_view path) {
    const std::string s = trimmedOf(path);
    if (s.empty()) return {};
    // Une variable IHM (un membre, une case : le type de ce qu'il designe).
    if (auto t = types::typeOfPath(p, s); !t.empty()) return t;
    // Une instance : "Pompe_1" dans la vue, ou "Vue.Pompe_1".
    const auto instanceIn = [&](const View& v, std::string_view name) -> std::string {
        if (const auto* o = v.objectByName(name); o && o->kind == Kind::SymbolInstance)
            if (const auto* sym = symbolOf(p, *o)) return sym->name;
        return {};
    };
    if (view) if (auto t = instanceIn(*view, s); !t.empty()) return t;
    if (const auto dot = s.find('.'); dot != std::string::npos)
        if (const auto* v = p.viewByName(std::string_view(s).substr(0, dot)))
            return instanceIn(*v, std::string_view(s).substr(dot + 1));
    return {};
}

// ------------------------------------------ 1.10.1 (U2) : a, b, Resultat ---
std::vector<OperatorName> operatorNames(const HmiOperator& o, std::string_view owner) {
    std::vector<OperatorName> out;
    const auto fam = operatorFamily(o.op);
    if (fam == OperatorFamily::Unknown) return out;
    const std::string L = displayType(o.left), R = displayType(o.right), T = displayType(o.result);
    const std::string where = " (\xC3\xA0 gauche de " + o.op + ")";
    // L'objet : le porteur (s'il est a gauche, c'est a ; sinon b).
    const bool ownerLeft = owner.empty() || sameTypeName(owner, o.left);
    const bool ownerRight = !ownerLeft && !owner.empty() && sameTypeName(owner, o.right);
    const auto object = [](const std::string& t) { return "l'objet de type " + t; };
    const auto other = [](const std::string& t) { return "l'autre op\xC3\xA9rande, " + t; };
    switch (fam) {
        case OperatorFamily::Conversion:
            out.push_back({"a", L, "l'objet converti, " + L});
            out.push_back({std::string(kResultName), T, "le r\xC3\xA9sultat, " + T});
            out.push_back({operatorFunctionName(o), T, "le r\xC3\xA9sultat sous le nom de la conversion, " + T});
            break;
        case OperatorFamily::Assignment:
            out.push_back({"a", L, object(L) + ", modifi\xC3\xA9 en place" + where});
            out.push_back({"b", R, other(R) + " (\xC3\xA0 droite)"});
            break;
        case OperatorFamily::Arithmetic:
        case OperatorFamily::Comparison:
            out.push_back({"a", L, (ownerRight ? other(L) : object(L)) + where});
            out.push_back({"b", R, ownerRight ? object(R) + " (\xC3\xA0 droite)" : other(R) + " (\xC3\xA0 droite)"});
            out.push_back({std::string(kResultName), T, fam == OperatorFamily::Comparison ? "le r\xC3\xA9sultat, " + T + " (TRUE ou FALSE)"
                                                                                          : "le r\xC3\xA9sultat, " + T});
            break;
        case OperatorFamily::Unknown: break;
    }
    return out;
}

std::string operatorLegend(const HmiOperator& o, std::string_view owner) {
    const auto fam = operatorFamily(o.op);
    const std::string L = displayType(o.left), R = displayType(o.right), T = displayType(o.result);
    const std::string dot = "  \xC2\xB7  ";
    const bool ownerRight = !owner.empty() && !sameTypeName(owner, o.left) && sameTypeName(owner, o.right);
    switch (fam) {
        case OperatorFamily::Conversion:
            return "a : l'objet converti, " + L + dot + "le r\xC3\xA9sultat (" + T + ") : " + operatorFunctionName(o)
                 + " := \xE2\x80\xA6 ou Resultat := \xE2\x80\xA6";
        case OperatorFamily::Assignment:
            return "a : l'objet de type " + L + ", modifi\xC3\xA9 en place (\xC3\xA0 gauche de " + o.op + ")" + dot + "b : l'autre op\xC3\xA9rande, "
                 + R + dot + "pas de r\xC3\xA9sultat : a est modifi\xC3\xA9";
        case OperatorFamily::Arithmetic:
        case OperatorFamily::Comparison:
            if (ownerRight)
                return "a : l'autre op\xC3\xA9rande, " + L + " (\xC3\xA0 gauche de " + o.op + ")" + dot + "b : l'objet de type " + R
                     + " (\xC3\xA0 droite)" + dot + "Resultat : " + T;
            return "a : l'objet de type " + L + " (\xC3\xA0 gauche de " + o.op + ")" + dot + "b : l'autre op\xC3\xA9rande, " + R + dot
                 + "Resultat : " + T;
        case OperatorFamily::Unknown: break;
    }
    return {};
}

std::string operatorExample(const Project& p, const HmiOperator& o) {
    const auto fam = operatorFamily(o.op);
    const std::string fn = operatorFunctionName(o);
    const std::string L = displayType(o.left), R = displayType(o.right), T = displayType(o.result);
    const Members ml = membersFor(p, L), mr = membersFor(p, R), mt = membersFor(p, T);
    const bool lStruct = !ml.all.empty(), rStruct = !mr.all.empty(), tStruct = !mt.all.empty();
    const auto first = [](const Members& m) { return !m.numeric.empty() ? m.numeric.front() : m.all.front(); };
    switch (fam) {
        case OperatorFamily::Conversion:
            if (tStruct) {
                const std::string& m = mt.all.front();
                std::string from = "a";
                if (lStruct) {
                    const bool common = std::any_of(ml.all.begin(), ml.all.end(), [&](const std::string& x) { return sameTypeName(x, m); });
                    from = "a." + (common ? m : ml.all.front());
                }
                return "Resultat." + m + " := " + from + ";";
            }
            if (sameTypeName(T, "STRING")) return fn + " := TO_STRING(" + (lStruct ? "a." + ml.all.front() : std::string("a")) + ");";
            return fn + " := " + (lStruct ? "a." + first(ml) : std::string("a")) + ";";
        case OperatorFamily::Assignment:
        case OperatorFamily::Arithmetic:
        case OperatorFamily::Comparison: {
            const std::string op1 = fam == OperatorFamily::Assignment ? o.op.substr(0, 1) : o.op;
            const Members& shape = lStruct ? ml : mr;
            const std::string m = shape.all.empty() ? std::string{} : first(shape);
            const auto side = [&](bool structured, const char* name) { return structured && !m.empty() ? std::string(name) + "." + m : std::string(name); };
            if (fam == OperatorFamily::Assignment) return side(lStruct, "a") + " := " + side(lStruct, "a") + " " + op1 + " " + side(rStruct, "b") + ";";
            if (fam == OperatorFamily::Comparison) return "Resultat := " + side(lStruct, "a") + " " + o.op + " " + side(rStruct, "b") + ";";
            return (tStruct && !m.empty() ? "Resultat." + m : std::string("Resultat")) + " := " + side(lStruct, "a") + " " + op1 + " "
                 + side(rStruct, "b") + ";";
        }
        case OperatorFamily::Unknown: break;
    }
    return {};
}

std::vector<OperandMember> operandMembers(const Project& p, std::string_view type) {
    std::vector<OperandMember> out;
    const std::string t = trimmedOf(type);
    if (t.empty() || isBaseType(t)) return out;
    if (const auto* ht = p.hmiTypeByName(t)) {
        for (const auto& m : ht->members) out.push_back({m.name, types::normalized(m.type), m.description, false});
        return out;
    }
    const View* sym = nullptr;
    for (const auto* v : symbolsOf(p))
        if (sameTypeName(v->name, t)) sym = v;
    if (!sym) return out;
    // Un symbole : ses parametres, puis ce que publie une instance (comme Pompe_3.).
    for (const auto& prm : sym->params)
        out.push_back({prm.name, prm.type.empty() ? std::string("ANY") : prm.type,
                       prm.description.empty() ? std::string("param\xC3\xA8tre du symbole") : prm.description, true});
    Object probe;
    for (const auto& [v, inst] : instancesOf(p, sym->name)) {
        (void)v;
        if (inst) { probe = *inst; break; }
    }
    if (probe.kind != Kind::SymbolInstance) probe = makeObject(Kind::SymbolInstance, kNoId, sym->name, 0, 0, kNoId);
    for (const auto& m : pub::objectMembers(probe)) {
        if (m.key == "params" || m.key == "symbol") continue;   // les arguments, le nom du symbole : deja dits
        if (std::any_of(out.begin(), out.end(), [&](const OperandMember& x) { return sameTypeName(x.name, m.name); })) continue;
        out.push_back({m.name, m.type, m.text.empty() ? std::string("propri\xC3\xA9t\xC3\xA9 de l'instance") : m.text, false});
    }
    return out;
}

// ------------------------------------------------------------- l'edition ---
std::string operatorTemplate(const Project& p, const HmiOperator& o) {
    const auto fam = operatorFamily(o.op);
    const std::string fn = operatorFunctionName(o);
    const std::string L = displayType(o.left), R = displayType(o.right), T = displayType(o.result);
    std::string s = "(* " + operatorSignature(o) + " *)\n";
    if (fam == OperatorFamily::Conversion)
        s += "(* a : l'objet converti ; le r\xC3\xA9sultat : " + fn + " := ... ou Resultat := ... *)\n";
    else if (fam == OperatorFamily::Assignment)
        s += "(* a \xC3\xA0 gauche (modifi\xC3\xA9), b \xC3\xA0 droite *)\n";
    else
        s += "(* a \xC3\xA0 gauche, b \xC3\xA0 droite ; le r\xC3\xA9sultat : Resultat *)\n";

    const Members ml = membersFor(p, L), mr = membersFor(p, R), mt = membersFor(p, T);
    const bool lStruct = !ml.all.empty(), rStruct = !mr.all.empty(), tStruct = !mt.all.empty();
    const std::string op1 = fam == OperatorFamily::Assignment ? o.op.substr(0, 1) : o.op;
    const auto side = [](bool structured, const char* name, const std::string& m) {
        return structured ? std::string(name) + "." + m : std::string(name);
    };
    std::string code;
    switch (fam) {
        case OperatorFamily::Conversion:
            if (tStruct) {
                // Vers une structure : ses membres, depuis a (le meme nom, sinon le meme rang).
                const auto& targets = lStruct || mt.numeric.empty() ? mt.all : mt.numeric;   // depuis un nombre : les membres numeriques
                for (std::size_t i = 0; i < targets.size(); ++i) {
                    const std::string& m = targets[i];
                    std::string from = "a";
                    if (lStruct) {
                        const bool common = std::any_of(ml.all.begin(), ml.all.end(), [&](const std::string& x) { return sameTypeName(x, m); });
                        from = "a." + (common ? m : ml.all[std::min(i, ml.all.size() - 1)]);
                    }
                    code += "Resultat." + m + " := " + from + ";\n";
                }
            } else if (sameTypeName(T, "STRING")) {
                std::string parts;
                for (const auto& m : ml.all) parts += (parts.empty() ? "" : ", ' ; ', ") + std::string("TO_STRING(a.") + m + ")";
                code += fn + " := " + (parts.empty() ? "'" + L + "'" : ml.all.size() == 1 ? parts : "CONCAT(" + parts + ")") + ";\n";
            } else if (!ml.numeric.empty()) {
                code += fn + " := a." + ml.numeric.front() + ";   (* \xC3\xA0 compl\xC3\xA9ter *)\n";
            } else {
                code += fn + " := " + (sameTypeName(T, "BOOL") ? std::string("FALSE") : std::string("0")) + ";   (* \xC3\xA0 compl\xC3\xA9ter *)\n";
            }
            break;
        case OperatorFamily::Arithmetic:
        case OperatorFamily::Assignment: {
            const std::string target = fam == OperatorFamily::Assignment ? std::string("a") : std::string("Resultat");
            const Members& shape = lStruct ? ml : mr;
            const bool memberwise = fam == OperatorFamily::Assignment ? lStruct : tStruct;
            if (!shape.numeric.empty() && memberwise) {
                for (const auto& m : shape.numeric)
                    code += target + "." + m + " := " + side(lStruct, "a", m) + " " + op1 + " " + side(rStruct, "b", m) + ";\n";
            } else if (!shape.numeric.empty()) {
                const std::string& m = shape.numeric.front();
                code += target + " := " + side(lStruct, "a", m) + " " + op1 + " " + side(rStruct, "b", m) + ";\n";
            } else {
                code += "(* \xC3\xA0 \xC3\xA9" "crire : " + target + " := ... ; *)\n";
            }
            break;
        }
        case OperatorFamily::Comparison: {
            const Members& shape = lStruct ? ml : mr;
            if (!shape.numeric.empty()) {
                std::string all;
                const std::string cmp = o.op == "<>" ? std::string("=") : o.op;
                for (const auto& m : shape.numeric)
                    all += (all.empty() ? "" : " AND ") + std::string("(") + side(lStruct, "a", m) + " " + cmp + " " + side(rStruct, "b", m) + ")";
                code += "Resultat := " + (o.op == "<>" ? "NOT (" + all + ")" : all) + ";\n";
            } else {
                code += "Resultat := FALSE;   (* \xC3\xA0 compl\xC3\xA9ter *)\n";
            }
            break;
        }
        case OperatorFamily::Unknown: break;
    }
    return s + code;
}

HmiOperator makeOperator(Project& p, std::string_view op, std::string_view left, std::string_view right,
                         std::string_view result) {
    HmiOperator o = draftOperator(p, op, left, right, result);
    o.id = p.allocate();
    return o;
}

HmiOperator draftOperator(const Project& p, std::string_view op, std::string_view left, std::string_view right,
                          std::string_view result) {
    HmiOperator o;
    o.op = std::string(op);
    o.left = displayType(left);
    const auto fam = operatorFamily(op);
    if (fam != OperatorFamily::Conversion) o.right = displayType(right);
    if (fam == OperatorFamily::Comparison) o.result = "BOOL";
    else if (fam != OperatorFamily::Assignment) o.result = displayType(result);
    o.body = operatorTemplate(p, o);
    // 1.10 (decision 15) : le toString et le fromString d'une enumeration, ceux de HmiEnums.
    if (fam == OperatorFamily::Conversion) {
        if (const auto* e = findEnumeration(p, o.left); e && sameTypeName(o.result, "STRING")) o.body = enumToStringOperator(*e).body;
        else if (const auto* f = findEnumeration(p, o.result); f && sameTypeName(o.left, "STRING")) o.body = enumFromStringOperator(*f).body;
    }
    return o;
}

std::string operatorProblem(const Project& p, const OperatorOwner& owner, const HmiOperator& o, Id ignore,
                            const std::function<bool(std::string_view)>& plcType) {
    const auto fam = operatorFamily(o.op);
    if (fam == OperatorFamily::Unknown) return "genre d'op\xC3\xA9rateur inconnu : " + o.op;
    const auto known = [&](std::string_view t, bool allowSymbol) {
        if (isBaseType(t) || p.hmiTypeByName(trimmedOf(t))) return true;
        if (allowSymbol) {
            const auto ow = ownerByName(p, t);
            if (ow.kind == OperatorOwner::Kind::Symbol) return true;
        }
        return plcType && plcType(trimmedOf(t));
    };
    const auto unknownType = [&](std::string_view t) {
        return "type inconnu : " + trimmedOf(t) + " (ni un type de base, ni un type IHM, ni un symbole, ni un DDT de l'automate)";
    };
    if (trimmedOf(o.left).empty()) return "l'op\xC3\xA9rande de gauche n'a pas de type";
    if (!known(o.left, true)) return unknownType(o.left);
    if (fam == OperatorFamily::Conversion) {
        if (trimmedOf(o.result).empty()) return "conversion sans type cible";
        if (!isIdentifier(trimmedOf(o.result))) return "type cible invalide : " + o.result;
        if (!known(o.result, false)) {
            if (ownerByName(p, o.result).kind == OperatorOwner::Kind::Symbol)
                return "une conversion ne peut pas viser un symbole (" + trimmedOf(o.result) + ") : vise un type";
            return "conversion vers un type inconnu : " + trimmedOf(o.result);
        }
        if (sameTypeName(o.left, o.result)) return "conversion vers le m\xC3\xAAme type (" + displayType(o.left) + ")";
        if (!isOwnerName(owner, o.left) && !isOwnerName(owner, o.result))
            return "op\xC3\xA9randes impossibles : ni la source ni la cible n'est " + owner.name;
    } else {
        if (trimmedOf(o.right).empty()) return "l'op\xC3\xA9rande de droite n'a pas de type";
        if (!known(o.right, true)) return unknownType(o.right);
        if (fam == OperatorFamily::Assignment) {
            if (!isOwnerName(owner, o.left))
                return "op\xC3\xA9randes impossibles : la gauche d'un " + o.op + " est modifi\xC3\xA9" "e, elle doit \xC3\xAA" "tre " + owner.name;
            if (!o.result.empty() && !sameTypeName(o.result, o.left))
                return "retour du mauvais type : " + o.op + " ne rend rien (il modifie " + owner.name + ")";
        } else if (!isOwnerName(owner, o.left) && !isOwnerName(owner, o.right)) {
            return "op\xC3\xA9randes impossibles : ni la gauche ni la droite n'est " + owner.name;
        }
        if (fam == OperatorFamily::Comparison && !sameTypeName(o.result, "BOOL"))
            return "retour du mauvais type : une comparaison rend BOOL" + (o.result.empty() ? std::string{} : ", pas " + o.result);
        if (fam == OperatorFamily::Arithmetic) {
            if (trimmedOf(o.result).empty()) return "un " + o.op + " rend une valeur : son type manque";
            if (!known(o.result, false)) return "retour d'un type inconnu : " + trimmedOf(o.result);
        }
    }
    // En double : la meme fonction, les memes operandes, ailleurs dans le projet.
    const std::string key = uniqueKey(o);
    for (const auto& x : allOperators(p)) {
        if (x.op->id == o.id || x.op->id == ignore || x.op == &o) continue;
        if (uniqueKey(*x.op) == key)
            return "op\xC3\xA9rateur en double : " + operatorSignature(o)
                 + (x.owner.kind == owner.kind && x.owner.id == owner.id ? std::string(" (d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "fini ici)")
                                                                           : " (d\xC3\xA9j\xC3\xA0 sur le " + x.owner.label() + ")");
    }
    return {};
}

void renumberOperators(Project& p, std::vector<HmiOperator>& list) {
    for (auto& o : list) o.id = p.allocate();
}

namespace {
// Le nom `from` devient `to` dans les operandes, les resultats et les cibles ; une
// conversion vers ce type affecte TO_<ancien> dans son script : TO_<nouveau>.
std::size_t retarget(std::vector<HmiOperator>& list, std::string_view from, std::string_view to) {
    std::size_t changed = 0;
    const std::string oldFn = "TO_" + trimmedOf(from), newFn = "TO_" + trimmedOf(to);
    for (auto& o : list) {
        const HmiOperator before = o;
        if (sameTypeName(o.left, from)) o.left = std::string(to);
        if (sameTypeName(o.right, from)) o.right = std::string(to);
        if (sameTypeName(o.result, from)) o.result = std::string(to);
        // Le script : le type (une locale, la signature en tete) et TO_<ancien>.
        o.body = renameWord(renameWord(o.body, from, to), oldFn, newFn);
        if (!(o == before)) ++changed;
    }
    return changed;
}
} // namespace

std::size_t renameTypeInOperators(Project& p, std::string_view from, std::string_view to) {
    if (trimmedOf(from).empty() || trimmedOf(from) == trimmedOf(to)) return 0;
    // Une conversion vers ce type : ses appels TO_<ancien>(...) ailleurs suivent.
    bool converted = false;
    for (const auto& x : allOperators(p))
        if (x.op->op == kConversionOp && sameTypeName(x.op->result, from)) converted = true;
    std::size_t changed = 0;
    for (auto& t : p.programs.types) changed += retarget(t.operators, from, to);
    for (auto& v : p.views) changed += retarget(v.operators, from, to);
    if (converted) (void)renameFunctionEverywhere(p, "TO_" + trimmedOf(from), "TO_" + trimmedOf(to));
    return changed;
}

HmiOperator retitled(const HmiOperator& before, HmiOperator after) {
    // La signature en tete du script (le commentaire du script prerempli).
    const std::string oldHead = "(* " + operatorSignature(before), newHead = "(* " + operatorSignature(after);
    if (after.body.compare(0, oldHead.size(), oldHead) == 0) after.body = newHead + after.body.substr(oldHead.size());
    // Le nom de fonction affecte : TO_REAL -> TO_LREAL, ADD -> SUB.
    const std::string f0 = operatorFunctionName(before), f1 = operatorFunctionName(after);
    // Seulement le nom NU (le resultat affecte) : un appel TO_STRING(a.x) reste un appel.
    if (!f0.empty() && !f1.empty() && !sameTypeName(f0, f1)) after.body = renameBareWord(after.body, f0, f1, true);
    return after;
}

void copyOperators(Project& p, std::vector<HmiOperator>& list, std::string_view fromOwner, std::string_view toOwner) {
    renumberOperators(p, list);
    if (!trimmedOf(fromOwner).empty() && trimmedOf(fromOwner) != trimmedOf(toOwner)) (void)retarget(list, fromOwner, toOwner);
}

// --------------------------------------------------------------- Compiler ---
std::vector<OperatorIssue> operatorIssues(const Project& p, const std::function<bool(std::string_view)>& plcType) {
    std::vector<OperatorIssue> out;
    for (const auto& x : allOperators(p)) {
        const HmiOperator& o = *x.op;
        OperatorIssue base;
        base.owner = x.owner;
        base.id = o.id;
        base.signature = operatorSignature(o);
        if (auto why = operatorProblem(p, x.owner, o, kNoId, plcType); !why.empty()) {
            OperatorIssue i = base;
            i.message = std::move(why);
            out.push_back(std::move(i));
        }
        // Le script : ses declarations, sa syntaxe (a et b sont ses operandes). Une
        // locale peut etre d'un type IHM (structure ou enumeration).
        const TypeKnown knownType = [&p](std::string_view t) { return p.hmiTypeByName(trimmedOf(t)) != nullptr; };
        const auto parts = splitDeclarations(o.body, false, knownType);
        for (const auto& e : parts.errors) {
            OperatorIssue i = base;
            i.line = e.line;
            i.message = e.message;
            i.error = e.severity == ScriptDiagnostic::Severity::Error;
            out.push_back(std::move(i));
        }
        if (parts.body.find_first_not_of(" \t\r\n") == std::string::npos) {
            OperatorIssue i = base;
            i.message = "script vide : l'op\xC3\xA9rateur ne fait rien";
            i.error = false;
            out.push_back(std::move(i));
            continue;
        }
        for (const auto& d : checkScript(ScriptLang::ST, parts.body, base.signature, knownType)) {
            if (d.severity == ScriptDiagnostic::Severity::Info) continue;
            OperatorIssue i = base;
            i.line = d.line;
            i.message = d.message;
            i.error = d.severity == ScriptDiagnostic::Severity::Error;
            out.push_back(std::move(i));
        }
        const auto fam = operatorFamily(o.op);
        // 1.10.1 (U2) : a, b, Resultat (et TO_X) ont un type - un membre qui n'y est
        // pas est dit a sa place, avec le nom le plus proche ("veux-tu dire x ?").
        {
            const auto names = operatorNames(o, x.owner.name);
            const std::string code = codeOnly(o.body);
            int line = 1;
            std::size_t lineStart = 0;
            const auto at = [&](std::size_t from, std::size_t to, std::string message, bool error) {
                OperatorIssue i = base;
                i.line = line;
                i.column = static_cast<int>(from - lineStart) + 1;
                i.length = static_cast<int>(to - from);
                i.message = std::move(message);
                i.error = error;
                out.push_back(std::move(i));
            };
            for (std::size_t i = 0; i < code.size();) {
                const char c = code[i];
                if (c == '\n') {
                    ++line;
                    lineStart = ++i;
                    continue;
                }
                if (!identStart(c) || (i > 0 && (identPart(code[i - 1]) || code[i - 1] == '.' || code[i - 1] == '#'))) {
                    ++i;
                    continue;
                }
                std::size_t e = i;
                while (e < code.size() && identPart(code[e])) ++e;
                const std::string_view word = std::string_view(code).substr(i, e - i);
                if (fam == OperatorFamily::Assignment && sameTypeName(word, kResultName)) {
                    at(i, e, o.op + " ne rend rien : Resultat n'existe pas ici ; modifie a (a.x := ...)", false);
                    i = e;
                    continue;
                }
                const OperatorName* n = nullptr;
                for (const auto& on : names)
                    if (sameTypeName(on.name, word)) n = &on;
                std::size_t k = e;
                if (n) {
                    std::string type = n->type, where = n->name;
                    while (k < code.size() && code[k] == '.') {
                        const std::size_t ms = k + 1;
                        std::size_t me = ms;
                        while (me < code.size() && identPart(code[me])) ++me;
                        const std::string member = code.substr(ms, me - ms);
                        if (member.empty() || std::isdigit(static_cast<unsigned char>(member[0]))) break;   // un bit (x.0)
                        const auto list = operandMembers(p, type);
                        if (list.empty()) {
                            if (isBaseType(type)) at(ms, me, where + " est un " + displayType(type) + " : il n'a pas de membre " + member, true);
                            break;                                   // un DDT, un type inconnu : pas ici
                        }
                        const OperandMember* found = nullptr;
                        for (const auto& m : list)
                            if (sameTypeName(m.name, member)) found = &m;
                        if (!found) {
                            exprcheck::Context ctx;
                            std::string all;
                            for (const auto& m : list) {
                                ctx.candidates.push_back(m.name);
                                if (all.size() < 60) all += (all.empty() ? "" : ", ") + m.name;
                            }
                            const std::string near = exprcheck::closest(ctx, member);
                            at(ms, me, "le membre " + member + " n'existe pas dans " + type + " (" + where + ")"
                                           + (near.empty() ? " : ses membres sont " + all : " : veux-tu dire " + near + " ?"),
                               true);
                            k = me;
                            break;
                        }
                        type = found->type;
                        where += "." + found->name;
                        k = me;
                    }
                }
                i = std::max(k, e);
            }
        }
        // Un operateur qui rend une valeur la donne : Nom := ... ; ou RETURN ... ;
        if (fam == OperatorFamily::Conversion || fam == OperatorFamily::Arithmetic || fam == OperatorFamily::Comparison) {
            const std::string code = upperOf(codeOnly(parts.body));
            bool gives = false;
            const auto isIdent = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            for (const std::string& fn : {upperOf(kResultName), upperOf(operatorFunctionName(o))})
                for (std::size_t at = code.find(fn); at != std::string::npos && !gives; at = code.find(fn, at + fn.size())) {
                    if (at > 0 && (isIdent(code[at - 1]) || code[at - 1] == '.')) continue;
                    std::size_t k = at + fn.size();
                    if (k < code.size() && isIdent(code[k])) continue;
                    // Resultat := ... ou Resultat.x := ... (une structure rendue membre par membre)
                    while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
                    gives = code.compare(k, 2, ":=") == 0 || (k < code.size() && (code[k] == '.' || code[k] == '['));
                }
            for (std::size_t at = code.find("RETURN"); at != std::string::npos && !gives; at = code.find("RETURN", at + 6)) {
                if (at > 0 && isIdent(code[at - 1])) continue;
                std::size_t k = at + 6;
                if (k < code.size() && isIdent(code[k])) continue;
                while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
                gives = k < code.size() && code[k] != ';';
            }
            if (!gives) {
                OperatorIssue i = base;
                i.message = "l'op\xC3\xA9rateur ne donne jamais son r\xC3\xA9sultat (Resultat := ... ; ou RETURN ... ;)";
                i.error = false;
                out.push_back(std::move(i));
            }
        }
    }
    return out;
}

// ---------------------------------------------------- l'aide a la saisie ----
std::vector<OperatorSuggestion> operatorSuggestions(const Project& p, std::string_view typeName) {
    std::vector<OperatorSuggestion> out;
    std::set<std::string> seen;
    for (const auto& x : allOperators(p)) {
        const HmiOperator& o = *x.op;
        const bool touches = sameTypeName(o.left, typeName) || (!o.right.empty() && sameTypeName(o.right, typeName));
        if (!touches) continue;
        OperatorSuggestion s;
        if (o.op == kConversionOp) {
            if (!sameTypeName(o.left, typeName)) continue;     // une conversion VERS ce type ne s'applique pas a lui
            s.insert = operatorFunctionName(o) + "(";
        } else {
            s.insert = o.op + " ";
        }
        s.label = operatorSignature(o);
        s.help = o.description.empty() ? std::string(operatorLabel(o.op)) + " (" + x.owner.label() + ")" : o.description;
        if (seen.insert(s.label).second) out.push_back(std::move(s));
    }
    return out;
}

std::vector<OperatorSuggestion> allConversions(const Project& p) {
    std::vector<OperatorSuggestion> out;
    std::set<std::string> seen;
    for (const auto& x : allOperators(p)) {
        const HmiOperator& o = *x.op;
        if (o.op != kConversionOp) continue;
        OperatorSuggestion s;
        s.insert = operatorFunctionName(o) + "(";
        s.label = operatorSignature(o);
        s.help = o.description.empty() ? "conversion (" + x.owner.label() + ")" : o.description;
        if (seen.insert(s.label).second) out.push_back(std::move(s));
    }
    return out;
}

} // namespace hmi
