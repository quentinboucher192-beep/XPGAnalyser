// =============================================================================
//  tests/session_ui_assistant.inl - 1.50.1 : LA DISCUSSION DE L'ASSISTANT IA AVEC
//  LES GESTES DES SESSIONS, DANS SON CADRE (inclus par session_ui_test.cpp)
// -----------------------------------------------------------------------------
//  La 1.50.1 donne a la discussion de l'Assistant IA repondre / reduire / copier
//  en reprenant les widgets des sessions (sessionui::ReplyBar, les memes regles).
//  La regle de la 1.47 vaut aussi la : du volet le plus etroit (280 px) au plus
//  large (460 px), avec la vraie police - la barre << En reponse a Assistant :
//  << ... >> >> au-dessus de la saisie, l'extrait cite dans la bulle d'une
//  demande, << ... voir tout >> sous une longue reponse reduite, les pictogrammes
//  du survol : aucun texte hors de son cadre, les pictogrammes dans la
//  discussion, le texte de la barre raccourci (entier dans l'infobulle).
//  --captures : les images (le dessin logiciel rasterise).
// =============================================================================

void assistantDiscussion(SoftRenderer& r) {
    std::printf("-- 1.50.1 : la discussion de l'Assistant IA (repondre, reduire, copier) dans son cadre\n");
    namespace ai = app::ai;
    const auto dir = std::filesystem::temp_directory_path() / "xpg_sessionui_assistant";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ai::setRoot((dir / "IA").string());
    const auto msg = [](const std::string& id, const std::string& role, const std::string& text) {
        ai::Message m;
        m.id = id;
        m.role = role;
        m.date = "2026-11-13T14:32:00";
        m.text = text;
        if (role == "assistant") m.state = "fini";
        return m;
    };
    ai::Conversation c;
    c.id = "c-sessionui-assistant";
    c.title = "Essai cadre";
    c.created = c.modified = "2026-11-13T14:30:00";
    c.messages.push_back(msg("m1", "utilisateur", "Comment compter les pompes en marche de la station de relevage nord ?"));
    c.messages.push_back(msg("m2", "assistant",
                             "## Compter les pompes en marche de la station de relevage nord\n"
                             "Voici une m\xC3\xA9thode en plusieurs \xC3\xA9tapes, valable pour toutes les stations du projet.\n"
                             "- Lister les pompes du projet (Pompe_Relevage_Nord_1 \xC3\xA0 Pompe_Relevage_Nord_12)\n"
                             "- Lire leur \xC3\xA9tat de marche\n"
                             "- Additionner celles en marche\n"
                             "- Afficher le total sur la vue\n\n"
                             "```st\nNb := 0;\nFOR i := 1 TO 12 DO\n  IF Pompe_Relevage_Nord[i].Marche THEN Nb := Nb + 1; END_IF;\nEND_FOR;\n```\n"
                             "C'est tout."));
    c.messages.push_back(msg("m3", "utilisateur", "Merci."));
    ai::Message reply = msg("m4", "utilisateur", "Et pour les vannes motoris\xC3\xA9" "es de la station de relevage nord, la m\xC3\xAAme chose ?");
    ai::chatreply::writeQuote(reply, ai::chatreply::quoteOf(c.messages[1]));
    c.messages.push_back(reply);
    c.messages.push_back(msg("m5", "assistant", "Oui : la m\xC3\xAAme boucle sur les vannes."));
    std::string why;
    check(ai::saveConversation(c, &why), "la conversation d'essai s'ecrit (" + why + ")");

    ai::AiPanel panel("ai.panneau", ai::Assistant::Host{}, ai::AiPanel::UiHost{});
    panel.ensureLoaded();
    check(panel.assistant().select(c.id), "la conversation s'ouvre");
    panel.sync();
    const auto theme = ui::Theme::dark();
    int outside = 0, icons = 0, quotes = 0, collapsed = 0, bars = 0;
    for (const float w : {280.f, 320.f, 360.f, 400.f, 460.f}) {
        const gfx::Rect frame{0.f, 0.f, w, 820.f};
        panel.setBounds(frame);
        panel.layout();
        auto& chat = panel.chat();
        check(panel.replyTo(1), "Repondre a la longue reponse (" + num(w) + " px)");
        panel.sync();
        panel.layout();
        // la barre au-dessus de la saisie : dans le volet, son texte tient (raccourci), entier dans l'infobulle
        auto& bar = panel.replyBar();
        paintInto(r, bar, theme, frame);
        bars += textsOutside(r, bar.bounds(), "la barre a " + num(w) + " px");
        const bool barOk = bar.visible() && bar.bounds().x >= frame.x && bar.bounds().right() <= frame.right() + 0.5f
                           && bar.bounds().bottom() <= panel.promptRect().y + 0.5f && !bar.shown().empty()
                           && bar.label().find("Compter les pompes") != std::string::npos;
        if (!barOk) std::printf("   la barre a %s px : %s, << %s >>\n", num(w).c_str(), rectText(bar.bounds()).c_str(), bar.shown().c_str());
        bars += barOk ? 0 : 1;
        // la longue reponse (avant le dernier echange) : reduite ; la demande qui la cite : l'extrait dans sa bulle
        chat.revealMessage(3, false);
        panel.layout();
        paintInto(r, chat, theme, frame);
        outside += textsOutside(r, chat.bounds(), "la discussion a " + num(w) + " px (la demande citee)");
        const gfx::Rect q = chat.quoteRect(3), b = chat.bubbleRect(3);
        if (q.w <= 0.f || q.x < b.x || q.right() > b.right() + 0.5f) {
            std::printf("   l'extrait a %s px : %s hors de la bulle %s\n", num(w).c_str(), rectText(q).c_str(), rectText(b).c_str());
            ++quotes;
        }
        chat.revealMessage(1, false);
        panel.layout();
        const gfx::Rect head = chat.messageRect(1);
        (void)panel.dispatch(ui::MouseMove{{head.x + head.w * 0.5f, head.y + 6.f}, {}, {}});
        paintInto(r, chat, theme, frame);
        outside += textsOutside(r, chat.bounds(), "la discussion a " + num(w) + " px (la reponse reduite)");
        if (!chat.collapsed(1) || chat.toggleRect(1).right() > chat.bounds().right() + 0.5f) ++collapsed;
        for (int k = 0; k < 2; ++k) {
            const gfx::Rect hb = chat.hoverButton(1, k);
            if (hb.w <= 0.f || hb.x < chat.bounds().x || hb.right() > chat.bounds().right() + 0.5f || hb.y < chat.bounds().y) {
                std::printf("   le pictogramme %d a %s px : %s (la discussion %s)\n", k, num(w).c_str(), rectText(hb).c_str(), rectText(chat.bounds()).c_str());
                ++icons;
            }
        }
        if (w == 280.f || w == 400.f) capture(panel, {w, 820.f}, "assistant_repondre_reduit_" + std::to_string(static_cast<int>(w)) + ".png");
        panel.cancelReply();
        panel.sync();
    }
    check(bars == 0, "la barre << En reponse a Assistant : ... >> : au-dessus de la saisie, son texte dans le cadre, de 280 a 460 px");
    check(outside == 0, "la discussion : aucun texte hors de son cadre (l'extrait cite, << ... voir tout >>), de 280 a 460 px");
    check(quotes == 0, "l'extrait cite reste dans la bulle de la demande, de 280 a 460 px");
    check(collapsed == 0, "la longue reponse : reduite, << ... voir tout >> dans le cadre, de 280 a 460 px");
    check(icons == 0, "Repondre et Copier (le survol) : dans la discussion, de 280 a 460 px");
    std::filesystem::remove_all(dir, ec);
}
