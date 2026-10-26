// =============================================================================
//  app/hmi/HmiDebugSession.cpp - 1.13.0 (lot J2) : les points d'arret des
//  scripts IHM, pour la seance ; la marge des editeurs de code
// =============================================================================
#include "HmiDebugSession.hpp"

#include "HmiAskDialog.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../sim/Interpreter.hpp"
#include "../../ui/Commands.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace app {

namespace {

bool blankText(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const auto nl = text.find('\n', at);
        out.push_back(text.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return out;
}

// Les noms d'une condition (P, Tours, Fours[f].Temperature : le debut de chaque chemin), sans les mots du
// langage, les nombres, les litteraux types (T#5s), les chaines ni les fonctions appelees.
std::vector<std::string> conditionNames(std::string_view c) {
    static const char* const kWords[] = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "NULL", "IN", "ENTRE", "ET"};
    std::vector<std::string> out;
    for (std::size_t i = 0; i < c.size();) {
        const char ch = c[i];
        if (ch == '\'' || ch == '"') {
            const auto end = c.find(ch, i + 1);
            i = end == std::string_view::npos ? c.size() : end + 1;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            while (i < c.size() && (std::isalnum(static_cast<unsigned char>(c[i])) || c[i] == '.' || c[i] == '#' || c[i] == '_')) ++i;
            continue;
        }
        if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) { ++i; continue; }
        const bool member = i > 0 && (c[i - 1] == '.' || c[i - 1] == '#');
        const std::size_t from = i;
        while (i < c.size() && (std::isalnum(static_cast<unsigned char>(c[i])) || c[i] == '_')) ++i;
        std::string word(c.substr(from, i - from));
        if (i < c.size() && c[i] == '#') {                // un litteral type : T#5s, T_MODE#Auto
            ++i;
            while (i < c.size() && (std::isalnum(static_cast<unsigned char>(c[i])) || c[i] == '_' || c[i] == '.' || c[i] == ':')) ++i;
            continue;
        }
        std::size_t j = i;
        while (j < c.size() && c[j] == ' ') ++j;
        if (member || (j < c.size() && c[j] == '(')) continue;
        std::string up = word;
        for (auto& x : up) x = static_cast<char>(std::toupper(static_cast<unsigned char>(x)));
        if (std::any_of(std::begin(kWords), std::end(kWords), [&](const char* w) { return up == w; })) continue;
        if (std::find(out.begin(), out.end(), word) == out.end()) out.push_back(std::move(word));
    }
    return out;
}

// Le projet de la seance (pour refuser un script C, nommer un code).
const hmi::Project* projectOf(const HmiDebugSession& s) {
    if (s.project()) return s.project();
    return s.runtime() ? s.runtime()->project() : nullptr;
}

} // namespace

// ---------------------------------------------------------------- la seance ---
HmiDebugSession& HmiDebugSession::instance() {
    static HmiDebugSession s;
    return s;
}

void HmiDebugSession::touch(bool user) {
    if (user) ++revision_;
    ++state_;
    changed->emit();
}

void HmiDebugSession::bindProject(const hmi::DocumentPtr& doc) {
    if (!doc) return;
    // Le meme document, toujours vivant : rien ne change. Un autre (ou le meme emplacement, apres la fermeture du
    // projet : le lien faible est mort) : les points partent.
    if (const auto current = owner_.lock(); bound_ && current && current.get() == doc.get()) return;
    const bool had = !list_.empty();
    owner_ = doc;
    bound_ = true;
    list_.clear();
    followed_.clear();
    level_ = 0;
    if (had) touch(true);
}

const hmi::Project* HmiDebugSession::project() const noexcept {
    const auto doc = owner_.lock();
    return doc ? &doc->project : nullptr;
}

std::uint32_t HmiDebugSession::add(HmiDebugTarget code, std::uint32_t line, std::string condition, std::string* why) {
    if (code.empty()) {
        if (why) *why = "Choisis d'abord un script ou une fonction (un \xC3\xA9v\xC3\xA9nement vide n'a pas encore de code).";
        return 0;
    }
    if (const hmi::Project* p = projectOf(*this); p && code.script != hmi::kNoId)
        if (const hmi::Script* sc = p->script(code.script); sc && sc->lang != hmi::ScriptLang::ST) {
            if (why) *why = std::string(hmi::scriptLangKey(sc->lang)) + " : non ex\xC3\xA9" "cut\xC3\xA9 en simulation (C/C++) - pas de point d'arr\xC3\xAAt ici.";
            return 0;
        }
    if (const auto id = at(code, line)) return id;
    hmi::DebugBreakpoint b;
    b.id = nextId_++;
    b.script = code.script;
    b.function = code.function;
    b.line = std::max<std::uint32_t>(1, line);
    b.condition = trimmed(condition);
    list_.push_back(std::move(b));
    touch(true);
    return list_.back().id;
}

bool HmiDebugSession::toggle(HmiDebugTarget code, std::uint32_t line, std::string* why) {
    if (const auto id = at(code, line)) {
        (void)remove(id);
        return false;
    }
    return add(code, line, {}, why) != 0;
}

bool HmiDebugSession::remove(std::uint32_t id) {
    const auto it = std::find_if(list_.begin(), list_.end(), [&](const hmi::DebugBreakpoint& b) { return b.id == id; });
    if (it == list_.end()) return false;
    list_.erase(it);
    touch(true);
    return true;
}

bool HmiDebugSession::setEnabled(std::uint32_t id, bool on) {
    for (auto& b : list_)
        if (b.id == id) {
            if (b.enabled == on) return true;
            b.enabled = on;
            touch(true);
            return true;
        }
    return false;
}

bool HmiDebugSession::setCondition(std::uint32_t id, std::string condition) {
    std::string c = trimmed(condition);
    if (!c.empty() && c.front() == '=' && (c.size() < 2 || c[1] != '=')) c = trimmed(std::string_view(c).substr(1));   // =x > 3
    for (auto& b : list_)
        if (b.id == id) {
            b.condition = std::move(c);
            touch(true);
            return true;
        }
    return false;
}

void HmiDebugSession::clear() {
    if (list_.empty()) return;
    list_.clear();
    touch(true);
}

void HmiDebugSession::disableAll() {
    bool any = false;
    for (auto& b : list_) {
        any = any || b.enabled;
        b.enabled = false;
    }
    if (any) touch(true);
}

std::uint32_t HmiDebugSession::at(HmiDebugTarget code, std::uint32_t line) const {
    for (const auto& b : list_)
        if (b.script == code.script && b.function == code.function && (b.line == line || (b.effectiveLine && b.effectiveLine == line))) return b.id;
    return 0;
}

std::size_t HmiDebugSession::rankOf(std::uint32_t id) const {
    for (std::size_t i = 0; i < list_.size(); ++i)
        if (list_[i].id == id) return i + 1;
    return 0;
}

const hmi::DebugBreakpoint* HmiDebugSession::find(std::uint32_t id) const {
    for (const auto& b : list_)
        if (b.id == id) return &b;
    return nullptr;
}

void HmiDebugSession::shiftLines(HmiDebugTarget code, std::uint32_t prefix, std::uint32_t from, int delta) {
    if (delta == 0 || code.empty()) return;
    // La derniere ligne de la partie changee, comptee dans le nouveau texte (prefix : rien n'en reste).
    const long long lastChanged = static_cast<long long>(from) - 1 + delta;
    bool any = false;
    for (auto& b : list_) {
        if (b.script != code.script || b.function != code.function || b.line <= prefix) continue;
        long long moved = b.line >= from ? static_cast<long long>(b.line) + delta                       // le bas : sa ligne
                                         : std::min<long long>(b.line, std::max<long long>(prefix + 1, lastChanged));
        moved = std::max<long long>(1, moved);
        if (moved == static_cast<long long>(b.line)) continue;
        b.line = static_cast<std::uint32_t>(moved);
        b.effectiveLine = 0;                      // le moteur la relira
        any = true;
    }
    if (!any) return;
    // Deux points sur une meme ligne de ce code (une ligne supprimee entre eux) : un seul reste (le premier pose), qui
    // s'arrete la ou l'un des deux s'arretait - rien ne se perd sans un mot. Actif si l'un l'etait ; sa condition : celle
    // des points actifs, jointes par OR (un point actif sans condition : sans condition) ; leurs passages s'ajoutent.
    std::vector<hmi::DebugBreakpoint> kept;
    kept.reserve(list_.size());
    const auto joined = [](const std::string& a, const std::string& b) {
        if (a.empty() || b.empty()) return std::string{};
        if (a == b) return a;
        return "(" + a + ") OR (" + b + ")";
    };
    for (auto& b : list_) {
        const auto same = std::find_if(kept.begin(), kept.end(), [&](const hmi::DebugBreakpoint& k) {
            return b.script == code.script && b.function == code.function && k.script == b.script && k.function == b.function
                   && k.line == b.line;
        });
        if (same == kept.end()) {
            kept.push_back(std::move(b));
            continue;
        }
        hmi::DebugBreakpoint& k = *same;
        if (k.enabled && b.enabled) k.condition = joined(k.condition, b.condition);
        else if (b.enabled) k.condition = b.condition;                 // le seul actif : sa condition
        else if (!k.enabled) k.condition = joined(k.condition, b.condition);
        k.enabled = k.enabled || b.enabled;
        k.hits += b.hits;
        k.note.clear();
    }
    list_ = std::move(kept);
    touch(true);
}

void HmiDebugSession::followText(HmiDebugTarget code, const std::string& beforeText, const std::string& afterText) {
    if (code.empty() || beforeText == afterText) return;
    // Deja suivi (un autre editeur du meme code, le volet Debogage qui le montre) : pas deux fois.
    auto& seen = followed_[std::make_pair(code.script, code.function)];
    if (seen == afterText) return;
    seen = afterText;
    const auto before = splitLines(beforeText);
    const auto after = splitLines(afterText);
    const int delta = static_cast<int>(after.size()) - static_cast<int>(before.size());
    if (delta == 0) return;
    // Les lignes du haut (le prefixe commun) restent ; celles du bas (le suffixe commun) glissent de `delta`.
    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    shiftLines(code, static_cast<std::uint32_t>(prefix), static_cast<std::uint32_t>(before.size() - suffix + 1), delta);
}

void HmiDebugSession::setRuntime(hmi::Runtime* rt) {
    if (runtime_ == rt) return;
    runtime_ = rt;
    level_ = 0;
    touch(false);
}

void HmiDebugSession::pullFrom(const hmi::Runtime& rt) {
    bool any = false;
    for (auto& b : list_)
        for (const auto& r : rt.breakpoints())
            if (r.id == b.id && (r.hits != b.hits || r.effectiveLine != b.effectiveLine || r.note != b.note)) {
                b.hits = r.hits;
                b.effectiveLine = r.effectiveLine;
                b.note = r.note;
                any = true;
            }
    (void)any;
    starts_ = rt.startCount();
    // Hors pause, ou une nouvelle pause (apres un pas, Continuer) : la ligne d'arret, en haut de la pile.
    if (!rt.paused() || rt.pauseCount() != pauses_) level_ = 0;
    pauses_ = rt.pauseCount();
    touch(false);
}

void HmiDebugSession::followStart(const hmi::Runtime& rt) {
    if (rt.startCount() == starts_) return;
    starts_ = rt.startCount();
    for (auto& b : list_) b.hits = 0;
}

void HmiDebugSession::setLevel(std::size_t level) {
    if (level_ == level) return;
    level_ = level;
    touch(false);
}

bool HmiDebugSession::pausedAt(HmiDebugTarget& code, std::uint32_t& line, bool* top) const {
    if (!runtime_ || !runtime_->paused() || !runtime_->debugHit()) return false;
    const auto& stack = runtime_->debugHit()->stack;
    if (stack.empty()) return false;
    const std::size_t k = std::min(level_, stack.size() - 1);
    const auto& f = stack[stack.size() - 1 - k];
    code = HmiDebugTarget{f.script, f.function};
    line = f.line;
    if (top) *top = k == 0;
    return !code.empty();
}

// ------------------------------------------------------- la condition, ici ---
std::string conditionPreview(HmiDebugTarget code, const std::string& condition) {
    const std::string c = trimmed(condition);
    if (c.empty()) return "Sans condition : il s'arr\xC3\xAAtera \xC3\xA0 chaque passage.";
    if (!sim::parseExpression(c, hmi::dialectOptions()))
        return "Elle ne se lit pas : v\xC3\xA9rifie les parenth\xC3\xA8ses, les op\xC3\xA9rateurs (AND, OR, =, <>).";
    hmi::Runtime* rt = HmiDebugSession::instance().runtime();
    if (!rt || !rt->running()) return "La simulation ne tourne pas : elle sera essay\xC3\xA9" "e \xC3\xA0 chaque passage.";
    const hmi::Project* p = rt->project();
    const std::string name = p && !code.empty() ? hmi::Runtime::debugCodeName(*p, code.script, code.function) : std::string{};
    // Le verdict d'une valeur : vrai, faux (les noms et leurs valeurs), ou pas un BOOL (le point se desarmera).
    const auto verdict = [&](const std::string& value, const std::string& type) {
        if (type != "BOOL")
            return "Elle rend un " + type + ", pas un BOOL : le point se d\xC3\xA9sarmera au passage (une comparaison oubli\xC3\xA9" "e, \xC2\xAB = 0 \xC2\xBB ?).";
        std::string facts;
        for (const auto& n : conditionNames(c)) {
            std::string v;
            if (!rt->debugEvaluate(n, v)) continue;
            facts += (facts.empty() ? "" : ", ") + n + " = " + v;
            if (facts.size() > 80) break;
        }
        return std::string(value == "TRUE" ? "Maintenant, c'est vrai" : "Maintenant, c'est faux") + (facts.empty() ? std::string{} : " (" + facts + ")") + ".";
    };
    // En pause dans CE code (la ligne d'arret) : elle se lit la, comme au passage.
    const hmi::DebugHit* hit = rt->paused() && rt->debugHit() ? &*rt->debugHit() : nullptr;
    const bool here = hit && !hit->stack.empty() && !code.empty()
                      && HmiDebugTarget{hit->stack.back().script, hit->stack.back().function} == code;
    std::string value, why, type;
    if (here) {
        if (!rt->debugEvaluate(c, value, &why, &type)) return "Elle ne se lit pas ici : " + why;
        return verdict(value, type);
    }
    // Ailleurs : ses noms, juges dans le code du point (ses VAR, ses parametres, les variables IHM).
    if (!code.empty())
        if (const std::string problem = rt->conditionProblem(code.script, code.function, c); !problem.empty())
            return "Elle ne se lit pas dans " + (name.empty() ? std::string("ce code") : name) + " : " + problem + ".";
    // Hors pause, les variables de l'IHM se lisent maintenant ; les locales du code, seulement au passage.
    if (!rt->paused() && rt->debugEvaluate(c, value, &why, &type)) return verdict(value, type);
    return name.empty() ? std::string("Elle sera essay\xC3\xA9" "e au passage.")
                        : "Elle sera essay\xC3\xA9" "e au passage dans " + name + " (ses locales n'existent que pendant son ex\xC3\xA9" "cution).";
}

void askBreakpointCondition(HmiDebugTarget code, std::uint32_t line, const std::function<void(const std::string&, bool)>& say,
                            const std::string& lineText) {
    auto& s = HmiDebugSession::instance();
    std::string why;
    std::uint32_t id = s.at(code, line);
    if (!id) id = s.add(code, line, {}, &why);
    if (!id) {
        if (say && !why.empty()) say(why, true);
        return;
    }
    const auto* bp = s.find(id);
    const hmi::Project* p = projectOf(s);
    const std::string name = p ? hmi::Runtime::debugCodeName(*p, code.script, code.function) : std::string{};
    auto* manager = menu::MenuManager::instance();
    if (!manager) return;                       // sans ecran (essais) : le point est pose
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiBreakCondition";
    spec.title = "Condition du point d'arr\xC3\xAAt " + std::to_string(s.rankOf(id)) + (name.empty() ? std::string{} : " : " + name)
                 + ", ligne " + std::to_string(line);
    spec.text = "Il ne s'arr\xC3\xAAte que si elle est vraie. Du ST, dans la port\xC3\xA9" "e du code : ses locales (VAR, param\xC3\xA8tres), "
                "les variables IHM. Vide : \xC3\xA0 chaque passage.";
    if (!lineText.empty()) spec.text += "\nLa ligne : " + trimmed(lineText);
    if (bp && !bp->condition.empty()) spec.text += "\n" + conditionPreview(code, bp->condition);
    HmiAskDialog::Option o;
    o.label = "Condition";
    o.hasField = true;
    o.field = bp ? bp->condition : std::string{};
    o.placeholder = "P < 5.0 OR Tours MOD 100 = 0";
    o.monospace = true;
    o.check = [](const std::string& t) -> std::string {
        const std::string c = trimmed(t);
        if (c.empty()) return {};
        auto parsed = sim::parseExpression(c, hmi::dialectOptions());
        return parsed ? std::string{} : "Elle ne se lit pas (" + hmi::frenchSimMessage(parsed.error().message()) + ").";
    };
    spec.options.push_back(std::move(o));
    spec.note = "Une condition qui ne se lit pas au passage (un indice hors des bornes) d\xC3\xA9sarme le point, et le dit.";
    spec.confirm = "Valider";
    spec.width = 620.f;
    manager->ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [id, say, code](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = HmiAskDialog::parse(r.payload);
        (void)HmiDebugSession::instance().setCondition(id, a.field);
        if (say) say(blankText(a.field) ? "Point d'arr\xC3\xAAt sans condition : il s'arr\xC3\xAAtera \xC3\xA0 chaque passage."
                                        : "Condition du point d'arr\xC3\xAAt : " + trimmed(a.field) + " - " + conditionPreview(code, a.field),
                     false);
    });
}

// ------------------------------------------------------------------ la marge ---
void HmiBreakpointMargin::attach(ui::Widget& owner, ui::MultiLineText& editor, Target target, Say say) {
    owner_ = &owner;
    editor_ = &editor;
    target_ = std::move(target);
    say_ = std::move(say);
    editor.setBreakpointGutter(true);
    auto& s = HmiDebugSession::instance();
    links_ += editor.breakpointToggled->connect([this](std::size_t line0) {
        const auto code = target_ ? target_() : HmiDebugTarget{};
        std::string why;
        const bool placed = HmiDebugSession::instance().toggle(code, static_cast<std::uint32_t>(line0 + 1), &why);
        conditionOf_ = {};                       // 1.13.0 : un autre message - l'apercu d'une condition ne se redit plus
        if (say_) {
            if (!why.empty()) say_(why, true);
            else if (placed) say_("Point d'arr\xC3\xAAt pos\xC3\xA9 ligne " + std::to_string(line0 + 1) + " : la simulation s'y arr\xC3\xAAtera (Maj+clic : le d\xC3\xA9sactiver).", false);
            else say_("Point d'arr\xC3\xAAt retir\xC3\xA9 (ligne " + std::to_string(line0 + 1) + ").", false);
        }
        refresh();
    });
    links_ += editor.breakpointEnableToggled->connect([this](std::size_t line0) {
        auto& session = HmiDebugSession::instance();
        const auto code = target_ ? target_() : HmiDebugTarget{};
        if (const auto id = session.at(code, static_cast<std::uint32_t>(line0 + 1)))
            if (const auto* b = session.find(id)) {
                const bool on = !b->enabled;
                (void)session.setEnabled(id, on);
                conditionOf_ = {};
                if (say_)
                    say_(std::string(on ? "Point d'arr\xC3\xAAt r\xC3\xA9" "activ\xC3\xA9 ligne " : "Point d'arr\xC3\xAAt d\xC3\xA9" "sactiv\xC3\xA9 ligne ")
                             + std::to_string(line0 + 1) + (on ? std::string(" : la simulation s'y arr\xC3\xAAtera.")
                                                               : std::string(" : il reste, sans arr\xC3\xAAter (Maj+clic : le r\xC3\xA9" "activer).")),
                         false);
            }
        refresh();
    });
    links_ += editor.breakpointMenuRequested->connect([this](std::size_t line0, gfx::Point at) { openMenu(line0, at); });
    links_ += s.changed->connect([this] { refresh(); });
    reset();
    refresh();
}

void HmiBreakpointMargin::reset() {
    if (!editor_) return;
    const auto code = target_ ? target_() : HmiDebugTarget{};
    const std::string now = editor_->text();
    // Le meme code recharge (Annuler, Retablir, Migrer ce code, un remplacement) : ses points suivent leurs lignes.
    if (code == baselineOf_ && !code.empty()) HmiDebugSession::instance().followText(code, baseline_, now);
    baseline_ = now;
    baselineOf_ = code;
    shown_.clear();
    refresh();
}

void HmiBreakpointMargin::edited() {
    if (!editor_) return;
    const auto code = target_ ? target_() : HmiDebugTarget{};
    const std::string now = editor_->text();
    if (code == baselineOf_) HmiDebugSession::instance().followText(code, baseline_, now);
    baseline_ = now;
    baselineOf_ = code;
}

namespace {
// 1.13.0 : l'etat de la simulation dont depend l'apercu d'une condition : arretee, en marche, en pause (laquelle).
std::string conditionRunState() {
    const hmi::Runtime* rt = HmiDebugSession::instance().runtime();
    if (!rt || !rt->running()) return "arretee";
    return rt->paused() ? "pause " + std::to_string(rt->pauseCount()) : std::string("marche");
}
} // namespace

void HmiBreakpointMargin::refresh() {
    if (!editor_) return;
    auto& s = HmiDebugSession::instance();
    // 1.13.0 : l'apercu d'une condition dit plus haut (la barre du volet) date d'un autre etat de la simulation : il se
    // redit, recalcule pour l'etat d'ici ("Maintenant, c'est vrai (k = 3)", "Elle sera essayee au passage...").
    // Elle se redit a chaque changement (la marche, chaque pause, la reprise) jusqu'a l'arret qui suit la marche (dit une
    // derniere fois), ou jusqu'a ce que la marge dise autre chose.
    if (!conditionOf_.empty()) {
        if (const std::string state = conditionRunState(); state != conditionState_) {
            const bool ran = conditionState_ != "arretee";
            conditionState_ = state;
            const auto* b = s.find(s.at(conditionOf_, conditionLine_));
            const HmiDebugTarget code = conditionOf_;
            if (!b || blankText(b->condition) || (ran && state == "arretee")) conditionOf_ = {};
            if (b && !blankText(b->condition) && say_)
                say_("Condition du point d'arr\xC3\xAAt : " + trimmed(b->condition) + " - " + conditionPreview(code, b->condition), false);
        }
    }
    const auto code = target_ ? target_() : HmiDebugTarget{};
    std::vector<ui::MultiLineText::BreakMark> marks;
    std::string sig = std::to_string(code.script) + ":" + std::to_string(code.function);
    if (!code.empty())
        for (const auto& b : s.breakpoints()) {
            if (b.script != code.script || b.function != code.function) continue;
            const std::uint32_t line = b.effectiveLine ? b.effectiveLine : b.line;
            if (line == 0) continue;
            const bool armed = b.enabled && b.note.find("il ne s'arr\xC3\xAA") == std::string::npos;
            marks.push_back(ui::MultiLineText::BreakMark{line - 1, armed, !blankText(b.condition)});
            sig += "|" + std::to_string(line) + (armed ? "a" : "o") + (b.condition.empty() ? "" : "c");
        }
    HmiDebugTarget at;
    std::uint32_t line = 0;
    std::size_t exec = std::string::npos;
    if (s.pausedAt(at, line) && at == code && line > 0) exec = line - 1;
    sig += "#" + std::to_string(exec);
    if (sig == shown_) return;
    shown_ = sig;
    editor_->setBreakpoints(std::move(marks));
    editor_->setExecutionLine(exec);
    // La ligne en pause deja en vue : rien ne bouge ; sinon, en vue avec quelques lignes de contexte au-dessus
    // (pres du debut : depuis la premiere ligne).
    if (gfx::Rect seen{}; exec != std::string::npos && !editor_->positionRect(exec, 0, seen))
        editor_->scrollToLine(exec > 8 ? exec - 4 : 0);
    if (owner_) owner_->invalidate();
}

void HmiBreakpointMargin::openMenu(std::size_t line0, gfx::Point at) {
    if (!owner_) return;
    auto& s = HmiDebugSession::instance();
    const auto code = target_ ? target_() : HmiDebugTarget{};
    menuLine_ = static_cast<std::uint32_t>(line0 + 1);
    if (!menu_) {
        menu_ = &static_cast<ui::PopupMenu&>(owner_->addChild(std::make_unique<ui::PopupMenu>(owner_->id() + ".margeDebogage")));
        links_ += menu_->itemChosen->connect([this](int id) { choose(id); });
    }
    const std::uint32_t id = s.at(code, menuLine_);
    const auto* bp = id ? s.find(id) : nullptr;
    const hmi::Project* p = projectOf(s);
    const std::string name = p ? hmi::Runtime::debugCodeName(*p, code.script, code.function) : std::string{};
    std::size_t ofCode = 0;
    for (const auto& b : s.breakpoints()) ofCode += b.script == code.script && b.function == code.function;
    std::vector<ui::PopupMenu::Item> items;
    items.push_back(ui::cmd::heading("Ligne " + std::to_string(menuLine_), name.empty() ? std::string("marge") : "marge \xC2\xB7 " + name));
    ui::PopupMenu::Item toggle;
    toggle.label = "Point d'arr\xC3\xAAt";
    toggle.shortcut = "F9";
    toggle.checkable = true;
    toggle.checked = bp != nullptr;
    toggle.id = 1;
    toggle.enabled = !code.empty();
    if (!toggle.enabled) toggle.disabledReason = "pas encore de code ici";
    items.push_back(std::move(toggle));
    ui::PopupMenu::Item cond;
    cond.label = "Condition\xE2\x80\xA6";
    cond.id = 2;
    cond.enabled = !code.empty();
    if (bp && !bp->condition.empty()) cond.note = bp->condition;
    items.push_back(std::move(cond));
    ui::PopupMenu::Item onoff;
    onoff.label = bp && !bp->enabled ? "Activer" : "D\xC3\xA9sactiver";
    onoff.shortcut = "Maj+clic";
    onoff.id = 3;
    onoff.enabled = bp != nullptr;
    if (!onoff.enabled) onoff.disabledReason = "pas de point ici";
    items.push_back(std::move(onoff));
    items.push_back(ui::cmd::separator());
    ui::PopupMenu::Item all;
    all.label = "Retirer les points de ce code";
    all.id = 4;
    all.enabled = ofCode > 0;
    if (!all.enabled) all.disabledReason = "aucun point";
    items.push_back(std::move(all));
    menu_->setItems(std::move(items));
    menu_->setBounds(owner_->bounds());
    menu_->openAtSurface(at);
}

void HmiBreakpointMargin::choose(int item) {
    auto& s = HmiDebugSession::instance();
    if (item != 2) conditionOf_ = {};          // 1.13.0 : un autre geste - l'apercu d'une condition ne se redit plus
    const auto code = target_ ? target_() : HmiDebugTarget{};
    const std::uint32_t id = s.at(code, menuLine_);
    switch (item) {
        case 1: {
            std::string why;
            (void)s.toggle(code, menuLine_, &why);
            if (say_ && !why.empty()) say_(why, true);
            break;
        }
        case 2: askCondition(menuLine_); break;
        case 3:
            if (const auto* b = id ? s.find(id) : nullptr) (void)s.setEnabled(id, !b->enabled);
            break;
        case 4: {
            std::vector<std::uint32_t> ids;
            for (const auto& b : s.breakpoints())
                if (b.script == code.script && b.function == code.function) ids.push_back(b.id);
            for (const auto x : ids) (void)s.remove(x);
            if (say_) say_(std::to_string(ids.size()) + " point(s) d'arr\xC3\xAAt retir\xC3\xA9(s) de ce code.", false);
            break;
        }
        default: break;
    }
    refresh();
}

void HmiBreakpointMargin::askCondition(std::uint32_t line) {
    const auto code = target_ ? target_() : HmiDebugTarget{};
    std::string text;
    if (editor_) {
        const auto lines = splitLines(editor_->text());
        if (line >= 1 && line <= lines.size()) text = lines[line - 1];
    }
    // 1.13.0 : l'apercu dit apres Valider se redira, recalcule, au prochain changement d'etat de la simulation (refresh).
    askBreakpointCondition(code, line, [this, code, line](const std::string& t, bool warning) {
        if (say_) say_(t, warning);
        conditionOf_ = code;
        conditionLine_ = line;
        conditionState_ = conditionRunState();
    }, text);
    refresh();
}

} // namespace app
