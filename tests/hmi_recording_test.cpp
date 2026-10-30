// =============================================================================
//  tests/hmi_recording_test.cpp - 1.14.0 (M4) : ENREGISTRER ET REJOUER L'IHM
// -----------------------------------------------------------------------------
//  Sans ecran :
//   - le format : l'en-tete JSON, les noms, les durees ; une piste ecrite puis
//     relue telle quelle, les images completes toutes les 5 s, seuls les
//     changements entre deux ;
//   - LE FILM : une IHM simulee qui change des valeurs (un script cyclique),
//     ouvre une popup, leve une alarme (acquittee), recoit des clics, enregistree
//     N secondes ; puis relue - chaque valeur, la vue, les popups et les alarmes,
//     a chaque instant, egales a ce qui etait montre ; aller a un instant (image
//     complete puis changements) egal a la lecture continue jusqu'a lui ;
//   - la relecture : -10 s / +10 s, les vitesses, la pause, la fin, Lecture qui
//     repart du debut ; l'etat d'avant rendu exactement en quittant ;
//   - le fichier : mot de passe juste, faux, absent (et la piste illisible sans
//     lui), change, retire ; un fichier tronque relu jusqu'au dernier bloc ; un
//     enregistrement coupe net (.part) relu ; importer, exporter, renommer,
//     supprimer (vers la corbeille), la description ; la taille du dossier ;
//   - le projet change depuis : une variable supprimee ignoree et signalee ;
//   - les natives, les actions et les variables systeme, chacune ;
//   - l'Enregistreur (la question a l'arret) et le Lecteur en marche ;
//   - la ligne de commande : les options, les codes, le refus de la configuration.
//
//  RECORDING_OBS=1 : chaque observation.
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRecording.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiArchive.hpp"     // 1.14.0 (relecture M4b) : les enregistrements d'une archive
#include "../src/hmi/HmiNotify.hpp"      // 1.14.0 (relecture M4b) : le mot de passe masque de l'Enregistreur
#include "../src/project/ProjectArchive.hpp"
#include "../third_party/miniz.h"        // 1.14.0 (relecture M4b) : des fichiers hostiles fabriques
#include <chrono>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#  define XPG_GETPID _getpid
#else
#  include <unistd.h>
#  define XPG_GETPID getpid
#endif

using namespace hmi;
namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("RECORDING_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Un dossier de projet a soi, vide.
std::string freshFolder(const std::string& name) {
    const fs::path p = fs::temp_directory_path() / ("xpg-m4-" + std::to_string(XPG_GETPID()) + "-" + name);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return utf8(p);
}

Variable variable(Project& p, const char* name, const char* type, const char* initial) {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    return v;
}

Action act(Trigger t, Operation op, std::string target, std::string value = {}, std::string params = {}) {
    Action a;
    a.trigger = t;
    a.operation = op;
    a.target = std::move(target);
    a.value = std::move(value);
    a.params = std::move(params);
    return a;
}

// LA STATION : un niveau qui monte (un script cyclique), une pompe qu'un bouton bascule, un compteur, une
// alarme de niveau haut, une popup a parametre.
struct Station {
    Project p;
    Id      view{kNoId}, popup{kNoId}, button{kNoId}, recorder{kNoId}, player{kNoId};
    std::string folder;
    explicit Station(const std::string& tag, bool external = false) {
        folder = freshFolder(tag);
        p.config.name = "Station_Relevage";
        p.config.version = "V1";
        p.config.cycleMs = 100;
        p.programs.variables.push_back(variable(p, "Niveau", "REAL", "10.0"));
        p.programs.variables.push_back(variable(p, "Pompe", "BOOL", "FALSE"));
        p.programs.variables.push_back(variable(p, "Compteur", "INT", "0"));
        p.programs.variables.push_back(variable(p, "Mode", "STRING", "'Auto'"));
        Script cyc;
        cyc.id = p.allocate();
        cyc.name = "Procede";
        cyc.event = "Cyclique";
        cyc.periodMs = 100;
        cyc.body = "Niveau := Niveau + 0.5;\nCompteur := Compteur + 1;\n";
        p.programs.scripts.push_back(cyc);
        AlarmDef al;
        al.id = p.allocate();
        al.name = "Niveau_Haut";
        al.condition = "Niveau > 13.0";
        al.message = "Niveau tr\xC3\xA8s haut";
        al.priority = 1;
        p.alarms.push_back(al);
        View v = makeView(p, "Vue_Station");
        button = edit::add(p, v, Kind::Button, 100, 100);
        v.object(button)->name = "Btn_Pompe";
        v.object(button)->actions.push_back(act(Trigger::Click, Operation::Toggle, "Pompe"));
        recorder = edit::add(p, v, Kind::Recorder, 400, 100);
        v.object(recorder)->name = "Enregistreur";
        v.object(recorder)->set("recName", "Essai {Compteur}");
        player = edit::add(p, v, Kind::RecordingPlayer, 400, 300);
        v.object(player)->name = "Lecteur";
        view = v.id;
        View pop = makeView(p, "Popup_Pompe");
        pop.role = "popup";
        pop.width = 300;
        pop.height = 200;
        ViewParam vp;
        vp.name = "Titre";
        vp.type = "STRING";
        pop.params.push_back(vp);
        popup = pop.id;
        p.config.startView = v.id;
        p.views = {v, pop};
        p.recording.external = external;
    }
    void hooks(Runtime& rt) {
        Runtime::Hooks h;
        const std::string f = folder;
        h.projectFolder = [f] { return f; };
        rt.setHooks(h);
    }
};

std::string journalText(const Runtime& rt) {
    std::string out;
    for (const auto& e : rt.journal()) out += e.kind + " | " + e.source + " | " + e.message + "\n";
    return out;
}
bool journalHas(const Runtime& rt, const std::string& needle) { return journalText(rt).find(needle) != std::string::npos; }

// Une valeur en clair (un texte sans ses apostrophes).
std::string plainOf(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }
std::string valueText(const sim::Value* v) { return v ? plainOf(*v) : std::string("(rien)"); }
std::string readText(Runtime& rt, const std::string& name) {
    sim::Value v;
    return rt.environment().read(name, v) ? plainOf(v) : std::string("(rien)");
}

std::string fileBytes(const std::string& path) {
    std::ifstream in(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
void writeBytes(const std::string& path, const std::string& data) {
    std::ofstream o(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
    o.write(data.data(), static_cast<std::streamsize>(data.size()));
}

// ================================================================================ le format ==
void testFormat() {
    std::printf("format : en-tete, noms, durees\n");
    rec::Header h;
    h.name = "D\xC3\xA9marrage \"station\"";
    h.description = "ligne 1\nligne 2";
    h.date = "2026-10-29 14:02:00";
    h.duration = 135.25;
    h.project = "Station_Relevage";
    h.startView = "Vue_Station";
    h.where = "Simulation";
    h.by = "Quentin";
    h.clicks = 5;
    h.alarms = 2;
    h.viewsShown = "Vue_Station;Popup_Pompe";
    const std::string fileKey(32, 'k');
    rec::protect(h, "fat2026", fileKey);
    rec::Header back;
    std::string why;
    check(rec::fromJson(rec::toJson(h), back, &why), "l'en-tete JSON se relit (" + why + ")");
    check(back.name == h.name && back.description == h.description && back.date == h.date, "nom, description, date relus tels quels");
    check(std::fabs(back.duration - 135.25) < 1e-9, "la duree relue (135,25 s)");
    check(back.clicks == 5 && back.alarms == 2 && back.viewsShown == h.viewsShown, "les comptes et les vues relus");
    check(back.locked() && rec::passwordOk(back, "fat2026") && !rec::passwordOk(back, "faux"), "le mot de passe : juste accepte, faux refuse");
    check(rec::toJson(h).find("fat2026") == std::string::npos, "le mot de passe n'est pas dans l'en-tete");
    check(rec::validName("Essai nuit") && !rec::validName("a/b") && !rec::validName("") && !rec::validName("x."), "les noms valides et refuses");
    check(rec::cleanName("a:b*c?") == "a_b_c_", "un nom nettoye");
    // 1.14.0 (relecture M4b, point 14) : les noms reserves de Windows ne sont jamais des noms d'enregistrement.
    check(!rec::validName("CON") && !rec::validName("nul.essai") && !rec::validName("Com3") && !rec::validName("LPT9 ") && !rec::validName("..")
              && !rec::validName("..\\Projet") && rec::validName("Console") && rec::validName("COM10"),
          "CON, NUL.essai, COM3, LPT9, .. refuses ; Console, COM10 acceptes");
    check(rec::cleanName("CON") == "CON_" && rec::cleanName("lpt1.essai") == "lpt1_.essai" && rec::validName(rec::cleanName("aux")),
          "un nom reserve nettoye prend un _");
    // La cle du fichier : chiffree par le mot de passe dans l'en-tete, rendue par le bon seulement.
    check(rec::trackKey(back, "fat2026") == fileKey && rec::trackKey(back, "faux").empty() && back.fileKey.find("6b6b6b") == std::string::npos,
          "la cle du fichier : rendue par le bon mot de passe, pas en clair dans l'en-tete");
    check(rec::clock(65) == "01:05" && rec::clock(3725) == "1:02:05", "mm:ss et h:mm:ss");
    check(rec::parseDuration("10m") == 600.0 && rec::parseDuration("1h30m") == 5400.0 && rec::parseDuration("90s") == 90.0
              && rec::parseDuration("00:10:00") == 600.0 && rec::parseDuration("600") == 600.0 && !rec::parseDuration("10x"),
          "les durees de la ligne de commande");
    check(rec::folderOf(RecordingSettings{}, "/p").find("ihm/ressources/enregistrements") != std::string::npos, "le dossier natif");
}

// ============================================================================ la piste seule ==
void testTrack() {
    std::printf("piste : ecrire, relire, images completes, changements\n");
    const std::string folder = freshFolder("piste");
    rec::Recorder r;
    rec::RecorderOptions o;
    o.partPath = folder + "/Essai.xrec.part";
    o.header.name = "Essai";
    std::string why;
    check(r.open(o, &why), "le fichier en cours s'ouvre (" + why + ")");
    // 12 s a 10 prises par seconde : Niveau change a chaque prise, Mode une fois, Pompe jamais.
    for (int i = 0; i <= 120; ++i) {
        const double t = i / 10.0;
        r.beginSample(t);
        r.value("NIVEAU", sim::Value::real(10.0 + i * 0.5));
        r.value("MODE", sim::Value::text(i < 60 ? "Auto" : "Manu"));
        r.value("POMPE", sim::Value::boolean(false));
        r.endSample();
        (void)r.flush(t);
    }
    r.event(rec::Event{3.0, rec::EventKind::Click, "Vue_Station.Btn_Pompe", "Vue_Station", 120, 140});
    rec::Header written;
    check(r.close(12.0, true, folder + "/Essai.xrec", "Essai", "", "", &why, &written), "le .xrec s'ecrit (" + why + ")");
    check(!fs::exists(fs::path(folder) / "Essai.xrec.part"), "le .part est retire");
    rec::Header h;
    rec::Track tr;
    check(rec::readTrack(folder + "/Essai.xrec", "", h, tr, &why) == rec::Status::Ok, "la piste se relit (" + why + ")");
    check(tr.closed && !tr.truncated, "la fin propre est lue");
    check(tr.keys.size() == 3, "3 cles");
    std::size_t keyframes = 0;
    for (const auto& f : tr.frames) keyframes += f.keyframe ? 1 : 0;
    check(keyframes == 3, "une image complete toutes les 5 s (0, 5, 10) : " + std::to_string(keyframes));
    std::size_t pompe = 0;
    for (const auto& f : tr.frames)
        if (!f.keyframe)
            for (const auto& c : f.changes) pompe += tr.keys[c.key].key == "POMPE" ? 1 : 0;
    check(pompe == 0, "une valeur qui ne change pas n'est ecrite que dans les images completes");
    check(std::fabs(tr.end - 12.0) < 1e-6, "la duree : 12 s");
    check(h.frames == 121 && h.keys == 3 && h.clicks == 1, "les comptes de l'en-tete");
    // Aller a un instant = lire en continu jusqu'a lui.
    auto shared = std::make_shared<const rec::Track>(tr);
    rec::Player cont(shared), jump(shared);
    bool same = true;
    for (int i = 0; i <= 120; ++i) {
        const double t = i / 10.0;
        cont.advanceTo(t);
        jump.seek(t);
        for (std::uint32_t k = 0; k < tr.keys.size(); ++k)
            if (valueText(cont.value(k)) != valueText(jump.value(k))) same = false;
        if (valueText(jump.valueOf("NIVEAU")) != plainOf(sim::Value::real(10.0 + i * 0.5))) same = false;
    }
    check(same, "aller a chaque instant = la lecture continue (et la valeur ecrite)");
    jump.seek(7.0);
    check(valueText(jump.valueOf("MODE")) == "Manu", "apres 6 s, Mode vaut Manu");
    jump.seek(2.0);
    check(valueText(jump.valueOf("MODE")) == "Auto", "en revenant a 2 s, Auto");
    const auto marks = jump.markers();
    check(marks.size() == 1 && marks[0].kind == 'c' && std::fabs(marks[0].t - 3.0) < 1e-6, "le repere du clic a 3 s");
}

// ============================================================================ le film ==
struct Shot {
    double                   t{0};
    std::map<std::string, std::string> values;
    std::string              view;
    std::vector<std::string> popups;
    std::string              alarms;
};
std::string alarmsText(const std::vector<LiveAlarm>& l) {
    std::string s;
    for (const auto& a : l) s += a.name + (a.active ? "+" : "-") + (a.acked ? "A" : "") + ";";
    return s;
}
std::vector<std::string> popupNames(Runtime& rt, const Project& p) {
    std::vector<std::string> out;
    for (const Id id : rt.popups())
        if (const View* v = p.view(id)) out.push_back(v->name);
    return out;
}
std::string viewName(Runtime& rt, const Project& p) {
    const View* v = p.view(rt.currentView());
    return v ? v->name : std::string{};
}

void testFilm() {
    std::printf("le film : enregistrer une IHM qui tourne, la rejouer, comparer a chaque instant\n");
    Station st("film");
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    check(rt.startRecording(RecordingStart{"Film", "le film de l'essai", "", 0, false, "essai"}, now, &why), "l'enregistrement demarre (" + why + ")");
    check(rt.recording(), "SYS.Recording : il tourne");
    std::vector<Shot> film;
    const std::vector<std::string> names = {"Niveau", "Pompe", "Compteur", "Mode", "SYS.PopupCount", "SYS.CurrentView"};
    for (int step = 1; step <= 80; ++step) {
        now = step / 10.0;
        if (step == 15) {                                   // un clic sur le bouton : la pompe bascule
            rt.pointerMoved(st.view, 150, 120, st.button, true);
            rt.press(st.button, now);
            rt.release(st.button, now, true);
        }
        if (step == 30) (void)rt.openPopup(st.popup, Transition{}, now, "Titre := 'Pompe 1'", "centre");
        if (step == 45) (void)rt.acknowledge("*", now);
        if (step == 60) (void)rt.closePopup(Transition{}, now);
        rt.tick(now);
        Shot s;
        s.t = now;
        for (const auto& n : names) s.values[n] = readText(rt, n);
        s.view = viewName(rt, st.p);
        s.popups = popupNames(rt, st.p);
        s.alarms = alarmsText(rt.alarms());
        film.push_back(std::move(s));
    }
    const std::string kept = rt.stopRecording(true, now);
    check(kept == "Film", "garde sous son nom : " + kept);
    check(!rt.recording(), "il ne tourne plus");
    check(journalHas(rt, "gard\xC3\xA9 : Film"), "le journal dit qu'il est garde");
    // L'en-tete
    rec::Header h;
    check(rec::readHeader(rec::pathOf(rt.recordingsFolder(), "Film"), h, &why) == rec::Status::Ok, "l'en-tete se lit (" + why + ")");
    check(std::fabs(h.duration - 8.0) < 0.05, "la duree : 8 s (" + rec::clock(h.duration) + ")");
    check(h.startView == "Vue_Station" && h.where == "Simulation" && h.project == "Station_Relevage" && h.projectVersion == "V1",
          "vue de depart, ou, projet et version");
    check(h.clicks == 1 && h.alarms == 1, "1 clic, 1 alarme : " + std::to_string(h.clicks) + ", " + std::to_string(h.alarms));
    check(h.description == "le film de l'essai", "la description");
    check(h.viewsShown.find("Popup_Pompe") != std::string::npos, "les vues montrees : " + h.viewsShown);
    // L'etat d'avant la relecture.
    const std::string liveNiveau = readText(rt, "Niveau");
    const std::string liveView = viewName(rt, st.p);
    const auto livePopups = popupNames(rt, st.p);
    const std::string liveAlarms = alarmsText(rt.alarms());
    // La relecture : chaque instant du film.
    check(rt.startReplay(ReplayRequest{"Film", {}, 1.0, 0, {}, "essai"}, now, &why) == rec::Status::Ok, "la relecture demarre (" + why + ")");
    check(rt.replaying(), "SYS.Replay : une relecture est affichee");
    bool valuesOk = true, screenOk = true, alarmsOk = true;
    std::string first;
    for (const auto& s : film) {
        (void)rt.replaySeek(s.t);
        for (const auto& [n, v] : s.values) {
            if (n == "SYS.CurrentView" || n == "SYS.PopupCount") continue;
            const std::string got = readText(rt, n);
            if (got != v) {
                valuesOk = false;
                if (first.empty()) first = rec::clock(s.t) + " " + n + " : " + got + " au lieu de " + v;
            }
        }
        if (viewName(rt, st.p) != s.view || popupNames(rt, st.p) != s.popups) {
            screenOk = false;
            if (first.empty()) first = rec::clock(s.t) + " ecran";
        }
        if (alarmsText(rt.alarms()) != s.alarms) {
            alarmsOk = false;
            if (first.empty()) first = std::to_string(s.t) + " alarmes " + alarmsText(rt.alarms()) + " au lieu de " + s.alarms;
        }
    }
    check(valuesOk, "chaque valeur, a chaque instant, egale a ce qui etait montre " + first);
    check(screenOk, "la vue et les popups suivent la piste " + first);
    check(alarmsOk, "les alarmes comme elles etaient " + first);
    // Le moteur est mis de cote : un tick ne fait rien tourner.
    const std::string before = readText(rt, "Compteur");
    rt.tick(now + 5);
    check(readText(rt, "Compteur") == before, "pendant la relecture, les scripts ne tournent pas");
    rt.press(st.button, now + 5);
    rt.release(st.button, now + 5, true);
    check(rt.replaying() && rt.variable("Pompe") && rt.variable("Pompe")->display() == film.back().values["Pompe"],
          "pendant la relecture, un clic ne fait rien");
    // Les reperes : l'alarme, le clic, la vue (la popup n'est pas une vue).
    const auto marks = rt.replayPlayer()->markers();
    const bool hasAlarm = std::any_of(marks.begin(), marks.end(), [](const auto& m) { return m.kind == 'a'; });
    const bool hasClick = std::any_of(marks.begin(), marks.end(), [](const auto& m) { return m.kind == 'c' && m.text == "Vue_Station.Btn_Pompe"; });
    check(hasAlarm && hasClick, "les reperes : une alarme, un clic");
    // Le pointeur et l'onde du clic.
    rt.replaySeek(1.55);
    const auto clicks = rt.replayRecentClicks(0.6);
    check(clicks.size() == 1 && std::fabs(clicks[0].x - 150) < 1e-6 && std::fabs(clicks[0].y - 120) < 1e-6, "l'onde du clic, a sa place");
    const auto ptr = rt.replayPointer();
    check(ptr && std::fabs(ptr->x - 150) < 1e-6, "le pointeur enregistre");
    // Quitter : l'etat d'avant, exactement.
    check(rt.quitReplay(now, "essai"), "quitter la relecture");
    check(!rt.replaying(), "plus de relecture");
    check(readText(rt, "Niveau") == liveNiveau, "la valeur d'avant (Niveau " + liveNiveau + ")");
    check(viewName(rt, st.p) == liveView && popupNames(rt, st.p) == livePopups, "la vue et les popups d'avant");
    check(alarmsText(rt.alarms()) == liveAlarms, "les alarmes d'avant");
    rt.tick(now + 0.1);
    check(readText(rt, "Compteur") != before, "l'IHM reprend en direct (les scripts tournent)");
}

// ======================================================================= les commandes de la relecture ==
void testReplayControls() {
    std::printf("relecture : -10 s, +10 s, vitesses, pause, fin\n");
    Station st("controles");
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    (void)rt.startRecording(RecordingStart{"Long", {}, {}, 0, false, "essai"}, now, &why);
    for (int step = 1; step <= 400; ++step) {
        now = step / 10.0;
        rt.tick(now);
    }
    (void)rt.stopRecording(true, now);
    check(rt.startReplay(ReplayRequest{"Long", {}, 1.0, 0, {}, "essai"}, now, &why) == rec::Status::Ok, "relecture de 40 s");
    sim::Value v;
    const auto pos = [&] { return rt.replayInfo().position; };
    double wall = 100.0;
    rt.replayTick(wall);
    rt.replayTick(wall += 2.0);
    check(std::fabs(pos() - 2.0) < 1e-6, "x1 : 2 s de mur, 2 s de film");
    check(rt.replaySpeed(2.0) && rt.replayInfo().speed == 2.0, "x2");
    rt.replayTick(wall += 2.0);
    check(std::fabs(pos() - 6.0) < 1e-6, "x2 : 2 s de mur, 4 s de film");
    check(rt.replaySpeed(3.0) && rt.replayInfo().speed == 4.0, "3 : la vitesse la plus proche (x4)");
    check(rt.replaySpeedStep(+1) && rt.replayInfo().speed == 8.0 && rt.replaySpeedStep(+1) && rt.replayInfo().speed == 8.0, "+ : x8, et pas plus");
    check(rt.replaySpeedStep(-1) && rt.replaySpeedStep(-1) && rt.replaySpeedStep(-1) && rt.replaySpeedStep(-1) && rt.replaySpeedStep(-1)
              && rt.replayInfo().speed == 0.25,
          "- : jusqu'a x0,25");
    rt.replaySpeed(1.0);
    check(rt.replayStep(10.0) && std::fabs(pos() - 16.0) < 1e-6, "+10 s");
    check(rt.replayStep(-10.0) && std::fabs(pos() - 6.0) < 1e-6, "-10 s");
    check(rt.replayStep(-10.0) && std::fabs(pos()) < 1e-6, "-10 s au debut : 0");
    // La valeur suit la position : le compteur a 6 s = celui ecrit a 6 s.
    rt.replaySeek(6.0);
    const std::string at6 = readText(rt, "Compteur");
    rt.replaySeek(30.0);
    rt.replaySeek(6.0);
    check(readText(rt, "Compteur") == at6, "revenir a 6 s rend la meme valeur (" + at6 + ")");
    check(rt.replayPause(true) && rt.replayInfo().paused, "pause");
    rt.replayTick(wall += 5.0);
    check(std::fabs(pos() - 6.0) < 1e-6, "en pause, la position ne bouge pas");
    check(rt.environment().read("SYS.ReplayPaused", v) && v.isTruthy(), "SYS.ReplayPaused");
    check(rt.replayPause() && !rt.replayInfo().paused, "reprendre (bascule)");
    rt.replaySeek(39.5);
    rt.replayTick(wall += 0.1);
    rt.replayTick(wall += 2.0);
    check(rt.replayInfo().ended && rt.replayInfo().paused && std::fabs(pos() - rt.replayInfo().length) < 1e-6, "au bout : en pause a la fin");
    check(rt.replayPause(false) && pos() < 1e-6, "Lecture au bout : repart du debut");
    check(rt.environment().read("SYS.ReplayLength", v) && std::llabs(v.asInteger() - 40000) <= 50, "SYS.ReplayLength : 40 s");
    check(rt.environment().read("SYS.ReplaySpeed", v) && std::fabs(v.asReal() - 1.0) < 1e-9, "SYS.ReplaySpeed : 1");
    check(rt.quitReplay(now) && !rt.replaying(), "quitter");
    check(rt.environment().read("SYS.Replay", v) && !v.isTruthy(), "SYS.Replay : faux apres");
}

// ======================================================================= le fichier : mot de passe ==
void testPassword() {
    std::printf("fichier : mot de passe juste, faux, absent\n");
    Station st("motdepasse", true);
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    check(rt.startRecording(RecordingStart{"FAT", {}, "fat2026", 0, false, "essai"}, now, &why), "un enregistrement protege");
    const std::string folder = rt.recordingsFolder();
    // Pendant l'enregistrement, le fichier en cours est deja chiffre : pas une valeur en clair.
    for (int step = 1; step <= 30; ++step) rt.tick(now = step / 10.0);
    (void)rt.stopRecording(true, now);
    const std::string path = rec::pathOf(folder, "FAT");
    rec::Header h;
    rec::Track tr;
    check(rec::readTrack(path, "", h, tr, &why) == rec::Status::PasswordNeeded, "sans mot de passe : refuse (" + why + ")");
    check(rec::readTrack(path, "faux", h, tr, &why) == rec::Status::BadPassword, "mot de passe faux : refuse");
    check(rec::readTrack(path, "fat2026", h, tr, &why) == rec::Status::Ok && !tr.keys.empty() && !tr.frames.empty(), "mot de passe juste : relu");
    check(rt.startReplay(ReplayRequest{"FAT", {}, 1.0, 0, {}, "essai"}, now, &why) == rec::Status::PasswordNeeded, "rejouer sans : le mot de passe est demande");
    check(rt.startReplay(ReplayRequest{"FAT", {}, 1.0, 0, "x", "essai"}, now, &why) == rec::Status::BadPassword, "rejouer avec un faux : refuse");
    check(rt.startReplay(ReplayRequest{"FAT", {}, 1.0, 0, "fat2026", "essai"}, now, &why) == rec::Status::Ok, "rejouer avec le bon");
    (void)rt.quitReplay(now);
    // Ce que ca protege : la piste ne se lit pas sans la cle (l'en-tete sans son empreinte : rien de lisible).
    {
        const std::string data = fileBytes(path);
        check(data.find("NIVEAU") == std::string::npos, "le nom des variables n'est pas en clair dans le fichier");
        rec::Header open;
        check(rec::readHeader(path, open) == rec::Status::Ok && open.locked() && open.name == "FAT", "l'en-tete, lui, reste lisible (nom, date...)");
    }
    // Changer, puis retirer le mot de passe.
    check(rec::setPassword(path, "faux", "neuf", &why) == rec::Status::BadPassword, "changer avec un faux actuel : refuse");
    check(rec::setPassword(path, "fat2026", "neuf", &why) == rec::Status::Ok, "changer le mot de passe (" + why + ")");
    check(rec::readTrack(path, "fat2026", h, tr) == rec::Status::BadPassword && rec::readTrack(path, "neuf", h, tr) == rec::Status::Ok,
          "l'ancien ne va plus, le nouveau oui");
    check(rec::setPassword(path, "neuf", "", &why) == rec::Status::Ok && rec::readTrack(path, "", h, tr) == rec::Status::Ok && !h.locked(),
          "retirer la protection");
    // Un mot de passe donne a l'arret (la fenetre de l'Enregistreur) : la piste est chiffree a ce moment.
    (void)rt.startRecording(RecordingStart{"Tard", {}, {}, 0, false, "essai"}, now, &why);
    for (int step = 31; step <= 50; ++step) rt.tick(now = step / 10.0);
    const std::string name = "Tard", desc = "fin", pwd = "tard";
    check(rt.finishRecording(true, name, desc, pwd, now) == "Tard", "le mot de passe donne a l'arret");
    check(rec::readTrack(rec::pathOf(folder, "Tard"), "", h, tr) == rec::Status::PasswordNeeded
              && rec::readTrack(rec::pathOf(folder, "Tard"), "tard", h, tr) == rec::Status::Ok && h.description == "fin",
          "il protege la piste deja ecrite en clair");
}

// ======================================================================= le fichier coupe ==
void testTruncated() {
    std::printf("fichier : tronque, coupe net\n");
    Station st("tronque");
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    (void)rt.startRecording(RecordingStart{"Entier", {}, {}, 0, false, "essai"}, now, &why);
    for (int step = 1; step <= 100; ++step) rt.tick(now = step / 10.0);
    (void)rt.stopRecording(true, now);
    const std::string folder = rt.recordingsFolder();
    const std::string path = rec::pathOf(folder, "Entier");
    const std::string data = fileBytes(path);
    rec::Header h;
    rec::Track full;
    check(rec::readTrack(path, "", h, full) == rec::Status::Ok && !full.truncated, "entier : relu sans manque");
    // Coupe a 60 % de la piste (l'en-tete, au debut, reste entier) : relu jusqu'au dernier bloc entier.
    const std::string cut = folder + "/Coupe.xrec";
    writeBytes(cut, data.substr(0, data.size() - static_cast<std::size_t>(h.trackBytes) * 4 / 10 - 120));
    rec::Track part;
    check(rec::readTrack(cut, "", h, part, &why) == rec::Status::Ok, "un fichier tronque se lit (" + why + ")");
    check(part.truncated && h.cut, "il est dit coupe");
    check(part.end > 1.0 && part.end < full.end, "relu jusqu'au dernier bloc : " + rec::clock(part.end) + " sur " + rec::clock(full.end));
    check(!part.frames.empty() && part.frames.size() < full.frames.size(), "moins d'images, mais des images");
    {
        // Les valeurs relues sont celles du fichier entier, au meme instant.
        auto a = std::make_shared<const rec::Track>(part);
        auto b = std::make_shared<const rec::Track>(full);
        rec::Player pa(a), pb(b);
        pa.seek(part.end * 0.9);
        pb.seek(part.end * 0.9);
        check(valueText(pa.valueOf("COMPTEUR")) == valueText(pb.valueOf("COMPTEUR")), "les valeurs relues sont justes");
    }
    // Coupe net pendant l'enregistrement : le .part reste (le moteur detruit sans arret).
    {
        auto crash = std::make_unique<Runtime>();
        st.hooks(*crash);
        crash->bind(&st.p, nullptr);
        crash->start(0.0);
        double t = 0;
        (void)crash->startRecording(RecordingStart{"Panne", {}, {}, 0, false, "essai"}, t, &why);
        for (int step = 1; step <= 35; ++step) crash->tick(t = step / 10.0);
        const std::string partPath = rec::pathOf(folder, "Panne") + ".part";
        check(fs::exists(fs::path(std::u8string(partPath.begin(), partPath.end()))), "le fichier en cours est sur le disque");
        // Une panne : le fichier en cours reste tel qu'il est (une copie, pendant que l'enregistrement tourne).
        const std::string copy = folder + "/Panne2.xrec.part";
        writeBytes(copy, fileBytes(partPath));
        (void)crash->stopRecording(false, t);
        std::string target;
        check(rec::recoverPart(copy, &target, &why), "un .part abandonne devient un .xrec (" + why + ")");
        rec::Header ph;
        rec::Track pt;
        check(rec::readTrack(target, "", ph, pt, &why) == rec::Status::Ok && ph.cut, "relu, et dit coupe");
        check(pt.end >= 2.0 && pt.end <= 3.6, "jusqu'au dernier bloc ecrit (" + rec::clock(pt.end) + ")");
    }
}

// ======================================================================= importer, exporter... ==
void testFiles() {
    std::printf("fichiers : importer, exporter, renommer, supprimer, description, taille du dossier\n");
    Station st("fichiers");
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    (void)rt.startRecording(RecordingStart{"Source", {}, {}, 0, false, "essai"}, now, &why);
    for (int step = 1; step <= 20; ++step) rt.tick(now = step / 10.0);
    (void)rt.stopRecording(true, now);
    const std::string folder = rt.recordingsFolder();
    const std::string out = freshFolder("fichiers-dehors");
    std::string written;
    check(rec::exportFile(folder, "Source", out, &written, &why) && fs::exists(fs::path(out) / "Source.xrec"), "exporter vers un dossier");
    std::string name;
    check(rec::importFile(written, folder, name, &why) && name == "Source (2)", "importer : un nom libre (" + name + ")");
    check(rec::renameFile(folder, "Source (2)", "Copie", &why), "renommer");
    rec::Header h;
    check(rec::readHeader(rec::pathOf(folder, "Copie"), h) == rec::Status::Ok && h.name == "Copie", "l'en-tete suit le nom");
    check(!rec::renameFile(folder, "Copie", "Source", &why), "renommer sur un nom pris : refuse (" + why + ")");
    check(rec::setDescription(rec::pathOf(folder, "Copie"), "ma description", &why)
              && rec::readHeader(rec::pathOf(folder, "Copie"), h) == rec::Status::Ok && h.description == "ma description",
          "la description modifiable");
    const std::string trash = st.folder + "/ihm/corbeille";
    check(rec::removeFile(folder, "Copie", trash, &why) && fs::exists(fs::path(trash) / "Copie.xrec") && !fs::exists(fs::path(folder) / "Copie.xrec"),
          "supprimer : vers la corbeille");
    const auto items = rt.recordings(true);
    check(items.size() == 1 && items[0].name == "Source", "la liste du dossier");
    // La taille du dossier : les plus anciens partent.
    for (int i = 0; i < 3; ++i) {
        (void)rt.startRecording(RecordingStart{"Suite" + std::to_string(i), {}, {}, 0, false, "essai"}, now, &why);
        for (int step = 1; step <= 10; ++step) rt.tick(now += 0.1);
        (void)rt.stopRecording(true, now);
    }
    const auto bytes = rec::folderBytes(folder);
    const auto removed = rec::prune(folder, bytes - 1, "Suite2");
    check(removed.size() == 1 && removed[0] != "Suite2", "trop gros : le plus ancien part (" + (removed.empty() ? std::string("rien") : removed[0]) + ")");
}

// ======================================================================= le projet a change ==
void testProjectChanged() {
    std::printf("projet change depuis : une variable supprimee, ignoree et signalee\n");
    Station st("change");
    {
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        std::string why;
        (void)rt.startRecording(RecordingStart{"Avant", {}, {}, 0, false, "essai"}, now, &why);
        for (int step = 1; step <= 20; ++step) rt.tick(now = step / 10.0);
        (void)rt.stopRecording(true, now);
    }
    // Mode disparait du projet ; la popup aussi.
    auto& vars = st.p.programs.variables;
    vars.erase(std::remove_if(vars.begin(), vars.end(), [](const Variable& v) { return v.name == "Mode"; }), vars.end());
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    std::string why;
    check(rt.startReplay(ReplayRequest{"Avant", {}, 1.0, 0, {}, "essai"}, 1.0, &why) == rec::Status::Ok, "il se rejoue quand meme");
    const auto info = rt.replayInfo();
    check(info.note.find("1 variable") != std::string::npos && info.note.find("n'existe plus") != std::string::npos, "le bandeau le dit : " + info.note);
    std::size_t said = 0;
    for (const auto& e : rt.journal()) said += e.message.find("variables absentes du projet") != std::string::npos ? 1 : 0;
    check(said == 1, "la Console le dit une fois");
    rt.replaySeek(1.0);
    check(readText(rt, "Niveau") != "(rien)", "les autres valeurs se rejouent");
    (void)rt.quitReplay(2.0);
}

// ======================================================================= natives, actions, SYS. ==
void testNatives() {
    std::printf("natives IHM_ENREG_*, IHM_REJOUER, IHM_RELECTURE_*\n");
    Station st("natives");
    Script call;
    call.id = st.p.allocate();
    call.event = "Appel";
    const auto script = [&](const std::string& name, const std::string& body) {
        Script s = call;
        s.id = st.p.allocate();
        s.name = name;
        s.body = body;
        st.p.programs.scripts.push_back(s);
    };
    st.p.programs.variables.push_back(variable(st.p, "R_Bool", "BOOL", "FALSE"));
    st.p.programs.variables.push_back(variable(st.p, "R_Texte", "STRING", "''"));
    st.p.programs.variables.push_back(variable(st.p, "R_Duree", "TIME", "T#0s"));
    st.p.programs.variables.push_back(variable(st.p, "R_Nombre", "DINT", "0"));
    script("Demarrer", "R_Bool := IHM_ENREG_DEMARRER('Nat ' + 'A', T#1m);");
    script("EnCours", "R_Bool := IHM_ENREG_EN_COURS();");
    script("Arreter", "R_Texte := IHM_ENREG_ARRETER();");
    script("Jeter", "R_Texte := IHM_ENREG_ARRETER(FALSE);");
    script("Existe", "R_Bool := IHM_ENREG_EXISTE('Nat A');");
    script("Duree", "R_Duree := IHM_ENREG_DUREE('Nat A');");
    script("Liste", "VAR L : LIST OF STRING; END_VAR\nL := IHM_ENREG_LISTE();\nR_Nombre := LIST_COUNT(L);\nR_Texte := L[0];");
    script("ListePrefixe", "VAR L : LIST OF STRING; END_VAR\nL := IHM_ENREG_LISTE('Zz');\nR_Nombre := LIST_COUNT(L);");
    // 1.14.0 (relecture M4b) : pendant la relecture, rien ne s'ecrit (R_Bool := ... non plus) - le resultat se lit par IF.
    script("Rejouer", "IF IHM_REJOUER('Nat A', 2.0) THEN\n  IHM_RELECTURE_ALLER(T#1s);\nEND_IF;");
    script("Exporter", "R_Bool := IHM_ENREG_EXPORTER('Nat A', '" + st.folder + "/dehors');");
    script("Importer", "R_Texte := IHM_ENREG_IMPORTER('" + st.folder + "/dehors/Nat A.xrec');");
    script("Supprimer", "R_Bool := IHM_ENREG_SUPPRIMER('Nat A (2)');");
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    const auto run = [&](const std::string& name) {
        check(rt.callScript(name, now, &why), "le script " + name + " tourne (" + why + ")");
    };
    const auto var = [&](const char* n) { return readText(rt, n); };
    run("Demarrer");
    check(var("R_Bool") == "TRUE" && rt.recording(), "IHM_ENREG_DEMARRER : TRUE, il tourne");
    sim::Value v;
    check(rt.environment().read("SYS.RecordingName", v) && v.asString() == "Nat A", "SYS.RecordingName");
    run("EnCours");
    check(var("R_Bool") == "TRUE", "IHM_ENREG_EN_COURS : TRUE");
    for (int step = 1; step <= 25; ++step) rt.tick(now = step / 10.0);
    check(rt.environment().read("SYS.RecordingTime", v) && std::llabs(v.asInteger() - 2500) <= 100, "SYS.RecordingTime : 2,5 s");
    run("Arreter");
    check(var("R_Texte") == "Nat A" && !rt.recording(), "IHM_ENREG_ARRETER : le nom garde");
    check(rt.environment().read("SYS.Recording", v) && !v.isTruthy(), "SYS.Recording : faux");
    run("Existe");
    check(var("R_Bool") == "TRUE", "IHM_ENREG_EXISTE");
    run("Duree");
    check(rt.environment().read("R_Duree", v) && std::llabs(v.asInteger() - 2500) <= 150, "IHM_ENREG_DUREE : 2,5 s (" + var("R_Duree") + ")");
    run("Liste");
    check(var("R_Nombre") == "1" && var("R_Texte") == "Nat A", "IHM_ENREG_LISTE : la liste des noms");
    run("ListePrefixe");
    check(var("R_Nombre") == "0", "IHM_ENREG_LISTE('Zz') : le prefixe filtre");
    run("Exporter");
    check(var("R_Bool") == "TRUE" && fs::exists(fs::path(st.folder) / "dehors" / "Nat A.xrec"), "IHM_ENREG_EXPORTER");
    run("Importer");
    check(var("R_Texte") == "Nat A (2)", "IHM_ENREG_IMPORTER : le nom pris (" + var("R_Texte") + ")");
    run("Supprimer");
    check(var("R_Bool") == "TRUE" && !fs::exists(fs::path(rt.recordingsFolder()) / "Nat A (2).xrec"), "IHM_ENREG_SUPPRIMER");
    (void)rt.startRecording(RecordingStart{"Jete", {}, {}, 0, false, "essai"}, now, &why);
    run("Jeter");
    check(var("R_Texte").empty() && !fs::exists(fs::path(rt.recordingsFolder()) / "Jete.xrec"), "IHM_ENREG_ARRETER(FALSE) : rien n'est garde");
    run("Rejouer");
    check(rt.replaying(), "IHM_REJOUER : la relecture");
    check(std::fabs(rt.replayInfo().speed - 2.0) < 1e-9 && std::fabs(rt.replayInfo().position - 1.0) < 1e-6,
          "IHM_REJOUER a x2, puis IHM_RELECTURE_ALLER(T#1s) dans le meme script");
    // Les autres commandes de relecture (comme dans la suite d'un meme script).
    sim::Value r;
    rt.environment().call("IHM_RELECTURE_VITESSE", {}, {{"", sim::Value::real(4.0)}}, r);
    check(r.isTruthy() && rt.replayInfo().speed == 4.0, "IHM_RELECTURE_VITESSE");
    rt.environment().call("IHM_RELECTURE_PAUSE", {}, {}, r);
    check(r.isTruthy() && rt.replayInfo().paused, "IHM_RELECTURE_PAUSE (bascule)");
    rt.environment().call("IHM_RELECTURE_PAUSE", {}, {{"", sim::Value::boolean(false)}}, r);
    check(!rt.replayInfo().paused, "IHM_RELECTURE_PAUSE(FALSE)");
    rt.environment().call("IHM_RELECTURE_QUITTER", {}, {}, r);
    check(r.isTruthy() && !rt.replaying(), "IHM_RELECTURE_QUITTER");
    // Le nom, un texte a trous : 'Trous {Compteur}' (comme le Nom de l'Enregistreur et de l'action).
    sim::Value c;
    (void)rt.environment().read("Compteur", c);
    rt.environment().call("IHM_ENREG_DEMARRER", {}, {{"", sim::Value::text("Trous {Compteur}")}}, r);
    check(r.isTruthy() && rt.recordingInfo().name == "Trous " + plainOf(c), "IHM_ENREG_DEMARRER : un nom a trous (" + rt.recordingInfo().name + ")");
    (void)rt.stopRecording(false, now);
}

void testActions() {
    std::printf("actions : Enregistrement, Relecture, Fichiers\n");
    Station st("actions");
    View& v = st.p.views.front();
    const auto button = [&](const char* name, std::vector<Action> actions) {
        const Id id = edit::add(st.p, v, Kind::Button, 10, 10);
        v.object(id)->name = name;
        v.object(id)->actions = std::move(actions);
        return id;
    };
    const Id start = button("Btn_Start", {act(Trigger::Click, Operation::RecordStart, "Act {Compteur}", {}, "duree := 1")});
    const Id restart = button("Btn_Restart", {act(Trigger::Click, Operation::RecordStart, "Autre", {}, "deja := redemarrer")});
    const Id stop = button("Btn_Stop", {act(Trigger::Click, Operation::RecordStop, {})});
    const Id toggle = button("Btn_Toggle", {act(Trigger::Click, Operation::RecordToggle, "Bascule")});
    const Id cancel = button("Btn_Cancel", {act(Trigger::Click, Operation::RecordCancel, {})});
    const Id play = button("Btn_Play", {act(Trigger::Click, Operation::ReplayStart, "Bascule", "2", "depuis := 1"),
                                       act(Trigger::Click, Operation::ReplaySeek, {}, "+0.5"),
                                       act(Trigger::Click, Operation::ReplaySpeed, {}, "4"),
                                       act(Trigger::Click, Operation::ReplayPause, {})});
    const Id quit = button("Btn_Quit", {act(Trigger::Click, Operation::ReplayQuit, {})});
    const Id exp = button("Btn_Export", {act(Trigger::Click, Operation::RecordingExport, "Bascule", st.folder + "/exports")});
    const Id del = button("Btn_Delete", {act(Trigger::Click, Operation::RecordingDelete, "Bascule")});
    Runtime rt;
    st.hooks(rt);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    const auto click = [&](Id id) {
        rt.press(id, now);
        rt.release(id, now, true);
    };
    for (int step = 1; step <= 5; ++step) rt.tick(now = step / 10.0);
    click(start);
    sim::Value val;
    check(rt.recording() && rt.recordingInfo().name.rfind("Act ", 0) == 0, "Demarrer : le nom, sa formule remplie (" + rt.recordingInfo().name + ")");
    check(std::fabs(rt.recordingInfo().maxSeconds - 60.0) < 1e-9, "Demarrer : duree := 1 (min)");
    const std::string first = rt.recordingInfo().name;
    click(start);
    check(rt.recordingInfo().name == first, "Demarrer, deja en cours : rien (par defaut)");
    click(restart);
    check(rt.recordingInfo().name == "Autre" && fs::exists(fs::path(rt.recordingsFolder()) / (first + ".xrec")), "deja := redemarrer : garde puis redemarre");
    for (int step = 6; step <= 15; ++step) rt.tick(now = step / 10.0);
    click(stop);
    check(!rt.recording() && fs::exists(fs::path(rt.recordingsFolder()) / "Autre.xrec"), "Arreter et garder");
    click(toggle);
    check(rt.recording() && rt.recordingInfo().name == "Bascule", "Demarrer ou arreter : demarre");
    for (int step = 16; step <= 45; ++step) rt.tick(now = step / 10.0);
    click(toggle);
    check(!rt.recording() && fs::exists(fs::path(rt.recordingsFolder()) / "Bascule.xrec"), "Demarrer ou arreter : arrete et garde");
    click(toggle);
    click(cancel);
    check(!rt.recording() && !fs::exists(fs::path(rt.recordingsFolder()) / "Bascule (2).xrec"), "Arreter sans garder");
    click(play);
    const auto info = rt.replayInfo();
    check(info.active && std::fabs(info.position - 1.5) < 1e-6 && info.speed == 4.0 && info.paused,
          "Rejouer (depuis 1 s), Avancer de 0,5 s, Vitesse x4, Pause : la suite des actions d'un clic");
    rt.press(quit, now);   // pendant la relecture, l'IHM ne recoit pas de clic
    rt.release(quit, now, true);
    check(rt.replaying(), "un clic pendant la relecture ne part pas");
    (void)rt.quitReplay(now);
    click(exp);
    check(fs::exists(fs::path(st.folder) / "exports" / "Bascule.xrec"), "Exporter un enregistrement");
    click(del);
    check(!fs::exists(fs::path(rt.recordingsFolder()) / "Bascule.xrec") && fs::exists(fs::path(st.folder) / "ihm" / "corbeille" / "Bascule.xrec"),
          "Supprimer un enregistrement (vers la corbeille)");
    (void)quit;
    // Le libelle et l'arbre des operations.
    check(std::string(operationLabel(Operation::ReplayStart)) == "Rejouer un enregistrement" && operationFromLabel("Rejouer un enregistrement") == Operation::ReplayStart,
          "le libelle enregistre d'une operation se relit");
    check(describeAction(act(Trigger::Click, Operation::ReplaySeek, {}, "-10")).find("de -10 s") != std::string::npos, "la description d'une action");
}

// ======================================================================= les objets en marche ==
void testObjects() {
    std::printf("objets : l'Enregistreur, le Lecteur d'enregistrements\n");
    Station st("objets");
    Runtime rt;
    std::vector<RecordingStopAsk> asked;
    std::vector<ReplayRequest> passwords;
    Runtime::Hooks h;
    const std::string f = st.folder;
    h.projectFolder = [f] { return f; };
    h.recordingStop = [&](const RecordingStopAsk& a) { asked.push_back(a); };
    h.replayPassword = [&](const ReplayRequest& r) { passwords.push_back(r); };
    rt.setHooks(h);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    for (int step = 1; step <= 5; ++step) rt.tick(now = step / 10.0);
    rt.objectPart(st.recorder, "basculer", now);
    check(rt.recording() && rt.recordingInfo().name == "Essai " + readText(rt, "Compteur"), "l'Enregistreur demarre, son Nom a formule rempli (" + rt.recordingInfo().name + ")");
    for (int step = 6; step <= 30; ++step) rt.tick(now = step / 10.0);
    rt.objectPart(st.recorder, "basculer", now);
    check(asked.size() == 1 && rt.recordingInfo().stopping && !rt.recording(), "a l'arret, la fenetre est demandee (nom, description, mot de passe)");
    check(asked.size() == 1 && std::fabs(asked[0].seconds - 2.5) < 0.05, "elle dit la duree (" + (asked.empty() ? std::string{} : rec::clock(asked[0].seconds)) + ")");
    const std::string kept = rt.finishRecording(true, "Par l'objet", "description", "secret", now + 3);
    check(kept == "Par l'objet", "Enregistrer : le nom donne");
    rec::Header hh;
    check(rec::readHeader(rec::pathOf(rt.recordingsFolder(), kept), hh) == rec::Status::Ok && std::fabs(hh.duration - 2.5) < 0.05 && hh.locked(),
          "la duree s'arrete au clic (pas a la reponse), et le mot de passe protege");
    // Sans la question : le nom de la propriete.
    st.p.views.front().object(st.recorder)->setFlag("askName", false);
    rt.objectPart(st.recorder, "basculer", now);
    for (int step = 31; step <= 40; ++step) rt.tick(now = step / 10.0);
    rt.objectPart(st.recorder, "basculer", now);
    check(!rt.recording() && asked.size() == 1, "Demander le nom a l'arret decochee : garde tout de suite");
    // Le Lecteur : ses lignes, Rejouer.
    const auto rows = rt.recordings(true);
    check(rows.size() == 2, "deux enregistrements dans le dossier");
    rt.objectPart(st.player, "ligne:1", now);
    check(rt.recordingPlayerRow(st.player) == 1, "une ligne choisie");
    rt.objectPart(st.player, "jouer:0", now);
    check(rt.replaying() || !passwords.empty(), "Rejouer depuis le Lecteur");
    if (rt.replaying()) (void)rt.quitReplay(now);
    // Protege : l'ecran demande le mot de passe.
    std::size_t locked = 0;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].header.locked()) locked = i;
    rt.objectPart(st.player, "jouer:" + std::to_string(locked), now);
    check(!passwords.empty() && passwords.back().name == "Par l'objet" && !rt.replaying(), "protege : le mot de passe est demande a l'ecran");
    // L'enregistrement fixe.
    st.p.views.front().object(st.player)->set("fixed", "Par l'objet");
    rt.objectPart(st.player, "jouer", now);
    check(passwords.size() == 2, "Enregistrement fixe : toujours le meme");
    // Pilotable decoche : rien.
    st.p.views.front().object(st.recorder)->setFlag("pilotable", false);
    rt.objectPart(st.recorder, "basculer", now);
    check(!rt.recording(), "Pilotable decochee : le bouton ne fait rien");
}

// ======================================================================= la ligne de commande ==
void testCommandLine() {
    std::printf("ligne de commande : les options, les codes, le refus\n");
    using K = rec::Command::Kind;
    auto c = rec::parseCommand({"--enregistrer", "Essai nuit", "--duree", "10m", "--mot-de-passe", "x"});
    check(c.kind == K::Record && c.name == "Essai nuit" && c.duration == 600 && c.password == "x" && c.error.empty(), "--enregistrer \"Nom\" --duree 10m --mot-de-passe x");
    c = rec::parseCommand({"--arreter-enregistrement", "--jeter"});
    check(c.kind == K::Stop && c.discard, "--arreter-enregistrement --jeter");
    c = rec::parseCommand({"--rejouer", "D\xC3\xA9marrage station", "--vitesse", "2"});
    check(c.kind == K::Replay && c.name == "D\xC3\xA9marrage station" && c.speed == 2.0, "--rejouer \"Nom\" --vitesse 2");
    c = rec::parseCommand({"--quitter-relecture"});
    check(c.kind == K::QuitReplay, "--quitter-relecture");
    c = rec::parseCommand({"--rejouer"});
    check(!c.error.empty(), "--rejouer sans nom : une erreur (" + c.error + ")");
    c = rec::parseCommand({"--enregistrer", "X", "--duree", "dix"});
    check(!c.error.empty(), "--duree illisible : une erreur");
    c = rec::parseCommand({"--vitesse", "2"});
    check(!c.error.empty(), "--vitesse seule : une erreur (elle accompagne --rejouer)");
    c = rec::parseCommand({"--jeter"});
    check(!c.error.empty(), "--jeter seul : une erreur");
    c = rec::parseCommand({"projet.xpgproj", "--poste", "C:/IHM"});
    check(c.kind == K::None && c.error.empty(), "les autres options ne sont pas des commandes d'enregistrement");
    check(rec::parseCommand(rec::commandArgs(rec::parseCommand({"--rejouer", "A", "--vitesse", "0,5", "--reponse", "r.txt"}))).speed == 0.5,
          "la commande passee a l'instance ouverte se relit (0,5)");
    int code = -1;
    std::string msg;
    check(rec::readReply(rec::replyText(4, "mot de passe incorrect"), code, msg) && code == 4 && msg == "mot de passe incorrect", "la reponse de l'instance");
    // Le refus : la case de la configuration.
    Station refused("cli-refus", false);
    Runtime rt;
    refused.hooks(rt);
    rt.bind(&refused.p, nullptr);
    std::string said;
    check(rt.runCommand(rec::parseCommand({"--enregistrer", "X"}), 0, &said) == rec::kExitNoHmi, "aucune IHM en marche : 2");
    rt.start(0.0);
    check(rt.runCommand(rec::parseCommand({"--enregistrer", "X"}), 0, &said) == rec::kExitRefused && !rt.recording(),
          "Accepter les commandes d'une autre application decochee : 5 (" + said + ")");
    Station ok("cli", true);
    Runtime rt2;
    ok.hooks(rt2);
    rt2.bind(&ok.p, nullptr);
    rt2.start(0.0);
    double now = 0;
    check(rt2.runCommand(rec::parseCommand({"--enregistrer", "Cli", "--duree", "1h"}), now, &said) == rec::kExitDone && rt2.recording()
              && std::fabs(rt2.recordingInfo().maxSeconds - 3600) < 1e-9,
          "--enregistrer : 0, il tourne, 1 h au plus");
    for (int step = 1; step <= 20; ++step) rt2.tick(now = step / 10.0);
    check(rt2.runCommand(rec::parseCommand({"--arreter-enregistrement"}), now, &said) == rec::kExitDone && !rt2.recording(), "--arreter-enregistrement : 0");
    check(rt2.runCommand(rec::parseCommand({"--arreter-enregistrement"}), now, &said) == rec::kExitNotFound, "rien a arreter : 3");
    check(rt2.runCommand(rec::parseCommand({"--rejouer", "Inconnu"}), now, &said) == rec::kExitNotFound, "nom introuvable : 3");
    check(rt2.runCommand(rec::parseCommand({"--rejouer", "Cli", "--vitesse", "2"}), now, &said) == rec::kExitDone && rt2.replaying(), "--rejouer : 0");
    check(rt2.runCommand(rec::parseCommand({"--quitter-relecture"}), now, &said) == rec::kExitDone && !rt2.replaying(), "--quitter-relecture : 0");
    (void)rt2.startRecording(RecordingStart{"Prot", {}, "pw", 0, false, "essai"}, now, &said);
    for (int step = 21; step <= 30; ++step) rt2.tick(now = step / 10.0);
    (void)rt2.stopRecording(true, now);
    check(rt2.runCommand(rec::parseCommand({"--rejouer", "Prot", "--mot-de-passe", "faux"}), now, &said) == rec::kExitBadPassword, "mot de passe faux : 4");
    check(rt2.runCommand(rec::parseCommand({"--rejouer", "Prot"}), now, &said) == rec::kExitBadPassword, "sans mot de passe : 4");
}

// ======================================================================= duree maximale, sons ==
void testLimits() {
    std::printf("limites : duree maximale ; sons et journal enregistres\n");
    Station st("limites");
    Resource snd;
    snd.id = st.p.allocate();
    snd.name = "Bip.wav";
    st.p.assets.resources.push_back(snd);
    st.p.views.front().objects.front().actions.push_back(act(Trigger::Click, Operation::PlaySound, "Bip.wav"));
    Runtime rt;
    std::vector<std::string> played;
    Runtime::Hooks h;
    const std::string f = st.folder;
    h.projectFolder = [f] { return f; };
    std::vector<std::string> channels;
    h.playSound = [&](const std::string& r) {
        played.push_back(r);
        channels.push_back(rt.soundChannel());
    };
    rt.setHooks(h);
    rt.bind(&st.p, nullptr);
    rt.start(0.0);
    double now = 0;
    std::string why;
    (void)rt.startRecording(RecordingStart{"Court", {}, {}, 2.0, false, "essai"}, now, &why);
    for (int step = 1; step <= 10; ++step) {
        now = step / 10.0;
        if (step == 5) {
            rt.press(st.button, now);
            rt.release(st.button, now, true);
        }
        rt.tick(now);
    }
    for (int step = 11; step <= 30; ++step) rt.tick(now = step / 10.0);
    check(!rt.recording() && journalHas(rt, "dur\xC3\xA9" "e maximale atteinte"), "la duree maximale l'arrete (et le journal le dit)");
    rec::Header hh;
    rec::Track tr;
    check(rec::readTrack(rec::pathOf(rt.recordingsFolder(), "Court"), "", hh, tr) == rec::Status::Ok && std::fabs(hh.duration - 2.0) < 0.15,
          "il est garde : 2 s (" + rec::clock(hh.duration) + ")");
    const bool sound = std::any_of(tr.events.begin(), tr.events.end(), [](const rec::Event& e) {
        return e.kind == rec::EventKind::Sound && e.a == "Bip.wav" && e.b == "Actions";
    });
    check(sound, "le son et son canal (Actions)");
    const bool journal = std::any_of(tr.events.begin(), tr.events.end(), [](const rec::Event& e) { return e.kind == rec::EventKind::Journal; });
    check(journal, "les lignes du journal");
    // Le son se rejoue (source relecture) en lecture normale.
    played.clear();
    channels.clear();
    (void)rt.startReplay(ReplayRequest{"Court", {}, 1.0, 0, {}, "essai"}, now, &why);
    double wall = 50;
    rt.replayTick(wall);
    for (int i = 0; i < 12; ++i) rt.replayTick(wall += 0.1);
    check(played.size() == 1 && played[0] == "Bip.wav", "le son se rejoue en passant dessus");
    check(channels.size() == played.size() && !channels.empty() && channels.back() == "Relecture" && rt.soundChannel().empty(),
          "le son rejoue part sur le canal Relecture (sa source : la relecture)");
    rt.replaySeek(0);
    rt.replaySeek(1.5);
    check(played.size() == 1, "pas en sautant");
    (void)rt.quitReplay(now);
}

// ---- Generer, et le projet enregistre (la configuration, les deux objets) ----------------------------------
void testGenerateAndStore() {
    std::printf("Generer et le projet enregistre\n");
    Station s("generer");
    // Un enregistrement present : ce qui le cite ne dit rien.
    {
        Runtime rt;
        s.hooks(rt);
        rt.bind(&s.p, nullptr);
        rt.start(0);
        RecordingStart r;
        r.name = "Present";
        check(rt.startRecording(r, 0.0), "un enregistrement pour Generer");
        for (int i = 1; i <= 10; ++i) rt.tick(i * 0.1);
        check(rt.stopRecording(true, 1.0) == "Present", "garde : Present");
    }
    View& v = s.p.views.front();
    Object& btn = *v.object(s.button);
    btn.actions.push_back(act(Trigger::Click, Operation::ReplayStart, "Absent_action"));
    btn.actions.push_back(act(Trigger::Click, Operation::ReplayStart, "Present"));
    btn.actions.push_back(act(Trigger::Click, Operation::RecordingExport, "Present"));   // sans dossier
    v.object(s.player)->set("fixed", "Absent_fixe");
    v.object(s.recorder)->set("maxMinutes", "-1");
    Script sc;
    sc.id = s.p.allocate();
    sc.name = "Rejeu";
    sc.event = "Demarrage";
    sc.body = "IHM_REJOUER('Absent_script');\nIHM_REJOUER('Present');\n";
    s.p.programs.scripts.push_back(sc);
    const auto issues = generate(s.p, [](std::string_view) { return true; }, s.folder);
    const auto has = [&](std::string_view part, Issue::Severity sev) {
        for (const auto& i : issues)
            if (i.message.find(part) != std::string::npos && i.severity == sev) return true;
        return false;
    };
    const auto count = [&](std::string_view part) {
        std::size_t n = 0;
        for (const auto& i : issues) n += i.message.find(part) != std::string::npos;
        return n;
    };
    check(has("Absent_action", Issue::Severity::Warning), "Generer : une action Rejouer d'un nom absent");
    check(has("Absent_fixe", Issue::Severity::Warning), "Generer : un Lecteur fixe absent");
    check(has("Absent_script", Issue::Severity::Warning), "Generer : un script qui rejoue un nom absent");
    check(count("\xC2\xAB Present \xC2\xBB") == 0, "Generer : rien sur l'enregistrement present");
    check(has("vers quel dossier", Issue::Severity::Error), "Generer : exporter sans dossier");
    check(has("n\xC3\xA9gative", Issue::Severity::Error), "Generer : une duree maximale negative");
    // ---- le projet enregistre : la configuration, gardee seulement si elle change
    const auto textOf = [](const Project& p) {
        std::string all;
        for (const auto& f : serializeProject(p))
            if (f.path == "ihm.txt" && f.data) all.assign(f.data->begin(), f.data->end());
        return all;
    };
    Project plain;
    check(textOf(plain).find("enregistrements") == std::string::npos, "un projet sans reglage : pas de ligne enregistrements (le format ne bouge pas)");
    Project p2 = s.p;
    p2.recording.folder = "D:/Enregistrements";
    p2.recording.maxMinutes = 12;
    p2.recording.maxFolderMb = 500;
    p2.recording.rateHz = 5;
    p2.recording.pointer = false;
    p2.recording.sounds = false;
    p2.recording.popups = false;
    p2.recording.external = true;
    const auto files = serializeProject(p2);
    std::map<std::string, std::string> byPath;
    for (const auto& f : files)
        if (f.data) byPath[f.path].assign(f.data->begin(), f.data->end());
    const auto back = parseProject([&](const std::string& path, std::string& content) {
        const auto it = byPath.find(path);
        if (it == byPath.end()) return false;
        content = it->second;
        return true;
    });
    check(back.has_value(), "le projet se relit");
    if (back) {
        const auto& r = back->recording;
        check(r.folder == "D:/Enregistrements" && r.maxMinutes == 12 && r.maxFolderMb == 500 && r.rateHz == 5 && !r.pointer && !r.sounds
                  && !r.popups && r.external,
              "Configuration > Enregistrements : tout revient");
        const View* bv = back->view(s.view);
        const Object* recBack = bv ? bv->object(s.recorder) : nullptr;
        const Object* playBack = bv ? bv->object(s.player) : nullptr;
        check(recBack && recBack->kind == Kind::Recorder && recBack->text("recName") == "Essai {Compteur}", "l'Enregistreur revient (son Nom)");
        check(playBack && playBack->kind == Kind::RecordingPlayer && playBack->text("fixed") == "Absent_fixe", "le Lecteur revient (Enregistrement fixe)");
    }
}

// ======================================================================= 1.14.0 : la relecture contradictoire (M4b) ==
//  Les defauts trouves par la relecture du lot : chacun a son essai (il echouait avant la correction).
class FakePlc final : public sim::Environment {
public:
    std::map<std::string, sim::Value> values;
    int writes{0};
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        it->second = v;
        ++writes;
        return true;
    }
    bool exists(std::string_view n) override { return values.count(std::string(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
    void report(sim::Diagnostic) override {}
};

std::string le16s(std::uint32_t v) { return std::string{static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)}; }
std::string le32s(std::uint32_t v) {
    std::string o;
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    return o;
}
// Un en-tete local de zip (sans CRC : le lecteur ne le verifie pas) suivi de ses donnees.
std::string zipLocal(const std::string& name, std::uint32_t method, std::uint32_t csize, std::uint32_t usize, const std::string& data) {
    return le32s(0x04034b50u) + le16s(20) + le16s(0) + le16s(method) + le16s(0) + le16s(0x21) + le32s(0) + le32s(csize) + le32s(usize)
         + le16s(static_cast<std::uint32_t>(name.size())) + le16s(0) + name + data;
}
std::string deflated(const std::string& raw) {
    std::size_t n = 0;
    void* p = tdefl_compress_mem_to_heap(raw.data(), raw.size(), &n, TDEFL_DEFAULT_MAX_PROBES);
    std::string out(static_cast<const char*>(p), n);
    mz_free(p);
    return out;
}
// Un bloc de piste (compresse, en clair).
std::string trackBlock(const std::string& raw, std::uint32_t rawLenClaim = 0) {
    const std::string stored = deflated(raw);
    const auto crc = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(stored.data()), stored.size()));
    return std::string("XRB1") + std::string(1, '\x01') + le32s(0) + le32s(0) + le32s(rawLenClaim ? rawLenClaim : static_cast<std::uint32_t>(raw.size()))
         + le32s(static_cast<std::uint32_t>(stored.size())) + le32s(crc) + stored;
}

void testRelectureM4b() {
    std::printf("relecture contradictoire (M4b) : les defauts corriges\n");
    namespace R = rec;
    // ---- 1. LE MOT DE PASSE NE SAUTE PAS A L'ARRET (la fenetre : Valider sans le retaper, Echap) ----------------------
    {
        Station st("m4b-motdepasse");
        Runtime rt;
        std::vector<RecordingStopAsk> asked;
        Runtime::Hooks h;
        const std::string f = st.folder;
        h.projectFolder = [f] { return f; };
        h.recordingStop = [&](const RecordingStopAsk& a) { asked.push_back(a); };
        rt.setHooks(h);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        std::string why;
        check(rt.startRecording(RecordingStart{"Secret", {}, "secret", 0, true, "essai"}, now, &why), "un enregistrement protege au depart");
        for (int step = 1; step <= 20; ++step) rt.tick(now = step / 10.0);
        rt.askStopRecording(now, "essai");
        check(asked.size() == 1 && asked[0].locked, "la fenetre dit qu'il est protege");
        // L'arret ne relit ni ne recopie rien : l'en-tete final s'ecrit a sa place.
        const auto before = R::fileStats();
        check(rt.finishRecording(true, "Secret", "", "", now) == "Secret", "Valider sans retaper le mot de passe");
        check(R::fileStats().bytes == before.bytes, "l'arret ne relit rien du fichier en cours (" + std::to_string(R::fileStats().bytes - before.bytes) + " octets)");
        R::Header hh;
        R::Track tr;
        const std::string folder = rt.recordingsFolder();
        check(R::readTrack(R::pathOf(folder, "Secret"), "", hh, tr) == R::Status::PasswordNeeded
                  && R::readTrack(R::pathOf(folder, "Secret"), "secret", hh, tr) == R::Status::Ok && !tr.frames.empty(),
              "un mot de passe vide garde celui du depart (avant : le .xrec etait en clair)");
        // Echap (la fenetre rend un mot de passe vide, comme une fenetre absente).
        (void)rt.startRecording(RecordingStart{"Echap", {}, "secret", 0, true, "essai"}, now, &why);
        for (int step = 21; step <= 30; ++step) rt.tick(now = step / 10.0);
        rt.askStopRecording(now, "essai");
        (void)rt.finishRecording(true, "Echap", {}, {}, now);
        check(R::readTrack(R::pathOf(folder, "Echap"), "", hh, tr) == R::Status::PasswordNeeded, "Echap : toujours protege");
        // Retirer la protection : seulement par la case.
        (void)rt.startRecording(RecordingStart{"Clair", {}, "secret", 0, true, "essai"}, now, &why);
        for (int step = 31; step <= 40; ++step) rt.tick(now = step / 10.0);
        rt.askStopRecording(now, "essai");
        (void)rt.finishRecording(true, "Clair", {}, {}, now, nullptr, true);
        check(R::readTrack(R::pathOf(folder, "Clair"), "", hh, tr) == R::Status::Ok && !hh.locked() && !tr.frames.empty(),
              "Retirer la protection (la case) : en clair, et la piste se relit");
        // Un autre mot de passe a l'arret : seule la cle de l'en-tete change.
        (void)rt.startRecording(RecordingStart{"Autre", {}, "secret", 0, true, "essai"}, now, &why);
        for (int step = 41; step <= 50; ++step) rt.tick(now = step / 10.0);
        rt.askStopRecording(now, "essai");
        (void)rt.finishRecording(true, "Autre", {}, "neuf", now);
        check(R::readTrack(R::pathOf(folder, "Autre"), "secret", hh, tr) == R::Status::BadPassword
                  && R::readTrack(R::pathOf(folder, "Autre"), "neuf", hh, tr) == R::Status::Ok && !tr.frames.empty(),
              "un autre mot de passe a l'arret : l'ancien ne va plus, le nouveau relit la piste");
    }
    // ---- 2. RIEN NE PART VERS L'EQUIPEMENT PENDANT LE FILM -----------------------------------------------------------
    {
        Station st("m4b-ecritures");
        FakePlc plc;
        plc.values["MOTEUR_API"] = sim::Value::boolean(false);
        View& v = st.p.views.front();
        const Id bad = edit::add(st.p, v, Kind::Button, 10, 10);
        v.object(bad)->name = "Btn_Film";
        v.object(bad)->actions = {act(Trigger::Click, Operation::ReplayStart, "Base"), act(Trigger::Click, Operation::Set, "Pompe"),
                                  act(Trigger::Click, Operation::Assign, "Compteur", "50"), act(Trigger::Click, Operation::Set, "MOTEUR_API"),
                                  act(Trigger::Click, Operation::ReplaySeek, {}, "1")};
        Script sc;
        sc.id = st.p.allocate();
        sc.name = "Film";
        sc.event = "Appel";
        sc.body = "IHM_REJOUER('Base');\nCompteur := 777;\nMOTEUR_API := TRUE;\nIHM_RELECTURE_ALLER(T#1500ms);\n";
        st.p.programs.scripts.push_back(sc);
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, &plc);
        rt.start(0.0);
        double now = 0;
        std::string why;
        (void)rt.startRecording(RecordingStart{"Base", {}, {}, 0, false, "essai"}, now, &why);
        for (int step = 1; step <= 30; ++step) rt.tick(now = step / 10.0);
        (void)rt.stopRecording(true, now);
        const std::string pompe = readText(rt, "Pompe"), compteur = readText(rt, "Compteur");
        rt.press(bad, now);
        rt.release(bad, now, true);
        check(rt.replaying(), "le clic lance la relecture");
        check(std::fabs(rt.replayInfo().position - 1.0) < 1e-6, "l'action Relecture qui suit passe (Avancer de 1 s)");
        check(journalHas(rt, "3 actions de la suite non faites"), "les autres actions de la suite ne partent pas, et la Console le dit");
        (void)rt.quitReplay(now);
        check(readText(rt, "Pompe") == pompe && readText(rt, "Compteur") == compteur && plc.writes == 0,
              "apres : Pompe, Compteur et l'automate tels qu'avant (avant : ecrits pendant le film)");
        check(rt.callScript("Film", now, &why), "un script qui rejoue puis ecrit (" + why + ")");
        check(rt.replaying() && std::fabs(rt.replayInfo().position - 1.5) < 1e-6, "IHM_RELECTURE_ALLER apres IHM_REJOUER passe");
        std::size_t said = 0;
        for (const auto& e : rt.journal()) said += e.message.find("criture refus") != std::string::npos ? 1 : 0;
        check(said == 1, "les ecritures refusees, dites une fois (" + std::to_string(said) + ")");
        std::string rw;
        (void)rt.quitReplay(now);
        check(readText(rt, "Compteur") == compteur && plc.writes == 0, "Compteur := 777 et MOTEUR_API := TRUE n'ont rien ecrit");
    }
    // ---- 3. L'HEURE DE L'IHM NE RETARDE PAS DU TEMPS TENU -------------------------------------------------------------
    {
        Station st("m4b-heure");
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        sim::Value start0, start1;
        (void)rt.environment().read("SYS.StartTime", start0);
        const double e0 = rt.epochOf(1.0);
        rt.holdClock(600.0);
        check(std::fabs(rt.epochOf(1.0) - e0 - 600.0) < 1e-3, "10 min de relecture : les dates suivent le mur (+600 s)");
        (void)rt.environment().read("SYS.StartTime", start1);
        check(plainOf(start0) == plainOf(start1), "SYS.StartTime ne bouge pas (" + plainOf(start1) + ")");
    }
    // ---- 4. UN .xrec HOSTILE NE FAIT PAS ALLOUER SANS FIN -------------------------------------------------------------
    {
        const std::string folder = freshFolder("m4b-hostile");
        // L'en-tete compresse annonce 4 Gio.
        const std::string z = deflated("{\"format\": 2, \"nom\": \"Bombe\"}");
        writeBytes(folder + "/Entete.xrec", zipLocal("entete.json", 8, static_cast<std::uint32_t>(z.size()), 0xFFFFFFFFu, z));
        R::Header h;
        std::string why;
        const auto before = R::fileStats();
        check(R::readHeader(folder + "/Entete.xrec", h, &why) == R::Status::Unreadable && why.find("trop grand") != std::string::npos,
              "un en-tete annonce de 4 Gio : refuse sans rien allouer (" + why + ")");
        check(R::fileStats().bytes - before.bytes < 4096, "... et presque rien n'est lu");
        // Une piste de 200 000 changements "00 00" (400 Ko decompresses, quelques Ko dans le fichier) : au-dela des limites.
        std::string raw;
        raw += std::string("\x01\x00\x00\x01K\x01K\x00\x00", 9);              // une cle
        raw += std::string("\x02\x00", 2);                                     // des valeurs a 0 s...
        std::uint32_t n = 200000;
        while (n >= 0x80) { raw.push_back(static_cast<char>((n & 0x7F) | 0x80)); n >>= 7; }
        raw.push_back(static_cast<char>(n));
        for (int i = 0; i < 200000; ++i) raw += std::string("\x00\x00", 2);    // ... 200 000 fois (cle 0, rien)
        const std::string head = "{\"format\": 2, \"nom\": \"Bombe\"}";
        const std::string track = trackBlock(raw);
        writeBytes(folder + "/Bombe.xrec", zipLocal("entete.json", 0, static_cast<std::uint32_t>(head.size()), static_cast<std::uint32_t>(head.size()), head)
                                               + zipLocal("piste.bin", 0, static_cast<std::uint32_t>(track.size()), static_cast<std::uint32_t>(track.size()), track));
        const R::Limits saved = R::limits();
        R::setLimits(R::Limits{saved.trackRaw, 50000});
        R::Track tr;
        check(R::readTrack(folder + "/Bombe.xrec", "", h, tr, &why) == R::Status::Unreadable && why.find("trop grande") != std::string::npos
                  && tr.frames.empty(),
              "une piste au-dela des limites : refusee et dite (" + why + ")");
        R::setLimits(saved);
        check(R::readTrack(folder + "/Bombe.xrec", "", h, tr, &why) == R::Status::Ok && tr.frames.size() == 1, "dans les limites, elle se lit");
        // Un bloc qui annonce 64 Mio pour quelques octets : faux, pas alloue.
        const std::string liar = trackBlock("\x09\x00", 64u << 20);
        writeBytes(folder + "/Menteur.xrec", zipLocal("entete.json", 0, static_cast<std::uint32_t>(head.size()), static_cast<std::uint32_t>(head.size()), head)
                                                 + zipLocal("piste.bin", 0, static_cast<std::uint32_t>(liar.size()), static_cast<std::uint32_t>(liar.size()), liar));
        check(R::readTrack(folder + "/Menteur.xrec", "", h, tr, &why) == R::Status::Ok && tr.truncated && tr.blocks == 0,
              "un bloc qui ment sur sa taille : la lecture s'y arrete (coupe), rien n'est alloue");
        // Une piste compressee par un autre outil qui annonce 2 Gio.
        writeBytes(folder + "/Zip.xrec", zipLocal("entete.json", 0, static_cast<std::uint32_t>(head.size()), static_cast<std::uint32_t>(head.size()), head)
                                             + zipLocal("piste.bin", 8, 16, 0x7FFFFFFFu, std::string(16, '\0')));
        check(R::readTrack(folder + "/Zip.xrec", "", h, tr, &why) == R::Status::Unreadable && why.find("trop grande") != std::string::npos,
              "une piste compressee annoncee de 2 Gio : refusee (" + why + ")");
    }
    // ---- 5. LA LISTE NE LIT QUE LES EN-TETES, UNE FOIS -----------------------------------------------------------------
    {
        Station st("m4b-liste");
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        std::string why;
        for (const char* name : {"Un", "Deux"}) {
            (void)rt.startRecording(RecordingStart{name, {}, {}, 0, false, "essai"}, now, &why);
            for (int step = 1; step <= 200; ++step) rt.tick(now += 0.1);
            (void)rt.stopRecording(true, now);
        }
        const std::string folder = rt.recordingsFolder();
        R::Header h;
        (void)R::readHeader(R::pathOf(folder, "Un"), h);
        // Un gros enregistrement (des valeurs qui ne se compressent guere) : plus de 500 Ko de piste.
        {
            R::Recorder big;
            R::RecorderOptions o;
            o.partPath = R::pathOf(folder, "Gros") + ".part";
            o.header.name = "Gros";
            (void)big.open(o);
            std::uint64_t x = 88172645463325252ull;
            for (int i = 0; i < 2000; ++i) {
                big.beginSample(i / 10.0);
                for (int k = 0; k < 60; ++k) {
                    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                    big.value("V" + std::to_string(k), sim::Value::real(static_cast<double>(x % 1000000007ull) / 7.0));
                }
                big.endSample();
                (void)big.flush(i / 10.0);
            }
            (void)big.close(200.0, true, R::pathOf(folder, "Gros"), "Gros", {}, {});
        }
        const std::string grosPath = R::pathOf(folder, "Gros");
        const auto grosBytes = fs::file_size(fs::path(std::u8string(grosPath.begin(), grosPath.end())));
        const auto a = R::fileStats();
        auto items = R::listFolder(folder);
        const auto b = R::fileStats();
        check(items.size() == 3 && grosBytes > 500000 && b.bytes - a.bytes < 3 * 20000,
              "la liste lit les en-tetes seulement (" + std::to_string(b.bytes - a.bytes) + " octets lus pour 3 fichiers, dont un de "
                  + std::to_string(grosBytes) + " octets ; avant : tout)");
        items = R::listFolder(folder);
        const auto c = R::fileStats();
        check(c.opens == b.opens && c.bytes == b.bytes, "relistee sans changement : rien n'est relu");
        check(R::setDescription(R::pathOf(folder, "Un"), "une description", &why), "la description se change (sur place)");
        const auto d = R::fileStats();
        items = R::listFolder(folder);
        const auto e = R::fileStats();
        check(e.opens - d.opens == 1, "apres un changement, seul cet en-tete est relu");
        bool seen = false;
        for (const auto& it : items) seen = seen || (it.name == "Un" && it.header.description == "une description");
        check(seen, "... et il dit la nouvelle description");
        check(!h.at40.empty(), "l'ecran a 40 % est dans l'en-tete (le detail ne lit pas la piste) : " + h.at40);
    }
    // ---- 6. LE FILM MONTRE TOUT : les parametres Copie des popups, les courbes ------------------------------------------
    {
        Station st("m4b-film");
        View& pop = st.p.views.back();
        pop.params.front().mode = ParamMode::Copy;
        const Id label = edit::add(st.p, pop, Kind::Text, 10, 10);
        pop.object(label)->setExpr("text", "Titre");
        View& v = st.p.views.front();
        const Id trend = edit::add(st.p, v, Kind::Trend, 600, 400);
        v.object(trend)->set("variables", "Niveau");
        v.object(trend)->set("duration", "10");
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        std::string why;
        (void)rt.startRecording(RecordingStart{"Copie", {}, {}, 0, false, "essai"}, now, &why);
        std::string shownLive;
        double niveauAt4 = 0;
        for (int step = 1; step <= 80; ++step) {
            now = step / 10.0;
            if (step == 10) (void)rt.openPopup(st.popup, Transition{}, now, "Titre := 'Pompe 7'", "centre");
            if (step == 70) (void)rt.closePopup(Transition{}, now);
            rt.tick(now);
            if (step == 40) {
                shownLive = rt.shownText(st.popup, *rt.project()->view(st.popup)->object(label), "text");
                sim::Value nv;
                (void)rt.environment().read("Niveau", nv);
                niveauAt4 = nv.asReal();
            }
        }
        (void)rt.stopRecording(true, now);
        const auto* live = rt.trend(st.view, trend);
        const std::size_t livePoints = live && !live->empty() ? live->front().points.size() : 0;
        const double liveLast = live && !live->empty() && !live->front().points.empty() ? live->front().points.back().second : -1;
        check(shownLive == "Pompe 7", "en direct, la popup montre son parametre Copie (Pompe 7)");
        check(rt.startReplay(ReplayRequest{"Copie", {}, 1.0, 4.0, {}, "essai"}, now, &why) == R::Status::Ok, "la relecture a 4 s");
        const std::string shownReplay = rt.shownText(st.popup, *rt.project()->view(st.popup)->object(label), "text");
        check(shownReplay == "Pompe 7", "rejouee, la popup montre son parametre Copie (avant : vide) : " + shownReplay);
        const auto* rp = rt.trend(st.view, trend);
        const bool trendOk = rp && !rp->empty() && !rp->front().points.empty() && std::fabs(rp->front().points.back().second - niveauAt4) < 1e-6
                          && rp->front().points.size() >= 30;
        check(trendOk, "la courbe rejouee : refaite depuis la piste jusqu'a 4 s (" + std::to_string(rp && !rp->empty() ? rp->front().points.size() : 0)
                           + " points, la derniere valeur " + (rp && !rp->empty() && !rp->front().points.empty() ? std::to_string(rp->front().points.back().second) : std::string("?"))
                           + " au lieu de " + std::to_string(niveauAt4) + ")");
        const double edge = rp && !rp->empty() && !rp->front().points.empty() ? rp->front().points.back().first : -1;
        check(std::fabs(edge - rt.now()) < 0.11, "le dernier point de la courbe est au bord droit (l'heure tenue)");
        const std::size_t at4 = rp && !rp->empty() ? rp->front().points.size() : 0;
        double wall = 10;
        rt.replayTick(wall);
        rt.replayTick(wall += 1.0);
        const auto* rp2 = rt.trend(st.view, trend);
        check(rp2 && !rp2->empty() && !rp2->front().points.empty() && std::fabs(rp2->front().points.back().first - rt.now()) < 0.11
                  && rp2->front().points.size() >= at4 + 9,
              "en lecture, la courbe avance (les points glissent, les nouveaux s'ajoutent)");
        (void)rt.quitReplay(now);
        const auto* back = rt.trend(st.view, trend);
        check(back && !back->empty() && back->front().points.size() == livePoints && std::fabs(back->front().points.back().second - liveLast) < 1e-9,
              "en quittant, la courbe du direct revient telle quelle");
    }
    // ---- 7. LES CODES DE SORTIE : 6 occupee, 7 fichier ------------------------------------------------------------------
    {
        Station st("m4b-codes", true);
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        std::string said;
        check(rt.runCommand(R::parseCommand({"--enregistrer", "Un"}), now, &said) == R::kExitDone, "--enregistrer : 0");
        check(rt.runCommand(R::parseCommand({"--enregistrer", "Deux"}), now, &said) == R::kExitBusy, "deja un enregistrement : 6 (" + said + ")");
        check(rt.runCommand(R::parseCommand({"--rejouer", "Un"}), now, &said) == R::kExitBusy, "rejouer pendant un enregistrement : 6");
        for (int step = 1; step <= 10; ++step) rt.tick(now = step / 10.0);
        (void)rt.runCommand(R::parseCommand({"--arreter-enregistrement"}), now, &said);
        writeBytes(rt.recordingsFolder() + "/Abime.xrec", "ceci n'est pas un zip");
        check(rt.runCommand(R::parseCommand({"--rejouer", "Abime"}), now, &said) == R::kExitFile, "un fichier illisible : 7 (" + said + ")");
        check(rt.runCommand(R::parseCommand({"--rejouer", "Un"}), now, &said) == R::kExitDone, "--rejouer : 0");
        check(rt.runCommand(R::parseCommand({"--enregistrer", "Trois"}), now, &said) == R::kExitBusy, "enregistrer pendant une relecture : 6");
        (void)rt.quitReplay(now);
    }
    // ---- 8. --reponse : un fichier neuf du dossier temporaire, rien d'autre --------------------------------------------
    {
        std::string why;
        const std::string good = R::newReplyPath();
        check(R::replyPathOk(good, &why), "le chemin de la reponse que fait la commande est accepte (" + why + ")");
        check(!R::replyPathOk(freshFolder("m4b-rep") + "/xpg-enregistrement-1.txt", &why), "ailleurs que le dossier temporaire : refuse (" + why + ")");
        check(!R::replyPathOk(utf8(fs::temp_directory_path() / "ihm.txt"), &why), "un autre nom : refuse");
        check(!R::replyPathOk(utf8(fs::temp_directory_path() / "xpg-enregistrement-12a.txt"), &why), "des lettres dans le numero : refuse");
        check(R::writeReply(good, 5, "refus\xC3\xA9", &why), "la reponse s'ecrit (" + why + ")");
        int code = 0;
        std::string msg;
        check(R::readReply(fileBytes(good), code, msg) && code == 5 && msg == "refus\xC3\xA9", "et se relit");
        check(!R::writeReply(good, 0, "x", &why) && R::readReply(fileBytes(good), code, msg) && code == 5, "deja la : pas ecrasee (" + why + ")");
        check(!R::readReply("4\nmot de pa", code, msg), "une reponse a moitie ecrite ne se lit pas");
        std::error_code ec;
        fs::remove(fs::path(std::u8string(good.begin(), good.end())), ec);
    }
    // ---- 9. LE MOT DE PASSE DE L'ENREGISTREUR N'EST PAS EN CLAIR DANS LE CODE BRUT ---------------------------------------
    {
        Station st("m4b-masque");
        st.p.views.front().object(st.recorder)->set("password", "enigme42");
        const std::string code = serializeObjectCode(st.p.views.front(), {st.recorder});
        check(code.find("enigme42") == std::string::npos && code.find("m1:") != std::string::npos, "le code brut garde le mot de passe masque");
        auto parsed = parseObjectCode(code);
        check(parsed && !parsed->objects.empty() && notify::unmaskSecret(parsed->objects.front().text("password")) == "enigme42", "... et il se relit");
    }
    // ---- 10. C : un nombre est des millisecondes (comme la fiche, comme IHM_APRES) ----------------------------------------
    {
        Station st("m4b-ms");
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        sim::Value r;
        rt.environment().call("IHM_ENREG_DEMARRER", {}, {{"", sim::Value::text("C1")}, {"", sim::Value::integer(sim::Type::DInt, 60000)}}, r);
        check(r.isTruthy() && std::fabs(rt.recordingInfo().maxSeconds - 60.0) < 1e-9, "IHM_ENREG_DEMARRER(\"C1\", 60000) : 60 s (avant : 60000 s)");
        for (int step = 1; step <= 30; ++step) rt.tick(now = step / 10.0);
        (void)rt.stopRecording(true, now);
        std::string why;
        (void)rt.startReplay(ReplayRequest{"C1", {}, 1.0, 0, {}, "essai"}, now, &why);
        rt.environment().call("IHM_RELECTURE_ALLER", {}, {{"", sim::Value::integer(sim::Type::DInt, 1500)}}, r);
        check(std::fabs(rt.replayInfo().position - 1.5) < 1e-6, "IHM_RELECTURE_ALLER(1500) : 1,5 s");
        (void)rt.quitReplay(now);
    }
    // ---- 11. UN .part OUVERT PAR UN AUTRE PROGRAMME N'EST PAS RECUPERE ----------------------------------------------------
    {
        const std::string folder = freshFolder("m4b-part");
        R::Recorder r;
        R::RecorderOptions o;
        o.partPath = folder + "/Vivant.xrec.part";
        o.header.name = "Vivant";
        check(r.open(o), "un enregistrement en cours");
        for (int i = 0; i <= 20; ++i) {
            r.beginSample(i / 10.0);
            r.value("NIVEAU", sim::Value::real(i));
            r.endSample();
            (void)r.flush(i / 10.0, true);
        }
        R::markActive(o.partPath, false);            // comme s'il etait a un autre programme
        const fs::path part = fs::path(std::u8string(o.partPath.begin(), o.partPath.end()));
        std::error_code ec;
        fs::last_write_time(part, fs::file_time_type::clock::now() - std::chrono::minutes(5), ec);
        std::string why, target;
#if !defined(_WIN32)
        check(!R::recoverPart(o.partPath, &target, &why) && fs::exists(part) && !fs::exists(fs::path(folder) / "Vivant.xrec"),
              "ouvert par un autre programme : pas recupere (avant : un .xrec en double) - " + why);
        (void)R::listFolder(folder);
        check(fs::exists(part) && R::fileNames(folder).empty(), "la liste du dossier n'y touche pas non plus");
#endif
        (void)r.close(2.0, false, {}, {}, {}, {});
        // Abandonne pour de bon : une seule fenetre le recupere.
        R::Recorder r2;
        o.partPath = folder + "/Panne.xrec.part";
        (void)r2.open(o);
        for (int i = 0; i <= 20; ++i) {
            r2.beginSample(i / 10.0);
            r2.value("NIVEAU", sim::Value::real(i));
            r2.endSample();
            (void)r2.flush(i / 10.0, true);
        }
        const std::string copy = folder + "/Copie.xrec.part";
        writeBytes(copy, fileBytes(o.partPath));
        (void)r2.close(2.0, false, {}, {}, {}, {});
        check(R::recoverPart(copy, &target, &why) && !R::recoverPart(copy, nullptr, &why), "la premiere fenetre le prend, la seconde ne le trouve plus");
        R::Header h;
        R::Track tr;
        check(R::fileNames(folder).size() == 1 && R::readTrack(target, "", h, tr) == R::Status::Ok && h.cut && tr.frames.size() >= 15,
              "un seul .xrec, relu jusqu'au dernier bloc");
    }
    // ---- 12. EXPORTER : demander avant d'ecraser ; la copie sans le mot de passe -----------------------------------------
    {
        Station st("m4b-export");
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        double now = 0;
        std::string why, written;
        (void)rt.startRecording(RecordingStart{"Prot", {}, "pw", 0, false, "essai"}, now, &why);
        for (int step = 1; step <= 20; ++step) rt.tick(now = step / 10.0);
        (void)rt.stopRecording(true, now);
        const std::string folder = rt.recordingsFolder();
        const std::string out = freshFolder("m4b-export-dehors");
        writeBytes(out + "/Prot.xrec", "un autre fichier");
        bool existed = false;
        R::ExportOptions eo;
        eo.exists = &existed;
        check(!R::exportFile(folder, "Prot", out, &written, &why, eo) && existed && fileBytes(out + "/Prot.xrec") == "un autre fichier",
              "un fichier deja la : pas ecrase sans le demander (" + why + ")");
        eo.ifExists = R::ExportOptions::IfExists::FreeName;
        check(R::exportFile(folder, "Prot", out, &written, &why, eo) && R::nameOfPath(written) == "Prot (2)", "ou un nom libre : " + written);
        eo.ifExists = R::ExportOptions::IfExists::Replace;
        eo.unprotect = "faux";
        R::Status st2 = R::Status::Ok;
        eo.status = &st2;
        check(!R::exportFile(folder, "Prot", out, &written, &why, eo) && st2 == R::Status::BadPassword
                  && fileBytes(out + "/Prot.xrec") == "un autre fichier",
              "sans le mot de passe dans la copie, un faux : refuse, rien d'ecrit");
        eo.unprotect = "pw";
        R::Header h;
        R::Track tr;
        check(R::exportFile(folder, "Prot", out, &written, &why, eo) && R::readTrack(out + "/Prot.xrec", "", h, tr) == R::Status::Ok && !h.locked()
                  && !tr.frames.empty(),
              "remplacer, et la copie sans protection se rejoue sans mot de passe (" + why + ")");
        check(R::readTrack(R::pathOf(folder, "Prot"), "", h, tr) == R::Status::PasswordNeeded, "l'original reste protege");
    }
    // ---- 14. UN NOM N'EST JAMAIS UN CHEMIN ------------------------------------------------------------------------------
    {
        Station st("m4b-noms");
        Runtime rt;
        st.hooks(rt);
        rt.bind(&st.p, nullptr);
        rt.start(0.0);
        std::string why;
        const std::string decoy = rt.recordingsFolder() + "/../Film.xrec";     // ihm/ressources/Film.xrec : hors du dossier
        const std::string recFolder = rt.recordingsFolder();
        fs::create_directories(fs::path(std::u8string(recFolder.begin(), recFolder.end())));
        writeBytes(decoy, "dehors");
        check(rt.startReplay(ReplayRequest{"..\\Film", {}, 1.0, 0, {}, "essai"}, 0, &why) == R::Status::NotFound && why.find("refus") != std::string::npos,
              "rejouer ..\\Film : refuse (" + why + ")");
        check(rt.startReplay(ReplayRequest{"../Film", {}, 1.0, 0, {}, "essai"}, 0, &why) == R::Status::NotFound, "rejouer ../Film : refuse");
        check(!R::removeFile(rt.recordingsFolder(), "../Film", {}, &why) && fileBytes(decoy) == "dehors", "supprimer ../Film : refuse, le fichier d'a cote reste");
        std::string written;
        check(!R::exportFile(rt.recordingsFolder(), "../Film", st.folder, &written, &why), "exporter ../Film : refuse");
        check(!R::renameFile(rt.recordingsFolder(), "../Film", "X", &why), "renommer ../Film : refuse");
        check(R::cleanName("CON") == "CON_", "un nom reserve devient un nom propre");
    }
    // ---- 15. Renommer dans un script C : "..." ----------------------------------------------------------------------------
    {
        const std::string c = "if (IHM_ENREG_EXISTE(\"Essai\")) IHM_REJOUER(\"Essai\", 2.0);\nauto x = \"IHM_REJOUER('Essai')\";\n";
        const auto uses = R::namesInCode(c);
        check(uses.size() == 2 && uses[0].name == "Essai" && uses[1].name == "Essai", "les noms entre guillemets d'un script C (pas dans une chaine)");
        std::size_t count = 0;
        const std::string renamed = R::renameInCode(c, "Essai", "Nuit \"2\"", &count);
        check(count == 2 && renamed.find("IHM_REJOUER(\"Nuit \\\"2\\\"\", 2.0)") != std::string::npos && renamed.find("'Essai'") != std::string::npos,
              "renommes entre guillemets (\\\" echappe) ; la chaine d'a cote ne bouge pas : " + renamed);
        check(R::namesInCode("IHM_REJOUER('L''essai');").front().name == "L'essai", "ST : toujours entre apostrophes");
    }
    // ---- 16. L'ARCHIVE : la case decide pour tous ------------------------------------------------------------------------
    {
        namespace pa = project::archive;
        pa::CollectOptions co;
        co.recordings = false;
        co.recordingsDir = "mes_enregistrements";
        check(pa::excluded("mes_enregistrements/A.xrec", co) && pa::excluded("ihm/ressources/enregistrements/B.xrec", co)
                  && !pa::excluded("mes_enregistrements/notes.txt", co),
              "decochee : le dossier de la configuration (ses .xrec) et le natif restent");
        co.recordings = true;
        check(!pa::excluded("mes_enregistrements/A.xrec", co) && !pa::excluded("ihm/ressources/enregistrements/B.xrec", co)
                  && pa::excluded("mes_enregistrements/A.xrec.part", co),
              "cochee : tous partent (jamais un enregistrement en cours)");
        const std::string pf = freshFolder("m4b-archive");
        fs::create_directories(fs::path(pf) / "mes_enregistrements");
        writeBytes(pf + "/mes_enregistrements/A.xrec", "aaaa");
        writeBytes(pf + "/mes_enregistrements/B.xrec", "bbbbbb");
        RecordingSettings rs;
        rs.folder = "mes_enregistrements";
        const auto share = recordingsShare(rs, pf);
        check(share.files == 2 && share.bytes == 10 && !share.outside, "le libelle de l'export dit le vrai compte (2, pas \xC2\xAB aucun \xC2\xBB)");
        rs.folder = freshFolder("m4b-archive-ailleurs");
        writeBytes(rs.folder + "/C.xrec", "c");
        const auto away = recordingsShare(rs, pf);
        check(away.files == 1 && away.outside, "un dossier hors du projet : compte, et dit hors du projet");
    }
    // ---- 18. Generer : Pause, Avancer, Vitesse, Quitter seules sur un clic n'agiraient jamais ------------------------------
    {
        Station st("m4b-generer");
        View& v = st.p.views.front();
        const Id lone = edit::add(st.p, v, Kind::Button, 10, 10);
        v.object(lone)->name = "Btn_Quitter";
        v.object(lone)->actions = {act(Trigger::Click, Operation::ReplayQuit, {})};
        const Id suite = edit::add(st.p, v, Kind::Button, 10, 60);
        v.object(suite)->name = "Btn_Suite";
        v.object(suite)->actions = {act(Trigger::Click, Operation::ReplayStart, "X"), act(Trigger::Click, Operation::ReplayPause, {})};
        const auto issues = generate(st.p, [](std::string_view) { return true; }, st.folder);
        std::size_t lonely = 0;
        for (const auto& i : issues)
            if (i.message.find("n'agira jamais seule") != std::string::npos) {
                ++lonely;
                check(i.object == lone && i.severity == Issue::Severity::Warning, "l'avertissement est sur Btn_Quitter");
            }
        check(lonely == 1, "Generer : Quitter la relecture seule sur un clic est signalee ; apres Rejouer, non (" + std::to_string(lonely) + ")");
    }
    // ---- 18. prune : par la date de l'en-tete ---------------------------------------------------------------------------
    {
        const std::string folder = freshFolder("m4b-prune");
        const char* dates[] = {"2026-01-01 10:00:00", "2026-02-01 10:00:00", "2026-03-01 10:00:00"};
        const char* names[] = {"Ancien", "Moyen", "Recent"};
        for (int k = 0; k < 3; ++k) {
            R::Recorder r;
            R::RecorderOptions o;
            o.partPath = folder + "/" + names[k] + ".xrec.part";
            o.header.name = names[k];
            o.header.date = dates[k];
            (void)r.open(o);
            r.beginSample(0);
            r.value("X", sim::Value::real(k));
            r.endSample();
            (void)r.close(1.0, true, folder + "/" + names[k] + ".xrec", names[k], {}, {});
        }
        // La description de l'ancien change : son fichier rajeunit, pas son enregistrement.
        check(R::setDescription(folder + "/Ancien.xrec", "relu ce matin"), "la description de l'ancien change");
        const auto removed = R::prune(folder, R::folderBytes(folder) - 1);
        check(removed.size() == 1 && removed[0] == "Ancien", "trop gros : le plus ancien enregistre part (" + (removed.empty() ? std::string("rien") : removed[0]) + ")");
    }
}

} // namespace

int main() {
    testFormat();
    testTrack();
    testFilm();
    testReplayControls();
    testPassword();
    testTruncated();
    testFiles();
    testProjectChanged();
    testNatives();
    testActions();
    testObjects();
    testCommandLine();
    testLimits();
    testGenerateAndStore();
    testRelectureM4b();      // 1.14.0 : la relecture contradictoire du lot
    // Les dossiers de l'essai (xpg-m4-<pid>-*) : retires.
    {
        std::error_code ec;
        const std::string prefix = "xpg-m4-" + std::to_string(XPG_GETPID()) + "-";
        for (fs::directory_iterator it(fs::temp_directory_path(), ec), end; !ec && it != end; it.increment(ec))
            if (it->path().filename().string().rfind(prefix, 0) == 0) fs::remove_all(it->path(), ec);
    }
    std::printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
