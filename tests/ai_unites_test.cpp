// =============================================================================
//  tests/ai_unites_test.cpp - 1.22.8 : L'ASSISTANT IA CREE DES UNITES DE MESURE
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un dossier IA/ jetable :
//    1. les controles partages (l'editeur et l'IA) et la table des unites
//       courantes (mmCE, kW, %HR...) : le symbole pris (fourni, du projet), le
//       facteur nul, la famille neuve, la suggestion, la coherence ;
//    2. creer_unite : valide (mmCE, pression, 9.80665), puis une variable qui
//       l'emploie dans la MEME proposition (ecrite avant elle : le plan la pose
//       d'abord) - un seul Ctrl+Z ; un symbole deja pris, un facteur nul, une
//       famille inconnue : refuses ; un facteur loin de l'habituel : un
//       avertissement ; l'apercu montre la conversion ; le JSON garde le facteur
//       exact ; modifier_unite ;
//    3. le contexte (les unites du projet) et la regle 25 (les consignes, le schema) ;
//    4. la correction rapide sur << unite inconnue << mmCE >> >> (le cas de la
//       capture, trois variables) : une seule unite creee, les erreurs parties, la
//       fenetre ouverte, Revenir ; Tout corriger rapidement : une seule unite ;
//       << xyz >> : grisee avec la raison ; << Creer l'unite... >> pre-rempli pour
//       mmCE (la petite fenetre, Ajouter), vide pour xyz.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiContext.hpp"
#include "../src/app/ai/AiFixes.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPlan.hpp"
#include "../src/app/ai/AiPreviewDialog.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/screens/Screens.hpp"   // FormDialog : << Creer l'unite... >>
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiUnits.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace fx = app::ai::fixes;
namespace hu = hmi::units;
using A = ai::Operation::Action;
using K = fx::Action::Kind;

namespace {

int g_ok = 0, g_fail = 0;
std::string g_clipboard;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
bool anyHas(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return has(l, part); });
}
int indexHaving(const std::vector<std::string>& lines, const std::string& part) {
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (has(lines[i], part)) return static_cast<int>(i);
    return -1;
}
std::string joinAll(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += x + " | ";
    return s;
}
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const std::string kO = "\xC2\xAB ";   // << et son espace
const std::string kF = " \xC2\xBB";   // >>
std::string q(const std::string& s) { return kO + s + kF; }
const std::string kQuick = "Corriger rapidement";
const std::string kUnknown = "unit\xC3\xA9 inconnue " + q("mmCE");
const std::string kConversion = "Unit\xC3\xA9 mmCE (pression) : 1 mmCE = 9,80665 Pa";
const std::string kAsk = "Cr\xC3\xA9" "er l'unit\xC3\xA9 " + q("mmCE") + "\xE2\x80\xA6";

// Le projet de la capture : des mesures et des consignes en REAL, sans unite ; une unite du projet (inH2O) ; une vue.
hmi::Id seedProject(hmi::Project& p) {
    p.config.width = 1280;
    p.config.height = 800;
    p.views = {hmi::makeView(p, "Vue_Station")};
    for (const char* name : {"VE3210_Mesure", "VE3211_Mesure", "VE260_Mesure", "VE3210_Consigne"}) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = "REAL";
        v.initial = "0";
        p.programs.variables.push_back(v);
    }
    hmi::Variable b;
    b.id = p.allocate();
    b.name = "Pompe_Marche";
    b.type = "BOOL";
    b.initial = "FALSE";
    p.programs.variables.push_back(b);
    p.config.units.push_back({"inH2O", "pression", 249.08891, 0.0, "pouce d'eau"});
    return p.views[0].id;
}

ai::Operation op(A a, std::string name, std::vector<std::pair<std::string, std::string>> props = {}) {
    ai::Operation o;
    o.action = a;
    o.name = std::move(name);
    o.props = std::move(props);
    return o;
}
ai::Operation unit(const std::string& symbol, const std::string& family, const std::string& factor, std::vector<std::pair<std::string, std::string>> more = {}) {
    ai::Operation o = op(A::Unit, symbol, {{"famille", family}});
    if (!factor.empty()) o.props.emplace_back("facteur", factor);
    for (auto& kv : more) o.props.push_back(std::move(kv));
    return o;
}
ai::Operation withUnit(const std::string& variable, const std::string& symbol) {
    ai::Operation o;
    o.action = A::VarEdit;
    o.variable = variable;
    o.props = {{"unite", symbol}};
    return o;
}

struct Engine {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    hmi::Id            view{hmi::kNoId};
    Engine() { view = seedProject(doc->project); }
    [[nodiscard]] ai::Proposal proposal(std::vector<ai::Operation> ops) const {
        ai::Proposal p;
        p.id = "op-unites";
        p.request = "les unites";
        p.summary = "Unites";
        p.ops = std::move(ops);
        ai::scopeProposal(p, doc->project.view(view));
        p.fingerprint = ai::proposalPrint(doc->project, p);
        return p;
    }
    bool apply(const ai::Proposal& p, std::string* why = nullptr) {
        auto cmd = ai::makeCommand(doc, p, why);
        if (!cmd) return false;
        return static_cast<bool>(stack.push(std::move(cmd)));
    }
};

// ---- 1. les controles partages et la table ------------------------------------------------------------------------------
void testShared() {
    std::printf("[controles partages, table des unites courantes]\n");
    hmi::Project p;
    (void)seedProject(p);
    const hu::Catalog cat(p.config);
    std::string fam;
    const std::string bar = hu::newUnitProblem(cat, {"bar", "pression", 2.0, 0.0, ""});
    check(has(bar, "existe d\xC3\xA9j\xC3\xA0") && has(bar, "fournie"), "un symbole fourni : refuse, la raison le dit (" + bar + ")");
    const std::string inh = hu::newUnitProblem(cat, {"INH2O", "pression", 249.0, 0.0, ""});
    check(has(inh, "existe d\xC3\xA9j\xC3\xA0") && has(inh, "du projet"), "un symbole du projet (sans la casse) : refuse (" + inh + ")");
    check(has(hu::newUnitProblem(cat, {"mmCE", "pression", 0.0, 0.0, ""}), "non nul"), "un facteur nul : refuse");
    check(has(hu::newUnitProblem(cat, {"mmCE", "pressure", 9.8, 0.0, ""}), "famille inconnue"), "une famille inconnue : refusee");
    check(hu::newUnitProblem(cat, {"mmCE", "Pression", 9.80665, 0.0, ""}, &fam).empty() && fam == "pression", "mmCE : bonne (la famille par son libelle)");
    check(has(hu::newFamilyProblem(cat, "puissance", "bar"), "d\xC3\xA9j\xC3\xA0 une unit\xC3\xA9"), "une famille neuve dont la reference existe : refusee");
    check(hu::newFamilyProblem(cat, "puissance", "W").empty(), "une famille neuve (puissance, W) : bonne");
    check(has(hu::unitChangeProblem(p.config, "bar", 2.0, 0.0), "verrouill"), "modifier une fournie : refuse");
    check(hu::unitChangeProblem(p.config, "inH2O", 249.1, 0.0).empty(), "modifier une du projet : permis");

    const auto mm = hu::suggestUnit(cat, "mmce");
    check(mm && mm->unit.symbol == "mmCE" && mm->unit.family == "pression" && mm->unit.factor == 9.80665 && mm->family.name.empty(),
          "la table : mmCE, pression, 9.80665 (sans la casse)");
    const auto kw = hu::suggestUnit(cat, "kW");
    check(kw && kw->unit.family == "puissance" && kw->unit.factor == 1000.0 && kw->family.name == "puissance" && kw->family.reference == "W",
          "la table : kW, une famille neuve puissance (reference W)");
    const auto hr = hu::suggestUnit(cat, "%HR");
    check(hr && hr->family.reference == "%HR" && hr->unit.factor == 1.0, "la table : %HR, la reference de sa famille neuve");
    check(!hu::suggestUnit(cat, "xyz"), "xyz : inconnu de la table");
    check(!hu::suggestUnit(cat, "bar"), "bar : deja dans le catalogue, rien a suggerer");
    const std::string far = hu::coherenceWarning(cat, {"mmCE", "pression", 10.0, 0.0, ""});
    check(has(far, "9,80665") && has(far, "v\xC3\xA9rifie"), "un facteur loin de l'habituel : un avertissement (" + far + ")");
    check(hu::coherenceWarning(cat, {"mmCE", "pression", 9.80665, 0.0, ""}).empty(), "le facteur habituel : rien a dire");
    check(has(hu::coherenceWarning(cat, {"mmCE", "longueur", 0.001, 0.0, ""}), "famille"), "une autre famille : un avertissement");
    check(hu::coherenceWarning(cat, {"xyz", "pression", 3.0, 0.0, ""}).empty(), "un symbole inconnu de la table : rien a verifier");
    check(hu::exactNumber(9.80665) == "9.80665" && hu::exactNumber(1.0 / 3.6) != "0.278", "le facteur garde tous ses chiffres");
}

// ---- 2. creer_unite ------------------------------------------------------------------------------------------------------
void testOperation() {
    std::printf("[creer_unite, modifier_unite]\n");
    Engine e;
    // LE CAS DE LA CAPTURE : une variable en mmCE, l'unite ecrite APRES elle dans la proposition.
    ai::Operation vars;
    vars.action = A::Variables;
    vars.variables.push_back({"VE3300_Mesure", "REAL", "Pression du ventilateur", "0"});
    vars.variables.back().unit = "mmCE";
    const ai::Proposal p = e.proposal({vars, withUnit("VE3210_Mesure", "mmCE"), unit("mmCE", "pression", "9.80665", {{"libelle", "millim\xC3\xA8tre de colonne d'eau"}})});
    const ai::Check ck = ai::validate(e.doc->project, p);
    check(ck.ok(), "creer_unite puis une variable qui l'emploie, dans la meme proposition : valide (" + joinAll(ck.errors) + ")");
    const auto pos = [&](std::size_t op) { return std::find(ck.order.begin(), ck.order.end(), op) - ck.order.begin(); };
    check(pos(2) < pos(0) && pos(2) < pos(1), "le plan pose l'unite AVANT les variables qui l'emploient");
    const bool shown = std::any_of(ck.made.begin(), ck.made.end(), [](const ai::Check::Made& m) { return m.what == "Unit\xC3\xA9" && m.name == "mmCE" && m.detail == kConversion; });
    check(shown, "l'apercu montre la conversion : " + kConversion);
    check(!ck.newVariables.empty() && ck.newVariables.front().unit == "mmCE", "la variable a creer porte son unite");
    const ai::plan::Plan pl = ai::plan::build(e.doc->project, p, ck);
    const ai::plan::Step* st = pl.byOp(2);
    check(st && st->category == ai::plan::Category::Types && has(st->title, "l'unit\xC3\xA9 mmCE"), "le plan : << Creer l'unite mmCE >> (types et variables)");
    const std::size_t steps = e.stack.done().size();
    std::string why;
    check(e.apply(p, &why) && e.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    const hu::Catalog after(e.doc->project.config);
    const hmi::Variable* v300 = e.doc->project.variable("VE3300_Mesure");
    const hmi::Variable* v3210 = e.doc->project.variable("VE3210_Mesure");
    check(after.unit("mmCE") && after.unit("mmCE")->factor == 9.80665 && v300 && v300->unit == "mmCE" && v3210 && v3210->unit == "mmCE",
          "l'unite est dans le projet, les deux variables l'emploient");
    check(hu::convertValue(after, 1000.0, "mmCE", "Pa").ok && std::abs(hu::convertValue(after, 1000.0, "mmCE", "Pa").result - 9806.65) < 1e-6,
          "1000 mmCE = 9806,65 Pa");
    (void)e.stack.undo();
    check(!hu::Catalog(e.doc->project.config).unit("mmCE") && !e.doc->project.variable("VE3300_Mesure") && e.doc->project.variable("VE3210_Mesure")->unit.empty(),
          "un seul Ctrl+Z reprend tout (l'unite, la variable, l'unite de VE3210_Mesure)");

    // LES REFUS : un symbole deja pris (fourni, du projet), un facteur nul, une famille inconnue, un facteur absent.
    const auto errorsOf = [&](std::vector<ai::Operation> ops) { return joinAll(ai::validate(e.doc->project, e.proposal(std::move(ops))).errors); };
    const std::string taken = errorsOf({unit("bar", "pression", "2")});
    check(has(taken, "existe d\xC3\xA9j\xC3\xA0") && has(taken, "fournie"), "un symbole fourni deja pris : refuse (" + taken + ")");
    const std::string mine = errorsOf({unit("inH2O", "pression", "250")});
    check(has(mine, "existe d\xC3\xA9j\xC3\xA0") && has(mine, "du projet"), "un symbole du projet deja pris : refuse (" + mine + ")");
    check(has(errorsOf({unit("mmCE", "pression", "0")}), "non nul"), "un facteur nul : refuse");
    check(has(errorsOf({unit("mmCE", "pression", "")}), "facteur manque"), "un facteur absent : refuse (jamais 1 d'office)");
    const std::string nofam = errorsOf({unit("Wx", "puissance", "1000")});
    check(has(nofam, "inconnue") && has(nofam, "reference"), "une famille inconnue sans reference : refusee, la raison dit comment (" + nofam + ")");
    {   // l'identique : reutilisee, rien a poser
        const ai::Check same = ai::validate(e.doc->project, e.proposal({unit("bar", "pression", "100000")}));
        check(same.ok() && !same.reused.empty() && same.made.empty(), "bar a l'identique : reutilisee (" + joinAll(same.errors) + ")");
    }
    {   // un facteur loin de l'habituel : un avertissement, pas un refus
        const ai::Check far = ai::validate(e.doc->project, e.proposal({unit("mmCE", "pression", "10")}));
        check(far.ok() && anyHas(far.warnings, "9,80665"), "mmCE a 10 Pa : valide, avec un avertissement (" + joinAll(far.warnings) + ")");
    }
    {   // UNE FAMILLE NEUVE dite par sa reference : kW (puissance, W)
        const ai::Proposal kw = e.proposal({unit("kW", "puissance", "1000", {{"reference", "W"}, {"libelle_famille", "Puissance"}})});
        const ai::Check c = ai::validate(e.doc->project, kw);
        check(c.ok() && std::any_of(c.made.begin(), c.made.end(), [](const ai::Check::Made& m) { return m.name == "puissance"; }),
              "kW, famille neuve puissance (reference W) : valide, la famille se montre (" + joinAll(c.errors) + ")");
        check(e.apply(kw, &why), "appliquee");
        const hu::Catalog cat(e.doc->project.config);
        check(cat.family("puissance") && cat.unit("W") && cat.unit("kW") && cat.unit("kW")->factor == 1000.0, "la famille, sa reference W, kW");
        (void)e.stack.undo();
    }
    // LE JSON : le facteur exact (un nombre JSON), le symbole au premier niveau ; creer_variables + unite.
    {
        ai::json::Value arr;
        check(ai::json::parse("[{\"action\": \"creer_unite\", \"symbole\": \"mmCE\", \"famille\": \"pression\", \"facteur\": 9.80665, \"libelle\": \"x\"},"
                              " {\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"VE9\", \"type\": \"REAL\", \"unite\": \"mmCE\"}]}]",
                              arr),
              "le JSON se lit");
        std::vector<ai::Operation> ops;
        std::string bad;
        check(ai::operationsFromJson(arr, ops, &bad) && ops.size() == 2 && ops[0].action == A::Unit && ops[0].name == "mmCE", "creer_unite lue (" + bad + ")");
        const auto fac = std::find_if(ops[0].props.begin(), ops[0].props.end(), [](const auto& kv) { return kv.first == "facteur"; });
        check(fac != ops[0].props.end() && fac->second == "9.80665", "le facteur garde tous ses chiffres (9.80665, pas 9.807)");
        check(std::none_of(ops[0].props.begin(), ops[0].props.end(), [](const auto& kv) { return kv.first == "symbole"; }), "le symbole n'est pas une propriete");
        check(ops[1].variables.size() == 1 && ops[1].variables.front().unit == "mmCE", "creer_variables : l'unite de la variable");
        std::vector<ai::Operation> again;
        check(ai::operationsFromJson(ai::operationsToJson(ops), again) && again == ops, "aller et retour identiques");
        ai::PromptInput in;
        in.project = &e.doc->project;
        in.view = e.doc->project.view(e.view);
        const auto prop = ai::proposalFromTool("{\"resume\": \"u\", \"operations\": [{\"action\": \"creer_unite\", \"symbole\": \"mmCE\", \"famille\": "
                                               "\"pression\", \"facteur\": 9.80665}, {\"action\": \"modifier_variable\", \"variable\": \"VE3210_Mesure\", "
                                               "\"proprietes\": {\"unite\": \"mmCE\"}}]}",
                                               in, "unites", &bad);
        check(prop.has_value() && ai::validate(e.doc->project, *prop).ok(), "l'entree de l'outil : lue et valide (" + bad + ")");
        check(!ai::proposalFromTool("{\"operations\": [{\"action\": \"creer_unite\", \"famille\": \"pression\"}]}", in, "u", &bad) && has(bad, "symbole"),
              "creer_unite sans symbole : refusee a la lecture");
    }
    // MODIFIER_UNITE : une du projet (inH2O) ; une fournie, verrouillee.
    {
        const ai::Proposal mod = e.proposal({op(A::UnitEdit, "inH2O", {{"facteur", "249.1"}})});
        const ai::Check c = ai::validate(e.doc->project, mod);
        check(c.ok(), "modifier_unite inH2O : valide (" + joinAll(c.errors) + ")");
        check(e.apply(mod, &why) && hu::Catalog(e.doc->project.config).unit("inH2O")->factor == 249.1, "inH2O : 249,1 Pa");
        (void)e.stack.undo();
        check(has(joinAll(ai::validate(e.doc->project, e.proposal({op(A::UnitEdit, "bar", {{"facteur", "2"}})})).errors), "verrouill"),
              "modifier_unite bar : refusee (fournie)");
    }
    // UNE UNITE INCONNUE : le defaut structure (le clic droit la cree).
    {
        const ai::Check c = ai::validate(e.doc->project, e.proposal({withUnit("VE3210_Mesure", "mmCE"), withUnit("VE3211_Mesure", "mmCE")}));
        const auto units = std::count_if(c.faults.begin(), c.faults.end(), [](const ai::Check::Fault& f) { return f.kind == "unite" && f.name == "mmCE"; });
        check(c.errors.size() == 2 && units == 2 && has(c.errors.front(), kUnknown), "deux erreurs << unite inconnue >>, leurs defauts << unite >>");
        const ai::Check boolUnit = ai::validate(e.doc->project, e.proposal({withUnit("Pompe_Marche", "mmCE")}));
        check(!boolUnit.ok() && std::none_of(boolUnit.faults.begin(), boolUnit.faults.end(), [](const ai::Check::Fault& f) { return f.kind == "unite"; }),
              "une unite sur un BOOL : une erreur, pas une unite a creer");
    }
}

// ---- 3. le contexte et la regle 25 ---------------------------------------------------------------------------------------
void testPrompt() {
    std::printf("[contexte, regle 25]\n");
    Engine e;
    ai::PromptInput in;
    in.project = &e.doc->project;
    in.view = e.doc->project.view(e.view);
    in.request = "Mets les mesures en mmCE";
    const std::string sys = ai::systemPrompt(in);
    check(has(sys, "25. Une unit\xC3\xA9 qui manque se cr\xC3\xA9" "e (creer_unite") && has(sys, "facteur EXACT (1 mmCE = 9.80665 Pa)"),
          "la regle 25 : creer, le facteur exact");
    check(has(sys, "pression (Pa), temperature (K), debit (m3/h)") && has(sys, "unit\xC3\xA9s courantes existent (bar, mbar, degC"),
          "les familles fournies et leur reference ; les unites courantes existent");
    check(has(sys, "jamais pour une grandeur inconnue") && has(sys, "modifier_unite : symbole"), "jamais une unite inventee ; modifier_unite");
    check(sys.find("24. ") == std::string::npos || sys.find("24. ") < sys.find("25. Une unit"), "la regle 25 apres la 24");
    const std::string ctx = ai::contextText(in);
    check(has(ctx, "Unit\xC3\xA9s du projet : inH2O (pression : 1 inH2O = 249,089 Pa)"), "le contexte : les unites du projet et leur conversion");
    hmi::Project bare;
    (void)hmi::makeView(bare, "Vue");
    ai::PromptInput none;
    none.project = &bare;
    check(!has(ai::contextText(none), "Unit\xC3\xA9s du projet"), "un projet sans unite a lui : rien de plus");
    ai::json::Value tools;
    check(ai::json::parse(ai::operationsTool(), tools), "le schema se lit");
    const auto& props = tools.items().front().at("input_schema").at("properties").at("operations").at("items").at("properties");
    check(props.has("symbole") && props.at("variables").at("items").at("properties").has("unite"), "le schema : le symbole ; l'unite d'une variable");
    bool listed = false;
    for (const auto& a : props.at("action").at("enum").items()) listed = listed || a.asString() == "creer_unite";
    check(listed, "le schema : l'action creer_unite");
}

// ---- 4. la correction rapide ---------------------------------------------------------------------------------------------
struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};
    const ui::Theme              theme = ui::Theme::dark();
    const std::string            convId = "c-unites";

    Fixture(const std::string& name, const std::function<std::vector<ai::Operation>()>& ops) {
        dir = fs::temp_directory_path() / ("xpg_ai_unites_" + name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        view = seedProject(doc->project);
        ai::Conversation c;
        c.id = convId;
        c.title = "Les mesures";
        c.created = c.modified = ai::isoDate(ai::now());
        ai::Message ask;
        ask.id = "m1";
        ask.role = "utilisateur";
        ask.date = c.created;
        ask.text = "Mets les mesures en mmCE";
        c.messages.push_back(ask);
        ai::Message answer;
        answer.id = "m2";
        answer.role = "assistant";
        answer.date = c.created;
        answer.state = "fini";
        answer.text = "Voici la proposition.";
        ai::Proposal p;
        p.id = "op-mesures";
        p.state = "proposee";
        p.request = ask.text;
        p.summary = "Mesures en mmCE";
        p.ops = ops();
        ai::scopeProposal(p, doc->project.view(view));
        p.fingerprint = ai::proposalPrint(doc->project, p);
        answer.proposal = std::move(p);
        c.messages.push_back(std::move(answer));
        check(ai::saveConversation(c), "la conversation est ecrite");
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); };
        core.projectName = [] { return std::string("Station"); };
        core.projectPath = [this] { return U(dir / "Station"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        ui.status = [](const std::string&) {};
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 900, 900});
        panel->ensureLoaded();
        ai::Settings st = panel->assistant().settings();
        st.provider = "demo";
        st.applyWithErrors = true;
        panel->assistant().setSettings(st);
        panel->layout();
    }
    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
    void step() {
        t += 0.05;
        panel->tick(t);
        mm.applyPending();
        panel->layout();
    }
    int open() {
        const int i = as().indexOf(convId);
        check(i >= 0 && panel->selectConversation(i), "la conversation se rouvre");
        step();
        return as().proposal(1) ? 1 : -1;
    }
    void update(ai::AiPreviewDialog& pv) {
        t += 0.05;
        pv.Update(menu::FrameContext{0.05, t, &theme, {1600.f, 900.f}, 1.f});
    }
    ai::AiPreviewDialog* preview(int m) {
        panel->openPreview(m);
        mm.applyPending();
        auto* pv = dynamic_cast<ai::AiPreviewDialog*>(mm.top());
        if (pv && pv->widgetRoot()) {
            pv->widgetRoot()->setBounds({0, 0, 1600, 900});
            pv->widgetRoot()->layout();
        }
        return pv;
    }
    [[nodiscard]] ai::AiPreviewDialog* top() { return dynamic_cast<ai::AiPreviewDialog*>(mm.top()); }
    [[nodiscard]] std::size_t unitOps(int m) {
        const ai::Proposal* p = as().proposal(m);
        return p ? static_cast<std::size_t>(std::count_if(p->ops.begin(), p->ops.end(), [](const ai::Operation& o) { return o.action == A::Unit; })) : 0;
    }
};

std::vector<ai::Operation> captureOps() {
    return {withUnit("VE3210_Mesure", "mmCE"), withUnit("VE3211_Mesure", "mmCE"), withUnit("VE260_Mesure", "mmCE")};
}

void testQuick() {
    std::printf("[corriger rapidement : unite inconnue mmCE]\n");
    Fixture f("rapide", captureOps);
    const int m = f.open();
    if (m < 0) return;
    auto* pv = f.preview(m);
    check(pv != nullptr, "la previsualisation s'ouvre");
    if (!pv) return;
    const auto rows = pv->errorRows();
    check(rows.size() == 3 && std::all_of(rows.begin(), rows.end(), [](const std::string& r) { return has(r, kUnknown); }),
          "trois erreurs << unite inconnue << mmCE >> >> (" + joinAll(rows) + ")");
    const fx::Action quick = pv->quickFor(1, 0);
    check(quick.enabled && quick.does == K::Unit && has(quick.note, "1 mmCE = 9,80665 Pa"), "Corriger rapidement : creer mmCE (" + quick.note + ")");
    check(pv->openErrorMenu(0), "le clic droit sur la premiere erreur");
    const auto items = pv->menuItems();
    check(!items.empty() && items.front() == kQuick && std::find(items.begin(), items.end(), kAsk) != items.end(),
          "le menu : Corriger rapidement en tete, puis << Creer l'unite << mmCE >>... >>");
    check(pv->chooseMenuItem(kQuick), "Corriger rapidement");
    f.mm.applyPending();
    f.update(*pv);
    check(f.top() == pv, "la fenetre reste ouverte");
    check(pv->errorRows().empty(), "les trois erreurs sont parties (" + joinAll(pv->errorRows()) + ")");
    check(f.unitOps(m) == 1, "UNE seule unite ajoutee a la proposition");
    check(!hu::Catalog(f.doc->project.config).unit("mmCE"), "le projet, non (rien avant Appliquer)");
    check(anyHas(pv->correctionRows(), "ajout\xC3\xA9" "e") && anyHas(pv->correctionRows(), "1 mmCE = 9,80665 Pa"),
          "CORRECTIONS : l'unite ajoutee, sa conversion (" + joinAll(pv->correctionRows()) + ")");
    check(has(pv->bandText(), "Corrig\xC3\xA9") && has(pv->bandText(), "mmCE"), "la bande le dit (" + pv->bandText() + ")");
    // REVENIR : l'unite quitte la proposition, les erreurs reviennent
    check(pv->undoBand() && f.unitOps(m) == 0, "Revenir : l'unite quitte la proposition");
    f.update(*pv);
    check(pv->errorRows().size() == 3, "les erreurs reviennent");
    // Encore, puis Appliquer : un seul Ctrl+Z
    check(pv->quickFix(1, 1) && f.unitOps(m) == 1 && pv->errorRows().empty(), "Corriger rapidement (le geste)");
    const std::size_t steps = f.stack.done().size();
    check(pv->apply() && f.stack.done().size() == steps + 1, "Appliquer : UNE commande");
    const hu::Catalog cat(f.doc->project.config);
    check(cat.unit("mmCE") && cat.unit("mmCE")->factor == 9.80665 && f.doc->project.variable("VE260_Mesure")->unit == "mmCE", "l'unite et les trois variables");
    (void)f.stack.undo();
    check(!hu::Catalog(f.doc->project.config).unit("mmCE") && f.doc->project.variable("VE260_Mesure")->unit.empty(), "un seul Ctrl+Z reprend tout");
}

void testAll() {
    std::printf("[tout corriger rapidement : une seule unite]\n");
    Fixture f("tout", captureOps);
    const int m = f.open();
    auto* pv = m < 0 ? nullptr : f.preview(m);
    if (!pv) return;
    check(pv->quickCount(1) == 3, "trois corrections sures offertes");
    const std::size_t fixed = pv->quickFixAll(1);
    f.update(*pv);
    check(fixed == 3 && f.unitOps(m) == 1, "Tout corriger rapidement : 3 lignes corrigees, UNE unite creee (" + std::to_string(fixed) + ")");
    check(pv->errorRows().empty() && f.top() == pv, "plus d'erreur, la fenetre ouverte");
}

void testUnknown() {
    std::printf("[xyz : grisee ; Creer l'unite... pre-rempli]\n");
    Fixture f("inconnue", [] { return std::vector<ai::Operation>{withUnit("VE3210_Mesure", "xyz"), withUnit("VE3211_Mesure", "mmCE")}; });
    const int m = f.open();
    auto* pv = m < 0 ? nullptr : f.preview(m);
    if (!pv) return;
    const int x = indexHaving(pv->errorRows(), q("xyz"));
    check(x >= 0, "l'erreur de xyz");
    if (x < 0) return;
    const fx::Action quick = pv->quickFor(1, static_cast<std::size_t>(x));
    check(!quick.enabled && has(quick.why, "Famille et facteur inconnus") && has(quick.why, "Cr\xC3\xA9" "er l'unit\xC3\xA9\xE2\x80\xA6"),
          "xyz : grisee, avec la raison (" + quick.why + ")");
    // La fenetre : vide pour xyz, pre-remplie pour mmCE.
    const fx::UnitForm none = fx::unitForm(f.doc->project, "xyz");
    check(!none.known && none.values[0] == "xyz" && none.values[1] == fx::kPickFamily && none.values[2].empty() && none.families.front() == fx::kPickFamily,
          "xyz : la fenetre est a remplir (aucune famille choisie d'office)");
    const fx::UnitForm form = fx::unitForm(f.doc->project, "mmCE");
    check(form.known && form.values[1] == "pression (Pa)" && form.values[2] == "9.80665" && has(form.values[4], "colonne d'eau"),
          "mmCE : la fenetre est pre-remplie (pression, 9.80665, son libelle)");
    const fx::UnitForm kw = fx::unitForm(f.doc->project, "kW");
    check(kw.known && kw.values[1] == fx::kNewFamily && kw.values[5] == "puissance" && kw.values[6] == "W" && kw.values[2] == "1000",
          "kW : une famille neuve pre-remplie (puissance, W, 1000)");
    check(has(fx::unitFormCheck(f.doc->project.config, form.values), kConversion), "la fenetre lit la conversion : " + kConversion);
    int field = -1;
    bool ok = true;
    (void)fx::unitFormCheck(f.doc->project.config, none.values, &field, &ok);
    check(!ok && field == 1, "xyz sans famille : refusee, le champ Famille en cause");
    // << Creer l'unite << mmCE >>... >> : la petite fenetre par-dessus, Ajouter : l'unite dans la proposition
    const int k = indexHaving(pv->errorRows(), q("mmCE"));
    check(k >= 0 && pv->openErrorMenu(static_cast<std::size_t>(k)) && pv->chooseMenuItem(kAsk), "<< Creer l'unite << mmCE >>... >>");
    f.mm.applyPending();
    auto* dlg = dynamic_cast<app::FormDialog*>(f.mm.top());
    check(dlg != nullptr, "la petite fenetre s'ouvre par-dessus");
    if (dlg) {
        check(dlg->confirm(), "Ajouter (les valeurs pre-remplies)");
        f.mm.applyPending();
    }
    f.update(*pv);
    check(f.top() == pv && f.unitOps(m) == 1 && indexHaving(pv->errorRows(), q("mmCE")) < 0 && indexHaving(pv->errorRows(), q("xyz")) >= 0,
          "la previsualisation reste ouverte : mmCE ajoutee, son erreur partie, celle de xyz reste");
    // xyz, la fenetre vide : Ajouter refuse (la famille manque) - la fenetre reste
    const int k2 = indexHaving(pv->errorRows(), q("xyz"));
    check(k2 >= 0 && pv->openErrorMenu(static_cast<std::size_t>(k2)) && pv->chooseMenuItem("Cr\xC3\xA9" "er l'unit\xC3\xA9 " + q("xyz") + "\xE2\x80\xA6"),
          "<< Creer l'unite << xyz >>... >>");
    f.mm.applyPending();
    auto* dlg2 = dynamic_cast<app::FormDialog*>(f.mm.top());
    check(dlg2 != nullptr && !dlg2->confirm() && !dlg2->fieldError(1).empty(), "vide : Ajouter refuse, la raison sous Famille");
    if (dlg2) {
        f.mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        f.mm.applyPending();
    }
    check(f.unitOps(m) == 1, "rien de plus dans la proposition");
}

} // namespace

int main() {
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
        s.clipboardGet = [] { return g_clipboard; };
        ui::installPlatformServices(std::move(s));
    }
    {
        const fs::path root = fs::temp_directory_path() / "xpg_ai_unites_racine";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
        ai::setRoot(U(root / "IA"));
    }
    testShared();
    testOperation();
    testPrompt();
    testQuick();
    testAll();
    testUnknown();
    std::printf("ai_unites_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
