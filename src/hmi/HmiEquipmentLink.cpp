// =============================================================================
//  hmi/HmiEquipmentLink.cpp - 1.12.11 (lot G) : la liaison Equipement et le
//                             mode Etat des symboles du synoptique.
//                             Voir HmiEquipmentLink.hpp.
// =============================================================================
#include "HmiEquipmentLink.hpp"

#include "HmiCheck.hpp"
#include "HmiChoiceLists.hpp"   // la position fixe d'une vanne 3 voies (1-2, 1-3, 2-3)
#include "HmiControls.hpp"      // stateIndexOf : la valeur, une plage, le nom d'une valeur
#include "HmiEnums.hpp"
#include "HmiMarkers.hpp"
#include "HmiObjectAlarms.hpp"  // synopticLinkedKey : une Valeur tapee sans =
#include "HmiPopupParams.hpp"   // expressionType, typeMembers : le type d'un chemin, ses membres
#include "HmiRoles.hpp"
#include "HmiTypeRegistry.hpp"
#include "HmiTypes.hpp"         // un membre tableau (ARRAY[...] OF ...)

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace hmi::eqlink {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string upperCopy(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

bool same(std::string_view a, std::string_view b) { return upperCopy(trimmed(a)) == upperCopy(trimmed(b)); }

// Un mot sans casse ni accent (Arr\xC3\xAAt -> arret, D\xC3\xA9" "faut -> defaut, anim\xC3\xA9 -> anime).
std::string plainWord(std::string_view s) {
    std::string out;
    const std::string t = trimmed(s);
    for (std::size_t i = 0; i < t.size(); ++i) {
        const auto c = static_cast<unsigned char>(t[i]);
        if (c == 0xC3 && i + 1 < t.size()) {
            const auto n = static_cast<unsigned char>(t[i + 1]);
            ++i;
            if ((n >= 0xA8 && n <= 0xAB) || (n >= 0x88 && n <= 0x8B)) out += 'e';        // e accentue
            else if ((n >= 0xA0 && n <= 0xA5) || (n >= 0x80 && n <= 0x85)) out += 'a';
            else if (n == 0xA7 || n == 0x87) out += 'c';
            else if ((n >= 0xAC && n <= 0xAF) || (n >= 0x8C && n <= 0x8F)) out += 'i';
            else if ((n >= 0xB2 && n <= 0xB6) || (n >= 0x92 && n <= 0x96)) out += 'o';
            else if ((n >= 0xB9 && n <= 0xBC) || (n >= 0x99 && n <= 0x9C)) out += 'u';
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

// Les DDT de l'automate (XPGAnalyser API), donnes par l'application ; nuls : les types IHM seuls.
std::mutex gPlcLock;
std::shared_ptr<const params::PlcTypes> gPlc;

params::PlcTypes plcTypes() {
    const std::lock_guard<std::mutex> lock(gPlcLock);
    return gPlc ? *gPlc : params::PlcTypes{};
}

bool integerType(std::string_view type) {
    const sim::Type t = typereg::simTypeOf(type);
    return sim::isInteger(t) && t != sim::Type::Bool && t != sim::Type::Time;
}

} // namespace

void setPlcTypes(std::shared_ptr<const params::PlcTypes> plc) {
    const std::lock_guard<std::mutex> lock(gPlcLock);
    gPlc = std::move(plc);
}

// ================================================================ roles =====
const std::vector<RoleInfo>& allRoles() {
    static const std::vector<RoleInfo> all = {
        {Role::Running, "marche", "Valeur (marche)", "value", {"Marche", "EnMarche", "Running", "Etat"}},
        {Role::Measure, "mesure", "Mesure / Niveau", "value", {"Mesure", "Niveau", "Valeur", "Pv"}},
        {Role::Position, "position", "Position", "value", {"Position", "Voie", "Etat", ""}},
        {Role::Fault, "defaut", "D\xC3\xA9" "faut", "fault", {"Defaut", "Fault", "Alarme", ""}},
        {Role::Opening, "ouverture", "Ouverture", "opening", {"Ouverture", "Position", "", ""}},
        {Role::Moving, "mouvement", "En mouvement", "moving", {"EnMouvement", "Mouvement", "Moving", ""}},
        {Role::Mode, "mode", "Mode", "operatingMode", {"Mode", "", "", ""}},
        {Role::Label, "libelle", "Libell\xC3\xA9", "label", {"Nom", "Repere", "Tag", ""}},
    };
    return all;
}

const RoleInfo* roleInfo(std::string_view key) noexcept {
    for (const auto& r : allRoles())
        if (r.key == key) return &r;
    return nullptr;
}

bool kindHasAspects(Kind k) noexcept {
    return kindIsSynoptic(k) && !kindHasLevel(k) && k != Kind::IsaInstrument && k != Kind::ThreeWayValve;
}

std::vector<const RoleInfo*> rolesOf(Kind k) {
    std::vector<const RoleInfo*> out;
    if (!kindIsSynoptic(k)) return out;
    const auto add = [&](Role r) {
        for (const auto& info : allRoles())
            if (info.role == r) out.push_back(&info);
    };
    add(kindHasLevel(k) || k == Kind::IsaInstrument ? Role::Measure : k == Kind::ThreeWayValve ? Role::Position : Role::Running);
    add(Role::Fault);
    if (k == Kind::Valve) add(Role::Opening);
    if (k == Kind::Valve || k == Kind::ThreeWayValve) add(Role::Moving);
    // Le mode (Auto, Manu) : ce qui se pilote - pas un tube, une fleche, un contenant, un appareil electrique.
    if (k == Kind::Pump || k == Kind::Motor || k == Kind::Fan || k == Kind::Compressor || k == Kind::HeatExchanger || k == Kind::Boiler
        || k == Kind::Mixer || k == Kind::Conveyor || k == Kind::Valve || k == Kind::ThreeWayValve || k == Kind::Cylinder)
        add(Role::Mode);
    add(Role::Label);
    return out;
}

// ======================================================= types et membres ===
std::vector<Member> membersOf(const Project& p, std::string_view type) {
    std::vector<Member> out;
    if (trimmed(type).empty()) return out;
    for (const auto& [name, mt] : params::typeMembers(p, type, plcTypes())) out.push_back({name, mt});
    return out;
}

std::string structTypeOf(const Project& p, const View* view, std::string_view path) {
    const std::string bare = trimmed(markers::strip(path, markers::Mode::Expression));
    if (bare.empty()) return {};
    const std::string type = params::expressionType(p, view, bare, plcTypes());
    if (type.empty() || membersOf(p, type).empty()) return {};
    return trimmed(type);
}

// ================================================ le tableau, par type =====
std::string memberFor(const Project& p, std::string_view type, const RoleInfo& role, const std::vector<Member>& members, From* from) {
    if (from) *from = From::None;
    for (const auto& l : p.equipmentLinks)
        if (same(l.type, type) && l.role == role.key) {
            if (from) *from = From::Chosen;
            // Un membre choisi qui n'existe plus (retire du type) : aucun ; Generer le dit.
            for (const auto& m : members)
                if (same(m.name, l.member)) return m.name;
            return {};
        }
    for (const auto name : role.names) {
        if (name.empty()) continue;
        for (const auto& m : members)
            if (same(m.name, name)) {
                if (from) *from = From::Name;
                return m.name;
            }
    }
    return {};
}

bool choose(Project& p, std::string_view type, std::string_view role, const std::optional<std::string>& member) {
    auto it = std::find_if(p.equipmentLinks.begin(), p.equipmentLinks.end(),
                           [&](const EquipmentLink& l) { return same(l.type, type) && l.role == role; });
    if (!member) {
        if (it == p.equipmentLinks.end()) return false;
        p.equipmentLinks.erase(it);
        return true;
    }
    const std::string m = trimmed(*member);
    if (it != p.equipmentLinks.end()) {
        if (it->member == m) return false;
        it->member = m;
        return true;
    }
    p.equipmentLinks.push_back({trimmed(type), std::string(role), m});
    return true;
}

std::size_t renameType(Project& p, std::string_view from, std::string_view to) {
    std::size_t n = 0;
    for (auto& l : p.equipmentLinks)
        if (same(l.type, from)) {
            l.type = std::string(to);
            ++n;
        }
    return n;
}

std::size_t followMembers(Project& p, const HmiType& before, const HmiType& after) {
    std::vector<std::string> gone, born;
    for (const auto& m : before.members)
        if (std::none_of(after.members.begin(), after.members.end(), [&](const TypeMember& x) { return same(x.name, m.name); }))
            gone.push_back(m.name);
    for (const auto& m : after.members)
        if (std::none_of(before.members.begin(), before.members.end(), [&](const TypeMember& x) { return same(x.name, m.name); }))
            born.push_back(m.name);
    if (gone.size() != 1 || born.size() != 1) return 0;   // un ajout, un retrait, un deplacement : rien a suivre
    std::size_t n = 0;
    for (auto& l : p.equipmentLinks)
        if ((same(l.type, after.name) || same(l.type, before.name)) && same(l.member, gone.front())) {
            l.member = born.front();
            ++n;
        }
    return n;
}

// ================================================================ l'objet ===
std::string equipmentPath(const Object& o) { return formulaOf(o, kEquipmentKey); }

bool setByHand(const Object& o, std::string_view key) {
    const Prop* pr = o.find(key);
    if (!pr) return false;
    if (!trimmed(pr->expr).empty()) return true;
    if (key == "value" && !trimmed(o.text("variable")).empty()) return true;
    // Les valeurs d'un objet neuf de ce genre (une par genre, faites une fois).
    static std::mutex lock;
    static std::map<Kind, Object> fresh;
    std::string def;
    {
        const std::lock_guard<std::mutex> guard(lock);
        auto it = fresh.find(o.kind);
        if (it == fresh.end()) it = fresh.emplace(o.kind, makeObject(o.kind, kNoId, {}, 0, 0, kNoId)).first;
        def = it->second.text(key);
    }
    return trimmed(pr->value) != trimmed(def);
}

Binding bindingOf(const Project& p, const View* view, const Object& o) {
    Binding b;
    if (!kindIsSynoptic(o.kind)) return b;
    b.path = equipmentPath(o);
    if (b.path.empty()) return b;
    b.type = structTypeOf(p, view, b.path);
    b.members = membersOf(p, b.type);
    const std::string bare = trimmed(markers::strip(b.path, markers::Mode::Expression));
    for (const RoleInfo* r : rolesOf(o.kind)) {
        Row row;
        row.role = r;
        if (!b.type.empty()) row.member = memberFor(p, b.type, *r, b.members, &row.from);
        row.byHand = setByHand(o, r->property);
        if (!row.member.empty())
            row.source = r->role == Role::Label || r->role == Role::Mode ? "{" + bare + "." + row.member + "}" : bare + "." + row.member;
        b.rows.push_back(std::move(row));
    }
    // Le mode Etat d'office : la Valeur lit un entier (ETAT_MOTEUR) ou une enumeration, et l'objet n'a
    // pas le sien (un BOOL Marche reste vrai / faux).
    if (kindHasAspects(o.kind) && !hasAspects(o))
        for (const auto& row : b.rows)
            if (row.role->role == Role::Running && !row.member.empty() && !row.byHand) {
                std::string mt;
                for (const auto& m : b.members)
                    if (same(m.name, row.member)) mt = m.type;
                if (const HmiType* en = findEnumeration(p, mt)) b.aspects = formatAspects(aspectsFromEnum(*en));
                else if (integerType(mt)) b.aspects = formatAspects(motorStateAspects());
            }
    return b;
}

bool apply(const Project& p, const View* view, Object& o) {
    const Binding b = bindingOf(p, view, o);
    if (b.type.empty()) return false;
    bool changed = false;
    for (const auto& row : b.rows) {
        if (row.member.empty() || row.byHand) continue;
        const std::string key(row.role->property);
        if (row.role->role == Role::Label || row.role->role == Role::Mode) o.set(key, row.source);   // un texte a trous
        else o.setExpr(key, row.source);
        changed = true;
    }
    if (!b.aspects.empty()) {
        o.set(std::string(kAspectsKey), b.aspects);
        changed = true;
    }
    return changed;
}

Object bound(const Project& p, const View* view, const Object& o) {
    Object copy = o;
    (void)apply(p, view, copy);
    return copy;
}

bool viewHasLinks(const View& v) noexcept {
    for (const auto& o : v.objects)
        if (kindIsSynoptic(o.kind))
            if (const Prop* pr = o.find(kEquipmentKey); pr && (!pr->value.empty() || !pr->expr.empty())) return true;
    return false;
}

bool applyView(const Project& p, View& v) {
    if (!viewHasLinks(v)) return false;
    bool changed = false;
    for (auto& o : v.objects)
        if (kindIsSynoptic(o.kind) && !equipmentPath(o).empty()) changed = apply(p, &v, o) || changed;
    return changed;
}

// =============================================================== mode Etat ==
std::string_view lookKey(Look l) noexcept {
    switch (l) {
        case Look::On: return "marche";
        case Look::Fault: return "defaut";
        case Look::Maintenance: return "maintenance";
        case Look::Off: break;
    }
    return "arret";
}

std::string_view lookLabel(Look l) noexcept {
    switch (l) {
        case Look::On: return "Marche";
        case Look::Fault: return "D\xC3\xA9" "faut";
        case Look::Maintenance: return "Maintenance";
        case Look::Off: break;
    }
    return "Arr\xC3\xAAt";
}

std::optional<Look> lookFrom(std::string_view text) noexcept {
    const std::string w = plainWord(text);
    for (const Look l : {Look::Off, Look::On, Look::Fault, Look::Maintenance})
        if (w == lookKey(l) || w == plainWord(lookLabel(l))) return l;
    return std::nullopt;
}

std::vector<Aspect> parseAspects(std::string_view text, std::string* error) {
    std::vector<Aspect> out;
    for (const auto& entry : roles::splitList(markers::strip(text, markers::Mode::Text))) {
        if (entry.empty()) continue;
        const auto eq = entry.find('=');
        if (eq == std::string::npos) {
            if (error && error->empty()) *error = "\xC3\xA9tat sans \xC2\xAB = \xC2\xBB : " + entry;
            continue;
        }
        Aspect a;
        a.match = trimmed(std::string_view(entry).substr(0, eq));
        std::vector<std::string> parts;
        std::string rest = entry.substr(eq + 1);
        for (std::size_t from = 0;;) {
            const auto bar = rest.find('|', from);
            parts.push_back(trimmed(std::string_view(rest).substr(from, bar == std::string::npos ? std::string::npos : bar - from)));
            if (bar == std::string::npos) break;
            from = bar + 1;
        }
        const auto look = parts.empty() ? std::nullopt : lookFrom(parts.front());
        if (a.match.empty() || !look) {
            if (error && error->empty())
                *error = "\xC3\xA9tat " + entry + " : un aspect est attendu (arr\xC3\xAAt, marche, d\xC3\xA9" "faut, maintenance)";
            continue;
        }
        a.look = *look;
        for (std::size_t k = 1; k < parts.size(); ++k) {
            const std::string w = plainWord(parts[k]);
            if (w.empty()) continue;
            if (w == "anime" || w == "animee") a.animated = true;
            else if (w == "fixe") a.animated = false;
            else if (a.color.empty()) a.color = parts[k];
            else if (error && error->empty()) *error = "\xC3\xA9tat " + a.match + " : \xC2\xAB " + parts[k] + " \xC2\xBB inconnu (une couleur, ou anim\xC3\xA9)";
        }
        out.push_back(std::move(a));
    }
    return out;
}

std::string formatAspects(const std::vector<Aspect>& aspects) {
    std::string out;
    for (const auto& a : aspects) {
        if (!out.empty()) out += "; ";
        out += trimmed(a.match) + " = " + std::string(lookKey(a.look));
        if (!trimmed(a.color).empty()) out += " | " + trimmed(a.color);
        if (a.animated) out += " | anime";
    }
    return out;
}

Aspect aspectOf(const std::vector<Aspect>& aspects, std::string_view value, std::string_view name) {
    std::vector<StateEntry> states;
    states.reserve(aspects.size());
    for (const auto& a : aspects) states.push_back({a.match, std::string(lookKey(a.look)), a.color, a.animated});
    const int k = stateIndexOf(states, value, name);
    return k >= 0 ? aspects[static_cast<std::size_t>(k)] : Aspect{};
}

std::vector<Aspect> motorStateAspects() {
    return {{"0", Look::Off, "", false}, {"1", Look::On, "", true}, {"2", Look::Fault, "", false}, {"3", Look::Maintenance, "", false}};
}

Look lookOfName(std::string_view valueName) noexcept {
    std::string u;
    for (const char c : plainWord(valueName))
        if (std::isalnum(static_cast<unsigned char>(c))) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto has = [&](std::string_view w) { return u.find(w) != std::string::npos; };
    if (has("DEFAUT") || has("FAULT") || has("ALARM")) return Look::Fault;
    if (has("MAINT")) return Look::Maintenance;
    if (has("ARRET") || u.rfind("STOP", 0) == 0) return Look::Off;
    if (has("MARCHE") || u.rfind("RUN", 0) == 0 || u.rfind("AUTO", 0) == 0) return Look::On;
    return Look::Off;
}

std::vector<Aspect> aspectsFromEnum(const HmiType& en) {
    std::vector<Aspect> out;
    for (const auto& v : en.values) {
        const Look l = lookOfName(v.name);
        out.push_back({v.name, l, "", l == Look::On});
    }
    return out;
}

bool cylinderOnOff(const Object& o) noexcept {
    return o.kind == Kind::Cylinder && o.text(kCylinderModeKey).rfind("rentr", 0) == 0;
}

bool hasAspects(const Object& o) noexcept {
    if (!kindHasAspects(o.kind)) return false;
    const Prop* pr = o.find(kAspectsKey);
    return pr && pr->value.find_first_not_of(" \t") != std::string::npos;
}

// ================================================================ Generer ===
void check(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto add = [&](S s, const View& v, const Object& o, std::string key, std::string message) {
        Issue i;
        i.severity = s;
        i.category = "Objet";
        i.view = v.id;
        i.object = o.id;
        i.property = std::move(key);
        i.message = std::move(message);
        out.push_back(std::move(i));
    };
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            if (!kindIsSynoptic(o.kind)) continue;
            // ---- la liaison Equipement
            if (const std::string path = equipmentPath(o); !path.empty()) {
                const Binding b = bindingOf(p, &v, o);
                const std::string bare = trimmed(markers::strip(path, markers::Mode::Expression));
                const std::string anyType = params::expressionType(p, &v, bare, plcTypes());
                const std::size_t r = std::find_if(bare.begin(), bare.end(), [](char c) { return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_'); })
                                     - bare.begin();
                const ViewParam* prm = v.param(bare.substr(0, r));
                if (b.type.empty()) {
                    if (prm && trimmed(prm->type).empty())
                        add(S::Warning, v, o, std::string(kEquipmentKey),
                            "\xC3\xA9quipement " + path + " : le param\xC3\xA8tre " + prm->name
                                + " n'a pas de type - donne-lui son type de structure (T_Pompe) pour que ses membres se lient");
                    else if (!anyType.empty())
                        add(S::Warning, v, o, std::string(kEquipmentKey),
                            "\xC3\xA9quipement " + path + " (" + anyType + ") n'est pas une structure : la liaison ne lie rien - "
                                "donne une structure (P1, Pompes[2]), ou relie la Valeur seule");
                    else
                        add(S::Warning, v, o, std::string(kEquipmentKey),
                            "\xC3\xA9quipement " + path + " : chemin inconnu ou de type inconnu - la liaison ne lie rien en marche");
                } else {
                    std::size_t linked = 0;
                    for (const auto& row : b.rows) {
                        if (!row.member.empty()) ++linked;
                        // Le type du membre lie contre son role : un booleen (marche, defaut, mouvement), un nombre
                        // (mesure, ouverture, position) ; une structure ou un tableau ne se lit pas.
                        if (!row.member.empty() && !row.byHand) {
                            std::string mt;
                            for (const auto& m : b.members)
                                if (same(m.name, row.member)) mt = trimmed(m.type);
                            const sim::Type st = typereg::simTypeOf(mt);
                            types::Spec spec;
                            const bool date = st == sim::Type::Date || st == sim::Type::Tod || st == sim::Type::Dt;
                            const std::string where = "\xC3\xA9quipement " + path + " : " + std::string(row.role->label) + " <- " + row.member + " (" + mt + ")";
                            if (!membersOf(p, mt).empty() || (types::parseSpec(mt, spec) && spec.array()))
                                add(S::Warning, v, o, std::string(kEquipmentKey), where + " est un agr\xC3\xA9gat : choisis un membre simple (Liaison \xC3\x89quipement)");
                            else if ((row.role->role == Role::Running || row.role->role == Role::Fault || row.role->role == Role::Moving)
                                     && (st == sim::Type::String || st == sim::Type::Time || date))
                                add(S::Warning, v, o, std::string(kEquipmentKey), where + " se lit vrai / faux : un BOOL, un entier ou une \xC3\xA9num\xC3\xA9ration est attendu");
                            else if ((row.role->role == Role::Measure || row.role->role == Role::Opening || row.role->role == Role::Position)
                                     && (st == sim::Type::String || date))
                                add(S::Warning, v, o, std::string(kEquipmentKey), where + " : un nombre est attendu (une STRING s'y montrerait ###)");
                        }
                        if (row.from == From::Chosen && row.member.empty()) {
                            // choisi, mais le membre n'existe plus (ou « aucun ») : seulement s'il a disparu
                            for (const auto& l : p.equipmentLinks)
                                if (same(l.type, b.type) && l.role == row.role->key && !trimmed(l.member).empty())
                                    add(S::Warning, v, o, std::string(kEquipmentKey),
                                        "\xC3\xA9quipement " + path + " : le membre " + l.member + " choisi pour " + std::string(row.role->label)
                                            + " n'est plus dans " + b.type + " (Liaison \xC3\x89quipement : choisis-en un autre)");
                        }
                    }
                    if (linked == 0)
                        add(S::Warning, v, o, std::string(kEquipmentKey),
                            "\xC3\xA9quipement " + path + " (" + b.type + ") : aucun membre ne se lie par son nom (Marche, Defaut, Nom...) "
                                "- choisis-les dans la Liaison \xC3\x89quipement de l'inspecteur");
                }
            }
            // ---- le mode Etat : chaque ligne se lit, ses couleurs sont des couleurs
            if (hasAspects(o)) {
                std::string why;
                const auto aspects = parseAspects(o.text(kAspectsKey), &why);
                if (!why.empty()) add(S::Warning, v, o, std::string(kAspectsKey), "mode \xC3\x89tat : " + why);
                for (const auto& a : aspects)
                    if (!a.color.empty() && !roles::colorOk(a.color)) {
                        add(S::Warning, v, o, std::string(kAspectsKey),
                            "mode \xC3\x89tat, " + a.match + " : '" + a.color + "' n'est pas une couleur (#RRGGBB)");
                        break;
                    }
            }
            // ---- la vanne 3 voies : une position que son boisseau n'a pas (en L : 1-3 et 2-3 seulement)
            if (o.kind == Kind::ThreeWayValve && trimmed(o.text("valve3Bore", "T")) == "L"
                && o.text("positionMode", "entier").rfind("deux", 0) != 0)
                if (const Prop* pr = o.find("value"); pr && trimmed(pr->expr).empty() && choices::valve3Label(pr->value) == "1-2")
                    add(S::Warning, v, o, "value",
                        "position 1-2 impossible pour un boisseau en L (il relie 1-3 ou 2-3) : choisis 1-3, 2-3 ou ferm\xC3\xA9" "e, "
                        "ou un boisseau en T");
            // ---- la Valeur : une enumeration lue en booleen ; une structure entiere ; un BOOL sur un verin
            const Prop* val = o.find("value");
            if (!val) continue;
            std::string source = trimmed(val->expr);
            if (source.empty() && synopticLinkedKey(o, "value")) source = trimmed(val->value);
            if (source.empty()) continue;
            const std::string type = params::expressionType(p, &v, markers::strip(source), plcTypes());
            if (type.empty()) continue;
            if (o.kind == Kind::Cylinder && !cylinderOnOff(o) && upperCopy(trimmed(type)) == "BOOL")
                add(S::Warning, v, o, "value",
                    source + " est un BOOL : en course (entre min et max), vrai sort le v\xC3\xA9rin de 1 % - choisis Course : "
                             "rentr\xC3\xA9 / sorti");
            if (const HmiType* en = findEnumeration(p, type); en && kindHasAspects(o.kind) && !hasAspects(o))
                add(S::Warning, v, o, "value",
                    source + " est une \xC3\xA9num\xC3\xA9ration (" + en->name + ") lue vrai / faux : toute valeur sauf la premi\xC3\xA8re "
                        "se dessine en marche (un d\xC3\xA9" "faut aussi) - passe en mode \xC3\x89tat (Contenu \xE2\x80\xBA Depuis une \xC3\xA9num\xC3\xA9ration)");
            else if (!membersOf(p, type).empty())
                add(S::Warning, v, o, "value",
                    source + " est une structure (" + type + ") : donne-la \xC3\xA0 \xC3\x89quipement (" + source
                        + ") - ses membres se lient d'eux-m\xC3\xAAmes (Marche, Defaut, Nom...)");
        }
}

} // namespace hmi::eqlink
