// tests/simstatus_test.cpp - lot API 8 : le Centre de simulation, sans ecran.
//
//   simstatus_test
//
//  LE BANDEAU (app/SimStatus) : ce qu'il dit, en francais clair, et sa couleur,
//  cas par cas - pas de projet, arretee, tout tourne (la phrase de la maquette :
//  << Tout tourne : l'automate (cycle 12 480, 14 ms sur 20), l'IHM
//  (Vue_Armoire_A) et 3 equipements simules >>), a surveiller (deux fonctions
//  non simulees qui rendent 0 ; un equipement muet), la halte d'une boucle sans
//  fin dans BUILDING (et LE bouton : Mettre a jour BUILDING 0.24), en pause, un
//  point d'arret, une modification en ligne, l'ancien code. Les trois cartes,
//  la chaine (equipements <-> automate <-> IHM), CE QUI MERITE TON ATTENTION
//  (le plus grave d'abord), la pastille de l'arbre, les mots (milliers, durees).
//
//  LE JOURNAL (app/SimJournal) : ajouter, repeter (une ligne, << x 3 >>), les 30
//  dernieres secondes, filtrer par source, par gravite, chercher sans accents,
//  la capacite, le CSV.
#include "../src/app/SimJournal.hpp"
#include "../src/app/SimStatus.hpp"
#include "../src/app/SimCenter.hpp"   // lot API 8 (le moteur) : SimForcing::programSays

#include <cstdio>
#include <string>

using namespace app;
namespace ss = app::simstatus;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Un projet ouvert, l'automate pret et en marche : cycle 12 480, 14 ms de calcul
// sur 20, 21 entrees ; l'IHM en marche sur Vue_Armoire_A ; trois equipements
// simules qui repondent.
ss::Snapshot running() {
    ss::Snapshot s;
    s.project = true;
    s.plc.prepared = true;
    s.plc.state = ss::PlcFacts::State::Running;
    s.plc.cycle = 12480;
    s.plc.clockMs = 249600;
    s.plc.scanMicros = 14000;
    s.plc.periodMs = 20;
    s.plc.entries = 21;
    s.plc.sections = 18;
    s.hmi.exists = true;
    s.hmi.running = true;
    s.hmi.view = "Vue_Armoire_A";
    s.hmi.user = "jdupont";
    s.hmi.plcReads = 37;
    for (const char* name : {"Balance A", "Balance B", "Variateur"}) {
        ss::EquipFacts::One e;
        e.name = name;
        e.simulated = true;
        e.ok = true;
        e.tested = true;
        e.state = "Simul\xC3\xA9";
        s.equip.list.push_back(e);
    }
    s.equip.exchangesPerSecond = 64.0;
    return s;
}

const ss::Attention* findAttention(const ss::Report& r, const std::string& id) {
    for (const auto& a : r.attention)
        if (a.id == id) return &a;
    return nullptr;
}

void bandeau() {
    std::printf("le bandeau\n");
    {
        ss::Snapshot s;
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Off && r.banner.title == "Aucun projet ouvert" && r.banner.fix.empty(),
              "pas de projet : gris, \"Aucun projet ouvert\", pas de bouton");
    }
    {
        ss::Snapshot s;
        s.project = true;
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Off && r.banner.title == "La simulation est arr\xC3\xAAt\xC3\xA9" "e", "arretee : gris, \"La simulation est arretee\"");
        check(r.banner.fix.label == "Simuler" && r.banner.fix.key == "sim.run", "... son bouton : Simuler (sim.run)");
        check(r.folderBadge == "arr\xC3\xAAt\xC3\xA9" "e" && r.folderTone == ss::Tone::Off, "... la pastille de l'arbre : arretee, grise");
        check(!r.banner.meaning.empty(), "... et ce que ca veut dire");
    }
    {
        const auto r = ss::compute(running());
        check(r.banner.tone == ss::Tone::Ok, "tout tourne : vert");
        check(r.banner.title == "Tout tourne : l'automate (cycle 12\xE2\x80\xAF" "480, 14 ms sur 20), l'IHM (Vue_Armoire_A) et 3 \xC3\xA9quipements simul\xC3\xA9s",
              "la phrase de la maquette : " + r.banner.title);
        check(r.folderBadge == "en marche" && r.folderTone == ss::Tone::Ok && r.hmiBadge == "Vue_Armoire_A" && r.equipBadge == "3 simul\xC3\xA9s",
              "les pastilles (la maquette) : en marche (vert), la vue de l'IHM, 3 simules : " + r.hmiBadge + " / " + r.equipBadge);
        check(r.attention.empty(), "rien a signaler");
        auto s = running();
        s.hmi.running = false;
        const auto r2 = ss::compute(s);
        check(r2.banner.tone == ss::Tone::Ok && !has(r2.banner.title, "l'IHM") && r2.banner.fix.key == "ihm" && has(r2.banner.meaning, "L'IHM ne tourne pas"),
              "l'IHM arretee : le bouton Lancer l'IHM, et le bandeau le dit");
    }
    {
        auto s = running();
        s.plc.unknown.push_back({"STRING_TO_ASCII", 1892, "Reset_all", 12});
        s.plc.unknown.push_back({"FIND_INT", 40, "BUILDING.Corps", 58});
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Warning && r.banner.title == "\xC3\x80 surveiller : 2 fonctions ne sont pas simul\xC3\xA9" "es et rendent 0",
              "a surveiller : " + r.banner.title);
        check(r.banner.fix.key == "ligne:Reset_all:12" && has(r.banner.meaning, "STRING_TO_ASCII et FIND_INT"),
              "... le bouton va au premier appel, le sens nomme les fonctions");
        check(r.cards[0].tone == ss::Tone::Warning, "... la carte de l'automate passe a l'orange");
        const auto* a = findAttention(r, "inconnues");
        check(a && a->tone == ss::Tone::Warning && has(a->detail, "1\xE2\x80\xAF" "932 appels") && has(a->detail, "rendent 0"),
              "... et une ligne dans l'attention (1 932 appels, elles rendent 0)");
    }
    {
        auto s = running();
        s.equip.list[1].ok = false;
        s.equip.list[1].why = "d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9";
        s.equip.list[1].state = "Injoignable";
        s.plc.unknown.push_back({"STRING_TO_ASCII", 3, "Reset_all", 12});
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Warning && has(r.banner.title, "2 fonctions") == false && has(r.banner.title, "STRING_TO_ASCII n'est pas simul"),
              "une fonction : son nom dans le bandeau (" + r.banner.title + ")");
        check(has(r.banner.title, "et un autre point") && r.banner.second.key == "ensemble", "... et l'autre point compte, Tout voir");
        check(r.cards[2].tone == ss::Tone::Error && has(r.cards[2].sentence, "Balance B ne r\xC3\xA9pond pas : d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9"),
              "la carte des equipements : rouge, Balance B ne repond pas");
        check(r.cards[2].figures[1].value == "2 OK \xC2\xB7 1 KO" && r.cards[2].figures[1].tone == ss::Tone::Error, "... Liaisons : 2 OK . 1 KO");
        check(r.chain[0].broken && has(r.chain[0].why, "Balance B"), "la chaine : la fleche des equipements coupee, et pourquoi");
        check(!r.attention.empty() && r.attention.front().id == "equipement:Balance B", "l'attention : l'equipement muet d'abord (le plus grave)");
        check(r.attention.front().action.key == "reconnecter:Balance B" && r.attention.front().second.key == "equipements",
              "... deux boutons (la maquette) : Retablir la liaison, Ouvrir les equipements");
        check(r.equipBadge == "1 coup\xC3\xA9" && r.equipTone == ss::Tone::Error, "la pastille des equipements : 1 coupe (la maquette)");
    }
    {
        auto s = running();
        s.plc.state = ss::PlcFacts::State::Halted;
        s.plc.haltReason = "le cycle a d\xC3\xA9pass\xC3\xA9 2\xE2\x80\xAF" "000\xE2\x80\xAF" "000 instructions et s'est arr\xC3\xAAt\xC3\xA9";
        s.plc.haltSection = "BUILDING.Corps";
        s.plc.haltLine = 58;
        s.plc.haltBlock = "BUILDING";
        s.plc.haltLoop = true;
        s.plc.haltLimitMs = 1500;
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Error
                  && r.banner.title == "L'automate est arr\xC3\xAAt\xC3\xA9 : boucle sans fin dans BUILDING, ligne 58 (le cycle a d\xC3\xA9pass\xC3\xA9 1,5 s)",
              "la halte : rouge, " + r.banner.title);
        check(r.banner.fix.label == "Aller \xC3\xA0 la ligne" && r.banner.fix.key == "ligne:BUILDING.Corps:58" && r.banner.second.key == "relancer",
              "... Aller a la ligne, et Relancer depuis zero");
        check(r.folderBadge == "d\xC3\xA9" "faut" && r.folderTone == ss::Tone::Error, "... la pastille : defaut, rouge (la maquette)");
        check(r.chain[1].broken && has(r.chain[1].why, "valeurs fig\xC3\xA9" "es"), "... la fleche vers l'IHM coupee : elle lit des valeurs figees");
        check(!r.attention.empty() && r.attention.front().id == "halte", "... et en tete de l'attention");
        check(r.attention.front().action.label == "Voir la ligne 58" && r.attention.front().action.key == "ligne:BUILDING.Corps:58"
                  && r.attention.front().second.key == "relancer",
              "... deux boutons (la maquette) : Voir la ligne 58, Relancer du cycle 0");
        s.plc.haltNewer = "0.24";
        const auto r2 = ss::compute(s);
        check(r2.banner.fix.label == "Mettre \xC3\xA0 jour BUILDING 0.24" && r2.banner.fix.key == "bibliotheque",
              "une version plus recente en bibliotheque : Mettre a jour BUILDING 0.24");
        const auto* halt2 = findAttention(r2, "halte");
        check(has(r2.banner.meaning, "0.24") && halt2 && halt2->action.key == "bibliotheque" && halt2->action.label == "Mettre BUILDING \xC3\xA0 jour 0.24"
                  && halt2->second.label == "Voir la ligne 58" && has(halt2->detail, "0.24"),
              "... le sens et l'attention le disent : une ligne, deux boutons (la maquette)");
        check(r2.cards[0].buttons.size() == 2 && r2.cards[0].buttons[1].key == "debogage", "la carte de l'automate : Ouvrir l'automate, Deboguer (la maquette)");
    }
    {
        auto s = running();
        s.plc.state = ss::PlcFacts::State::Paused;
        s.plc.cycle = 1234;
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Info && r.banner.title == "En pause au cycle 1\xE2\x80\xAF" "234 : tu as appuy\xC3\xA9 sur Pause"
                  && r.banner.fix.key == "sim.run" && r.banner.fix.label == "Continuer" && r.banner.second.key == "sim.step",
              "en pause : bleu (la maquette), Continuer, Un cycle");
        check(r.folderBadge == "pause" && r.folderTone == ss::Tone::Info, "... la pastille du dossier : pause, bleue (la maquette)");
        check(r.chain[1].idle && !r.chain[1].alive, "... la fleche vers l'IHM au repos");
        s.plc.breakHit = true;
        s.plc.breakSection = "SFC_PurgeA";
        s.plc.breakLine = 42;
        const auto r2 = ss::compute(s);
        check(r2.banner.tone == ss::Tone::Warning && has(r2.banner.title, "SFC_PurgeA, ligne 42") && r2.banner.fix.key == "debogage"
                  && r2.banner.second.label == "Continuer",
              "un point d'arret : orange (la maquette), l'endroit, Deboguer, Continuer");
        check(r2.folderTone == ss::Tone::Warning && r2.folderBadge == "point d'arr\xC3\xAAt", "... la pastille du dossier : point d'arret, orange");
    }
    {
        auto s = running();
        s.plc.online = true;
        s.plc.onlineSummary = "3 sections gard\xC3\xA9" "es, 1 ajout\xC3\xA9" "e";
        s.plc.onlineAgeSeconds = 3;
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Info && has(r.banner.title, "Modification en ligne : 3 sections"), "modification en ligne : bleu");
        s.plc.onlineAgeSeconds = 45;
        check(ss::compute(s).banner.tone == ss::Tone::Ok, "... vingt secondes apres, le bandeau redevient vert");
        s.plc.online = false;
        s.plc.stale = true;
        const auto r3 = ss::compute(s);
        check(r3.banner.tone == ss::Tone::Info && r3.banner.fix.key == "relancer" && has(r3.banner.title, "ancien code"),
              "le programme a change : bleu, Relancer sur le nouveau code");
    }
    {
        ss::Snapshot s;
        s.project = true;
        s.plc.prepareError = "aucune section en ST de la t\xC3\xA2" "che MAST n'a pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e";
        const auto r = ss::compute(s);
        check(r.banner.tone == ss::Tone::Error && has(r.banner.title, "ne d\xC3\xA9marre pas") && r.folderBadge == "d\xC3\xA9" "faut",
              "la preparation echoue : rouge");
    }
}

void cartes() {
    std::printf("les cartes, la chaine, l'attention\n");
    auto s = running();
    s.plc.forced = 2;
    s.plc.oldestForced = "Armoires[0].ana.PT1.mes";
    s.plc.oldestForcedSeconds = 720;
    s.hmi.alarms = 2;
    s.hmi.activeAlarms = 2;
    s.hmi.unacked = 1;
    s.hmi.topAlarm = "Surpression cuve A";
    s.hmi.topPriority = 2;
    const auto r = ss::compute(s);
    const auto& plc = r.cards[0];
    check(plc.key == "automate" && plc.title == "AUTOMATE" && plc.tone == ss::Tone::Ok && plc.state == "en marche", "AUTOMATE : vert, en marche");
    check(plc.figures[0].value == "12\xE2\x80\xAF" "480" && plc.figures[1].value == "14 ms" && plc.figures[1].detail == "sur 20 ms (MAST)",
          "... cycle 12 480, 14 ms sur 20 ms (MAST)");
    check(plc.figures[1].bar > 0.69 && plc.figures[1].bar < 0.71 && plc.figures[1].tone == ss::Tone::Ok, "... une barre a 70 %, verte");
    check(plc.figures[2].value == "2" && plc.figures[2].detail == "le plus ancien : 12 min" && plc.figures[2].tone == ss::Tone::Warning,
          "... 2 forcages, le plus ancien depuis 12 min");
    check(plc.buttons.size() == 2 && plc.buttons[0].key == "automate" && plc.buttons[1].key == "debogage", "... ses boutons (la maquette) : Ouvrir l'automate, Deboguer");
    const auto& ihm = r.cards[1];
    check(ihm.tone == ss::Tone::Warning && ihm.figures[0].value == "Vue_Armoire_A" && ihm.figures[1].value == "jdupont"
              && ihm.figures[2].value == "2" && ihm.figures[2].detail == "dont 1 \xC3\xA0 acquitter",
          "IHM : orange (une alarme a acquitter), la vue, l'utilisateur, 2 alarmes dont 1 a acquitter");
    check(ihm.buttons.size() == 2 && ihm.buttons[0].key == "ihm" && ihm.buttons[1].key == "alarmes", "... Ouvrir l'IHM, Voir les alarmes");
    const auto& eq = r.cards[2];
    check(eq.tone == ss::Tone::Ok && eq.figures[0].value == "3" && eq.figures[0].detail == "dont 3 simul\xC3\xA9s" && eq.figures[1].value == "3 OK"
              && eq.figures[2].value == "64/s",
          "EQUIPEMENTS : 3 dont 3 simules, 3 OK, 64/s");
    check(r.chain[0].alive && r.chain[0].text == "64 \xC3\xA9" "changes/s" && r.chain[1].alive && r.chain[1].text == "37 variables lues",
          "la chaine : 64 echanges/s, 37 variables lues, les deux vivantes");
    check(r.attention.size() == 2 && r.attention[0].id == "alarmes" && r.attention[1].id == "forcages",
          "l'attention : l'alarme a acquitter, puis les forcages anciens");
    check(r.attention[1].tone == ss::Tone::Warning && has(r.attention[1].text, "depuis 12 min pour Armoires[0].ana.PT1.mes")
              && r.attention[1].action.key == "forcages" && r.attention[1].second.key == "forcages:tout-relacher",
          "... les forcages : depuis 12 min, Voir les forcages, Tout relacher (la maquette)");
    // Un cycle trop long : 46 ms pour 20.
    s.plc.scanMicros = 46000;
    const auto r2 = ss::compute(s);
    check(r2.cards[0].figures[1].tone == ss::Tone::Error && r2.cards[0].figures[1].bar == 1.0, "46 ms sur 20 : la barre pleine, rouge");
    const auto* slow = findAttention(r2, "cycle-lent");
    check(slow && slow->tone == ss::Tone::Warning && has(slow->text, "46 ms pour 20 ms") && has(slow->detail, "2,3 fois moins vite"),
          "... et l'attention : le temps simule avance 2,3 fois moins vite");
    // Sans equipement ni IHM.
    ss::Snapshot bare;
    bare.project = true;
    bare.plc.prepared = true;
    bare.plc.state = ss::PlcFacts::State::Running;
    bare.plc.cycle = 5;
    const auto r3 = ss::compute(bare);
    check(r3.chain[0].idle && r3.chain[0].text == "aucun \xC3\xA9quipement" && r3.chain[1].idle && r3.chain[1].text == "pas d'IHM",
          "sans equipement ni IHM : deux fleches au repos, qui le disent");
    check(r3.cards[1].state == "pas d'IHM" && r3.cards[2].state == "aucun" && r3.equipBadge.empty(), "... les cartes aussi");
    check(r3.banner.title == "Tout tourne : l'automate (cycle 5)", "... et le bandeau : " + r3.banner.title);
}

void mots() {
    std::printf("les mots\n");
    check(ss::thousands(0) == "0" && ss::thousands(999) == "999" && ss::thousands(1234567) == "1\xE2\x80\xAF" "234\xE2\x80\xAF" "567", "les milliers");
    check(ss::milliseconds(2.34) == "2,3 ms" && ss::milliseconds(146.4) == "146 ms" && ss::milliseconds(1500) == "1,5 s"
              && ss::milliseconds(2000) == "2 s",
          "les millisecondes : 2,3 ms, 146 ms, 1,5 s, 2 s");
    check(ss::duration(1.3) == "1,3 s" && ss::duration(42) == "42 s" && ss::duration(150) == "2 min 30 s" && ss::duration(720) == "12 min"
              && ss::duration(3700) == "1 h 1 min",
          "les durees : 1,3 s, 42 s, 2 min 30 s, 12 min, 1 h 1 min");
    check(ss::plural(1, "alarme", "alarmes") == "1 alarme" && ss::plural(2, "alarme", "alarmes") == "2 alarmes", "le pluriel");
    const auto line = ss::bannerLine(ss::compute(running()));
    check(line.rfind("[vert] Tout tourne", 0) == 0 && has(line, "Ce que \xC3\xA7" "a veut dire : ") && has(line, "Bouton : Voir les courbes (courbes)"),
          "le bandeau en une ligne (etat-bandeau) : " + line);
}

void journal() {
    std::printf("le journal\n");
    double t = 100.0;
    SimJournal j;
    j.setClock([&t] { return t; }, [] { return std::string("14:05:12"); });
    j.setCycle(1234);
    const auto a = j.add(SimSource::Automate, SimSeverity::Ok, "demarrage", "Simuler : le programme tourne", {}, "automate");
    check(j.size() == 1 && j.events().front().id == a && j.events().front().cycle == 1234 && j.events().front().clock == "14:05:12",
          "un evenement : son numero, son cycle, son heure");
    check(SimJournal::explanationOf(j.events().front()).find("cycle apr\xC3\xA8s cycle") != std::string::npos, "... et ce qu'il veut dire (son genre)");
    t = 100.5;
    j.add(SimSource::Equipements, SimSeverity::Error, "liaison", "Balance B ne r\xC3\xA9pond plus", {}, "equipements");
    t = 101.0;
    j.add(SimSource::Equipements, SimSeverity::Error, "liaison", "Balance B ne r\xC3\xA9pond plus", {}, "equipements");
    t = 101.5;
    j.add(SimSource::Equipements, SimSeverity::Error, "liaison", "Balance B ne r\xC3\xA9pond plus", {}, "equipements");
    check(j.size() == 2 && j.events().back().repeats == 3, "le meme, aussitot : une ligne, x 3");
    check(SimJournal::line(j.events().back()).find("(x 3)") != std::string::npos, "... la ligne le dit");
    t = 110.0;
    j.add(SimSource::Debogage, SimSeverity::Info, "Point d'arr\xC3\xAAt pos\xC3\xA9 : SFC_PurgeA, ligne 42", "ligne:SFC_PurgeA:42");
    check(j.events().back().kind == "debogage" && j.events().back().go == "ligne:SFC_PurgeA:42", "l'API simple : add(source, gravite, texte, cle)");
    check(SimJournal::goLine("SFC_PurgeA", 42) == "ligne:SFC_PurgeA:42", "la cle d'une ligne : goLine");
    t = 111.0;
    const auto hit = j.add(SimSource::Debogage, SimSeverity::Info, "point-arret", "Point d'arr\xC3\xAAt : SFC_PurgeA, ligne 42", {}, SimJournal::goLine("SFC_PurgeA", 42));
    const auto before = j.size();
    t = 111.2;
    const auto again = j.add(SimSource::Debogage, SimSeverity::Info, "arret", "Arr\xC3\xAAt\xC3\xA9 sur le point d'arr\xC3\xAAt 1 : SFC_PurgeA ligne 42", {},
                             SimJournal::goLine("SFC_PurgeA", 42));
    check(again == hit && j.size() == before && j.events().back().kind == "arret" && j.events().back().text.find("point d'arr\xC3\xAAt 1") != std::string::npos,
          "un arret dit deux fois (breakHit, puis le debogage) : une ligne, la phrase du debogage");
    t = 150.0;
    j.add(SimSource::Ihm, SimSeverity::Warning, "alarme", "Alarme : Surpression cuve A", {}, "alarmes");
    const auto recent = j.since(30.0);
    check(recent.size() == 1 && recent.front()->kind == "alarme", "les 30 dernieres secondes : l'alarme seule");
    check(j.since(45.0).size() == 3 && j.since(60.0).size() == 5, "les 45 dernieres : trois ; les 60 dernieres : cinq");
    const auto eq = j.filtered(SimJournal::bit(SimSource::Equipements), SimSeverity::Info);
    check(eq.size() == 1 && eq.front()->source == SimSource::Equipements, "filtrer par source : les equipements");
    const auto bad = j.filtered(0, SimSeverity::Warning);
    check(bad.size() == 2 && bad.front()->kind == "alarme", "a surveiller et erreurs : deux, les plus recents d'abord");
    check(j.filtered(0, SimSeverity::Error).size() == 1, "les erreurs seules : une");
    check(j.filtered(0, SimSeverity::Info, "repond").size() == 1, "chercher sans accents : \"repond\" trouve \"r\xC3\xA9pond\"");
    check(j.filtered(0, SimSeverity::Info, "equipements").size() == 1, "... et la source (\"equipements\")");
    check(j.count(SimSource::Automate) == 1 && j.countAtLeast(SimSeverity::Warning) == 2, "compter");
    const auto rev = j.revision();
    j.setCapacity(16);
    for (int i = 0; i < 40; ++i) {
        t += 3.0;
        j.add(SimSource::Automate, SimSeverity::Info, "cycle", "Un cycle (" + std::to_string(i) + ")", {}, {});
    }
    check(j.size() == 16 && j.events().back().text == "Un cycle (39)" && j.revision() > rev, "la capacite : les plus anciens partent");
    const auto csv = j.csv();
    check(csv.rfind("Heure;Cycle;Source;Gravit\xC3\xA9;", 0) == 0 && csv.find("Un cycle (39)") != std::string::npos, "le CSV");
    j.clear();
    check(j.empty(), "effacer");
}

// ---- Lot API 8 : le moteur - LE PROGRAMME DIRAIT (l'onglet Forcages) ----
//  Le collecteur (SimulationWorkspace.cpp) remplit SimForcing::programSays par
//  SimulationHost::unforcedValue pour un forcage de l'automate ; la case d'un
//  jumeau n'en a pas. Le moteur lui-meme : tests/simdebug_test.cpp (section 7).
void programmeDirait() {
    std::printf("\nLe programme dirait\n");
    SimForcing fresh;
    check(fresh.programSays.empty(), "un for\xC3\xA7" "age neuf : rien de lu");
    SimForcing plc;
    plc.where = "Automate";
    plc.what = "Pompe_Marche";
    plc.value = "TRUE";
    plc.programSays = "FALSE";
    plc.key = "plc:Pompe_Marche";
    SimForcing twin;
    twin.source = SimForcing::Source::Equipement;
    twin.what = "40001 \xC2\xB7 INT";
    twin.value = "12";
    twin.key = "twin:Balance B:40001|INT";
    SimCenterModel model;
    model.forcings = {plc, twin};
    check(model.forcings.size() == 2 && model.forcings[0].programSays == "FALSE" && model.forcings[0].programSays != model.forcings[0].value,
          "l'automate : forc\xC3\xA9" "e \xC3\xA0 TRUE, le programme dirait FALSE");
    check(model.forcings[1].programSays.empty(), "la case d'un jumeau : sans objet (vide)");
}

} // namespace

int main() {
    bandeau();
    cartes();
    mots();
    journal();
    programmeDirait();   // lot API 8 (le moteur)
    std::printf("\n%s (%d)\n", failures == 0 ? "simstatus : tout va bien" : "simstatus : ECHECS", failures);
    return failures == 0 ? 0 : 1;
}
