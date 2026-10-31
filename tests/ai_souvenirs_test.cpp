// tests/ai_souvenirs_test.cpp - 1.21.0 : LA MEMOIRE PERSISTANTE de l'Assistant (section 4 de la demande, essais 9.4).
//
//   ai_souvenirs_test
//
//  Sans ecran ni fournisseur, sur un dossier IA/ jetable :
//  1. les portees (utilisateur, projet, conversation, technique), les refus ;
//  2. l'identite des projets : un identifiant fiable, deux projets de meme nom distingues, un projet deplace retrouve,
//     une copie distinguee ; l'ISOLATION de deux projets (jamais un souvenir de l'un dans les demandes de l'autre) ;
//  3. les contradictions (les deux gardees, la provenance, le conflit signale, resolu) ; 4. les doublons ;
//  5. epingler, le statut, la confiance ; relus apres un redemarrage ;
//  6. l'ecrivain << assistant >> : proposer (provisoire), valider, refuser ; ce qui part avec une demande ;
//  7. la migration d'un fichier du format 1 (sauvegarde d'abord, champ inconnu garde) ; sauvegarder, restaurer ;
//  8. la recherche dans les anciennes conversations : l'index (sur son fil aussi), par mots, objet, equipement, langage,
//     date, projet, probleme, solution ; la chronologie ; rien : le dire ;
//  9. les outils du modele (proposer_souvenir, chercher_souvenirs, chercher_conversations) ;
//  10. la fenetre : l'onglet Souvenirs (ses gestes, sa phrase honnete, son dessin sans texte dehors).
#include "../src/app/ai/AiConversationIndex.hpp"
#include "../src/app/ai/AiKnowledge.hpp"
#include "../src/app/ai/AiMemory.hpp"
#include "../src/app/ai/AiMemoryDialog.hpp"
#include "../src/app/ai/AiMemoryJson.hpp"
#include "../src/app/ai/AiMemoryManager.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiStore.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"

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
namespace ai = app::ai;
namespace mem = app::ai::memory;
namespace ci = app::ai::convindex;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    static const bool all = [] {
        const char* e = std::getenv("AI_SOUVENIRS_TOUT");
        return e && *e == '1';
    }();
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    } else if (all) {
        std::printf("  ok : %s\n", what.c_str());
    }
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path()
                              / ("xpg_ai_souvenirs_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007));
    return p;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

// Deux projets de MEME NOM dans deux dossiers, et un troisieme.
struct Projects {
    mem::ProjectIdentity a, b, c;
};
Projects makeProjects() {
    Projects p;
    std::error_code ec;
    fs::create_directories(scratch() / "affaires" / "A" / "Station", ec);
    fs::create_directories(scratch() / "affaires" / "B" / "Station", ec);
    fs::create_directories(scratch() / "affaires" / "Eau", ec);
    p.a = mem::identifyProject("Station", (scratch() / "affaires" / "A" / "Station").string(), "2026-10-01 08:00");
    p.b = mem::identifyProject("Station", (scratch() / "affaires" / "B" / "Station").string(), "2026-10-02 09:00");
    p.c = mem::identifyProject("Eau", (scratch() / "affaires" / "Eau").string());
    return p;
}

// ------------------------------------------------------------------------- 1. les portees ---
void scopes() {
    std::printf("== 1. les portees ==\n");
    mem::Registry reg;
    reg.load();
    check(mem::scopeFor("preferences", "") == "utilisateur" && mem::scopeFor("regles", "P") == "projet" && mem::scopeFor("metier", "") == "technique",
          "la portee deduite d'une connaissance d'avant (categorie, projet)");
    mem::Knowledge u;
    u.scope = "utilisateur";
    u.content = "R\xC3\xA9ponses courtes, en fran\xC3\xA7" "ais, avec le code d'abord.";
    const auto ru = reg.remember(u);
    check(ru.kind == mem::RememberResult::Kind::Added && reg.find(ru.id) && reg.find(ru.id)->scopeKey() == "utilisateur", "une preference utilisateur");
    check(reg.find(ru.id) && reg.find(ru.id)->name.rfind("R\xC3\xA9ponses courtes", 0) == 0, "le nom tire de la premiere ligne du contenu");
    check(reg.find(ru.id) && reg.find(ru.id)->category == "preferences", "sa categorie : preferences");
    mem::Knowledge t;
    t.scope = "technique";
    t.name = "Timer TON en ST";
    t.content = "Un TON se relance en remettant IN a FALSE un cycle.";
    check(reg.remember(t).kind == mem::RememberResult::Kind::Added, "une solution technique");
    mem::Knowledge p;
    p.scope = "projet";
    p.content = "Les pompes se nomment P_xx.";
    const auto rp = reg.remember(p);
    check(rp.kind == mem::RememberResult::Kind::Refused && !rp.why.empty(), "un souvenir de projet sans projet : refuse (" + rp.why + ")");
    mem::Knowledge cv;
    cv.scope = "conversation";
    cv.content = "Reste a faire : la vue des alarmes.";
    check(reg.remember(cv).kind == mem::RememberResult::Kind::Refused, "un souvenir de conversation sans conversation : refuse");
    cv.conversation = "c-1";
    const auto rc = reg.remember(cv);
    check(rc.ok(), "avec sa conversation : retenu");
    mem::Context inC1, inC2;
    inC1.conversationId = "c-1";
    inC2.conversationId = "c-2";
    if (const auto* k = reg.find(rc.id)) {
        check(mem::hereState(*k, inC1) == mem::Here::Active && mem::hereState(*k, inC2) == mem::Here::OtherConversation,
              "un souvenir de conversation : actif dans la sienne seulement");
    }
    check(!mem::scopeLabel("technique").empty() && mem::statusLabel("hypothese") == "Hypoth\xC3\xA8se", "les libelles");
}

// ---------------------------------------------------- 2. l'identite des projets, l'isolation ---
void projectsAndIsolation() {
    std::printf("== 2. l'identite des projets, l'isolation de deux projets ==\n");
    const auto p = makeProjects();
    check(p.a.id.rfind("prj-", 0) == 0 && p.b.id.rfind("prj-", 0) == 0 && p.a.id != p.b.id, "deux projets de meme nom : deux identifiants (" + p.a.id + ", " + p.b.id + ")");
    check(p.a.ambiguous && p.b.ambiguous && !p.c.ambiguous, "le nom porte par deux projets est dit ambigu");
    const auto again = mem::identifyProject("Station", (scratch() / "affaires" / "A" / "Station" / "." / "").string());
    check(again.id == p.a.id && !again.isNew, "le meme dossier (ecrit autrement) : le meme identifiant");
    check(fs::exists(ai::projectsFile()) && mem::knownProjects().size() == 3, "projets.json : trois projets connus");
    check(mem::projectNameOf(p.c.id) == "Eau", "le nom par l'identifiant");
    // deplace : l'ancien dossier n'existe plus, meme nom et meme creation -> le meme identifiant
    std::error_code ec;
    fs::create_directories(scratch() / "affaires" / "D", ec);
    const auto before = mem::identifyProject("Ligne", (scratch() / "affaires" / "D" / "Ligne").string(), "2026-09-30 07:00");
    fs::create_directories(scratch() / "affaires" / "D" / "Ligne", ec);
    fs::rename(scratch() / "affaires" / "D" / "Ligne", scratch() / "affaires" / "D" / "Ligne_v2", ec);
    const auto moved = mem::identifyProject("Ligne", (scratch() / "affaires" / "D" / "Ligne_v2").string(), "2026-09-30 07:00");
    check(moved.id == before.id && moved.relinked, "un projet deplace garde son identifiant (ses souvenirs le suivent)");
    // copie : l'original est toujours la -> un autre identifiant
    fs::create_directories(scratch() / "affaires" / "E" / "Ligne_v2", ec);
    const auto copy = mem::identifyProject("Ligne", (scratch() / "affaires" / "E" / "Ligne_v2").string(), "2026-09-30 07:00");
    check(copy.id != moved.id && !copy.relinked, "une copie (l'original toujours la) : un autre identifiant");
    check(mem::identifyProject("Sans dossier", "").id.empty(), "un projet jamais enregistre : pas d'identifiant");

    // L'ISOLATION
    mem::Registry reg;
    reg.load();
    const mem::Context ca = mem::contextFor(p.a), cb = mem::contextFor(p.b);
    mem::Knowledge k;
    k.scope = "projet";
    k.projectId = p.a.id;
    k.project = p.a.name;
    k.content = "SECRET_A : la station A a 3 pompes.";
    const auto r = reg.remember(k);
    check(r.kind == mem::RememberResult::Kind::Added, "un souvenir du projet A");
    const auto* ka = reg.find(r.id);
    check(ka && mem::hereState(*ka, ca) == mem::Here::Active && mem::hereState(*ka, cb) == mem::Here::OtherProject,
          "actif dans A, jamais dans B (meme nom, autre projet)");
    check(ka && mem::hereState(*ka, std::string_view("Station")) == mem::Here::OtherProject, "par le nom seul : jamais (il est lie par son identifiant)");
    // ce qui part avec une demande
    hmi::Project project;
    project.views = {hmi::makeView(project, "Vue_Station")};
    ai::PromptInput in;
    in.project = &project;
    in.view = &project.views[0];
    in.projectName = "Station";
    in.knowledge = &reg;
    in.projectId = p.b.id;
    in.projectNameAmbiguous = true;
    check(!has(ai::systemPrompt(in), "SECRET_A"), "une demande dans B n'emporte pas le souvenir de A");
    in.projectId = p.a.id;
    check(has(ai::systemPrompt(in), "SECRET_A"), "une demande dans A l'emporte");
    // un souvenir d'avant (lie par le nom) : pas quand le nom est ambigu ; lie une fois le nom sur
    mem::Knowledge legacy;
    legacy.name = "Ancienne regle";
    legacy.project = "Eau";
    legacy.content = "LEGACY_EAU : les vannes sont bleues.";
    check(reg.add(legacy), "un souvenir lie par le nom (comme avant la 1.21)");
    mem::Context ambiguous = ca;
    ambiguous.projectName = "Eau";
    ambiguous.nameAmbiguous = true;
    check(mem::hereState(*reg.find(legacy.id), ambiguous) == mem::Here::Unbound && reg.bindLegacy(ambiguous) == 0,
          "le nom porte par deux projets : pas envoye, pas lie (a associer)");
    const mem::Context cc = mem::contextFor(p.c);
    check(reg.bindLegacy(cc) == 1 && reg.find(legacy.id)->projectId == p.c.id, "le nom sur : lie a l'identifiant du projet Eau");
    check(mem::hereState(*reg.find(legacy.id), cc) == mem::Here::Active, "et actif dans Eau");
    // ASSOCIER a un projet, puis a tous
    std::string why;
    check(reg.associate(r.id, p.c.id, p.c.name, &why) && reg.find(r.id)->projectId == p.c.id, "associer au projet Eau : " + why);
    check(reg.associate(r.id, {}, {}, &why) && reg.find(r.id)->projectId.empty() && reg.find(r.id)->scopeKey() == "technique",
          "associer a tous les projets : la portee projet devient technique");
    check(has(reg.find(r.id)->history.back().text, "tous les projets"), "l'historique le dit");
    (void)reg.remove(r.id);
    (void)reg.remove(legacy.id);
}

// ---------------------------------------------------------- 3. les contradictions, 4. les doublons ---
void conflictsAndDuplicates() {
    std::printf("== 3. les contradictions ; 4. les doublons ==\n");
    const auto p = makeProjects();
    mem::Registry reg;
    reg.load();
    mem::Knowledge a;
    a.scope = "projet";
    a.projectId = p.a.id;
    a.project = p.a.name;
    a.name = "Convention de nommage";
    a.content = "Les pompes se nomment P_xx.";
    a.conversation = "c-10";
    a.message = "m3";
    const auto ra = reg.remember(a);
    check(ra.kind == mem::RememberResult::Kind::Added, "une convention");
    // le meme contenu, ecrit autrement : un doublon
    mem::Knowledge dup = a;
    dup.content = "  les POMPES se nomment p_xx !  ";
    const std::size_t before = reg.count(false);
    const auto rd = reg.remember(dup);
    check(rd.kind == mem::RememberResult::Kind::Duplicate && rd.id == ra.id && reg.count(false) == before, "le meme contenu : un doublon, rien d'ajoute");
    // une contradiction : les deux restent
    mem::Knowledge b = a;
    b.content = "Les pompes se nomment PMP_xx.";
    b.conversation = "c-11";
    b.message = "m1";
    b.source = "assistant";
    const auto rb = reg.remember(b);
    check(rb.kind == mem::RememberResult::Kind::Conflict && rb.conflictsWith == std::vector<std::string>{ra.id}, "une contradiction : signalee");
    const auto* ka = reg.find(ra.id);
    const auto* kb = reg.find(rb.id);
    check(ka && kb && ka->content == a.content && kb->content == b.content, "les deux sont gardees, rien n'est ecrase");
    check(ka && kb && ka->conflicted() && kb->conflicted() && reg.conflictsOf(ra.id).size() == 1, "toutes deux marquees en conflit");
    check(kb && has(kb->provenance(), "c-11") && has(kb->provenance(), "m1") && ka && has(ka->provenance(), "c-10"), "la provenance de chacune");
    check(ka && has(ka->history.back().text, "Contredite") && has(ka->history.back().text, "c-11"), "l'historique de l'ancienne dit qui la contredit");
    // ailleurs (un autre projet) : pas une contradiction
    mem::Knowledge other = b;
    other.projectId = p.b.id;
    other.conversation.clear();
    check(reg.remember(other).kind == mem::RememberResult::Kind::Added, "le meme sujet dans un AUTRE projet : pas en conflit");
    // relu apres un redemarrage
    mem::Registry again;
    again.load();
    check(again.find(rb.id) && again.find(rb.id)->conflicts == std::vector<std::string>{ra.id} && again.find(ra.id)->conflicted(), "le conflit relu");
    // resoudre : garder b
    std::string why;
    check(again.resolveConflict(rb.id, ra.id, false, &why), "garder la plus recente : " + why);
    check(!again.find(ra.id)->active && !again.find(ra.id)->conflicted() && !again.find(rb.id)->conflicted() && again.find(rb.id)->status == "confirme",
          "l'autre desactivee (gardee pour memoire), plus de conflit");
    // un conflit dont l'un est supprime : l'autre n'est plus en conflit
    mem::Knowledge c1 = a, c2 = a;
    c1.name = c2.name = "Couleur des defauts";
    c1.content = "Defaut en rouge.";
    c2.content = "Defaut en orange.";
    const auto r1 = again.remember(c1), r2 = again.remember(c2);
    check(r2.kind == mem::RememberResult::Kind::Conflict, "un nouveau conflit");
    check(again.remove(r1.id) && !again.find(r2.id)->conflicted(), "l'un supprime : l'autre n'est plus en conflit");
}

// ------------------------------------------------------------ 5. epingler, statut, confiance ---
void pinStatus() {
    std::printf("== 5. epingler, le statut, la confiance ==\n");
    mem::Registry reg;
    reg.load();
    mem::Knowledge k;
    k.scope = "utilisateur";
    k.content = "Toujours des noms de variables en majuscules.";
    const auto r = reg.remember(k);
    std::string why;
    check(reg.setPinned(r.id, true, &why) && reg.setStatus(r.id, "hypothese", &why) && reg.setConfidence(r.id, 0.4, &why), "epingle, hypothese, 40 %");
    check(!reg.setStatus(r.id, "bidule", &why), "un statut inconnu : refuse");
    mem::Registry again;
    again.load();
    const auto* x = again.find(r.id);
    check(x && x->pinned && x->status == "hypothese" && x->confidence > 0.39 && x->confidence < 0.41, "relus apres un redemarrage");
    check(x && x->history.size() >= 3, "l'historique de chaque geste");
    mem::Json j;
    check(mem::parseJson(readAll(x->file), j) && j.num("format") == 2 && j.flag("epingle") && j.str("statut") == "hypothese" && j.str("portee") == "utilisateur",
          "le fichier au format 2");
    check(!again.setPinned(mem::kKindsKnowledge, true, &why), "une integree ne s'epingle pas");
}

// -------------------------------------------------------------- 6. l'ecrivain << assistant >> ---
void writer() {
    std::printf("== 6. l'Assistant propose, l'utilisateur valide ==\n");
    const auto p = makeProjects();
    mem::Registry reg;
    reg.load();
    const mem::Context ctx = mem::contextFor(p.a, "c-20");
    mem::MemoryProposal prop;
    prop.content = "PROPOSE_1 : les alarmes de niveau sont en priorite 2.";
    prop.scope = "projet";
    prop.conversation = "c-20";
    prop.message = "m5";
    prop.confidence = 0.7;
    const auto r = mem::proposeMemory(reg, prop, ctx);
    check(r.kind == mem::RememberResult::Kind::Added, "une proposition de l'Assistant");
    const auto* k = reg.find(r.id);
    check(k && k->status == "provisoire" && k->source == "assistant" && k->projectId == p.a.id && k->conversation == "c-20" && k->message == "m5",
          "provisoire, source Assistant, liee au projet et a son message");
    check(k && mem::hereState(*k, ctx) == mem::Here::Pending && reg.pendingHere(ctx).size() == 1, "a valider : elle ne part pas");
    hmi::Project project;
    project.views = {hmi::makeView(project, "Vue")};
    ai::PromptInput in;
    in.project = &project;
    in.view = &project.views[0];
    in.projectName = p.a.name;
    in.projectId = p.a.id;
    in.knowledge = &reg;
    check(!has(ai::systemPrompt(in), "PROPOSE_1"), "pas dans la demande avant validation");
    std::string why;
    check(mem::validateMemory(reg, r.id, "confirme", &why) && reg.find(r.id)->status == "confirme", "validee");
    check(has(ai::systemPrompt(in), "PROPOSE_1"), "dans la demande apres validation");
    check(!mem::validateMemory(reg, r.id, "provisoire", &why), "valider comme provisoire : refuse");
    // une hypothese et un epingle : dits comme tels, l'epingle d'abord
    mem::MemoryProposal h;
    h.content = "HYPO_1 : la cuve 2 deborde peut-etre au-dela de 95 %.";
    h.scope = "projet";
    const auto rh = mem::proposeMemory(reg, h, ctx);
    check(mem::validateMemory(reg, rh.id, "hypothese", &why), "gardee comme hypothese");
    mem::MemoryProposal pin;
    pin.content = "EPINGLE_1 : jamais de rouge hors defaut.";
    pin.scope = "utilisateur";
    const auto rp = mem::proposeMemory(reg, pin, ctx);
    check(mem::validateMemory(reg, rp.id) && reg.setPinned(rp.id, true), "une autre, validee et epinglee");
    const std::string s = ai::systemPrompt(in);
    check(has(s, "HYPOTH\xC3\x88SE non confirm\xC3\xA9" "e") && s.find("EPINGLE_1") < s.find("PROPOSE_1"), "l'hypothese dite ; l'epingle en premier");
    check(has(s, "r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9"), "les consignes disent que le modele n'est pas reentraine");
    // refuser
    mem::MemoryProposal bad;
    bad.content = "REFUSE_1 : a oublier.";
    bad.scope = "technique";
    const auto rb = mem::proposeMemory(reg, bad, ctx);
    check(mem::rejectMemory(reg, rb.id, &why) && !reg.find(rb.id), "refusee : supprimee");
    // sans projet enregistre : pas de souvenir de projet
    mem::Context none;
    check(mem::proposeMemory(reg, prop, none).kind == mem::RememberResult::Kind::Refused, "une proposition de projet sans projet : refusee");
}

// ------------------------------------------------------ 7. la migration, sauvegarder, restaurer ---
void migrationBackup() {
    std::printf("== 7. la migration du format 1, sauvegarder, restaurer ==\n");
    // un fichier de la 1.20 (format 1), avec un champ inconnu
    const std::string old = "{\n  \"format\": 1,\n  \"id\": \"k-20261001-080000-1\",\n  \"nom\": \"Ancienne preference\",\n  \"description\": \"\",\n"
                            "  \"categorie\": \"preferences\",\n  \"source\": \"utilisateur\",\n  \"projet\": \"\",\n  \"actif\": true,\n"
                            "  \"ajoute\": \"2026-10-01T08:00:00\",\n  \"modifie\": \"2026-10-01T08:00:00\",\n  \"contenu\": \"OLD_1 : du vert pour marche.\",\n"
                            "  \"etiquette\": \"venue de la 1.20\"\n}\n";
    const fs::path file = fs::path(ai::knowledgeDir()) / "k-20261001-080000-1.json";
    writeText(file, old);
    const std::size_t backupsBefore = mem::listBackups().size();
    mem::Registry reg;
    reg.load();
    const auto* k = reg.find("k-20261001-080000-1");
    check(k && k->status == "confirme" && k->scopeKey() == "utilisateur" && k->format == 2, "relue : confirmee, portee utilisateur (sa categorie)");
    check(!reg.migration().empty() && has(reg.migration(), "1 souvenir"), "la migration est dite : " + reg.migration());
    mem::Json j;
    check(mem::parseJson(readAll(file), j) && j.num("format") == 2 && j.str("statut") == "confirme" && j.str("etiquette") == "venue de la 1.20",
          "le fichier reecrit au format 2, le champ inconnu garde");
    const auto backups = mem::listBackups();
    check(backups.size() == backupsBefore + 1 && has(backups.front().reason, "migration"), "une sauvegarde AVANT la migration");
    check(!backups.empty() && readAll(fs::path(backups.front().folder) / "connaissances" / "k-20261001-080000-1.json") == old,
          "la sauvegarde garde le fichier d'origine, octet pour octet");
    mem::Registry again;
    again.load();
    check(again.migration().empty() && mem::listBackups().size() == backupsBefore + 1, "relue de nouveau : rien a migrer, pas d'autre sauvegarde");

    // SAUVEGARDER, puis RESTAURER
    std::string name, why;
    check(mem::backupMemory("essai", &name, &why) && !name.empty(), "sauvegarder : " + name + why);
    mem::Knowledge extra;
    extra.scope = "technique";
    extra.content = "APRES_SAUVEGARDE : a perdre a la restauration.";
    const auto rx = again.remember(extra);
    check(again.remove("k-20261001-080000-1"), "une suppression apres la sauvegarde");
    const std::size_t n = mem::listBackups().size();
    check(mem::restoreBackup(name, &why), "restaurer : " + why);
    mem::Registry restored;
    restored.load();
    check(restored.find("k-20261001-080000-1") && !restored.find(rx.id), "l'etat de la sauvegarde : l'ancienne revenue, la nouvelle partie");
    check(mem::listBackups().size() == n + 1 && has(mem::listBackups().front().reason, "avant restauration"), "l'etat d'avant sauvegarde lui aussi");
    check(!mem::restoreBackup("../connaissances", &why) && !mem::restoreBackup("memoire-inexistante", &why), "un nom de sauvegarde invalide : refuse");
    // les dix plus recentes restent
    for (int i = 0; i < 12; ++i) (void)mem::backupMemory("rotation");
    check(mem::listBackups().size() == mem::kKeepBackups, "les sauvegardes : dix au plus");
}

// ------------------------------------------------- 8. la recherche dans les anciennes conversations ---
ai::Conversation conv(const std::string& id, const std::string& title, const std::string& project, const std::string& date,
                      std::vector<std::pair<std::string, std::string>> msgs) {
    ai::Conversation c;
    c.id = id;
    c.title = title;
    c.created = date + "T09:00:00";
    c.modified = date + "T09:30:00";
    c.project.name = project;
    int n = 0;
    for (auto& [role, text] : msgs) {
        ai::Message m;
        m.id = "m" + std::to_string(++n);
        m.role = role;
        m.date = date + "T09:" + (n < 10 ? "0" : "") + std::to_string(n) + ":00";
        m.text = text;
        c.messages.push_back(std::move(m));
    }
    return c;
}

void conversationSearch() {
    std::printf("== 8. retrouver les anciennes conversations ==\n");
    const std::string U = "utilisateur", A = "assistant";
    auto c1 = conv("c-20261020-090000-1", "Calcul des temps de la DFB", "Station", "2026-10-20",
                   {{U, "Le calcul des temps dans ma DFB_Temps donne une erreur : le TON ne repart pas."},
                    {A, "La cause : IN reste a TRUE. La correction en ST :\n```st\nTON_1(IN := Marche AND NOT TON_1.Q, PT := T#5s);\n```"},
                    {U, "Merci, ca marche."}});
    auto c2 = conv("c-20261025-090000-1", "Pompes de la station", "Station", "2026-10-25",
                   {{U, "Ajoute la pompe P101 et la vanne V-201 sur le synoptique."}, {A, "Voici 2 objets : une Pompe et une Vanne."}});
    auto c3 = conv("c-20261028-090000-1", "Reprise du calcul des temps", "Station", "2026-10-28",
                   {{U, "Reprends le probleme du calcul des temps de DFB_Temps : il y a encore une erreur au demarrage."},
                    {A, "Il suffit d'initialiser TON_1 au premier cycle."}});
    auto c4 = conv("c-20261029-090000-1", "Traitement de l'eau", "Eau", "2026-10-29", {{U, "Une jauge pour le niveau de la cuve."}});
    for (const auto* c : {&c1, &c2, &c3, &c4}) check(ai::saveConversation(*c), "enregistree : " + c->title);
    std::size_t re = 0;
    std::vector<ai::Conversation> all;
    ci::Index idx = ci::refreshIndex(&re, &all);
    check(idx.entries.size() == 4 && re == 4 && fs::exists(ci::indexFile()), "l'index : 4 conversations, ecrit dans index/");
    const auto entry = [&](const std::string& id) -> const ci::Entry* {
        for (const auto& e : idx.entries)
            if (e.id == id) return &e;
        return nullptr;
    };
    const auto* e1 = entry(c1.id);
    const auto contains = [](const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); };
    check(e1 && contains(e1->objects, "DFB_Temps") && contains(e1->objects, "TON_1"), "les objets cites (DFB_Temps, TON_1)");
    check(e1 && contains(e1->languages, "ST") && !e1->problems.empty() && !e1->solutions.empty(), "le langage ST, un probleme, une solution");
    const auto* e2 = entry(c2.id);
    check(e2 && contains(e2->equipment, "P101") && contains(e2->equipment, "V-201") && contains(e2->equipment, "pompe") && contains(e2->equipment, "vanne"),
          "les equipements (P101, V-201, pompe, vanne)");
    const auto* e4 = entry(c4.id);
    check(e4 && contains(e4->types, "Gauge"), "un genre d'objet IHM (Jauge -> Gauge)");
    // par mots-cles : plusieurs conversations, une chronologie
    ci::Query q;
    q.text = "calcul temps DFB";
    auto r = ci::search(idx, q, all);
    check(r.matches.size() == 2, "par mots-cles : les deux conversations du calcul des temps (" + std::to_string(r.matches.size()) + ")");
    check(r.chronology.size() >= 2 && std::is_sorted(r.chronology.begin(), r.chronology.end(),
                                                      [](const ci::Excerpt& a, const ci::Excerpt& b) { return a.date < b.date; }),
          "la chronologie, du plus ancien au plus recent");
    const std::string text = ci::resultText(r, q);
    check(has(text, "Calcul des temps de la DFB") && has(text, "Reprise du calcul des temps") && has(text, "Chronologie"), "le texte : les deux et la chronologie");
    // par objet, equipement, langage, projet, dates, probleme, solution
    ci::Query qo;
    qo.object = "DFB_Temps";
    check(ci::search(idx, qo, all).matches.size() == 2, "par objet (DFB_Temps)");
    ci::Query qe;
    qe.equipment = "P101";
    r = ci::search(idx, qe, all);
    check(r.matches.size() == 1 && r.matches[0].id == c2.id, "par equipement (P101)");
    ci::Query ql;
    ql.language = "st";
    r = ci::search(idx, ql, all);
    check(r.matches.size() == 1 && r.matches[0].id == c1.id, "par langage (ST)");
    ci::Query qp;
    qp.project = "Eau";
    r = ci::search(idx, qp, all);
    check(r.matches.size() == 1 && r.matches[0].id == c4.id, "par projet (Eau)");
    ci::Query qd;
    qd.from = "2026-10-26";
    qd.to = "2026-10-28";
    r = ci::search(idx, qd, all);
    check(r.matches.size() == 1 && r.matches[0].id == c3.id, "par dates (du 26 au 28 octobre)");
    ci::Query qk;
    qk.kind = ci::Query::Kind::Problem;
    qk.text = "erreur";
    r = ci::search(idx, qk, all);
    check(r.matches.size() == 2 && !r.matches[0].excerpts.empty(), "par probleme rencontre (erreur)");
    ci::Query qs;
    qs.kind = ci::Query::Kind::Solution;
    qs.object = "TON_1";
    r = ci::search(idx, qs, all);
    check(!r.matches.empty() && has(ci::resultText(r, qs), "solution"), "par solution apportee");
    ci::Query qx;
    qx.text = "calcul temps";
    qx.excludeConversation = c3.id;
    r = ci::search(idx, qx, all);
    check(r.matches.size() == 1 && r.matches[0].id == c1.id, "la conversation en cours n'est pas retrouvee elle-meme");
    ci::Query none;
    none.text = "introuvablezz";
    r = ci::search(idx, none, all);
    check(r.matches.empty() && has(ci::resultText(r, none), "n'invente pas"), "rien : le dire, ne rien inventer");
    // l'index se refait par morceaux
    c2.messages.push_back(c2.messages.front());
    c2.messages.back().id = "m9";
    c2.modified = "2026-10-25T10:00:00";
    check(ai::saveConversation(c2), "une conversation modifiee");
    idx = ci::refreshIndex(&re, &all);
    check(re == 1 && idx.entries.size() == 4, "relue seule (1 sur 4)");
    check(ci::loadIndex().entries.size() == 4, "l'index relu du disque");
    // sur son fil
    ci::Indexer indexer;
    ci::Query qt;
    qt.text = "pompe";
    const auto ticket = indexer.search(qt);
    check(indexer.waitIdle(10000), "le fil d'indexation finit");
    const auto events = indexer.poll();
    check(events.size() == 1 && events[0].ticket == ticket && events[0].type == ci::Indexer::Event::Type::Searched && !events[0].result.matches.empty(),
          "la recherche sur le fil rend son resultat a poll()");
    indexer.cancel();
    (void)indexer.rebuild();
    check(indexer.waitIdle(10000), "une reconstruction sur le fil");
}

// ------------------------------------------------------------------------ 9. les outils ---
void tools() {
    std::printf("== 9. les outils du modele ==\n");
    const auto p = makeProjects();
    const mem::Context ctx = mem::contextFor(p.c, "c-30");
    mem::Registry reg;
    reg.load();
    check(has(mem::rememberToolJson(), "\"name\": \"proposer_souvenir\"") && has(mem::searchMemoryToolJson(), "chercher_souvenirs")
              && has(ci::searchConversationsToolJson(), "chercher_conversations"),
          "les definitions des trois outils");
    bool err = true;
    std::string out = mem::runRememberTool(reg, "{\"contenu\": \"OUTIL_1 : les vues font 1920 x 1080.\", \"portee\": \"projet\", \"titre\": \"Taille des vues\"}",
                                           ctx, "c-30", "m2", &err);
    check(!err && has(out, "provisoire") && has(out, "PROPOS"), "proposer : enregistre, provisoire, et dit (" + out.substr(0, 60) + ")");
    std::string id;
    for (const auto& k : reg.all())
        if (k.content.rfind("OUTIL_1", 0) == 0) id = k.id;
    check(!id.empty() && reg.find(id)->projectId == p.c.id && reg.find(id)->message == "m2", "lie au projet ouvert et a son message");
    out = mem::runRememberTool(reg, "{\"contenu\": \"outil_1 : les vues font 1920 x 1080\", \"portee\": \"projet\", \"titre\": \"Taille des vues\"}", ctx, "c-30", "m3", &err);
    check(!err && has(out, "D\xC3\xA9j\xC3\xA0"), "un doublon : deja dans la memoire");
    out = mem::runRememberTool(reg, "{\"contenu\": \"Les vues font 1280 x 800.\", \"portee\": \"projet\", \"titre\": \"Taille des vues\"}", ctx, "c-30", "m4", &err);
    check(!err && has(out, "CONTREDIT") && has(out, "OUTIL_1"), "une contradiction : dite au modele, avec l'ancienne");
    out = mem::runRememberTool(reg, "{\"contenu\": \"x\", \"portee\": \"bidule\"}", ctx, "c-30", "m5", &err);
    check(err, "une portee inconnue : une erreur");
    out = mem::runRememberTool(reg, "pas du json", ctx, "c-30", "m5", &err);
    check(err && has(out, "illisible"), "une entree illisible");
    (void)mem::validateMemory(reg, id);
    out = mem::runSearchMemoryTool(reg, "{\"requete\": \"vues\"}", ctx, &err);
    check(!err && has(out, "OUTIL_1") && has(out, "EN CONFLIT") && has(out, "propos\xC3\xA9, pas encore valid\xC3\xA9"), "chercher : trouve, le conflit et la proposition dits");
    const mem::Context elsewhere = mem::contextFor(p.a);
    out = mem::runSearchMemoryTool(reg, "{\"requete\": \"OUTIL_1\"}", elsewhere, &err);
    check(has(out, "Aucun souvenir") && !has(out, "1920"), "depuis un autre projet : rien (jamais melanges)");
    out = mem::runSearchMemoryTool(reg, "{\"requete\": \"OUTIL_1\", \"autres_projets\": true}", elsewhere, &err);
    check(has(out, "1920") && has(out, "projet Eau"), "a la demande : les autres projets, leur projet dit");
    out = ci::runSearchConversationsTool("{\"mots\": \"calcul temps\", \"objet\": \"DFB_Temps\"}", "", &err);
    check(!err && has(out, "Calcul des temps de la DFB"), "chercher_conversations : retrouve");
    out = ci::runSearchConversationsTool("{\"mots\": \"zzzintrouvable\"}", "", &err);
    check(!err && has(out, "Aucune conversation"), "chercher_conversations : rien, et le dire");
}

// ------------------------------------------------------------------------- 10. la fenetre ---
class Recorder final : public gfx::IRenderer {
public:
    gfx::Size size{1920.f, 1200.f};
    gfx::Rect panel{};
    std::vector<std::string> outside, all;
    std::vector<gfx::Rect> clips;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
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
        float x0 = o.x, y0 = o.y, x1 = o.x + measure(t, f).width, y1 = o.y + static_cast<float>(f.v);
        if (!clips.empty()) {
            const auto& c = clips.back();
            x0 = std::max(x0, c.x);
            y0 = std::max(y0, c.y);
            x1 = std::min(x1, c.right());
            y1 = std::min(y1, c.bottom());
            if (x1 <= x0 || y1 <= y0) return;
        }
        all.emplace_back(t);
        if (panel.w > 0.f && (x0 < panel.x - 0.5f || x1 > panel.right() + 1.f || y0 < panel.y - 0.5f || y1 > panel.bottom() + 1.f)) outside.emplace_back(t);
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

void paint(app::AiMemoryDialog& d, Recorder& rec, const ui::Theme& theme, const std::string& label) {
    ui::Widget* root = d.widgetRoot();
    root->setBounds({0.f, 0.f, rec.size.w, rec.size.h});
    root->layout();
    const float w = std::min(1240.f, rec.size.w - 40.f), h = std::min(800.f, rec.size.h - 40.f);
    rec.panel = {std::floor((rec.size.w - w) * 0.5f), std::floor((rec.size.h - h) * 0.5f), w, h};
    d.showTab(app::AiMemoryDialog::Tab::Souvenirs);
    root->layout();
    rec.outside.clear();
    rec.all.clear();
    std::vector<ui::Widget*> overlays;
    const ui::PaintContext ctx{rec, theme, {0.f, 0.f, rec.size.w, rec.size.h}, 0.0, &overlays};
    root->invalidate();
    root->render(ctx);
    check(rec.all.size() > 10, label + " : l'onglet Souvenirs se dessine (" + std::to_string(rec.all.size()) + " textes)");
    check(rec.outside.empty(), label + " : aucun texte hors de la fenetre" + (rec.outside.empty() ? std::string{} : " : " + rec.outside.front()));
    bool honest = false;
    for (const auto& t : rec.all) honest = honest || t == std::string(mem::kNotRetrained);
    check(honest, label + " : la phrase honnete, en entier (" + std::string(mem::kNotRetrained) + ")");
}

void dialog() {
    std::printf("== 10. la fenetre : l'onglet Souvenirs ==\n");
    const auto p = makeProjects();
    {
        // un souvenir de A en conflit, une proposition a valider
        mem::Registry reg;
        reg.load();
        const mem::Context ctx = mem::contextFor(p.a, "c-40");
        mem::MemoryProposal x;
        x.scope = "projet";
        x.name = "Seuil haut";
        x.content = "FEN_1 : le seuil haut est a 90 %.";
        (void)mem::validateMemory(reg, mem::proposeMemory(reg, x, ctx).id);
        x.content = "FEN_2 : le seuil haut est a 95 %.";
        (void)mem::validateMemory(reg, mem::proposeMemory(reg, x, ctx).id);
        mem::MemoryProposal q;
        q.scope = "conversation";
        q.content = "FEN_3 : reste a faire la vue des tendances.";
        (void)mem::proposeMemory(reg, q, ctx);
    }
    app::AiMemoryDialog::Spec spec;
    spec.project = p.a.name;
    spec.projectId = p.a.id;
    spec.projectAmbiguous = p.a.ambiguous;
    spec.conversationId = "c-40";
    app::AiMemoryDialog d(std::move(spec));
    check(static_cast<bool>(d.Initialize()), "la fenetre se construit");
    check(d.showTab("souvenirs") && d.tab() == app::AiMemoryDialog::Tab::Souvenirs, "l'onglet Souvenirs, par son nom");
    d.setSouvenirFilter({}, {}, "ici");
    const auto shown = d.shownSouvenirs();
    check(shown.size() >= 3, "ce projet : ses souvenirs (" + std::to_string(shown.size()) + ")");
    check(!shown.empty() && shown.front()->conflicted(), "les conflits d'abord");
    d.setSouvenirFilter({}, "conflits", "ici");
    check(d.shownSouvenirs().size() == 2, "le filtre << En conflit >>");
    d.setSouvenirFilter({}, "provisoire", "ici");
    check(d.shownSouvenirs().size() == 1 && d.selectSouvenir(d.shownSouvenirs().front()->id), "les propositions a valider");
    check(d.rejectSelected() && d.shownSouvenirs().empty(), "refuser une proposition");
    d.setSouvenirFilter({}, {}, {});
    d.setSouvenirSearch("FEN_2");
    check(d.shownSouvenirs().size() == 1 && d.selectSouvenir(d.shownSouvenirs().front()->id), "la recherche dans les souvenirs");
    check(d.pinSelected(true) && d.selectedSouvenir() && d.selectedSouvenir()->pinned, "epingler");
    check(d.keepSelectedInConflict() && d.selectedSouvenir() && !d.selectedSouvenir()->conflicted(), "garder celui-ci : le conflit resolu");
    check(d.setSelectedStatus("hypothese") && d.selectedSouvenir()->status == "hypothese", "garder comme hypothese");
    check(d.associateSelectedAll() && d.selectedSouvenir()->projectId.empty(), "associer a tous les projets");
    check(d.associateSelectedHere() && d.selectedSouvenir()->projectId == p.a.id, "associer au projet ouvert");
    check(d.editSelectedSouvenir("Seuil haut (corrige)", "FEN_2 : le seuil haut est a 96 %.") && d.selectedSouvenir()->content == "FEN_2 : le seuil haut est a 96 %.",
          "modifier");
    check(d.backupNow() && !d.backups().empty(), "sauvegarder maintenant");
    check(d.deleteSelectedSouvenir() && d.confirming(), "supprimer : une confirmation d'abord");
    check(d.confirmYes() && d.shownSouvenirs().empty(), "supprime");
    check(d.restoreLatestBackup() && d.confirming(), "restaurer : une confirmation d'abord");
    check(d.confirmYes() && d.shownSouvenirs().size() == 1, "restaure : le souvenir revenu");
    d.setSouvenirSearch({});
    check(d.selectSouvenir(d.shownSouvenirs().front()->id), "un souvenir choisi pour le dessin");
    Recorder big;
    paint(d, big, ui::Theme::dark(), "1920 x 1200");
    Recorder small;
    small.size = {1280.f, 800.f};
    paint(d, small, ui::Theme::light(), "1280 x 800");
    d.beginSouvenirEdit();
    check(d.souvenirEditing(), "l'editeur du souvenir");
    Recorder edit;
    paint(d, edit, ui::Theme::dark(), "l'editeur ouvert");
    d.closeSouvenirEdit();
}

} // namespace

int main() {
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    fs::create_directories(scratch(), ec);
    ai::setRoot((scratch() / "IA").string());
    scopes();
    projectsAndIsolation();
    conflictsAndDuplicates();
    pinStatus();
    writer();
    migrationBackup();
    conversationSearch();
    tools();
    dialog();
    if (!std::getenv("AI_SOUVENIRS_GARDER")) fs::remove_all(scratch(), ec);
    else std::printf("garde : %s\n", scratch().string().c_str());
    std::printf("%d verification(s), %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
