// =============================================================================
//  sim/DialectLib.cpp - 1.12.7 : la bibliotheque du dialecte IHM (voir l'en-tete)
// =============================================================================
#include "DialectLib.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace sim::lib {

namespace {

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// Ce que vaut chaque type numerique (la maquette : TYPEINFO).
struct TypeRow {
    std::string_view name, c, macro, minText, maxText, summary;
    int  bits;
    bool isSigned, isFloat, isBool;
};
const std::vector<TypeRow>& typeRows() {
    static const std::vector<TypeRow> k = {
        {"SINT", "int8_t", "INT8", "-128", "127", "entier sign\xC3\xA9, 8 bits", 8, true, false, false},
        {"INT", "int16_t", "INT16", "-32768", "32767", "entier sign\xC3\xA9, 16 bits", 16, true, false, false},
        {"DINT", "int32_t", "INT32", "-2147483648", "2147483647", "entier sign\xC3\xA9, 32 bits", 32, true, false, false},
        {"LINT", "int64_t", "INT64", "-9223372036854775808", "9223372036854775807", "entier sign\xC3\xA9, 64 bits", 64, true, false, false},
        {"USINT", "uint8_t", "UINT8", "0", "255", "entier non sign\xC3\xA9, 8 bits", 8, false, false, false},
        {"UINT", "uint16_t", "UINT16", "0", "65535", "entier non sign\xC3\xA9, 16 bits", 16, false, false, false},
        {"UDINT", "uint32_t", "UINT32", "0", "4294967295", "entier non sign\xC3\xA9, 32 bits", 32, false, false, false},
        {"ULINT", "uint64_t", "UINT64", "0", "18446744073709551615", "entier non sign\xC3\xA9, 64 bits", 64, false, false, false},
        {"BYTE", "uint8_t", "UINT8", "0", "255", "cha\xC3\xAEne de 8 bits", 8, false, false, false},
        {"WORD", "uint16_t", "UINT16", "0", "65535", "cha\xC3\xAEne de 16 bits", 16, false, false, false},
        {"DWORD", "uint32_t", "UINT32", "0", "4294967295", "cha\xC3\xAEne de 32 bits", 32, false, false, false},
        {"LWORD", "uint64_t", "UINT64", "0", "18446744073709551615", "cha\xC3\xAEne de 64 bits", 64, false, false, false},
        {"REAL", "float", "FLT", "1.17549435E-38", "3.40282347E+38", "r\xC3\xA9" "el IEEE 754, 32 bits", 32, true, true, false},
        {"LREAL", "double", "DBL", "2.2250738585072014E-308", "1.7976931348623157E+308", "r\xC3\xA9" "el IEEE 754, 64 bits", 64, true, true, false},
        {"BOOL", "bool", "", "FALSE", "TRUE", "bool\xC3\xA9" "en", 1, false, false, true},
    };
    return k;
}
const TypeRow* typeRow(std::string_view name) noexcept {
    const std::string u = upper(trimmed(name));
    for (const auto& r : typeRows())
        if (r.name == u) return &r;
    return nullptr;
}

// Les caracteristiques des reels (std::numeric_limits<float>, <double>).
struct FloatRow {
    std::string_view eps, dmin;
    int dig, d10, md10, minE, maxE, minE10, maxE10;
};
const FloatRow& floatRow(bool lreal) {
    static const FloatRow r{"1.1920929E-07", "1.40129846E-45", 24, 6, 9, -125, 128, -37, 38};
    static const FloatRow l{"2.220446049250313E-16", "4.9406564584124654E-324", 53, 15, 17, -1021, 1024, -307, 308};
    return lreal ? l : r;
}

bool isBoolConstant(std::string_view m) {
    return m.rfind("IS_", 0) == 0 || m == "HAS_DENORM_LOSS" || m == "HAS_INFINITY" || m == "HAS_QUIET_NAN" || m == "HAS_SIGNALING_NAN"
        || m == "TRAPS" || m == "TINYNESS_BEFORE";
}

// Le plus court texte d'un double qui se relit a l'identique (comme String(x) en JavaScript).
std::string shortest(double v) {
    char buf[64];
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*g", p, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::size_t distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

} // namespace

// ------------------------------------------------------------------ les templates ---
const std::vector<std::string_view>& limitsTypes() {
    static const std::vector<std::string_view> k = {"SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT",
                                                    "BYTE", "WORD", "DWORD", "LWORD", "REAL", "LREAL", "BOOL"};
    return k;
}

const std::vector<TemplateInfo>& templates() {
    static const std::vector<TemplateInfo> k = [] {
        const std::vector<std::string_view> ints(limitsTypes().begin(), limitsTypes().begin() + 12);
        std::vector<std::string_view> nums = ints;
        nums.push_back("REAL");
        nums.push_back("LREAL");
        const std::string_view intWant = "un type entier est attendu (SINT \xE2\x80\xA6 ULINT, BYTE \xE2\x80\xA6 LWORD)";
        std::vector<TemplateInfo> v;
        v.push_back({"NUMERIC_LIMITS", true, true, "", limitsTypes(),
                     "un type num\xC3\xA9rique est attendu (SINT \xE2\x80\xA6 ULINT, BYTE \xE2\x80\xA6 LWORD, REAL, LREAL, BOOL)", "NUMERIC_LIMITS<UINT>.MAX()",
                     "NUMERIC_LIMITS attend son type : NUMERIC_LIMITS<UINT>.MAX()", "std::numeric_limits",
                     "Les bornes et les caract\xC3\xA9ristiques d'un type num\xC3\xA9rique : NUMERIC_LIMITS<UINT>.MAX() = 65535."});
        v.push_back({"NUMBERS", true, false, "LREAL", {"REAL", "LREAL"}, "REAL ou LREAL est attendu", "NUMBERS<REAL>.PI", "",
                     "std::numbers", "Les constantes math\xC3\xA9matiques (\xCF\x80, e, la racine de 2\xE2\x80\xA6) : NUMBERS.PI en LREAL, NUMBERS<REAL>.PI en REAL."});
        v.push_back({"SATURATE_CAST", false, true, "", ints, intWant, "SATURATE_CAST<INT>(x)",
                     "SATURATE_CAST attend son type : SATURATE_CAST<INT>(x)", "std::saturate_cast",
                     "Convertit x vers T en bloquant aux bornes de T."});
        v.push_back({"MIDPOINT", false, false, "", nums, "un type num\xC3\xA9rique est attendu (SINT \xE2\x80\xA6 LWORD, REAL, LREAL)", "MIDPOINT<INT>(a, b)", "",
                     "std::midpoint", "Le milieu de a et b, sans d\xC3\xA9" "bordement."});
        for (std::string_view n : {"ADD_SAT", "SUB_SAT", "MUL_SAT", "DIV_SAT"})
            v.push_back({n, false, false, "", ints, intWant,
                         n == "ADD_SAT" ? "ADD_SAT<INT>(a, b)" : n == "SUB_SAT" ? "SUB_SAT<INT>(a, b)" : n == "MUL_SAT" ? "MUL_SAT<INT>(a, b)" : "DIV_SAT<INT>(a, b)",
                         "", n == "ADD_SAT" ? "std::add_sat" : n == "SUB_SAT" ? "std::sub_sat" : n == "MUL_SAT" ? "std::mul_sat" : "std::div_sat",
                         "Le calcul bloqu\xC3\xA9 aux bornes du type au lieu de boucler."});
        return v;
    }();
    return k;
}

const TemplateInfo* templateInfo(std::string_view name) noexcept {
    const std::string u = upper(trimmed(name));
    for (const auto& t : templates())
        if (t.name == u) return &t;
    return nullptr;
}

bool templateAccepts(const TemplateInfo& t, std::string_view type) noexcept {
    const std::string u = upper(trimmed(type));
    return std::find(t.types.begin(), t.types.end(), u) != t.types.end();
}

std::string_view typeSummary(std::string_view type) noexcept {
    const auto* r = typeRow(type);
    return r ? r->summary : std::string_view{};
}
std::string_view cTypeName(std::string_view type) noexcept {
    const auto* r = typeRow(type);
    return r ? r->c : std::string_view{};
}

bool elementaryTypeName(std::string_view name) noexcept {
    static constexpr std::string_view kOthers[] = {"STRING", "WSTRING", "CHAR", "WCHAR", "TIME", "LTIME", "DATE", "LDATE", "DT", "LDT",
                                                   "TOD", "LTOD", "DATE_AND_TIME", "TIME_OF_DAY", "ARRAY", "LIST", "VECTOR", "MAP", "TUPLE"};
    if (typeRow(name)) return true;
    const std::string u = upper(trimmed(name));
    for (const auto o : kOthers)
        if (o == u) return true;
    return false;
}

const std::vector<MemberInfo>& limitsMembers() {
    static const std::vector<MemberInfo> k = {
        {"MIN", "min", true, "La plus petite valeur finie ; pour REAL et LREAL, le plus petit positif normalis\xC3\xA9 (le plus n\xC3\xA9gatif, c'est LOWEST).", "", 0},
        {"MAX", "max", true, "La plus grande valeur finie.", "", 0},
        {"LOWEST", "lowest", true, "La plus petite valeur finie : \xE2\x88\x92MAX() pour REAL et LREAL, MIN() pour un entier. (C++11)", "", 0},
        {"EPSILON", "epsilon", true, "L'\xC3\xA9" "cart entre 1 et le r\xC3\xA9" "el suivant ; 0 pour un entier.", "", 0},
        {"ROUND_ERROR", "round_error", true, "L'erreur d'arrondi maximale : 0.5 pour REAL et LREAL, 0 pour un entier.", "", 0},
        {"INFINITY", "infinity", true, "L'infini positif, si HAS_INFINITY ; sinon 0.", "", 0},
        {"QUIET_NAN", "quiet_NaN", true, "Un \xC2\xAB pas un nombre \xC2\xBB silencieux, si HAS_QUIET_NAN ; sinon 0.", "", 0},
        {"SIGNALING_NAN", "signaling_NaN", true, "Un \xC2\xAB pas un nombre \xC2\xBB qui signale, si HAS_SIGNALING_NAN ; sinon 0.", "", 0},
        {"DENORM_MIN", "denorm_min", true, "Le plus petit positif, d\xC3\xA9normalis\xC3\xA9 ; 0 pour un entier.", "", 0},
        {"IS_SPECIALIZED", "is_specialized", false, "Vrai : NUMERIC_LIMITS conna\xC3\xAEt ce type.", "", 0},
        {"IS_SIGNED", "is_signed", false, "Vrai si le type a des n\xC3\xA9gatifs.", "", 0},
        {"IS_INTEGER", "is_integer", false, "Vrai pour un entier (et BOOL).", "", 0},
        {"IS_EXACT", "is_exact", false, "Vrai si les calculs sont exacts : les entiers.", "", 0},
        {"HAS_INFINITY", "has_infinity", false, "Vrai si le type a l'infini : REAL, LREAL.", "", 0},
        {"HAS_QUIET_NAN", "has_quiet_NaN", false, "Vrai si le type a un \xC2\xAB pas un nombre \xC2\xBB silencieux.", "", 0},
        {"HAS_SIGNALING_NAN", "has_signaling_NaN", false, "Vrai si le type a un \xC2\xAB pas un nombre \xC2\xBB qui signale.", "", 0},
        {"HAS_DENORM", "has_denorm", false, "1 si le type a des d\xC3\xA9normalis\xC3\xA9s (REAL, LREAL), 0 sinon. D\xC3\xA9pr\xC3\xA9" "ci\xC3\xA9 en C++23.", "", 0},
        {"HAS_DENORM_LOSS", "has_denorm_loss", false, "Vrai si une perte de pr\xC3\xA9" "cision est vue comme une perte de d\xC3\xA9normalisation. D\xC3\xA9pr\xC3\xA9" "ci\xC3\xA9 en C++23.", "", 0},
        {"ROUND_STYLE", "round_style", false, "L'arrondi : 0 vers z\xC3\xA9ro (les entiers), 1 au plus pr\xC3\xA8s (REAL, LREAL).", "", 0},
        {"IS_IEC559", "is_iec559", false, "Vrai si le type suit l'IEEE 754 (IEC 559) : REAL, LREAL.", "", 0},
        {"IS_BOUNDED", "is_bounded", false, "Vrai si les valeurs sont en nombre fini : tous ces types.", "", 0},
        {"IS_MODULO", "is_modulo", false, "Vrai si un d\xC3\xA9" "bordement boucle : les non sign\xC3\xA9s. Faux pour les sign\xC3\xA9s (en C++, leur d\xC3\xA9" "bordement est ind\xC3\xA9" "fini).", "", 0},
        {"DIGITS", "digits", false, "Les chiffres en base RADIX, sans le signe : 15 pour INT, 24 pour REAL (sa mantisse).", "", 0},
        {"DIGITS10", "digits10", false, "Les chiffres d\xC3\xA9" "cimaux qui passent sans perte : 4 pour INT, 6 pour REAL.", "", 0},
        {"MAX_DIGITS10", "max_digits10", false, "Les chiffres d\xC3\xA9" "cimaux pour \xC3\xA9" "crire un r\xC3\xA9" "el et le relire \xC3\xA0 l'identique : 9 pour REAL, 17 pour LREAL ; 0 pour un entier. (C++11)", "", 0},
        {"RADIX", "radix", false, "La base : 2.", "", 0},
        {"MIN_EXPONENT", "min_exponent", false, "Le plus petit e tel que 2^(e\xE2\x88\x92" "1) soit un r\xC3\xA9" "el normalis\xC3\xA9 : \xE2\x88\x92" "125 pour REAL ; 0 pour un entier.", "", 0},
        {"MIN_EXPONENT10", "min_exponent10", false, "Le plus petit e tel que 10^e soit un r\xC3\xA9" "el normalis\xC3\xA9 : \xE2\x88\x92" "37 pour REAL.", "", 0},
        {"MAX_EXPONENT", "max_exponent", false, "Le plus grand e tel que 2^(e\xE2\x88\x92" "1) soit fini : 128 pour REAL.", "", 0},
        {"MAX_EXPONENT10", "max_exponent10", false, "Le plus grand e tel que 10^e soit fini : 38 pour REAL, 308 pour LREAL.", "", 0},
        {"TRAPS", "traps", false, "Vrai si un calcul peut lever une exception mat\xC3\xA9rielle : la division enti\xC3\xA8re par z\xC3\xA9ro, sur x86.", "", 0},
        {"TINYNESS_BEFORE", "tinyness_before", false, "Vrai si un r\xC3\xA9sultat trop petit est vu avant l'arrondi.", "", 0},
    };
    return k;
}

const std::vector<MemberInfo>& numbersMembers() {
    static const std::vector<MemberInfo> k = {
        {"E", "e", false, "e, la base des logarithmes naturels.", "M_E", 2.718281828459045},
        {"LOG2E", "log2e", false, "log2 e : le logarithme de e en base 2.", "M_LOG2E", 1.4426950408889634},
        {"LOG10E", "log10e", false, "log10 e : le logarithme de e en base 10.", "M_LOG10E", 0.4342944819032518},
        {"PI", "pi", false, "\xCF\x80.", "M_PI", 3.141592653589793},
        {"INV_PI", "inv_pi", false, "1/\xCF\x80.", "M_1_PI", 0.3183098861837907},
        {"INV_SQRTPI", "inv_sqrtpi", false, "1 / racine de \xCF\x80.", "(M_2_SQRTPI / 2)", 0.5641895835477563},
        {"LN2", "ln2", false, "ln 2.", "M_LN2", 0.6931471805599453},
        {"LN10", "ln10", false, "ln 10.", "M_LN10", 2.302585092994046},
        {"SQRT2", "sqrt2", false, "La racine de 2.", "M_SQRT2", 1.4142135623730951},
        {"SQRT3", "sqrt3", false, "La racine de 3.", "", 1.7320508075688772},
        {"INV_SQRT3", "inv_sqrt3", false, "1 / racine de 3.", "", 0.5773502691896257},
        {"EGAMMA", "egamma", false, "\xCE\xB3, la constante d'Euler-Mascheroni.", "", 0.5772156649015329},
        {"PHI", "phi", false, "\xCF\x86, le nombre d'or.", "", 1.618033988749895},
    };
    return k;
}

const MemberInfo* templateMember(std::string_view templ, std::string_view member) noexcept {
    const std::string t = upper(trimmed(templ)), m = upper(trimmed(member));
    const auto& pool = t == "NUMBERS" ? numbersMembers() : limitsMembers();
    if (t != "NUMBERS" && t != "NUMERIC_LIMITS") return nullptr;
    for (const auto& x : pool)
        if (x.name == m) return &x;
    return nullptr;
}

std::string limitsText(std::string_view type, std::string_view member) {
    const auto* r = typeRow(type);
    const auto* m = templateMember("NUMERIC_LIMITS", member);
    if (!r || !m) return {};
    const bool F = r->isFloat, B = r->isBool;
    const std::string zero = F ? "0.0" : B ? "FALSE" : "0", Y = "TRUE", N = "FALSE";
    const int digits = F ? floatRow(r->name == "LREAL").dig : B ? 1 : r->isSigned ? r->bits - 1 : r->bits;
    const FloatRow& f = floatRow(r->name == "LREAL");
    const std::string_view n = m->name;
    if (n == "MIN") return std::string(r->minText);
    if (n == "MAX") return std::string(r->maxText);
    if (n == "LOWEST") return F ? "-" + std::string(r->maxText) : std::string(r->minText);
    if (n == "EPSILON") return F ? std::string(f.eps) : zero;
    if (n == "ROUND_ERROR") return F ? "0.5" : zero;
    if (n == "INFINITY") return F ? "+INF" : zero;
    if (n == "QUIET_NAN") return F ? "NAN" : zero;
    if (n == "SIGNALING_NAN") return F ? "SNAN" : zero;
    if (n == "DENORM_MIN") return F ? std::string(f.dmin) : zero;
    if (n == "IS_SPECIALIZED" || n == "IS_BOUNDED") return Y;
    if (n == "IS_SIGNED") return F || r->isSigned ? Y : N;
    if (n == "IS_INTEGER" || n == "IS_EXACT") return F ? N : Y;
    if (n == "HAS_INFINITY" || n == "HAS_QUIET_NAN" || n == "HAS_SIGNALING_NAN" || n == "IS_IEC559") return F ? Y : N;
    if (n == "HAS_DENORM") return F ? "1" : "0";
    if (n == "HAS_DENORM_LOSS" || n == "TINYNESS_BEFORE") return N;
    if (n == "ROUND_STYLE") return F ? "1" : "0";
    if (n == "IS_MODULO") return !F && !B && !r->isSigned ? Y : N;
    if (n == "DIGITS") return std::to_string(digits);
    if (n == "DIGITS10") return std::to_string(F ? f.d10 : static_cast<int>(std::floor(digits * 0.30102999566398)));
    if (n == "MAX_DIGITS10") return std::to_string(F ? f.md10 : 0);
    if (n == "RADIX") return "2";
    if (n == "MIN_EXPONENT") return std::to_string(F ? f.minE : 0);
    if (n == "MIN_EXPONENT10") return std::to_string(F ? f.minE10 : 0);
    if (n == "MAX_EXPONENT") return std::to_string(F ? f.maxE : 0);
    if (n == "MAX_EXPONENT10") return std::to_string(F ? f.maxE10 : 0);
    if (n == "TRAPS") return F ? N : Y;
    return {};
}

std::string limitsType(std::string_view type, std::string_view member) {
    const auto* r = typeRow(type);
    const auto* m = templateMember("NUMERIC_LIMITS", member);
    if (!r || !m) return {};
    if (m->function) return std::string(r->name);
    return isBoolConstant(m->name) ? "BOOL" : "DINT";
}

bool limitsValue(std::string_view type, std::string_view member, Value& out) {
    const auto* r = typeRow(type);
    const auto* m = templateMember("NUMERIC_LIMITS", member);
    if (!r || !m) return false;
    const std::string_view n = m->name;
    if (!m->function) {
        const std::string t = limitsText(type, member);
        if (isBoolConstant(n)) out = Value::boolean(t == "TRUE");
        else out = Value::integer(Type::DInt, std::strtoll(t.c_str(), nullptr, 10));
        return true;
    }
    if (r->isBool) {
        out = Value::boolean(n == "MAX");
        return true;
    }
    if (r->isFloat) {
        const bool l = r->name == "LREAL";
        double v = 0;
        if (n == "MIN") v = l ? std::numeric_limits<double>::min() : std::numeric_limits<float>::min();
        else if (n == "MAX") v = l ? std::numeric_limits<double>::max() : std::numeric_limits<float>::max();
        else if (n == "LOWEST") v = l ? std::numeric_limits<double>::lowest() : std::numeric_limits<float>::lowest();
        else if (n == "EPSILON") v = l ? std::numeric_limits<double>::epsilon() : std::numeric_limits<float>::epsilon();
        else if (n == "ROUND_ERROR") v = 0.5;
        else if (n == "INFINITY") v = std::numeric_limits<double>::infinity();
        else if (n == "QUIET_NAN") v = std::numeric_limits<double>::quiet_NaN();
        else if (n == "SIGNALING_NAN") v = std::numeric_limits<double>::signaling_NaN();
        else if (n == "DENORM_MIN") v = l ? std::numeric_limits<double>::denorm_min() : std::numeric_limits<float>::denorm_min();
        out = Value::real(v);
        return true;
    }
    IntType it;
    if (!intType(r->name, it)) return false;
    if (n == "MIN" || n == "LOWEST") out = Value::integer(it.type, it.lo);
    else if (n == "MAX") out = Value::integer(it.type, static_cast<std::int64_t>(it.hiBits));
    else out = Value::integer(it.type, 0);
    return true;
}

bool numbersValue(std::string_view type, std::string_view member, Value& out) {
    const std::string t = type.empty() ? std::string("LREAL") : upper(trimmed(type));
    const auto* m = templateMember("NUMBERS", member);
    if (!m || (t != "REAL" && t != "LREAL")) return false;
    out = Value::real(t == "REAL" ? static_cast<double>(static_cast<float>(m->value)) : m->value);
    return true;
}

std::string numbersText(std::string_view type, std::string_view member) {
    const std::string t = type.empty() ? std::string("LREAL") : upper(trimmed(type));
    const auto* m = templateMember("NUMBERS", member);
    if (!m || (t != "REAL" && t != "LREAL")) return {};
    if (t == "REAL") {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(static_cast<float>(m->value)));
        return buf;
    }
    return shortest(m->value);
}

std::string cppMember(std::string_view templ, std::string_view type, const MemberInfo& m) {
    const std::string t = upper(trimmed(type));
    if (upper(templ) == "NUMBERS") {
        if (t.empty() || t == "LREAL") return "std::numbers::" + std::string(m.cpp);
        return "std::numbers::" + std::string(m.cpp) + "_v<" + t + ">";
    }
    return "std::numeric_limits<" + t + ">::" + std::string(m.cpp) + (m.function ? "()" : "");
}

std::string cMember(std::string_view templ, std::string_view type, const MemberInfo& m) {
    if (upper(templ) == "NUMBERS") return std::string(m.c);
    const auto* r = typeRow(type);
    if (!r) return {};
    const std::string_view n = m.name;
    if (r->isFloat) {
        const std::string P(r->macro);
        if (n == "MIN") return P + "_MIN";
        if (n == "MAX") return P + "_MAX";
        if (n == "LOWEST") return "-" + P + "_MAX";
        if (n == "EPSILON") return P + "_EPSILON";
        if (n == "DENORM_MIN") return P + "_TRUE_MIN";
        if (n == "INFINITY") return "INFINITY";
        if (n == "QUIET_NAN") return "NAN";
        if (n == "SIGNALING_NAN") return P + "_SNAN";
        if (n == "HAS_DENORM") return P + "_HAS_SUBNORM";
        if (n == "ROUND_STYLE") return "FLT_ROUNDS";
        if (n == "IS_IEC559") return "__STDC_IEC_559__";
        if (n == "DIGITS") return P + "_MANT_DIG";
        if (n == "DIGITS10") return P + "_DIG";
        if (n == "MAX_DIGITS10") return P + "_DECIMAL_DIG";
        if (n == "RADIX") return "FLT_RADIX";
        if (n == "MIN_EXPONENT") return P + "_MIN_EXP";
        if (n == "MIN_EXPONENT10") return P + "_MIN_10_EXP";
        if (n == "MAX_EXPONENT") return P + "_MAX_EXP";
        if (n == "MAX_EXPONENT10") return P + "_MAX_10_EXP";
        return {};
    }
    if (r->isBool) return n == "MIN" || n == "LOWEST" ? "false" : n == "MAX" ? "true" : "";
    const std::string M(r->macro);
    if (n == "MIN" || n == "LOWEST") return r->isSigned ? M + "_MIN" : std::string{};
    if (n == "MAX") return M + "_MAX";
    if (n == "DIGITS") return r->isSigned ? M + "_WIDTH - 1" : M + "_WIDTH";
    return {};
}

// ------------------------------------------------- les entiers : bornes et calcul exact ---
bool intType(std::string_view name, IntType& out) noexcept {
    const auto* r = typeRow(name);
    if (!r || r->isFloat || r->isBool) return false;
    out.isSigned = r->isSigned;
    out.bits = r->bits;
    const std::string_view n = r->name;
    out.type = n == "SINT" || n == "INT" ? Type::Int : n == "DINT" ? Type::DInt : n == "LINT" ? Type::LInt
             : n == "USINT" || n == "UINT" ? Type::UInt : n == "UDINT" ? Type::UDInt : n == "ULINT" ? Type::ULInt
             : n == "BYTE" ? Type::Byte : n == "WORD" ? Type::Word : n == "DWORD" ? Type::DWord : Type::LWord;
    if (r->isSigned) {
        out.lo = r->bits == 64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (r->bits - 1));
        out.hiBits = r->bits == 64 ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                                   : (std::uint64_t{1} << (r->bits - 1)) - 1;
    } else {
        out.lo = 0;
        out.hiBits = r->bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << r->bits) - 1;
    }
    return true;
}

Wide wideOf(const Value& v) noexcept {
    Wide w;
    if (v.type() == Type::ULInt || v.type() == Type::LWord) {
        w.mag = static_cast<std::uint64_t>(v.asInteger());
        return w;
    }
    const std::int64_t x = v.asInteger();
    if (x < 0) {
        w.neg = true;
        w.mag = static_cast<std::uint64_t>(-(x + 1)) + 1;
    } else {
        w.mag = static_cast<std::uint64_t>(x);
    }
    return w;
}

namespace {
Wide negated(Wide a) noexcept {
    if (a.mag != 0 || a.huge) a.neg = !a.neg;
    return a;
}
Wide normal(Wide a) noexcept {
    if (a.mag == 0 && !a.huge) a.neg = false;
    return a;
}
} // namespace

Wide wideAdd(Wide a, Wide b) noexcept {
    if (a.huge) return a;
    if (b.huge) return b;
    Wide r;
    if (a.neg == b.neg) {
        r.neg = a.neg;
        r.mag = a.mag + b.mag;
        if (r.mag < a.mag) r.huge = true;
        return normal(r);
    }
    if (a.mag >= b.mag) {
        r.neg = a.neg;
        r.mag = a.mag - b.mag;
    } else {
        r.neg = b.neg;
        r.mag = b.mag - a.mag;
    }
    return normal(r);
}

Wide wideSub(Wide a, Wide b) noexcept { return wideAdd(a, negated(b)); }

Wide wideMul(Wide a, Wide b) noexcept {
    Wide r;
    r.neg = a.neg != b.neg;
    if ((!a.huge && a.mag == 0) || (!b.huge && b.mag == 0)) return Wide{};
    if (a.huge || b.huge || (a.mag != 0 && b.mag > std::numeric_limits<std::uint64_t>::max() / a.mag)) {
        r.huge = true;
        return r;
    }
    r.mag = a.mag * b.mag;
    return normal(r);
}

Wide wideDiv(Wide a, Wide b) noexcept {
    if (b.huge) return Wide{};
    if (b.mag == 0) return Wide{};
    Wide r;
    r.neg = a.neg != b.neg;
    if (a.huge) {
        r.huge = true;
        return r;
    }
    r.mag = a.mag / b.mag;
    return normal(r);
}

Wide wideHalf(Wide a) noexcept {
    if (a.huge) return a;
    a.mag /= 2;
    return normal(a);
}

int wideCompare(Wide a, Wide b) noexcept {
    a = normal(a);
    b = normal(b);
    if (a.neg != b.neg) return a.neg ? -1 : 1;
    const int sign = a.neg ? -1 : 1;
    if (a.huge != b.huge) return (a.huge ? 1 : -1) * sign;
    if (a.mag == b.mag) return 0;
    return (a.mag < b.mag ? -1 : 1) * sign;
}

namespace {
Wide loOf(const IntType& t) noexcept { return wideOf(Value::integer(Type::LInt, t.lo)); }
Wide hiOf(const IntType& t) noexcept {
    Wide w;
    w.mag = t.hiBits;
    return w;
}
} // namespace

bool fits(Wide v, const IntType& t) noexcept { return wideCompare(v, loOf(t)) >= 0 && wideCompare(v, hiOf(t)) <= 0; }

Value saturate(Wide v, const IntType& t) {
    if (wideCompare(v, loOf(t)) < 0) return Value::integer(t.type, t.lo);
    if (wideCompare(v, hiOf(t)) > 0) return Value::integer(t.type, static_cast<std::int64_t>(t.hiBits));
    const std::uint64_t bits = v.neg ? std::uint64_t{0} - v.mag : v.mag;
    return Value::integer(t.type, static_cast<std::int64_t>(bits));
}

// ------------------------------------------------------ les enumerations natives ---
const std::vector<EnumInfo>& dialectEnums() {
    static const std::vector<EnumInfo> k = {
        {"COMPARAISON", {"Egal", "Different", "Inferieur", "InferieurOuEgal", "Superieur", "SuperieurOuEgal"},
         "La comparaison des fonctions qui cherchent, comptent ou retirent sous condition (LIST_FIND_IF, LIST_COUNT_IF, LIST_REMOVE_IF\xE2\x80\xA6)."},
        {"OPERATION", {"Somme", "Produit", "Min", "Max", "Et", "Ou"},
         "L'op\xC3\xA9ration des fonctions de std::numeric (ACCUMULATE, REDUCE, PARTIAL_SUM, les SCAN\xE2\x80\xA6)."},
        {"TRANSFORMATION", {"Identite", "Abs", "Carre", "Racine", "Neg", "Inverse"},
         "La transformation de chaque \xC3\xA9l\xC3\xA9ment (TRANSFORM_REDUCE, TRANSFORM_INCLUSIVE_SCAN, TRANSFORM_EXCLUSIVE_SCAN)."},
    };
    return k;
}

bool dialectEnumValue(std::string_view literal, std::int64_t& out) noexcept {
    const std::string_view s = trimmed(literal);
    const auto hash = s.find('#');
    if (hash == std::string_view::npos) return false;
    const std::string e = upper(trimmed(s.substr(0, hash))), v = upper(trimmed(s.substr(hash + 1)));
    for (const auto& en : dialectEnums()) {
        if (en.name != e) continue;
        for (std::size_t k = 0; k < en.values.size(); ++k)
            if (upper(en.values[k]) == v) {
                out = static_cast<std::int64_t>(k);
                return true;
            }
        return false;
    }
    return false;
}

// --------------------------------------------------------- les natives de la 1.12.7 ---
const std::vector<NativeInfo>& natives() {
    // nom, min, max, ecrit, retaille, ARRAY, membre, groupe, signature, phrase
    static const std::vector<NativeInfo> k = {
        // ---- lire et ecrire
        {"TUPLE_GET", 2, 2, false, false, false, false, "tuple", "TUPLE_GET(t, n) : le type du champ n", "Le champ n du TUPLE (1 : le premier), comme t.Item1 ; n est une constante."},
        {"LIST_SET", 3, 3, true, false, true, false, "liste", "LIST_SET(L, i, x) : BOOL", "\xC3\x89" "crit x \xC3\xA0 l'indice i ; FALSE, sans erreur, hors de la liste (rien n'est \xC3\xA9" "crit). Une LIST, un VECTOR, un ARRAY (\xC3\xA0 ses indices)."},
        {"MAP_SET", 3, 3, true, false, false, false, "map", "MAP_SET(m, cl\xC3\xA9, v) : BOOL", "\xC3\x89" "crit la valeur de la cl\xC3\xA9, en la cr\xC3\xA9" "ant si elle manque (comme m[cl\xC3\xA9] := v) ; TRUE si la cl\xC3\xA9 est nouvelle."},
        {"TUPLE_SET", 3, 3, true, false, false, false, "tuple", "TUPLE_SET(t, n, x) : BOOL", "\xC3\x89" "crit le champ n (1 : le premier), comme t.Item1 := x ; n est une constante."},
        {"MAP_FIRST_KEY", 1, 2, false, false, false, false, "map", "MAP_FIRST_KEY(m [, d\xC3\xA9" "faut]) : cl\xC3\xA9", "La plus petite cl\xC3\xA9 (les cl\xC3\xA9s sont rang\xC3\xA9" "es) ; une MAP vide : D\xC3\x89" "FAUT, ou une erreur sans lui."},
        {"MAP_LAST_KEY", 1, 2, false, false, false, false, "map", "MAP_LAST_KEY(m [, d\xC3\xA9" "faut]) : cl\xC3\xA9", "La plus grande cl\xC3\xA9 ; une MAP vide : D\xC3\x89" "FAUT, ou une erreur sans lui."},
        {"TUPLE_SIZE", 1, 1, false, false, false, false, "tuple", "TUPLE_SIZE(t) : DINT", "Le nombre de champs du TUPLE (une constante)."},
        // ---- ajouter et retirer
        {"MAP_INSERT", 3, 3, true, false, false, false, "map", "MAP_INSERT(m, cl\xC3\xA9, v) : BOOL", "Ajoute la cl\xC3\xA9 seulement si elle manque : n'\xC3\xA9" "crase jamais ; TRUE si elle a \xC3\xA9t\xC3\xA9 ajout\xC3\xA9" "e."},
        {"LIST_INSERT_SORTED", 2, 3, true, true, false, false, "liste", "LIST_INSERT_SORTED(L, x [, d\xC3\xA9" "croissant]) : DINT", "Ins\xC3\xA8re la valeur \xC3\xA0 sa place dans une liste tri\xC3\xA9" "e (apr\xC3\xA8s ses \xC3\xA9gales) ; rend son indice."},
        {"LIST_POP_FIRST", 1, 1, true, true, false, false, "liste", "LIST_POP_FIRST(L) : T", "Retire le premier \xC3\xA9l\xC3\xA9ment et le rend (une file d'attente) ; une liste vide : une erreur."},
        {"LIST_REMOVE_ALL", 2, 2, true, true, false, false, "liste", "LIST_REMOVE_ALL(L, x) : DINT", "Retire toutes les valeurs \xC3\xA9gales ; rend le nombre retir\xC3\xA9."},
        {"LIST_REMOVE_IF", 3, 4, true, true, false, true, "liste", "LIST_REMOVE_IF(L, comparaison, valeur [, membre]) : DINT", "Retire les \xC3\xA9l\xC3\xA9ments qui v\xC3\xA9rifient la comparaison avec valeur (MEMBRE : celui d'une structure) ; rend le nombre retir\xC3\xA9."},
        {"MAP_REMOVE_IF", 3, 3, true, false, false, false, "map", "MAP_REMOVE_IF(m, comparaison, valeur) : DINT", "Retire les cl\xC3\xA9s dont la valeur v\xC3\xA9rifie la comparaison ; rend le nombre retir\xC3\xA9."},
        {"LIST_DISTINCT", 1, 1, true, true, false, false, "liste", "LIST_DISTINCT(L) : DINT", "Retire les doublons (garde la premi\xC3\xA8re de chaque valeur, dans l'ordre) ; rend le nombre retir\xC3\xA9."},
        {"MAP_INCREMENT", 2, 3, true, false, false, false, "map", "MAP_INCREMENT(m, cl\xC3\xA9 [, x]) : valeur", "Ajoute x (1 sans lui) \xC3\xA0 la valeur de la cl\xC3\xA9, en partant de 0 si elle manque ; rend la nouvelle valeur."},
        {"LIST_APPEND", 2, 2, true, true, false, false, "liste", "LIST_APPEND(L, autre) : DINT", "Ajoute \xC3\xA0 la fin tous les \xC3\xA9l\xC3\xA9ments d'une autre liste (ou d'un ARRAY) ; rend le nombre d'\xC3\xA9l\xC3\xA9ments."},
        {"MAP_MERGE", 2, 3, true, false, false, false, "map", "MAP_MERGE(m, autre [, remplacer]) : DINT", "Ajoute les cl\xC3\xA9s d'une autre MAP (une cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 garde sa valeur, sauf avec REMPLACER) ; rend le nombre de cl\xC3\xA9s ajout\xC3\xA9" "es."},
        {"LIST_FILL", 2, 2, true, false, true, false, "liste", "LIST_FILL(L, x) : DINT", "Met x dans toutes les cases, sans changer la taille ; rend le nombre de cases."},
        // ---- chercher
        {"LIST_LAST_INDEX_OF", 2, 2, false, false, true, false, "liste", "LIST_LAST_INDEX_OF(L, x) : DINT", "L'indice de la derni\xC3\xA8re valeur \xC3\xA9gale ; \xE2\x88\x92" "1 si elle n'y est pas."},
        {"MAP_KEY_OF", 3, 3, false, false, false, false, "map", "MAP_KEY_OF(m, v, d\xC3\xA9" "faut) : cl\xC3\xA9", "La premi\xC3\xA8re cl\xC3\xA9 (dans leur ordre) dont la valeur est \xC3\xA9gale \xC3\xA0 v ; D\xC3\x89" "FAUT si aucune."},
        {"LIST_COUNT_OF", 2, 2, false, false, true, false, "liste", "LIST_COUNT_OF(L, x) : DINT", "Le nombre de fois o\xC3\xB9 la valeur est dans la liste."},
        {"LIST_FIND_IF", 3, 4, false, false, true, true, "liste", "LIST_FIND_IF(L, comparaison, valeur [, membre]) : DINT", "L'indice du premier \xC3\xA9l\xC3\xA9ment qui v\xC3\xA9rifie la comparaison ; \xE2\x88\x92" "1 si aucun."},
        {"LIST_COUNT_IF", 3, 4, false, false, true, true, "liste", "LIST_COUNT_IF(L, comparaison, valeur [, membre]) : DINT", "Le nombre d'\xC3\xA9l\xC3\xA9ments qui v\xC3\xA9rifient la comparaison."},
        {"LIST_ALL", 3, 4, false, false, true, true, "liste", "LIST_ALL(L, comparaison, valeur [, membre]) : BOOL", "Vrai si tous les \xC3\xA9l\xC3\xA9ments v\xC3\xA9rifient la comparaison (vrai pour une liste vide)."},
        {"LIST_ANY", 3, 4, false, false, true, true, "liste", "LIST_ANY(L, comparaison, valeur [, membre]) : BOOL", "Vrai si au moins un \xC3\xA9l\xC3\xA9ment v\xC3\xA9rifie la comparaison."},
        {"LIST_NONE", 3, 4, false, false, true, true, "liste", "LIST_NONE(L, comparaison, valeur [, membre]) : BOOL", "Vrai si aucun \xC3\xA9l\xC3\xA9ment ne v\xC3\xA9rifie la comparaison."},
        {"LIST_MIN_INDEX", 1, 2, false, false, true, true, "liste", "LIST_MIN_INDEX(L [, membre]) : DINT", "L'indice de la plus petite valeur (la premi\xC3\xA8re) ; \xE2\x88\x92" "1 pour une liste vide."},
        {"LIST_MAX_INDEX", 1, 2, false, false, true, true, "liste", "LIST_MAX_INDEX(L [, membre]) : DINT", "L'indice de la plus grande valeur (la premi\xC3\xA8re) ; \xE2\x88\x92" "1 pour une liste vide."},
        {"MAP_FLOOR_KEY", 3, 3, false, false, false, false, "map", "MAP_FLOOR_KEY(m, k, d\xC3\xA9" "faut) : cl\xC3\xA9", "La plus grande cl\xC3\xA9 inf\xC3\xA9rieure ou \xC3\xA9gale \xC3\xA0 k ; D\xC3\x89" "FAUT s'il n'y en a pas (une table de paliers)."},
        {"MAP_CEIL_KEY", 3, 3, false, false, false, false, "map", "MAP_CEIL_KEY(m, k, d\xC3\xA9" "faut) : cl\xC3\xA9", "La plus petite cl\xC3\xA9 sup\xC3\xA9rieure ou \xC3\xA9gale \xC3\xA0 k ; D\xC3\x89" "FAUT s'il n'y en a pas."},
        // ---- ranger
        {"LIST_SORT_BY", 2, 3, true, false, true, false, "liste", "LIST_SORT_BY(L, membre [, d\xC3\xA9" "croissant]) : BOOL", "Trie une liste de structures (ou de TUPLE : 'Item2') par un membre ; \xC3\xA0 valeur \xC3\xA9gale, l'ordre ne change pas."},
        {"LIST_IS_SORTED", 1, 2, false, false, true, false, "liste", "LIST_IS_SORTED(L [, d\xC3\xA9" "croissant]) : BOOL", "Vrai si la liste est tri\xC3\xA9" "e (dans le sens demand\xC3\xA9)."},
        {"LIST_SWAP", 3, 3, true, false, true, false, "liste", "LIST_SWAP(L, i, j) : BOOL", "\xC3\x89" "change deux \xC3\xA9l\xC3\xA9ments ; FALSE si un indice est hors de la liste."},
        {"LIST_MOVE", 3, 3, true, false, true, false, "liste", "LIST_MOVE(L, de, vers) : BOOL", "D\xC3\xA9place un \xC3\xA9l\xC3\xA9ment d'un indice \xC3\xA0 un autre ; ceux d'entre les deux glissent d'une place."},
        {"LIST_ROTATE", 2, 2, true, false, true, false, "liste", "LIST_ROTATE(L, n) : BOOL", "Fait tourner les \xC3\xA9l\xC3\xA9ments de n places vers le d\xC3\xA9" "but (n n\xC3\xA9gatif : vers la fin)."},
        // ---- extraire et convertir
        {"LIST_SLICE", 3, 3, false, false, true, false, "liste", "LIST_SLICE(L, d\xC3\xA9" "but, nombre) : LIST OF T", "Une copie d'une partie : NOMBRE \xC3\xA9l\xC3\xA9ments \xC3\xA0 partir de D\xC3\x89" "BUT (ce qui d\xC3\xA9passe est ignor\xC3\xA9)."},
        {"LIST_CONCAT", 2, 2, false, false, true, false, "liste", "LIST_CONCAT(A, B) : LIST OF T", "Une nouvelle liste : les \xC3\xA9l\xC3\xA9ments de A, puis ceux de B."},
        {"LIST_COPY", 2, 2, false, false, true, false, "liste", "LIST_COPY(source, destination) : DINT", "Copie SOURCE dans DESTINATION case par case, DESTINATION garde sa taille ; rend le nombre copi\xC3\xA9."},
        {"TO_LIST", 1, 1, false, false, true, false, "liste", "TO_LIST(x) : LIST OF T", "Les \xC3\xA9l\xC3\xA9ments d'un ARRAY (ligne par ligne), d'un VECTOR, d'une liste : une nouvelle LIST."},
        {"MAP_VALUES", 1, 1, false, false, false, false, "map", "MAP_VALUES(m) : LIST OF valeur", "Les valeurs, dans l'ordre des cl\xC3\xA9s : une LIST."},
        {"MAP_ITEMS", 1, 1, false, false, false, false, "map", "MAP_ITEMS(m) : LIST OF TUPLE(cl\xC3\xA9, valeur)", "Les paires (cl\xC3\xA9, valeur), dans l'ordre des cl\xC3\xA9s : une LIST de TUPLE."},
        {"TUPLE_TO_STRING", 1, 1, false, false, false, false, "tuple", "TUPLE_TO_STRING(t) : STRING", "Le TUPLE \xC3\xA9" "crit comme un litt\xC3\xA9ral : ('four', 850.0)."},
        {"TUPLE_UNPACK", 2, -1, false, false, false, false, "tuple", "TUPLE_UNPACK(t, a, b, \xE2\x80\xA6) : BOOL", "\xC3\x89" "crit chaque champ dans une variable, dans l'ordre : autant de variables que de champs."},
        // ---- std::numeric
        {"ACCUMULATE", 2, 3, false, false, true, false, "numeric", "ACCUMULATE(T, init [, op]) : comme init", "Plie les \xC3\xA9l\xC3\xA9ments dans init, du premier au dernier : la somme, ou avec OPERATION le produit, le minimum, le maximum."},
        {"REDUCE", 1, 3, false, false, true, false, "numeric", "REDUCE(T [, init [, op]]) : comme les \xC3\xA9l\xC3\xA9ments", "Comme ACCUMULATE, sans ordre impos\xC3\xA9 ; init vaut 0 s'il manque."},
        {"INNER_PRODUCT", 3, 3, false, false, true, false, "numeric", "INNER_PRODUCT(A, B, init) : comme init", "Le produit scalaire : init plus la somme des A[i] \xC3\x97 B[i]."},
        {"TRANSFORM_REDUCE", 4, 4, false, false, true, false, "numeric", "TRANSFORM_REDUCE(T, init, op, transformation) : comme init", "Transforme chaque \xC3\xA9l\xC3\xA9ment (Abs, Carre, Racine\xE2\x80\xA6), puis plie avec op."},
        {"PARTIAL_SUM", 1, 2, false, false, true, false, "numeric", "PARTIAL_SUM(T [, op]) : comme T", "Les sommes cumul\xC3\xA9" "es [a, a+b, a+b+c\xE2\x80\xA6] ; avec op, le produit cumul\xC3\xA9, le minimum atteint\xE2\x80\xA6"},
        {"INCLUSIVE_SCAN", 1, 2, false, false, true, false, "numeric", "INCLUSIVE_SCAN(T [, op]) : comme T", "Comme PARTIAL_SUM, sans ordre impos\xC3\xA9 : le maximum atteint jusque-l\xC3\xA0\xE2\x80\xA6"},
        {"EXCLUSIVE_SCAN", 2, 3, false, false, true, false, "numeric", "EXCLUSIVE_SCAN(T, init [, op]) : comme T", "Le cumul de ce qui pr\xC3\xA9" "c\xC3\xA8" "de : [init, init+a, init+a+b\xE2\x80\xA6]."},
        {"TRANSFORM_INCLUSIVE_SCAN", 3, 3, false, false, true, false, "numeric", "TRANSFORM_INCLUSIVE_SCAN(T, op, transformation) : comme T", "Transforme chaque \xC3\xA9l\xC3\xA9ment, puis cumule."},
        {"TRANSFORM_EXCLUSIVE_SCAN", 4, 4, false, false, true, false, "numeric", "TRANSFORM_EXCLUSIVE_SCAN(T, init, op, transformation) : comme T", "Transforme chaque \xC3\xA9l\xC3\xA9ment, puis cumule ce qui pr\xC3\xA9" "c\xC3\xA8" "de."},
        {"ADJACENT_DIFFERENCE", 1, 1, false, false, true, false, "numeric", "ADJACENT_DIFFERENCE(T) : comme T", "Les \xC3\xA9" "carts entre voisins : [a, b\xE2\x88\x92" "a, c\xE2\x88\x92" "b\xE2\x80\xA6]."},
        {"IOTA", 2, 3, true, false, true, false, "numeric", "IOTA(T, d\xC3\xA9part [, pas]) : DINT", "Remplit T de d\xC3\xA9part, d\xC3\xA9part + pas, d\xC3\xA9part + 2 pas\xE2\x80\xA6 (pas : 1 sans lui) ; rend le nombre de cases."},
        {"GCD", 2, 2, false, false, false, false, "numeric", "GCD(a, b) : entier", "Le plus grand diviseur commun de deux entiers ; 0 si les deux sont nuls."},
        {"LCM", 2, 2, false, false, false, false, "numeric", "LCM(a, b) : entier", "Le plus petit multiple commun de deux entiers ; 0 si l'un est nul."},
        {"MIDPOINT", 2, 2, false, false, false, false, "numeric", "MIDPOINT[<T>](a, b) : T", "Le milieu de a et b sans d\xC3\xA9" "bordement (entre deux entiers, arrondi vers a)."},
        {"ADD_SAT", 2, 2, false, false, false, false, "numeric", "ADD_SAT[<T>](a, b) : T", "a + b, bloqu\xC3\xA9 aux bornes de T au lieu de boucler."},
        {"SUB_SAT", 2, 2, false, false, false, false, "numeric", "SUB_SAT[<T>](a, b) : T", "a \xE2\x88\x92 b, bloqu\xC3\xA9 aux bornes de T."},
        {"MUL_SAT", 2, 2, false, false, false, false, "numeric", "MUL_SAT[<T>](a, b) : T", "a \xC3\x97 b, bloqu\xC3\xA9 aux bornes de T."},
        {"DIV_SAT", 2, 2, false, false, false, false, "numeric", "DIV_SAT[<T>](a, b) : T", "a / b, bloqu\xC3\xA9 aux bornes de T (b = 0 reste une erreur)."},
        {"SATURATE_CAST", 1, 1, false, false, false, false, "numeric", "SATURATE_CAST<T>(x) : T", "x converti vers T en bloquant aux bornes (un r\xC3\xA9" "el est d'abord arrondi)."},
        // ---- <bit>
        {"BIT_COUNT", 1, 1, false, false, false, false, "bits", "BIT_COUNT(w) : DINT", "Le nombre de bits \xC3\xA0 1 (std::popcount)."},
        {"BIT_FIRST", 1, 1, false, false, false, false, "bits", "BIT_FIRST(w) : DINT", "Le num\xC3\xA9ro du plus bas bit \xC3\xA0 1 ; \xE2\x88\x92" "1 si aucun (std::countr_zero)."},
        {"BIT_TEST", 2, 2, false, false, false, false, "bits", "BIT_TEST(w, n) : BOOL", "Vrai si le bit n est \xC3\xA0 1."},
        {"BIT_SET", 2, 2, false, false, false, false, "bits", "BIT_SET(w, n) : comme w", "La valeur avec le bit n \xC3\xA0 1 (w ne change pas)."},
        {"BIT_RESET", 2, 2, false, false, false, false, "bits", "BIT_RESET(w, n) : comme w", "La valeur avec le bit n \xC3\xA0 0 (w ne change pas)."},
    };
    return k;
}

const NativeInfo* native(std::string_view name) noexcept {
    const std::string u = upper(trimmed(name));
    for (const auto& n : natives())
        if (n.name == u) return &n;
    return nullptr;
}

bool oldListAcceptsArray(std::string_view name) noexcept {
    const std::string u = upper(name);
    for (const char* n : {"LIST_GET", "LIST_CONTAINS", "LIST_INDEX_OF", "LIST_MIN", "LIST_MAX", "LIST_SUM", "LIST_AVG", "LIST_SORT", "LIST_REVERSE", "JOIN"})
        if (u == n) return true;
    return false;
}

bool oldListResizes(std::string_view name) noexcept {
    const std::string u = upper(name);
    for (const char* n : {"LIST_ADD", "LIST_INSERT", "LIST_REMOVE_AT", "LIST_REMOVE", "LIST_CLEAR", "VECTOR_PUSH", "VECTOR_POP", "VECTOR_INSERT",
                          "VECTOR_ERASE", "VECTOR_CLEAR", "VECTOR_RESIZE"})
        if (u == n) return true;
    return false;
}

// ---------------------------------------------------------------- veux-tu dire ? ---
std::string closest(std::string_view name, const std::vector<std::string>& pool) {
    const std::string n = upper(name);
    std::size_t bd = n.size() <= 3 ? 2 : 3;
    std::string best;
    for (const auto& p : pool) {
        const std::string u = upper(p);
        const std::size_t x = distance(n, u);
        if (x < bd && x > 0 && x < std::min(n.size(), u.size())) {
            bd = x;
            best = p;
        }
    }
    return best.empty() ? std::string{} : " \xE2\x80\x94 veux-tu dire " + best + " ?";
}

} // namespace sim::lib
