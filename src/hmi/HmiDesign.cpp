#include "HmiDesign.hpp"

#include "HmiEdit.hpp"
#include "HmiOperators.hpp"   // 1.10

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
        // ... des popups (a parametres, aucune variable du projet en dur)...
        {"confirmation", "Confirmation", "Une popup \xC3\xA0 param\xC3\xA8tres (Question, Reponse) : la question, Oui et Non.", "popup",
         {"popup"}, 420, 200},
        {"consigne", "Saisie d'une consigne",
         "Une popup \xC3\xA0 param\xC3\xA8tre (Consigne) : la valeur actuelle, le champ de saisie, Valider et Fermer.", "popup", {"popup"}, 420, 260},
        {"moteur", "Popup moteur",
         "Une popup \xC3\xA0 param\xC3\xA8tre (Moteur) : le moteur, son \xC3\xA9tat, Auto / Manu, Marche et Arr\xC3\xAAt, le compteur horaire.",
         "popup", {"popup"}, 480, 300},
        {"vanne", "Popup vanne",
         "Une popup \xC3\xA0 param\xC3\xA8tre (Vanne) : la vanne r\xC3\xA9glante, son ouverture, la consigne, Ouvrir et Fermer.", "popup",
         {"popup"}, 480, 280},
        // ... des symboles (240 x 160)...
        {"symbole_moteur", "Moteur et son \xC3\xA9tat", "Un symbole : le moteur, son voyant de d\xC3\xA9" "faut, son libell\xC3\xA9.", "symbole",
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
        (void)addAt(p, v, Kind::CommStatus, "Etat_Communication", m, H - 104, std::min(560.0, W - 2 * m), 40);
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
        auto& mot = addAt(p, v, Kind::Motor, "Moteur", 16, 16, W * 0.5 - 16, H - 16 - 64);
        mot.set("variable", "");
        (void)addAt(p, v, Kind::Indicator, "Defaut", W - 16 - 32, 16, 32, 32);
        auto& l = addAt(p, v, Kind::Text, "Libelle", 16, H - 48, W - 32, 32);
        l.set("text", "Moteur");
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
        (void)addAt(p, v, Kind::CommStatus, "Etat_Communication", W * 0.45 + 2 * m, (H - 40) / 2, W * 0.2, 40);
        (void)addAt(p, v, Kind::AlarmBanner, "Bandeau_Alarmes", W * 0.65 + 3 * m, (H - 44) / 2, W * 0.35 - 4 * m, 44);
        return;
    }
}

} // namespace hmi::design
