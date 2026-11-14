// =============================================================================
//  tests/admin_gui_derogation152.inl - 1.52.0 (agent D) : LA DEROGATION << tout >>,
//  ACTIVEE PAR UN CODE, DANS LES FENETRES (docs/ADMINS.md, par. 11) - inclus par
//  admin_gui_compte_test.cpp, contre le VRAI service local (Lea, admin invitee) :
//   - l'Admin+ : le dialogue (30 min, 1 h, 4 h, 24 h, 72 h, << Autre duree >> : des
//     heures et des minutes, 10 min a 72 h, une duree illisible dite sans rien
//     envoyer) ; << Elle commence quand il tape le code. Le code est valable 1 h. >> ;
//     accordee pour 2 h 30 : le code en grand (retire de la cle), << Donne-lui ce
//     code... >>, Copier ; jamais dans le resultat ; Service en ligne et Mon compte
//     le remontrent tant qu'il attend ; << a tape le code >> quand le regard la voit
//     activee ;
//   - XPG Admin (Lea) : le regard des 15 s voit la derogation en attente -> l'alerte,
//     le dialogue du code ouvert seul (une fois), la pastille << Derogation recue :
//     taper le code >>, la carte de Mon compte avec son champ ; un code faux (la
//     phrase du service, les essais), le bon : << Derogation active : tu vois tout
//     comme l'admin+ pendant 2 h 30 >>, la pastille << Derogation : 2 h 29 >> qui
//     compte ; revoquee : l'avis, plus de pastille.
//  Les codes sont compares, jamais ecrits dans un message d'essai.
// =============================================================================

namespace d152 {

// Le texte dessine, les lignes coupees remises bout a bout (une phrase coupee en deux lignes se retrouve).
std::string drawDialog(menu::IMenu& m, float w = 1400.f, float h = 900.f) {
    TextRenderer r;
    std::string t = paintMenu(m, r, w, h);
    for (auto& c : t)
        if (c == '\n') c = ' ';
    return t;
}

// Finir le travail lent de la coquille (le regard parti seul compris).
void settle(ShellRig& s) {
    for (int i = 0; i < 4 && s.shell->finishSlow(); ++i) {
    }
}

// Le regard des 15 s, tout de suite (comme la coquille toutes les 15 s).
bool watchNow(ShellRig& s) {
    settle(s);
    if (!s.shell->autoWatch()) return false;
    settle(s);
    s.menus.applyPending();
    return true;
}

menu::IMenu* topDialog(ShellRig& s) {
    s.menus.applyPending();
    menu::IMenu* t = s.menus.top();
    return t && t != s.shell ? t : nullptr;
}

std::string lastFlash(ShellRig& s) {
    const auto* m = s.shell->lastMessage();
    return m ? m->result.message : std::string();
}

} // namespace d152

void derogationGui152(Fixture& wf, Fixture& lsf, const std::string& leaHex) {
    std::printf("-- 1.52.0 : la derogation par code - l'Admin+ (le dialogue, le code), XPG Admin (l'alerte, le code tape)\n");
    namespace D = d152;
    HostRig prig(wf);
    std::string code, id;
    {
        auto dlg = gui::makeDerogationDialog(prig.host(), leaHex);
        (void)dlg->Initialize();
        ui::Widget& root = *dlg->widgetRoot();
        std::string t = D::drawDialog(*dlg);
        check(has(t, "Elle commence quand il tape le code. Le code est valable 1 h.") && has(t, "72 h au plus, jamais permanente"),
              "le dialogue : elle commence quand il tape le code ; 72 h au plus");
        check(root.findById("derogation.duree.30min") && root.findById("derogation.duree.1") && root.findById("derogation.duree.4") &&
                  root.findById("derogation.duree.24") && root.findById("derogation.duree.72") && root.findById("derogation.duree.autre"),
              "les durees : 30 min, 1 h, 4 h, 24 h, 72 h, Autre duree");
        check(has(t, "Jamais ce qui demande ta cl") && has(t, "Brancher Stripe") && has(t, "SON propre compte"), "ce qu'elle n'ouvre jamais ; Cloudflare : son propre compte");
        check(!shown(root, "derogation.duree.heures"), "Autre duree : ses champs caches tant qu'elle n'est pas choisie");
        auto* other = find<ui::RadioButton>(root, "derogation.duree.autre");
        if (other) other->select();
        t = D::drawDialog(*dlg);
        check(shown(root, "derogation.duree.heures") && shown(root, "derogation.duree.minutes") && has(t, "de 10 min \xC3\xA0 72 h"),
              "Autre duree : les heures et les minutes (10 min a 72 h)");
        (void)type(root, "derogation.duree.heures", "80");
        (void)type(root, "derogation.duree.minutes", "0");
        check(click(root, "derogation.accorder") && prig.reports.empty(), "80 h : rien n'est envoye");
        t = D::drawDialog(*dlg);
        check(has(t, "de 10 min \xC3\xA0 72 h (jamais plus"), "la phrase : de 10 min a 72 h");
        (void)type(root, "derogation.duree.heures", "2");
        (void)type(root, "derogation.duree.minutes", "75");
        (void)click(root, "derogation.accorder");
        t = D::drawDialog(*dlg);
        check(prig.reports.empty() && has(t, "les minutes vont de 0 \xC3\xA0 59"), "75 minutes : dit, rien n'est envoye");
        (void)type(root, "derogation.duree.minutes", "30");
        check(click(root, "derogation.accorder") && lastOk(prig), "2 h 30 : accordee (" + lastText(prig) + ")");
        const gui::Derogation& d = wf.ws->derogation();
        id = d.id;
        code = wf.ws->derogationCode(id);
        check(d.pendingAt(wf.ws->now()) && d.duration == 9000 && d.forKey(leaHex) && code.size() == 8, "en attente du code, 2 h 30, pour Lea");
        check(!has(lastText(prig), code) && !has(lastText(prig), gui::formatDerogationCode(code)), "le resultat ne porte pas le code");
        t = D::drawDialog(*dlg);
        check(has(t, gui::formatDerogationCode(code)) && has(t, "Donne-lui ce code (de vive voix, au t\xC3\xA9l\xC3\xA9phone) : il le tape dans XPG Admin. Valable jusqu'\xC3\xA0 "),
              "le code en grand, << Donne-lui ce code (de vive voix, au telephone) ... Valable jusqu'a ... >>");
        check(shown(root, "derogation.copier") && !shown(root, "derogation.accorder") && shown(root, "derogation.revoquer"), "Copier ; Revoquer ; plus d'Accorder");
        check(click(root, "derogation.copier") && has(D::drawDialog(*dlg), "copi\xC3\xA9"), "Copier : dit");
    }
    {
        auto sp = gui::makeServicePage(prig.host());
        const std::string t = draw(*sp, 1400.f, 3200.f);
        check(has(t, "En attente du code") && has(t, gui::formatDerogationCode(code)) && has(t, "valable jusqu'\xC3\xA0") && sp->findById("service.derogation.revoquer") &&
                  sp->findById("service.derogation.revoquer")->enabled(),
              "Service en ligne : en attente du code, le code (retire de la cle), Revoquer");
        auto ap = gui::makeAccountPage(prig.host());
        check(has(draw(*ap), "en attente du code : " + gui::formatDerogationCode(code)), "Mon compte de l'admin+ : en attente du code, le code");
    }

    // ---- XPG Admin (Lea)
    la::setEdition(la::Edition::Classique);
    ShellRig lea(lsf.admin);
    (void)lea.paint();
    ShellRig* plus = nullptr;
    {
        check(D::watchNow(lea), "le regard des 15 s (XPG Admin de Lea)");
        const auto& toasts = lea.shell->toasts();
        check(!toasts.empty() && toasts.back().derogation && toasts.back().title == "L'admin+ t'a envoy\xC3\xA9 une d\xC3\xA9rogation (2 h 30)" &&
                  toasts.back().line.rfind("Tape le code qu'il voit sur son PC", 0) == 0 &&
                  D::lastFlash(lea) == "L'admin+ t'a envoy\xC3\xA9 une d\xC3\xA9rogation (2 h 30) : tape le code qu'il voit sur son PC.",
              "l'alerte et l'avis : << L'admin+ t'a envoye une derogation (2 h 30) : tape le code qu'il voit sur son PC. >>");
        menu::IMenu* top = D::topDialog(lea);
        check(top && top->id() == "dialog.derogation.code", "le dialogue du code s'ouvre seul");
        bool pending = false;
        check(lea.shell->derogationChipText(&pending) == "D\xC3\xA9rogation re\xC3\xA7ue : taper le code" && pending, "la pastille ambre : << Derogation recue : taper le code >>");
        check(!lea.shell->pageAllowed("securite") && !lea.shell->pageAllowed("admins"), "en attente : toujours la gestion seulement");
        if (top) {
            ui::Widget& root = *top->widgetRoot();
            std::string t = D::drawDialog(*top);
            check(has(t, "Tape le code de 8 chiffres que l'admin+ voit sur son PC") && has(t, "5 essais"), "le dialogue : tape le code ; 5 essais");
            const std::string wrong = code == "11111111" ? std::string("22222222") : std::string("11111111");
            (void)type(root, "derogation.code", wrong.substr(0, 4) + " " + wrong.substr(4));
            check(click(root, "derogation.code.activer"), "Activer (un code faux)");
            D::settle(lea);
            t = D::drawDialog(*top);
            check(has(t, "Code faux : il te reste 4 essais.") && find<ui::InputText>(root, "derogation.code")->text().empty(),
                  "le refus du service, dans le dialogue ; le champ vide (" + D::lastFlash(lea) + ")");
            check(click(root, "derogation.code.fermer") && !D::topDialog(lea), "Plus tard : ferme");
        }
        // une fois par derogation : le regard suivant ne le rouvre pas
        check(D::watchNow(lea) && !D::topDialog(lea), "le regard suivant : le dialogue ne se rouvre pas");
        // Mon compte : la carte et son champ
        lea.shell->navigate("compte");
        const std::string t = lea.paint();
        check(has(t, "D\xC3\xA9rogation re\xC3\xA7ue : taper le code") && shown(lea.root(), "compte.derogation.code") && shown(lea.root(), "compte.derogation.activer") &&
                  has(t, "4 essais"),
              "Mon compte : la carte << Derogation recue >>, le champ, 4 essais");
        // l'Admin+ regarde aussi (il la voit en attente)
        la::setEdition(la::Edition::Plus);
        plus = new ShellRig(wf.admin);
        (void)plus->paint();
        check(D::watchNow(*plus) && plus->shell->workspace().derogation().pendingAt(plus->shell->workspace().now()), "l'Admin+ : son regard la voit en attente");
        la::setEdition(la::Edition::Classique);
    }
    {
        // la pastille rouvre le dialogue ; le bon code (tape avec un tiret)
        check(click(lea.root(), "shell.derogation"), "la pastille : un clic");
        menu::IMenu* top = D::topDialog(lea);
        check(top && top->id() == "dialog.derogation.code", "la pastille ouvre le dialogue du code");
        if (top) {
            ui::Widget& root = *top->widgetRoot();
            (void)type(root, "derogation.code", code.substr(0, 4) + "-" + code.substr(4));
            (void)click(root, "derogation.code.activer");
            D::settle(lea);
            const std::string t = D::drawDialog(*top);
            check(has(t, "D\xC3\xA9rogation active : tu vois tout comme l'admin+ pendant 2 h 30 (jusqu'\xC3\xA0 "), "<< Derogation active : tu vois tout comme l'admin+ pendant 2 h 30 (jusqu'a ...) >>");
            check(lea.shell->pageAllowed("securite") && lea.shell->pageAllowed("service") && lea.shell->pageAllowed("admins"), "le menu suit aussitot : tout, comme l'admin+");
            check(click(root, "derogation.code.activer") && !D::topDialog(lea), "Fermer");
        }
        (void)lea.paint();
        check(lea.shell->toasts().empty() || !lea.shell->toasts().back().derogation, "activee : l'alerte de la derogation recue s'en va");
        bool pending = true;
        const std::string chip = lea.shell->derogationChipText(&pending);
        check(!pending && chip.rfind("D\xC3\xA9rogation : 2 h ", 0) == 0, "la pastille : << Derogation : 2 h .. >> (" + chip + ")");
        check(has(lea.paint(), chip), "la pastille, en haut");
        g_now += 1800;
        const std::string later = lea.shell->derogationChipText();
        check(later != chip && later.rfind("D\xC3\xA9rogation : ", 0) == 0 && (later.find(" 2 h 00") != std::string::npos || later.find(" 1 h 59") != std::string::npos),
              "30 minutes plus tard, elle compte (" + later + ")");
        g_now -= 1800;
        // l'Admin+ : son regard la voit activee
        la::setEdition(la::Edition::Plus);
        check(D::watchNow(*plus) && has(D::lastFlash(*plus), "L\xC3\xA9" "a Essai a tap\xC3\xA9 le code : sa d\xC3\xA9rogation court jusqu'\xC3\xA0 "),
              "l'Admin+ : << Lea Essai a tape le code : sa derogation court jusqu'a ... >> (" + D::lastFlash(*plus) + ")");
        const auto r = wf.ws->revokeDerogation();
        check(r.ok, "l'Admin+ la revoque (" + r.message + ")");
        la::setEdition(la::Edition::Classique);
    }
    {
        check(D::watchNow(lea), "le regard de Lea");
        check(has(D::lastFlash(lea), "r\xC3\xA9voqu\xC3\xA9" "e par l'admin+") && lea.shell->derogationChipText().empty(), "revoquee : l'avis ; plus de pastille (" + D::lastFlash(lea) + ")");
        check(!lea.shell->pageAllowed("securite") && !lea.shell->pageAllowed("admins"), "la gestion seulement, de nouveau");
    }
    delete plus;
    la::setEdition(la::Edition::Plus);
}
