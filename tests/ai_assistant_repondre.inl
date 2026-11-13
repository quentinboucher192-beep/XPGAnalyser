// =============================================================================
//  tests/ai_assistant_repondre.inl - 1.50.1 : REPONDRE, REDUIRE, COPIER dans la
//  discussion de l'Assistant IA (inclus par ai_assistant_test.cpp)
// -----------------------------------------------------------------------------
//  La 1.50.0 les avait donnes a la discussion des sessions ; le client : << les
//  limites restantes >> - l'Assistant IA ne les avait pas. Ce que l'essai tient :
//   - la coupe d'un message reduit (collapseCut) : 4 rangees, jamais au milieu
//     d'un bloc de code (avant lui ; un message qui commence par un bloc garde son
//     en-tete), la marge d'un bloc entier gardee ; un message de 5 rangees entier ;
//   - une longue reponse plus ancienne que le dernier echange : reduite (4 lignes,
//     << ... voir tout >>, ce qui est cache n'est pas dessine) ; un clic la deballe
//     (<< Reduire >>) ; << Tout reduire >> ; le dernier echange reste deballe ;
//   - Repondre (le survol, le clic droit, le panneau) : la barre << En reponse a
//     Assistant : << ... >> >> au-dessus de la saisie, Echap l'annule ; la demande
//     envoyee porte la citation (Message::extra "reponseA"), sa bulle montre
//     l'extrait (un clic : le message cite, choisi) ; LE MODELE LA RECOIT balisee
//     devant le texte (requestFor) ; relue apres un redemarrage ; une autre
//     conversation ouverte l'oublie ;
//   - Copier : le texte tel qu'il est ecrit, avec l'auteur et l'heure, Ctrl+C sur
//     le message choisi, << Copier le code >> (le code exact, sans les balises) ;
//   - le clic droit d'une demande garde << Creer une mission avec ce message >>.
// =============================================================================

namespace repondre {

std::string g_clip;

void installClipboard() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
    s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
    s.clipboardGet = [] { return g_clip; };
    s.clipboardSet = [](std::string_view t) { g_clip = std::string(t); };
    ui::installPlatformServices(std::move(s));
}

const char* const kLong =
    "## Compter les pompes\n"
    "Voici une m\xC3\xA9thode en plusieurs \xC3\xA9tapes.\n"
    "- Lister les pompes du projet\n"
    "- Lire leur \xC3\xA9tat\n"
    "- Additionner celles en marche\n"
    "- Afficher le total sur la vue\n"
    "\n"
    "```st\n"
    "Nb := 0;\n"
    "FOR i := 1 TO 10 DO\n"
    "  IF Pompe[i].Marche THEN Nb := Nb + 1; END_IF;\n"
    "END_FOR;\n"
    "```\n"
    "C'est tout.";

const char* const kCode = "Nb := 0;\nFOR i := 1 TO 10 DO\n  IF Pompe[i].Marche THEN Nb := Nb + 1; END_IF;\nEND_FOR;";

ai::Message msg(const std::string& id, const std::string& role, const std::string& date, const std::string& text) {
    ai::Message m;
    m.id = id;
    m.role = role;
    m.date = date;
    m.text = text;
    if (role == "assistant") m.state = "fini";
    return m;
}

void writeConversation() {
    ai::Conversation c;
    c.id = "c-repondre-essai";
    c.title = "Essai r\xC3\xA9pondre";
    c.created = c.modified = "2026-10-30T17:30:00";
    c.messages.push_back(msg("m1", "utilisateur", "2026-10-30T17:30:00", "Comment compter les pompes en marche ?"));
    c.messages.push_back(msg("m2", "assistant", "2026-10-30T17:31:00", kLong));
    c.messages.push_back(msg("m3", "utilisateur", "2026-10-30T17:32:00", "Merci, et pour les vannes ?"));
    c.messages.push_back(msg("m4", "assistant", "2026-10-30T17:33:00", "Le m\xC3\xAAme principe : une boucle sur les vannes."));
    std::string why;
    check(ai::saveConversation(c, &why), "la conversation d'essai s'ecrit (" + why + ")");
}

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

} // namespace repondre

void testCollapseCut() {
    std::printf("[1.50.1 : la coupe d'un message reduit]\n");
    using V = ai::AiChatView;
    const auto cut = [](const std::string& md) { return V::collapseCut(V::layoutText(md, 400.f)); };
    const auto size = [](const std::string& md) { return V::layoutText(md, 400.f).size(); };
    check(cut("a\nb\nc\nd\ne") == size("a\nb\nc\nd\ne"), "5 lignes : entier (pas de << voir tout >> pour une ligne)");
    check(cut("a\nb\nc\nd\ne\nf") == 4, "6 lignes : les 4 premieres");
    const std::string mid = "a\nb\n```st\n1\n2\n3\n4\n5\n```";
    check(cut(mid) == 2, "la coupe tomberait dans un bloc de code : elle passe avant lui (" + std::to_string(cut(mid)) + ")");
    const std::string first = "```st\n1\n2\n3\n4\n5\n6\n```";
    check(cut(first) == 1, "un message qui commence par un bloc : son en-tete seul (Copier : le code entier)");
    const std::string whole = "a\n```st\n1\n2\n```\nb\nc\nd";
    check(cut(whole) == 5, "un bloc montre en entier garde sa marge du bas (" + std::to_string(cut(whole)) + ")");
}

void testReplyCollapseCopy() {
    std::printf("[1.50.1 : repondre, reduire, copier dans la discussion de l'Assistant]\n");
    repondre::installClipboard();
    Fixture f("repondre");
    repondre::writeConversation();
    f.build();   // relit le dossier de l'Assistant : la conversation d'essai
    check(f.as().select("c-repondre-essai"), "la conversation d'essai s'ouvre");
    f.panel->sync();
    f.step();
    auto& chat = f.panel->chat();
    chat.scrollBy(-100000.f);
    f.step();

    // ---- reduire / deballer ----
    check(chat.collapsible(1) && chat.collapsed(1), "la longue reponse (avant le dernier echange) : reduite");
    check(chat.shownLineCount(1) == 4 && chat.renderedLines(1).size() > 6,
          "reduite : 4 lignes montrees sur " + std::to_string(chat.renderedLines(1).size()));
    check(chat.toggleText(1) == "\xE2\x80\xA6 voir tout", "<< ... voir tout >> sous le texte");
    check(!chat.collapsible(3) && !chat.collapsed(2), "le dernier echange : court, entier");
    {
        Recorder rec;
        f.render(rec);
        check(rec.saw("\xE2\x80\xA6 voir tout"), "<< ... voir tout >> est dessine");
        // (les mots d'une ligne de texte sont dessines un a un ; une ligne de code d'un coup)
        check(rec.saw("Lister") && rec.saw("Lire") && !rec.saw("Additionner") && !rec.saw("Afficher") && !rec.saw("FOR i := 1"), "ce qui est reduit n'est pas dessine (ni la fin de la liste, ni le code)");
    }
    const gfx::Rect tg = chat.toggleRect(1);
    check(tg.w > 0.f && chat.bounds().contains({tg.x + 2.f, tg.y + 2.f}), "<< ... voir tout >> : dans la discussion");
    (void)f.panel->dispatch(ui::MouseDown{{tg.x + 4.f, tg.y + tg.h / 2.f}, ui::MouseButton::Left, 1, {}});
    (void)f.panel->dispatch(ui::MouseUp{{tg.x + 4.f, tg.y + tg.h / 2.f}, ui::MouseButton::Left, {}});
    f.step();
    check(!chat.collapsed(1) && chat.shownLineCount(1) == chat.renderedLines(1).size(), "un clic sur << ... voir tout >> : deballee, tout montre");
    check(chat.toggleText(1) == "R\xC3\xA9" "duire", "deballee : << Reduire >>");
    {
        Recorder rec;
        f.render(rec);
        check(rec.saw("Afficher") && rec.saw("FOR i := 1 TO 10 DO") && rec.saw("END_FOR;"), "deballee : la fin et le bloc de code (entier) sont dessines");
    }
    chat.collapseAll();
    f.step();
    check(chat.collapsed(1), "<< Tout reduire >> : reduite de nouveau");
    chat.setExpanded(1, true);
    f.step();

    // ---- repondre : le survol, la barre au-dessus de la saisie, Echap ----
    chat.revealMessage(1, false);
    f.step();
    const gfx::Rect mr = chat.messageRect(1);
    (void)f.panel->dispatch(ui::MouseMove{{mr.x + mr.w / 2.f, mr.y + 6.f}, {}, {}});
    f.step();
    check(chat.hoveredMessage() == 1, "le survol d'une reponse");
    const gfx::Rect rb = chat.hoverButton(1, 0), cb = chat.hoverButton(1, 1);
    check(rb.w > 0.f && cb.w > 0.f && chat.bounds().contains({rb.x, rb.y}) && chat.bounds().contains({cb.right() - 1.f, cb.bottom() - 1.f}) && rb.right() <= cb.x,
          "Repondre et Copier : deux pictogrammes sur sa ligne, dans la discussion");
    (void)f.panel->dispatch(ui::MouseDown{{rb.x + rb.w / 2.f, rb.y + rb.h / 2.f}, ui::MouseButton::Left, 1, {}});
    (void)f.panel->dispatch(ui::MouseUp{{rb.x + rb.w / 2.f, rb.y + rb.h / 2.f}, ui::MouseButton::Left, {}});
    f.panel->sync();
    f.step();
    auto& bar = f.panel->replyBar();
    check(bar.active() && bar.replyId() == "m2", "Repondre : la barre << En reponse a ... >>");
    check(bar.label() == "En r\xC3\xA9ponse \xC3\xA0 Assistant : \xC2\xAB Compter les pompes\xE2\x80\xA6 \xC2\xBB", "son texte : l'auteur, l'extrait sans la mise en forme (" + bar.label() + ")");
    check(bar.visible() && bar.bounds().bottom() <= f.panel->promptRect().y + 0.5f && bar.bounds().h > 0.f, "la barre est juste au-dessus de la saisie");
    check(bar.bounds().bottom() <= f.panel->promptRect().y && chat.bounds().bottom() <= bar.bounds().y, "la discussion s'arrete au-dessus de la barre");
    check(f.panel->prompt().focused(), "le clavier dans la saisie");
    (void)f.panel->dispatch(ui::KeyDown{ui::Key::Escape, {}, false});
    f.step();
    check(!bar.active() && !f.as().replyTo(), "Echap : la reponse est annulee");
    check(f.panel->replyTo(1) && bar.active(), "Repondre (le panneau) : la barre revient");

    // ---- la demande part avec la citation ; le modele la recoit ----
    check(f.sendAndWait("Et pour les vannes motoris\xC3\xA9" "es ?"), "la demande part");
    f.panel->sync();
    f.step();
    const auto* c = f.as().current();
    check(c && c->messages.size() == 6, "la demande et sa reponse");
    if (!c || c->messages.size() < 6) return;
    const auto q = ai::chatreply::readQuote(c->messages[4]);
    check(q && q->id == "m2" && q->who == "Assistant" && q->excerpt == "Compter les pompes\xE2\x80\xA6", "la demande porte la citation (reponseA)");
    check(!f.as().replyTo() && !bar.active(), "partie : plus de barre");
    check(chat.quoteText(4) == "Assistant : \xC2\xAB Compter les pompes\xE2\x80\xA6 \xC2\xBB", "sa bulle montre l'extrait au-dessus du texte (" + chat.quoteText(4) + ")");
    const gfx::Rect qr = chat.quoteRect(4), bub = chat.bubbleRect(4);
    check(qr.w > 0.f && bub.w > 0.f && qr.x >= bub.x && qr.y >= bub.y && qr.bottom() <= bub.bottom() + 0.5f, "l'extrait est dans la bulle");
    {
        Recorder rec;
        f.render(rec);
        check(rec.saw("Assistant : \xC2\xAB Compter les pompes"), "l'extrait est dessine");
    }
    (void)f.panel->dispatch(ui::MouseDown{{qr.x + 6.f, qr.y + qr.h / 2.f}, ui::MouseButton::Left, 1, {}});
    (void)f.panel->dispatch(ui::MouseUp{{qr.x + 6.f, qr.y + qr.h / 2.f}, ui::MouseButton::Left, {}});
    f.step();
    check(chat.selectedMessage() == 1, "un clic sur l'extrait : le message cite est choisi (et mis en vue)");
    const auto req = f.as().requestFor(*c, c->messages.size());
    std::string asked, plain;
    for (const auto& t : req.turns)
        if (t.role == ai::ProviderTurn::Role::User) {
            if (t.text.find("motoris") != std::string::npos) asked = t.text;
            if (t.text.find("Comment compter") != std::string::npos) plain = t.text;
        }
    check(asked.rfind("[Citation \xE2\x80\x94 l'utilisateur r\xC3\xA9pond \xC3\xA0 ta r\xC3\xA9ponse de 17:31 :]", 0) == 0, "le modele recoit la citation, balisee, d'abord");
    check(asked.find("> ## Compter les pompes\n> Voici une m\xC3\xA9thode") != std::string::npos && asked.find("[Fin de la citation]\n\nEt pour les vannes") != std::string::npos,
          "les premieres lignes du message cite, puis la fin de la citation, puis la demande");
    check(plain == "Comment compter les pompes en marche ?", "une demande sans citation part telle quelle");

    // ---- relue apres un redemarrage (le fichier de la conversation) ----
    f.build();
    check(f.as().select("c-repondre-essai"), "la conversation se rouvre");
    f.panel->sync();
    f.step();
    const auto* again = f.as().current();
    check(again && again->messages.size() == 6 && ai::chatreply::readQuote(again->messages[4]) && f.panel->chat().quoteText(4).find("Compter les pompes") != std::string::npos,
          "la citation est relue (reponseA dans le fichier)");
    auto& chat2 = f.panel->chat();
    check(chat2.collapsed(1), "une nouvelle seance : la longue reponse, avant le dernier echange, est reduite");

    // ---- copier ----
    repondre::g_clip.clear();
    check(chat2.copyMessage(1, false) && repondre::g_clip == repondre::kLong, "Copier : le texte tel qu'il est ecrit (le markdown, les balises du code)");
    check(chat2.copyMessage(1, true) && repondre::g_clip.rfind("17:31 Assistant : ## Compter les pompes\n", 0) == 0, "Copier avec l'auteur et l'heure : << 17:31 Assistant : ... >>");
    check(chat2.copyMessage(0, true) && repondre::g_clip == "17:30 Moi : Comment compter les pompes en marche ?", "une demande : << 17:30 Moi : ... >>");
    chat2.revealMessage(2, false);
    f.step();
    const gfx::Rect b2 = chat2.bubbleRect(2);
    (void)f.panel->dispatch(ui::MouseDown{{b2.x + 4.f, b2.y + 4.f}, ui::MouseButton::Left, 1, {}});
    (void)f.panel->dispatch(ui::MouseUp{{b2.x + 4.f, b2.y + 4.f}, ui::MouseButton::Left, {}});
    f.step();
    check(chat2.selectedMessage() == 2 && chat2.focused(), "un clic sur un message le choisit (le clavier dans la discussion)");
    repondre::g_clip.clear();
    ui::KeyMods ctrl;
    ctrl.ctrl = true;
    (void)chat2.dispatch(ui::KeyDown{ui::Key::C, ctrl, false});
    check(repondre::g_clip == "Merci, et pour les vannes ?", "Ctrl+C : le message choisi");

    // ---- le clic droit : sur un bloc de code, sur une demande ----
    chat2.setExpanded(1, true);
    chat2.revealMessage(1, false);
    f.step();
    {
        Recorder rec;
        f.render(rec);   // la place des blocs de code (le dessin)
    }
    gfx::Rect codeAt{};
    for (const auto& [at, code] : chat2.copyButtons())
        if (code == repondre::kCode) codeAt = at;
    check(codeAt.w > 0.f, "le bloc de code est dessine, son bouton Copier aussi");
    (void)f.panel->dispatch(ui::MouseDown{{codeAt.x - 40.f, codeAt.y + codeAt.h + 8.f}, ui::MouseButton::Right, 1, {}});
    const auto items = chat2.messageMenuItems();
    check(repondre::contains(items, "R\xC3\xA9pondre") && repondre::contains(items, "Copier") && repondre::contains(items, "Copier avec l'auteur et l'heure")
              && repondre::contains(items, "Copier le code") && repondre::contains(items, "R\xC3\xA9" "duire") && repondre::contains(items, "Tout r\xC3\xA9" "duire")
              && !repondre::contains(items, "Cr\xC3\xA9" "er une mission avec ce message"),
          "le clic droit d'une reponse : Repondre, Copier, avec l'auteur et l'heure, Copier le code, Reduire, Tout reduire");
    repondre::g_clip.clear();
    check(chat2.chooseMessageMenuItem("Copier le code") && repondre::g_clip == repondre::kCode, "Copier le code : le code exact, sans les balises");
    check(chat2.openMessageMenu(0, -1), "le clic droit d'une demande");
    const auto mine = chat2.messageMenuItems();
    check(repondre::contains(mine, "R\xC3\xA9pondre") && repondre::contains(mine, "Cr\xC3\xA9" "er une mission avec ce message") && !repondre::contains(mine, "Copier le code"),
          "une demande : Repondre, Copier... et Creer une mission avec ce message (1.22.6)");
    check(chat2.chooseMessageMenuItem("R\xC3\xA9pondre") && f.panel->replyBar().active() && f.panel->replyBar().replyId() == "m1", "Repondre (le clic droit) a sa propre demande");

    // ---- une autre conversation ouverte : la reponse est oubliee ----
    f.panel->newConversation();
    f.step();
    check(!f.as().replyTo() && !f.panel->replyBar().active(), "une autre conversation : plus de citation en attente");
}
