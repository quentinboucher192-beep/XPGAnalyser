// =============================================================================
//  hmi/HmiOverload.cpp - 1.11.20 : les signatures et les surcharges (voir l'en-tete)
// =============================================================================
#include "HmiOverload.hpp"

#include "HmiDecl.hpp"
#include "HmiModel.hpp"
#include "HmiScript.hpp"
#include "HmiTypeRegistry.hpp"
#include "../sim/Interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace hmi::overload {
namespace tr = hmi::typereg;
namespace {

bool sameName(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string quoted(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }

// Les arguments rapproches des parametres : slots[k] = l'indice de l'argument du parametre k
// (-1 : non donne). Vide : l'appel remplit la signature ; sinon la raison.
std::string bind(const Signature& sig, const std::vector<Arg>& args, std::vector<int>& slots) {
    const std::size_t n = sig.params.size();
    slots.assign(n, -1);
    std::size_t next = 0, positional = 0;
    for (const auto& a : args) positional += a.name.empty() ? 1 : 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Arg& a = args[i];
        std::size_t k = n;
        if (a.name.empty()) {
            while (next < n && slots[next] >= 0) ++next;
            if (next >= n)
                return "trop d'arguments (" + std::to_string(positional) + " pour " + std::to_string(n) + ")";
            k = next++;
        } else {
            for (std::size_t j = 0; j < n; ++j)
                if (sameName(sig.params[j].name, a.name)) k = j;
            if (k == n) return "param\xC3\xA8tre inconnu : " + a.name;
            if (slots[k] >= 0) return "param\xC3\xA8tre donn\xC3\xA9 deux fois : " + sig.params[k].name;
        }
        const Param& p = sig.params[k];
        if (a.output && p.mode != Mode::Out)
            return "\xC2\xAB => \xC2\xBB est pour une sortie : " + p.name + " n'est pas un param\xC3\xA8tre VAR_OUTPUT";
        if (byReference(p) && !a.designator) {
            const std::string t = tr::comparable(a.type);
            const bool indirect = t.rfind("REF_TO", 0) == 0 || t.rfind("POINTERTO", 0) == 0 || t.rfind("REFERENCETO", 0) == 0;
            if (!(p.mode == Mode::In && indirect)) {
                const char* how = p.mode == Mode::InOut ? "VAR_IN_OUT" : p.mode == Mode::Out ? "VAR_OUTPUT" : "REF_TO";
                return p.name + " est pass\xC3\xA9 par r\xC3\xA9" "f\xC3\xA9rence (" + std::string(how) + ") : il faut une variable"
                       + (a.text.empty() ? std::string{} : ", pas " + quoted(a.text));
            }
        }
        slots[k] = static_cast<int>(i);
    }
    for (std::size_t k = 0; k < n; ++k) {
        if (slots[k] >= 0) continue;
        const Param& p = sig.params[k];
        if (p.mode == Mode::InOut) return "il manque " + p.name + " (VAR_IN_OUT : une variable)";
        if (p.mode == Mode::In && !p.optional) return "il manque l'argument " + p.name + " (" + p.type + ")";
    }
    return {};
}

std::string argTypes(const std::vector<Arg>& args) {
    std::string out;
    for (const auto& a : args) {
        const std::string t = a.literal ? literalType(a.value) : a.type.empty() ? std::string("?") : a.type;
        out += (out.empty() ? "" : ", ") + t;
    }
    return "(" + out + ")";
}

std::string joinShapes(const std::vector<Signature>& c, const std::vector<int>& which) {
    std::string out;
    for (std::size_t i = 0; i < which.size(); ++i) {
        out += (i == 0 ? "" : i + 1 == which.size() ? " et " : ", ");
        out += c[static_cast<std::size_t>(which[i])].shape();
    }
    return out;
}

bool elementary(std::string_view t) { return tr::numericOf(t).family != tr::Family::None; }

} // namespace

std::string_view modeWord(Mode m) noexcept {
    switch (m) {
        case Mode::InOut: return "VAR_IN_OUT ";
        case Mode::Out:   return "VAR_OUTPUT ";
        case Mode::In:    break;
    }
    return "";
}

std::string Signature::text() const {
    std::string s = name + "(";
    for (std::size_t k = 0; k < params.size(); ++k) {
        s += (k ? "; " : "");
        s += modeWord(params[k].mode);
        s += params[k].name + " : " + params[k].type;
    }
    s += ")";
    if (!result.empty()) s += " : " + result;
    return s;
}

std::string Signature::shape() const {
    std::string s = name + "(";
    for (std::size_t k = 0; k < params.size(); ++k) {
        s += (k ? ", " : "");
        s += modeWord(params[k].mode);
        s += params[k].type;
    }
    return s + ")";
}

bool byReference(const Param& p) {
    if (p.mode != Mode::In) return true;
    const std::string t = tr::comparable(p.type);
    return t.rfind("REF_TO", 0) == 0 || t.rfind("REFERENCETO", 0) == 0;
}

std::string literalType(long long value) {
    if (value >= std::numeric_limits<std::int16_t>::min() && value <= std::numeric_limits<std::int16_t>::max()) return "INT";
    if (value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max()) return "DINT";
    return "LINT";
}

int cost(const Arg& a, const Param& p) {
    const std::string to = tr::comparable(p.type);
    if (byReference(p)) {
        if (a.type.empty()) return 0;
        const std::string from = tr::comparable(a.type);
        if (from == to || to == "ANY") return 0;
        if (tr::isNumber(from) && tr::isNumber(to)) return 4;
        // REF_TO T recoit une reference (ou un pointeur) vers le meme T.
        const auto target = [](const std::string& t) {
            for (const char* head : {"REF_TO", "REFERENCETO", "POINTERTO"})
                if (t.rfind(head, 0) == 0) return t.substr(std::string_view(head).size());
            return t;
        };
        if (p.mode == Mode::In && target(from) == target(to)) return 0;
        return -1;
    }
    std::string from = a.type;
    if (a.literal) {
        const std::string natural = literalType(a.value);
        if (tr::isInteger(to) && tr::valueFits(a.value, to)) return to == natural ? 0 : 1;
        if (tr::numericOf(to).family == tr::Family::Real) return 2;
        from = natural;
    }
    if (tr::comparable(from) == "ANY" || from.empty()) return 0;
    const auto v = tr::conversion(from, p.type);
    switch (v.kind) {
        case tr::Conversion::Exact:     return 0;
        case tr::Conversion::Widening:  return v.cost;
        case tr::Conversion::Lossy:     return 4;
        case tr::Conversion::Narrowing: return 8;
        case tr::Conversion::Forbidden: break;
    }
    if (tr::numericOf(from).family == tr::Family::Real && tr::numericOf(to).family == tr::Family::Integer) return 16;
    return -1;
}

std::string misfit(const Signature& sig, const std::vector<Arg>& args) {
    std::vector<int> slots;
    return bind(sig, args, slots);
}

bool sameShape(const Signature& a, const Signature& b) {
    if (a.params.size() != b.params.size()) return false;
    for (std::size_t k = 0; k < a.params.size(); ++k) {
        const Param& x = a.params[k];
        const Param& y = b.params[k];
        if (x.mode != y.mode) return false;
        const std::string tx = tr::comparable(x.type), ty = tr::comparable(y.type);
        if (tx == ty) continue;
        // Deux types de base que le moteur calcule de la meme facon : un appel ne les distinguerait pas.
        if (elementary(tx) && elementary(ty) && tr::simTypeOf(tx) == tr::simTypeOf(ty)) continue;
        return false;
    }
    return true;
}

Choice choose(const std::vector<Signature>& candidates, const std::vector<Arg>& args) {
    Choice out;
    if (candidates.empty()) return out;
    const std::string name = candidates.front().name;
    std::vector<int> viable;
    std::vector<std::vector<int>> slotsOf(candidates.size());
    std::string reasons;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::string why = bind(candidates[i], args, slotsOf[i]);
        if (why.empty()) viable.push_back(static_cast<int>(i));
        else reasons += (reasons.empty() ? "" : " ; ") + candidates[i].shape() + " : " + why;
    }
    if (viable.empty()) {
        out.why = candidates.size() == 1 ? reasons.substr(reasons.find(" : ") + 3)
                                         : "aucune surcharge de " + name + " ne prend cet appel - " + reasons;
        return out;
    }
    if (viable.size() == 1) {
        out.chosen = viable.front();
        return out;
    }
    // Plusieurs : les types.
    struct Scored { int index; int total; bool unknown; };
    std::vector<Scored> scored;
    std::string refused;
    for (const int i : viable) {
        const auto& c = candidates[static_cast<std::size_t>(i)];
        const auto& slots = slotsOf[static_cast<std::size_t>(i)];
        int total = 0;
        bool unknown = false, ok = true;
        for (std::size_t k = 0; k < slots.size() && ok; ++k) {
            if (slots[k] < 0) continue;
            const Arg& a = args[static_cast<std::size_t>(slots[k])];
            const int c1 = cost(a, c.params[k]);
            if (c1 < 0) {
                ok = false;
                const std::string t = a.literal ? literalType(a.value) : a.type;
                refused += (refused.empty() ? "" : " ; ") + c.shape() + " : " + c.params[k].name + " attend un "
                           + c.params[k].type + ", pas un " + t;
                break;
            }
            total += c1;
            unknown = unknown || (a.type.empty() && !a.literal);
        }
        if (ok) scored.push_back({i, total, unknown});
    }
    if (scored.empty()) {
        out.why = "aucune surcharge de " + name + " n'accepte ces types " + argTypes(args) + " - " + refused;
        return out;
    }
    int best = std::numeric_limits<int>::max();
    for (const auto& s : scored) best = std::min(best, s.total);
    bool unknown = false;
    for (const auto& s : scored)
        if (s.total == best) {
            out.tied.push_back(s.index);
            unknown = unknown || s.unknown;
        }
    if (out.tied.size() == 1) {
        out.chosen = out.tied.front();
        out.tied.clear();
        return out;
    }
    if (unknown) {
        out.uncertain = true;
        out.why = joinShapes(candidates, out.tied) + " : le type d'un argument n'est pas connu ici " + argTypes(args)
                  + " - la simulation choisira avec le vrai type";
        return out;
    }
    out.why = "appel ambigu de " + name + " " + argTypes(args) + " : " + joinShapes(candidates, out.tied)
              + " conviennent autant - pr\xC3\xA9" "cisez un type (INT#5, 5.0, TO_REAL(x))";
    return out;
}

Signature signatureOf(const HmiFunction& f) {
    Signature s;
    s.name = f.name;
    s.result = f.returnType;
    s.key = "#" + std::to_string(f.id);
    // Tous les types sont lus (une structure IHM, un tableau) : la forme seule compte ici.
    const auto parts = splitDeclarations(decl::codeOf(f), true, [](std::string_view) { return true; });
    for (const auto* l : parts.parameters()) {
        Param p;
        p.name = l->name;
        p.type = l->type;
        p.mode = l->section == LocalVar::Section::InOut ? Mode::InOut : l->section == LocalVar::Section::Output ? Mode::Out : Mode::In;
        p.optional = p.mode == Mode::Out || (p.mode == Mode::In && !l->initial.empty());
        s.params.push_back(std::move(p));
    }
    return s;
}

Signature signatureOf(const sim::Function& f) {
    Signature s;
    s.name = sim::functionName(f);
    s.result = sim::functionResult(f);
    for (const auto& p : sim::functionParams(f)) {
        Param prm;
        prm.name = p.name;
        prm.type = p.type;
        prm.mode = p.mode == sim::ParamInfo::Mode::InOut ? Mode::InOut : p.mode == sim::ParamInfo::Mode::Output ? Mode::Out : Mode::In;
        prm.optional = prm.mode != Mode::InOut;
        s.params.push_back(std::move(prm));
    }
    return s;
}

std::vector<Arg> argsOf(const std::vector<sim::ArgShape>& shapes) {
    std::vector<Arg> out;
    out.reserve(shapes.size());
    for (const auto& a : shapes) out.push_back(Arg{a.name, a.output, a.type, a.designator, a.literal, static_cast<long long>(a.value), a.text});
    return out;
}

} // namespace hmi::overload
