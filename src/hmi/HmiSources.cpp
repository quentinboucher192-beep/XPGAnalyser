#include "HmiSources.hpp"
#include "HmiEnums.hpp"
#include "HmiMarkers.hpp"
#include "HmiRoles.hpp"
#include "HmiTypeForms.hpp"
#include "HmiTypes.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>

namespace hmi::sources {
namespace {

std::string trimmedOf(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return std::string(s);
}

// Le premier [*] hors d'une chaine '...' ou "..." ; npos : aucun.
std::size_t jokerAt(std::string_view text, std::size_t from = 0) {
    char quote = 0;
    for (std::size_t i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') {
            // Une apostrophe collee a une lettre (l'armoire) n'ouvre pas de chaine.
            const bool glued = i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) || static_cast<unsigned char>(text[i - 1]) >= 0x80);
            if (!glued && text.find(c, i + 1) != std::string_view::npos) quote = c;
            continue;
        }
        if (c == '[' && text.compare(i, 3, "[*]") == 0) return i;
    }
    return std::string_view::npos;
}

bool chartLike(Kind k) noexcept {
    return k == Kind::Trend || k == Kind::BarChart || k == Kind::PieChart || k == Kind::RadarChart || k == Kind::XYChart
        || k == Kind::StateChart || k == Kind::VariableTable;
}

std::string at(const std::vector<std::string>& v, std::size_t i) { return i < v.size() ? v[i] : std::string{}; }

bool anyJoker(std::string_view value) {
    for (const auto& item : roles::splitList(value, ';'))
        if (hasJoker(item)) return true;
    return false;
}

} // namespace

namespace {
// Ce qui precede [*] doit etre un chemin (Cuves, $Cuves$, Lignes[2].Cuves, Recette.Etapes) : un libelle
// d'onglet « Toutes [*] » ou un calcul « SEL(Mode, Cuves[*]...) » ne sont pas des jokers (ils restent tels
// quels, comme avant).
bool pathBefore(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    if (s.empty() || s.back() == ' ' || s.back() == '\t') return false;
    const auto first = static_cast<unsigned char>(s.front());
    if (!(std::isalpha(first) || first == '_' || first == '$' || first >= 0x80)) return false;
    int depth = 0;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '[') ++depth;
        else if (c == ']') --depth;
        else if (c == ' ' || c == ',') {
            if (depth <= 0) return false;
        } else if (!(std::isalnum(c) || c == '_' || c == '.' || c == '$' || c == '-' || c == '+' || c >= 0x80)) return false;
        if (depth < 0) return false;
    }
    return depth == 0;
}
} // namespace

std::optional<Joker> jokerOf(std::string_view text) {
    const auto at = jokerAt(text);
    if (at == std::string_view::npos || !pathBefore(text.substr(0, at))) return std::nullopt;
    return Joker{std::string(text.substr(0, at)), std::string(text.substr(at + 3))};
}

std::string sourceName(const Joker& j) { return trimmedOf(markers::strip(j.before, markers::Mode::Expression)); }

std::string itemAt(const Joker& j, std::string_view index, bool enumValue) {
    if (enumValue) return std::string(index) + j.after;
    return j.before + "[" + std::string(index) + "]" + j.after;
}

bool expandsFormulas(Kind k, std::string_view key) noexcept {
    return (key == "variables" && chartLike(k)) || (key == "references" && k == Kind::RadarChart);
}

bool expandsTexts(Kind k, std::string_view key) noexcept {
    return (k == Kind::NavBar && key == "views") || (k == Kind::AlarmSummary && key == "groups") || (k == Kind::TabContainer && key == "tabs");
}

bool repeats(const Object& o) {
    return o.kind == Kind::SymbolInstance && !roles::nameOf(o.text(kRepeatFromKey)).empty() && o.expr(kRepeatFromKey).empty();
}

bool objectHasJoker(const Object& o) {
    if (repeats(o)) return true;
    for (const auto& p : o.props) {
        if (!p.expr.empty() || p.value.find("[*]") == std::string::npos) continue;
        if ((expandsFormulas(o.kind, p.key) || expandsTexts(o.kind, p.key)) && anyJoker(p.value)) return true;
    }
    return false;
}

bool viewHasJoker(const View& v) {
    return std::any_of(v.objects.begin(), v.objects.end(), [](const Object& o) { return objectHasJoker(o); });
}

std::vector<std::string> sourcesOf(const Object& o) {
    std::vector<std::string> out;
    const auto add = [&](std::string name) {
        if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
    };
    if (repeats(o)) add(roles::nameOf(o.text(kRepeatFromKey)));
    for (const auto& p : o.props) {
        if (!p.expr.empty() || p.value.find("[*]") == std::string::npos) continue;
        if (!expandsFormulas(o.kind, p.key) && !expandsTexts(o.kind, p.key) && p.key != "names" && p.key != "labels") continue;
        for (const auto& item : roles::splitList(p.value, ';'))
            if (const auto j = jokerOf(item)) add(sourceName(*j));
    }
    return out;
}

bool expandObject(Object& o, const SourceFn& source, const TextFn& text) {
    bool changed = false;
    // ---- les listes de formules, et leurs listes paralleles
    for (const char* key : {"variables", "references"}) {
        if (!expandsFormulas(o.kind, key)) continue;
        Prop* p = o.find(key);
        if (!p || !p->expr.empty() || !anyJoker(p->value)) continue;
        const bool main = std::string_view(key) == "variables";
        const auto items = roles::splitList(p->value, ';');
        const auto names = main ? roles::splitList(o.text("names"), ';') : std::vector<std::string>{};
        const auto colors = main ? roles::splitList(o.text("colors"), ';') : std::vector<std::string>{};
        const auto units = main ? roles::splitList(o.text("units"), ';') : std::vector<std::string>{};
        const auto formats = main ? roles::splitList(o.text("formats"), ';') : std::vector<std::string>{};
        std::vector<std::string> outE, outN, outC, outU, outF;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto j = jokerOf(items[i]);
            if (!j) {
                outE.push_back(items[i]);
                outN.push_back(at(names, i));
                outC.push_back(at(colors, i));
                outU.push_back(at(units, i));
                outF.push_back(at(formats, i));
                continue;
            }
            const auto src = source ? source(sourceName(*j)) : std::nullopt;
            if (!src) continue;   // une source inconnue : aucun element (Generer le dit)
            const std::string name = at(names, i);
            const auto nameJoker = jokerOf(name);
            for (std::size_t k = 0; k < src->indices.size(); ++k) {
                const std::string& index = src->indices[k];
                const std::string label = k < src->labels.size() ? src->labels[k] : index;
                outE.push_back(itemAt(*j, index, src->enumeration));
                std::string shown;
                if (nameJoker) {
                    const auto t = text ? text(itemAt(*nameJoker, index, src->enumeration)) : std::nullopt;
                    shown = t ? *t : label;
                } else if (!name.empty()) {
                    shown = roles::nameOf(name) + " " + label;
                } else if (!index.empty() && index.front() == '\'') {
                    shown = label;   // une MAP : la cle nomme sa barre, sa part (cle = libelle, valeur = barre)
                }
                outN.push_back(std::move(shown));
                outC.emplace_back();          // la palette tourne
                outU.push_back(at(units, i));  // l'unite et le format se repetent
                outF.push_back(at(formats, i));
            }
        }
        p->value = joinSemicolons(outE);
        if (main) {
            const auto anyOf = [](const std::vector<std::string>& v) {
                return std::any_of(v.begin(), v.end(), [](const std::string& s) { return !s.empty(); });
            };
            if (anyOf(outN) || o.find("names")) o.set("names", anyOf(outN) ? joinSemicolons(outN) : std::string{});
            if (!colors.empty()) o.set("colors", anyOf(outC) ? joinSemicolons(outC) : std::string{});
            if (!units.empty()) o.set("units", anyOf(outU) ? joinSemicolons(outU) : std::string{});
            if (!formats.empty()) o.set("formats", anyOf(outF) ? joinSemicolons(outF) : std::string{});
        }
        changed = true;
    }
    // ---- les listes de noms et de textes (vues, zones, onglets)
    for (const char* key : {"views", "groups", "tabs"}) {
        if (!expandsTexts(o.kind, key)) continue;
        Prop* p = o.find(key);
        if (!p || !p->expr.empty() || !anyJoker(p->value)) continue;
        const auto items = roles::splitList(p->value, ';');
        const bool nav = o.kind == Kind::NavBar;
        const auto labels = nav ? roles::splitList(o.text("labels"), ';') : std::vector<std::string>{};
        std::vector<std::string> out, outL;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto j = jokerOf(items[i]);
            if (!j) {
                out.push_back(items[i]);
                outL.push_back(at(labels, i));
                continue;
            }
            const auto src = source ? source(sourceName(*j)) : std::nullopt;
            if (!src) continue;
            const auto labelJoker = jokerOf(at(labels, i));
            for (std::size_t k = 0; k < src->indices.size(); ++k) {
                const std::string& index = src->indices[k];
                const std::string label = k < src->labels.size() ? src->labels[k] : index;
                std::string shown;
                if (src->enumeration && trimmedOf(j->after).empty()) {
                    shown = label;   // une enumeration : le texte de sa valeur
                } else {
                    const auto t = text ? text(itemAt(*j, index, src->enumeration)) : std::nullopt;
                    shown = t ? *t : label;
                }
                // un ';' dans un texte calcule couperait la liste : il ne passe pas
                std::replace(shown.begin(), shown.end(), ';', ',');
                out.push_back(std::move(shown));
                std::string l;
                if (labelJoker) {
                    const auto t = text ? text(itemAt(*labelJoker, index, src->enumeration)) : std::nullopt;
                    l = t ? *t : std::string{};
                    std::replace(l.begin(), l.end(), ';', ',');
                }
                outL.push_back(std::move(l));
            }
        }
        p->value = joinSemicolons(out);
        if (nav && o.find("labels")) {
            const bool any = std::any_of(outL.begin(), outL.end(), [](const std::string& s) { return !s.empty(); });
            o.set("labels", any ? joinSemicolons(outL) : std::string{});
        }
        changed = true;
    }
    return changed;
}

std::size_t expandView(View& v, const SourceFn& source, const TextFn& text) {
    std::size_t n = 0;
    for (auto& o : v.objects)
        if (o.kind != Kind::SymbolInstance && expandObject(o, source, text)) ++n;
    return n;
}

SourceFn projectSources(const Project& p) {
    return [&p](std::string_view name) -> std::optional<Source> {
        const std::string n = trimmedOf(name);
        Source s;
        if (const auto* e = findEnumeration(p, n)) {
            s.enumeration = true;
            for (const auto& v : e->values) {
                s.indices.push_back(e->name + "#" + v.name);
                s.labels.push_back(v.text.empty() ? v.name : v.text);
            }
            return s;
        }
        const auto* var = p.variable(n);
        if (!var) return std::nullopt;
        types::Spec sp;
        if (!types::parseSpec(var->type, sp) || !sp.array()) return std::nullopt;
        for (long long i = sp.low[0]; i <= sp.high[0]; ++i) {
            if (sp.dims == 1) {
                s.indices.push_back(std::to_string(i));
                s.labels.push_back(std::to_string(i));
            } else {
                for (long long j = sp.low[1]; j <= sp.high[1]; ++j) {
                    s.indices.push_back(std::to_string(i) + "," + std::to_string(j));
                    s.labels.push_back(std::to_string(i) + "," + std::to_string(j));
                }
            }
        }
        return s;
    };
}

std::string sourceProblem(const Project& p, const View* view, std::string_view name, std::string& first) {
    const std::string n = trimmedOf(name);
    first = "0";
    if (n.empty()) return "le joker [*] suit une source vide : \xC3\xA9" "cris Cuves[*].Niveau";
    if (const auto s = projectSources(p)(n)) {
        if (!s->indices.empty()) first = s->indices.front();
        return {};
    }
    if (const auto* var = p.variable(n)) {
        const auto form = typeform::decompose(var->type).form;
        if (form == typeform::Form::List || form == typeform::Form::Vector) return {};
        if (form == typeform::Form::Map) {
            first = "''";
            return {};
        }
        return n + " (" + var->type + ") n'a pas d'\xC3\xA9l\xC3\xA9ments : le joker [*] parcourt un tableau, une LIST, un VECTOR, "
                   "une MAP ou une \xC3\xA9num\xC3\xA9ration";
    }
    // Un membre (Recette.Etapes), un parametre de la vue (une popup qui recoit la liste) : en marche.
    const auto dot = n.find_first_of(".[");
    const std::string root = n.substr(0, dot);
    if (view && view->param(root)) return {};
    if (dot != std::string::npos && p.variable(root)) return {};
    return "source inconnue : " + n + " (le joker [*] ne donnera aucun \xC3\xA9l\xC3\xA9ment)";
}

std::string replaceJokers(std::string_view text, std::string_view index) {
    std::string out;
    std::size_t from = 0;
    for (std::size_t at = jokerAt(text); at != std::string_view::npos; at = jokerAt(text, from)) {
        out.append(text.substr(from, at - from));
        out += "[" + std::string(index) + "]";
        from = at + 3;
    }
    out.append(text.substr(from));
    return out;
}

std::string sampleFormula(std::string_view text, std::string_view index) {
    const auto j = jokerOf(text);
    return j ? itemAt(*j, index) : std::string(text);
}

bool repeatInstances(View& v, const SourceFn& source) {
    if (std::none_of(v.objects.begin(), v.objects.end(), [](const Object& o) { return repeats(o); })) return false;
    std::set<Id> used;
    for (const auto& o : v.objects) used.insert(o.id);
    for (const auto& sc : v.scripts) used.insert(sc.id);
    std::vector<Object> out;
    out.reserve(v.objects.size() * 2);
    for (const auto& o : v.objects) {
        if (!repeats(o)) {
            out.push_back(o);
            continue;
        }
        const auto src = source ? source(roles::nameOf(o.text(kRepeatFromKey))) : std::nullopt;
        if (!src || src->indices.empty() || src->enumeration) continue;   // rien a poser (Generer le dit)
        const bool vertical = o.text(kRepeatAxisKey, "horizontal").rfind("vert", 0) == 0;
        const double gap = std::max(0.0, o.number(kRepeatGapKey, 10));
        const Box b = o.box();
        for (std::size_t k = 0; k < src->indices.size(); ++k) {
            Object c = o;
            c.set(kRepeatFromKey, "");
            if (Prop* args = c.find("params"); args && args->expr.empty()) args->value = replaceJokers(args->value, src->indices[k]);
            if (k > 0) {
                Id id = static_cast<Id>(0x40000000u | ((static_cast<std::uint32_t>(o.id) & 0xFFFFu) << 12) | (static_cast<std::uint32_t>(k) & 0xFFFu));
                while (used.count(id)) id = static_cast<Id>(0x40000000u | ((static_cast<std::uint32_t>(id) + 1u) & 0x3FFFFFFFu));
                used.insert(id);
                c.id = id;
                c.name = o.name + "_" + std::to_string(k + 1);
                const double step = static_cast<double>(k) * ((vertical ? b.h : b.w) + gap);
                if (vertical) c.setNumber("y", b.y + step);
                else c.setNumber("x", b.x + step);
            }
            out.push_back(std::move(c));
        }
    }
    v.objects = std::move(out);
    return true;
}

} // namespace hmi::sources
