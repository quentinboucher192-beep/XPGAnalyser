// app/hmi/HmiTwinPanes.cpp - Configuration > Equipements, lot 17 : supprimer
// et deplacer un equipement, ses zones memoire (a la main, un modele, la
// detection), la carte memoire et son scanner, les jumeaux (esclaves Modbus
// virtuels) et le reseau simule.
#include "HmiCommPanes.hpp"

#include "HmiCommHost.hpp"
#include "HmiEquipmentHost.hpp"
#include "HmiMemoryMap.hpp"
#include "HmiTwinValues.hpp"
#include "HmiNetDiagram.hpp"
#include "HmiPaneKit.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiTwin.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../hmi/HmiZones.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using PG = ui::PropertyGrid;
namespace eq = hmi::equip;
namespace zn = hmi::zones;
using hmi::MemTable;

namespace {

const MemTable kMapOrder[] = {MemTable::Holding, MemTable::InputRegisters, MemTable::Coils, MemTable::DiscreteInputs};

bool loopbackHost(const std::string& host) {
    std::uint32_t ip = 0;
    return host == "localhost" || (eq::parseIpv4(host, ip) && (ip >> 24) == 127);
}

std::string ageText(double seconds) {
    if (seconds < 0) return "jamais";
    if (seconds < 1.5) return "\xC3\xA0 l'instant";
    if (seconds < 90) return "il y a " + std::to_string(static_cast<int>(std::lround(seconds))) + " s";
    return "il y a " + std::to_string(static_cast<int>(std::lround(seconds / 60))) + " min";
}

std::string clockNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "le %02d/%02d \xC3\xA0 %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
    return b;
}

// Le texte d'une exception Modbus.
const std::vector<std::string>& exceptionLabels() {
    static const std::vector<std::string> k{"aucune", "01 fonction non prise en charge", "02 adresse ill\xC3\xA9gale", "03 valeur ill\xC3\xA9gale",
                                            "04 d\xC3\xA9" "faut de l'esclave", "06 esclave occup\xC3\xA9", "0B passerelle : pas de r\xC3\xA9ponse"};
    return k;
}

std::string exceptionLabel(int code) {
    switch (code) {
        case 0: return exceptionLabels()[0];
        case 1: return exceptionLabels()[1];
        case 2: return exceptionLabels()[2];
        case 3: return exceptionLabels()[3];
        case 4: return exceptionLabels()[4];
        case 6: return exceptionLabels()[5];
        case 11: return exceptionLabels()[6];
        default: return std::to_string(code);
    }
}

// Le libelle d'un parametre de comportement selon son genre.
std::string behaviorParam(hmi::BehaviorKind k, char which) {
    using K = hmi::BehaviorKind;
    switch (k) {
        case K::Constant: return which == 'a' ? "Valeur" : std::string{};
        case K::Sine: return which == 'a' ? "Minimum" : which == 'b' ? "Maximum" : which == 'p' ? "P\xC3\xA9riode (s)" : std::string{};
        case K::Ramp: return which == 'a' ? "D\xC3\xA9part" : which == 'b' ? "Arriv\xC3\xA9" "e" : which == 'p' ? "Dur\xC3\xA9" "e (s)" : std::string{};
        case K::Counter: return which == 'a' ? "D\xC3\xA9part" : which == 'b' ? "Pas" : which == 'p' ? "Toutes les (s)" : std::string{};
        case K::Blink: return which == 'p' ? "P\xC3\xA9riode (s)" : std::string{};
        case K::Random: return which == 'a' ? "Minimum" : which == 'b' ? "Maximum" : which == 'p' ? "Toutes les (s)" : std::string{};
        case K::Copy: return which == 's' ? "Source (adresse)" : which == 'd' ? "Retard (s)" : std::string{};
        case K::FollowPlc: return which == 's' ? "Variable de l'automate" : std::string{};
        case K::Steps: return which == 's' ? "\xC3\x89tapes (1; 5; 3)" : which == 'p' ? "Toutes les (s)" : std::string{};
    }
    return {};
}

std::string num(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

} // namespace

// ============================================================ petits outils ===
const hmi::Equipment* HmiCommPane::equipmentOf(const std::string& name) const {
    return name.empty() || name == kPlcKey ? nullptr : doc_->project.equipmentByName(name);
}

std::pair<std::uint32_t, std::uint32_t> HmiCommPane::plcMemory() const {
    const domain::Project* plc = hosts_.plc ? hosts_.plc() : nullptr;
    if (!plc || !plc->hardware.memory.declared) return {0, 0};
    return {plc->hardware.memory.internalBits, plc->hardware.memory.internalWords};
}

std::string HmiCommPane::twinStateText(const hmi::Equipment& e) const {
    if (!e.hasTwin()) return "pas d'esclave simul\xC3\xA9";
    if (!e.enabled) return "d\xC3\xA9sactiv\xC3\xA9";
    if (!e.twinRunning) return "arr\xC3\xAAt\xC3\xA9";
    auto* h = host();
    const int port = h ? h->simulatedPort(e.name) : 0;
    if (!port) return "pas encore d\xC3\xA9marr\xC3\xA9";
    const auto st = h->simulatedStats(e.name);
    std::string s = !e.twinResponds ? std::string("panne simul\xC3\xA9" "e (ne r\xC3\xA9pond plus)")
                   : e.twinException ? "r\xC3\xA9pond l'exception " + exceptionLabel(e.twinException)
                                     : std::string("en marche");
    s += " \xC2\xB7 " + std::to_string(st.requests) + " requ\xC3\xAAte" + (st.requests > 1 ? "s" : "");
    if (e.twinExpose) s += " \xC2\xB7 visible sur 0.0.0.0:" + std::to_string(e.twinExposePort);
    return s;
}

// ================================================================ supprimer ===
bool HmiCommPane::deleteEquipment(const std::string& name, const std::vector<std::string>& drop, bool dropTwin, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (name == kPlcKey) return fail("L'automate du projet ne se supprime pas (Configuration \xE2\x80\xBA Communication : le simulateur, ou un autre automate).");
    const auto* e = doc_->project.equipmentByName(name);
    if (!e) return fail("pas d'\xC3\xA9quipement " + name);
    const std::string real = e->name;
    const bool keepTwin = !dropTwin && e->twin && !e->simulated;
    std::size_t removed = 0, unbound = 0;
    const auto dropped = [&](const std::string& v) {
        return std::any_of(drop.begin(), drop.end(), [&](const std::string& d) { return same(d, v); });
    };
    const std::string label = keepTwin ? "Supprimer le vrai appareil " + real + " (son esclave simul\xC3\xA9 reste)"
                                       : "Supprimer l'\xC3\xA9quipement " + real + (drop.empty() ? std::string{} : " et " + std::to_string(drop.size()) + " variable(s)");
    if (!changeProject(label, [&](hmi::Project& x) {
            if (keepTwin) {
                for (auto& q : x.equipments)
                    if (q.name == real) {
                        q.simulated = true;
                        q.twin = false;
                    }
            } else {
                std::erase_if(x.equipments, [&](const hmi::Equipment& q) { return q.name == real; });
            }
            auto& vars = x.programs.variables;
            for (auto it = vars.begin(); it != vars.end();) {
                if (!same(it->equipment, real)) {
                    ++it;
                    continue;
                }
                if (dropped(it->name)) {
                    it = vars.erase(it);
                    ++removed;
                    continue;
                }
                if (!keepTwin) {
                    it->equipment.clear();
                    it->address.clear();
                    ++unbound;
                }
                ++it;
            }
        }))
        return false;
    if (!keepTwin && same(equipment_, real)) equipment_.clear();
    std::string m = keepTwin ? real + " : le vrai appareil est retir\xC3\xA9, son esclave simul\xC3\xA9 reste (seulement simul\xC3\xA9)" : real + " supprim\xC3\xA9";
    if (removed) m += " \xC2\xB7 " + std::to_string(removed) + " variable(s) supprim\xC3\xA9" "e(s) : ce qui les cite est signal\xC3\xA9 par Compiler";
    if (unbound) m += " \xC2\xB7 " + std::to_string(unbound) + " variable(s) gard\xC3\xA9" "e(s), d\xC3\xA9li\xC3\xA9" "e(s)";
    say(m + ". Ctrl+Z rend tout.");
    return true;
}

bool HmiCommPane::askDeleteEquipment(const std::string& name) {
    if (name.empty()) return false;
    if (name == kPlcKey) {
        say("L'automate du projet ne se supprime pas (Configuration \xE2\x80\xBA Communication : le simulateur, ou un autre automate).", true);
        return false;
    }
    const auto* e = doc_->project.equipmentByName(name);
    if (!e) return false;
    if (!hosts_.ask) return false;
    const auto& p = doc_->project;
    const auto vars = eq::boundVariables(p, *e);
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiDelete";
    spec.title = "Supprimer l'\xC3\xA9quipement \xC2\xAB " + e->name + " \xC2\xBB";
    const std::string where = e->simulated ? std::string("seulement simul\xC3\xA9") : e->host + ":" + std::to_string(e->port);
    spec.text = e->name + " (" + std::string(hmi::equipmentTypeLabel(e->type)) + ", " + where + ") sera retir\xC3\xA9 du projet.";
    if (!vars.empty())
        spec.text += "\nSes variables IHM : d\xC3\xA9" "coch\xC3\xA9" "e, elle reste (d\xC3\xA9li\xC3\xA9" "e : une variable IHM locale) ; coch\xC3\xA9" "e, elle est supprim\xC3\xA9" "e "
                     "(ce qui la cite - vues, scripts, alarmes - sera signal\xC3\xA9 par Compiler).";
    spec.text += "\nCtrl+Z rend tout.";
    spec.listTitle = "Ses variables IHM (" + std::to_string(vars.size()) + ")";
    std::vector<std::string> names;
    for (const auto* v : vars) {
        names.push_back(v->name);
        const auto writers = zn::writersOf(p, v->name);
        spec.items.push_back({v->name, v->type + " \xC2\xB7 " + v->address + " \xC2\xB7 " + (writers.empty() ? std::string("lue") : "\xC3\xA9" "crite par " + writers.front()), false});
    }
    const bool twin = e->twin && !e->simulated;
    if (twin) spec.extras.push_back({"Supprimer aussi son esclave simul\xC3\xA9 (" + e->twinLabel() + ")", "", true});
    spec.confirm = "Supprimer l'\xC3\xA9quipement";
    spec.danger = true;
    spec.confirmLabel = [twin](const std::vector<bool>& items, const std::vector<bool>& extras, int) {
        const auto n = static_cast<std::size_t>(std::count(items.begin(), items.end(), true));
        const bool keep = twin && !extras.empty() && !extras[0];
        std::string s = keep ? std::string("Supprimer le vrai appareil") : std::string("Supprimer l'\xC3\xA9quipement");
        if (n) s += " et " + std::to_string(n) + " variable" + (n > 1 ? "s" : "");
        return s;
    };
    spec.note = e->simulated ? std::string{} : std::string("L'appareil lui-m\xC3\xAAme n'est pas touch\xC3\xA9 : seul le projet l'oublie.");
    const std::string real = e->name;
    hosts_.ask(std::move(spec), [this, real, names, twin](bool ok, const HmiAskDialog::Answer& a) {
        if (!ok) return;
        std::vector<std::string> drop;
        for (std::size_t i = 0; i < names.size() && i < a.items.size(); ++i)
            if (a.items[i]) drop.push_back(names[i]);
        const bool dropTwin = !twin || a.extras.empty() || a.extras[0];
        (void)deleteEquipment(real, drop, dropTwin);
    });
    return true;
}

// ================================================================= deplacer ===
std::string HmiCommPane::freeAddressOn(const std::string& port, const std::string& equipment) const {
    const auto& p = doc_->project;
    std::vector<std::uint32_t> taken;
    if (simView_) {
        const hmi::SimPort* sp = nullptr;
        for (const auto& x : p.simPorts)
            if (x.name == port) sp = &x;
        std::uint32_t ip = 0;
        if (!sp || !eq::parseIpv4(sp->ip, ip)) return {};
        for (const auto& x : p.simPorts)
            if (std::uint32_t a = 0; eq::parseIpv4(x.ip, a)) taken.push_back(a);
        for (const auto& e : p.equipments)
            if (std::uint32_t a = 0; e.hasTwin() && e.name != equipment && eq::parseIpv4(e.twinAddress(), a)) taken.push_back(a);
        const std::uint32_t got = eq::suggestAddress(ip, sp->prefix, taken);
        return got ? eq::ipv4Text(got) : std::string{};
    }
    const auto a = adapter(port);
    if (!a || !a->ip()) return {};
    if (auto* h = host())
        for (const auto& x : h->adapters())
            for (const auto& [ip, pre] : x.addresses) taken.push_back(ip);
    for (const auto& e : p.equipments)
        if (std::uint32_t ip = 0; e.name != equipment && eq::parseIpv4(e.host, ip)) taken.push_back(ip);
    if (std::uint32_t ip = 0; p.comm.modbus() && eq::parseIpv4(p.comm.host, ip)) taken.push_back(ip);
    const std::uint32_t got = eq::suggestAddress(a->ip(), a->prefix(), taken);
    return got ? eq::ipv4Text(got) : std::string{};
}

bool HmiCommPane::askMoveToPort(const std::string& equipment, const std::string& port) {
    const auto& p = doc_->project;
    const bool plc = equipment == kPlcKey;
    const auto* e = equipmentOf(equipment);
    if (!plc && !e) return false;
    const std::string label = plc ? std::string("l'automate du projet") : e->name;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiMove";
    if (simView_) {
        if (!e || !e->hasTwin()) return false;
        const hmi::SimPort* sp = nullptr;
        for (const auto& x : p.simPorts)
            if (x.name == port) sp = &x;
        if (!sp) return false;
        std::uint32_t tip = 0, pip = 0;
        if (eq::parseIpv4(e->twinAddress(), tip) && eq::parseIpv4(sp->ip, pip) && eq::sameNetwork(tip, pip, sp->prefix)) {
            say(e->twinLabel() + " est d\xC3\xA9j\xC3\xA0 dans le r\xC3\xA9seau de " + sp->name + ".");
            return false;
        }
        const std::string free = freeAddressOn(port, equipment);
        std::string keep;
        if (tip) {
            std::vector<std::uint32_t> taken{tip};
            const std::uint32_t k = eq::suggestAddress(tip, sp->prefix, taken);
            keep = k ? eq::ipv4Text(k) : std::string{};
        }
        spec.title = "D\xC3\xA9placer \xC2\xAB " + e->twinLabel() + " \xC2\xBB sur " + sp->name;
        spec.text = e->twinLabel() + " (" + e->twinAddress() + ") n'est pas dans le r\xC3\xA9seau de " + sp->name + " (" + sp->ip + " / " + std::to_string(sp->prefix) + ").";
        spec.options.push_back({"Changer l'adresse de l'esclave simul\xC3\xA9", "Il r\xC3\xA9pondra \xC3\xA0 cette adresse du r\xC3\xA9seau simul\xC3\xA9.", free, true, "192.168.56.20"});
        spec.options.push_back({"Garder son adresse : le port simul\xC3\xA9 prend une adresse de son r\xC3\xA9seau",
                                keep.empty() ? std::string{} : sp->name + " devient " + keep + " / " + std::to_string(sp->prefix) + " (rien ne change sur le vrai PC).", {}, false, {}});
        spec.confirm = "D\xC3\xA9placer";
        spec.note = "Ctrl+Z revient en arri\xC3\xA8re.";
    } else {
        const auto a = adapter(port);
        if (!a) return false;
        const std::string hostText = plc ? p.comm.host : e->host;
        std::uint32_t ip = 0;
        if (!eq::parseIpv4(hostText, ip)) {
            say(label + " : son adresse est un nom (" + hostText + ") ; changez-la dans sa fiche.", true);
            return false;
        }
        for (const auto& [aip, pre] : a->addresses)
            if (eq::sameNetwork(ip, aip, pre)) {
                say(label + " est d\xC3\xA9j\xC3\xA0 dans le r\xC3\xA9seau du port " + a->name + ".");
                return false;
            }
        const std::string free = freeAddressOn(port, equipment);
        int pre = 24;
        if ((ip & 0xFFF00000u) == 0xAC100000u) pre = 16;
        std::vector<std::uint32_t> taken{ip};
        if (auto* h = host())
            for (const auto& x : h->adapters())
                for (const auto& [xip, xpre] : x.addresses) taken.push_back(xip);
        for (const auto& q : p.equipments)
            if (std::uint32_t qip = 0; eq::parseIpv4(q.host, qip)) taken.push_back(qip);
        if (std::uint32_t cip = 0; eq::parseIpv4(p.comm.host, cip)) taken.push_back(cip);
        const std::uint32_t k = eq::suggestAddress(ip, pre, taken);
        const std::string keep = k ? eq::ipv4Text(k) : std::string{};
        // Un autre port est-il deja dans ce reseau ?
        std::string already;
        if (auto* h = host())
            for (const auto& x : h->adapters())
                for (const auto& [xip, xpre] : x.addresses)
                    if (x.name != a->name && eq::sameNetwork(ip, xip, xpre)) already = x.name;
        spec.title = "D\xC3\xA9placer \xC2\xAB " + label + " \xC2\xBB sur le port " + a->name;
        spec.text = label + " (" + hostText + ") n'est pas dans le r\xC3\xA9seau du port " + a->name + " (" + (a->ip() ? eq::networkText(a->ip(), a->prefix()) : std::string("sans adresse")) + ").";
        spec.options.push_back({"Changer l'adresse de l'\xC3\xA9quipement",
                                "L'IHM le cherchera \xC3\xA0 cette adresse, dans le r\xC3\xA9seau du port : l'appareil doit l'avoir (elle se r\xC3\xA8gle sur l'appareil lui-m\xC3\xAAme).",
                                free, true, "192.168.1.40"});
        spec.options.push_back({"Garder son adresse : donner au port " + a->name + " une adresse de plus" + (keep.empty() ? std::string{} : " (" + keep + " / " + std::to_string(pre) + ")"),
                                "Le port garde les siennes et rejoint aussi le r\xC3\xA9seau de l'\xC3\xA9quipement. Il faut les droits d'administrateur ; "
                                "\xC2\xAB Remettre l'adresse d'avant \xC2\xBB revient en arri\xC3\xA8re."
                                    + (already.empty() ? std::string{} : " Attention : le port " + already + " est d\xC3\xA9j\xC3\xA0 dans ce r\xC3\xA9seau (le PC choisira l'un ou l'autre)."),
                                {}, false, {}});
        spec.confirm = "D\xC3\xA9placer";
        spec.note = "Changer l'adresse : Ctrl+Z revient \xC3\xA0 l'ancienne.";
    }
    if (!hosts_.ask) return false;
    hosts_.ask(std::move(spec), [this, equipment, port](bool ok, const HmiAskDialog::Answer& a) {
        if (ok) (void)moveToPort(equipment, port, a.option == 0, a.field);
    });
    return true;
}

bool HmiCommPane::moveToPort(const std::string& equipment, const std::string& port, bool changeAddress, const std::string& rawAddress, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& p = doc_->project;
    const bool plc = equipment == kPlcKey;
    const auto* e = equipmentOf(equipment);
    if (!plc && !e) return fail("pas d'\xC3\xA9quipement " + equipment);
    std::string address = trimmed(rawAddress);
    if (simView_) {
        if (!e || !e->hasTwin()) return fail(equipment + " n'a pas d'esclave simul\xC3\xA9");
        const hmi::SimPort* sp = nullptr;
        for (const auto& x : p.simPorts)
            if (x.name == port) sp = &x;
        if (!sp) return fail("pas de port simul\xC3\xA9 " + port);
        const std::string name = e->name, spName = sp->name;
        if (changeAddress) {
            if (address.empty()) address = freeAddressOn(port, equipment);
            std::uint32_t ip = 0;
            if (!eq::parseIpv4(address, ip)) return fail("adresse \xC2\xAB " + address + " \xC2\xBB : quatre nombres de 0 \xC3\xA0 255");
            if (!changeProject("D\xC3\xA9placer l'esclave simul\xC3\xA9 de " + name + " sur " + spName, [&](hmi::Project& x) {
                    for (auto& q : x.equipments)
                        if (q.name == name) q.twinHost = eq::ipv4Text(ip);
                }))
                return false;
            say(e->twinLabel() + " : " + eq::ipv4Text(ip) + " (r\xC3\xA9seau simul\xC3\xA9 de " + spName + "). Ctrl+Z revient en arri\xC3\xA8re.");
            return true;
        }
        std::uint32_t tip = 0;
        if (!eq::parseIpv4(e->twinAddress(), tip)) return fail("l'adresse de l'esclave simul\xC3\xA9 est un nom");
        std::vector<std::uint32_t> taken{tip};
        const std::uint32_t k = eq::suggestAddress(tip, sp->prefix, taken);
        if (!k) return fail("aucune adresse libre dans le r\xC3\xA9seau de l'esclave simul\xC3\xA9");
        if (!changeProject(spName + " : " + eq::ipv4Text(k), [&](hmi::Project& x) {
                for (auto& q : x.simPorts)
                    if (q.name == spName) q.ip = eq::ipv4Text(k);
            }))
            return false;
        say(spName + " devient " + eq::ipv4Text(k) + " / " + std::to_string(sp->prefix) + " : il rejoint " + e->twinLabel() + ". Ctrl+Z revient en arri\xC3\xA8re.");
        return true;
    }
    const auto a = adapter(port);
    if (!a) return fail("port inconnu : " + port);
    const std::string label = plc ? std::string("l'automate du projet") : e->name;
    if (changeAddress) {
        if (address.empty()) address = freeAddressOn(port, equipment);
        std::uint32_t ip = 0;
        if (!eq::parseIpv4(address, ip)) return fail("adresse \xC2\xAB " + address + " \xC2\xBB : quatre nombres de 0 \xC3\xA0 255 (192.168.1.40)");
        bool inNet = false;
        for (const auto& [aip, pre] : a->addresses) inNet = inNet || eq::sameNetwork(ip, aip, pre);
        if (!inNet) return fail(address + " n'est pas dans le r\xC3\xA9seau du port " + a->name);
        const std::string text = eq::ipv4Text(ip);
        if (plc) {
            if (!change("Adresse de l'automate : " + text, [&](hmi::Communication& x) { x.host = text; })) return false;
        } else if (!setEquipmentField(e->name, "hote", text, why)) {
            return false;
        }
        say(label + " : " + text + " (r\xC3\xA9seau du port " + a->name + "). Il doit avoir cette adresse ; Ctrl+Z revient \xC3\xA0 l'ancienne.");
        return true;
    }
    // Garder son adresse : le port en recoit une de plus, dans le reseau de l'equipement.
    auto* h = host();
    if (!h) return fail("le r\xC3\xA9seau du PC n'est pas suivi ici");
    std::uint32_t ip = 0;
    if (!eq::parseIpv4(plc ? p.comm.host : e->host, ip)) return fail(label + " : son adresse est un nom");
    const int pre = (ip & 0xFFF00000u) == 0xAC100000u ? 16 : 24;
    std::vector<std::uint32_t> taken{ip};
    for (const auto& x : h->adapters())
        for (const auto& [xip, xpre] : x.addresses) taken.push_back(xip);
    for (const auto& q : p.equipments)
        if (std::uint32_t qip = 0; eq::parseIpv4(q.host, qip)) taken.push_back(qip);
    const std::uint32_t k = address.empty() ? eq::suggestAddress(ip, pre, taken) : [&] {
        std::uint32_t typed = 0;
        return eq::parseIpv4(address, typed) ? typed : 0u;
    }();
    if (!k) return fail("aucune adresse libre dans le r\xC3\xA9seau de " + label);
    hmi::netinfo::PortConfig cfg;
    cfg.add = true;
    cfg.ip = eq::ipv4Text(k);
    cfg.mask = eq::maskText(pre);
    std::string reason;
    if (!h->applyPort(*a, cfg, &reason)) return fail("Le port " + a->name + " : " + reason);
    port_ = a->name;
    say("Le port " + a->name + " re\xC3\xA7oit l'adresse " + cfg.ip + " / " + std::to_string(pre) + " en plus : il rejoint " + label
        + ". \xC2\xAB Remettre l'adresse d'avant \xC2\xBB revient en arri\xC3\xA8re.");
    return true;
}

// ============================================================ zones memoire ===
bool HmiCommPane::setZones(const std::string& equipment, MemTable table, const std::string& text, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (equipment == kPlcKey) return fail("Les zones de l'automate du projet viennent de sa configuration (.XHW) : %M et %MW.");
    const auto* e = doc_->project.equipmentByName(equipment);
    if (!e) return fail("pas d'\xC3\xA9quipement " + equipment);
    if (!e->modbus()) return fail(e->name + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas de m\xC3\xA9moire Modbus");
    std::vector<hmi::MemRange> ranges;
    std::string reason;
    if (!zn::parseRanges(table, text, ranges, &reason)) return fail(std::string(zn::tableLabel(table)) + " : " + reason);
    const std::string name = e->name;
    if (!changeProject(name + " : zones " + std::string(zn::tableKey(table)), [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == name) {
                    if (!q.zones.declared) {
                        // Les autres tables : rien tant qu'on ne les dit pas.
                        q.zones = hmi::MemZones{};
                        q.zones.declared = true;
                    }
                    q.zones.of(table) = ranges;
                    q.zones.origin = "saisies \xC3\xA0 la main";
                }
        }))
        return false;
    say(name + " \xC2\xB7 " + std::string(zn::tableLabel(table)) + " : " + zn::rangesText(ranges) + " (Ctrl+Z revient en arri\xC3\xA8re).");
    return true;
}

bool HmiCommPane::applyZonePreset(const std::string& equipment, const std::string& key, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (equipment == kPlcKey) return fail("Les zones de l'automate du projet viennent de sa configuration (.XHW).");
    const auto* e = doc_->project.equipmentByName(equipment);
    if (!e) return fail("pas d'\xC3\xA9quipement " + equipment);
    hmi::MemZones z;
    if (key != "non") {
        const auto [bits, words] = plcMemory();
        std::string reason;
        if (!zn::applyPreset(doc_->project, *e, key, bits, words, z, &reason)) return fail("mod\xC3\xA8le : " + reason);
    }
    const std::string name = e->name;
    if (!changeProject(name + " : zones (" + key + ")", [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == name) q.zones = z;
        }))
        return false;
    say(name + " : " + (z.declared ? zn::summary(z) : std::string("zones non d\xC3\xA9" "clar\xC3\xA9" "es (pas de v\xC3\xA9rification)")) + ".");
    return true;
}

bool HmiCommPane::detectZones(const std::string& equipment, bool twin, bool ask, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    hmi::zones::Detector::Target target;
    std::string label;
    if (equipment == kPlcKey) {
        const auto& c = doc_->project.comm;
        if (!c.modbus()) return fail("L'automate du projet est le simulateur : ses zones viennent de sa configuration (.XHW).");
        target.host = c.host;
        target.port = c.port;
        target.unit = c.unit;
        target.timeoutMs = c.timeoutMs;
        label = "l'automate du projet";
    } else {
        const auto* e = doc_->project.equipmentByName(equipment);
        if (!e) return fail("pas d'\xC3\xA9quipement " + equipment);
        if (!e->modbus()) return fail(e->name + " est un \xC3\xA9quipement Ethernet TCP/IP : pas de m\xC3\xA9moire Modbus");
        const bool viaTwin = twin || e->simulated;
        if (viaTwin) {
            const int port = host() ? host()->simulatedPort(e->name) : 0;
            if (!port) return fail("L'esclave simul\xC3\xA9 de " + e->name + " n'est pas en marche (fiche : En marche).");
            target.host = "127.0.0.1";
            target.port = port;
            label = e->twinLabel();
        } else {
            target.host = e->host;
            target.port = e->port;
            label = e->name + " (" + e->host + ")";
        }
        target.unit = e->unit;
        target.timeoutMs = std::min(e->timeoutMs, 1500);
        twin = viaTwin;
    }
    if (detector_ && detector_->running()) detector_->stop();
    detector_ = std::make_unique<hmi::zones::Detector>();
    detector_->start(target);
    detectEquip_ = equipment;
    detectTwin_ = twin;
    detectAuto_ = !ask;
    detectShown_ = false;
    if (!ask) return true;
    say("D\xC3\xA9tecter les zones de " + label + " : lecture seule (fonctions 1 \xC3\xA0 4), jamais d'\xC3\xA9" "criture.");
    if (!hosts_.ask) return true;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiDetect";
    spec.title = "D\xC3\xA9tecter les zones de \xC2\xAB " + label + " \xC2\xBB";
    spec.live = [this] { return detectText(); };
    spec.ready = [this] {
        const auto r = detectReport();
        return r.has_value() && !r->failed && equipmentOf(detectEquip_) != nullptr;
    };
    spec.stopLabel = "Arr\xC3\xAAter";
    spec.onStop = [this] {
        if (detector_) detector_->stop();
    };
    spec.confirm = "Appliquer ces zones";
    spec.cancel = "Fermer";
    spec.note = "La d\xC3\xA9tection ne fait que LIRE (fonctions 1, 2, 3, 4) : elle n'\xC3\xA9" "crit jamais dans l'appareil. Appliquer : ses zones deviennent "
                "celles-ci (Ctrl+Z revient en arri\xC3\xA8re).";
    spec.width = 680.f;
    spec.height = 470.f;
    hosts_.ask(std::move(spec), [this](bool ok, const HmiAskDialog::Answer&) {
        if (ok) (void)applyDetected();
        else if (detector_ && detector_->running()) detector_->stop();
        detectShown_ = true;
    });
    return true;
}

bool HmiCommPane::detecting() const { return detector_ && detector_->running(); }

std::optional<hmi::zones::DetectReport> HmiCommPane::detectReport() const {
    if (!detector_ || detector_->running()) return std::nullopt;
    return detector_->report();
}

std::string HmiCommPane::detectText() const {
    if (!detector_) return "Aucune d\xC3\xA9tection.";
    const auto& t = detector_->target();
    std::string s = "Lecture de " + t.host + ":" + std::to_string(t.port) + ", esclave " + std::to_string(t.unit) + ".\n";
    if (detector_->running()) {
        const auto pr = detector_->progress();
        char line[160];
        std::snprintf(line, sizeof line, "En cours : %d %% \xC2\xB7 %d requ\xC3\xAAtes \xC2\xB7 %.1f s", static_cast<int>(std::lround(pr.fraction * 100)), pr.requests, pr.seconds);
        s += std::string(line) + "\n" + pr.line + "\n";
        return s;
    }
    const auto rep = detector_->report();
    if (!rep) return s + "Termin\xC3\xA9" "e.";
    if (rep->failed) return s + "\xC3\x89" "chec : " + rep->why;
    for (const MemTable tb : kMapOrder)
        for (const auto& d : rep->tables) {
            if (d.table != tb) continue;
            std::string line = std::string(zn::tableLabel(tb)) + " (" + std::string(zn::tableModicon(tb)) + ") : ";
            if (d.absent) line += "absente (la fonction est refus\xC3\xA9" "e)";
            else if (d.failed) line += "plus de r\xC3\xA9ponse";
            else if (d.ranges.empty()) line += "rien";
            else {
                std::string list;
                for (std::size_t k = 0; k < d.ranges.size() && k < 6; ++k)
                    list += (k ? ", " : "") + zn::modicon(tb, d.ranges[k].first) + " \xC3\xA0 " + zn::modicon(tb, d.ranges[k].last);
                if (d.ranges.size() > 6) list += " (+" + std::to_string(d.ranges.size() - 6) + ")";
                line += list;
            }
            if (!d.comment.empty() && !d.absent) line += " \xE2\x80\x94 " + d.comment;
            if (d.everywhere) line += " (il r\xC3\xA9pond partout : la fin de sa m\xC3\xA9moire n'est pas connue)";
            if (d.cut) line += " (arr\xC3\xAAt\xC3\xA9" "e : trop de requ\xC3\xAAtes)";
            s += line + "\n";
        }
    char tail[120];
    std::snprintf(tail, sizeof tail, "%d requ\xC3\xAAtes en %.1f s%s.", rep->requests, rep->seconds, rep->stopped ? " (arr\xC3\xAAt\xC3\xA9" "e avant la fin)" : "");
    return s + tail;
}

bool HmiCommPane::applyDetected(std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto rep = detectReport();
    if (!rep) return fail("aucune d\xC3\xA9tection termin\xC3\xA9" "e");
    if (rep->failed) return fail("d\xC3\xA9tection en \xC3\xA9" "chec : " + rep->why);
    const auto* e = equipmentOf(detectEquip_);
    if (!e) return fail("les zones de l'automate du projet viennent de sa configuration (.XHW)");
    const hmi::MemZones z = rep->zones("d\xC3\xA9tect\xC3\xA9" "es " + clockNow());
    const std::string name = e->name;
    if (!changeProject(name + " : zones d\xC3\xA9tect\xC3\xA9" "es", [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == name) q.zones = z;
        }))
        return false;
    say(name + " : " + zn::summary(z) + " (d\xC3\xA9tect\xC3\xA9" "es ; Ctrl+Z revient en arri\xC3\xA8re).");
    return true;
}

// ============================================================ carte memoire ===
hmi::zones::MemoryMap HmiCommPane::currentMap() const {
    const auto& p = doc_->project;
    if (mapEquip_ == kPlcKey) {
        const auto [bits, words] = plcMemory();
        return zn::buildPlcMap(p, currentPlan(), bits, words);
    }
    if (const auto* e = equipmentOf(mapEquip_)) return zn::buildMap(p, *e);
    return {};
}

hmi::MemZones HmiCommPane::mapShownZones(const hmi::Equipment* e) const {
    hmi::MemZones z;
    if (e) {
        if (e->zones.declared) return e->zones;
        std::string why;
        (void)zn::applyPreset(doc_->project, *e, "variables", 0, 0, z, &why);
        // Le scanner a lu autre chose (des zones non declarees : celles des variables).
        if (mapScanner_ && mapScanKey_ == mapEquip_ + "|" + (mapTwin_ ? "1" : "0")) {
            const auto s = mapScanner_->scanned();
            for (const auto t : hmi::kMemTables)
                for (const auto& r : s.of(t)) z.of(t).push_back(r);
            for (auto& v : z.tables) zn::normalize(v);
        }
        return z;
    }
    // L'automate du projet : ses variables, a la centaine.
    const auto m = currentMap();
    for (const auto& v : m.vars) {
        const std::uint32_t first = v.first / 100 * 100, last = std::min<std::uint32_t>(65535, (v.last / 100 + 1) * 100 - 1);
        z.of(v.table).push_back({first, last});
    }
    for (auto& v : z.tables) zn::normalize(v);
    return z;
}

void HmiCommPane::refreshMapTargets() {
    if (!mapTargetBox_) return;
    const auto& p = doc_->project;
    mapTargets_.clear();
    std::vector<ui::DropDown::Item> items;
    mapTargets_.push_back({kPlcKey, false});
    items.push_back({p.comm.modbus() ? "Automate du projet \xC2\xB7 " + p.comm.host : std::string("Automate du projet (le simulateur)"), kPlcKey, {}, true});
    for (const auto& e : p.equipments) {
        if (!e.modbus()) continue;
        if (!e.simulated) {
            mapTargets_.push_back({e.name, false});
            items.push_back({e.name + " \xC2\xB7 " + e.host + " (le vrai)", e.name, {}, true});
        }
        if (e.hasTwin()) {
            mapTargets_.push_back({e.name, true});
            items.push_back({e.simulated ? e.name + " \xC2\xB7 seulement simul\xC3\xA9" : e.twinLabel(), e.name + "|jumeau", {}, true});
        }
    }
    // La cible par defaut : l'equipement choisi (son jumeau si l'IHM lui parle), sinon le premier.
    if (mapEquip_.empty() || (mapEquip_ != kPlcKey && !equipmentOf(mapEquip_))) {
        mapEquip_.clear();
        const std::string want = !equipment_.empty() ? equipment_ : std::string{};
        for (const auto& [name, twin] : mapTargets_)
            if (mapEquip_.empty() && name == want) {
                mapEquip_ = name;
                mapTwin_ = twin;
            }
        if (mapEquip_.empty() && mapTargets_.size() > 1) {
            mapEquip_ = mapTargets_[1].first;
            mapTwin_ = mapTargets_[1].second;
        }
        if (mapEquip_.empty()) {
            mapEquip_ = kPlcKey;
            mapTwin_ = false;
        }
    }
    int selected = 0;
    for (std::size_t i = 0; i < mapTargets_.size(); ++i)
        if (mapTargets_[i].first == mapEquip_ && mapTargets_[i].second == mapTwin_) selected = static_cast<int>(i);
    if (selected == 0 && mapEquip_ != kPlcKey)
        for (std::size_t i = 0; i < mapTargets_.size(); ++i)
            if (mapTargets_[i].first == mapEquip_) {
                selected = static_cast<int>(i);
                mapTwin_ = mapTargets_[i].second;
                break;
            }
    const bool was = refreshing_;
    refreshing_ = true;
    mapTargetBox_->setItems(std::move(items));
    mapTargetBox_->setSelectedIndex(selected);
    refreshing_ = was;
}

void HmiCommPane::showMap(const std::string& equipment, bool twin) {
    if (equipment != kPlcKey) {
        const auto* e = equipmentOf(equipment);
        if (!e) return;
        mapEquip_ = e->name;
        mapTwin_ = twin || e->simulated;
        if (!e->hasTwin()) mapTwin_ = false;
    } else {
        mapEquip_ = kPlcKey;
        mapTwin_ = false;
    }
    // Un autre equipement : le scanner s'arrete (il lisait l'autre), la case choisie s'oublie.
    if (mapScanner_ && mapScanKey_ != mapEquip_ + "|" + (mapTwin_ ? "1" : "0")) {
        mapScanner_->stop();
        mapScanner_.reset();
        mapScanKey_.clear();
        map_->clearSelection();
    }
    refreshMapTargets();
    refreshMap(true);
    rebuildProperties();
}

bool HmiCommPane::setMapOption(const std::string& key, const std::string& raw) {
    auto o = map_->options();
    const std::string v = lower(trimmed(raw));
    if (key == "montrer") {
        o.show = v.find("prob") != std::string::npos ? 2 : v.find("activ") != std::string::npos && v.find("sans") != std::string::npos ? 3
                 : v.find("variable") != std::string::npos && v.find("tout") == std::string::npos ? 1 : 0;
        if (mapShowBox_) {
            const bool was = refreshing_;
            refreshing_ = true;
            mapShowBox_->setSelectedIndex(o.show);
            refreshing_ = was;
        }
    } else if (key == "valeurs") {
        o.radix = v.find("hex") != std::string::npos ? 2 : v.find("sign") != std::string::npos ? 1 : 0;
        o.perRow = o.radix == 2 ? 16 : 10;
        if (mapRadixBox_) {
            const bool was = refreshing_;
            refreshing_ = true;
            mapRadixBox_->setSelectedIndex(o.radix);
            refreshing_ = was;
        }
    } else if (key == "replier") {
        o.fold = yes(v);
        if (mapFoldBox_) mapFoldBox_->setState(o.fold ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    } else if (key == "chercher") {
        o.search = trimmed(raw);
        if (mapSearchBox_ && mapSearchBox_->text() != o.search) mapSearchBox_->setText(o.search);
    } else if (key == "par_ligne") {
        o.perRow = v == "16" ? 16 : 10;
    } else {
        say("option inconnue : " + key, true);
        return false;
    }
    map_->setOptions(o);
    return true;
}

bool HmiCommPane::startMapScan(std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    hmi::zones::Scanner::Target target;
    hmi::MemZones zones, fallback;
    std::string label;
    if (mapEquip_ == kPlcKey) {
        const auto& c = doc_->project.comm;
        if (!c.modbus()) return fail("L'automate du projet est le simulateur : sa m\xC3\xA9moire se voit dans Simulation (pas de scan Modbus).");
        target.host = c.host;
        target.port = c.port;
        target.unit = c.unit;
        target.timeoutMs = c.timeoutMs;
        zones = currentMap().zones;
        fallback = mapShownZones(nullptr);
        label = "l'automate du projet";
    } else {
        const auto* e = equipmentOf(mapEquip_);
        if (!e) return fail("choisissez un \xC3\xA9quipement (Carte de)");
        if (mapTwin_) {
            const int port = host() ? host()->simulatedPort(e->name) : 0;
            if (!port) return fail("L'esclave simul\xC3\xA9 de " + e->name + " n'est pas en marche.");
            target.host = "127.0.0.1";
            target.port = port;
            label = e->twinLabel();
        } else {
            target.host = e->host;
            target.port = e->port;
            label = e->name;
        }
        target.unit = e->unit;
        target.timeoutMs = std::min(e->timeoutMs, 1500);
        zones = e->zones;
        fallback = mapShownZones(e);
    }
    target.periodMs = mapTwin_ ? 500 : 1000;
    if (mapScanner_) mapScanner_->stop();
    mapScanner_ = std::make_unique<hmi::zones::Scanner>();
    mapScanner_->start(target, zones, fallback);
    mapScanKey_ = mapEquip_ + "|" + (mapTwin_ ? "1" : "0");
    if (!mapTwin_) say("Scanner " + label + " : ses zones lues en boucle, une passe par seconde (lecture seule) ; Arr\xC3\xAAter le scan quand c'est vu.");
    refreshMap(false);
    return true;
}

void HmiCommPane::stopMapScan() {
    if (!mapScanner_) return;
    mapScanner_->stop();
    const auto st = mapScanner_->state();
    say("Scan arr\xC3\xAAt\xC3\xA9 : " + std::to_string(st.passes) + " passe(s), " + std::to_string(mapScanner_->activeCells()) + " case(s) active(s). Ce qui a \xC3\xA9t\xC3\xA9 vu reste sur la carte.");
    refreshMap(false);
}

bool HmiCommPane::mapScanning() const { return mapScanner_ && mapScanner_->running(); }

void HmiCommPane::selectCell(MemTable table, std::uint32_t offset) {
    map_->select(table, offset);
    rebuildProperties();
}

std::optional<std::pair<MemTable, std::uint32_t>> HmiCommPane::selectedCell() const {
    const auto s = map_->selected();
    if (!s) return std::nullopt;
    return std::make_pair(s->table, s->offset);
}

void HmiCommPane::refreshMap(bool structure) {
    if (!map_) return;
    const auto* e = equipmentOf(mapEquip_);
    const std::string key = mapEquip_ + "|" + (mapTwin_ ? "1" : "0");
    if (structure) {
        auto m = currentMap();
        const auto shown = mapShownZones(e);
        map_->setMap(std::move(m), shown);
        // Les comportements du jumeau : un rond sur leurs cases.
        std::set<std::pair<int, std::uint32_t>> cells, forced;
        if (e && mapTwin_) {
            for (const auto& b : e->behaviors) {
                if (!b.enabled) continue;                  // lot 18 : un mouvement arrete ne se montre pas
                const auto fr = hmi::twin::freeRow(b.address, b.type);
                if (!fr) continue;
                std::string ty = b.type;
                for (auto& ch : ty) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                const std::uint32_t n = fr->boolean ? 1u : (ty == "REAL" || ty == "LREAL" || ty == "DINT" || ty == "UDINT" || ty == "DWORD" || ty == "TIME") ? 2u : 1u;
                for (std::uint32_t k = 0; k < n; ++k) cells.insert({static_cast<int>(fr->table), fr->offset + k});
            }
            // Lot 18 : les cases forcees.
            for (const auto& c : hmi::twin::forcedCells(e->forcings, e->wordOrder != "fort")) forced.insert({static_cast<int>(c.table), c.offset});
        }
        map_->setBehaviorCells(std::move(cells));
        map_->setForcedCells(std::move(forced));
        std::size_t errors = 0;
        for (const auto& c : map_->map().conflicts) errors += c.severity == 3 ? 1 : 0;
        errors += map_->map().outOfZone();
        tabs_->setTabBadge(TMap, errors ? std::to_string(errors) + " erreur" + (errors > 1 ? "s" : "") : std::string("nouveau"),
                           errors ? ui::Tone::Error : ui::Tone::Accent);
    }
    // Le direct : les cases (le scanner), les variables (la liaison de l'IHM).
    MemCellFn cells;
    int passes = 0;
    hmi::zones::Scanner* sc = mapScanner_ && mapScanKey_ == key ? mapScanner_.get() : nullptr;
    if (sc && (sc->running() || sc->state().passes > 0)) {
        cells = [sc](MemTable t, std::uint32_t o) { return sc->stat(t, o); };
        passes = sc->state().passes;
    }
    hmi::comm::Link* lk = nullptr;
    if (mapEquip_ == kPlcKey) {
        if (auto* ch = hosts_.comm ? hosts_.comm() : nullptr) lk = ch->link();
    } else if (e && host()) {
        // La liaison de l'IHM lit-elle ce qu'on montre (le jumeau, ou le vrai) ?
        if (host()->viaTwin(e->name) == mapTwin_) lk = host()->link(e->name);
    }
    std::vector<MemVarLive> vars;
    const auto& mv = map_->map().vars;
    vars.resize(mv.size());
    if (lk)
        for (std::size_t i = 0; i < mv.size(); ++i) {
            const auto& v = mv[i];
            std::string whyq;
            const auto q = lk->quality(v.name, &whyq);
            if (q == hmi::comm::Quality::Good || q == hmi::comm::Quality::Stale) {
                sim::Value raw;
                if (lk->read(v.name, raw)) {
                    const auto* var = doc_->project.variable(v.name);
                    vars[i].text = var && var->scaled() ? hmi::formatValue(eq::fromRegister(*var, raw)) : hmi::formatValue(raw);
                }
            }
            const auto a = lk->activity(v.name);
            vars[i].read = a.readAt >= 0 && a.now - a.readAt < 3.0;
            vars[i].written = a.writtenAt >= 0 && a.now - a.writtenAt < 10.0;
        }
    map_->setLive(cells, std::move(vars), passes);
    // La banniere.
    std::string banner;
    int tone = 0;
    if (sc && sc->running()) {
        const auto st = sc->state();
        char ms[32];
        std::snprintf(ms, sizeof ms, "%.0f ms", st.passMs);
        std::size_t freeCells = 0;
        const std::size_t active = sc->activeCells();
        (void)freeCells;
        banner = std::string(mapTwin_ ? "M\xC3\xA9moire de l'esclave simul\xC3\xA9, en direct" : "Scan en cours") + " \xC2\xB7 passe " + std::to_string(st.passes) + " (" + std::to_string(st.requestsPerPass)
                 + " requ\xC3\xAAte" + (st.requestsPerPass > 1 ? "s" : "") + ", " + ms + ") \xC2\xB7 " + std::to_string(active) + " case" + (active > 1 ? "s" : "") + " active"
                 + (active > 1 ? "s" : "") + " \xC2\xB7 lecture seule";
        if (!st.why.empty()) {
            banner += " \xC2\xB7 " + st.why;
            tone = 2;
        } else {
            tone = 1;
        }
    } else if (sc && sc->state().passes > 0) {
        banner = "Scan arr\xC3\xAAt\xC3\xA9 \xC2\xB7 " + std::to_string(sc->state().passes) + " passe(s) \xC2\xB7 " + std::to_string(sc->activeCells())
                 + " case(s) active(s) : ce qui a \xC3\xA9t\xC3\xA9 vu reste affich\xC3\xA9";
        tone = 4;
    } else if (mapEquip_ == kPlcKey) {
        banner = doc_->project.comm.modbus() ? std::string("L'automate du projet : ses variables au plan d'adressage ; \xC2\xAB Scanner l'\xC3\xA9quipement \xC2\xBB lit sa m\xC3\xA9moire en boucle")
                                             : std::string("L'automate du projet (le simulateur) : ses variables au plan d'adressage, ses zones de sa configuration");
    } else if (e) {
        banner = mapTwin_ ? "L'esclave simul\xC3\xA9 \xC2\xAB " + e->twinLabel() + " \xC2\xBB : " + twinStateText(*e)
                          : "\xC2\xAB Scanner l'\xC3\xA9quipement \xC2\xBB : ses zones lues en boucle (lecture seule), pour voir o\xC3\xB9 l'esclave a de l'activit\xC3\xA9";
        if (!e->zones.declared) banner += " \xC2\xB7 zones non d\xC3\xA9" "clar\xC3\xA9" "es (\xC2\xAB D\xC3\xA9tecter les zones \xC2\xBB, ou la fiche)";
        tone = mapTwin_ ? 4 : 0;
    }
    map_->setBanner(banner, tone);
}

bool HmiCommPane::createVariableHere(std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto sel = map_->selected();
    if (!sel) return fail("choisissez d'abord une case de la carte (un clic)");
    if (mapEquip_ == kPlcKey) return fail("Les variables de l'automate du projet se d\xC3\xA9" "clarent dans son programme (ou la table des adresses).");
    const auto* e = equipmentOf(mapEquip_);
    if (!e) return fail("choisissez un \xC3\xA9quipement (Carte de)");
    const bool bits = zn::isBits(sel->table);
    std::string type = bits ? "BOOL" : "INT";
    // Le type vu : un REAL vraisemblable sur deux mots.
    if (!bits && mapScanner_) {
        const auto a = mapScanner_->stat(sel->table, sel->offset);
        const auto b = mapScanner_->stat(sel->table, sel->offset + 1);
        if (a && b && a->seen && b->seen)
            for (const auto& [label, text] : zn::interpretations(a->value, b->value, e->wordOrder != "fort"))
                if (label.find("REAL") != std::string::npos && text.find("vraisemblable") != std::string::npos) type = "REAL";
    }
    const std::string address = zn::modicon(sel->table, sel->offset);
    if (hosts_.askBindAt) {
        hosts_.askBindAt(e->name, address, type);
        return true;
    }
    std::string name = "Mesure_" + address;
    for (int k = 2; doc_->project.variable(name); ++k) name = "Mesure_" + address + "_" + std::to_string(k);
    return bindVariable(name, e->name, address, type, why);
}

bool HmiCommPane::proposeFreeAddress(const std::string& variable, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* v = doc_->project.variable(variable);
    if (!v || !v->bound()) return fail("choisissez une variable IHM li\xC3\xA9" "e (sur la carte, ou Variables li\xC3\xA9" "es)");
    const auto* e = doc_->project.equipmentByName(v->equipment);
    if (!e) return fail("\xC3\xA9quipement inconnu : " + v->equipment);
    const auto m = zn::buildMap(doc_->project, *e);
    // Sa place : la table, le nombre de cases.
    MemTable table = MemTable::Holding;
    std::uint32_t first = 0, last = 0;
    bool found = false;
    for (const auto& x : m.vars)
        if (same(x.root, v->name)) {
            if (!found) {
                table = x.table;
                first = x.first;
                last = x.last;
                found = true;
            }
            first = std::min(first, x.first);
            last = std::max(last, x.last);
        }
    if (!found) return fail(v->name + " n'a pas de place lisible (son adresse)");
    const std::uint32_t size = last - first + 1;
    // Les cases prises par les autres.
    std::set<std::uint32_t> used;
    for (const auto& x : m.vars)
        if (x.table == table && !same(x.root, v->name))
            for (std::uint32_t o = x.first; o <= x.last && o - x.first < 70000; ++o) used.insert(o);
    std::vector<hmi::MemRange> ranges = e->zones.declared ? e->zones.of(table) : std::vector<hmi::MemRange>{{0, 65535}};
    const bool two = size >= 2 && !zn::isBits(table);
    for (const auto& r : ranges)
        for (std::uint64_t s = r.first; s + size - 1 <= r.last; ++s) {
            if (two && s % 2) continue;
            bool ok = true;
            for (std::uint32_t k = 0; k < size && ok; ++k) ok = !used.count(static_cast<std::uint32_t>(s) + k);
            if (!ok) continue;
            if (static_cast<std::uint32_t>(s) == first) return fail(v->name + " est d\xC3\xA9j\xC3\xA0 \xC3\xA0 une place libre (" + zn::modicon(table, first) + ")");
            return moveVariable(v->name, static_cast<int>(s) - static_cast<int>(first), why);
        }
    return fail("aucune place libre de " + std::to_string(size) + " case(s) dans les zones de " + e->name);
}

bool HmiCommPane::moveVariable(const std::string& variable, int delta, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* v = doc_->project.variable(variable);
    if (!v || !v->bound()) return fail(variable + " n'est pas une variable IHM li\xC3\xA9" "e (les variables de l'automate se d\xC3\xA9placent dans son programme)");
    if (delta == 0) return true;
    hmi::comm::Point pt;
    std::string reason;
    if (!eq::placeEquipmentAddress(v->address, eq::registerType(*v), pt, &reason)) return fail(v->name + " : " + reason);
    const long long target = static_cast<long long>(pt.offset) + delta;
    if (target < 0 || target > 65535) return fail("hors de la m\xC3\xA9moire Modbus (0 \xC3\xA0 65535)");
    const auto t = zn::tableOf(pt.area);
    std::string address = zn::modicon(t, static_cast<std::uint32_t>(target));
    if (pt.encoding == hmi::comm::Encoding::BitOfWord) address += "." + std::to_string(pt.bit);
    // L'ancienne adresse, avant la commande (apres, v la dit deja nouvelle).
    const std::string name = v->name, before = v->address;
    if (!setBoundField(name, "adresse", address, why)) return false;
    say(name + " : " + address + " (Ctrl+Z revient \xC3\xA0 " + before + ").");
    if (map_) map_->select(t, static_cast<std::uint32_t>(target));
    return true;
}

bool HmiCommPane::exportMap(std::string* where) {
    const auto m = map_->map();
    hmi::ExportTable table;
    table.title = "Carte m\xC3\xA9moire \xE2\x80\x94 " + (mapEquip_ == kPlcKey ? std::string("automate du projet") : mapEquip_);
    table.subtitle = doc_->project.config.name + " - " + hmi::wallStamp().substr(0, 19);
    table.headers = {"Variable", "Adresse", "Type", "Table", "De", "\xC3\x80", "Acc\xC3\xA8s", "\xC3\x89" "crite par", "Chevauchement"};
    for (std::size_t i = 0; i < m.vars.size(); ++i) {
        const auto& v = m.vars[i];
        std::string clash;
        for (const auto& c : m.conflicts)
            if (c.a == i || c.b == i) clash += (clash.empty() ? "" : " ; ") + zn::conflictText(m, c);
        if (v.outOfZone) clash += (clash.empty() ? "" : " ; ") + std::string("hors zone");
        table.rows.push_back({v.name, v.address, v.type, std::string(zn::tableLabel(v.table)), zn::modicon(v.table, v.first), zn::modicon(v.table, v.last),
                              v.written() ? "\xC3\xA9" "crite" : "lue", fewOf(v.writers, 4), clash});
    }
    table.notes = {"Zones : " + (m.zones.declared ? zn::summary(m.zones) : std::string("non d\xC3\xA9" "clar\xC3\xA9" "es")),
                   "Chevauchements : des lectures = normal ; une \xC3\xA9" "criture et une lecture = attention ; deux \xC3\xA9" "critures = erreur."};
    hmi::ExportRequest rq;
    rq.fileName = hmi::exportFileName("carte_memoire_" + hmi::wallStamp().substr(0, 10), hmi::ExportFormat::Excel);
    rq.format = "Excel";
    rq.source = "carte";
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(hmi::exportBytes(table, hmi::ExportFormat::Excel));
    rq.origin = "Configuration > \xC3\x89quipements > Carte m\xC3\xA9moire";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Carte m\xC3\xA9moire \xC3\xA9" "crite : " + path : "Export impossible" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

void HmiCommPane::rebuildMapProperties(std::vector<PG::Category>& cats) {
    const auto& m = map_->map();
    const auto* e = equipmentOf(mapEquip_);
    const auto sel = map_->selected();
    if (!sel) {
        PG::Category c;
        c.name = "Carte m\xC3\xA9moire";
        c.properties.push_back(prop("De", mapEquip_ == kPlcKey ? std::string("l'automate du projet") : (e ? (mapTwin_ ? e->twinLabel() : e->name) : std::string("\xE2\x80\x94")), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Zones", m.zones.declared ? zn::summary(m.zones) : std::string("non d\xC3\xA9" "clar\xC3\xA9" "es : pas de v\xC3\xA9rification"), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Variables", std::to_string(m.vars.size()) + " case(s) de variables", PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Chevauchements", std::to_string(m.count(3)) + " erreur(s), " + std::to_string(m.count(2)) + " attention, " + std::to_string(m.count(1))
                                                          + " partag\xC3\xA9(s) en lecture \xC2\xB7 " + std::to_string(m.outOfZone()) + " hors zone",
                                    PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Une case", "un clic : sa fiche ici ; double-clic : sa variable", PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        return;
    }
    const MemTable t = sel->table;
    const std::uint32_t o = sel->offset;
    const bool bits = zn::isBits(t);
    PG::Category c;
    c.name = "Case choisie";
    c.properties.push_back(prop(bits ? "Bit" : "Mot", zn::modicon(t, o) + "  (" + zn::schneider(t, o) + ")", PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Table", std::string(zn::tableLabel(t)) + " (" + std::string(zn::tableModicon(t)) + ")", PG::ValueType::ReadOnly));
    if (m.zones.declared) {
        std::string zone = "hors des zones : l'appareil la refusera";
        for (const auto& r : m.zones.of(t))
            if (o >= r.first && o <= r.last) zone = "zone " + zn::modicon(t, r.first) + " \xC3\xA0 " + zn::modicon(t, r.last);
        c.properties.push_back(prop("Zone", zone, PG::ValueType::ReadOnly));
    }
    std::optional<hmi::zones::CellStat> st;
    if (mapScanner_ && mapScanKey_ == mapEquip_ + "|" + (mapTwin_ ? "1" : "0")) st = mapScanner_->stat(t, o);
    if (st && st->seen) {
        c.properties.push_back(prop("Valeur", bits ? std::string(st->value ? "1" : "0") : std::to_string(st->value), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Activit\xC3\xA9", st->changes ? "a chang\xC3\xA9 " + std::to_string(st->changes) + " fois (de " + std::to_string(st->min) + " \xC3\xA0 " + std::to_string(st->max) + ")"
                                                              : st->value ? std::string("constante") : std::string("toujours 0 (une case morte ?)"),
                                    PG::ValueType::ReadOnly));
    } else {
        c.properties.push_back(prop("Valeur", mapEquip_ == kPlcKey || !mapTwin_ ? std::string("pas lue : \xC2\xAB Scanner l'\xC3\xA9quipement \xC2\xBB") : std::string("pas encore lue"),
                                    PG::ValueType::ReadOnly));
    }
    // Qui la lit, qui l'ecrit (le jumeau le sait).
    if (e && mapTwin_ && host())
        if (const auto bank = host()->twinBank(e->name)) {
            const auto a = bank->access(t, o);
            const double now = hmi::twin::now();
            c.properties.push_back(prop("Lue par", a.readAt < 0 ? std::string("personne") : a.reader + " (" + ageText(now - a.readAt) + ")", PG::ValueType::ReadOnly));
            c.properties.push_back(prop("\xC3\x89" "crite par", a.writeAt < 0 ? std::string("personne") : a.writer + " (" + ageText(now - a.writeAt) + ")", PG::ValueType::ReadOnly));
        }
    const auto at = m.at(t, o);
    if (at.empty()) {
        c.properties.push_back(prop("Variable IHM", "aucune ici \xE2\x80\x94 \xC2\xAB Cr\xC3\xA9" "er une variable IHM ici \xC2\xBB (barre du haut)", PG::ValueType::ReadOnly));
    } else {
        for (const auto i : at) {
            const auto& v = m.vars[i];
            c.properties.push_back(prop(v.name, v.type + (v.bit >= 0 ? " (bit " + std::to_string(v.bit) + ")" : std::string{}) + " \xC2\xB7 " + v.address + " \xC2\xB7 "
                                                     + (v.written() ? "\xC3\xA9" "crite par " + fewOf(v.writers, 2) : std::string("lue")),
                                        PG::ValueType::ReadOnly));
        }
        for (const auto& k : m.conflicts)
            if (k.table == t && o >= k.first && o <= k.last)
                c.properties.push_back(prop(k.severity == 3 ? "Erreur" : k.severity == 2 ? "Attention" : "Partag\xC3\xA9" "e", zn::conflictText(m, k), PG::ValueType::ReadOnly));
    }
    cats.push_back(std::move(c));
    // Vue comme : la valeur de plusieurs facons.
    if (st && st->seen && !bits) {
        PG::Category v;
        v.name = "Vue comme";
        std::optional<std::uint16_t> next;
        if (mapScanner_)
            if (const auto n = mapScanner_->stat(t, o + 1); n && n->seen) next = n->value;
        for (const auto& [label, text] : zn::interpretations(st->value, next, m.lowWordFirst)) v.properties.push_back(prop(label, text, PG::ValueType::ReadOnly));
        cats.push_back(std::move(v));
    }
    // Le jumeau : ecrire une valeur, le comportement de la case.
    if (e && mapTwin_) {
        const std::string name = e->name;
        PG::Category j;
        j.name = "L'esclave simul\xC3\xA9 (cette case)";
        const std::string address = zn::modicon(t, o);
        j.properties.push_back(prop("\xC3\x89" "crire une valeur", {}, PG::ValueType::Text,
                                    [this, name, t, o](std::string_view v) { return writeTwin(name, t, o, std::string(v)); },
                                    "Une valeur dans sa m\xC3\xA9moire, tout de suite : 1234, -5, 0x1F, 12.5 (un REAL sur deux mots), TRUE. L'IHM la lira comme d'un vrai. "
                                    "Une case forc\xC3\xA9" "e la refuse (exception 04)."));
        // Lot 18 : animer, forcer la case - les memes reglages que l'onglet Valeurs simulees.
        {
            std::string rowAddr = address;
            for (const auto i : m.at(t, o)) {
                rowAddr = m.vars[i].address;
                break;
            }
            if (const auto vr = valuesCtl_->row(name, rowAddr)) {
                const hmi::Behavior* vb = vr->behavior >= 0 ? &e->behaviors[static_cast<std::size_t>(vr->behavior)] : nullptr;
                const hmi::Forcing* vf = vr->forcing >= 0 ? &e->forcings[static_cast<std::size_t>(vr->forcing)] : nullptr;
                const std::string ra = vr->address;
                const bool rowLow = e->wordOrder != "fort";
                j.properties.push_back(prop("Animer", vb && vb->enabled ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                                            [this, name, ra](std::string_view v) { return animate(name, ra, v == "TRUE" || v == "true" || v == "1"); },
                                            "La case bouge toute seule (" + (vr->variable.empty() ? ra : vr->variable)
                                                + ") : le mouvement se r\xC3\xA8gle ici (Comportement) ou dans l'onglet Valeurs simul\xC3\xA9" "es (la barre)."));
                j.properties.push_back(prop("Forcer", vf ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                                            [this, name, ra, rowLow](std::string_view v) {
                                                if (!(v == "TRUE" || v == "true" || v == "1")) return force(name, ra, {});
                                                std::string raw = "0";
                                                if (host())
                                                    if (const auto bank = host()->twinBank(name))
                                                        if (const auto r1 = valuesCtl_->row(name, ra))
                                                            if (const auto now = hmi::twin::rowValue(*bank, *r1, rowLow)) raw = hmi::twin::numberText(*now);
                                                for (auto& ch : raw)
                                                    if (ch == ',') ch = '.';
                                                return force(name, ra, raw);
                                            },
                                            "Tenir la case \xC3\xA0 sa valeur brute : une \xC3\xA9" "criture (l'IHM, l'outil Modbus, \xC2\xAB \xC3\x89" "crire une valeur \xC2\xBB) y est refus\xC3\xA9" "e (exception 04), compt\xC3\xA9" "e."));
                j.properties.push_back(prop("\xC3\x80 la valeur brute", vf ? hmi::twin::numberText(vf->value) : std::string{}, PG::ValueType::Text,
                                            [this, name, ra](std::string_view v) { return force(name, ra, std::string(v)); },
                                            vr->boolean ? std::string("0 ou 1 ; vide : d\xC3\xA9" "forcer") : "Ce qui est dans le registre (" + vr->type + ") ; vide : d\xC3\xA9" "forcer."));
                if (vf && vr->scaled) j.properties.push_back(prop("Soit", num(vr->eng(vf->value)) + " (l'\xC3\xA9" "chelle de " + vr->variable + ")", PG::ValueType::ReadOnly));
                if (vf && !vf->since.empty()) j.properties.push_back(prop("Depuis", vf->since, PG::ValueType::ReadOnly));
                if (host())
                    if (const auto bank = host()->twinBank(name)) {
                        const auto cn = bank->counters();
                        j.properties.push_back(prop("\xC3\x89" "critures refus\xC3\xA9" "es", std::to_string(cn.forcedRefused) + (cn.lastRefused.empty() ? std::string{} : " (" + cn.lastRefused + ")"),
                                                    PG::ValueType::ReadOnly));
                    }
            }
        }
        const hmi::Behavior* b = nullptr;
        for (const auto& x : e->behaviors) {
            hmi::comm::Point pt;
            if (!eq::placeEquipmentAddress(x.address, eq::typeOfName(x.type), pt)) continue;
            const std::uint32_t n = pt.bits() ? 1u : static_cast<std::uint32_t>(std::max<int>(1, pt.size));
            if (zn::tableOf(pt.area) == t && o >= pt.offset && o < pt.offset + n) b = &x;
        }
        std::vector<std::string> kinds{"aucun"};
        for (const auto k : hmi::kBehaviorKinds) kinds.emplace_back(hmi::behaviorKindLabel(k));
        // Pas encore de comportement : il se pose au debut de la variable qui couvre la case.
        std::string baddr = b ? b->address : address;
        if (!b)
            for (const auto i : m.at(t, o))
                if (m.vars[i].bit < 0 && !zn::isBits(t)) {
                    baddr = zn::modicon(t, m.vars[i].first);
                    break;
                }
        const auto field = [this, name, baddr](const char* key) {
            return [this, name, baddr, key](std::string_view v) { return setBehavior(name, baddr, key, std::string(v)); };
        };
        j.properties.push_back(prop("Comportement", b ? std::string(hmi::behaviorKindLabel(b->kind)) : std::string("aucun"), PG::ValueType::Enum, field("genre"),
                                    "Ce que la case fait toute seule : constante, sinus, rampe, compteur, clignote, al\xC3\xA9" "atoire, recopie (d'une autre case, avec un retard), "
                                    "suit l'automate (une variable du simulateur), \xC3\xA9tapes (une liste de valeurs rejou\xC3\xA9" "e).",
                                    kinds));
        if (b) {
            std::vector<std::string> types{"INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "BOOL"};
            j.properties.push_back(prop("Adresse", b->address, PG::ValueType::ReadOnly));
            j.properties.push_back(prop("Type", b->type, PG::ValueType::Enum, field("type"), "REAL, DINT : deux mots.", types));
            if (const auto la = behaviorParam(b->kind, 'a'); !la.empty()) j.properties.push_back(prop(la, num(b->a), PG::ValueType::Real, field("a")));
            if (const auto lb = behaviorParam(b->kind, 'b'); !lb.empty()) j.properties.push_back(prop(lb, num(b->b), PG::ValueType::Real, field("b")));
            if (const auto lp = behaviorParam(b->kind, 'p'); !lp.empty()) j.properties.push_back(prop(lp, num(b->period), PG::ValueType::Real, field("periode")));
            if (const auto ld = behaviorParam(b->kind, 'd'); !ld.empty()) j.properties.push_back(prop(ld, num(b->delay), PG::ValueType::Real, field("retard")));
            if (const auto ls = behaviorParam(b->kind, 's'); !ls.empty()) j.properties.push_back(prop(ls, b->source, PG::ValueType::Text, field("source")));
            if (host())
                if (const auto bank = host()->twinBank(name))
                    if (const auto now = hmi::twin::valueAt(*bank, *b, e->wordOrder != "fort")) j.properties.push_back(prop("Maintenant", num(*now), PG::ValueType::ReadOnly));
            std::string whyb;
            if (!hmi::twin::validBehavior(*b, &whyb)) j.properties.push_back(prop("Refus\xC3\xA9", whyb, PG::ValueType::ReadOnly));
        }
        cats.push_back(std::move(j));
    }
    // Mes variables dans cette zone.
    {
        PG::Category z;
        z.name = "Mes variables dans cette table";
        std::size_t n = 0, onActive = 0, onDead = 0;
        for (const auto& v : m.vars) {
            if (v.table != t) continue;
            ++n;
            if (mapScanner_ && mapScanKey_ == mapEquip_ + "|" + (mapTwin_ ? "1" : "0")) {
                const auto s1 = mapScanner_->stat(t, v.first);
                if (s1 && s1->active()) ++onActive;
                else if (s1 && s1->seen) ++onDead;
            }
        }
        z.properties.push_back(prop("Variables", std::to_string(n), PG::ValueType::ReadOnly));
        if (mapScanner_) {
            z.properties.push_back(prop("Sur des cases actives", std::to_string(onActive), PG::ValueType::ReadOnly));
            z.properties.push_back(prop("Sur des cases mortes", std::to_string(onDead) + (onDead ? " (toujours 0 : la bonne adresse ?)" : std::string{}), PG::ValueType::ReadOnly));
        }
        std::size_t shown = 0;
        for (const auto& k : m.conflicts) {
            if (k.table != t || k.severity < 2 || shown >= 4) continue;
            z.properties.push_back(prop(k.severity == 3 ? "Erreur" : "Attention", zn::conflictText(m, k), PG::ValueType::ReadOnly));
            ++shown;
        }
        cats.push_back(std::move(z));
    }
}

// ================================================================ jumeaux ===
bool HmiCommPane::createTwin(const std::string& equipment, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* e = equipmentOf(equipment);
    if (!e) return fail(equipment == kPlcKey ? std::string("L'automate du projet a d\xC3\xA9j\xC3\xA0 son simulateur.") : "pas d'\xC3\xA9quipement " + equipment);
    if (!e->modbus()) return fail(e->name + " est un \xC3\xA9quipement Ethernet TCP/IP : pas d'esclave simul\xC3\xA9 (il se surveille par ping)");
    if (e->hasTwin()) return fail(e->name + " a d\xC3\xA9j\xC3\xA0 son esclave simul\xC3\xA9 (" + e->twinLabel() + ")");
    const std::string name = e->name;
    if (!changeProject(name + " : cloner en esclave simul\xC3\xA9", [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == name) {
                    q.twin = true;
                    q.twinRunning = true;
                    q.useTwin = true;
                    q.twinAuto = true;      // 1.9 : un clone tout neuf - le vrai ; l'esclave s'il ne repond pas
                }
        }))
        return false;
    selectEquipment(name);
    const auto* made = equipmentOf(name);
    say(name + " a son esclave simul\xC3\xA9 : \xC2\xAB " + (made ? made->twinLabel() : name) + " \xC2\xBB, sa ligne sous le vrai (au cadenas). L'IHM lit le vrai ; "
        "l'esclave s'il ne r\xC3\xA9pond pas (fiche : L'IHM lit) ; sur le poste d'exploitation, le vrai.");
    return true;
}

std::string HmiCommPane::addVirtualSlave(const std::string& rawName, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return std::string{};
    };
    const auto& p = doc_->project;
    std::string name = trimmed(rawName);
    if (name.empty()) {
        for (int i = 1; i < 10000 && name.empty(); ++i)
            if (const std::string candidate = "Esclave virtuel " + std::to_string(i); !p.equipmentByName(candidate)) name = candidate;
    } else if (p.equipmentByName(name)) {
        return fail(name + " : ce nom est d\xC3\xA9j\xC3\xA0 pris");
    }
    // Son adresse : une libre dans le premier port simule (sinon dans 10.10.100.0 / 24),
    // qu'aucun equipement ni jumeau n'a deja.
    std::string address = "10.10.100.10";
    {
        std::uint32_t ip = 0;
        int prefix = 24;
        if (!p.simPorts.empty() && eq::parseIpv4(p.simPorts.front().ip, ip)) prefix = p.simPorts.front().prefix;
        else (void)eq::parseIpv4("10.10.100.1", ip);
        std::vector<std::uint32_t> taken{ip};
        for (const auto& x : p.simPorts)
            if (std::uint32_t a = 0; eq::parseIpv4(x.ip, a)) taken.push_back(a);
        for (const auto& e : p.equipments) {
            if (std::uint32_t a = 0; eq::parseIpv4(e.host, a)) taken.push_back(a);
            if (std::uint32_t a = 0; eq::parseIpv4(e.twinAddress(), a)) taken.push_back(a);
        }
        if (const auto got = eq::suggestAddress(ip, prefix, taken)) address = eq::ipv4Text(got);
    }
    hmi::Equipment e;
    e.name = name;
    e.type = hmi::EquipmentType::ModbusTcp;
    e.host = address;
    e.port = 502;
    e.simulated = true;
    e.twinRunning = true;
    e.zones.declared = true;
    e.zones.of(MemTable::Holding) = {{0, 99}};
    e.zones.of(MemTable::InputRegisters) = {{0, 99}};
    e.zones.of(MemTable::Coils) = {{0, 99}};
    e.zones.of(MemTable::DiscreteInputs) = {{0, 99}};
    e.zones.origin = "mod\xC3\xA8le : 100 de chaque (\xC3\xA0 changer)";
    e.description = "esclave virtuel (seulement simul\xC3\xA9)";
    if (!changeProject("Ajouter l'esclave virtuel " + name, [&](hmi::Project& x) {
            e.id = x.allocate();
            x.equipments.push_back(e);
        }))
        return fail("impossible d'ajouter " + name);
    selectEquipment(name);
    say(name + " ajout\xC3\xA9 : un esclave Modbus virtuel (" + address + ", 100 cases de chaque table) - ses zones, ses comportements se r\xC3\xA8glent dans sa fiche et la carte m\xC3\xA9moire.");
    return name;
}

void HmiCommPane::runTwins(bool on) {
    std::size_t n = 0;
    for (const auto& e : doc_->project.equipments) n += e.hasTwin() && e.twinRunning != on ? 1 : 0;
    if (!n) {
        say(on ? "Les esclaves simul\xC3\xA9s sont d\xC3\xA9j\xC3\xA0 en marche." : "Les esclaves simul\xC3\xA9s sont d\xC3\xA9j\xC3\xA0 arr\xC3\xAAt\xC3\xA9s.");
        return;
    }
    (void)changeProject(on ? "D\xC3\xA9marrer les esclaves simul\xC3\xA9s" : "Arr\xC3\xAAter les esclaves simul\xC3\xA9s", [&](hmi::Project& x) {
        for (auto& q : x.equipments)
            if (q.hasTwin()) q.twinRunning = on;
    });
    say(std::to_string(n) + " esclave(s) simul\xC3\xA9(s) " + (on ? "d\xC3\xA9marr\xC3\xA9(s)" : "arr\xC3\xAAt\xC3\xA9(s) : ils ne r\xC3\xA9pondent plus (l'IHM les voit injoignables)") + ".");
}

bool HmiCommPane::restartTwin(const std::string& equipment, std::string* why) {
    const auto* e = equipmentOf(equipment);
    if (!e || !e->hasTwin() || !host()) {
        const std::string m = equipment + " n'a pas d'esclave simul\xC3\xA9 en marche";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    host()->restartTwin(e->name);
    say(e->twinLabel() + " : sa m\xC3\xA9moire repart de " + std::string(hmi::twinStartLabel(e->twinStart)) + ".");
    return true;
}

bool HmiCommPane::writeTwin(const std::string& equipment, MemTable table, std::uint32_t offset, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* e = equipmentOf(equipment);
    if (!e || !e->hasTwin()) return fail(equipment + " n'a pas d'esclave simul\xC3\xA9");
    auto bank = host() ? host()->twinBank(e->name) : nullptr;
    if (!bank) return fail("L'esclave simul\xC3\xA9 de " + e->name + " n'est pas en marche.");
    const std::string v = trimmed(raw);
    if (v.empty()) return true;
    std::vector<std::uint16_t> words;
    if (zn::isBits(table)) {
        const std::string l = lower(v);
        words.push_back(l == "1" || l == "true" || l == "vrai" || l == "on" ? 1 : 0);
    } else if (v.find('.') != std::string::npos || v.find('e') != std::string::npos || v.find('E') != std::string::npos) {
        char* end = nullptr;
        const double d = std::strtod(v.c_str(), &end);
        if (end && *end) return fail("\xC2\xAB " + v + " \xC2\xBB : un nombre (1234, -5, 0x1F, 12.5)");
        const float f = static_cast<float>(d);
        std::uint32_t bitsOf = 0;
        std::memcpy(&bitsOf, &f, sizeof bitsOf);
        const auto lo = static_cast<std::uint16_t>(bitsOf & 0xFFFF), hi = static_cast<std::uint16_t>(bitsOf >> 16);
        if (e->wordOrder != "fort") words = {lo, hi};
        else words = {hi, lo};
    } else {
        char* end = nullptr;
        const long long n = std::strtoll(v.c_str(), &end, 0);
        if (end && *end) return fail("\xC2\xAB " + v + " \xC2\xBB : un nombre (1234, -5, 0x1F, 12.5)");
        if (n < -32768 || n > 65535) return fail(v + " ne tient pas dans un mot (-32768 \xC3\xA0 65535)");
        words.push_back(static_cast<std::uint16_t>(n));
    }
    if (bank->write(table, offset, words, "\xC3\x89" "crire une valeur") == 4) {
        // Lot 18 : une case forcee ne s'ecrit pas (comme pour l'IHM et l'outil Modbus : exception 04).
        refreshMap(false);
        rebuildProperties();
        return fail(e->twinLabel() + " \xC2\xB7 " + zn::modicon(table, offset) + " est forc\xC3\xA9" "e : \xC3\xA9" "criture refus\xC3\xA9" "e (exception 04, compt\xC3\xA9" "e). D\xC3\xA9" "forcez-la d'abord.");
    }
    say(e->twinLabel() + " \xC2\xB7 " + zn::modicon(table, offset) + " = " + v + (words.size() > 1 ? " (REAL, deux mots)" : std::string{}) + ".");
    refreshMap(false);
    rebuildProperties();
    return true;
}

bool HmiCommPane::setBehavior(const std::string& equipment, const std::string& rawAddress, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* e = equipmentOf(equipment);
    if (!e || !e->hasTwin()) return fail(equipment + " n'a pas d'esclave simul\xC3\xA9");
    const std::string address = trimmed(rawAddress);
    const std::string v = trimmed(raw);
    std::vector<hmi::Behavior> list = e->behaviors;
    auto it = std::find_if(list.begin(), list.end(), [&](const hmi::Behavior& b) { return same(b.address, address); });
    if (key == "genre" && (lower(v) == "aucun" || v.empty())) {
        if (it == list.end()) return true;
        list.erase(it);
    } else {
        if (it == list.end()) {
            hmi::Behavior b;
            b.address = address;
            hmi::comm::Point pt;
            const bool placed = eq::placeEquipmentAddress(address, eq::typeOfName("INT"), pt);
            b.type = placed && pt.bits() ? "BOOL" : "INT";
            // Le type de la variable qui commence la (un REAL : deux mots).
            if (placed && !pt.bits())
                for (const auto& mv : zn::buildMap(doc_->project, *e).vars)
                    if (mv.table == zn::tableOf(pt.area) && mv.first == pt.offset && mv.bit < 0) {
                        static const char* ok[] = {"INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL"};
                        for (const char* t : ok)
                            if (mv.type == t) b.type = mv.type;
                    }
            if (b.type == "BOOL") b.b = 1;
            list.push_back(b);
            it = list.end() - 1;
        }
        auto& b = *it;
        const auto number = [&](double& out, const char* label) {
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            if (v.empty() || (end && *end) || !std::isfinite(d)) return fail(std::string(label) + " : un nombre");
            out = d;
            return true;
        };
        if (key == "genre") {
            const auto k = hmi::behaviorKindFrom(v);
            if (!k) return fail("comportement \xC2\xAB " + v + " \xC2\xBB inconnu");
            b.kind = *k;
            // Des reglages de depart qui disent quelque chose.
            if (b.kind == hmi::BehaviorKind::Sine || b.kind == hmi::BehaviorKind::Random) {
                if (b.b <= b.a) b.b = b.a + 100;
            } else if (b.kind == hmi::BehaviorKind::Counter) {
                b.b = 1;
                b.period = 1;
            } else if (b.kind == hmi::BehaviorKind::Blink) {
                b.period = 2;
            }
        } else if (key == "type") {
            const std::string t = lower(v);
            std::string u;
            for (const char ch : t) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            b.type = u;
        } else if (key == "a") {
            if (!number(b.a, "valeur")) return false;
        } else if (key == "b") {
            if (!number(b.b, "valeur")) return false;
        } else if (key == "periode") {
            if (!number(b.period, "p\xC3\xA9riode")) return false;
        } else if (key == "retard") {
            if (!number(b.delay, "retard")) return false;
        } else if (key == "source") {
            b.source = v;
        } else {
            return fail("r\xC3\xA9glage inconnu : " + key);
        }
        std::string reason;
        if (!hmi::twin::validBehavior(b, &reason)) return fail("comportement de " + address + " : " + reason);
    }
    const std::string name = e->name;
    if (!changeProject(name + " : comportement de " + address, [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == name) q.behaviors = list;
        }))
        return false;
    refreshMap(true);
    return true;
}

bool HmiCommPane::saveTwinMemory(const std::string& equipment, std::string* why) {
    const auto* e = equipmentOf(equipment);
    auto bank = e && host() ? host()->twinBank(e->name) : nullptr;
    if (!bank) {
        const std::string m = "l'esclave simul\xC3\xA9 de " + equipment + " n'est pas en marche";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    const auto snap = bank->snapshot();
    const std::string name = e->name;
    if (!changeProject(name + " : garder la m\xC3\xA9moire de l'esclave simul\xC3\xA9", [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == name) {
                    q.twinMemory = snap;
                    q.twinStart = hmi::TwinStart::Saved;
                }
        }))
        return false;
    std::size_t words = 0;
    for (const auto& s : snap) words += s.values.size();
    say(e->twinLabel() + " : sa m\xC3\xA9moire est gard\xC3\xA9" "e (" + std::to_string(words) + " case(s)) ; au d\xC3\xA9marrage, il repartira d'elle.");
    return true;
}

bool HmiCommPane::exportTwinMemory(const std::string& equipment, std::string* where) {
    const auto* e = equipmentOf(equipment);
    auto bank = e && host() ? host()->twinBank(e->name) : nullptr;
    if (!bank) {
        say("l'esclave simul\xC3\xA9 de " + equipment + " n'est pas en marche", true);
        return false;
    }
    const std::string text = hmi::twin::exportCsv(*bank);
    hmi::ExportRequest rq;
    rq.fileName = "memoire_" + e->name + "_" + hmi::wallStamp().substr(0, 10) + ".csv";
    for (auto& ch : rq.fileName)
        if (ch == ' ' || ch == '/' || ch == '\\') ch = '_';
    rq.format = "CSV";
    rq.source = "jumeau";
    rq.rows = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    rq.data = std::make_shared<const hmi::Bytes>(text.begin(), text.end());
    rq.origin = "Configuration > \xC3\x89quipements > Carte m\xC3\xA9moire";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "M\xC3\xA9moire de l'esclave simul\xC3\xA9 export\xC3\xA9" "e : " + path : "Export impossible" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

bool HmiCommPane::importTwinMemory(const std::string& equipment, const std::string& csv, std::string* why) {
    const auto* e = equipmentOf(equipment);
    auto bank = e && host() ? host()->twinBank(e->name) : nullptr;
    if (!bank) {
        const std::string m = "l'esclave simul\xC3\xA9 de " + equipment + " n'est pas en marche";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    std::string reason;
    std::size_t count = 0;
    if (!hmi::twin::importCsv(*bank, csv, &reason, &count)) {
        say("Importer : " + reason, true);
        if (why) *why = reason;
        return false;
    }
    say(e->twinLabel() + " : " + std::to_string(count) + " case(s) import\xC3\xA9" "e(s) dans sa m\xC3\xA9moire.");
    refreshMap(false);
    return true;
}

// ========================================================== reseau simule ===
std::string HmiCommPane::addSimPort(const std::string& rawIp, int prefix, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return std::string{};
    };
    const auto& p = doc_->project;
    std::string ip = trimmed(rawIp);
    if (prefix < 8 || prefix > 30) return fail("masque : de /8 \xC3\xA0 /30");
    if (ip.empty()) {
        for (int k = 0; k < 250 && ip.empty(); ++k) {
            const std::string candidate = "10.10." + std::to_string(k) + ".1";
            std::uint32_t c = 0;
            (void)eq::parseIpv4(candidate, c);
            bool taken = false;
            for (const auto& x : p.simPorts)
                if (std::uint32_t a = 0; eq::parseIpv4(x.ip, a) && eq::sameNetwork(a, c, std::min(x.prefix, prefix))) taken = true;
            if (!taken) ip = candidate;
        }
    }
    std::uint32_t addr = 0;
    if (!eq::parseIpv4(ip, addr)) return fail("adresse \xC2\xAB " + ip + " \xC2\xBB : quatre nombres de 0 \xC3\xA0 255");
    std::string name;
    for (int i = 1; i < 1000 && name.empty(); ++i) {
        const std::string candidate = "Port simul\xC3\xA9 " + std::to_string(i);
        if (std::none_of(p.simPorts.begin(), p.simPorts.end(), [&](const hmi::SimPort& x) { return x.name == candidate; })) name = candidate;
    }
    hmi::SimPort sp;
    sp.name = name;
    sp.ip = eq::ipv4Text(addr);
    sp.prefix = prefix;
    if (!changeProject("Ajouter " + name, [&](hmi::Project& x) { x.simPorts.push_back(sp); })) return fail("impossible d'ajouter " + name);
    simPort_ = name;
    say(name + " : " + sp.ip + " / " + std::to_string(prefix) + " (le PC simul\xC3\xA9 ; rien ne change sur le vrai PC).");
    return name;
}

bool HmiCommPane::copyPcNetwork(std::string* why) {
    auto* h = host();
    if (!h || !h->adaptersKnown()) {
        const std::string m = "les ports du PC ne sont pas encore lus (Actualiser)";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    const auto& p = doc_->project;
    std::vector<hmi::SimPort> added;
    int next = 1;
    const auto nameFree = [&](const std::string& n) {
        return std::none_of(p.simPorts.begin(), p.simPorts.end(), [&](const hmi::SimPort& x) { return x.name == n; })
               && std::none_of(added.begin(), added.end(), [&](const hmi::SimPort& x) { return x.name == n; });
    };
    for (const auto& a : h->adapters()) {
        if (!a.ip() || a.automaticAddress() || loopbackHost(eq::ipv4Text(a.ip()))) continue;
        bool known = false;
        for (const auto& x : p.simPorts)
            if (std::uint32_t xi = 0; eq::parseIpv4(x.ip, xi) && eq::sameNetwork(xi, a.ip(), std::min(x.prefix, a.prefix()))) known = true;
        if (known) continue;
        hmi::SimPort sp;
        while (!nameFree("Port simul\xC3\xA9 " + std::to_string(next))) ++next;
        sp.name = "Port simul\xC3\xA9 " + std::to_string(next++);
        sp.ip = eq::ipv4Text(a.ip());
        sp.prefix = a.prefix();
        sp.copyOf = a.name;
        added.push_back(sp);
    }
    if (added.empty()) {
        say("Rien \xC3\xA0 copier : chaque r\xC3\xA9seau du PC a d\xC3\xA9j\xC3\xA0 son port simul\xC3\xA9.");
        return true;
    }
    if (!changeProject("Copier le r\xC3\xA9seau du PC (" + std::to_string(added.size()) + " port(s))", [&](hmi::Project& x) {
            for (const auto& sp : added) x.simPorts.push_back(sp);
        }))
        return false;
    say(std::to_string(added.size()) + " port(s) simul\xC3\xA9(s) : les m\xC3\xAAmes r\xC3\xA9seaux que le PC ; les esclaves simul\xC3\xA9s gardent l'adresse de leur appareil.");
    return true;
}

bool HmiCommPane::removeSimPort(const std::string& name, std::string* why) {
    const auto& p = doc_->project;
    if (std::none_of(p.simPorts.begin(), p.simPorts.end(), [&](const hmi::SimPort& x) { return x.name == name; })) {
        const std::string m = "pas de port simul\xC3\xA9 " + name;
        say(m, true);
        if (why) *why = m;
        return false;
    }
    if (!changeProject("Retirer " + name, [&](hmi::Project& x) { std::erase_if(x.simPorts, [&](const hmi::SimPort& q) { return q.name == name; }); })) return false;
    if (simPort_ == name) simPort_.clear();
    say(name + " retir\xC3\xA9 (Ctrl+Z le rend).");
    return true;
}

bool HmiCommPane::setSimPortField(const std::string& name, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& p = doc_->project;
    const auto it = std::find_if(p.simPorts.begin(), p.simPorts.end(), [&](const hmi::SimPort& x) { return x.name == name; });
    if (it == p.simPorts.end()) return fail("pas de port simul\xC3\xA9 " + name);
    hmi::SimPort next = *it;
    const std::string v = trimmed(raw);
    if (key == "nom") {
        if (v.empty()) return fail("un port a un nom");
        if (v != name && std::any_of(p.simPorts.begin(), p.simPorts.end(), [&](const hmi::SimPort& x) { return x.name == v; })) return fail(v + " : ce nom est d\xC3\xA9j\xC3\xA0 pris");
        next.name = v;
    } else if (key == "ip") {
        std::uint32_t ip = 0;
        if (!eq::parseIpv4(v, ip)) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : quatre nombres de 0 \xC3\xA0 255");
        next.ip = eq::ipv4Text(ip);
    } else if (key == "masque") {
        int pre = 24;
        if (!eq::parseMask(v, pre)) return fail("masque \xC2\xAB " + v + " \xC2\xBB : 255.255.255.0, ou /24");
        next.prefix = pre;
    } else {
        return fail("champ inconnu : " + key);
    }
    if (!changeProject(name + " : " + key, [&](hmi::Project& x) {
            for (auto& q : x.simPorts)
                if (q.name == name) q = next;
        }))
        return false;
    if (simPort_ == name) simPort_ = next.name;
    rebuildProperties();
    return true;
}

void HmiCommPane::selectSimPort(const std::string& name) {
    simPort_ = name;
    portSide_ = true;
    refreshDiagram();
    rebuildProperties();
}

void HmiCommPane::setNetworkView(bool simulated) {
    if (simView_ == simulated) return;
    simView_ = simulated;
    portSide_ = false;
    refreshDiagram();
    rebuildProperties();
    tools_->invalidate();
    onLayout();
}

void HmiCommPane::rebuildSimPortProperties(std::vector<PG::Category>& cats) {
    const auto& p = doc_->project;
    const auto it = std::find_if(p.simPorts.begin(), p.simPorts.end(), [&](const hmi::SimPort& x) { return x.name == simPort_; });
    PG::Category c;
    c.name = "Port simul\xC3\xA9";
    if (it == p.simPorts.end()) {
        c.properties.push_back(prop("Port", "un clic sur un port simul\xC3\xA9 du sch\xC3\xA9ma", PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Ports simul\xC3\xA9s", std::to_string(p.simPorts.size()), PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        return;
    }
    const std::string name = it->name;
    const auto field = [this, name](const char* key) {
        return [this, name, key](std::string_view v) { return setSimPortField(simPort_.empty() ? name : simPort_, key, std::string(v)); };
    };
    c.properties.push_back(prop("Nom", it->name, PG::ValueType::Text, field("nom")));
    c.properties.push_back(prop("Adresse IP", it->ip, PG::ValueType::Text, field("ip"), "L'adresse du PC simul\xC3\xA9 sur ce port (rien ne change sur le vrai PC)."));
    c.properties.push_back(prop("Masque", eq::maskText(it->prefix) + " (/" + std::to_string(it->prefix) + ")", PG::ValueType::Text, field("masque")));
    c.properties.push_back(prop("Origine", it->copyOf.empty() ? std::string("cr\xC3\xA9\xC3\xA9 \xC3\xA0 la main") : "copie du port " + it->copyOf, PG::ValueType::ReadOnly));
    std::vector<std::string> slaves;
    std::uint32_t pip = 0;
    (void)eq::parseIpv4(it->ip, pip);
    for (const auto& e : p.equipments)
        if (std::uint32_t tip = 0; e.hasTwin() && eq::parseIpv4(e.twinAddress(), tip) && eq::sameNetwork(tip, pip, it->prefix)) slaves.push_back(e.twinLabel());
    c.properties.push_back(prop("Esclaves virtuels", slaves.empty() ? std::string("aucun") : fewOf(slaves, 4), PG::ValueType::ReadOnly));
    cats.push_back(std::move(c));
}

void HmiCommPane::refreshSimDiagram(NetDiagram& d) {
    const auto& p = doc_->project;
    auto* h = host();
    d.simulated = true;
    d.toggle = true;
    d.toggleNote = "le r\xC3\xA9seau simul\xC3\xA9 : les esclaves simul\xC3\xA9s ; rien ne sort sur le vrai r\xC3\xA9seau (sauf \xC2\xAB Visible sur le vrai r\xC3\xA9seau \xC2\xBB)";
    d.computer = h ? h->computerName() : std::string("Ce PC");
    d.system = "en simulation (le PC simul\xC3\xA9)";
    struct Net {
        std::uint32_t ip{0};
        int           prefix{24};
        int           port{-1};
    };
    std::vector<Net> nets;
    for (std::size_t i = 0; i < p.simPorts.size(); ++i) {
        const auto& sp = p.simPorts[i];
        NetDiagram::Port port;
        port.key = sp.name;
        port.number = static_cast<int>(i) + 1;
        port.title = sp.name;
        port.card = sp.copyOf.empty() ? std::string("cr\xC3\xA9\xC3\xA9 \xC3\xA0 la main") : "copie de " + sp.copyOf;
        port.link = "branch\xC3\xA9 (simul\xC3\xA9)";
        port.up = true;
        port.ip = sp.ip;
        port.prefix = std::to_string(sp.prefix);
        port.selected = portSide_ && simPort_ == sp.name;
        d.ports.push_back(std::move(port));
        std::uint32_t ip = 0;
        if (eq::parseIpv4(sp.ip, ip)) nets.push_back({ip, sp.prefix, static_cast<int>(i)});
    }
    std::size_t running = 0, twins = 0, only = 0;
    std::map<int, std::vector<std::size_t>> byNet;
    std::vector<NetDiagram::Equip> cards;
    std::vector<std::pair<std::uint32_t, std::string>> outside;
    for (const auto& e : p.equipments) {
        if (!e.hasTwin()) continue;
        if (e.simulated) ++only;
        else ++twins;
        running += e.twinRunning && e.enabled ? 1 : 0;
        NetDiagram::Equip c;
        c.key = e.name;
        c.name = e.twinLabel();
        c.tag = e.simulated ? std::string("seulement simul\xC3\xA9") : "esclave simul\xC3\xA9 de " + e.name;
        c.line1 = e.twinAddress() + ":" + std::to_string(e.twinPort) + " \xC2\xB7 Modbus TCP/IP \xC2\xB7 esclave " + std::to_string(e.unit);
        c.line2 = twinStateText(e);
        c.tone = !e.enabled || !e.twinRunning ? 5 : !e.twinResponds || e.twinException ? 2 : 4;
        c.selected = !portSide_ && same(equipment_, e.name);
        c.disabled = !e.enabled || !e.twinRunning;
        c.tip = c.name + " : " + c.line2;
        std::uint32_t ip = 0;
        int net = -1;
        if (eq::parseIpv4(e.twinAddress(), ip))
            for (std::size_t k = 0; k < nets.size() && net < 0; ++k)
                if (eq::sameNetwork(ip, nets[k].ip, nets[k].prefix)) net = static_cast<int>(k);
        const std::size_t index = cards.size();
        if (net >= 0) byNet[net].push_back(index);
        else outside.push_back({ip, e.name});
        c.net = net;          // provisoire : l'indice dans nets
        cards.push_back(std::move(c));
    }
    std::map<int, int> netOf;
    for (const auto& [k, list] : byNet) {
        NetDiagram::Net n;
        n.port = nets[static_cast<std::size_t>(k)].port;
        n.label = "R\xC3\xA9seau simul\xC3\xA9 " + eq::networkText(nets[static_cast<std::size_t>(k)].ip, nets[static_cast<std::size_t>(k)].prefix) + " \xC2\xB7 "
                  + std::to_string(list.size()) + " esclave" + (list.size() > 1 ? "s" : "");
        netOf[k] = static_cast<int>(d.nets.size());
        if (d.ports[static_cast<std::size_t>(n.port)].net < 0) d.ports[static_cast<std::size_t>(n.port)].net = static_cast<int>(d.nets.size());
        d.nets.push_back(std::move(n));
    }
    gives_.clear();
    for (auto& c : cards) {
        const int k = c.net;
        c.net = k >= 0 ? netOf[k] : -1;
        if (k < 0) {
            NetDiagram::Outside o;
            o.equip = c.key;
            o.title = c.name;
            o.line = c.line1;
            std::uint32_t ip = 0;
            const auto* e = p.equipmentByName(c.key);
            if (e && eq::parseIpv4(e->twinAddress(), ip)) {
                const int pre = (ip & 0xFFF00000u) == 0xAC100000u ? 16 : 24;
                o.text = "Aucun port du PC simul\xC3\xA9 n'est dans le r\xC3\xA9seau " + eq::networkText(ip, pre) + " : l'IHM simul\xC3\xA9" "e ne le joint pas.";
                std::vector<std::uint32_t> taken{ip};
                const std::uint32_t g = eq::suggestAddress(ip, pre, taken);
                if (g) {
                    o.give = "Ajouter au PC simul\xC3\xA9 un port " + eq::ipv4Text(g) + " / " + std::to_string(pre);
                    gives_.push_back({{}, eq::ipv4Text(g), pre});
                }
            } else {
                o.text = "Son adresse n'est pas une adresse IP : changez-la dans sa fiche.";
            }
            if (o.give.empty()) gives_.push_back({});
            o.edit = "Modifier l'adresse de " + c.name;
            d.outside.push_back(std::move(o));
        }
        d.equips.push_back(std::move(c));
    }
    if (!d.outside.empty()) d.outsideTitle = "Hors r\xC3\xA9seau simul\xC3\xA9 : " + std::to_string(d.outside.size()) + " esclave" + (d.outside.size() > 1 ? "s" : "");
    d.portsSummary.push_back(std::to_string(p.simPorts.size()) + " port" + (p.simPorts.size() > 1 ? "s" : "") + " simul\xC3\xA9" + (p.simPorts.size() > 1 ? "s" : ""));
    d.portsSummary.push_back(std::to_string(twins + only) + " esclave(s) virtuel(s)");
    d.portsSummary.push_back(std::to_string(twins) + " esclave(s) simul\xC3\xA9(s), " + std::to_string(only) + " seulement simul\xC3\xA9(s)");
    d.portsSummary.push_back(std::to_string(running) + " en marche");
    d.hint = "Tirer un esclave sur un port simul\xC3\xA9 : il change d'adresse (ou le port prend une adresse de son r\xC3\xA9seau).";
    tabs_->setTabBadge(TNetwork, std::to_string(p.simPorts.size()) + " simul\xC3\xA9" + (p.simPorts.size() > 1 ? "s" : ""), ui::Tone::Accent);
}

// ================================================ lot 18 : les valeurs simulees ===
bool HmiCommPane::animate(const std::string& equipment, const std::string& address, bool on, std::string* why) {
    return valuesCtl_->setAnimated(equipment, address, on, why);
}
bool HmiCommPane::setBand(const std::string& equipment, const std::string& address, double lo, double hi, std::string* why) {
    return valuesCtl_->setBand(equipment, address, lo, hi, why);
}
bool HmiCommPane::force(const std::string& equipment, const std::string& address, const std::string& raw, std::string* why) {
    return valuesCtl_->setForced(equipment, address, raw, why);
}
bool HmiCommPane::unforceAll(std::string* why) { return valuesCtl_->unforceAll(why); }

void HmiCommPane::showValues(const std::string& equipment, const std::string& address) {
    tabs_->setCurrentIndex(TValues);
    valuesCtl_->refresh();
    if (!equipment.empty()) valuesCtl_->select(equipment, address);
    rebuildProperties();
}

void HmiCommPane::refreshValuesBadge() {
    if (!valuesCtl_ || !tabs_) return;
    const auto n = valuesCtl_->counts();
    if (!n.twins) {
        tabs_->setTabBadge(TValues, "nouveau", ui::Tone::Accent);
        return;
    }
    std::string text = std::to_string(n.animated) + " anim\xC3\xA9" + (n.animated > 1 ? "es" : "e");
    if (n.forced) text += " \xC2\xB7 " + std::to_string(n.forced) + " F";
    tabs_->setTabBadge(TValues, text, n.forced ? ui::Tone::Warning : ui::Tone::Accent);
}

// ============================================================== le rythme ===
void HmiCommPane::tickLot17(double time) {
    const int tab = static_cast<int>(tabs_->currentIndex());
    // La detection a la creation : appliquee si elle a trouve quelque chose.
    if (detector_ && !detector_->running() && !detectShown_) {
        detectShown_ = true;
        const auto rep = detector_->report();
        if (detectAuto_) {
            const auto* e = equipmentOf(detectEquip_);
            bool any = false;
            if (rep && !rep->failed)
                for (const auto& d : rep->tables) any = any || !d.ranges.empty();
            if (e && any && !e->zones.declared) (void)applyDetected();
        }
    }
    // Lot 18 : les valeurs simulees - le direct dix fois par seconde, la fiche deux fois.
    if (tab == TValues && valuesCtl_) {
        if (time - lastValues_ >= 0.1) {
            lastValues_ = time;
            valuesCtl_->tick();
        }
        if (time - lastValuesSide_ >= 1.0) {
            lastValuesSide_ = time;
            refreshValuesBadge();
            if (side() == Side::Values && !grid_->editing()) rebuildProperties();
        }
    }
    // La carte : le direct deux fois par seconde ; un jumeau se lit tout seul.
    if (tab == TMap) {
        if (mapTwin_ && mapEquip_ != kPlcKey && (!mapScanner_ || mapScanKey_ != mapEquip_ + "|1" || !mapScanner_->running()))
            if (const auto* e = equipmentOf(mapEquip_); e && host() && host()->simulatedPort(e->name)) (void)startMapScan();
        if (time - lastMap_ >= 0.5) {
            lastMap_ = time;
            refreshMap(false);
            if (side() == Side::Map && !grid_->editing()) rebuildProperties();
        }
    }
}

} // namespace app
