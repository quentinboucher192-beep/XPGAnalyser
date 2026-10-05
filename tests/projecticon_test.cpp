// tests/projecticon_test.cpp - lot API 6 : l'icone du projet.
//
//   projecticon_test <MAST.XPG>
//
//  Les quinze dessins de la galerie (aucun vide, des cles uniques, les
//  initiales du projet) ; les outils de l'editeur (le crayon et sa symetrie, la
//  ligne, le rectangle, le pot de peinture qui s'arrete aux bords) ; le texte
//  de config/icone.txt ecrit puis relu a l'identique, et refuse quand il est
//  faux ; la reduction en 16 x 16 ; la commande et son Ctrl+Z ; le projet
//  range en dossier puis rouvert garde son icone, et la perd quand on la retire
//  (le fichier disparait).
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ApiCommands.hpp"
#include "../src/project/ProjectIcon.hpp"
#include "../src/project/ProjectStore.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace icon = project::icon;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::size_t painted(const domain::ProjectIcon& i) {
    std::size_t n = 0;
    for (const auto p : i.pixels) n += p != 0 ? 1u : 0u;
    return n;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("1. La galerie\n");
    {
        const auto& all = icon::presets();
        check(all.size() == 15, "quinze dessins");
        std::set<std::string> keys;
        bool allDrawn = true, allSized = true;
        for (const auto& p : all) {
            keys.insert(p.key);
            const auto i = icon::preset(p.key, "Armoire_Gaz");
            allSized = allSized && i.pixels.size() == 32u * 32u;
            const auto n = painted(i);
            allDrawn = allDrawn && n >= 40;
            std::printf("       %-12s %-20s %4zu pixels\n", p.key, p.label, n);
        }
        check(keys.size() == all.size(), "des cles uniques");
        check(allSized, "32 x 32 chacun");
        check(allDrawn, "aucun vide (40 pixels au moins)");
        check(icon::preset("inconnu").empty(), "une cle inconnue : pas d'icone");
        check(icon::initialsOf("Armoire_Gaz") == "AG" && icon::initialsOf("station pompage") == "SP" && icon::initialsOf("PR3") == "PR",
              "les initiales : AG, SP, PR");
        check(icon::preset("initiales", "Armoire_Gaz") != icon::preset("initiales", "Station_Pompage"), "les initiales changent avec le projet");
    }

    std::printf("2. Les outils\n");
    {
        auto i = icon::blank();
        check(icon::isBlank(i) && i.palette == icon::defaultPalette(), "une icone vide, la palette de depart");
        icon::put(i, 3, 4, 7, true);
        check(icon::at(i, 3, 4) == 7 && icon::at(i, 28, 4) == 7 && painted(i) == 2, "le crayon en symetrie : le pixel et son reflet (x -> 31 - x)");
        check(icon::at(i, -1, 0) == 0 && icon::at(i, 32, 0) == 0, "hors de l'icone : transparent");
        icon::line(i, 0, 31, 31, 31, 2);
        check(icon::at(i, 0, 31) == 2 && icon::at(i, 15, 31) == 2 && icon::at(i, 31, 31) == 2, "une ligne d'un bord a l'autre");
        auto f = icon::blank();
        icon::frame(f, 8, 8, 23, 23, 5);
        std::size_t border = painted(f);
        check(border == 60, "un rectangle 16 x 16 : 60 pixels de contour");
        icon::fill(f, 15, 15, 9);
        check(icon::at(f, 15, 15) == 9 && icon::at(f, 9, 9) == 9 && icon::at(f, 8, 8) == 5 && icon::at(f, 0, 0) == 0,
              "le pot remplit l'interieur, s'arrete au contour");
        check(painted(f) == 256, "l'interieur rempli : 14 x 14 + 60 = 256");
        icon::fill(f, 0, 0, 3);
        check(icon::at(f, 0, 0) == 3 && icon::at(f, 31, 31) == 3 && icon::at(f, 15, 15) == 9, "le pot dehors : tout l'exterieur, rien dedans");
    }

    std::printf("3. config/icone.txt\n");
    {
        auto i = icon::preset("bouteille");
        i.palette[7] = 0x123456;
        const auto text = icon::toText(i);
        auto back = icon::fromText(text);
        check(back && *back == i, "ecrit puis relu : la meme icone, la palette changee comprise");
        check(text.find("#123456") != std::string::npos, "la palette en #RRGGBB");
        check(!icon::fromText("pas une icone"), "un texte faux est refuse");
        std::uint32_t rgb = 0;
        check(icon::parseHex("#e4574f", rgb) && rgb == 0xE4574F && icon::hex(rgb) == "#E4574F" && !icon::parseHex("#12", rgb), "les couleurs en hexadecimal");
        const auto big = icon::rgba(i);
        const auto small = icon::rgba16(i);
        check(big.size() == 32u * 32u * 4u && small.size() == 16u * 16u * 4u, "RGBA : 32 x 32 et 16 x 16");
        check(big[3] == 0 && small[3] == 0, "le coin transparent le reste");
        std::size_t opaque16 = 0;
        for (std::size_t k = 3; k < small.size(); k += 4) opaque16 += small[k] > 0 ? 1u : 0u;
        check(opaque16 > 20, "la reduction garde le dessin");
    }

    std::printf("4. La commande, et le dossier\n");
    if (argc < 2) {
        std::printf("       (sans MAST.XPG : le dossier n'est pas essaye)\n");
    } else {
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        check(static_cast<bool>(imported), "le projet d'essai se lit");
        if (imported) {
            auto p = std::const_pointer_cast<domain::Project>(imported->project);
            check(p->icon.empty(), "un export n'a pas d'icone");
            project::SetProjectIconCommand set(p, icon::preset("flamme"));
            check(static_cast<bool>(set.execute()) && p->icon == icon::preset("flamme"), "la commande pose l'icone");
            check(set.label() == "Changer l'ic\xC3\xB4ne du projet", "son libelle");
            check(static_cast<bool>(set.undo()) && p->icon.empty(), "Ctrl+Z la retire");
            domain::ProjectIcon wrong;
            wrong.pixels.assign(10, 1);
            project::SetProjectIconCommand bad(p, wrong);
            check(!bad.execute() && p->icon.empty(), "une icone qui n'a pas 32 x 32 pixels est refusee");
            check(static_cast<bool>(set.execute()), "et reposee");

            const auto dir = (fs::temp_directory_path() / ("xpg-projecticon-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
            project::Manifest m;
            m.name = "Essai_Icone";
            check(static_cast<bool>(project::ProjectStore::save(*p, m, dir)), "le projet s'ecrit en dossier");
            check(fs::exists(fs::path(dir) / "config" / "icone.txt"), "config/icone.txt est la");
            auto reopened = project::ProjectStore::open(dir);
            check(reopened && reopened->project && reopened->project->icon == icon::preset("flamme"), "rouvert : la meme icone");
            project::SetProjectIconCommand none(p, domain::ProjectIcon{});
            check(static_cast<bool>(none.execute()) && p->icon.empty() && none.label() == "Retirer l'ic\xC3\xB4ne du projet", "la retirer : une commande aussi");
            check(static_cast<bool>(project::ProjectStore::save(*p, m, dir)) && !fs::exists(fs::path(dir) / "config" / "icone.txt"),
                  "enregistre sans icone : le fichier disparait");
            auto again = project::ProjectStore::open(dir);
            check(again && again->project && again->project->icon.empty(), "rouvert : pas d'icone");
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    }

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "projecticon_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
