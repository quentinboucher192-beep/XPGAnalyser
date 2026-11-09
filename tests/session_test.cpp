// =============================================================================
//  tests/session_test.cpp - 1.43.0 : les sessions (plusieurs PC sur un projet)
// -----------------------------------------------------------------------------
//    - le protocole : l'annonce (aller-retour, champs refuses), le code (affiche,
//      tape), les initiales, les couleurs, les blocs d'identifiants ;
//    - les differences de lignes : un ajout, un retrait, deux changements loin l'un
//      de l'autre appliques sur un texte qui a bouge, un conflit refuse ;
//    - le miroir : deux PC (deux projets) qui s'envoient leurs changements objet par
//      objet (deux objets de la meme vue en meme temps : aucun ne s'ecrase), une vue
//      neuve, une vue supprimee, deux variables ajoutees en meme temps des deux
//      cotes, les empreintes egales a la fin ; l'encodage en trames ;
//    - le lien : un vrai hote et un vrai equipier sur la boucle locale (TLS 1.3) : la
//      decouverte, l'entree sans code, le bon code, un code faux (3 essais), une
//      empreinte qui ne correspond pas, des trames dans les deux sens, l'arret.
//    - 1.45.0 : le journal (un objet deplace, ajoute, modifie, supprime ; une
//      ressource ajoutee par un equipier qui arrive chez l'hote et chez les autres ;
//      une variable ; la fusion d'un glisser ; la borne ; le CSV enregistre avec le
//      projet) ; le dossier des copies des equipiers (effacee au depart, purgee au
//      lancement) ; de vrais liens : l'hote retire un equipier (sa raison, les
//      autres le savent, il revient avec la meme empreinte), un equipier part
//      (l'hote le voit), l'hote arrete (tous coupes) ; le service trop vieux (404
//      << route >> : la phrase qui dit de le redeployer), a l'ouverture et a l'entree.
// =============================================================================
#include "../src/session/SessionJournal.hpp"   // 1.45.0
#include "../src/session/SessionLink.hpp"
#include "../src/session/SessionProtocol.hpp"
#include "../src/session/SessionRelay.hpp"     // 1.45.0 : le service trop vieux
#include "../src/session/SessionSync.hpp"

#include "../src/deploy/Crypto.hpp"
#include "../src/deploy/TlsIdentity.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (std::getenv("SESSION_VERBOSE")) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

using namespace session;
using hmi::Id;

// ---------------------------------------------------------------- un projet ---
hmi::Object rect(Id id, const std::string& name, Id layer, int x) {
    hmi::Object o;
    o.id = id;
    o.kind = hmi::Kind::Rectangle;
    o.name = name;
    o.layer = layer;
    o.props = {{"x", std::to_string(x), ""}, {"y", "10", ""}, {"w", "100", ""}, {"h", "40", ""}, {"fill", "#336699", ""}};
    return o;
}

hmi::Project sample() {
    hmi::Project p;
    p.nextId = 100;
    for (int k = 0; k < 2; ++k) {
        hmi::View v;
        v.id = static_cast<Id>(10 + k);
        v.name = k == 0 ? "Synoptique" : "Alarmes";
        v.layers.push_back({static_cast<Id>(20 + k), "Fond", true, false});
        v.activeLayer = v.layers.front().id;
        for (int i = 0; i < 4; ++i) v.objects.push_back(rect(static_cast<Id>(30 + k * 10 + i), "Objet_" + std::to_string(i), v.activeLayer, 10 * i));
        p.views.push_back(v);
    }
    hmi::Variable a;
    a.id = 60;
    a.name = "Niveau";
    a.type = "REAL";
    p.programs.variables.push_back(a);
    return p;
}

hmi::Object* obj(hmi::Project& p, Id view, Id id) {
    auto* v = p.view(view);
    return v ? v->object(id) : nullptr;
}

std::string propOf(const hmi::Object* o, const std::string& key) {
    if (!o) return "?";
    for (const auto& pr : o->props)
        if (pr.key == key) return pr.value;
    return "";
}

void setProp(hmi::Object* o, const std::string& key, const std::string& value) {
    for (auto& pr : o->props)
        if (pr.key == key) { pr.value = value; return; }
    o->props.push_back({key, value, ""});
}

// Ce que A a change part chez B (en trames, comme sur le reseau).
void ship(Mirror& from, hmi::Project& pf, Mirror& to, hmi::Project& pt, std::set<Id> touched, bool project, const std::string& what) {
    std::vector<ViewDelta> views;
    ProjectDelta pd;
    from.collect(pf, touched, project, views, pd);
    for (const auto& d : views) {
        core::json::Value h;
        std::string data;
        encodeView(d, h, data);
        core::json::Value h2;
        check(core::json::parse(core::json::write(h, false), h2, nullptr), what + " : l'en-tete VUE se relit");
        ViewDelta back;
        std::string why;
        check(decodeView(h2, data, back, &why), what + " : VUE se relit (" + why + ")");
        check(to.applyView(pt, back, &why), what + " : VUE s'applique (" + why + ")");
    }
    if (!pd.empty()) {
        core::json::Value h;
        std::string data;
        encodeProject(pd, h, data);
        core::json::Value h2;
        check(core::json::parse(core::json::write(h, false), h2, nullptr), what + " : l'en-tete FICHIERS se relit");
        ProjectDelta back;
        std::string why;
        check(decodeProject(h2, data, back, &why), what + " : FICHIERS se relit (" + why + ")");
        ProjectApplied done;
        check(to.applyProject(pt, back, done, &why), what + " : FICHIERS s'applique (" + why + ")");
        check(done.conflicts.empty(), what + " : sans conflit");
    }
}

bool sameShared(Mirror& a, hmi::Project& pa, Mirror& b, hmi::Project& pb) { return a.hashes(pa) == b.hashes(pb); }

// ------------------------------------------------------------------- essais ---
void protocole() {
    std::printf("-- le protocole\n");
    Announce a;
    a.session = "Mise en service Armoire_Gaz";
    a.host = "Camille";
    a.machine = "PC-BUREAU-01";
    a.machineId = "M-7F21";
    a.app = "IHM Studio";
    a.version = "1.43.0";
    a.project = "Armoire_Gaz";
    a.fingerprint = std::string(64, 'a');
    a.sessionId = "0123456789abcdef";
    a.port = 47863;
    a.members = 3;
    a.needsCode = true;
    const std::string m = hereMessage("n1", a);
    std::string nonce;
    Announce b;
    check(parseHere(m, nonce, b) && nonce == "n1", "ICI se relit");
    check(b.session == a.session && b.host == "Camille" && b.machineId == "M-7F21" && b.project == "Armoire_Gaz" && b.members == 3
              && b.needsCode && b.port == 47863 && b.fingerprint == a.fingerprint && b.sessionId == a.sessionId,
          "ICI : tous les champs");
    Announce bad = a;
    bad.fingerprint = "zz";
    check(!parseHere(hereMessage("n1", bad), nonce, b), "ICI sans empreinte valable : refuse");
    bad = a;
    bad.sessionId = "court";
    check(!parseHere(hereMessage("n1", bad), nonce, b), "ICI sans identifiant de session : refuse");
    std::string n2;
    check(parseSearch(searchMessage("abc"), n2) && n2 == "abc", "CHERCHE se relit");
    check(!parseSearch("{\"xpgd\":1,\"op\":\"DISCOVER\",\"nonce\":\"x\"}", n2), "la decouverte du deploiement n'est pas une recherche de session");

    const std::string code = newCode();
    check(code.size() == 6 && codeTyped(code) == code, "le code : 6 chiffres");
    check(codeShown("482913") == "482 913", "le code affiche en deux groupes");
    check(codeTyped(" 482-913 ") == "482913", "le code tape avec un tiret ou des espaces");
    check(initials("L\xC3\xA9" "a Martin") == "LM" && initials("camille") == "C" && initials("") == "?", "les initiales");
    std::vector<Member> ms(2);
    ms[0].color = palette()[0];
    ms[1].color = palette()[1];
    check(freeColor(ms) == palette()[2], "la premiere couleur libre");
    check(freeColor(ms, palette()[5]) == palette()[5], "la couleur voulue si elle est libre");
    check(freeColor(ms, palette()[1]) == palette()[2], "pas celle d'un autre");

    Member mem;
    mem.number = 2;
    mem.name = "L\xC3\xA9" "a";
    mem.machineId = "M-1A2B";
    mem.color = palette()[1];
    mem.role = Role::Watch;
    mem.view = 10;
    mem.selection = {31, 32};
    Member back;
    check(fromJson(toJson(mem), back) && back.name == mem.name && back.role == Role::Watch && back.selection == mem.selection && back.view == 10,
          "un membre en JSON (aller-retour)");

    hmi::Project p = sample();
    check(blockStart(0) == 1 && blockStart(1) == kIdBlock && blockStart(3) == 3 * kIdBlock, "les blocs : n x 2^28");
    check(firstFreeInBlock(p, 1) == kIdBlock, "un bloc vide commence a son debut");
    p.views[0].objects.push_back(rect(kIdBlock + 41, "Venu_de_Lea", p.views[0].activeLayer, 5));
    check(firstFreeInBlock(p, 1) == kIdBlock + 42, "une session d'avant y a laisse un objet : le suivant");
    check(firstFreeInBlock(p, 2) == 2 * kIdBlock, "le bloc d'un autre membre n'en est pas touche");
}

void lignes() {
    std::printf("-- les differences de lignes\n");
    const std::string base = "a\nb\nc\nd\ne\nf\ng\nh\n";
    auto h = diffLines(base, "a\nb\nc\nX\ne\nf\ng\nh\nY\n");
    std::string t = base;
    check(patchLines(t, h) && t == "a\nb\nc\nX\ne\nf\ng\nh\nY\n", "un changement et un ajout a la fin");
    // Le meme changement sur un texte qui a bouge (deux lignes ajoutees en tete) : il trouve sa place.
    std::string moved = "0\n1\na\nb\nc\nd\ne\nf\ng\nh\n";
    check(patchLines(moved, h) && moved == "0\n1\na\nb\nc\nX\ne\nf\ng\nh\nY\n", "sur un texte qui a bouge");
    // Deux ajouts au meme endroit (deux equipiers, une variable chacun) : les deux restent.
    const std::string vars = "debut\nvariable id=1\nfin\n";
    const auto hA = diffLines(vars, "debut\nvariable id=1\nvariable id=2\nfin\n");
    const auto hB = diffLines(vars, "debut\nvariable id=1\nvariable id=3\nfin\n");
    std::string atHost = vars;
    check(patchLines(atHost, hA) && patchLines(atHost, hB), "deux ajouts au meme endroit");
    check(atHost.find("id=2") != std::string::npos && atHost.find("id=3") != std::string::npos, "les deux ajouts sont la");
    // Un conflit : la ligne que le morceau retire a change.
    std::string other = "a\nb\nc\nZ\ne\nf\ng\nh\n";
    const std::string before = other;
    check(!patchLines(other, diffLines(base, "a\nb\nc\nX\ne\nf\ng\nh\n")) && other == before, "un conflit : refuse, rien ne change");
    check(diffLines(base, base).empty(), "deux textes egaux : rien");
    std::string empty;
    check(patchLines(empty, diffLines("", "x\ny\n")) && empty == "x\ny\n", "depuis un texte vide");
    std::string crlf = "a\r\nb\r\n";
    check(patchLines(crlf, diffLines("a\r\nb\r\n", "a\r\nc\r\n")) && crlf == "a\r\nc\r\n", "les fins de ligne Windows gardees");
}

void miroir() {
    std::printf("-- le miroir : deux PC, un projet\n");
    hmi::Project host = sample(), lea = sample();
    Mirror mh, ml;
    mh.reset(host);
    ml.reset(lea);
    check(sameShared(mh, host, ml, lea), "au depart : les memes empreintes");

    // L'hote deplace Objet_0, Lea change la couleur d'Objet_2 : dans la meme vue, en meme temps.
    setProp(obj(host, 10, 30), "x", "500");
    setProp(obj(lea, 10, 32), "fill", "#FF0000");
    std::vector<ViewDelta> fromHost, fromLea;
    ProjectDelta ph, pl;
    mh.collect(host, {10}, false, fromHost, ph);
    ml.collect(lea, {10}, false, fromLea, pl);
    check(fromHost.size() == 1 && fromHost[0].objects.size() == 1 && fromHost[0].objects[0].id == 30, "l'hote : un objet change");
    check(fromLea.size() == 1 && fromLea[0].objects.size() == 1 && fromLea[0].objects[0].id == 32, "Lea : un objet change");
    check(!fromHost[0].orderChanged && !fromHost[0].shellChanged, "ni l'ordre ni la vue");
    std::string why;
    // Les deux se croisent : chacun applique celui de l'autre.
    check(ml.applyView(lea, fromHost[0], &why), "Lea applique celui de l'hote");
    check(mh.applyView(host, fromLea[0], &why), "l'hote applique celui de Lea");
    check(propOf(obj(lea, 10, 30), "x") == "500" && propOf(obj(lea, 10, 32), "fill") == "#FF0000", "Lea : les deux changements");
    check(propOf(obj(host, 10, 30), "x") == "500" && propOf(obj(host, 10, 32), "fill") == "#FF0000", "l'hote : les deux changements");
    check(sameShared(mh, host, ml, lea), "les memes empreintes apres le croisement");
    // Rien de plus a envoyer : le miroir a pris les changements recus.
    fromHost.clear();
    ph = ProjectDelta{};
    mh.collect(host, {10, 11}, true, fromHost, ph);
    check(fromHost.empty() && ph.empty(), "un changement recu ne repart pas");

    // Lea cree un objet (dans son bloc) au-dessus, en supprime un, et passe un objet devant.
    {
        auto* v = lea.view(10);
        v->objects.push_back(rect(kIdBlock + 1, "Neuf_de_Lea", v->activeLayer, 77));
        v->objects.erase(v->objects.begin() + 1);   // Objet_1 (31)
        std::swap(v->objects[0], v->objects[1]);
    }
    ship(ml, lea, mh, host, {10}, false, "creer, supprimer, reordonner");
    {
        const auto* v = host.view(10);
        check(v->objects.size() == 4 && v->object(31) == nullptr && v->object(kIdBlock + 1) != nullptr, "l'hote : cree et supprime");
        check(v->objects[0].id == 32 && v->objects[1].id == 30, "l'hote : le nouvel ordre");
    }
    check(sameShared(mh, host, ml, lea), "les memes empreintes");

    // Les proprietes de la vue (son fond) : la vue sans ses objets passe.
    host.view(11)->background = "#101010";
    ship(mh, host, ml, lea, {11}, false, "le fond d'une vue");
    check(lea.view(11)->background == "#101010", "Lea : le fond");

    // Une vue neuve chez l'hote (avec ses objets), une vue supprimee chez Lea.
    {
        hmi::View v;
        v.id = 12;
        v.name = "Vue_Neuve";
        v.layers.push_back({13, "Fond", true, false});
        v.activeLayer = 13;
        v.objects.push_back(rect(14, "Bouton", 13, 3));
        host.views.push_back(v);
    }
    ship(mh, host, ml, lea, {}, true, "une vue neuve");
    check(lea.view(12) && lea.view(12)->objects.size() == 1 && lea.view(12)->name == "Vue_Neuve", "Lea : la vue neuve et son objet");
    lea.views.erase(lea.views.begin() + 1);   // Alarmes (11)
    std::vector<ViewDelta> vd;
    ProjectDelta pd;
    ml.collect(lea, {}, false, vd, pd);
    check(!pd.empty(), "une vue supprimee : l'index part");
    ProjectApplied done;
    check(mh.applyProject(host, pd, done, &why), "l'hote applique (" + why + ")");
    check(host.view(11) == nullptr && done.removedViews.size() == 1 && done.removed.size() == 1 && done.removed[0].name == "Alarmes",
          "l'hote : la vue retiree (rendue pour sa corbeille)");
    check(sameShared(mh, host, ml, lea), "les memes empreintes");

    // Deux variables ajoutees en meme temps des deux cotes : les deux restent partout.
    {
        hmi::Variable a;
        a.id = 70;
        a.name = "Debit";
        host.programs.variables.push_back(a);
        hmi::Variable b;
        b.id = kIdBlock + 2;
        b.name = "Pression";
        lea.programs.variables.push_back(b);
    }
    std::vector<ViewDelta> v1, v2;
    ProjectDelta d1, d2;
    mh.collect(host, {}, true, v1, d1);
    ml.collect(lea, {}, true, v2, d2);
    ProjectApplied a1, a2;
    check(ml.applyProject(lea, d1, a1, &why) && a1.conflicts.empty(), "Lea recoit Debit");
    check(mh.applyProject(host, d2, a2, &why) && a2.conflicts.empty(), "l'hote recoit Pression");
    const auto hasVar = [](const hmi::Project& p, const std::string& n) {
        for (const auto& v : p.programs.variables)
            if (v.name == n) return true;
        return false;
    };
    check(hasVar(host, "Debit") && hasVar(host, "Pression") && hasVar(host, "Niveau"), "l'hote : les trois variables");
    check(hasVar(lea, "Debit") && hasVar(lea, "Pression") && hasVar(lea, "Niveau"), "Lea : les trois variables");
    // Deux ajouts au meme endroit se rangent dans l'ordre de leur arrivee : chez l'hote et chez Lea, pas le meme. Les
    // empreintes le voient ; Lea reprend le fichier de l'hote (l'hote fait foi).
    {
        const auto hh = mh.hashes(host), hl = ml.hashes(lea);
        std::vector<std::string> differ;
        for (const auto& [k, v] : hh)
            if (!hl.count(k) || hl.at(k) != v) differ.push_back(k);
        check(differ.size() == 1 && differ[0] == "f:ihm.txt", "les empreintes montrent l'ecart : ihm.txt seul");
        FileDelta f;
        check(mh.fileOf(host, "ihm.txt", f) && f.kind == FileDelta::Kind::Whole, "l'hote donne son fichier entier");
        ProjectDelta resync;
        resync.files.push_back(f);
        ProjectApplied r;
        check(ml.applyProject(lea, resync, r, &why), "Lea le reprend (" + why + ")");
    }
    check(sameShared(mh, host, ml, lea), "les memes empreintes a la fin");
    // Le compteur reste a chacun.
    check(host.nextId == 100 && lea.nextId == 100, "le compteur d'identifiants ne passe pas");

    // Un rattrapage : la vue entiere.
    const auto full = Mirror::fullView(*host.view(10));
    hmi::Project fresh = sample();
    Mirror mf;
    mf.reset(fresh);
    check(mf.applyView(fresh, full, &why) && *fresh.view(10) == *host.view(10), "un rattrapage : la vue entiere");
    // Remettre l'etat juste de quelques objets (un changement refuse).
    const auto some = Mirror::objectsOf(*host.view(10), {30, 31});
    check(some.objects.size() == 1 && some.removed.size() == 1 && some.removed[0] == 31, "l'etat de quelques objets : present / absent");
}


void annuler() {
    std::printf("-- Ctrl+Z en session : seulement ses propres gestes\n");
    auto doc = std::make_shared<hmi::Document>();
    doc->project = sample();
    doc->shared = true;
    doc->mergeShell = [](hmi::Project& cur, const hmi::Project& from, const hmi::Project& to) { return mergeShell(cur, from, to); };
    // Mon geste : deplacer Objet_0.
    auto cmd = hmi::changeView(doc, 10, "D\xC3\xA9placer", [](hmi::Project&, hmi::View& v) { setProp(v.object(30), "x", "900"); });
    check(cmd && cmd->execute().has_value(), "mon deplacement");
    // Un equipier, pendant ce temps : il change Objet_2, cree un objet, en retire un.
    {
        auto* v = doc->project.view(10);
        setProp(v->object(32), "fill", "#00FF00");
        v->objects.push_back(rect(kIdBlock + 7, "De_Lea", v->activeLayer, 1));
        v->objects.erase(v->objects.begin() + 1);   // Objet_1
    }
    check(cmd->undo().has_value(), "Ctrl+Z");
    {
        const auto* v = doc->project.view(10);
        check(propOf(v->object(30), "x") == "0", "mon objet revient");
        check(propOf(v->object(32), "fill") == "#00FF00", "le changement de l'equipier reste");
        check(v->object(kIdBlock + 7) != nullptr, "l'objet cree par l'equipier reste");
        check(v->object(31) == nullptr, "l'objet retire par l'equipier ne revient pas");
    }
    check(cmd->execute().has_value() && propOf(doc->project.view(10)->object(30), "x") == "900", "Ctrl+Y");

    // Une creation annulee : seul mon objet part, et l'ordre de ceux de l'equipier tient.
    auto add = hmi::changeView(doc, 10, "Cr\xC3\xA9" "er", [](hmi::Project&, hmi::View& v) { v.objects.push_back(rect(500, "Le_mien", v.activeLayer, 3)); });
    check(add && add->execute().has_value(), "je cree un objet");
    doc->project.view(10)->objects.push_back(rect(kIdBlock + 8, "Encore_de_Lea", doc->project.view(10)->activeLayer, 2));
    check(add->undo().has_value() && doc->project.view(10)->object(500) == nullptr && doc->project.view(10)->object(kIdBlock + 8) != nullptr,
          "annuler ma creation : le mien part, le sien reste");
    check(add->execute().has_value() && doc->project.view(10)->object(500) != nullptr, "la retablir : il revient");

    // Le reste du projet : j'ajoute une variable, un equipier en ajoute une autre ; Ctrl+Z retire seulement la mienne.
    auto addVar = hmi::changeProject(doc, "Variable", [](hmi::Project& p) {
        hmi::Variable v;
        v.id = 300;
        v.name = "Mienne";
        p.programs.variables.push_back(v);
    });
    check(addVar && addVar->execute().has_value(), "j'ajoute une variable");
    {
        hmi::Variable v;
        v.id = kIdBlock + 9;
        v.name = "Sienne";
        doc->project.programs.variables.push_back(v);
    }
    check(addVar->undo().has_value(), "Ctrl+Z sur la variable");
    bool mine = false, theirs = false;
    for (const auto& v : doc->project.programs.variables) {
        mine = mine || v.name == "Mienne";
        theirs = theirs || v.name == "Sienne";
    }
    check(!mine && theirs, "la mienne part, la sienne reste");
    check(doc->project.view(10) && doc->project.view(10)->object(500) != nullptr, "les vues ne bougent pas");

    // Hors session : comme avant (la pile refait la liste a l'identique).
    auto plain = std::make_shared<hmi::Document>();
    plain->project = sample();
    auto c2 = hmi::changeView(plain, 10, "D\xC3\xA9placer", [](hmi::Project&, hmi::View& v) { setProp(v.object(30), "x", "900"); });
    check(c2 && c2->execute().has_value() && c2->undo().has_value() && propOf(plain->project.view(10)->object(30), "x") == "0", "hors session : inchange");
}

void lien() {
    std::printf("-- le lien : un hote et un equipier sur la boucle locale\n");
    deploy::TlsIdentity hostId, leaId, otherId;
    std::string why;
    check(deploy::TlsIdentity::generate("XPG-Studio PC-BUREAU-01", hostId, &why), "l'identite de l'hote");
    check(deploy::TlsIdentity::generate("XPG-Studio PC-ATELIER-02", leaId, &why), "l'identite de Lea");
    check(deploy::TlsIdentity::generate("XPG-Studio Autre", otherId, &why), "une autre identite");

    const auto wait = [](const std::function<bool()>& cond, int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            if (cond()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return cond();
    };

    // ---- sans code ----
    {
        HostLink host;
        HostConfig cfg;
        cfg.bindAddress = "127.0.0.1";
        cfg.port = 0;
        cfg.discoveryPort = 0;
        cfg.hostName = "Camille";
        cfg.sessionId = "00112233445566aa";
        check(host.start(cfg, hostId, &why), "l'hote ecoute (" + why + ")");
        Announce a;
        a.session = "Essai";
        a.host = "Camille";
        a.fingerprint = hostId.fingerprintHex();
        a.sessionId = cfg.sessionId;
        host.setAnnounce(a);
        SearchOptions so;
        so.broadcast = false;
        so.direct = {"127.0.0.1"};
        so.port = host.discoveryPort();
        so.waitMs = 600;
        const auto found = search(so, &why);
        check(found.size() == 1 && found[0].announce.session == "Essai" && found[0].announce.port == host.port()
                  && found[0].announce.fingerprint == hostId.fingerprintHex(),
              "la decouverte trouve la session (son port, son empreinte)");

        GuestLink lea;
        GuestConfig g;
        g.address = "127.0.0.1";
        g.port = host.port();
        g.fingerprint = hostId.fingerprintHex();
        g.sessionId = cfg.sessionId;
        g.hello = core::json::Value::object();
        g.hello.set("nom", "L\xC3\xA9" "a");
        check(lea.start(g, leaId, &why), "Lea se connecte");
        std::vector<LinkEvent> hostEvents, leaEvents;
        check(wait([&] { for (auto& e : host.poll()) hostEvents.push_back(std::move(e)); return !hostEvents.empty(); }, 5000), "l'hote voit frapper");
        int conn = 0;
        if (!hostEvents.empty()) {
            const auto& k = hostEvents[0];
            check(k.kind == LinkEvent::Kind::Knock && k.header.str("nom") == "L\xC3\xA9" "a" && !k.codeOk && k.fingerprint == leaId.fingerprintHex(),
                  "Knock : le nom, l'empreinte de Lea, sans code");
            conn = k.conn;
        }
        core::json::Value att = core::json::Value::object();
        att.set("op", "ATTENTE");
        host.send(conn, att);
        check(wait([&] { for (auto& e : lea.poll()) leaEvents.push_back(std::move(e)); return leaEvents.size() >= 2; }, 5000), "Lea : connectee puis ATTENTE");
        if (leaEvents.size() >= 2) {
            check(leaEvents[0].kind == LinkEvent::Kind::Connected && leaEvents[1].header.str("op") == "ATTENTE", "Connected, puis ATTENTE");
        }
        // Des trames dans les deux sens, une grosse (le projet part en morceaux de 1 Mio).
        core::json::Value pj = core::json::Value::object();
        pj.set("op", "PROJET");
        host.send(conn, pj, std::string(1024 * 1024, 'x'));
        leaEvents.clear();
        check(wait([&] { for (auto& e : lea.poll()) leaEvents.push_back(std::move(e)); return !leaEvents.empty(); }, 8000), "Lea recoit 1 Mio");
        if (!leaEvents.empty()) check(leaEvents[0].header.str("op") == "PROJET" && leaEvents[0].data.size() == 1024u * 1024u, "le morceau entier");
        core::json::Value msg = core::json::Value::object();
        msg.set("op", "MESSAGE");
        msg.set("texte", "bonjour");
        lea.send(msg);
        hostEvents.clear();
        check(wait([&] { for (auto& e : host.poll()) hostEvents.push_back(std::move(e)); return !hostEvents.empty(); }, 5000), "l'hote recoit le message");
        if (!hostEvents.empty()) check(hostEvents[0].conn == conn && hostEvents[0].header.str("texte") == "bonjour", "le message, de la bonne connexion");
        // Lea part : l'hote le voit.
        core::json::Value bye = core::json::Value::object();
        bye.set("op", "AUREVOIR");
        lea.closeAfter(bye);
        hostEvents.clear();
        check(wait([&] {
                  for (auto& e : host.poll()) hostEvents.push_back(std::move(e));
                  for (const auto& e : hostEvents)
                      if (e.kind == LinkEvent::Kind::Closed) return true;
                  return false;
              }, 8000),
              "l'hote voit Lea partir");
        lea.stop();
        host.stop();
        check(!host.running(), "l'hote s'arrete");
    }

    // ---- avec le code ----
    {
        HostLink host;
        HostConfig cfg;
        cfg.bindAddress = "127.0.0.1";
        cfg.port = 0;
        cfg.discoveryPort = -1;
        cfg.hostName = "Camille";
        cfg.sessionId = "aabbccddeeff0011";
        cfg.code = "482913";
        check(host.start(cfg, hostId, &why), "l'hote (avec code) ecoute");
        const auto attempt = [&](const deploy::TlsIdentity& who, const std::string& code, const std::string& pin, std::vector<LinkEvent>& got) {
            GuestLink g;
            GuestConfig c;
            c.address = "127.0.0.1";
            c.port = host.port();
            c.fingerprint = pin;
            c.sessionId = cfg.sessionId;
            c.code = code;
            c.hello = core::json::Value::object();
            c.hello.set("nom", "Essai");
            std::string w;
            (void)g.start(c, who, &w);
            got.clear();
            (void)wait([&] {
                for (auto& e : g.poll()) got.push_back(std::move(e));
                for (const auto& e : got)
                    if (e.kind == LinkEvent::Kind::Closed || e.kind == LinkEvent::Kind::Connected) return true;
                return false;
            }, 8000);
            if (!got.empty() && got.back().kind != LinkEvent::Kind::Closed) {
                core::json::Value bye = core::json::Value::object();
                bye.set("op", "AUREVOIR");
                g.closeAfter(bye);
            }
            g.stop();
        };
        const auto refusedWrongCode = [](const std::vector<LinkEvent>& got) {
            for (const auto& e : got)
                if (e.kind == LinkEvent::Kind::Frame && e.header.str("op") == "REFUS" && e.header.flag("code_faux")) return true;
            return false;
        };
        std::vector<LinkEvent> got;
        attempt(leaId, "111111", hostId.fingerprintHex(), got);
        check(refusedWrongCode(got), "un code faux : refuse");
        std::vector<LinkEvent> hostEvents;
        (void)wait([&] { for (auto& e : host.poll()) hostEvents.push_back(std::move(e)); return false; }, 300);
        bool knocked = false;
        for (const auto& e : hostEvents) knocked = knocked || e.kind == LinkEvent::Kind::Knock;
        check(!knocked, "un code faux : l'hote ne voit personne frapper");

        attempt(leaId, "482 913", hostId.fingerprintHex(), got);
        bool waiting = false;
        for (const auto& e : got) waiting = waiting || (e.kind == LinkEvent::Kind::Connected);
        check(waiting, "le bon code (tape avec un espace) : connectee");
        hostEvents.clear();
        check(wait([&] {
                  for (auto& e : host.poll()) hostEvents.push_back(std::move(e));
                  for (const auto& e : hostEvents)
                      if (e.kind == LinkEvent::Kind::Knock) return true;
                  return false;
              }, 5000),
              "le bon code : l'hote voit frapper");
        for (const auto& e : hostEvents)
            if (e.kind == LinkEvent::Kind::Knock) check(e.codeOk, "Knock : le code est prouve");

        // Trois codes faux depuis le meme PC : plus d'essai, meme avec le bon.
        attempt(otherId, "000000", hostId.fingerprintHex(), got);
        attempt(otherId, "000001", hostId.fingerprintHex(), got);
        attempt(otherId, "000002", hostId.fingerprintHex(), got);
        attempt(otherId, "482913", hostId.fingerprintHex(), got);
        bool connected = false, refused = false;
        for (const auto& e : got) {
            connected = connected || e.kind == LinkEvent::Kind::Connected;
            refused = refused || (e.kind == LinkEvent::Kind::Frame && e.header.str("op") == "REFUS");
        }
        check(!connected && refused, "apres 3 codes faux : refuse, meme avec le bon");

        // Sans code alors que la session le demande : refuse sans essai compte.
        attempt(leaId, "", hostId.fingerprintHex(), got);
        bool needCode = false;
        for (const auto& e : got) needCode = needCode || (e.kind == LinkEvent::Kind::Frame && e.header.flag("il_faut_le_code"));
        check(needCode, "sans code : il faut le code");

        // Une empreinte epinglee qui n'est pas celle de l'hote : rien ne part.
        attempt(leaId, "482913", otherId.fingerprintHex(), got);
        bool pinned = false;
        for (const auto& e : got) pinned = pinned || (e.kind == LinkEvent::Kind::Closed && e.why.find("certificat") != std::string::npos);
        check(pinned, "une autre empreinte que celle annoncee : refusee par l'equipier");
        host.stop();
    }
    check(sessionBinding("a") != sessionBinding("b") && sessionBinding("a").size() == 64, "le lien du code a la session");
}


// =================================================================================================== 1.45.0 ===
void journal145() {
    std::printf("-- 1.45.0 : le journal de la session\n");
    const std::string sep = " \xE2\x80\xBA ";
    // ---- un objet deplace (le geste de l'hote : collect et ce qu'il remplace) ----
    hmi::Project host = sample();
    host.views[0].name = "Vue_Accueil";
    obj(host, 10, 30)->name = "Texte_Etat";
    Mirror mh;
    mh.reset(host);
    setProp(obj(host, 10, 30), "x", "250");
    std::vector<ViewDelta> views;
    ProjectDelta pd;
    Mirror::Previous prev;
    mh.collect(host, {10}, false, views, pd, &prev);
    check(views.size() == 1 && prev.views.count(10), "collect : la vue d'avant (son objet touche)");
    std::vector<std::string> lines;
    if (views.size() == 1) lines = describeView(views[0], &prev.views[10], "Vue_Accueil");
    check(lines.size() == 1 && lines[0] == "Vue_Accueil" + sep + "Texte_Etat d\xC3\xA9plac\xC3\xA9", "un objet deplace : << Vue_Accueil > Texte_Etat deplace >>");

    // ---- le changement d'un equipier, decrit par l'hote avant de l'appliquer ----
    hmi::Project lea = host;
    Mirror ml;
    ml.reset(lea);
    mh.reset(host);
    setProp(obj(lea, 10, 31), "w", "300");                 // redimensionne
    setProp(obj(lea, 10, 32), "fill", "#AA0000");          // modifie
    lea.views[0].objects.push_back(rect(0x10000005u, "Voyant_Gaz", 20, 400));   // ajoute
    std::erase_if(lea.views[0].objects, [](const hmi::Object& o) { return o.id == 33; });   // supprime
    views.clear();
    pd = {};
    ml.collect(lea, {10}, false, views, pd);
    lines.clear();
    if (views.size() == 1) lines = describeView(views[0], host.view(10), "Vue_Accueil");
    const auto has = [&](const std::string& t) { return std::find(lines.begin(), lines.end(), t) != lines.end(); };
    check(has("Vue_Accueil" + sep + "Objet_1 redimensionn\xC3\xA9"), "redimensionne");
    check(has("Vue_Accueil" + sep + "Objet_2 modifi\xC3\xA9 (fill)"), "modifie (la propriete)");
    check(has("Vue_Accueil" + sep + "Voyant_Gaz ajout\xC3\xA9"), "ajoute");
    check(has("Vue_Accueil" + sep + "Objet_3 supprim\xC3\xA9"), "supprime (le nom d'avant)");
    // Plus de 3 objets d'un meme geste : un compte.
    hmi::Project many = host;
    Mirror mm;
    mm.reset(many);
    for (auto& o : many.views[0].objects) setProp(&o, "y", "90");
    views.clear();
    mm.collect(many, {10}, false, views, pd, &prev);
    lines.clear();
    if (views.size() == 1) lines = describeView(views[0], &prev.views[10], "Vue_Accueil");
    check(lines.size() == 1 && lines[0] == "Vue_Accueil" + sep + "4 objets d\xC3\xA9plac\xC3\xA9s", "4 objets deplaces d'un geste : un compte");

    // ---- 1.45.0 (point 7) : UNE RESSOURCE AJOUTEE PAR UN EQUIPIER arrive chez l'hote (et repart aux autres) ----
    hmi::Project h2 = sample(), l2 = sample(), o2 = sample();
    Mirror mh2, ml2, mo2;
    mh2.reset(h2);
    ml2.reset(l2);
    mo2.reset(o2);
    hmi::Resource logo;
    logo.id = 0x10000010u;
    logo.name = "logo.png";
    logo.format = "PNG";
    auto png = std::make_shared<hmi::Bytes>();
    const unsigned char sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0x0A, 0xFF, 0x00};
    png->assign(std::begin(sig), std::end(sig));
    logo.data = png;
    logo.bytes = png->size();
    l2.assets.resources.push_back(logo);
    std::vector<ViewDelta> v2;
    ProjectDelta p2;
    ml2.collect(l2, {}, true, v2, p2);
    bool whole = false;
    for (const auto& f : p2.files) whole = whole || (f.path.rfind("ressources/", 0) == 0 && f.kind == FileDelta::Kind::Whole && f.whole.size() == png->size());
    check(whole, "la ressource part entiere (ressources/..., ses octets)");
    // L'hote : les phrases avant d'appliquer (les fichiers d'avant : l'etat partage), puis il applique.
    const auto described = describeFiles(p2, [&](const std::string& path) { return mh2.hasFile(path); });
    check(std::find(described.begin(), described.end(), "ressource logo.png ajout\xC3\xA9" "e") != described.end(),
          "le journal : << ressource logo.png ajoutee >> (" + (described.empty() ? std::string("rien") : described[0]) + ")");
    // En trames, comme sur le reseau : chez l'hote, puis de l'hote aux autres.
    {
        core::json::Value h;
        std::string data;
        encodeProject(p2, h, data);
        ProjectDelta back;
        std::string why;
        core::json::Value h2b;
        check(core::json::parse(core::json::write(h, false), h2b, nullptr) && decodeProject(h2b, data, back, &why), "la ressource : la trame se relit");
        ProjectApplied done;
        check(mh2.applyProject(h2, back, done, &why) && done.conflicts.empty(), "la ressource s'applique chez l'hote (" + why + ")");
        ProjectApplied done2;
        check(mo2.applyProject(o2, back, done2, &why), "... et chez un autre equipier (" + why + ")");
    }
    const auto found = [](const hmi::Project& p) -> const hmi::Resource* {
        for (const auto& r : p.assets.resources)
            if (r.name == "logo.png") return &r;
        return nullptr;
    };
    check(found(h2) && found(h2)->data && *found(h2)->data == *png, "l'hote a l'image, octet pour octet");
    check(found(o2) && found(o2)->data && *found(o2)->data == *png, "l'autre equipier aussi");
    check(sameShared(mh2, h2, ml2, l2) && sameShared(mh2, h2, mo2, o2), "les empreintes egales (hote, Lea, l'autre)");
    // La meme ressource remplacee : << remplacee >>.
    ProjectDelta again;
    FileDelta f;
    for (const auto& x : p2.files)
        if (x.path.rfind("ressources/", 0) == 0) f = x;
    again.files.push_back(f);
    const auto replaced = describeFiles(again, [&](const std::string& path) { return mh2.hasFile(path); });
    check(replaced.size() == 1 && replaced[0] == "ressource logo.png remplac\xC3\xA9" "e", "une ressource qui existait : remplacee");

    // ---- une variable ajoutee (l'index ihm.txt) ----
    hmi::Project h3 = sample();
    Mirror m3;
    m3.reset(h3);
    hmi::Variable dv;
    dv.id = 0x10000020u;
    dv.name = "Debit";
    dv.type = "REAL";
    h3.programs.variables.push_back(dv);
    std::vector<ViewDelta> v3;
    ProjectDelta p3;
    Mirror::Previous prev3;
    m3.collect(h3, {}, true, v3, p3, &prev3);
    const auto vlines = describeFiles(p3, [&](const std::string& path) { return prev3.files.count(path) > 0; });
    check(std::find(vlines.begin(), vlines.end(), "variable Debit ajout\xC3\xA9" "e") != vlines.end(), "une variable ajoutee : << variable Debit ajoutee >>");

    // ---- le journal lui-meme : la fusion d'un glisser, la borne, le JSON, la ligne, le CSV ----
    Journal j;
    const std::int64_t t0 = 1800000000;
    j.add(1, "L\xC3\xA9" "a", 0x2E86DE, "Vue_Accueil" + sep + "Texte_Etat d\xC3\xA9plac\xC3\xA9", t0);
    j.add(1, "L\xC3\xA9" "a", 0x2E86DE, "Vue_Accueil" + sep + "Texte_Etat d\xC3\xA9plac\xC3\xA9", t0 + 1);
    j.add(0, "Camille", 0xE67E22, "ressource logo.png ajout\xC3\xA9" "e", t0 + 2);
    j.add(1, "L\xC3\xA9" "a", 0x2E86DE, "Vue_Accueil" + sep + "Texte_Etat d\xC3\xA9plac\xC3\xA9", t0 + 3);   // fondue (parmi les 8 dernieres)
    check(j.entries().size() == 2 && j.entries()[0].count == 3 && j.entries()[0].t == t0 + 3, "un glisser : une entree (x3), l'heure du dernier geste");
    j.add(1, "L\xC3\xA9" "a", 0x2E86DE, "Vue_Accueil" + sep + "Texte_Etat d\xC3\xA9plac\xC3\xA9", t0 + 400);   // plus tard : une autre
    check(j.entries().size() == 3, "plus de 2 min apres : une entree neuve");
    const std::string line = lineOf(j.entries()[0]);
    check(line.find(" L\xC3\xA9" "a : Vue_Accueil" + sep + "Texte_Etat d\xC3\xA9plac\xC3\xA9 (\xC3\x97" "3)") != std::string::npos && line.size() > 5 && line[2] == ':',
          "la ligne : << 14:32 Lea : Vue_Accueil > Texte_Etat deplace (x3) >>");
    JournalEntry back;
    check(fromJson(toJson(j.entries()[0]), back) && back == j.entries()[0], "une entree : aller-retour JSON");
    Journal guest;
    guest.replace({j.entries().begin(), j.entries().end()});
    auto upd = j.entries()[2];
    upd.count = 5;
    guest.put(upd);
    check(guest.entries().size() == 3 && guest.entries()[2].count == 5, "chez l'equipier : l'entree fondue remplace la sienne (meme numero)");
    Journal big;
    for (std::size_t i = 0; i < kJournalMax + 25; ++i) big.add(0, "Camille", 0, "objet " + std::to_string(i) + " modifi\xC3\xA9", t0 + static_cast<std::int64_t>(i) * 200);
    check(big.entries().size() == kJournalMax && big.dropped() == 25 && big.entries().front().text == "objet 25 modifi\xC3\xA9", "borne : 2 000 entrees, les plus anciennes tombent");
    const std::string csv = journalCsv(j.entries());
    check(csv.rfind("\xEF\xBB\xBF" "Date;Heure;Auteur;Changement;Gestes\r\n", 0) == 0 && csv.find(";Camille;ressource logo.png ajout\xC3\xA9" "e;1\r\n") != std::string::npos,
          "le CSV : la marque UTF-8, l'en-tete, une ligne par entree");
    // Enregistre avec le projet (l'hote, a la fin) : ihm/historique/sessions/.
    namespace fs = std::filesystem;
    const fs::path proj = fs::temp_directory_path() / ("xpg-session145-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(proj, ec);
    fs::create_directories(proj, ec);
    std::string saved, why;
    check(saveJournal(proj.string(), "Mise en service Armoire_Gaz", t0, j.entries(), &saved, &why), "le journal s'enregistre (" + why + ")");
    check(!saved.empty() && fs::exists(saved, ec) && saved.find("ihm/historique/sessions") != std::string::npos
              && saved.find("Mise en service Armoire_Gaz.csv") != std::string::npos,
          "dans ihm/historique/sessions/<date>_<nom>.csv (" + saved + ")");
    std::string saved2;
    check(saveJournal(proj.string(), "Mise en service Armoire_Gaz", t0, j.entries(), &saved2, &why) && saved2 != saved, "deux fois la meme minute : un autre fichier");
    std::string none;
    check(saveJournal(proj.string(), "Vide", t0, {}, &none, &why) && none.empty(), "un journal vide : rien d'ecrit");
    fs::remove_all(proj, ec);
}

void copies145() {
    std::printf("-- 1.45.0 : la copie de l'equipier (effacee au depart, purgee au lancement)\n");
    namespace fs = std::filesystem;
    const fs::path shared = fs::temp_directory_path() / ("xpg-commun145-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(shared, ec);
    const std::string area = guestArea(shared.string());
    check(fs::path(area).filename() == "Sessions", "l'aire : <dossier commun>/Sessions");
    const auto makeCopy = [&](const std::string& sid) {
        const fs::path dir = fs::path(area) / sid / "Armoire_Gaz";
        fs::create_directories(dir / "ihm" / "vues", ec);
        std::ofstream(dir / "project.xpgproj") << "nom=Armoire_Gaz\n";
        std::ofstream(dir / "ihm" / "ihm.txt") << "ihm\n";
        return dir.string();
    };
    const std::string copy = makeCopy("00112233445566aa");
    check(fs::exists(copy, ec) && insideArea(area, copy) && insideArea(area, area) && !insideArea(area, shared.string())
              && !insideArea(area, (shared / "SessionsX").string()),
          "insideArea : la copie oui, le dossier commun non, un voisin au nom proche non");
    std::string why;
    check(removeCopy(area, "00112233445566aa", &why) && !fs::exists(fs::path(area) / "00112233445566aa", ec), "l'equipier part : sa copie s'efface (" + why + ")");
    check(removeCopy(area, "00112233445566aa", &why), "deja partie : rien a faire");
    check(removeCopy(area, "../x", &why), "un identifiant qui remonte : refuse en silence (rien d'efface)");
    // Un arret brutal : deux restes ; l'un est ouvert dans une autre fenetre (garde), l'autre part.
    const std::string left1 = makeCopy("aaaaaaaaaaaaaaaa");
    const std::string left2 = makeCopy("bbbbbbbbbbbbbbbb");
    const auto purged = purgeArea(area, [&](const std::string& f) { return f == left2; });
    check(purged == 1 && !fs::exists(left1, ec) && fs::exists(left2, ec), "au lancement : le reste purge, la copie ouverte ailleurs gardee");
    check(purgeArea(area) == 1 && !fs::exists(left2, ec), "sans rien a garder : tout part");
    check(purgeArea((shared / "absent").string()) == 0, "une aire absente : rien");
    fs::remove_all(shared, ec);
}

void departs145() {
    std::printf("-- 1.45.0 : retirer, partir, arreter (de vrais liens sur la boucle locale)\n");
    deploy::TlsIdentity hostId, leaId, hugoId;
    std::string why;
    check(deploy::TlsIdentity::generate("XPG-Studio PC-BUREAU-01", hostId, &why), "l'identite de l'hote");
    check(deploy::TlsIdentity::generate("XPG-Studio PC-ATELIER-02", leaId, &why), "l'identite de Lea");
    check(deploy::TlsIdentity::generate("XPG-Studio PC-ATELIER-03", hugoId, &why), "l'identite de Hugo");
    const auto wait = [](const std::function<bool()>& cond, int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            if (cond()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return cond();
    };
    HostLink host;
    HostConfig cfg;
    cfg.bindAddress = "127.0.0.1";
    cfg.port = 0;
    cfg.discoveryPort = -1;
    cfg.hostName = "Camille";
    cfg.sessionId = "0123456789abcdef";
    check(host.start(cfg, hostId, &why), "l'hote ecoute (" + why + ")");
    std::vector<LinkEvent> hostEvents;
    const auto pumpHost = [&] { for (auto& e : host.poll()) hostEvents.push_back(std::move(e)); };
    // Frapper : la connexion et l'empreinte que l'hote voit.
    const auto knock = [&](GuestLink& g, const deploy::TlsIdentity& who, const std::string& name, int& conn, std::string& print) {
        GuestConfig c;
        c.address = "127.0.0.1";
        c.port = host.port();
        c.fingerprint = hostId.fingerprintHex();
        c.sessionId = cfg.sessionId;
        c.hello = core::json::Value::object();
        c.hello.set("nom", name);
        std::string w;
        if (!g.start(c, who, &w)) return false;
        conn = 0;
        const bool ok = wait([&] {
            pumpHost();
            for (auto it = hostEvents.begin(); it != hostEvents.end(); ++it)
                if (it->kind == LinkEvent::Kind::Knock && it->header.str("nom") == name) {
                    conn = it->conn;
                    print = it->fingerprint;
                    hostEvents.erase(it);
                    return true;
                }
            return false;
        }, 6000);
        return ok;
    };
    GuestLink lea, hugo;
    int leaConn = 0, hugoConn = 0;
    std::string leaPrint, hugoPrint;
    check(knock(lea, leaId, "L\xC3\xA9" "a", leaConn, leaPrint) && leaPrint == leaId.fingerprintHex(), "Lea frappe (son empreinte)");
    check(knock(hugo, hugoId, "Hugo", hugoConn, hugoPrint), "Hugo frappe");
    std::vector<LinkEvent> leaEvents, hugoEvents;
    const auto pumpLea = [&] { for (auto& e : lea.poll()) leaEvents.push_back(std::move(e)); };
    const auto pumpHugo = [&] { for (auto& e : hugo.poll()) hugoEvents.push_back(std::move(e)); };
    const auto frameOf = [](const std::vector<LinkEvent>& evs, const std::string& op) -> const LinkEvent* {
        for (const auto& e : evs)
            if (e.kind == LinkEvent::Kind::Frame && e.header.str("op") == op) return &e;
        return nullptr;
    };
    const auto closed = [](const std::vector<LinkEvent>& evs, int conn = -1) {
        for (const auto& e : evs)
            if (e.kind == LinkEvent::Kind::Closed && (conn < 0 || e.conn == conn)) return true;
        return false;
    };

    // ---- L'HOTE RETIRE LEA : AUREVOIR {retire, raison} puis la connexion fermee ; les autres : NOUVELLE ----
    core::json::Value bye = core::json::Value::object();
    bye.set("op", "AUREVOIR");
    bye.set("retire", true);
    bye.set("raison", "Camille t'a retir\xC3\xA9 de la session");
    host.closeAfter(leaConn, bye);
    core::json::Value news = core::json::Value::object();
    news.set("op", "NOUVELLE");
    news.set("texte", "Camille a retir\xC3\xA9 L\xC3\xA9" "a de la session");
    news.set("ton", "warning");
    host.send(hugoConn, news);
    check(wait([&] { pumpLea(); return closed(leaEvents); }, 8000), "Lea : coupee");
    const auto* gone = frameOf(leaEvents, "AUREVOIR");
    check(gone && gone->header.flag("retire") && gone->header.str("raison") == "Camille t'a retir\xC3\xA9 de la session",
          "Lea recoit la raison avant la coupure (<< Camille t'a retire de la session >>)");
    check(wait([&] { pumpHugo(); return frameOf(hugoEvents, "NOUVELLE") != nullptr; }, 5000)
              && frameOf(hugoEvents, "NOUVELLE")->header.str("texte") == "Camille a retir\xC3\xA9 L\xC3\xA9" "a de la session",
          "Hugo le lit (NOUVELLE : la discussion, une notification)");
    check(wait([&] { pumpHost(); return closed(hostEvents, leaConn); }, 8000), "l'hote voit la connexion de Lea fermee");
    lea.stop();
    // Lea revient : la MEME empreinte (le retrait la retient : l'hote la fait entrer lui-meme, meme avec le code).
    GuestLink lea2;
    int lea2Conn = 0;
    std::string lea2Print;
    check(knock(lea2, leaId, "L\xC3\xA9" "a", lea2Conn, lea2Print) && lea2Print == leaPrint && lea2Conn != leaConn,
          "Lea qui revient : une autre connexion, la meme empreinte (retenue par l'hote)");

    // ---- UN EQUIPIER PART : AUREVOIR, l'hote le voit ----
    hostEvents.clear();
    core::json::Value leave = core::json::Value::object();
    leave.set("op", "AUREVOIR");
    hugo.closeAfter(leave);
    check(wait([&] { pumpHost(); return frameOf(hostEvents, "AUREVOIR") != nullptr; }, 8000) && frameOf(hostEvents, "AUREVOIR")->conn == hugoConn,
          "Hugo part : l'hote recoit son AUREVOIR (<< Hugo a quitte la session >>)");
    hugo.stop();

    // ---- L'HOTE ARRETE : AUREVOIR {fin, raison} a tous, puis tout se ferme ----
    std::vector<LinkEvent> lea2Events;
    core::json::Value end = core::json::Value::object();
    end.set("op", "AUREVOIR");
    end.set("fin", true);
    end.set("raison", "Camille a arr\xC3\xAAt\xC3\xA9 la session");
    host.closeAfter(lea2Conn, end);
    host.stop();
    check(wait([&] {
              for (auto& e : lea2.poll()) lea2Events.push_back(std::move(e));
              return closed(lea2Events);
          }, 8000),
          "l'hote arrete : Lea est coupee");
    const auto* fin = frameOf(lea2Events, "AUREVOIR");
    check(fin && fin->header.flag("fin") && fin->header.str("raison") == "Camille a arr\xC3\xAAt\xC3\xA9 la session",
          "... avec << Camille a arrete la session >> (elle revient a l'accueil)");
    lea2.stop();
    check(!host.running(), "l'hote s'est arrete");
}

// Un service des licences d'avant la 1.44 : 404 << route >> sur tout ce qui est /v1/relais/...
class OldService final : public RelayTransport {
public:
    explicit OldService(bool relay144) : relay144_(relay144) {}
    RelayReply call(const RelayCall& c) override {
        ++calls;
        RelayReply r;
        if (cancelled_) return r;
        if (c.path == "/v1/etat") {
            r.status = 200;
            r.body = "{\"ok\":true,\"heure\":1800000000}";
            return r;
        }
        r.status = 404;
        r.body = relay144_ ? "{\"erreur\":\"absente\",\"message\":\"Aucune session ouverte avec ce code : v\xC3\xA9rifie-le (l'h\xC3\xB4te te le donne), ou la session est arr\xC3\xAAt\xC3\xA9" "e.\"}"
                           : "{\"erreur\":\"route\",\"message\":\"Cette adresse n'existe pas sur le service des licences.\"}";
        return r;
    }
    void cancel() override { cancelled_ = true; }
    std::atomic<int> calls{0};

private:
    bool relay144_;
    std::atomic<bool> cancelled_{false};
};

void serviceTropVieux() {
    std::printf("-- 1.45.0 : le service des licences trop vieux (404 << route >>)\n");
    check(relayTooOldText() == "Le service des licences n'est pas encore en 1.44 : l'\xC3\xA9" "diteur doit le red\xC3\xA9ployer (Admin \xE2\x80\xBA Service en ligne \xE2\x80\xBA D\xC3\xA9ployer)",
          "la phrase");
    const auto wait = [](const std::function<bool()>& cond, int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            if (cond()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return cond();
    };
    std::string why;
    // L'hote : POST /v1/relais/ouvrir -> 404 route.
    {
        RelayHostLink h;
        RelayHostConfig rc;
        rc.transport = std::make_shared<OldService>(false);
        rc.code = newRelayCode();
        rc.sessionName = "Mise en service Armoire_Gaz";
        rc.hostName = "Camille";
        check(h.start(rc, &why), "l'hote demarre sur un service 1.43 (" + why + ")");
        std::string closedWhy;
        check(wait([&] {
                  for (const auto& e : h.poll())
                      if (e.kind == LinkEvent::Kind::Closed && e.conn == 0) closedWhy = e.why;
                  return !closedWhy.empty();
              }, 20000),
              "la session ne s'ouvre pas (Closed, conn 0)");
        check(closedWhy == relayTooOldText() && relayTooOld(closedWhy), "l'hote : << le service des licences n'est pas encore en 1.44... >> (" + closedWhy + ")");
        check(closedWhy.find("cette adresse n'existe pas") == std::string::npos, "plus << cette adresse n'existe pas sur le service des licences >>");
        h.stop();
    }
    // L'equipier : POST /v1/relais/<localisateur>/frapper -> 404 route.
    {
        RelayGuestLink g;
        RelayGuestConfig gc;
        gc.transport = std::make_shared<OldService>(false);
        gc.code = newRelayCode();
        gc.hello = core::json::Value::object();
        gc.hello.set("nom", "L\xC3\xA9" "a");
        check(g.start(gc, &why), "l'equipier frappe sur un service 1.43 (" + why + ")");
        std::string closedWhy;
        check(wait([&] {
                  for (const auto& e : g.poll())
                      if (e.kind == LinkEvent::Kind::Closed) closedWhy = e.why;
                  return !closedWhy.empty();
              }, 20000),
              "l'equipier n'entre pas");
        check(closedWhy == relayTooOldText(), "l'equipier : la meme phrase (" + closedWhy + ")");
        g.stop();
    }
    // Un service 1.44 qui ne trouve pas la session (un code faux) : sa phrase a lui, pas celle du service trop vieux.
    {
        RelayGuestLink g;
        RelayGuestConfig gc;
        gc.transport = std::make_shared<OldService>(true);
        gc.code = newRelayCode();
        gc.hello = core::json::Value::object();
        check(g.start(gc, &why), "un code faux sur un service 1.44");
        std::string closedWhy;
        (void)wait([&] {
            for (const auto& e : g.poll())
                if (e.kind == LinkEvent::Kind::Closed) closedWhy = e.why;
            return !closedWhy.empty();
        }, 20000);
        check(!relayTooOld(closedWhy) && closedWhy.find("aucune session ouverte avec ce code") != std::string::npos,
              "un code faux : << aucune session ouverte avec ce code >> (" + closedWhy + ")");
        g.stop();
    }
}

} // namespace

int main() {
    protocole();
    lignes();
    miroir();
    annuler();
    lien();
    journal145();          // 1.45.0
    copies145();
    departs145();
    serviceTropVieux();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
