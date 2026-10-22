// =============================================================================
//  hmi/HmiRuntimeSlow.cpp - 1.12.17 : les performances dans le temps, les lenteurs
// -----------------------------------------------------------------------------
//  CE QUI TOURNE, L'UN DANS L'AUTRE. Un geste de l'operateur, un script, l'affichage
//  d'une vue, l'ouverture d'une popup, une valeur calculee : chacun ouvre un cadre
//  (slowBegin) et le ferme (slowEnd). Ferme, il dit sa duree a son temps habituel ;
//  dans un autre cadre, il devient une ligne de " Dedans " ; seul, s'il est lent, il
//  part au journal des lenteurs. Le cycle IHM, a part : ses postes (scripts,
//  alarmes, courbes, le reste) et ses depassements. Voir HmiPerfHistory.hpp.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiComm.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace hmi {

namespace {

std::string lowerFirst(std::string s) {
    if (!s.empty() && static_cast<unsigned char>(s[0]) < 0x80) s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

void Runtime::resetPerfHistory() {
    perfHistory_.reset(now_);
    if (project_) perfHistory_.setPeriod(std::max(10, project_->config.cycleMs));   // la periode, des le depart
}

void Runtime::slowBegin(SlowKind kind, std::string key, std::string what, std::string where, Id view, Id object, Id script) {
    SlowFrame f;
    f.kind = kind;
    f.key = std::move(key);
    f.what = std::move(what);
    f.where = std::move(where);
    f.view = view;
    f.object = object;
    f.script = script;
    f.t0 = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    slowStack_.push_back(std::move(f));
}

void Runtime::slowEnd() {
    if (slowStack_.empty()) return;
    SlowFrame f = std::move(slowStack_.back());
    slowStack_.pop_back();
    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double ms = static_cast<double>(nowNs - f.t0) / 1e6;
    const double usual = usual_.usual(f.key);
    const bool slow = slowRules_.slow(ms, usual);
    usual_.add(f.key, ms, slow);
    // Dans un autre cadre : une ligne de son " Dedans " (et ce qu'il y avait dedans, en retrait).
    if (!slowStack_.empty()) {
        auto& parent = slowStack_.back();
        constexpr std::size_t kParts = 60;
        if (parent.parts.size() < kParts) {
            parent.parts.push_back({f.what, ms, 0});
            for (auto& p : f.parts)
                if (parent.parts.size() < kParts) parent.parts.push_back({std::move(p.what), p.ms, p.depth + 1});
        }
        return;
    }
    if (!slow || !running_) return;
    SlowEntry e;
    e.kind = f.kind;
    e.ms = ms;
    e.usual = usual;
    e.what = f.what;
    e.where = f.where;
    e.viewId = f.view;
    e.objectId = f.object;
    e.scriptId = f.script;
    e.runs = usual_.runs(f.key);
    e.slowRuns = usual_.slowRuns(f.key);
    // Un geste dit ce qu'il a declenche : " Clic sur Btn_Regler -> ouvrir Pop_Reglage ".
    if (f.kind == SlowKind::Action)
        for (const auto& p : f.parts)
            if (p.depth == 0 && (p.what.rfind("Ouvrir ", 0) == 0 || p.what.rfind("Afficher ", 0) == 0)) {
                std::string target = p.what;
                if (const auto paren = target.find(" ("); paren != std::string::npos) target.resize(paren);
                e.what += " \xE2\x86\x92 " + lowerFirst(target);
                break;
            }
    e.inside = std::move(f.parts);
    slowNote(std::move(e));
}

void Runtime::slowNote(SlowEntry e) {
    e.at = now_;
    e.clock = wallClockMs();
    e.user = user_;
    if (const auto* v = viewOf(current_)) e.view = v->name;
    perfHistory_.slow();
    slowLog_.add(std::move(e));
}

void Runtime::cyclePost(const std::string& key, const std::string& label, double ms) {
    perfHistory_.post(key, label, ms);
    cycleParts_.push_back({label, ms, 0});
}

// Une fois par seconde : les echanges de chaque equipement (ses requetes, sa reponse) ; une
// lecture lente est une lenteur (Equipement).
void Runtime::sampleExchanges(double now) {
    if (!project_ || (exchangeSampled_ >= 0 && now - exchangeSampled_ < 1.0)) return;
    exchangeSampled_ = now;
    for (const auto& eq : project_->equipments) {
        auto* lk = equipmentLink(eq.name);
        if (!lk) continue;
        const auto d = lk->diagnostics();
        const auto it = exchangeRequests_.find(eq.name);
        if (it == exchangeRequests_.end()) {                // la premiere fois : ce qu'il avait deja fait ne compte pas
            exchangeRequests_[eq.name] = d.requests;
            continue;
        }
        const std::uint64_t fresh = d.requests >= it->second ? d.requests - it->second : d.requests;
        it->second = d.requests;
        if (fresh == 0) continue;
        perfHistory_.exchanges(static_cast<std::uint32_t>(fresh), d.avgMs > 0 ? d.avgMs : d.lastMs);
        const double ms = d.cycleMs > 0 ? d.cycleMs : d.lastMs;
        if (ms <= 0) continue;
        // 1.12.26 : une lecture se juge PAR REQUETE (300 variables en 15 requetes de 15 ms : pas une
        // reponse lente - elle etait notee chaque seconde) ; une lecture plus longue que la periode de
        // l'equipement est toujours une lenteur (l'IHM ne suit plus).
        const std::size_t reqs = std::max<std::size_t>(1, d.requestsPerCycle);
        const double per = ms / static_cast<double>(reqs);
        const bool over = eq.periodMs > 0 && ms > eq.periodMs;
        const std::string key = "equipement:" + eq.name;
        const double usual = usual_.usual(key);   // par requete
        const bool slow = over || slowRules_.slow(per, usual);
        usual_.add(key, per, slow);
        if (!slow) continue;
        SlowEntry e;
        e.kind = SlowKind::Equipment;
        e.ms = ms;
        e.usual = usual * static_cast<double>(reqs);   // la meme lecture, au temps habituel par requete
        const std::string what = "Lecture de " + std::to_string(d.subscribed) + (d.subscribed > 1 ? " variables" : " variable") + " ("
                               + std::to_string(reqs) + (reqs > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte") + ")";
        e.what = over ? what + ", plus longue que la p\xC3\xA9riode (" + std::to_string(eq.periodMs) + " ms)"
                      : what + ", r\xC3\xA9ponse lente (" + slowMs(per) + " par requ\xC3\xAAte)";
        e.where = eq.name;
        e.equipment = eq.name;
        e.runs = usual_.runs(key);
        e.slowRuns = usual_.slowRuns(key);
        e.inside.push_back({"R\xC3\xA9ponse : la derni\xC3\xA8re " + slowMs(d.lastMs) + ", en moyenne " + slowMs(d.avgMs), d.lastMs, 0});
        if (d.requestsPerCycle > 0) e.inside.push_back({std::to_string(d.requestsPerCycle) + " requ\xC3\xAAtes par lecture", 0.0, 0});
        slowNote(std::move(e));
    }
}

} // namespace hmi
