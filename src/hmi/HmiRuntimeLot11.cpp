// =============================================================================
//  hmi/HmiRuntimeLot11.cpp - le moteur des objets du lot 11
// -----------------------------------------------------------------------------
//  GRAPHIQUES    le chronogramme retient les changements de chaque ligne, la
//                courbe XY ses N derniers points (une remise a zero sur front
//                montant de "reset"), l'histogramme ses mesures (une par
//                periode, sur une fenetre).
//  ALARMES       la mise de cote (qui, pourquoi, jusqu'a quand ; la fin d'elle-
//                meme), le son de la priorite a l'apparition et sa repetition
//                tant qu'elle attend son acquittement, "Faire taire", l'alarme
//                et la zone choisies (bandeau, liste, resume).
//  PRODUCTION    les compteurs du poste (bons, rebuts, cadence, TRS), remis a
//                zero a chaque debut de poste ou par RAZ ; le tableau de
//                variables (une valeur se modifie, permission Piloter) ;
//                l'editeur de recette (modifier, enregistrer, appliquer, lire).
//  EXPORT        le tableau d'une source, puis le fichier (l'ecran l'ecrit).
// =============================================================================
#include "HmiObjectAlarms.hpp"
#include "HmiAlarmGroups.hpp"   // 1.10.2 (AL) : le son du groupe
#include "HmiRuntime.hpp"

#include "HmiAlarmViews.hpp"
#include "HmiCharts.hpp"
#include "HmiExport.hpp"
#include "HmiProduction.hpp"
#include "HmiWidgets.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : les reperes $...$

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hmi {

namespace {

std::string keyOf(Id view, Id object) { return std::to_string(view) + ":" + std::to_string(object); }

std::string trimmedText(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

bool numberOf(const std::string& text, double& out) {
    if (text == "TRUE") { out = 1; return true; }
    if (text == "FALSE") { out = 0; return true; }
    return parseNumber(text, out);
}

bool truthy(const std::string& v) { return v == "TRUE" || (v != "FALSE" && v != "0" && !v.empty() && v != "0.0"); }

// Ce qu'un objet lit par une propriete : son expression ("=..."), sinon le texte
// de la propriete, qui en est une (Compteur_Bons, Pompes[0].Debit).
std::string sourceOf(const Object& o, std::string_view key) {
    const auto* p = o.find(key);
    if (!p) return {};
    return trimmedText(p->expr.empty() ? p->value : p->expr);
}

int indexAfter(std::string_view part, std::string_view prefix) {
    if (part.substr(0, prefix.size()) != prefix) return -1;
    const std::string rest(part.substr(prefix.size()));
    if (rest.empty() || !std::isdigit(static_cast<unsigned char>(rest[0]))) return -1;
    return std::atoi(rest.c_str());
}

// "evenements" pour "evenements" accentue : les sources se comparent sans accents.
std::string plainWord(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) out += 'e';
            else if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) out += 'a';
            else if (d == 0xA7 || d == 0x87) out += 'c';
            else if (d == 0xB4 || d == 0x94) out += 'o';
            else out += '?';
            ++i;
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

std::string clockOf(const std::string& stamp) { return stamp.size() >= 19 ? stamp.substr(11, 8) : stamp; }

std::string durationText(double s) {
    if (s < 0) return {};
    const auto total = static_cast<long long>(std::llround(s));
    const long long h = total / 3600, m = (total / 60) % 60, sec = total % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof buf, "%lld h %02lld min", h, m);
    else if (m > 0) std::snprintf(buf, sizeof buf, "%lld min %02lld s", m, sec);
    else std::snprintf(buf, sizeof buf, "%lld s", sec);
    return buf;
}

std::string percent(double f) { return formatNumber(std::round(f * 1000.0) / 10.0) + " %"; }

// Une valeur ecrite comme dans un jeu de recette (une expression) : une chaine
// entre apostrophes ($' et $$ echappes), un nombre ou un booleen tel quel.
std::string recipeLiteral(const sim::Value& v) {
    if (v.type() != sim::Type::String) return formatValue(v);
    std::string out = "'";
    for (const char c : v.asString()) {
        if (c == '\'' || c == '$') out += '$';
        out += c;
    }
    return out + "'";
}

// Deux valeurs de recette pareilles : le meme texte, ou le meme nombre (5.0 et 5).
bool sameRecipeValue(const std::string& a, const std::string& b) {
    double x = 0, y = 0;
    return a == b || (parseNumber(a, x) && parseNumber(b, y) && std::fabs(x - y) < 1e-12);
}

} // namespace

bool RecipeEditorState::dirty() const noexcept { return std::find(edited.begin(), edited.end(), true) != edited.end(); }

void Runtime::resetLot11() {
    charts_.clear();
    chartResets_.clear();
    chartNext_.clear();
    shelved_.clear();
    selectedAlarm_.clear();
    selectedZone_.clear();
    silenced_ = false;
    nextAlarmSound_ = -1;
    soundCycle_ = -1;
    soundsThisCycle_.clear();
    soundMissing_.clear();
    bannerSteps_.clear();
    production_.clear();
    editors_.clear();
    lastExport_.clear();
    exports_ = 0;
}

// ================================================================ graphiques ===
const std::vector<TrendSeries>* Runtime::chartSeries(Id view, Id object) const {
    const auto it = charts_.find(keyOf(view, object));
    return it == charts_.end() ? nullptr : &it->second;
}

void Runtime::sampleCharts(double now) {
    // 1.12.3 : le meme plan que les courbes - toutes les vues ou l'on peut naviguer, des le demarrage.
    for (const Id id : samplingPlan(now)) {
        const auto* v = viewOf(id);
        if (!v) continue;
        const auto aliases = viewAliases(id);
        for (const auto& o : v->objects) {
            if (o.kind != Kind::StateChart && o.kind != Kind::XYChart && o.kind != Kind::Histogram) continue;
            const std::string key = keyOf(id, o.id);
            auto& series = charts_[key];
            const auto eval = [&](const std::string& expr, double& out) {
                if (plcUnread(expr)) return false;   // lot 14 : pas encore lue, ou illisible
                bool ok = false;
                const std::string value = evalText(expr, &ok);
                return ok && numberOf(value, out);
            };
            if (o.kind == Kind::Histogram) {
                const std::string expr = sourceOf(o, "variable");
                if (series.size() != 1 || series[0].expression != expr) series.assign(1, TrendSeries{expr, {}});
                if (expr.empty()) continue;
                (void)plcUnread(expr);   // 1.9 : abonnee entre deux echantillons (voir keepSubscribed)
                auto& next = chartNext_[key];
                if (now + 1e-9 < next) continue;
                next = now + std::max(50.0, o.number("samplePeriod", 1000)) / 1000.0;
                double x = 0;
                if (eval(expr, x)) series[0].points.emplace_back(now, x);
                const double window = o.number("window", 600);
                auto& pts = series[0].points;
                if (window > 0) while (!pts.empty() && pts.front().first < now - window) pts.pop_front();
                const auto cap = static_cast<std::size_t>(std::clamp(o.number("maxSamples", 5000), 10.0, 200000.0));
                while (pts.size() > cap) pts.pop_front();
                continue;
            }
            const auto items = chartItems(o);
            bool same = series.size() == items.size();
            for (std::size_t i = 0; same && i < items.size(); ++i) same = series[i].expression == items[i].expression;
            if (!same) {
                series.clear();
                for (const auto& it : items) series.push_back(TrendSeries{it.expression, {}});
            }
            if (o.kind == Kind::StateChart) {
                // Les changements seulement ; on garde le dernier d'avant la fenetre
                // (l'etat au bord gauche).
                const double keep = std::max(1.0, o.number("duration", 60)) * 1.2 + 1.0;
                for (auto& s : series) {
                    double x = 0;
                    if (eval(s.expression, x) && (s.points.empty() || s.points.back().second != x)) s.points.emplace_back(now, x);
                    while (s.points.size() >= 2 && s.points[1].first < now - keep) s.points.pop_front();
                }
                continue;
            }
            // Courbe XY : une remise a zero sur front montant de "reset".
            const std::string reset = sourceOf(o, "reset");
            if (!reset.empty()) {
                bool ok = false;
                const std::string value = evalText(reset, &ok);
                if (ok) {
                    auto& last = chartResets_[key];
                    if (truthy(value) && !truthy(last)) {
                        for (auto& s : series) s.points.clear();
                        log("Action", where(*v, &o), "courbe XY remise \xC3\xA0 z\xC3\xA9ro");
                    }
                    last = value;
                }
            }
            const std::string xExpr = sourceOf(o, "xVariable");
            double x = 0;
            if (xExpr.empty() || !eval(xExpr, x)) continue;
            const auto cap = static_cast<std::size_t>(std::clamp(o.number("maxPoints", 300), 2.0, 20000.0));
            for (auto& s : series) {
                double y = 0;
                if (eval(s.expression, y)) s.points.emplace_back(x, y);
                while (s.points.size() > cap) s.points.pop_front();
            }
        }
    }
}

// ================================================================== alarmes ===
bool Runtime::isShelved(Id alarm) const {
    return std::any_of(shelved_.begin(), shelved_.end(), [&](const ShelvedAlarm& s) { return s.alarm == alarm; });
}

void Runtime::selectAlarm(std::string_view name) { selectedAlarm_ = std::string(name); }

std::size_t Runtime::shelve(std::string_view target, double minutes, std::string_view reason, double now, std::string* why) {
    now_ = std::max(now_, now);
    const auto fail = [&](std::string text) -> std::size_t {
        if (why) *why = std::move(text);
        return 0;
    };
    if (!project_) return fail("aucun projet");
    if (project_->security.enabled && !permitted("Acquitter")) {
        const std::string reasonText = "mettre de c\xC3\xB4t\xC3\xA9 : permission \xC2\xAB Acquitter \xC2\xBB requise ("
                                     + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", "mise de c\xC3\xB4t\xC3\xA9", reasonText);
        return fail(reasonText);
    }
    std::string wanted = trimmedText(target);
    if (wanted.empty()) wanted = selectedAlarm_;
    if (wanted.empty()) return fail("aucune alarme choisie (cliquer une alarme dans un bandeau ou une liste)");
    std::vector<const AlarmDef*> defs;
    if (wanted.rfind("groupe:", 0) == 0) {
        const std::string group = wanted.substr(7);
        for (const auto& d : project_->alarms) if (d.group == group) defs.push_back(&d);
        // 1.9 : les alarmes des objets - un groupe d'objet, ou leur groupe declare.
        refreshObjectAlarms();
        for (const auto& d : objectAlarmDefsIn(group)) defs.push_back(d);
        if (defs.empty()) return fail("aucune alarme dans le groupe " + group);
    } else if (const auto* d = alarmDefByName(wanted)) {
        defs.push_back(d);
    } else {
        return fail("alarme '" + wanted + "' introuvable");
    }
    const int limit = std::max(1, project_->alarmSettings.maxShelveMin);
    const bool unlimited = minutes <= 0;
    const double shown = unlimited ? 0.0 : std::min(minutes, static_cast<double>(limit));
    const std::string what = unlimited ? std::string("sans limite") : formatNumber(std::round(shown * 10) / 10) + " min";
    const std::string by = user_;
    const std::string text = std::string(reason);
    for (const auto* d : defs) {
        auto it = std::find_if(shelved_.begin(), shelved_.end(), [&](const ShelvedAlarm& s) { return s.alarm == d->id; });
        if (it == shelved_.end()) {
            shelved_.push_back({});
            it = shelved_.end() - 1;
        }
        it->alarm = d->id;
        it->name = d->name;
        it->group = d->group;
        if (const auto* oa = objectAlarm(d->id)) {        // 1.9
            it->objectGroup = oa->objectGroup;
            it->symbols = oa->symbols;
        }
        it->priority = d->priority;
        it->by = by;
        it->reason = text;
        it->since = dateStampOf(now);
        it->until = unlimited ? -1.0 : now + shown * 60.0;
        // Elle quitte la liste des alarmes en cours ; elle reviendra, si sa
        // condition est encore vraie, a la fin de la mise de cote.
        alarms_.erase(std::remove_if(alarms_.begin(), alarms_.end(), [&](const LiveAlarm& a) { return a.alarm == d->id; }), alarms_.end());
        alarmPending_.erase(d->id);
        event("Mise de c\xC3\xB4t\xC3\xA9", d->name,
              what + (by.empty() ? std::string{} : ", par " + by) + (text.empty() ? std::string{} : " : " + text)
                  + (!unlimited && minutes > limit ? " (limit\xC3\xA9" "e \xC3\xA0 " + std::to_string(limit) + " min)" : std::string{}));
        audit("Mise de c\xC3\xB4t\xC3\xA9", {}, d->name, "en service", "de c\xC3\xB4t\xC3\xA9 (" + what + ")", text);   // lot 13
    }
    return defs.size();
}

std::size_t Runtime::unshelve(std::string_view target, double now, std::string* why) {
    now_ = std::max(now_, now);
    if (project_ && project_->security.enabled && !permitted("Acquitter")) {
        const std::string reasonText = "remettre en service : permission \xC2\xAB Acquitter \xC2\xBB requise ("
                                     + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", "mise de c\xC3\xB4t\xC3\xA9", reasonText);
        if (why) *why = reasonText;
        return 0;
    }
    std::string wanted = trimmedText(target);
    if (wanted.empty()) wanted = selectedAlarm_;
    if (wanted.empty()) {
        if (why) *why = "aucune alarme choisie";
        return 0;
    }
    const bool all = wanted == "*";
    const bool group = wanted.rfind("groupe:", 0) == 0;
    std::size_t done = 0;
    for (std::size_t i = shelved_.size(); i-- > 0;) {
        const auto& s = shelved_[i];
        if (!(all || (group ? alarmGroupMatches(s.group, s.objectGroup, s.symbols, wanted.substr(7)) : s.name == wanted))) continue;
        event("Fin de mise de c\xC3\xB4t\xC3\xA9", s.name, "remise en service" + (user_.empty() ? std::string{} : " par " + user_));
        audit("Remise en service", {}, s.name, "de c\xC3\xB4t\xC3\xA9", "en service");      // lot 13
        shelved_.erase(shelved_.begin() + static_cast<long>(i));
        ++done;
    }
    if (done == 0 && why) *why = "rien de mis de c\xC3\xB4t\xC3\xA9 pour " + wanted;
    return done;
}

void Runtime::silenceAlarms(double now, const std::string& source) {
    now_ = std::max(now_, now);
    silenced_ = true;
    nextAlarmSound_ = -1;
    if (hooks_.stopSounds) hooks_.stopSounds();          // 1.12.3 : ce qui joue se tait
    event("Silence", source.empty() ? std::string("alarmes") : source, "alarme sonore coup\xC3\xA9" "e jusqu'\xC3\xA0 la prochaine apparition");
    audit("Silence", source, "alarmes sonores", "son", "coup\xC3\xA9");       // lot 13
}

// 1.10.2 (AL) : le son d'une alarme - celui de son groupe, sinon celui de sa priorite.
std::string Runtime::alarmSound(const LiveAlarm& a) const {
    if (!project_) return {};
    const std::string groupSound = a.group.empty() ? std::string{} : alarmGroupSettings(*project_, a.group).sound;
    if (!groupSound.empty()) return groupSound;
    return project_->alarmSettings.sounds[static_cast<std::size_t>(std::clamp(a.priority, 1, kAlarmPriorities) - 1)];
}

void Runtime::alarmAppeared(const LiveAlarm& a, double now) {
    silenced_ = false;
    if (!project_) return;
    const auto& as = project_->alarmSettings;
    const std::string name = alarmSound(a);
    if (as.repeatS > 0) nextAlarmSound_ = now + as.repeatS;
    if (name.empty()) return;
    // 1.12.3 : plusieurs alarmes dans le meme cycle - leurs sons jouent ensemble (ils
    // se melangent) ; un meme son ne part qu'une fois.
    if (soundCycle_ != cycles_) {
        soundCycle_ = cycles_;
        soundsThisCycle_.clear();
    }
    if (soundsThisCycle_.count(name)) return;
    if (!project_->resourceByName(name)) {
        if (soundMissing_.insert(name).second) log("Erreur", "alarme " + a.name, "son d'alarme '" + name + "' introuvable (Configuration > Alarmes)");
        return;
    }
    soundsThisCycle_.insert(name);
    (void)playSound(name, "alarme " + a.name);
}

void Runtime::alarmsCycle(double now) {
    // Les mises de cote qui arrivent a leur fin.
    for (std::size_t i = shelved_.size(); i-- > 0;) {
        if (shelved_[i].until < 0 || now + 1e-9 < shelved_[i].until) continue;
        event("Fin de mise de c\xC3\xB4t\xC3\xA9", shelved_[i].name, "d\xC3\xA9lai \xC3\xA9" "coul\xC3\xA9");
        shelved_.erase(shelved_.begin() + static_cast<long>(i));
    }
    // Le son qui se repete tant qu'une alarme attend son acquittement.
    if (!project_) return;
    const auto& as = project_->alarmSettings;
    const int p = highestPriority();
    if (p == 0 || silenced_ || as.repeatS <= 0) {
        if (p == 0) nextAlarmSound_ = -1;
        return;
    }
    if (nextAlarmSound_ < 0) nextAlarmSound_ = now + as.repeatS;
    if (now + 1e-9 < nextAlarmSound_) return;
    nextAlarmSound_ = now + as.repeatS;
    // 1.12.3 : la repetition reprend le son de l'alarme la plus grave a acquitter - celui
    // de son groupe s'il en a un (la plus recente a priorite egale).
    const LiveAlarm* worst = nullptr;
    for (const auto& a : alarms_)
        if (!a.acked && a.priority == p) worst = &a;
    const std::string name = worst ? alarmSound(*worst) : as.sounds[static_cast<std::size_t>(std::clamp(p, 1, kAlarmPriorities) - 1)];
    if (!name.empty() && project_->resourceByName(name)) (void)playSound(name, "alarmes (r\xC3\xA9p\xC3\xA9tition)");
}

int Runtime::bannerAlarm(const Object& o) const {
    const auto it = bannerSteps_.find(o.id);
    // 1.11 (REP-1, decision 74) : le groupe se lit sans les $ de ses reperes ($Zone_A$ : Zone_A).
    return bannerPick(alarms_, markers::strip(o.text("group"), markers::Mode::Text), o.text("show", "la plus grave"), now_ - startNow_,
                      static_cast<int>(o.number("period", 4000)), it == bannerSteps_.end() ? 0 : it->second);
}

// ================================================================ production ===
ProductionFigures Runtime::production(Id object) const {
    const auto it = production_.find(object);
    return it == production_.end() ? ProductionFigures{} : it->second.figures;
}

void Runtime::productionReset(const Object&, ProductionState& st, double now, const std::string&) {
    st.baseGood = st.lastGood + st.offsetGood;
    st.baseBad = st.lastBad + st.offsetBad;
    st.runSeconds = 0;
    st.shiftStart = now;
    st.totals.clear();
}

void Runtime::productionCycle(double now) {
    std::vector<Id> shown{current_};
    shown.insert(shown.end(), popups_.begin(), popups_.end());
    for (const Id id : shown) {
        const auto* v = viewOf(id);
        if (!v) continue;
        const auto aliases = viewAliases(id);
        for (const auto& o : v->objects) {
            if (o.kind != Kind::ProductionCounter) continue;
            auto& st = production_[o.id];
            const auto read = [&](const char* key, double fallback) {
                const std::string expr = sourceOf(o, key);
                if (expr.empty()) return fallback;
                bool ok = false;
                const std::string value = evalText(expr, &ok);
                double x = fallback;
                return ok && numberOf(value, x) ? x : fallback;
            };
            const double good = read("good", st.lastGood), bad = read("bad", st.lastBad);
            // Un compteur qui redescend : l'automate l'a remis a zero ; on garde le compte.
            if (st.started && good < st.lastGood - 1e-9) st.offsetGood += st.lastGood;
            if (st.started && bad < st.lastBad - 1e-9) st.offsetBad += st.lastBad;
            st.lastGood = good;
            st.lastBad = bad;
            const std::string runExpr = sourceOf(o, "running");
            const bool running = runExpr.empty() ? true : evalBool(runExpr, false);
            // Le poste en cours (l'heure du poste).
            std::vector<int> shifts;
            (void)parseShifts(o.text("shifts"), shifts);
            const std::string stamp = dateStampOf(now);
            const int minute = stamp.size() >= 16 ? std::atoi(stamp.substr(11, 2).c_str()) * 60 + std::atoi(stamp.substr(14, 2).c_str()) : 0;
            const int shift = currentShift(shifts, minute);
            if (!st.started) {
                st.started = true;
                productionReset(o, st, now, {});
                st.shiftMinute = shift;
                st.shift = shift >= 0 ? "Poste de " + minutesText(shift) : "Depuis " + clockOf(stamp);
            } else if (shift >= 0 && shift != st.shiftMinute) {
                st.shiftMinute = shift;
                productionReset(o, st, now, "poste");
                st.shift = "Poste de " + minutesText(shift);
                event("Production", where(*v, &o), "nouveau poste : " + st.shift + ", compteurs \xC3\xA0 z\xC3\xA9ro");
            }
            const double dt = st.lastTick >= 0 ? std::max(0.0, now - st.lastTick) : 0.0;
            st.lastTick = now;
            if (running) st.runSeconds += dt;
            st.running = running;
            const double g = good + st.offsetGood - st.baseGood, b = bad + st.offsetBad - st.baseBad;
            const double total = std::max(0.0, g) + std::max(0.0, b);
            st.totals.emplace_back(now, total);
            double window = 300;
            (void)numberProp(o, "rateWindow", window);
            window = std::max(10.0, window);
            while (st.totals.size() > 2 && st.totals.front().first < now - window) st.totals.pop_front();
            const double span = st.totals.back().first - st.totals.front().first;
            const double rate = span >= 1.0 ? (st.totals.back().second - st.totals.front().second) / span * 3600.0 : 0.0;
            double ideal = 0, target = 0;
            (void)numberProp(o, "idealRate", ideal);
            (void)numberProp(o, "target", target);
            st.figures = productionFigures(g, b, st.runSeconds, now - st.shiftStart, ideal, rate, target);
            st.figures.running = running;
            st.figures.shift = st.shift;
        }
    }
}

// ======================================================== editeur de recette ===
const RecipeEditorState* Runtime::recipeEditor(Id object) const {
    const auto it = editors_.find(object);
    return it == editors_.end() ? nullptr : &it->second;
}

RecipeEditorState& Runtime::editorOf(const Object& o) {
    auto& st = editors_[o.id];
    const std::string recipeName = markers::strip(o.text("recipe"), markers::Mode::Text);   // 1.11 (REP-1) : sans ses $
    if (st.recipe != recipeName) {
        st = RecipeEditorState{};
        st.recipe = recipeName;
    }
    const Recipe* r = project_ ? project_->recipeByName(recipeName) : nullptr;
    if (!r) {
        st.record = kNoId;
        st.values.clear();
        st.edited.clear();
        return st;
    }
    if (st.record == kNoId || !r->record(st.record)) {
        st.record = r->records.empty() ? kNoId : r->records.front().id;
        st.edited.assign(r->fields.size(), false);
    }
    const RecipeRecord* rec = st.record != kNoId ? r->record(st.record) : nullptr;
    const std::size_t n = r->fields.size();
    st.values.resize(n);
    st.edited.resize(n, false);
    for (std::size_t i = 0; i < n; ++i)
        if (!st.edited[i]) st.values[i] = rec && i < rec->values.size() ? rec->values[i] : std::string{};
    return st;
}

void Runtime::editorsCycle() {
    std::vector<Id> shown{current_};
    shown.insert(shown.end(), popups_.begin(), popups_.end());
    for (const Id id : shown) {
        const auto* v = viewOf(id);
        if (!v) continue;
        for (const auto& o : v->objects) {
            if (o.kind != Kind::RecipeEditor) continue;
            auto& st = editorOf(o);
            std::vector<std::string> values;
            (void)readRecipe(st.recipe, values);
            st.installed = std::move(values);
        }
    }
}

// ============================================================= parties =========
void Runtime::lot11Part(const View& v, const Object& o, std::string_view part, double now) {
    const auto aliases = viewAliases(v.id);
    const std::string source = where(v, &o);
    auto& f = forms_[o.id];
    const auto focusOn = [&](const std::string& field, std::string text) {
        if (focused_ == o.id && f.focus == field) return;      // deja en saisie : on garde ce qui est tape
        f.text[field] = std::move(text);
        f.focus = field;
        f.caret[field] = f.text[field].size();
        f.fresh = true;
        f.message.clear();
        f.error = false;
        focused_ = o.id;
    };
    const auto refusedWithout = [&](const char* permission) {
        if (!project_ || !project_->security.enabled || permitted(permission)) return false;
        const std::string refused = std::string(kindLabel(o.kind)) + " : permission \xC2\xAB " + permission + " \xC2\xBB requise ("
                                  + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", source, refused);
        formMessage(o.id, std::string("permission ") + permission + " requise", true, now);
        return true;
    };
    switch (o.kind) {
        case Kind::History: {
            // Une ligne d'une liste d'alarmes : l'alarme choisie (consigne, mise de cote).
            const int row = indexAfter(part, "ligne:");
            if (row < 0) return;
            const std::string src = plainWord(o.text("source", "alarmes"));
            if (src.rfind("mise", 0) == 0) {
                const auto rows = historyShelvedRows(shelved_, markers::strip(o.text("group"), markers::Mode::Text));   // 1.11 (REP-1)
                if (static_cast<std::size_t>(row) < rows.size()) selectAlarm(shelved_[rows[static_cast<std::size_t>(row)]].name);
            } else if (src == "alarmes" || src.rfind("acquitt", 0) == 0) {
                const auto rows = historyAlarmRows(alarms_, src == "alarmes" ? "alarmes" : "acquitt\xC3\xA9" "es",
                                                   markers::strip(o.text("group"), markers::Mode::Text));   // 1.11 (REP-1)
                if (static_cast<std::size_t>(row) < rows.size()) selectAlarm(alarms_[rows[static_cast<std::size_t>(row)]].name);
            }
            return;
        }
        case Kind::AlarmBanner: {
            if (part == "suivante") { ++bannerSteps_[o.id]; return; }
            if (part != "acquitter") return;
            const int k = bannerAlarm(o);
            if (k < 0) { log("Action", source, "acquitter : aucune alarme dans le bandeau"); return; }
            const std::string name = alarms_[static_cast<std::size_t>(k)].name;
            selectAlarm(name);
            std::string why;
            if (acknowledge(name, now, &why) == 0) log("Action", source, "acquitter " + name + " : " + why);
            return;
        }
        case Kind::AlarmSummary: {
            const int k = indexAfter(part, "zone:");
            const auto zones = zonesOf(*project_, o.text("groups"));
            if (k < 0 || static_cast<std::size_t>(k) >= zones.size()) return;
            selectedZone_ = zones[static_cast<std::size_t>(k)];
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            if (!var.empty()) (void)write(var, sim::Value::text(selectedZone_), source);
            log("Action", source, "zone choisie : " + selectedZone_);
            return;
        }
        case Kind::ProductionCounter: {
            if (part != "raz" || refusedWithout("Piloter")) return;
            auto& st = production_[o.id];
            productionReset(o, st, now, "raz");
            st.shift = "Depuis " + clockOf(dateStampOf(now));
            event("Production", source, "compteurs remis \xC3\xA0 z\xC3\xA9ro" + (user_.empty() ? std::string{} : " par " + user_));
            return;
        }
        case Kind::VariableTable: {
            const int row = indexAfter(part, "ligne:");
            const auto rows = variableRows(o);
            if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) return;
            const auto& r = rows[static_cast<std::size_t>(row)];
            if (!o.flag("writable", true)) { formMessage(o.id, "tableau en lecture seule", true, now); return; }
            if (!isVariablePath(r.expression)) {
                formMessage(o.id, "\xC2\xAB " + r.expression + " \xC2\xBB est un calcul : en lecture seule", true, now);
                return;
            }
            if (refusedWithout("Piloter")) return;
            sim::Value cur;
            std::string text;
            if (environment().read(r.expression, cur))
                text = cur.type() == sim::Type::String ? cur.asString()
                     : cur.type() == sim::Type::Real   ? formatValue(cur, rowFormat(project_, o, static_cast<std::size_t>(row)))
                                                       : formatValue(cur);
            focusOn("ligne:" + std::to_string(row), text);
            return;
        }
        case Kind::RecipeEditor: {
            auto& st = editorOf(o);
            const Recipe* r = project_ ? project_->recipeByName(st.recipe) : nullptr;
            const auto say = [&](std::string text, bool error) {
                st.message = std::move(text);
                st.error = error;
                st.messageAt = now;
            };
            if (!r) { say("aucune recette (propri\xC3\xA9t\xC3\xA9 Recette)", true); return; }
            const RecipeRecord* rec = st.record != kNoId ? r->record(st.record) : nullptr;
            if (part == "precedent" || part == "suivant") {
                if (r->records.empty()) return;
                if (st.dirty()) { say("modifications non enregistr\xC3\xA9" "es : Enregistrer ou Annuler d'abord", true); return; }
                std::size_t at = 0;
                for (std::size_t k = 0; k < r->records.size(); ++k) if (r->records[k].id == st.record) at = k;
                const std::size_t n = r->records.size();
                at = part == "suivant" ? (at + 1) % n : (at + n - 1) % n;
                st.record = r->records[at].id;
                st.edited.assign(r->fields.size(), false);
                (void)editorOf(o);
                st.message.clear();
                return;
            }
            if (const int row = indexAfter(part, "ligne:"); row >= 0) {
                if (!rec) { say("aucun jeu : Nouveau en cr\xC3\xA9" "e un", true); return; }
                if (static_cast<std::size_t>(row) >= r->fields.size()) return;
                // Un element de texte se tape sans ses apostrophes (elles reviennent a
                // la validation) ; les autres, tels quels.
                std::string shown = st.values[static_cast<std::size_t>(row)];
                sim::Value target;
                const auto& fd = r->fields[static_cast<std::size_t>(row)];
                if (!fd.variable.empty() && environment().read(fd.variable, target) && target.type() == sim::Type::String)
                    shown = recipeValueShown(shown);
                focusOn("ligne:" + std::to_string(row), shown);
                return;
            }
            if (part.rfind("bouton:", 0) != 0) return;
            const std::string button(part.substr(7));
            if (refusedWithout("Recettes")) { say("permission Recettes requise", true); return; }
            if (focused_ == o.id) { f.focus.clear(); focused_ = kNoId; }
            if (button == "Enregistrer") {
                if (!rec) { say("aucun jeu \xC3\xA0 enregistrer", true); return; }
                if (!st.dirty()) { say("rien \xC3\xA0 enregistrer", false); return; }
                if (!hooks_.recipeRequest) { say("l'\xC3\xA9" "cran ne sait pas enregistrer ici", true); return; }
                RecipeRequest rq;
                rq.op = "enregistrer";
                rq.object = o.id;
                rq.recipe = r->name;
                rq.record = rec->id;
                rq.recordName = rec->name;
                rq.values = st.values;
                rq.source = source;
                const std::string name = rec->name;
                hooks_.recipeRequest(rq);
                st.edited.assign(st.edited.size(), false);
                event("Recette", r->name, name + " enregistr\xC3\xA9" + (user_.empty() ? std::string{} : " par " + user_) + " (" + source + ")");
                say(name + " enregistr\xC3\xA9", false);
            } else if (button == "Appliquer") {
                if (!rec) { say("aucun jeu \xC3\xA0 appliquer", true); return; }
                std::string why;
                const bool dirty = st.dirty();
                if (applyValues(*r, st.values, rec->name + (dirty ? " (modifi\xC3\xA9)" : ""), now, &why))
                    say(rec->name + " appliqu\xC3\xA9" + (dirty ? " (valeurs modifi\xC3\xA9" "es, pas enregistr\xC3\xA9" "es)" : ""), false);
                else say(why, true);
            } else if (button == "Lire") {
                // Les valeurs de l'installation, ecrites comme des valeurs de recette
                // (une chaine entre apostrophes : elle se rappliquera telle quelle).
                std::size_t changed = 0;
                for (std::size_t k = 0; k < r->fields.size() && k < st.values.size(); ++k) {
                    sim::Value cur;
                    if (r->fields[k].variable.empty() || !environment().read(r->fields[k].variable, cur)) continue;
                    const std::string value = recipeLiteral(cur);
                    if (sameRecipeValue(value, st.values[k])) continue;
                    st.values[k] = value;
                    st.edited[k] = true;
                    ++changed;
                }
                say(changed ? std::to_string(changed) + " valeur(s) lue(s) dans l'installation : Enregistrer pour les garder"
                            : std::string("l'installation a d\xC3\xA9j\xC3\xA0 ces valeurs"), false);
            } else if (button == "Annuler") {
                st.edited.assign(st.edited.size(), false);
                (void)editorOf(o);
                say("modifications abandonn\xC3\xA9" "es", false);
            } else if (button == "Nouveau") {
                if (!hooks_.recipeRequest) { say("l'\xC3\xA9" "cran ne sait pas cr\xC3\xA9" "er de jeu ici", true); return; }
                RecipeRequest rq;
                rq.op = "ajouter";
                rq.object = o.id;
                rq.recipe = r->name;
                rq.values = st.values;
                rq.source = source;
                hooks_.recipeRequest(rq);
            } else {
                log("Erreur", source, "bouton inconnu : " + button);
            }
            return;
        }
        default:
            return;
    }
}

bool Runtime::lot11Submit(const View& v, const Object& o, double now) {
    const auto aliases = viewAliases(v.id);
    const std::string source = where(v, &o);
    auto& f = forms_[o.id];
    const int row = indexAfter(f.focus, "ligne:");
    if (row < 0) return false;
    const std::string field = f.focus;
    const std::string text = trimmedText(f.text[field]);
    const auto done = [&] {
        f.text.erase(field);
        f.focus.clear();
        f.fresh = false;
        if (focused_ == o.id) focused_ = kNoId;
    };
    const auto fail = [&](const std::string& message) {
        formMessage(o.id, message, true, now);
        return false;
    };
    if (o.kind == Kind::VariableTable) {
        const auto rows = variableRows(o);
        if (static_cast<std::size_t>(row) >= rows.size()) { done(); return false; }
        const std::string var = rows[static_cast<std::size_t>(row)].expression;
        if (project_ && project_->security.enabled && !permitted("Piloter")) return fail("permission Piloter requise");
        sim::Value cur;
        if (!environment().read(var, cur)) return fail("variable inconnue : " + var);
        sim::Value next;
        double x = 0;
        switch (cur.type()) {
            case sim::Type::String: next = sim::Value::text(text); break;
            case sim::Type::Bool: {
                const std::string t = plainWord(text);
                if (t == "1" || t == "true" || t == "vrai" || t == "oui") next = sim::Value::boolean(true);
                else if (t == "0" || t == "false" || t == "faux" || t == "non") next = sim::Value::boolean(false);
                else return fail("vrai ou faux attendu (1, 0, TRUE, FALSE)");
                break;
            }
            case sim::Type::Real:
                if (!parseNumber(text, x)) return fail("un nombre est attendu");
                next = sim::Value::real(x);
                break;
            default:
                if (!parseNumber(text, x)) return fail("un nombre est attendu");
                if (std::fabs(x - std::round(x)) > 1e-9) return fail("un entier est attendu");
                next = sim::Value::integer(cur.type() == sim::Type::Unknown ? sim::Type::Int : cur.type(), static_cast<std::int64_t>(std::llround(x)));
                break;
        }
        if (!write(var, next, source)) return fail("\xC3\xA9" "criture refus\xC3\xA9" "e");
        sim::Value after;
        (void)environment().read(var, after);
        log("Action", source, "saisie : " + var + " = " + formatValue(after));
        f.message.clear();
        f.error = false;
        done();
        return true;
    }
    if (o.kind == Kind::RecipeEditor) {
        auto& st = editorOf(o);
        const Recipe* r = project_ ? project_->recipeByName(st.recipe) : nullptr;
        if (!r || static_cast<std::size_t>(row) >= r->fields.size()) { done(); return false; }
        const auto& fd = r->fields[static_cast<std::size_t>(row)];
        std::string value = text;
        // Un element de texte (sa variable est une chaine) : ce qui est tape sans
        // apostrophes (Azote, Azote N2) est une chaine.
        sim::Value target;
        if (!value.empty() && value.front() != '\'' && !fd.variable.empty() && environment().read(fd.variable, target)
            && target.type() == sim::Type::String)
            value = recipeLiteral(sim::Value::text(value));
        if (!value.empty() && value.front() != '\'' && !Expression::compile(value).valid()) return fail("valeur illisible : " + value);
        double number = 0, lo = 0, hi = 0;
        if (parseNumber(value, number)
            && ((!fd.min.empty() && parseNumber(fd.min, lo) && number < lo) || (!fd.max.empty() && parseNumber(fd.max, hi) && number > hi)))
            return fail(fd.name + " : de " + (fd.min.empty() ? std::string("-") : fd.min) + " \xC3\xA0 " + (fd.max.empty() ? std::string("-") : fd.max));
        const RecipeRecord* rec = st.record != kNoId ? r->record(st.record) : nullptr;
        const std::string saved = rec && static_cast<std::size_t>(row) < rec->values.size() ? rec->values[static_cast<std::size_t>(row)] : std::string{};
        st.values[static_cast<std::size_t>(row)] = value;
        st.edited[static_cast<std::size_t>(row)] = !sameRecipeValue(value, saved);
        st.message = st.dirty() ? "modifi\xC3\xA9 : Enregistrer pour garder, Annuler pour revenir" : std::string{};
        st.error = false;
        st.messageAt = now;
        f.message.clear();
        f.error = false;
        done();
        return true;
    }
    return false;
}

// =================================================================== export ===
bool Runtime::exportTable(std::string_view sourceText, ExportTable& t, std::string* why) {
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    t = ExportTable{};
    const std::string source = trimmedText(sourceText);
    const std::string word = plainWord(source);
    t.subtitle = dateStampOf(now_).substr(0, 19) + (project_ ? " - " + project_->config.name : std::string{});
    if (word == "alarmes" || word.empty()) {
        t.title = "Alarmes en cours";
        t.headers = {"Apparue", "Priorit\xC3\xA9", "Alarme", "Message", "Groupe", "\xC3\x89tat", "Acquitt\xC3\xA9" "e par"};
        for (const auto& a : alarms_)
            t.rows.push_back({a.appeared.substr(0, 19), std::to_string(a.priority) + " - " + std::string(alarmPriorityLabel(a.priority)),
                              a.name, a.message, a.group, a.state(), a.ackedBy});
        return true;
    }
    if (word == "historique") {
        t.title = "Historique des alarmes";
        t.headers = {"Apparue", "Acquitt\xC3\xA9" "e", "Disparue", "Alarme", "Priorit\xC3\xA9", "Groupe", "Message", "Par"};
        const auto add = [&](const AlarmOccurrence& a) {
            t.rows.push_back({a.appeared.substr(0, 19), a.acked.substr(0, std::min<std::size_t>(19, a.acked.size())),
                              a.cleared.substr(0, std::min<std::size_t>(19, a.cleared.size())), a.name, std::to_string(a.priority), a.group,
                              a.message, a.ackedBy});
        };
        if (history_) for (auto it = history_->alarms.rbegin(); it != history_->alarms.rend(); ++it) add(*it);
        else for (auto it = closed_.rbegin(); it != closed_.rend(); ++it) add(*it);
        return true;
    }
    if (word == "evenements") {
        t.title = "\xC3\x89v\xC3\xA9nements";
        t.headers = {"Heure", "Type", "Source", "Message", "Utilisateur"};
        for (auto it = events_.rbegin(); it != events_.rend(); ++it)
            t.rows.push_back({it->stamp.substr(0, std::min<std::size_t>(19, it->stamp.size())), it->kind, it->source, it->message, it->user});
        return true;
    }
    if (word == "systeme") {
        t.title = "Journal syst\xC3\xA8me";
        t.headers = {"Heure", "Type", "Source", "Message"};
        for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) t.rows.push_back({it->stamp, it->kind, it->source, it->message});
        return true;
    }
    // Lot 13 : le journal d'audit, avec ses empreintes (la chaine se verifie hors de l'IHM).
    if (word == "audit") {
        t.title = "Journal d'audit";
        t.headers = {"Horodatage", "Utilisateur", "Type", "Source", "Cible", "Avant", "Apr\xC3\xA8s", "Motif", "Signature", "Empreinte"};
        for (const auto& e : auditTrail())
            t.rows.push_back({e.stamp, e.user, e.kind, e.source, e.target, e.before, e.after, e.reason, e.signature, e.hash});
        const auto check = verifyAudit(auditTrail());
        t.subtitle += " - " + check.message;
        return true;
    }
    if (word == "mesures") {
        t.title = "Mesures archiv\xC3\xA9" "es";
        t.headers = {"Horodatage", "Variable", "Valeur"};
        if (!history_) return fail("aucun historique des mesures ici");
        for (const auto& s : history_->samples) t.rows.push_back({s.stamp.substr(0, std::min<std::size_t>(19, s.stamp.size())), s.variable, formatNumber(s.value)});
        return true;
    }
    if (word.rfind("recette:", 0) == 0) {
        const std::string name = trimmedText(source.substr(8));
        const auto* r = project_ ? project_->recipeByName(name) : nullptr;
        if (!r) return fail("recette '" + name + "' introuvable");
        t.title = "Recette " + r->name;
        t.headers = {"Jeu"};
        for (const auto& fd : r->fields) t.headers.push_back(fd.name + (fd.unit.empty() ? std::string{} : " (" + fd.unit + ")"));
        for (const auto& rec : r->records) {
            std::vector<std::string> row{rec.name};
            for (std::size_t k = 0; k < r->fields.size(); ++k) {
                std::string v = k < rec.values.size() ? rec.values[k] : std::string{};
                if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
                row.push_back(v);
            }
            t.rows.push_back(std::move(row));
        }
        return true;
    }
    if (word.rfind("objet:", 0) != 0) return fail("source inconnue : " + source + " (alarmes, historique, \xC3\xA9v\xC3\xA9nements, syst\xC3\xA8me, mesures, audit, recette:Nom, objet:Nom)");
    const std::string name = trimmedText(source.substr(6));
    // L'objet : dans la popup du dessus d'abord, puis la vue.
    const View* view = nullptr;
    const Object* o = nullptr;
    std::vector<Id> order(popups_.rbegin(), popups_.rend());
    order.push_back(current_);
    for (const Id id : order) {
        const auto* v = viewOf(id);
        if (!v) continue;
        for (const auto& x : v->objects)
            if (x.name == name) { view = v; o = &x; break; }
        if (o) break;
    }
    if (!o) return fail("objet '" + name + "' introuvable dans la vue affich\xC3\xA9" "e");
    const View snapshot = *view;
    const Object obj = *o;
    const auto aliases = viewAliases(snapshot.id);
    t.title = obj.text("text").empty() || obj.kind == Kind::ExportButton ? obj.name : TextTemplate::compile(obj.text("text")).render(environment());
    if (t.title.empty()) t.title = obj.name;
    const auto evalCell = [&](const std::string& expr) {
        bool ok = false;
        const std::string v = evalText(expr, &ok);
        return ok ? v : std::string("###");
    };
    switch (obj.kind) {
        case Kind::Trend: {
            const auto* series = trend(snapshot.id, obj.id);
            const auto names = splitSemicolons(obj.text("names"));
            t.headers = {"Temps (s)"};
            std::vector<std::string> pens;
            if (series) for (std::size_t i = 0; i < series->size(); ++i) {
                const std::string n = i < names.size() && !trimmedText(names[i]).empty() ? trimmedText(names[i]) : (*series)[i].expression;
                t.headers.push_back(n);
            }
            if (series && !series->empty())
                for (std::size_t k = 0; k < (*series)[0].points.size(); ++k) {
                    const double time = (*series)[0].points[k].first;
                    std::vector<std::string> row{formatNumber(std::round((time - startNow_) * 100) / 100)};
                    for (const auto& s : *series) {
                        std::string cell;
                        for (const auto& p : s.points) if (std::fabs(p.first - time) < 1e-6) { cell = formatNumber(p.second); break; }
                        row.push_back(cell);
                    }
                    t.rows.push_back(std::move(row));
                }
            return true;
        }
        case Kind::XYChart: {
            const auto* series = chartSeries(snapshot.id, obj.id);
            const auto items = chartItems(obj);
            t.headers = {"Courbe", obj.text("xLabel").empty() ? std::string("X") : obj.text("xLabel"),
                         obj.text("yLabel").empty() ? std::string("Y") : obj.text("yLabel")};
            if (series) for (std::size_t i = 0; i < series->size() && i < items.size(); ++i)
                for (const auto& p : (*series)[i].points) t.rows.push_back({chartItemLabel(items[i]), formatNumber(p.first), formatNumber(p.second)});
            return true;
        }
        case Kind::StateChart: {
            const auto* series = chartSeries(snapshot.id, obj.id);
            const auto items = chartItems(obj);
            t.headers = {"Ligne", "D\xC3\xA9" "but (s)", "Fin (s)", "Dur\xC3\xA9" "e (s)", "Valeur"};
            if (series) for (std::size_t i = 0; i < series->size() && i < items.size(); ++i) {
                const double end = now_;
                const double start = end - std::max(1.0, obj.number("duration", 60));
                for (const auto& seg : stateSegments((*series)[i].points, start, end))
                    t.rows.push_back({chartItemLabel(items[i]), formatNumber(std::round((seg.from - startNow_) * 10) / 10),
                                      formatNumber(std::round((seg.to - startNow_) * 10) / 10), formatNumber(std::round((seg.to - seg.from) * 10) / 10),
                                      formatNumber(seg.value)});
            }
            return true;
        }
        case Kind::Histogram: {
            const auto* series = chartSeries(snapshot.id, obj.id);
            std::vector<double> samples;
            if (series && !series->empty()) for (const auto& p : (*series)[0].points) samples.push_back(p.second);
            const double lo = obj.number("min", 0), hi = obj.number("max", 100);
            const int bins = static_cast<int>(obj.number("bins", 10));
            double low = 0, high = 0;
            const bool hasLow = parseNumber(obj.text("low"), low), hasHigh = parseNumber(obj.text("high"), high);
            const auto h = histogram(samples, lo, hi, bins, hasLow ? std::optional<double>(low) : std::nullopt,
                                     hasHigh ? std::optional<double>(high) : std::nullopt);
            t.headers = {"Classe", "De", "\xC3\x80", "Nombre"};
            const double step = (hi > lo ? hi - lo : 1.0) / static_cast<double>(h.counts.size());
            for (std::size_t k = 0; k < h.counts.size(); ++k)
                t.rows.push_back({std::to_string(k + 1), formatNumber(lo + step * static_cast<double>(k)),
                                  formatNumber(lo + step * static_cast<double>(k + 1)), std::to_string(h.counts[k])});
            t.rows.push_back({"Mesures", "", "", std::to_string(h.n)});
            t.rows.push_back({"Moyenne", "", "", formatNumber(std::round(h.mean * 1000) / 1000)});
            t.rows.push_back({"\xC3\x89" "cart type", "", "", formatNumber(std::round(h.sigma * 1000) / 1000)});
            if (h.cp >= 0) t.rows.push_back({"Cp", "", "", formatNumber(std::round(h.cp * 100) / 100)});
            if (h.cpk > -1) t.rows.push_back({"Cpk", "", "", formatNumber(std::round(h.cpk * 100) / 100)});
            return true;
        }
        case Kind::BarChart: case Kind::PieChart: case Kind::RadarChart: {
            const auto items = chartItems(obj);
            t.headers = {obj.kind == Kind::RadarChart ? std::string("Axe") : std::string("\xC3\x89l\xC3\xA9ment"), "Valeur"};
            std::vector<double> values;
            for (const auto& it : items) {
                const std::string v = evalCell(it.expression);
                double x = 0;
                values.push_back(numberOf(v, x) ? x : 0.0);
                t.rows.push_back({chartItemLabel(it), v});
            }
            if (obj.kind == Kind::PieChart) {
                t.headers.push_back("Part");
                const auto slices = pieSlices(values);
                for (std::size_t k = 0; k < t.rows.size(); ++k) t.rows[k].push_back(k < slices.size() ? percent(slices[k].fraction) : std::string{});
            }
            if (obj.kind == Kind::RadarChart) {
                const auto refs = chartItems(obj, "references");
                if (!refs.empty()) {
                    t.headers.push_back("Consigne");
                    for (std::size_t k = 0; k < t.rows.size(); ++k) t.rows[k].push_back(k < refs.size() ? evalCell(refs[k].expression) : std::string{});
                }
            }
            return true;
        }
        case Kind::VariableTable: {
            t.headers = {"Nom", "Valeur", "Unit\xC3\xA9", "Expression"};
            for (const auto& r : variableRows(obj)) t.rows.push_back({r.name.empty() ? r.expression : r.name, evalCell(r.expression), r.unit, r.expression});
            return true;
        }
        case Kind::Table: {
            // 1.12.2 : un tableau dynamique (rowsFrom) s'exporte entier - toutes ses lignes, pas seulement les montrees.
            if (trimmedText(obj.text("rowsFrom")).size() > 0 && tableRows(obj, t.headers, t.rows)) return true;
            t.headers = splitSemicolons(obj.text("columns"));
            for (const auto& row : parseCells(obj.text("cells"))) {
                std::vector<std::string> out;
                for (const auto& cell : row) {
                    if (cellIsExpression(cell)) out.push_back(evalCell(cell.substr(1)));
                    else if (cellIsTemplate(cell)) out.push_back(TextTemplate::compile(cell).render(environment()));
                    else out.push_back(cell);
                }
                t.rows.push_back(std::move(out));
            }
            return true;
        }
        case Kind::ProductionCounter: {
            const auto f = production(obj.id);
            t.headers = {"Indicateur", "Valeur"};
            t.rows = {{"Poste", f.shift},
                      {"Pi\xC3\xA8" "ces bonnes", formatNumber(f.good)},
                      {"Rebuts", formatNumber(f.bad)},
                      {"Total", formatNumber(f.total)},
                      {"Cadence (pi\xC3\xA8" "ces/h)", formatNumber(std::round(f.rate))},
                      {"Disponibilit\xC3\xA9", percent(f.availability)},
                      {"Performance", percent(f.performance)},
                      {"Qualit\xC3\xA9", percent(f.quality)},
                      {"TRS", percent(f.oee)},
                      {"Temps de marche", durationText(f.runSeconds)},
                      {"Temps du poste", durationText(f.plannedSeconds)}};
            if (f.target > 0) t.rows.push_back({"Objectif", formatNumber(f.target)});
            return true;
        }
        case Kind::AlarmStats: {
            t.headers = {"Rang", "Alarme", "Groupe", "Priorit\xC3\xA9", "Nombre", "Dur\xC3\xA9" "e active"};
            const auto rows = alarmStatistics(closed_, history_ ? &history_->alarms : nullptr, alarms_, obj.text("range", "depuis le lancement"),
                                              markers::strip(obj.text("group"), markers::Mode::Text),   // 1.11 (REP-1) : sans ses $
                                              obj.text("sort", "nombre"), static_cast<std::size_t>(std::max(0.0, obj.number("top", 5))),
                                              dateStampOf(now_));
            for (std::size_t k = 0; k < rows.size(); ++k)
                t.rows.push_back({std::to_string(k + 1), rows[k].name, rows[k].group, std::to_string(rows[k].priority), std::to_string(rows[k].count),
                                  durationText(rows[k].seconds)});
            return true;
        }
        case Kind::History: {
            const std::string src = plainWord(obj.text("source", "alarmes"));
            if (src == "evenements") return exportTable("\xC3\xA9v\xC3\xA9nements", t, why);
            if (src == "systeme" || src == "historique") return exportTable(src == "systeme" ? "syst\xC3\xA8me" : "historique", t, why);
            if (src == "audit") return exportTable("audit", t, why);                 // lot 13
            if (src.rfind("mise", 0) == 0) {
                t.title = "Alarmes mises de c\xC3\xB4t\xC3\xA9";
                t.headers = {"Alarme", "Groupe", "Par", "Raison", "Depuis", "Reste"};
                for (const auto& s : shelved_)
                    t.rows.push_back({s.name, s.group, s.by, s.reason, s.since.substr(0, std::min<std::size_t>(19, s.since.size())),
                                      s.until < 0 ? std::string("sans limite") : durationText(s.until - now_)});
                return true;
            }
            return exportTable("alarmes", t, why);
        }
        default:
            return fail("objet '" + name + "' (" + std::string(kindLabel(obj.kind)) + ") : rien \xC3\xA0 exporter");
    }
}

bool Runtime::exportData(std::string_view source, std::string_view fileName, std::string_view format, double now, const std::string& origin,
                         std::string* why, bool askWhere) {
    now_ = std::max(now_, now);
    const std::string from = origin.empty() ? std::string("export") : origin;
    std::string reason;
    ExportTable table;
    if (!exportTable(source, table, &reason)) {
        if (why) *why = reason;
        log("Erreur", from, "export : " + reason);
        return false;
    }
    const std::string rendered = TextTemplate::compile(fileName.empty() ? std::string_view("export_{SYS.Date}") : fileName).render(environment());
    const auto chosen = format.empty() ? exportFormatFrom(rendered) : exportFormatFrom(format);
    const ExportFormat f = chosen.value_or(ExportFormat::Csv);
    ExportRequest rq;
    rq.fileName = exportFileName(rendered, f);
    rq.format = std::string(exportFormatLabel(f));
    rq.source = std::string(source);
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const Bytes>(exportBytes(table, f));
    rq.origin = from;
    // ---- Lot API 8 : les exports qui demandent ou ----
    // Un geste de l'operateur (un clic, un double clic, un appui long, et ce
    // qu'il declenche : l'action, le script qu'il lance, la vue qu'il ouvre) et
    // l'option cochee : l'ecran demande ou (exports/ propose), ecrit a la reponse
    // et le dit (exportAnswered). Ce qui part tout seul - une temporisation, un
    // front, un changement de valeur, un script periodique - ne demande jamais :
    // personne n'a rien demande a cet instant, et un poste sans personne ne
    // repondrait pas ; le fichier va dans exports/, comme avant.
    if (askWhere && gesture_ > 0 && hooks_.askExport && hooks_.askExport(rq)) {
        log("Action", from, "export " + rq.fileName + " : o\xC3\xB9 l'\xC3\xA9" "crire ? (question pos\xC3\xA9" "e \xC3\xA0 l'op\xC3\xA9rateur)");
        return true;
    }
    // ---- fin Lot API 8 ----
    if (!hooks_.exportFile) {
        const std::string text = "l'\xC3\xA9" "cran ne sait pas \xC3\xA9" "crire de fichier ici";
        if (why) *why = text;
        log("Action", from, "export " + rq.fileName + " : " + text);
        return false;
    }
    std::string where;
    if (!hooks_.exportFile(rq, &where)) {
        if (why) *why = where;
        log("Erreur", from, "export " + rq.fileName + " : " + where);
        return false;
    }
    lastExport_ = rq.fileName;
    ++exports_;
    event("Export", from, std::string(source) + " \xE2\x86\x92 " + rq.fileName + " (" + std::to_string(rq.rows) + " ligne(s), " + rq.format + ")");
    return true;
}

// ---- Lot API 8 : les exports qui demandent ou ----
void Runtime::exportAnswered(const ExportRequest& rq, bool written, const std::string& where, double now) {
    now_ = std::max(now_, now);
    const std::string from = rq.origin.empty() ? std::string("export") : rq.origin;
    if (!written) {
        if (where.empty()) log("Action", from, "export " + rq.fileName + " : annul\xC3\xA9 (rien n'est \xC3\xA9" "crit)");
        else log("Erreur", from, "export " + rq.fileName + " : " + where);
        return;
    }
    // Le fichier ecrit (un autre nom a pu etre choisi) : SYS.LastExport en garde le nom.
    const auto slash = where.find_last_of("/\\");
    lastExport_ = where.empty() ? rq.fileName : where.substr(slash == std::string::npos ? 0 : slash + 1);
    ++exports_;
    // Dans exports/ sous le nom habituel : dit comme avant ; ailleurs : le chemin.
    const std::string shown = where.empty() || where == "exports/" + rq.fileName ? rq.fileName : where;
    event("Export", from, rq.source + " \xE2\x86\x92 " + shown + " (" + std::to_string(rq.rows) + " ligne(s), " + rq.format + ")");
}

bool Runtime::exportAsOperator(std::string_view source, std::string_view fileName, std::string_view format, double now, bool askWhere,
                               std::string* why) {
    GestureGuard gesture(*this, "Op\xC3\xA9rateur");
    return exportData(source, fileName, format, now, "Op\xC3\xA9rateur", why, askWhere);
}
// ---- fin Lot API 8 ----

} // namespace hmi
