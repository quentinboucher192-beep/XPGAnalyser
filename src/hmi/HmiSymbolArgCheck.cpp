// =============================================================================
//  hmi/HmiSymbolArgCheck.cpp - 1.12.11 (lot G) : les arguments d'une instance de
//                              symbole. Voir l'en-tete.
// =============================================================================
#include "HmiSymbolArgCheck.hpp"

#include "HmiCheck.hpp"
#include "HmiExpr.hpp"        // Expression, scanRoots
#include "HmiExprCheck.hpp"
#include "HmiMarkers.hpp"
#include "HmiPopupParams.hpp"
#include "HmiRoles.hpp"
#include "HmiRuntime.hpp"     // isVariablePath
#include "HmiSources.hpp"     // un argument a joker (Cuve := Cuves[*]) : celui d'une repetition
#include "HmiSymbols.hpp"

#include <algorithm>
#include <cctype>

namespace hmi::symargs {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string rootOf(std::string_view t) {
    std::size_t i = 0;
    while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
    return std::string(t.substr(0, i));
}

// Un morceau sans := (ni = d'un nom) : un argument sans nom, au rang de son parametre.
bool positional(std::string_view piece) {
    char quote = 0;
    for (std::size_t i = 0; i < piece.size(); ++i) {
        const char c = piece[i];
        if (quote) { if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '=' || (c == ':' && i + 1 < piece.size() && piece[i + 1] == '=')) return false;
    }
    return true;
}

// Ce que le moteur colle tel quel faute de conversion : ni un litteral, ni un nom connu, ni un calcul
// sur des noms connus (la regle de constantArgument, HmiSymbols.cpp). `named` : un argument nomme dont un
// nom est inconnu est deja une erreur de checkSymbols (« variable inexistante ») - pas deux messages.
bool unconverted(const Project& p, std::string_view type, const std::string& a, bool named, std::string& why) {
    const std::string t = trimmed(type);
    if (t.empty() || t == "ANY" || t == "any" || isLiteralArgument(a)) return false;
    if (isVariablePath(a)) {
        if (isKnownName(&p, rootOf(a))) return false;
    } else if (const Expression e = Expression::compile(a); e.valid()) {
        if (a.find('(') != std::string::npos) return false;
        const auto& roots = e.roots();
        if (std::all_of(roots.begin(), roots.end(), [&p](const std::string& r) { return isKnownName(&p, r); })) return false;
    }
    if (named && !scanRoots(a).empty()) return false;
    return argumentLiteral(&p, t, a, &why).empty();
}

} // namespace

void check(const Project& p, const exprcheck::PlcPaths& paths, std::vector<Issue>& out) {
    params::PlcTypes plc;
    plc.rootType = paths.rootType;
    plc.memberType = paths.memberType;
    plc.isType = paths.isStruct;
    const auto warn = [&](const View& v, const Object& o, std::string message) {
        Issue i;
        i.severity = Issue::Severity::Warning;
        i.category = "Symbole";
        i.view = v.id;
        i.object = o.id;
        i.property = "params";
        i.message = std::move(message);
        out.push_back(std::move(i));
    };
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            if (o.kind != Kind::SymbolInstance) continue;
            const View* sv = symbolOf(p, o);
            if (!sv || !isSymbolView(*sv)) continue;   // dit par checkSymbols
            const std::string text = o.text("params");
            // ---- les arguments sans nom : en trop, ou au rang d'un parametre deja nomme
            {
                std::vector<std::string> ranks;
                for (const auto& piece : roles::splitList(text))
                    if (positional(piece)) ranks.push_back(trimmed(piece));
                while (!ranks.empty() && ranks.back().empty()) ranks.pop_back();
                const auto named = parseArguments(text);
                for (std::size_t k = 0; k < ranks.size(); ++k) {
                    if (ranks[k].empty()) continue;
                    if (k >= sv->params.size()) {
                        warn(v, o, std::to_string(ranks.size()) + " arguments sans nom pour " + std::to_string(sv->params.size())
                                       + " param\xC3\xA8tre(s) de " + sv->name + " : " + ranks[k] + " et les suivants sont ignor\xC3\xA9s");
                        break;
                    }
                    const auto& prm = sv->params[k];
                    if (std::any_of(named.begin(), named.end(), [&](const auto& n) { return trimmed(n.first) == prm.name; }))
                        warn(v, o, "l'argument sans nom " + ranks[k] + " (rang " + std::to_string(k + 1) + ") est ignor\xC3\xA9 : "
                                       + prm.name + " est d\xC3\xA9j\xC3\xA0 donn\xC3\xA9 par son nom");
                }
            }
            // ---- chaque parametre : le type de son argument, l'ecriture, la conversion
            const auto given = givenArguments(*sv, text);
            const auto byName = parseArguments(text);
            for (std::size_t k = 0; k < sv->params.size() && k < given.size(); ++k) {
                const auto& prm = sv->params[k];
                const std::string a = trimmed(markers::strip(given[k]));
                if (a.empty() || sources::hasJoker(a)) continue;   // manquant (checkSymbols) ; une repetition (Cuves[*])
                const std::string type = trimmed(prm.type);
                const bool named = std::any_of(byName.begin(), byName.end(), [&](const auto& n) { return trimmed(n.first) == prm.name; });
                std::string why;
                if (unconverted(p, type, a, named, why)) {
                    warn(v, o, "argument " + prm.name + " : " + a + " ne se convertit pas en " + params::normalizedType(type)
                                   + (why.empty() ? std::string{} : " (" + why + ")") + " - il est coll\xC3\xA9 tel quel dans le symbole");
                    continue;
                }
                const std::string effective = effectiveArgument(&p, type, a);
                const std::string got = params::expressionType(p, &v, effective, plc);
                if (!type.empty() && !got.empty() && !params::typeAccepts(type, got))
                    warn(v, o, "le param\xC3\xA8tre " + prm.name + " de " + sv->name + " attend " + params::normalizedType(type) + ", " + a + " est "
                                   + params::normalizedType(got) + " : donne une valeur du bon type");
                if (!isVariablePath(effective) && params::popupWritesParam(*sv, prm.name))
                    warn(v, o, "le symbole " + sv->name + " \xC3\xA9" "crit " + prm.name + " (une action, un champ de saisie, un script), mais " + a
                                   + " n'est pas une variable : l'\xC3\xA9" "criture n'ira nulle part - donne une variable");
            }
        }
}

} // namespace hmi::symargs
