// tests/ai_memoire_test.cpp - 1.14.2 (Assistant IA, chantier IA2) : "Memoire et apprentissage de l'IA".
//
//   ai_memoire_test
//
//  Sans ecran, sur un dossier IA/ jetable (le contrat : app/ai/AiPaths.hpp). La section 12.10 de la demande,
//  toute :
//  1. le JSON des fichiers de l'IA : lu, ecrit, un champ inconnu garde, \uXXXX et paires de substitution ;
//  2. l'ajout, la modification, la suppression d'une connaissance ; la sauvegarde et la relecture (un champ
//     inconnu du fichier reste) ; les integrees : 116 genres tires des vraies tables, activables, jamais supprimees ;
//  3. les connaissances par projet : une connaissance d'un autre projet n'est pas active ici ; oublier un projet ;
//  4. le calcul REEL de l'espace : des fichiers de tailles connues dans chaque categorie, les sommes exactes ;
//  5. le nettoyage du cache : les categories cochees seules ; les conversations et les pieces jointes
//     originales intactes (octet pour octet) ; la date du dernier nettoyage gardee ;
//  6. les donnees persistantes : supprimees seulement apres confirmation ;
//  7. la memoire vive lue (non nulle) ; l'activite ecrite et relue ; la recherche ;
//  8. la fenetre : les jauges et les comptes qui suivent chaque operation, les confirmations, et le dessin des
//     six onglets (aucun texte hors de la fenetre) en 1920 x 1200 et 1280 x 800.
#include "../src/app/ai/AiKnowledge.hpp"
#include "../src/app/ai/AiMemory.hpp"
#include "../src/app/ai/AiMemoryDialog.hpp"
#include "../src/app/ai/AiMemoryJson.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/hmi/HmiPanels.hpp"   // hmiPaletteWords : les mots-cles de la bibliotheque
#include "../src/hmi/HmiModel.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mem = app::ai::memory;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    static const bool all = [] {
        const char* e = std::getenv("AI_MEMOIRE_TOUT");
        return e && *e == '1';
    }();
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    } else if (all) {
        std::printf("  ok : %s\n", what.c_str());
    }
}

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path()
                              / ("xpg_ai_memoire_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007));
    return p;
}

void writeBytes(const fs::path& p, std::size_t n, char fill = 'x') {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    const std::string s(n, fill);
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void writeText(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uintmax_t sizeOf(const fs::path& p) {
    std::error_code ec;
    return fs::file_size(p, ec);
}

// Une conversation au format du contrat (CONTEXTE-IA.md), avec un champ que le centre ne connait pas.
std::string conversationJson(const std::string& id, const std::string& title, const std::string& project, const std::string& pieceFile) {
    return "{\"format\": 1, \"id\": \"" + id + "\", \"titre\": \"" + title + "\", \"cree\": \"2026-10-30T17:35:12\",\n"
           " \"modifie\": \"2026-10-30T17:41:03\", \"demo\": true, \"inconnu\": {\"garde\": [1, 2]},\n"
           " \"projet\": {\"nom\": \"" + project + "\", \"chemin\": \"C:/Projets/" + project + ".xihm\", \"vue\": \"Vue_Station\"},\n"
           " \"messages\": [\n"
           "   {\"id\": \"m1\", \"role\": \"utilisateur\", \"date\": \"2026-10-30T17:35:12\", \"texte\": \"Cr\\u00e9" "e-moi un synoptique de station de pompage avec trois cuves\",\n"
           "    \"pieces\": [{\"id\": \"p1\", \"nom\": \"plan.pdf\", \"type\": \"pdf\", \"octets\": 1234, \"fichier\": \"" + pieceFile + "\"}]},\n"
           "   {\"id\": \"m2\", \"role\": \"assistant\", \"date\": \"2026-10-30T17:35:20\", \"texte\": \"Voici **9 objets** : trois cuves Tank, six pompes Pump.\",\n"
           "    \"etat\": \"fini\", \"proposition\": {\"id\": \"op1\", \"etat\": \"proposee\", \"creer\": 9, \"modifier\": 1, \"supprimer\": 0}}\n"
           " ]}\n";
}

// ------------------------------------------------------------------------------- 1. le JSON ---
void json() {
    std::printf("== 1. le JSON des fichiers de l'IA ==\n");
    mem::Json j;
    std::string why;
    check(mem::parseJson("{\"a\": 1, \"b\": [true, null, \"x\\u00e9\\ud83d\\ude00\"], \"c\": {\"d\": -2.5e1}}", j, &why), "lu : " + why);
    check(j.num("a") == 1.0 && j.get("b") && j.get("b")->items.size() == 3 && j.get("b")->items[2].text == "x\xC3\xA9\xF0\x9F\x98\x80",
          "les valeurs, \\u00e9 et la paire de substitution en UTF-8");
    check(j.get("c") && j.get("c")->num("d") == -25.0, "un nombre a exposant");
    const std::string back = mem::writeJson(j, false);
    check(back == "{\"a\": 1, \"b\": [true, null, \"x\xC3\xA9\xF0\x9F\x98\x80\"], \"c\": {\"d\": -2.5e1}}", "reecrit tel quel (l'ordre, le texte des nombres) : " + back);
    check(!mem::parseJson("{\"a\": }", j, &why) && why.find("position") != std::string::npos, "mal forme : la position dite (" + why + ")");
    check(!mem::parseJson("{\"a\": 1} x", j), "du texte apres la valeur : refuse");
    std::string deep(100, '[');
    check(!mem::parseJson(deep, j, &why), "trop imbrique : refuse");
    mem::Json o = mem::Json::makeObject();
    o.set("texte", mem::Json::makeString("ligne 1\nligne \"2\"\t\x01"));
    check(mem::writeJson(o, false) == "{\"texte\": \"ligne 1\\nligne \\\"2\\\"\\t\\u0001\"}", "les echappements a l'ecriture");
}

// ------------------------------------------------------------ 2. les connaissances (le registre) ---
void knowledge() {
    std::printf("== 2. les connaissances : ajouter, modifier, supprimer, relire ==\n");
    // les integrees : tirees des vraies tables
    const auto& built = mem::builtIns();
    check(built.size() == 6, "six connaissances integrees (" + std::to_string(built.size()) + ")");
    const mem::Knowledge* kinds = nullptr;
    for (const auto& k : built)
        if (k.id == mem::kKindsKnowledge) kinds = &k;
    check(kinds != nullptr, "la connaissance des genres d'objets");
    std::size_t lines = 0, missing = 0;
    if (kinds) {
        for (const char ch : kinds->content) lines += ch == '\n';
        for (const auto& info : hmi::kKindInfos)
            if (kinds->content.find(std::string(info.key) + " | " + std::string(info.label)) == std::string::npos) ++missing;
    }
    check(lines == 117 && missing == 0, "116 genres, chacun avec sa cle et son libelle de hmi::kKindInfos (" + std::to_string(lines) + " lignes, "
                                            + std::to_string(missing) + " absents)");
    check(mem::Registry::kindsTotal() == 116, "116 genres en tout (" + std::to_string(mem::Registry::kindsTotal()) + ")");
    bool allSized = true, allSoftware = true;
    for (const auto& k : built) {
        allSized = allSized && k.size() > 200 && k.size() == k.content.size();
        allSoftware = allSoftware && k.builtIn && k.source == "logiciel" && mem::knownCategory(k.category);
    }
    check(allSized && allSoftware, "chacune : source logiciel, une categorie connue, sa taille = celle de son texte");
    for (const auto& k : built)
        if (k.id == "logiciel.proprietes") {
            const hmi::Object pump = hmi::makeObject(hmi::Kind::Pump, 1, "P", 0, 0, 0);
            bool all = !pump.props.empty();
            for (const auto& p : pump.props) all = all && k.content.find(p.key) != std::string::npos;
            check(all && k.content.find("Pump :") != std::string::npos, "les proprietes de la pompe : celles de sa creation par defaut");
        } else if (k.id == "logiciel.vocabulaire") {
            // une ligne par genre qui a des mots-cles dans la bibliotheque (app::hmiPaletteWords), ceux-la
            std::size_t withWords = 0, vocLines = 0;
            bool allWords = true;
            for (const auto& info : hmi::kKindInfos) {
                const auto words = app::hmiPaletteWords(info.kind);
                if (words.empty()) continue;
                ++withWords;
                const auto at = k.content.find(std::string(info.label) + " (" + std::string(info.key) + ") :");
                allWords = allWords && at != std::string::npos && k.content.find(std::string(words.front()), at) != std::string::npos;
            }
            for (const char ch : k.content) vocLines += ch == '\n';
            check(withWords > 0 && vocLines == withWords + 1 && allWords && k.content.find("fin de course") != std::string::npos,
                  "le vocabulaire : les mots-cles de recherche de la bibliotheque (" + std::to_string(withWords) + " genres)");
        } else if (k.id == "logiciel.variables") {
            check(k.content.find("BOOL") != std::string::npos && k.content.find("->") != std::string::npos, "l'objet pose pour une variable");
        } else if (k.id == "logiciel.modeles") {
            check(k.content.find("synoptique") != std::string::npos || k.content.find("Synoptique") != std::string::npos, "les modeles de vues");
        }

    mem::Registry reg;
    reg.load();
    check(reg.count(false) == 6 && reg.kindsKnown() == 116, "un dossier vide : les six integrees, 116/116 genres connus");
    // ajouter
    mem::Knowledge k;
    k.name = "Pompes doublees";
    k.description = "Deux pompes par cuve, l'une en secours";
    k.category = "regles";
    k.content = "Chaque cuve a deux pompes : P1 marche, P2 secours.";
    std::string why;
    check(reg.add(k, &why), "ajout : " + why);
    check(k.id.rfind("k-", 0) == 0 && !k.added.empty() && k.added == k.modified && k.source == "utilisateur", "id, dates, source attribues (" + k.id + ")");
    const fs::path file = fs::path(app::ai::knowledgeDir()) / (k.id + ".json");
    check(fs::exists(file), "son fichier : connaissances/" + k.id + ".json");
    mem::Json j;
    check(mem::parseJson(readAll(file), j) && j.num("format") == 1 && j.str("nom") == k.name && j.str("categorie") == "regles" && j.flag("actif")
              && j.str("projet").empty() && j.str("contenu") == k.content,
          "le fichier au format du contrat");
    // un champ inconnu ajoute a la main reste a la reecriture
    j.set("etiquette", mem::Json::makeString("venue d'ailleurs"));
    writeText(file, mem::writeJson(j, true));
    reg.load();
    check(reg.count(false) == 7 && reg.find(k.id) && reg.find(k.id)->name == k.name, "relu : 7 connaissances, la nouvelle avec son nom");
    // modifier
    mem::Knowledge changed = *reg.find(k.id);
    changed.content = "Chaque cuve a deux pompes (P1, P2) et un niveau LT.";
    changed.project = "Station_Pompage";
    check(reg.update(changed, &why), "modification : " + why);
    reg.load();
    const auto* after = reg.find(k.id);
    check(after && after->content == changed.content && after->project == "Station_Pompage" && after->added == k.added, "relue : le contenu, le projet, la date d'ajout");
    check(mem::parseJson(readAll(file), j) && j.str("etiquette") == "venue d'ailleurs", "le champ inconnu du fichier est garde");
    // activer / desactiver
    check(reg.setActive(k.id, false) && !reg.find(k.id)->active, "desactiver");
    reg.load();
    check(!reg.find(k.id)->active, "desactivee : relue ainsi");
    check(reg.setActive(k.id, true) && reg.find(k.id)->active, "reactiver");
    // une integree : desactivee, jamais supprimee
    check(!reg.remove(mem::kKindsKnowledge, &why) && why.find("d\xC3\xA9sactive") != std::string::npos, "une integree ne se supprime pas : " + why);
    check(!reg.update(*reg.find(mem::kKindsKnowledge), &why), "une integree ne se modifie pas");
    check(reg.setActive(mem::kKindsKnowledge, false) && reg.kindsKnown() == 0, "les genres desactives : 0/116 connus");
    reg.load();
    check(reg.kindsKnown() == 0 && fs::exists(mem::stateFile()), "relu : toujours desactivee (memoire.json)");
    check(reg.setActive(mem::kKindsKnowledge, true) && reg.kindsKnown() == 116, "reactivee : 116/116");
    // supprimer
    check(reg.remove(k.id, &why) && !fs::exists(file) && reg.count(false) == 6, "suppression : le fichier part, 6 connaissances");
    reg.load();
    check(reg.count(false) == 6, "relu : 6");
    // un fichier abime ne bloque rien
    writeText(fs::path(app::ai::knowledgeDir()) / "abime.json", "{\"nom\": ");
    reg.load();
    check(reg.count(false) == 6 && reg.problems().size() == 1, "un fichier abime : dit, les autres lus");
    fs::remove(fs::path(app::ai::knowledgeDir()) / "abime.json");
}

// ---------------------------------------------------------------------- 3. par projet (12.7) ---
void perProject() {
    std::printf("== 3. les connaissances par projet ==\n");
    mem::Registry reg;
    reg.load();
    mem::Knowledge a, b, c;
    a.name = "Nomenclature station";
    a.project = "Station_Pompage";
    a.content = "Les pompes se nomment P_xx.";
    b.name = "Nomenclature eau";
    b.project = "Traitement_Eau";
    b.content = "Les vannes se nomment V_xx.";
    c.name = "Couleurs maison";
    c.category = "preferences";
    c.content = "Marche en vert, defaut en rouge.";
    check(reg.add(a) && reg.add(b) && reg.add(c), "trois ajouts (deux projets, une commune)");
    const auto* ka = reg.find(a.id);
    const auto* kb = reg.find(b.id);
    const auto* kc = reg.find(c.id);
    check(mem::hereState(*ka, "Station_Pompage") == mem::Here::Active && mem::hereState(*kb, "Station_Pompage") == mem::Here::OtherProject
              && mem::hereState(*kc, "Station_Pompage") == mem::Here::Active,
          "dans Station_Pompage : la sienne et la commune actives, celle de Traitement_Eau non");
    check(mem::hereState(*ka, "") == mem::Here::OtherProject, "sans projet ouvert : une connaissance de projet n'est pas active");
    const auto projects = reg.projects();
    check(projects == std::vector<std::string>{"Station_Pompage", "Traitement_Eau"}, "les projets cites");
    const auto hereBytes = reg.bytesHere("Station_Pompage");
    check(hereBytes == reg.bytes(true) - kb->size(), "ce qui part avec une demande : tout l'actif sauf l'autre projet");
    check(reg.removeProject("Traitement_Eau") == 1 && !reg.find(b.id) && reg.find(a.id), "oublier Traitement_Eau : sa connaissance seule");
    std::string why;
    check(reg.remove(a.id, &why) && reg.remove(c.id, &why), "nettoye");
}

// ------------------------------------------------------------ 4. le calcul reel de l'espace ---
struct Layout {
    fs::path root, conv, piece;
    std::string convBytes, pieceBytes;
};

Layout fillFolder() {
    const fs::path ia = app::ai::root();
    Layout l;
    l.root = ia;
    l.piece = ia / "pieces-jointes" / "p1" / "plan.pdf";
    writeBytes(l.piece, 5000, 'P');
    writeBytes(ia / "pieces-jointes" / "p2" / "photo.png", 3000, 'I');      // une piece que rien ne cite
    l.conv = ia / "conversations" / "c-20261030-173512-1.json";
    writeText(l.conv, conversationJson("c-20261030-173512-1", "Station de pompage", "Station_Pompage", "pieces-jointes/p1/plan.pdf"));
    writeText(ia / "conversations" / "c-20261030-180000-1.json",
              conversationJson("c-20261030-180000-1", "Traitement de l'eau", "Traitement_Eau", "pieces-jointes/p9/absent.pdf"));
    writeBytes(ia / "index" / "mots.idx", 700);
    writeBytes(ia / "journaux" / "assistant.log", 300);
    writeBytes(ia / "parametres.json", 120);
    writeBytes(ia / "cache" / "requetes" / "r1.bin", 1000);
    writeBytes(ia / "cache" / "requetes" / "sous" / "r2.bin", 24);
    writeBytes(ia / "cache" / "traitement" / "t.tmp", 2000);
    writeBytes(ia / "cache" / "images" / "i.png", 4000);
    writeBytes(ia / "cache" / "documents" / "d.txt", 500);
    writeBytes(ia / "cache" / "apercus" / "a.bin", 600);
    writeBytes(ia / "cache" / "telechargements" / "dl.bin", 700);
    writeBytes(ia / "cache" / "perdu.bin", 50);                            // hors des six categories
    writeBytes(ia / "autre.txt", 10);                                      // a la racine
    l.convBytes = readAll(l.conv);
    l.pieceBytes = readAll(l.piece);
    return l;
}

void space() {
    std::printf("== 4. le calcul reel de l'espace ==\n");
    const auto l = fillFolder();
    const fs::path ia = l.root;
    const auto m = mem::measure();
    const std::uintmax_t convs = sizeOf(l.conv) + sizeOf(ia / "conversations" / "c-20261030-180000-1.json");
    const std::uintmax_t activity = sizeOf(ia / "activite.jsonl");
    std::uintmax_t knowledge = 0;
    std::size_t knowledgeFiles = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(ia / "connaissances", ec)) {
        knowledge += e.file_size();
        ++knowledgeFiles;
    }
    const std::uintmax_t settings = 120 + sizeOf(ia / "memoire.json");
    check(m.exists && m.root == app::ai::root(), "le dossier mesure : " + m.root);
    check(m.of(mem::Part::Conversations).bytes == convs && m.of(mem::Part::Conversations).files == 2, "conversations : la somme exacte");
    check(m.of(mem::Part::Attachments).bytes == 8000 && m.of(mem::Part::Attachments).files == 2, "pieces jointes : 5000 + 3000");
    check(m.of(mem::Part::Knowledge).bytes == knowledge && m.of(mem::Part::Knowledge).files == knowledgeFiles, "connaissances : leurs fichiers");
    check(m.of(mem::Part::Index).bytes == 700 && m.of(mem::Part::Logs).bytes == 300, "index 700, journaux 300");
    check(m.of(mem::Part::Activity).bytes == activity && activity > 0, "le journal d'activite (" + std::to_string(activity) + " o)");
    check(m.of(mem::Part::Settings).bytes == settings, "parametres.json + memoire.json");
    check(m.of(mem::Part::CacheRequests).bytes == 1024 && m.of(mem::Part::CacheRequests).files == 2, "cache des requetes : 1000 + 24 (un sous-dossier compris)");
    check(m.of(mem::Part::CacheProcessing).bytes == 2000 && m.of(mem::Part::CacheImages).bytes == 4000 && m.of(mem::Part::CacheDocuments).bytes == 500
              && m.of(mem::Part::CachePreviews).bytes == 600 && m.of(mem::Part::CacheDownloads).bytes == 700,
          "les six categories du cache");
    check(m.of(mem::Part::CacheOther).bytes == 50 && m.of(mem::Part::Other).bytes == 10, "le reste du cache, le reste de la racine");
    check(m.cache().bytes == 1024 + 2000 + 4000 + 500 + 600 + 700 + 50 && m.cache().files == 8, "le cache : " + std::to_string(m.cache().bytes));
    std::uintmax_t sum = 0;
    std::size_t files = 0;
    for (const auto& u : m.parts) {
        sum += u.bytes;
        files += u.files;
    }
    std::uintmax_t real = 0;
    std::size_t realFiles = 0;
    for (const auto& e : fs::recursive_directory_iterator(ia, ec))
        if (e.is_regular_file()) {
            real += e.file_size();
            ++realFiles;
        }
    check(m.total.bytes == sum && m.total.bytes == real && m.total.files == realFiles && files == realFiles,
          "le total = la somme des categories = ce qu'il y a sur le disque (" + std::to_string(m.total.bytes) + " o, " + std::to_string(realFiles) + " fichiers)");
    check(m.disk.known && m.disk.capacity > 0 && m.disk.available > 0 && m.disk.available <= m.disk.capacity, "l'espace du disque (std::filesystem::space)");
    // les conversations au format du contrat
    const auto convs2 = mem::loadConversations();
    std::size_t messages = 0;
    for (const auto& c : convs2) messages += c.messages.size();
    check(convs2.size() == 2 && messages == 4, "2 conversations, 4 messages");
    bool attached = false, missingFile = false;
    for (const auto& c : convs2)
        for (const auto& a : c.attachments) {
            if (a.name == "plan.pdf" && a.present && a.bytes == 5000 && c.id == "c-20261030-173512-1") attached = true;
            if (a.file == "pieces-jointes/p9/absent.pdf" && !a.present) missingFile = true;
        }
    check(attached && missingFile, "les pieces jointes citees : presente (5000 o), absente dite");
    check(convs2.front().project == "Traitement_Eau" || convs2.front().project == "Station_Pompage", "le projet d'une conversation");
    const auto folders = mem::attachmentFolders(convs2);
    bool p1 = false, p2 = false;
    for (const auto& f : folders) {
        if (f.id == "p1" && f.conversation == "c-20261030-173512-1" && f.usage.bytes == 5000) p1 = true;
        if (f.id == "p2" && f.conversation.empty() && f.usage.bytes == 3000) p2 = true;
    }
    check(p1 && p2, "les dossiers joints : p1 cite par sa conversation, p2 orphelin");
}

// ------------------------------------------------------------------------- 5. le nettoyage ---
void cleaning() {
    std::printf("== 5. le nettoyage du cache ==\n");
    const fs::path ia = app::ai::root();
    const fs::path conv = ia / "conversations" / "c-20261030-173512-1.json";
    const fs::path piece = ia / "pieces-jointes" / "p1" / "plan.pdf";
    const std::string convBefore = readAll(conv), pieceBefore = readAll(piece);
    const auto before = mem::measure();
    const auto plan = mem::cachePlan(before);
    check(plan.size() == 7, "le detail avant : six categories et les autres fichiers du cache (" + std::to_string(plan.size()) + ")");
    bool consequences = true;
    for (const auto& line : plan) consequences = consequences && !line.consequence.empty();
    check(consequences && plan.front().usage.bytes == 1024, "chaque categorie : sa taille, ce que ca change");
    // vider deux categories seulement
    const auto r = mem::clearCache({mem::Part::CacheImages, mem::Part::CacheRequests, mem::Part::Conversations});
    check(r.ok() && r.freed.bytes == 5024 && r.freed.files == 3, "vides : images et requetes (5024 o, 3 fichiers) ; une part hors du cache ignoree");
    const auto after = mem::measure();
    check(after.of(mem::Part::CacheImages).files == 0 && after.of(mem::Part::CacheRequests).files == 0 && after.of(mem::Part::CacheProcessing).bytes == 2000,
          "les cochees vides, les autres intactes");
    check(fs::is_directory(ia / "cache" / "images"), "le dossier de la categorie reste");
    check(readAll(conv) == convBefore && readAll(piece) == pieceBefore, "conversation et piece jointe originale : octet pour octet");
    check(after.of(mem::Part::Conversations).bytes == before.of(mem::Part::Conversations).bytes
              && after.of(mem::Part::Attachments).bytes == before.of(mem::Part::Attachments).bytes
              && after.of(mem::Part::Knowledge).bytes == before.of(mem::Part::Knowledge).bytes,
          "les tailles des donnees persistantes ne bougent pas");
    check(!after.lastCleaning.empty() && after.lastCleaning == mem::readState().lastCleaning, "la date du dernier nettoyage gardee : " + after.lastCleaning);
    const auto acts = mem::readActivity();
    check(!acts.empty() && acts.back().type == "nettoyage" && acts.back().text.find(mem::sizeText(5024)) != std::string::npos
              && acts.back().text.find("3 fichiers") != std::string::npos,
          "l'activite : " + (acts.empty() ? std::string("?") : acts.back().text));
    // un lien dans le cache vers un fichier et un dossier HORS de IA/ : le lien part, jamais ce qu'il vise
    const fs::path outsideFile = scratch() / "dehors.txt";
    const fs::path outsideDir = scratch() / "dehors";
    writeText(outsideFile, "a garder");
    writeText(outsideDir / "garde.txt", "a garder aussi");
    std::error_code lec;
    fs::create_symlink(outsideFile, ia / "cache" / "documents" / "lien.txt", lec);
    fs::create_directory_symlink(outsideDir, ia / "cache" / "traitement" / "lien-dossier", lec);
    const bool links = !lec;
    // tout le reste du cache
    const auto r2 = mem::clearCache({mem::Part::CacheProcessing, mem::Part::CacheDocuments, mem::Part::CachePreviews, mem::Part::CacheDownloads,
                                     mem::Part::CacheOther});
    const auto empty = mem::measure();
    check(r2.ok() && empty.cache().files == 0 && empty.cache().bytes == 0 && !fs::exists(ia / "cache" / "perdu.bin"), "le cache entier vide");
    if (links)
        check(readAll(outsideFile) == "a garder" && readAll(outsideDir / "garde.txt") == "a garder aussi"
                  && !fs::exists(fs::symlink_status(ia / "cache" / "documents" / "lien.txt")),
              "un lien du cache vers l'exterieur : le lien part, ce qu'il vise reste");
    check(readAll(conv) == convBefore && readAll(piece) == pieceBefore && fs::exists(ia / "parametres.json") && fs::exists(ia / "autre.txt"),
          "toujours intacts : conversations, pieces jointes, parametres, le reste");
}

// ------------------------------------------------------------- 6. les donnees persistantes ---
void persistent() {
    std::printf("== 6. les donnees persistantes : seulement confirme ==\n");
    const fs::path ia = app::ai::root();
    const auto before = mem::measure();
    const auto refused = mem::deletePersistent({mem::Persistent::Attachments}, false);
    check(!refused.ok() && refused.freed.files == 0 && mem::measure().of(mem::Part::Attachments).bytes == before.of(mem::Part::Attachments).bytes,
          "sans confirmation : refuse, rien ne part");
    const auto plan = mem::persistentPlan(before);
    check(plan.size() == 6 && plan[1].usage.bytes == 8000, "le detail : six categories, les pieces jointes 8000 o");
    const auto r = mem::deletePersistent({mem::Persistent::Attachments}, true);
    const auto after = mem::measure();
    check(r.ok() && r.freed.bytes == 8000 && after.of(mem::Part::Attachments).files == 0 && after.of(mem::Part::Conversations).files == 2,
          "confirme : les pieces jointes seules");
    check(fs::exists(ia / "parametres.json"), "les parametres restent");
}

// --------------------------------------------------------- 7. RAM, activite, recherche ---
void ramActivitySearch() {
    std::printf("== 7. la memoire vive, l'activite, la recherche ==\n");
    const auto ram = mem::readRam();
    check(ram.processKnown && ram.process > 0 && ram.peak >= ram.process, "la RAM du processus lue (" + mem::sizeText(ram.process) + ", pic " + mem::sizeText(ram.peak) + ")");
    check(!ram.systemKnown || (ram.total > 0 && ram.available <= ram.total), "la RAM du PC, si elle se lit");
    check(ram.localAi == 0, "les traitements IA locaux : aucun");
    const auto n = mem::readActivity().size();
    check(mem::appendActivity("fichier", "Fichier analys\xC3\xA9 : plan.pdf") && mem::appendActivity("conversation", "Conversation cr\xC3\xA9\xC3\xA9" "e"), "deux lignes");
    // une ligne abimee, ecrite par un autre, est sautee
    {
        std::ofstream out(fs::path(app::ai::activityFile()), std::ios::binary | std::ios::app);
        out << "pas du json\n";
    }
    const auto acts = mem::readActivity();
    check(acts.size() == n + 2 && acts.back().type == "conversation" && acts[acts.size() - 2].text == "Fichier analys\xC3\xA9 : plan.pdf",
          "relues dans l'ordre, la ligne abimee sautee");
    check(!acts.back().date.empty() && acts.back().date[10] == 'T', "la date ISO locale");
    // la recherche
    mem::Registry reg;
    reg.load();
    mem::Knowledge k;
    k.name = "Vanne motoris\xC3\xA9" "e";
    k.content = "Une vanne motoris\xC3\xA9" "e a deux fins de course.";
    check(reg.add(k), "une connaissance a chercher");
    const auto convs = mem::loadConversations();
    auto hits = mem::search("station", convs, reg.all());
    bool title = false;
    for (const auto& h : hits) title = title || (h.kind == mem::Hit::Kind::Conversation && h.title == "Station de pompage");
    check(title, "un titre de conversation");
    hits = mem::search("synoptique cuves", convs, reg.all());
    bool msg = false;
    for (const auto& h : hits) msg = msg || (h.kind == mem::Hit::Kind::Message && h.excerpt.find("synoptique") != std::string::npos);
    check(msg, "un message (deux mots, l'extrait)");
    hits = mem::search("motorisee", convs, reg.all());
    bool kn = false;
    for (const auto& h : hits) kn = kn || (h.kind == mem::Hit::Kind::Knowledge && h.ref == k.id);
    check(kn, "une connaissance, sans les accents");
    hits = mem::search("plan.pdf", convs, reg.all());
    bool att = false;
    for (const auto& h : hits) att = att || h.kind == mem::Hit::Kind::Attachment;
    check(att, "une reference a une piece jointe");
    hits = mem::search("genres d'objets", convs, reg.all());
    bool built = false;
    for (const auto& h : hits) built = built || h.ref == mem::kKindsKnowledge;
    check(built, "une connaissance du logiciel");
    check(mem::search("", convs, reg.all()).empty() && mem::search("introuvablezz", convs, reg.all()).empty(), "rien : rien");
    check(reg.remove(k.id), "nettoye");
}

// ---------------------------------------------------------------------------- 8. la fenetre ---
class Recorder final : public gfx::IRenderer {
public:
    gfx::Size size{1920.f, 1200.f};
    gfx::Rect panel{};
    std::vector<std::string> outside;
    std::size_t texts{0};
    std::string technical;   // 1.14.2 (captures) : un nom de fonction du systeme ou de fichier systeme dessine
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    std::vector<gfx::Rect> clips;
    void pushClip(const gfx::Rect& r) override {
        gfx::Rect c = r;
        if (!clips.empty()) {
            const auto& o = clips.back();
            const float x0 = std::max(c.x, o.x), y0 = std::max(c.y, o.y);
            const float x1 = std::min(c.right(), o.right()), y1 = std::min(c.bottom(), o.bottom());
            c = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
        }
        clips.push_back(c);
    }
    void popClip() override {
        if (!clips.empty()) clips.pop_back();
    }
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point o, std::string_view t, gfx::FontId f, gfx::Color) override {
        ++texts;
        for (const char* api : {"/proc/", "GetProcessMemoryInfo", "GlobalMemoryStatus"})
            if (t.find(api) != std::string_view::npos) technical = std::string(t);
        // ce qui se voit : le texte coupe par la decoupe en cours
        float x0 = o.x, y0 = o.y, x1 = o.x + measure(t, f).width, y1 = o.y + static_cast<float>(f.v);
        if (!clips.empty()) {
            const auto& c = clips.back();
            x0 = std::max(x0, c.x);
            y0 = std::max(y0, c.y);
            x1 = std::min(x1, c.right());
            y1 = std::min(y1, c.bottom());
            if (x1 <= x0 || y1 <= y0) return;
        }
        if (panel.w > 0.f && (x0 < panel.x - 0.5f || x1 > panel.right() + 1.f || y0 < panel.y - 0.5f || y1 > panel.bottom() + 1.f))
            outside.emplace_back(t);
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {0.55f * static_cast<float>(f.v) * static_cast<float>(t.size()) * 0.9f, static_cast<float>(f.v), 0.76f * f.v, 0.24f * f.v};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view t, gfx::FontId f, float maxWidth) const override {
        const float per = 0.55f * static_cast<float>(f.v) * 0.9f;
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, maxWidth / per)));
    }
};

void paintAll(app::AiMemoryDialog& d, Recorder& rec, const ui::Theme& theme, const char* label) {
    ui::Widget* root = d.widgetRoot();
    root->setBounds({0.f, 0.f, rec.size.w, rec.size.h});
    root->layout();
    const float w = std::min(1240.f, rec.size.w - 40.f), h = std::min(800.f, rec.size.h - 40.f);
    rec.panel = {std::floor((rec.size.w - w) * 0.5f), std::floor((rec.size.h - h) * 0.5f), w, h};
    for (int t = 0; t <= static_cast<int>(app::AiMemoryDialog::Tab::Activity); ++t) {
        d.showTab(static_cast<app::AiMemoryDialog::Tab>(t));
        root->layout();
        std::vector<ui::Widget*> overlays;
        rec.outside.clear();
        rec.texts = 0;
        rec.technical.clear();
        const ui::PaintContext ctx{rec, theme, {0.f, 0.f, rec.size.w, rec.size.h}, 0.0, &overlays};
        root->invalidate();
        root->render(ctx);
        check(rec.texts > 5, std::string(label) + " : l'onglet " + app::AiMemoryDialog::tabTitle(static_cast<app::AiMemoryDialog::Tab>(t)) + " se dessine");
        check(rec.technical.empty(), std::string(label) + " : aucun nom de fonction ni de fichier du syst\xC3\xA8me \xC3\xA0 l'\xC3\xA9" "cran ("
                                         + app::AiMemoryDialog::tabTitle(static_cast<app::AiMemoryDialog::Tab>(t)) + (rec.technical.empty() ? std::string{} : " : " + rec.technical) + ")");
        check(rec.outside.empty(), std::string(label) + " : aucun texte hors de la fenetre (" + app::AiMemoryDialog::tabTitle(static_cast<app::AiMemoryDialog::Tab>(t))
                                       + (rec.outside.empty() ? std::string{} : " : " + rec.outside.front()) + ")");
    }
}

void dialog() {
    std::printf("== 8. la fenetre : les jauges et les comptes suivent chaque operation ==\n");
    const fs::path ia = app::ai::root();
    // de nouveau du cache et des pieces jointes
    writeBytes(ia / "cache" / "images" / "i.png", 4000);
    writeBytes(ia / "cache" / "apercus" / "a.bin", 600);
    writeBytes(ia / "pieces-jointes" / "p1" / "plan.pdf", 5000, 'P');
    app::AiMemoryDialog::Spec spec;
    spec.project = "Station_Pompage";
    std::string opened;
    spec.openFolder = [&](const std::string& p) {
        opened = p;
        return std::string{};
    };
    app::AiMemoryDialog d(std::move(spec));
    check(static_cast<bool>(d.Initialize()), "la fenetre se construit");
    const auto total0 = d.measured().total.bytes;
    check(d.conversations().size() == 2 && d.messageCount() == 4 && d.registry().kindsKnown() == 116, "les comptes : 2 conversations, 4 messages, 116/116");
    check(d.stateText().find("2 conversations") != std::string::npos, "l'etat : " + d.stateText());
    check(d.measured().cache().bytes == 4600, "la jauge du cache : 4600 o");
    // ajouter une connaissance : comptes, tailles, activite suivent
    const auto act0 = d.activity().size();
    mem::Knowledge k;
    k.name = "Regle des couleurs";
    k.category = "preferences";
    k.content = "Marche : vert ; arret : gris ; defaut : rouge.";
    check(d.addKnowledge(k), "ajouter (la fenetre) : " + d.message());
    check(d.registry().count(false) == 7 && d.measured().of(mem::Part::Knowledge).files == 1 && d.measured().total.bytes > total0,
          "apres l'ajout : 7 connaissances, un fichier de plus, le total grandit");
    check(d.activity().size() == act0 + 1 && d.activity().back().type == "connaissance", "l'activite suit");
    check(d.selectedKnowledge() && d.selectedKnowledge()->name == "Regle des couleurs", "la nouvelle est choisie");
    // l'editeur : modifier puis enregistrer
    d.beginEdit();
    check(d.editorOpen() && !d.editorReadOnly(), "Modifier ouvre l'editeur");
    d.closeEditor();
    d.beginView();
    check(d.editorOpen() && d.editorReadOnly() && !d.saveEditor(), "Consulter : en lecture seule, rien a enregistrer");
    d.closeEditor();
    mem::Knowledge changes = *d.selectedKnowledge();
    changes.content += " Alarme : clignotant.";
    check(d.updateSelected(changes) && d.selectedKnowledge()->content == changes.content, "modifier (la fenetre)");
    // le filtre par categorie et par projet
    d.setCategoryFilter("preferences");
    check(d.shownKnowledge().size() == 1, "le filtre Preferences : 1");
    d.setCategoryFilter("");
    d.setProjectFilter("*");
    check(d.shownKnowledge().size() == 7, "les communes a tous les projets : 7");
    d.setProjectFilter("");
    // desactiver la connaissance des genres : le compte suit
    check(d.selectKnowledge("Genres d'objets IHM") && d.toggleSelected() && d.registry().kindsKnown() == 0, "desactiver les genres : 0/116");
    check(!d.deleteSelected(), "une integree : pas de suppression proposee");
    check(d.toggleSelected() && d.registry().kindsKnown() == 116, "reactivee : 116/116");
    // supprimer : confirme d'abord
    check(d.selectKnowledge("Regle des couleurs") && d.deleteSelected() && d.confirming(), "supprimer : la confirmation d'abord (" + d.confirmText() + ")");
    d.confirmNo();
    check(!d.confirming() && d.registry().count(false) == 7, "Non : rien ne part");
    check(d.deleteSelected() && d.confirmYes() && d.registry().count(false) == 6 && d.measured().of(mem::Part::Knowledge).files == 0,
          "Oui : supprimee, la jauge suit");
    // Memoire : une conversation
    d.showTab(app::AiMemoryDialog::Tab::Memory);
    d.showMemoryCategory(app::AiMemoryDialog::MemCat::Conversations);
    check(d.memoryRowCount() == 2 && d.selectMemoryRow("Traitement"), "les conversations, en choisir une");
    check(d.deleteMemorySelection() && d.confirming() && d.confirmText().find("Traitement de l'eau") != std::string::npos, "sa suppression demandee");
    check(d.confirmYes() && d.conversations().size() == 1 && d.messageCount() == 2 && d.measured().of(mem::Part::Conversations).files == 1,
          "supprimee : 1 conversation, 2 messages, la jauge suit");
    check(d.activity().back().type == "suppression" && d.activity().back().text.find("Traitement de l'eau") != std::string::npos,
          "son activite : " + d.activity().back().text);
    // la recherche
    d.setSearch("pompage");
    check(d.memoryCategory() == app::AiMemoryDialog::MemCat::Search && !d.hits().empty() && d.memoryRowCount() == d.hits().size(), "la recherche");
    d.setSearch("");
    // une piece jointe
    d.showMemoryCategory(app::AiMemoryDialog::MemCat::Attachments);
    check(d.selectMemoryRow("plan.pdf") && d.deleteMemorySelection(true) && d.measured().of(mem::Part::Attachments).files == 0, "une piece jointe supprimee");
    // Stockage : l'emplacement, Ouvrir le dossier
    check(d.location() == app::ai::root() && d.openFolder() && opened == app::ai::root(), "l'emplacement, Ouvrir le dossier");
    // Nettoyage : le detail, la confirmation, la jauge
    d.showTab(app::AiMemoryDialog::Tab::Cleaning);
    check(d.cacheSelection().bytes == 4600, "a liberer : tout le cache, coche par defaut (4600 o)");
    d.setCacheChecked(mem::Part::CacheImages, false);
    check(d.cacheSelection().bytes == 600 && !d.cacheChecked(mem::Part::CacheImages), "les images decochees : 600 o");
    check(d.clearCache() && d.confirming() && d.confirmText().find("600 o") != std::string::npos,
          "Vider le cache : la confirmation dit ce qui sera libere (" + d.confirmText() + ")");
    d.confirmNo();
    check(d.measured().cache().bytes == 4600, "Non : rien ne part");
    check(d.clearCache() && d.confirmYes() && d.measured().cache().bytes == 4000 && !d.measured().lastCleaning.empty(),
          "Oui : 600 o liberes, la jauge suit, la date gardee (" + d.message() + ")");
    // les donnees persistantes : "Je comprends", puis la confirmation
    d.setPersistentChecked(mem::Persistent::Conversations, true);
    check(!d.deletePersistent() && d.conversations().size() == 1, "sans \xC2\xAB Je comprends \xC2\xBB : refuse");
    d.setUnderstood(true);
    check(d.deletePersistent() && d.confirming(), "\xC2\xAB Je comprends \xC2\xBB coche : la confirmation (" + d.confirmText() + ")");
    d.confirmNo();
    check(d.conversations().size() == 1, "Non : la conversation reste");
    check(d.deletePersistent() && d.confirmYes() && d.conversations().empty() && d.messageCount() == 0
              && d.measured().of(mem::Part::Conversations).files == 0,
          "Oui : supprimees, les comptes et les jauges suivent");
    check(!d.understood(), "\xC2\xAB Je comprends \xC2\xBB se decoche apres");
    check(d.measured().of(mem::Part::Settings).files >= 1 && fs::exists(ia / "parametres.json"), "les parametres jamais touches");
    // 1.14.2 (captures) : une categorie VIDE ne se coche pas dans la zone rouge (grisee, son infobulle dit
    // pourquoi) ; Supprimer definitivement reste grise tant qu'aucune categorie non vide n'est cochee.
    {
        auto* box = dynamic_cast<ui::Checkbox*>(d.widgetRoot()->findById("ia.memoire.nettoyage.persistant.conversations"));
        auto* erase = dynamic_cast<ui::Button*>(d.widgetRoot()->findById("ia.memoire.nettoyage.supprimer"));
        check(box && erase, "la zone rouge : la case des conversations et le bouton");
        d.setUnderstood(true);
        d.setPersistentChecked(mem::Persistent::Conversations, true);
        check(d.persistentEmpty(mem::Persistent::Conversations) && !d.persistentChecked(mem::Persistent::Conversations),
              "une categorie vide (0 conversation) ne se coche pas");
        check(box && !box->enabled() && !box->isChecked() && box->tooltip().find("rien \xC3\xA0 supprimer") != std::string::npos,
              "sa case est grisee, son infobulle dit pourquoi (" + (box ? box->tooltip() : std::string{}) + ")");
        check(erase && !erase->enabled() && !erase->tooltip().empty(), "\xC2\xAB Je comprends \xC2\xBB coche, rien de non vide : Supprimer reste grise ("
                                                                     + (erase ? erase->tooltip() : std::string{}) + ")");
        check(!d.deletePersistent() && !d.confirming(), "rien a supprimer : pas de confirmation (" + d.message() + ")");
        check(!d.persistentEmpty(mem::Persistent::Activity), "le journal d'activite n'est pas vide");
        d.setPersistentChecked(mem::Persistent::Activity, true);
        check(d.persistentChecked(mem::Persistent::Activity) && erase && erase->enabled() && erase->tooltip().empty(),
              "une categorie non vide cochee : Supprimer s'active");
        auto* act = dynamic_cast<ui::Checkbox*>(d.widgetRoot()->findById("ia.memoire.nettoyage.persistant.activite"));
        check(act && act->enabled() && act->isChecked(), "sa case est cochee et active");
        d.setPersistentChecked(mem::Persistent::Activity, false);
        d.setUnderstood(false);
        check(erase && !erase->enabled() && erase->tooltip().find("Je comprends") != std::string::npos, "decochee : de nouveau grise");
    }
    // 1.14.2 (captures) : ouverte par-dessus une autre fenetre modale (les parametres de l'Assistant), elle
    // n'assombrit pas le fond une seconde fois.
    {
        check(d.traits().dimsBelow, "seule : son voile assombrit le fond");
        app::AiMemoryDialog::Spec over;
        over.overModal = true;
        const app::AiMemoryDialog d2(std::move(over));
        check(!d2.traits().dimsBelow && d2.traits().blocksInput, "sur une autre fenetre modale : pas de second voile (toujours modale)");
    }
    // 1.14.2 (captures) : la memoire vive dite en mots de l'utilisateur ; le detail technique a part (l'infobulle)
    check(!d.ram().by.empty() && d.ram().by.find("/proc") == std::string::npos && d.ram().by.find("Get") == std::string::npos,
          "la memoire vive : mesuree par " + d.ram().by);
    // Activite : le journal, filtre par type
    d.showTab(app::AiMemoryDialog::Tab::Activity);
    const auto all = d.activityRowCount();
    d.setActivityFilter("nettoyage");
    check(d.activityRowCount() >= 1 && d.activityRowCount() < all, "le journal filtre par type (" + std::to_string(d.activityRowCount()) + " sur "
                                                                        + std::to_string(all) + ")");
    d.setActivityFilter("");
    check(d.selectRow("Cache vid") && d.currentTable() && d.currentTable()->selection().size() == 1, "une ligne du journal par son texte (seance : memoire-ligne)");
    std::size_t nettoyages = 0, suppressions = 0, connaissances = 0;
    for (const auto& a : d.activity()) {
        nettoyages += a.type == "nettoyage";
        suppressions += a.type == "suppression";
        connaissances += a.type == "connaissance";
    }
    check(nettoyages >= 1 && suppressions >= 4 && connaissances >= 4, "chaque operation de la fenetre a sa ligne (ajout, modification, activation, suppressions, nettoyage)");
    check(d.showTab("stockage") && d.tab() == app::AiMemoryDialog::Tab::Storage && !d.showTab("inconnu"), "un onglet par son nom");
    // le dessin des six onglets, aux deux tailles, dans les deux themes
    writeText(ia / "conversations" / "c-20261030-173512-1.json",
              conversationJson("c-20261030-173512-1", "Station de pompage", "Station_Pompage", "pieces-jointes/p1/plan.pdf"));
    d.refresh();
    Recorder big;
    paintAll(d, big, ui::Theme::dark(), "1920 x 1200");
    Recorder small;
    small.size = {1280.f, 800.f};
    paintAll(d, small, ui::Theme::light(), "1280 x 800");
    d.beginAdd();
    Recorder edit;
    paintAll(d, edit, ui::Theme::dark(), "l'editeur ouvert");
}

} // namespace

int main() {
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    fs::create_directories(scratch(), ec);
    app::ai::setRoot((scratch() / "IA").string());
    json();
    knowledge();
    perProject();
    space();
    cleaning();
    persistent();
    ramActivitySearch();
    dialog();
    if (!std::getenv("AI_MEMOIRE_GARDER")) fs::remove_all(scratch(), ec);
    else std::printf("garde : %s\n", scratch().string().c_str());
    std::printf("%d verification(s), %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
