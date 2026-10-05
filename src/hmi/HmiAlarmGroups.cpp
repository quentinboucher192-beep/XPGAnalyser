// 1.10.2 (chantier AL) : les groupes d'alarmes de IHM > Alarmes > Groupes et les liens
// des groupes d'alarmes des objets vers eux. Voir HmiAlarmGroups.hpp.
#include "HmiAlarmGroups.hpp"

#include "HmiObjectAlarms.hpp"   // wildcardMatch

#include <algorithm>

namespace hmi {

std::vector<AlarmGroupDef> alarmGroupsOf(const Project& p) {
    std::vector<AlarmGroupDef> out = p.alarmGroups;
    const auto known = [&](std::string_view name) {
        return std::any_of(out.begin(), out.end(), [&](const AlarmGroupDef& g) { return g.name == name; });
    };
    for (const auto& a : p.alarms) {
        if (a.group.empty() || known(a.group)) continue;
        AlarmGroupDef g;
        g.name = a.group;
        out.push_back(std::move(g));
    }
    return out;
}

const AlarmGroupDef* alarmGroupByName(const Project& p, std::string_view name) noexcept {
    for (const auto& g : p.alarmGroups)
        if (g.name == name) return &g;
    return nullptr;
}

AlarmGroupDef* alarmGroupById(Project& p, Id id) noexcept {
    for (auto& g : p.alarmGroups)
        if (g.id == id) return &g;
    return nullptr;
}

bool alarmGroupExists(const Project& p, std::string_view name) noexcept {
    if (name.empty()) return false;
    if (alarmGroupByName(p, name)) return true;
    return std::any_of(p.alarms.begin(), p.alarms.end(), [&](const AlarmDef& a) { return a.group == name; });
}

AlarmGroupDef alarmGroupSettings(const Project& p, std::string_view name) {
    if (const auto* g = alarmGroupByName(p, name)) return *g;
    AlarmGroupDef d;
    d.name = std::string(name);
    return d;
}

std::string alarmZoneOf(const Project& p, std::string_view group) {
    if (const auto* g = alarmGroupByName(p, group); g && !g->zone.empty()) return g->zone;
    return std::string(group);
}

std::string_view alarmAckModeLabel(AlarmAckMode m) noexcept {
    switch (m) {
        case AlarmAckMode::Group: return "Par groupe";
        case AlarmAckMode::Auto: return "Automatique au retour";
        case AlarmAckMode::Single: break;
    }
    return "Un par un";
}

std::string uniqueAlarmGroupName(const Project& p, std::string_view wanted) {
    const std::string base = wanted.empty() ? std::string("Groupe") : std::string(wanted);
    if (!alarmGroupExists(p, base)) return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + "_" + std::to_string(n);
        if (!alarmGroupExists(p, candidate)) return candidate;
    }
}

// ---- les liens -------------------------------------------------------------------------
namespace {

bool isPattern(std::string_view s) noexcept { return s.find('*') != std::string_view::npos; }
bool isSymbolLink(std::string_view s) noexcept { return s.rfind("symbole:", 0) == 0; }

bool inSymbols(std::string_view symbols, std::string_view name) noexcept {
    if (name.empty()) return false;
    for (std::size_t at = 0; at <= symbols.size();) {
        const std::size_t semi = std::min(symbols.find(';', at), symbols.size());
        if (symbols.substr(at, semi - at) == name) return true;
        at = semi + 1;
    }
    return false;
}

} // namespace

const AlarmGroupLink* alarmGroupLinkOf(const Project& p, std::string_view objectGroup, std::string_view symbols) noexcept {
    if (objectGroup.empty()) return nullptr;
    const AlarmGroupLink* enclosing = nullptr;
    const AlarmGroupLink* pattern = nullptr;
    const AlarmGroupLink* symbol = nullptr;
    for (const auto& l : p.alarmGroupLinks) {
        if (l.group.empty()) continue;
        const std::string_view g = l.objectGroup;
        if (isSymbolLink(g)) {
            if (!symbol && inSymbols(symbols, g.substr(8))) symbol = &l;
        } else if (isPattern(g)) {
            if (!pattern && wildcardMatch(objectGroup, g)) pattern = &l;
        } else if (g == objectGroup) {
            return &l;                                               // le groupe lui-meme
        } else if (objectGroup.size() > g.size() && objectGroup.compare(0, g.size(), g) == 0 && objectGroup[g.size()] == '.') {
            if (!enclosing || enclosing->objectGroup.size() < g.size()) enclosing = &l;   // le plus pres
        }
    }
    return enclosing ? enclosing : pattern ? pattern : symbol;
}

const AlarmGroupLink* alarmGroupLinkFor(const Project& p, std::string_view objectGroup) noexcept {
    for (const auto& l : p.alarmGroupLinks)
        if (l.objectGroup == objectGroup) return &l;
    return nullptr;
}

std::vector<std::string> objectGroupsLinkedTo(const Project& p, std::string_view group) {
    std::vector<std::string> out;
    for (const auto& l : p.alarmGroupLinks)
        if (l.group == group) out.push_back(l.objectGroup);
    return out;
}

std::vector<const AlarmGroupLink*> danglingAlarmGroupLinks(const Project& p) {
    std::vector<const AlarmGroupLink*> out;
    for (const auto& l : p.alarmGroupLinks)
        if (!l.group.empty() && !alarmGroupExists(p, l.group)) out.push_back(&l);
    return out;
}

bool setAlarmGroupLink(Project& p, std::string_view objectGroup, std::string_view group) {
    if (objectGroup.empty()) return false;
    auto it = std::find_if(p.alarmGroupLinks.begin(), p.alarmGroupLinks.end(),
                           [&](const AlarmGroupLink& l) { return l.objectGroup == objectGroup; });
    if (group.empty()) {
        if (it == p.alarmGroupLinks.end()) return false;
        p.alarmGroupLinks.erase(it);
        return true;
    }
    if (it != p.alarmGroupLinks.end()) {
        if (it->group == group) return false;
        it->group = std::string(group);
        return true;
    }
    p.alarmGroupLinks.push_back({std::string(objectGroup), std::string(group)});
    return true;
}

std::size_t renameAlarmGroup(Project& p, std::string_view from, std::string_view to) {
    if (from.empty() || from == to) return 0;
    std::size_t n = 0;
    for (auto& g : p.alarmGroups)
        if (g.name == from) { g.name = std::string(to); ++n; }
    for (auto& a : p.alarms)
        if (a.group == from) { a.group = std::string(to); ++n; }
    for (auto& l : p.alarmGroupLinks)
        if (l.group == from) { l.group = std::string(to); ++n; }
    for (auto& v : p.views)
        for (auto& o : v.objects)
            for (auto& ov : o.alarmOverrides)
                if (ov.group && *ov.group == from) { ov.group = std::string(to); ++n; }
    return n;
}

} // namespace hmi
