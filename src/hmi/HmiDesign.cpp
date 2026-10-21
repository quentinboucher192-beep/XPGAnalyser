#include "HmiDesign.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : IHM Studio - pas d'automate dans les modeles de pages

#include "HmiEdit.hpp"
#include "HmiOperators.hpp"   // 1.10
#include "HmiEnums.hpp"       // 1.12.13 : l'enumeration du modele Tableaux dynamiques (toString, fromString)
#include "HmiTable.hpp"       // 1.12.13 : ses colonnes dynamiques
#include "HmiWidgets.hpp"     // 1.12.13 : formatCells (ses cases fixes)

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace hmi::design {

namespace {

std::string upperCopy(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le dernier morceau d'un chemin : "Armoires[0].ana.PT1" -> "PT1".
std::string leafOf(std::string_view path) {
    std::size_t end = path.size();
    while (end > 0 && path[end - 1] == ']') {
        const auto open = path.rfind('[', end - 1);
        if (open == std::string_view::npos) break;
        end = open;
    }
    const auto dot = path.substr(0, end).rfind('.');
    return std::string(path.substr(dot == std::string_view::npos ? 0 : dot + 1, end - (dot == std::string_view::npos ? 0 : dot + 1)));
}

// Un nom qui dit ce que l'objet montre : tel quel s'il est libre dans la vue,
// sinon _2, _3... (Voyant_Pompe_Marche, puis Voyant_Pompe_Marche_2).
std::string freshName(const View& v, const std::string& stem, Id self = kNoId) {
    const auto used = [&](const std::string& n) {
        for (const auto& o : v.objects)
            if (o.id != self && o.name == n) return true;
        return false;
    };
    if (!used(stem)) return stem;
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = stem + "_" + std::to_string(i);
        if (!used(candidate)) return candidate;
    }
    return stem;
}

Object& addAt(Project& p, View& v, Kind k, const std::string& stem, double x, double y, double w = 0, double h = 0) {
    const Id id = edit::add(p, v, k, x, y);
    Object& o = *v.object(id);
    if (!stem.empty()) o.name = freshName(v, stem, id);
    if (w > 0) o.setNumber("w", w);
    if (h > 0) o.setNumber("h", h);
    return o;
}

Object& title(Project& p, View& v, const std::string& text, double x, double y, double w, double size = 24) {
    Object& t = addAt(p, v, Kind::Text, "Titre", x, y, w, size * 1.7);
    t.set("text", text);
    t.setNumber("fontSize", size);
    t.set("align", "gauche");
    t.set("textColor", "#FFFFFF");
    return t;
}

} // namespace

// ================================================================ variables ===
VarShape shapeOf(std::string_view type, bool hasMembers) {
    if (hasMembers) return VarShape::Structure;
    const std::string t = upperCopy(type);
    if (t == "BOOL" || t == "EBOOL") return VarShape::Bool;
    if (t.rfind("STRING", 0) == 0 || t.rfind("WSTRING", 0) == 0) return VarShape::Text;
    if (t.rfind("ARRAY", 0) == 0) return VarShape::Other;
    static const std::set<std::string> numbers = {"INT", "DINT", "UINT", "UDINT", "SINT", "USINT", "LINT", "ULINT", "REAL", "LREAL",
                                                  "BYTE", "WORD", "DWORD", "LWORD", "TIME"};
    if (numbers.count(t)) return VarShape::Number;
    return VarShape::Other;
}

VarShape shapeOf(const VarInfo& v) { return shapeOf(v.type, !v.members.empty()); }

Kind kindForVariable(const VarInfo& v) {
    switch (shapeOf(v)) {
        case VarShape::Bool: return Kind::Indicator;
        case VarShape::Number: return Kind::NumericDisplay;
        case VarShape::Text: return Kind::Text;
        case VarShape::Structure: return Kind::Button;
        case VarShape::Other: break;
    }
    return Kind::Text;
}

std::string nameStem(std::string_view path) {
    // 1.11.1 (API-V, R1111-6) : une variable posee depuis l'arbre de la
    // bibliotheque s'appelle API.… ; l'objet garde le nom d'avant
    // (« Afficheur_UDINT_1174 », pas « Afficheur_API_UDINT_1174 »).
    if (path.size() > 4 && path.compare(0, 4, "API.") == 0) path.remove_prefix(4);
    std::string out;
    for (const char c : path) {
        if (identChar(c)) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front()))) out = "V_" + out;
    return out;
}

std::string equipmentPopupName(std::string_view typeName) { return "Popup_" + nameStem(typeName); }

const View* existingEquipmentPopup(const Project& p, std::string_view typeName, std::string_view parameter) {
    const View* v = p.viewByName(equipmentPopupName(typeName));
    return v && v->param(parameter) ? v : nullptr;
}

namespace {

// L'objet d'une variable dans une vue (dans le projet ou non) ; une structure
// ouvre `popup` (vide : pas d'action).
Id placeInView(Project& p, View& v, const VarInfo& info, double x, double y, const std::string& popup) {
    const VarShape shape = shapeOf(info);
    const std::string stem = nameStem(info.name);
    const std::string label = !info.comment.empty() ? info.comment : leafOf(info.name);
    switch (shape) {
        case VarShape::Bool: {
            Object& o = addAt(p, v, Kind::Indicator, "Voyant_" + stem, x, y, 32, 32);
            o.setExpr("value", info.name);
            return o.id;
        }
        case VarShape::Number: {
            Object& o = addAt(p, v, Kind::NumericDisplay, "Afficheur_" + stem, x, y, 200, 56);
            o.setExpr("value", info.name);
            o.set("label", label);
            const std::string t = upperCopy(info.type);
            o.set("format", t == "REAL" || t == "LREAL" ? "0.00" : "0");
            return o.id;
        }
        case VarShape::Text: {
            Object& o = addAt(p, v, Kind::Text, "Texte_" + stem, x, y, 240, 32);
            o.set("text", "{" + info.name + "}");
            o.set("align", "gauche");
            return o.id;
        }
        case VarShape::Structure: {
            Object& o = addAt(p, v, Kind::Button, "Btn_" + stem, x, y, 200, 48);
            o.set("text", leafOf(info.name) + " \xE2\x80\xA6");
            if (!popup.empty()) {
                Action a;
                a.trigger = Trigger::Click;
                a.operation = Operation::Popup;
                a.target = popup;
                a.value = "Equipement := " + info.name;
                o.actions.push_back(std::move(a));
            }
            return o.id;
        }
        case VarShape::Other:
            break;
    }
    return kNoId;
}

} // namespace

Id placeVariable(Project& p, Id view, const VarInfo& info, double x, double y, std::string popup) {
    if (!p.view(view)) return kNoId;
    // La popup d'equipement d'une structure : celle qui existe, sinon generee
    // (une vue de plus dans le projet - la vue visee se reprend ensuite).
    if (shapeOf(info) == VarShape::Structure && popup.empty()) {
        if (const View* existing = existingEquipmentPopup(p, info.type)) popup = existing->name;
        else {
            TypeViewOptions opt;
            opt.typeName = info.type;
            opt.sample = info.name;
            const Id made = generateTypeView(p, opt, info.members);
            if (const View* pv = made != kNoId ? p.view(made) : nullptr) popup = pv->name;
        }
    }
    View* v = p.view(view);
    return v ? placeInView(p, *v, info, x, y, popup) : kNoId;
}

// ============================================================ la vue d'un type ===
Id generateTypeView(Project& p, const TypeViewOptions& opt, const std::vector<VarInfo>& members) {
    std::vector<const VarInfo*> shown;
    for (const auto& m : members)
        if (shapeOf(m) != VarShape::Other && shapeOf(m) != VarShape::Structure) shown.push_back(&m);
    if (shown.empty()) return kNoId;
    const std::string wanted = !opt.viewName.empty() ? opt.viewName
                             : opt.popup ? equipmentPopupName(opt.typeName) : "Vue_" + nameStem(opt.typeName);
    View v = makeView(p, uniqueViewName(p, wanted));
    // Deux colonnes au-dela de douze lignes.
    const std::size_t rows = shown.size() > 12 ? (shown.size() + 1) / 2 : shown.size();
    const int columns = shown.size() > 12 ? 2 : 1;
    const double rowH = 46, top = 64, colW = 460;
    v.width = opt.popup ? static_cast<int>(colW * columns + 20) : p.config.width;
    v.height = opt.popup ? static_cast<int>(top + rowH * static_cast<double>(rows) + 20) : p.config.height;
    v.role = opt.popup ? "popup" : "vue";
    v.background = "#1B2029";
    v.description = "G\xC3\xA9n\xC3\xA9r\xC3\xA9" "e depuis le type " + opt.typeName + " : une ligne par membre, lue \xC3\xA0 travers le param\xC3\xA8tre "
                  + opt.parameter + ".";
    if (opt.popup) v.popup.title = opt.typeName;
    ViewParam prm;
    prm.name = opt.parameter;
    prm.defaultValue = opt.sample;
    prm.description = "L'instance de " + opt.typeName + " montr\xC3\xA9" "e (" + opt.parameter + " := Pompe_1)";
    v.params.push_back(prm);
    (void)title(p, v, opt.typeName, 20, 12, colW * columns - 20, 20);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const VarInfo& m = *shown[i];
        const double x0 = 20 + colW * static_cast<double>(i / rows), y = top + rowH * static_cast<double>(i % rows);
        Object& label = addAt(p, v, Kind::Text, "Libelle_" + nameStem(m.name), x0, y + 4, 220, 32);
        // Un commentaire trop long pour la colonne (etat : "1 prete | 2 defaut |
        // ...") debordait sur la valeur : le nom du membre, alors.
        label.set("text", m.comment.empty() || m.comment.size() > 30 ? m.name : m.comment);
        label.set("align", "gauche");
        label.setNumber("fontSize", 14);
        label.set("textColor", "#C8D0DC");
        VarInfo bound = m;
        bound.name = opt.parameter + "." + m.name;
        bound.comment = m.comment.empty() ? m.name : m.comment;
        const Id id = placeInView(p, v, bound, x0 + 230, y + (shapeOf(m) == VarShape::Bool ? 8 : 2), {});
        if (Object* o = v.object(id)) {
            o->name = freshName(v, nameStem(m.name), o->id);
            if (o->kind == Kind::NumericDisplay) {
                o->setNumber("w", 200);
                o->setNumber("h", 40);
                o->set("label", "");
            }
        }
    }
    const Id id = v.id;
    p.views.push_back(std::move(v));
    return id;
}

// ===================================================== rechercher / remplacer ===
namespace {
// La premiere occurrence retenue (la casse, le mot entier) ; npos : aucune.
std::size_t firstMatch(std::string_view s, std::string_view find, const FindOptions& o) {
    if (find.empty()) return std::string_view::npos;
    const std::string hay = o.matchCase ? std::string(s) : upperCopy(s);
    const std::string needle = o.matchCase ? std::string(find) : upperCopy(find);
    for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) {
        const std::size_t end = at + needle.size();
        if (!o.wholeWord || ((at == 0 || !identChar(s[at - 1])) && (end >= s.size() || !identChar(s[end])))) return at;
    }
    return std::string_view::npos;
}
} // namespace

std::string replaced(std::string_view s, std::string_view find, std::string_view replacement, const FindOptions& o, std::size_t* count) {
    std::size_t n = 0;
    std::string out;
    if (find.empty()) {
        if (count) *count = 0;
        return std::string(s);
    }
    const std::string hay = o.matchCase ? std::string(s) : upperCopy(s);
    const std::string needle = o.matchCase ? std::string(find) : upperCopy(find);
    std::size_t at = 0;
    while (at <= s.size()) {
        const auto hit = hay.find(needle, at);
        if (hit == std::string::npos) break;
        const bool startsOk = !o.wholeWord || hit == 0 || !identChar(s[hit - 1]);
        const std::size_t end = hit + needle.size();
        const bool endsOk = !o.wholeWord || end >= s.size() || !identChar(s[end]);
        if (startsOk && endsOk) {
            out.append(s.substr(at, hit - at));
            out.append(replacement);
            at = end;
            ++n;
        } else {
            out.append(s.substr(at, hit + 1 - at));
            at = hit + 1;
        }
    }
    if (at < s.size()) out.append(s.substr(at));
    if (count) *count = n;
    return out;
}

namespace {

// Les champs de texte d'un projet, dans l'ordre ou on les lit ; `fn(texte,
// vue, objet, ou)` peut le changer (P non const).
bool geometric(const std::string& key) { return key == "x" || key == "y" || key == "w" || key == "h" || key == "rot"; }

template <class P, class F>
void walk(P& p, const FindOptions& o, F&& fn) {
    const std::string dot = " \xC2\xB7 ";
    // 1.11.18 (refonte, lot 3) : les declarations du modele d'un code - leur type et leur valeur.
    const auto declarations = [&](auto& list, Id view, const std::string& where) {
        for (auto& d : list) {
            fn(d.type, view, kNoId, where + dot + "d\xC3\xA9" "claration " + d.name + " (type)");
            fn(d.value, view, kNoId, where + dot + "d\xC3\xA9" "claration " + d.name + " (valeur)");
        }
    };
    for (auto& v : p.views) {
        if (o.view != kNoId && v.id != o.view) continue;
        for (auto& obj : v.objects) {
            const std::string at = v.name + dot + obj.name + dot;
            for (auto& prop : obj.props) {
                if (geometric(prop.key)) continue;
                fn(prop.value, v.id, obj.id, at + prop.key);
                fn(prop.expr, v.id, obj.id, at + prop.key + " (expression)");
            }
            for (std::size_t i = 0; i < obj.actions.size(); ++i) {
                auto& a = obj.actions[i];
                const std::string an = at + "action " + std::to_string(i + 1) + " ";
                fn(a.target, v.id, obj.id, an + "(cible)");
                fn(a.value, v.id, obj.id, an + "(valeur)");
                fn(a.guard, v.id, obj.id, an + "(condition)");
                fn(a.watch, v.id, obj.id, an + "(surveill\xC3\xA9" "e)");
                fn(a.params, v.id, obj.id, an + "(param\xC3\xA8tres)");   // 1.11.6
            }
        }
        for (std::size_t i = 0; i < v.actions.size(); ++i) {
            auto& a = v.actions[i];
            const std::string an = v.name + dot + "action de vue " + std::to_string(i + 1) + " ";
            fn(a.target, v.id, kNoId, an + "(cible)");
            fn(a.value, v.id, kNoId, an + "(valeur)");
            fn(a.guard, v.id, kNoId, an + "(condition)");
            fn(a.watch, v.id, kNoId, an + "(surveill\xC3\xA9" "e)");
            fn(a.params, v.id, kNoId, an + "(param\xC3\xA8tres)");      // 1.11.6
        }
        for (auto& s : v.scripts) {
            const std::string where = v.name + dot + "script " + (s.name.empty() ? s.event : s.name);
            fn(s.body, v.id, kNoId, where);
            declarations(s.decls, v.id, where);                         // 1.11.18 (lot 3)
        }
        for (auto& prm : v.params) fn(prm.defaultValue, v.id, kNoId, v.name + dot + "param\xC3\xA8tre " + prm.name);
        fn(v.popup.title, v.id, kNoId, v.name + dot + "titre de la popup");
        // 1.12.16 : ses constantes (type, valeur, calcul) et ses fonctions (corps, declarations).
        declarations(v.constants, v.id, v.name + dot + "constantes");
        for (auto& d : v.constants) fn(d.calc, v.id, kNoId, v.name + dot + "constante " + d.name + " (calcul)");
        for (auto& f : v.functions) {
            fn(f.body, v.id, kNoId, v.name + dot + "fonction " + f.name);
            declarations(f.decls, v.id, v.name + dot + "fonction " + f.name);
        }
    }
    if (o.view != kNoId) return;
    for (auto& s : p.programs.scripts) {
        fn(s.body, kNoId, kNoId, "Script g\xC3\xA9n\xC3\xA9ral" + dot + s.name);
        declarations(s.decls, kNoId, "Script g\xC3\xA9n\xC3\xA9ral" + dot + s.name);   // 1.11.18 (lot 3)
    }
    for (auto& f : p.programs.functions) {
        fn(f.body, kNoId, kNoId, "Fonction IHM" + dot + f.name);
        declarations(f.decls, kNoId, "Fonction IHM" + dot + f.name);          // 1.11.18 (lot 3)
    }
    for (auto& a : p.alarms) {
        fn(a.condition, kNoId, kNoId, "Alarme" + dot + a.name + dot + "condition");
        fn(a.message, kNoId, kNoId, "Alarme" + dot + a.name + dot + "message");
    }
}

} // namespace

std::vector<FindHit> find(const Project& p, std::string_view text, std::string_view replacement, const FindOptions& o) {
    std::vector<FindHit> hits;
    if (text.empty()) return hits;
    walk(p, o, [&](const std::string& field, Id view, Id object, const std::string& where) {
        if (field.empty()) return;
        std::size_t n = 0;
        std::string after = replaced(field, text, replacement, o, &n);
        if (n == 0) return;
        FindHit h;
        h.view = view;
        h.object = object;
        h.where = where;
        h.before = field;
        h.after = std::move(after);
        h.count = n;
        const std::size_t at = firstMatch(field, text, o);
        h.first = at == std::string::npos ? 0 : at;
        hits.push_back(std::move(h));
    });
    return hits;
}

std::size_t replaceAll(Project& p, std::string_view text, std::string_view replacement, const FindOptions& o) {
    std::size_t fields = 0;
    if (text.empty()) return 0;
    walk(p, o, [&](std::string& field, Id, Id, const std::string&) {
        if (field.empty()) return;
        std::size_t n = 0;
        std::string after = replaced(field, text, replacement, o, &n);
        if (n == 0) return;
        field = std::move(after);
        ++fields;
    });
    return fields;
}

Id duplicateViewReplacing(Project& p, Id viewId, const std::string& newName, std::string_view text, std::string_view replacement,
                          const FindOptions& options, std::size_t* replacedFields) {
    const View* src = p.view(viewId);
    if (!src) return kNoId;
    View copy = *src;
    copy.id = p.allocate();
    copy.name = uniqueViewName(p, newName.empty() ? src->name + "_copie" : newName);
    std::map<Id, Id> ids;
    for (auto& l : copy.layers) {
        const Id n = p.allocate();
        ids[l.id] = n;
        l.id = n;
    }
    for (auto& o : copy.objects) ids[o.id] = p.allocate();
    for (auto& o : copy.objects) {
        o.id = ids[o.id];
        if (ids.count(o.layer)) o.layer = ids[o.layer];
        if (ids.count(o.parent)) o.parent = ids[o.parent];
    }
    for (auto& s : copy.scripts) s.id = p.allocate();
    for (auto& f : copy.functions) f.id = p.allocate();        // 1.12.16 : ses fonctions (les constantes : changeProject)
    copyOperators(p, copy.operators, src->name, copy.name);   // 1.10 : les operateurs d'un symbole
    copy.activeLayer = ids.count(copy.activeLayer) ? ids[copy.activeLayer] : (copy.layers.empty() ? kNoId : copy.layers.front().id);
    // Le remplacement, dans la copie seule.
    Project one;
    one.views.push_back(std::move(copy));
    FindOptions o = options;
    o.view = one.views.front().id;
    const std::size_t fields = replaceAll(one, text, replacement, o);
    if (replacedFields) *replacedFields = fields;
    const Id made = one.views.front().id;
    p.views.push_back(std::move(one.views.front()));
    return made;
}

// ================================================================ styles ===
const std::vector<std::string>& styleKeys() {
    static const std::vector<std::string> keys = {"fill", "stroke", "strokeWidth", "radius", "opacity", "textColor",
                                                  "font", "fontSize", "align", "colorOn", "colorOff", "background"};
    return keys;
}

Style styleFromObject(Project& p, const Object& o, const std::string& name) {
    Style s;
    s.id = p.allocate();
    s.name = uniqueStyleName(p, name.empty() ? std::string("Style") : name);
    for (const auto& k : styleKeys())
        if (const auto* prop = o.find(k); prop && prop->expr.empty()) s.props.push_back({k, prop->value, {}});
    s.description = "Fait de " + o.name + " (" + std::string(kindLabel(o.kind)) + ")";
    return s;
}

void applyStyle(Object& o, const Style& s) {
    for (const auto& prop : s.props)
        if (auto* mine = o.find(prop.key)) mine->value = prop.value;   // une expression garde la main
    o.set("namedStyle", s.name);
}

std::size_t propagateStyle(Project& p, const Style& before, const Style& after) {
    const auto valueIn = [](const Style& s, const std::string& key) -> const Prop* {
        for (const auto& prop : s.props) if (prop.key == key) return &prop;
        return nullptr;
    };
    std::size_t changed = 0;
    const std::string was = upperCopy(before.name);
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            auto* cite = o.find("namedStyle");
            if (!cite || upperCopy(cite->value) != was) continue;
            bool touched = false;
            for (const auto& prop : after.props) {
                auto* mine = o.find(prop.key);
                if (!mine) continue;
                const Prop* old = valueIn(before, prop.key);
                // Une valeur que l'objet a changee lui-meme reste la sienne.
                if (old && mine->value != old->value) continue;
                if (mine->value != prop.value) {
                    mine->value = prop.value;
                    touched = true;
                }
            }
            if (cite->value != after.name) {
                cite->value = after.name;
                touched = true;
            }
            changed += touched;
        }
    return changed;
}

std::size_t styleUsers(const Project& p, std::string_view name) {
    std::size_t n = 0;
    const std::string wanted = upperCopy(name);
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (const auto* cite = o.find("namedStyle"); cite && !cite->value.empty() && upperCopy(cite->value) == wanted) ++n;
    return n;
}

std::size_t forgetStyle(Project& p, std::string_view name) {
    std::size_t n = 0;
    const std::string wanted = upperCopy(name);
    for (auto& v : p.views)
        for (auto& o : v.objects)
            if (auto* cite = o.find("namedStyle"); cite && upperCopy(cite->value) == wanted) {
                cite->value.clear();
                ++n;
            }
    std::erase_if(p.styles, [&](const Style& s) { return upperCopy(s.name) == wanted; });
    return n;
}

// ======================================================== modeles de vues ===
const std::vector<ViewTemplate>& viewTemplates() {
    static const std::vector<ViewTemplate> all = {
        // {cle, nom, description, role impose, roles proposes en tete (vide : tous)}
        {"vide", "Vide", "Une vue blanche."},
        {"synoptique", "Synoptique", "Un titre, la barre de navigation, le fil d'Ariane, le cadre du proc\xC3\xA9" "d\xC3\xA9, le bandeau d'alarme.", "",
         {"vue", "modele"}},
        {"tableau", "Tableau de bord", "Un titre, quatre afficheurs, une courbe, le compteur d'alarmes.", "", {"vue", "modele"}},
        {"equipement", "Popup d'\xC3\xA9quipement", "Une popup \xC3\xA0 param\xC3\xA8tre (Equipement) : son \xC3\xA9tat, sa mesure, Marche et Arr\xC3\xAAt.",
         "popup", {"popup"}},
        {"reglages", "R\xC3\xA9glages", "Un titre, trois consignes \xC3\xA0 saisir, Appliquer.", "", {"vue", "modele"}},
        {"alarmes", "Alarmes", "Le bandeau, la liste des alarmes, le compteur, Acquitter tout.", "", {"vue", "modele"}},
        // 1.10.2 : des vues...
        {"accueil", "Accueil",
         "Un titre, l'horloge, la barre de navigation, un texte d'accueil, l'\xC3\xA9tat de la communication, le bandeau d'alarme.", "",
         {"vue", "modele"}},
        {"courbes", "Courbes", "Un titre, deux courbes, chacune avec sa mesure et sa fl\xC3\xA8" "che de tendance.", "", {"vue", "modele"}},
        {"maintenance", "Maintenance",
         "Quatre \xC3\xA9quipements : leur libell\xC3\xA9, leur compteur horaire, leur voyant de d\xC3\xA9" "faut, Remise \xC3\xA0 z\xC3\xA9ro.", "",
         {"vue", "modele"}},
        {"communication", "Communication", "L'\xC3\xA9tat de la communication, le diagnostic de l'automate, l'historique.", "",
         {"vue", "modele"}},
        {"production", "Production", "Les compteurs de production et le TRS, une courbe, le tableau des variables.", "", {"vue", "modele"}},
        // 1.12.13 : le Tableau en lignes dynamiques, une source de chaque genre (ses types et ses variables crees s'ils manquent).
        {"tableaux_dynamiques", "Tableaux dynamiques",
         "Un tableau par genre de source : ARRAY de structures, LIST, MAP et plusieurs MAP par la cl\xC3\xA9, structure, \xC3\xA9" "num\xC3\xA9ration, "
         "une liste par colonne, cases fixes R/W ; ses types et ses variables sont cr\xC3\xA9\xC3\xA9s s'ils manquent, ses scripts les font vivre.",
         "", {"vue"}},
        // 1.12.16 : les constantes et les fonctions d'une vue et de sa popup (ses variables et sa popup creees s'ils manquent).
        {"constantes_fonctions", "Constantes et fonctions",
         "Les constantes d'une vue (\xC3\xA9" "crites, une calcul\xC3\xA9" "e au d\xC3\xA9marrage) et ses fonctions (publiques, une priv\xC3\xA9" "e), "
         "appel\xC3\xA9" "es par ses objets et ses actions ; sa popup de r\xC3\xA9glage lit une constante de la vue et calcule les siennes "
         "\xC3\xA0 chaque ouverture.",
         "", {"vue"}},
        // ... des popups (a parametres, aucune variable du projet en dur)...
        {"confirmation", "Confirmation", "Une popup \xC3\xA0 param\xC3\xA8tres (Question, Reponse) : la question, Oui et Non.", "popup",
         {"popup"}, 420, 200},
        {"consigne", "Saisie d'une consigne",
         "Une popup \xC3\xA0 param\xC3\xA8tre (Consigne) : la valeur actuelle, le champ de saisie, Valider (\xC3\xA9" "crit la saisie, puis ferme) "
         "et Fermer.", "popup", {"popup"}, 420, 260},
        {"moteur", "Popup moteur",
         "Une popup \xC3\xA0 param\xC3\xA8tre (Moteur) : le moteur, son \xC3\xA9tat, Auto / Manu, Marche et Arr\xC3\xAAt, le compteur horaire.",
         "popup", {"popup"}, 480, 300},
        {"vanne", "Popup vanne",
         "Une popup \xC3\xA0 param\xC3\xA8tre (Vanne) : la vanne r\xC3\xA9glante, son ouverture, la consigne, Ouvrir et Fermer.", "popup",
         {"popup"}, 480, 280},
        // ... des symboles (240 x 160)...
        // 1.12.11 (lot G) : un modele d'equipement lie par sa structure (le parametre Moteur : T_Moteur).
        {"symbole_moteur", "Moteur et son \xC3\xA9tat",
         "Un symbole li\xC3\xA9 \xC3\xA0 sa structure (Moteur : T_Moteur) : le moteur, son voyant de d\xC3\xA9" "faut, son libell\xC3\xA9. "
         "Pos\xC3\xA9, il suffit de lui donner M1.", "symbole",
         {"symbole"}, 240, 160},
        {"symbole_mesure", "Mesure et unit\xC3\xA9", "Un symbole : l'afficheur, l'unit\xC3\xA9, la fl\xC3\xA8" "che de tendance.", "symbole",
         {"symbole"}, 240, 160},
        // ... un en-tete et un pied de page (hauteur 80).
        {"entete", "En-t\xC3\xAAte", "Un titre, le fil d'Ariane, l'utilisateur, l'horloge, le compteur d'alarmes.", "entete", {"entete"}, 0, 80},
        {"pied", "Pied de page", "La barre de navigation, l'\xC3\xA9tat de la communication, le bandeau d'alarme.", "pied", {"pied"}, 0, 80},
    };
    return all;
}

const ViewTemplate* viewTemplate(std::string_view key) {
    for (const auto& t : viewTemplates())
        if (t.key == key || t.label == key) return &t;
    return nullptr;
}

// ===================================================== 1.12.13 : le modele Tableaux dynamiques ===
//  UN TABLEAU PAR GENRE DE SOURCE, en marche des la simulation (fillTablesDemo) :
//    Cuves      ARRAY[1..4] OF T_Cuve : [#], des membres, une formule (%), les seuils de la variable, une formule de
//               couleur, Consigne R/W, un texte a trous (la vanne), une autre liste (Debits[*] : "..." sur la 4e
//               ligne) ; la ligne choisie ecrit Cuve_Choisie ; un double clic : Element.Consigne := Element.Capacite / 2
//               (Element par reference) ; Case saisie : Ancienne et Nouvelle ; un clic sur un en-tete trie (Tri_Cuves).
//    Recette    T_Recette, une structure : une ligne par membre, Valeur R/W.
//    Alarmes    LIST OF T_Alarme : une ligne par alarme (LIST_ADD), "Aucune alarme" sans ligne ; un clic sur Active la
//               bascule ; + Alarme et - Alarme la font grandir et retrecir.
//    Stock      MAP[STRING] OF INT, avec Prix et Seuils (d'autres MAP lues par la cle) : "..." ou une cle manque ;
//               triee par quantite ; la ligne choisie ecrit Article (la cle) ; + Article ajoute une cle sans prix.
//    T_Etat     une enumeration : Nom, Valeur, Texte.
//    Listes     sans source : une liste par colonne (Lignes[*], Cadences[*] R/W, Debits[*]) - la plus longue fait les
//               lignes, "..." ailleurs.
//    Reglages   des cases fixes, la colonne du milieu R/W (colAccess R;W;R).
//  Les types et les variables sont crees s'ils manquent (rangees dans le dossier "Tableaux dynamiques") ; un nom deja
//  pris par autre chose prend _Ex, et les tableaux citent le nom retenu. Les scripts de la vue donnent les valeurs de
//  depart (a l'ouverture, une fois) et font vivre les cuves (a chaque cycle : le niveau suit la consigne).
namespace {

// Un type du modele : repris s'il existe a l'identique (les memes membres, les memes valeurs), cree sinon (une
// enumeration avec son toString et son fromString) ; un nom pris par un autre type : Nom_Ex, Nom_Ex2... Rend le nom retenu.
std::string demoType(Project& p, HmiType t, const std::string& folder) {
    const std::string base = t.name;
    for (int n = 1; n < 100; ++n) {
        const HmiType* have = p.hmiTypeByName(t.name);
        if (!have) break;
        if (have->kind == t.kind && have->members == t.members && have->values == t.values) return have->name;
        t.name = base + "_Ex" + (n > 1 ? std::to_string(n) : std::string{});
    }
    t.id = p.allocate();
    t.folder = folder;
    if (t.kind == HmiTypeKind::Enumeration) (void)regenerateEnumConversions(p, t);
    const std::string name = t.name;
    p.programs.types.push_back(std::move(t));
    return name;
}

// Une variable du modele : reprise si le modele l'a deja creee (le meme type, dans son dossier), creee sinon ; un nom pris
// par une variable du projet (meme de ce type : ses gestes ne doivent pas ecrire dans les donnees du projet) : Nom_Ex...
// Rend le nom retenu.
std::string demoVariable(Project& p, const std::string& base, const std::string& type, const std::string& initial,
                         const std::string& description, const std::string& folder) {
    std::string name = base;
    for (int n = 1; n < 100; ++n) {
        const Variable* have = p.variable(name);
        if (!have) break;
        if (upperCopy(have->type) == upperCopy(type) && have->folder == folder) return have->name;
        name = base + "_Ex" + (n > 1 ? std::to_string(n) : std::string{});
    }
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    v.description = description;
    v.folder = folder;
    p.programs.variables.push_back(std::move(v));
    return name;
}

// Une ligne d'Unites et formats (la plage, les seuils) pour un chemin, si le projet n'en a pas.
void demoDisplay(Project& p, const std::string& path, const char* unit, const char* format, const char* min, const char* max,
                 const char* high, const char* highAlarm) {
    for (const auto& d : p.displays)
        if (upperCopy(d.path) == upperCopy(path)) return;
    VariableDisplay d;
    d.path = path;
    d.unit = unit;
    d.format = format;
    d.min = min;
    d.max = max;
    d.high = high;
    d.highAlarm = highAlarm;
    p.displays.push_back(std::move(d));
}

table::Column demoColumn(const char* title, std::string content, double width = 1.0, bool write = false, const char* format = "",
                         const char* unit = "") {
    table::Column c;
    c.title = title;
    c.content = std::move(content);
    c.width = width;
    c.write = write;
    c.format = format;
    c.unit = unit;
    return c;
}

void demoGesture(Object& o, Trigger t, Operation op, std::string target, std::string value, std::string column = {}) {
    Action a;
    a.trigger = t;
    a.operation = op;
    a.target = std::move(target);
    a.value = std::move(value);
    a.column = std::move(column);
    o.actions.push_back(std::move(a));
}

void fillTablesDemo(Project& p, View& v) {
    namespace tb = table;
    const std::string folder = "Tableaux dynamiques";
    // ---- les types
    HmiType etat;
    etat.name = "T_Etat";
    etat.kind = HmiTypeKind::Enumeration;
    etat.description = "L'\xC3\xA9tat d'une cuve (le mod\xC3\xA8le Tableaux dynamiques)";
    etat.values = {{"ARRET", 0, "Arr\xC3\xAAt", ""}, {"MARCHE", 1, "En marche", ""}, {"DEFAUT", 2, "D\xC3\xA9" "faut", ""}};
    const std::string tEtat = demoType(p, etat, folder);
    HmiType vanne;
    vanne.name = "T_Vanne";
    vanne.description = "La vanne de remplissage d'une cuve";
    vanne.members = {{"Ouverture", "REAL", "", "Son ouverture (%)"}, {"Ouverte", "BOOL", "", "Ouverte (plus de 1 %)"}};
    const std::string tVanne = demoType(p, vanne, folder);
    HmiType cuve;
    cuve.name = "T_Cuve";
    cuve.description = "Une cuve : son niveau suit sa consigne";
    cuve.members = {{"Nom", "STRING", "", "Son nom"},
                    {"Niveau", "REAL", "", "Son niveau (m)"},
                    {"Capacite", "REAL", "10.0", "Sa hauteur utile (m)"},
                    {"Consigne", "REAL", "", "Le niveau vis\xC3\xA9 (m)"},
                    {"Etat", tEtat, "", "Arr\xC3\xAAt, en marche ou en d\xC3\xA9" "faut"},
                    {"Vanne", tVanne, "", "Sa vanne de remplissage"}};
    const std::string tCuve = demoType(p, cuve, folder);
    HmiType alarme;
    alarme.name = "T_Alarme";
    alarme.description = "Une alarme de la liste";
    alarme.members = {{"Message", "STRING", "", "Ce qu'elle dit"}, {"Priorite", "INT", "", "1 : la plus forte"},
                      {"Active", "BOOL", "", "Toujours l\xC3\xA0"}};
    const std::string tAlarme = demoType(p, alarme, folder);
    HmiType recette;
    recette.name = "T_Recette";
    recette.description = "Une recette de cuisson";
    recette.members = {{"Nom", "STRING", "", "Son nom"}, {"Temperature", "REAL", "", "La temp\xC3\xA9rature du four (\xC2\xB0" "C)"},
                       {"Duree", "INT", "", "La dur\xC3\xA9" "e (min)"}, {"Vapeur", "BOOL", "", "Avec de la vapeur"}};
    const std::string tRecette = demoType(p, recette, folder);
    // ---- les variables
    const auto var = [&](const char* name, const std::string& type, const char* initial, const char* description) {
        return demoVariable(p, name, type, initial, description, folder);
    };
    const std::string cuves = var("Cuves", "ARRAY[1..4] OF " + tCuve, "", "Quatre cuves : un tableau de structures (ARRAY)");
    const std::string debits = var("Debits", "ARRAY[1..3] OF REAL", "", "Trois d\xC3\xA9" "bits (m\xC2\xB3/h) : une liste plus courte que Cuves");
    const std::string alarmes = var("Alarmes", "LIST OF " + tAlarme, "", "Les alarmes : une LIST qui grandit et r\xC3\xA9tr\xC3\xA9" "cit");
    const std::string stock = var("Stock", "MAP[STRING] OF INT", "['vis' := 30, 'ecrou' := 50, 'rondelle' := 12]",
                                  "Le stock de chaque article : une MAP");
    const std::string prix = var("Prix", "MAP[STRING] OF REAL", "['vis' := 0.12, 'ecrou' := 0.08]",
                                 "Le prix de chaque article : une autre MAP de m\xC3\xAAme cl\xC3\xA9 (la rondelle n'en a pas)");
    const std::string seuils = var("Seuils", "MAP[STRING] OF INT", "['vis' := 20, 'ecrou' := 20, 'rondelle' := 20]",
                                   "Le stock minimal de chaque article : une troisi\xC3\xA8me MAP");
    const std::string recetteV = var("Recette", tRecette, "", "Une recette : une structure (STRUCT)");
    const std::string lignes = var("Lignes", "LIST OF STRING", "['L1', 'L2', 'L3', 'L4']", "Quatre lignes : une LIST de textes");
    const std::string cadences = var("Cadences", "LIST OF INT", "[120, 95, 101, 88, 130]", "Cinq cadences : la plus longue des listes");
    const std::string choisie = var("Cuve_Choisie", "INT", "1", "La cuve choisie d'un clic (Ligne choisie \xE2\x86\x92)");
    const std::string article = var("Article", "STRING", "'vis'", "L'article choisi d'un clic (sa cl\xC3\xA9)");
    const std::string tri = var("Tri_Cuves", "STRING", "''", "La colonne du tri des cuves (un clic sur un en-t\xC3\xAAte)");
    const std::string triDesc = var("Tri_Desc", "BOOL", "FALSE", "Le tri des cuves d\xC3\xA9" "croissant (un second clic)");
    const std::string saisie = var("Derniere_Saisie", "STRING", "''", "La derni\xC3\xA8re case saisie (Case saisie : Ancienne, Nouvelle)");
    const std::string ajoutes = var("Articles_Ajoutes", "INT", "0", "Les articles ajout\xC3\xA9s par le bouton + Article");
    // La plage et les seuils des cuves : le niveau change de couleur au-dessus de 8 m, rougit au-dessus de 9 m ; la consigne
    // se saisit de 0 a 10 m.
    demoDisplay(p, cuves + "[].Niveau", "m", "0.0", "0", "10", "8", "9");
    demoDisplay(p, cuves + "[].Consigne", "m", "0.0", "0", "10", "", "");
    // ---- les scripts de la vue
    const auto decl = [&](const char* name, const std::string& type) {
        Declaration d;
        d.id = p.allocate();
        d.name = name;
        d.type = type;
        return d;
    };
    const auto c = [&](const char* i) { return cuves + "[" + i + "]"; };
    Script open;
    open.id = p.allocate();
    open.name = "Valeurs_De_Depart";
    open.event = "OnOpen";
    open.description = "Les valeurs de d\xC3\xA9part des tableaux (une fois)";
    open.decls = {decl("a", tAlarme)};
    open.body = "IF " + c("1") + ".Nom = '' THEN\n"
                "    " + c("1") + ".Nom := 'Cuve A'; " + c("1") + ".Niveau := 2.0; " + c("1") + ".Consigne := 3.0;\n"
                "    " + c("2") + ".Nom := 'Cuve B'; " + c("2") + ".Niveau := 7.5; " + c("2") + ".Consigne := 7.5;\n"
                "    " + c("3") + ".Nom := 'Cuve C'; " + c("3") + ".Niveau := 6.0; " + c("3") + ".Consigne := 9.6;\n"
                "    " + c("4") + ".Nom := 'Cuve D'; " + c("4") + ".Niveau := 0.5; " + c("4") + ".Consigne := 0.0;\n"
                "END_IF;\n"
                "IF LIST_COUNT(" + alarmes + ") = 0 THEN\n"
                "    a.Message := 'Niveau haut cuve C'; a.Priorite := 1; a.Active := TRUE; LIST_ADD(" + alarmes + ", a);\n"
                "    a.Message := 'Pompe 2 en d\xC3\xA9" "faut'; a.Priorite := 2; a.Active := FALSE; LIST_ADD(" + alarmes + ", a);\n"
                "    a.Message := 'Porte armoire ouverte'; a.Priorite := 3; a.Active := TRUE; LIST_ADD(" + alarmes + ", a);\n"
                "END_IF;\n"
                "IF " + recetteV + ".Nom = '' THEN\n"
                "    " + recetteV + ".Nom := 'Pain de mie'; " + recetteV + ".Temperature := 210.0; " + recetteV + ".Duree := 35; "
                + recetteV + ".Vapeur := TRUE;\n"
                "END_IF;\n";
    v.scripts.push_back(std::move(open));
    Script cycle;
    cycle.id = p.allocate();
    cycle.name = "Faire_Vivre";
    cycle.event = "OnCycle";
    cycle.description = "Le niveau suit la consigne, la vanne s'ouvre, l'\xC3\xA9tat suit ; les d\xC3\xA9" "bits suivent les vannes";
    cycle.decls = {decl("i", "INT")};
    cycle.body = "FOR i := 1 TO 4 DO\n"
                 "    " + c("i") + ".Niveau := " + c("i") + ".Niveau + (" + c("i") + ".Consigne - " + c("i") + ".Niveau) * 0.05;\n"
                 "    " + c("i") + ".Vanne.Ouverture := LIMIT(0.0, (" + c("i") + ".Consigne - " + c("i") + ".Niveau) * 40.0, 100.0);\n"
                 "    " + c("i") + ".Vanne.Ouverte := " + c("i") + ".Vanne.Ouverture > 1.0;\n"
                 "    IF " + c("i") + ".Niveau > 9.0 THEN " + c("i") + ".Etat := " + tEtat + "#DEFAUT;\n"
                 "    ELSIF " + c("i") + ".Vanne.Ouverte THEN " + c("i") + ".Etat := " + tEtat + "#MARCHE;\n"
                 "    ELSE " + c("i") + ".Etat := " + tEtat + "#ARRET;\n"
                 "    END_IF;\n"
                 "END_FOR;\n"
                 "FOR i := 1 TO 3 DO\n"
                 "    " + debits + "[i] := " + c("i") + ".Vanne.Ouverture * 0.2;\n"
                 "END_FOR;\n";
    v.scripts.push_back(std::move(cycle));
    // ---- la mise en page : un titre, puis 3 x 3 cases (Cuves en prend deux)
    const double W = v.width, H = v.height;
    const double m = std::max(12.0, W * 0.012);
    const double top = 56;
    const double cw = (W - 4 * m) / 3, rh = (H - top - 3 * m) / 3;
    const double font = rh < 260 ? 12 : 14;
    const double headH = 34, noteH = std::clamp(rh * 0.2, 34.0, 52.0);   // un bouton du titre : 32 px (un doigt)
    auto& t0 = title(p, v, "Tableaux dynamiques : un tableau par genre de source", m, 8, W - 2 * m, 22);
    t0.name = freshName(v, "Titre_Tableaux", t0.id);
    struct Place {
        double x, y, w, h;
    };
    // Une case : son titre, son explication en bas ; rend la place du tableau.
    const auto card = [&](const std::string& stem, int col, int row, int span, const std::string& head, const std::string& explain) {
        const double x = m + col * (cw + m), y = top + row * (rh + m), w = span * cw + (span - 1) * m;
        auto& h = addAt(p, v, Kind::Text, "Titre_" + stem, x, y, w, headH);
        h.set("text", head);
        h.setNumber("fontSize", 15);
        h.set("align", "gauche");
        h.set("textColor", "#E6EAF0");
        auto& n = addAt(p, v, Kind::Text, "Note_" + stem, x, y + rh - noteH, w, noteH);
        n.set("text", explain);
        n.setNumber("fontSize", 12);
        n.set("align", "gauche");
        n.set("textColor", "#9AA6B8");
        n.setFlag("wrap", true);
        return Place{x, y + headH + 4, w, rh - headH - noteH - 8};
    };
    const auto dynTable = [&](const std::string& name, const Place& at, const std::string& from, const std::vector<tb::Column>& cols) -> Object& {
        auto& o = addAt(p, v, Kind::Table, name, at.x, at.y, at.w, at.h);
        o.set(std::string(tb::kModeKey), std::string(tb::modeLabel(tb::Mode::Rows)));
        o.set("rowsFrom", from);
        o.set(std::string(tb::kColumnsKey), tb::formatColumns(cols));
        o.setNumber("fontSize", font);
        return o;
    };
    // Un bouton a droite du titre d'une case (son code : un script de l'action).
    const auto headButton = [&](const std::string& stem, const std::string& text, const Place& at, int fromRight, const std::string& code) {
        const double bw = std::min(130.0, at.w * 0.2);
        auto& b = addAt(p, v, Kind::Button, stem, at.x + at.w - (fromRight + 1) * (bw + 6), at.y - headH - 4, bw, headH - 2);
        b.set("text", text);
        b.setNumber("fontSize", 12);
        demoGesture(b, Trigger::Click, Operation::RunScript, "", code);
        return &b;
    };
    // -- Cuves : ARRAY de structures
    const Place pc = card("Cuves", 0, 0, 2, "ARRAY[1..4] OF " + tCuve + " - " + cuves + " : une ligne par \xC3\xA9l\xC3\xA9ment",
                          "[#] le rang ; [*].Niveau un membre (les seuils d'Unit\xC3\xA9s et formats) ; % une formule ; \xC3\x89tat une formule de couleur ; "
                          "Consigne R/W (un clic : la saisie, de 0 \xC3\xA0 10) ; Vanne un texte \xC3\xA0 trous ; D\xC3\xA9" "bit une autre liste (" + debits
                          + "[*] : \xC2\xAB ... \xC2\xBB sur la 4e ligne). Un clic choisit la ligne (" + choisie + ") ; un double clic : Element.Consigne := "
                          "Element.Capacite / 2 (Element par r\xC3\xA9" "f\xC3\xA9rence) ; un clic sur un en-t\xC3\xAAte trie, un second inverse.");
    std::vector<tb::Column> cc = {demoColumn("N\xC2\xB0", "[#]", 0.5),
                                  demoColumn("Nom", "[*].Nom", 1.1),
                                  demoColumn("Niveau", "[*].Niveau"),
                                  demoColumn("%", "=[*].Niveau * 100 / [*].Capacite", 0.6, false, "0"),
                                  demoColumn("\xC3\x89tat", "[*].Etat", 1.1),
                                  demoColumn("Consigne", "[*].Consigne", 1.0, true, "0.0", "m"),
                                  demoColumn("Vanne", "{[*].Vanne.Ouverture:0} %", 0.8),
                                  demoColumn("D\xC3\xA9" "bit", debits + "[*]", 0.9, false, "0.0", "m\xC2\xB3/h")};
    cc[2].color = "seuils";
    cc[4].color = "formule";
    cc[4].colorExpr = "=SEL([*].Etat = " + tEtat + "#DEFAUT, SEL([*].Etat = " + tEtat + "#MARCHE, '#8B949E', '#2EA043'), '#E5534B')";
    auto& tc = dynTable("Tableau_Cuves", pc, cuves, cc);
    tc.set(std::string(tb::kPickKey), choisie);
    tc.setExpr(std::string(tb::kSortKey), tri);
    tc.setExpr(std::string(tb::kSortOrderKey), "SEL(" + triDesc + ", 'croissant', 'd\xC3\xA9" "croissant')");
    demoGesture(tc, Trigger::CellDoubleClick, Operation::RunScript, "", "Element.Consigne := Element.Capacite / 2.0;");
    demoGesture(tc, Trigger::CellEdit, Operation::Assign, saisie,
                "CONCAT('Cuve ', TO_STRING(Index), ' : ', TO_STRING(Ancienne), ' \xE2\x86\x92 ', TO_STRING(Nouvelle))");
    demoGesture(tc, Trigger::HeaderClick, Operation::RunScript, "",
                "IF " + tri + " = Titre THEN " + triDesc + " := NOT " + triDesc + "; ELSE " + tri + " := Titre; " + triDesc
                    + " := FALSE; END_IF;");
    // -- Recette : une structure
    const Place pr = card("Recette", 2, 0, 1, tRecette + " - " + recetteV + " : une ligne par membre",
                          "[*].Membre le nom du membre, [*].Valeur sa valeur ; R/W : chaque membre se saisit dans son type.");
    (void)dynTable("Tableau_Recette", pr, recetteV, {demoColumn("Membre", "[*].Membre"), demoColumn("Valeur", "[*].Valeur", 1.0, true)});
    // -- Alarmes : une LIST de structures
    const Place pa = card("Alarmes", 0, 1, 1, "LIST OF " + tAlarme + " - " + alarmes,
                          "Une ligne par \xC3\xA9l\xC3\xA9ment : la liste grandit (+ Alarme : LIST_ADD) et r\xC3\xA9tr\xC3\xA9" "cit (- Alarme) ; vide : \xC2\xAB Aucune "
                          "alarme \xC2\xBB. Un clic sur Active la bascule (Element.Active := NOT Element.Active).");
    std::vector<tb::Column> ca = {demoColumn("Message", "[*].Message", 2.2), demoColumn("Prio.", "[*].Priorite", 0.6, true),
                                  demoColumn("Active", "[*].Active", 0.7)};
    ca[2].color = "formule";
    ca[2].colorExpr = "=SEL([*].Active, '#8B949E', '#E5534B')";
    auto& ta = dynTable("Tableau_Alarmes", pa, alarmes, ca);
    ta.set(std::string(tb::kEmptyKey), "Aucune alarme");
    demoGesture(ta, Trigger::CellClick, Operation::RunScript, "", "Element.Active := NOT Element.Active;", "Active");
    (void)headButton("Btn_Retirer_Alarme", "- Alarme", pa, 0, "IF LIST_COUNT(" + alarmes + ") > 0 THEN LIST_REMOVE_AT(" + alarmes + ", 0); END_IF;");
    (void)headButton("Btn_Ajouter_Alarme", "+ Alarme", pa, 1,
                     "VAR a : " + tAlarme + "; END_VAR\n"
                     "a.Message := CONCAT('Alarme ', TO_STRING(LIST_COUNT(" + alarmes + ") + 1)); a.Priorite := 2; a.Active := TRUE;\n"
                     "LIST_ADD(" + alarmes + ", a);");
    // -- Stock : plusieurs MAP de meme cle
    const Place ps = card("Stock", 1, 1, 1, "MAP[STRING] OF INT - " + stock + ", avec " + prix + " et " + seuils,
                          "[*].Cle la cl\xC3\xA9, [*].Valeur la quantit\xC3\xA9 (R/W, rouge sous le seuil) ; " + prix + "[[*].Cle] lit une autre MAP "
                          "par la cl\xC3\xA9 : \xC2\xAB ... \xC2\xBB o\xC3\xB9 elle manque ; tri\xC3\xA9" "e par quantit\xC3\xA9 ; un clic \xC3\xA9" "crit l'article (" + article + ").");
    std::vector<tb::Column> cs = {demoColumn("Article", "[*].Cle", 1.2),
                                  demoColumn("Qt\xC3\xA9", "[*].Valeur", 0.7, true),
                                  demoColumn("Seuil", seuils + "[[*].Cle]", 0.7),
                                  demoColumn("Prix", prix + "[[*].Cle]", 0.8, true, "0.00", "\xE2\x82\xAC"),
                                  demoColumn("Valeur", "=" + stock + "[[*].Cle] * " + prix + "[[*].Cle]", 0.9, false, "0.00", "\xE2\x82\xAC")};
    cs[1].color = "formule";
    cs[1].colorExpr = "=SEL([*].Valeur < " + seuils + "[[*].Cle], '#2EA043', '#E5534B')";
    auto& ts = dynTable("Tableau_Stock", ps, stock, cs);
    ts.set(std::string(tb::kPickKey), article);
    ts.set(std::string(tb::kSortKey), "Qt\xC3\xA9");
    ts.set(std::string(tb::kSortOrderKey), "d\xC3\xA9" "croissant");
    (void)headButton("Btn_Reappro", "R\xC3\xA9" "appro.", ps, 0, stock + "['rondelle'] := MAP_GET(" + stock + ", 'rondelle', 0) + 25;");
    (void)headButton("Btn_Ajouter_Article", "+ Article", ps, 1,
                     ajoutes + " := " + ajoutes + " + 1;\n" + stock + "[CONCAT('joint ', TO_STRING(" + ajoutes + "))] := 5 * " + ajoutes
                         + ";\n" + seuils + "[CONCAT('joint ', TO_STRING(" + ajoutes + "))] := 10;");
    // -- T_Etat : une enumeration
    const Place pe = card("Etats", 2, 1, 1, tEtat + " : une ligne par valeur (une \xC3\xA9num\xC3\xA9ration)",
                          "[*].Nom l'identifiant, [*].Valeur le nombre, [*].Texte le texte montr\xC3\xA9.");
    (void)dynTable("Tableau_Etats", pe, tEtat,
                   {demoColumn("Nom", "[*].Nom"), demoColumn("Valeur", "[*].Valeur", 0.6), demoColumn("Texte", "[*].Texte")});
    // -- sans source : une liste par colonne
    const Place pl = card("Listes", 0, 2, 1, "Sans source : une liste par colonne",
                          lignes + "[*] (4), " + cadences + "[*] (5, R/W), " + debits + "[*] (3) : la plus longue fait les lignes, "
                          "\xC2\xAB ... \xC2\xBB l\xC3\xA0 o\xC3\xB9 une liste s'arr\xC3\xAAte.");
    (void)dynTable("Tableau_Listes", pl, "",
                   {demoColumn("Ligne", lignes + "[*]"), demoColumn("Cadence", cadences + "[*]", 1.0, true),
                    demoColumn("D\xC3\xA9" "bit", debits + "[*]", 1.0, false, "0.0", "m\xC2\xB3/h")});
    // -- des cases fixes, la colonne du milieu R/W
    const Place pf = card("Reglages", 1, 2, 1, "Cases fixes : la colonne Valeur en R/W",
                          "Chaque case est un texte, un texte \xC3\xA0 trous ou =formule ; R/W par colonne (R;W;R) : une case qui n'a qu'une "
                          "variable s'\xC3\xA9" "crit au clic.");
    auto& tf = addAt(p, v, Kind::Table, "Tableau_Reglages", pf.x, pf.y, pf.w, pf.h);
    tf.set(std::string(tb::kModeKey), std::string(tb::modeLabel(tb::Mode::Fixed)));
    tf.set("columns", "R\xC3\xA9glage;Valeur;Unit\xC3\xA9");
    tf.set("cells", formatCells({{"Consigne de la cuve A", "{" + c("1") + ".Consigne:0.0}", "m"},
                                 {"Consigne de la cuve B", "{" + c("2") + ".Consigne:0.0}", "m"},
                                 {"Prix de la vis", "=" + prix + "['vis']", "\xE2\x82\xAC"},
                                 {"Article choisi", "{" + article + "}", ""}}));
    tf.setNumber("rows", 4);
    tf.set(std::string(tb::kAccessKey), "R;W;R");
    tf.set("widths", "2;1;1");
    tf.setNumber("fontSize", font);
    // -- en marche : ce que les gestes ecrivent
    const Place pm = card("En_Marche", 2, 2, 1, "En marche",
                          "En simulation : un clic sur une case Consigne, Qt\xC3\xA9 ou Prix ouvre la saisie ; + Article ajoute une cl\xC3\xA9 sans "
                          "prix ; R\xC3\xA9" "appro. fait remonter la rondelle dans le tri.");
    const char* lines[] = {"Cuve choisie : {Cuve_Choisie}", "Article choisi : {Article}", "Tri des cuves : {Tri_Cuves}",
                           "Derni\xC3\xA8re saisie : {Derniere_Saisie}"};
    const std::string names[] = {choisie, article, tri, saisie};
    const std::string stems[] = {"Cuve_Choisie", "Article", "Tri_Cuves", "Derniere_Saisie"};
    const double lh = std::min(30.0, pm.h / 4);
    for (int k = 0; k < 4; ++k) {
        std::string text = lines[k];
        const std::string hole = "{" + stems[k] + "}";
        if (const auto at = text.find(hole); at != std::string::npos) text.replace(at, hole.size(), "{" + names[k] + "}");
        auto& l = addAt(p, v, Kind::Text, "Texte_" + stems[k], pm.x, pm.y + k * lh, pm.w, lh);
        l.set("text", text);
        l.setNumber("fontSize", 14);
        l.set("align", "gauche");
    }
}

} // namespace

// ===================================================== 1.12.16 : le modele Constantes et fonctions ===
//  LES CONSTANTES ET LES FONCTIONS D'UNE VUE, ET CELLES DE SA POPUP, en marche des la simulation :
//    la vue (celle qu'on cree) : Seuil_Haut et Tiede (ecrites ; Seuil_Haut publique), Rampe (calculee au demarrage : son
//    code rend la valeur) ; Pourcent() (publique), Couleur() (privee), Borne() ; une barre remplie par Pourcent() et
//    coloree par Couleur(), un texte a trous qui les lit, -10 / +10 (Consigne := Borne(...)) et Regler... qui ouvre la
//    popup ; un script cyclique fait suivre la consigne a la temperature ;
//    la popup Pop_Reglage (creee, un nom libre) : son parametre Consigne (Reference), Pas (ecrite), Maxi (calculee au
//    demarrage, elle lit <Vue>.Seuil_Haut - une constante publique, par son nom qualifie), Depart (lit le parametre :
//    calculee a chaque ouverture), Monter() et Descendre() ; Annuler remet la consigne d'a l'ouverture.
//  Les variables Four_Temperature et Four_Consigne sont creees si elles manquent (dossier "Constantes et fonctions").
namespace {

Declaration demoConstant(Project& p, const std::string& name, const std::string& type, const std::string& value,
                         const std::string& about, bool pub = true) {
    Declaration d;
    d.id = p.allocate();
    d.kind = DeclKind::Constant;
    d.name = name;
    d.type = type;
    d.value = value;
    d.description = about;
    d.visibility = pub ? Visibility::Public : Visibility::Private;
    return d;
}

Declaration demoComputed(Project& p, const std::string& name, const std::string& type, const std::string& calc, const std::string& about) {
    Declaration d = demoConstant(p, name, type, {}, about);
    d.computed = true;
    d.calc = calc;
    return d;
}

HmiFunction demoFunction(Project& p, const std::string& name, const std::string& returns, const std::string& body,
                         const std::string& param, const std::string& about, bool pub = true) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = returns;
    f.body = body;
    f.description = about;
    f.visibility = pub ? Visibility::Public : Visibility::Private;
    Declaration x;
    x.id = p.allocate();
    x.kind = DeclKind::Parameter;
    x.name = param;
    x.type = "REAL";
    f.decls.push_back(std::move(x));
    return f;
}

void fillMembersDemo(Project& p, View& v) {
    const std::string folder = "Constantes et fonctions";
    const std::string temp = demoVariable(p, "Four_Temperature", "REAL", "20.0",
                                          "La temp\xC3\xA9rature du four (le mod\xC3\xA8le Constantes et fonctions)", folder);
    const std::string cons = demoVariable(p, "Four_Consigne", "REAL", "60.0", "La consigne du four", folder);
    // ---- la vue : ses constantes, ses fonctions
    v.constants.push_back(demoConstant(p, "Seuil_Haut", "REAL", "85.0", "Au-del\xC3\xA0 : rouge (lue par la popup de r\xC3\xA9glage)"));
    v.constants.push_back(demoConstant(p, "Tiede", "REAL", "50.0", "Au-del\xC3\xA0 : orange", false));
    v.constants.push_back(demoComputed(p, "Rampe", "REAL", "RETURN (Seuil_Haut - 20.0) / 30.0;\n",
                                       "La mont\xC3\xA9" "e vis\xC3\xA9" "e en \xC2\xB0" "C par minute : calcul\xC3\xA9" "e au d\xC3\xA9marrage"));
    v.functions.push_back(demoFunction(p, "Pourcent", "REAL", "RETURN LIMIT(0.0, t / Seuil_Haut * 100.0, 100.0);\n", "t",
                                       "Une temp\xC3\xA9rature en % du seuil haut"));
    v.functions.push_back(demoFunction(p, "Couleur", "STRING",
                                       "IF t >= Seuil_Haut THEN\n    RETURN '#E5534B';\nELSIF t >= Tiede THEN\n    RETURN '#F2994A';\nEND_IF;\n"
                                       "RETURN '#4FA3FF';\n",
                                       "t", "La couleur d'une temp\xC3\xA9rature (priv\xC3\xA9" "e : la vue seule l'appelle)", false));
    v.functions.push_back(demoFunction(p, "Borne", "REAL", "RETURN LIMIT(20.0, t, Seuil_Haut + 15.0);\n", "t",
                                       "Une consigne ramen\xC3\xA9" "e entre 20 \xC2\xB0" "C et le seuil haut + 15"));
    Script cycle;
    cycle.id = p.allocate();
    cycle.name = "Chauffer";
    cycle.event = "OnCycle";
    cycle.description = "La temp\xC3\xA9rature suit la consigne";
    cycle.body = temp + " := " + temp + " + (" + cons + " - " + temp + ") * 0.02;\n";
    v.scripts.push_back(std::move(cycle));
    // ---- la popup de reglage (son nom : libre)
    View pop = makeView(p, uniqueViewName(p, "Pop_Reglage"));
    pop.role = "popup";
    pop.width = std::min(440, p.config.width);
    pop.height = std::min(270, p.config.height);
    pop.popup.title = "R\xC3\xA9glage du four";
    pop.description = "La popup du mod\xC3\xA8le Constantes et fonctions : ses constantes, ses fonctions, son param\xC3\xA8tre";
    ViewParam prm;
    prm.name = "Consigne";
    prm.type = "REAL";
    prm.mode = ParamMode::Reference;
    prm.description = "La consigne r\xC3\xA9gl\xC3\xA9" "e (Consigne := " + cons + ")";
    pop.params.push_back(prm);
    pop.constants.push_back(demoConstant(p, "Pas", "REAL", "5.0", "Le pas de + et -"));
    pop.constants.push_back(demoComputed(p, "Maxi", "REAL", "RETURN " + v.name + ".Seuil_Haut + 15.0;\n",
                                         "Lit la constante publique de la vue, par son nom qualifi\xC3\xA9 : calcul\xC3\xA9" "e au d\xC3\xA9marrage"));
    pop.constants.push_back(demoComputed(p, "Depart", "REAL", "RETURN Consigne;\n",
                                         "Lit le param\xC3\xA8tre : calcul\xC3\xA9" "e \xC3\xA0 chaque ouverture (Annuler y revient)"));
    pop.functions.push_back(demoFunction(p, "Monter", "REAL", "RETURN LIMIT(20.0, t + Pas, Maxi);\n", "t", "Un pas de plus, jusqu'\xC3\xA0 Maxi"));
    pop.functions.push_back(demoFunction(p, "Descendre", "REAL", "RETURN LIMIT(20.0, t - Pas, Maxi);\n", "t", "Un pas de moins, jusqu'\xC3\xA0 20 \xC2\xB0" "C"));
    {
        const double w = pop.width;
        (void)title(p, pop, "R\xC3\xA9glage du four", 20, 12, w - 40, 20);
        auto& lu = addAt(p, pop, Kind::Text, "Lecture_Consigne", 20, 56, w - 40, 30);
        lu.set("text", "Consigne : {Consigne:0} \xC2\xB0" "C   (\xC3\xA0 l'ouverture : {Depart:0}, maxi {Maxi:0})");
        lu.setNumber("fontSize", 15);
        lu.set("align", "gauche");
        const auto button = [&](const char* stem, const char* text, double x, double y, double bw, Operation op, std::string value) {
            auto& b = addAt(p, pop, Kind::Button, stem, x, y, bw, 44);
            b.set("text", text);
            Action a;
            a.trigger = Trigger::Click;
            a.operation = op;
            if (op != Operation::ClosePopup) a.target = "Consigne";
            a.value = std::move(value);
            b.actions.push_back(std::move(a));
        };
        const double bw = (w - 60) / 2;
        button("Btn_Descendre", "- Pas", 20, 104, bw, Operation::Assign, "Descendre(Consigne)");
        button("Btn_Monter", "+ Pas", 40 + bw, 104, bw, Operation::Assign, "Monter(Consigne)");
        button("Btn_Annuler", "Annuler", 20, 164, bw, Operation::Assign, "Depart");
        button("Btn_Fermer", "Fermer", 40 + bw, 164, bw, Operation::ClosePopup, {});
    }
    const std::string popName = pop.name;
    p.views.push_back(std::move(pop));
    // ---- la vue : la mise en page
    const double W = v.width, H = v.height;
    const double m = std::max(16.0, W * 0.015);
    (void)title(p, v, v.name + " : les constantes et les fonctions de la vue", m, 12, W - 2 * m, 22);
    auto& bar = addAt(p, v, Kind::ProgressBar, "Charge", m, 70, W * 0.55, 40);
    bar.setExpr("value", "Pourcent(" + temp + ")");
    bar.setExpr("fill", "Couleur(" + temp + ")");
    auto& lecture = addAt(p, v, Kind::Text, "Lecture", m, 120, W * 0.55, 30);
    lecture.set("text", "{" + temp + ":0.0} \xC2\xB0" "C : {Pourcent(" + temp + "):0} % du seuil haut ({Seuil_Haut:0} \xC2\xB0" "C)");
    lecture.setNumber("fontSize", 16);
    lecture.set("align", "gauche");
    auto& consigne = addAt(p, v, Kind::Text, "Lecture_Consigne", m, 156, W * 0.55, 30);
    consigne.set("text", "Consigne {" + cons + ":0} \xC2\xB0" "C   \xC2\xB7   mont\xC3\xA9" "e vis\xC3\xA9" "e {Rampe:0.0} \xC2\xB0" "C/min");
    consigne.setNumber("fontSize", 15);
    consigne.set("align", "gauche");
    const auto button = [&](const char* stem, const char* text, double x, double y, double bw, Operation op, std::string target,
                            std::string value) {
        auto& b = addAt(p, v, Kind::Button, stem, x, y, bw, 44);
        b.set("text", text);
        Action a;
        a.trigger = Trigger::Click;
        a.operation = op;
        a.target = std::move(target);
        a.value = std::move(value);
        b.actions.push_back(std::move(a));
    };
    const double bx = m * 2 + W * 0.55, bw = std::max(120.0, W - bx - m);
    button("Btn_Moins", "-10 \xC2\xB0" "C", bx, 70, bw / 2 - 6, Operation::Assign, cons, "Borne(" + cons + " - 10.0)");
    button("Btn_Plus", "+10 \xC2\xB0" "C", bx + bw / 2 + 6, 70, bw / 2 - 6, Operation::Assign, cons, "Borne(" + cons + " + 10.0)");
    button("Btn_Regler", "R\xC3\xA9gler\xE2\x80\xA6", bx, 126, bw, Operation::Popup, popName, "Consigne := " + cons);
    auto& note = addAt(p, v, Kind::Text, "Explication", m, std::max(210.0, H * 0.45), W - 2 * m, std::max(120.0, H * 0.4));
    note.set("text", "Sous-onglet Constantes : Seuil_Haut et Tiede s'\xC3\xA9" "crivent, Rampe se calcule au d\xC3\xA9marrage (son code rend la valeur). "
                     "Sous-onglet Fonctions : Pourcent() et Borne() (publiques), Couleur() (priv\xC3\xA9" "e). La barre et les textes les "
                     "appellent par leur nom ; -10 et +10 \xC3\xA9" "crivent Consigne := Borne(...). R\xC3\xA9gler\xE2\x80\xA6 ouvre " + popName + " : Maxi lit "
                     + v.name + ".Seuil_Haut (une constante publique, par son nom qualifi\xC3\xA9), Depart lit le param\xC3\xA8tre - calcul\xC3\xA9" "e \xC3\xA0 "
                     "chaque ouverture, Annuler y revient.");
    note.setNumber("fontSize", 14);
    note.set("textColor", "#9AA6B8");
    note.setFlag("wrap", true);
    note.set("align", "gauche");
}

} // namespace

void fillFromTemplate(Project& p, View& v, std::string_view key) {
    const double W = v.width, H = v.height;
    const double m = std::max(16.0, W * 0.015);
    if (key == "synoptique") {
        auto& band = addAt(p, v, Kind::Rectangle, "Bande_Titre", 0, 0, W, 64);
        band.set("fill", "#1B2028");
        band.set("stroke", "");
        (void)title(p, v, v.name, m, 12, W * 0.5, 24);
        auto& nav = addAt(p, v, Kind::NavBar, "Barre_Navigation", m, 72, W - 2 * m, 46);
        (void)nav;
        (void)addAt(p, v, Kind::Breadcrumb, "Fil", m, 126, W * 0.5, 32);
        auto& frame = addAt(p, v, Kind::Frame, "Cadre_Procede", m, 168, W - 2 * m, H - 168 - 64 - m);
        frame.set("title", "Proc\xC3\xA9" "d\xC3\xA9");
        (void)addAt(p, v, Kind::AlarmBanner, "Bandeau_Alarmes", m, H - 56, W - 2 * m, 44);
        return;
    }
    if (key == "tableau") {
        (void)title(p, v, v.name, m, 16, W * 0.6, 26);
        const double tileW = (W - 2 * m - 3 * m) / 4;
        for (int i = 0; i < 4; ++i) {
            auto& d = addAt(p, v, Kind::NumericDisplay, "Mesure_" + std::to_string(i + 1), m + i * (tileW + m), 84, tileW, 90);
            d.set("label", "Mesure " + std::to_string(i + 1));
        }
        auto& trend = addAt(p, v, Kind::Trend, "Courbe", m, 196, (W - 3 * m) * 0.7, H - 196 - m);
        (void)trend;
        (void)addAt(p, v, Kind::AlarmCounter, "Compteur_Alarmes", m * 2 + (W - 3 * m) * 0.7, 196, (W - 3 * m) * 0.3, 90);
        return;
    }
    if (key == "equipement") {
        v.role = "popup";
        v.width = std::min(480, p.config.width);
        v.height = std::min(320, p.config.height);
        v.popup.title = "\xC3\x89quipement";
        ViewParam prm;
        prm.name = "Equipement";
        prm.description = "L'\xC3\xA9quipement montr\xC3\xA9 (Equipement := Pompe_1)";
        v.params.push_back(prm);
        const double w = v.width;
        (void)title(p, v, "\xC3\x89quipement", 20, 12, w - 40, 20);
        auto& state = addAt(p, v, Kind::Indicator, "Voyant_Marche", 24, 70, 32, 32);
        state.setExpr("value", "Equipement.Marche");
        auto& stateText = addAt(p, v, Kind::Text, "Etat", 68, 70, 200, 32);
        stateText.set("text", "{Equipement.Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}");
        stateText.set("align", "gauche");
        auto& mes = addAt(p, v, Kind::NumericDisplay, "Mesure", 24, 120, w - 48, 56);
        mes.setExpr("value", "Equipement.Mesure");
        mes.set("label", "Mesure");
        auto& on = addAt(p, v, Kind::Button, "Btn_Marche", 24, 200, (w - 60) / 2, 48);
        on.set("text", "Marche");
        Action a;
        a.trigger = Trigger::Click;
        a.operation = Operation::Set;
        a.target = "Equipement.Marche";
        on.actions.push_back(a);
        auto& off = addAt(p, v, Kind::Button, "Btn_Arret", 36 + (w - 60) / 2, 200, (w - 60) / 2, 48);
        off.set("text", "Arr\xC3\xAAt");
        a.operation = Operation::Reset;
        off.actions.push_back(a);
        return;
    }
    if (key == "reglages") {
        (void)title(p, v, v.name, m, 16, W * 0.6, 26);
        for (int i = 0; i < 3; ++i) {
            const double y = 90 + i * 64;
            auto& l = addAt(p, v, Kind::Text, "Libelle_" + std::to_string(i + 1), m, y + 6, 280, 36);
            l.set("text", "Consigne " + std::to_string(i + 1));
            l.set("align", "gauche");
            (void)addAt(p, v, Kind::InputField, "Saisie_" + std::to_string(i + 1), m + 300, y, 220, 44);
        }
        auto& ok = addAt(p, v, Kind::Button, "Btn_Appliquer", m + 300, 90 + 3 * 64 + 12, 220, 48);
        ok.set("text", "Appliquer");
        return;
    }
    if (key == "alarmes") {
        (void)title(p, v, v.name, m, 16, W * 0.5, 26);
        (void)addAt(p, v, Kind::AlarmCounter, "Compteur", W - m - 260, 16, 260, 56);
        (void)addAt(p, v, Kind::AlarmBanner, "Bandeau", m, 84, W - 2 * m, 44);
        (void)addAt(p, v, Kind::History, "Liste_Alarmes", m, 140, W - 2 * m, H - 140 - 80);
        auto& ack = addAt(p, v, Kind::Button, "Btn_Acquitter", W - m - 260, H - 64, 260, 48);
        ack.set("text", "Acquitter tout");
        Action a;
        a.trigger = Trigger::Click;
        a.operation = Operation::AckAlarm;
        a.target = "*";
        ack.actions.push_back(a);
        return;
    }
    // ---- 1.10.2 : les modeles en plus. Tout est place d'apres W et H (la taille
    // choisie dans la galerie), sans chevauchement ni objet hors de la vue.
    const auto button = [&](const std::string& stem, const std::string& text, double x, double y, double w, double h) -> Object& {
        auto& b = addAt(p, v, Kind::Button, stem, x, y, w, h);
        b.set("text", text);
        return b;
    };
    const auto act = [](Object& o, Operation op, const std::string& target, const std::string& value = {}) {
        Action a;
        a.trigger = Trigger::Click;
        a.operation = op;
        a.target = target;
        a.value = value;
        o.actions.push_back(a);
    };
    // Une popup a parametres : son role, son titre, ses parametres (aucune
    // variable du projet en dur ; la taille reste celle de la galerie).
    const auto popup = [&](const char* caption) {
        v.role = "popup";
        v.popup.title = caption;
    };
    const auto param = [&](const char* name, const char* type, const char* description) {
        ViewParam prm;
        prm.name = name;
        prm.type = type;
        prm.description = description;
        v.params.push_back(prm);
    };
    const auto digitalClock = [&](double x, double y, double w, double h, double size) {
        auto& c = addAt(p, v, Kind::Clock, "Horloge", x, y, w, h);
        c.set("clockStyle", "num\xC3\xA9rique");
        c.setNumber("fontSize", size);
    };
    // -- des vues
    if (key == "accueil") {
        (void)title(p, v, v.name, m, 12, W * 0.55, 24);
        const double cw = std::min(260.0, W * 0.3);
        digitalClock(W - m - cw, 8, cw, 48, 24);
        (void)addAt(p, v, Kind::NavBar, "Barre_Navigation", m, 72, W - 2 * m, 46);
        auto& hello = addAt(p, v, Kind::Text, "Texte_Accueil", m, 136, W - 2 * m, std::max(40.0, H - 240 - m));
        hello.set("text", "Bienvenue. Choisissez une vue dans la barre de navigation.");
        hello.setNumber("fontSize", 20);
        if (core::hasApi()) (void)addAt(p, v, Kind::CommStatus, "Etat_Communication", m, H - 104, std::min(560.0, W - 2 * m), 40);   // 1.12.0
        (void)addAt(p, v, Kind::AlarmBanner, "Bandeau_Alarmes", m, H - 56, W - 2 * m, 44);
        return;
    }
    if (key == "courbes") {
        (void)title(p, v, v.name, m, 12, W * 0.6, 24);
        const double side = std::min(220.0, W * 0.25);
        const double rh = (H - 72 - 2 * m) / 2;
        const double dh = std::min(90.0, rh * 0.5);
        for (int i = 0; i < 2; ++i) {
            const std::string n = std::to_string(i + 1);
            const double y = 72 + i * (rh + m);
            (void)addAt(p, v, Kind::Trend, "Courbe_" + n, m, y, W - 3 * m - side, rh);
            auto& d = addAt(p, v, Kind::NumericDisplay, "Mesure_" + n, W - m - side, y, side, dh);
            d.set("label", "Mesure " + n);
            const double ah = std::min(90.0, rh - dh - 8);
            (void)addAt(p, v, Kind::TrendArrow, "Tendance_" + n, W - m - side, y + dh + 8, std::min(ah, side), ah);
        }
        return;
    }
    if (key == "maintenance") {
        (void)title(p, v, v.name, m, 12, W * 0.6, 24);
        const double rh = std::min(72.0, (H - 80 - m) / 4 - 12);
        const double bw = std::min(200.0, W * 0.2);
        for (int i = 0; i < 4; ++i) {
            const std::string n = std::to_string(i + 1);
            const double y = 80 + i * (rh + 12);
            auto& l = addAt(p, v, Kind::Text, "Libelle_" + n, m, y, W * 0.22, rh);
            l.set("text", "\xC3\x89quipement " + n);
            l.set("align", "gauche");
            auto& c = addAt(p, v, Kind::HourMeter, "Compteur_" + n, W * 0.22 + 2 * m, y, W * 0.34, rh);
            c.set("label", "Heures de marche");
            (void)addAt(p, v, Kind::Indicator, "Defaut_" + n, W * 0.56 + 3 * m, y + (rh - 32) / 2, 32, 32);
            (void)button("Btn_Raz_" + n, "Remise \xC3\xA0 z\xC3\xA9ro", W - m - bw, y, bw, rh);
        }
        return;
    }
    if (key == "communication") {
        (void)title(p, v, v.name, m, 12, W * 0.6, 24);
        // 1.12.0 : IHM Studio - pas d'automate du projet : l'historique seul.
        if (!core::hasApi()) {
            (void)addAt(p, v, Kind::History, "Historique", m, 72, W - 2 * m, H - 72 - m);
            return;
        }
        (void)addAt(p, v, Kind::CommStatus, "Etat_Communication", m, 72, W - 2 * m, 40);
        const double half = (W - 3 * m) / 2;
        (void)addAt(p, v, Kind::PlcDiagnostic, "Diagnostic", m, 128, half, H - 128 - m);
        (void)addAt(p, v, Kind::History, "Historique", 2 * m + half, 128, half, H - 128 - m);
        return;
    }
    if (key == "production") {
        (void)title(p, v, v.name, m, 12, W * 0.6, 24);
        const double half = (W - 3 * m) / 2;
        const double hh = (H - 72 - 2 * m) / 2;
        (void)addAt(p, v, Kind::ProductionCounter, "Compteurs", m, 72, half, hh);
        (void)addAt(p, v, Kind::Trend, "Courbe", 2 * m + half, 72, half, hh);
        (void)addAt(p, v, Kind::VariableTable, "Variables", m, 72 + hh + m, W - 2 * m, hh);
        return;
    }
    // 1.12.13 : un tableau par genre de source (ses types, ses variables, ses scripts de vue).
    if (key == "tableaux_dynamiques") {
        fillTablesDemo(p, v);
        return;
    }
    // 1.12.16 : les constantes et les fonctions d'une vue et de sa popup (ses variables et sa popup).
    if (key == "constantes_fonctions") {
        fillMembersDemo(p, v);
        return;
    }
    // -- des popups
    if (key == "confirmation") {
        popup("Confirmation");
        param("Question", "STRING", "La question pos\xC3\xA9" "e (Question := 'Arr\xC3\xAAter la ligne ?')");
        param("Reponse", "BOOL", "La r\xC3\xA9ponse : VRAI pour Oui, FAUX pour Non (Reponse := Arret_Confirme)");
        auto& q = addAt(p, v, Kind::Text, "Question", 20, 20, W - 40, std::max(32.0, H - 108));
        q.set("text", "{Question}");
        q.setNumber("fontSize", 18);
        const double bw = (W - 60) / 2;
        auto& yes = button("Btn_Oui", "Oui", 20, H - 68, bw, 48);
        act(yes, Operation::Set, "Reponse");
        act(yes, Operation::ClosePopup, "");
        auto& no = button("Btn_Non", "Non", 40 + bw, H - 68, bw, 48);
        act(no, Operation::Reset, "Reponse");
        act(no, Operation::ClosePopup, "");
        return;
    }
    if (key == "consigne") {
        popup("Consigne");
        param("Consigne", "", "La consigne r\xC3\xA9gl\xC3\xA9" "e (Consigne := Consigne_Four)");
        auto& now = addAt(p, v, Kind::NumericDisplay, "Valeur_Actuelle", 20, 20, W - 40, 64);
        now.setExpr("value", "Consigne");
        now.set("label", "Valeur actuelle");
        auto& in = addAt(p, v, Kind::InputField, "Saisie_Consigne", 20, 100, W - 40, 48);
        in.set("variable", "Consigne");
        const double bw = (W - 60) / 2;
        auto& ok = button("Btn_Valider", "Valider", 20, H - 68, bw, 48);
        // 1.11.23 : Valider ecrit d'abord la saisie en cours (refusee : la popup reste ouverte).
        act(ok, Operation::SubmitInputs, "");
        act(ok, Operation::ClosePopup, "");
        auto& close = button("Btn_Fermer", "Fermer", 40 + bw, H - 68, bw, 48);
        act(close, Operation::ClosePopup, "");
        return;
    }
    if (key == "moteur") {
        popup("Moteur");
        param("Moteur", "", "Le moteur montr\xC3\xA9 (Moteur := Moteur_1) : Marche, Defaut, Auto");
        auto& mot = addAt(p, v, Kind::Motor, "Moteur", 24, 20, 96, 80);
        mot.setExpr("value", "Moteur.Marche");
        mot.set("variable", "");
        mot.setExpr("fault", "Moteur.Defaut");
        auto& state = addAt(p, v, Kind::Text, "Etat", 136, 20, W - 160, 36);
        state.set("text", "{Moteur.Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}");
        state.set("align", "gauche");
        auto& mode = addAt(p, v, Kind::Switch, "Auto_Manu", 136, 64, W - 160, 40);
        mode.set("variable", "Moteur.Auto");
        const double bw = (W - 60) / 2;
        auto& on = button("Btn_Marche", "Marche", 20, 124, bw, 48);
        act(on, Operation::Set, "Moteur.Marche");
        auto& off = button("Btn_Arret", "Arr\xC3\xAAt", 40 + bw, 124, bw, 48);
        act(off, Operation::Reset, "Moteur.Marche");
        auto& hours = addAt(p, v, Kind::HourMeter, "Compteur_Horaire", 20, 192, W - 40, std::min(64.0, H - 192 - 16));
        hours.set("condition", "Moteur.Marche");
        hours.set("label", "Heures de marche");
        return;
    }
    if (key == "vanne") {
        popup("Vanne");
        param("Vanne", "", "La vanne montr\xC3\xA9" "e (Vanne := Vanne_1) : Position, Consigne, Defaut");
        auto& valve = addAt(p, v, Kind::Valve, "Vanne", 24, 20, 96, 80);
        valve.set("valveType", "r\xC3\xA9glante");
        valve.setExpr("opening", "Vanne.Position");
        valve.setExpr("fault", "Vanne.Defaut");
        auto& bar = addAt(p, v, Kind::Bargraph, "Ouverture", 136, 32, W - 160, 56);
        bar.set("orientation", "horizontale");
        bar.set("variable", "Vanne.Position");
        bar.set("unit", "%");
        auto& sp = addAt(p, v, Kind::Slider, "Consigne", 20, 116, W - 40, 64);
        sp.set("variable", "Vanne.Consigne");
        sp.set("unit", "%");
        const double bw = (W - 60) / 2;
        auto& open = button("Btn_Ouvrir", "Ouvrir", 20, H - 68, bw, 48);
        act(open, Operation::Assign, "Vanne.Consigne", "100");
        auto& shut = button("Btn_Fermer", "Fermer", 40 + bw, H - 68, bw, 48);
        act(shut, Operation::Assign, "Vanne.Consigne", "0");
        return;
    }
    // -- des symboles (240 x 160)
    if (key == "symbole_moteur") {
        // 1.12.11 (lot G) : UN MODELE D'EQUIPEMENT LIE PAR SA STRUCTURE. Le parametre Moteur est une T_Moteur
        // (Marche, Defaut, Nom ; creee si le projet n'en a pas) ; le moteur la prend par sa liaison Equipement
        // (Valeur <- Marche, Defaut <- Defaut, Libelle <- Nom), le voyant montre son defaut, le texte son nom.
        // Pose dans une vue, il suffit de donner M1 (une T_Moteur) a l'instance. Avant : ni parametre, ni liaison.
        if (!p.hmiTypeByName("T_Moteur")) {
            HmiType t;
            t.id = p.allocate();
            t.name = "T_Moteur";
            t.description = "Un moteur (le mod\xC3\xA8le de symbole Moteur et son \xC3\xA9tat)";
            t.members = {{"Marche", "BOOL", "", "En marche"}, {"Defaut", "BOOL", "", "En d\xC3\xA9" "faut"}, {"Nom", "STRING", "", "Son rep\xC3\xA8re (M-101)"}};
            p.programs.types.push_back(std::move(t));
        }
        param("Moteur", "T_Moteur", "Le moteur montr\xC3\xA9 (Moteur := M1) : Marche, Defaut, Nom - sa liaison \xC3\x89quipement les lit");
        auto& mot = addAt(p, v, Kind::Motor, "Dessin_Moteur", 16, 16, W * 0.5 - 16, H - 16 - 64);   // pas « Moteur » : le parametre
        mot.set("variable", "");
        mot.set("equipment", "Moteur");
        mot.set("labelPosition", "aucun");   // le libelle est le texte du bas (le sien nomme ses alarmes)
        auto& fault = addAt(p, v, Kind::Indicator, "Defaut", W - 16 - 32, 16, 32, 32);
        fault.setExpr("value", "Moteur.Defaut");
        fault.set("colorOn", "#E5534B");
        auto& l = addAt(p, v, Kind::Text, "Libelle", 16, H - 48, W - 32, 32);
        l.set("text", "{Moteur.Nom}");
        return;
    }
    if (key == "symbole_mesure") {
        (void)addAt(p, v, Kind::NumericDisplay, "Mesure", 16, 16, W * 0.6 - 16, H * 0.45);
        auto& u = addAt(p, v, Kind::Text, "Unite", W * 0.6 + 8, 16, W * 0.4 - 24, H * 0.45);
        u.set("text", "unit\xC3\xA9");
        const double a = std::min(64.0, H * 0.55 - 48);
        (void)addAt(p, v, Kind::TrendArrow, "Tendance", 16, H * 0.45 + 32, a, a);
        return;
    }
    // -- un en-tete, un pied de page (hauteur 80)
    if (key == "entete") {
        (void)title(p, v, v.name, m, 6, W * 0.25, 18);
        (void)addAt(p, v, Kind::Breadcrumb, "Fil", m, 42, W * 0.25, 30);
        (void)addAt(p, v, Kind::UserInfo, "Utilisateur", W * 0.3, (H - 44) / 2, W * 0.25, 44);
        digitalClock(W * 0.58, (H - 48) / 2, W * 0.2, 48, 22);
        (void)addAt(p, v, Kind::AlarmCounter, "Compteur_Alarmes", W * 0.81, (H - 56) / 2, W * 0.19 - m, 56);
        return;
    }
    if (key == "pied") {
        (void)addAt(p, v, Kind::NavBar, "Barre_Navigation", m, (H - 46) / 2, W * 0.45, 46);
        if (core::hasApi()) (void)addAt(p, v, Kind::CommStatus, "Etat_Communication", W * 0.45 + 2 * m, (H - 40) / 2, W * 0.2, 40);   // 1.12.0
        (void)addAt(p, v, Kind::AlarmBanner, "Bandeau_Alarmes", W * 0.65 + 3 * m, (H - 44) / 2, W * 0.35 - 4 * m, 44);
        return;
    }
}

} // namespace hmi::design
