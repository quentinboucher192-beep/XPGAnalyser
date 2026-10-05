#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"
#include "HmiMedia.hpp"
#include "HmiScript.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace hmi {

namespace {

constexpr std::string_view kResult = "__hmi_r";

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le premier segment d'un chemin : "Pompes[3].Valid" -> "Pompes".
std::string_view rootOf(std::string_view path) {
    std::size_t i = 0;
    while (i < path.size() && path[i] != '.' && path[i] != '[') ++i;
    return path.substr(0, i);
}

bool isPureFunction(std::string_view name) {
    static const std::set<std::string> kPure = {
        "ABS", "SQRT", "LN", "LOG", "EXP", "EXPT", "SIN", "COS", "TAN", "ASIN", "ACOS", "ATAN",
        "MIN", "MAX", "LIMIT", "SEL", "MUX", "TRUNC", "ROUND", "LEN", "LEFT", "RIGHT", "MID",
        "CONCAT", "INSERT", "DELETE", "REPLACE", "FIND", "SHL", "SHR", "ROL", "ROR", "NEG"};
    const std::string u = upper(name);
    if (kPure.count(u)) return true;
    // Toutes les conversions : INT_TO_REAL, REAL_TO_STRING, DINT_TO_TIME...
    const auto to = u.find("_TO_");
    return to != std::string::npos && to > 0 && to + 4 < u.size();
}

// L'environnement d'une evaluation : les noms locaux d'abord, l'automate
// ensuite, et le resultat capture au passage. Aucune ecriture ne sort.
class ReadOnlyEnv final : public sim::Environment {
public:
    ReadOnlyEnv(sim::Environment& plc, const Scope* scope) : plc_(plc), scope_(scope) {}

    bool read(std::string_view name, sim::Value& out) override {
        if (name == kResult) { out = result_; return got_; }
        if (scope_) {
            if (const auto* v = scope_->value(name)) { out = *v; return true; }
            return plc_.read(scope_->resolve(name), out);
        }
        return plc_.read(name, out);
    }
    bool write(std::string_view name, const sim::Value& v) override {
        if (name != kResult) return false;
        result_ = v;
        got_ = true;
        return true;
    }
    bool exists(std::string_view name) override {
        if (name == kResult) return true;
        if (scope_) {
            if (scope_->value(name)) return true;
            return plc_.exists(scope_->resolve(name));
        }
        return plc_.exists(name);
    }
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, sim::Value>>& arguments, sim::Value& result) override {
        // Une fonction (INT_TO_REAL, ABS...) : oui. Une instance de bloc : non,
        // l'appeler depuis une vue la ferait avancer d'un cycle. L'interpreteur
        // passe le meme nom pour les deux : on ne laisse passer que les
        // fonctions standard, qui n'ont pas d'etat.
        // Lot 7 : une fonction IHM du projet (en lecture seule).
        if (auto* host = dynamic_cast<FunctionHost*>(&plc_); host && host->hostsFunction(name))
            return host->callFromExpression(name, arguments, result);
        if (!isPureFunction(name)) {
            if (message_.empty())
                message_ = "'" + std::string(name) + "' : une expression de vue n'appelle que des fonctions "
                           "standard (conversions, ABS, MIN, MAX, LIMIT...) et les fonctions IHM du projet, pas un bloc";
            return false;
        }
        return plc_.call(name, instance, arguments, result);
    }
    void report(sim::Diagnostic d) override {
        if (message_.empty()) message_ = d.message;
    }
    // 1.11 (chantier T3, porte en 1.10.2) : les conversions d'une enumeration
    // sont des operateurs du type (regenerateEnumConversions). Le dialecte les
    // trouve par le type declare de l'argument : sans ces quatre relais vers
    // l'hote, TO_STRING(Mode) rendait le nombre et TO_T_MODE(2) etait refuse.
    std::string declaredType(std::string_view name) override {
        if (scope_ && scope_->value(name)) return {};
        return plc_.declaredType(scope_ ? scope_->resolve(name) : std::string(name));
    }
    bool structMembers(std::string_view typeName, std::vector<std::pair<std::string, std::string>>& out) override {
        return plc_.structMembers(typeName, out);
    }
    bool findOperator(std::string_view op, std::string_view l, std::string_view r, sim::OperatorSource& out) override {
        return plc_.findOperator(op, l, r, out);
    }
    std::string canonicalName(std::string_view name) override {
        return plc_.canonicalName(scope_ ? scope_->resolve(name) : std::string(name));
    }

    [[nodiscard]] bool               got() const noexcept { return got_; }
    [[nodiscard]] const sim::Value&  result() const noexcept { return result_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    sim::Environment& plc_;
    const Scope*      scope_;
    sim::Value        result_;
    bool              got_{false};
    std::string       message_;
};

} // namespace

bool isStandardFunction(std::string_view name) { return isPureFunction(name); }

// ---------------------------------------------------------------- Scope ----
// Lot 8 : sans casse, comme le ST (Moteur, MOTEUR et moteur sont le meme nom).
void Scope::setValue(std::string name, sim::Value v) { values_[upper(name)] = std::move(v); }
void Scope::setAlias(std::string name, std::string path) { aliases_[upper(name)] = std::move(path); }

const sim::Value* Scope::value(std::string_view name) const {
    if (const auto it = values_.find(upper(name)); it != values_.end()) return &it->second;
    return parent_ ? parent_->value(name) : nullptr;
}

const std::string* Scope::alias(std::string_view name) const {
    if (const auto it = aliases_.find(upper(name)); it != aliases_.end()) return &it->second;
    return parent_ ? parent_->alias(name) : nullptr;
}

bool Scope::empty() const noexcept { return values_.empty() && aliases_.empty() && (!parent_ || parent_->empty()); }

std::vector<std::string> Scope::names() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : aliases_) out.push_back(k);
    for (const auto& [k, v] : values_) out.push_back(k);
    return out;
}

std::string Scope::resolve(std::string_view path) const {
    const std::string_view root = rootOf(path);
    // Un alias peut en designer un autre (un composant dans un composant) :
    // on deroule, avec une borne contre les boucles.
    std::string out(path);
    for (int guard = 0; guard < 16; ++guard) {
        const auto* a = alias(rootOf(out));
        if (!a) break;
        out = *a + out.substr(rootOf(out).size());
    }
    (void)root;
    return out;
}

// ------------------------------------------------------------ analyse ------
bool isReadOnly(std::string_view s, std::string* why) {
    bool inString = false;
    char quoteChar = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (inString) {
            if (c == quoteChar) inString = false;
            continue;
        }
        if (c == '\'' || c == '"') { inString = true; quoteChar = c; continue; }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            if (end == std::string_view::npos) break;
            i = end + 1;
            continue;
        }
        if (c == ';') { if (why) *why = "une expression ne contient pas de ';'"; return false; }
        if (c == ':' && i + 1 < s.size() && s[i + 1] == '=') {
            if (why) *why = "une expression n'affecte rien (':=') : les \xC3\xA9" "critures passent par les actions";
            return false;
        }
    }
    return true;
}

std::vector<std::string> scanRoots(std::string_view s) {
    static const std::set<std::string> kKeywords = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE"};
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            i = end == std::string_view::npos ? s.size() : end + 1;
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? s.size() : end + 2;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            // 16#FF, 2#1010, 1.5E3 : un litteral, jamais une racine.
            while (i < s.size() && (isIdentChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (isIdentStart(c)) {
            const std::size_t start = i;
            while (i < s.size() && isIdentChar(s[i])) ++i;
            const std::string word(s.substr(start, i - start));
            const bool member = start > 0 && s[start - 1] == '.';
            const bool typed = i < s.size() && s[i] == '#';          // T#5s, TIME#1s, INT#3
            std::size_t k = i;
            while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
            const bool call = k < s.size() && s[k] == '(';
            if (typed) {
                ++i;
                while (i < s.size() && (isIdentChar(s[i]) || s[i] == '.')) ++i;
                continue;
            }
            if (member || call || kKeywords.count(upper(word))) continue;
            if (std::find(out.begin(), out.end(), word) == out.end()) out.push_back(word);
            continue;
        }
        ++i;
    }
    return out;
}

// ----------------------------------------------------------- Expression ----
Expression Expression::compile(std::string_view source) {
    Expression e;
    e.source_ = std::string(source);
    // 1.11 (REP) : les `$` des reperes sont transparents ($V[1].Ouv$ se lit V[1].Ouv) ;
    // source() les garde.
    std::string trimmed = markers::strip(e.source_, markers::Mode::Expression);
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    std::size_t lead = 0;
    while (lead < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[lead]))) ++lead;
    trimmed.erase(0, lead);
    if (trimmed.empty()) { e.error_ = "expression vide"; return e; }
    std::string why;
    if (!isReadOnly(trimmed, &why)) { e.error_ = why; return e; }
    // 1.10.2 (de T3, c82063b) : le dialecte IHM, pour lire un litteral
    // d'enumeration (Mode = T_MODE#Auto, decision 15) ; sans lui, "#" etait un
    // caractere inattendu dans toute case d'expression (refusee avec un message
    // vide). Le reste du dialecte (fonctions, pointeurs, +=) ne change rien a une
    // expression en lecture seule.
    auto program = sim::parse(std::string(kResult) + " := " + trimmed + ";", "expression",
                              sim::ParseOptions{true});
    if (!program) {
        // Lot 13 : dit en francais ; l'expression tient sur une ligne et le ';' est celui qu'on lui ajoute.
        std::string m = frenchSimMessage(program.error().context.empty() ? program.error().message() : program.error().context);
        if (m.rfind("ligne 1 : ", 0) == 0) m.erase(0, 10);
        const std::string atEnd = " (trouv\xC3\xA9 : ';')";
        if (const auto at = m.find(atEnd); at != std::string::npos) m.replace(at, atEnd.size(), " (\xC3\xA0 la fin de l'expression)");
        // 1.11 (R111, REP-5) : un `$` reste apres la lecture des reperes - un repere sans `$` de fin ?
        if (m.find("'$'") != std::string::npos) {
            // 1.11.1 (REP-5) : un $ mal apparie - la faute dite a sa place, et le texte juste, au lieu
            // du "caractere inattendu" du dernier $ ($V[1].Ouv+$V[2].Ouv$ : le repere V[1].Ouv+ avait
            // pris le $ du suivant) : "il manque le $ de fin de $V[1].Ouv : ecris $V[1].Ouv$+$V[2].Ouv$".
            if (auto advice = markers::pairingAdvice(e.source_); !advice.empty()) m = std::move(advice);
            m += markers::pairingRule();   // 1.11.2 : la meme regle que dans un script ST (HmiScript.cpp)
        }
        e.error_ = m;
        return e;
    }
    e.program_ = *program;
    e.roots_ = scanRoots(trimmed);
    return e;
}

core::Result<sim::Value> Expression::evaluate(sim::Environment& plc, const Scope* scope) const {
    if (!program_) return core::fail(core::ErrorCode::InvalidArgument, error_.empty() ? "expression vide" : error_);
    ReadOnlyEnv env(plc, scope);
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 1000;
    limits.maxStatementsPerScan = 1000;
    (void)sim::execute(*program_, env, limits);
    if (!env.got())
        return core::fail(core::ErrorCode::InvalidArgument,
                          env.message().empty() ? "l'expression n'a rien produit" : env.message());
    return env.result();
}

// ------------------------------------------------------------ formats ------
namespace {
// Une duree a lire : "0 s", "12 s", "2,5 s", "2 min 05 s", "1 h 02 min 05 s".
std::string elapsedText(double seconds) {
    std::string sign;
    if (seconds < 0) { sign = "-"; seconds = -seconds; }
    char b[64];
    if (seconds < 59.95) {
        const double r = std::round(seconds * 10.0) / 10.0;
        std::snprintf(b, sizeof b, r == std::floor(r) ? "%.0f s" : "%.1f s", r);
        std::string out(b);
        if (const auto dot = out.find('.'); dot != std::string::npos) out[dot] = ',';
        return sign + out;
    }
    const long total = std::lround(seconds);
    if (total < 3600) std::snprintf(b, sizeof b, "%ld min %02ld s", total / 60, total % 60);
    else std::snprintf(b, sizeof b, "%ld h %02ld min %02ld s", total / 3600, (total / 60) % 60, total % 60);
    return sign + b;
}
} // namespace

std::string formatValue(const sim::Value& v, std::string_view format) {
    const bool numeric = sim::isNumeric(v.type()) || v.type() == sim::Type::Bool;
    const double value = !numeric ? 0.0
                         : v.type() == sim::Type::Real ? v.asReal() : static_cast<double>(v.asInteger());
    if (format.empty()) {
        if (v.type() == sim::Type::Real) {
            char b[64];
            std::snprintf(b, sizeof b, "%.6g", v.asReal());
            return b;
        }
        return v.type() == sim::Type::String ? v.asString() : v.display();
    }
    // Le texte d'un booleen (ou de toute valeur : vrai = non nulle).
    if (const auto bar = format.find('|'); bar != std::string_view::npos)
        return std::string(v.isTruthy() ? format.substr(0, bar) : format.substr(bar + 1));
    const char kind = format[0];
    int width = 0;
    if (format.size() > 1 && std::isdigit(static_cast<unsigned char>(format[1])))
        width = std::atoi(std::string(format.substr(1)).c_str());
    if (kind == 'x' || kind == 'X' || kind == 'b' || kind == 'B') {
        if (!numeric) return v.display();
        auto bits = static_cast<unsigned long long>(v.asInteger());
        std::string out;
        if (kind == 'b' || kind == 'B') {
            do { out.insert(out.begin(), static_cast<char>('0' + (bits & 1u))); bits >>= 1; } while (bits);
        } else {
            char b[32];
            std::snprintf(b, sizeof b, kind == 'x' ? "%llx" : "%llX", bits);
            out = b;
        }
        if (static_cast<int>(out.size()) < width) out.insert(0, static_cast<std::size_t>(width) - out.size(), '0');
        return out;
    }
    if (kind == 'e' || kind == 'E') {
        if (!numeric) return v.display();
        char b[64];
        std::snprintf(b, sizeof b, kind == 'e' ? "%.*e" : "%.*E", format.size() > 1 ? width : 3, value);
        return b;
    }
    if (kind == 't' || kind == 'T') {
        // TIME est en millisecondes ; un nombre, en secondes.
        if (v.type() == sim::Type::Time) return elapsedText(static_cast<double>(v.asInteger()) / 1000.0);
        return numeric ? elapsedText(value) : v.display();
    }
    if (!numeric) return v.display();
    // 0, 000, 0.0, 0.##, +0.0, 0.0%
    std::string_view f = format;
    const bool sign = !f.empty() && f.front() == '+';
    if (sign) f.remove_prefix(1);
    const bool percent = !f.empty() && f.back() == '%';
    if (percent) f.remove_suffix(1);
    const auto dot = f.find('.');
    const std::string_view whole = f.substr(0, dot);
    const std::string_view frac = dot == std::string_view::npos ? std::string_view{} : f.substr(dot + 1);
    const int minDigits = static_cast<int>(std::count(whole.begin(), whole.end(), '0'));
    const int decimals = static_cast<int>(frac.size());
    const int optional = static_cast<int>(std::count(frac.begin(), frac.end(), '#'));
    const double shown = percent ? value * 100.0 : value;
    char b[96];
    std::snprintf(b, sizeof b, sign ? "%+.*f" : "%.*f", decimals, shown);
    std::string out = b;
    // Les '#' de fin : ces decimales tombent quand elles valent zero.
    if (optional > 0 && decimals > 0) {
        int trimmed = 0;
        while (trimmed < optional && !out.empty() && out.back() == '0') { out.pop_back(); ++trimmed; }
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    // Les chiffres de la partie entiere : au moins `minDigits`.
    if (minDigits > 1) {
        const std::size_t begin = (out[0] == '-' || out[0] == '+') ? 1 : 0;
        std::size_t end = out.find('.');
        if (end == std::string::npos) end = out.size();
        const int have = static_cast<int>(end - begin);
        if (have < minDigits) out.insert(begin, static_cast<std::size_t>(minDigits - have), '0');
    }
    if (percent) out += " %";
    return out;
}

bool looksLikeFormat(std::string_view f) noexcept {
    if (f.empty()) return false;
    if (f.find('|') != std::string_view::npos) return f.find('}') == std::string_view::npos;
    const char k = f[0];
    const auto digitsAfter = [&](std::size_t from) {
        for (std::size_t i = from; i < f.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(f[i]))) return false;
        return true;
    };
    if (k == 'x' || k == 'X' || k == 'b' || k == 'B' || k == 'e' || k == 'E') return digitsAfter(1);
    if ((k == 't' || k == 'T') && f.size() == 1) return true;
    bool any = false;
    for (std::size_t i = 0; i < f.size(); ++i) {
        const char c = f[i];
        if (c == '0' || c == '#') { any = true; continue; }
        if (c == '.') continue;
        if (c == '+' && i == 0) continue;
        if (c == '%' && i + 1 == f.size()) continue;
        return false;
    }
    return any;
}

std::string unescapeText(std::string_view s) {
    if (s.find('\\') == std::string_view::npos) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; continue; }
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 't') { out += "    "; ++i; continue; }
        out += s[i];
    }
    return out;
}

// --------------------------------------------------------- TextTemplate ----
TextTemplate TextTemplate::compile(std::string_view source) {
    TextTemplate t;
    t.source_ = std::string(source);
    // 1.11 (REP) : un texte se montre sans les `$` de ses reperes ("Vanne $V1$" :
    // "Vanne V1", "{$V[0]$.Nom}" : le nom de V[0]) et `$$` y ecrit un `$`.
    const std::string plain = markers::strip(source, markers::Mode::Text);
    const std::string_view text = plain;
    std::string literal;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        // {{ et }} : une accolade litterale.
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') { literal += '{'; i += 2; continue; }
        if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') { literal += '}'; i += 2; continue; }
        if (c != '{') { literal += c; ++i; continue; }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) { literal += text.substr(i); break; }
        if (!literal.empty()) {
            Piece p;
            p.literal = unescapeText(literal);
            t.pieces_.push_back(std::move(p));
            literal.clear();
        }
        std::string inside(text.substr(i + 1, close - i - 1));
        Piece p;
        p.isExpr = true;
        // Le format est apres le dernier ':' qui n'est pas celui d'un ":=" et
        // qui ne suit que des caracteres de format.
        const auto colon = inside.rfind(':');
        if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
            const std::string fmt = inside.substr(colon + 1);
            if (looksLikeFormat(fmt)) {
                p.format = fmt;
                inside.resize(colon);
            } else if (fmt == "u" || fmt == "U") {
                // Lot 13 : {X:u} - le format et l'unite de la variable (HmiDisplay) ;
                // sans unite connue ici, la valeur telle quelle.
                inside.resize(colon);
            }
        }
        p.expr = Expression::compile(inside);
        t.pieces_.push_back(std::move(p));
        i = close + 1;
    }
    if (!literal.empty()) {
        Piece p;
        p.literal = unescapeText(literal);
        t.pieces_.push_back(std::move(p));
    }
    return t;
}

bool TextTemplate::dynamic() const noexcept {
    for (const auto& p : pieces_) if (p.isExpr) return true;
    return false;
}

std::string TextTemplate::render(sim::Environment& plc, const Scope* scope) const {
    std::string out;
    for (const auto& p : pieces_) {
        if (!p.isExpr) { out += p.literal; continue; }
        auto v = p.expr.evaluate(plc, scope);
        out += v ? formatValue(*v, p.format) : std::string("###");
    }
    return out;
}

std::vector<std::string> TextTemplate::errors() const {
    std::vector<std::string> out;
    for (const auto& p : pieces_)
        if (p.isExpr && !p.expr.valid()) out.push_back("{" + p.expr.source() + "} : " + p.expr.error());
    return out;
}

std::vector<std::string> TextTemplate::roots() const {
    std::vector<std::string> out;
    for (const auto& p : pieces_)
        if (p.isExpr)
            for (const auto& r : p.expr.roots())
                if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    return out;
}

} // namespace hmi
