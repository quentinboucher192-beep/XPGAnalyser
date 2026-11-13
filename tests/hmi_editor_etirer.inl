// tests/hmi_editor_etirer.inl - 1.50.0 (planche 02) : ETIRER dans le clic droit d'un objet de la vue.
// Inclus par hmi_editor_test.cpp (Bench, check, near, same_text, findItem, Recorder). HMI_TEST_ETI=1 : seul.
//  Le moteur (hmi/HmiStretch) sans ecran : chaque cible (les bords de la vue, toute la largeur / hauteur / vue,
//  le voisin, le bord de la selection, un repere, une colonne), les marges gardees ou non, un groupe, un objet
//  tourne, une ligne, un objet verrouille ; puis l'editeur : le menu (Etirer > juste apres Aligner >), la barre
//  d'outils, l'Explorateur, Ctrl+Alt+Maj+fleche, un seul Ctrl+Z, l'apercu qui ne change rien, la fenetre Etirer...

namespace etirer1500ns {

Id rect(Project& p, View& v, const char* name, Box box) {
    const Id id = edit::add(p, v, Kind::Rectangle, box.x, box.y);
    v.object(id)->name = name;
    v.object(id)->setBox(box);
    return id;
}

void boxIs(const View& v, Id id, Box want, const std::string& what, double tol = 0.51) {
    const auto* o = v.object(id);
    if (!o) { check(false, what + " : l'objet"); return; }
    const Box b = o->box();
    near(b.x, want.x, what + " (x)", tol);
    near(b.y, want.y, what + " (y)", tol);
    near(b.w, want.w, what + " (largeur)", tol);
    near(b.h, want.h, what + " (hauteur)", tol);
}

} // namespace etirer1500ns

void etirer1500Moteur() {
    std::printf("== 1.50.0 : \xC3\x89tirer - le moteur (hmi::stretch) ==\n");
    namespace st = hmi::stretch;
    using etirer1500ns::rect;
    using etirer1500ns::boxIs;
    Project p;
    View v = makeView(p, "Vue_Ligne2");
    v.width = 1024;
    v.height = 600;
    v.grid.step = 8;
    // ---- la marge de la vue : la grille de colonnes montree, sinon le pas d'aimantation ----
    near(st::viewMargin(v), 8, "la marge de la vue : sans grille de colonnes, le pas d'aimantation (8)");
    v.columns.visible = true;
    v.columns.margin = 16;
    near(st::viewMargin(v), 16, "la marge de la vue : celle de la grille de colonnes montree (16)");
    near(st::neighbourGap(v), 8, "l'espacement avec un voisin : le pas d'aimantation");
    const Id courbe = rect(p, v, "Courbe_1", {200, 100, 412, 260});
    // ---- jusqu'au bord droit (la planche : + 396 px, largeur 412 -> 808, marge 16) ----
    {
        st::Request r;   // le bord droit, la vue, les marges gardees
        const View before = v;
        const auto plan = st::plan(v, {courbe}, r);
        check(v == before, "le plan ne change rien a la vue (l'apercu)");
        check(plan.possible() && plan.changes.size() == 1 && plan.why.empty(), "jusqu'au bord droit : un objet a etirer");
        if (plan.possible()) {
            near(plan.changes[0].after.right(), 1008, "le bord droit va a la vue moins la marge (1024 - 16)");
            near(plan.changes[0].after.x, 200, "le bord gauche ne bouge pas");
            same_text(st::deltaText(plan.changes[0]), "+ 396 px \xC2\xB7 largeur 412 \xE2\x86\x92 808", "l'apercu dit ce qui change (la planche)");
            same_text(st::sizeText(plan.changes[0]), "x 200 \xC2\xB7 largeur 412 \xE2\x86\x92 808 \xC2\xB7 hauteur 260 (ne change pas)",
                      "la fenetre dit la taille obtenue (la planche)");
        }
        near(plan.margin, 16, "la marge appliquee : 16");
        same_text(st::sentence(v, r, plan),
                  "\xC3\x89tirement de Courbe_1 jusqu'au bord droit de la vue : largeur 412 \xE2\x86\x92 808 px (marge 16 px gard\xC3\xA9" "e). "
                  "Ctrl+Z l'annule.",
                  "la phrase de la barre d'etat");
        View w = v;
        st::apply(w, plan);
        boxIs(w, courbe, {200, 100, 808, 260}, "apply : Courbe_1 etiree jusqu'au bord droit");
        // Deja au bord : rien, la raison dite.
        const auto again = st::plan(w, {courbe}, r);
        check(!again.possible() && again.why == "d\xC3\xA9j\xC3\xA0 au bord", "deja au bord : grise, \"deja au bord\"");
    }
    // ---- chaque bord, toute la largeur / hauteur / vue ; les marges gardees ou non ----
    {
        const auto run = [&](unsigned edges, bool keep, Box want, const char* what) {
            st::Request r;
            r.edges = edges;
            r.keepMargins = keep;
            View w = v;
            st::apply(w, st::plan(v, {courbe}, r));
            boxIs(w, courbe, want, what);
        };
        run(st::kLeft, true, {16, 100, 596, 260}, "jusqu'au bord gauche (marge 16) : le bord droit reste a 612");
        run(st::kTop, true, {200, 16, 412, 344}, "jusqu'en haut (marge 16)");
        run(st::kBottom, true, {200, 100, 412, 484}, "jusqu'en bas (600 - 16)");
        run(st::kWidth, true, {16, 100, 992, 260}, "toute la largeur (les deux bords, marges gardees)");
        run(st::kHeight, true, {200, 16, 412, 568}, "toute la hauteur");
        run(st::kAll, true, {16, 16, 992, 568}, "toute la vue (les quatre bords)");
        run(st::kRight, false, {200, 100, 824, 260}, "Garder les marges decochee : l'objet touche le bord (1024)");
        run(st::kAll, false, {0, 0, 1024, 600}, "toute la vue, sans marge");
        st::Request custom;
        custom.margin = 40;   // la fenetre : une marge donnee
        View w = v;
        st::apply(w, st::plan(v, {courbe}, custom));
        near(w.object(courbe)->box().right(), 984, "une marge donnee (la fenetre) : 1024 - 40");
    }
    // ---- l'objet voisin ----
    {
        View w = v;
        const Id panneau = rect(p, w, "Panneau_Etat", {700, 150, 200, 80});   // a droite, chevauche en hauteur
        (void)rect(p, w, "Loin", {700, 500, 100, 50});                        // a droite mais pas en face : ignore
        st::Request r;
        r.to = st::To::Neighbour;
        const auto plan = st::plan(w, {courbe}, r);
        check(plan.possible() && plan.neighbour == "Panneau_Etat", "le voisin a droite : Panneau_Etat (celui d'en face)");
        st::apply(w, plan);
        near(w.object(courbe)->box().right(), 692, "il s'arrete avant lui, l'espacement garde (700 - 8)");
        check(st::neighbour(w, courbe, st::kRight, {courbe}) == panneau, "neighbour() : le plus proche du cote droit");
        st::Request left = r;
        left.edges = st::kLeft;
        const auto none = st::plan(w, {courbe}, left);
        check(!none.possible() && none.why == "aucun objet \xC3\xA0 gauche", "aucun voisin a gauche : grise, la raison dite");
        st::Request touch = r;
        touch.keepMargins = false;
        View t = v;
        (void)rect(p, t, "Panneau_Etat", {700, 150, 200, 80});
        st::apply(t, st::plan(t, {courbe}, touch));
        near(t.object(courbe)->box().right(), 700, "sans marge : contre lui");
    }
    // ---- le bord de la selection (deux objets ou plus) ----
    {
        View w = v;
        const Id a = rect(p, w, "A", {100, 400, 100, 30});
        const Id b = rect(p, w, "B", {150, 440, 300, 30});
        const Id c = rect(p, w, "C", {120, 480, 200, 30});
        st::Request r;
        r.to = st::To::Selection;
        const auto one = st::plan(w, {a}, r);
        check(!one.possible() && one.why == "deux objets ou plus", "le bord de la selection : un objet seul, grise (deux objets ou plus)");
        const auto plan = st::plan(w, {a, b, c}, r);
        check(plan.changes.size() == 2, "les 3 objets : A et C s'etirent, B est deja au bord");
        st::apply(w, plan);
        near(w.object(a)->box().right(), 450, "A jusqu'au bord droit de la selection (450)");
        near(w.object(c)->box().right(), 450, "C aussi (Aligner a droite qui agrandit)");
        near(w.object(a)->box().x, 100, "A garde son bord gauche");
    }
    // ---- un repere ----
    {
        View w = v;
        st::Request r;
        r.to = st::To::Guide;
        r.index = 0;
        const auto none = st::plan(w, {courbe}, r);
        check(!none.possible() && none.why == "aucun rep\xC3\xA8re", "aucun repere : grise, la raison dite");
        w.guides.push_back({true, 900});
        w.guides.push_back({true, 50});
        w.guides.push_back({true, 400});
        w.guides.push_back({false, 500});
        View g = w;
        st::apply(g, st::plan(w, {courbe}, r));
        near(g.object(courbe)->box().right(), 900, "un repere vertical a droite : le bord droit y va (pas de marge)");
        r.index = 1;
        g = w;
        st::apply(g, st::plan(w, {courbe}, r));
        near(g.object(courbe)->box().x, 50, "un repere a gauche : le bord gauche y va");
        near(g.object(courbe)->box().right(), 612, "... le droit reste");
        r.index = 2;
        const auto inside = st::plan(w, {courbe}, r);
        check(!inside.possible() && inside.why == "le rep\xC3\xA8re traverse l'objet", "un repere qui traverse l'objet : grise, la raison dite");
        r.index = 3;
        g = w;
        st::apply(g, st::plan(w, {courbe}, r));
        near(g.object(courbe)->box().bottom(), 500, "un repere horizontal dessous : le bas y va");
        // La fenetre : le repere le plus proche dans le sens du bord.
        st::Request nearest;
        nearest.to = st::To::Guide;
        nearest.edges = st::kRight;
        g = w;
        st::apply(g, st::plan(w, {courbe}, nearest));
        near(g.object(courbe)->box().right(), 900, "le repere le plus proche a droite (la fenetre)");
    }
    // ---- une colonne de la grille ----
    {
        View w = v;
        w.columns.count = 4;
        w.columns.gutter = 16;
        const auto spans = edit::columnSpans(w);
        check(spans.size() == 4, "4 colonnes");
        st::Request r;
        r.to = st::To::Column;
        r.index = 3;
        View g = w;
        st::apply(g, st::plan(w, {courbe}, r));
        if (spans.size() == 4) near(g.object(courbe)->box().right(), spans[3].x + spans[3].w, "jusqu'a la colonne 4 : son bord droit (1008)");
        near(g.object(courbe)->box().x, 200, "... le bord gauche reste");
        r.index = 0;
        g = w;
        st::apply(g, st::plan(w, {courbe}, r));
        near(g.object(courbe)->box().x, 16, "jusqu'a la colonne 1 : le bord gauche y va (16)");
        w.columns.visible = false;
        const auto hidden = st::plan(w, {courbe}, r);
        check(!hidden.possible() && hidden.why == "pas de grille de colonnes", "grille de colonnes cachee : grise, la raison dite");
    }
    // ---- un groupe s'etire en entier (ses objets a l'echelle) ----
    {
        View w = v;
        const Id g1 = rect(p, w, "G1", {100, 400, 100, 50});
        const Id g2 = rect(p, w, "G2", {250, 400, 50, 50});
        const Id grp = edit::group(p, w, {g1, g2});
        boxIs(w, grp, {100, 400, 200, 50}, "le groupe (100..300)");
        st::Request r;
        r.keepMargins = false;
        const auto plan = st::plan(w, {grp}, r);
        check(plan.changes.size() == 1 && plan.changes[0].id == grp, "un groupe : une seule unite");
        st::apply(w, plan);
        boxIs(w, grp, {100, 400, 924, 50}, "le groupe jusqu'au bord droit (1024)");
        boxIs(w, g1, {100, 400, 462, 50}, "son 1er objet a l'echelle (x 4,62)");
        boxIs(w, g2, {793, 400, 231, 50}, "son 2e objet a l'echelle, au bout");
    }
    // ---- un objet tourne : par sa boite ----
    {
        View w = v;
        const Id t = rect(p, w, "Tourne", {300, 300, 100, 40});
        w.object(t)->setNumber("rot", 90);
        const Box was = edit::rotatedBounds(*w.object(t));
        near(was.w, 40, "tourne d'un quart : sa boite a l'ecran fait 40 de large");
        st::Request r;
        r.keepMargins = false;
        st::apply(w, st::plan(w, {t}, r));
        const Box now = edit::rotatedBounds(*w.object(t));
        near(now.right(), 1024, "tourne : sa boite va jusqu'au bord");
        near(now.x, was.x, "... son bord gauche (a l'ecran) ne bouge pas");
        near(now.h, was.h, "... sa hauteur a l'ecran ne change pas");
        near(w.object(t)->box().h, now.w, "tourne d'un quart : c'est sa hauteur propre qui s'allonge");
        near(w.object(t)->rotation(), 90, "... toujours tourne de 90");
        const Id u = rect(p, w, "Penche", {200, 200, 100, 40});
        w.object(u)->setNumber("rot", 30);
        st::apply(w, st::plan(w, {u}, r));
        near(edit::rotatedBounds(*w.object(u)).right(), 1024, "tourne de 30 : sa boite atteint le bord");
    }
    // ---- une ligne : dans son sens seulement ----
    {
        View w = v;
        const Id h = edit::add(p, w, Kind::Line, 100, 300);   // 0,0 160,0
        w.object(h)->name = "Ligne_H";
        st::Request r;
        r.keepMargins = false;
        st::apply(w, st::plan(w, {h}, r));
        boxIs(w, h, {100, 300, 924, 0}, "une ligne horizontale s'allonge jusqu'au bord");
        same_text(w.object(h)->text("points"), "0,0 924,0", "ses points suivent");
        const Id vl = edit::add(p, w, Kind::Line, 500, 100);
        w.object(vl)->set("points", "0,0 0,120");
        w.object(vl)->setBox({500, 100, 0, 120});
        const auto across = st::plan(w, {vl}, r);
        check(!across.possible() && across.why == "une ligne s'\xC3\xA9tire dans son sens", "une ligne verticale en largeur : grisee, la raison dite");
        st::Request down = r;
        down.edges = st::kBottom;
        st::apply(w, st::plan(w, {vl}, down));
        boxIs(w, vl, {500, 100, 0, 500}, "... mais elle s'allonge vers le bas");
        const Id d = edit::add(p, w, Kind::Line, 100, 100);
        w.object(d)->set("points", "0,0 100,50");
        w.object(d)->setBox({100, 100, 100, 50});
        st::Request right = r;
        st::apply(w, st::plan(w, {d}, right));
        boxIs(w, d, {100, 100, 924, 462}, "une ligne en biais : elle s'allonge, son angle garde (largeur et hauteur a l'echelle)");
        st::Request whole = r;
        whole.edges = st::kAll;
        View z = w;
        const Id h2 = edit::add(p, z, Kind::Line, 100, 50);
        st::apply(z, st::plan(z, {h2}, whole));
        boxIs(z, h2, {0, 50, 1024, 0}, "toute la vue, une ligne : dans son sens (toute la largeur)");
    }
    // ---- un objet verrouille : grise, la raison ; avec d'autres : pas touche, dit ----
    {
        View w = v;
        const Id lk = rect(p, w, "Bouton_2", {100, 450, 80, 30});
        w.object(lk)->locked = true;
        st::Request r;
        const auto plan = st::plan(w, {lk}, r);
        check(!plan.possible() && plan.why == "verrouill\xC3\xA9", "un objet verrouille : grise, \"verrouille\"");
        const auto both = st::plan(w, {lk, courbe}, r);
        check(both.changes.size() == 1 && both.skipped.size() == 1 && both.skipped[0] == "Bouton_2 (verrouill\xC3\xA9)",
              "avec un objet libre : lui seul s'etire, le verrouille est dit");
        check(st::sentence(w, r, both).find("Pas touch\xC3\xA9 : Bouton_2 (verrouill\xC3\xA9)") != std::string::npos,
              "la barre d'etat dit qui n'a pas ete touche");
    }
}

void etirer1500Editeur() {
    std::printf("== 1.50.0 : \xC3\x89tirer - l'editeur (menu, barre, apercu, Ctrl+Z, fenetre) ==\n");
    namespace st = hmi::stretch;
    using etirer1500ns::boxIs;
    using app::HmiEditor;
    Bench b;
    b.v().grid.snapGrid = false;
    const Id a = edit::add(b.doc->project, b.v(), Kind::Rectangle, 100, 100);
    const Id c = edit::add(b.doc->project, b.v(), Kind::Rectangle, 100, 300);
    b.v().object(a)->name = "Rect_A";
    b.v().object(c)->name = "Rect_C";
    b.v().object(a)->setBox({100, 100, 200, 80});
    b.v().object(c)->setBox({100, 300, 300, 80});
    b.editor->refresh();
    auto& cv = b.editor->canvas();
    auto* menu = b.editor->canvasMenu();
    if (!menu) { check(false, "l'editeur a son menu"); return; }
    const auto rightClick = [&](double x, double y) {
        cv.dispatch(ui::MouseDown{b.screen(x, y), ui::MouseButton::Right, 1, {}});
        cv.dispatch(ui::MouseUp{b.screen(x, y), ui::MouseButton::Right, {}});
    };
    const auto indexOf = [](const std::vector<ui::PopupMenu::Item>& items, std::string_view label) {
        for (std::size_t i = 0; i < items.size(); ++i) if (items[i].label == label) return static_cast<int>(i);
        return -1;
    };
    // ---- le menu : Etirer > juste apres Aligner >, marque nouveau ----
    cv.setSelection({a});
    rightClick(150, 140);
    check(menu->isOpen(), "le clic droit sur Rect_A ouvre le menu");
    const auto items = menu->items();
    const int ali = indexOf(items, "Aligner"), eti = indexOf(items, "\xC3\x89tirer"), tra = indexOf(items, "Transformer");
    check(ali >= 0 && eti == ali + 1 && tra == eti + 1, "Etirer > juste apres Aligner >, avant Transformer >");
    const auto* stretchItem = eti >= 0 ? &items[static_cast<std::size_t>(eti)] : nullptr;
    check(stretchItem && stretchItem->enabled && !stretchItem->children.empty() && stretchItem->badge == "nouveau" && stretchItem->paintIcon,
          "Etirer : un sous-menu, marque nouveau (1.50.1 : la pastille), son icone");
    if (!stretchItem) return;
    const auto& sub = stretchItem->children;
    std::string order;
    for (const auto& it : sub) order += (it.separator ? std::string("|") : it.label) + ";";
    same_text(order,
              "Jusqu'au bord de la vue;Jusqu'au bord gauche;Jusqu'au bord droit;Jusqu'en haut;Jusqu'en bas;|;Toute la largeur de la vue;"
              "Toute la hauteur de la vue;Toute la vue;Jusqu'\xC3\xA0\xE2\x80\xA6;L'objet voisin;Le bord de la s\xC3\xA9lection;Un rep\xC3\xA8re;"
              "Une colonne de la grille;|;Garder les marges (10 px);\xC3\x89tirer\xE2\x80\xA6;",
              "le sous-menu Etirer, dans l'ordre de la planche");
    const auto* right = findItem(sub, "Jusqu'au bord droit");
    check(right && right->enabled && right->shortcut == "Ctrl+Alt+Maj+\xE2\x86\x92" && right->id == HmiEditor::kStretchMenu + 1,
          "Jusqu'au bord droit : Ctrl+Alt+Maj+fleche droite (les touches etaient libres)");
    const auto* left = findItem(sub, "Jusqu'au bord gauche");
    check(left && left->shortcut == "Ctrl+Alt+Maj+\xE2\x86\x90", "Jusqu'au bord gauche : Ctrl+Alt+Maj+fleche gauche");
    const auto* selBorder = findItem(sub, "Le bord de la s\xC3\xA9lection");
    check(selBorder && !selBorder->enabled && selBorder->disabledReason == "deux objets ou plus", "le bord de la selection : grise, deux objets ou plus");
    const auto* guide = findItem(sub, "Un rep\xC3\xA8re");
    check(guide && !guide->enabled && guide->disabledReason == "aucun rep\xC3\xA8re", "un repere : grise, aucun repere");
    const auto* column = findItem(sub, "Une colonne de la grille");
    check(column && !column->enabled && column->disabledReason == "pas de grille de colonnes", "une colonne : grise, pas de grille de colonnes");
    const auto* voisin = findItem(sub, "L'objet voisin");
    const auto* below = voisin ? findItemStarting(voisin->children, "En bas") : nullptr;
    check(voisin && voisin->enabled && below && below->enabled && below->label == "En bas : Rect_C",
          "l'objet voisin > En bas : Rect_C (son nom)");
    const auto* keep = findItem(sub, "Garder les marges (10 px)");
    check(keep && keep->checkable && keep->checked, "Garder les marges : cochee d'office (la marge de la vue : le pas, 10 px)");
    check(ui::cmd::badShortcuts(items).empty(), "la colonne de droite : des touches seulement (Ctrl+Alt+Maj+fleches)");

    // ---- l'apercu au survol : rien ne change avant le clic ----
    {
        const auto before = b.v();
        check(eti >= 0 && menu->openSubmenuAt(0, static_cast<std::size_t>(eti)) && menu->columnCount() == 2, "le sous-menu Etirer s'ouvre");
        const int row = indexOf(menu->columnItems(1), "Jusqu'au bord droit");
        const auto r = menu->columnItemRect(1, static_cast<std::size_t>(std::max(0, row)));
        menu->dispatch(ui::MouseMove{{r.x + 20.f, r.y + r.h * 0.5f}, {0, 0}, {}});
        check(menu->highlightedIn(1) == row, "la souris sur Jusqu'au bord droit");
        Recorder rec;
        const auto theme = ui::Theme::dark();
        b.editor->layout();
        b.editor->render(ui::PaintContext{rec, theme, {0, 0, 2000, 1000}, 0.0, nullptr});
        const auto& pv = cv.stretchPreview();
        check(pv.has_value() && pv->changes.size() == 1, "l'apercu est sur la vue");
        if (pv && !pv->changes.empty()) near(pv->changes[0].after.right(), 990, "l'apercu : le bord droit a 1000 - 10");
        check(rec.wrote("+ 690 px \xC2\xB7 largeur 200 \xE2\x86\x92 890") && rec.wrote("marge 10"), "l'apercu dessine ce qui change (+ 690 px) et la marge");
        check(b.v() == before, "l'apercu ne change rien a la vue");
        menu->close();
        b.editor->syncStretchPreview();
        check(!cv.stretchPreview().has_value(), "le menu ferme : l'apercu s'efface");
        check(b.v() == before, "... et rien n'a bouge");
    }
    // ---- le clic : un objet etire, la barre d'etat, Ctrl+Z ----
    {
        const std::size_t done = b.stack.done().size();
        rightClick(150, 140);
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 1);   // Jusqu'au bord droit
        menu->close();
        near(b.v().object(a)->box().right(), 990, "Jusqu'au bord droit : 1000 - 10 (marge gardee)");
        check(b.stack.done().size() == done + 1, "une commande");
        check(b.editor->statusBar().message().find("\xC3\x89tirement de Rect_A jusqu'au bord droit de la vue : largeur 200 \xE2\x86\x92 890 px") == 0,
              "la barre d'etat dit ce qui a change (" + b.editor->statusBar().message() + ")");
        (void)b.stack.undo();
        b.editor->refresh();
        near(b.v().object(a)->box().right(), 300, "Ctrl+Z : rendu");
    }
    // ---- deux objets : un seul Ctrl+Z ; Garder les marges decochee ----
    {
        cv.setSelection({a, c});
        rightClick(150, 140);
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 900);   // Garder les marges : decochee
        menu->close();
        check(!b.editor->stretchKeepMargins(), "Garder les marges : decochee");
        const std::size_t done = b.stack.done().size();
        rightClick(150, 140);
        const auto* sb = findItem(menu->items(), "Le bord de la s\xC3\xA9lection");
        check(sb && sb->enabled, "deux objets : le bord de la selection se fait");
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 4);   // Toute la largeur
        menu->close();
        near(b.v().object(a)->box().x, 0, "toute la largeur, sans marge : A de 0...");
        near(b.v().object(a)->box().right(), 1000, "... a 1000");
        near(b.v().object(c)->box().right(), 1000, "C aussi");
        check(b.stack.done().size() == done + 1, "deux objets etires : UNE commande");
        (void)b.stack.undo();
        b.editor->refresh();
        near(b.v().object(a)->box().right(), 300, "un seul Ctrl+Z rend A...");
        near(b.v().object(c)->box().right(), 400, "... et C");
        // Le bord de la selection > Droite : A va jusqu'a 400 (le bord droit de C).
        rightClick(150, 140);
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 21);
        menu->close();
        near(b.v().object(a)->box().right(), 400, "le bord de la selection > Droite : A jusqu'au bord de C");
        (void)b.stack.undo();
        b.editor->refresh();
        b.editor->setStretchKeepMargins(true);
    }
    // ---- Ctrl+Alt+Maj+fleche : jusqu'au bord de la vue ----
    {
        cv.setSelection({a});
        b.down(b.screen(800, 500));
        b.up(b.screen(800, 500));
        cv.setSelection({a});
        cv.takeFocus();
        ui::KeyMods cas;
        cas.ctrl = true;
        cas.alt = true;
        cas.shift = true;
        b.key(ui::Key::Down, cas);
        near(b.v().object(a)->box().bottom(), 590, "Ctrl+Alt+Maj+bas : jusqu'en bas (600 - 10)");
        near(b.v().object(a)->box().y, 100, "... le haut ne bouge pas (pas un deplacement)");
        (void)b.stack.undo();
        b.editor->refresh();
        ui::KeyMods shift;
        shift.shift = true;
        b.key(ui::Key::Right, shift);
        near(b.v().object(a)->box().x, 110, "Maj+fleche : toujours un pas de grille");
        near(b.v().object(a)->box().w, 200, "... sans etirer");
        (void)b.stack.undo();
        b.editor->refresh();
    }
    // ---- un objet verrouille (l'Explorateur le vise) : Etirer grise, la raison ----
    {
        b.v().object(c)->locked = true;
        cv.setSelection({c});
        b.editor->openCanvasMenu(b.screen(150, 340), true);   // l'Explorateur d'objets : la selection
        const auto* e2 = findItem(menu->items(), "\xC3\x89tirer");
        check(menu->isOpen() && e2 && !e2->enabled && e2->disabledReason == "verrouill\xC3\xA9", "l'Explorateur : Etirer grise sur un objet verrouille");
        menu->close();
        b.v().object(c)->locked = false;
        cv.setSelection({a});
        b.editor->openCanvasMenu(b.screen(150, 140), true);
        const auto ex = menu->items();
        check(indexOf(ex, "\xC3\x89tirer") == indexOf(ex, "Aligner") + 1, "l'Explorateur d'objets : le meme menu (Etirer apres Aligner)");
        menu->close();
    }
    // ---- la barre d'outils : Etirer v ----
    {
        cv.setSelection({a});
        check(b.editor->tools().isEnabled(HmiEditor::ActStretch) && b.editor->tools().labelOf(HmiEditor::ActStretch) == "\xC3\x89tirer \xE2\x96\xBE",
              "la barre d'outils : Etirer v (une selection)");
        b.editor->run(HmiEditor::ActStretch);
        check(menu->isOpen() && !menu->items().empty() && menu->items().front().label == "\xC3\x89tirer"
                  && findItem(menu->items(), "Toute la hauteur de la vue") != nullptr,
              "Etirer v ouvre le meme menu sous le bouton");
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 5);   // Toute la hauteur
        menu->close();
        near(b.v().object(a)->box().y, 10, "toute la hauteur (barre d'outils) : de 10...");
        near(b.v().object(a)->box().bottom(), 590, "... a 590");
        (void)b.stack.undo();
        b.editor->refresh();
        cv.setSelection({});
        check(!b.editor->tools().isEnabled(HmiEditor::ActStretch), "rien de choisi : Etirer v grise");
    }
    // ---- la fenetre Etirer... ----
    {
        cv.setSelection({a});
        menu::MenuManager mm{menu::MenuFactory{}};
        menu::MenuManager::ScopedInstance scope(mm);
        rightClick(150, 140);
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 901);
        menu->close();
        mm.applyPending();
        auto* d = dynamic_cast<app::HmiStretchDialog*>(mm.top());
        check(d != nullptr, "Etirer... ouvre la fenetre");
        if (d) {
            const auto before = b.v();
            check(d->edges() == st::kRight && d->target() == st::To::View && std::fabs(d->margin() - 10) < 1e-9,
                  "la fenetre : le bord droit, la vue, la marge de la vue (10) d'office");
            same_text(d->sizeLine(), "x 100 \xC2\xB7 largeur 200 \xE2\x86\x92 890 \xC2\xB7 hauteur 80 (ne change pas)", "la taille obtenue");
            check(cv.stretchPreview().has_value(), "l'apercu suit la fenetre");
            d->setEdge(st::kBottom, true);
            d->setMargin(20);
            check(cv.stretchPreview() && std::fabs(cv.stretchPreview()->changes[0].after.bottom() - 580) < 0.5, "l'apercu suit chaque changement (bas, marge 20)");
            Recorder rec2;
            const auto theme2 = ui::Theme::dark();
            b.editor->render(ui::PaintContext{rec2, theme2, {0, 0, 2000, 1000}, 0.0, nullptr});
            check(cv.stretchPreview().has_value(), "un dessin ne retire pas l'apercu de la fenetre");
            check(b.v() == before, "la fenetre ouverte : rien ne change");
            d->setTarget(st::To::Selection);
            check(!d->canConfirm() && d->sizeLine() == "Rien \xC3\xA0 \xC3\xA9tirer : deux objets ou plus", "la selection d'un seul objet : le bouton grise, pourquoi");
            d->setTarget(st::To::View);
            check(d->confirm(), "Etirer");
            mm.applyPending();
            boxIs(b.v(), a, {100, 100, 880, 480}, "la fenetre : bords droit et bas, marge 20");
            check(!cv.stretchPreview().has_value(), "l'apercu s'efface");
            (void)b.stack.undo();
            b.editor->refresh();
            // Annuler : rien.
            rightClick(150, 140);
            menu->itemChosen->emit(HmiEditor::kStretchMenu + 901);
            menu->close();
            mm.applyPending();
            if (auto* again = dynamic_cast<app::HmiStretchDialog*>(mm.top())) again->cancel();
            mm.applyPending();
            check(!cv.stretchPreview().has_value() && std::fabs(b.v().object(a)->box().right() - 300) < 0.5, "Annuler : rien ne change, l'apercu s'efface");
        }
        st::Request r;
        r.edges = st::kTop;
        same_text(app::HmiStretchDialog::encode(r), "4;0;-1;-1;1", "la reponse de la fenetre (encode)");
        check(app::HmiStretchDialog::parse("12;1;8;-1;0") == st::Request{12, st::To::Neighbour, -1, false, 8}, "... relue (parse)");
    }
}

// =============================================================================================================
//  1.50.1 : les limites de la 1.50.0 levees - la pastille violette << nouveau >> (un menu : generique ; la barre :
//  << Etirer v NV >>), les MARGES DE LA VUE (une propriete, enregistree, lue d'un fichier d'avant), Garder les
//  marges qui les prend d'abord (puis la grille de colonnes, puis le pas) et le dit, et un reglage DU PROJET.
// =============================================================================================================
#include "../src/ui/ThemeFile.hpp"   // 1.50.1 : contrastRatio (la pastille se lit)

namespace etirer1501ns {

// Un enregistreur qui garde aussi les couleurs (le violet de la pastille) et les traits (les tirets des marges).
struct InkRec final : gfx::IRenderer {
    struct Text { std::string s; gfx::Color c; gfx::Point at; };
    struct Fill { gfx::Rect r; gfx::Color c; };
    struct Line { gfx::Point a, b; gfx::Color c; };
    std::vector<Text> texts;
    std::vector<Fill> fills;
    std::vector<Line> lines;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect& r, gfx::Color c) override { fills.push_back({r, c}); }
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect& r, gfx::Color c, float) override { fills.push_back({r, c}); }
    void line(gfx::Point a, gfx::Point b, gfx::Color c, float) override { lines.push_back({a, b, c}); }
    void drawText(gfx::Point at, std::string_view t, gfx::FontId, gfx::Color c) override { texts.push_back({std::string(t), c, at}); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId) const override { return {8.f * static_cast<float>(t.size()), 16.f, 12.f, 4.f}; }
    float lineHeight(gfx::FontId) const override { return 16.f; }
    gfx::Size surfaceSize() const override { return {4000.f, 1000.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId, float maxWidth) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, maxWidth) / 8.f));
    }
    const Text* text(std::string_view s) const {
        for (const auto& t : texts) if (t.s == s) return &t;
        return nullptr;
    }
};

bool sameColor(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

const ui::PropertyGrid::Property* prop(const std::vector<ui::PropertyGrid::Category>& cats, std::string_view cat, std::string_view name) {
    for (const auto& c : cats)
        if (c.name == cat)
            for (const auto& p : c.properties)
                if (p.name == name) return &p;
    return nullptr;
}

std::string withoutMargins(std::string text) {
    for (const char* key : {" marge_haut=", " marge_bas=", " marge_gauche=", " marge_droite="})
        if (const auto at = text.find(key); at != std::string::npos) {
            const auto end = text.find_first_of(" \n", at + 1);
            text.erase(at, end - at);
        }
    return text;
}

} // namespace etirer1501ns

// ---- la pastille : generique (ui), lisible dans chaque theme --------------------------------------------------
void etirer1501Pastille() {
    std::printf("== 1.50.1 : la pastille << nouveau >> (menus, barre d'outils) ==\n");
    using etirer1501ns::InkRec;
    same_text(ui::tagPillText("nouveau"), "NOUVEAU", "la pastille : en capitales");
    same_text(ui::tagPillText("nv"), "NV", "... << nv >> -> NV");
    same_text(ui::tagPillText("\xC3\xA9t\xC3\xA9"), "\xC3\x89T\xC3\x89", "... les accents aussi (ete -> ETE)");
    // Lisible dans chaque theme integre : l'encre sur son fond, 4,5:1 (7:1 en contraste eleve) ; violette.
    std::string weak;
    for (const auto& info : ui::Theme::catalog()) {
        const auto t = ui::Theme::byName(info.key);
        const auto bc = ui::tagPillColors(t, ui::Tone::New);
        const double need = t.isHighContrast() ? 7.0 : 4.5;
        if (ui::contrastRatio(bc.ink, bc.fill) + 1e-9 < need) weak += std::string(weak.empty() ? "" : ", ") + info.key;
    }
    check(weak.empty(), "la pastille se lit dans les " + std::to_string(ui::Theme::catalog().size()) + " themes integres"
                            + (weak.empty() ? std::string{} : " (pas : " + weak + ")"));
    {
        const auto light = ui::Theme::light(), dark = ui::Theme::dark();
        const auto bl = ui::tagPillColors(light), bd = ui::tagPillColors(dark);
        check(bl.ink.b > bl.ink.g + 60 && bl.ink.r > bl.ink.g && bd.ink.b > bd.ink.g + 60 && bd.ink.r > bd.ink.g,
              "violette en clair et en sombre (le --new de la maquette)");
        check(etirer1501ns::sameColor(bl.ink, gfx::Color::rgb(0x7C3AED)), "en clair : exactement le violet de la maquette (#7C3AED)");
        check(bl.fill.r > 220 && bd.fill.r < 120, "le fond : pale en clair, sombre en sombre (la teinte sur le fond des panneaux)");
    }
    // Un menu quelconque : la pastille apres le libelle, la colonne s'elargit pour elle, le lisere a gauche.
    {
        ui::PopupMenu plain("m1"), badged("m2");
        std::vector<ui::PopupMenu::Item> a(1), bItems(1);
        a[0].label = "Etirer";
        a[0].id = 1;
        bItems[0] = a[0];
        bItems[0].badge = "nouveau";
        plain.setItems(a);
        badged.setItems(bItems);
        plain.openAt({10, 10}, {2000, 1000});
        badged.openAt({10, 10}, {2000, 1000});
        check(badged.popupRect().w >= plain.popupRect().w + ui::tagPillWidth("nouveau") + 7.f,
              "le menu s'elargit pour la pastille (" + std::to_string(plain.popupRect().w) + " -> " + std::to_string(badged.popupRect().w) + ")");
        InkRec rec;
        const auto theme = ui::Theme::light();
        badged.setBounds({0, 0, 2000, 1000});
        {
            std::vector<ui::Widget*> overlays;
            const ui::PaintContext ctx{rec, theme, {0, 0, 2000, 1000}, 0.0, &overlays};
            badged.render(ctx);
            for (auto* w : overlays) w->paintTopMost(ctx);
        }
        const auto* label = rec.text("Etirer");
        const auto* pill = rec.text("NOUVEAU");
        check(label && pill && pill->at.x > label->at.x + 40.f, "la pastille NOUVEAU est dessinee apres le libelle");
        check(pill && etirer1501ns::sameColor(pill->c, ui::tagPillColors(theme).ink), "... dans le violet du theme");
        bool fill = false, bar = false;
        for (const auto& f : rec.fills) {
            if (etirer1501ns::sameColor(f.c, ui::tagPillColors(theme).fill) && pill && f.r.contains({pill->at.x + 2.f, pill->at.y + 2.f})) fill = true;
            if (etirer1501ns::sameColor(f.c, ui::tagPillColors(theme).ink) && f.r.w <= 4.f && f.r.h > 10.f) bar = true;
        }
        check(fill && bar, "... sur son cartouche pale, et le lisere violet a gauche de l'entree");
    }
}

// ---- les marges de la vue : le modele, l'enregistrement, un fichier d'avant -------------------------------------
void etirer1501Marges() {
    std::printf("== 1.50.1 : les marges de la vue (propriete, fichier, defaut) ==\n");
    namespace st = hmi::stretch;
    Project p;
    View v = makeView(p, "Vue_Marges");
    v.width = 1024;
    v.height = 600;
    v.grid.step = 8;
    check(!v.margins.any() && v.margins == hmi::ViewMargins{}, "une vue (makeView, un projet d'avant) : pas de marges a elle (0)");
    const std::string before = serializeView(v);
    check(before.find("marge_") == std::string::npos, "sans marges : rien d'ecrit (un projet d'avant s'ecrit a l'identique)");
    // ---- d'ou vient la marge gardee : la vue, sinon la grille de colonnes, sinon le pas ----
    check(st::viewMargins(v).from == st::Margins::From::Snap && st::marginsText(st::viewMargins(v)) == "8 px",
          "sans marges ni colonnes : le pas d'aimantation (8 px), comme en 1.50.0");
    v.columns.visible = true;
    v.columns.margin = 40;
    check(st::viewMargins(v).from == st::Margins::From::Columns && std::fabs(st::viewMargin(v) - 40) < 1e-9,
          "la grille de colonnes montree : sa marge (40), comme en 1.50.0");
    v.margins = {20, 30, 12, 24};   // haut, bas, gauche, droite
    const auto m = st::viewMargins(v);
    check(m.from == st::Margins::From::View && m.top == 20 && m.bottom == 30 && m.left == 12 && m.right == 24 && !m.uniform(),
          "les marges de la vue d'abord (avant la grille de colonnes)");
    same_text(st::marginsText(m), "haut 20 \xC2\xB7 bas 30 \xC2\xB7 gauche 12 \xC2\xB7 droite 24 px", "differentes : chacune dite");
    same_text(st::marginsSource(m), "les marges de la vue", "d'ou elles viennent");
    near(st::viewMargin(v), 30, "une seule valeur : la plus grande (30)");
    // ---- l'enregistrement, la relecture ----
    const std::string text = serializeView(v);
    check(text.find(" marge_haut=20 marge_bas=30 marge_gauche=12 marge_droite=24") != std::string::npos,
          "ecrites sur la ligne vue (marge_haut, marge_bas, marge_gauche, marge_droite)");
    {
        auto back = parseView(text);
        check(back && back.value().margins == v.margins, "relues telles quelles");
        auto old = parseView(etirer1501ns::withoutMargins(text));
        check(old && !old.value().margins.any(), "un fichier d'avant (sans ces champs) : relu sans marges (0)");
        if (old) check(st::viewMargins(old.value()).from == st::Margins::From::Columns, "... Etirer y garde la grille de colonnes, comme avant");
        LoadReport rep;
        (void)parseView(text, &rep);
        check(rep.warnings.empty(), "aucun avertissement a la lecture");
        // Une IHM Studio d'avant : des champs inconnus sur la ligne vue sont ignores sans rien dire (la vue se relit).
        const auto eol = text.find('\n', text.find("\nvue ") + 1);
        auto future = parseView(text.substr(0, eol) + " marge_autre=3" + text.substr(eol));
        check(static_cast<bool>(future), "un champ inconnu sur la ligne vue ne bloque rien (ce qu'une version d'avant fait des marges)");
    }
    // Le projet entier, enregistre et relu.
    {
        Project q;
        View w = makeView(q, "Vue_A");
        w.margins = {16, 16, 16, 16};
        q.views.push_back(w);
        const auto files = serializeProject(q);
        const FileReader reader = [&](const std::string& path, std::string& content) {
            for (const auto& f : files)
                if (f.path == path) {
                    content.assign(f.data->begin(), f.data->end());
                    return true;
                }
            return false;
        };
        auto back = parseProject(reader);
        check(back && !back.value().views.empty() && back.value().views.front().margins == w.margins,
              "le projet enregistre puis rouvert : les marges de sa vue (16 x 4)");
    }
    // Une vue neuve (Nouvelle vue, Nouvelle popup) : 16 px de chaque cote, comme la maquette ; un symbole : 0.
    {
        auto doc = std::make_shared<Document>();
        core::CommandStack stack;
        Id made = kNoId, popup = kNoId, symbol = kNoId;
        (void)stack.push(app::hmiNewViewCommand(doc, "Vue_Neuve", 1024, 600, {}, &made, "vue"));
        (void)stack.push(app::hmiNewViewCommand(doc, "Popup_Neuve", 400, 300, {}, &popup, "popup"));
        (void)stack.push(app::hmiNewViewCommand(doc, "Symbole_Neuf", 200, 100, {}, &symbol, "symbole"));
        const auto* nv = doc->project.view(made);
        check(nv && nv->margins == hmi::ViewMargins{16, 16, 16, 16}, "Nouvelle vue : 16 px de chaque cote (la maquette)");
        check(doc->project.view(popup) && doc->project.view(popup)->margins.left == 16, "Nouvelle popup : 16 px aussi");
        check(doc->project.view(symbol) && !doc->project.view(symbol)->margins.any(), "un symbole : pas de marges");
        if (nv) same_text(st::marginsText(st::viewMargins(*nv)), "16 px", "egales : << 16 px >>");
    }
    // ---- Etirer avec les marges de la vue (le moteur) ----
    {
        View w = v;   // 1024 x 600 ; marges haut 20, bas 30, gauche 12, droite 24
        const Id c = edit::add(p, w, Kind::Rectangle, 200, 100);
        w.object(c)->name = "Courbe_1";
        w.object(c)->setBox({200, 100, 412, 260});
        st::Request r;
        auto plan = st::plan(w, {c}, r);
        check(plan.possible(), "jusqu'au bord droit : possible");
        if (plan.possible()) near(plan.changes[0].after.right(), 1000, "le bord droit a 1024 - 24 (la marge de droite de la vue)");
        near(plan.margin, 24, "la marge appliquee : 24");
        check(st::sentence(w, r, plan).find("(marge 24 px gard\xC3\xA9" "e)") != std::string::npos, "la barre d'etat : marge 24 px gardee");
        r.edges = st::kAll;
        plan = st::plan(w, {c}, r);
        View g = w;
        st::apply(g, plan);
        etirer1500ns::boxIs(g, c, {12, 20, 988, 550}, "toute la vue : chaque bord sa marge (12, 20, 1024-24, 600-30)");
        check(st::sentence(w, r, plan).find("(marges gard\xC3\xA9" "es : haut 20, bas 30, gauche 12, droite 24 px)") != std::string::npos,
              "differentes : la phrase les dit (" + st::sentence(w, r, plan) + ")");
        r.keepMargins = false;
        g = w;
        st::apply(g, st::plan(w, {c}, r));
        etirer1500ns::boxIs(g, c, {0, 0, 1024, 600}, "decochee : l'objet touche les bords (les marges de la vue ignorees)");
        st::Request given;
        given.margin = 5;   // la fenetre : une marge donnee, a chaque bord
        g = w;
        st::apply(g, st::plan(w, {c}, given));
        near(g.object(c)->box().right(), 1019, "une marge donnee (la fenetre) passe avant celles de la vue (1024 - 5)");
        st::Request neighbour;
        neighbour.to = st::To::Neighbour;
        (void)edit::add(p, w, Kind::Rectangle, 700, 150);
        const auto pn = st::plan(w, {c}, neighbour);
        near(pn.margin, 8, "le voisin : toujours l'espacement du pas d'aimantation (8), pas une marge de la vue");
    }
}

// ---- l'editeur : la pastille du menu et de la barre, la propriete, Etirer jusqu'a la marge, le reglage du projet --
void etirer1501Editeur() {
    std::printf("== 1.50.1 : Etirer dans l'editeur (pastille, marges de la vue, reglage du projet) ==\n");
    using app::HmiEditor;
    using etirer1501ns::InkRec;
    Bench b;
    b.v().grid.snapGrid = false;
    const Id a = edit::add(b.doc->project, b.v(), Kind::Rectangle, 100, 100);
    b.v().object(a)->name = "Rect_A";
    b.v().object(a)->setBox({100, 100, 200, 80});
    b.v().margins = {16, 16, 16, 16};
    b.editor->refresh();
    auto& cv = b.editor->canvas();
    auto* menu = b.editor->canvasMenu();
    if (!menu) { check(false, "l'editeur a son menu"); return; }
    const auto rightClick = [&](double x, double y) {
        cv.dispatch(ui::MouseDown{b.screen(x, y), ui::MouseButton::Right, 1, {}});
        cv.dispatch(ui::MouseUp{b.screen(x, y), ui::MouseButton::Right, {}});
    };
    // ---- le clic droit : Etirer porte la pastille violette (plus la note grise) ----
    cv.setSelection({a});
    rightClick(150, 140);
    const auto* eti = findItem(menu->items(), "\xC3\x89tirer");
    check(eti && eti->badge == "nouveau" && eti->badgeTone == ui::Tone::New && eti->note.empty(),
          "Etirer : la pastille << nouveau >> (violette), plus la note grise");
    const auto* keep = eti ? findItem(eti->children, "Garder les marges (16 px)") : nullptr;
    check(keep && keep->checked, "Garder les marges (16 px) : la valeur gardee, celle de la vue");
    {
        InkRec rec;
        const auto theme = ui::Theme::dark();
        b.editor->layout();
        {
            std::vector<ui::Widget*> overlays;
            const ui::PaintContext ctx{rec, theme, {0, 0, 2000, 1000}, 0.0, &overlays};
            b.editor->render(ctx);
            for (auto* w : overlays) w->paintTopMost(ctx);
        }
        const auto* pill = rec.text("NOUVEAU");
        check(pill && etirer1501ns::sameColor(pill->c, ui::tagPillColors(theme).ink), "le menu ouvert dessine NOUVEAU, en violet (theme sombre)");
        // Les marges de la vue : en tirets dans l'editeur (le haut de la vue, a 16).
        const float y16 = b.screen(0, 16).y;
        bool dashed = false;
        for (const auto& l : rec.lines)
            if (etirer1501ns::sameColor(l.c, gfx::Color::rgb(0x0D9488)) && std::fabs(l.a.y - y16) < 0.6f && std::fabs(l.b.y - y16) < 0.6f) dashed = true;
        check(dashed, "les marges de la vue : un trait en tirets dans l'editeur");
    }
    // ---- Jusqu'au bord droit : jusqu'a la marge de la vue ----
    menu->itemChosen->emit(HmiEditor::kStretchMenu + 1);
    menu->close();
    near(b.v().object(a)->box().right(), 984, "Jusqu'au bord droit : 1000 - 16 (la marge de la vue, pas le pas de 10)");
    check(b.editor->statusBar().message().find("(marge 16 px gard\xC3\xA9" "e)") != std::string::npos, "la barre d'etat le dit");
    (void)b.stack.undo();
    b.editor->refresh();
    // Des marges differentes : le menu les dit, chaque bord la sienne.
    b.v().margins.right = 40;
    b.editor->refresh();
    rightClick(150, 140);
    check(findItem(menu->items(), "Garder les marges (haut 16 \xC2\xB7 bas 16 \xC2\xB7 gauche 16 \xC2\xB7 droite 40 px)") != nullptr,
          "des marges differentes : le menu dit chacune");
    menu->itemChosen->emit(HmiEditor::kStretchMenu + 4);   // toute la largeur
    menu->close();
    etirer1500ns::boxIs(b.v(), a, {16, 100, 944, 80}, "toute la largeur : de 16 a 1000 - 40");
    (void)b.stack.undo();
    b.editor->refresh();
    // La fenetre Etirer... : des marges differentes - << celles de la vue >> (le champ vide), chaque bord la sienne.
    {
        cv.setSelection({a});
        auto d = b.editor->makeStretchDialog();
        check(d && d->margin() < 0, "la fenetre : << celles de la vue >> (des marges differentes)");
        if (d) same_text(d->sizeLine(), "x 100 \xC2\xB7 largeur 200 \xE2\x86\x92 860 \xC2\xB7 hauteur 80 (ne change pas)", "... sa taille : jusqu'a 1000 - 40");
        if (d) check(app::HmiStretchDialog::parse(app::HmiStretchDialog::encode(d->request())).margin < 0, "... la reponse garde << celles de la vue >>");
        d.reset();
        b.editor->stretchDialogDone(false, {});
    }
    b.v().margins.right = 16;
    b.editor->refresh();
    // Ctrl+Alt+Maj+fleche : la marge de la vue aussi.
    {
        cv.setSelection({a});
        cv.takeFocus();
        ui::KeyMods cas;
        cas.ctrl = cas.alt = cas.shift = true;
        b.key(ui::Key::Down, cas);
        near(b.v().object(a)->box().bottom(), 584, "Ctrl+Alt+Maj+bas : 600 - 16");
        (void)b.stack.undo();
        b.editor->refresh();
    }
    // ---- la propriete de la vue : Marges de la vue (En haut, En bas, A gauche, A droite), un Ctrl+Z ----
    {
        cv.setSelection({});
        b.editor->layout();
        const auto cats = b.editor->properties().categories();
        const auto* top = etirer1501ns::prop(cats, "Marges de la vue", "En haut");
        const auto* right = etirer1501ns::prop(cats, "Marges de la vue", "\xC3\x80 droite");
        check(top && right && top->value == "16" && right->value == "16" && etirer1501ns::prop(cats, "Marges de la vue", "En bas")
                  && etirer1501ns::prop(cats, "Marges de la vue", "\xC3\x80 gauche"),
              "les proprietes de la vue : la section Marges de la vue (En haut, En bas, A gauche, A droite)");
        const std::size_t done = b.stack.done().size();
        if (top) check(top->commit("24"), "En haut : 24");
        near(b.v().margins.top, 24, "la marge du haut devient 24");
        check(b.stack.done().size() == done + 1, "une modification de la vue (un Ctrl+Z)");
        if (right) check(!right->commit("-3"), "une marge negative : refusee");
        (void)b.stack.undo();
        b.editor->refresh();
        near(b.v().margins.top, 16, "Ctrl+Z : 16");
    }
    // ---- la barre d'outils : << Etirer v NV >> en violet ----
    {
        b.editor->setBounds({0, 0, 3600, 1000});
        b.editor->layout();
        cv.setSelection({a});
        auto& tools = b.editor->tools();
        same_text(tools.badgeOf(HmiEditor::ActStretch), "nv", "la barre : Etirer v porte la pastille nv");
        const auto br = tools.badgeRectOf(HmiEditor::ActStretch), bt = tools.rectOf(HmiEditor::ActStretch);
        check(br.w > 0 && br.x > bt.x + 40.f && br.right() <= bt.right() + 0.5f, "la pastille est dans le bouton, apres son libelle");
        InkRec rec;
        const auto theme = ui::Theme::light();
        b.editor->render(ui::PaintContext{rec, theme, {0, 0, 3600, 1000}, 0.0, nullptr});
        const auto* nv = rec.text("NV");
        const auto* label = rec.text("\xC3\x89tirer \xE2\x96\xBE");
        const auto ink = ui::tagPillColors(theme).ink;
        check(nv && etirer1501ns::sameColor(nv->c, ink), "NV dessine dans le violet du theme");
        check(label && etirer1501ns::sameColor(label->c, ink), "le libelle Etirer v aussi (la maquette)");
        b.editor->setBounds({0, 0, 2000, 1000});
        b.editor->layout();
    }
}

// ---- Garder les marges : un reglage DU PROJET, pas une modification (pas de Ctrl+Z) ; il survit a la reouverture --
void etirer1501Projet() {
    std::printf("== 1.50.1 : Garder les marges, un reglage du projet ==\n");
    using app::HmiEditor;
    std::map<std::string, std::string> store;
    std::string project = "Essai_A";
    ui::FilterMemory::install(ui::FilterMemory::Store{
        [&](const std::string& k) { const auto it = store.find(project + "." + k); return it == store.end() ? std::string{} : it->second; },
        [&](const std::string& k, const std::string& val) {
            if (val.empty()) store.erase(project + "." + k);
            else store[project + "." + k] = val;
        },
        {}});
    const auto setUp = [](Bench& b) {
        b.v().grid.snapGrid = false;
        const Id a = edit::add(b.doc->project, b.v(), Kind::Rectangle, 100, 100);
        b.v().object(a)->setBox({100, 100, 200, 80});
        b.editor->refresh();
        b.editor->canvas().setSelection({a});
        return a;
    };
    {
        Bench b;
        const Id a = setUp(b);
        check(b.editor->stretchKeepMargins(), "d'office : Garder les marges cochee");
        const std::size_t done = b.stack.done().size();
        auto* menu = b.editor->canvasMenu();
        b.editor->openCanvasMenu(b.screen(150, 140), true);
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 900);
        menu->close();
        check(!b.editor->stretchKeepMargins(), "decochee");
        same_text(store["Essai_A.hmi.etirer"], "sans-marges", "retenu pour le projet (avec ses reglages d'editeur)");
        check(b.stack.done().size() == done, "pas une modification du projet : rien dans Ctrl+Z");
        check(b.editor->statusBar().message().find("retenu pour ce projet") != std::string::npos, "la barre d'etat le dit");
        // Un autre editeur du meme projet (une autre vue ouverte) : le meme reglage.
        Bench other;
        check(!other.editor->stretchKeepMargins(), "un autre editeur du projet : decochee aussi");
        (void)a;
    }
    {
        // Le projet rouvert : un editeur neuf retrouve le reglage ; le menu le montre ; Etirer touche le bord.
        Bench b;
        const Id a = setUp(b);
        check(!b.editor->stretchKeepMargins(), "le projet rouvert : Garder les marges toujours decochee");
        auto* menu = b.editor->canvasMenu();
        b.editor->openCanvasMenu(b.screen(150, 140), true);
        const auto* keep = findItemStarting(menu->items(), "Garder les marges");
        check(keep && !keep->checked, "... le menu la montre decochee");
        menu->itemChosen->emit(HmiEditor::kStretchMenu + 1);
        menu->close();
        near(b.v().object(a)->box().right(), 1000, "... et l'objet touche le bord (sans marge)");
        (void)b.stack.undo();
    }
    {
        project = "Essai_B";   // un autre projet : son reglage a lui (d'office : cochee)
        Bench b;
        (void)setUp(b);
        check(b.editor->stretchKeepMargins(), "un autre projet : cochee (le reglage est par projet)");
    }
    {
        project = "Essai_A";
        Bench b;
        (void)setUp(b);
        b.editor->setStretchKeepMargins(true);
        check(store.count("Essai_A.hmi.etirer") == 0 && b.editor->stretchKeepMargins(), "recochee : plus rien de retenu (le defaut)");
    }
    ui::FilterMemory::install({});
    {
        Bench b;
        b.editor->setStretchKeepMargins(false);
        check(!b.editor->stretchKeepMargins(), "sans memoire (un essai) : le reglage reste a l'editeur");
    }
}

void etirer1501() {
    etirer1501Pastille();
    etirer1501Marges();
    etirer1501Editeur();
    etirer1501Projet();
}

void etirer1500() {
    etirer1500Moteur();
    etirer1500Editeur();
    etirer1501();   // 1.50.1
}
