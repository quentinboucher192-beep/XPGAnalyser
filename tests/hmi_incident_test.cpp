// =============================================================================
//  tests/hmi_incident_test.cpp - 1.20.0 : les incidents (essais A a G de la demande)
// -----------------------------------------------------------------------------
//  A. la detection (apparition, maintien, disparition, reapparition, plusieurs a la fois, filtres) ;
//  B. les dependances (variable directe, membre d'une structure, equipement, adresse, script, fonction,
//     plusieurs alarmes sur une variable, dependances indirectes) ;
//  C. l'analyse (la chaine, les alarmes liees, l'equipement commun, la chronologie, correlation et causalite,
//     ce qui manque, aucune relation inventee) ;
//  D. l'enregistrement (la capture, la video du tampon circulaire, l'horodatage, le contexte, en attente/) ;
//  E. l'immuabilite (le projet modifie, une alarme supprimee, un equipement renomme, une vue modifiee) ;
//  F. l'archivage (valider, deplacer, conserver, verifier les empreintes, reprendre apres un arret brutal) ;
//  G. la resilience (IA distante et locale indisponibles, erreur et disque plein, incidents simultanes,
//     projet volumineux, beaucoup d'alarmes).
//  1.21.0 : une alarme qui bat (300 000 reapparitions : un incident, le compte exact, la memoire et le fichier
//     bornes), regroupement 0 (comme avant), l'anti-rebond, le plafond horaire, le quota disque, les reglages.
// =============================================================================
#include "../src/app/ai/AiIncident.hpp"
#include "../src/app/hmi/HmiIncidentHost.hpp"
#include "../src/app/hmi/HmiIncidentsPane.hpp"   // 1.20.2 : le volet (dossiers par alarme, recherche, criticite, tri)
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiIncident.hpp"
#include "../src/hmi/HmiRelations.hpp"
#include "../src/hmi/HmiVideo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace hmi;
namespace inc = hmi::incident;
namespace st = app::ai::incidents;
namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// ---- le projet : une station (deux pompes, une cuve, un equipement Modbus, des scripts, une fonction) ------
TypeMember member(const char* name, const char* type) {
    TypeMember m;
    m.name = name;
    m.type = type;
    return m;
}
Variable makeVar(Project& p, const char* name, const char* type, const char* equipment = "", const char* address = "") {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.equipment = equipment;
    v.address = address;
    return v;
}
AlarmDef makeAlarm(Project& p, const char* name, const char* condition, int priority = 2) {
    AlarmDef a;
    a.id = p.allocate();
    a.name = name;
    a.condition = condition;
    a.message = name;
    a.priority = priority;
    a.description = std::string("Alarme ") + name;
    return a;
}

Project station() {
    Project p;
    p.config.name = "Station_Essai";
    p.config.version = "2.4.1";
    HmiType pompe;
    pompe.id = p.allocate();
    pompe.name = "T_Pompe";
    pompe.members = {member("Marche", "BOOL"), member("Defaut", "BOOL"), member("Pression", "REAL")};
    p.programs.types.push_back(pompe);
    for (const char* n : {"Station_Modbus", "Automate_Cuves"}) {
        Equipment eq;
        eq.id = p.allocate();
        eq.name = n;
        eq.simulated = true;
        p.equipments.push_back(eq);
    }
    p.programs.variables.push_back(makeVar(p, "Pompe_1", "T_Pompe", "Station_Modbus", "40001"));
    p.programs.variables.push_back(makeVar(p, "Pompe_2", "T_Pompe", "Station_Modbus", "40005"));
    p.programs.variables.push_back(makeVar(p, "Niveau_Cuve_1", "REAL", "Automate_Cuves", "40021"));
    p.programs.variables.push_back(makeVar(p, "Debit_Entree", "REAL", "Station_Modbus", "40031"));
    p.programs.variables.push_back(makeVar(p, "Seuil_Haut", "REAL"));
    // un script ECRIT la variable de la condition et lit une autre mesure (la dependance indirecte)
    Script calc;
    calc.id = p.allocate();
    calc.name = "Calcul_Niveau";
    calc.event = "Cyclique";
    calc.body = "Niveau_Cuve_1 := Niveau_Cuve_1 + Debit_Entree * 0.1;\n";
    p.programs.scripts.push_back(calc);
    // une fonction qui LIT la variable
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Niveau_Normalise";
    f.returnType = "REAL";
    f.body = "Niveau_Normalise := Niveau_Cuve_1 / 100.0;";
    p.programs.functions.push_back(f);
    p.alarms.push_back(makeAlarm(p, "Defaut_Pompe_2", "Pompe_2.Defaut", 1));
    p.alarms.push_back(makeAlarm(p, "Pression_Basse_Pompe_2", "Pompe_2.Pression < 1.5", 2));
    p.alarms.push_back(makeAlarm(p, "Niveau_Haut", "Niveau_Cuve_1 > Seuil_Haut", 2));
    p.alarms.push_back(makeAlarm(p, "Niveau_Tres_Haut", "Niveau_Cuve_1 > 95.0", 1));
    p.alarms.push_back(makeAlarm(p, "Defaut_Pompe_1", "Pompe_1.Defaut", 2));
    View v = makeView(p, "Vue_Station");
    v.objects.push_back(makeObject(hmi::Kind::Indicator, p.allocate(), "Voyant_P2", 10, 10, v.layers.front().id));
    v.objects.back().setExpr("value", "Pompe_2.Defaut");
    v.objects.push_back(makeObject(hmi::Kind::Text, p.allocate(), "Txt_Niveau", 10, 60, v.layers.front().id));
    v.objects.back().set("text", "Niveau : {Niveau_Cuve_1:0.0} %");
    p.views.push_back(v);
    return p;
}

AlarmNotice notice(const char* kind, const char* name, const char* at, int priority = 2) {
    AlarmNotice n;
    n.kind = kind;
    n.name = name;
    n.at = at;
    n.priority = priority;
    return n;
}

const char* kReapp = "R\xC3\xA9" "apparition";

std::string readText(const std::string& path) {
    std::string s;
    (void)core::readFileAll(fs::path(std::u8string(path.begin(), path.end())), s);
    return s;
}

fs::path tempRoot(const char* name) {
    const fs::path r = fs::temp_directory_path() / (std::string("xpg_incident_") + name);
    std::error_code ec;
    fs::remove_all(r, ec);
    fs::create_directories(r, ec);
    return r;
}

// ---- A. la detection ----------------------------------------------------------------------------------------------
void detection() {
    std::printf("A. la detection\n");
    inc::Settings s;
    s.heldAfter = 30;
    s.debounce = 5;
    inc::Monitor m(s);
    m.notice(notice("Apparition", "Defaut_Pompe_2", "2026-10-31 10:00:00.000"), 0.0);
    auto t = m.takeTriggers();
    check(t.size() == 1 && t[0].kind == inc::EventKind::Appear, "A1 une apparition : un incident");
    // le meme front redit pendant que l'alarme est active : rien (jamais un incident a chaque cycle)
    for (int i = 0; i < 50; ++i) m.notice(notice("Apparition", "Defaut_Pompe_2", "x"), 0.1 * i);
    check(m.takeTriggers().empty(), "A1 l'alarme qui reste active ne refait pas d'incident");
    m.tick(10.0);
    check(m.take().size() == 1, "A2 (seulement l'apparition dans les evenements)");
    m.tick(31.0);
    auto ev = m.take();
    check(ev.size() == 1 && ev[0].kind == inc::EventKind::Held, "A2 le maintien est dit une fois apres 30 s");
    m.tick(60.0);
    check(m.take().empty(), "A2 ... et une seule fois");
    m.notice(notice("Disparition", "Defaut_Pompe_2", "2026-10-31 10:01:00.000"), 60.0);
    ev = m.take();
    check(ev.size() == 1 && ev[0].kind == inc::EventKind::Clear && has(ev[0].detail, "active 60,0 s"), "A3 la disparition et sa duree");
    check(m.active().empty(), "A3 plus active");
    m.notice(notice("Apparition", "Defaut_Pompe_2", "2026-10-31 10:02:00.000"), 120.0);
    t = m.takeTriggers();
    check(t.size() == 1 && t[0].kind == inc::EventKind::Reappear, "A4 la reapparition apres une disparition : un nouvel incident");
    check(m.occurrences("defaut_pompe_2") == 2, "A4 deux apparitions comptees (sans casse)");
    m.notice(notice("Disparition", "Defaut_Pompe_2", "x"), 121.0);
    m.notice(notice("Apparition", "Defaut_Pompe_2", "x"), 122.0);
    check(m.takeTriggers().empty(), "A4 une reapparition dans l'anti-rebond (5 s) : le meme incident");
    // plusieurs alarmes au meme instant : un incident chacune
    for (const char* n : {"A_1", "A_2", "A_3", "A_4", "A_5"}) m.notice(notice("Apparition", n, "2026-10-31 10:05:00.000"), 300.0);
    check(m.takeTriggers().size() == 5, "A5 cinq alarmes simultanees : cinq incidents");
    check(m.active().size() == 6, "A5 six alarmes actives");
    // les filtres : surveillees, ignorees
    inc::Settings f;
    f.watched = {"Defaut_*"};
    f.ignored = {"*Pompe_1"};
    inc::Monitor m2(f);
    m2.notice(notice("Apparition", "Defaut_Pompe_2", "x"), 0);
    m2.notice(notice("Apparition", "Defaut_Pompe_1", "x"), 0);
    m2.notice(notice("Apparition", "Niveau_Haut", "x"), 0);
    t = m2.takeTriggers();
    check(t.size() == 1 && t[0].name == "Defaut_Pompe_2", "A6 seules les alarmes surveillees font un incident");
    check(m2.timeline(-1, 1).size() == 3, "A6 ... mais toutes vont dans la chronologie");
    // la communication : un front de perte fait un incident, le retour non
    inc::Monitor m3;
    EquipmentStatus e;
    e.name = "Station_Modbus";
    e.tested = true;
    e.reachable = true;
    e.state = "Connect\xC3\xA9";
    m3.equipments({e}, 0);
    check(m3.takeTriggers().empty(), "A7 un equipement joignable : rien");
    e.reachable = false;
    e.state = "Injoignable";
    e.why = "d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9";
    m3.equipments({e}, 1);
    m3.equipments({e}, 2);
    t = m3.takeTriggers();
    check(t.size() == 1 && t[0].kind == inc::EventKind::CommLost, "A7 la perte de communication : un incident (une fois)");
    e.reachable = true;
    m3.equipments({e}, 3);
    check(m3.takeTriggers().empty() && m3.take().back().kind == inc::EventKind::CommBack, "A7 le retour : un evenement, pas un incident");
    inc::Settings off;
    off.enabled = false;
    inc::Monitor m4(off);
    m4.notice(notice("Apparition", "X", "x"), 0);
    check(m4.takeTriggers().empty() && m4.take().empty(), "A8 la surveillance arretee : rien");
}

// ---- B. C. les dependances et l'analyse --------------------------------------------------------------------------
inc::Snapshot snap(const Project& p, const char* alarm, std::vector<inc::Event> timeline = {}, std::vector<EquipmentStatus> eq = {},
                   std::map<std::string, std::string> values = {}) {
    inc::SnapshotInput in;
    in.project = &p;
    in.trigger.kind = inc::EventKind::Appear;
    in.trigger.name = alarm;
    in.trigger.at = "2026-10-31 10:00:10.000";
    in.trigger.t = 10.0;
    in.timeline = std::move(timeline);
    in.equipments = std::move(eq);
    in.openView = "Vue_Station";
    in.projectName = p.config.name;
    in.projectVersion = p.config.version;
    in.id = "0123456789abcdef0123456789abcdef";
    in.createdAt = "2026-10-31 10:00:10.050";
    in.read = [values](std::string_view path) -> std::optional<std::pair<std::string, std::string>> {
        const auto it = values.find(std::string(path));
        if (it == values.end()) return std::nullopt;
        return std::make_pair(it->second, std::string("bonne"));
    };
    return inc::snapshot(in);
}
bool chainHas(const inc::Snapshot& s, const std::string& kind, const std::string& name, inc::Certainty c) {
    return std::any_of(s.chain.begin(), s.chain.end(), [&](const inc::Link& l) { return l.kind == kind && has(l.name, name) && l.certainty == c; });
}

void dependencies() {
    std::printf("B. les dependances\n");
    const Project p = station();
    const inc::Snapshot a = snap(p, "Defaut_Pompe_2");
    check(a.alarm.known && a.alarm.condition == "Pompe_2.Defaut" && a.alarm.priority == 1, "B0 l'alarme copiee (condition, priorite)");
    check(a.conditionVariables.size() == 1 && a.conditionVariables[0] == "Pompe_2.Defaut", "B2 le membre d'une structure lu par la condition");
    check(chainHas(a, "variable", "Pompe_2.Defaut", inc::Certainty::Fact), "B1 la variable directement utilisee (fait)");
    check(chainHas(a, "structure", "T_Pompe", inc::Certainty::Fact), "B2 la structure");
    check(chainHas(a, "equipement", "Station_Modbus", inc::Certainty::Fact), "B3 l'equipement associe");
    check(chainHas(a, "adresse", "40005", inc::Certainty::Fact), "B4 l'adresse de communication");
    check(chainHas(a, "objet", "Voyant_P2", inc::Certainty::Fact) && chainHas(a, "vue", "Vue_Station", inc::Certainty::Fact), "B la vue et l'objet qui la montrent");
    check(std::find(a.visibleObjects.begin(), a.visibleObjects.end(), "Voyant_P2") != a.visibleObjects.end(), "B l'objet visible sur la vue ouverte");
    const inc::Snapshot n = snap(p, "Niveau_Haut");
    check(chainHas(n, "script", "Calcul_Niveau", inc::Certainty::Fact), "B5 le script qui ecrit la variable");
    check(chainHas(n, "fonction", "Niveau_Normalise", inc::Certainty::Fact), "B6 la fonction qui la lit");
    check(chainHas(n, "variable", "Debit_Entree", inc::Certainty::Indirect), "B8 la dependance indirecte (ce que lit le script qui ecrit)");
    check(chainHas(n, "equipement", "Station_Modbus", inc::Certainty::Indirect), "B8 l'equipement de la dependance indirecte");
    check(std::find(n.conditionVariables.begin(), n.conditionVariables.end(), "Seuil_Haut") != n.conditionVariables.end(), "B une variable IHM de la condition");
    bool shared = false;
    for (const auto& r : n.related)
        if (r.name == "Niveau_Tres_Haut" && r.kind == inc::Related::Kind::Shared) shared = true;
    check(shared, "B7 plusieurs alarmes partageant une variable");

    std::printf("C. l'analyse\n");
    // la chronologie : la perte de communication de Station_Modbus avant l'alarme de pression ; une alarme simultanee
    std::vector<inc::Event> tl;
    inc::Event lost;
    lost.kind = inc::EventKind::CommLost;
    lost.name = "Station_Modbus";
    lost.t = 7.5;
    lost.at = "2026-10-31 10:00:07.500";
    tl.push_back(lost);
    inc::Event pressure;
    pressure.kind = inc::EventKind::Appear;
    pressure.name = "Pression_Basse_Pompe_2";
    pressure.t = 10.0;
    tl.push_back(pressure);
    inc::Event other;
    other.kind = inc::EventKind::Appear;
    other.name = "Alarme_Sans_Lien";
    other.t = 10.4;
    tl.push_back(other);
    inc::Event late;
    late.kind = inc::EventKind::Appear;
    late.name = "Defaut_Pompe_1";
    late.t = 40.0;
    tl.push_back(late);
    EquipmentStatus down;
    down.name = "Station_Modbus";
    down.tested = true;
    down.reachable = false;
    down.state = "Injoignable";
    const inc::Snapshot c = snap(p, "Pression_Basse_Pompe_2", tl, {down}, {{"Pompe_2.Pression", "1.2"}});
    check(chainHas(c, "equipement", "Station_Modbus", inc::Certainty::Fact), "C1 la chaine complete jusqu'a l'equipement");
    std::map<std::string, inc::Related::Kind> rel;
    for (const auto& r : c.related) rel[r.name] = r.kind;
    check(rel.count("Defaut_Pompe_2") && rel["Defaut_Pompe_2"] == inc::Related::Kind::Secondary,
          "C2 une alarme du meme equipement, apres la perte de communication : potentiellement secondaire");
    check(rel.count("Defaut_Pompe_1") && rel["Defaut_Pompe_1"] == inc::Related::Kind::Secondary, "C3 l'equipement commun (Pompe_1 aussi sur Station_Modbus)");
    check(rel.count("Alarme_Sans_Lien") && rel["Alarme_Sans_Lien"] == inc::Related::Kind::Simultaneous, "C2 simultanee sans dependance connue");
    check(!c.hypotheses.empty() && c.hypotheses.front().certainty == inc::Certainty::Indirect && has(c.hypotheses.front().text, "communication"),
          "C la piste la mieux appuyee d'abord : la communication (relation indirecte)");
    const std::string fiche = inc::diagnosis(c);
    for (const char* section : {"### 1. R", "### 2. Alarme", "### 3. Origine", "### 4. Cha", "### 5. Autres", "### 6. Chronologie", "### 7. Analyse",
                                "### 8. Causes", "### 9. ", "### 10. Niveau"})
        check(has(fiche, section), std::string("C la fiche a sa section ") + section);
    check(has(fiche, "n'est pas une preuve de causalit"), "C5 correlation n'est pas causalite (dit dans la fiche)");
    check(has(fiche, "peut expliquer") && !has(fiche, "a caus"), "C5 une piste dit << peut expliquer >>, jamais << a cause >>");
    check(has(fiche, "Pompe_2.Pression = 1.2 (bonne)"), "C la valeur disponible au moment du defaut");
    // C4 : la chronologie dans l'ordre
    std::size_t posLost = fiche.find("Perte de communication : Station_Modbus"), posApp = fiche.find("Apparition : Pression_Basse_Pompe_2");
    check(posLost != std::string::npos && posApp != std::string::npos && posLost < posApp, "C4 la chronologie dans l'ordre des evenements");
    // C6 : une valeur absente est dite absente
    const inc::Snapshot nv = snap(p, "Niveau_Haut");
    bool absent = std::any_of(nv.values.begin(), nv.values.end(), [](const inc::ValueAt& v) { return !v.available && v.value.empty(); });
    check(absent && std::any_of(nv.missing.begin(), nv.missing.end(), [](const std::string& m) { return has(m, "non disponible"); }),
          "C6 les informations manquantes signalees (jamais une valeur inventee)");
    check(has(inc::diagnosis(nv), "non disponible"), "C6 ... dans la fiche");
    // C7 : aucune relation inventee : chaque nom de la chaine existe dans le projet
    std::set<std::string> known;
    for (const auto& v : p.programs.variables) known.insert(v.name);
    for (const auto& t : p.programs.types) known.insert(t.name);
    for (const auto& e : p.equipments) known.insert(e.name);
    for (const auto& s : p.programs.scripts) known.insert(s.name);
    for (const auto& f : p.programs.functions) known.insert(f.name);
    for (const auto& vw : p.views) {
        known.insert(vw.name);
        for (const auto& o : vw.objects) known.insert(o.name);
    }
    for (const auto* sp : {&a, &n, &c})
        for (const auto& l : sp->chain) {
            bool found = false;
            for (const auto& k : known)
                if (has(l.name, k)) found = true;
            check(found, "C7 aucun maillon invente : " + l.kind + " " + l.name);
        }
    // une alarme inconnue du projet : dite, pas inventee
    const inc::Snapshot u = snap(p, "Alarme_Objet_Inconnue");
    check(!u.alarm.known && u.chain.empty() && !u.missing.empty(), "C7 une alarme hors du projet : aucune chaine inventee, le manque dit");
    // le contexte cible pour une IA : borne
    check(inc::aiContext(c, 2000).size() <= 2000 + 32, "C le contexte envoye a l'IA est borne");
    check(st::certaintyOf("blabla\nCertitude : relation indirecte") == "indirect" && st::certaintyOf("rien") == "", "C la certitude dite par le modele");
}

// ---- D. E. F. le stockage --------------------------------------------------------------------------------------
std::vector<std::uint8_t> fakeJpeg() { return {0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4, 0xFF, 0xD9}; }

void storage() {
    std::printf("D. E. F. le dossier d'incident\n");
    const fs::path root = tempRoot("store");
    st::Store store(root.string());
    Project p = station();
    inc::Snapshot s = snap(p, "Defaut_Pompe_2");
    st::Capture cap;
    cap.jpeg = fakeJpeg();
    cap.width = 64;
    cap.height = 48;
    cap.at = s.createdAt;
    std::string folder, why;
    check(store.create(s, cap, st::initialAnalysis(s, s.createdAt), folder, &why), "D6 la creation dans en attente/ : " + why);
    check(fs::path(folder).parent_path().filename() == "en attente", "D6 le dossier est dans << en attente >>");
    for (const char* f : {"incident.json", "capture.jpg", "analyses/000-initiale.json", "empreintes.sha256"})
        check(fs::exists(fs::path(folder) / f), std::string("D le fichier ") + f);
    const std::string original = readText((fs::path(folder) / "incident.json").string());
    check(has(original, "\"horodatage\": \"2026-10-31 10:00:10.000\"") && has(original, "\"version\": \"2.4.1\""), "D4 l'horodatage et la version du projet");
    check(has(original, "\"vue_affichee\": \"Vue_Station\"") && has(original, "Voyant_P2"), "D5 la vue affichee et les elements visibles");
    check(has(original, "capture d'\xC3\xA9" "cran unique (une image, pas une vid\xC3\xA9o)"), "D1 la capture est dite image, jamais video");
    // la video (Motion-JPEG) : lisible par le lecteur de l'application
    st::Video v;
    v.width = 2;
    v.height = 2;
    v.fps = 2;
    v.before = 1;
    v.after = 1;
    for (int i = 0; i < 4; ++i) {
        v.frames.push_back(std::make_shared<const std::vector<std::uint8_t>>(fakeJpeg()));
        v.times.push_back(-1.0 + 0.5 * i);
    }
    check(store.addVideo(folder, v, &why), "D2 la video ajoutee : " + why);
    check(!store.addVideo(folder, v, &why), "E la video ne se reecrit pas");
    std::string avi = readText((fs::path(folder) / "video.avi").string());
    hmi::video::AviInfo info;
    check(hmi::video::parseAvi(hmi::Bytes(avi.begin(), avi.end()), info) && info.frames.size() == 4 && info.mjpeg(), "D2 un vrai AVI Motion-JPEG de 4 images");
    // les analyses et commentaires : des fichiers de plus
    st::Analysis later;
    later.source = "Fournisseur essai (modele-x)";
    later.state = "faite";
    later.text = "Synthese : la pompe 2.\nCertitude : hypoth\xC3\xA8se";
    later.certainty = "hypothese";
    check(store.addAnalysis(folder, later, &why) && later.number == 1, "E une nouvelle analyse : un fichier de plus (n 1)");
    st::Comment cm;
    cm.text = "Vu sur site : capteur debranche";
    cm.author = "essai";
    check(store.addComment(folder, cm, &why) && cm.number == 1, "E un commentaire : un fichier de plus");
    st::Item it = store.read(folder);
    check(it.readable && it.analyses.size() == 2 && it.analyses[0].kind == "initiale" && it.analyses[1].number == 1,
          "E l'analyse initiale gardee, la nouvelle a part (les comparer)");
    check(it.hasCapture && it.hasVideo && it.videoFrames == 4, "D la fiche relue : capture et video");
    check(it.equipment() == "Station_Modbus" && it.synthesis() == "Synthese : la pompe 2." && it.certainty() == "hypoth\xC3\xA8se", "D la ligne de la liste");
    check(store.verify(folder).ok, "F4 les empreintes verifiees");
    // E : le projet change ; l'incident ne bouge pas
    for (auto& e : p.equipments)
        if (e.name == "Station_Modbus") e.name = "Station_Renommee";
    p.alarms.erase(std::remove_if(p.alarms.begin(), p.alarms.end(), [](const AlarmDef& a) { return a.name == "Defaut_Pompe_2"; }), p.alarms.end());
    p.views[0].objects.clear();
    p.views[0].name = "Vue_Refaite";
    (void)store.list(st::State::Pending);
    check(readText((fs::path(folder) / "incident.json").string()) == original, "E1-E4 projet modifie, alarme supprimee, equipement renomme, vue modifiee : l'incident reste identique");
    check(store.read(folder).snapshot.alarm.condition == "Pompe_2.Defaut", "E les noms et la condition d'origine");
    // E : une alteration se voit
    {
        const std::string capPath = (fs::path(folder) / "capture.jpg").string();
        std::string bytes = readText(capPath);
        (void)core::writeFileAtomic(fs::path(capPath), bytes + "x", core::AtomicWrite{false, true});
        const st::Integrity bad = store.verify(folder);
        check(!bad.ok && bad.altered.size() == 1 && bad.altered[0] == "capture.jpg", "E une capture modifiee est detectee");
        std::string w;
        std::string target;
        check(!store.validate(folder, "essai", "", target, &w) && has(w, "n'est pas intact"), "F un incident altere n'est pas valide");
        (void)core::writeFileAtomic(fs::path(capPath), bytes, core::AtomicWrite{false, true});
    }
    // F : valider
    std::string validated;
    check(store.validate(folder, "Technicien", "Capteur remplace", validated, &why), "F1 la validation : " + why);
    check(fs::path(validated).parent_path().filename() == "valides" && !fs::exists(folder), "F2 deplace vers valides/ (l'original retire apres la copie verifiee)");
    check(store.verify(validated).ok, "F4 les empreintes de la copie");
    const st::Item vi = store.read(validated);
    check(vi.state == st::State::Validated && vi.validation && vi.validation->author == "Technicien" && vi.analyses.size() == 2 && vi.comments.size() == 1
              && vi.hasVideo && vi.hasCapture,
          "F3 la capture, la video, les analyses, les commentaires et la validation conserves");
    check(readText((fs::path(validated) / "incident.json").string()) == original, "F3 le contexte d'origine conserve");
    check(!store.remove(validated, &why), "F un incident valide ne se supprime pas d'ici");
    // jamais ecraser : un second incident de meme nom
    inc::Snapshot s2 = s;
    s2.id = st::newId();
    inc::Snapshot s3 = s;
    s3.id = st::newId();
    check(s2.id != s3.id && s2.id.size() == 32, "F un identifiant unique par incident");
    std::string f2, f3;
    check(store.create(s2, cap, st::initialAnalysis(s2, s2.createdAt), f2, &why), "F un second incident de la meme alarme");
    check(store.create(s3, cap, st::initialAnalysis(s3, s3.createdAt), f3, &why) && f3 != f2, "F jamais ecraser : un autre nom");
    std::string v2;
    check(store.validate(f2, "x", "", v2, &why) && v2 != validated, "F la validation ne remplace pas un valide existant");
    check(readText((fs::path(validated) / "incident.json").string()) == original, "F ... le premier valide reste identique");
    // l'historique de l'alarme
    const auto hist = store.history("Defaut_Pompe_2");
    check(hist.size() == 3, "E l'historique : les incidents precedents de la meme alarme");
    // F5 : la reprise apres un arret brutal
    const fs::path pend = fs::path(store.pendingFolder());
    fs::copy(f3, pend / "20261031-100010-REPRIS.partiel", fs::copy_options::recursive);
    fs::remove(pend / "20261031-100010-REPRIS.partiel" / "empreintes.sha256");
    fs::create_directories(pend / "20261031-100011-CASSE.partiel");
    fs::create_directories(fs::path(store.validatedFolder()) / "copie-interrompue.partiel");
    const auto notes = store.recover();
    check(fs::exists(pend / "20261031-100010-REPRIS") && store.verify((pend / "20261031-100010-REPRIS").string()).ok, "F5 un .partiel complet est repris (empreintes refaites)");
    check(fs::exists(pend / ".20261031-100011-CASSE.incomplet") && !fs::exists(pend / "20261031-100011-CASSE.partiel"), "F5 un .partiel incomplet est garde a part");
    check(!fs::exists(fs::path(store.validatedFolder()) / "copie-interrompue.partiel"), "F5 une copie de validation interrompue est retiree");
    check(notes.size() >= 3, "F5 la reprise dit ce qu'elle a fait");
    // F5 : une validation finie dont l'original est reste
    fs::copy(validated, pend / "doublon", fs::copy_options::recursive);
    fs::remove(pend / "doublon" / "validation.json");
    (void)store.recover();
    check(!fs::exists(pend / "doublon"), "F5 l'original reste apres une validation finie est retire");
    // le liste : en attente, du plus recent au plus ancien ; les dossiers caches ignores
    const auto pending = store.list(st::State::Pending);
    check(std::none_of(pending.begin(), pending.end(), [](const st::Item& x) { return x.name.front() == '.'; }), "F les dossiers incomplets ne sont pas listes");
    std::error_code ec;
    fs::remove_all(root, ec);
}

// ---- G. la resilience (le disque) ------------------------------------------------------------------------------
void disk() {
    std::printf("G. le disque\n");
    const fs::path root = tempRoot("disk");
    st::Store store(root.string());
    const Project p = station();
    const inc::Snapshot s = snap(p, "Defaut_Pompe_2");
    store.failWritesForTests([](const std::string& path) { return has(path, "incident.json"); });
    std::string folder, why;
    check(!store.create(s, {}, st::initialAnalysis(s, s.createdAt), folder, &why) && has(why, "disque plein"), "G4 le disque plein : l'erreur dite");
    check(store.pendingCount() == 0, "G3 aucun dossier a moitie ecrit ne reste visible");
    store.failWritesForTests(nullptr);
    check(store.create(s, {}, st::initialAnalysis(s, s.createdAt), folder, &why), "G3 le disque revenu : l'incident s'ecrit");
    check(has(readText((fs::path(folder) / "incident.json").string()), "aucune image"), "D sans vue en marche : pas d'image, et c'est dit");
    // un dossier impossible (un fichier a la place du dossier)
    const fs::path blocked = root / "bloque";
    (void)core::writeFileAtomic(blocked, "x", core::AtomicWrite{false, false});
    st::Store bad((blocked / "enregistrements").string());
    check(!bad.create(s, {}, st::initialAnalysis(s, s.createdAt), folder, &why) && !why.empty(), "G3 un dossier impossible a creer : l'erreur dite");
    std::error_code ec;
    fs::remove_all(root, ec);
}

// ---- D. G. l'hote : le tampon circulaire, l'ecriture en arriere-plan, l'IA ---------------------------------------
class FakeProvider : public app::ai::Provider {
public:
    FakeProvider(bool ok, int* calls) : ok_(ok), calls_(calls) {}
    std::string_view key() const noexcept override { return "faux"; }
    bool start(app::ai::ProviderRequest r, std::string*) override {
        ++*calls_;
        request_ = std::move(r);
        started_ = true;
        return true;
    }
    std::vector<app::ai::ProviderEvent> poll() override {
        std::vector<app::ai::ProviderEvent> out;
        if (!started_) return out;
        started_ = false;
        app::ai::ProviderEvent e;
        if (ok_) {
            e.type = app::ai::ProviderEvent::Type::Text;
            e.text = "La perte de communication peut expliquer l'alarme.\nCertitude : relation indirecte";
            out.push_back(e);
            e = {};
            e.type = app::ai::ProviderEvent::Type::Done;
            e.stopReason = "end_turn";
        } else {
            e.type = app::ai::ProviderEvent::Type::Error;
            e.text = "service injoignable (essai)";
        }
        out.push_back(e);
        return out;
    }
    void cancel() override {}
    bool busy() const noexcept override { return started_; }
    app::ai::ProviderRequest request_;

private:
    bool ok_;
    int* calls_;
    bool started_{false};
};

std::vector<std::uint8_t> frame(int w, int h, std::uint8_t shade) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, shade);
    for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    return px;
}

void host() {
    std::printf("D. G. l'hote\n");
    const fs::path proj = tempRoot("host");
    const Project p = station();
    bool remoteOk = false;
    int calls = 0;
    std::string analystWhy;
    bool analystReady = true;
    {
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        h.setContextSource([&]() -> std::optional<app::IncidentContext> {
            app::IncidentContext c;
            c.project = &p;
            c.projectName = p.config.name;
            c.projectVersion = p.config.version;
            c.openView = "Vue_Station";
            c.read = [](std::string_view path) -> std::optional<std::pair<std::string, std::string>> {
                if (path == "Pompe_2.Defaut") return std::make_pair(std::string("VRAI"), std::string("bonne"));
                return std::nullopt;
            };
            return c;
        });
        h.setAnalyst([&](std::string* why) -> std::optional<app::IncidentAnalyst> {
            if (!analystReady) {
                if (why) *why = "pas de cl\xC3\xA9 (essai)";
                return std::nullopt;
            }
            app::IncidentAnalyst a;
            a.provider = "anthropic";
            a.label = "Faux (modele-essai)";
            a.base.model = "modele-essai";
            return a;
        });
        h.setProviderFactory([&](const std::string&) { return std::make_unique<FakeProvider>(remoteOk, &calls); });
        inc::Settings s = h.settings();
        s.before = 2;
        s.after = 1;
        s.framesPerSecond = 4;
        check(h.setSettings(s), "les reglages enregistres dans le projet");
        check(fs::exists(proj / "ihm/ressources/enregistrements/reglages-incidents.json"), "les reglages : reglages-incidents.json");
        double t = 0;
        const auto run = [&](double until) {
            for (; t < until; t += 0.25) {
                h.tick(t);
                if (h.wantsFrame(t)) h.offerPixels(frame(32, 24, static_cast<std::uint8_t>(t * 10)), 32, 24, t);
                (void)h.drain(2000);
            }
        };
        run(3.0);
        check(h.bufferedFrames() >= 8 && h.bufferedFrames() <= 22, "D3 le tampon circulaire garde les secondes d'avant (borne)");
        // l'alarme : l'IA distante indisponible
        remoteOk = false;
        h.notice(notice("Apparition", "Defaut_Pompe_2", "2026-10-31 10:00:03.000", 1));
        check(h.wantsFrame(t), "D1 la capture du defaut est demandee aussitot");
        run(6.0);
        (void)h.drain(3000);
        h.tick(t);
        (void)h.drain(3000);
        h.tick(t + 0.01);
        (void)h.drain(3000);
        auto l = h.listing();
        check(l->pending.size() == 1, "D6 un incident en attente, cree sans geste");
        if (!l->pending.empty()) {
            const st::Item& it = l->pending[0];
            check(it.hasCapture && it.captureWidth == 32, "D1 la capture d'ecran du defaut");
            check(it.hasVideo && it.videoFrames >= 8, "D2 D3 la video : les secondes d'avant et d'apres (" + std::to_string(it.videoFrames) + " images)");
            check(!it.snapshot.values.empty() && it.snapshot.values[0].value == "VRAI", "D5 les valeurs au moment du defaut");
            check(it.snapshot.project == "Station_Essai" && it.snapshot.projectVersion == "2.4.1", "D5 le projet et sa version");
            check(calls == 1, "G1 l'IA distante a ete appelee une fois");
            check(it.analyses.size() == 2 && it.analyses[1].state == "echec" && has(it.analysisState(), "en attente"),
                  "G1 l'IA distante indisponible : l'incident garde, l'analyse en attente");
            check(h.verify(it.folder).ok, "D les empreintes du dossier");
            // une nouvelle analyse, l'IA revenue
            remoteOk = true;
            h.analyze(it.folder);
            h.tick(t);
            h.tick(t + 0.01);
            (void)h.drain(3000);
            h.tick(t + 0.02);
            (void)h.drain(3000);
            l = h.listing();
            const st::Item again = l->pending[0];
            check(again.analyses.size() == 3 && again.analyses[2].state == "faite" && again.certainty() == "relation indirecte" && again.analysisState() == "faite",
                  "C l'analyse IA en arriere-plan, ajoutee a part (la certitude lue)");
            check(has(again.analyses[0].text, "### 1. R"), "E l'analyse initiale (la fiche) intacte");
            // le cache : la meme demande au meme modele n'est pas renvoyee
            const int before = calls;
            h.analyze(again.folder);
            h.tick(t + 0.03);
            (void)h.drain(3000);
            check(calls == before, "G le cache : une analyse deja faite pour ce contexte n'est pas renvoyee");
            // G2 : l'IA locale (aucune IA prete)
            analystReady = false;
            h.analyze(again.folder);
            h.tick(t + 0.04);
            (void)h.drain(3000);
            h.tick(t + 0.05);
            (void)h.drain(3000);
            l = h.listing();
            check(l->pending[0].analyses.size() == 4 && l->pending[0].analyses[3].state == "echec" && has(l->pending[0].analyses[3].error, "pas de cl"),
                  "G2 aucune IA prete : la demande notee en attente, rien de perdu");
            analystReady = true;
            // commenter, valider (en arriere-plan)
            h.comment(again.folder, "Vu", "essai");
            h.validate(again.folder, "ok", "essai");
            (void)h.drain(3000);
            l = h.listing();
            check(l->pending.empty() && l->validated.size() == 1 && l->validated[0].comments.size() == 1, "F la validation depuis l'hote");
        }
        // G5 : plusieurs incidents simultanes
        remoteOk = true;
        for (const char* n : {"Niveau_Haut", "Niveau_Tres_Haut", "Defaut_Pompe_1", "Pression_Basse_Pompe_2"})
            h.notice(notice("Apparition", n, "2026-10-31 10:01:00.000"));
        run(t + 3.0);
        (void)h.drain(5000);
        for (int i = 0; i < 12; ++i) {
            h.tick(t + 0.01 * i);
            (void)h.drain(3000);
        }
        l = h.listing();
        check(l->pending.size() == 4, "G5 quatre alarmes simultanees : quatre incidents (" + std::to_string(l->pending.size()) + ")");
        std::set<std::string> folders;
        for (const auto& it : l->pending) folders.insert(it.folder);
        check(folders.size() == 4, "G5 quatre dossiers distincts");
        bool simul = false;
        for (const auto& it : l->pending)
            for (const auto& r : it.snapshot.related)
                if (r.kind == inc::Related::Kind::Shared || r.kind == inc::Related::Kind::Simultaneous) simul = true;
        check(simul, "G5 chaque incident voit les autres (liees ou simultanees)");
        check(h.bufferedBytes() < 48u * 1024u * 1024u, "G la memoire du tampon bornee");
        // G3 : le disque refuse, puis revient : l'incident gardé en memoire est reecrit
        h.storeForTests()->failWritesForTests([](const std::string& path) { return has(path, "incident.json"); });
        h.notice(notice("Disparition", "Defaut_Pompe_2", "x"));
        h.notice(notice("Apparition", "Defaut_Pompe_2", "2026-10-31 10:02:00.000"));
        run(t + 2.0);
        (void)h.drain(3000);
        const auto events = h.takeEvents();
        check(std::any_of(events.begin(), events.end(), [](const std::string& e) { return has(e, "non enregistr") && has(e, "r\xC3\xA9" "essay"); }),
              "G3 l'erreur disque dite, l'incident garde en memoire");
        h.storeForTests()->failWritesForTests(nullptr);
        const std::size_t beforeRetry = h.listing()->pending.size();
        h.tick(t + 31.0);
        (void)h.drain(3000);
        h.tick(t + 31.1);
        (void)h.drain(3000);
        check(h.listing()->pending.size() == beforeRetry + 1, "G3 le disque revenu : l'incident reessaye est ecrit");
        h.shutdown();
    }
    std::error_code ec;
    fs::remove_all(proj, ec);
}

// ---- G. le projet volumineux, beaucoup d'alarmes ----------------------------------------------------------------
void volume() {
    std::printf("G. le volume\n");
    Project p = station();
    for (int i = 0; i < 3000; ++i) {
        const std::string n = "Mesure_" + std::to_string(i);
        p.programs.variables.push_back(makeVar(p, n.c_str(), "REAL", i % 2 ? "Station_Modbus" : "Automate_Cuves", std::to_string(41000 + i * 2).c_str()));
    }
    for (int i = 0; i < 1500; ++i) {
        const std::string n = "Alarme_" + std::to_string(i), c = "Mesure_" + std::to_string(i * 2) + " > 50.0";
        p.alarms.push_back(makeAlarm(p, n.c_str(), c.c_str()));
    }
    for (int i = 0; i < 40; ++i) {
        Script s;
        s.id = p.allocate();
        s.name = "Script_" + std::to_string(i);
        for (int k = 0; k < 20; ++k) s.body += "Mesure_" + std::to_string(i * 20 + k) + " := Mesure_" + std::to_string(i * 20 + k + 1) + " + 1.0;\n";
        p.programs.scripts.push_back(s);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const rel::Graph g = rel::build(p);
    const auto t1 = std::chrono::steady_clock::now();
    inc::SnapshotInput in;
    in.project = &p;
    in.graph = &g;
    in.trigger.name = "Alarme_100";
    in.trigger.t = 1;
    double worst = 0;
    for (int i = 0; i < 50; ++i) {
        in.trigger.name = "Alarme_" + std::to_string(i * 30);
        const auto a = std::chrono::steady_clock::now();
        const inc::Snapshot s = inc::snapshot(in);
        worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
        if (i == 0) check(s.chain.size() <= 120 && !s.chain.empty(), "G6 la chaine bornee (" + std::to_string(s.chain.size()) + " maillons)");
    }
    const double graphMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("  graphe : %.0f ms (3000 variables, 1500 alarmes, 40 scripts) ; un contexte : %.1f ms au pire\n", graphMs, worst);
    check(worst < 200.0, "G6 le contexte d'un defaut sur un gros projet, le graphe deja fait : < 200 ms");
    // beaucoup d'alarmes : le detecteur ne lit pas le projet, il suit les fronts
    inc::Monitor m;
    const auto a = std::chrono::steady_clock::now();
    for (int cycle = 0; cycle < 100; ++cycle)
        for (int i = 0; i < 1500; ++i)
            m.notice(notice(cycle % 2 ? "Disparition" : "Apparition", ("Alarme_" + std::to_string(i)).c_str(), "x"), cycle * 10.0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    const auto trig = m.takeTriggers();
    check(trig.size() == 1500 * 50, "G7 150 000 avis : un incident par front d'apparition (" + std::to_string(trig.size()) + ")");
    std::printf("  detecteur : 150000 avis en %.0f ms\n", ms);
    check(ms < 3000.0, "G7 le detecteur reste leger");
    check(m.timeline(-1, 1e9).size() <= 2000, "G la chronologie gardee est bornee");
}

// ---- 1.20.2 : le volet - les dossiers par alarme, la recherche, la criticite, le tri ------------------------------
void incidentsPane() {
    std::printf("1.20.2 le volet : dossiers par alarme, recherche, criticite, tri\n");
    const fs::path proj = tempRoot("volet");
    const Project p = station();
    {
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        check(h.drain(3000), "V l'hote pret (la reprise, la premiere liste)");
        st::Store* store = h.storeForTests();
        check(store != nullptr, "V le stockage de l'hote");
        // A = Defaut_Pompe_2 (Critique) trois fois ; B = Niveau_Haut (Haute) une fois, le plus recent
        struct Def {
            const char* alarm;
            const char* at;
        };
        const Def defs[] = {{"Defaut_Pompe_2", "2026-10-31 10:00:00.000"},
                            {"Defaut_Pompe_2", "2026-10-31 10:05:00.000"},
                            {"Defaut_Pompe_2", "2026-10-31 10:10:00.000"},
                            {"Niveau_Haut", "2026-10-31 11:47:33.000"}};
        std::vector<std::string> names;   // les noms des dossiers, dans l'ordre de defs
        for (const auto& d : defs) {
            if (!store) break;
            inc::SnapshotInput in;
            in.project = &p;
            in.trigger.kind = inc::EventKind::Appear;
            in.trigger.name = d.alarm;
            in.trigger.at = d.at;
            in.openView = "Vue_Station";
            in.projectName = p.config.name;
            in.projectVersion = p.config.version;
            in.id = st::newId();
            in.createdAt = d.at;
            const inc::Snapshot s = inc::snapshot(in);
            std::string folder, why;
            check(store->create(s, {}, st::initialAnalysis(s, s.createdAt), folder, &why), std::string("V l'incident ") + d.alarm + " " + why);
            names.push_back(fs::path(folder).filename().string());
        }
        if (store && names.size() == 4) {
            st::Comment cm;
            cm.text = "Fuite_Vanne_Quatre constatee sur site";
            cm.author = "essai";
            std::string why;
            check(store->addComment((fs::path(store->pendingFolder()) / names[3]).string(), cm, &why), "V un commentaire sur B " + why);
        }
        auto doc = std::make_shared<hmi::Document>();
        app::HmiIncidentsPane pane("hmi.incidents", doc, &h);
        pane.setBounds({0, 0, 1400, 800});
        pane.layout();
        check(h.drain(3000), "V la relecture des dossiers");
        (void)pane.poll();
        pane.layout();
        check(pane.pendingCount() == 4 && names.size() == 4, "V quatre incidents en attente (" + std::to_string(pane.pendingCount()) + ")");
        if (names.size() == 4) {
            const auto groupRow = [&](std::size_t incidents) {
                for (std::size_t r = 0; r < pane.rowCount(); ++r)
                    if (pane.rowIsGroup(r) && pane.rowIncidents(r) == incidents) return static_cast<int>(r);
                return -1;
            };
            // 1. deux lignes : B seul (le plus recent d'abord), puis le dossier ferme de A et ses trois incidents
            check(pane.rowCount() == 2, "V1 deux lignes : un dossier pour A, une ligne pour B (" + std::to_string(pane.rowCount()) + ")");
            check(!pane.rowIsGroup(0) && has(pane.rowText(0), "Niveau_Haut"), "V1 B, le plus recent, en premier : une ligne simple");
            check(pane.rowIsGroup(1) && !pane.rowIsOpen(1) && pane.rowIncidents(1) == 3, "V1 le dossier de A : trois incidents, ferme au depart");
            check(has(pane.rowText(1), "Defaut_Pompe_2") && has(pane.rowText(1), "3 incidents") && has(pane.rowText(1), "Critique"),
                  "V1 le texte du dossier : l'alarme, << 3 incidents >>, la criticite (" + pane.rowText(1) + ")");
            check(has(pane.rowText(1), names[2]), "V1 le dossier montre le plus recent de A");
            check(pane.selectedRow() == 0 && pane.selectedName() == names[3], "V1 la premiere ligne choisie, comme avant");
            // 2. deplier : cinq lignes, les incidents de A en retrait, du plus recent au plus ancien ; B reste choisi
            pane.toggleGroup(1);
            check(pane.rowCount() == 5 && pane.rowIsOpen(1), "V2 deplie : cinq lignes");
            check(pane.rowDepth(0) == 0 && pane.rowDepth(1) == 0 && pane.rowDepth(2) == 1 && pane.rowDepth(4) == 1, "V2 les incidents du dossier en retrait");
            check(has(pane.rowText(2), names[2]) && has(pane.rowText(3), names[1]) && has(pane.rowText(4), names[0]), "V2 dans le dossier : le plus recent d'abord");
            check(has(pane.rowText(2), "10:10:00") && has(pane.rowText(4), "10:00:00"), "V2 les dates des incidents du dossier");
            check(pane.selectedRow() == 0 && pane.selectedName() == names[3], "V2 deplier ne change pas l'incident choisi");
            // 3. un incident du dossier choisi, puis replie : le dossier est choisi, l'incident reste celui des gestes
            check(pane.select(names[1]) && pane.selectedRow() == 3, "V3 choisir le deuxieme incident de A");
            pane.toggleGroup(1);
            check(pane.rowCount() == 2 && pane.selectedRow() == 1 && pane.selectedName() == names[1], "V3 replie : le dossier choisi, l'incident garde");
            pane.toggleGroup(1);
            check(pane.selectedRow() == 3 && pane.selectedName() == names[1], "V3 deplie de nouveau : l'incident rechoisi");
            pane.toggleGroup(1);
            check(pane.select(names[0]) && pane.selectedRow() == 1 && pane.selectedName() == names[0] && !pane.rowIsOpen(1),
                  "V3 choisir un incident d'un dossier ferme : le dossier est choisi, sans le deplier");
            // 4. la recherche : un mot du commentaire de B ; la date affichee ; l'alarme
            pane.search().setText("fuite_vanne");
            check(pane.rowCount() == 1 && !pane.rowIsGroup(0) && has(pane.rowText(0), "Niveau_Haut") && pane.shownCount() == 1,
                  "V4 la recherche dans les commentaires : B seul");
            check(pane.search().countText() == "1 sur 4", "V4 le compte de la recherche (" + pane.search().countText() + ")");
            check(pane.selectedName() == names[3], "V4 cache par la recherche : la ligne restante est choisie");
            pane.search().setText("31/10/2026 10:05");
            check(pane.rowCount() == 1 && !pane.rowIsGroup(0) && has(pane.rowText(0), names[1]),
                  "V4 la date affichee : un seul incident de A retenu, une ligne simple (pas de dossier pour un seul)");
            pane.search().setText("Defaut_Pompe_2");
            check(groupRow(3) >= 0 && pane.shownCount() >= 3, "V4 l'alarme : ses trois incidents, en dossier");
            pane.search().setText("Defaut_Pompe_2 -10:05");
            check(groupRow(2) >= 0 && groupRow(3) < 0, "V4 un mot exclu : le dossier de A n'a plus que deux incidents");
            pane.search().setText("");
            check(pane.rowCount() == 2 && pane.search().countText().empty(), "V4 la recherche effacee : tout revient");
            // 5. la criticite
            check(pane.select(names[3]), "V5 B choisi");
            pane.criticality().setSelectedIndex(1);
            check(pane.rowCount() == 1 && pane.rowIsGroup(0) && pane.rowIncidents(0) == 3 && pane.shownCount() == 3, "V5 Critique : le dossier de A seul");
            pane.criticality().setSelectedIndex(2);
            check(pane.rowCount() == 1 && !pane.rowIsGroup(0) && has(pane.rowText(0), "Niveau_Haut"), "V5 Haute : B seul");
            pane.criticality().setSelectedIndex(4);
            check(pane.rowCount() == 0 && pane.selected() == nullptr && pane.shownCount() == 0, "V5 Basse : rien");
            check(pane.pendingCount() == 4, "V5 le badge compte tous les incidents (les filtres n'y font rien)");
            pane.criticality().setSelectedIndex(0);
            check(pane.rowCount() == 2 && pane.selectedName() == names[3], "V5 toutes : tout revient, le choix d'avant aussi");
            // 6. le tri
            pane.sortOrder().setSelectedIndex(app::HmiIncidentsPane::SortCriticality);
            check(pane.rowIsGroup(0) && has(pane.rowText(0), "Defaut_Pompe_2") && has(pane.rowText(1), "Niveau_Haut"), "V6 Criticite : le dossier Critique d'abord");
            check(pane.selectedName() == names[3] && pane.selectedRow() == 1, "V6 le tri garde l'incident choisi");
            pane.sortOrder().setSelectedIndex(app::HmiIncidentsPane::SortCount);
            check(pane.rowIsGroup(0) && pane.rowIncidents(0) == 3 && pane.rowIncidents(1) == 1, "V6 Nombre d'incidents : le dossier de trois d'abord");
            pane.sortOrder().setSelectedIndex(app::HmiIncidentsPane::SortAlarm);
            check(has(pane.rowText(0), "Defaut_Pompe_2") && has(pane.rowText(1), "Niveau_Haut"), "V6 Alarme : de A a Z");
            pane.sortOrder().setSelectedIndex(app::HmiIncidentsPane::SortOldest);
            check(pane.rowIsGroup(0) && has(pane.rowText(1), "Niveau_Haut"), "V6 Plus anciens : le dossier (dernier a 10:10) avant B (11:47)");
            pane.toggleGroup(0);
            check(pane.rowCount() == 5 && has(pane.rowText(1), names[2]) && has(pane.rowText(3), names[0]) && has(pane.rowText(4), "Niveau_Haut"),
                  "V6 dans un dossier, le plus recent d'abord, quel que soit le tri");
            pane.sortOrder().setSelectedIndex(app::HmiIncidentsPane::SortRecent);
            check(has(pane.rowText(0), "Niveau_Haut") && pane.rowIsGroup(1) && pane.rowIsOpen(1), "V6 Plus recents : B d'abord ; le dossier reste deplie");
            pane.toggleGroup(1);
            // 7. une relecture garde les dossiers et le choix ; les gestes d'un dossier portent sur l'incident qu'il montre
            check(pane.select(names[2]) && pane.selectedRow() == 1, "V7 le plus recent de A : son dossier ferme choisi");
            h.comment((fs::path(store->pendingFolder()) / names[0]).string(), "Vu", "essai");
            check(h.drain(3000) && pane.poll(), "V7 une relecture (un commentaire ajoute)");
            check(pane.rowCount() == 2 && pane.selectedRow() == 1 && pane.selectedName() == names[2], "V7 apres la relecture : le dossier ferme, le meme incident");
            std::string why;
            check(pane.removeSelected(&why), "V7 supprimer depuis le dossier : " + why);
            check(h.drain(3000) && pane.poll(), "V7 la relecture apres la suppression");
            check(pane.pendingCount() == 3 && pane.rowCount() == 2 && pane.rowIsGroup(1) && pane.rowIncidents(1) == 2,
                  "V7 un seul incident supprime (celui que le dossier montrait), pas tout le dossier");
            check(pane.selectedRow() == 1 && pane.selectedName() == names[1], "V7 le dossier reste choisi : son plus recent desormais");
            // 8. 1.20.2 : l'interrupteur general - arreter la creation des incidents, puis la reprendre (garde avec le projet)
            check(pane.incidentsEnabled() && pane.setIncidentsEnabled(false) && !pane.incidentsEnabled() && !h.settings().enabled,
                  "V8 l'interrupteur : la creation des incidents arretee");
            check(fs::exists(fs::path(proj) / "ihm" / "ressources" / "enregistrements" / "reglages-incidents.json"),
                  "V8 l'arret est garde avec le projet (reglages-incidents.json)");
            check(pane.setIncidentsEnabled(true) && pane.incidentsEnabled() && h.settings().enabled, "V8 un second clic la reprend");
        }
        h.shutdown();
    }
    std::error_code ec;
    fs::remove_all(proj, ec);
}

} // namespace

// ---- 1.20.2 : la case Incidents des alarmes (et de leurs groupes), l'interrupteur general -------------------------
void incidentsCase() {
    std::printf("1.20.2 : la case Incidents des alarmes, l'interrupteur general\n");
    const fs::path proj = tempRoot("case");
    Project p = station();
    for (auto& a : p.alarms)
        if (a.name == "Niveau_Haut") a.incidents = false;   // la case Incidents decochee
    AlarmGroupDef g;
    g.id = p.allocate();
    g.name = "Zone_Objets";
    g.incidents = false;                                   // ses alarmes d'objets : pas d'incident
    p.alarmGroups.push_back(g);
    const auto objectNotice = [](const char* name, const char* group) {
        AlarmNotice n = notice("Apparition", name, "2026-10-31 11:00:00.000");
        n.group = group;
        return n;
    };
    {
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        h.setContextSource([&]() -> std::optional<app::IncidentContext> {
            app::IncidentContext c;
            c.project = &p;
            c.projectName = p.config.name;
            return c;
        });
        double t = 0;
        const auto run = [&](double until) {
            for (; t < until; t += 0.25) {
                h.tick(t);
                if (h.wantsFrame(t)) h.offerPixels(frame(32, 24, 60), 32, 24, t);
                (void)h.drain(2000);
            }
        };
        run(1.0);
        check(h.enabled(), "1.20.2 la surveillance en marche par defaut");
        h.notice(notice("Apparition", "Niveau_Haut", "2026-10-31 11:00:00.000"));
        run(t + 3.0);
        check(h.created() == 0, "1.20.2 une alarme dont la case Incidents est decochee : aucun incident");
        h.notice(notice("Apparition", "Defaut_Pompe_1", "2026-10-31 11:00:05.000"));
        run(t + 3.0);
        check(h.created() == 1, "1.20.2 cochee (par defaut) : un incident (" + std::to_string(h.created()) + ")");
        h.notice(objectNotice("Vue_Station.Voyant_P2.Defaut", "Zone_Objets"));
        run(t + 3.0);
        check(h.created() == 1, "1.20.2 une alarme d'objet de Zone_Objets (case du groupe decochee) : aucun incident");
        h.notice(objectNotice("Vue_Station.Voyant_P2.Fuite", "Autre"));
        run(t + 3.0);
        check(h.created() == 2, "1.20.2 une alarme d'objet d'un autre groupe : un incident");
        // l'interrupteur general
        check(h.setEnabled(false) && !h.enabled() && !h.settings().enabled, "1.20.2 arreter la surveillance (enregistre)");
        h.notice(notice("Apparition", "Niveau_Tres_Haut", "2026-10-31 11:00:20.000"));
        check(!h.wantsFrame(t), "1.20.2 arretee : aucune image demandee");
        run(t + 3.0);
        check(h.created() == 2 && h.bufferedFrames() == 0, "1.20.2 arretee : aucun incident, le tampon des images rendu");
        check(h.setEnabled(true) && h.enabled(), "1.20.2 remise en marche");
        h.notice(notice("Apparition", "Niveau_Tres_Haut", "2026-10-31 11:00:30.000"));
        run(t + 3.0);
        check(h.created() == 3, "1.20.2 en marche : un incident de nouveau (" + std::to_string(h.created()) + ")");
        check(h.setEnabled(false), "1.20.2 arretee de nouveau");
        (void)h.drain(3000);
    }
    {
        app::IncidentHost h2;
        h2.setProjectFolder(proj.string());
        check(!h2.enabled(), "1.20.2 l'arret relu du projet (reglages-incidents.json, cle active)");
    }
}

// ---- 1.21.0 : UNE ALARME QUI BAT - les reapparitions regroupees, le plafond horaire, le quota disque ----------------
namespace {

// L'horodatage du moteur pour l'instant simule `t` (secondes depuis le 31/10/2026 a 00:00).
std::string stampAt(double t) {
    const long long ms = std::llround(t * 1000.0), sec = ms / 1000, day = sec / 86400;
    char buf[64];
    std::snprintf(buf, sizeof buf, "2026-%s-%02lld %02lld:%02lld:%02lld.%03lld", day == 0 ? "10" : "11", day == 0 ? 31LL : day, (sec % 86400) / 3600,
                  (sec % 3600) / 60, sec % 60, ms % 1000);
    return buf;
}

// Un fournisseur d'IA qui garde le texte de la demande (le contexte envoye).
class SeeingProvider : public app::ai::Provider {
public:
    explicit SeeingProvider(std::string* seen) : seen_(seen) {}
    std::string_view key() const noexcept override { return "faux"; }
    bool start(app::ai::ProviderRequest r, std::string*) override {
        if (!r.turns.empty()) *seen_ = r.turns.back().text;
        started_ = true;
        return true;
    }
    std::vector<app::ai::ProviderEvent> poll() override {
        std::vector<app::ai::ProviderEvent> out;
        if (!started_) return out;
        started_ = false;
        app::ai::ProviderEvent e;
        e.type = app::ai::ProviderEvent::Type::Text;
        e.text = "L'alarme bat autour de son seuil.\nCertitude : hypoth\xC3\xA8se";
        out.push_back(e);
        e = {};
        e.type = app::ai::ProviderEvent::Type::Done;
        e.stopReason = "end_turn";
        out.push_back(e);
        return out;
    }
    void cancel() override {}
    bool busy() const noexcept override { return started_; }

private:
    std::string* seen_;
    bool         started_{false};
};

bool eventsHave(const std::vector<std::string>& events, const std::string& part) {
    return std::any_of(events.begin(), events.end(), [&](const std::string& e) { return has(e, part); });
}

const st::Item* itemNamed(const std::vector<st::Item>& list, const std::string& alarm) {
    for (const auto& it : list)
        if (it.snapshot.trigger.name == alarm) return &it;
    return nullptr;
}

} // namespace

// R : 300 000 reapparitions toutes les 2 s (temps simule) - un seul incident, le compte exact, la memoire et le fichier
// bornes, les histogrammes justes ; calme plus que la fenetre : un deuxieme incident ; valide : la rafale close.
void burstHost() {
    std::printf("1.21.0 : une alarme qui bat - 300 000 reapparitions, un seul incident\n");
    const fs::path proj = tempRoot("rafale");
    const Project p = station();
    {
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        h.setContextSource([&]() -> std::optional<app::IncidentContext> {
            app::IncidentContext c;
            c.project = &p;
            c.projectName = p.config.name;
            c.read = [](std::string_view path) -> std::optional<std::pair<std::string, std::string>> {
                if (path == "Pompe_2.Defaut") return std::make_pair(std::string("VRAI"), std::string("bonne"));
                return std::nullopt;
            };
            return c;
        });
        const inc::Settings s0 = h.settings();
        check(s0.regroup == 600.0 && s0.maxPerHour == 60 && s0.mediaQuotaMb == 2048 && s0.debounce == 5.0,
              "R0 par defaut : regroupement 600 s, 60 nouveaux incidents par heure, 2048 Mo, l'anti-rebond 5 s inchange");
        double t = 0;
        const auto at = [&](double when) {
            t = when;
            h.tick(t);
        };
        const auto front = [&](const char* kind, double when) {
            at(when);
            h.notice(notice(kind, "Defaut_Pompe_2", stampAt(when).c_str()));
        };
        // l'incident : l'apparition (pas de vue en marche : il se cree sans image apres une seconde)
        front("Apparition", 100.0);
        front("Disparition", 100.5);
        at(101.6);
        check(h.drain(5000), "R l'ecriture du premier incident");
        at(101.7);
        auto l = h.listing();
        check(h.created() == 1 && l->pending.size() == 1 && h.liveGroups() == 1, "R1 la premiere apparition : un incident, sa rafale ouverte");
        const std::string id = l->pending.empty() ? std::string{} : l->pending[0].snapshot.id;
        const std::string folder = l->pending.empty() ? std::string{} : l->pending[0].folder;
        // 300 000 reapparitions, toutes les 2 s (active 0,5 s chacune) : pres de 7 jours simules
        constexpr int kN = 300000;
        std::map<std::int64_t, std::uint32_t> perMinute, perHour;
        std::size_t peak = 0;
        const auto c0 = std::chrono::steady_clock::now();
        for (int i = 1; i <= kN; ++i) {
            const double ti = 100.0 + 2.0 * i;
            front("Apparition", ti);
            front("Disparition", ti + 0.5);
            ++perMinute[static_cast<std::int64_t>(std::floor(ti / 60.0))];
            ++perHour[static_cast<std::int64_t>(std::floor(ti / 3600.0))];
            if (i % 25000 == 0)
                if (const auto* live = h.reappearances(id)) peak = std::max(peak, live->memoryBytes());
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
        std::printf("  %d reapparitions (600 000 avis, 600 000 images du moteur) en %.0f ms\n", kN, ms);
        const double last = 100.0 + 2.0 * kN;
        check(h.created() == 1 && h.listing()->pending.size() == 1, "R2 300 000 reapparitions toutes les 2 s : UN seul incident, un seul dossier");
        const inc::Burst* b = h.reappearances(id);
        check(b != nullptr, "R2 la rafale en cours, trouvee par l'incident");
        if (b) {
            check(b->count == 300000, "R2 le compte exact : 300000 (" + std::to_string(b->count) + ")");
            check(b->firstT == 102.0 && b->lastT == last && b->firstAt.view() == stampAt(102.0) && b->lastAt.view() == stampAt(last),
                  "R2 la premiere et la derniere (" + b->firstAt.str() + ", " + b->lastAt.str() + ")");
            check(b->minGap == 2.0 && b->maxGap == 2.0 && b->meanGap() == 2.0, "R2 l'intervalle min, moyen, max : 2 s");
            check(b->activeCount == static_cast<std::uint64_t>(kN) + 1 && std::fabs(b->meanActive() - 0.5) < 1e-9 && b->activeMax == 0.5,
                  "R2 la duree active a chaque disparition (0,5 s ; l'incident compris)");
            const auto recent = b->recent();
            check(recent.size() == inc::Burst::kRecent && recent.back().t == last && recent.front().t == last - 2.0 * (inc::Burst::kRecent - 1),
                  "R2 les 200 dernieres, dans l'ordre");
            check(recent.back().value.view() == "VRAI" && recent.back().active == 0.5f && recent.back().at.view() == stampAt(last),
                  "R2 le detail : l'heure, la duree, la valeur lue au meme cycle");
            // la memoire : bornee, la meme au debut et a la fin
            check(b->memoryBytes() <= inc::Burst::kMaxBytes && peak == b->memoryBytes() && b->ring.capacity() == inc::Burst::kRecent,
                  "R3 la memoire de la rafale bornee : " + std::to_string(b->memoryBytes()) + " octets, au plus " + std::to_string(inc::Burst::kMaxBytes)
                      + " (la meme a 25 000 et a 300 000)");
            check(sizeof(inc::Reappearance) <= 64 && sizeof(inc::Burst) <= 1024,
                  "R3 la taille des structures : " + std::to_string(sizeof(inc::Reappearance)) + " octets par reapparition detaillee, "
                      + std::to_string(sizeof(inc::Burst)) + " pour la rafale");
            check(h.liveGroups() == 1 && h.monitor().timeline(-1, 1e12).size() <= 2000, "R3 une rafale en memoire ; la chronologie du detecteur bornee");
            // les histogrammes justes
            bool minutesOk = b->lastMinute == static_cast<std::int64_t>(std::floor(last / 60.0));
            for (int k = 0; k < inc::Burst::kMinutes; ++k) {
                const auto it = perMinute.find(b->lastMinute - (inc::Burst::kMinutes - 1) + k);
                minutesOk = minutesOk && b->minutes[static_cast<std::size_t>(k)] == (it == perMinute.end() ? 0u : it->second);
            }
            bool hoursOk = b->lastHour == static_cast<std::int64_t>(std::floor(last / 3600.0));
            for (int k = 0; k < inc::Burst::kHours; ++k) {
                const auto it = perHour.find(b->lastHour - (inc::Burst::kHours - 1) + k);
                hoursOk = hoursOk && b->hours[static_cast<std::size_t>(k)] == (it == perHour.end() ? 0u : it->second);
            }
            check(minutesOk && b->minutes.back() > 0, "R4 l'histogramme par minute (la derniere heure) juste");
            check(hoursOk && b->hours[0] == 1800, "R4 l'histogramme par heure (24 h) juste (1800 par heure pleine)");
            // le diagnostic en clair, et le contexte de l'IA (borne)
            const std::string diag = inc::burstDiagnosis(*b);
            check(has(diag, "instable") && has(diag, "toutes les 2") && has(diag, "hyst\xC3\xA9r\xC3\xA9sis") && has(diag, "temporisation")
                      && has(diag, "contact") && has(diag, "pas des causes"),
                  "R4 le diagnostic : instable, la frequence, les pistes (sans cause affirmee)");
            check(inc::burstContext(*b, 600).size() <= 640, "R4 le resume pour l'IA est borne");
        }
        // le fichier : ecrit (au plus toutes les 5 s, une seule ecriture en attente), borne, relu juste, hors empreinte
        at(last + 6.0);
        check(h.drain(20000), "R5 les ecritures finies");
        const std::string text = readText((fs::path(folder) / "reapparitions.json").string());
        check(!text.empty() && text.size() < 32u * 1024u, "R5 reapparitions.json borne : " + std::to_string(text.size()) + " octets");
        app::ai::json::Value doc;
        inc::Burst back;
        check(app::ai::json::parse(text, doc) && st::fromJson(doc, back), "R5 reapparitions.json relu");
        if (b) {
            check(back.count == b->count && back.firstT == b->firstT && back.lastT == b->lastT && back.minGap == b->minGap && back.maxGap == b->maxGap
                      && back.activeCount == b->activeCount,
                  "R5 relu juste : le compte, la premiere, la derniere, les intervalles, les durees");
            check(back.minutes == b->minutes && back.hours == b->hours && back.lastMinute == b->lastMinute && back.lastHour == b->lastHour,
                  "R5 relu juste : les histogrammes");
            const auto r1 = back.recent(), r2 = b->recent();
            check(r1.size() == r2.size() && !r1.empty() && r1.front().t == r2.front().t && r1.back().t == r2.back().t
                      && r1.back().at.view() == r2.back().at.view() && r1.back().value.view() == "VRAI" && r1.back().active == 0.5f,
                  "R5 relu juste : les 200 dernieres");
        }
        check(h.verify(folder).ok, "R5 reapparitions.json est hors empreinte : le dossier reste intact (verifie)");
        // le volet : la ligne le dit
        {
            auto vdoc = std::make_shared<hmi::Document>();
            app::HmiIncidentsPane pane("hmi.incidents", vdoc, &h);
            pane.setBounds({0, 0, 1400, 800});
            pane.layout();
            (void)h.drain(3000);
            (void)pane.poll();
            check(pane.rowCount() == 1 && has(pane.rowText(0), "300\xC2\xA0" "000 r\xC3\xA9" "apparitions"),
                  "R6 le volet : la ligne de l'incident dit ses 300 000 reapparitions (" + (pane.rowCount() ? pane.rowText(0) : std::string{}) + ")");
            pane.sortOrder().setSelectedIndex(app::HmiIncidentsPane::SortReappear);
            check(pane.rowCount() == 1, "R6 le tri par nombre de reapparitions");
        }
        // l'analyse demandee ensuite : le resume des reapparitions dans son contexte
        std::string seen;
        h.setAnalyst([](std::string*) -> std::optional<app::IncidentAnalyst> {
            app::IncidentAnalyst a;
            a.provider = "anthropic";
            a.label = "Faux (modele-essai)";
            a.base.model = "modele-essai";
            return a;
        });
        h.setProviderFactory([&seen](const std::string&) { return std::make_unique<SeeingProvider>(&seen); });
        h.analyze(folder);
        at(t + 0.1);
        at(t + 0.1);
        (void)h.drain(3000);
        check(has(seen, "R\xC3\xA9" "apparitions regroup") && has(seen, "nombre : 300000"), "R6 << Analyser >> : le resume des reapparitions part avec la fiche");
        h.setAnalyst(nullptr);
        // calme plus longtemps que la fenetre : la rafale est ecrite une derniere fois et oubliee ; puis un DEUXIEME incident
        at(last + 0.5 + 601.0);
        check(h.liveGroups() == 0 && h.reappearances(id) == nullptr, "R7 restee calme plus de 600 s : la rafale close");
        (void)h.drain(5000);
        const double t2 = t + 10.0;
        front("Apparition", t2);
        at(t2 + 1.2);
        (void)h.drain(5000);
        at(t2 + 1.3);
        l = h.listing();
        check(h.created() == 2 && l->pending.size() == 2, "R7 la reapparition apres la fenetre : un deuxieme incident");
        std::string id2, folder2;
        for (const auto& it : l->pending) {
            if (it.snapshot.id == id)
                check(it.reappearances && it.reappearances->count == 300000, "R7 le premier garde ses 300 000 reapparitions (relues du dossier)");
            else {
                id2 = it.snapshot.id;
                folder2 = it.folder;
            }
        }
        // trois reapparitions, puis VALIDER : la rafale close, la copie validee la garde ; la suivante ouvre un incident
        front("Disparition", t2 + 1.5);
        for (int k = 1; k <= 3; ++k) {
            front("Apparition", t2 + 2.0 * k);
            front("Disparition", t2 + 2.0 * k + 0.5);
        }
        const inc::Burst* b2 = h.reappearances(id2);
        check(b2 && b2->count == 3 && h.created() == 2, "R8 trois reapparitions : comptees dans le deuxieme incident");
        h.validate(folder2, "vu", "essai");
        (void)h.drain(5000);
        at(t2 + 7.0);
        check(h.reappearances(id2) == nullptr, "R8 valide : sa rafale est close");
        l = h.listing();
        const st::Item* v = l->validated.empty() ? nullptr : &l->validated[0];
        check(v && v->reappearances && v->reappearances->count == 3 && h.verify(v->folder).ok,
              "R8 la copie validee garde ses reapparitions (hors empreinte : verifiee intacte)");
        front("Apparition", t2 + 12.0);
        at(t2 + 13.2);
        (void)h.drain(5000);
        at(t2 + 13.3);
        check(h.created() == 3, "R8 une reapparition apres la validation : un NOUVEL incident (" + std::to_string(h.created()) + ")");
        h.shutdown();
    }
    // 30 millions de reapparitions : la meme taille
    {
        inc::Burst big;
        big.start(0.0, "2026-10-31 00:00:00.000");
        std::size_t after300k = 0;
        for (int i = 1; i <= 30000000; ++i) {
            big.add(0.1 * i, "2026-10-31 00:00:00.000", "1,49");
            if (i == 300000) after300k = big.memoryBytes();
        }
        check(big.count == 30000000 && big.memoryBytes() == after300k && big.memoryBytes() <= inc::Burst::kMaxBytes,
              "R3 30 millions de reapparitions : la meme taille qu'a 300 000 (" + std::to_string(big.memoryBytes()) + " octets)");
    }
    std::error_code ec;
    fs::remove_all(proj, ec);
}

// G : regroupement 0 (le comportement d'avant), l'anti-rebond inchange, le plafond horaire, le quota disque, les reglages.
void burstGuards() {
    std::printf("1.21.0 : regroupement 0, anti-rebond, plafond horaire, quota disque, reglages\n");
    // le meme scenario, regroupement 0 puis 600 s : apparitions a 10, 20 (apres une disparition), 22 (dans l'anti-rebond de 20), 30
    for (const double regroup : {0.0, 600.0}) {
        const fs::path proj = tempRoot(regroup > 0 ? "rafale600" : "rafale0");
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        inc::Settings s = h.settings();
        s.regroup = regroup;
        check(h.setSettings(s), "G les reglages");
        const auto front = [&](const char* kind, double when) {
            h.tick(when);
            h.notice(notice(kind, "Niveau_Haut", stampAt(when).c_str()));
        };
        const auto settle = [&](double when) {
            h.tick(when);
            (void)h.drain(3000);
            h.tick(when + 0.01);
        };
        front("Apparition", 10.0);
        front("Disparition", 10.5);
        settle(11.2);
        front("Apparition", 20.0);
        front("Disparition", 20.5);
        settle(21.2);
        const std::size_t afterSecond = h.created();
        front("Apparition", 22.0);   // 2 s apres celle de 20 : dans l'anti-rebond (5 s)
        front("Disparition", 22.5);
        settle(23.2);
        const std::size_t afterDebounce = h.created();
        front("Apparition", 30.0);
        settle(31.2);
        if (regroup <= 0) {
            check(afterSecond == 2 && afterDebounce == 2 && h.created() == 3,
                  "G1 regroupement 0 : comme avant - un incident par reapparition hors anti-rebond (3), aucun dans l'anti-rebond");
            check(h.liveGroups() == 0, "G1 regroupement 0 : aucune rafale");
            bool none = true;
            for (const auto& it : h.listing()->pending) none = none && !fs::exists(fs::path(it.folder) / "reapparitions.json");
            check(none, "G1 regroupement 0 : aucun reapparitions.json");
        } else {
            const auto l = h.listing();
            const std::string id = l->pending.empty() ? std::string{} : l->pending[0].snapshot.id;
            const inc::Burst* b = h.reappearances(id);
            check(h.created() == 1 && b && b->count == 3,
                  "G2 regroupement 600 s, le meme scenario : un incident, 3 reapparitions (celle de l'anti-rebond comptee aussi)");
        }
        h.shutdown();
        std::error_code ec;
        fs::remove_all(proj, ec);
    }
    // le detecteur seul : l'anti-rebond inchange (une reapparition dans le delai ne demande pas d'incident)
    {
        inc::Monitor m;
        m.notice(notice("Apparition", "X", "a"), 0.0);
        m.notice(notice("Disparition", "X", "b"), 1.0);
        m.notice(notice("Apparition", "X", "c"), 2.0);
        check(m.takeTriggers().size() == 1 && m.settings().debounce == 5.0, "G3 l'anti-rebond du detecteur inchange (5 s)");
    }
    // le plafond horaire : 3 par heure, 5 alarmes -> 3 incidents, 2 comptes sans dossier ; une heure plus tard, de nouveau
    {
        const fs::path proj = tempRoot("plafond");
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        inc::Settings s = h.settings();
        s.maxPerHour = 3;
        check(h.setSettings(s), "P les reglages");
        double t = 5.0;
        h.tick(t);
        for (int i = 1; i <= 5; ++i) h.notice(notice("Apparition", ("Alarme_" + std::to_string(i)).c_str(), stampAt(t).c_str()));
        h.tick(t + 1.2);
        (void)h.drain(3000);
        h.tick(t + 1.3);
        auto g = h.guards();
        check(h.created() == 3 && h.listing()->pending.size() == 3, "P1 plafond de 3 par heure, 5 alarmes : 3 incidents, 3 dossiers");
        check(g.skipped == 2 && g.skippedNow == 2 && g.capReached && g.createdLastHour == 3 && g.skippedBy.size() == 2,
              "P1 les 2 autres : comptees, sans dossier (par alarme)");
        auto events = h.takeEvents();
        check(eventsHave(events, "plafond horaire atteint"), "P2 le plafond dit au journal de l'hote");
        t += 3601.0;
        h.tick(t);
        h.notice(notice("Apparition", "Alarme_6", stampAt(t).c_str()));
        h.tick(t + 1.2);
        (void)h.drain(3000);
        h.tick(t + 1.3);
        g = h.guards();
        events = h.takeEvents();
        check(h.created() == 4 && !g.capReached && g.skippedNow == 0 && g.skipped == 2, "P3 une heure plus tard : de nouveau un incident");
        check(eventsHave(events, "2 incidents non cr\xC3\xA9\xC3\xA9s : plafond horaire atteint"), "P3 << 2 incidents non crees : plafond horaire atteint >>, dit");
        h.shutdown();
        std::error_code ec;
        fs::remove_all(proj, ec);
    }
    // le quota disque des images et videos : au-dela, les NOUVEAUX incidents sans image ni video ; rien de supprime
    {
        const fs::path proj = tempRoot("quota");
        app::IncidentHost h;
        h.setProjectFolder(proj.string());
        check(h.drain(3000), "Q l'hote pret");
        st::Store* store = h.storeForTests();
        std::string oldFolder, why;
        if (store) {
            inc::SnapshotInput in;
            in.trigger.kind = inc::EventKind::Appear;
            in.trigger.name = "Ancien_Defaut";
            in.trigger.at = "2026-10-30 08:00:00.000";
            in.id = st::newId();
            in.createdAt = in.trigger.at;
            const inc::Snapshot old = inc::snapshot(in);
            st::Capture big;
            big.jpeg.assign(1536u * 1024u, 0x55);   // 1,5 Mo (le stockage ne la decode pas)
            big.width = 10;
            big.height = 10;
            check(store->create(old, big, st::initialAnalysis(old, old.createdAt), oldFolder, &why), "Q un incident deja la, sa capture de 1,5 Mo " + why);
        }
        inc::Settings s = h.settings();
        s.mediaQuotaMb = 1;
        s.framesPerSecond = 4;
        check(h.setSettings(s), "Q le quota : 1 Mo");
        h.refresh();
        (void)h.drain(3000);
        double t = 1.0;
        h.tick(t);
        auto g = h.guards();
        check(g.mediaBytes >= 1536u * 1024u && g.quotaBytes == 1024u * 1024u && g.quotaReached, "Q1 la place connue (relue avec la liste) : le quota est atteint");
        check(!h.wantsFrame(t), "Q1 quota atteint : aucune image a prendre (elle ne serait pas gardee)");
        h.notice(notice("Apparition", "Nouveau_Defaut", stampAt(t).c_str()));
        check(!h.wantsFrame(t), "Q1 ... meme pour le defaut");
        h.tick(t + 0.1);
        (void)h.drain(3000);
        h.tick(t + 0.2);
        const auto l = h.listing();
        const st::Item* it = itemNamed(l->pending, "Nouveau_Defaut");
        check(h.created() == 1 && it != nullptr, "Q2 l'incident est cree quand meme (sans attendre d'image)");
        if (it) {
            check(!it->hasCapture && !it->hasVideo && has(it->captureNote, "quota") && it->mediaBytes == 0, "Q2 SANS image ni video, et c'est dit");
            check(std::any_of(it->snapshot.missing.begin(), it->snapshot.missing.end(), [](const std::string& m) { return has(m, "quota"); }),
                  "Q2 la fiche le dit aussi (non disponible)");
        }
        check(!oldFolder.empty() && fs::exists(oldFolder) && itemNamed(l->pending, "Ancien_Defaut") != nullptr, "Q3 rien n'est supprime automatiquement");
        check(eventsHave(h.takeEvents(), "quota disque"), "Q4 le quota dit au journal de l'hote");
        g = h.guards();
        check(g.withoutMedia == 1, "Q4 compte : un incident cree sans image ni video");
        h.shutdown();
        std::error_code ec;
        fs::remove_all(proj, ec);
    }
    // les reglages : gardes avec le projet ; un ancien fichier sans les cles : les valeurs par defaut
    {
        const fs::path proj = tempRoot("reglages121");
        {
            app::IncidentHost h;
            h.setProjectFolder(proj.string());
            inc::Settings s = h.settings();
            s.regroup = 120;
            s.maxPerHour = 10;
            s.mediaQuotaMb = 500;
            check(h.setSettings(s), "S les reglages enregistres");
        }
        {
            app::IncidentHost h;
            h.setProjectFolder(proj.string());
            check(h.settings().regroup == 120 && h.settings().maxPerHour == 10 && h.settings().mediaQuotaMb == 500, "S relus : regroupement, plafond, quota");
        }
        (void)core::writeFileAtomic(proj / "ihm" / "ressources" / "enregistrements" / "reglages-incidents.json",
                                    "{\"format\": 1, \"active\": true, \"anti_rebond_s\": 7}", core::AtomicWrite{false, false});
        {
            app::IncidentHost h;
            h.setProjectFolder(proj.string());
            check(h.settings().debounce == 7 && h.settings().regroup == 600 && h.settings().maxPerHour == 60 && h.settings().mediaQuotaMb == 2048,
                  "S un ancien reglages-incidents.json (sans les cles) : les valeurs par defaut");
        }
        // le dialogue des reglages : trois champs de plus
        inc::Settings in;
        in.regroup = 90;
        in.maxPerHour = 7;
        in.mediaQuotaMb = 100;
        const auto values = app::incidentsui::settingsValues(in);
        inc::Settings out;
        std::string why;
        check(app::incidentsui::settingsLabels().size() == app::incidentsui::kSettingsFields && values.size() == app::incidentsui::kSettingsFields
                  && app::incidentsui::settingsFrom(values, out, &why) && out.regroup == 90 && out.maxPerHour == 7 && out.mediaQuotaMb == 100,
              "S le dialogue : regroupement, plafond horaire, quota (aller-retour) " + why);
        auto bad = values;
        bad[9] = "-1";
        std::size_t field = 0;
        check(!app::incidentsui::settingsFrom(bad, out, &why, &field) && field == 9, "S un regroupement negatif : refuse");
        std::error_code ec;
        fs::remove_all(proj, ec);
    }
}

// ---- 1.21.0 : la video d'un incident lue comme un enregistrement (le declenchement, la lecture, la vitesse, la boucle,
// le pas a pas) et le zoom de l'image ----------------------------------------------------------------------------------
void incidentPlayer() {
    std::printf("1.21.0 : le lecteur des videos d'incident, le zoom de l'image\n");
    namespace iu = app::incidentsui;
    const auto near = [](double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; };
    // ---- le declenchement dans la video (video.json : instants_s, avant_s)
    {
        std::vector<double> times, shifted;
        for (int i = 0; i < 76; ++i) times.push_back(-10.0 + i * 0.2);   // 5 images par seconde : 10 s avant, 5 s apres
        for (const double t : times) shifted.push_back(t + 0.1);
        check(near(iu::triggerPosition(times, 5, 10, 15.2), 10.0, 1e-9), "1.21.0 declenchement : une cadence juste, -instants[0]");
        check(near(iu::triggerPosition(shifted, 5, 10, 15.2), 9.9, 1e-9), "1.21.0 declenchement : entre deux images");
        check(near(iu::triggerPosition({-3, -1, 0.5, 2}, 2, 0, 2), (1.0 + 1.0 / 1.5) / 2.0, 1e-9),
              "1.21.0 declenchement : des instants irreguliers (avant la 1.20.2), au prorata");
        check(near(iu::triggerPosition({0, 0.5, 1}, 2, 0, 1.5), 0.0), "1.21.0 declenchement sur la premiere image");
        check(iu::triggerPosition({2, 2.5, 3}, 2, 0, 1.5) < 0, "1.21.0 la video commence apres le declenchement : inconnu");
        check(near(iu::triggerPosition({}, 5, 10, 15), 10.0), "1.21.0 sans instants : avant_s");
        check(iu::triggerPosition({}, 5, 20, 15) < 0, "1.21.0 avant_s plus long que la video : inconnu");
        check(iu::triggerPosition({}, 0, 0, 15) < 0, "1.21.0 ni instants ni avant_s : inconnu");
    }
    // ---- la lecture : position, pause, vitesse, -10 s / +10 s, le pas a pas, le bout, la boucle
    {
        iu::VideoClock c;
        c.start(15.0, 5.0, 10.0);
        check(c.playing && near(c.position, 0) && near(c.speed, 1) && !c.loop && !c.ended, "1.21.0 la video part du debut, en lecture, x1");
        check(c.frames() == 75 && c.frame() == 0 && c.hasTrigger() && near(c.sinceTrigger(), -10.0), "1.21.0 75 images, t = -10 s");
        c.advance(1.0);
        check(near(c.position, 1.0), "1.21.0 elle avance de dt x vitesse");
        c.playPause();
        c.advance(1.0);
        check(!c.playing && near(c.position, 1.0), "1.21.0 en pause, rien ne bouge");
        c.playPause();
        c.faster();
        check(near(c.speed, 2.0), "1.21.0 + : x2 (les vitesses de la relecture)");
        c.advance(0.5);
        check(near(c.position, 2.0), "1.21.0 a x2, 0,5 s d'horloge font 1 s de video");
        c.slower();
        c.slower();
        c.slower();
        check(near(c.speed, 0.25), "1.21.0 - trois fois : x0,25");
        c.slower();
        check(near(c.speed, 0.25), "1.21.0 x0,25 est la plus lente");
        for (int i = 0; i < 10; ++i) c.faster();
        check(near(c.speed, 8.0), "1.21.0 x8 est la plus rapide");
        c.setSpeed(3.0);
        check(near(c.speed, 4.0), "1.21.0 une vitesse quelconque : la plus proche (x3 -> x4)");
        c.setSpeed(1.4);
        check(near(c.speed, 1.0), "1.21.0 x1,4 -> x1");
        c.setSpeed(-2);
        check(near(c.speed, 1.0), "1.21.0 une vitesse absurde : x1");
        c.seek(5);
        c.step(-10);
        check(near(c.position, 0), "1.21.0 -10 s s'arrete au debut");
        c.step(10);
        check(near(c.position, 10) && c.playing, "1.21.0 +10 s, la lecture continue");
        c.step(10);
        check(near(c.position, 15) && c.ended && !c.playing, "1.21.0 +10 s s'arrete a la fin (sans boucle : en pause, finie)");
        c.toTrigger();
        check(near(c.position, 10) && near(c.sinceTrigger(), 0) && !c.ended, "1.21.0 aller au declenchement : t = 0");
        c.playPause();
        c.stepFrame(1);
        check(!c.playing && c.frame() == 51 && near(c.position, 10.2), "1.21.0 l'image suivante met en pause (image 51)");
        c.stepFrame(-2);
        check(c.frame() == 49 && near(c.sinceTrigger(), -0.2), "1.21.0 deux images en arriere : t = -0,2 s");
        c.seek(0);
        c.stepFrame(-1);
        check(c.frame() == 0 && near(c.position, 0), "1.21.0 pas d'image avant la premiere");
        c.seek(15);
        check(c.frame() == 74, "1.21.0 a la fin : la derniere image (" + std::to_string(c.frame()) + ")");
        c.stepFrame(1);
        check(c.frame() == 74, "1.21.0 pas d'image apres la derniere");
        c.seek(14.5);
        c.playPause();
        c.advance(1.0);
        check(near(c.position, 15) && !c.playing && c.ended, "1.21.0 au bout, sans boucle : la derniere image reste");
        c.playPause();
        check(c.playing && near(c.position, 0) && !c.ended, "1.21.0 au bout, Lecture repart du debut");
        c.loop = true;
        c.seek(14.5);
        c.advance(1.0);
        check(c.playing && near(c.position, 0.5), "1.21.0 en boucle, elle repart du debut");
        c.setSpeed(0.5);
        c.start(10, 10, 2);
        check(near(c.speed, 0.5) && c.loop && near(c.position, 0) && c.frames() == 100, "1.21.0 la vitesse et la boucle restent d'une video a l'autre");
        iu::VideoClock d;
        d.start(3, 0, -1);
        d.toTrigger();
        check(!d.hasTrigger() && near(d.position, 0) && d.frames() == 0, "1.21.0 sans declenchement ni cadence : D ne fait rien");
        d.stepFrame(1);
        check(near(d.position, 0.1) && !d.playing, "1.21.0 sans cadence, une image vaut 0,1 s");
    }
    // ---- les textes de la barre
    check(iu::clockTenths(6.84) == "00:06,8" && iu::clockTenths(75) == "01:15,0" && iu::clockTenths(-1) == "00:00,0",
          "1.21.0 le temps au dixieme (" + iu::clockTenths(6.84) + ")");
    check(iu::signedSeconds(-3.21) == "\xE2\x88\x92" "3,2 s" && iu::signedSeconds(1.2) == "+1,2 s" && iu::signedSeconds(0.01) == "0,0 s",
          "1.21.0 t depuis le declenchement (" + iu::signedSeconds(-3.21) + ")");
    check(iu::speedLabel(0.25) == "\xC3\x97" "0,25" && iu::speedLabel(8) == "\xC3\x97" "8", "1.21.0 les vitesses ecrites comme la relecture");
    // ---- le zoom de l'image
    {
        iu::ImageView v;
        const gfx::Rect box{0.f, 0.f, 400.f, 300.f};
        gfx::Rect r = v.place(box, 800, 400);   // 800 x 400 ajustee : 400 x 200, centree
        check(near(r.x, 0) && near(r.y, 50) && near(r.w, 400) && near(r.h, 200) && !v.zoomed(), "1.21.0 l'image entiere, ajustee");
        v.zoomAt(2.f, box, 800, 400, {100.f, 150.f});
        r = v.place(box, 800, 400);
        check(v.zoomed() && near(r.w, 800) && near(r.x + 0.25f * r.w, 100, 1.0) && near(r.y + 0.5f * r.h, 150, 1.0),
              "1.21.0 la molette zoome autour de la souris (le point reste dessous)");
        v.drag({1000.f, 1000.f}, box, 800, 400);
        r = v.place(box, 800, 400);
        check(near(r.x, 0, 1.0) && near(r.y, 0, 1.0), "1.21.0 glisser : l'image ne laisse pas de bord vide (haut gauche)");
        v.drag({-5000.f, -5000.f}, box, 800, 400);
        r = v.place(box, 800, 400);
        check(near(r.x + r.w, 400, 1.0) && near(r.y + r.h, 300, 1.0), "1.21.0 glisser : ... ni en bas a droite");
        v.zoomAt(100.f, box, 800, 400, {200.f, 150.f});
        check(near(v.zoom, iu::ImageView::kMax), "1.21.0 le zoom s'arrete a 10");
        v.zoomBy(1.f / iu::ImageView::kStep, box, 800, 400);
        check(near(v.zoom, iu::ImageView::kMax / iu::ImageView::kStep, 1e-4), "1.21.0 zoom arriere : un cran");
        v.zoomAt(0.5f, box, 800, 400, {10.f, 10.f});
        r = v.place(box, 800, 400);
        check(!v.zoomed() && near(v.pan.x, 0) && near(v.pan.y, 0) && near(r.y, 50), "1.21.0 pas plus petit que l'image entiere");
        v.zoomBy(2.f, box, 800, 400);
        v.fit();
        check(!v.zoomed() && near(v.pan.x, 0), "1.21.0 Ajuster (le double-clic) : l'image entiere");
        r = v.place(box, 0, 0);
        check(near(r.w, 400) && near(r.h, 300), "1.21.0 une image sans taille : le cadre");
    }
}

int main() {
    detection();
    dependencies();
    storage();
    disk();
    host();
    volume();
    incidentsCase();
    incidentsPane();   // 1.20.2 : le volet (dossiers par alarme, recherche, criticite, tri)
    incidentPlayer();  // 1.21.0 : la video lue comme un enregistrement, le zoom de l'image
    burstHost();       // 1.21.0 : une alarme qui bat - 300 000 reapparitions, un seul incident
    burstGuards();     // 1.21.0 : regroupement 0, anti-rebond, plafond horaire, quota disque, reglages
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
