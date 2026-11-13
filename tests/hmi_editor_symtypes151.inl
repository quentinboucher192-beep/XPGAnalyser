// =============================================================================
//  tests/hmi_editor_symtypes151.inl - 1.51.0 : LES TYPES IHM D'UN SYMBOLE
// -----------------------------------------------------------------------------
//  Inclus par hmi_editor_test.cpp (HMI_TEST_SYMT=1 : seul). << Faire en sorte que les symboles puissent porter des
//  types IHM >> (hmi/HmiSymbolTypes.hpp) :
//    1. la creation dans le sous-onglet Types IHM du symbole (le volet des types, sur le symbole), un homonyme du
//       projet permis ; la liste du projet ne les montre pas ; l'arbre (Types IHM sous le symbole) ; Ctrl+Z ;
//    2. leur emploi dans le symbole par leur nom court (un parametre, une fonction, un script), la portee la plus
//       proche contre un homonyme du projet ; hors du symbole par leur nom complet (Symbole.T_Etat) - le moteur ;
//    3. renommer (le type, le symbole), supprimer (ses emplois d'abord), dupliquer, exporter et importer le symbole,
//       la corbeille, l'enregistrement relu, une version d'avant qui lit le fichier.
// =============================================================================
#include "../src/hmi/HmiSymbolTypes.hpp"
#include "../src/hmi/HmiTrash.hpp"
#include "../src/hmi/HmiDesign.hpp"
#include "../src/hmi/HmiScript.hpp"   // projectHeader

namespace symt151 {

hmi::Script calledScript(hmi::Project& p, const std::string& name, const std::string& body) {
    hmi::Script s;
    s.id = p.allocate();
    s.name = name;
    s.event = "Appel";
    s.body = body;
    return s;
}

hmi::Declaration declaration(hmi::Project& p, hmi::DeclKind kind, const std::string& name, const std::string& type, const std::string& value = {}) {
    hmi::Declaration d;
    d.id = p.allocate();
    d.kind = kind;
    d.name = name;
    d.type = type;
    d.value = value;
    return d;
}

hmi::Variable variable(hmi::Project& p, const std::string& name, const std::string& type, const std::string& initial = "0") {
    hmi::Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    return v;
}

const hmi::HmiType* ownedNamed(const hmi::Project& p, hmi::Id symbol, const std::string& name) {
    return hmi::symtypes::find(p, symbol, name);
}

bool anyIssue(const std::vector<hmi::Issue>& issues, const std::string& part) {
    return std::any_of(issues.begin(), issues.end(), [&](const hmi::Issue& i) {
        return i.severity == hmi::Issue::Severity::Error && i.message.find(part) != std::string::npos;
    });
}
std::string issueWith(const std::vector<hmi::Issue>& issues, const std::string& part) {
    for (const auto& i : issues)
        if (i.severity == hmi::Issue::Severity::Error && i.message.find(part) != std::string::npos) return i.property + " : " + i.message;
    return "(aucune)";
}

std::string readAll(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace symt151

void symtypes151() {
    std::printf("1.51.0 : les types IHM d'un symbole\n");
    using namespace symt151;
    namespace st = hmi::symtypes;
    auto doc = std::make_shared<Document>();
    core::CommandStack stack;
    core::CommandGroupScope::registerStack(&stack);
    auto apply = [&](core::CommandPtr c) { if (c) (void)stack.push(std::move(c)); };
    Project& p = doc->project;

    // ---- le projet d'essai : deux homonymes au projet (T_Etat, T_Mode), un symbole S_Pompe, une vue qui le pose ----
    {
        HmiType rootEtat;
        rootEtat.id = p.allocate();
        rootEtat.name = "T_Etat";
        rootEtat.members = {{"Code", "INT", "", ""}};
        p.programs.types.push_back(rootEtat);
        HmiType rootMode = hmi::makeEnumeration(p, "T_Mode");
        rootMode.values = {{"Manu", 0, "", ""}, {"Auto", 1, "", ""}};
        (void)hmi::regenerateEnumConversions(p, rootMode);
        p.programs.types.push_back(rootMode);
        p.programs.variables.push_back(variable(p, "Autre", "T_Etat"));
    }
    View sym = makeView(p, "S_Pompe");
    sym.role = "symbole";
    sym.width = 200;
    sym.height = 100;
    const Id symId = sym.id;
    p.views.push_back(sym);

    // ---- 1. LA CREATION : le sous-onglet Types IHM du symbole ----
    app::HmiTypesPane types("symt151", doc, apply);
    types.setBounds({0, 0, 1600, 900});
    types.setSymbolScope(symId);
    types.layout();
    std::string why;
    const std::size_t depth0 = stack.done().size();
    const Id etatId = types.addType("T_Etat", &why);
    const auto* etat = p.hmiType(etatId);
    check(etat && etat->symbol == symId && etat->ns == "S_Pompe" && hmi::fullName(*etat) == "S_Pompe.T_Etat" && stack.done().size() == depth0 + 1,
          "Nouveau type dans le symbole : S_Pompe.T_Etat, un homonyme du projet permis (la port\xC3\xA9" "e la plus proche), une commande " + why);
    check(types.addMember(etatId, "Niveau", "REAL", &why) && types.addMember(etatId, "Defaut", "BOOL", &why),
          "ses membres (le m\xC3\xAAme volet que Types IHM) : Niveau, Defaut " + why);
    check(types.addType("T_Etat", &why) == kNoId && why.find("d\xC3\xA9j\xC3\xA0") != std::string::npos,
          "un second T_Etat dans le m\xC3\xAAme symbole : refus\xC3\xA9 (" + why + ")");
    Id modeId = kNoId;
    apply(hmi::changeProject(doc, "Nouvelle \xC3\xA9num\xC3\xA9ration", [&](Project& q) {
        modeId = st::addType(q, symId, "T_Mode", hmi::HmiTypeKind::Enumeration);
        if (auto* t = q.hmiType(modeId)) {
            t->values = {{"Arret", 0, "", ""}, {"Marche", 1, "", ""}};
            (void)hmi::regenerateEnumConversions(q, *t);
        }
    }));
    const Id libreId = types.addType("T_Libre", &why);
    check(modeId != kNoId && libreId != kNoId && st::typesOf(p, symId).size() == 3, "le symbole porte 3 types (T_Etat, T_Mode, T_Libre)");
    {
        const auto listed = hmi::fold::items(p, hmi::fold::List::Types);
        check(listed.size() == 2 && std::none_of(listed.begin(), listed.end(), [&](const hmi::fold::Item& i) { return i.id == etatId; }),
              "la liste Types IHM du projet ne montre pas ceux du symbole");
        app::HmiTypesPane page("symt151p", doc, apply);
        page.setBounds({0, 0, 1600, 900});
        page.layout();
        page.refresh();
        check(page.typeTable().model() && page.typeTable().model()->rowCount() == 2, "la page Types IHM : les 2 types du projet");
        check(types.typeTable().model() && types.typeTable().model()->rowCount() == 3, "le sous-onglet du symbole : ses 3 types, sans dossiers");
    }
    {
        const std::size_t before = st::typesOf(p, symId).size();
        (void)stack.undo();
        check(st::typesOf(p, symId).size() == before - 1 && !p.hmiType(libreId), "Ctrl+Z reprend la cr\xC3\xA9" "ation de T_Libre (un pas)");
        (void)stack.redo();
        check(p.hmiType(libreId) != nullptr, "Ctrl+Y la refait");
    }
    // L'arbre : Types IHM [3] sous le symbole, ses types dessous.
    {
        app::ProjectTreeModel tree(std::make_shared<domain::Project>());
        tree.setHmi(doc);
        using NK = app::ProjectTreeModel::NodeKind;
        using HP = app::ProjectTreeModel::HmiPart;
        const auto viewNode = app::ProjectTreeModel::pack(NK::HmiView, static_cast<domain::Index>(symId & ((1u << 28) - 1)));
        const auto partNode = tree.childAt(viewNode, static_cast<std::size_t>(HP::Types));
        check(tree.childCount(viewNode) == static_cast<std::size_t>(HP::Count) && tree.text(partNode) == "Types IHM"
                  && tree.counterOf(partNode) == "3",
              "l'arbre : \xC2\xAB Types IHM \xC2\xBB et sa pastille 3 sous le symbole (" + tree.text(partNode) + " " + tree.counterOf(partNode) + ")");
        check(tree.childCount(partNode) == 3 && app::ProjectTreeModel::kindOf(tree.childAt(partNode, 0)) == NK::HmiTypeNode,
              "... ses trois types, des noeuds de type IHM (le clic ouvre leur page)");
    }

    // ---- 2. L'EMPLOI : un parametre, une fonction, un script du symbole ; une instance dans une vue ; le moteur ----
    apply(hmi::changeView(doc, symId, "Parametres", [&](Project& q, View& v) {
        v.params.push_back({"Etat", "", "", "T_Etat"});
        v.params.push_back({"Mode", "Marche", "", "T_Mode"});
        hmi::HmiFunction f;
        f.id = q.allocate();
        f.name = "Haut";
        f.returnType = "BOOL";
        f.decls.push_back(symt151::declaration(q, hmi::DeclKind::Parameter, "Seuil", "REAL"));
        f.decls.push_back(symt151::declaration(q, hmi::DeclKind::Variable, "e", "T_Etat"));
        f.body = "e.Niveau := Etat.Niveau * 2.0;\nNiv := e.Niveau;\nHaut := Etat.Niveau > Seuil AND Mode = T_Mode#Marche;";
        v.functions.push_back(std::move(f));
        hmi::Script open;
        open.id = q.allocate();
        open.name = "OnOpen";
        open.event = "OnOpen";
        open.body = "IF Mode = T_Mode#Marche THEN\n  Ouvert := Ouvert + 1;\nEND_IF;";
        v.scripts.push_back(std::move(open));
    }));
    {
        const View* s = p.view(symId);
        check(s && s->params.size() == 2 && s->params[0].type == "S_Pompe.T_Etat" && s->params[1].type == "S_Pompe.T_Mode",
              "un parametre du symbole de type T_Etat : le projet a un T_Etat, la port\xC3\xA9" "e la plus proche l'\xC3\xA9" "crit S_Pompe.T_Etat ("
                  + (s && !s->params.empty() ? s->params[0].type : std::string("?")) + ")");
        check(s && s->functions.size() == 1 && s->functions[0].decls[1].type == "S_Pompe.T_Etat",
              "la locale e : T_Etat de la fonction aussi (" + (s ? s->functions[0].decls[1].type : std::string("?")) + ")");
        check(st::qualify(p, *s, "x := T_Mode#Marche; y := Autre.T_Mode;") == "x := S_Pompe.T_Mode#Marche; y := Autre.T_Mode;",
              "le code du symbole pour le moteur : T_Mode#Marche -> S_Pompe.T_Mode#Marche (un membre reste) : "
                  + st::qualify(p, *s, "x := T_Mode#Marche; y := Autre.T_Mode;"));
        check(st::scopeOf(p, *s) == "S_Pompe" && st::shortIn(p, *s, "ARRAY[1..3] OF S_Pompe.T_Etat") == "ARRAY[1..3] OF T_Etat",
              "la port\xC3\xA9" "e du symbole ; ses types montr\xC3\xA9s sans pr\xC3\xA9" "fixe");
    }
    // Hors du symbole : par leur nom complet.
    p.programs.variables.push_back(variable(p, "Etat1", "S_Pompe.T_Etat", ""));
    p.programs.variables.push_back(variable(p, "Res", "BOOL", "FALSE"));
    p.programs.variables.push_back(variable(p, "Niv", "REAL", "0"));
    p.programs.variables.push_back(variable(p, "Ouvert", "INT", "0"));
    check(hmi::types::validType(p, "S_Pompe.T_Etat", &why) && hmi::types::membersOf(p, "S_Pompe.T_Etat").size() == 4,
          "une variable IHM de type S_Pompe.T_Etat (hors du symbole, son nom complet) : ses membres (Valeur, Marche, Niveau, Defaut) " + why);
    View vue = makeView(p, "Vue_Ligne");
    const Id vueId = vue.id;
    const Id inst = placeSymbol(p, vue, "S_Pompe", 10, 10);
    vue.object(inst)->name = "Pompe_1";
    vue.object(inst)->set("params", "Etat := Etat1; Mode := Marche");
    p.views.push_back(vue);
    p.config.startView = vueId;
    p.programs.scripts.push_back(calledScript(p, "Essai", "Etat1.Niveau := 75.0;\nRes := Vue_Ligne.Pompe_1.Haut(50.0);"));
    p.programs.scripts.push_back(calledScript(p, "Dehors", "VAR\n  m : S_Pompe.T_Mode;\nEND_VAR\nm := S_Pompe.T_Mode#Marche;\nIF m = S_Pompe.T_Mode#Marche THEN Ouvert := Ouvert + 10; END_IF;"));
    {
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        std::string err;
        const auto* ouvert = rt.variable("Ouvert");
        check(ouvert && ouvert->asInteger() == 1,
              "en marche : l'OnOpen du symbole dans Pompe_1 lit Mode = T_Mode#Marche (celui du symbole, pas T_Mode du projet) : Ouvert = "
                  + (ouvert ? ouvert->display() : std::string("?")));
        const bool ran = rt.callScript("Essai", 0.1, &err);
        const auto* res = rt.variable("Res");
        const auto* niv = rt.variable("Niv");
        check(ran && res && res->asInteger() != 0 && niv && std::fabs(niv->asReal() - 150.0) < 1e-6,
              "Vue_Ligne.Pompe_1.Haut(50.0) : sa locale e : T_Etat (Niveau, pas le Code du T_Etat du projet), TRUE et Niv = 150 ("
                  + err + (res ? " Res=" + res->display() : std::string()) + (niv ? " Niv=" + niv->display() : std::string()) + ")");
        const bool out = rt.callScript("Dehors", 0.2, &err);
        check(out && rt.variable("Ouvert") && rt.variable("Ouvert")->asInteger() == 11,
              "un script g\xC3\xA9n\xC3\xA9ral : m : S_Pompe.T_Mode, S_Pompe.T_Mode#Marche (le nom complet, hors du symbole) " + err);
    }
    {
        const auto issues = hmi::compileWith(p, [](std::string_view) { return false; }, {});
        check(!anyIssue(issues, "T_Etat") && !anyIssue(issues, "T_Mode") && !anyIssue(issues, "S_Pompe"),
              "Compiler : rien sur les types du symbole (" + issueWith(issues, "T_") + " " + issueWith(issues, "S_Pompe") + ")");
        // Hors du symbole, un nom court qui n'est qu'a lui : Compiler propose le nom complet.
        Project q = p;
        q.programs.scripts.push_back(calledScript(q, "Court", "VAR\n  x : T_Libre;\nEND_VAR\nx.Marche := TRUE;"));
        const auto more = hmi::compileWith(q, [](std::string_view) { return false; }, {});
        bool said = false;
        for (const auto& i : more) said = said || (i.property == "Court" && i.message.find("S_Pompe.T_Libre") != std::string::npos);
        check(said, "hors du symbole, T_Libre (nom court) : Compiler dit d'\xC3\xA9" "crire S_Pompe.T_Libre (" + issueWith(more, "T_Libre") + ")");
    }

    {
        // L'en-tete C du projet (un script C du symbole) : S_Pompe_T_Etat, et T_Etat dans le symbole.
        const std::string h = hmi::projectHeader(p, hmi::ScriptLang::C, p.view(symId));
        check(h.find("} S_Pompe_T_Etat;") != std::string::npos && h.find("typedef S_Pompe_T_Etat T_Etat;") != std::string::npos
                  && h.find("} T_Etat;") != std::string::npos,
              "l'en-t\xC3\xAAte C : S_Pompe_T_Etat (et T_Etat du projet) ; dans le symbole, T_Etat est le sien");
    }

    // ---- 3. RENOMMER, SUPPRIMER ----
    {
        const std::size_t d = stack.done().size();
        check(types.renameType(etatId, "T_EtatPompe", &why) && stack.done().size() == d + 1, "renommer T_Etat du symbole en T_EtatPompe : une commande " + why);
        const View* s = p.view(symId);
        check(p.variable("Etat1")->type == "S_Pompe.T_EtatPompe" && s->params[0].type == "S_Pompe.T_EtatPompe"
                  && s->functions[0].decls[1].type == "S_Pompe.T_EtatPompe" && p.variable("Autre")->type == "T_Etat",
              "ses emplois suivent (Etat1, le parametre, la locale) ; T_Etat du projet (Autre) ne bouge pas ("
                  + p.variable("Etat1")->type + ", " + s->params[0].type + ", " + p.variable("Autre")->type + ")");
        (void)stack.undo();
        check(p.hmiType(etatId)->name == "T_Etat" && p.variable("Etat1")->type == "S_Pompe.T_Etat", "Ctrl+Z : T_Etat, Etat1 en S_Pompe.T_Etat");
    }
    {
        // Le symbole renomme : l'espace de ses types et leurs emplois hors du symbole suivent.
        apply(hmi::changeProject(doc, "Renommer le symbole", [&](Project& q) {
            (void)hmi::renameSymbol(q, "S_Pompe", "S_Vanne");
            q.view(symId)->name = "S_Vanne";
        }));
        check(p.hmiType(etatId)->ns == "S_Vanne" && p.variable("Etat1")->type == "S_Vanne.T_Etat"
                  && p.view(symId)->params[0].type == "S_Vanne.T_Etat" && p.generalScript("Dehors")->body.find("S_Vanne.T_Mode#Marche") != std::string::npos,
              "le symbole renomm\xC3\xA9 S_Vanne : S_Vanne.T_Etat (Etat1, le parametre), S_Vanne.T_Mode#Marche dans un script ("
                  + p.variable("Etat1")->type + ")");
        (void)stack.undo();
        check(p.hmiType(etatId)->ns == "S_Pompe" && p.variable("Etat1")->type == "S_Pompe.T_Etat", "Ctrl+Z : S_Pompe de nouveau");
    }
    {
        const std::size_t d = stack.done().size();
        types.selectType(libreId);
        types.showTypeTab(app::HmiTypesPane::TabMembers);
        check(!types.deleteType(etatId, &why) && why.find("employ") != std::string::npos && types.selectedType() == etatId
                  && types.currentTypeTab() == app::HmiTypesPane::TabUses && stack.done().size() == d && !types.uses().empty(),
              "supprimer T_Etat employ\xC3\xA9 : refus\xC3\xA9, ses emplois d'abord (l'onglet Emplois) : " + why);
        check(types.deleteType(libreId, &why) && !p.hmiType(libreId) && stack.done().size() == d + 1, "T_Libre (sans emploi) : supprim\xC3\xA9 " + why);
        (void)stack.undo();
        check(p.hmiType(libreId) && p.hmiType(libreId)->symbol == symId, "Ctrl+Z le rend au symbole");
    }

    // ---- 4. LE SYMBOLE DUPLIQUE, EXPORTE, IMPORTE ; LA CORBEILLE ----
    {
        Id copyId = kNoId;
        apply(hmi::changeProject(doc, "Dupliquer", [&](Project& q) { copyId = hmi::design::duplicateViewReplacing(q, symId, "S_Pompe_Bis", {}, {}); }));
        const View* c = p.view(copyId);
        const auto own = st::typesOf(p, copyId);
        check(c && own.size() == 3 && own[0]->ns == "S_Pompe_Bis" && own[0]->id != etatId && c->params[0].type == "S_Pompe_Bis.T_Etat"
                  && st::typesOf(p, symId).size() == 3,
              "dupliquer le symbole : la copie a ses 3 types (S_Pompe_Bis.T_Etat), son parametre vise les siens ("
                  + (c ? c->params[0].type : std::string("?")) + ")");
        (void)stack.undo();
        check(!p.view(copyId) && st::typesOf(p, copyId).empty(), "Ctrl+Z : la copie et ses types partent");
    }
    {
        const auto pkg = hmi::pkg::collectSymbols(p, {symId});
        const auto owned = std::count_if(pkg.content.programs.types.begin(), pkg.content.programs.types.end(),
                                         [&](const HmiType& t) { return t.symbol == symId; });
        check(owned == 3, "exporter le symbole (.xpgsymboles) : ses 3 types partent avec lui");
        const auto zipped = hmi::pkg::fromZip(hmi::pkg::toZip(pkg));
        check(zipped.has_value(), "le paquet se relit");
        if (zipped.has_value()) {
            // Dans un autre projet : le symbole et ses types.
            auto other = std::make_shared<Document>();
            core::CommandStack ostack;
            const auto pl = hmi::pkg::plan(other->project, zipped.value());
            check(std::none_of(pl.items.begin(), pl.items.end(), [](const hmi::pkg::Item& i) { return i.kind == hmi::pkg::ItemKind::Type && i.name == "T_Mode"; }),
                  "le plan : les types du symbole suivent son choix (pas de ligne a eux)");
            (void)ostack.push(hmi::changeProject(other, "Importer", [&](Project& q) { (void)hmi::pkg::importInto(q, zipped.value(), pl); }));
            const View* is = other->project.viewByName("S_Pompe");
            const auto ot = is ? st::typesOf(other->project, is->id) : std::vector<const HmiType*>{};
            check(is && ot.size() == 3 && ot[0]->ns == "S_Pompe" && other->project.hmiTypeByName("S_Pompe.T_Mode") != nullptr,
                  "importer dans un autre projet : S_Pompe et ses 3 types (S_Pompe.T_Mode)");
            // Dans le meme projet, sous un autre nom : ses types suivent le nouveau nom, ceux de S_Pompe restent.
            auto pl2 = hmi::pkg::plan(p, zipped.value());
            for (auto& it : pl2.items)
                if (it.name == "S_Pompe") {
                    it.choice = hmi::pkg::Choice::Rename;
                    it.newName = "S_Pompe_2";
                }
            apply(hmi::changeProject(doc, "Importer", [&](Project& q) { (void)hmi::pkg::importInto(q, zipped.value(), pl2); }));
            const View* s2 = p.viewByName("S_Pompe_2");
            const auto t2 = s2 ? st::typesOf(p, s2->id) : std::vector<const HmiType*>{};
            check(s2 && t2.size() == 3 && t2[0]->ns == "S_Pompe_2" && s2->params[0].type == "S_Pompe_2.T_Etat" && st::typesOf(p, symId).size() == 3
                      && p.hmiType(etatId)->ns == "S_Pompe",
                  "importer sous le nom S_Pompe_2 : ses types S_Pompe_2.T_Etat (son parametre aussi), ceux de S_Pompe intacts ("
                      + (s2 ? s2->params[0].type : std::string("?")) + ")");
            (void)stack.undo();
            check(!p.viewByName("S_Pompe_2"), "Ctrl+Z reprend l'import");
        }
    }
    {
        // La corbeille : le symbole supprime emporte ses types, dans le meme element ; restaure, ils reviennent.
        const auto all = p.programs.types.size();
        std::size_t trashed = 0;
        apply(hmi::deleteFromProject(doc, "Supprimer le symbole", [&](Project& q) {
            q.views.erase(std::remove_if(q.views.begin(), q.views.end(), [&](const View& v) { return v.id == symId; }), q.views.end());
        }, &trashed));
        check(trashed == 1 && p.programs.types.size() == all - 3 && !p.trash.empty() && p.trash.back().content
                  && p.trash.back().content->programs.types.size() == 3,
              "supprimer le symbole : ses 3 types partent avec lui, dans le m\xC3\xAAme \xC3\xA9l\xC3\xA9ment de la corbeille");
        const Id item = p.trash.empty() ? kNoId : p.trash.back().id;
        apply(hmi::restoreFromTrash(doc, item));
        const View* back = p.viewByName("S_Pompe");
        check(back && st::typesOf(p, back->id).size() == 3 && p.programs.types.size() == all, "le restaurer : ses types reviennent avec lui");
        (void)stack.undo();
        (void)stack.undo();
        check(p.view(symId) && st::typesOf(p, symId).size() == 3, "Ctrl+Z, Ctrl+Z : comme avant");
    }

    // ---- 5. L'ENREGISTREMENT, RELU ; UNE VERSION D'AVANT ----
    {
        namespace fs = std::filesystem;
        const auto dir = fs::temp_directory_path() / "xpg_hmi_symtypes151";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        check(static_cast<bool>(hmi::save(p, dir.string())), "le projet s'\xC3\xA9" "crit");
        const auto back = hmi::load(dir.string());
        const HmiType* t = back.has_value() ? back.value().hmiTypeByName("S_Pompe.T_Etat") : nullptr;
        check(t && t->symbol == symId && t->members.size() == 4 && back.value().view(symId)->params[0].type == "S_Pompe.T_Etat"
                  && st::typesOf(back.value(), symId).size() == 3,
              "relu : les 3 types du symbole, son parametre");
        // Le fichier : symbole=<id> sur la ligne type_ihm ; une version d'avant l'ignore - un type de l'espace S_Pompe.
        fs::path index;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec))
            if (e.path().filename() == "ihm.txt") index = e.path();
        std::string text = readAll(index);
        check(text.find("symbole=" + std::to_string(symId)) != std::string::npos, "ihm.txt : symbole=" + std::to_string(symId) + " sur ses types");
        for (std::size_t at; (at = text.find(" symbole=" + std::to_string(symId))) != std::string::npos;)
            text.erase(at, (" symbole=" + std::to_string(symId)).size());
        { std::ofstream out(index, std::ios::binary | std::ios::trunc); out << text; }
        const auto old = hmi::load(dir.string());
        const HmiType* o = old.has_value() ? old.value().hmiTypeByName("S_Pompe.T_Etat") : nullptr;
        check(o && o->symbol == kNoId && o->members.size() == 4 && old.value().view(vueId) && old.value().variable("Etat1"),
              "sans symbole= (une version d'avant) : un type de l'espace S_Pompe, rien ne se perd");
        fs::remove_all(dir, ec);
    }
    core::CommandGroupScope::registerStack(nullptr);
}
