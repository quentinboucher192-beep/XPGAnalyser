// =============================================================================
//  tests/hmicheck_expr_test.cpp - Lot API 8 : les expressions impossibles
// -----------------------------------------------------------------------------
//  Compiler et Generer disent chaque expression qui se lit mais ne peut pas
//  marcher, a son endroit exact : un nom inconnu (avec le plus proche - une
//  variable renommee sur place laisse l'ancien nom), un membre, un indice
//  constant hors bornes, une fonction inconnue, le mauvais nombre d'arguments,
//  un texte compare a un nombre, un booleen ou une couleur attendus et autre
//  chose donne, une division par zero, une ecriture impossible. Et les
//  expressions justes ne declenchent rien (les exemples de la bibliotheque).
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiExamples.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPackage.hpp"      // lot API 8 : finitions (un modele de vue garde dans le projet)

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("  ECHEC  %s\n", what.c_str());
        ++failures;
    }
}

using S = Issue::Severity;

// Une erreur a cet endroit dont le message contient ces mots.
const Issue* find(const std::vector<Issue>& issues, Id view, Id object, std::string_view property, std::string_view text) {
    for (const auto& i : issues)
        if (i.severity == S::Error && i.view == view && i.object == object && (property.empty() || i.property == property)
            && i.message.find(text) != std::string::npos)
            return &i;
    return nullptr;
}
std::size_t errorsOn(const std::vector<Issue>& issues, Id view, Id object) {
    std::size_t n = 0;
    for (const auto& i : issues)
        if (i.severity == S::Error && i.view == view && i.object == object) ++n;
    return n;
}
void dump(const std::vector<Issue>& issues) {
    for (const auto& i : issues)
        if (i.severity == S::Error) std::printf("         [%s] %s : %s\n", i.category.c_str(), i.property.c_str(), i.message.c_str());
}

struct Fixture {
    Project p;
    Id      view{kNoId};
    Id      jauge{kNoId}, label{kNoId}, bon{kNoId}, bouton{kNoId}, alarm{kNoId}, recipe{kNoId}, courbe{kNoId};
};

Id addObject(Project& p, View& v, Kind k, const char* name) {
    const Id id = edit::add(p, v, k, 10, 10);
    v.object(id)->name = name;
    return id;
}

Fixture build() {
    Fixture f;
    Project& p = f.p;
    p.programs.types.push_back(HmiType{p.allocate(), "T_Four", {}, {TypeMember{"Temp", "REAL"}, TypeMember{"Porte", "BOOL"}}});
    p.programs.variables.push_back(Variable{p.allocate(), "Vitesse_Moteur", "REAL", "0", {}});
    p.programs.variables.push_back(Variable{p.allocate(), "Mode", "STRING", "'Auto'", {}});
    p.programs.variables.push_back(Variable{p.allocate(), "Marche", "BOOL", "FALSE", {}});
    Variable lecture{p.allocate(), "Lecture", "INT", "0", {}};
    lecture.readOnly = true;
    p.programs.variables.push_back(lecture);
    p.programs.variables.push_back(Variable{p.allocate(), "Consignes", "ARRAY[0..9] OF REAL", "0", {}});
    p.programs.variables.push_back(Variable{p.allocate(), "Four1", "T_Four", "", {}});

    View v = makeView(p, "Vue_Moteur");
    // L'objet qui a tout faux, une propriete par probleme.
    f.jauge = addObject(p, v, Kind::Rectangle, "Jauge");
    auto* j = v.object(f.jauge);
    j->setExpr("x", "Vitesse * 2");                        // le cas de l'utilisateur : Vitesse renommee en Vitesse_Moteur
    j->setExpr("y", "Consignes[12]");                      // indice constant hors bornes
    j->setExpr("w", "FOO(Vitesse_Moteur)");                // fonction inconnue
    j->setExpr("h", "LIMIT(0, Vitesse_Moteur)");           // mauvais nombre d'arguments
    j->setExpr("visible", "Mode = 3");                     // texte compare a un nombre
    j->setExpr("blink", "Mode");                           // booleen attendu, texte donne
    j->setExpr("fill", "Vitesse_Moteur > 3");              // couleur attendue, booleen donne
    j->setExpr("stroke", "'rouge'");                       // pas une couleur
    j->setExpr("rot", "Vitesse_Moteur / 0");               // division par zero
    j->setExpr("opacity", "Four1.Tmp");                    // membre inconnu
    j->setExpr("radius", "Vitesse_Moteur * (2");           // syntaxe
    // Un texte a trous avec une faute de frappe.
    f.label = addObject(p, v, Kind::Text, "Etiquette");
    v.object(f.label)->set("text", "Vitesse : {Vitesse_Moteu:0.0} km/h");
    // L'objet juste : rien a dire.
    f.bon = addObject(p, v, Kind::Rectangle, "Correct");
    auto* b = v.object(f.bon);
    b->setExpr("x", "Vitesse_Moteur * 2 + Pompe1");
    b->setExpr("y", "Consignes[3] + Four1.Temp");
    b->setExpr("w", "INT_TO_REAL(Lecture) + ABS(-3) + MIN(1, 2, 3) + LEN(Mode)");
    b->setExpr("visible", "Marche AND Mode = 'Auto' OR Four1.Porte");
    b->setExpr("fill", "SEL(Marche, '#808080', '#00A000')");
    b->setExpr("stroke", "'Mode : ' + Mode");
    b->setExpr("rot", "Vitesse_Moteur / 2 + 16#FF");
    // Les ecritures : une variable en lecture seule, une constante.
    f.bouton = addObject(p, v, Kind::Button, "Bouton");
    Action set;
    set.operation = Operation::Set;
    set.target = "Lecture";
    v.object(f.bouton)->actions.push_back(set);
    Action assign;
    assign.operation = Operation::Assign;
    assign.target = "3";
    assign.value = "Vitesse_Moteur";
    v.object(f.bouton)->actions.push_back(assign);
    // Tranche 2 : une courbe dont une plume garde l'ancien nom.
    f.courbe = addObject(p, v, Kind::Trend, "Courbe");
    v.object(f.courbe)->set("variables", "Vitesse_Moteur; Vitesse");
    f.view = v.id;
    p.views.push_back(std::move(v));
    // ... et une variable archivee aussi.
    p.history.archived.push_back("Vitesse");
    // Une alarme et une recette qui citent l'ancien nom.
    AlarmDef a;
    a.id = p.allocate();
    a.name = "Survitesse";
    a.condition = "Vitesse > 100";
    f.alarm = a.id;
    p.alarms.push_back(a);
    Recipe r;
    r.id = p.allocate();
    r.name = "Recette_Moteur";
    r.fields.push_back(RecipeField{});
    r.fields.back().name = "Consigne";
    RecipeRecord rec;
    rec.name = "Lent";
    rec.values.push_back("Vitesse * 0.5");
    r.records.push_back(rec);
    f.recipe = r.id;
    p.recipes.push_back(r);
    return f;
}

bool plc(std::string_view n) { return n == "Pompe1" || n == "Vitesse_API"; }

void outils() {
    std::printf("exprcheck : les outils\n");
    using W = exprcheck::Want;
    check(exprcheck::wantOf("visible") == W::Bool && exprcheck::wantOf("blink") == W::Bool, "visible, blink : un booleen");
    check(exprcheck::wantOf("fill") == W::Color && exprcheck::wantOf("textColor") == W::Color, "fill, textColor : une couleur");
    check(exprcheck::wantOf("x") == W::Number && exprcheck::wantOf("text") == W::Any, "x : un nombre ; text : tout");
    const auto t = exprcheck::templateExpressions("a {X:0.0} b {Y} c {SEL(M, 'a:b', 'c')}");
    check(t.size() == 3 && t[0] == "X" && t[1] == "Y" && t[2] == "SEL(M, 'a:b', 'c')", "les trous d'un texte, sans leur format");
    Fixture f = build();
    exprcheck::Context c;
    c.project = &f.p;
    check(exprcheck::closest(c, "Vitesse") == "Vitesse_Moteur", "le plus proche de Vitesse : Vitesse_Moteur");
    check(exprcheck::closest(c, "Vitesse_Moteu") == "Vitesse_Moteur", "une lettre de moins : Vitesse_Moteur");
    check(exprcheck::closest(c, "Zzz").empty(), "rien d'assez proche : rien");
}

void compiler() {
    std::printf("Compiler : les expressions impossibles\n");
    Fixture f = build();
    const auto issues = compileWith(f.p, plc);
    const Id v = f.view, j = f.jauge;
    const auto* vitesse = find(issues, v, j, "x", "Vitesse n'existe pas : veux-tu dire Vitesse_Moteur ?");
    check(vitesse != nullptr, "nom inconnu, avec le plus proche (vue, objet, propriete)");
    check(vitesse && vitesse->category == "Expression", "... dans la categorie Expression");
    check(find(issues, v, j, "y", "12") != nullptr, "indice constant hors bornes : Consignes[12]");
    check(find(issues, v, j, "w", "fonction inconnue : FOO") != nullptr, "fonction inconnue");
    check(find(issues, v, j, "h", "LIMIT prend 3 arguments, pas 2") != nullptr, "mauvais nombre d'arguments");
    check(find(issues, v, j, "visible", "tu compares un texte (Mode)") != nullptr, "texte compare a un nombre");
    check(find(issues, v, j, "blink", "un bool\xC3\xA9" "en est attendu") != nullptr, "booleen attendu, texte donne");
    check(find(issues, v, j, "fill", "une couleur est attendue") != nullptr, "couleur attendue, booleen donne");
    check(find(issues, v, j, "stroke", "'rouge' n'est pas une couleur") != nullptr, "un texte qui n'est pas une couleur");
    check(find(issues, v, j, "rot", "division par z\xC3\xA9ro") != nullptr, "division par une constante nulle");
    check(find(issues, v, j, "opacity", "Tmp") != nullptr, "membre inconnu d'une structure IHM");
    check(find(issues, v, j, "radius", "Vitesse_Moteur * (2") != nullptr, "la syntaxe (deja dite par compile)");
    check(find(issues, v, f.label, "text", "Vitesse_Moteu n'existe pas : veux-tu dire Vitesse_Moteur ?") != nullptr,
          "texte a trous : le nom inconnu et le plus proche");
    check(find(issues, v, f.bouton, "", "lecture seule") != nullptr, "ecriture vers une variable en lecture seule");
    check(find(issues, v, f.bouton, "", "constante") != nullptr, "ecriture vers une constante");
    bool alarm = false, recipe = false;
    for (const auto& i : issues) {
        if (i.severity == S::Error && i.item == f.alarm && i.category == "Alarme" && i.message.find("veux-tu dire Vitesse_Moteur") != std::string::npos) alarm = true;
        if (i.severity == S::Error && i.item == f.recipe && i.category == "Recette" && i.message.find("Vitesse n'existe pas") != std::string::npos) recipe = true;
    }
    check(alarm, "condition d'alarme : l'alarme en cause (double-clic)");
    check(find(issues, v, f.courbe, "variables", "plume Vitesse : Vitesse n'existe pas : veux-tu dire Vitesse_Moteur ?") != nullptr,
          "plume de courbe : le nom inconnu et le plus proche");
    bool archived = false;
    for (const auto& i : issues)
        if (i.severity == S::Error && i.category == "Historique" && i.message.find("veux-tu dire Vitesse_Moteur") != std::string::npos) archived = true;
    check(archived, "variable archiv\xC3\xA9" "e : le nom inconnu et le plus proche");
    check(recipe, "valeur de recette : la recette en cause");
    check(errorsOn(issues, v, f.bon) == 0, "l'objet juste : aucune erreur");
    if (errorsOn(issues, v, f.bon)) dump(issues);
    // Sans l'automate, un nom inconnu peut etre une variable de l'automate : il passe.
    const auto blind = compileWith(f.p, {});
    check(find(blind, v, j, "x", "Vitesse n'existe pas") == nullptr, "sans automate connu : le nom passe");
    check(find(blind, v, j, "h", "LIMIT prend 3") != nullptr, "sans automate connu : le reste est dit");
}

void generer() {
    std::printf("G\xC3\xA9n\xC3\xA9rer : les memes erreurs, et elles bloquent\n");
    Fixture f = build();
    const auto issues = generateWith(f.p, plc, GenerateOptions{});
    const Id v = f.view, j = f.jauge;
    check(count(issues).errors > 0, "des erreurs : la generation est bloquee");
    check(find(issues, v, j, "", "veux-tu dire Vitesse_Moteur ?") != nullptr, "nom inconnu, avec le plus proche");
    std::size_t vitesse = 0;
    for (const auto& i : issues)
        if (i.severity == S::Error && i.object == j && i.message.find("Vitesse ") != std::string::npos
            && i.message.find("Vitesse_Moteur *") == std::string::npos)
            ++vitesse;
    check(vitesse == 1, "le nom inconnu n'est dit qu'une fois (" + std::to_string(vitesse) + ")");
    check(find(issues, v, j, "radius", "Vitesse_Moteur * (2") != nullptr, "une expression illisible bloque aussi");
    check(find(issues, v, j, "w", "fonction inconnue : FOO") != nullptr, "fonction inconnue");
    check(find(issues, v, j, "fill", "une couleur est attendue") != nullptr, "couleur attendue");
    check(errorsOn(issues, v, f.bon) == 0, "l'objet juste : aucune erreur");
    if (errorsOn(issues, v, f.bon)) dump(issues);
}

// Tranche 2 : les chemins de l'automate (un membre, un indice constant), par
// les fonctions que l'application tire de son projet (PlcTypes).
void automate() {
    std::printf("les chemins de l'automate\n");
    exprcheck::PlcPaths paths;
    paths.rootType = [](std::string_view r) {
        return r == "Armoires" ? std::string("ARRAY[0..3] OF T_Armoire") : r == "Pompe1" ? std::string("INT") : std::string();
    };
    paths.memberType = [](std::string_view t, std::string_view m) {
        return t == "T_Armoire" && m == "etat" ? std::string("INT") : std::string();
    };
    paths.isStruct = [](std::string_view t) { return t == "T_Armoire"; };
    Fixture f = build();
    exprcheck::Context c;
    c.project = &f.p;
    c.known = [](std::string_view n) { return n == "Armoires" || n == "Pompe1"; };
    c.plc = paths;
    const auto has = [&](std::string_view src, std::string_view text) {
        for (const auto& pb : exprcheck::check(c, src))
            if (pb.message.find(text) != std::string::npos) return true;
        return false;
    };
    check(has("Armoires[0].etatt > 1", "n'a pas de membre etatt"), "membre inconnu d'une structure de l'automate");
    check(has("Armoires[7].etat", "indice 7 hors des bornes de Armoires (0..3"), "indice constant hors bornes d'un tableau de l'automate");
    check(has("Pompe1.champ", "il n'a pas de membre"), "membre d'une variable \xC3\xA9l\xC3\xA9mentaire de l'automate");
    check(exprcheck::check(c, "Armoires[2].etat + Pompe1.3 + Armoires[Pompe1].etat").empty(), "chemins justes, bit d'un mot, indice calcul\xC3\xA9 : rien");
    check(has("Armoires[1].etat = 'x'", "tu compares"), "le type du bout d'un chemin de l'automate est connu");
    // Compiler s'en sert, a l'endroit de l'objet.
    auto& v = f.p.views.front();
    v.object(f.bon)->setExpr("h", "Armoires[9].etat");
    const auto plcNames = [](std::string_view n) { return n == "Armoires" || n == "Pompe1"; };
    const auto issues = compileWith(f.p, plcNames, paths);
    check(find(issues, v.id, f.bon, "h", "indice 9 hors des bornes") != nullptr, "Compiler : l'indice hors bornes de l'automate, a son endroit");
    GenerateOptions opt;
    opt.plcPaths = paths;
    check(find(generateWith(f.p, plcNames, opt), v.id, f.bon, "h", "indice 9 hors des bornes") != nullptr, "G\xC3\xA9n\xC3\xA9rer aussi");
    check(find(compileWith(f.p, plcNames), v.id, f.bon, "h", "hors des bornes") == nullptr, "sans les chemins de l'automate : pas verifie");
}

void exemples() {
    std::printf("les exemples de la biblioth\xC3\xA8que : aucune erreur nouvelle\n");
    std::size_t n = 0;
    std::string bad;
    for (const auto k : kPlaceableKinds) {
        auto ex = examples::exampleFor(k);
        if (!ex) continue;
        ++n;
        const auto yes = [](std::string_view) { return true; };
        const auto issues = expressionIssues(ex->project, yes);
        for (const auto& i : issues)
            if (i.severity == S::Error) bad += "\n         " + std::string(kindLabel(k)) + " : " + i.property + " : " + i.message;
    }
    check(n > 20, "des exemples (" + std::to_string(n) + ")");
    check(bad.empty(), "aucune expression d'exemple n'est dite impossible" + bad);
}

// ---- Lot API 8 : finitions (les formats d'unites, les symboles hors des vues, les ecrans modeles) ----
bool anyError(const std::vector<Issue>& issues, std::string_view category, std::string_view property, std::string_view text) {
    for (const auto& i : issues)
        if (i.severity == S::Error && i.category == category && (property.empty() || i.property == property)
            && i.message.find(text) != std::string::npos)
            return true;
    return false;
}

void finitions() {
    std::printf("finitions : les formats d'unit\xC3\xA9s, les symboles, les \xC3\xA9" "crans mod\xC3\xA8les\n");
    exprcheck::PlcPaths paths;
    paths.rootType = [](std::string_view r) {
        return r == "Armoires" ? std::string("ARRAY[0..3] OF T_Armoire") : r == "Pompe1" ? std::string("INT") : std::string();
    };
    paths.memberType = [](std::string_view t, std::string_view m) {
        return t == "T_Armoire" && m == "etat" ? std::string("INT") : std::string();
    };
    paths.isStruct = [](std::string_view t) { return t == "T_Armoire"; };
    const auto plcNames = [](std::string_view n) { return n == "Armoires" || n == "Pompe1"; };
    Fixture f = build();
    Project& p = f.p;
    // Configuration > Unites et formats : un membre inconnu (tous les elements), un format illisible, une ligne juste.
    p.displays.push_back(VariableDisplay{"Armoires[].etatt", "bar", "0.0"});
    p.displays.push_back(VariableDisplay{"Vitesse_Moteur", "tr/min", "0,0x"});
    p.displays.push_back(VariableDisplay{"Armoires[].etat", "", "0"});
    p.displays.push_back(VariableDisplay{"Four1.Temp", "\xC2\xB0" "C", "0.0"});
    // Un symbole : Armoire := Armoires[0] ; son voyant cite un membre qui n'existe pas.
    View sym = makeView(p, "Carte");
    sym.role = "symbole";
    sym.params.push_back(ViewParam{"Armoire", "Armoires[0]", {}});
    const Id titre = addObject(p, sym, Kind::Text, "Titre");
    sym.object(titre)->set("text", "Etat {Armoire.etat}");
    const Id voyant = addObject(p, sym, Kind::Rectangle, "Voyant");
    sym.object(voyant)->setExpr("visible", "Armoire.etatt > 0");
    const Id symId = sym.id;
    p.views.push_back(std::move(sym));
    // Trois instances : un argument juste, un indice hors bornes, une variable sans membre.
    View* v = p.view(f.view);
    const auto instance = [&](const char* name, const char* args) {
        Object o = makeObject(Kind::SymbolInstance, p.allocate(), name, 400, 20, v->activeLayer);
        o.set("symbol", "Carte");
        o.set("params", args);
        v->objects.push_back(o);
        return o.id;
    };
    const Id bonne = instance("Carte_A", "Armoire := Armoires[1]");
    const Id horsBornes = instance("Carte_B", "Armoire := Armoires[7]");
    const Id entier = instance("Carte_C", "Armoire := Pompe1");
    // Un ecran modele (un gabarit) : ses objets sont lus a leur endroit, comme une vue.
    View modele = makeView(p, "Modele_Bandeau");
    modele.role = "modele";
    const Id bandeau = addObject(p, modele, Kind::Rectangle, "Bandeau");
    modele.object(bandeau)->setExpr("visible", "Mode");
    const Id modeleId = modele.id;
    p.views.push_back(std::move(modele));
    // Un modele de vue garde dans le projet (ihm/modeles) : relu comme le petit projet qu'il est.
    {
        Project m;
        m.programs.variables.push_back(Variable{m.allocate(), "Etat_Texte", "STRING", "''", {}});
        View mv = makeView(m, "Synoptique_Type");
        const Id voyantM = addObject(m, mv, Kind::Rectangle, "Voyant");
        mv.object(voyantM)->setExpr("visible", "Etat_Texte");          // un booleen attendu, un texte donne
        mv.object(voyantM)->setExpr("x", "Pression_Sud * 2");          // un nom de l'automate : choisi a la creation
        const Id mvId = mv.id;
        m.views.push_back(std::move(mv));
        ViewTemplateFile tpl;
        tpl.id = p.allocate();
        tpl.name = "Synoptique_Type";
        tpl.data = std::make_shared<const Bytes>(pkg::toZip(pkg::collect(m, {mvId})));
        p.viewTemplates.push_back(tpl);
    }

    const auto issues = compileWith(p, plcNames, paths);
    check(anyError(issues, "Mod\xC3\xA8le de vue", "Synoptique_Type", "Synoptique_Type.Voyant (visible) : Etat_Texte : un bool\xC3\xA9" "en est attendu"),
          "un mod\xC3\xA8le de vue du projet : l'expression impossible, avec sa vue, son objet et sa propri\xC3\xA9t\xC3\xA9");
    check(!anyError(issues, "Mod\xC3\xA8le de vue", "", "Pression_Sud"), "... un nom de l'automate n'y est pas v\xC3\xA9rifi\xC3\xA9 (choisi \xC3\xA0 la cr\xC3\xA9" "ation)");
    check(anyError(issues, "Unit\xC3\xA9", "Armoires[].etatt", "n'a pas de membre etatt"),
          "format d'unit\xC3\xA9 : le membre inconnu du chemin, \xC3\xA0 sa ligne (tous les \xC3\xA9l\xC3\xA9ments [])");
    check(anyError(issues, "Unit\xC3\xA9", "Vitesse_Moteur", "format \xC2\xAB 0,0x \xC2\xBB illisible"),
          "format d'unit\xC3\xA9 : le format illisible, \xC3\xA0 sa ligne (Compiler aussi)");
    check(!anyError(issues, "Unit\xC3\xA9", "Armoires[].etat", "") && !anyError(issues, "Unit\xC3\xA9", "Four1.Temp", ""),
          "les lignes justes : rien");
    check(find(issues, symId, voyant, "visible", "n'a pas de membre etatt") != nullptr,
          "le symbole relu avec ses valeurs par d\xC3\xA9" "faut : le membre inconnu, sur l'objet du symbole");
    check(find(issues, f.view, bonne, "", "") == nullptr, "l'instance juste : rien sur elle (le faux du symbole est dit sur le symbole)");
    if (find(issues, f.view, bonne, "", "")) dump(issues);
    check(find(issues, f.view, horsBornes, "params", "indice 7 hors des bornes de Armoires") != nullptr,
          "l'argument hors bornes : sur l'instance (propri\xC3\xA9t\xC3\xA9 params)");
    std::size_t sept = 0;
    for (const auto& i : issues)
        if (i.severity == S::Error && i.object == horsBornes && i.message.find("indice 7") != std::string::npos) ++sept;
    check(sept == 1, "... dit une fois (" + std::to_string(sept) + ")");
    check(find(issues, f.view, entier, "params", "Pompe1 est un INT de l'automate") != nullptr,
          "l'argument sans le membre que le symbole lit : sur l'instance, avec l'objet du symbole");
    check(find(issues, f.view, entier, "params", "Titre (text)") != nullptr, "... le message nomme l'objet et la propri\xC3\xA9t\xC3\xA9 du symbole");
    check(find(issues, modeleId, bandeau, "visible", "un bool\xC3\xA9" "en est attendu") != nullptr,
          "un \xC3\xA9" "cran mod\xC3\xA8le : son expression impossible, \xC3\xA0 son objet");
    const auto generated = generateWith(p, plcNames, [&] {
        GenerateOptions o;
        o.plcPaths = paths;
        return o;
    }());
    std::size_t format = 0;
    for (const auto& i : generated)
        if (i.severity == S::Error && i.category == "Unit\xC3\xA9" && i.message.find("illisible") != std::string::npos) ++format;
    check(format == 1, "G\xC3\xA9n\xC3\xA9rer : le format illisible dit une fois (" + std::to_string(format) + ")");
    check(anyError(generated, "Unit\xC3\xA9", "Armoires[].etatt", "etatt"), "G\xC3\xA9n\xC3\xA9rer : le chemin aussi");
    // L'exemple des symboles (trois cartes, un symbole a parametres) : rien de dit.
    if (auto ex = examples::exampleFor(Kind::SymbolInstance)) {
        std::string bad;
        for (const auto& i : expressionIssues(ex->project, [](std::string_view) { return true; }))
            if (i.severity == S::Error) bad += "\n         " + i.category + " : " + i.property + " : " + i.message;
        check(bad.empty(), "l'exemple des symboles : aucune expression dite impossible" + bad);
    }
}
// ---- fin Lot API 8 : finitions ----

// ---- Lot API 8 : corrections des captures ----
// Le compteur TRS (ProductionCounter) : "En marche" (running), les pieces bonnes
// (good) et les rebuts (bad) sont un TEXTE qui est une expression. Un nom
// inconnu dans "En marche" passait Compiler (la capture PNG_777) : il est dit,
// a sa propriete ; la syntaxe aussi ; le compteur juste ne dit rien.
void compteurTrs() {
    std::printf("Compiler : le compteur TRS (En marche, pi\xC3\xA8" "ces bonnes, rebuts)\n");
    Fixture f = build();
    Project& p = f.p;
    View v = makeView(p, "Vue_Ligne");
    const Id faux = addObject(p, v, Kind::ProductionCounter, "Compteurs_Ligne");
    v.object(faux)->set("good", "Vitesse_Moteur");
    v.object(faux)->set("running", "Marchee");                       // une faute de frappe (Marche)
    const Id illisible = addObject(p, v, Kind::ProductionCounter, "Compteurs_Illisible");
    v.object(illisible)->set("good", "Vitesse_Moteur");
    v.object(illisible)->set("running", "Marche AND (");
    const Id juste = addObject(p, v, Kind::ProductionCounter, "Compteurs_Justes");
    v.object(juste)->set("good", "Pompe1");                           // une variable de l'automate
    v.object(juste)->set("bad", "Lecture");
    v.object(juste)->set("running", "Marche AND Vitesse_Moteur > 0");
    const Id vue = v.id;
    p.views.push_back(std::move(v));
    const auto issues = compileWith(p, plc);
    const auto* marche = find(issues, vue, faux, "running", "Marchee n'existe pas : veux-tu dire Marche ?");
    check(marche != nullptr, "En marche : le nom inconnu, avec le plus proche");
    check(marche && marche->category == "Expression", "... dans la categorie Expression");
    check(find(issues, vue, illisible, "running", "Marche AND (") != nullptr, "En marche : la syntaxe (dite par compile)");
    check(errorsOn(issues, vue, juste) == 0, "le compteur juste : aucune erreur");
    if (errorsOn(issues, vue, juste)) dump(issues);
    const auto sites = expressionIssues(p, plc);
    check(find(sites, vue, faux, "running", "Marchee n'existe pas") != nullptr, "les expressions impossibles : En marche lue aussi");
    std::size_t said = 0;
    for (const auto& i : generateWith(p, plc, GenerateOptions{}))
        if (i.severity == S::Error && i.object == faux && i.message.find("Marchee") != std::string::npos) ++said;
    check(said == 1, "Generer : le nom inconnu n'est dit qu'une fois (" + std::to_string(said) + ")");
}
// ---- fin Lot API 8 : corrections des captures ----

} // namespace

int main() {
    outils();
    compiler();
    generer();
    automate();
    exemples();
    finitions();     // ---- Lot API 8 : finitions ----
    compteurTrs();   // ---- Lot API 8 : corrections des captures ----
    std::printf("%d controle(s), %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
