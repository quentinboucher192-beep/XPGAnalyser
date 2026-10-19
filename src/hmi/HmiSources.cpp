#include "HmiSources.hpp"
#include "HmiCharts.hpp"     // la palette des etats
#include "HmiControls.hpp"   // la liste d'etats (Etats depuis)
#include "HmiEnums.hpp"
#include "HmiMarkers.hpp"
#include "HmiRoles.hpp"
#include "HmiTypeForms.hpp"
#include "HmiTypes.hpp"
#include "HmiWidgets.hpp"
#include "HmiVertices.hpp"   // 1.12.12 (lot H) : « Points depuis » d'une ligne, d'un polygone

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
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

// Une valeur de cette cle a-t-elle un joker ? [*] partout ; .* et /* dans une liste de formules (les membres,
// les variables d'un dossier) ; /* dans les vues d'une barre de navigation (un dossier de vues). Un onglet
// « Pompe.* », un groupe « A/* » restent des textes.
bool anyJoker(Kind k, std::string_view key, std::string_view value) {
    const bool formulas = expandsFormulas(k, key);
    const bool navViews = k == Kind::NavBar && key == "views";
    for (const auto& item : roles::splitList(value, ';')) {
        if (hasJoker(item)) return true;
        if (formulas && (memberBase(item) || folderOf(item))) return true;
        if (navViews && folderOf(item)) return true;
    }
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

std::optional<std::string> memberBase(std::string_view text) {
    const std::string t = trimmedOf(text);
    if (t.size() < 3 || t.compare(t.size() - 2, 2, ".*") != 0) return std::nullopt;
    const std::string base = t.substr(0, t.size() - 2);
    if (!pathBefore(base)) return std::nullopt;
    return base;
}

std::vector<TypeMember> leafMembers(const Project& p, std::string_view type) {
    std::vector<TypeMember> members;
    std::function<void(const std::string&, const std::string&, int)> walk = [&](const std::string& t, const std::string& prefix, int depth) {
        for (const auto& m : types::membersOf(p, t)) {
            const std::string path = prefix + m.name;
            types::Spec ms;
            if (types::parseSpec(m.type, ms) && ms.dims == 1 && ms.count(0) <= 16) {
                for (long long k = ms.low[0]; k <= ms.high[0]; ++k) {
                    const std::string at = path + "[" + std::to_string(k) + "]";
                    if (types::isElementary(ms.element) || depth >= 3 || types::membersOf(p, ms.element).empty())
                        members.push_back(TypeMember{at, ms.element});
                    else
                        walk(ms.element, at + ".", depth + 1);
                }
            } else if (!types::isElementary(m.type) && depth < 3 && !types::membersOf(p, m.type).empty()) {
                walk(m.type, path + ".", depth + 1);
            } else {
                members.push_back(TypeMember{path, m.type});
            }
        }
    };
    const std::string t = trimmedOf(type);
    if (!t.empty() && !types::isElementary(t)) walk(t, "", 0);
    return members;
}

std::optional<std::string> folderOf(std::string_view text) {
    const std::string t = trimmedOf(text);
    if (t.size() < 3 || t.compare(t.size() - 2, 2, "/*") != 0) return std::nullopt;
    std::string folder = trimmedOf(std::string_view(t).substr(0, t.size() - 2));
    if (folder.empty() || folder.front() == '/' || folder.back() == '/') return std::nullopt;
    for (const char c : folder)
        if (c == '\'' || c == '"' || c == '(' || c == ')' || c == '[' || c == ']' || c == ':' || c == '=' || c == '*') return std::nullopt;
    return folder;
}

std::vector<std::string> variablesInFolder(const Project& p, std::string_view folder) {
    std::vector<std::string> out;
    const std::string f = trimmedOf(folder);
    if (f.empty()) return out;
    const auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string lf = lower(f);
    for (const auto& v : p.programs.variables) {
        const std::string vf = lower(v.folder);
        if (vf != lf && vf.rfind(lf + "/", 0) != 0) continue;
        if (!types::isElementary(v.type) && !findEnumeration(p, v.type)) continue;   // une structure, un tableau : non
        out.push_back(v.name);
    }
    return out;
}

namespace {
bool inFolder(std::string_view folder, std::string_view wanted) {
    const auto lower = [](std::string_view s) {
        std::string out(s);
        for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::string f = lower(folder), w = lower(wanted);
    return !w.empty() && (f == w || f.rfind(w + "/", 0) == 0);
}
} // namespace

std::vector<std::string> viewsInFolder(const Project& p, std::string_view folder) {
    std::vector<std::string> out;
    const std::string f = trimmedOf(folder);
    for (const auto& v : p.views)
        if (v.role == "vue" && inFolder(v.folder, f)) out.push_back(v.name);
    return out;
}

namespace {
bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Le type d'un chemin qui part d'un type (le parametre d'une popup, d'un symbole : Cuve.Mesures, Cuve etant
// un T_Cuve) : ses membres et ses indices, comme types::typeOfPath ; vide : inconnu.
std::string typeAlong(const Project& p, std::string type, std::string_view rest) {
    std::size_t i = 0;
    while (i < rest.size()) {
        const char c = rest[i];
        if (c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        if (c == '[') {
            int depth = 0;
            std::size_t j = i;
            for (; j < rest.size(); ++j) {
                if (rest[j] == '[') ++depth;
                else if (rest[j] == ']' && --depth == 0) break;
            }
            if (j >= rest.size()) return {};
            types::Spec s;
            if (!types::parseSpec(type, s) || !s.array()) return {};
            type = s.element;
            i = j + 1;
            continue;
        }
        if (c == '.') {
            std::size_t j = i + 1;
            while (j < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[j])) || rest[j] == '_')) ++j;
            const std::string_view member = rest.substr(i + 1, j - i - 1);
            std::string found;
            for (const auto& m : types::membersOf(p, type))
                if (sameName(m.name, member)) found = m.type;
            if (found.empty()) return {};
            type = found;
            i = j;
            continue;
        }
        return {};
    }
    return type;
}

// Le type d'un chemin de la vue : une variable du projet, ou un parametre de la vue (son type declare).
std::string typeInView(const Project& p, const View* view, std::string_view path) {
    const std::string plain = trimmedOf(markers::strip(path, markers::Mode::Expression));
    if (std::string t = types::typeOfPath(p, plain); !t.empty()) return t;
    if (!view) return {};
    const auto cut = plain.find_first_of(".[");
    const auto* prm = view->param(plain.substr(0, cut));
    if (!prm || trimmedOf(prm->type).empty()) return {};
    return typeAlong(p, prm->type, cut == std::string::npos ? std::string_view{} : std::string_view(plain).substr(cut));
}

// Les membres que `.*` parcourt dans un type : un TUPLE, Item1, Item2... ; une structure, ses membres simples.
std::vector<std::string> membersOfType(const Project& p, const std::string& type) {
    std::vector<std::string> out;
    // Un TUPLE (§ 2.3 : une serie par element) : Item1, Item2...
    if (const auto shape = typeform::decompose(type); shape.form == typeform::Form::Tuple) {
        std::size_t n = 1;
        if (!trimmedOf(shape.parameter).empty()) {
            ++n;
            int depth = 0;
            for (const char c : shape.parameter) {
                if (c == '(' || c == '[') ++depth;
                else if (c == ')' || c == ']') --depth;
                else if (c == ',' && depth == 0) ++n;
            }
        }
        for (std::size_t k = 1; k <= n; ++k) out.push_back("Item" + std::to_string(k));
        return out;
    }
    for (const auto& m : leafMembers(p, type)) out.push_back(m.name);
    return out;
}

// LA COURBE XY EN PROFIL (§ 3.4) : une Variable X a joker (Profil[*].X) trace, a chaque echantillon, tous les
// points (X[i], Y[i]) de sa source (le moteur : Runtime::sampleCharts) ; ses courbes Y citent la meme source
// (Profil[*].Y) et ne se deplient pas en une courbe par element.
bool xyProfile(const Object& o) {
    if (o.kind != Kind::XYChart) return false;
    const Prop* x = o.find("xVariable");
    return x && hasJoker(x->expr.empty() ? x->value : x->expr);
}
} // namespace

std::vector<std::string> membersOfPath(const Project& p, std::string_view path) {
    const std::string plain = trimmedOf(markers::strip(path, markers::Mode::Expression));
    std::string type = types::typeOfPath(p, plain);
    // Un element d'une LIST ou d'un VECTOR (Lignes[0], Moteurs[*].* sur une LIST) : le type de ses elements.
    if (type.empty() && !plain.empty() && plain.back() == ']') {
        int depth = 0;
        std::size_t open = std::string::npos;
        for (std::size_t i = plain.size(); i-- > 0;) {
            if (plain[i] == ']') ++depth;
            else if (plain[i] == '[' && --depth == 0) {
                open = i;
                break;
            }
        }
        if (open != std::string::npos && open > 0) {
            const auto shape = typeform::decompose(types::typeOfPath(p, plain.substr(0, open)));
            if (shape.form == typeform::Form::List || shape.form == typeform::Form::Vector) type = shape.element;
        }
    }
    return membersOfType(p, type);
}

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

std::optional<std::pair<std::string, std::string>> xyReferenceSource(std::string_view value) {
    const std::string t = trimmedOf(value);
    if (t.find("[*]") == std::string::npos) return std::nullopt;
    // La virgule qui separe X et Y : hors crochets, parentheses et chaines.
    int depth = 0;
    char quote = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') quote = c;
        else if (c == '[' || c == '(') ++depth;
        else if (c == ']' || c == ')') --depth;
        else if (c == ',' && depth == 0) {
            const std::string x = trimmedOf(std::string_view(t).substr(0, i)), y = trimmedOf(std::string_view(t).substr(i + 1));
            if (x.empty() || y.empty() || !jokerOf(x)) return std::nullopt;
            return std::make_pair(x, y);
        }
    }
    return std::nullopt;
}

bool hasStatesFrom(const Object& o) {
    if (o.kind != Kind::MultiStateText && o.kind != Kind::MultiStateIndicator && o.kind != Kind::StateChart) return false;
    const Prop* p = o.find(kStatesFromKey);
    return p && p->expr.empty() && !trimmedOf(p->value).empty();
}

bool objectHasJoker(const Object& o) {
    if (repeats(o) || hasStatesFrom(o)) return true;
    if (vertices::hasPointsFrom(o)) return true;   // 1.12.12 (lot H) : ses points viennent d'une source
    if (o.kind == Kind::XYChart)
        if (const Prop* r = o.find("reference"); r && r->expr.empty() && xyReferenceSource(r->value)) return true;
    for (const auto& p : o.props) {
        if (!p.expr.empty() || (p.value.find("[*]") == std::string::npos && p.value.find('*') == std::string::npos)) continue;
        if ((expandsFormulas(o.kind, p.key) || expandsTexts(o.kind, p.key)) && anyJoker(o.kind, p.key, p.value)) return true;
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
    if (hasStatesFrom(o)) add(roles::nameOf(o.text(kStatesFromKey)));
    // 1.12.12 (lot H) : « Points depuis » - la source de ses points.
    if (vertices::hasPointsFrom(o))
        if (const auto ps = vertices::pointsSourceOf(o.text(vertices::kPointsFromKey)))
            if (const auto j = jokerOf(ps->x)) add(sourceName(*j));
    for (const auto& p : o.props) {
        if (!p.expr.empty() || p.value.find('*') == std::string::npos) continue;
        if (!expandsFormulas(o.kind, p.key) && !expandsTexts(o.kind, p.key) && p.key != "names" && p.key != "labels") continue;
        for (const auto& item : roles::splitList(p.value, ';')) {
            if (const auto j = jokerOf(item)) add(sourceName(*j));
            else if (const auto base = memberBase(item)) add(trimmedOf(markers::strip(*base, markers::Mode::Expression)) + ".*");
            else if (const auto folder = folderOf(item)) add(o.kind == Kind::NavBar && p.key == "views" ? std::string(kViewFolderPrefix) + *folder : *folder + "/*");
        }
    }
    return out;
}

bool expandObject(Object& o, const SourceFn& source, const TextFn& text) {
    bool changed = false;
    // ---- les listes de formules, et leurs listes paralleles
    // 1.12.7 (lot E) : Moteurs[*].* - le joker donne Moteurs[1].*, Moteurs[2].*..., que le second passage
    // deplie en membres (Moteurs[1].Marche, Moteurs[1].Debit...).
    const auto formulasPass = [&]() {
        bool any = false;
        if (xyProfile(o)) return false;   // un profil : Profil[*].Y reste une courbe (tous les points)
        for (const char* key : {"variables", "references"}) {
            if (!expandsFormulas(o.kind, key)) continue;
            Prop* p = o.find(key);
            if (!p || !p->expr.empty() || !anyJoker(o.kind, key, p->value)) continue;
            const bool main = std::string_view(key) == "variables";
            const auto items = roles::splitList(p->value, ';');
            const auto names = main ? roles::splitList(o.text("names"), ';') : std::vector<std::string>{};
            const auto colors = main ? roles::splitList(o.text("colors"), ';') : std::vector<std::string>{};
            const auto units = main ? roles::splitList(o.text("units"), ';') : std::vector<std::string>{};
            const auto formats = main ? roles::splitList(o.text("formats"), ';') : std::vector<std::string>{};
            // 1.12.16 : la condition de chaque plume d'une courbe - un joker suit l'element (Cuves[*].Actif), sinon elle se repete.
            const auto visible = main ? roles::splitList(o.text("penVisible"), ';') : std::vector<std::string>{};
            std::vector<std::string> outE, outN, outC, outU, outF, outV;
            for (std::size_t i = 0; i < items.size(); ++i) {
                // 1.12.7 (lot E) : Pompe.* - un element par membre de la structure ; Ligne/Convoyeur/* - un element
                // par variable du dossier (leur source : le projet).
                const auto base = memberBase(items[i]);
                const auto folder = base ? std::nullopt : folderOf(items[i]);
                if (base || folder) {
                    const auto src = source ? source(base ? *base + ".*" : *folder + "/*") : std::nullopt;
                    if (!src) continue;
                    const std::string name = at(names, i);
                    for (std::size_t k = 0; k < src->indices.size(); ++k) {
                        outE.push_back(base ? *base + "." + src->indices[k] : src->indices[k]);
                        outN.push_back(name.empty() ? src->indices[k] : roles::nameOf(name) + " " + src->indices[k]);
                        outC.emplace_back();
                        outU.push_back(at(units, i));
                        outF.push_back(at(formats, i));
                        outV.push_back(at(visible, i));
                    }
                    continue;
                }
                const auto j = jokerOf(items[i]);
                if (!j) {
                    outE.push_back(items[i]);
                    outN.push_back(at(names, i));
                    outC.push_back(at(colors, i));
                    outU.push_back(at(units, i));
                    outF.push_back(at(formats, i));
                    outV.push_back(at(visible, i));
                    continue;
                }
                const auto src = source ? source(sourceName(*j)) : std::nullopt;
                if (!src) continue;   // une source inconnue : aucun element (Generer le dit)
                const std::string name = at(names, i);
                const auto nameJoker = jokerOf(name);
                const std::string cond = at(visible, i);
                const auto condJoker = jokerOf(cond);
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
                    } else if ((!index.empty() && index.front() == '\'') || trimmedOf(j->after) == ".*") {
                        // une MAP : la cle nomme sa barre, sa part (cle = libelle, valeur = barre) ; Moteurs[*].* : le
                        // numero de l'element precede ses membres (1 Marche, 1 Debit, 2 Marche...)
                        shown = label;
                    }
                    outN.push_back(std::move(shown));
                    outC.emplace_back();          // la palette tourne
                    outU.push_back(at(units, i));  // l'unite et le format se repetent
                    outF.push_back(at(formats, i));
                    outV.push_back(condJoker ? itemAt(*condJoker, index, src->enumeration) : cond);
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
                if (!visible.empty()) o.set("penVisible", anyOf(outV) ? joinSemicolons(outV) : std::string{});
            }
            any = true;
        }
        return any;
    };
    if (formulasPass()) {
        changed = true;
        (void)formulasPass();
    }
    // ---- les listes de noms et de textes (vues, zones, onglets)
    for (const char* key : {"views", "groups", "tabs"}) {
        if (!expandsTexts(o.kind, key)) continue;
        Prop* p = o.find(key);
        if (!p || !p->expr.empty() || !anyJoker(o.kind, key, p->value)) continue;
        const auto items = roles::splitList(p->value, ';');
        const bool nav = o.kind == Kind::NavBar;
        const auto labels = nav ? roles::splitList(o.text("labels"), ';') : std::vector<std::string>{};
        std::vector<std::string> out, outL;
        for (std::size_t i = 0; i < items.size(); ++i) {
            // 1.12.7 (lot E) : Process/* - les vues ordinaires d'un dossier de vues (une barre de navigation).
            if (const auto folder = nav ? folderOf(items[i]) : std::nullopt) {
                const auto src = source ? source(std::string(kViewFolderPrefix) + *folder) : std::nullopt;
                if (!src) continue;
                for (const auto& name : src->indices) {
                    out.push_back(name);
                    outL.emplace_back();   // le libelle : le nom de la vue (Vue_Ligne_1 : Ligne 1)
                }
                continue;
            }
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
    // ---- 1.12.7 (lot E, § 3.4) : la reference d'une courbe XY depuis une source - x,y x,y... en marche (il faut
    // les valeurs : sans `text`, l'apercu, elle reste telle qu'ecrite).
    if (o.kind == Kind::XYChart && text)
        if (Prop* ref = o.find("reference"); ref && ref->expr.empty())
            if (const auto xy = xyReferenceSource(ref->value)) {
                const auto xj = jokerOf(xy->first);
                const auto yj = jokerOf(xy->second);
                const auto src = source && xj ? source(sourceName(*xj)) : std::nullopt;
                std::string points;
                for (std::size_t k = 0; src && k < src->indices.size(); ++k) {
                    const std::string& index = src->indices[k];
                    const auto tx = text(itemAt(*xj, index, src->enumeration));
                    const auto ty = text(yj ? itemAt(*yj, index, src->enumeration) : xy->second);
                    double x = 0, y = 0;
                    if (tx && ty && parseNumber(*tx, x) && parseNumber(*ty, y))
                        points += (points.empty() ? "" : " ") + formatNumber(x) + "," + formatNumber(y);
                }
                ref->value = points;
                changed = true;
            }
    // ---- 1.12.12 (lot H) : « Points depuis » - les points d'une ligne, d'un polygone lus dans une source, en marche
    // (sans `text`, l'apercu : l'editeur les dessine depuis les valeurs initiales, vertices::previewPoints). Une
    // source qui ne se lit pas : aucun point (jamais une erreur de marche).
    if (vertices::hasPointsFrom(o) && text) {
        std::vector<vertices::Vertex> pts;
        if (const auto read = vertices::pointsFromSource(o, source, text))
            for (const auto& p : *read) pts.push_back(vertices::Vertex{p.x, p.y, {}, {}});
        o.set("points", vertices::format(pts));
        changed = true;
    }
    // ---- 1.12.7 (lot E) : « Etats depuis » - la liste d'etats lue dans une source
    if (hasStatesFrom(o)) {
        const std::string name = roles::nameOf(o.text(kStatesFromKey));
        const auto src = source ? source(name) : std::nullopt;
        const auto written = parseStateList(o.text("stateList"));
        const bool chart = o.kind == Kind::StateChart;
        const auto& palette = chartPalette();
        const auto clean = [](std::string t) {   // un ';' ou un '|' couperait la liste d'etats
            std::replace(t.begin(), t.end(), ';', ',');
            std::replace(t.begin(), t.end(), '|', '/');
            return t;
        };
        std::vector<StateEntry> states;
        for (std::size_t k = 0; src && k < src->indices.size(); ++k) {
            const std::string& index = src->indices[k];
            const std::string label = k < src->labels.size() ? src->labels[k] : index;
            StateEntry e;
            if (src->enumeration) {
                // T_MODE#Auto : un multi-etats la reconnait par son nom, un chronogramme trace son nombre.
                const auto hash = index.find('#');
                const auto number = chart && text ? text(index) : std::nullopt;
                e.match = chart ? (number ? *number : std::to_string(k)) : index.substr(hash == std::string::npos ? 0 : hash + 1);
                e.text = label;
            } else {
                // Une MAP : sa cle (le texte d'une cle 'vis', le nombre d'une cle 3) ; une LIST, un tableau : l'indice.
                e.match = !index.empty() && index.front() == '\'' ? label : index;
                const auto t = text ? text(name + "[" + index + "]") : std::nullopt;
                e.text = t ? *t : label;
            }
            e.match = clean(e.match);
            e.text = clean(e.text);
            for (const auto& w : written)
                if (w.match == e.match) {
                    e.color = w.color;
                    e.blink = w.blink;
                }
            if (e.color.empty() && !palette.empty()) e.color = palette[k % palette.size()];
            states.push_back(std::move(e));
        }
        for (const auto& w : written)
            if (w.match == "*") states.push_back(w);   // le « sinon » reste le dernier
        o.set("stateList", formatStateList(states));
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
        if (n.size() > 2 && n.compare(n.size() - 2, 2, ".*") == 0) {
            const auto members = membersOfPath(p, n.substr(0, n.size() - 2));
            if (members.empty()) return std::nullopt;
            s.indices = members;
            s.labels = members;
            return s;
        }
        if (const auto folder = folderOf(n)) {
            const auto vars = variablesInFolder(p, *folder);
            if (vars.empty()) return std::nullopt;
            s.indices = vars;
            s.labels = vars;
            return s;
        }
        if (n.rfind(kViewFolderPrefix, 0) == 0) {
            s.indices = viewsInFolder(p, std::string_view(n).substr(kViewFolderPrefix.size()));
            s.labels = s.indices;
            return s;
        }
        if (const auto* e = findEnumeration(p, n)) {
            s.enumeration = true;
            for (const auto& v : e->values) {
                s.indices.push_back(e->name + "#" + v.name);
                s.labels.push_back(v.text.empty() ? v.name : v.text);
            }
            return s;
        }
        // Une variable IHM, ou un chemin dans l'une d'elles (Lignes[2].Mesures, Four1.Vannes) : le type ecrit.
        const auto* var = p.variable(n);
        const std::string type = var ? var->type : types::typeOfPath(p, n);
        if (type.empty()) return std::nullopt;
        types::Spec sp;
        if (!types::parseSpec(type, sp) || !sp.array()) return std::nullopt;
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

SourceFn previewSources(const Project& p) {
    return [&p](std::string_view name) -> std::optional<Source> {
        if (auto s = projectSources(p)(name)) return s;
        const std::string n = trimmedOf(name);
        const auto* var = p.variable(n);
        if (!var) return std::nullopt;
        const auto shape = typeform::decompose(var->type);
        if (shape.form != typeform::Form::List && shape.form != typeform::Form::Vector && shape.form != typeform::Form::Map) return std::nullopt;
        std::vector<std::string> items;
        if (!trimmedOf(var->initial).empty() && !typeform::literalItems(var->initial, items)) return std::nullopt;
        const auto unquoted = [](std::string t) {
            t = trimmedOf(t);
            if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) t = t.substr(1, t.size() - 2);
            return t;
        };
        Source s;
        if (shape.form == typeform::Form::Map) {
            std::vector<std::string> keys;
            for (const auto& it : items) {
                const auto at = it.find(":=");
                if (at != std::string::npos) keys.push_back(trimmedOf(std::string_view(it).substr(0, at)));
            }
            std::sort(keys.begin(), keys.end(), [&](const std::string& a, const std::string& b) { return unquoted(a) < unquoted(b); });
            for (const auto& k : keys) {
                s.indices.push_back(k);   // 'vis' (une cle texte, entre apostrophes) ou 3
                s.labels.push_back(unquoted(k));
            }
            return s;
        }
        const bool texts = trimmedOf(shape.element) == "STRING" || trimmedOf(shape.element) == "WSTRING";
        for (std::size_t i = 0; i < items.size(); ++i) {
            s.indices.push_back(std::to_string(i));
            s.labels.push_back(texts ? unquoted(items[i]) : std::to_string(i + 1));
        }
        return s;
    };
}

bool previewObject(const Project& p, Object& o) {
    if (o.kind == Kind::SymbolInstance || !objectHasJoker(o)) return false;
    // Une source que l'editeur ne connait pas (le parametre d'un symbole, d'une popup : connu en marche) : l'objet
    // reste tel quel (sinon l'apercu perdrait la ligne).
    bool missing = false;
    const SourceFn known = previewSources(p);
    const SourceFn source = [&](std::string_view name) {
        auto s = known(name);
        if (!s) missing = true;
        return s;
    };
    Object shown = o;
    if (!expandObject(shown, source, nullptr) || missing) return false;
    o = std::move(shown);
    return true;
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
    if (view && view->param(root)) {
        // Son type declare dit ses bornes (Cuve.Mesures : ARRAY[1..3] - le premier element est 1, pas 0).
        types::Spec sp;
        if (types::parseSpec(typeInView(p, view, n), sp) && sp.array())
            first = sp.dims == 1 ? std::to_string(sp.low[0]) : std::to_string(sp.low[0]) + "," + std::to_string(sp.low[1]);
        return {};
    }
    if (dot != std::string::npos && p.variable(root)) return {};
    return "source inconnue : " + n + " (le joker [*] ne donnera aucun \xC3\xA9l\xC3\xA9ment)";
}

namespace {
// Les membres que `base.*` parcourt : ceux d'un chemin du projet ; un parametre de la vue (une popup qui
// recoit une structure : Cuve.*), ceux de son type. `later` : un parametre sans type connu, ou un chemin
// dans un parametre (Cuve.Pompe.*) - ses membres se connaissent en marche.
std::vector<std::string> membersFor(const Project& p, const View* view, std::string_view base, bool& later) {
    later = false;
    auto members = membersOfPath(p, base);
    if (members.empty() && view) {
        const std::string plain = trimmedOf(markers::strip(base, markers::Mode::Expression));
        if (view->param(plain.substr(0, plain.find_first_of(".[")))) {
            members = membersOfType(p, typeInView(p, view, plain));   // Cuve.*, Cuve.Pompe.* : selon son type
            later = members.empty();
        }
    }
    return members;
}

std::string counted(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
} // namespace

std::optional<std::string> sampleOf(const Project& p, const View* view, std::string_view item, std::string* problem) {
    if (problem) problem->clear();
    if (const auto base = memberBase(item)) {
        bool later = false;
        const auto members = membersFor(p, view, *base, later);
        if (later) return std::string(*base);   // en marche
        if (members.empty()) {
            if (problem) *problem = trimmedOf(markers::strip(*base, markers::Mode::Expression))
                                  + " n'est pas une structure : le joker .* parcourt les membres d'une structure";
            return std::string(*base);
        }
        return *base + "." + members.front();
    }
    if (const auto folder = folderOf(item)) {
        const auto vars = variablesInFolder(p, *folder);
        if (vars.empty()) {
            if (problem) *problem = "le dossier " + *folder + " n'a aucune variable simple : le joker /* ne donnera aucun \xC3\xA9l\xC3\xA9ment";
            return std::string{};
        }
        return vars.front();
    }
    const auto j = jokerOf(item);
    if (!j) return std::nullopt;
    std::string first;
    const std::string why = sourceProblem(p, view, sourceName(*j), first);
    if (problem) *problem = why;
    std::string sample = itemAt(*j, first, first.find('#') != std::string::npos);
    if (why.empty() && memberBase(sample)) return sampleOf(p, view, sample, problem);   // Moteurs[*].* : un membre du premier
    return sample;
}

std::string jokerSummary(const Project& p, const View* view, std::string_view item) {
    if (const auto base = memberBase(item)) {
        const std::string plain = trimmedOf(markers::strip(*base, markers::Mode::Expression));
        bool later = false;
        const auto members = membersFor(p, view, *base, later);
        if (later) return ".* : les membres de " + plain + " (connus en marche)";
        if (members.empty()) return plain + " n'est pas une structure : le joker .* ne donnera aucun \xC3\xA9l\xC3\xA9ment";
        return ".* : " + counted(members.size(), "membre", "membres") + " de " + plain;
    }
    if (const auto folder = folderOf(item)) {
        const auto vars = variablesInFolder(p, *folder);
        if (vars.empty()) return "le dossier " + *folder + " n'a aucune variable simple : le joker /* ne donnera aucun \xC3\xA9l\xC3\xA9ment";
        return "/* : " + counted(vars.size(), "variable", "variables") + " du dossier " + *folder;
    }
    const auto j = jokerOf(item);
    if (!j) return {};
    const std::string name = sourceName(*j);
    std::string first;
    if (auto why = sourceProblem(p, view, name, first); !why.empty()) return why;
    if (const auto s = projectSources(p)(name))
        return "[*] : " + counted(s->indices.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + " de " + name;
    return "[*] : les \xC3\xA9l\xC3\xA9ments de " + name + " (leur nombre suit la source en marche)";
}

std::vector<std::string> jokerSuggestions(const Project& p, const View* view) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    const auto add = [&](std::string s) {
        if (out.size() < 400 && seen.insert(s).second) out.push_back(std::move(s));
    };
    // Les elements d'un conteneur : ses valeurs simples, ou chaque membre simple de ses structures.
    const auto elements = [&](const std::string& base, const std::string& element) {
        const auto leaves = leafMembers(p, element);
        if (leaves.empty()) add(base + "[*]");
        for (const auto& m : leaves) add(base + "[*]." + m.name);
    };
    if (view)
        for (const auto& prm : view->params)
            if (!leafMembers(p, prm.type).empty()) add(prm.name + ".*");
    std::set<std::string> folders;
    for (const auto& v : p.programs.variables) {
        const auto shape = typeform::decompose(v.type);
        switch (shape.form) {
            case typeform::Form::Array: case typeform::Form::Array2D: case typeform::Form::List: case typeform::Form::Vector:
                elements(v.name, shape.element);
                break;
            case typeform::Form::Map:
                add(v.name + "[*]");
                break;
            case typeform::Form::Tuple:
                add(v.name + ".*");   // Item1, Item2...
                break;
            case typeform::Form::Simple:
                if (!leafMembers(p, v.type).empty()) add(v.name + ".*");
                break;
            default:
                break;
        }
        if (!v.folder.empty() && (types::isElementary(v.type) || findEnumeration(p, v.type)))
            for (std::string f = v.folder; !f.empty();) {   // le dossier, et ceux qui le contiennent
                folders.insert(f);
                const auto slash = f.rfind('/');
                if (slash == std::string::npos) break;
                f.erase(slash);
            }
    }
    for (const auto& f : folders) add(f + "/*");
    return out;
}

namespace {
// Chaque element d'une liste a;b;c passe par `f` (nullopt : il reste tel qu'ecrit) ; vrai : un a change.
bool rewriteItems(std::string& value, const std::function<std::optional<std::string>(const std::string&)>& f) {
    std::string rebuilt;
    bool changed = false;
    bool first = true;
    for (const auto& item : roles::splitListRaw(value)) {
        std::string next = item;
        if (auto r = f(item)) {
            next = std::move(*r);
            changed = changed || next != item;
        }
        rebuilt += (first ? "" : ";") + next;
        first = false;
    }
    if (changed) value = std::move(rebuilt);
    return changed;
}
} // namespace

std::size_t renameFolderJokers(Project& p, bool views, std::string_view from, std::string_view to) {
    const std::string f = trimmedOf(from), t = trimmedOf(to);
    if (f.empty() || t.empty()) return 0;
    const auto moved = [&](const std::string& folder) -> std::optional<std::string> {
        if (sameName(folder, f)) return t;
        if (folder.size() > f.size() && folder[f.size()] == '/' && sameName(std::string_view(folder).substr(0, f.size()), f))
            return t + folder.substr(f.size());
        return std::nullopt;
    };
    std::size_t n = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            const char* key = views ? (o.kind == Kind::NavBar ? "views" : nullptr) : (expandsFormulas(o.kind, "variables") ? "variables" : nullptr);
            Prop* pr = key ? o.find(key) : nullptr;
            if (!pr || !pr->expr.empty() || pr->value.find("/*") == std::string::npos) continue;
            if (rewriteItems(pr->value, [&](const std::string& item) -> std::optional<std::string> {
                    const auto folder = folderOf(item);
                    if (!folder) return std::nullopt;
                    const auto m = moved(*folder);
                    return m ? std::optional<std::string>(*m + "/*") : std::nullopt;
                }))
                ++n;
        }
    return n;
}

std::size_t renameSourceJokers(Project& p, std::string_view from, std::string_view to) {
    const std::string f = trimmedOf(from), t = trimmedOf(to);
    if (f.empty() || t.empty() || f == t) return 0;
    const auto renamed = [&](const std::string& item) -> std::optional<std::string> {
        const auto j = jokerOf(item);
        if (!j || !sameName(trimmedOf(j->before), f)) return std::nullopt;   // un repere $T_MODE$ reste
        const auto lead = item.find_first_not_of(" \t");
        return item.substr(0, lead == std::string::npos ? 0 : lead) + t + "[*]" + j->after;
    };
    std::size_t n = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            bool changed = false;
            for (auto& pr : o.props) {
                if (!pr.expr.empty()) continue;
                if (pr.key == "itemsFrom" || pr.key == "rowsFrom" || pr.key == kRepeatFromKey || pr.key == kStatesFromKey) {
                    // une source nommee telle quelle (Elements depuis T_MODE)
                    if (sameName(trimmedOf(pr.value), f)) {
                        pr.value = t;
                        changed = true;
                    } else if (const auto r = renamed(pr.value)) {
                        pr.value = *r;
                        changed = true;
                    }
                    continue;
                }
                const bool lists = expandsFormulas(o.kind, pr.key) || expandsTexts(o.kind, pr.key) || pr.key == "names" || pr.key == "labels";
                if (lists && pr.value.find("[*]") != std::string::npos && rewriteItems(pr.value, renamed)) changed = true;
            }
            if (changed) ++n;
        }
    return n;
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

std::vector<Object> previewRepeat(const Project& p, const Object& o) {
    if (!repeats(o)) return {};
    View one;
    one.objects.push_back(o);
    if (!repeatInstances(one, previewSources(p))) return {};
    return std::move(one.objects);
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
        const double perRowAsked = o.number(kRepeatPerRowKey, 0);
        const std::size_t perRow = perRowAsked >= 1 ? static_cast<std::size_t>(perRowAsked) : 0;   // 0 : une seule rangee
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
                // Le rang dans la rangee, et la rangee (perRow : apres tant de copies, la suivante).
                const std::size_t along = perRow ? k % perRow : k;
                const std::size_t across = perRow ? k / perRow : 0;
                const double step = static_cast<double>(along) * ((vertical ? b.h : b.w) + gap);
                const double shift = static_cast<double>(across) * ((vertical ? b.w : b.h) + gap);
                c.setNumber("x", b.x + (vertical ? shift : step));
                c.setNumber("y", b.y + (vertical ? step : shift));
            }
            out.push_back(std::move(c));
        }
    }
    v.objects = std::move(out);
    return true;
}

} // namespace hmi::sources
