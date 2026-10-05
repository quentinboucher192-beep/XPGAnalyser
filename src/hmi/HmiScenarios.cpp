#include "HmiScenarios.hpp"

#include "HmiCheck.hpp"
#include "HmiExpr.hpp"
#include "HmiLanguages.hpp"
#include "HmiRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace hmi {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Sans casse ni accents (e accentue -> e, a accentue -> a...), les blancs reduits.
std::string folded(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            ++i;
            if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) out += 'e';
            else if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) out += 'a';
            else if ((d >= 0xB9 && d <= 0xBC) || (d >= 0x99 && d <= 0x9C)) out += 'u';
            else if ((d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F)) out += 'i';
            else if ((d >= 0xB2 && d <= 0xB6) || (d >= 0x92 && d <= 0x96)) out += 'o';
            else if (d == 0xA7 || d == 0x87) out += 'c';
            continue;
        }
        if (std::isspace(c)) {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool same(std::string_view a, std::string_view b) { return folded(a) == folded(b); }

std::string seconds(double s) {
    char b[32];
    std::snprintf(b, sizeof b, s < 10 ? "%.2f s" : "%.1f s", s);
    std::string out = b;
    for (auto& c : out) if (c == '.') c = ',';
    return out;
}

std::string quoted(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

struct KindName { StepKind kind; const char* label; };
constexpr KindName kKinds[] = {
    {StepKind::OpenView, "Ouvrir la vue"},
    {StepKind::Click, "Cliquer"},
    {StepKind::Type, "Saisir"},
    {StepKind::Write, "\xC3\x89" "crire"},
    {StepKind::Wait, "Attendre"},
    {StepKind::WaitUntil, "Attendre que"},
    {StepKind::Check, "V\xC3\xA9rifier"},
    {StepKind::Login, "Se connecter"},
    {StepKind::Logout, "Se d\xC3\xA9" "connecter"},
    {StepKind::Sign, "Signer"},
    {StepKind::Acknowledge, "Acquitter"},
    {StepKind::Comment, "Commentaire"},
};

// Les entrees du journal depuis une marque (la derniere entree vue).
struct Mark {
    double      time{-1};
    std::string message, source;
};
Mark markOf(const Runtime& rt) {
    Mark m;
    if (!rt.journal().empty()) {
        const auto& e = rt.journal().back();
        m.time = e.time;
        m.message = e.message;
        m.source = e.source;
    }
    return m;
}
std::vector<const JournalEntry*> since(const Runtime& rt, const Mark& m) {
    std::vector<const JournalEntry*> out;
    const auto& j = rt.journal();
    for (auto it = j.rbegin(); it != j.rend(); ++it) {
        if (m.time >= 0 && it->time == m.time && it->message == m.message && it->source == m.source) break;
        out.push_back(&*it);
    }
    std::reverse(out.begin(), out.end());
    return out;
}
// Un refus ou une erreur de l'IHM, dans ce que le pas a laisse au journal.
const JournalEntry* refusal(const std::vector<const JournalEntry*>& entries) {
    for (const auto* e : entries) {
        const std::string k = folded(e->kind);
        if (k == "erreur" || k.find("refus") != std::string::npos) return e;
    }
    return nullptr;
}

// Un objet a l'ecran : "Objet" (la popup du dessus d'abord, puis la vue) ou
// "Vue.Objet".
const Object* shownObject(const Runtime& rt, std::string_view target, Id* viewOut) {
    std::vector<Id> shown(rt.popups().rbegin(), rt.popups().rend());
    shown.push_back(rt.currentView());
    const std::string t = trim(target);
    if (const auto dot = t.find('.'); dot != std::string::npos) {
        const std::string viewName = t.substr(0, dot), rest = t.substr(dot + 1);
        for (const Id id : shown)
            if (const View* v = rt.composedView(id); v && same(v->name, viewName))
                if (const Object* o = v->objectByName(rest)) {
                    if (viewOut) *viewOut = id;
                    return o;
                }
    }
    for (const Id id : shown)
        if (const View* v = rt.composedView(id))
            if (const Object* o = v->objectByName(t)) {
                if (viewOut) *viewOut = id;
                return o;
            }
    return nullptr;
}

// Une valeur lue contre la valeur attendue : les nombres en nombres, les
// booleens en booleens (TRUE, VRAI, 1), le texte sans casse et sans apostrophes.
bool matches(const sim::Value& v, std::string_view expectedRaw) {
    std::string expected = trim(expectedRaw);
    if (expected.size() >= 2 && (expected.front() == '\'' || expected.front() == '"') && expected.back() == expected.front())
        expected = expected.substr(1, expected.size() - 2);
    const std::string actual = trim(formatValue(v));
    const std::string e = folded(expected);
    if (v.type() == sim::Type::Bool) {
        const bool truthy = v.isTruthy();
        if (e == "true" || e == "vrai" || e == "1") return truthy;
        if (e == "false" || e == "faux" || e == "0") return !truthy;
    }
    double a = 0, b = 0;
    if (parseNumber(actual, a) && parseNumber(expected, b)) return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b));
    std::string plain = actual;
    if (plain.size() >= 2 && plain.front() == '\'' && plain.back() == '\'') plain = plain.substr(1, plain.size() - 2);
    return folded(plain) == e;
}

// "chef + admin" -> {chef, admin} ; "1234 ; admin2026" -> {1234, admin2026}.
std::pair<std::string, std::string> splitTwo(std::string_view s, char sep) {
    const auto at = s.find(sep);
    if (at == std::string_view::npos) return {trim(s), {}};
    return {trim(s.substr(0, at)), trim(s.substr(at + 1))};
}

} // namespace

// =================================================================== les pas ===
const std::vector<std::string>& stepActions() {
    static const std::vector<std::string> list = [] {
        std::vector<std::string> out;
        for (const auto& k : kKinds) out.emplace_back(k.label);
        return out;
    }();
    return list;
}

StepKind stepKind(std::string_view action) {
    const std::string a = folded(action);
    for (const auto& k : kKinds)
        if (folded(k.label) == a) return k.kind;
    if (a == "ouvrir" || a == "vue" || a == "naviguer") return StepKind::OpenView;
    if (a == "clic" || a == "toucher") return StepKind::Click;
    if (a == "verification" || a == "controler") return StepKind::Check;
    return StepKind::Unknown;
}

std::string stepLabel(StepKind k) {
    for (const auto& x : kKinds)
        if (x.kind == k) return x.label;
    return "?";
}

StepFields stepFields(StepKind k) {
    switch (k) {
        case StepKind::OpenView:
            return {"la vue (ou la popup) \xC3\xA0 ouvrir", "", ""};
        case StepKind::Click:
            return {"l'objet \xC3\xA0 l'\xC3\xA9" "cran : Objet, ou Vue.Objet",
                    "facultatif : une partie (option:2, case:1,10, suivant...) ; un s\xC3\xA9lecteur de langue : la langue (en)", ""};
        case StepKind::Type:
            return {"le champ de saisie", "le texte tap\xC3\xA9 (puis Entr\xC3\xA9" "e)", ""};
        case StepKind::Write:
            return {"la variable (IHM ou automate)", "la valeur : une expression (12.5, TRUE, 'Azote', Consigne + 1)", ""};
        case StepKind::Wait:
            return {"", "la dur\xC3\xA9" "e, en ms", ""};
        case StepKind::WaitUntil:
            return {"", "la condition attendue (Vanne_Purge AND Pression > 3)", "le d\xC3\xA9lai maximal, en ms (5000 par d\xC3\xA9" "faut)"};
        case StepKind::Check:
            return {"", "l'expression lue (Vanne_Purge, SYS.CurrentView, Pression * 2)",
                    "la valeur attendue (TRUE, 12.5, 'Vue_Commandes') ; vide : l'expression doit \xC3\xAAtre vraie"};
        case StepKind::Login:
            return {"l'identifiant", "le mot de passe (ou le code)", ""};
        case StepKind::Logout:
            return {"", "", ""};
        case StepKind::Sign:
            return {"le signataire, vide : l'utilisateur connect\xC3\xA9 ; double : signataire + visa (chef + admin)",
                    "le mot de passe ; double : mot de passe ; mot de passe du visa", "le motif (dans la liste, ou tap\xC3\xA9)"};
        case StepKind::Acknowledge:
            return {"l'alarme, un groupe (groupe:Armoire A) ; vide : toutes", "", ""};
        case StepKind::Comment:
            return {"", "", ""};
        case StepKind::Unknown:
            break;
    }
    return {};
}

std::string describeStep(const TestStep& s) {
    const StepKind k = stepKind(s.action);
    const std::string t = trim(s.target), v = trim(s.value), e = trim(s.expected);
    switch (k) {
        case StepKind::OpenView: return "Ouvrir la vue " + t;
        case StepKind::Click: return "Cliquer " + t + (v.empty() ? std::string{} : " (" + v + ")");
        case StepKind::Type: return "Saisir " + quoted(v) + " dans " + t;
        case StepKind::Write: return "\xC3\x89" "crire " + t + " := " + v;
        case StepKind::Wait: return "Attendre " + v + " ms";
        case StepKind::WaitUntil: return "Attendre que " + v + (e.empty() ? std::string{} : " (au plus " + e + " ms)");
        case StepKind::Check: return "V\xC3\xA9rifier " + v + (e.empty() ? std::string{} : " = " + e);
        case StepKind::Login: return "Se connecter " + t + (v.empty() ? std::string{} : " (****)");
        case StepKind::Logout: return "Se d\xC3\xA9" "connecter";
        case StepKind::Sign: return "Signer" + (t.empty() ? std::string{} : " " + t) + (e.empty() ? std::string{} : " (motif " + quoted(e) + ")");
        case StepKind::Acknowledge: return "Acquitter " + (t.empty() || t == "*" ? std::string("toutes les alarmes") : t);
        case StepKind::Comment: return s.note.empty() ? std::string("Commentaire") : s.note;
        case StepKind::Unknown: break;
    }
    return s.action + " " + t;
}

std::string_view verdictLabel(Verdict v) noexcept {
    switch (v) {
        case Verdict::Pending: return "en attente";
        case Verdict::Passed:  return "r\xC3\xA9ussi";
        case Verdict::Failed:  return "\xC3\xA9" "chec";
        case Verdict::Error:   return "erreur";
        case Verdict::Skipped: return "non jou\xC3\xA9";
        case Verdict::Info:    return "\xE2\x80\x94";
    }
    return "?";
}

std::size_t ScenarioReport::count(Verdict v) const {
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [v](const StepResult& r) { return r.verdict == v; }));
}

bool ScenarioReport::ok() const { return done && !stopped && count(Verdict::Failed) == 0 && count(Verdict::Error) == 0; }

std::string ScenarioReport::summary() const {
    const std::size_t played = steps.size() - count(Verdict::Pending);
    if (!done) return "en cours : pas " + std::to_string(std::min(played + 1, steps.size())) + " / " + std::to_string(steps.size());
    std::string s = std::to_string(steps.size()) + " pas : " + std::to_string(count(Verdict::Passed)) + " r\xC3\xA9ussi(s)";
    if (const auto f = count(Verdict::Failed)) s += ", " + std::to_string(f) + " \xC3\xA9" "chec(s)";
    if (const auto e = count(Verdict::Error)) s += ", " + std::to_string(e) + " erreur(s)";
    if (const auto k = count(Verdict::Skipped)) s += ", " + std::to_string(k) + " non jou\xC3\xA9(s)";
    if (stopped) s += " (arr\xC3\xAAt\xC3\xA9)";
    return s;
}

// ================================================================= le moteur ===
ScenarioRun::ScenarioRun(const TestScenario& sc, Runtime& rt, double now, double pace, bool stopOnFailure)
    : scenario_(sc), rt_(rt), start_(now), next_(now), pace_(std::max(0.0, pace)), stopOnFailure_(stopOnFailure) {
    report_.scenario = sc.id;
    report_.name = sc.name;
    report_.started = rt.dateStampOf(now).substr(0, 19);
    report_.steps.resize(sc.steps.size());
    if (sc.steps.empty()) report_.done = true;
}

void ScenarioRun::stop(double now) {
    if (report_.done) return;
    for (std::size_t i = index_; i < report_.steps.size(); ++i)
        if (report_.steps[i].verdict == Verdict::Pending) report_.steps[i].verdict = Verdict::Skipped;
    waiting_ = false;
    report_.stopped = true;
    report_.done = true;
    report_.seconds = now - start_;
}

void ScenarioRun::finish(Verdict v, std::string detail, double now) {
    if (index_ >= report_.steps.size()) return;
    auto& r = report_.steps[index_];
    r.verdict = v;
    r.detail = std::move(detail);
    r.seconds = now - start_ - r.at;
    ++index_;
    if (next_ < now + pace_) next_ = now + pace_;
    if (stopOnFailure_ && (v == Verdict::Failed || v == Verdict::Error)) {
        for (std::size_t i = index_; i < report_.steps.size(); ++i) report_.steps[i].verdict = Verdict::Skipped;
        index_ = report_.steps.size();
    }
    if (index_ >= report_.steps.size()) {
        report_.done = true;
        report_.seconds = now - start_;
    }
}

bool ScenarioRun::advance(double now) {
    if (report_.done) return true;
    report_.seconds = now - start_;
    if (waiting_) {
        const TestStep& st = scenario_.steps[index_];
        const auto e = Expression::compile(st.value);
        if (!e.valid()) {
            waiting_ = false;
            finish(Verdict::Error, "condition illisible : " + e.error(), now);
            return report_.done;
        }
        auto r = e.evaluate(rt_.environment());
        if (!r) {
            waiting_ = false;
            finish(Verdict::Error, "condition illisible : " + r.error().message(), now);
        } else if (r->isTruthy()) {
            waiting_ = false;
            finish(Verdict::Passed, "vraie apr\xC3\xA8s " + seconds(now - start_ - report_.steps[index_].at), now);
        } else if (now >= deadline_) {
            waiting_ = false;
            finish(Verdict::Failed, "toujours fausse apr\xC3\xA8s " + seconds(now - start_ - report_.steps[index_].at), now);
        }
        return report_.done;
    }
    if (now + 1e-9 < next_) return false;
    if (index_ >= scenario_.steps.size()) {
        report_.done = true;
        return true;
    }
    execute(now);
    return report_.done;
}

void ScenarioRun::execute(double now) {
    const TestStep& st = scenario_.steps[index_];
    report_.steps[index_].at = now - start_;
    const StepKind kind = stepKind(st.action);
    const std::string target = trim(st.target), value = trim(st.value), expected = trim(st.expected);
    const Mark mark = markOf(rt_);
    const auto refusedSince = [&]() -> std::string {
        if (const JournalEntry* e = refusal(since(rt_, mark))) return e->message.empty() ? e->kind : e->message;
        return {};
    };
    switch (kind) {
        case StepKind::OpenView: {
            const View* v = rt_.project() ? rt_.project()->viewByName(target) : nullptr;
            if (!v) return finish(Verdict::Error, "vue " + quoted(target) + " introuvable", now);
            Transition t;
            t.kind = TransitionKind::Instant;
            t.durationMs = 0;
            const bool popup = v->role == "popup";
            const bool ok = popup ? rt_.openPopup(v->id, t, now) : rt_.navigate(v->id, t, now);
            const bool shown = popup ? std::find(rt_.popups().begin(), rt_.popups().end(), v->id) != rt_.popups().end()
                                     : rt_.currentView() == v->id;
            if (ok && shown) return finish(Verdict::Passed, v->name + (popup ? " ouverte" : " \xC3\xA0 l'\xC3\xA9" "cran"), now);
            const std::string why = refusedSince();
            return finish(Verdict::Failed, why.empty() ? std::string("la vue ne s'est pas ouverte") : why, now);
        }
        case StepKind::Click: {
            Id view = kNoId;
            const Object* o = shownObject(rt_, target, &view);
            if (!o) return finish(Verdict::Error, "objet " + quoted(target) + " absent de l'\xC3\xA9" "cran", now);
            const Id id = o->id;
            touched_ = id;
            touchedAt_ = now;
            // Un clic comme a l'ecran : les objets a champ (saisie) ou a bouton
            // (deconnexion) passent par leur partie ; une partie donnee aussi.
            std::string part = !value.empty() ? value
                             : o->kind == Kind::InputField ? std::string("champ")
                             : o->kind == Kind::LogoutButton ? std::string("bouton")
                             : o->kind == Kind::LanguageSelector ? std::string("langue:suivante") : std::string{};
            // Lot 13 : un selecteur de langue se clique aussi par la langue (en, English).
            if (o->kind == Kind::LanguageSelector && part.rfind("langue:", 0) != 0 && rt_.project()) {
                const auto choices = languageChoices(*o, rt_.project()->languages);
                for (std::size_t i = 0; i < choices.size(); ++i)
                    if (same(choices[i].code, part) || same(choices[i].name, part)) part = "langue:" + std::to_string(i);
                if (part.rfind("langue:", 0) != 0) return finish(Verdict::Error, "langue " + quoted(value) + " absente du s\xC3\xA9lecteur", now);
            }
            if (!part.empty()) {
                rt_.objectPart(id, part, now);
            } else {
                rt_.press(id, now);
                rt_.release(id, now, true);
            }
            if (const std::string why = refusedSince(); !why.empty()) return finish(Verdict::Failed, why, now);
            if (const FormState* f = rt_.formState(id); f && f->error && !f->message.empty()) return finish(Verdict::Failed, f->message, now);
            if (rt_.signatureShown()) return finish(Verdict::Passed, "signature demand\xC3\xA9" "e (pas suivant : Signer)", now);
            return finish(Verdict::Passed, {}, now);
        }
        case StepKind::Type: {
            Id view = kNoId;
            const Object* o = shownObject(rt_, target, &view);
            if (!o) return finish(Verdict::Error, "champ " + quoted(target) + " absent de l'\xC3\xA9" "cran", now);
            const Id id = o->id;
            touched_ = id;
            touchedAt_ = now;
            if (o->kind != Kind::InputField) return finish(Verdict::Error, quoted(target) + " n'est pas un champ de saisie", now);
            rt_.objectPart(id, "champ", now);                 // le focus, comme un toucher
            if (const std::string why = refusedSince(); !why.empty()) return finish(Verdict::Failed, why, now);
            rt_.typeText(value, now);
            rt_.typeKey(EditKey::Enter, now);
            if (const std::string why = refusedSince(); !why.empty()) return finish(Verdict::Failed, why, now);
            if (const FormState* f = rt_.formState(id); f && f->error && !f->message.empty()) return finish(Verdict::Failed, f->message, now);
            if (rt_.signatureShown()) return finish(Verdict::Passed, "signature demand\xC3\xA9" "e (pas suivant : Signer)", now);
            return finish(Verdict::Passed, quoted(value) + " valid\xC3\xA9", now);
        }
        case StepKind::Write: {
            const auto e = Expression::compile(value);
            if (!e.valid()) return finish(Verdict::Error, "valeur illisible : " + e.error(), now);
            auto v = e.evaluate(rt_.environment());
            if (!v) return finish(Verdict::Error, "valeur illisible : " + v.error().message(), now);
            if (!rt_.environment().write(target, *v)) return finish(Verdict::Error, "variable " + quoted(target) + " inconnue ou en lecture seule", now);
            return finish(Verdict::Passed, target + " = " + formatValue(*v), now);
        }
        case StepKind::Wait: {
            double ms = 0;
            if (!parseNumber(value, ms) || ms < 0) return finish(Verdict::Error, "dur\xC3\xA9" "e illisible : " + quoted(value), now);
            next_ = now + ms / 1000.0;
            const std::size_t at = index_;
            finish(Verdict::Passed, std::to_string(static_cast<long long>(std::llround(ms))) + " ms", now);
            report_.steps[at].seconds = ms / 1000.0;          // la duree de l'attente, pas celle de l'ordre
            return;
        }
        case StepKind::WaitUntil: {
            double ms = 5000;
            if (!expected.empty() && (!parseNumber(expected, ms) || ms < 0))
                return finish(Verdict::Error, "d\xC3\xA9lai illisible : " + quoted(expected), now);
            waiting_ = true;
            deadline_ = now + ms / 1000.0;
            (void)advance(now);           // deja vraie ?
            return;
        }
        case StepKind::Check: {
            const auto e = Expression::compile(value);
            if (!e.valid()) return finish(Verdict::Error, "expression illisible : " + e.error(), now);
            auto v = e.evaluate(rt_.environment());
            if (!v) return finish(Verdict::Error, v.error().message(), now);
            const std::string shown = formatValue(*v);
            if (expected.empty())
                return v->isTruthy() ? finish(Verdict::Passed, value + " vraie", now) : finish(Verdict::Failed, value + " = " + shown + " (fausse)", now);
            return matches(*v, expected) ? finish(Verdict::Passed, value + " = " + shown, now)
                                         : finish(Verdict::Failed, value + " = " + shown + ", attendu " + expected, now);
        }
        case StepKind::Login: {
            std::string why;
            if (rt_.login(target, value, now, &why)) return finish(Verdict::Passed, target + " connect\xC3\xA9", now);
            return finish(Verdict::Failed, why.empty() ? std::string("connexion refus\xC3\xA9" "e") : why, now);
        }
        case StepKind::Logout: {
            const std::string who = rt_.userLogin();
            rt_.logout(now, "essai " + scenario_.name);
            return finish(Verdict::Passed, who.empty() ? std::string("personne n'\xC3\xA9tait connect\xC3\xA9") : who + " d\xC3\xA9" "connect\xC3\xA9", now);
        }
        case StepKind::Sign: {
            if (!rt_.signatureShown()) return finish(Verdict::Failed, "aucune signature demand\xC3\xA9" "e", now);
            const auto [signer, visa] = splitTwo(target, '+');
            const auto [pwd, visaPwd] = splitTwo(st.value, ';');
            // Le signataire : aux fleches, quand personne n'est connecte.
            if (!signer.empty() && rt_.userLogin().empty()) {
                for (int k = 0; k < 64; ++k) {
                    const User* s = rt_.signatureSigner();
                    if (s && same(s->login, signer)) break;
                    rt_.signaturePart("signataire:suivant", now);
                }
                const User* s = rt_.signatureSigner();
                if (!s || !same(s->login, signer)) return finish(Verdict::Error, "signataire " + quoted(signer) + " introuvable", now);
            }
            rt_.signaturePart("champ:motdepasse", now);
            rt_.typeText(pwd, now);
            const SignatureRequest* rq = rt_.signatureRequest();
            if (rq && !expected.empty()) {
                if (!rq->reasons.empty()) {
                    for (std::size_t k = 0; k < rq->reasons.size() && !same(rq->reasons[rq->reason], expected); ++k)
                        rt_.signaturePart("motif:suivant", now);
                    rq = rt_.signatureRequest();
                    if (!rq || !same(rq->reasons[rq->reason], expected))
                        return finish(Verdict::Error, "motif " + quoted(expected) + " absent de la liste", now);
                } else {
                    rt_.signaturePart("champ:motif", now);
                    rt_.typeText(expected, now);
                }
            }
            rq = rt_.signatureRequest();
            if (rq && rq->twoSigners) {
                if (!visa.empty()) {
                    for (int k = 0; k < 64; ++k) {
                        const User* s = rt_.signatureVisa();
                        if (s && same(s->login, visa)) break;
                        rt_.signaturePart("visa:suivant", now);
                    }
                    const User* s = rt_.signatureVisa();
                    if (!s || !same(s->login, visa)) return finish(Verdict::Error, "visa " + quoted(visa) + " introuvable", now);
                }
                rt_.signaturePart("champ:visa", now);
                rt_.typeText(visaPwd, now);
            }
            rt_.signaturePart("bouton:signer", now);
            if (!rt_.signatureShown()) return finish(Verdict::Passed, "sign\xC3\xA9 : " + rt_.lastSignature(), now);
            const std::string why = rt_.signatureForm().message;
            return finish(Verdict::Failed, why.empty() ? std::string("signature refus\xC3\xA9" "e") : why, now);
        }
        case StepKind::Acknowledge: {
            std::string why;
            const std::size_t n = rt_.acknowledge(target.empty() ? std::string_view("*") : std::string_view(target), now, &why);
            if (n > 0) return finish(Verdict::Passed, std::to_string(n) + " alarme(s) acquitt\xC3\xA9" "e(s)", now);
            return finish(Verdict::Failed, why.empty() ? std::string("rien \xC3\xA0 acquitter") : why, now);
        }
        case StepKind::Comment:
            return finish(Verdict::Info, st.note, now);
        case StepKind::Unknown:
            break;
    }
    finish(Verdict::Error, "action inconnue : " + quoted(st.action), now);
}

ScenarioReport runScenario(const Project& p, const TestScenario& sc, sim::Environment* plc, double pace, double limitS) {
    Runtime rt;
    rt.bind(&p, plc);
    rt.start(0.0);
    ScenarioRun run(sc, rt, 0.0, pace);
    double t = 0;
    while (!run.advance(t) && t < limitS) {
        t += 0.05;
        rt.tick(t);
    }
    if (!run.done()) run.stop(t);
    return run.report();
}

// ================================================================ le rapport ===
namespace {
const std::string kVisaLine = "Jou\xC3\xA9 par : ______________________________    Le : ________________    "
                              "Visa : ______________________________";
} // namespace

ExportTable reportTable(const TestScenario& sc, const ScenarioReport& rep) {
    ExportTable t;
    t.title = "Essai " + sc.name + " : " + std::string(!rep.done ? "en cours" : rep.ok() ? "r\xC3\xA9ussi" : "\xC3\xA9" "chec");
    t.subtitle = rep.started + " - " + rep.summary() + " - " + seconds(rep.seconds);
    t.headers = {"N\xC2\xB0", "Pas", "Verdict", "D\xC3\xA9tail", "Instant"};
    for (std::size_t i = 0; i < sc.steps.size(); ++i) {
        const StepResult r = i < rep.steps.size() ? rep.steps[i] : StepResult{};
        t.rows.push_back({std::to_string(i + 1), describeStep(sc.steps[i]), std::string(verdictLabel(r.verdict)), r.detail,
                          r.verdict == Verdict::Pending || r.verdict == Verdict::Skipped ? std::string{} : seconds(r.at)});
    }
    // Lot 13 : ce que l'essai verifie, et le visa de qui l'a joue (le proces-verbal de reception).
    if (!trim(sc.description).empty()) t.notes.push_back("Objet de l'essai : " + trim(sc.description));
    t.notes.push_back(kVisaLine);
    return t;
}

ExportTable campaignTable(const std::vector<std::pair<const TestScenario*, const ScenarioReport*>>& runs) {
    ExportTable t;
    std::size_t ok = 0;
    for (const auto& [sc, rep] : runs) ok += rep && rep->ok();
    t.title = "Essais de r\xC3\xA9" "ception : " + std::to_string(ok) + " / " + std::to_string(runs.size()) + " r\xC3\xA9ussi(s)";
    t.subtitle = runs.empty() || !runs.front().second ? std::string{} : runs.front().second->started;
    t.headers = {"Essai", "N\xC2\xB0", "Pas", "Verdict", "D\xC3\xA9tail"};
    for (const auto& [sc, rep] : runs) {
        if (!sc) continue;
        t.rows.push_back({sc->name, "", rep ? rep->summary() : std::string("pas encore jou\xC3\xA9"),
                          rep ? std::string(!rep->done ? "en cours" : rep->ok() ? "r\xC3\xA9ussi" : "\xC3\xA9" "chec") : std::string{}, ""});
        for (std::size_t i = 0; i < sc->steps.size(); ++i) {
            const StepResult r = rep && i < rep->steps.size() ? rep->steps[i] : StepResult{};
            t.rows.push_back({"", std::to_string(i + 1), describeStep(sc->steps[i]), std::string(verdictLabel(r.verdict)), r.detail});
        }
    }
    t.notes.push_back(kVisaLine);
    return t;
}

// ================================================================== Generer ===
void checkScenarios(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto add = [&](S sev, const TestScenario& sc, std::size_t step, std::string msg) {
        Issue i;
        i.severity = sev;
        i.category = "Essai";
        i.property = sc.name + (step > 0 ? " (pas " + std::to_string(step) + ")" : std::string{});
        i.message = std::move(msg);
        i.item = sc.id;
        out.push_back(std::move(i));
    };
    // Un objet de ce nom quelque part (Objet, Vue.Objet, ou un objet d'instance "Carte_A.Titre").
    const auto objectKnown = [&](const std::string& t) {
        for (const auto& v : p.views) {
            if (v.objectByName(t)) return true;
            if (const auto dot = t.find('.'); dot != std::string::npos && same(v.name, t.substr(0, dot)) && v.objectByName(t.substr(dot + 1)))
                return true;
            // Une instance de symbole : son nom, puis celui d'un objet du symbole.
            if (const auto dot = t.find('.'); dot != std::string::npos)
                if (const Object* inst = v.objectByName(t.substr(0, dot)); inst && inst->kind == Kind::SymbolInstance) return true;
        }
        return false;
    };
    std::vector<std::string> names;
    for (const auto& sc : p.scenarios) {
        if (sc.name.empty()) add(S::Error, sc, 0, "essai sans nom");
        else if (std::find_if(names.begin(), names.end(), [&](const std::string& n) { return same(n, sc.name); }) != names.end())
            add(S::Error, sc, 0, "nom d'essai en double : " + sc.name);
        names.push_back(sc.name);
        if (sc.steps.empty()) add(S::Info, sc, 0, "essai sans pas : il ne v\xC3\xA9rifie rien");
        bool checks = false;
        for (std::size_t i = 0; i < sc.steps.size(); ++i) {
            const auto& st = sc.steps[i];
            const std::size_t n = i + 1;
            const StepKind k = stepKind(st.action);
            const std::string t = trim(st.target), v = trim(st.value);
            switch (k) {
                case StepKind::Unknown:
                    add(S::Error, sc, n, "action inconnue : " + quoted(st.action));
                    break;
                case StepKind::OpenView:
                    if (!p.viewByName(t)) add(S::Error, sc, n, "vue " + quoted(t) + " introuvable");
                    break;
                case StepKind::Click: case StepKind::Type:
                    if (t.empty()) add(S::Error, sc, n, "aucun objet \xC3\xA0 toucher");
                    else if (!objectKnown(t)) add(S::Warning, sc, n, "objet " + quoted(t) + " introuvable dans les vues");
                    break;
                case StepKind::Write: case StepKind::Check: case StepKind::WaitUntil: {
                    checks = checks || k != StepKind::Write;
                    if (k == StepKind::Write && t.empty()) add(S::Error, sc, n, "aucune variable \xC3\xA0 \xC3\xA9" "crire");
                    const auto e = Expression::compile(v);
                    if (v.empty()) add(S::Error, sc, n, k == StepKind::Write ? "aucune valeur" : "aucune expression");
                    else if (!e.valid()) add(S::Error, sc, n, "expression illisible : " + e.error());
                    break;
                }
                case StepKind::Wait: {
                    double ms = 0;
                    if (!parseNumber(v, ms) || ms < 0) add(S::Error, sc, n, "dur\xC3\xA9" "e illisible : " + quoted(v));
                    else if (ms > 600000) add(S::Warning, sc, n, "attente de plus de 10 minutes");
                    break;
                }
                case StepKind::Login:
                    if (!p.userByLogin(t)) add(S::Warning, sc, n, "compte " + quoted(t) + " introuvable");
                    break;
                case StepKind::Acknowledge:
                    if (!t.empty() && t != "*" && t.rfind("groupe:", 0) != 0 && !p.alarmByName(t))
                        add(S::Warning, sc, n, "alarme " + quoted(t) + " introuvable");
                    break;
                default:
                    break;
            }
        }
        if (!sc.steps.empty() && !checks)
            add(S::Info, sc, 0, "aucun pas V\xC3\xA9rifier ni Attendre que : l'essai ne juge que les refus de l'IHM");
    }
}

} // namespace hmi
