// =============================================================================
//  tests/sound_library_test.cpp - 1.24.0 : la bibliotheque de sons (hmi::soundlib)
// -----------------------------------------------------------------------------
//  Les sons natifs (fabriques : des WAV lisibles, la bonne duree, ni muets ni
//  satures, toujours les memes), la recherche, Mes sons (un dossier : ajouter,
//  le meme contenu, un autre sous le meme nom, refuser ce qui n'est pas un son,
//  retirer), importer dans le projet (une ressource, puis reutilisee).
// =============================================================================
#include "../src/hmi/HmiMedia.hpp"
#include "../src/hmi/HmiSoundLibrary.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

namespace sl = hmi::soundlib;
namespace fs = std::filesystem;

std::string stamp() {
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

bool found(const std::string& query, const std::string& id) {
    for (const auto& s : sl::builtIn())
        if (s.id == id) return sl::matches(s, query);
    return false;
}

void natives() {
    std::printf("[1] les sons natifs\n");
    const auto& all = sl::builtIn();
    check(all.size() == 22, "22 sons natifs");
    check(sl::builtInCategories().size() == 4 && sl::builtInCategories().front() == "Alarmes", "4 categories, les alarmes d'abord");
    bool everyOk = true, everyCategory = true;
    for (const auto& s : all) {
        if (!s.data || s.mine() || s.format != "WAV" || s.description.empty()) everyOk = false;
        if (std::find(sl::builtInCategories().begin(), sl::builtInCategories().end(), s.category) == sl::builtInCategories().end())
            everyCategory = false;
        const hmi::MediaInfo m = hmi::inspectMedia(*s.data, sl::resourceName(s));
        if (m.kind != hmi::MediaKind::Sound || !m.ok() || m.format != "WAV" || m.channels != 1 || m.sampleRate != sl::kRate
            || m.bitsPerSample != 16 || std::fabs(m.seconds - s.seconds) > 0.01)
            everyOk = false;
        const auto pcm = sl::samplesOf(s.id);
        int peak = 0;
        for (const auto v : pcm) peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (peak < 4000 || peak > 32000) {
            everyOk = false;
            std::printf("    %s : crete %d\n", s.id.c_str(), peak);
        }
        if (s.seconds < 0.02 || s.seconds > 3.0) everyOk = false;
    }
    check(everyOk, "chacun : un WAV PCM 16 bits mono a 22 050 Hz, sa duree, ni muet ni sature");
    check(everyCategory, "chacun dans une categorie connue");
    const auto* high = sl::findBuiltIn("alarme-haute");
    check(high && std::fabs(high->seconds - 1.52) < 0.02 && high->label == "Alarme haute priorit\xC3\xA9", "l'alarme haute : 2 salves de 3 bips (1,52 s)");
    check(sl::samplesOf("carillon") == sl::samplesOf("carillon"), "toujours le meme son (la synthese est fixe)");
    check(sl::findBuiltIn("inconnu") == nullptr && sl::samplesOf("inconnu").empty(), "un id inconnu : rien");
    // Les alarmes sont rangees de la plus grave a la plus douce : la critique d'abord.
    check(all.front().id == "alarme-critique", "la critique en tete");
}

void search() {
    std::printf("[2] chercher\n");
    check(found("sirene", "alarme-critique") && found("sirene", "sirene-montante") && !found("sirene", "carillon"), "sirene : les deux sirenes");
    check(found("succ\xC3\xA8s", "succes") && found("SUCCES", "succes"), "sans accents ni casse");
    check(found("proc", "demarrage") && !found("proc", "clic"), "le debut d'un mot de la categorie (Procede)");
    check(found("alarme haute", "alarme-haute") && !found("alarme haute", "alarme-basse"), "chaque mot doit commencer un mot");
    check(found("", "clic"), "une recherche vide : tout");
}

void mine() {
    std::printf("[3] Mes sons\n");
    const fs::path dir = fs::temp_directory_path() / ("xpg_sons_" + stamp());
    std::error_code ec;
    fs::remove_all(dir, ec);
    sl::setUserFolder("");
    std::string why;
    check(!sl::addUserSound("x.wav", *sl::findBuiltIn("clic")->data, nullptr, nullptr, &why) && why.find("dossier") != std::string::npos,
          "sans dossier : refuse");
    const auto rev0 = sl::userRevision();
    sl::setUserFolder(dir.string());
    check(sl::userRevision() > rev0 && sl::userSounds().empty(), "un dossier neuf : vide (le numero monte)");
    std::string stored;
    bool existed = true;
    check(sl::addUserSound("Mon bip.wav", *sl::findBuiltIn("bip-court")->data, &stored, &existed) && stored == "Mon bip.wav" && !existed,
          "ajouter : Mon bip.wav");
    check(sl::addUserSound("Autre nom.wav", *sl::findBuiltIn("bip-court")->data, &stored, &existed) && existed && stored == "Mon bip.wav",
          "le meme contenu : deja la, rien d'ecrit");
    check(sl::addUserSound("Mon bip.wav", *sl::findBuiltIn("carillon")->data, &stored, &existed) && stored == "Mon bip (2).wav",
          "un autre contenu sous le meme nom : Mon bip (2).wav");
    const hmi::Bytes notSound = {'h', 'e', 'l', 'l', 'o'};
    check(!sl::addUserSound("texte.wav", notSound, nullptr, nullptr, &why) && why.find("n'est pas un son") != std::string::npos,
          "ce qui n'est pas un son : refuse");
    auto mineList = sl::userSounds();
    check(mineList.size() == 2 && mineList[0].label == "Mon bip" && mineList[0].mine() && mineList[0].category == "Mes sons"
              && mineList[0].id == "mes/Mon bip.wav" && std::fabs(mineList[0].seconds - sl::findBuiltIn("bip-court")->seconds) < 0.01,
          "Mes sons : deux, par nom, avec leur duree");
    // Un fichier du disque.
    const fs::path file = dir / ".." / ("xpg_sons_fichier_" + stamp() + ".wav");
    {
        std::FILE* f = std::fopen(file.string().c_str(), "wb");
        const auto& d = *sl::findBuiltIn("succes")->data;
        if (f) {
            std::fwrite(d.data(), 1, d.size(), f);
            std::fclose(f);
        }
    }
    check(sl::addUserFile(file.string(), &stored) && sl::userSounds().size() == 3, "un fichier du disque");
    fs::remove(file, ec);
    const auto rev1 = sl::userRevision();
    check(sl::removeUserSound(sl::userSounds().front()) && sl::userSounds().size() == 2 && sl::userRevision() > rev1, "retirer un de Mes sons");
    check(!sl::removeUserSound(*sl::findBuiltIn("clic"), &why) && why.find("natif") != std::string::npos, "un son natif ne se retire pas");
    fs::remove_all(dir, ec);
    sl::setUserFolder("");
}

void import() {
    std::printf("[4] importer dans le projet\n");
    hmi::Project p;
    const auto& high = *sl::findBuiltIn("alarme-haute");
    check(sl::importedAs(p, high) == nullptr, "pas encore dans le projet");
    const auto a = sl::importSound(p, high);
    check(a.id != hmi::kNoId && !a.reused && a.name == "alarme-haute.wav" && p.assets.resources.size() == 1, "une ressource : alarme-haute.wav");
    const hmi::Resource* r = p.resource(a.id);
    check(r && r->kind() == hmi::MediaKind::Sound && std::fabs(r->seconds - high.seconds) < 0.01 && r->origin.find("Biblioth") == 0,
          "un son, sa duree, son origine dite");
    const auto b = sl::importSound(p, high);
    check(b.reused && b.id == a.id && p.assets.resources.size() == 1, "importe deux fois : la meme ressource");
    check(sl::importedAs(p, high) && sl::importedAs(p, high)->id == a.id, "importedAs la retrouve");
    // Un nom deja pris par un autre contenu : nom_2 (la regle des ressources).
    sl::Sound other = *sl::findBuiltIn("carillon");
    other.id = "alarme-haute";
    const auto c = sl::importSound(p, other);
    check(!c.reused && c.name != "alarme-haute.wav" && p.assets.resources.size() == 2, "un nom pris par un autre contenu : un autre nom");
}

void texts() {
    std::printf("[5] les durees\n");
    check(sl::durationText(1.52) == "1,52 s" && sl::durationText(1.4) == "1,4 s" && sl::durationText(0.08) == "0,08 s"
              && sl::durationText(2.0) == "2,0 s" && sl::durationText(12.34) == "12,3 s",
          "1,52 s ; 1,4 s ; 0,08 s ; 2,0 s ; 12,3 s");
}

} // namespace

int main() {
    std::printf("[1.24.0 : la bibliotheque de sons]\n");
    natives();
    search();
    mine();
    import();
    texts();
    std::printf("sound_library_test : %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
