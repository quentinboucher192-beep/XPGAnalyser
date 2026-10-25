// =============================================================================
//  hmi/HmiRuntimeDebug.cpp - 1.13.0 : le debogueur des scripts IHM
// -----------------------------------------------------------------------------
//  LA PAUSE EST REELLE, au milieu de l'execution (le debogueur de l'automate,
//  lui, ne s'arrete qu'en fin de cycle). Un script ST qui peut s'arreter (un
//  point arme, un pas demande, IHM_POINT_ARRET quelque part dans le projet)
//  tourne sur un fil a lui - ScriptTask, celui des scripts detaches (1.12.26) -
//  meme s'il n'est pas detache : au point d'arret, son fil rend la main a celui
//  de l'IHM (yield) et attend. L'IHM se fige (tick ne fait plus rien, les gestes
//  sont ignores), l'ecran continue de peindre la derniere image. Un seul fil
//  tourne a la fois : rien a verrouiller.
//
//  LE COUT EST NUL QUAND ON NE DEBOGUE PAS : sans point arme ni pas demande, les
//  scripts tournent sur le fil de l'IHM comme avant, et aucun programme ne porte
//  de ligne marquee.
//
//  OU L'ON EST. Chaque execution (un script, une fonction IHM simple) porte un
//  numero de programme (sim::setProgramTag) qui dit son code ; une fonction aux
//  types riches tourne dans le script qui l'appelle, sous sa propre Function
//  (richOwners_). La pile se lit dans le moteur : les executions qui attendent
//  un appel (sim::callerFrames), puis celle de la ligne (LineProbe::frames), et
//  au bas le contexte (le cycle IHM, un geste, un script detache).
//
//  LE PAS A PAS se regle par une profondeur : le nombre de cadres de la pile a
//  la pause. Suivant : la prochaine ligne a une profondeur au plus egale ;
//  Entrer : la prochaine ligne, ou qu'elle soit (dans du code qu'on sait
//  montrer) ; Sortir : la prochaine ligne d'un cadre plus bas. Un WAIT au milieu
//  rend la main normalement : l'IHM respire, puis le pas reprend.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiDecl.hpp"
#include "HmiEnums.hpp"
#include "HmiLog.hpp"
#include "HmiMarkers.hpp"
#include "HmiScript.hpp"
#include "../sim/DebugShared.hpp"
#include "../sim/Interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace hmi {

namespace {

std::string upperText(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool blank(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::size_t textHash(std::string_view s) { return std::hash<std::string_view>{}(s) ^ (s.size() * 0x9E3779B97F4A7C15ULL); }

const HmiFunction* functionById(const Project& p, Id id) {
    if (id == kNoId) return nullptr;
    if (const auto* f = p.function(id)) return f;
    for (const auto& v : p.views)
        for (const auto& f : v.functions)
            if (f.id == id) return &f;
    return nullptr;
}

// Le texte d'un code a deboguer, tel que le moteur le lit (les lignes du corps restent les memes que
// dans l'editeur) ; faux et `why` : inconnu, ou pas du ST execute.
bool debugText(const Project& p, Id script, Id function, std::string& text, bool& isFunction, std::string* why) {
    if (script != kNoId) {
        const Script* sc = p.script(script);
        if (!sc) {
            if (why) *why = "script introuvable : il ne s'arr\xC3\xAAtera pas";
            return false;
        }
        if (sc->lang != ScriptLang::ST) {
            if (why) *why = "non ex\xC3\xA9" "cut\xC3\xA9 en simulation (C/C++) : il ne s'arr\xC3\xAAtera pas";
            return false;
        }
        text = markers::strip(decl::codeOf(*sc));
        isFunction = false;
        return true;
    }
    if (const HmiFunction* f = functionById(p, function)) {
        text = functionText(*f);
        isFunction = true;
        return true;
    }
    if (why) *why = "fonction introuvable : il ne s'arr\xC3\xAAtera pas";
    return false;
}

std::vector<std::uint32_t> linesOfText(const std::string& text, bool isFunction, std::string* why) {
    if (isFunction) {
        auto fn = sim::parseFunction(text, "fonction");
        if (!fn) {
            if (why) *why = frenchSimMessage(fn.error().context.empty() ? fn.error().message() : fn.error().context);
            return {};
        }
        return sim::statementLines(**fn);
    }
    auto program = sim::parse(text, "script", dialectOptions());
    if (!program) {
        if (why) *why = frenchSimMessage(program.error().context.empty() ? program.error().message() : program.error().context);
        return {};
    }
    return sim::codeStatementLines(**program);
}

// Le texte sans ses commentaires ni ses chaines (remplaces par des blancs : les places restent).
std::string codeOnly(std::string_view s) {
    std::string out(s);
    for (std::size_t i = 0; i < out.size();) {
        if (out[i] == '(' && i + 1 < out.size() && out[i + 1] == '*') {
            const auto end = out.find("*)", i + 2);
            const std::size_t stop = end == std::string::npos ? out.size() : end + 2;
            for (std::size_t k = i; k < stop; ++k) if (out[k] != '\n') out[k] = ' ';
            i = stop;
        } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '\'' || out[i] == '"') {
            const char q = out[i];
            out[i++] = ' ';
            while (i < out.size() && out[i] != q && out[i] != '\n') out[i++] = ' ';
            if (i < out.size() && out[i] == q) out[i++] = ' ';
        } else {
            ++i;
        }
    }
    return out;
}

// Les noms que declare un code (en majuscules) : ce qui precede « : » (pas « := ») dans un bloc VAR,
// l'en-tete d'une fonction ; les variables de FOR, FOR EACH et CATCH ; le nom d'une fonction (son retour).
std::set<std::string> declaredNames(std::string_view code) {
    std::set<std::string> out;
    const std::string s = codeOnly(code);
    std::string previous;
    bool afterForEach = false;
    for (std::size_t i = 0; i < s.size();) {
        if (!identStart(s[i])) {
            if (s[i] == ';' || s[i] == '\n') afterForEach = false;
            ++i;
            continue;
        }
        const std::size_t from = i;
        while (i < s.size() && identChar(s[i])) ++i;
        const std::string word = upperText(std::string_view(s).substr(from, i - from));
        std::size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
        const bool colon = j < s.size() && s[j] == ':' && (j + 1 >= s.size() || s[j + 1] != '=');
        if (colon || previous == "FOR" || previous == "CATCH" || previous == "FUNCTION" || afterForEach) out.insert(word);
        if (previous == "FOR" && word == "EACH") afterForEach = true;
        if (word == "IN" || word == "DO" || word == "TO") afterForEach = false;
        if (afterForEach && word != "EACH") out.insert(word);
        previous = word;
    }
    return out;
}

// Les noms d'une condition qu'il faut connaitre : le debut de chaque chemin (Armoires dans
// Armoires[0].ana), sans les mots du langage, les litteraux types (T#5s, 16#FF, T_MODE#Auto) ni les
// fonctions appelees (un nom suivi de « ( »).
std::vector<std::string> conditionRoots(std::string_view condition) {
    static const std::set<std::string> kWords = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "NULL", "IN", "BETWEEN"};
    std::vector<std::string> out;
    const std::string s = codeOnly(condition);
    for (std::size_t i = 0; i < s.size();) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {             // un nombre : 12, 3.5, 16#FF, 2#1010, 1e3
            while (i < s.size() && (identChar(s[i]) || s[i] == '.' || s[i] == '#')) ++i;
            continue;
        }
        if (!identStart(s[i])) { ++i; continue; }
        std::size_t back = i;
        while (back > 0 && (s[back - 1] == ' ' || s[back - 1] == '\t')) --back;
        const bool member = back > 0 && (s[back - 1] == '.' || s[back - 1] == '#');
        const std::size_t from = i;
        while (i < s.size() && identChar(s[i])) ++i;
        const std::string word(s.substr(from, i - from));
        std::size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
        bool called = j < s.size() && s[j] == '(';
        if (!called && i < s.size() && s[i] == '<') {                   // Limiter<INT>(x) : un appel generique (colle)
            const auto close = s.find('>', i);
            std::size_t k = close == std::string::npos ? s.size() : close + 1;
            while (k < s.size() && s[k] == ' ') ++k;
            called = close != std::string::npos && k < s.size() && s[k] == '(';
        }
        const bool typed = i < s.size() && s[i] == '#';
        if (typed) {                                                     // T#5s, T_MODE#Auto : tout le litteral
            ++i;
            while (i < s.size() && (identChar(s[i]) || s[i] == '.' || s[i] == ':' || s[i] == '-')) ++i;
            continue;
        }
        if (member || called || kWords.count(upperText(word))) continue;
        if (std::find(out.begin(), out.end(), word) == out.end()) out.push_back(word);
    }
    return out;
}

std::string conditionFrench(const core::Error& e) { return frenchSimMessage(sim::debug::conditionProblem(e)); }

struct Probing {
    int& n;
    explicit Probing(int& x) : n(x) { ++n; }
    ~Probing() { --n; }
    Probing(const Probing&) = delete;
    Probing& operator=(const Probing&) = delete;
};

} // namespace

// ------------------------------------------------------------------- les codes ---
std::vector<std::uint32_t> Runtime::breakableLines(const Project& p, Id script, Id function) {
    std::string text;
    bool isFunction = false;
    if (!debugText(p, script, function, text, isFunction, nullptr)) return {};
    return linesOfText(text, isFunction, nullptr);
}

std::string Runtime::debugCodeName(const Project& p, Id script, Id function) {
    if (script != kNoId) {
        const Script* sc = p.script(script);
        if (!sc) return {};
        if (const Id v = p.viewOfScript(script); v != kNoId)
            if (const View* view = p.view(v)) return view->name + "." + sc->event;
        return "script " + sc->name;
    }
    if (function != kNoId) {
        if (const auto* f = p.function(function)) return "fonction " + f->name;
        for (const auto& v : p.views)
            for (const auto& f : v.functions)
                if (f.id == function) return "fonction " + v.name + "." + f.name;
    }
    return {};
}

std::size_t Runtime::debugCodeHash(Id script, Id function) const {
    if (!project_) return 0;
    std::string text;
    bool isFunction = false;
    if (!debugText(*project_, script, function, text, isFunction, nullptr)) return 0;
    return textHash(text);
}

std::uint32_t Runtime::debugTag(Id script, Id function) {
    if (script == kNoId && function == kNoId) return 0;
    const auto key = std::make_pair(script, function);
    if (const auto it = ownerTags_.find(key); it != ownerTags_.end()) return it->second;
    tagOwners_.push_back(key);
    const auto tag = static_cast<std::uint32_t>(tagOwners_.size());
    ownerTags_[key] = tag;
    return tag;
}

std::pair<Id, Id> Runtime::frameOwner(const sim::LineProbe::Frame& f) const {
    if (f.standalone) {
        const auto it = richOwners_.find(static_cast<const sim::Function*>(f.code));
        return it == richOwners_.end() ? std::make_pair(kNoId, kNoId) : std::make_pair(kNoId, it->second);
    }
    if (f.tag == 0 || f.tag > tagOwners_.size()) return {kNoId, kNoId};
    return tagOwners_[f.tag - 1];
}

// ----------------------------------------------------------------- les points ---
void Runtime::setDebugger(bool on) {
    if (debugOn_ == on) return;
    debugOn_ = on;
    if (!on) {
        breakpoints_.clear();
        resolveBreakpoints();
    }
    ++debugRev_;
}

void Runtime::setBreakpoints(std::vector<DebugBreakpoint> list) {
    // Les passages comptes restent (le meme point, par son identifiant).
    for (auto& bp : list)
        for (const auto& old : breakpoints_)
            if (old.id == bp.id) bp.hits = old.hits;
    breakpoints_ = std::move(list);
    resolveBreakpoints();
}

// Chacun retrouve son code, sa ligne (la premiere instruction a partir de celle demandee), sa condition
// (lue dans le dialecte ; ses noms connus du code ou de l'IHM - un nom inconnu se dit tout de suite).
void Runtime::resolveBreakpoints() {
    breakStates_.assign(breakpoints_.size(), BreakState{});
    anyArmed_ = false;
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        auto& bp = breakpoints_[i];
        auto& st = breakStates_[i];
        bp.effectiveLine = 0;
        bp.note.clear();
        if (!project_) continue;
        std::string text, why;
        bool isFunction = false;
        if (!debugText(*project_, bp.script, bp.function, text, isFunction, &why)) {
            bp.note = why;
            continue;
        }
        st.codeHash = textHash(text);
        const auto lines = linesOfText(text, isFunction, &why);
        if (lines.empty()) {
            bp.note = why.empty() ? std::string("aucune instruction : il ne s'arr\xC3\xAAtera pas")
                                  : "le code ne se lit pas (" + why + ") : il ne s'arr\xC3\xAAtera pas";
            continue;
        }
        const auto want = std::max<std::uint32_t>(1, bp.line);
        const auto at = sim::debug::effectiveLine(lines, want);
        if (at == 0) {
            bp.note = sim::debug::noStatementNote(want);
            continue;
        }
        bp.effectiveLine = at;
        if (at != bp.line) bp.note = sim::debug::shiftedNote(at, bp.line);
        if (!blank(bp.condition)) {
            auto parsed = sim::parseExpression(bp.condition, dialectOptions());
            if (!parsed) {
                sim::debug::addNote(bp.note, sim::debug::invalidCondition(conditionFrench(parsed.error())));
                continue;
            }
            // Les noms : ceux que le code declare (ses VAR, ses parametres, ses boucles), les declarations du
            // modele, puis tout ce que l'IHM connait (variables, vues, systeme, automate, enumerations).
            auto declared = declaredNames(text);
            const View* view = nullptr;
            if (bp.script != kNoId) {
                if (const Script* sc = project_->script(bp.script)) {
                    for (const auto& d : sc->decls) declared.insert(upperText(d.name));
                    if (const Id v = project_->viewOfScript(bp.script); v != kNoId) view = project_->view(v);
                }
            } else if (const HmiFunction* f = functionById(*project_, bp.function)) {
                for (const auto& d : f->decls) declared.insert(upperText(d.name));
                declared.insert(upperText(f->name));
            }
            std::string unknown;
            for (const auto& root : conditionRoots(bp.condition)) {
                const std::string u = upperText(root);
                if (declared.count(u)) continue;
                if (view && (view->param(root) || view->objectByName(root))) continue;
                if (view && std::any_of(view->constants.begin(), view->constants.end(), [&](const Declaration& d) { return upperText(d.name) == u; }))
                    continue;
                if (envOf().exists(root) || !envOf().declaredType(root).empty() || envOf().richVariable(root)) continue;
                if (project_->variable(root) || project_->viewByName(root) || project_->generalScript(root) || u == "SYS" || u == "API") continue;
                if (logLevelByName(root)) continue;
                bool enumValue = false;
                for (const auto& t : project_->programs.types)
                    for (const auto& ev : t.values)
                        if (upperText(ev.name) == u) enumValue = true;
                if (enumValue) continue;
                unknown = root;
                break;
            }
            if (!unknown.empty()) {
                sim::debug::addNote(bp.note, sim::debug::invalidCondition(unknown + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici"));
                continue;
            }
            st.condition = *parsed;
        }
        st.armed = bp.enabled;
        anyArmed_ = anyArmed_ || st.armed;
    }
    remarkPrograms();
    ++debugRev_;
}

// Les lignes marquees de chaque programme deja lu et de chaque fonction aux types riches, refaites tout
// de suite (un point pose pendant la pause sert des la reprise) ; aucun point : plus rien de marque.
void Runtime::remarkPrograms() {
    const auto linesOf = [this](Id script, Id function) {
        std::vector<std::uint32_t> lines;
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
            if (breakStates_[i].armed && breakpoints_[i].effectiveLine && breakpoints_[i].script == script
                && breakpoints_[i].function == function)
                lines.push_back(breakpoints_[i].effectiveLine);
        return lines;
    };
    if (!anyArmed_ && !marked_) return;
    for (auto& [key, prep] : programs_) {
        if (!prep.program) continue;
        const auto tag = sim::programTag(*prep.program);
        if (tag == 0 || tag > tagOwners_.size()) {
            sim::setBreakLines(*prep.program, {});
            continue;
        }
        const auto [script, function] = tagOwners_[tag - 1];
        sim::setBreakLines(*prep.program, anyArmed_ ? linesOf(script, function) : std::vector<std::uint32_t>{});
    }
    for (const auto& [fn, id] : richOwners_)
        if (fn) sim::setBreakLines(*fn, anyArmed_ ? linesOf(kNoId, id) : std::vector<std::uint32_t>{});
    marked_ = anyArmed_;
}

// Avant chaque execution : le numero de son code, le pas a pas possible, ses lignes marquees.
void Runtime::prepareDebug(sim::Program& program, Id script, Id function, sim::RunLimits& limits) {
    if (!debugOn_) return;
    sim::setProgramTag(program, debugTag(script, function));
    limits.stepAll = &stepAll_;
    if (!anyArmed_ && !marked_) return;
    std::vector<std::uint32_t> lines;
    if (anyArmed_)
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
            if (breakStates_[i].armed && breakpoints_[i].effectiveLine && breakpoints_[i].script == script
                && breakpoints_[i].function == function && (script != kNoId || function != kNoId))
                lines.push_back(breakpoints_[i].effectiveLine);
    sim::setBreakLines(program, lines);
}

void Runtime::markRichFunction(const sim::Function& fn, Id function) {
    if (!debugOn_) return;
    richOwners_[&fn] = function;
    if (!anyArmed_ && !marked_) return;
    std::vector<std::uint32_t> lines;
    if (anyArmed_)
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
            if (breakStates_[i].armed && breakpoints_[i].effectiveLine && breakpoints_[i].script == kNoId
                && breakpoints_[i].function == function)
                lines.push_back(breakpoints_[i].effectiveLine);
    sim::setBreakLines(fn, lines);
}

bool Runtime::debugWanted() const noexcept { return debugOn_ && (anyArmed_ || codeBreaks_ || pauseAsked_ || step_.has_value()); }

// Une fois par seconde : le projet ecrit-il IHM_POINT_ARRET quelque part ; un code dont un point depend
// a-t-il change (son texte : il se relit, la ligne se re-decale et le dit).
void Runtime::debugTick(double now) {
    if (!debugOn_ || !project_ || now - codeBreaksAt_ < 1.0) return;
    codeBreaksAt_ = now;
    const auto writes = [](const std::string& body) {
        static const std::string kWord = "IHM_POINT_ARRET";
        if (body.size() < kWord.size()) return false;
        for (std::size_t i = 0; i + kWord.size() <= body.size(); ++i) {
            if (std::toupper(static_cast<unsigned char>(body[i])) != 'I') continue;
            std::size_t k = 0;
            while (k < kWord.size() && std::toupper(static_cast<unsigned char>(body[i + k])) == kWord[k]) ++k;
            if (k == kWord.size()) return true;
        }
        return false;
    };
    bool any = false;
    for (const auto& sc : project_->programs.scripts) any = any || (sc.lang == ScriptLang::ST && writes(sc.body));
    for (const auto& f : project_->programs.functions) any = any || writes(f.body);
    for (const auto& v : project_->views) {
        for (const auto& sc : v.scripts) any = any || (sc.lang == ScriptLang::ST && writes(sc.body));
        for (const auto& f : v.functions) any = any || writes(f.body);
    }
    codeBreaks_ = any;
    for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
        if (debugCodeHash(breakpoints_[i].script, breakpoints_[i].function) != breakStates_[i].codeHash) {
            resolveBreakpoints();
            break;
        }
}

void Runtime::debugNote(std::size_t index, const std::string& note) {
    if (index >= breakpoints_.size() || index >= breakStates_.size()) return;
    if (!breakStates_[index].said.insert(note).second) return;
    sim::debug::addNote(breakpoints_[index].note, note);
    ++debugRev_;
}

// ------------------------------------------------------------------ la pile ---
std::vector<DebugFrame> Runtime::debugStack(const std::vector<sim::LineProbe::Frame>& chain,
                                            const std::vector<sim::LineProbe::Frame>& here) const {
    std::vector<DebugFrame> out;
    // Au bas : ce qui a lance le code.
    DebugFrame context;
    context.label = taskContext(currentTask_);
    out.push_back(std::move(context));
    std::vector<const sim::LineProbe::Frame*> all;
    for (const auto& f : chain) all.push_back(&f);
    for (const auto& f : here) all.push_back(&f);
    std::size_t cursor = 0;                    // les executions (debugExec_) suivent l'ordre de la pile
    for (const auto* f : all) {
        if (f->tag == 0 && !f->standalone) continue;          // une expression (un texte a trous, une fx) : pas de code
        const auto [script, function] = frameOwner(*f);
        DebugFrame d;
        d.script = script;
        d.function = function;
        d.line = f->line;
        d.locals = f->locals;
        const std::string code = project_ ? debugCodeName(*project_, script, function) : std::string{};
        if (f->function.empty()) d.label = code.empty() ? std::string("code") : code;
        else if (f->standalone) d.label = code.empty() ? "fonction " + f->function : code;
        else d.label = f->function + " (fonction interne de " + (code.empty() ? std::string("ce code") : code) + ")";
        // Une fonction IHM simple : ses locales sont dans le cadre du moteur.
        if (f->function.empty() && function != kNoId) {
            for (std::size_t k = cursor; k < debugExec_.size(); ++k) {
                if (debugExec_[k].tag != f->tag) continue;
                cursor = k + 1;
                const auto* frame = debugExec_[k].frame;
                const auto* locals = debugExec_[k].locals;
                if (!frame) break;
                d.locals.clear();
                const auto shown = [](const sim::Value& v) { return v.type() == sim::Type::String ? "'" + v.asString() + "'" : v.display(); };
                std::set<std::string> seen;
                if (locals)
                    for (const auto& l : *locals) {
                        const auto it = frame->find(upperText(l.name));
                        if (it == frame->end()) continue;
                        DebugLocal x;
                        x.name = l.name;
                        x.type = l.type;
                        x.value = shown(it->second);
                        x.parameter = l.isParameter();
                        d.locals.push_back(std::move(x));
                        seen.insert(it->first);
                    }
                for (const auto& [name, v] : *frame) {
                    if (seen.count(name)) continue;
                    DebugLocal x;
                    x.name = name;
                    x.value = shown(v);
                    d.locals.push_back(std::move(x));
                }
                break;
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

// --------------------------------------------------------- une ligne atteinte ---
void Runtime::debugReached(std::uint32_t tag, std::uint32_t line, sim::LineProbe& probe, bool code) {
    (void)tag;
    if (!debugOn_ || debugProbing_ > 0 || !running_) return;
    if (currentTask_ && taskIsCancelled(currentTask_)) {
        probe.stop();
        return;
    }
    std::vector<sim::LineProbe::Frame> here;
    {
        const Probing p(debugProbing_);
        here = probe.frames();
    }
    if (here.empty()) return;
    const auto owner = frameOwner(here.back());
    const bool known = owner.first != kNoId || owner.second != kNoId;

    // Pourquoi s'arreter : un point de cette ligne (sa condition vraie), IHM_POINT_ARRET, une pause
    // demandee, le pas en cours (dans cette tache, a la bonne profondeur).
    std::uint32_t hitId = 0;
    std::size_t hitIndex = static_cast<std::size_t>(-1);
    bool disarmed = false;
    if (!code && known) {
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i) {
            auto& bp = breakpoints_[i];
            auto& st = breakStates_[i];
            if (!st.armed || bp.effectiveLine != line || bp.script != owner.first || bp.function != owner.second) continue;
            if (st.condition) {
                core::Result<sim::Value> v = sim::Value::boolean(false);
                {
                    const Probing p(debugProbing_);
                    ++readOnly_;
                    v = probe.evaluate(*st.condition);
                    --readOnly_;
                }
                if (!v) {
                    // Invalide ici (un indice hors des bornes pour cette instance...) : il ne s'arretera plus, et le dit.
                    st.armed = false;
                    st.condition.reset();
                    debugNote(i, sim::debug::invalidCondition(conditionFrench(v.error())));
                    disarmed = true;
                    continue;
                }
                if (!v->isTruthy()) continue;
            }
            ++bp.hits;
            ++debugRev_;
            if (hitId == 0) {
                hitId = bp.id;
                hitIndex = i;
            }
        }
    }
    if (disarmed) {
        anyArmed_ = std::any_of(breakStates_.begin(), breakStates_.end(), [](const BreakState& s) { return s.armed; });
        remarkPrograms();
    }
    DebugHit::Why why = DebugHit::Why::Breakpoint;
    bool stop = false;
    std::vector<sim::LineProbe::Frame> chain;
    bool chainRead = false;
    const auto readChain = [&] {
        if (chainRead) return;
        const Probing p(debugProbing_);
        chain = sim::callerFrames();
        chainRead = true;
    };
    if (code) {
        stop = true;
        why = DebugHit::Why::Code;
    } else if (hitId) {
        stop = true;
    } else if (pauseAsked_ && known) {
        stop = true;
        why = DebugHit::Why::Pause;
    } else if (step_ && currentTask_ && step_->task == currentTask_ && known) {
        readChain();
        const std::size_t depth = chain.size() + here.size();
        switch (step_->step) {
            case DebugStep::Over: stop = depth <= step_->depth; break;
            case DebugStep::Into: stop = true; break;
            case DebugStep::Out:  stop = depth < step_->depth; break;
        }
        why = DebugHit::Why::Step;
    }
    if (!stop) return;

    // On ne peut pas s'arreter partout : dans une fonction appelee par une expression (elle doit rendre sa valeur
    // tout de suite, comme WAIT), hors d'un script lance par l'IHM (un minuteur, une surveillance), ou deja en pause.
    const auto refuse = [&](const std::string& said) {
        if (hitIndex != static_cast<std::size_t>(-1)) {
            debugNote(hitIndex, said);
        } else if (why == DebugHit::Why::Code) {
            const std::string key = std::to_string(owner.first) + ":" + std::to_string(owner.second) + ":" + std::to_string(line);
            if (debugSaid_.insert(key).second)
                logAt(LogLevel::Warning, "D\xC3\xA9" "bogage", source_, "IHM_POINT_ARRET : " + said, static_cast<int>(line));
        }
    };
    if (paused_) return;
    if (readOnly_ > 0) {
        refuse("appel\xC3\xA9" "e par une expression : pas d'arr\xC3\xAAt");
        return;
    }
    if (!currentTask_) {
        refuse("hors d'un script lanc\xC3\xA9 par l'IHM (un minuteur, une surveillance) : pas d'arr\xC3\xAAt");
        return;
    }

    // L'arret : ou, pourquoi, les valeurs de la ligne, la pile et ses locales.
    readChain();
    DebugHit hit;
    hit.why = why;
    hit.breakpoint = hitId;
    hit.script = owner.first;
    hit.function = owner.second;
    hit.line = line;
    hit.cycle = static_cast<std::uint64_t>(cycles_);
    hit.detached = taskIsDetached(currentTask_);
    {
        const Probing p(debugProbing_);
        ++readOnly_;
        hit.values = probe.lineValues();
        --readOnly_;
    }
    for (auto& [name, value] : hit.values) {
        if (value.empty()) {
            const bool aggregate = envOf().richVariable(name) != nullptr || this->aggregate(name) != nullptr;
            value = aggregate ? std::string("(structure ou tableau)") : std::string("?");
        } else if (variableForced(name)) {
            value += " (forc\xC3\xA9" "e)";
        }
    }
    hit.stack = debugStack(chain, here);
    hit.context = hit.stack.empty() ? std::string{} : hit.stack.front().label;
    const std::string codeName = project_ ? debugCodeName(*project_, owner.first, owner.second) : std::string{};
    hit.where = (codeName.empty() ? std::string("code") : codeName) + ", ligne " + std::to_string(line);
    std::string said;
    switch (why) {
        case DebugHit::Why::Breakpoint: {
            std::size_t rank = 0;
            for (std::size_t i = 0; i < breakpoints_.size(); ++i)
                if (breakpoints_[i].id == hitId) rank = i + 1;
            said = "arr\xC3\xAAt\xC3\xA9 au point " + std::to_string(rank) + " (" + hit.where + ")";
            break;
        }
        case DebugHit::Why::Code: said = "arr\xC3\xAAt\xC3\xA9 par IHM_POINT_ARRET (" + hit.where + ")"; break;
        case DebugHit::Why::Pause: said = "en pause (" + hit.where + ")"; break;
        case DebugHit::Why::Step: said = "en pause apr\xC3\xA8s le pas (" + hit.where + ")"; break;
    }
    pausedDepth_ = chain.size() + here.size();
    debugHit_ = std::move(hit);
    paused_ = currentTask_;
    pausedProbe_ = &probe;
    pausedScript_ = owner.first;
    pausedFunction_ = owner.second;
    pausedHash_ = debugCodeHash(owner.first, owner.second);
    step_.reset();
    pauseAsked_ = false;
    stepAll_ = false;
    ++debugRev_;
    logAt(LogLevel::Info, "D\xC3\xA9" "bogage", source_, said, static_cast<int>(line));

    // La pause : le fil du script rend la main a celui de l'IHM et attend Continuer, un pas, ou l'arret.
    TaskState* self = currentTask_;
    const bool go = pauseTask(self);
    pausedProbe_ = nullptr;
    if (paused_ == self) {                     // l'arret de la simulation (stopTasks) : sans reprise
        paused_ = nullptr;
        debugHit_.reset();
    }
    if (!go) {
        probe.stop();
        return;
    }
    // Le code a change pendant la pause : il ne peut pas reprendre au milieu d'une ligne qui n'existe plus.
    if (debugCodeHash(owner.first, owner.second) != pausedHash_) {
        logAt(LogLevel::Warning, "D\xC3\xA9" "bogage", source_,
              (codeName.empty() ? std::string("le script") : codeName) + " a chang\xC3\xA9 pendant la pause : il reprendra au prochain cycle",
              static_cast<int>(line));
        cancelTask(self);
        probe.stop();
    }
}

// ---------------------------------------------------------------- la reprise ---
void Runtime::debugResume(std::optional<StepOrder> order, const std::string& said) {
    TaskState* t = paused_;
    if (!t) return;
    paused_ = nullptr;
    debugHit_.reset();
    step_ = order;
    if (step_) step_->task = t;
    stepAll_ = step_.has_value() || pauseAsked_;
    ++debugRev_;
    if (!said.empty()) logAt(LogLevel::Info, "D\xC3\xA9" "bogage", "IHM", said);
    resumePaused(t);
}

bool Runtime::debugContinue() {
    if (!paused_) return false;
    debugResume(std::nullopt, "continu\xC3\xA9");
    return true;
}

bool Runtime::debugStep(DebugStep step) {
    if (!paused_) return false;
    StepOrder order;
    order.step = step;
    order.depth = pausedDepth_;
    const char* name = step == DebugStep::Over ? "Suivant" : step == DebugStep::Into ? "Entrer" : "Sortir";
    debugResume(order, std::string("pas : ") + name);
    return true;
}

void Runtime::debugPause() {
    if (paused_ || !debugOn_) return;
    pauseAsked_ = true;
    stepAll_ = true;
    ++debugRev_;
    logAt(LogLevel::Info, "D\xC3\xA9" "bogage", "IHM", "pause demand\xC3\xA9" "e : le prochain script qui tourne s'arr\xC3\xAAtera \xC3\xA0 sa ligne");
}

void Runtime::clearDebugState() {
    paused_ = nullptr;
    pausedProbe_ = nullptr;
    debugHit_.reset();
    step_.reset();
    pauseAsked_ = false;
    stepAll_ = false;
    ++debugRev_;
}

bool Runtime::debugEvaluate(const std::string& expression, std::string& value, std::string* why) {
    auto parsed = sim::parseExpression(expression, dialectOptions());
    if (!parsed) {
        if (why) *why = conditionFrench(parsed.error());
        return false;
    }
    core::Result<sim::Value> r = sim::Value::boolean(false);
    if (paused_ && pausedProbe_) {
        withTaskState(paused_, [&] {
            const Probing p(debugProbing_);
            ++readOnly_;
            r = pausedProbe_->evaluate(**parsed);
            --readOnly_;
        });
    } else {
        const Probing p(debugProbing_);
        ++readOnly_;
        r = sim::evaluate(**parsed, envOf());
        --readOnly_;
    }
    if (!r) {
        if (why) *why = conditionFrench(r.error());
        return false;
    }
    value = r->type() == sim::Type::String ? "'" + r->asString() + "'" : r->display();
    return true;
}

} // namespace hmi
