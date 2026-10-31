// tests/ai_apprentissage_test.cpp - 1.17.0 : ce que l'IA integree sait, corrigeable a la main (l'onglet Apprentissage).
//
//   ai_apprentissage_test
//
//  Sur un dossier IA/ jetable :
//  1. les modeles metier : ceux du logiciel listes ; corriger T_Pompe -> l'IA integree propose les nouveaux membres ;
//     revenir a l'original ; desactiver un modele (elle ne le propose plus) ; en ajouter un (compresseur) ; un type invalide
//     refuse avec la raison ;
//  2. le vocabulaire : << bache >> = une cuve -> << Ajoute une bache >> pose une Cuve ; un mot = un modele ; un mot a
//     ignorer ; oublier un mot ;
//  3. tout est dans connaissances/ (le contrat d'AiKnowledge) et part dans le .xia.
#include "../src/app/ai/AiKnowledge.hpp"
#include "../src/app/ai/AiLearning.hpp"
#include "../src/app/ai/AiLearningFile.hpp"
#include "../src/app/ai/AiLocal.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiStore.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace ln = app::ai::learning;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path() / ("apprentissage_essai_" + std::to_string(static_cast<long long>(std::rand())));
    return p;
}

struct Bench {
    hmi::DocumentPtr doc = std::make_shared<hmi::Document>();
    hmi::Id          view{hmi::kNoId};
    Bench() {
        auto& p = doc->project;
        hmi::View v = hmi::makeView(p, "Vue_Test");
        v.width = 1600;
        v.height = 900;
        p.views = {v};
        view = p.views.front().id;
    }
    ai::LocalReply ask(const std::string& text) {
        ai::PromptInput in;
        in.project = &doc->project;
        in.view = doc->project.view(view);
        return ai::understand(in, text);
    }
};

const ai::Operation* typeOp(const ai::LocalReply& r) {
    if (!r.proposal) return nullptr;
    for (const auto& op : r.proposal->ops)
        if (op.action == ai::Operation::Action::Type) return &op;
    return nullptr;
}
bool hasMember(const ai::Operation* op, const std::string& name) {
    return op && std::any_of(op->members.begin(), op->members.end(), [&](const auto& m) { return m.name == name; });
}

void testModels() {
    std::printf("[les modeles metier]\n");
    Bench b;
    const auto all = ln::models();
    const auto pompe = ln::model("pompe");
    check(all.size() >= 8 && pompe && pompe->builtIn && pompe->name == "T_Pompe" && !pompe->corrected, "les modeles du logiciel (T_Pompe...)");
    // Avant : le modele du logiciel.
    auto r = b.ask("Cr\xC3\xA9" "e un type pour une pompe");
    check(typeOp(r) && typeOp(r)->name == "T_Pompe" && hasMember(typeOp(r), "Marche") && !hasMember(typeOp(r), "Debit"), "avant : T_Pompe du logiciel");
    // Corriger : un debit, plus d'heures de marche.
    ln::BusinessModel m = *pompe;
    m.members.erase(std::remove_if(m.members.begin(), m.members.end(), [](const auto& x) { return x.name == "HeuresMarche"; }), m.members.end());
    m.members.push_back({"Debit", "REAL", "D\xC3\xA9" "bit (m3/h)"});
    std::string why;
    check(ln::saveModel(m, &why), "corriger T_Pompe (" + why + ")");
    const auto corrected = ln::model("pompe");
    check(corrected && corrected->corrected && corrected->builtIn, "T_Pompe : corrig\xC3\xA9");
    r = b.ask("Cr\xC3\xA9" "e un type pour une pompe");
    check(hasMember(typeOp(r), "Debit") && !hasMember(typeOp(r), "HeuresMarche"), "apr\xC3\xA8s : l'IA int\xC3\xA9gr\xC3\xA9" "e propose les nouveaux membres");
    // Revenir a l'original.
    check(ln::revertModel("pompe", &why) && !ln::model("pompe")->corrected, "revenir \xC3\xA0 l'original");
    r = b.ask("Cr\xC3\xA9" "e un type pour une pompe");
    check(hasMember(typeOp(r), "HeuresMarche") && !hasMember(typeOp(r), "Debit"), "de nouveau le modele du logiciel");
    // Desactiver la vanne : elle ne la propose plus d'elle-meme.
    check(ln::setModelActive("vanne", false, &why) && !ln::model("vanne"), "d\xC3\xA9sactiver T_Vanne (" + why + ")");
    r = b.ask("Cr\xC3\xA9" "e un type pour une vanne");
    check(!typeOp(r) || typeOp(r)->name != "T_Vanne", "d\xC3\xA9sactiv\xC3\xA9 : plus de T_Vanne propos\xC3\xA9");
    check(ln::setModelActive("vanne", true, &why) && ln::model("vanne"), "r\xC3\xA9" "activer T_Vanne");
    // Ajouter un modele : le compresseur.
    ln::BusinessModel c;
    c.key = "compresseur";
    c.name = "T_Compresseur";
    c.label = "compresseur";
    c.description = "Compresseur d'air";
    c.words = {"compresseur", "surpresseur"};
    c.members = {{"Marche", "BOOL", "En marche"}, {"Pression", "REAL", "Pression de refoulement (bar)"}, {"Defaut", "BOOL", "D\xC3\xA9" "faut"}};
    check(ln::saveModel(c, &why), "ajouter T_Compresseur (" + why + ")");
    r = b.ask("Cr\xC3\xA9" "e un type pour un surpresseur");
    check(typeOp(r) && typeOp(r)->name == "T_Compresseur" && hasMember(typeOp(r), "Pression"), "un mot du nouveau mod\xC3\xA8le : T_Compresseur propos\xC3\xA9");
    // Refus.
    ln::BusinessModel bad = c;
    bad.key = "four";
    bad.name = "T_Four";
    bad.members = {{"Temperature", "REEL_FAUX[", ""}};
    check(!ln::saveModel(bad, &why) && !why.empty(), "un type invalide : refus\xC3\xA9 (" + why + ")");
    bad.members = {{"2Temp", "REAL", ""}};
    check(!ln::saveModel(bad, &why), "un nom de membre invalide : refus\xC3\xA9 (" + why + ")");
    check(!ln::removeModel("pompe", &why), "un mod\xC3\xA8le du logiciel ne se supprime pas");
    check(ln::removeModel("compresseur", &why) && !ln::model("compresseur"), "supprimer T_Compresseur");
}

void testWords() {
    std::printf("[le vocabulaire]\n");
    Bench b;
    std::string why;
    auto r = b.ask("Ajoute une b\xC3\xA2" "che");
    const bool before = r.proposal && std::any_of(r.proposal->ops.begin(), r.proposal->ops.end(), [](const auto& op) { return op.kind == hmi::kindKey(hmi::Kind::Tank); });
    check(!before, "avant : << bache >> n'est pas une cuve");
    ln::WordEntry w;
    w.word = "b\xC3\xA2" "che";
    w.means = ln::Meaning::Kind;
    w.target = std::string(hmi::kindKey(hmi::Kind::Tank));
    check(ln::learnWord(w, &why), "apprendre << bache >> = une cuve (" + why + ")");
    r = b.ask("Ajoute une b\xC3\xA2" "che");
    const bool after = r.proposal && std::any_of(r.proposal->ops.begin(), r.proposal->ops.end(), [](const auto& op) { return op.kind == hmi::kindKey(hmi::Kind::Tank); });
    check(after, "apr\xC3\xA8s : << Ajoute une bache >> pose une Cuve");
    // un mot = un modele
    ln::WordEntry mw;
    mw.word = "groupe motopompe";
    mw.means = ln::Meaning::Model;
    mw.target = "pompe";
    check(ln::learnWord(mw, &why), "un mot = un mod\xC3\xA8le (" + why + ")");
    ln::WordEntry one;
    one.word = "motopompe";
    one.means = ln::Meaning::Model;
    one.target = "pompe";
    check(ln::learnWord(one, &why), "<< motopompe >> = T_Pompe");
    r = b.ask("Cr\xC3\xA9" "e un type pour une motopompe");
    check(typeOp(r) && typeOp(r)->name == "T_Pompe", "<< type pour une motopompe >> : T_Pompe");
    // un mot a ignorer
    ln::WordEntry ig;
    ig.word = "pompe";
    ig.means = ln::Meaning::Ignore;
    check(ln::learnWord(ig, &why), "un mot \xC3\xA0 ignorer (" + why + ")");
    check(ln::modelForWord("pompe").empty(), "ignor\xC3\xA9 : << pompe >> n'appelle plus de mod\xC3\xA8le");
    check(ln::forgetWord("pompe", &why) && ln::modelForWord("pompe") == "pompe", "oubli\xC3\xA9 : de nouveau T_Pompe");
    check(!ln::learnWord(ln::WordEntry{"truc", ln::Meaning::Kind, "PasUnGenre"}, &why), "un genre inconnu : refus\xC3\xA9 (" + why + ")");
    check(ln::setWordActive("b\xC3\xA2" "che", false, &why), "d\xC3\xA9sactiver << bache >>");
    r = b.ask("Ajoute une b\xC3\xA2" "che");
    check(!(r.proposal && std::any_of(r.proposal->ops.begin(), r.proposal->ops.end(), [](const auto& op) { return op.kind == hmi::kindKey(hmi::Kind::Tank); })),
          "d\xC3\xA9sactiv\xC3\xA9 : plus compris");
    check(ln::setWordActive("b\xC3\xA2" "che", true, &why), "r\xC3\xA9" "activ\xC3\xA9");
    // les mots du logiciel se lisent
    const auto all = ln::words(true);
    check(std::any_of(all.begin(), all.end(), [](const auto& e) { return e.builtIn && e.word == "bac"; }), "les mots du logiciel (bac) se lisent");
}

void testStorage() {
    std::printf("[le stockage et l'export]\n");
    {   // un modele de l'utilisateur, a emporter
        ln::BusinessModel c;
        c.key = "doseur";
        c.name = "T_Doseur";
        c.members = {{"Consigne", "REAL", "Dose voulue"}, {"Dose", "REAL", "Dose faite"}};
        std::string w;
        check(ln::saveModel(c, &w), "T_Doseur (" + w + ")");
    }
    app::ai::memory::Registry reg;
    reg.load();
    int models = 0, words = 0;
    for (const auto& k : reg.all()) {
        if (k.builtIn) continue;
        if (k.category == "modeles") ++models;
        if (k.category == "vocabulaire") ++words;
    }
    check(models >= 1 && words >= 3, "des connaissances comme les autres (mod\xC3\xA8les " + std::to_string(models) + ", mots " + std::to_string(words) + ")");
    const std::string file = (scratch() / "appris.xia").string();
    std::string why;
    check(ln::exportTo(file, {}, &why), "export\xC3\xA9 (" + why + ")");
    const fs::path other = scratch() / "autrePC";
    fs::create_directories(other);
    ai::setRoot(other.string());
    ln::ImportReport r;
    check(ln::importFrom(file, r, false, &why) && r.added >= 4, "import\xC3\xA9 sur un autre PC (" + std::to_string(r.added) + ")");
    Bench b;
    const auto rep = b.ask("Ajoute une b\xC3\xA2" "che");
    check(rep.proposal && std::any_of(rep.proposal->ops.begin(), rep.proposal->ops.end(), [](const auto& op) { return op.kind == hmi::kindKey(hmi::Kind::Tank); }),
          "l'autre PC comprend << bache >>");
}

void testMeasures() {
    std::printf("[les mesures]\n");
    const fs::path r = scratch() / "mesures";
    fs::create_directories(r);
    ai::setRoot(r.string());
    // Le mercredi 28/10/2026 a midi (heure du PC) ; la semaine d'avant, le mercredi 21.
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 28;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    const std::time_t thisWeek = std::mktime(&tm);
    tm.tm_mday = 21;
    tm.tm_isdst = -1;
    const std::time_t lastWeek = std::mktime(&tm);
    // Aucune mesure : des semaines vides, pas de chiffre invente.
    auto w = ln::weeks(8, static_cast<long long>(thisWeek), ln::readMeasures(), {});
    check(w.size() == 8 && std::all_of(w.begin(), w.end(), [](const ln::Week& x) { return x.asked == 0 && x.decided == 0; }), "aucune mesure : 8 semaines vides");
    ai::setClock([&] { return lastWeek; });
    ln::recordReply(true, "Ajoute une pompe", true);
    ln::recordReply(false, "Quelle est la capitale de l'Australie ?", false);
    ai::setClock([&] { return thisWeek; });
    ln::recordReply(true, "Ajoute 3 pompes", true);
    ln::recordReply(true, "Combien d'objets ?", false);
    ln::recordReply(true, "Mets P1 en rouge", true);
    ln::recordReply(false, "Fais un truc joli", false);
    const auto m = ln::readMeasures();
    check(m.size() == 6, "6 mesures relues");
    std::vector<ln::Decision> d = {{ai::isoDate(lastWeek), true}, {ai::isoDate(thisWeek), true}, {ai::isoDate(thisWeek), false}};
    w = ln::weeks(8, static_cast<long long>(thisWeek), m, d);
    check(w.back().asked == 4 && w.back().understood == 3 && w[6].asked == 2 && w[6].understood == 1,
          "les taux par semaine : 3/4 cette semaine, 1/2 la pr\xC3\xA9" "c\xC3\xA9" "dente");
    check(w.back().decided == 2 && w.back().kept == 1 && w[6].decided == 1 && w[6].kept == 1, "les propositions gard\xC3\xA9" "es : 1/2, 1/1");
    // A lui apprendre : les non comprises de la semaine, sans doublon ; Ignorer la retire.
    ln::recordReply(false, "Fais un truc joli", false);
    auto todo = ln::notUnderstood(7, static_cast<long long>(thisWeek));
    check(todo.size() == 1 && todo.front().request == "Fais un truc joli", "\xC3\x80 lui apprendre : 1 demande (la capitale a plus de 7 jours)");
    ln::dismissRequest("Fais un truc joli");
    check(ln::notUnderstood(7, static_cast<long long>(thisWeek)).empty(), "Ignorer : la demande quitte la liste");
    // Une demande longue : 200 caracteres au plus.
    ln::recordReply(false, std::string(500, 'x'), false);
    check(ln::readMeasures().back().request.size() == 200, "la demande gard\xC3\xA9" "e : 200 caract\xC3\xA8res au plus");
    // Les elements appris par origine.
    ln::BusinessModel c;
    c.key = "agitateur";
    c.name = "T_Agitateur";
    c.members = {{"Marche", "BOOL", ""}};
    std::string why;
    check(ln::saveModel(c, &why), "un mod\xC3\xA8le appris cette semaine");
    ln::WordEntry word;
    word.word = "malaxeur";
    word.means = ln::Meaning::Model;
    word.target = "agitateur";
    check(ln::learnWord(word, &why, "assistant"), "un mot appris par l'Assistant");
    w = ln::weeks(8, static_cast<long long>(thisWeek), ln::readMeasures(), {});
    check(w.back().byUser == 1 && w.back().byAssistant == 1, "appris cette semaine : 1 par toi, 1 par l'Assistant");
    ai::setClock(nullptr);
}

} // namespace

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    const fs::path r = scratch() / "IA";
    fs::create_directories(r);
    ai::setRoot(r.string());
    testModels();
    testWords();
    testStorage();
    testMeasures();
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    std::printf("ai_apprentissage_test : %d verifications, %d echecs\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
