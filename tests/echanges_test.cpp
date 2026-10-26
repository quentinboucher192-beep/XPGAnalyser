// tests/echanges_test.cpp - 1.12.30 : les echanges de projets (archives .xihm, .xapi).
//
//   echanges_test <bac/Armoire_Gaz> <libs/> [dossier d'un vrai projet IHM a passer aller-retour]
//
//  Sans ecran :
//  1. La base commune (project/ProjectArchive) : le manifeste ecrit puis relu, le base64,
//     les chemins refuses au deballage, ce que l'archive laisse (les artefacts du build, exports,
//     corbeille) et ce qu'elle prend (1.12.31 : le cache de construction).
//  2. L'archive .xihm d'un vrai projet (bac/Armoire_Gaz, copie) : exporter, relire, deballer
//     dans un dossier neuf ; fichier par fichier (octets et dates) et compte par compte, le
//     projet rouvert est identique. Un fichier externe lie hors du dossier vient avec, et le
//     lien suit sa copie (ou ne change pas s'il est deja la, identique).
//  3. Les archives abimees (un octet change, un fichier coupe, pas un zip), d'une version plus
//     recente (format 3 ; un projet IHM de format 99), d'une autre application (.xapi dans
//     IHM Studio), une extension inconnue.
//  4. L'ancienne archive .zip de l'IHM (1.x) : elle se relit, et s'ouvre en projet.
//  5. L'archive .xapi (API Studio) du meme projet : le programme, la disposition des grafcets
//     a cote du dossier, les macros de la bibliotheque (ajoutees si absentes) ; les comptes.
//  2 bis. 1.12.31 : le cache de construction voyage - exporter, deballer ailleurs, ouvrir : le build
//     est a jour, rien a regenerer (ses artefacts refaits au deballage, octet pour octet) ; une source
//     modifiee apres le deballage, un artefact altere : regeneres ; un fichier externe relie a sa copie :
//     ce seul element ; une archive d'avant (sans cache) : tout se regenere, comme avant.
//  7. Un projet passe en argument (le projet de demonstration, en copie) : l'aller-retour ;
//     1.12.31 : un build complet d'abord, le build a jour apres le deballage.
//  8. 1.13.0 (relecture) : une page web (.html) ouverte - un refus en francais, dans les deux editions,
//     avant l'importeur d'API Studio ; l'import d'un paquet de types ou de fonctions voit les methodes,
//     le parent, les interfaces, les generiques et les cas de test (plus "identique" a tort).
#include "../src/app/App.hpp"               // 1.13.0 : App::webPageRefusal (une page web ne s'ouvre plus)
#include "../src/app/hmi/HmiBuild.hpp"      // 1.12.31 : l'etat du build, tel que l'arbre le montre
#include "../src/core/AtomicFile.hpp"
#include "../src/core/Edition.hpp"
#include "../src/hmi/HmiArchive.hpp"
#include "../src/hmi/HmiAssets.hpp"
#include "../src/hmi/HmiHistory.hpp"
#include "../src/hmi/HmiPackage.hpp"        // 1.13.0 : les paquets de types et de fonctions
#include "../src/hmi/HmiPipeline.hpp"       // 1.12.31 : le cache de construction
#include "../src/hmi/HmiStore.hpp"
#include "../src/project/ApiArchive.hpp"
#include "../src/project/ProjectArchive.hpp"
#include "../src/project/ProjectStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace pa = project::archive;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    // 1.12.31 : ECHANGES_TEST_TOUT=1 dit aussi ce qui passe (les comptes du cache de construction, par exemple).
    static const bool all = [] {
        const char* e = std::getenv("ECHANGES_TEST_TOUT");
        return e && *e == '1';
    }();
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    } else if (all) {
        std::printf("  ok : %s\n", what.c_str());
    }
}

std::string readAll(const fs::path& p) {
    std::string s;
    (void)core::readFileAll(p, s);
    return s;
}

void writeAll(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::int64_t mtime(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(ft)));
}

// Les fichiers d'un dossier (chemin relatif -> contenu), hors de ce que l'archive laisse.
std::map<std::string, std::string> filesOf(const fs::path& root, bool versions = true, bool history = true) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string rel = fs::relative(it->path(), root).generic_string();
        if (pa::excluded(rel, pa::CollectOptions{versions, history})) continue;
        out[rel] = readAll(it->path());
    }
    return out;
}

// Deux dossiers identiques, fichier par fichier (et date par date) ; ce qui differe est dit.
bool sameFolders(const fs::path& a, const fs::path& b, const std::string& label, const std::vector<std::string>& except = {},
                 bool dates = true) {
    const auto fa = filesOf(a), fb = filesOf(b);
    bool same = true;
    std::size_t said = 0;
    const auto skip = [&except](const std::string& rel) { return std::find(except.begin(), except.end(), rel) != except.end(); };
    for (const auto& [rel, content] : fa) {
        if (skip(rel)) continue;
        const auto it = fb.find(rel);
        if (it == fb.end()) {
            same = false;
            if (said++ < 6) std::printf("  %s : %s manque apres l'aller-retour\n", label.c_str(), rel.c_str());
        } else if (it->second != content) {
            same = false;
            if (said++ < 6) std::printf("  %s : %s differe (%zu / %zu octets)\n", label.c_str(), rel.c_str(), content.size(), it->second.size());
        } else if (dates && std::llabs(mtime(a / rel) - mtime(b / rel)) > 1) {
            same = false;
            if (said++ < 6) std::printf("  %s : %s n'a pas garde sa date\n", label.c_str(), rel.c_str());
        }
    }
    for (const auto& [rel, content] : fb)
        if (!skip(rel) && !fa.count(rel)) {
            same = false;
            if (said++ < 6) std::printf("  %s : %s en trop apres l'aller-retour\n", label.c_str(), rel.c_str());
        }
    return same;
}

fs::path scratch() {
    static const fs::path dir = fs::temp_directory_path() / "xpg_echanges_test";
    return dir;
}

void copyFolder(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::remove_all(to, ec);
    fs::create_directories(to.parent_path(), ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    // Les dates d'origine (fs::copy ne les garde pas partout) : celles de la source.
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) {
            std::error_code e2;
            fs::last_write_time(to / fs::relative(it->path(), from), fs::last_write_time(it->path(), e2), e2);
        }
}

// ============================================================ 1. la base commune ==
void baseCommune() {
    std::printf("1. la base commune : manifeste, base64, chemins, exclusions\n");
    pa::Manifest m;
    m.kind = pa::Kind::Api;
    m.kindKnown = true;
    m.tool = "API Studio 1.12.30";
    m.created = "2026-10-23 20:00";
    m.project = "Ligne \"gaz\" \\ 2";
    m.folder = "Ligne_gaz";
    m.counts.push_back({"sections", "Sections", 12, 12});
    pa::Entry e;
    e.path = "sections/Init.st";
    e.bytes = 42;
    e.sha256 = std::string(64, 'a');
    e.mtime = 1792781119;
    m.entries.push_back(e);
    e.path = "_externes/12-recette.xlsx";
    e.origin = pa::Origin::External;
    e.link = "D:\\Donnees\\recette gaz.xlsx";
    m.entries.push_back(e);
    pa::Manifest back;
    std::vector<std::string> problems;
    pa::parse(pa::toText(m), back, problems);
    check(problems.empty(), "le manifeste se relit sans faute");
    check(back.format == pa::kFormat && back.kind == pa::Kind::Api && back.kindKnown && back.tool == m.tool && back.project == m.project
              && back.folder == m.folder,
          "le manifeste : format, genre, outil, projet (guillemets et \\ compris), dossier");
    check(back.counts.size() == 1 && back.counts[0].key == "sections" && back.counts[0].expected == 12, "le manifeste : les comptes");
    check(back.entries.size() == 2 && back.entries[1].origin == pa::Origin::External && back.entries[1].link == e.link
              && back.entries[0].mtime == 1792781119 && back.entries[0].bytes == 42,
          "le manifeste : chaque fichier, sa date, son origine et son lien");

    // Le base64 (project/ProjectArchive) : l'export SVG (app/SvgRenderer) s'en sert pour les images.
    const std::uint8_t abc[] = {'A', 'B', 'C'};
    const std::uint8_t ma[] = {'M', 'a'};
    check(pa::base64(abc, 3) == "QUJD" && pa::base64(ma, 2) == "TWE=" && pa::base64(nullptr, 0).empty(),
          "le base64 : des vecteurs connus (QUJD, TWE=, vide)");

    check(pa::safePath("ihm/vues/0003-Vue.vue") && pa::safePath("_externes/1-a.xlsx"), "les chemins sains passent");
    check(!pa::safePath("../dehors.txt") && !pa::safePath("ihm/../../x") && !pa::safePath("/etc/passwd") && !pa::safePath("C:/Windows/x")
              && !pa::safePath("ihm\\x") && !pa::safePath("a//b") && !pa::safePath(""),
          "les chemins qui sortiraient du dossier sont refuses (.., absolu, lecteur, \\, vide)");

    const pa::CollectOptions all{true, true}, lean{false, false};
    check(pa::excluded(".xpg/build/ihm/08-vues/vue_12-Vue.txt", all) && pa::excluded(".xpg/build/ihm", all) && pa::excluded(".xpg/build/build.lock", all)
              && pa::excluded(".xpg/build/build-cache.txt.bak", all) && pa::excluded(".xpg/build/build-cache.txt.tmp", all)
              && pa::excluded("exports/dossier.pdf", all) && pa::excluded("ihm/corbeille/0001-a.png", all)
              && pa::excluded("ihm/vues/0001-a.vue.tmp", all) && pa::excluded("copie.xihm", all) && pa::excluded("sous/autre.XAPI", all),
          "laisse : les artefacts du build, son verrou, la copie de secours du cache, les exports, la corbeille, les temporaires, les archives");
    std::string why;
    check(pa::excluded(".xpg/build/ihm/08-vues/vue_12-Vue.txt", all, &why) && why.find("refaits au d\xC3\xA9" "ballage") != std::string::npos,
          "... et pourquoi : les artefacts, refaits au deballage (" + why + ")");
    check(!pa::excluded(".xpg/build/build-cache.txt", all) && !pa::excluded(".xpg/build", all) && !pa::excluded(".xpg/build/build-cache.txt", lean),
          "1.12.31 : prend le cache de construction (build-cache.txt), meme sans versions ni historique");
    check(!pa::excluded(".xpg/simulation/remanence.txt", all) && !pa::excluded("ihm/historique-edition/0003.txt", all)
              && !pa::excluded("versions/index.txt", all) && !pa::excluded("ihm/historique/mesures.csv", all)
              && !pa::excluded("project.lock", all) && !pa::excluded("src/MAST.XPG", all),
          "pris : la simulation, l'historique des objets, les versions, l'historique de marche, le verrou, les sources");
    check(pa::excluded("versions/index.txt", lean) && pa::excluded("ihm/historique/mesures.csv", lean)
              && !pa::excluded("ihm/historique-edition/0003.txt", lean),
          "au choix : sans les versions, sans l'historique de marche (l'historique des objets reste)");
    check(pa::freeFolder(scratch().string(), "Ligne: 2/3").find("Ligne_ 2_3") != std::string::npos, "un nom de dossier sans caractere interdit");
}

// ===================================================== 2. l'archive .xihm (IHM) ==
void archiveIhm(const fs::path& bac) {
    std::printf("2. l'archive .xihm d'un vrai projet : aller-retour fichier par fichier\n");
    core::setEdition(core::Edition::Ihm);
    const fs::path src = scratch() / "ihm" / "Armoire_Gaz";
    copyFolder(bac, src);
    // Ce que l'archive laisse : les artefacts et le verrou du build, une corbeille, une archive posee dans le
    // dossier. 1.12.31 : le cache (build-cache.txt) vient avec.
    writeAll(src / ".xpg" / "build" / "build-cache.txt", "cache");
    writeAll(src / ".xpg" / "build" / "build.lock", "verrou pid=4120\n");
    writeAll(src / ".xpg" / "build" / "ihm" / "08-vues" / "vue_3-Vue_A.txt", "artefact\n");
    writeAll(src / ".xpg" / "simulation" / "remanence.txt", "remanence 1\nfin\n");
    writeAll(src / "ihm" / "corbeille" / "0099-vieux.png", "vieux");
    writeAll(src / "ihm" / "historique-edition" / "0003.txt", "historique des objets\nfin\n");
    writeAll(src / "ancienne.xihm", "PK");
    // Un fichier externe hors du dossier : il vient avec, et le lien le suit.
    const fs::path outside = scratch() / "ihm" / "Partage" / "consignes_atelier.csv";
    writeAll(outside, "Nom;Valeur\nP1;7.5\n");
    {
        auto p = hmi::load(src.string());
        check(static_cast<bool>(p), "le projet d'essai se relit");
        if (!p) return;
        auto f = hmi::linkExternal(*p, "consignes_atelier", hmi::ExternalKind::Csv, outside.string(), "", src.string());
        p->assets.files.push_back(f);
        check(static_cast<bool>(hmi::save(*p, src.string())), "le projet d'essai, un lien hors du dossier en plus, s'enregistre");
    }

    hmi::ArchiveReport out;
    hmi::ProjectArchiveOptions opt;
    opt.tool = "IHM Studio 1.12.30";
    const fs::path xihm = scratch() / "ihm" / "Armoire_Gaz.xihm";
    check(static_cast<bool>(hmi::exportProjectArchive(src.string(), xihm.string(), opt, &out)), "exporter l'archive .xihm");
    check(fs::exists(xihm) && out.archiveBytes == fs::file_size(xihm), "le fichier .xihm, de la taille annoncee");
    bool manifest = false, card = false, views = false, data = false, versions = false, sim = false, edits = false, ext = false,
         cache = false, artifacts = false, trash = false, exports = false, nested = false;
    for (const auto& e : out.entries) {
        card = card || e.path == "project.xpgproj";
        views = views || e.path.rfind("ihm/vues/", 0) == 0;
        data = data || e.path == "donnees/parametres.xlsx";
        versions = versions || e.path == "versions/index.txt";
        sim = sim || e.path == ".xpg/simulation/remanence.txt";
        edits = edits || e.path == "ihm/historique-edition/0003.txt";
        ext = ext || e.path.rfind("_externes/", 0) == 0;
        cache = cache || e.path == ".xpg/build/build-cache.txt";
        artifacts = artifacts || (e.path.rfind(".xpg/build/", 0) == 0 && e.path != ".xpg/build/build-cache.txt");
        trash = trash || e.path.rfind("ihm/corbeille/", 0) == 0;
        exports = exports || e.path.rfind("exports/", 0) == 0;
        nested = nested || e.path == "ancienne.xihm";
    }
    (void)manifest;
    check(card && views && data && versions && sim && edits, "l'archive prend la fiche, les vues, les donnees, les versions, la simulation, l'historique des objets");
    check(ext, "l'archive prend le fichier externe lie hors du dossier (_externes/)");
    check(cache, "1.12.31 : l'archive prend le cache de construction (build-cache.txt)");
    check(!artifacts && !trash && !exports && !nested, "l'archive laisse les artefacts et le verrou du build, la corbeille, les exports et l'archive posee dedans");
    check(!out.skipped.empty(), "ce qu'elle laisse est dit (" + std::to_string(out.skipped.size()) + " ligne(s))");

    auto read = pa::readFile(xihm.string());
    check(static_cast<bool>(read) && read->intact() && read->hasManifest && read->manifest.kind == pa::Kind::Ihm,
          "relire l'archive : manifeste, genre IHM, chaque fichier intact");
    if (!read) return;
    check(hmi::refusal(*read).empty(), "IHM Studio l'accepte");

    // Deballer dans un dossier neuf : le lien externe suit sa copie (le fichier d'origine est parti).
    fs::remove(outside);
    const fs::path dest = scratch() / "ihm" / "out" / "Armoire_Gaz";
    std::error_code ec;
    fs::remove_all(dest.parent_path(), ec);
    hmi::ArchiveReport in;
    check(static_cast<bool>(hmi::unpackProjectArchive(*read, dest.string(), &in)), "deballer l'archive dans un dossier neuf");
    check(in.coherent(), "le controle : rien a redire (" + (in.problems.empty() ? std::string{} : in.problems.front()) + ")");
    bool allCounts = !in.counts.empty();
    for (const auto& c : in.counts) allCounts = allCounts && c.ok();
    check(allCounts, "compte par compte : vues, objets, alarmes, ressources, historique, versions...");
    check(sameFolders(src, dest, "xihm", {"ihm/ihm.txt", "externes/consignes_atelier.csv"}),
          "fichier par fichier, octets et dates : le dossier deballe est identique (le lien relie a part)");
    check(fs::exists(dest / "externes" / "consignes_atelier.csv"), "le fichier externe est pose dans externes/");
    bool relinked = false;
    for (const auto& n : in.notes) relinked = relinked || n.find("reli\xC3\xA9") != std::string::npos;
    check(relinked, "le lien du fichier externe suit sa copie, et c'est dit");
    {
        auto a = hmi::load(src.string()), b = hmi::load(dest.string());
        check(a && b, "les deux projets se relisent");
        if (a && b) {
            const hmi::ExternalFile* moved = nullptr;
            for (auto& f : b->assets.files) if (f.name == "consignes_atelier") moved = &f;
            check(moved && moved->path == "externes/consignes_atelier.csv"
                      && hmi::externalState(*moved, dest.string()).status == hmi::ExternalStatus::Present,
                  "le lien relie : externes/consignes_atelier.csv, present (taille et date d'origine)");
            // Le reste du projet IHM : identique.
            for (auto& f : b->assets.files) if (f.name == "consignes_atelier") f.path = outside.string();
            for (auto& f : a->assets.files) if (f.name == "consignes_atelier") f.path = outside.string();
            check(a->views == b->views && a->programs == b->programs && a->alarms == b->alarms && a->config == b->config
                      && a->assets.files == b->assets.files,
                  "le projet rouvert : vues, programmes, alarmes, configuration, fichiers externes identiques");
        }
    }
    // Un deuxieme deballage, le fichier d'origine revenu, identique : le lien ne change pas.
    writeAll(outside, "Nom;Valeur\nP1;7.5\n");
    const fs::path dest2 = scratch() / "ihm" / "out" / "Armoire_Gaz (2)";
    hmi::ArchiveReport in2;
    check(static_cast<bool>(hmi::unpackProjectArchive(*read, dest2.string(), &in2)) && in2.coherent(), "deballer encore, ailleurs");
    check(sameFolders(src, dest2, "xihm-2"), "le fichier d'origine la, identique : le dossier deballe est identique, ihm.txt compris");
    check(!fs::exists(dest2 / "externes"), "et pas de copie inutile dans externes/");
    // Le dossier pris n'est pas vide : refus.
    hmi::ArchiveReport busy;
    check(!hmi::unpackProjectArchive(*read, dest.string(), &busy), "deballer dans un dossier qui n'est pas vide : refuse");
    check(pa::freeFolder(dest.parent_path().string(), "Armoire_Gaz").find("Armoire_Gaz (3)") != std::string::npos,
          "le dossier propose est libre : Armoire_Gaz (3)");

    // Sans les versions ni l'historique de marche : plus petite, et le controle le sait.
    hmi::ProjectArchiveOptions lean;
    lean.versions = false;
    lean.history = false;
    hmi::ArchiveReport small;
    auto zip = hmi::projectArchive(src.string(), lean, &small);
    check(zip && zip->size() < out.archiveBytes, "sans versions ni historique de marche : plus petite ("
                                                     + std::to_string(zip ? zip->size() : 0) + " < " + std::to_string(out.archiveBytes) + ")");
    if (zip) {
        auto c = pa::read(*zip);
        const fs::path dest3 = scratch() / "ihm" / "out" / "leger";
        hmi::ArchiveReport in3;
        check(c && hmi::unpackProjectArchive(*c, dest3.string(), &in3) && in3.coherent(),
              "sans versions ni historique : se deballe, coherente (" + (in3.problems.empty() ? std::string{} : in3.problems.front()) + ")");
        check(!fs::exists(dest3 / "versions") && !fs::exists(dest3 / "ihm" / "historique"), "... et sans eux");
    }
}

// ================================================ 2 bis. le cache de construction ==
//  1.12.31 : L'ARCHIVE EMPORTE LE CACHE DE CONSTRUCTION. Comment il decide qu'il est valable : par
//  EMPREINTES seulement (hmi::pipeline - le contenu, la configuration, l'interface de chaque element, celles
//  de ses dependances, la version du generateur, et l'empreinte de chaque artefact, a un chemin relatif) ; ni
//  les dates ni un chemin absolu. Deballe ailleurs, il vaut donc ce qu'il valait - et les artefacts, qui se
//  deduisent du projet, sont refaits au deballage, chacun a l'empreinte que le cache a retenue.
namespace pl = hmi::pipeline;

pl::Report buildIn(const hmi::Project& p, const fs::path& folder, pl::Mode mode = pl::Mode::Start) {
    pl::Options o;
    o.buildFolder = pl::buildFolderOf(folder.string());
    o.projectFolder = folder.string();
    o.now = "2026-10-24 10:00:00";
    pl::Request request;
    request.mode = mode;
    return pl::run(p, pl::ApiInfo{}, request, o);
}

// L'analyse de l'arbre et de Demarrer : le cache du disque, les artefacts du disque.
pl::Analysis analysisOf(const hmi::Project& p, const fs::path& folder) {
    const std::string bf = pl::buildFolderOf(folder.string());
    return pl::analyse(pl::collect(p, pl::ApiInfo{}), pl::loadCache(bf), pl::diskArtifacts(bf));
}

// Les fichiers du build (sans le verrou ni la copie de secours) : chemin relatif -> contenu.
std::map<std::string, std::string> buildFiles(const fs::path& folder) {
    std::map<std::string, std::string> out;
    const fs::path bf = pl::buildFolderOf(folder.string());
    std::error_code ec;
    for (fs::recursive_directory_iterator it(bf, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string rel = fs::relative(it->path(), bf).generic_string();
        if (rel == "build.lock" || (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".bak") == 0)) continue;
        out[rel] = readAll(it->path());
    }
    return out;
}

std::string statusText(const pl::Analysis& a) {
    std::size_t todo = 0;
    std::string first;
    for (std::size_t k = 0; k < a.items.size(); ++k)
        if (a.items[k].status != pl::Status::UpToDate) {
            if (todo++ == 0) first = a.elements[k].key + (a.items[k].reasons.empty() ? std::string{} : " (" + a.items[k].reasons.front() + ")");
        }
    return std::to_string(a.elements.size()) + " elements, " + std::to_string(todo) + " a refaire" + (first.empty() ? std::string{} : ", dont " + first)
         + (a.fullReason.empty() ? std::string{} : " ; tout : " + a.fullReason);
}

void cacheDeConstruction(const fs::path& bac) {
    std::printf("2 bis. le cache de construction : exporter, deballer ailleurs, le build est a jour\n");
    core::setEdition(core::Edition::Ihm);
    const fs::path src = scratch() / "cache" / "Armoire_Gaz";
    copyFolder(bac, src);
    auto proj = hmi::load(src.string());
    check(static_cast<bool>(proj), "le projet du cache se relit");
    if (!proj) return;
    // Le build complet (Demarrer) du projet d'origine : son cache et ses artefacts sur le disque.
    const auto first = buildIn(*proj, src);
    const auto a0 = analysisOf(*proj, src);
    check(first.generated > 0 && a0.upToDate(), "le projet d'origine construit : " + std::to_string(first.generated) + " element(s) generes, "
                                                    + std::to_string(first.compiled) + " compiles ; a jour (" + statusText(a0) + ")");
    const auto originBuild = buildFiles(src);

    // ---- exporter : le cache vient, ses artefacts non (ils se refont au deballage)
    hmi::ArchiveReport out;
    auto zip = hmi::projectArchive(src.string(), hmi::ProjectArchiveOptions{}, &out);
    check(static_cast<bool>(zip), "exporter le projet construit");
    if (!zip) return;
    bool cacheIn = false, artifactsIn = false, told = false;
    for (const auto& e : out.entries) {
        cacheIn = cacheIn || e.path == ".xpg/build/build-cache.txt";
        artifactsIn = artifactsIn || (e.path.rfind(".xpg/build/", 0) == 0 && e.path != ".xpg/build/build-cache.txt");
    }
    for (const auto& sk : out.skipped) told = told || (sk.find(".xpg/build/") != std::string::npos && sk.find("refaits") != std::string::npos);
    check(cacheIn && !artifactsIn && told, "l'archive prend build-cache.txt, laisse les artefacts, et le dit");
    auto contents = pa::read(*zip);
    check(contents && contents->intact(), "l'archive se relit, intacte");
    if (!contents) return;

    // ---- deballer ailleurs : les artefacts refaits, octet pour octet ceux du build d'origine
    const fs::path dest = scratch() / "cache" / "ailleurs" / "Projets" / "Armoire_Gaz";
    std::error_code ec;
    fs::remove_all(dest.parent_path(), ec);
    hmi::ArchiveReport in;
    check(static_cast<bool>(hmi::unpackProjectArchive(*contents, dest.string(), &in)) && in.coherent(), "deballer dans un autre dossier");
    check(in.buildCache && in.artifacts == originBuild.size() - 1 && in.stale == 0,
          "le cache a suivi : " + std::to_string(in.artifacts) + " artefact(s) refaits (" + std::to_string(originBuild.size() - 1)
              + " attendus), aucun element a refaire (" + std::to_string(in.stale) + ")");
    const auto unpackedBuild = buildFiles(dest);
    std::size_t differ = 0;
    for (const auto& [rel, text] : originBuild) {
        const auto it = unpackedBuild.find(rel);
        if (it == unpackedBuild.end() || it->second != text) {
            if (differ++ < 4) std::printf("  build : %s %s\n", rel.c_str(), it == unpackedBuild.end() ? "manque" : "differe");
        }
    }
    check(differ == 0 && unpackedBuild.size() == originBuild.size(), "le dossier .xpg/build deballe : fichier par fichier, octet pour octet celui d'origine ("
                                                                         + std::to_string(unpackedBuild.size()) + " fichiers)");

    // ---- ouvrir : a jour, rien a regenerer ni a compiler (l'analyse, Demarrer, l'arbre de l'application)
    auto reopened = hmi::load(dest.string());
    check(static_cast<bool>(reopened), "le projet deballe se rouvre");
    if (!reopened) return;
    const auto a1 = analysisOf(*reopened, dest);
    check(a1.upToDate() && a1.fullReason.empty(), "rouvert ailleurs : a jour (" + statusText(a1) + ")");
    {
        app::HmiBuildManager manager([&]() -> std::optional<app::HmiBuildSetup> {
            app::HmiBuildSetup setup;
            setup.project = std::make_shared<const hmi::Project>(*reopened);
            setup.projectFolder = dest.string();
            return setup;
        });
        manager.analyseNow();
        (void)manager.poll();
        manager.wait();
        const auto st = manager.status();
        check(st && st->upToDate() && st->headline().rfind("Projet \xC3\xA0 jour", 0) == 0,
              "l'arbre et la barre d'etat : " + (st ? st->headline() : std::string("?")));
    }
    const auto r1 = buildIn(*reopened, dest);
    check(r1.ok && r1.upToDate && r1.generated == 0 && r1.compiled == 0 && r1.tasks == 0,
          "Demarrer : rien a generer ni a compiler (" + std::to_string(r1.generated) + " genere(s), " + std::to_string(r1.compiled) + " compile(s))");

    // ---- une source modifiee apres le deballage : regeneree, elle seule (jamais un cache perime pris pour bon)
    {
        hmi::Id vid = hmi::kNoId;
        for (auto& v : reopened->views) {
            if (v.role != "vue") continue;
            for (auto& o : v.objects)
                if (const auto* t = o.find("text"); t && t->expr.empty()) {
                    o.set("text", t->value + " (modifi\xC3\xA9)");
                    vid = v.id;
                    break;
                }
            if (vid != hmi::kNoId) break;
        }
        check(vid != hmi::kNoId && static_cast<bool>(hmi::save(*reopened, dest.string())), "une source modifiee apres le deballage (le texte d'un objet d'une vue)");
        auto again = hmi::load(dest.string());
        if (!again) return;
        const auto a2 = analysisOf(*again, dest);
        std::set<std::string> todo;
        bool ofThatView = true;
        for (std::size_t k = 0; k < a2.items.size(); ++k)
            if (a2.items[k].status != pl::Status::UpToDate) {
                todo.insert(a2.elements[k].key);
                ofThatView = ofThatView && (a2.elements[k].id == vid || a2.elements[k].owner == vid);
            }
        check(!a2.upToDate() && !todo.empty() && ofThatView, "l'analyse le voit : " + std::to_string(todo.size()) + " element(s) de cette vue a refaire, rien d'autre ("
                                                                 + statusText(a2) + ")");
        const auto r2 = buildIn(*again, dest);
        std::set<std::string> generated;
        for (const auto& l : r2.log)
            if (l.category == "G\xC3\xA9n\xC3\xA9ration" && !l.element.empty() && l.severity == pl::Severity::Information) generated.insert(l.element);
        check(r2.ok && !generated.empty() && generated == todo, "Demarrer les regenere, et rien d'autre (" + std::to_string(generated.size()) + " genere(s))");
        check(analysisOf(*again, dest).upToDate(), "puis a jour");
    }
    // ---- un artefact altere a la main, un autre supprime : leurs elements sont regeneres
    {
        auto again = hmi::load(dest.string());
        if (!again) return;
        const auto cache = pl::loadCache(pl::buildFolderOf(dest.string()));
        std::vector<std::string> keys;
        for (const auto& [key, en] : cache.entries)
            if (!en.artifact.empty() && keys.size() < 2) {
                const fs::path file = fs::path(pl::buildFolderOf(dest.string())) / en.artifact;
                if (keys.empty()) writeAll(file, readAll(file) + "# retouche\n");
                else fs::remove(file, ec);
                keys.push_back(key);
            }
        const auto a3 = analysisOf(*again, dest);
        const auto* i0 = keys.size() > 0 ? a3.item(keys[0]) : nullptr;
        const auto* i1 = keys.size() > 1 ? a3.item(keys[1]) : nullptr;
        check(i0 && i0->status == pl::Status::ArtifactMissing && i1 && i1->status == pl::Status::ArtifactMissing,
              "un artefact retouche, un autre supprime : tous deux \"artefact absent ou altere\"");
        const auto r3 = buildIn(*again, dest);
        check(r3.ok && r3.generated >= 2 && analysisOf(*again, dest).upToDate(), "Demarrer les regenere, puis a jour");
    }

    // ---- un fichier externe lie hors du projet : relie a sa copie au deballage - ce seul element est a refaire
    {
        const fs::path withLink = scratch() / "cache" / "lien" / "Armoire_Gaz";
        copyFolder(src, withLink);
        const fs::path outside = scratch() / "cache" / "lien" / "Partage" / "consignes.csv";
        writeAll(outside, "Nom;Valeur\nP1;7.5\n");
        auto p = hmi::load(withLink.string());
        if (!p) return;
        auto f = hmi::linkExternal(*p, "consignes", hmi::ExternalKind::Csv, outside.string(), "", withLink.string());
        p->assets.files.push_back(f);
        check(static_cast<bool>(hmi::save(*p, withLink.string())), "un lien hors du dossier, enregistre");
        auto p2 = hmi::load(withLink.string());
        if (!p2) return;
        (void)buildIn(*p2, withLink);
        check(analysisOf(*p2, withLink).upToDate(), "construit : a jour");
        auto z = hmi::projectArchive(withLink.string(), hmi::ProjectArchiveOptions{}, nullptr);
        auto c2 = z ? pa::read(*z) : core::Result<pa::Contents>(core::fail(core::ErrorCode::InvalidArgument, "zip"));
        fs::remove(outside, ec);
        const fs::path d2 = scratch() / "cache" / "lien" / "out" / "Armoire_Gaz";
        hmi::ArchiveReport in2;
        const bool unpacked2 = c2 && static_cast<bool>(hmi::unpackProjectArchive(*c2, d2.string(), &in2));
        check(unpacked2 && in2.buildCache && in2.stale == 1,
              "deballe ailleurs, le lien relie a sa copie : un element a refaire (" + std::to_string(in2.stale) + "), dit");
        auto p3 = hmi::load(d2.string());
        if (p3) {
            const auto a4 = analysisOf(*p3, d2);
            std::size_t todo = 0;
            std::string which;
            for (std::size_t k = 0; k < a4.items.size(); ++k)
                if (a4.items[k].status != pl::Status::UpToDate) { ++todo; which = a4.elements[k].key; }
            check(todo == 1 && which == "externe:" + std::to_string(f.id), "l'analyse : ce seul element (" + which + "), le reste est a jour");
        }
    }

    // ---- une archive d'avant (sans cache, comme la 1.12.30) : elle s'ouvre, et tout se regenere, comme avant
    {
        auto items = pa::collectFolder(src.string(), pa::CollectOptions{});
        if (!items) return;
        std::erase_if(*items, [](const pa::Item& it) { return it.path.rfind(".xpg/build/", 0) == 0; });
        pa::Manifest m = contents->manifest;
        m.tool = "IHM Studio 1.12.30";
        auto old = pa::pack(m, *items);
        auto c3 = old ? pa::read(*old) : core::Result<pa::Contents>(core::fail(core::ErrorCode::InvalidArgument, "pack"));
        const fs::path d3 = scratch() / "cache" / "avant" / "Armoire_Gaz";
        hmi::ArchiveReport in3;
        check(c3 && hmi::unpackProjectArchive(*c3, d3.string(), &in3) && in3.coherent() && !in3.buildCache && in3.artifacts == 0,
              "une archive sans cache (1.12.30) : deballee, coherente, pas de cache");
        auto p4 = hmi::load(d3.string());
        if (p4) {
            const auto a5 = analysisOf(*p4, d3);
            check(!a5.upToDate() && !a5.fullReason.empty(), "rouverte : tout est a refaire (" + a5.fullReason + ")");
            const auto r5 = buildIn(*p4, d3);
            check(r5.ok && r5.generated == a5.elements.size() && analysisOf(*p4, d3).upToDate(),
                  "Demarrer regenere tout (" + std::to_string(r5.generated) + " / " + std::to_string(a5.elements.size()) + "), puis a jour");
        }
    }
}

// ================================================ 3. abimees, recentes, etrangeres ==
void archivesRefusees() {
    std::printf("3. les archives abimees, plus recentes, d'une autre application ; les extensions inconnues\n");
    const fs::path xihm = scratch() / "ihm" / "Armoire_Gaz.xihm";
    std::string bytes = readAll(xihm);
    check(!bytes.empty(), "l'archive de l'essai 2 est la");
    if (bytes.empty()) return;
    // Un octet change au milieu d'un fichier stocke tel quel (une image) : le CRC le voit.
    const auto png = bytes.find("\x89PNG");
    check(png != std::string::npos, "une image est stockee telle quelle (deja compressee)");
    if (png != std::string::npos) {
        std::string bad = bytes;
        bad[png + 40] = static_cast<char>(bad[png + 40] ^ 0x5A);
        const fs::path p = scratch() / "abimee.xihm";
        writeAll(p, bad);
        auto c = pa::readFile(p.string());
        check(c && !c->intact() && !c->damaged.empty(), "un octet change : l'archive est dite abimee (" + (c && !c->problems.empty() ? c->problems.front() : std::string{}) + ")");
        if (c) {
            hmi::ArchiveReport r;
            const fs::path d = scratch() / "out-abimee";
            std::error_code ec;
            fs::remove_all(d, ec);
            auto st = hmi::unpackProjectArchive(*c, d.string(), &r);
            check(!st && st.error().context.find("ab\xC3\xAEm\xC3\xA9" "e") != std::string::npos, "abimee : rien n'est deballe, et c'est dit");
            check(!fs::exists(d), "... pas meme le dossier");
            hmi::ArchiveReport r2;
            check(static_cast<bool>(hmi::unpackProjectArchive(*c, d.string(), &r2, true)), "Deballer ce qui est intact : le reste se deballe");
            bool said = false;
            for (const auto& n : r2.notes) said = said || n.find("laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9") != std::string::npos;
            check(said, "... et le fichier abime est dit, laisse de cote");
        }
    }
    // Coupee (une copie interrompue).
    {
        const fs::path p = scratch() / "coupee.xihm";
        writeAll(p, bytes.substr(0, bytes.size() / 2));
        auto c = pa::readFile(p.string());
        check(!c, "une archive coupee : refusee, illisible (" + (c ? std::string{} : c.error().context) + ")");
    }
    // Pas un zip du tout.
    {
        const fs::path p = scratch() / "texte.xihm";
        writeAll(p, "ceci n'est pas une archive");
        auto c = pa::readFile(p.string());
        check(!c && c.error().context.find("abim\xC3\xA9") != std::string::npos, "un texte nomme .xihm : 'abime, coupe, ou d'un autre genre'");
    }
    // Une version plus recente : le format de l'archive, puis celui du projet IHM.
    {
        auto c = pa::readFile(xihm.string());
        if (c) {
            pa::Manifest m = c->manifest;
            std::vector<pa::Item> items;
            for (const auto& e : m.entries) {
                pa::Item it;
                it.path = e.path;
                const auto* f = c->file(e.path);
                it.data = std::make_shared<const pa::Bytes>(f->begin(), f->end());
                it.mtime = e.mtime;
                it.origin = e.origin;
                it.link = e.link;
                items.push_back(std::move(it));
            }
            m.format = pa::kFormat + 1;
            m.tool = "IHM Studio 1.13.0";
            auto newer = pa::pack(m, items);
            auto r = newer ? pa::read(*newer) : core::Result<pa::Contents>(core::fail(core::ErrorCode::InvalidArgument, "pack"));
            check(r && r->newer && hmi::refusal(*r).find("plus r\xC3\xA9" "cente") != std::string::npos
                      && hmi::refusal(*r).find("IHM Studio 1.13.0") != std::string::npos,
                  "une archive d'une version plus recente : refusee, avec la version qui l'a ecrite");
            hmi::ArchiveReport rr;
            check(r && !hmi::unpackProjectArchive(*r, (scratch() / "out-recente").string(), &rr), "... et pas deballee");
            m.format = pa::kFormat;
            m.dataFormat = 99;
            auto future = pa::pack(m, items);
            auto r2 = future ? pa::read(*future) : core::Result<pa::Contents>(core::fail(core::ErrorCode::InvalidArgument, "pack"));
            check(r2 && hmi::refusal(*r2).find("format 99") != std::string::npos, "un projet IHM d'un format plus recent (99) : refuse");
            m.dataFormat = 0;
            m.kind = pa::Kind::Api;
            auto api = pa::pack(m, items);
            auto r3 = api ? pa::read(*api) : core::Result<pa::Contents>(core::fail(core::ErrorCode::InvalidArgument, "pack"));
            check(r3 && hmi::refusal(*r3).find("API Studio") != std::string::npos, "l'archive d'un projet API : 'elle s'ouvre avec API Studio'");
        }
    }
    // Les extensions : une archive se reconnait, le reste non.
    {
        const fs::path txt = scratch() / "notes.txt";
        writeAll(txt, "PK des notes");
        check(!pa::sniff(txt.string()).archive, "une extension inconnue (.txt) : pas une archive");
        check(pa::sniff(xihm.string()).archive && pa::sniff(xihm.string()).kind == pa::Kind::Ihm, ".xihm : une archive de projet IHM");
    }
}

// ============================================================ 4. l'ancienne .zip ==
void ancienneArchive(const fs::path& bac) {
    std::printf("4. l'ancienne archive .zip de l'IHM (1.x) : relue, et ouverte en projet\n");
    const fs::path zip = bac / "Armoire_Gaz_ihm.zip";
    check(pa::sniff(zip.string()).archive, "le .zip de la 1.x se reconnait par son manifeste");
    auto c = pa::readFile(zip.string());
    check(c && c->hasManifest && c->manifest.format == 1 && !c->manifest.kindKnown && c->intact(), "relu : format 1, intact");
    if (!c) return;
    check(hmi::refusal(*c).empty(), "IHM Studio l'accepte");
    const fs::path dest = scratch() / "ancienne" / "Armoire_Gaz";
    std::error_code ec;
    fs::remove_all(dest.parent_path(), ec);
    hmi::ArchiveReport in;
    check(static_cast<bool>(hmi::unpackProjectArchive(*c, dest.string(), &in)), "deballee dans un dossier neuf");
    check(in.coherent(), "coherente (" + (in.problems.empty() ? std::string{} : in.problems.front()) + ")");
    check(!in.buildCache && in.artifacts == 0 && !fs::exists(dest / ".xpg" / "build"), "1.12.31 : pas de cache de construction (le premier build refera tout)");
    check(project::ProjectStore::isProjectFolder(dest.string()), "le dossier recoit sa fiche (project.xpgproj) : c'est un projet");
    auto m = project::ProjectStore::readManifest(dest.string());
    check(m && m->edition == "ihm" && m->name == "Armoire_Gaz", "la fiche : un projet IHM, au nom de l'archive");
    // L'IHM en place (le volet Exporter / Importer) relit aussi une archive .xihm.
    hmi::ArchiveReport inPlace;
    auto p = hmi::importArchive((scratch() / "ihm" / "Armoire_Gaz.xihm").string(), nullptr, &inPlace);
    check(p && inPlace.coherent(), "Importer (en place) relit l'IHM d'une archive .xihm");
}

// =========================================================== 5. l'archive .xapi ==
void archiveApi(const fs::path& bac, const fs::path& libs) {
    std::printf("5. l'archive .xapi : le programme, la disposition des grafcets, les macros\n");
    core::setEdition(core::Edition::Api);
    const fs::path src = scratch() / "api" / "Armoire_Gaz";
    copyFolder(bac, src);
    writeAll(scratch() / "api" / "Armoire_Gaz.xpglayout", "# xpglayout 1\nchart ManuA\nstep 0 120 40\n");
    pa::ApiOptions opt;
    opt.tool = "API Studio 1.12.30";
    opt.libraryRoot = libs.string();
    pa::ApiReport out;
    const fs::path xapi = scratch() / "api" / "Armoire_Gaz.xapi";
    check(static_cast<bool>(pa::exportApiArchive(src.string(), xapi.string(), opt, &out)), "exporter l'archive .xapi");
    bool program = false, layout = false, macros = false;
    for (const auto& e : out.manifest.entries) {
        program = program || e.path.rfind("sections/", 0) == 0;
        layout = layout || (e.origin == pa::Origin::Beside && e.path == "_a-cote/Armoire_Gaz.xpglayout");
        macros = macros || (e.origin == pa::Origin::Library && e.path.rfind("_macros/", 0) == 0);
    }
    check(program && layout && macros, "elle prend le programme, la disposition des grafcets (a cote), les macros");
    long long sections = -1;
    for (const auto& c : out.manifest.counts) if (c.key == "sections") sections = c.expected;
    check(sections > 0, "le manifeste annonce les sections (" + std::to_string(sections) + ")");
    auto read = pa::readFile(xapi.string());
    check(read && read->intact() && read->manifest.kind == pa::Kind::Api, "relue : genre API, intacte");
    if (!read) return;
    check(!hmi::refusal(*read).empty(), "IHM Studio la refuse ('elle s'ouvre avec API Studio')");

    const fs::path dest = scratch() / "api" / "out" / "Armoire_Gaz";
    const fs::path lib = scratch() / "api" / "libs";
    std::error_code ec;
    fs::remove_all(dest.parent_path(), ec);
    fs::remove_all(lib, ec);
    fs::create_directories(lib / "Macros", ec);
    writeAll(lib / "Macros" / "CreerSR.mac", "# la mienne\n");
    pa::UnpackOptions uo;
    uo.libraryRoot = lib.string();
    pa::ApiReport in;
    check(static_cast<bool>(pa::unpackApiArchive(*read, dest.string(), uo, &in)), "deballer l'archive .xapi");
    check(in.coherent(), "le controle : rien a redire (" + (in.problems.empty() ? std::string{} : in.problems.front()) + ")");
    bool allCounts = !in.counts.empty();
    for (const auto& c : in.counts) allCounts = allCounts && c.ok();
    check(allCounts, "compte par compte : sections, unites, DFB, DDT, variables, taches, modules, tables, versions");
    check(sameFolders(src, dest, "xapi"), "fichier par fichier : le dossier deballe est identique");
    check(fs::exists(dest.parent_path() / "Armoire_Gaz.xpglayout")
              && readAll(dest.parent_path() / "Armoire_Gaz.xpglayout") == readAll(scratch() / "api" / "Armoire_Gaz.xpglayout"),
          "la disposition des grafcets revient a cote du dossier");
    check(!in.unpacked.macrosAdded.empty() && readAll(lib / "Macros" / "CreerSR.mac") == "# la mienne\n",
          "les macros absentes s'ajoutent a la bibliotheque (" + std::to_string(in.unpacked.macrosAdded.size())
              + ") ; celle qui y est deja est gardee");
    auto again = project::ProjectStore::openModel(dest.string());
    check(again && again->project && !again->project->sections.empty(), "le projet deballe se rouvre (son programme)");

    // 1.12.31 : un dossier qui a aussi son IHM (un projet de la 1.11, ouvert par les deux), construite dans IHM Studio -
    // le .xapi prend le cache de son build (pas ses artefacts) ; au deballage, API Studio les refait d'apres lui
    // (ProjectExchange, comme ici) : IHM Studio trouve le projet a jour.
    {
        const fs::path built = scratch() / "api" / "cache" / "Armoire_Gaz";
        copyFolder(bac, built);
        core::setEdition(core::Edition::Ihm);
        auto hp = hmi::load(built.string());
        check(static_cast<bool>(hp), "1.12.31 : l'IHM du dossier se relit");
        if (!hp) return;
        (void)buildIn(*hp, built);
        const bool builtOk = analysisOf(*hp, built).upToDate();
        core::setEdition(core::Edition::Api);
        pa::ApiOptions o3;
        o3.macros = false;
        pa::ApiReport out3;
        auto zip3 = pa::apiArchive(built.string(), o3, &out3);
        bool cacheIn = false, artifactsIn = false;
        for (const auto& e : out3.manifest.entries) {
            cacheIn = cacheIn || e.path == ".xpg/build/build-cache.txt";
            artifactsIn = artifactsIn || (e.path.rfind(".xpg/build/", 0) == 0 && e.path != ".xpg/build/build-cache.txt");
        }
        check(builtOk && zip3 && cacheIn && !artifactsIn, "1.12.31 : son IHM construite, le .xapi prend le cache du build, pas ses artefacts");
        if (!zip3) return;
        auto back3 = pa::read(*zip3);
        const fs::path dest3 = scratch() / "api" / "cache" / "out" / "Armoire_Gaz";
        pa::ApiReport in3;
        check(back3 && static_cast<bool>(pa::unpackApiArchive(*back3, dest3.string(), pa::UnpackOptions{}, &in3)), "1.12.31 : deballe dans API Studio");
        pl::Restored rs;
        if (auto hp3 = hmi::load(dest3.string())) rs = pl::restoreArtifacts(*hp3, {}, pl::buildFolderOf(dest3.string()));
        core::setEdition(core::Edition::Ihm);
        auto hp4 = hmi::load(dest3.string());
        const auto a4 = hp4 ? analysisOf(*hp4, dest3) : pl::Analysis{};
        check(rs.cache && rs.written > 0 && rs.stale == 0 && hp4 && a4.upToDate(),
              "1.12.31 : ses artefacts refaits au deballage (" + std::to_string(rs.written) + ") : IHM Studio le trouve a jour (" + statusText(a4) + ")");
        core::setEdition(core::Edition::Api);
    }
}

// ====================================================== 6. un projet en argument ==
void allerRetour(const fs::path& given) {
    std::printf("7. l'aller-retour d'un vrai projet : %s\n", given.string().c_str());
    core::setEdition(core::Edition::Ihm);
    // 1.12.31 : une copie, construite d'abord (Demarrer) - son cache de construction est celui du jour.
    const fs::path folder = scratch() / "demo" / "src" / given.filename();
    copyFolder(given, folder);
    {
        auto p0 = hmi::load(folder.string());
        check(static_cast<bool>(p0), "le projet se relit pour son build");
        if (!p0) return;
        const auto b0 = std::chrono::steady_clock::now();
        const auto r0 = buildIn(*p0, folder);
        const auto b1 = std::chrono::steady_clock::now();
        const auto a0 = analysisOf(*p0, folder);
        std::printf("   build : %zu elements, %zu generes, %zu compiles, %d erreur(s) en %lld ms ; %s\n", a0.elements.size(), r0.generated, r0.compiled,
                    r0.errors, static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(b1 - b0).count()), statusText(a0).c_str());
    }
    hmi::ArchiveReport out;
    hmi::ProjectArchiveOptions opt;
    const fs::path xihm = scratch() / "demo" / (folder.filename().string() + ".xihm");
    const auto t0 = std::chrono::steady_clock::now();
    check(static_cast<bool>(hmi::exportProjectArchive(folder.string(), xihm.string(), opt, &out)), "exporter");
    const auto t1 = std::chrono::steady_clock::now();
    auto read = pa::readFile(xihm.string());
    check(read && read->intact(), "relire");
    if (!read) return;
    const fs::path dest = scratch() / "demo" / "out" / folder.filename();
    std::error_code ec;
    fs::remove_all(dest.parent_path(), ec);
    hmi::ArchiveReport in;
    check(static_cast<bool>(hmi::unpackProjectArchive(*read, dest.string(), &in)), "deballer");
    const auto t2 = std::chrono::steady_clock::now();
    check(in.coherent(), "controle coherent (" + (in.problems.empty() ? std::string{} : in.problems.front()) + ")");
    check(sameFolders(folder, dest, "demo"), "fichier par fichier, octets et dates : identique");
    // 1.12.31 : le build deballe en est au meme point que celui d'origine (a jour s'il l'etait).
    {
        auto po = hmi::load(folder.string());
        auto pd = hmi::load(dest.string());
        if (po && pd) {
            const auto ao = analysisOf(*po, folder), ad = analysisOf(*pd, dest);
            bool same = ao.items.size() == ad.items.size() && ao.fullReason == ad.fullReason;
            for (std::size_t k = 0; same && k < ao.items.size(); ++k) same = ao.items[k].status == ad.items[k].status;
            check(in.buildCache && in.stale == 0 && same, "le build deballe : au meme point que celui d'origine, element par element ("
                                                              + std::to_string(in.artifacts) + " artefacts refaits ; " + statusText(ad) + ")");
            check(!ao.upToDate() || ad.upToDate(), "construit a jour : rouvert a jour");
        }
    }
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    for (const auto& e : out.entries) { ++files; bytes += e.bytes; }
    std::printf("   %zu fichiers, %s de donnees -> archive de %s ; exporter %lld ms, relire et deballer %lld ms\n", files,
                pa::sizeText(bytes).c_str(), pa::sizeText(out.archiveBytes).c_str(),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()));
    for (const auto& c : in.counts) std::printf("   %-28s %lld / %lld\n", c.section.c_str(), c.expected, c.found);
    for (const auto& s : out.skipped) std::printf("   laisse : %s\n", s.c_str());
}

// ======================================================= 8. 1.13.0 (relecture) ==
// n31 : une page web (.html) ouverte dans API Studio tombait dans l'importeur ("no parser recognises this
// file", sous un titre anglais) et prenait la place des sources du projet en main. Le refus est dit avant.
void pageWebRefusee() {
    std::printf("8. une page web (.html) ouverte : refusee en francais, dans les deux editions\n");
    core::setEdition(core::Edition::Api);
    const auto api = app::App::webPageRefusal("D:/Projets/Ligne_gaz.html");
    check(api && api->title == "Pas un projet de l'automate" && api->text.rfind("Ligne_gaz.html : une page web ne s'ouvre plus", 0) == 0
              && api->text.find("API Studio ouvre") != std::string::npos && api->text.find(".XHW") != std::string::npos,
          "API Studio : Ligne_gaz.html refusee, en francais, avec ce qu'il ouvre (" + (api ? api->title : std::string("rien")) + ")");
    check(app::App::webPageRefusal("/tmp/Notice.HTM").has_value(), "API Studio : .HTM (en majuscules) aussi");
    check(!app::App::webPageRefusal("/tmp/Config.XHW") && !app::App::webPageRefusal("/tmp/Ligne.xapi")
              && !app::App::webPageRefusal("/tmp/MAST.XPG") && !app::App::webPageRefusal("/tmp/html"),
          "API Studio : un export .XHW, une archive .xapi, un .XPG, un dossier nomme html : pas refuses ici");
    core::setEdition(core::Edition::Ihm);
    const auto ihm = app::App::webPageRefusal("/tmp/Synoptique.html");
    check(ihm && ihm->title == "Pas un projet IHM" && ihm->text.find("IHM Studio ouvre") != std::string::npos
              && ihm->text.find("API Studio") == std::string::npos,
          "IHM Studio : la page refusee, avec ce qu'IHM Studio ouvre (sans parler d'API Studio)");
}

namespace pk = hmi::pkg;

hmi::HmiFunction method(hmi::Project& p, const char* name, const char* body) {
    hmi::HmiFunction m;
    m.id = p.allocate();
    m.name = name;
    m.body = body;
    m.isVirtual = true;
    return m;
}
hmi::HmiType structure(hmi::Project& p, const char* name) {
    hmi::HmiType t;
    t.id = p.allocate();
    t.name = name;
    for (const char* n : {"Etat", "Position"}) {
        hmi::TypeMember m;
        m.name = n;
        m.type = std::string(n) == "Etat" ? "BOOL" : "REAL";
        t.members.push_back(m);
    }
    return t;
}

// n22 : l'import d'un paquet - un type qui ne differe que par ses methodes, son parent ou ses interfaces, une
// fonction qui ne differe que par ses cas de test ou ses generiques, etaient "identiques, garde(e)s".
void paquetsObjets() {
    std::printf("8 bis. un paquet de types ou de fonctions : methodes, heritage, interfaces, generiques, cas de test\n");
    core::setEdition(core::Edition::Ihm);
    // ---- un paquet de types : T_Vanne EXTENDS T_Equipement IMPLEMENTS I_Demarrable, deux methodes
    hmi::Project src;
    src.programs.types.push_back(structure(src, "T_Equipement"));
    hmi::HmiType iface;
    iface.id = src.allocate();
    iface.name = "I_Demarrable";
    iface.kind = hmi::HmiTypeKind::Interface;
    iface.methods.push_back(method(src, "Ouvrir", ""));
    src.programs.types.push_back(iface);
    hmi::HmiType vanne = structure(src, "T_Vanne");
    vanne.base = "T_Equipement";
    vanne.interfaces = {"I_Demarrable"};
    vanne.methods.push_back(method(src, "Ouvrir", "Etat := TRUE;"));
    vanne.methods.push_back(method(src, "Fermer", "Etat := FALSE;"));
    hmi::FunctionTest mt;
    mt.id = src.allocate();
    mt.name = "ouvre";
    vanne.methods.back().tests.push_back(mt);
    src.programs.types.push_back(vanne);
    const hmi::Id vanneId = src.programs.types.back().id;
    const auto tpkg = pk::collectPrograms(src, pk::ProgramKind::Types, {vanneId});
    const auto has = [&](const char* n) {
        return std::any_of(tpkg.content.programs.types.begin(), tpkg.content.programs.types.end(), [&](const hmi::HmiType& t) { return t.name == n; });
    };
    check(has("T_Vanne") && has("T_Equipement") && has("I_Demarrable"), "le paquet de T_Vanne emporte son parent et son interface");

    // Le projet a un T_Vanne aux memes membres, sans methode, sans parent ni interface.
    hmi::Project dst;
    dst.nextId = 1000;
    dst.programs.types.push_back(structure(dst, "T_Vanne"));
    auto plan = pk::plan(dst, tpkg);
    const pk::Item* it = plan.find(pk::ItemKind::Type, "T_Vanne");
    check(it && it->state == pk::State::Different && it->detail.find("identique") == std::string::npos,
          "T_Vanne du paquet (methodes, EXTENDS, IMPLEMENTS) : different de celui du projet, pas identique (" + (it ? it->detail : std::string("?")) + ")");
    if (pk::Item* m = plan.find(pk::ItemKind::Type, "T_Vanne")) m->choice = pk::Choice::Replace;
    (void)pk::importInto(dst, tpkg, plan);
    const hmi::HmiType* got = dst.hmiTypeByName("T_Vanne");
    check(got && got->methods.size() == 2 && got->base == "T_Equipement" && got->interfaces == std::vector<std::string>{"I_Demarrable"},
          "remplace : le T_Vanne du projet recoit ses deux methodes, son parent et son interface");
    check(got && got->methods.size() == 2 && got->methods[0].id >= 1000 && got->methods[1].id >= 1000
              && !got->methods[1].tests.empty() && got->methods[1].tests[0].id >= 1000,
          "ses methodes et leurs cas recoivent des identifiants du projet (pas ceux du paquet)");
    // Ajoute (absent du projet) : les memes identifiants neufs.
    hmi::Project fresh;
    fresh.nextId = 5000;
    (void)pk::importInto(fresh, tpkg, pk::plan(fresh, tpkg));
    const hmi::HmiType* added = fresh.hmiTypeByName("T_Vanne");
    check(added && added->methods.size() == 2 && added->methods[0].id >= 5000 && added->methods[1].tests[0].id >= 5000,
          "ajoute : ses methodes et leurs cas renumerotes");
    // Le meme type, identique en tout : toujours identique.
    hmi::Project twin = src;
    const auto twinPlan = pk::plan(twin, tpkg);
    const pk::Item* same = twinPlan.find(pk::ItemKind::Type, "T_Vanne");
    check(same && same->state == pk::State::Same, "le meme T_Vanne (methodes comprises) : identique, garde");

    // ---- un paquet de fonctions : Moyenne, la meme que celle du projet, mais avec un cas de test et un generique
    hmi::Project fsrc;
    hmi::HmiFunction moy;
    moy.id = fsrc.allocate();
    moy.name = "Moyenne";
    moy.returnType = "REAL";
    moy.body = "Moyenne := (A + B) / 2.0;";
    hmi::FunctionTest ft;
    ft.id = fsrc.allocate();
    ft.name = "deux";
    ft.arguments = {"1.0", "3.0"};
    ft.expected = "2.0";
    moy.tests.push_back(ft);
    moy.returnUnit = "bar";
    fsrc.programs.functions.push_back(moy);
    const auto fpkg = pk::collectPrograms(fsrc, pk::ProgramKind::Functions, {moy.id});
    hmi::Project fdst;
    fdst.nextId = 2000;
    hmi::HmiFunction ours = moy;
    ours.id = fdst.allocate();
    ours.tests.clear();
    ours.returnUnit.clear();
    fdst.programs.functions.push_back(ours);
    auto fplan = pk::plan(fdst, fpkg);
    const pk::Item* fi = fplan.find(pk::ItemKind::Function, "Moyenne");
    check(fi && fi->state == pk::State::Different, "Moyenne du paquet, avec un cas de test et une unite : differente (" + (fi ? fi->detail : std::string("?")) + ")");
    if (pk::Item* m = fplan.find(pk::ItemKind::Function, "Moyenne")) m->choice = pk::Choice::Replace;
    (void)pk::importInto(fdst, fpkg, fplan);
    const hmi::HmiFunction* fgot = fdst.functionByName("Moyenne");
    check(fgot && fgot->tests.size() == 1 && fgot->tests[0].name == "deux" && fgot->tests[0].id >= 2000 && fgot->returnUnit == "bar",
          "remplacee : elle recoit son cas (identifiant du projet) et l'unite de son retour");
    // Une fonction identique a ses identifiants pres (les cas compris) : identique.
    hmi::Project fsame;
    fsame.nextId = 3000;
    hmi::HmiFunction copy = moy;
    copy.id = fsame.allocate();
    copy.tests[0].id = fsame.allocate();
    fsame.programs.functions.push_back(copy);
    const auto sp = pk::plan(fsame, fpkg);
    const pk::Item* si = sp.find(pk::ItemKind::Function, "Moyenne");
    check(si && si->state == pk::State::Same, "la meme Moyenne, son cas compris (autres identifiants) : identique");
    // Une generique de plus : differente.
    hmi::Project fgen = fsame;
    fgen.programs.functions.back().generics = {{"T", "ANY_NUM"}};
    const auto gp = pk::plan(fgen, fpkg);
    const pk::Item* gi = gp.find(pk::ItemKind::Function, "Moyenne");
    check(gi && gi->state == pk::State::Different, "un generique de difference : differente");
}

// La seconde relecture (lot D2) : un paquet et les espaces de noms (T2-53), un renommage dans le paquet qui suit l'heritage
// et les methodes (R27), les variables des cas de test qui voyagent (R29).
void paquetsEspaces() {
    std::printf("8 ter. un paquet et les espaces de noms, l'heritage renomme, les variables des cas de test\n");
    core::setEdition(core::Edition::Ihm);
    // T2-53 : le paquet porte Gaz.T_Vanne ; le projet a Util.T_Vanne (memes membres).
    {
        hmi::Project src;
        hmi::HmiType g = structure(src, "T_Vanne");
        g.ns = "Gaz";
        src.programs.types.push_back(g);
        const auto tpkg = pk::collectPrograms(src, pk::ProgramKind::Types, {src.programs.types.back().id});
        hmi::Project dst;
        dst.nextId = 1000;
        hmi::HmiType u = structure(dst, "T_Vanne");
        u.ns = "Util";
        dst.programs.types.push_back(u);
        const auto plan = pk::plan(dst, tpkg);
        const pk::Item* it = plan.find(pk::ItemKind::Type, "T_Vanne");
        check(it && it->state == pk::State::Missing && it->choice == pk::Choice::Add && it->ns == "Gaz",
              "T2-53 : Gaz.T_Vanne du paquet n'est pas Util.T_Vanne du projet : absent, ajoute (" + (it ? it->detail : std::string("?")) + ")");
        (void)pk::importInto(dst, tpkg, plan);
        check(dst.hmiTypeByName("Gaz.T_Vanne") && dst.hmiTypeByName("Util.T_Vanne") && dst.programs.types.size() == 2,
              "T2-53 : Gaz.T_Vanne ajoute a cote de Util.T_Vanne");
        // Le projet a deja Gaz.T_Vanne ET Util.T_Vanne : identique, pas de doublon.
        hmi::Project both = dst;
        const auto again = pk::plan(both, tpkg);
        const pk::Item* same = again.find(pk::ItemKind::Type, "T_Vanne");
        check(same && same->state == pk::State::Same, "T2-53 : Gaz.T_Vanne deja la (avec Util.T_Vanne) : identique, garde (pas de doublon)");
        (void)pk::importInto(both, tpkg, again);
        check(both.programs.types.size() == 2, "T2-53 : aucun Gaz.T_Vanne en double");
    }
    // T2-53 : une fonction d'espace n'est pas appariee a l'homonyme d'un autre espace.
    {
        hmi::Project src;
        hmi::HmiFunction f;
        f.id = src.allocate();
        f.name = "EnDefaut";
        f.ns = "Gaz";
        f.returnType = "BOOL";
        f.body = "EnDefaut := TRUE;";
        src.programs.functions.push_back(f);
        const auto fpkg = pk::collectPrograms(src, pk::ProgramKind::Functions, {f.id});
        hmi::Project dst;
        dst.nextId = 1000;
        hmi::HmiFunction g = f;
        g.id = dst.allocate();
        g.ns = "Util";
        dst.programs.functions.push_back(g);
        const auto plan = pk::plan(dst, fpkg);
        const pk::Item* it = plan.find(pk::ItemKind::Function, "EnDefaut");
        check(it && it->state == pk::State::Missing, "T2-53 : Gaz.EnDefaut n'est pas Util.EnDefaut : absente, ajoutee");
        (void)pk::importInto(dst, fpkg, plan);
        check(dst.functionByName("Gaz.EnDefaut") && dst.functionByName("Util.EnDefaut"), "T2-53 : Gaz.EnDefaut ajoutee a cote de Util.EnDefaut");
    }
    // R27 : un type renomme a l'import - son sous-type du paquet (EXTENDS) et la methode qui le nomme suivent.
    {
        hmi::Project src;
        src.programs.types.push_back(structure(src, "T_Vanne"));
        hmi::HmiType mot = structure(src, "T_Vanne_Motorisee");
        mot.base = "T_Vanne";
        hmi::HmiFunction m = method(src, "Copie", "VAR v : T_Vanne; END_VAR\nv.Etat := TRUE;");
        m.returnType = "T_Vanne";
        mot.methods.push_back(m);
        src.programs.types.push_back(mot);
        src.programs.types.front().methods.push_back(method(src, "Ouvrir", "Etat := TRUE;"));   // differe du projet : renomme
        const auto tpkg = pk::collectPrograms(src, pk::ProgramKind::Types, {src.programs.types[0].id, src.programs.types[1].id});
        hmi::Project dst;
        dst.nextId = 1000;
        dst.programs.types.push_back(structure(dst, "T_Vanne"));
        auto plan = pk::plan(dst, tpkg);
        const pk::Item* it = plan.find(pk::ItemKind::Type, "T_Vanne");
        check(it && it->choice == pk::Choice::Rename && !it->newName.empty(), "R27 : T_Vanne different, choisi : importe sous un autre nom");
        const std::string newName = it ? it->newName : std::string{};
        (void)pk::importInto(dst, tpkg, plan);
        const hmi::HmiType* sub = dst.hmiTypeByName("T_Vanne_Motorisee");
        check(sub && sub->base == newName, "R27 : le sous-type importe herite du type renomme (" + (sub ? sub->base : std::string("?")) + ")");
        check(sub && !sub->methods.empty() && sub->methods.front().returnType == newName
                  && sub->methods.front().body.find(newName) != std::string::npos,
              "R27 : la methode du sous-type (retour, corps) vise le type renomme");
    }
    // R29 : une fonction dont un cas de test pose Compteur_Clics emporte la variable.
    {
        hmi::Project src;
        hmi::Variable v;
        v.id = src.allocate();
        v.name = "Compteur_Clics";
        v.type = "INT";
        src.programs.variables.push_back(v);
        hmi::HmiFunction f;
        f.id = src.allocate();
        f.name = "Incrementer";
        f.body = "Compteur_Clics := Compteur_Clics + 1;";
        hmi::FunctionTest t;
        t.id = src.allocate();
        t.name = "un";
        t.givens.push_back({"Compteur_Clics", "0"});
        t.checks.push_back({"Compteur_Clics", "1", ""});
        f.tests.push_back(t);
        f.body = "RETURN;";                                   // le corps ne nomme plus la variable : seul le cas la pose
        src.programs.functions.push_back(f);
        const auto fpkg = pk::collectPrograms(src, pk::ProgramKind::Functions, {f.id});
        check(std::any_of(fpkg.content.programs.variables.begin(), fpkg.content.programs.variables.end(),
                          [](const hmi::Variable& x) { return x.name == "Compteur_Clics"; }),
              "R29 : le paquet de la fonction emporte la variable que posent ses cas de test");
    }
    // D2b-8 : un paquet a espaces porte Gaz.T_Vanne (different de celui du projet : importe sous un autre nom) et
    // Util.T_Vanne. Le renommage DANS LE PAQUET suit l'identite : Gaz.T_Vanne et le T_Vanne court lu depuis Gaz prennent
    // le nouveau nom ; Util.T_Vanne, et le T_Vanne court lu depuis Util, restent (avant : chaque mot T_Vanne changeait).
    {
        hmi::Project src;
        const auto typed = [](const char* name, const char* type) {
            hmi::TypeMember m;
            m.name = name;
            m.type = type;
            return m;
        };
        hmi::HmiType g = structure(src, "T_Vanne");
        g.ns = "Gaz";
        g.methods.push_back(method(src, "Ouvrir", "Etat := TRUE;"));          // differe du Gaz.T_Vanne du projet
        g.members.push_back(typed("Secours", "Util.T_Vanne"));
        src.programs.types.push_back(g);
        hmi::HmiType u = structure(src, "T_Vanne");
        u.ns = "Util";
        src.programs.types.push_back(u);
        hmi::HmiType banc = structure(src, "T_Banc");
        banc.ns = "Util";
        banc.members.push_back(typed("A", "T_Vanne"));                         // lu depuis Util : Util.T_Vanne
        banc.members.push_back(typed("B", "Util.T_Vanne"));
        banc.members.push_back(typed("C", "Gaz.T_Vanne"));
        banc.methods.push_back(method(src, "Copier", "VAR\n    v : T_Vanne;\n    w : Gaz.T_Vanne;\nEND_VAR\nv.Etat := w.Etat;"));
        src.programs.types.push_back(banc);
        hmi::HmiType poste = structure(src, "T_Poste");
        poste.ns = "Gaz";
        poste.members.push_back(typed("D", "T_Vanne"));                        // lu depuis Gaz : Gaz.T_Vanne
        src.programs.types.push_back(poste);
        std::vector<hmi::Id> ids;
        for (const auto& t : src.programs.types) ids.push_back(t.id);
        const auto tpkg = pk::collectPrograms(src, pk::ProgramKind::Types, ids);
        hmi::Project dst;
        dst.nextId = 1000;
        hmi::HmiType ours = structure(dst, "T_Vanne");
        ours.ns = "Gaz";
        dst.programs.types.push_back(ours);
        auto plan = pk::plan(dst, tpkg);
        std::string newName;
        for (const auto& it : plan.items)
            if (it.kind == pk::ItemKind::Type && it.name == "T_Vanne" && it.ns == "Gaz" && it.choice == pk::Choice::Rename) newName = it.newName;
        check(!newName.empty(), "D2b-8 : Gaz.T_Vanne du paquet, different, s'importe sous un autre nom (" + newName + ")");
        (void)pk::importInto(dst, tpkg, plan);
        const auto typeOf = [&](const char* full) -> const hmi::HmiType* { return dst.hmiTypeByName(full); };
        const auto memberType = [](const hmi::HmiType* t, const char* m) -> std::string {
            if (t)
                for (const auto& x : t->members)
                    if (x.name == m) return x.type;
            return "?";
        };
        const hmi::HmiType* gz = typeOf(("Gaz." + newName).c_str());
        const hmi::HmiType* ut = typeOf("Util.T_Vanne");
        const hmi::HmiType* bc = typeOf("Util.T_Banc");
        const hmi::HmiType* ps = typeOf("Gaz.T_Poste");
        check(gz && ut && bc && ps && typeOf("Gaz.T_Vanne") && typeOf("Gaz.T_Vanne")->methods.empty(),
              "D2b-8 : le projet a son Gaz.T_Vanne, et Gaz." + newName + ", Util.T_Vanne, Util.T_Banc, Gaz.T_Poste importes");
        check(memberType(gz, "Secours") == "Util.T_Vanne", "D2b-8 : Gaz." + newName + ".Secours vise toujours Util.T_Vanne (" + memberType(gz, "Secours") + ")");
        check(memberType(bc, "A") == "T_Vanne" && memberType(bc, "B") == "Util.T_Vanne",
              "D2b-8 : Util.T_Banc : A (T_Vanne lu depuis Util) et B (Util.T_Vanne) restent (" + memberType(bc, "A") + ", " + memberType(bc, "B") + ")");
        check(memberType(bc, "C") == "Gaz." + newName, "D2b-8 : Util.T_Banc.C (Gaz.T_Vanne) suit : " + memberType(bc, "C"));
        check(memberType(ps, "D") == newName, "D2b-8 : Gaz.T_Poste.D (T_Vanne lu depuis Gaz) suit : " + memberType(ps, "D"));
        const std::string body = bc && !bc->methods.empty() ? bc->methods.front().body : std::string{};
        check(body.find("v : T_Vanne;") != std::string::npos && body.find("w : Gaz." + newName + ";") != std::string::npos,
              "D2b-8 : la methode de Util.T_Banc : v : T_Vanne reste, w : Gaz.T_Vanne suit [" + body + "]");
    }
}

} // namespace

int main(int argc, char** argv) {
    const fs::path bac = argc > 1 ? fs::path(argv[1]) : fs::path("bac/Armoire_Gaz");
    const fs::path libs = argc > 2 ? fs::path(argv[2]) : fs::path("libs");
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    fs::create_directories(scratch(), ec);
    baseCommune();
    pageWebRefusee();            // 1.13.0 (relecture)
    paquetsObjets();
    paquetsEspaces();             // 1.13.0 (seconde relecture)
    if (fs::exists(bac / "project.xpgproj")) {
        archiveIhm(bac);
        cacheDeConstruction(bac);         // 1.12.31 : le cache de construction voyage
        archivesRefusees();
        ancienneArchive(bac);
        archiveApi(bac, libs);
    } else {
        std::printf("ECHEC : %s n'est pas le projet d'essai (bac/Armoire_Gaz)\n", bac.string().c_str());
        ++failures;
    }
    if (argc > 3) allerRetour(fs::path(argv[3]));
    // XPG_ECHANGES_GARDER=1 : les archives et les pages restent dans le dossier temporaire (les regarder).
    if (!std::getenv("XPG_ECHANGES_GARDER")) fs::remove_all(scratch(), ec);
    else std::printf("gard\xC3\xA9 : %s\n", scratch().string().c_str());
    std::printf("%d verification(s), %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
