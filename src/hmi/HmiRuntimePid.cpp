// =============================================================================
//  hmi/HmiRuntimePid.cpp - 1.12.24 : le Reglage PID en marche
// -----------------------------------------------------------------------------
//  CHAQUE OBJET Reglage PID DES VUES lit sa boucle a sa periode, que sa vue soit
//  affichee ou non (comme les courbes et les compteurs de production) : la
//  consigne, la mesure, la sortie, le mode, et le reglage du bloc. Il le donne
//  a son pid::Learner, qui apprend le procede et en tire ses statistiques ; sa
//  memoire se copie dans l'historique (History::pid, une fois par minute et a
//  l'arret) et se relit quand il repart.
//
//  SA FENETRE (une a la fois) : un clic sur la carte l'ouvre. Appliquer ecrit
//  Kp, Ti et Td dans leurs variables - apres confirmation, au niveau d'acces de
//  l'objet, avec sa signature si elle est demandee - et le Journal garde
//  l'ancien reglage (Revenir). Copier les valeurs, Oublier et reapprendre,
//  Exporter (CSV) dans exports/.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiDisplay.hpp"
#include "HmiExpr.hpp"
#include "HmiHistory.hpp"
#include "HmiPidLoop.hpp"
#include "HmiRanges.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>

namespace hmi {

namespace {

std::string pidKeyOf(Id view, Id object) { return std::to_string(view) + ":" + std::to_string(object); }

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Ce que l'objet lit par une propriete : son expression (=...), sinon son texte, qui en est une.
std::string sourceText(const Object& o, std::string_view key) {
    const auto* p = o.find(key);
    if (!p) return {};
    return trimmed(p->expr.empty() ? p->value : p->expr);
}

// Le debut d'un mot, sans casse ni accents ("apres" pour "après confirmation").
bool startsWith(std::string_view text, std::string_view prefix) {
    std::string p;
    for (std::size_t i = 0; i < text.size() && p.size() < prefix.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size()) {
            const auto d = static_cast<unsigned char>(text[++i]);
            p += (d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB) ? 'e' : (d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5) ? 'a' : '?';
            continue;
        }
        p += static_cast<char>(std::tolower(c));
    }
    return p == prefix;
}

double timeUnitOf(std::string_view t) {
    const std::string s = trimmed(t);
    if (s == "min" || startsWith(s, "minute")) return 60.0;
    if (s == "ms" || startsWith(s, "milli")) return 0.001;
    if (startsWith(s, "0,1") || startsWith(s, "0.1") || startsWith(s, "dix")) return 0.1;
    return 1.0;
}

pid::Form formOf(std::string_view t) {
    if (const auto f = pid::parseForm(trimmed(t))) return *f;
    if (startsWith(t, "par")) return pid::Form::Parallel;
    if (startsWith(t, "ser")) return pid::Form::Series;
    return pid::Form::Standard;
}

// Le nombre d'une valeur lue ; une duree (TIME) en secondes.
bool numberOfValue(const sim::Value& v, double& x, bool* time = nullptr) {
    if (time) *time = false;
    switch (v.type()) {
        case sim::Type::Unknown: return false;
        case sim::Type::String: return parseNumber(trimmed(v.asString()), x);
        case sim::Type::Time:
            if (time) *time = true;
            x = static_cast<double>(v.asInteger()) / 1000.0;
            return true;
        default:
            x = v.asReal();
            return std::isfinite(x);
    }
}

// Les noms des membres d'une structure de boucle, dans l'ordre ou on les essaie.
constexpr std::string_view kSpNames[] = {"SP", "Consigne", "Setpoint", "Csg", "W"};
constexpr std::string_view kPvNames[] = {"PV", "Mesure", "Measure", "Mes", "X"};
constexpr std::string_view kOutNames[] = {"OUT", "Sortie", "Output", "OP", "MV", "Y"};
constexpr std::string_view kAutoNames[] = {"AUTO", "EnAuto", "Auto_Mode", "Mode_Auto"};
constexpr std::string_view kKpNames[] = {"KP", "Gain", "Kc"};
constexpr std::string_view kTiNames[] = {"TI", "Ki"};
constexpr std::string_view kTdNames[] = {"TD", "Kd"};

} // namespace

// ====================================================================== lecture ===
const PidLoop* Runtime::pidLoop(Id view, Id object) const {
    const auto it = pid_.find(pidKeyOf(view, object));
    return it == pid_.end() ? nullptr : &it->second;
}

const PidLoop* Runtime::pidLoop(Id object) const {
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
        if (const auto* L = pidLoop(*it, object)) return L;
    if (const auto* L = pidLoop(current_, object)) return L;
    for (const auto& [key, L] : pid_)
        if (L.object == object) return &L;
    return nullptr;
}

const PidLoop* Runtime::pidShownLoop() const {
    return pidWindow_.shown ? pidLoop(pidWindow_.view, pidWindow_.object) : nullptr;
}

// Une formule lue dans l'environnement du moteur (la vue a deja ses parametres en place).
bool Runtime::pidRead(const std::string& expr, double& out, bool* time) {
    if (expr.empty()) return false;
    auto it = expressions_.find(expr);
    if (it == expressions_.end()) it = expressions_.emplace(expr, Expression::compile(expr)).first;
    if (!it->second.valid()) return false;
    const auto value = it->second.evaluate(environment());
    return value && numberOfValue(*value, out, time);
}

// La boucle de l'objet : ses formules (celles de la structure pour les cases vides), son titre,
// son unite, ses echelles, la forme et les unites du bloc. Refaite quand une case change.
void Runtime::pidResolve(PidLoop& L, const View& v, const Object& o, const Object& shown) {
    std::string sig;
    for (const char* key : {"pidLoop", "pidSp", "pidPv", "pidOut", "pidAuto", "pidKp", "pidTi", "pidTd", "pidPvMin", "pidPvMax", "unit",
                            "title", "pidForm", "pidTimeUnit", "pidKpMode", "pidKpScale", "pidWrite"})
        sig += sourceText(o, key) + "\x1F" + shown.text(key) + "\x1E";
    if (sig == L.bind.signature && !L.bind.pv.empty()) return;
    PidBinding b;
    b.signature = sig;
    const std::string loop = sourceText(o, "pidLoop");
    double probe = 0;
    const auto pick = [&](const char* key, std::initializer_list<std::string_view> names) -> std::string {
        if (std::string own = sourceText(o, key); !own.empty()) return own;
        if (loop.empty()) return {};
        for (const auto n : names) {
            const std::string path = loop + "." + std::string(n);
            if (pidRead(path, probe)) return path;
        }
        return {};
    };
    b.sp = pick("pidSp", {kSpNames[0], kSpNames[1], kSpNames[2], kSpNames[3], kSpNames[4]});
    b.pv = pick("pidPv", {kPvNames[0], kPvNames[1], kPvNames[2], kPvNames[3], kPvNames[4]});
    b.out = pick("pidOut", {kOutNames[0], kOutNames[1], kOutNames[2], kOutNames[3], kOutNames[4], kOutNames[5]});
    b.autoMode = pick("pidAuto", {kAutoNames[0], kAutoNames[1], kAutoNames[2], kAutoNames[3]});
    b.kp = pick("pidKp", {kKpNames[0], kKpNames[1], kKpNames[2]});
    b.ti = pick("pidTi", {kTiNames[0], kTiNames[1]});
    b.td = pick("pidTd", {kTdNames[0], kTdNames[1]});
    // Le titre : le Titre de l'objet, sinon la boucle, sinon la mesure, sinon son nom.
    b.title = trimmed(shown.text("title"));
    if (b.title.empty()) b.title = !loop.empty() ? loop : ranges::isName(b.pv) ? b.pv : o.name;
    // L'unite et l'etendue de la mesure : les siennes, sinon celles de sa variable (Unites et formats).
    const std::string pvVar = ranges::isName(b.pv) ? b.pv : std::string{};
    b.unit = trimmed(shown.text("unit"));
    if (b.unit.empty() && !pvVar.empty() && project_)
        if (const auto* d = displayOf(*project_, pvVar, &v)) b.unit = d->unit;
    const auto bound = [&](const char* key, const char* inherited, double& out) {
        std::string t = trimmed(shown.text(key));
        if (t.empty() && !pvVar.empty() && project_) t = ranges::inheritedOf(*project_, pvVar, inherited, &v).value;
        if (t.empty()) return false;
        return parseNumber(t, out) || pidRead(t, out);
    };
    double lo = 0, hi = 0;
    if (bound("pidPvMin", "min", lo) && bound("pidPvMax", "max", hi) && hi > lo) {
        b.pvMin = lo;
        b.pvMax = hi;
    }
    // Le bloc : sa forme (un choix, ou la variable qui la dit), ses unites, son Kp.
    std::string form = shown.text("pidForm", "standard");
    if (const auto* fp = o.find("pidForm"); fp && !fp->expr.empty()) {
        double n = 0;
        if (pidRead(trimmed(fp->expr), n)) form = std::to_string(std::lround(n));
    }
    b.form = formOf(form);
    b.timeSeconds = timeUnitOf(shown.text("pidTimeUnit", "s"));
    b.normalized = startsWith(shown.text("pidKpMode", "physique"), "norm");
    double scale = 1;
    b.kpScale = parseNumber(trimmed(shown.text("pidKpScale", "1")), scale) && scale > 0 ? scale : 1.0;
    b.writes = startsWith(shown.text("pidWrite", "apr\xC3\xA8s confirmation"), "apr");
    L.bind = std::move(b);
}

// La configuration du Learner, telle que l'objet la dit.
static pid::Config pidConfigOf(const Object& shown, const PidBinding& b) {
    pid::Config c;
    double ms = 1000;
    (void)parseNumber(trimmed(shown.text("pidPeriod", "1000")), ms);
    c.period = std::clamp(ms / 1000.0, 0.1, 10.0);
    double lo = 0, hi = 100;
    (void)parseNumber(trimmed(shown.text("pidOutMin", "0")), lo);
    (void)parseNumber(trimmed(shown.text("pidOutMax", "100")), hi);
    if (hi <= lo) hi = lo + 100;
    c.outMin = lo;
    c.outMax = hi;
    c.pvSpan = b.pvMax > b.pvMin ? b.pvMax - b.pvMin : 0.0;
    double tol = 0;
    if (parseNumber(trimmed(shown.text("pidTolerance")), tol) && tol > 0) c.tolerance = tol;
    else c.tolerance = c.pvSpan > 0 ? c.pvSpan * 0.01 : 1.0;
    double days = 7, forget = 24;
    (void)parseNumber(trimmed(shown.text("pidMemory", "7")), days);
    (void)parseNumber(trimmed(shown.text("pidForget", "24")), forget);
    c.memoryDays = std::clamp(days, 1.0, 30.0);
    c.forgetHours = std::max(0.0, forget);
    const std::string learn = shown.text("pidLearn", "en AUTO et en MANU");
    c.learnAuto = startsWith(learn, "en auto");
    c.learnManual = startsWith(learn, "en manu") || learn.find("MANU") != std::string::npos;
    if (startsWith(learn, "jam")) c.learnAuto = c.learnManual = false;
    const std::string action = shown.text("pidAction", "d\xC3\xA9tect\xC3\xA9");
    c.action = startsWith(action, "inv") ? 1 : startsWith(action, "dir") ? -1 : 0;
    c.goal = pidGoalOf(shown.text("pidGoal", "\xC3\x89quilibr\xC3\xA9"));
    c.derivative = pidDerivativeOf(shown.text("pidDerivative", "si elle aide"));
    c.form = b.form;
    return c;
}

// ======================================================================= le cycle ===
void Runtime::pidCycle(double now) {
    if (!project_) return;
    // Les objets a lire : ceux des vues ou l'on navigue (leur ecran modele compris), affichees ou non.
    if (now - pidPlanAt_ >= 2.0 || now < pidPlanAt_) {
        pidPlanAt_ = now;
        pidPlan_.clear();
        for (const auto& pv : project_->views) {
            const bool navigable = pv.role == "vue" || (pv.role == "popup" && pv.params.empty());
            if (!navigable) continue;
            const View* c = viewOf(pv.id);
            if (!c) continue;
            for (const auto& o : c->objects)
                if (o.kind == Kind::PidTuner) pidPlan_.emplace_back(pv.id, o.id);
        }
    }
    for (const auto& [vid, oid] : pidPlan_) {
        const View* v = viewOf(vid);
        const Object* o = v ? v->object(oid) : nullptr;
        if (!o) continue;
        PidLoop& L = pid_[pidKeyOf(vid, oid)];
        if (L.object == kNoId) {
            L.view = vid;
            L.object = oid;
            L.key = pidMemoryName(v->name + "." + o->name);
            // Ce qu'il savait : sa memoire dans l'historique.
            if (history_)
                if (const auto it = history_->pid.find(L.key); it != history_->pid.end()) L.loaded = L.learner.load(it->second);
        }
        if (L.nextAt >= 0 && now + 1e-9 < L.nextAt) continue;
        const auto aliases = viewAliases(vid);
        Object shown = *o;
        evaluateProps(vid, shown);
        pidResolve(L, *v, *o, shown);
        const pid::Config cfg = pidConfigOf(shown, L.bind);
        if (!(cfg == L.learner.config())) L.learner.configure(cfg);
        // La prochaine lecture : a la periode (un retard ne s'accumule pas).
        L.nextAt = L.nextAt < 0 || now - L.nextAt > cfg.period ? now + cfg.period : L.nextAt + cfg.period;
        double sp = 0, pvv = 0, out = 0, am = 1;
        std::string missing;
        if (L.bind.pv.empty()) missing = "la mesure (PV) n'est pas li\xC3\xA9" "e";
        else if (!pidRead(L.bind.pv, pvv)) missing = "mesure illisible : " + L.bind.pv;
        else if (L.bind.sp.empty()) missing = "la consigne (SP) n'est pas li\xC3\xA9" "e";
        else if (!pidRead(L.bind.sp, sp)) missing = "consigne illisible : " + L.bind.sp;
        else if (!L.bind.out.empty() && !pidRead(L.bind.out, out)) missing = "sortie illisible : " + L.bind.out;
        if (!missing.empty()) {
            L.read = false;
            L.error = std::move(missing);
            continue;
        }
        if (!L.bind.autoMode.empty() && !pidRead(L.bind.autoMode, am)) am = 1;
        L.read = true;
        L.error.clear();
        L.sp = sp;
        L.pv = pvv;
        L.out = out;
        L.autoMode = am != 0;
        // Le reglage du bloc, lu dans ses unites.
        L.gains.reset();
        L.block.reset();
        double kp = 0, ti = 0, td = 0;
        bool tiTime = false, tdTime = false;
        if (!L.bind.kp.empty() && pidRead(L.bind.kp, kp) && (L.bind.ti.empty() || pidRead(L.bind.ti, ti, &tiTime))
            && (L.bind.td.empty() || pidRead(L.bind.td, td, &tdTime))) {
            const double unit = L.bind.timeSeconds;
            if (tiTime) ti /= unit;   // une duree (TIME) : en secondes, ramenee a l'unite du bloc
            if (tdTime) td /= unit;
            L.block = pid::BlockValues{kp, ti, td};
            L.gains = pid::fromBlock(*L.block, L.bind.form, L.bind.units(cfg.outSpan()));
        }
        L.wall = epochOf(now);
        pid::Sample s;
        s.t = L.wall;
        s.sp = sp;
        s.pv = pvv;
        s.out = L.bind.out.empty() ? L.learner.outRest() : out;
        s.autoMode = L.autoMode;
        L.learner.push(s, L.gains);
        // Sa memoire dans l'historique, une fois par minute.
        if (history_ && (L.keptAt < 0 || now - L.keptAt >= 60.0 || now < L.keptAt)) {
            L.keptAt = now;
            history_->pid[L.key] = L.learner.save();
        }
    }
}

void Runtime::pidKeepAll() {
    if (!history_) return;
    for (auto& [key, L] : pid_)
        if (L.object != kNoId && L.learner.sampleCount() > 0) history_->pid[L.key] = L.learner.save();
}

// ======================================================================= la fenetre ===
void Runtime::pidOpen(Id view, Id object, int tab, double now) {
    const PidWindow keep = pidWindow_;
    pidWindow_ = PidWindow{};
    pidWindow_.shown = true;
    pidWindow_.view = view;
    pidWindow_.object = object;
    pidWindow_.tab = std::clamp(tab, 0, 3);
    // La meme boucle rouverte : sa fenetre de temps et son objectif restent.
    if (keep.view == view && keep.object == object) {
        pidWindow_.range = keep.range;
        pidWindow_.goal = keep.goal;
    }
    lastActivity_ = now;
}

void Runtime::pidSay(std::string message, bool bad, double now) {
    pidWindow_.message = std::move(message);
    pidWindow_.messageBad = bad;
    pidWindow_.messageUntil = now + 6.0;
}

// Ecrire un reglage dans le bloc (ses trois variables, ou ce qui en est ecrivable).
bool Runtime::pidWrite(PidLoop& L, const View& v, const Object& o, const pid::Gains& g, const std::string& what, double now) {
    const pid::BlockValues blk = pidBlockOf(L, g);
    const std::string source = where(v, &o);
    GestureGuard gesture(*this, source, "R\xC3\xA9glage PID");
    int written = 0, skipped = 0;
    const auto put = [&](const std::string& path, double x) {
        if (path.empty()) return;
        if (!ranges::isName(path)) { ++skipped; return; }
        if (writeConverted(path, sim::Value::real(x), source, &o)) ++written;
        else ++skipped;
    };
    put(L.bind.kp, blk.p);
    put(L.bind.ti, blk.i);
    put(L.bind.td, blk.d);
    if (written == 0) {
        pidSay("Rien n'est \xC3\xA9" "crit : Kp, Ti et Td ne sont pas des variables \xC3\xA9" "crivables.", true, now);
        return false;
    }
    const pid::Gains before = L.gains.value_or(pid::Gains{});
    const auto& pr = L.learner.proposal(L.learner.config().goal, L.learner.config().derivative, L.bind.form);
    L.learner.noteApplied(epochOf(now), before, g, pr.ok ? pr.overshoot : -1);
    L.gains = g;
    L.block = blk;
    PidApplied a;
    a.t = epochOf(now);
    a.before = before;
    a.after = g;
    a.goal = what;
    a.user = userLogin();
    L.applied.push_back(a);
    const std::string unit = pidTimeUnitName(L.bind.timeSeconds);
    const std::string text = what + " : Kp " + pidNumber(blk.p, 3) + ", Ti " + pidNumber(blk.i, 2) + " " + unit + ", Td " + pidNumber(blk.d, 2) + " "
                           + unit + (skipped ? " (" + std::to_string(skipped) + " valeur(s) non \xC3\xA9" "crite(s))" : std::string{});
    event("R\xC3\xA9glage PID", source, text);
    pidSay("\xC3\x89" "crit \xC2\xB7 " + text, skipped > 0, now);
    if (history_) history_->pid[L.key] = L.learner.save();
    return true;
}

// Ce que le geste demande, une fois permis (et signe s'il le faut).
void Runtime::pidDo(std::string_view part, double now, bool signedAlready) {
    PidLoop* L = nullptr;
    if (const auto it = pid_.find(pidKeyOf(pidWindow_.view, pidWindow_.object)); it != pid_.end()) L = &it->second;
    const View* v = viewOf(pidWindow_.view);
    const Object* src = v ? v->object(pidWindow_.object) : nullptr;
    if (!L || !src) return;
    View snapshot = *v;
    Object& o = *snapshot.object(pidWindow_.object);
    evaluateProps(snapshot.id, o);
    const bool changes = part == "apply:oui" || part == "forget:oui" || part.rfind("revert:oui", 0) == 0;
    if (changes) {
        std::string why;
        if (!objectAllowed(o, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(snapshot, &o), why);
            pidSay("Refus\xC3\xA9 : " + why, true, now);
            pidWindow_.confirm = pidWindow_.forgetAsk = false;
            pidWindow_.revertAsk = -1;
            return;
        }
        if (!signedAlready && signatureNeeded(snapshot, o)) {
            requestSignature(snapshot, o, SignatureRequest::Gesture::Part, now, "pid:" + std::string(part));
            return;
        }
    }
    const auto goalNow = [&] {
        return pidWindow_.goal >= 0 ? static_cast<pid::Goal>(std::clamp(pidWindow_.goal, 0, 2)) : L->learner.config().goal;
    };
    if (part == "apply:oui") {
        pidWindow_.confirm = false;
        const auto& pr = L->learner.proposal(goalNow(), L->learner.config().derivative, L->bind.form);
        if (!pr.ok) {
            pidSay("Pas encore de r\xC3\xA9glage \xC3\xA0 proposer : il apprend le proc\xC3\xA9" "d\xC3\xA9.", true, now);
            return;
        }
        (void)pidWrite(*L, snapshot, o, pr.gains, std::string(pid::goalName(goalNow())), now);
    } else if (part == "forget:oui") {
        pidWindow_.forgetAsk = false;
        L->learner.forget();
        event("R\xC3\xA9glage PID", where(snapshot, &o), "oubli\xC3\xA9 : il r\xC3\xA9" "apprend le proc\xC3\xA9" "d\xC3\xA9");
        pidSay("Oubli\xC3\xA9 : il r\xC3\xA9" "apprend le proc\xC3\xA9" "d\xC3\xA9 (les statistiques restent).", false, now);
        if (history_) history_->pid[L->key] = L->learner.save();
    } else if (part.rfind("revert:oui", 0) == 0) {
        const int at = pidWindow_.revertAsk;
        pidWindow_.revertAsk = -1;
        // L'ecriture du Journal d'avant (sa place parmi les reglages ecrits, le plus recent d'abord).
        std::vector<const pid::Event*> applied;
        for (auto it = L->learner.events().rbegin(); it != L->learner.events().rend(); ++it)
            if (it->kind == pid::EventKind::Applied) applied.push_back(&*it);
        if (at < 0 || static_cast<std::size_t>(at) >= applied.size()) return;
        const pid::Gains back = applied[static_cast<std::size_t>(at)]->before;
        if (back.kp == 0) {
            pidSay("L'ancien r\xC3\xA9glage n'est pas connu (le bloc n'\xC3\xA9tait pas lu).", true, now);
            return;
        }
        (void)pidWrite(*L, snapshot, o, back, "Revenir", now);
    }
}

void Runtime::pidPart(std::string_view part, double now) {
    lastActivity_ = now;
    if (!pidWindow_.shown) return;
    const auto number = [&](std::string_view prefix) {
        const std::string rest(part.substr(prefix.size()));
        return std::atoi(rest.c_str());
    };
    if (part == "fermer" || part == "dehors") {
        pidWindow_.shown = false;
        return;
    }
    if (part.rfind("onglet:", 0) == 0) {
        pidWindow_.tab = std::clamp(number("onglet:"), 0, 3);
        pidWindow_.confirm = pidWindow_.forgetAsk = false;
        pidWindow_.revertAsk = -1;
        pidWindow_.hoverT = -1;
        return;
    }
    if (part.rfind("duree:", 0) == 0) {
        pidWindow_.range = std::clamp(number("duree:"), 0, 3);
        return;
    }
    if (part.rfind("objectif:", 0) == 0) {
        pidWindow_.goal = std::clamp(number("objectif:"), 0, 2);
        pidWindow_.confirm = false;
        return;
    }
    if (part == "journal:haut") { pidWindow_.scroll = std::max(0, pidWindow_.scroll - 5); return; }
    if (part == "journal:bas") { pidWindow_.scroll += 5; return; }
    const PidLoop* L = pidShownLoop();
    if (!L) return;
    if (part == "appliquer") {
        if (!L->bind.writes) {
            pidSay("\xC3\x89" "crire le r\xC3\xA9glage : jamais (r\xC3\xA9glage de l'objet). Copie les valeurs.", true, now);
            return;
        }
        pidWindow_.confirm = true;
        return;
    }
    if (part == "appliquer:non") { pidWindow_.confirm = false; return; }
    if (part == "appliquer:oui") { pidDo("apply:oui", now, false); return; }
    if (part == "oublier") { pidWindow_.forgetAsk = true; return; }
    if (part == "oublier:non") { pidWindow_.forgetAsk = false; return; }
    if (part == "oublier:oui") { pidDo("forget:oui", now, false); return; }
    if (part.rfind("revenir:", 0) == 0) {
        const std::string rest(part.substr(8));
        if (rest == "non") pidWindow_.revertAsk = -1;
        else if (rest == "oui") pidDo("revert:oui", now, false);
        else pidWindow_.revertAsk = std::atoi(rest.c_str());
        return;
    }
    if (part == "copier") {
        const pid::Goal g = pidWindow_.goal >= 0 ? static_cast<pid::Goal>(pidWindow_.goal) : L->learner.config().goal;
        const auto& pr = L->learner.proposal(g, L->learner.config().derivative, L->bind.form);
        if (!pr.ok) {
            pidSay("Pas encore de r\xC3\xA9glage \xC3\xA0 copier.", true, now);
            return;
        }
        const auto blk = pidBlockOf(*L, pr.gains);
        const std::string unit = pidTimeUnitName(L->bind.timeSeconds);
        const bool parallel = L->bind.form == pid::Form::Parallel;
        pidWindow_.clipboard = std::string(parallel ? "Kp = " : "Kp = ") + pidNumber(blk.p, 3) + (parallel ? "\tKi = " : "\tTi = ") + pidNumber(blk.i, 3)
                             + (parallel ? "" : " " + unit) + (parallel ? "\tKd = " : "\tTd = ") + pidNumber(blk.d, 3) + (parallel ? "" : " " + unit);
        pidSay("Copi\xC3\xA9 : " + pidWindow_.clipboard, false, now);
        return;
    }
    if (part == "exporter") {
        const View* v = viewOf(L->view);
        const Object* o = v ? v->object(L->object) : nullptr;
        if (!o || !hooks_.exportFile) {
            pidSay("Exporter : rien pour \xC3\xA9" "crire le fichier ici.", true, now);
            return;
        }
        const std::string csv = pidCsv(*L, pidRangeSeconds(pidWindow_.range));
        ExportRequest rq;
        rq.fileName = "pid_" + L->key + "_" + dateStampOf(now).substr(0, 10) + ".csv";
        rq.format = "CSV";
        rq.source = "pid:" + L->key;
        rq.rows = static_cast<std::size_t>(std::count(csv.begin(), csv.end(), '\n'));
        rq.data = std::make_shared<const Bytes>(csv.begin(), csv.end());
        rq.origin = where(*v, o);
        std::string at;
        const bool ok = hooks_.exportFile(rq, &at);
        pidSay(ok ? "Export\xC3\xA9 : " + at : "Export impossible : " + at, !ok, now);
        return;
    }
}

// Une partie de la carte (le clic du moteur), ou le geste signe qui revient (pid:...).
void Runtime::pidObjectPart(const View& v, const Object& o, std::string_view part, double now) {
    if (part.rfind("pid:", 0) == 0) {
        // Le geste que la signature a valide : la fenetre est celle de cet objet.
        if (pidWindow_.view != v.id || pidWindow_.object != o.id) pidOpen(v.id, o.id, 2, now);
        pidDo(part.substr(4), now, true);
        return;
    }
    const int tab = part == "conseil" ? 2 : part == "appris" ? 1 : 0;
    pidOpen(v.id, o.id, tab, now);
}

} // namespace hmi
