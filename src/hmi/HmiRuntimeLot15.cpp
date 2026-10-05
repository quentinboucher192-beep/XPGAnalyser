// hmi/HmiRuntimeLot15.cpp - le moteur de l'IHM et les equipements du reseau
// (lot 15) : les variables IHM liees a un equipement (lues et ecrites par sa
// liaison, mises a l'echelle, avec leur qualite), SYS.Equip*.
#include "HmiObjectAlarms.hpp"
#include "HmiRuntime.hpp"

#include "HmiComm.hpp"
#include "HmiEquipment.hpp"
#include "HmiHistory.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Un texte sur une ligne : les tabulations, les retours et les \ echappes.
std::string escaped(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

std::string unescaped(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::vector<std::string> fieldsOf(std::string_view line) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (true) {
        const auto tab = line.find('\t', from);
        out.emplace_back(line.substr(from, tab == std::string_view::npos ? std::string_view::npos : tab - from));
        if (tab == std::string_view::npos) break;
        from = tab + 1;
    }
    return out;
}

std::string valueText(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool: return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real: {
            char b[48];
            std::snprintf(b, sizeof b, "%.17g", v.asReal());
            return b;
        }
        case sim::Type::String: return v.asString();
        default: return std::to_string(v.asInteger());
    }
}

} // namespace

std::string Runtime::stateSnapshot() const {
    std::string out = "# XpgAnalyzer - l'etat du poste d'exploitation\n";
    out += "heure\t" + dateStampOf(now_) + "\n";
    if (const auto* v = viewOf(current_)) out += "vue\t" + escaped(v->name) + "\n";
    if (project_) {
        for (const auto& var : project_->programs.variables) {
            if (var.bound()) continue;             // relue dans l'equipement
            // Lot 16 : une structure ou un tableau, case par case.
            const auto leaves = types::isComposite(var.type) ? types::leafVariables(*project_, var) : std::vector<Variable>{var};
            for (const auto& leaf : leaves) {
                const sim::Value* value = variable(leaf.name);
                if (!value) continue;
                out += "variable\t" + escaped(leaf.name) + "\t" + escaped(valueText(*value)) + "\n";
            }
        }
    }
    for (const auto& a : alarms_)
        out += "alarme\t" + escaped(a.name) + "\t" + (a.active ? "1" : "0") + "\t" + (a.acked ? "1" : "0") + "\t" + a.appeared + "\t" + a.ackedAt + "\t"
               + escaped(a.ackedBy) + "\t" + a.cleared + "\t" + escaped(a.message) + "\n";
    return out;
}

bool Runtime::restoreState(std::string_view text, double now, std::string* report) {
    if (!project_) return false;
    now_ = std::max(now_, now);
    std::size_t vars = 0, alarms = 0;
    std::string view, at;
    std::size_t from = 0;
    while (from < text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = text.substr(from, nl - from);
        from = nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        const auto f = fieldsOf(line);
        if (f[0] == "heure" && f.size() > 1) at = f[1];
        else if (f[0] == "vue" && f.size() > 1) view = unescaped(f[1]);
        else if (f[0] == "variable" && f.size() > 2) {
            const std::string name = unescaped(f[1]);
            // Lot 16 : "Four1.Temperature" - la variable est la racine du chemin.
            const auto* var = project_->variable(std::string_view(name).substr(0, name.find_first_of(".[")));
            if (!var || var->bound()) continue;
            sim::Value* slot = ihmSlot(name);
            if (!slot) continue;
            const std::string value = unescaped(f[2]);
            const sim::Type t = slot->type();
            sim::Value v;
            if (t == sim::Type::Bool) v = sim::Value::boolean(value == "TRUE" || value == "1");
            else if (t == sim::Type::Real) v = sim::Value::real(std::strtod(value.c_str(), nullptr));
            else if (t == sim::Type::String) v = sim::Value::text(value);
            else if (t == sim::Type::Time) v = sim::Value::time(std::atoll(value.c_str()));
            else v = sim::Value::integer(t, std::atoll(value.c_str()));
            slot->assignFrom(v);
            ++vars;
        } else if (f[0] == "alarme" && f.size() > 8) {
            const std::string name = unescaped(f[1]);
            const AlarmDef* def = nullptr;
            for (const auto& d : project_->alarms)
                if (sameName(d.name, name)) def = &d;
            if (!def) {                          // 1.9 : une alarme d'objet (generee) se reprend aussi
                if (!objectAlarms_) refreshObjectAlarms();
                def = alarmDefByName(name);
            }
            if (!def || std::any_of(alarms_.begin(), alarms_.end(), [&](const LiveAlarm& a) { return a.alarm == def->id; })) continue;
            LiveAlarm a;
            a.alarm = def->id;
            a.name = def->name;
            a.group = def->group;
            if (const auto* oa = objectAlarm(def->id)) {
                a.objectGroup = oa->objectGroup;
                a.symbols = oa->symbols;
            }
            a.category = def->category;
            a.priority = std::clamp(def->priority, 1, kAlarmPriorities);
            a.ackRequired = def->ackRequired;
            a.active = f[2] == "1";
            a.acked = f[3] == "1";
            a.appeared = f[4];
            a.ackedAt = f[5];
            a.ackedBy = unescaped(f[6]);
            a.cleared = f[7];
            a.message = unescaped(f[8]);
            alarms_.push_back(std::move(a));
            ++alarms;
        }
    }
    std::stable_sort(alarms_.begin(), alarms_.end(), [](const LiveAlarm& x, const LiveAlarm& y) {
        return x.priority != y.priority ? x.priority < y.priority : x.appeared > y.appeared;
    });
    bool viewShown = false;
    if (!view.empty()) viewShown = navigate(view, Transition{}, now);
    const std::string summary = std::to_string(vars) + " variable(s), " + std::to_string(alarms) + " alarme(s) en cours"
                                + (viewShown ? ", la vue " + view : std::string{}) + (at.empty() ? std::string{} : " (\xC3\xA9tat de " + at.substr(0, 19) + ")");
    event("Reprise", "Poste d'exploitation", "\xC3\x89tat d'avant l'arr\xC3\xAAt recharg\xC3\xA9 : " + summary);
    log("Syst\xC3\xA8me", "Reprise", "\xC3\xA9tat d'avant l'arr\xC3\xAAt recharg\xC3\xA9 : " + summary);
    if (report) *report = summary;
    return true;
}

bool Runtime::qualityRelevant() const noexcept { return link() != nullptr || !bound_.empty(); }

void Runtime::refreshBound() {
    if (!project_) {
        bound_.clear();
        boundSource_.clear();
        return;
    }
    if (project_->programs.variables == boundSource_ && project_->programs.types == boundTypes_) return;
    boundSource_ = project_->programs.variables;
    boundTypes_ = project_->programs.types;
    bound_.clear();
    ++boundRev_;                 // 1.9 : les variables suivies se repartissent a nouveau
    // Lot 16 : une structure ou un tableau lie - chaque case a son adresse.
    for (const auto& v : boundSource_) {
        if (!v.bound()) continue;
        if (!types::isComposite(v.type)) {
            bound_.emplace(upperOf(v.name), v);
            continue;
        }
        for (auto& leaf : types::leafVariables(*project_, v)) {
            std::string key = upperOf(leaf.name);
            bound_.emplace(std::move(key), std::move(leaf));
        }
    }
}

comm::Quality Runtime::variableQuality(const Variable& v, std::string* why) const {
    if (!v.bound()) return comm::Quality::None;
    if (!types::isComposite(v.type)) return boundQuality(v, why);
    // La pire des cases lues : mauvaise, puis ancienne, bonne ; en attente
    // seulement si aucune n'a encore ete lue (une case qu'aucune vue ne montre
    // n'est pas lue, et ne compte pas pour la structure).
    const auto rank = [](comm::Quality q) {
        switch (q) {
            case comm::Quality::Bad: return 4;
            case comm::Quality::Stale: return 3;
            case comm::Quality::Good: return 2;
            case comm::Quality::Pending: return 1;
            default: return 0;
        }
    };
    comm::Quality worst = comm::Quality::None;
    std::string worstWhy;
    std::size_t cases = 0;
    const std::string prefix = upperOf(v.name);
    for (auto it = bound_.lower_bound(prefix); it != bound_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
        if (it->first.size() == prefix.size() || (it->first[prefix.size()] != '.' && it->first[prefix.size()] != '[')) continue;
        ++cases;
        std::string w;
        const auto q = boundQuality(it->second, &w);
        if (rank(q) > rank(worst)) {
            worst = q;
            worstWhy = it->second.name + (w.empty() ? std::string{} : " : " + w);
        }
    }
    if (cases == 0) {
        if (why) *why = "aucune case li\xC3\xA9" "e";
        return comm::Quality::Bad;
    }
    if (why && worst != comm::Quality::Good) *why = worstWhy;
    return worst;
}

const Variable* Runtime::boundVariable(std::string_view name) const {
    if (bound_.empty()) return nullptr;
    const auto it = bound_.find(upperOf(name));
    return it == bound_.end() ? nullptr : &it->second;
}

comm::Link* Runtime::equipmentLink(std::string_view equipment) {
    if (!hooks_.equipmentLink || equipment.empty()) return nullptr;
    return hooks_.equipmentLink(std::string(equipment));
}

std::vector<EquipmentStatus> Runtime::equipmentStatuses() const {
    if (hooks_.equipmentStatus) return hooks_.equipmentStatus();
    // Sans l'ecran (les essais) : ce que dit le projet, rien n'a ete teste.
    std::vector<EquipmentStatus> out;
    if (!project_) return out;
    for (const auto& e : project_->equipments) {
        EquipmentStatus st;
        st.name = e.name;
        st.type = std::string(equipmentTypeLabel(e.type));
        st.enabled = e.enabled;
        st.simulated = e.simulated;
        st.state = !e.enabled ? "D\xC3\xA9sactiv\xC3\xA9" : e.simulated ? "Simul\xC3\xA9" : "Pas encore test\xC3\xA9";
        out.push_back(std::move(st));
    }
    return out;
}

std::optional<EquipmentStatus> Runtime::equipmentStatus(std::string_view name) const {
    for (auto& st : equipmentStatuses())
        if (sameName(st.name, name)) return st;
    return std::nullopt;
}

void Runtime::readBound(const std::string& key, sim::Value& cache) {
    const auto it = bound_.find(key);
    if (it == bound_.end()) return;
    const Variable& v = it->second;
    auto* lk = equipmentLink(v.equipment);
    if (!lk) return;
    sim::Value raw;
    if (!lk->read(v.name, raw)) return;
    const auto q = lk->quality(v.name);
    if (q == comm::Quality::Good || q == comm::Quality::Stale) cache.assignFrom(equip::fromRegister(v, raw));
}

int Runtime::writeBound(const std::string& key, const sim::Value& value, std::string* why) {
    const auto it = bound_.find(key);
    if (it == bound_.end()) return -1;
    const Variable& v = it->second;
    const auto* eq = project_ ? project_->equipmentByName(v.equipment) : nullptr;
    const auto refuse = [&](std::string text) {
        if (why) *why = v.name + " : " + std::move(text);
        return 0;
    };
    if (!eq) return refuse("\xC3\xA9quipement inconnu (" + v.equipment + ")");
    if (!eq->enabled) return refuse("l'\xC3\xA9quipement " + eq->name + " est d\xC3\xA9sactiv\xC3\xA9");
    if (!eq->modbus()) return refuse("un \xC3\xA9quipement Ethernet TCP/IP ne s'\xC3\xA9" "crit pas");
    if (v.readOnly || !eq->writes) return refuse("en lecture seule (" + eq->name + ")");
    auto* lk = equipmentLink(eq->name);
    if (!lk) return refuse("pas de liaison avec " + eq->name);
    if (!lk->write(v.name, equip::toRegister(v, value))) return refuse("\xC3\xA9" "criture refus\xC3\xA9" "e par la liaison de " + eq->name);
    return 1;
}

comm::Quality Runtime::boundQuality(const Variable& v, std::string* why) const {
    const auto* eq = project_ ? project_->equipmentByName(v.equipment) : nullptr;
    const auto bad = [&](std::string text) {
        if (why) *why = std::move(text);
        return comm::Quality::Bad;
    };
    if (!eq) return bad("\xC3\xA9quipement inconnu : " + v.equipment);
    if (!eq->enabled) return bad("l'\xC3\xA9quipement " + eq->name + " est d\xC3\xA9sactiv\xC3\xA9");
    if (!eq->modbus()) return bad(eq->name + " est un \xC3\xA9quipement Ethernet TCP/IP : pas de variables");
    const comm::Link* lk = hooks_.equipmentLink ? hooks_.equipmentLink(eq->name) : nullptr;
    if (!lk) {
        if (why) *why = "en attente de la liaison avec " + eq->name;
        return comm::Quality::Pending;
    }
    const auto q = lk->quality(v.name, why);
    if (q != comm::Quality::None) return q;
    const std::string reason = lk->plan().whyNot(v.name);
    return bad(reason.empty() ? "sans adresse sur " + eq->name : reason);
}

bool Runtime::equipSysValue(std::string_view n, sim::Value& out) const {
    const auto statuses = equipmentStatuses();
    int enabled = 0, online = 0, offline = 0, simulated = 0;
    std::string names;
    for (const auto& st : statuses) {
        if (!st.enabled) continue;
        ++enabled;
        if (st.simulated) ++simulated;
        if (st.reachable) ++online;
        else if (st.tested && !st.simulated) {
            ++offline;
            names += (names.empty() ? "" : ", ") + st.name;
        }
    }
    const auto integer = [&](long long v) { out = sim::Value::integer(sim::Type::Int, v); return true; };
    if (n == "EquipCount") return integer(enabled);
    if (n == "EquipOnline") return integer(online);
    if (n == "EquipOffline") return integer(offline);
    if (n == "EquipSimulated") return integer(simulated);
    if (n == "EquipOfflineNames") {
        out = sim::Value::text(names);
        return true;
    }
    return false;
}

} // namespace hmi
