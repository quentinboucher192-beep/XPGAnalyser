// =============================================================================
//  tests/semantic_colors_test.cpp - 1.23.1 : les couleurs semantiques (@Nom)
// -----------------------------------------------------------------------------
//  Les fournies (verrouillees, ISA-101), celles du projet, citer << @Nom >> dans
//  une case, une formule ('@Alarme'), un style, le fond d'une vue ; le registre
//  publie que lisent ceux qui dessinent (et les fonctions de couleur) ; compter,
//  renommer, retirer (la couleur reste ecrite en dur) ; dupliquer ; enregistrer
//  et relire ; ce que Generer en dit ; un paquet de vues les emporte.
// =============================================================================
#include "../src/core/SemanticColors.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiNatives.hpp"
#include "../src/hmi/HmiPackage.hpp"
#include "../src/hmi/HmiQuality.hpp"
#include "../src/hmi/HmiRoles.hpp"
#include "../src/hmi/HmiSemanticColors.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiStyles.hpp"

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

namespace sem = hmi::semantic;
using hmi::Kind;
using hmi::Object;
using hmi::Project;
using hmi::SemanticColor;

struct Bench {
    Project p;
    hmi::Id view{hmi::kNoId}, pump{hmi::kNoId}, text{hmi::kNoId};
    Bench() {
        hmi::View& v = p.views.emplace_back(hmi::makeView(p, "Vue_Station"));
        view = v.id;
        const hmi::Id layer = v.layers.empty() ? hmi::kNoId : v.layers.front().id;
        pump = p.allocate();
        v.objects.push_back(hmi::makeObject(Kind::Pump, pump, "P1", 100, 100, layer));
        text = p.allocate();
        v.objects.push_back(hmi::makeObject(Kind::Text, text, "T1", 100, 300, layer));
    }
    Object& obj(hmi::Id id) { return *p.view(view)->object(id); }
    SemanticColor& add(const std::string& name, const std::string& value) {
        SemanticColor c;
        c.id = p.allocate();
        c.name = name;
        c.value = value;
        return p.semanticColors.emplace_back(std::move(c));
    }
};

Project roundTrip(const Project& p) {
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

bool said(const std::vector<hmi::Issue>& issues, const std::string& what) {
    return std::any_of(issues.begin(), issues.end(), [&](const hmi::Issue& i) { return i.message.find(what) != std::string::npos; });
}

void builtins() {
    std::printf("[1] les fournies\n");
    const auto& all = sem::builtins();
    check(all.size() == 12, "douze couleurs fournies");
    check(std::all_of(all.begin(), all.end(), [](const SemanticColor& c) { return c.builtin && c.value.size() == 7 && c.value[0] == '#'; }),
          "verrouillees, chacune une couleur #RRGGBB");
    const auto* marche = sem::builtin("Marche");
    check(marche && marche->value == "#4D4D4D", "ISA-101 : la marche en gris fonce (pas de vert)");
    check(sem::builtin("@alarme critique") && sem::builtin("ALARME")->value == "#F57C00", "sans casse, avec ou sans @");
    check(sem::builtin("D\xC3\xA9" "faut") && sem::builtin("\xC3\x89quipement") && sem::builtin("D\xC3\xA9sactiv\xC3\xA9"), "les noms accentues");
    Project p;
    check(sem::all(p).size() == 12 && sem::find(p, "@Fond") == sem::builtin("Fond"), "un projet neuf : les fournies");
}

void projectAndPublish() {
    std::printf("[2] celles du projet, le registre publie\n");
    Bench b;
    b.add("Eau", "#2571B7");
    b.add("Eau froide", "@Eau");   // une couleur qui en cite une autre
    check(sem::all(b.p).size() == 14 && sem::all(b.p).front()->name == "Eau", "celles du projet, puis les fournies");
    check(sem::resolve(b.p, "@eau froide") == "#2571B7" && sem::resolve(b.p, "#ABCDEF") == "#ABCDEF", "resoudre : la chaine suivie ; une couleur telle quelle");
    check(sem::resolve(b.p, "@Inconnue") == "@Inconnue", "une inconnue reste telle quelle");
    sem::publish(b.p);
    check(core::semantic::count() == 14, "le registre : 14 couleurs");
    check(core::semantic::resolve(" @Alarme ") == "#F57C00" && core::semantic::resolve("@EAU FROIDE") == "#2571B7", "le registre lit sans casse");
    check(core::semantic::lookup("Absente") == std::nullopt, "une absente : rien");
    // Ceux qui lisent une couleur : le controle de saisie, la qualite (contraste), les fonctions de couleur.
    check(hmi::roles::colorOk("@Alarme") && hmi::roles::colorOk("@Nimporte") && !hmi::roles::colorOk("@a'b"), "une case couleur accepte << @Nom >>");
    const auto rgb = hmi::parseRgb("@Eau");
    check(rgb && *rgb == 0x2571B7u, "la qualite (contraste) lit @Eau");
    std::uint8_t r = 0, g = 0, bl = 0, a = 0;
    check(hmi::natives::parseColor("@Alarme critique", r, g, bl, a) && r == 0xD3 && g == 0x2F && bl == 0x2F && a == 255,
          "les fonctions de couleur lisent '@Alarme critique'");
    check(!hmi::natives::parseColor("@Inconnue", r, g, bl, a), "une inconnue : illisible");
}

void usersRenameForget() {
    std::printf("[3] citer, compter, renommer, retirer\n");
    Bench b;
    b.add("Eau", "#2571B7");
    b.add("Eau froide", "@Eau");
    b.obj(b.pump).set("colorOn", "@Eau");
    b.obj(b.pump).setExpr("colorFault", "SEL(Defaut_P1, '@Alarme', '@eau')");
    b.obj(b.text).set("textColor", " @Eau ");
    b.p.view(b.view)->background = "@Fond";
    hmi::Style s;
    s.id = b.p.allocate();
    s.name = "Bleu";
    hmi::styles::targetFor(s, "Pump").props.push_back({"stroke", "@Eau", {}});
    b.p.styles.push_back(s);
    check(sem::users(b.p, "Eau") == 5, "@Eau : une valeur, une formule, un texte, un style, une couleur (5)");
    check(sem::users(b.p, "@Alarme") == 1 && sem::users(b.p, "Fond") == 1, "@Alarme dans la formule ; @Fond au fond de la vue");
    // Renommer : chaque citation suit.
    check(sem::renameReferences(b.p, "Eau", "Eau de ville") == 5, "renommer : 5 citations");
    b.p.semanticColors.front().name = "Eau de ville";
    check(b.obj(b.pump).text("colorOn") == "@Eau de ville" && b.obj(b.pump).expr("colorFault") == "SEL(Defaut_P1, '@Alarme', '@Eau de ville')",
          "la valeur et la formule suivent");
    check(b.p.styles.front().targets.front().props.front().value == "@Eau de ville" && b.p.semanticColors[1].value == "@Eau de ville",
          "le style et la couleur qui la citait suivent");
    check(sem::users(b.p, "Eau") == 0 && sem::users(b.p, "Eau de ville") == 5, "plus rien ne cite l'ancien nom");
    // Retirer : ce qui la cite garde sa couleur, ecrite en dur.
    check(sem::forget(b.p, "Eau de ville") == 5, "retirer : 5 citations remplacees");
    check(b.obj(b.pump).text("colorOn") == "#2571B7" && b.obj(b.pump).expr("colorFault") == "SEL(Defaut_P1, '@Alarme', '#2571B7')",
          "la couleur reste a l'ecran (en dur)");
    check(b.p.semanticColors[1].value == "#2571B7" && sem::users(b.p, "Eau de ville") == 0, "plus aucune citation");
    check(sem::forget(b.p, "Alarme") == 0 && b.obj(b.pump).expr("colorFault").find("@Alarme") != std::string::npos, "une fournie ne se retire pas");
}

void namesAndDuplicate() {
    std::printf("[4] les noms, dupliquer\n");
    Bench b;
    check(!sem::nameFree(b.p, "Alarme") && !sem::nameFree(b.p, "@marche") && !sem::nameFree(b.p, ""), "le nom d'une fournie n'est pas libre");
    const auto& eau = b.add("Eau", "#2571B7");
    check(!sem::nameFree(b.p, "eau") && sem::nameFree(b.p, "eau", eau.id) && sem::nameFree(b.p, "Vapeur"), "un nom du projet, sauf la couleur elle-meme");
    check(sem::uniqueName(b.p, "Eau") == "Eau 2" && sem::uniqueName(b.p, "Alarme") == "Alarme 2" && sem::uniqueName(b.p, "") == "Couleur",
          "un nom libre");
    const SemanticColor copy = sem::duplicate(b.p, *sem::builtin("Alarme"));
    check(!copy.builtin && copy.id != hmi::kNoId && copy.name == "Alarme (copie)" && copy.value == "#F57C00", "dupliquer une fournie : une du projet");
    check(copy.description.find("couleur fournie") != std::string::npos, "sa description dit d'ou elle vient");
    check(sem::reference("Alarme") == "@Alarme" && sem::reference("@Alarme") == "@Alarme", "la citation d'un nom");
}

void storeAndCheck() {
    std::printf("[5] enregistrer, relire, Generer\n");
    Bench b;
    auto& eau = b.add("Eau", "#2571B7");
    eau.description = "Les conduites d'eau";
    eau.folder = "Fluides";
    b.obj(b.pump).set("colorOn", "@Eau");
    const Project q = roundTrip(b.p);
    check(q.semanticColors.size() == 1, "les fournies ne sont pas ecrites");
    if (!q.semanticColors.empty()) {
        const auto& r = q.semanticColors.front();
        check(r.id == eau.id && r.name == "Eau" && r.value == "#2571B7" && r.description == eau.description && r.folder == "Fluides" && !r.builtin,
              "relue a l'identique");
    }
    check(q.nextId > eau.id, "le prochain identifiant passe la couleur");
    const hmi::View* qv = q.view(b.view);
    check(qv && qv->object(b.pump) && qv->object(b.pump)->text("colorOn") == "@Eau", "la citation relue");
    // Generer : une citation inconnue, une couleur illisible, le nom d'une fournie, une boucle.
    Project bad = b.p;
    bad.view(b.view)->object(b.text)->set("textColor", "@Absente");
    bad.view(b.view)->object(b.pump)->setExpr("colorFault", "SEL(X, '@Fantome', '@Alarme')");
    SemanticColor ill;
    ill.id = bad.allocate();
    ill.name = "Rouge vif";
    ill.value = "rouge";
    bad.semanticColors.push_back(ill);
    SemanticColor clash;
    clash.id = bad.allocate();
    clash.name = "Alarme";
    clash.value = "#FF0000";
    bad.semanticColors.push_back(clash);
    SemanticColor loopA, loopB;
    loopA.id = bad.allocate();
    loopA.name = "A";
    loopA.value = "@B";
    loopB.id = bad.allocate();
    loopB.name = "B";
    loopB.value = "@A";
    bad.semanticColors.push_back(loopA);
    bad.semanticColors.push_back(loopB);
    const auto issues = hmi::generate(bad, [](std::string_view) { return false; });
    check(said(issues, "cite @Absente : couleur s\xC3\xA9mantique inconnue"), "Generer : une case cite une inconnue");
    check(said(issues, "cite @Fantome : couleur s\xC3\xA9mantique inconnue"), "Generer : une formule cite une inconnue");
    check(said(issues, "couleur illisible"), "Generer : une couleur illisible");
    check(said(issues, "nom d'une couleur fournie"), "Generer : le nom d'une fournie");
    check(said(issues, "tournent en rond"), "Generer : une boucle");
    const auto located = std::find_if(issues.begin(), issues.end(), [](const hmi::Issue& i) { return i.message.find("@Absente") != std::string::npos; });
    check(located != issues.end() && located->view == b.view && located->object == b.text, "la citation inconnue mene a l'objet");
    const auto clean = hmi::generate(b.p, [](std::string_view) { return false; });
    check(!said(clean, "couleur s\xC3\xA9mantique inconnue") && !said(clean, "couleur illisible"), "Generer : un projet propre ne dit rien");
}

void package() {
    std::printf("[6] un paquet de vues les emporte\n");
    Bench b;
    b.add("Eau", "#2571B7");
    b.add("Eau froide", "@Eau");
    b.add("Vapeur", "#DDDDDD");   // citee par personne
    b.obj(b.pump).set("colorOn", "@Eau froide");
    const auto pkg = hmi::pkg::collect(b.p, {b.view});
    const auto& c = pkg.content.semanticColors;
    const auto named = [&](const std::string& n) {
        return std::any_of(c.begin(), c.end(), [&](const SemanticColor& x) { return x.name == n; });
    };
    check(named("Eau froide") && named("Eau") && !named("Vapeur"), "les citees (et celles qu'elles citent), pas les autres");
    Project other;
    hmi::pkg::Plan plan = hmi::pkg::plan(other, pkg);
    const auto res = hmi::pkg::importInto(other, pkg, plan);
    check(sem::find(other, "Eau froide") && sem::resolve(other, "@Eau froide") == "#2571B7", "importees dans un autre projet");
    check(std::any_of(res.added.begin(), res.added.end(), [](const std::string& s) { return s == "couleur @Eau"; }), "le compte rendu les dit ajoutees");
}

} // namespace

int main() {
    std::printf("[1.23.1 : les couleurs semantiques]\n");
    builtins();
    projectAndPublish();
    usersRenameForget();
    namesAndDuplicate();
    storeAndCheck();
    package();
    std::printf("semantic_colors_test : %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
