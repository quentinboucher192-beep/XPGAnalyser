// =============================================================================
//  tests/sfc_test.inl - 1.51.0 (S) : le GRAFCET natif (SFC) lu, modifie, simule
// -----------------------------------------------------------------------------
//  Inclus par graphxml_test.cpp (check, failures, kSfc). Des graphes FICTIFS,
//  ecrits comme Control Expert les ecrit (Essai_..., jamais un projet de client) :
//    - le modele : chaque element (etape, transition, divergences et convergences
//      en OU et en ET, saut, lien, texte, actions et leurs qualificatifs,
//      supervision), le graphe resolu, et l'ecriture octet pour octet ;
//    - les modifications : etape + transition dessous, divergences, saut,
//      receptivite (variable, expression -> section ST), actions, renommer,
//      supprimer, deplacer ; chacune une commande que Ctrl+Z defait ;
//    - la simulation : sequence, OU (priorite a gauche), ET, une evolution par
//      cycle, Etape.x / Etape.t, retard, supervision, N S R L D P1 P0, une
//      section d'action ST, une receptivite ST et une en LD.
// =============================================================================
#include "../src/project/SfcDiagram.hpp"
#include "../src/project/SfcEdit.hpp"
#include "../src/project/RenameCommands.hpp"
#include "../src/sim/Runtime.hpp"
#include "../src/sim/SfcRun.hpp"

#include <memory>

namespace sfctest {

// Un graphe riche, colonne 0 et colonne 3, comme Control Expert le range.
const char* kRich =
    "<SFCProgram areaNum=\"0\" operatorCtrl=\"0\">\r\n"
    "\t\t<identProgram name=\"G7_Essai\" type=\"section\" task=\"MAST\" SectionOrder=\"1\"></identProgram>\r\n"
    "\t\t<chartSource name=\"Chart\">\r\n"
    "\t\t\t<networkSFC>\r\n"
    "\t\t\t\t<step stepType=\"initialStep\" stepName=\"Init\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"0\"></objPosition>\r\n"
    "\t\t\t\t\t<action qualifier=\"N\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<variableName>Lampe_Init</variableName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral></tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<literals max=\"\" min=\"\" delay=\"\"></literals>\r\n"
    "\t\t\t\t\t<comment>attente du d\xC3\xA9part</comment>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"1\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t<variableName>Depart</variableName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<step stepType=\"step\" stepName=\"Remplir\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"2\"></objPosition>\r\n"
    "\t\t\t\t\t<action qualifier=\"S\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<variableName>Pompe</variableName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral></tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<action qualifier=\"L\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<variableName>Vanne</variableName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral>t#2s</tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<action qualifier=\"D\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<variableName>Alarme</variableName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral>t#5s</tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<action qualifier=\"P1\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<sectionName>A_Compte</sectionName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral></tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<literals max=\"t#10s\" min=\"t#1s\" delay=\"\"></literals>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<altBranch width=\"4\" relativePos=\"0\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"3\"></objPosition>\r\n"
    "\t\t\t\t</altBranch>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"3\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t<variableName>Plein</variableName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"3\" posY=\"3\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t<sectionName>T_Vide</sectionName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<step stepType=\"step\" stepName=\"Chauffer\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"4\"></objPosition>\r\n"
    "\t\t\t\t\t<literals max=\"\" min=\"\" delay=\"\"></literals>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<step stepType=\"step\" stepName=\"Vider\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"3\" posY=\"4\"></objPosition>\r\n"
    "\t\t\t\t\t<action qualifier=\"R\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<variableName>Pompe</variableName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral></tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<literals max=\"\" min=\"\" delay=\"\"></literals>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"5\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t<sectionName>T_Chaud</sectionName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"3\" posY=\"5\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"true\">\r\n"
    "\t\t\t\t\t\t<variableName>Pompe</variableName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<altJoint width=\"4\" relativePos=\"0\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"5\"></objPosition>\r\n"
    "\t\t\t\t</altJoint>\r\n"
    "\t\t\t\t<step stepType=\"step\" stepName=\"Attente\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"6\"></objPosition>\r\n"
    "\t\t\t\t\t<literals max=\"\" min=\"\" delay=\"t#500ms\"></literals>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"7\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t<variableName>Go_Par</variableName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<parBranch width=\"4\" relativePos=\"0\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"8\"></objPosition>\r\n"
    "\t\t\t\t</parBranch>\r\n"
    "\t\t\t\t<step stepType=\"step\" stepName=\"Moteur_A\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"8\"></objPosition>\r\n"
    "\t\t\t\t\t<literals max=\"\" min=\"\" delay=\"\"></literals>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<step stepType=\"step\" stepName=\"Moteur_B\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"3\" posY=\"8\"></objPosition>\r\n"
    "\t\t\t\t\t<action qualifier=\"P0\">\r\n"
    "\t\t\t\t\t\t<actionName>\r\n"
    "\t\t\t\t\t\t\t<variableName>Sortie_P0</variableName>\r\n"
    "\t\t\t\t\t\t</actionName>\r\n"
    "\t\t\t\t\t\t<tValue>\r\n"
    "\t\t\t\t\t\t\t<tLiteral></tLiteral>\r\n"
    "\t\t\t\t\t\t</tValue>\r\n"
    "\t\t\t\t\t</action>\r\n"
    "\t\t\t\t\t<literals max=\"\" min=\"\" delay=\"\"></literals>\r\n"
    "\t\t\t\t</step>\r\n"
    "\t\t\t\t<parJoint width=\"4\" relativePos=\"0\">\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"8\"></objPosition>\r\n"
    "\t\t\t\t</parJoint>\r\n"
    "\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t<objPosition posX=\"0\" posY=\"9\"></objPosition>\r\n"
    "\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t<sectionName>T_AB</sectionName>\r\n"
    "\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t</transition>\r\n"
    "\t\t\t\t<linkSFC>\r\n"
    "\t\t\t\t\t<directedLinkSource objectType=\"transition\">\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"0\" posY=\"9\"></objPosition>\r\n"
    "\t\t\t\t\t</directedLinkSource>\r\n"
    "\t\t\t\t\t<directedLinkDestination objectType=\"step\">\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"0\" posY=\"0\"></objPosition>\r\n"
    "\t\t\t\t\t</directedLinkDestination>\r\n"
    "\t\t\t\t\t<gridObjPosition posX=\"0.5\" posY=\"10\"></gridObjPosition>\r\n"
    "\t\t\t\t\t<gridObjPosition posX=\"-0.5\" posY=\"10\"></gridObjPosition>\r\n"
    "\t\t\t\t\t<gridObjPosition posX=\"-0.5\" posY=\"0\"></gridObjPosition>\r\n"
    "\t\t\t\t\t<gridObjPosition posX=\"0.5\" posY=\"0\"></gridObjPosition>\r\n"
    "\t\t\t\t</linkSFC>\r\n"
    "\t\t\t\t<textBox width=\"2\" height=\"1\">cuve d'essai<objPosition posX=\"1\" posY=\"0\"></objPosition>\r\n"
    "\t\t\t\t</textBox>\r\n"
    "\t\t\t</networkSFC>\r\n"
    "\t\t</chartSource>\r\n"
    "\t\t<transitionSource name=\"T_Vide\">\r\n"
    "\t\t\t<STSource>Niveau &lt; 10</STSource>\r\n"
    "\t\t</transitionSource>\r\n"
    "\t\t<transitionSource name=\"T_Chaud\">\r\n"
    "\t\t\t<LDSource nbColumns=\"11\">\r\n"
    "\t\t\t\t<networkLD>\r\n"
    "\t\t\t\t\t<typeLine>\r\n"
    "\t\t\t\t\t\t<contact typeContact=\"openContact\" contactVariableName=\"Chaud\"></contact>\r\n"
    "\t\t\t\t\t\t<HLink nbCells=\"9\"></HLink>\r\n"
    "\t\t\t\t\t\t<coil typeCoil=\"coil\" coilVariableName=\"T_Chaud\"></coil>\r\n"
    "\t\t\t\t\t</typeLine>\r\n"
    "\t\t\t\t</networkLD>\r\n"
    "\t\t\t</LDSource>\r\n"
    "\t\t</transitionSource>\r\n"
    "\t\t<transitionSource name=\"T_AB\">\r\n"
    "\t\t\t<STSource>Moteur_A.t &gt;= t#1s AND Moteur_B.x</STSource>\r\n"
    "\t\t</transitionSource>\r\n"
    "\t\t<actionSource name=\"A_Compte\">\r\n"
    "\t\t\t<STSource>Compteur := Compteur + 1;\r\n"
    "</STSource>\r\n"
    "\t\t</actionSource>\r\n"
    "\t</SFCProgram>";

std::string document() {
    std::string d =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<PGMExchangeFile>\r\n"
        "\t<fileHeader company=\"Schneider Automation\" product=\"Control Expert V15.3 - 230214C\" dateTime=\"date_and_time#2026-11-13-10:0:0\" content=\"Fichier source multiprogramme\" DTDVersion=\"41\"></fileHeader>\r\n"
        "\t<contentHeader name=\"Essai_SFC\" version=\"0.0.1\"></contentHeader>\r\n"
        "\t<logicConf>\r\n"
        "\t\t<resource resName=\"M580\" resIdent=\"BME P58 2040 03.20\">\r\n"
        "\t\t\t<taskDesc task=\"MAST\" taskType=\"periodic\" valueType=\"20\">\r\n"
        "\t\t\t\t<sectionDesc name=\"G7_Essai\" SectionOrder=\"1\"></sectionDesc>\r\n"
        "\t\t\t</taskDesc>\r\n"
        "\t\t</resource>\r\n"
        "\t</logicConf>\r\n"
        "\t";
    d += kRich;
    d += "\r\n\t<dataBlock>\r\n";
    for (const char* v : { "Depart", "Plein", "Chaud", "Go_Par", "Lampe_Init", "Pompe", "Vanne", "Alarme", "Sortie_P0" })
        d += std::string("\t\t<variables name=\"") + v + "\" typeName=\"BOOL\"></variables>\r\n";
    d += "\t\t<variables name=\"Niveau\" typeName=\"INT\"></variables>\r\n";
    d += "\t\t<variables name=\"Compteur\" typeName=\"INT\"></variables>\r\n";
    d += "\t</dataBlock>\r\n</PGMExchangeFile>\r\n";
    return d;
}

int itemNamed(const sfc::Chart& c, std::string_view name) { return c.findStep(name); }

int transitionAt(const sfc::Chart& c, int x, int y) {
    const int i = c.itemAt(x, y);
    return i >= 0 && c.items[static_cast<std::size_t>(i)].kind == sfc::ItemKind::Transition ? i : -1;
}

std::vector<std::string> names(const sfc::Chart& c, const std::vector<int>& items) {
    std::vector<std::string> out;
    for (const int i : items) out.push_back(c.items[static_cast<std::size_t>(i)].name);
    std::sort(out.begin(), out.end());
    return out;
}

bool boolOf(const sim::Runtime& rt, const char* name) {
    sim::Value v;
    return rt.get(name, v) && v.isTruthy();
}
long long intOf(const sim::Runtime& rt, const char* name) {
    sim::Value v;
    return rt.get(name, v) ? static_cast<long long>(v.asInteger()) : -999;
}
bool active(const sim::Runtime& rt, const char* step) {
    const auto* run = rt.sfcChart("G7_Essai");
    const auto* s = run ? run->stepNamed(step) : nullptr;
    return s && s->active;
}
std::string situation(const sim::Runtime& rt) {
    const auto* run = rt.sfcChart("G7_Essai");
    std::string out;
    if (run) for (const auto& n : run->activeSteps()) out += (out.empty() ? "" : ",") + n;
    return out;
}

void model() {
    std::printf("\n--- SFC : le modele, l'ecriture octet pour octet\n");
    // le petit graphe de graphxml
    {
        const auto c = sfc::parse(kSfc);
        check(c.ok && c.editable, "kSfc : lu et modifiable");
        check(c.programName == "G7", "kSfc : son nom (identProgram)");
        check(c.items.size() == 5, "kSfc : 5 elements (2 etapes, 2 transitions, 1 saut)");
        check(c.sources.size() == 1 && c.sources[0].language == domain::PouLanguage::LD && c.sources[0].name == "T1",
              "kSfc : la section de transition T1 en LD");
        check(sfc::write(c) == kSfc, "kSfc : reecrit sans modification = l'original");
        const auto t = sfc::resolve(c);
        check(t.transitions.size() == 2, "kSfc : 2 transitions resolues");
        const int t2 = transitionAt(c, 1, 4);
        const auto* tl = t.of(t2);
        check(tl && names(c, tl->sources) == std::vector<std::string>{ "X1" }
                  && names(c, tl->targets) == std::vector<std::string>{ "X0" },
              "kSfc : la 2e transition va de X1 a X0 (par le saut)");
        check(t.problems.empty(), "kSfc : aucun probleme");
    }
    // le riche
    const auto c = sfc::parse(kRich);
    check(c.ok && c.editable, "riche : lu");
    check(sfc::write(c) == kRich, "riche : reecrit sans modification = l'original");
    int steps = 0, trans = 0, bars = 0, links = 0, boxes = 0;
    for (const auto& it : c.items) {
        steps += it.kind == sfc::ItemKind::Step;
        trans += it.kind == sfc::ItemKind::Transition;
        bars += it.isBar();
        links += it.kind == sfc::ItemKind::Link;
        boxes += it.kind == sfc::ItemKind::TextBox;
    }
    check(steps == 7 && trans == 7 && bars == 4 && links == 1 && boxes == 1,
          "riche : 7 etapes, 7 transitions, 4 divergences/convergences, 1 lien, 1 texte");
    const int init = itemNamed(c, "Init"), remplir = itemNamed(c, "Remplir");
    check(init >= 0 && c.items[static_cast<std::size_t>(init)].initial(), "riche : Init est initiale");
    check(init >= 0 && c.items[static_cast<std::size_t>(init)].comment == "attente du d\xC3\xA9part", "riche : le commentaire d'Init");
    if (remplir >= 0) {
        const auto& r = c.items[static_cast<std::size_t>(remplir)];
        check(r.actions.size() == 4 && r.actions[0].qualifier == "S" && r.actions[1].qualifier == "L"
                  && r.actions[1].time == "t#2s" && r.actions[3].isSection && r.actions[3].name == "A_Compte",
              "riche : les actions de Remplir (S, L t#2s, D, P1 section)");
        check(r.tmin == "t#1s" && r.tmax == "t#10s", "riche : la supervision de Remplir (min, max)");
    }
    {
        const auto& tb = c.items.back();
        check(tb.kind == sfc::ItemKind::TextBox && tb.text == "cuve d'essai" && tb.boxW == 2, "riche : le texte");
    }
    const auto t = sfc::resolve(c);
    check(t.problems.empty(), "riche : aucun probleme");
    for (const auto& [i, m] : t.problems) std::printf("    probleme %d : %s\n", i, m.c_str());
    const auto* left = t.of(transitionAt(c, 0, 3));
    const auto* right = t.of(transitionAt(c, 3, 3));
    check(left && right && names(c, left->sources) == std::vector<std::string>{ "Remplir" }
              && names(c, right->sources) == std::vector<std::string>{ "Remplir" }
              && names(c, left->targets) == std::vector<std::string>{ "Chauffer" }
              && names(c, right->targets) == std::vector<std::string>{ "Vider" },
          "riche : divergence en OU - Remplir -> Chauffer | Vider");
    const auto* jl = t.of(transitionAt(c, 0, 5));
    const auto* jr = t.of(transitionAt(c, 3, 5));
    check(jl && jr && names(c, jl->targets) == std::vector<std::string>{ "Attente" }
              && names(c, jr->targets) == std::vector<std::string>{ "Attente" },
          "riche : convergence en OU vers Attente");
    const auto* go = t.of(transitionAt(c, 0, 7));
    check(go && go->andSplit && names(c, go->targets) == std::vector<std::string>{ "Moteur_A", "Moteur_B" },
          "riche : divergence en ET - Moteur_A et Moteur_B ensemble");
    const auto* ab = t.of(transitionAt(c, 0, 9));
    check(ab && ab->andJoin && names(c, ab->sources) == std::vector<std::string>{ "Moteur_A", "Moteur_B" }
              && names(c, ab->targets) == std::vector<std::string>{ "Init" },
          "riche : convergence en ET puis le lien vers Init");
    check(sfc::conditionText(c, c.items[static_cast<std::size_t>(transitionAt(c, 3, 5))]) == "NOT Pompe",
          "riche : une receptivite inversee");
    check(sfc::conditionText(c, c.items[static_cast<std::size_t>(transitionAt(c, 3, 3))]) == "Niveau < 10",
          "riche : la receptivite d'une section ST");

    // renommer une variable : receptivites, actions, sections ST et LD ; le reste tel quel
    {
        std::string x = kRich;
        const auto n = project::renameInCode(x, "Pompe", "Pompe_1", false);
        const auto r = sfc::parse(x);
        int pompe = 0;
        for (const auto& it : r.items) {
            for (const auto& a : it.actions) pompe += a.name == "Pompe_1";
            pompe += it.kind == sfc::ItemKind::Transition && it.condText == "Pompe_1";
        }
        check(n == 3 && pompe == 3, "renommer Pompe : les actions S et R et la receptivite NOT Pompe");
        std::string y = kRich;
        project::renameInCode(y, "Chaud", "Temp_OK", false);
        project::renameInCode(y, "Niveau", "Niv", false);
        check(y.find("contactVariableName=\"Temp_OK\"") != std::string::npos && y.find("<STSource>Niv &lt; 10</STSource>") != std::string::npos,
              "renommer dans la section LD (contact) et la section ST");
        check(y.find("stepName=\"Chauffer\"") != std::string::npos && project::renameInCode(y, "Init", "Debut", false) == 0,
              "les noms d'etape ne sont pas des variables : rien a renommer");
        std::string z = kRich;
        check(project::renameInCode(z, "Absent", "Autre", false) == 0 && z == kRich, "rien a renommer : le XML inchange");
    }

    // un element modifie : lui seul change
    auto m = c;
    auto& vider = m.items[static_cast<std::size_t>(itemNamed(m, "Vider"))];
    vider.tmax = "t#30s";
    vider.dirty = true;
    const auto out = sfc::write(m);
    std::string expect = kRich;
    {
        const std::string from = "\t\t\t\t\t<objPosition posX=\"3\" posY=\"4\"></objPosition>";
        const auto at = expect.find(from);
        const auto lit = expect.find("<literals max=\"\"", at);
        expect.replace(lit, std::string("<literals max=\"\"").size(), "<literals max=\"t#30s\"");
    }
    check(out == expect, "un champ change : seule sa ligne change dans le XML");
}

void drawing() {
    std::printf("\n--- SFC : le dessin (la grille de Control Expert)\n");
    const auto c = sfc::parse(kRich);
    const auto t = sfc::resolve(c);
    sfc::Grid grid;
    const auto d = sfc::buildDiagram(c, t, grafcet::fixedWidthMeasure(7.f), &grid);
    check(d.steps.size() == 7 && d.transitions.size() == 7, "dessin : 7 etapes, 7 transitions");
    check(d.junctions.size() == 4, "dessin : 4 traits (OU et ET)");
    int ands = 0;
    for (const auto& j : d.junctions) ands += j.isAnd;
    check(ands == 2, "dessin : la divergence et la convergence en ET en double trait");
    check(d.notes.size() == 1 && d.notes[0].text == "cuve d'essai", "dessin : le texte");
    check(d.actions.size() == 7, "dessin : 7 actions a droite de leur etape");
    const auto* init = d.step(c.findStep("Init"));
    check(init && init->initial && init->label == "Init", "dessin : Init, double carre, son nom dans la case");
    bool qual = false;
    for (const auto& a : d.actions) qual = qual || (a.qualifier == "L" && a.text == "Vanne" && a.kindText == "t#2s");
    check(qual, "dessin : l'action L Vanne t#2s");
    if (const auto* rem = d.step(c.findStep("Remplir"))) {
        const auto cell = grid.cellAt({ rem->box.x + rem->box.w / 2.f, rem->box.y + rem->box.h / 2.f });
        check(cell.first == 0 && cell.second == 2, "dessin : la case sous le centre de Remplir est (0, 2)");
    }
    const auto* moteurB = d.step(c.findStep("Moteur_B"));
    const auto* moteurA = d.step(c.findStep("Moteur_A"));
    check(moteurA && moteurB && moteurB->box.x > moteurA->box.right(), "dessin : la colonne 3 a droite de la colonne 0");
    bool up = false;
    for (const auto& seg : d.segments) up = up || seg.arrowUp;
    check(up, "dessin : le lien qui remonte a Init porte sa fleche");
    const auto g = sfc::asGrafcet(c, t, "G7_Essai");
    check(g.steps.size() == 7 && g.transitions.size() == 7, "le meme graphe pour la vue (grafcet::Chart)");
    const auto k = sfc::checks(c, t);
    check(k.empty(), "aucun controle en defaut");
    // la lecture en texte : les actions, et la case de chaque ligne
    domain::Section sec;
    sec.language = domain::PouLanguage::SFC;
    sec.body = kRich;
    const auto lines = project::graphListing(sec);
    bool acts = false, pompe = false, cell = false, section = false;
    for (const auto& l : lines) {
        acts = acts || l.text.find("actions : S Pompe, L Vanne t#2s, D Alarme t#5s, P1 [A_Compte]") != std::string::npos;
        for (const auto& w : l.writes) pompe = pompe || w == "Pompe";
        cell = cell || (l.text.find("\xC3\xA9tape Vider") != std::string::npos && l.cellX == 3 && l.cellY == 4);
        section = section || (l.text.find("Compteur := Compteur + 1;") != std::string::npos && l.cellX == 0 && l.cellY == 2
                              && !l.writes.empty() && l.writes[0] == "Compteur");
    }
    check(acts, "texte : les actions de Remplir");
    check(pompe, "texte : Pompe est ecrite par une action");
    check(cell, "texte : la ligne de Vider sait sa case (3, 4)");
    check(section, "texte : la section d'action A_Compte, ecrite, a la case de Remplir");
}

void edits() {
    std::printf("\n--- SFC : les modifications\n");
    auto c = sfc::parse(kSfc);
    const int x1 = c.findStep("X1");
    auto r = sfc::addStepBelow(c, x1, "X2", "CMD_B");
    check(r.ok, "etape + transition sous X1");
    {
        const auto again = sfc::parse(sfc::write(c));
        const auto t = sfc::resolve(again);
        const int x2 = again.findStep("X2");
        check(again.ok && x2 >= 0 && again.items[static_cast<std::size_t>(x2)].x == 1
                  && again.items[static_cast<std::size_t>(x2)].y == 5,
              "X2 en (1, 5) apres relecture");
        const auto* tn = t.of(transitionAt(again, 1, 4));
        check(tn && names(again, tn->sources) == std::vector<std::string>{ "X1" }
                  && names(again, tn->targets) == std::vector<std::string>{ "X2" }
                  && again.items[static_cast<std::size_t>(tn->item)].condText == "CMD_B",
              "la nouvelle transition X1 -> X2 sur CMD_B");
        const auto* told = t.of(transitionAt(again, 1, 6));
        check(told && names(again, told->sources) == std::vector<std::string>{ "X2" }
                  && names(again, told->targets) == std::vector<std::string>{ "X0" },
              "l'ancienne transition (T1) suit X2 et revient a X0");
        check(t.problems.empty(), "aucun probleme apres l'ajout");
        check(sfc::write(again).find("\t\t\t\t\t<step stepType=\"initialStep\" stepName=\"X0\">\r\n"
                                     "\t\t\t\t\t\t<objPosition posX=\"1\" posY=\"1\"></objPosition>\r\n"
                                     "\t\t\t\t\t</step>") != std::string::npos,
              "X0 ressort tel quel");
    }
    // supprimer la nouvelle transition : elle part avec X2 qui la suit, les rangees
    // remontent - le XML d'origine
    r = sfc::removeItem(c, transitionAt(c, 1, 4));
    check(r.ok && c.findStep("X2") < 0, "supprimer la nouvelle transition (et X2 qui la suit)");
    check(sfc::write(c) == kSfc, "ajouter puis supprimer : le XML d'origine, octet pour octet");

    // la receptivite : une expression part dans une section ST
    c = sfc::parse(kSfc);
    r = sfc::setCondition(c, transitionAt(c, 1, 2), "X0.t > t#2s AND CMD_MARCHE", "G7_T1");
    check(r.ok && c.source("transitionSource", "G7_T1") && c.source("transitionSource", "G7_T1")->body
                                                               == "X0.t > t#2s AND CMD_MARCHE",
          "une expression devient la section de transition ST G7_T1");
    {
        const auto x = sfc::write(c);
        check(x.find("<sectionName>G7_T1</sectionName>") != std::string::npos
                  && x.find("<transitionSource name=\"G7_T1\">\r\n\t\t\t\t<STSource>X0.t &gt; t#2s AND CMD_MARCHE</STSource>\r\n"
                            "\t\t\t</transitionSource>") != std::string::npos,
              "la section ecrite comme Control Expert (STSource, entites)");
        check(x.find(kSfc + std::string(kSfc).find("<transitionSource name=\"T1\">")) != std::string::npos
                  || x.find("<transitionSource name=\"T1\">") != std::string::npos,
              "T1 reste");
    }
    r = sfc::setCondition(c, transitionAt(c, 1, 2), "NOT CMD_ARRET", "");
    check(r.ok && !c.source("transitionSource", "G7_T1"), "une variable a la place : la section inutile part");
    {
        const auto& t = c.items[static_cast<std::size_t>(transitionAt(c, 1, 2))];
        check(t.cond == sfc::CondKind::Variable && t.condText == "CMD_ARRET" && t.invert, "NOT X : inversee");
    }

    // divergence en OU sous X0
    c = sfc::parse(kSfc);
    r = sfc::addAlternative(c, c.findStep("X0"), "X5");
    check(r.ok, "divergence en OU sous X0");
    {
        const auto t = sfc::resolve(c);
        check(t.outOf(c.findStep("X0")).size() == 2, "X0 a deux transitions de sortie");
        const auto* branch = t.of(r.select);
        check(branch && names(c, branch->targets) == std::vector<std::string>{ "X5" }, "la branche mene a X5");
        bool jumpBack = false;
        for (const int ti : t.outOf(c.findStep("X5")))
            if (const auto* tl = t.of(ti)) jumpBack = jumpBack || names(c, tl->targets) == std::vector<std::string>{ "X1" };
        check(jumpBack, "X5 revient (saut) a X1, l'etape de la branche principale");
        int problems = 0;
        for (const auto& [i, msg] : t.problems) problems += msg.find("r\xC3\xA9" "ceptivit\xC3\xA9") == std::string::npos;
        check(problems == 0, "rien d'autre que les receptivites a ecrire");
        check(sfc::parse(sfc::write(c)).items.size() == c.items.size(), "relue : les memes elements");
    }
    // divergence en ET sous la transition de X0
    c = sfc::parse(kSfc);
    r = sfc::addSimultaneous(c, transitionAt(c, 1, 2), "X6");
    check(r.ok, "divergence en ET sous la transition de X0");
    {
        const auto t = sfc::resolve(c);
        const auto* tl = t.of(transitionAt(c, 1, 2));
        check(tl && tl->andSplit && names(c, tl->targets) == std::vector<std::string>{ "X1", "X6" }, "X1 et X6 ensemble");
        const auto* after = t.of(transitionAt(c, 1, 4));
        check(after && after->andJoin && names(c, after->sources) == std::vector<std::string>{ "X1", "X6" },
              "la transition suivante attend X1 et X6");
        // une etape sous X6 : la convergence descend, X1 la rejoint par un lien
        r = sfc::addStepBelow(c, c.findStep("X6"), "X7", "CMD_C");
        check(r.ok, "etape + transition sous X6 (dans le ET)");
        const auto t2 = sfc::resolve(c);
        const auto* after2 = t2.of(transitionAt(c, 1, 6));
        check(after2 && after2->andJoin && names(c, after2->sources) == std::vector<std::string>{ "X1", "X7" },
              "la convergence attend maintenant X1 et X7 (lien depuis X1)");
        check(t2.problems.empty(), "aucun probleme");
        for (const auto& [i, msg] : t2.problems) std::printf("    probleme %d : %s\n", i, msg.c_str());
    }
    // saut, actions, renommer, deplacer
    c = sfc::parse(kSfc);
    r = sfc::addJump(c, transitionAt(c, 1, 2), "X0");
    check(!r.ok, "un saut sous une transition deja suivie : refuse");
    r = sfc::renameStep(c, c.findStep("X0"), "Repos");
    check(r.ok && c.findStep("Repos") >= 0, "renommer X0 en Repos");
    {
        bool jump = false;
        for (const auto& it : c.items) jump = jump || (it.kind == sfc::ItemKind::Jump && it.name == "Repos");
        check(jump, "le saut suit le nouveau nom");
    }
    std::vector<sfc::Action> acts(2);
    acts[0].qualifier = "N";
    acts[0].name = "Voyant";
    acts[1].qualifier = "L";
    acts[1].name = "Klaxon";
    acts[1].time = "t#3s";
    r = sfc::setActions(c, c.findStep("X1"), acts);
    check(r.ok && c.items[static_cast<std::size_t>(c.findStep("X1"))].actions.size() == 2, "deux actions sur X1");
    acts[1].time.clear();
    check(!sfc::setActions(c, c.findStep("X1"), acts).ok, "L sans duree : refuse");
    {
        const auto x = sfc::write(c);
        check(x.find("<action qualifier=\"L\">") != std::string::npos && x.find("<tLiteral>t#3s</tLiteral>") != std::string::npos,
              "les actions ecrites (qualificatif, duree)");
    }
    r = sfc::moveItem(c, c.itemAt(1, 5), 2, 5);
    check(r.ok && c.itemAt(2, 5) >= 0, "deplacer le saut d'une case");
    check(!sfc::moveItem(c, c.findStep("X1"), 1, 2).ok, "deplacer sur une case prise : refuse");

    // la commande : un pas, que Ctrl+Z defait
    {
        auto project = std::make_shared<domain::Project>();
        domain::Section s;
        s.name = project->strings.intern("G7");
        s.language = domain::PouLanguage::SFC;
        s.body = kSfc;
        project->sections.push_back(s);
        auto chart = sfc::parse(project->sections[0].body);
        (void)sfc::addStepBelow(chart, chart.findStep("X1"), "X2", "CMD_B");
        project::SfcEditCommand cmd(project, 0, sfc::write(chart), "Ajouter une \xC3\xA9tape");
        check(static_cast<bool>(cmd.execute()) && project->sections[0].body != kSfc, "la commande ecrit le GRAFCET");
        check(!cmd.mergeableWith(cmd), "jamais fusionnee : un geste, un Ctrl+Z");
        check(static_cast<bool>(cmd.undo()) && project->sections[0].body == kSfc, "Ctrl+Z : le XML d'origine");
        check(static_cast<bool>(cmd.execute()) && sfc::parse(project->sections[0].body).findStep("X2") >= 0,
              "Ctrl+Y : de nouveau");
    }
    // un GRAFCET neuf
    {
        const auto x = sfc::newProgram("G7_Neuf", "MAST", "S_0");
        const auto n = sfc::parse(x);
        check(n.ok && n.editable && n.steps().size() == 1 && n.items[0].initial() && sfc::write(n) == x,
              "Nouvelle section SFC : une etape initiale, relue telle quelle");
        auto m = n;
        check(sfc::addStepBelow(m, 0, "S_1", "Marche").ok && sfc::resolve(m).transitions.size() == 1,
              "Nouvelle section SFC : on y ajoute etape + transition");
    }
}

void simulation() {
    std::printf("\n--- SFC : la simulation\n");
    auto project = std::make_shared<domain::Project>();
    {
        importer::XpgParser parser;
        const auto outcome = parser.parse(document(), *project, importer::ParseOptions{}, {});
        check(static_cast<bool>(outcome), "le projet d'essai se lit");
    }
    {
        const auto x = project::crossReference(*project, "Pompe");
        check(x.writes.size() >= 2, "references : Pompe ecrite par les actions (S, R)");
        const auto c = project::crossReference(*project, "Compteur");
        check(!c.writes.empty(), "references : Compteur ecrit par la section d'action");
        const auto n = project::crossReference(*project, "Niveau");
        check(!n.reads.empty(), "references : Niveau lu par la receptivite ST");
    }
    sim::Runtime rt(project);
    const auto prepared = rt.prepare("MAST");
    check(static_cast<bool>(prepared), "le GRAFCET se prepare");
    for (const auto& d : rt.preparationDiagnostics())
        std::printf("    preparation : %s (%s)\n", d.message.c_str(), d.section.c_str());
    check(rt.sfcChart("G7_Essai") != nullptr, "le GRAFCET est dans la simulation");
    auto scan = [&](int n = 1) { for (int i = 0; i < n; ++i) (void)rt.step(20); };

    scan();
    check(situation(rt) == "Init", "cycle 1 : la situation initiale (Init)");
    check(boolOf(rt, "Lampe_Init") && boolOf(rt, "Init.x") && boolOf(rt, "Init.X"), "N : Lampe_Init ; Init.x (et .X)");
    // une evolution par cycle : Depart et Plein vrais ensemble
    (void)rt.set("Depart", sim::Value::boolean(true));
    (void)rt.set("Plein", sim::Value::boolean(true));
    scan();
    check(situation(rt) == "Remplir", "Depart : Remplir (une seule evolution, Plein attend)");
    check(!boolOf(rt, "Lampe_Init"), "N : Lampe_Init retombe avec Init");
    check(boolOf(rt, "Pompe") && boolOf(rt, "Vanne") && !boolOf(rt, "Alarme") && intOf(rt, "Compteur") == 1,
          "S Pompe, L Vanne, D Alarme pas encore, P1 : A_Compte une fois");
    scan();
    check(situation(rt) == "Chauffer", "cycle suivant : Plein, Chauffer");
    check(boolOf(rt, "Remplir.tminErr"), "Remplir quittee avant 1 s : tminErr");
    check(intOf(rt, "Compteur") == 1, "P1 : pas de derniere execution");
    check(boolOf(rt, "Pompe"), "S : Pompe reste a 1 apres Remplir");
    // la receptivite en LD
    scan(3);
    check(situation(rt) == "Chauffer", "T_Chaud (LD) fausse : Chauffer reste");
    (void)rt.set("Chaud", sim::Value::boolean(true));
    scan();
    check(situation(rt) == "Attente", "Chaud : la receptivite LD passe, convergence en OU vers Attente");
    // le retard d'Attente (t#500ms)
    (void)rt.set("Go_Par", sim::Value::boolean(true));
    scan(10);
    check(situation(rt) == "Attente", "retard : Go_Par vrai mais Attente dure 500 ms");
    scan(15);
    check(situation(rt) == "Moteur_A,Moteur_B", "apres 500 ms : divergence en ET, Moteur_A et Moteur_B");
    const auto* run = rt.sfcChart("G7_Essai");
    // T_AB : Moteur_A.t >= t#1s AND Moteur_B.x
    scan(30);
    check(situation(rt) == "Moteur_A,Moteur_B", "600 ms : T_AB fausse");
    {
        sim::Value t;
        check(rt.get("Moteur_A.t", t) && t.asInteger() == 600, "Moteur_A.t = 600 ms");
    }
    (void)rt.set("Plein", sim::Value::boolean(false));
    (void)rt.set("Depart", sim::Value::boolean(false));
    scan(21);
    check(situation(rt) == "Init", "1 s : convergence en ET, le lien ramene a Init");
    check(boolOf(rt, "Sortie_P0"), "P0 : Sortie_P0 un cycle a la desactivation de Moteur_B");
    scan();
    check(!boolOf(rt, "Sortie_P0"), "P0 : puis retombe");
    check(run && run->evolutions >= 5, "les evolutions sont comptees");

    // la branche de droite, la remise a zero (R), L et D dans le temps, la supervision max
    rt.reset();
    (void)rt.set("Depart", sim::Value::boolean(true));
    (void)rt.set("Niveau", sim::Value::integer(sim::Type::Int, 50));
    scan(2);
    check(situation(rt) == "Remplir", "reset : Init puis Remplir");
    scan(100);
    check(!boolOf(rt, "Vanne") && !boolOf(rt, "Alarme"), "L : Vanne retombe apres 2 s ; D : pas encore");
    scan(150);
    check(boolOf(rt, "Alarme") && !boolOf(rt, "Remplir.tmaxErr"), "D : Alarme apres 5 s");
    scan(251);
    check(boolOf(rt, "Remplir.tmaxErr"), "supervision : Remplir active plus de 10 s");
    (void)rt.set("Niveau", sim::Value::integer(sim::Type::Int, 5));
    (void)rt.set("Plein", sim::Value::boolean(true));
    scan();
    check(situation(rt) == "Chauffer", "OU : les deux receptivites vraies, la plus a gauche passe");
    // la branche de droite
    rt.reset();
    (void)rt.set("Depart", sim::Value::boolean(true));
    (void)rt.set("Niveau", sim::Value::integer(sim::Type::Int, 5));
    scan(3);
    check(situation(rt) == "Vider", "Niveau < 10 (section ST) : Vider");
    check(!boolOf(rt, "Pompe"), "R : Vider remet Pompe a 0");
    scan();
    check(situation(rt) == "Attente", "NOT Pompe : convergence vers Attente");

    // forcer une etape
    check(rt.sfcSetStep("G7_Essai", "Vider", true) && active(rt, "Vider") && active(rt, "Attente"),
          "forcer : Vider activee a la main (Attente reste)");
    check(rt.sfcSetStep("G7_Essai", "Attente", false) && !active(rt, "Attente"), "forcer : Attente desactivee");
    check(rt.sfcInit("G7_Essai"), "initialiser");
    scan();
    check(situation(rt) == "Init", "initialiser : la situation initiale au cycle suivant");
}

// Un GRAFCET modifie se range dans le dossier du projet, se relit, s'exporte et se relit.
void store() {
    std::printf("\n--- SFC : modifie, range, rouvert, exporte\n");
    auto project = std::make_shared<domain::Project>();
    importer::XpgParser parser;
    check(static_cast<bool>(parser.parse(document(), *project, importer::ParseOptions{}, {})), "le projet d'essai se lit");
    domain::Index g7 = domain::kNoIndex;
    for (std::size_t i = 0; i < project->sections.size(); ++i)
        if (project->strings.text(project->sections[i].name) == "G7_Essai") g7 = static_cast<domain::Index>(i);
    if (g7 == domain::kNoIndex) { check(false, "G7_Essai trouve"); return; }
    auto chart = sfc::parse(project->sections[g7].body);
    (void)sfc::addStepBelow(chart, chart.findStep("Attente"), "Pause", "", "");
    (void)sfc::setCondition(chart, transitionAt(chart, 0, 7), "Attente.t > t#1s AND Go_Par", "G7_Essai_T1");
    project::SfcEditCommand cmd(project, g7, sfc::write(chart), "essai");
    check(static_cast<bool>(cmd.execute()), "le GRAFCET modifie");
    const auto edited = project->sections[g7].body;
    const auto folder = (std::filesystem::temp_directory_path() / "sfc_test_projet").string();
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    project::Manifest m;
    m.name = "Essai_SFC";
    check(static_cast<bool>(project::ProjectStore::save(*project, m, folder)), "range dans le dossier du projet");
    const auto opened = project::ProjectStore::open(folder);
    const domain::Section* back = nullptr;
    if (opened && opened->project)
        for (const auto& sec : opened->project->sections)
            if (opened->project->strings.text(sec.name) == "G7_Essai") back = &sec;
    check(back && back->body == edited && back->language == domain::PouLanguage::SFC, "rouvert : le meme GRAFCET, octet pour octet");
    exporter::XpgWriteOptions wo;
    wo.dateTime = "date_and_time#2026-11-13-10:0:0";
    const auto out = exporter::writeXpg(*project, wo);
    check(out && out->find("<transitionSource name=\"G7_Essai_T1\">") != std::string::npos
              && out->find("stepName=\"Pause\"") != std::string::npos,
          "exporte (XPG) : l'etape et la section de transition ajoutees");
    domain::Project again;
    check(out && static_cast<bool>(parser.parse(*out, again, importer::ParseOptions{}, {})), "l'export se relit");
    const domain::Section* reread = nullptr;
    for (const auto& sec : again.sections)
        if (again.strings.text(sec.name) == "G7_Essai") reread = &sec;
    if (reread) {
        const auto c = sfc::parse(reread->body);
        const auto t = sfc::resolve(c);
        check(c.findStep("Pause") >= 0 && t.problems.empty(), "relu : Pause est la, aucun probleme");
    }
    else check(false, "relu : G7_Essai");
    check(static_cast<bool>(cmd.undo()) && project->sections[g7].body == kRich, "Ctrl+Z : le GRAFCET d'origine");
    std::filesystem::remove_all(folder, ec);
}

void run() {
    model();
    drawing();
    edits();
    simulation();
    store();
}

} // namespace sfctest
