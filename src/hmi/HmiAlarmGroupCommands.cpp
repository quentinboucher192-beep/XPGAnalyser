// 1.10.2 (chantier AL) : les commandes des groupes d'alarmes et de leurs liens.
#include "HmiAlarmGroupCommands.hpp"
#include "HmiObjectAlarms.hpp"

#include <algorithm>

namespace hmi {

core::CommandPtr addAlarmGroupCmd(const DocumentPtr& doc, std::string_view wanted, std::string* created) {
    const std::string name = uniqueAlarmGroupName(doc->project, wanted.empty() ? std::string_view("Groupe") : wanted);
    if (created) *created = name;
    return changeProject(doc, "Ajouter le groupe d'alarmes " + name, [&](Project& p) {
        AlarmGroupDef g;
        g.id = p.allocate();
        g.name = name;
        p.alarmGroups.push_back(std::move(g));
    });
}

core::CommandPtr setAlarmGroupCmd(const DocumentPtr& doc, std::string_view name, const AlarmGroupDef& settings) {
    if (name.empty() || !alarmGroupExists(doc->project, name)) return nullptr;
    return changeProject(doc, "R\xC3\xA9glages du groupe d'alarmes " + std::string(name), [&](Project& p) {
        AlarmGroupDef* g = nullptr;
        for (auto& x : p.alarmGroups)
            if (x.name == name) g = &x;
        if (!g) {                                   // un groupe nomme par des alarmes : declare
            AlarmGroupDef d;
            d.id = p.allocate();
            d.name = std::string(name);
            p.alarmGroups.push_back(std::move(d));
            g = &p.alarmGroups.back();
        }
        const Id id = g->id;
        *g = settings;
        g->id = id;
        g->name = std::string(name);
        g->priority = std::clamp(g->priority, 0, kAlarmPriorities);
        g->level = std::clamp(g->level, 0, 4);
    });
}

core::CommandPtr renameAlarmGroupCmd(const DocumentPtr& doc, std::string_view from, std::string_view to, std::string* why) {
    if (from == to || from.empty()) return nullptr;
    if (to.empty()) {
        if (why) *why = "un groupe d'alarmes a un nom";
        return nullptr;
    }
    if (alarmGroupExists(doc->project, to)) {
        if (why) *why = "le groupe " + std::string(to) + " existe d\xC3\xA9j\xC3\xA0";
        return nullptr;
    }
    return changeProject(doc, "Renommer le groupe d'alarmes " + std::string(from) + " en " + std::string(to),
                         [&](Project& p) { (void)renameAlarmGroup(p, from, to); });
}

core::CommandPtr deleteAlarmGroupCmd(const DocumentPtr& doc, std::string_view name) {
    if (!alarmGroupByName(doc->project, name)) return nullptr;
    return changeProject(doc, "Supprimer le groupe d'alarmes " + std::string(name), [&](Project& p) {
        p.alarmGroups.erase(std::remove_if(p.alarmGroups.begin(), p.alarmGroups.end(),
                                           [&](const AlarmGroupDef& g) { return g.name == name; }),
                            p.alarmGroups.end());
    });
}

core::CommandPtr linkObjectGroupsCmd(const DocumentPtr& doc, const std::vector<std::string>& objectGroups, std::string_view group) {
    if (objectGroups.empty()) return nullptr;
    const std::string label = group.empty()
        ? (objectGroups.size() == 1 ? "D\xC3\xA9lier " + objectGroups.front() : "D\xC3\xA9lier " + std::to_string(objectGroups.size()) + " groupes d'objets")
        : (objectGroups.size() == 1 ? "Lier " + objectGroups.front() + " \xC3\xA0 " + std::string(group)
                                    : "Lier " + std::to_string(objectGroups.size()) + " groupes d'objets \xC3\xA0 " + std::string(group));
    return changeProject(doc, label, [&](Project& p) {
        for (const auto& og : objectGroups) (void)setAlarmGroupLink(p, og, group);
    });
}

// 1.11 (R111) : « Lier... » dans une fenetre a cocher.
std::vector<LinkableObjectGroup> linkableObjectGroups(const Project& p) {
    std::vector<LinkableObjectGroup> groups, views, symbols, others;
    const auto bump = [](std::vector<LinkableObjectGroup>& list, const std::string& name, int kind) {
        for (auto& g : list)
            if (g.name == name) {
                ++g.alarms;
                return;
            }
        list.push_back({name, kind, 1, {}});
    };
    for (const auto& oa : objectAlarms(p)) {
        if (!oa.objectGroup.empty()) bump(groups, oa.objectGroup, 0);
        if (!oa.view.empty()) bump(views, oa.view + ".*", 1);
        std::string syms = oa.symbols;
        if (!oa.symbol.empty()) syms += (syms.empty() ? "" : ";") + oa.symbol;
        std::vector<std::string> seen;
        for (std::size_t at = 0; at <= syms.size();) {
            std::size_t end = syms.find(';', at);
            if (end == std::string::npos) end = syms.size();
            const std::string s = syms.substr(at, end - at);
            at = end + 1;
            if (s.empty() || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
            seen.push_back(s);
            bump(symbols, "symbole:" + s, 2);
        }
    }
    std::vector<LinkableObjectGroup> out;
    for (auto* list : {&groups, &views, &symbols})
        for (auto& g : *list) out.push_back(std::move(g));
    for (const auto& l : p.alarmGroupLinks)
        if (std::none_of(out.begin(), out.end(), [&](const LinkableObjectGroup& g) { return g.name == l.objectGroup; })
            && std::none_of(others.begin(), others.end(), [&](const LinkableObjectGroup& g) { return g.name == l.objectGroup; }))
            others.push_back({l.objectGroup, 3, 0, {}});
    for (auto& g : others) out.push_back(std::move(g));
    for (auto& g : out)
        for (const auto& l : p.alarmGroupLinks)
            if (l.objectGroup == g.name) g.linkedTo = l.group;
    return out;
}

core::CommandPtr setLinkedObjectGroupsCmd(const DocumentPtr& doc, std::string_view group, const std::vector<std::string>& checked) {
    if (group.empty()) return nullptr;
    const auto before = objectGroupsLinkedTo(doc->project, group);
    std::vector<std::string> add, remove;
    for (const auto& c : checked)
        if (!c.empty() && std::find(before.begin(), before.end(), c) == before.end() && std::find(add.begin(), add.end(), c) == add.end())
            add.push_back(c);
    for (const auto& b : before)
        if (std::find(checked.begin(), checked.end(), b) == checked.end()) remove.push_back(b);
    if (add.empty() && remove.empty()) return nullptr;
    const std::string g(group);
    const auto count = [](std::size_t n) { return n == 1 ? std::string("1 groupe d'objets") : std::to_string(n) + " groupes d'objets"; };
    std::string label;
    if (remove.empty()) label = add.size() == 1 ? "Lier " + add.front() + " \xC3\xA0 " + g : "Lier " + count(add.size()) + " \xC3\xA0 " + g;
    else if (add.empty()) label = remove.size() == 1 ? "D\xC3\xA9lier " + remove.front() + " de " + g : "D\xC3\xA9lier " + count(remove.size()) + " de " + g;
    else label = "Lier \xC3\xA0 " + g + " : " + std::to_string(add.size()) + " coch\xC3\xA9(s), " + std::to_string(remove.size()) + " d\xC3\xA9" "coch\xC3\xA9(s)";
    return changeProject(doc, label, [&](Project& p) {
        for (const auto& r : remove) (void)setAlarmGroupLink(p, r, "");
        for (const auto& a : add) (void)setAlarmGroupLink(p, a, g);
    });
}

} // namespace hmi
