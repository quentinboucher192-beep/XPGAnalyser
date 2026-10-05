// tests/typeusage_test.cpp - lot API 5 : qui se sert de quoi, et renommer.
//
//   typeusage_test <MAST.XPG>
//
//  Sur le projet d'essai : chaque DDT et chaque DFB a son « utilise par », les
//  instances de DFB_GRAFCETENGINE se retrouvent (globales et locales), les
//  variables se rangent par genre, l'index des sections repond. Puis RENOMMER :
//  une variable globale (le code et les tables suivent), un type derive (les
//  variables suivent), une unite (les tables suivent), un champ refuse quand il
//  est ambigu ; chaque fois, l'export relu est le meme apres un Ctrl+Z. Et le
//  projet range en dossier rend le meme en-tete qu'un export direct.
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ApiCommands.hpp"
#include "../src/project/IoCheck.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/project/RenameCommands.hpp"
#include "../src/project/TypeUsage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace domain;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::shared_ptr<Project> load(const std::string& path) {
    static core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(path);
    if (!imported) {
        std::printf("       import : %s\n", imported.error().message().c_str());
        return nullptr;
    }
    return std::const_pointer_cast<Project>(imported->project);
}

std::string exportOf(const Project& p) {
    exporter::XpgWriteOptions opts;
    opts.dateTime = "date_and_time#2026-1-1-0:00:00";
    auto x = exporter::writeXpg(p, opts);
    return x ? *x : std::string{};
}

Index variableNamed(const Project& p, std::string_view name) {
    for (Index i = 0; i < p.variables.size(); ++i)
        if (p.variables[i].scope == VariableScope::Global && p.strings.text(p.variables[i].name) == name) return i;
    return kNoIndex;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : typeusage_test <MAST.XPG>\n");
        return 2;
    }
    auto p = load(argv[1]);
    check(p != nullptr, "le projet se charge");
    if (!p) return 1;

    std::printf("1. Les types derives\n");
    std::size_t used = 0;
    for (const auto& dt : p->derivedTypes) {
        const auto u = project::usage::usesOfType(*p, p->strings.text(dt.name));
        used += u.used() ? 1u : 0u;
    }
    std::printf("       %zu types derives, %zu utilises\n", p->derivedTypes.size(), used);
    check(p->derivedTypes.size() >= 20 && used >= p->derivedTypes.size() - 3, "presque tous les DDT servent a quelque chose");
    const auto armoire = project::usage::usesOfType(*p, "armoire");
    std::printf("       armoire : %s\n", armoire.summary().c_str());
    check(armoire.used(), "armoire est utilise");
    const auto io = project::usage::usesOfType(*p, "IW");
    std::printf("       IW : %s\n", io.summary().c_str());

    std::printf("2. Les blocs DFB\n");
    const project::usage::Where where(*p);
    std::size_t engine = 0;
    for (Index i = 0; i < p->pous.size(); ++i) {
        const auto& pou = p->pous[i];
        if (pou.kind != PouKind::FunctionBlockType || !pou.userDefined) continue;
        const std::string name(p->strings.text(pou.name));
        const auto inst = project::usage::instancesOf(*p, name, where);
        const auto itf = project::usage::interfaceOf(*p, i);
        std::printf("       %-24s %2zu instances, %zu entrees, %zu sorties, %zu publiques\n", name.c_str(), inst.size(),
                    itf.inputs.size(), itf.outputs.size(), itf.publics.size());
        if (name == "DFB_GRAFCETENGINE") engine = inst.size();
    }
    check(engine >= 1, "DFB_GRAFCETENGINE a ses instances (" + std::to_string(engine) + ")");

    std::printf("3. Les variables\n");
    std::size_t genres[5] = {0, 0, 0, 0, 0};
    for (const auto& v : p->variables)
        if (v.scope == VariableScope::Global) ++genres[static_cast<int>(project::usage::genreOf(*p, v))];
    for (int g = 0; g < 5; ++g)
        std::printf("       %-36s %zu\n", std::string(project::usage::genreLabel(static_cast<project::usage::Genre>(g))).c_str(), genres[g]);
    check(genres[0] > 0 && genres[1] > 0 && genres[2] > 0 && genres[3] > 0, "les globales se rangent en genres");
    const auto armoires = where.sectionNames("Armoires");
    std::printf("       Armoires : %s\n", armoires.c_str());
    check(!armoires.empty(), "l'index des sections repond (Armoires)");

    std::printf("4. Renommer\n");
    const auto start = exportOf(*p);
    {
        const auto v = variableNamed(*p, "index_armoire_non_utilisee");
        check(v != kNoIndex, "la variable index_armoire_non_utilisee existe");
        const auto before = where.sectionsOf("index_armoire_non_utilisee").size();
        project::RenameCommand cmd(p, project::RenameCommand::What::Variable, v, "Index_Armoire_Libre");
        const auto r = cmd.execute();
        check(static_cast<bool>(r), "renommer une variable globale" + (r ? "" : " : " + r.error().message()));
        const project::usage::Where after(*p);
        check(after.sectionsOf("index_armoire_non_utilisee").empty() && after.sectionsOf("Index_Armoire_Libre").size() == before,
              "le code suit (" + std::to_string(cmd.sectionsRewritten()) + " sections)");
        (void)cmd.undo();
        check(exportOf(*p) == start, "Ctrl+Z : l'export est le meme qu'avant");
    }
    {
        Index dt = kNoIndex;
        for (Index i = 0; i < p->derivedTypes.size(); ++i)
            if (p->strings.text(p->derivedTypes[i].name) == "config_gaz") dt = i;
        check(dt != kNoIndex, "le type config_gaz existe");
        const auto uses = project::usage::usesOfType(*p, "config_gaz");
        project::RenameCommand cmd(p, project::RenameCommand::What::DerivedType, dt, "Config_Gaz_V2");
        const auto r = cmd.execute();
        check(static_cast<bool>(r), "renommer un type derive" + (r ? "" : " : " + r.error().message()));
        const auto moved = project::usage::usesOfType(*p, "Config_Gaz_V2");
        check(moved.globals == uses.globals && moved.units == uses.units && !project::usage::usesOfType(*p, "config_gaz").used(),
              "les variables suivent (" + moved.summary() + ")");
        (void)cmd.undo();
        check(exportOf(*p) == start, "Ctrl+Z : l'export est le meme qu'avant");
    }
    {
        Index unit = kNoIndex;
        for (Index i = 0; i < p->pous.size(); ++i)
            if (p->pous[i].kind == PouKind::ProgramUnit && p->strings.text(p->pous[i].name) == "Gestion_armoires") unit = i;
        check(unit != kNoIndex, "l'unite Gestion_armoires existe");
        std::size_t owned = 0;
        for (const auto& t : p->animationTables) owned += p->strings.text(t.owner) == "Gestion_armoires" ? 1u : 0u;
        project::RenameCommand cmd(p, project::RenameCommand::What::Unit, unit, "Armoires_Gestion");
        check(static_cast<bool>(cmd.execute()), "renommer une unite");
        std::size_t follow = 0;
        for (const auto& t : p->animationTables) follow += p->strings.text(t.owner) == "Armoires_Gestion" ? 1u : 0u;
        check(follow == owned && owned > 0, "ses tables d'animation suivent (" + std::to_string(follow) + ")");
        (void)cmd.undo();
        check(exportOf(*p) == start, "Ctrl+Z : l'export est le meme qu'avant");
        // Le projet rendu cherche ses noms dans SES chaines (la copie de l'etat
        // d'avant refait l'index : sans cela, MAST ne se retrouvait plus).
        bool found = !p->tasks.empty();
        for (const auto& t : p->tasks) found = found && p->strings.intern(std::string(p->strings.text(t.name))) == t.name;
        for (const auto& pou : p->pous) found = found && p->strings.intern(std::string(p->strings.text(pou.name))) == pou.name;
        check(found, "Ctrl+Z : chaque nom se retrouve dans l'index des chaines");
        domain::Project copy = *p;
        {
            domain::Project gone = copy;       // une copie de copie, detruite aussitot
            (void)gone;
        }
        check(copy.strings.intern("MAST") == p->strings.intern("MAST") && copy.strings.intern("Nom_tout_neuf_42") != 0,
              "une copie du projet a son propre index de chaines");
    }
    {
        const auto v = variableNamed(*p, "Armoires");
        check(!project::renameProblem(*p, project::RenameCommand::What::Variable, v, "ConfigGaz").empty() ||
                  variableNamed(*p, "ConfigGaz") == kNoIndex,
              "un nom deja pris est refuse");
        check(!project::renameProblem(*p, project::RenameCommand::What::Variable, v, "2Armoires").empty(), "un nom qui commence par un chiffre est refuse");
        std::string code = "x := a.champ; (* a.champ *) b := 'a.champ'; champ := 1;";
        const auto n = project::renameInCode(code, "champ", "zone", true);
        check(n == 1 && code == "x := a.zone; (* a.champ *) b := 'a.champ'; champ := 1;", "renommer un membre : ni les commentaires, ni les chaines, ni la racine");
    }

    std::printf("5. Le projet range en dossier : le meme en-tete\n");
    {
        const auto dir = (fs::temp_directory_path() / ("xpg-typeusage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
        fs::remove_all(dir);
        project::Manifest m;
        m.name = p->header.projectName;
        m.version = p->header.projectVersion;
        m.company = p->header.company;
        m.product = p->header.product;
        m.dtdVersion = p->header.dtdVersion;
        check(static_cast<bool>(project::ProjectStore::save(*p, m, dir)), "le projet s'ecrit en dossier");
        auto reopened = project::ProjectStore::open(dir);
        check(reopened && reopened->project, "et se relit");
        if (reopened && reopened->project) check(exportOf(*reopened->project) == start, "l'export via le dossier est celui d'un export direct, en-tete compris");
        if (reopened && reopened->project) {
            // Les champs relus savent de quel type ils sont : « utilise par » est le meme.
            std::size_t same = 0;
            for (const auto& dt : p->derivedTypes) {
                const std::string name(p->strings.text(dt.name));
                same += project::usage::usesOfType(*p, name).summary() == project::usage::usesOfType(*reopened->project, name).summary() ? 1u : 0u;
            }
            check(same == p->derivedTypes.size(), "relu du dossier, chaque type a le meme \xC2\xAB utilise par \xC2\xBB (" + std::to_string(same) + "/"
                                                      + std::to_string(p->derivedTypes.size()) + ")");
        }
        fs::remove_all(dir);
    }

    std::printf("6. Le plan memoire : les trois zones, les bornes, l'utilisation\n");
    {
        auto zones = project::io::memoryZones(*p);
        check(zones.size() == 3 && zones[0].prefix == "%M" && zones[1].prefix == "%MW" && zones[2].prefix == "%KW", "trois zones : %M, %MW, %KW");
        for (const auto& z : zones)
            std::printf("       %-4s taille %u (%s), %zu variables situees, %u/%u employes = %s, %zu cellules en direct\n", z.prefix.c_str(), z.size,
                        z.configured ? ".XHW" : "deduite", z.variables.size(), z.used, z.span(), project::io::percentText(z.percent()).c_str(), z.direct.size());
        check(zones.size() == 3 && !zones[1].configured && zones[1].size >= zones[1].highest && zones[1].size % 100 == 0,
              "sans .XHW : la taille va jusqu'a la derniere cellule employee, arrondie a la centaine");
        check(zones.size() == 3 && zones[1].used > 0 && zones[1].percent() > 0.0 && zones[1].percent() <= 100.0, "des %MW employes, en pour cent");
        check(zones.size() == 3 && zones[0].used > 0, "des %M employes (situes ou nommes en direct)");
        // Le .XHW des essais : 512 bits, 1024 mots, 256 constantes.
        p->hardware.memory.declared = true;
        p->hardware.memory.internalBits = 512;
        p->hardware.memory.internalWords = 1024;
        p->hardware.memory.constantWords = 256;
        zones = project::io::memoryZones(*p);
        check(zones[1].configured && zones[1].size == 1024 && zones[1].from == 0 && zones[1].to == 1023 && zones[2].size == 256 && zones[0].size == 512,
              "avec le .XHW : 512 %M, 1024 %MW, 256 %KW, lus en entier");
        check(!zones[1].outside.empty(), "les variables au-dela de %MW1023 sont signalees (" + (zones[1].outside.empty() ? std::string("aucune") : zones[1].outside.front()) + ")");
        const auto whole = zones[1].used;
        project::SetMemoryWindowCommand cmd(p, domain::MemoryZone::Words, domain::MemoryWindow{true, 1000, 1199});
        check(static_cast<bool>(cmd.execute()), "borner la lecture : %MW1000 a %MW1199 (une commande)");
        zones = project::io::memoryZones(*p);
        check(zones[1].bounded && zones[1].from == 1000 && zones[1].to == 1199 && zones[1].span() == 200 && zones[1].used <= 200,
              "la lecture suit les bornes : 200 mots, " + project::io::percentText(zones[1].percent()) + " employes");
        check(zones[1].gapSize() == 0 || (zones[1].gapFrom >= 1000 && zones[1].gapTo <= 1199), "la place libre se cherche dans les bornes");
        {
            const auto dir = (fs::temp_directory_path() / ("xpg-memoire-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
            project::Manifest m;
            m.name = p->header.projectName;
            check(static_cast<bool>(project::ProjectStore::save(*p, m, dir)), "les bornes s'ecrivent avec le projet (config/memoire.txt)");
            auto reopened = project::ProjectStore::open(dir);
            check(reopened && reopened->project && reopened->project->memoryWindows[1] == p->memoryWindows[1] && !reopened->project->memoryWindows[0].set,
                  "et se relisent");
            fs::remove_all(dir);
        }
        domain::MemoryWindow bad{true, 10, 5};
        project::SetMemoryWindowCommand refused(p, domain::MemoryZone::Bits, bad);
        check(!refused.execute(), "une fin avant le debut est refusee");
        (void)cmd.undo();
        zones = project::io::memoryZones(*p);
        check(!p->memoryWindows[1].set && zones[1].used == whole && zones[1].to == 1023, "Ctrl+Z : toute la zone de nouveau");
        check(project::io::percentText(7.5) == "7,5 %" && project::io::percentText(0.04) == "< 0,1 %" && project::io::percentText(55.0) == "55 %",
              "les pourcentages s'ecrivent a la francaise");
        p->hardware.memory = {};
    }

    std::printf("7. Les adresses du code : ecrites, en table, indexees (constantes ou dynamiques)\n");
    {
        domain::Project q;
        domain::Variable k;
        k.name = q.strings.intern("NB_ELEMENTS");
        k.scope = domain::VariableScope::Constant;
        k.initValue = q.strings.intern("4");
        q.variables.push_back(k);
        domain::Section sec;
        sec.name = q.strings.intern("Essai_memoire");
        sec.body = "%MW500 := 50;                      (* une ecriture : de la memoire *)\n"
                   "x := %MW510;                       (* une lecture *)\n"
                   "FOR i := 0 TO 9 DO %MW600[i] := 0; END_FOR;\n"
                   "y := %MW700[index];                (* dynamique, sans boucle *)\n"
                   "%MW800[2*3+1] := 1;                (* constant : %MW807 *)\n"
                   "%MW900[NB_ELEMENTS - 1] := 2;      (* une constante du projet : %MW903 *)\n"
                   "%MW20:5 := %MW40:5;                (* deux tables de cinq mots *)\n"
                   "FOR j := 1 TO NB_ELEMENTS DO %MF1000[j] := 0.0; END_FOR;  (* des REAL : deux mots chacun *)\n"
                   "%M100[16#A] := TRUE;               (* %M110 *)\n"
                   "z := '%MW999';                     (* une chaine : rien *)\n"
                   "taille := 150; depart := 6000; nb := 20;\n"
                   "FOR i := 0 TO nb - 1 DO FOR j := 0 TO taille - 1 DO\n"
                   "  %MW0[depart + i * taille + j] := 0;\n"
                   "END_FOR; END_FOR;\n"
                   "%MW0[depart + choix * taille + (taille - 1)].0 := TRUE;  (* choix : inconnu *)\n";
        q.sections.push_back(sec);
        const auto zones = project::io::memoryZones(q);
        const auto& w = zones[1];
        const auto has = [](const std::vector<std::uint32_t>& v, std::uint32_t c) { return std::binary_search(v.begin(), v.end(), c); };
        check(has(w.direct, 500) && has(w.written, 500), "%MW500 := 50 prend sa place (ecrit)");
        check(has(w.direct, 510) && !has(w.written, 510), "%MW510 lu : employe, pas ecrit");
        check(has(w.dynamicCells, 600) && has(w.dynamicCells, 609) && !has(w.dynamicCells, 610), "%MW600[i], FOR i := 0 TO 9 : %MW600 a %MW609, dynamique");
        check(has(w.direct, 807) && has(w.direct, 903) && !has(w.direct, 999), "les indices constants s'evaluent (%MW807, %MW903) ; une chaine ne compte pas");
        check(has(w.direct, 20) && has(w.direct, 24) && has(w.direct, 40) && has(w.direct, 44) && !has(w.direct, 25), "%MW20:5 et %MW40:5 : deux tables de cinq mots");
        check(has(w.dynamicCells, 1002) && has(w.dynamicCells, 1009) && !has(w.dynamicCells, 1001) && !has(w.dynamicCells, 1010),
              "%MF1000[j], j de 1 a 4 : deux mots par REAL, %MW1002 a %MW1009");
        std::size_t unknownDynamic = 0;
        for (const auto& in : w.indexed) {
            std::printf("       %-18s %-10s %s\n", in.text.c_str(), in.constant ? "constant" : in.bounded ? "dynamique" : "inconnu", in.why.c_str());
            unknownDynamic += in.dynamic && !in.bounded ? 1u : 0u;
        }
        check(unknownDynamic == 2 && w.dynamicUnknown == 2 && has(w.dynamicCells, 700), "%MW700[index] : dynamique, index inconnu, estime a sa base et signale");
        check(has(zones[0].direct, 110), "%M100[16#A] : %M110");
        check(has(w.dynamicCells, 6000) && has(w.dynamicCells, 8999) && !has(w.dynamicCells, 9000),
              "%MW0[depart + i*taille + j] : depart, taille, nb n'ont qu'une valeur ; i, j bornes par FOR : %MW6000 a %MW8999");
        bool estimate = false;
        for (const auto& in : w.indexed) estimate = estimate || (in.estimated && in.lo == 6149 && in.write);
        check(estimate, "%MW0[depart + choix*taille + (taille-1)].0 := TRUE : choix inconnu, estime a 0 (%MW6149), ecrit");
        check(w.used == static_cast<std::uint32_t>(w.direct.size() + w.dynamicCells.size()), "l'utilisation compte le direct et les plages dynamiques");
    }

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "typeusage_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
