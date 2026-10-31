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
#include <cstdio>
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
    check(!r.proposal && has(r.text, "instances de bloc") && !has(r.text, "d\xC3\xA9j\xC3\xA0 son \xC3\xA9quivalent"), "des instances de bloc : dit (" + r.text + ")");
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
    testPlcFile(argc > 1 ? argv[1] : nullptr);
    std::printf("ai_integree_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
