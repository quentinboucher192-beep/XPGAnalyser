#include "HmiExpr.hpp"
#include "../sim/DialectLib.hpp"   // 1.12.7 : les templates ne sont pas des racines
#include "HmiNatives.hpp"   // 1.12.0 : les couleurs et l'aleatoire
#include "HmiMarkers.hpp"
#include "HmiRoles.hpp"     // 1.12.6 (lot B) : les proprietes du role Texte, le lecteur
#include "HmiTypes.hpp"     // 1.12.6 : les bornes d'un tableau (parseSpec)
#include "HmiMedia.hpp"
#include "HmiScript.hpp"

#include <algorithm>
#include <charconv>   // 1.12.5 : exactValue (std::to_chars au plus court)
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
    // 1.12.0 : les couleurs (RGB, COULEUR_...) et l'aleatoire (RANDOM...), natives de l'IHM.
    if (natives::isOwnFunction(u)) return true;
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
            if (scope_->value(name) || scope_->object(name)) return true;    // 1.12.4 : un parametre objet
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
        // 1.12.16 : une fonction de la vue, par son nom (la portee de la vue y mene : Vue_Four.Pourcent) -
        // avant celles du projet.
        if (auto* host = dynamic_cast<FunctionHost*>(&plc_); host && scope_ && name.find('.') == std::string_view::npos) {
            const std::string resolved = scope_->resolve(name);
            if (resolved != name && resolved.find('.') != std::string::npos && host->hostsFunction(resolved))
                return host->callFromExpression(resolved, arguments, result);
        }
        // Lot 7 : une fonction IHM du projet (en lecture seule).
        if (auto* host = dynamic_cast<FunctionHost*>(&plc_); host && host->hostsFunction(name))
            return host->callFromExpression(name, arguments, result);
        // 1.11.10 : la fonction d'une instance par un parametre de la vue (dans une popup du
        // symbole, INSTANCE_DU_SYMBOLE.Etat() : l'instance qui l'a ouverte).
        if (auto* host = dynamic_cast<FunctionHost*>(&plc_); host && scope_ && name.find('.') != std::string_view::npos) {
            const std::string resolved = scope_->resolve(name);
            if (resolved != name && host->hostsFunction(resolved)) return host->callFromExpression(resolved, arguments, result);
        }
        // 1.12.0 : les couleurs et l'aleatoire - servis ici, quel que soit l'environnement.
        if (natives::isOwnFunction(name)) {
            std::vector<sim::Value> values;
            for (const auto& a : arguments) values.push_back(a.second);
            std::string why;
            if (natives::call(name, values, result, &why)) return true;
            if (message_.empty()) message_ = why;
            return false;
        }
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
    // 1.12.26 : une expression lit un fichier, une ressource du projet (RESOURCE_READ('aide.txt'), FILE_EXISTS(...)) ;
    // elle n'en ecrit, n'en copie, n'en supprime aucun.
    bool fileOp(std::string_view op, const std::vector<std::string>& args, FileResult& out, std::string& why) override {
        if (sim::lib::changesFiles(op)) {
            why = "une expression ne change rien : " + std::string(op) + " se fait dans un script";
            return false;
        }
        return plc_.fileOp(op, args, out, why);
    }
    // 1.11 (chantier T3, porte en 1.10.2) : les conversions d'une enumeration
    // sont des operateurs du type (regenerateEnumConversions). Le dialecte les
    // trouve par le type declare de l'argument : sans ces quatre relais vers
    // l'hote, TO_STRING(Mode) rendait le nombre et TO_T_MODE(2) etait refuse.
    std::string declaredType(std::string_view name) override {
        if (scope_ && scope_->value(name)) return {};
        if (scope_)
            if (auto own = scope_->object(name); own && own->type) return own->type->text();   // 1.12.4
        return plc_.declaredType(scope_ ? scope_->resolve(name) : std::string(name));
    }
    bool structMembers(std::string_view typeName, std::vector<std::pair<std::string, std::string>>& out) override {
        return plc_.structMembers(typeName, out);
    }
    bool enumItems(std::string_view typeName, std::vector<EnumItem>& out) override { return plc_.enumItems(typeName, out); }   // 1.12.6
    // 1.12.2 : une variable objet (LIST, MAP...) : une copie - une expression ne modifie rien
    // (LIST_ADD(L, 1) dans une expression ajoute a la copie, pas a la variable).
    sim::ObjRef richVariable(std::string_view name) override {
        if (scope_ && scope_->value(name)) return nullptr;
        if (scope_)
            if (auto own = scope_->object(name)) return sim::deepCopy(*own);    // 1.12.4 : un parametre objet recu en valeur
        auto o = plc_.richVariable(scope_ ? scope_->resolve(name) : std::string(name));
        return o ? sim::deepCopy(*o) : nullptr;
    }
    // 1.12.4 : une structure qui porte des membres objets (Recette.Etapes.Count).
    bool hasRichMembers(std::string_view name) override {
        if (scope_ && (scope_->value(name) || scope_->object(name))) return false;
        return plc_.hasRichMembers(scope_ ? scope_->resolve(name) : std::string(name));
    }
    bool findOperator(std::string_view op, std::string_view l, std::string_view r, sim::OperatorSource& out) override {
        return plc_.findOperator(op, l, r, out);
    }
    std::string canonicalName(std::string_view name) override {
        return plc_.canonicalName(scope_ ? scope_->resolve(name) : std::string(name));
    }
    // 1.11.20 : les surcharges des fonctions du projet - le choix de l'hote (la regle du controle) ;
    // une fonction d'instance par un parametre de la vue (INSTANCE_DU_SYMBOLE.Etat) : son chemin.
    int overloads(std::string_view name) override { return plc_.overloads(throughScope(name)); }
    bool chooseOverload(std::string_view name, const std::vector<sim::ArgShape>& args, std::string& key, std::string& why) override {
        return plc_.chooseOverload(throughScope(name), args, key, why);
    }
    int chooseInner(const std::vector<const sim::Function*>& candidates, const std::vector<sim::ArgShape>& args, std::string& why) override {
        return plc_.chooseInner(candidates, args, why);
    }
    std::string resultType(std::string_view name, const std::vector<sim::ArgShape>& args) override {
        return plc_.resultType(throughScope(name), args);
    }

    [[nodiscard]] std::string throughScope(std::string_view name) const {
        return scope_ && name.find('.') != std::string_view::npos ? scope_->resolve(name) : std::string(name);
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
void Scope::setObject(std::string name, sim::ObjRef o) { objects_[upper(name)] = std::move(o); }

const sim::Value* Scope::value(std::string_view name) const {
    if (const auto it = values_.find(upper(name)); it != values_.end()) return &it->second;
    return parent_ ? parent_->value(name) : nullptr;
}

const std::string* Scope::alias(std::string_view name) const {
    if (const auto it = aliases_.find(upper(name)); it != aliases_.end()) return &it->second;
    return parent_ ? parent_->alias(name) : nullptr;
}

sim::ObjRef Scope::object(std::string_view name) const {
    if (const auto it = objects_.find(upper(name)); it != objects_.end()) return it->second;
    return parent_ ? parent_->object(name) : nullptr;
}

bool Scope::empty() const noexcept {
    return values_.empty() && aliases_.empty() && objects_.empty() && (!parent_ || parent_->empty());
}

std::vector<std::string> Scope::names() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : aliases_) out.push_back(k);
    for (const auto& [k, v] : values_) out.push_back(k);
    for (const auto& [k, v] : objects_) out.push_back(k);
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
    std::set<std::string> lambdaParams;     // 1.12.20 : les parametres des LAMBDA lues (en majuscules)
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
            // 1.11.24 : un repere autour du membre ($V[0]$.$Nom$) ne le fait pas racine.
            std::size_t before = start;
            while (before > 0 && s[before - 1] == '$') --before;
            const bool member = before > 0 && s[before - 1] == '.';
            const bool typed = i < s.size() && s[i] == '#';          // T#5s, TIME#1s, INT#3
            std::size_t k = i;
            while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
            const bool call = k < s.size() && s[k] == '(';
            if (typed) {
                ++i;
                while (i < s.size() && (isIdentChar(s[i]) || s[i] == '.')) ++i;
                continue;
            }
            // 1.12.7 : un template (NUMERIC_LIMITS<UINT>.MAX(), NUMBERS.PI) n'est pas une racine, ni son type.
            if (!member && sim::lib::templateInfo(word)) {
                if (k < s.size() && s[k] == '<') {
                    std::size_t a = k + 1;
                    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
                    std::size_t b = a;
                    while (b < s.size() && (isIdentChar(s[b]) || (s[b] == '.' && b + 1 < s.size() && isIdentStart(s[b + 1])))) ++b;   // 1.13.0 : <Gaz.T_Pompe>
                    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
                    if (b > a && b < s.size() && s[b] == '>') i = b + 1;
                }
                continue;
            }
            // 1.12.20 : LAMBDA (x : REAL) : BOOL => x > 5 - l'en-tete n'a pas de racine (ses types non plus), ses
            // parametres non plus dans le corps ; LAMBDA ... END_LAMBDA (un bloc) : rien jusqu'a END_LAMBDA.
            if (!member && call && upper(word) == "LAMBDA") {
                std::size_t j = k + 1;
                int depth = 1;
                bool naming = true;
                while (j < s.size() && depth > 0) {
                    const char d = s[j];
                    if (d == '(') ++depth;
                    else if (d == ')') --depth;
                    else if (depth == 1 && (d == ';' || d == ',')) naming = true;
                    else if (depth == 1 && d == ':') naming = false;
                    else if (depth == 1 && naming && isIdentStart(d)) {
                        std::size_t e = j;
                        while (e < s.size() && isIdentChar(s[e])) ++e;
                        lambdaParams.insert(upper(s.substr(j, e - j)));
                        j = e;
                        continue;
                    }
                    ++j;
                }
                const auto arrow = s.find("=>", j);
                const auto block = [&] {                       // le debut d'un bloc avant la fleche
                    std::string rest = upper(s.substr(j, arrow == std::string_view::npos ? std::string_view::npos : arrow - j));
                    return rest.find("END_LAMBDA") != std::string::npos || rest.find("RETURN") != std::string::npos
                        || rest.find(":=") != std::string::npos;
                }();
                if (arrow != std::string_view::npos && !block) {
                    i = arrow + 2;                             // le corps : lu comme le reste
                    continue;
                }
                const std::string rest = upper(s.substr(j));
                const auto end = rest.find("END_LAMBDA");
                i = end == std::string::npos ? s.size() : j + end + 10;
                continue;
            }
            if (member || call || kKeywords.count(upper(word)) || lambdaParams.count(upper(word))) continue;
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

// ------------------------------------------------- 1.12.6 : ce qu'elle designe -
namespace {
// Un chemin de variable : des noms, des points, des indices entre crochets (Cuves[i + 1].Niveau).
bool pathLike(std::string_view t) noexcept {
    if (t.empty() || !(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_')) return false;
    int depth = 0;
    for (const char c : t) {
        if (c == '[') { ++depth; continue; }
        if (c == ']') { if (--depth < 0) return false; continue; }
        if (depth > 0) continue;
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return depth == 0;
}
std::string trimmedSource(std::string_view s) {
    std::string t = markers::strip(s, markers::Mode::Expression);
    const auto a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
}
} // namespace

std::string Expression::pathType(sim::Environment& plc, const Scope* scope) const {
    const std::string t = trimmedSource(source_);
    if (!pathLike(t)) return {};
    ReadOnlyEnv env(plc, scope);
    return env.declaredType(t);
}

roles::Shape Expression::shapeOf(sim::Environment& plc, const Scope* scope, const sim::Value* value) const {
    roles::Shape out;
    const std::string path = trimmedSource(source_);
    if (!pathLike(path)) return out;
    ReadOnlyEnv env(plc, scope);
    const std::string type = env.declaredType(path);
    if (type.empty()) return out;
    if (value) {
        std::vector<sim::Environment::EnumItem> items;
        if (sim::isInteger(value->type()) && env.enumItems(type, items))
            for (const auto& it : items)
                if (it.value == value->asInteger()) { out.enumText = it.text.empty() ? it.name : it.text; out.enumName = it.name; break; }
        return out;
    }
    // La valeur ne s'est pas calculee : un agregat ?
    const auto shown = [&](const std::string& p) -> std::string {
        auto v = Expression::compile(p).evaluate(plc, scope);
        return v ? roles::displayText(*v) : std::string("?");
    };
    const std::string up = [&] { std::string u = type; for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return u; }();
    if (auto rich = env.richVariable(path)) {
        out.aggregate = sim::display(*rich);
        const bool list = up.rfind("LIST", 0) == 0 || up.rfind("VECTOR", 0) == 0 || up.rfind("SET ", 0) == 0 || up.rfind("RING[", 0) == 0
                          || up.rfind("RING [", 0) == 0;   // 1.12.7 : SET, RING
        out.aggregateWhy = path + (up.rfind("MAP", 0) == 0 ? " est une MAP : choisis une valeur (" + path + "['cle']) ou son nombre (" + path + ".Count)"
                                   : list ? " est une liste : choisis un \xC3\xA9l\xC3\xA9ment (" + path + "[0]) ou son nombre (" + path + ".Count)"
                                          : " est un objet (" + type + ") : choisis un de ses membres");
        return out;
    }
    std::vector<std::pair<std::string, std::string>> members;
    if (env.structMembers(type, members) && !members.empty()) {
        std::string shownMembers, proposed;
        for (std::size_t k = 0; k < members.size(); ++k) {
            if (k < 8) shownMembers += (shownMembers.empty() ? "" : ", ") + members[k].first + ": " + shown(path + "." + members[k].first);
            if (k < 3) proposed += (proposed.empty() ? "" : ", ") + path + "." + members[k].first;
        }
        out.aggregate = "{" + shownMembers + (members.size() > 8 ? ", ...}" : "}");
        out.aggregateWhy = path + " est une structure (" + type + ") : choisis un de ses membres (" + proposed + (members.size() > 3 ? "..." : "") + ")";
        return out;
    }
    types::Spec spec;
    if (types::parseSpec(type, spec) && spec.array()) {
        std::string items;
        const long long n = std::min<long long>(spec.count(0), 12);
        for (long long k = 0; k < n; ++k) {
            const std::string cell = path + "[" + std::to_string(spec.low[0] + k) + (spec.dims > 1 ? "," + std::to_string(spec.low[1]) : std::string{}) + "]";
            items += (items.empty() ? "" : " ; ") + shown(cell);
        }
        out.aggregate = items + (spec.count(0) > 12 ? " ; ..." : "");
        out.aggregateWhy = path + " est un tableau (" + type + ") : choisis une case (" + path + "[" + std::to_string(spec.low[0])
                         + (spec.dims > 1 ? "," + std::to_string(spec.low[1]) : std::string{}) + "])";
    }
    return out;
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

std::string exactValue(const sim::Value& v) {
    if (v.type() != sim::Type::Real) return formatValue(v);
    char b[64];
    const auto r = std::to_chars(b, b + sizeof b, v.asReal());
    return r.ec == std::errc{} ? std::string(b, r.ptr) : formatValue(v);
}

bool displayedTextKey(std::string_view key) noexcept {
    // 1.12.6 (lot B) : le role Texte de la table des roles (HmiRoles.hpp) - le texte, les libelles,
    // les titres, l'unite, le separateur, les lettres et la boucle d'un instrument, le nom d'un export...
    // Pas « display » (la ligne d'un utilisateur : {nom}, {groupe} sont les siens) ni les listes.
    if (const auto* r = roles::keyRole(key)) return r->role == roles::Role::Text;
    return key == "tooltip" || key == "message";
}

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
        // 1.12.1 : une date, une heure, une date et heure - sans leur prefixe (2026-10-09 14:30:00).
        if (sim::isDateType(v.type())) {
            std::string t = sim::dateText(v.type(), v.asInteger());
            if (v.type() == sim::Type::Dt && t.size() > 10) t[10] = ' ';
            return t;
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
// 1.12.16 : la fin d'un trou {...} - la premiere '}' hors des chaines ('...', "...", $' y est une apostrophe) :
// {FORMAT('{:.1f} bar', P)}. Une chaine qui ne se ferme pas : la premiere '}', comme avant.
std::size_t templateHoleEnd(std::string_view text, std::size_t from) noexcept {
    char quote = 0;
    for (std::size_t k = from; k < text.size(); ++k) {
        const char c = text[k];
        if (quote) {
            if (c == '$' && k + 1 < text.size()) ++k;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') quote = c;
        else if (c == '}') return k;
    }
    return text.find('}', from);
}

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
        const auto close = templateHoleEnd(text, i + 1);
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
        // 1.12.1 : pas celui d'une heure ({Debut = TOD#14:30:00}, DT#2026-10-09-14:30).
        const auto inTimeLiteral = [&](std::size_t at) {
            std::size_t b = at;
            while (b > 0 && (std::isdigit(static_cast<unsigned char>(inside[b - 1])) || inside[b - 1] == ':' || inside[b - 1] == '-'
                             || inside[b - 1] == '.'))
                --b;
            if (b == 0 || inside[b - 1] != '#') return false;
            std::size_t w = b - 1;
            while (w > 0 && (std::isalpha(static_cast<unsigned char>(inside[w - 1])) || inside[w - 1] == '_')) --w;
            std::string word = inside.substr(w, b - 1 - w);
            for (auto& ch : word) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return word == "TOD" || word == "DT" || word == "TIME_OF_DAY" || word == "DATE_AND_TIME";
        };
        if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=' && !inTimeLiteral(colon)) {
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
        if (!v) { out += "###"; continue; }
        // 1.12.6 (lot B) : un trou sans format se lit dans le role Texte - un BOOL Vrai / Faux, un TIME
        // « 1,5 s », une date a la francaise, une enumeration son texte (avant : TRUE, T#1500ms, 1).
        if (p.format.empty()) {
            if (sim::isInteger(v->type()) && v->type() != sim::Type::Time)
                if (const auto shape = p.expr.shapeOf(plc, scope, &*v); !shape.enumText.empty()) { out += shape.enumText; continue; }
            out += roles::displayText(*v);
            continue;
        }
        out += formatValue(*v, p.format);
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
