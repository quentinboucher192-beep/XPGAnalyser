// =============================================================================
//  tests/macros2_test.cpp - lot macros 1 : le formulaire, les dossiers, la
//  memoire, la session, et le Ctrl+Z d'une macro qui importe
// -----------------------------------------------------------------------------
//      macros2_test <MAST.XPG> [libs]
//
//  1. MacroSpec : les lignes #! (champ, libelle, option, groupe, avance,
//     exemple, tableau, lit, produit, appliquer, categorie), les motifs, les
//     verifications de nom et de nombre. Avec [libs] : les 31 macros livrees
//     se lisent sans un seul probleme.
//  2. MacroFolders : le rangement (categories, dossiers ecrits, gestes) ; une
//     macro rangee explicitement n'impose plus le dossier de sa categorie.
//  3. MacroMemory : les reponses par projet, les profils, les recents, le disque.
//  4. MacroSession : les tours, les reponses, Appliquer = une commande.
//  5. LibImport dans une macro appliquee : UN Ctrl+Z rend le projet a l'identique
//     (il echouait : "cannot undo the creation of this section"), et le
//     retablir refait tout.
// =============================================================================
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/Macro.hpp"
#include "../src/project/MacroFolders.hpp"
#include "../src/project/MacroMemory.hpp"
#include "../src/project/MacroSession.hpp"
#include "../src/project/MacroSpec.hpp"
#include "../src/project/SharedLibrary.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mm = project::macro;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  ECHEC  %s\n", what.c_str()); ++failures; }
}

void same(const std::string& got, const std::string& want, const std::string& what) {
    if (got != want) {
        std::printf("  ECHEC  %s : attendu [%s], obtenu [%s]\n", what.c_str(), want.c_str(), got.c_str());
        ++failures;
    }
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// ---------------------------------------------------------------- 1. spec ----
void testSpec(const std::string& libs) {
    std::puts("1. MacroSpec");
    const std::string source =
        "(* Essai\n"
        "   #! summary = Un essai.\n"
        "   #! version = 1.20\n"
        "   #! categorie = Importer/Etapes\n"
        "   #! champ classeur = fichier .xlsx .xlsm facultatif\n"
        "   #! libelle classeur = Le classeur de l'affaire\n"
        "   #! champ tache = tache\n"
        "   #! champ sev = nombre 1..4 : 1 Information, 2 Avertissement, 3 Defaut, 4 Arret\n"
        "   #! champ ou = choix\n"
        "   #! option ou A = Dans la tache\n"
        "   #! champ prefixe = nom\n"
        "   #! exemple prefixe = {}Pompes, {}Vannes\n"
        "   #! champ m*nom = nom\n"
        "   #! libelle m*nom = Mode {} : son nom\n"
        "   #! groupe Alarmes = sev, ou\n"
        "   #! avance = prefixe\n"
        "   #! tableau es = Le tableau des E/S\n"
        "   #! lit = Config, ES\n"
        "   #! lit-facultatif = Modes\n"
        "   #! produit = des sections\n"
        "   #! appliquer = Ecrire {tache}\n"
        "*)\n"
        "c := Ask('classeur', 'Chemin', '');\n"
        "t := Ask('tache', 'Tache', 'MAST');\n"
        "s := AskNumber('sev', 'Gravite', 1, 4, 3);\n"
        "o := AskChoice('ou', 'Ou ?', 'A,B', 'A');\n"
        "p := Ask('prefixe', 'Prefixe', 'EQ_');\n"
        "AskNow();\n"
        "RunMacro('Autre');\n";
    const auto spec = mm::parseMacroSpec(source, "Essai");
    check(spec.problems.empty(), "aucun probleme dans l'en-tete d'essai");
    for (const auto& p : spec.problems) std::printf("     %s\n", p.c_str());
    same(spec.version, "1.20", "la version");
    same(spec.category, "Importer/Etapes", "le dossier");
    const auto* classeur = spec.field("classeur");
    check(classeur && classeur->kind == mm::FieldKind::File && classeur->optional, "classeur : fichier facultatif");
    check(classeur && classeur->extensions.size() == 2, "classeur : deux extensions");
    same(classeur ? classeur->label : "", "Le classeur de l'affaire", "le libelle avec apostrophe");
    const auto* sev = spec.field("sev");
    check(sev && sev->kind == mm::FieldKind::Number && sev->hasRange && sev->options.size() == 4, "sev : nombre 1..4 a libelles");
    same(sev ? sev->optionLabel("3") : "", "Defaut", "le libelle d'une option");
    const auto* motif = spec.field("m3nom");
    check(motif != nullptr, "un motif couvre m3nom");
    if (motif) same(mm::labelFor(*motif, "m3nom", ""), "Mode 3 : son nom", "le libelle d'un motif");
    same(spec.groupOf("ou"), "Alarmes", "le groupe");
    check(spec.isAdvanced("prefixe"), "prefixe replie");
    const auto* prefixe = spec.field("prefixe");
    if (prefixe) same(mm::exampleFor(*prefixe, "EQ_"), "EQ_Pompes, EQ_Vannes", "l'exemple d'un nom");
    check(spec.reads.size() == 2 && spec.readsOptional.size() == 1, "les onglets lus");
    check(spec.tables.size() == 1 && spec.tables.front().name == "es", "le tableau");
    check(spec.launches.size() == 1 && spec.launches.front() == "Autre", "RunMacro lu dans le code");
    same(mm::applyTextFor(spec, {{"tache", "MAST"}}, "?"), "Ecrire MAST", "le texte d'Appliquer");
    check(mm::checkName("EQ_Pompes", false).verdict == mm::Verdict::Ok, "un nom IEC");
    check(mm::checkName("EQ__Pompes", false).verdict == mm::Verdict::Error, "deux _ de suite refuses");
    check(mm::checkName("\xC3\xA9t\xC3\xA9", false).verdict == mm::Verdict::Error, "un accent refuse");
    check(mm::checkNumber("5", 1, 4).verdict == mm::Verdict::Error, "hors bornes");
    check(mm::checkMacroName("_corbeille").verdict == mm::Verdict::Error, "un nom de macro en _ refuse");

    if (libs.empty()) return;
    std::size_t n = 0, problems = 0;
    for (const auto& e : fs::directory_iterator(fs::path(libs) / "Macros")) {
        if (e.path().extension() != ".mac") continue;
        const auto s = mm::parseMacroSpec(slurp(e.path()), e.path().stem().string());
        ++n;
        problems += s.problems.size();
        for (const auto& p : s.problems) std::printf("     %s : %s\n", e.path().stem().string().c_str(), p.c_str());
        check(!s.category.empty(), e.path().stem().string() + " a un dossier (#! categorie)");
    }
    check(n >= 31, "les 31 macros livrees sont lues (" + std::to_string(n) + ")");
    check(problems == 0, "aucune ligne #! en defaut dans libs/Macros");
    // Les modeles aussi.
    std::size_t models = 0;
    for (const auto& e : fs::directory_iterator(fs::path(libs) / "Macros" / "_modeles")) {
        if (e.path().extension() != ".mac") continue;
        ++models;
        const auto s = mm::parseMacroSpec(slurp(e.path()), e.path().stem().string());
        check(s.problems.empty() && !s.summary.empty(), "le modele " + e.path().stem().string() + " se lit");
    }
    check(models >= 5, "les cinq modeles de libs/Macros/_modeles");
}

// ------------------------------------------------------------- 2. dossiers ----
void testFolders(const fs::path& tmp) {
    std::puts("2. MacroFolders");
    mm::MacroFolders f((tmp / "dossiers.txt").string());
    check(f.load(), "un fichier absent n'est pas une erreur");
    f.setMacros({{"ImporterClasseur", "Importer"}, {"ImporterVoies", "Importer/Etapes"}, {"CreerSR", "Creer"}, {"Libre", ""}});
    auto layout = f.arrange();
    check(layout.hasFolder("Importer") && layout.hasFolder("Importer/Etapes") && layout.hasFolder("Creer"),
          "les dossiers viennent des categories");
    same(layout.folderOf("ImporterVoies"), "Importer/Etapes", "une macro dans un sous-dossier");
    same(layout.folderOf("Libre"), "", "sans categorie : a la racine");
    check(layout.countIn("Importer", true) == 2 && layout.countIn("Importer", false) == 1, "compter a fond ou pas");

    std::string why;
    check(f.addFolder("Mes imports", &why), "creer un dossier");
    check(f.moveMacros({"CreerSR"}, "Mes imports") == 1, "ranger une macro");
    check(f.renameFolder("Mes imports", "Mes outils", &why), "renommer un dossier");
    layout = f.arrange();
    same(layout.folderOf("CreerSR"), "Mes outils", "la macro suit son dossier renomme");
    check(!f.addFolder("", &why), "un dossier sans nom est refuse");
    check(f.removeFolder("Mes outils", &why), "supprimer un dossier");
    layout = f.arrange();
    same(layout.folderOf("CreerSR"), "", "ses macros montent d'un cran");

    // La macro rangee explicitement : sa categorie n'impose plus un dossier
    // qui resterait vide (une macro creee depuis un modele, restauree).
    mm::MacroFolders g((tmp / "dossiers2.txt").string());
    g.setMacros({{"A", "Creer"}, {"Neuve", "Mes macros"}});
    g.place("Neuve", "Creer");
    layout = g.arrange();
    same(layout.folderOf("Neuve"), "Creer", "la macro placee va la ou on l'a mise");
    check(!layout.hasFolder("Mes macros"), "le dossier de sa categorie n'apparait pas vide");

    // Le texte du fichier se relit a l'identique.
    const auto text = g.render();
    mm::MacroFolders h;
    h.parse(text);
    same(h.render(), text, "rendre puis relire le rangement");
    check(g.save().has_value(), "ecrire dossiers.txt");
}

// ------------------------------------------------------------- 3. memoire ----
void testMemory(const fs::path& tmp) {
    std::puts("3. MacroMemory");
    const auto path = (tmp / "memoire.txt").string();
    {
        mm::MacroMemory m(path);
        check(m.load(), "une memoire absente n'est pas une erreur");
        m.remember("C:/Affaires/A.xpgproj", "ImporterClasseur", {{"tache", "MAST"}, {"classeur", "C:/A.xlsm"}});
        m.remember("C:/Affaires/B.xpgproj", "ImporterClasseur", {{"tache", "FAST"}});
        m.saveProfile("ImporterClasseur", "Usine B", {{"tache", "FAST"}, {"sevEq", "4"}});
        for (int i = 0; i < 10; ++i) m.touchFile("C:/f" + std::to_string(i) + ".xlsm");
        m.setFavourite("CreerSR", true);
        m.noteRun("CreerSR", "27/09 18:10", "1 section");
        check(m.save(), "ecrire la memoire");
    }
    mm::MacroMemory m(path);
    check(m.load(), "relire la memoire");
    same(m.answers("C:/Affaires/A.xpgproj", "ImporterClasseur").count("tache") ? m.answers("C:/Affaires/A.xpgproj", "ImporterClasseur").at("tache") : "",
          "MAST", "les reponses par projet");
    same(m.answers("C:/Affaires/B.xpgproj", "ImporterClasseur").at("tache"), "FAST", "l'autre projet garde les siennes");
    check(m.profiles("ImporterClasseur").size() == 1, "un profil");
    same(m.profile("ImporterClasseur", "Usine B").at("sevEq"), "4", "le profil se relit");
    check(m.recentFiles().size() == 8 && m.recentFiles().front() == "C:/f9.xlsm", "huit fichiers recents, le dernier d'abord");
    check(m.favourite("CreerSR") && m.lastRun("CreerSR") != nullptr, "favorite et dernier lancement");
    m.renameMacro("CreerSR", "CreerSousRoutine");
    check(m.favourite("CreerSousRoutine") && !m.favourite("CreerSR"), "le renommage suit");
}

// ------------------------------------------------------------------ outils ----
std::shared_ptr<domain::Project> load(const std::string& xpg) {
    static core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(xpg);
    if (!imported) return nullptr;
    return imported->project;
}

// Tout ce qu'un Ctrl+Z doit rendre : les noms, les corps, les taches.
std::string signature(const domain::Project& p) {
    std::string sig;
    for (const auto& v : p.variables) { sig += p.strings.text(v.name); sig += ';'; }
    for (const auto& s : p.sections) { sig += p.strings.text(s.name); sig += '|'; sig += s.body; }
    for (const auto& d : p.pous) { sig += p.strings.text(d.name); sig += ','; }
    for (const auto& d : p.derivedTypes) { sig += p.strings.text(d.name); sig += ','; }
    for (const auto& t : p.tasks)
        for (const auto i : t.sections) sig += std::to_string(i) + '.';
    return sig;
}

// Une bibliotheque minuscule : un DDT, un DFB avec son corps, deux macros.
void writeLibrary(const fs::path& libs) {
    write(libs / "index.txt",
          "ddt ; Essai ; ST_Essai ; 1.00 ; test ; un type\n"
          "dfb ; Essai ; DFB_ESSAI ; 1.00 ; test ; un bloc\n"
          "mac ; Macros ; ImporteEtCree ; 1.00 ; test ; une SR, un import, une variable\n"
          "mac ; Macros ; Formulaire ; 1.00 ; test ; deux tours\n");
    write(libs / "Essai" / "ST_Essai.ddt",
          "name = ST_Essai\nversion = 1.00\n\nMarche ; BOOL ; Member ;  ; en marche\nDefaut ; BOOL ; Member ;  ; en defaut\n");
    write(libs / "Essai" / "DFB_ESSAI.dfb",
          "name = DFB_ESSAI\nversion = 1.00\n\nEntree ; BOOL ; Input ;  ; \nSortie ; BOOL ; Output ;  ; \n"
          "<<<section Corps ST>>>\nSortie := Entree;\n<<<end>>>\n");
    // L'ordre qui cassait le Ctrl+Z : une sous-routine (un POU et une section),
    // PUIS des imports qui ajoutent des POU et des sections apres elle.
    write(libs / "Macros" / "ImporteEtCree.mac",
          "(* ImporteEtCree\n"
          "   #! summary = Une sous-routine, deux imports, une variable.\n"
          "   #! categorie = Essai\n"
          "*)\n"
          "IF NOT SectionExists('SR_Essai_M2') THEN AddSubroutine('SR_Essai_M2', 'MAST'); END_IF;\n"
          "IF NOT PouExists('DFB_ESSAI') THEN LibImport('DFB_ESSAI'); END_IF;\n"
          "IF NOT TypeExists('ST_Essai') THEN LibImport('ST_Essai'); END_IF;\n"
          "AddVariable('Essai_M2', 'ST_Essai', 'Global', '');\n"
          "AddVariable('Bloc_M2', 'DFB_ESSAI', 'Global', '');\n"
          "IF NOT SectionExists('Essai_M2') THEN AddSection('Essai_M2', 'MAST', 'ST', ''); END_IF;\n"
          "AppendToSection('Essai_M2', 'Bloc_M2(Entree := Essai_M2.Marche);');\n");
    write(libs / "Macros" / "Formulaire.mac",
          "(* Formulaire\n"
          "   #! summary = Deux tours.\n"
          "   #! champ ou = choix\n"
          "   #! champ nom = section nouvelle\n"
          "   #! libelle nom = Nom de la section\n"
          "   #! appliquer = Creer {nom}\n"
          "*)\n"
          "ou := AskChoice('ou', 'Ou ?', 'Tache,Unite', 'Tache');\n"
          "AskNow();\n"
          "nom := Ask('nom', 'Nom', 'Formulaire_M2');\n"
          "AskNow();\n"
          "IF NOT SectionExists(nom) THEN AddSection(nom, 'MAST', 'ST', ''); END_IF;\n");
}

// ------------------------------------------------------------- 4. session ----
void testSession(const std::string& xpg, const fs::path& libs) {
    std::puts("4. MacroSession");
    auto project = load(xpg);
    check(project != nullptr, "le projet d'essai se charge");
    if (!project) return;
    project::SharedLibrary library(libs.string());
    check(library.scan().has_value(), "la bibliotheque d'essai se lit");
    const auto before = signature(*project);
    mm::MacroSession session(project, libs.string(), "Formulaire", library.macroSource("Formulaire"));
    const auto& out = session.refresh();
    check(out.rounds >= 2, "deux tours (AskNow)");
    check(session.field("nom") != nullptr && session.field("ou") != nullptr, "les deux questions atteintes");
    same(signature(*project), before, "l'apercu ne laisse rien dans le projet");
    session.setAnswer("nom", "Formulaire_Tape");
    (void)session.refresh(true);
    same(session.applyText(), "Creer Formulaire_Tape", "le bouton Appliquer suit la reponse");
    project::MacroReport report;
    auto command = session.apply(report);
    check(command != nullptr && report.ok, "Appliquer rend une commande");
    check(signature(*project) != before, "Appliquer a change le projet");
    if (command) {
        check(command->undo().has_value(), "Ctrl+Z");
        same(signature(*project), before, "Ctrl+Z rend le projet d'avant");
    }
}

// ------------------------------------------------ 5. LibImport et Ctrl+Z ----
void testImportUndo(const std::string& xpg, const fs::path& libs) {
    std::puts("5. LibImport dans une macro appliquee : Ctrl+Z, puis Ctrl+Y");
    auto project = load(xpg);
    if (!project) return;
    project::SharedLibrary library(libs.string());
    (void)library.scan();
    const auto before = signature(*project);
    const auto sections = project->sections.size(), pous = project->pous.size();
    project::MacroRunner runner(project, &library);
    const auto report = runner.run(library.macroSource("ImporteEtCree"), "ImporteEtCree", project::MacroMode::Apply);
    check(report.ok, "la macro s'applique : " + report.failure);
    auto command = runner.takeCommand();
    check(command != nullptr, "une commande");
    if (!command) return;
    check(project->pous.size() >= pous + 2 && project->sections.size() >= sections + 3,
          "la SR, le DFB (et son corps), la section sont la");
    const auto after = signature(*project);
    const auto undone = command->undo();
    check(undone.has_value(), "Ctrl+Z reussit (il echouait : " + (undone.has_value() ? std::string("-") : undone.error().message()) + ")");
    same(signature(*project) == before ? "identique" : "different", "identique", "le projet d'avant, a l'identique");
    const auto redone = command->execute();
    check(redone.has_value(), "Ctrl+Y reussit" + (redone.has_value() ? std::string{} : " : " + redone.error().message()));
    same(signature(*project) == after ? "identique" : "different", "identique", "Ctrl+Y refait tout, imports compris");
    check(command->undo().has_value(), "Ctrl+Z une deuxieme fois");
    same(signature(*project) == before ? "identique" : "different", "identique", "et le projet revient encore");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "macros2_test <MAST.XPG> [libs]\n");
        return 2;
    }
    const std::string xpg = argv[1];
    const std::string libs = argc > 2 ? argv[2] : std::string{};
    // Un dossier par execution : ctest -j lance macros2 et macros2libs dans la
    // meme seconde, et l'un effacait le dossier de l'autre (lot API 2).
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto tmp = fs::temp_directory_path()
        / ("macros2_test_" + std::to_string(::time(nullptr)) + "_" + std::to_string(argc) + "_" + std::to_string(stamp));
    fs::create_directories(tmp);
    writeLibrary(tmp / "libs");

    testSpec(libs);
    testFolders(tmp);
    testMemory(tmp);
    testSession(xpg, tmp / "libs");
    testImportUndo(xpg, tmp / "libs");

    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("%s : %d echec(s)\n", failures ? "ECHEC" : "OK", failures);
    return failures ? 1 : 0;
}
