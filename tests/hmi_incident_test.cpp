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
// =============================================================================
#include "../src/app/ai/AiIncident.hpp"
#include "../src/app/hmi/HmiIncidentHost.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiIncident.hpp"
#include "../src/hmi/HmiRelations.hpp"
#include "../src/hmi/HmiVideo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
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
        v.frames.push_back(fakeJpeg());
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

} // namespace

int main() {
    detection();
    dependencies();
    storage();
    disk();
    host();
    volume();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
