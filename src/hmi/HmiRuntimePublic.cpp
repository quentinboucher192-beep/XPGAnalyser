// =============================================================================
//  hmi/HmiRuntimePublic.cpp - les variables systeme et d'instances en marche
//                             (lot 9)
// -----------------------------------------------------------------------------
//  SYS.* : ce que le moteur sait (l'utilisateur, l'heure, la vue, les alarmes,
//  l'automate...), calcule a chaque lecture.
//
//  Vue.Objet.Propriete : la propriete TELLE QU'ELLE EST EN MARCHE - ecrite
//  (figee), sinon son expression, son texte a trous ou la variable que montre
//  l'objet, sinon sa valeur statique. Les ecritures sont gardees ici et posees
//  sur la vue composee (viewOf) : le dessin, les clics et les expressions les
//  voient au prochain passage du moteur. Une ecriture sur un ecran modele (un
//  en-tete, un pied de page) vaut pour toutes les vues qui l'empruntent ; sur
//  une vue, pour elle seule.
// =============================================================================
#include "HmiAlarmGroups.hpp"     // 1.11.1 : AlarmLinkedGroup
#include "HmiObjectAlarms.hpp"
#include "HmiRuntime.hpp"

#include "HmiLanguages.hpp"
#include "HmiLive.hpp"
#include "HmiPublicVars.hpp"
#include "HmiPolicy.hpp"
#include "HmiTemplates.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>
#if !defined(_WIN32)
#include <unistd.h>
#endif

// 1.8.0 : la version vient de core/Version.hpp (la meme partout).
#include "../core/Version.hpp"
#ifndef XPG_ANALYZER_VERSION
#define XPG_ANALYZER_VERSION "1.0.0"
#endif

namespace hmi {

namespace {

constexpr int kMaxPublicDepth = 4;     // une propriete qui se lit elle-meme (par son texte)

std::tm localTm(double epoch) {
    const std::time_t secs = static_cast<std::time_t>(std::floor(epoch));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    return tm;
}

std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

// ---- Lot API 8 : Windows (MinGW) - la semaine ISO et l'ecart a UTC ----
// msvcrt.dll (la CRT de MinGW) ne connait pas "%V" (strftime rendait "" : la semaine valait 0)
// et son "%z" est le NOM du fuseau, pas "+0200" (la CRT de Visual Studio fait les deux) :
// calcules ici, memes resultats que glibc (verifie sur 1970-2100, 8 fuseaux).
int isoWeek(const std::tm& tm) {
    const auto weeks = [](int y) {       // 53 si le 31 decembre est un jeudi, ou celui d'avant un mercredi
        const auto p = [](int a) { return (a + a / 4 - a / 100 + a / 400) % 7; };
        return (p(y) == 4 || p(y - 1) == 3) ? 53 : 52;
    };
    const int year = tm.tm_year + 1900;
    const int week = (tm.tm_yday - (tm.tm_wday + 6) % 7 + 10) / 7;
    if (week < 1) return weeks(year - 1);
    if (week > weeks(year)) return 1;
    return week;
}

std::string utcOffset(double epoch, const std::tm& local) {    // "+02:00"
    const std::time_t secs = static_cast<std::time_t>(std::floor(epoch));
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &secs);
#else
    gmtime_r(&secs, &g);
#endif
    int days = local.tm_yday - g.tm_yday;
    if (local.tm_year != g.tm_year) days = local.tm_year > g.tm_year ? 1 : -1;
    const int minutes = days * 1440 + (local.tm_hour - g.tm_hour) * 60 + (local.tm_min - g.tm_min);
    const int a = minutes < 0 ? -minutes : minutes;
    return std::string(minutes < 0 ? "-" : "+") + two(a / 60) + ":" + two(a % 60);
}
// ---- fin Lot API 8 : la semaine ISO et l'ecart a UTC ----

std::string hostName() {
#if defined(_WIN32)
    if (const char* n = std::getenv("COMPUTERNAME")) return n;
    return {};
#else
    char b[256] = {};
    if (gethostname(b, sizeof b - 1) == 0) return b;
    return {};
#endif
}

std::string osUser() {
    for (const char* k : {"USERNAME", "USER", "LOGNAME"})
        if (const char* v = std::getenv(k); v && *v) return v;
    return {};
}

// Une valeur ecrite, rendue dans le type de sa propriete.
sim::Value typedValue(std::string_view key, const std::string& text) {
    const std::string type = pub::keyType(key, text);
    double n = 0;
    if (type == "BOOL") return sim::Value::boolean(parseBool(text, false));
    if (type == "INT" && parseNumber(text, n)) return sim::Value::integer(sim::Type::Int, static_cast<std::int64_t>(std::llround(n)));
    if (type == "REAL" && parseNumber(text, n)) return sim::Value::real(n);
    return sim::Value::text(text);
}

// Ce qu'on garde d'une valeur ecrite : son texte (TRUE / FALSE, un nombre, le texte).
std::string storedText(const sim::Value& v) {
    if (v.type() == sim::Type::String) return v.asString();
    if (v.type() == sim::Type::Bool) return v.isTruthy() ? "TRUE" : "FALSE";
    if (v.type() == sim::Type::Real) return formatNumber(v.asReal());
    if (sim::isInteger(v.type())) return std::to_string(v.asInteger());
    return formatValue(v);
}

// Un litteral ST qui vaut ce texte : 12, TRUE, 'Azote' (l'apostrophe doublee en $').
std::string literalOf(const std::string& text) {
    double n = 0;
    if (parseNumber(text, n) && text.find(',') == std::string::npos) return formatNumber(n);
    if (pub::same(text, "TRUE") || pub::same(text, "FALSE")) return text;
    std::string out = "'";
    for (const char c : text) out += c == '\'' ? std::string("$'") : std::string(1, c);
    return out + "'";
}

// Une propriete ecrite, posee sur un objet de la vue composee. La valeur d'une
// commande ou d'un afficheur relie a sa variable devient une expression
// constante (sinon le lien la reprendrait) ; le reste, une valeur statique
// (et l'expression tombe).
void putOverride(Object& o, const std::string& key, std::string value, double dy) {
    double y = 0;
    if (key == "y" && dy != 0 && parseNumber(value, y)) value = formatNumber(y + dy);
    const bool linked = key == "value" && !autoValueSource(o).empty();
    Prop* p = o.find(key);
    if (!p) {
        o.props.push_back(Prop{key, value, {}});
        p = &o.props.back();
    }
    p->value = value;
    p->expr = linked ? literalOf(value) : std::string{};
}

const std::string kFrenchDays[7] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
const std::string kFrenchMonths[12] = {"janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin",
                                       "juillet", "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre"};

} // namespace

// ============================================================ les ecritures ===
std::size_t Runtime::overrideCount() const noexcept {
    std::size_t n = backgrounds_.size();
    for (const auto& [_, props] : overrides_) n += props.size();
    return n;
}

const std::string* Runtime::overrideOf(Id view, Id object, std::string_view key) const {
    const auto look = [&](Id v) -> const std::string* {
        const auto it = overrides_.find({v, object});
        if (it == overrides_.end()) return nullptr;
        const auto p = it->second.find(std::string(key));
        return p == it->second.end() ? nullptr : &p->second;
    };
    if (const auto* s = look(view)) return s;
    // Un objet emprunte : ecrit sur son modele.
    const View* v = project_ ? project_->view(view) : nullptr;
    if (!v) return nullptr;
    const auto chain = templateChain(*project_, *v);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        if (const auto* s = look((*it)->id)) return s;
    if (const View* h = headerOf(*project_, *v))
        if (const auto* s = look(h->id)) return s;
    if (const View* f = footerOf(*project_, *v))
        if (const auto* s = look(f->id)) return s;
    return nullptr;
}

bool Runtime::overridden(const View& v) const {
    if (backgrounds_.count(v.id)) return true;
    const auto it = overrides_.lower_bound({v.id, 0});
    return it != overrides_.end() && it->first.first == v.id;
}

void Runtime::applyOverrides(View& c, const View& v, const View* only) const {
    if (!project_ || (overrides_.empty() && backgrounds_.empty())) return;
    const auto from = [&](Id owner, double dy) {
        for (auto it = overrides_.lower_bound({owner, 0}); it != overrides_.end() && it->first.first == owner; ++it)
            if (Object* o = c.object(it->first.second); o && !(only && only->object(it->first.second)))
                for (const auto& [key, value] : it->second) putOverride(*o, key, value, dy);
    };
    for (const View* t : templateChain(*project_, v)) from(t->id, 0);
    if (const View* h = headerOf(*project_, v)) from(h->id, 0);
    if (const View* f = footerOf(*project_, v)) from(f->id, static_cast<double>(v.height - f->height));
    from(v.id, 0);
    if (const auto it = backgrounds_.find(v.id); it != backgrounds_.end()) c.background = it->second;
}

bool Runtime::viewOpen(Id view) const {
    return view != kNoId && (view == current_ || std::find(popups_.begin(), popups_.end(), view) != popups_.end());
}

// ================================================================ systeme ====
bool Runtime::sysValue(std::string_view wanted, sim::Value& out) const {
    // 1.9 : SYS.Slave.<nom>.<membre> - une structure par esclave simule (HmiRuntimeSimPage.cpp).
    if (wanted.size() > 6 && pub::same(wanted.substr(0, 6), "Slave.")) return slaveSysValue(wanted, out);
    // 1.11.23 : SYS.Key.<touche> - la touche est tenue (HmiRuntimeInput.cpp).
    if (wanted.size() > 4 && pub::same(wanted.substr(0, 4), "Key.")) return keySysValue(wanted.substr(4), out);
    const pub::SysVar* sv = pub::sysVar(wanted);
    if (!sv) return false;
    const std::string_view n = sv->name;
    if (sv->domain == 13) return slaveSysValue(n, out);   // 1.9 : SYS.Sim* (les esclaves simules)
    if (sv->domain == pub::kInputDomain) return inputSysValue(n, out);   // 1.11.23 : la souris et le clavier
    const auto text = [&](std::string s) { out = sim::Value::text(std::move(s)); return true; };
    const auto flag = [&](bool b) { out = sim::Value::boolean(b); return true; };
    const auto integer = [&](long long v) { out = sim::Value::integer(sim::Type::Int, v); return true; };
    const auto dint = [&](long long v) { out = sim::Value::integer(sim::Type::DInt, v); return true; };
    const auto udint = [&](long long v) { out = sim::Value::integer(sim::Type::UDInt, std::max(0LL, v)); return true; };
    const auto duration = [&](double seconds) {
        out = sim::Value::time(static_cast<std::int64_t>(std::llround(std::max(0.0, seconds) * 1000.0)));
        return true;
    };
    const Project* p = project_;
    const auto viewName = [&](Id id) { const View* v = p ? p->view(id) : nullptr; return v ? v->name : std::string{}; };

    // ---- l'utilisateur et la securite
    const User* u = user();
    const UserGroup* g = nullptr;
    if (u && p)
        for (const auto& gr : p->security.groups) if (gr.id == u->group) g = &gr;
    if (n == "UserName") return text(user_);
    if (n == "UserFullName") return text(u ? u->fullName : std::string{});
    if (n == "UserGroup") return text(g ? g->name : std::string{});
    if (n == "UserLevel") return integer(level());
    if (n == "UserRoles") {
        std::string s;
        if (g) for (const auto& r : g->roles) s += (s.empty() ? "" : ";") + r;
        return text(s);
    }
    if (n == "UserPermissions") {
        std::string s;
        if (u && p)
            for (const auto& perm : permissionNames())
                if (userHas(*p, *u, perm)) s += (s.empty() ? "" : ";") + perm;
        return text(s);
    }
    if (n == "UserLoggedIn") return flag(!user_.empty());
    if (n == "UserLoginTime") return text(user_.empty() ? std::string{} : dateStampOf(loginAt_).substr(0, 19));
    if (n == "UserSessionTime") return duration(user_.empty() ? 0.0 : now_ - loginAt_);
    if (n == "AutoLogoutRemaining") return duration(std::max(0.0, autoLogoutRemaining(now_)));
    if (n == "SecurityEnabled") return flag(p && p->security.enabled);
    if (n == "StartUser") return text(p ? p->security.startUser : std::string{});
    if (n == "UserCount") return integer(p ? static_cast<long long>(p->security.users.size()) : 0);
    if (n == "CanNavigate") return flag(permitted("Naviguer"));
    if (n == "CanControl") return flag(permitted("Piloter"));
    if (n == "CanAcknowledge") return flag(permitted("Acquitter"));
    if (n == "CanUseRecipes") return flag(permitted("Recettes"));
    if (n == "CanRunScripts") return flag(permitted("Scripts"));
    if (n == "CanAdminister") return flag(permitted("Administrer"));
    // Lot 12 : le menu de connexion.
    if (n == "LoginMenuOpen") return flag(loginShown_);
    if (n == "LoginMenuTab") return text(loginShown_ ? std::string(loginTabLabel(loginTab_)) : std::string{});
    if (n == "LoginMenuTabs") {
        std::string s;
        for (const LoginTab t : loginTabs()) s += (s.empty() ? "" : ";") + std::string(loginTabLabel(t));
        return text(s);
    }
    // Lot 13 : la securite renforcee.
    if (n == "AutoLogoutWarning") return flag(logoutWarning(now_));
    if (n == "PasswordDaysLeft") {
        if (!u || !p) return integer(-1);
        const auto left = passwordDaysLeft(p->security, *u, dayOf(dateStampOf(now_)));
        return integer(left ? *left : -1);
    }
    if (n == "PasswordRenewal") return flag(renewalPending());
    if (n == "LockedAccountCount") {
        long long locked = 0;
        const double e = epochOf(now_);
        for (const auto& a : history_ ? history_->accounts : accountsLocal_) locked += a.locked(e) ? 1 : 0;
        return integer(locked);
    }
    if (n == "SignaturePending") return flag(signature_.has_value());
    if (n == "LastSignature") return text(lastSignature_);
    if (n == "BadgeLogin") return flag(p && p->security.badgeLogin);
    if (n == "AuditEnabled") return flag(auditOn());
    if (n == "AuditCount") return dint(static_cast<long long>(auditTrail().size()));

    // ---- la date et l'heure (celles du poste, a l'instant IHM)
    // Lot 10 : l'heure de l'IHM, avec l'ecart regle dans Parametres systeme.
    const double wall = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0;
    const double epoch = running_ ? epochOf(now_) : wall + settings_.clockOffset;
    if (sv->domain == 1) {
        const std::tm tm = localTm(epoch);
        const int wd = (tm.tm_wday + 6) % 7;            // 0 lundi
        if (n == "DateTime" || n == "Date" || n == "Time") {
            const std::string date = std::to_string(tm.tm_year + 1900) + "-" + two(tm.tm_mon + 1) + "-" + two(tm.tm_mday);
            const std::string time = two(tm.tm_hour) + ":" + two(tm.tm_min) + ":" + two(tm.tm_sec);
            return text(n == "Date" ? date : n == "Time" ? time : date + " " + time);
        }
        if (n == "Year") return integer(tm.tm_year + 1900);
        if (n == "Month") return integer(tm.tm_mon + 1);
        if (n == "Day") return integer(tm.tm_mday);
        if (n == "Hour") return integer(tm.tm_hour);
        if (n == "Minute") return integer(tm.tm_min);
        if (n == "Second") return integer(tm.tm_sec);
        if (n == "Millisecond") return integer(static_cast<long long>(std::floor((epoch - std::floor(epoch)) * 1000.0)) % 1000);
        if (n == "WeekDay") return integer(wd + 1);
        if (n == "WeekDayName") return text(kFrenchDays[wd]);
        if (n == "MonthName") return text(kFrenchMonths[tm.tm_mon]);
        if (n == "WeekNumber") return integer(isoWeek(tm));            // Lot API 8 : pas "%V" (MinGW)
        if (n == "DayOfYear") return integer(tm.tm_yday + 1);
        if (n == "UnixTime") return udint(static_cast<long long>(std::floor(epoch)));
        if (n == "TimeZone") return text("UTC" + utcOffset(epoch, tm));  // Lot API 8 : pas "%z" (MinGW)
        if (n == "DaylightSaving") return flag(tm.tm_isdst > 0);
    }

    // ---- les vues et les popups
    if (n == "CurrentView") return text(viewName(current_));
    if (n == "PreviousView") return text(viewName(previousView_));
    if (n == "StartView") return text(p ? viewName(p->config.startView) : std::string{});
    if (n == "TopView") return text(viewName(topView()));
    if (n == "PopupCount") return integer(static_cast<long long>(popups_.size()));
    if (n == "TopPopup") return text(popups_.empty() ? std::string{} : viewName(popups_.back()));
    if (n == "ModalOpen") {
        bool modal = false;
        for (const Id id : popups_)
            if (const View* v = p ? p->view(id) : nullptr) modal = modal || popupSettingsOf(*v).modal;
        return flag(modal);
    }
    if (n == "ViewCount") return integer(p ? static_cast<long long>(p->views.size()) : 0);
    if (n == "NavigationCount") return dint(navigations_);
    if (n == "TransitionRunning") return flag(animation_.has_value());
    if (n == "FocusedObject" || n == "PressedObject") {
        const Id id = n == "FocusedObject" ? focused_ : pressed_;
        const View* v = id != kNoId ? shownViewOf(id) : nullptr;
        const Object* o = v ? v->object(id) : nullptr;
        return text(o ? v->name + "." + o->name : std::string{});
    }
    if (n == "ScreenWidth") return integer(p ? p->config.width : 0);
    if (n == "ScreenHeight") return integer(p ? p->config.height : 0);
    if (n == "Orientation") return text(p ? p->config.orientation : std::string{});
    // lot 12
    if (n == "CanGoBack") return flag(!back_.empty());
    if (n == "CanGoForward") return flag(!forward_.empty());
    if (n == "HistoryDepth") return integer(static_cast<long long>(back_.size()));
    if (n == "HomeView") return text(viewName(homeView()));
    if (n == "NavigationPath") return text(navigationPath());
    if (n == "ViewZoom") { out = sim::Value::real(viewZoom_); return true; }

    // ---- les alarmes
    if (n == "AlarmCount") return integer(static_cast<long long>(alarms_.size()));
    if (n == "AlarmActiveCount") {
        long long c = 0;
        for (const auto& a : alarms_) c += a.active;
        return integer(c);
    }
    if (n == "AlarmUnackCount") return integer(static_cast<long long>(unacknowledged()));
    if (n == "AlarmHighestPriority") return integer(highestPriority());
    if (n == "AlarmLastName") return text(lastAlarm_);
    if (n == "AlarmLastMessage") return text(lastAlarmMessage_);
    if (n == "AlarmLastTime") return text(lastAlarmAt_);
    if (n == "AlarmDefinedCount") return integer(p ? static_cast<long long>(p->alarms.size()) : 0);
    if (n == "AlarmHistoryCount") return integer(static_cast<long long>(closed_.size()));
    // lot 11 : mises de cote, silence, alarme choisie
    if (n == "AlarmShelvedCount") return integer(static_cast<long long>(shelved_.size()));
    if (n == "AlarmSilenced") return flag(silenced_);
    if (n == "AlarmSelected") return text(selectedAlarm_);
    if (n == "AlarmZoneSelected") return text(selectedZone_);

    // ---- les recettes
    if (n == "RecipeCount") return integer(p ? static_cast<long long>(p->recipes.size()) : 0);
    if (n == "RecipeRecordCount") {
        long long c = 0;
        if (p) for (const auto& r : p->recipes) c += static_cast<long long>(r.records.size());
        return integer(c);
    }
    if (n == "RecipeLastApplied") return text(lastRecipe_);
    if (n == "RecipeLastTime") return text(lastRecipeAt_);
    if (n == "RecipeApplyCount") return integer(recipeApplies_);

    // ---- les historiques et le journal
    if (n == "HistoryAlarms") return flag(p && p->history.alarms);
    if (n == "HistoryEvents") return flag(p && p->history.events);
    if (n == "HistorySystem") return flag(p && p->history.system);
    if (n == "ArchivedVariableCount") return integer(p ? static_cast<long long>(p->history.archived.size()) : 0);
    if (n == "EventCount") return integer(static_cast<long long>(events_.size()));
    if (n == "JournalCount") return integer(static_cast<long long>(journal_.size()));
    if (n == "LastJournal") return text(journal_.empty() ? std::string{} : journal_.back().message);
    if (n == "LastExport") return text(lastExport_);          // lot 11
    if (n == "ExportCount") return integer(exports_);
    // 1.12.3 : comptees depuis le demarrage - le nombre ne redescend plus quand la memoire oublie
    // les vieilles lignes du journal.
    if (n == "ErrorCount") return integer(errorCount_);
    if (n == "LastError") return text(lastError_);

    // ---- l'automate
    // Lot 14 : relie a un automate reel (Modbus TCP), il est "connecte" quand il
    // repond ; son etat de marche ne se lit pas par la liaison.
    if (sv->domain == 6 && link()) {
        sim::Value c;
        (void)commSysValue("CommConnected", c);
        if (n == "PlcConnected") return flag(c.isTruthy());
        if (n == "PlcStatus") return text(c.isTruthy() ? "Connect\xC3\xA9" : "Absent");
        if (n == "PlcError") return commSysValue("CommLastError", out);
        if (n == "PlcProject") {
            PlcStatus st;
            if (hooks_.plcStatus) st = hooks_.plcStatus();
            return text(st.project);
        }
        if (n == "PlcRunning" || n == "PlcPaused") return flag(false);
        if (n == "PlcScanCount") return udint(0);
        if (n == "PlcCycleTime" || n == "PlcForcedCount") return integer(0);
    }
    if (n.rfind("Equip", 0) == 0) return equipSysValue(n, out);   // lot 15 : les equipements du reseau
    if (sv->domain == 12) return commSysValue(n, out);   // lot 14 : la communication
    if (n.rfind("Notify", 0) == 0 || n == "ReportLast" || n == "WebClients") return notifySysValue(n, out);   // lot 14
    if (sv->domain == 6) {
        PlcStatus st;
        if (hooks_.plcStatus) st = hooks_.plcStatus();
        else st.attached = plc_ != nullptr;
        if (n == "PlcConnected") return flag(plc_ != nullptr && st.attached);
        if (n == "PlcRunning") return flag(st.running);
        if (n == "PlcPaused") return flag(st.paused);
        if (n == "PlcStatus") {
            if (!plc_ || !st.attached) return text("Absent");
            if (st.halted) return text("En d\xC3\xA9" "faut");
            return text(st.running ? "En marche" : st.paused ? "En pause" : "Arr\xC3\xAAt\xC3\xA9");
        }
        if (n == "PlcScanCount") return udint(static_cast<long long>(st.scans));
        if (n == "PlcCycleTime") return integer(st.cycleMs);
        if (n == "PlcForcedCount") return integer(st.forced);
        if (n == "PlcError") return text(st.error);
        if (n == "PlcProject") return text(st.project);
    }

    // ---- l'IHM en marche
    if (n == "Running") return flag(running_);
    if (n == "StationMode" || n == "StationScreens") {                 // lot 14 : le poste d'exploitation
        const auto st = hooks_.station ? hooks_.station() : std::pair<bool, int>{false, 1};
        return n == "StationMode" ? flag(st.first) : integer(st.first ? std::max(1, st.second) : 1);
    }
    if (n == "StartTime") return text(running_ ? dateStampOf(startNow_).substr(0, 19) : std::string{});
    if (n == "Uptime") return duration(running_ ? now_ - startNow_ : 0.0);
    if (n == "UptimeSeconds") return dint(running_ ? static_cast<long long>(std::floor(now_ - startNow_)) : 0);
    if (n == "CycleTime") return integer(p ? p->config.cycleMs : 0);
    if (n == "CycleCount") return dint(cycles_);
    if (n == "IdleTime") return duration(now_ - lastActivity_);
    if (n == "ScriptCount") return integer(p ? static_cast<long long>(p->programs.scripts.size()) : 0);
    if (n == "ScriptErrorCount") {
        long long c = 0;
        for (const auto& [_, e] : errors_) c += !e.empty();
        return integer(c);
    }
    if (n == "SoundCount") return integer(static_cast<long long>(soundsPlayed_));
    if (n == "LastSound") return text(lastSound_);
    if (n == "OverrideCount") return integer(static_cast<long long>(overrideCount()));
    // Lot 13 : les performances (onglet Performances de la simulation).
    if (n == "CycleLoad") { out = sim::Value::real(std::round(perf_.cycle.last * 1000.0) / 1000.0); return true; }
    if (n == "CycleLoadMax") { out = sim::Value::real(std::round(perf_.cycle.max * 1000.0) / 1000.0); return true; }
    if (n == "CycleOverruns") return dint(static_cast<long long>(perf_.overruns));
    if (n == "PaintTime") { out = sim::Value::real(std::round(perf_.paint.last * 1000.0) / 1000.0); return true; }
    if (n == "ExpressionCount") return integer(static_cast<long long>(perf_.expressions));

    // ---- le projet
    if (n == "ProjectName") return text(p ? p->config.name : std::string{});
    if (n == "ProjectDescription") return text(p ? p->config.description : std::string{});
    if (n == "ProjectVersion") return text(p ? p->config.version : std::string{});
    if (n == "ProjectAuthor") return text(p ? p->config.author : std::string{});
    if (n == "ProjectCreated") return text(p ? p->config.created : std::string{});
    if (n == "ProjectModified") return text(p ? p->config.modified : std::string{});
    if (n == "VariableCount") return integer(p ? static_cast<long long>(p->programs.variables.size()) : 0);
    if (n == "FunctionCount") return integer(p ? static_cast<long long>(p->programs.functions.size()) : 0);
    if (n == "ObjectCount") {
        long long c = 0;
        if (p) for (const auto& v : p->views) c += static_cast<long long>(v.objects.size());
        return integer(c);
    }

    // ---- le poste et l'application
    if (n == "ComputerName") return text(hostName());
#if defined(_WIN32)
    if (n == "OsName") return text("Windows");
#elif defined(__APPLE__)
    if (n == "OsName") return text("macOS");
#else
    if (n == "OsName") return text("Linux");
#endif
    if (n == "OsUser") return text(osUser());
    if (n == "ProcessorCount") return integer(static_cast<long long>(std::max(1u, std::thread::hardware_concurrency())));
    if (n == "AppName") return text("XpgAnalyzer");
    if (n == "AppVersion") return text(XPG_ANALYZER_VERSION);
    // lot 13 : la langue de l'IHM (Configuration > Langues ; SYS.Language s'ecrit)
    if (n == "Language") return text(!language_.empty() ? language_ : p ? p->languages.source() : std::string("fr"));
    if (n == "LanguageName") {
        const std::string code = !language_.empty() ? language_ : p ? p->languages.source() : std::string("fr");
        const Language* l = p ? p->languages.find(code) : nullptr;
        return text(l && !l->name.empty() ? l->name : languageName(code));
    }
    if (n == "LanguageCount") return integer(p ? static_cast<long long>(p->languages.list.size()) : 1);
    if (n == "LanguageList") {
        std::string all;
        if (p) for (const auto& l : p->languages.list) all += (all.empty() ? "" : ";") + l.code;
        return text(all.empty() ? std::string("fr") : all);
    }
    if (n == "LanguageChanges") return integer(static_cast<long long>(languageChanges_));
    // lot 13 : l'affichage
    if (n == "TextScale") return integer(settings_.textScale);
    if (n == "ColorMode") return text(settings_.colorMode);
    if (n == "StatusSymbols") return flag(settings_.symbols);
    if (n == "Theme") return text(settings_.theme);

    // ---- les ressources
    if (sv->domain == 10) {
        const auto countOf = [&](MediaKind k) {
            long long c = 0;
            if (p) for (const auto& r : p->assets.resources) c += r.kind() == k;
            return c;
        };
        if (n == "ResourceCount") return integer(p ? static_cast<long long>(p->assets.resources.size()) : 0);
        if (n == "ImageCount") return integer(countOf(MediaKind::Image));
        if (n == "SoundResourceCount") return integer(countOf(MediaKind::Sound));
        if (n == "VideoCount") return integer(countOf(MediaKind::Video));
        if (n == "FontCount") return integer(countOf(MediaKind::Font));
        if (n == "ExternalFileCount") return integer(p ? static_cast<long long>(p->assets.files.size()) : 0);
    }

    // ---- lot 10 : les parametres systeme (le menu natif, les reglages du poste)
    if (sv->domain == 11) {
        if (n == "SystemMenuOpen") return flag(systemShown_);
        if (n == "SystemMenuTab") return text(!systemShown_ ? std::string{} : std::string(systemTabLabel(systemTab_)));   // 1.9 : Simulation
        if (n == "Brightness") return integer(settings_.brightness);
        if (n == "ScreenSaverMinutes") return integer(settings_.screensaverMin);
        if (n == "ScreenSaverActive") return flag(asleep(now_));
        if (n == "SoundEnabled") return flag(settings_.soundOn);
        if (n == "Volume") return integer(settings_.volume);
        if (n == "AutoLogoutMinutes") return integer(autoLogoutMinutes());
        if (n == "AutoLogoutFromProject") return flag(settings_.autoLogoutMin < 0);
        if (n == "KeyboardMode") return text(settings_.keyboard);
        if (n == "ClockOffset") return dint(std::llround(settings_.clockOffset));
        if (n == "DeviceDateTime") {
            const std::tm tm = localTm(running_ ? epochOf(now_) - settings_.clockOffset : wall);
            return text(std::to_string(tm.tm_year + 1900) + "-" + two(tm.tm_mon + 1) + "-" + two(tm.tm_mday) + " " + two(tm.tm_hour) + ":"
                        + two(tm.tm_min) + ":" + two(tm.tm_sec));
        }
    }
    return false;
}

// ================================================================ les vues ===
bool Runtime::viewMemberValue(const View& v, std::string_view member, sim::Value& out) {
    const pub::InfoVar* info = pub::viewInfo(member);
    if (!info) return false;
    const std::string_view n = info->name;
    const auto slot = [&]() -> const PopupSlot* {
        for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) if (it->view == v.id) return &*it;
        return nullptr;
    };
    if (n == "Name") out = sim::Value::text(v.name);
    else if (n == "Description") out = sim::Value::text(v.description);
    else if (n == "Role") out = sim::Value::text(std::string(viewRoleLabel(v.role)));
    else if (n == "Width") out = sim::Value::integer(sim::Type::Int, v.width);
    else if (n == "Height") out = sim::Value::integer(sim::Type::Int, v.height);
    else if (n == "Background") {
        const auto it = backgrounds_.find(v.id);
        out = sim::Value::text(it != backgrounds_.end() ? it->second : v.background);
    } else if (n == "Open") out = sim::Value::boolean(viewOpen(v.id));
    else if (n == "IsCurrent") out = sim::Value::boolean(v.id == current_ && running_);
    else if (n == "IsPopup") out = sim::Value::boolean(slot() != nullptr);
    else if (n == "X") out = sim::Value::real(slot() ? slot()->x : 0.0);
    else if (n == "Y") out = sim::Value::real(slot() ? slot()->y : 0.0);
    else if (n == "Title") out = sim::Value::text(popupSettingsOf(v).title);
    else if (n == "ObjectCount") out = sim::Value::integer(sim::Type::Int, static_cast<std::int64_t>(v.objects.size()));
    else if (n == "OpenCount") {
        const auto it = openCounts_.find(v.id);
        out = sim::Value::integer(sim::Type::DInt, it == openCounts_.end() ? 0 : it->second);
    } else return false;
    return true;
}

// ============================================================== les objets ===
bool Runtime::objectMemberValue(const View& v, const Object& o, std::string_view key, std::string_view info, sim::Value& out) {
    if (!info.empty()) {
        const std::string_view n = info;
        if (n == "Name") out = sim::Value::text(o.name);
        else if (n == "Type") out = sim::Value::text(std::string(kindLabel(o.kind)));
        else if (n == "Id") out = sim::Value::integer(sim::Type::DInt, static_cast<std::int64_t>(o.id));
        else if (n == "Layer") {
            const Layer* l = v.layer(o.layer);
            out = sim::Value::text(l ? l->name : std::string{});
        } else if (n == "Shown") {
            sim::Value visible = sim::Value::boolean(true);
            if (o.find("visible")) (void)objectMemberValue(v, o, "visible", {}, visible);
            out = sim::Value::boolean(running_ && viewOpen(v.id) && visible.isTruthy());
        } else if (gifMemberValue(o, n, out)) {
            // lot 16 : Playing, Frame, FrameCount, Loops d'un GIF anime
        } else if (n.rfind("Alarm", 0) == 0) {
            // 1.9 : le groupe d'alarmes interne d'un objet du synoptique ou d'une instance.
            if (!kindIsSynoptic(o.kind) && o.kind != Kind::SymbolInstance) return false;
            const std::string group = objectGroupOf(v, o);
            if (n == "AlarmGroup") {
                out = sim::Value::text(group);
            } else if (n == "AlarmLinkedGroup") {
                // 1.11.1 (decision 108) : le groupe de l'IHM auquel ce groupe interne est lie.
                std::string symbols;
                if (o.kind == Kind::SymbolInstance)
                    if (const Prop* sp = o.find("symbol")) symbols = sp->value;
                const AlarmGroupLink* link = project_ ? alarmGroupLinkOf(*project_, group, symbols) : nullptr;
                out = sim::Value::text(link ? link->group : std::string{});
            } else {
                // Decision 11 h 55 : AlarmActive / AlarmUnacked en BOOL, les nombres a part
                // (calque sur SYS.AlarmActiveCount, SYS.AlarmUnackCount, SYS.AlarmHighestPriority).
                const GroupFigures f = objectGroupFigures(group);
                if (n == "AlarmActive") out = sim::Value::boolean(f.active > 0);
                else if (n == "AlarmUnacked") out = sim::Value::boolean(f.unacked > 0);
                else {
                    const std::size_t x = n == "AlarmCount"       ? f.count
                                        : n == "AlarmActiveCount" ? f.active
                                        : n == "AlarmUnackCount"  ? f.unacked
                                        : n == "AlarmHighest"     ? static_cast<std::size_t>(f.highest)
                                                                  : static_cast<std::size_t>(-1);
                    if (x == static_cast<std::size_t>(-1)) return false;
                    out = sim::Value::integer(sim::Type::Int, static_cast<std::int64_t>(x));
                }
            }
        } else if (n == "Pressed") out = sim::Value::boolean(running_ && pressed_ == o.id && viewOpen(v.id));
        else if (n == "Focused") out = sim::Value::boolean(running_ && focused_ == o.id && viewOpen(v.id));
        else if (n == "Enabled") {
            bool ok = objectAllowed(o);
            if (ok && kindWritesVariable(o.kind)) ok = permitted("Piloter");
            out = sim::Value::boolean(ok);
        } else return false;
        return true;
    }
    // Ecrite en marche : figee.
    if (const std::string* w = overrideOf(v.id, o.id, key)) {
        out = typedValue(key, *w);
        return true;
    }
    const Prop* p = o.find(key);
    // La valeur d'une commande ou d'un afficheur (sans propriete "value") : sa variable.
    if (!p && !(key == "value" && pub::linkedValue(o.kind))) return false;
    const std::string stat = p ? p->value : std::string{};
    if (publicDepth_ >= kMaxPublicDepth) {
        out = typedValue(key, stat);
        return true;
    }
    ++publicDepth_;
    const auto aliases = viewAliases(v.id);
    bool done = false;
    // Pilotee : son expression, ou la variable que montre l'objet.
    std::string source = p ? p->expr : std::string{};
    if (source.empty() && key == "value") source = autoValueSource(o);
    if (!source.empty()) {
        auto it = expressions_.find(source);
        if (it == expressions_.end()) it = expressions_.emplace(source, Expression::compile(source)).first;
        if (auto val = it->second.evaluate(environment())) {
            out = *val;
            done = true;
        }
    } else if (key == "text" && stat.find('{') != std::string::npos) {
        // Un texte a trous : tel qu'on le lit a l'ecran.
        out = sim::Value::text(TextTemplate::compile(stat).render(environment()));
        done = true;
    }
    --publicDepth_;
    if (!done) out = typedValue(key, stat);
    return true;
}

// ===================================================== 1.11.1 : une alarme d'objet ===
//  Vue.Objet.Alarmes.<alarme>.<membre> (decision 108) : `alarm`, son nom complet dans
//  la liste ("Vue.Objet.Defaut"). Une alarme absente de la liste n'est ni active ni a
//  acquitter ; decochee, elle ne vit pas (Enabled faux).
bool Runtime::alarmMemberValue(std::string_view alarm, std::string_view member, sim::Value& out) const {
    const LiveAlarm* live = nullptr;
    for (const auto& a : alarms_) if (pub::same(a.name, alarm)) { live = &a; break; }
    bool shelved = false;
    for (const auto& s : shelved_) shelved = shelved || pub::same(s.name, alarm);
    const AlarmDef* def = alarmDefByName(alarm);
    const std::string_view n = member;
    if (n == "Active") out = sim::Value::boolean(live && live->active);
    else if (n == "Unacked") out = sim::Value::boolean(live && !live->acked);
    else if (n == "Acked") out = sim::Value::boolean(!live || live->acked);
    else if (n == "Shelved") out = sim::Value::boolean(shelved);
    else if (n == "Enabled") out = sim::Value::boolean(def != nullptr);
    else if (n == "State") out = sim::Value::text(live ? live->state() : std::string{});
    else if (n == "Priority") out = sim::Value::integer(sim::Type::Int, live ? live->priority : def ? def->priority : 0);
    else if (n == "Message") out = sim::Value::text(live ? live->message : def ? def->message : std::string{});
    else if (n == "Group") out = sim::Value::text(live ? live->group : def ? def->group : std::string{});
    else if (n == "Name") out = sim::Value::text(std::string(alarm));
    else return false;
    return true;
}

int Runtime::alarmMemberWrite(std::string_view path, std::string_view alarm, std::string_view member, const sim::Value& value,
                              std::string* why) {
    const auto refuse = [&](std::string reason) {
        if (why) *why = std::move(reason);
        return 0;
    };
    const std::string name(alarm);
    std::string why2;
    if (member == "Acked") {
        if (!value.isTruthy()) return refuse(std::string(path) + " : une alarme ne se d\xC3\xA9sacquitte pas (\xC3\xA9" "cris TRUE pour l'acquitter)");
        bool waiting = false;
        for (const auto& a : alarms_) waiting = waiting || (pub::same(a.name, alarm) && !a.acked);
        if (!waiting) return 1;     // rien a acquitter : deja fait
        if (acknowledge(name, now_, &why2) > 0) return 1;
        return refuse(std::string(path) + " : " + why2);
    }
    if (member == "Shelved") {
        bool shelved = false;
        for (const auto& s : shelved_) shelved = shelved || pub::same(s.name, alarm);
        if (value.isTruthy() == shelved) return 1;      // deja dans cet etat
        const std::string reason = source_.empty() ? std::string(path) : source_;
        const std::size_t done = value.isTruthy() ? shelve(name, 0.0, reason, now_, &why2) : unshelve(name, now_, &why2);
        if (done > 0) return 1;
        return refuse(std::string(path) + " : " + why2);
    }
    return refuse(std::string(path) + " : l'\xC3\xA9tat de l'alarme, en lecture seule (Acked et Shelved s'\xC3\xA9" "crivent)");
}

// ================================================================ lire ======
bool Runtime::publicRead(std::string_view path, sim::Value& out) {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos || !project_) return false;
    const std::string_view root = path.substr(0, dot);
    if (pub::isSysRoot(root)) return sysValue(path.substr(dot + 1), out);
    const View* pv = pub::viewNamed(*project_, root);
    if (!pv) return false;
    const View* v = viewOf(pv->id);
    if (!v) return false;
    const auto r = pub::resolve(*project_, path, v);
    if (r.what == pub::Resolved::What::ViewMember) return viewMemberValue(*v, r.info->name, out);
    if (r.what != pub::Resolved::What::ObjectMember || !r.object) return false;
    // 1.11.1 (decision 108) : Vue.Objet.Alarmes.<alarme>.<membre>.
    if (!r.alarm.empty() && r.alarmMember) return alarmMemberValue(r.alarm, r.alarmMember->name, out);
    // 1.10.2 (chantier A) : un parametre d'instance de symbole vaut ce qu'il relie.
    if (!r.param.empty()) {
        if (r.argument.empty() || publicDepth_ >= kMaxPublicDepth) return false;
        ++publicDepth_;
        auto it = expressions_.find(r.argument);
        if (it == expressions_.end()) it = expressions_.emplace(r.argument, Expression::compile(r.argument)).first;
        const auto val = it->second.evaluate(environment());
        --publicDepth_;
        if (!val) return false;
        out = *val;
        return true;
    }
    return objectMemberValue(*v, *r.object, r.key, r.info ? r.info->name : std::string_view{}, out);
}

bool Runtime::publicExists(std::string_view path) const {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos || !project_) return false;
    const View* pv = pub::isSysRoot(path.substr(0, dot)) ? nullptr : pub::viewNamed(*project_, path.substr(0, dot));
    const auto r = pub::resolve(*project_, path, pv ? viewOf(pv->id) : nullptr);
    return r.what == pub::Resolved::What::Sys || r.what == pub::Resolved::What::ViewMember
        || r.what == pub::Resolved::What::ObjectMember;
}

// ================================================================ ecrire ====
int Runtime::publicWrite(std::string_view path, const sim::Value& value, std::string* why) {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos || !project_) return -1;
    const std::string_view root = path.substr(0, dot);
    const auto refuse = [&](std::string reason) {
        if (why) *why = std::move(reason);
        return 0;
    };
    const View* pv = pub::isSysRoot(root) ? nullptr : pub::viewNamed(*project_, root);
    if (!pub::isSysRoot(root) && !pv) return -1;
    const View* v = pv ? viewOf(pv->id) : nullptr;
    const auto r = pub::resolve(*project_, path, v);
    using W = pub::Resolved::What;
    switch (r.what) {
        case W::None: return -1;
        case W::Sys:
            // Lot 13 : SYS.Language s'ecrit (la langue change) ; les autres, en lecture seule.
            if (r.access == pub::Access::ReadWrite && r.sys && pub::same(r.sys->name, "Language")) {
                std::string why2;
                if (setLanguage(storedText(value), now_, source_.empty() ? std::string("SYS.Language") : source_, &why2)) return 1;
                return refuse(std::string(path) + " : " + why2);
            }
            if (r.access == pub::Access::ReadWrite && r.sys) {
                const std::string_view n = r.sys->name;
                const char* key = pub::same(n, "TextScale") ? "texte" : pub::same(n, "ColorMode") ? "couleurs"
                                : pub::same(n, "StatusSymbols") ? "symboles" : "theme";
                std::string why2;
                const std::string asked = r.type == "BOOL" ? std::string(value.isTruthy() ? "TRUE" : "FALSE") : storedText(value);
                if (setDisplay(key, asked, now_, source_.empty() ? "SYS." + std::string(n) : source_, &why2)) return 1;
                return refuse(std::string(path) + " : " + why2);
            }
            return refuse(std::string(path) + " : variable syst\xC3\xA8me, en lecture seule");
        case W::Unknown:
            return refuse(r.error);
        case W::Incomplete:
            return refuse(std::string(path) + " : chemin incomplet (Vue.Objet.Propri\xC3\xA9t\xC3\xA9)");
        case W::ViewMember: {
            const std::string_view n = r.info->name;
            if (r.access != pub::Access::ReadWrite) return refuse(std::string(path) + " : en lecture seule");
            if (n == "Background") {
                backgrounds_[pv->id] = storedText(value);
                log("Action", source_, std::string(path) + " = " + backgrounds_[pv->id]);
                return 1;
            }
            // X, Y : la popup ouverte se deplace.
            for (std::size_t k = slots_.size(); k-- > 0;)
                if (slots_[k].view == pv->id) {
                    double x = slots_[k].x, y = slots_[k].y;
                    const double to = value.type() == sim::Type::Real ? value.asReal() : static_cast<double>(value.asInteger());
                    (n == "X" ? x : y) = to;
                    movePopup(k, x, y);
                    return 1;
                }
            return refuse(std::string(path) + " : la vue n'est pas ouverte en popup");
        }
        case W::ObjectMember: {
            if (!r.alarm.empty() && r.alarmMember) return alarmMemberWrite(path, r.alarm, r.alarmMember->name, value, why);   // 1.11.1
            if (r.info) return refuse(std::string(path) + " : ce que le moteur sait de l'objet, en lecture seule");
            if (!r.param.empty())   // 1.10.2 : un parametre d'instance se lit ; on ecrit ce qu'il relie
                return refuse(std::string(path) + " : param\xC3\xA8tre de l'instance, en lecture seule (\xC3\xA9" "cris " + r.argument + ")");
            if (r.access != pub::Access::ReadWrite)
                return refuse(std::string(path) + " : la propri\xC3\xA9t\xC3\xA9 " + pub::publicName(r.key) + " ne s'\xC3\xA9" "crit pas en marche");
            std::string text = storedText(value);
            if (r.type == "BOOL") text = value.type() == sim::Type::String ? (parseBool(text, false) ? "TRUE" : "FALSE")
                                                                           : (value.isTruthy() ? "TRUE" : "FALSE");
            if (r.type == "REAL" || r.type == "INT") {
                double n = 0;
                if (!parseNumber(text, n)) return refuse(std::string(path) + " : un nombre est attendu (" + text + ")");
                text = formatNumber(n);
            }
            overrides_[{pv->id, r.object->id}][r.key] = text;
            log("Action", source_, std::string(path) + " = " + text);
            return 1;
        }
    }
    return -1;
}

} // namespace hmi
