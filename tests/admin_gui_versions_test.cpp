// =============================================================================
//  tests/admin_gui_versions_test.cpp - 1.44.0 : LES VERSIONS, L'ENVOI DE L'ADMIN,
//  LA SAUVEGARDE DE SECOURS ET LES ADRESSES DE SECOURS a l'ecran
// -----------------------------------------------------------------------------
//  A blanc (TextRenderer, HostRig) sur un poste Admin d'essai (cle d'ESSAI,
//  registre d'exemple) :
//   - Versions : la page, << Publier une version... >> (deux petits .exe PE
//     fabriques ici, glisses sur la page : leur version lue, << elles
//     concordent >> ; deux versions differentes : dit) ;
//   - Parametres de securite : la sauvegarde de secours (le dossier, la phrase
//     deux fois, masquee, jamais montree ni gardee ; << Activer >> : reglee et
//     faite) ; restaurer une sauvegarde complete dans un poste neuf ;
//   - Service en ligne : une adresse de secours ajoutee, enregistree (le registre
//     signe de nouveau).
//  Puis contre le VRAI service local (sans Node ni wrangler : SAUTE) : publier la
//  version (la barre, le tableau << Versions publiees >>), et << Ajouter un admin >>
//  : << Par un lien >> (l'installateur de l'Admin publie, un lien d'une fois copie).
//  Aucune donnee reelle : des fichiers et des noms d'essai.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace admintest;

namespace {

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string draw(ui::Widget& w, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
std::string drawMenu(menu::IMenu& m) {
    TextRenderer r;
    return paintMenu(m, r, 1400.f, 900.f);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}
bool enabledBtn(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b && b->enabled() && b->visible();
}
bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }
std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(rien)"); }

// ---- un petit .exe (PE32) avec une ressource VS_VERSIONINFO (le meme que tests/admins_fichiers_test.cpp) ----
void put16(std::string& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<char>(v & 0xFF);
    b[o + 1] = static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& b, std::size_t o, std::uint32_t v) {
    put16(b, o, v & 0xFFFF);
    put16(b, o + 2, v >> 16);
}
std::string u16(const std::string& ascii) {
    std::string out;
    for (char c : ascii) {
        out += c;
        out += '\0';
    }
    out += std::string(2, '\0');
    return out;
}
void pad4(std::string& b) {
    while (b.size() % 4) b += '\0';
}
std::string verNode(const std::string& key, const std::string& value, std::uint16_t valueLen, std::uint16_t type, const std::vector<std::string>& children) {
    std::string b(6, '\0');
    b += u16(key);
    pad4(b);
    b += value;
    for (const auto& ch : children) {
        pad4(b);
        b += ch;
    }
    put16(b, 0, static_cast<std::uint32_t>(b.size()));
    put16(b, 2, valueLen);
    put16(b, 4, type);
    return b;
}
std::string strNode(const std::string& key, const std::string& value) {
    return verNode(key, u16(value), static_cast<std::uint16_t>(value.size() + 1), 1, {});
}
std::string buildPe(const std::array<std::uint16_t, 4>& fixed, const std::string& product, const std::string& overlay) {
    std::string fixedInfo(52, '\0');
    put32(fixedInfo, 0, 0xFEEF04BDu);
    put32(fixedInfo, 4, 0x00010000u);
    put32(fixedInfo, 8, (static_cast<std::uint32_t>(fixed[0]) << 16) | fixed[1]);
    put32(fixedInfo, 12, (static_cast<std::uint32_t>(fixed[2]) << 16) | fixed[3]);
    put32(fixedInfo, 16, (static_cast<std::uint32_t>(fixed[0]) << 16) | fixed[1]);
    put32(fixedInfo, 20, (static_cast<std::uint32_t>(fixed[2]) << 16) | fixed[3]);
    put32(fixedInfo, 24, 0x3F);
    put32(fixedInfo, 32, 0x40004);
    put32(fixedInfo, 36, 1);
    std::vector<std::string> strings;
    strings.push_back(strNode("CompanyName", "Editeur d'essai"));
    strings.push_back(strNode("ProductName", "Installateur d'essai"));
    strings.push_back(strNode("ProductVersion", product));
    std::vector<std::string> children;
    children.push_back(verNode("StringFileInfo", "", 0, 1, {verNode("040904b0", "", 0, 1, strings)}));
    std::string translation(4, '\0');
    put16(translation, 0, 0x0409);
    put16(translation, 2, 0x04b0);
    children.push_back(verNode("VarFileInfo", "", 0, 1, {verNode("Translation", translation, 4, 0, {})}));
    const std::string vi = verNode("VS_VERSION_INFO", fixedInfo, 52, 0, children);
    std::string rsrc(0x58, '\0');
    put16(rsrc, 0x0E, 1);
    put32(rsrc, 0x10, 16);
    put32(rsrc, 0x14, 0x80000000u | 0x18);
    put16(rsrc, 0x18 + 0x0E, 1);
    put32(rsrc, 0x28, 1);
    put32(rsrc, 0x2C, 0x80000000u | 0x30);
    put16(rsrc, 0x30 + 0x0E, 1);
    put32(rsrc, 0x40, 0x409);
    put32(rsrc, 0x44, 0x48);
    put32(rsrc, 0x48, 0x1000 + 0x58);
    put32(rsrc, 0x4C, static_cast<std::uint32_t>(vi.size()));
    rsrc += vi;
    const std::size_t rawSize = (rsrc.size() + 0x1FF) & ~static_cast<std::size_t>(0x1FF);
    rsrc.resize(rawSize, '\0');
    std::string pe(0x200, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    put32(pe, 0x3C, 0x40);
    pe.replace(0x40, 4, std::string("PE\0\0", 4));
    put16(pe, 0x44, 0x014C);
    put16(pe, 0x46, 1);
    put16(pe, 0x54, 224);
    put16(pe, 0x56, 0x0102);
    const std::size_t opt = 0x58;
    put16(pe, opt, 0x10B);
    put32(pe, opt + 28, 0x400000);
    put32(pe, opt + 32, 0x1000);
    put32(pe, opt + 36, 0x200);
    put32(pe, opt + 56, 0x1000 + static_cast<std::uint32_t>((rawSize + 0xFFF) & ~static_cast<std::size_t>(0xFFF)));
    put32(pe, opt + 60, 0x200);
    put16(pe, opt + 68, 2);
    put32(pe, opt + 92, 16);
    put32(pe, opt + 96 + 16, 0x1000);
    put32(pe, opt + 96 + 20, 0x58 + static_cast<std::uint32_t>(vi.size()));
    const std::size_t sec = opt + 224;
    pe.replace(sec, 8, std::string(".rsrc\0\0\0", 8));
    put32(pe, sec + 8, static_cast<std::uint32_t>(0x58 + vi.size()));
    put32(pe, sec + 12, 0x1000);
    put32(pe, sec + 16, static_cast<std::uint32_t>(rawSize));
    put32(pe, sec + 20, 0x200);
    put32(pe, sec + 36, 0x40000040u);
    return pe + rsrc + overlay;
}
std::string randomBytes(std::size_t n, std::mt19937_64& rng) {
    std::string s(n, '\0');
    for (auto& c : s) c = static_cast<char>(rng() & 0xFF);
    return s;
}

const std::string kPhrase = "trois chevaux bleus traversent la riviere gelee";

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_versions_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();
    std::mt19937_64 rng(1443);

    Fixture fx(source, "versions-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    const fs::path studios = fx.root / "installateurs" / "API-IHM-Studio-Setup-1.44.1.exe";
    const fs::path adminExe = fx.root / "installateurs" / "XPG-License-Admin-Setup-1.44.1.exe";
    const fs::path adminOther = fx.root / "autres" / "XPG-License-Admin-Setup-1.44.2.exe";
    writeFile(studios, buildPe({1, 44, 1, 0}, "1.44.1", randomBytes(300000, rng)));
    writeFile(adminExe, buildPe({1, 44, 1, 0}, "1.44.1", randomBytes(120000, rng)));
    writeFile(adminOther, buildPe({1, 44, 2, 0}, "1.44.2", randomBytes(1000, rng)));
    HostRig rig(fx);

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Versions : la page, publier (sans le service)\n");
    auto vp = gui::makeVersionsPage(rig.host());
    {
        std::string t = draw(*vp);
        check(has(t, "Versions") && has(t, "Versions publi\xC3\xA9" "es") && has(t, "seul l'admin+ publie") && has(t, "n'est pas r\xC3\xA9gl\xC3\xA9"),
              "la page : les versions publiees ; sans service, c'est dit");
        check(click(*vp, "versions.publier") && shown(*vp, "versions.studios.choisir") && shown(*vp, "versions.admin.choisir"), "Publier une version... : le panneau");
        t = draw(*vp);
        check(has(t, "L'INSTALLATEUR DES STUDIOS") && has(t, "L'INSTALLATEUR DE L'ADMIN") && has(t, "Ce qui va se passer") && has(t, "Importante"),
              "les deux installateurs, ce qui va se passer, Importante");
        check(vp->dropFiles({U(studios), U(adminOther)}), "deux installateurs glisses sur la page");
        t = draw(*vp);
        check(has(t, "version 1.44.1 lue dans le fichier") && has(t, "version 1.44.2 lue dans le fichier") && has(t, "pas de la m\xC3\xAA" "me version"),
              "deux versions differentes : dit");
        check(!enabledBtn(*vp, "versions.signer"), "Signer et publier : grise");
        rig.picks.push_back(U(adminExe));
        check(click(*vp, "versions.admin.choisir"), "Choisir... l'installateur de l'Admin");
        t = draw(*vp);
        check(has(t, "elles concordent") && has(t, "La premi\xC3\xA8re version publi\xC3\xA9" "e") && has(t, "Signer et publier la 1.44.1"), "elles concordent : Signer et publier la 1.44.1");
        check(!enabledBtn(*vp, "versions.signer"), "... grise tant que le service n'est pas regle");
        auto* notes = find<ui::MultiLineText>(*vp, "versions.notes");
        if (notes) notes->setText("\xE2\x80\xA2 Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es.\n\xE2\x80\xA2 Admin : le journal se trie par criticit\xC3\xA9.\n");
        check(notes != nullptr, "le champ des notes");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Service en ligne : une adresse de secours\n");
    {
        auto sp = gui::makeServicePage(rig.host());
        std::string t = draw(*sp, 1100.f, 3000.f);
        check(has(t, "Les adresses de secours") && has(t, "Les Studios 1.43 et avant refusent un registre qui en contient"), "la carte et sa mise en garde");
        const std::uint64_t before = fx.ws->registry().number;
        check(type(*sp, "service.secours.nouvelle", "https://xpg-secours.essai.invalid") && click(*sp, "service.secours.ajouter"), "Ajouter... une adresse");
        t = draw(*sp, 1100.f, 3000.f);
        check(shown(*sp, "service.secours.1") && find<ui::InputText>(*sp, "service.secours.1")->text() == "https://xpg-secours.essai.invalid" &&
                  has(t, "Pas encore enregistr"),
              "dans la liste, pas encore enregistree");
        check(type(*sp, "service.secours.nouvelle", "http://pas-https.invalid") && click(*sp, "service.secours.ajouter") && !lastOk(rig), "http:// : refuse");
        check(enabledBtn(*sp, "service.secours.enregistrer") && click(*sp, "service.secours.enregistrer") && lastOk(rig), "Enregistrer : " + lastText(rig));
        fx.ws->reload();
        const auto f = fx.ws->fallbackServices();
        check(f.size() == 1 && f.front() == "https://xpg-secours.essai.invalid" && fx.ws->registry().number == before + 1, "le registre n. + 1 porte l'adresse de secours");
        sp->refresh();
        check(click(*sp, "service.secours.retirer.1") && click(*sp, "service.secours.enregistrer") && lastOk(rig), "Retirer, enregistrer : " + lastText(rig));
        fx.ws->reload();
        check(fx.ws->fallbackServices().empty(), "plus d'adresse de secours");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Parametres de securite : la sauvegarde de secours\n");
    std::string backupPath;
    {
        auto sp = gui::makeSecurityPage(rig.host());
        std::string t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "Sauvegarde de secours") && has(t, "pas encore r\xC3\xA9gl\xC3\xA9" "e") && has(t, "Restaurer une sauvegarde compl\xC3\xA8te"), "la carte : pas encore reglee");
        auto* p1 = find<ui::InputText>(*sp, "securite.sauvegarde.phrase");
        check(p1 && p1->masked() && find<ui::InputText>(*sp, "securite.sauvegarde.phrase2")->masked(), "la phrase : masquee, deux fois");
        rig.picks.push_back(U(fx.root / "sauvegardes"));
        check(click(*sp, "securite.sauvegarde.choisir") && find<ui::InputText>(*sp, "securite.sauvegarde.dossier")->text() == U(fx.root / "sauvegardes"), "Choisir le dossier...");
        type(*sp, "securite.sauvegarde.phrase", "court");
        t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "Trop faible"), "une phrase trop faible : dit");
        type(*sp, "securite.sauvegarde.phrase", kPhrase);
        type(*sp, "securite.sauvegarde.phrase2", kPhrase);
        t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "Phrase acceptable, la m\xC3\xAA" "me deux fois") && !has(t, kPhrase), "acceptable ; jamais dessinee");
        check(click(*sp, "securite.sauvegarde.activer") && lastOk(rig), "Activer la sauvegarde de chaque jour : " + lastText(rig));
        check(find<ui::InputText>(*sp, "securite.sauvegarde.phrase")->text().empty() && find<ui::InputText>(*sp, "securite.sauvegarde.phrase2")->text().empty(),
              "les champs de la phrase : vides");
        const auto s = fx.ws->backupSettings();
        check(s.configured && s.last > 0 && fs::exists(fs::path(s.lastPath)), "reglee, et la premiere sauvegarde faite");
        backupPath = s.lastPath;
        sp->refresh();
        t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "r\xC3\xA9gl\xC3\xA9" "e : chaque jour, les 30 derni\xC3\xA8res gard\xC3\xA9" "es") && has(t, "Derni\xC3\xA8re"), "l'etat : reglee, la derniere");
        check(click(*sp, "securite.sauvegarde.maintenant") && lastOk(rig) && !rig.last()->output.empty(), "Sauvegarder maintenant : " + lastText(rig));
        check(!fx.ws->backupDueToday(), "celle du jour est faite");
        bool leaked = false;
        for (const auto& r : rig.reports) leaked = leaked || has(r.text(), kPhrase);
        for (const auto& f : rig.flashes) leaked = leaked || has(f, kPhrase);
        check(!leaked && !foundIn(fx.admin, kPhrase), "la phrase : dans aucun message, aucun fichier du poste");
    }
    std::printf("-- restaurer la sauvegarde complete dans un poste neuf\n");
    {
        Fixture neuf(source, "versions-neuf");
        HostRig nrig(neuf);
        auto sp = gui::makeSecurityPage(nrig.host());
        std::string t = draw(*sp, 1100.f, 3600.f);
        check(neuf.ws->role() == gui::Role::None && has(t, "Restaurer une sauvegarde compl\xC3\xA8te") && !has(t, "Sauvegarde de secours (si"),
              "un poste neuf : la restauration, pas la sauvegarde");
        type(*sp, "securite.restauration.phrase", kPhrase);
        nrig.picks.push_back(backupPath);
        const std::size_t d0 = nrig.dialogs.size();
        check(click(*sp, "securite.restauration") && nrig.dialogs.size() == d0 + 1, "le fichier choisi : ce qu'il contient, une confirmation");
        if (nrig.dialogs.size() == d0 + 1) {
            const std::string d = drawMenu(*nrig.dialogs.back());
            check(has(d, "Sauvegarde compl\xC3\xA8te du") && has(d, fx.ws->key().id), "la confirmation : la date, la cle de l'editeur");
            check(click(*nrig.dialogs.back()->widgetRoot(), "securite.restauration.confirmation.oui") && lastOk(nrig), "restauree : " + lastText(nrig));
        }
        neuf.ws->reload();
        check(neuf.ws->key().present && neuf.ws->key().id == fx.ws->key().id && neuf.ws->role() == gui::Role::Plus, "la meme cle de l'editeur : l'admin+");
        check(find<ui::InputText>(*sp, "securite.restauration.phrase")->text().empty(), "le champ de la phrase : vide");
    }

    // ------------------------------------------------------------------------------------------------ le vrai service local
    std::printf("-- le service local : publier la version, l'Admin par un lien\n");
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    xpg_test::LocalService svc({fx.ws->signingKey() ? fx.ws->signingKey()->publicKeyHex() : std::string()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    check(svc.ready(), "le service demarre (" + svc.error() + ")");
    if (!svc.ready()) return finish();
    check(fx.ws->serviceConfigure(svc.url()).ok, "l'adresse du service");
    {
        const auto c = fx.ws->serviceCertify({});
        check(c.ok, "la cle du service certifiee (" + c.text() + ")");
        fx.ws->reload();
    }
    {
        vp->refresh();
        check(click(*vp, "versions.relever") && lastOk(rig), "Relever : " + lastText(rig));
        std::string t = draw(*vp);
        check(has(t, "Aucune version publi\xC3\xA9" "e"), "aucune version publiee encore");
        if (!find<ui::Button>(*vp, "versions.studios.choisir")->visible()) (void)click(*vp, "versions.publier");
        (void)vp->dropFiles({U(studios), U(adminExe)});
        t = draw(*vp);
        check(enabledBtn(*vp, "versions.signer") && has(t, "elles concordent"), "Signer et publier la 1.44.1 : possible");
        check(click(*vp, "versions.signer") && lastOk(rig), "publiee : " + lastText(rig));
        t = draw(*vp);
        check(has(t, "1.44.1") && has(t, "actuelle") && has(t, "tout le monde") && has(t, "les admins") && has(t, "Admins :"), "le tableau : 1.44.1 actuelle, pour qui, les admins");
        check(!shown(*vp, "versions.signer"), "le panneau se referme");
    }
    {
        auto dlg = gui::makeAddAdminDialog(rig.host());
        (void)dlg->Initialize();
        ui::Widget& root = *dlg->widgetRoot();
        (void)drawMenu(*dlg);
        check(shown(root, "admins.ajouter.pc") && shown(root, "admins.ajouter.envoyer"), "dans les notifications de son Studio : le PC, Envoyer");
        auto* byLink = find<ui::RadioButton>(root, "admins.ajouter.envoi.lien");
        if (byLink) byLink->select();
        (void)drawMenu(*dlg);
        check(shown(root, "admins.ajouter.lien.creer") && !shown(root, "admins.ajouter.pc"), "par un lien : Creer le lien");
        check(click(root, "admins.ajouter.lien.creer") && lastOk(rig), "le lien (l'installateur de l'Admin publie) : " + lastText(rig));
        const std::string d = drawMenu(*dlg);
        check(has(d, "Lien (48 h, une seule fois)") && has(d, "/v1/fichiers/lien?jeton=") && has(d, "Le lien est pr\xC3\xAAt"), "le lien montre ; l'etape 2 le dit");
        g_copied.clear();
        check(click(root, "admins.ajouter.lien.copier") && has(g_copied, "/v1/fichiers/lien?jeton="), "Copier le lien : dans le presse-papiers");
    }
    return finish();
}
