// =============================================================================
//  tests/styles_types_test.cpp - 1.23.0 : les styles par type d'objet (hmi::styles)
// -----------------------------------------------------------------------------
//  Les styles fournis (verrouilles), appliquer (les valeurs imposees, la valeur
//  d'avant retenue), une expression qui garde la main, briser (reprendre ou
//  garder), retablir, une saisie qui brise, propager un style change, adopter
//  un projet d'avant, un parametre de symbole, l'enregistrement et la relecture,
//  ce que Generer en dit, faire et dupliquer un style.
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiDesign.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiStyles.hpp"
#include "../src/hmi/HmiSymbols.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

namespace st = hmi::styles;
using hmi::Kind;
using hmi::Object;
using hmi::Project;
using hmi::Style;

bool has(const std::vector<std::string>& list, const std::string& k) { return std::find(list.begin(), list.end(), k) != list.end(); }

// Un projet : une vue avec deux pompes et un bouton.
struct Bench {
    Project p;
    hmi::Id view{hmi::kNoId}, pump1{hmi::kNoId}, pump2{hmi::kNoId}, button{hmi::kNoId};
    Bench() {
        hmi::View& v = p.views.emplace_back(hmi::makeView(p, "Vue_Station"));
        view = v.id;
        const hmi::Id layer = v.layers.empty() ? hmi::kNoId : v.layers.front().id;
        pump1 = p.allocate();
        v.objects.push_back(hmi::makeObject(Kind::Pump, pump1, "P1", 100, 100, layer));
        pump2 = p.allocate();
        v.objects.push_back(hmi::makeObject(Kind::Pump, pump2, "P2", 300, 100, layer));
        button = p.allocate();
        v.objects.push_back(hmi::makeObject(Kind::Button, button, "Btn_Marche", 100, 300, layer));
    }
    Object& obj(hmi::Id id) { return *p.view(view)->object(id); }
};

void builtins() {
    std::printf("[1] les styles fournis\n");
    const auto& all = st::builtins();
    check(all.size() == 4, "quatre styles fournis");
    std::vector<std::string> names;
    for (const auto& s : all) names.push_back(s.name);
    check(has(names, "ISA-101") && has(names, "Sombre (d'origine)") && has(names, "Clair") && has(names, "Contraste \xC3\xA9lev\xC3\xA9"),
          "ISA-101, Sombre (d'origine), Clair, Contraste eleve");
    check(std::all_of(all.begin(), all.end(), [](const Style& s) { return s.builtin && !s.targets.empty(); }),
          "tous verrouilles, chacun vise des types");
    const Style* isa = st::builtin("isa-101");
    check(isa && st::target(*isa, "Pump") && st::target(*isa, "Button") && st::target(*isa, "Tank"), "ISA-101 vise Pompe, Bouton, Cuve...");
    // L'ISA-101 : une pompe en marche est pleine (gris fonce), sans vert.
    if (isa)
        if (const auto* t = st::target(*isa, "Pump"))
            for (const auto& pr : t->props)
                if (pr.key == "colorOn") check(pr.value == "#4D4D4D", "ISA-101 : la pompe en marche, gris fonce (pas de vert)");
    // Sombre (d'origine) : les valeurs d'un objet neuf du logiciel.
    if (const Style* dark = st::builtin("Sombre (d'origine)"))
        if (const auto* t = st::target(*dark, "Button")) {
            const Object fresh = hmi::makeObject(Kind::Button, 1, {}, 0, 0, hmi::kNoId);
            bool same = !t->props.empty();
            for (const auto& pr : t->props) same = same && fresh.text(pr.key) == pr.value;
            check(same, "Sombre (d'origine) : les valeurs d'un bouton neuf");
        }
    Project p;
    check(st::find(p, "ISA-101") == isa && st::all(p).size() == 4, "un projet sans style : les fournis se trouvent");
    Style mine;
    mine.id = p.allocate();
    mine.name = "Clair";
    p.styles.push_back(mine);
    check(st::find(p, "clair") == &p.styles.front() && st::all(p).size() == 4, "un style du projet du meme nom cache le fourni");
    check(!st::nameFree(p, "ISA-101") && !st::nameFree(p, "Clair") && st::nameFree(p, "Mon style"), "un nom fourni n'est pas libre");
}

void types() {
    std::printf("[2] les types\n");
    Bench b;
    check(st::typeOf(b.obj(b.pump1)) == "Pump" && st::typeLabel("Pump") == "Pompe" && st::typeLabel("*") == "Tous les objets",
          "le type d'une pompe et son libelle");
    const auto types = st::availableTypes(b.p);
    check(has(types, "Button") && has(types, "Pump") && !has(types, "Group") && !has(types, "SymbolInstance"),
          "les genres de la bibliotheque (sans le groupe ni l'instance generique)");
    const auto props = st::propertiesOf(b.p, "Pump");
    const auto keyIn = [&](const std::string& k) {
        return std::any_of(props.begin(), props.end(), [&](const st::PropChoice& c) { return c.key == k; });
    };
    check(keyIn("colorOn") && keyIn("label") && keyIn("w") && !keyIn("namedStyle") && !keyIn("styleWas"),
          "n'importe quelle propriete du type (pas l'etat du style)");
}

void applyAndBreak() {
    std::printf("[3] appliquer, briser, retablir\n");
    Bench b;
    const Style& isa = *st::builtin("ISA-101");
    Object& p1 = b.obj(b.pump1);
    p1.setExpr("stroke", "SEL(Defaut, '#FF0000', '#000000')");   // une expression garde la main
    const std::size_t changed = st::apply(b.p, p1, isa);
    check(changed >= 4, "des valeurs changees (" + std::to_string(changed) + ")");
    check(p1.text("colorOn") == "#4D4D4D" && p1.text("colorOff") == "#F5F5F5" && st::citedStyle(p1) == "ISA-101", "les valeurs imposees, le style cite");
    check(st::valueBefore(p1, "colorOn").value_or("?") == "#2ECC71", "la valeur d'avant retenue (le vert d'origine)");
    const auto imposed = st::imposedKeys(b.p, p1);
    check(has(imposed, "colorOn") && has(imposed, "textColor") && !has(imposed, "stroke") && !has(imposed, "label"),
          "imposees : celles du style ; pas celle pilotee par une expression, ni une que le style ne porte pas");
    const Style* by = nullptr;
    std::string val;
    check(st::imposes(b.p, p1, "colorOn", &by, &val) && by == &isa && val == "#4D4D4D", "imposes : le style et sa valeur");
    // Briser en reprenant l'ancienne valeur.
    check(st::breakKey(b.p, p1, "colorOn"), "briser colorOn");
    check(p1.text("colorOn") == "#2ECC71" && has(st::brokenKeys(p1), "colorOn") && !st::imposes(b.p, p1, "colorOn"),
          "brisee : l'ancienne valeur revient, le style ne l'impose plus");
    check(!st::breakKey(b.p, p1, "colorOn"), "briser deux fois : non");
    // Retablir.
    check(st::restoreKey(b.p, p1, "colorOn") && p1.text("colorOn") == "#4D4D4D" && st::brokenKeys(p1).empty(), "retablir : la valeur du style");
    // Briser en gardant la valeur actuelle.
    check(st::breakKey(b.p, p1, "colorOff", true) && p1.text("colorOff") == "#F5F5F5" && has(st::brokenKeys(p1), "colorOff"),
          "briser en gardant la valeur actuelle");
    // Une saisie sur une valeur imposee la brise ; la meme valeur que le style, non.
    p1.set("textColor", "#1C1C1C");
    check(!st::edited(b.p, p1, "textColor"), "la meme valeur que le style : toujours imposee");
    p1.set("textColor", "#FFFFFF");
    check(st::edited(b.p, p1, "textColor") && has(st::brokenKeys(p1), "textColor") && p1.text("textColor") == "#FFFFFF",
          "une saisie differente : brisee, la saisie gardee");
    check(st::restoreAll(b.p, p1) == 2 && st::brokenKeys(p1).empty() && p1.text("textColor") == "#1C1C1C", "tout retablir");
    // Un autre style : ses valeurs d'avant restent les vraies.
    const Style& light = *st::builtin("Clair");
    (void)st::apply(b.p, p1, light);
    check(st::valueBefore(p1, "colorOn").value_or("?") == "#2ECC71" && p1.text("colorOn") == "#1C7A3A",
          "un autre style : la valeur d'avant reste celle d'avant le premier");
    // Ne plus citer : les valeurs restent, l'etat s'en va.
    st::detach(p1);
    check(st::citedStyle(p1).empty() && !p1.find("styleWas") && !p1.find("styleBroken") && p1.text("colorOn") == "#1C7A3A",
          "ne plus citer : les valeurs restent, l'etat s'efface");
    // L'ancienne fonction (lot 12) passe par le moteur.
    Object& btn = b.obj(b.button);
    hmi::design::applyStyle(btn, isa);
    check(btn.text("fill") == "#D4D4D4" && st::valueBefore(btn, "fill").value_or("?") == "#2F6FD6", "design::applyStyle : le moteur");
}

void propagateAndAdopt() {
    std::printf("[4] propager, adopter\n");
    Bench b;
    Style mine = st::duplicate(b.p, *st::builtin("ISA-101"), "Mon ISA");
    check(!mine.builtin && mine.name == "Mon ISA" && mine.targets == st::builtin("ISA-101")->targets, "dupliquer a partir de : une copie modifiable");
    b.p.styles.push_back(mine);
    Object& p1 = b.obj(b.pump1);
    Object& p2 = b.obj(b.pump2);
    (void)st::apply(b.p, p1, mine);
    (void)st::apply(b.p, p2, mine);
    (void)st::breakKey(b.p, p2, "colorOff", false);   // P2 garde son gris d'origine
    p1.set("stroke", "#123456");                      // P1 change son contour sans passer par l'inspecteur
    Style after = mine;
    for (auto& pr : st::targetFor(after, "Pump").props) {
        if (pr.key == "colorOn") pr.value = "#333333";
        if (pr.key == "colorOff") pr.value = "#EEEEEE";
        if (pr.key == "stroke") pr.value = "#222222";
    }
    st::targetFor(after, "Pump").props.push_back({"labelPosition", "dessus", {}});
    const std::size_t changed = st::propagate(b.p, mine, after);
    b.p.styles.front() = after;
    check(changed == 2, "deux objets suivent (" + std::to_string(changed) + ")");
    check(p1.text("colorOn") == "#333333" && p2.text("colorOn") == "#333333", "la valeur imposee suit");
    check(p2.text("colorOff") == "#5A6577" && p1.text("colorOff") == "#EEEEEE", "une valeur brisee ne suit pas");
    check(p1.text("stroke") == "#123456" && has(st::brokenKeys(p1), "stroke"), "une valeur changee a la main devient brisee (gardee)");
    check(p1.text("labelPosition") == "dessus" && st::valueBefore(p1, "labelPosition").value_or("?") == "dessous",
          "une cle nouvellement portee : imposee, sa valeur d'avant retenue");
    check(st::users(b.p, "mon isa") == 2, "deux objets citent le style");
    // Adopter un projet d'avant : l'etat manque.
    Bench old;
    Style s;
    s.id = old.p.allocate();
    s.name = "Ancien";
    s.props = {{"colorOn", "#4D4D4D", {}}, {"stroke", "#4D4D4D", {}}};
    old.p.styles.push_back(s);
    Object& o = old.obj(old.pump1);
    o.set("namedStyle", "Ancien");
    o.set("colorOn", "#4D4D4D");     // suit le style
    o.set("stroke", "#AABBCC");      // changee a la main
    check(st::adopt(old.p) == 1, "adopter : un objet");
    check(has(st::brokenKeys(o), "stroke") && !has(st::brokenKeys(o), "colorOn") && st::imposes(old.p, o, "colorOn"),
          "adopter : ce qui differe est brise, le reste impose");
    check(st::adopt(old.p) == 0, "adopter deux fois : rien");
    // Retirer le style.
    check(st::forget(b.p, "Mon ISA") == 2 && b.p.styles.empty() && st::citedStyle(p1).empty() && p1.text("colorOn") == "#333333",
          "retirer : les objets gardent leurs valeurs");
}

void symbolParam() {
    std::printf("[5] un parametre de symbole\n");
    Bench b;
    hmi::View& sym = b.p.views.emplace_back(hmi::makeView(b.p, "Sym_Voyant"));
    sym.role = std::string(hmi::kSymbolRole);
    sym.params.push_back({"Teinte", "'gris'", "", "STRING"});
    hmi::View* v = b.p.view(b.view);
    const hmi::Id inst = b.p.allocate();
    Object io = hmi::makeObject(Kind::SymbolInstance, inst, "V1", 10, 10, v->layers.empty() ? hmi::kNoId : v->layers.front().id);
    io.set("symbol", "Sym_Voyant");
    v->objects.push_back(io);
    Object& o = *v->object(inst);
    check(st::typeOf(o) == "symbole:Sym_Voyant" && st::typeLabel("symbole:Sym_Voyant") == "Symbole Sym_Voyant", "le type d'une instance");
    check(has(st::availableTypes(b.p), "symbole:Sym_Voyant"), "les symboles du projet sont des types");
    const auto props = st::propertiesOf(b.p, "symbole:Sym_Voyant");
    check(std::any_of(props.begin(), props.end(), [](const st::PropChoice& c) { return c.key == "param:Teinte" && c.defaultValue == "'gris'"; }),
          "le parametre du symbole se propose (param:Teinte, sa valeur par defaut)");
    Style s;
    s.id = b.p.allocate();
    s.name = "Voyants";
    st::targetFor(s, "symbole:Sym_Voyant").props.push_back({"param:Teinte", "'rouge'", {}});
    b.p.styles.push_back(s);
    (void)st::apply(b.p, o, s);
    check(st::valueOf(b.p, o, "param:Teinte").value_or("?") == "'rouge'" && st::imposes(b.p, o, "param:Teinte"), "le parametre impose");
    check(st::breakKey(b.p, o, "param:Teinte") && st::valueOf(b.p, o, "param:Teinte").value_or("?").empty(),
          "brise : l'argument d'avant (aucun : la valeur par defaut du symbole)");
    const Object shown = st::previewObject(b.p, s, "symbole:Sym_Voyant");
    check(shown.kind == Kind::SymbolInstance && shown.text("symbol") == "Sym_Voyant" && st::valueOf(b.p, shown, "param:Teinte").value_or("?") == "'rouge'",
          "l'apercu d'un symbole : une instance, le style applique");
}

hmi::Project roundTrip(const Project& p) {
    std::map<std::string, std::string> files;
    for (const auto& f : hmi::serializeProject(p)) files[f.path] = std::string(f.data->begin(), f.data->end());
    auto r = hmi::parseProject([&](const std::string& path, std::string& content) {
        const auto it = files.find(path);
        if (it == files.end()) return false;
        content = it->second;
        return true;
    });
    return r ? std::move(*r) : Project{};
}

void storeAndCheck() {
    std::printf("[6] enregistrer, relire, Generer\n");
    Bench b;
    Style s = st::duplicate(b.p, *st::builtin("ISA-101"), "Mon ISA");
    s.props.push_back({"opacity", "1", {}});   // tous les objets
    b.p.styles.push_back(s);
    Object& p1 = b.obj(b.pump1);
    (void)st::apply(b.p, p1, s);
    (void)st::breakKey(b.p, p1, "colorOff");
    Object& btn = b.obj(b.button);
    (void)st::apply(b.p, btn, *st::builtin("Contraste \xC3\xA9lev\xC3\xA9"));
    const Project q = roundTrip(b.p);
    check(q.styles.size() == 1, "les styles fournis ne sont pas ecrits");
    if (q.styles.size() == 1) {
        const Style& r = q.styles.front();
        check(r.targets == s.targets && r.props == s.props && !r.builtin, "les cibles par type et << tous les objets >> relues");
    }
    const hmi::View* qv = q.view(b.view);
    const Object* q1 = qv ? qv->object(b.pump1) : nullptr;
    check(q1 && has(st::brokenKeys(*q1), "colorOff") && st::valueBefore(*q1, "colorOn").value_or("?") == "#2ECC71" && st::imposes(q, *q1, "colorOn"),
          "l'etat de l'objet relu (brisee, valeur d'avant)");
    const Object* qb = qv ? qv->object(b.button) : nullptr;
    check(qb && st::citedStyle(*qb) == "Contraste \xC3\xA9lev\xC3\xA9" && st::imposes(q, *qb, "fill"), "un objet qui cite un style fourni, relu");
    // Generer : un style fourni cite n'est pas << introuvable >> ; un style mal forme se dit.
    Project bad = b.p;
    Style broken;
    broken.id = bad.allocate();
    broken.name = "ISA-101";
    st::targetFor(broken, "symbole:Inexistant").props.push_back({"fill", "#000000", {}});
    st::targetFor(broken, "Button").props.push_back({"pasUnePropriete", "1", {}});
    bad.styles.push_back(broken);
    const auto issues = hmi::generate(bad, [](std::string_view) { return false; });
    const auto said = [&](const std::string& what) {
        return std::any_of(issues.begin(), issues.end(), [&](const hmi::Issue& i) { return i.message.find(what) != std::string::npos; });
    };
    check(said("type inconnu") && said("n'existe pas pour ce type") && said("nom d'un style fourni"), "Generer : type inconnu, propriete absente, nom fourni");
    const auto clean = hmi::generate(b.p, [](std::string_view) { return false; });
    check(std::none_of(clean.begin(), clean.end(), [](const hmi::Issue& i) { return i.message.find("introuvable (IHM > Styles)") != std::string::npos; }),
          "Generer : un objet qui cite un style fourni n'est pas en defaut");
}

void makeAndPreview() {
    std::printf("[7] faire un style, l'apercu\n");
    Bench b;
    Object& btn = b.obj(b.button);
    btn.set("fill", "#112233");
    const Style s = st::fromObject(b.p, btn, "Bouton bleu");
    const auto* t = st::target(s, "Button");
    check(t && !t->props.empty() && s.props.empty() && s.name == "Bouton bleu", "fait d'un bouton : la cible Bouton");
    bool fill = false;
    if (t)
        for (const auto& pr : t->props) fill = fill || (pr.key == "fill" && pr.value == "#112233");
    check(fill, "avec ses valeurs d'apparence");
    const Object prev = st::previewObject(b.p, *st::builtin("ISA-101"), "Pump");
    check(prev.kind == Kind::Pump && prev.text("colorOn") == "#4D4D4D", "l'apercu d'une pompe en ISA-101");
    check(st::previewObject(b.p, s, "PasUnType").props.empty(), "un type inconnu : rien");
    check(st::problems(b.p, s).empty(), "un style bien forme : aucun probleme");
}

} // namespace

int main() {
    std::printf("[1.23.0 : les styles par type d'objet]\n");
    builtins();
    types();
    applyAndBreak();
    propagateAndAdopt();
    symbolParam();
    storeAndCheck();
    makeAndPreview();
    std::printf("styles_types_test : %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
