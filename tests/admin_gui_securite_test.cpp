// =============================================================================
//  tests/admin_gui_securite_test.cpp - 1.35.0 : les pages << Parametres de
//  securite >> (la cle de l'editeur) et << Service en ligne >> de
//  XPG-License-Manager-Admin, dessinees a blanc
// -----------------------------------------------------------------------------
//  La cle : creee par la page (une cle d'ESSAI, apres la confirmation), sauvegardee
//  (Argon2id sur le vrai fil de Background : les champs vides des le lancement,
//  la phrase jamais montree ; trop faible ou differente : refusee AVANT tout travail
//  lent), verifiee (bonne, mauvaise phrase), restauree dans un second poste,
//  exportee (TestPublisherKey.hpp). Un travail en cours : les boutons gris.
//
//  Le service en ligne (1.35.0, a la place de la synchro GitHub) : l'adresse
//  (refusee, puis normalisee), cle-publique.txt, le kit des Studios refuse pour une
//  cle d'essai ; la cle de synchro du premier poste et son code de secours montre
//  UNE fois (Continuer bloque tant que la case n'est pas cochee, Copier sur un
//  clic), puis seulement apres une confirmation ; hors ligne (un service d'essai
//  ou rien n'ecoute) : chaque refus dit, rien d'invente ; un travail en cours :
//  tout gris. Puis contre le VRAI service local (tests/service_local.hpp ; sans
//  Node ni wrangler : SAUTE) : Verifier, Certifier (l'empreinte montree, Annuler
//  ne signe rien), Synchroniser l'archive, Relever, les compteurs, un second
//  poste par le code de secours qui recoit l'archive.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/ArchiveSync.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/Containers.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace admintest;
namespace archive = license_admin::archive;
namespace online = license_admin::online;

namespace {

// ---------------------------------------------------------------------------
//  les aides
// ---------------------------------------------------------------------------
std::string g_clipboard;   // le presse-papiers des essais

void installServices() {
    installMetrics();
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return static_cast<float>(TextRenderer::chars(t)) * h * 0.5f;
    };
    s.lineHeight = [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; };
    s.clipboardGet = [] { return g_clipboard; };
    s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
    s.surfaceSize = [] { return gfx::Size{1280.f, 800.f}; };
    ui::installPlatformServices(std::move(s));
}

// Tout ce qui a ete dessine pendant l'essai (les secrets n'y sont jamais).
std::vector<std::string> g_painted;

// Dessiner deux fois (une page qui voit un travail commencer ou finir se refait a l'image suivante) ; le texte
// a plat (les lignes coupees remises bout a bout).
std::string draw(ui::Widget& w, TextRenderer& tr, float width = 1100.f, float height = 760.f) {
    (void)paint(w, tr, width, height);
    std::string t = paint(w, tr, width, height);
    g_painted.push_back(t);
    for (auto& c : t)
        if (c == '\n') c = ' ';
    return t;
}

std::string drawMenu(menu::IMenu& m, TextRenderer& tr) {
    std::string t = paintMenu(m, tr);
    g_painted.push_back(t);
    for (auto& c : t)
        if (c == '\n') c = ' ';
    return t;
}

bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    if (!w) return false;
    for (ui::Widget* p = w; p; p = p->parent())
        if (!p->visible()) return false;
    return true;
}

bool enabledW(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->enabled();
}

std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(aucun rapport)"); }

bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }

// Aucun rapport, aucun message du bandeau, aucun dessin ne contient `secret`.
bool nowhere(const std::vector<const HostRig*>& rigs, const std::string& secret) {
    for (const auto* rig : rigs) {
        for (const auto& r : rig->reports)
            if (has(r.text(), secret) || has(r.output, secret)) return false;
        for (const auto& f : rig->flashes)
            if (has(f, secret)) return false;
    }
    for (const auto& t : g_painted)
        if (has(t, secret)) return false;
    return true;
}

// Un travail lent qui attend qu'on le libere (les boutons gris pendant ce temps).
struct Blocker {
    std::atomic<bool> release{false};
    bool start(HostRig& rig) {
        rig.slowOnThread = true;
        return rig.host().runSlow("Essai : un travail long", [this] {
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return gui::OpResult::done("Travail d'essai fini.");
        }, nullptr);
    }
};

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    installServices();
    TextRenderer tr;
    menu::MenuManager menus{menu::MenuFactory{}};
    const std::string phrase = "cheval agrafe batterie correcte";
    std::string why;

    // =========================================================================================== la cle
    Fixture fa(source, "secu-a");
    HostRig rigA(fa);
    auto pa = gui::makeSecurityPage(rigA.host());
    rigA.refreshers.push_back([&] {
        fa.ws->reload();
        pa->refresh();
    });

    std::printf("-- securite : pas encore de cle\n");
    {
        const std::string t = draw(*pa, tr);
        check(pa->title() == "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9", "le titre : " + pa->title());
        check(has(t, "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9") && has(t, "La cl\xC3\xA9 qui signe toutes les licences"), "le titre et la phrase sous lui");
        check(has(t, "Pas encore de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste"), "sans cle : dit");
        check(has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9") && has(t, "Restaurer une sauvegarde"), "les deux choix : creer, restaurer");
        check(shown(*pa, "securite.creer") && shown(*pa, "securite.restaurer") && shown(*pa, "securite.nom") && shown(*pa, "securite.essai"),
              "les champs et les boutons des deux choix");
        check(!shown(*pa, "securite.sauver") && !shown(*pa, "securite.verifier") && !shown(*pa, "securite.exporter"),
              "sans cle : ni sauvegarde, ni verification, ni export");
        check(has(t, "Ce qui prot\xC3\xA8ge ton poste") && has(t, "ne quitte jamais ce PC") && has(t, "jamais livr\xC3\xA9" "e"), "ce qui protege le poste");
        check(find<ui::Checkbox>(*pa, "securite.essai") && !find<ui::Checkbox>(*pa, "securite.essai")->isChecked(), "cle d'essai : decochee par defaut");
        check(find<ui::InputText>(*pa, "securite.restaurer.phrase")->masked(), "la phrase de restauration : masquee");
        check(click(*pa, "securite.dossier") && !rigA.openedFolders.empty() && rigA.openedFolders.back() == fa.ws->folder(), "Ouvrir le dossier");
        check(has(t, "admin"), "le dossier est dit");
    }

    std::printf("-- securite : creer la cle (confirmation)\n");
    {
        check(click(*pa, "securite.creer") && rigA.dialogs.empty() && !lastOk(rigA) && has(lastText(rigA), "nom"),
              "sans nom : refuse, pas de dialogue (" + lastText(rigA) + ")");
        type(*pa, "securite.nom", "Cl\xC3\xA9 d'essai application");
        find<ui::Checkbox>(*pa, "securite.essai")->setState(ui::Checkbox::State::Checked);
        check(click(*pa, "securite.creer") && rigA.dialogs.size() == 1, "Creer la cle... : une confirmation");
        if (!rigA.dialogs.empty()) {
            rigA.dialogs.back()->attach(&menus);
            const std::string d = drawMenu(*rigA.dialogs.back(), tr);
            check(has(d, "UNE SEULE FOIS") && has(d, "compte Windows") && has(d, "sauvegarde") && has(d, "CL\xC3\x89 D'ESSAI"),
                  "la confirmation dit : une fois, protegee pour ce compte, la sauvegarde juste apres, cle d'essai");
            check(click(*rigA.dialogs.back()->widgetRoot(), "securite.creer.confirmation.non") && !fa.ws->key().present, "Annuler : pas de cle");
        }
        check(click(*pa, "securite.creer") && rigA.dialogs.size() == 2, "une seconde fois");
        if (rigA.dialogs.size() == 2) check(click(*rigA.dialogs.back()->widgetRoot(), "securite.creer.confirmation.oui"), "Creer la cle");
        check(fa.ws->key().present && fa.ws->key().test, "la cle d'essai est creee");
        check(lastOk(rigA) && has(lastText(rigA), "Cl\xC3\xA9 de l'\xC3\xA9" "diteur cr\xC3\xA9\xC3\xA9" "e"), "le rapport : " + lastText(rigA));
        const std::string t = draw(*pa, tr);
        check(has(t, fa.ws->key().id) && has(t, fa.ws->key().fingerprint), "l'identifiant et l'empreinte");
        check(has(t, "CL\xC3\x89 D'ESSAI") && has(t, "jamais pour des clients"), "CLE D'ESSAI dit en clair");
        check(has(t, "Protection") && has(t, "Fichier") && has(t, "Dossier"), "protection, fichier, dossier");
        check(!shown(*pa, "securite.creer") && shown(*pa, "securite.sauver") && shown(*pa, "securite.verifier") && shown(*pa, "securite.exporter"),
              "avec la cle : sauvegarder, verifier, exporter");
        check(has(t, "compil\xC3\xA9" "e dans API Studio et IHM Studio"), "l'export dit ce qui est compile dans les Studios");
    }

    std::printf("-- securite : la phrase refusee avant tout travail lent\n");
    {
        for (const char* id : {"securite.sauver.phrase", "securite.sauver.phrase2", "securite.verifier.phrase"})
            check(find<ui::InputText>(*pa, id)->masked(), std::string("champ masque : ") + id);
        const std::size_t asked0 = rigA.asked.size();
        type(*pa, "securite.sauver.phrase", "court");
        type(*pa, "securite.sauver.phrase2", "court");
        std::string t = draw(*pa, tr);
        check(has(t, "Trop faible"), "l'indication : trop faible");
        check(click(*pa, "securite.sauver") && !lastOk(rigA) && has(lastText(rigA), "12 caract"), "trop courte : refusee (" + lastText(rigA) + ")");
        type(*pa, "securite.sauver.phrase", "123456789012345");
        type(*pa, "securite.sauver.phrase2", "123456789012345");
        check(click(*pa, "securite.sauver") && !lastOk(rigA) && has(lastText(rigA), "trop simple"), "que des chiffres : refusee (" + lastText(rigA) + ")");
        type(*pa, "securite.sauver.phrase", phrase);
        type(*pa, "securite.sauver.phrase2", phrase + " !");
        t = draw(*pa, tr);
        check(has(t, "ne sont pas les m\xC3\xAAmes"), "l'indication : pas les memes");
        check(click(*pa, "securite.sauver") && !lastOk(rigA) && has(lastText(rigA), "pas les m\xC3\xAAmes"), "differentes : refusees (" + lastText(rigA) + ")");
        check(rigA.asked.size() == asked0 && !rigA.bg.running(), "rien demande, aucun travail lance");
    }

    std::printf("-- securite : sauvegarder (Argon2id sur un autre fil)\n");
    const fs::path backup = fa.root / "coffre" / "cle-editeur.xpgcle";
    {
        fs::create_directories(backup.parent_path());
        type(*pa, "securite.sauver.phrase", phrase);
        type(*pa, "securite.sauver.phrase2", phrase);
        std::string t = draw(*pa, tr);
        check(has(t, "Phrase acceptable, la m\xC3\xAAme deux fois"), "l'indication : acceptable");
        rigA.slowOnThread = true;
        rigA.picks.push_back(U(backup));
        check(click(*pa, "securite.sauver"), "Sauvegarder la cle...");
        check(!rigA.asked.empty() && rigA.asked.back().save && has(rigA.asked.back().start, "cle-editeur.xpgcle"),
              "enregistrer sous : cle-editeur.xpgcle");
        check(find<ui::InputText>(*pa, "securite.sauver.phrase")->text().empty() && find<ui::InputText>(*pa, "securite.sauver.phrase2")->text().empty(),
              "les champs sont vides des le lancement");
        check(has(rigA.bg.label(), "Sauvegarde"), "le travail lent : " + rigA.bg.label());
        t = draw(*pa, tr);
        if (rigA.bg.running()) {
            check(has(t, "Sauvegarde de la cl\xC3\xA9 en cours"), "la page dit : en cours");
            check(!enabledW(*pa, "securite.sauver") && !enabledW(*pa, "securite.verifier") && !enabledW(*pa, "securite.exporter"),
                  "pendant ce temps : boutons gris");
        }
        check(rigA.finishSlow(), "la sauvegarde finit");
        check(lastOk(rigA) && has(lastText(rigA), "Sauvegarde de la cl\xC3\xA9 \xC3\xA9" "crite"), "le rapport : " + lastText(rigA));
        check(fs::exists(backup) && has(readFile(backup), fa.ws->key().id), "le fichier .xpgcle, pour cette cle");
        t = draw(*pa, tr);
        check(!has(t, "en cours") && enabledW(*pa, "securite.sauver"), "fini : les boutons reviennent");
    }

    std::printf("-- securite : un travail en cours, tout attend\n");
    {
        Blocker blocker;
        check(blocker.start(rigA), "un travail long commence");
        std::string t = draw(*pa, tr);
        check(has(t, "Un travail est en cours"), "la page le dit");
        check(!enabledW(*pa, "securite.sauver") && !enabledW(*pa, "securite.verifier") && !enabledW(*pa, "securite.exporter"), "les boutons sont gris");
        check(!click(*pa, "securite.verifier"), "un clic n'y fait rien");
        blocker.release.store(true);
        check(rigA.finishSlow(), "le travail long finit");
        t = draw(*pa, tr);
        check(enabledW(*pa, "securite.verifier") && !has(t, "Un travail est en cours"), "puis tout revient");
    }

    std::printf("-- securite : verifier une sauvegarde\n");
    {
        const std::size_t asked0 = rigA.asked.size();
        check(click(*pa, "securite.verifier") && !lastOk(rigA) && has(lastText(rigA), "phrase") && rigA.asked.size() == asked0,
              "sans phrase : refuse avant de choisir le fichier");
        type(*pa, "securite.verifier.phrase", "une autre phrase bien longue");
        rigA.picks.push_back(U(backup));
        check(click(*pa, "securite.verifier") && find<ui::InputText>(*pa, "securite.verifier.phrase")->text().empty(), "lancee : le champ est vide");
        check(rigA.finishSlow() && !lastOk(rigA) && has(lastText(rigA), "Refus"), "une mauvaise phrase : refusee (" + lastText(rigA) + ")");
        type(*pa, "securite.verifier.phrase", phrase);
        rigA.picks.push_back(U(backup));
        check(click(*pa, "securite.verifier") && rigA.finishSlow() && lastOk(rigA) && has(lastText(rigA), "Bonne"), "la bonne : " + lastText(rigA));
    }

    std::printf("-- securite : exporter la cle publique\n");
    {
        rigA.slowOnThread = false;
        const fs::path header = fa.root / "TestPublisherKey.hpp";
        rigA.picks.push_back(U(header));
        check(click(*pa, "securite.exporter"), "Exporter la cle publique...");
        check(!rigA.asked.empty() && rigA.asked.back().save && rigA.asked.back().start == "TestPublisherKey.hpp",
              "enregistrer sous : TestPublisherKey.hpp (une cle d'essai)");
        check(lastOk(rigA) && fs::exists(header) && has(readFile(header), fa.ws->key().id), "l'en-tete contient la cle (" + lastText(rigA) + ")");
        check(find<ui::Checkbox>(*pa, "securite.export.garder") != nullptr, "la case : garder les cles du fichier");
    }

    std::printf("-- securite : restaurer sur un autre poste\n");
    {
        Fixture fb(source, "secu-b");
        HostRig rigB(fb);
        auto pb = gui::makeSecurityPage(rigB.host());
        rigB.refreshers.push_back([&] {
            fb.ws->reload();
            pb->refresh();
        });
        std::string t = draw(*pb, tr);
        check(has(t, "Pas encore de cl\xC3\xA9") && shown(*pb, "securite.restaurer"), "le second poste : pas de cle");
        check(click(*pb, "securite.restaurer") && !lastOk(rigB) && rigB.asked.empty(), "sans phrase : refuse avant de choisir");
        rigB.slowOnThread = true;
        type(*pb, "securite.restaurer.phrase", "une autre phrase bien longue");
        rigB.picks.push_back(U(backup));
        check(click(*pb, "securite.restaurer") && find<ui::InputText>(*pb, "securite.restaurer.phrase")->text().empty(), "lancee : le champ est vide");
        check(rigB.finishSlow() && !lastOk(rigB) && !fb.ws->key().present, "une mauvaise phrase : refusee (" + lastText(rigB) + ")");
        type(*pb, "securite.restaurer.phrase", phrase);
        rigB.picks.push_back(U(backup));
        check(click(*pb, "securite.restaurer") && rigB.finishSlow() && lastOk(rigB), "la bonne : " + lastText(rigB));
        check(fb.ws->key().present && fb.ws->key().id == fa.ws->key().id, "la meme cle sur le second poste");
        t = draw(*pb, tr);
        check(has(t, fa.ws->key().id) && shown(*pb, "securite.sauver"), "la page la montre");
        check(nowhere({&rigA, &rigB}, phrase) && nowhere({&rigA, &rigB}, "cheval agrafe"), "la phrase : jamais dans un rapport, un message, un dessin");
    }

    // =========================================================================================== le service en ligne
    Fixture fc(source, "service-c");
    check(fc.ready(), "le poste d'essai : cle et registre");
    {
        const auto l = fc.licence("XPG-2026-00001", "Client d'essai Alpha", "PROFESSIONAL", {fc.request("pc-a", "Poste essai A")});
        check(l.ok, "une licence pour l'archive (" + l.text() + ")");
    }
    HostRig rig(fc);
    auto sp = gui::makeServicePage(rig.host());
    rig.refreshers.push_back([&] {
        fc.ws->reload();
        sp->refresh();
    });
    std::string code;   // le code de secours du premier poste

    std::printf("-- service : rien de regle, la mise en ligne resumee\n");
    {
        const std::string t = draw(*sp, tr, 1100.f, 2400.f);
        check(sp->title() == "Service en ligne" && has(t, "Service en ligne"), "le titre");
        check(has(t, "pas encore r\xC3\xA9gl\xC3\xA9") && has(t, "pas encore sur ce poste") && has(t, "jamais depuis ce poste"), "l'etat : rien de regle");
        check(has(t, "Mettre le service en ligne (une fois)") && has(t, "Node.js") && has(t, "npx wrangler login") && has(t, "deployer.bat") &&
                  has(t, "cle-publique.txt") && has(t, "ServiceAddress.hpp"),
              "les etapes de la mise en ligne (LISEZ-MOI-SERVICE.txt en bref)");
        check(has(t, "ne va jamais sur le service") && has(t, "code d'activation"), "ce qui ne va jamais sur le service");
        check(!enabledW(*sp, "service.verifier") && !enabledW(*sp, "service.certifier") && !enabledW(*sp, "service.licences.relever"),
              "sans adresse : Verifier, Certifier, Relever gris");
        check(shown(*sp, "service.archive.premier") && shown(*sp, "service.archive.secours") && shown(*sp, "service.archive.cle") && !shown(*sp, "service.archive.code") &&
                  !shown(*sp, "service.archive.synchroniser"),
              "l'archive : le choix du poste ; le code seulement pour un autre poste");
        check(click(*sp, "service.adresse.enregistrer") && !lastOk(rig) && has(lastText(rig), "colle d'abord"), "sans adresse : refuse (" + lastText(rig) + ")");
        type(*sp, "service.adresse", "pas une adresse");
        check(click(*sp, "service.adresse.enregistrer") && !lastOk(rig) && !fc.ws->service().configured, "une adresse fausse : refusee (" + lastText(rig) + ")");
        type(*sp, "service.adresse", "http://exemple.invalid");
        check(click(*sp, "service.adresse.enregistrer") && !lastOk(rig) && !fc.ws->service().configured, "http:// ailleurs que sur ce poste : refuse");
    }

    std::printf("-- service : l'adresse (sans reseau)\n");
    {
        type(*sp, "service.adresse", "  HTTPS://XPG-Licences.Essai.Workers.dev/  ");
        check(click(*sp, "service.adresse.enregistrer") && lastOk(rig), "enregistree : " + lastText(rig));
        check(fc.ws->service().ready() && fc.ws->service().config.url == "https://xpg-licences.essai.workers.dev", "en minuscules, sans le / final");
        check(find<ui::InputText>(*sp, "service.adresse")->text() == "https://xpg-licences.essai.workers.dev", "le champ montre l'adresse retenue");
        const std::string t = draw(*sp, tr, 1100.f, 2400.f);
        check(has(t, "pas encore v\xC3\xA9rifi\xC3\xA9") && enabledW(*sp, "service.verifier") && enabledW(*sp, "service.certifier"), "ensuite : Verifier");
        check(!rig.bg.running() && rig.reports.size() >= 1, "rien n'est parti sur le reseau");
    }

    std::printf("-- service : cle-publique.txt (pour PUBLISHER_KEYS) ; le kit des Studios refuse une cle d'essai\n");
    {
        const fs::path dir = fc.root / "mise-en-ligne";
        fs::create_directories(dir);
        rig.picks.push_back(U(dir));
        check(click(*sp, "service.cle-publique") && lastOk(rig), "Ecrire cle-publique.txt : " + lastText(rig));
        check(!rig.asked.empty() && rig.asked.back().folder, "un dossier demande");
        const std::string pub = readFile(dir / "cle-publique.txt");
        check(fc.ws->signingKey() && has(pub, fc.ws->signingKey()->publicKeyHex()) && !has(pub, "PRIV") && has(lastText(rig), "ESSAI"),
              "la cle PUBLIQUE en hexadecimal ; une cle d'essai : dit");
        auto secu = gui::makeSecurityPage(rig.host());
        const std::size_t asked = rig.asked.size();
        check(click(*secu, "securite.kit-studios") && !lastOk(rig) && has(lastText(rig), "ESSAI") && rig.asked.size() == asked,
              "le kit des Studios : une cle d'essai refusee avant de choisir (" + lastText(rig) + ")");
    }

    std::printf("-- service : le premier poste et son code de secours\n");
    {
        check(click(*sp, "service.archive.cle") && lastOk(rig), "Creer la cle de synchro : " + lastText(rig));
        check(fc.ws->service().syncKey && !fc.ws->service().syncKeyId.empty(), "la cle de synchro sur le poste");
        code = fc.ws->syncRecoveryCode(&why);
        check(code.rfind("XPGS1-", 0) == 0, "le code de secours existe");
        check(!has(lastText(rig), code), "le code n'est pas dans le rapport");
        std::string t = draw(*sp, tr, 1100.f, 2400.f);
        check(has(t, code), "le code est montre, en entier");
        check(has(t, "Note-le MAINTENANT hors de ce PC"), "et ce qu'il faut en faire");
        check(!enabledW(*sp, "service.code.continuer") && !click(*sp, "service.code.continuer"), "Continuer : bloque tant que la case n'est pas cochee");
        g_clipboard.clear();
        check(click(*sp, "service.code.copier") && g_clipboard == code, "Copier : le code dans le presse-papiers (sur ce clic)");
        check(!rig.flashes.empty() && !has(rig.flashes.back(), code), "le message ne contient pas le code");
        find<ui::Checkbox>(*sp, "service.code.note")->setState(ui::Checkbox::State::Checked);
        check(enabledW(*sp, "service.code.continuer") && click(*sp, "service.code.continuer"), "coche : Continuer");
        t = draw(*sp, tr, 1100.f, 2400.f);
        check(!has(t, code), "le code n'est plus montre");
        check(has(t, fc.ws->service().syncKeyId) && shown(*sp, "service.archive.synchroniser") && shown(*sp, "service.archive.recevoir") &&
                  !shown(*sp, "service.archive.cle"),
              "l'etat : l'identifiant de la cle ; Synchroniser, Recevoir seulement");
    }

    std::printf("-- service : afficher le code de secours (apres confirmation)\n");
    {
        check(shown(*sp, "service.archive.code.afficher"), "le bouton est la");
        check(click(*sp, "service.archive.code.afficher") && !rig.dialogs.empty(), "une confirmation d'abord");
        if (!rig.dialogs.empty()) {
            rig.dialogs.back()->attach(&menus);
            const std::string d = drawMenu(*rig.dialogs.back(), tr);
            check(has(d, "personne ne regarde ton \xC3\xA9" "cran") && !has(d, code), "la confirmation : pas encore le code");
            check(click(*rig.dialogs.back()->widgetRoot(), "service.code.confirmation.non"), "Annuler");
        }
        std::string t = draw(*sp, tr, 1100.f, 2400.f);
        check(!has(t, code), "annule : pas de code");
        check(click(*sp, "service.archive.code.afficher") && !rig.dialogs.empty() && click(*rig.dialogs.back()->widgetRoot(), "service.code.confirmation.oui"),
              "Afficher le code");
        t = draw(*sp, tr, 1100.f, 2400.f);
        check(has(t, code), "le code est montre");
        check(click(*sp, "service.code.masquer"), "Masquer le code");
        t = draw(*sp, tr, 1100.f, 2400.f);
        check(!has(t, code), "cache de nouveau");
    }

    std::printf("-- service : hors ligne, chaque refus est dit (rien d'invente)\n");
    {
        type(*sp, "service.adresse", "http://127.0.0.1:9");   // un service d'essai sur ce poste ou rien n'ecoute
        check(click(*sp, "service.adresse.enregistrer") && lastOk(rig) && has(lastText(rig), "ESSAI"), "http://127.0.0.1 : un service d'essai, dit");
        rig.slowOnThread = true;
        check(click(*sp, "service.verifier") && has(rig.bg.label(), "Service"), "Verifier : un travail lent (" + rig.bg.label() + ")");
        check(rig.finishSlow() && !lastOk(rig) && has(lastText(rig), "pas de r\xC3\xA9ponse du service"), "hors ligne : refuse (" + lastText(rig) + ")");
        std::string t = draw(*sp, tr, 1100.f, 2400.f);
        check(has(t, "le service n'a pas r\xC3\xA9pondu"), "la page le dit");
        const std::size_t dialogs = rig.dialogs.size();
        check(click(*sp, "service.certifier") && rig.finishSlow() && !lastOk(rig) && rig.dialogs.size() == dialogs, "Certifier hors ligne : refuse, rien a signer");
        check(click(*sp, "service.archive.synchroniser") && rig.finishSlow() && !lastOk(rig) && has(lastText(rig), "Refus"), "Synchroniser hors ligne : " + lastText(rig));
        check(archive::lastSync(fc.ws->folder()).at == 0, "aucune synchro notee");
        check(click(*sp, "service.licences.relever") && rig.finishSlow() && !lastOk(rig), "Relever hors ligne : refuse");
        check(click(*sp, "service.stats") && rig.finishSlow() && !lastOk(rig), "les compteurs hors ligne : refuses");
        t = draw(*sp, tr, 1100.f, 2400.f);
        check(has(t, "Dernier r\xC3\xA9sultat") && has(t, "jamais sur ce poste"), "la page : les derniers refus, aucun releve");
    }

    std::printf("-- service : un travail en cours, tout attend\n");
    {
        Blocker blocker;
        check(blocker.start(rig), "un travail long commence");
        const std::string t = draw(*sp, tr, 1100.f, 2400.f);
        check(has(t, "Un travail est en cours"), "la page le dit");
        check(!enabledW(*sp, "service.verifier") && !enabledW(*sp, "service.certifier") && !enabledW(*sp, "service.archive.synchroniser") &&
                  !enabledW(*sp, "service.archive.recevoir") && !enabledW(*sp, "service.licences.relever") && !enabledW(*sp, "service.adresse.enregistrer"),
              "tout est gris");
        check(!click(*sp, "service.archive.synchroniser"), "un clic n'y fait rien");
        blocker.release.store(true);
        check(rig.finishSlow(), "le travail long finit");
        (void)draw(*sp, tr, 1100.f, 2400.f);
        check(enabledW(*sp, "service.archive.synchroniser") && enabledW(*sp, "service.verifier"), "puis tout revient");
    }

    // ------------------------------------------------------------------------------------- le vrai service, local
    std::printf("-- service : le vrai service local (Verifier, Certifier, Synchroniser, un autre poste)\n");
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    xpg_test::LocalService svc({fc.ws->signingKey() ? fc.ws->signingKey()->publicKeyHex() : std::string()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
    } else {
        check(svc.ready(), "le service demarre (" + svc.error() + ")");
        if (svc.ready()) {
            rig.slowOnThread = true;
            type(*sp, "service.adresse", svc.url());
            check(click(*sp, "service.adresse.enregistrer") && lastOk(rig), "l'adresse du service local : " + lastText(rig));
            check(click(*sp, "service.verifier") && rig.finishSlow() && lastOk(rig), "Verifier : " + lastText(rig));
            std::string t = draw(*sp, tr, 1100.f, 2400.f);
            check(has(t, "il r\xC3\xA9pond") && has(t, "accept\xC3\xA9" "e par le service"), "il repond ; la cle de l'editeur acceptee");
            check(fc.ws->service().hasState && !fc.ws->service().state.certOk && find<ui::Button>(*sp, "service.certifier")->text() == "Certifier la cl\xC3\xA9 du service\xE2\x80\xA6",
                  "pas encore de certificat : Certifier");
            const std::string serviceKey = fc.ws->service().state.serviceKey;
            check(serviceKey.size() == 64 && has(t, online::keyFingerprint(serviceKey)), "la cle du service : son empreinte");

            std::size_t dialogs = rig.dialogs.size();
            check(click(*sp, "service.certifier") && rig.finishSlow() && lastOk(rig) && rig.dialogs.size() == dialogs + 1, "Certifier : la cle lue, une confirmation");
            if (rig.dialogs.size() == dialogs + 1) {
                rig.dialogs.back()->attach(&menus);
                const std::string d = drawMenu(*rig.dialogs.back(), tr);
                check(has(d, online::keyFingerprint(serviceKey)) && has(d, "Compare-la"), "la confirmation montre l'empreinte a comparer");
                check(click(*rig.dialogs.back()->widgetRoot(), "service.certification.non") && !fc.ws->service().state.certOk, "Annuler : rien n'est signe");
            }
            check(click(*sp, "service.certifier") && rig.finishSlow() && rig.dialogs.size() == dialogs + 2 &&
                      click(*rig.dialogs.back()->widgetRoot(), "service.certification.oui"),
                  "Certifier (confirme)");
            check(rig.finishSlow() && lastOk(rig) && has(lastText(rig), "certifi\xC3\xA9" "e jusqu'au"), "la cle du service certifiee : " + lastText(rig));
            check(rig.finishSlow(), "l'etat relu aussitot");
            t = draw(*sp, tr, 1100.f, 2400.f);
            check(fc.ws->service().state.certOk && has(t, "valable jusqu'au") && find<ui::Button>(*sp, "service.certifier")->text() == "Renouveler le certificat\xE2\x80\xA6",
                  "le certificat : valable ; Renouveler");

            check(click(*sp, "service.archive.verifier") && rig.finishSlow() && lastOk(rig) && has(lastText(rig), "envoyer"),
                  "Verifier (sans rien changer) : ce qui partirait : " + lastText(rig));
            check(archive::lastSync(fc.ws->folder()).at == 0, "rien n'a change");
            check(click(*sp, "service.archive.synchroniser") && rig.finishSlow() && lastOk(rig) && has(lastText(rig), "Archive synchronis"),
                  "Synchroniser : " + lastText(rig));
            check(archive::lastSync(fc.ws->folder()).at != 0 && archive::lastSync(fc.ws->folder()).url == svc.url(), "la synchro notee");
            t = draw(*sp, tr, 1100.f, 2400.f);
            check(has(t, "Envoy\xC3\xA9") && has(t, "(version 1)"), "la page : envoye, la version");
            check(click(*sp, "service.archive.verifier") && rig.finishSlow() && lastOk(rig) && has(lastText(rig), "m\xC3\xAAmes fichiers"), "puis a jour : " + lastText(rig));
            check(click(*sp, "service.licences.relever") && rig.finishSlow() && lastOk(rig), "Relever les licences en ligne : " + lastText(rig));
            check(click(*sp, "service.stats") && rig.finishSlow() && lastOk(rig), "les compteurs : " + lastText(rig));
            t = draw(*sp, tr, 1100.f, 2400.f);
            check(!has(t, "jamais sur ce poste") && has(t, "Licences en ligne"), "le releve est note");

            // Un autre poste : la meme cle de l'editeur (restauree), la cle de synchro par le code de secours.
            Fixture fd(source, "service-d");
            {
                const fs::path bk = fc.root / "cle-c.xpgcle";
                const auto saved = fc.ws->backupKey(phrase, U(bk));
                const auto restored = fd.ws->restoreKey(U(bk), phrase);
                check(saved.ok && restored.ok, "(le second poste : la cle de l'editeur restauree) " + restored.text());
            }
            HostRig rigD(fd);
            auto dp = gui::makeServicePage(rigD.host());
            rigD.refreshers.push_back([&] {
                fd.ws->reload();
                dp->refresh();
            });
            (void)draw(*dp, tr, 1100.f, 2400.f);
            find<ui::RadioButton>(*dp, "service.archive.secours")->select();
            (void)draw(*dp, tr, 1100.f, 2400.f);
            check(shown(*dp, "service.archive.code") && find<ui::InputText>(*dp, "service.archive.code")->masked(), "le champ du code : montre, masque");
            check(click(*dp, "service.archive.code.montrer") && !find<ui::InputText>(*dp, "service.archive.code")->masked(), "Montrer : en clair");
            check(click(*dp, "service.archive.code.montrer") && find<ui::InputText>(*dp, "service.archive.code")->masked(), "Masquer : masque");
            check(click(*dp, "service.archive.cle") && !lastOk(rigD), "sans code : refuse");
            type(*dp, "service.archive.code", code);
            check(click(*dp, "service.archive.cle") && lastOk(rigD), "le code de secours : " + lastText(rigD));
            check(fd.ws->service().syncKeyId == fc.ws->service().syncKeyId, "la meme cle de synchro que le premier poste");
            check(find<ui::InputText>(*dp, "service.archive.code")->text().empty(), "le champ du code est vide");
            t = draw(*dp, tr, 1100.f, 2400.f);
            check(!has(t, code), "pas de code montre sur ce poste");
            type(*dp, "service.adresse", svc.url());
            check(click(*dp, "service.adresse.enregistrer") && lastOk(rigD), "l'adresse du service");
            rigD.slowOnThread = true;
            check(click(*dp, "service.archive.recevoir") && rigD.finishSlow() && lastOk(rigD) && has(lastText(rigD), "Re\xC3\xA7u"), "Recevoir seulement : " + lastText(rigD));
            fd.ws->reload();
            check(fd.ws->licence("XPG-2026-00001") != nullptr, "la licence du premier poste est recue");
            t = draw(*dp, tr, 1100.f, 2400.f);
            check(has(t, "Re\xC3\xA7u"), "la page : recu");
            check(nowhere({&rig, &rigD}, phrase), "la phrase : jamais montree");
        }
    }

    std::printf("-- les secrets\n");
    {
        std::size_t paintedWithCode = 0;
        for (const auto& t : g_painted)
            if (has(t, code)) ++paintedWithCode;
        check(paintedWithCode == 2, "le code de secours : dessine seulement deux fois (le premier poste, puis Afficher) : " + std::to_string(paintedWithCode));
        bool inReports = false;
        for (const auto& x : rig.reports) inReports = inReports || has(x.text(), code);
        for (const auto& f : rig.flashes) inReports = inReports || has(f, code);
        check(!inReports, "le code de secours : jamais dans un rapport ni un message");
        check(!foundIn(fc.root, code), "le code de secours : dans aucun fichier du poste");
    }

    return finish();
}
