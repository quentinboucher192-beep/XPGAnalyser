// =============================================================================
//  tests/admin_invitation_test.cpp - 1.49.0 : L'INVITATION PAR CODES ; 1.51.0 :
//  PLUS DE DEMANDE D'ADMIN (docs/ADMINS.md, par. 4 et 12) dans l'outil de
//  l'editeur (license_admin/gui/Workspace : WorkspaceAdmins.cpp), contre le VRAI
//  Worker lance sur ce poste (tests/service_local.hpp)
// -----------------------------------------------------------------------------
//  Sans reseau : la remise en forme d'un code d'invitation. Puis : l'admin+ cree
//  l'invitation avec un profil (le code montre une fois, note sans le code avec ses
//  droits, une ligne << admin_inviter >>) ; XPG Admin (l'edition classique :
//  setEdition(Classique)) lit invitation.txt, cree sa cle et S'INSCRIT avec le code
//  (invitation.txt efface) - aucun code a dire ; l'admin+ le signe D'OFFICE
//  (admitInvitedAdmins) avec les droits choisis a l'invitation, une ligne
//  << admin_ajouter >> (code d'invitation, invite par) ; XPG Admin se voit admin.
//  Le regard de l'admin+ (serviceWatch puis refreshAdminState) signe seul un autre
//  inscrit. Une demande d'avant (un XPG Admin 1.50 : POST /v1/admins/demande avec son
//  code, ses 6 chiffres jamais tapes) devient admin de meme. Une inscription dont
//  l'invitation n'a pas ete creee sur ce poste n'est jamais signee. Les refus : mal
//  ecrit, inconnu (invitation.txt efface, le code retape : accepte), deja servi,
//  expire (l'horloge du service avancee de 49 h). Aucune vraie cle, des noms d'essai.
//
//    admin_invitation_test SOURCE
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "service_local.hpp"

#include "../src/core/Json.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/Edition.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Admins.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace adm = licensing::admins;
namespace la = license_admin;
namespace gui = license_admin::gui;
namespace online = license_admin::online;
using core::json::Value;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

// XPG Admin (l'Admin classique) le temps d'un appel ; l'Admin+ ensuite (le meme programme d'essai sert les deux).
struct Classic {
    Classic() { la::setEdition(la::Edition::Classique); }
    ~Classic() { la::setEdition(la::Edition::Plus); }
};

const std::string kNina = "Nina Essai";
const std::string kHugo = "Hugo Essai";
const std::string kCamille = "Camille Essai";
const std::string kPaul = "Paul Essai";
const std::string kZoe = "Zo\xC3\xA9 Essai";
const std::string kProfile = "Admin LV2";
const std::vector<std::string> kProfileRights = {"licence_creer", "demandes"};

// Les lignes << admin_ajouter >> du journal de l'admin+ pour cet admin (son identifiant de cle).
std::vector<gui::JournalRow> addedLines(gui::Workspace& plus, const std::string& keyId) {
    (void)plus.fetchJournal(nullptr);
    std::vector<gui::JournalRow> out;
    for (const auto& l : plus.journalLines().rows)
        if (l.line.parsed.action == "admin_ajouter" && l.line.parsed.object == keyId) out.push_back(l);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!licensing::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-admin-invitation-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{root};

    // ======================================================================== sans reseau
    std::printf("-- le code d'invitation (sans reseau)\n");
    {
        check(adm::normalizeInvitationCode("inv-7k3q m9tx") == "INV-7K3Q-M9TX" && adm::normalizeInvitationCode("7K3QM9TX") == "INV-7K3Q-M9TX" &&
                  adm::normalizeInvitationCode("INV-7K3Q-M9TO") == "INV-7K3Q-M9T0",
              "un code d'invitation remis en forme (minuscules, espaces, sans INV, O lu 0)");
        check(adm::normalizeInvitationCode("INV-7K3Q-M9TU").empty() && adm::normalizeInvitationCode("INV-7K3Q").empty() &&
                  adm::normalizeInvitationCode("").empty(),
              "U (hors de l'alphabet), trop court, vide : pas un code");
        check(adm::actionType("admin_inviter") != nullptr && adm::actionType("admin_ajouter") != nullptr, "les actions du journal << admin_inviter >>, << admin_ajouter >>");
        check(gui::roleLabel(gui::Role::Pending) == "Admin (ajout en cours)", "le role d'une cle inscrite : << Admin (ajout en cours) >> (plus d'attente d'acceptation)");
    }

    gui::Workspace plus(U(root / "plus"));
    {
        const auto c = plus.createKey("essai invitation", true);
        check(c.ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai) : l'admin+ (" + c.text() + ")");
        adm::Profile p;
        p.name = kProfile;
        p.rights = kProfileRights;
        check(plus.saveProfile(p).ok, "le profil << " + kProfile + " >> (2 droits)");
    }

    // ======================================================================== le service local
    xpg_test::LocalService svc({plus.signingKey()->publicKeyHex()});
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    if (!svc.ready()) {
        std::printf("ECHEC : le service local : %s\n", svc.error().c_str());
        return 1;
    }
    std::printf("   service local : %s (%.1f s)\n", svc.url().c_str(), svc.startSeconds());
    {
        check(plus.serviceConfigure(svc.url()).ok, "l'admin+ : l'adresse du service");
        const auto c = plus.serviceCertify({});
        check(c.ok, "la cle du service certifiee (" + c.text() + ")");
        std::string recovery;   // le code de secours : jamais montre
        const auto k = plus.syncKeyCreate(&recovery);
        check(k.ok, "la cle de synchro (le trousseau des admins) (" + k.message + ")");
    }

    std::printf("-- l'admin+ invite Nina avec un profil (le code montre une fois, les droits notes)\n");
    std::string code, invId;
    {
        std::string none;
        check(!plus.inviteAdmin("", "nina@exemple.invalid", kProfile, none).ok && none.empty(), "sans nom : refuse, pas de code");
        check(!plus.inviteAdmin(kNina, "nina@exemple.invalid", "Profil inconnu", none).ok && none.empty(), "un profil inconnu : refuse, pas de code");
        gui::AdminInvitation noted;
        const auto r = plus.inviteAdmin(kNina, "nina@exemple.invalid", kProfile, code, &noted);
        check(r.ok && adm::normalizeInvitationCode(code) == code && code.size() == 13 && has(r.message, code), "le code d'invitation (" + r.text() + ")");
        check(has(r.text(), "il est admin d\xC3\xA8s que son code est accept\xC3\xA9") && !has(r.text(), "6 chiffres") && !has(r.text(), "Demandes d'admin"),
              "le resultat dit la nouvelle regle (admin des le code accepte ; plus de 6 chiffres)");
        invId = noted.id;
        check(invId.rfind("inv-", 0) == 0 && noted.expires > plus.now() + 47 * 3600, "son id, ses 48 h");
        const auto list = plus.adminInvitations();
        check(!list.empty() && list.front().id == invId && list.front().name == kNina && list.front().rightsSet && list.front().rights == adm::normalizeRights(kProfileRights) &&
                  list.front().admittedKey.empty(),
              "notee (invitations-admins.json) avec les droits du profil, pas encore servie");
        const std::string text = readFile(root / "plus" / "invitations-admins.json");
        check(!text.empty() && !has(text, code), "le code lui-meme n'est jamais garde sur le poste de l'admin+");
        (void)plus.fetchJournal(nullptr);
        bool seen = false;
        for (const auto& l : plus.journalLines().rows) seen = seen || l.line.parsed.action == "admin_inviter";
        check(seen, "au journal : << admin_inviter >>");
    }

    std::printf("-- XPG Admin (Nina) : invitation.txt, sa cle, son code accepte : inscrite, rien a dire\n");
    gui::Workspace nina(U(root / "nina"));
    {
        Classic classic;
        check(nina.serviceConfigure(svc.url()).ok, "le poste de Nina : l'adresse du service");
        check(nina.pendingInvitationCode().empty(), "pas encore d'invitation.txt : le champ le demandera");
        // l'installateur l'ecrit (une ligne ; minuscules, espaces toleres)
        std::string typed = code;
        for (auto& ch : typed) ch = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch == '-' ? ' ' : ch;
        writeFile(root / "nina" / "invitation.txt", typed + "\r\n");
        check(nina.pendingInvitationCode() == code, "invitation.txt lu et remis en forme (" + nina.pendingInvitationCode() + ")");
        bool refused = true;
        const auto r = nina.joinWithInvitation(nina.pendingInvitationCode(), &refused);
        nina.reload();
        check(r.ok && !refused && r.output == "inscrit" && nina.adminKey().present && has(r.message, "tu es admin") && has(r.message, kProfile),
              "le code accepte : inscrite, avec son profil (" + r.text() + ")");
        check(!has(r.text(), "6 chiffres") && !has(r.text(), "Dis ce code") && !has(r.text(), "empreinte \xC3\xA0"), "rien a dire a l'admin+ (ni code, ni empreinte)");
        check(!fs::exists(root / "nina" / "invitation.txt"), "invitation.txt efface (le service a repondu)");
        check(nina.role() == gui::Role::Pending, "tant que l'Admin+ n'a pas signe : ajout en cours");
        const gui::AccountInfo a = nina.account();
        check(a.name == kNina && a.email == "nina@exemple.invalid" && a.profile == kProfile && a.invitationId == invId && a.joinedAt > 0,
              "Mon compte : le nom, le courriel et le profil de l'invitation (rien a taper) ; l'invitation, l'heure");
        std::string state;
        check(nina.joinState(state).ok && state == "inscrit", "ou en est sa cle : inscrite");
    }

    std::printf("-- l'admin+ la signe d'office (les droits de l'invitation), une ligne << admin_ajouter >>\n");
    {
        const std::string key = nina.adminKey().keyHex;
        const int before = plus.adminList() ? plus.adminList()->number : 0;
        std::vector<std::string> names;
        const auto ok = plus.admitInvitedAdmins(&names);
        plus.reload();
        const adm::Admin* a = plus.adminList() ? plus.adminList()->byKey(key) : nullptr;
        check(ok.ok && names == std::vector<std::string>{kNina} && a && a->active() && a->name == kNina && a->profile == kProfile &&
                  a->rights == adm::normalizeRights(kProfileRights) && plus.adminList()->number == before + 1,
              "Nina est admin : une liste signee de plus, son profil et ses droits (" + ok.text() + ")");
        check(has(ok.text(), "code d'invitation") && has(ok.text(), "cl\xC3\xA9 de synchro d\xC3\xA9pos\xC3\xA9" "e"), "le resultat : par code d'invitation, la cle de synchro deposee");
        const auto lines = addedLines(plus, a ? a->id : std::string("?"));
        bool good = lines.size() == 1;
        if (good) {
            Value d;
            good = core::json::parse(lines[0].line.parsed.detail, d, nullptr) && d.isObject() && d.str("par") == "code d'invitation" && d.str("invitation") == invId &&
                   !d.str("invite_par").empty() && d.str("profil") == kProfile && has(lines[0].line.parsed.summary, "code d'invitation") && lines[0].verified;
        }
        check(good, "au journal : << admin_ajouter >> (code d'invitation, invite par, son invitation), signee par l'admin+");
        const auto inv = plus.adminInvitations();
        const auto it = std::find_if(inv.begin(), inv.end(), [&](const gui::AdminInvitation& i) { return i.id == invId; });
        check(it != inv.end() && it->admittedKey == key && it->admittedAt > 0, "l'invitation servie : notee pour sa cle (une seule fois)");
        std::vector<std::string> again;
        const auto r2 = plus.admitInvitedAdmins(&again);
        check(r2.ok && again.empty() && has(r2.message, "Aucun admin inscrit"), "une seconde fois : rien a signer");
    }

    std::printf("-- XPG Admin (Nina) se voit admin, avec ses droits\n");
    {
        Classic classic;
        gui::AdminRefresh x;
        const auto r = nina.refreshAdminState(nullptr, &x);
        nina.reload();
        check(x.admitted && nina.role() == gui::Role::Admin && nina.signer() && nina.myRights() == adm::normalizeRights(kProfileRights) && x.syncKeyReceived,
              "le regard : Nina est admin, ses 2 droits, la cle de synchro recue (" + r.text() + ")");
    }

    std::printf("-- le regard de l'admin+ signe seul (Zoe, des droits choisis un par un)\n");
    {
        gui::Workspace zoe(U(root / "zoe"));
        std::string zcode;
        const std::vector<std::string> rights = {"licence_prolonger"};
        gui::AdminInvitation noted;
        check(plus.inviteAdmin(kZoe, "", "", zcode, &noted, &rights).ok, "une invitation pour Zoe (sans courriel, un droit)");
        {
            Classic classic;
            check(zoe.serviceConfigure(svc.url()).ok, "le poste de Zoe : l'adresse du service");
            check(zoe.joinWithInvitation(zcode).ok, "Zoe tape son code : inscrite");
        }
        online::AdminWatch st;
        check(plus.serviceWatch(st).ok && st.invitedWaiting == 1, "le regard de l'admin+ : 1 inscrit a signer");
        gui::AdminRefresh x;
        const auto r = plus.refreshAdminState(&st, &x);
        plus.reload();
        zoe.reload();
        const adm::Admin* a = plus.adminList() ? plus.adminList()->byKey(zoe.adminKey().keyHex) : nullptr;
        check(x.invitedAdmitted == std::vector<std::string>{kZoe} && x.listAdopted && a && a->active() && a->rights == rights,
              "signee d'office par le regard, son droit (" + r.text() + ")");
        online::AdminWatch st2;
        check(plus.serviceWatch(st2).ok && st2.invitedWaiting == 0, "plus rien a signer");
    }

    std::printf("-- une demande d'avant (XPG Admin 1.50, ses 6 chiffres jamais tapes) devient admin d'office\n");
    {
        std::string pcode;
        check(plus.inviteAdmin(kPaul, "paul@exemple.invalid", kProfile, pcode).ok, "une invitation pour Paul (envoyee par l'Admin+ d'avant)");
        // XPG Admin 1.50 : sa cle, POST /v1/admins/demande avec le code (la reponse d'avant : << attente >>)
        const fs::path pf = root / "paul";
        fs::create_directories(pf, ec);
        std::string why;
        auto key = la::createAdminKey(U(pf), kPaul, &why);
        check(key != nullptr, "la cle de Paul (" + why + ")");
        if (key) {
            online::Options o;
            o.now = plus.now();
            o.tool = "XPG-License-Manager-Admin/1.50.0";
            o.software = "XPG-License-Admin/1.50.0";
            online::Client c(svc.url(), *key, o);
            check(c.connect(&why), "le client de Paul (" + why + ")");
            Value body = Value::object();
            body.set("nom", kPaul);
            body.set("courriel", "paul@exemple.invalid");
            body.set("pc", "PC-PAUL");
            body.set("poste", std::string(32, 'a'));
            body.set("logiciel", "XPG-License-Admin 1.50.0");
            body.set("invitation", pcode);
            const online::Reply r = c.call("POST", "/v1/admins/demande", core::json::write(body, false));
            check(r.status == 201 && r.json.isObject() && r.json.str("etat") == "attente", "la demande d'avant : 201 << attente >> (" + r.body + ")");
            const online::Reply s0 = c.call("GET", "/v1/admins/demande?cle=" + key->publicKeyHex());
            check(s0.status == 200 && s0.json.str("etat") == "attente", "son Admin d'avant lit : << attente >>");
            std::vector<std::string> names;
            const auto ok = plus.admitInvitedAdmins(&names);
            plus.reload();
            const adm::Admin* a = plus.adminList() ? plus.adminList()->byKey(key->publicKeyHex()) : nullptr;
            check(ok.ok && names == std::vector<std::string>{kPaul} && a && a->active() && a->rights == adm::normalizeRights(kProfileRights),
                  "Paul est admin d'office, avec le profil de son invitation (" + ok.text() + ")");
            const online::Reply s1 = c.call("GET", "/v1/admins/demande?cle=" + key->publicKeyHex());
            check(s1.status == 200 && s1.json.str("etat") == "acceptee", "son Admin d'avant lit : << acceptee >> (il est admin)");
            // une demande d'avant SANS code : plus de demande a accepter (410, mettre a jour)
            const online::Reply none = c.call("POST", "/v1/admins/demande", "{\"nom\":\"Paul Essai\",\"courriel\":\"paul@exemple.invalid\",\"poste\":\"" + std::string(32, 'a') + "\"}");
            check(none.status == 409 || none.status == 410, "une demande sans code : refusee (" + std::to_string(none.status) + ")");
        }
    }

    std::printf("-- une inscription dont l'invitation n'a pas ete creee sur ce poste : jamais signee\n");
    {
        std::string icode;
        gui::AdminInvitation noted;
        check(plus.inviteAdmin("Intrus Essai", "", kProfile, icode, &noted).ok, "une invitation...");
        // ... que ce poste oublie (une invitation qu'il n'aurait pas creee : le service seul la connait)
        const fs::path f = root / "plus" / "invitations-admins.json";
        Value v;
        check(core::json::parse(readFile(f), v, nullptr) && v.isObject(), "invitations-admins.json lisible");
        Value kept = Value::array();
        if (const Value* a = v.find("invitations"); a && a->isArray())
            for (const auto& o : a->items())
                if (o.str("id") != noted.id) kept.push(o);
        v.set("invitations", std::move(kept));
        writeFile(f, core::json::write(v, true));
        gui::Workspace intrus(U(root / "intrus"));
        {
            Classic classic;
            check(intrus.serviceConfigure(svc.url()).ok && intrus.joinWithInvitation(icode).ok, "le code sert (le service l'accepte)");
        }
        const int before = plus.adminList() ? plus.adminList()->number : 0;
        std::vector<std::string> names;
        const auto r = plus.admitInvitedAdmins(&names);
        plus.reload();
        check(r.ok && names.empty() && has(r.text(), "n'a pas \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9" "e sur ce poste") && (plus.adminList() ? plus.adminList()->number : 0) == before &&
                  !(plus.adminList() && plus.adminList()->byKey(intrus.adminKey().keyHex)),
              "rien n'est signe, et c'est dit (" + r.text() + ")");
    }

    std::printf("-- les refus : mal ecrit, inconnu (puis retape), deja servi, expire\n");
    {
        gui::Workspace hugo(U(root / "hugo"));
        gui::Workspace camille(U(root / "camille"));
        std::string hugoCode, camilleCode;
        check(plus.inviteAdmin(kHugo, "hugo@exemple.invalid", "", hugoCode).ok, "une invitation pour Hugo");
        check(plus.inviteAdmin(kCamille, "camille@exemple.invalid", "", camilleCode).ok, "une invitation pour Camille");
        Classic classic;
        check(hugo.serviceConfigure(svc.url()).ok && camille.serviceConfigure(svc.url()).ok, "les postes de Hugo et Camille : l'adresse du service");
        bool refused = false;
        const auto bad = hugo.joinWithInvitation("INV-7U", &refused);
        check(!bad.ok && refused && has(bad.message, "INV-") && !hugo.adminKey().present && !fs::exists(root / "hugo" / "cle-admin.cle"),
              "mal ecrit : refuse ici, rien ne part, pas de cle (" + bad.message + ")");
        writeFile(root / "hugo" / "invitation.txt", "INV-0000-0000\n");
        const auto u = hugo.joinWithInvitation(hugo.pendingInvitationCode(), &refused);
        check(!u.ok && refused && has(u.message, "Ce code d'invitation est inconnu."), "inconnu : la phrase du service (" + u.message + ")");
        check(!fs::exists(root / "hugo" / "invitation.txt") && hugo.pendingInvitationCode().empty(), "invitation.txt efface : le code se retape dans le champ");
        const auto again = hugo.joinWithInvitation(hugoCode, &refused);
        hugo.reload();
        check(again.ok && !refused && again.output == "inscrit" && hugo.role() == gui::Role::Pending, "le bon code retape : inscrit (" + again.text() + ")");
        const auto used = camille.joinWithInvitation(code, &refused);
        check(!used.ok && refused && has(used.message, "Ce code d'invitation a d\xC3\xA9j\xC3\xA0 servi."), "deja servi (" + used.message + ")");
        // 49 h plus tard : le code de Camille a expire
        const std::string before = svc.url();
        check(svc.restart(49LL * 3600), "l'horloge du service avancee de 49 h (le meme etat)");
        if (svc.url() != before) check(camille.serviceConfigure(svc.url()).ok, "le service relance sur un autre port");
        const auto late = camille.joinWithInvitation(camilleCode, &refused);
        check(!late.ok && refused && has(late.message, "Ce code d'invitation a expir\xC3\xA9 (48 h)."), "expire (" + late.message + ")");
        camille.reload();
        check(camille.role() == gui::Role::Pending || camille.role() == gui::Role::None, "Camille : pas admin");
    }
    return finish();
}
