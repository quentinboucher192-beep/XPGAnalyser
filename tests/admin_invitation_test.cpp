// =============================================================================
//  tests/admin_invitation_test.cpp - 1.49.0 : L'INVITATION PAR CODES (docs/ADMINS.md,
//  par. 12) dans l'outil de l'editeur (license_admin/gui/Workspace :
//  WorkspaceAdmins.cpp), contre le VRAI Worker lance sur ce poste
//  (tests/service_local.hpp)
// -----------------------------------------------------------------------------
//  Sans reseau : le code de confirmation (la valeur connue, la meme que l'essai du
//  service : service/test/invitations.test.js), << 123 456 >> ou << 123456 >>, la
//  remise en forme d'un code d'invitation. Puis : l'admin+ cree l'invitation (le code
//  montre une fois, note sans le code, une ligne << admin_inviter >>) ; XPG Admin
//  (l'edition classique : setEdition(Classique)) lit invitation.txt, cree sa cle,
//  envoie la demande avec le code, efface invitation.txt, montre les 6 chiffres ;
//  l'admin+ voit l'invitation de la demande, tape un FAUX code (rien n'est signe :
//  la liste ne bouge pas), puis le bon : XPG Admin se voit admin. Les refus : un code
//  inconnu (invitation.txt efface, le code retape : accepte), deja servi, expire
//  (l'horloge du service avancee de 49 h). Aucune vraie cle, des noms d'essai.
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
int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
std::string digitsOf(const std::string& s) {
    std::string d;
    for (const char c : s)
        if (c >= '0' && c <= '9') d += c;
    return d;
}
// Un autre code que celui-ci (6 chiffres).
std::string otherCode(const std::string& code) {
    std::string d = digitsOf(code);
    d[5] = d[5] == '9' ? '0' : static_cast<char>(d[5] + 1);
    return d.substr(0, 3) + " " + d.substr(3);
}

// XPG Admin (l'Admin classique) le temps d'un appel ; l'Admin+ ensuite (le meme programme d'essai sert les deux).
struct Classic {
    Classic() { la::setEdition(la::Edition::Classique); }
    ~Classic() { la::setEdition(la::Edition::Plus); }
};

const std::string kNina = "Nina Essai";
const std::string kHugo = "Hugo Essai";
const std::string kCamille = "Camille Essai";

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
    std::printf("-- le code de confirmation, le code d'invitation (sans reseau)\n");
    {
        std::string k;
        for (int i = 0; i < 32; ++i) {
            char b[3];
            std::snprintf(b, sizeof b, "%02x", i);
            k += b;
        }
        check(adm::confirmationCode(k) == "763 090", "la valeur connue (la meme que service/test/invitations.test.js) : " + adm::confirmationCode(k));
        check(adm::confirmationMatches(k, "763 090") && adm::confirmationMatches(k, "763090") && adm::confirmationMatches(k, " 763 090 "),
              "<< 763 090 >> ou << 763090 >> : accepte");
        check(!adm::confirmationMatches(k, "763 091") && !adm::confirmationMatches(k, "76309") && !adm::confirmationMatches(k, "7630900") &&
                  !adm::confirmationMatches(k, "763a090") && !adm::confirmationMatches(k, ""),
              "un autre chiffre, 5 ou 7 chiffres, une lettre, rien : refuse");
        check(adm::confirmationCode("pas une cle").empty(), "pas une cle : pas de code");
        check(adm::normalizeInvitationCode("inv-7k3q m9tx") == "INV-7K3Q-M9TX" && adm::normalizeInvitationCode("7K3QM9TX") == "INV-7K3Q-M9TX" &&
                  adm::normalizeInvitationCode("INV-7K3Q-M9TO") == "INV-7K3Q-M9T0",
              "un code d'invitation remis en forme (minuscules, espaces, sans INV, O lu 0)");
        check(adm::normalizeInvitationCode("INV-7K3Q-M9TU").empty() && adm::normalizeInvitationCode("INV-7K3Q").empty() &&
                  adm::normalizeInvitationCode("").empty(),
              "U (hors de l'alphabet), trop court, vide : pas un code");
        check(adm::actionType("admin_inviter") != nullptr, "l'action du journal << admin_inviter >> est connue");
    }

    gui::Workspace plus(U(root / "plus"));
    {
        const auto c = plus.createKey("essai invitation", true);
        check(c.ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai) : l'admin+ (" + c.text() + ")");
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
    }

    std::printf("-- l'admin+ invite Nina (le code montre une fois)\n");
    std::string code, invId;
    {
        std::string none;
        check(!plus.inviteAdmin("", "nina@exemple.invalid", adm::kAllRightsProfile, none).ok && none.empty(), "sans nom : refuse, pas de code");
        gui::AdminInvitation noted;
        const auto r = plus.inviteAdmin(kNina, "nina@exemple.invalid", adm::kAllRightsProfile, code, &noted);
        check(r.ok && adm::normalizeInvitationCode(code) == code && code.size() == 13 && has(r.message, code), "le code d'invitation (" + r.text() + ")");
        invId = noted.id;
        check(invId.rfind("inv-", 0) == 0 && noted.expires > plus.now() + 47 * 3600, "son id, ses 48 h");
        const auto list = plus.adminInvitations();
        check(!list.empty() && list.front().id == invId && list.front().name == kNina, "notee (invitations-admins.json)");
        std::string text;
        {
            std::ifstream f(root / "plus" / "invitations-admins.json", std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(f), {});
        }
        check(!text.empty() && !has(text, code), "le code lui-meme n'est jamais garde sur le poste de l'admin+");
        (void)plus.fetchJournal(nullptr);
        bool seen = false;
        for (const auto& l : plus.journalLines().rows) seen = seen || l.line.parsed.action == "admin_inviter";
        check(seen, "au journal : << admin_inviter >>");
    }

    std::printf("-- XPG Admin (Nina) : invitation.txt, sa cle, sa demande, les 6 chiffres\n");
    gui::Workspace nina(U(root / "nina"));
    std::string ninaCode;
    {
        Classic classic;
        check(nina.serviceConfigure(svc.url()).ok, "le poste de Nina : l'adresse du service");
        check(nina.pendingInvitationCode().empty(), "pas encore d'invitation.txt : le champ la demandera");
        // l'installateur l'ecrit (une ligne ; minuscules, espaces toleres)
        std::string typed = code;
        for (auto& ch : typed) ch = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch == '-' ? ' ' : ch;
        writeFile(root / "nina" / "invitation.txt", typed + "\r\n");
        check(nina.pendingInvitationCode() == code, "invitation.txt lu et remis en forme (" + nina.pendingInvitationCode() + ")");
        bool refused = true;
        const auto r = nina.requestAdminInvited(kNina, "nina@exemple.invalid", nina.pendingInvitationCode(), &refused);
        nina.reload();
        ninaCode = r.output;
        check(r.ok && !refused && nina.adminKey().present && ninaCode == adm::confirmationCode(nina.adminKey().keyHex) && has(r.message, "Dis ce code"),
              "la demande partie : les 6 chiffres a dire (" + r.text() + ")");
        check(!fs::exists(root / "nina" / "invitation.txt"), "invitation.txt efface (le service a repondu)");
        check(nina.role() == gui::Role::Pending && nina.pendingConfirmation() == ninaCode, "en attente : l'ecran montre << " + ninaCode + " >>");
        std::string state;
        check(nina.adminRequestState(state).ok && state == "attente", "l'etat de la demande : attente");
    }

    std::printf("-- l'admin+ : la demande et son invitation ; un faux code, puis le bon\n");
    {
        std::vector<gui::AdminRequest> reqs;
        const auto p = plus.adminRequests(reqs, false);
        const gui::AdminRequest* q = nullptr;
        for (const auto& x : reqs)
            if (x.key == nina.adminKey().keyHex) q = &x;
        check(p.ok && q && q->byCode() && q->invitationId == invId && q->invitationName == kNina && q->invitationProfile == adm::kAllRightsProfile,
              "la demande de Nina montre son invitation (" + (q ? q->invitationId : std::string("?")) + ")");
        const std::string key = nina.adminKey().keyHex;
        const int before = plus.adminList() ? plus.adminList()->number : 0;
        const auto w1 = plus.acceptAdmins({{key, adm::kAllRightsProfile, true, otherCode(ninaCode)}});
        plus.reload();
        check(!w1.ok && has(w1.message, "rien n'est sign") && (plus.adminList() ? plus.adminList()->number : 0) == before,
              "un faux code : rien n'est signe (" + w1.message + ")");
        const auto w2 = plus.acceptAdmin(key, adm::kAllRightsProfile, nullptr, true, {});
        check(!w2.ok && has(w2.message, "6 chiffres"), "l'empreinte cochee ne suffit pas a une demande avec invitation (" + w2.message + ")");
        const auto w3 = plus.acceptAdmin(key, adm::kAllRightsProfile, nullptr, false, otherCode(ninaCode));
        check(!w3.ok && has(w3.message, "rien n'est sign"), "un seul : un faux code, refuse");
        const auto ok = plus.acceptAdmins({{key, adm::kAllRightsProfile, false, digitsOf(ninaCode)}});
        plus.reload();
        const adm::Admin* a = plus.adminList() ? plus.adminList()->byKey(key) : nullptr;
        check(ok.ok && a && a->active() && a->name == kNina && has(ok.text(), "code de confirmation"),
              "le bon code (<< 123456 >> sans espace) : Nina est admin (" + ok.text() + ")");
    }

    std::printf("-- XPG Admin (Nina) se voit admin\n");
    {
        Classic classic;
        gui::AdminRefresh x;
        const auto r = nina.refreshAdminState(nullptr, &x);
        nina.reload();
        check(x.accepted && nina.role() == gui::Role::Admin && nina.signer(), "le regard : acceptee, Nina est admin (" + r.text() + ")");
    }

    std::printf("-- les refus : inconnu (puis retape), deja servi, expire\n");
    {
        gui::Workspace hugo(U(root / "hugo"));
        gui::Workspace camille(U(root / "camille"));
        std::string hugoCode, camilleCode;
        check(plus.inviteAdmin(kHugo, "hugo@exemple.invalid", "", hugoCode).ok, "une invitation pour Hugo");
        check(plus.inviteAdmin(kCamille, "camille@exemple.invalid", "", camilleCode).ok, "une invitation pour Camille");
        Classic classic;
        check(hugo.serviceConfigure(svc.url()).ok && camille.serviceConfigure(svc.url()).ok, "les postes de Hugo et Camille : l'adresse du service");
        bool refused = false;
        const auto bad = hugo.requestAdminInvited(kHugo, "hugo@exemple.invalid", "INV-7U", &refused);
        check(!bad.ok && refused && has(bad.message, "INV-"), "mal ecrit : refuse ici, rien ne part (" + bad.message + ")");
        writeFile(root / "hugo" / "invitation.txt", "INV-0000-0000\n");
        const auto u = hugo.requestAdminInvited(kHugo, "hugo@exemple.invalid", hugo.pendingInvitationCode(), &refused);
        check(!u.ok && refused && has(u.message, "Ce code d'invitation est inconnu."), "inconnu : la phrase du service (" + u.message + ")");
        check(!fs::exists(root / "hugo" / "invitation.txt") && hugo.pendingInvitationCode().empty(), "invitation.txt efface : le code se retape dans le champ");
        const auto again = hugo.requestAdminInvited(kHugo, "hugo@exemple.invalid", hugoCode, &refused);
        hugo.reload();
        check(again.ok && !refused && again.output == adm::confirmationCode(hugo.adminKey().keyHex), "le bon code retape : la demande part (" + again.text() + ")");
        const auto used = camille.requestAdminInvited(kCamille, "camille@exemple.invalid", code, &refused);
        check(!used.ok && refused && has(used.message, "Ce code d'invitation a d\xC3\xA9j\xC3\xA0 servi."), "deja servi (" + used.message + ")");
        // 49 h plus tard : le code de Camille a expire
        const std::string before = svc.url();
        check(svc.restart(49LL * 3600), "l'horloge du service avancee de 49 h (le meme etat)");
        if (svc.url() != before) check(camille.serviceConfigure(svc.url()).ok, "le service relance sur un autre port");
        const auto late = camille.requestAdminInvited(kCamille, "camille@exemple.invalid", camilleCode, &refused);
        check(!late.ok && refused && has(late.message, "Ce code d'invitation a expir\xC3\xA9 (48 h)."), "expire (" + late.message + ")");
    }
    return finish();
}
