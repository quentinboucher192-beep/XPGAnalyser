// =============================================================================
//  tests/ai_registre_test.cpp - 1.15.0 : la vraie IA, le registre et la preparation des demandes
// -----------------------------------------------------------------------------
//  Sans reseau ni ecran, sur un vrai document IHM et une vraie pile d'annulation,
//  dans un dossier IA/ jetable :
//    - le registre des genres contre les tables du logiciel (116/116, chaque
//      propriete de hmi::makeObject, son role, ses mots de choix) ;
//    - le texte compact (la liste, le detail d'un genre, les genres evoques, la
//      taille de la demande : une vue vide, la station de 31 objets, 300 objets) ;
//    - le contexte (chaque reglage de confidentialite retire sa partie ; jamais
//      la valeur d'une variable ; une grande vue resumee, la selection d'abord) ;
//    - les connaissances (actives oui ; desactivees, d'un autre projet : non) ;
//    - le schema de l'outil (du JSON valable, l'enumeration des 116 genres) ;
//    - proposalFromTool sur des entrees reelles (la station ecrite comme le modele
//      l'ecrirait, une entree illisible, une operation inconnue, un champ inconnu,
//      un genre inconnu, une propriete inventee, une variable inventee, une
//      formule et un choix faux) et le texte rendu au modele ;
//    - les nouvelles operations (dupliquer, code ; deplacer et redimensionner par
//      modifier) appliquees en UN Ctrl+Z, et defaites.
//  Un argument : le fichier ou ecrire l'exemple complet (le champ system et l'outil
//  de la station de pompage).
// =============================================================================
#include "../src/app/ai/AiContext.hpp"
#include "../src/app/ai/AiDelete.hpp"   // 1.21.0 : les genres de supprimer (le schema)
#include "../src/app/ai/AiDemo.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiKnowledge.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiProvider.hpp"
#include "../src/app/ai/AiRegistry.hpp"
#include "../src/app/hmi/HmiPanels.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRoles.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace mem = app::ai::memory;

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

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const char* const kStationRequest =
    "Cr\xC3\xA9" "e-moi un synoptique de station de pompage avec trois cuves, deux pompes par cuve, des vannes, des indicateurs de "
    "niveau et les boutons de commande associ\xC3\xA9s.";

// Un projet IHM : la vue Vue_Station (active), la vue Accueil, des variables ; la pile de l'application.
struct Fixture {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    hmi::Id            view{hmi::kNoId}, home{hmi::kNoId};
    std::vector<hmi::Id> selection;

    Fixture() {
        auto& p = doc->project;
        p.config.name = "Station_Pompage";
        p.views = {hmi::makeView(p, "Vue_Station"), hmi::makeView(p, "Accueil")};
        view = p.views[0].id;
        home = p.views[1].id;
        const auto var = [&](const char* name, const char* type, const char* initial, const char* description) {
            hmi::Variable v;
            v.id = p.allocate();
            v.name = name;
            v.type = type;
            v.initial = initial;
            v.description = description;
            p.programs.variables.push_back(v);
        };
        var("Pompe_1_Marche", "BOOL", "FALSE", "Marche de la pompe 1");
        var("Pompe_1_Defaut", "BOOL", "FALSE", "D\xC3\xA9" "faut de la pompe 1");
        var("Niveau_Cuve_1", "REAL", "4242.5", "Niveau de la cuve 1 en %");
        var("Consigne_Debit", "REAL", "17.25", "");
    }
    [[nodiscard]] hmi::View& v() { return *doc->project.view(view); }
    [[nodiscard]] const hmi::Object* object(const std::string& name) {
        for (const auto& o : v().objects)
            if (o.name == name) return &o;
        return nullptr;
    }
    [[nodiscard]] ai::PromptInput input(std::string request = {}) {
        ai::PromptInput in;
        in.project = &doc->project;
        in.view = doc->project.view(view);
        in.selection = selection;
        in.projectName = "Station_Pompage";
        in.request = std::move(request);
        return in;
    }
    // Applique une proposition comme Appliquer : UNE commande sur la pile.
    bool apply(const ai::Proposal& p, std::string* why = nullptr) {
        auto cmd = ai::makeCommand(doc, p, why);
        if (!cmd) return false;
        return static_cast<bool>(stack.push(std::move(cmd)));
    }
    // La station de la demonstration (31 objets), appliquee.
    void station() {
        ai::DemoContext c;
        c.project = &doc->project;
        c.view = doc->project.view(view);
        const auto r = ai::demoReply(ai::demoSuggestions()[0], c);
        if (r.proposal) (void)apply(*r.proposal);
    }
};

std::optional<ai::Proposal> fromTool(Fixture& f, const std::string& json, std::string* why = nullptr, const std::string& request = "demande") {
    return ai::proposalFromTool(json, f.input(request), request, why);
}

// ------------------------------------------------------------- 1. le registre ----
void registryAgainstTables() {
    std::printf("== 1. le registre contre les tables du logiciel ==\n");
    const auto t0 = std::chrono::steady_clock::now();
    const ai::KindRegistry& reg = ai::registry();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  le registre se construit en %lld ms (une fois)\n", static_cast<long long>(ms));
    check(reg.kinds().size() == 116 && std::size(hmi::kKindInfos) == 116, "116 genres (" + std::to_string(reg.kinds().size()) + ")");
    std::size_t sameKey = 0, sameProps = 0, sameRoles = 0, described = 0, choicesOk = 0, withChoices = 0, placeable = 0, bad = 0;
    for (std::size_t i = 0; i < reg.kinds().size(); ++i) {
        const auto& e = reg.kinds()[i];
        const auto& info = hmi::kKindInfos[i];
        if (e.kind == info.kind && e.key == info.key && e.label == info.label && hmi::kindFromKey(e.key) == e.kind) ++sameKey;
        const hmi::Object o = hmi::makeObject(e.kind, 1, "x", 0, 0, hmi::kNoId);
        bool props = o.props.size() == e.props.size();
        bool rolesOk = props;
        for (std::size_t k = 0; props && k < o.props.size(); ++k) {
            props = o.props[k].key == e.props[k].key && o.props[k].value == e.props[k].value;
            const auto r = hmi::roles::roleOf(e.kind, e.props[k].key);
            rolesOk = rolesOk && r.role == e.props[k].role && r.item == e.props[k].item
                      && e.props[k].variable == hmi::roles::readsAsFormula(e.kind, e.props[k].key);
            if (!e.props[k].choices.empty()) {
                ++withChoices;
                const auto& c = e.props[k].choices;
                if (e.props[k].value.empty() || std::find(c.begin(), c.end(), e.props[k].value) != c.end()) ++choicesOk;
            }
        }
        if (props) ++sameProps;
        else if (++bad < 4) std::printf("  proprietes differentes : %s\n", e.key.c_str());
        if (rolesOk) ++sameRoles;
        if (!e.description.empty()) ++described;
        if (e.placeable) ++placeable;
        check(e.family == app::hmiPaletteFamily(e.kind), "la famille de " + e.key);
    }
    check(sameKey == 116, "chaque cle, chaque libelle : ceux de hmi::kKindInfos, relus par kindFromKey (" + std::to_string(sameKey) + ")");
    check(sameProps == 116, "chaque genre : exactement les proprietes de hmi::makeObject, dans l'ordre, avec leur valeur (" + std::to_string(sameProps) + ")");
    check(sameRoles == 116, "chaque propriete : son role et son type de hmi::roles::roleOf (" + std::to_string(sameRoles) + ")");
    check(withChoices > 20 && choicesOk == withChoices, "les mots de choix de l'inspecteur : " + std::to_string(withChoices) + " listes, chacune contient sa valeur par defaut");
    check(placeable == std::size(hmi::kPlaceableKinds), "les genres de la bibliotheque : ceux de kPlaceableKinds (" + std::to_string(placeable) + ")");
    check(described >= 100, "une description (le sujet d'aide) pour " + std::to_string(described) + " genres");
    std::printf("  %zu genres decrits par l'aide, %zu listes de choix\n", described, withChoices);
    // Les communes, et ce que porte une vanne, une cuve.
    std::set<std::string> common;
    for (const auto& p : reg.common()) common.insert(p.key);
    check(common.count("x") && common.count("y") && common.count("w") && common.count("h") && common.count("variable") && common.count("visible"),
          "les proprietes communes : x, y, w, h, visible, variable...");
    const auto* valve = reg.find("Valve");
    const auto* lp = valve ? valve->prop("labelPosition") : nullptr;
    check(lp && lp->choices == std::vector<std::string>{"dessous", "dessus", "aucun"}, "Valve.labelPosition : dessous, dessus, aucun (l'inspecteur)");
    const auto* tank = reg.find(hmi::Kind::Tank);
    check(tank && tank->level && tank->shows && tank->synoptic && tank->prop("fillColor") && tank->prop("fillColor")->role == hmi::roles::Role::Color,
          "la cuve : un niveau, un symbole, fillColor est une couleur");
    check(tank && tank->prop("fault") && tank->prop("fault")->variable, "la cuve : son defaut porte une variable (une formule)");
    const auto* push = reg.find("PushButton");
    check(push && push->writes && push->prop("variable") && push->prop("variable")->variable, "le bouton a impulsion ecrit sa variable");
    check(!reg.find("Licorne") && reg.find(hmi::Kind::FileExplorer) && reg.find(hmi::Kind::FileExplorer)->key == "FileExplorer", "find : par cle, par genre");
}

// ---------------------------------------------------------- 2. le texte compact ----
void compactText() {
    std::printf("== 2. le texte compact ==\n");
    const ai::KindRegistry& reg = ai::registry();
    const std::string list = reg.kindsText();
    std::size_t lines = 0, missing = 0;
    for (const char c : list) lines += c == '\n';
    for (const auto& info : hmi::kKindInfos)
        if (!has(list, std::string(info.key) + " | " + std::string(info.label) + " | ")) ++missing;
    check(lines == 117 && missing == 0, "la liste : une ligne par genre (cle | libelle | famille | valeur), " + std::to_string(list.size()) + " octets");
    const std::string tank = reg.detailLine(*reg.find("Tank"));
    check(has(tank, "Tank (Cuve) 120x160 - Une cuve de stockage") && has(tank, "fillColor(c)=") && has(tank, "fault(f:b)") && has(tank, "unit(t)") && !has(tank, " x(n)")
              && !has(tank, " visible("),
          "le detail de la cuve : sa taille, ses proprietes propres en abrege, sans les communes (" + std::to_string(tank.size()) + " octets)");
    const std::string valve = reg.detailLine(*reg.find("Valve"));
    check(has(valve, "labelPosition(dessous|dessus|aucun)=dessous"), "un choix : ses mots");
    const std::string common = reg.commonText();
    check(has(common, "x(n)") && has(common, "variable(f") && has(common, "TRUE ou FALSE"), "la legende et les communes (" + std::to_string(common.size()) + " octets)");
    std::size_t biggest = 0, total = 0;
    for (const auto& e : reg.kinds()) {
        const auto n = reg.detailLine(e).size();
        biggest = std::max(biggest, n);
        total += n;
    }
    std::printf("  detail : %zu octets pour les 116 genres, %zu au plus pour un genre\n", total, biggest);
    // Les genres evoques par la demande de la station.
    const auto ev = reg.evoked(kStationRequest);
    std::string names;
    for (const auto k : ev) names += std::string(hmi::kindKey(k)) + " ";
    std::printf("  la station evoque : %s\n", names.c_str());
    const auto in = [&](hmi::Kind k) { return std::find(ev.begin(), ev.end(), k) != ev.end(); };
    check(in(hmi::Kind::Tank) && in(hmi::Kind::Pump) && in(hmi::Kind::Valve), "la station evoque la cuve, la pompe, la vanne");
    check(in(hmi::Kind::Button) || in(hmi::Kind::PushButton), "et un bouton");
    const auto ev2 = reg.evoked("Ajoute un variateur ATV et deux ventilateurs");
    check(std::find(ev2.begin(), ev2.end(), hmi::Kind::SpeedDrive) != ev2.end() && std::find(ev2.begin(), ev2.end(), hmi::Kind::Fan) != ev2.end(),
          "les mots de la bibliotheque (variateur, ATV) et les pluriels (ventilateurs)");
    const auto ev3 = reg.evoked("Ajoute un variateur ATV", /*useWords=*/false);
    check(std::find(ev3.begin(), ev3.end(), hmi::Kind::SpeedDrive) == ev3.end(), "sans le vocabulaire : le variateur n'est plus trouve par ses mots");
    check(reg.evoked("bonjour, comment vas-tu ?").empty(), "une demande sans objet n'evoque rien");
    const auto near = reg.nearKinds("Pompe");
    check(!near.empty() && near.front() == "Pump (Pompe)", "le genre le plus proche de \xC2\xAB Pompe \xC2\xBB : Pump (Pompe)");
    check(reg.nearKinds("Xyzzyqwv").empty(), "rien de proche d'un mot sans rapport");
}

// ------------------------------------------------------------- 3. les tailles ----
void sizes(const std::string& examplePath) {
    std::printf("== 3. la taille de la demande ==\n");
    Fixture f;
    const std::string tool = ai::operationsTool();
    const std::string empty = ai::systemPrompt(f.input(kStationRequest));
    f.station();
    check(f.v().objects.size() == 31, "la station : 31 objets dans la vue");
    const std::string follow = "Associe la pompe P11 \xC3\xA0 Pompe_1_Marche et mets la vanne de droite en rouge.";
    const std::string station = ai::systemPrompt(f.input(follow));
    // 300 objets : la station, et des voyants en grille.
    Fixture big;
    big.station();
    for (int i = 0; big.v().objects.size() < 300; ++i) {
        hmi::Object o = hmi::makeObject(hmi::Kind::Indicator, big.doc->project.allocate(), "Voyant_" + std::to_string(i + 1),
                                        1000 + (i % 25) * 36, 40 + (i / 25) * 40, big.v().activeLayer);
        o.set("colorOn", "#E5534B");
        big.v().objects.push_back(o);
    }
    big.selection = {big.object("Pompe_31")->id};
    ai::ContextInfo info;
    const std::string bigCtx = ai::contextText(big.input("Mets cette pompe en rouge"), {}, &info);
    const std::string bigPrompt = ai::systemPrompt(big.input("Mets cette pompe en rouge"));
    std::printf("  vue vide + la station demandee : %zu octets (system) + %zu (outil)\n", empty.size(), tool.size());
    std::printf("  station de 31 objets + une suite : %zu octets (system)\n", station.size());
    std::printf("  vue de %zu objets : %zu octets (system), contexte %zu octets : %zu detailles, %zu nommes\n", big.v().objects.size(), bigPrompt.size(),
                bigCtx.size(), info.detailed, info.named);
    // 1.16.0 (chantier OPTIONS) : + creer_vue, creer_variables, creer_type, les copies et les reperes, les formules - leurs
    // consignes, un exemple de chaque et leur schema (environ 3,5 Ko) : la borne passe de 30 a 34 Ko.
    // 1.18.0 (les outils de l'IA) : + creer_equipement, adresser, creer_script, creer_fonction, creer_alarme, creer_ressource,
    // creer_dossier, ranger, les roles et parametres d'une vue, les generiques - leurs consignes (environ 2,5 Ko) et leur
    // schema (environ 2,5 Ko) : la borne passe de 34 a 40 Ko. Le contexte des outils ne montre que ce qui existe (ou que la
    // demande evoque).
    // 1.19.0 (le plan d'operations) : + plusieurs vues dans une proposition (le champ "vue"), la reutilisation, une installation
    // complete en une proposition - environ 0,9 Ko : la borne passe de 40 a 41 Ko.
    // 1.21.0 (l'IA qui agit) : + la suppression intelligente (strategie, exclusions, le schema de supprimer), le bac a sable
    // (regle 16) et le travail en plusieurs etapes (regle 17) - environ 2,5 Ko : la borne passe de 41 a 45 Ko. La partie
    // stable des consignes est mise en cache par le fournisseur : elle n'est payee en entier qu'une fois.
    check(empty.size() + tool.size() < 45000, "une demande ordinaire (vue vide) : moins de 45 Ko avec l'outil");
    check(station.size() + tool.size() < 45000, "la station de 31 objets : moins de 45 Ko avec l'outil");
    check(info.summarized && info.detailed < 300 && info.detailed >= 31 && bigPrompt.size() < 45000, "300 objets : resumes, la demande reste bornee");
    // La selection d'abord, puis les plus proches : Pompe_31 et ses voisins detailles, les voyants du fond nommes seulement.
    check(has(bigCtx, "\nPompe_31 | Pump |") && has(bigCtx, "\nMarche_P31 | PushButton |") && !has(bigCtx, "\nTitre_Station | Text |")
              && has(bigCtx, "Titre_Station"),
          "la selection et ses voisins detailles ; les objets loin d'elle nommes seulement");
    check(has(bigCtx, "la vue est grande") && has(bigCtx, "Objets nomm\xC3\xA9s seulement"), "le texte dit que la vue est resumee");
    if (!examplePath.empty()) {
        std::ofstream out(fs::path(examplePath), std::ios::binary);
        Fixture e;
        const auto parts0 = ai::promptParts(e.input(kStationRequest));
        e.station();
        e.selection = {e.object("Vanne_3")->id, e.object("Vanne_2")->id};
        const auto parts1 = ai::promptParts(e.input(follow));
        ai::json::Value toolValue;
        (void)ai::json::parse(tool, toolValue);
        out << "EXEMPLE COMPLET - le champ system et l'outil, tels que le registre les prepare (1.15.0)\n"
            << "=====================================================================================\n\n"
            << "A. Vue vide, la demande : " << kStationRequest << "\n"
            << "   system : " << parts0.stable.size() + parts0.dynamic.size() << " octets (stable " << parts0.stable.size() << ", dynamique "
            << parts0.dynamic.size() << ") ; outil : " << tool.size() << " octets\n"
            << "B. La station appliquee (31 objets), la selection Vanne_2 et Vanne_3, la demande : " << follow << "\n"
            << "   system : " << parts1.stable.size() + parts1.dynamic.size() << " octets (stable " << parts1.stable.size() << ", dynamique "
            << parts1.dynamic.size() << ")\n"
            << "C. Une vue de 300 objets : system " << bigPrompt.size() << " octets (contexte " << bigCtx.size() << ")\n\n"
            << "------------------------------------------------------------- B. le champ system (partie stable) ---\n"
            << parts1.stable
            << "\n------------------------------------------------------------- B. le champ system (partie dynamique) ---\n"
            << parts1.dynamic
            << "\n------------------------------------------------------------- A. la partie dynamique (vue vide) ---\n"
            << parts0.dynamic
            << "\n------------------------------------------------------------- l'outil (le tableau \"tools\") ---\n"
            << ai::json::write(toolValue, true) << "\n";
        std::printf("  exemple ecrit : %s\n", examplePath.c_str());
    }
}

// ------------------------------------------------------------- 4. le contexte ----
void context() {
    std::printf("== 4. le contexte et la confidentialite ==\n");
    Fixture f;
    f.station();
    f.selection = {f.object("Pompe_12")->id, f.object("Pompe_11")->id};
    std::string all = ai::systemPrompt(f.input("Mets celle de droite en rouge"));
    check(has(all, "Vue active : Vue_Station (1920 x 1080 px"), "la vue active : son nom, sa taille");
    check(has(all, "Cuve_1 | Tank | ") && has(all, "label=Cuve 1"), "ses objets : nom, genre, x,y,w,h, les proprietes changees");
    {
        const auto at = all.find("\nCuve_1 | Tank | ");
        const std::string line = at == std::string::npos ? std::string() : all.substr(at + 1, all.find('\n', at + 1) - at - 1);
        check(!line.empty() && !has(line, "rot=") && !has(line, "opacity=") && !has(line, "visible=") && !has(line, "refresh="),
              "pas les proprietes par defaut : " + line);
    }
    check(has(all, "S\xC3\xA9lection (2, de gauche \xC3\xA0 droite) : Pompe_11 (Pump), Pompe_12 (Pump)"), "la selection, de gauche a droite");
    check(has(all, "Pompe_1_Marche : BOOL - Marche de la pompe 1") && has(all, "Niveau_Cuve_1 : REAL"), "les variables : nom, type, description");
    check(!has(all, "4242.5") && !has(all, "17.25"), "jamais la valeur d'une variable (ni l'initiale)");
    check(has(all, "Autres vues du projet") && has(all, "Accueil"), "les autres vues : leurs noms");
    check(has(all, "Projet : Station_Pompage"), "le projet");
    // La vue decochee : aucun objet ne part, le texte le dit.
    auto in = f.input("Mets celle de droite en rouge");
    in.settings.sendView = false;
    std::string noView = ai::systemPrompt(in);
    check(!has(noView, "Cuve_1 |") && !has(noView, "Pompe_31 | Pump") && has(noView, "Les objets de la vue ne sont PAS envoy"), "sendView decoche : pas d'objet, et c'est dit");
    check(has(noView, "Vue active : Vue_Station"), "la vue garde son nom et sa taille (pour placer les objets)");
    // La selection decochee.
    in = f.input("Mets celle de droite en rouge");
    in.settings.sendSelection = false;
    std::string noSel = ai::systemPrompt(in);
    check(!has(noSel, "S\xC3\xA9lection (") && has(noSel, "La s\xC3\xA9lection n'est PAS envoy"), "sendSelection decoche : pas de selection, et c'est dit");
    // Les variables decochees : aucun nom de variable dans tout le texte.
    in = f.input("Associe la pompe a Pompe_1_Defaut");
    in.request.clear();
    in.settings.sendVariables = false;
    std::string noVar = ai::systemPrompt(in);
    check(!has(noVar, "Pompe_1_Marche") && !has(noVar, "Pompe_1_Defaut") && !has(noVar, "Consigne_Debit") && has(noVar, "Les variables ne sont PAS envoy"),
          "sendVariables decoche : aucun nom de variable, et c'est dit");
    // L'automate relie : ses variables (Env::plcVariables).
    in = f.input("demande");
    in.env.plcName = [](std::string_view n) { return n == "API.Pression_Ligne"; };
    in.env.plcVariables = [] { return std::vector<std::pair<std::string, std::string>>{{"API.Pression_Ligne", "REAL"}}; };
    check(has(ai::contextText(in), "Variables de l'automate reli\xC3\xA9 (1)") && has(ai::contextText(in), "API.Pression_Ligne : REAL"),
          "les variables de l'automate relie");
    // Pas de vue : dit.
    in = f.input("demande");
    in.view = nullptr;
    check(has(ai::contextText(in), "Aucune vue n'est ouverte"), "sans vue : le modele le sait");
    // changedProps : une formule se montre fx(...).
    hmi::Object o = hmi::makeObject(hmi::Kind::Indicator, 9, "V", 0, 0, hmi::kNoId);
    o.setExpr("value", "Pompe_1_Marche AND NOT Pompe_1_Defaut");
    o.set("colorOn", "#E5534B");
    const std::string ch = ai::changedProps(o);
    check(has(ch, "value=fx(Pompe_1_Marche AND NOT Pompe_1_Defaut)") && has(ch, "colorOn=#E5534B") && !has(ch, "shape"), "changedProps : " + ch);
}

// ---------------------------------------------------------- 5. les connaissances ----
void knowledge() {
    std::printf("== 5. les connaissances actives ==\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_registre";
    fs::remove_all(dir);
    fs::create_directories(dir);
    ai::setRoot(U(dir / "IA"));
    mem::Registry reg;
    reg.load();
    check(reg.kindsKnown() == 116, "le centre de memoire : 116/116 genres connus (le registre)");
    const auto* kinds = reg.find(mem::kKindsKnowledge);
    check(kinds && kinds->content == ai::registry().kindsText(), "la connaissance des genres : le texte meme du registre");
    const auto* props = reg.find("logiciel.proprietes");
    check(props && props->content == ai::registry().propertiesText() && has(props->content, "Pump :"), "celle des proprietes aussi");
    const auto add = [&](const char* name, const char* content, const char* project, bool active) {
        mem::Knowledge k;
        k.name = name;
        k.content = content;
        k.project = project;
        k.category = "regles";
        check(reg.add(k), std::string("ajouter ") + name);
        if (!active) check(reg.setActive(k.id, false), std::string("desactiver ") + name);
    };
    add("Regle generale", "REGLE_TOUS : les pompes de secours sont a droite.", "", true);
    add("Regle du projet", "REGLE_ICI : les vannes sont jaunes.", "Station_Pompage", true);
    add("Regle d'ailleurs", "REGLE_AILLEURS : tout en bleu.", "Traitement_Eau", true);
    add("Regle inactive", "REGLE_INACTIVE : rien.", "", false);
    Fixture f;
    auto in = f.input("Ajoute une cuve");
    in.knowledge = &reg;
    std::string s = ai::systemPrompt(in);
    check(has(s, "REGLE_TOUS") && has(s, "REGLE_ICI"), "les connaissances actives ici partent (tous les projets, ce projet)");
    check(!has(s, "REGLE_AILLEURS"), "celle d'un autre projet ne part pas");
    check(!has(s, "REGLE_INACTIVE"), "une connaissance desactivee ne part pas");
    // Relues du disque (in.knowledge nul) : pareil.
    in.knowledge = nullptr;
    s = ai::systemPrompt(in);
    check(has(s, "REGLE_TOUS") && has(s, "REGLE_ICI") && !has(s, "REGLE_AILLEURS") && !has(s, "REGLE_INACTIVE"), "relues du disque : les memes");
    // Les integrees : desactivees, elles ne partent pas.
    check(has(s, "Tank | Cuve | Synoptique\n") && has(s, "Propri\xC3\xA9t\xC3\xA9s communes"), "les integrees actives : la liste des genres, les proprietes");
    check(reg.setActive(mem::kKindsKnowledge, false) && reg.setActive("logiciel.proprietes", false) && reg.setActive("logiciel.modeles", false),
          "desactiver les genres, les proprietes, les modeles");
    in.knowledge = &reg;
    s = ai::systemPrompt(in);
    check(!has(s, "Tank | Cuve | Synoptique\n") && has(s, "La liste des genres n'est pas jointe"), "les genres desactives : la liste ne part pas, c'est dit");
    check(!has(s, "D\xC3\xA9tail des genres utiles") && has(s, "Le d\xC3\xA9tail des propri\xC3\xA9t\xC3\xA9s n'est pas joint"), "les proprietes desactivees : pas de detail");
    const auto* modeles = reg.find("logiciel.modeles");
    check(modeles && !has(s, modeles->content.substr(0, 60)), "les modeles desactives : absents");
    check(reg.kindsKnown() == 0, "et le centre compte 0/116");
    (void)reg.setActive(mem::kKindsKnowledge, true);
    (void)reg.setActive("logiciel.proprietes", true);
    (void)reg.setActive("logiciel.modeles", true);
    // Le vocabulaire actif : il part, et trouve les genres par les mots de la bibliotheque.
    const auto* voc = reg.find("logiciel.vocabulaire");
    std::printf("  tailles des integrees : genres %zu, proprietes %zu, vocabulaire %zu, actions %zu, variables %zu, modeles %zu octets\n",
                reg.find(mem::kKindsKnowledge)->size(), reg.find("logiciel.proprietes")->size(), voc ? voc->size() : 0,
                reg.find("logiciel.actions")->size(), reg.find("logiciel.variables")->size(), reg.find("logiciel.modeles")->size());
    ai::setRoot({});
    fs::remove_all(dir);
}

// ------------------------------------------------------------- 6. l'outil ----
void toolSchema() {
    std::printf("== 6. le schema de l'outil ==\n");
    const std::string text = ai::operationsTool();
    ai::json::Value v;
    std::string err;
    check(ai::json::parse(text, v, &err) && v.isArray() && v.items().size() == 1, "du JSON valable : un tableau d'un outil (" + err + ")");
    if (!v.isArray() || v.items().empty()) return;
    const auto& tool = v.items().front();
    check(tool.str("name") == ai::kOperationsTool && !tool.str("description").empty(), "son nom : proposer_operations ; une description");
    check(has(tool.str("description"), "TOUTE demande qui modifie la vue"), "la description dit quand l'appeler");
    const auto& schema = tool.at("input_schema");
    check(schema.str("type") == "object" && schema.at("required").items().size() == 1 && schema.at("required").items()[0].asString() == "operations",
          "input_schema : un objet, operations obligatoire");
    const auto& item = schema.at("properties").at("operations").at("items");
    const auto& kinds = item.at("properties").at("genre").at("enum");
    // 1.21.0 : puis les 14 genres de supprimer (la suppression intelligente : vue, variable, type...).
    constexpr std::size_t kRemoval = static_cast<std::size_t>(ai::del::Genre::Folder) + 1;
    bool all = kinds.items().size() == 116 + kRemoval;
    for (std::size_t i = 0; all && i < 116; ++i) all = kinds.items()[i].asString() == hmi::kKindInfos[i].key;
    for (std::size_t k = 0; all && k < kRemoval; ++k) all = kinds.items()[116 + k].asString() == ai::del::genreKey(static_cast<ai::del::Genre>(k));
    check(all, "genre : l'enumeration des 116 cles de kKindInfos, puis les 14 genres de supprimer");
    const auto& actions = item.at("properties").at("action").at("enum");
    std::set<std::string> a;
    for (const auto& x : actions.items()) a.insert(x.asString());
    // 1.16.0 : + creer_vue, creer_variables, creer_type (le chantier OPTIONS).
    // 1.18.0 : + les huit outils (creer_symbole, creer_popup, creer_modele_vue se lisent comme creer_vue + role).
    check(a == std::set<std::string>{"creer", "modifier", "associer", "aligner", "distribuer", "supprimer", "dupliquer", "code", "creer_vue",
                                     "creer_variables", "creer_type", "creer_equipement", "adresser", "creer_script", "creer_fonction",
                                     "creer_alarme", "creer_ressource", "creer_dossier", "ranger"},
          "action : les dix-neuf operations");
    check(item.at("properties").has("proprietes") && item.at("properties").has("variable") && item.at("properties").has("noms")
              && item.at("properties").has("code") && schema.at("properties").has("resume"),
          "les champs du format existant (+ noms, dx, dy, code)");
    std::printf("  l'outil : %zu octets\n", text.size());
}

// --------------------------------------------------------- 7. proposalFromTool ----
void fromToolInputs() {
    std::printf("== 7. proposalFromTool sur des entrees reelles ==\n");
    Fixture f;
    // La station, ecrite comme le modele l'ecrirait : des nombres en nombres, un resume.
    std::string json = "{\"resume\": \"Station de pompage\", \"operations\": [";
    json += "{\"action\": \"creer\", \"genre\": \"Text\", \"nom\": \"Titre_Station\", \"proprietes\": {\"x\": 40, \"y\": 24, \"w\": 520, \"h\": 40, "
            "\"text\": \"Station de pompage\", \"fontSize\": 26, \"align\": \"gauche\"}}";
    for (int c = 1; c <= 3; ++c) {
        const std::string n = std::to_string(c), x = std::to_string(80 + (c - 1) * 600);
        json += ",{\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_" + n + "\", \"proprietes\": {\"x\": " + x
                + ", \"y\": 94, \"w\": 150, \"h\": 200, \"label\": \"Cuve " + n + "\", \"max\": 100, \"unit\": \"%\"}}";
        json += ",{\"action\": \"creer\", \"genre\": \"Bargraph\", \"nom\": \"Niveau_Cuve_" + n + "_Bg\", \"proprietes\": {\"x\": " + std::to_string(254 + (c - 1) * 600)
                + ", \"y\": 94, \"w\": 56, \"h\": 200, \"unit\": \"%\"}}";
        json += ",{\"action\": \"creer\", \"genre\": \"Valve\", \"nom\": \"Vanne_" + n + "\", \"proprietes\": {\"x\": " + std::to_string(115 + (c - 1) * 600)
                + ", \"y\": 324, \"label\": \"V" + n + "\"}}";
        json += ",{\"action\": \"creer\", \"genre\": \"Pipe\", \"nom\": \"Collecteur_" + n + "\", \"proprietes\": {\"x\": " + std::to_string(60 + (c - 1) * 600)
                + ", \"y\": 416, \"w\": 480, \"h\": 20}}";
        for (int k = 1; k <= 2; ++k) {
            const std::string pn = n + std::to_string(k), px = std::to_string(60 + (c - 1) * 600 + (k - 1) * 240);
            json += ",{\"action\": \"creer\", \"genre\": \"Pump\", \"nom\": \"Pompe_" + pn + "\", \"proprietes\": {\"x\": " + px + ", \"y\": 454, \"label\": \"P" + pn + "\"}}";
            json += ",{\"action\": \"creer\", \"genre\": \"PushButton\", \"nom\": \"Marche_P" + pn + "\", \"proprietes\": {\"x\": " + px
                    + ", \"y\": 580, \"w\": 96, \"h\": 36, \"text\": \"Marche\", \"fill\": \"#2F7D4F\"}}";
            json += ",{\"action\": \"creer\", \"genre\": \"PushButton\", \"nom\": \"Arret_P" + pn + "\", \"proprietes\": {\"x\": " + px
                    + ", \"y\": 624, \"w\": 96, \"h\": 36, \"text\": \"Arr\xC3\xAAt\", \"fill\": \"#7D3A36\"}}";
        }
    }
    json += ",{\"action\": \"associer\", \"objet\": \"Pompe_11\", \"variable\": \"Pompe_1_Marche\"}";
    json += ",{\"action\": \"aligner\", \"objets\": [\"Cuve_1\", \"Cuve_2\", \"Cuve_3\"], \"bord\": \"haut\"}";
    json += "]}";
    std::string why;
    auto p = fromTool(f, json, &why, kStationRequest);
    check(p.has_value(), "la station du modele : lue (" + why + ")");
    if (p) {
        check(p->summary == "Station de pompage" && p->view == "Vue_Station" && p->viewId == f.view && !p->fingerprint.empty() && p->request == kStationRequest,
              "la proposition : son resume, sa vue, son empreinte, la demande");
        const auto ck = ai::validate(f.doc->project, *p);
        check(ck.ok() && ck.create == 31, "valide : 31 objets a creer (" + (ck.errors.empty() ? std::string() : ck.errors.front()) + ")");
        check(ck.bindings.size() == 1 && has(ck.bindings.front(), "Pompe_1_Marche"), "Pompe_11 -> Pompe_1_Marche");
        check(ai::feedback(ck, *p).empty(), "rien a dire au modele : elle est valable");
        const std::size_t steps = f.stack.done().size();
        check(f.apply(*p) && f.v().objects.size() == 31 && f.stack.done().size() == steps + 1, "appliquee : 31 objets, UN pas d'annulation");
        check(f.object("Pompe_11") && f.object("Pompe_11")->text("variable") == "Pompe_1_Marche", "la pompe P11 lit sa variable");
        check(f.object("Titre_Station") && f.object("Titre_Station")->text("fontSize") == "26", "un nombre JSON devient le texte de la propriete");
        (void)f.stack.undo();
        check(f.v().objects.empty(), "un Ctrl+Z defait tout");
    }
    // La station de la demonstration, au format des operations (le contrat), relue telle quelle.
    {
        ai::DemoContext c;
        c.project = &f.doc->project;
        c.view = f.doc->project.view(f.view);
        const auto r = ai::demoReply(ai::demoSuggestions()[0], c);
        ai::json::Value in = ai::json::Value::object();
        in.set("resume", "Synoptique de station de pompage");
        in.set("operations", ai::operationsToJson(r.proposal->ops));
        auto p2 = fromTool(f, ai::json::write(in, false), &why);
        check(p2 && ai::validate(f.doc->project, *p2).create == 31, "la station de la demonstration, au format de l'outil : 31 objets");
    }
    // Illisible, operation inconnue, champ inconnu, types faux.
    check(!fromTool(f, "{\"operations\": [", &why) && has(why, "illisible"), "une entree illisible : refusee (" + why + ")");
    check(!fromTool(f, "[1, 2]", &why) && has(why, "objet"), "pas un objet : refusee");
    check(!fromTool(f, "{\"resume\": \"x\"}", &why) && has(why, "operations"), "sans operations : refusee");
    check(!fromTool(f, "{\"operations\": []}", &why) && has(why, "vide"), "aucune operation : refusee");
    check(!fromTool(f, "{\"operations\": [{\"action\": \"executer\", \"code\": \"system('rm -rf /')\"}]}", &why) && has(why, "executer")
              && has(why, "aucun code"),
          "une operation inconnue (du code) : refusee (" + why + ")");
    check(!fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"x\": 10}]}", &why) && has(why, "\xC2\xAB x \xC2\xBB")
              && has(why, "proprietes"),
          "un champ inconnu : refuse, la raison pour le modele (" + why + ")");
    check(!fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"Cuve_1\"}]}", &why) && has(why, "proprietes"), "modifier sans proprietes : refuse");
    check(!fromTool(f, "{\"operations\": [{\"action\": \"aligner\", \"objets\": [\"A\", \"B\"], \"bord\": \"milieu\"}]}", &why) && has(why, "bord"),
          "un bord inconnu : refuse");
    check(!fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"proprietes\": {\"x\": [1]}}]}", &why) && has(why, "texte"),
          "une valeur qui n'est ni texte ni nombre : refusee");
    check(!fromTool(f, "{\"operations\": [{\"action\": \"supprimer\", \"objets\": \"Cuve_1\"}]}", &why) && has(why, "liste"), "objets doit etre une liste");
    {
        auto in = f.input();
        in.view = nullptr;
        check(!ai::proposalFromTool("{\"operations\": [{\"action\": \"supprimer\", \"objets\": [\"A\"]}]}", in, "x", &why) && has(why, "Aucune vue"),
              "sans vue ouverte : refusee");
    }
    // Genre inconnu : lue, mais refusee a la validation ; le modele recoit les genres proches et leur detail.
    auto g = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Pompe\", \"nom\": \"P1\"}]}", &why);
    check(g.has_value(), "un genre inconnu : la proposition se lit");
    if (g) {
        const auto ck = ai::validate(f.doc->project, *g);
        check(!ck.ok() && has(ck.errors.front(), "Genre d'objet inconnu") && has(ck.errors.front(), "Pump (Pompe)"), "refusee, avec le genre proche : " + ck.errors.front());
        const std::string fb = ai::feedback(ck, *g);
        check(has(fb, "Proposition refus\xC3\xA9" "e") && has(fb, "Pump (Pompe) ") && has(fb, "repropose"), "le texte rendu au modele : l'erreur et le detail de Pump");
    }
    // Un genre hors de la bibliotheque (le groupe : il nait d'une selection) : refuse.
    auto gr = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Group\", \"nom\": \"G1\"}]}", &why);
    check(gr && !ai::validate(f.doc->project, *gr).ok() && has(ai::validate(f.doc->project, *gr).errors.front(), "s\xC3\xA9lection"),
          "creer un groupe : refuse (un groupe nait d'une selection)");
    // Propriete inventee.
    auto pi = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_9\", \"proprietes\": {\"couleurMagique\": \"#FF0000\"}}]}", &why);
    if (pi) {
        const auto ck = ai::validate(f.doc->project, *pi);
        check(!ck.ok() && has(ck.errors.front(), "couleurMagique"), "une propriete inventee : refusee");
        check(has(ai::feedback(ck, *pi), "Tank (Cuve) 120x160"), "le modele recoit les vraies proprietes de la cuve");
    }
    // Variable inventee : par associer, par la propriete variable d'un creer.
    f.station();
    auto vi = fromTool(f, "{\"operations\": [{\"action\": \"associer\", \"objet\": \"Pompe_12\", \"variable\": \"PUMP_01_RUN\"}]}", &why);
    if (vi) {
        const auto ck = ai::validate(f.doc->project, *vi);
        check(!ck.ok() && ck.missingVariable == "PUMP_01_RUN", "une variable inventee (associer) : refusee, notee");
        check(has(ai::feedback(ck, *vi), "PUMP_01_RUN") && has(ai::feedback(ck, *vi), "Ne l'invente pas"), "le modele le sait");
    }
    auto vc = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Indicator\", \"nom\": \"Voyant_P11\", \"proprietes\": {\"x\": 60, \"y\": 700, "
                          "\"variable\": \"POMPE_FANTOME\"}}]}", &why);
    if (vc) {
        auto ck = ai::validate(f.doc->project, *vc);
        check(!ck.ok() && ck.missingVariable == "POMPE_FANTOME", "une variable inventee dans les proprietes d'un creer : refusee aussi");
        // L'utilisateur choisit une variable existante : elle s'applique.
        vc->variable.use = "Pompe_1_Marche";
        ck = ai::validate(f.doc->project, *vc);
        check(ck.ok() && f.apply(*vc) && f.object("Voyant_P11")->text("variable") == "Pompe_1_Marche", "la variable choisie par l'utilisateur la remplace");
    }
    auto ok = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Indicator\", \"nom\": \"Voyant_Defaut\", \"proprietes\": {\"variable\": "
                          "\"Pompe_1_Defaut\", \"colorOn\": \"#E5534B\"}}]}", &why);
    check(ok && ai::validate(f.doc->project, *ok).ok() && ai::validate(f.doc->project, *ok).bindings.size() == 1, "une variable existante dans un creer : acceptee");
    // Une formule qui lit un nom inconnu ; une formule juste.
    auto fo = fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"Pompe_21\", \"proprietes\": {\"fault\": \"Pompe_2_Defaut\"}}]}", &why);
    if (fo) {
        const auto ck = ai::validate(f.doc->project, *fo);
        check(!ck.ok() && has(ck.errors.front(), "Pompe_2_Defaut") && has(ck.errors.front(), "Pompe_1_Defaut"), "une formule sur une variable inventee : refusee, la proche citee");
    }
    auto fj = fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"Pompe_11\", \"proprietes\": {\"fault\": \"Pompe_1_Defaut AND NOT Pompe_1_Marche\"}}]}", &why);
    check(fj && ai::validate(f.doc->project, *fj).ok(), "une formule sur des variables existantes : acceptee");
    // Un texte a trous : ses trous ne lisent que des variables existantes.
    auto tt = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Text\", \"nom\": \"Txt_Niveau\", \"proprietes\": {\"text\": "
                          "\"Niveau : {Niveau_Cuve_1:0.0} %\"}}, {\"action\": \"creer\", \"genre\": \"Text\", \"nom\": \"Txt_Faux\", \"proprietes\": "
                          "{\"text\": \"Debit : {Debit_Fantome} m3/h\"}}]}", &why);
    if (tt) {
        const auto ck = ai::validate(f.doc->project, *tt);
        check(ck.errors.size() == 1 && has(ck.errors.front(), "Debit_Fantome") && has(ck.errors.front(), "Txt_Faux"),
              "un texte a trous : {Niveau_Cuve_1} accepte, {Debit_Fantome} refuse");
    }
    // Un choix hors liste ; une couleur illisible ; un booleen.
    auto cx = fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"Vanne_1\", \"proprietes\": {\"labelPosition\": \"milieu\"}}]}", &why);
    if (cx) {
        const auto ck = ai::validate(f.doc->project, *cx);
        check(!ck.ok() && has(ck.errors.front(), "dessous, dessus, aucun"), "un choix hors liste : refuse, les mots cites");
    }
    auto cc = fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"Vanne_1\", \"proprietes\": {\"colorOn\": \"rouge\", \"visible\": \"peut-etre\"}}]}", &why);
    if (cc) {
        const auto ck = ai::validate(f.doc->project, *cc);
        check(ck.errors.size() == 2, "une couleur et un booleen illisibles : deux erreurs");
    }
    // Un objet cite qui n'existe pas.
    auto oc = fromTool(f, "{\"operations\": [{\"action\": \"supprimer\", \"objets\": [\"Vanne_1\", \"Vanne_99\"]}, {\"action\": \"modifier\", \"objet\": "
                          "\"Pompe_99\", \"proprietes\": {\"colorOn\": \"#E5534B\"}}]}", &why);
    if (oc) {
        const auto ck = ai::validate(f.doc->project, *oc);
        check(!ck.ok() && has(ck.errors.front(), "Pompe_99"), "un objet inconnu : refuse");
    }
}

// ------------------------------------------------------- 8. les nouvelles operations ----
std::size_t countKind(Fixture& f, hmi::Kind k) {
    return static_cast<std::size_t>(std::count_if(f.v().objects.begin(), f.v().objects.end(), [k](const hmi::Object& o) { return o.kind == k; }));
}

void newOperations() {
    std::printf("== 8. les nouvelles operations, en un Ctrl+Z ==\n");
    Fixture f;
    f.station();
    std::string why;
    const std::string before = ai::fingerprint(f.v());
    // DUPLIQUER une vanne, la copie modifiee dans la meme liste (son nom voulu la designe).
    auto d = fromTool(f, "{\"resume\": \"Vanne 4\", \"operations\": [{\"action\": \"dupliquer\", \"objets\": [\"Vanne_3\"], \"noms\": [\"Vanne_4\"], "
                         "\"dx\": 0, \"dy\": 120}, {\"action\": \"modifier\", \"objet\": \"Vanne_4\", \"proprietes\": {\"label\": \"V4\"}}]}", &why);
    check(d.has_value(), "dupliquer : lu (" + why + ")");
    if (d) {
        const auto ck = ai::validate(f.doc->project, *d);
        check(ck.ok() && ck.create == 1 && ck.modify == 0, "valide : une copie (creee puis retouchee : toujours une creation)");
        const auto* src = f.object("Vanne_3");
        const double sx = src->number("x"), sy = src->number("y");
        const hmi::Id srcId = src->id;
        const std::size_t steps = f.stack.done().size();
        check(f.apply(*d, &why) && f.stack.done().size() == steps + 1, "appliquee : UN pas (" + why + ")");
        const auto* c = f.object("Vanne_4");
        check(c && c->kind == hmi::Kind::Valve && c->number("x") == sx && c->number("y") == sy + 120 && c->text("label") == "V4",
              "Vanne_4 : une vanne, 120 px plus bas, son libelle V4");
        check(c && c->id != srcId && f.object("Vanne_3")->text("label") == "V3", "l'original n'a pas bouge");
        (void)f.stack.undo();
        check(!f.object("Vanne_4") && ai::fingerprint(f.v()) == before, "Ctrl+Z : la vue d'avant, exactement");
    }
    // DUPLIQUER un groupe : ses enfants viennent avec lui.
    {
        auto& v = f.v();
        const hmi::Id g = hmi::edit::group(f.doc->project, v, {f.object("Pompe_11")->id, f.object("Marche_P11")->id});
        (void)g;
        const std::size_t pumps = countKind(f, hmi::Kind::Pump), groups = countKind(f, hmi::Kind::Group);
        const std::string gname = v.object(g)->name;
        auto dg = fromTool(f, "{\"operations\": [{\"action\": \"dupliquer\", \"objets\": [\"" + gname + "\"], \"dx\": 300, \"dy\": 0}]}", &why);
        const auto ck = dg ? ai::validate(f.doc->project, *dg) : ai::Check{};
        check(dg && ck.ok() && ck.create == 3, "un groupe duplique : le groupe et ses deux enfants");
        check(dg && f.apply(*dg) && countKind(f, hmi::Kind::Pump) == pumps + 1 && countKind(f, hmi::Kind::Group) == groups + 1, "appliquee : une pompe et un groupe de plus");
        (void)f.stack.undo();
    }
    // DEPLACER, REDIMENSIONNER : modifier x, y, w, h (le geste natif setBox).
    auto mv = fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"Cuve_2\", \"proprietes\": {\"x\": 700, \"y\": 100, \"w\": 180, \"h\": 240}}]}", &why);
    if (mv) {
        const auto ck = ai::validate(f.doc->project, *mv);
        check(ck.ok() && ck.modify == 1 && !ck.items.empty() && ck.items.front().changes.size() == 4, "deplacer et redimensionner : x, y, w, h avant -> apres");
        check(f.apply(*mv) && f.object("Cuve_2")->box() == hmi::Box{700, 100, 180, 240}, "appliquee : la cuve est a sa nouvelle place, a sa nouvelle taille");
        (void)f.stack.undo();
    }
    // Un objet verrouille ne se deplace pas.
    f.v().objects.front().locked = true;
    const std::string lockedName = f.v().objects.front().name;
    auto lk = fromTool(f, "{\"operations\": [{\"action\": \"modifier\", \"objet\": \"" + lockedName + "\", \"proprietes\": {\"x\": 500}}]}", &why);
    check(lk && !ai::validate(f.doc->project, *lk).ok() && has(ai::validate(f.doc->project, *lk).errors.front(), "verrouill"), "un objet verrouille : refuse");
    f.v().objects.front().locked = false;
    // Repartir : trois objets ou plus.
    auto ds = fromTool(f, "{\"operations\": [{\"action\": \"distribuer\", \"objets\": [\"Pompe_11\", \"Pompe_12\"], \"sens\": \"horizontal\"}]}", &why);
    check(ds && !ai::validate(f.doc->project, *ds).ok(), "repartir deux objets : refuse (il en faut trois)");
    // Un nom deja pris : l'objet neuf s'appelle autrement, la suite de la liste le vise lui.
    auto tk = fromTool(f, "{\"operations\": [{\"action\": \"creer\", \"genre\": \"Pump\", \"nom\": \"Pompe_11\", \"proprietes\": {\"x\": 60, \"y\": 760}}, "
                          "{\"action\": \"associer\", \"objet\": \"Pompe_11\", \"variable\": \"Pompe_1_Marche\"}]}", &why);
    if (tk) {
        const auto ck = ai::validate(f.doc->project, *tk);
        check(ck.ok() && ck.create == 1 && ck.modify == 0 && !ck.warnings.empty() && has(ck.warnings.front(), "Pompe_11"), "un nom pris : averti, l'objet neuf renomme");
        check(f.apply(*tk) && f.object("Pompe_11_1") && f.object("Pompe_11_1")->text("variable") == "Pompe_1_Marche" && f.object("Pompe_11")->text("variable").empty(),
              "associer vise le nouvel objet (Pompe_11_1), pas l'ancien");
        (void)f.stack.undo();
    }
    // CODE BRUT : un bouton qui ouvre la vue Accueil (son code, tel que le logiciel l'ecrit).
    {
        hmi::View tmp;
        tmp.id = 1;
        hmi::Object b = hmi::makeObject(hmi::Kind::Button, 2, "Bouton_Accueil", 1500, 40, hmi::kNoId);
        b.set("text", "Accueil");
        hmi::Action a;
        a.operation = hmi::Operation::Navigate;
        a.target = "Accueil";
        b.actions.push_back(a);
        tmp.objects.push_back(b);
        const std::string code = hmi::serializeObjectCode(tmp, {b.id});
        ai::json::Value in = ai::json::Value::object();
        ai::json::Value op = ai::json::Value::object();
        op.set("action", "code");
        op.set("code", code);
        ai::json::Value ops = ai::json::Value::array();
        ops.push(op);
        ai::json::Value op2 = ai::json::Value::object();
        op2.set("action", "modifier");
        op2.set("objet", "Bouton_Accueil");
        ai::json::Value pr = ai::json::Value::object();
        pr.set("fill", "#2F7D4F");
        op2.set("proprietes", pr);
        ops.push(op2);
        in.set("operations", ops);
        auto cp = fromTool(f, ai::json::write(in, false), &why);
        const auto ck = cp ? ai::validate(f.doc->project, *cp) : ai::Check{};
        check(cp && ck.ok() && ck.create == 1, "code : un bouton qui navigue, valide (" + (ck.errors.empty() ? why : ck.errors.front()) + ")");
        const std::size_t steps = f.stack.done().size();
        check(cp && f.apply(*cp) && f.stack.done().size() == steps + 1, "appliquee en UN pas");
        const auto* nb = f.object("Bouton_Accueil");
        check(nb && nb->actions.size() == 1 && nb->actions.front().operation == hmi::Operation::Navigate && nb->text("fill") == "#2F7D4F",
              "le bouton pose, son action Naviguer, retouche dans la meme liste");
        (void)f.stack.undo();
        check(!f.object("Bouton_Accueil"), "Ctrl+Z : parti");
        // Du code dans une action : refuse.
        b.actions.front().operation = hmi::Operation::RunScript;
        b.actions.front().value = "Pompe_1_Marche := TRUE;";
        tmp.objects = {b};
        op.set("code", hmi::serializeObjectCode(tmp, {b.id}));
        ops = ai::json::Value::array();
        ops.push(op);
        in.set("operations", ops);
        auto bad = fromTool(f, ai::json::write(in, false), &why);
        const auto ck2 = bad ? ai::validate(f.doc->project, *bad) : ai::Check{};
        check(bad && !ck2.ok() && has(ck2.errors.front(), "du code"), "une action qui execute un script : refusee");
        // Une action qui ecrit une variable inventee ; une propriete inventee.
        b.actions.front().operation = hmi::Operation::Set;
        b.actions.front().target = "Variable_Fantome";
        b.actions.front().value.clear();
        b.props.push_back({"couleurMagique", "#FF0000", ""});
        tmp.objects = {b};
        op.set("code", hmi::serializeObjectCode(tmp, {b.id}));
        ops = ai::json::Value::array();
        ops.push(op);
        in.set("operations", ops);
        auto bad2 = fromTool(f, ai::json::write(in, false), &why);
        const auto ck3 = bad2 ? ai::validate(f.doc->project, *bad2) : ai::Check{};
        bool var = false, prop = false;
        for (const auto& e : ck3.errors) {
            var = var || has(e, "Variable_Fantome");
            prop = prop || has(e, "couleurMagique");
        }
        check(bad2 && var && prop, "une variable et une propriete inventees dans le code : refusees");
        op.set("code", "objet genre=Licorne nom=X\nfin\n");
        ops = ai::json::Value::array();
        ops.push(op);
        in.set("operations", ops);
        auto bad3 = fromTool(f, ai::json::write(in, false), &why);
        check(bad3 && !ai::validate(f.doc->project, *bad3).ok(), "un code illisible : refuse");
    }
    // Le format : les nouvelles operations font l'aller-retour JSON (la conversation les garde).
    if (d) {
        std::vector<ai::Operation> ops;
        check(ai::operationsFromJson(ai::operationsToJson(d->ops), ops) && ops.size() == 2 && ops[0].action == ai::Operation::Action::Duplicate
                  && ops[0].names == std::vector<std::string>{"Vanne_4"} && ops[0].dx == 0 && ops[0].dy == 120,
              "dupliquer : JSON aller-retour (noms, dx, dy)");
    }
    // L'apercu : la copie, rien ne change dans le projet.
    if (d) {
        const std::string fp = ai::fingerprint(f.v());
        const auto pv = ai::preview(f.doc->project, *d);
        check(pv.ok && pv.touched.created.size() == 1 && ai::fingerprint(f.v()) == fp, "l'apercu de dupliquer : une copie, la vue intacte");
    }
}

// ------------------------------------------------------- 9. les variables de l'automate ----
void plcVariables() {
    std::printf("== 9. une variable de l'automate relie ==\n");
    Fixture f;
    f.station();
    ai::Env env;
    env.plcName = [](std::string_view n) { return n == "Pression_Ligne" || n == "API.Pression_Ligne"; };
    std::string why;
    auto p = fromTool(f, "{\"operations\": [{\"action\": \"associer\", \"objet\": \"Pompe_11\", \"variable\": \"API.Pression_Ligne\"}]}", &why);
    check(p && ai::validate(f.doc->project, *p, env).ok(), "associer a une variable de l'automate : valide avec l'Env");
    check(p && !ai::validate(f.doc->project, *p).ok(), "sans l'Env : introuvable");
    auto cmd = p ? ai::makeCommand(f.doc, *p, &why, env) : nullptr;
    check(cmd != nullptr, "makeCommand avec l'Env : la commande se fait (" + why + ")");
    if (cmd) (void)f.stack.push(std::move(cmd));
    check(f.object("Pompe_11")->text("variable") == "API.Pression_Ligne", "la pompe lit la variable de l'automate");
}

} // namespace

int main(int argc, char** argv) {
    const std::string example = argc > 1 ? argv[1] : std::string{};
    registryAgainstTables();
    compactText();
    sizes(example);
    context();
    knowledge();
    toolSchema();
    fromToolInputs();
    newOperations();
    plcVariables();
    std::printf("ai_registre_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
