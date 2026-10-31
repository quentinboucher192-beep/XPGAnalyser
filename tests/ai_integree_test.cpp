// =============================================================================
//  tests/ai_integree_test.cpp - 1.16.0 : l'IA integree (sans connexion)
// -----------------------------------------------------------------------------
//  Un projet d'essai (une vue : une cuve, deux pompes, une vanne, trois boutons,
//  un texte ; des variables BOOL et REAL) et des demandes en francais, avec leurs
//  variantes (accents ou non, majuscules, chiffres ou lettres, pluriels, une
//  faute) :
//    - chaque famille de phrases -> la bonne proposition, VALIDEE sans erreur
//      (AiOperations : validate), appliquee en UN pas (makeCommand), reprise
//      par Ctrl+Z (la vue revient a son empreinte d'avant) ;
//    - une demande non comprise -> une reponse honnete et des suggestions ;
//    - une variable inconnue -> jamais inventee (les plus proches citees, la
//      phrase pour la creer proposee) ; une hesitation -> une question ;
//    - une suite de demandes (et une autre, la meme en bleu, mets-la en rouge) ;
//    - l'Assistant branche sur "integree" (le choix par defaut) : envoyer, la
//      carte, appliquer, Ctrl+Z ; le mode Direct ; les suggestions.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiAutomate.hpp"
#include "../src/app/ai/AiLocal.hpp"
#include "../src/app/ai/AiRegistry.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace ai = app::ai;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& s, std::string_view w) { return s.find(w) != std::string::npos; }

// Le projet d'essai.
struct Bench {
    hmi::DocumentPtr          doc = std::make_shared<hmi::Document>();
    core::CommandStack        stack;
    hmi::Id                   view{hmi::kNoId};
    std::vector<hmi::Id>      selection;
    ai::Env                   env;
    std::vector<ai::LocalTurn> history;

    Bench() {
        auto& p = doc->project;
        hmi::View v = hmi::makeView(p, "Vue_Test");
        v.width = 1600;
        v.height = 900;
        const auto add = [&](hmi::Kind k, const std::string& name, double x, double y, const char* key, const std::string& value) {
            hmi::Object o = hmi::makeObject(k, p.allocate(), name, x, y, v.layers.empty() ? hmi::kNoId : v.layers.front().id);
            if (key) o.set(key, value);
            v.objects.push_back(std::move(o));
        };
        add(hmi::Kind::Tank, "Cuve_1", 100, 100, "label", "Cuve 1");
        add(hmi::Kind::Pump, "Pompe_1", 400, 100, "label", "P1");
        add(hmi::Kind::Pump, "Pompe_2", 560, 100, "label", "P2");
        add(hmi::Kind::Valve, "Vanne_1", 400, 300, "label", "V1");
        add(hmi::Kind::PushButton, "Bouton_1", 100, 420, "text", "Marche");
        add(hmi::Kind::PushButton, "Bouton_2", 300, 440, "text", "Arr\xC3\xAAt");
        add(hmi::Kind::PushButton, "Bouton_3", 520, 410, "text", "Reset");
        add(hmi::Kind::Text, "Texte_1", 100, 20, "text", "Station");
        p.views = {v};
        view = p.views.front().id;
        const auto var = [&](const std::string& name, const std::string& type) {
            hmi::Variable x;
            x.id = p.allocate();
            x.name = name;
            x.type = type;
            x.initial = type == "BOOL" ? "FALSE" : "0";
            p.programs.variables.push_back(x);
        };
        var("Pompe_1_Marche", "BOOL");
        var("Pompe_2_Marche", "BOOL");
        var("Defaut_V1", "BOOL");
        var("Niveau_Cuve_1", "REAL");
        var("Niveau_Cuve_2", "REAL");
    }
    [[nodiscard]] hmi::View& v() { return *doc->project.view(view); }
    [[nodiscard]] const hmi::Object* object(const std::string& name) {
        for (const auto& o : v().objects)
            if (o.name == name) return &o;
        return nullptr;
    }
    [[nodiscard]] ai::PromptInput input() {
        ai::PromptInput in;
        in.project = &doc->project;
        in.view = doc->project.view(view);
        in.selection = selection;
        in.env = env;
        return in;
    }
    // La demande ; la reponse entre dans l'historique (une suite de demandes).
    ai::LocalReply ask(const std::string& text) {
        auto r = ai::understand(input(), text, history);
        history.push_back({text, r.proposal});
        return r;
    }
    [[nodiscard]] ai::Check validate(const ai::LocalReply& r) { return ai::validate(doc->project, *r.proposal, env); }
    // Une proposition valide, appliquee en un pas puis reprise : `after` verifie la vue entre les deux.
    bool applyUndo(const ai::LocalReply& r, const std::function<bool()>& after, std::string* why = nullptr) {
        if (!r.proposal) return false;
        const std::string before = ai::fingerprint(v());
        const std::size_t objects = v().objects.size();
        auto cmd = ai::makeCommand(doc, *r.proposal, why, env);
        if (!cmd) return false;
        if (!stack.push(std::move(cmd))) return false;
        const bool ok = after ? after() : true;
        (void)stack.undo();
        return ok && ai::fingerprint(v()) == before && v().objects.size() == objects;
    }
    bool apply(const ai::LocalReply& r) {
        if (!r.proposal) return false;
        auto cmd = ai::makeCommand(doc, *r.proposal, nullptr, env);
        return cmd && stack.push(std::move(cmd));
    }
};

std::size_t countOps(const ai::LocalReply& r, ai::Operation::Action a) {
    if (!r.proposal) return 0;
    return static_cast<std::size_t>(std::count_if(r.proposal->ops.begin(), r.proposal->ops.end(), [a](const ai::Operation& o) { return o.action == a; }));
}
std::size_t countKind(const ai::LocalReply& r, std::string_view kind) {
    if (!r.proposal) return 0;
    return static_cast<std::size_t>(std::count_if(r.proposal->ops.begin(), r.proposal->ops.end(),
                                                  [&](const ai::Operation& o) { return o.action == ai::Operation::Action::Create && o.kind == kind; }));
}
std::string prop(const ai::Operation& op, std::string_view key) {
    for (const auto& [k, v] : op.props)
        if (k == key) return v;
    return {};
}
const ai::Operation* firstOp(const ai::LocalReply& r) { return r.proposal && !r.proposal->ops.empty() ? &r.proposal->ops.front() : nullptr; }
bool valid(Bench& b, const ai::LocalReply& r, const std::string& what) {
    if (!r.proposal) {
        check(false, what + " : pas de proposition (" + r.text + ")");
        return false;
    }
    const auto ck = b.validate(r);
    std::string errs;
    for (const auto& e : ck.errors) errs += e + " | ";
    check(ck.ok(), what + " : validee (" + errs + ")");
    return ck.ok();
}

// ------------------------------------------------------------------ creer ----
void testCreate() {
    std::printf("[creer]\n");
    Bench b;
    auto r = b.ask("ajoute 3 pompes");
    check(countKind(r, "Pump") == 3, "ajoute 3 pompes : trois Pompe");
    if (valid(b, r, "ajoute 3 pompes")) {
        const auto& ops = r.proposal->ops;
        check(ops[0].name == "Pompe_3" && ops[1].name == "Pompe_4" && ops[2].name == "Pompe_5", "des noms neufs et uniques (Pompe_3, 4, 5) : " + ops[0].name);
        check(prop(ops[0], "label") == "P3", "le libelle P3");
        // une place libre : dans la vue, sans recouvrir l'existant
        bool clear = true, inside = true;
        for (const auto& op : ops) {
            const hmi::Box nb{std::stod(prop(op, "x")), std::stod(prop(op, "y")), std::stod(prop(op, "w")), std::stod(prop(op, "h"))};
            inside = inside && nb.x >= 0 && nb.y >= 0 && nb.right() <= b.v().width && nb.bottom() <= b.v().height;
            for (const auto& o : b.v().objects) clear = clear && !hmi::edit::rotatedBounds(o).intersects(nb);
        }
        check(clear && inside, "une place libre, dans la vue, sans recouvrir l'existant");
        check(std::stod(prop(ops[0], "y")) == std::stod(prop(ops[2], "y")), "en ligne (le meme y)");
        check(b.applyUndo(r, [&] { return b.object("Pompe_3") && b.object("Pompe_5"); }), "appliquee en un pas, reprise par Ctrl+Z");
    }
    check(countKind(b.ask("Ajoute trois pompes"), "Pump") == 3, "trois (en lettres)");
    check(countKind(b.ask("AJOUTE DEUX POMPES"), "Pump") == 2, "en majuscules");
    check(countKind(b.ask("rajoute une pompe stp"), "Pump") == 1, "rajoute une pompe");
    check(countKind(b.ask("ajoute 2 pompe"), "Pump") == 2, "2 pompe (sans s)");
    check(countKind(b.ask("ajoute 2 pommpes"), "Pump") == 2, "une faute de frappe (pommpes)");
    r = b.ask("cr\xC3\xA9" "e une cuve et un bargraphe");
    check(countKind(r, "Tank") == 1 && countKind(r, "Bargraph") == 1, "une cuve et un bargraphe");
    valid(b, r, "cuve et bargraphe");
    check(countKind(b.ask("cree un bac"), "Tank") == 1, "un bac : une cuve (synonyme)");
    check(countKind(b.ask("Ajoute un r\xC3\xA9servoir"), "Tank") == 1, "un reservoir : une cuve");
    check(countKind(b.ask("ajoute un moteur"), "Motor") == 1, "un moteur");
    check(countKind(b.ask("ajoute un voyant TOR"), "Indicator") == 1, "un voyant TOR");
    check(countKind(b.ask("ajoute un bouton poussoir"), "PushButton") == 1, "un bouton poussoir");
    check(countKind(b.ask("cr\xC3\xA9" "e 4 voyants align\xC3\xA9s"), "Indicator") == 4, "cree 4 voyants alignes : creer l'emporte sur aligner");
    check(countKind(b.ask("ajoute un bouton D\xC3\xA9marrer"), "PushButton") == 1, "un bouton Demarrer : un bouton de commande");
    // des boutons Marche et Arret sous la pompe P1
    r = b.ask("mets deux boutons Marche et Arr\xC3\xAAt sous la pompe P1");
    check(countKind(r, "PushButton") == 2, "deux boutons (de commande) Marche et Arret");
    if (valid(b, r, "boutons sous P1")) {
        const auto& ops = r.proposal->ops;
        const auto* p1 = b.object("Pompe_1");
        check(prop(ops[0], "text") == "Marche" && prop(ops[1], "text") == "Arr\xC3\xAAt", "leurs textes");
        check(std::stod(prop(ops[0], "y")) > p1->box().bottom(), "sous la pompe P1");
        check(ops[0].name == "Marche_Pompe_1", "un nom parlant : " + ops[0].name);
    }
    // un voyant rouge a droite de la cuve
    r = b.ask("ajoute un voyant rouge \xC3\xA0 droite de la cuve");
    check(countKind(r, "Indicator") == 1, "un voyant");
    if (valid(b, r, "voyant rouge a droite")) {
        const auto& op = r.proposal->ops.front();
        check(prop(op, "colorOn") == "#E5534B", "rouge : la couleur des defauts du logiciel");
        check(std::stod(prop(op, "x")) >= b.object("Cuve_1")->box().right(), "a droite de la cuve");
    }
    r = b.ask("Ajoute un voyant vert a gauche de V1");
    if (valid(b, r, "voyant vert a gauche de V1"))
        check(std::stod(prop(r.proposal->ops.front(), "x")) + 32 <= b.object("Vanne_1")->box().x && prop(r.proposal->ops.front(), "colorOn") == "#2ECC71",
              "a gauche de V1, vert");
    // des pompes sans nombre : une question
    r = b.ask("ajoute des pompes");
    check(!r.proposal && has(r.text, "Combien") && !r.suggestions.empty(), "des pompes : combien ? (une question)");
    r = b.ask("ajoute un truc bizarre");
    check(!r.proposal, "un genre inconnu : rien d'invente");
}

// ------------------------------------------------------------- synoptique ----
void testSynoptic() {
    std::printf("[synoptique]\n");
    Bench b;
    auto r = b.ask("cree un synoptique de station de pompage avec 3 cuves et 2 pompes par cuve");
    check(countKind(r, "Tank") == 3 && countKind(r, "Pump") == 6 && countKind(r, "PushButton") == 12 && countKind(r, "Valve") == 3,
          "3 cuves, 6 pompes, 12 boutons, 3 vannes");
    if (valid(b, r, "station 3x2"))
        check(b.applyUndo(r, [&] { return b.object("Pompe_11") && b.object("Marche_P32"); }), "le synoptique en un pas, repris par Ctrl+Z");
    r = b.ask("Cr\xC3\xA9" "e-moi un synoptique avec deux cuves et une pompe par cuve");
    check(countKind(r, "Tank") == 2 && countKind(r, "Pump") == 2, "deux cuves, une pompe par cuve (en lettres)");
    valid(b, r, "station 2x1");
    r = b.ask("synoptique de station de pompage avec 2 cuves et 6 pompes sans boutons");
    check(countKind(r, "Pump") == 6 && countKind(r, "PushButton") == 0, "6 pompes en tout (3 par cuve), sans boutons");
}

// ---------------------------------------------------------------- modifier ----
void testModify() {
    std::printf("[modifier]\n");
    Bench b;
    auto r = b.ask("mets P1 en rouge");
    const auto* op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Modify && op->name == "Pompe_1" && prop(*op, "colorOn") == "#E5534B", "mets P1 en rouge : colorOn");
    if (valid(b, r, "P1 en rouge")) check(b.applyUndo(r, [&] { return b.object("Pompe_1")->text("colorOn") == "#E5534B"; }), "appliquee, reprise");
    op = firstOp(r = b.ask("Mets pompe 1 en jaune"));
    check(op && op->name == "Pompe_1" && prop(*op, "colorOn") == "#F2C94C", "pompe 1 (deux mots) en jaune");
    // 1.16.0 (correctif) : sa couleur en marche est deja verte - rien a proposer, et dit
    r = b.ask("Mets pompe 1 en vert");
    check(!r.proposal && has(r.text, "d\xC3\xA9j\xC3\xA0 en **vert**"), "pompe 1 en vert : deja verte, rien de propose (" + r.text + ")");
    op = firstOp(r = b.ask("mets pompe_1 en bleu clair"));
    check(op && op->name == "Pompe_1" && !prop(*op, "colorOn").empty() && prop(*op, "colorOn") != "#2F6FD6", "pompe_1 (casse) en bleu clair");
    op = firstOp(r = b.ask("mets la cuve en jaune"));
    check(op && op->name == "Cuve_1" && prop(*op, "fillColor") == "#F2C94C", "la cuve : la couleur de son contenu");
    valid(b, r, "la cuve en jaune");
    op = firstOp(r = b.ask("mets les pompes en orange"));
    check(countOps(r, ai::Operation::Action::Modify) == 2, "les pompes : les deux");
    r = b.ask("mets la pompe en rouge");
    check(!r.proposal && has(r.text, "Pompe_1") && has(r.text, "Pompe_2"), "la pompe (il y en a deux) : laquelle ? (une question)");
    // la selection : celle de droite
    b.selection = {b.object("Pompe_1")->id, b.object("Pompe_2")->id};
    op = firstOp(r = b.ask("mets celle de droite en rouge"));
    check(op && op->name == "Pompe_2", "celle de droite de la selection");
    b.selection.clear();
    // le texte
    op = firstOp(r = b.ask("change le texte de Bouton_1 en D\xC3\xA9marrer"));
    check(op && op->name == "Bouton_1" && prop(*op, "text") == "D\xC3\xA9marrer", "le texte d'un bouton");
    valid(b, r, "texte");
    op = firstOp(r = b.ask("\xC3\xA9" "cris \"Stop urgence\" sur Bouton_2"));
    check(op && op->name == "Bouton_2" && prop(*op, "text") == "Stop urgence", "un texte entre guillemets");
    // renommer
    r = b.ask("renomme Cuve_1 en Bac_Eau");
    if (valid(b, r, "renommer"))
        check(b.applyUndo(r, [&] { return b.object("Bac_Eau") && !b.object("Cuve_1") && b.object("Bac_Eau")->text("label") == "Cuve 1"; }),
              "renommer : Bac_Eau a la place de Cuve_1, ses proprietes suivent ; Ctrl+Z");
    r = b.ask("renomme Cuve_1 en Pompe_1");
    check(!r.proposal && has(r.text, "pris"), "un nom deja pris : refuse");
    // la taille
    op = firstOp(r = b.ask("agrandis la cuve"));
    check(op && std::stod(prop(*op, "w")) == 150 && std::stod(prop(*op, "h")) == 200, "agrandir : 120x160 -> 150x200");
    valid(b, r, "agrandir");
    op = firstOp(r = b.ask("r\xC3\xA9" "duis V1"));
    check(op && op->name == "Vanne_1" && std::stod(prop(*op, "w")) == 64, "reduire V1");
    op = firstOp(r = b.ask("largeur 200 pour Bouton_3"));
    check(op && prop(*op, "w") == "200" && prop(*op, "h") == "44", "largeur 200 (la hauteur garde)");
    // deplacer
    op = firstOp(r = b.ask("d\xC3\xA9place V1 \xC3\xA0 droite de P2"));
    check(op && op->name == "Vanne_1" && std::stod(prop(*op, "x")) >= b.object("Pompe_2")->box().right(), "deplacer V1 a droite de P2");
    valid(b, r, "deplacer");
    op = firstOp(r = b.ask("monte P1 de 50"));
    check(op && prop(*op, "y") == "50", "monter de 50 (100 -> 50)");
    op = firstOp(r = b.ask("decale Texte_1 de 40 vers la droite"));
    check(op && prop(*op, "x") == "140", "decaler de 40 vers la droite");
    // cacher, montrer
    op = firstOp(r = b.ask("cache V1"));
    check(op && prop(*op, "visible") == "FALSE", "cacher : visible FALSE");
    valid(b, r, "cacher");
    // rendre visible un objet cache (deja visible : rien a proposer, dit)
    r = b.ask("rends visible V1");
    check(!r.proposal && has(r.text, "Rien ne changerait"), "rendre visible V1, deja visible : rien de propose");
    for (auto& o : b.v().objects)
        if (o.name == "Vanne_1") o.set("visible", "FALSE");
    op = firstOp(r = b.ask("rends visible V1"));
    check(op && prop(*op, "visible") == "TRUE", "rendre visible");
    for (auto& o : b.v().objects)
        if (o.name == "Vanne_1") o.set("visible", "TRUE");
}

// ---------------------------------------------------------------- associer ----
void testBind() {
    std::printf("[associer]\n");
    Bench b;
    auto r = b.ask("associe P1 \xC3\xA0 Pompe_1_Marche");
    auto* op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Bind && op->name == "Pompe_1" && op->variable == "Pompe_1_Marche", "associe P1 a Pompe_1_Marche");
    if (valid(b, r, "associer")) check(b.applyUndo(r, [&] { return b.object("Pompe_1")->text("variable") == "Pompe_1_Marche"; }), "appliquee, reprise");
    op = firstOp(r = b.ask("associe p1 a pompe_1_marche"));
    check(op && op->variable == "Pompe_1_Marche", "sans casse : le nom tel que le projet l'ecrit");
    op = firstOp(r = b.ask("relie la pompe 2 \xC3\xA0 Pompe_2_Marchee"));
    check(op && op->name == "Pompe_2" && op->variable == "Pompe_2_Marche", "une faute (une lettre) sur un long nom : la seule proche");
    op = firstOp(r = b.ask("relie la cuve au niveau"));
    check(op && op->name == "Cuve_1" && op->variable == "Niveau_Cuve_1", "relie la cuve au niveau : Niveau_Cuve_1 (le numero de la cuve)");
    valid(b, r, "relier au niveau");
    op = firstOp(r = b.ask("branche P2 sur la marche"));
    check(op && op->variable == "Pompe_2_Marche", "la marche de P2 : Pompe_2_Marche");
    // une hesitation : une question
    r = b.ask("associe la vanne \xC3\xA0 la marche");
    check(!r.proposal && has(r.text, "Pompe_1_Marche") && has(r.text, "Pompe_2_Marche") && !r.suggestions.empty(),
          "deux variables possibles : une question, les candidates citees");
    // une variable inconnue : jamais inventee
    r = b.ask("associe P1 \xC3\xA0 PUMP_01_RUN");
    check(!r.proposal && has(r.text, "n'existe pas"), "PUMP_01_RUN n'existe pas : rien de propose");
    check(std::any_of(r.suggestions.begin(), r.suggestions.end(), [](const std::string& s) { return has(s, "variable PUMP_01_RUN"); }),
          "la phrase pour la creer est proposee");
    r = b.ask("associe P1 \xC3\xA0 Pompe_9_Marche");
    check(!r.proposal && has(r.text, "Pompe_1_Marche"), "une inconnue : les plus proches citees");
    r = b.ask("associe Texte_1 \xC3\xA0 Pompe_1_Marche");
    check(!r.proposal || valid(b, r, "texte associe"), "un texte : associe s'il se relie, sinon dit");
}

// ------------------------------------------------------- variables et vue ----
void testVariablesAndView() {
    std::printf("[variables, vue]\n");
    Bench b;
    auto r = b.ask("cr\xC3\xA9" "e les variables Pompe_3_Marche et Pompe_4_Marche en BOOL");
    auto* op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Variables && op->variables.size() == 2 && op->variables[0].name == "Pompe_3_Marche"
              && op->variables[1].type == "BOOL",
          "creer deux variables BOOL (l'operation creer_variables)");
    if (valid(b, r, "creer des variables")) {
        const std::size_t vars = b.doc->project.programs.variables.size();
        check(b.applyUndo(r, [&] { return b.doc->project.programs.variables.size() == vars + 2; })
                  && b.doc->project.programs.variables.size() == vars,
              "les variables creees en un pas, retirees par Ctrl+Z");
    }
    op = firstOp(r = b.ask("cree une variable Niveau_Cuve REAL"));
    check(op && op->variables.size() == 1 && op->variables[0].type == "REAL" && op->variables[0].initial == "0", "une variable REAL");
    op = firstOp(r = b.ask("cree la variable Temperature_Four"));
    check(op && op->variables.size() == 1 && op->variables[0].type == "REAL" && has(r.text, "tir\xC3\xA9 du nom"), "le type tire du nom, et dit");
    r = b.ask("cree la variable Pompe_1_Marche");
    check(!r.proposal && has(r.text, "existe d\xC3\xA9j\xC3\xA0"), "une variable qui existe : rien a creer");
    r = b.ask("cr\xC3\xA9" "e les variables de l'automate");
    check(!r.proposal && has(r.text, "pas encore lu d'automate") && has(r.text, ".XPG"), "sans automate : dit, et comment joindre le .XPG");
    b.env.plcVariables = [] { return std::vector<std::pair<std::string, std::string>>{{"PT101", "REAL"}, {"PT102", "REAL"}, {"FT201", "REAL"}}; };
    r = b.ask("cree les variables de l'automate qui commencent par PT");
    op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Variables && op->variables.size() == 2 && op->variables[0].name == "PT101",
          "les variables de l'automate qui commencent par PT : les deux (fromPlc)");
    if (r.proposal) valid(b, r, "variables de l'automate");
    r = b.ask("cree les variables de l'automate qui commencent par ZZ");
    check(!r.proposal && has(r.text, "Rien \xC3\xA0 cr\xC3\xA9" "er"), "rien ne commence par ZZ : rien a creer (dit)");
    r = b.ask("propose les types IHM des DDT de l'automate");
    check(r.proposal || has(r.text, "automate"), "les types des DDT : proposes, ou dit");
    b.env = {};
    op = firstOp(r = b.ask("cr\xC3\xA9" "e une vue Vue_Process en 1920x1080"));
    check(op && op->action == ai::Operation::Action::View && op->name == "Vue_Process" && prop(*op, "largeur") == "1920" && prop(*op, "hauteur") == "1080",
          "creer une vue (creer_vue) : son nom, sa taille");
    if (valid(b, r, "creer une vue")) {
        const std::size_t views = b.doc->project.views.size();
        check(b.applyUndo(r, [&] { return b.doc->project.views.size() == views + 1 && b.doc->project.viewByName("Vue_Process"); })
                  && b.doc->project.views.size() == views,
              "la vue creee en un pas, retiree par Ctrl+Z");
    }
    op = firstOp(r = b.ask("cree une nouvelle vue de 1280 x 800"));
    check(op && op->action == ai::Operation::Action::View && prop(*op, "largeur") == "1280" && op->name == "Vue_1", "une vue sans nom : Vue_1, 1280x800");
    r = b.ask("cr\xC3\xA9" "e une vue Vue_Pompes avec 3 pompes et un voyant");
    check(firstOp(r) && firstOp(r)->action == ai::Operation::Action::View && countKind(r, "Pump") == 3 && countKind(r, "Indicator") == 1
              && r.proposal->ops[1].name == "Pompe_1",
          "une vue neuve et ses objets (les noms de la vue neuve : Pompe_1)");
    if (valid(b, r, "vue et objets")) {
        const std::size_t views = b.doc->project.views.size();
        check(b.applyUndo(r, [&] {
                  const auto* nv = b.doc->project.viewByName("Vue_Pompes");
                  return nv && nv->objects.size() == 4;
              }) && b.doc->project.views.size() == views,
              "la vue et ses 4 objets en un pas, repris par Ctrl+Z");
    }
    r = b.ask("cree une vue Vue_Test");
    check(!r.proposal && has(r.text, "existe d\xC3\xA9j\xC3\xA0"), "une vue qui existe : refusee");
}

// ------------------------------------- aligner, repartir, dupliquer, supprimer ----
void testArrange() {
    std::printf("[aligner, repartir, dupliquer, supprimer]\n");
    Bench b;
    auto r = b.ask("aligne les boutons en haut");
    auto* op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Align && op->objects.size() == 3 && op->edge == "haut", "aligner les trois boutons en haut");
    if (valid(b, r, "aligner"))
        check(b.applyUndo(r, [&] { return b.object("Bouton_1")->box().y == b.object("Bouton_2")->box().y; }), "alignes, puis Ctrl+Z");
    op = firstOp(r = b.ask("Aligne les pompes \xC3\xA0 gauche"));
    check(op && op->objects.size() == 2 && op->edge == "gauche", "aligner les pompes a gauche");
    op = firstOp(r = b.ask("r\xC3\xA9partis les boutons"));
    check(op && op->action == ai::Operation::Action::Distribute && op->objects.size() == 3 && op->edge == "horizontal", "repartir les boutons");
    valid(b, r, "repartir");
    r = b.ask("repartis les pompes");
    check(!r.proposal && has(r.text, "trois"), "repartir deux objets : dit qu'il en faut trois");
    op = firstOp(r = b.ask("duplique P1 3 fois vers la droite"));
    check(op && op->action == ai::Operation::Action::Duplicate && op->count == 3 && op->dx > 0 && op->dy == 0 && op->objects.front() == "Pompe_1",
          "dupliquer 3 fois vers la droite");
    if (valid(b, r, "dupliquer")) {
        const std::size_t n = b.v().objects.size();
        check(b.applyUndo(r, [&] { return b.v().objects.size() == n + 3; }), "trois copies en un pas, reprises par Ctrl+Z");
    }
    op = firstOp(r = b.ask("copie la cuve vers le bas"));
    check(op && op->count == 1 && op->dy > 0 && op->dx == 0, "copier vers le bas");
    // des reperes $...$ : ils avancent
    b.v().objects[1].set("label", "$P1$");
    op = firstOp(r = b.ask("duplique Pompe_1 2 fois"));
    check(op && op->markers, "des reperes $...$ : markers");
    b.v().objects[1].set("label", "P1");
    op = firstOp(r = b.ask("supprime les vannes"));
    check(op && op->action == ai::Operation::Action::Remove && op->objects == std::vector<std::string>{"Vanne_1"}, "supprimer les vannes");
    if (valid(b, r, "supprimer")) check(b.applyUndo(r, [&] { return !b.object("Vanne_1"); }), "supprimee, puis Ctrl+Z la rend");
    r = b.ask("efface les boutons");
    check(countOps(r, ai::Operation::Action::Remove) == 1 && firstOp(r)->objects.size() == 3, "efface les boutons : trois");
    if (r.proposal) check(b.validate(r).confirmRemoval, "plusieurs : la confirmation existante");
}

// ------------------------------------------------------------------- animer ----
void testAnimate() {
    std::printf("[animer]\n");
    Bench b;
    auto r = b.ask("fais clignoter V1 quand Defaut_V1");
    auto* op = firstOp(r);
    check(op && op->name == "Vanne_1" && prop(*op, "fault") == "Defaut_V1", "clignoter : le defaut du symbole suit Defaut_V1");
    if (valid(b, r, "clignoter")) check(b.applyUndo(r, [&] { return b.object("Vanne_1")->text("fault") == "Defaut_V1"; }), "applique, repris");
    op = firstOp(r = b.ask("P1 verte si Pompe_1_Marche sinon rouge"));
    check(op && prop(*op, "colorOn") == "#2ECC71" && prop(*op, "colorOff") == "#E5534B" && prop(*op, "variable") == "Pompe_1_Marche",
          "verte si marche sinon rouge : ses couleurs et sa variable");
    valid(b, r, "verte si sinon rouge");
    op = firstOp(r = b.ask("Texte_1 en rouge si Defaut_V1 sinon blanc"));
    check(op && has(prop(*op, "textColor"), "=SEL(Defaut_V1") && has(prop(*op, "textColor"), "#E5534B"), "un texte : une formule SEL (fx)");
    if (valid(b, r, "formule SEL"))
        check(b.applyUndo(r, [&] { return has(b.object("Texte_1")->expr("textColor"), "SEL(Defaut_V1"); }), "la formule posee (fx), reprise par Ctrl+Z");
    op = firstOp(r = b.ask("cache Texte_1 quand Pompe_1_Marche"));
    check(op && prop(*op, "visible") == "=NOT Pompe_1_Marche", "cacher quand : la formule de visible");
    valid(b, r, "visible selon une variable");
    r = b.ask("fais clignoter V1 quand Defaut_Inconnu");
    check(!r.proposal && has(r.text, "n'existe pas"), "une condition sur une variable inconnue : jamais inventee");
}

// --------------------------------------------------------------- questions ----
void testQuestions() {
    std::printf("[questions]\n");
    Bench b;
    auto r = b.ask("Que sais-tu faire ?");
    check(!r.proposal && has(r.text, "Ce que je comprends") && r.suggestions.size() >= 4, "que sais-tu faire : la liste et des exemples");
    check(std::any_of(r.suggestions.begin(), r.suggestions.end(), [](const std::string& s) { return has(s, "Pompe_1"); }),
          "des exemples sur les vrais noms du projet");
    r = b.ask("combien d'objets ?");
    check(!r.proposal && has(r.text, "8 objets"), "combien d'objets : 8");
    r = b.ask("combien de pompes");
    check(has(r.text, "2 pompes") && has(r.text, "Pompe_2"), "combien de pompes : 2, nommees");
    r = b.ask("quelles variables ?");
    check(has(r.text, "Pompe_1_Marche") && has(r.text, "REAL") && has(r.text, "5 variables"), "quelles variables : nom et type");
    r = b.ask("quelles variables de niveau ?");
    check(has(r.text, "Niveau_Cuve_1") && !has(r.text, "Defaut_V1"), "un filtre : les variables de niveau");
    r = b.ask("quels genres d'objets connais-tu ?");
    check(has(r.text, "Pompe") && has(r.text, "Synoptique") && has(r.text, "genres"), "quels genres : par famille");
    r = b.ask("quelles vues ?");
    check(has(r.text, "Vue_Test"), "quelles vues");
    // pas compris : honnete, des suggestions
    r = b.ask("quel temps fera-t-il demain \xC3\xA0 Lyon");
    check(!r.proposal && has(r.text, "pas compris") && !r.suggestions.empty(), "non compris : une reponse honnete et des suggestions");
    r = b.ask("blabla");
    check(!r.proposal && !r.suggestions.empty(), "un mot sans sens : des suggestions");
    // sans vue ouverte
    ai::PromptInput none;
    none.project = &b.doc->project;
    r = ai::understand(none, "ajoute une pompe");
    check(!r.proposal && has(r.text, "Ouvre d'abord une vue"), "sans vue : le dit");
    check(ai::localSuggestions().size() >= 4, "les suggestions de la conversation vide");
}

// --------------------------------------------------------------- une suite ----
void testFollowUp() {
    std::printf("[une suite]\n");
    Bench b;
    auto r = b.ask("ajoute une pompe");
    check(countKind(r, "Pump") == 1, "une pompe");
    r = b.ask("et une autre");
    check(countKind(r, "Pump") == 1 && r.proposal->ops.front().name == "Pompe_3", "et une autre : une pompe de plus (" + (r.proposal ? r.proposal->ops.front().name : std::string("-")) + ")");
    valid(b, r, "et une autre");
    r = b.ask("la m\xC3\xAAme en bleu");
    check(countKind(r, "Pump") == 1 && prop(r.proposal->ops.front(), "colorOn") == "#2F6FD6", "la meme en bleu");
    r = b.ask("encore deux");
    check(countKind(r, "Pump") == 2, "encore deux");
    // le dernier objet cree, applique : "mets-le en rouge"
    Bench c;
    r = c.ask("ajoute un voyant");
    check(c.apply(r) && c.object("Voyant_1"), "un voyant, applique");
    auto* op = firstOp(r = c.ask("mets-le en rouge"));
    check(op && op->name == "Voyant_1" && prop(*op, "colorOn") == "#E5534B", "mets-le en rouge : le dernier cree");
    // pareil pour un autre objet
    r = c.ask("mets P1 en jaune");
    op = firstOp(r = c.ask("pareil pour P2"));
    check(op && op->name == "Pompe_2" && prop(*op, "colorOn") == "#F2C94C", "pareil pour P2 : la meme modification");
    r = ai::understand(c.input(), "et une autre", {});
    check(!r.proposal && !r.suggestions.empty(), "une suite sans rien avant : dit, et suggere");
}

// ------------------------------------------------ l'Assistant sur "integree" ----
void testAssistant() {
    std::printf("[assistant integree]\n");
    Bench b;
    ai::Assistant::Host host;
    host.document = [&b] { return b.doc; };
    host.activeView = [&b] { return b.view; };
    host.selection = [&b] { return b.selection; };
    host.apply = [&b](core::CommandPtr c) { (void)b.stack.push(std::move(c)); };
    ai::Assistant a(host);
    a.load();
    check(a.settings().provider == "integree" && a.local() && !a.live(), "une installation neuve : l'IA integree par defaut");
    check(a.connectionText() == "IA int\xC3\xA9gr\xC3\xA9" "e \xE2\x80\x93 sans connexion", "l'en-tete : " + a.connectionText());
    check(a.ready() && !a.needsSetup(), "aucune configuration (ni modele ni cle)");
    a.newConversation();
    check(a.suggestions() == ai::localSuggestions(), "la conversation vide : les phrases qu'elle comprend");
    double t = 10;
    const auto run = [&] {
        for (int k = 0; k < 4000 && a.generating(); ++k) a.tick(t += 0.05);
    };
    check(a.send("ajoute 2 pompes"), "envoyer");
    a.tick(t += 0.05);
    check(a.phase() == ai::Assistant::Phase::Waiting || a.phase() == ai::Assistant::Phase::Thinking, "un court etat d'attente, de reflexion");
    run();
    const auto* c = a.current();
    const int m = c ? static_cast<int>(c->messages.size()) - 1 : -1;
    const ai::Proposal* p = a.proposal(m);
    check(p && p->state == "proposee" && p->create == 2, "la carte : 2 objets a creer");
    check(c && c->messages.back().text.find("Pompe_3") != std::string::npos && c->messages.back().state == "fini", "la reponse ecrite");
    check(!a.canRetry(), "Reessayer n'a pas de sens");
    const std::size_t before = b.v().objects.size();
    std::string why;
    check(a.apply(m, &why) && b.v().objects.size() == before + 2, "appliquer (" + why + ")");
    check(a.appliedState(m) == ai::Assistant::Applied::Applied, "l'etat reel de la carte : appliquee");
    (void)b.stack.undo();
    check(b.v().objects.size() == before && a.appliedState(m) == ai::Assistant::Applied::Undone, "Ctrl+Z : la vue d'avant, la carte le dit");
    // une suite dans la meme conversation (les tours d'avant passent)
    check(a.send("et une autre"), "une suite");
    run();
    p = a.proposal(static_cast<int>(a.current()->messages.size()) - 1);
    check(p && p->create == 1, "et une autre : une pompe");
    // pas compris : les suggestions sous la reponse
    check(a.send("raconte-moi une blague"), "une demande hors sujet");
    run();
    check(a.suggestionsAfterLast() && !a.suggestions().empty() && a.suggestions() != ai::localSuggestions(), "ses suggestions, sous la reponse");
    check(!a.proposal(static_cast<int>(a.current()->messages.size()) - 1), "aucune proposition");
    // le mode Direct : appliquee des la reponse
    a.setDirectMode(true);
    check(a.send("mets P1 en rouge"), "mode Direct");
    run();
    p = a.proposal(static_cast<int>(a.current()->messages.size()) - 1);
    check(p && p->state == "appliquee" && b.object("Pompe_1")->text("colorOn") == "#E5534B", "mode Direct : appliquee des la reponse");
    (void)b.stack.undo();
    check(b.object("Pompe_1")->text("colorOn") != "#E5534B", "et reprise par Ctrl+Z");
    a.setDirectMode(false);
    // la demonstration reste : ses phrases fixes
    ai::Settings st = a.settings();
    st.provider = "demo";
    a.setSettings(st);
    check(!a.local() && a.suggestions() == ai::demoSuggestions() && a.connectionText().find("D\xC3\xA9monstration") == 0,
          "la demonstration : ses suggestions, son en-tete");
}


// ======================================= 1.16.0 : les correctifs des captures (IA integree) ====
std::size_t chars(const std::string& s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}
bool shortAll(const std::vector<std::string>& v) {
    return std::all_of(v.begin(), v.end(), [](const std::string& s) { return chars(s) <= 45; });
}
std::size_t opsOn(const ai::LocalReply& r, const std::string& name) {
    if (!r.proposal) return 0;
    return static_cast<std::size_t>(std::count_if(r.proposal->ops.begin(), r.proposal->ops.end(), [&](const ai::Operation& o) { return o.name == name; }));
}
void addObject(Bench& b, hmi::Kind k, const std::string& name, double x, double y, const char* key = nullptr, const std::string& value = {}) {
    auto& v = b.v();
    hmi::Object o = hmi::makeObject(k, b.doc->project.allocate(), name, x, y, v.layers.empty() ? hmi::kNoId : v.layers.front().id);
    if (key) o.set(key, value);
    v.objects.push_back(std::move(o));
}
void removeObject(Bench& b, const std::string& name) {
    auto& objs = b.v().objects;
    objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const hmi::Object& o) { return o.name == name; }), objs.end());
}

// 1. Un objet cite qui n'existe pas : dit, jamais remplace par le dernier objet cite.
void testMissingObject() {
    std::printf("[correctifs : un objet introuvable]\n");
    Bench b;
    auto r = b.ask("mets la cuve en jaune");   // le dernier objet cite : Cuve_1
    check(firstOp(r) && firstOp(r)->name == "Cuve_1", "la cuve citee");
    r = b.ask("Mets Bouton_9 en rouge");
    check(!r.proposal, "Bouton_9 n'existe pas : aucune proposition (" + (r.proposal ? r.proposal->ops.front().name : std::string("-")) + ")");
    check(has(r.text, "Je ne trouve pas \xC2\xAB Bouton_9 \xC2\xBB dans la vue") && has(r.text, "`Bouton_1`") && !has(r.text, "Cuve_1"),
          "Bouton_9 : je ne le trouve pas, les plus proches (Bouton_1...) : " + r.text);
    check(std::any_of(r.suggestions.begin(), r.suggestions.end(), [](const std::string& s) { return s == "Mets Bouton_1 en rouge"; }),
          "la demande refaite sur le plus proche, a cliquer");
    r = b.ask("Largeur 200 pour Bouton_9");
    check(!r.proposal && has(r.text, "Bouton_9"), "largeur 200 pour Bouton_9 : rien sur Cuve_1");
    r = b.ask("Cache Texte_9 quand Pompe_1_Marche");
    check(!r.proposal && has(r.text, "Texte_9"), "cache Texte_9 quand... : rien de cache");
    r = b.ask("Ajoute un voyant \xC3\xA0 droite de Pompe_9");
    check(!r.proposal && has(r.text, "Pompe_9"), "un voyant a droite de Pompe_9 (absente) : dit, rien pose ailleurs");
    r = b.ask("Supprime P7");
    check(!r.proposal && has(r.text, "P7"), "supprime P7 (absent) : rien supprime");
    r = b.ask("mets la cuve en vert");
    r = b.ask("Mets D\xC3\xA9marreur en rouge");
    check(!r.proposal && has(r.text, "Je ne trouve pas \xC2\xAB D\xC3\xA9marreur"), "un mot a majuscule qui n'est rien de connu : dit, pas la cuve (" + r.text + ")");
    // le repli sur le dernier objet cite reste pour un pronom
    r = b.ask("mets la cuve en jaune");
    auto* op = firstOp(r = b.ask("Mets-la en rouge"));
    check(op && op->name == "Cuve_1" && prop(*op, "fillColor") == "#E5534B", "mets-la en rouge : la cuve citee juste avant");
}

// 2. "Le texte de X" : une propriete de X, pas le genre Texte.
void testTextOf() {
    std::printf("[correctifs : le texte de]\n");
    Bench b;
    for (auto& o : b.v().objects)
        if (o.name == "Texte_1") o.set("text", "Texte");   // le texte par defaut d'un Texte neuf
    auto r = b.ask("Change le texte de Bouton_1 en D\xC3\xA9marrer");
    check(r.proposal && r.proposal->ops.size() == 1 && opsOn(r, "Bouton_1") == 1 && opsOn(r, "Texte_1") == 0,
          "le texte de Bouton_1 : Bouton_1 seul, pas Texte_1 (" + r.text + ")");
    valid(b, r, "le texte de Bouton_1");
    r = b.ask("Mets le texte de Bouton_2 en rouge");
    check(firstOp(r) && firstOp(r)->name == "Bouton_2" && prop(*firstOp(r), "textColor") == "#E5534B", "le texte de Bouton_2 en rouge : sa couleur de texte");
    removeObject(b, "Bouton_1");
    r = b.ask("Change le texte de Bouton_1 en D\xC3\xA9marrer");
    check(!r.proposal && has(r.text, "Je ne trouve pas \xC2\xAB Bouton_1") && !has(r.text, "pas de texte"),
          "sans Bouton_1 : je ne le trouve pas (pas \"il n'y a pas de texte\") : " + r.text);
}

// 3, 4. Une designation ambigue : demander ; le clignotement des objets qui savent.
void testAmbiguous() {
    std::printf("[correctifs : V1, P1 ambigus, clignoter]\n");
    Bench b;
    addObject(b, hmi::Kind::Indicator, "Voyant_1", 900, 100);   // son abrege : v1 (comme Vanne_1)
    auto r = b.ask("Cache V1");
    check(r.proposal && r.proposal->ops.size() == 1 && opsOn(r, "Vanne_1") == 1, "cache V1 : la vanne (son libelle V1), pas aussi Voyant_1");
    r = b.ask("D\xC3\xA9place V1 \xC3\xA0 droite de P2");
    check(r.proposal && r.proposal->ops.size() == 1 && opsOn(r, "Vanne_1") == 1, "deplace V1 : la vanne seule");
    r = b.ask("Fais clignoter V1 quand Defaut_V1");
    check(r.proposal && opsOn(r, "Vanne_1") == 1 && opsOn(r, "Voyant_1") == 0 && prop(r.proposal->ops.front(), "fault") == "Defaut_V1",
          "fais clignoter V1 (une vanne et un voyant dans la vue) : la vanne (" + r.text + ")");
    check(!has(r.text, "La formule ne lit"), "pas de formule posee : pas de phrase sur la formule");
    // plusieurs objets : ceux qui savent clignoter ; les autres dits a part
    r = b.ask("Fais clignoter la vanne et le voyant quand Defaut_V1");
    check(r.proposal && opsOn(r, "Vanne_1") == 1 && opsOn(r, "Voyant_1") == 0 && has(r.text, "Voyant_1"),
          "la vanne et le voyant : la vanne clignote, le voyant est dit a part (" + r.text + ")");
    // deux objets portent le libelle P1 (Dupliquer garde le libelle) : demander
    r = b.ask("duplique P1 3 fois vers la droite");
    check(b.apply(r) && b.object("Pompe_3") && b.object("Pompe_3")->text("label") == "P1", "trois copies de P1 (libelle P1 garde)");
    r = b.ask("P1 verte si Pompe_1_Marche sinon rouge");
    check(!r.proposal && has(r.text, "Pompe_1") && has(r.text, "Pompe_3") && has(r.text, "Lequel"),
          "P1 designe 4 pompes : une question, pas 4 modifications (" + r.text + ")");
    check(std::any_of(r.suggestions.begin(), r.suggestions.end(), [](const std::string& s) { return s == "Pompe_1 verte si Pompe_1_Marche sinon rouge"; }),
          "la demande refaite sur chaque candidat");
    r = b.ask("Pompe_1 verte si Pompe_1_Marche sinon rouge");
    check(r.proposal && r.proposal->ops.size() == 1 && opsOn(r, "Pompe_1") == 1, "le nom exact : un seul objet");
    r = b.ask("mets les pompes en orange");
    check(countOps(r, ai::Operation::Action::Modify) == 5, "les pompes (un pluriel) : toutes");
    // un texte qui designe : "Marche" est le texte de Bouton_1
    r = b.ask("mets Marche en vert");
    check(r.proposal && opsOn(r, "Bouton_1") == 1, "par son texte : Marche -> Bouton_1");
}

// 5. Les placements : sans recouvrir, dans la vue.
void testPlacement() {
    std::printf("[correctifs : placer]\n");
    Bench b;
    addObject(b, hmi::Kind::Pump, "Pompe_3", 676, 100, "label", "P3");   // la place juste a droite de P2
    auto r = b.ask("D\xC3\xA9place V1 \xC3\xA0 droite de P2");
    const auto* op = firstOp(r);
    bool clear = op != nullptr;
    if (op) {
        const auto* v1 = b.object("Vanne_1");
        const hmi::Box nb{std::stod(prop(*op, "x")), std::stod(prop(*op, "y")), v1->box().w, v1->box().h};
        for (const auto& o : b.v().objects)
            if (o.name != "Vanne_1" && hmi::edit::rotatedBounds(o).intersects(nb)) clear = false;
        check(nb.x >= b.object("Pompe_2")->box().right() && nb.right() <= b.v().width, "a droite de P2, dans la vue");
    }
    check(clear, "V1 a droite de P2 sans recouvrir P3 (la place prise)");
    check(has(r.text, "prise par `Pompe_3`"), "la reponse dit le decalage : " + r.text);
    valid(b, r, "deplacer sans recouvrir");
    op = firstOp(r = b.ask("Monte P1 de 150"));
    check(op && prop(*op, "y") == "0" && has(r.text, "bord de la vue"), "monte P1 de 150 (y = 100) : borne a y = 0, et dit (" + r.text + ")");
    r = b.ask("Monte Texte_1 de 50");
    check(op && firstOp(r) && prop(*firstOp(r), "y") == "0", "monte Texte_1 de 50 (y = 20) : y = 0");
}

// 6. Une operation qui ne change rien ne se propose pas.
void testAlreadyDone() {
    std::printf("[correctifs : deja fait]\n");
    Bench b;
    for (auto& o : b.v().objects)
        if (o.name.rfind("Bouton_", 0) == 0) {
            auto bx = o.box();
            bx.y = 420;
            o.setBox(bx);
        }
    auto r = b.ask("Aligne les boutons en haut");
    check(!r.proposal && has(r.text, "Les boutons sont d\xC3\xA9j\xC3\xA0 align\xC3\xA9s en haut"),
          "deux boutons deja alignes : dit, rien de propose (" + r.text + ")");
    // repartis a ecarts egaux
    double x = 100;
    for (auto& o : b.v().objects)
        if (o.name.rfind("Bouton_", 0) == 0) {
            auto bx = o.box();
            bx.x = x;
            o.setBox(bx);
            x += bx.w + 50;
        }
    r = b.ask("R\xC3\xA9partis les boutons");
    check(!r.proposal && has(r.text, "d\xC3\xA9j\xC3\xA0 r\xC3\xA9partis"), "deja repartis : dit, rien de propose (" + r.text + ")");
    r = b.ask("Aligne les pompes en haut");   // Pompe_1 et Pompe_2 : y = 100
    check(!r.proposal && has(r.text, "Les pompes sont d\xC3\xA9j\xC3\xA0 align\xC3\xA9" "es en haut"), "les pompes deja alignees (accorde) : " + r.text);
    r = b.ask("mets P1 en rouge");
    check(b.apply(r), "P1 en rouge, applique");
    r = b.ask("mets P1 en rouge");
    check(!r.proposal && has(r.text, "d\xC3\xA9j\xC3\xA0 en **rouge**"), "P1 deja rouge : rien de propose");
}

// 7, 8. Chaque variable son type ; creer un type IHM.
void testTypes() {
    std::printf("[correctifs : types]\n");
    Bench b;
    auto r = b.ask("Cr\xC3\xA9" "e les variables Niveau_Cuve_3 en REAL et Defaut_P1 en BOOL");
    auto* op = firstOp(r);
    check(op && op->variables.size() == 2 && op->variables[0].type == "REAL" && op->variables[1].type == "BOOL" && op->variables[0].initial == "0",
          "X en REAL et Y en BOOL : chacune son type");
    op = firstOp(r = b.ask("Cr\xC3\xA9" "e les variables Niveau_A et Niveau_B en BOOL"));
    check(op && op->variables.size() == 2 && op->variables[0].type == "BOOL" && op->variables[1].type == "BOOL" && !has(r.text, "tir\xC3\xA9 du nom"),
          "X et Y en BOOL : les deux en BOOL (pas tire du nom)");
    op = firstOp(r = b.ask("Cr\xC3\xA9" "e les variables Vitesse_X : REAL, Marche_X : BOOL"));
    check(op && op->variables.size() == 2 && op->variables[0].type == "REAL" && op->variables[1].type == "BOOL", "X : REAL, Y : BOOL");
    op = firstOp(r = b.ask("Cr\xC3\xA9" "e les variables Vitesse_P9 et Defaut_P9"));
    check(op && op->variables.size() == 2 && op->variables[0].type == "REAL" && op->variables[1].type == "BOOL" && has(r.text, "tir\xC3\xA9 du nom"),
          "sans type dit : tire du nom, et dit");
    // un type IHM
    r = b.ask("Cr\xC3\xA9" "e un type T_Pompe avec Marche en BOOL et Vitesse en REAL");
    op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Type && op->name == "T_Pompe" && op->members.size() == 2 && op->members[0].name == "Marche"
              && op->members[0].type == "BOOL" && op->members[1].name == "Vitesse" && op->members[1].type == "REAL",
          "cree un type T_Pompe avec Marche en BOOL et Vitesse en REAL : creer_type (" + r.text + ")");
    if (valid(b, r, "creer un type"))
        check(b.applyUndo(r, [&] { return b.doc->project.hmiTypeByName("T_Pompe") != nullptr; }) && !b.doc->project.hmiTypeByName("T_Pompe"),
              "le type cree en un pas, retire par Ctrl+Z");
    op = firstOp(r = b.ask("Cr\xC3\xA9" "e une structure T_Vanne avec Ouverte, Fermee et Niveau"));
    check(op && op->action == ai::Operation::Action::Type && op->members.size() == 3 && op->members[0].type == "BOOL" && op->members[2].type == "REAL"
              && has(r.text, "tir\xC3\xA9 du nom"),
          "une structure, des membres sans type : tires du nom, et dit");
    r = b.ask("Cr\xC3\xA9" "e un type");
    check(!r.proposal && has(r.text, "Quel nom"), "un type sans nom : demande son nom");
    // la portee : une vue neuve est visee par son nom (comme les IA en ligne), des variables ne visent aucune vue
    r = b.ask("Cr\xC3\xA9" "e une vue Vue_Process avec 2 pompes");
    check(r.proposal && r.proposal->view == "Vue_Process" && r.proposal->viewId == hmi::kNoId && r.proposal->fingerprint.rfind("vue:absente", 0) == 0,
          "creer une vue : la proposition vise la vue neuve (son nom, vue:absente) : " + (r.proposal ? r.proposal->view : std::string("-")));
    r = b.ask("Cr\xC3\xA9" "e la variable Debit_Z en REAL");
    check(r.proposal && r.proposal->view.empty() && r.proposal->viewId == hmi::kNoId, "des variables seules : aucune vue visee");
}

// 9. Les textes.
void testTexts() {
    std::printf("[correctifs : textes]\n");
    Bench b;
    auto r = b.ask("Combien d'objets ?");
    check(has(r.text, "2 pompes") && has(r.text, "3 boutons \xC3\xA0 impulsion") && !has(r.text, "2 pompe,"), "les pluriels accordes : " + r.text);
    r = b.ask("ajoute deux afficheurs num\xC3\xA9riques");
    check(countKind(r, "NumericDisplay") == 2 && has(r.text, "2 afficheurs num\xC3\xA9riques"), "2 afficheurs numeriques (le nom et l'adjectif)");
    r = b.ask("ajoute un voyant rouge \xC3\xA0 droite de la cuve");
    check(has(r.text, "En rouge : `Voyant_1`"), "la couleur dite : " + r.text);
    r = b.ask("Fais clignoter V1 quand Defaut_V1");
    check(!has(r.text, "La formule"), "un defaut lie par une variable : pas de phrase sur une formule");
    r = b.ask("Texte_1 en rouge si Defaut_V1 sinon blanc");
    check(has(r.text, "La formule"), "une formule posee : la phrase sur la formule");
    r = b.ask("Quels genres d'objets ?");
    check(has(r.text, std::to_string(ai::registry().kinds().size()) + " genres"), "quels genres : le compte du registre (" + std::to_string(ai::registry().kinds().size()) + ")");
    // relie la cuve au niveau, sans variable de niveau : les candidates d'abord, puis la phrase pour la creer
    auto& vars = b.doc->project.programs.variables;
    vars.erase(std::remove_if(vars.begin(), vars.end(), [](const hmi::Variable& v) { return v.name.rfind("Niveau", 0) == 0; }), vars.end());
    r = b.ask("Relie la cuve au niveau");
    const auto create = r.text.find("cr\xC3\xA9" "e-la d'abord");
    const auto first = r.text.find("\n- ");
    check(!r.proposal && create != std::string::npos && first != std::string::npos && (first == create - 3 || has(r.text, "les plus proches")),
          "la liste commence par les candidates (ou par la creation, sans \"ou\") : " + r.text);
    check(!has(r.text, "\n- ou cr") || r.text.find("les plus proches") < r.text.find("\n- ou cr"), "\"ou cree-la\" seulement apres des candidates");
    check(has(r.text, "Niveau_Cuve_1"), "un nom parlant propose a la creation (Niveau_Cuve_1), pas \"niveau\"");
}

// 10. Sans automate relie.
void testNoPlc() {
    std::printf("[correctifs : sans automate]\n");
    Bench b;
    // comme dans le logiciel : les listes sont branchees, mais vides sans automate
    b.env.plcVariables = [] { return std::vector<std::pair<std::string, std::string>>{}; };
    b.env.plcTypes = [] { return std::vector<ai::Env::PlcType>{}; };
    auto r = b.ask("Cr\xC3\xA9" "e les variables de l'automate qui commencent par PT");
    check(!r.proposal && has(r.text, "pas encore lu d'automate") && !has(r.text, "Rien \xC3\xA0 cr\xC3\xA9" "er"),
          "variables de l'automate, sans automate : dit (" + r.text + ")");
    r = b.ask("Propose les types IHM des DDT");
    check(!r.proposal && has(r.text, "pas encore lu d'automate"), "les types des DDT, sans automate : dit");
    // avec un automate : les types qu'elles emploient d'abord
    b.env.plcVariables = [] { return std::vector<std::pair<std::string, std::string>>{{"PT101", "T_Mesure"}, {"PT102", "REAL"}}; };
    b.env.plcTypes = [] { return std::vector<ai::Env::PlcType>{{"T_Mesure", "", {{"Valeur", "REAL", ""}, {"Defaut", "BOOL", ""}}}}; };
    r = b.ask("Cr\xC3\xA9" "e les variables de l'automate qui commencent par PT");
    check(r.proposal && r.proposal->ops.size() == 2 && r.proposal->ops[0].action == ai::Operation::Action::Type && r.proposal->ops[0].name == "T_Mesure"
              && r.proposal->ops[1].action == ai::Operation::Action::Variables,
          "les variables de l'automate : le type qu'elles emploient d'abord");
    r = b.ask("Propose les types IHM des DDT");
    check(firstOp(r) && firstOp(r)->action == ai::Operation::Action::Type && firstOp(r)->name == "T_Mesure", "propose les types IHM des DDT : creer_type");
}

// 11. Les suggestions : des objets qui existent, courtes.
void testSuggestions() {
    std::printf("[correctifs : suggestions]\n");
    Bench b;
    b.v().objects.clear();
    auto r = b.ask("blabla");
    check(!r.suggestions.empty() && std::none_of(r.suggestions.begin(), r.suggestions.end(), [](const std::string& s) { return has(s, "Pompe_1") || has(s, "Mets "); }),
          "vue vide : des suggestions pour creer, aucun objet cite");
    check(shortAll(r.suggestions), "vue vide : des suggestions courtes");
    Bench c;
    r = c.ask("Que sais-tu faire ?");
    check(shortAll(r.suggestions) && shortAll(ai::localSuggestions()), "des suggestions de 45 caracteres au plus");
    // les formulations du texte (entre `...`) tiennent aussi sur une ligne
    bool ok = true;
    for (std::size_t a = r.text.find('`'); a != std::string::npos; a = r.text.find('`', a + 1)) {
        const auto z = r.text.find('`', a + 1);
        if (z == std::string::npos) break;
        if (chars(r.text.substr(a + 1, z - a - 1)) > 45) ok = false;
        a = z;
    }
    check(ok, "que sais-tu faire : chaque formulation tient en 45 caracteres");
    check(has(r.text, "type T_Pompe"), "que sais-tu faire : creer un type");
    r = c.ask("Mets Bouton_9 en rouge");
    check(shortAll(r.suggestions), "les suggestions d'un objet introuvable : courtes");
}

// Les phrases de l'aide (F1, Ce qu'elle comprend) qui n'avaient pas leur essai.
void testHelpPhrases() {
    std::printf("[les phrases de l'aide]\n");
    Bench b;
    auto r = b.ask("Mets la cuve en bleu clair");
    check(firstOp(r) && firstOp(r)->name == "Cuve_1" && !prop(*firstOp(r), "fillColor").empty() && prop(*firstOp(r), "fillColor") != "#2F6FD6",
          "mets la cuve en bleu clair");
    r = b.ask("Cr\xC3\xA9" "e un synoptique de station de pompage avec 3 cuves et 2 pompes par cuve sans vannes");
    check(countKind(r, "Tank") == 3 && countKind(r, "Valve") == 0, "un synoptique sans vannes");
    r = b.ask("Quels genres d'objets ?");
    check(!r.proposal && has(r.text, "Synoptique"), "quels genres d'objets ?");
    r = b.ask("Synoptique de 3 cuves et 2 pompes par cuve");
    check(countKind(r, "Tank") == 3 && countKind(r, "Pump") == 6, "la suggestion courte du synoptique");
    Bench c;
    r = c.ask("ajoute un voyant");
    check(c.apply(r), "un voyant, applique");
    auto* op = firstOp(r = c.ask("Mets-la en rouge"));
    check(op && op->name == "Voyant_1", "mets-la en rouge : le dernier cree");
    for (const auto& s : ai::localSuggestions()) {
        Bench d;
        r = d.ask(s);
        check(r.proposal || has(r.text, "Ce que je comprends") || has(r.text, "objets"), "la suggestion " + s + " : comprise");
    }
}


// ======================================= 1.16.1 : comprendre la demande, pas la decouper ====
std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Les noms qu'une proposition pose : objets, vue, type et ses membres, variables, copies.
std::vector<std::string> proposedNames(const ai::LocalReply& r) {
    std::vector<std::string> out;
    if (!r.proposal) return out;
    for (const auto& op : r.proposal->ops) {
        if (op.action == ai::Operation::Action::Create || op.action == ai::Operation::Action::View || op.action == ai::Operation::Action::Type)
            out.push_back(op.name);
        for (const auto& m : op.members) out.push_back(m.name);
        for (const auto& v : op.variables) out.push_back(v.name);
        for (const auto& n : op.names) out.push_back(n);
    }
    return out;
}
std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}
// Un nom absurde : un mot-outil du francais, un mot de remplissage, ou un verbe d'une demande.
bool absurdName(const std::string& n) {
    static const std::set<std::string> k = {
        "pour", "qui", "que", "avec", "de", "du", "des", "le", "la", "les", "l", "un", "une", "son", "sa", "ses", "mon", "ma", "mes", "ce",
        "cet", "cette", "nouveau", "nouvelle", "et", "ou", "en", "a", "au", "aux", "dont", "par", "sur", "dans", "il", "elle", "je", "tu",
        "me", "moi", "s", "d", "j", "exemple", "etc", "gestion", "membres", "membre", "champs", "donnee", "donnees", "truc", "machin",
        "quelque", "chose", "notamment", "comme", "gerer", "gere", "appelle", "appele", "appelee", "nomme", "nommee", "cree", "creer",
        "creez", "ajoute", "ajouter", "rajoute", "mets", "met", "mettre", "pose", "place", "fais", "faire", "veux", "voudrais", "peux",
        "faut", "faudrait", "aimerais", "definis"};
    return k.count(lowerAscii(n)) != 0;
}
const ai::Operation* typeOp(const ai::LocalReply& r) {
    if (!r.proposal) return nullptr;
    for (const auto& op : r.proposal->ops)
        if (op.action == ai::Operation::Action::Type) return &op;
    return nullptr;
}
const ai::Operation::MemberSpec* member(const ai::Operation& op, const std::string& name) {
    for (const auto& m : op.members)
        if (m.name == name) return &m;
    return nullptr;
}
std::string memberList(const ai::Operation* op) {
    if (!op) return "-";
    std::string s = op->name + " :";
    for (const auto& m : op->members) s += " " + m.name + "/" + m.type;
    return s;
}
bool asciiIdentifier(const std::string& s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// 1.16.1 - 1. Creer un type : le nom et les membres compris dans la phrase (la capture de l'utilisateur).
void testTypeUnderstood() {
    std::printf("[1.16.1 : creer un type, compris]\n");
    Bench b;
    auto r = b.ask("Cr\xC3\xA9" "e un type pour g\xC3\xA9rer les capteurs");
    const auto* op = typeOp(r);
    check(op && op->name == "T_Capteur", "un type pour gerer les capteurs : T_Capteur, pas << pour >> (" + memberList(op) + " " + r.text + ")");
    check(op && !member(*op, "Gerer") && !member(*op, "capteurs") && member(*op, "Mesure"), "ses membres : un modele, pas Gerer ni capteurs");
    if (op) valid(b, r, "un type pour gerer les capteurs");
    r = b.ask("Non je veux cr\xC3\xA9" "er une structure qui s'appelle Capteur avec des membres de gestion, par exemple son type, sa mesure etc, ses defauts");
    op = typeOp(r);
    check(op && op->name == "Capteur", "une structure qui s'appelle Capteur : le type Capteur, pas << qui >> (" + memberList(op) + " " + r.text + ")");
    if (op) {
        check(op->members.size() >= 3 && op->members[0].name == "Type" && op->members[1].name == "Mesure" && op->members[2].name == "Defauts",
              "ses membres d'abord : Type, Mesure, Defauts (" + memberList(op) + ")");
        check(op->members.size() >= 3 && op->members[0].type == "STRING" && op->members[1].type == "REAL" && op->members[2].type == "BOOL",
              "leurs types : STRING, REAL, BOOL (" + memberList(op) + ")");
        for (const char* bad : {"appelle", "Capteur", "gestion", "Gestion", "exemple", "Exemple", "etc", "Etc", "qui", "membres", "Membres"})
            check(!member(*op, bad), std::string("aucun membre << ") + bad + " >> (" + memberList(op) + ")");
        check(op->members.size() == 3 || has(r.text, "compl\xC3\xA9t\xC3\xA9"), "des membres ajoutes au modele : dits (" + r.text + ")");
        valid(b, r, "la structure Capteur");
    }
    op = typeOp(r = b.ask("Cr\xC3\xA9" "e une structure nomm\xC3\xA9" "e Mesure_Debit avec une mesure, un d\xC3\xA9" "faut et un \xC3\xA9tat"));
    check(op && op->name == "Mesure_Debit" && op->members.size() == 3 && op->members[0].name == "Mesure" && op->members[0].type == "REAL"
              && op->members[1].name == "Defaut" && op->members[1].type == "BOOL" && op->members[2].name == "Etat"
              && (op->members[2].type == "INT" || op->members[2].type == "BOOL"),
          "avec une mesure, un defaut et un etat : Mesure REAL, Defaut BOOL, Etat (" + memberList(op) + ")");
    op = typeOp(r = b.ask("cr\xC3\xA9" "e un type appel\xC3\xA9 T_Analog comme la mesure et l'unit\xC3\xA9"));
    check(op && op->name == "T_Analog" && op->members.size() == 2 && op->members[0].name == "Mesure" && op->members[0].type == "REAL"
              && op->members[1].name == "Unite" && op->members[1].type == "STRING",
          "comme la mesure et l'unite : Mesure REAL, Unite STRING (" + memberList(op) + ")");
    op = typeOp(r = b.ask("Cr\xC3\xA9" "e un type T_Cuve avec un niveau, un volume et une alarme haute"));
    check(op && op->name == "T_Cuve" && op->members.size() == 3 && op->members[0].name == "Niveau" && op->members[0].type == "REAL"
              && op->members[1].name == "Volume" && op->members[1].type == "REAL" && op->members[2].name == "AlarmeHaute" && op->members[2].type == "BOOL",
          "un niveau, un volume et une alarme haute : Niveau, Volume, AlarmeHaute (" + memberList(op) + ")");
    op = typeOp(r = b.ask("Je veux une structure Moteur avec marche, d\xC3\xA9" "faut, courant"));
    check(op && op->name == "Moteur" && op->members.size() == 3 && op->members[0].name == "Marche" && op->members[0].type == "BOOL"
              && op->members[1].name == "Defaut" && op->members[2].name == "Courant" && op->members[2].type == "REAL",
          "des membres en minuscules : Marche, Defaut, Courant (" + memberList(op) + ")");
    op = typeOp(r = b.ask("D\xC3\xA9" "finis une structure T_Regul avec Mesure, Consigne et Sortie en REAL"));
    check(op && op->members.size() == 3 && std::all_of(op->members.begin(), op->members.end(), [](const auto& m) { return m.type == "REAL"; }),
          "Mesure, Consigne et Sortie en REAL (" + memberList(op) + ")");
    // sans nom sur, sans modele : elle demande
    r = b.ask("Cr\xC3\xA9" "e un type avec Marche et Defaut");
    check(!r.proposal && has(r.text, "nom"), "un type sans nom (des membres, aucun modele) : son nom demande (" + r.text + ")");
    r = b.ask("Cr\xC3\xA9" "e un nouveau type");
    check(!r.proposal && has(r.text, "Quel nom"), "un nouveau type : son nom demande, pas << nouveau >> (" + r.text + ")");
    // "de type ..." precise un objet : ce n'est pas un type a creer
    r = b.ask("Ajoute une pompe de type centrifuge");
    check(countKind(r, "Pump") == 1 && !typeOp(r), "une pompe de type centrifuge : une pompe, pas un type (" + r.text + ")");
}

// 1.16.1 - 2. Les modeles metier : un type sans membres dits part d'un modele classique, et le dit.
void testTypeModels() {
    std::printf("[1.16.1 : les modeles metier]\n");
    Bench b;
    struct Case {
        std::string                                       phrase, name;
        std::vector<std::pair<std::string, std::string>>  must;   // un type vide : seulement present
    };
    const std::vector<Case> cases = {
        {"Cr\xC3\xA9" "e un type pour g\xC3\xA9rer les capteurs", "T_Capteur",
         {{"Mesure", "REAL"}, {"Unite", "STRING"}, {"Min", "REAL"}, {"Max", "REAL"}, {"SeuilHaut", "REAL"}, {"SeuilBas", "REAL"},
          {"AlarmeHaute", "BOOL"}, {"AlarmeBasse", "BOOL"}, {"Defaut", "BOOL"}}},
        {"il me faut un type pour un capteur TOR", "T_CapteurTOR", {{"Etat", "BOOL"}, {"Defaut", "BOOL"}}},
        {"Cr\xC3\xA9" "e une structure pour une pompe", "T_Pompe", {{"Marche", "BOOL"}, {"Defaut", "BOOL"}, {"Auto", "BOOL"}, {"Commande", "BOOL"}, {"HeuresMarche", ""}}},
        {"une structure pour une pompe", "T_Pompe", {{"Marche", "BOOL"}}},
        {"Cr\xC3\xA9" "e un type Vanne", "Vanne", {{"Ouverte", "BOOL"}, {"Fermee", "BOOL"}, {"Commande", "BOOL"}, {"Defaut", "BOOL"}}},
        {"un type Vanne", "Vanne", {{"Ouverte", "BOOL"}}},
        {"je voudrais un type moteur", "T_Moteur", {{"Marche", "BOOL"}, {"Defaut", "BOOL"}}},
        {"peux-tu me faire une structure pour un variateur ?", "T_Variateur", {{"Consigne", "REAL"}, {"Vitesse", "REAL"}}},
        {"Cr\xC3\xA9" "e un type pour une cuve", "T_Cuve", {{"Niveau", "REAL"}}},
        {"Il me faut un type de donn\xC3\xA9" "es pour un capteur de pression", "T_Capteur", {{"Mesure", "REAL"}}},
        {"Pourrais-tu me cr\xC3\xA9" "er un type Pompe ?", "Pompe", {{"Marche", "BOOL"}}},
    };
    static const std::set<std::string> known = {"BOOL", "INT", "DINT", "UINT", "UDINT", "WORD", "DWORD", "REAL", "LREAL", "STRING", "TIME"};
    for (const auto& c : cases) {
        const auto r = ai::understand(b.input(), c.phrase, {});
        const auto* op = typeOp(r);
        check(op && op->name == c.name, c.phrase + " : le type " + c.name + " (" + memberList(op) + " " + r.text + ")");
        if (!op) continue;
        check(r.proposal->ops.size() == 1, c.phrase + " : un type seul (aucun objet pose)");
        for (const auto& [n, t] : c.must) {
            const auto* m = member(*op, n);
            check(m && (t.empty() || m->type == t), c.phrase + " : le membre " + n + " " + t + " (" + memberList(op) + ")");
        }
        bool clean = true, described = true;
        for (const auto& m : op->members) {
            clean = clean && asciiIdentifier(m.name) && known.count(m.type);
            described = described && !m.description.empty();
        }
        check(clean, c.phrase + " : des noms sans accent, des types connus (" + memberList(op) + ")");
        check(described && std::any_of(op->props.begin(), op->props.end(), [](const auto& kv) { return kv.first == "description" && !kv.second.empty(); }),
              c.phrase + " : le type et ses membres decrits");
        check(has(r.text, "mod\xC3\xA8le de d\xC3\xA9part") && has(r.text, "adapter") && has(r.text, "aper\xC3\xA7u"),
              c.phrase + " : dit (un modele de depart, a adapter, l'apercu) : " + r.text);
        valid(b, r, c.phrase);
    }
    // le nom d'un modele deja pris : dit, et un autre propose
    auto r = b.ask("Cr\xC3\xA9" "e une structure pour une pompe");
    check(b.apply(r) && b.doc->project.hmiTypeByName("T_Pompe"), "T_Pompe cree (applique)");
    r = b.ask("Cr\xC3\xA9" "e une structure pour une pompe");
    const auto* op = typeOp(r);
    check(op && op->name != "T_Pompe" && op->name.rfind("T_Pompe", 0) == 0 && has(r.text, "existe d\xC3\xA9j\xC3\xA0"),
          "T_Pompe pris : un autre nom propose, et dit (" + memberList(op) + " " + r.text + ")");
    if (op) valid(b, r, "le modele sous un autre nom");
    r = b.ask("Cr\xC3\xA9" "e un type T_Pompe avec Marche en BOOL");
    check(!r.proposal && has(r.text, "existe d\xC3\xA9j\xC3\xA0") && has(r.text, "T_Pompe_2"), "un nom donne deja pris : dit, un autre nom propose (" + r.text + ")");
    // des membres dits : les siens seulement (le modele ne s'ajoute que sur << par exemple >>, << etc >>)
    const auto* op2 = typeOp(r = b.ask("Cr\xC3\xA9" "e un type T_Vanne2 pour une vanne avec Ouverte et Fermee"));
    check(op2 && op2->members.size() == 2, "une vanne avec Ouverte et Fermee : ces deux membres (" + memberList(op2) + ")");
    op2 = typeOp(r = b.ask("Cr\xC3\xA9" "e un type T_Vanne3 pour une vanne avec par exemple Ouverte et Fermee"));
    check(op2 && op2->members.size() > 2 && op2->members[0].name == "Ouverte" && member(*op2, "Commande") && has(r.text, "compl\xC3\xA9t\xC3\xA9"),
          "par exemple : les siens d'abord, le modele complete, et dit (" + memberList(op2) + ")");
}

// 1.16.1 - 3. Le garde-fou : aucun nom propose n'est un mot-outil ni un verbe de la demande.
void testNameGuard() {
    std::printf("[1.16.1 : jamais un mot-outil pour nom]\n");
    Bench b;
    for (const std::string phrase : {"ajoute une pompe appel\xC3\xA9" "e Pour", "cr\xC3\xA9" "e une vue qui s'appelle Avec", "renomme Cuve_1 en Pour",
                                     "Cr\xC3\xA9" "e une structure qui s'appelle Nouveau", "Cr\xC3\xA9" "e une structure qui s'appelle Gerer avec une mesure",
                                     "Cr\xC3\xA9" "e la variable Pour en BOOL", "ajoute un voyant nomm\xC3\xA9 Avec", "cr\xC3\xA9" "e une vue appel\xC3\xA9" "e De"}) {
        const auto r = ai::understand(b.input(), phrase, {});
        check(!r.proposal && !r.text.empty(), phrase + " : aucune proposition, une question (" + joined(proposedNames(r)) + ")");
    }
    // un vrai nom passe
    auto r = b.ask("ajoute une pompe appel\xC3\xA9" "e Pompe_Eau");
    check(r.proposal && r.proposal->ops.size() == 1 && r.proposal->ops.front().name == "Pompe_Eau", "un vrai nom : pris");
}

// 1.16.1 - 4. Une demande trop mal comprise : understood = false (l'Assistant peut la passer a une IA connectee).
void testUnderstoodFlag() {
    std::printf("[1.16.1 : understood]\n");
    Bench b;
    for (const std::string phrase : {"Cr\xC3\xA9" "e un type pour le truc", "il me faut un machin pour le process", "fais un truc sympa",
                                     "je veux cr\xC3\xA9" "er quelque chose", "raconte-moi une blague"}) {
        const auto r = ai::understand(b.input(), phrase, {});
        check(!r.proposal && !r.understood && !r.text.empty() && !r.suggestions.empty() && shortAll(r.suggestions),
              phrase + " : understood = false, un texte honnete et des suggestions (" + r.text + ")");
    }
    for (const std::string phrase : {"ajoute 3 pompes", "Cr\xC3\xA9" "e un type pour g\xC3\xA9rer les capteurs", "mets P1 en rouge", "Cr\xC3\xA9" "e un type",
                                     "supprime la pompe", "Que sais-tu faire ?"}) {
        const auto r = ai::understand(b.input(), phrase, {});
        check(r.understood, phrase + " : compris (une proposition, ou une question sur ce qui manque)");
    }
}

// 1.16.1 (captures sous Wine) : les defauts vus - un membre << Duree en INT >>, un poeme, un objet cree ET associe.
void testCaptures1161() {
    std::printf("[1.16.1 : captures]\n");
    Bench b;
    auto r = b.ask("Cr\xC3\xA9" "e un type T_CfgPurge avec Debit en REAL, Duree en INT et Active en BOOL");
    const ai::Operation* op = firstOp(r);
    const auto member = [&](const char* n, const char* t) {
        return op && std::any_of(op->members.begin(), op->members.end(), [&](const auto& m) { return m.name == n && m.type == t; });
    };
    check(op && op->members.size() == 3 && member("Debit", "REAL") && member("Duree", "INT") && member("Active", "BOOL"),
          "Duree en INT : un membre (le mot << duree >> n'est plus pris pour le type TIME)");
    r = b.ask("Cr\xC3\xA9" "e un type T_Cycle avec Temps en TIME, Duree en INT et Compteur en DINT");
    op = firstOp(r);
    check(op && op->members.size() == 3 && member("Temps", "TIME") && member("Duree", "INT") && member("Compteur", "DINT"), "Temps, Duree, Compteur");
    r = ai::understand(b.input(), "\xC3\x89" "cris-moi un po\xC3\xA8me sur les pompes centrifuges", {});
    check(!r.proposal && !r.understood, "un poeme : pas comprise (une IA connectee peut la reprendre)");
    r = ai::understand(b.input(), "change le texte du bouton", {});
    check(!r.proposal && !r.understood && has(r.text, "Quel texte"), "le texte sans le texte : la question, pas comprise");
    const auto prop = [](const ai::Operation* o, const char* k) {
        std::string v;
        if (o)
            for (const auto& [kk, vv] : o->props)
                if (kk == k) v = vv;
        return v;
    };
    r = b.ask("Ajoute un voyant li\xC3\xA9 \xC3\xA0 Pompe_1_Marche");
    op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Create && prop(op, "variable") == "Pompe_1_Marche" && op->name != "Pompe_1_Marche"
              && r.proposal->ops.size() == 1,
          "un voyant lie a Pompe_1_Marche : cree ET associe (" + r.text + ")");
    if (op) valid(b, r, "voyant lie");
    r = b.ask("Ajoute un voyant associ\xC3\xA9 \xC3\xA0 Pompe_2_Marche");
    op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Create && prop(op, "variable") == "Pompe_2_Marche", "associe a : pareil (" + r.text + ")");
    r = b.ask("Ajoute un voyant li\xC3\xA9 \xC3\xA0 Variable_Qui_N_Existe_Pas");
    op = firstOp(r);
    check(op && op->action == ai::Operation::Action::Create && prop(op, "variable").empty() && r.proposal->ops.size() == 1 && has(r.text, "n'existe pas"),
          "une variable inconnue : le voyant seul, et pourquoi - jamais inventee (" + r.text + ")");
}

// 1.16.1 - 5. Un corpus de robustesse : des phrases d'automaticien. Chacune donne la bonne proposition, ou
// une question, ou understood = false - jamais une proposition absurde (un nom pris dans un mot-outil).
using Pred = std::function<bool(const ai::LocalReply&)>;
Pred makes(std::string kind, std::size_t n) {
    return [=](const ai::LocalReply& r) { return countKind(r, kind) == n && r.proposal->ops.size() == n; };
}
Pred makesNamed(std::string kind, std::string name) {
    return [=](const ai::LocalReply& r) { return countKind(r, kind) == 1 && r.proposal->ops.front().name == name; };
}
Pred modifies(std::string name) {
    return [=](const ai::LocalReply& r) { return r.proposal && countOps(r, ai::Operation::Action::Modify) >= 1 && opsOn(r, name) == r.proposal->ops.size(); };
}
Pred binds(std::string name, std::string var) {
    return [=](const ai::LocalReply& r) {
        return r.proposal && r.proposal->ops.size() == 1 && r.proposal->ops[0].action == ai::Operation::Action::Bind && r.proposal->ops[0].name == name
            && r.proposal->ops[0].variable == var;
    };
}
Pred vars(std::vector<std::pair<std::string, std::string>> want) {
    return [=](const ai::LocalReply& r) {
        const auto* op = firstOp(r);
        if (!op || op->action != ai::Operation::Action::Variables || op->variables.size() != want.size()) return false;
        for (std::size_t i = 0; i < want.size(); ++i)
            if (op->variables[i].name != want[i].first || op->variables[i].type != want[i].second) return false;
        return true;
    };
}
Pred typed(std::string name, std::vector<std::string> members) {
    return [=](const ai::LocalReply& r) {
        const auto* op = typeOp(r);
        if (!op || op->name != name) return false;
        return std::all_of(members.begin(), members.end(), [&](const std::string& m) { return member(*op, m) != nullptr; });
    };
}
Pred dups(std::string name) {
    return [=](const ai::LocalReply& r) {
        const auto* op = firstOp(r);
        return op && op->action == ai::Operation::Action::Duplicate && op->objects.size() == 1 && op->objects[0] == name;
    };
}
Pred renames(std::string from, std::string to) {
    return [=](const ai::LocalReply& r) {
        const auto* op = firstOp(r);
        return op && op->action == ai::Operation::Action::Duplicate && op->objects.size() == 1 && op->objects[0] == from && op->names.size() == 1
            && op->names[0] == to;
    };
}
Pred removes(std::string name) {
    return [=](const ai::LocalReply& r) {
        const auto* op = firstOp(r);
        return op && op->action == ai::Operation::Action::Remove && op->objects.size() == 1 && op->objects[0] == name;
    };
}
Pred newView(std::string name) {
    return [=](const ai::LocalReply& r) {
        const auto* op = firstOp(r);
        return op && op->action == ai::Operation::Action::View && (name.empty() ? op->name.rfind("Vue_", 0) == 0 : op->name == name);
    };
}
Pred asks() {
    return [](const ai::LocalReply& r) { return !r.proposal && r.understood; };
}
Pred lost() {
    return [](const ai::LocalReply& r) { return !r.proposal && !r.understood; };
}

void testCorpus() {
    std::printf("[1.16.1 : le corpus de robustesse]\n");
    Bench b;
    const std::vector<std::pair<std::string, Pred>> corpus = {
        // creer des objets
        {"je voudrais 2 pompes", makes("Pump", 2)},
        {"peux-tu ajouter un voyant rouge ?", makes("Indicator", 1)},
        {"il me faut une cuve", makes("Tank", 1)},
        {"rajoute moi deux vannes", makes("Valve", 2)},
        {"pose un bouton Marche sous P1", makes("PushButton", 1)},
        {"place un voyant a droite de la cuve", makes("Indicator", 1)},
        {"Pourriez-vous ajouter trois voyants ?", makes("Indicator", 3)},
        {"mets moi un afficheur numerique", makes("NumericDisplay", 1)},
        {"ajoute un bargraphe stp", makes("Bargraph", 1)},
        {"Je veux un moteur \xC3\xA0 gauche de P1", makes("Motor", 1)},
        {"ajouter une pompe", makes("Pump", 1)},
        {"Cr\xC3\xA9" "e 2 boutons Marche et Arr\xC3\xAAt", makes("PushButton", 2)},
        {"met une vanne sous la cuve", makes("Valve", 1)},
        {"il faudrait un voyant vert", makes("Indicator", 1)},
        {"fais-moi une pompe", makes("Pump", 1)},
        {"dessine une cuve", makes("Tank", 1)},
        {"Ajoutez un moteur s'il vous plait", makes("Motor", 1)},
        {"j'aimerais avoir 4 voyants", makes("Indicator", 4)},
        {"tu peux me rajouter une pompe \xC3\xA0 droite de P2 ?", makes("Pump", 1)},
        {"je souhaite ajouter un moteur", makes("Motor", 1)},
        {"J'ai besoin d'un voyant rouge", makes("Indicator", 1)},
        {"rajoute deux boutons Marche et Arret sous la cuve", makes("PushButton", 2)},
        {"il me faudrait une vanne a gauche de la cuve", makes("Valve", 1)},
        {"Ajoute une pompe qui s'appelle Pompe_Gavage", makesNamed("Pump", "Pompe_Gavage")},
        {"rajoute un voyant D\xC3\xA9" "faut \xC3\xA0 c\xC3\xB4t\xC3\xA9 de la vanne", makes("Indicator", 1)},
        // des variables
        {"Cr\xC3\xA9" "e la variable Debit_Entree en REAL", vars({{"Debit_Entree", "REAL"}})},
        {"je voudrais une variable Marche_P3 de type BOOL", vars({{"Marche_P3", "BOOL"}})},
        {"peux-tu cr\xC3\xA9" "er les variables Temp_1 et Temp_2 en r\xC3\xA9" "el ?", vars({{"Temp_1", "REAL"}, {"Temp_2", "REAL"}})},
        {"il me faut une variable bool\xC3\xA9" "enne Alarme_Generale", vars({{"Alarme_Generale", "BOOL"}})},
        {"Ajoute une variable Consigne_Vitesse", vars({{"Consigne_Vitesse", "REAL"}})},
        {"Cr\xC3\xA9" "ez la variable Compteur_Pieces en INT", vars({{"Compteur_Pieces", "INT"}})},
        {"Peux-tu cr\xC3\xA9" "er une variable Niveau_Bac de type r\xC3\xA9" "el", vars({{"Niveau_Bac", "REAL"}})},
        {"cr\xC3\xA9" "e moi les variables Marche_P5 et Arret_P5", vars({{"Marche_P5", "BOOL"}, {"Arret_P5", "BOOL"}})},
        {"je voudrais une variable", asks()},
        // des types
        {"Cr\xC3\xA9" "e un type pour g\xC3\xA9rer les capteurs", typed("T_Capteur", {"Mesure", "Unite", "Defaut"})},
        {"Non je veux cr\xC3\xA9" "er une structure qui s'appelle Capteur avec des membres de gestion, par exemple son type, sa mesure etc, ses defauts",
         typed("Capteur", {"Type", "Mesure", "Defauts"})},
        {"Cr\xC3\xA9" "e une structure pour une pompe", typed("T_Pompe", {"Marche", "Defaut", "Auto", "Commande", "HeuresMarche"})},
        {"Cr\xC3\xA9" "e un type Vanne", typed("Vanne", {"Ouverte", "Fermee", "Commande", "Defaut"})},
        {"je voudrais un type moteur", typed("T_Moteur", {"Marche", "Defaut"})},
        {"peux-tu me faire une structure pour un variateur ?", typed("T_Variateur", {"Consigne", "Vitesse"})},
        {"Cr\xC3\xA9" "e un type T_Cuve avec un niveau, un volume et une alarme haute", typed("T_Cuve", {"Niveau", "Volume", "AlarmeHaute"})},
        {"Cr\xC3\xA9" "e une structure nomm\xC3\xA9" "e Mesure_Debit avec une mesure, un d\xC3\xA9" "faut et un \xC3\xA9tat", typed("Mesure_Debit", {"Mesure", "Defaut", "Etat"})},
        {"cr\xC3\xA9" "e un type appel\xC3\xA9 T_Analog comme la mesure et l'unit\xC3\xA9", typed("T_Analog", {"Mesure", "Unite"})},
        {"il me faut un type pour un capteur TOR", typed("T_CapteurTOR", {"Etat", "Defaut"})},
        {"D\xC3\xA9" "finis une structure T_Regul avec Mesure, Consigne et Sortie en REAL", typed("T_Regul", {"Mesure", "Consigne", "Sortie"})},
        {"cr\xC3\xA9" "er un type de donn\xC3\xA9" "e pour les moteurs", typed("T_Moteur", {"Marche"})},
        {"Pourrais-tu me cr\xC3\xA9" "er un type Pompe ?", typed("Pompe", {"Marche"})},
        {"Je veux une structure Moteur avec marche, d\xC3\xA9" "faut, courant", typed("Moteur", {"Marche", "Defaut", "Courant"})},
        {"Cr\xC3\xA9" "e un nouveau type", asks()},
        {"Cr\xC3\xA9" "e un type pour le truc", lost()},
        {"Ajoute une pompe de type centrifuge", makes("Pump", 1)},
        // modifier
        {"peux-tu mettre la cuve en bleu ?", modifies("Cuve_1")},
        {"je voudrais que la vanne soit rouge", modifies("Vanne_1")},
        {"change la couleur de P2 en jaune", modifies("Pompe_2")},
        {"agrandis la cuve", modifies("Cuve_1")},
        {"Pouvez-vous d\xC3\xA9placer V1 \xC3\xA0 droite de P2 ?", modifies("Vanne_1")},
        {"renomme Cuve_1 en Bac_Eau", renames("Cuve_1", "Bac_Eau")},
        {"cache Texte_1", modifies("Texte_1")},
        {"change le texte de Bouton_1 en D\xC3\xA9marrer", modifies("Bouton_1")},
        {"d\xC3\xA9" "cale P2 vers la droite de 50", modifies("Pompe_2")},
        {"rends la cuve rouge", modifies("Cuve_1")},
        {"colorie P2 en orange", modifies("Pompe_2")},
        {"met la pompe 1 en rouge", modifies("Pompe_1")},
        // associer
        {"associe P1 a Pompe_1_Marche", binds("Pompe_1", "Pompe_1_Marche")},
        {"relie la vanne \xC3\xA0 Defaut_V1", binds("Vanne_1", "Defaut_V1")},
        {"peux-tu lier P2 \xC3\xA0 Pompe_2_Marche ?", binds("Pompe_2", "Pompe_2_Marche")},
        {"branche Pompe_2 sur Pompe_2_Marche", binds("Pompe_2", "Pompe_2_Marche")},
        {"pourrais-tu associer la pompe 1 \xC3\xA0 Pompe_1_Marche", binds("Pompe_1", "Pompe_1_Marche")},
        // animer
        {"fais clignoter V1 quand Defaut_V1", modifies("Vanne_1")},
        {"P1 en vert si Pompe_1_Marche sinon rouge", modifies("Pompe_1")},
        {"je voudrais que la vanne clignote quand Defaut_V1", modifies("Vanne_1")},
        {"cache Texte_1 quand Pompe_1_Marche", modifies("Texte_1")},
        {"je voudrais que P1 clignote si Defaut_V1", modifies("Pompe_1")},
        // dupliquer
        {"duplique V1", dups("Vanne_1")},
        {"copie la cuve 2 fois", dups("Cuve_1")},
        {"peux-tu dupliquer V1 vers la droite ?", dups("Vanne_1")},
        {"fais 3 copies de Pompe_2", dups("Pompe_2")},
        {"je voudrais dupliquer la vanne", dups("Vanne_1")},
        {"fais une copie de la cuve", dups("Cuve_1")},
        // des vues
        {"je voudrais cr\xC3\xA9" "er une vue Synoptique_2", newView("Synoptique_2")},
        {"il me faut une nouvelle vue", newView("")},
        {"je voudrais une nouvelle vue Vue_Pompes", newView("Vue_Pompes")},
        // supprimer, et le flou
        {"tu peux supprimer V1 ?", removes("Vanne_1")},
        {"enl\xC3\xA8ve Bouton_3", removes("Bouton_3")},
        {"supprime la pompe", asks()},
        {"fais un truc sympa", lost()},
        {"il me faut un machin pour le process", lost()},
        {"je veux voir le niveau de la cuve", lost()},
        {"Ajoute un champ Vitesse au type T_Pompe", lost()},
        {"Mets-moi 3 pompes en ligne", makes("Pump", 3)},
        {"mets un titre Station de pompage", [](const ai::LocalReply& r) { return countKind(r, "Text") == 1 && r.proposal->ops.size() == 1 && prop(r.proposal->ops[0], "text") == "Station de pompage"; }},
        {"Il me faut 2 boutons marche et arr\xC3\xAAt", [](const ai::LocalReply& r) { return countKind(r, "PushButton") == 2 && prop(r.proposal->ops[0], "text") == "Marche"; }},
        {"place la vanne sous la cuve", modifies("Vanne_1")},
        {"Cr\xC3\xA9" "e un type T_Dosage pour une pompe", typed("T_Dosage", {"Marche", "Defaut"})},
        {"Cr\xC3\xA9" "e un type T_Moteur avec heures de marche, temps de d\xC3\xA9marrage et nombre de d\xC3\xA9marrages", typed("T_Moteur", {"HeuresMarche", "TempsDemarrage", "NombreDemarrages"})},
        {"Je voudrais cr\xC3\xA9" "er une structure avec des champs : marche, arr\xC3\xAAt, d\xC3\xA9" "faut. Appelle-la T_Cmd", typed("T_Cmd", {"Marche", "Arret", "Defaut"})},
        {"je voudrais une structure pour un r\xC3\xA9gulateur PID", typed("T_Regulateur", {"Mesure", "Consigne", "Sortie"})},
        {"je veux cr\xC3\xA9" "er quelque chose", lost()},
    };
    std::size_t good = 0;
    for (const auto& [phrase, expect] : corpus) {
        const auto r = ai::understand(b.input(), phrase, {});
        bool ok = expect(r);
        // jamais absurde : une proposition valide, aucun nom pris dans un mot-outil ou un verbe
        if (r.proposal) {
            const auto ck = b.validate(r);
            std::vector<std::string> bad;
            for (const auto& n : proposedNames(r))
                if (absurdName(n)) bad.push_back(n);
            check(ck.ok() && bad.empty(), phrase + " : une proposition valide, sans nom absurde (" + joined(bad) + (ck.ok() ? "" : " " + ck.errors.front()) + ")");
        } else {
            check(!r.text.empty() && shortAll(r.suggestions), phrase + " : une reponse honnete");
        }
        check(ok, phrase + " : attendu (" + joined(proposedNames(r)) + " | " + r.text.substr(0, 160) + ")");
        good += ok ? 1 : 0;
    }
    check(corpus.size() >= 60, "au moins 60 phrases");
    std::printf("  corpus : %zu/%zu phrases comprises comme attendu\n", good, corpus.size());
}

} // namespace

// 1.16.0 (captures) : IHM Studio n'a pas d'automate relie - le .XPG joint est lu (AiAutomate : readPlcFile,
// fillPlcEnv), puis ses globales et ses DDT se proposent en variables et types IHM.
void testPlcFile(const char* xpg) {
    std::printf("[l'automate lu dans un .XPG joint]\n");
    check(ai::isPlcExport("MAST.XPG") && ai::isPlcExport("prog.xef") && !ai::isPlcExport("plan.pdf") && !ai::isPlcExport("cfg.xhw"),
          "les exports reconnus : .XPG, .XEF (pas un PDF, pas la configuration .XHW seule)");
    if (!xpg) {
        check(false, "le chemin de MAST.XPG manque (argument de l'essai)");
        return;
    }
    std::string why;
    const auto f = ai::readPlcFile(xpg, &why);
    check(f && f->project && f->name == "MAST.XPG" && f->variables > 100 && f->types == 28,
          "MAST.XPG lu : " + (f ? std::to_string(f->variables) + " globales, " + std::to_string(f->types) + " DDT" : why));
    check(!ai::readPlcFile("/chemin/absent.XPG", &why) && !why.empty(), "un fichier absent : refuse, avec la raison");
    if (!f) return;
    Bench b;
    ai::fillPlcEnv(b.env, [p = f->project] { return p; });
    b.env.plcSource = [] { return std::string("MAST.XPG"); };
    const auto vars = b.env.plcVariables();
    const auto ddts = b.env.plcTypes();
    check(vars.size() == f->variables && ddts.size() == 28 && !vars.empty() && vars.front().first.rfind("API.", 0) == 0,
          "les listes de l'automate lu (API.Nom, les 28 DDT)");
    auto r = b.ask("Propose les types IHM des DDT");
    std::size_t types = 0;
    if (r.proposal)
        for (const auto& op : r.proposal->ops) types += op.action == ai::Operation::Action::Type ? 1 : 0;
    check(r.proposal && types > 0 && types <= 28, "les types IHM des DDT du .XPG : " + std::to_string(types) + " types proposes");
    // une globale de l'automate, par un morceau de son nom
    std::string piece;
    for (const auto& [n, t] : vars)
        if (t == "EBOOL" || t == "BOOL" || t == "INT" || t == "REAL") {
            piece = n.substr(4, std::min<std::size_t>(5, n.size() - 4));
            break;
        }
    r = b.ask("Cr\xC3\xA9" "e les variables de l'automate qui commencent par " + piece);
    std::size_t created = 0;
    if (r.proposal)
        for (const auto& op : r.proposal->ops) created += op.action == ai::Operation::Action::Variables ? op.variables.size() : 0;
    check(!piece.empty() && r.proposal && created > 0, "des variables IHM proposees depuis le .XPG (" + piece + ") : " + std::to_string(created));
    if (r.proposal)
        for (const auto& op : r.proposal->ops)
            for (const auto& v : op.variables) check(v.name.rfind("API.", 0) != 0 && v.type != "EBOOL", "le nom IHM sans API., le type converti : " + v.name + " " + v.type);
    check(r.proposal && has(r.text, "MAST.XPG"), "la reponse dit d'ou vient l'automate (MAST.XPG), pas \"l'automate relie\"");
    // des instances de bloc (PT1_A : CAPTEUR, un DFB) : dit, pas "deja dans l'IHM"
    r = b.ask("Cr\xC3\xA9" "e les variables de l'automate qui commencent par PT");
    check(!r.proposal && has(r.text, "instances de bloc") && !has(r.text, "d\xC3\xA9j\xC3\xA0 son \xC3\xA9quivalent") && !has(r.text, "API.PT"),
          "des instances de bloc : dit, par leur nom sans API. (" + r.text + ")");
}

// ---- 1.18.0 : les outils (les phrases de l'IA integree) --------------------------------------------------------
void testTools118() {
    std::printf("[1.18.0 : les outils]\n");
    using A = ai::Operation::Action;
    Bench b;
    std::string why;
    // un type generique
    auto r = b.ask("Cr\xC3\xA9" "e un type g\xC3\xA9n\xC3\xA9rique T_Pile<T> avec Valeur en T et Nombre en INT");
    if (valid(b, r, "type generique")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::Type && op->name == "T_Pile" && op->generics == std::vector<std::string>{"T"} && op->members.size() == 2
                  && op->members[0].type == "T",
              "T_Pile<T> : le nom, le generique T, le membre Valeur en T (" + (op ? op->name + " g" + std::to_string(op->generics.size()) + " m" + std::to_string(op->members.size())
                                                                                    + (op->members.empty() ? std::string{} : " " + op->members[0].name + ":" + op->members[0].type) : std::string{}) + ")");
        check(b.apply(r) && b.doc->project.hmiTypeByName("T_Pile") && b.doc->project.hmiTypeByName("T_Pile")->generics.size() == 1, "applique : T_Pile<T>");
        (void)b.stack.undo();
    }
    // un symbole, une popup (titre, modale, parametres), un en-tete, un pied de page, un ecran modele
    r = b.ask("Cr\xC3\xA9" "e un symbole Sym_Pompe<T> avec les param\xC3\xA8tres Etat en BOOL et Mesure en T");
    if (valid(b, r, "symbole")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::View && prop(*op, "role") == "symbole" && op->name == "Sym_Pompe" && op->generics.size() == 1 && op->params.size() == 2
                  && op->params[1].type == "T",
              "le symbole : son role, ses generiques, ses parametres");
        check(b.apply(r) && b.doc->project.viewByName("Sym_Pompe") && b.doc->project.viewByName("Sym_Pompe")->role == "symbole", "applique : le symbole");
        (void)b.stack.undo();
    }
    r = b.ask("Cr\xC3\xA9" "e une popup modale Reglage_Pompe \xC2\xAB R\xC3\xA9glage de la pompe \xC2\xBB");
    if (valid(b, r, "popup")) {
        const auto* op = firstOp(r);
        check(op && prop(*op, "role") == "popup" && op->name == "Reglage_Pompe" && prop(*op, "modale") == "oui" && prop(*op, "titre") == "R\xC3\xA9glage de la pompe",
              "la popup : modale, son titre");
    }
    r = b.ask("Ajoute un en-t\xC3\xAAte");
    check(r.proposal && firstOp(r) && prop(*firstOp(r), "role") == "entete" && firstOp(r)->name == "Entete", "un en-tete : Entete");
    r = b.ask("Cr\xC3\xA9" "e un pied de page Pied_Station");
    check(r.proposal && firstOp(r) && prop(*firstOp(r), "role") == "pied" && firstOp(r)->name == "Pied_Station", "un pied de page nomme");
    r = b.ask("Cr\xC3\xA9" "e un \xC3\xA9" "cran mod\xC3\xA8le Modele_Process");
    check(r.proposal && firstOp(r) && prop(*firstOp(r), "role") == "modele" && firstOp(r)->name == "Modele_Process", "un ecran modele");
    // pas un outil : "fais clignoter le voyant quand l'alarme..." ne cree pas d'alarme
    r = b.ask("Fais clignoter Bouton_1 quand l'alarme Defaut_V1 est vraie");
    check(countOps(r, A::Alarm) == 0, "\"quand l'alarme\" : pas une alarme neuve");
    // un equipement, la detection PROPOSEE
    r = b.ask("Cr\xC3\xA9" "e un \xC3\xA9quipement Centrale_1 en 192.168.1.40, port 502, esclave 3, et d\xC3\xA9tecte ce qui r\xC3\xA9pond");
    if (valid(b, r, "equipement")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::Equipment && op->name == "Centrale_1" && prop(*op, "adresse") == "192.168.1.40" && prop(*op, "esclave") == "3"
                  && prop(*op, "port") == "502" && op->detect,
              "l'equipement : nom, adresse, port, esclave, detection proposee");
        const auto det = ai::proposedDetections(*r.proposal);
        check(det.size() == 1 && det[0].second == "192.168.1.0/24", "la plage proposee : 192.168.1.0/24");
        check(r.text.find("c'est toi qui") != std::string::npos, "la reponse dit que l'utilisateur lance la detection");
        check(b.apply(r) && b.doc->project.equipmentByName("Centrale_1"), "applique : l'equipement");
    }
    r = b.ask("Cr\xC3\xA9" "e un \xC3\xA9quipement Automate_2");
    check(!r.proposal && r.text.find("adresse IP") != std::string::npos, "sans adresse : la question");
    // des adresses : la variable doit exister ; plusieurs : la suite
    r = b.ask("Lie Niveau_Cuve_1 et Niveau_Cuve_2 \xC3\xA0 l'\xC3\xA9quipement Centrale_1 en %MW100");
    if (valid(b, r, "adresses")) {
        check(countOps(r, A::Address) == 2 && prop(r.proposal->ops[0], "adresse") == "%MW100" && prop(r.proposal->ops[1], "adresse").empty(),
              "deux adresses : %MW100 puis a la suite");
        check(b.apply(r) && !b.doc->project.variable("Niveau_Cuve_1")->address.empty(), "applique : Niveau_Cuve_1 adressee");
        (void)b.stack.undo();
    }
    r = b.ask("Lie Niveau_Inexistant_9 \xC3\xA0 Centrale_1 en %MW200");
    check(!r.proposal && r.text.find("Niveau_Inexistant_9") != std::string::npos, "une variable inconnue : jamais inventee");
    (void)b.stack.undo();   // l'equipement
    // une alarme (la condition dite en francais), un script, une fonction
    r = b.ask("Cr\xC3\xA9" "e une alarme Niveau_Haut quand Niveau_Cuve_1 d\xC3\xA9passe 90 avec le message \xC2\xAB Cuve 1 pleine \xC2\xBB, priorit\xC3\xA9 1");
    if (valid(b, r, "alarme")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::Alarm && op->name == "Niveau_Haut" && prop(*op, "condition") == "Niveau_Cuve_1 > 90" && prop(*op, "message") == "Cuve 1 pleine"
                  && prop(*op, "priorite") == "1",
              "l'alarme : condition > 90, message, priorite");
    }
    r = b.ask("Cr\xC3\xA9" "e une alarme Fuite quand Variable_Absente > 3");
    check(!r.proposal, "une condition sur une variable inconnue : refusee (" + r.text.substr(0, 60) + ")");
    r = b.ask("Cr\xC3\xA9" "e un script Surveillance toutes les 5 secondes");
    if (valid(b, r, "script cyclique")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::Script && op->name == "Surveillance" && prop(*op, "evenement") == "Cyclique" && prop(*op, "periode") == "5000",
              "le script : cyclique, 5000 ms");
    }
    r = b.ask("Cr\xC3\xA9" "e un script Init au d\xC3\xA9marrage");
    check(r.proposal && firstOp(r) && prop(*firstOp(r), "evenement") == "Demarrage", "un script au demarrage");
    r = b.ask("Cr\xC3\xA9" "e une fonction Plus_Grand<T> avec les param\xC3\xA8tres A en T et B en T qui rend T");
    if (valid(b, r, "fonction generique")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::Function && op->name == "Plus_Grand" && op->generics.size() == 1 && prop(*op, "retour") == "T"
                  && prop(*op, "corps").find("A : T;") != std::string::npos,
              "la fonction generique : <T>, retour T, VAR_INPUT (" + (op ? op->name + " g" + std::to_string(op->generics.size()) + " r=" + prop(*op, "retour") + " c=" + prop(*op, "corps") : std::string{}) + ")");
        check(b.apply(r) && b.doc->project.functionByName("Plus_Grand"), "applique : la fonction");
        (void)b.stack.undo();
    }
    // des dossiers : creer, ranger
    r = b.ask("Range Vue_Test dans le dossier Process");
    if (valid(b, r, "ranger")) {
        const auto* op = firstOp(r);
        check(op && op->action == A::Store && prop(*op, "dossier") == "Process" && prop(*op, "liste") == "vues" && op->objects == std::vector<std::string>{"Vue_Test"},
              "ranger : la vue, le dossier, la liste vues");
    }
    r = b.ask("Cr\xC3\xA9" "e un dossier Recettes pour les scripts");
    check(r.proposal && firstOp(r) && firstOp(r)->action == A::Folder && prop(*firstOp(r), "liste") == "scripts", "creer un dossier de scripts");
    // les variables systeme
    r = b.ask("Quelles variables syst\xC3\xA8me ?");
    check(!r.proposal && r.text.find("SYS.") != std::string::npos, "les variables systeme : SYS.xxx");
}

int main(int argc, char** argv) {
    testCreate();
    testSynoptic();
    testModify();
    testBind();
    testVariablesAndView();
    testArrange();
    testAnimate();
    testQuestions();
    testFollowUp();
    testAssistant();
    testMissingObject();
    testTextOf();
    testAmbiguous();
    testPlacement();
    testAlreadyDone();
    testTypes();
    testTexts();
    testNoPlc();
    testSuggestions();
    testHelpPhrases();
    testTypeUnderstood();
    testTypeModels();
    testNameGuard();
    testUnderstoodFlag();
    testCaptures1161();
    testCorpus();
    testTools118();
    testPlcFile(argc > 1 ? argv[1] : nullptr);
    std::printf("ai_integree_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
