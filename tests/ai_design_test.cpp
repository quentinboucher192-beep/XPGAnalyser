// =============================================================================
//  tests/ai_design_test.cpp - 1.19.0 : l'IA qui concoit et construit des projets complets (AiDesign)
// -----------------------------------------------------------------------------
//    ai_design_test [bac/Armoire_Gaz]
//
//  Sans ecran ni reseau, sur un projet IHM VIDE et sur un VRAI projet (le bac Armoire_Gaz, COPIE dans le dossier
//  temporaire : l'original n'est jamais ecrit) :
//    - les quatre installations (station de pompage, ligne de production, parc de cuves, regulation) : la
//      proposition passe app::ai::validate SANS UNE ERREUR (les proprietes, les formules, les codes compiles), puis
//      makeCommand l'applique en UN pas d'annulation ; tout est la (types, variables, equipement simule, adresses
//      sans chevauchement, alarmes, scripts, symbole, popup, vue et son bouton Details) ; Ctrl+Z rend le projet
//      EXACT (operator== du projet entier, le compteur d'identifiants mis a part : il ne recule jamais, voulu) ;
//      Ctrl+Y le refait a l'identique ;
//    - la REUTILISATION : la station deux fois, T_Pompe deja la (identique, ou avec d'autres membres), une variable
//      Pompe_1 d'un autre type, une pompe de plus dans la vue existante ;
//    - modifier l'existant : etendre une serie, dupliquer, une alarme sur une serie, remplacer un symbole, ajouter
//      une mesure, ranger dans un dossier - chaque texte dit combien d'elements sont concernes ;
//    - les temps : la station a 3 pompes construite et validee bien sous la seconde ; une GRANDE installation
//      (20 pompes, 10 cuves) : construite, validee, appliquee, ses temps affiches.
//  Aucun code cree n'est execute : les scripts sont seulement compiles par la validation.
// =============================================================================
#include "../src/app/ai/AiDesign.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiAudit.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace dz = app::ai::design;
using A = ai::Operation::Action;

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

std::string errorsOf(const ai::Check& c) {
    std::string s;
    for (std::size_t i = 0; i < c.errors.size() && i < 6; ++i) s += c.errors[i] + " | ";
    if (c.errors.size() > 6) s += "... (" + std::to_string(c.errors.size()) + " erreurs)";
    return s;
}

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::size_t opsOf(const ai::Proposal& p, A a) {
    return static_cast<std::size_t>(std::count_if(p.ops.begin(), p.ops.end(), [&](const ai::Operation& o) { return o.action == a; }));
}
std::size_t newVariablesOf(const ai::Proposal& p) {
    std::size_t n = 0;
    for (const auto& o : p.ops)
        if (o.action == A::Variables) n += o.variables.size();
    return n;
}
bool viewOp(const ai::Proposal& p, const std::string& name) {
    return std::any_of(p.ops.begin(), p.ops.end(), [&](const ai::Operation& o) { return o.action == A::View && o.name == name; });
}

// Le projet entier, sans son compteur d'identifiants (il ne recule jamais : voulu, HmiCommands.cpp).
hmi::Project comparable(hmi::Project p) {
    p.nextId = 0;
    return p;
}
// L'empreinte du projet enregistre (tous ses fichiers), le compteur mis a part.
std::string printOf(const hmi::Project& p) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& f : hmi::serializeProject(comparable(p))) {
        for (const char c : f.path) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        if (f.data)
            for (const auto c : *f.data) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    return std::to_string(h);
}

struct Fixture {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    explicit Fixture(hmi::Project p = {}) { doc->project = std::move(p); }
    [[nodiscard]] hmi::Project& pr() { return doc->project; }
    bool apply(const ai::Proposal& p, std::string* why = nullptr) {
        auto cmd = ai::makeCommand(doc, p, why);
        if (!cmd) return false;
        return static_cast<bool>(stack.push(std::move(cmd)));
    }
};

// La proposition d'une conception, avec sa demande (les descriptions creees la citent).
dz::Design withRequest(dz::Design d, const std::string& request) {
    d.proposal.request = request;
    return d;
}

// VALIDER SANS ERREUR, APPLIQUER EN UN PAS, ANNULER EXACT, REFAIRE A L'IDENTIQUE. Faux : pas applique.
bool roundTrip(Fixture& f, const dz::Design& d, const std::string& what, ai::Check* checked = nullptr) {
    const hmi::Project before = f.pr();
    const std::string printBefore = printOf(before);
    const ai::Check ck = ai::validate(f.pr(), d.proposal);
    if (checked) *checked = ck;
    check(ck.ok(), what + " : valid\xC3\xA9" "e sans erreur (" + errorsOf(ck) + ")");
    check(ck.outcomes.size() == d.proposal.ops.size()
              && std::all_of(ck.outcomes.begin(), ck.outcomes.end(), [](const ai::Check::Outcome& o) { return o.state == ai::Check::Outcome::State::Validated; }),
          what + " : chaque op\xC3\xA9ration valid\xC3\xA9" "e");
    std::string why;
    const std::size_t steps = f.stack.done().size();
    const bool applied = f.apply(d.proposal, &why);
    check(applied && f.stack.done().size() == steps + 1, what + " : appliqu\xC3\xA9" "e en UNE commande (" + why + ")");
    if (!applied) return false;
    const hmi::Project after = f.pr();
    check(!(comparable(after) == comparable(before)), what + " : le projet a chang\xC3\xA9");
    (void)f.stack.undo();
    check(comparable(f.pr()) == comparable(before) && printOf(f.pr()) == printBefore, what + " : Ctrl+Z rend le projet exact");
    (void)f.stack.redo();
    check(comparable(f.pr()) == comparable(after), what + " : Ctrl+Y refait \xC3\xA0 l'identique");
    return true;
}

const hmi::Object* objectNamed(const hmi::View& v, const std::string& name) {
    for (const auto& o : v.objects)
        if (o.name == name) return &o;
    return nullptr;
}
std::size_t objectsCiting(const hmi::View& v, const std::string& var) {
    std::size_t n = 0;
    for (const auto& o : v.objects) {
        bool hit = false;
        for (const auto& p : o.props) hit = hit || has(p.value, var) || has(p.expr, var);
        for (const auto& a : o.actions) hit = hit || has(a.value, var) || has(a.target, var);
        n += hit ? 1 : 0;
    }
    return n;
}
const hmi::Object* popupButtonFor(const hmi::View& v, const std::string& popup, const std::string& argument) {
    for (const auto& o : v.objects)
        for (const auto& a : o.actions)
            if (a.operation == hmi::Operation::Popup && a.target == popup && has(a.value, argument)) return &o;
    return nullptr;
}

// Le bac Armoire_Gaz, COPIE dans le dossier temporaire (l'original n'est jamais ecrit), relu.
std::optional<hmi::Project> demoProject(const std::string& given) {
    std::vector<fs::path> tries;
    if (!given.empty()) tries.emplace_back(given);
    for (const char* c : {"bac/Armoire_Gaz", "../bac/Armoire_Gaz", "../../bac/Armoire_Gaz"}) tries.emplace_back(c);
    for (const auto& src : tries) {
        std::error_code ec;
        if (!fs::is_directory(src / "ihm", ec)) continue;
        const fs::path copy = fs::temp_directory_path() / "xpg_ai_design_bac" / "Armoire_Gaz";
        fs::remove_all(copy.parent_path(), ec);
        fs::create_directories(copy, ec);
        fs::copy(src, copy, fs::copy_options::recursive, ec);
        if (ec) continue;
        auto loaded = hmi::load(copy.string());
        if (loaded) return std::move(*loaded);
    }
    return std::nullopt;
}

// ---- la station de pompage, de bout en bout (le projet vide) ----------------------------------------
hmi::Project stationEndToEnd() {
    std::printf("station de pompage (projet vide)\n");
    Fixture f;
    dz::InstallationSpec spec;   // 2 pompes, 1 cuve, tout
    const auto t0 = std::chrono::steady_clock::now();
    const auto d = withRequest(dz::installation(f.pr(), spec),
                               "Cr\xC3\xA9" "e une station de pompage compl\xC3\xA8te avec deux pompes, une cuve, les \xC3\xA9quipements Modbus, les variables, "
                               "les alarmes, les scripts, les symboles, les popups et la vue de supervision.");
    const double build = msSince(t0);
    const auto& p = d.proposal;
    std::printf("  %zu op\xC3\xA9rations : %zu types, %zu variables, %zu \xC3\xA9quipement, %zu adresses, %zu alarmes, %zu scripts, %zu vues, %zu objets, %zu codes, %zu rangement\n",
                p.ops.size(), opsOf(p, A::Type), newVariablesOf(p), opsOf(p, A::Equipment), opsOf(p, A::Address), opsOf(p, A::Alarm), opsOf(p, A::Script),
                opsOf(p, A::View), opsOf(p, A::Create), opsOf(p, A::Code), opsOf(p, A::Store));
    check(opsOf(p, A::Type) == 2 && newVariablesOf(p) == 3 && opsOf(p, A::Equipment) == 1 && opsOf(p, A::Address) == 3, "types (2), variables (3), \xC3\xA9quipement (1), adresses (3)");
    check(opsOf(p, A::Alarm) == 4 && opsOf(p, A::Script) == 2 && opsOf(p, A::View) == 3 && opsOf(p, A::Store) == 1, "alarmes (4), scripts (2), vues (3 : symbole, popup, supervision), rangement");
    check(viewOp(p, "Sym_Pompe") && viewOp(p, "Pop_Pompe") && viewOp(p, "Vue_Station"), "Sym_Pompe, Pop_Pompe, Vue_Station");
    check(has(d.text, "2 pompes") && has(d.text, "1 cuve") && has(d.text, "T_Pompe") && has(d.text, "Station_Modbus") && has(d.text, "simul\xC3\xA9")
              && has(d.text, "Sym_Pompe") && has(d.text, "Pop_Pompe") && has(d.text, "Vue_Station") && has(d.text, "4 (") && has(d.text, "un seul Ctrl+Z"),
          "le texte dit combien de chaque, et un seul Ctrl+Z");
    check(anyHas(d.limits, "instances") && anyHas(d.limits, "commandes"), "le texte dit ses limites (instances de symbole, commandes de la popup)");
    check(p.view == "Vue_Station" && !ai::usesOpenView(p) && !p.fingerprint.empty(), "la port\xC3\xA9" "e : la vue neuve Vue_Station, aucune vue ouverte ; l'empreinte prise");
    const auto t1 = std::chrono::steady_clock::now();
    const ai::Check ck = ai::validate(f.pr(), p);
    const double validate = msSince(t1);
    std::printf("  construite en %.1f ms, valid\xC3\xA9" "e en %.1f ms (%zu avertissements)\n", build, validate, ck.warnings.size());
    for (const auto& w : ck.warnings) std::printf("    avertissement : %s\n", w.c_str());
    check(ck.ok() && ck.errors.empty(), "z\xC3\xA9ro erreur (" + errorsOf(ck) + ")");
    check(ck.newTypes.size() == 2 && ck.newVariables.size() == 3 && ck.reused.empty(), "la v\xC3\xA9rification compte 2 types et 3 variables neufs, rien de r\xC3\xA9utilis\xC3\xA9");
    check(roundTrip(f, d, "la station"), "la station s'applique");
    const auto& pr = f.pr();
    const auto* tp = pr.hmiTypeByName("T_Pompe");
    const std::vector<std::pair<std::string, std::string>> wanted = {{"Marche", "BOOL"}, {"Defaut", "BOOL"}, {"Disponible", "BOOL"}, {"Mode_Auto", "BOOL"},
                                                                     {"Consigne", "REAL"}, {"Intensite", "REAL"}, {"Heures_Fonctionnement", "DINT"}};
    bool exact = tp && tp->members.size() == wanted.size();
    for (std::size_t i = 0; exact && i < wanted.size(); ++i) exact = tp->members[i].name == wanted[i].first && tp->members[i].type == wanted[i].second;
    check(exact, "T_Pompe : exactement Marche, Defaut, Disponible, Mode_Auto (BOOL), Consigne, Intensite (REAL), Heures_Fonctionnement (DINT)");
    const auto* p1 = pr.variable("Pompe_1");
    const auto* p2 = pr.variable("Pompe_2");
    const auto* c1 = pr.variable("Cuve_1");
    check(p1 && p2 && c1 && p1->type == "T_Pompe" && c1->type == "T_Cuve", "Pompe_1, Pompe_2 : T_Pompe ; Cuve_1 : T_Cuve");
    const auto* eq = pr.equipmentByName("Station_Modbus");
    check(eq && eq->simulated && eq->modbus(), "Station_Modbus : Modbus TCP simul\xC3\xA9");
    check(p1 && p2 && c1 && p1->equipment == "Station_Modbus" && p2->equipment == "Station_Modbus" && c1->equipment == "Station_Modbus" && !p1->address.empty()
              && p1->address != p2->address,
          "les trois structures adress\xC3\xA9" "es sur Station_Modbus (" + std::string(c1 ? c1->address : "") + ", " + std::string(p1 ? p1->address : "") + ", "
              + std::string(p2 ? p2->address : "") + ")");
    check(hmi::audit::overlaps(pr, "Station_Modbus").empty(), "aucun chevauchement d'adresses");
    check(pr.alarmByName("Pompe_1_Defaut") && pr.alarmByName("Pompe_2_Defaut") && pr.alarmByName("Cuve_1_Niveau_Haut") && pr.alarmByName("Cuve_1_Niveau_Bas"),
          "les alarmes : d\xC3\xA9" "faut par pompe, niveaux haut et bas");
    check(pr.generalScript("Station_Init") && pr.generalScript("Station_Cycle") && pr.generalScript("Station_Cycle")->event == "Cyclique",
          "les scripts Station_Init (d\xC3\xA9marrage) et Station_Cycle (cyclique)");
    const auto* sym = pr.viewByName("Sym_Pompe");
    const auto* pop = pr.viewByName("Pop_Pompe");
    const auto* vue = pr.viewByName("Vue_Station");
    check(sym && sym->role == "symbole" && sym->params.size() == 1 && sym->params[0].type == "T_Pompe" && !sym->objects.empty(), "Sym_Pompe : symbole, param\xC3\xA8tre Pompe : T_Pompe, ses objets");
    check(pop && pop->role == "popup" && pop->param("Pompe") && pop->objects.size() > 10, "Pop_Pompe : popup, param\xC3\xA8tre Pompe, ses objets");
    check(vue && vue->role == "vue" && objectNamed(*vue, "Pompe_1") && objectNamed(*vue, "Cuve_1") && objectNamed(*vue, "Collecteur") && objectNamed(*vue, "Titre"),
          "Vue_Station : les pompes, la cuve, le collecteur, le titre");
    check(vue && objectNamed(*vue, "Pompe_1") && objectNamed(*vue, "Pompe_1")->text("variable") == "Pompe_1.Marche"
              && objectNamed(*vue, "Pompe_1")->expr("fault") == "Pompe_1.Defaut",
          "la pompe 1 : sa marche (variable) et son d\xC3\xA9" "faut (formule)");
    check(vue && popupButtonFor(*vue, "Pop_Pompe", "Pompe := Pompe_2"), "un bouton D\xC3\xA9tails ouvre Pop_Pompe sur Pompe_2");
    check(sym && pop && vue && sym->folder == "Station" && pop->folder == "Station" && vue->folder == "Station" && p1 && p1->folder == "Station",
          "le dossier Station : la vue, le symbole, la popup, les variables");
    return pr;
}

// ---- la reutilisation -------------------------------------------------------------------------------------
void reuse(const hmi::Project& station) {
    std::printf("r\xC3\xA9utilisation\n");
    {
        Fixture f(station);
        const auto d = withRequest(dz::installation(f.pr(), {}), "la m\xC3\xAAme station");
        const auto ck = ai::validate(f.pr(), d.proposal);
        check(ck.ok(), "la station une seconde fois : valide (" + errorsOf(ck) + ")");
        check(std::any_of(ck.reused.begin(), ck.reused.end(), [](const ai::Check::Made& m) { return m.name == "T_Pompe"; })
                  && std::any_of(ck.reused.begin(), ck.reused.end(), [](const ai::Check::Made& m) { return m.name == "Pompe_1"; }),
              "Check::reused : T_Pompe, Pompe_1 (" + std::to_string(ck.reused.size()) + ")");
        check(ck.newTypes.empty() && ck.newVariables.empty() && !viewOp(d.proposal, "Sym_Pompe") && !viewOp(d.proposal, "Pop_Pompe") && !viewOp(d.proposal, "Vue_Station")
                  && opsOf(d.proposal, A::Alarm) == 0 && opsOf(d.proposal, A::Script) == 0 && opsOf(d.proposal, A::Address) == 0,
              "rien n'est recr\xC3\xA9\xC3\xA9 : ni type, ni variable, ni symbole, ni popup, ni vue, ni alarme, ni script, ni adresse");
        check(has(d.text, "Tout existe d\xC3\xA9j\xC3\xA0") && has(d.text, "R\xC3\xA9utilis\xC3\xA9") && anyHas(d.reused, "T_Pompe") && anyHas(d.reused, "Sym_Pompe"),
              "le texte et Design::reused le disent");
        std::string why;
        const hmi::Project before = f.pr();
        check(!f.apply(d.proposal, &why) && has(why, "ne change rien") && comparable(f.pr()) == comparable(before), "appliquer : rien ne change (" + why + ")");
    }
    {
        Fixture f(station);
        dz::InstallationSpec spec;
        spec.pumps = 3;
        const auto d = withRequest(dz::installation(f.pr(), spec), "une troisi\xC3\xA8me pompe");
        ai::Check ck;
        check(roundTrip(f, d, "la station \xC3\xA0 3 pompes sur celle \xC3\xA0 2", &ck), "la station \xC3\xA0 3 pompes s'applique");
        check(ck.newVariables.size() == 1 && ck.newVariables[0].name == "Pompe_3" && !viewOp(d.proposal, "Vue_Station") && opsOf(d.proposal, A::Alarm) == 1,
              "seule Pompe_3 est neuve (sa variable, son alarme), la vue existante est compl\xC3\xA9t\xC3\xA9" "e");
        const auto* vue = f.pr().viewByName("Vue_Station");
        check(vue && objectNamed(*vue, "Pompe_3") && popupButtonFor(*vue, "Pop_Pompe", "Pompe := Pompe_3"), "Vue_Station montre Pompe_3 et son bouton D\xC3\xA9tails");
        check(hmi::audit::overlaps(f.pr(), "Station_Modbus").empty(), "Pompe_3 adress\xC3\xA9" "e sans chevauchement (" + std::string(f.pr().variable("Pompe_3") ? f.pr().variable("Pompe_3")->address : "") + ")");
        check(anyHas(d.limits, "Station_Cycle") || anyHas(d.reused, "Station_Cycle"), "le script existant n'est pas r\xC3\xA9\xC3\xA9" "crit : le texte le dit");
    }
    {
        // T_Pompe deja cree, identique : reutilise (la validation le dit)
        Fixture f;
        ai::Operation t;
        t.action = A::Type;
        t.name = "T_Pompe";
        t.members = {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}, {"Disponible", "BOOL", ""}, {"Mode_Auto", "BOOL", ""},
                     {"Consigne", "REAL", ""}, {"Intensite", "REAL", ""}, {"Heures_Fonctionnement", "DINT", ""}};
        ai::Proposal first;
        first.ops = {t};
        std::string why;
        check(f.apply(first, &why), "T_Pompe cr\xC3\xA9\xC3\xA9 d'abord (" + why + ")");
        const auto d = withRequest(dz::installation(f.pr(), {}), "station");
        ai::Check ck;
        check(roundTrip(f, d, "la station sur T_Pompe existant", &ck), "appliqu\xC3\xA9" "e");
        check(std::any_of(ck.reused.begin(), ck.reused.end(), [](const ai::Check::Made& m) { return m.what == "Type" && m.name == "T_Pompe"; })
                  && ck.newTypes.size() == 1 && ck.newTypes[0].first == "T_Cuve",
              "T_Pompe r\xC3\xA9utilis\xC3\xA9 (Check::reused), seul T_Cuve est neuf");
        check(anyHas(d.reused, "T_Pompe") && has(d.text, "T_Pompe"), "le texte dit T_Pompe r\xC3\xA9utilis\xC3\xA9");
        check(f.pr().programs.types.size() == 2, "aucun type en double");
    }
    {
        // T_Pompe deja la avec D'AUTRES membres : garde tel quel, l'installation suit ses membres
        Fixture f;
        hmi::HmiType t;
        t.id = f.pr().allocate();
        t.name = "T_Pompe";
        t.members = {{"Marche", "BOOL"}, {"Vitesse", "REAL"}, {"Auto", "BOOL"}};
        f.pr().programs.types.push_back(t);
        const auto d = withRequest(dz::installation(f.pr(), {}), "station");
        check(!std::any_of(d.proposal.ops.begin(), d.proposal.ops.end(), [](const ai::Operation& o) { return o.action == A::Type && o.name == "T_Pompe"; }),
              "aucun creer_type T_Pompe (il serait refus\xC3\xA9) : le type du projet sert tel quel");
        check(roundTrip(f, d, "la station sur un T_Pompe diff\xC3\xA9rent"), "appliqu\xC3\xA9" "e");
        check(!f.pr().alarmByName("Pompe_1_Defaut") && f.pr().alarmByName("Cuve_1_Niveau_Haut") && anyHas(d.limits, "D\xC3\xA9" "faut"),
              "sans membre Defaut : pas d'alarme de d\xC3\xA9" "faut de pompe, et le texte le dit");
        const auto* tp = f.pr().hmiTypeByName("T_Pompe");
        check(tp && tp->members.size() == 3, "T_Pompe n'a pas chang\xC3\xA9");
        const auto* sym = f.pr().viewByName("Sym_Pompe");
        check(sym && std::any_of(sym->objects.begin(), sym->objects.end(), [](const hmi::Object& o) { return o.expr("value") == "Pompe.Auto"; }),
              "le symbole suit les membres du type existant (Auto pour Mode_Auto)");
    }
    {
        // une variable Pompe_1 d'un autre type : jamais remplacee, la pompe prend le numero libre suivant
        Fixture f;
        hmi::Variable v;
        v.id = f.pr().allocate();
        v.name = "Pompe_1";
        v.type = "BOOL";
        f.pr().programs.variables.push_back(v);
        const auto d = withRequest(dz::installation(f.pr(), {}), "station");
        check(roundTrip(f, d, "la station avec une variable Pompe_1 en BOOL"), "appliqu\xC3\xA9" "e");
        check(f.pr().variable("Pompe_1")->type == "BOOL" && f.pr().variable("Pompe_2") && f.pr().variable("Pompe_3") && f.pr().variable("Pompe_3")->type == "T_Pompe",
              "Pompe_1 garde son type ; les pompes sont Pompe_2 et Pompe_3");
        check(anyHas(d.limits, "Pompe_1 (BOOL)"), "le texte le dit");
    }
}

// ---- les quatre installations, sur le projet vide et sur le vrai projet -----------------------------------
void installations(const std::optional<hmi::Project>& demo) {
    std::printf("les quatre installations\n");
    struct Case {
        dz::Installation kind;
        int              pumps, tanks, loops;
        const char*      view;
    };
    const Case cases[] = {{dz::Installation::PumpingStation, 3, 2, 1, "Vue_Station"},
                          {dz::Installation::ProductionLine, 4, 0, 0, "Vue_Ligne"},
                          {dz::Installation::TankFarm, 0, 3, 0, "Vue_Parc_Cuves"},
                          {dz::Installation::Regulation, 2, 0, 2, "Vue_Regulation"}};
    for (int onDemo = 0; onDemo < 2; ++onDemo) {
        if (onDemo && !demo) {
            std::printf("  (bac/Armoire_Gaz absent : le vrai projet n'est pas essay\xC3\xA9)\n");
            continue;
        }
        for (const auto& c : cases) {
            Fixture f(onDemo ? *demo : hmi::Project{});
            dz::InstallationSpec spec;
            spec.kind = c.kind;
            spec.pumps = c.pumps;
            spec.tanks = c.tanks;
            spec.loops = c.loops;
            const auto t0 = std::chrono::steady_clock::now();
            const auto d = withRequest(dz::installation(f.pr(), spec), std::string(dz::installationLabel(c.kind)));
            const std::string what = std::string(dz::installationLabel(c.kind)) + (onDemo ? " (Armoire_Gaz)" : " (vide)");
            ai::Check ck;
            check(roundTrip(f, d, what, &ck), what + " : appliqu\xC3\xA9" "e");
            const double ms = msSince(t0);
            const std::string view = ai::newViewName(d.proposal);
            const auto* v = f.pr().viewByName(view);
            check(v && v->objects.size() > 5 && opsOf(d.proposal, A::Alarm) > 0 && opsOf(d.proposal, A::Script) > 0 && opsOf(d.proposal, A::Address) > 0,
                  what + " : la vue " + view + ", des alarmes, un script, des adresses");
            if (!onDemo) check(view == c.view, what + " : la vue s'appelle " + c.view);
            if (onDemo && c.kind == dz::Installation::ProductionLine) {
                // Armoire_Gaz a deja SA Vue_Ligne (une autre ligne) : elle n'est pas touchee, la vue neuve prend un autre nom
                const auto* mine = f.pr().viewByName("Vue_Ligne");
                const auto* before = demo->viewByName("Vue_Ligne");
                check(view == "Vue_Ligne_2" && mine && before && *mine == *before, what + " : la Vue_Ligne de l'utilisateur n'est pas touch\xC3\xA9" "e (la vue neuve : " + view + ")");
            }
            check(hmi::audit::overlaps(f.pr()).size() == hmi::audit::overlaps(onDemo ? *demo : hmi::Project{}).size(),
                  what + " : aucun chevauchement d'adresses ajout\xC3\xA9");
            std::printf("  %-34s %3zu op\xC3\xA9rations, %2zu avertissements, %.0f ms (tout compris)\n", what.c_str(), d.proposal.ops.size(), ck.warnings.size(), ms);
            if (onDemo && c.kind == dz::Installation::TankFarm)
                check(anyHas(d.reused, "T_Vanne") && !std::any_of(d.proposal.ops.begin(), d.proposal.ops.end(), [](const ai::Operation& o) { return o.action == A::Type && o.name == "T_Vanne"; }),
                      "Armoire_Gaz a d\xC3\xA9j\xC3\xA0 T_Vanne (3 membres) : r\xC3\xA9utilis\xC3\xA9 tel quel, pas recr\xC3\xA9\xC3\xA9");
        }
    }
}

// ---- les reglages de la demande : un nom donne, des parties retirees, rien a construire -------------------------
void options() {
    std::printf("les r\xC3\xA9glages de la demande\n");
    {
        Fixture f;
        dz::InstallationSpec spec;
        spec.name = "Nord";
        spec.equipment = false;
        spec.scripts = false;
        spec.popups = false;
        const auto d = withRequest(dz::installation(f.pr(), spec), "station Nord sans \xC3\xA9quipement ni script ni popup");
        check(opsOf(d.proposal, A::Equipment) == 0 && opsOf(d.proposal, A::Address) == 0 && opsOf(d.proposal, A::Script) == 0 && !viewOp(d.proposal, "Pop_Pompe")
                  && opsOf(d.proposal, A::Code) == 0,
              "sans \xC3\xA9quipement, ni adresse, ni script, ni popup (ni bouton D\xC3\xA9tails)");
        check(roundTrip(f, d, "la station Nord"), "appliqu\xC3\xA9" "e");
        check(f.pr().variable("Nord_Pompe_1") && f.pr().variable("Nord_Cuve_1") && f.pr().viewByName("Vue_Nord") && f.pr().alarmByName("Nord_Pompe_1_Defaut"),
              "un nom donn\xC3\xA9 pr\xC3\xA9" "fixe les variables (Nord_Pompe_1), la vue (Vue_Nord), les alarmes");
    }
    {
        dz::InstallationSpec spec;
        spec.pumps = 0;
        spec.tanks = 0;
        const auto d = dz::installation(hmi::Project{}, spec);
        check(d.proposal.ops.empty() && has(d.text, "Rien \xC3\xA0 construire"), "aucune pompe ni cuve : rien \xC3\xA0 construire, et le texte le dit");
        const auto e = dz::extendSeries(hmi::Project{}, "pompes", 1);
        check(e.proposal.ops.empty() && has(e.text, "aucune s\xC3\xA9rie"), "\xC3\xA9tendre une s\xC3\xA9rie absente : rien");
    }
}

// ---- les temps ---------------------------------------------------------------------------------------------
void timings(const std::optional<hmi::Project>& demo) {
    std::printf("les temps\n");
    for (int onDemo = 0; onDemo < 2; ++onDemo) {
        if (onDemo && !demo) continue;
        const hmi::Project base = onDemo ? *demo : hmi::Project{};
        dz::InstallationSpec spec;
        spec.pumps = 3;
        const auto t0 = std::chrono::steady_clock::now();
        const auto d = dz::installation(base, spec);
        const auto ck = ai::validate(base, d.proposal);
        const double ms = msSince(t0);
        std::printf("  station \xC3\xA0 3 pompes%s : construite et valid\xC3\xA9" "e en %.1f ms\n", onDemo ? " (Armoire_Gaz)" : "", ms);
        check(ck.ok() && ms < 1000, "la station \xC3\xA0 3 pompes : bien sous la seconde (" + std::to_string(static_cast<int>(ms)) + " ms)");
    }
    // une GRANDE installation
    Fixture f(demo ? *demo : hmi::Project{});
    dz::InstallationSpec spec;
    spec.pumps = 20;
    spec.tanks = 10;
    auto t0 = std::chrono::steady_clock::now();
    const auto d = withRequest(dz::installation(f.pr(), spec), "grande installation");
    const double build = msSince(t0);
    t0 = std::chrono::steady_clock::now();
    const auto ck = ai::validate(f.pr(), d.proposal);
    const double validate = msSince(t0);
    t0 = std::chrono::steady_clock::now();
    std::string why;
    const bool applied = f.apply(d.proposal, &why);
    const double apply = msSince(t0);
    std::size_t vars = 0, cells = 0;
    for (const auto& v : f.pr().programs.variables)
        if (v.equipment == "Station_Modbus") ++vars;
    cells = hmi::audit::ranges(f.pr(), "Station_Modbus").size();
    const auto* vue = f.pr().viewByName("Vue_Station");
    std::printf("  grande installation (20 pompes, 10 cuves%s) : %zu op\xC3\xA9rations, %zu variables (%zu cases Modbus), %zu objets dans la vue ;\n"
                "    construite en %.1f ms, valid\xC3\xA9" "e en %.1f ms, appliqu\xC3\xA9" "e en %.1f ms\n",
                demo ? ", Armoire_Gaz" : "", d.proposal.ops.size(), vars, cells, vue ? vue->objects.size() : 0, build, validate, apply);
    check(ck.ok(), "la grande installation : z\xC3\xA9ro erreur (" + errorsOf(ck) + ")");
    check(applied && vars == 30 && cells >= 200 && hmi::audit::overlaps(f.pr(), "Station_Modbus").empty(),
          "appliqu\xC3\xA9" "e : 30 structures, des centaines de cases, aucun chevauchement (" + why + ")");
    (void)f.stack.undo();
    check(comparable(f.pr()) == comparable(demo ? *demo : hmi::Project{}), "Ctrl+Z la retire enti\xC3\xA8rement");
}

// ---- modifier l'existant -------------------------------------------------------------------------------------
void modifications(const hmi::Project& station, const std::optional<hmi::Project>& demo) {
    std::printf("modifier l'existant\n");
    // -- une troisieme pompe sur le meme modele
    {
        Fixture f(station);
        const auto d = withRequest(dz::extendSeries(f.pr(), "pompes", 1), "Ajoute une troisi\xC3\xA8me pompe sur le m\xC3\xAAme mod\xC3\xA8le");
        check(has(d.text, "2 \xC3\xA9l\xC3\xA9ments") && has(d.text, "Pompe_3") && has(d.text, "mod\xC3\xA8le : **Pompe_2**"), "le texte : la s\xC3\xA9rie (2), le mod\xC3\xA8le, Pompe_3");
        check(roundTrip(f, d, "\xC3\xA9tendre la s\xC3\xA9rie Pompe"), "appliqu\xC3\xA9" "e");
        const auto& pr = f.pr();
        const auto* p3 = pr.variable("Pompe_3");
        check(p3 && p3->type == "T_Pompe" && p3->equipment == "Station_Modbus" && !p3->address.empty() && hmi::audit::overlaps(pr).empty(),
              "Pompe_3 : T_Pompe, adress\xC3\xA9" "e sur Station_Modbus sans chevauchement (" + std::string(p3 ? p3->address : "") + ")");
        check(pr.alarmByName("Pompe_3_Defaut") && pr.alarmByName("Pompe_3_Defaut")->condition == "Pompe_3.Defaut", "son alarme de d\xC3\xA9" "faut");
        const auto* vue = pr.viewByName("Vue_Station");
        const auto* o2 = vue ? objectNamed(*vue, "Pompe_2") : nullptr;
        const auto* o3 = vue ? objectNamed(*vue, "Pompe_3") : nullptr;
        check(o2 && o3 && o3->text("variable") == "Pompe_3.Marche" && o3->number("x") - o2->number("x") == o2->number("x") - objectNamed(*vue, "Pompe_1")->number("x"),
              "dans Vue_Station : la pompe 3, li\xC3\xA9" "e \xC3\xA0 Pompe_3, au pas de la s\xC3\xA9rie");
        check(vue && popupButtonFor(*vue, "Pop_Pompe", "Pompe := Pompe_3") && objectsCiting(*vue, "Pompe_3") >= 5, "et son bouton D\xC3\xA9tails, son intensit\xC3\xA9, sa disponibilit\xC3\xA9");
        check(anyHas(d.limits, "Station_Cycle"), "le script qui lit Pompe_2 n'est pas r\xC3\xA9\xC3\xA9" "crit : le texte le dit");
        const auto* coll = vue ? objectNamed(*vue, "Collecteur") : nullptr;
        check(coll && coll->expr("value") == "Pompe_1.Marche OR Pompe_2.Marche OR Pompe_3.Marche" && !objectNamed(*vue, "Collecteur_2") && !objectNamed(*vue, "Collecteur_Pompe_3"),
              "le collecteur (commun \xC3\xA0 la s\xC3\xA9rie) n'est pas copi\xC3\xA9 : sa formule s'\xC3\xA9tend \xC3\xA0 Pompe_3 (" + std::string(coll ? coll->expr("value") : "") + ")");
        check(vue && objectNamed(*vue, "Txt_Dispo_Pompe_3") && objectNamed(*vue, "Details_Pompe_3"), "les objets nomm\xC3\xA9s d'apr\xC3\xA8s Pompe_2 suivent (Txt_Dispo_Pompe_3, Details_Pompe_3)");
    }
    // -- dupliquer la cuve 1 en cuve 4
    {
        Fixture f(station);
        const auto d = withRequest(dz::duplicateItem(f.pr(), "cuve 1", "cuve 4"), "Duplique la cuve 1 en cuve 4");
        check(has(d.text, "Cuve_4") && has(d.text, "2 (celles du mod\xC3\xA8le"), "le texte : Cuve_4, ses 2 alarmes");
        check(roundTrip(f, d, "dupliquer Cuve_1 en Cuve_4"), "appliqu\xC3\xA9" "e");
        const auto* vue = f.pr().viewByName("Vue_Station");
        check(f.pr().variable("Cuve_4") && f.pr().alarmByName("Cuve_4_Niveau_Haut") && vue && objectNamed(*vue, "Cuve_4")
                  && objectNamed(*vue, "Cuve_4")->text("label") == "Cuve 4",
              "Cuve_4 : la variable, ses alarmes, la cuve dans la vue (son libell\xC3\xA9 Cuve 4)");
        check(!dz::duplicateItem(f.pr(), "Cuve_1", "Cuve_4").proposal.ops.size() && has(dz::duplicateItem(f.pr(), "Cuve_1", "Cuve_4").text, "existe d\xC3\xA9j\xC3\xA0"),
              "Cuve_4 existe : rien n'est remplac\xC3\xA9");
    }
    // -- une alarme sur les quatre cuves
    {
        Fixture f;
        dz::InstallationSpec spec;
        spec.tanks = 4;
        check(f.apply(dz::installation(f.pr(), spec).proposal), "une station \xC3\xA0 4 cuves");
        const auto none = dz::alarmOnSeries(f.pr(), "cuves", "temp\xC3\xA9rature", "haute", "80", "Temperature_Haute");
        check(none.proposal.ops.empty() && has(none.text, "4 \xC3\xA9l\xC3\xA9ments") && anyHas(none.limits, "Temperature"),
              "temp\xC3\xA9rature haute : T_Cuve n'a pas de temp\xC3\xA9rature - aucune alarme, et le texte dit pourquoi");
        const auto d = withRequest(dz::alarmOnSeries(f.pr(), "cuves", "niveau", ">", "95", "Niveau_Tres_Haut"), "Ajoute une alarme de niveau tr\xC3\xA8s haut sur les quatre cuves");
        check(opsOf(d.proposal, A::Alarm) == 4 && has(d.text, "4 alarmes") && has(d.text, "4 \xC3\xA9l\xC3\xA9ments"), "4 alarmes sur 4 cuves, le texte le dit");
        check(roundTrip(f, d, "alarme sur la s\xC3\xA9rie Cuve"), "appliqu\xC3\xA9" "e");
        check(f.pr().alarmByName("Cuve_4_Niveau_Tres_Haut") && f.pr().alarmByName("Cuve_4_Niveau_Tres_Haut")->condition == "Cuve_4.Niveau > 95.0"
                  && f.pr().alarmByName("Cuve_4_Niveau_Tres_Haut")->group == "Station",
              "Cuve_4.Niveau > 95.0, dans le groupe des alarmes de la s\xC3\xA9rie");
        // une mesure a cote (Cuve_1_Temperature) : l'alarme la prend
        dz::Design m = withRequest(dz::addMeasure(f.pr(), "cuve 1", "temp\xC3\xA9rature"), "mesure");
        check(roundTrip(f, m, "une mesure de temp\xC3\xA9rature sur Cuve_1"), "appliqu\xC3\xA9" "e");
        const auto t = dz::alarmOnSeries(f.pr(), "Cuve", "Temperature", ">", "80", "Temperature_Haute");
        check(opsOf(t.proposal, A::Alarm) == 1 && ai::validate(f.pr(), t.proposal).ok(), "Cuve_1_Temperature > 80.0 : 1 alarme, les 3 autres cuves dites sans mesure");
    }
    // -- remplacer le symbole des pompes
    {
        Fixture f(station);
        // l'utilisateur a pose deux instances de Sym_Pompe et prepare un autre symbole
        auto& vue = *f.pr().viewByName("Vue_Station");
        for (int k = 1; k <= 2; ++k) {
            auto o = hmi::makeObject(hmi::Kind::SymbolInstance, f.pr().allocate(), "Inst_Pompe_" + std::to_string(k), 900 + 170 * k, 150, vue.layers.front().id);
            o.set("symbol", "Sym_Pompe");
            o.set("params", "Pompe := Pompe_" + std::to_string(k));
            vue.objects.push_back(o);
        }
        ai::Operation sym;
        sym.action = A::View;
        sym.name = "Sym_Pompe_V2";
        sym.props = {{"role", "symbole"}};
        sym.params = {{"Pompe", "T_Pompe", "", ""}};
        ai::Proposal first;
        first.ops = {sym};
        std::string why;
        check(f.apply(first, &why), "un second symbole Sym_Pompe_V2 (" + why + ")");
        const auto d = withRequest(dz::replaceSymbol(f.pr(), "pompes", "Sym_Pompe_V2"), "Remplace le symbole de toutes les pompes par Sym_Pompe_V2");
        check(opsOf(d.proposal, A::Modify) == 2 && has(d.text, "2 instances") && has(d.text, "1 vue"), "2 instances dans 1 vue : le texte le dit");
        check(roundTrip(f, d, "remplacer le symbole"), "appliqu\xC3\xA9" "e");
        const auto* v = f.pr().viewByName("Vue_Station");
        check(v && objectNamed(*v, "Inst_Pompe_1")->text("symbol") == "Sym_Pompe_V2" && objectNamed(*v, "Inst_Pompe_2")->text("params") == "Pompe := Pompe_2",
              "les instances : Sym_Pompe_V2, leurs param\xC3\xA8tres gard\xC3\xA9s");
        check(dz::replaceSymbol(f.pr(), "pompes", "Pas_De_Symbole").proposal.ops.empty(), "un symbole absent : rien");
    }
    // -- une mesure de pression sur la pompe 2
    {
        Fixture f(station);
        const auto d = withRequest(dz::addMeasure(f.pr(), "pompe 2", "pression"), "Ajoute une mesure de pression sur la pompe 2");
        check(has(d.text, "Pompe_2_Pression") && has(d.text, "PT") && has(d.text, "1 vue"), "le texte : la variable, l'instrument PT, 1 vue");
        check(roundTrip(f, d, "une mesure de pression"), "appliqu\xC3\xA9" "e");
        const auto* m = f.pr().variable("Pompe_2_Pression");
        const auto* v = f.pr().viewByName("Vue_Station");
        check(m && m->type == "REAL" && m->equipment == "Station_Modbus" && m->readOnly && hmi::audit::overlaps(f.pr()).empty(), "Pompe_2_Pression : REAL, adress\xC3\xA9" "e en lecture seule, sans chevauchement");
        check(v && objectNamed(*v, "PT_Pompe_2") && objectNamed(*v, "PT_Pompe_2")->text("variable") == "Pompe_2_Pression", "l'instrument PT_Pompe_2 dans Vue_Station");
    }
    // -- sur le vrai projet : ranger, remplacer le symbole des cartes
    if (demo) {
        Fixture f(*demo);
        const auto d = withRequest(dz::moveToFolder(f.pr(), "vues", "production", "Production"), "D\xC3\xA9place toutes les vues de production dans le dossier Production");
        // Vue_Production par son nom, Vue_Ligne par sa description (<< ... la ligne de production ... >>)
        check(has(d.text, "2 \xC3\xA9l\xC3\xA9ments") && has(d.text, "Vue_Production") && has(d.text, "Vue_Ligne") && has(d.text, "le dossier est cr\xC3\xA9\xC3\xA9"),
              "2 vues concern\xC3\xA9" "es (Vue_Production, Vue_Ligne par sa description), le dossier neuf : le texte le dit");
        check(roundTrip(f, d, "ranger les vues de production"), "appliqu\xC3\xA9" "e");
        check(f.pr().viewByName("Vue_Production") && f.pr().viewByName("Vue_Production")->folder == "Production" && f.pr().viewByName("Vue_Ligne")->folder == "Production",
              "Vue_Production et Vue_Ligne sont dans Production");
        const auto r = withRequest(dz::replaceSymbol(f.pr(), "Carte_Armoire", "Carte_Didacticiel"), "Remplace le symbole des cartes par Carte_Didacticiel");
        std::printf("  Armoire_Gaz, cartes : %zu instances chang\xC3\xA9" "es, %zu limites\n", opsOf(r.proposal, A::Modify), r.limits.size());
        check(opsOf(r.proposal, A::Modify) == 3 && has(r.text, "9 instances de symbole concern\xC3\xA9" "es") && anyHas(r.limits, "Nom n'existe pas dans Carte_Didacticiel"),
              "9 instances concern\xC3\xA9" "es : les 3 compatibles changent, les 6 autres sont dites (leur param\xC3\xA8tre Nom n'existe pas dans le nouveau symbole)");
        check(roundTrip(f, r, "remplacer le symbole des cartes"), "appliqu\xC3\xA9" "e");
        const auto all = withRequest(dz::moveToFolder(f.pr(), "scripts", "", "Tous"), "tous les scripts");
        check(roundTrip(f, all, "ranger tous les scripts"), "appliqu\xC3\xA9" "e");
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<hmi::Project> demo = demoProject(argc > 1 ? argv[1] : "");
    const hmi::Project station = stationEndToEnd();
    reuse(station);
    installations(demo);
    options();
    timings(demo);
    modifications(station, demo);
    std::printf("ai_design_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
