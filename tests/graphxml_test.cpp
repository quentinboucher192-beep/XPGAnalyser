// =============================================================================
//  tests/graphxml_test.cpp - 1.12.24 : un ladder et un GRAFCET gardes tels quels
// -----------------------------------------------------------------------------
//  Une section LD, FBD ou SFC n'est pas du texte : jusque-la, l'import n'en
//  gardait que les bribes de texte entre les balises, et un GRAFCET natif
//  (SFCProgram) devenait autant de sections LD que de receptivites, toutes du nom
//  du GRAFCET. La reexportation rendait un programme que Control Expert refuse.
//
//  Ce qui est verifie ici, sur un petit XPG ecrit comme Control Expert l'ecrit :
//    - chaque schema est lu en entier, octet pour octet (le XML garde) ;
//    - le GRAFCET est UNE section SFC, avec son nom et son ordre ;
//    - la reexportation rend chaque schema a l'identique, et se relit pareil ;
//    - le dossier du projet range un schema en .xml et le relit pareil ;
//    - la lecture en texte et les references croisees (bobine = ecriture) ;
//    - les variables : l'ordre de Control Expert (commentaire, attributs, valeur
//      initiale, elements), les valeurs imbriquees des voies d'un module, et la
//      periode de la tache MAST (valueType) ;
//    - un DDT d'equipement : sa date, son commentaire et ses attributs restent les
//      siens (le commentaire ne se pose plus sur la derniere variable lue).
// =============================================================================
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectParser.hpp"
#include "../src/project/CrossReference.hpp"
#include "../src/project/GraphListing.hpp"
#include "../src/project/ProjectStore.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

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

const char* kSfc =
    "<SFCProgram areaNum=\"0\" operatorCtrl=\"0\">\r\n"
    "\t\t\t<identProgram name=\"G7\" type=\"section\" SectionOrder=\"2\"></identProgram>\r\n"
    "\t\t\t<chartSource name=\"Chart\">\r\n"
    "\t\t\t\t<networkSFC>\r\n"
    "\t\t\t\t\t<step stepType=\"initialStep\" stepName=\"X0\">\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"1\" posY=\"1\"></objPosition>\r\n"
    "\t\t\t\t\t</step>\r\n"
    "\t\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"1\" posY=\"2\"></objPosition>\r\n"
    "\t\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t\t<variableName>CMD_MARCHE</variableName>\r\n"
    "\t\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t\t</transition>\r\n"
    "\t\t\t\t\t<step stepType=\"step\" stepName=\"X1\">\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"1\" posY=\"3\"></objPosition>\r\n"
    "\t\t\t\t\t</step>\r\n"
    "\t\t\t\t\t<transition>\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"1\" posY=\"4\"></objPosition>\r\n"
    "\t\t\t\t\t\t<transitionCondition invertLogic=\"false\">\r\n"
    "\t\t\t\t\t\t\t<sectionName>T1</sectionName>\r\n"
    "\t\t\t\t\t\t</transitionCondition>\r\n"
    "\t\t\t\t\t</transition>\r\n"
    "\t\t\t\t\t<jumpSFC stepName=\"X0\">\r\n"
    "\t\t\t\t\t\t<objPosition posX=\"1\" posY=\"5\"></objPosition>\r\n"
    "\t\t\t\t\t</jumpSFC>\r\n"
    "\t\t\t\t</networkSFC>\r\n"
    "\t\t\t</chartSource>\r\n"
    "\t\t\t<transitionSource name=\"T1\">\r\n"
    "\t\t\t\t<LDSource nbColumns=\"11\">\r\n"
    "\t\t\t\t\t<networkLD>\r\n"
    "\t\t\t\t\t\t<typeLine>\r\n"
    "\t\t\t\t\t\t\t<shortCircuit>\r\n"
    "\t\t\t\t\t\t\t\t<VLink></VLink>\r\n"
    "\t\t\t\t\t\t\t\t<compareBlock width=\"2\">\r\n"
    "\t\t\t\t\t\t\t\t\t<expression>X1.T &gt;= T_SEQ_MAX</expression>\r\n"
    "\t\t\t\t\t\t\t\t</compareBlock>\r\n"
    "\t\t\t\t\t\t\t</shortCircuit>\r\n"
    "\t\t\t\t\t\t\t<HLink nbCells=\"8\"></HLink>\r\n"
    "\t\t\t\t\t\t\t<coil typeCoil=\"coil\" coilVariableName=\"T1\"></coil>\r\n"
    "\t\t\t\t\t\t</typeLine>\r\n"
    "\t\t\t\t\t</networkLD>\r\n"
    "\t\t\t\t</LDSource>\r\n"
    "\t\t\t</transitionSource>\r\n"
    "\t\t</SFCProgram>";

// Une variable de module d'E/S : ses reglages de voie sont des elements imbriques
// qui portent des valeurs. Et un tableau dont les elements sont commentes.
const char* kAmo =
    "\t\t<variables name=\"AMO\" typeName=\"T_U_ANA_STD_OUT_8\">\r\n"
    "\t\t\t<comment>Sorties variateurs</comment>\r\n"
    "\t\t\t<attribute name=\"ManagedKey\" value=\"K1\"></attribute>\r\n"
    "\t\t\t<attribute name=\"Owner\" value=\"\\2.2\\0.5\"></attribute>\r\n"
    "\t\t\t<instanceElementDesc name=\"ANA_CH_OUT\">\r\n"
    "\t\t\t\t<instanceElementDesc name=\"[0]\">\r\n"
    "\t\t\t\t\t<instanceElementDesc name=\"FCT_TYPE\">\r\n"
    "\t\t\t\t\t\t<value>16#0001</value>\r\n"
    "\t\t\t\t\t</instanceElementDesc>\r\n"
    "\t\t\t\t</instanceElementDesc>\r\n"
    "\t\t\t</instanceElementDesc>\r\n"
    "\t\t</variables>\r\n";

const char* kTab =
    "\t\t<variables name=\"TAB\" typeName=\"ARRAY[0..1] OF BOOL\">\r\n"
    "\t\t\t<comment>Deux voies</comment>\r\n"
    "\t\t\t<variableInit value=\"[2(FALSE)]\"></variableInit>\r\n"
    "\t\t\t<instanceElementDesc name=\"[0]\">\r\n"
    "\t\t\t\t<comment>voie 0</comment>\r\n"
    "\t\t\t</instanceElementDesc>\r\n"
    "\t\t\t<instanceElementDesc name=\"[1]\">\r\n"
    "\t\t\t\t<comment>voie 1</comment>\r\n"
    "\t\t\t</instanceElementDesc>\r\n"
    "\t\t</variables>\r\n";

const char* kDdt =
    "\t<DDTSource DDTName=\"T_DEV\" version=\"0.01\" dateTime=\"dt#2024-05-14-10:20:30\">\r\n"
    "\t\t<comment>Device DDT de test</comment>\r\n"
    "\t\t<attribute name=\"IsTypeLocked\" value=\"true\"></attribute>\r\n"
    "\t\t<attribute name=\"IsDeviceDDT\" value=\"true\"></attribute>\r\n"
    "\t\t<structure>\r\n"
    "\t\t\t<variables name=\"ETAT\" typeName=\"INT\">\r\n"
    "\t\t\t\t<comment>Etat</comment>\r\n"
    "\t\t\t</variables>\r\n"
    "\t\t</structure>\r\n"
    "\t</DDTSource>\r\n";

std::string document() {
    std::string d =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<PGMExchangeFile>\r\n"
        "\t<fileHeader company=\"Schneider Automation\" product=\"Control Expert V15.3 - 230214C\" dateTime=\"date_and_time#2026-10-22-10:0:0\" content=\"Fichier source multiprogramme\" DTDVersion=\"41\"></fileHeader>\r\n"
        "\t<contentHeader name=\"Projet\" version=\"0.0.1\"></contentHeader>\r\n"
        "\t<logicConf>\r\n"
        "\t\t<resource resName=\"M580\" resIdent=\"BME H58 2040 03.20\">\r\n"
        "\t\t\t<taskDesc task=\"MAST\" taskType=\"periodic\" valueType=\"100\" maxExecTime=\"250\">\r\n"
        "\t\t\t\t<sectionDesc name=\"Init\" SectionOrder=\"1\"></sectionDesc>\r\n"
        "\t\t\t\t<programUnitDesc name=\"TE\" SectionOrder=\"2\"></programUnitDesc>\r\n"
        "\t\t\t</taskDesc>\r\n"
        "\t\t</resource>\r\n"
        "\t</logicConf>\r\n"
        "\t<program>\r\n"
        "\t\t<identProgram name=\"Init\" type=\"section\" task=\"MAST\" SectionOrder=\"1\"></identProgram>\r\n"
        "\t\t<STSource>A := TRUE;\r\n</STSource>\r\n"
        "\t</program>\r\n"
        "\t<programUnit name=\"TE\">\r\n"
        "\t\t<privateLocalVariables>\r\n"
        "\t\t\t<variables name=\"MT_PROD\" typeName=\"EBOOL\"></variables>\r\n"
        "\t\t</privateLocalVariables>\r\n"
        "\t\t<program>\r\n"
        "\t\t\t<identProgram name=\"MT\" type=\"section\" SectionOrder=\"1\"></identProgram>\r\n"
        "\t\t\t";
    d += kLd;
    d += "\r\n\t\t</program>\r\n\t\t";
    d += kSfc;
    d += "\r\n"
        "\t</programUnit>\r\n"
        "\t<dataBlock>\r\n"
        "\t\t<variables name=\"A\" typeName=\"BOOL\"></variables>\r\n";
    d += kAmo;
    d += kTab;
    d += "\t</dataBlock>\r\n";
    d += kDdt;
    d += "</PGMExchangeFile>\r\n";
    return d;
}

const domain::Section* sectionNamed(const domain::Project& p, std::string_view name) {
    for (const auto& s : p.sections)
        if (p.strings.text(s.name) == name) return &s;
    return nullptr;
}

std::size_t sectionsNamed(const domain::Project& p, std::string_view name) {
    std::size_t n = 0;
    for (const auto& s : p.sections) n += p.strings.text(s.name) == name ? 1 : 0;
    return n;
}

bool parse(const std::string& text, domain::Project& p) {
    importer::XpgParser parser;
    const auto outcome = parser.parse(text, p, importer::ParseOptions{}, {});
    if (!outcome) std::printf("  parse : %s\n", outcome.error().message().c_str());
    return static_cast<bool>(outcome);
}

} // namespace

int main() {
    std::printf("1.12.24 : un ladder et un GRAFCET gardes tels quels.\n");
    const auto text = document();

    domain::Project p;
    check(parse(text, p), "le petit XPG se lit");

    const auto* mt = sectionNamed(p, "MT");
    check(mt && mt->language == domain::PouLanguage::LD, "MT : une section LD");
    check(mt && mt->body == kLd, "MT : son XML est garde octet pour octet");
    check(mt && domain::keepsSourceXml(*mt), "MT : keepsSourceXml");

    const auto* g7 = sectionNamed(p, "G7");
    check(sectionsNamed(p, "G7") == 1, "G7 : une seule section (plus une section LD par receptivite)");
    check(g7 && g7->language == domain::PouLanguage::SFC, "G7 : une section SFC");
    check(g7 && g7->order == 2, "G7 : son ordre");
    check(g7 && g7->body == kSfc, "G7 : tout le SFCProgram garde octet pour octet");
    check(g7 && g7->owner != domain::kNoIndex && g7->owner < p.pous.size()
              && p.strings.text(p.pous[g7->owner].name) == "TE",
          "G7 : dans l'unite TE");
    check(sectionNamed(p, "Init") && sectionNamed(p, "Init")->body == "A := TRUE;\r\n", "Init : le ST reste du texte");
    check(!p.tasks.empty() && p.tasks[0].period == 100, "MAST periodique : 100 ms lus dans valueType");
    const domain::Variable* amo = nullptr;
    const domain::Variable* tab = nullptr;
    for (const auto& v : p.variables) {
        if (p.strings.text(v.name) == "AMO") amo = &v;
        if (p.strings.text(v.name) == "TAB") tab = &v;
    }
    check(amo && amo->instanceElements.size() == 1 && amo->instanceElements[0].name == "ANA_CH_OUT"
              && amo->instanceElements[0].raw.find("<value>16#0001</value>") != std::string::npos
              && amo->instanceElements[0].raw.find('\r') == std::string::npos,
          "AMO : un element imbrique, garde avec ses valeurs (en LF)");
    check(tab && tab->instanceElements.size() == 2 && tab->instanceElements[1].name == "[1]"
              && p.strings.text(tab->instanceElements[1].comment) == "voie 1" && tab->instanceElements[1].raw.empty()
              && p.strings.text(tab->comment) == "Deux voies",
          "TAB : deux elements commentes (nom + commentaire), le commentaire de la variable a part");
    check(p.derivedTypes.size() == 1 && p.strings.text(p.derivedTypes[0].comment) == "Device DDT de test"
              && p.derivedTypes[0].attributes.size() == 2 && p.derivedTypes[0].dateTime == "dt#2024-05-14-10:20:30",
          "T_DEV : commentaire, attributs et date du type");

    // ---- reexportation
    exporter::XpgWriteOptions wo;
    wo.dateTime = "date_and_time#2026-10-22-10:0:0";
    const auto out = exporter::writeXpg(p, wo);
    check(static_cast<bool>(out), "la reexportation s'ecrit");
    if (out) {
        check(out->find(kLd) != std::string::npos, "le ladder ressort tel quel");
        check(out->find(kSfc) != std::string::npos, "le GRAFCET ressort tel quel (son identProgram compris)");
        check(out->find("<SFCSource>") == std::string::npos, "pas de SFCSource invente");
        check(out->find(kAmo) != std::string::npos, "AMO ressort telle quelle (ordre, valeurs imbriquees)");
        check(out->find(kTab) != std::string::npos, "TAB ressort telle quelle (commentaire, valeur initiale, elements)");
        check(out->find(kDdt) != std::string::npos, "T_DEV ressort tel quel (date, commentaire, attributs, champ)");
        check(out->find("<taskDesc task=\"MAST\" taskType=\"periodic\" valueType=\"100\" maxExecTime=\"250\">") != std::string::npos,
              "la periode de la MAST ressort dans valueType");
        {
            std::size_t k = 0;
            while (k < out->size() && k < text.size() && (*out)[k] == text[k]) ++k;
            if (k < text.size() || k < out->size())
                std::printf("  (premier ecart avec l'original a l'octet %zu : [%s])\n", k, out->substr(k, 60).c_str());
        }
        domain::Project again;
        check(parse(*out, again), "la reexportation se relit");
        const auto* mt2 = sectionNamed(again, "MT");
        const auto* g72 = sectionNamed(again, "G7");
        check(mt2 && mt && mt2->body == mt->body, "relue : le meme ladder");
        check(g72 && g7 && g72->body == g7->body && g72->order == g7->order, "relue : le meme GRAFCET");
        const auto twice = exporter::writeXpg(again, wo);
        check(twice && *twice == *out, "une deuxieme reexportation est identique a la premiere");
    }

    // ---- un GRAFCET renomme et deplace : son identProgram suit le modele
    if (auto* g = const_cast<domain::Section*>(sectionNamed(p, "G7"))) {
        g->name = p.strings.intern("G7_SEQ");
        g->order = 3;
        const auto moved = exporter::writeXpg(p, wo);
        check(moved && moved->find("<identProgram name=\"G7_SEQ\" type=\"section\" SectionOrder=\"3\"></identProgram>") != std::string::npos,
              "renomme et deplace : l'identProgram du GRAFCET est refait");
        check(moved && moved->find("<chartSource name=\"Chart\">") != std::string::npos, "renomme : le graphe reste");
        g->name = p.strings.intern("G7");
        g->order = 2;
    }

    // ---- le dossier du projet
    const auto folder = (std::filesystem::temp_directory_path() / "graphxml_test_projet").string();
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    project::Manifest m;
    m.name = "graphxml";
    const auto saved = project::ProjectStore::save(p, m, folder);
    check(static_cast<bool>(saved), "le projet se range en dossier");
    check(std::filesystem::exists(std::filesystem::path(folder) / "units" / "TE" / "code" / "MT.xml"),
          "le ladder est range en .xml");
    check(std::filesystem::exists(std::filesystem::path(folder) / "units" / "TE" / "code" / "G7.xml"),
          "le GRAFCET est range en .xml");
    if (saved) {
        const auto opened = project::ProjectStore::open(folder);
        check(opened && opened->project, "le dossier se rouvre");
        if (opened && opened->project) {
            const auto& q = *opened->project;
            const auto* mt3 = sectionNamed(q, "MT");
            const auto* g73 = sectionNamed(q, "G7");
            check(mt3 && mt3->body == kLd && mt3->language == domain::PouLanguage::LD, "rouvert : le meme ladder");
            check(g73 && g73->body == kSfc && g73->language == domain::PouLanguage::SFC, "rouvert : le meme GRAFCET");
            const auto again = exporter::writeXpg(q, wo);
            check(again && again->find(kLd) != std::string::npos && again->find(kSfc) != std::string::npos,
                  "rouvert puis reexporte : les deux schemas tels quels");
            check(again && again->find(kAmo) != std::string::npos && again->find(kTab) != std::string::npos,
                  "rouvert puis reexporte : AMO et TAB telles quelles");
            check(again && again->find(kDdt) != std::string::npos, "rouvert puis reexporte : T_DEV tel quel");
            check(!q.tasks.empty() && q.tasks[0].period == 100, "rouvert : la MAST garde sa periode");
        }
    }
    std::filesystem::remove_all(folder, ec);

    // ---- la lecture en texte
    if (mt) {
        const auto listing = project::graphListingText(*mt);
        std::printf("%s\n", listing.c_str());
        check(listing.find("[/E64A[6]]") != std::string::npos, "lecture : le contact inverse");
        check(listing.find("[P E64A[5]]") != std::string::npos, "lecture : le front montant");
        check(listing.find("(S MT_PROD)") != std::string::npos, "lecture : la bobine mise a 1");
        check(listing.find("|") != std::string::npos, "lecture : la liaison verticale");
        check(listing.find("{TON_MT} : TON TON_MT") != std::string::npos && listing.find("PT := t#1s") != std::string::npos,
              "lecture : le bloc et ses broches");
        const auto lines = project::graphListing(*mt);
        bool coil = false;
        for (const auto& l : lines)
            for (const auto& w : l.writes) coil = coil || w == "MT_PROD";
        check(coil, "lecture : MT_PROD est ecrit par la bobine");
    }
    if (g7) {
        const auto listing = project::graphListingText(*g7);
        std::printf("%s\n", listing.c_str());
        check(listing.find("\xC3\xA9tape X0 (initiale)") != std::string::npos, "GRAFCET : l'etape initiale");
        check(listing.find("transition CMD_MARCHE") != std::string::npos, "GRAFCET : la transition sur variable");
        check(listing.find("saut vers X0") != std::string::npos, "GRAFCET : le saut");
        check(listing.find("[X1.T >= T_SEQ_MAX]") != std::string::npos, "GRAFCET : la receptivite LD et sa comparaison");
    }

    // ---- les references croisees
    {
        const auto x = project::crossReference(p, "MT_PROD");
        check(x.writes.size() == 1 && x.reads.empty(), "references : MT_PROD ecrit une fois dans le ladder");
        const auto e = project::crossReference(p, "E64A");
        check(e.reads.size() == 2, "references : E64A lu deux fois (E64A[6], E64A[5])");
        const auto t = project::crossReference(p, "T_SEQ_MAX");
        check(t.reads.size() == 1, "references : T_SEQ_MAX lu dans la receptivite du GRAFCET");
        const auto c = project::crossReference(p, "CMD_MARCHE");
        check(c.reads.size() == 1, "references : CMD_MARCHE lu par la transition");
    }

    std::printf(failures ? "\n%d echec(s)\n" : "\ntout est bon\n", failures);
    return failures ? 1 : 0;
}
