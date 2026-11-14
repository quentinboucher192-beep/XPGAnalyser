// =============================================================================
//  tests/admin_gui_edition152.inl - 1.52.0 : CE QUE VOIT UN ADMIN DANS XPG ADMIN
//  (inclus par admin_gui_edition_test.cpp ; gui/ClassicView.hpp, docs/ADMINS.md
//  par. 1 et 12)
// -----------------------------------------------------------------------------
//   - XPG Admin sans derogation (un admin de la liste, et un poste pas encore
//     admin) : le menu de la gestion, exactement ; ni Parametres de securite, ni
//     Service en ligne, ni Administrateurs (ni le journal, les droits, les
//     versions) ; un lien vers l'une d'elles est refuse et dit ; la pastille de la
//     cle et la carte du service menent a Mon compte et a << Ce PC >> ;
//   - Mon compte : la carte << Ton code d'invitation >> (un code refuse : la phrase,
//     le code se retape), << Ce PC >> (l'adresse, l'archive, le mail ; rien de ce
//     qui gere le service) ;
//   - une derogation de l'admin+ qui court (forgee ici : Workspace::adoptDerogation,
//     sans le service) : le menu de l'admin+, tout entier, tout de suite ; les
//     gestes de la cle de l'editeur gris avec leur raison ; elle finit pendant
//     qu'une page qu'elle ouvrait est montree : retour a Mon compte, et c'est dit ;
//   - l'Admin+ : son menu ne change pas.
//  Aucun code (d'invitation, de derogation) n'est jamais ecrit dans un message.
// =============================================================================

namespace v152 {

// Le menu de la gestion (XPG Admin sans derogation), dans l'ordre du menu.
const std::vector<std::string> kManagement = {"tableau",    "statistiques", "licences", "clients",  "activations", "editions",    "options",
                                             "fonctionnalites", "flottantes", "boutique", "commandes", "compte",      "revocations", "historique"};
const char* const kHidden[] = {"securite", "service", "admins", "journal", "droits", "versions"};
const std::string kEditorOnly = "Sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur : l'admin+ seul, m\xC3\xAAme sous d\xC3\xA9rogation.";

std::vector<std::string> menuOf(ShellRig& rig) {
    std::vector<std::string> out;
    for (const auto& e : gui::Shell::entries())
        if (shown(rig.root(), std::string("nav.") + e.id)) out.push_back(e.id);
    return out;
}
std::vector<std::string> allEntries() {
    std::vector<std::string> out;
    for (const auto& e : gui::Shell::entries()) out.push_back(e.id);
    return out;
}
std::string joined(const std::vector<std::string>& v) {
    std::string t;
    for (const auto& s : v) t += (t.empty() ? "" : ",") + s;
    return t;
}
std::string lastText(ShellRig& rig) { return rig.shell->lastMessage() ? rig.shell->lastMessage()->result.message : std::string(); }
bool tipIs(ui::Widget& root, std::string_view id, const std::string& text) {
    auto* w = root.findById(id);
    return w && w->tooltip() == text;
}
bool enabledW(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->enabled();
}
bool clickShown(ui::Widget& root, std::string_view id) { return shown(root, id) && click(root, id); }

// Une derogation de l'admin+ pour cette cle, activee (le code tape), qui court de `start` a `end` (l'heure de g_now).
gui::Derogation forged(const std::string& keyHex, const std::string& name, lic::Seconds start, lic::Seconds end) {
    gui::Derogation d;
    d.known = true;
    d.id = "0123456789abcdef0123456789abcdef";
    d.adminKey = keyHex;
    d.adminName = name;
    d.by = std::string(64, 'a');
    d.start = start;
    d.end = end;
    d.state = "active";
    d.serverNow = g_now;
    d.readAt = g_now;
    d.format = 2;
    d.right = "tout";
    d.duration = end - start;
    return d;
}

// Les refus d'un lien (navigate, --page) vers une page qui s'ouvre avec une derogation.
void refusals(ShellRig& rig, const std::string& who) {
    rig.shell->navigate("tableau");
    for (const char* id : kHidden) {
        rig.shell->navigate(id);
        (void)rig.paint();
        const gui::Shell::NavEntry* e = gui::Shell::entry(id);
        const std::string want = "\xC2\xAB " + std::string(e ? e->label : id) + " \xC2\xBB s'ouvre avec une d\xC3\xA9rogation de l'admin+.";
        check(rig.shell->currentPageId() == "tableau" && has(lastText(rig), want), who + " : un lien vers " + id + " est refuse et dit (" + lastText(rig) + ")");
    }
    rig.shell->navigate("service");
    check(has(lastText(rig), "Mon compte \xE2\x80\xBA Ce PC"), who + " : le refus de Service en ligne dit ou est l'adresse, l'archive, le mail (Ce PC)");
    rig.shell->navigate("admins");
    check(has(lastText(rig), "ton code d'invitation : Mon compte"), who + " : le refus d'Administrateurs dit Mon compte");
}

void run(const std::string& source, Fixture& fx) {
    const lic::SigningKey* editorPtr = fx.ws->signingKey();
    check(editorPtr != nullptr, "1.52.0 : la cle d'essai de l'editeur (le poste de l'admin+)");
    if (!editorPtr) return;
    const lic::SigningKey& editor = *editorPtr;

    // ---------------------------------------------------------------------------------------- l'Admin+ ne change pas
    std::printf("-- 1.52.0 : l'Admin+ - son menu ne change pas (avec sa cle ; sans elle)\n");
    {
        la::setEdition(la::Edition::Plus);
        fx.ws->reload();
        ShellRig rig(fx.admin);
        (void)rig.paint();
        check(rig.shell->workspace().role() == gui::Role::Plus && joined(menuOf(rig)) == joined(allEntries()),
              "l'Admin+ (sa cle) : tout le menu (" + joined(menuOf(rig)) + ")");
        rig.shell->navigate("service");
        (void)rig.paint();
        check(rig.shell->currentPageId() == "service", "l'Admin+ : Service en ligne s'ouvre");
        check(click(rig.root(), "shell.cle") && rig.shell->currentPageId() == "securite", "l'Admin+ : la pastille de la cle -> Parametres de securite");
        Fixture empty(source, "edition152-plus-vide");
        ShellRig none(empty.admin);
        (void)none.paint();
        std::vector<std::string> want;
        for (const auto& id : allEntries())
            if (id != "journal" && id != "versions") want.push_back(id);
        check(none.shell->workspace().role() == gui::Role::None && joined(menuOf(none)) == joined(want),
              "l'Admin+ sans cle : le menu d'avant (ni journal ni versions ; Administrateurs, Service en ligne, Parametres de securite) (" + joined(menuOf(none)) + ")");
        none.shell->navigate("journal");
        check(none.shell->currentPageId() != "journal" && has(lastText(none), "r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+"), "l'Admin+ sans cle : le journal reste refuse comme avant");
    }

    // ---------------------------------------------------------------------- XPG Admin, pas encore admin (role None)
    std::printf("-- 1.52.0 : XPG Admin pas encore admin - le menu de la gestion, la carte du code dans Mon compte\n");
    la::setEdition(la::Edition::Classique);
    {
        Fixture nf(source, "edition152-none");
        nf.ws->reload();
        ShellRig rig(nf.admin);
        std::string t = rig.paint();
        check(rig.shell->workspace().role() == gui::Role::None && joined(menuOf(rig)) == joined(kManagement),
              "pas encore admin : le menu de la gestion, exactement (" + joined(menuOf(rig)) + ")");
        check(has(t, "PILOTAGE") && has(t, "CATALOGUE") && has(t, "VENTE EN LIGNE") && has(t, "S\xC3\x89" "CURIT\xC3\x89"), "les quatre groupes ont un titre (aucun n'est vide)");
        refusals(rig, "pas encore admin");
        check(click(rig.root(), "shell.cle") && rig.shell->currentPageId() == "compte", "la pastille de la cle -> Mon compte");
        t = rig.paint();
        check(shown(rig.root(), "compte.invitation.code") && shown(rig.root(), "compte.invitation.valider") && has(t, "Ton code d'invitation"),
              "Mon compte : la carte << Ton code d'invitation >> (le champ, Devenir admin)");
        check(!enabledW(rig.root(), "compte.invitation.valider") && shown(rig.root(), "compte.invitation.service") && has(t, "Il faut d'abord l'adresse du service"),
              "sans l'adresse du service : Devenir admin gris, << Regler le service... >>");
        check(!shown(rig.root(), "compte.devenir-admin") && !shown(rig.root(), "compte.creer-cle"), "ni << Mon code d'invitation... >> (la carte est la), ni la cle de l'editeur");
        check(clickShown(rig.root(), "compte.invitation.service") && rig.shell->currentPageId() == "poste", "Regler le service... -> Mon compte > Ce PC");
        t = rig.paint();
        check(has(t, "Ce PC : le service, l'archive, le mail") && shown(rig.root(), "poste.adresse") && enabledW(rig.root(), "poste.adresse.enregistrer") &&
                  shown(rig.root(), "poste.connexion.utiliser"),
              "Ce PC : l'adresse a taper (pas encore admin), << Utiliser une connexion... >>");
        check(!shown(rig.root(), "poste.deployer") && !shown(rig.root(), "poste.certifier") && !shown(rig.root(), "poste.repartir") &&
                  !shown(rig.root(), "poste.secours.ajouter") && !has(t, "Mettre le service en ligne") && has(t, "Le mail au client"),
              "Ce PC : rien de ce qui gere le service (ni Deployer, ni Certifier, ni secours, ni Repartir) ; le mail au client");
        check(type(rig.root(), "poste.adresse", "http://127.0.0.1:9") && click(rig.root(), "poste.adresse.enregistrer") && rig.shell->workspace().service().configured,
              "Ce PC : l'adresse enregistree (" + lastText(rig) + ")");
        // un code refuse (le service ne repond pas, ou le code de l'installateur refuse au premier lancement) : la phrase, il se retape
        rig.shell->navigate("compte");
        (void)rig.paint();
        check(enabledW(rig.root(), "compte.invitation.valider"), "l'adresse reglee : Devenir admin s'ouvre");
        check(type(rig.root(), "compte.invitation.code", "INV-0000-0000") && click(rig.root(), "compte.invitation.valider") && rig.shell->finishSlow(),
              "Devenir admin : le code part (le service ne repond pas ici)");
        t = rig.paint();
        bool logged = false;   // le code tape n'est dans aucun message de la seance
        for (const auto& l : rig.shell->sessionLog()) {
            logged = logged || has(l.result.message, "INV-0000-0000");
            for (const auto& d : l.result.details) logged = logged || has(d, "INV-0000-0000");
        }
        check(rig.shell->workspace().role() != gui::Role::Admin && shown(rig.root(), "compte.invitation.code") && shown(rig.root(), "compte.invitation.valider"),
              "refuse : la carte reste, le code se retape (" + lastText(rig) + ")");
        check(!logged, "le code tape n'est dans aucun message de la seance");
        gui::requestPageFocus("compte.invitation", "code-refuse:ce code d'invitation est inconnu");   // Shell::autoJoin, premier lancement
        rig.shell->navigate("tableau");
        rig.shell->navigate("compte");
        t = rig.paint();
        check(has(t, "Ce code d'invitation est inconnu.") && shown(rig.root(), "compte.invitation.code") && shown(rig.root(), "compte.invitation.valider"),
              "le code de l'installateur refuse : Mon compte le dit, le code se retape");
        check(clickShown(rig.root(), "nav.tableau") && clickShown(rig.root(), "tableau.devenir-admin") && rig.shell->currentPageId() == "compte",
              "le tableau de bord : << Mon code d'invitation... >> -> Mon compte");
    }

    // ------------------------------------------------- XPG Admin, une cle d'admin pas encore dans la liste (role Pending)
    std::printf("-- 1.52.0 : XPG Admin, une cle pas encore admise - la carte du code, puis << Ton ajout >>, dans Mon compte\n");
    {
        Fixture pf(source, "edition152-attente");
        std::string err;
        auto nina = la::createAdminKey(U(pf.admin), "Nina Essai", &err);
        pf.ws->reload();
        check(nina && pf.ws->role() == gui::Role::Pending && !pf.ws->joined(), "une cle d'admin, sans inscription (" + err + ")");
        ShellRig rig(pf.admin);
        (void)rig.paint();
        check(joined(menuOf(rig)) == joined(kManagement), "en attente : le menu de la gestion (" + joined(menuOf(rig)) + ")");
        check(click(rig.root(), "shell.cle") && rig.shell->currentPageId() == "compte", "la pastille (code a taper) -> Mon compte");
        std::string t = rig.paint();
        check(shown(rig.root(), "compte.invitation.code") && has(t, "Ton code d'invitation") && !shown(rig.root(), "compte.attente.verifier") &&
                  !shown(rig.root(), "compte.attente"),
              "sans inscription : la carte du code (pas encore << Ton ajout >>)");
        writeFile(pf.admin / "inscription-admin.json", "{\"format\":\"xpg-inscription-admin/1\",\"nom\":\"Nina Essai\",\"inscrit\":" + std::to_string(g_now - 60) + "}\n");
        rig.shell->reloadAll();
        t = rig.paint();
        check(rig.shell->workspace().joined() && shown(rig.root(), "compte.attente.verifier") && has(t, "Ton ajout comme admin") &&
                  !shown(rig.root(), "compte.invitation.code"),
              "inscrite : << Ton ajout comme admin >>, << Verifier maintenant >> (plus le champ du code)");
    }

    // --------------------------------------------------------------------------- XPG Admin, un admin (Camille Essai)
    std::printf("-- 1.52.0 : XPG Admin chez une admin (Camille Essai) - la gestion ; Ce PC ; la derogation ouvre tout\n");
    Fixture cf(source, "edition152-camille");
    std::string why;
    auto camille = la::createAdminKey(U(cf.admin), kCamille, &why);
    check(camille != nullptr, "la cle d'admin d'essai de Camille (" + why + ")");
    if (!camille) return;
    {
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
        writeFile(cf.admin / "admins.xpgadm", lic::writeBlock(lic::signBlock(lic::DocKind::Admins, adm::writeAdminList(l), editor.publicKey(),
                                                                             [&editor](std::string_view m) { return editor.sign(m); })));
        pinEditor(cf.admin, editor);
    }
    cf.ws->reload();
    ShellRig rig(cf.admin);
    std::string t = rig.paint();
    gui::Workspace& w = rig.shell->workspace();
    check(w.role() == gui::Role::Admin, "Camille : admin");
    check(joined(menuOf(rig)) == joined(kManagement), "sans derogation : le menu de la gestion, exactement (" + joined(menuOf(rig)) + ")");
    check(has(t, "S\xC3\x89" "CURIT\xC3\x89"), "le groupe Securite garde son titre (Mon compte, Revocations, Historique)");
    refusals(rig, "Camille");
    check(click(rig.root(), "shell.cle") && rig.shell->currentPageId() == "compte", "la pastille de la cle (Admin : Camille) -> Mon compte");
    check(has(rig.root().findById("shell.cle")->tooltip(), "Clic : Mon compte."), "son infobulle le dit : Clic : Mon compte.");
    rig.shell->navigate("tableau");
    check(click(rig.root(), "shell.synchro") && rig.shell->currentPageId() == "poste", "la pastille du service -> Mon compte > Ce PC");
    rig.shell->navigate("compte");
    t = rig.paint();
    check(shown(rig.root(), "compte.ce-pc"), "Mon compte (admin) : << Ce PC... >>");
    check(!shown(rig.root(), "compte.invitation.code"), "Mon compte (admin) : pas la carte du code (deja admin)");
    check(clickShown(rig.root(), "compte.ce-pc") && rig.shell->currentPageId() == "poste", "Ce PC... -> la sous-page de Mon compte");
    t = rig.paint();
    check(has(t, "L'archive en ligne") && has(t, "elle arrive par le trousseau") && has(t, "Le mail au client") && shown(rig.root(), "poste.mail.enregistrer"),
          "Ce PC (admin) : l'archive (la cle de synchro par le trousseau), le mail au client");
    check(shown(rig.root(), "poste.adresse") && !enabledW(rig.root(), "poste.adresse.enregistrer") && !shown(rig.root(), "poste.deployer") &&
              !shown(rig.root(), "poste.certifier"),
          "Ce PC (admin) : l'adresse se lit (l'admin+ la change), ni Deployer ni Certifier");
    check(rig.shell->pageTitle() == "Ce PC : le service, l'archive, le mail", "son titre : " + rig.shell->pageTitle());

    // ---- la derogation de l'admin+ : activee (le code tape), elle court ----
    std::printf("-- 1.52.0 : une derogation de l'admin+ qui court - le menu de l'admin+, les gestes de sa cle gris\n");
    rig.shell->navigate("licences");
    w.adoptDerogation(forged(camille->publicKeyHex(), kCamille, g_now - 60, g_now + 3600));
    t = rig.paint();
    check(joined(menuOf(rig)) == joined(allEntries()), "la derogation : le menu de l'admin+, tout entier, tout de suite (" + joined(menuOf(rig)) + ")");
    for (const char* id : kHidden) {
        rig.shell->navigate(id);
        (void)rig.paint();
        check(rig.shell->currentPageId() == id, std::string("sous derogation : ") + id + " s'ouvre");
    }
    rig.shell->navigate("admins");
    t = rig.paint();
    check(shown(rig.root(), "admins.ajouter") && !enabledW(rig.root(), "admins.ajouter") && tipIs(rig.root(), "admins.ajouter", kEditorOnly),
          "Administrateurs : << Ajouter un admin... >> gris, sa raison (la cle de l'editeur)");
    rig.shell->navigate("droits");
    (void)rig.paint();
    check(!enabledW(rig.root(), "droits.nouveau") && tipIs(rig.root(), "droits.nouveau", kEditorOnly) && !enabledW(rig.root(), "droits.publier") &&
              tipIs(rig.root(), "droits.publier", kEditorOnly),
          "Droits : Nouveau profil, Signer et publier gris, leur raison");
    rig.shell->navigate("versions");
    t = rig.paint();
    check(!enabledW(rig.root(), "versions.publier") && tipIs(rig.root(), "versions.publier", kEditorOnly) && !has(t, "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+."),
          "Versions : elle se lit (plus << Reserve a l'admin+ >>) ; Publier une version gris, sa raison");
    rig.shell->navigate("journal");
    (void)rig.paint();
    check(!enabledW(rig.root(), "journal.criticite") && tipIs(rig.root(), "journal.criticite", kEditorOnly), "Journal des admins : la criticite grise, sa raison");
    rig.shell->navigate("service");
    t = rig.paint();
    check(!shown(rig.root(), "service.certifier") && !shown(rig.root(), "service.derogation.accorder") && !shown(rig.root(), "service.repartir") &&
              enabledW(rig.root(), "service.adresse.enregistrer"),
          "Service en ligne : ce que la derogation permet (l'adresse) ; ni Certifier, ni Accorder une derogation, ni Repartir de zero");
    rig.shell->navigate("securite");
    t = rig.paint();
    check(!shown(rig.root(), "securite.creer") && !shown(rig.root(), "securite.restaurer") && !shown(rig.root(), "securite.restauration") &&
              !shown(rig.root(), "securite.sauvegarde.maintenant") && has(t, "XPG Admin n'utilise jamais la cl\xC3\xA9 de l'\xC3\xA9" "diteur"),
          "Parametres de securite : la page de XPG Admin (ni la cle de l'editeur, ni la sauvegarde complete)");
    check(click(rig.root(), "shell.cle") && rig.shell->currentPageId() == "compte", "sous derogation aussi, la pastille de la cle -> Mon compte");

    // ---- elle finit (son heure passee, meme hors ligne) pendant qu'une page qu'elle ouvrait est montree ----
    std::printf("-- 1.52.0 : la derogation finit sur une page qu'elle ouvrait - retour a Mon compte, c'est dit\n");
    rig.shell->navigate("versions");
    (void)rig.paint();
    const lic::Seconds before = g_now;
    g_now += 3601;
    t = rig.paint();
    check(rig.shell->currentPageId() == "compte" && lastText(rig) == "Ta d\xC3\xA9rogation a pris fin : retour \xC3\xA0 la gestion.",
          "finie : Mon compte, << Ta derogation a pris fin : retour a la gestion. >> (" + lastText(rig) + ")");
    check(joined(menuOf(rig)) == joined(kManagement), "le menu de la gestion revient tout de suite (" + joined(menuOf(rig)) + ")");
    g_now = before;
    // revoquee pendant qu'une page de la gestion est montree : le menu suit, on reste ou on est
    w.adoptDerogation(forged(camille->publicKeyHex(), kCamille, g_now - 60, g_now + 3600));
    (void)rig.paint();
    check(joined(menuOf(rig)) == joined(allEntries()), "une nouvelle derogation : tout, de nouveau");
    rig.shell->navigate("licences");
    gui::Derogation revoked = forged(camille->publicKeyHex(), kCamille, g_now - 60, g_now + 3600);
    revoked.state = "revoquee";
    revoked.revoked = g_now;
    w.adoptDerogation(revoked);
    (void)rig.paint();
    check(rig.shell->currentPageId() == "licences" && joined(menuOf(rig)) == joined(kManagement), "revoquee sur Licences : on y reste, le menu de la gestion revient");
    // celle d'un autre admin ne m'ouvre rien
    w.adoptDerogation(forged(std::string(64, 'b'), "Nina Essai", g_now - 60, g_now + 3600));
    (void)rig.paint();
    check(joined(menuOf(rig)) == joined(kManagement), "la derogation d'un autre admin : rien ne s'ouvre ici");
    la::setEdition(la::Edition::Plus);
}

} // namespace v152
