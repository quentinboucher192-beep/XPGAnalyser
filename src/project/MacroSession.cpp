// =============================================================================
//  project/MacroSession.cpp - les tours d'apercu d'un lancement
// =============================================================================
#include "MacroSession.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

namespace project::macro {

MacroSession::MacroSession(ProjectPtr project, std::string libsRoot, std::string name, std::string source)
    : project_(std::move(project)),
      library_(std::make_unique<SharedLibrary>(std::move(libsRoot))),
      name_(std::move(name)),
      source_(std::move(source)),
      spec_(parseMacroSpec(source_, name_)) {
    (void)library_->scan();
}

MacroSession::~MacroSession() = default;

void MacroSession::setAnswer(const std::string& key, std::string value) {
    answers_[key] = std::move(value);
    remembered_.erase(key);
}

void MacroSession::clearAnswer(const std::string& key) {
    answers_.erase(key);
    remembered_.erase(key);
}

void MacroSession::preload(const Answers& remembered) {
    for (const auto& [k, v] : remembered) {
        if (k == "confirm") continue;
        answers_[k] = v;
        remembered_.insert(k);
    }
}

MacroSession::Answers MacroSession::effectiveAnswers() const {
    Answers all = answers_;
    for (const auto& [k, v] : proposed_) all.emplace(k, v);
    all["confirm"] = "O";
    return all;
}

std::string MacroSession::setTable(const std::string& table, const std::string& path) {
    if (path.empty()) {
        tables_.erase(table);
        tablePaths_.erase(table);
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return "fichier introuvable : " + path;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return "fichier vide : " + path;
    tables_[table] = std::move(bytes);
    tablePaths_[table] = path;
    return {};
}

FieldSpec MacroSession::specFor(const MacroQuestion& q) const {
    FieldSpec f;
    if (const auto* declared = spec_.field(q.key)) f = *declared;
    else {
        f.key = q.key;
        switch (q.kind) {
            case MacroQuestion::Kind::Text: f.kind = FieldKind::Text; break;
            case MacroQuestion::Kind::Number: f.kind = FieldKind::Number; break;
            case MacroQuestion::Kind::YesNo: f.kind = FieldKind::YesNo; break;
            case MacroQuestion::Kind::Choice: {
                const bool yesNo = q.choices.size() == 2
                    && ((q.choices[0] == "O" && q.choices[1] == "N") || (q.choices[0] == "N" && q.choices[1] == "O"));
                f.kind = yesNo ? FieldKind::YesNo : FieldKind::Choice;
                break;
            }
        }
    }
    // Ce que la macro dit au moment de poser la question complete la ligne.
    if (q.kind == MacroQuestion::Kind::Number && !f.hasRange && q.maximum > q.minimum) {
        f.hasRange = true;
        f.minimum = q.minimum;
        f.maximum = q.maximum;
    }
    if ((f.kind == FieldKind::Choice || f.kind == FieldKind::YesNo) && !q.choices.empty()) {
        // Les valeurs viennent de l'appel ; les libelles de la ligne #!.
        std::vector<FieldOption> options;
        for (const auto& c : q.choices) {
            FieldOption o{c, c};
            if (const auto label = f.optionLabel(c); !label.empty()) o.label = label;
            options.push_back(std::move(o));
        }
        f.options = std::move(options);
    }
    if (f.kind == FieldKind::YesNo && f.options.empty()) f.options = {{"O", "oui"}, {"N", "non"}};
    return f;
}

const MacroSession::Outcome& MacroSession::refresh(bool exact) {
    const auto t0 = std::chrono::steady_clock::now();
    Outcome out;
    Answers proposed;
    MacroReport last;
    const auto hasErrors = [](const MacroReport& r) {
        return std::any_of(r.diagnostics.begin(), r.diagnostics.end(),
                           [](const sim::Diagnostic& d) { return d.severity == sim::Diagnostic::Severity::Error; });
    };
    for (std::size_t round = 1; round <= 8; ++round) {
        MacroRunner runner(project_, library_.get());
        for (const auto& [n, csv] : tables_) runner.addTable(n, csv);
        Answers all = answers_;
        for (const auto& [k, v] : proposed) all.emplace(k, v);
        all["confirm"] = "O";
        runner.setAnswers(all);
        last = runner.run(source_, name_, MacroMode::Preview);
        out.rounds = round;
        if (!last.needsAnswers) {
            out.complete = last.ok;
            break;
        }
        bool added = false;
        for (const auto& q : last.questions) {
            if (q.key == "confirm" || answers_.count(q.key) || proposed.count(q.key)) continue;
            proposed[q.key] = q.preset;
            added = true;
        }
        const bool wentThrough = !last.stopped && last.failure.empty() && !hasErrors(last);
        if (wentThrough && !exact) {
            // Alle au bout, chaque question sans reponse ayant rendu sa valeur
            // proposee : c'est exactement l'apercu avec ces valeurs.
            last.ok = true;
            last.needsAnswers = false;
            last.questions.clear();
            out.complete = true;
            break;
        }
        if (!added) {
            out.stopped = last.stopped;
            break;
        }
    }
    // Les valeurs proposees de ce qui a ete atteint (une branche ecartee ne
    // laisse pas sa valeur derriere elle).
    proposed_.clear();
    for (const auto& q : last.reached) {
        if (q.key == "confirm") {
            out.confirm = q.prompt;
            continue;
        }
        Field f;
        f.question = q;
        f.spec = specFor(q);
        f.label = labelFor(f.spec, q.key, q.prompt);
        f.group = spec_.groupOf(q.key);
        f.advanced = spec_.isAdvanced(q.key);
        if (const auto a = answers_.find(q.key); a != answers_.end()) {
            f.value = a->second;
            f.origin = remembered_.count(q.key) ? Field::Origin::Remembered : Field::Origin::Typed;
        } else {
            const auto p = proposed.find(q.key);
            f.value = p != proposed.end() ? p->second : q.preset;
            proposed_[q.key] = f.value;
        }
        out.fields.push_back(std::move(f));
    }
    out.failure = last.failure;
    if (out.failure.empty() && hasErrors(last))
        for (const auto& d : last.diagnostics)
            if (d.severity == sim::Diagnostic::Severity::Error) {
                out.failure = d.message;
                break;
            }
    out.report = std::move(last);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    outcome_ = std::move(out);
    return outcome_;
}

const MacroSession::Field* MacroSession::field(const std::string& key) const {
    for (const auto& f : outcome_.fields)
        if (f.question.key == key) return &f;
    return nullptr;
}

core::CommandPtr MacroSession::apply(MacroReport& report) {
    MacroRunner runner(project_, library_.get());
    for (const auto& [n, csv] : tables_) runner.addTable(n, csv);
    runner.setAnswers(effectiveAnswers());
    report = runner.run(source_, name_, MacroMode::Apply);
    if (!report.ok) return nullptr;
    return runner.takeCommand();
}

std::string MacroSession::applyText() const {
    return applyTextFor(spec_, effectiveAnswers(), outcome_.confirm);
}

} // namespace project::macro
