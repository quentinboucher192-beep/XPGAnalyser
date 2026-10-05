// =============================================================================
//  hmi/HmiRuntimeObjectAlarms.cpp - le moteur des alarmes des objets (1.9)
// -----------------------------------------------------------------------------
//  Les alarmes generees (HmiObjectAlarms.hpp : objets du synoptique, instances
//  de symboles) recoivent un identifiant stable pendant la marche (par leur
//  nom), et passent par evaluateAlarms() exactement comme celles du projet :
//  apparition, delai, acquittement, mise de cote, historique, sons,
//  notifications, SYS.Alarm*. Elles sont refaites quand le projet change (au
//  plus une fois par seconde) ; une alarme dont l'objet a disparu (ou qu'on a
//  decochee) se termine comme une alarme du projet supprimee en marche.
// =============================================================================
#include "HmiRuntime.hpp"
#include "HmiObjectAlarms.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace hmi {

struct Runtime::ObjectAlarmTable {
    const Project*                          project{nullptr};
    std::vector<ObjectAlarm>                all;       // generees, cochees ou non
    std::map<std::string, Id, std::less<>>  ids;       // nom -> identifiant (garde d'un rafraichissement a l'autre)
    std::map<Id, std::size_t>               byId;      // identifiant -> indice dans `all`
    std::set<std::string, std::less<>>      groups;    // les groupes internes, et ceux des instances qui les contiennent
    Id                                      next{0xC0000000u};   // hors de la plage des alarmes du projet
};

const std::vector<ObjectAlarm>& Runtime::objectAlarmList() const noexcept {
    static const std::vector<ObjectAlarm> none;
    return objectAlarms_ ? objectAlarms_->all : none;
}

std::vector<AlarmDef> Runtime::activeObjectAlarmDefs() const {
    std::vector<AlarmDef> out;
    for (const auto& oa : objectAlarmList())
        if (oa.active) out.push_back(oa.def);
    return out;
}

void Runtime::refreshObjectAlarms() {
    if (!objectAlarms_) objectAlarms_ = std::make_shared<ObjectAlarmTable>();
    auto& t = *objectAlarms_;
    // La liste cochee d'avant (noms et definitions) : la generation ne change que si elle change.
    std::vector<std::string> before;
    for (const auto& oa : t.all)
        if (oa.active) before.push_back(oa.def.name + '\x1f' + oa.def.condition + '\x1f' + oa.def.message);
    t.project = project_;
    t.all = project_ ? objectAlarms(*project_) : std::vector<ObjectAlarm>{};
    t.byId.clear();
    t.groups.clear();
    for (std::size_t i = 0; i < t.all.size(); ++i) {
        auto& oa = t.all[i];
        auto it = t.ids.find(oa.def.name);
        if (it == t.ids.end()) it = t.ids.emplace(oa.def.name, t.next++).first;
        oa.def.id = it->second;
        oa.base.id = it->second;
        // Deux alarmes de meme nom sur un objet (la verification le dit) : la premiere compte.
        t.byId.emplace(it->second, i);
        // "Vue.Instance.Objet" : le groupe, et celui de l'instance qui le contient.
        for (std::size_t dot = oa.objectGroup.find('.'); dot != std::string::npos; dot = oa.objectGroup.find('.', dot + 1))
            if (dot > 0 && oa.objectGroup.find('.') < dot) t.groups.insert(oa.objectGroup.substr(0, dot));
        t.groups.insert(oa.objectGroup);
    }
    std::vector<std::string> after;
    for (const auto& oa : t.all)
        if (oa.active) after.push_back(oa.def.name + '\x1f' + oa.def.condition + '\x1f' + oa.def.message);
    if (after != before || objectAlarmsGen_ == 0) ++objectAlarmsGen_;
}

void Runtime::objectAlarmsCycle(double now) {
    if (!objectAlarms_ || objectAlarms_->project != project_ || now - objectAlarmsAt_ >= 1.0 || now < objectAlarmsAt_) {
        refreshObjectAlarms();
        objectAlarmsAt_ = now;
    }
    // 1.9 (fusion de B et G) : la liste cochee a change (ici ou ailleurs) : les
    // liaisons suivent en permanence les variables de la nouvelle (watchAlarms).
    if (objectAlarmsWatched_ != objectAlarmsGen_) {
        objectAlarmsWatched_ = objectAlarmsGen_;
        (void)watchAlarms("objets", activeObjectAlarmDefs());
    }
}

std::size_t Runtime::objectAlarmCount() const noexcept {
    if (!objectAlarms_) return 0;
    return static_cast<std::size_t>(
        std::count_if(objectAlarms_->all.begin(), objectAlarms_->all.end(), [](const ObjectAlarm& a) { return a.active; }));
}

const ObjectAlarm* Runtime::objectAlarm(Id id) const noexcept {
    if (!objectAlarms_) return nullptr;
    const auto it = objectAlarms_->byId.find(id);
    return it == objectAlarms_->byId.end() ? nullptr : &objectAlarms_->all[it->second];
}

const AlarmDef* Runtime::alarmDef(Id id) const noexcept {
    if (project_)
        if (const auto* d = project_->alarm(id)) return d;
    const auto* oa = objectAlarm(id);
    return oa ? &oa->def : nullptr;
}

const AlarmDef* Runtime::alarmDefByName(std::string_view name) const noexcept {
    if (project_)
        if (const auto* d = project_->alarmByName(name)) return d;
    if (!objectAlarms_) return nullptr;
    const auto it = objectAlarms_->ids.find(name);
    if (it == objectAlarms_->ids.end()) return nullptr;
    const auto* oa = objectAlarm(it->second);
    return oa ? &oa->def : nullptr;
}

bool Runtime::isObjectGroup(std::string_view group) const {
    return objectAlarms_ && objectAlarms_->groups.count(group) > 0;
}

Runtime::GroupFigures Runtime::objectGroupFigures(std::string_view group) const {
    GroupFigures f;
    f.known = isObjectGroup(group);
    if (!f.known) return f;
    const std::string g(group);
    for (const auto& a : alarms_) {
        if (a.objectGroup.empty()) continue;
        if (!(a.objectGroup == g || (a.objectGroup.size() > g.size() && a.objectGroup.compare(0, g.size(), g) == 0
                                     && a.objectGroup[g.size()] == '.')))
            continue;
        ++f.count;
        f.active += a.active;
        f.unacked += !a.acked;
        if (f.highest == 0 || a.priority < f.highest) f.highest = a.priority;
    }
    return f;
}

std::vector<const AlarmDef*> Runtime::objectAlarmDefsIn(std::string_view filter) const {
    std::vector<const AlarmDef*> out;
    if (!objectAlarms_) return out;
    for (const auto& oa : objectAlarms_->all)
        if (oa.active && alarmGroupMatches(oa.def.group, oa.objectGroup, oa.symbols, filter)) out.push_back(&oa.def);
    return out;
}

} // namespace hmi
