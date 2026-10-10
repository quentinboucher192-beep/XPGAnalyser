// =============================================================================
//  hmi/HmiRuntimeSystem.cpp - le menu natif "Parametres systeme" en marche
//                             (lot 10)
// -----------------------------------------------------------------------------
//  Le menu que l'IHM porte elle-meme (HmiSystemMenu.hpp) : l'ouvrir, appliquer
//  une partie cliquee, les reglages du poste et leurs effets (la veille, le
//  son, la deconnexion automatique, le clavier, l'heure de l'IHM), le
//  diagnostic lu dans les variables SYS.
//
//  LES REGLAGES PROTEGES : sous securite, veille, deconnexion, clavier, heure
//  et maintenance demandent la permission Administrer. Un refus est un
//  evenement "Acces refuse" (historique et journal) et le message du menu.
//  Chaque reglage change est ecrit au journal ("Luminosite : 70 %").
// =============================================================================
#include "HmiRuntime.hpp"
#include "../core/Edition.hpp"   // 1.12.2 : XPGAnalyser IHM n'a pas d'automate

#include "HmiPublicVars.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hmi {

namespace {

constexpr const char* kSource = "Param\xC3\xA8tres syst\xC3\xA8me";

// 45 s ; 3 min 05 s ; 2 h 03 min 05 s
std::string durationText(double seconds) {
    const long long s = static_cast<long long>(std::floor(std::max(0.0, seconds)));
    char b[48];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min %02lld s", s / 3600, (s / 60) % 60, s % 60);
    else if (s >= 60) std::snprintf(b, sizeof b, "%lld min %02lld s", s / 60, s % 60);
    else std::snprintf(b, sizeof b, "%lld s", s);
    return b;
}

// +00:05:00 ; -1 j 02:00:00
std::string offsetText(double seconds) {
    const long long total = std::llround(seconds);
    const char sign = total < 0 ? '-' : '+';
    long long a = total < 0 ? -total : total;
    const long long days = a / 86400;
    a %= 86400;
    char b[48];
    if (days > 0) std::snprintf(b, sizeof b, "%c%lld j %02lld:%02lld:%02lld", sign, days, a / 3600, (a / 60) % 60, a % 60);
    else std::snprintf(b, sizeof b, "%c%02lld:%02lld:%02lld", sign, a / 3600, (a / 60) % 60, a % 60);
    return b;
}

std::string counted(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Les minutes : jour, mois, annee, heure, minute (sans les secondes).
bool sameMinute(const DateTime& a, const DateTime& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour && a.minute == b.minute;
}

} // namespace

// ================================================================= ouvrir ===
void Runtime::openSystemMenu(int tab, double now, const std::string& source) {
    now_ = std::max(now_, now);
    if (!systemShown_) {
        systemMessage_.clear();
        systemMessageError_ = false;
    }
    systemShown_ = true;
    resetLogin();                   // lot 12 : un menu natif a la fois
    // 1.9 : l'onglet 2, la page Simulation - sauf sur un poste qui ne l'a pas.
    int wanted = tab == 1 ? 1 : tab == kSimulationTab ? kSimulationTab : 0;
    const bool missing = wanted == kSimulationTab && !simPageAvailable();
    if (missing) wanted = 0;
    if (wanted != systemTab_ && (wanted == kSimulationTab || systemTab_ == kSimulationTab)) {
        systemMessage_.clear();       // la page a sa propre ligne d'etat
        systemMessageError_ = false;
    }
    systemTab_ = wanted;
    systemScroll_ = 0;
    systemClock_.reset();
    log("Action", source.empty() ? std::string(kSource) : source,
        std::string("param\xC3\xA8tres syst\xC3\xA8me") + (systemTab_ == 1 ? " (diagnostic)" : systemTab_ == kSimulationTab ? " (simulation)" : ""));
    if (missing) systemSay("La page Simulation n'existe pas sur ce poste (Configuration \xE2\x80\xBA Poste d'exploitation)", true);
    if (systemTab_ == kSimulationTab) simPageOpened(source);
}

bool Runtime::settingAllowed(std::string_view key) const {
    bool guarded = true;
    if (const auto* spec = settingSpec(key)) guarded = spec->guarded;
    else if (const auto* m = maintenanceSpec(key)) guarded = m->guarded;
    return !guarded || permitted("Administrer");
}

void Runtime::systemSay(std::string text, bool error) {
    systemMessage_ = std::move(text);
    systemMessageError_ = error;
}

int Runtime::autoLogoutMinutes() const {
    if (settings_.autoLogoutMin >= 0) return settings_.autoLogoutMin;
    return project_ ? std::max(0, project_->security.autoLogoutMin) : 0;
}

DateTime Runtime::systemClock(bool* pending) const {
    if (pending) *pending = systemClock_.has_value();
    return systemClock_ ? *systemClock_ : dateTimeFromEpoch(epochOf(now_));
}

// ============================================================ une partie ===
void Runtime::systemPart(std::string_view part, double now) {
    now_ = std::max(now_, now);
    lastActivity_ = std::max(lastActivity_, now);          // un toucher
    if (!systemShown_) return;
    if (part == "fermer" || part == "dehors") {
        systemShown_ = false;
        systemClock_.reset();
        return;
    }
    if (part == "onglet:reglages" || part == "onglet:diagnostic") {
        if (systemTab_ == kSimulationTab) {
            systemMessage_.clear();       // la ligne d'etat de la page Simulation ne la suit pas
            systemMessageError_ = false;
        }
        systemTab_ = part == "onglet:diagnostic" ? 1 : 0;
        return;
    }
    // 1.9 : la page Simulation (HmiRuntimeSimPage.cpp).
    if (part == "onglet:simulation") {
        if (!simPageAvailable()) {
            systemSay("La page Simulation n'existe pas sur ce poste (Configuration \xE2\x80\xBA Poste d'exploitation)", true);
            return;
        }
        if (systemTab_ != kSimulationTab) {
            systemMessage_.clear();
            systemMessageError_ = false;
        }
        systemTab_ = kSimulationTab;
        simPageOpened({});
        return;
    }
    if (systemTab_ == kSimulationTab) {
        (void)simPagePart(part, now);
        return;
    }
    if (part.rfind("defiler:", 0) == 0) {
        const std::string arg(part.substr(8));
        const std::size_t rows = diagnosticRows();
        const long long last = rows > 0 ? static_cast<long long>(rows) - 1 : 0;
        long long next = static_cast<long long>(systemScroll_);
        if (!arg.empty() && arg[0] == '=') next = std::atoll(arg.c_str() + 1);
        else next += std::atoll(arg.c_str());
        systemScroll_ = static_cast<std::size_t>(std::clamp(next, 0LL, last));
        return;
    }
    const auto colon = part.find(':');
    if (colon == std::string_view::npos) return;
    const std::string how(part.substr(0, colon));
    const std::string key(part.substr(colon + 1));
    const auto refuse = [&](std::string_view what) {
        const std::string msg = std::string(what) + " : permission \xC2\xAB Administrer \xC2\xBB requise ("
                              + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", kSource, msg);
        systemSay(msg, true);
    };
    const int projectMin = project_ ? project_->security.autoLogoutMin : 0;

    // ---- l'heure de l'IHM : les fleches changent le reglage en cours, Appliquer le pose
    if (how == "heure") {
        if (!settingAllowed("heure")) { refuse("Date et heure"); return; }
        const DateTime ihm = dateTimeFromEpoch(epochOf(now_));
        const double posteEpoch = epochOf(now_) - settings_.clockOffset;
        if (key.rfind("plus:", 0) == 0 || key.rfind("moins:", 0) == 0) {
            const bool plus = key[0] == 'p';
            systemClock_ = stepDateTime(systemClock_.value_or(ihm), std::string_view(key).substr(plus ? 5 : 6), plus ? 1 : -1);
            systemSay("Date et heure : " + isoDateTime(*systemClock_, "date et heure", false) + " \xE2\x80\x94 Appliquer pour la r\xC3\xA9gler", false);
            return;
        }
        if (key == "maintenant") {
            systemClock_ = dateTimeFromEpoch(posteEpoch);
            systemSay("Date et heure : celle du poste, " + isoDateTime(*systemClock_, "date et heure", false)
                          + " \xE2\x80\x94 Appliquer pour la reprendre", false);
            return;
        }
        if (key != "appliquer") return;
        if (!systemClock_ || sameMinute(*systemClock_, ihm)) {
            systemClock_.reset();
            systemSay("Date et heure : inchang\xC3\xA9" "e", false);
            return;
        }
        DateTime d = *systemClock_;
        systemClock_.reset();
        if (sameMinute(d, dateTimeFromEpoch(posteEpoch))) {
            settings_.clockOffset = 0;
        } else {
            d.second = 0;                                   // la minute reglee commence
            settings_.clockOffset = epochFromDateTime(d) - posteEpoch;
        }
        const std::string shown = "Date et heure : " + isoDateTime(dateTimeFromEpoch(epochOf(now_)), "date et heure", true)
                                + (std::fabs(settings_.clockOffset) < 0.5 ? std::string(" (celle du poste)")
                                                                          : " (\xC3\xA9" "cart " + offsetText(settings_.clockOffset) + " avec le poste)");
        log("Action", kSource, shown);
        systemSay(shown, false);
        return;
    }

    // ---- la maintenance
    if (how == "action") {
        const MaintenanceSpec* spec = maintenanceSpec(key);
        if (!spec) return;
        if (!settingAllowed(key)) { refuse(spec->label); return; }
        if (key == "eteindre") {
            // Eteindre l'ecran : le menu se ferme, le prochain toucher le rallume.
            systemShown_ = false;
            systemClock_.reset();
            sleepNow_ = sleeping_ = true;
            log("Syst\xC3\xA8me", kSource, "\xC3\xA9" "cran \xC3\xA9teint");
        } else if (key == "journal") {
            const std::size_t n = journal_.size();
            journal_.clear();
            log("Action", kSource, "journal vid\xC3\xA9 (" + counted(static_cast<long long>(n), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ")");
            systemSay("Journal vid\xC3\xA9", false);
        } else if (key == "defaut") {
            settings_ = SystemSettings{};
            applyProjectDisplay();           // lot 13 : l'affichage, celui du projet
            log("Action", kSource, "r\xC3\xA9glages par d\xC3\xA9" "faut");
            systemSay("R\xC3\xA9glages par d\xC3\xA9" "faut : luminosit\xC3\xA9 100 %, son activ\xC3\xA9, volume 100 %, l'heure du poste", false);
        } else if (key == "redemarrer") {
            // L'ecran le fait, comme son bouton ; sans lui, le moteur repart seul.
            // Le journal repart de zero : la ligne vient apres.
            const double at = now_;
            if (hooks_.restart) {
                hooks_.restart();
            } else {
                stop(at);
                start(at);
            }
            log("Syst\xC3\xA8me", kSource, "IHM red\xC3\xA9marr\xC3\xA9" "e (r\xC3\xA9glages du poste gard\xC3\xA9s)");
        }
        return;
    }

    // ---- un reglage
    const auto* spec = settingSpec(key);
    if (!spec || spec->kind == SettingKind::Clock) return;
    if (!settingAllowed(key)) { refuse(spec->label); return; }
    SystemSettings next = settings_;
    if (!stepSetting(next, key, how)) {
        systemSay(std::string(spec->label) + " : " + settingText(settings_, key, projectMin) + " (d\xC3\xA9j\xC3\xA0 au bout)", false);
        return;
    }
    settings_ = next;
    const std::string shown = std::string(spec->label) + " : " + settingText(settings_, key, projectMin);
    log("Action", kSource, shown);
    systemSay(shown, false);
}

// ============================================================== la veille ===
bool Runtime::asleep(double now) const {
    if (!running_) return false;
    return sleepNow_ || (settings_.screensaverMin > 0 && now - lastActivity_ >= settings_.screensaverMin * 60.0);
}

bool Runtime::wake(double now) {
    now_ = std::max(now_, now);
    const bool was = asleep(now) || sleeping_;
    if (!was) return false;
    lastActivity_ = std::max(lastActivity_, now);
    sleeping_ = sleepNow_ = false;
    log("Syst\xC3\xA8me", kSource, "\xC3\xA9" "cran rallum\xC3\xA9");
    return true;
}

void Runtime::sleepTick(double now) {
    const bool s = asleep(now);
    if (s && !sleeping_) log("Syst\xC3\xA8me", kSource, "\xC3\xA9" "cran en veille (" + std::to_string(settings_.screensaverMin) + " min sans toucher)");
    sleeping_ = s;
}

// ============================================================ le diagnostic ===
std::vector<DiagGroup> Runtime::diagnostics() const {
    const auto val = [&](std::string_view n) {
        sim::Value v;
        (void)sysValue(n, v);
        return v;
    };
    const auto str = [&](std::string_view n) { return val(n).asString(); };
    const auto num = [&](std::string_view n) { return static_cast<long long>(val(n).asInteger()); };
    const auto row = [](DiagGroup& g, std::string label, std::string value, std::string_view sys, int tone = 0) {
        g.rows.push_back({std::move(label), std::move(value), sys.empty() ? std::string{} : "SYS." + std::string(sys), tone});
    };
    std::vector<DiagGroup> out;

    DiagGroup ihm{"IHM", {}};
    const std::string version = str("ProjectVersion");
    row(ihm, "Projet", str("ProjectName") + (version.empty() ? std::string{} : "  (version " + version + ")"), "ProjectName");
    row(ihm, "R\xC3\xA9solution", std::to_string(num("ScreenWidth")) + " x " + std::to_string(num("ScreenHeight")) + "  " + str("Orientation"),
        "ScreenWidth");
    const long long popups = num("PopupCount");
    const std::string view = str("CurrentView");
    row(ihm, "Vue courante", (view.empty() ? std::string("(aucune)") : view)
                                 + (popups > 0 ? "  + " + counted(popups, "popup", "popups") : std::string{}),
        "CurrentView");
    row(ihm, "En marche depuis", running_ ? durationText(now_ - startNow_) : std::string("arr\xC3\xAAt\xC3\xA9" "e"), "Uptime", running_ ? 0 : 1);
    row(ihm, "Cycle IHM", std::to_string(num("CycleTime")) + " ms  \xC2\xB7  " + counted(num("CycleCount"), "cycle", "cycles"), "CycleTime");
    const long long scriptErrors = num("ScriptErrorCount");
    row(ihm, "Scripts", counted(num("ScriptCount"), "script g\xC3\xA9n\xC3\xA9ral", "scripts g\xC3\xA9n\xC3\xA9raux")
                            + (scriptErrors > 0 ? ", " + std::to_string(scriptErrors) + " en erreur" : std::string{}),
        "ScriptErrorCount", scriptErrors > 0 ? 2 : 0);
    const long long errors = num("ErrorCount");
    row(ihm, "Journal", counted(num("JournalCount"), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ", " + counted(errors, "erreur", "erreurs"), "JournalCount",
        errors > 0 ? 1 : 0);
    const std::string lastError = str("LastError");
    row(ihm, "Derni\xC3\xA8re erreur", lastError.empty() ? std::string("aucune") : lastError, "LastError", lastError.empty() ? 3 : 2);
    out.push_back(std::move(ihm));

    if (core::hasApi()) {      // 1.12.2 : XPGAnalyser IHM n'a pas d'automate
    DiagGroup plc{"Automate", {}};
    const std::string status = str("PlcStatus");
    row(plc, "\xC3\x89tat", status, "PlcStatus", status == "En marche" ? 3 : status == "En d\xC3\xA9" "faut" ? 2 : status == "Absent" ? 0 : 1);
    const std::string plcProject = str("PlcProject");
    row(plc, "Projet", plcProject.empty() ? std::string("\xE2\x80\x94") : plcProject, "PlcProject");
    row(plc, "Cycles", std::to_string(num("PlcScanCount")), "PlcScanCount");
    const long long period = num("PlcCycleTime");
    row(plc, "P\xC3\xA9riode du cycle", period > 0 ? std::to_string(period) + " ms" : std::string("\xE2\x80\x94"), "PlcCycleTime");
    const long long forced = num("PlcForcedCount");
    row(plc, "Variables forc\xC3\xA9" "es", forced > 0 ? std::to_string(forced) : std::string("aucune"), "PlcForcedCount", forced > 0 ? 1 : 0);
    const std::string plcError = str("PlcError");
    row(plc, "D\xC3\xA9" "faut", plcError.empty() ? std::string("aucun") : plcError, "PlcError", plcError.empty() ? 0 : 2);
    out.push_back(std::move(plc));
    }

    DiagGroup usr{"Utilisateur", {}};
    const std::string login = str("UserName"), full = str("UserFullName"), group = str("UserGroup");
    row(usr, "Connect\xC3\xA9", login.empty() ? std::string("personne")
                                       : (full.empty() || full == login ? login : full + "  (" + login + ")"),
        "UserName");
    row(usr, "Groupe", group.empty() ? std::string("\xE2\x80\x94") : group + "  (niveau " + std::to_string(num("UserLevel")) + ")", "UserGroup");
    const bool security = val("SecurityEnabled").isTruthy();
    row(usr, "S\xC3\xA9" "curit\xC3\xA9", security ? std::string("active") : std::string("inactive : tout est permis"), "SecurityEnabled",
        security ? 3 : 1);
    row(usr, "Connect\xC3\xA9 depuis", login.empty() ? std::string("\xE2\x80\x94") : durationText(static_cast<double>(num("UserSessionTime")) / 1000.0),
        "UserSessionTime");
    const double remaining = autoLogoutRemaining(now_);
    row(usr, "D\xC3\xA9" "connexion dans", remaining < 0 ? std::string("jamais") : durationText(remaining), "AutoLogoutRemaining",
        remaining >= 0 && remaining < 60 ? 1 : 0);
    out.push_back(std::move(usr));

    DiagGroup alarms{"Alarmes", {}};
    const long long listed = num("AlarmCount"), active = num("AlarmActiveCount"), unack = num("AlarmUnackCount");
    row(alarms, "En cours", listed > 0 ? counted(listed, "alarme", "alarmes") + ", " + counted(active, "active", "actives")
                                       : std::string("aucune"),
        "AlarmCount", listed > 0 ? 1 : 3);
    row(alarms, "\xC3\x80 acquitter", unack > 0 ? std::to_string(unack) : std::string("aucune"), "AlarmUnackCount", unack > 0 ? 2 : 0);
    const long long highest = num("AlarmHighestPriority");
    row(alarms, "Priorit\xC3\xA9 la plus forte",
        highest > 0 ? std::to_string(highest) + " (" + std::string(alarmPriorityLabel(static_cast<int>(highest))) + ")" : std::string("\xE2\x80\x94"),
        "AlarmHighestPriority");
    const std::string lastAlarm = str("AlarmLastName");
    row(alarms, "Derni\xC3\xA8re apparue", lastAlarm.empty() ? std::string("aucune") : lastAlarm + "  (" + str("AlarmLastTime") + ")",
        "AlarmLastName");
    out.push_back(std::move(alarms));

    DiagGroup poste{"Poste", {}};
    const double offset = settings_.clockOffset;
    row(poste, "Date et heure", str("DateTime") + (std::fabs(offset) < 0.5 ? std::string{} : "  (\xC3\xA9" "cart " + offsetText(offset) + ")"),
        "DateTime", std::fabs(offset) < 0.5 ? 0 : 1);
    row(poste, "Ordinateur", str("ComputerName"), "ComputerName");
    row(poste, "Syst\xC3\xA8me", str("OsName") + "  \xC2\xB7  compte " + str("OsUser"), "OsName");
    row(poste, "Processeurs", std::to_string(num("ProcessorCount")), "ProcessorCount");
    row(poste, "Application", str("AppName") + " " + str("AppVersion"), "AppVersion");
    row(poste, "Ressources", counted(num("ResourceCount"), "ressource", "ressources") + ", "
                                 + counted(num("ExternalFileCount"), "fichier externe", "fichiers externes"),
        "ResourceCount");
    out.push_back(std::move(poste));

    // 1.9 : les esclaves simules (s'il y en a) - le ton 4 : violet, ce qui est lu en simule.
    const auto slaves = simSlaves(false);
    if (!slaves.empty()) {
        DiagGroup sim{"Esclaves simul\xC3\xA9s", {}};
        long long linked = 0;
        for (const auto& s : slaves) linked += s.linked ? 1 : 0;
        const long long count = num("SimSlaveCount"), running = num("SimSlavesRunning");
        const long long only = count - linked;
        std::string kinds;
        if (linked > 0) kinds += counted(linked, "li\xC3\xA9", "li\xC3\xA9s");
        if (only > 0) kinds += (kinds.empty() ? "" : ", ") + counted(only, "seulement simul\xC3\xA9", "seulement simul\xC3\xA9s");
        row(sim, "Esclaves", std::to_string(count) + (kinds.empty() ? std::string{} : "  (" + kinds + ")"), "SimSlaveCount");
        row(sim, "En marche", std::to_string(running) + " sur " + std::to_string(count), "SimSlavesRunning", running < count ? 1 : 3);
        const std::string names = str("SimReadNames");
        row(sim, "Lus par l'IHM", names.empty() ? std::string("aucun : l'IHM lit les vrais appareils") : names, "SimReadNames", names.empty() ? 0 : 4);
        const long long fallbacks = num("SimFallbacks");
        row(sim, "Bascules en cours", fallbacks > 0 ? counted(fallbacks, "bascule", "bascules") + " (le vrai ne r\xC3\xA9pond pas)" : std::string("aucune"),
            "SimFallbacks", fallbacks > 0 ? 1 : 0);
        row(sim, "Anim\xC3\xA9" "es \xC2\xB7 forc\xC3\xA9" "es", std::to_string(num("SimAnimated")) + " \xC2\xB7 " + std::to_string(num("SimForced")),
            "SimAnimated");
        out.push_back(std::move(sim));
    }
    return out;
}

std::size_t Runtime::diagnosticRows() const {
    std::size_t n = 0;
    for (const auto& g : diagnostics()) n += 1 + g.rows.size();
    return n;
}

// ================================================================= le son ===
bool Runtime::playSound(const std::string& name, const std::string& source) {
    if (!settings_.soundOn) {
        log("Action", source, "son : " + name + " (son coup\xC3\xA9)");
        return false;
    }
    ++soundsPlayed_;
    lastSound_ = name;
    log("Action", source, "son : " + name);
    if (hooks_.playSound) hooks_.playSound(name);
    return true;
}

} // namespace hmi
