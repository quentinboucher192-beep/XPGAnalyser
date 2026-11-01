// =============================================================================
//  tests/ai_build_test.cpp - 1.22.5 : L'AVIS DU LOGICIEL (AiBuild)
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un vrai projet d'essai construit en memoire (une
//  vue, un type T_Cuve, des variables) :
//    a. une proposition qui cree un objet dont la formule lit un membre inconnu
//       d'une variable (Cuve.Inexistant : la validation ne le voit pas, Compiler
//       si) : l'erreur est NOUVELLE, sur cet objet (son id dans la copie, son
//       nom, sa vue, sa propriete), avec son operation ; dite une fois (Generer
//       et Compiler la disent tous deux) ; une modification d'un objet existant
//       (son id d'avant) ; un objet renomme (le nom etait pris) ;
//    b. une proposition propre : aucun constat nouveau, alors que le projet
//       d'avant en a (un avertissement du projet, une erreur sur un objet que la
//       proposition deplace) - ils ne sont PAS repris ;
//    c. un avertissement nouveau (une courbe sans plume) ;
//    d. le cache : deux verdicts, le projet d'avant genere une seule fois (une
//       copie aussi) ; un autre projet, d'autres noms d'automate : refait ;
//    e. merge : errors, warnings, spots, faults (deux fois : rien de plus) ;
//    f. feedbackText (la correction du logiciel, la consigne, la limite) ;
//    g. le JSON aller-retour ; le faux verdict des essais ;
//    h. la previsualisation : la categorie GENERER ET COMPILER, << en cours >>
//       puis le resume, ses lignes en couleur, l'objet encadre (rouge, ambre),
//       la selection qui l'encadre, le clic droit, Appliquer bloque ; le verdict
//       garde dans la proposition ; une correction l'oublie.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiBuild.hpp"
#include "../src/app/ai/AiFixes.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPreviewDialog.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace bd = app::ai::build;

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
bool listed(const std::vector<std::string>& lines, const std::string& exact) { return std::find(lines.begin(), lines.end(), exact) != lines.end(); }
int indexHaving(const std::vector<std::string>& lines, const std::string& part) {
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (has(lines[i], part)) return static_cast<int>(i);
    return -1;
}
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const std::string kGenerate = "G\xC3\xA9n\xC3\xA9rer";
const std::string kDash = " \xE2\x80\x94 ";
const std::string kHeading = "G\xC3\x89N\xC3\x89RER ET COMPILER";
const std::string kPending = "en cours\xE2\x80\xA6";
const std::string kAsk = "Demander \xC3\xA0 l'IA de corriger";

bool same(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Un renderer qui retient les textes, leurs couleurs et les cadres.
struct Recorder final : gfx::IRenderer {
    struct Stroke {
        gfx::Rect  r;
        gfx::Color c;
        float      w;
    };
    std::vector<std::pair<std::string, gfx::Color>> coloured;
    std::vector<Stroke>                             strokes;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect& r, gfx::Color c, float w) override { strokes.push_back({r, c, w}); }
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color c) override { coloured.emplace_back(std::string(t), c); }
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
    // Un texte dessine qui contient `part`, de cette couleur.
    [[nodiscard]] bool inColour(std::string_view part, gfx::Color c) const {
        return std::any_of(coloured.begin(), coloured.end(), [&](const auto& t) { return t.first.find(part) != std::string::npos && same(t.second, c); });
    }
    [[nodiscard]] bool framed(gfx::Point at, gfx::Color c, float w) const {
        return std::any_of(strokes.begin(), strokes.end(), [&](const Stroke& s) { return same(s.c, c) && s.w == w && s.r.contains(at); });
    }
};

// ---- le projet d'essai ---------------------------------------------------------------------------------------------------
// Une vue, un type T_Cuve (Niveau, Alarme), quatre variables ; un objet existant, Voyant_Ancien, dont la formule divise par
// zero (une erreur deja la) ; aucune vue de demarrage (un avertissement deja la).
hmi::Id seedProject(hmi::Project& p) {
    p.views = {hmi::makeView(p, "Vue_Station")};
    hmi::HmiType t;
    t.id = p.allocate();
    t.name = "T_Cuve";
    t.members = {{"Niveau", "REAL"}, {"Alarme", "BOOL"}};
    p.programs.types.push_back(t);
    const auto add = [&](const char* name, const char* type, const char* initial) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = type;
        v.initial = initial;
        p.programs.variables.push_back(v);
    };
    add("Niveau", "REAL", "0");
    add("Marche", "BOOL", "FALSE");
    add("Nom", "STRING", "");
    add("Cuve", "T_Cuve", "");
    hmi::View& v = p.views.front();
    hmi::Object old = hmi::makeObject(hmi::Kind::Indicator, p.allocate(), "Voyant_Ancien", 300, 40, v.layers.empty() ? hmi::kNoId : v.layers.front().id);
    old.setExpr("visible", "Niveau / 0 > 1");
    v.objects.push_back(old);
    return v.id;
}

ai::Operation create(const std::string& kind, const std::string& name, std::vector<std::pair<std::string, std::string>> props) {
    ai::Operation op;
    op.action = ai::Operation::Action::Create;
    op.kind = kind;
    op.name = name;
    op.props = std::move(props);
    return op;
}
ai::Operation modify(const std::string& name, std::vector<std::pair<std::string, std::string>> props) {
    ai::Operation op;
    op.action = ai::Operation::Action::Modify;
    op.name = name;
    op.props = std::move(props);
    return op;
}

ai::Proposal proposalOn(hmi::Id view, std::vector<ai::Operation> ops) {
    ai::Proposal p;
    p.id = "op-cuve";
    p.state = "proposee";
    p.view = "Vue_Station";
    p.viewId = view;
    p.request = "Ajoute les voyants de la cuve";
    p.summary = "Voyants de la cuve";
    p.ops = std::move(ops);
    return p;
}

// La proposition des essais a, c, e, f, h : un voyant propre (0), le voyant qui lit Cuve.Inexistant (1), une courbe sans
// plume (2).
ai::Proposal faultyProposal(hmi::Id view) {
    return proposalOn(view, {create("Indicator", "Voyant_Marche", {{"x", "40"}, {"y", "40"}, {"variable", "Marche"}}),
                             create("Indicator", "Voyant_Cuve", {{"x", "40"}, {"y", "120"}, {"visible", "=Cuve.Inexistant > 1"}}),
                             create("Trend", "Courbe_Niveau", {{"x", "200"}, {"y", "200"}})});
}

const hmi::Object* objectIn(const hmi::Project& p, const std::string& name) {
    for (const auto& v : p.views)
        if (const auto* o = v.objectByName(name)) return o;
    return nullptr;
}

const bd::Finding* findingOn(const bd::Verdict& v, const std::string& object) {
    for (const auto& f : v.findings)
        if (f.objectName == object) return &f;
    return nullptr;
}

// ---- a, b, c : les constats nouveaux ---------------------------------------------------------------------------------------
void testFindings() {
    std::printf("[constats nouveaux]\n");
    hmi::Project p;
    const hmi::Id view = seedProject(p);
    const ai::Proposal prop = faultyProposal(view);
    check(ai::validate(p, prop).ok(), "la validation ne voit rien (Cuve.Inexistant : Cuve existe)");
    const bd::Verdict v = bd::verdict(p, prop);
    std::printf("  le verdict : %s en %s (avant : %zu constats, apres : %zu)\n", v.summary().c_str(), bd::durationText(v.ms).c_str(), v.before,
                v.after);
    check(v.ran && v.why.empty(), "Generer et Compiler ont tourne sur la copie");
    check(v.errors == 1 && v.warnings == 1 && v.findings.size() == 2, "1 erreur, 1 avertissement nouveaux (" + v.summary() + ")");
    check(v.summary() == kGenerate + " et Compiler : 1 erreur, 1 avertissement nouveaux", "le resume : " + v.summary());
    check(v.before == 2 && v.after == 4, "avant : 2 constats (la vue de demarrage, Voyant_Ancien) ; apres : 4");
    check(v.ms > 0.0, "la duree est mesuree");
    // (a) L'ERREUR NOUVELLE, sur son objet, avec son operation
    const bd::Finding* e = findingOn(v, "Voyant_Cuve");
    check(e && e->error && v.findings.front().objectName == "Voyant_Cuve", "l'erreur vient d'abord, sur Voyant_Cuve");
    if (e) {
        const auto after = ai::appliedCopy(p, prop);
        const hmi::Object* o = after ? objectIn(*after, "Voyant_Cuve") : nullptr;
        check(o && e->object == o->id && p.views.front().object(e->object) == nullptr, "son objet : l'id neuf de la copie (absent du projet d'avant)");
        check(e->view == "Vue_Station" && e->property == "visible" && e->category == "Expression", "sa vue, sa propriete, sa categorie");
        check(e->where == "Vue_Station . Voyant_Cuve . visible", "l'endroit lisible : " + e->where);
        check(e->op == 1, "son operation : la 1 (" + std::to_string(e->op) + ")");
        check(e->step == "Compiler", "dite par Compiler (Generer la dit aussi : une fois) : " + e->step);
        check(has(e->message, "n'a pas de membre Inexistant"), "le message tel que la fenetre Sorties l'ecrit : " + e->message);
        check(std::count_if(v.findings.begin(), v.findings.end(), [](const bd::Finding& f) { return f.objectName == "Voyant_Cuve"; }) == 1,
              "un constat dit une fois");
    }
    // (c) L'AVERTISSEMENT NOUVEAU : une courbe sans plume
    const bd::Finding* w = findingOn(v, "Courbe_Niveau");
    check(w && !w->error && w->step == kGenerate && w->category == "Courbe" && has(w->message, "aucune plume") && w->op == 2,
          "l'avertissement de Generer : Courbe_Niveau sans plume, l'operation 2");
    check(!findingOn(v, "Voyant_Ancien") && !findingOn(v, "Voyant_Marche"), "ni Voyant_Ancien (deja la) ni Voyant_Marche (propre)");
    // Le meme verdict avec le Check de la validation : l'operation lue dans ses objets.
    const bd::Verdict v2 = bd::verdict(p, prop, {}, ai::validate(p, prop));
    check(v2.findings.size() == 2 && findingOn(v2, "Voyant_Cuve") && findingOn(v2, "Voyant_Cuve")->op == 1, "avec le Check : la meme operation");

    // (b) UNE PROPOSITION PROPRE : rien de nouveau ; ce qui etait la n'est pas repris (meme sur un objet qu'elle deplace).
    const ai::Proposal clean = proposalOn(view, {create("Indicator", "Voyant_Marche", {{"x", "40"}, {"y", "40"}, {"variable", "Marche"}}),
                                                 modify("Voyant_Ancien", {{"x", "320"}})});
    const bd::Verdict c = bd::verdict(p, clean);
    check(c.ran && c.findings.empty() && c.errors == 0 && c.warnings == 0 && c.clean(), "propre : aucun constat nouveau");
    check(c.before == 2 && c.after == 2, "les 2 constats d'avant sont toujours la, pas repris");
    check(c.summary() == kGenerate + " et Compiler : aucun constat nouveau", "le resume : " + c.summary());

    // Une MODIFICATION qui casse un objet existant : son id d'avant, l'operation qui le modifie.
    const ai::Proposal broke = proposalOn(view, {create("Indicator", "Voyant_Marche", {{"x", "40"}, {"y", "40"}, {"variable", "Marche"}}),
                                                 modify("Voyant_Ancien", {{"visible", "=Cuve > 1"}})});
    const bd::Verdict b = bd::verdict(p, broke);
    const bd::Finding* m = findingOn(b, "Voyant_Ancien");
    check(b.errors == 1 && m && m->op == 1 && m->object == p.views.front().objectByName("Voyant_Ancien")->id && m->property == "visible",
          "un objet existant modifie : son id d'avant, l'operation 1, la propriete visible");

    // Un nom pris : l'objet neuf est renomme (Voyant_Ancien_1...) - son operation se retrouve par le nom voulu.
    const ai::Proposal renamed = proposalOn(view, {create("Indicator", "Voyant_Ancien", {{"x", "40"}, {"y", "300"}, {"visible", "=Cuve.Inexistant > 1"}})});
    const bd::Verdict r = bd::verdict(p, renamed);
    const bd::Finding* rf = r.findings.empty() ? nullptr : &r.findings.front();
    check(r.errors == 1 && rf && rf->objectName != "Voyant_Ancien" && rf->objectName.rfind("Voyant_Ancien", 0) == 0 && rf->op == 0,
          "un objet neuf renomme : " + (rf ? rf->objectName : std::string("?")) + ", son operation 0");

    // Rien ne s'applique (la seule operation est refusee par la validation) : pas lances, et pourquoi.
    const ai::Proposal refused = proposalOn(view, {create("Indicator", "Voyant_X", {{"visible", "=Inconnue > 1"}})});
    const bd::Verdict n = bd::verdict(p, refused);
    check(!n.ran && !n.why.empty() && n.findings.empty() && !n.clean(), "rien ne s'applique : pas lances (" + n.why + ")");
    check(has(n.summary(), "pas lanc\xC3\xA9s : "), "le resume le dit : " + n.summary());
}

// ---- d : le cache ----------------------------------------------------------------------------------------------------------
void testCache() {
    std::printf("[cache du projet d'avant]\n");
    hmi::Project p;
    const hmi::Id view = seedProject(p);
    bd::clearCacheForTests();
    const std::size_t r0 = bd::beforeRunsForTests();
    const bd::Verdict a = bd::verdict(p, faultyProposal(view));
    check(bd::beforeRunsForTests() == r0 + 1, "le premier verdict genere et compile le projet d'avant");
    const bd::Verdict b = bd::verdict(p, proposalOn(view, {create("Indicator", "Voyant_Marche", {{"variable", "Marche"}})}));
    check(bd::beforeRunsForTests() == r0 + 1 && b.before == a.before, "le deuxieme verdict sur le meme projet : le projet d'avant n'est pas refait");
    const hmi::Project copy = p;   // une copie (le fil de fond en prend une a chaque verification) : la meme empreinte
    (void)bd::verdict(copy, faultyProposal(view));
    check(bd::beforeRunsForTests() == r0 + 1, "une copie du meme projet : toujours le cache (l'empreinte du contenu)");
    hmi::Project other = p;
    hmi::Variable extra;
    extra.id = other.allocate();
    extra.name = "Debit";
    extra.type = "REAL";
    extra.initial = "0";
    other.programs.variables.push_back(extra);
    (void)bd::verdict(other, faultyProposal(view));
    check(bd::beforeRunsForTests() == r0 + 2, "un autre projet : refait");
    ai::Env env;
    env.plcName = [](std::string_view n) { return n == "API_MARCHE"; };
    env.plcNameList = [] { return std::vector<std::string>{"API_MARCHE"}; };
    (void)bd::verdict(p, faultyProposal(view), env);
    check(bd::beforeRunsForTests() == r0 + 3, "d'autres noms d'automate : refait");
    (void)bd::verdict(p, faultyProposal(view), env);
    check(bd::beforeRunsForTests() == r0 + 3, "les memes noms d'automate : le cache");
    ai::Env loose;
    loose.plcName = [](std::string_view) { return false; };   // sans sa liste : pas de cle, pas de cache
    (void)bd::verdict(p, faultyProposal(view), loose);
    (void)bd::verdict(p, faultyProposal(view), loose);
    check(bd::beforeRunsForTests() == r0 + 5, "un automate sans liste de noms : jamais de cache (refait a chaque fois)");
}

// ---- e, f, g : merge, le texte pour l'IA, le JSON ---------------------------------------------------------------------------
void testMergeTextJson() {
    std::printf("[merge, texte pour l'IA, JSON]\n");
    hmi::Project p;
    const hmi::Id view = seedProject(p);
    const ai::Proposal prop = faultyProposal(view);
    const bd::Verdict v = bd::verdict(p, prop);
    const bd::Finding* e = findingOn(v, "Voyant_Cuve");
    const bd::Finding* w = findingOn(v, "Courbe_Niveau");
    if (!e || !w) {
        check(false, "les deux constats de la proposition");
        return;
    }
    const std::string eLine = "Compiler : Vue_Station . Voyant_Cuve . visible" + kDash + e->message;
    check(bd::lineOf(*e) == eLine, "la ligne : << Compiler : endroit - message >> (" + bd::lineOf(*e) + ")");
    // (e) MERGE
    ai::Check ck = ai::validate(p, prop);
    const std::size_t errors0 = ck.errors.size(), warnings0 = ck.warnings.size(), spots0 = ck.spots.size(), faults0 = ck.faults.size();
    bd::merge(ck, v);
    check(ck.errors.size() == errors0 + 1 && ck.errors.back() == eLine && !ck.ok(), "errors : la ligne de Compiler ; la proposition n'est plus applicable");
    check(ck.warnings.size() == warnings0 + 1 && ck.warnings.back() == bd::lineOf(*w) && ck.warnings.back().rfind(kGenerate + " : ", 0) == 0,
          "warnings : la ligne de Generer");
    check(ck.spots.size() == spots0 + 2, "un Spot par constat qui a un objet");
    const auto spot = std::find_if(ck.spots.begin(), ck.spots.end(), [&](const ai::Check::Spot& s) { return s.message == eLine; });
    check(spot != ck.spots.end() && spot->error && spot->object == "Voyant_Cuve" && spot->id == e->object && spot->view == "Vue_Station" && spot->op == 1,
          "le Spot de l'erreur : l'objet, son id, sa vue, l'operation, rouge");
    const auto wspot = std::find_if(ck.spots.begin(), ck.spots.end(), [&](const ai::Check::Spot& s) { return s.message == bd::lineOf(*w); });
    check(wspot != ck.spots.end() && !wspot->error && wspot->object == "Courbe_Niveau", "le Spot de l'avertissement : ambre");
    check(ck.faults.size() == faults0 + 1 && ck.faults.back().kind == "build" && ck.faults.back().op == 1 && ck.faults.back().name == "Voyant_Cuve"
              && ck.faults.back().message == eLine,
          "un Fault << build >> par erreur (l'operation, l'objet, la ligne)");
    bd::merge(ck, v);
    check(ck.errors.size() == errors0 + 1 && ck.spots.size() == spots0 + 2 && ck.faults.size() == faults0 + 1, "merge deux fois : rien de plus");
    ai::Check none;
    bd::merge(none, bd::Verdict{});
    check(none.errors.empty() && none.spots.empty(), "un verdict qui n'a pas tourne : rien");
    // (f) LE TEXTE POUR L'IA
    const std::string t = bd::feedbackText(v);
    check(t.rfind("Le logiciel a g\xC3\xA9n\xC3\xA9r\xC3\xA9 et compil\xC3\xA9 le projet avec ta proposition appliqu\xC3\xA9" "e (une copie) : "
                  "1 erreur, 1 avertissement nouveaux.\n", 0) == 0,
          "la premiere ligne : ce que le logiciel a fait, les comptes");
    check(has(t, "- erreur (Compiler), Vue_Station . Voyant_Cuve . visible : " + e->message) && has(t, "[op\xC3\xA9ration 2]"),
          "une ligne par constat : l'etape, l'endroit, le message, l'operation");
    check(has(t, "- avertissement (" + kGenerate + "), Vue_Station . Courbe_Niveau . variables : "), "l'avertissement");
    check(has(t, "proposer_operations") && has(t, "ne touche qu'\xC3\xA0 ce qui est en cause") && has(t, "dis en une phrase ce que tu as chang\xC3\xA9")
              && has(t, "Un avertissement voulu peut rester : dis pourquoi."),
          "la consigne : renvoyer la proposition entiere, ne toucher qu'a ce qui est en cause, dire ce qui a change");
    bd::Verdict fixable = v;
    fixable.findings.front().fixFrom = "Cuve.Inexistant";
    fixable.findings.front().fixTo = "Cuve.Niveau";
    check(has(bd::feedbackText(fixable), "correction du logiciel : remplacer \xC2\xAB Cuve.Inexistant \xC2\xBB par \xC2\xAB Cuve.Niveau \xC2\xBB"),
          "la correction du logiciel, quand il en a une");
    const std::string one = bd::feedbackText(v, 1);
    check(has(one, "- ... et 1 autre.") && !has(one, "Courbe_Niveau"), "max : les premiers, puis << et 1 autre >>");
    check(bd::feedbackText(bd::verdict(p, proposalOn(view, {create("Indicator", "Voyant_Marche", {{"variable", "Marche"}})}))).empty(),
          "propre : rien a dire");
    check(bd::feedbackText(bd::Verdict{}).empty(), "pas lance : rien a dire");
    // (g) LE JSON
    std::string text = ai::json::write(bd::toJson(fixable));
    ai::json::Value back;
    check(ai::json::parse(text, back), "le JSON se relit");
    const bd::Verdict r = bd::fromJson(back);
    check(r.ran == fixable.ran && r.why == fixable.why && std::abs(r.ms - fixable.ms) < 0.06 && r.before == fixable.before && r.after == fixable.after
              && r.errors == fixable.errors && r.warnings == fixable.warnings && r.findings.size() == fixable.findings.size(),
          "aller-retour : ran, why, ms, before, after, errors, warnings");
    bool same = r.findings.size() == fixable.findings.size();
    for (std::size_t k = 0; same && k < r.findings.size(); ++k) {
        const auto &x = r.findings[k], &y = fixable.findings[k];
        same = x.error == y.error && x.step == y.step && x.category == y.category && x.message == y.message && x.where == y.where && x.view == y.view
            && x.object == y.object && x.objectName == y.objectName && x.property == y.property && x.op == y.op && x.fixFrom == y.fixFrom
            && x.fixTo == y.fixTo;
    }
    check(same, "aller-retour : chaque constat, champ par champ");
    bd::Verdict skipped;
    skipped.why = "la vue a chang\xC3\xA9";
    const bd::Verdict s = bd::fromJson(bd::toJson(skipped));
    check(!s.ran && s.why == skipped.why && s.findings.empty(), "un verdict pas lance, et pourquoi");
    check(bd::durationText(320.4) == "320 ms" && bd::durationText(1440) == "1,4 s" && bd::durationText(12000) == "12,0 s", "la duree a l'ecran");
    // LE FAUX VERDICT DES ESSAIS
    bd::setVerdictForTests([](const hmi::Project&, const ai::Proposal& pr) {
        bd::Verdict fake;
        fake.why = "faux : " + pr.id;
        return fake;
    });
    check(bd::verdict(p, prop).why == "faux : op-cuve", "setVerdictForTests remplace le vrai");
    bd::setVerdictForTests(nullptr);
    check(bd::verdict(p, prop).ran, "nul : le vrai revient");
}

// ---- h : la previsualisation --------------------------------------------------------------------------------------------
struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};
    const std::string            convId = "c-cuve";

    Fixture(const std::string& name, const ai::json::Value* stored, bool withProcess = true) {
        dir = fs::temp_directory_path() / ("xpg_ai_build_" + name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        view = seedProject(doc->project);
        ai::Conversation c;
        c.id = convId;
        c.title = "La cuve";
        c.created = c.modified = ai::isoDate(ai::now());
        ai::Message ask;
        ask.id = "m1";
        ask.role = "utilisateur";
        ask.date = c.created;
        ask.text = "Ajoute les voyants de la cuve";
        c.messages.push_back(ask);
        ai::Message answer;
        answer.id = "m2";
        answer.role = "assistant";
        answer.date = c.created;
        answer.state = "fini";
        answer.text = "Voici les voyants.";
        ai::Proposal p = faultyProposal(view);
        if (stored) p.extra.set("logiciel", *stored);
        // Une proposition de la 1.22.5 : passee par le processus (son verdict est attendu) ; sans lui, une proposition d'avant.
        if (withProcess) {
            ai::json::Value line = ai::json::Value::object();
            line.set("quoi", "recue");
            p.extra["processus"] = ai::json::Value::array();
            p.extra["processus"].push(std::move(line));
        }
        answer.proposal = p;
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
        st.applyWithErrors = false;
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
    ai::AiPreviewDialog* preview(int m) {
        panel->openPreview(m);
        mm.applyPending();
        auto* pv = dynamic_cast<ai::AiPreviewDialog*>(mm.top());
        if (pv && pv->widgetRoot()) {
            pv->widgetRoot()->setBounds({0, 0, 1600, 900});
            pv->widgetRoot()->layout();
            Recorder rec;
            pv->widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), pv->widgetRoot()->bounds(), t, nullptr});
        }
        return pv;
    }
    Recorder draw(ai::AiPreviewDialog& pv) const {
        Recorder rec;
        pv.widgetRoot()->layout();
        pv.widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), pv.widgetRoot()->bounds(), t, nullptr});
        return rec;
    }
};

std::string headingOf(const ai::AiPreviewDialog& pv) {
    for (const auto& r : pv.visibleRows())
        if (r.rfind(kHeading, 0) == 0) return r;
    return {};
}

void testPreview() {
    std::printf("[previsualisation]\n");
    const ui::Theme th = ui::Theme::dark();
    hmi::Project project;
    const hmi::Id viewId = seedProject(project);
    const bd::Verdict verdict = bd::verdict(project, faultyProposal(viewId));
    {
        Fixture f("apercu", nullptr);
        const int m = f.open();
        check(m == 1, "la proposition");
        if (m < 0) return;
        auto* pv = f.preview(m);
        check(pv != nullptr, "la previsualisation s'ouvre");
        if (!pv) return;
        // AVANT LE VERDICT : << en cours >>
        check(!pv->verdictKnown() && headingOf(*pv) == kHeading + kDash + kPending, "la categorie : << " + headingOf(*pv) + " >>");
        check(pv->buildRows().empty() && pv->check().ok() && pv->applyButton() && pv->applyButton()->enabled(),
              "rien encore : la validation seule (Appliquer possible)");
        const hmi::Id cuve = pv->objectId("Voyant_Cuve"), courbe = pv->objectId("Courbe_Niveau"), marche = pv->objectId("Voyant_Marche");
        check(cuve != hmi::kNoId && courbe != hmi::kNoId && pv->issueOf(cuve) == 0, "les objets dans l'apercu, sans cadre d'erreur");
        // LE VERDICT ARRIVE
        pv->setVerdict(verdict);
        const std::string head = headingOf(*pv);
        check(pv->verdictKnown() && head == kHeading + " (2)" + kDash + "1 erreur, 1 avertissement nouveaux \xC2\xB7 " + bd::durationText(verdict.ms),
              "le titre dit le resume : << " + head + " >>");
        const auto rows = pv->buildRows();
        check(rows.size() == 2 && rows[0] == bd::lineOf(verdict.findings[0]) && has(rows[0], "Compiler : Vue_Station . Voyant_Cuve . visible")
                  && has(rows[1], kGenerate + " : Vue_Station . Courbe_Niveau"),
              "ses lignes : l'etape, l'endroit, le message (l'erreur d'abord)");
        const auto shown = pv->visibleRows();
        check(indexHaving(shown, "D\xC3\xA9j\xC3\xA0 l\xC3\xA0 avant ta proposition : 2 constats") >= 0, "ce qui etait deja la est dit, pas repris");
        check(pv->errorRows().empty() && pv->warningRows().empty(), "pas redits dans ERREURS ni AVERTISSEMENTS");
        check(!pv->check().ok() && pv->check().errors.size() == 1 && pv->applyButton() && !pv->applyButton()->enabled(),
              "l'erreur du logiciel compte : Appliquer bloque (sans << appliquer malgre >>)");
        check(pv->issueOf(cuve) == 2 && pv->issueOf(courbe) == 1 && pv->issueOf(marche) == 0, "les niveaux : Voyant_Cuve rouge, Courbe_Niveau ambre");
        {
            const Recorder rec = f.draw(*pv);
            check(rec.framed(pv->objectPoint("Voyant_Cuve"), th.color.error, 3.f), "Voyant_Cuve encadre de rouge");
            check(rec.framed(pv->objectPoint("Courbe_Niveau"), th.color.warning, 3.f) && !rec.framed(pv->objectPoint("Courbe_Niveau"), th.color.error, 3.f),
                  "Courbe_Niveau encadre d'ambre");
            check(rec.inColour("Compiler : Vue_Station", th.onSurface(th.color.error)), "a gauche : la ligne de l'erreur en rouge");
            check(rec.inColour(kGenerate + " : Vue_Station", th.onSurface(th.color.warning)), "a gauche : la ligne de l'avertissement en ambre");
            check(rec.inColour(kHeading + " (2)", th.color.textMuted), "le titre de la categorie");
            check(has(pv->previewTooltip(pv->objectPoint("Voyant_Cuve")), "Erreur : Compiler : "), "l'infobulle de l'objet : l'avis du logiciel");
        }
        // CHOISIR UNE LIGNE : son objet s'encadre
        check(pv->selectRow("Compiler : Vue_Station . Voyant_Cuve") && pv->highlighted() == std::vector<hmi::Id>{cuve},
              "la ligne de l'erreur : Voyant_Cuve s'encadre");
        {
            const Recorder rec = f.draw(*pv);
            check(rec.framed(pv->objectPoint("Voyant_Cuve"), th.color.accent, 3.f), "le cadre de la ligne choisie (la couleur d'accent)");
        }
        check(pv->selectRow(kGenerate + " : Vue_Station . Courbe_Niveau") && pv->highlighted() == std::vector<hmi::Id>{courbe},
              "la ligne de l'avertissement : Courbe_Niveau");
        pv->setIssueFilter(true, false);
        check(listed(pv->visibleRows(), rows[0]) && !listed(pv->visibleRows(), rows[1]), "la puce Erreurs : la ligne du logiciel en est");
        pv->setIssueFilter(false, false);
        // LE CLIC DROIT
        check(pv->openBuildMenu(0) && pv->menuOpen(), "le clic droit sur l'erreur du logiciel");
        const auto items = pv->menuItems();
        check(listed(items, "Copier le message") && listed(items, kAsk) && listed(items, "Retirer cette op\xC3\xA9ration"),
              "Copier, Demander a l'IA de corriger, Retirer cette operation");
        check(pv->chooseMenuItem("Copier le message") && g_clipboard == rows[0], "Copier le message");
        // UNE CORRECTION (l'operation retiree) : la proposition change, le verdict ne vaut plus
        check(pv->openBuildMenu(0) && pv->chooseMenuItem("Retirer cette op\xC3\xA9ration"), "Retirer cette operation");
        check(f.as().proposal(m)->excluded == std::vector<std::size_t>{1}, "l'operation 1 est retiree de la proposition");
        check(!pv->verdictKnown() && headingOf(*pv) == kHeading + kDash + kPending && pv->buildRows().empty() && pv->check().ok(),
              "le verdict d'avant ne vaut plus : << en cours >>, l'erreur partie");
        // Le verdict de la proposition corrigee : un avertissement seul, Appliquer revient.
        ai::Proposal corrected = *f.as().proposal(m);
        pv->setVerdict(bd::verdict(f.doc->project, corrected));
        check(pv->verdictKnown() && pv->buildRows().size() == 1 && has(headingOf(*pv), "1 avertissement nouveau \xC2\xB7")
                  && pv->applyButton()->enabled(),
              "corrigee : << 1 avertissement nouveau >>, Appliquer possible");
        // DEMANDER A L'IA : le texte prepare (rien ne part)
        check(pv->openBuildMenu(0) && pv->chooseMenuItem(kAsk), "Demander a l'IA de corriger");
        f.mm.applyPending();
        check(has(f.panel->prompt().text(), "Corrige ta proposition") && has(f.panel->prompt().text(), "Courbe_Niveau"),
              "la saisie : la demande de correction cite le constat");
    }
    // LE VERDICT GARDE DANS LA PROPOSITION (Proposal::extra << logiciel >>) : il se lit a l'ouverture.
    {
        const ai::json::Value stored = bd::toJson(verdict);
        Fixture f("garde", &stored);
        const int m = f.open();
        auto* pv = m < 0 ? nullptr : f.preview(m);
        check(pv && pv->verdictKnown() && pv->buildRows().size() == 2 && has(headingOf(*pv), "1 erreur, 1 avertissement nouveaux"),
              "garde dans la proposition : lu a l'ouverture");
        if (pv) {
            const hmi::Id cuve = pv->objectId("Voyant_Cuve");
            check(pv->issueOf(cuve) == 2, "l'objet de l'erreur, encadre (par son id, sinon son nom)");
        }
    }
    // UNE PROPOSITION D'AVANT LA 1.22.5 (sans processus, rien ne tourne) : << pas lances >>, jamais << en cours >> sans fin.
    {
        Fixture f("ancienne", nullptr, false);
        const int m = f.open();
        auto* pv = m < 0 ? nullptr : f.preview(m);
        check(pv && !pv->verdictKnown() && has(headingOf(*pv), "pas lanc\xC3\xA9s") && !has(headingOf(*pv), kPending),
              "une proposition d'avant : << pas lances >> (" + (pv ? headingOf(*pv) : std::string("?")) + ")");
    }
    // LA CORRECTION DU LOGICIEL d'un clic (un constat qui en a une) : au menu, en tete.
    {
        bd::Verdict withFix = verdict;
        withFix.findings.front().fixFrom = "Cuve.Inexistant";
        withFix.findings.front().fixTo = "Cuve.Niveau";
        const ai::json::Value stored = bd::toJson(withFix);
        Fixture f("correction", &stored);
        const int m = f.open();
        auto* pv = m < 0 ? nullptr : f.preview(m);
        check(pv && pv->openBuildMenu(0) && listed(pv->menuItems(), "Appliquer la correction du logiciel"),
              "le menu d'un constat qui a une correction : Appliquer la correction du logiciel");
    }
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
    testFindings();
    testCache();
    testMergeTextJson();
    testPreview();
    std::printf("ai_build_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
