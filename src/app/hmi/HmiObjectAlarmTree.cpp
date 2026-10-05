// =============================================================================
//  app/hmi/HmiObjectAlarmTree.cpp - le noeud "Alarmes" d'un objet (1.10, chantier O)
// =============================================================================
#include "HmiObjectAlarmTree.hpp"

#include "../../hmi/HmiObjectAlarms.hpp"

#include <map>

namespace app::alarmtree {

namespace {

std::string_view priorityName(int p) {
    switch (p) {
        case 1: return "Critique";
        case 2: return "Haute";
        case 3: return "Moyenne";
        default: return "Basse";
    }
}

Entry entryOf(const hmi::ObjectAlarm& a) {
    Entry e;
    e.name = a.localName.empty() ? a.def.name : a.localName;
    e.full = a.def.name;
    e.localName = a.localName;
    e.path = a.path;
    e.object = a.objectId;
    e.priority = a.def.priority;
    e.active = a.active;
    e.overridden = !a.overridden.empty();
    return e;
}

std::string labelOf(const std::string& group, std::size_t n, bool root) {
    return (root ? std::string("Alarmes \xC2\xB7 ") : std::string{}) + group + " (" + std::to_string(n) + ")";
}

} // namespace

std::string Entry::detail() const {
    std::string out(priorityName(priority));
    if (!active) out += " \xC2\xB7 d\xC3\xA9" "coch\xC3\xA9" "e";
    if (overridden) out += " \xC2\xB7 surcharg\xC3\xA9" "e";
    return out;
}

std::optional<Group> nodeOf(const hmi::Project& project, const hmi::View& view, const hmi::Object& object) {
    if (!hmi::viewGeneratesAlarms(view)) return std::nullopt;
    const auto alarms = hmi::objectAlarmsOf(project, view, object);
    if (alarms.empty()) return std::nullopt;
    Group root;
    root.group = hmi::objectGroupOf(view, object);
    root.count = alarms.size();
    root.label = labelOf(root.group, root.count, true);
    // Les alarmes de l'objet lui-meme, puis un groupe par objet de l'instance
    // (dans l'ordre ou ils apparaissent).
    std::vector<std::string> order;
    std::map<std::string, std::vector<Entry>> byGroup;
    for (const auto& a : alarms) {
        if (a.objectGroup.empty() || a.objectGroup == root.group) {
            root.alarms.push_back(entryOf(a));
            continue;
        }
        if (byGroup.find(a.objectGroup) == byGroup.end()) order.push_back(a.objectGroup);
        byGroup[a.objectGroup].push_back(entryOf(a));
    }
    for (const auto& g : order) {
        Group sub;
        sub.group = g;
        sub.alarms = std::move(byGroup[g]);
        sub.count = sub.alarms.size();
        sub.label = labelOf(g, sub.count, false);
        root.groups.push_back(std::move(sub));
    }
    return root;
}

std::vector<Line> linesOf(const Group& root) {
    std::vector<Line> out;
    out.push_back({0, &root, nullptr});
    for (const auto& a : root.alarms) out.push_back({1, nullptr, &a});
    for (const auto& g : root.groups) {
        out.push_back({1, &g, nullptr});
        for (const auto& a : g.alarms) out.push_back({2, nullptr, &a});
    }
    return out;
}

unsigned priorityColor(int priority) noexcept {
    switch (priority) {
        case 1: return 0xE5484Du;      // critique : rouge
        case 2: return 0xF08C3Au;      // haute : orange
        case 3: return 0xE8C547u;      // moyenne : jaune
        default: return 0x5B9BF0u;     // basse : bleu
    }
}

} // namespace app::alarmtree
