// =============================================================================
//  hmi/HmiIncident.cpp - 1.20.0 : le detecteur, le contexte du defaut, la fiche de diagnostic
// =============================================================================
#include "HmiIncident.hpp"

#include "HmiRelations.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace hmi::incident {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) { return a.size() == b.size() && upper(a) == upper(b); }

// Un motif sans casse, * pour n'importe quelle suite.
bool matches(std::string_view pattern, std::string_view text) {
    const std::string p = upper(pattern), t = upper(text);
    std::size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == t[ti])) {
            ++pi;
            ++ti;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", s);
    std::string out(buf);
    for (auto& c : out)
        if (c == '.') c = ',';
    return out + " s";
}

void addUnique(std::vector<std::string>& v, const std::string& s) {
    if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

} // namespace

// ---- les libelles ----------------------------------------------------------------------------------------------------
std::string_view eventLabel(EventKind k) noexcept {
    switch (k) {
        case EventKind::Appear: return "Apparition";
        case EventKind::Reappear: return "R\xC3\xA9" "apparition";
        case EventKind::Held: return "Maintien";
        case EventKind::Ack: return "Acquittement";
        case EventKind::Clear: return "Disparition";
        case EventKind::CommLost: return "Perte de communication";
        case EventKind::CommBack: return "Communication retrouv\xC3\xA9" "e";
        case EventKind::Technical: return "\xC3\x89v\xC3\xA9nement technique";
    }
    return "?";
}
std::string_view eventKey(EventKind k) noexcept {
    switch (k) {
        case EventKind::Appear: return "apparition";
        case EventKind::Reappear: return "reapparition";
        case EventKind::Held: return "maintien";
        case EventKind::Ack: return "acquittement";
        case EventKind::Clear: return "disparition";
        case EventKind::CommLost: return "perte_communication";
        case EventKind::CommBack: return "communication_retrouvee";
        case EventKind::Technical: return "technique";
    }
    return "apparition";
}
EventKind eventFromKey(std::string_view k) noexcept {
    for (const auto e : {EventKind::Appear, EventKind::Reappear, EventKind::Held, EventKind::Ack, EventKind::Clear, EventKind::CommLost,
                         EventKind::CommBack, EventKind::Technical})
        if (eventKey(e) == k) return e;
    return EventKind::Appear;
}
std::string_view certaintyLabel(Certainty c) noexcept {
    switch (c) {
        case Certainty::Fact: return "fait v\xC3\xA9rifi\xC3\xA9";
        case Certainty::Indirect: return "relation indirecte";
        case Certainty::Hypothesis: return "hypoth\xC3\xA8se";
    }
    return "?";
}
std::string_view certaintyKey(Certainty c) noexcept {
    switch (c) {
        case Certainty::Fact: return "fait";
        case Certainty::Indirect: return "indirect";
        case Certainty::Hypothesis: return "hypothese";
    }
    return "hypothese";
}
Certainty certaintyFromKey(std::string_view k) noexcept {
    if (k == "fait") return Certainty::Fact;
    if (k == "indirect") return Certainty::Indirect;
    return Certainty::Hypothesis;
}
std::string_view relatedLabel(Related::Kind k) noexcept {
    switch (k) {
        case Related::Kind::Shared: return "d\xC3\xA9pendance technique commune";
        case Related::Kind::Simultaneous: return "apparue au m\xC3\xAAme moment";
        case Related::Kind::Secondary: return "potentiellement secondaire";
        case Related::Kind::Independent: return "ind\xC3\xA9pendante";
    }
    return "?";
}

bool watched(const Settings& s, std::string_view alarm) {
    for (const auto& p : s.ignored)
        if (matches(p, alarm)) return false;
    if (s.watched.empty()) return true;
    return std::any_of(s.watched.begin(), s.watched.end(), [&](const std::string& p) { return matches(p, alarm); });
}

// ---- le detecteur ----------------------------------------------------------------------------------------------------
Monitor::Monitor(Settings s) : settings_(std::move(s)) {}

void Monitor::push(Event e, bool trigger) {
    history_.push_back(e);
    while (history_.size() > 2000) history_.pop_front();
    fresh_.push_back(e);
    if (trigger) triggers_.push_back(std::move(e));
}

void Monitor::notice(const AlarmNotice& n, double t) {
    if (!settings_.enabled) return;
    EventKind kind = EventKind::Appear;
    if (n.kind == "Apparition") kind = EventKind::Appear;
    else if (n.kind == "R\xC3\xA9" "apparition") kind = EventKind::Reappear;
    else if (n.kind == "Acquittement") kind = EventKind::Ack;
    else if (n.kind == "Disparition") kind = EventKind::Clear;
    else return;
    const bool watch = watched(settings_, n.name);
    State& st = alarms_[upper(n.name)];
    st.alarm = n.alarm;
    st.priority = n.priority;
    Event e;
    e.kind = kind;
    e.name = n.name;
    e.alarm = n.alarm;
    e.at = n.at;
    e.t = t;
    e.detail = n.message;
    e.priority = n.priority;
    bool trigger = false;
    switch (kind) {
        case EventKind::Appear:
        case EventKind::Reappear: {
            if (st.active) return;   // deja active : pas un nouveau front (le moteur ne le redit pas, par prudence)
            // apparue de nouveau apres une disparition : une reapparition (le moteur dit Apparition d'une alarme fermee)
            if (st.seen > 0 && st.cleared >= 0) e.kind = EventKind::Reappear;
            st.active = true;
            st.since = t;
            st.at = n.at;
            st.heldSaid = false;
            ++st.seen;
            trigger = watch && t - st.lastTrigger > settings_.debounce;
            if (trigger) st.lastTrigger = t;
            break;
        }
        case EventKind::Clear:
            if (!st.active) return;
            st.active = false;
            st.cleared = t;
            if (st.since > 0 || t > 0) e.detail = (e.detail.empty() ? std::string{} : e.detail + " - ") + "active " + seconds(t - st.since);
            break;
        default: break;
    }
    push(std::move(e), trigger);
}

void Monitor::equipments(const std::vector<EquipmentStatus>& list, double t) {
    if (!settings_.enabled) return;
    for (const auto& s : list) {
        if (!s.enabled || !s.tested) continue;
        const auto it = reachable_.find(s.name);
        const bool before = it == reachable_.end() ? true : it->second;
        reachable_[s.name] = s.reachable;
        if (before == s.reachable) continue;
        Event e;
        e.kind = s.reachable ? EventKind::CommBack : EventKind::CommLost;
        e.name = s.name;
        e.t = t;
        e.detail = s.reachable ? s.state : (s.state + (s.why.empty() ? std::string{} : " (" + s.why + ")"));
        push(std::move(e), !s.reachable && settings_.communication);
    }
}

void Monitor::technical(std::string name, std::string detail, double t, std::string at) {
    if (!settings_.enabled) return;
    Event e;
    e.kind = EventKind::Technical;
    e.name = std::move(name);
    e.detail = std::move(detail);
    e.t = t;
    e.at = std::move(at);
    push(std::move(e), watched(settings_, e.name));
}

void Monitor::tick(double t) {
    if (!settings_.enabled || settings_.heldAfter <= 0) return;
    for (auto& [key, st] : alarms_) {
        if (!st.active || st.heldSaid || t - st.since < settings_.heldAfter) continue;
        st.heldSaid = true;
        Event e;
        e.kind = EventKind::Held;
        e.alarm = st.alarm;
        e.priority = st.priority;
        e.t = t;
        e.detail = "toujours active apr\xC3\xA8s " + seconds(t - st.since);
        for (const auto& h : history_)
            if (upper(h.name) == key) e.name = h.name;
        if (e.name.empty()) e.name = key;
        push(std::move(e), false);
    }
}

std::vector<Event> Monitor::take() { return std::exchange(fresh_, {}); }
std::vector<Event> Monitor::takeTriggers() { return std::exchange(triggers_, {}); }

std::vector<Event> Monitor::timeline(double from, double to) const {
    std::vector<Event> out;
    for (const auto& e : history_)
        if (e.t >= from && e.t <= to) out.push_back(e);
    return out;
}

std::vector<std::string> Monitor::active() const {
    std::vector<std::string> out;
    for (const auto& [key, st] : alarms_)
        if (st.active) {
            std::string name = key;
            for (auto it = history_.rbegin(); it != history_.rend(); ++it)
                if (upper(it->name) == key) {
                    name = it->name;
                    break;
                }
            out.push_back(name);
        }
    return out;
}

std::size_t Monitor::occurrences(std::string_view alarm) const {
    const auto it = alarms_.find(upper(alarm));
    return it == alarms_.end() ? 0 : it->second.seen;
}

void Monitor::clear() {
    alarms_.clear();
    reachable_.clear();
    history_.clear();
    fresh_.clear();
    triggers_.clear();
}

// ---- le contexte -----------------------------------------------------------------------------------------------------
Snapshot snapshot(const SnapshotInput& in) {
    Snapshot s;
    s.id = in.id;
    s.createdAt = in.createdAt.empty() ? in.trigger.at : in.createdAt;
    s.project = in.projectName;
    s.projectVersion = in.projectVersion;
    s.software = in.software;
    s.trigger = in.trigger;
    s.timeline = in.timeline;
    s.activeAlarms = in.active;
    s.openView = in.openView;
    for (const auto& e : in.equipments)
        s.equipmentStates.push_back(e.name + " : " + e.state + (e.why.empty() || e.reachable ? std::string{} : " (" + e.why + ")"));
    if (!in.project) {
        s.missing.push_back("le projet n'\xC3\xA9tait pas disponible : aucune d\xC3\xA9pendance n'a pu \xC3\xAAtre lue");
        return s;
    }
    const Project& pr = *in.project;
    const std::string name = in.trigger.name;
    // 1. l'alarme, copiee
    if (const AlarmDef* a = pr.alarmByName(name)) {
        s.alarm.known = true;
        s.alarm.id = a->id;
        s.alarm.name = a->name;
        s.alarm.description = a->description;
        s.alarm.condition = a->condition;
        s.alarm.message = a->message;
        s.alarm.group = a->group;
        s.alarm.category = a->category;
        s.alarm.instruction = a->instruction;
        s.alarm.priority = a->priority;
        s.alarm.delayMs = a->delayMs;
        s.alarm.ackRequired = a->ackRequired;
    } else {
        s.alarm.name = name;
        s.alarm.priority = in.trigger.priority;
        if (in.trigger.kind == EventKind::CommLost || in.trigger.kind == EventKind::CommBack) s.alarm.category = "Communication";
        else s.missing.push_back("l'alarme \xC2\xAB " + name + " \xC2\xBB n'est pas une alarme du projet (une alarme d'objet ou un \xC3\xA9v\xC3\xA9nement) : "
                                 "sa condition n'est pas copi\xC3\xA9" "e");
    }
    std::optional<rel::Graph> own;
    if (!in.graph) own = rel::build(pr);
    const rel::Graph& g = in.graph ? *in.graph : *own;
    std::set<std::string> seen;   // "kind|name" deja dans la chaine
    const auto add = [&](Link l) {
        if (static_cast<int>(s.chain.size()) >= in.maxLinks) return false;
        if (!seen.insert(l.kind + "|" + upper(l.name) + "|" + l.relation).second) return false;
        s.chain.push_back(std::move(l));
        return true;
    };
    // 2. la condition : les chemins qu'elle lit
    std::optional<std::size_t> alarmNode;
    for (const auto i : g.findAll(name))
        if (g.node(i).kind == rel::Kind::Alarm) alarmNode = i;
    std::vector<std::string> roots;
    if (alarmNode) {
        for (const auto e : g.out(*alarmNode)) {
            const auto& edge = g.edges()[e];
            if (edge.link != rel::Link::Watches && edge.link != rel::Link::Reads) continue;
            const auto& to = g.node(edge.to);
            if (to.kind != rel::Kind::Variable) continue;
            const std::string path = edge.path.empty() ? to.name : edge.path;
            if (edge.link == rel::Link::Watches) addUnique(s.conditionVariables, path);
            addUnique(roots, to.name);
            add({"variable", path, edge.link == rel::Link::Watches ? "lue par la condition" : "lue par le message", edge.where, to.detail, 1,
                 Certainty::Fact});
        }
    }
    if (s.conditionVariables.empty() && s.alarm.known)
        s.missing.push_back("la condition ne lit aucune variable que le graphe connaisse (un nom calcul\xC3\xA9, ou une variable de l'automate)");
    // 3. chaque variable : sa structure, son equipement et son adresse, qui l'ecrit, qui la lit, ou elle se voit
    std::set<std::string> equipmentsOfChain;
    std::vector<std::string> writers;
    for (const auto& root : roots) {
        const Variable* v = pr.variable(root);
        if (!v) continue;
        if (pr.hmiTypeByName(v->type)) add({"structure", v->type, "type de " + v->name, {}, {}, 1, Certainty::Fact});
        if (!v->equipment.empty()) {
            equipmentsOfChain.insert(v->equipment);
            std::string where;
            if (const Equipment* eq = pr.equipmentByName(v->equipment))
                where = std::string(equipmentTypeLabel(eq->type)) + " " + (eq->simulated ? std::string("simul\xC3\xA9") : eq->host + ":" + std::to_string(eq->port))
                        + " (esclave " + std::to_string(eq->unit) + ")";
            add({"equipement", v->equipment, "source de " + v->name, {}, where, 1, Certainty::Fact});
            add({"adresse", v->name + " @ " + (v->address.empty() ? std::string("?") : v->address), "adresse de communication", {}, v->equipment, 1,
                 Certainty::Fact});
        }
        const auto node = g.find(rel::Kind::Variable, root);
        if (!node) continue;
        for (const auto e : g.in(*node)) {
            const auto& edge = g.edges()[e];
            const auto& from = g.node(edge.from);
            const std::string kind = from.kind == rel::Kind::Script ? "script" : from.kind == rel::Kind::Function ? "fonction"
                                   : from.kind == rel::Kind::Object ? "objet" : from.kind == rel::Kind::Alarm ? "alarme" : std::string(rel::kindLabel(from.kind));
            if (from.kind == rel::Kind::Alarm && iequal(from.name, name)) continue;
            if (edge.link == rel::Link::Writes) {
                add({kind, from.name, "\xC3\xA9" "crit " + (edge.path.empty() ? root : edge.path), edge.where, from.detail, 1, Certainty::Fact});
                if (from.kind == rel::Kind::Script || from.kind == rel::Kind::Function) writers.push_back(from.name);
            } else if (edge.link == rel::Link::Reads && (from.kind == rel::Kind::Script || from.kind == rel::Kind::Function)) {
                add({kind, from.name, "lit " + (edge.path.empty() ? root : edge.path), edge.where, from.detail, 1, Certainty::Fact});
            } else if (edge.link == rel::Link::Reads && from.kind == rel::Kind::Object) {
                std::string viewName;
                if (const View* vv = pr.view(from.view)) viewName = vv->name;
                add({"objet", from.name + (viewName.empty() ? std::string{} : " (" + viewName + ")"), "affiche " + (edge.path.empty() ? root : edge.path),
                     edge.where, from.detail, 1, Certainty::Fact});
                if (!viewName.empty()) add({"vue", viewName, "montre " + root, {}, {}, 1, Certainty::Fact});
                if (!in.openView.empty() && iequal(viewName, in.openView)) addUnique(s.visibleObjects, from.name);
            }
        }
    }
    // 4. un niveau de plus : ce que lisent les traitements qui ecrivent les variables de la condition (ils peuvent influencer)
    for (const auto& w : writers) {
        for (const auto i : g.findAll(w)) {
            if (g.node(i).kind != rel::Kind::Script && g.node(i).kind != rel::Kind::Function) continue;
            for (const auto e : g.out(i)) {
                const auto& edge = g.edges()[e];
                if (edge.link != rel::Link::Reads) continue;
                const auto& to = g.node(edge.to);
                if (to.kind != rel::Kind::Variable || std::find(roots.begin(), roots.end(), to.name) != roots.end()) continue;
                add({"variable", edge.path.empty() ? to.name : edge.path, "lue par " + w + " (qui \xC3\xA9" "crit la condition)", edge.where, to.detail, 2,
                     Certainty::Indirect});
                if (const Variable* v = pr.variable(to.name); v && !v->equipment.empty()) {
                    add({"equipement", v->equipment, "source de " + v->name + " (via " + w + ")", {}, {}, 2, Certainty::Indirect});
                    equipmentsOfChain.insert(v->equipment);
                }
            }
        }
    }
    // 5. les valeurs au moment du defaut
    for (const auto& path : s.conditionVariables) {
        ValueAt va;
        va.path = path;
        if (in.read)
            if (const auto got = in.read(path)) {
                va.available = true;
                va.value = got->first;
                va.quality = got->second;
            }
        if (!va.available) s.missing.push_back("la valeur de " + path + " au moment du d\xC3\xA9" "faut (non disponible)");
        s.values.push_back(std::move(va));
    }
    // 6. les autres alarmes : une dependance commune, simultanees, secondaires, independantes
    std::map<std::string, Related> rel;
    for (const auto& root : roots)
        for (const auto i : rel::alarmsAbout(g, root)) {
            const auto& n = g.node(i);
            if (iequal(n.name, name)) continue;
            Related r;
            r.name = n.name;
            r.kind = Related::Kind::Shared;
            r.why = "m\xC3\xAAme variable " + root;
            rel.emplace(upper(n.name), r);
        }
    // le meme equipement : seulement les alarmes PRESENTES autour du defaut (actives, ou dans la chronologie) - pas
    // toutes celles de l'equipement (un projet de 1500 alarmes : du bruit, et du temps)
    std::vector<std::string> around = in.active;
    for (const auto& e : in.timeline)
        if (e.kind == EventKind::Appear || e.kind == EventKind::Reappear) around.push_back(e.name);
    for (const auto& a : around) {
        if (iequal(a, name) || rel.count(upper(a))) continue;
        const auto node = g.find(rel::Kind::Alarm, a);
        if (!node) continue;
        for (const auto e : g.out(*node)) {
            const auto& to = g.node(g.edges()[e].to);
            const Variable* v = to.kind == rel::Kind::Variable ? pr.variable(to.name) : nullptr;
            if (v && equipmentsOfChain.count(v->equipment)) {
                Related r;
                r.name = g.node(*node).name;
                r.kind = Related::Kind::Shared;
                r.why = "m\xC3\xAAme \xC3\xA9quipement " + v->equipment;
                rel.emplace(upper(a), r);
                break;
            }
        }
    }
    double commLostAt = -1;
    std::string commLostName;
    for (const auto& e : in.timeline) {
        if (e.kind == EventKind::CommLost && equipmentsOfChain.count(e.name) && e.t <= in.trigger.t + 0.001) {
            commLostAt = e.t;
            commLostName = e.name;
        }
        if ((e.kind != EventKind::Appear && e.kind != EventKind::Reappear) || iequal(e.name, name)) continue;
        const double dt = e.t - in.trigger.t;
        auto it = rel.find(upper(e.name));
        if (it == rel.end()) {
            Related r;
            r.name = e.name;
            r.kind = std::abs(dt) <= 1.0 ? Related::Kind::Simultaneous : Related::Kind::Independent;
            r.why = std::abs(dt) <= 1.0 ? "apparue " + seconds(std::abs(dt)) + (dt < 0 ? " avant" : " apr\xC3\xA8s") + ", sans d\xC3\xA9pendance commune connue"
                                        : "apparue " + seconds(std::abs(dt)) + (dt < 0 ? " avant" : " apr\xC3\xA8s");
            it = rel.emplace(upper(e.name), r).first;
        } else {
            it->second.why += " ; apparue " + seconds(std::abs(dt)) + (dt < 0 ? " avant" : " apr\xC3\xA8s");
        }
        it->second.at = e.at;
    }
    // les equipements dont chaque alarme liee lit des variables (une perte de communication la rend secondaire)
    const auto equipmentsOf = [&](const std::string& alarm) {
        std::set<std::string> out;
        const auto node = g.find(rel::Kind::Alarm, alarm);
        if (!node) return out;
        for (const auto e : g.out(*node)) {
            const auto& to = g.node(g.edges()[e].to);
            if (to.kind != rel::Kind::Variable) continue;
            if (const Variable* v = pr.variable(to.name); v && !v->equipment.empty()) out.insert(v->equipment);
        }
        return out;
    };
    for (auto& [key, r] : rel) {
        r.active = std::any_of(in.active.begin(), in.active.end(), [&](const std::string& a) { return iequal(a, r.name); });
        if (commLostAt >= 0 && r.kind == Related::Kind::Shared && equipmentsOf(r.name).count(commLostName)) {
            r.kind = Related::Kind::Secondary;
            r.why += " ; apr\xC3\xA8s la perte de communication de " + commLostName;
        }
        s.related.push_back(r);
    }
    std::sort(s.related.begin(), s.related.end(), [](const Related& a, const Related& b) { return static_cast<int>(a.kind) < static_cast<int>(b.kind); });
    // 7. les pistes, classees - jamais une cause affirmee
    if (commLostAt >= 0) {
        Hypothesis h;
        h.text = "Un d\xC3\xA9" "faut de communication avec " + commLostName + " peut expliquer l'alarme : ses mesures ont pu \xC3\xAAtre fig\xC3\xA9" "es ou "
                 "fausses.";
        h.certainty = Certainty::Indirect;
        h.evidence.push_back("perte de communication de " + commLostName + " " + seconds(in.trigger.t - commLostAt) + " avant l'alarme");
        h.evidence.push_back("la condition lit des variables de " + commLostName);
        h.check.push_back("la liaison avec " + commLostName + " (c\xC3\xA2" "ble, alimentation, adresse IP)");
        s.hypotheses.push_back(h);
    }
    for (const auto& st : in.equipments)
        if (equipmentsOfChain.count(st.name) && st.tested && !st.reachable && commLostAt < 0) {
            Hypothesis h;
            h.text = "L'\xC3\xA9quipement " + st.name + " \xC3\xA9tait injoignable au moment du d\xC3\xA9" "faut : ses mesures n'\xC3\xA9taient pas \xC3\xA0 jour.";
            h.certainty = Certainty::Indirect;
            h.evidence.push_back(st.name + " : " + st.state + (st.why.empty() ? std::string{} : " (" + st.why + ")"));
            h.check.push_back("la communication avec " + st.name);
            s.hypotheses.push_back(h);
        }
    for (const auto& root : roots)
        if (const Variable* v = pr.variable(root); v && !v->equipment.empty()) {
            Hypothesis h;
            h.text = "La mesure " + root + " vient de " + v->equipment + (v->address.empty() ? std::string{} : " (" + v->address + ")")
                     + " : un capteur ou une mesure incorrecte peut d\xC3\xA9" "clencher l'alarme.";
            h.evidence.push_back("la condition lit " + root);
            h.check.push_back("le capteur et la valeur brute de " + root + " sur " + v->equipment);
            s.hypotheses.push_back(h);
        }
    for (const auto& w : writers) {
        Hypothesis h;
        h.text = "Le traitement " + w + " \xC3\xA9" "crit une variable de la condition : un calcul ou une condition de r\xC3\xA9gulation peut l'influencer.";
        h.evidence.push_back(w + " \xC3\xA9" "crit une variable lue par la condition");
        h.check.push_back("le code de " + w);
        s.hypotheses.push_back(h);
    }
    for (const auto& r : s.related)
        if (r.kind == Related::Kind::Shared || r.kind == Related::Kind::Secondary)
            for (const auto& e : in.timeline)
                if ((e.kind == EventKind::Appear || e.kind == EventKind::Reappear) && iequal(e.name, r.name) && e.t < in.trigger.t) {
                    Hypothesis h;
                    h.text = "Une cause commune est possible : " + r.name + " (" + r.why + ") est apparue " + seconds(in.trigger.t - e.t) + " avant.";
                    h.evidence.push_back(r.why);
                    h.check.push_back("ce qui a d\xC3\xA9" "clench\xC3\xA9 " + r.name);
                    s.hypotheses.push_back(h);
                    break;
                }
    std::stable_sort(s.hypotheses.begin(), s.hypotheses.end(),
                     [](const Hypothesis& a, const Hypothesis& b) { return static_cast<int>(a.certainty) < static_cast<int>(b.certainty); });
    // 8. a examiner
    for (const auto& l : s.chain)
        if (l.depth == 1 && (l.kind == "variable" || l.kind == "equipement" || l.kind == "script" || l.kind == "fonction"))
            addUnique(s.toCheck, l.kind + " " + l.name);
    // 9. le resume
    s.summary = std::string(eventLabel(in.trigger.kind)) + " de \xC2\xAB " + name + " \xC2\xBB" + (in.trigger.at.empty() ? std::string{} : " le " + in.trigger.at)
                + (s.conditionVariables.empty() ? std::string{} : " ; la condition lit " + std::to_string(s.conditionVariables.size()) + " variable(s)")
                + (equipmentsOfChain.empty() ? std::string{} : " de " + std::to_string(equipmentsOfChain.size()) + " \xC3\xA9quipement(s)")
                + (s.related.empty() ? std::string{} : " ; " + std::to_string(s.related.size()) + " autre(s) alarme(s) li\xC3\xA9" "e(s) ou proche(s)") + ".";
    return s;
}

// ---- la fiche --------------------------------------------------------------------------------------------------------
std::string diagnosis(const Snapshot& s, std::string_view analysis) {
    std::string t;
    t += "## Incident : " + (s.alarm.name.empty() ? s.trigger.name : s.alarm.name) + "\n";
    t += "### 1. R\xC3\xA9sum\xC3\xA9\n" + s.summary + "\n";
    t += "### 2. Alarme d\xC3\xA9" "clench\xC3\xA9" "e\n";
    t += "- \xC3\x89tat : " + std::string(eventLabel(s.trigger.kind)) + (s.trigger.at.empty() ? std::string{} : ", " + s.trigger.at) + "\n";
    if (!s.alarm.condition.empty()) t += "- Condition : `" + s.alarm.condition + "`\n";
    if (s.alarm.delayMs > 0) t += "- Temporisation : " + std::to_string(s.alarm.delayMs) + " ms\n";
    t += "- Priorit\xC3\xA9 : " + std::to_string(s.alarm.priority) + (s.alarm.group.empty() ? std::string{} : ", groupe " + s.alarm.group) + "\n";
    if (!s.alarm.description.empty()) t += "- Description : " + s.alarm.description + "\n";
    for (const auto& v : s.values)
        t += "- " + v.path + " = " + (v.available ? v.value + (v.quality.empty() ? std::string{} : " (" + v.quality + ")") : std::string("non disponible")) + "\n";
    t += "### 3. Origine des donn\xC3\xA9" "es\n";
    bool origin = false;
    for (const auto& l : s.chain)
        if (l.kind == "structure" || l.kind == "equipement" || l.kind == "adresse") {
            t += "- " + l.kind + " " + l.name + " \xE2\x80\x94 " + l.relation + (l.detail.empty() ? std::string{} : " (" + l.detail + ")") + "\n";
            origin = true;
        }
    if (!origin) t += "- Aucune variable de la condition n'est li\xC3\xA9" "e \xC3\xA0 un \xC3\xA9quipement.\n";
    t += "### 4. Cha\xC3\xAEne de d\xC3\xA9pendances\n";
    for (const auto& l : s.chain)
        t += "- [" + std::string(certaintyLabel(l.certainty)) + "] " + l.kind + " " + l.name + " \xE2\x80\x94 " + l.relation
             + (l.where.empty() ? std::string{} : " \xE2\x80\x94 " + l.where) + "\n";
    if (s.chain.empty()) t += "- Aucune d\xC3\xA9pendance lue dans le projet.\n";
    t += "### 5. Autres alarmes\n";
    for (const auto& r : s.related)
        t += "- " + r.name + " : " + std::string(relatedLabel(r.kind)) + " (" + r.why + ")" + (r.active ? " - active" : "") + "\n";
    if (s.related.empty()) t += "- Aucune autre alarme li\xC3\xA9" "e ou proche.\n";
    t += "### 6. Chronologie\n";
    for (const auto& e : s.timeline)
        t += "- " + (e.at.empty() ? seconds(e.t) : e.at) + " \xE2\x80\x94 " + std::string(eventLabel(e.kind)) + " : " + e.name + "\n";
    if (s.timeline.empty()) t += "- Aucun autre \xC3\xA9v\xC3\xA9nement enregistr\xC3\xA9.\n";
    t += "*L'ordre d'apparition n'est pas une preuve de causalit\xC3\xA9.*\n";
    t += "### 7. Analyse technique\n";
    if (!analysis.empty()) t += std::string(analysis) + "\n";
    else t += "Les relations ci-dessus sont lues dans le projet tel qu'il \xC3\xA9tait au moment du d\xC3\xA9" "faut ; aucune analyse IA n'est encore jointe.\n";
    t += "### 8. Causes potentielles\n";
    for (std::size_t i = 0; i < s.hypotheses.size(); ++i) {
        const auto& h = s.hypotheses[i];
        t += std::to_string(i + 1) + ". [" + std::string(certaintyLabel(h.certainty)) + "] " + h.text + "\n";
        for (const auto& ev : h.evidence) t += "   - appui : " + ev + "\n";
    }
    if (s.hypotheses.empty()) t += "- Aucune piste ne ressort des donn\xC3\xA9" "es disponibles.\n";
    t += "### 9. \xC3\x89l\xC3\xA9ments \xC3\xA0 v\xC3\xA9rifier\n";
    std::vector<std::string> checks = s.toCheck;
    for (const auto& h : s.hypotheses)
        for (const auto& c : h.check) addUnique(checks, c);
    for (const auto& c : checks) t += "- " + c + "\n";
    if (checks.empty()) t += "- Rien de particulier.\n";
    t += "### 10. Niveau de certitude\n";
    std::size_t facts = 0, indirect = 0;
    for (const auto& l : s.chain) (l.certainty == Certainty::Fact ? facts : indirect) += 1;
    t += "- Faits v\xC3\xA9rifi\xC3\xA9s (lus dans le projet) : " + std::to_string(facts) + "\n";
    t += "- Relations indirectes : " + std::to_string(indirect) + "\n";
    t += "- Hypoth\xC3\xA8ses : " + std::to_string(s.hypotheses.size()) + " (des pistes, pas des causes certaines)\n";
    if (!s.missing.empty()) {
        t += "- Non disponible :\n";
        for (const auto& m : s.missing) t += "  - " + m + "\n";
    }
    return t;
}

std::string aiContext(const Snapshot& s, std::size_t maxBytes) {
    std::string t = diagnosis(s);
    if (t.size() > maxBytes) {
        t.resize(maxBytes);
        t += "\n(...tronqu\xC3\xA9)";
    }
    return t;
}

std::string folderName(const Snapshot& s) {
    std::string stamp;
    for (const char c : s.createdAt)
        if (std::isdigit(static_cast<unsigned char>(c))) stamp += c;
    if (stamp.size() >= 14) stamp = stamp.substr(0, 8) + "-" + stamp.substr(8, 6);
    std::string name;
    for (const char c : s.trigger.name) name += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_';
    if (name.size() > 60) name.resize(60);
    std::string id = s.id.size() > 8 ? s.id.substr(0, 8) : s.id;
    return (stamp.empty() ? std::string("incident") : stamp) + "-" + (name.empty() ? std::string("evenement") : name) + (id.empty() ? std::string{} : "-" + id);
}

} // namespace hmi::incident
