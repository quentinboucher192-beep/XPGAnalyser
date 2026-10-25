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
    static const char* const kWords[] = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "NULL"};
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

void HmiDebugSession::bindProject(const hmi::Project* owner) {
    if (owner == owner_ || !owner) return;
    const bool had = !list_.empty();
    owner_ = owner;
    list_.clear();
    level_ = 0;
    if (had) touch(true);
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

void HmiDebugSession::shiftLines(HmiDebugTarget code, std::uint32_t from, int delta) {
    if (delta == 0) return;
    bool any = false;
    for (auto& b : list_) {
        if (b.script != code.script || b.function != code.function || b.line < from) continue;
        const long long moved = static_cast<long long>(b.line) + delta;
        b.line = static_cast<std::uint32_t>(std::max<long long>(from, moved));
        b.effectiveLine = 0;                      // le moteur la relira
        any = true;
    }
    if (any) touch(true);
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
    if (!rt.paused()) level_ = 0;
    touch(false);
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
std::string conditionPreview(const std::string& condition) {
    const std::string c = trimmed(condition);
    if (c.empty()) return "Sans condition : il s'arr\xC3\xAAtera \xC3\xA0 chaque passage.";
    if (!sim::parseExpression(c, hmi::dialectOptions()))
        return "Elle ne se lit pas : v\xC3\xA9rifie les parenth\xC3\xA8ses, les op\xC3\xA9rateurs (AND, OR, =, <>).";
    hmi::Runtime* rt = HmiDebugSession::instance().runtime();
    if (!rt || !rt->running()) return "La simulation ne tourne pas : elle sera essay\xC3\xA9" "e \xC3\xA0 chaque passage.";
    std::string value, why;
    if (!rt->debugEvaluate(c, value, &why))
        return rt->paused() ? "Elle ne se lit pas ici : " + why
                            : "Elle se lira au passage (les locales du code n'existent que pendant son ex\xC3\xA9" "cution).";
    std::string facts;
    for (const auto& n : conditionNames(c)) {
        std::string v;
        if (!rt->debugEvaluate(n, v)) continue;
        facts += (facts.empty() ? "" : ", ") + n + " = " + v;
        if (facts.size() > 80) break;
    }
    const bool truth = value == "TRUE";
    return std::string(truth ? "Maintenant, c'est vrai" : "Maintenant, c'est faux") + (facts.empty() ? std::string{} : " (" + facts + ")") + ".";
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
    if (bp && !bp->condition.empty()) spec.text += "\n" + conditionPreview(bp->condition);
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
    manager->ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [id, say](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = HmiAskDialog::parse(r.payload);
        (void)HmiDebugSession::instance().setCondition(id, a.field);
        if (say) say(blankText(a.field) ? "Point d'arr\xC3\xAAt sans condition : il s'arr\xC3\xAAtera \xC3\xA0 chaque passage."
                                        : "Condition du point d'arr\xC3\xAAt : " + trimmed(a.field) + " - " + conditionPreview(a.field),
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
            if (const auto* b = session.find(id)) (void)session.setEnabled(id, !b->enabled);
        refresh();
    });
    links_ += editor.breakpointMenuRequested->connect([this](std::size_t line0, gfx::Point at) { openMenu(line0, at); });
    links_ += s.changed->connect([this] { refresh(); });
    reset();
    refresh();
}

void HmiBreakpointMargin::reset() {
    if (!editor_) return;
    baseline_ = editor_->text();
    baselineOf_ = target_ ? target_() : HmiDebugTarget{};
    shown_.clear();
    refresh();
}

void HmiBreakpointMargin::edited() {
    if (!editor_) return;
    const auto code = target_ ? target_() : HmiDebugTarget{};
    const std::string now = editor_->text();
    if (!(code == baselineOf_)) {
        baseline_ = now;
        baselineOf_ = code;
        return;
    }
    const auto before = splitLines(baseline_);
    const auto after = splitLines(now);
    baseline_ = now;
    const int delta = static_cast<int>(after.size()) - static_cast<int>(before.size());
    if (delta == 0 || code.empty()) return;
    // Les lignes du bas qui n'ont pas change (le suffixe commun) glissent de `delta`.
    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    const auto from = static_cast<std::uint32_t>(before.size() - suffix + 1);   // la premiere ligne (1 = la premiere) qui glisse
    HmiDebugSession::instance().shiftLines(code, from, delta);
}

void HmiBreakpointMargin::refresh() {
    if (!editor_) return;
    auto& s = HmiDebugSession::instance();
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
    if (exec != std::string::npos) editor_->scrollToLine(exec);
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
    askBreakpointCondition(code, line, say_, text);
    refresh();
}

} // namespace app
