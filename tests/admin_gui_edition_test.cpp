// =============================================================================
//  tests/admin_gui_edition_test.cpp - 1.49.0 : TROIS EXECUTABLES (docs/ADMINS.md,
//  par. 12) - XPG Admin (l'Admin classique, license_admin::setEdition(Classique)
//  comme le main de XPG-Admin.exe) et l'Admin+ (le defaut), construits ici, dans
//  le meme processus, dessines a blanc :
//   - l'Admin+ sans la cle de l'editeur : seulement << Creer la cle de l'editeur >>
//     et << Restaurer la cle de l'editeur >> (et la phrase du contrat), jamais
//     << Demander a devenir admin >> ;
//   - XPG Admin : jamais la cle de l'editeur (meme posee dans son dossier), ni
//     les entrees, pages et gestes de l'admin+ ; un lien vers l'une d'elles est
//     refuse et dit ; sans cle : le chemin de l'admin (l'invitation) seulement.
//  argv[1] : la racine du depot (le registre d'exemple).
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/Edition.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/menu/MenuManager.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace admintest;
namespace adm = licensing::admins;
namespace la = license_admin;

namespace {

const std::string kCamille = "Camille Essai";

std::string draw(ui::Widget& w, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}

// Toute la pile (la coquille), comme la fenetre.
struct ShellRig {
    menu::MenuManager menus{menu::MenuFactory{}};
    gui::Shell*       shell{nullptr};
    TextRenderer      r;
    explicit ShellRig(const fs::path& folder) {
        auto owned = std::make_unique<gui::Shell>(U(folder), [] { return g_now; });
        shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
    }
    std::string paint(float w = 1400.f, float h = 900.f) {
        static const ui::Theme theme = ui::Theme::byName("Light");
        r.size = {w, h};
        menus.applyPending();
        const menu::FrameContext fc{0.016, 1.0, &theme, r.surfaceSize(), 1.f};
        menus.Update(fc);
        r.beginFrame(theme.color.windowBg);
        menus.Render(r, fc);
        r.endFrame();
        return r.all();
    }
    ui::Widget& root() { return *shell->widgetRoot(); }
};

// Les entrees du menu de l'admin+ seul (le contrat : le journal des admins, les droits, publier une version, les demandes d'admin).
const char* const kPlusPages[] = {"journal", "droits", "versions", "demandes-admins"};

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary) << text;
}
void pinEditor(const fs::path& folder, const lic::SigningKey& editor) {
    writeFile(folder / "cles-editeur.json", "{\"format\":\"xpg-cles-editeur/1\",\"cles\":[\"" + editor.publicKeyHex() + "\"]}\n");
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    installMetrics();
    const std::string sentence = "Ce programme est celui de l'admin+. Un admin installe XPG Admin, re\xC3\xA7u avec son invitation.";

    // ------------------------------------------------------------------------------------------------
    std::printf("-- l'Admin+ sans la cle de l'editeur : Creer, Restaurer, la phrase ; jamais Demander a devenir admin\n");
    {
        check(!la::classique() && std::string(gui::appName()) == "XPG License Manager Admin", "le defaut : l'Admin+ (XPG License Manager Admin)");
        Fixture fx(source, "edition-plus-vide");
        ShellRig rig(fx.admin);
        std::string t = rig.paint();
        check(has(t, sentence), "le tableau de bord : la phrase du contrat");
        check(shown(rig.root(), "tableau.creer-cle") && shown(rig.root(), "tableau.restaurer") && !shown(rig.root(), "tableau.devenir-admin"),
              "le tableau de bord : Creer, Restaurer ; pas Demander a devenir admin");
        check(has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6") && has(t, "Restaurer la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6") &&
                  !has(t, "Demander \xC3\xA0 devenir admin"),
              "les deux seuls gestes, par leur nom");
        rig.shell->navigate("admins");
        t = rig.paint();
        check(rig.shell->currentPageId() == "admins" && has(t, sentence) && shown(rig.root(), "admins.creer-cle") && shown(rig.root(), "admins.restaurer"),
              "Administrateurs : la phrase, Creer, Restaurer");
        check(!shown(rig.root(), "admins.demande.nom") && !shown(rig.root(), "admins.demande.courriel") && !shown(rig.root(), "admins.demande.invitation") &&
                  !has(t, "Demander \xC3\xA0 devenir admin"),
              "Administrateurs : pas le formulaire de la demande d'admin");
        rig.shell->navigate("compte");
        t = rig.paint();
        check(shown(rig.root(), "compte.creer-cle") && !shown(rig.root(), "compte.devenir-admin") && has(t, sentence), "Mon compte : la cle de l'editeur seulement, la phrase");
        rig.shell->navigate("securite");
        (void)rig.paint();
        check(shown(rig.root(), "securite.creer") && shown(rig.root(), "securite.restaurer"), "Parametres de securite : Creer la cle, Restaurer une sauvegarde");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- XPG Admin (Classique) sur un poste qui a la cle de l'editeur : il ne la lit jamais\n");
    Fixture fx(source, "edition-editeur");
    check(fx.ready(), "le poste de l'admin+ (cle d'essai, registre)");
    if (!fx.ws->signingKey()) return finish();
    const std::string editorId = fx.ws->key().id;
    la::setEdition(la::Edition::Classique);
    {
        check(la::classique() && std::string(gui::appName()) == "XPG Admin", "XPG Admin (setEdition, comme le main de XPG-Admin.exe)");
        fx.ws->reload();
        check(!fx.ws->key().present && fx.ws->signingKey() == nullptr && fx.ws->role() == gui::Role::None && has(fx.ws->key().problem, "XPG Admin"),
              "la cle de l'editeur posee la : pas lue, aucun role (" + fx.ws->key().problem + ")");
        const gui::OpResult c = fx.ws->createKey("essai", true);
        const gui::OpResult rk = fx.ws->restoreKey(U(fx.root / "rien.xpgcle"), "une phrase");
        const gui::OpResult rb = fx.ws->restoreBackup(U(fx.root / "rien.xpgsauve"), "une phrase", {}, nullptr);
        check(!c.ok && has(c.message, "XPG Admin ne fait pas cela") && !rk.ok && has(rk.message, "XPG Admin ne fait pas cela") && !rb.ok &&
                  has(rb.message, "XPG Admin ne fait pas cela"),
              "creer, restaurer la cle, restaurer la sauvegarde complete : refuses, dit (" + c.message + ")");
        check(gui::Workspace(U(fx.admin)).role() == gui::Role::None, "un Workspace neuf : pareil");

        ShellRig rig(fx.admin);
        std::string t = rig.paint();
        check(has(t, "XPG Admin") && !has(t, "XPG License Manager") && has(t, "sur invitation"), "le menu : << XPG Admin >>, << sur invitation >>");
        check(rig.shell->title() == "XPG Admin", "le titre : XPG Admin");
        for (const char* id : kPlusPages) {
            auto* nav = find<ui::Button>(rig.root(), std::string("nav.") + id);
            check(!nav || !nav->visible(), std::string("le menu n'a pas ") + id);
            const std::size_t before = rig.shell->lastMessage() ? 1 : 0;
            (void)before;
            rig.shell->navigate(id);
            check(rig.shell->currentPageId() != id, std::string("un lien vers ") + id + " : refuse");
        }
        check(rig.shell->lastMessage() && has(rig.shell->lastMessage()->result.message, "n'est pas dans XPG Admin"),
              "le refus est dit : << ... n'est pas dans XPG Admin >> (" + (rig.shell->lastMessage() ? rig.shell->lastMessage()->result.message : std::string()) + ")");
        rig.shell->navigate("tableau");
        t = rig.paint();
        check(!shown(rig.root(), "tableau.creer-cle") && !shown(rig.root(), "tableau.restaurer") && shown(rig.root(), "tableau.devenir-admin"),
              "le tableau de bord : le chemin de l'admin seulement (ni Creer ni Restaurer)");
        check(!has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur") && has(t, "code d'invitation"), "ni << Creer la cle de l'editeur >> ; l'invitation est dite");
        rig.shell->navigate("admins");
        t = rig.paint();
        check(!shown(rig.root(), "admins.creer-cle") && !shown(rig.root(), "admins.restaurer") && !shown(rig.root(), "admins.ajouter") &&
                  shown(rig.root(), "admins.demande.nom"),
              "Administrateurs : la demande d'admin (l'invitation), ni Creer ni Restaurer ni Ajouter un admin");
        check(!has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur"), "Administrateurs : pas la carte de la cle de l'editeur");
        rig.shell->navigate("compte");
        (void)rig.paint();
        check(!shown(rig.root(), "compte.creer-cle") && shown(rig.root(), "compte.devenir-admin"), "Mon compte : Demander a devenir admin, pas la cle de l'editeur");
        rig.shell->navigate("securite");
        t = rig.paint();
        check(!shown(rig.root(), "securite.creer") && !shown(rig.root(), "securite.restaurer") && !shown(rig.root(), "securite.restauration") &&
                  has(t, "XPG Admin n'utilise jamais la cl\xC3\xA9 de l'\xC3\xA9" "diteur"),
              "Parametres de securite : ni creer, ni restaurer la cle, ni la sauvegarde complete ; dit");
        rig.shell->navigate("service");
        (void)rig.paint();
        check(!shown(rig.root(), "service.certifier"), "Service en ligne : pas de << Certifier la cle du service >>");
    }
    la::setEdition(la::Edition::Plus);
    fx.ws->reload();
    check(fx.ws->key().present && fx.ws->key().id == editorId && fx.ws->role() == gui::Role::Plus, "de retour en Admin+ : la cle de l'editeur relue");

    // ------------------------------------------------------------------------------------------------
    std::printf("-- XPG Admin chez un admin de la liste (Camille Essai) : ses droits, rien de l'admin+\n");
    {
        Fixture cf(source, "edition-camille");
        std::string why;
        auto camille = la::createAdminKey(U(cf.admin), kCamille, &why);
        check(camille != nullptr, "la cle d'admin d'essai de Camille (" + why + ")");
        if (!camille) return finish();
        adm::AdminList l;
        l.number = 2;
        l.issued = g_now - 86400;
        adm::Admin a;
        a.id = camille->keyId();
        a.key = camille->publicKeyHex();
        a.name = kCamille;
        a.pc = "PC-ESSAI-01";
        a.since = g_now - 86400;
        a.profile = "Admin LV1";
        a.rights = adm::normalizeRights({"licence_creer", "demandes"});
        l.admins.push_back(a);
        const lic::SigningKey& editor = *fx.ws->signingKey();
        writeFile(cf.admin / "admins.xpgadm", lic::writeBlock(lic::signBlock(lic::DocKind::Admins, adm::writeAdminList(l), editor.publicKey(),
                                                                             [&editor](std::string_view m) { return editor.sign(m); })));
        pinEditor(cf.admin, editor);
        la::setEdition(la::Edition::Classique);
        cf.ws->reload();
        check(cf.ws->role() == gui::Role::Admin && cf.ws->can("licence_creer"), "Camille : admin, ses droits");
        const gui::OpResult p = cf.ws->publishVersion(U(cf.root / "a.exe"), U(cf.root / "b.exe"), {}, false);
        check(!p.ok && has(p.message, "XPG Admin ne fait pas cela"), "publier une version : refuse, dit (" + p.message + ")");
        ShellRig rig(cf.admin);
        (void)rig.paint();
        for (const char* id : kPlusPages) check(!shown(rig.root(), std::string("nav.") + id), std::string("le menu de Camille n'a pas ") + id);
        check(shown(rig.root(), "nav.admins") && shown(rig.root(), "nav.compte") && shown(rig.root(), "nav.licences"), "il garde Administrateurs, Mon compte, Licences");
        rig.shell->navigate("admins");
        (void)rig.paint();
        check(!shown(rig.root(), "admins.ajouter"), "Administrateurs : pas << Ajouter un admin >>");
        rig.shell->navigate("service");
        (void)rig.paint();
        check(!shown(rig.root(), "service.certifier"), "Service en ligne : pas de << Certifier >>");
        la::setEdition(la::Edition::Plus);
    }
    return finish();
}
