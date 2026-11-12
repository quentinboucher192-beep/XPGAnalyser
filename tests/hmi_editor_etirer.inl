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
    check(stretchItem && stretchItem->enabled && !stretchItem->children.empty() && stretchItem->note == "nouveau" && stretchItem->paintIcon,
          "Etirer : un sous-menu, marque nouveau, son icone");
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

void etirer1500() {
    etirer1500Moteur();
    etirer1500Editeur();
}
