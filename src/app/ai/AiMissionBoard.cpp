// =============================================================================
//  app/ai/AiMissionBoard.cpp - 1.22.0 : le tableau de bord d'une mission (voir le .hpp)
// =============================================================================
#include "AiMissionBoard.hpp"
#include "../../core/Perf.hpp"   // 1.22.2 : ce qui consomme (l'onglet Performances)

#include "AiNotifications.hpp"   // notifs::oneLine
#include "AiPanel.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <optional>

namespace app::ai {

// ============================================================================ missionui ==
namespace missionui {

namespace {
bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
} // namespace

gfx::Color color(const ui::Theme& t, Tone tone) {
    const auto& c = t.color;
    switch (tone) {
        case Tone::Text: return c.text;
        case Tone::Muted: return c.textMuted;
        case Tone::Info: return c.info;
        case Tone::Accent: return c.accent;
        case Tone::Ok: return c.ok;
        case Tone::Warning: return c.warning;
        case Tone::Error: return c.error;
        case Tone::Off: return c.textDisabled;
        case Tone::Card: return t.brand.card.a ? t.brand.card : c.inputBg;
        case Tone::Header: return c.headerBg;
        case Tone::RowAlt: return c.rowAltBg;
        case Tone::Border: return c.border;
    }
    return c.text;
}

Tone stateTone(mission::State s) noexcept {
    using S = mission::State;
    switch (s) {
        case S::Planning: return Tone::Info;
        case S::Active: return Tone::Accent;
        case S::Paused: return Tone::Warning;
        case S::Blocked: return Tone::Error;
        case S::Done: return Tone::Ok;
        case S::Cancelled: return Tone::Off;
    }
    return Tone::Muted;
}

Tone stepTone(mission::StepState s) noexcept {
    using S = mission::StepState;
    switch (s) {
        case S::ToPlan: return Tone::Off;
        case S::Waiting: return Tone::Muted;
        case S::Ready: return Tone::Info;
        case S::Running: return Tone::Accent;
        case S::AwaitingValidation: return Tone::Accent;
        case S::Applied: return Tone::Info;
        case S::Verifying: return Tone::Info;
        case S::Done: return Tone::Ok;
        case S::Error: return Tone::Error;
        case S::Blocked: return Tone::Warning;
        case S::Skipped: return Tone::Off;
        case S::Cancelled: return Tone::Off;
    }
    return Tone::Muted;
}

Tone proposalTone(std::string_view s) noexcept {
    if (s == "generee") return Tone::Accent;
    if (s == "validee") return Tone::Info;
    if (s == "appliquee") return Tone::Ok;
    if (s == "echouee") return Tone::Error;
    if (s == "obsolete" || s == "interrompue") return Tone::Warning;
    if (s == "annulee") return Tone::Off;
    return Tone::Muted;
}

Tone coherenceTone(mission::Coherence c) noexcept {
    using C = mission::Coherence;
    switch (c) {
        case C::Same: return Tone::Ok;
        case C::Changed: return Tone::Warning;
        case C::Missing: return Tone::Error;
        case C::Unknown: return Tone::Muted;
    }
    return Tone::Muted;
}

Tone eventTone(std::string_view kind) noexcept {
    if (kind == "erreur") return Tone::Error;
    if (kind == "interruption" || kind == "pause" || kind == "annulation") return Tone::Warning;
    if (kind == "application" || kind == "terminee" || kind == "audit") return Tone::Ok;
    if (kind == "proposition" || kind == "validation" || kind == "plan" || kind == "plan_modifie") return Tone::Accent;
    return Tone::Info;
}

std::string proposalStateLabel(std::string_view s) {
    if (s == "generee") return "\xC3\xA0 valider";
    if (s == "validee") return "valid\xC3\xA9" "e";
    if (s == "appliquee") return "appliqu\xC3\xA9" "e";
    if (s == "refusee") return "refus\xC3\xA9" "e";
    if (s == "echouee") return "en \xC3\xA9" "chec";
    if (s == "obsolete") return "obsol\xC3\xA8te";
    if (s == "interrompue") return "interrompue";
    if (s == "annulee") return "annul\xC3\xA9" "e";
    return std::string(s);
}

std::string eventLabel(std::string_view k) {
    if (k == "creation") return "Cr\xC3\xA9" "ation";
    if (k == "analyse") return "Analyse";
    if (k == "plan") return "Plan";
    if (k == "plan_modifie") return "Plan modifi\xC3\xA9";
    if (k == "proposition") return "Proposition";
    if (k == "validation") return "Validation";
    if (k == "application") return "Application";
    if (k == "etape") return "\xC3\x89tape";
    if (k == "erreur") return "Erreur";
    if (k == "decision") return "D\xC3\xA9" "cision";
    if (k == "pause") return "Pause";
    if (k == "reprise") return "Reprise";
    if (k == "annulation") return "Annulation";
    if (k == "interruption") return "Interruption";
    if (k == "actualisation") return "Actualisation";
    if (k == "audit") return "Audit";
    if (k == "terminee") return "Termin\xC3\xA9" "e";
    if (k == "archive") return "Archivage";
    std::string s(k);
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

std::string title(const mission::Mission& m) {
    if (!m.name.empty()) return m.name;
    std::string s = notifs::oneLine(m.request, 60);
    return s.empty() ? std::string("Mission sans nom") : s;
}

std::string count(int n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n > 1 ? many : one);
}

std::string proposalCounts(const mission::ProposalLink& p) {
    std::string s = count(p.ops, "op\xC3\xA9ration", "op\xC3\xA9rations");
    s += " \xC2\xB7 " + count(p.create, "cr\xC3\xA9" "ation", "cr\xC3\xA9" "ations");
    s += " \xC2\xB7 " + count(p.modify, "modification", "modifications");
    s += " \xC2\xB7 " + count(p.reuse, "r\xC3\xA9utilisation", "r\xC3\xA9utilisations");
    if (p.remove > 0) s += " \xC2\xB7 " + count(p.remove, "suppression", "suppressions");
    return s;
}

std::string stepList(const mission::Mission& m, const std::vector<std::string>& ids, std::size_t max) {
    std::string s;
    std::size_t n = 0;
    for (const auto& id : ids) {
        if (n == max) {
            s += ", \xE2\x80\xA6";
            break;
        }
        const mission::Step* st = m.step(id);
        s += (n ? ", " : "") + id + (st && !st->title.empty() ? " " + st->title : std::string{});
        ++n;
    }
    return s;
}

// 1.22.2 : depliee par defaut, celle qui attend une decision (a valider, verifiee, en echec).
bool cardOpenByDefault(std::string_view s) noexcept { return s == "generee" || s == "validee" || s == "echouee"; }

std::string proposalLine(const mission::ProposalLink& p) {
    std::string s = count(p.ops, "op\xC3\xA9ration", "op\xC3\xA9rations");
    if (p.state == "appliquee" || p.applied + p.failed + p.skipped > 0) {
        s += " \xC2\xB7 " + count(p.applied, "appliqu\xC3\xA9" "e", "appliqu\xC3\xA9" "es");
        if (p.failed > 0) s += ", " + std::to_string(p.failed) + " en \xC3\xA9" "chec";
        if (p.skipped > 0) s += ", " + count(p.skipped, "ignor\xC3\xA9" "e", "ignor\xC3\xA9" "es");
    } else if (p.create + p.modify + p.reuse + p.remove > 0) {
        if (p.create > 0) s += " \xC2\xB7 " + count(p.create, "cr\xC3\xA9" "ation", "cr\xC3\xA9" "ations");
        if (p.modify > 0) s += " \xC2\xB7 " + count(p.modify, "modification", "modifications");
        if (p.reuse > 0) s += " \xC2\xB7 " + count(p.reuse, "r\xC3\xA9utilisation", "r\xC3\xA9utilisations");
        if (p.remove > 0) s += " \xC2\xB7 " + count(p.remove, "suppression", "suppressions");
    }
    return s;
}

std::string forceFinishQuestion(const mission::Mission& m, bool calling) {
    const auto left = mission::unfinishedSteps(m);
    const auto waiting = mission::awaitingProposals(m);
    const auto ids = [](const std::vector<std::string>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i == 8) {
                s += ", \xE2\x80\xA6";
                break;
            }
            s += (i ? ", " : "") + v[i];
        }
        return s;
    };
    std::string s = "Forcer la fin de la mission \xC2\xAB " + title(m) + " \xC2\xBB ?\n";
    if (left.empty())
        s += "Toutes ses \xC3\xA9tapes sont termin\xC3\xA9" "es ou ignor\xC3\xA9" "es : elle passe \xC3\xA0 \xC2\xAB Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) \xC2\xBB.";
    else if (left.size() == 1)
        s += "1 \xC3\xA9tape n'est pas termin\xC3\xA9" "e (" + ids(left) + ") : elle sera marqu\xC3\xA9" "e \xC2\xAB ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e) \xC2\xBB.";
    else
        s += std::to_string(left.size()) + " \xC3\xA9tapes ne sont pas termin\xC3\xA9" "es (" + ids(left) + ") : elles seront marqu\xC3\xA9" "es \xC2\xAB ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e) \xC2\xBB.";
    s += " Rien n'est d\xC3\xA9" "fait dans le projet";
    if (waiting.empty()) {
        s += ".";
    } else {
        std::vector<std::string> tags;
        for (const auto& id : waiting)
            if (const mission::ProposalLink* p = m.proposal(id)) tags.push_back("P" + std::to_string(p->order));
        s += waiting.size() == 1 ? " ; une proposition en attente (" + ids(tags) + ") ne sera pas appliqu\xC3\xA9" "e."
                                 : " ; les propositions en attente (" + ids(tags) + ") ne seront pas appliqu\xC3\xA9" "es.";
    }
    if (calling) s += "\nLa r\xC3\xA9ponse de l'IA en cours pour elle est arr\xC3\xAAt\xC3\xA9" "e (rien n'est propos\xC3\xA9).";
    s += "\nLe rapport final suit : l'audit global, sur une copie du projet. L'historique est gard\xC3\xA9.";
    return s;
}

std::pair<int, int> stepOps(const mission::Mission& m, const mission::Step& s) {
    int done = 0, total = 0;
    for (const auto& p : m.proposals) {
        const bool covers = std::find(p.steps.begin(), p.steps.end(), s.id) != p.steps.end();
        for (const auto& l : p.lines) {
            if (!(l.step == s.id || (l.step.empty() && covers))) continue;
            if (l.state == "annulee" || l.state == "ignoree") continue;   // retiree : elle ne compte pas
            ++total;
            if (l.state == "appliquee") ++done;
        }
    }
    return {done, total};
}

mission::Live liveOf(const Assistant& as, const mission::Mission& m) {
    mission::Live l;
    // l'appel de la mission (ecrit avant de partir) et la conversation qui ecrit maintenant : la meme -> l'IA repond
    if (m.pending.active && as.generating()) {
        const std::string c = as.generatingConversationId();
        l.calling = !c.empty()
                    && (c == m.pending.conversation
                        || (m.pending.conversation.empty()
                            && (c == m.conversation || std::find(m.conversations.begin(), m.conversations.end(), c) != m.conversations.end())));
        if (l.calling)
            if (const std::time_t t0 = parseIso(m.pending.started); t0 > 0) l.elapsed = std::max<long long>(0, static_cast<long long>(app::ai::now() - t0));
    }
    if (as.applyingAny())
        if (std::string pid = as.applyingProposal(); !pid.empty() && m.proposal(pid)) {
            l.applying = std::move(pid);
            l.applyPercent = static_cast<int>(std::lround(std::clamp(as.applyProgress(), 0.f, 1.f) * 100.f));
        }
    l.auditing = as.auditing(m.id);
    return l;
}

std::int64_t liveKey(const mission::Live& l) noexcept {
    std::int64_t k = l.calling ? 1 + std::max<long long>(0, l.elapsed) : 0;
    k = k * 128 + (l.applying.empty() ? 0 : 1 + std::clamp(l.applyPercent, 0, 100));
    return k * 2 + (l.auditing ? 1 : 0);
}

Tone activityTone(mission::Activity a) noexcept {
    using A = mission::Activity;
    switch (a) {
        case A::Idle: return Tone::Info;
        case A::Working: return Tone::Accent;
        case A::ToValidate: return Tone::Accent;
        case A::Waiting: return Tone::Muted;
        case A::Blocked: return Tone::Warning;
        case A::Error: return Tone::Error;
        case A::Done: return Tone::Ok;
        case A::Off: return Tone::Off;
    }
    return Tone::Muted;
}

Tone journalTone(std::string_view kind) noexcept {
    if (kind == "operation") return Tone::Ok;
    if (kind == "echec") return Tone::Error;
    if (kind == "decision") return Tone::Info;
    return eventTone(kind);
}

std::string Moment::line() const {
    if (step.empty()) return activity.text;
    return step + " \xC2\xB7 " + progress.text() + " \xE2\x80\x94 " + activity.text;
}

Moment momentOf(const mission::Mission& m, const mission::Live& live) {
    Moment mo;
    const auto whole = [&](const std::string& text) {
        mo.activity.text = text;
        mo.activity.live = live.auditing || live.calling;
        mo.activity.kind = mo.activity.live ? mission::Activity::Working : mission::Activity::Blocked;
        return mo;
    };
    const std::string own = mission::missionActivity(m, live);
    // le plan, l'actualisation, l'audit final : la mission elle-meme travaille (aucune etape ne le porte)
    if (!own.empty() && (live.auditing || (m.pending.active && (m.pending.purpose == "plan" || m.pending.purpose == "actualisation"))))
        return whole(own);
    if (m.state == mission::State::Done && m.forced.on()) {   // 1.22.2 : << Terminee (fin forcee) : 2 etapes ignorees (S4, S5). >>
        mo.activity.kind = mission::Activity::Done;
        mo.activity.text = own;
        return mo;
    }
    if (m.state == mission::State::Cancelled || m.state == mission::State::Done) return mo;   // rien en cours
    if (const mission::Step* s = mission::currentStep(m, live)) {
        mo.step = s->id;
        mo.title = s->title;
        mo.progress = mission::stepProgressOf(m, *s);
        mo.activity = mission::activityOf(m, *s, live);
        return mo;
    }
    if (!own.empty()) return whole(own);
    return mo;
}

Next nextOf(const mission::Mission& m, bool busy) {
    using mission::State;
    using mission::StepState;
    using K = Next::Kind;
    Next n;
    if (m.state == State::Cancelled) {
        n.why = "Mission annul\xC3\xA9" "e : ses propositions ne s'appliquent plus ; son historique reste consultable.";
        return n;
    }
    if (m.state == State::Done && m.forced.on() && !m.finalReport.isObject() && !busy && m.coherence != mission::Coherence::Missing && !m.steps.empty()) {
        // 1.22.2 : une fin forcee sans rapport (son projet n'etait pas ouvert) - l'audit final le fait, comme pour une fin normale
        n.kind = Next::Kind::Audit;
        n.label = "Lancer l'audit final";
        n.tooltip = "Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : la v\xC3\xA9rification globale du projet, sur un fil de fond et une copie du projet, "
                    "puis le rapport final (il dit la fin forc\xC3\xA9" "e et les \xC3\xA9tapes ignor\xC3\xA9" "es). Le projet n'est pas modifi\xC3\xA9.";
        return n;
    }
    if (m.state == State::Done) {
        n.why = m.forced.on() ? std::string("Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : le rapport final dit ce qui a \xC3\xA9t\xC3\xA9 fait, ce qui a \xC3\xA9t\xC3\xA9 ignor\xC3\xA9 "
                                            "et ce qui reste \xC3\xA0 v\xC3\xA9rifier.")
                              : std::string("Mission termin\xC3\xA9" "e : le rapport final dit ce qui a \xC3\xA9t\xC3\xA9 fait et ce qui reste \xC3\xA0 v\xC3\xA9rifier.");
        return n;
    }
    if (busy) {
        n.why = "L'IA travaille sur cette mission : sa conversation montre la r\xC3\xA9ponse qui s'\xC3\xA9" "crit.";
        return n;
    }
    if (m.coherence == mission::Coherence::Missing) {
        n.why = "Le projet de la mission n'est pas celui qui est ouvert (ou il est introuvable) : ouvre-le pour la reprendre.";
        return n;
    }
    if (m.state == State::Paused) {
        n.kind = K::Resume;
        n.label = "Reprendre la mission";
        n.tooltip = "Le constat de reprise d'abord (le projet compar\xC3\xA9 \xC3\xA0 la derni\xC3\xA8re sauvegarde), puis la mission repart. "
                    "Rien n'est appliqu\xC3\xA9 sans toi.";
        return n;
    }
    if (m.steps.empty()) {
        n.kind = K::Refresh;
        n.label = "Relancer la planification";
        n.tooltip = "L'IA analyse le projet r\xC3\xA9" "el et pr\xC3\xA9pare le plan (planifier_mission) ; aucune op\xC3\xA9ration n'est propos\xC3\xA9" "e encore.";
        return n;
    }
    if (m.coherence == mission::Coherence::Changed) {
        n.kind = K::Refresh;
        n.label = "Actualiser le contexte";
        n.tooltip = "Le projet a chang\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde : l'IA le relit et revoit le plan. Les \xC3\xA9tapes "
                    "termin\xC3\xA9" "es et les op\xC3\xA9rations appliqu\xC3\xA9" "es sont gard\xC3\xA9" "es.";
        return n;
    }
    for (const auto& s : m.steps)
        if (s.state == StepState::Error) {
            n.kind = K::Correct;
            n.step = s.id;
            n.label = "Corriger " + s.id;
            n.tooltip = "Demander \xC3\xA0 l'IA une proposition de correction pour \xC2\xAB " + s.title + " \xC2\xBB (\xC3\xA0 v\xC3\xA9rifier et "
                        "appliquer comme les autres ; une proposition d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9" "e n'est jamais modifi\xC3\xA9" "e)."
                        + (s.error.empty() ? std::string{} : "\nL'erreur : " + s.error);
            return n;
        }
    for (const auto& p : m.proposals)
        if (p.state == "generee" || p.state == "validee") {
            n.kind = K::Review;
            n.proposal = p.id;
            n.label = "Voir la proposition P" + std::to_string(p.order);
            n.tooltip = "\xC2\xAB " + (p.title.empty() ? std::string("P") + std::to_string(p.order) : p.title)
                        + " \xC2\xBB attend ta d\xC3\xA9" "cision : sa carte dans la conversation de la mission (Pr\xC3\xA9visualiser, "
                          "Appliquer, Refuser).";
            return n;
        }
    bool allDone = true;
    for (const auto& s : m.steps)
        if (s.mandatory && !mission::isFinished(s.state) && s.state != StepState::Cancelled) allDone = false;
    if (allDone) {
        n.kind = K::Audit;
        n.label = m.finalReport.isObject() ? "Refaire l'audit final" : "Lancer l'audit final";
        n.tooltip = "La v\xC3\xA9rification globale du projet (G\xC3\xA9n\xC3\xA9rer et Compiler comme le logiciel, adresses, orphelins), "
                    "sur un fil de fond et une copie du projet, puis le rapport final. Le projet n'est pas modifi\xC3\xA9.";
        return n;
    }
    const mission::Batch batch = mission::nextBatch(m, 1 << 20);
    if (!batch.steps.empty()) {
        n.kind = K::Continue;
        n.label = "Proposer la suite";
        n.steps = batch.steps;
        n.tooltip = "Demander \xC3\xA0 l'IA la prochaine proposition, avec l'\xC3\xA9tat r\xC3\xA9" "el du projet : " + stepList(m, batch.steps, 4)
                    + ". Une carte \xC3\xA0 v\xC3\xA9rifier et appliquer, comme les autres.";
        return n;
    }
    n.why = batch.why.empty() ? std::string("Rien n'est r\xC3\xA9" "alisable pour l'instant.") : batch.why;
    return n;
}

bool wantsResume(const mission::Mission& m, mission::Coherence now, bool busy) {
    using mission::State;
    if (busy || m.state == State::Done || m.state == State::Cancelled) return false;
    return m.state == State::Paused || m.pending.active || now == mission::Coherence::Changed || now == mission::Coherence::Missing;
}

std::string elide(const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (ui::measureWidth(s, f) <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    std::size_t lo = 0, hi = s.size();
    while (lo < hi) {   // le plus long debut qui tient avec les points
        std::size_t mid = (lo + hi + 1) / 2;
        while (mid > 0 && mid < s.size() && continuation(s[mid])) --mid;
        if (mid <= lo) break;
        if (ui::measureWidth(s.substr(0, mid) + dots, f) <= w) lo = mid;
        else hi = mid - 1;
    }
    while (lo > 0 && lo < s.size() && continuation(s[lo])) --lo;
    std::string out = s.substr(0, lo);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float w, std::size_t maxLines) {
    std::vector<std::string> out;
    std::string line;
    bool more = false;
    w = std::max(w, 30.f);
    const auto full = [&] { return maxLines > 0 && out.size() >= maxLines; };
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n' || text[i] == '\r') {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.push_back(std::move(line));
            line.clear();
            ++i;
            if (full()) {
                more = text.find_first_not_of(" \r\n", i) != std::string::npos;
                break;
            }
            continue;
        }
        if (text[i] == ' ' || text[i] == '\t') {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t' && text[j] != '\n' && text[j] != '\r') ++j;
        std::string word = text.substr(i, j - i);
        if (ui::measureWidth(word, f) > w) word = elide(word, f, w);   // un mot plus large que la ligne : coupe
        std::string tryLine = line.empty() ? word : line + " " + word;
        if (line.empty() || ui::measureWidth(tryLine, f) <= w) {
            line = std::move(tryLine);
            i = j;
            continue;
        }
        out.push_back(std::move(line));   // le mot commence la ligne suivante
        line.clear();
        if (full()) {
            more = true;
            break;
        }
    }
    if (!line.empty()) {
        if (!full()) out.push_back(std::move(line));
        else more = true;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    if (more && !out.empty()) out.back() = elide(out.back() + " \xE2\x80\xA6", f, w);
    return out;
}

void paintOps(const ui::PaintContext& ctx, const std::vector<Op>& ops, gfx::Point o) {
    for (const auto& op : ops) {
        const gfx::Rect r{o.x + op.r.x, o.y + op.r.y, op.r.w, op.r.h};
        const gfx::Color c = color(ctx.theme, op.tone).withAlpha(static_cast<std::uint8_t>(std::clamp(op.alpha, 0, 255)));
        switch (op.kind) {
            case Op::Kind::Text: ctx.r.drawText({r.x, r.y}, op.text, op.font, c); break;
            case Op::Kind::Bold:
                ctx.r.drawText({r.x, r.y}, op.text, op.font, c);
                ctx.r.drawText({r.x + 0.8f, r.y}, op.text, op.font, c);   // le gras du logiciel
                break;
            case Op::Kind::Fill: ctx.r.fillRect(r, c); break;
            case Op::Kind::Round: ctx.r.fillRoundedRect(r, c, std::min(6.f, r.h / 2.f)); break;
            case Op::Kind::Stroke: ctx.r.strokeRect(r, c, 1.f); break;
            case Op::Kind::Chip: {
                const gfx::Color full = color(ctx.theme, op.tone);
                ctx.r.fillRoundedRect(r, full.withAlpha(46), r.h / 2.f);
                ctx.r.drawText({r.x + 7.f, r.y + (r.h - ctx.r.lineHeight(op.font)) / 2.f}, op.text, op.font, full);
                break;
            }
            case Op::Kind::Bar: {
                const gfx::Color full = color(ctx.theme, op.tone);
                ctx.r.fillRoundedRect(r, full.withAlpha(40), r.h / 2.f);
                const float f = std::clamp(op.frac, 0.f, 1.f);
                if (f > 0.f) ctx.r.fillRoundedRect({r.x, r.y, std::max(r.h, r.w * f), r.h}, full, r.h / 2.f);
                break;
            }
            case Op::Kind::Icon: ui::drawIcon(ctx.r, op.icon, r, c); break;
        }
    }
}

} // namespace missionui

// ============================================================================ la page ==
namespace {

using missionui::Op;
using missionui::Tone;

const gfx::FontId kTitle{20};
const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
constexpr float kPad = 16.f;
constexpr float kGap = 10.f;
constexpr float kBtnH = 28.f;
constexpr float kHeadH = 26.f;
constexpr float kMaxW = 1180.f;

float lineH(gfx::FontId f) { return ui::lineHeight(f) + 3.f; }

double steadySeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void text(std::vector<Op>& ops, float x, float y, std::string s, gfx::FontId f, Tone t, bool bold = false) {
    Op op;
    op.kind = bold ? Op::Kind::Bold : Op::Kind::Text;
    op.r = {x, y, 0.f, ui::lineHeight(f)};
    op.text = std::move(s);
    op.font = f;
    op.tone = t;
    ops.push_back(std::move(op));
}

// Le texte a la ligne, a partir de `y` ; rend le bas.
float wrapped(std::vector<Op>& ops, float x, float y, float w, const std::string& s, gfx::FontId f, Tone t, std::size_t maxLines = 0,
              bool bold = false) {
    for (auto& l : missionui::wrap(s, f, w, maxLines)) {
        text(ops, x, y, std::move(l), f, t, bold);
        y += lineH(f);
    }
    return y;
}

float chip(std::vector<Op>& ops, float x, float y, const std::string& s, Tone t, gfx::FontId f = kMeta) {
    Op op;
    op.kind = Op::Kind::Chip;
    const float w = ui::measureWidth(s, f) + 14.f;
    op.r = {x, y, w, ui::lineHeight(f) + 6.f};
    op.text = s;
    op.font = f;
    op.tone = t;
    ops.push_back(std::move(op));
    return w;
}

void fill(std::vector<Op>& ops, gfx::Rect r, Tone t, int a = 255, bool round = false) {
    Op op;
    op.kind = round ? Op::Kind::Round : Op::Kind::Fill;
    op.r = r;
    op.tone = t;
    op.alpha = a;
    ops.push_back(std::move(op));
}

void stroke(std::vector<Op>& ops, gfx::Rect r, Tone t, int a = 255) {
    Op op;
    op.kind = Op::Kind::Stroke;
    op.r = r;
    op.tone = t;
    op.alpha = a;
    ops.push_back(std::move(op));
}

void bar(std::vector<Op>& ops, gfx::Rect r, double frac, Tone t) {
    Op op;
    op.kind = Op::Kind::Bar;
    op.r = r;
    op.frac = static_cast<float>(std::clamp(frac, 0.0, 1.0));
    op.tone = t;
    ops.push_back(std::move(op));
}

void icon(std::vector<Op>& ops, gfx::Rect r, ui::Icon i, Tone t) {
    Op op;
    op.kind = Op::Kind::Icon;
    op.r = r;
    op.icon = i;
    op.tone = t;
    ops.push_back(std::move(op));
}

std::string pct(double v) { return std::to_string(static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 100.0))) + " %"; }

std::string joined(const std::vector<std::string>& v, std::string_view sep = ", ") {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? std::string(sep) : std::string{}) + v[i];
    return s;
}

// L'etat d'une operation suivie (plan::stateKey) : son libelle et sa couleur.
std::pair<std::string, Tone> opState(const std::string& k) {
    if (k == "appliquee") return {"appliqu\xC3\xA9" "e", Tone::Ok};
    if (k == "echouee") return {"en \xC3\xA9" "chec", Tone::Error};
    if (k == "ignoree") return {"ignor\xC3\xA9" "e", Tone::Off};
    if (k == "annulee") return {"annul\xC3\xA9" "e", Tone::Off};
    if (k == "validee") return {"valid\xC3\xA9" "e", Tone::Info};
    return {"\xC3\xA0 faire", Tone::Muted};
}

std::string when(const std::string& iso) { return iso.empty() ? std::string("\xE2\x80\x94") : shortDate(iso); }

// Un intertitre : un bandeau fin (comme << CONVERSATIONS >> du panneau) ; rend le bas.
float heading(std::vector<Op>& ops, float x, float y, float w, const std::string& s) {
    fill(ops, {x, y, w, kHeadH}, Tone::RowAlt);
    fill(ops, {x, y + kHeadH - 1.f, w, 1.f}, Tone::Border);
    text(ops, x + 10.f, y + (kHeadH - ui::lineHeight(kMeta)) / 2.f, s, kMeta, Tone::Muted);
    return y + kHeadH + 8.f;
}

} // namespace

AiMissionBoard::AiMissionBoard(std::string id, AiPanel& panel, std::string missionId)
    : ui::Widget(std::move(id)), panel_(panel), missionId_(std::move(missionId)) {
    setFocusPolicy(true);
    setZoomSupport(ui::ZoomSupport::Scaled);   // le zoom de la page, comme les autres pages du centre
    // 1.22.2 : le clic droit sur l'en-tete d'une carte de proposition (le menu de l'application)
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(this->id() + ".menu")));
    links_ += menu_->itemChosen->connect([this](int item) { menuChosen(item); });
    refresh();
}

std::string AiMissionBoard::tabTitle() const { return "Mission IA \xC2\xB7 " + (name_.empty() ? std::string("introuvable") : name_); }

void AiMissionBoard::say(const std::string& t) {
    if (!t.empty() && panel_.uiHost().status) panel_.uiHost().status(t);
}

void AiMissionBoard::setMode(Mode m) {
    if (m == Mode::Resume) inspect();
    if (m == mode_ && builtW_ >= 0.f) {
        rebuild();
        return;
    }
    mode_ = m;
    scroll_ = 0.f;
    rebuild();
}

void AiMissionBoard::showHistory() {
    pendingSection_ = "historique";
    if (builtW_ > 0.f) {
        const gfx::Rect r = sectionRect("historique");
        if (r.h > 0.f) {
            scrollTo(scroll_ + (r.y - bounds().y) - 8.f);
            pendingSection_.clear();
        }
    }
    invalidateLayout();
}

void AiMissionBoard::inspect() {
    const auto& as = panel_.assistant();
    recovery_ = as.inspectMission(missionId_);
    inspected_ = true;
    inspectedAt_ = steadySeconds();
    const auto doc = as.host().document ? as.host().document() : nullptr;
    docRev_ = doc ? static_cast<std::uint64_t>(doc->revision) : 0;
}

void AiMissionBoard::refresh() {
    XPG_MESURE("ia.mission.tableau");   // 1.22.2
    if (mode_ == Mode::Resume) inspect();
    rebuild();
}

bool AiMissionBoard::poll() {
    const auto& as = panel_.assistant();
    bool redo = as.missionsRevision() != seenRev_ || as.generating() != generating_;
    if (const auto* m = as.findMission(missionId_)) {
        redo |= panel_.missionBusy(*m) != busy_;
        // 1.22.1 : le direct (l'appel a l'IA, l'application de fond) - au plus une fois par seconde, ou par pour-cent
        redo |= missionui::liveKey(missionui::liveOf(as, *m)) != liveKey_;
    } else {
        redo |= found_;
    }
    // L'ecran de reprise : le projet a change (sa revision) - le constat est refait, au plus toutes les 2 s (un geste
    // d'edition n'en fait pas un a chaque image).
    if (mode_ == Mode::Resume) {
        const auto doc = as.host().document ? as.host().document() : nullptr;
        const std::uint64_t rev = doc ? static_cast<std::uint64_t>(doc->revision) : 0;
        const double now = steadySeconds();
        if (rev != docRev_ && (now - inspectedAt_ >= 2.0 || now < inspectedAt_)) {
            inspect();
            redo = true;
        }
    }
    if (!redo) return false;
    rebuild();
    return true;
}

float AiMissionBoard::useButton(Block& blk, const std::string& key, const std::string& label, const std::string& tip, ui::Button::Style style,
                                bool enabled, gfx::Point at, std::function<void()> run) {
    Slot& s = slots_[key];
    if (!s.button) {
        auto btn = std::make_unique<ui::Button>(label, id() + "." + key);
        s.button = &static_cast<ui::Button&>(addChild(std::move(btn)));
        links_ += s.button->clicked->connect([this, key] {
            const auto it = slots_.find(key);
            if (it == slots_.end()) return;
            const auto go = it->second.run;   // une copie : le geste peut refaire la page (et ses gestes)
            if (go) go();
        });
    }
    s.button->setText(label);
    s.button->setStyle(style);
    s.button->setTooltip(tip);
    s.button->setEnabled(enabled);
    s.run = std::move(run);
    s.used = true;
    const float w = ui::measureWidth(label, gfx::FontId{16}) + 28.f;
    blk.buttons.emplace_back(gfx::Rect{at.x, at.y, w, kBtnH}, key);
    return w;
}

ui::Button* AiMissionBoard::button(std::string_view key) const {
    const auto it = slots_.find(std::string(key));
    if (it == slots_.end() || !it->second.used || !it->second.button) return nullptr;
    return it->second.button;
}

// ------------------------------------------------------------------------------- les sections ==
AiMissionBoard::Block AiMissionBoard::blockMissing(float x, float w) {
    Block b;
    b.section = "entete";
    float y = 14.f;
    icon(b.ops, {x, y, 22.f, 22.f}, ui::Icon::Warning, Tone::Warning);
    text(b.ops, x + 30.f, y, "Mission introuvable", kTitle, Tone::Text, true);
    y += lineH(kTitle) + 8.f;
    y = wrapped(b.ops, x, y, w,
                "Son fichier (IA/missions/" + missionId_ + ".json) n'est plus l\xC3\xA0, ou il n'a pas pu \xC3\xAAtre relu. "
                "Ferme cet onglet : l'onglet Missions IA de l'Assistant liste celles qui existent.",
                kBody, Tone::Muted);
    b.h = y + 12.f;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockHeader(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "entete";
    const auto& as = panel_.assistant();
    float y = 14.f;
    // le nom, l'etat
    icon(b.ops, {x, y + 1.f, 22.f, 22.f}, ui::Icon::Task, Tone::Accent);
    std::string state = mission::stateText(m);   // 1.22.2 : << Terminee (fin forcee) >>
    if (m.archived) state += " \xC2\xB7 archiv\xC3\xA9" "e";
    const float cw = ui::measureWidth(state, kMeta) + 14.f;
    chip(b.ops, x + w - cw, y + 2.f, state, missionui::stateTone(m.state));
    const std::string name = missionui::title(m);
    const std::string shown = missionui::elide(name, kTitle, w - cw - 46.f);
    text(b.ops, x + 30.f, y, shown, kTitle, Tone::Text, true);
    if (shown != name) b.tips.emplace_back(gfx::Rect{x, y, w - cw, lineH(kTitle)}, name);
    y += lineH(kTitle) + 6.f;
    // le projet, la derniere activite, la coherence
    std::string meta = "Projet : " + (m.project.name.empty() ? std::string("non enregistr\xC3\xA9") : m.project.name)
                       + " \xC2\xB7 Derni\xC3\xA8re activit\xC3\xA9 : " + when(m.modified) + " \xC2\xB7 Cr\xC3\xA9\xC3\xA9" "e " + when(m.created);
    y = wrapped(b.ops, x, y, w, meta, kMeta, Tone::Muted, 2);
    if (m.coherence != mission::Coherence::Unknown) {
        const std::string co = "Projet : " + std::string(mission::coherenceLabel(m.coherence));
        chip(b.ops, x, y + 2.f, co, missionui::coherenceTone(m.coherence));
        if (!m.coherenceText.empty()) b.tips.emplace_back(gfx::Rect{x, y, ui::measureWidth(co, kMeta) + 14.f, 22.f}, m.coherenceText);
        y += 26.f;
    }
    // la progression globale (sa regle en infobulle)
    y += 6.f;
    const std::string p = pct(progress_.global) + " \xC2\xB7 globale";
    const float pw = ui::measureWidth(p, kMeta) + 12.f;
    bar(b.ops, {x, y + 3.f, std::max(40.f, w - pw), 10.f}, progress_.global,
        m.state == mission::State::Done ? Tone::Ok : m.state == mission::State::Blocked ? Tone::Error : Tone::Accent);
    text(b.ops, x + w - pw + 10.f, y, p, kMeta, Tone::Text, true);
    b.tips.emplace_back(gfx::Rect{x, y - 2.f, w, 20.f}, "La progression globale : les \xC3\xA9tapes TERMIN\xC3\x89" "ES (v\xC3\xA9rifi\xC3\xA9" "es dans le projet).\n\n"
                                                             + mission::progressRule());
    y += 18.f;
    y = wrapped(b.ops, x, y, w, progress_.text(), kMeta, Tone::Muted, 2);
    if (busy_) {
        y = wrapped(b.ops, x, y + 2.f, w,
                    as.auditing(missionId_) ? std::string("Audit final en cours (sur un fil de fond, une copie du projet)\xE2\x80\xA6")
                                            : std::string("L'IA travaille sur cette mission : sa conversation montre la r\xC3\xA9ponse\xE2\x80\xA6"),
                    kMeta, Tone::Accent, 2);
    }
    // 1.22.1 : EN CE MOMENT - l'etape du moment, sa progression, ce qu'elle fait (un clic : son journal, dans le plan)
    if (mode_ == Mode::Board)
        if (const missionui::Moment mo = missionui::momentOf(m, live_); !mo.empty()) {
            const float top = y + 2.f;
            y = wrapped(b.ops, x, top, w, "En ce moment : " + mo.line(), kMeta, missionui::activityTone(mo.activity.kind), 2, true);
            std::string tip = mo.step.empty() ? mo.activity.text : mo.step + " " + mo.title + " \xE2\x80\x94 " + mo.progress.text() + " (" + mo.progress.detail() + ")\n" + mo.activity.text;
            if (!mo.step.empty()) {
                tip += "\nUn clic : son journal (dans le plan d'action).";
                b.clicks.emplace_back(gfx::Rect{x, top, w, y - top}, "voir:" + mo.step);
            }
            b.tips.emplace_back(gfx::Rect{x, top, w, y - top}, tip);
        }
    // les gestes : Pause / Reprendre, Annuler, la conversation, l'autre ecran
    y += 8.f;
    float bx = x;
    const auto place = [&](float bw) {
        bx += bw + 6.f;
    };
    const auto room = [&](const std::string& label) {
        const float bw = ui::measureWidth(label, gfx::FontId{16}) + 28.f;
        if (bx > x && bx + bw > x + w) {
            bx = x;
            y += kBtnH + 6.f;
        }
    };
    using St = mission::State;
    const std::string mid = missionId_;
    if (m.state == St::Paused) {
        // un projet introuvable : on ne propose pas de reprendre (l'ecran de reprise l'explique)
        const bool missing = (inspected_ ? recovery_.coherence : m.coherence) == mission::Coherence::Missing;
        room("Reprendre");
        place(useButton(b, "reprendre", "Reprendre",
                        missing ? std::string("Le projet de la mission n'est pas ouvert : ouvre-le d'abord (l'\xC3\xA9" "cran de reprise l'explique).")
                                : std::string("Reprendre la mission (le constat de reprise d'abord ; rien n'est appliqu\xC3\xA9 sans toi)."),
                        ui::Button::Style::Primary, !busy_ && !missing, {bx, y}, [this, mid] {
                            if (panel_.missionAct(mid, AiPanel::MissionAct::Resume)) setMode(Mode::Board);
                        }));
    } else {
        const bool can = m.state == St::Active || m.state == St::Planning || m.state == St::Blocked;
        room("Pause");
        place(useButton(b, "pause", "Pause",
                        can ? std::string("Mettre la mission en pause : la r\xC3\xA9ponse en cours pour elle s'arr\xC3\xAAte (marqu\xC3\xA9" "e interrompue), "
                                          "plus rien ne s'applique jusqu'\xC3\xA0 Reprendre.")
                            : std::string("La mission n'est pas en cours."),
                        ui::Button::Style::Default, can, {bx, y}, [this, mid] { (void)panel_.missionAct(mid, AiPanel::MissionAct::Pause); }));
    }
    {
        const bool can = m.state != St::Done && m.state != St::Cancelled;
        room("Annuler la mission\xE2\x80\xA6");
        place(useButton(b, "annuler", "Annuler la mission\xE2\x80\xA6",
                        can ? std::string("Annuler la mission (une question d'abord) : ses propositions ne s'appliqueront plus ; ce qui est "
                                          "d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9 reste dans le projet. L'historique est gard\xC3\xA9.")
                            : std::string("La mission est d\xC3\xA9j\xC3\xA0 termin\xC3\xA9" "e ou annul\xC3\xA9" "e."),
                        ui::Button::Style::Flat, can, {bx, y}, [this, mid] { (void)panel_.missionAct(mid, AiPanel::MissionAct::Cancel); }));
    }
    {
        // 1.22.2 : FORCER LA FIN - une question d'abord ; refuse (et pourquoi) : terminee, annulee, en lecture seule, une
        // proposition de la mission qui s'applique sur un fil de fond (le direct refait la page quand elle commence ou finit)
        std::string whyNot;
        const bool can = as.canForceFinish(missionId_, &whyNot);
        const std::string label = "Forcer la fin de la mission\xE2\x80\xA6";
        room(label);
        place(useButton(b, "forcer", label,
                        can ? std::string("Terminer la mission l\xC3\xA0 o\xC3\xB9 elle en est (une question d'abord) : ses \xC3\xA9tapes non termin\xC3\xA9" "es passent "
                                          "\xC2\xAB ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e) \xC2\xBB, une proposition en attente ne s'applique pas, la r\xC3\xA9ponse "
                                          "de l'IA en cours pour elle s'arr\xC3\xAAte. Rien n'est d\xC3\xA9" "fait dans le projet ; le rapport final suit.")
                            : "Impossible maintenant : " + whyNot,
                        ui::Button::Style::Flat, can, {bx, y}, [this, mid] { (void)panel_.missionAct(mid, AiPanel::MissionAct::ForceFinish); }));
    }
    room("Conversation");
    place(useButton(b, "conversation", "Conversation", "Ouvrir la conversation de la mission dans le panneau de l'Assistant (ses cartes, ses r\xC3\xA9ponses).",
                    ui::Button::Style::Flat, true, {bx, y}, [this, mid] { (void)panel_.missionAct(mid, AiPanel::MissionAct::Conversation); }));
    const std::string other = mode_ == Mode::Board ? std::string("\xC3\x89" "cran de reprise") : std::string("Tableau de bord");
    room(other);
    place(useButton(b, "mode", other,
                    mode_ == Mode::Board ? std::string("Le constat de reprise : le dernier \xC3\xA9tat sauvegard\xC3\xA9, le projet compar\xC3\xA9, la prochaine action.")
                                         : std::string("Le tableau de bord complet : le plan, les propositions, l'historique."),
                    ui::Button::Style::Flat, true, {bx, y}, [this] { setMode(mode_ == Mode::Board ? Mode::Resume : Mode::Board); }));
    y += kBtnH + 12.f;
    b.h = y;
    Op back;   // le fond de l'en-tete, sous tout le reste (toute la largeur de la page)
    back.kind = Op::Kind::Fill;
    back.r = {0.f, 0.f, std::max(builtW_, x + w + kPad), b.h};
    back.tone = Tone::Header;
    b.ops.insert(b.ops.begin(), back);
    Op rule;
    rule.kind = Op::Kind::Fill;
    rule.r = {0.f, b.h - 1.f, std::max(builtW_, x + w + kPad), 1.f};
    rule.tone = Tone::Border;
    b.ops.push_back(rule);
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockProgress(const mission::Mission&, float x, float w) {
    Block b;
    b.section = "progression";
    float y = heading(b.ops, x, 0.f, w, "PROGRESSION");
    const std::string rule = mission::progressRule();
    struct Row { const char* label; double v; const char* tip; };
    const Row rows[] = {
        {"Planification", progress_.planning, "Planification : 100 % d\xC3\xA8s que le plan existe."},
        {"G\xC3\xA9n\xC3\xA9ration", progress_.generation, "G\xC3\xA9n\xC3\xA9ration : le poids des \xC3\xA9tapes qui ont une proposition (ou plus loin)."},
        {"Validation", progress_.validation, "Validation : celles dont la proposition est valid\xC3\xA9" "e ou appliqu\xC3\xA9" "e."},
        {"Application", progress_.application, "Application : celles appliqu\xC3\xA9" "es ou termin\xC3\xA9" "es."},
        {"Globale", progress_.global, "Globale : celles TERMIN\xC3\x89" "ES (ce qu'elles cr\xC3\xA9" "ent est dans le projet)."},
    };
    const float lw = 120.f, vw = 56.f;
    for (const auto& r : rows) {
        const bool global = std::string_view(r.label) == "Globale";
        text(b.ops, x, y, r.label, kMeta, global ? Tone::Text : Tone::Muted, global);
        bar(b.ops, {x + lw, y + 4.f, std::max(40.f, w - lw - vw), 8.f}, r.v, global ? Tone::Accent : Tone::Info);
        text(b.ops, x + w - vw + 10.f, y, pct(r.v), kMeta, Tone::Text);
        b.tips.emplace_back(gfx::Rect{x, y - 2.f, w, 20.f}, std::string(r.tip) + "\n\n" + rule);
        y += 20.f;
    }
    y = wrapped(b.ops, x, y + 4.f, w, progress_.text(), kMeta, Tone::Muted, 2);
    b.h = y + 4.f;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockGoal(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "objectif";
    float y = heading(b.ops, x, 0.f, w, "OBJECTIF");
    if (!m.description.empty()) y = wrapped(b.ops, x, y, w, m.description, kBody, Tone::Text, 3) + 4.f;
    text(b.ops, x, y, "Demande initiale :", kMeta, Tone::Muted);
    y += lineH(kMeta);
    const auto all = missionui::wrap(m.request, kBody, w);
    const std::size_t shownLines = requestOpen_ ? all.size() : std::min<std::size_t>(3, all.size());
    if (!requestOpen_ && all.size() > 3) {
        y = wrapped(b.ops, x, y, w, m.request, kBody, Tone::Text, 3);
    } else {
        for (std::size_t i = 0; i < shownLines; ++i) {
            text(b.ops, x, y, all[i], kBody, Tone::Text);
            y += lineH(kBody);
        }
    }
    if (all.size() > 3) {
        const std::string link = requestOpen_ ? std::string("R\xC3\xA9" "duire") : std::string("Voir la demande enti\xC3\xA8re \xE2\x80\xBA");
        text(b.ops, x, y + 2.f, link, kMeta, Tone::Accent);
        const float lw = ui::measureWidth(link, kMeta);
        b.clicks.emplace_back(gfx::Rect{x - 2.f, y, lw + 6.f, lineH(kMeta) + 4.f}, "demande");
        b.tips.emplace_back(gfx::Rect{x - 2.f, y, lw + 6.f, lineH(kMeta) + 4.f},
                            requestOpen_ ? std::string("Revenir au r\xC3\xA9sum\xC3\xA9") : std::string("La demande exacte, telle que tu l'as \xC3\xA9" "crite"));
        y += lineH(kMeta) + 4.f;
    }
    // l'analyse du projet faite par l'IA (planifier_mission)
    const auto& a = m.analysis;
    if (!a.conform.empty() || !a.toModify.empty() || !a.toComplete.empty() || !a.toCreate.empty() || !a.impossible.empty() || !a.summary.empty()) {
        y += 6.f;
        text(b.ops, x, y, "Analyse du projet", kMeta, Tone::Text, true);
        y += lineH(kMeta);
        const std::string counts = missionui::count(static_cast<int>(a.conform.size()), "\xC3\xA9l\xC3\xA9ment conforme", "\xC3\xA9l\xC3\xA9ments conformes")
                                   + " \xC2\xB7 " + std::to_string(a.toModify.size()) + " \xC3\xA0 modifier \xC2\xB7 " + std::to_string(a.toComplete.size())
                                   + " \xC3\xA0 compl\xC3\xA9ter \xC2\xB7 " + std::to_string(a.toCreate.size()) + " \xC3\xA0 cr\xC3\xA9" "er \xC2\xB7 "
                                   + missionui::count(static_cast<int>(a.impossible.size()), "impossible", "impossibles");
        y = wrapped(b.ops, x, y, w, counts, kMeta, Tone::Muted, 2);
        if (!a.summary.empty()) y = wrapped(b.ops, x, y, w, a.summary, kMeta, Tone::Text, 3);
        for (std::size_t i = 0; i < a.impossible.size() && i < 3; ++i)
            y = wrapped(b.ops, x, y, w, "Impossible : " + a.impossible[i], kMeta, Tone::Warning, 2);
    }
    b.h = y + 4.f;
    return b;
}

void AiMissionBoard::doNext() {
    const missionui::Next n = next_;   // une copie : le geste refait la page
    using K = missionui::Next::Kind;
    switch (n.kind) {
        case K::Resume: (void)panel_.missionAct(missionId_, AiPanel::MissionAct::Resume); break;
        case K::Refresh: (void)panel_.missionAct(missionId_, AiPanel::MissionAct::Refresh); break;
        case K::Review: (void)panel_.missionProposal(missionId_, n.proposal, 1); break;
        case K::Correct: (void)panel_.missionAct(missionId_, AiPanel::MissionAct::Correct, n.step); break;
        case K::Audit: (void)panel_.missionAct(missionId_, AiPanel::MissionAct::Audit); break;
        case K::Continue: (void)panel_.missionAct(missionId_, AiPanel::MissionAct::Continue); break;
        case K::None: break;
    }
}

AiMissionBoard::Block AiMissionBoard::blockNext(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "prochaine";
    const auto& as = panel_.assistant();
    float y = heading(b.ops, x, 0.f, w, "PROCHAINE ACTION");
    y = wrapped(b.ops, x, y, w, mission::nextAction(m), kBody, Tone::Text, 4) + 4.f;
    using K = missionui::Next::Kind;
    if (next_.kind != K::None) {
        const bool needsAi = next_.kind == K::Refresh || next_.kind == K::Correct || next_.kind == K::Continue;
        const bool enabled = !(needsAi && as.generating());
        std::string tip = next_.tooltip;
        if (!enabled) tip += "\n(Une r\xC3\xA9ponse est en cours : attends sa fin.)";
        (void)useButton(b, "action", next_.label, tip, ui::Button::Style::Primary, enabled, {x, y}, [this] { doNext(); });
        y += kBtnH + 8.f;
    } else if (!next_.why.empty()) {
        y = wrapped(b.ops, x, y, w, next_.why, kMeta, Tone::Muted, 3) + 2.f;
    }
    if (m.state == mission::State::Done && m.finalReport.isObject()) {
        const std::string link = "Voir le rapport final \xE2\x80\xBA";
        text(b.ops, x, y, link, kMeta, Tone::Accent);
        b.clicks.emplace_back(gfx::Rect{x - 2.f, y, ui::measureWidth(link, kMeta) + 6.f, lineH(kMeta) + 4.f}, "rapport");
        y += lineH(kMeta) + 4.f;
    }
    // un appel a l'IA interrompu (a la fermeture) : rien n'est rejoue
    if (m.pending.active && !busy_)
        y = wrapped(b.ops, x, y, w,
                    "Un appel \xC3\xA0 l'IA a \xC3\xA9t\xC3\xA9 interrompu (" + (m.pending.purpose.empty() ? std::string("une r\xC3\xA9ponse") : m.pending.purpose)
                        + (m.pending.started.empty() ? std::string{} : ", " + shortDate(m.pending.started))
                        + ") : sa proposition n'existe pas, rien n'est rejou\xC3\xA9 sans v\xC3\xA9rification.",
                    kMeta, Tone::Warning, 3);
    // les erreurs et les blocages
    std::size_t shown = 0, more = 0;
    for (const auto& s : m.steps) {
        const bool err = s.state == mission::StepState::Error, blocked = s.state == mission::StepState::Blocked;
        if (!err && !blocked) continue;
        if (shown == 6) {
            ++more;
            continue;
        }
        ++shown;
        const std::string why = err ? (s.error.empty() ? s.note : s.error) : s.note;
        y = wrapped(b.ops, x, y, w,
                    s.id + " " + s.title + " \xE2\x80\x94 " + std::string(mission::stepLabel(s.state)) + (why.empty() ? std::string{} : " : " + why),
                    kMeta, err ? Tone::Error : Tone::Warning, 2);
    }
    if (more > 0) y = wrapped(b.ops, x, y, w, "\xE2\x80\xA6 et " + missionui::count(static_cast<int>(more), "autre", "autres") + " (le plan les montre)", kMeta, Tone::Muted, 1);
    b.h = y + 4.f;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockPlan(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "plan";
    std::string head = "PLAN D'ACTION \xE2\x80\x94 " + missionui::count(static_cast<int>(m.steps.size()), "\xC3\xA9tape", "\xC3\xA9tapes");
    if (m.planRevision > 0) head += " \xC2\xB7 r\xC3\xA9vision " + std::to_string(m.planRevision);
    float y = heading(b.ops, x, 0.f, w, head);
    if (m.steps.empty()) {
        y = wrapped(b.ops, x, y, w, "Le plan n'est pas encore fait : l'IA analyse d'abord le projet r\xC3\xA9" "el (ce qui existe, ce qui manque), puis l'\xC3\xA9tablit.",
                    kMeta, Tone::Muted, 3);
        b.h = y + 4.f;
        return b;
    }
    const bool wide = w >= 760.f;
    const float inner = w - 16.f;
    const float wN = 44.f, wState = 200.f, wDeps = 120.f, wProg = 140.f;
    const float rest = std::max(120.f, inner - (wN + wState + wDeps + wProg));
    const float wTitle = rest * 0.45f, wDetail = rest - wTitle;
    const float cN = x + 8.f, cTitle = cN + wN, cState = cTitle + wTitle, cDetail = cState + wState, cDeps = cDetail + wDetail, cProg = cDeps + wDeps;
    if (wide) {
        fill(b.ops, {x, y - 4.f, w, lineH(kMeta) + 6.f}, Tone::RowAlt, 140);
        const struct { float cx; const char* t; } cols[] = {{cN, "N\xC2\xB0"}, {cTitle, "\xC3\x89tape"}, {cState, "\xC3\x89tat"}, {cDetail, "D\xC3\xA9tails"},
                                                            {cDeps, "D\xC3\xA9pendances"}, {cProg, "Progression"}};
        for (const auto& c : cols) text(b.ops, c.cx, y - 1.f, c.t, kMeta, Tone::Muted);
        y += lineH(kMeta) + 6.f;
    }
    // 1.22.1 : une ligne de plus par etape - ce qu'elle fait maintenant (mission::activityOf) ; sa progression REELLE
    // (mission::stepProgressOf : ses operations appliquees sur ses operations prevues) ; un clic deplie son journal.
    const float rowH = wide ? 10.f + lineH(kBody) + 2.f * lineH(kMeta) + 4.f : 10.f + lineH(kBody) + 3.f * lineH(kMeta) + 4.f;
    const std::string stepRule = mission::stepProgressRule();
    std::size_t index = 0;
    for (const auto& s : m.steps) {
        const float top = y;
        if (index++ % 2 == 1) fill(b.ops, {x, top, w, rowH}, Tone::RowAlt, 70);
        const Tone tone = missionui::stepTone(s.state);
        const std::string label = mission::stepText(m, s);   // 1.22.2 : << Ignoree (fin forcee) >>
        const mission::StepProgress sp = mission::stepProgressOf(m, s);
        const mission::StepActivity act = mission::activityOf(m, s, live_);
        const Tone actTone = missionui::activityTone(act.kind);
        const bool open = stepsOpen_.count(s.id) > 0;
        const Tone progTone = s.state == mission::StepState::Done ? Tone::Ok : s.state == mission::StepState::Error ? Tone::Error : Tone::Info;
        // la progression en court : "2/5 op.", "estim. 4 op.", "hors calcul"
        std::string ops;
        if (!sp.counted()) ops = "hors calcul";
        else if (sp.estimated) ops = s.state == mission::StepState::Done ? std::string("v\xC3\xA9rifi\xC3\xA9" "e") : "estim. " + std::to_string(sp.planned) + " op.";
        else ops = std::to_string(sp.applied) + "/" + std::to_string(sp.planned) + " op.";
        const std::string deps = s.deps.empty() ? std::string("\xE2\x80\x94") : joined(s.deps);
        std::string ttl = s.title + (s.mandatory ? std::string{} : std::string(" (facultative)"));
        // l'infobulle de la ligne : tout, en entier
        std::string tip = s.id + "  " + s.title + " \xE2\x80\x94 " + label;
        tip += "\nEn ce moment : " + act.text;
        if (!s.detail.empty()) tip += "\n" + s.detail;
        tip += "\nCat\xC3\xA9gorie : " + std::string(mission::categoryLabel(s.category)) + (s.mandatory ? "" : " \xC2\xB7 facultative");
        if (!s.deps.empty()) tip += "\nD\xC3\xA9pend de : " + joined(s.deps);
        if (!s.implicit.empty()) tip += " (dont " + joined(s.implicit) + " par les r\xC3\xA8gles de cat\xC3\xA9gories)";
        if (!s.expected.empty()) tip += "\nDoit produire : " + joined(s.expected);
        if (!s.note.empty()) tip += "\nNote : " + s.note;
        if (!s.error.empty()) tip += "\nErreur : " + s.error;
        if (!s.proposals.empty()) {
            std::vector<std::string> ps;
            for (const auto& pid : s.proposals)
                if (const auto* p = m.proposal(pid)) ps.push_back("P" + std::to_string(p->order));
            if (!ps.empty()) tip += "\nPropositions : " + joined(ps);
        }
        tip += "\nProgression de l'\xC3\xA9tape : " + sp.text() + " \xE2\x80\x94 " + sp.detail() + ".";
        tip += open ? "\nUn clic : replier son journal." : "\nUn clic : son journal (les propositions, les op\xC3\xA9rations faites, les erreurs).";
        tip += "\n\n" + stepRule;
        b.tips.emplace_back(gfx::Rect{x, top, w, rowH}, tip);
        b.clicks.emplace_back(gfx::Rect{x, top, w, rowH}, "etape:" + s.id);
        stepRows_[s.id] = gfx::Rect{x, top, w, rowH};
        const float y1 = top + 6.f, y2 = y1 + lineH(kBody) + 1.f;
        const std::string caret = open ? "\xE2\x96\xBE" : "\xE2\x96\xB8";
        if (wide) {
            text(b.ops, cN, y1, s.id, kBody, Tone::Text, true);
            text(b.ops, cTitle, y1, missionui::elide(ttl, kBody, wTitle - 10.f), kBody, s.state == mission::StepState::Cancelled || s.state == mission::StepState::Skipped ? Tone::Muted : Tone::Text);
            text(b.ops, cTitle, y2, missionui::elide(std::string(mission::categoryLabel(s.category)), kMeta, wTitle - 10.f), kMeta, Tone::Muted);
            (void)chip(b.ops, cState, y1 - 2.f, missionui::elide(label, kMeta, wState - 24.f), tone);
            if (!s.note.empty()) text(b.ops, cState, y2 + 2.f, missionui::elide(s.note, kMeta, wState - 10.f), kMeta, Tone::Muted);
            if (!s.error.empty()) {
                text(b.ops, cDetail, y1 + 1.f, missionui::elide(s.detail.empty() ? s.title : s.detail, kMeta, wDetail - 10.f), kMeta, Tone::Text);
                text(b.ops, cDetail, y2 + 2.f, missionui::elide(s.error, kMeta, wDetail - 10.f), kMeta, Tone::Error);
            } else {
                const auto d = missionui::wrap(s.detail, kMeta, wDetail - 10.f, 2);
                float dy = y1 + 1.f;
                for (const auto& l : d) {
                    text(b.ops, cDetail, dy, l, kMeta, Tone::Text);
                    dy += lineH(kMeta);
                }
            }
            text(b.ops, cDeps, y1 + 1.f, missionui::elide(deps, kMeta, wDeps - 10.f), kMeta, Tone::Text);
            if (!s.implicit.empty()) text(b.ops, cDeps, y2 + 2.f, missionui::elide("dont implicites : " + joined(s.implicit), kMeta, wDeps - 10.f), kMeta, Tone::Muted);
            if (sp.counted()) {
                bar(b.ops, {cProg, y1 + 5.f, 60.f, 6.f}, sp.frac(), progTone);
                text(b.ops, cProg + 68.f, y1 + 1.f, sp.text(), kMeta, Tone::Text, true);
            } else {
                text(b.ops, cProg, y1 + 1.f, sp.text(), kMeta, Tone::Muted);
            }
            text(b.ops, cProg, y2 + 2.f, missionui::elide(ops, kMeta, wProg - 10.f), kMeta, Tone::Muted);
            // ce qu'elle fait maintenant, sur toute la largeur
            const float y3 = y2 + lineH(kMeta) + 2.f;
            text(b.ops, cN, y3, caret, kMeta, Tone::Muted);
            text(b.ops, cTitle, y3, missionui::elide(act.text, kMeta, x + w - 8.f - cTitle), kMeta, actTone, act.live);
        } else {
            // etroit : quatre lignes - le numero et le titre (l'etat a droite), le detail ou l'erreur, les dependances et la
            // progression, ce qu'elle fait maintenant
            const float cw2 = ui::measureWidth(label, kMeta) + 14.f;
            (void)chip(b.ops, x + w - cw2 - 4.f, y1 - 2.f, label, tone);
            text(b.ops, x + 8.f, y1, missionui::elide(s.id + "  " + ttl, kBody, w - cw2 - 24.f), kBody, Tone::Text, true);
            const std::string second = !s.error.empty() ? s.error : s.detail;
            text(b.ops, x + 8.f, y2, missionui::elide(second, kMeta, w - 16.f), kMeta, !s.error.empty() ? Tone::Error : Tone::Text);
            const float y3 = y2 + lineH(kMeta);
            float px = x + 8.f;
            if (sp.counted()) {
                bar(b.ops, {px, y3 + 5.f, 44.f, 6.f}, sp.frac(), progTone);
                px += 52.f;
            }
            text(b.ops, px, y3, missionui::elide(sp.text() + " (" + ops + ") \xC2\xB7 d\xC3\xA9pend de " + deps, kMeta, x + w - 8.f - px), kMeta, Tone::Muted);
            const float y4 = y3 + lineH(kMeta);
            text(b.ops, x + 8.f, y4, caret, kMeta, Tone::Muted);
            text(b.ops, x + 22.f, y4, missionui::elide(act.text, kMeta, w - 30.f), kMeta, actTone, act.live);
        }
        y += rowH;
        if (!open) continue;
        // LE JOURNAL DE L'ETAPE (le plus ancien d'abord ; les plus recents s'il est long)
        const std::size_t first = b.ops.size();
        const float jx = x + 8.f + (wide ? wN : 14.f), jw = x + w - 8.f - jx;
        const float dw = 92.f;
        float jy = y + 6.f;
        const auto journal = mission::stepJournal(m, s);
        constexpr std::size_t kMaxJournal = 60;
        text(b.ops, jx, jy, "Journal de " + s.id + " \xE2\x80\x94 " + missionui::count(static_cast<int>(journal.size()), "\xC3\xA9v\xC3\xA9nement", "\xC3\xA9v\xC3\xA9nements"), kMeta,
             Tone::Text, true);
        jy += lineH(kMeta) + 2.f;
        if (journal.empty())
            jy = wrapped(b.ops, jx, jy, jw, "Rien encore : aucune proposition ni op\xC3\xA9ration pour cette \xC3\xA9tape.", kMeta, Tone::Muted, 2);
        const std::size_t from = journal.size() > kMaxJournal ? journal.size() - kMaxJournal : 0;
        if (from > 0)
            jy = wrapped(b.ops, jx, jy, jw, "\xE2\x80\xA6 " + missionui::count(static_cast<int>(from), "\xC3\xA9v\xC3\xA9nement plus ancien", "\xC3\xA9v\xC3\xA9nements plus anciens"),
                         kMeta, Tone::Muted, 1);
        for (std::size_t i = from; i < journal.size(); ++i) {
            const auto& j = journal[i];
            const float jt = jy;
            text(b.ops, jx, jy, when(j.date), kMeta, Tone::Muted);
            const bool op = j.kind == "operation" || j.kind == "echec";
            jy = wrapped(b.ops, jx + dw + (op ? 14.f : 0.f), jy, jw - dw - (op ? 14.f : 0.f), j.text, kMeta, missionui::journalTone(j.kind), 2);
            if (jy < jt + lineH(kMeta)) jy = jt + lineH(kMeta);
            const std::string kind = j.kind == "operation" ? std::string("Op\xC3\xA9ration appliqu\xC3\xA9" "e")
                                     : j.kind == "echec"   ? std::string("Op\xC3\xA9ration en \xC3\xA9" "chec")
                                                           : missionui::eventLabel(j.kind);
            b.tips.emplace_back(gfx::Rect{jx, jt, jw, jy - jt}, (j.date.empty() ? std::string{} : j.date + " \xE2\x80\x94 ") + kind + "\n" + j.text);
        }
        // la carte de sa derniere proposition (plus bas)
        if (!s.proposals.empty())
            if (const auto* p = m.proposal(s.proposals.back())) {
                const std::string link = "Voir la carte de P" + std::to_string(p->order) + " \xE2\x80\xBA";
                text(b.ops, jx, jy + 2.f, link, kMeta, Tone::Accent);
                b.clicks.emplace_back(gfx::Rect{jx - 2.f, jy, ui::measureWidth(link, kMeta) + 6.f, lineH(kMeta) + 4.f}, "carte:" + p->id);
                jy += lineH(kMeta) + 4.f;
            }
        jy += 6.f;
        std::vector<Op> back;
        fill(back, {x + 4.f, y, w - 8.f, jy - y}, Tone::Card, 255, true);
        stroke(back, {x + 4.f, y, w - 8.f, jy - y}, Tone::Border, 160);
        b.ops.insert(b.ops.begin() + static_cast<std::ptrdiff_t>(first), back.begin(), back.end());
        y = jy + 4.f;
    }
    b.h = y + 6.f;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockProposals(const mission::Mission& m, float x, float w) {
    XPG_MESURE("ia.mission.propositions");   // 1.22.2 : les cartes des propositions (depliees ou reduites)
    Block b;
    b.section = "propositions";
    const auto& as = panel_.assistant();
    const std::string head = "PROPOSITIONS (" + std::to_string(m.proposals.size()) + ")";
    float y = heading(b.ops, x, 0.f, w, head);
    allOpen_ = allClose_ = {};
    if (m.proposals.empty()) {
        y = wrapped(b.ops, x, y, w, "Aucune proposition encore : \xC2\xAB Proposer la suite \xC2\xBB demande la premi\xC3\xA8re \xC3\xA0 l'IA (les \xC3\xA9tapes pr\xC3\xAAtes, regroup\xC3\xA9" "es).",
                    kMeta, Tone::Muted, 3);
        b.h = y + 4.f;
        return b;
    }
    // 1.22.2 : TOUT DEPLIER / TOUT REPLIER, a droite de l'intertitre (dessous s'il n'y a pas la place)
    {
        const std::string openAll = "Tout d\xC3\xA9plier", closeAll = "Tout replier";
        const float wo = ui::measureWidth(openAll, kMeta), wc = ui::measureWidth(closeAll, kMeta);
        float lx = x + w - 10.f - wc - 18.f - wo, ly = (kHeadH - ui::lineHeight(kMeta)) / 2.f;
        if (lx < x + 10.f + ui::measureWidth(head, kMeta) + 16.f) {
            lx = x + 10.f;
            ly = y - 4.f;
            y += lineH(kMeta) + 4.f;
        }
        text(b.ops, lx, ly, openAll, kMeta, Tone::Accent);
        text(b.ops, lx + wo + 18.f, ly, closeAll, kMeta, Tone::Accent);
        allOpen_ = {lx - 3.f, ly - 3.f, wo + 6.f, lineH(kMeta) + 6.f};
        allClose_ = {lx + wo + 15.f, ly - 3.f, wc + 6.f, lineH(kMeta) + 6.f};
        b.clicks.emplace_back(allOpen_, "cartes:ouvrir");
        b.clicks.emplace_back(allClose_, "cartes:fermer");
        b.tips.emplace_back(allOpen_, "D\xC3\xA9plier toutes les propositions (leur carte enti\xC3\xA8re).");
        b.tips.emplace_back(allClose_, "R\xC3\xA9" "duire toutes les propositions \xC3\xA0 une ligne (P1, le titre, les nombres, l'\xC3\xA9tat).");
    }
    std::vector<const mission::ProposalLink*> list;
    for (const auto& p : m.proposals) list.push_back(&p);
    std::stable_sort(list.begin(), list.end(), [](const auto* a, const auto* c) { return a->order < c->order; });
    const float cx = x + 12.f, cw = w - 24.f;
    for (const auto* pp : list) {
        const auto& p = *pp;
        const std::string tag = "P" + std::to_string(p.order);
        const std::string key = p.order > 0 ? tag : p.id;
        const std::optional<bool> chosen = panel_.missionCard(missionId_, p.id);
        const bool open = chosen ? *chosen : missionui::cardOpenByDefault(p.state);
        const float top = y;
        const std::size_t first = b.ops.size();   // le cadre se pose sous la carte, sa hauteur connue (a la fin)
        std::vector<Op> card;
        float cy = top + 10.f;
        // L'EN-TETE (un clic : deplier / reduire) : le chevron, P2, le titre (reduite : et ses nombres), l'etat
        text(b.ops, cx, cy, open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", kBody, Tone::Muted);
        const float px = cx + 16.f;
        const float tw = chip(b.ops, px, cy - 2.f, tag, Tone::Accent);
        const std::string st = missionui::proposalStateLabel(p.state);
        const float sw = ui::measureWidth(st, kMeta) + 14.f;
        (void)chip(b.ops, cx + cw - sw, cy - 2.f, st, missionui::proposalTone(p.state));
        const std::string ttl = p.title.empty() ? std::string("Proposition ") + tag : p.title;
        const std::string line = missionui::proposalLine(p);
        const std::string shownTitle = open ? ttl : ttl + " \xE2\x80\x94 " + line;
        text(b.ops, px + tw + 8.f, cy, missionui::elide(shownTitle, kBody, cw - 16.f - tw - sw - 20.f), kBody, Tone::Text, true);
        const float headH = open ? 10.f + lineH(kBody) + 4.f : 10.f + lineH(kBody) + 10.f;
        const gfx::Rect headRect{x, top, w, headH};
        b.clicks.emplace_back(headRect, "plier:" + p.id);
        b.tips.emplace_back(headRect, tag + " \xE2\x80\x94 " + ttl + " (" + st + ")\n" + line + "\n"
                                          + (open ? "Un clic : la r\xC3\xA9" "duire \xC3\xA0 une ligne." : "Un clic : la d\xC3\xA9plier (sa carte enti\xC3\xA8re).")
                                          + " Clic droit : D\xC3\xA9plier, R\xC3\xA9" "duire, Tout d\xC3\xA9plier, Tout replier.");
        cardHeads_[p.id] = headRect;
        cy += lineH(kBody) + 4.f;
        if (!open) {
            const float h = headH;
            fill(card, {x, top, w, h}, Tone::Card, 255, true);
            stroke(card, {x, top, w, h}, Tone::Border);
            b.ops.insert(b.ops.begin() + static_cast<std::ptrdiff_t>(first), card.begin(), card.end());
            anchors_[p.id] = top;
            y = top + h + 8.f;
            continue;
        }
        cy = wrapped(b.ops, cx, cy, cw, missionui::proposalCounts(p), kMeta, Tone::Text, 2);
        // les etapes qu'elle couvre, et celles dont elles dependent (hors de la proposition)
        std::vector<std::string> outside;
        for (const auto& sid : p.steps)
            if (const auto* s = m.step(sid))
                for (const auto& d : s->deps)
                    if (std::find(p.steps.begin(), p.steps.end(), d) == p.steps.end() && std::find(outside.begin(), outside.end(), d) == outside.end())
                        outside.push_back(d);
        std::string steps = "\xC3\x89tapes : " + (p.steps.empty() ? std::string("\xE2\x80\x94") : missionui::stepList(m, p.steps, 4));
        if (!outside.empty()) steps += " \xC2\xB7 d\xC3\xA9pend de : " + joined(outside);
        cy = wrapped(b.ops, cx, cy, cw, steps, kMeta, Tone::Muted, 2);
        std::string dates = "G\xC3\xA9n\xC3\xA9r\xC3\xA9" "e " + when(p.created);
        if (!p.appliedAt.empty()) dates += " \xC2\xB7 appliqu\xC3\xA9" "e " + when(p.appliedAt);
        if (p.state == "appliquee" || p.applied + p.failed + p.skipped > 0)
            dates += " \xC2\xB7 " + missionui::count(p.applied, "appliqu\xC3\xA9" "e", "appliqu\xC3\xA9" "es") + ", " + std::to_string(p.failed) + " en \xC3\xA9" "chec, "
                     + missionui::count(p.skipped, "ignor\xC3\xA9" "e", "ignor\xC3\xA9" "es");
        cy = wrapped(b.ops, cx, cy, cw, dates, kMeta, Tone::Muted, 2);
        for (std::size_t i = 0; i < p.errors.size() && i < 3; ++i) cy = wrapped(b.ops, cx, cy, cw, p.errors[i], kMeta, Tone::Error, 2);
        if (p.errors.size() > 3) cy = wrapped(b.ops, cx, cy, cw, "\xE2\x80\xA6 et " + std::to_string(p.errors.size() - 3) + " autres erreurs", kMeta, Tone::Error, 1);
        // le compte rendu reel (une proposition appliquee reste consultable, meme sa conversation supprimee)
        if (!p.report.isNull()) {
            const plan::Report rep = plan::reportFromJson(p.report);
            const auto names = [](const plan::Line& l) {
                std::string out = std::to_string(l.names.size()) + " " + l.category + " : ";
                for (std::size_t k = 0; k < l.names.size(); ++k) out += (k ? ", " : "") + l.names[k];
                return out;
            };
            std::size_t lines = 0;
            const auto add = [&](const std::string& s, Tone t) {
                if (lines >= 8) return;
                ++lines;
                cy = wrapped(b.ops, cx, cy, cw, s, kMeta, t, 2);
            };
            text(b.ops, cx, cy + 2.f, "Compte rendu", kMeta, Tone::Text, true);
            cy += lineH(kMeta) + 2.f;
            for (const auto& l : rep.created) add("Cr\xC3\xA9\xC3\xA9 \xE2\x80\x94 " + names(l), Tone::Ok);
            for (const auto& l : rep.modified) add("Modifi\xC3\xA9 \xE2\x80\x94 " + names(l), Tone::Text);
            for (const auto& l : rep.reused) add("R\xC3\xA9utilis\xC3\xA9 \xE2\x80\x94 " + names(l), Tone::Text);
            for (const auto& t : rep.todo) add("\xC3\x80 faire \xE2\x80\x94 " + t, Tone::Warning);
            for (const auto& f : rep.failures) add("Erreur \xE2\x80\x94 " + f, Tone::Error);
            if (!rep.verdict.empty()) add(rep.verdict, rep.failed + rep.skipped > 0 ? Tone::Warning : Tone::Ok);
        }
        // ses operations, une par ligne (un clic les deplie)
        if (!p.lines.empty()) {
            const bool opsShown = opsOpen_.count(p.id) > 0;
            const std::string link = opsShown ? std::string("Masquer les op\xC3\xA9rations")
                                          : "Voir les " + missionui::count(static_cast<int>(p.lines.size()), "op\xC3\xA9ration", "op\xC3\xA9rations") + " \xE2\x80\xBA";
            text(b.ops, cx, cy + 2.f, link, kMeta, Tone::Accent);
            b.clicks.emplace_back(gfx::Rect{cx - 2.f, cy, ui::measureWidth(link, kMeta) + 6.f, lineH(kMeta) + 4.f}, "ops:" + p.id);
            cy += lineH(kMeta) + 4.f;
            if (opsShown) {
                constexpr std::size_t kMaxLines = 120;
                for (std::size_t i = 0; i < p.lines.size() && i < kMaxLines; ++i) {
                    const auto& l = p.lines[i];
                    const auto [lab, tone] = opState(l.state);
                    const float lw = ui::measureWidth(lab, kMeta);
                    text(b.ops, cx + cw - lw, cy, lab, kMeta, tone);
                    std::string s = l.id + "  " + l.title + (l.step.empty() ? std::string{} : "  (" + l.step + ")");
                    text(b.ops, cx + 8.f, cy, missionui::elide(s, kMeta, cw - lw - 20.f), kMeta, Tone::Text);
                    std::string tip = s + " \xE2\x80\x94 " + lab;
                    if (!l.verb.empty()) tip += "\n" + l.verb;
                    if (!l.creates.empty()) tip += "\nCr\xC3\xA9" "e : " + joined(l.creates);
                    if (!l.error.empty()) tip += "\nErreur : " + l.error;
                    b.tips.emplace_back(gfx::Rect{cx, cy, cw, lineH(kMeta)}, tip);
                    cy += lineH(kMeta);
                    if (!l.error.empty()) cy = wrapped(b.ops, cx + 24.f, cy, cw - 24.f, l.error, kMeta, Tone::Error, 2);
                }
                if (p.lines.size() > kMaxLines) cy = wrapped(b.ops, cx + 8.f, cy, cw, "\xE2\x80\xA6 et " + std::to_string(p.lines.size() - kMaxLines) + " autres", kMeta, Tone::Muted, 1);
            }
        }
        if (!p.next.empty()) cy = wrapped(b.ops, cx, cy + 2.f, cw, "Suite pr\xC3\xA9vue par l'IA : " + p.next, kMeta, Tone::Muted, 3);
        // les gestes, par la conversation de la mission (ceux de la carte)
        cy += 6.f;
        const std::string mid = missionId_, pid = p.id;
        if (p.state == "generee" || p.state == "validee") {
            std::string why;
            const bool may = mission::mayApply(m, p.id, &why);
            float bx = cx;
            bx += useButton(b, key + ".apercu", "Pr\xC3\xA9visualiser\xE2\x80\xA6",
                            "L'aper\xC3\xA7u de la proposition (les objets, les propri\xC3\xA9t\xC3\xA9s avant \xE2\x86\x92 apr\xC3\xA8s, la vue dessin\xC3\xA9" "e) : rien ne change dans le projet.",
                            ui::Button::Style::Primary, true, {bx, cy}, [this, mid, pid] { (void)panel_.missionProposal(mid, pid, 0); }) + 6.f;
            bx += useButton(b, key + ".verifier", "V\xC3\xA9rifier",
                            "Sa carte dans la conversation de la mission : la validation, le plan d'op\xC3\xA9rations d\xC3\xA9pli\xC3\xA9, les erreurs et les avertissements.",
                            ui::Button::Style::Default, true, {bx, cy}, [this, mid, pid] { (void)panel_.missionProposal(mid, pid, 1); }) + 6.f;
            if (bx + ui::measureWidth("Appliquer", gfx::FontId{16}) + 28.f > cx + cw) {
                bx = cx;
                cy += kBtnH + 6.f;
            }
            (void)useButton(b, key + ".appliquer", "Appliquer",
                            may ? std::string("Appliquer au projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z) - les confirmations de la carte d'abord.")
                                : "Elle ne peut plus s'appliquer : " + why,
                            ui::Button::Style::Default, may && !as.applyingAny(), {bx, cy}, [this, mid, pid] { (void)panel_.missionProposal(mid, pid, 2); });
            cy += kBtnH + 4.f;
        } else if (as.indexOf(p.conversation) >= 0) {
            (void)useButton(b, key + ".voir", "Voir dans la conversation", "Sa carte dans la conversation de la mission (le plan, le compte rendu).",
                            ui::Button::Style::Flat, true, {cx, cy}, [this, mid, pid] { (void)panel_.missionProposal(mid, pid, 1); });
            cy += kBtnH + 4.f;
        } else if (!p.conversation.empty()) {
            cy = wrapped(b.ops, cx, cy, cw, "Sa conversation a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9" "e : ce compte rendu est celui gard\xC3\xA9 avec la mission.", kMeta, Tone::Muted, 2);
        }
        const float h = cy + 8.f - top;
        fill(card, {x, top, w, h}, Tone::Card, 255, true);
        stroke(card, {x, top, w, h}, Tone::Border);
        b.ops.insert(b.ops.begin() + static_cast<std::ptrdiff_t>(first), card.begin(), card.end());
        anchors_[p.id] = top;
        y = top + h + 8.f;
    }
    b.h = y;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockFollowing(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "suite";
    float y = heading(b.ops, x, 0.f, w, "SUITE PR\xC3\x89VUE");
    std::vector<std::string> left;
    for (const auto& s : m.steps) {
        using S = mission::StepState;
        if (mission::isFinished(s.state) || s.state == S::Cancelled || s.state == S::Running || s.state == S::AwaitingValidation || s.state == S::Applied
            || s.state == S::Verifying)
            continue;
        left.push_back(s.id);
    }
    if (left.empty()) {
        y = wrapped(b.ops, x, y, w, "Rien de plus de pr\xC3\xA9vu : chaque \xC3\xA9tape est termin\xC3\xA9" "e, ignor\xC3\xA9" "e ou couverte par une proposition.", kMeta, Tone::Muted, 2);
    } else {
        y = wrapped(b.ops, x, y, w, missionui::count(static_cast<int>(left.size()), "\xC3\xA9tape restante", "\xC3\xA9tapes restantes") + " : " + missionui::stepList(m, left, 12),
                    kMeta, Tone::Text, 4);
    }
    // ce que l'IA a dit de la suite (la derniere proposition)
    const mission::ProposalLink* last = nullptr;
    for (const auto& p : m.proposals)
        if (!last || p.order > last->order) last = &p;
    if (last && !last->next.empty()) y = wrapped(b.ops, x, y + 2.f, w, "Dite par l'IA (P" + std::to_string(last->order) + ") : " + last->next, kMeta, Tone::Muted, 3);
    b.h = y + 4.f;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockReport(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "rapport";
    const mission::FinalReport r = mission::reportFromJson(m.finalReport);
    float y = heading(b.ops, x, 0.f, w, "RAPPORT FINAL" + (r.date.empty() ? std::string{} : " \xE2\x80\x94 " + shortDate(r.date)));
    if (!r.verdict.empty()) y = wrapped(b.ops, x, y, w, r.verdict, kBody, r.complete ? Tone::Ok : Tone::Warning, 4, true) + 2.f;
    if (r.auditRun) {
        const Tone t = r.auditErrors > 0 ? Tone::Error : r.auditWarnings > 0 ? Tone::Warning : Tone::Ok;
        y = wrapped(b.ops, x, y, w,
                    "V\xC3\xA9rification globale du projet : " + missionui::count(r.auditErrors, "erreur", "erreurs") + ", "
                        + missionui::count(r.auditWarnings, "avertissement", "avertissements") + ".",
                    kMeta, t, 2);
    } else {
        y = wrapped(b.ops, x, y, w, "V\xC3\xA9rification globale du projet : non v\xC3\xA9rifiable (l'audit n'a pas pu \xC3\xAAtre fait) \xE2\x80\x94 rien n'est dit v\xC3\xA9rifi\xC3\xA9.",
                    kMeta, Tone::Warning, 2);
    }
    const auto list = [&](const char* label, const std::vector<std::string>& v, Tone t) {
        if (v.empty()) return;
        text(b.ops, x, y + 4.f, std::string(label) + " (" + std::to_string(v.size()) + ")", kMeta, Tone::Text, true);
        y += lineH(kMeta) + 4.f;
        y = wrapped(b.ops, x + 12.f, y, w - 12.f, joined(v), kMeta, t, 4);
    };
    list("Cr\xC3\xA9\xC3\xA9", r.created, Tone::Ok);
    list("Modifi\xC3\xA9", r.modified, Tone::Text);
    list("R\xC3\xA9utilis\xC3\xA9", r.reused, Tone::Text);
    list("\xC3\x89tapes termin\xC3\xA9" "es", r.finished, Tone::Text);
    list("Ignor\xC3\xA9" "es (fin forc\xC3\xA9" "e)", r.skipped, Tone::Warning);   // 1.22.2
    list("Non fait", r.notDone, Tone::Warning);
    list("Erreurs rencontr\xC3\xA9" "es", r.errors, Tone::Error);
    list("\xC3\x80 v\xC3\xA9rifier", r.toCheck, Tone::Warning);
    b.h = y + 6.f;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockResume(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "reprise";
    const auto& as = panel_.assistant();
    using S = mission::StepState;
    float y = heading(b.ops, x, 0.f, w, "REPRISE DE LA MISSION");
    // le dernier etat sauvegarde
    text(b.ops, x, y, "Dernier \xC3\xA9tat sauvegard\xC3\xA9 : " + when(m.modified), kBody, Tone::Text, true);
    y += lineH(kBody) + 2.f;
    if (!m.resumePoint.empty()) y = wrapped(b.ops, x, y, w, m.resumePoint, kMeta, Tone::Text, 3);
    if (!m.summary.empty()) y = wrapped(b.ops, x, y, w, m.summary, kMeta, Tone::Muted, 4);
    if (!inspected_ && m.pending.active && !busy_)   // le constat (fait) le dit dans son texte, plus bas
        y = wrapped(b.ops, x, y + 2.f, w,
                    "Un appel \xC3\xA0 l'IA \xC3\xA9tait en cours \xC3\xA0 la fermeture" + (m.pending.purpose.empty() ? std::string{} : " (" + m.pending.purpose + ")")
                        + " : il est marqu\xC3\xA9 interrompu ; sa proposition n'existe pas, rien n'est rejou\xC3\xA9.",
                    kMeta, Tone::Warning, 3);
    // les etapes : terminees, en cours, restantes
    std::vector<std::string> finished, running, left;
    for (const auto& s : m.steps) {
        if (s.state == S::Cancelled) continue;
        if (mission::isFinished(s.state)) finished.push_back(s.id);
        else if (s.state == S::Running || s.state == S::AwaitingValidation || s.state == S::Applied || s.state == S::Verifying) running.push_back(s.id);
        else left.push_back(s.id);
    }
    y += 6.f;
    text(b.ops, x, y,
         "\xC3\x89tapes : " + missionui::count(static_cast<int>(finished.size()), "termin\xC3\xA9" "e", "termin\xC3\xA9" "es") + " \xC2\xB7 "
             + std::to_string(running.size()) + " en cours \xC2\xB7 " + missionui::count(static_cast<int>(left.size()), "restante", "restantes"),
         kMeta, Tone::Text, true);
    y += lineH(kMeta) + 2.f;
    const auto group = [&](const char* label, const std::vector<std::string>& v, Tone t) {
        if (v.empty()) return;
        const float lw = ui::measureWidth(label, kMeta) + 8.f;
        text(b.ops, x + 12.f, y, label, kMeta, t);
        y = wrapped(b.ops, x + 12.f + lw, y, w - 12.f - lw, missionui::stepList(m, v, 8), kMeta, Tone::Text, 3);
    };
    group("Termin\xC3\xA9" "es :", finished, Tone::Ok);
    group("En cours :", running, Tone::Accent);
    group("Restantes :", left, Tone::Muted);
    // 1.22.1 : OU EN EST CHAQUE ETAPE - sa progression reelle et ce qu'elle fait maintenant
    if (!m.steps.empty()) {
        y += 6.f;
        text(b.ops, x, y, "O\xC3\xB9 en est chaque \xC3\xA9tape", kMeta, Tone::Text, true);
        y += lineH(kMeta) + 2.f;
        constexpr std::size_t kMaxSteps = 20;
        std::size_t n = 0;
        for (const auto& s : m.steps) {
            if (n == kMaxSteps) {
                y = wrapped(b.ops, x + 12.f, y, w - 12.f, "\xE2\x80\xA6 et " + missionui::count(static_cast<int>(m.steps.size() - kMaxSteps), "autre", "autres") + " (le tableau de bord les montre)",
                            kMeta, Tone::Muted, 1);
                break;
            }
            ++n;
            const mission::StepProgress sp = mission::stepProgressOf(m, s);
            const mission::StepActivity act = mission::activityOf(m, s, live_);
            if (sp.counted())
                bar(b.ops, {x + 12.f, y + 5.f, 44.f, 6.f}, sp.frac(),
                    s.state == S::Done ? Tone::Ok : s.state == S::Error ? Tone::Error : Tone::Info);
            const std::string line = s.id + " \xC2\xB7 " + sp.text() + " \xE2\x80\x94 " + act.text;
            text(b.ops, x + 64.f, y, missionui::elide(line, kMeta, w - 64.f), kMeta, missionui::activityTone(act.kind), act.live);
            b.tips.emplace_back(gfx::Rect{x, y, w, lineH(kMeta)}, s.id + "  " + s.title + " \xE2\x80\x94 " + mission::stepText(m, s) + "\n" + act.text + "\nProgression : "
                                                                      + sp.text() + " (" + sp.detail() + ")");
            y += lineH(kMeta);
        }
    }
    // les dernieres operations appliquees (la plus recente d'abord)
    y += 6.f;
    text(b.ops, x, y, "Derni\xC3\xA8res op\xC3\xA9rations appliqu\xC3\xA9" "es", kMeta, Tone::Text, true);
    y += lineH(kMeta) + 2.f;
    {
        std::vector<const mission::ProposalLink*> applied;
        for (const auto& p : m.proposals)
            if (p.state == "appliquee") applied.push_back(&p);
        std::stable_sort(applied.begin(), applied.end(), [](const auto* a, const auto* c) { return a->appliedAt > c->appliedAt; });
        std::size_t n = 0;
        for (const auto* p : applied)
            for (const auto& l : p->lines) {
                if (l.state != "appliquee" || n >= 8) continue;
                ++n;
                text(b.ops, x + 12.f, y, missionui::elide("P" + std::to_string(p->order) + " \xC2\xB7 " + l.id + "  " + l.title, kMeta, w - 12.f), kMeta, Tone::Text);
                y += lineH(kMeta);
            }
        if (n == 0) {
            for (const auto* p : applied) {   // une proposition appliquee sans le detail de ses operations : son resume
                if (n >= 8) break;
                ++n;
                text(b.ops, x + 12.f, y, missionui::elide("P" + std::to_string(p->order) + " \xC2\xB7 " + p->title + " (" + missionui::proposalCounts(*p) + ")", kMeta, w - 12.f),
                     kMeta, Tone::Text);
                y += lineH(kMeta);
            }
        }
        if (n == 0) y = wrapped(b.ops, x + 12.f, y, w - 12.f, "Aucune op\xC3\xA9ration appliqu\xC3\xA9" "e pour l'instant.", kMeta, Tone::Muted, 1);
    }
    // la coherence du projet
    y += 6.f;
    text(b.ops, x, y, "\xC3\x89tat du projet", kMeta, Tone::Text, true);
    const float hw = ui::measureWidth("\xC3\x89tat du projet", kMeta) + 12.f;
    const mission::Coherence co = inspected_ ? recovery_.coherence : mission::Coherence::Unknown;
    (void)chip(b.ops, x + hw, y - 3.f, inspected_ ? std::string(mission::coherenceLabel(co)) : std::string("pas encore v\xC3\xA9rifi\xC3\xA9"), missionui::coherenceTone(co));
    y += lineH(kMeta) + 6.f;
    if (!recovery_.text.empty()) y = wrapped(b.ops, x, y, w, recovery_.text, kMeta, Tone::Text, 5);
    if (co == mission::Coherence::Changed) {
        std::size_t n = 0;
        for (const auto& c : recovery_.changes) {
            if (n == 12) {
                y = wrapped(b.ops, x + 12.f, y, w - 12.f, "\xE2\x80\xA6 et " + std::to_string(recovery_.changes.size() - 12) + " autres changements", kMeta, Tone::Muted, 1);
                break;
            }
            ++n;
            y = wrapped(b.ops, x + 12.f, y, w - 12.f, "\xE2\x80\xA2 " + c, kMeta, Tone::Warning, 2);
        }
        if (recovery_.changes.empty()) y = wrapped(b.ops, x + 12.f, y, w - 12.f, "Le projet a chang\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde de la mission.", kMeta, Tone::Warning, 2);
    } else if (co == mission::Coherence::Missing) {
        y = wrapped(b.ops, x, y, w, "Le projet de la mission n'est pas celui qui est ouvert, ou il a \xC3\xA9t\xC3\xA9 d\xC3\xA9plac\xC3\xA9, renomm\xC3\xA9 ou supprim\xC3\xA9.",
                    kMeta, Tone::Error, 3);
        std::string where = m.project.name.empty() ? std::string{} : "\xC2\xAB " + m.project.name + " \xC2\xBB";
        if (!m.project.path.empty()) where += (where.empty() ? "" : ", ") + m.project.path;
        y = wrapped(b.ops, x, y, w, "La mission ne peut pas reprendre ici : ouvre son projet" + (where.empty() ? std::string{} : " (" + where + ")") + " pour la reprendre.",
                    kMeta, Tone::Error, 3);
        y = wrapped(b.ops, x, y, w, "Rien n'est propos\xC3\xA9 ni appliqu\xC3\xA9 sur un autre projet ; l'historique reste consultable.", kMeta, Tone::Muted, 2);
    }
    // la prochaine action
    y += 6.f;
    text(b.ops, x, y, "Prochaine action", kMeta, Tone::Text, true);
    y += lineH(kMeta) + 2.f;
    y = wrapped(b.ops, x, y, w, recovery_.nextAction.empty() ? mission::nextAction(m) : recovery_.nextAction, kBody, Tone::Text, 4) + 6.f;
    // les gestes
    const std::string mid = missionId_;
    float bx = x;
    const auto next = [&](float bw) { bx += bw + 6.f; };
    const bool over = m.state == mission::State::Done || m.state == mission::State::Cancelled;
    if (over) {
        y = wrapped(b.ops, x, y, w,
                    m.state == mission::State::Done ? std::string("Mission termin\xC3\xA9" "e : rien \xC3\xA0 reprendre (le tableau de bord garde son rapport).")
                                                     : std::string("Mission annul\xC3\xA9" "e : rien \xC3\xA0 reprendre (son historique reste consultable)."),
                    kMeta, Tone::Muted, 2) + 4.f;
    } else if (co == mission::Coherence::Missing) {
        next(useButton(b, "reprise.historique", "Consulter l'historique", "La chronologie de la mission (plus bas).", ui::Button::Style::Default, true, {bx, y},
                       [this] { showHistory(); }));
    } else if (co == mission::Coherence::Changed) {
        next(useButton(b, "reprise.actualiser", "Actualiser le contexte",
                       "L'IA relit le projet tel qu'il est et revoit le plan (les \xC3\xA9tapes termin\xC3\xA9" "es et les op\xC3\xA9rations appliqu\xC3\xA9" "es sont gard\xC3\xA9" "es). "
                       "\xC3\x80 faire avant de reprendre.",
                       ui::Button::Style::Primary, !as.generating(), {bx, y}, [this, mid] { (void)panel_.missionAct(mid, AiPanel::MissionAct::Refresh); }));
        if (m.state == mission::State::Paused)
            next(useButton(b, "reprise.reprendre", "Reprendre sans actualiser",
                           "Reprendre la mission sans relire le projet : les prochaines propositions partiront de l'\xC3\xA9tat r\xC3\xA9" "el du projet quand m\xC3\xAAme.",
                           ui::Button::Style::Default, true, {bx, y}, [this, mid] {
                               if (panel_.missionAct(mid, AiPanel::MissionAct::Resume)) setMode(Mode::Board);
                           }));
    } else if (m.state == mission::State::Paused || m.pending.active || recovery_.interrupted) {
        next(useButton(b, "reprise.reprendre", "Reprendre la mission",
                       "La mission repart de son dernier \xC3\xA9tat sauvegard\xC3\xA9 ; rien n'est appliqu\xC3\xA9 sans toi.", ui::Button::Style::Primary, !busy_, {bx, y},
                       [this, mid] {
                           if (panel_.missionAct(mid, AiPanel::MissionAct::Resume)) setMode(Mode::Board);
                       }));
    } else if (next_.kind != missionui::Next::Kind::None) {
        next(useButton(b, "action", next_.label, next_.tooltip, ui::Button::Style::Primary, !as.generating(), {bx, y}, [this] { doNext(); }));
    }
    if (bx + 200.f > x + w && bx > x) {
        bx = x;
        y += kBtnH + 6.f;
    }
    next(useButton(b, "reprise.verifier", "V\xC3\xA9rifier de nouveau", "Comparer de nouveau le projet ouvert \xC3\xA0 la derni\xC3\xA8re sauvegarde de la mission.",
                   ui::Button::Style::Flat, true, {bx, y}, [this] {
                       inspect();
                       rebuild();
                   }));
    y += kBtnH + 8.f;
    b.h = y;
    return b;
}

AiMissionBoard::Block AiMissionBoard::blockHistory(const mission::Mission& m, float x, float w) {
    Block b;
    b.section = "historique";
    float y = heading(b.ops, x, 0.f, w, "HISTORIQUE (" + missionui::count(static_cast<int>(m.history.size()), "\xC3\xA9v\xC3\xA9nement", "\xC3\xA9v\xC3\xA9nements") + ")");
    if (m.history.empty()) {
        y = wrapped(b.ops, x, y, w, "Aucun \xC3\xA9v\xC3\xA9nement.", kMeta, Tone::Muted, 1);
        b.h = y + 4.f;
        return b;
    }
    constexpr std::size_t kFirst = 40;
    const std::size_t n = historyAll_ ? m.history.size() : std::min(kFirst, m.history.size());
    const float dw = 130.f, kw = 110.f;
    for (std::size_t k = 0; k < n; ++k) {
        const auto& e = m.history[m.history.size() - 1 - k];   // le plus recent d'abord
        const float top = y;
        text(b.ops, x, y, when(e.date), kMeta, Tone::Muted);
        const std::string kind = missionui::eventLabel(e.kind);
        text(b.ops, x + dw, y, missionui::elide(kind, kMeta, kw - 8.f), kMeta, missionui::eventTone(e.kind), true);
        std::string t = e.text;
        if (!e.step.empty() || !e.proposal.empty()) {
            std::string ref = e.step;
            if (!e.proposal.empty()) {
                const auto* p = m.proposal(e.proposal);
                ref += (ref.empty() ? "" : " \xC2\xB7 ") + (p ? "P" + std::to_string(p->order) : e.proposal);
            }
            t += "  (" + ref + ")";
        }
        y = wrapped(b.ops, x + dw + kw, y, w - dw - kw, t, kMeta, e.kind == "erreur" ? Tone::Error : Tone::Text, 3);
        if (y < top + lineH(kMeta)) y = top + lineH(kMeta);
        b.tips.emplace_back(gfx::Rect{x, top, w, y - top}, e.date + " \xE2\x80\x94 " + kind + "\n" + e.text);
        y += 3.f;
    }
    if (m.history.size() > kFirst) {
        const std::string link = historyAll_ ? std::string("Montrer seulement les plus r\xC3\xA9" "cents")
                                             : "Afficher les " + std::to_string(m.history.size() - kFirst) + " \xC3\xA9v\xC3\xA9nements plus anciens \xE2\x80\xBA";
        text(b.ops, x, y + 2.f, link, kMeta, Tone::Accent);
        b.clicks.emplace_back(gfx::Rect{x - 2.f, y, ui::measureWidth(link, kMeta) + 6.f, lineH(kMeta) + 4.f}, "historique");
        y += lineH(kMeta) + 6.f;
    }
    // les decisions prises en chemin
    if (!m.decisions.empty()) {
        y += 6.f;
        text(b.ops, x, y, "D\xC3\xA9" "cisions (" + std::to_string(m.decisions.size()) + ")", kMeta, Tone::Text, true);
        y += lineH(kMeta) + 2.f;
        for (const auto& d : m.decisions) y = wrapped(b.ops, x + 12.f, y, w - 12.f, when(d.date) + " \xE2\x80\x94 " + d.question + " \xE2\x86\x92 " + d.answer, kMeta, Tone::Text, 3);
    }
    b.h = y + 8.f;
    return b;
}

// ------------------------------------------------------------------------------- la page ==
void AiMissionBoard::rebuild() {
    const auto& as = panel_.assistant();
    seenRev_ = as.missionsRevision();
    generating_ = as.generating();
    for (auto& [k, s] : slots_) s.used = false;
    blocks_.clear();
    anchors_.clear();
    cardHeads_.clear();   // 1.22.2
    allOpen_ = allClose_ = {};
    const auto bb = bounds();
    builtW_ = bb.w;
    const float w = std::min(kMaxW, std::max(300.f, bb.w - 2.f * kPad - 12.f));
    const float x = kPad;
    float y = 0.f;
    const auto push = [&](Block&& blk) {
        blk.y = y;
        y += blk.h + kGap;
        blocks_.push_back(std::move(blk));
    };
    const mission::Mission* m = as.findMission(missionId_);
    found_ = m != nullptr;
    stepRows_.clear();
    if (!m) {
        busy_ = false;
        progress_ = {};
        next_ = {};
        live_ = {};
        liveKey_ = 0;
        push(blockMissing(x, w));
    } else {
        name_ = missionui::title(*m);
        busy_ = panel_.missionBusy(*m);
        live_ = missionui::liveOf(as, *m);   // 1.22.1 : le moment (l'appel a l'IA, l'application de fond)
        liveKey_ = missionui::liveKey(live_);
        progress_ = mission::progressOf(*m);   // la progression REELLE (jamais un pourcentage invente)
        next_ = missionui::nextOf(*m, busy_);
        push(blockHeader(*m, x, w));
        if (mode_ == Mode::Resume) {
            push(blockResume(*m, x, w));
        } else {
            push(blockProgress(*m, x, w));
            push(blockGoal(*m, x, w));
            push(blockNext(*m, x, w));
            push(blockPlan(*m, x, w));
            push(blockProposals(*m, x, w));
            if (!m->steps.empty()) push(blockFollowing(*m, x, w));
            if (m->finalReport.isObject()) push(blockReport(*m, x, w));
        }
        push(blockHistory(*m, x, w));
        // les cartes des propositions : leur haut dans la page (un clic sur une etape y mene)
        for (const auto& blk : blocks_) {
            if (blk.section == "propositions") {
                for (auto& [pid, top] : anchors_) top += blk.y;
                for (auto& [pid, r] : cardHeads_) r.y += blk.y;   // 1.22.2 : l'en-tete de chaque carte, dans la page
                if (allOpen_.w > 0.f) allOpen_.y += blk.y;
                if (allClose_.w > 0.f) allClose_.y += blk.y;
            }
            if (blk.section == "plan")
                for (auto& [sid, r] : stepRows_) r.y += blk.y;   // 1.22.1 : les lignes des etapes, dans la page
        }
    }
    content_ = y + kPad;
    for (auto& [k, s] : slots_)
        if (!s.used && s.button) s.button->setVisibility(ui::Visibility::Collapsed);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - bb.h));
    placeButtons();
    invalidate();
}

void AiMissionBoard::placeButtons() {
    const auto b = bounds();
    for (const auto& blk : blocks_)
        for (const auto& [r, key] : blk.buttons) {
            const auto it = slots_.find(key);
            if (it == slots_.end() || !it->second.button) continue;
            const gfx::Rect at{b.x + r.x, b.y + blk.y - scroll_ + r.y, r.w, r.h};
            it->second.button->setBounds(at);
            const bool inside = at.y >= b.y && at.bottom() <= b.bottom() && at.right() <= b.right() + 0.5f;
            it->second.button->setVisibility(inside ? ui::Visibility::Visible : ui::Visibility::Hidden);
        }
}

void AiMissionBoard::scrollTo(float offset) {
    scroll_ = std::clamp(offset, 0.f, std::max(0.f, content_ - bounds().h));
    placeButtons();
    invalidate();
}

gfx::Rect AiMissionBoard::sectionRect(std::string_view key) const {
    const auto b = bounds();
    for (const auto& blk : blocks_)
        if (blk.section == key) return {b.x, b.y + blk.y - scroll_, b.w, blk.h};
    return {};
}

bool AiMissionBoard::toggleStep(const std::string& stepId) {
    const auto* m = panel_.assistant().findMission(missionId_);
    if (!m || !m->step(stepId)) return false;
    doClick("etape:" + stepId);
    return true;
}

gfx::Rect AiMissionBoard::stepRect(std::string_view stepId) const {
    const auto it = stepRows_.find(std::string(stepId));
    if (it == stepRows_.end()) return {};
    const auto b = bounds();
    return {b.x + it->second.x, b.y + it->second.y - scroll_, it->second.w, it->second.h};
}

// ---- 1.22.2 : les cartes des propositions, depliees ou reduites -------------------------------------------------------
bool AiMissionBoard::cardOpen(std::string_view proposalId) const {
    const std::string pid(proposalId);
    if (const std::optional<bool> chosen = panel_.missionCard(missionId_, pid)) return *chosen;
    const auto* m = panel_.assistant().findMission(missionId_);
    const auto* p = m ? m->proposal(pid) : nullptr;
    return p && missionui::cardOpenByDefault(p->state);
}

void AiMissionBoard::setCardOpen(const std::string& proposalId, bool open) {
    panel_.setMissionCard(missionId_, proposalId, open);
    rebuild();
}

bool AiMissionBoard::toggleCard(const std::string& proposalId) {
    const auto* m = panel_.assistant().findMission(missionId_);
    if (!m || !m->proposal(proposalId)) return false;
    setCardOpen(proposalId, !cardOpen(proposalId));
    return true;
}

void AiMissionBoard::setAllCards(bool open) {
    if (const auto* m = panel_.assistant().findMission(missionId_))
        for (const auto& p : m->proposals) panel_.setMissionCard(missionId_, p.id, open);
    rebuild();
}

gfx::Rect AiMissionBoard::cardHeadRect(std::string_view proposalId) const {
    const auto it = cardHeads_.find(std::string(proposalId));
    if (it == cardHeads_.end()) return {};
    const auto b = bounds();
    return {b.x + it->second.x, b.y + it->second.y - scroll_, it->second.w, it->second.h};
}

gfx::Rect AiMissionBoard::allCardsRect(bool open) const {
    const gfx::Rect r = open ? allOpen_ : allClose_;
    if (r.w <= 0.f) return {};
    const auto b = bounds();
    return {b.x + r.x, b.y + r.y - scroll_, r.w, r.h};
}

void AiMissionBoard::openCardMenu(const std::string& proposalId, gfx::Point at) {
    const auto* m = panel_.assistant().findMission(missionId_);
    const auto* p = m ? m->proposal(proposalId) : nullptr;
    if (!p) return;
    menuCard_ = proposalId;
    const bool open = cardOpen(proposalId);
    std::vector<ui::PopupMenu::Item> items;
    const auto item = [&](std::string label, ui::Icon icon, int id, bool enabled = true, std::string why = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.icon = icon;
        it.id = id;
        it.enabled = enabled;
        it.disabledReason = std::move(why);
        items.push_back(std::move(it));
    };
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = "P" + std::to_string(p->order) + " " + notifs::oneLine(p->title.empty() ? std::string("Proposition") : p->title, 60);
    head.shortcut = missionui::proposalStateLabel(p->state);
    items.push_back(head);
    item("D\xC3\xA9plier", ui::Icon::Expand, kMenuCardOpen, !open, "elle est d\xC3\xA9j\xC3\xA0 d\xC3\xA9pli\xC3\xA9" "e");
    item("R\xC3\xA9" "duire", ui::Icon::Collapse, kMenuCardClose, open, "elle est d\xC3\xA9j\xC3\xA0 r\xC3\xA9" "duite");
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    item("Tout d\xC3\xA9plier", ui::Icon::Expand, kMenuAllOpen);
    item("Tout replier", ui::Icon::Collapse, kMenuAllClose);
    menu_->setItems(std::move(items));
    menu_->openAtSurface(toSurface(at));
}

void AiMissionBoard::menuChosen(int id) {
    const std::string pid = std::exchange(menuCard_, std::string{});
    switch (id) {
        case kMenuCardOpen:
            if (!pid.empty()) setCardOpen(pid, true);
            break;
        case kMenuCardClose:
            if (!pid.empty()) setCardOpen(pid, false);
            break;
        case kMenuAllOpen: setAllCards(true); break;
        case kMenuAllClose: setAllCards(false); break;
        default: break;
    }
}

std::vector<std::string> AiMissionBoard::lines() const {
    std::vector<std::string> out;
    for (const auto& blk : blocks_)
        for (const auto& op : blk.ops)
            if ((op.kind == Op::Kind::Text || op.kind == Op::Kind::Bold || op.kind == Op::Kind::Chip) && !op.text.empty()) out.push_back(op.text);
    return out;
}

void AiMissionBoard::doClick(const std::string& what) {
    if (what == "demande") requestOpen_ = !requestOpen_;
    else if (what == "historique") historyAll_ = !historyAll_;
    else if (what == "rapport") pendingSection_ = "rapport";
    else if (what.rfind("ops:", 0) == 0) {
        const std::string pid = what.substr(4);
        if (!opsOpen_.erase(pid)) opsOpen_.insert(pid);
    } else if (what.rfind("etape:", 0) == 0) {
        // 1.22.1 : une etape - son journal deplie (ou replie)
        const std::string sid = what.substr(6);
        if (!stepsOpen_.erase(sid)) stepsOpen_.insert(sid);
    } else if (what.rfind("voir:", 0) == 0) {
        // l'etape du moment (l'en-tete) : son journal deplie, sa ligne du plan en vue
        const std::string sid = what.substr(5);
        stepsOpen_.insert(sid);
        rebuild();
        if (const auto it = stepRows_.find(sid); it != stepRows_.end()) scrollTo(it->second.y - 8.f);
        return;
    } else if (what.rfind("carte:", 0) == 0) {
        // la carte d'une proposition (plus bas) - 1.22.2 : depliee (on y va pour la lire)
        const std::string pid = what.substr(6);
        if (!cardOpen(pid)) {
            panel_.setMissionCard(missionId_, pid, true);
            rebuild();
        }
        if (const auto it = anchors_.find(pid); it != anchors_.end()) scrollTo(it->second - 8.f);
        return;
    } else if (what.rfind("plier:", 0) == 0) {
        // 1.22.2 : l'en-tete d'une carte - depliee / reduite (le choix est garde pour la seance)
        const std::string pid = what.substr(6);
        panel_.setMissionCard(missionId_, pid, !cardOpen(pid));
    } else if (what == "cartes:ouvrir" || what == "cartes:fermer") {
        setAllCards(what == "cartes:ouvrir");
        return;
    }
    rebuild();
    if (!pendingSection_.empty()) {
        if (const gfx::Rect r = sectionRect(pendingSection_); r.h > 0.f) scrollTo(scroll_ + (r.y - bounds().y) - 8.f);
        pendingSection_.clear();
    }
}

void AiMissionBoard::onLayout() {
    menu_->setBounds(bounds());   // 1.22.2 : le menu du clic droit (il se place lui-meme a l'ouverture)
    if (std::fabs(bounds().w - builtW_) > 0.5f) rebuild();
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - bounds().h));
    if (!pendingSection_.empty() && bounds().h > 0.f) {
        if (const gfx::Rect r = sectionRect(pendingSection_); r.h > 0.f) scroll_ = std::clamp(scroll_ + (r.y - bounds().y) - 8.f, 0.f, std::max(0.f, content_ - bounds().h));
        pendingSection_.clear();
    }
    placeButtons();
}

void AiMissionBoard::onPaint(const ui::PaintContext& ctx) {
    XPG_MESURE("ia.peinture.tableau-mission");   // 1.22.2
    tips_.clear();
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    ctx.r.pushClip(b);
    for (const auto& blk : blocks_) {
        const float top = b.y + blk.y - scroll_;
        if (top + blk.h < b.y || top > b.bottom()) continue;   // hors de la vue : rien
        missionui::paintOps(ctx, blk.ops, {b.x, top});
        for (const auto& [r, t] : blk.tips) tips_.emplace_back(gfx::Rect{b.x + r.x, top + r.y, r.w, r.h}, t);
    }
    ctx.r.popClip();
    bar_.paint(ctx, b, content_, b.h, scroll_);
}

std::string AiMissionBoard::liveTooltip(gfx::Point mouse) const {
    for (auto it = tips_.rbegin(); it != tips_.rend(); ++it)
        if (it->first.contains(mouse)) return it->second;
    return {};
}

ui::EventResult AiMissionBoard::onEvent(const ui::InputEvent& ev) {
    const auto b = bounds();
    if (bar_.handle(*this, ev, b, content_, b.h, scroll_)) {
        placeButtons();
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && b.contains(w->pos) && !w->mods.ctrl) {
        scrollTo(scroll_ - w->dy * 48.f);
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && b.contains(d->pos)) {
        takeFocus();
        if (d->button == ui::MouseButton::Right) {
            // 1.22.2 : le clic droit sur l'en-tete d'une carte de proposition - Deplier, Reduire, Tout deplier, Tout replier
            for (const auto& [pid, r] : cardHeads_)
                if (gfx::Rect{b.x + r.x, b.y + r.y - scroll_, r.w, r.h}.contains(d->pos)) {
                    const std::string go = pid;   // une copie : le menu peut refaire la page
                    openCardMenu(go, d->pos);
                    return ui::EventResult::Consumed;
                }
            return ui::EventResult::Consumed;
        }
        if (d->button != ui::MouseButton::Left) return ui::EventResult::Consumed;
        for (const auto& blk : blocks_) {
            const float top = b.y + blk.y - scroll_;
            for (const auto& [r, what] : blk.clicks)
                if (gfx::Rect{b.x + r.x, top + r.y, r.w, r.h}.contains(d->pos)) {
                    const std::string go = what;   // une copie : le clic refait la page
                    doClick(go);
                    return ui::EventResult::Consumed;
                }
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !k->mods.ctrl && !k->mods.alt) {
        switch (k->key) {
            case ui::Key::PageDown: scrollTo(scroll_ + b.h * 0.85f); return ui::EventResult::Consumed;
            case ui::Key::PageUp: scrollTo(scroll_ - b.h * 0.85f); return ui::EventResult::Consumed;
            case ui::Key::Down: scrollTo(scroll_ + 40.f); return ui::EventResult::Consumed;
            case ui::Key::Up: scrollTo(scroll_ - 40.f); return ui::EventResult::Consumed;
            case ui::Key::Home: scrollTo(0.f); return ui::EventResult::Consumed;
            case ui::Key::End: scrollTo(content_); return ui::EventResult::Consumed;
            default: break;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
