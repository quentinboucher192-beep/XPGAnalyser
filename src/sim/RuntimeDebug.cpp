// =============================================================================
//  sim/RuntimeDebug.cpp - lot API 8 : le debogage et la modification en ligne
// -----------------------------------------------------------------------------
//  POINTS D'ARRET. Une ligne d'une section (ou "BLOC.Section", le corps d'un
//  DFB), une condition ST facultative. Le runner teste, a chaque instruction,
//  un vecteur de lignes marquees - vide pour une section sans point d'arret :
//  le cout est nul quand il n'y en a pas. Au passage, le runtime evalue la
//  condition (sans rien ecrire, sans appeler de bloc), compte le passage, et
//  retient le premier du cycle : les valeurs de la ligne, la pile, le cycle.
//  Le cycle va AU BOUT. On ne s'arrete jamais au milieu : l'etat d'un automate
//  n'est coherent qu'entre deux cycles (les sorties d'une section ecrites, pas
//  celles de la suivante), et c'est aussi ce que fait Control Expert en
//  simulation quand on lui demande un arret « en fin de cycle ».
//
//  MODIFICATION EN LIGNE. Le programme a change pendant que la simulation
//  tourne ou est en pause : un runtime neuf est prepare sur le nouveau code et
//  reprend les valeurs de l'ancien - chaque case de meme nom et de meme type -,
//  son horloge, son compte de cycles, ses forcages, ses courbes. Ce qui est
//  nouveau part de sa valeur initiale, ce qui a disparu est oublie.
// =============================================================================
#include "Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_set>

namespace sim {
namespace {

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

void addNote(Breakpoint& bp, const std::string& note) {
    bp.note = bp.note.empty() ? note : bp.note + " ; " + note;
}

// Ce que dit le moteur d'une condition qui ne va pas, en francais.
std::string conditionProblem(const core::Error& e) {
    const std::string& m = e.context;
    if (m.size() > 2 && m.front() == '\'') {
        if (const auto close = m.find('\'', 1); close != std::string::npos) {
            const auto name = m.substr(1, close - 1);
            const std::string_view rest = std::string_view(m).substr(close + 1);
            if (rest == " is not declared") return name + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici";
            if (rest.rfind(" is not a function", 0) == 0)
                return name + " n'est pas une fonction que le simulateur conna\xC3\xAEt";
        }
    }
    if (m.rfind("line ", 0) == 0) {
        std::string found;
        if (const auto f = m.rfind("(found '"); f != std::string::npos && m.size() >= f + 10)
            found = m.substr(f + 8, m.size() - f - 10);
        return found.empty() ? std::string("elle ne se lit pas")
                             : "elle ne se lit pas (pr\xC3\xA8s de \xC2\xAB " + found + " \xC2\xBB)";
    }
    if (m == "division by zero" || m == "modulo by zero") return "division par z\xC3\xA9ro";
    if (e.code == core::ErrorCode::NotImplemented)
        return "\xC2\xAB " + m + " \xC2\xBB : une condition n'appelle ni bloc ni fonction inconnue";
    return m.empty() ? e.message() : m;
}

// L'empreinte : chaque morceau du programme, dans l'ordre, melange a la suite.
class Fingerprint {
public:
    void add(std::uint64_t v) noexcept { h_ ^= v + 0x9E3779B97F4A7C15ULL + (h_ << 6) + (h_ >> 2); }
    void add(std::string_view s) noexcept {
        add(static_cast<std::uint64_t>(std::hash<std::string_view>{}(s)));
        add(static_cast<std::uint64_t>(s.size()));
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return h_; }
private:
    std::uint64_t h_{0xCBF29CE484222325ULL};
};

} // namespace

// ------------------------------------------------------------ points d'arret ---
void Runtime::setBreakpoints(std::vector<Breakpoint> list) {
    // Une condition deja vraie le reste : poser un autre point d'arret (ou
    // changer de programme en ligne) ne la fait pas "devenir" vraie.
    auto truth = std::move(adoptedTruth_);
    adoptedTruth_.clear();
    for (std::size_t i = 0; i < breakpoints_.size() && i < breakInfo_.size(); ++i)
        truth.push_back(ConditionTruth{breakpoints_[i].id, breakpoints_[i].condition, breakInfo_[i].trueBefore || breakInfo_[i].trueNow});
    breakpoints_ = std::move(list);
    resolveBreakpoints();
    for (std::size_t i = 0; i < breakpoints_.size() && i < breakInfo_.size(); ++i)
        for (const auto& t : truth)
            if (t.id == breakpoints_[i].id && t.condition == breakpoints_[i].condition) breakInfo_[i].trueBefore = t.wasTrue;
}

// La fin d'un cycle : ce qui etait vrai pendant ce cycle devient "d'avant".
void Runtime::rollConditionTruth() noexcept {
    for (auto& info : breakInfo_) {
        info.trueBefore = info.trueNow;
        info.trueNow = false;
    }
}

// Chacun retrouve sa section (son numero), sa ligne - la premiere instruction a
// partir de celle demandee -, sa condition (lue, puis essayee une fois dans la
// portee de la section : un nom inconnu se dit tout de suite, pas au passage).
void Runtime::resolveBreakpoints() {
    breakInfo_.assign(breakpoints_.size(), BreakInfo{});
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        auto& bp = breakpoints_[i];
        auto& info = breakInfo_[i];
        bp.effectiveLine = 0;
        bp.note.clear();
        const auto tag = tagOfSection(bp.section);
        if (tag == 0) {
            bp.note = whyNotSimulated(std::string(trimmed(bp.section)));
            continue;
        }
        const auto lines = statementLines(*tagPrograms_[tag]);
        const auto want = std::max<std::uint32_t>(1, bp.line);
        const auto at = std::lower_bound(lines.begin(), lines.end(), want);
        if (at == lines.end()) {
            bp.note = "aucune instruction \xC3\xA0 partir de la ligne " + std::to_string(want) + " : il ne s'arr\xC3\xAAtera pas";
            continue;
        }
        bp.effectiveLine = *at;
        info.tag = tag;
        if (*at != bp.line)
            bp.note = "d\xC3\xA9" "cal\xC3\xA9 \xC3\xA0 la ligne " + std::to_string(*at) + " : la ligne " + std::to_string(bp.line)
                      + " n'a pas d'instruction";
        if (!trimmed(bp.condition).empty()) {
            auto parsed = parseExpression(bp.condition);
            if (!parsed) {
                addNote(bp, "condition invalide : " + conditionProblem(parsed.error()) + " ; il ne s'arr\xC3\xAAtera pas");
                continue;
            }
            info.condition = *parsed;
            std::string scope;
            if (scopeOfTag(tag, scope)) {
                const auto saved = scopePrefix_;
                scopePrefix_ = scope;
                ++probing_;
                const auto tried = evaluate(*info.condition, *this);
                --probing_;
                scopePrefix_ = saved;
                if (!tried) {
                    info.condition.reset();
                    addNote(bp, "condition invalide : " + conditionProblem(tried.error()) + " ; il ne s'arr\xC3\xAAtera pas");
                    continue;
                }
            }
        }
        info.armed = bp.enabled;
    }
    markBreakLines();
}

// Les lignes marquees de chaque section ; aucune : un vecteur vide, que le
// runner teste sans rien faire d'autre.
void Runtime::markBreakLines() {
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> lines;
    anyArmed_ = false;
    for (std::size_t i = 0; i < breakpoints_.size() && i < breakInfo_.size(); ++i) {
        const auto& info = breakInfo_[i];
        if (!info.armed || info.tag == 0 || breakpoints_[i].effectiveLine == 0) continue;
        lines[info.tag].push_back(breakpoints_[i].effectiveLine);
        anyArmed_ = true;
    }
    static const std::vector<std::uint32_t> none;
    for (std::uint32_t t = 1; t < tagPrograms_.size(); ++t) {
        if (!tagPrograms_[t]) continue;
        const auto it = lines.find(t);
        setBreakLines(*tagPrograms_[t], it == lines.end() ? none : it->second);
    }
}

std::uint32_t Runtime::tagOfSection(std::string_view section) const {
    const auto want = lowered(trimmed(section));
    if (want.empty()) return 0;
    for (std::uint32_t t = 1; t < tagNames_.size(); ++t)
        if (tagPrograms_[t] && lowered(tagNames_[t]) == want) return t;
    // Le nom seul d'une section de DFB ("Main"), s'il n'y en a qu'une.
    std::uint32_t found = 0;
    int count = 0;
    for (std::uint32_t t = 1; t < tagNames_.size(); ++t) {
        if (!tagPrograms_[t]) continue;
        const auto& name = tagNames_[t];
        const auto dot = name.find('.');
        if (dot != std::string::npos && lowered(std::string_view(name).substr(dot + 1)) == want) {
            found = t;
            ++count;
        }
    }
    return count == 1 ? found : 0;
}

std::string Runtime::whyNotSimulated(const std::string& section) const {
    const auto want = lowered(section);
    const auto dot = want.find('.');
    for (const auto& d : prepareDiagnostics_) {
        // Les diagnostics nomment une section de tache seule ("Matrice"), un
        // corps de DFB en entier ("BLOC.Section").
        const auto said = lowered(d.section);
        if (said != want && (dot == std::string::npos || said != want.substr(dot + 1))) continue;
        if (d.severity == Diagnostic::Severity::Error)
            return "la section " + section + " ne se lit pas : il ne s'arr\xC3\xAAtera pas";
        return "la section " + section + " n'est pas en ST : le simulateur ne l'ex\xC3\xA9" "cute pas";
    }
    return "section introuvable dans " + task_ + " : " + section;
}

// La portee dans laquelle une section s'execute : son unite ("Unite."), rien
// (une section de la tache), ou une instance de son DFB (le corps d'un bloc).
bool Runtime::scopeOfTag(std::uint32_t tag, std::string& scope) const {
    for (const auto& r : programs_)
        if (r.program && programTag(*r.program) == tag) {
            scope = r.scope;
            return true;
        }
    if (tag >= tagNames_.size()) return false;
    const auto& name = tagNames_[tag];
    const auto dot = name.find('.');
    if (dot == std::string::npos) return false;
    const auto type = lowered(std::string_view(name).substr(0, dot));
    for (const auto& [instance, instanceType] : instanceTypes_)
        if (lowered(instanceType) == type) {
            scope = instance + ".";
            return true;
        }
    return false;
}

// Le runner vient d'atteindre une ligne marquee. La condition d'abord (dans la
// portee du moment : l'instance du bloc qui s'execute), puis le passage.
void Runtime::breakpointReached(std::uint32_t tag, std::uint32_t line, LineProbe& probe) {
    if (probing_ > 0) return;
    bool changedMarks = false;
    for (std::size_t i = 0; i < breakpoints_.size() && i < breakInfo_.size(); ++i) {
        auto& bp = breakpoints_[i];
        auto& info = breakInfo_[i];
        if (!info.armed || info.tag != tag || bp.effectiveLine != line) continue;
        if (info.condition) {
            ++probing_;
            const auto v = evaluate(*info.condition, *this);
            --probing_;
            if (!v) {
                // Invalide ici (un indice hors des bornes pour cette instance...) :
                // il ne s'arretera plus, et le dit.
                info.armed = false;
                info.condition.reset();
                addNote(bp, "condition invalide : " + conditionProblem(v.error()) + " ; il ne s'arr\xC3\xAAtera pas");
                changedMarks = true;
                continue;
            }
            if (!v->isTruthy()) continue;
            // Elle DEVIENT vraie : pas deja vraie a un passage de ce cycle ni du
            // cycle d'avant (la maquette : bp.prev).
            const bool becomes = !info.trueNow && !info.trueBefore;
            info.trueNow = true;
            if (!becomes) continue;
        }
        ++bp.hits;
        if (scanHit_) continue;                  // le premier du cycle seulement
        BreakHit hit;
        hit.id = bp.id;
        hit.section = tag < tagNames_.size() ? tagNames_[tag] : std::string{};
        hit.line = line;
        hit.scan = currentScan_;
        ++probing_;
        hit.values = probe.lineValues();
        --probing_;
        for (auto& [name, value] : hit.values) {
            if (value.empty()) {
                const bool aggregate = aggregates_.count(lowered(scopePrefix_ + name)) != 0 || aggregates_.count(lowered(name)) != 0;
                value = aggregate ? std::string("(structure ou tableau)") : std::string("?");
            } else if (isForced(name)) {
                value += " (forc\xC3\xA9" "e)";
            }
        }
        hit.stack = stackNow();
        scanHit_ = std::move(hit);
    }
    if (changedMarks) markBreakLines();
}

// MAST > l'unite (si l'entree en est une) > la section > chaque instance de DFB
// appelee (son type) > la section du bloc.
std::vector<std::string> Runtime::stackNow() const {
    std::vector<std::string> out{task_};
    if (currentRunnable_ < programs_.size()) {
        const auto& r = programs_[currentRunnable_];
        if (r.entry < entryNames_.size() && entryNames_[r.entry] != r.section) out.push_back(entryNames_[r.entry]);
        out.push_back(r.section);
    }
    for (const auto& f : frames_) {
        out.push_back(*f.instance + " (" + *f.type + ")");
        out.push_back(f.tag < tagNames_.size() ? tagNames_[f.tag] : std::string{});
    }
    return out;
}

std::optional<BreakHit> Runtime::takeBreakHit() {
    auto out = std::move(pendingHit_);
    pendingHit_.reset();
    return out;
}

// ----------------------------------------------------------- le pas a pas ---
std::string Runtime::nextEntry() const {
    if (!scanOpen_ || nextRunnable_ >= programs_.size()) return {};
    const auto e = programs_[nextRunnable_].entry;
    return e < entryNames_.size() ? entryNames_[e] : std::string{};
}

std::size_t Runtime::nextEntryIndex() const noexcept {
    return scanOpen_ && nextRunnable_ < programs_.size() ? programs_[nextRunnable_].entry : 0;
}

std::vector<SectionTime> Runtime::sectionTimes() const {
    std::vector<SectionTime> out;
    if (lastTimes_.size() != programs_.size()) return out;       // pas encore de cycle complet
    out.reserve(programs_.size());
    for (std::size_t i = 0; i < programs_.size(); ++i) {
        const auto& r = programs_[i];
        // 1.11 : "inactive" au meme cycle que les temps (micros a -1, runSection), pas
        // r.active : apres une halte, il dirait le cycle arrete, les temps le precedent.
        const bool active = lastTimes_[i].second >= 0;
        out.push_back(SectionTime{r.entry < entryNames_.size() ? entryNames_[r.entry] : r.section, r.section,
                                  lastTimes_[i].first, active ? lastTimes_[i].second : 0, active, r.conditionText});
    }
    return out;
}

// ------------------------------------------------ la modification en ligne ---
// Ce que prepare() lit du projet, et rien d'autre : les commentaires, les
// tables d'animation, l'IHM, les versions n'y sont pas - les changer ne
// touche pas une simulation qui tourne.
std::uint64_t Runtime::programFingerprint(const domain::Project& p) {
    Fingerprint f;
    const auto text = [&p](domain::SymbolId id) { return p.strings.text(id); };
    f.add(p.variables.size());
    for (const auto& v : p.variables) {
        f.add(text(v.name));
        f.add(text(v.type.name));
        f.add(text(v.type.elementType));
        f.add(static_cast<std::uint64_t>(v.type.klass));
        f.add(static_cast<std::uint64_t>(v.type.arrayLow));
        f.add(static_cast<std::uint64_t>(v.type.arrayHigh));
        f.add(v.type.stringLength);
        f.add(v.type.derivedIndex);
        f.add(v.type.fbTypeIndex);
        f.add(static_cast<std::uint64_t>(v.scope));
        f.add(v.owner);
        f.add(text(v.initValue));
        f.add(v.located ? 1u : 0u);
        f.add(v.address.raw);
        // 1.10.2 : le lien d'un parametre d'unite a sa globale est du programme.
        for (const auto& [key, value] : v.attributes)
            if (key == "EffectiveParameter") f.add(value);
    }
    f.add(p.derivedTypes.size());
    for (const auto& t : p.derivedTypes) {
        f.add(text(t.name));
        f.add(t.fields.size());
        for (const auto fi : t.fields) f.add(fi);
    }
    f.add(p.pous.size());
    for (const auto& pou : p.pous) {
        f.add(text(pou.name));
        f.add(static_cast<std::uint64_t>(pou.kind));
        f.add(text(pou.task));
        f.add(pou.order);
        f.add(pou.parent);
        for (const auto* list : {&pou.sections, &pou.parameters, &pou.locals, &pou.children}) {
            f.add(list->size());
            for (const auto i : *list) f.add(i);
        }
    }
    f.add(p.sections.size());
    for (const auto& s : p.sections) {
        f.add(text(s.name));
        f.add(static_cast<std::uint64_t>(s.language));
        f.add(text(s.task));
        f.add(s.order);
        f.add(s.owner);
        f.add(s.isSubroutine ? 1u : 0u);
        f.add(text(s.activationCondition));
        f.add(s.body);
    }
    f.add(p.tasks.size());
    for (const auto& t : p.tasks) {
        f.add(text(t.name));
        f.add(t.type);
        f.add(t.period);
    }
    const auto& m = p.hardware.memory;
    f.add(m.declared ? 1u : 0u);
    f.add(m.internalBits);
    f.add(m.internalWords);
    return f.value();
}

Adoption Runtime::adoptStateFrom(const Runtime& previous) {
    Adoption a;
    clockMs_ = previous.clockMs_;
    deltaMs_ = previous.deltaMs_;
    scans_ = previous.scans_;
    currentScan_ = previous.scans_;
    continueOnUnknown_ = previous.continueOnUnknown_;
    maxScanStatementsSetting_ = previous.maxScanStatementsSetting_;
    maxScanMsSetting_ = previous.maxScanMsSetting_;
    historyDepth_ = previous.historyDepth_;
    unknownCalls_ = previous.unknownCalls_;
    memoryWarnings_ = previous.memoryWarnings_;

    // Les sections de l'ancien programme, par leur nom dans le nouveau : qui a
    // ecrit une case gardee le dit encore (une section disparue garde son nom).
    std::vector<std::uint32_t> tagMap(previous.tagNames_.size(), 0);
    for (std::size_t t = 1; t < previous.tagNames_.size(); ++t) {
        const auto& name = previous.tagNames_[t];
        const auto it = std::find(tagNames_.begin() + 1, tagNames_.end(), name);
        if (it != tagNames_.end()) {
            tagMap[t] = static_cast<std::uint32_t>(it - tagNames_.begin());
        } else {
            tagMap[t] = static_cast<std::uint32_t>(tagNames_.size());
            tagNames_.push_back(name);
            tagPrograms_.push_back(nullptr);
        }
    }
    const auto carry = [&tagMap](Slot& to, const Slot& from) {
        to.value = from.value;
        to.written = from.written;
        to.forced = from.forced;
        to.forcedValue = from.forcedValue;
        to.writerTag = from.writerTag < tagMap.size() ? tagMap[from.writerTag] : 0;
        to.writerLine = from.writerLine;
        to.writerScan = from.writerScan;
    };

    // Chaque case du nouveau programme : de meme nom (la casse n'y fait rien,
    // comme en ST) et de meme type, elle reprend tout ; sinon elle part de sa
    // valeur initiale (un forcage la suit quand meme, converti).
    std::unordered_set<const Slot*> taken;
    taken.reserve(previous.slots_.size());
    for (auto& [name, slot] : slots_) {
        const Slot* before = nullptr;
        if (const auto it = previous.slots_.find(name); it != previous.slots_.end()) {
            before = &it->second;
        } else if (const auto alias = previous.canonical_.find(lowered(name)); alias != previous.canonical_.end()) {
            if (const auto again = previous.slots_.find(alias->second); again != previous.slots_.end()) before = &again->second;
        }
        if (before && before->value.type() == slot.value.type()) {
            carry(slot, *before);
            taken.insert(before);
            ++a.kept;
            continue;
        }
        ++a.added;
        if (before && before->forced) {
            slot.forced = true;
            slot.forcedValue = Value::defaultOf(slot.value.type());
            slot.forcedValue.assignFrom(before->forcedValue);
        }
    }
    // Ce que l'ancien avait et que le nouveau n'a pas : une adresse ou une
    // variable de boucle que le programme avait creee revient telle quelle
    // (le programme la recreerait au premier acces - avec zero) ; le reste a
    // disparu du programme.
    for (const auto& [name, slot] : previous.slots_) {
        if (taken.count(&slot) != 0) continue;
        if (slot.dynamic && slots_.find(name) == slots_.end() && canonical_.find(lowered(name)) == canonical_.end()) {
            declare(name, slot.value.type(), /*dynamic*/ true);
            if (const auto it = slots_.find(name); it != slots_.end()) {
                carry(it->second, slot);
                ++a.kept;
            }
            continue;
        }
        ++a.dropped;
    }
    // Les temporisations et compteurs en cours (TON, CTU... : leur etat).
    for (auto& [name, state] : blocks_)
        if (const auto it = previous.blocks_.find(name); it != previous.blocks_.end() && it->second.type == state.type)
            state = it->second;
    // Les courbes suivies, avec leurs points.
    for (const auto& [name, samples] : previous.history_)
        if (known(name)) history_[name] = samples;
    // Les conditions deja vraies (setBreakpoints les reprend).
    adoptedTruth_.clear();
    for (std::size_t i = 0; i < previous.breakpoints_.size() && i < previous.breakInfo_.size(); ++i)
        adoptedTruth_.push_back(ConditionTruth{previous.breakpoints_[i].id, previous.breakpoints_[i].condition,
                                               previous.breakInfo_[i].trueBefore || previous.breakInfo_[i].trueNow});
    clearResolveCache();
    return a;
}

void Runtime::adoptForcingFrom(const Runtime& previous) {
    for (const auto& [name, slot] : previous.slots_)
        if (slot.forced) (void)force(name, slot.forcedValue);
}

std::vector<Diagnostic> Runtime::newPreparationErrors(const Runtime& previous) const {
    std::vector<Diagnostic> out;
    for (const auto& d : prepareDiagnostics_) {
        if (d.severity != Diagnostic::Severity::Error) continue;
        const bool before = std::any_of(previous.prepareDiagnostics_.begin(), previous.prepareDiagnostics_.end(), [&d](const Diagnostic& o) {
            return o.severity == d.severity && o.section == d.section && o.message == d.message;
        });
        if (!before) out.push_back(d);
    }
    return out;
}

} // namespace sim
