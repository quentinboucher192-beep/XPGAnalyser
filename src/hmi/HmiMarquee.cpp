// =============================================================================
//  hmi/HmiMarquee.cpp - 1.12.12 (lot H) : le texte defilant depuis une source
//  (voir HmiMarquee.hpp). Le moteur, lui, lit les messages en marche
//  (Runtime::marqueeMessages, HmiRuntimeMedias.cpp).
// =============================================================================
#include "HmiMarquee.hpp"

#include "HmiEnums.hpp"
#include "HmiPopupParams.hpp"
#include "HmiRoles.hpp"
#include "HmiSources.hpp"
#include "HmiTypeForms.hpp"

#include <cctype>

namespace hmi::marquee {

namespace {

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}
std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string unquoted(std::string t) {
    t = trimmed(t);
    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) t = t.substr(1, t.size() - 2);
    return t;
}
bool textType(std::string_view type) {
    const std::string t = lower(trimmed(type));
    return t == "string" || t == "wstring" || t.rfind("string[", 0) == 0 || t.rfind("string(", 0) == 0;
}
bool listForm(typeform::Form f) {
    using F = typeform::Form;
    return f == F::Array || f == F::Array2D || f == F::List || f == F::Vector || f == F::Set || f == F::Ring;
}

} // namespace

bool isAlarmSource(std::string_view from) noexcept {
    std::string_view t = from;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.remove_suffix(1);
    const auto same = [&](std::string_view w) {
        if (t.size() != w.size()) return false;
        for (std::size_t i = 0; i < t.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(t[i])) != std::tolower(static_cast<unsigned char>(w[i]))) return false;
        return true;
    };
    return same(kAlarmsSource) || same("alarmes");
}

std::string join(const std::vector<std::string>& messages, std::string_view separator) {
    std::string out;
    bool first = true;
    for (const auto& m : messages) {
        if (m.empty()) continue;
        if (!first) out += separator;
        out += m;
        first = false;
    }
    return out;
}

std::vector<std::string> preview(const Project& p, std::string_view fromText, std::string_view group) {
    std::vector<std::string> out;
    const std::string from = roles::nameOf(fromText);
    if (from.empty()) return out;
    if (isAlarmSource(from)) {
        // Les alarmes du projet (de ce groupe), leurs messages tels qu'ecrits : ce qui passera quand elles sonneront.
        const std::string g = roles::nameOf(group);
        for (const auto& a : p.alarms)
            if (g.empty() || a.group == g) out.push_back(a.message.empty() ? a.name : a.message);
        return out;
    }
    std::string name = from;
    std::string after;
    if (const auto j = sources::jokerOf(from)) {
        name = sources::sourceName(*j);
        after = j->after;
    }
    if (const auto* e = findEnumeration(p, name); e && after.empty()) {
        for (const auto& v : e->values) out.push_back(v.text.empty() ? v.name : v.text);
        return out;
    }
    const auto* var = p.variable(name);
    if (!var) return out;
    const auto shape = typeform::decompose(var->type);
    std::vector<std::string> items;
    if (!trimmed(var->initial).empty() && !typeform::literalItems(var->initial, items)) items.clear();
    if (shape.form == typeform::Form::Simple) {
        if (textType(var->type) && !trimmed(var->initial).empty()) out.push_back(unquoted(var->initial));
        return out;
    }
    if (!listForm(shape.form)) return out;
    for (std::size_t i = 0; i < items.size(); ++i)
        out.push_back(after.empty() ? unquoted(items[i]) : std::to_string(i + 1));   // un membre : le numero de l'element
    return out;
}

std::string sourceProblem(const Project& p, const View* view, std::string_view fromText, std::string* info) {
    const std::string from = roles::nameOf(fromText);
    if (from.empty() || isAlarmSource(from)) return {};
    // Un joker : la source et la formule d'un element, comme pour une plume (lot E).
    if (sources::hasJoker(from)) {
        std::string problem;
        (void)sources::sampleOf(p, view, from, &problem);
        return problem;
    }
    // Une enumeration : ses textes.
    if (findEnumeration(p, from)) return {};
    // Un parametre de la vue : son type se connait en marche.
    if (view && view->param(from) && params::expressionType(p, view, from).empty()) return {};
    const std::string type = params::expressionType(p, view, from);
    if (type.empty())
        return "messages depuis : source inconnue \xC2\xAB " + from + " \xC2\xBB (une LIST ou un tableau de textes, un joker Messages[*], "
               "ou \xC2\xAB alarmes en cours \xC2\xBB) ; le texte de l'objet d\xC3\xA9" "filera";
    const auto shape = typeform::decompose(type);
    if (shape.form == typeform::Form::Map) {
        if (info) *info = "messages depuis " + from + " (une MAP) : ses valeurs, dans l'ordre de ses cl\xC3\xA9s";
        return {};
    }
    if (shape.form == typeform::Form::Tuple)
        return "messages depuis : " + from + " est un TUPLE - une LIST ou un tableau de textes est attendu";
    if (!listForm(shape.form)) {
        if (textType(type)) {
            if (info) *info = "messages depuis : " + from + " est une STRING seule - un seul message (une LIST OF STRING en ferait plusieurs)";
            return {};
        }
        return "messages depuis : " + from + " (" + type + ") n'est pas une liste - une LIST ou un tableau de textes est attendu "
               "(ou un joker : " + from + "[*])";
    }
    if (!textType(shape.element) && info)
        *info = "messages depuis " + from + " : ses \xC3\xA9l\xC3\xA9ments (" + trimmed(shape.element)
              + ") ne sont pas des textes ; ils d\xC3\xA9" "fileront comme ils s'affichent";
    return {};
}

} // namespace hmi::marquee
