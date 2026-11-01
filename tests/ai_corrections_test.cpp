// =============================================================================
//  tests/ai_corrections_test.cpp - 1.22.1 : CORRIGER UNE PROPOSITION DEPUIS SES ERREURS
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un dossier IA/ jetable : la proposition de la
//  capture (une vanne, sept variables introuvables dont << Vanne.Cmd >> deux
//  fois, un code brut illisible) - et le clic droit qui la corrige :
//    1. le moteur : sept variables manquantes -> leurs defauts structures (l'operation,
//       les plus proches, le type conseille) ; le texte des erreurs ne change pas ;
//       les doublons regroupes (<< (2 operations) >>) ; les actions du menu ; un choix
//       par variable (toutes ses occurrences), creer (le membre << Vanne_NbOuv >>, son
//       type), retirer une operation ; Appliquer : la proposition corrigee, un seul
//       Ctrl+Z ; le choix unique d'avant toujours lu ; le resume (<< 5 variables
//       remplacees... >>) ; le JSON aller-retour, un fichier 1.22.0 relu ;
//    2. la previsualisation : la liste regroupee, le menu d'une erreur, Utiliser,
//       Revenir, Creer, Choisir une variable... (le choix du haut), Copier, Retirer,
//       Demander a l'IA (la saisie, rien ne part), la categorie Corrections, la
//       conversation enregistree puis relue, Appliquer ;
//    3. la carte de la conversation : ses notes regroupees, le clic droit, la ligne
//       << Corrigee : ... >> ;
//    4. une grosse proposition (au moins 40 operations) : la correction se reverifie
//       sur le fil de fond de la carte, et l'erreur s'en va.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiFixes.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPreviewDialog.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace fx = app::ai::fixes;

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
bool listed(const std::vector<std::string>& lines, const std::string& exact) {
    return std::find(lines.begin(), lines.end(), exact) != lines.end();
}
int indexHaving(const std::vector<std::string>& lines, const std::string& part) {
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (has(lines[i], part)) return static_cast<int>(i);
    return -1;
}

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string readAll(const fs::path& p) {
    std::string s;
    (void)core::readFileAll(p, s);
    return s;
}

const std::string kO = "\xC2\xAB ";   // << et son espace
const std::string kF = " \xC2\xBB";   // >>
std::string q(const std::string& s) { return kO + s + kF; }
const std::string kOps2 = " (2 op\xC3\xA9rations)";
const std::string kChoose = "Choisir une variable\xE2\x80\xA6";
const std::string kAsk = "Demander \xC3\xA0 l'IA de corriger";

// Le message de la validation pour une variable introuvable (il ne change pas : les essais d'avant le lisent).
std::string missingText(const std::string& name) {
    return "La variable " + q(name) + " n'existe pas dans le projet : l'Assistant ne l'invente pas. "
           "Choisis une variable existante, ou demande sa cr\xC3\xA9" "ation.";
}

// Un renderer qui retient les textes.
struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex*, std::size_t) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
};

// ---- le projet et la proposition de la capture ------------------------------------------------
hmi::Id seedProject(hmi::Project& p) {
    p.views = {hmi::makeView(p, "Vue_Station")};
    const auto add = [&](const char* name, const char* type) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = type;
        v.initial = std::string(type) == "BOOL" ? "FALSE" : "0";
        p.programs.variables.push_back(v);
    };
    add("Vanne_Ouverte", "BOOL");
    add("Vanne_Autorisation", "BOOL");
    add("Vanne_NbFerm_Total", "DINT");
    add("Pompe_1_Marche", "BOOL");
    return p.views.front().id;
}

ai::Operation voyant(const std::string& name, const std::string& variable, int y) {
    ai::Operation op;
    op.action = ai::Operation::Action::Create;
    op.kind = "Indicator";
    op.name = name;
    op.props = {{"x", "40"}, {"y", std::to_string(y)}, {"variable", variable}};
    return op;
}

// Les operations 0 a 7 lisent sept variables introuvables (Vanne.Cmd deux fois) ; la 8 est un code brut illisible.
const std::vector<std::string> kMissing = {"Vanne.Cmd", "Vanne.Autorisee", "Vanne.F0", "Vanne.F1", "Vanne.NbOuv", "Vanne.NbFerm", "Vanne.RazCpt"};

ai::Proposal captureProposal(hmi::Id view) {
    ai::Proposal p;
    p.id = "op-vanne";
    p.state = "proposee";
    p.view = "Vue_Station";
    p.viewId = view;
    p.request = "Ajoute la commande de la vanne et ses compteurs";
    p.summary = "Commande de la vanne";
    p.ops.push_back(voyant("Voyant_Cmd", "Vanne.Cmd", 40));
    p.ops.push_back(voyant("Voyant_Cmd_2", "Vanne.Cmd", 80));
    p.ops.push_back(voyant("Voyant_Autorisee", "Vanne.Autorisee", 120));
    p.ops.push_back(voyant("Voyant_F0", "Vanne.F0", 160));
    p.ops.push_back(voyant("Voyant_F1", "Vanne.F1", 200));
    p.ops.push_back(voyant("Voyant_NbOuv", "Vanne.NbOuv", 240));
    p.ops.push_back(voyant("Voyant_NbFerm", "Vanne.NbFerm", 280));
    p.ops.push_back(voyant("Voyant_RazCpt", "Vanne.RazCpt", 320));
    ai::Operation code;
    code.action = ai::Operation::Action::Code;
    code.code = "ceci n'est pas un code brut\nfin\nune ligne apres fin";
    p.ops.push_back(code);
    return p;
}

const hmi::Object* objectNamed(const hmi::Project& p, hmi::Id view, const std::string& name) {
    const hmi::View* v = p.view(view);
    if (!v) return nullptr;
    for (const auto& o : v->objects)
        if (o.name == name) return &o;
    return nullptr;
}

std::vector<std::string> flatLabels(const std::vector<fx::Action>& actions, const std::string& path = {}) {
    std::vector<std::string> out;
    for (const auto& a : actions) {
        if (a.kind == fx::Action::Kind::Separator) continue;
        const std::string label = path.empty() ? a.label : path + " > " + a.label;
        if (a.children.empty()) out.push_back(label);
        else for (auto& c : flatLabels(a.children, label)) out.push_back(std::move(c));
    }
    return out;
}

ai::VariableChoice use(const std::string& from, const std::string& name) {
    ai::VariableChoice c;
    c.from = from;
    c.use = name;
    return c;
}
ai::VariableChoice create(const std::string& from, const std::string& type) {
    ai::VariableChoice c;
    c.from = from;
    c.create = true;
    c.type = type;
    return c;
}

// ---- 1. le moteur ------------------------------------------------------------------------------------
void testEngine() {
    std::printf("[moteur]\n");
    auto doc = std::make_shared<hmi::Document>();
    const hmi::Id view = seedProject(doc->project);
    ai::Proposal p = captureProposal(view);
    auto ck = ai::validate(doc->project, p);
    check(ck.errors.size() == 9, "la capture : 9 erreurs (" + std::to_string(ck.errors.size()) + ")");
    check(std::count(ck.errors.begin(), ck.errors.end(), missingText("Vanne.Cmd")) == 2, "le texte des erreurs ne change pas (Vanne.Cmd, deux fois)");
    check(ck.missingVariable == "Vanne.Cmd", "la premiere variable introuvable reste dite (missingVariable)");
    // LES DEFAUTS STRUCTURES
    std::set<std::string> names;
    std::size_t variables = 0;
    for (const auto& f : ck.faults)
        if (f.kind == "variable") {
            ++variables;
            names.insert(f.name);
        }
    check(names.size() == 7 && variables == 8, "sept variables manquantes : sept defauts de variable (Vanne.Cmd dans deux operations : 8)");
    check(names == std::set<std::string>(kMissing.begin(), kMissing.end()), "les sept noms, tels que la proposition les ecrit");
    check(ck.faults.size() == ck.errors.size(), "un defaut structure par erreur");
    std::vector<std::size_t> cmdOps;
    const ai::Check::Fault* cmd = nullptr;
    const ai::Check::Fault* nbOuv = nullptr;
    const ai::Check::Fault* raz = nullptr;
    const ai::Check::Fault* codeFault = nullptr;
    for (const auto& f : ck.faults) {
        if (f.kind == "variable" && f.name == "Vanne.Cmd") {
            cmdOps.push_back(f.op);
            cmd = &f;
        }
        if (f.name == "Vanne.NbOuv") nbOuv = &f;
        if (f.name == "Vanne.RazCpt") raz = &f;
        if (f.kind == "code") codeFault = &f;
    }
    check(cmdOps == std::vector<std::size_t>{0, 1}, "Vanne.Cmd : les operations 0 et 1 (les indices de la proposition)");
    check(cmd && cmd->message == missingText("Vanne.Cmd") && std::find(cmd->closest.begin(), cmd->closest.end(), "Vanne_Ouverte") != cmd->closest.end()
              && cmd->closest.size() <= 5,
          "son message, et les plus proches (Vanne_Ouverte, 5 au plus)");
    check(cmd && cmd->type == "BOOL" && nbOuv && nbOuv->type == "DINT" && nbOuv->op == 5 && raz && raz->type == "BOOL",
          "le type conseille : Cmd BOOL, NbOuv DINT, RazCpt BOOL");
    check(codeFault && codeFault->op == 8 && has(codeFault->message, "Code brut"), "le code brut illisible : un defaut << code >>, l'operation 8");
    // LES DOUBLONS
    const auto lines = fx::errorLines(ck);
    check(lines.size() == 8, "8 lignes d'erreur (Vanne.Cmd regroupee)");
    check(!lines.empty() && lines.front().text == missingText("Vanne.Cmd") + kOps2 && lines.front().ops == std::vector<std::size_t>{0, 1}
              && lines.front().count == 2,
          "<< ... (2 operations) >>, ses deux operations");
    // LE MENU d'une variable introuvable, d'un code brut
    if (!lines.empty()) {
        const auto menu = fx::menuFor(p, ck, lines.front(), &doc->project);
        const auto labels = flatLabels(menu.actions);
        check(menu.title == "Variable " + q("Vanne.Cmd") + " introuvable", "la tete du menu : la variable");
        check(listed(labels, "Utiliser " + q("Vanne_Ouverte")) && listed(labels, kChoose)
                  && listed(labels, "Cr\xC3\xA9" "er la variable " + q("Vanne_Cmd") + " > BOOL (conseill\xC3\xA9)")
                  && listed(labels, "Cr\xC3\xA9" "er la variable " + q("Vanne_Cmd") + " > DINT")
                  && listed(labels, "Retirer les 2 op\xC3\xA9rations qui l'utilisent") && listed(labels, "Copier le message") && listed(labels, kAsk),
              "variable : Utiliser, Choisir, Creer (le membre devient Vanne_Cmd, le type), Retirer, Copier, Demander a l'IA");
        check(indexHaving(labels, "Utiliser") == 0, "les proches d'abord");
    }
    const auto codeLine = std::find_if(lines.begin(), lines.end(), [](const fx::Line& l) { return has(l.message, "Code brut"); });
    check(codeLine != lines.end(), "la ligne du code brut");
    if (codeLine != lines.end()) {
        const auto menu = fx::menuFor(p, ck, *codeLine, &doc->project);
        const auto labels = flatLabels(menu.actions);
        check(menu.title == "Code brut" && listed(labels, "Retirer cette op\xC3\xA9ration") && listed(labels, "Copier le message") && listed(labels, kAsk)
                  && indexHaving(labels, "Utiliser") < 0,
              "code brut : Retirer cette operation, Copier, Demander a l'IA - rien sur les variables");
        const auto ask = std::find_if(menu.actions.begin(), menu.actions.end(), [](const fx::Action& a) { return a.kind == fx::Action::Kind::AskAi; });
        check(ask != menu.actions.end() && has(ask->value, "Corrige ta proposition") && has(ask->value, codeLine->message),
              "Demander a l'IA : le texte prepare cite l'erreur");
    }

    // REMPLACER Vanne.Cmd : ses deux erreurs disparaissent, ses deux operations lisent Vanne_Ouverte.
    p.choices.push_back(use("Vanne.Cmd", "Vanne_Ouverte"));
    ck = ai::validate(doc->project, p);
    check(ck.errors.size() == 7 && std::count(ck.errors.begin(), ck.errors.end(), missingText("Vanne.Cmd")) == 0, "Vanne.Cmd remplacee : ses 2 erreurs s'en vont");
    check(std::count_if(ck.bindings.begin(), ck.bindings.end(),
                        [](const std::string& b) { return has(b, "Vanne_Ouverte (\xC3\xA0 la place de Vanne.Cmd)"); }) == 2,
          "les deux associations le disent");
    check(ck.missingVariable == "Vanne.Autorisee", "la suivante est la premiere introuvable");
    // CREER Vanne.NbOuv (DINT) : << Vanne_NbOuv >> (un membre ne se cree pas seul)
    p.choices.push_back(create("Vanne.NbOuv", "DINT"));
    ck = ai::validate(doc->project, p);
    check(ck.errors.size() == 6 && std::any_of(ck.warnings.begin(), ck.warnings.end(), [](const std::string& w) {
              return has(w, "La variable Vanne_NbOuv (DINT) sera cr\xC3\xA9\xC3\xA9" "e") && has(w, "ta demande");
          }),
          "creer : l'erreur part, l'avertissement dit la variable et son type");
    // Les autres ; le code brut retire.
    p.choices.push_back(use("Vanne.Autorisee", "Vanne_Autorisation"));
    p.choices.push_back(use("Vanne.F0", "Pompe_1_Marche"));
    p.choices.push_back(use("Vanne.F1", "Vanne_Ouverte"));
    p.choices.push_back(use("Vanne.NbFerm", "Vanne_NbFerm_Total"));
    p.choices.push_back(create("Vanne.RazCpt", "BOOL"));
    p.excluded = {8};
    ck = ai::validate(doc->project, p);
    check(ck.ok(), "tout corrige : la proposition est valide (" + (ck.errors.empty() ? std::string() : ck.errors.front()) + ")");
    check(fx::summary(p) == "5 variables remplac\xC3\xA9" "es, 2 variables \xC3\xA0 cr\xC3\xA9" "er, 1 op\xC3\xA9ration retir\xC3\xA9" "e",
          "le resume des corrections : " + fx::summary(p));
    const auto done = fx::corrections(p);
    check(done.size() == 8 && done.front().text == q("Vanne.Cmd") + " \xE2\x86\x92 Vanne_Ouverte" && done.front().state == "remplac\xC3\xA9" "e"
              && done.front().undo.kind == fx::Action::Kind::Revert && done.back().undo.kind == fx::Action::Kind::Restore,
          "les corrections : une ligne chacune, et son Revenir");
    // UN CHOIX NOMME UNE VARIABLE QUI N'EXISTE PAS : refuse, jamais invente.
    {
        ai::Proposal bad = p;
        bad.choices.front().use = "Vanne_Fantome";
        const auto b = ai::validate(doc->project, bad);
        check(!b.ok() && has(b.errors.front(), "Vanne_Fantome") && has(b.errors.front(), "n'existe pas non plus"), "une variable choisie introuvable : refusee");
    }
    // APPLIQUER : la proposition corrigee, un seul Ctrl+Z.
    core::CommandStack stack;
    const std::size_t vars = doc->project.programs.variables.size();
    std::string why;
    auto command = ai::makeCommand(doc, p, &why);
    check(command != nullptr, "la commande de la proposition corrigee (" + why + ")");
    if (command) (void)stack.push(std::move(command));
    const auto bound = [&](const std::string& o) {
        const hmi::Object* x = objectNamed(doc->project, view, o);
        return x ? x->text("variable") : std::string("(absent)");
    };
    check(bound("Voyant_Cmd") == "Vanne_Ouverte" && bound("Voyant_Cmd_2") == "Vanne_Ouverte", "Vanne.Cmd : remplacee dans ses deux operations");
    check(bound("Voyant_Autorisee") == "Vanne_Autorisation" && bound("Voyant_F0") == "Pompe_1_Marche" && bound("Voyant_NbFerm") == "Vanne_NbFerm_Total",
          "les autres remplacees");
    const hmi::Variable* nb = doc->project.variable("Vanne_NbOuv");
    const hmi::Variable* rz = doc->project.variable("Vanne_RazCpt");
    check(nb && nb->type == "DINT" && nb->initial == "0" && bound("Voyant_NbOuv") == "Vanne_NbOuv", "Vanne_NbOuv creee (DINT, 0), le voyant la lit");
    check(rz && rz->type == "BOOL" && rz->initial == "FALSE" && bound("Voyant_RazCpt") == "Vanne_RazCpt", "Vanne_RazCpt creee (BOOL)");
    check(doc->project.programs.variables.size() == vars + 2 && !doc->project.variable("Vanne"), "rien d'autre n'est cree (aucune variable inventee)");
    check(stack.done().size() == 1, "un seul pas d'annulation");
    (void)stack.undo();
    check(doc->project.programs.variables.size() == vars && !doc->project.variable("Vanne_NbOuv") && !objectNamed(doc->project, view, "Voyant_Cmd"),
          "un Ctrl+Z reprend tout (les variables creees, les objets)");

    // LE CHOIX UNIQUE D'AVANT (un fichier 1.22.0) : toujours lu ; un choix par variable passe devant lui.
    {
        ai::Proposal old = captureProposal(view);
        old.variable.use = "Pompe_1_Marche";
        auto o = ai::validate(doc->project, old);
        check(o.errors.size() == 1 && has(o.errors.front(), "Code brut"), "le choix unique d'avant : toutes les variables introuvables le prennent");
        old.choices.push_back(use("Vanne.Cmd", "Vanne_Ouverte"));
        o = ai::validate(doc->project, old);
        check(std::any_of(o.bindings.begin(), o.bindings.end(), [](const std::string& b) { return has(b, "Voyant_Cmd \xE2\x86\x92 Vanne_Ouverte"); })
                  && std::any_of(o.bindings.begin(), o.bindings.end(), [](const std::string& b) { return has(b, "Voyant_F0 \xE2\x86\x92 Pompe_1_Marche"); }),
              "le choix de Vanne.Cmd passe devant, les autres gardent celui d'avant");
    }

    // LE JSON : aller-retour ; un fichier 1.22.0 (sans le champ) ; des entrees abimees laissees de cote.
    {
        const ai::json::Value j = ai::toJson(p);
        check(j.has("variablesChoisies") && j.at("variablesChoisies").items().size() == 7, "le JSON garde les sept choix (variablesChoisies)");
        ai::json::Value again;
        check(ai::json::parse(ai::json::write(j), again), "ecrit puis relu");
        const ai::Proposal back = ai::proposalFromJson(again);
        check(back.choices == p.choices && back.excluded == p.excluded && back.extra.members().empty(), "relu : les memes choix, les memes retraits, rien d'inconnu");
        const ai::Proposal plain = captureProposal(view);
        check(!ai::toJson(plain).has("variablesChoisies"), "sans correction : le champ n'est pas ecrit (le fichier reste celui de la 1.22.0)");
        ai::json::Value v122;
        check(ai::json::parse("{\"id\": \"op1\", \"etat\": \"proposee\", \"vue\": \"Vue_Station\", \"variableChoisie\": {\"utiliser\": \"Pompe_1_Marche\", "
                              "\"creer\": false, \"type\": \"BOOL\"}, \"operations\": [{\"action\": \"associer\", \"objet\": \"Voyant_1\", "
                              "\"variable\": \"Vanne.Cmd\"}]}",
                              v122),
              "un fichier 1.22.0");
        const ai::Proposal r = ai::proposalFromJson(v122);
        check(r.choices.empty() && r.variable.use == "Pompe_1_Marche" && r.ops.size() == 1 && r.extra.members().empty(),
              "un fichier 1.22.0 se relit : aucun choix par variable, le choix d'avant garde");
        ai::json::Value bad;
        (void)ai::json::parse("{\"id\": \"op2\", \"operations\": [], \"variablesChoisies\": [{\"utiliser\": \"X\"}, {\"de\": \"A\", \"utiliser\": \"B\"}, "
                              "{\"de\": \"a\", \"utiliser\": \"C\"}, {\"de\": \"D\"}, 3, {\"de\": \"E\", \"creer\": true}]}",
                              bad);
        const ai::Proposal rb = ai::proposalFromJson(bad);
        check(rb.choices.size() == 2 && rb.choices[0].from == "A" && rb.choices[0].use == "B" && rb.choices[1].from == "E" && rb.choices[1].create
                  && rb.choices[1].type == "BOOL",
              "des entrees abimees ou en double : laissees de cote (le type par defaut : BOOL)");
    }
    check(ai::creatableName("Vanne.Cmd") == "Vanne_Cmd" && ai::creatableName("Pompes[3].Marche") == "Pompes_3_Marche" && ai::creatableName("PUMP_01") == "PUMP_01",
          "le nom a creer : un identifiant (Vanne_Cmd, Pompes_3_Marche ; PUMP_01 tel quel)");
}

// ---- le panneau ------------------------------------------------------------------------------------
struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    std::vector<std::string>     statuses;
    double                       t{100.0};
    const std::string            convId = "c-vanne";

    Fixture(const std::string& name, bool fresh, const std::function<ai::Proposal(hmi::Id)>& make = captureProposal) {
        dir = fs::temp_directory_path() / ("xpg_ai_corrections_" + name);
        if (fresh) {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        view = seedProject(doc->project);
        if (fresh) {
            ai::Conversation c;
            c.id = convId;
            c.title = "La vanne";
            c.created = c.modified = ai::isoDate(ai::now());
            ai::Message ask;
            ask.id = "m1";
            ask.role = "utilisateur";
            ask.date = c.created;
            ask.text = "Ajoute la commande de la vanne et ses compteurs";
            c.messages.push_back(ask);
            ai::Message answer;
            answer.id = "m2";
            answer.role = "assistant";
            answer.date = c.created;
            answer.state = "fini";
            answer.text = "Voici la commande de la vanne.";
            answer.proposal = make(view);
            c.messages.push_back(std::move(answer));
            check(ai::saveConversation(c), "la conversation est ecrite");
        }
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); };
        core.projectName = [] { return std::string("Station"); };
        core.projectPath = [this] { return U(dir / "Station"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        ui.status = [this](const std::string& s) { statuses.push_back(s); };
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 900, 900});
        panel->ensureLoaded();
        ai::Settings st = panel->assistant().settings();
        st.provider = "demo";
        st.applyWithErrors = true;   // la capture : << laissees de cote par Appliquer malgre >>
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
    // La conversation ouverte ; le message de la proposition (-1 : introuvable).
    int open() {
        const int i = as().indexOf(convId);
        check(i >= 0 && panel->selectConversation(i), "la conversation se rouvre");
        step();
        return as().proposal(1) ? 1 : -1;
    }
    void render(Recorder& rec) {
        panel->layout();
        panel->render(ui::PaintContext{rec, ui::Theme::dark(), panel->bounds(), t, nullptr});
    }
    ai::AiPreviewDialog* preview(int m) {
        panel->openPreview(m);
        mm.applyPending();
        auto* pv = dynamic_cast<ai::AiPreviewDialog*>(mm.top());
        if (pv && pv->widgetRoot()) {
            pv->widgetRoot()->setBounds({0, 0, 1600, 900});
            pv->widgetRoot()->layout();
            Recorder rec;   // un dessin : la place de chaque ligne (le clic droit)
            pv->widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), pv->widgetRoot()->bounds(), t, nullptr});
        }
        return pv;
    }
    [[nodiscard]] const hmi::Object* object(const std::string& name) const { return objectNamed(doc->project, view, name); }
};

// Choisir une entree du menu d'une ligne d'erreur (celle qui contient `part`).
bool fixError(ai::AiPreviewDialog& pv, const std::string& part, const std::string& item) {
    const int k = indexHaving(pv.errorRows(), part);
    if (k < 0 || !pv.openErrorMenu(static_cast<std::size_t>(k))) return false;
    return pv.chooseMenuItem(item);
}

// ---- 2. la previsualisation ------------------------------------------------------------------------
void testPreview() {
    std::printf("[previsualisation]\n");
    {
        Fixture f("apercu", true);
        const int m = f.open();
        check(m == 1, "la proposition de la capture");
        if (m < 0) return;
        auto* pv = f.preview(m);
        check(pv != nullptr, "la previsualisation s'ouvre");
        if (!pv) return;
        auto rows = pv->errorRows();
        check(rows.size() == 8 && rows.front() == missingText("Vanne.Cmd") + kOps2, "ERREURS : 8 lignes, << ... (2 operations) >> pour Vanne.Cmd");
        check(pv->variableShown() == "Vanne.Cmd", "le choix du haut : la premiere variable introuvable");
        // LE MENU D'UNE VARIABLE INTROUVABLE
        check(pv->openErrorMenu(0) && pv->menuOpen(), "le clic droit sur Vanne.Cmd : le menu s'ouvre");
        const auto items = pv->menuItems();
        check(listed(items, "Utiliser " + q("Vanne_Ouverte")) && listed(items, kChoose)
                  && listed(items, "Cr\xC3\xA9" "er la variable " + q("Vanne_Cmd") + " > BOOL (conseill\xC3\xA9)")
                  && listed(items, "Retirer les 2 op\xC3\xA9rations qui l'utilisent") && listed(items, "Copier le message") && listed(items, kAsk),
              "ses entrees : Utiliser, Choisir, Creer, Retirer, Copier, Demander a l'IA");
        // UTILISER : l'erreur disparait, la correction se lit, la conversation la garde.
        check(pv->chooseMenuItem("Utiliser " + q("Vanne_Ouverte")) && !pv->menuOpen(), "Utiliser Vanne_Ouverte");
        const ai::Proposal* p = f.as().proposal(m);
        check(p && p->choices.size() == 1 && p->choices.front().from == "Vanne.Cmd" && p->choices.front().use == "Vanne_Ouverte", "la proposition change");
        rows = pv->errorRows();
        check(rows.size() == 7 && indexHaving(rows, "Vanne.Cmd") < 0, "Vanne.Cmd n'est plus dans les erreurs");
        check(pv->correctionRows() == std::vector<std::string>{q("Vanne.Cmd") + " \xE2\x86\x92 Vanne_Ouverte | remplac\xC3\xA9" "e"}, "CORRECTIONS : la ligne << remplacee >>");
        check(pv->variableShown() == "Vanne.Autorisee", "le choix du haut passe a la suivante");
        check(f.doc->project.variable("Vanne_Cmd") == nullptr && f.object("Voyant_Cmd") == nullptr, "le projet n'a pas bouge");
        const fs::path file = f.dir / "IA" / "conversations" / (f.convId + ".json");
        check(has(readAll(file), "\"variablesChoisies\"") && has(readAll(file), "\"de\": \"Vanne.Cmd\""), "gardee avec la conversation");
        // REVENIR
        check(pv->openCorrectionMenu(0) && listed(pv->menuItems(), "Revenir"), "le clic droit sur la correction : Revenir");
        check(pv->chooseMenuItem("Revenir") && f.as().proposal(m)->choices.empty() && pv->errorRows().size() == 8 && pv->correctionRows().empty(),
              "Revenir : l'erreur revient, la correction s'en va");
        check(fixError(*pv, "Vanne.Cmd", "Utiliser " + q("Vanne_Ouverte")), "de nouveau : Utiliser Vanne_Ouverte");
        check(fixError(*pv, "Vanne.Autorisee", "Utiliser " + q("Vanne_Autorisation")), "Vanne.Autorisee : Utiliser Vanne_Autorisation");
        // CREER (DINT conseille pour un compteur)
        check(fixError(*pv, "Vanne.NbOuv", "Cr\xC3\xA9" "er la variable " + q("Vanne_NbOuv") + " > DINT (conseill\xC3\xA9)"), "Vanne.NbOuv : Creer, DINT");
        check(listed(pv->correctionRows(), q("Vanne.NbOuv") + " \xE2\x86\x92 Vanne_NbOuv | \xC3\xA0 cr\xC3\xA9" "er (DINT)"), "CORRECTIONS : << a creer (DINT) >>");
        check(f.doc->project.variable("Vanne_NbOuv") == nullptr, "rien n'est cree avant Appliquer");
        {
            const int w = indexHaving(pv->warningRows(), "Vanne_NbOuv (DINT)");
            check(w >= 0 && pv->openWarningMenu(static_cast<std::size_t>(w)) && listed(pv->menuItems(), "Retirer cette op\xC3\xA9ration")
                      && listed(pv->menuItems(), "Copier le message"),
                  "un avertissement d'une operation : Retirer, Copier");
            (void)pv->chooseMenuItem("Copier le message");
            check(has(g_clipboard, "Vanne_NbOuv (DINT)"), "Copier le message (l'avertissement)");
        }
        // CHOISIR UNE VARIABLE... : le choix du haut vise Vanne.F0
        check(fixError(*pv, "Vanne.F0", kChoose) && pv->variableShown() == "Vanne.F0", "Choisir une variable... : le choix du haut vise Vanne.F0");
        const auto list = pv->variableItems();
        check(!list.empty() && list.back() == "Cr\xC3\xA9" "er " + q("Vanne_F0") + " (BOOL) dans les variables de l'IHM", "sa liste : Creer Vanne_F0 en dernier");
        int idx = -1;
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == "Pompe_1_Marche" || list[i].rfind("Pompe_1_Marche ", 0) == 0) idx = static_cast<int>(i);
        check(idx > 0 && pv->chooseVariable(idx), "Pompe_1_Marche choisie dans la liste");
        check(indexHaving(pv->errorRows(), "Vanne.F0") < 0 && listed(pv->correctionRows(), q("Vanne.F0") + " \xE2\x86\x92 Pompe_1_Marche | remplac\xC3\xA9" "e"),
              "Vanne.F0 corrigee par la liste du haut");
        check(fixError(*pv, "Vanne.NbFerm", "Utiliser " + q("Vanne_NbFerm_Total")), "Vanne.NbFerm : Utiliser Vanne_NbFerm_Total");
        check(fixError(*pv, "Vanne.RazCpt", "Cr\xC3\xA9" "er la variable " + q("Vanne_RazCpt") + " > BOOL (conseill\xC3\xA9)"), "Vanne.RazCpt : Creer, BOOL");
        // LE CODE BRUT : Copier, puis Retirer cette operation
        const int c = indexHaving(pv->errorRows(), "Code brut");
        check(c >= 0 && pv->openErrorMenu(static_cast<std::size_t>(c)) && indexHaving(pv->menuItems(), "Utiliser") < 0, "le code brut : pas de variable a proposer");
        check(pv->chooseMenuItem("Copier le message") && has(g_clipboard, "Code brut"), "Copier le message");
        check(fixError(*pv, "Code brut", "Retirer cette op\xC3\xA9ration"), "Retirer cette operation");
        check(indexHaving(pv->errorRows(), "Code brut") < 0 && f.as().proposal(m)->excluded == std::vector<std::size_t>{8}, "le code brut retire : son erreur part");
        // DEMANDER A L'IA : la saisie, rien ne part
        const std::size_t messages = f.as().current()->messages.size();
        check(fixError(*pv, "Vanne.F1", kAsk), "Vanne.F1 : Demander a l'IA de corriger");
        f.mm.applyPending();
        check(dynamic_cast<ai::AiPreviewDialog*>(f.mm.top()) == nullptr, "la fenetre se ferme (le texte est dans la saisie)");
        check(has(f.panel->prompt().text(), "Corrige ta proposition") && has(f.panel->prompt().text(), "Vanne.F1"), "la saisie : la demande de correction");
        check(f.as().current()->messages.size() == messages && !f.as().generating(), "rien n'est envoye seul");
        // Vanne.F1 : retirer l'operation qui la lit ; tout est corrige ; Appliquer.
        pv = f.preview(m);
        check(pv && fixError(*pv, "Vanne.F1", "Retirer l'op\xC3\xA9ration qui l'utilise"), "Vanne.F1 : retirer l'operation qui l'utilise");
        if (!pv) return;
        check(pv->errorRows().empty() && pv->check().ok() && pv->applyButton() && pv->applyButton()->enabled(), "plus d'erreur : Appliquer");
        {
            Recorder rec;
            pv->widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), pv->widgetRoot()->bounds(), f.t, nullptr});
            check(rec.saw("CORRECTIONS (8)") && rec.saw("Corrig\xC3\xA9 : 4 variables remplac\xC3\xA9" "es, 2 variables"), "la categorie CORRECTIONS et son resume");
            check(fx::summary(*f.as().proposal(m)) == "4 variables remplac\xC3\xA9" "es, 2 variables \xC3\xA0 cr\xC3\xA9" "er, 2 op\xC3\xA9rations retir\xC3\xA9" "es",
                  "le resume : " + fx::summary(*f.as().proposal(m)));
        }
        const std::size_t vars = f.doc->project.programs.variables.size();
        check(pv->apply(), "Appliquer");
        f.mm.applyPending();
        const auto bound = [&](const std::string& o) { return f.object(o) ? f.object(o)->text("variable") : std::string("(absent)"); };
        check(f.as().proposal(m)->state == "appliquee" && bound("Voyant_Cmd") == "Vanne_Ouverte" && bound("Voyant_Cmd_2") == "Vanne_Ouverte"
                  && bound("Voyant_F0") == "Pompe_1_Marche" && bound("Voyant_NbOuv") == "Vanne_NbOuv" && !f.object("Voyant_F1"),
              "appliquee : la proposition corrigee");
        check(f.doc->project.programs.variables.size() == vars + 2 && f.doc->project.variable("Vanne_NbOuv")
                  && f.doc->project.variable("Vanne_NbOuv")->type == "DINT",
              "les deux variables demandees sont creees");
        (void)f.stack.undo();
        check(f.doc->project.programs.variables.size() == vars && !f.object("Voyant_Cmd"), "un Ctrl+Z reprend tout");
    }
    // RELUE : une autre seance sur le meme dossier - les corrections sont la.
    {
        Fixture g("apercu", false);
        const int i = g.as().indexOf(g.convId);
        check(i >= 0 && g.panel->selectConversation(i), "la conversation se relit");
        const ai::Proposal* p = g.as().proposal(1);
        check(p && p->choices.size() == 6 && p->excluded == std::vector<std::size_t>{4, 8}, "relue : les six choix et les deux retraits");
    }
}

// ---- 3. la carte de la conversation ---------------------------------------------------------------
void testCard() {
    std::printf("[carte]\n");
    Fixture f("carte", true);
    const int m = f.open();
    if (m < 0) return;
    Recorder rec;
    f.render(rec);
    auto notes = f.panel->chat().cardNotes(m);
    check(notes.size() == 3 && has(notes.front(), "Vanne.Cmd") && has(notes.front(), "op\xC3\xA9rations)"), "les notes de la carte : regroupees (Vanne.Cmd, 2 operations)");
    check(f.panel->chat().openNoteMenu(m, 0), "le clic droit sur la premiere note");
    const auto items = f.panel->chat().noteMenuItems();
    check(listed(items, "Utiliser " + q("Vanne_Ouverte")) && listed(items, kChoose) && listed(items, "Retirer les 2 op\xC3\xA9rations qui l'utilisent")
              && listed(items, "Copier le message") && listed(items, kAsk),
          "ses entrees, comme dans la previsualisation");
    check(f.panel->chat().chooseNoteMenuItem("Utiliser " + q("Vanne_Ouverte")), "Utiliser Vanne_Ouverte");
    f.step();
    Recorder rec2;
    f.render(rec2);
    notes = f.panel->chat().cardNotes(m);
    check(!notes.empty() && !has(notes.front(), "Vanne.Cmd") && has(notes.front(), "Vanne.Autorisee"), "la carte se revalide : Vanne.Cmd n'y est plus");
    check(rec2.saw("Corrig\xC3\xA9" "e : 1 variable remplac\xC3\xA9" "e."), "la carte dit ce qui a ete corrige");
    check(std::any_of(f.statuses.begin(), f.statuses.end(), [](const std::string& s) { return has(s, "remplac\xC3\xA9" "e par Vanne_Ouverte"); }),
          "la barre d'etat le dit");
    // Choisir une variable... depuis la carte : la previsualisation, son choix du haut sur cette variable.
    check(f.panel->chat().openNoteMenu(m, 0) && f.panel->chat().chooseNoteMenuItem(kChoose), "Choisir une variable... (Vanne.Autorisee)");
    f.mm.applyPending();
    auto* pv = dynamic_cast<ai::AiPreviewDialog*>(f.mm.top());
    check(pv && pv->variableShown() == "Vanne.Autorisee", "la previsualisation s'ouvre sur Vanne.Autorisee");
    if (pv) pv->close();
    f.mm.applyPending();
    // Copier, Retirer.
    check(f.panel->chat().openNoteMenu(m, 0) && f.panel->chat().chooseNoteMenuItem("Copier le message") && g_clipboard == missingText("Vanne.Autorisee"),
          "Copier le message");
    check(f.panel->chat().openNoteMenu(m, 0) && f.panel->chat().chooseNoteMenuItem("Retirer l'op\xC3\xA9ration qui l'utilise")
              && f.as().proposal(m)->excluded == std::vector<std::size_t>{2},
          "Retirer l'operation qui l'utilise");
    // Revenir (l'API de l'Assistant, comme la ligne CORRECTIONS de la previsualisation).
    check(f.as().clearChoice(m, "Vanne.Cmd") && f.as().excludeOps(m, {2}, false) && f.as().proposal(m)->choices.empty()
              && f.as().proposal(m)->excluded.empty(),
          "revenir sur les deux corrections");
    check(!f.as().chooseVariable(m, "Vanne.Cmd", ai::VariableChoice{}), "un choix vide : refuse");
}

// ---- 4. une grosse proposition : la verification de fond ---------------------------------------------
ai::Proposal bigProposal(hmi::Id view) {
    ai::Proposal p;
    p.id = "op-grosse";
    p.state = "proposee";
    p.view = "Vue_Station";
    p.viewId = view;
    p.request = "Une grosse page de voyants";
    p.summary = "Voyants";
    for (int i = 0; i < 44; ++i)
        p.ops.push_back(voyant("Voyant_" + std::to_string(i + 1), i == 7 || i == 30 ? "Vanne.Cmd" : "Pompe_1_Marche", 20 + (i % 20) * 30));
    return p;
}

void testBackground() {
    std::printf("[grosse proposition]\n");
    Fixture f("grosse", true, bigProposal);
    const int m = f.open();
    if (m < 0) return;
    const auto settle = [&] {
        for (int guard = 0; guard < 2000 && f.panel->chat().checking(m); ++guard) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            f.step();
        }
        f.step();
    };
    settle();
    ai::Check ck;
    check(f.panel->chat().resultReady(m, &ck, nullptr, nullptr) && ck.errors.size() == 2, "44 operations : verifiees sur le fil de fond (2 erreurs)");
    const auto lines = fx::errorLines(ck);
    check(lines.size() == 1 && lines.front().text == missingText("Vanne.Cmd") + kOps2, "une ligne : << ... (2 operations) >>");
    ai::VariableChoice c;
    c.use = "Vanne_Ouverte";
    check(f.as().chooseVariable(m, "Vanne.Cmd", c), "la correction (la proposition seule)");
    f.step();
    settle();
    check(f.panel->chat().resultReady(m, &ck, nullptr, nullptr) && ck.ok(), "la carte se reverifie (son fil de fond) : plus d'erreur");
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
    testEngine();
    testPreview();
    testCard();
    testBackground();
    std::printf("ai_corrections_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
