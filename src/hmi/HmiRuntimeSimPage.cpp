// =============================================================================
//  hmi/HmiRuntimeSimPage.cpp - la page Simulation de Parametres systeme (1.9)
// -----------------------------------------------------------------------------
//  Le 3e onglet du menu natif : tous les esclaves simules de l'application,
//  de facon generique (comme l'onglet Valeurs simulees) - ce que lit l'IHM
//  pour chaque equipement lie (le vrai, l'esclave, automatique), la marche, la
//  panne, l'exception forcee, et les valeurs de l'esclave choisi (animer, le
//  mouvement, la zone, la periode, forcer). Ici :
//
//    * ce que la geometrie doit savoir (simPageShape) ;
//    * une partie cliquee (systemPart -> simPagePart) : la commande
//      (Runtime::simSlaveCommand : la permission Administrer, le journal) et le
//      message du menu ; sans la permission, rien ne part ("Acces refuse") ;
//    * les variables SYS.Sim* et SYS.Slave.<nom>.<membre> ;
//    * IHM_ESCLAVE_SIMULE (readOnSlave).
//
//  Les choix de la page durent jusqu'au redemarrage de l'IHM (l'ecran les
//  oublie : EquipmentHost::clearRuntimeChoices) ; ils ne vont pas dans le projet.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiPublicVars.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace hmi {

namespace {

constexpr const char* kSource = "Page Simulation";

// "14:02:21" (heure locale).
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

// Un nombre pour une commande : "1450", "12.5", "-3".
std::string plainNumber(double v) {
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(std::llround(v)));
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

// Un nombre pour l'ecran : "1 450", "12,6".
std::string frenchNumber(double v) {
    std::string s = plainNumber(v);
    std::replace(s.begin(), s.end(), '.', ',');
    const std::size_t start = (!s.empty() && s[0] == '-') ? 1 : 0;
    std::size_t end = s.find(',');
    if (end == std::string::npos) end = s.size();
    if (s.find('e') != std::string::npos) return s;
    for (std::size_t i = end; i > start + 3; i -= 3) s.insert(i - 3, " ");
    return s;
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Un registre entier (le pas des - et + est entier, au moins 1).
bool integerType(std::string_view type) {
    const std::string t = upperOf(type);
    return !(t == "REAL" || t == "LREAL" || t.empty());
}

bool unsignedType(std::string_view type) {
    const std::string t = upperOf(type);
    return t == "UINT" || t == "UDINT" || t == "WORD" || t == "DWORD" || t == "USINT" || t == "BYTE" || t == "BOOL";
}

std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

const SimSlaveValue* valueOf(const SimSlave& s, std::string_view key) {
    for (const auto& v : s.values)
        if (v.key == key) return &v;
    return nullptr;
}

std::string valueName(const SimSlaveValue& v) { return v.variable.empty() ? v.address : v.variable; }

// Le pas suivant (+1) ou precedent (-1) sur la grille du pas : 10 -> 15 ; 12,3 -> 15 ou 10.
double stepped(double x, double step, int direction) {
    if (!(step > 0)) return x;
    const double k = x / step;
    const double n = direction > 0 ? std::floor(k + 1e-9) + 1 : std::ceil(k - 1e-9) - 1;
    const double out = n * step;
    return std::fabs(out) < step * 1e-9 ? 0.0 : out;
}

// Le pas d'une zone ou d'une valeur forcee : un dixieme de l'etendue (ou de la grandeur).
double valueStep(const SimSlaveValue& v, double around) {
    const double span = std::fabs(v.high - v.low);
    const double magnitude = std::max({std::fabs(v.low), std::fabs(v.high), std::fabs(around)});
    return simStep(std::max(span, magnitude / 5.0), !v.scaled && integerType(v.type));
}

std::string kindLabelOf(std::string_view key) {
    if (key.empty() || key == "aucun") return "aucun";
    if (const auto k = behaviorKindFrom(key)) return std::string(behaviorKindLabel(*k));
    return std::string(key);
}

} // namespace

// ============================================================== ce qui se voit ===
bool Runtime::simPageAvailable() const {
    if (!project_) return true;
    // Sur le poste d'exploitation : la case "Page Simulation" de la fiche du poste.
    const bool station = hooks_.station && hooks_.station().first;
    return !station || project_->station.simPage;
}

bool Runtime::simPageAllowed() const { return permitted("Administrer"); }

bool Runtime::slaveRead(const SimSlave& s) noexcept { return s.enabled && (s.read || !s.linked); }

bool Runtime::readOnSlave(std::string_view equipment) const {
    for (const auto& s : simSlaves(false))
        if (pub::same(s.equipment, equipment)) return slaveRead(s);
    return false;
}

std::string Runtime::simCardWhy(const SimSlave& s) {
    if (!slaveRead(s)) return {};
    if (!s.linked) return "L'IHM lit son esclave : pas encore de vrai appareil.";
    if (s.fallback)
        return "L'IHM lit l'esclave : le vrai ne r\xC3\xA9pond pas" + (s.realSilentSince > 0 ? " depuis " + clockOf(s.realSilentSince) : std::string{}) + ".";
    if (s.chosen) return "L'IHM lit l'esclave : choisi sur cette page.";
    return "L'IHM lit l'esclave : sa fiche le demande.";
}

std::size_t Runtime::simChosenIndex(const std::vector<SimSlave>& slaves) const {
    for (std::size_t i = 0; i < slaves.size(); ++i)
        if (pub::same(slaves[i].equipment, simChosen_)) return i;
    // Par defaut : le premier que l'IHM lit a la place d'un vrai appareil, sinon le premier.
    for (std::size_t i = 0; i < slaves.size(); ++i)
        if (slaves[i].linked && slaveRead(slaves[i])) return i;
    return 0;
}

void Runtime::simChooseDefault(const std::vector<SimSlave>& slaves) {
    if (slaves.empty()) return;
    const std::size_t i = simChosenIndex(slaves);
    if (!pub::same(slaves[i].equipment, simChosen_)) {
        simChosen_ = slaves[i].equipment;
        simScroll_ = 0;
    }
}

SimPageShape Runtime::simPageShape(const std::vector<SimSlave>& slaves) const {
    SimPageShape s;
    s.present = simPageAvailable();
    s.locked = !simPageAllowed();
    if (s.locked || !s.present) return s;
    // La ligne sous le segment : ce que lit l'IHM (violet), sinon pourquoi le segment est grise.
    for (const auto& sl : slaves)
        s.cards.push_back({sl.linked, !simCardWhy(sl).empty() || (sl.linked && sl.modeLocked && !sl.modeWhy.empty())});
    if (slaves.empty()) return s;
    s.chosen = simChosenIndex(slaves);
    s.listScroll = simListScroll_;
    const auto& cur = slaves[s.chosen];
    for (const auto& v : cur.values) s.rows.push_back({v.key, v.boolean, v.kind, v.forced});
    s.scroll = simScroll_;
    s.chosenLinked = cur.linked;
    return s;
}

void Runtime::simPageOpened(const std::string& source) {
    if (!simPageAllowed()) {
        // La carte du refus se montre ; l'ouverture refusee va au journal.
        event("Acc\xC3\xA8s refus\xC3\xA9", source.empty() ? std::string(kSource) : source,
              "page Simulation : permission \xC2\xAB Administrer \xC2\xBB requise ("
                  + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")");
        return;
    }
    simListScroll_ = SimPageShape::kAutoScroll;      // la liste montre la carte choisie
    simChooseDefault(simSlaves(false));
}

// ============================================================ une partie ===
bool Runtime::simPagePart(std::string_view part, double now) {
    if (!simPageAvailable()) return true;
    if (part == "connecter") {
        // Se connecter... : le menu natif de connexion ; connecte avec la permission
        // Administrer, la page revient (loginSubmit).
        openLoginMenu(LoginTab::Connexion, now, kSource);
        simAfterLogin_ = true;
        return true;
    }
    if (!simPageAllowed()) {
        // Rien ne part : le journal le dit, le menu aussi.
        if (part.empty()) return true;
        const std::string msg = "page Simulation : permission \xC2\xAB Administrer \xC2\xBB requise ("
                              + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", kSource, msg);
        systemSay(capitalized(msg), true);
        return true;
    }
    const auto slaves = simSlaves(true);
    simChooseDefault(slaves);
    const auto colon = part.find(':');
    const std::string how(part.substr(0, colon));
    const std::string rest = colon == std::string_view::npos ? std::string{} : std::string(part.substr(colon + 1));
    // "defiler:3", "defiler:-1", "defiler:=12" : borne a [0, count - 1].
    const auto moved = [&](std::size_t current, std::size_t count) {
        long long next = current == SimPageShape::kAutoScroll ? 0 : static_cast<long long>(current);
        if (!rest.empty() && rest[0] == '=') next = std::atoll(rest.c_str() + 1);
        else next += std::atoll(rest.c_str());
        const long long last = count > 0 ? static_cast<long long>(count) - 1 : 0;
        return static_cast<std::size_t>(std::clamp(next, 0LL, last));
    };
    const auto send = [&](SimSlaveCommand c, const std::string& said) {
        std::string why;
        if (!simSlaveCommand(c, now, &why)) {
            systemSay(capitalized(why.empty() ? std::string("refus\xC3\xA9") : why), true);
            return false;
        }
        ++simRevision_;
        systemSay(said, false);
        return true;
    };
    const auto choose = [&](std::size_t i) {
        if (i >= slaves.size()) return;
        if (!pub::same(slaves[i].equipment, simChosen_)) {
            simChosen_ = slaves[i].equipment;
            simScroll_ = 0;
            simListScroll_ = SimPageShape::kAutoScroll;     // sa carte reste a l'ecran
        }
        ++simRevision_;
    };

    // ---- la liste des esclaves
    if (how == "defiler_liste") {
        simListScroll_ = moved(simListScroll_, slaves.size());
        return true;
    }
    if (how == "esclave" || how == "carte") {
        const std::size_t i = static_cast<std::size_t>(std::max(0, std::atoi(rest.c_str())));
        if (i < slaves.size()) {
            choose(i);
            systemSay(slaves[i].name + " : " + slaves[i].state, false);
        }
        return true;
    }
    if (how == "source") {
        // "<i>:vrai|esclave|auto" : ce que lit l'IHM pour cet equipement.
        const auto c2 = rest.find(':');
        const std::size_t i = static_cast<std::size_t>(std::max(0, std::atoi(rest.substr(0, c2).c_str())));
        if (i >= slaves.size() || c2 == std::string::npos) return true;
        choose(i);
        const auto& s = slaves[i];
        if (s.modeLocked) {
            systemSay(s.equipment + " : " + (s.modeWhy.empty() ? std::string("la source ne se change pas ici") : s.modeWhy), true);
            return true;
        }
        const auto src = readSourceFrom(rest.substr(c2 + 1));
        if (!src) return true;
        const std::string said = *src == ReadSource::Real    ? "l'IHM lit le vrai appareil"
                                 : *src == ReadSource::Slave ? "l'IHM lit l'esclave simul\xC3\xA9"
                                                             : "automatique : le vrai ; l'esclave s'il ne r\xC3\xA9pond pas";
        (void)send({s.equipment, "source", {}, std::string(readSourceKey(*src))}, s.equipment + " : " + said);
        return true;
    }
    if (part == "animer_tout") {
        (void)send({{}, "tout_animer", {}, {}}, "Tous les esclaves : les valeurs sont anim\xC3\xA9" "es (sauf les cases forc\xC3\xA9" "es)");
        return true;
    }
    if (part == "arreter_tout") {
        (void)send({{}, "tout_arreter", {}, {}}, "Tous les esclaves : plus rien ne bouge");
        return true;
    }
    if (part == "deforcer_tout") {
        (void)send({{}, "deforcer_tout", {}, {}}, "Tous les esclaves : plus aucune case forc\xC3\xA9" "e");
        return true;
    }

    // ---- l'esclave choisi
    if (slaves.empty()) return true;
    const SimSlave& cur = slaves[simChosenIndex(slaves)];
    const std::string& eq = cur.equipment;
    if (how == "defiler") {
        simScroll_ = moved(simScroll_, cur.values.size());
        return true;
    }
    if (part == "marche") {
        const bool on = !cur.running;
        (void)send({eq, "marche", {}, on ? "1" : "0"}, cur.name + (on ? " : en marche" : " : arr\xC3\xAAt\xC3\xA9"));
        return true;
    }
    if (part == "repond") {
        const bool on = !cur.responds;
        (void)send({eq, "repond", {}, on ? "1" : "0"}, cur.name + (on ? " : r\xC3\xA9pond" : " : ne r\xC3\xA9pond plus (panne simul\xC3\xA9" "e)"));
        return true;
    }
    if (how == "exception") {
        const int code = simNextException(cur.exception, rest == "precedent" ? -1 : 1);
        (void)send({eq, "exception", {}, std::to_string(code)},
                   cur.name + (code ? " : exception " + simExceptionText(code) + " forc\xC3\xA9" "e \xC3\xA0 chaque requ\xC3\xAAte" : std::string(" : aucune exception")));
        return true;
    }
    if (part == "animer_esclave") {
        (void)send({eq, "tout_animer", {}, {}}, cur.name + " : ses valeurs sont anim\xC3\xA9" "es (sauf les cases forc\xC3\xA9" "es)");
        return true;
    }
    if (part == "arreter_esclave") {
        (void)send({eq, "tout_arreter", {}, {}}, cur.name + " : ses valeurs ne bougent plus");
        return true;
    }
    if (part == "deforcer_esclave") {
        (void)send({eq, "deforcer_tout", {}, {}}, cur.name + " : plus aucune case forc\xC3\xA9" "e");
        return true;
    }
    if (part == "revenir") {
        if (!cur.linked) {
            systemSay(eq + " est seulement simul\xC3\xA9 : il n'a pas de vrai appareil", true);
            return true;
        }
        (void)send({eq, "revenir", {}, {}}, eq + " : l'IHM lit de nouveau le vrai appareil");
        return true;
    }

    // ---- une ligne : "<partie>:<cle>" ou "<partie>:<cle>:<sens>"
    std::string key = rest, dirText;
    if (how != "animer" && how != "forcer") {
        const auto c2 = rest.rfind(':');
        if (c2 == std::string::npos) return true;
        key = rest.substr(0, c2);
        dirText = rest.substr(c2 + 1);
    }
    const int dir = dirText == "moins" || dirText == "precedent" ? -1 : 1;
    const SimSlaveValue* v = valueOf(cur, key);
    if (!v) return true;
    const std::string name = valueName(*v);
    if (how == "animer") {
        const bool on = !v->animated;
        (void)send({eq, "animer", v->key, on ? "1" : "0"}, name + (on ? " : anim\xC3\xA9" "e" : " : ne bouge plus"));
        return true;
    }
    if (how == "mouvement") {
        const std::string next = simNextKind(v->kind, v->boolean, dir);
        (void)send({eq, "mouvement", v->key, next}, name + (next == "aucun" ? std::string(" : aucun mouvement") : " : " + kindLabelOf(next)));
        return true;
    }
    if (how == "zone_min" || how == "zone_max") {
        double lo = std::min(v->low, v->high), hi = std::max(v->low, v->high);
        const double step = valueStep(*v, 0);
        if (how == "zone_min") lo = dir > 0 ? std::min(stepped(lo, step, 1), hi - step) : stepped(lo, step, -1);
        else hi = dir > 0 ? stepped(hi, step, 1) : std::max(stepped(hi, step, -1), lo + step);
        if (unsignedType(v->type) && !v->scaled) lo = std::max(0.0, lo);
        (void)send({eq, "zone", v->key, plainNumber(lo) + ";" + plainNumber(hi)},
                   name + " : zone " + frenchNumber(lo) + " \xC3\xA0 " + frenchNumber(hi) + (v->unit.empty() ? std::string{} : " " + v->unit));
        return true;
    }
    if (how == "periode") {
        const double p = simNextPeriod(v->period, dir);
        (void)send({eq, "periode", v->key, plainNumber(p)}, name + " : p\xC3\xA9riode " + frenchNumber(p) + " s");
        return true;
    }
    if (how == "forcer") {
        if (v->forced) {
            (void)send({eq, "forcer", v->key, {}}, name + " : d\xC3\xA9" "forc\xC3\xA9" "e (elle garde sa valeur, les \xC3\xA9" "critures passent)");
            return true;
        }
        const double at = v->boolean ? (v->number != 0 ? 1.0 : 0.0) : v->number;
        (void)send({eq, "forcer_valeur", v->key, plainNumber(at)}, name + " : forc\xC3\xA9" "e \xC3\xA0 " + frenchNumber(at));
        return true;
    }
    if (how == "forcee") {
        double x = v->forcedNumber;
        if (v->boolean) x = dir > 0 ? 1.0 : 0.0;
        else x = stepped(x, valueStep(*v, x), dir);
        if (unsignedType(v->type) && !v->scaled) x = std::max(0.0, x);
        (void)send({eq, "forcer_valeur", v->key, plainNumber(x)}, name + " : forc\xC3\xA9" "e \xC3\xA0 " + frenchNumber(x));
        return true;
    }
    return true;
}

// ========================================================= les variables SYS ===
bool Runtime::slaveSysValue(std::string_view n, sim::Value& out) const {
    const auto text = [&](std::string s) { out = sim::Value::text(std::move(s)); return true; };
    const auto flag = [&](bool b) { out = sim::Value::boolean(b); return true; };
    const auto integer = [&](long long v) { out = sim::Value::integer(sim::Type::Int, v); return true; };
    const auto dint = [&](long long v) { out = sim::Value::integer(sim::Type::DInt, v); return true; };
    const auto udint = [&](std::uint64_t v) {
        out = sim::Value::integer(sim::Type::UDInt, static_cast<long long>(std::min<std::uint64_t>(v, 4294967295ULL)));
        return true;
    };
    // SYS.Slave.<nom>.<membre>
    if (n.size() > 6 && pub::same(n.substr(0, 6), "Slave.")) {
        const std::string_view rest = n.substr(6);
        const auto dot = rest.find('.');
        if (dot == std::string_view::npos) return false;
        const auto* m = pub::slaveMember(rest.substr(dot + 1));
        if (!m) return false;
        const std::string_view key = rest.substr(0, dot);
        for (const auto& s : simSlaves(false)) {
            if (!pub::same(s.key, key)) continue;
            const std::string_view member = m->name;
            if (member == "Name") return text(s.name);
            if (member == "Kind") return text(s.linked ? "li\xC3\xA9" : "seulement simul\xC3\xA9");
            if (member == "Equipment") return text(s.equipment);
            if (member == "Running") return flag(s.running);
            if (member == "Responds") return flag(s.responds);
            if (member == "Exception") return integer(s.exception);
            if (member == "Read") return flag(slaveRead(s));
            if (member == "Fallback") return flag(s.fallback);
            if (member == "Mode") return text(s.mode);
            if (member == "RealOnline") return flag(s.realOnline);
            if (member == "Port") return dint(s.port);
            if (member == "Requests") return udint(s.requests);
            if (member == "RefusedWrites") return udint(s.refusedWrites);
            if (member == "Clients") return integer(s.clients);
            if (member == "Animated") return integer(s.animated);
            if (member == "Forced") return integer(s.forced);
            return false;
        }
        return false;
    }
    // Les compteurs de toute l'IHM.
    const auto slaves = simSlaves(false);
    long long running = 0, fallbacks = 0, animated = 0, forced = 0;
    std::string names;
    for (const auto& s : slaves) {
        running += s.running ? 1 : 0;
        fallbacks += s.read && s.fallback ? 1 : 0;
        animated += s.animated;
        forced += s.forced;
        if (slaveRead(s)) names += (names.empty() ? "" : ", ") + s.equipment;
    }
    if (pub::same(n, "SimSlaveCount")) return integer(static_cast<long long>(slaves.size()));
    if (pub::same(n, "SimSlavesRunning")) return integer(running);
    if (pub::same(n, "SimReads")) return flag(!names.empty());
    if (pub::same(n, "SimReadNames")) return text(names);
    if (pub::same(n, "SimFallbacks")) return integer(fallbacks);
    if (pub::same(n, "SimAnimated")) return integer(animated);
    if (pub::same(n, "SimForced")) return integer(forced);
    return false;
}

} // namespace hmi
