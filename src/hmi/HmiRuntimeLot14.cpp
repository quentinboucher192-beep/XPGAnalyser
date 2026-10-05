// hmi/HmiRuntimeLot14.cpp - le moteur de l'IHM relie a un automate reel (lot 14) :
// la qualite des valeurs, les variables SYS.Comm*, les boutons du diagnostic.
#include "HmiRuntime.hpp"
#include "HmiApiVars.hpp"   // 1.11.1 (API-M) : API.X est X pour la liaison

#include "HmiComm.hpp"
#include "HmiPublicVars.hpp"
#include "HmiReports.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace hmi {

const comm::Link* Runtime::link() const noexcept { return dynamic_cast<const comm::Link*>(plc_); }
comm::Link* Runtime::link() noexcept { return dynamic_cast<comm::Link*>(plc_); }

bool Runtime::simulatorAttached() const noexcept { return plc_ != nullptr && link() == nullptr; }

comm::Quality Runtime::plcQuality(std::string_view path, std::string* why) const {
    if (path.empty()) return comm::Quality::None;
    const std::string_view root = path.substr(0, path.find_first_of(".["));
    // Lot 15 : une variable IHM liee a un equipement - la qualite de sa liaison ;
    // lot 16 : une case d'une structure liee (Four1.Temperature), ou toute la structure.
    if (const auto* bv = boundVariable(path)) return boundQuality(*bv, why);
    if (root.size() == path.size() && project_)
        if (const auto* v = project_->variable(root); v && v->bound() && types::isComposite(v->type)) return variableQuality(*v, why);
    const auto* lk = link();
    if (!lk) return comm::Quality::None;
    if (root.empty() || pub::isSysRoot(root)) return comm::Quality::None;
    if (project_) {
        if (project_->variable(root)) return comm::Quality::None;       // une variable IHM
        if (project_->viewByName(root)) return comm::Quality::None;     // Vue.Objet.Propriete
        for (const auto& v : project_->views)
            for (const auto& prm : v.params)
                if (pub::same(prm.name, root)) return comm::Quality::None;   // le parametre d'une popup
    }
    // 1.11.1 (API-M, R1111-3) : API.X a la qualite de X (la liaison ne connait que
    // X). Sans cela, une valeur API. d'une liaison perdue s'affichait sans marque,
    // comme si elle etait lue, et une variable sans adresse sans sa raison.
    // 1.11.1 (decision 122) : pas de melange simule / reel. Une variable sans adresse
    // ne se lit pas sur la liaison, avec ou sans API. ; sa marque dit pourquoi et le
    // remede (le Simulateur pour la voir calculee, ou une adresse).
    static const std::string kRemedy =
        " - pour la voir calcul\xC3\xA9" "e : passe la Communication en Simulateur ; pour la lire sur l'automate : "
        "donne-lui une adresse dans le programme de l'automate (Control Expert, puis Fichier > Importer) "
        "ou une ligne dans Configuration > Communication";
    const auto withRemedy = [why](comm::Quality q) {
        // 1.11.2 (R1111-15, decision 144) : le remede dit de donner une adresse ; la
        // raison du plan ne le redit pas (« non localisee : donne-lui une adresse (...) »).
        if (why && q == comm::Quality::Bad && why->starts_with("sans adresse Modbus") && why->find(kRemedy) == std::string::npos)
            *why = apivars::withoutAddressAdvice(*why) + kRemedy;
        return q;
    };
    if (apivars::isApiPath(path)) {
        const std::string name = apivars::stripApi(path);
        const auto q = lk->quality(name, why);
        if (q != comm::Quality::None) return withRemedy(q);
        // Un chemin API. que le plan ne connait pas (une variable d'unite, un membre
        // sans adresse) : illisible sur la liaison. La marque le dit, avec la raison
        // (R1111-3 : le libelle restait fixe, sans rien dire). Le nom nu garde la
        // conduite d'avant.
        if (why) *why = "sans adresse Modbus : " + name + " n'est pas au plan d'adressage, la liaison ne la lit pas (simulation seulement)";
        return withRemedy(comm::Quality::Bad);
    }
    return withRemedy(lk->quality(path, why));
}

bool Runtime::commReconnect(double now, const std::string& source) {
    (void)now;
    auto* lk = link();
    if (!lk) {
        log("Communication", source, "Reconnecter : l'IHM est sur le simulateur, sans liaison Modbus");
        return false;
    }
    lk->reconnect();
    log("Communication", source, "Liaison refaite \xC3\xA0 la demande" + (user_.empty() ? std::string{} : " de " + user_));
    return true;
}

bool Runtime::commResetCounters(double now, const std::string& source) {
    (void)now;
    auto* lk = link();
    if (!lk) return false;
    lk->resetCounters();
    log("Communication", source, "Compteurs de la liaison remis \xC3\xA0 z\xC3\xA9ro" + (user_.empty() ? std::string{} : " par " + user_));
    return true;
}

void Runtime::commPart(const Object& o, std::string_view part, double now) {
    if (part == "bouton:reconnecter") (void)commReconnect(now, o.name);
    else if (part == "bouton:compteurs") (void)commResetCounters(now, o.name);
}

bool Runtime::commSysValue(std::string_view n, sim::Value& out) const {
    const auto text = [&](std::string s) { out = sim::Value::text(std::move(s)); return true; };
    const auto flag = [&](bool b) { out = sim::Value::boolean(b); return true; };
    const auto integer = [&](long long v) { out = sim::Value::integer(sim::Type::Int, v); return true; };
    const auto udint = [&](unsigned long long v) { out = sim::Value::integer(sim::Type::UDInt, static_cast<long long>(v)); return true; };
    const auto real = [&](double v) { out = sim::Value::real(std::round(v * 100.0) / 100.0); return true; };
    const Communication plain{};
    const Communication& cfg = project_ ? project_->comm : plain;
    const auto demo = hooks_.commDemo ? hooks_.commDemo() : std::pair<bool, int>{false, 0};
    const auto* lk = link();
    if (n == "CommMode") return text(lk ? "modbus" : "simulateur");
    if (n == "CommReadOnly") return flag(lk ? !lk->settings().writes : !cfg.writes);
    if (n == "CommDemoServer") return flag(demo.first);
    if (!lk) {
        if (n == "CommConnected") return flag(plc_ != nullptr);
        if (n == "CommState") return text(plc_ ? "Simulateur" : "Absent");
        if (n == "CommAddress" || n == "CommSince" || n == "CommDevice" || n == "CommLastError") return text({});
        if (n == "CommResponseTime" || n == "CommResponseTimeAvg" || n == "CommResponseTimeMax" || n == "CommCycleTime") return real(0);
        if (n == "CommRequests" || n == "CommErrors" || n == "CommTimeouts" || n == "CommReconnects") return udint(0);
        if (n == "CommGoodCount" || n == "CommStaleCount" || n == "CommBadCount") return integer(0);
        return false;
    }
    const auto d = lk->diagnostics();
    if (n == "CommConnected") return flag(d.connected);
    if (n == "CommState") return text(d.state);
    if (n == "CommAddress") return text(d.host + ":" + std::to_string(d.port) + " (esclave " + std::to_string(d.unit) + ")");
    if (n == "CommSince") return text(d.since);
    if (n == "CommDevice") return text(d.device);
    if (n == "CommResponseTime") return real(d.lastMs);
    if (n == "CommResponseTimeAvg") return real(d.avgMs);
    if (n == "CommResponseTimeMax") return real(d.maxMs);
    if (n == "CommCycleTime") return real(d.cycleMs);
    if (n == "CommRequests") return udint(d.requests);
    if (n == "CommErrors") return udint(d.errors);
    if (n == "CommTimeouts") return udint(d.timeouts);
    if (n == "CommReconnects") return udint(d.reconnects);
    if (n == "CommLastError") return text(d.lastError);
    if (n == "CommGoodCount") return integer(static_cast<long long>(d.good));
    if (n == "CommStaleCount") return integer(static_cast<long long>(d.stale));
    if (n == "CommBadCount") return integer(static_cast<long long>(d.bad));
    return false;
}

// ================================================================ lot 14 : C ===
//  Les notifications : chaque evenement d'alarme part a l'ecran (hooks), qui
//  choisit les destinataires et envoie.
void Runtime::notice(const LiveAlarm& a, std::string kind) {
    if (!hooks_.alarmNotice) return;
    AlarmNotice n;
    n.kind = std::move(kind);
    n.alarm = a.alarm;
    n.name = a.name;
    n.message = a.message;
    n.group = a.group;
    n.category = a.category;
    n.instruction = a.instruction;
    n.priority = a.priority;
    n.user = n.kind == "Acquittement" ? user_ : std::string{};
    n.at = dateStampOf(now_);
    hooks_.alarmNotice(n);
}

namespace {

// 1.9 : une variable echantillonnee (mesure archivee, courbe, graphique) reste
// ABONNEE entre deux echantillons : la liaison oublie au bout de 10 s ce qu'on
// ne lit plus, et demander la qualite n'abonne pas - une periode plus longue
// trouvait la variable "en attente" a chaque echantillon. Une lecture abonne
// la variable : on lit, la valeur ne sert pas.
void keepSubscribed(Runtime& rt, const std::string& path) {
    sim::Value scratch;
    if (const auto* bv = rt.boundVariable(path)) {
        if (auto* lk = rt.equipmentLink(bv->equipment)) (void)lk->read(bv->name, scratch);
        return;
    }
    auto* lk = rt.link();
    if (lk && rt.plcQuality(path) != comm::Quality::None) (void)lk->read(path, scratch);
}

std::string upperKey(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Les chemins d'automate de ces textes, sans doublon, dans l'ordre de rencontre.
void addPaths(std::string_view text, std::vector<std::string>& out, std::set<std::string>& seen) {
    if (text.empty()) return;
    for (auto& path : comm::plcPaths(text))
        if (seen.insert(upperKey(path)).second) out.push_back(std::move(path));
}

constexpr std::string_view kFollowOwner = "alarmes";

// 1.11.1 (API-M) : API.X est X pour la liaison (le plan d'adressage ne connait
// que X) - sauf si le projet IHM a une variable ou une vue nommee API (elle
// garde son sens).
std::string linkName(const Project* p, std::string path) {
    if (!apivars::isApiPath(path)) return path;
    if (p && (p->variable("API") || p->viewByName("API"))) return path;
    return apivars::stripApi(path);
}
std::vector<std::string> linkNames(const Project* p, std::vector<std::string> paths) {
    for (auto& path : paths) path = linkName(p, std::move(path));
    return paths;
}

} // namespace

std::size_t Runtime::watchAlarms(std::string_view source, const std::vector<AlarmDef>& defs) {
    std::vector<std::string> paths;
    std::set<std::string> seen;
    for (const auto& def : defs) {
        addPaths(def.condition, paths, seen);
        // Le message et la consigne se remplissent a l'apparition : leurs
        // variables deja lues, pas la valeur par defaut.
        if (def.message.find('{') != std::string::npos) addPaths(def.message, paths, seen);
        if (def.instruction.find('{') != std::string::npos) addPaths(def.instruction, paths, seen);
    }
    const std::string key(source);
    const auto it = alarmWatch_.find(key);
    const bool same = it == alarmWatch_.end() ? paths.empty() : it->second == paths;
    if (!same) {
        if (paths.empty()) alarmWatch_.erase(key);
        else alarmWatch_[key] = std::move(paths);
        ++watchRev_;
    }
    followWatched(now_, true);
    return watchedAlarmPaths().size();
}

std::vector<std::string> Runtime::watchedAlarmPaths() const {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& [source, paths] : alarmWatch_)
        for (const auto& path : paths)
            if (seen.insert(upperKey(path)).second) out.push_back(path);
    return out;
}

void Runtime::followWatched(double now, bool force) {
    if (!project_) return;
    if (!force && followNext_ >= 0 && now < followNext_) return;
    followNext_ = now + 0.5;
    // Les alarmes du projet (et les mesures archivees) modifiees en marche.
    if (project_->alarms != watchedProject_) {
        watchedProject_ = project_->alarms;
        (void)watchAlarms("projet", watchedProject_);
        return;                                   // watchAlarms vient de tout remettre
    }
    if (project_->history.archived != watchedArchive_) {
        watchedArchive_ = project_->history.archived;
        std::vector<std::string> paths;
        std::set<std::string> seen;
        for (const auto& expr : watchedArchive_) addPaths(expr, paths, seen);
        if (paths.empty()) alarmWatch_.erase("archives");
        else alarmWatch_["archives"] = std::move(paths);
        ++watchRev_;
    }
    // La repartition : refaite quand les listes ou les variables liees changent.
    if (routedRev_ != watchRev_ || routedBoundRev_ != boundRev_) {
        routedRev_ = watchRev_;
        routedBoundRev_ = boundRev_;
        followPlc_.clear();
        followEquip_.clear();
        std::set<std::string> plcSeen;
        std::map<std::string, std::set<std::string>> equipSeen;
        const auto toEquipment = [&](const Variable& v) {
            if (v.equipment.empty()) return;
            if (equipSeen[v.equipment].insert(upperKey(v.name)).second) followEquip_[v.equipment].push_back(v.name);
        };
        for (const auto& path : watchedAlarmPaths()) {
            // Une variable IHM liee a un equipement, ou une case d'une structure liee.
            if (const auto* bv = boundVariable(path)) { toEquipment(*bv); continue; }
            const std::string_view root = std::string_view(path).substr(0, path.find_first_of(".["));
            if (root.empty() || pub::isSysRoot(root)) continue;
            if (const auto* v = project_->variable(root)) {
                if (!v->bound()) continue;                          // une variable IHM
                if (!types::isComposite(v->type)) { toEquipment(*v); continue; }   // Mot.3 : le mot
                if (root.size() != path.size()) continue;           // une case inconnue
                // Toute la structure liee : chacune de ses cases.
                const std::string prefix = upperKey(v->name);
                for (auto b = bound_.lower_bound(prefix); b != bound_.end() && b->first.compare(0, prefix.size(), prefix) == 0; ++b)
                    if (b->first.size() > prefix.size() && (b->first[prefix.size()] == '.' || b->first[prefix.size()] == '['))
                        toEquipment(b->second);
                continue;
            }
            if (project_->viewByName(root)) continue;               // Vue.Objet.Propriete
            bool param = false;
            for (const auto& vw : project_->views)
                for (const auto& prm : vw.params) param = param || pub::same(prm.name, root);
            if (param) continue;                                    // le parametre d'une popup
            std::string name = linkName(project_, path);           // 1.11.1 : API.X -> X
            if (plcSeen.insert(upperKey(name)).second) followPlc_.push_back(std::move(name));
        }
    }
    // Remises a chaque fois : une liaison refaite (ou un equipement passe sur
    // son esclave simule) recoit sa liste ; une liste inchangee ne coute rien.
    if (auto* lk = link()) (void)lk->follow(kFollowOwner, followPlc_);
    for (const auto& [equipment, names] : followEquip_)
        if (auto* lk = equipmentLink(equipment)) (void)lk->follow(kFollowOwner, names);
    for (const auto& equipment : followedEquip_)
        if (!followEquip_.count(equipment))
            if (auto* lk = equipmentLink(equipment)) (void)lk->follow(kFollowOwner, {});
    followedEquip_.clear();
    for (const auto& [equipment, names] : followEquip_) followedEquip_.insert(equipment);
}

void Runtime::unfollowAll() {
    if (auto* lk = link()) (void)lk->follow(kFollowOwner, {});
    for (const auto& equipment : followedEquip_)
        if (auto* lk = equipmentLink(equipment)) (void)lk->follow(kFollowOwner, {});
    followedEquip_.clear();
}

// Relie a un automate reel : une variable de la condition pas encore lue (en
// attente) ou illisible (mauvaise) - la condition ne decide rien. 1.9 : ses
// variables sont suivies en permanence des le demarrage (watchAlarms) : elles
// se lisent meme sans vue ouverte, la condition finit par decider.
bool Runtime::alarmUndecided(const AlarmDef& def) {
    if (!qualityRelevant()) return false;
    auto& entry = alarmPaths_[def.id];
    if (entry.first != def.condition || (entry.second.empty() && !def.condition.empty())) entry = {def.condition, linkNames(project_, comm::plcPaths(def.condition))};
    for (const auto& path : entry.second) {
        const auto q = plcQuality(path);
        if (q == comm::Quality::Pending || q == comm::Quality::Bad) return true;
    }
    return false;
}

// Relie a un automate reel : une expression dont une variable n'est pas encore
// lue (en attente) ou illisible (mauvaise) n'a pas de valeur a garder. Les
// archives, les courbes et les graphiques la sautent : pas de faux zero au
// demarrage, avant la premiere lecture, ni apres "Mauvaise apres". 1.9 : ses
// variables sont abonnees au passage (keepSubscribed) - ce qu'on echantillonne
// reste lu par la liaison, meme sans vue qui le montre.
bool Runtime::plcUnread(const std::string& expression) {
    if (expression.empty() || !qualityRelevant()) return false;
    auto it = exprPaths_.find(expression);
    if (it == exprPaths_.end()) {
        if (exprPaths_.size() > 2048) exprPaths_.clear();
        it = exprPaths_.emplace(expression, linkNames(project_, comm::plcPaths(expression))).first;
    }
    for (const auto& path : it->second) keepSubscribed(*this, path);
    for (const auto& path : it->second) {
        const auto q = plcQuality(path);
        if (q == comm::Quality::Pending || q == comm::Quality::Bad) return true;
    }
    return false;
}

bool Runtime::notifySysValue(std::string_view n, sim::Value& out) const {
    const NotifyStats st = hooks_.notifyStats ? hooks_.notifyStats() : NotifyStats{};
    if (n == "NotifyEnabled") { out = sim::Value::boolean(st.enabled || (project_ && project_->notify.enabled)); return true; }
    if (n == "NotifySent") { out = sim::Value::integer(sim::Type::UDInt, static_cast<long long>(st.sent)); return true; }
    if (n == "NotifyFailed") { out = sim::Value::integer(sim::Type::UDInt, static_cast<long long>(st.failed)); return true; }
    if (n == "NotifyLast") { out = sim::Value::text(st.last); return true; }
    if (n == "WebClients") { out = sim::Value::integer(sim::Type::Int, st.webClients); return true; }
    if (n == "ReportLast") { out = sim::Value::text(lastReport_); return true; }
    return false;
}

// ================================================================ lot 14 : D ===
//  Les rapports periodiques. Une fois par seconde au plus : le prochain depart
//  de chaque rapport actif ; passe, le rapport de la periode qui vient de
//  finir s'ecrit. Au lancement, le prochain depart part de maintenant : une
//  heure passee pendant que l'IHM etait arretee ne se rattrape pas.
namespace {
double wallEpochOf(long long startWallMs, double startNow, double clockOffset, double now) {
    return static_cast<double>(startWallMs) / 1000.0 + (now - startNow + clockOffset);
}
} // namespace

void Runtime::reportsCycle(double now) {
    if (!project_ || project_->reports.empty()) return;
    if (lastReportCheck_ >= 0 && now - lastReportCheck_ < 1.0) return;
    lastReportCheck_ = now;
    const double wall = wallEpochOf(startWallMs_, startNow_, settings_.clockOffset, now);
    for (const auto& r : project_->reports) {
        if (!r.enabled) {
            nextReport_.erase(r.id);
            continue;
        }
        auto it = nextReport_.find(r.id);
        if (it == nextReport_.end()) {
            nextReport_[r.id] = report::nextDue(r, wall);
            continue;
        }
        if (wall < it->second) continue;
        it->second = report::nextDue(r, wall);
        std::string where;
        (void)writeReport(r.id, now, false, &where);
    }
}

bool Runtime::writeReport(Id id, double now, bool current, std::string* where) {
    const auto fail = [&](std::string why) {
        if (where) *where = why;
        return false;
    };
    const Report* r = project_ ? project_->report(id) : nullptr;
    if (!r) return fail("rapport introuvable");
    now_ = std::max(now_, now);
    const double wall = wallEpochOf(startWallMs_, startNow_, settings_.clockOffset, now_);
    const double to = current ? wall + 1.0 : report::lastDue(*r, wall);   // maintenant : ce qui vient d'arriver compris
    const double from = current ? report::periodStart(*r, report::nextDue(*r, wall)) : report::periodStart(*r, to);
    // L'historique et les alarmes en cours (pas encore dans l'historique).
    History local;
    const History& h = history_ ? *history_ : local;
    History merged;
    const History* source = &h;
    if (!alarms_.empty()) {
        merged.alarms = h.alarms;
        merged.samples = h.samples;
        merged.events = h.events;
        for (const auto& a : alarms_) {
            AlarmOccurrence o;
            o.alarm = a.alarm;
            o.name = a.name;
            o.message = a.message;
            o.group = a.group;
            o.category = a.category;
            o.priority = a.priority;
            o.appeared = a.appeared;
            o.acked = a.acked && a.ackRequired ? a.ackedAt : std::string{};
            o.ackedBy = a.ackedBy;
            o.cleared = a.active ? std::string{} : a.cleared;
            merged.alarms.push_back(std::move(o));
        }
        source = &merged;
    }
    std::vector<report::Production> figures;
    for (const auto& [objectId, st] : production_) {
        for (const auto& v : project_->views)
            if (const auto* o = v.object(objectId)) figures.push_back({v.name + "/" + o->name, st.figures});
    }
    const auto content = report::build(*project_, *source, *r, from, to, figures, dateStampOf(now_));
    const auto format = r->format == "Excel" ? ExportFormat::Excel : ExportFormat::Pdf;
    ExportRequest rq;
    rq.fileName = report::fileName(*r, to, format);
    rq.format = std::string(exportFormatLabel(format));
    rq.source = "rapport:" + r->name;
    rq.rows = 0;
    for (const auto& t : content.sections) rq.rows += t.rows.size();
    rq.data = std::make_shared<const Bytes>(report::render(content, format));
    rq.origin = "Rapport " + r->name;
    std::string path;
    const bool ok = hooks_.exportFile && hooks_.exportFile(rq, &path);
    if (!ok) {
        log("Erreur", "Rapport " + r->name, "rapport non \xC3\xA9" "crit : " + (path.empty() ? std::string("aucun dossier d'export") : path));
        return fail(path.empty() ? std::string("aucun dossier d'export (le projet est-il enregistr\xC3\xA9 ?)") : path);
    }
    lastReport_ = path;
    event("Rapport", r->name, "du " + report::stampOf(from) + " au " + report::stampOf(to) + " : " + path);
    if (hooks_.reportWritten) {
        ReportOutput out;
        out.report = r->id;
        out.name = r->name;
        out.title = content.title;
        out.from = report::stampOf(from);
        out.to = report::stampOf(to);
        out.fileName = rq.fileName;
        out.path = path;
        out.format = rq.format;
        out.data = rq.data;
        out.recipients = r->recipients;
        hooks_.reportWritten(out);
    }
    if (where) *where = path;
    return true;
}

std::string Runtime::nextReportText(Id id) const {
    const Report* r = project_ ? project_->report(id) : nullptr;
    if (!r) return {};
    if (!r->enabled) return "d\xC3\xA9sactiv\xC3\xA9";
    const auto it = nextReport_.find(id);
    const double wall = wallEpochOf(startWallMs_, startNow_, settings_.clockOffset, now_);
    return report::stampOf(it != nextReport_.end() ? it->second : report::nextDue(*r, wall));
}

} // namespace hmi
