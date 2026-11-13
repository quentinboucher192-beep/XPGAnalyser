// =============================================================================
//  sim/SfcRuntime.cpp - 1.51.0 (S) : les sections SFC dans le simulateur
// -----------------------------------------------------------------------------
//  Les fonctions membres de Runtime qui preparent et font tourner un GRAFCET
//  natif (sim/SfcRun.hpp dit la semantique). Ici plutot que dans Runtime.cpp :
//  le meme objet, un fichier a lui.
// =============================================================================
#include "Runtime.hpp"
#include "SfcRun.hpp"

#include "../project/LadderSim.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sim {

namespace {

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trimmedText(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

} // namespace

bool sfcDuration(std::string_view text, std::int64_t& ms) {
    std::string s = lowered(trimmedText(text));
    const auto hash = s.find('#');
    if (hash == std::string::npos) return false;
    const auto head = s.substr(0, hash);
    if (head != "t" && head != "time") return false;
    std::string body;
    for (const char c : s.substr(hash + 1)) if (c != '_') body.push_back(c);
    if (body.empty()) return false;
    bool negative = false;
    if (body.front() == '-') { negative = true; body.erase(body.begin()); }
    double total = 0.0;
    std::size_t i = 0;
    bool any = false;
    while (i < body.size()) {
        std::size_t j = i;
        while (j < body.size() && (std::isdigit(static_cast<unsigned char>(body[j])) || body[j] == '.')) ++j;
        if (j == i) return false;
        const double v = std::atof(body.substr(i, j - i).c_str());
        std::size_t k = j;
        while (k < body.size() && std::isalpha(static_cast<unsigned char>(body[k]))) ++k;
        const auto unit = body.substr(j, k - j);
        double factor = 0.0;
        if (unit == "d") factor = 86400000.0;
        else if (unit == "h") factor = 3600000.0;
        else if (unit == "m") factor = 60000.0;
        else if (unit == "s") factor = 1000.0;
        else if (unit == "ms") factor = 1.0;
        else if (unit.empty() && k == body.size() && !any) factor = 1.0;   // t#500 : des ms
        else return false;
        total += v * factor;
        any = true;
        i = k;
    }
    ms = static_cast<std::int64_t>(std::llround(negative ? -total : total));
    return any;
}

// ------------------------------------------------------------- SfcChartRun
const SfcChartRun::Step* SfcChartRun::stepNamed(std::string_view name) const {
    const auto want = lowered(name);
    for (const auto& s : steps) if (lowered(s.name) == want) return &s;
    return nullptr;
}

const SfcChartRun::Step* SfcChartRun::stepOfItem(int item) const {
    for (const auto& s : steps) if (s.item == item) return &s;
    return nullptr;
}

const SfcChartRun::Trans* SfcChartRun::transOfItem(int item) const {
    for (const auto& t : trans) if (t.item == item) return &t;
    return nullptr;
}

const SfcChartRun::ActionCtl* SfcChartRun::action(std::string_view name) const {
    const auto want = lowered(name);
    for (const auto& a : actions) if (lowered(a.name) == want) return &a;
    return nullptr;
}

std::vector<std::string> SfcChartRun::activeSteps() const {
    std::vector<std::string> out;
    for (const auto& s : steps) if (s.active) out.push_back(s.name);
    return out;
}

// ------------------------------------------------------------- preparer
bool Runtime::prepareSfc(const domain::Project& p, domain::Index si, std::uint32_t entry, const std::string& scope) {
    const auto& section = p.sections[si];
    const auto name = std::string(p.strings.text(section.name));
    auto run = std::make_shared<SfcChartRun>();
    run->section = name;
    run->scope = scope;
    run->chart = sfc::parse(section.body);
    if (!run->chart.ok) {
        prepareDiagnostics_.push_back(Diagnostic{
            Diagnostic::Severity::Error, "SFC: " + run->chart.error, 0, scope + name});
        return false;
    }
    run->topo = sfc::resolve(run->chart);
    for (const auto& [item, message] : run->topo.problems)
        prepareDiagnostics_.push_back(Diagnostic{Diagnostic::Severity::Warning, "SFC: " + message, 0, scope + name});
    const auto& items = run->chart.items;

    // ---- les etapes et leurs variables
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.kind != sfc::ItemKind::Step || it.name.empty()) continue;
        SfcChartRun::Step st;
        st.item = static_cast<int>(i);
        st.name = it.name;
        st.initial = it.stepKind == sfc::StepKind::Initial;
        std::int64_t ms = 0;
        if (sfcDuration(it.tmin, ms) && ms > 0) st.tmin = ms;
        if (sfcDuration(it.tmax, ms) && ms > 0) st.tmax = ms;
        if (sfcDuration(it.delay, ms) && ms > 0) st.delay = ms;
        run->steps.push_back(st);
        const std::string base = scope + it.name;
        if (!find(base + ".x")) {
            declare(base + ".x", Type::Bool);
            declare(base + ".t", Type::Time);
            declare(base + ".tminErr", Type::Bool);
            declare(base + ".tmaxErr", Type::Bool);
        }
    }
    auto stepIndex = [&](int item) {
        for (std::size_t k = 0; k < run->steps.size(); ++k)
            if (run->steps[k].item == item) return static_cast<int>(k);
        return -1;
    };

    // ---- les transitions et leurs receptivites
    for (const auto& tl : run->topo.transitions) {
        const auto& it = items[static_cast<std::size_t>(tl.item)];
        SfcChartRun::Trans tr;
        tr.item = tl.item;
        tr.x = it.x;
        tr.y = it.y;
        for (const int s : tl.sources) if (const int k = stepIndex(s); k >= 0) tr.src.push_back(k);
        for (const int s : tl.targets) if (const int k = stepIndex(s); k >= 0) tr.dst.push_back(k);
        tr.text = sfc::conditionText(run->chart, it);
        auto expression = [&](const std::string& text) {
            if (auto e = parseExpression(text)) {
                tr.kind = SfcChartRun::CondKind::Expression;
                tr.expr = *e;
            }
            else {
                tr.kind = SfcChartRun::CondKind::Unsupported;
                tr.why = "r\xC3\xA9" "ceptivit\xC3\xA9 illisible : " + text;
            }
        };
        if (it.cond == sfc::CondKind::Variable || it.cond == sfc::CondKind::Expression) {
            expression(it.invert ? "NOT (" + it.condText + ")" : it.condText);
        }
        else if (it.cond == sfc::CondKind::Section) {
            const auto* src = run->chart.source("transitionSource", it.condText);
            if (!src) {
                tr.kind = SfcChartRun::CondKind::Unsupported;
                tr.why = "section de transition introuvable : " + it.condText;
            }
            else if (src->language == domain::PouLanguage::ST) {
                std::string text = trimmedText(src->body);
                // "T1 := expression;" : l'expression ; sinon le texte entier est l'expression
                const auto assign = text.find(":=");
                const std::string target = assign == std::string::npos ? std::string{} : trimmedText(text.substr(0, assign));
                if (!target.empty() && lowered(target) == lowered(it.condText)) {
                    text = trimmedText(text.substr(assign + 2));
                }
                while (!text.empty() && (text.back() == ';' || std::isspace(static_cast<unsigned char>(text.back()))))
                    text.pop_back();
                if (auto e = parseExpression(text)) {
                    tr.kind = SfcChartRun::CondKind::Expression;
                    tr.expr = *e;
                    if (it.invert) {
                        if (auto inv = parseExpression("NOT (" + text + ")")) tr.expr = *inv;
                    }
                }
                else if (auto prog = parse(src->body, scope + name + "." + it.condText)) {
                    // des instructions qui ecrivent une variable du nom de la transition
                    tr.kind = SfcChartRun::CondKind::Statements;
                    tr.program = *prog;
                    tr.target = it.condText;
                    if (!find(scope + it.condText) && !find(it.condText)) declare(scope + it.condText, Type::Bool, true);
                    setProgramTag(*tr.program, static_cast<std::uint32_t>(tagNames_.size()));
                    tagNames_.push_back(scope + name + "." + it.condText);
                    tagPrograms_.push_back(tr.program.get());
                }
                else {
                    tr.kind = SfcChartRun::CondKind::Unsupported;
                    tr.why = "section de transition " + it.condText + " : " + prog.error().message();
                }
            }
            else if (src->language == domain::PouLanguage::LD) {
                auto compiled = project::ladder::compileXml(src->body, scope + name + "." + it.condText);
                if (compiled) {
                    tr.kind = SfcChartRun::CondKind::Ladder;
                    tr.ladder = *compiled;
                    tr.ladderState = std::make_shared<project::ladder::State>();
                }
                else {
                    tr.kind = SfcChartRun::CondKind::Unsupported;
                    tr.why = "section de transition LD " + it.condText + " : " + compiled.error().message();
                }
            }
            else {
                tr.kind = SfcChartRun::CondKind::Unsupported;
                tr.why = "section de transition en " + std::string(domain::toString(src->language))
                       + " : non simul\xC3\xA9" "e (fausse)";
            }
        }
        else {
            tr.kind = SfcChartRun::CondKind::None;
            tr.why = "pas de r\xC3\xA9" "ceptivit\xC3\xA9 (fausse)";
        }
        if (!tr.why.empty())
            prepareDiagnostics_.push_back(Diagnostic{Diagnostic::Severity::Warning, "SFC: " + tr.why, 0, scope + name});
        run->trans.push_back(std::move(tr));
    }

    // ---- les actions, par nom
    for (std::size_t k = 0; k < run->steps.size(); ++k) {
        const auto& it = items[static_cast<std::size_t>(run->steps[k].item)];
        for (const auto& a : it.actions) {
            if (a.name.empty()) continue;
            SfcChartRun::ActionCtl* ctl = nullptr;
            for (auto& c : run->actions)
                if (lowered(c.name) == lowered(a.name) && c.isSection == a.isSection) ctl = &c;
            if (!ctl) {
                SfcChartRun::ActionCtl fresh;
                fresh.name = a.name;
                fresh.isSection = a.isSection;
                if (a.isSection) {
                    const auto* src = run->chart.source("actionSource", a.name);
                    if (!src) fresh.why = "section d'action introuvable : " + a.name;
                    else if (src->language == domain::PouLanguage::ST) {
                        if (auto prog = parse(src->body, scope + name + "." + a.name)) {
                            fresh.program = *prog;
                            setProgramTag(*fresh.program, static_cast<std::uint32_t>(tagNames_.size()));
                            tagNames_.push_back(scope + name + "." + a.name);
                            tagPrograms_.push_back(fresh.program.get());
                        }
                        else fresh.why = "section d'action " + a.name + " : " + prog.error().message();
                    }
                    else if (src->language == domain::PouLanguage::LD) {
                        if (auto compiled = project::ladder::compileXml(src->body, scope + name + "." + a.name)) {
                            fresh.ladder = *compiled;
                            fresh.ladderState = std::make_shared<project::ladder::State>();
                        }
                        else fresh.why = "section d'action LD " + a.name + " : " + compiled.error().message();
                    }
                    else fresh.why = "section d'action en " + std::string(domain::toString(src->language)) + " : non simul\xC3\xA9" "e";
                    if (!fresh.why.empty())
                        prepareDiagnostics_.push_back(Diagnostic{Diagnostic::Severity::Warning, "SFC: " + fresh.why, 0, scope + name});
                }
                run->actions.push_back(std::move(fresh));
                ctl = &run->actions.back();
            }
            SfcChartRun::Assoc as;
            as.step = static_cast<int>(k);
            as.qualifier = sfc::normalQualifier(a.qualifier);
            if (!a.time.empty()) {
                std::int64_t ms = 0;
                if (sfcDuration(a.time, ms)) as.timeMs = ms;
                else if (auto e = parseExpression(a.time)) as.timeExpr = *e;
            }
            ctl->assocs.push_back(std::move(as));
        }
    }

    Runnable runnable{name, scope, nullptr, entry, {}, {}, true};
    run->tag = static_cast<std::uint32_t>(tagNames_.size());
    tagNames_.push_back(scope + name);
    tagPrograms_.push_back(nullptr);
    runnable.sfc = run;
    if (section.activationCondition != 0) {
        auto text = std::string(p.strings.text(section.activationCondition));
        text.erase(0, text.find_first_not_of(" \t\r\n"));
        text.erase(text.find_last_not_of(" \t\r\n;") + 1);
        if (!text.empty())
            if (auto condition = parseExpression(text)) {
                runnable.condition = *condition;
                runnable.conditionText = text;
            }
    }
    programs_.push_back(std::move(runnable));
    return true;
}

// ------------------------------------------------------------- un cycle
bool Runtime::runSfc(Runnable& runnable) {
    auto& run = *runnable.sfc;
    const std::int64_t now = clockMs_;
    bool ok = true;
    trace_.tag = run.tag;
    trace_.line = 0;

    auto setFlag = [&](const std::string& n, const Value& v) {
        if (auto* slot = find(n)) {
            if (slot->value.type() != v.type() || slot->value.asInteger() != v.asInteger()) {
                slot->value = v;
                noteWrite(*slot);
            }
        }
    };
    auto evalBool = [&](const Expression& e) -> std::optional<bool> {
        const auto v = evaluate(e, *this);
        if (!v) return std::nullopt;
        return v->isTruthy();
    };

    // ---- 1. les receptivites, puis les franchissements (une evolution)
    std::vector<int> fire;
    if (!run.started) {
        // la situation initiale : l'evolution de ce cycle
        for (auto& s : run.steps) {
            s.active = s.initial;
            s.activatedAt = now;
            s.elapsed = 0;
            s.tminErr = s.tmaxErr = false;
        }
        run.started = true;
        for (auto& t : run.trans) { t.enabled = false; t.fired = false; t.value.reset(); }
    }
    else {
        for (auto& t : run.trans) {
            t.fired = false;
            bool enabled = !t.src.empty();
            for (const int k : t.src) {
                const auto& s = run.steps[static_cast<std::size_t>(k)];
                if (!s.active || (s.delay > 0 && now - s.activatedAt < s.delay)) enabled = false;
            }
            t.enabled = enabled;
            switch (t.kind) {
            case SfcChartRun::CondKind::Expression:
                t.value = evalBool(*t.expr);
                break;
            case SfcChartRun::CondKind::Statements:
                if (enabled) {
                    const auto r = execute(*t.program, *this, scanLimits_);
                    if (!r.completed) ok = false;
                    Value v;
                    t.value = read(t.target, v) ? std::optional<bool>(v.isTruthy()) : std::nullopt;
                }
                break;
            case SfcChartRun::CondKind::Ladder:
                if (enabled) {
                    project::ladder::RunOptions o;
                    o.captureCoil = run.chart.items[static_cast<std::size_t>(t.item)].condText;
                    o.scanStatements = &scanStatements_;
                    o.maxStatements = maxScanStatementsSetting_;
                    const auto r = project::ladder::run(*t.ladder, *this, *t.ladderState, o);
                    for (const auto& d : r.diagnostics) report(d);
                    if (!r.ok) ok = false;
                    bool v = r.captured && r.value;
                    if (run.chart.items[static_cast<std::size_t>(t.item)].invert) v = !v;
                    t.value = v;
                }
                break;
            default:
                t.value = false;
                break;
            }
        }
        // les franchissables, de gauche a droite : une etape d'amont deja prise ne l'est pas deux fois
        std::vector<int> order;
        for (std::size_t i = 0; i < run.trans.size(); ++i) order.push_back(static_cast<int>(i));
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const auto& ta = run.trans[static_cast<std::size_t>(a)];
            const auto& tb = run.trans[static_cast<std::size_t>(b)];
            return ta.x != tb.x ? ta.x < tb.x : ta.y < tb.y;
        });
        std::vector<char> taken(run.steps.size(), 0);
        for (const int i : order) {
            auto& t = run.trans[static_cast<std::size_t>(i)];
            if (!t.enabled || !t.value || !*t.value) continue;
            bool free = true;
            for (const int k : t.src) if (taken[static_cast<std::size_t>(k)]) free = false;
            if (!free) continue;
            for (const int k : t.src) taken[static_cast<std::size_t>(k)] = 1;
            fire.push_back(i);
        }
        std::vector<char> off(run.steps.size(), 0), on(run.steps.size(), 0);
        for (const int i : fire) {
            auto& t = run.trans[static_cast<std::size_t>(i)];
            t.fired = true;
            for (const int k : t.src) off[static_cast<std::size_t>(k)] = 1;
            for (const int k : t.dst) on[static_cast<std::size_t>(k)] = 1;
        }
        if (!fire.empty()) ++run.evolutions;
        for (std::size_t k = 0; k < run.steps.size(); ++k) {
            auto& s = run.steps[k];
            if (off[k] && !on[k]) {
                if (s.tmin > 0 && now - s.activatedAt < s.tmin) s.tminErr = true;
                s.elapsed = now - s.activatedAt;
                s.active = false;
            }
            if (on[k]) {
                s.active = true;
                s.activatedAt = now;
                s.elapsed = 0;
                s.tminErr = s.tmaxErr = false;
            }
        }
    }

    // ---- 2. les variables d'etape
    for (auto& s : run.steps) {
        if (s.active) {
            s.elapsed = now - s.activatedAt;
            if (s.tmax > 0 && s.elapsed > s.tmax) s.tmaxErr = true;
        }
        const std::string base = run.scope + s.name;
        setFlag(base + ".x", Value::boolean(s.active));
        setFlag(base + ".t", Value::time(s.elapsed));
        setFlag(base + ".tminErr", Value::boolean(s.tminErr));
        setFlag(base + ".tmaxErr", Value::boolean(s.tmaxErr));
    }

    // ---- 3. les actions (ACTION_CONTROL)
    for (auto& a : run.actions) {
        bool N = false, R = false, S = false, L = false, D = false, P = false, SD = false, DS = false, SL = false;
        std::int64_t tL = 0, tD = 0, tSD = 0, tDS = 0, tSL = 0;
        for (const auto& as : a.assocs) {
            const auto& s = run.steps[static_cast<std::size_t>(as.step)];
            const bool x = s.active;
            const bool rising = x && !s.wasActive;
            const bool falling = !x && s.wasActive;
            std::int64_t t = as.timeMs;
            if (as.timeExpr) {
                const auto v = evaluate(*as.timeExpr, *this);
                if (v) t = v->asInteger();
            }
            const auto& q = as.qualifier;
            if (q == "N") N = N || x;
            else if (q == "R") R = R || x;
            else if (q == "S") S = S || x;
            else if (q == "L") { if (x) { L = true; tL = t; } }
            else if (q == "D") { if (x) { D = true; tD = t; } }
            else if (q == "P" || q == "P1") P = P || rising;
            else if (q == "P0") P = P || falling;
            else if (q == "SD") { if (x) { SD = true; tSD = t; } else if (tSD == 0) tSD = t; }
            else if (q == "DS") { if (x) { DS = true; tDS = t; } }
            else if (q == "SL") { if (x) { SL = true; tSL = t; } else if (tSL == 0) tSL = t; }
            else N = N || x;   // un qualificatif inconnu : comme N
        }
        if (R) a.sFF = a.sdFF = a.dsFF = a.slFF = false;
        if (S && !R) a.sFF = true;
        if (L) { if (a.lSince < 0) a.lSince = now; } else a.lSince = -1;
        const bool lOn = L && now - a.lSince < tL;
        if (D) { if (a.dSince < 0) a.dSince = now; } else a.dSince = -1;
        const bool dOn = D && now - a.dSince >= tD;
        if (SD && !R) a.sdFF = true;
        if (a.sdFF) { if (a.sdSince < 0) a.sdSince = now; } else a.sdSince = -1;
        const bool sdOn = a.sdFF && now - a.sdSince >= tSD;
        if (DS) {
            if (a.dsSince < 0) a.dsSince = now;
            if (now - a.dsSince >= tDS && !R) a.dsFF = true;
        }
        else a.dsSince = -1;
        if (SL && !R) a.slFF = true;
        if (a.slFF) { if (a.slSince < 0) a.slSince = now; } else a.slSince = -1;
        const bool slOn = a.slFF && now - a.slSince < tSL;
        a.qPrev = a.q;
        a.q = (N || lOn || dOn || P || a.sFF || sdOn || a.dsFF || slOn) && !R;

        if (!a.isSection) {
            (void)write(a.name, Value::boolean(a.q));
            continue;
        }
        if (!(a.q || a.qPrev)) continue;        // active, ou sa derniere execution
        if (a.program) {
            const auto r = execute(*a.program, *this, scanLimits_);
            scanTopStatements_ += r.statements;
            if (!r.completed) { ok = false; break; }
        }
        else if (a.ladder) {
            project::ladder::RunOptions o;
            o.scanStatements = &scanStatements_;
            o.maxStatements = maxScanStatementsSetting_;
            const auto r = project::ladder::run(*a.ladder, *this, *a.ladderState, o);
            for (const auto& d : r.diagnostics) report(d);
            if (!r.ok || r.halted) { ok = false; break; }
        }
    }
    for (auto& s : run.steps) s.wasActive = s.active;
    trace_ = {};
    return ok;
}

void Runtime::resetSfc() {
    for (auto& r : programs_) {
        if (!r.sfc) continue;
        auto& run = *r.sfc;
        run.started = false;
        run.evolutions = 0;
        for (auto& s : run.steps) {
            s.active = s.wasActive = false;
            s.activatedAt = s.elapsed = 0;
            s.tminErr = s.tmaxErr = false;
        }
        for (auto& t : run.trans) {
            t.value.reset();
            t.enabled = t.fired = false;
            if (t.ladderState) t.ladderState->reset();
        }
        for (auto& a : run.actions) {
            a.q = a.qPrev = a.sFF = a.sdFF = a.dsFF = a.slFF = false;
            a.lSince = a.dSince = a.sdSince = a.dsSince = a.slSince = -1;
            if (a.ladderState) a.ladderState->reset();
        }
    }
}

// ------------------------------------------------------------- lire, forcer
SfcChartRun* Runtime::sfcChartMutable(std::string_view section) {
    const auto want = lowered(section);
    for (auto& r : programs_)
        if (r.sfc && lowered(r.scope + r.section) == want) return r.sfc.get();
    for (auto& r : programs_)
        if (r.sfc && lowered(r.section) == want) return r.sfc.get();
    return nullptr;
}

const SfcChartRun* Runtime::sfcChart(std::string_view section) const {
    return const_cast<Runtime*>(this)->sfcChartMutable(section);
}

std::vector<std::string> Runtime::sfcSections() const {
    std::vector<std::string> out;
    for (const auto& r : programs_)
        if (r.sfc) out.push_back(r.scope + r.section);
    return out;
}

bool Runtime::sfcSetStep(std::string_view section, std::string_view step, bool active) {
    auto* run = sfcChartMutable(section);
    if (!run) return false;
    const auto want = lowered(step);
    for (auto& s : run->steps) {
        if (lowered(s.name) != want) continue;
        if (!run->started) {
            // avant le premier cycle : la situation initiale d'abord, puis le forcage
            for (auto& o : run->steps) { o.active = o.initial; o.activatedAt = clockMs_; o.elapsed = 0; }
            run->started = true;
        }
        if (s.active != active) {
            s.active = active;
            if (active) { s.activatedAt = clockMs_; s.elapsed = 0; s.tminErr = s.tmaxErr = false; }
            else s.elapsed = clockMs_ - s.activatedAt;
        }
        const std::string base = run->scope + s.name;
        if (auto* slot = find(base + ".x")) slot->value = Value::boolean(s.active);
        if (auto* slot = find(base + ".t")) slot->value = Value::time(s.elapsed);
        return true;
    }
    return false;
}

bool Runtime::sfcInit(std::string_view section) {
    auto* run = sfcChartMutable(section);
    if (!run) return false;
    run->started = false;
    for (auto& s : run->steps) {
        s.active = false;
        s.tminErr = s.tmaxErr = false;
    }
    for (auto& a : run->actions) {
        a.sFF = a.sdFF = a.dsFF = a.slFF = false;
        a.lSince = a.dSince = a.sdSince = a.dsSince = a.slSince = -1;
    }
    return true;
}

} // namespace sim
