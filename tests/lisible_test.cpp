// tests/lisible_test.cpp - 1.8.0 : l'export lisible, le comparateur, les icones au choix.
//
//   lisible_test <MAST.XPG> [dossier]
//
//  Sur le vrai programme (65 sections, 3 unites) :
//   1. le catalogue des icones : dix-huit, des cles uniques, un dessin chacune,
//      les suggestions d'apres le nom ;
//   2. les icones rangees dans le projet : la cle d'une section, la commande et
//      son Ctrl+Z, le dossier ecrit puis relu, le renommage qui les emporte ;
//   3. le comparateur : SFC_ManuA / SFC_ManuB, l'equivalence A <-> B deduite des
//      noms, et la seule vraie difference (la garde de Ctrl_ManuA) ;
//   4. l'export : l'ordre d'execution, les trois formats (texte, zip Excel,
//      PDF), la portee (une unite, deux sections).
//  Avec un dossier : les fichiers y sont ecrits (pour les regarder).
#include "../src/core/CodeIcons.hpp"
#include "../src/export/ProgramBook.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/CodeIconKeys.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/project/RenameCommands.hpp"
#include "../src/project/SectionCompare.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace ci = core::codeicons;
namespace pci = project::codeicons;
namespace cmp = project::compare;
namespace book = exporter::book;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

domain::Index sectionNamed(const domain::Project& p, std::string_view owner, std::string_view name) {
    for (domain::Index s = 0; s < p.sections.size(); ++s) {
        if (p.strings.text(p.sections[s].name) != name) continue;
        const auto o = p.sections[s].owner;
        const std::string ownerName = o < p.pous.size() ? std::string(p.strings.text(p.pous[o].name)) : std::string{};
        if (owner.empty() || ownerName == owner || (owner == "MAST" && (o >= p.pous.size() || p.pous[o].kind != domain::PouKind::ProgramUnit))) return s;
    }
    return domain::kNoIndex;
}

domain::Index pouNamed(const domain::Project& p, std::string_view name) {
    for (domain::Index i = 0; i < p.pous.size(); ++i)
        if (p.strings.text(p.pous[i].name) == name) return i;
    return domain::kNoIndex;
}

void write(const fs::path& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}
void write(const fs::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : lisible_test <MAST.XPG> [dossier]\n");
        return 2;
    }
    const fs::path out = argc > 2 ? fs::path(argv[2]) : fs::path{};

    std::printf("1. Le catalogue des icones\n");
    {
        check(ci::kCount == 18, "dix-huit icones");
        std::set<std::string> keys;
        bool drawn = true, described = true;
        for (std::size_t i = 0; i < ci::kCount; ++i) {
            keys.insert(std::string(ci::info(i).key));
            drawn = drawn && !ci::glyph(i).empty();
            described = described && !ci::info(i).name.empty() && ci::info(i).description.size() > 10;
            for (const auto& path : ci::glyph(i))
                for (const auto& pt : path.points) drawn = drawn && pt.x >= 0.f && pt.x <= 16.f && pt.y >= 0.f && pt.y <= 16.f;
        }
        check(keys.size() == ci::kCount, "des cles uniques");
        check(drawn, "un dessin chacune, dans la grille 16 x 16");
        check(described, "un nom et une description chacune");
        check(ci::indexOf("grafcet") == 4 && ci::indexOf("inconnue") < 0 && ci::indexOf("") < 0, "indexOf");
        check(ci::suggest("SFC_ManuB", ci::Kind::Section) == "grafcet", "SFC_ManuB : grafcet");
        check(ci::suggest("SFC_ManuA_Actions", ci::Kind::Section) == "actions", "SFC_ManuA_Actions : actions");
        check(ci::suggest("Acquisitions_TOR", ci::Kind::Section) == "tor", "Acquisitions_TOR : entrees TOR");
        check(ci::suggest("Acquisitions_ANA", ci::Kind::Section) == "ana", "Acquisitions_ANA : mesures");
        check(ci::suggest("Gestion_sorties", ci::Kind::Section) == "sorties", "Gestion_sorties : sorties");
        check(ci::suggest("Reset_all", ci::Kind::Section) == "reset", "Reset_all : reinitialisation");
        check(ci::suggest("SFC_DEBUG", ci::Kind::Section) == "debug", "SFC_DEBUG : diagnostic");
        check(ci::suggest("armoire", ci::Kind::Ddt) == "donnees", "un DDT : structure de donnees");
        check(ci::suggest("Zzz", ci::Kind::Section).empty(), "rien a suggerer : vide");
        check(ci::keyOf(ci::Kind::Section, "MAST", "Init") == "section:MAST/Init", "la cle d'une section");
    }

    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(argv[1]);
    if (!imported || !imported->project) {
        std::printf("import impossible\n");
        return 1;
    }
    auto project = imported->project;
    auto& p = *project;

    std::printf("2. Les icones rangees dans le projet\n");
    const auto init = sectionNamed(p, "MAST", "Init");
    const auto manuA = sectionNamed(p, "Logigrammes_A", "SFC_ManuA");
    const auto manuB = sectionNamed(p, "Logigrammes_B", "SFC_ManuB");
    const auto unitA = pouNamed(p, "Logigrammes_A");
    {
        check(init != domain::kNoIndex && manuA != domain::kNoIndex && manuB != domain::kNoIndex && unitA != domain::kNoIndex, "les sections et l'unite trouvees");
        check(pci::keyForSection(p, init) == "section:MAST/Init", "Init : section:MAST/Init");
        check(pci::keyForSection(p, manuA) == "section:Logigrammes_A/SFC_ManuA", "SFC_ManuA : section:Logigrammes_A/SFC_ManuA");
        check(pci::keyForPou(p, unitA) == "unit:Logigrammes_A", "l'unite : unit:Logigrammes_A");
        check(pci::sectionIcon(p, manuA) < 0, "au depart : aucune icone");
        pci::SetCodeIconsCommand set(project, {{pci::keyForSection(p, manuA), "grafcet"}, {pci::keyForSection(p, manuB), "grafcet"}, {"unit:Logigrammes_A", "grafcet"},
                                               {pci::keyForSection(p, init), "init"}},
                                     "Ic\xC3\xB4nes");
        check(static_cast<bool>(set.execute()), "la commande passe");
        check(pci::sectionIcon(p, manuA) == ci::indexOf("grafcet") && pci::pouIcon(p, unitA) == ci::indexOf("grafcet"), "SFC_ManuA et son unite : grafcet");
        check(pci::iconOf(p, "SECTION:mast/init") == ci::indexOf("init"), "la recherche ignore la casse");
        check(static_cast<bool>(set.undo()) && p.codeIcons.empty(), "Ctrl+Z : plus rien");
        check(static_cast<bool>(set.execute()) && p.codeIcons.size() == 4, "refaite : quatre");
        pci::SetCodeIconsCommand bad(project, {{"section:MAST/Init", "licorne"}}, "x");
        check(!bad.execute(), "une icone inconnue est refusee");

        // Le dossier : ecrit, relu.
        const auto folder = fs::temp_directory_path() / ("lisible_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        project::Manifest m;
        m.name = "Essai";
        check(static_cast<bool>(project::ProjectStore::save(p, m, folder.string())), "le projet range en dossier");
        check(fs::exists(folder / "config" / "icones-code.txt"), "config/icones-code.txt ecrit");
        auto reopened = project::ProjectStore::open(folder.string());
        check(reopened && reopened->project && reopened->project->codeIcons == p.codeIcons, "relu : les memes icones");

        // Renommer l'unite : ses icones suivent.
        project::RenameCommand rename(project, project::RenameCommand::What::Unit, unitA, "Logigrammes_X");
        check(static_cast<bool>(rename.execute()), "l'unite renommee");
        check(pci::iconOf(p, "unit:Logigrammes_X") == ci::indexOf("grafcet") && pci::iconOf(p, "section:Logigrammes_X/SFC_ManuA") == ci::indexOf("grafcet"),
              "l'unite et ses sections gardent leur icone");
        check(static_cast<bool>(rename.undo()) && pci::iconOf(p, "unit:Logigrammes_A") == ci::indexOf("grafcet"), "Ctrl+Z : l'ancien nom et son icone");
        const auto sec = sectionNamed(p, "Logigrammes_A", "SFC_ManuA");
        project::RenameCommand renameSection(project, project::RenameCommand::What::Section, sec, "SFC_ManuA2");
        check(static_cast<bool>(renameSection.execute()) && pci::iconOf(p, "section:Logigrammes_A/SFC_ManuA2") == ci::indexOf("grafcet"),
              "une section renommee garde la sienne");
        (void)renameSection.undo();
        std::error_code ec;
        fs::remove_all(folder, ec);
    }

    std::printf("3. Le comparateur\n");
    {
        const auto left = cmp::splitLines(p.sections[manuA].body), right = cmp::splitLines(p.sections[manuB].body);
        const auto eq = cmp::equivalenceFromNames("SFC_ManuA", "SFC_ManuB", "Logigrammes_A", "Logigrammes_B");
        check(eq && eq->left == "A" && eq->right == "B" && eq->fromNames, "A <-> B deduit des noms");
        const auto eq2 = cmp::equivalenceFromNames("Matrice", "Matrice", "Logigrammes_A", "Logigrammes_B");
        check(eq2 && eq2->left == "A" && eq2->right == "B", "Matrice / Matrice : A <-> B deduit des unites");
        const auto eq3 = cmp::equivalenceFromNames("Config_HCL", "Config_ND3");
        check(eq3 && eq3->left == "HCL" && eq3->right == "ND3", "Config_HCL / Config_ND3 : HCL <-> ND3");
        cmp::Options raw;
        const auto r0 = cmp::compare(left, right, raw);
        cmp::Options o;
        o.equivalences.push_back(*eq);
        const auto r1 = cmp::compare(left, right, o);
        std::printf("       sans : %d %%, %zu identiques ; avec A <-> B : %d %%, %zu identiques, %zu differences\n", r0.similarity, r0.same, r1.similarity, r1.same,
                    r1.blocks.size());
        check(r0.similarity < 60, "sans l'equivalence : moins de 60 % semblables");
        check(r1.similarity >= 95, "avec : 95 % au moins");
        check(r1.blocks.size() == 2, "deux differences");
        bool guard = false;
        for (const auto& row : r1.rows)
            if (row.kind == cmp::Row::Changed && left[static_cast<std::size_t>(row.left)].find("Ctrl_ManuA.Cmd.Enable") != std::string::npos
                && left[static_cast<std::size_t>(row.left)].find("modification_majeure") != std::string::npos)
                guard = true;
        check(guard, "la vraie difference : la garde de Ctrl_ManuA (modification_majeure)");
        std::vector<cmp::Span> ls, rs;
        cmp::inlineDiff("Ctrl_ManuA.Cmd.Enable := configuree and not(modification_majeure);", "Ctrl_ManuB.Cmd.Enable := configuree;", o, ls, rs);
        check(!ls.empty() && rs.empty(), "dans la ligne : seulement la garde, a gauche");
        o.equivalences.push_back({"[0]", "[1]", true, false});
        const auto r2 = cmp::compare(left, right, o);
        check(r2.blocks.size() == 1, "avec [0] <-> [1] en plus : une seule difference");
        const auto rep = cmp::report("SFC_ManuA (Logigrammes_A)", "SFC_ManuB (Logigrammes_B)", left, right, o, r2);
        check(rep.find("modification_majeure") != std::string::npos && rep.find("1 / 1") != std::string::npos, "le rapport en texte");
        const auto same = cmp::compare(left, left, raw);
        check(same.similarity == 100 && same.blocks.empty(), "une section contre elle-meme : 100 %");
        const auto noSpace = cmp::compare({"a := b;"}, {"a:=b;"}, raw);
        check(noSpace.similarity == 100, "les espaces ignores");
        cmp::Options strict;
        strict.ignoreSpaces = false;
        check(cmp::compare({"a := b;"}, {"a:=b;"}, strict).similarity == 0, "sinon comptes");
    }

    std::printf("4. L'export lisible\n");
    {
        book::Options o;
        o.exportedAt = "30/09/2026 \xC3\xA0 10:42";
        o.appVersion = "1.8.0";
        const auto b = book::build(p, o);
        std::size_t units = 0;
        for (const auto& it : b.order) units += it.kind == book::Item::Unit ? 1u : 0u;
        std::printf("       %zu sections, %zu lignes, %zu unites, %zu DFB, %zu variables\n", b.totalSections, b.totalLines, units, b.dfbs.size(), b.variables.size());
        check(b.totalSections == 65 && b.sections.size() == 65, "65 sections");
        check(units == 3, "trois unites de programme");
        check(!b.sections.empty() && b.sections.front().name == "Init" && b.sections.front().rank == 1, "la premiere : Init");
        check(b.sections.size() > 16 && b.sections[16].name == "SFC_ManuA" && b.sections[16].owner == "Logigrammes_A", "la 17e : SFC_ManuA (Logigrammes_A)");
        check(b.sections.size() > 64 && b.sections[64].name == "Reset_reset_config", "la derniere : Reset_reset_config");
        check(b.sections[5].condition == "ConfigurationMemoireOk", "Config_HCL : sa condition");
        check(b.sections[16].icon == ci::indexOf("grafcet"), "l'icone choisie suit");
        bool writes = false, calls = false;
        for (const auto& s : b.sections) {
            writes = writes || !s.writes.empty();
            calls = calls || (s.name == "Init" && !s.calls.empty() && s.calls.front().find("RTCDECODED") != std::string::npos);
        }
        check(writes, "ce que les sections ecrivent");
        check(calls, "Init appelle Rtc (RTCDECODED)");
        check(b.dfbs.size() == 8, "huit blocs DFB en annexe");
        const auto txt = book::toText(b, o);
        check(txt.rfind("\xEF\xBB\xBF", 0) == 0, "texte : UTF-8 avec BOM");
        check(txt.find("\r\n") != std::string::npos, "texte : fins de ligne Windows");
        check(txt.find("[17/65]  SFC_ManuA") != std::string::npos, "texte : [17/65]  SFC_ManuA");
        check(txt.find("COMMENT LIRE CE FICHIER") != std::string::npos && txt.find("SOMMAIRE") != std::string::npos, "texte : guide et sommaire");
        const auto xlsx = book::toXlsx(b, o);
        check(xlsx.size() > 1000 && xlsx[0] == 'P' && xlsx[1] == 'K', "Excel : un zip");
        const std::string xs(xlsx.begin(), xlsx.end());
        check(xs.find("xl/workbook.xml") != std::string::npos && xs.find("xl/styles.xml") != std::string::npos, "Excel : classeur et styles");
        const auto pdf = book::toPdf(b, o);
        const std::string ps(pdf.begin(), pdf.end());
        check(ps.rfind("%PDF-1.4", 0) == 0 && ps.find("%%EOF") != std::string::npos, "PDF : en-tete et fin");
        std::size_t pages = 0;
        for (std::size_t at = 0; (at = ps.find("/Type /Page ", at)) != std::string::npos; ++at) ++pages;
        std::printf("       texte %zu octets, Excel %zu octets, PDF %zu octets (%zu pages)\n", txt.size(), xlsx.size(), pdf.size(), pages);
        check(pages > 80, "PDF : plus de 80 pages");
        check(ps.find("/Outlines") != std::string::npos, "PDF : les signets");
        check(book::fileStem(b, o, "2026-09-30") == "Programme_Projet_2026-09-30" || book::fileStem(b, o, "2026-09-30").rfind("Programme_", 0) == 0, "le nom des fichiers");
        if (!out.empty()) {
            fs::create_directories(out);
            const auto stem = book::fileStem(b, o, "2026-09-30");
            write(out / (stem + ".txt"), txt);
            write(out / (stem + ".xlsx"), xlsx);
            write(out / (stem + ".pdf"), pdf);
        }

        book::Options u = o;
        u.scope = book::Options::Scope::Unit;
        u.unit = "Logigrammes_A";
        const auto bu = book::build(p, u);
        check(bu.sections.size() == 17 && bu.totalSections == 65, "une unite : ses 17 sections, leur rang parmi 65");
        check(!bu.order.empty() && bu.order.front().kind == book::Item::Unit, "une unite : son en-tete d'abord");
        book::Options s2 = o;
        s2.scope = book::Options::Scope::Sections;
        s2.sections = {manuB, manuA};
        const auto bs = book::build(p, s2);
        check(bs.sections.size() == 2 && bs.sections[0].name == "SFC_ManuA", "deux sections : dans l'ordre d'execution");
        check(book::fileStem(bs, s2, "2026-09-30").find("SFC_ManuA_SFC_ManuB") != std::string::npos, "leur nom de fichier");
        check(bs.dfbs.size() < 8, "une partie : seulement les blocs qu'elle appelle");
        if (!out.empty()) {
            write(out / (book::fileStem(bu, u, "2026-09-30") + ".pdf"), book::toPdf(bu, u));
            write(out / (book::fileStem(bs, s2, "2026-09-30") + ".xlsx"), book::toXlsx(bs, s2));
            write(out / (book::fileStem(bs, s2, "2026-09-30") + ".txt"), book::toText(bs, s2));
        }
    }

    std::printf(failures ? "\n%d ECHEC(S)\n" : "\nTout est bon.\n", failures);
    return failures ? 1 : 0;
}
