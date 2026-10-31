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
#include "../src/app/ai/AiLocal.hpp"
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
    op = firstOp(r = b.ask("Mets pompe 1 en vert"));
    check(op && op->name == "Pompe_1" && prop(*op, "colorOn") == "#2ECC71", "pompe 1 (deux mots) en vert");
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
    op = firstOp(r = b.ask("rends visible V1"));
    check(op && prop(*op, "visible") == "TRUE", "rendre visible");
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
    op = firstOp(r = b.ask("cree une variable Niveau_Cuve REAL"));
    check(op && op->variables.size() == 1 && op->variables[0].type == "REAL" && op->variables[0].initial == "0", "une variable REAL");
    op = firstOp(r = b.ask("cree la variable Temperature_Four"));
    check(op && op->variables.size() == 1 && op->variables[0].type == "REAL" && has(r.text, "tir\xC3\xA9 du nom"), "le type tire du nom, et dit");
    r = b.ask("cree la variable Pompe_1_Marche");
    check(!r.proposal && has(r.text, "existe d\xC3\xA9j\xC3\xA0"), "une variable qui existe : rien a creer");
    r = b.ask("cr\xC3\xA9" "e les variables de l'automate");
    check(!r.proposal && has(r.text, "Aucun automate"), "sans automate relie : dit");
    b.env.plcVariables = [] { return std::vector<std::pair<std::string, std::string>>{{"PT101", "REAL"}}; };
    r = b.ask("cree les variables de l'automate qui commencent par PT");
    check(r.proposal || has(r.text, "Rien \xC3\xA0 cr\xC3\xA9" "er"), "avec un automate : la proposition, ou rien a creer (dit)");
    r = b.ask("propose les types IHM des DDT de l'automate");
    check(r.proposal || has(r.text, "automate"), "les types des DDT : proposes, ou dit");
    b.env = {};
    op = firstOp(r = b.ask("cr\xC3\xA9" "e une vue Vue_Process en 1920x1080"));
    check(op && op->action == ai::Operation::Action::View && op->name == "Vue_Process" && prop(*op, "largeur") == "1920" && prop(*op, "hauteur") == "1080",
          "creer une vue (creer_vue) : son nom, sa taille");
    op = firstOp(r = b.ask("cree une nouvelle vue de 1280 x 800"));
    check(op && op->action == ai::Operation::Action::View && prop(*op, "largeur") == "1280" && op->name == "Vue_1", "une vue sans nom : Vue_1, 1280x800");
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
    valid(b, r, "dupliquer");
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
    op = firstOp(r = b.ask("cache Texte_1 quand Pompe_1_Marche"));
    check(op && prop(*op, "visible") == "=NOT Pompe_1_Marche", "cacher quand : la formule de visible");
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

} // namespace

int main() {
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
    std::printf("ai_integree_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
