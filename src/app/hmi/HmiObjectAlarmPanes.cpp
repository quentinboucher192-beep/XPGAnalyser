// =============================================================================
//  app/hmi/HmiObjectAlarmPanes.cpp - l'interface des alarmes des objets (1.9)
// -----------------------------------------------------------------------------
//  Voir l'en-tete. Les lignes viennent de hmi::objectAlarms / objectAlarmsOf
//  (la meme generation que le moteur) ; chaque changement est une commande
//  hmi::changeProject (Ctrl+Z la defait).
// =============================================================================
#include "HmiObjectAlarmPanes.hpp"
#include "HmiAssist.hpp"                    // 1.10.2 (chantier A) : l'aide a la saisie des conditions
#include "HmiParamPanes.hpp"                // 1.10.2 (chantier A) : les parametres du symbole dans ses conditions
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : les champs a expression, partout pareils
#include "HmiIcons.hpp"

#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiAlarmGroupCommands.hpp"   // 1.10.2 (AL) : la case Groupe de l'IHM
#include "../../hmi/HmiSymbols.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace app::objalarms {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;

namespace {

const char* const kDot = " \xC2\xB7 ";

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool yes(std::string_view v) {
    const std::string t = trimmed(v);
    return t == "1" || t == "oui" || t == "true" || t == "TRUE" || t == "vrai" || t == "Oui";
}

std::string priorityText(int p) { return std::to_string(p) + " - " + std::string(hmi::alarmPriorityLabel(p)); }

std::vector<std::string> priorityChoices() {
    std::vector<std::string> out;
    for (int p = 1; p <= hmi::kAlarmPriorities; ++p) out.push_back(priorityText(p));
    return out;
}

bool isTextField(std::string_view f) {
    return f == "condition" || f == "message" || f == "consigne" || f == "description" || f == "groupe";
}

// La definition d'une alarme generee dans les termes de sa source : celle du
// symbole qui la declare (non developpee), sinon celle de la bibliotheque.
hmi::AlarmDef rawBase(const hmi::Project& p, const hmi::ObjectAlarm& oa) {
    if (oa.source == hmi::ObjectAlarmSource::Symbol)
        if (const auto* sym = p.viewByName(oa.symbol))
            for (const auto& a : sym->alarms)
                if (a.name == oa.localName) return a;
    hmi::AlarmDef d = oa.base;
    d.name = oa.localName;
    return d;
}

const hmi::ObjectAlarm* generatedFor(const std::vector<hmi::ObjectAlarm>& list, std::string_view path, std::string_view alarm) {
    for (const auto& a : list)
        if (a.path == path && a.localName == alarm) return &a;
    return nullptr;
}

// Le champ d'une surcharge : sa valeur en texte, ou nul.
std::optional<std::string> overrideText(const hmi::AlarmOverride& ov, std::string_view f) {
    if (f == "active" && ov.active) return std::string(*ov.active ? "oui" : "non");
    if (f == "condition" && ov.condition) return *ov.condition;
    if (f == "message" && ov.message) return *ov.message;
    if (f == "priorite" && ov.priority) return priorityText(*ov.priority);
    if (f == "categorie" && ov.category) return *ov.category;
    if (f == "groupe" && ov.group) return *ov.group;
    if (f == "delai" && ov.delayMs) return std::to_string(*ov.delayMs);
    if (f == "acquittement" && ov.ackRequired) return std::string(*ov.ackRequired ? "oui" : "non");
    if (f == "consigne" && ov.instruction) return *ov.instruction;
    if (f == "description" && ov.description) return *ov.description;
    return std::nullopt;
}

// Ecrit le champ `f` de `d` (deja verifie) dans la surcharge.
void writeOverride(hmi::AlarmOverride& ov, std::string_view f, const hmi::AlarmDef& d) {
    if (f == "condition") ov.condition = d.condition;
    else if (f == "message") ov.message = d.message;
    else if (f == "priorite") ov.priority = d.priority;
    else if (f == "categorie") ov.category = d.category;
    else if (f == "groupe") ov.group = d.group;
    else if (f == "delai") ov.delayMs = d.delayMs;
    else if (f == "acquittement") ov.ackRequired = d.ackRequired;
    else if (f == "consigne") ov.instruction = d.instruction;
    else if (f == "description") ov.description = d.description;
}

std::string sourceName(const hmi::ObjectAlarm& oa) {
    return oa.source == hmi::ObjectAlarmSource::Symbol ? "du symbole " + oa.symbol : std::string("de la biblioth\xC3\xA8que");
}

hmi::View* symbolView(hmi::Project& p, Id symbol) {
    hmi::View* v = p.view(symbol);
    return v && hmi::isSymbolView(*v) ? v : nullptr;
}

std::string freeName(const hmi::View& sym, std::string base) {
    if (base.empty()) base = "Alarme";
    const auto taken = [&](const std::string& n) {
        return std::any_of(sym.alarms.begin(), sym.alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == n; });
    };
    if (!taken(base)) return base;
    for (int i = 1;; ++i)
        if (const std::string n = base + "_" + std::to_string(i); !taken(n)) return n;
}

} // namespace

// ================================================================ champs ====
std::string fieldLabel(std::string_view f) {
    static const std::map<std::string, std::string, std::less<>> labels = {
        {"active", "Active"}, {"nom", "Nom"}, {"condition", "Condition"}, {"message", "Message"},
        {"priorite", "Priorit\xC3\xA9"}, {"categorie", "Cat\xC3\xA9gorie"}, {"groupe", "Groupe (d\xC3\xA9" "clar\xC3\xA9)"},
        {"delai", "D\xC3\xA9lai (ms)"}, {"acquittement", "Acquittement"}, {"consigne", "Consigne"}, {"description", "Description"}};
    const auto it = labels.find(f);
    return it == labels.end() ? std::string(f) : it->second;
}

std::string fieldValue(const hmi::AlarmDef& d, std::string_view f) {
    if (f == "nom") return d.name;
    if (f == "condition") return d.condition;
    if (f == "message") return d.message;
    if (f == "priorite") return priorityText(d.priority);
    if (f == "categorie") return d.category;
    if (f == "groupe") return d.group;
    if (f == "delai") return std::to_string(d.delayMs);
    if (f == "acquittement") return d.ackRequired ? "oui" : "non";
    if (f == "consigne") return d.instruction;
    if (f == "description") return d.description;
    return {};
}

bool parseField(hmi::AlarmDef& d, std::string_view f, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string reason) {
        if (why) *why = std::move(reason);
        return false;
    };
    const std::string value = f == "message" || f == "description" || f == "consigne" ? raw : trimmed(raw);
    if (f == "nom") {
        if (!hmi::isIdentifier(value)) return fail("nom invalide : lettres, chiffres et _ (" + value + ")");
        d.name = value;
    } else if (f == "condition") {
        const auto e = hmi::Expression::compile(value.empty() ? std::string("FALSE") : value);
        if (!e.valid()) return fail("condition illisible : " + e.error());
        std::string ro;
        if (!hmi::isReadOnly(value, &ro)) return fail(ro);
        d.condition = value;
    } else if (f == "message" || f == "consigne") {
        const auto errors = hmi::TextTemplate::compile(value).errors();
        if (!errors.empty()) return fail(std::string(f == "message" ? "message : " : "consigne : ") + errors.front());
        (f == "message" ? d.message : d.instruction) = value;
    } else if (f == "priorite") {
        const int prio = value.empty() ? 0 : value[0] - '0';
        if (prio < 1 || prio > hmi::kAlarmPriorities) return fail("priorit\xC3\xA9 de 1 (critique) \xC3\xA0 4 (basse)");
        d.priority = prio;
    } else if (f == "categorie") {
        if (value.empty()) return fail("une cat\xC3\xA9gorie : D\xC3\xA9" "faut, Alarme, Avertissement ou Information");
        d.category = value;
    } else if (f == "groupe") {
        d.group = value;
    } else if (f == "delai") {
        double ms = 0;
        if (!hmi::parseNumber(value, ms) || ms < 0) return fail("d\xC3\xA9lai : un nombre de millisecondes (0 ou plus)");
        d.delayMs = static_cast<int>(ms);
    } else if (f == "acquittement") {
        d.ackRequired = yes(value);
    } else if (f == "description") {
        d.description = value;
    } else {
        return fail("champ inconnu : " + std::string(f));
    }
    return true;
}

// ================================================================ A1 ========
std::string addSymbolAlarm(const hmi::DocumentPtr& doc, const Apply& apply, Id symbol, std::string name, std::string* why) {
    const hmi::View* sym = doc->project.view(symbol);
    if (!sym || !hmi::isSymbolView(*sym)) {
        if (why) *why = "pas un symbole";
        return {};
    }
    if (!name.empty() && !hmi::isIdentifier(name)) {
        if (why) *why = "nom invalide : lettres, chiffres et _ (" + name + ")";
        return {};
    }
    const std::string n = freeName(*sym, name.empty() ? std::string("Alarme") : name);
    auto cmd = hmi::changeProject(doc, "Alarme du symbole " + sym->name + " : " + n, [&](hmi::Project& pr) {
        hmi::View* s = symbolView(pr, symbol);
        if (!s) return;
        hmi::AlarmDef a;
        a.id = pr.allocate();
        a.name = n;
        a.condition = "FALSE";
        a.message = "{Nom} : " + n;
        if (std::none_of(s->params.begin(), s->params.end(), [](const hmi::ViewParam& vp) { return vp.name == "Nom"; }))
            a.message = n;
        s->alarms.push_back(std::move(a));
    });
    if (!cmd) return {};
    apply(std::move(cmd));
    return n;
}

bool setSymbolAlarmField(const hmi::DocumentPtr& doc, const Apply& apply, Id symbol, const std::string& alarm, const std::string& field,
                         const std::string& value, std::string* why) {
    const hmi::View* sym = doc->project.view(symbol);
    if (!sym || !hmi::isSymbolView(*sym)) {
        if (why) *why = "pas un symbole";
        return false;
    }
    const auto it = std::find_if(sym->alarms.begin(), sym->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == alarm; });
    if (it == sym->alarms.end()) {
        if (why) *why = "alarme introuvable : " + alarm;
        return false;
    }
    hmi::AlarmDef next = *it;
    if (!parseField(next, field, value, why)) return false;
    if (field == "nom" && next.name != alarm
        && std::any_of(sym->alarms.begin(), sym->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == next.name; })) {
        if (why) *why = "'" + next.name + "' existe d\xC3\xA9j\xC3\xA0 dans le symbole";
        return false;
    }
    if (next == *it) return true;
    const std::string symName = sym->name;
    auto cmd = hmi::changeProject(doc, "Alarme " + alarm + " du symbole " + symName + " : " + fieldLabel(field), [&](hmi::Project& pr) {
        hmi::View* s = symbolView(pr, symbol);
        if (!s) return;
        for (auto& a : s->alarms)
            if (a.name == alarm) a = next;
        // Renommer : les surcharges des instances suivent.
        if (field == "nom") (void)hmi::renameSymbolAlarmOverrides(pr, symName, alarm, next.name);
    });
    if (cmd) apply(std::move(cmd));
    return true;
}

bool deleteSymbolAlarm(const hmi::DocumentPtr& doc, const Apply& apply, Id symbol, const std::string& alarm) {
    auto cmd = hmi::changeProject(doc, "Supprimer l'alarme " + alarm + " du symbole", [&](hmi::Project& pr) {
        if (hmi::View* s = symbolView(pr, symbol))
            s->alarms.erase(std::remove_if(s->alarms.begin(), s->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == alarm; }),
                            s->alarms.end());
    });
    if (!cmd) return false;
    apply(std::move(cmd));
    return true;
}

std::string duplicateSymbolAlarm(const hmi::DocumentPtr& doc, const Apply& apply, Id symbol, const std::string& alarm) {
    const hmi::View* sym = doc->project.view(symbol);
    if (!sym) return {};
    const auto it = std::find_if(sym->alarms.begin(), sym->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == alarm; });
    if (it == sym->alarms.end()) return {};
    const std::string n = freeName(*sym, alarm + "_copie");
    const std::size_t at = static_cast<std::size_t>(it - sym->alarms.begin()) + 1;
    auto cmd = hmi::changeProject(doc, "Dupliquer l'alarme " + alarm + " du symbole", [&](hmi::Project& pr) {
        hmi::View* s = symbolView(pr, symbol);
        if (!s || at > s->alarms.size()) return;
        hmi::AlarmDef copy = s->alarms[at - 1];
        copy.id = pr.allocate();
        copy.name = n;
        s->alarms.insert(s->alarms.begin() + static_cast<long>(at), std::move(copy));
    });
    if (!cmd) return {};
    apply(std::move(cmd));
    return n;
}

bool moveSymbolAlarm(const hmi::DocumentPtr& doc, const Apply& apply, Id symbol, const std::string& alarm, int delta) {
    auto cmd = hmi::changeProject(doc, "Ordonner les alarmes du symbole", [&](hmi::Project& pr) {
        hmi::View* s = symbolView(pr, symbol);
        if (!s) return;
        const auto it = std::find_if(s->alarms.begin(), s->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == alarm; });
        if (it == s->alarms.end()) return;
        const long i = it - s->alarms.begin();
        const long j = i + delta;
        if (j < 0 || j >= static_cast<long>(s->alarms.size())) return;
        std::swap(s->alarms[static_cast<std::size_t>(i)], s->alarms[static_cast<std::size_t>(j)]);
    });
    if (!cmd) return false;
    apply(std::move(cmd));
    return true;
}

std::size_t createSymbolAlarms(const hmi::DocumentPtr& doc, const Apply& apply, Id symbol, const std::vector<hmi::AlarmDef>& defs) {
    std::size_t made = 0;
    auto cmd = hmi::changeProject(doc, "Cr\xC3\xA9" "er " + std::to_string(defs.size()) + " alarme(s) du symbole", [&](hmi::Project& pr) {
        hmi::View* s = symbolView(pr, symbol);
        if (!s) return;
        made = 0;
        for (auto d : defs) {
            if (std::any_of(s->alarms.begin(), s->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == d.name; })) continue;
            d.id = pr.allocate();
            s->alarms.push_back(std::move(d));
            ++made;
        }
    });
    if (!cmd) return 0;
    apply(std::move(cmd));
    return made;
}

std::vector<Proposal> proposals(const hmi::Project& p, const hmi::View& symbol, std::string_view param, std::string_view type) {
    std::vector<Proposal> out;
    // Les noms deja pris : suggestSymbolAlarms les evite (Defaut -> Defaut_1) ; on les
    // montre grises a la place, comme la maquette ("deja : Defaut_Thermique").
    hmi::View bare = symbol;
    bare.alarms.clear();
    for (auto& d : hmi::suggestSymbolAlarms(p, bare, param, type)) {
        Proposal pr;
        pr.taken = std::any_of(symbol.alarms.begin(), symbol.alarms.end(),
                               [&](const hmi::AlarmDef& a) { return a.name == d.name || a.condition == d.condition; });
        pr.def = std::move(d);
        out.push_back(std::move(pr));
    }
    return out;
}

Preview symbolPreview(const hmi::Project& p, const hmi::View& symbol, std::string_view instance) {
    Preview pv;
    const auto list = hmi::instancesOf(p, symbol.name);
    const hmi::View* chosenView = nullptr;
    const hmi::Object* chosen = nullptr;
    for (const auto& [v, o] : list) {
        if (!v || !o || !hmi::viewGeneratesAlarms(*v)) continue;
        PreviewInstance pi;
        pi.view = v->id;
        pi.object = o->id;
        pi.path = hmi::objectGroupOf(*v, *o);
        const auto alarms = hmi::objectAlarmsOf(p, *v, *o);
        for (const auto& a : alarms)
            if (a.path.empty()) {
                ++pi.alarms;
                pi.overriddenFields += a.overridden.size();
            }
        if (!chosen && (instance.empty() || instance == pi.path)) {
            chosen = o;
            chosenView = v;
        }
        pv.instances.push_back(std::move(pi));
    }
    if (!chosen) return pv;
    pv.instance = hmi::objectGroupOf(*chosenView, *chosen);
    pv.group = pv.instance;
    if (const auto* a = chosen->find("params")) pv.arguments = a->value;
    std::set<std::string> declared;
    for (auto& a : hmi::objectAlarmsOf(p, *chosenView, *chosen)) {
        if (!a.path.empty()) continue;           // les objets du symbole ont leur propre groupe
        if (!a.def.group.empty()) declared.insert(a.def.group);
        pv.alarms.push_back(std::move(a));
    }
    for (const auto& d : declared) pv.declared += (pv.declared.empty() ? "" : ", ") + d;
    for (const auto& iv : hmi::pub::kAlarmInfo) pv.publicVars.push_back(pv.group + "." + std::string(iv.name));
    return pv;
}

std::string symbolStatus(const hmi::Project& p, const hmi::View& symbol) {
    std::string params;
    for (const auto& vp : symbol.params) params += (params.empty() ? "" : ", ") + vp.name;
    const std::size_t inst = hmi::instancesOf(p, symbol.name).size();
    return symbol.name + kDot + "symbole" + kDot + std::to_string(symbol.params.size()) + " param\xC3\xA8tre(s)"
           + (params.empty() ? std::string{} : " (" + params + ")") + kDot + std::to_string(symbol.alarms.size()) + " alarme(s)" + kDot
           + std::to_string(inst) + " instance(s)";
}

// ================================================================ A2 ========
ObjectSection objectSection(const hmi::Project& p, const hmi::View& v, const hmi::Object& o) {
    ObjectSection s;
    s.carries = hmi::kindIsSynoptic(o.kind) || o.kind == hmi::Kind::SymbolInstance;
    if (!s.carries) return s;
    s.group = hmi::objectGroupOf(v, o);
    const auto list = hmi::objectAlarmsOf(p, v, o);
    std::size_t fromSymbol = 0, active = 0, fields = 0, unusable = 0;
    for (const auto& oa : list) {
        AlarmRow row;
        row.localName = oa.localName;
        row.path = oa.path;
        row.symbol = oa.symbol;
        row.name = oa.def.name;
        row.active = oa.active;
        row.priority = oa.def.priority;
        row.category = oa.def.category;
        row.overridden = oa.overridden.size();
        fromSymbol += oa.source == hmi::ObjectAlarmSource::Symbol;
        active += oa.active;
        fields += oa.overridden.size();
        const hmi::AlarmDef raw = rawBase(p, oa);
        const hmi::AlarmOverride* ov = hmi::findOverride(o, oa.path, oa.localName);
        const std::string where = o.name + (oa.path.empty() ? std::string{} : "." + oa.path);
        for (const auto& f : hmi::overrideFields()) {
            FieldRow fr;
            fr.field = f;
            fr.label = fieldLabel(f);
            fr.base = f == "active" ? std::string("oui") : fieldValue(raw, f);
            const auto over = ov ? overrideText(*ov, f) : std::nullopt;
            fr.overridden = over.has_value();
            fr.value = over ? *over : fr.base;
            if (isTextField(f)) {
                const std::string dev = fieldValue(oa.def, f);
                if (dev != fr.value) fr.developed = dev;
            }
            if (fr.overridden)
                fr.tip = fr.label + " surcharg\xC3\xA9" "e sur " + where + "\nValeur " + sourceName(oa) + " : " + fr.base
                         + "\nIci : " + fr.value + ". Le symbole peut changer : cette valeur reste.";
            else
                fr.tip = "Valeur " + sourceName(oa) + ". La changer ici la surcharge sur " + where + ".";
            row.fields.push_back(std::move(fr));
        }
        s.alarms.push_back(std::move(row));
    }
    // A5 : les alarmes par defaut "sans objet" d'un objet du synoptique, grisees.
    if (hmi::kindIsSynoptic(o.kind))
        for (const auto& la : hmi::libraryAlarms(o)) {
            if (la.applicable) continue;
            AlarmRow row;
            row.localName = la.def.name;
            row.name = s.group + "." + la.def.name;
            row.active = false;
            row.applicable = false;
            row.why = "sans objet : " + la.why;
            row.priority = la.def.priority;
            row.category = la.def.category;
            s.alarms.push_back(std::move(row));
            ++unusable;
        }
    if (o.kind == hmi::Kind::SymbolInstance)
        s.summary = std::to_string(fromSymbol) + " du symbole" + kDot + std::to_string(active) + " actives" + kDot + std::to_string(fields)
                    + " champs surcharg\xC3\xA9s";
    else
        s.summary = std::string("par d\xC3\xA9" "faut") + kDot + std::to_string(active) + " actives" + kDot + std::to_string(unusable) + " sans objet";
    for (const auto& iv : hmi::pub::kAlarmInfo)
        s.publicVars.push_back({s.group + "." + std::string(iv.name), std::string(iv.type), std::string(iv.text)});
    return s;
}

namespace {

// Applique `edit` a la surcharge (view, object, path, alarm) dans une commande.
bool editOverride(const hmi::DocumentPtr& doc, const Apply& apply, Id view, Id object, const std::string& path, const std::string& alarm,
                  const std::string& label, const std::function<void(hmi::Object&)>& edit) {
    auto cmd = hmi::changeProject(doc, label, [&](hmi::Project& pr) {
        hmi::View* v = pr.view(view);
        hmi::Object* o = v ? v->object(object) : nullptr;
        if (o) edit(*o);
    });
    (void)path;
    (void)alarm;
    if (!cmd) return false;
    apply(std::move(cmd));
    return true;
}

} // namespace

bool setOverride(const hmi::DocumentPtr& doc, const Apply& apply, Id view, Id object, const std::string& path, const std::string& alarm,
                 const std::string& field, const std::string& value, std::string* why) {
    const hmi::View* v = doc->project.view(view);
    const hmi::Object* o = v ? v->object(object) : nullptr;
    if (!o) {
        if (why) *why = "objet introuvable";
        return false;
    }
    if (field == "active") return setActive(doc, apply, view, object, path, alarm, yes(value));
    const auto list = hmi::objectAlarmsOf(doc->project, *v, *o);
    const hmi::ObjectAlarm* oa = generatedFor(list, path, alarm);
    if (!oa) {
        if (why) *why = "alarme introuvable sur l'objet : " + alarm;
        return false;
    }
    const hmi::AlarmDef raw = rawBase(doc->project, *oa);
    hmi::AlarmDef next = raw;
    if (!parseField(next, field, value, why)) return false;
    const bool same = fieldValue(next, field) == fieldValue(raw, field);
    return editOverride(doc, apply, view, object, path, alarm, fieldLabel(field) + " de " + alarm + " sur " + o->name, [&](hmi::Object& obj) {
        if (same) (void)hmi::resetOverride(obj, path, alarm, field);     // la valeur du symbole : plus de surcharge
        else writeOverride(hmi::overrideOf(obj, path, alarm), field, next);
    });
}

bool setActive(const hmi::DocumentPtr& doc, const Apply& apply, Id view, Id object, const std::string& path, const std::string& alarm,
               bool active) {
    return editOverride(doc, apply, view, object, path, alarm,
                        std::string(active ? "Cocher" : "D\xC3\xA9" "cocher") + " l'alarme " + alarm, [&](hmi::Object& obj) {
                            if (active) (void)hmi::resetOverride(obj, path, alarm, "active");   // active : la valeur par defaut
                            else hmi::overrideOf(obj, path, alarm).active = false;
                        });
}

bool revert(const hmi::DocumentPtr& doc, const Apply& apply, Id view, Id object, const std::string& path, const std::string& alarm,
            const std::string& field) {
    return editOverride(doc, apply, view, object, path, alarm,
                        field.empty() ? "Tout revenir : " + alarm : "Revenir \xC3\xA0 la valeur du symbole : " + fieldLabel(field),
                        [&](hmi::Object& obj) { (void)hmi::resetOverride(obj, path, alarm, field); });
}

namespace {
constexpr const char* kNoIhmGroup = "(aucun)";   // 1.10.2 (AL) : la case Groupe de l'IHM, sans lien
}

std::vector<PG::Category> inspectorCategories(const hmi::DocumentPtr& doc, const Apply& apply, Id view, Id object,
                                              const domain::Project* plc) {
    std::vector<PG::Category> out;
    const hmi::View* v = doc->project.view(view);
    const hmi::Object* o = v ? v->object(object) : nullptr;
    if (!o) return out;
    const ObjectSection s = objectSection(doc->project, *v, *o);
    if (!s.carries) return out;
    const auto paths = std::make_shared<const hmi::exprcheck::PlcPaths>(hmiPlcPaths(plc));
    PG::Category alarms;
    alarms.name = "Alarmes de l'objet";
    {
        PG::Property sum;
        sum.name = "R\xC3\xA9sum\xC3\xA9";
        sum.value = s.summary;
        sum.description = "Groupe d'alarmes interne : " + s.group + ". Une case d\xC3\xA9" "coch\xC3\xA9" "e, un champ chang\xC3\xA9 : une surcharge, sur cet objet seulement.";
        alarms.properties.push_back(std::move(sum));
    }
    // 1.10.2 (AL) : la case "Groupe de l'IHM" - le groupe d'alarmes de IHM > Alarmes
    // auquel ce groupe d'objets est lie ("(aucun)" : pas de lien). Une commande (Ctrl+Z).
    // Ce qui gagne : la surcharge "groupe" d'une alarme, puis ce lien, puis le symbole.
    {
        PG::Property link;
        link.name = "Groupe de l'IHM";
        link.type = PG::ValueType::Enum;
        link.enumValues.push_back(kNoIhmGroup);
        for (const auto& g : hmi::alarmGroupsOf(doc->project)) link.enumValues.push_back(g.name);
        const auto* own = hmi::alarmGroupLinkFor(doc->project, s.group);
        link.value = own ? own->group : std::string(kNoIhmGroup);
        if (own && std::find(link.enumValues.begin(), link.enumValues.end(), own->group) == link.enumValues.end())
            link.enumValues.push_back(own->group);   // un lien vers un groupe absent : Compiler le dit
        link.description = "Le groupe d'alarmes de IHM \xE2\x80\xBA Alarmes dont les alarmes de " + s.group
                           + " prennent le comportement (priorit\xC3\xA9 par d\xC3\xA9" "faut, couleurs, acquittement, son, zone, niveau, archivage). "
                             "Une surcharge \xC2\xAB groupe \xC2\xBB d'une alarme passe avant.";
        if (!own)
            if (const auto* inherited = hmi::alarmGroupLinkOf(doc->project, s.group)) {
                link.placeholder = inherited->group + " (par " + inherited->objectGroup + ")";
                link.description += " Aujourd'hui : " + inherited->group + ", par le lien de " + inherited->objectGroup + ".";
            }
        const std::string group = s.group;
        link.commit = [doc, apply, group](std::string_view chosen) {
            const std::string target = chosen == kNoIhmGroup ? std::string{} : std::string(chosen);
            const auto* now = hmi::alarmGroupLinkFor(doc->project, group);
            if ((now ? now->group : std::string{}) == target) return true;
            auto cmd = hmi::linkObjectGroupsCmd(doc, {group}, target);
            if (!cmd) return false;
            apply(std::move(cmd));
            return true;
        };
        alarms.properties.push_back(std::move(link));
    }
    // Des noms stables (la grille garde alors ce qui est deplie et le defilement
    // d'une saisie a l'autre) : l'etat de l'alarme dans sa premiere ligne, le
    // repere SURCHARGE et la valeur du symbole dans l'aide de chaque champ, une
    // seule ligne "Revenir a la valeur du symbole" (un champ, ou Tout revenir).
    for (const auto& row : s.alarms) {
        PG::Category c;
        c.name = row.path.empty() ? row.localName : row.path + "." + row.localName;
        c.expanded = false;
        PG::Property state;
        state.name = "\xC3\x89tat";
        if (!row.applicable) {
            state.value = row.why;
            c.properties.push_back(std::move(state));
            alarms.children.push_back(std::move(c));
            continue;
        }
        state.value = std::string(row.active ? "active" : "d\xC3\xA9" "sactiv\xC3\xA9" "e sur cet objet")
                      + (row.overridden ? kDot + std::to_string(row.overridden) + " SURCHARG\xC3\x89" "S" : std::string{});
        state.description = row.name;
        const std::string path = row.path, name = row.localName;
        // 1.9 (chantier U) : Tout revenir, au bout de la ligne de l'etat.
        if (row.overridden) {
            state.revertTip = "Tout revenir : " + name;
            state.revert = [doc, apply, view, object, path, name] { (void)revert(doc, apply, view, object, path, name); };
        }
        c.properties.push_back(std::move(state));
        std::vector<std::string> revertable{"", "Tout revenir"};
        for (const auto& f : row.fields) {
            PG::Property pr;
            pr.name = f.label;
            pr.value = f.value;
            pr.description = (f.overridden ? std::string("SURCHARG\xC3\x89") + kDot : std::string{}) + f.tip;
            const std::string field = f.field;
            if (field == "active" || field == "acquittement") {
                pr.type = PG::ValueType::Boolean;
                pr.value = f.value == "oui" ? "true" : "false";
            } else if (field == "priorite") {
                pr.type = PG::ValueType::Enum;
                pr.enumValues = priorityChoices();
            } else if (field == "categorie") {
                pr.type = PG::ValueType::Enum;
                pr.enumValues = hmi::alarmCategories();
            } else if (field == "delai") {
                pr.type = PG::ValueType::Integer;
            } else {
                pr.type = PG::ValueType::Text;
            }
            // Decision 7 : la condition est une expression - la pastille fx (rouge
            // si impossible), jugee dans les termes du symbole qui declare
            // l'alarme (ses parametres connus) ; surchargee : fx ET le repere.
            const hmi::View* terms = row.symbol.empty() ? v : doc->project.viewByName(row.symbol);
            if (field == "condition" && !f.value.empty()) {
                pr.expression = f.value;
                pr.exprError = conditionError(doc->project, terms ? *terms : *v, f.value, plc, nullptr, paths.get());
            }
            pr.commit = [doc, apply, view, object, path, name, field](std::string_view val) {
                std::string text(val);
                if (field == "active" || field == "acquittement") text = yes(text) ? "oui" : "non";
                // 1.10 (chantier K) : vider la condition ou le delai d'une alarme de l'objet
                // revient a la valeur du symbole (le delai vide etait refuse).
                if ((field == "condition" || field == "delai") && trimmed(text).empty())
                    return revert(doc, apply, view, object, path, name, field);
                return setOverride(doc, apply, view, object, path, name, field, text);
            };
            if (field == "condition") ui::exprfield::markWhole(pr, ui::exprfield::Expect::Bool, false);   // 1.10 (chantier K)
            if (field == "delai") ui::exprfield::mark(pr, ui::exprfield::Expect::Number);
            // 1.9 (chantier U) : le repere dans la ligne - lisere et etiquette
            // SURCHARGE sur la valeur, la valeur du symbole dans l'infobulle, le
            // bouton de retour au bout de la ligne (une commande : Ctrl+Z).
            if (f.overridden) {
                pr.overridden = true;
                pr.overrideDefault = f.base;
                pr.overrideFrom = row.symbol.empty() ? std::string("Valeur de la biblioth\xC3\xA8que")
                                                     : "Valeur du symbole " + row.symbol;
                pr.revertTip = row.symbol.empty() ? std::string("Revenir \xC3\xA0 la valeur de la biblioth\xC3\xA8que")
                                                  : std::string("Revenir \xC3\xA0 la valeur du symbole");
                pr.revert = [doc, apply, view, object, path, name, field] {
                    (void)revert(doc, apply, view, object, path, name, field);
                };
            }
            c.properties.push_back(std::move(pr));
            if (isTextField(field) && field != "groupe") {
                // La forme developpee, en lecture seule (toujours la : une ligne stable).
                PG::Property dev;
                dev.name = f.label + " (d\xC3\xA9velopp\xC3\xA9" "e)";
                dev.value = f.developed.empty() ? f.value : f.developed;
                dev.description = "Sur cet objet, les param\xC3\xA8tres remplac\xC3\xA9s par les arguments : lecture seule.";
                if (field == "condition" && !dev.value.empty()) {
                    dev.expression = dev.value;   // la forme developpee, jugee dans la vue de l'objet
                    dev.exprError = conditionError(doc->project, *v, dev.value, plc, nullptr, paths.get());
                }
                c.properties.push_back(std::move(dev));
            }
            if (f.overridden) revertable.push_back(f.label);
        }
        PG::Property back;
        back.name = "Revenir \xC3\xA0 la valeur du symbole";
        back.type = PG::ValueType::Enum;
        back.enumValues = revertable;
        back.value = "";
        back.description = row.overridden ? "Choisis un champ surcharg\xC3\xA9, ou Tout revenir." : "Aucun champ surcharg\xC3\xA9.";
        std::vector<std::pair<std::string, std::string>> labelToField;
        for (const auto& f : row.fields) labelToField.emplace_back(f.label, f.field);
        back.commit = [doc, apply, view, object, path, name, labelToField](std::string_view val) {
            if (val.empty()) return true;
            if (val == "Tout revenir") return revert(doc, apply, view, object, path, name);
            for (const auto& [label, field] : labelToField)
                if (label == val) return revert(doc, apply, view, object, path, name, field);
            return false;
        };
        c.properties.push_back(std::move(back));
        alarms.children.push_back(std::move(c));
    }
    out.push_back(std::move(alarms));
    PG::Category vars;
    vars.name = "Variables publiques de l'objet";
    vars.expanded = false;
    for (const auto& pv : s.publicVars) {
        PG::Property pr;
        pr.name = pv.path;
        pr.value = pv.type;
        pr.description = pv.meaning + " Lecture seule.";
        vars.properties.push_back(std::move(pr));
    }
    out.push_back(std::move(vars));
    return out;
}

// ================================================================ A3 ========
std::vector<TreeRow> generatedTree(const hmi::Project& p) {
    std::vector<TreeRow> out;
    const auto all = hmi::objectAlarms(p);
    // Decision 11 h 55 : Symboles d'abord (les alarmes ecrites par le client), puis Objets du synoptique.
    for (const bool symbols : {true, false}) {
        std::vector<const hmi::ObjectAlarm*> part;
        for (const auto& a : all)
            if ((a.source == hmi::ObjectAlarmSource::Symbol || !a.symbols.empty()) == symbols) part.push_back(&a);
        if (part.empty()) continue;
        TreeRow cat;
        cat.kind = RowKind::Category;
        cat.label = symbols ? "Symboles" : "Objets du synoptique";
        std::size_t act = 0;
        for (const auto* a : part) act += a->active;
        cat.count = std::to_string(part.size()) + kDot + std::to_string(act) + " actives";
        out.push_back(cat);
        std::string lastView;
        std::string lastGroup;
        for (std::size_t i = 0; i < part.size(); ++i) {
            const hmi::ObjectAlarm& a = *part[i];
            if (a.view != lastView) {
                TreeRow vr;
                vr.kind = RowKind::View;
                vr.depth = 1;
                vr.label = a.view;
                vr.view = a.viewId;
                std::size_t n = 0;
                for (std::size_t k = i; k < part.size() && part[k]->view == a.view; ++k) ++n;
                vr.count = std::to_string(n);
                out.push_back(vr);
                lastView = a.view;
                lastGroup.clear();
            }
            if (a.objectGroup != lastGroup) {
                TreeRow orow;
                orow.kind = RowKind::Object;
                orow.depth = 2;
                orow.label = a.objectGroup.substr(a.view.size() + 1);
                orow.detail = (a.symbol.empty() ? std::string(hmi::kindLabel(a.kind)) : a.symbol) + kDot + a.objectGroup + ".*";
                orow.view = a.viewId;
                orow.object = a.objectId;
                orow.path = a.path;
                std::size_t n = 0, on = 0;
                for (std::size_t k = i; k < part.size() && part[k]->objectGroup == a.objectGroup; ++k) {
                    ++n;
                    on += part[k]->active;
                }
                orow.count = std::to_string(n) + kDot + std::to_string(on) + " actives";
                out.push_back(orow);
                lastGroup = a.objectGroup;
            }
            TreeRow ar;
            ar.kind = RowKind::Alarm;
            ar.depth = 3;
            ar.label = a.localName;
            ar.detail = a.def.name;
            ar.view = a.viewId;
            ar.object = a.objectId;
            ar.path = a.path;
            ar.alarm = a.localName;
            ar.active = a.active;
            ar.overridden = !a.overridden.empty();
            ar.priority = a.def.priority;
            ar.category = a.def.category;
            ar.groups = (a.def.group.empty() ? std::string{} : a.def.group + kDot) + a.objectGroup;
            ar.condition = a.def.condition;
            out.push_back(std::move(ar));
        }
    }
    return out;
}

std::string generatedSummary(const hmi::Project& p) {
    const auto all = hmi::objectAlarms(p);
    std::size_t act = 0, over = 0;
    for (const auto& a : all) {
        act += a.active;
        over += !a.overridden.empty();
    }
    return std::to_string(p.alarms.size()) + " du projet" + kDot + std::to_string(all.size()) + " g\xC3\xA9n\xC3\xA9r\xC3\xA9" "es par les objets ("
           + std::to_string(act) + " actives)" + kDot + std::to_string(over) + " surcharge(s)";
}

bool openTarget(const TreeRow& row, Id& view, Id& object) {
    if (row.kind == RowKind::Category || row.view == kNoId) return false;
    view = row.view;
    object = row.object;
    return true;
}

// ================================================================ A4 ========
std::vector<GroupChoice> groupChoices(const hmi::Project& p, std::string_view search) {
    std::vector<GroupChoice> out;
    std::string needle = trimmed(search);
    for (auto& ch : needle) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const auto keep = [&](const std::string& text) {
        if (needle.empty()) return true;
        std::string l = text;
        for (auto& ch : l) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return l.find(needle) != std::string::npos;
    };
    // Les groupes declares (projet et generees), avec leur nombre d'alarmes.
    const auto all = hmi::objectAlarms(p);
    std::map<std::string, std::size_t> declared;
    for (const auto& a : p.alarms)
        if (!a.group.empty()) ++declared[a.group];
    for (const auto& a : all)
        if (a.active && !a.def.group.empty()) ++declared[a.def.group];
    for (const auto& [g, n] : declared)
        if (keep(g)) out.push_back({"Groupes du projet", 0, g, g, std::to_string(n) + " alarme(s)"});
    // Les groupes des objets : vue (motif), puis ses objets.
    std::string lastView;
    std::set<std::string> objects;
    for (const auto& a : all) {
        if (a.view != lastView) {
            lastView = a.view;
            objects.clear();
            if (keep(a.view) || needle.empty())
                out.push_back({"Groupes des objets", 0, a.view + kDot + "tous ses objets", a.view + ".*", "motif " + a.view + ".*"});
        }
        // Le groupe de l'objet pose (une instance prend ses objets).
        const std::string group = a.view + "." + a.objectName;
        if (objects.insert(group).second && keep(group))
            out.push_back({"Groupes des objets", 1, a.objectName, group, a.symbol.empty() && a.path.empty() ? std::string("synoptique") : a.symbols});
    }
    // Les symboles qui ont des instances.
    std::set<std::string> syms;
    for (const auto& a : all)
        for (std::size_t at = 0; at <= a.symbols.size() && !a.symbols.empty();) {
            const std::size_t semi = std::min(a.symbols.find(';', at), a.symbols.size());
            syms.insert(a.symbols.substr(at, semi - at));
            at = semi + 1;
        }
    for (const auto& s : syms)
        if (keep(s)) out.push_back({"Symboles", 0, "Tous les objets du symbole " + s, "symbole:" + s, "symbole:" + s});
    return out;
}

bool filterHas(std::string_view filter, std::string_view item) {
    for (std::size_t at = 0; at <= filter.size();) {
        const std::size_t semi = std::min(filter.find(';', at), filter.size());
        if (trimmed(filter.substr(at, semi - at)) == trimmed(item)) return true;
        at = semi + 1;
    }
    return false;
}

std::string toggleFilter(std::string_view filter, std::string_view item, bool on) {
    std::vector<std::string> parts;
    for (std::size_t at = 0; at <= filter.size();) {
        const std::size_t semi = std::min(filter.find(';', at), filter.size());
        const std::string part = trimmed(filter.substr(at, semi - at));
        if (!part.empty() && part != trimmed(item)) parts.push_back(part);
        at = semi + 1;
    }
    if (on && !trimmed(item).empty()) parts.push_back(trimmed(item));
    std::string out;
    for (const auto& part : parts) out += (out.empty() ? "" : "; ") + part;
    return out;
}

std::size_t filterReach(const hmi::Project& p, std::string_view filter) {
    std::size_t n = 0;
    for (const auto& a : p.alarms) n += hmi::alarmGroupMatches(a.group, {}, {}, filter);
    for (const auto& a : hmi::objectAlarms(p))
        if (a.active) n += hmi::alarmGroupMatches(a.def.group, a.objectGroup, a.symbols, filter);
    return n;
}

// ================================================================ A5 ========
int libraryAlarmCount(hmi::Kind kind) {
    if (!hmi::kindIsSynoptic(kind)) return 0;
    hmi::Object o;
    o.kind = kind;
    o.name = "X";
    return static_cast<int>(hmi::libraryAlarms(o).size());
}

void paintBell(const ui::PaintContext& ctx, int count, const gfx::Rect& tile) {
    if (count <= 0) return;
    const auto& c = ctx.theme.color;
    const std::string n = std::to_string(count);
    const float tw = ctx.r.measure(n, ctx.theme.font.smallUi).width;
    const gfx::Rect pill{tile.x + tile.w - tw - 24, tile.y + 3, tw + 21, 14};
    ctx.r.fillRoundedRect(pill, c.warning.withAlpha(40), 7);
    ctx.r.strokeRect(pill, c.warning.withAlpha(160), 1);
    drawHmiGlyph(ctx.r, HmiGlyph::Bell, {pill.x + 3, pill.y + 2, 10, 10}, c.warning);
    ctx.r.drawText({pill.x + 15, pill.y}, n, ctx.theme.font.smallUi, c.warning);
}

std::string librarySummary(const hmi::Object& o) {
    std::size_t on = 0, off = 0;
    for (const auto& la : hmi::libraryAlarms(o)) (la.applicable ? on : off) += 1;
    return std::string("par d\xC3\xA9" "faut") + kDot + std::to_string(on) + " actives" + kDot + std::to_string(off) + " sans objet";
}

// ---- 1.9 (decision 7) : la pastille fx des conditions ----
std::string conditionError(const hmi::Project& p, const hmi::View& where, const std::string& condition, const domain::Project* plc,
                           const std::set<std::string, std::less<>>* plcUpperNames, const hmi::exprcheck::PlcPaths* plcPaths) {
    if (trimmed(condition).empty()) return {};
    // Un booleen est attendu, comme pour la propriete "visible" d'un objet.
    return hmiExpressionError(where, "visible", condition, plc, &p, plcUpperNames, plcPaths);
}

std::string conditionTip(const std::string& condition, const std::string& error) {
    return "Expression : " + condition + (error.empty() ? std::string{} : "\nErreur : " + error);
}

void paintFx(gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) {
    if (icon != kFxIcon) return;
    // La pastille pleine (la couleur de la teinte : accent, rouge si impossible)
    // et "fx" trace en blanc : la meme marque que la grille, en petit.
    const float h = std::min(box.h, 14.f), w = std::max(box.w, 18.f);
    const gfx::Rect pill{box.x - 1.f, box.y + (box.h - h) * 0.5f, w, h};
    r.fillRoundedRect(pill, color, h * 0.5f);
    const gfx::Color ink{255, 255, 255, 255};
    const float x = pill.x + w * 0.5f - 5.f, top = pill.y + 3.f, bottom = pill.y + h - 3.f, mid = pill.y + h * 0.5f - 1.f;
    r.line({x + 2.f, top + 1.f}, {x + 2.f, bottom}, ink, 1.4f);            // f : le fut
    r.line({x + 2.f, top + 1.f}, {x + 4.f, top}, ink, 1.4f);                //     le crochet
    r.line({x, mid}, {x + 4.f, mid}, ink, 1.4f);                            //     la barre
    r.line({x + 5.5f, mid}, {x + 9.5f, bottom}, ink, 1.4f);                 // x
    r.line({x + 9.5f, mid}, {x + 5.5f, bottom}, ink, 1.4f);
}

int conditionBadge(const hmi::Project& p, const hmi::View& v, const hmi::Object& o, const domain::Project* plc,
                   const std::set<std::string, std::less<>>* plcUpperNames, const hmi::exprcheck::PlcPaths* plcPaths) {
    if (!hmi::kindIsSynoptic(o.kind) && o.kind != hmi::Kind::SymbolInstance) return 0;
    int state = 0;
    for (const auto& ov : o.alarmOverrides)
        if (ov.condition) state = 1;
    // Rouge : une alarme generee de l'objet (cochee) dont la condition developpee
    // ne peut pas marcher dans la vue ou l'objet est pose.
    for (const auto& oa : hmi::objectAlarmsOf(p, v, o))
        if (oa.active && !conditionError(p, v, oa.def.condition, plc, plcUpperNames, plcPaths).empty()) return 2;
    return state;
}

} // namespace app::objalarms

// =============================================================================
//  A1 : le volet Alarmes d'un symbole (le sous-onglet Alarmes de son document).
// =============================================================================
#include "HmiPaneKit.hpp"

namespace app {

namespace {
enum SymbolAlarmAction : int { SAAdd = 1, SACreate, SADup, SADel, SAUp, SADown };

// Le type declare d'un parametre du symbole : ViewParam::type une fois fusionne
// le chantier F (decision du 01/10, 11 h 10) ; avant : aucun (vide) - le
// panneau Creer des alarmes le fait alors choisir.
template <typename Param> std::string declaredType(const Param& prm) {
    if constexpr (requires(const Param& x) { x.type; }) return std::string(prm.type);
    else return {};
}

// Les lignes d'un tableau avec une infobulle chacune (decision 7 : la condition,
// "Expression : ... / Erreur : ..." au survol de la ligne).
class TipRows final : public ui::ITableModel {
public:
    TipRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<std::string> tips,
            hmikit::Rows::Style style)
        : rows_(std::move(headers), std::move(rows), std::move(style)), tips_(std::move(tips)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.rowCount(); }
    [[nodiscard]] std::size_t columnCount() const override { return rows_.columnCount(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return rows_.headerText(c); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override { return rows_.cellText(r, c); }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return rows_.cellStyle(r, c); }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return rows_.less(a, b, c); }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips_.size() ? tips_[r] : std::string{}; }

private:
    hmikit::Rows             rows_;
    std::vector<std::string> tips_;
};

// La petite pastille fx d'une case de condition : rouge si `error`.
void fxCell(ui::CellStyle& s, bool error) {
    s.monospace = true;
    s.customIcon = objalarms::kFxIcon;
    s.iconTone = error ? ui::Tone::Error : ui::Tone::Accent;
    if (error) s.fgTone = ui::Tone::Error;
}
} // namespace

HmiSymbolAlarmsPane::HmiSymbolAlarmsPane(std::string id, hmi::DocumentPtr doc, hmi::Id symbol, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), symbol_(symbol), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(SAAdd, HmiGlyph::Plus, "Ajouter une alarme au symbole", "Ajouter");
    tools->add(SACreate, HmiGlyph::Bell, "Cr\xC3\xA9" "er des alarmes depuis un param\xC3\xA8tre typ\xC3\xA9", "Cr\xC3\xA9" "er des alarmes\xE2\x80\xA6");
    tools->add(SADup, HmiGlyph::Duplicate, "Dupliquer l'alarme", "Dupliquer");
    tools->add(SADel, HmiGlyph::Delete, "Supprimer l'alarme (Ctrl+Z la rend)", "Supprimer");
    tools->add(SAUp, HmiGlyph::Up, "Monter l'alarme", "Monter");
    tools->add(SADown, HmiGlyph::Down, "Descendre l'alarme", "Descendre");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    for (int a : {SADup, SADel, SAUp, SADown}) tools_->setEnabledWhen(a, [this] { return !selectedAlarm().empty(); });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Alarme", 170.f}, {"Condition", 240.f}, {"Message", 240.f}, {"Priorit\xC3\xA9", 110.f}, {"Cat\xC3\xA9gorie", 110.f},
                       {"D\xC3\xA9lai (ms)", 90.f, 50.f, true, true, true, ui::Align::End}});
    table->setIconPainter(&objalarms::paintFx);   // decision 7 : la pastille fx des conditions
    table_ = &static_cast<ui::TableView&>(split->addPane(std::move(table), 0.66f, 320.f));
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    // 1.10.2 (chantier A) : la condition d'une alarme du symbole a l'aide a la saisie
    // (variables, Vue.Objet.Propriete, parametres des instances), comme l'inspecteur.
    // Les parametres du symbole (Armoire, Nom) en tete : le temps de la liste, la vue
    // de l'aide a la saisie est le symbole, puis elle redevient celle de l'editeur.
    grid->setFieldAssist([this, base = assist::gridAssist(assist::sourcesFor(doc_))](std::string_view category,
                                                                                    const ui::PropertyGrid::Property& p) -> ui::InputText::Assist {
        auto a = base(category, p);
        if (!a) return a;
        return [this, a](std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
            const hmi::View* was = doc_ ? hmiparams::assistView(doc_->project) : nullptr;
            const hmi::Id keep = was ? was->id : hmi::kNoId;
            hmiparams::setAssistView(symbol_);
            a(before, from, out);
            hmiparams::setAssistView(keep);
        };
    });
    grid_ = &static_cast<ui::PropertyGrid&>(split->addPane(std::move(grid), 0.34f, 260.f));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    auto status = std::make_unique<ui::StatusBar>(base + ".status");
    status_ = &static_cast<ui::StatusBar&>(addChild(std::move(status)));

    links_ += tools_->triggered->connect([this](int a) {
        const std::string cur = selectedAlarm();
        std::string made;
        switch (a) {
            case SAAdd: made = objalarms::addSymbolAlarm(doc_, apply_, symbol_); break;
            case SACreate:
                // "Creer des alarmes..." ouvre le panneau (en tete de la fiche) sur le
                // premier parametre type ; "Creer N alarmes" y cree. Sans parametre :
                // un modele vide (Ajouter).
                if (const hmi::View* sym = doc_->project.view(symbol_); sym && sym->params.empty()) {
                    made = objalarms::addSymbolAlarm(doc_, apply_, symbol_);
                } else if (sym && param_.empty()) {
                    const auto typed = std::find_if(sym->params.begin(), sym->params.end(),
                                                    [](const hmi::ViewParam& prm) { return !declaredType(prm).empty(); });
                    const hmi::ViewParam& prm = typed != sym->params.end() ? *typed : sym->params.front();
                    param_ = prm.name;
                    type_ = declaredType(prm);
                }
                break;
            case SADup: made = objalarms::duplicateSymbolAlarm(doc_, apply_, symbol_, cur); break;
            case SADel: (void)objalarms::deleteSymbolAlarm(doc_, apply_, symbol_, cur); break;
            case SAUp: (void)objalarms::moveSymbolAlarm(doc_, apply_, symbol_, cur, -1); made = cur; break;
            case SADown: (void)objalarms::moveSymbolAlarm(doc_, apply_, symbol_, cur, +1); made = cur; break;
            default: break;
        }
        refresh();
        if (!made.empty()) selectAlarm(made);
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { rebuildProperties(); });
    links_ += doc_->changed->connect([this](hmi::Id) { refresh(); });
    refresh();
}

void HmiSymbolAlarmsPane::refresh() {
    const std::string keep = selectedAlarm();
    names_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> prios;
    std::vector<std::string> tips;
    std::vector<int> fx;   // 0 : pas de condition ; 1 : fx ; 2 : fx rouge
    const hmi::View* sym = doc_->project.view(symbol_);
    if (sym)
        for (const auto& a : sym->alarms) {
            names_.push_back(a.name);
            prios.push_back(a.priority);
            rows.push_back({a.name, a.condition, a.message, objalarms::fieldValue(a, "priorite"), a.category, std::to_string(a.delayMs)});
            // Decision 7 : la colonne Condition porte la petite pastille fx.
            const std::string err = objalarms::conditionError(doc_->project, *sym, a.condition, plc ? plc() : nullptr);
            fx.push_back(a.condition.empty() ? 0 : err.empty() ? 1 : 2);
            tips.push_back(a.condition.empty() ? std::string{} : objalarms::conditionTip(a.condition, err));
        }
    model_ = std::make_shared<TipRows>(
        std::vector<std::string>{"Alarme", "Condition", "Message", "Priorit\xC3\xA9", "Cat\xC3\xA9gorie", "D\xC3\xA9lai (ms)"}, std::move(rows),
        std::move(tips), [prios, fx](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r < prios.size() && c == 0) s.bold = true;
            if (c == 1) s.monospace = true;
            if (c == 1 && r < fx.size() && fx[r] > 0) fxCell(s, fx[r] == 2);
            return s;
        });
    table_->setModel(model_);
    if (!keep.empty()) selectAlarm(keep);
    preview_ = sym ? objalarms::symbolPreview(doc_->project, *sym, instance_) : objalarms::Preview{};
    statusText_ = sym ? objalarms::symbolStatus(doc_->project, *sym) : std::string{};
    status_->setMessage(statusText_);
    rebuildProperties();
}

std::string HmiSymbolAlarmsPane::selectedAlarm() const {
    const int r = hmikit::selectedRow(*table_);
    return r >= 0 && static_cast<std::size_t>(r) < names_.size() ? names_[static_cast<std::size_t>(r)] : std::string{};
}

void HmiSymbolAlarmsPane::selectAlarm(const std::string& name) {
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == name) { table_->selectModelRows({static_cast<ui::RowIndex>(i)}); return; }
}

void HmiSymbolAlarmsPane::setProposalSource(std::string param, std::string type) {
    param_ = std::move(param);
    type_ = std::move(type);
    rebuildProperties();
}

std::size_t HmiSymbolAlarmsPane::createProposals() {
    const hmi::View* sym = doc_->project.view(symbol_);
    if (!sym || param_.empty()) return 0;
    std::vector<hmi::AlarmDef> defs;
    for (const auto& pr : objalarms::proposals(doc_->project, *sym, param_, type_))
        if (!pr.taken) defs.push_back(pr.def);
    const std::size_t n = objalarms::createSymbolAlarms(doc_, apply_, symbol_, defs);
    refresh();
    return n;
}

void HmiSymbolAlarmsPane::setPreviewInstance(std::string instance) {
    instance_ = std::move(instance);
    refresh();
}

namespace {
// Une ligne en lecture seule de la fiche.
ui::PropertyGrid::Property ro(std::string name, std::string value) {
    ui::PropertyGrid::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    return p;
}
} // namespace

void HmiSymbolAlarmsPane::rebuildProperties() {
    using PG = ui::PropertyGrid;
    std::vector<PG::Category> cats;
    const hmi::View* sym = doc_->project.view(symbol_);
    if (!sym) { grid_->setCategories({}); return; }
    const std::string cur = selectedAlarm();
    const auto it = std::find_if(sym->alarms.begin(), sym->alarms.end(), [&](const hmi::AlarmDef& a) { return a.name == cur; });
    if (it != sym->alarms.end()) {
        PG::Category fiche;
        fiche.name = cur;
        for (const char* f : {"nom", "condition", "message", "priorite", "categorie", "groupe", "delai", "acquittement", "consigne", "description"}) {
            PG::Property pr;
            const std::string field = f;
            pr.name = objalarms::fieldLabel(field);
            pr.value = objalarms::fieldValue(*it, field);
            pr.type = field == "priorite" || field == "categorie" ? PG::ValueType::Enum
                      : field == "acquittement"                   ? PG::ValueType::Boolean
                      : field == "delai"                          ? PG::ValueType::Integer
                                                                  : PG::ValueType::Text;
            if (field == "priorite")
                for (int p = 1; p <= hmi::kAlarmPriorities; ++p) pr.enumValues.push_back(std::to_string(p) + " - " + std::string(hmi::alarmPriorityLabel(p)));
            if (field == "categorie") pr.enumValues = hmi::alarmCategories();
            if (field == "acquittement") pr.value = it->ackRequired ? "true" : "false";
            pr.description = "Les param\xC3\xA8tres du symbole s'emploient comme dans ses objets : Moteur.Defaut, {Nom}. Une instance remplace chaque param\xC3\xA8tre par son argument.";
            // Decision 7 : la condition porte la pastille fx (rouge si impossible),
            // jugee avec les parametres du symbole connus.
            if (field == "condition" && !pr.value.empty()) {
                pr.expression = pr.value;
                pr.exprError = objalarms::conditionError(doc_->project, *sym, pr.value, plc ? plc() : nullptr);
            }
            pr.commit = [this, cur, field](std::string_view v) {
                std::string text(v);
                if (field == "acquittement") text = (text == "true" || text == "1") ? "oui" : "non";
                return objalarms::setSymbolAlarmField(doc_, apply_, symbol_, cur, field, text);
            };
            if (field == "condition") ui::exprfield::markWhole(pr, ui::exprfield::Expect::Bool, false);   // 1.10 (chantier K)
            fiche.properties.push_back(std::move(pr));
        }
        cats.push_back(std::move(fiche));
        // "Dans les instances" : le nom genere, les groupes, les instances, les surcharges.
        PG::Category inst;
        inst.name = "Dans les instances";
        std::size_t over = 0;
        std::string overOn;
        for (const auto& pi : preview_.instances) {
            const hmi::View* v = doc_->project.view(pi.view);
            const hmi::Object* o = v ? v->object(pi.object) : nullptr;
            if (const auto* ov = o ? hmi::findOverride(*o, "", cur) : nullptr) {
                ++over;
                overOn += (overOn.empty() ? "" : ", ") + o->name;
                (void)ov;
            }
        }
        inst.properties.push_back(ro("Nom g\xC3\xA9n\xC3\xA9r\xC3\xA9", "<Vue>.<Instance>." + cur));
        inst.properties.push_back(ro("Groupes", (it->group.empty() ? std::string{} : it->group + " et ") + "celui de l'objet"));
        inst.properties.push_back(ro("Instances", std::to_string(preview_.instances.size())));
        inst.properties.push_back(ro("Surcharg\xC3\xA9" "e sur", std::to_string(over) + (overOn.empty() ? std::string{} : " : " + overOn)));
        cats.push_back(std::move(inst));
    }
    // Creer des alarmes (le panneau ouvert par "Creer des alarmes...") : le
    // parametre, son type, les alarmes proposees (deja prises : marquees), puis
    // Creer et Fermer. En tete de la fiche : c'est ce qu'on vient de demander.
    if (!param_.empty()) {
        PG::Category create;
        create.name = "Cr\xC3\xA9" "er des alarmes";
        PG::Property from;
        from.name = "Depuis le param\xC3\xA8tre";
        from.type = PG::ValueType::Enum;
        for (const auto& prm : sym->params) from.enumValues.push_back(prm.name);
        if (std::find(from.enumValues.begin(), from.enumValues.end(), param_) == from.enumValues.end()) from.enumValues.push_back(param_);
        from.value = param_;
        from.description = "Ses membres donnent les alarmes : une par membre BOOL, deux seuils par membre num\xC3\xA9rique.";
        from.commit = [this](std::string_view v) {
            const hmi::View* s = doc_->project.view(symbol_);
            const hmi::ViewParam* prm = s ? s->param(v) : nullptr;
            if (!prm) return false;
            param_ = prm->name;
            type_ = declaredType(*prm);
            rebuildProperties();
            return true;
        };
        create.properties.push_back(std::move(from));
        PG::Property type;
        type.name = "Type du param\xC3\xA8tre";
        type.type = PG::ValueType::Enum;
        type.enumValues = {"", "BOOL"};
        for (const auto& t : doc_->project.programs.types) type.enumValues.push_back(t.name);
        if (std::find(type.enumValues.begin(), type.enumValues.end(), type_) == type.enumValues.end()) type.enumValues.push_back(type_);
        type.value = type_;
        type.description = "Un type IHM du projet (ses membres) ou BOOL. Choisis-le si le param\xC3\xA8tre n'en d\xC3\xA9" "clare pas.";
        type.commit = [this](std::string_view v) {
            type_ = std::string(v);
            rebuildProperties();
            return true;
        };
        create.properties.push_back(std::move(type));
        std::size_t free = 0;
        for (const auto& pr : objalarms::proposals(doc_->project, *sym, param_, type_)) {
            free += pr.taken ? 0 : 1;
            create.properties.push_back(ro(pr.def.name, pr.taken ? "d\xC3\xA9j\xC3\xA0 pris : " + pr.def.condition
                                                                 : pr.def.condition + "  \xC2\xAB " + pr.def.message + " \xC2\xBB"));
        }
        PG::Property go;
        go.name = "Cr\xC3\xA9" "er les alarmes propos\xC3\xA9" "es";
        go.type = PG::ValueType::Boolean;
        go.value = "false";
        go.description = free ? "Coche : " + std::to_string(free) + " alarme(s) cr\xC3\xA9\xC3\xA9" "e(s) dans le symbole, en une fois (Ctrl+Z les retire)."
                              : std::string("Rien \xC3\xA0 cr\xC3\xA9" "er : choisis un param\xC3\xA8tre et son type.");
        if (free) go.commit = [this](std::string_view) { return createProposals() > 0; };
        create.properties.push_back(std::move(go));
        PG::Property close;
        close.name = "Fermer";
        close.type = PG::ValueType::Boolean;
        close.value = "false";
        close.description = "Ferme le panneau Cr\xC3\xA9" "er des alarmes.";
        close.commit = [this](std::string_view) {
            param_.clear();
            type_.clear();
            rebuildProperties();
            return true;
        };
        create.properties.push_back(std::move(close));
        cats.insert(cats.begin(), std::move(create));
    }
    // L'apercu du groupe genere sur l'instance choisie.
    PG::Category prev;
    prev.name = "Aper\xC3\xA7u du groupe g\xC3\xA9n\xC3\xA9r\xC3\xA9";
    if (preview_.instance.empty()) {
        prev.properties.push_back(ro("Sur l'instance", "aucune instance pos\xC3\xA9" "e"));
    } else {
        prev.properties.push_back(ro("Sur l'instance", preview_.instance));
        prev.properties.push_back(ro("Arguments", preview_.arguments));
        prev.properties.push_back(ro("Groupe interne", preview_.group + (preview_.declared.empty() ? std::string{} : " et \xC2\xAB " + preview_.declared + " \xC2\xBB")));
        // Decision 7 : la condition developpee de chaque alarme porte la pastille fx,
        // jugee dans la vue de l'instance ; le message (texte a trous) dans l'aide.
        const hmi::View* where = doc_->project.viewByName(preview_.instance.substr(0, preview_.instance.find('.')));
        for (const auto& a : preview_.alarms) {
            PG::Property pr = ro(a.def.name, a.def.condition);
            if (!a.def.condition.empty()) {
                pr.expression = a.def.condition;
                pr.exprError = objalarms::conditionError(doc_->project, where ? *where : *sym, a.def.condition, plc ? plc() : nullptr);
            }
            pr.description = "\xC2\xAB " + a.def.message + " \xC2\xBB";
            prev.properties.push_back(std::move(pr));
        }
        for (const auto& pi : preview_.instances)
            prev.properties.push_back(ro(pi.path, std::to_string(pi.alarms) + " alarme(s)"
                                                    + (pi.overriddenFields ? " \xC2\xB7 " + std::to_string(pi.overriddenFields) + " champ(s) surcharg\xC3\xA9(s)" : std::string{})));
    }
    cats.push_back(std::move(prev));
    grid_->setCategories(std::move(cats));
}

void HmiSymbolAlarmsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 40, b.w, std::max(0.f, b.h - 64)});
}

void HmiSymbolAlarmsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

// ---------------------------------------------------------- A3 : les generees ----
HmiGeneratedAlarmsTable::HmiGeneratedAlarmsTable(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    auto table = std::make_unique<ui::TableView>(this->id() + ".table");
    table->setColumns({{"Alarme", 260.f}, {"Priorit\xC3\xA9", 110.f}, {"Cat\xC3\xA9gorie", 110.f}, {"Groupes", 200.f}, {"Condition", 230.f},
                       {"Nombre", 110.f, 50.f, true, true, true, ui::Align::End}});
    table->setIconPainter(&objalarms::paintFx);   // decision 7 : la pastille fx des conditions
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    // 1.9 (chantier U) : la fiche de l'alarme choisie, a droite (A3).
    sheet_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".sheet")));
    sheet_->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));   // 1.10.2 (chantier A)
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
        showSheet(sel.empty() ? -1 : static_cast<int>(sel.front()));
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) { (void)openAt(static_cast<int>(r)); });
    // 1.9 (chantier U) : un clic sur la case coche ou decoche (comme Espace).
    links_ += table_->checkClicked->connect([this](ui::RowIndex r) {
        if (r < rows_.size()) (void)setActiveAt(static_cast<int>(r), !rows_[r].active);
    });
    links_ += doc_->changed->connect([this](hmi::Id) { refresh(); });
    refresh();
}

void HmiGeneratedAlarmsTable::refresh() {
    rows_ = objalarms::generatedTree(doc_->project);
    summary_ = objalarms::generatedSummary(doc_->project);
    std::vector<std::vector<std::string>> cells;
    // Decision 7 : la condition developpee porte la petite pastille fx (rouge si
    // impossible dans la vue de l'objet) et l'infobulle de la ligne.
    std::vector<int> fx;
    std::vector<std::string> tips;
    {
        const domain::Project* program = plc ? plc() : nullptr;
        const auto paths = hmiPlcPaths(program);
        for (const auto& r : rows_) {
            const hmi::View* where = r.kind == objalarms::RowKind::Alarm ? doc_->project.view(r.view) : nullptr;
            const std::string err = where ? objalarms::conditionError(doc_->project, *where, r.condition, program, nullptr, &paths) : std::string{};
            const bool has = where && !r.condition.empty();
            fx.push_back(has ? (err.empty() ? 1 : 2) : 0);
            tips.push_back(has ? objalarms::conditionTip(r.condition, err) : std::string{});
        }
    }
    for (const auto& r : rows_) {
        if (r.kind == objalarms::RowKind::Alarm)
            cells.push_back({r.label, objalarms::fieldValue([&] {
                                 hmi::AlarmDef d;
                                 d.priority = r.priority;
                                 return d;
                             }(), "priorite"),
                             r.category, r.groups, r.condition, std::string{}});
        else
            cells.push_back({r.label + (r.kind == objalarms::RowKind::Object ? std::string(" \xC2\xB7 ") + r.detail : std::string{}), {}, {}, {}, {}, r.count});
    }
    const auto rowsCopy = rows_;
    model_ = std::make_shared<TipRows>(
        std::vector<std::string>{"Alarme", "Priorit\xC3\xA9", "Cat\xC3\xA9gorie", "Groupes", "Condition", "Nombre"}, std::move(cells),
        std::move(tips), [rowsCopy, fx](ui::RowIndex i, std::size_t c) {
            ui::CellStyle st;
            if (i >= rowsCopy.size()) return st;
            const auto& r = rowsCopy[i];
            if (c == 0) {
                st.indent = 16.f * static_cast<float>(r.depth);
                if (r.kind != objalarms::RowKind::Alarm) {
                    st.bold = true;
                    st.expander = 1;
                }
                if (r.kind == objalarms::RowKind::Category) st.badge = "G\xC3\x89N\xC3\x89R\xC3\x89" "ES";
                if (r.kind == objalarms::RowKind::Alarm) {
                    // 1.9 (chantier U) : une vraie case (cocher, decocher : la surcharge).
                    st.check = r.active ? 1 : 0;
                    // Le cadenas gris : generee, non modifiable ici sauf par la surcharge.
                    st.icon = ui::Icon::Lock;
                    st.iconTone = ui::Tone::Muted;
                }
                if (r.kind == objalarms::RowKind::Alarm && r.overridden) {
                    st.badge = "SURCHARG\xC3\x89";
                    st.badgeTone = ui::Tone::Accent;
                }
                if (r.kind == objalarms::RowKind::Alarm && !r.active) st.fgTone = ui::Tone::Muted;
            }
            if (c == 4) st.monospace = true;
            if (c == 4 && i < fx.size() && fx[i] > 0) fxCell(st, fx[i] == 2);
            return st;
        });
    table_->setModel(model_);
    if (!sheetName_.empty()) showSheet(rowOf(sheetName_));   // 1.9 (chantier U) : la fiche suit
}

void HmiGeneratedAlarmsTable::showSheet(int row) {
    using PG = ui::PropertyGrid;
    if (!sheet_) return;
    const std::string before = sheetName_;
    const bool alarm = row >= 0 && static_cast<std::size_t>(row) < rows_.size()
                    && rows_[static_cast<std::size_t>(row)].kind == objalarms::RowKind::Alarm;
    sheetName_ = alarm ? rows_[static_cast<std::size_t>(row)].detail : std::string{};
    std::vector<PG::Category> cats;
    if (alarm) {
        const objalarms::TreeRow r = rows_[static_cast<std::size_t>(row)];
        const hmi::View* v = doc_->project.view(r.view);
        const hmi::Object* o = v ? v->object(r.object) : nullptr;
        std::string symbol;   // le symbole qui la declare ("" : la bibliotheque)
        if (v && o)
            for (const auto& a : objalarms::objectSection(doc_->project, *v, *o).alarms)
                if (a.localName == r.alarm && a.path == r.path) symbol = a.symbol;
        PG::Category head;
        head.name = "G\xC3\xA9n\xC3\xA9r\xC3\xA9" "e par " + (symbol.empty() ? std::string("la biblioth\xC3\xA8que") : symbol)
                    + ", sur " + (o ? o->name : std::string("?"));
        PG::Property name;
        name.name = "Alarme";
        name.value = r.detail;
        name.description = "Ses champs viennent " + std::string(symbol.empty() ? "de la biblioth\xC3\xA8que" : "du symbole")
                           + " ; tu peux les surcharger ici, pour cet objet seulement.";
        head.properties.push_back(std::move(name));
        PG::Property go;
        go.name = "Ouvrir";
        go.type = PG::ValueType::Enum;
        go.enumValues = {"", "L'objet dans sa vue"};
        if (!symbol.empty()) go.enumValues.emplace_back("Le symbole");
        go.description = "L'objet dans sa vue (sur la section Alarmes de l'objet), ou le symbole qui d\xC3\xA9" "clare l'alarme.";
        go.commit = [this, view = r.view, object = r.object, symbol](std::string_view val) {
            if (val == "L'objet dans sa vue") {
                if (open) open(view, object);
                return true;
            }
            if (val == "Le symbole") {
                const hmi::View* s = doc_->project.viewByName(symbol);
                if (s && openSymbol) openSymbol(s->id);
                return s != nullptr;
            }
            return val.empty();
        };
        head.properties.push_back(std::move(go));
        cats.push_back(std::move(head));
        // Ses champs : ceux de la section Alarmes de l'objet (le repere des surcharges compris).
        const std::string key = r.path.empty() ? r.alarm : r.path + "." + r.alarm;
        for (const auto& c : objalarms::inspectorCategories(doc_, apply_, r.view, r.object, plc ? plc() : nullptr))
            for (const auto& sub : c.children)
                if (sub.name == key) {
                    PG::Category field = sub;
                    field.expanded = true;
                    cats.push_back(std::move(field));
                }
    }
    sheet_->setCategories(std::move(cats));
    if (before != sheetName_) invalidateLayout();
}

int HmiGeneratedAlarmsTable::rowOf(const std::string& generatedName) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == objalarms::RowKind::Alarm && rows_[i].detail == generatedName) return static_cast<int>(i);
    return -1;
}

bool HmiGeneratedAlarmsTable::setActiveAt(int row, bool active) {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size() || rows_[static_cast<std::size_t>(row)].kind != objalarms::RowKind::Alarm)
        return false;
    const objalarms::TreeRow r = rows_[static_cast<std::size_t>(row)];
    return objalarms::setActive(doc_, apply_, r.view, r.object, r.path, r.alarm, active);
}

bool HmiGeneratedAlarmsTable::openAt(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return false;
    hmi::Id v = hmi::kNoId, o = hmi::kNoId;
    if (!objalarms::openTarget(rows_[static_cast<std::size_t>(row)], v, o)) return false;
    if (open) open(v, o);
    return true;
}

void HmiGeneratedAlarmsTable::onLayout() {
    // 1.9 (chantier U) : la fiche a droite quand une alarme generee est choisie.
    const auto b = bounds();
    const bool side = sheet_ && !sheetName_.empty() && b.w > 640.f;
    const float w = side ? std::floor(b.w * 0.62f) : b.w;
    table_->setBounds({b.x, b.y, w, b.h});
    if (sheet_) {
        sheet_->setVisibility(side ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        sheet_->setBounds({b.x + w + 4.f, b.y, side ? b.w - w - 4.f : 0.f, b.h});
    }
}

ui::EventResult HmiGeneratedAlarmsTable::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Space) {
        const int r = hmikit::selectedRow(*table_);
        if (r >= 0 && static_cast<std::size_t>(r) < rows_.size()) {
            (void)setActiveAt(r, !rows_[static_cast<std::size_t>(r)].active);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ---------------------------------------------------------- les sous-onglets ----
HmiSymbolTabs::HmiSymbolTabs(std::string id, hmi::DocumentPtr doc, hmi::Id symbol)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), symbol_(symbol) {}

void HmiSymbolTabs::setCurrent(int tab) {
    if (tab < Drawing || tab > Popups || tab == current_) return;
    current_ = tab;
    invalidate();
    changed->emit(tab);
}

std::string HmiSymbolTabs::label(int tab) const {
    const hmi::View* sym = doc_->project.view(symbol_);
    if (tab == Drawing) return "Dessin";
    if (tab == Alarms) return "Alarmes (" + std::to_string(sym ? sym->alarms.size() : 0) + ")";
    if (tab == Operators) return "Op\xC3\xA9rateurs (" + std::to_string(sym ? sym->operators.size() : 0) + ")";   // 1.10 (S2)
    if (tab == Functions) return "Fonctions (" + std::to_string(sym ? sym->functions.size() : 0) + ")";            // 1.11.10
    if (tab == Popups) {
        std::size_t n = 0;
        for (const auto& v : doc_->project.views) n += sym && v.ownerSymbol == sym->id ? 1 : 0;
        return "Popups (" + std::to_string(n) + ")";
    }
    return "Instances (" + std::to_string(sym ? hmi::instancesOf(doc_->project, sym->name).size() : 0) + ")";
}

bool HmiSymbolTabs::tabRect(int tab, gfx::Rect& out) const {
    if (tab < Drawing || tab > Popups) return false;
    const auto b = bounds();
    out = {b.x + 8 + 130.f * static_cast<float>(tab), b.y + 2, 124, b.h - 4};
    return true;
}

void HmiSymbolTabs::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.panelBg);
    for (int t = Drawing; t <= Popups; ++t) {
        gfx::Rect r;
        (void)tabRect(t, r);
        const bool cur = t == current_;
        if (cur) {
            ctx.r.fillRoundedRect(r, c.accent.withAlpha(40), 4);
            ctx.r.fillRect({r.x, r.y + r.h - 2, r.w, 2}, c.accent);
        }
        // 1.11.10 : Fonctions en violet (comme leurs icones dans l'arbre).
        const gfx::Color glyph = t == Functions ? ctx.theme.brand.scopeInOut : cur ? c.accent : c.textMuted;
        drawHmiGlyph(ctx.r, t == Drawing ? HmiGlyph::Symbol : t == Alarms ? HmiGlyph::Bell : t == Operators ? HmiGlyph::Compare
                          : t == Functions ? HmiGlyph::Code : t == Popups ? HmiGlyph::Popup : HmiGlyph::Duplicate,
                     {r.x + 6, r.y + (r.h - 14) / 2, 14, 14}, glyph);
        ctx.r.drawText({r.x + 26, r.y + (r.h - 16) / 2}, label(t), ctx.theme.font.smallUi, cur ? c.accent : c.text);
    }
}

ui::EventResult HmiSymbolTabs::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseDown>(&ev))
        for (int t = Drawing; t <= Popups; ++t) {
            gfx::Rect r;
            if (tabRect(t, r) && r.contains(m->pos)) {
                setCurrent(t);
                return ui::EventResult::Consumed;
            }
        }
    return ui::EventResult::Ignored;
}

} // namespace app
