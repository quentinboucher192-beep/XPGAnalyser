// =============================================================================
//  tests/hmi_trace_test.cpp - 1.11.17 (refonte des scripts, lot 0) : LA TRACE DE REFERENCE
// -----------------------------------------------------------------------------
//  bac/Armoire_Gaz tourne comme sur le poste : l'IHM (hmi::Runtime : scripts
//  generaux et de vue, fonctions, alarmes) une image par cycle du projet (50 ms),
//  40 secondes, et l'automate simule (sim::Runtime, MAST) une seconde a la fois,
//  une fois par seconde (son programme est lourd : 125 ms par cycle dans une
//  construction sans optimisation). L'IHM passe une fois par chaque vue (OnOpen,
//  OnCycle, OnClose) et appelle chaque script « Appel ». Chaque seconde : les cases
//  des variables IHM qui ont change, les alarmes qui ont change d'etat, le journal
//  (sans l'heure du poste).
//
//  La trace est comparee a tests/fixtures/trace/armoire_gaz.trace. La refonte des
//  scripts (lots 2 a 9 : les declarations deviennent des modeles) change leur
//  forme, pas l'execution : la trace doit rester la meme. Un ecart montre la
//  premiere ligne qui differe. XPG_TRACE_REFAIRE=1 reecrit la reference, apres un
//  changement voulu (la relire avant de la verser) - et l'execute deux fois : rien
//  ne doit dependre de l'horloge du poste.
//
//      hmi_trace_test <dossier du projet> <trace de reference>
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiSimData.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/sim/Runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

// Une valeur, toujours ecrite de la meme facon (un reel a 6 chiffres : les derniers
// bits d'un sinus ne font pas un ecart).
std::string text(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Real: {
            char b[64];
            std::snprintf(b, sizeof b, "%.6g", v.asReal());
            return b;
        }
        case sim::Type::String:  return "'" + v.asString() + "'";
        case sim::Type::Bool:    return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Unknown: return "?";
        default:                 return std::to_string(v.asInteger());
    }
}

// L'heure et la date du poste dans un message (SYS.Time, SYS.Date) : masquees.
std::string masked(const std::string& s) {
    static const std::regex time(R"(\b\d{2}:\d{2}:\d{2}(\.\d+)?\b)");
    static const std::regex date(R"(\b\d{4}-\d{2}-\d{2}\b)");
    static const std::regex day(R"(\b\d{2}/\d{2}/\d{4}\b)");
    return std::regex_replace(std::regex_replace(std::regex_replace(s, time, "hh:mm:ss"), date, "aaaa-mm-jj"), day, "jj/mm/aaaa");
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// La premiere ligne qui differe (1 : la premiere), 0 : aucune.
std::size_t firstDifference(const std::string& a, const std::string& b, std::string& la, std::string& lb) {
    std::istringstream ia(a), ib(b);
    std::size_t n = 0;
    while (true) {
        const bool ga = static_cast<bool>(std::getline(ia, la));
        const bool gb = static_cast<bool>(std::getline(ib, lb));
        ++n;
        if (!ga && !gb) return 0;
        if (!ga) la = "(fin)";
        if (!gb) lb = "(fin)";
        if (!ga || !gb || la != lb) return n;
    }
}

std::string trace(const std::string& folder) {
    std::ostringstream out;
    auto opened = project::ProjectStore::open(folder);
    check(opened.has_value() && opened.value().project, "le projet automate s'ouvre : " + folder);
    auto loaded = load(folder);
    check(loaded.has_value(), "le projet IHM s'ouvre");
    if (!opened.has_value() || !opened.value().project || !loaded.has_value()) return {};
    sim::Runtime plc(opened.value().project);
    check(static_cast<bool>(plc.prepare("MAST")), "l'automate simule est pret (MAST)");
    const Project p = std::move(loaded.value());

    // Les vues a visiter (pas les popups, symboles, modeles, entetes, pieds) et les scripts a appeler.
    std::vector<const View*> tour;
    for (const auto& v : p.views)
        if (v.role.empty() || v.role == "vue") tour.push_back(&v);
    std::vector<std::string> calls;
    for (const auto& sc : p.programs.scripts)
        if (sc.event == "Appel" && sc.lang == ScriptLang::ST) calls.push_back(sc.name);
    const int cycleMs = p.config.cycleMs > 0 ? p.config.cycleMs : 50;
    const int perSecond = 1000 / cycleMs;
    const int frames = 40 * perSecond;
    check(static_cast<int>(tour.size()) <= 38 && static_cast<int>(calls.size()) <= 38, "une vue et un appel par seconde tiennent en 40 s");
    out << "# xpg-trace 1 : " << p.config.name << ", " << frames << " images de " << cycleMs << " ms, " << tour.size()
        << " vues visitees, " << calls.size() << " script(s) Appel\n";

    Runtime rt;
    rt.bind(&p, &plc);
    rt.start(0.0);
    std::map<std::string, std::string> cells, states;
    // Le journal, lu a chaque image (il garde ses 500 dernieres lignes : plein, il perd les
    // plus anciennes ; la derniere lue s'y retrouve en partant de la fin).
    std::vector<std::string> pending;
    std::size_t seen = 0;
    const JournalEntry* lastSeen = nullptr;
    JournalEntry last;
    bool lost = false;
    const auto same = [](const JournalEntry& a, const JournalEntry& b) {
        return a.time == b.time && a.cycle == b.cycle && a.kind == b.kind && a.source == b.source && a.message == b.message;
    };
    const auto readJournal = [&] {
        const auto& j = rt.journal();
        std::size_t from = seen;
        if (lastSeen && (seen > j.size() || seen == 0 || !same(j[seen - 1], last))) {
            from = j.size() + 1;
            for (std::size_t k = j.size(); k-- > 0;)
                if (same(j[k], last)) { from = k + 1; break; }
            if (from > j.size()) { lost = true; from = 0; }
        }
        for (std::size_t i = from; i < j.size(); ++i)
            pending.push_back("journal " + j[i].kind + " [" + j[i].source + "] " + masked(j[i].message));
        seen = j.size();
        if (!j.empty()) { last = j.back(); lastSeen = &last; }
    };
    const auto sample = [&](double now) {
        char head[32];
        std::snprintf(head, sizeof head, "t=%.2f", now);
        out << head << "  vue=" << (p.view(rt.currentView()) ? p.view(rt.currentView())->name : std::string("-")) << "\n";
        for (const auto& c : rt.captureData()) {
            const std::string key = c.name + c.path;
            const std::string value = text(c.value);
            auto it = cells.find(key);
            if (it != cells.end() && it->second == value) continue;
            cells[key] = value;
            out << "  " << key << " = " << value << "\n";
        }
        std::map<std::string, std::string> now2;
        for (const auto& a : rt.alarms()) now2[a.name] = a.state();
        for (const auto& [name, state] : now2)
            if (states[name] != state) out << "  alarme " << name << " : " << state << "\n";
        for (const auto& [name, state] : states)
            if (!state.empty() && !now2.count(name)) out << "  alarme " << name << " : sortie\n";
        states = now2;
        // Les lignes qui se suivent a l'identique : une fois, avec leur nombre.
        for (std::size_t i = 0; i < pending.size();) {
            std::size_t k = i + 1;
            while (k < pending.size() && pending[k] == pending[i]) ++k;
            out << "  " << pending[i] << (k - i > 1 ? " (x" + std::to_string(k - i) + ")" : std::string{}) << "\n";
            i = k;
        }
        pending.clear();
    };
    readJournal();
    sample(0.0);
    for (int f = 1; f <= frames; ++f) {
        const double now = f * cycleMs / 1000.0;
        const int second = (f - 1) / perSecond, inSecond = (f - 1) % perSecond;
        if (inSecond == perSecond / 4 && second < static_cast<int>(calls.size())) {
            std::string why;
            const bool ok = rt.callScript(calls[static_cast<std::size_t>(second)], now, &why);
            pending.push_back("appel " + calls[static_cast<std::size_t>(second)] + (ok ? "" : " : " + why));
            readJournal();
        }
        if (inSecond == perSecond / 2 && second < static_cast<int>(tour.size())) {
            const bool ok = rt.navigate(tour[static_cast<std::size_t>(second)]->id, Transition{}, now);
            pending.push_back("vers " + tour[static_cast<std::size_t>(second)]->name + (ok ? "" : " : refus"));
            readJournal();
        }
        if (inSecond == 0) (void)plc.step(1000);
        rt.tick(now);
        readJournal();
        if (f % perSecond == 0) sample(now);
    }
    rt.stop(frames * cycleMs / 1000.0, "fin de la trace");
    check(!lost, "le journal suivi image par image : aucune ligne perdue");
    return out.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage : hmi_trace_test <dossier du projet> <trace de reference>\n");
        return 2;
    }
    std::printf("-- 1.11.17 : la trace de reference de %s\n", argv[1]);
    const std::string got = trace(argv[1]);
    check(!got.empty(), "une trace");
    const std::string reference = argv[2];
    if (const char* redo = std::getenv("XPG_TRACE_REFAIRE"); redo && *redo == '1') {
        // Deux fois la meme execution : la meme trace (rien ne depend de l'horloge du poste).
        check(trace(argv[1]) == got, "deux executions, la meme trace");
        std::ofstream(reference, std::ios::binary) << got;
        std::printf("  reference reecrite : %s (%zu octets)\n", reference.c_str(), got.size());
    } else {
        const std::string want = readAll(reference);
        check(!want.empty(), "la reference existe : " + reference + " (XPG_TRACE_REFAIRE=1 pour la faire)");
        std::string a, b;
        if (const std::size_t line = firstDifference(want, got, a, b); line && !want.empty())
            check(false, "la trace differe de la reference a la ligne " + std::to_string(line) + "\n    attendu : " + a + "\n    obtenu  : " + b);
        else
            check(true, "la trace est celle de la reference");
    }
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
