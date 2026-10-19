// =============================================================================
//  app/screens/SimulationWorkspace.cpp - lot API 8 : le Centre de simulation
// -----------------------------------------------------------------------------
//  LE DOSSIER SIMULATION DE L'ARBRE (entre IHM et Versions) et ses onglets :
//  Vue d'ensemble, Automate (l'onglet de l'API d'avant), IHM (la simulation de
//  l'IHM), Equipements (les jumeaux, les liaisons), Debogage (openSimDebug),
//  Forcages, Courbes, Journal.
//
//  A CHAQUE IMAGE (tickSimCenter, depuis Update) : la photo de l'automate, de
//  l'IHM et des equipements (simSnapshot), le calcul de ce qu'il faut en dire
//  (SimStatus), les pastilles de l'arbre, un instant de plus sur les courbes ;
//  quatre fois par seconde, le collecteur regarde ce qui a change (les forcages,
//  les fonctions non simulees, l'IHM, les liaisons) et l'ecrit au journal
//  (App::simJournal). Les changements d'etat de l'automate, les points d'arret
//  et les modifications en ligne arrivent par les signaux de SimulationHost.
//
//  Les volets (SimCenter.hpp) dessinent le modele et demandent des cles
//  (simCenterGo) : un onglet, une ligne, une variable, Simuler...
// =============================================================================
#include "Screens.hpp"
#include "../../core/Edition.hpp"   // 1.12.14 : XPGAnalyser IHM - la simulation de l'IHM seule

#include "../ApiPanes.hpp"
#include "../App.hpp"
#include "../SimCenter.hpp"
#include "../SimDebugPane.hpp"          // claimsKey : l'onglet Debogage prend F5, F10, F11
#include "../SimJournal.hpp"
#include "../SimStatus.hpp"
#include "../SimulationPane.hpp"
#include "../TutorialsLot8.hpp"          // Lot API 8 : didacticiels et aide (Aide : la page de l'onglet)
#include "../TaskPanes.hpp"             // ApiHmiRead (hmiReadsOfPlc)
#include "../hmi/HmiAssist.hpp"
#include "../hmi/HmiCommPanes.hpp"
#include "../hmi/HmiPanels.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../../domain/ExecutionOrder.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../sim/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace app {

using NK = ProjectTreeModel::NodeKind;
namespace ss = simstatus;

namespace {

bool onScreenNow(const ui::Widget* w) {
    if (!w) return false;
    for (; w; w = w->parent())
        if (!w->visible()) return false;
    return true;
}

std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    return s;
}

// << line 42: ... >> : la ligne d'un message du moteur (0 : aucune).
std::uint32_t lineOfMessage(std::string_view m) {
    const auto at = m.find("line ");
    if (at == std::string_view::npos) return 0;
    std::uint32_t n = 0;
    for (std::size_t i = at + 5; i < m.size() && m[i] >= '0' && m[i] <= '9'; ++i) n = n * 10 + static_cast<std::uint32_t>(m[i] - '0');
    return n;
}

// Le volet d'un onglet du Centre (dans son cadre).
template <typename Pane>
Pane* centerPaneOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<Pane*>(&frame->content()) : nullptr;
}

const char* const kCenterKeys[] = {"ensemble", "forcages", "courbes", "journal"};

} // namespace

// ============================================================ l'etat du Centre ====
struct MainAnalysisScreen::SimCenterState {
    std::shared_ptr<SimCenterModel> model = std::make_shared<SimCenterModel>();
    core::ConnectionScope hostLinks;             // les signaux de SimulationHost
    const SimulationHost* watched{nullptr};
    std::string           projectKey{"\x01"};   // le projet suivi : un autre, tout repart
    // l'automate
    int                   state{-1};
    bool                  attached{false};
    std::uint64_t         generation{~std::uint64_t{0}};
    std::uint64_t         scanSeen{0};
    std::size_t           unknownSeen{0};
    std::set<std::string> diagSeen;              // les avertissements deja dits (ce runtime)
    std::string           prepareError;          // Simuler a echoue : pourquoi
    std::uint64_t         onlineScan{~std::uint64_t{0}};
    double                onlineAt{-1};
    struct Forced {
        std::string       name, value, who;
        SimForcing::Source source{SimForcing::Source::Automate};
        double            since{0};
    };
    std::map<std::string, Forced> forced;        // cle : le nom en minuscules
    std::map<std::string, double> twinSince;     // cle "twin:..." -> vu depuis
    bool                  twinPrimed{false};     // la premiere lecture ne dit rien (ce qui etait deja la)
    std::uint64_t         forcingSignature{0};
    // l'IHM
    bool                  hmiRunning{false};
    std::uint64_t         hmiView{0};
    std::string           hmiUser;
    std::map<std::uint64_t, std::string> alarms; // actives : id -> message
    double                hmiJournalTime{-1};
    std::uint64_t         slowSeen{0};           // 1.12.17 : la derniere lenteur passee au journal
    std::size_t           scriptErrors{0};
    std::string           lastScriptError;
    std::size_t           plcReads{0};
    double                plcReadsAt{-100};
    // les equipements
    std::map<std::string, bool>          equipOk;
    std::map<std::string, std::uint64_t> requests;
    double                requestsAt{-1};
    double                exchangeRate{0};
    // le rythme
    double                nextPoll{0}, nextReport{0}, nextSample{0}, nextPaint{0}, nextHmiTick{0}, nextSlowPaint{0};
    bool                  reportDirty{true};
    std::uint64_t         reportShown{0};
    std::weak_ptr<ProjectTreeModel> badgeModel;
    std::string           badgesShown;
    // 1.12.17 : la Simulation depliee - quand, depuis quand chaque vue ou popup est a l'ecran
    // ("14:03:10"), les requetes de chaque equipement a la derniere fois (ses echanges par seconde).
    double                treeRowsAt{-10};
    bool                  treeRunning{false};
    std::map<std::uint64_t, std::string>                    shownSince;
    std::map<std::string, std::pair<std::uint64_t, double>> treeRequests;
};

// ------------------------------------------------------------ l'installation ----
void MainAnalysisScreen::ensureSimCenter() {
    if (!simCenter_) simCenter_ = std::make_shared<SimCenterState>();
    auto& st = *simCenter_;
    auto& host = app_.simulation();
    if (st.watched == &host) return;
    st.watched = &host;
    st.hostLinks.clear();
    // Les changements d'etat : demarrage, reprise, pause, un cycle, arret, halte.
    st.hostLinks += host.changed->connect([this] {
        if (!simCenter_) return;
        auto& s = *simCenter_;
        auto& h = app_.simulation();
        auto& j = app_.simJournal();
        j.setCycle(h.scanCount());
        const int now = h.attached() ? static_cast<int>(h.state()) : -1;
        const auto scan = h.scanCount();
        using State = SimulationHost::State;
        if (h.attached() && h.generation() != s.generation) {
            // Un programme (re)prepare : ce que le collecteur savait de l'autre ne vaut plus.
            s.generation = h.generation();
            s.unknownSeen = 0;
            s.diagSeen.clear();
            s.forced.clear();
            s.prepareError.clear();
            std::size_t entries = 0, sections = 0;
            if (const auto p = app_.project()) {
                entries = domain::executionOrder(*p, "MAST").size();
                for (const auto& t : p->tasks)
                    if (p->strings.text(t.name) == "MAST") sections = t.sections.size();
            }
            j.add(SimSource::Automate, SimSeverity::Info, "preparation",
                  "Programme pr\xC3\xA9par\xC3\xA9 : " + ss::plural(entries, "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + " de MAST, "
                      + ss::plural(sections, "section", "sections"),
                  {}, "automate");
        }
        if (now != s.state) {
            const int was = s.state;
            s.state = now;
            if (now == static_cast<int>(State::Running))
                j.add(SimSource::Automate, SimSeverity::Ok, was == static_cast<int>(State::Paused) ? "reprise" : "demarrage",
                      was == static_cast<int>(State::Paused) ? "Reprise au cycle " + ss::thousands(scan) : "Simuler : le programme tourne", {},
                      "automate");
            else if (now == static_cast<int>(State::Paused) && was == static_cast<int>(State::Running))
                j.add(SimSource::Automate, SimSeverity::Info, "pause", "Pause au cycle " + ss::thousands(scan), {}, "automate");
            else if (now == static_cast<int>(State::Paused))
                j.add(SimSource::Automate, SimSeverity::Info, "cycle", "Un cycle : cycle " + ss::thousands(scan), {}, "automate");
            else if (now == static_cast<int>(State::Stopped) && was >= 0 && was != static_cast<int>(State::Stopped))
                j.add(SimSource::Automate, SimSeverity::Info, "arret", "Arr\xC3\xAAt : tout revient \xC3\xA0 z\xC3\xA9ro", {}, "automate");
            else if (now == static_cast<int>(State::Halted)) {
                std::string reason, section;
                std::uint32_t line = 0;
                for (const auto& d : h.lastDiagnostics())
                    if (d.severity == sim::Diagnostic::Severity::Error) {
                        reason = SimulationPane::frenchMessage(d.message);
                        section = d.section;
                        line = d.line ? d.line : lineOfMessage(d.message);
                        break;
                    }
                if (reason.empty()) reason = SimulationPane::frenchMessage(h.haltMessage());
                j.add(SimSource::Automate, SimSeverity::Error, "halte", "Halte au cycle " + ss::thousands(scan) + " : " + reason, {},
                      section.empty() ? std::string("automate") : "ligne:" + section + ":" + std::to_string(line));
            }
        } else if (now == static_cast<int>(State::Paused) && scan > s.scanSeen && scan - s.scanSeen == 1) {
            j.add(SimSource::Automate, SimSeverity::Info, "cycle", "Un cycle : cycle " + ss::thousands(scan), {}, "automate");
        }
        s.scanSeen = scan;
        if (s.attached != h.attached()) {
            s.attached = h.attached();
            if (!s.attached) s.forced.clear();
        }
        s.reportDirty = true;
    });
    // Ce que dit le simulateur pendant un cycle : un avertissement, une fois.
    st.hostLinks += host.scanned->connect([this](const sim::ScanReport& report) {
        if (!simCenter_ || report.diagnostics.empty()) return;
        auto& s = *simCenter_;
        for (const auto& d : report.diagnostics) {
            if (d.severity != sim::Diagnostic::Severity::Warning) continue;
            const std::string key = d.section + "|" + std::to_string(d.line) + "|" + d.message;
            if (s.diagSeen.size() > 200 || !s.diagSeen.insert(key).second) continue;
            const auto line = d.line ? d.line : lineOfMessage(d.message);
            app_.simJournal().add(SimSource::Automate, SimSeverity::Warning, "simulateur",
                                  "Le simulateur : " + SimulationPane::frenchMessage(d.message)
                                      + (d.section.empty() ? std::string{} : " (" + SimulationPane::frenchPlace(d.section, line) + ")"),
                                  {}, d.section.empty() ? std::string("automate") : "ligne:" + d.section + ":" + std::to_string(line));
        }
    });
    // Le moteur : un point d'arret vient de mettre en pause ; une modification en ligne.
    st.hostLinks += host.breakHit->connect([this] {
        if (!simCenter_) return;
        const auto& hit = app_.simulation().lastBreakHit();
        if (hit)
            app_.simJournal().add(SimSource::Debogage, SimSeverity::Info, "point-arret",
                                  "Point d'arr\xC3\xAAt : " + hit->section + ", ligne " + std::to_string(hit->line) + " (cycle " + ss::thousands(hit->scan) + ")",
                                  {}, "ligne:" + hit->section + ":" + std::to_string(hit->line));
        simCenter_->reportDirty = true;
    });
    st.hostLinks += host.onlineChanged->connect([this] {
        if (!simCenter_) return;
        const auto& oc = app_.simulation().lastOnlineChange();
        if (oc) {
            simCenter_->onlineScan = oc->atScan;
            simCenter_->onlineAt = app_.simJournal().now();
            app_.simJournal().add(SimSource::Automate, oc->failed ? SimSeverity::Error : SimSeverity::Info, "en-ligne",
                                  (oc->failed ? "Modification en ligne refus\xC3\xA9" "e : " : "Modification en ligne : ")
                                      + (oc->summary.empty() ? std::to_string(oc->kept) + " gard\xC3\xA9" "es, " + std::to_string(oc->added) + " ajout\xC3\xA9" "es, "
                                                                   + std::to_string(oc->dropped) + " retir\xC3\xA9" "es"
                                                             : oc->summary),
                                  {}, "journal");
        }
        simCenter_->reportDirty = true;
    });
}

// ------------------------------------------------------------------ le rythme ----
void MainAnalysisScreen::tickSimCenter(double now) {
    ensureSimCenter();
    auto& st = *simCenter_;
    auto& journal = app_.simJournal();
    auto& host = app_.simulation();
    const double t = journal.now();
    st.model->journal = &journal;
    // Un autre projet : le journal, les courbes et ce qu'on savait repartent de zero.
    const auto project = app_.project();
    const std::string key = app_.projectFolder() + "|" + (project ? project->header.projectName : std::string{});
    if (key != st.projectKey) {
        const bool first = st.projectKey == "\x01";
        st.projectKey = key;
        if (!first || !journal.empty()) journal.clear();
        st.model->clearTrends();
        st.forced.clear();
        st.twinSince.clear();
        st.twinPrimed = false;
        st.alarms.clear();
        st.equipOk.clear();
        st.requests.clear();
        st.hmiRunning = false;
        st.hmiView = 0;
        st.hmiUser.clear();
        st.scriptErrors = 0;
        st.lastScriptError.clear();
        st.plcReadsAt = -100;
        st.prepareError.clear();
        st.onlineAt = -1;
        if (project)
            journal.add(SimSource::Simulation, SimSeverity::Info, "projet",
                        "Projet ouvert : " + (app_.projectFolder().empty() ? project->header.projectName
                                                                           : std::filesystem::path(app_.projectFolder()).filename().string()),
                        "Le journal de la simulation repart de z\xC3\xA9ro avec ce projet.", {});
        st.reportDirty = true;
    }
    journal.setCycle(host.scanCount());
    if (t >= st.nextPoll) {
        st.nextPoll = t + 0.25;
        pollSimCenter(t);
        st.reportDirty = true;
    }
    if (st.reportDirty || t >= st.nextReport) {
        st.reportDirty = false;
        st.nextReport = t + 0.5;
        st.model->report = ss::compute(simSnapshot(t));
        ++st.model->revision;
        refreshSimBadges();
    }
    st.model->now = t;
    // Les courbes : un instant de plus, vingt fois par seconde au plus, tant que
    // l'automate tourne ou que l'IHM vit (a l'arret, rien ne bouge : la courbe
    // attend au lieu de s'allonger d'une droite).
    auto* hmiPane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
    const bool hmiLive = hmiPane && hmiPane->runtime().running();
    if (!st.model->trends().empty() && t >= st.nextSample
        && ((host.attached() && host.state() == SimulationHost::State::Running) || hmiLive)) {
        st.nextSample = t + 0.05;
        std::vector<std::optional<double>> values;
        std::vector<std::string> shown;
        auto* rt = host.attached() ? app_.simulationRuntime() : nullptr;
        for (const auto& tr : st.model->trends()) {
            sim::Value v;
            bool ok = false;
            if (!tr.hmi && rt) ok = rt->get(tr.path, v);
            else if (tr.hmi && hmiLive)
                if (const auto* hv = hmiPane->runtime().variable(tr.path)) {
                    v = *hv;
                    ok = true;
                }
            if (!ok) {
                values.emplace_back(std::nullopt);
                shown.emplace_back();
                continue;
            }
            values.emplace_back(v.type() == sim::Type::Bool ? (v.isTruthy() ? 1.0 : 0.0)
                                : v.type() == sim::Type::String ? std::numeric_limits<double>::quiet_NaN() : v.asReal());
            shown.push_back(v.display());
        }
        st.model->pushSample(t, values, shown);
    }
    // L'IHM avance quand la vue d'ensemble la montre (son onglet cache ne la
    // fait plus avancer) : dix fois par seconde.
    // 1.10 : l'IHM EN MARCHE AVANCE TOUJOURS, son onglet cache ou non (avant, elle
    // se figeait des qu'on changeait d'onglet, sauf sous la vue d'ensemble) : ses
    // scripts, ses timers, ses alarmes tournent comme ceux de l'API.
    auto* overview = centerPaneOf<SimOverviewPane>(apiTab("sim:ensemble"));
    // 1.10 (decision 12) : le plein ecran de l'IHM dont l'onglet a ete cache dessous
    // (Ctrl+Tab...) s'arrete : la fenetre redevient comme avant.
    if (hmiPane && hmiPane->fullScreen() && !onScreenNow(hmiPane)) hmiPane->setFullScreen(false);
    // 1.10.1 (C1) : a L'HORLOGE DES IMAGES (`now`, celle du dessin de l'onglet), pas a
    // celle du journal (`t`, steady_clock) : avec deux horloges, l'IHM revenue a l'ecran
    // restait figee a l'heure de l'autre (la vanne a 12 %, 55 % apres un changement
    // d'onglet), et l'onglet cache sautait de plusieurs jours (compteurs horaires,
    // deconnexion automatique, veille).
    if (hmiLive && !onScreenNow(hmiPane) && now >= st.nextHmiTick) {
        st.nextHmiTick = now + 0.1;
        hmiPane->refreshAt(now);
    }
    // Les volets a l'ecran se redessinent : la vue d'ensemble a 15 images par
    // seconde quand quelque chose y vit (les fleches, le voyant), sinon quand
    // son contenu change.
    if (overview && onScreenNow(overview)) {
        const auto& rep = st.model->report;
        const bool alive = rep.chain[0].alive || rep.chain[1].alive || rep.banner.tone == ss::Tone::Ok;
        if ((alive && t >= st.nextPaint) || st.reportShown != st.model->revision) {
            st.nextPaint = t + 1.0 / 15.0;
            st.reportShown = st.model->revision;
            overview->invalidate();
        }
    }
    if (auto* pane = centerPaneOf<SimForcingPane>(apiTab("sim:forcages")); pane && onScreenNow(pane)) {
        pane->refresh();
        if (t >= st.nextSlowPaint) pane->invalidate();
    }
    if (auto* pane = centerPaneOf<SimTrendsPane>(apiTab("sim:courbes")); pane && onScreenNow(pane)) pane->invalidate();
    if (auto* pane = centerPaneOf<SimJournalPane>(apiTab("sim:journal")); pane && onScreenNow(pane)) {
        pane->refresh();
        if (t >= st.nextSlowPaint) pane->invalidate();
    }
    if (t >= st.nextSlowPaint) st.nextSlowPaint = t + 1.0;
}

// ---------------------------------------------------------------- le collecteur ----
void MainAnalysisScreen::pollSimCenter(double t) {
    auto& st = *simCenter_;
    auto& journal = app_.simJournal();
    auto& host = app_.simulation();
    auto* rt = host.attached() ? app_.simulationRuntime() : nullptr;
    const auto doc = app_.hmi();
    auto* hmiPane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));

    // ---- l'automate : les fonctions non simulees, les forcages -----------------------
    if (rt) {
        const auto& unknown = rt->unknownCalls();
        for (std::size_t i = st.unknownSeen; i < unknown.size(); ++i) {
            const auto& u = unknown[i];
            journal.add(SimSource::Automate, host.continueOnUnknownCalls() ? SimSeverity::Warning : SimSeverity::Error, "inconnue",
                        u.name + (host.continueOnUnknownCalls() ? " n'est pas simul\xC3\xA9" "e : elle rend 0" : " n'est pas simul\xC3\xA9" "e : le cycle s'arr\xC3\xAAte dessus")
                            + (u.section.empty() ? std::string{} : " (" + SimulationPane::frenchPlace(u.section, u.line) + ")"),
                        {}, u.section.empty() ? std::string("automate") : "ligne:" + u.section + ":" + std::to_string(u.line));
        }
        st.unknownSeen = unknown.size();
    }
    std::map<std::string, SimCenterState::Forced> forcedNow;
    if (rt) {
        // Qui l'a posee : l'onglet devant quand elle est apparue.
        std::string who;
        auto source = SimForcing::Source::Automate;
        auto* page = centre_ && centre_->tabCount() ? centre_->page(centre_->currentIndex()) : nullptr;
        if (page && page == hmiTab("simulation")) {
            source = SimForcing::Source::Ihm;
            who = "l'IHM en simulation" + (st.hmiUser.empty() ? std::string{} : " (" + st.hmiUser + ")");
        } else if (page && page == apiTab("simulation")) who = "l'onglet Automate";
        else if (page && page == apiTab("tables")) who = "une table d'animation";
        else if (page && page == apiTab("sim:forcages")) who = "le Centre de simulation";
        else if (!runningTrail().empty()) who = "un parcours du didacticiel";
        else who = "la table de for\xC3\xA7" "age de l'automate";
        for (const auto& name : rt->forcedNames()) {
            const auto k = lowerOf(name);
            sim::Value v;
            const std::string value = rt->get(name, v) ? v.display() : std::string("?");
            const auto it = st.forced.find(k);
            if (it == st.forced.end()) {
                SimCenterState::Forced f{name, value, who, source, t};
                journal.add(source == SimForcing::Source::Ihm ? SimSource::Ihm : SimSource::Automate, SimSeverity::Warning, "forcage",
                            "Forc\xC3\xA9" "e : " + name + " = " + value + (who.empty() ? std::string{} : " (par " + who + ")"), {}, "forcages");
                forcedNow.emplace(k, std::move(f));
            } else {
                auto f = it->second;
                if (f.value != value) {
                    journal.add(f.source == SimForcing::Source::Ihm ? SimSource::Ihm : SimSource::Automate, SimSeverity::Warning, "forcage",
                                "Forc\xC3\xA9" "e : " + name + " = " + value, {}, "forcages");
                    f.value = value;
                }
                forcedNow.emplace(k, std::move(f));
            }
        }
        for (const auto& [k, f] : st.forced)
            if (!forcedNow.count(k))
                journal.add(f.source == SimForcing::Source::Ihm ? SimSource::Ihm : SimSource::Automate, SimSeverity::Info, "relache",
                            "Rel\xC3\xA2" "ch\xC3\xA9" "e : " + f.name, {}, "forcages");
        st.forced = std::move(forcedNow);
    }
    // Les forcages : ceux de l'automate, puis les cases des jumeaux.
    std::vector<SimForcing> rows;
    for (const auto& [k, f] : st.forced) {
        SimForcing row;
        row.source = f.source;
        row.where = f.source == SimForcing::Source::Ihm ? "IHM (automate)" : "Automate";
        row.what = f.name;
        row.value = f.value;
        row.who = f.who;
        row.since = f.since;
        row.sinceText = ss::duration(std::max(0.0, t - f.since));
        row.key = "plc:" + f.name;
        // ---- Lot API 8 : le moteur - le programme dirait (la case sous le forcage) ----
        if (rt) (void)host.unforcedValue(f.name, row.programSays);
        rows.push_back(std::move(row));
    }
    std::map<std::string, double> twinNow;
    if (doc)
        for (const auto& e : doc->project.equipments)
            for (const auto& f : e.forcings) {
                const std::string k = "twin:" + e.name + ":" + f.address + "|" + f.type;
                const auto seen = st.twinSince.find(k);
                const double since = seen == st.twinSince.end() ? t : seen->second;
                if (seen == st.twinSince.end() && st.twinPrimed)
                    journal.add(SimSource::Equipements, SimSeverity::Warning, "forcage",
                                "Case forc\xC3\xA9" "e dans l'esclave simul\xC3\xA9 de " + e.name + " : " + f.address + " = " + std::to_string(f.value), {}, "forcages");
                twinNow[k] = since;
                SimForcing row;
                row.source = SimForcing::Source::Equipement;
                row.where = e.twinLabel();
                row.what = f.address + " \xC2\xB7 " + f.type;
                char b[48];
                std::snprintf(b, sizeof b, "%g", f.value);
                row.value = b;
                row.who = "l'esclave simul\xC3\xA9 (Valeurs simul\xC3\xA9" "es)";
                row.since = since;
                row.sinceText = f.since.empty() ? ss::duration(std::max(0.0, t - since)) : "depuis " + f.since;
                row.key = k;
                rows.push_back(std::move(row));
            }
    for (const auto& [k, since] : st.twinSince)
        if (!twinNow.count(k)) journal.add(SimSource::Equipements, SimSeverity::Info, "relache", "Case rel\xC3\xA2" "ch\xC3\xA9" "e : " + k.substr(5), {}, "forcages");
    st.twinSince = std::move(twinNow);
    st.twinPrimed = true;
    std::uint64_t signature = rows.size();
    for (const auto& r : rows)
        for (const char ch : r.key + r.value) signature = signature * 131u + static_cast<unsigned char>(ch);
    st.model->forcings = std::move(rows);
    if (signature != st.forcingSignature) {
        st.forcingSignature = signature;
        ++st.model->forcingRevision;
    }

    // ---- l'IHM : en marche, sa vue, son utilisateur, ses alarmes, ses erreurs ---------
    const bool running = hmiPane && hmiPane->runtime().running();
    if (running) {
        auto& run = hmiPane->runtime();
        const auto* view = doc ? doc->project.view(run.currentView()) : nullptr;
        const std::string viewName = view ? view->name : std::string{};
        if (!st.hmiRunning) {
            journal.add(SimSource::Ihm, SimSeverity::Ok, "ihm-demarrage", "L'IHM d\xC3\xA9marre" + (viewName.empty() ? std::string{} : " sur " + viewName), {}, "ihm");
            st.hmiView = run.currentView();
            st.scriptErrors = 0;
            st.lastScriptError.clear();
            st.alarms.clear();
            st.hmiJournalTime = -1;
        } else if (run.currentView() != st.hmiView) {
            st.hmiView = run.currentView();
            if (!viewName.empty()) journal.add(SimSource::Ihm, SimSeverity::Info, "vue", "Vue affich\xC3\xA9" "e : " + viewName, {}, "vue:" + viewName);
        }
        if (run.userLogin() != st.hmiUser) {
            journal.add(SimSource::Ihm, SimSeverity::Info, "utilisateur",
                        run.userLogin().empty() ? "D\xC3\xA9" "connexion de " + st.hmiUser : "Connexion : " + run.userLogin(), {}, "ihm");
            st.hmiUser = run.userLogin();
        }
        std::map<std::uint64_t, std::string> active;
        for (const auto& a : run.alarms()) {
            if (!a.active) continue;
            const std::string text = a.message.empty() ? a.name : a.message;
            active[a.alarm] = text;
            if (!st.alarms.count(a.alarm))
                journal.add(SimSource::Ihm, a.priority <= 1 ? SimSeverity::Error : SimSeverity::Warning, "alarme",
                            "Alarme : " + text + " (priorit\xC3\xA9 " + std::to_string(a.priority) + ")", {}, "alarmes");
        }
        for (const auto& [id, text] : st.alarms)
            if (!active.count(id)) journal.add(SimSource::Ihm, SimSeverity::Ok, "alarme-fin", "Alarme disparue : " + text, {}, "alarmes");
        st.alarms = std::move(active);
        for (const auto& e : run.journal()) {
            if (e.time <= st.hmiJournalTime) continue;
            st.hmiJournalTime = e.time;
            if (e.kind != "Erreur") continue;
            ++st.scriptErrors;
            st.lastScriptError = (e.source.empty() ? std::string{} : e.source + " : ") + e.message;
            journal.add(SimSource::Ihm, SimSeverity::Error, "script", "Erreur de script : " + st.lastScriptError, {}, "ihm");
        }
    } else if (st.hmiRunning) {
        journal.add(SimSource::Ihm, SimSeverity::Info, "ihm-arret", "L'IHM s'arr\xC3\xAAte", {}, "ihm");
        st.alarms.clear();
        st.hmiUser.clear();
    }
    st.hmiRunning = running;
    // 1.12.17 : chaque lenteur passe aussi au journal (le filtre Lenteurs) ; un clic l'ouvre dans la Vue d'ensemble.
    if (hmiPane) {
        const auto& slow = hmiPane->runtime().slowLog();
        if (slow.lastSerial() < st.slowSeen) st.slowSeen = 0;
        for (const auto& e : slow.entries()) {
            if (e.serial <= st.slowSeen) continue;
            std::string text = hmi::slowKindName(e.kind) + " : " + e.what + " - " + hmi::slowMs(e.ms);
            if (e.kind == hmi::SlowKind::Cycle) text += " (p\xC3\xA9riode " + hmi::slowMs(e.period) + ")";
            else if (e.usual > 0) text += " (habituel " + hmi::slowMs(e.usual) + ")";
            if (!e.where.empty() && e.where != "IHM") text += ", " + e.where;
            journal.add(SimSource::Lenteurs, e.kind == hmi::SlowKind::Cycle ? SimSeverity::Error : SimSeverity::Warning, "lenteur", text, {},
                        "lenteur:" + std::to_string(e.serial));
        }
        st.slowSeen = slow.lastSerial();
    }
    // Les variables de l'automate que l'IHM lit : un parcours de toute l'IHM, toutes les dix secondes.
    if (core::hasApi() && t - st.plcReadsAt > 10.0) {      // 1.12.14 : XPGAnalyser IHM - pas d'automate
        st.plcReadsAt = t;
        st.plcReads = hmiReadsOfPlc().size();
    }

    // ---- les equipements : leurs liaisons, leurs echanges -------------------------------
    auto& equip = app_.equipments();
    std::uint64_t total = 0;
    for (const auto& e : equip.statuses()) {
        if (!e.enabled) continue;
        const bool sim = e.simulated || e.viaTwin;
        const bool ok = e.reachable || (sim && e.twinTone != 2 && e.twinTone != 5);
        const bool tested = e.tested || sim;
        if (tested) {
            const auto it = st.equipOk.find(e.name);
            if (it == st.equipOk.end()) {
                if (!ok) journal.add(SimSource::Equipements, SimSeverity::Error, "liaison",
                                     e.name + " ne r\xC3\xA9pond pas" + (e.why.empty() ? std::string{} : " : " + e.why), {}, "equipements");
            } else if (it->second != ok) {
                journal.add(SimSource::Equipements, ok ? SimSeverity::Ok : SimSeverity::Error, "liaison",
                            ok ? e.name + " r\xC3\xA9pond de nouveau" : e.name + " ne r\xC3\xA9pond plus" + (e.why.empty() ? std::string{} : " : " + e.why), {},
                            "equipements");
            }
            st.equipOk[e.name] = ok;
        }
        std::uint64_t n = 0;
        if (auto* link = equip.link(e.name)) n += link->diagnostics().requests;
        if (sim) n += equip.simulatedStats(e.name).requests;
        const auto prev = st.requests.find(e.name);
        if (prev != st.requests.end() && n >= prev->second) total += n - prev->second;
        st.requests[e.name] = n;
    }
    if (st.requestsAt >= 0.0 && t > st.requestsAt) {
        const double rate = static_cast<double>(total) / (t - st.requestsAt);
        st.exchangeRate = st.exchangeRate * 0.5 + rate * 0.5;     // un peu lisse : il ne saute pas a chaque relecture
    }
    st.requestsAt = t;
}

// ------------------------------------------------------------------ la photo ----
ss::Snapshot MainAnalysisScreen::simSnapshot(double t) {
    ss::Snapshot s;
    auto& st = *simCenter_;
    const auto project = app_.project();
    s.project = project != nullptr;
    auto& host = app_.simulation();
    auto* rt = host.attached() ? app_.simulationRuntime() : nullptr;
    auto& p = s.plc;
    p.prepared = host.attached() && rt;
    p.prepareError = p.prepared ? std::string{} : st.prepareError;
    using State = SimulationHost::State;
    switch (host.state()) {
        case State::Running: p.state = ss::PlcFacts::State::Running; break;
        case State::Paused:  p.state = ss::PlcFacts::State::Paused; break;
        case State::Halted:  p.state = ss::PlcFacts::State::Halted; break;
        case State::Stopped: p.state = ss::PlcFacts::State::Stopped; break;
    }
    p.cycle = host.scanCount();
    p.clockMs = rt ? rt->clockMs() : 0;
    p.scanMicros = host.lastScanMicros();
    p.periodMs = host.scanIntervalMs();
    p.speed = host.speed();
    if (project) {
        p.entries = domain::executionOrder(*project, "MAST").size();
        for (const auto& task : project->tasks)
            if (project->strings.text(task.name) == "MAST") {
                p.sections = task.sections.size();
                if (task.period > 0) p.periodMs = static_cast<std::int64_t>(task.period);
            }
    }
    if (p.prepared && host.state() == State::Halted) {
        for (const auto& d : host.lastDiagnostics())
            if (d.severity == sim::Diagnostic::Severity::Error) {
                p.haltReason = SimulationPane::frenchMessage(d.message);
                p.haltSection = d.section;
                p.haltLine = d.line ? d.line : lineOfMessage(d.message);
                const auto& m = d.message;
                p.haltLoop = m.find("loop is probably not terminating") != std::string::npos || m.find(" loop ran more than ") != std::string::npos
                          || m.find("loop with a step of zero") != std::string::npos;
                if (m.find("ran for more than its time limit") != std::string::npos) p.haltLimitMs = 1500;
                else if (m.rfind("the scan exceeded ", 0) == 0) {
                    // "le cycle a depasse 2 000 000 instructions et s'est arrete" : ce qu'il a depasse.
                    const std::string fr = p.haltReason, head = "d\xC3\xA9pass\xC3\xA9 ";
                    const auto from = fr.find(head);
                    const auto to = fr.find(" et s'est");
                    if (from != std::string::npos && to != std::string::npos && to > from + head.size())
                        p.haltLimitText = fr.substr(from + head.size(), to - from - head.size());
                } else if (m.find(" loop ran more than ") != std::string::npos) {
                    p.haltLimitText = p.haltReason;
                }
                break;
            }
        if (p.haltReason.empty()) {
            p.haltReason = SimulationPane::frenchMessage(host.haltMessage());
            if (p.haltReason.rfind("Cycle ", 0) == 0)
                if (const auto colon = p.haltReason.find(" : "); colon != std::string::npos) p.haltReason = p.haltReason.substr(colon + 3);
        }
        if (const auto dot = p.haltSection.find('.'); dot != std::string::npos && dot > 0) p.haltBlock = p.haltSection.substr(0, dot);
        if (!p.haltBlock.empty()) p.haltNewer = newerInLibrary(p.haltBlock);
    }
    if (rt)
        for (const auto& u : rt->unknownCalls()) p.unknown.push_back({u.name, u.calls, u.section, u.line});
    p.continueUnknown = host.continueOnUnknownCalls();
    p.forced = 0;
    for (const auto& f : st.model->forcings) {
        ++p.forced;
        if (f.since >= 0.0 && (p.oldestForced.empty() || t - f.since > p.oldestForcedSeconds)) {
            p.oldestForced = f.what;
            p.oldestForcedSeconds = t - f.since;
        }
    }
    p.stale = host.stale();
    if (const auto& oc = host.lastOnlineChange(); oc && st.onlineAt >= 0.0) {
        p.online = true;
        p.onlineFailed = oc->failed;
        p.onlineSummary = oc->summary;
        p.onlineAgeSeconds = t - st.onlineAt;
    }
    if (const auto& hit = host.lastBreakHit(); hit && host.state() == State::Paused) {
        p.breakHit = true;
        p.breakSection = hit->section;
        p.breakLine = hit->line;
    }
    p.breakpoints = host.breakpoints().size();
    for (const auto& sec : host.sectionTimes()) {
        if (!sec.active) {                       // 1.10.2 : sa condition d'activation etait fausse
            if (p.inactiveSections++ == 0) {
                p.inactiveSection = sec.entry.empty() || sec.entry == sec.section ? sec.section : sec.entry + " \xE2\x80\xBA " + sec.section;
                p.inactiveCondition = sec.condition;
            }
            continue;
        }
        if (sec.micros > p.heaviestMicros) {
            p.heaviestMicros = sec.micros;
            p.heaviestSection = sec.section.empty() ? sec.entry : sec.section;
        }
    }

    // L'IHM.
    auto& h = s.hmi;
    const auto doc = app_.hmi();
    h.exists = doc && !doc->project.views.empty();
    auto* hmiPane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
    h.running = hmiPane && hmiPane->runtime().running();
    if (h.running) {
        auto& run = hmiPane->runtime();
        if (const auto* v = doc ? doc->project.view(run.currentView()) : nullptr) h.view = v->name;
        h.user = run.userLogin();
        h.alarms = run.alarms().size();
        for (const auto& a : run.alarms()) h.activeAlarms += a.active ? 1u : 0u;
        h.unacked = run.unacknowledged();
        h.topPriority = run.highestPriority();
        for (const auto& a : run.alarms())
            if (!a.acked && a.priority == h.topPriority) {
                h.topAlarm = a.message.empty() ? a.name : a.message;
                break;
            }
        h.scriptErrors = st.scriptErrors;
        h.lastScriptError = st.lastScriptError;
    }
    h.plcReads = st.plcReads;

    // Les equipements.
    for (const auto& e : app_.equipments().statuses()) {
        ss::EquipFacts::One one;
        one.name = e.name;
        one.enabled = e.enabled;
        one.simulated = e.simulated || e.viaTwin;
        one.ok = e.reachable || (one.simulated && e.twinTone != 2 && e.twinTone != 5);
        one.tested = e.tested || one.simulated;
        one.state = e.state;
        one.why = !e.why.empty() ? e.why : one.simulated && !one.ok ? e.twinState : std::string{};
        s.equip.list.push_back(std::move(one));
    }
    s.equip.exchangesPerSecond = st.exchangeRate;
    // 1.12.14 : XPGAnalyser IHM n'a pas d'automate - rien de lui dans la Vue d'ensemble (ni preparation,
    // ni cycle, ni section) ; les forcages restent : les cases des esclaves simules.
    if (!core::hasApi()) {
        ss::PlcFacts none;
        none.forced = s.plc.forced;
        none.oldestForced = s.plc.oldestForced;
        none.oldestForcedSeconds = s.plc.oldestForcedSeconds;
        s.plc = std::move(none);
    }
    return s;
}

// ---------------------------------------------------------------- les pastilles ----
void MainAnalysisScreen::refreshSimBadges() {
    if (!treeModel_ || !explorer_ || !simCenter_) return;
    auto& st = *simCenter_;
    const auto& rep = st.model->report;
    const auto tone = [](ss::Tone t) {
        switch (t) {
            case ss::Tone::Ok:      return ui::Tone::Ok;
            case ss::Tone::Warning: return ui::Tone::Warning;
            case ss::Tone::Error:   return ui::Tone::Error;
            case ss::Tone::Info:    return ui::Tone::Info;
            case ss::Tone::Off:     break;
        }
        return ui::Tone::Muted;
    };
    const std::string forcings = st.model->forcings.empty() ? std::string{} : std::to_string(st.model->forcings.size());
    const std::string trends = st.model->trends().empty() ? std::string{} : std::to_string(st.model->trends().size());
    // ---- 1.11.22 : le deballage du dossier Simulation - les lignes, poussees ici (pas au dessin) ----
    bool rows = false;
    {
        using SR = ProjectTreeModel::SimRow;
        std::vector<SR> bps, frc, trs;
        for (const auto& b : app_.simulation().breakpoints()) {
            if (SimDebugPane::isTransient(b.id)) continue;      // "aller a la ligne" : pas un point de l'utilisateur
            SR r;
            r.text = b.section + "  \xC2\xB7  ligne " + std::to_string(b.line);
            r.hint = !b.enabled ? std::string("d\xC3\xA9sactiv\xC3\xA9") : !b.condition.empty() ? "si " + b.condition
                   : b.hits ? std::to_string(b.hits) + " passage(s)" : std::string{};
            r.key = "bp:" + std::to_string(b.id);
            r.off = !b.enabled;
            bps.push_back(std::move(r));
        }
        for (const auto& f : st.model->forcings) {
            SR r;
            r.text = f.what + " = " + f.value;
            r.hint = f.source == SimForcing::Source::Automate ? (f.programSays.empty() ? std::string{} : "le programme dirait " + f.programSays)
                                                               : f.where;
            r.key = f.key;
            frc.push_back(std::move(r));
        }
        for (const auto& t : st.model->trends()) {
            SR r;
            r.text = t.path + (t.last.empty() ? std::string{} : " = " + t.last);
            r.hint = t.hmi ? std::string("IHM") : std::string{};
            r.key = t.path;
            trs.push_back(std::move(r));
        }
        rows = treeModel_->setSimRows(ProjectTreeModel::SimList::Breakpoints, std::move(bps));
        rows = treeModel_->setSimRows(ProjectTreeModel::SimList::Forcings, std::move(frc)) || rows;
        rows = treeModel_->setSimRows(ProjectTreeModel::SimList::Trends, std::move(trs)) || rows;
        const auto journal = app_.simJournal().size();
        rows = treeModel_->setSimBadge(NK::SimJournal, journal ? std::to_string(journal) : std::string{}, ui::Tone::None) || rows;
        rows = refreshSimTreeRows() || rows;                   // 1.12.17 : la Simulation depliee
    }
    // ---- fin 1.11.22 ----
    const std::string shown = rep.folderBadge + "|" + ss::toneName(rep.folderTone) + "|" + rep.hmiBadge + "|" + rep.equipBadge + "|"
                            + ss::toneName(rep.equipTone) + "|" + forcings + "|" + trends;
    if (shown == st.badgesShown && st.badgeModel.lock() == treeModel_) {
        if (rows) explorer_->invalidate();
        return;
    }
    bool changed = st.badgeModel.lock() != treeModel_;
    st.badgesShown = shown;
    st.badgeModel = treeModel_;
    changed = treeModel_->setSimBadge(NK::SimFolder, rep.folderBadge, tone(rep.folderTone)) || changed;
    changed = treeModel_->setSimBadge(NK::HmiSimulation, rep.hmiBadge, ui::Tone::Ok) || changed;
    changed = treeModel_->setSimBadge(NK::SimEquipment, rep.equipBadge, tone(rep.equipTone)) || changed;
    changed = treeModel_->setSimBadge(NK::SimForcing, forcings, ui::Tone::Warning) || changed;
    changed = treeModel_->setSimBadge(NK::SimTrends, trends, ui::Tone::None) || changed;
    if (changed) explorer_->invalidate();
}

// ------------------------------------------------- 1.12.17 : la Simulation depliee ----
//  Sous Vue d'ensemble : Performances (le cycle de la derniere minute), Lenteurs, A surveiller.
//  Sous IHM : Affiche (la vue, les popups, depuis quand), Scripts (leur temps moyen), Alarmes,
//  Utilisateur. Sous Equipements : chacun (son etat, ses echanges), ses Lues et Ecrites. Sous
//  Journal : une ligne par source. Une fois par seconde ; l'IHM arretee : sa derniere marche, grisee.
bool MainAnalysisScreen::refreshSimTreeRows() {
    if (!treeModel_ || !simCenter_) return false;
    auto& st = *simCenter_;
    const double t = st.model->now;
    if (st.treeRowsAt > -10 && t >= st.treeRowsAt && t - st.treeRowsAt < 1.0) return false;
    st.treeRowsAt = t;
    using SR = ProjectTreeModel::SimRow;
    using SL = ProjectTreeModel::SimList;
    const std::string mid = " \xC2\xB7 ";
    auto* hmiPane = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
    hmi::Runtime* rt = hmiPane ? &hmiPane->runtime() : nullptr;
    const bool running = rt && rt->running();
    const bool ran = rt && (running || !rt->perfHistory().seconds().empty());
    if (running && !st.treeRunning) st.shownSince.clear();          // une nouvelle marche
    st.treeRunning = running;
    const auto doc = app_.hmi();
    bool changed = false;
    // ---- Vue d'ensemble ----
    {
        std::vector<SR> ov;
        if (core::hasIhm()) {
            SR perf;
            perf.text = "Performances";
            perf.icon = ui::Icon::Chart;
            perf.key = "performances";
            perf.hint = "pas encore de mesure";
            if (ran) {
                const auto sm = rt->perfHistory().summary(rt->runNow(), 60);
                if (sm.cycles) perf.hint = "cycle " + hmi::slowMs(sm.mean) + mid + "p95 " + hmi::slowMs(sm.p95);
                if (sm.overruns) {
                    perf.badge = std::to_string(sm.overruns);
                    perf.badgeTone = ui::Tone::Error;
                }
                perf.off = !running;
            }
            ov.push_back(std::move(perf));
            SR slow;
            slow.text = "Lenteurs";
            slow.icon = ui::Icon::History;
            slow.key = "lenteurs";
            slow.hint = "aucune";
            if (rt && !rt->slowLog().entries().empty()) {
                const auto& log = rt->slowLog().entries();
                slow.hint = "la derni\xC3\xA8re \xC3\xA0 " + log.back().clock.substr(0, 8);
                slow.badge = std::to_string(log.size());
                slow.badgeTone = ui::Tone::Warning;
            }
            ov.push_back(std::move(slow));
        }
        SR watch;
        watch.text = "\xC3\x80 surveiller";
        watch.icon = ui::Icon::Warning;
        watch.key = "ensemble";
        const auto& att = st.model->report.attention;
        watch.hint = att.empty() ? std::string("rien") : att.front().text;
        watch.tone = att.empty() ? ui::Tone::Muted : ui::Tone::Warning;
        if (!att.empty()) {
            watch.badge = std::to_string(att.size());
            watch.badgeTone = att.front().tone == ss::Tone::Error ? ui::Tone::Error : ui::Tone::Warning;
        }
        ov.push_back(std::move(watch));
        changed = treeModel_->setSimRows(SL::Overview, std::move(ov)) || changed;
    }
    // ---- IHM : Affiche, Scripts, Alarmes, Utilisateur (la derniere marche, grisee) ----
    {
        std::vector<SR> hm;
        if (ran && doc) {
            const auto& p = doc->project;
            const bool off = !running;
            SR shown;
            shown.text = "Affich\xC3\xA9";
            shown.icon = ui::Icon::Screen;
            shown.key = "ihm";
            shown.off = off;
            std::vector<hmi::Id> ids;
            if (rt->currentView() != hmi::kNoId) ids.push_back(rt->currentView());
            for (const auto id : rt->popups()) ids.push_back(id);
            std::set<std::uint64_t> still;
            for (std::size_t i = 0; i < ids.size(); ++i) {
                const auto* v = p.view(ids[i]);
                if (!v) continue;
                const auto key = static_cast<std::uint64_t>(ids[i]);
                still.insert(key);
                auto it = st.shownSince.find(key);
                if (it == st.shownSince.end() && running) it = st.shownSince.emplace(key, hmi::wallClockMs().substr(0, 8)).first;
                const std::string since = it != st.shownSince.end() ? it->second : std::string{};
                SR r;
                r.text = v->name;
                r.icon = ui::Icon::Screen;
                r.tone = i == 0 ? ui::Tone::None : ui::Tone::Family2;
                r.hint = i == 0 ? (since.empty() ? std::string{} : "depuis " + since)
                                : "popup" + (since.empty() ? std::string{} : ", ouverte \xC3\xA0 " + since);
                r.key = i == 0 ? "vue:" + v->name : std::string("ihm");
                r.off = off;
                shown.children.push_back(std::move(r));
            }
            if (running)
                for (auto it = st.shownSince.begin(); it != st.shownSince.end();) it = still.count(it->first) ? std::next(it) : st.shownSince.erase(it);
            shown.badge = std::to_string(shown.children.size());
            hm.push_back(std::move(shown));
            // Les scripts : les plus longs d'abord, leur temps moyen.
            SR scripts;
            scripts.text = "Scripts";
            scripts.icon = ui::Icon::Code;
            scripts.key = "ihm";
            scripts.off = off;
            std::vector<std::pair<hmi::Id, const hmi::ScriptPerf*>> ran1;
            for (const auto& [id, s] : rt->perf().scripts)
                if (s.time.count) ran1.push_back({id, &s});
            std::stable_sort(ran1.begin(), ran1.end(), [](const auto& a, const auto& b) { return a.second->time.mean() > b.second->time.mean(); });
            for (std::size_t i = 0; i < ran1.size() && i < 50; ++i) {
                const auto& [id, s] = ran1[i];
                SR r;
                r.text = s->name;
                r.icon = ui::Icon::Code;
                r.hint = hmi::slowMs(s->time.mean()) + " en moyenne" + mid + std::to_string(s->time.count) + " fois";
                r.key = "script:" + std::to_string(static_cast<std::uint64_t>(id));
                r.off = off;
                if (i < 2) scripts.hint += (scripts.hint.empty() ? std::string{} : mid) + s->name + " " + hmi::slowMs(s->time.mean());
                scripts.children.push_back(std::move(r));
            }
            if (!ran1.empty()) scripts.badge = std::to_string(ran1.size());
            hm.push_back(std::move(scripts));
            // Les alarmes : chacune, son etat.
            SR alarms;
            alarms.text = "Alarmes";
            alarms.icon = ui::Icon::Warning;
            alarms.key = "alarmes";
            alarms.off = off;
            std::size_t active = 0, toAck = 0;
            for (const auto& a : rt->alarms()) {
                active += a.active ? 1u : 0u;
                toAck += !a.acked && a.ackRequired ? 1u : 0u;
                if (alarms.children.size() >= 50) continue;
                SR r;
                r.text = a.message.empty() ? a.name : a.message;
                r.icon = ui::Icon::Warning;
                r.tone = a.active ? ui::Tone::Warning : ui::Tone::Muted;
                r.hint = a.state();
                r.key = "alarmes";
                r.off = off;
                alarms.children.push_back(std::move(r));
            }
            alarms.hint = std::to_string(active) + (active > 1 ? " actives" : " active")
                        + (toAck ? mid + std::to_string(toAck) + " \xC3\xA0 acquitter" : std::string{});
            if (!rt->alarms().empty()) {
                alarms.badge = std::to_string(rt->alarms().size());
                alarms.badgeTone = active ? ui::Tone::Warning : ui::Tone::None;
            }
            hm.push_back(std::move(alarms));
            SR user;
            user.text = "Utilisateur";
            user.icon = ui::Icon::User;
            user.key = "ihm";
            user.off = off;
            const int level = rt->level();
            user.hint = rt->userLogin().empty() ? std::string("personne")
                                                 : rt->userLogin() + (level > 0 && level < 99 ? mid + "niveau " + std::to_string(level) : std::string{});
            hm.push_back(std::move(user));
        }
        changed = treeModel_->setSimRows(SL::Hmi, std::move(hm)) || changed;
    }
    // ---- Equipements : chacun, ses Lues et Ecrites ----
    {
        std::vector<SR> eqs;
        if (core::hasIhm())
            for (const auto& e : app_.equipments().statuses()) {
                SR r;
                r.text = e.name;
                r.icon = ui::Icon::Network;
                r.key = "equipement:" + e.name;
                const bool simulated = e.simulated || e.viaTwin;
                std::string hint = !e.enabled ? std::string("d\xC3\xA9sactiv\xC3\xA9") : simulated ? std::string("simul\xC3\xA9")
                                 : e.reachable ? std::string("joignable") : e.tested ? std::string("muet") : std::string("pas essay\xC3\xA9");
                r.tone = !e.enabled ? ui::Tone::Muted : simulated ? ui::Tone::Info : e.reachable ? ui::Tone::Ok : e.tested ? ui::Tone::Error : ui::Tone::None;
                std::size_t reads = 0;
                std::uint64_t writes = 0;
                if (auto* lk = rt ? rt->equipmentLink(e.name) : nullptr) {
                    const auto d = lk->diagnostics();
                    reads = d.subscribed;
                    writes = d.writes;
                    auto& seen = st.treeRequests[e.name];
                    if (running && seen.second < t && d.requests >= seen.first && seen.first > 0) {
                        const double rate = static_cast<double>(d.requests - seen.first) / (t - seen.second);
                        char buf[32];
                        std::snprintf(buf, sizeof buf, rate < 10 ? "%.1f" : "%.0f", rate);
                        std::string txt(buf);
                        for (auto& ch : txt)
                            if (ch == '.') ch = ',';
                        if (rate > 0) hint += mid + txt + " \xC3\xA9" "changes/s";
                    }
                    seen = {d.requests, t};
                    if (d.avgMs > 0) hint += mid + hmi::slowMs(d.avgMs);
                }
                r.hint = hint;
                r.off = rt && !running;
                SR in;
                in.text = "Lues";
                in.icon = ui::Icon::LocatedVariable;
                in.tone = ui::Tone::Input;
                in.badge = std::to_string(reads);
                in.hint = reads == 1 ? "variable" : "variables";
                in.key = "equipement-lues:" + e.name;
                in.off = r.off;
                SR out;
                out.text = "\xC3\x89" "crites";
                out.icon = ui::Icon::LocatedVariable;
                out.tone = ui::Tone::Output;
                out.badge = std::to_string(writes);
                out.hint = writes == 1 ? "\xC3\xA9" "criture" : "\xC3\xA9" "critures";
                out.key = "equipement-ecrites:" + e.name;
                out.off = r.off;
                r.children = {std::move(in), std::move(out)};
                eqs.push_back(std::move(r));
            }
        changed = treeModel_->setSimRows(SL::Equipments, std::move(eqs)) || changed;
    }
    // ---- Journal : une ligne par source (le filtre du Journal) ----
    {
        std::vector<SR> jr;
        const auto& j = app_.simJournal();
        for (const auto s : {SimSource::Automate, SimSource::Ihm, SimSource::Equipements, SimSource::Debogage, SimSource::Lenteurs, SimSource::Simulation}) {
            const auto n = j.count(s);
            if (!core::hasApi() && (s == SimSource::Automate || s == SimSource::Debogage)) continue;
            if (!core::hasIhm() && (s == SimSource::Ihm || s == SimSource::Equipements || s == SimSource::Lenteurs) && n == 0) continue;
            if (s == SimSource::Simulation && n == 0) continue;
            SR r;
            r.text = SimJournal::sourceName(s);
            r.icon = s == SimSource::Ihm ? ui::Icon::Screen : s == SimSource::Equipements ? ui::Icon::Network : s == SimSource::Lenteurs ? ui::Icon::History
                   : s == SimSource::Automate ? ui::Icon::Cpu : s == SimSource::Debogage ? ui::Icon::Halt : ui::Icon::Play;
            r.key = std::string("journal:") + (s == SimSource::Ihm ? "ihm" : s == SimSource::Equipements ? "equipements" : s == SimSource::Lenteurs ? "lenteurs"
                                             : s == SimSource::Automate ? "automate" : s == SimSource::Debogage ? "debogage" : "simulation");
            if (n) r.badge = std::to_string(n);
            if (n && s == SimSource::Lenteurs) r.badgeTone = ui::Tone::Warning;
            jr.push_back(std::move(r));
        }
        changed = treeModel_->setSimRows(SL::Journal, std::move(jr)) || changed;
    }
    return changed;
}

// ---------------------------------------------------------------- les onglets ----
bool MainAnalysisScreen::routeSimNode(ui::NodeId node) {
    switch (ProjectTreeModel::kindOf(node)) {
        case NK::SimFolder:
        case NK::SimOverview:  (void)openSimCenter("ensemble"); return true;
        case NK::SimEquipment: (void)openSimCenter("equipements"); return true;
        case NK::SimDebug:     (void)openSimCenter("debogage"); return true;
        case NK::SimForcing:   (void)openSimCenter("forcages"); return true;
        case NK::SimTrends:    (void)openSimCenter("courbes"); return true;
        case NK::SimJournal:   (void)openSimCenter("journal"); return true;
        case NK::SimRow: {                                 // 1.11.22 : une ligne du deballage
            const auto* row = treeModel_ ? treeModel_->simRowOf(node) : nullptr;
            switch (ProjectTreeModel::simListOf(node)) {
                case ProjectTreeModel::SimList::Breakpoints: {
                    std::string section;
                    int line = 0;
                    if (row && row->key.rfind("bp:", 0) == 0) {
                        const auto id = static_cast<std::uint32_t>(std::strtoul(row->key.c_str() + 3, nullptr, 10));
                        for (const auto& b : app_.simulation().breakpoints())
                            if (b.id == id) { section = b.section; line = b.line; }
                    }
                    openSimDebugAt(section, line);
                    return true;
                }
                case ProjectTreeModel::SimList::Forcings:
                    (void)openSimCenter("forcages");
                    if (auto* pane = dynamic_cast<SimForcingPane*>(simCenterPane("forcages")); pane && row) {
                        pane->refresh();
                        const auto eq = row->text.find(" = ");
                        (void)pane->select(eq == std::string::npos ? row->text : row->text.substr(0, eq));
                    }
                    return true;
                case ProjectTreeModel::SimList::Trends:
                    (void)openSimCenter("courbes");
                    return true;
                // 1.12.17 : la Simulation depliee - la cle de la ligne dit ou aller.
                case ProjectTreeModel::SimList::Overview:
                case ProjectTreeModel::SimList::Hmi:
                case ProjectTreeModel::SimList::Equipments:
                case ProjectTreeModel::SimList::Journal:
                    if (row) simCenterGo(row->key);
                    return true;
            }
            return true;
        }
        default:               return false;
    }
}

bool MainAnalysisScreen::openSimCenter(const std::string& key) {
    if (key == "automate") {
        openApiTabFromAction("simulation");
        return true;
    }
    if (key == "ihm") {
        if (!app_.hmi()) {
            if (status_) status_->setTransientMessage("Le projet n'a pas d'IHM.", 6.0);
            return false;
        }
        openHmiPane("simulation");
        return true;
    }
    if (key == "debogage") {
        openSimDebug();
        return true;
    }
    if (key == "equipements") {
        if (!app_.hmi()) {
            if (status_) status_->setTransientMessage("Le projet n'a pas d'IHM : pas d'\xC3\xA9quipement.", 6.0);
            return false;
        }
        openHmiPane("communication");
        if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab("communication"))) {
            // Un equipement muet : l'etat des liaisons ; des jumeaux : leurs valeurs ; sinon la liste.
            bool mute = false, twins = false;
            for (const auto& e : app_.equipments().statuses()) {
                mute = mute || (e.enabled && e.tested && !e.reachable && !e.simulated && !e.viaTwin);
                twins = twins || e.simulated || e.twin;
            }
            comm->tabs().setCurrentIndex(static_cast<std::size_t>(mute ? HmiCommPane::TState : twins ? HmiCommPane::TValues : HmiCommPane::TEquipments));
        }
        return true;
    }
    if (std::find(std::begin(kCenterKeys), std::end(kCenterKeys), key) == std::end(kCenterKeys)) return false;
    if (!centre_) return false;
    if (!app_.project()) {
        if (status_) status_->setTransientMessage("Simulation : ouvre d'abord un projet.", 6.0, ui::StatusBar::Severity::Warning);
        return false;
    }
    const std::string tabKey = "sim:" + key;
    if (auto* page = apiTab(tabKey)) {
        if (showPage(page)) return true;
        apiTabs_.erase(tabKey);
    }
    // Le centre doit etre a l'ecran (Affichage > Panneaux a afficher).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(ui::Checkbox::State::Checked);
    ensureSimCenter();
    auto model = simCenter_->model;
    model->journal = &app_.simJournal();
    SimCenterHosts hosts;
    hosts.go = [this](const std::string& k) { simCenterGo(k); };
    hosts.release = [this](const std::vector<std::string>& keys) { releaseSimForcings(keys); };
    hosts.addTrend = [this](const std::string& path, std::string* why) { return addSimTrend(path, why); };
    hosts.status = [this](const std::string& m) {
        if (status_) status_->setTransientMessage(m, 6.0);
    };
    hosts.assist = [this](ui::InputText& field) {
        if (auto doc = app_.hmi()) field.setAssist(assist::fieldAssist(assist::sourcesFor(doc)));
    };
    // 1.12.17 : les performances dans le temps, les lenteurs - celles du moteur de l'IHM, lues a chaque dessin.
    hosts.perfView = [this]() {
        SimPerfView v;
        if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) {
            const auto& rt = sim->runtime();
            v.perf = &rt.perfHistory();
            v.slow = &rt.slowLog();
            v.rules = rt.slowRules();
            v.now = rt.runNow();
            v.running = rt.running();
        } else if (auto doc = app_.hmi()) {
            v.rules.absoluteMs = doc->project.config.slowMs;
            v.rules.factor = doc->project.config.slowFactor;
        }
        return v;
    };
    hosts.resetPerf = [this] {
        if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) sim->runtime().resetPerfHistory();
    };
    hosts.clearSlow = [this] {
        if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) sim->runtime().clearSlowLog();
        if (status_) status_->setTransientMessage("Le journal des lenteurs est vide.", 4.0);
    };
    hosts.setSlowRules = [this](const hmi::SlowRules& r) {
        if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) sim->runtime().setSlowRules(r);
        if (auto doc = app_.hmi())       // pour le projet (Ctrl+Z le reprend)
            if (auto cmd = hmi::changeProject(doc, "Seuils des lenteurs", [&r](hmi::Project& p) {
                    p.config.slowMs = static_cast<int>(std::lround(r.absoluteMs));
                    p.config.slowFactor = static_cast<int>(std::lround(r.factor));
                }))
                app_.apply(std::move(cmd));
        if (status_)
            status_->setTransientMessage("Une lenteur : plus de " + hmi::slowMs(r.absoluteMs) + ", ou plus de " + std::to_string(static_cast<int>(r.factor))
                                             + " fois son temps habituel (pour le projet).", 5.0);
    };
    hosts.exportCsv = [this](const std::string& which) {
        auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (!sim) return;
        if (which == "lenteurs") saveApiCsv("lenteurs_ihm", sim->runtime().slowLog().csv());
        else saveApiCsv("performances_ihm", sim->runtime().perfHistory().csv());
    };
    hosts.goSlow = [this](std::uint64_t serial) { goToSlow(serial); };
    std::unique_ptr<ApiFrame> frame;
    std::string title;
    ui::Icon icon = ui::Icon::Play;
    if (key == "ensemble") {
        auto pane = std::make_unique<SimOverviewPane>("analysis.sim.ensemble.volet", model, hosts);
        frame = std::make_unique<ApiFrame>("analysis.sim.ensemble", std::move(pane));
        auto& t = frame->tools();
        // 1.12.14 : XPGAnalyser IHM n'a pas d'automate - Demarrer et Arreter l'IHM (Simuler, Pause, Un cycle,
        // Relancer du cycle 0 preparaient le programme de MAST, qu'il n'a pas).
        const bool plc = core::hasApi();
        if (plc) {
            t.add(1, HmiGlyph::Play, "Lance le programme ici m\xC3\xAAme, cycle apr\xC3\xA8s cycle (reprend apr\xC3\xA8s une pause) \xC2\xB7 F5", "Simuler");
            t.add(2, HmiGlyph::Stop, "Met la simulation en pause : les valeurs restent fig\xC3\xA9" "es", "Pause");
            t.add(3, HmiGlyph::Forward, "Ex\xC3\xA9" "cute exactement un cycle, puis pause", "Un cycle");
            t.add(4, HmiGlyph::Refresh, "Arr\xC3\xAAte et remet tout \xC3\xA0 z\xC3\xA9ro \xC2\xB7 Maj+F5", "Arr\xC3\xAAter");
            t.add(9, HmiGlyph::Refresh, "Arr\xC3\xAAte, remet tout \xC3\xA0 z\xC3\xA9ro et relance (la maquette)", "Relancer du cycle 0");
        } else {
            t.add(1, HmiGlyph::Play, "D\xC3\xA9marre l'IHM : ses scripts, ses alarmes, ses vues ; ses variables, les esclaves simul\xC3\xA9s de ses "
                                     "\xC3\xA9quipements \xC2\xB7 F8", "D\xC3\xA9marrer l'IHM");
            t.add(4, HmiGlyph::Stop, "Arr\xC3\xAAte l'IHM \xC2\xB7 Maj+F8", "Arr\xC3\xAAter l'IHM");
        }
        t.separator();
        t.add(5, HmiGlyph::Lock, plc ? "Tous les for\xC3\xA7" "ages : l'automate, l'IHM, les esclaves simul\xC3\xA9s"
                                     : "Tous les for\xC3\xA7" "ages : les cases des esclaves simul\xC3\xA9s", "For\xC3\xA7" "ages");
        t.add(6, HmiGlyph::Trend, "Jusqu'\xC3\xA0 huit variables dans le temps", "Courbes");
        t.add(7, HmiGlyph::List, "Tout ce qui s'est pass\xC3\xA9, expliqu\xC3\xA9", "Journal");
        t.separator();
        t.add(8, HmiGlyph::Help, "L'aide (F1)", "Aide");
        using State = SimulationHost::State;
        if (plc) {
            t.setEnabledWhen(1, [this] { return !(app_.simulation().attached() && (app_.simulation().state() == State::Running || app_.simulation().state() == State::Halted)); });
            t.setEnabledWhen(2, [this] { return app_.simulation().attached() && app_.simulation().state() == State::Running; });
            t.setEnabledWhen(3, [this] { return !(app_.simulation().attached() && app_.simulation().state() == State::Halted); });
            t.setEnabledWhen(4, [this] { return app_.simulation().attached() && app_.simulation().state() != State::Stopped; });
        } else {
            const auto hmiOn = [this] {
                auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
                if (const auto tab = hmiTabs_.find("simulation"); !sim && tab != hmiTabs_.end()) sim = dynamic_cast<HmiSimulationPane*>(tab->second);
                return sim && sim->hmiRunning();
            };
            t.setEnabledWhen(1, [hmiOn] { return !hmiOn(); });
            t.setEnabledWhen(4, [hmiOn] { return hmiOn(); });
        }
        paneLinks_ += t.triggered->connect([this](int a) {
            static const char* const keys[] = {"", "sim.run", "sim.pause", "sim.step", "sim.stop", "forcages", "courbes", "journal"};
            if (a >= 1 && a <= 7) simCenterGo(keys[a]);
            else if (a == 9) simCenterGo("relancer");
            else {
                lot8::setPendingHelpAnchor(lot8::helpAnchorFor("ensemble"));   // Lot API 8 : didacticiels et aide (sa page)
                (void)app_.actions().trigger("help.open", app_.commands());
            }
        });
        frame->setHint("F9 (ou un clic sur l'\xC3\xA9tat, dans la barre du haut) ouvre cette vue \xC2\xB7 un clic sur une carte ouvre son onglet \xC2\xB7 "
                       "une ligne de \xC2\xAB Ce qui m\xC3\xA9rite ton attention \xC2\xBB dispara\xC3\xAEt quand c'est r\xC3\xA9gl\xC3\xA9");
        title = "Simulation \xC2\xB7 Vue d'ensemble";
        icon = ui::Icon::Station;
    } else if (key == "forcages") {
        auto pane = std::make_unique<SimForcingPane>("analysis.sim.forcages.volet", model, hosts);
        auto* raw = pane.get();
        frame = std::make_unique<ApiFrame>("analysis.sim.forcages", std::move(pane));
        auto& t = frame->tools();
        t.add(1, HmiGlyph::Unlock, "Rend les lignes choisies au programme (double-clic : une ligne)", "Rel\xC3\xA2" "cher");
        t.add(2, HmiGlyph::Unlock, "Rend toutes les variables et toutes les cases forc\xC3\xA9" "es", "Tout rel\xC3\xA2" "cher");
        t.separator();
        if (core::hasApi()) {            // 1.12.14 : XPGAnalyser IHM - pas d'onglet Automate
            t.add(3, HmiGlyph::Lock, "Forcer une variable de l'automate (l'onglet Automate, la variable choisie)", "Forcer\xE2\x80\xA6");
            t.add(4, HmiGlyph::Export, "Exporter les for\xC3\xA7" "ages de l'automate dans un fichier (les rejouer plus tard)", "Exporter\xE2\x80\xA6");
            t.separator();
        }
        t.add(5, HmiGlyph::Help, "L'aide (F1)", "Aide");
        t.setEnabledWhen(2, [model] { return !model->forcings.empty(); });
        paneLinks_ += t.triggered->connect([this, raw](int a) {
            switch (a) {
                case 1: raw->releaseSelected(); break;
                case 2: raw->releaseAll(); break;
                case 3:
                    openApiTabFromAction("simulation");
                    if (status_) status_->setTransientMessage("Double-clic sur une valeur (ou Forcer\xE2\x80\xA6) : elle est forc\xC3\xA9" "e.", 6.0);
                    break;
                case 4:
                    openApiTabFromAction("simulation");
                    if (auto* sim = centerPaneOf<SimulationPane>(apiTab("simulation"))) sim->runAction(SimulationPane::AExport);
                    break;
                default:   // Lot API 8 : didacticiels et aide (sa page de l'aide)
                    lot8::setPendingHelpAnchor(lot8::helpAnchorFor("forcages"));
                    (void)app_.actions().trigger("help.open", app_.commands());
                    break;
            }
        });
        frame->setHint("Tous les for\xC3\xA7" "ages au m\xC3\xAA" "me endroit \xC2\xB7 double-clic : rel\xC3\xA2" "cher \xC2\xB7 un for\xC3\xA7" "age oubli\xC3\xA9 fausse la suite des essais");
        title = "Simulation \xC2\xB7 For\xC3\xA7" "ages";
        icon = ui::Icon::Force;
    } else if (key == "courbes") {
        auto pane = std::make_unique<SimTrendsPane>("analysis.sim.courbes.volet", model, hosts);
        frame = std::make_unique<ApiFrame>("analysis.sim.courbes", std::move(pane));
        auto& t = frame->tools();
        t.add(1, HmiGlyph::Stop, "Arr\xC3\xAAter l'affichage ici (la mesure continue) ; Reprendre le relance", "Figer");
        t.add(2, HmiGlyph::Delete, "Retirer toutes les courbes", "Tout retirer");
        t.add(3, HmiGlyph::Export, "Les courbes en CSV (s\xC3\xA9parateur ;) : l'instant, puis une colonne par variable", "Exporter (CSV)\xE2\x80\xA6");
        t.separator();
        t.add(4, HmiGlyph::Help, "L'aide (F1)", "Aide");
        t.setCheckedWhen(1, [model] { return model->frozen(); });
        t.setEnabledWhen(2, [model] { return !model->trends().empty(); });
        t.setEnabledWhen(3, [model] { return !model->times().empty(); });
        paneLinks_ += t.triggered->connect([this, model](int a) {
            switch (a) {
                case 1: model->setFrozen(!model->frozen(), model->now); break;
                case 2: model->clearTrends(); break;
                case 3: if (!model->times().empty()) saveApiCsv("courbes-simulation", model->trendsCsv()); break;
                default:   // Lot API 8 : didacticiels et aide (sa page de l'aide)
                    lot8::setPendingHelpAnchor(lot8::helpAnchorFor("courbes"));
                    (void)app_.actions().trigger("help.open", app_.commands());
                    break;
            }
        });
        frame->setHint(std::string(core::hasApi() ? "Huit variables au plus, automate ou IHM" : "Huit variables de l'IHM au plus")   // 1.12.14
                       + " \xC2\xB7 un clic dans les traces pose le curseur \xC2\xB7 Figer arr\xC3\xAAte l'affichage, pas la mesure");
        title = "Simulation \xC2\xB7 Courbes";
        icon = ui::Icon::Chart;
    } else {
        auto pane = std::make_unique<SimJournalPane>("analysis.sim.journal.volet", model, hosts);
        auto* raw = pane.get();
        frame = std::make_unique<ApiFrame>("analysis.sim.journal", std::move(pane));
        auto& t = frame->tools();
        t.add(1, HmiGlyph::Link, "Aller \xC3\xA0 ce que dit la ligne choisie (double-clic : pareil)", "Aller \xC3\xA0");
        t.add(2, HmiGlyph::Export, "Tout le journal en CSV (s\xC3\xA9parateur ;)", "Exporter (CSV)\xE2\x80\xA6");
        t.add(3, HmiGlyph::Delete, "Vider le journal (la simulation continue)", "Effacer");
        t.separator();
        t.add(4, HmiGlyph::Help, "L'aide (F1)", "Aide");
        paneLinks_ += t.triggered->connect([this, raw](int a) {
            switch (a) {
                case 1: (void)raw->goSelected(); break;
                case 2: saveApiCsv("journal-simulation", app_.simJournal().csv()); break;
                case 3:
                    app_.simJournal().clear();
                    raw->refresh();
                    break;
                default:   // Lot API 8 : didacticiels et aide (sa page de l'aide)
                    lot8::setPendingHelpAnchor(lot8::helpAnchorFor("journal"));
                    (void)app_.actions().trigger("help.open", app_.commands());
                    break;
            }
        });
        frame->setHint("Tout ce qui arrive pendant la simulation, le plus r\xC3\xA9" "cent en haut \xC2\xB7 double-clic : y aller \xC2\xB7 les pastilles filtrent par source");
        title = "Simulation \xC2\xB7 Journal";
        icon = ui::Icon::Document;
    }
    auto* raw = frame.get();
    const auto at = centre_->addTab(ui::TabControl::Tab{title, icon, true, false}, std::move(frame));
    apiTabs_[tabKey] = raw;
    centre_->setCurrentIndex(at);
    simCenter_->reportDirty = true;
    return true;
}

ui::Widget* MainAnalysisScreen::simCenterPane(const std::string& key, bool open) {
    if (open && !apiTab("sim:" + key)) (void)openSimCenter(key);
    auto* frame = dynamic_cast<ApiFrame*>(apiTab("sim:" + key));
    return frame ? &frame->content() : nullptr;
}

SimCenterModel& MainAnalysisScreen::simCenterModel() {
    ensureSimCenter();
    return *simCenter_->model;
}

ss::Report MainAnalysisScreen::simCenterReport() {
    ensureSimCenter();
    const double t = app_.simJournal().now();
    pollSimCenter(t);
    simCenter_->model->report = ss::compute(simSnapshot(t));
    ++simCenter_->model->revision;
    return simCenter_->model->report;
}

// ------------------------------------------------------------- les raccourcis ----
bool MainAnalysisScreen::simCenterShortcut(const ui::KeyDown& k) {
    if (k.repeat || k.mods.ctrl || k.mods.alt) return false;
    const bool f5 = k.key == ui::Key::F5;
    const bool f10 = k.key == ui::Key::F10 && !k.mods.shift;
    if (!f5 && !f10) return false;
    // L'onglet Debogage a l'ecran les prend lui-meme (SimDebugPane).
    if (SimDebugPane::claimsKey(k.key)) return false;
    // Pas quand on tape : un champ, l'editeur de code (celui d'une section, d'une macro).
    const ui::Widget* focus = nullptr;
    const std::function<void(const ui::Widget&)> find = [&](const ui::Widget& w) {
        if (focus) return;
        if (w.focused()) {
            focus = &w;
            return;
        }
        for (const auto& child : w.children()) find(*child);
    };
    if (auto* root = widgetRoot()) find(*root);
    if (dynamic_cast<const ui::InputText*>(focus)) return false;
    if (const auto* editor = dynamic_cast<const ui::MultiLineText*>(focus); editor && !editor->readOnly()) return false;
    if (!app_.project()) return false;
    if (f5) {
        simCenterGo(k.mods.shift ? "sim.stop" : "sim.run");     // F5 : Simuler / Continuer ; Maj+F5 : Arreter
        return true;
    }
    // F10 : la section suivante seulement, puis pause (le moteur : stepSection).
    auto& host = app_.simulation();
    if (!host.attached()) {
        std::string why;
        if (!attachSimulation(&why)) {
            if (status_) status_->setTransientMessage("La simulation ne d\xC3\xA9marre pas : " + SimulationPane::frenchMessage(why), 6.0);
            return true;
        }
    }
    if (host.state() == SimulationHost::State::Halted) {
        if (status_) status_->setTransientMessage("L'automate est arr\xC3\xAAt\xC3\xA9 sur un d\xC3\xA9" "faut : Arr\xC3\xAAter (Maj+F5) d'abord.", 6.0);
        return true;
    }
    host.stepSection();
    if (simCenter_) simCenter_->reportDirty = true;
    return true;
}

// ------------------------------------------------------------------ aller a ----
// 1.12.17 : Aller a d'une lenteur - le script (a sa vue), l'objet du geste, la vue ou la popup ;
// un depassement : les performances ; un echange : les equipements.
void MainAnalysisScreen::goToSlow(std::uint64_t serial) {
    auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
    const auto* e = sim ? sim->runtime().slowLog().find(serial) : nullptr;
    if (!e) return;
    if (e->kind == hmi::SlowKind::Cycle) {
        simCenterGo("performances");
        return;
    }
    if (e->kind == hmi::SlowKind::Equipment) {
        simCenterGo("equipements");
        return;
    }
    if (e->scriptId != hmi::kNoId) {
        openHmiScripts(e->viewId, e->scriptId);
        return;
    }
    if (e->viewId != hmi::kNoId) openHmiView(e->viewId, -1, e->objectId);
}

void MainAnalysisScreen::simCenterGo(const std::string& key) {
    if (key.empty()) return;
    ensureSimCenter();
    auto& st = *simCenter_;
    // 1.12.17 : la Vue d'ensemble, sur un bloc ; une lenteur, ouverte ; le Journal, filtre Lenteurs.
    if (key == "performances" || key == "lenteurs" || key.rfind("lenteur:", 0) == 0) {
        if (!openSimCenter("ensemble")) return;
        if (auto* overview = centerPaneOf<SimOverviewPane>(apiTab("sim:ensemble"))) {
            if (key.rfind("lenteur:", 0) == 0) {
                overview->setSlowFilter(-1);
                overview->openSlow(std::strtoull(key.c_str() + 8, nullptr, 10));
            }
            overview->reveal(key == "performances" ? "performances" : "lenteurs");
        }
        return;
    }
    if (key == "journal-lenteurs") {
        if (!openSimCenter("journal")) return;
        if (auto* pane = centerPaneOf<SimJournalPane>(apiTab("sim:journal"))) (void)pane->setSource("lenteurs");
        return;
    }
    // 1.12.17 : les lignes de la Simulation depliee - le Journal d'une source, un script, un equipement.
    if (key.rfind("journal:", 0) == 0) {
        if (!openSimCenter("journal")) return;
        if (auto* pane = centerPaneOf<SimJournalPane>(apiTab("sim:journal"))) (void)pane->setSource(key.substr(8));
        return;
    }
    if (key.rfind("script:", 0) == 0) {
        const auto doc = app_.hmi();
        if (!doc) return;
        const auto id = std::strtoull(key.c_str() + 7, nullptr, 10);
        openHmiScripts(doc->project.viewOfScript(static_cast<hmi::Id>(id)), id);
        return;
    }
    if (key.rfind("equipement:", 0) == 0 || key.rfind("equipement-lues:", 0) == 0 || key.rfind("equipement-ecrites:", 0) == 0) {
        const auto colon = key.find(':');
        const std::string name = key.substr(colon + 1);
        if (!openSimCenter("equipements")) return;
        if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab("communication"))) {
            comm->tabs().setCurrentIndex(static_cast<std::size_t>(key.rfind("equipement:", 0) == 0 ? HmiCommPane::TEquipments : HmiCommPane::TValues));
            comm->selectEquipment(name);
        }
        return;
    }
    if (key == "sim.run" || key == "sim.pause" || key == "sim.stop" || key == "sim.step" || key == "relancer") {
        // 1.12.14 : XPGAnalyser IHM n'a pas d'automate - Simuler demarre l'IHM, Arreter l'arrete ; rien ne
        // prepare plus le programme de MAST ("La simulation ne se prepare pas - aucune section en ST").
        if (!core::hasApi()) {
            if (key != "sim.pause" && key != "sim.step") (void)topBarLot8Action(key == "sim.stop" ? "hmi.stop" : "hmi.start");
            st.prepareError.clear();
            st.reportDirty = true;
            return;
        }
        if (key == "relancer") runSimulationTransport("sim.stop");
        // La raison d'un refus de preparer : le bandeau la dira.
        if ((key == "sim.run" || key == "sim.step" || key == "relancer") && !app_.simulation().attached()) {
            std::string why;
            if (!attachSimulation(&why)) {
                st.prepareError = SimulationPane::frenchMessage(why);
                app_.simJournal().add(SimSource::Automate, SimSeverity::Error, "preparation", "La simulation ne d\xC3\xA9marre pas : " + st.prepareError, {},
                                      "automate");
                st.reportDirty = true;
                return;
            }
        }
        st.prepareError.clear();
        runSimulationTransport(key == "relancer" ? "sim.run" : key);
        st.reportDirty = true;
        return;
    }
    if (key.rfind("ligne:", 0) == 0) {
        const auto rest = key.substr(6);
        const auto colon = rest.rfind(':');
        const std::string section = colon == std::string::npos ? rest : rest.substr(0, colon);
        const auto line = colon == std::string::npos ? 0u : static_cast<std::uint32_t>(std::strtoul(rest.c_str() + colon + 1, nullptr, 10));
        goToSectionLine(section, line);
        return;
    }
    if (key.rfind("variable:", 0) == 0) {
        openApiTabFromAction("simulation");
        if (auto* sim = centerPaneOf<SimulationPane>(apiTab("simulation")); sim && !sim->selectVariable(key.substr(9)) && status_)
            status_->setTransientMessage(key.substr(9) + " : la simulation ne la conna\xC3\xAEt pas.", 6.0);
        return;
    }
    if (key.rfind("vue:", 0) == 0) {
        if (!openSimCenter("ihm")) return;
        const auto doc = app_.hmi();
        auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (doc && sim)
            for (const auto& v : doc->project.views)
                if (lowerOf(v.name) == lowerOf(key.substr(4))) {
                    sim->goToView(v.id);
                    break;
                }
        return;
    }
    if (key == "alarmes") {
        // 1.11.24 : la simulation de l'IHM n'a plus d'onglet Alarmes - ses apparitions sont dans la Console.
        if (!openSimCenter("ihm")) return;
        showHmiBuildOutputs(2);
        return;
    }
    if (key == "bibliotheque") {
        onApiRequest("bibliotheque");
        return;
    }
    // Les seconds boutons de l'attention (la maquette) : Tout relacher, Retablir la liaison.
    if (key == "forcages:tout-relacher") {
        std::vector<std::string> keys;
        for (const auto& f : st.model->forcings) keys.push_back(f.key);
        releaseSimForcings(keys);
        st.reportDirty = true;
        return;
    }
    if (key.rfind("reconnecter:", 0) == 0) {
        const auto name = key.substr(12);
        app_.equipments().reconnect(name);
        app_.equipments().test(name);
        app_.simJournal().add(SimSource::Equipements, SimSeverity::Info, "liaison", "R\xC3\xA9tablir la liaison : " + name + " (reconnexion, puis un essai)", {},
                              "equipements");
        if (status_) status_->setTransientMessage(name + " : reconnexion demand\xC3\xA9" "e, puis un essai de la liaison.", 6.0);
        st.nextPoll = 0;
        st.reportDirty = true;
        return;
    }
    if (openSimCenter(key)) return;
    if (status_) status_->setTransientMessage("Simulation : je ne sais pas aller \xC3\xA0 \xC2\xAB " + key + " \xC2\xBB.", 6.0);
}

// ------------------------------------------------------------- courbes, forcages ----
bool MainAnalysisScreen::addSimTrend(const std::string& raw, std::string* why) {
    ensureSimCenter();
    std::string path = trimmed(raw);
    bool hmiOnly = false;
    if (lowerOf(path).rfind("ihm:", 0) == 0) {
        hmiOnly = true;
        path = trimmed(path.substr(4));
    }
    if (path.empty()) {
        if (why) *why = "aucun chemin";
        return false;
    }
    auto& model = *simCenter_->model;
    if (!core::hasApi()) hmiOnly = true;      // 1.12.14 : XPGAnalyser IHM - pas d'automate a preparer
    if (!hmiOnly) {
        // L'automate : prepare au besoin (sans lancer), pour savoir si le chemin existe.
        if (!app_.simulation().attached() && app_.project()) (void)attachSimulation(nullptr);
        if (auto* rt = app_.simulation().attached() ? app_.simulationRuntime() : nullptr) {
            sim::Value v;
            if (rt->get(path, v)) {
                if (!model.addTrend(path, false, v.type() == sim::Type::Bool, why)) return false;
                simCenter_->reportDirty = true;
                return true;
            }
        }
    }
    if (const auto doc = app_.hmi()) {
        if (const auto* var = doc->project.variable(path)) {
            if (!model.addTrend(var->name, true, lowerOf(var->type) == "bool", why)) return false;
            simCenter_->reportDirty = true;
            return true;
        }
    }
    if (why)
        *why = "\xC2\xAB " + path + " \xC2\xBB n'est " + (hmiOnly ? std::string("pas une variable de l'IHM")
                                                                 : std::string("ni une variable de l'automate ni une variable de l'IHM"));
    return false;
}

void MainAnalysisScreen::releaseSimForcings(const std::vector<std::string>& keys) {
    ensureSimCenter();
    auto* rt = app_.simulation().attached() ? app_.simulationRuntime() : nullptr;
    std::size_t done = 0;
    std::map<std::string, std::vector<std::string>> twins;      // equipement -> "adresse|type"
    for (const auto& k : keys) {
        if (k.rfind("plc:", 0) == 0) {
            if (rt && rt->unforce(k.substr(4))) ++done;
        } else if (k.rfind("twin:", 0) == 0) {
            const auto rest = k.substr(5);
            const auto colon = rest.find(':');
            if (colon != std::string::npos) twins[rest.substr(0, colon)].push_back(rest.substr(colon + 1));
        }
    }
    if (!twins.empty())
        if (const auto doc = app_.hmi()) {
            std::size_t removed = 0;
            auto cmd = hmi::changeProject(doc, "Rel\xC3\xA2" "cher des cases des esclaves simul\xC3\xA9s", [&](hmi::Project& p) {
                for (auto& e : p.equipments) {
                    const auto it = twins.find(e.name);
                    if (it == twins.end()) continue;
                    const auto before = e.forcings.size();
                    std::erase_if(e.forcings, [&](const hmi::Forcing& f) {
                        return std::find(it->second.begin(), it->second.end(), f.address + "|" + f.type) != it->second.end();
                    });
                    removed += before - e.forcings.size();
                }
            });
            if (cmd) app_.apply(std::move(cmd), false);
            done += removed;
        }
    simCenter_->nextPoll = 0;      // la liste suit tout de suite
    if (status_)
        status_->setTransientMessage(done ? ss::plural(done, "for\xC3\xA7" "age rel\xC3\xA2" "ch\xC3\xA9", "for\xC3\xA7" "ages rel\xC3\xA2" "ch\xC3\xA9" "s")
                                                + (core::hasApi() ? " : le programme y \xC3\xA9" "crit de nouveau."
                                                                  : " : les esclaves simul\xC3\xA9s r\xC3\xA9pondent de nouveau leur m\xC3\xA9moire.")
                                          : std::string("Rien \xC3\xA0 rel\xC3\xA2" "cher."),
                                     6.0);
}

} // namespace app
