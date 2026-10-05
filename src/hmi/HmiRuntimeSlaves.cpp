// =============================================================================
//  hmi/HmiRuntimeSlaves.cpp - le moteur de l'IHM et les esclaves simules (1.9)
// -----------------------------------------------------------------------------
//  Un vrai appareil peut avoir un ESCLAVE SIMULE LIE (son clone, au cadenas) ;
//  l'IHM lit l'un ou l'autre (la fiche de l'equipement, la bascule automatique,
//  la page Simulation). Ici :
//
//    * les esclaves de l'application (Hooks::simSlaves), pour la page
//      Simulation et les variables SYS.Slave.* ;
//    * une variable liee lue en ce moment sur un esclave (slaveReadOf) : les
//      reperes des objets (un cadre violet en tirets, une pastille) ;
//    * les equipements lus sur leur esclave (le bandeau LECTURES SIMULEES) ;
//    * chaque bascule (le vrai <-> l'esclave) : un evenement et une marque sur
//      les courbes des variables liees a l'equipement ;
//    * les commandes de la page : la permission Administrer, le journal.
// =============================================================================
#include "HmiRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace hmi {

namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// La racine d'un chemin : "Four1.Consigne" -> "Four1", "Tab[2]" -> "Tab".
std::string_view rootOf(std::string_view path) {
    std::size_t end = path.size();
    for (std::size_t i = 0; i < path.size(); ++i)
        if (path[i] == '.' || path[i] == '[') {
            end = i;
            break;
        }
    std::string_view r = path.substr(0, end);
    while (!r.empty() && std::isspace(static_cast<unsigned char>(r.front()))) r.remove_prefix(1);
    while (!r.empty() && std::isspace(static_cast<unsigned char>(r.back()))) r.remove_suffix(1);
    return r;
}

// L'heure murale (s depuis 1970).
double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// "14:02:31" (heure locale).
std::string clockOf(double wall) {
    if (wall <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(wall);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

} // namespace

std::vector<SimSlave> Runtime::simSlaves(bool withValues) const {
    if (hooks_.simSlaves) return hooks_.simSlaves(withValues);
    // Sans l'ecran (les essais) : ce que dit le projet.
    std::vector<SimSlave> out;
    if (!project_) return out;
    for (const auto& e : project_->equipments) {
        if (!e.modbus() || !e.hasTwin()) continue;
        SimSlave s;
        s.equipment = e.name;
        s.name = e.twinLabel();
        s.key = slaveKey(e.name);
        s.linked = e.linkedSlave();
        s.enabled = e.enabled;
        s.responds = e.twinResponds;
        s.exception = e.twinException;
        s.mode = std::string(readSourceKey(e.simulated ? ReadSource::Slave : e.appRead()));
        s.read = e.simulated || e.appRead() == ReadSource::Slave;
        s.modeLocked = e.simulated;
        s.state = "pas en marche";
        for (const auto& b : e.behaviors) s.animated += b.enabled ? 1 : 0;
        s.forced = static_cast<int>(e.forcings.size());
        out.push_back(std::move(s));
    }
    return out;
}

std::string Runtime::slaveReadOf(std::string_view path) const {
    if (slaveReads_.empty()) return {};
    const std::string_view root = rootOf(path);
    if (root.empty()) return {};
    const Variable* v = boundVariable(root);
    if (!v || v->equipment.empty()) return {};
    const auto it = slaveReads_.find(upperOf(v->equipment));
    if (it == slaveReads_.end() || !it->second.first) return {};
    return v->equipment;
}

std::vector<EquipmentStatus> Runtime::simulatedReads() const {
    std::vector<EquipmentStatus> out;
    for (auto& st : equipmentStatuses())
        if (st.enabled && st.viaTwin) out.push_back(std::move(st));
    return out;
}

bool Runtime::simSlaveCommand(const SimSlaveCommand& c, double now, std::string* why) {
    now_ = std::max(now_, now);
    lastActivity_ = std::max(lastActivity_, now);
    const std::string source = "Page Simulation";
    const auto refuse = [&](std::string m) {
        if (why) *why = m;
        return false;
    };
    if (!permitted("Administrer")) {
        const std::string msg = "page Simulation : permission \xC2\xAB Administrer \xC2\xBB requise ("
                              + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", source, msg);
        return refuse(msg);
    }
    if (!hooks_.simSlaveCommand) return refuse("pas d'esclave simul\xC3\xA9 ici");
    std::string reason;
    if (!hooks_.simSlaveCommand(c, &reason)) return refuse(reason.empty() ? std::string("refus\xC3\xA9") : reason);
    // Au journal : qui, quoi (les gestes de la page ne vont pas dans le projet).
    std::string what = c.what;
    if (!c.key.empty()) what += " " + c.key;
    if (!c.value.empty()) what += " = " + c.value;
    log("Action", source, (c.equipment.empty() ? std::string("tous les esclaves") : c.equipment) + " : " + what
                              + (user_.empty() ? std::string{} : " (" + user_ + ")"));
    if (why) why->clear();
    return true;
}

void Runtime::slaveWatch(double now) {
    if (!project_) return;
    std::map<std::string, std::pair<bool, bool>> next;
    std::vector<EquipmentStatus> changed;
    for (auto& st : equipmentStatuses()) {
        if (!st.twin) continue;
        const bool via = st.enabled && st.viaTwin;
        const std::string key = upperOf(st.name);
        next[key] = {via, st.fallback};
        const auto it = slaveReads_.find(key);
        const bool was = it != slaveReads_.end() && it->second.first;
        if (it != slaveReads_.end() && was != via) changed.push_back(std::move(st));
    }
    slaveReads_ = std::move(next);
    for (const auto& st : changed) {
        const bool via = st.enabled && st.viaTwin;
        std::string text;
        if (via)
            text = st.fallback ? "ne r\xC3\xA9pond plus" + (st.realSilentSince > 0 ? " depuis " + clockOf(st.realSilentSince) : std::string{})
                                     + " : l'IHM lit son esclave simul\xC3\xA9"
                               : "l'IHM lit son esclave simul\xC3\xA9" + (st.chosen ? std::string(" (page Simulation)") : std::string{});
        else
            text = "l'IHM lit de nouveau le vrai appareil" + (st.realOnline ? std::string(" (il r\xC3\xA9pond)") : std::string{});
        event("Communication", st.name, text);
        // Une marque sur les courbes des variables liees a cet equipement : le trait
        // pointille violet et l'heure de la bascule ("14:02:31 - lu sur l'esclave simule").
        const std::string at = clockOf(st.readSince > 0 ? st.readSince : wallSeconds());
        const std::string mark = (at.empty() ? std::string{} : at + " \xC2\xB7 ") + (via ? "lu sur l'esclave simul\xC3\xA9" : "lu sur le vrai appareil");
        for (const auto& v : project_->programs.variables)
            if (!v.equipment.empty() && upperOf(v.equipment) == upperOf(st.name)) addTrendMarker(v.name, mark, false, true);
    }
    (void)now;
}

} // namespace hmi
