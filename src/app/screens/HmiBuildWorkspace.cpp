// =============================================================================
//  app/screens/HmiBuildWorkspace.cpp - 1.11.13 : la generation incrementale de l'IHM
// -----------------------------------------------------------------------------
//  La colle entre l'ecran et le build (hmi/HmiBuild.hpp, hmi::pipeline) :
//    - le gestionnaire, cree avec l'IHM ; chaque modification du document
//      relance l'analyse (300 ms apres la derniere) : l'arbre suit aussitot ;
//    - les etats dans l'arbre (ProjectTreeModel::HmiBuildMarks), refaits a
//      chaque analyse, jamais a l'image ;
//    - runHmiBuild : les commandes (menu de l'arbre, barres des editeurs,
//      Demarrer) ; la fenetre de progression s'ouvre si le build dure plus de
//      0,3 s (un projet a jour demarre sans fenetre) ;
//    - a la fin : les sorties (l'onglet IHM . Sorties), un bilan dans la barre
//      d'etat, la suite (`then` : demarrer la simulation).
//    - double-clic sur un diagnostic : sa source (openHmiIssue, comme Compiler).
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../hmi/HmiBuild.hpp"
#include "../hmi/HmiBuildPanes.hpp"
#include "../hmi/HmiCommHost.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiFunctionPanes.hpp"
#include "../hmi/HmiPanels.hpp"
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiSimulation.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace app {

namespace pl = hmi::pipeline;
using ui::StatusBar;

struct MainAnalysisScreen::HmiBuildDone {
    std::shared_ptr<const pl::Report> report;
    pl::Request                       request;
    double                            seconds{0};
};

namespace {
std::string plural(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
} // namespace

void MainAnalysisScreen::ensureHmiBuild() {
    if (hmiBuild_) return;
    hmiBuild_ = std::make_shared<HmiBuildManager>([this]() -> std::optional<HmiBuildSetup> {
        const auto doc = app_.hmi();
        if (!doc) return std::nullopt;
        HmiBuildSetup s;
        s.project = std::make_shared<const hmi::Project>(doc->project);
        const auto plc = app_.project();
        s.api = hmiApiInfo(plc.get());
        s.projectFolder = app_.projectFolder();
        if (plc) {
            auto names = hmiPlcUpperNames(plc.get());
            s.plcHasName = [names](std::string_view root) {
                std::string u(root);
                for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                return names->count(u) > 0;
            };
        }
        s.plcPaths = hmiPlcPaths(plc.get());
        s.plan = CommHost::planFor(doc->project.comm, plc.get(), app_.simulationRuntime());
        return s;
    });
    hmiBuildLinks_ += hmiBuild_->finished->connect([this](std::shared_ptr<const pl::Report> rep) {
        if (!rep) return;
        auto done = std::make_shared<HmiBuildDone>();
        done->report = rep;
        done->request = hmiBuild_->lastRequest();
        done->seconds = hmiBuild_->elapsed();
        hmiBuildHistory_.push_back(done);
        if (hmiBuildHistory_.size() > 20) hmiBuildHistory_.erase(hmiBuildHistory_.begin());
        const bool failed = !rep->ok && !rep->cancelled;
        if (auto* out = hmiBuildOutput(failed)) {
            out->addReport(*rep, done->request, done->seconds);
            if (failed) out->showTab(HmiBuildOutputPane::kDiagnostics);
        }
        if (status_) {
            if (rep->locked) status_->setTransientMessage("Build refus\xC3\xA9 : un autre build tourne sur ce projet.", 8.0, StatusBar::Severity::Error);
            else if (rep->cancelled) status_->setTransientMessage("Build annul\xC3\xA9 : les artefacts valides pr\xC3\xA9" "c\xC3\xA9" "dents sont conserv\xC3\xA9s.", 6.0, StatusBar::Severity::Warning);
            else if (failed)
                status_->setTransientMessage(hmiBuildTitle_ + " : " + plural(rep->errors, "erreur", "erreurs") + (hmiBuildStarts_ ? " \xE2\x80\x94 la simulation ne d\xC3\xA9marre pas" : std::string{})
                                                 + " (panneau du bas, Diagnostics : double-clic, la source).",
                                             10.0, StatusBar::Severity::Error);
            else if (rep->upToDate)
                status_->setTransientMessage("Projet \xC3\xA0 jour" + std::string(hmiBuildStarts_ ? " \xE2\x80\x94 la simulation d\xC3\xA9marre." : "."), 4.0, StatusBar::Severity::Success);
            else
                status_->setTransientMessage(hmiBuildTitle_ + " : " + std::to_string(rep->generated) + " g\xC3\xA9n\xC3\xA9r\xC3\xA9(s), " + std::to_string(rep->compiled)
                                                 + " compil\xC3\xA9(s), " + std::to_string(rep->reused) + " r\xC3\xA9utilis\xC3\xA9(s) \xE2\x80\x94 "
                                                 + plural(rep->warnings, "avertissement", "avertissements") + ".",
                                             6.0, StatusBar::Severity::Success);
        }
        hmiBuildStartedAt_ = -1.0;
        auto then = std::move(hmiBuildThen_);
        hmiBuildThen_ = nullptr;
        if (then) then(*rep);
    });
    hmiBuildLinks_ += hmiBuild_->statusChanged->connect([this] { applyHmiBuildMarks(); });
    hmiBuild_->setClock([this] { return frameClock_; });   // les 300 ms en temps d'images (les sessions rejouees aussi)
    hmiBuild_->analyseNow();
}

bool MainAnalysisScreen::runHmiBuild(pl::Mode mode, std::vector<std::string> scope, bool chosen, std::string title,
                                     std::function<void(const pl::Report&)> then) {
    ensureHmiBuild();
    if (!hmiBuild_) return false;
    std::string why;
    if (!hmiBuild_->start(pl::Request{mode, std::move(scope), chosen}, &why)) {
        if (status_) status_->setTransientMessage(title + " impossible : " + why + ".", 6.0, StatusBar::Severity::Warning);
        return false;
    }
    hmiBuildTitle_ = std::move(title);
    hmiBuildStarts_ = mode == pl::Mode::Start;
    hmiBuildThen_ = std::move(then);
    hmiBuildStartedAt_ = frameClock_;
    hmiBuildBackground_ = false;
    if (status_) status_->setTransientMessage(hmiBuildTitle_ + " : analyse des modifications\xE2\x80\xA6", 3.0);
    return true;
}

void MainAnalysisScreen::startHmiBuild(const std::string& source) {
    ensureHmiBuild();
    if (hmiBuild_ && hmiBuild_->building()) {
        hmiStartPending_ = source.empty() ? std::string("d\xC3\xA9marrage") : source;
        if (status_) status_->setTransientMessage("Un build est en cours : la simulation d\xC3\xA9marrera \xC3\xA0 sa fin (sur un build valide).", 6.0);
        return;
    }
    const bool launched = runHmiBuild(pl::Mode::Start, {}, false, "D\xC3\xA9marrer la simulation", [this](const pl::Report& r) {
        auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (!sim) return;
        std::string why;
        if (r.locked) why = "un autre build tourne sur ce projet";
        else if (r.cancelled) why = "build annul\xC3\xA9";
        else if (!r.ok) why = plural(r.errors, "erreur bloquante", "erreurs bloquantes");
        sim->buildDone(r.ok, why);
    });
    if (!launched)
        if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) sim->buildDone(false, "le build n'a pas pu partir (aucun projet IHM ?)");
}

void MainAnalysisScreen::tickHmiBuild() {
    if (bottomPanel_) bottomPanel_->tick();          // 1.11.14 : la Console (les lignes arrivees)
    if (!hmiBuild_) {
        if (app_.hmi()) ensureHmiBuild();
        return;
    }
    (void)hmiBuild_->poll();
    // Les barres des editeurs ouverts : l'etat de l'element choisi (rien ne se redessine s'il est le meme).
    for (const auto& [key, page] : hmiTabs_) {
        if (auto* sp = dynamic_cast<HmiScriptsPane*>(page)) sp->refreshBuildState();
        else if (auto* fp = dynamic_cast<HmiFunctionsPane*>(page)) fp->refreshBuildState();
        else if (auto* ed = dynamic_cast<HmiEditor*>(page)) {
            if (auto* sf = ed->symbolFunctions()) {
                if (!sf->hosts().build) {   // les fonctions d'un symbole : la barre de build aussi
                    HmiFunctionsPane::Hosts h = sf->hosts();
                    wireHmiBuildHosts(h);
                    sf->setHosts(std::move(h));
                }
                sf->refreshBuildState();
            }
        }
    }
    // Un demarrage attendait la fin d'un autre build : son tour.
    if (!hmiStartPending_.empty() && !hmiBuild_->building()) {
        const std::string source = std::move(hmiStartPending_);
        hmiStartPending_.clear();
        startHmiBuild(source);
    }
    // La fenetre de progression : un build qui dure (un projet a jour demarre sans elle).
    if (hmiBuild_->building() && !hmiBuildDialogOpen_ && !hmiBuildBackground_ && hmiBuildStartedAt_ >= 0.0 && frameClock_ - hmiBuildStartedAt_ > 0.3) {
        const auto pr = hmiBuild_->progress();
        if (pr.total > 0) {
            hmiBuildDialogOpen_ = true;
            app_.menus().ShowDialog(std::make_unique<HmiBuildProgressDialog>(hmiBuild_, hmiBuildTitle_, hmiBuildStarts_), [this](const menu::DialogResult& r) {
                hmiBuildDialogOpen_ = false;
                if (r.payload == "background") {
                    hmiBuildBackground_ = true;
                    if (status_) status_->setTransientMessage(hmiBuildTitle_ + " continue en arri\xC3\xA8re-plan : les sorties le diront \xC3\xA0 la fin.", 6.0);
                } else if (r.payload == "outputs")
                    (void)hmiBuildOutput(true);
            });
        }
    }
}

void MainAnalysisScreen::applyHmiBuildMarks() {
    if (!hmiBuild_ || !treeModel_) return;
    const auto st = hmiBuild_->status();
    if (!st || st->generation == hmiBuildMarksGen_) return;
    hmiBuildMarksGen_ = st->generation;
    auto marks = std::make_shared<ProjectTreeModel::HmiBuildMarks>();
    const auto mark = [](pl::State state, int warnings, const std::string& tip) {
        const auto look = hmiStateLook(state, warnings);
        return ProjectTreeModel::HmiBuildMarks::Mark{std::string(look.glyph), look.tone, hmiStateRank(state, warnings), tip};
    };
    for (const auto& [key, s] : st->shown) {
        std::string tip = s.tip;
        if (const auto* e = st->analysis.element(key)) tip = e->path + "\n" + tip;
        marks->byKey[key] = mark(s.state, s.state == pl::State::UpToDate ? s.warnings : 0, tip);
    }
    for (const auto& [path, f] : st->folders)
        marks->byPath[path] = mark(f.state, f.warnings, std::string(pl::stateLabel(f.state)) + " \xE2\x80\x94 " + f.tip);
    for (const auto& e : st->analysis.elements)
        if ((e.kind == pl::ElementKind::View || e.kind == pl::ElementKind::Popup || e.kind == pl::ElementKind::Symbol || e.kind == pl::ElementKind::ViewTemplate)
            && e.id != hmi::kNoId && e.key.rfind("paquet-modele:", 0) != 0) {
            marks->viewPaths[e.id] = e.path;
            marks->viewKeys[e.id] = e.key;
        }
    treeModel_->setHmiBuildMarks(std::move(marks));
    if (explorer_) explorer_->invalidate();
}

HmiBuildOutputPane* MainAnalysisScreen::hmiBuildOutput(bool open) {
    // 1.11.14 : les sorties vivent dans le panneau du bas (plus d'onglet IHM . Sorties).
    if (open) showBottomPanel(true);
    return bottomPanel_;
}

// 1.11.14 : le panneau du bas - ses signaux, une fois, a la construction de l'ecran.
void MainAnalysisScreen::wireBottomPanel() {
    if (!bottomPanel_) return;
    links_ += bottomPanel_->diagnosticActivated->connect([this](const pl::Diagnostic& d) { openHmiDiagnostic(d); });
    links_ += bottomPanel_->elementActivated->connect([this](const std::string& k) { openHmiElement(k); });
    links_ += bottomPanel_->consoleActivated->connect([this](const ConsoleEntry& e) { openConsoleSource(e); });
    bottomPanel_->setOnClose([this] { showBottomPanel(false); });
    bottomPanel_->setExportFolder([this] {
        const std::string folder = app_.projectFolder();
        return folder.empty() ? std::string{} : (std::filesystem::path(folder) / "exports").string();
    });
}

// 1.11.14 : une ligne de la Console - aller a ce qui l'a dite (le script a sa ligne,
// la fonction a sa ligne, l'objet dans sa vue, la vue).
void MainAnalysisScreen::openConsoleSource(const ConsoleEntry& e) {
    auto doc = app_.hmi();
    if (!doc) return;
    if (status_)
        status_->setTransientMessage(std::string(hmi::logLevelName(e.level)) + " \xC2\xB7 " + (e.where().empty() ? std::string{} : e.where() + " : ") + e.message, 8.0,
                                     e.level >= hmi::LogLevel::Error ? StatusBar::Severity::Error
                                     : e.level == hmi::LogLevel::Warning ? StatusBar::Severity::Warning : StatusBar::Severity::Info);
    const auto& p = doc->project;
    if (e.function != hmi::kNoId) {
        for (const auto& f : p.programs.functions)
            if (f.id == e.function) {
                hmi::Issue i;
                i.category = "Fonction";
                i.item = e.function;
                i.line = e.line;
                i.message = e.message;
                openHmiIssue(i);
                return;
            }
        for (const auto& v : p.views)           // une fonction d'un symbole : le symbole, ses Fonctions
            for (const auto& f : v.functions)
                if (f.id == e.function) {
                    openHmiView(v.id, static_cast<int>(ProjectTreeModel::HmiPart::Functions));
                    return;
                }
    }
    if (e.script != hmi::kNoId && p.script(e.script)) {
        hmi::Issue i;
        i.script = e.script;
        i.line = e.line;
        i.message = e.message;
        openHmiIssue(i);
        return;
    }
    if (e.view != hmi::kNoId) {
        hmi::Issue i;
        i.category = "Console";
        i.view = e.view;
        i.object = e.object;
        i.message = e.message;
        openHmiIssue(i);
    }
}

// La barre d'un editeur : la commande sur l'element choisi, nomme dans la barre d'etat.
void MainAnalysisScreen::runHmiBuildFor(pl::Mode mode, const std::string& key) {
    if (key.empty()) return;
    std::string what = key;
    if (hmiBuild_)
        if (const auto st = hmiBuild_->status())
            if (const auto* e = st->analysis.element(key)) what = std::string(pl::kindLabel(e->kind)) + " " + e->name;
    (void)runHmiBuild(mode, {key}, true, std::string(pl::modeLabel(mode)) + " : " + what);
}

void MainAnalysisScreen::replayHmiBuilds(HmiBuildOutputPane& pane) {
    for (const auto& done : hmiBuildHistory_)
        if (done && done->report) pane.addReport(*done->report, done->request, done->seconds);
}

MainAnalysisScreen::HmiBuildBadge MainAnalysisScreen::hmiBuildBadge(const std::string& key) const {
    HmiBuildBadge b;
    if (!hmiBuild_ || key.empty()) return b;
    const auto st = hmiBuild_->status();
    const auto* s = st ? st->of(key) : nullptr;
    if (!s) return b;
    const auto look = hmiStateLook(s->state, s->state == pl::State::UpToDate ? s->warnings : 0);
    b.glyph = std::string(look.glyph);
    b.tone = look.tone;
    b.label = std::string(pl::stateLabel(s->state));
    if (s->state == pl::State::GenerationFailed || s->state == pl::State::CompilationFailed) b.label += " (" + plural(s->errors, "erreur", "erreurs") + ")";
    else if (s->state == pl::State::UpToDate && s->warnings) b.label += " (" + plural(s->warnings, "avertissement", "avertissements") + ")";
    b.tip = s->tip;
    b.known = true;
    return b;
}

MainAnalysisScreen::HmiBuildInfo MainAnalysisScreen::hmiBuildInfo(const std::string& key, const std::vector<std::string>& paths) const {
    HmiBuildInfo info;
    if (!hmiBuild_) return info;
    const auto st = hmiBuild_->status();
    if (!st) return info;
    if (!key.empty()) {
        if (const auto* s = st->of(key)) info.state = std::string(pl::stateLabel(s->state)) + (s->detail.empty() ? std::string{} : ", " + s->detail);
        if (const auto it = st->cache.entries.find(key); it != st->cache.entries.end()) {
            info.diagnostics = it->second.diagnostics;
            const std::string folder = app_.projectFolder();
            if (!it->second.artifact.empty() && !folder.empty() && it->second.generation == pl::State::Generated)
                info.artifact = (std::filesystem::path(pl::buildFolderOf(folder)) / it->second.artifact).string();
        }
    } else {
        const HmiBuildStatus::Summary* worst = nullptr;
        for (const auto& path : paths)
            if (const auto* f = st->folder(path); f && (!worst || hmiStateRank(f->state, f->warnings) > hmiStateRank(worst->state, worst->warnings))) worst = f;
        if (worst) info.state = std::string(pl::stateLabel(worst->state));
        for (const auto& [k, en] : st->cache.entries) {
            const auto* e = st->analysis.element(k);
            if (!e || !pl::inScope(*e, paths)) continue;
            info.diagnostics.insert(info.diagnostics.end(), en.diagnostics.begin(), en.diagnostics.end());
        }
    }
    std::stable_sort(info.diagnostics.begin(), info.diagnostics.end(), [](const pl::Diagnostic& a, const pl::Diagnostic& b) { return a.blocking() && !b.blocking(); });
    return info;
}

void MainAnalysisScreen::openHmiDiagnostic(const pl::Diagnostic& d) {
    auto doc = app_.hmi();
    if (!doc) return;
    if (status_ && !d.message.empty())
        status_->setTransientMessage((d.code.empty() ? std::string{} : d.code + " \xC2\xB7 ") + (d.path.empty() ? std::string{} : d.path + " : ")
                                         + (d.line ? "ligne " + std::to_string(d.line) + (d.column ? ", colonne " + std::to_string(d.column) : std::string{}) + " : " : std::string{})
                                         + d.message,
                                     10.0, d.blocking() ? StatusBar::Severity::Error : StatusBar::Severity::Warning);
    // Une fonction d'un symbole : le symbole, son sous-onglet Fonctions.
    if (d.element.rfind("fonction-symbole:", 0) == 0 && d.view != hmi::kNoId) {
        openHmiView(d.view, static_cast<int>(ProjectTreeModel::HmiPart::Functions));
        return;
    }
    // Ce que Compiler et Generer savent ouvrir (un script et sa ligne, une fonction, un objet...).
    if (d.script != hmi::kNoId || d.view != hmi::kNoId || d.item != hmi::kNoId || d.category == "Variable IHM") {
        hmi::Issue i;
        i.severity = d.blocking() ? hmi::Issue::Severity::Error : d.severity == pl::Severity::Warning ? hmi::Issue::Severity::Warning : hmi::Issue::Severity::Info;
        i.category = d.category;
        i.view = d.view;
        i.object = d.object;
        i.property = d.property;
        i.message = d.message;
        i.script = d.script;
        i.line = d.line;
        i.item = d.item;
        i.column = d.column;
        i.length = d.length;
        openHmiIssue(i);
        return;
    }
    openHmiElement(d.element);
}

void MainAnalysisScreen::openHmiElement(const std::string& key) {
    auto doc = app_.hmi();
    if (!doc || key.empty()) return;
    const auto colon = key.find(':');
    const std::string kind = key.substr(0, colon);
    std::uint64_t id = 0;
    if (colon != std::string::npos) {
        const std::string rest = key.substr(colon + 1);
        if (!rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
            id = std::stoull(rest);
    }
    const auto& p = doc->project;
    using Part = ProjectTreeModel::HmiPart;
    if (kind == "vue" || kind == "popup" || kind == "symbole" || kind == "modele") openHmiView(id);
    else if (kind == "animations") openHmiView(id, static_cast<int>(Part::Animations));
    else if (kind == "actions") openHmiView(id);
    else if (kind == "script-vue") openHmiScripts(p.viewOfScript(static_cast<hmi::Id>(id)), id, 0);
    else if (kind == "script") openHmiScripts(0, id, 0);
    else if (kind == "fonction") openHmiFunctions(id, 0);
    else if (kind == "fonction-symbole") {
        for (const auto& v : p.views)
            for (const auto& f : v.functions)
                if (f.id == static_cast<hmi::Id>(id)) {
                    openHmiView(v.id, static_cast<int>(Part::Functions));
                    return;
                }
    } else if (kind == "variable") {
        openHmiPane("scripts");
        if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) pane->selectVariable(static_cast<hmi::Id>(id));
    } else if (kind == "type") {
        openHmiPane("scripts");
        if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
            pane->showTab(HmiScriptsPane::TabTypes);
            if (auto* types = pane->typesPane()) types->selectType(static_cast<hmi::Id>(id));
        }
    } else if (kind == "alarme" || kind == "alarmes-reglages") openHmiPane("alarmes");
    else if (kind == "recette") openHmiPane("recettes");
    else if (kind == "securite") openHmiPane("utilisateurs");
    else if (kind == "historiques") openHmiPane("historiques");
    else if (kind == "affichages") openHmiPane("unites");
    else if (kind == "langues") openHmiPane("langues");
    else if (kind == "styles") openHmiPane("styles");
    else if (kind == "ressource" || kind == "externe") openHmiPane(kind == "ressource" ? "ressources" : "fichiers");
    else if (kind == "simulation") openHmiPane("config");
    else if (kind == "api-echanges") openHmiPane("echange");
    else if (status_) status_->setTransientMessage(key + " : rien \xC3\xA0 ouvrir ici (un \xC3\xA9l\xC3\xA9ment de l'API : voir l'arbre API).", 5.0);
}

// ---- les scripts et les essais ------------------------------------------------------
bool MainAnalysisScreen::scriptHmiBuild(const std::string& mode, const std::string& scope, bool dialog, std::string* why) {
    static const std::pair<const char*, pl::Mode> kModes[] = {
        {"generer", pl::Mode::Generate}, {"regenerer", pl::Mode::Regenerate}, {"compiler", pl::Mode::Compile},
        {"generer-compiler", pl::Mode::GenerateCompile}, {"regenerer-compiler", pl::Mode::RegenerateCompile},
        {"demarrer", pl::Mode::Start}, {"nettoyer", pl::Mode::Clean}};
    const pl::Mode* m = nullptr;
    for (const auto& [k, v] : kModes)
        if (mode == k) m = &v;
    if (!m) {
        if (why) *why = "mode inconnu : " + mode;
        return false;
    }
    if (!app_.hmi()) {
        if (why) *why = "pas d'IHM";
        return false;
    }
    std::vector<std::string> sc;
    if (!scope.empty()) sc.push_back(scope);
    const bool chosen = !scope.empty() && scope.find('/') == std::string::npos;
    if (*m == pl::Mode::Start) {
        // demarrer : comme Demarrer l'IHM - l'onglet Simulation . IHM, son build, puis la
        // simulation s'il est valide (un build « Demarrer » sans elle mentirait dans sa fenetre).
        if (!hmiTab("simulation")) openHmiPane("simulation");
        auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (sim && !(hmiBuild_ && hmiBuild_->building())) sim->startHmi("session");
        if (!sim || !hmiBuild_ || !hmiBuild_->building()) {
            if (why) *why = sim ? "la simulation tourne d\xC3\xA9j\xC3\xA0" : "pas d'onglet Simulation \xC2\xB7 IHM";
            return false;
        }
    } else if (!runHmiBuild(*m, sc, chosen, std::string(pl::modeLabel(*m)) + (scope.empty() ? std::string{} : " " + scope))) {
        if (why) *why = "un build est d\xC3\xA9j\xC3\xA0 en cours";
        return false;
    }
    if (!dialog) hmiBuildBackground_ = true;   // sans fenetre : la barre d'etat et les sorties seulement
    if (dialog && !hmiBuildDialogOpen_) {
        hmiBuildDialogOpen_ = true;
        app_.menus().ShowDialog(std::make_unique<HmiBuildProgressDialog>(hmiBuild_, hmiBuildTitle_, hmiBuildStarts_), [this](const menu::DialogResult& r) {
            hmiBuildDialogOpen_ = false;
            if (r.payload == "outputs") (void)hmiBuildOutput(true);
        });
    }
    return true;
}

bool MainAnalysisScreen::hmiBuildBusy() const {
    return hmiBuild_ && (hmiBuild_->building() || !hmiStartPending_.empty() || !hmiBuild_->settled());   // une analyse attendue compte
}

std::string MainAnalysisScreen::hmiBuildSummary() const {
    if (!hmiBuild_) return "pas de build";
    std::string out;
    if (const auto st = hmiBuild_->status()) out = st->headline();
    if (const auto rep = hmiBuild_->lastReport())
        out += " | dernier build : " + std::string(rep->upToDate ? "projet \xC3\xA0 jour" : rep->ok ? "r\xC3\xA9ussi" : rep->cancelled ? "annul\xC3\xA9" : "\xC3\xA9" "chou\xC3\xA9")
             + ", " + std::to_string(rep->generated) + " g\xC3\xA9n\xC3\xA9r\xC3\xA9(s), " + std::to_string(rep->compiled) + " compil\xC3\xA9(s), "
             + std::to_string(rep->errors) + " erreur(s)";
    return out;
}

std::string MainAnalysisScreen::hmiConsoleSummary() const {
    if (!bottomPanel_) return "pas de panneau du bas";
    const auto& c = bottomPanel_->console();
    std::string out = std::to_string(c.entries().size()) + " ligne(s), " + std::to_string(c.errors()) + " erreur(s), " + std::to_string(c.warnings())
                    + " avertissement(s), session " + std::to_string(c.session());
    if (!c.entries().empty()) out += " | derni\xC3\xA8re : " + HmiConsole::lineOf(c.entries().back());
    return out;
}

bool MainAnalysisScreen::hmiConsoleHas(std::string_view text) const {
    if (!bottomPanel_) return false;
    for (const auto& e : bottomPanel_->console().entries())
        if (HmiConsole::lineOf(e).find(text) != std::string::npos || e.message.find(text) != std::string::npos) return true;
    return false;
}

void MainAnalysisScreen::showHmiBuildOutputs(int tab) {
    // 0 : Sorties ; 1 : Diagnostics ; 2 : Console (1.11.14).
    if (auto* out = hmiBuildOutput(true))
        out->showTab(tab == 1 ? HmiBuildOutputPane::kDiagnostics : tab == 2 ? HmiBuildOutputPane::kConsole : HmiBuildOutputPane::kSorties);
}

bool MainAnalysisScreen::scriptHmiEditScript(const std::string& name, const std::string& line, std::string* why) {
    auto doc = app_.hmi();
    if (!doc) {
        if (why) *why = "pas d'IHM";
        return false;
    }
    hmi::Id target = hmi::kNoId;
    for (const auto& sc : doc->project.programs.scripts)
        if (sc.name == name) target = sc.id;
    for (const auto& v : doc->project.views)
        for (const auto& sc : v.scripts)
            if (target == hmi::kNoId && (sc.name == name || v.name + "." + sc.name == name)) target = sc.id;
    if (target == hmi::kNoId) {
        if (why) *why = "script introuvable : " + name;
        return false;
    }
    auto cmd = hmi::changeProject(doc, "Modifier le script " + name, [&](hmi::Project& p) {
        if (auto* sc = p.script(target)) sc->body += (sc->body.empty() || sc->body.back() == '\n' ? "" : "\n") + line + "\n";
    });
    if (!cmd) {
        if (why) *why = "rien n'a chang\xC3\xA9";
        return false;
    }
    app_.apply(std::move(cmd), false);
    return true;
}

} // namespace app
