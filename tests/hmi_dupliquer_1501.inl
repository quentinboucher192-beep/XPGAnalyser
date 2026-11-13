// =============================================================================
//  tests/hmi_dupliquer_1501.inl - 1.50.1 : LES LIMITES DE « DUPLIQUER... » LEVEES (inclus par hmi_dupliquer_test.cpp)
// -----------------------------------------------------------------------------
//  - la liste groupee sur PLUSIEURS NIVEAUX : l'arbre des profondeurs (Armoires[.] > Departs[.] > Voies[.]),
//    Profondeur > Objet, Objet > Profondeur ; les comptes a chaque niveau (tout le sous-arbre), Tout deplier / Tout
//    replier a chaque niveau, deplier un groupe par son nom (ses parents aussi) ; toujours virtuelle (512 reperes) ;
//  - PLUSIEURS REGLES DANS UN GROUPE : chaque texte peut porter la sienne dans sa case Regle (sans etre « a part »),
//    celle du groupe pour les autres ; de meme une adresse, un repere a indices (sa plus grande profondeur) ;
//  - « Tableau (Excel) » : Echap annule la case ouverte (la valeur revient, la fenetre reste ; Echap encore la ferme,
//    comme les autres fenetres), Entree valide et descend, Ctrl+Entree ecrit la case puis duplique ; Echap dans la
//    regle a part (Formule...) l'abandonne au lieu de l'appliquer.
// =============================================================================
#include "../src/app/hmi/HmiAssist.hpp"   // le chemin qui existe : le meme controle que le Studio (assist::describe)
#include "../src/menu/MenuManager.hpp"

namespace {

// Les lignes d'une liste toute depliee : chaque groupe compte exactement les reperes montres sous lui.
bool countsHold(const std::vector<D::ListRow>& rows, std::string& bad) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].group || !rows[i].expanded) continue;
        std::size_t under = 0;
        for (std::size_t k = i + 1; k < rows.size() && rows[k].level > rows[i].level; ++k) under += rows[k].group ? 0 : 1;
        if (under != rows[i].count || rows[i].members.size() != rows[i].count) {
            bad = rows[i].text + " : " + std::to_string(rows[i].count) + " annonces, " + std::to_string(under) + " dessous";
            return false;
        }
    }
    return true;
}

int rowOf(const D& d, const std::string& text, bool group) {
    const auto& rows = d.listRows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].group == group && rows[i].text == text) return static_cast<int>(i);
    return -1;
}

std::size_t markerRows(const D& d) {
    std::size_t n = 0;
    for (const auto& r : d.listRows()) n += r.group ? 0 : 1;
    return n;
}

void niveaux() {
    std::printf("-- 1.50.1 : la liste sur plusieurs niveaux (l'arbre des profondeurs, les objets)\n");
    Big big;
    const auto d = open(big.b, big.sel, 31);
    d->setGrid({{0, 3}, {0, 7}});
    check(d->grouping() == D::Grouping::Depth, "par defaut : Profondeur");
    d->expandAll(true);
    const auto& rows = d->listRows();
    const std::string dot = "\xC2\xB7";
    const std::string armoires = "Armoires[" + dot + "]", departs = ".Departs[" + dot + "]", voies = ".Voies[" + dot + "]",
                      mesures = ".Mesures[" + dot + "]";
    const int a = rowOf(*d, armoires, true), b = rowOf(*d, departs, true), v = rowOf(*d, voies, true), m = rowOf(*d, mesures, true);
    check(a == 0 && b == 1 && v == 2 && m > v, "l'arbre : Armoires[.] > .Departs[.] > .Voies[.], puis .Mesures[.] (" + std::to_string(a) + ", "
              + std::to_string(b) + ", " + std::to_string(v) + ", " + std::to_string(m) + ")");
    if (a < 0 || b < 0 || v < 0 || m < 0) return;
    check(rows[static_cast<std::size_t>(a)].level == 0 && rows[static_cast<std::size_t>(b)].level == 1 && rows[static_cast<std::size_t>(v)].level == 2
              && rows[static_cast<std::size_t>(m)].level == 2 && rows[static_cast<std::size_t>(v) + 1].level == 3 && !rows[static_cast<std::size_t>(v) + 1].group,
          "les niveaux : 0, 1, 2 ; les reperes de Voies au niveau 3");
    check(rows[static_cast<std::size_t>(a)].count == 468 && rows[static_cast<std::size_t>(b)].count == 448 && rows[static_cast<std::size_t>(v)].count == 320
              && rows[static_cast<std::size_t>(m)].count == 128,
          "les comptes a chaque niveau : 468 > 448 > 320 et 128 (tout le sous-arbre, comme la colonne Reperes)");
    check(rowOf(*d, "Adresses %MW", true) >= 0 && rows[static_cast<std::size_t>(rowOf(*d, "Adresses %MW", true))].level == 0
              && rowOf(*d, "Textes", true) >= 0,
          "les adresses et les textes : en tete, a cote de l'arbre");
    check(markerRows(*d) == 512, "tout deplie : les 512 reperes (" + std::to_string(markerRows(*d)) + ")");
    std::string bad;
    check(countsHold(rows, bad), "chaque groupe compte les reperes sous lui " + bad);
    // Les 20 Armoires[0].Info : sous Armoires[.] directement, apres son sous-groupe .Departs[.].
    bool infoUnderArmoires = false;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].group && rows[i].text == "$Armoires[0].Info3$") infoUnderArmoires = rows[i].level == 1;
    check(infoUnderArmoires, "$Armoires[0].Info3$ : sous Armoires[.] (niveau 1), pas sous une profondeur plus grande");
    // ---- Tout replier : les tetes seules ; deplier un groupe par son nom (ses parents aussi) ----
    d->expandAll(false);
    check(d->listRows().size() == 3 && std::all_of(d->listRows().begin(), d->listRows().end(), [](const D::ListRow& r) { return r.group && r.level == 0 && !r.expanded; }),
          "Tout replier : les 3 tetes (Armoires[.], Adresses, Textes), repliees (" + std::to_string(d->listRows().size()) + " lignes)");
    check(d->listRows().front().count == 468 && d->listRows().front().members.size() == 468, "repliee, la tete compte toujours ses 468 reperes");
    check(d->setGroupExpanded("Departs", true), "deplier Departs");
    // Armoires (deplie : son parent), Departs (deplie), Voies, Mesures (replies), les 20 Info, Adresses, Textes.
    check(d->listRows().size() == 26 && d->listRows()[1].text == departs && d->listRows()[1].expanded && d->listRows()[2].text == voies
              && !d->listRows()[2].expanded && d->listRows()[2].count == 320,
          "Departs deplie, son parent aussi ; Voies et Mesures restent repliees, comptees (" + std::to_string(d->listRows().size()) + " lignes)");
    d->toggleGroup(d->listRows()[2].key);
    check(d->listRows().size() == 26 + 320 && d->listRows()[3].level == 3, "la fleche de Voies : ses 320 reperes");
    check(!d->setGroupExpanded("Inconnu", true), "un nom qui n'est pas un groupe : refuse");
    // ---- Profondeur > Objet ----
    d->setGrouping(D::Grouping::DepthObject);
    d->expandAll(true);
    check(markerRows(*d) == 512 && countsHold(d->listRows(), bad), "Profondeur > Objet : 512 reperes, chaque groupe compte les siens " + bad);
    {
        const int vv = rowOf(*d, voies, true);
        std::size_t objects = 0, objectRows = 0;
        if (vv >= 0)
            for (std::size_t i = static_cast<std::size_t>(vv) + 1; i < d->listRows().size() && d->listRows()[i].level > 2; ++i)
                if (d->listRows()[i].group) {
                    ++objects;
                    objectRows += d->listRows()[i].level == 3 && d->listRows()[i].count == 32 ? 1 : 0;
                }
        check(vv >= 0 && objects == 10 && objectRows == 10, "sous .Voies[.] : ses 10 objets de 32 reperes (niveau 3) - " + std::to_string(objects));
        const int dep = rowOf(*d, "Depart_0_0", true);
        check(dep == vv + 1 && d->listRows()[static_cast<std::size_t>(dep)].key != "O:Depart_0_0", "le premier : Depart_0_0, une cle a lui sous Voies");
        const int adr = rowOf(*d, "Adresses %MW", true);
        check(adr >= 0 && d->listRows()[static_cast<std::size_t>(adr) + 1].group && d->listRows()[static_cast<std::size_t>(adr) + 1].text == "Obj_14"
                  && d->listRows()[static_cast<std::size_t>(adr) + 1].count == 12,
              "sous Adresses : Obj_14 (12) puis Obj_15 (4)");
    }
    // ---- Objet > Profondeur ----
    d->setGrouping(D::Grouping::ObjectDepth);
    d->expandAll(true);
    std::size_t heads = 0;
    for (const auto& r : d->listRows()) heads += r.group && r.level == 0 ? 1 : 0;
    check(heads == 16 && markerRows(*d) == 512 && countsHold(d->listRows(), bad), "Objet > Profondeur : 16 objets en tete, 512 reperes " + bad);
    check(d->listRows().size() > 4 && d->listRows()[0].text == "Depart_0_0" && d->listRows()[1].text == armoires && d->listRows()[1].level == 1
              && d->listRows()[2].text == departs && d->listRows()[3].text == voies && d->listRows()[3].level == 3 && d->listRows()[3].count == 32,
          "Depart_0_0 > Armoires[.] > .Departs[.] > .Voies[.] (32)");
    check(d->setGroupExpanded("Voies", false) && markerRows(*d) == 512 - 320, "replier Voies (par son nom) : sous chacun des 10 objets");
    // ---- les filtres, la recherche : l'arbre suit ----
    d->setGrouping(D::Grouping::Depth);
    d->expandAll(true);
    d->setSearch("Info1");
    check(d->listRows().size() == 12 && d->listRows()[0].text == armoires && d->listRows()[0].count == 11 && rowOf(*d, departs, true) < 0,
          "chercher Info1 : Armoires[.] (11), sans .Departs[.] vide");
    d->setSearch({});
    d->setGrouping(D::Grouping::DepthObject);
    d->setListFilter(D::ListFilter::Addresses);
    check(markerRows(*d) == 16 && d->listRows()[0].text == "Adresses %MW" && d->listRows()[0].count == 16, "filtre Adresses : le groupe, ses objets, 16 reperes");
    d->setListFilter(D::ListFilter::All);
    // ---- a plat, par objet : comme avant ----
    d->setGrouping(D::Grouping::None);
    check(d->listRows().size() == 512 && std::none_of(d->listRows().begin(), d->listRows().end(), [](const D::ListRow& r) { return r.group || r.level != 0; }),
          "Rien : 512 lignes a plat");
    d->setGrouping(D::Grouping::Object);
    heads = 0;
    for (const auto& r : d->listRows()) heads += r.group ? 1 : 0;
    check(heads == 16, "Objet : 16 groupes (comme en 1.50.0)");
    // ---- les noms du choix ----
    check(D::groupingNamed("profondeur>objet") == D::Grouping::DepthObject && D::groupingNamed("Objet \xE2\x80\xBA Profondeur") == D::Grouping::ObjectDepth
              && D::groupingNamed("rien") == D::Grouping::None && !D::groupingNamed("autre"),
          "« Grouper par » se lit : profondeur>objet, le libelle, rien");
    // ---- la mesure : chaque groupement, deplie, 512 reperes ----
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto g : {D::Grouping::Depth, D::Grouping::DepthObject, D::Grouping::ObjectDepth, D::Grouping::Object}) {
        d->setGrouping(g);
        d->expandAll(false);
        d->expandAll(true);
    }
    const double ms = msSince(t0);
    std::printf("   mesure (Debug) : 4 groupements x (replier, deplier) %.0f ms\n", ms);
    check(ms < 1500.0, "4 groupements, replier et deplier : " + std::to_string(static_cast<int>(ms)) + " ms (borne : 1 500 en Debug)");
}

void reglesParTexte() {
    std::printf("-- 1.50.1 : plusieurs regles dans un groupe (chaque texte la sienne, dans la liste)\n");
    Big big;
    const auto d = open(big.b, big.sel, 31);
    d->setGrid({{0, 3}, {0, 7}});
    d->setGroupRule("Textes", {dup::RuleKind::Formula, "'D\xC3\xA9part {i+1}.{j+1}'"});
    d->setGroupRule("Adresses", {dup::RuleKind::Formula, "%MW(o + 10 * i + j)"});
    check(d->setMarkerRule("$Titre3$", "'Mesure {i+1}'") && d->setMarkerRule("Titre5", "'T{n:02}'"), "deux textes ont leur regle");
    check(d->cell(31, "$Titre3$") == "Mesure 4" && d->cell(31, "$Titre5$") == "T31" && d->cell(31, "$Titre4$") == "D\xC3\xA9part 4.8",
          "Titre3 : Mesure 4 ; Titre5 : T31 ; Titre4 suit le groupe : Depart 4.8 (" + d->cell(31, "$Titre3$") + ", " + d->cell(31, "$Titre5$") + ")");
    check(d->markerRules("Textes") == 2 && d->listCount(D::ListFilter::Own) == 0, "2 regles dans le groupe des textes ; aucun repere « a part »");
    d->expandAll(true);
    {
        const int t3 = rowOf(*d, "$Titre3$", false), t4 = rowOf(*d, "$Titre4$", false), tx = rowOf(*d, "Textes", true);
        check(t3 >= 0 && d->listRows()[static_cast<std::size_t>(t3)].rule == "'Mesure {i+1}'" && d->listRows()[static_cast<std::size_t>(t3)].rule2
                  && !d->listRows()[static_cast<std::size_t>(t3)].own && d->listRows()[static_cast<std::size_t>(t3)].level == 1,
              "la ligne de Titre3 : sa regle dans sa case, sans « a part », dans le groupe Textes");
        check(t4 >= 0 && !d->listRows()[static_cast<std::size_t>(t4)].rule2 && d->listRows()[static_cast<std::size_t>(t4)].rule.find("comme son groupe") != std::string::npos,
              "Titre4 : comme son groupe");
        check(tx >= 0 && d->listRows()[static_cast<std::size_t>(tx)].where.find("2 avec leur r\xC3\xA8gle") != std::string::npos,
              "la tete Textes : « 2 avec leur regle » (" + (tx >= 0 ? d->listRows()[static_cast<std::size_t>(tx)].where : std::string()) + ")");
    }
    // La regle du groupe change : ceux qui ont la leur la gardent.
    d->setGroupRule("Textes", {dup::RuleKind::Formula, "'Txt {n}'"});
    check(d->cell(31, "$Titre4$") == "Txt 31" && d->cell(31, "$Titre3$") == "Mesure 4", "le groupe : 'Txt {n}' - Titre4 le suit, Titre3 garde la sienne");
    check(d->setMarkerRule("$Titre3$", "'Txt {n}'") && d->markerRules("Textes") == 1 && d->cell(31, "$Titre3$") == "Txt 31",
          "la formule du groupe tapee : le repere suit de nouveau son groupe");
    check(d->setMarkerRule("$Titre5$", "") && d->markerRules("Textes") == 0, "vide : comme son groupe");
    // garder : le repere reste dans la copie, sans bloquer
    check(d->setMarkerRule("$Titre6$", "garder") && d->cell(31, "$Titre6$").empty() && d->canConfirm(), "« garder » : Titre6 garde son repere, rien ne bloque");
    d->setMarkerRule("$Titre6$", "");
    // une formule fausse bloque, et le dit
    d->setMarkerRule("$Titre7$", "'X{i'");
    check(!d->canConfirm() && d->blockingText().find("$Titre7$") != std::string::npos, "une regle fausse (accolade) : bloque, la note nomme $Titre7$");
    d->setMarkerRule("$Titre7$", "");
    check(d->canConfirm(), "retiree : Dupliquer de nouveau permis (" + d->blockingText() + ")");
    // ---- les autres groupes : une adresse, un repere a indices ----
    check(d->setMarkerRule("$%MW1040$", "%MW(2000 + i)") && d->cell(31, "$%MW1040$") == "%MW2003" && d->cell(31, "$%MW1080$") == "%MW1117",
          "une adresse a sa regle : %MW(2000 + i) -> %MW2003 ; les autres suivent le groupe");
    check(d->setMarkerRule("$Armoires[0].Departs[0].Voies[5].S9$", "o + i") && d->cell(31, "$Armoires[0].Departs[0].Voies[5].S9$") == "Armoires[3].Departs[7].Voies[8].S9"
              && d->cell(31, "$Armoires[0].Departs[0].Voies[5].S8$") == "Armoires[3].Departs[7].Voies[5].S8",
          "un repere a indices : sa regle pour sa plus grande profondeur (Voies[5 + 3]) ; les autres Voies restent fixes");
    check(d->markerRules("Voies") == 1 && d->markerRules("Adresses") == 1 && d->listCount(D::ListFilter::Own) == 0, "comptes : Voies 1, Adresses 1 ; aucun a part");
    // ---- « a part » (Formule...) reste ce qu'il etait ----
    check(d->setOwnFormula("$Titre8$", "'AP {i}'"), "Titre8 a part");
    check(d->listCount(D::ListFilter::Own) == 1 && d->cell(31, "$Titre8$") == "AP 3", "a part : compte dans le filtre « A part », AP 3");
    d->expandAll(true);
    {
        const int t8 = rowOf(*d, "$Titre8$", false);
        check(t8 >= 0 && d->listRows()[static_cast<std::size_t>(t8)].own && d->listRows()[static_cast<std::size_t>(t8)].rule.rfind("\xC3\xA0 part : ", 0) == 0,
              "sa ligne : « a part : 'AP {i}' »");
    }
    check(d->setMarkerRule("$Titre8$", "\xC3\xA0 part : 'AP2 {i}'") && d->cell(31, "$Titre8$") == "AP2 3" && d->listCount(D::ListFilter::Own) == 0,
          "sa case reecrite (« a part : » ote) : sa regle dans la liste, plus « a part »");
    // ---- Dupliquer : chaque texte selon sa regle ----
    d->setMarkerRule("$Titre3$", "'Mesure {i+1}'");
    check(d->canConfirm(), "Dupliquer permis (" + d->blockingText() + ")");
    hmi::Project p = big.b.doc->project;
    (void)dup::apply(p, *p.view(big.b.view), d->plan());
    std::string seen3, seen4;
    for (const auto& o : p.view(big.b.view)->objects)
        for (const auto& pr : o.props) {
            if (pr.value.find("Mesure 4") != std::string::npos) seen3 = pr.value;
            if (pr.value.find("Txt 31") != std::string::npos) seen4 = pr.value;
        }
    check(!seen3.empty() && !seen4.empty(), "les copies : Mesure 4 (sa regle) et Txt 31 (le groupe) dans la derniere (" + seen3 + ", " + seen4 + ")");
}

void reglesDansLaListe(TextRecorder& r) {
    std::printf("-- 1.50.1 : la regle d'un repere s'ecrit dans sa case de la liste\n");
    Big big;
    const auto d = open(big.b, big.sel, 31);
    check(static_cast<bool>(d->Initialize()), "la fenetre se construit");
    d->setGrid({{0, 3}, {0, 7}});
    d->setGroupRule("Textes", {dup::RuleKind::Formula, "'D\xC3\xA9part {i+1}.{j+1}'"});
    d->expandAll(false);
    d->setGroupExpanded("Textes", true);
    ui::Widget* root = d->widgetRoot();
    paintInto(r, *root, {1280.f, 800.f});
    auto* list = d->markerList();
    const int t3 = rowOf(*d, "$Titre3$", false);
    check(list && t3 >= 0 && list->beginCellEdit(static_cast<ui::RowIndex>(t3), 2), "la case Regle de $Titre3$ s'ouvre");
    if (list && t3 >= 0) {
        list->finishCellEdit(true, "'Mesure {i+1}'");
        check(d->cell(31, "$Titre3$") == "Mesure 4" && d->markerRules("Textes") == 1, "tapee : 'Mesure {i+1}' - Mesure 4, une regle dans le groupe");
    }
    const int tx = rowOf(*d, "Textes", true);
    check(list && tx >= 0 && list->beginCellEdit(static_cast<ui::RowIndex>(tx), 2), "la case Regle du groupe Textes s'ouvre aussi");
    if (list && tx >= 0) {
        list->finishCellEdit(true, "varie : 'G{n}'");
        check(d->rules().rule(*d->findGroup("Textes")).formula == "'G{n}'" && d->cell(31, "$Titre4$") == "G31" && d->cell(31, "$Titre3$") == "Mesure 4",
              "le groupe : 'G{n}' (« varie : » ote) ; Titre3 garde la sienne");
    }
    const int t4 = rowOf(*d, "$Titre4$", false);
    check(list && t4 >= 0 && list->beginCellEdit(static_cast<ui::RowIndex>(t4), 2), "la case de Titre4");
    if (list && t4 >= 0) {
        list->finishCellEdit(false, "'Rien'");
        check(d->markerRules("Textes") == 1 && d->cell(31, "$Titre4$") == "G31", "Echap dans la case : rien ne change");
    }
    // A l'ecran : la liste depliee sur deux niveaux reste virtuelle et dans son cadre, a chaque groupement.
    for (const auto g : {D::Grouping::Depth, D::Grouping::DepthObject, D::Grouping::ObjectDepth, D::Grouping::Object, D::Grouping::None}) {
        d->setGrouping(g);
        d->expandAll(true);
        for (const gfx::Size size : {gfx::Size{1280.f, 800.f}, gfx::Size{900.f, 620.f}}) {
            ui::dialogframe::reset();
            paintInto(r, *root, size);
            const std::string where = D::groupingLabel(g) + ", " + std::to_string(static_cast<int>(size.w));
            std::size_t markerTexts = 0;
            for (const auto& t : r.texts) markerTexts += t.text.rfind("$Armoires[0]", 0) == 0 || t.text.rfind("$%MW", 0) == 0 || t.text.rfind("$Titre", 0) == 0 ? 1 : 0;
            check((markerTexts > 0 || size.w < 1000.f) && markerTexts < 80, where + " : la liste virtuelle (" + std::to_string(markerTexts) + " reperes dessines sur 512)");
            check(textsOutside(r, d->panelRect(), where) == 0, where + " : aucun texte hors de son cadre");
            check(buttonProblems(*root, d->panelRect(), where) == 0, where + " : les boutons dans le cadre");
            const auto* gb = d->groupingList();
            check(gb && gb->bounds().w > 0.f && gb->bounds().right() <= d->panelRect().right() + 0.5f
                      && ui::measureWidth(D::groupingLabel(g), gfx::FontId{16}) + 24.f <= gb->bounds().w,
                  where + " : « Grouper par » tient son libelle");
        }
    }
}

// ---- l'exemple des captures (dupliquer-exemple) ----
//  En 1.50.0, Armoires etait ARRAY[0..3] OF INT : « Armoires[0].Nom n'existe pas » (32 cases rouges), Dupliquer gris.
//  Les types IHM fictifs tiennent chaque chemin ; le controle des chemins est celui du Studio (assist::describe).
void exempleCaptures() {
    std::printf("-- 1.50.1 : l'exemple des captures (dupliquer-exemple) - des types IHM, Dupliquer permis, un seul avertissement\n");
    const auto make = [](bool oldTypes, std::vector<Id>& made) {
        auto doc = std::make_shared<hmi::Document>();
        hmi::View v = hmi::makeView(doc->project, "Vue_Accueil");
        v.width = 1920;
        v.height = 1080;
        const Id vid = v.id;
        doc->project.views.push_back(v);
        if (oldTypes) {   // la 1.50.0
            hmi::Variable var;
            var.id = doc->project.allocate();
            var.name = "Armoires";
            var.type = "ARRAY[0..3] OF INT";
            doc->project.programs.variables.push_back(var);
        }
        made = D::addExample(doc->project, *doc->project.view(vid));
        D::Spec spec;
        spec.doc = doc;
        spec.view = vid;
        spec.selection = made;
        spec.copies = 3;
        spec.exists = [doc](std::string_view path) { return app::assist::describe(doc->project, nullptr, path).found; };
        auto d = std::make_unique<D>(std::move(spec));
        // Les reglages du script des captures (fonctions-C.txt).
        d->setGrid({{0, 3}, {0, 7}});
        d->setGroupRule("Adresses", {dup::RuleKind::Formula, "(o + 10 * i + j)"});
        d->setGroupRule("Textes", {dup::RuleKind::Formula, "'D\xC3\xA9part {i+1}.{j+1}'"});
        d->setGroupRule("Noms des objets", {dup::RuleKind::Formula, "'Depart_{i}_{j}'"});
        return std::make_pair(doc, std::move(d));
    };
    {
        std::vector<Id> made;
        auto [doc, d] = make(true, made);
        check(!d->canConfirm() && d->blockingText().find("Armoires[0].Nom n'existe pas") != std::string::npos,
              "la 1.50.0 (Armoires : ARRAY OF INT) : Dupliquer gris, Armoires[0].Nom n'existe pas - ce que montrait la capture");
    }
    std::vector<Id> made;
    auto [doc, d] = make(false, made);
    check(made.size() == 7 && d->scanned().markers.size() == 17, "7 objets, 17 reperes (" + std::to_string(d->scanned().markers.size()) + ")");
    for (const auto* t : {"T_Armoire", "T_Depart", "T_Voie", "T_Mesure"})
        check(std::any_of(doc->project.programs.types.begin(), doc->project.programs.types.end(), [&](const hmi::HmiType& x) { return x.name == t; }),
              std::string("le type IHM ") + t);
    check(d->setMarkerRule("$L\xC3\xA9gende armoire$", "'Armoire {i+1}'") && d->setMarkerRule("$Code D00$", "'D{i}{j:02}'")
              && d->setMarkerRule("$%M200$", "%M(200 + 8 * i + j)") && d->setMarkerRule("$Armoires[0].Departs[0].Voies[5].Seuil$", "5 + 4 * i"),
          "les regles de chacun (le script) : deux textes, une adresse, le seuil");
    check(d->canConfirm(), "l'exemple : Dupliquer permis (" + d->blockingText() + ")");
    std::size_t faults = 0, warns = 0;
    bool counter = false;
    for (const auto& c : d->conflicts()) {
        (c.blocks ? faults : warns) += 1;
        counter = counter || c.text.find("%MW1037 d\xC3\xA9j\xC3\xA0 prise par Compteur") != std::string::npos;
    }
    check(faults == 0 && warns == 1 && counter, "un seul avertissement : %MW1037 deja prise par Compteur (" + std::to_string(warns) + " / " + std::to_string(faults) + ")");
    const auto* gd = d->findGroup("Departs");
    const auto* gv = d->findGroup("Voies");
    check(gd && gd->bounds && gd->bounds->first == 0 && gd->bounds->second == 7 && gv && gv->bounds && gv->bounds->second == 31,
          "les bornes des tableaux des types : Departs 0..7, Voies 0..31 (avant : inconnues)");
    check(d->cell(31, "$L\xC3\xA9gende armoire$") == "Armoire 4" && d->cell(31, "$Code D00$") == "D307" && d->cell(31, "$Titre 1.1$") == "D\xC3\xA9part 4.8"
              && d->cell(31, "$%M200$") == "%M231" && d->cell(31, "$Armoires[0].Departs[0].Voies[5].Seuil$") == "Armoires[3].Departs[7].Voies[17].Seuil",
          "la derniere copie : Armoire 4, D307, Depart 4.8, %M231, Voies[17]");
    // La premiere capture : Profondeur > Objet, tout replie, Voies et Afficheur_I5 deplies.
    d->setGrouping(D::Grouping::DepthObject);
    d->expandAll(false);
    check(d->setGroupExpanded("Voies", true) && d->setGroupExpanded("Afficheur_I5", true), "deplier Voies, puis Afficheur_I5");
    std::string seen;
    for (const auto& r : d->listRows()) seen += std::string(static_cast<std::size_t>(r.level) * 2, ' ') + r.text + (r.group ? " (" + std::to_string(r.count) + ")" : "") + "\n";
    const std::string dot = "\xC2\xB7";
    const int seuil = rowOf(*d, "$Armoires[0].Departs[0].Voies[5].Seuil$", false), i5 = rowOf(*d, "Afficheur_I5", true);
    check(d->listRows().size() == 13 && d->listRows()[0].text == "Armoires[" + dot + "]" && d->listRows()[0].count == 12 && d->listRows()[2].text == ".Voies[" + dot + "]"
              && i5 > 2 && d->listRows()[static_cast<std::size_t>(i5)].level == 3 && seuil > i5 && seuil <= i5 + 3
              && d->listRows()[static_cast<std::size_t>(seuil)].level == 4 && d->listRows()[static_cast<std::size_t>(seuil)].rule2,
          "la capture 1 : Armoires[.] (12) > .Departs[.] > .Voies[.] > Afficheur_I5 > le seuil et sa regle\n" + seen);
    // La seconde : Profondeur, les textes et les adresses deplies.
    d->setGrouping(D::Grouping::Depth);
    d->expandAll(false);
    check(d->setGroupExpanded("Textes", true) && d->setGroupExpanded("Adresses", true) && d->listRows().size() == 8, "la capture 2 : 8 lignes");
}

// ---- Echap dans « Tableau (Excel) » ----
struct Host {
    menu::MenuManager mm{menu::MenuFactory{}};
    int               closed{0};
    menu::DialogResult result{};
    bool              applied{false};
    dup::Plan         played;
    ui::Theme         theme = ui::Theme::xpg();
    D*                d{nullptr};
    D* show(Bench& b, const std::vector<Id>& sel, int copies) {
        D::Spec spec;
        spec.doc = b.doc;
        spec.view = b.view;
        spec.selection = sel;
        spec.copies = copies;
        spec.exists = [](std::string_view) { return true; };
        spec.apply = [this](const dup::Plan& p) {
            applied = true;
            played = p;
        };
        mm.ShowDialog(std::make_unique<D>(std::move(spec)), [this](const menu::DialogResult& r) {
            ++closed;
            result = r;
        });
        mm.applyPending();
        d = dynamic_cast<D*>(mm.top());
        frame();
        return d;
    }
    void frame() { mm.Update(menu::FrameContext{0.016, 1.0, &theme, {1280.f, 800.f}, 1.f}); }
    void key(ui::Key k, bool ctrl = false) {
        ui::KeyDown e{k, {}, false};
        e.mods.ctrl = ctrl;
        (void)mm.HandleEvent(e);
        mm.applyPending();
        if (mm.top() == d) frame();
    }
    void type(const std::string& t) { (void)mm.HandleEvent(ui::TextInput{t}); }
    bool click(const gfx::Rect& r) {
        if (r.w <= 0.f) return false;
        const gfx::Point at{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
        (void)mm.HandleEvent(ui::MouseDown{at, ui::MouseButton::Left, 1, {}});
        (void)mm.HandleEvent(ui::MouseUp{at, ui::MouseButton::Left, {}});
        frame();
        return true;
    }
};

void echapTableau(TextRecorder& r) {
    std::printf("-- 1.50.1 : Echap dans une case de << Tableau (Excel) >> (puis Entree, Ctrl+Entree, la regle a part)\n");
    Bench b;
    const Id a = b.object(Kind::Text, "Txt_1", 100, 100);
    b.v().object(a)->set("text", "$Etiquette$ / $Unite$");
    Host h;
    D* d = h.show(b, {a}, 3);
    check(d != nullptr, "la fenetre s'ouvre (un gestionnaire de fenetres)");
    if (!d) return;
    check(d->fillSeries("$Etiquette$", "E{n}", "1"), "la colonne $Etiquette$ : E1, E2, E3");
    d->showTab(2);
    h.frame();
    paintInto(r, *d->widgetRoot(), {1280.f, 800.f});   // les largeurs mesurees
    h.frame();
    const std::string before = d->cell(1, "$Etiquette$");
    const int col = 1;   // $Etiquette$ : la premiere colonne de reperes
    check(before == "E1" && h.click(d->sheetCell(1, col)), "un clic sur la case (ligne 2, $Etiquette$)");
    check(d->sheetEditing() && d->sheetEditor() && d->sheetEditor()->visible() && d->sheetEditor()->focused() && d->sheetEditor()->text() == "E1",
          "la case s'ouvre : son champ, le clavier, sa valeur (E1)");
    h.type("ZZZ");
    check(d->sheetEditor()->text() == "ZZZ" && d->cell(1, "$Etiquette$") == "E1", "tape : ZZZ (le tableau garde E1 tant que rien n'est valide)");
    h.key(ui::Key::Escape);
    check(!d->sheetEditing() && !d->sheetEditor()->visible() && !d->sheetEditor()->focused(), "Echap : la case se ferme, le champ rend le clavier");
    check(d->cell(1, "$Etiquette$") == "E1", "Echap : la valeur d'avant reste (E1, pas ZZZ) - avant, la perte du clavier ecrivait ZZZ");
    check(h.mm.top() == d && h.closed == 0, "Echap : la fenetre reste ouverte");
    if (h.mm.top() != d) return;   // fermee : `d` n'est plus
    // Entree : valide et descend.
    h.click(d->sheetCell(1, col));
    h.type("V9");
    h.key(ui::Key::Return);
    check(d->cell(1, "$Etiquette$") == "V9" && d->sheetEditing() && d->sheetEditor()->text() == "E2" && d->chosenRow() == 2,
          "Entree : V9 ecrit, la case de dessous s'ouvre (E2)");
    h.type("W");
    h.key(ui::Key::Escape);
    check(h.mm.top() == d, "Echap encore dans une case : la fenetre reste");
    if (h.mm.top() != d) return;
    check(!d->sheetEditing() && d->cell(2, "$Etiquette$") == "E2", "Echap encore dans une case : E2 rendu");
    // Echap sans case ouverte : la fenetre se ferme (Annuler), comme les autres fenetres.
    h.key(ui::Key::Escape);
    check(h.closed == 1 && h.result.button == menu::DialogResult::Button::Cancel && !h.applied && h.mm.top() == nullptr,
          "Echap une fois de plus (aucune case ouverte) : la fenetre se ferme, rien n'est duplique");
    // Ctrl+Entree pendant une case ouverte : la case s'ecrit, puis Dupliquer.
    Host h2;
    D* d2 = h2.show(b, {a}, 3);
    if (!d2) return;
    d2->fillSeries("$Etiquette$", "E{n}", "1");
    d2->fillSeries("$Unite$", "U{n}", "1");
    d2->showTab(2);
    h2.frame();
    paintInto(r, *d2->widgetRoot(), {1280.f, 800.f});
    h2.frame();
    h2.click(d2->sheetCell(3, 2));
    h2.type("kW");
    h2.key(ui::Key::Return, true);
    check(h2.applied && h2.closed == 1 && h2.played.rows.size() == 4 && h2.played.rows[3].markers.count(dup::markerKey("Unite"))
              && h2.played.rows[3].markers.at(dup::markerKey("Unite")) == "kW",
          "Ctrl+Entree dans une case : kW ecrit, puis Dupliquer (avant : la case tapee se perdait)");
    // La regle a part (Formule...) : Echap l'abandonne (avant : la perte du clavier l'appliquait).
    Host h3;
    D* d3 = h3.show(b, {a}, 3);
    if (!d3) return;
    paintInto(r, *d3->widgetRoot(), {1280.f, 800.f});
    h3.frame();
    const int row = rowOf(*d3, "$Unite$", false);
    check(row >= 0 && d3->markerList(), "la ligne de $Unite$");
    if (row < 0 || !d3->markerList()) return;
    d3->markerList()->selectModelRows({static_cast<ui::RowIndex>(row)}, true);
    d3->markerList()->contextAction->emit(1001);   // Formule...
    h3.frame();
    h3.type("'Q{n}'");
    h3.key(ui::Key::Escape);
    check(h3.mm.top() == d3, "Formule... puis Echap : la fenetre reste");
    if (h3.mm.top() != d3) return;
    check(d3->rules().own.empty(), "Formule... puis Echap : rien d'applique");
    d3->markerList()->contextAction->emit(1001);
    h3.frame();
    h3.type("'Q{n}'");
    h3.key(ui::Key::Return);
    check(d3->rules().own.count(dup::markerKey("Unite")) && d3->cell(2, "$Unite$") == "Q2", "Formule... puis Entree : appliquee (Q2)");
    h3.key(ui::Key::Escape);
    check(h3.closed == 1 && !h3.applied, "Echap ensuite : la fenetre se ferme");
}

} // namespace
