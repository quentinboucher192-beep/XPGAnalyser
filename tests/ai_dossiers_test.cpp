// =============================================================================
//  tests/ai_dossiers_test.cpp - 1.22.1 : l'Assistant IA range les variables qu'il cree (AiVarFolders)
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau :
//    - le chemin (normalize), la cle (sans casse ni blancs en trop), l'existant repris (resolve), le prefixe ;
//    - creer_variables : un dossier par variable, un dossier pour l'operation ; les dossiers et sous-dossiers crees a
//      l'application (un seul Ctrl+Z) ; un dossier existant repris (casse, blancs), jamais double ; un chemin invalide
//      refuse ; creer_dossier et ranger de variables : le meme rangement ;
//    - sans dossier : deduit du prefixe (le dossier des variables du meme prefixe, sinon un dossier au prefixe commun),
//      et dit ; un << ranger >> de la proposition passe avant ; le melange de deux equipements signale ;
//    - le JSON (aller-retour, ancien JSON sans dossier), la lecture stricte de l'outil, son schema ;
//    - les consignes (la regle 18, l'exemple) et le contexte (l'arborescence, bornee) ;
//    - l'IA integree : les variables qu'elle cree ont leur dossier, par la meme regle.
// =============================================================================
#include "../src/app/ai/AiContext.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiLocal.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiVarFolders.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiTypes.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace vf = app::ai::varfold;
using A = ai::Operation::Action;
using Spec = ai::Operation::VariableSpec;

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

bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

bool anyHas(const std::vector<std::string>& list, const std::string& what) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& s) { return has(s, what); });
}

std::string all(const std::vector<std::string>& list) {
    std::string s;
    for (const auto& e : list) s += e + " | ";
    return s;
}

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const std::string kDeduced = "d\xC3\xA9" "duit du pr\xC3\xA9" "fixe";
const std::string kMixed = "m\xC3\xA9langerait";

// Un projet : la vue Vue_Station ; les variables de la pompe 1 rangees dans Pompes/Pompe1, deux a la racine ; les
// dossiers Pompes et Station de pompage (vide).
struct Fixture {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    hmi::Id            view{hmi::kNoId};

    Fixture() {
        auto& p = doc->project;
        p.config.width = 1280;
        p.config.height = 800;
        p.views = {hmi::makeView(p, "Vue_Station")};
        view = p.views[0].id;
        const auto var = [&](const char* name, const char* type, const char* folder) {
            hmi::Variable v;
            v.id = p.allocate();
            v.name = name;
            v.type = type;
            v.folder = folder;
            p.programs.variables.push_back(v);
        };
        var("Pompe1_Marche", "BOOL", "Pompes/Pompe1");
        var("Pompe1_Defaut", "BOOL", "Pompes/Pompe1");
        var("Heure_Systeme", "STRING", "");
        var("Niveau_Cuve", "REAL", "");
        p.programs.folders = {"Pompes", "Station de pompage"};
    }
    [[nodiscard]] ai::Proposal proposal(std::vector<ai::Operation> ops) const {
        ai::Proposal p;
        p.id = "op1";
        p.request = "essai";
        p.summary = "Essai";
        p.ops = std::move(ops);
        if (ai::usesOpenView(p)) {
            p.view = doc->project.view(view)->name;
            p.viewId = view;
        }
        p.fingerprint = ai::proposalPrint(doc->project, p);
        return p;
    }
    bool apply(const ai::Proposal& p, std::string* why = nullptr) {
        auto cmd = ai::makeCommand(doc, p, why);
        if (!cmd) return false;
        return static_cast<bool>(stack.push(std::move(cmd)));
    }
    [[nodiscard]] const hmi::Variable* var(const std::string& name) const { return doc->project.variable(name); }
    // Combien de dossiers du projet (ceux de la liste et ceux des variables) ont cette cle.
    [[nodiscard]] std::size_t foldersLike(const std::string& path) const {
        const auto list = hmi::types::allFolders(doc->project);
        return static_cast<std::size_t>(std::count_if(list.begin(), list.end(), [&](const std::string& f) { return vf::key(f) == vf::key(path); }));
    }
    [[nodiscard]] bool listed(const std::string& path) const {
        const auto list = hmi::types::allFolders(doc->project);
        return std::find(list.begin(), list.end(), path) != list.end();
    }
};

ai::Operation op(A a, std::vector<std::pair<std::string, std::string>> props = {}) {
    ai::Operation o;
    o.action = a;
    o.props = std::move(props);
    return o;
}
ai::Operation vars(std::vector<Spec> list, std::string folder = {}) {
    ai::Operation o = op(A::Variables);
    o.variables = std::move(list);
    if (!folder.empty()) o.props.emplace_back("dossier", std::move(folder));
    return o;
}
std::size_t madeFolders(const ai::Check& ck, const std::string& name = {}) {
    return static_cast<std::size_t>(std::count_if(ck.made.begin(), ck.made.end(), [&](const ai::Check::Made& m) {
        return m.what == "Dossier" && (name.empty() || m.name == name);
    }));
}
const Spec* shown(const ai::Check& ck, const std::string& name) {
    for (const auto& v : ck.newVariables)
        if (v.name == name) return &v;
    return nullptr;
}

// ---- 1. les regles, une a une -------------------------------------------------------------------------
void rules() {
    std::printf("les regles (chemin, cle, existant, prefixe)\n");
    std::string out, why;
    check(vf::normalize("  Station de pompage /  Pompes//P1 / ", out, &why) && out == "Station de pompage/Pompes/P1",
          "un chemin remis en forme : blancs rognes, noms vides retires (" + out + ")");
    check(vf::normalize("Pompes \t  A/B", out) && out == "Pompes A/B", "les blancs du milieu : un seul");
    check(vf::normalize("", out) && out.empty() && vf::normalize(" / ", out) && out.empty(), "vide ou / seul : aucun dossier (vrai, vide)");
    check(!vf::normalize("Zone:1", out, &why) && has(why, "\xC2\xAB : \xC2\xBB") && has(why, "n'est pas permis"), "un : refuse, le message le dit (" + why + ")");
    check(!vf::normalize("A\\B", out, &why) && has(why, "n'est pas permis"), "un \\ refuse");
    check(!vf::normalize("A\nB", out, &why) && has(why, "contr\xC3\xB4le"), "un retour a la ligne refuse");
    check(!vf::normalize("A/B/C/D/E/F/G", out, &why) && has(why, "6 niveaux"), "7 niveaux : refuse (6 au plus)");
    check(vf::normalize("A/B/C/D/E/F", out) && out == "A/B/C/D/E/F", "6 niveaux : accepte");
    check(!vf::normalize(std::string(61, 'x'), out, &why) && has(why, "60 caract\xC3\xA8res"), "un nom de 61 caracteres : refuse");
    check(vf::key("Pompes") == vf::key(" pompes ") && vf::key("A / B") == vf::key("a/b") && vf::key("Pompes  A") == vf::key("pompes a"),
          "la cle : sans la casse ni les blancs en trop");
    check(vf::key("\xC3\x89tats") == vf::key("\xC3\xA9tats") && vf::key("\xC3\x89TATS") == vf::key("\xC3\xA9tats"), "la cle : une lettre accentuee sans sa casse");
    check(vf::resolve("pompes/P2", {"Pompes", "Pompes/Pompe1"}) == "Pompes/P2" && vf::resolve("POMPES/pompe1", {"Pompes/Pompe1"}) == "Pompes/Pompe1",
          "l'existant repris niveau par niveau ; un niveau neuf garde son ecriture");
    const auto px = [](const char* n) { return vf::prefixOf(n); };
    check(px("P1_Marche").text == "P1" && px("P1_Marche").equipment && px("Marche_P1").text == "P1", "P1_Marche, Marche_P1 : l'equipement P1");
    check(px("Pompe_1_Marche").text == "Pompe_1" && px("Pompe_1").text == "Pompe_1" && px("Niveau_Cuve_2").text == "Cuve_2",
          "Pompe_1_Marche : Pompe_1 (les chiffres seuls gardent le morceau d'avant)");
    check(px("Pompe1_Marche").text == "Pompe1" && px("Cuve_Niveau").text == "Cuve" && !px("Cuve_Niveau").equipment,
          "Cuve_Niveau : un prefixe simple, pas un equipement");
    check(px("Marche").text.empty() && px("P1").text.empty(), "un nom d'un seul morceau : aucun prefixe");
}

// ---- 2. un dossier par variable, un dossier par operation ; crees a l'application ------------------------
void givenFolders() {
    std::printf("un dossier par variable, par operation\n");
    Fixture f;
    auto p = f.proposal({vars({{"P1_Marche", "BOOL", "", ""}, {"P1_Defaut", "BOOL", "", ""}, {"PT1_Pression", "REAL", "", "", "Station de pompage/Mesures"}},
                              "Station de pompage/Pompes/P1")});
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && ck.newVariables.size() == 3, "valide (" + all(ck.errors) + ")");
    const Spec* m = shown(ck, "P1_Marche");
    const Spec* pt = shown(ck, "PT1_Pression");
    check(m && m->folder == "Station de pompage/Pompes/P1" && pt && pt->folder == "Station de pompage/Mesures",
          "l'apercu : le dossier de l'operation, sinon celui de la variable");
    check(madeFolders(ck, "Station de pompage/Pompes/P1") == 1 && madeFolders(ck, "Station de pompage/Mesures") == 1 && madeFolders(ck) == 2,
          "la validation dit les dossiers crees (une fois chacun)");
    check(std::any_of(ck.made.begin(), ck.made.end(), [](const ai::Check::Made& x) { return has(x.detail, "dossier cr\xC3\xA9\xC3\xA9 : Station de pompage/Mesures"); }),
          "<< dossier cree : Station de pompage/Mesures >>");
    check(!anyHas(ck.warnings, kDeduced) && !anyHas(ck.warnings, kMixed), "rien de deduit, aucun melange (" + all(ck.warnings) + ")");
    const std::size_t steps = f.stack.done().size();
    const auto foldersBefore = f.doc->project.programs.folders;
    std::string why;
    check(f.apply(p, &why) && f.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    check(f.var("P1_Marche") && f.var("P1_Marche")->folder == "Station de pompage/Pompes/P1" && f.var("PT1_Pression")
              && f.var("PT1_Pression")->folder == "Station de pompage/Mesures",
          "les variables sont dans leur dossier");
    check(f.listed("Station de pompage/Pompes") && f.listed("Station de pompage/Pompes/P1") && f.listed("Station de pompage/Mesures"),
          "les dossiers et les sous-dossiers existent");
    check(f.foldersLike("Station de pompage") == 1 && f.foldersLike("Station de pompage/Pompes/P1") == 1, "aucun double");
    (void)f.stack.undo();
    check(!f.var("P1_Marche") && f.doc->project.programs.folders == foldersBefore, "Ctrl+Z : les variables et les dossiers repartent");

    // Deux ecritures du meme dossier neuf dans une operation : un seul dossier.
    auto twice = f.proposal({vars({{"A1_X", "BOOL", "", "", "Zone A/Pompes"}, {"A2_X", "BOOL", "", "", "zone  a / POMPES"}})});
    const auto tk = ai::validate(f.doc->project, twice);
    check(tk.ok() && madeFolders(tk) == 1 && shown(tk, "A2_X") && shown(tk, "A2_X")->folder == "Zone A/Pompes",
          "le meme dossier ecrit deux fois : cree une fois, la seconde le reprend");
}

// ---- 3. l'existant repris (casse, blancs) ; creer_dossier et ranger --------------------------------------
void existingFolders() {
    std::printf("un dossier existant, repris\n");
    Fixture f;
    auto p = f.proposal({vars({{"Pompe1_Vitesse", "REAL", "", "", "  pompes /  POMPE1 "}})});
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && shown(ck, "Pompe1_Vitesse") && shown(ck, "Pompe1_Vitesse")->folder == "Pompes/Pompe1" && madeFolders(ck) == 0,
          "<< pompes / POMPE1 >> : le dossier Pompes/Pompe1 existant, rien de cree (" + all(ck.errors) + ")");
    std::string why;
    check(f.apply(p, &why) && f.var("Pompe1_Vitesse") && f.var("Pompe1_Vitesse")->folder == "Pompes/Pompe1", "appliquee : dans Pompes/Pompe1");
    check(f.foldersLike("Pompes/Pompe1") == 1 && f.foldersLike("Pompes") == 1, "jamais double");
    // creer_dossier d'un dossier de variables qui existe (autre casse) : rien a creer.
    auto mk = f.proposal({op(A::Folder, {{"liste", "variables"}, {"dossier", " POMPES "}})});
    const auto mc = ai::validate(f.doc->project, mk);
    check(mc.ok() && madeFolders(mc) == 0 && anyHas(mc.warnings, "existe d\xC3\xA9j\xC3\xA0"), "creer_dossier << POMPES >> : il existe deja (dit)");
    // ranger des variables dans << station DE pompage >> : le dossier existant.
    auto st = op(A::Store, {{"liste", "variables"}, {"dossier", "station DE pompage"}});
    st.objects = {"Niveau_Cuve"};
    auto sp = f.proposal({st});
    std::string why2;
    check(ai::validate(f.doc->project, sp).ok() && f.apply(sp, &why2) && f.var("Niveau_Cuve")->folder == "Station de pompage"
              && f.foldersLike("Station de pompage") == 1,
          "ranger dans << station DE pompage >> : Station de pompage, sans double (" + why2 + ")");
    auto bad = op(A::Folder, {{"liste", "variables"}, {"dossier", "Zone?1"}});
    check(anyHas(ai::validate(f.doc->project, f.proposal({bad})).errors, "invalide"), "creer_dossier de variables, un ? : refuse");
}

// ---- 4. un chemin invalide : refuse, avec la raison ------------------------------------------------------
void invalidFolders() {
    std::printf("un chemin invalide\n");
    Fixture f;
    const auto ck = ai::validate(f.doc->project, f.proposal({vars({{"X1_A", "BOOL", "", "", "Zone:1"}, {"X2_A", "BOOL", "", "", "A/B/C/D/E/F/G"},
                                                                   {"X3_A", "BOOL", "", "", "Bon/Dossier"}})}));
    check(!ck.ok() && anyHas(ck.errors, "X1_A") && anyHas(ck.errors, "n'est pas permis") && anyHas(ck.errors, "X2_A") && anyHas(ck.errors, "6 niveaux"),
          "un : et 7 niveaux : deux erreurs claires (" + all(ck.errors) + ")");
    check(ck.newVariables.size() == 1 && ck.newVariables[0].name == "X3_A", "la variable au bon chemin reste valable");
    const auto ok = ai::validate(f.doc->project, f.proposal({vars({{"X4_A", "BOOL", "", "", " / "}})}));
    check(ok.ok(), "un dossier fait de / et de blancs : aucun dossier (pas une erreur)");
    const auto oc = ai::validate(f.doc->project, f.proposal({vars({{"X5_A", "BOOL", "", ""}}, "Pompes|2")}));
    check(!oc.ok() && anyHas(oc.errors, "creer_variables") && anyHas(oc.errors, "n'est pas permis") && oc.newVariables.empty(),
          "le dossier de l'operation invalide : refuse, aucune variable");
    std::string why;
    check(!f.apply(f.proposal({vars({{"X6_A", "BOOL", "", "", "Zone*1"}})}), &why) && !f.var("X6_A"), "refusee : rien ne s'applique");
}

// ---- 5. sans dossier : deduit du prefixe, et dit ---------------------------------------------------------
void deducedFolders() {
    std::printf("sans dossier : deduit du prefixe\n");
    Fixture f;
    auto p = f.proposal({vars({{"Pompe1_Vitesse", "REAL", "", ""}, {"Marche_Pompe1", "BOOL", "", ""}})});
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && shown(ck, "Pompe1_Vitesse") && shown(ck, "Pompe1_Vitesse")->folder == "Pompes/Pompe1" && shown(ck, "Marche_Pompe1")
              && shown(ck, "Marche_Pompe1")->folder == "Pompes/Pompe1",
          "Pompe1_Vitesse, Marche_Pompe1 : dans Pompes/Pompe1, la ou vivent deja les variables Pompe1");
    check(anyHas(ck.warnings, "rang\xC3\xA9" "es dans \xC2\xAB Pompes/Pompe1 \xC2\xBB") && anyHas(ck.warnings, kDeduced + " Pompe1")
              && anyHas(ck.warnings, "Pompe1_Vitesse"),
          "l'apercu le dit (" + all(ck.warnings) + ")");
    check(madeFolders(ck) == 0, "un dossier existant : rien de cree");
    std::string why;
    check(f.apply(p, &why) && f.var("Pompe1_Vitesse")->folder == "Pompes/Pompe1", "appliquee : rangee dans Pompes/Pompe1 (" + why + ")");

    // Un prefixe commun a deux variables creees ensemble, sans variable existante : un dossier a son nom.
    Fixture g;
    auto q = g.proposal({vars({{"Four2_Temperature", "REAL", "", ""}, {"Four2_Consigne", "REAL", "", ""}, {"Compteur_Global", "DINT", "", ""}})});
    const auto qk = ai::validate(g.doc->project, q);
    check(qk.ok() && shown(qk, "Four2_Consigne") && shown(qk, "Four2_Consigne")->folder == "Four2" && madeFolders(qk, "Four2") == 1,
          "Four2_* : le dossier Four2, cree (" + all(qk.errors) + ")");
    check(anyHas(qk.warnings, kDeduced + " Four2") && anyHas(qk.warnings, "commun"), "dit : le prefixe commun");
    check(shown(qk, "Compteur_Global") && shown(qk, "Compteur_Global")->folder.empty() && !anyHas(qk.warnings, "Compteur_Global"),
          "une variable isolee sans prefixe partage : la racine, comme avant");
    check(g.apply(q) && g.var("Four2_Temperature")->folder == "Four2" && g.listed("Four2") && g.var("Compteur_Global")->folder.empty(),
          "appliquee : Four2 existe, Compteur_Global a la racine");

    // Un << ranger >> de la proposition passe avant : rien de deduit, aucun dossier Four3.
    Fixture h;
    auto st = op(A::Store, {{"liste", "variables"}, {"dossier", "Fours"}});
    st.objects = {"Four3_A", "Four3_B"};
    auto r = h.proposal({vars({{"Four3_A", "BOOL", "", ""}, {"Four3_B", "BOOL", "", ""}}), st});
    const auto rk = ai::validate(h.doc->project, r);
    check(rk.ok() && !anyHas(rk.warnings, kDeduced) && madeFolders(rk, "Four3") == 0 && shown(rk, "Four3_A") && shown(rk, "Four3_A")->folder == "Fours",
          "rangees par ranger : rien de deduit, l'apercu montre Fours (" + all(rk.warnings) + ")");
    check(h.apply(r) && h.var("Four3_A")->folder == "Fours" && !h.listed("Four3"), "appliquee : dans Fours, aucun dossier Four3");

    // L'IA integree : la meme regle (suggest).
    const auto s = vf::suggest(f.doc->project, {"Pompe1_Courant", "Four9_A", "Four9_B", "Seule"});
    check(s.size() == 4 && s[0] == "Pompes/Pompe1" && s[1] == "Four9" && s[2] == "Four9" && s[3].empty(), "suggest : la meme regle que l'apercu");
}

// ---- 6. le melange de deux equipements ---------------------------------------------------------------------
void mixedFolders() {
    std::printf("le melange\n");
    Fixture f;
    const auto ck = ai::validate(f.doc->project, f.proposal({vars({{"P1_Marche", "BOOL", "", ""}, {"P1_Defaut", "BOOL", "", ""},
                                                                   {"P2_Marche", "BOOL", "", ""}, {"P2_Defaut", "BOOL", "", ""}},
                                                                  "Station de pompage/Pompes")}));
    check(ck.ok() && anyHas(ck.warnings, kMixed + " 2 \xC3\xA9quipements (P1 et P2)")
              && anyHas(ck.warnings, "\xC2\xAB Station de pompage/Pompes/P1 \xC2\xBB et \xC2\xAB Station de pompage/Pompes/P2 \xC2\xBB"),
          "P1_* et P2_* dans un meme dossier : signale, la separation proposee (" + all(ck.warnings) + ")");
    const auto mk = ai::validate(f.doc->project, f.proposal({vars({{"PT1_Pression", "REAL", "", ""}, {"FT2_Debit", "REAL", "", ""},
                                                                   {"LT3_Niveau", "REAL", "", ""}},
                                                                  "Station de pompage/Mesures")}));
    check(mk.ok() && !anyHas(mk.warnings, kMixed), "une liste de mesures (une variable par capteur) : pas un melange");
    // Avec les variables deja la : Pompe2 dans le dossier de Pompe1.
    const auto ek = ai::validate(f.doc->project, f.proposal({vars({{"Pompe2_Marche", "BOOL", "", "", "Pompes/Pompe1"},
                                                                   {"Pompe2_Defaut", "BOOL", "", "", "Pompes/Pompe1"}})}));
    check(ek.ok() && anyHas(ek.warnings, "\xC2\xAB Pompes/Pompe1 \xC2\xBB " + kMixed + " 2 \xC3\xA9quipements (Pompe1 et Pompe2)")
              && anyHas(ek.warnings, "\xC2\xAB Pompes/Pompe1 \xC2\xBB et \xC2\xAB Pompes/Pompe2 \xC2\xBB"),
          "Pompe2 dans le dossier de Pompe1 : signale, a cote de lui (Pompes/Pompe2) (" + all(ek.warnings) + ")");
    // Le melange n'empeche rien : un avertissement.
    std::string why;
    check(f.apply(f.proposal({vars({{"P7_A", "BOOL", "", ""}, {"P7_B", "BOOL", "", ""}, {"P8_A", "BOOL", "", ""}, {"P8_B", "BOOL", "", ""}}, "Divers")}), &why)
              && f.var("P8_B") && f.var("P8_B")->folder == "Divers",
          "un melange s'applique quand meme (l'utilisateur decide) (" + why + ")");
}

// ---- 7. le JSON, la lecture de l'outil, le schema -------------------------------------------------------
void json() {
    std::printf("le JSON\n");
    std::vector<ai::Operation> ops = {vars({{"P1_Marche", "BOOL", "Marche", "", "Station/Pompes/P1"}, {"P1_Defaut", "BOOL", "", "FALSE"}}, "Station/Pompes")};
    const ai::json::Value v = ai::operationsToJson(ops);
    const std::string text = ai::json::write(v, false);
    check(v.isArray() && v.items().size() == 1 && v.items()[0].str("dossier") == "Station/Pompes" && !v.items()[0].has("proprietes")
              && v.items()[0].at("variables").items()[0].str("dossier") == "Station/Pompes/P1" && !v.items()[0].at("variables").items()[1].has("dossier"),
          "ecrit : le dossier de l'operation au premier niveau, celui de la variable dans la variable (" + text + ")");
    std::vector<ai::Operation> back;
    std::string why;
    check(ai::operationsFromJson(v, back, &why) && back == ops, "relu a l'identique (" + why + ")");
    ai::json::Value old;
    check(ai::json::parse("[{\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"A_1\", \"type\": \"BOOL\"}]}]", old), "un JSON d'avant");
    check(ai::operationsFromJson(old, back, &why) && back.size() == 1 && back[0].variables.size() == 1 && back[0].variables[0].folder.empty()
              && back[0].props.empty(),
          "sans dossier : relu comme avant (aucun dossier)");
    Fixture f;
    auto p = f.proposal(ops);
    const auto q = ai::proposalFromJson(ai::toJson(p));
    check(q.ops == p.ops, "une proposition entiere relue : ses dossiers");

    // La lecture stricte de l'outil.
    ai::PromptInput in;
    in.project = &f.doc->project;
    const auto read = [&](const std::string& o) {
        why.clear();
        return ai::proposalFromTool("{\"resume\": \"x\", \"operations\": [" + o + "]}", in, "demande", &why);
    };
    auto r = read("{\"action\": \"creer_variables\", \"dossier\": \"Station de pompage/Pompes\", \"variables\": [{\"nom\": \"P3_Marche\", \"type\": \"BOOL\", "
                  "\"dossier\": \"Station de pompage/Pompes/P3\"}, {\"nom\": \"P3_Defaut\", \"type\": \"BOOL\"}]}");
    check(r && r->ops.size() == 1 && r->ops[0].props.size() == 1 && r->ops[0].props[0].second == "Station de pompage/Pompes"
              && r->ops[0].variables[0].folder == "Station de pompage/Pompes/P3" && r->ops[0].variables[1].folder.empty(),
          "lu : le dossier de l'operation et celui d'une variable (" + why + ")");
    check(!read("{\"action\": \"creer_variables\", \"dossier\": 3, \"variables\": [{\"nom\": \"A\", \"type\": \"BOOL\"}]}") && has(why, "dossier"),
          "un dossier qui n'est pas un texte : refuse");
    check(!read("{\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"A\", \"type\": \"BOOL\", \"repertoire\": \"X\"}]}") && has(why, "repertoire"),
          "un champ inconnu dans une variable : toujours refuse");
    ai::json::Value tools;
    check(ai::json::parse(ai::operationsTool(), tools), "le schema se lit");
    const auto& props = tools.items().front().at("input_schema").at("properties").at("operations").at("items").at("properties");
    check(props.at("variables").at("items").at("properties").has("dossier") && props.at("variables").at("items").at("required").items().size() == 2
              && has(props.at("dossier").str("description"), "creer_variables"),
          "le schema : dossier dans une variable (facultatif) et pour l'operation");
}

// ---- 8. les consignes, le contexte ------------------------------------------------------------------------
void promptAndContext() {
    std::printf("les consignes, le contexte\n");
    Fixture f;
    ai::PromptInput in;
    in.project = &f.doc->project;
    in.view = f.doc->project.view(f.view);
    in.request = "cree les variables de la pompe 2";
    const std::string sys = ai::systemPrompt(in);
    check(has(sys, "18. Ranger les variables") && has(sys, "Station de pompage/Pompes/P1") && has(sys, "Station de pompage/Vannes/V1")
              && has(sys, "Station de pompage/Mesures") && has(sys, "4 niveaux au plus") && has(sys, "(Commun)") && has(sys, "Compteurs")
              && has(sys, "jamais deux \xC3\xA9quipements dans le m\xC3\xAAme dossier final"),
          "les consignes : la regle 18, l'exemple de la station, les natures, le dossier commun");
    ai::ContextInfo info;
    const std::string ctx = ai::contextText(in, {}, &info);
    check(has(ctx, "Dossiers des variables IHM (3,") && has(ctx, "\nPompes (0, 2 en tout)\n") && has(ctx, "\n  Pompe1 (2)\n")
              && has(ctx, "\nStation de pompage (0)\n") && has(ctx, "\xC3\x80 la racine (sans dossier) : 2 variables"),
          "le contexte : l'arborescence reelle et ses comptes");
    check(info.folders == 3 && info.foldersListed == 3, "tous les dossiers montres (3)");
    check(has(ctx, "Pompe1_Marche : BOOL | Pompes/Pompe1"), "le dossier de chaque variable listee");
    // Un gros projet : borne, les premiers niveaux d'abord.
    Fixture big;
    auto& bp = big.doc->project;
    for (int z = 1; z <= 5; ++z)
        for (int e = 1; e <= 40; ++e) {
            hmi::Variable v;
            v.id = bp.allocate();
            v.name = "Z" + std::to_string(z) + "_E" + std::to_string(e) + "_Marche";
            v.type = "BOOL";
            v.folder = "Zone " + std::to_string(z) + "/Equipement " + std::to_string(e);
            bp.programs.variables.push_back(v);
        }
    ai::PromptInput bin;
    bin.project = &bp;
    ai::ContextLimits lim;
    lim.folderBytes = 400;
    ai::ContextInfo binfo;
    const std::string bctx = ai::contextText(bin, lim, &binfo);
    check(binfo.folders == 208 && binfo.foldersListed < binfo.folders && has(bctx, "autres dossiers"),
          "un gros projet : l'arborescence bornee, le reste compte (" + std::to_string(binfo.foldersListed) + "/" + std::to_string(binfo.folders) + ")");
    check(has(bctx, "\nZone 1 (0, 40 en tout)\n") && has(bctx, "\nZone 5 (0, 40 en tout)\n"), "les premiers niveaux toujours la");
    const std::string tree = vf::treeText(bp, 400);
    check(tree.size() < 1000, "la borne tient (" + std::to_string(tree.size()) + " octets)");
    hmi::Project none;
    check(has(vf::treeText(none, 400), "aucun"), "sans dossier : dit");
}

// ---- 9. l'IA integree --------------------------------------------------------------------------------------
void integrated() {
    std::printf("l'IA integree\n");
    Fixture f;
    ai::PromptInput in;
    in.project = &f.doc->project;
    in.view = f.doc->project.view(f.view);
    auto r = ai::understand(in, "cr\xC3\xA9" "e les variables Four5_Marche et Four5_Defaut en BOOL");
    const ai::Operation* o = r.proposal && !r.proposal->ops.empty() ? &r.proposal->ops.front() : nullptr;
    check(o && o->action == A::Variables && o->variables.size() == 2 && o->variables[0].folder == "Four5" && o->variables[1].folder == "Four5",
          "deux variables Four5_* : le dossier Four5");
    check(has(r.text, "Rangement") && has(r.text, "**Four5**"), "la reponse le dit");
    if (r.proposal) {
        const auto ck = ai::validate(f.doc->project, *r.proposal);
        check(ck.ok() && !anyHas(ck.warnings, kDeduced) && madeFolders(ck, "Four5") == 1, "valide : le dossier donne, rien de deduit apres coup");
    }
    r = ai::understand(in, "cr\xC3\xA9" "e la variable Pompe1_Courant en REAL");
    o = r.proposal && !r.proposal->ops.empty() ? &r.proposal->ops.front() : nullptr;
    check(o && o->variables.size() == 1 && o->variables[0].folder == "Pompes/Pompe1", "Pompe1_Courant : avec les variables de la pompe 1");
    r = ai::understand(in, "cr\xC3\xA9" "e la variable Seule_Mesure en REAL");
    o = r.proposal && !r.proposal->ops.empty() ? &r.proposal->ops.front() : nullptr;
    check(o && o->variables.size() == 1 && o->variables[0].folder.empty() && !has(r.text, "Rangement"), "une variable isolee : la racine, rien a dire");
}

} // namespace

int main() {
    // Le dossier IA des essais (les connaissances relues par les consignes).
    const fs::path root = fs::temp_directory_path() / "xpg_ai_dossiers_racine";
    fs::remove_all(root);
    fs::create_directories(root);
    ai::setRoot(U(root / "IA"));
    rules();
    givenFolders();
    existingFolders();
    invalidFolders();
    deducedFolders();
    mixedFolders();
    json();
    promptAndContext();
    integrated();
    fs::remove_all(root);
    std::printf("ai_dossiers_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
