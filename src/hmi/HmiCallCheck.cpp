// =============================================================================
//  hmi/HmiCallCheck.cpp - 1.11.20 : les appels des fonctions de l'utilisateur,
//  controles comme le moteur les fait (voir l'en-tete)
// =============================================================================
#include "HmiCallCheck.hpp"

#include "HmiEnums.hpp"
#include "HmiModel.hpp"
#include "HmiOverload.hpp"
#include "HmiSymbols.hpp"
#include "HmiTypes.hpp"
#include "../sim/Interpreter.hpp"

#include <cctype>

namespace hmi::callcheck {
namespace {

bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool identStart(char c) noexcept { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<std::string> segments(std::string_view dotted) {
    std::vector<std::string> out;
    std::size_t i = 0;
    for (;;) {
        const auto dot = dotted.find('.', i);
        std::string s(dotted.substr(i, dot == std::string_view::npos ? std::string_view::npos : dot - i));
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        out.push_back(std::move(s));
        if (dot == std::string_view::npos) break;
        i = dot + 1;
    }
    return out;
}

// Le symbole dont le code est controle : le symbole lui-meme, ou celui qui porte la popup.
const View* symbolHere(const Context& c) {
    if (!c.view || !c.project) return nullptr;
    if (isSymbolView(*c.view)) return c.view;
    return popupOwner(*c.project, *c.view);
}

// Le type d'un membre d'un type IHM ("T_Four", "Temperature" -> "REAL") ; vide : inconnu.
std::string memberOf(const Project& p, const std::string& type, const std::string& member) {
    for (const auto& m : types::membersOf(p, type))
        if (same(m.name, member)) return m.type;
    return {};
}

// Les types des noms, pour sim::callSites (rien n'y est lu ni ecrit).
class Typing final : public sim::Environment {
public:
    explicit Typing(const Context& c) : c_(c) {}
    bool read(std::string_view, sim::Value&) override { return false; }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view) override { return false; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}

    std::string declaredType(std::string_view name) override {
        if (!c_.project) return {};
        if (std::string t = types::typeOfPath(*c_.project, name); !t.empty()) return t;      // une variable IHM (membres, cases)
        const auto segs = segments(name);
        if (segs.empty() || segs.front().empty()) return {};
        if (c_.view)
            if (const auto* prm = c_.view->param(segs.front())) {                        // un parametre de la vue
                std::string t = prm->type;
                for (std::size_t k = 1; k < segs.size() && !t.empty(); ++k) t = memberOf(*c_.project, t, segs[k]);
                return t;
            }
        if (c_.plc.rootType) {                                                         // une variable de l'automate
            std::string t = c_.plc.rootType(segs.front());
            for (std::size_t k = 1; k < segs.size() && !t.empty(); ++k) t = c_.plc.memberType ? c_.plc.memberType(t, segs[k]) : std::string{};
            return t;
        }
        return {};
    }
    bool structMembers(std::string_view typeName, std::vector<std::pair<std::string, std::string>>& out) override {
        if (!c_.project) return false;
        if (const auto* e = findEnumeration(*c_.project, typeName)) {
            for (const auto& v : e->values) out.emplace_back(v.name, "#" + std::to_string(v.value));
            return !out.empty();
        }
        for (const auto& m : types::membersOf(*c_.project, typeName)) out.emplace_back(m.name, m.type);
        return !out.empty();
    }
    int overloads(std::string_view name) override { return static_cast<int>(candidates(c_, name).size()); }
    int chooseInner(const std::vector<const sim::Function*>& fs, const std::vector<sim::ArgShape>& args, std::string& why) override {
        std::vector<overload::Signature> sigs;
        for (const auto* f : fs) sigs.push_back(overload::signatureOf(*f));
        const auto c = overload::choose(sigs, overload::argsOf(args));
        if (c.chosen < 0) why = c.why;
        return c.chosen;
    }
    std::string resultType(std::string_view name, const std::vector<sim::ArgShape>& args) override {
        const auto fs = candidates(c_, name);
        if (fs.empty()) return {};
        if (fs.size() == 1) return fs.front()->returnType;
        std::vector<overload::Signature> sigs;
        for (const auto* f : fs) sigs.push_back(overload::signatureOf(*f));
        const auto c = overload::choose(sigs, overload::argsOf(args));
        return c.chosen >= 0 ? sigs[static_cast<std::size_t>(c.chosen)].result : std::string{};
    }

private:
    const Context& c_;
};

// La colonne de l'appel : le `rank`-ieme `callee(` de sa ligne (hors chaines et commentaires),
// le nom ecrit avec son chemin (Vanne_3.Ouvrir).
void locate(std::string_view code, const sim::CallSite& site, Problem& p) {
    std::size_t start = 0;
    for (std::uint32_t l = 1; l < site.line; ++l) {
        const auto nl = code.find('\n', start);
        if (nl == std::string_view::npos) return;
        start = nl + 1;
    }
    const auto end = code.find('\n', start);
    const std::string_view line = code.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    const auto segs = segments(site.callee);
    const std::string& last = segs.back();
    std::uint32_t seen = 0;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '\'' || c == '"') {
            const auto close = line.find(c, i + 1);
            if (close == std::string_view::npos) return;
            i = close + 1;
            continue;
        }
        if (c == '(' && i + 1 < line.size() && line[i + 1] == '*') {
            const auto close = line.find("*)", i + 2);
            if (close == std::string_view::npos) return;
            i = close + 2;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return;
        if (!identStart(c) || (i > 0 && identChar(line[i - 1]))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < line.size() && identChar(line[j])) ++j;
        const std::string_view word = line.substr(i, j - i);
        std::size_t k = j;
        while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
        if (k < line.size() && line[k] == '(' && same(word, last)) {
            // Le chemin ecrit devant : Vue.Vanne_3.Ouvrir
            std::string written(word);
            std::size_t b = i;
            for (;;) {
                std::size_t d = b;
                while (d > 0 && (line[d - 1] == ' ' || line[d - 1] == '\t')) --d;
                if (d == 0 || line[d - 1] != '.') break;
                --d;
                while (d > 0 && (line[d - 1] == ' ' || line[d - 1] == '\t')) --d;
                std::size_t a = d;
                while (a > 0 && identChar(line[a - 1])) --a;
                if (a == d) break;
                written = std::string(line.substr(a, d - a)) + "." + written;
                b = a;
            }
            if (same(written, site.callee) && ++seen == site.rank) {
                p.column = static_cast<int>(i) + 1;
                p.length = static_cast<int>(word.size());
                return;
            }
        }
        i = j;
    }
}

std::vector<Problem> judge(const Context& c, std::string_view code, const std::vector<sim::CallSite>& sites,
                           const std::vector<std::shared_ptr<const sim::Function>>& inner, const sim::Function* self) {
    std::vector<Problem> out;
    for (const auto& site : sites) {
        std::vector<overload::Signature> sigs;
        // Une fonction interne du script d'abord (pas la fonction controlee elle-meme : son nom
        // designe ses surcharges du projet ou du symbole).
        for (const auto& f : inner)
            if (f.get() != self && same(sim::functionName(*f), site.callee)) sigs.push_back(overload::signatureOf(*f));
        if (sigs.empty())
            for (const auto* f : candidates(c, site.callee)) sigs.push_back(overload::signatureOf(*f));
        if (sigs.empty()) continue;
        const auto args = overload::argsOf(site.args);
        const auto choice = overload::choose(sigs, args);
        if (choice.chosen >= 0) {
            // 1.11.21 : la surcharge prise - ses arguments aux conversions interdites (une seule
            // surcharge de cette arite est prise sans regarder les types).
            const auto& chosen = sigs[static_cast<std::size_t>(choice.chosen)];
            const auto bad = overload::typeMisfits(chosen, args);
            if (bad.empty()) continue;
            Problem p;
            p.line = static_cast<int>(site.line);
            p.error = false;
            std::string text;
            for (const auto& m : bad) {
                p.error = p.error || m.error;
                text += (text.empty() ? "" : " ; ") + m.message
                      + (m.error ? " (une E/S : la simulation refuserait l'appel)"
                                 : " (une conversion interdite, que la simulation fait sans rien dire)");
            }
            p.message = (sigs.size() == 1 ? chosen.name : chosen.shape()) + " : " + text;
            locate(code, site, p);
            out.push_back(std::move(p));
            continue;
        }
        Problem p;
        p.line = static_cast<int>(site.line);
        p.error = !choice.uncertain;
        p.message = sigs.size() == 1 ? sigs.front().name + " : " + choice.why : choice.why;
        locate(code, site, p);
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace

std::vector<const HmiFunction*> candidates(const Context& c, std::string_view callee) {
    if (!c.project || callee.empty() || callee.find('#') != std::string_view::npos) return {};
    const auto segs = segments(callee);
    const View* sym = symbolHere(c);
    if (segs.size() == 1) {
        if (sym)
            if (auto fs = symbolFunctions(*sym, segs.front()); !fs.empty()) return fs;
        return c.project->functionsNamed(segs.front());
    }
    if (segs.size() == 2 && same(segs.front(), kSuperName)) return sym ? symbolFunctions(*sym, segs.back()) : std::vector<const HmiFunction*>{};
    // Vanne_3.Ouvrir : une instance de la vue (ou du symbole), des instances imbriquees.
    if (const View* here = sym ? sym : c.view) {
        const View* s = nullptr;
        for (const auto& o : here->objects)
            if (o.kind == Kind::SymbolInstance && same(o.name, segs.front())) s = symbolOf(*c.project, o);
        for (std::size_t k = 1; s && k + 1 < segs.size(); ++k) {
            const View* next = nullptr;
            for (const auto& o : s->objects)
                if (o.kind == Kind::SymbolInstance && same(o.name, segs[k])) next = symbolOf(*c.project, o);
            s = next;
        }
        if (s) return symbolFunctions(*s, segs.back());
    }
    return symbolFunctionsAt(*c.project, callee);                    // Vue.Vanne_3.Ouvrir
}

std::vector<Problem> checkCode(const Context& c, std::string_view code, bool* ok) {
    if (ok) *ok = false;
    // Le corps d'une fonction : lu comme le moteur le lit (FUNCTION Nom : R <corps> END_FUNCTION,
    // ses declarations sur la ligne 1) - ses parametres ont leur type.
    std::string text;
    if (c.function)
        text = "FUNCTION " + c.function->name + (c.function->returnType.empty() ? std::string{} : " : " + c.function->returnType) + " "
             + std::string(code) + "\nEND_FUNCTION\n";
    else
        text.assign(code.begin(), code.end());
    auto parsed = sim::parse(text, "controle", sim::ParseOptions{true});
    if (!parsed) return {};
    if (ok) *ok = true;
    Typing typing(c);
    const auto inner = sim::innerFunctions(**parsed);
    const sim::Function* self = c.function && !inner.empty() ? inner.front().get() : nullptr;
    return judge(c, code, sim::callSites(**parsed, typing), inner, self);
}

std::vector<Problem> checkExpression(const Context& c, std::string_view expression, bool* ok) {
    if (ok) *ok = false;
    auto parsed = sim::parseExpression(expression);
    if (!parsed) return {};
    if (ok) *ok = true;
    Typing typing(c);
    return judge(c, expression, sim::callSites(**parsed, typing), {}, nullptr);
}

} // namespace hmi::callcheck
