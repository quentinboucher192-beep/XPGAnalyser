// =============================================================================
//  hmi/HmiObjectAlarms.cpp - les alarmes des objets (1.9) : la bibliotheque,
//                            les symboles, les surcharges, la generation, les
//                            filtres. Voir HmiObjectAlarms.hpp.
// =============================================================================
#include "HmiObjectAlarms.hpp"
#include "HmiAlarmGroups.hpp"
#include "HmiSymbols.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : markers::strip
#include "HmiSources.hpp"   // 1.12.7 (lot E) : « Repeter depuis »
#include "HmiEquipmentLink.hpp"   // 1.12.11 (lot G) : la liaison Equipement remplit le Defaut
#include "HmiRanges.hpp"   // 1.12.10 (lot F) : les seuils pris a la variable
#include "HmiRoles.hpp"    // 1.12.31 : formulaValueKey, d'apres le role de la propriete
#include "HmiWidgets.hpp"  // 1.14.1 : les fins de course

#include <algorithm>
#include <cmath>
#include <optional>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>

namespace hmi {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

// Une constante : TRUE, FALSE, un nombre, une chaine entre apostrophes ou guillemets.
bool isConstant(std::string_view raw) {
    const std::string s = trimmed(raw);
    if (s.empty()) return true;
    const std::string u = upper(s);
    if (u == "TRUE" || u == "FALSE") return true;
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"'))) return true;
    char* end = nullptr;
    (void)std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

const std::string& defautCategory() { return alarmCategories()[0]; }    // "Defaut" (accentue)

LibraryAlarm make(std::string name, std::string condition, std::string message, int priority, std::string category,
                  int delayMs = 0) {
    LibraryAlarm la;
    la.def.name = std::move(name);
    la.def.condition = std::move(condition);
    la.def.message = std::move(message);
    la.def.priority = priority;
    la.def.category = std::move(category);
    la.def.delayMs = delayMs;
    return la;
}

LibraryAlarm unusable(LibraryAlarm la, std::string why) {
    la.applicable = false;
    la.why = std::move(why);
    return la;
}

} // namespace

std::string_view objectAlarmSourceLabel(ObjectAlarmSource s) noexcept {
    return s == ObjectAlarmSource::Symbol ? std::string_view("symbole") : std::string_view("biblioth\xC3\xA8que");
}

std::string objectCaption(const Object& o) {
    std::string label = trimmed(o.text("label"));
    return label.empty() ? o.name : label;
}

bool linksVariable(std::string_view text) {
    if (isConstant(text)) return false;
    // Un booleen ecrit en francais (vrai, faux, oui, non) : une constante, pas un nom.
    const std::string t = trimmed(text);
    return parseBool(t, false) != parseBool(t, true);   // inconnu : le repli decide - un nom
}

bool synopticLinkedKey(const Object& o, std::string_view key) {
    if (!kindIsSynoptic(o.kind)) return false;
    const Prop* pr = o.find(key);
    if (!pr || !trimmed(pr->expr).empty() || !linksVariable(pr->value)) return false;
    if (key == "fault" || key == "moving") return true;
    if (key != "value" || !trimmed(o.text("variable")).empty()) return false;   // sa variable passe avant
    // Un chemin de variable : un nom, des membres, des indices.
    const std::string t = trimmed(markers::strip(pr->value, markers::Mode::Expression));
    if (t.empty() || !(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_')) return false;
    for (const char c : t)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '[' || c == ']' || c == ',' || c == ' '))
            return false;
    return t.find('#') == std::string::npos;
}

bool formulaValueKey(const Object& o, std::string_view key) {
    if (key == "variable" || key == "state" || key == "lamp") return false;   // leur valeur : "value" (autoValueSource)
    const auto r = roles::roleOf(o.kind, key);
    if (r.role != roles::Role::Formula) return false;
    using R = roles::Role;
    if (r.item == R::Text || r.item == R::List || r.item == R::Name || r.item == R::Mini) return false;   // un chemin, une source
    const Prop* pr = o.find(key);
    return pr && trimmed(pr->expr).empty() && linksVariable(pr->value);
}

std::string linkedSource(const Object& o, std::string_view key) {
    const Prop* pr = o.find(key);
    if (pr && !trimmed(pr->expr).empty()) return isConstant(pr->expr) ? std::string{} : trimmed(pr->expr);
    // La valeur d'un objet du synoptique sans expression : sa variable.
    if (key == "value") {
        if (const std::string var = trimmed(o.text("variable")); !var.empty()) return var;
    }
    if (!pr || !linksVariable(pr->value)) return {};   // 1.12.5 : vrai, oui... sont des constantes
    return trimmed(pr->value);
}

namespace {
LibraryAlarm limitDiscordAlarm(const Object& o, const std::string& cap) {
    int delayMs = 30000;
    if (const Prop* tp = o.find("travelTime"); tp && trimmed(tp->expr).empty())
        if (const double sec = limits::travelSeconds(tp->value); sec > 0) delayMs = static_cast<int>(std::lround(sec * 1000.0));
    std::vector<std::string> parts;
    std::string why;
    const auto both = [](const std::string& a, const std::string& b) { return "(" + a + ") AND (" + b + ")"; };
    if (o.kind == Kind::ThreeWayValve) {
        const bool lBore = trimmed(o.text("valve3Bore", "T")) == "L";
        std::vector<std::pair<int, std::string>> ways;   // les voies reliees (0 : fermee) et leur fin de course
        for (int w = 0; w < 4; ++w)
            if (!(lBore && w == 1))
                if (std::string src = linkedSource(o, limits::kWayKeys[w]); !src.empty()) ways.emplace_back(w, std::move(src));
        for (std::size_t i = 0; i < ways.size(); ++i)
            for (std::size_t j = i + 1; j < ways.size(); ++j) parts.push_back(both(ways[i].second, ways[j].second));
        const std::string order = linkedSource(o, "positionOrder");
        static const char* const kCodes[4] = {"0", "12", "13", "23"};
        if (!order.empty()) {
            for (const auto& [w, src] : ways)
                parts.push_back("((" + order + ") = " + std::to_string(w) + (w ? " OR (" + order + ") = " + kCodes[w] : std::string{}) + ") AND NOT ("
                                + src + ")");
        } else if (ways.size() >= 2) {
            std::string none;
            for (const auto& [w, src] : ways) none += (none.empty() ? "NOT (" : " AND NOT (") + src + ")";
            parts.push_back(none);
        }
        if (parts.empty())
            why = ways.empty() ? "aucune fin de course de voie (limit12, limit13, limit23, limitClosed) n'est reli\xC3\xA9" "e"
                               : "une seule fin de course reli\xC3\xA9" "e, sans Ordre de position (positionOrder) : rien \xC3\xA0 comparer";
    } else {
        const std::string fco = linkedSource(o, "limitOpen"), fcf = linkedSource(o, "limitClosed"), ord = linkedSource(o, "openOrder");
        if (!fco.empty() && !fcf.empty()) parts.push_back(both(fco, fcf));
        if (!ord.empty()) {
            if (!fco.empty()) parts.push_back("(" + ord + ") AND NOT (" + fco + ")");
            if (!fcf.empty()) parts.push_back("NOT (" + ord + ") AND NOT (" + fcf + ")");
        } else if (!fco.empty() && !fcf.empty()) {
            parts.push_back("NOT (" + fco + ") AND NOT (" + fcf + ")");
        }
        if (parts.empty())
            why = fco.empty() && fcf.empty() ? "les fins de course (limitOpen, limitClosed) ne sont pas reli\xC3\xA9" "es"
                                             : "une seule fin de course reli\xC3\xA9" "e, sans Ordre d'ouverture (openOrder) : rien \xC3\xA0 comparer";
    }
    std::string condition;
    for (const auto& part : parts) condition += (condition.empty() ? "(" : " OR (") + part + ")";
    auto la = make("Discordance", condition, cap + " : discordance des fins de course", 2, "Alarme", delayMs);
    return parts.empty() ? unusable(std::move(la), why) : la;
}
} // namespace

// ================================================================ bibliotheque ==
std::vector<LibraryAlarm> libraryAlarms(const Object& o) {
    std::vector<LibraryAlarm> out;
    if (!kindIsSynoptic(o.kind)) return out;
    const std::string cap = objectCaption(o);
    // Defaut : tout objet du synoptique dont "fault" est relie.
    {
        const std::string fault = linkedSource(o, "fault");
        auto la = make("Defaut", fault, cap + " : d\xC3\xA9" "faut", 2, defautCategory());
        out.push_back(fault.empty() ? unusable(std::move(la), "le d\xC3\xA9" "faut (fault) n'est pas reli\xC3\xA9") : std::move(la));
    }
    // Les niveaux : cuve, bouteille de gaz, silo, tremie (pas le verin).
    if (kindHasLevel(o.kind) && o.kind != Kind::Cylinder) {
        const std::string value = linkedSource(o, "value");
        const bool gas = o.kind == Kind::GasBottle;
        struct Level { const char* name; const char* key; const char* op; int priority; const char* category; std::string message; const char* threshold; };
        const std::string bottle = "Bouteille " + cap;
        const Level levels[] = {
            {"Niveau_Tres_Bas", "lowAlarm", "<=", 2, "Alarme",
             gas ? bottle + " presque vide" : cap + " : niveau tr\xC3\xA8s bas", "le seuil tr\xC3\xA8s bas (lowAlarm)"},
            {"Niveau_Bas", "low", "<=", 3, "Avertissement",
             gas ? bottle + " : pression basse" : cap + " : niveau bas", "le seuil bas (low)"},
            {"Niveau_Haut", "high", ">=", 3, "Avertissement",
             gas ? bottle + " : pression haute" : cap + " : niveau haut", "le seuil haut (high)"},
            {"Niveau_Tres_Haut", "highAlarm", ">=", 2, "Alarme",
             gas ? bottle + " : pression tr\xC3\xA8s haute" : cap + " : niveau tr\xC3\xA8s haut", "le seuil tr\xC3\xA8s haut (highAlarm)"},
        };
        for (const auto& l : levels) {
            const Prop* tp = o.find(l.key);
            std::string threshold = tp ? trimmed(!trimmed(tp->expr).empty() ? tp->expr : tp->value) : std::string{};
            auto la = make(l.name, std::string{}, l.message, l.priority, l.category);
            if (value.empty()) {
                out.push_back(unusable(std::move(la), gas ? "la pression (value) n'est pas reli\xC3\xA9" "e"
                                                          : "le niveau (value) n'est pas reli\xC3\xA9"));
                continue;
            }
            if (threshold.empty()) {
                out.push_back(unusable(std::move(la), std::string(l.threshold) + " est vide"));
                continue;
            }
            la.def.condition = "(" + value + ") " + l.op + " " + threshold;
            out.push_back(std::move(la));
        }
    }
    // La vanne dont "moving" est relie : elle ne finit pas sa course en 30 s.
    // 1.10.4 : la vanne 3 voies aussi (son boisseau tourne d'une voie a l'autre).
    if (o.kind == Kind::Valve || o.kind == Kind::ThreeWayValve || kindIsDamper(o.kind)) {   // 1.12.24 : et les registres
        const std::string moving = linkedSource(o, "moving");
        auto la = make("Course_Trop_Longue", moving, cap + " : ne finit pas sa course", 3, "Alarme", 30000);
        out.push_back(moving.empty() ? unusable(std::move(la), "le mouvement (moving) n'est pas reli\xC3\xA9") : std::move(la));
    }
    // 1.14.1 : COMMANDE PAR LES FINS DE COURSE (la vanne, la vanne 3 voies, les registres) - la discordance : deux
    // fins de course vraies a la fois, l'Ordre donne et sa fin de course pas atteinte, ou (sans Ordre) aucune ;
    // son delai : le Temps de manoeuvre (30 s sans lui).
    if (limits::kindHasLimits(o.kind) && limits::limitMode(o)) {
        out.push_back(limitDiscordAlarm(o, cap));
    } else
    // 1.12.24 : le registre dont les deux fins de course sont reliees - ni l'une ni l'autre hors mouvement (il
    // est reste en route), ou les deux a la fois (un capteur colle) ; le clapet coupe-feu declenche.
    if (kindIsDamper(o.kind)) {
        const std::string fco = linkedSource(o, "limitOpen"), fcf = linkedSource(o, "limitClosed");
        const std::string moving = linkedSource(o, "moving");
        const std::string still = moving.empty() ? std::string{} : " AND NOT (" + moving + ")";
        auto la = make("Discordance", "((" + fco + ") AND (" + fcf + ")) OR (NOT (" + fco + ") AND NOT (" + fcf + ")" + still + ")",
                       cap + " : discordance des fins de course", 2, "Alarme", 10000);
        out.push_back(fco.empty() || fcf.empty() ? unusable(std::move(la), "les deux fins de course (limitOpen, limitClosed) ne sont pas reli\xC3\xA9" "es")
                                                 : std::move(la));
    }
    if (o.kind == Kind::FireDamper) {
        const std::string tripped = linkedSource(o, "tripped");
        auto la = make("Declenche", tripped, cap + " : clapet coupe-feu d\xC3\xA9" "clench\xC3\xA9", 1, "Alarme", 0);
        out.push_back(tripped.empty() ? unusable(std::move(la), "le d\xC3\xA9" "clenchement (tripped) n'est pas reli\xC3\xA9") : std::move(la));
    }
    return out;
}

// ================================================================ surcharges ====
const std::vector<std::string>& overrideFields() {
    static const std::vector<std::string> f = {"active",    "condition", "message", "priorite", "categorie",
                                               "groupe",    "delai",     "acquittement", "consigne", "description"};
    return f;
}

std::vector<std::string> overriddenFields(const AlarmOverride& ov) {
    std::vector<std::string> out;
    const bool set[] = {ov.active.has_value(),   ov.condition.has_value(), ov.message.has_value(), ov.priority.has_value(),
                        ov.category.has_value(), ov.group.has_value(),     ov.delayMs.has_value(), ov.ackRequired.has_value(),
                        ov.instruction.has_value(), ov.description.has_value()};
    for (std::size_t i = 0; i < overrideFields().size(); ++i)
        if (set[i]) out.push_back(overrideFields()[i]);
    return out;
}

const AlarmOverride* findOverride(const Object& o, std::string_view path, std::string_view alarm) noexcept {
    for (const auto& ov : o.alarmOverrides)
        if (ov.alarm == alarm && ov.path == path) return &ov;
    return nullptr;
}

AlarmOverride& overrideOf(Object& o, std::string_view path, std::string_view alarm) {
    for (auto& ov : o.alarmOverrides)
        if (ov.alarm == alarm && ov.path == path) return ov;
    AlarmOverride ov;
    ov.alarm = std::string(alarm);
    ov.path = std::string(path);
    o.alarmOverrides.push_back(std::move(ov));
    return o.alarmOverrides.back();
}

bool resetOverride(Object& o, std::string_view path, std::string_view alarm, std::string_view field) {
    auto it = std::find_if(o.alarmOverrides.begin(), o.alarmOverrides.end(),
                           [&](const AlarmOverride& ov) { return ov.alarm == alarm && ov.path == path; });
    if (it == o.alarmOverrides.end()) return false;
    if (field.empty()) {
        o.alarmOverrides.erase(it);
        return true;
    }
    AlarmOverride& ov = *it;
    bool changed = false;
    const auto clear = [&](auto& opt) { changed = opt.has_value(); opt.reset(); };
    if (field == "active") clear(ov.active);
    else if (field == "condition") clear(ov.condition);
    else if (field == "message") clear(ov.message);
    else if (field == "priorite") clear(ov.priority);
    else if (field == "categorie") clear(ov.category);
    else if (field == "groupe") clear(ov.group);
    else if (field == "delai") clear(ov.delayMs);
    else if (field == "acquittement") clear(ov.ackRequired);
    else if (field == "consigne") clear(ov.instruction);
    else if (field == "description") clear(ov.description);
    if (ov.empty()) o.alarmOverrides.erase(it);
    return changed;
}

AlarmDef applyOverride(const AlarmDef& base, const AlarmOverride* ov, bool* active) {
    AlarmDef d = base;
    if (!ov) return d;
    if (active && ov->active) *active = *ov->active;
    if (ov->condition) d.condition = *ov->condition;
    if (ov->message) d.message = *ov->message;
    if (ov->priority) d.priority = std::clamp(*ov->priority, 1, kAlarmPriorities);
    // 1.11 (R111, REP-1) : les $ d'un repere sont transparents ($Zone$ : le groupe Zone).
    if (ov->category) d.category = markers::strip(*ov->category, markers::Mode::Text);
    if (ov->group) d.group = markers::strip(*ov->group, markers::Mode::Text);
    if (ov->delayMs) d.delayMs = std::max(0, *ov->delayMs);
    if (ov->ackRequired) d.ackRequired = *ov->ackRequired;
    if (ov->instruction) d.instruction = markers::strip(*ov->instruction, markers::Mode::Text);
    if (ov->description) d.description = markers::strip(*ov->description, markers::Mode::Text);
    return d;
}

// ================================================================ generation ====
std::string objectGroupOf(const View& v, const Object& o) {
    return v.name + "." + (o.name.empty() ? "#" + std::to_string(o.id) : o.name);
}

bool viewGeneratesAlarms(const View& v) noexcept {
    return !isSymbolView(v) && v.params.empty();
}

namespace {

// Une alarme de symbole developpee avec les arguments de l'instance.
AlarmDef developed(const AlarmDef& a, const SymbolArguments& args) {
    AlarmDef d = a;
    if (args.empty()) return d;
    d.condition = substituteParams(a.condition, args);
    d.message = resolveLiteralHoles(substituteInTemplate(a.message, args));
    d.instruction = resolveLiteralHoles(substituteInTemplate(a.instruction, args));
    d.description = resolveLiteralHoles(substituteInTemplate(a.description, args));
    d.group = resolveLiteralHoles(substituteInTemplate(a.group, args));
    return d;
}

// Decision 11 h 55 : une surcharge s'ecrit dans les termes du symbole (Moteur.Courant
// > 50.0) ; developpee ici avec les memes arguments que la definition qu'elle remplace.
std::optional<AlarmOverride> developedOverride(const AlarmOverride* ov, const SymbolArguments* args) {
    if (!ov) return std::nullopt;
    AlarmOverride d = *ov;
    if (!args || args->empty()) return d;
    const auto text = [&](std::optional<std::string>& f) {
        if (f) f = resolveLiteralHoles(substituteInTemplate(*f, *args));
    };
    if (d.condition) d.condition = substituteParams(*d.condition, *args);
    text(d.message);
    text(d.instruction);
    text(d.description);
    text(d.group);
    return d;
}

// 1.12.3 : une popup de symbole s'ouvre pour une instance, avec ses arguments : une
// condition qui cite un parametre du symbole (Moteur.Defaut) ne se lit pas a part.
bool citesSymbolParams(const Project& p, const View& v, std::string_view condition) {
    if (v.ownerSymbol == kNoId || condition.empty()) return false;
    const View* sym = p.view(v.ownerSymbol);
    if (!sym || sym->params.empty()) return false;
    static constexpr std::string_view kProbe = "XPG_PARAMETRE_DU_SYMBOLE";
    SymbolArguments probe;
    for (const auto& prm : sym->params) probe.emplace_back(prm.name, std::string(kProbe));
    return substituteParams(condition, probe).find(kProbe) != std::string::npos;
}

struct Generator {
    const Project&            p;
    std::vector<ObjectAlarm>& out;

    // Une alarme : `base` deja developpee (et surchargee par le symbole) ; la
    // surcharge de l'objet pose `posed` (au chemin `path`), ecrite dans les termes
    // du symbole, est developpee avec `terms` puis s'applique dessus.
    void emit(const View& v, const Object& posed, const std::string& group, const std::string& path, const std::string& symbol,
              const std::string& symbols, ObjectAlarmSource source, Kind kind, const AlarmDef& base, bool baseActive,
              const SymbolArguments* terms = nullptr) {
        ObjectAlarm oa;
        oa.localName = base.name;
        oa.objectGroup = group;
        oa.view = v.name;
        oa.viewId = v.id;
        oa.objectId = posed.id;
        oa.objectName = posed.name;
        oa.path = path;
        oa.symbol = symbol;
        oa.symbols = symbols;
        oa.source = source;
        oa.kind = kind;
        oa.base = base;
        oa.base.id = kNoId;
        oa.base.name = group + "." + base.name;
        bool active = baseActive;
        const AlarmOverride* ov = findOverride(posed, path, base.name);
        const std::optional<AlarmOverride> dov = developedOverride(ov, terms);
        // 1.10.2 (AL) : le lien du groupe d'objets vers un groupe de IHM > Alarmes, entre
        // le symbole et la surcharge de l'objet (qui gagne) : l'alarme devient une alarme
        // de ce groupe, avec sa priorite par defaut et son acquittement automatique.
        AlarmDef linked = oa.base;
        if (const auto* link = alarmGroupLinkOf(p, group, symbols)) {
            const AlarmGroupDef gs = alarmGroupSettings(p, link->group);
            linked.group = link->group;
            if (gs.priority >= 1) linked.priority = std::clamp(gs.priority, 1, kAlarmPriorities);
            if (gs.ack == AlarmAckMode::Auto) linked.ackRequired = false;
        }
        oa.def = applyOverride(linked, dov ? &*dov : nullptr, &active);
        // 1.12.3 : une popup de symbole ne fabrique pas l'alarme d'un objet dont la condition
        // cite un parametre du symbole (avant : "condition illisible" au demarrage).
        if (citesSymbolParams(p, v, oa.def.condition)) return;
        oa.active = active;
        if (ov) oa.overridden = overriddenFields(*ov);
        out.push_back(std::move(oa));
    }

    // Les surcharges posees DANS le symbole (par son auteur), de l'interieur vers
    // l'exterieur : l'objet lui-meme (chemin ""), puis chaque instance imbriquee
    // qui le contient (le reste du chemin).
    AlarmDef layered(const AlarmDef& start, const Object& carrier, const std::vector<std::pair<const Object*, std::string>>& outer,
                     bool* active, const SymbolArguments* terms) {
        auto dov = developedOverride(findOverride(carrier, "", start.name), terms);
        AlarmDef d = applyOverride(start, dov ? &*dov : nullptr, active);
        for (const auto& [holder, rest] : outer) {
            dov = developedOverride(findOverride(*holder, rest, start.name), terms);
            d = applyOverride(d, dov ? &*dov : nullptr, active);
        }
        return d;
    }

    void posedSynoptic(const View& v, const Object& o) {
        const std::string group = objectGroupOf(v, o);
        // 1.12.11 (lot G) : la liaison Equipement - Defaut <- P1.Defaut, comme le moteur la remplit ;
        // 1.12.10 (lot F) : les seuils laisses vides sont ceux de la variable (Unites et formats), comme au dessin.
        Object ranged = eqlink::bound(p, &v, o);
        (void)ranges::apply(ranged, p, &v);
        for (const auto& la : libraryAlarms(ranged))
            if (la.applicable) emit(v, o, group, {}, {}, {}, ObjectAlarmSource::Library, o.kind, la.def, true);
    }

    void posedInstance(const View& v, const Object& inst) {
        const View* sym = symbolOf(p, inst);
        if (!sym) return;
        const std::string group = objectGroupOf(v, inst);
        const SymbolArguments args = symbolArguments(*sym, inst, &p);
        for (const auto& a : sym->alarms)
            emit(v, inst, group, {}, sym->name, sym->name, ObjectAlarmSource::Symbol, Kind::SymbolInstance, developed(a, args), true, &args);
        // Les objets du symbole, developpes comme le moteur les pose : noms
        // "Instance.Objet", "Instance.Sous.Objet", parametres remplaces.
        const Expansion e = expandInstance(p, inst);
        std::map<std::string, std::string, std::less<>> chain;        // "Instance.Sous" -> "Sym_A;Sym_B"
        std::map<std::string, const Object*, std::less<>> nested;     // les instances imbriquees developpees
        chain[inst.name] = sym->name;
        std::map<std::string, SymbolArguments, std::less<>> argsOf;   // "Instance.Sous" -> ses arguments developpes
        argsOf[inst.name] = args;
        const std::string prefix = inst.name + ".";
        for (const auto& c : e.objects) {
            if (c.name.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string rel = c.name.substr(prefix.size());
            const std::string parentKey = c.name.substr(0, c.name.rfind('.'));
            std::string symbols = chain.count(parentKey) ? chain[parentKey] : sym->name;
            // Les instances imbriquees qui contiennent c, de l'interieur vers l'exterieur.
            std::vector<std::pair<const Object*, std::string>> outer;
            for (std::string key = parentKey; key.size() > inst.name.size(); key = key.substr(0, key.rfind('.'))) {
                if (const auto it = nested.find(key); it != nested.end())
                    outer.emplace_back(it->second, c.name.substr(key.size() + 1));
                if (key.rfind('.') == std::string::npos) break;
            }
            const std::string cgroup = v.name + "." + c.name;
            // Les termes du symbole qui contient c (ses textes, et ceux de ses surcharges).
            const auto pa = argsOf.find(parentKey);
            const SymbolArguments* parentArgs = pa == argsOf.end() ? &args : &pa->second;
            if (c.kind == Kind::SymbolInstance) {
                const View* s2 = symbolOf(p, c);
                if (!s2) continue;
                nested[c.name] = &c;
                chain[c.name] = symbols + ";" + s2->name;
                const SymbolArguments& args2 = argsOf[c.name] = symbolArguments(*s2, c, &p);
                for (const auto& a : s2->alarms) {
                    bool active = true;
                    const AlarmDef base = layered(developed(a, args2), c, outer, &active, &args2);
                    emit(v, inst, cgroup, rel, s2->name, chain[c.name], ObjectAlarmSource::Symbol, Kind::SymbolInstance, base, active,
                         &args2);
                }
            } else if (kindIsSynoptic(c.kind)) {
                Object ranged = eqlink::bound(p, &v, c);             // 1.12.11 (lot G) : la liaison Equipement ;
                (void)ranges::apply(ranged, p, &v);                  // 1.12.10 (lot F) : les seuils de la variable
                for (const auto& la : libraryAlarms(ranged)) {
                    if (!la.applicable) continue;
                    bool active = true;
                    const AlarmDef base = layered(la.def, c, outer, &active, parentArgs);
                    emit(v, inst, cgroup, rel, {}, symbols, ObjectAlarmSource::Library, c.kind, base, active, parentArgs);
                }
            }
        }
    }

    void object(const View& v, const Object& o) {
        if (kindIsSynoptic(o.kind)) posedSynoptic(v, o);
        else if (o.kind == Kind::SymbolInstance) {
            // 1.12.7 (lot E) : une instance repetee - les alarmes de chaque copie, comme le moteur les pose
            // (Cuve, Cuve_2...). Une LIST n'a de taille qu'en marche : ses copies n'ont pas d'alarme generee.
            if (sources::repeats(o)) {
                View one;
                one.id = v.id;
                one.name = v.name;
                one.objects = {o};
                (void)sources::repeatInstances(one, sources::projectSources(p));
                for (const auto& c : one.objects) posedInstance(v, c);
                return;
            }
            posedInstance(v, o);
        }
    }
};

} // namespace

std::vector<ObjectAlarm> objectAlarms(const Project& p) {
    std::vector<ObjectAlarm> out;
    Generator g{p, out};
    for (const auto& v : p.views) {
        if (!viewGeneratesAlarms(v)) continue;
        for (const auto& o : v.objects) g.object(v, o);
    }
    return out;
}

std::vector<ObjectAlarm> objectAlarmsOf(const Project& p, const View& v, const Object& o) {
    std::vector<ObjectAlarm> out;
    Generator g{p, out};
    g.object(v, o);
    return out;
}

// ================================================================ renommages ====
namespace {

// Le chemin `path` avec le segment `from` remplace par `to` (tous les segments egaux).
std::string renamedPath(std::string_view path, std::string_view from, std::string_view to) {
    std::string out;
    for (std::size_t at = 0; at <= path.size();) {
        const std::size_t dot = std::min(path.find('.', at), path.size());
        const std::string_view seg = path.substr(at, dot - at);
        if (!out.empty()) out += '.';
        out += seg == from ? to : seg;
        at = dot + 1;
    }
    return out;
}

bool generatedHas(const std::vector<ObjectAlarm>& list, std::string_view path, std::string_view alarm, std::string_view symbol) {
    for (const auto& a : list)
        if (a.path == path && a.localName == alarm && (symbol.empty() || a.symbol == symbol)) return true;
    return false;
}

} // namespace

std::size_t renameSymbolAlarmOverrides(Project& p, std::string_view symbol, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || from == to) return 0;
    std::size_t n = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            if (o.alarmOverrides.empty() || o.kind != Kind::SymbolInstance) continue;
            std::vector<ObjectAlarm> list;
            bool listed = false;
            for (auto& ov : o.alarmOverrides) {
                if (ov.alarm != from) continue;
                if (!listed) { list = objectAlarmsOf(p, v, o); listed = true; }
                if (generatedHas(list, ov.path, to, symbol) && !generatedHas(list, ov.path, from, symbol)
                    && !findOverride(o, ov.path, to)) {
                    ov.alarm = std::string(to);
                    ++n;
                }
            }
        }
    return n;
}

std::size_t renameOverridePaths(Project& p, std::string_view symbol, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || from == to) return 0;
    std::size_t n = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            if (o.alarmOverrides.empty() || o.kind != Kind::SymbolInstance) continue;
            std::vector<ObjectAlarm> list;
            bool listed = false;
            for (auto& ov : o.alarmOverrides) {
                if (ov.path.empty()) continue;
                const std::string next = renamedPath(ov.path, from, to);
                if (next == ov.path) continue;
                if (!listed) { list = objectAlarmsOf(p, v, o); listed = true; }
                // Le nouveau chemin vise une alarme generee, l'ancien plus rien : il suit.
                if (generatedHas(list, next, ov.alarm, {}) && !generatedHas(list, ov.path, ov.alarm, {}) && !findOverride(o, next, ov.alarm)) {
                    ov.path = next;
                    ++n;
                }
            }
        }
    (void)symbol;
    return n;
}

// ================================================================ filtres =======
bool wildcardMatch(std::string_view text, std::string_view pattern) noexcept {
    std::size_t t = 0, pi = 0, star = std::string_view::npos, mark = 0;
    while (t < text.size()) {
        if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            mark = t;
        } else if (pi < pattern.size() && pattern[pi] == text[t]) {
            ++pi;
            ++t;
        } else if (star != std::string_view::npos) {
            pi = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

bool alarmGroupMatches(std::string_view declared, std::string_view objectGroup, std::string_view symbols,
                       std::string_view filter) noexcept {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    };
    filter = trim(filter);
    if (filter.empty() || filter == "*") return true;
    const auto oneOf = [&](std::string_view item) {
        if (item.empty()) return false;
        if (item.rfind("symbole:", 0) == 0) {
            const std::string_view name = trim(item.substr(8));
            for (std::size_t at = 0; at <= symbols.size();) {
                const std::size_t semi = std::min(symbols.find(';', at), symbols.size());
                if (symbols.substr(at, semi - at) == name && !name.empty()) return true;
                at = semi + 1;
            }
            return false;
        }
        if (item.find('*') != std::string_view::npos)
            return wildcardMatch(declared, item) || (!objectGroup.empty() && wildcardMatch(objectGroup, item));
        if (declared == item || objectGroup == item) return true;
        return objectGroup.size() > item.size() && objectGroup.compare(0, item.size(), item) == 0 && objectGroup[item.size()] == '.';
    };
    if (filter.find(';') == std::string_view::npos) return oneOf(filter);
    for (std::size_t at = 0; at <= filter.size();) {
        const std::size_t semi = std::min(filter.find(';', at), filter.size());
        if (oneOf(trim(filter.substr(at, semi - at)))) return true;
        at = semi + 1;
    }
    return false;
}

// ================================================================ aide ==========
std::vector<AlarmDef> suggestSymbolAlarms(const Project& p, const View& symbol, std::string_view param, std::string_view type) {
    std::vector<AlarmDef> out;
    std::set<std::string> taken;
    for (const auto& a : symbol.alarms) taken.insert(a.name);
    const auto unique = [&](std::string base) {
        std::string name = base;
        for (int n = 2; taken.count(name); ++n) name = base + "_" + std::to_string(n);
        taken.insert(name);
        return name;
    };
    const std::string prm(param);
    const auto boolAlarm = [&](const std::string& name, const std::string& path, const std::string& what) {
        AlarmDef a;
        a.name = unique(name);
        a.condition = path;
        a.message = prm + " : " + what;
        a.priority = 2;
        a.category = defautCategory();
        out.push_back(std::move(a));
    };
    const auto isNumeric = [](const std::string& t) {
        static const std::set<std::string> num = {"INT", "UINT", "DINT", "UDINT", "SINT", "USINT", "LINT", "ULINT",
                                                  "REAL", "LREAL", "WORD", "DWORD", "BYTE"};
        return num.count(t) > 0;
    };
    const std::string t = upper(trimmed(type));
    if (t == "BOOL") {
        boolAlarm(prm, prm, prm);
        return out;
    }
    const HmiType* ht = p.hmiTypeByName(trimmed(type));
    if (!ht) return out;
    for (const auto& m : ht->members) {
        const std::string mt = upper(trimmed(m.type));
        const std::string path = prm + "." + m.name;
        const std::string what = m.description.empty() ? m.name : m.description;
        if (mt == "BOOL") {
            boolAlarm(m.name, path, what);
        } else if (isNumeric(mt)) {
            AlarmDef lo;
            lo.name = unique(m.name + "_Bas");
            lo.condition = path + " <= 0";
            lo.message = prm + " : " + what + " bas ({" + path + "})";
            lo.priority = 3;
            lo.category = "Avertissement";
            lo.description = "Seuil \xC3\xA0 r\xC3\xA9gler.";
            out.push_back(std::move(lo));
            AlarmDef hi;
            hi.name = unique(m.name + "_Haut");
            hi.condition = path + " >= 100";
            hi.message = prm + " : " + what + " haut ({" + path + "})";
            hi.priority = 3;
            hi.category = "Avertissement";
            hi.description = "Seuil \xC3\xA0 r\xC3\xA9gler.";
            out.push_back(std::move(hi));
        }
    }
    return out;
}

} // namespace hmi
