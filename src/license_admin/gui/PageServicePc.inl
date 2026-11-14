// =============================================================================
//  license_admin/gui/PageServicePc.inl - 1.52.0 : MON COMPTE > CE PC (inclus par
//  PageService.cpp : la meme page, son mode << poste >>, ses widgets << poste.* >>)
// -----------------------------------------------------------------------------
//  XPG Admin sans derogation n'a plus << Service en ligne >> (ClassicView.hpp) : ce
//  dont un admin a besoin pour lui-meme y etait et vient ici, sous Mon compte -
//    - LA CONNEXION AU SERVICE : d'ou vient l'adresse (la connexion signee par
//      l'editeur), << Utiliser une connexion... >> ;
//    - LE SERVICE : l'adresse (tapee ici par un poste pas encore admin, sans
//      connexion signee ; un admin la lit), l'etat, << Verifier >> ;
//    - L'ARCHIVE EN LIGNE : la cle de synchro recue par le trousseau, la derniere
//      synchro, << Synchroniser l'archive >>, << Recevoir seulement >>, << Verifier
//      (sans rien changer) >> ;
//    - LE MAIL AU CLIENT : la boite de CE PC (le meme reglage que Service en ligne).
//  Rien de ce qui gere le service (deployer, certifier, les adresses de secours, la
//  derogation, repartir de zero) : c'est a l'admin+ ; une derogation l'ouvre dans
//  Service en ligne.
// =============================================================================

const char* ServicePage::pcSubtitle() {
    return "Ce que ce PC r\xC3\xA8gle lui-m\xC3\xAAme pour toi : l'adresse du service de l'\xC3\xA9" "diteur, l'archive re\xC3\xA7ue par le trousseau, le mail au client. "
           "Le service lui-m\xC3\xAAme est \xC3\xA0 l'admin+ (une d\xC3\xA9rogation qu'il t'accorde ouvre Service en ligne).";
}

void ServicePage::pcBlocks(Blocks& bl) {
    const auto& s = ws().service();
    const Role role = ws().role();
    const bool member = role == Role::Admin || role == Role::Retired;

    connectionBlocks(bl);   // d'ou vient l'adresse (la connexion signee), << Utiliser une connexion... >>

    // ---- le service : l'adresse, l'etat, Verifier ----
    const bool reached = s.hasState && s.state.reached;
    bl.card("Le service", !s.problem.empty() ? ui::Tone::Error : s.ready() && reached ? ui::Tone::Ok : ui::Tone::Warning);
    bl.field("Adresse", url_, 460.f, {saveUrl_});
    if (role == Role::None || role == Role::Pending)
        bl.note("Sans connexion sign\xC3\xA9" "e : colle l'adresse que l'admin+ t'a donn\xC3\xA9" "e (https://\xE2\x80\xA6workers.dev), puis Enregistrer l'adresse ; ton code "
                "d'invitation y est v\xC3\xA9rifi\xC3\xA9.");
    else
        bl.note("L'adresse du service de l'\xC3\xA9" "diteur : seul l'admin+ la change (ou toi, pendant une d\xC3\xA9rogation).");
    if (!s.problem.empty()) bl.text("Probl\xC3\xA8me : " + s.problem, ui::Tone::Error);
    if (!s.configured) bl.pair("\xC3\x89tat", "pas encore r\xC3\xA9gl\xC3\xA9", ui::Tone::Warning);
    else if (!s.hasState) bl.pair("\xC3\x89tat", "pas encore v\xC3\xA9rifi\xC3\xA9 dans cette s\xC3\xA9" "ance : V\xC3\xA9rifier", ui::Tone::Muted);
    else if (!reached) bl.pair("\xC3\x89tat", "le service n'a pas r\xC3\xA9pondu (" + dateTimeFr(s.stateAt) + ")", ui::Tone::Error);
    else
        bl.pair("\xC3\x89tat", "il r\xC3\xA9pond (version " + (s.state.version.empty() ? std::string("?") : s.state.version) + ", v\xC3\xA9rifi\xC3\xA9 le " + dateTimeFr(s.stateAt) + ")",
                ui::Tone::Ok);
    if (service_.has) bl.pair("Dernier r\xC3\xA9sultat", service_.message + " (" + dateTimeFr(service_.at) + ")", service_.ok ? ui::Tone::Ok : ui::Tone::Error);
    bl.row({verify_});
    bl.end();

    // ---- l'archive en ligne (un admin : la cle de synchro arrive par le trousseau) ----
    if (member) {
        const auto& last = s.lastSync;
        bl.card("L'archive en ligne", s.syncKey ? (last.at ? ui::Tone::Ok : ui::Tone::Warning) : ui::Tone::Warning);
        bl.text("Les licences, le registre, les r\xC3\xA9vocations de l'\xC3\xA9" "diteur, gard\xC3\xA9s chiffr\xC3\xA9s sur le service : ce PC les re\xC3\xA7oit, et y d\xC3\xA9pose ce que "
                "tu signes.");
        bl.pair("Cl\xC3\xA9 de synchro", s.syncKey ? s.syncKeyId + " (re\xC3\xA7ue par le trousseau)"
                                                    : std::string("pas encore : elle arrive par le trousseau d\xC3\xA8s que l'admin+ t'a sign\xC3\xA9"),
                s.syncKey ? ui::Tone::None : ui::Tone::Warning);
        bl.pair("Derni\xC3\xA8re synchro", last.at ? dateTimeFr(last.at) + " (version " + std::to_string(last.version) + ")" : std::string("jamais depuis ce poste"),
                last.at ? ui::Tone::None : ui::Tone::Muted);
        if (s.syncKey) bl.row({sync_, receive_, dry_});
        if (archive_.has) {
            bl.pair("Dernier r\xC3\xA9sultat", archive_.message + " (" + dateTimeFr(archive_.at) + ")", archive_.ok ? ui::Tone::Ok : ui::Tone::Error);
            for (std::size_t i = 0; i < archive_.details.size() && i < 30; ++i) bl.bullet(archive_.details[i]);
            if (archive_.details.size() > 30) bl.note("\xE2\x80\xA6 et " + std::to_string(archive_.details.size() - 30) + " autres.");
        }
        bl.end();
    }

    mailBlocks(bl);   // la boite de ce PC (le mail au client)
}
