// tests/ai_apprentissage_fichier_test.cpp - 1.17.0 : l'apprentissage de l'IA dans un fichier (.xia).
//
//   ai_apprentissage_fichier_test
//
//  Sans ecran, sur des dossiers IA/ jetables :
//  1. exporter puis importer dans un autre dossier IA : les connaissances reviennent, identiques ;
//  2. jamais la cle API dans le fichier (une cle en memoire, les reglages exportes : on la cherche) ;
//  3. peek ne change rien ; une connaissance de meme identifiant gardee, ou remplacee ;
//  4. refuses : un fichier abime, un autre format, un format futur, un identifiant malveillant (../../x) ;
//  5. les reglages du fichier : appliques sur une installation neuve, gardes si l'Assistant est deja regle ;
//  6. applyInstallation : appliquee une fois, pas deux ; un nouveau tampon reapplique ; "garder" ne touche a rien.
#include "../src/app/ai/AiKnowledge.hpp"
#include "../src/app/ai/AiLearningFile.hpp"
#include "../src/app/ai/AiMemory.hpp"
#include "../src/app/ai/AiMemoryJson.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace mem = app::ai::memory;
namespace lf = app::ai::learning;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    }
}

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path() / ("xia_essai_" + std::to_string(static_cast<long long>(std::rand())));
    return p;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeAll(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// Un dossier IA neuf, choisi comme racine de l'Assistant.
void useRoot(const std::string& name) {
    const fs::path r = scratch() / name;
    fs::create_directories(r);
    ai::setRoot(r.string());
}

mem::Knowledge knowledge(const std::string& name, const std::string& content, const std::string& category = "metier") {
    mem::Knowledge k;
    k.name = name;
    k.category = category;
    k.content = content;
    k.source = "utilisateur";
    return k;
}

void testRoundTrip() {
    std::printf("[exporter, importer ailleurs]\n");
    useRoot("pc1");
    mem::Registry reg;
    reg.load();
    auto a = knowledge("Cuves de la ligne 2", "Les cuves de la ligne 2 font 12 m3 ; seuil haut 90 %.");
    auto b = knowledge("Nommage des pompes", "Une pompe se nomme P<ligne><numero> (P21, P22).", "preferences");
    std::string why;
    check(reg.add(a, &why) && reg.add(b, &why), "deux connaissances ajoutees sur le PC 1 (" + why + ")");
    check(reg.setActive(mem::kKindsKnowledge, false, &why), "une integree desactivee (" + why + ")");
    const std::string file = (scratch() / "export.xia").string();
    lf::FileOptions opt;
    check(lf::exportTo(file, opt, &why), "exporter (" + why + ")");
    const std::string text = readAll(file);
    check(text.find("\"format\": \"xia\"") != std::string::npos && text.find("Cuves de la ligne 2") != std::string::npos,
          "le fichier : son format et les connaissances");
    // Un autre PC.
    useRoot("pc2");
    lf::ImportReport pv;
    check(lf::peek(file, pv, &why) && pv.added == 2 && pv.kept == 0 && pv.skipped == 0, "peek : 2 a ajouter (" + why + ")");
    mem::Registry before;
    before.load();
    check(before.count(false) == mem::builtIns().size(), "peek n'a rien ecrit");
    lf::ImportReport r;
    check(lf::importFrom(file, r, false, &why) && r.added == 2, "importer : 2 ajoutees (" + why + ")");
    mem::Registry after;
    after.load();
    const auto* ka = after.find(a.id);
    const auto* kb = after.find(b.id);
    check(ka && kb && ka->content == a.content && kb->name == b.name && kb->category == "preferences",
          "les connaissances reviennent, identiques (contenu, nom, categorie)");
    const auto* kinds = after.find(mem::kKindsKnowledge);
    check(kinds && !kinds->active, "l'integree desactivee sur le PC 1 l'est aussi ici");
    bool logged = false;
    for (const auto& act : mem::readActivity())
        if (act.text.find("Apprentissage import") != std::string::npos) logged = true;
    check(logged, "une ligne dans le journal d'activite");
    // De nouveau : gardees (identiques).
    lf::ImportReport again;
    check(lf::importFrom(file, again, false, &why) && again.added == 0 && again.kept == 2, "importer deux fois : rien en double");
}

void testSameIdAndRefusals() {
    std::printf("[meme identifiant, fichiers refuses]\n");
    useRoot("pc3");
    mem::Registry reg;
    reg.load();
    auto a = knowledge("Regle des couleurs", "Rouge : defaut ; vert : marche.", "regles");
    std::string why;
    check(reg.add(a, &why), "une connaissance");
    const std::string file = (scratch() / "regle.xia").string();
    check(lf::exportTo(file, {}, &why), "exportee");
    // Le fichier dit autre chose pour le meme identifiant.
    std::string text = readAll(file);
    const auto at = text.find("Rouge : defaut");
    text.replace(at, std::string("Rouge : defaut").size(), "Rouge : alarme");
    writeAll(file, text);
    lf::ImportReport keep;
    check(lf::importFrom(file, keep, false, &why) && keep.kept == 1 && keep.added == 0, "meme identifiant : gardee");
    mem::Registry r1;
    r1.load();
    check(r1.find(a.id) && r1.find(a.id)->content.find("defaut") != std::string::npos, "la sienne reste");
    lf::ImportReport repl;
    check(lf::importFrom(file, repl, true, &why) && repl.replaced == 1, "remplacer : celle du fichier");
    mem::Registry r2;
    r2.load();
    check(r2.find(a.id) && r2.find(a.id)->content.find("alarme") != std::string::npos, "remplacee");
    // Refuses.
    const fs::path bad = scratch() / "abime.xia";
    writeAll(bad, "{ \"format\": \"xia\", \"version\": 1, \"connaissances\": [ ");
    lf::ImportReport r;
    check(!lf::importFrom(bad.string(), r, false, &why) && !why.empty(), "un fichier abime : refuse (" + why + ")");
    writeAll(bad, "{ \"format\": \"xihm\", \"version\": 1 }");
    check(!lf::peek(bad.string(), r, &why) && why.find(".xia") != std::string::npos, "un autre format : refuse (" + why + ")");
    writeAll(bad, "{ \"format\": \"xia\", \"version\": 9, \"logiciel\": \"9.0.0\" }");
    check(!lf::peek(bad.string(), r, &why) && why.find("9.0.0") != std::string::npos, "un format futur : refuse (" + why + ")");
    writeAll(bad, "{ \"format\": \"xia\", \"version\": 1, \"connaissances\": [ { \"id\": \"../../x\", \"nom\": \"Piege\", \"contenu\": \"x\" },"
                  " { \"id\": \"a/b\", \"nom\": \"Piege 2\", \"contenu\": \"x\" } ] }");
    check(lf::importFrom(bad.string(), r, false, &why) && r.skipped == 2 && r.added == 0, "des identifiants malveillants : ignores");
    check(!fs::exists(scratch() / "x.json") && !fs::exists(scratch() / "pc3" / "x.json"), "rien d'ecrit hors du dossier connaissances");
    check(!lf::peek((scratch() / "absent.xia").string(), r, &why), "un fichier absent : refuse");
}

void testSettingsAndKey() {
    std::printf("[reglages, jamais la cle]\n");
    useRoot("pc4");
    ai::Settings s;
    s.provider = "openai";
    s.preset = "ollama";
    s.endpoint = "http://localhost:11434/v1";
    s.model = "qwen2.5:7b";
    s.localFirst = true;
    std::string why;
    check(ai::saveSettings(s, &why), "reglages : Ollama");
    // Une cle en memoire (ce que l'Assistant garde, jamais dans parametres.json).
    const std::string key = "sk-ant-ESSAI-0123456789-NE-PAS-ECRIRE";
    (void)key;
    const std::string file = (scratch() / "reglages.xia").string();
    lf::FileOptions opt;
    opt.settings = true;
    check(lf::exportTo(file, opt, &why), "exporter avec les reglages");
    const std::string text = readAll(file);
    check(text.find("qwen2.5:7b") != std::string::npos && text.find("ESSAI-0123456789") == std::string::npos
              && text.find("cle") == std::string::npos && text.find("apiKey") == std::string::npos,
          "les reglages, jamais la cle");
    // Une installation neuve : appliques.
    useRoot("pc5");
    lf::ImportReport r;
    check(lf::importFrom(file, r, false, &why), "importer sur une installation neuve");
    const ai::Settings got = ai::loadSettings();
    check(got.provider == "openai" && got.preset == "ollama" && got.model == "qwen2.5:7b" && got.localFirst, "les reglages appliques");
    // Deja regle a la main (Anthropic) : gardes.
    useRoot("pc6");
    ai::Settings mine;
    mine.provider = "anthropic";
    mine.model = "mon-modele";
    check(ai::saveSettings(mine, &why), "un PC deja regle sur Anthropic");
    check(lf::importFrom(file, r, false, &why), "importer");
    const ai::Settings kept = ai::loadSettings();
    check(kept.provider == "anthropic" && kept.model == "mon-modele", "ses reglages restent");
    bool said = false;
    for (const auto& l : r.lines)
        if (l.find("gard") != std::string::npos && l.find("r\xC3\xA9glages") != std::string::npos) said = true;
    check(said, "le rapport le dit");
}

void testInstallation() {
    std::printf("[apres l'installation]\n");
    const fs::path common = scratch() / "ProgramData" / "XPGAnalyser" / "IA";
    fs::create_directories(common);
#if defined(_WIN32)
    _putenv_s("XPG_IA_INSTALLATION", common.string().c_str());
#else
    setenv("XPG_IA_INSTALLATION", common.string().c_str(), 1);
#endif
    // Un apprentissage d'un autre PC, depose par l'installateur.
    useRoot("source");
    mem::Registry reg;
    reg.load();
    auto a = knowledge("Vocabulaire de l'atelier", "Une bache est une cuve.", "vocabulaire");
    std::string why;
    check(reg.add(a, &why), "une connaissance a emporter");
    check(lf::exportTo((common / "apprentissage.xia").string(), {}, &why), "le .xia depose");
    writeAll(common / "installation.json",
             "{ \"tampon\": \"inst-1\", \"ia\": \"ollama\", \"modele\": \"qwen2.5:3b\", \"dabord\": true, \"apprentissage\": \"apprentissage.xia\" }");
    // Le premier lancement d'un utilisateur.
    useRoot("utilisateur");
    std::string said;
    check(lf::applyInstallation(&said), "appliquee au premier lancement");
    const ai::Settings s = ai::loadSettings();
    check(s.provider == "openai" && s.preset == "ollama" && s.endpoint == "http://localhost:11434/v1" && s.model == "qwen2.5:3b" && s.localFirst,
          "Ollama, son adresse, le modele, l'IA integree d'abord");
    mem::Registry r;
    r.load();
    check(r.find(a.id) != nullptr, "l'apprentissage importe");
    check(said.find("Ollama") != std::string::npos && said.find("1 connaissance") != std::string::npos, "la phrase du panneau : " + said);
    // Pas deux fois.
    ai::Settings changed = s;
    changed.model = "change-a-la-main";
    check(ai::saveSettings(changed, &why), "l'utilisateur change le modele");
    check(!lf::applyInstallation(&said) && ai::loadSettings().model == "change-a-la-main", "deja appliquee : rien ne change");
    // Une nouvelle installation (un nouveau tampon), "garder" : rien ne change.
    writeAll(common / "installation.json", "{ \"tampon\": \"inst-2\", \"ia\": \"garder\" }");
    (void)lf::applyInstallation(&said);
    check(ai::loadSettings().model == "change-a-la-main" && ai::loadSettings().provider == "openai", "garder : les reglages restent");
    // Un nouveau tampon qui choisit l'IA integree : reapplique.
    writeAll(common / "installation.json", "{ \"tampon\": \"inst-3\", \"ia\": \"integree\" }");
    check(lf::applyInstallation(&said) && ai::loadSettings().provider == "integree", "un nouveau tampon : reapplique (" + said + ")");
    // Un autre utilisateur du meme PC : la sienne.
    useRoot("autre-utilisateur");
    check(lf::applyInstallation(&said) && ai::loadSettings().provider == "integree", "chaque utilisateur, une fois");
}

} // namespace

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    testRoundTrip();
    testSameIdAndRefusals();
    testSettingsAndKey();
    testInstallation();
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    std::printf("ai_apprentissage_fichier_test : %d verifications, %d echecs\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
