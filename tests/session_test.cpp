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
// =============================================================================
#include "../src/session/SessionLink.hpp"
#include "../src/session/SessionProtocol.hpp"
#include "../src/session/SessionSync.hpp"

#include "../src/deploy/Crypto.hpp"
#include "../src/deploy/TlsIdentity.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <chrono>
#include <cstdio>
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

} // namespace

int main() {
    protocole();
    lignes();
    miroir();
    annuler();
    lien();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
