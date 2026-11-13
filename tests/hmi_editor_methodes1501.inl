// =============================================================================
//  tests/hmi_editor_methodes1501.inl - 1.50.1 : LES LIMITES DES METHODES DE LA 1.50.0, LEVEES
// -----------------------------------------------------------------------------
//  Inclus par hmi_editor_test.cpp (apres methodes150) :
//    1. ESSAYER une methode (comme une fonction) : sur une instance d'essai du type (SELF avant pose comme dans
//       l'onglet Tests), le resultat et SELF apres ; le projet n'est jamais touche.
//    2. LES SURCHARGES des methodes, comme celles des fonctions : la fenetre, Nouvelle surcharge, la liste, le choix
//       d'un appel par ses arguments (le moteur, Compiler), une meme forme et un appel ambigu dits clairement.
//    3. La SIGNATURE d'une methode VIRTUELLE du parent (un parametre renomme, retype, ajoute, retire ; le retour) suit
//       dans ses redefinitions : une question qui les nomme, un Ctrl+Z, leurs corps se lisent encore.
//    4. L'AIDE A LA SAISIE : une methode privee grisee (la raison dite) apres tout chemin vers une instance - variable
//       IHM, locale, parametre, membre d'un autre type, a[i]., p^., F(). - et pas dans les methodes du type (SELF., son
//       nom nu) ni d'un sous-type (la regle du moteur).
// =============================================================================

namespace m1501 {

// Une methode posee directement dans le modele (le corps, les parametres en une ligne).
hmi::HmiFunction method(Project& p, const std::string& name, const std::string& params, const std::string& ret, const std::string& body,
                        bool isVirtual = false, bool isPrivate = false) {
    hmi::HmiFunction m;
    m.id = p.allocate();
    m.name = name;
    m.returnType = ret;
    m.body = body;
    m.isVirtual = isVirtual;
    m.visibility = isPrivate ? hmi::Visibility::Private : hmi::Visibility::Public;
    std::string why;
    (void)hmi::setMethodParams(m, params, [&p] { return p.allocate(); }, &why);
    return m;
}

const app::assist::Item* item(const std::vector<app::assist::Item>& items, const std::string& text) {
    for (const auto& it : items)
        if (it.text == text) return &it;
    return nullptr;
}

bool hasIssue(const std::vector<hmi::Issue>& issues, const std::string& part, hmi::Issue::Severity sev = hmi::Issue::Severity::Error) {
    return std::any_of(issues.begin(), issues.end(), [&](const hmi::Issue& i) { return i.severity == sev && i.message.find(part) != std::string::npos; });
}
std::string firstIssue(const std::vector<hmi::Issue>& issues, const std::string& part) {
    for (const auto& i : issues)
        if (i.message.find(part) != std::string::npos) return i.message;
    return issues.empty() ? std::string("(aucun constat)") : "(rien sur " + part + ") " + issues.front().message;
}

} // namespace m1501

void methodes1501() {
    std::printf("1.50.1 : Essayer une m\xC3\xA9thode, ses surcharges, la signature d'une virtuelle, le gris des priv\xC3\xA9" "es\n");
    namespace de = hmi::decledit;
    using namespace m1501;
    struct EditionIhm {
        core::Edition before = core::edition();
        EditionIhm() { core::setEdition(core::Edition::Ihm); }
        ~EditionIhm() { core::setEdition(before); }
    } editionIhm;
    auto doc = std::make_shared<Document>();
    core::CommandStack stack;
    core::CommandGroupScope::registerStack(&stack);
    auto apply = [&](core::CommandPtr c) { (void)stack.push(std::move(c)); };
    Project& p = doc->project;
    HmiType pompe;
    pompe.id = p.allocate();
    pompe.name = "T_Pompe";
    pompe.members = {{"Debit", "REAL", "", ""}, {"Pression", "REAL", "", ""}, {"EnMarche", "BOOL", "", ""}, {"EnDefaut", "BOOL", "", ""}};
    const Id pompeId = pompe.id;
    p.programs.types.push_back(pompe);
    {
        Variable v;
        v.id = p.allocate();
        v.name = "Pompe1";
        v.type = "T_Pompe";
        p.programs.variables.push_back(v);
    }
    app::HmiTypesPane types("ty1501", doc, apply);
    types.setBounds({0, 0, 1800, 1000});
    types.layout();
    types.selectType(pompeId);
    auto* methods = types.methods();
    check(methods && methods->type() == pompeId, "T_Pompe : la section M\xC3\xA9thodes");
    if (!methods) return;
    std::string why;

    // ---- 1. ESSAYER : une instance d'essai du type, SELF avant, le resultat et SELF apres ----
    app::HmiNewFunctionDialog::Answer a;
    a.name = "Demarrer";
    a.returnType = "BOOL";
    const Id dem = methods->createMethod(a, &why);
    check(dem != kNoId && methods->setParams(dem, "Consigne : REAL; Rampe : TIME", &why)
              && methods->setBody(dem, "IF EnDefaut THEN\n    Demarrer := FALSE;\nELSE\n    Debit := Consigne - Pression;\n    EnMarche := TRUE;\n"
                                       "    IHM_JOURNAL('d\xC3\xA9marrage {Debit}');\n    Demarrer := TRUE;\nEND_IF;\n"),
          "Demarrer(Consigne : REAL; Rampe : TIME) : BOOL, son corps " + why);
    {
        methods->selectMethod(dem);
        const int tryTool = methods->tools().actionByTip("Essayer la m\xC3\xA9thode");
        check(tryTool == app::HmiMethodsPane::ATry && methods->tools().isEnabled(tryTool),
              "la barre des M\xC3\xA9thodes : Essayer (comme une fonction), permis sur Demarrer");
        const auto fields = methods->tryFields(dem);
        check(fields.size() == 6 && fields[0].label == "Consigne : REAL" && !fields[0].member && fields[1].label == "Rampe : TIME"
                  && fields[2].member && fields[2].label == "SELF.Debit : REAL (avant)" && fields[5].label == "SELF.EnDefaut : BOOL (avant)"
                  && fields[2].placeholder.find("valeur initiale") != std::string::npos,
              "la fen\xC3\xAA" "tre d'Essayer : un argument par param\xC3\xA8tre, puis SELF avant (un membre par case, vide : sa valeur initiale)");
        // Sans hote : l'essai part tout de suite ; avec hote : la fenetre est demandee (l'ecran l'ouvre).
        Id asked = kNoId;
        std::vector<std::pair<hmi::pipeline::Severity, std::string>> shown;
        app::HmiMethodsPane::TryHosts th;
        th.ask = [&](Id id) { asked = id; };
        th.output = [&](const std::vector<std::pair<hmi::pipeline::Severity, std::string>>& lines) { shown = lines; };
        methods->setTryHosts(th);
        methods->tools().triggered->emit(app::HmiMethodsPane::ATry);
        check(asked == dem, "Essayer : la fen\xC3\xAA" "tre des arguments est demand\xC3\xA9" "e \xC3\xA0 l'h\xC3\xB4te (Demarrer)");
        const std::string undoBefore = stack.undoLabel();
        const std::size_t varsBefore = p.programs.variables.size();
        const bool ok = methods->tryWithAnswers(dem, {"6,0", "T#2s", "", "2.0", "", ""});
        const auto& t = methods->lastTrial();
        const auto has = [](const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); };
        check(ok && t.ok && t.result == "TRUE" && t.call == "T_Pompe.Demarrer(Consigne := 6.0, Rampe := T#2s)",
              "Essayer Demarrer(6,0 ; T#2s) : TRUE (" + t.call + " = " + t.result + " " + t.error + ")");
        check(has(t.before, "Pression := 2.0") && has(t.self, "Debit = 4.0") && has(t.self, "EnMarche = TRUE") && has(t.self, "Pression = 2.0"),
              "SELF avant (Pression := 2.0) pos\xC3\xA9, SELF apr\xC3\xA8s : Debit = 4.0, EnMarche = TRUE");
        check(has(t.changed, "SELF.Debit : 0.0  \xE2\x86\x92  4.0") && has(t.changed, "SELF.EnMarche : FALSE  \xE2\x86\x92  TRUE")
                  && !has(t.changed, "SELF.Pression : 0.0  \xE2\x86\x92  2.0"),
              "ce que la m\xC3\xA9thode a chang\xC3\xA9 : SELF.Debit, SELF.EnMarche (pas la pose de Pression)");
        check(!t.journal.empty() && t.journal.front().find("d\xC3\xA9marrage 4") != std::string::npos,
              "son journal : " + (t.journal.empty() ? std::string("(vide)") : t.journal.front()));
        bool afterLine = false, resultLine = false;
        for (const auto& [sev, text] : shown) {
            afterLine = afterLine || text.rfind("SELF apr\xC3\xA8s : ", 0) == 0;
            resultLine = resultLine || (text.find("= TRUE") != std::string::npos && text.find("instance d'essai de T_Pompe") != std::string::npos);
        }
        check(resultLine && afterLine, "aux Sorties du panneau du bas : le r\xC3\xA9sultat (une instance d'essai de T_Pompe) et SELF apr\xC3\xA8s");
        check(methods->lastMessage().rfind("Essai : T_Pompe.Demarrer(", 0) == 0 && methods->lastMessage().find("= TRUE") != std::string::npos,
              "la barre du volet : " + methods->lastMessage());
        check(stack.undoLabel() == undoBefore && p.programs.variables.size() == varsBefore && !p.variable("Essai_SELF") && p.programs.functions.empty(),
              "le projet n'est pas touch\xC3\xA9 (aucune commande, ni Essai_SELF, ni Essai_Demarrer)");
        // SELF avant change le resultat : en defaut, la pompe refuse.
        check(methods->tryMethod(dem, {"6.0", "T#2s"}, {{"SELF.EnDefaut", "VRAI"}}) && methods->lastTrial().result == "FALSE"
                  && methods->lastTrial().changed.empty(),
              "SELF.EnDefaut := VRAI avant : FALSE, rien ne change (" + methods->lastTrial().error + ")");
        check(!methods->tryMethod(dem, {"6.0 +", "T#2s"}) && methods->lastTrial().error.rfind("Consigne : ", 0) == 0,
              "un argument illisible : l'essai le dit (" + methods->lastTrial().error + ")");
        check(!methods->tryMethod(dem, {"6.0"}, {{"Inconnu", "1"}}) && methods->lastTrial().error.find("n'a pas ce membre") != std::string::npos,
              "SELF.Inconnu : refus\xC3\xA9 (" + methods->lastTrial().error + ")");
        // Une privee s'essaie aussi (le banc la rend appelable).
        check(methods->setPrivate(dem, true) && methods->tryMethod(dem, {"1.0", "T#1s"}) && methods->lastTrial().result == "TRUE",
              "Demarrer priv\xC3\xA9" "e : elle s'essaie aussi (" + methods->lastTrial().error + ")");
        (void)stack.undo();
        methods->setTryHosts({});
    }

    // ---- 2. LES SURCHARGES ----
    a = {};
    a.name = "Regler";
    a.returnType = "REAL";
    const Id r1 = methods->createMethod(a, &why);
    check(r1 != kNoId && methods->setParams(r1, "v : REAL", &why) && methods->setBody(r1, "Debit := v * 2.0;\nRegler := Debit;\n"),
          "Regler(v : REAL) : REAL " + why);
    Id r2 = kNoId;
    {
        // La fenetre : un nom deja pris fait une surcharge (dit sous le nom) ; pas virtuelle.
        menu::MenuManager mm{menu::MenuFactory{}};
        app::HmiNewFunctionDialog::Spec spec;
        spec.doc = doc;
        spec.type = pompeId;
        mm.ShowDialog(std::make_unique<app::HmiNewFunctionDialog>(std::move(spec)), [](const menu::DialogResult&) {});
        mm.applyPending();
        auto* d = dynamic_cast<app::HmiNewFunctionDialog*>(mm.top());
        check(d && d->answer().name != "Regler", "la fen\xC3\xAA" "tre propose un nom libre (" + (d ? d->answer().name : std::string("?")) + ")");
        if (d) {
            d->setName("Regler");
            check(d->canConfirm() && d->nameNote() == "D\xC3\xA9j\xC3\xA0 pris : une surcharge (2 m\xC3\xA9thodes Regler) ; ses param\xC3\xA8tres la distingueront.",
                  "Regler d\xC3\xA9j\xC3\xA0 pris : une surcharge, permise (" + d->nameNote() + d->nameProblem() + ")");
            d->setVirtual(true);
            check(!d->canConfirm() && d->nameProblem().find("ne peut pas \xC3\xAAtre virtuelle") != std::string::npos,
                  "une surcharge virtuelle : refus\xC3\xA9" "e (" + d->nameProblem() + ")");
            d->cancel();
        }
        mm.applyPending();
        // Nouvelle surcharge (la barre) : le meme nom, son retour ; l'onglet Parametres s'ouvre ; un Ctrl+Z.
        methods->selectMethod(r1);
        const int ov = methods->tools().actionByTip("Nouvelle surcharge");
        check(ov == app::HmiMethodsPane::AOverload && methods->tools().isEnabled(ov), "Nouvelle surcharge, permise sur Regler (non virtuelle)");
        const std::size_t before = p.hmiType(pompeId)->methods.size();
        methods->tools().triggered->emit(ov);
        r2 = methods->selectedMethod();
        const auto* m2 = hmi::methodById(p, r2);
        check(r2 != r1 && m2 && m2->name == "Regler" && m2->returnType == "REAL" && p.hmiType(pompeId)->methods.size() == before + 1
                  && methods->currentCodeTab() == app::HmiMethodsPane::CodeTabParameters && stack.undoLabel() == "Nouvelle m\xC3\xA9thode T_Pompe.Regler",
              "Nouvelle surcharge : un 2e Regler : REAL, l'onglet Param\xC3\xA8tres ouvert (" + stack.undoLabel() + ")");
        (void)stack.undo();
        check(p.hmiType(pompeId)->methods.size() == before, "un Ctrl+Z la retire");
        (void)stack.redo();
        r2 = p.hmiType(pompeId)->methods.back().id;
        // Ses parametres la distinguent ; la meme forme qu'une autre est refusee, la raison dite.
        const bool sameShape = methods->setParams(r2, "x : REAL", &why);
        check(!sameShape && why.find("deux surcharges ont la m\xC3\xAAme forme Regler(REAL)") != std::string::npos,
              "Regler(x : REAL) : la m\xC3\xAAme forme que Regler(v : REAL), refus\xC3\xA9 (" + why + ")");
        check(methods->setParams(r2, "a : INT; b : INT", &why) && methods->setBody(r2, "Pression := TO_REAL(a + b);\nRegler := Pression;\n"),
              "Regler(a : INT; b : INT) : REAL " + why);
        const bool madeVirtual = methods->setVirtual(r2, true, &why);
        check(!madeVirtual && why.find("virtuelle et surcharg\xC3\xA9" "e") != std::string::npos,
              "une surcharge ne devient pas virtuelle (" + why + ")");
        methods->refresh();
        std::size_t rows = 0, kinds = 0;
        const auto model = methods->methodTable().model();
        for (std::size_t r = 0; model && r < model->rowCount(); ++r)
            if (model->cellText(r, 0) == "Regler") {
                ++rows;
                kinds += model->cellText(r, 2) == "surcharge" ? 1 : 0;
            }
        check(rows == 2 && kinds == 2, "la liste : deux lignes Regler, Genre \xC2\xAB surcharge \xC2\xBB, leurs param\xC3\xA8tres les distinguent");
        const auto* s2 = hmi::methodById(p, r2);
        check(s2 && hmi::methodFunctionName(*p.hmiType(pompeId), *s2) == "METHODE_T_POMPE_REGLER__" + std::to_string(r2)
                  && hmi::methodFunctionName(*p.hmiType(pompeId), *hmi::methodById(p, r1)) == "METHODE_T_POMPE_REGLER",
              "leurs noms internes : METHODE_T_POMPE_REGLER, puis METHODE_T_POMPE_REGLER__<id>");
    }
    {
        // Le choix d'un appel par ses arguments - un script, une methode (son nom nu), le moteur.
        auto* t = p.hmiType(pompeId);
        t->methods.push_back(method(p, "Tout", "", "", "Regler(1, 2);\nRegler(1.5);\n"));
        t->methods.push_back(method(p, "Mixer", "a : INT; b : REAL", "", "Debit := 1.0;\n"));
        t->methods.push_back(method(p, "Mixer", "a : REAL; b : INT", "", "Debit := 2.0;\n"));
        Script sc;
        sc.id = p.allocate();
        sc.name = "Lancement";
        sc.event = "Appel";
        sc.body = "Pompe1.Regler(2.5);\nPompe1.Regler(3, 4);\n";
        p.programs.scripts.push_back(sc);
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        hmi::Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        std::string err;
        sim::Value debit, pression;
        const bool ran = rt.callScript("Lancement", 0.2, &err);
        check(ran && rt.environment().read("Pompe1.Debit", debit) && rt.environment().read("Pompe1.Pression", pression)
                  && debit.asReal() == 5.0 && pression.asReal() == 7.0,
              "Pompe1.Regler(2.5) prend Regler(REAL) (Debit = 5.0), Pompe1.Regler(3, 4) prend Regler(INT, INT) (Pression = 7.0) " + err);
        p.programs.scripts.back().body = "Pompe1.Tout();\n";
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        const bool ranTout = rt.callScript("Lancement", 0.2, &err);
        check(ranTout && rt.environment().read("Pompe1.Debit", debit) && rt.environment().read("Pompe1.Pression", pression)
                  && debit.asReal() == 3.0 && pression.asReal() == 3.0,
              "dans une m\xC3\xA9thode, Regler(1, 2) puis Regler(1.5) (son nom nu) : chacun la sienne " + err);
        // Compiler : un appel ambigu, un appel qu'aucune ne prend, deux surcharges de la meme forme - dits clairement.
        p.programs.scripts.back().body = "Pompe1.Mixer(1, 2);\nPompe1.Regler('a', 2, 3);\nPompe1.Mixer(1, 2.5);\n";
        const auto issues = hmi::compileWith(p, {}, {});
        check(hasIssue(issues, "appel ambigu de T_Pompe.Mixer") && hasIssue(issues, "T_Pompe.Mixer(INT, REAL) et T_Pompe.Mixer(REAL, INT)"),
              "Compiler : Pompe1.Mixer(1, 2) est ambigu - " + firstIssue(issues, "Mixer"));
        check(hasIssue(issues, "aucune surcharge de T_Pompe.Regler ne prend cet appel"), "Compiler : Pompe1.Regler('a', 2, 3) - " + firstIssue(issues, "Regler"));
        check(std::count_if(issues.begin(), issues.end(), [](const hmi::Issue& i) { return i.message.find("Mixer") != std::string::npos; }) == 1,
              "Pompe1.Mixer(1, 2.5) se choisit sans faute (Mixer(INT, REAL))");
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        p.programs.scripts.back().body = "Pompe1.Mixer(1, 2);\n";
        const bool ranAmbiguous = rt.callScript("Lancement", 0.2, &err);
        check(!ranAmbiguous && err.find("appel ambigu") != std::string::npos, "en marche aussi, l'appel ambigu est refus\xC3\xA9 : " + err);
        // dans le corps d'une methode : Compiler lit ses appels de methodes de la meme regle
        t = p.hmiType(pompeId);
        t->methods.push_back(method(p, "Faute", "", "", "Mixer(1, 2);\n"));
        const Id fauteId = t->methods.back().id;
        const auto inBody = hmi::checkMethod(p, {}, fauteId);
        check(hasIssue(inBody, "T_Pompe.Faute, ligne 1 : appel ambigu de T_Pompe.Mixer"), "le corps d'une m\xC3\xA9thode : " + firstIssue(inBody, "Mixer"));
        // deux surcharges de la meme forme (un projet ecrit a la main) : Compiler le dit, sur chacune
        t = p.hmiType(pompeId);
        t->methods.push_back(method(p, "Regler", "w : REAL", "REAL", "Regler := w;\n"));
        const auto twice = hmi::generate(p, {}, {});
        const auto shapeCount = std::count_if(twice.begin(), twice.end(), [](const hmi::Issue& i) {
            return i.message.find("T_Pompe.Regler : deux surcharges ont la m\xC3\xAAme forme Regler(REAL)") != std::string::npos;
        });
        check(shapeCount == 2, "deux Regler(REAL) : G\xC3\xA9n\xC3\xA9rer le dit sur chacune - " + firstIssue(twice, "m\xC3\xAAme forme"));
        const auto inEditor2 = hmi::checkMethod(p, {}, t->methods.back().id);
        check(hasIssue(inEditor2, "deux surcharges ont la m\xC3\xAAme forme Regler(REAL) - un appel ne saurait laquelle prendre"),
              "et son \xC3\xA9" "diteur (F7) : " + firstIssue(inEditor2, "forme"));
        t = p.hmiType(pompeId);
        t->methods.erase(std::remove_if(t->methods.begin(), t->methods.end(),
                                        [](const hmi::HmiFunction& f) { return f.name == "Faute" || f.name == "Mixer" || f.name == "Tout" || (f.name == "Regler" && hmi::methodParamsText(f) == "w : REAL"); }),
                         t->methods.end());
        p.programs.scripts.clear();
        p.views.clear();
        p.config.startView = kNoId;
        // Renommer une methode surchargee : ses surcharges avec elle (un appel Regler(...) les vise toutes).
        methods->refresh();
        const bool renamedOverloads = methods->renameMethod(r2, "Ajuster", &why);
        check(renamedOverloads && hmi::methodById(p, r1)->name == "Ajuster" && hmi::methodById(p, r2)->name == "Ajuster",
              "renommer une surcharge : les deux Regler deviennent Ajuster " + why);
        (void)stack.undo();
        check(hmi::methodById(p, r1)->name == "Regler" && hmi::methodById(p, r2)->name == "Regler", "Ctrl+Z : Regler et Regler");
    }

    // ---- 3. LA SIGNATURE D'UNE VIRTUELLE SUIT DANS SES REDEFINITIONS ----
    {
        HmiType base;
        base.id = p.allocate();
        base.name = "T_Base";
        base.members = {{"Niveau", "REAL", "", ""}};
        base.methods.push_back(method(p, "Calculer", "Entree : REAL", "REAL", "Calculer := Entree + Niveau;\n", true));
        const Id baseId = base.id, calc = base.methods.back().id;
        p.programs.types.push_back(base);
        HmiType fille;
        fille.id = p.allocate();
        fille.name = "T_Fille";
        fille.base = "T_Base";
        const Id filleId = fille.id;
        p.programs.types.push_back(fille);
        HmiType petite;
        petite.id = p.allocate();
        petite.name = "T_PetiteFille";
        petite.base = "T_Fille";
        const Id petiteId = petite.id;
        p.programs.types.push_back(petite);
        types.selectType(filleId);
        const Id over1 = methods->overrideMethod(calc, &why);
        check(over1 != kNoId && methods->setBody(over1, "(* double *)\nCalculer := SUPER.Calculer(Entree) * 2.0 + Entree;\n"),
              "T_Fille red\xC3\xA9" "finit Calculer (SUPER.Calculer(Entree)) " + why);
        {
            hmi::FunctionTest c;
            c.id = p.allocate();
            c.name = "trois";
            c.arguments = {"3.0"};
            c.expected = "9.0";
            p.hmiType(filleId)->methods.front().tests.push_back(c);
        }
        types.selectType(petiteId);
        const Id over2 = methods->overrideMethod(over1, &why);
        check(over2 != kNoId && methods->setBody(over2, "Calculer := SUPER.Calculer(Entree := Entree) - 1.0;\n"),
              "T_PetiteFille la red\xC3\xA9" "finit aussi (SUPER.Calculer(Entree := Entree)) " + why);
        // La question : qui elle nomme ; Oui tout de suite.
        std::vector<std::string> asked;
        bool answer = true;
        std::function<void()> pending;
        app::several::setConfirmHost([&](const std::string& title, const std::string& text, const std::string&, std::function<void()> yes) {
            asked.push_back(title + "\n" + text);
            if (answer) yes();
            else pending = std::move(yes);
        });
        types.selectType(baseId);
        methods->selectMethod(calc);
        auto& grid = methods->parametersGrid();
        const auto paramNames = [&](Id id) {
            std::string s;
            if (const auto* m = hmi::methodById(p, id))
                for (const auto& d : m->decls)
                    if (d.kind == hmi::DeclKind::Parameter) s += (s.empty() ? "" : "; ") + d.name + " : " + d.type;
            return s;
        };
        const std::string label0 = stack.undoLabel();
        const bool renamed = grid.setCell(0, de::Column::Name, "Valeur");
        check(renamed, "renommer Entree en Valeur (l'onglet Param\xC3\xA8tres de T_Base.Calculer) " + grid.lastMessage());
        check(asked.size() == 1 && asked[0].rfind("Changer la signature de T_Base.Calculer ?", 0) == 0
                  && asked[0].find("\xE2\x80\xA2 T_Fille.Calculer") != std::string::npos && asked[0].find("\xE2\x80\xA2 T_PetiteFille.Calculer") != std::string::npos
                  && asked[0].find("ses 2 red\xC3\xA9" "finitions") != std::string::npos,
              "une question qui nomme les red\xC3\xA9" "finitions touch\xC3\xA9" "es : " + (asked.empty() ? std::string("(aucune)") : asked[0]));
        const auto* f1 = hmi::methodById(p, over1);
        const auto* f2 = hmi::methodById(p, over2);
        check(paramNames(calc) == "Valeur : REAL" && paramNames(over1) == "Valeur : REAL" && paramNames(over2) == "Valeur : REAL",
              "Valeur partout : " + paramNames(calc) + " | " + paramNames(over1) + " | " + paramNames(over2));
        check(f1 && f1->body == "(* double *)\nCalculer := SUPER.Calculer(Valeur) * 2.0 + Valeur;\n"
                  && f2 && f2->body == "Calculer := SUPER.Calculer(Valeur := Valeur) - 1.0;\n",
              "leurs corps suivent (SUPER.Calculer(Valeur), l'argument nomm\xC3\xA9 aussi) : " + (f1 ? f1->body : "") + (f2 ? f2->body : ""));
        check(hmi::checkMethod(p, {}, over1).empty() && hmi::checkMethod(p, {}, over2).empty() && hmi::checkMethod(p, {}, calc).empty(),
              "leurs corps se lisent (Compiler : rien \xC3\xA0 redire)");
        const auto tests = hmi::runFunctionTests(p, *hmi::methodById(p, over1));
        check(tests.results.size() == 1 && tests.results[0].verdict == hmi::Verdict::Passed,
              "le cas de T_Fille.Calculer passe encore (3.0 -> 9.0) : " + (tests.results.empty() ? std::string("?") : tests.results[0].detail));
        check(stack.undoLabel().find("(et ses 2 red\xC3\xA9" "finitions)") != std::string::npos, "UNE commande : " + stack.undoLabel());
        (void)stack.undo();
        check(paramNames(calc) == "Entree : REAL" && paramNames(over1) == "Entree : REAL" && paramNames(over2) == "Entree : REAL"
                  && hmi::methodById(p, over1)->body.find("SUPER.Calculer(Entree) * 2.0 + Entree") != std::string::npos && stack.undoLabel() == label0,
              "UN Ctrl+Z : Entree partout, les corps d'avant");
        // Le type suit.
        const bool typed = grid.setCell(0, de::Column::Type, "LREAL");
        check(typed && paramNames(over1) == "Entree : LREAL" && paramNames(over2) == "Entree : LREAL",
              "Entree : LREAL - les red\xC3\xA9" "finitions suivent : " + paramNames(over1));
        (void)stack.undo();
        // Un parametre ajoute (+ Ajouter, puis son nom), puis retire : les redefinitions et leur SUPER suivent.
        grid.selectRows({0});
        const bool added = grid.add();
        grid.table().finishCellEdit(true, "Gain");
        check(added && paramNames(calc) == "Entree : REAL; Gain : REAL" && paramNames(over1) == "Entree : REAL; Gain : REAL"
                  && paramNames(over2) == "Entree : REAL; Gain : REAL",
              "Gain ajout\xC3\xA9 : " + paramNames(over1) + " | " + paramNames(over2));
        f1 = hmi::methodById(p, over1);
        f2 = hmi::methodById(p, over2);
        check(f1 && f1->body.find("SUPER.Calculer(Entree, Gain)") != std::string::npos && f2
                  && f2->body.find("SUPER.Calculer(Entree := Entree, Gain := Gain)") != std::string::npos
                  && hmi::checkMethod(p, {}, over1).empty() && hmi::checkMethod(p, {}, over2).empty(),
              "SUPER.Calculer(Entree, Gain) : leurs corps se lisent encore - " + (f1 ? f1->body : "") + (f2 ? f2->body : ""));
        check(f1 && f1->tests.size() == 1 && f1->tests[0].arguments == std::vector<std::string>{"3.0"}, "le cas de T_Fille garde son argument (3.0)");
        grid.selectRows({1});
        check(grid.removeSelected() && paramNames(over1) == "Entree : REAL" && paramNames(over2) == "Entree : REAL"
                  && hmi::methodById(p, over1)->body.find("SUPER.Calculer(Entree) * 2.0") != std::string::npos,
              "Gain retir\xC3\xA9 : partout, SUPER.Calculer(Entree)");
        // Le retour suit aussi (les proprietes de la methode).
        const bool retOk = methods->setReturnType(calc, "LREAL", &why);
        const auto afterRet = hmi::compileWith(p, {}, {});
        check(retOk && hmi::methodById(p, over1)->returnType == "LREAL" && hmi::methodById(p, over2)->returnType == "LREAL"
                  && !hasIssue(afterRet, "change sa signature"),
              "le retour LREAL : les red\xC3\xA9" "finitions suivent, Compiler ne dit pas \xC2\xAB change sa signature \xC2\xBB " + why);
        (void)stack.undo();
        check(hmi::methodById(p, over1)->returnType == "REAL", "Ctrl+Z : REAL partout");
        // Non : rien ne change (ni la virtuelle).
        answer = false;
        asked.clear();
        (void)grid.setCell(0, de::Column::Name, "Autre");
        check(asked.size() == 1 && paramNames(calc) == "Entree : REAL" && paramNames(over1) == "Entree : REAL", "la question sans r\xC3\xA9ponse : rien ne change");
        if (pending) pending();
        check(paramNames(calc) == "Autre : REAL" && paramNames(over2) == "Autre : REAL", "Oui (plus tard) : Autre partout");
        (void)stack.undo();
        answer = true;
        // Un nom qu'une redefinition emploie deja (une locale) : refuse, la raison dite, rien ne change.
        {
            auto* fm = &p.hmiType(filleId)->methods.front();
            hmi::Declaration loc;
            loc.id = p.allocate();
            loc.kind = hmi::DeclKind::Variable;
            loc.name = "Tampon";
            loc.type = "REAL";
            fm->decls.push_back(loc);
        }
        asked.clear();
        const bool tampon = grid.setCell(0, de::Column::Name, "Tampon");
        check(!tampon && grid.lastMessage().find("T_Fille.Calculer : le param\xC3\xA8tre Tampon") != std::string::npos && asked.empty()
                  && paramNames(calc) == "Entree : REAL",
              "Tampon (une locale de T_Fille.Calculer) : refus\xC3\xA9, sans question - " + grid.lastMessage());
        app::several::setConfirmHost({});
        p.programs.types.erase(std::remove_if(p.programs.types.begin(), p.programs.types.end(),
                                              [&](const HmiType& x) { return x.id == baseId || x.id == filleId || x.id == petiteId; }),
                               p.programs.types.end());
        types.selectType(pompeId);
    }

    // ---- 4. L'AIDE A LA SAISIE : une privee grisee hors de son type, partout ; pas dans ses methodes ----
    {
        a = {};
        a.name = "Journal";
        a.pub = false;
        const Id jid = methods->createMethod(a, &why);
        a = {};
        a.name = "Moi";
        a.returnType = "T_Pompe";
        (void)methods->createMethod(a, &why);
        HmiType station;
        station.id = p.allocate();
        station.name = "T_Station";
        station.members = {{"Pompe", "T_Pompe", "", ""}};
        p.programs.types.push_back(station);
        for (const auto& [name, type] : {std::pair<const char*, const char*>{"Station1", "T_Station"}, {"Pompes", "ARRAY[1..3] OF T_Pompe"}}) {
            Variable v;
            v.id = p.allocate();
            v.name = name;
            v.type = type;
            p.programs.variables.push_back(v);
        }
        p.programs.functions.push_back(method(p, "PompeDe", "n : INT", "T_Pompe", "PompeDe := Pompe1;\n"));
        HmiType derivee;
        derivee.id = p.allocate();
        derivee.name = "T_PompeDoublee";
        derivee.base = "T_Pompe";
        derivee.methods.push_back(method(p, "Doubler", "", "", "Journal();\n"));
        const Id deriveeId = derivee.id;
        p.programs.types.push_back(derivee);
        HmiType autre;
        autre.id = p.allocate();
        autre.name = "T_Autre";
        autre.members = {{"Cible", "T_Pompe", "", ""}};
        autre.methods.push_back(method(p, "Agir", "", "", "Cible.Demarrer(1.0, T#1s);\n"));
        const Id autreId = autre.id;
        p.programs.types.push_back(autre);
        // (le code ecrit, ce qui precede le curseur) -> Journal grisee ?
        struct Case {
            std::string where, before;
        };
        const std::vector<Case> outside = {
            {"une variable IHM", "Pompe1."},
            {"une locale", "VAR\n    loc : T_Pompe;\nEND_VAR\nloc."},
            {"un param\xC3\xA8tre", "VAR_INPUT\n    q : T_Pompe;\nEND_VAR\nq."},
            {"le membre d'un autre type (variable IHM)", "Station1.Pompe."},
            {"le membre d'un autre type (locale)", "VAR\n    s : T_Station;\nEND_VAR\ns.Pompe."},
            {"une case a[i]", "VAR\n    t : ARRAY[0..3] OF T_Pompe;\n    i : INT;\nEND_VAR\nt[i]."},
            {"une case d'une variable IHM", "Pompes[2]."},
            {"un pointeur p^", "VAR\n    r : REF_TO T_Pompe;\nEND_VAR\nr^."},
            {"le r\xC3\xA9sultat d'une fonction", "PompeDe(1)."},
            {"le r\xC3\xA9sultat d'une m\xC3\xA9thode", "Pompe1.Moi()."},
        };
        for (const auto& c : outside) {
            const auto items = app::assist::suggest(p, nullptr, c.before, "", true);
            const auto* jm = item(items, "Journal");
            const auto* dm = item(items, "Demarrer");
            check(jm && jm->muted && jm->detail.find("priv\xC3\xA9" "e : seulement depuis une m\xC3\xA9thode de T_Pompe") != std::string::npos && dm && !dm->muted,
                  c.where + " (" + c.before.substr(c.before.rfind('\n') == std::string::npos ? 0 : c.before.rfind('\n') + 1)
                      + ") : Journal gris\xC3\xA9" "e, la raison dite ; Demarrer non" + (jm ? " [" + jm->detail + "]" : std::string(" [absente]")));
        }
        // Dans une methode de T_Pompe : SELF., Pompe1., son nom nu - pas grisee (le moteur la permet).
        const std::string self = "VAR_IN_OUT SELF : T_Pompe; THIS : T_Pompe; END_VAR ";
        for (const auto& [where, before, prefix] : {std::tuple<const char*, std::string, const char*>{"SELF.", self + "SELF.", ""},
                                                    {"THIS.", self + "THIS.", ""},
                                                    {"Pompe1. (dans une m\xC3\xA9thode)", self + "Pompe1.", ""},
                                                    {"son nom nu", self + "\n", "Jour"}}) {
            const auto items = app::assist::suggest(p, nullptr, before, prefix, true);
            const auto* jm = item(items, "Journal");
            check(jm && !jm->muted && jm->detail.find("priv\xC3\xA9" "e, permise ici") != std::string::npos,
                  std::string("dans une m\xC3\xA9thode de T_Pompe, ") + where + " : Journal propos\xC3\xA9" "e, pas gris\xC3\xA9" "e" + (jm ? " [" + jm->detail + "]" : std::string(" [absente]")));
        }
        // Un sous-type : ses methodes l'appellent (insideMethodOf : un type qui en herite) - pas grisee.
        {
            const auto items = app::assist::suggest(p, nullptr, "VAR_IN_OUT SELF : T_PompeDoublee; THIS : T_PompeDoublee; END_VAR SELF.", "", true);
            const auto* jm = item(items, "Journal");
            check(jm && !jm->muted, "dans une m\xC3\xA9thode de T_PompeDoublee (un sous-type), SELF.Journal : pas gris\xC3\xA9" "e");
        }
        // Un autre type (T_Autre.Agir) : Cible.Journal grisee.
        {
            const auto items = app::assist::suggest(p, nullptr, "VAR_IN_OUT SELF : T_Autre; THIS : T_Autre; END_VAR VAR Cible : T_Pompe; END_VAR Cible.", "", true);
            const auto* jm = item(items, "Journal");
            check(jm && jm->muted, "dans une m\xC3\xA9thode de T_Autre, Cible.Journal : gris\xC3\xA9" "e");
        }
        // Le vrai editeur des Methodes (ses declarations devant le code) : SELF. ne grise pas Journal ; T_Autre : si.
        types.selectType(pompeId);
        methods->selectMethod(dem);
        methods->showCodeTab(app::HmiMethodsPane::CodeTabCode);
        auto& ed = methods->editor();
        const std::string body = ed.text() + "SELF.";
        ed.setText(body);
        const auto lastLine = static_cast<std::size_t>(std::count(body.begin(), body.end(), '\n'));
        ed.goToLine(lastLine);
        ed.selectRange(lastLine, 5, 0);
        ed.requestCompletion();
        const auto greyed = [](const ui::MultiLineText::Completion& c) {
            return c.badge.accent && c.badge.color.r == c.badge.color.g && c.badge.color.g == c.badge.color.b;
        };
        const auto inEditor = std::find_if(ed.completions().begin(), ed.completions().end(), [](const ui::MultiLineText::Completion& c) { return c.text == "Journal"; });
        check(inEditor != ed.completions().end() && !greyed(*inEditor), "l'\xC3\xA9" "diteur de Demarrer : SELF. propose Journal, pas en gris");
        ed.dismissCompletion();
        types.selectType(autreId);
        methods->selectMethod(p.hmiType(autreId)->methods.front().id);
        auto& ed2 = methods->editor();
        ed2.setText("Cible.");
        ed2.selectRange(0, 6, 0);
        ed2.requestCompletion();
        const auto grey = std::find_if(ed2.completions().begin(), ed2.completions().end(), [](const ui::MultiLineText::Completion& c) { return c.text == "Journal"; });
        check(grey != ed2.completions().end() && greyed(*grey),
              "l'\xC3\xA9" "diteur de T_Autre.Agir : Cible.Journal en gris");
        ed2.dismissCompletion();
        (void)jid;
        (void)deriveeId;
    }
    core::CommandGroupScope::registerStack(nullptr);
}
