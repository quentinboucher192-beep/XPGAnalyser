// =============================================================================
//  tests/session_ui_discussion.inl - 1.50.0 : la discussion entre deux membres, dans session_ui_test.cpp
// -----------------------------------------------------------------------------
//  Le client : << faire en sorte de pouvoir repondre a des messages specifiques
//  dans le chat, pouvoir les deballer/reduire, pouvoir les copier dans le
//  presse-papier >>. Les messages passent par le protocole (la trame de l'hote,
//  relue chez Lea : session::chatFrame / readChat), puis l'onglet << Session >>
//  de Lea (app/hmi/HmiSessionPane, la liste de SessionText) :
//   - un compte rendu de huit lignes est reduit d'emblee, << ... voir tout >> le
//     deballe, il le reste quand un message arrive, << Tout reduire >> (le menu du
//     clic droit) le reduit ;
//   - << Repondre >> (le survol) montre << En reponse a ... >>, le message envoye
//     porte l'identifiant du message cite ; la reponse revenue de l'hote montre
//     l'extrait, un clic dessus met le message cite en vue et le choisit ; un
//     message a l'ancienne (sans identifiant) se repond aussi ; une reponse dont
//     le message cite est parti garde son extrait ;
//   - Ctrl+C sur le message choisi, << Copier avec l'auteur et l'heure >> (le
//     menu), << Copier >> du survol (le texte entier d'un message reduit) ;
//   - le journal seul : Copier seulement ; chez Camille (l'autre membre) : les
//     memes identifiants, la meme citation.
// =============================================================================

namespace d150 {

gfx::Point center(const gfx::Rect& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

// Ce qui passe sur le reseau : la trame que l'hote envoie a un membre de `version`, relue chez lui.
session::ChatMessage wire(const session::ChatMessage& m, const char* version) {
    core::json::Value back;
    (void)core::json::parse(core::json::write(session::chatFrame(m, version), false), back, nullptr);
    return session::readChat(back);
}

// Ce que le service des sessions met dans le Board (app/SessionService::publishBoard).
app::sessionmarks::Line lineOf(const session::ChatMessage& m, const std::string& me) {
    app::sessionmarks::Line l;
    l.time = m.time;
    l.from = m.from;
    l.text = m.text;
    l.color = m.color;
    l.mine = m.from == me;
    l.id = m.id;
    l.replyId = m.reply.id;
    l.replyFrom = m.reply.from;
    l.replyText = m.reply.excerpt;
    return l;
}

bool drawn(const SoftRenderer& r, const std::string& prefix) {
    for (const auto& t : r.texts)
        if (t.text.rfind(prefix, 0) == 0) return true;
    return false;
}

const SoftRenderer::Text* textOf(const SoftRenderer& r, const std::string& text) {
    for (const auto& t : r.texts)
        if (t.text == text) return &t;
    return nullptr;
}

} // namespace d150

void discussion150(SoftRenderer& r) {
    std::printf("-- 1.50.0 : la discussion entre deux membres - repondre, reduire / deballer, copier\n");
    namespace sm = app::sessionmarks;
    using d150::center;
    const auto theme = ui::Theme::dark();
    const ui::KeyMods none{};
    ui::KeyMods ctrl{};
    ctrl.ctrl = true;

    // ---- les messages : donnes par l'hote (Camille, 1.50), lus chez Lea ----
    session::ChatMessage m1{"m1", kCamille, kReport, "14:31", 0xE67E22, {}};
    session::ChatMessage m2{"m2", kLea, "Je regarde les alarmes.", "14:32", 0x2E86DE, {}};
    // Un message a l'ancienne (un hote 1.49 : ni id ni reponse).
    core::json::Value ancient = core::json::Value::object();
    ancient.set("op", "MESSAGE");
    ancient.set("de", kHugo);
    ancient.set("couleur", static_cast<std::int64_t>(0x27AE60));
    ancient.set("texte", "J'ai fini les voyants.");
    ancient.set("heure", "14:33");
    const session::ChatMessage m3 = session::readChat(ancient);
    sm::Board b;
    b.sessionName = "Essai des r\xC3\xA9ponses";
    b.hostName = kCamille;
    b.people.push_back({0, kCamille, "CE", "PC-BUREAU-ETUDES-01", "M-1A2B", "Vue_Accueil", 1, 0xE67E22, true, false, false});
    b.people.push_back({1, kLea, "LE", "PC-ATELIER-02", "M-7F21", "Vue_Alarmes", 2, 0x2E86DE, false, true, false});
    for (const auto& m : {d150::wire(m1, "1.50.0"), d150::wire(m2, "1.50.0"), m3}) b.chat.push_back(d150::lineOf(m, kLea));
    // Ce que Lea envoie (le texte, le message auquel elle repond) : les crochets du service.
    std::vector<std::pair<std::string, std::string>> sent;
    sm::Hooks hooks;
    hooks.say = [&sent](const std::string& t, const std::string& to) { sent.emplace_back(t, to); };
    sm::setHooks(hooks);
    sm::setActive(true);
    sm::setBoard(b);

    app::HmiSessionPane pane("sortie.session");
    const gfx::Rect frame{0.f, 0.f, 900.f, 420.f};
    pane.setBounds(frame);
    paintInto(r, pane, theme, frame);
    auto* list = pane.list();

    // ---- REDUIRE / DEBALLER ----
    int i1 = list->indexOfId("m1");
    const auto u1 = static_cast<std::size_t>(std::max(0, i1));
    check(i1 >= 0 && list->collapsible(u1) && list->collapsed(u1) && list->wrappedText(u1).size() >= 8,
          "un compte rendu de huit lignes : reduit d'emblee (" + std::to_string(list->wrappedText(u1).size()) + " lignes)");
    check(d150::drawn(r, "Compte rendu de l'essai") && !d150::drawn(r, "7. Reste") && d150::textOf(r, "\xE2\x80\xA6 voir tout"),
          "reduit : ses premieres lignes, puis << ... voir tout >> (la fin cachee)");
    pane.dispatch(ui::MouseDown{center(list->toggleRect(u1)), ui::MouseButton::Left, 1, none});
    paintInto(r, pane, theme, frame);
    check(!list->collapsed(u1) && d150::drawn(r, "7. Reste") && d150::textOf(r, "R\xC3\xA9" "duire"), "<< ... voir tout >> : deballe, tout le texte, puis << Reduire >>");
    // Un message arrive (la liste est refaite) : le compte rendu reste deballe (l'etat suit son identifiant).
    b.chat.push_back(d150::lineOf(d150::wire({"m4", kCamille, "Parfait, merci.", "14:40", 0xE67E22, {}}, "1.50.0"), kLea));
    sm::setBoard(b);
    pane.refresh();
    paintInto(r, pane, theme, frame);
    i1 = list->indexOfId("m1");
    check(i1 >= 0 && list->rows().size() == 4 && !list->collapsed(static_cast<std::size_t>(i1)), "un message arrive : le compte rendu reste deballe");
    // Le menu du clic droit : Repondre, Copier, Copier avec l'auteur et l'heure, Reduire, Tout reduire.
    list->openMenu(i1, center(list->toggleRect(static_cast<std::size_t>(i1))));
    std::vector<std::string> labels;
    bool allEnabled = true;
    if (auto* menu = list->contextMenu())
        for (const auto& it : menu->items())
            if (!it.separator) {
                labels.push_back(it.label);
                allEnabled = allEnabled && it.enabled;
            }
    const std::vector<std::string> wanted{"R\xC3\xA9pondre", "Copier", "Copier avec l'auteur et l'heure", "R\xC3\xA9" "duire", "Tout r\xC3\xA9" "duire"};
    check(labels == wanted && allEnabled, "le clic droit d'un message : Repondre, Copier, Copier avec l'auteur et l'heure, Reduire, Tout reduire");
    if (auto* menu = list->contextMenu()) menu->itemChosen->emit(5);
    paintInto(r, pane, theme, frame);
    check(list->collapsed(static_cast<std::size_t>(list->indexOfId("m1"))), "<< Tout reduire >> : le compte rendu est reduit de nouveau");

    // ---- REPONDRE ----
    i1 = list->indexOfId("m1");
    const auto row1 = static_cast<std::size_t>(i1);
    pane.dispatch(ui::MouseMove{center(list->toggleRect(row1)), {}, none});
    paintInto(r, pane, theme, frame);
    check(list->hoveredRow() == i1 && list->hoverButton(row1, 0).w > 0.f && list->hoverButton(row1, 1).w > 0.f,
          "le survol d'un message : << Repondre >> et << Copier >>");
    pane.dispatch(ui::MouseDown{center(list->hoverButton(row1, 0)), ui::MouseButton::Left, 1, none});
    paintInto(r, pane, theme, frame);
    const std::string label = app::sessionui::replyLabel(kCamille, session::chatExcerpt(kReport));
    auto* bar = pane.replyBar();
    check(bar->active() && bar->replyId() == "m1" && bar->label() == label && bar->visible(), "<< Repondre >> : << " + bar->label() + " >> au-dessus du champ");
    const auto* shown = d150::textOf(r, bar->shown());
    check(shown && shown->rect.x >= bar->bounds().x && shown->rect.right() <= bar->bounds().right() + 0.5f && bar->cancelButton()->visible(),
          "la barre de reponse : son texte dans son cadre, sa croix");
    check(pane.send("Oui, je relis les libell\xC3\xA9s.") && !sent.empty() && sent.back().first == "Oui, je relis les libell\xC3\xA9s." && sent.back().second == "m1",
          "envoyer : le message part avec la reference (m1)");
    check(!bar->active(), "envoye : plus de reponse en cours");
    // La croix annule.
    list->reply(row1);
    check(bar->active(), "Repondre de nouveau");
    paintInto(r, pane, theme, frame);
    pane.dispatch(ui::MouseDown{center(bar->cancelButton()->bounds()), ui::MouseButton::Left, 1, none});
    pane.dispatch(ui::MouseUp{center(bar->cancelButton()->bounds()), ui::MouseButton::Left, none});
    check(!bar->active(), "la croix : la reponse est annulee");
    // La reponse revient de l'hote (m5, `reponse` {m1}) : l'extrait au-dessus du texte ; un clic dessus va au compte rendu.
    session::ChatMessage m5{"m5", kLea, "Oui, je relis les libell\xC3\xA9s.", "14:41", 0x2E86DE, {"m1", kCamille, session::chatExcerpt(kReport)}};
    b.chat.push_back(d150::lineOf(d150::wire(m5, "1.50.0"), kLea));
    for (int k = 0; k < 4; ++k)   // de quoi faire defiler : le compte rendu sort de la vue
        b.chat.push_back(d150::lineOf(d150::wire({"m" + std::to_string(10 + k), kCamille, "Message " + std::to_string(k + 1) + " de la suite.", "14:5" + std::to_string(k),
                                                  0xE67E22, {}},
                                                 "1.50.0"),
                                      kLea));
    sm::setBoard(b);
    pane.refresh();
    const gfx::Rect low{0.f, 0.f, 900.f, 220.f};
    pane.setBounds(low);
    paintInto(r, pane, theme, low);
    const int i5 = list->indexOfId("m5");
    const auto row5 = static_cast<std::size_t>(std::max(0, i5));
    const auto lb = list->bounds();
    const auto inView = [&](std::size_t i) {
        const auto t = list->toggleRect(i);
        return t.w > 0.f && t.y >= lb.y - 0.5f && t.bottom() <= lb.bottom() + 0.5f;
    };
    i1 = list->indexOfId("m1");
    check(i5 >= 0 && list->quoteRect(row5).w > 0.f && list->rows()[row5].replyId == "m1" && !inView(static_cast<std::size_t>(i1)),
          "la reponse : l'extrait cite au-dessus de son texte (le compte rendu hors de la vue)");
    const auto q5 = list->quoteRect(row5);
    check(q5.w > 0.f && q5.y >= lb.y - 0.5f && q5.bottom() <= lb.bottom() + 0.5f, "l'extrait de la reponse est en vue");
    pane.dispatch(ui::MouseDown{center(q5), ui::MouseButton::Left, 1, none});
    paintInto(r, pane, theme, low);
    check(list->selected() == i1 && inView(static_cast<std::size_t>(i1)), "un clic sur l'extrait : le message cite en vue, choisi (mis en valeur)");
    // Un message a l'ancienne (sans identifiant) : on y repond (son identifiant tire du message, le meme chez chacun).
    const int i3 = list->indexOfId(m3.id);
    check(i3 >= 0 && m3.id == session::chatIdOf(kHugo, "14:33", "J'ai fini les voyants.") && list->reply(static_cast<std::size_t>(i3)) && bar->replyId() == m3.id,
          "un message a l'ancienne : on y repond (son identifiant tire du message)");
    check(pane.send("Merci Hugo.") && sent.back().second == m3.id, "... la reponse porte son identifiant");
    // Une reponse dont le message cite est parti de la liste : son extrait reste, le clic ne va nulle part.
    b.chat.push_back(d150::lineOf(d150::wire({"m20", kCamille, "Et celui-ci ?", "15:02", 0xE67E22, {"m0", kLea, "Un tr\xC3\xA8s vieux message"}}, "1.50.0"), kLea));
    sm::setBoard(b);
    pane.refresh();
    paintInto(r, pane, theme, low);
    const auto row20 = static_cast<std::size_t>(std::max(0, list->indexOfId("m20")));
    check(list->quoteRect(row20).w > 0.f && !list->goToQuoted(row20), "le message cite n'est plus dans la liste : l'extrait reste, le clic ne va nulle part");

    // ---- COPIER ----
    pane.setBounds(frame);
    list->scrollToBottom();
    paintInto(r, pane, theme, frame);
    const auto* t20 = d150::textOf(r, "Et celui-ci ?");
    check(t20 != nullptr, "le dernier message est montre");
    if (t20) pane.dispatch(ui::MouseDown{center(t20->rect), ui::MouseButton::Left, 1, none});
    g_clipboard.clear();
    pane.dispatch(ui::KeyDown{ui::Key::C, ctrl, false});
    check(list->selected() == list->indexOfId("m20") && g_clipboard == "Et celui-ci ?", "un clic choisit le message, Ctrl+C le copie (le texte seul)");
    list->openMenu(list->selected(), t20 ? center(t20->rect) : gfx::Point{});
    if (auto* menu = list->contextMenu()) menu->itemChosen->emit(3);
    check(g_clipboard == "15:02 " + kCamille + " : Et celui-ci ?", "<< Copier avec l'auteur et l'heure >> : << 15:02 Camille Essai... : Et celui-ci ? >>");
    // << Copier >> du survol d'un message reduit : son texte entier.
    pane.setBounds({0.f, 0.f, 900.f, 1200.f});
    paintInto(r, pane, theme, {0.f, 0.f, 900.f, 1200.f});
    i1 = list->indexOfId("m1");
    pane.dispatch(ui::MouseMove{center(list->toggleRect(static_cast<std::size_t>(i1))), {}, none});
    g_clipboard.clear();
    pane.dispatch(ui::MouseDown{center(list->hoverButton(static_cast<std::size_t>(i1), 1)), ui::MouseButton::Left, 1, none});
    check(list->collapsed(static_cast<std::size_t>(i1)) && g_clipboard == kReport, "<< Copier >> d'un message reduit : tout son texte (huit lignes)");

    // ---- LE JOURNAL SEUL : Copier seulement ----
    b.journal.push_back({"14:34", kLea, "Vue_Accueil \xE2\x80\xBA Texte_Etat d\xC3\xA9plac\xC3\xA9", 0x2E86DE, false, true, 3});
    sm::setBoard(b);
    pane.refresh();
    pane.setFilter(app::HmiSessionPane::Filter::Journal);
    paintInto(r, pane, theme, frame);
    list->openMenu(0, {20.f, 60.f});
    bool replyOff = false;
    if (auto* menu = list->contextMenu())
        for (const auto& it : menu->items())
            if (it.label == "R\xC3\xA9pondre") replyOff = !it.enabled && it.disabledReason == "une ligne du journal";
    check(list->rows().size() == 1 && list->hoverButton(0, 0).w <= 0.f && list->hoverButton(0, 1).w > 0.f && replyOff,
          "le journal : Copier seulement (Repondre grise : << une ligne du journal >>)");
    check(list->copy(0, false) && g_clipboard == "Vue_Accueil \xE2\x80\xBA Texte_Etat d\xC3\xA9plac\xC3\xA9  (\xC3\x97" "3)", "copier une ligne du journal : son texte et ses gestes");
    pane.setFilter(app::HmiSessionPane::Filter::All);

    // ---- CHEZ CAMILLE (l'autre membre) : les memes identifiants, la meme citation ----
    {
        sm::Board c = b;
        c.host = true;
        for (auto& l : c.chat) l.mine = l.from == kCamille;
        sm::setBoard(c);
        app::HmiSessionPane other("sortie.session.camille");
        other.setBounds(frame);
        paintInto(r, other, theme, frame);
        auto* l2 = other.list();
        const int j5 = l2->indexOfId("m5"), j3 = l2->indexOfId(m3.id);
        check(j5 >= 0 && j3 >= 0 && l2->rows()[static_cast<std::size_t>(j5)].replyWho == kCamille && l2->goToQuoted(static_cast<std::size_t>(j5))
                  && l2->selected() == l2->indexOfId("m1"),
              "chez Camille : la reponse de Lea cite son compte rendu (m1), le clic y va");
    }
    // La fenetre de la session (sessionwin::Body) : la meme liste - Repondre y ouvre la barre au-dessus du champ.
#ifdef XPG_SESSION_WINDOW
    {
        auto rig = hostWindow(false, false);
        bool asked = false;
        rig.body->lines()->onReply = [&](const app::sessionui::Row& row) {
            asked = true;
            rig.body->replyBar()->set(row);
            rig.body->invalidateLayout();
        };
        rig.body->setBounds({0.f, 0.f, 943.f, 609.f});
        paintInto(r, *rig.body, theme, {0.f, 0.f, 943.f, 609.f});
        auto* wl = rig.body->lines();
        const int k5 = wl->indexOfId("m5");
        check(k5 >= 0 && wl->reply(static_cast<std::size_t>(k5)) && asked, "la fenetre de la session : Repondre (la meme liste)");
        paintInto(r, *rig.body, theme, {0.f, 0.f, 943.f, 609.f});
        const auto rb = rig.body->replyBar()->bounds();
        const auto fb = rig.body->field->bounds();
        check(rig.body->replyBar()->visible() && rb.bottom() <= fb.y + 0.5f && rb.x >= fb.x - 0.5f && wl->bounds().bottom() <= rb.y + 0.5f,
              "la fenetre : << En reponse a ... >> entre la discussion et le champ");
        capture(*rig.body, {943.f, 609.f}, "fenetre_session_943x609_reponse.png");
    }
#endif
    pane.setBounds({0.f, 0.f, 900.f, 420.f});
    capture(pane, {900.f, 420.f}, "onglet_session_900x420_discussion.png");
    sm::setActive(false);
    sm::setBoard({});
    sm::setHooks({});
}
