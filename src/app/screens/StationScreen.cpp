// app/screens/StationScreen.cpp - le poste d'exploitation (lot 14).
#include "StationScreen.hpp"
#include "../hmi/HmiParamPanes.hpp"   // 1.9 : les parametres des popups

#include "Screens.hpp"
#include "StationExportDialog.hpp"        // lot API 8 : ou enregistrer un export, au doigt
#include "../App.hpp"
#include "../Capture.hpp"
#include "../ExportTarget.hpp"            // lot API 8 : exportFileFor, exportWhatOf
#include "../hmi/HmiImages.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../../hmi/HmiCrypto.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace app {

// Un ecran secondaire : sa fenetre (son renderer, a l'App), son cache d'images,
// et le meme dessin que l'ecran principal - la vue evaluee (HmiLayerBuilder,
// avec la qualite des valeurs lues a l'automate), dessinee par un
// HmiLiveCanvas qui n'est dans aucun arbre de widgets.
struct StationScreen::Secondary {
    int                 display{2};
    std::string         viewName;
    gfx::IRenderer*     renderer{nullptr};   // a l'App (closeScreenWindows)
    HmiImageCache       images;
    HmiLayerBuilder     builder;
    HmiLiveCanvas       canvas{"station.screen"};
};

namespace {

// Un export (bouton d'export, rapport) : dans exports/ du dossier du projet.
// Lot API 8 : `target` - l'endroit choisi dans le dialogue du poste (vide :
// exports/, sous le nom habituel) ; la regle de l'editeur (exportFileFor).
bool writeExport(const std::string& projectFolder, const hmi::ExportRequest& rq, std::string* where, const std::string& target = {}) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path folder = projectFolder.empty() ? fs::temp_directory_path(ec) / "xpg-exports" : fs::path(projectFolder) / "exports";
    fs::create_directories(folder, ec);
    const fs::path file = exportFileFor(folder, rq.fileName, target);
    const bool elsewhere = file.parent_path().lexically_normal() != folder.lexically_normal()
                           || file.filename() != exportPathOf(rq.fileName).filename();
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out || !rq.data) {
        if (where) *where = "\xC3\xA9" "criture impossible : " + exportUtf8Of(file);
        return false;
    }
    out.write(reinterpret_cast<const char*>(rq.data->data()), static_cast<std::streamsize>(rq.data->size()));
    out.close();
    if (where) *where = projectFolder.empty() || elsewhere ? exportUtf8Of(file) : "exports/" + rq.fileName;
    return static_cast<bool>(out);
}

} // namespace

StationScreen::StationScreen(App& app) : menu::WidgetMenu("station"), app_(app) {}

// Les fenetres des ecrans secondaires sont a l'App : elle les ferme en sortant
// du poste (leaveStation) ou en s'arretant - pas ici, l'App peut etre deja a
// moitie detruite.
StationScreen::~StationScreen() = default;

menu::MenuTraits StationScreen::traits() const {
    menu::MenuTraits t;
    t.closableWithEscape = false;
    return t;
}

core::Status StationScreen::buildUi() {
    auto doc = app_.hmi();
    if (!doc) return core::fail(core::ErrorCode::InvalidArgument, "pas d'IHM");
    auto root = std::make_unique<ui::DockLayout>("station.root");
    HmiSimulationHost host;
    host.runtime = [this] { return app_.simulationRuntime(); };
    host.link = [this]() -> sim::Environment* { return app_.comm().link(); };
    host.commEvents = [this] {
        auto events = app_.comm().takeEvents();
        for (auto& e : app_.equipments().takeEvents()) events.push_back(std::move(e));   // lot 15
        return events;
    };
    // Lot 15 : les variables IHM liees a un equipement, l'etat des equipements.
    host.equipmentLink = [this](const std::string& name) { return app_.equipments().link(name); };
    host.equipmentStatus = [this] { return app_.equipments().statuses(); };
    // 1.9 : les esclaves simules (la page Simulation, Ctrl+Alt+S) ; leurs choix
    // s'oublient quand l'IHM redemarre.
    host.simSlaves = [this](bool withValues) {
        const auto d = app_.hmi();
        return d ? app_.equipments().simSlaves(d->project, withValues) : std::vector<hmi::SimSlave>{};
    };
    host.simSlaveCommand = [this](const hmi::SimSlaveCommand& c, std::string* why) {
        const auto d = app_.hmi();
        return d && app_.equipments().simSlaveCommand(d->project, c, why);
    };
    host.equipments = [this] { return &app_.equipments(); };
    host.commDemo = [this] { return std::make_pair(app_.comm().running(), app_.comm().demoPort()); };
    // Lot 14 : les notifications des alarmes, les rapports a envoyer.
    host.alarmNotice = [this](const hmi::AlarmNotice& n) { app_.notify().notice(n); };
    host.notifyStats = [this] {
        auto st = app_.notify().stats();
        st.webClients = app_.web().clientCount();
        return st;
    };
    host.notifyEvents = [this] {
        auto events = app_.notify().takeEvents();
        for (auto& e : app_.web().takeEvents()) events.push_back(std::move(e));
        return events;
    };
    host.reportWritten = [this](const hmi::ReportOutput& r) { app_.notify().report(r); };
    host.station = [this] { return std::make_pair(true, 1 + static_cast<int>(secondaryCount())); };
    host.state = [this] { return app_.simulation().attached() ? app_.simulation().statusLine() : std::string("simulateur non lanc\xC3\xA9"); };
    host.transport = [this](std::string_view id) {
        auto& sim = app_.simulation();
        if (!sim.attached() && app_.project()) (void)sim.attach(app_.project());
        if (!sim.attached()) return;
        if (id == "sim.prepare") return;   // 1.10 : l'IHM seule - la memoire, preparee sans cycle
        using State = SimulationHost::State;
        if (id == "sim.run") sim.setState(State::Running);
        else if (id == "sim.pause") sim.setState(State::Paused);
        else if (id == "sim.stop") sim.setState(State::Stopped);
        else sim.step();
    };
    host.plc = [this] {
        hmi::PlcStatus st;
        const auto& sim = app_.simulation();
        st.attached = sim.attached();
        st.running = sim.state() == SimulationHost::State::Running;
        st.paused = sim.state() == SimulationHost::State::Paused;
        st.halted = sim.state() == SimulationHost::State::Halted;
        st.scans = sim.scanCount();
        st.cycleMs = static_cast<int>(sim.scanIntervalMs());
        st.error = sim.haltMessage();
        if (const auto* rt = sim.runtime()) st.forced = static_cast<int>(rt->forcedNames().size());
        if (const auto plcProject = app_.project()) st.project = plcProject->header.projectName;
        return st;
    };
    host.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeExport(app_.projectFolder(), rq, where); };
    // ---- Lot API 8 : les exports qui demandent ou ----
    // Lance par un geste de l'operateur : le dialogue du poste, au doigt (askExport).
    host.askExport = [this](const hmi::ExportRequest& rq, std::function<void(bool, const std::string&)> done) {
        return askExport(rq, std::move(done));
    };
    // ---- fin Lot API 8 ----
    host.audit = [this](const hmi::AuditEntry& e) {
        const std::string folder = app_.projectFolder();
        if (!folder.empty() && hmi::exists(folder)) (void)hmi::appendAuditFile(e, folder);
    };
    hmiparams::setProgram([this] { return app_.project(); });   // 1.9 : les DDT pour les copies des parametres
    // 1.11.16 : la remanence d'exploitation - le stockage du poste, dans le dossier du projet.
    host.retainFile = [this] { const std::string folder = app_.projectFolder(); return folder.empty() ? std::string{} : hmi::retain::fileOf(folder).string(); };
    auto pane = std::make_unique<HmiSimulationPane>("station.pane", doc, std::move(host));
    pane->setStationMode(true);
    pane_ = pane.get();
    root->dock(std::move(pane), ui::DockLayout::Side::Center, 0.f);
    setRoot(std::move(root));
    return core::ok();
}

void StationScreen::onEnter() {
    auto doc = app_.hmi();
    if (!doc) return;
    // onEnter revient chaque fois qu'un dialogue se ferme au-dessus : le poste
    // ne demarre qu'une fois.
    if (started_) return;
    started_ = true;
    const auto& st = doc->project.station;
    app_.setStationWindow(st.fullScreen, st.hideCursor);
    // Le kiosque : fermer la fenetre (Alt+F4) demande le mot de passe de sortie.
    app_.setQuitGuard([this] {
        auto current = app_.hmi();
        if (!current || !current->project.station.kiosk) return false;
        askExit();
        return true;
    });
    // Sur le simulateur (ou le simulateur expose par le serveur de
    // demonstration) : l'automate simule tourne des le lancement.
    if ((!doc->project.comm.modbus() || doc->project.comm.demoServer) && st.runSimulator && app_.project()) {
        auto& sim = app_.simulation();
        if (!sim.attached()) (void)sim.attach(app_.project());
        if (sim.attached() && sim.state() != SimulationHost::State::Running) sim.setState(SimulationHost::State::Running);
    }
    if (pane_) {
        pane_->restart();
        pane_->runtime().log("Syst\xC3\xA8me", "Poste d'exploitation",
                             "Poste d'exploitation lanc\xC3\xA9" + std::string(doc->project.comm.modbus() ? " (automate " + doc->project.comm.host + ")" : " (simulateur)"));
    }
    openSecondaries();
    // Lot 15 : la reprise apres un arret brutal - l'etat d'avant (la vue, les
    // variables, les alarmes). Relance sans personne : 30 s pour dire non, puis d'office.
    bool ask = false;
    if (auto state = app_.takeStationRestore(&ask); state && pane_) {
        if (!ask) {
            std::string report;
            (void)pane_->runtime().restoreState(*state, pane_->runtime().now(), &report);
            pane_->reapplyRetained();   // 1.11.16 : les remanentes, plus recentes, l'emportent
            pane_->refreshNow();
        } else {
            auto dialog = std::make_unique<MessageDialog>(
                "Reprise du poste d'exploitation",
                "Le poste s'est arr\xC3\xAAt\xC3\xA9 brutalement (le PC s'est \xC3\xA9teint, ou le logiciel s'est ferm\xC3\xA9 sans passer par la sortie).\n\n"
                "Voulez-vous recharger les donn\xC3\xA9" "es pr\xC3\xA9" "c\xC3\xA9" "dentes : la vue montr\xC3\xA9" "e, les variables (les compteurs), "
                "les alarmes en cours et leur acquittement ?\n\nSans r\xC3\xA9ponse, elles sont recharg\xC3\xA9" "es d'office dans 30 s.",
                MessageDialog::Icon::Question, "Recharger");
            dialog->setCancelLabel("D\xC3\xA9marrer \xC3\xA0 neuf");
            dialog->setCountdown(30.0);
            const std::string text = *state;
            app_.menus().ShowDialog(std::move(dialog), [this, text](const menu::DialogResult& r) {
                if (!pane_) return;
                if (!r.accepted()) {
                    pane_->runtime().log("Syst\xC3\xA8me", "Reprise", "\xC3\xA9tat d'avant l'arr\xC3\xAAt non recharg\xC3\xA9 (d\xC3\xA9marrage \xC3\xA0 neuf)");
                    return;
                }
                std::string report;
                (void)pane_->runtime().restoreState(text, pane_->runtime().now(), &report);
                pane_->reapplyRetained();   // 1.11.16 : les remanentes, plus recentes, l'emportent
                pane_->refreshNow();
            });
        }
    }
}

// Un dialogue s'ouvre au-dessus (la sortie) : le poste continue dessous.
void StationScreen::onExit() {}

void StationScreen::openSecondaries() {
    closeSecondaries();
    auto doc = app_.hmi();
    if (!doc) return;
    const auto& st = doc->project.station;
    for (const auto& e : st.screens) {
        const auto* v = doc->project.viewByName(e.view);
        if (!v || e.display < 2) continue;
        auto sec = std::make_unique<Secondary>();
        sec->display = e.display;
        sec->viewName = e.view;
        sec->renderer = app_.openScreenWindow(e.display, v->width, v->height, doc->project.config.name + " - " + e.view, st.fullScreen);
        if (!sec->renderer) {
            if (pane_) pane_->runtime().log("Erreur", "Poste d'exploitation", "\xC3\x89" "cran " + std::to_string(e.display) + " : fen\xC3\xAAtre impossible");
            continue;
        }
        screens_.push_back(std::move(sec));
    }
    epoch_ = app_.screenEpoch();
}

void StationScreen::closeSecondaries() {
    if (screens_.empty()) return;
    screens_.clear();
    app_.closeScreenWindows();
    epoch_ = app_.screenEpoch();
}

void StationScreen::requestCapture(int display, std::string path) {
    captureDone_.erase(path);
    captures_.emplace_back(display, std::move(path));
}

std::optional<std::string> StationScreen::captureResult(const std::string& path) {
    const auto it = captureDone_.find(path);
    if (it == captureDone_.end()) return std::nullopt;
    std::string r = it->second;
    captureDone_.erase(it);
    return r;
}

std::size_t StationScreen::secondaryCount() const noexcept { return screens_.size(); }

gfx::IRenderer* StationScreen::screenRenderer(int display) const {
    if (app_.screenEpoch() != epoch_) return nullptr;
    for (const auto& s : screens_)
        if (s->display == display) return s->renderer;
    return nullptr;
}

void StationScreen::paintSecondaries(double time) {
    auto doc = app_.hmi();
    // L'App a ferme les fenetres (on sort du poste) : leurs renderers ne sont plus.
    if (!screens_.empty() && app_.screenEpoch() != epoch_) screens_.clear();
    // Une capture demandee d'un ecran qui n'est pas ouvert : pourquoi.
    for (auto it = captures_.begin(); it != captures_.end();) {
        const int display = it->first;
        if (std::none_of(screens_.begin(), screens_.end(), [display](const auto& sc) { return sc->display == display; })) {
            captureDone_[it->second] = "\xC3\xA9" "cran " + std::to_string(display) + " non ouvert (Configuration > Poste d'exploitation)";
            it = captures_.erase(it);
        } else {
            ++it;
        }
    }
    if (!doc || !pane_ || screens_.empty()) return;
    // Dix images par seconde suffisent a un ecran de supervision (une capture
    // demandee se fait tout de suite).
    if (captures_.empty() && lastSecondary_ >= 0 && time - lastSecondary_ < 0.1) return;
    lastSecondary_ = time;
    auto& rt = pane_->runtime();
    static const ui::Theme theme = ui::Theme::dark();
    for (auto& sec : screens_) {
        auto& r = *sec->renderer;
        const auto* v = doc->project.viewByName(sec->viewName);
        HmiImageCache::Scope scope(sec->images);
        r.beginFrame(gfx::Color{0, 0, 0, 255});
        const auto size = r.surfaceSize();
        if (v && rt.running()) {
            auto layers = sec->builder.build(rt, doc->project, rt.now(), nullptr, v->id);
            sec->canvas.setStation(true);
            sec->canvas.setAssets(&doc->project.assets);
            sec->canvas.setLive(&rt, &doc->history);
            sec->canvas.setProject(&doc->project);
            sec->canvas.showLayers(std::move(layers), false);
            sec->canvas.setBounds({0.f, 0.f, static_cast<float>(size.w), static_cast<float>(size.h)});
            sec->canvas.layout();
            const ui::PaintContext ctx{r, theme, {0.f, 0.f, static_cast<float>(size.w), static_cast<float>(size.h)}, time, nullptr};
            sec->canvas.render(ctx);
        } else {
            r.drawText({24.f, 24.f}, v ? std::string_view("L'IHM d\xC3\xA9marre...") : std::string_view("Vue introuvable"), theme.font.ui,
                       gfx::Color::rgb(0xC8D0DC));
        }
        // Les captures de cet ecran : avant de le presenter.
        for (auto it = captures_.begin(); it != captures_.end();) {
            if (it->first != sec->display) { ++it; continue; }
            const auto res = captureToPng(r, it->second);
            captureDone_[it->second] = res ? std::string{} : res.error().message();
            it = captures_.erase(it);
        }
        r.endFrame();
    }
}

void StationScreen::Render(gfx::IRenderer& r, const menu::FrameContext& fc) {
    WidgetMenu::Render(r, fc);
    now_ = fc.totalSeconds;
    paintSecondaries(fc.totalSeconds);
    // Lot 15 : l'etat de la marche, garde toutes les 10 s pour la reprise.
    if (pane_ && pane_->runtime().running() && fc.totalSeconds - lastSnapshot_ >= 10.0) {
        lastSnapshot_ = fc.totalSeconds;
        (void)app_.recovery().saveStationState(pane_->runtime().stateSnapshot());
    }
}

ui::EventResult StationScreen::HandleEvent(const ui::InputEvent& e) {
    // Ctrl+Alt+Q : la sortie.
    if (const auto* k = std::get_if<ui::KeyDown>(&e); k && k->key == ui::Key::Q && k->mods.ctrl && k->mods.alt) {
        askExit();
        return ui::EventResult::Consumed;
    }
    // 1.9 : Ctrl+Alt+S - la page Simulation de Parametres systeme (les esclaves
    // simules), meme sans bouton Parametres systeme dans le projet. Sans la
    // permission Administrer : la carte du refus, "Acces refuse" au journal.
    if (const auto* k = std::get_if<ui::KeyDown>(&e); k && k->key == ui::Key::S && k->mods.ctrl && k->mods.alt) {
        openSimulationPage();
        return ui::EventResult::Consumed;
    }
    // Lot 15 : F1 - l'aide, pour un utilisateur connecte au niveau le plus haut
    // (l'administrateur du poste) ; les autres n'y ont pas acces.
    if (const auto* k = std::get_if<ui::KeyDown>(&e); k && k->key == ui::Key::F1 && k->mods.none()) {
        auto doc = app_.hmi();
        if (doc && pane_) {
            int top = 1;
            for (const auto& g : doc->project.security.groups) top = std::max(top, g.level);
            const int level = pane_->runtime().level();
            if (level >= top) {
                pane_->runtime().log("S\xC3\xA9" "curit\xC3\xA9", "Poste d'exploitation", "Aide ouverte (F1) par " + (pane_->runtime().userLogin().empty() ? std::string("le poste") : pane_->runtime().userLogin()));
                app_.setHelpTopic("poste-exploitation");
                app_.menus().PushMenu("help.hmi");
            } else {
                pane_->runtime().log("S\xC3\xA9" "curit\xC3\xA9", "Poste d'exploitation",
                                     "Aide (F1) refus\xC3\xA9" "e : niveau " + std::to_string(level) + ", il faut le niveau " + std::to_string(top));
            }
        }
        return ui::EventResult::Consumed;
    }
    // Cinq touchers du coin haut droit en trois secondes (un ecran tactile).
    if (const auto* m = std::get_if<ui::MouseDown>(&e)) {
        auto doc = app_.hmi();
        const auto area = root().bounds();
        if (doc && doc->project.station.cornerExit && m->pos.x >= area.right() - 70.f && m->pos.y <= area.y + 70.f) {
            taps_.push_back(now_);
            std::erase_if(taps_, [this](double t) { return now_ - t > 3.0; });
            if (taps_.size() >= 5) {
                taps_.clear();
                askExit();
                return ui::EventResult::Consumed;
            }
        }
    }
    return WidgetMenu::HandleEvent(e);
}

void StationScreen::openSimulationPage() {
    auto doc = app_.hmi();
    if (!doc || !pane_) return;
    if (!doc->project.station.simPage) {
        // La fiche du poste l'a decochee : la page n'existe pas ici.
        pane_->runtime().log("S\xC3\xA9" "curit\xC3\xA9", "Poste d'exploitation",
                             "Ctrl+Alt+S : la page Simulation n'existe pas sur ce poste (Configuration \xE2\x80\xBA Poste d'exploitation)");
        return;
    }
    pane_->openSystemMenu(hmi::kSimulationTab, "Ctrl+Alt+S (poste d'exploitation)");
}

void StationScreen::askExit() {
    auto doc = app_.hmi();
    if (!doc) return;
    // Deja demande (Alt+F4 deux fois) : un seul dialogue.
    if (const auto* top = app_.menus().top(); top && top->id() == "dialog.stationExit") return;
    const auto& st = doc->project.station;
    const bool guarded = !st.exitHash.empty();
    std::vector<FormDialog::Field> fields;
    if (guarded) fields.push_back({"Mot de passe de sortie", "", "", true, {}});
    fields.push_back({"Ensuite", "Passer en conception", "", false, {"Passer en conception", "Quitter l'application"}});
    auto dialog = std::make_unique<FormDialog>(
        "dialog.stationExit", "Quitter le poste d'exploitation",
        guarded ? std::string("Le mot de passe de sortie (Configuration > Poste d'exploitation). Passer en conception : l'\xC3\xA9" "diteur, "
                              "sans fermer l'application ; Quitter : la fermer.")
                : std::string("Aucun mot de passe de sortie n'est d\xC3\xA9" "fini (Configuration > Poste d'exploitation) : la sortie est libre."),
        std::move(fields), "Valider");
    app_.menus().ShowDialog(std::move(dialog), [this, guarded](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto v = FormDialog::split(r.payload);
        const std::string password = guarded && !v.empty() ? v[0] : std::string{};
        const std::string next = v.empty() ? std::string{} : v.back();
        const auto& station = current->project.station;
        if (guarded && hmi::passwordHash(station.exitSalt, password) != station.exitHash) {
            if (pane_) pane_->runtime().log("S\xC3\xA9" "curit\xC3\xA9", "Poste d'exploitation", "Sortie refus\xC3\xA9" "e : mot de passe de sortie faux");
            return;
        }
        if (pane_) pane_->runtime().log("S\xC3\xA9" "curit\xC3\xA9", "Poste d'exploitation", "Sortie du poste : " + next);
        if (next.rfind("Quitter", 0) == 0) app_.requestQuit();
        else app_.leaveStation();
    });
}

// ---- Lot API 8 : les exports qui demandent ou ----
bool StationScreen::askExport(const hmi::ExportRequest& rq, std::function<void(bool, const std::string&)> done) {
    namespace fs = std::filesystem;
    const std::string projectFolder = app_.projectFolder();
    if (projectFolder.empty() || !done) return false;          // pas de dossier : exports/ du dossier temporaire, sans question
    StationExportDialog::Spec spec;
    spec.what = exportWhatOf(rq.source, rq.format);
    spec.proposed = exportUtf8Of(fs::path(projectFolder) / "exports" / exportPathOf(rq.fileName));
    spec.browse = ui::saveFile(exportFilterOf(rq.format), spec.proposed, "Exporter " + spec.what);
    // LE KIOSQUE : l'explorateur ouvre tout le disque (et ce qu'il permet d'y
    // faire). Comme F1, il est a l'administrateur du poste - le niveau le plus
    // haut ; les autres exportent dans exports/ (ou annulent), rien d'autre.
    if (const auto doc = app_.hmi(); doc && doc->project.station.kiosk && pane_) {
        int top = 1;
        for (const auto& g : doc->project.security.groups) top = std::max(top, g.level);
        if (pane_->runtime().level() < top) {
            spec.fixedPath = true;              // le chemin non plus ne se tape pas
            spec.browseDenied = "Kiosque : choisir un autre endroit est r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'administrateur du poste (niveau "
                                + std::to_string(top) + ") ; le fichier va dans exports/ du projet.";
        }
    }
    const bool fixed = spec.fixedPath;
    app_.menus().ShowDialog(std::make_unique<StationExportDialog>(std::move(spec)), [this, rq, done = std::move(done), fixed](const menu::DialogResult& r) {
        if (!r.accepted()) {
            done(false, {});
            return;
        }
        std::string where;
        const bool ok = writeExport(app_.projectFolder(), rq, &where, fixed ? std::string{} : r.payload);
        done(ok, where);
    });
    return true;
}
// ---- fin Lot API 8 ----

} // namespace app
