// =============================================================================
//  tests/session_ui_window.inl - 1.47.0 : la fenetre de la session (app/SessionWindow), dans session_ui_test.cpp
// -----------------------------------------------------------------------------
//  Le contenu de la fenetre (sessionwin::Body) avec ses boutons, rempli comme le
//  fait app/SessionDialogs (snapshotOf), pose dans des fenetres de 320 x 300 a
//  2560 x 1200 : l'hote (le code par Internet, trois equipiers, quelqu'un qui
//  frappe, la discussion, le journal vide - la capture du client - puis plein),
//  l'equipier, quelqu'un qui entre (pas encore de session), huit equipiers.
// =============================================================================

struct WindowRig {
    std::unique_ptr<app::sessionwin::Body> body;
    app::sessionwin::Snapshot snap;
};

ui::Button* addButton(app::sessionwin::Body& body, const std::string& text, const std::string& id, ui::Button::Style st) {
    auto* b = &static_cast<ui::Button&>(body.addChild(std::make_unique<ui::Button>(text, "dialog.session." + id)));
    b->setStyle(st);
    return b;
}

app::sessionwin::Snapshot hostSnapshot(bool journal, bool journalEmpty, std::size_t extraPeople = 0) {
    app::sessionwin::Snapshot s;
    s.active = true;
    s.title = "Session \xC2\xAB " + kSessionName + " \xC2\xBB \xE2\x80\x94 tu es l'h\xC3\xB4te";
    s.teamTitle = "L'\xC3\xA9quipe \xC2\xB7 " + std::to_string(3 + extraPeople);
    s.code = true;
    s.codeLabel = "Code de la session \xC2\xB7 par Internet, chiffr\xC3\xA9" "e de bout en bout";
    s.codeText = "K7Q4-M2XP-T9";
    s.people.push_back({0, kCamille, "(toi) \xC2\xB7 h\xC3\xB4te", "CE", "PC-BUREAU-ETUDES-01 \xC2\xB7 M-1A2B \xC2\xB7 modifie \xC2\xB7 Vue_Accueil", 0xE67E22});
    s.people.push_back({1, kLea, "", "LE",
                        "PC-ATELIER-02 \xC2\xB7 M-7F21 \xC2\xB7 modifie \xC2\xB7 Vue_Alarmes_Et_Defauts_De_L_Armoire_Gaz \xC2\xB7 3 objets", 0x2E86DE});
    s.people.push_back({2, kHugo, "", "HE", "PC-MAINTENANCE-03 \xC2\xB7 M-33C0 \xC2\xB7 regarde", 0x27AE60});
    for (std::size_t i = 0; i < extraPeople; ++i)
        s.people.push_back({static_cast<int>(3 + i), "Nina Essai " + std::to_string(i + 1), "", "NE", "PC-ESSAI-0" + std::to_string(i + 4) + " \xC2\xB7 modifie", 0x8E44AD});
    s.knocks.push_back({"Nina Essai-Longuenom de la Soci\xC3\xA9t\xC3\xA9 d'essai", "PC-ESSAI-LONGUENOM-04 \xC2\xB7 M-9D9D \xC2\xB7 192.168.100.240"});
    const auto b = board();
    for (const auto& l : b.chat) s.chat.push_back({l.time, l.from, l.text, l.system ? 0u : l.color, false, l.system, l.mine, 1});
    if (!journalEmpty)
        for (const auto& l : b.journal) s.journal.push_back({l.time, l.from, l.text, l.color, true, false, l.mine, l.count});
    s.showJournal = journal;
    s.hint = "Tu enregistres pour tout le monde (Ctrl+S). Fermer le projet arr\xC3\xAAte la session.";
    return s;
}

// La fenetre de l'hote : ses boutons comme SessionDialogs les cree.
WindowRig hostWindow(bool journal, bool journalEmpty, std::size_t extraPeople = 0) {
    WindowRig w;
    w.body = std::make_unique<app::sessionwin::Body>("dialog.session.corps");
    auto& b = *w.body;
    w.snap = hostSnapshot(journal, journalEmpty, extraPeople);
    b.copy = addButton(b, "Copier le code", "copier", ui::Button::Style::Flat);
    for (const auto& p : w.snap.people) {
        app::sessionwin::Body::MemberButtons row;
        const std::string n = std::to_string(p.number);
        if (p.number != 0) {
            row.follow = addButton(b, "Suivre", "suivre." + n, ui::Button::Style::Default);
            row.role = addButton(b, p.number == 2 ? "Laisser modifier" : "Regarder seulement", "role." + n, ui::Button::Style::Default);
            row.remove = addButton(b, "Retirer de la session\xE2\x80\xA6", "retirer." + n, ui::Button::Style::Danger);
        }
        b.rows.push_back(row);
    }
    b.knockRows.push_back({addButton(b, "Faire entrer", "entrer.1", ui::Button::Style::Primary), addButton(b, "Refuser", "refuser.1", ui::Button::Style::Default)});
    b.chatView = addButton(b, "Discussion", "vue.discussion", journal ? ui::Button::Style::Flat : ui::Button::Style::Primary);
    b.journalView = addButton(b, "Journal \xC2\xB7 " + std::to_string(w.snap.journal.size()), "vue.journal", journal ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    if (journal) b.exportJournal = addButton(b, "Exporter (CSV)", "journal.exporter", ui::Button::Style::Flat);
    b.field = &static_cast<ui::InputText&>(b.addChild(std::make_unique<ui::InputText>("dialog.session.message")));
    b.send = addButton(b, "Envoyer", "envoyer", ui::Button::Style::Primary);
    b.stop = addButton(b, "Arr\xC3\xAAter la session", "arreter", ui::Button::Style::Danger);
    b.close = addButton(b, "Fermer", "fermer", ui::Button::Style::Default);
    b.setSnapshot(w.snap);
    return w;
}

WindowRig guestWindow() {
    WindowRig w;
    w.body = std::make_unique<app::sessionwin::Body>("dialog.session.corps");
    auto& b = *w.body;
    w.snap = hostSnapshot(false, false);
    w.snap.title = "Session \xC2\xAB " + kSessionName + " \xC2\xBB \xE2\x80\x94 h\xC3\xB4te : " + kCamille;
    w.snap.code = false;
    w.snap.knocks.clear();
    w.snap.people[0].tag = "\xC2\xB7 h\xC3\xB4te";
    w.snap.people[1].tag = "(toi)";
    w.snap.hint = "C'est " + kCamille + " qui enregistre ; ta copie s'efface quand tu pars. Ctrl+Z annule tes gestes.";
    for (const auto& p : w.snap.people) {
        app::sessionwin::Body::MemberButtons row;
        if (p.number != 1) row.follow = addButton(b, "Suivre", "suivre." + std::to_string(p.number), ui::Button::Style::Default);
        b.rows.push_back(row);
    }
    b.chatView = addButton(b, "Discussion", "vue.discussion", ui::Button::Style::Primary);
    b.journalView = addButton(b, "Journal \xC2\xB7 2", "vue.journal", ui::Button::Style::Flat);
    b.field = &static_cast<ui::InputText&>(b.addChild(std::make_unique<ui::InputText>("dialog.session.message")));
    b.send = addButton(b, "Envoyer", "envoyer", ui::Button::Style::Primary);
    b.stop = addButton(b, "Quitter la session", "arreter", ui::Button::Style::Danger);
    b.close = addButton(b, "Fermer", "fermer", ui::Button::Style::Default);
    b.setSnapshot(w.snap);
    return w;
}

WindowRig joiningWindow() {
    WindowRig w;
    w.body = std::make_unique<app::sessionwin::Body>("dialog.session.corps");
    auto& b = *w.body;
    w.snap.title = "Session";
    w.snap.progress = "On entre dans la session \xC2\xAB " + kSessionName + " \xC2\xBB de " + kCamille +
                      " : le projet arrive (3,2 Mo sur 9,0 Mo). L'h\xC3\xB4te doit te faire entrer ; s'il demande le code, tape celui qu'il t'a donn\xC3\xA9.";
    b.stop = addButton(b, "Annuler", "annuler", ui::Button::Style::Default);
    b.close = addButton(b, "Fermer", "fermer", ui::Button::Style::Default);
    b.setSnapshot(w.snap);
    return w;
}

void fenetreSession(SoftRenderer& r) {
    std::printf("-- la fenetre de la session, de 320 x 300 a 2560 x 1200\n");
    const auto theme = ui::Theme::dark();
    struct Case {
        std::string name;
        std::function<WindowRig()> make;
    };
    const std::vector<Case> cases{
        {"hote, discussion", [] { return hostWindow(false, false); }},
        {"hote, journal vide", [] { return hostWindow(true, true); }},
        {"hote, journal", [] { return hostWindow(true, false); }},
        {"hote, 11 equipiers", [] { return hostWindow(false, false, 8); }},
        {"equipier", [] { return guestWindow(); }},
        {"on entre", [] { return joiningWindow(); }},
    };
    int bad = 0, sizes = 0;
    bool emptyWrapped = true, twoSeen = false, oneSeen = false;
    for (const auto& k : cases) {
        auto rig = k.make();
        auto& body = *rig.body;
        for (const float w : {320.f, 400.f, 480.f, 560.f, 640.f, 760.f, 943.f, 1100.f, 1400.f, 1920.f, 2560.f})
            for (const float h : {300.f, 400.f, 480.f, 609.f, 800.f, 1200.f}) {
                body.setBounds({0.f, 0.f, w, h});
                body.invalidateLayout();
                paintInto(r, body, theme, {0.f, 0.f, w, h});
                ++sizes;
                const gfx::Rect panel = body.panel();
                const std::string where = k.name + " " + num(w) + "x" + num(h);
                int here = 0;
                if (panel.x < -0.5f || panel.y < -0.5f || panel.right() > w + 0.5f || panel.bottom() > h + 0.5f) {
                    std::printf("   %s : le cadre %s sort de la fenetre\n", where.c_str(), rectText(panel).c_str());
                    ++here;
                }
                here += textsOutside(r, panel, where) + buttonProblems(body, panel, where);
                // Le champ dans le cadre.
                if (body.field && body.field->visible()) {
                    const auto f = body.field->bounds();
                    if (f.x < panel.x || f.right() > panel.right() + 0.5f || f.bottom() > panel.bottom() + 0.5f) {
                        std::printf("   %s : le champ %s hors du cadre\n", where.c_str(), rectText(f).c_str());
                        ++here;
                    }
                }
                // La capture du client : le texte du journal vide, retourne a la ligne dans la boite.
                if (k.name == "hote, journal vide" && body.lines() && body.lines()->visible()) {
                    const auto box = body.lines()->bounds();
                    for (const auto& t : r.texts)
                        if (t.text.rfind("Aucun", 0) == 0 && t.rect.right() > box.right() + 0.5f) emptyWrapped = false;
                }
                twoSeen = twoSeen || body.twoColumns();
                oneSeen = oneSeen || (!body.twoColumns() && k.name != "on entre");
                bad += here;
            }
    }
    check(bad == 0, "la fenetre de la session : chaque texte et chaque bouton dans le cadre, aucun chevauchement, " + std::to_string(sizes) +
                        " tailles (" + std::to_string(bad) + " ecart(s))");
    check(emptyWrapped, "le journal vide (la capture du client, 943 x 609) : << Aucun changement encore... >> retourne a la ligne dans sa boite");
    check(twoSeen && oneSeen, "deux colonnes quand il y a la place, l'une sous l'autre sinon");

    // Un nom raccourci : son texte entier dans l'infobulle (les textes coupes sont notes avec le texte entier).
    {
        auto rig = hostWindow(false, false);
        rig.body->setBounds({0.f, 0.f, 320.f, 609.f});
        paintInto(r, *rig.body, theme, {0.f, 0.f, 320.f, 609.f});
        const auto cut = rig.body->cutTexts();
        const bool title = std::find(cut.begin(), cut.end(), rig.snap.title) != cut.end();
        const bool lea = std::find(cut.begin(), cut.end(), kLea) != cut.end();
        check(title && lea, "320 px : le titre et le nom de L\xC3\xA9" "a raccourcis, entiers dans l'infobulle");
        // Les boutons d'un equipier sous son nom quand la colonne est etroite (sur deux lignes plutot que raccourcis) :
        // leur libelle entier ou court, jamais coupe (le bouton peut etre hors de la zone qui defile : cache).
        auto* remove = rig.body->rows[1].remove;
        check(remove && (remove->text() == "Retirer de la session\xE2\x80\xA6" || remove->text() == "Retirer\xE2\x80\xA6"),
              "320 px : << Retirer de la session... >> lisible (" + (remove ? remove->text() : std::string("absent")) + ")");
    }
    // Onze equipiers : l'equipe defile ; les boutons hors de la vue sont caches, ceux dedans y sont entiers.
    {
        auto rig = hostWindow(false, false, 8);
        rig.body->setBounds({0.f, 0.f, 943.f, 609.f});
        paintInto(r, *rig.body, theme, {0.f, 0.f, 943.f, 609.f});
        const auto view = rig.body->teamView();
        int hidden = 0, outside = 0;
        for (const auto& row : rig.body->rows)
            for (auto* b : {row.follow, row.role, row.remove}) {
                if (!b) continue;
                if (!b->visible()) ++hidden;
                else if (b->bounds().y < view.y - 0.5f || b->bounds().bottom() > view.bottom() + 0.5f) ++outside;
            }
        check(rig.body->teamContent() > view.h && hidden > 0 && outside == 0, "onze \xC3\xA9quipiers : l'\xC3\xA9quipe d\xC3\xA9" "file, rien ne d\xC3\xA9" "borde de sa zone");
    }
    // Les mots : coupes seulement s'ils sont plus longs que la ligne.
    {
        const std::string text = kLongMessage + " " + kLongWord;
        const auto lines = app::sessionui::wrap(text, gfx::FontId{13}, 220.f);
        bool whole = true, narrow = true;
        std::vector<std::string> words;
        for (std::size_t i = 0, j; i < text.size(); i = j + 1) {
            j = text.find(' ', i);
            if (j == std::string::npos) j = text.size();
            words.push_back(text.substr(i, j - i));
        }
        for (const auto& l : lines) {
            narrow = narrow && ui::measureWidth(l, gfx::FontId{13}) <= 220.5f;
            for (std::size_t i = 0, j; i < l.size(); i = j + 1) {
                j = l.find(' ', i);
                if (j == std::string::npos) j = l.size();
                const std::string w = l.substr(i, j - i);
                if (std::find(words.begin(), words.end(), w) == words.end() && kLongWord.find(w) == std::string::npos) whole = false;
            }
        }
        check(narrow && whole, "retour a la ligne : chaque ligne tient (220 px), les mots entiers sauf celui plus long que la ligne");
        check(app::sessionui::elide("\xC3\xA9t\xC3\xA9 \xC3\xA9t\xC3\xA9 \xC3\xA9t\xC3\xA9 \xC3\xA9t\xC3\xA9", gfx::FontId{13}, 40.f).find("\xE2\x80\xA6") != std::string::npos,
              "raccourci : << ... >>, jamais au milieu d'un caractere accentue");
    }
    // Les captures : etroite, celle du client (943 x 609, le journal vide), large.
    for (const auto& [w, h] : std::vector<std::pair<float, float>>{{480.f, 520.f}, {943.f, 609.f}, {1600.f, 1000.f}}) {
        const std::string size = std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h));
        {
            auto rig = hostWindow(true, true);
            rig.body->setBounds({0.f, 0.f, w, h});
            capture(*rig.body, {w, h}, "fenetre_session_" + size + "_journal_vide.png");
        }
        {
            auto rig = hostWindow(false, false);
            rig.body->setBounds({0.f, 0.f, w, h});
            capture(*rig.body, {w, h}, "fenetre_session_" + size + "_discussion.png");
        }
    }
}

// ------------------------------------------------------------- Demarrer, Rejoindre (sessionwin::FlowBody) ---
//  La fenetre << Rejoindre une session >> avec huit sessions trouvees (des noms d'hote longs), le code, une autre
//  adresse, une erreur : de 320 x 260 a 1920 x 1200 - tout dans le cadre ; ce qui ne tient pas en hauteur defile.
void fenetresDemarrerRejoindre(SoftRenderer& r) {
    std::printf("-- Rejoindre une session (la fenetre en lignes), de 320 x 260 a 1920 x 1200\n");
    using K = app::sessionwin::FlowBody::Item::Kind;
    const auto theme = ui::Theme::dark();
    app::sessionwin::FlowBody body;
    body.title = "Rejoindre une session \xE2\x80\x94 " + kSessionName;
    body.wanted = 660.f;
    body.error = "Aucune session ne r\xC3\xA9pond sur PC-BUREAU-ETUDES-TRES-LONG-NOM-01.essai.exemple.fr : v\xC3\xA9rifie l'adresse, et que l'h\xC3\xB4te a d\xC3\xA9marr\xC3\xA9 la session.";
    auto group = std::make_shared<ui::RadioGroup>();
    auto radio = [&](const std::string& text, int v) {
        return &static_cast<ui::RadioButton&>(body.addChild(std::make_unique<ui::RadioButton>(text, group, v, "rejoindre." + std::to_string(v))));
    };
    auto button = [&](const std::string& text, const std::string& id) {
        return &static_cast<ui::Button&>(body.addChild(std::make_unique<ui::Button>(text, "rejoindre." + id)));
    };
    body.add(K::Muted, "Ouvre le projet d'un \xC3\xA9quipier, avec lui, en m\xC3\xAAme temps. Comment le rejoins-tu ?");
    auto mode = std::make_shared<ui::RadioGroup>();
    body.row({&static_cast<ui::RadioButton&>(body.addChild(std::make_unique<ui::RadioButton>("Sur le r\xC3\xA9seau du bureau", mode, 0, "rejoindre.bureau"))),
              &static_cast<ui::RadioButton&>(body.addChild(std::make_unique<ui::RadioButton>("Par Internet", mode, 1, "rejoindre.internet")))},
             28.f);
    body.gap(8.f);
    for (int i = 0; i < 8; ++i) {
        body.widget(radio(kSessionName + " \xE2\x80\x94 h\xC3\xB4te : " + kCamille, i), 26.f);
        body.add(K::Muted, "PC-BUREAU-ETUDES-0" + std::to_string(i) + " \xC2\xB7 192.168.100." + std::to_string(200 + i) + " \xC2\xB7 IHM Studio 1.47.0 \xC2\xB7 3 dedans \xC2\xB7 il faut le code", 28.f);
    }
    body.add(K::Title, "Le code de la session (6 chiffres, donn\xC3\xA9 par l'h\xC3\xB4te " + kCamille + ")");
    body.code("Code de la session : K7Q4-M2XP-T9-" + kLongWord, button("Copier", "copier"));
    body.footer = {button("Annuler", "annuler"), button("Rejoindre la session de " + kCamille, "ok")};
    int bad = 0, sizes = 0;
    bool scrolled = false;
    for (const float w : {320.f, 400.f, 480.f, 640.f, 943.f, 1280.f, 1920.f})
        for (const float h : {260.f, 400.f, 609.f, 800.f, 1200.f}) {
            body.setBounds({0.f, 0.f, w, h});
            body.invalidateLayout();
            paintInto(r, body, theme, {0.f, 0.f, w, h});
            ++sizes;
            // Le cadre : le fond dessine (le premier rectangle du dialogue) - ici, la fenetre moins ses marges.
            const gfx::Rect frame{0.f, 0.f, w, h};
            const std::string where = "Rejoindre " + num(w) + "x" + num(h);
            bad += textsOutside(r, frame, where) + buttonProblems(body, frame, where);
            // Les choix : a l'interieur de la fenetre, et ceux qui sortent de la vue caches.
            for (const auto& c : body.children())
                if (c->visible() && (c->bounds().right() > w + 0.5f || c->bounds().bottom() > h + 0.5f || c->bounds().y < -0.5f)) {
                    std::printf("   %s : %s %s hors de la fenetre\n", where.c_str(), c->id().c_str(), rectText(c->bounds()).c_str());
                    ++bad;
                }
            int hidden = 0;
            for (const auto& c : body.children()) hidden += c->visibility() == ui::Visibility::Hidden ? 1 : 0;
            scrolled = scrolled || hidden > 0;
        }
    check(bad == 0, "Rejoindre : textes, choix et boutons dans la fenetre, " + std::to_string(sizes) + " tailles (" + std::to_string(bad) + " ecart(s))");
    check(scrolled, "Rejoindre : ce qui ne tient pas en hauteur defile (les choix hors de la vue caches)");
    body.setBounds({0.f, 0.f, 480.f, 520.f});
    capture(body, {480.f, 520.f}, "rejoindre_480x520.png");
}
