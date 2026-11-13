// =============================================================================
//  tests/ladder_test.cpp - 1.51.0 : le ladder (LD) dans API Studio
// -----------------------------------------------------------------------------
//  Ce qui est verifie, sur des ladders FICTIFS ecrits comme Control Expert :
//    - la lecture en grille (contacts, bobines, fils, liaisons verticales, blocs,
//      comparaisons, operations, sauts, etiquettes, commentaires, inconnus) ;
//    - l'ecriture : a l'identique sans modification ; un reseau modifie reecrit
//      seul, aux conventions du fichier, et relu pareil ;
//    - les gestes de l'editeur, chacun un pas de Ctrl+Z ;
//    - la simulation : serie, parallele, S/R, fronts, TON, compare, operate,
//      l'etat en direct ; l'API d'une receptivite LD (agent S) ;
//    - les references croisees, le renommage, la section LD neuve.
// =============================================================================
#include "../src/core/Command.hpp"
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectParser.hpp"
#include "../src/project/CrossReference.hpp"
#include "../src/project/EditCommands.hpp"
#include "../src/project/GraphListing.hpp"
#include "../src/project/Ladder.hpp"
#include "../src/project/LadderEdit.hpp"
#include "../src/project/LadderSim.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/sim/Runtime.hpp"

#include <cstdio>
#include <map>
#include <string>

using namespace project::ladder;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* what) {
    ++checks;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Le ladder de graphxml_test (CRLF, shortCircuit, bloc place par sa position).
const char* kLd =
    "<LDSource nbColumns=\"11\">\r\n"
    "\t\t\t\t<networkLD>\r\n"
    "\t\t\t\t\t<typeLine>\r\n"
    "\t\t\t\t\t\t<contact typeContact=\"closedContact\" contactVariableName=\"E64A[6]\"></contact>\r\n"
    "\t\t\t\t\t\t<contact typeContact=\"openContact\" contactVariableName=\"X1.X\"></contact>\r\n"
    "\t\t\t\t\t\t<shortCircuit>\r\n"
    "\t\t\t\t\t\t\t<VLink></VLink>\r\n"
    "\t\t\t\t\t\t\t<HLink nbCells=\"1\"></HLink>\r\n"
    "\t\t\t\t\t\t</shortCircuit>\r\n"
    "\t\t\t\t\t\t<HLink nbCells=\"7\"></HLink>\r\n"
    "\t\t\t\t\t\t<coil typeCoil=\"setCoil\" coilVariableName=\"MT_PROD\"></coil>\r\n"
    "\t\t\t\t\t</typeLine>\r\n"
    "\t\t\t\t\t<typeLine>\r\n"
    "\t\t\t\t\t\t<contact typeContact=\"PContact\" contactVariableName=\"E64A[5]\"></contact>\r\n"
    "\t\t\t\t\t\t<HLink nbCells=\"1\"></HLink>\r\n"
    "\t\t\t\t\t\t<emptyCell nbCells=\"9\"></emptyCell>\r\n"
    "\t\t\t\t\t</typeLine>\r\n"
    "\t\t\t\t\t<typeLine>\r\n"
    "\t\t\t\t\t\t<emptyLine nbRows=\"2\"></emptyLine>\r\n"
    "\t\t\t\t\t</typeLine>\r\n"
    "\t\t\t\t\t<typeLine>\r\n"
    "\t\t\t\t\t\t<FFBBlock instanceName=\"TON_MT\" typeName=\"TON\" additionnalPinNumber=\"0\" enEnO=\"true\" width=\"10\" height=\"4\">\r\n"
    "\t\t\t\t\t\t\t<objPosition posX=\"8\" posY=\"4\"></objPosition>\r\n"
    "\t\t\t\t\t\t\t<descriptionFFB execAfter=\"\">\r\n"
    "\t\t\t\t\t\t\t\t<inputVariable invertedPin=\"false\" formalParameter=\"PT\" effectiveParameter=\"t#1s\"></inputVariable>\r\n"
    "\t\t\t\t\t\t\t\t<outputVariable invertedPin=\"false\" formalParameter=\"Q\" effectiveParameter=\"MANQUE_TENSION\"></outputVariable>\r\n"
    "\t\t\t\t\t\t\t</descriptionFFB>\r\n"
    "\t\t\t\t\t\t</FFBBlock>\r\n"
    "\t\t\t\t\t</typeLine>\r\n"
    "\t\t\t\t</networkLD>\r\n"
    "\t\t\t</LDSource>";

// Plus riche, en LF, VLink seule : comparaison, operation, saut, etiquette, RETURN,
// commentaire, un element et un enfant inconnus, un commentaire XML, des entites.
const char* kRich =
    "<LDSource nbColumns=\"6\" extra=\"x&amp;y\">\n"
    "\t<networkLD>\n"
    "\t\t<textBox width=\"6\" height=\"1\">Marche moteur &amp; voyant<objPosition posX=\"0\" posY=\"0\"></objPosition></textBox>\n"
    "\t\t<typeLine>\n"
    "\t\t\t<emptyLine nbRows=\"1\"></emptyLine>\n"
    "\t\t</typeLine>\n"
    "\t\t<typeLine>\n"
    "\t\t\t<contact typeContact=\"openContact\" contactVariableName=\"Marche\" vendor=\"keep\"></contact>\n"
    "\t\t\t<VLink></VLink>\n"
    "\t\t\t<contact typeContact=\"closedContact\" contactVariableName=\"Arret\"></contact>\n"
    "\t\t\t<compareBlock width=\"2\">\n"
    "\t\t\t\t<expression>Niveau &lt; 80</expression>\n"
    "\t\t\t</compareBlock>\n"
    "\t\t\t<HLink nbCells=\"1\"></HLink>\n"
    "\t\t\t<coil typeCoil=\"coil\" coilVariableName=\"Moteur\"></coil>\n"
    "\t\t</typeLine>\n"
    "\t\t<typeLine>\n"
    "\t\t\t<contact typeContact=\"openContact\" contactVariableName=\"Moteur\"></contact>\n"
    "\t\t\t<emptyCell nbCells=\"5\"></emptyCell>\n"
    "\t\t</typeLine>\n"
    "\t\t<mysteryElement a=\"1\"></mysteryElement>\n"
    "\t</networkLD>\n"
    "\t<!-- un commentaire XML garde -->\n"
    "\t<networkLD>\n"
    "\t\t<typeLine>\n"
    "\t\t\t<labelCell label=\"SUITE\"></labelCell>\n"
    "\t\t\t<contact typeContact=\"NContact\" contactVariableName=\"Capteur\"></contact>\n"
    "\t\t\t<operateBlock width=\"3\">\n"
    "\t\t\t\t<statement>Compteur := Compteur + 1;</statement>\n"
    "\t\t\t</operateBlock>\n"
    "\t\t\t<strangeCell nbCells=\"1\"></strangeCell>\n"
    "\t\t</typeLine>\n"
    "\t\t<typeLine>\n"
    "\t\t\t<contact typeContact=\"openContact\" contactVariableName=\"Fin\"></contact>\n"
    "\t\t\t<HLink nbCells=\"4\"></HLink>\n"
    "\t\t\t<control typeControl=\"jump\" label=\"SUITE\"></control>\n"
    "\t\t</typeLine>\n"
    "\t\t<typeLine>\n"
    "\t\t\t<contact typeContact=\"openContact\" contactVariableName=\"Defaut\"></contact>\n"
    "\t\t\t<HLink nbCells=\"4\"></HLink>\n"
    "\t\t\t<control typeControl=\"return\"></control>\n"
    "\t\t</typeLine>\n"
    "\t</networkLD>\n"
    "\t<unknownChild/>\n"
    "</LDSource>";

const Cell& cell(const Diagram& d, int n, int r, int c) {
    return d.networks[static_cast<std::size_t>(n)].cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
}

// Deux schemas disent-ils la meme chose (grille, liaisons, blocs, commentaires) ?
bool sameModel(const Diagram& a, const Diagram& b, std::string* why = nullptr) {
    auto say = [&](std::string w) { if (why) *why = std::move(w); return false; };
    if (a.networks.size() != b.networks.size()) return say("nombre de reseaux");
    for (std::size_t i = 0; i < a.networks.size(); ++i) {
        const auto& x = a.networks[i];
        const auto& y = b.networks[i];
        // les rangees vides en bas ne comptent pas
        auto used = [](const Network& n) {
            int last = 0;
            for (int r = 0; r < n.rows(); ++r) {
                bool any = false;
                for (const auto& c : n.cells[static_cast<std::size_t>(r)]) any = any || (c.kind != Kind::Empty);
                for (auto l : n.links[static_cast<std::size_t>(r)]) any = any || l;
                if (any) last = r + 1;
            }
            for (const auto& bl : n.blocks) last = std::max(last, bl.y + bl.height);
            for (const auto& c : n.comments) last = std::max(last, c.y + c.height);
            return last;
        };
        if (used(x) != used(y)) return say("reseau " + std::to_string(i) + " : rangees");
        for (int r = 0; r < used(x); ++r) {
            const auto& cx = x.cells[static_cast<std::size_t>(r)];
            const auto& cy = y.cells[static_cast<std::size_t>(r)];
            for (std::size_t c = 0; c < std::min(cx.size(), cy.size()); ++c) {
                const auto& p = cx[c];
                const auto& q = cy[c];
                if (p.kind != q.kind || p.name != q.name || p.text != q.text || p.width != q.width
                    || (p.kind == Kind::Contact && p.contact != q.contact) || (p.kind == Kind::Coil && p.coil != q.coil))
                    return say("reseau " + std::to_string(i) + " case " + std::to_string(r) + "," + std::to_string(c));
            }
            const auto& lx = x.links[static_cast<std::size_t>(r)];
            const auto& ly = y.links[static_cast<std::size_t>(r)];
            for (std::size_t k = 0; k < std::min(lx.size(), ly.size()); ++k)
                if (lx[k] != ly[k]) return say("reseau " + std::to_string(i) + " liaison " + std::to_string(r) + "," + std::to_string(k));
        }
        if (x.blocks.size() != y.blocks.size()) return say("blocs");
        for (std::size_t k = 0; k < x.blocks.size(); ++k) {
            const auto& p = x.blocks[k];
            const auto& q = y.blocks[k];
            if (p.type != q.type || p.instance != q.instance || p.x != q.x || p.y != q.y || p.width != q.width
                || p.height != q.height || p.enEno != q.enEno || p.pins.size() != q.pins.size())
                return say("bloc " + std::to_string(k));
            for (std::size_t m = 0; m < p.pins.size(); ++m)
                if (p.pins[m].formal != q.pins[m].formal || p.pins[m].effective != q.pins[m].effective
                    || p.pins[m].inverted != q.pins[m].inverted || p.pins[m].dir != q.pins[m].dir)
                    return say("broche " + std::to_string(m));
        }
        if (x.comments.size() != y.comments.size()) return say("commentaires");
        for (std::size_t k = 0; k < x.comments.size(); ++k)
            if (x.comments[k].text != y.comments[k].text || x.comments[k].x != y.comments[k].x || x.comments[k].y != y.comments[k].y)
                return say("commentaire");
    }
    return true;
}

Diagram mustParse(std::string_view xml) {
    auto d = parse(xml);
    if (!d) {
        std::printf("  parse : %s\n", d.error().message().c_str());
        return Diagram{};
    }
    return *d;
}

// Un environnement minimal : une table de variables.
class MapEnv final : public sim::Environment {
public:
    std::map<std::string, sim::Value> vars;
    std::vector<sim::Diagnostic> reports;
    bool read(std::string_view n, sim::Value& out) override {
        auto it = vars.find(std::string(n));
        if (it == vars.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        auto it = vars.find(std::string(n));
        if (it == vars.end()) return false;
        it->second = v;
        return true;
    }
    bool exists(std::string_view n) override { return vars.count(std::string(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic d) override { reports.push_back(std::move(d)); }
    bool b(const char* n) { return vars[n].isTruthy(); }
};

// ------------------------------------------------------------------ 1. le format
void testFormat() {
    std::printf("1. lire et ecrire\n");
    auto d = mustParse(kLd);
    check(d.columns == 11 && d.networks.size() == 1, "kLd : 11 colonnes, un reseau");
    check(write(d) == kLd, "kLd : rendu octet pour octet sans modification");
    if (d.networks.size() == 1) {
        const auto& n = d.networks[0];
        check(n.rows() >= 5, "kLd : la grille va jusqu'au bloc (rangee 4)");
        check(cell(d, 0, 0, 0).kind == Kind::Contact && cell(d, 0, 0, 0).contact == ContactType::Closed
                  && cell(d, 0, 0, 0).name == "E64A[6]", "kLd : contact ferme E64A[6] en (0,0)");
        check(cell(d, 0, 0, 1).contact == ContactType::Open && cell(d, 0, 0, 1).name == "X1.X", "kLd : contact ouvert X1.X");
        check(cell(d, 0, 0, 2).kind == Kind::Wire && n.link(0, 2), "kLd : le shortCircuit = un fil, et la liaison a sa gauche");
        check(cell(d, 0, 0, 9).kind == Kind::Wire && cell(d, 0, 0, 10).kind == Kind::Coil
                  && cell(d, 0, 0, 10).coil == CoilType::Set && cell(d, 0, 0, 10).name == "MT_PROD", "kLd : (S MT_PROD) en colonne 10");
        check(cell(d, 0, 1, 0).contact == ContactType::Rising && cell(d, 0, 1, 1).kind == Kind::Wire
                  && cell(d, 0, 1, 2).kind == Kind::Empty, "kLd : rangee 1 : [P E64A[5]] puis un fil puis du vide");
        check(n.blocks.size() == 1 && n.blocks[0].type == "TON" && n.blocks[0].instance == "TON_MT" && n.blocks[0].x == 8
                  && n.blocks[0].y == 4 && n.blocks[0].enEno && n.blocks[0].pins.size() == 2
                  && n.blocks[0].pins[0].effective == "t#1s" && n.blocks[0].pins[1].dir == PinDir::Out,
              "kLd : le bloc TON_MT, sa position, ses broches");
        check(n.blocks.size() == 1 && n.blocks[0].pinRow(0) == 5 && n.blocks[0].pinRow(1) == 5,
              "kLd : EN/ENO en rangee 4, PT et Q en rangee 5");
    }

    auto r = mustParse(kRich);
    check(write(r) == kRich, "kRich : rendu octet pour octet (LF, commentaire XML, enfant inconnu, entites)");
    check(r.columns == 6 && r.networks.size() == 2 && r.newline == "\n" && r.bareVLink, "kRich : 6 colonnes, LF, VLink seule");
    if (r.networks.size() == 2) {
        const auto& n0 = r.networks[0];
        check(n0.comments.size() == 1 && n0.comments[0].text == "Marche moteur & voyant", "kRich : le commentaire, decode");
        check(cell(r, 0, 1, 0).name == "Marche" && n0.link(1, 1) && cell(r, 0, 1, 1).contact == ContactType::Closed,
              "kRich : VLink seule = liaison a la frontiere courante");
        check(cell(r, 0, 1, 2).kind == Kind::Compare && cell(r, 0, 1, 2).text == "Niveau < 80" && cell(r, 0, 1, 2).width == 2
                  && cell(r, 0, 1, 3).kind == Kind::Covered, "kRich : [Niveau < 80] sur deux colonnes");
        check(cell(r, 0, 1, 5).kind == Kind::Coil && cell(r, 0, 1, 5).name == "Moteur", "kRich : (Moteur)");
        check(n0.extras.size() == 1, "kRich : l'element inconnu du reseau est garde");
        check(cell(r, 1, 0, 0).kind == Kind::Label && cell(r, 1, 0, 0).name == "SUITE", "kRich : l'etiquette SUITE");
        check(cell(r, 1, 0, 1).contact == ContactType::Falling, "kRich : [N Capteur]");
        check(cell(r, 1, 0, 2).kind == Kind::Operate && cell(r, 1, 0, 2).text == "Compteur := Compteur + 1;", "kRich : l'operation");
        check(cell(r, 1, 0, 5).kind == Kind::Raw, "kRich : une case inconnue (Raw)");
        check(cell(r, 1, 1, 5).kind == Kind::Jump && cell(r, 1, 1, 5).name == "SUITE", "kRich : le saut ->> SUITE");
        check(cell(r, 1, 2, 5).kind == Kind::Return, "kRich : RETURN");
    }

    // ---- modifier un reseau : lui seul est reecrit, aux conventions du fichier
    {
        auto m = r;
        check(setName(m, Pos{ 1, 1, 0 }, "Fin_cycle"), "setName : Fin -> Fin_cycle");
        const auto out = write(m);
        check(out.find(r.networks[0].raw) != std::string::npos, "un reseau modifie : l'autre reste octet pour octet");
        check(out.find("<!-- un commentaire XML garde -->") != std::string::npos && out.find("<unknownChild/>") != std::string::npos,
              "les blancs, commentaires et enfants inconnus restent");
        check(out.find("contactVariableName=\"Fin_cycle\"") != std::string::npos && out.find('\r') == std::string::npos,
              "le reseau reecrit : la nouvelle variable, en LF comme le fichier");
        check(out.find("<strangeCell nbCells=\"1\"></strangeCell>") != std::string::npos, "la case inconnue est reecrite telle quelle");
        auto again = mustParse(out);
        std::string why;
        check(sameModel(m, again, &why), ("relu : le meme schema " + why).c_str());
        check(write(again) == out, "relu puis rendu : identique");
    }
    // ---- tout reecrire (chaque reseau modifie) : relu, le meme schema
    for (const char* src : { kLd, kRich }) {
        auto m = mustParse(src);
        for (auto& n : m.networks) n.modified = true;
        const auto out = write(m);
        auto again = mustParse(out);
        std::string why;
        check(sameModel(m, again, &why), ("tout reecrit puis relu : le meme schema " + why).c_str());
        check(out.find("vendor=\"keep\"") != std::string::npos || src == kLd, "un attribut inconnu d'un contact est garde");
        check(out.find("extra=\"x&amp;y\"") != std::string::npos || src == kLd, "l'en-tete garde ses attributs encodes");
        if (src == kLd) {
            check(out.find("<shortCircuit>\r\n\t\t\t\t\t\t\t<VLink></VLink>\r\n\t\t\t\t\t\t\t<HLink nbCells=\"1\"></HLink>\r\n\t\t\t\t\t\t</shortCircuit>")
                      != std::string::npos, "kLd reecrit : le shortCircuit comme Control Expert, en CRLF et tabulations");
            check(out.find("<emptyLine nbRows=\"2\"></emptyLine>") != std::string::npos, "kLd reecrit : emptyLine nbRows=2");
            check(out.find("<FFBBlock instanceName=\"TON_MT\" typeName=\"TON\" additionnalPinNumber=\"0\" enEnO=\"true\" width=\"10\" height=\"4\">")
                      != std::string::npos, "kLd reecrit : le bloc, ses attributs dans leur ordre");
        }
    }
    // ---- <LDSource/>, la section neuve
    {
        auto e = mustParse("<LDSource/>");
        check(e.selfClosed && e.networks.empty() && write(e) == "<LDSource/>", "<LDSource/> : rendu tel quel");
        insertNetwork(e, 0);
        const auto out = write(e);
        check(out.find("<networkLD>") != std::string::npos && out.find("</LDSource>") != std::string::npos, "<LDSource/> + un reseau : rouvert");
        check(parse(out).has_value(), "... et relisible");
        const auto empty = emptySourceXml(11, 2);
        auto ed = mustParse(empty);
        check(ed.networks.size() == 1 && ed.columns == 11 && write(ed) == empty, "emptySourceXml : un reseau, 11 colonnes, stable");
        check(empty.find("\r\n\t\t\t<networkLD>") != std::string::npos && empty.find("\r\n\t\t</LDSource>") != std::string::npos,
              "emptySourceXml : indente pour une section de tache");
    }
    check(!parse("<LDSource><networkLD></LDSource>").has_value(), "un XML casse : une erreur, pas un schema faux");
}

// ------------------------------------------------------------------ 2. l'editeur
void testEditor() {
    std::printf("2. les gestes de l'editeur\n");
    auto d = mustParse(emptySourceXml(11, 2));
    // le reseau neuf : rien ; on en insere un modele
    const int n = insertNetwork(d, 1);
    check(n == 1 && d.networks.size() == 2 && cell(d, 1, 0, 0).kind == Kind::Wire && cell(d, 1, 0, 10).kind == Kind::Coil,
          "insertNetwork : un fil du rail a une bobine");
    check(placeContact(d, Pos{ 1, 0, 0 }, ContactType::Open, "A"), "placeContact A en (0,0)");
    check(setName(d, Pos{ 1, 0, 10 }, "B"), "setName : la bobine s'appelle B");
    const auto branch = addParallel(d, Pos{ 1, 0, 0 });
    check(branch.row == 1 && branch.col == 0 && cell(d, 1, 1, 0).kind == Kind::Contact && d.networks[1].link(0, 0)
              && d.networks[1].link(0, 1), "addParallel : un contact sous A, les deux liaisons");
    check(setName(d, branch, "C"), "la branche s'appelle C");
    check(placeContact(d, Pos{ 1, 0, 1 }, ContactType::Closed, "D"), "placeContact /D en (0,1)");
    check(invert(d, Pos{ 1, 0, 1 }) && cell(d, 1, 0, 1).contact == ContactType::Open, "invert : ferme -> ouvert");
    check(cycleType(d, Pos{ 1, 0, 1 }) && cell(d, 1, 0, 1).contact == ContactType::Closed, "cycleType : ouvert -> ferme");
    check(erase(d, Pos{ 1, 0, 1 }) && cell(d, 1, 0, 1).kind == Kind::Wire, "erase entre deux elements : un fil");
    check(placeCompare(d, Pos{ 1, 0, 2 }, "X > 5") && cell(d, 1, 0, 3).kind == Kind::Covered, "placeCompare sur deux colonnes");
    check(moveElement(d, Pos{ 1, 0, 2 }, Pos{ 1, 0, 5 }) && cell(d, 1, 0, 5).kind == Kind::Compare && cell(d, 1, 0, 2).kind == Kind::Wire,
          "moveElement : la comparaison deplacee, un fil a sa place");
    check(insertRow(d, 1, 1) && d.networks[1].rows() == 3 && cell(d, 1, 2, 0).name == "C" && d.networks[1].link(1, 0),
          "insertRow : la branche descend, la liaison traverse");
    check(deleteRow(d, 1, 1) && d.networks[1].rows() == 2 && cell(d, 1, 1, 0).name == "C" && d.networks[1].link(0, 0),
          "deleteRow : elle remonte, la liaison reste");
    check(moveNetwork(d, 1, 0) && cell(d, 0, 0, 0).name == "A", "moveNetwork : le reseau monte");
    check(setNetworkComment(d, 0, "Essai") && networkComment(d, 0) == "Essai" && cell(d, 0, 1, 0).name == "A",
          "setNetworkComment : un commentaire au-dessus");

    Block ton;
    ton.type = "TON";
    ton.instance = "T1";
    ton.pins = { Pin{ "IN", "", false, PinDir::In, {}, {} }, Pin{ "PT", "t#100ms", false, PinDir::In, {}, {} },
                 Pin{ "Q", "", false, PinDir::Out, {}, {} }, Pin{ "ET", "", false, PinDir::Out, {}, {} } };
    fitBlock(ton);
    const int nb = insertNetwork(d, 2);
    erase(d, Pos{ nb, 0, 10 });
    for (int c = 0; c < 11; ++c) erase(d, Pos{ nb, 0, c });
    placeContact(d, Pos{ nb, 0, 0 }, ContactType::Open, "A");
    check(placeBlock(d, Pos{ nb, 0, 3 }, ton) == 0, "placeBlock TON en colonne 3");
    check(placeCoil(d, Pos{ nb, 0, 10 }, CoilType::Normal, "TQ") && cell(d, nb, 0, 1).kind == Kind::Wire
              && cell(d, nb, 0, 6).kind == Kind::Wire, "les fils jusqu'au bloc et du bloc a la bobine");
    check(placeBlock(d, Pos{ nb, 0, 4 }, ton) < 0, "un bloc sur un autre : refuse");
    check(setPin(d, nb, 0, 1, "t#200ms") && d.networks[static_cast<std::size_t>(nb)].blocks[0].pins[1].effective == "t#200ms",
          "setPin : PT := t#200ms");

    // relu apres ecriture
    const auto out = write(d);
    auto again = mustParse(out);
    std::string why;
    check(sameModel(d, again, &why), ("apres les gestes : ecrit puis relu, le meme schema " + why).c_str());
    check(deleteNetwork(d, 0) && d.networks.size() == 2, "deleteNetwork");

    // ---- l'annulation : un geste, un pas
    auto proj = project::ProjectStore::createEmpty("Essai_LD", "BMXP342020");
    core::CommandStack stack;
    (void)stack.push(std::make_unique<project::AddSectionCommand>(proj, "Essai_LD", "MAST", domain::PouLanguage::LD));
    const auto si = static_cast<domain::Index>(proj->sections.size() - 1);
    const auto body0 = proj->sections[si].body;
    check(domain::keepsSourceXml(proj->sections[si]) && parse(body0).has_value(), "Nouvelle section LD : un ladder vide");
    auto d1 = mustParse(body0);
    insertNetwork(d1, 1);
    const auto body1 = write(d1);
    (void)stack.push(std::make_unique<LadderEditCommand>(proj, si, body1, "Ins\xC3\xA9rer un r\xC3\xA9seau"));
    placeContact(d1, Pos{ 1, 0, 0 }, ContactType::Open, "A");
    const auto body2 = write(d1);
    (void)stack.push(std::make_unique<LadderEditCommand>(proj, si, body2, "Contact"));
    check(proj->sections[si].body == body2, "deux gestes faits");
    (void)stack.undo();
    check(proj->sections[si].body == body1, "Ctrl+Z : le contact seul est defait");
    (void)stack.undo();
    check(proj->sections[si].body == body0, "Ctrl+Z : puis le reseau");
    (void)stack.redo();
    check(proj->sections[si].body == body1, "Ctrl+Y : le reseau revient");
    // l'export XPG de la section neuve
    exporter::XpgWriteOptions wo;
    const auto xpg = exporter::writeXpg(*proj, wo);
    check(xpg && xpg->find(body1) != std::string::npos, "l'export XPG rend le ladder tel quel");
    if (xpg) {
        domain::Project back;
        importer::XpgParser parser;
        check(parser.parse(*xpg, back, importer::ParseOptions{}, {}).has_value(), "l'export se relit");
        bool found = false;
        for (const auto& s : back.sections)
            if (back.strings.text(s.name) == "Essai_LD") found = s.language == domain::PouLanguage::LD && s.body == body1;
        check(found, "relu : la section LD, son XML a l'identique");
    }
}

// ------------------------------------------------------------------ 3. la simulation
std::string ladderBody() {
    auto d = mustParse(emptySourceXml(11, 2));
    deleteNetwork(d, 0);
    // 1 : C := A AND NOT B
    int n = insertNetwork(d, 0);
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "A");
    placeContact(d, Pos{ n, 0, 1 }, ContactType::Closed, "B");
    setName(d, Pos{ n, 0, 10 }, "C");
    // 2 : D := A OR B
    n = insertNetwork(d, 1);
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "A");
    setName(d, Pos{ n, 0, 10 }, "D");
    setName(d, addParallel(d, Pos{ n, 0, 0 }), "B");
    // 3 : S / R
    n = insertNetwork(d, 2);
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "S");
    placeCoil(d, Pos{ n, 0, 10 }, CoilType::Set, "Q");
    n = insertNetwork(d, 3);
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "R");
    placeCoil(d, Pos{ n, 0, 10 }, CoilType::Reset, "Q");
    // 4 : front montant de A -> P1 ; front descendant (bobine N) -> N1
    n = insertNetwork(d, 4);
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Rising, "A");
    setName(d, Pos{ n, 0, 10 }, "P1");
    n = insertNetwork(d, 5);
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "A");
    placeCoil(d, Pos{ n, 0, 10 }, CoilType::Falling, "N1");
    // 5 : A -> TON T1 (PT t#100ms) -> TQ
    n = insertNetwork(d, 6);
    for (int c = 0; c < 11; ++c) erase(d, Pos{ n, 0, c });
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "A");
    Block ton;
    ton.type = "TON";
    ton.instance = "T1";
    ton.pins = { Pin{ "IN", "", false, PinDir::In, {}, {} }, Pin{ "PT", "t#100ms", false, PinDir::In, {}, {} },
                 Pin{ "Q", "", false, PinDir::Out, {}, {} }, Pin{ "ET", "TE", false, PinDir::Out, {}, {} } };
    fitBlock(ton);
    placeBlock(d, Pos{ n, 0, 2 }, ton);
    placeCoil(d, Pos{ n, 0, 10 }, CoilType::Normal, "TQ");
    // 6 : [X > 5] -> GT5 ; A -> [CNT := CNT + 1]
    n = insertNetwork(d, 7);
    placeCompare(d, Pos{ n, 0, 0 }, "X > 5");
    setName(d, Pos{ n, 0, 10 }, "GT5");
    n = insertNetwork(d, 8);
    for (int c = 0; c < 11; ++c) erase(d, Pos{ n, 0, c });
    placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "A");
    placeOperate(d, Pos{ n, 0, 8 }, "CNT := CNT + 1;", 3);
    // 7 : une fonction : SUM := ADD(X, 10) ; GT : X >= 3 -> G3 par un EFB GE_INT cable
    n = insertNetwork(d, 9);
    for (int c = 0; c < 11; ++c) erase(d, Pos{ n, 0, c });
    Block add;
    add.type = "ADD_INT";
    add.pins = { Pin{ "IN1", "X", false, PinDir::In, {}, {} }, Pin{ "IN2", "10", false, PinDir::In, {}, {} },
                 Pin{ "OUT", "SUM", false, PinDir::Out, {}, {} } };
    fitBlock(add);
    placeBlock(d, Pos{ n, 0, 3 }, add);
    return write(d);
}

void testSimulation() {
    std::printf("3. la simulation\n");
    auto proj = project::ProjectStore::createEmpty("Essai_LD_SIM", "BMXP342020");
    core::CommandStack stack;
    for (const char* v : { "A", "B", "C", "D", "S", "R", "Q", "P1", "N1", "TQ", "GT5", "FROMST" }) {
        project::AddVariableCommand::Spec s;
        s.name = v;
        s.type = "BOOL";
        s.scope = domain::VariableScope::Global;
        (void)stack.push(std::make_unique<project::AddVariableCommand>(proj, s));
    }
    for (auto [v, t] : { std::pair{ "T1", "TON" }, std::pair{ "X", "INT" }, std::pair{ "CNT", "INT" }, std::pair{ "SUM", "INT" },
                         std::pair{ "TE", "TIME" } }) {
        project::AddVariableCommand::Spec s;
        s.name = v;
        s.type = t;
        s.scope = domain::VariableScope::Global;
        (void)stack.push(std::make_unique<project::AddVariableCommand>(proj, s));
    }
    (void)stack.push(std::make_unique<project::AddSectionCommand>(proj, "Essai_LD", "MAST", domain::PouLanguage::LD));
    const auto si = static_cast<domain::Index>(proj->sections.size() - 1);
    (void)stack.push(std::make_unique<LadderEditCommand>(proj, si, ladderBody(), "Essai"));
    // Une section ST APRES le ladder : elle lit ce qu'il a ecrit dans le meme cycle.
    (void)stack.push(std::make_unique<project::AddSectionCommand>(proj, "Apres", "MAST", domain::PouLanguage::ST));
    (void)stack.push(std::make_unique<project::SetSectionBodyCommand>(
        proj, static_cast<domain::Index>(proj->sections.size() - 1), "FROMST := C;\n"));

    sim::Runtime rt(proj);
    const auto prepared = rt.prepare();
    check(prepared.has_value(), "le runtime se prepare avec la section LD");
    for (const auto& dgn : rt.preparationDiagnostics())
        std::printf("    (preparation) %s : %s\n", dgn.section.c_str(), dgn.message.c_str());
    bool noLdWarning = true;
    for (const auto& dgn : rt.preparationDiagnostics()) noLdWarning = noLdWarning && dgn.message.find("Structured Text only") == std::string::npos;
    check(noLdWarning, "plus de \"ST seulement\" pour la section LD");
    auto get = [&](const char* n) { sim::Value v; (void)rt.get(n, v); return v; };
    auto set = [&](const char* n, sim::Value v) { (void)rt.set(n, v); };

    (void)rt.step(20);
    check(!get("C").isTruthy() && !get("D").isTruthy(), "A=0, B=0 : C=0, D=0");
    set("A", sim::Value::boolean(true));
    auto rep = rt.step(20);
    for (const auto& dgn : rep.diagnostics) std::printf("    (cycle) %s : %s\n", dgn.section.c_str(), dgn.message.c_str());
    check(get("C").isTruthy() && get("D").isTruthy(), "A=1, B=0 : C=1 (serie), D=1 (parallele)");
    check(get("FROMST").isTruthy(), "la section ST suivante lit C dans le meme cycle");
    check(get("P1").isTruthy(), "contact P : P1 vrai au cycle du front");
    check(get("CNT").asInteger() == 1, "operate : CNT := CNT + 1 execute (alimente)");
    (void)rt.step(20);
    check(!get("P1").isTruthy(), "contact P : P1 retombe au cycle suivant");
    check(get("CNT").asInteger() == 2, "operate : une fois par cycle");
    set("B", sim::Value::boolean(true));
    (void)rt.step(20);
    check(!get("C").isTruthy() && get("D").isTruthy(), "A=1, B=1 : C=0 (contact ferme), D=1");
    set("A", sim::Value::boolean(false));
    set("B", sim::Value::boolean(false));
    (void)rt.step(20);
    check(get("N1").isTruthy(), "bobine N : N1 vrai au cycle ou le courant tombe");
    (void)rt.step(20);
    check(!get("N1").isTruthy(), "bobine N : puis faux");
    check(!get("D").isTruthy(), "A=0, B=0 : D=0");

    set("S", sim::Value::boolean(true));
    (void)rt.step(20);
    set("S", sim::Value::boolean(false));
    (void)rt.step(20);
    check(get("Q").isTruthy(), "bobine S : Q reste a 1");
    set("R", sim::Value::boolean(true));
    (void)rt.step(20);
    check(!get("Q").isTruthy(), "bobine R : Q retombe");
    set("R", sim::Value::boolean(false));

    // TON : 100 ms au pas de 20 ms
    set("A", sim::Value::boolean(true));
    int scans = 0;
    while (scans < 20 && !get("TQ").isTruthy()) { (void)rt.step(20); ++scans; }
    std::printf("    TON : TQ apres %d cycles, ET = %s\n", scans, get("TE").display().c_str());
    check(scans >= 5 && scans <= 7, "TON (PT t#100ms) : TQ apres ~100 ms");
    check(get("T1.Q").isTruthy() && get("TE").asInteger() == 100, "TON : T1.Q et ET (sortie cablee a TE)");
    set("A", sim::Value::boolean(false));
    (void)rt.step(20);
    check(!get("TQ").isTruthy(), "TON : IN tombe, Q tombe");

    set("X", sim::Value::integer(sim::Type::Int, 7));
    (void)rt.step(20);
    check(get("GT5").isTruthy(), "compare [X > 5] : vrai pour X=7");
    check(get("SUM").asInteger() == 17, "EFB ADD_INT : SUM = X + 10");
    set("X", sim::Value::integer(sim::Type::Int, 2));
    (void)rt.step(20);
    check(!get("GT5").isTruthy() && get("SUM").asInteger() == 12, "X=2 : GT5 faux, SUM = 12");

    // forcer une entree lue par un contact
    check(rt.force("A", sim::Value::boolean(true)), "forcer A");
    (void)rt.step(20);
    check(get("C").isTruthy(), "le forcage passe par les contacts");
    (void)rt.unforce("A");

    // l'etat en direct
    const auto* live = rt.ladderLive("Essai_LD");
    check(live && live->networks.size() >= 9, "ladderLive : un etat par reseau");
    if (live && live->networks.size() >= 9) {
        set("A", sim::Value::boolean(true));
        set("B", sim::Value::boolean(false));
        (void)rt.step(20);
        live = rt.ladderLive("Essai_LD");
        const auto& n0 = live->networks[0];
        check(n0.ran && n0.node.size() >= 1 && n0.node[0][0] == 1 && n0.node[0][1] == 1 && n0.node[0][2] == 1
                  && n0.cell[0][0] == 1 && n0.cell[0][1] == 1 && n0.cell[0][10] == 1,
              "en direct : le courant traverse A, /B, et C est active");
        const auto& nt = live->networks[6];
        check(nt.blocks.size() == 1 && nt.blocks[0].ran && nt.blocks[0].pinValues.size() == 4
                  && nt.blocks[0].pinValues[0] == "TRUE", "en direct : le bloc TON, IN = TRUE");
        std::printf("    TON en direct : IN=%s PT=%s Q=%s ET=%s\n", nt.blocks[0].pinValues[0].c_str(),
                    nt.blocks[0].pinValues[1].c_str(), nt.blocks[0].pinValues[2].c_str(), nt.blocks[0].pinValues[3].c_str());
    }
    check(rt.ladderSections().size() == 1, "ladderSections : la section LD");

    // ---- sans le runtime : les memes regles sur un environnement nu
    {
        MapEnv env;
        for (const char* v : { "A", "B", "C", "T1" }) env.vars[v] = sim::Value::boolean(false);
        auto d = mustParse(emptySourceXml(11, 2));
        setName(d, Pos{ 0, 0, 0 }, "x");   // rien : case vide
        deleteNetwork(d, 0);
        const int n = insertNetwork(d, 0);
        placeContact(d, Pos{ n, 0, 0 }, ContactType::Open, "A");
        setName(d, Pos{ n, 0, 10 }, "T1");
        const int m = insertNetwork(d, 1);
        placeContact(d, Pos{ m, 0, 0 }, ContactType::Open, "B");
        setName(d, Pos{ m, 0, 10 }, "C");
        const auto xml = write(d);
        auto prog = compileXml(xml, "T1");
        check(prog.has_value(), "compileXml (API de l'agent S)");
        if (prog) {
            State st;
            env.vars["A"] = sim::Value::boolean(true);
            env.vars["B"] = sim::Value::boolean(true);
            auto r = evaluateCondition(**prog, env, st, "T1");
            check(r.has_value() && *r && !env.b("T1") && env.b("C"),
                  "evaluateCondition : la bobine T1 rend le courant sans ecrire ; les autres ecrivent");
            env.vars["A"] = sim::Value::boolean(false);
            r = evaluateCondition(**prog, env, st, "T1");
            check(r.has_value() && !*r, "evaluateCondition : A=0 -> faux");
            auto none = evaluateCondition(**prog, env, st, "T9");
            check(!none.has_value(), "pas de bobine T9 : une erreur");
            env.vars.erase("B");
            auto bad = run(**prog, env, st);
            check(!bad.ok && !env.reports.empty(), "une variable inconnue : une erreur dite");
        }
        // le ladder riche : saut et RETURN
        MapEnv e2;
        for (const char* v : { "Marche", "Arret", "Moteur", "Capteur", "Fin", "Defaut" }) e2.vars[v] = sim::Value::boolean(false);
        e2.vars["Niveau"] = sim::Value::integer(sim::Type::Int, 50);
        e2.vars["Compteur"] = sim::Value::integer(sim::Type::Int, 0);
        auto rich = compileXml(kRich, "Riche");
        check(rich.has_value() && !compileWarnings(**rich).empty(), "kRich compile (l'element inconnu est signale)");
        if (rich) {
            State st;
            e2.vars["Marche"] = sim::Value::boolean(true);
            auto r = run(**rich, e2, st);
            check(r.ok && e2.b("Moteur"), "kRich : Marche, /Arret, [Niveau < 80] -> Moteur");
            e2.vars["Marche"] = sim::Value::boolean(false);
            r = run(**rich, e2, st);
            check(e2.b("Moteur"), "kRich : l'auto-maintien (Moteur en parallele de Marche)");
            e2.vars["Niveau"] = sim::Value::integer(sim::Type::Int, 90);
            r = run(**rich, e2, st);
            check(!e2.b("Moteur"), "kRich : Niveau 90 -> la comparaison coupe");
            e2.vars["Capteur"] = sim::Value::boolean(true);
            (void)run(**rich, e2, st);
            e2.vars["Capteur"] = sim::Value::boolean(false);
            (void)run(**rich, e2, st);
            check(e2.vars["Compteur"].asInteger() == 1, "kRich : [N Capteur] -> Compteur + 1 une fois");
            e2.vars["Defaut"] = sim::Value::boolean(true);
            r = run(**rich, e2, st);
            check(r.returned, "kRich : RETURN pris");
            e2.vars["Defaut"] = sim::Value::boolean(false);
            e2.vars["Fin"] = sim::Value::boolean(true);
            r = run(**rich, e2, st, RunOptions{});
            check(r.ok && r.networksRun > 2, "kRich : ->> SUITE (un saut en arriere, borne)");
        }
    }
}

// ------------------------------------------------------------------ 4. references, renommage
void testNames() {
    std::printf("4. les noms\n");
    auto d = mustParse(kLd);
    const auto uses = namesUsed(d);
    int writes = 0, reads = 0;
    for (const auto& u : uses) (u.write ? writes : reads) += 1;
    check(writes == 2 && reads >= 4, "namesUsed : MT_PROD et MANQUE_TENSION ecrits ; contacts, PT, instance lus");
    auto r = d;
    const auto count = renameVariable(r, "E64A", "ENTREES");
    check(count == 2 && cell(r, 0, 0, 0).name == "ENTREES[6]" && cell(r, 0, 1, 0).name == "ENTREES[5]",
          "renameVariable : E64A[6] et E64A[5] -> ENTREES[...]");
    check(renameVariable(r, "TON_MT", "TEMPO") == 1 && r.networks[0].blocks[0].instance == "TEMPO", "renommer l'instance");
    check(renameVariable(r, "X1", "ETAPE1") == 1 && cell(r, 0, 0, 1).name == "ETAPE1.X", "renommer la racine d'un chemin");
    check(renameInText("X1.X AND NOT x1 OR AX1 OR S.X1", "x1", "Y") == "Y.X AND NOT Y OR AX1 OR S.X1",
          "renameInText : sans la casse, mots entiers, pas un membre");
    const auto out = write(r);
    check(out.find("ENTREES[6]") != std::string::npos && out.find("E64A") == std::string::npos, "renomme : ecrit");

    // references croisees et renommage par le projet
    auto proj = project::ProjectStore::createEmpty("Essai_LD_REF", "BMXP342020");
    core::CommandStack stack;
    (void)stack.push(std::make_unique<project::AddSectionCommand>(proj, "Ladder", "MAST", domain::PouLanguage::LD));
    const auto si = static_cast<domain::Index>(proj->sections.size() - 1);
    (void)stack.push(std::make_unique<LadderEditCommand>(proj, si, kLd, "Essai"));
    const auto x = project::crossReference(*proj, "MT_PROD");
    check(x.writes.size() == 1 && x.reads.empty(), "references : MT_PROD ecrit par la bobine S");
    const auto e = project::crossReference(*proj, "E64A");
    check(e.reads.size() == 2, "references : E64A lu deux fois");
}

} // namespace

int main() {
    std::printf("1.51.0 : le ladder dans API Studio.\n");
    testFormat();
    testEditor();
    testSimulation();
    testNames();
    std::printf(failures ? "\n%d echec(s) sur %d\n" : "\ntout est bon (%d verifications)\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
