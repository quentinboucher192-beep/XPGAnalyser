// =============================================================================
//  tests/hmi_build_test.cpp - 1.11.13 : la generation incrementale de l'IHM
// -----------------------------------------------------------------------------
//  Le build de la Simulation IHM (hmi/HmiPipeline) sur un petit projet complet :
//  des variables IHM, une fonction, un type, un symbole et sa fonction, trois
//  vues (deux citent la meme variable de l'API, la troisieme rien), un script
//  general, des scripts de vue, une alarme.
//
//  Chaque essai dit ce qui est REFAIT et ce qui ne l'est pas : un projet a jour
//  ne refait rien ; un script modifie ne refait que lui ; une variable de l'API
//  change de type : ce qui la cite, pas le reste ; une fonction change de corps :
//  elle seule ; de signature : ses appelants aussi ; un symbole supprime : la vue
//  qui le pose echoue ; une compilation echouee bloque le demarrage, la
//  correction le debloque ; Regenerer un element, une branche, tout ; le cache
//  absent ou coupe, un artefact supprime a la main, une annulation, un verrou,
//  une coupure pendant l'ecriture d'un fichier.
// =============================================================================
#include "../src/app/hmi/HmiBuild.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPipeline.hpp"
#include "../src/hmi/HmiScript.hpp"   // 1.11.16 : renameFunctionEverywhere (la commande Renommer)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace hmi;
namespace pl = hmi::pipeline;
namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("  ECHEC  %s\n", what.c_str());
        ++failures;
    }
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

// ---- le projet ------------------------------------------------------------------
struct Bench {
    Project     p;
    pl::ApiInfo api;
    pl::Cache   cache;                                  // le cache entre deux builds (relu de son texte)
    std::map<std::string, std::string> artifacts;       // les artefacts "sur le disque"
    pl::Report  last;
    std::string folder;                                 // non vide : le vrai disque (build-cache.txt, artefacts)
    Id fnMoyenne{kNoId}, scriptInit{kNoId}, typeVanne{kNoId}, symVanne{kNoId}, fnOuvrir{kNoId};
    Id vueSyn{kNoId}, vueMes{kNoId}, vueAide{kNoId}, scriptCycle{kNoId}, scriptMes{kNoId}, alarme{kNoId};
    Id texteAide{kNoId}, jaugeSyn{kNoId};

    pl::Options options() {
        pl::Options o;
        o.now = "2026-10-08 10:00:00";
        o.plcHasName = [this](std::string_view n) {
            for (const auto& v : api.variables)
                if (v.name.size() == n.size() && std::equal(n.begin(), n.end(), v.name.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
                    return true;
            return false;
        };
        o.plcPaths.rootType = [this](std::string_view n) -> std::string {
            for (const auto& v : api.variables)
                if (v.name == n) return v.type;
            return {};
        };
        if (!folder.empty()) {
            o.buildFolder = folder;
            o.projectFolder = folder;
            return o;
        }
        o.cache = &cache;
        o.artifacts = [this](const std::string& rel, const std::string& h) {
            const auto it = artifacts.find(rel);
            return it != artifacts.end() && pl::hash(it->second) == h;
        };
        o.writeArtifact = [this](const std::string& rel, const std::string& text) { artifacts[rel] = text; };
        o.removeArtifact = [this](const std::string& rel) { artifacts.erase(rel); };
        return o;
    }
    pl::Report build(pl::Mode m, std::vector<std::string> scope = {}, bool chosen = false, const pl::ProgressFn& progress = {},
                     const std::atomic<bool>* cancel = nullptr) {
        last = pl::run(p, api, pl::Request{m, std::move(scope), chosen}, options(), progress, cancel);
        // le cache suivant : celui que le texte ecrit redonne (le format est essaye a chaque build)
        if (folder.empty()) {
            cache = pl::parseCache(pl::serialize(last.cache));
            check(cache.status == pl::Cache::Status::Ok, "le cache ecrit se relit (" + cache.why + ")");
        }
        return last;
    }
    pl::Analysis analysis() {
        const pl::Cache c = folder.empty() ? cache : pl::loadCache(folder);
        const auto o = options();
        return pl::analyse(pl::collect(p, api), c, o.artifacts ? o.artifacts : pl::diskArtifacts(folder));
    }
    pl::State state(const std::string& key) {
        const pl::Cache c = folder.empty() ? cache : pl::loadCache(folder);
        return pl::shown(analysis(), c, key).state;
    }
    // Les cles refaites par le dernier build (generees), et compilees.
    std::set<std::string> generated() const {
        std::set<std::string> out;
        for (const auto& l : last.log)
            if (l.category == "G\xC3\xA9n\xC3\xA9ration" && !l.element.empty() && l.severity == pl::Severity::Information) out.insert(l.element);
        return out;
    }
    std::set<std::string> compiled() const {
        std::set<std::string> out;
        for (const auto& l : last.log)
            if (l.category == "Compilation" && !l.element.empty() && l.severity != pl::Severity::Error) out.insert(l.element);
        return out;
    }
    View&   view(Id id) { return *p.view(id); }
    Script& script(Id id) { return *p.script(id); }
};

std::string key(pl::ElementKind k, Id id) { return std::string(pl::kindKey(k)) + ":" + std::to_string(id); }

std::string join(const std::set<std::string>& s) {
    std::string out;
    for (const auto& x : s) out += (out.empty() ? "" : ", ") + x;
    return out.empty() ? "(rien)" : out;
}

void fill(Bench& b) {
    Project& p = b.p;
    b.api.config = "CPU=M580 tache MAST 20 ms";
    b.api.variables = {{"Pression_Gaz", "REAL", "%MF100"}, {"Vanne_Ouverte", "BOOL", "%M10"}, {"Inutile", "INT", "%MW5"}};
    p.programs.variables.push_back(Variable{p.allocate(), "Compteur", "INT", "0", "un compteur"});
    p.programs.variables.push_back(Variable{p.allocate(), "Debit", "REAL", "0", {}});
    p.programs.variables.push_back(Variable{p.allocate(), "Mode", "STRING", "'Auto'", {}});
    HmiType t;
    t.id = b.typeVanne = p.allocate();
    t.name = "T_Vanne";
    t.members = {{"Ouverte", "BOOL", "", ""}, {"Position", "REAL", "", ""}};
    p.programs.types.push_back(t);
    p.programs.variables.push_back(Variable{p.allocate(), "Vanne_A", "T_Vanne", "", {}});
    b.fnMoyenne = p.allocate();
    p.programs.functions.push_back(HmiFunction{b.fnMoyenne, "Moyenne", "REAL", "VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := (a + b) / 2.0;", "la moyenne", false});
    b.scriptInit = p.allocate();
    p.programs.scripts.push_back(Script{b.scriptInit, "Init", ScriptLang::ST, "Demarrage", "Debit := Moyenne(1.0, 3.0);\nCompteur := 0;", 1000, "", "au demarrage", ""});

    // le symbole Sym_Vanne (un parametre V : T_Vanne, une fonction Ouvrir)
    View sym = makeView(p, "Sym_Vanne");
    sym.role = "symbole";
    sym.width = 80;
    sym.height = 80;
    sym.params = {ViewParam{"V", "", "", "T_Vanne", ParamMode::Reference}};
    {
        const Id id = edit::add(p, sym, Kind::Indicator, 10, 10);
        sym.object(id)->name = "Voyant";
        sym.object(id)->setExpr("value", "V.Ouverte");
    }
    b.fnOuvrir = p.allocate();
    sym.functions.push_back(HmiFunction{b.fnOuvrir, "Ouvrir", "", "V.Ouverte := TRUE;", {}, false});
    b.symVanne = sym.id;
    p.views.push_back(sym);

    // Vue_Synoptique : la pression de l'API, une instance du symbole, un script cyclique
    View syn = makeView(p, "Vue_Synoptique");
    b.jaugeSyn = edit::add(p, syn, Kind::Gauge, 100, 100);
    syn.object(b.jaugeSyn)->name = "Jauge_Pression";
    syn.object(b.jaugeSyn)->setExpr("value", "Pression_Gaz");
    {
        const Id id = edit::add(p, syn, Kind::SymbolInstance, 400, 100);
        syn.object(id)->name = "Vanne_1";
        syn.object(id)->set("symbol", "Sym_Vanne");
        syn.object(id)->set("params", "V := Vanne_A");
    }
    b.scriptCycle = p.allocate();
    syn.scripts.push_back(Script{b.scriptCycle, "Cycle", ScriptLang::ST, "OnCycle", "Compteur := Compteur + 1;", 1000, "", "", ""});
    b.vueSyn = syn.id;
    p.views.push_back(syn);

    // Vue_Mesures : la meme variable de l'API, un script
    View mes = makeView(p, "Vue_Mesures");
    {
        const Id id = edit::add(p, mes, Kind::Gauge, 100, 100);
        mes.object(id)->name = "Jauge_Double";
        mes.object(id)->setExpr("value", "Pression_Gaz * 2.0");
    }
    b.scriptMes = p.allocate();
    mes.scripts.push_back(Script{b.scriptMes, "Ouverture", ScriptLang::ST, "OnOpen", "Mode := 'Mesures';", 1000, "", "", ""});
    b.vueMes = mes.id;
    p.views.push_back(mes);

    // Vue_Aide : un texte fixe, rien de cite
    View aide = makeView(p, "Vue_Aide");
    b.texteAide = edit::add(p, aide, Kind::Text, 50, 50);
    aide.object(b.texteAide)->name = "Texte_Aide";
    aide.object(b.texteAide)->set("text", "Appuyer sur F1");
    b.vueAide = aide.id;
    p.views.push_back(aide);
    p.config.startView = b.vueSyn;

    AlarmDef a;
    a.id = b.alarme = p.allocate();
    a.name = "Pression_Haute";
    a.condition = "Pression_Gaz > 5.0";
    a.message = "Pression haute";
    p.alarms.push_back(a);
}

// ---- les essais -----------------------------------------------------------------

// Les elements, leurs genres et leurs dependances : ce que le build suit.
void collecte() {
    std::printf("-- collecte : elements, ordre API puis IHM, dependances\n");
    Bench b;
    fill(b);
    const auto els = pl::collect(b.p, b.api);
    std::map<std::string, const pl::Element*> by;
    for (const auto& e : els) by[e.key] = &e;
    check(by.count("api-config") && by.count("api-echanges"), "la configuration de l'API et ses tables d'echange sont des elements");
    check(by.count("api-variable:Pression_Gaz") && by.count("api-variable:Vanne_Ouverte") == 0 && by.count("api-variable:Inutile") == 0,
          "seule la variable de l'API que l'IHM cite est collectee (Pression_Gaz ; pas Vanne_Ouverte ni Inutile)");
    // l'ordre : toute l'API avant toute l'IHM, et les etapes dans l'ordre
    bool ordered = true;
    for (std::size_t k = 1; k < els.size(); ++k) {
        const int a = (pl::areaOf(els[k - 1].kind) == pl::Area::Api ? 0 : 100) + pl::stepOf(els[k - 1].kind);
        const int c = (pl::areaOf(els[k].kind) == pl::Area::Api ? 0 : 100) + pl::stepOf(els[k].kind);
        ordered = ordered && a <= c;
    }
    check(ordered, "les elements sont dans l'ordre du build : API (4 etapes) puis IHM (16 etapes)");
    const auto has = [&](const std::string& from, const std::string& to, pl::DepMode mode) {
        const auto it = by.find(from);
        if (it == by.end()) return false;
        for (const auto& d : it->second->deps)
            if (d.key == to && d.mode == mode) return true;
        return false;
    };
    const std::string init = key(pl::ElementKind::Script, b.scriptInit);
    check(has(init, key(pl::ElementKind::Function, b.fnMoyenne), pl::DepMode::Interface), "Init cite Moyenne : dependance d'interface");
    check(has(init, key(pl::ElementKind::Variable, b.p.programs.variables[1].id), pl::DepMode::Interface), "Init cite Debit : dependance d'interface");
    check(has(key(pl::ElementKind::View, b.vueSyn), key(pl::ElementKind::Symbol, b.symVanne), pl::DepMode::Content),
          "Vue_Synoptique pose Sym_Vanne : dependance de contenu (le symbole est deballe)");
    check(has(key(pl::ElementKind::Animations, b.vueSyn), "api-variable:Pression_Gaz", pl::DepMode::Interface),
          "les animations de Vue_Synoptique citent Pression_Gaz (l'API)");
    check(has(key(pl::ElementKind::Animations, b.vueMes), "api-variable:Pression_Gaz", pl::DepMode::Interface),
          "les animations de Vue_Mesures aussi");
    check(!has(key(pl::ElementKind::Animations, b.vueAide), "api-variable:Pression_Gaz", pl::DepMode::Interface), "Vue_Aide ne cite rien");
    check(has(key(pl::ElementKind::Alarm, b.alarme), "api-variable:Pression_Gaz", pl::DepMode::Interface), "l'alarme cite Pression_Gaz");
    check(has(key(pl::ElementKind::Variable, b.p.programs.variables[3].id), key(pl::ElementKind::Type, b.typeVanne), pl::DepMode::Content),
          "Vanne_A est de type T_Vanne : dependance de contenu");
    check(by.count(key(pl::ElementKind::SymbolFunction, b.fnOuvrir)) && by[key(pl::ElementKind::SymbolFunction, b.fnOuvrir)]->path == "IHM/Symboles/Sym_Vanne/Fonctions/Ouvrir",
          "la fonction du symbole est un element, a son chemin");
    // la documentation n'entre pas dans les empreintes
    const auto e1 = pl::collect(b.p, b.api);
    b.script(b.scriptInit).description = "une autre description";
    b.p.programs.variables[0].description = "autre";
    b.p.programs.functions[0].description = "autre";
    b.view(b.vueAide).description = "autre";
    const auto e2 = pl::collect(b.p, b.api);
    bool same = e1.size() == e2.size();
    for (std::size_t k = 0; same && k < e1.size(); ++k) same = e1[k].content == e2[k].content && e1[k].config == e2[k].config && e1[k].iface == e2[k].iface;
    check(same, "changer une description ne change aucune empreinte");
    // les identifiants d'un code : sans chaines ni commentaires, les trous {Nom} comptent, les indices aussi
    const auto ids = pl::identifiers("x := Armoires[i].ana; (* Pas_Moi *) // Ni_Moi\nIHM_LOG(INFO, 'Debit : {Debit:0.0} {Mode}'); t := T#2s;");
    const std::set<std::string> got(ids.begin(), ids.end());
    check(got.count("x") && got.count("Armoires.ana") && got.count("i") && got.count("Debit") && got.count("Mode") && !got.count("Pas_Moi") && !got.count("Ni_Moi")
              && !got.count("T") && !got.count("s"),
          "identifiers : chaines, commentaires, indices, trous {Nom} et litteraux T#2s");
    check(pl::signatureOf("VAR_INPUT\n  a : REAL; (* x *)\n  b : REAL;\nEND_VAR\nRETURN;") == "a : REAL; b : REAL;", "signatureOf : le bloc VAR_INPUT normalise");
}

// demarrage sans modification ; premiere generation complete
void premiereGenerationEtDemarrageSansModification() {
    std::printf("-- premiere generation complete, puis demarrage sans modification\n");
    Bench b;
    fill(b);
    std::vector<pl::Progress> seen;
    const auto r1 = b.build(pl::Mode::Start, {}, false, [&seen](const pl::Progress& pr) { seen.push_back(pr); });
    check(r1.ok && r1.errors == 0, "premier build sans erreur (" + std::to_string(r1.errors) + " erreur(s))");
    if (!r1.ok) for (const auto& d : r1.diagnostics) if (d.blocking()) std::printf("         [%s] %s : %s\n", d.code.c_str(), d.path.c_str(), d.message.c_str());
    check(contains(r1.fullReason, "cache absent"), "premier build : regeneration complete, raison dite (" + r1.fullReason + ")");
    const auto all = pl::collect(b.p, b.api);
    std::size_t code = 0;
    for (const auto& e : all) code += pl::compilable(e.kind) ? 1 : 0;
    std::printf("         %zu elements, %zu avec du code ; %zu taches, %zu generes, %zu compiles, %.1f ms\n", all.size(), code, r1.tasks, r1.generated, r1.compiled, r1.ms);
    check(all.size() >= 25 && code >= 10, "le projet d'essai a de quoi construire");
    check(r1.generated == all.size(), "tout est genere (" + std::to_string(r1.generated) + " / " + std::to_string(all.size()) + ")");
    check(r1.compiled == code, "tout ce qui a du code est compile (" + std::to_string(r1.compiled) + " / " + std::to_string(code) + ")");
    check(b.artifacts.size() == all.size(), "un artefact par element (" + std::to_string(b.artifacts.size()) + ")");
    check(b.artifacts.count("ihm/08-vues/vue_" + std::to_string(b.vueSyn) + "-Vue_Synoptique.txt") == 1, "l'artefact d'une vue : ihm/08-vues/<cle>-<nom>.txt");
    {
        const auto& a = b.artifacts["ihm/08-vues/vue_" + std::to_string(b.vueSyn) + "-Vue_Synoptique.txt"];
        check(contains(a, "Vanne_1") && contains(a, "Voyant"), "la vue generee contient l'instance deballee (le voyant du symbole)");
        check(a.size() > 4 && a.substr(a.size() - 4) == "fin\n", "un artefact finit par \"fin\"");
    }
    {
        // les dossiers et les noms sans accent ni octet UTF-8 ("05-scripts_generaux", pas "05-scripts_g__n__raux")
        bool ascii = true, scripts = false;
        for (const auto& [rel, t] : b.artifacts) {
            for (const char c : rel) ascii = ascii && static_cast<unsigned char>(c) < 0x80;
            scripts = scripts || rel.rfind("ihm/05-scripts_generaux/", 0) == 0;
        }
        check(ascii && scripts && !b.artifacts.empty(), "les chemins des artefacts sont en ASCII : ihm/05-scripts_generaux/...");
    }
    // la progression : de vraies etapes, toutes terminees
    check(!seen.empty() && seen.back().done == seen.back().total && seen.back().total == r1.tasks, "la progression va de 0 a toutes les taches");
    bool apiFirst = true, sawIhm = false;
    for (const auto& pr : seen) {
        if (pr.phases[static_cast<int>(pl::Phase::Ihm)] == pl::PhaseState::Running) sawIhm = true;
        if (sawIhm && pr.phases[static_cast<int>(pl::Phase::Api)] == pl::PhaseState::Running) apiFirst = false;
    }
    check(apiFirst && sawIhm, "la generation de l'API est finie avant que celle de l'IHM commence");
    check(seen.back().phases[static_cast<int>(pl::Phase::Validate)] == pl::PhaseState::Done, "la validation finale est faite");
    for (const auto& e : all) {
        const auto st = b.state(e.key);
        if (st != pl::State::UpToDate) { check(false, e.path + " : " + std::string(pl::stateLabel(st)) + " apres le build (attendu : a jour)"); break; }
    }
    // demarrer a nouveau sans rien changer : rien n'est refait
    const auto r2 = b.build(pl::Mode::Start);
    check(r2.ok && r2.upToDate && r2.tasks == 0 && r2.generated == 0 && r2.compiled == 0, "demarrage sans modification : rien n'est refait (Projet a jour)");
    bool said = false;
    for (const auto& l : r2.log) said = said || contains(l.message, "Projet \xC3\xA0 jour");
    check(said, "le journal dit \"Projet a jour\"");
    check(r2.reused == all.size(), "tous les elements sont reutilises");
}

// modification d'un seul script ; d'une vue sans impact ; de la documentation
void unSeulScriptEtUneVue() {
    std::printf("-- un seul script, une vue sans impact, la documentation\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    b.script(b.scriptCycle).body = "Compteur := Compteur + 2;";
    check(b.state(key(pl::ElementKind::ViewScript, b.scriptCycle)) == pl::State::Modified, "le script modifie est Modifie dans l'arbre, tout de suite");
    check(b.state(key(pl::ElementKind::ViewScript, b.scriptMes)) == pl::State::UpToDate, "l'autre script reste a jour");
    const auto r = b.build(pl::Mode::Start);
    check(r.ok, "build apres modification d'un script");
    check(b.generated() == std::set<std::string>{key(pl::ElementKind::ViewScript, b.scriptCycle)}, "seul le script est regenere (" + join(b.generated()) + ")");
    check(b.compiled() == std::set<std::string>{key(pl::ElementKind::ViewScript, b.scriptCycle)}, "seul le script est recompile (" + join(b.compiled()) + ")");
    // une vue sans impact : un objet deplace dans Vue_Aide
    b.view(b.vueAide).object(b.texteAide)->set("x", "70");
    const auto r2 = b.build(pl::Mode::Start);
    check(r2.ok && b.generated() == std::set<std::string>{key(pl::ElementKind::View, b.vueAide)}, "Vue_Aide modifiee : elle seule est regeneree (" + join(b.generated()) + ")");
    check(b.compiled().empty(), "rien n'est compile (un deplacement n'a pas de code)");
    // la documentation : rien
    b.script(b.scriptInit).description = "autre";
    b.view(b.vueMes).description = "autre";
    const auto r3 = b.build(pl::Mode::Start);
    check(r3.upToDate && r3.tasks == 0, "changer une description ne refait rien");
    // un commentaire change le code (il est dans le script) : le script est recompile
    b.script(b.scriptInit).body += "\n(* commentaire *)";
    (void)b.build(pl::Mode::Start);
    check(b.generated() == std::set<std::string>{key(pl::ElementKind::Script, b.scriptInit)}, "un commentaire dans un script : le script seul");
}

// modification d'une variable API utilisee par plusieurs vues
void variableApi() {
    std::printf("-- une variable de l'API utilisee par plusieurs vues\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    // son adresse change : son interface (nom, type) non - elle seule
    b.api.variables[0].address = "%MF200";
    (void)b.build(pl::Mode::Start);
    check(b.generated() == std::set<std::string>{"api-variable:Pression_Gaz"}, "adresse changee : seule la variable de l'API est regeneree (" + join(b.generated()) + ")");
    // son type change : ce qui la cite (les animations des deux vues, l'alarme), pas Vue_Aide
    b.api.variables[0].type = "LREAL";
    check(b.state(key(pl::ElementKind::Animations, b.vueSyn)) == pl::State::Obsolete, "les animations qui la citent sont Obsoletes avant le build");
    const auto r = b.build(pl::Mode::Start);
    check(r.ok, "build apres le changement de type");
    const std::set<std::string> want{"api-variable:Pression_Gaz", key(pl::ElementKind::Animations, b.vueSyn), key(pl::ElementKind::Animations, b.vueMes),
                                     key(pl::ElementKind::Alarm, b.alarme)};
    check(b.generated() == want, "type change : l'API, les animations des deux vues et l'alarme (" + join(b.generated()) + ")");
    check(!b.generated().count(key(pl::ElementKind::View, b.vueAide)) && !b.generated().count(key(pl::ElementKind::Animations, b.vueAide)), "Vue_Aide n'est pas touchee");
    check(b.compiled() == std::set<std::string>{key(pl::ElementKind::Animations, b.vueSyn), key(pl::ElementKind::Animations, b.vueMes), key(pl::ElementKind::Alarm, b.alarme)},
          "les expressions qui la citent sont recompilees (" + join(b.compiled()) + ")");
    // la raison de l'obsolescence se lit
    b.api.variables[0].type = "REAL";
    const auto a = b.analysis();
    const auto* it = a.item(key(pl::ElementKind::Animations, b.vueMes));
    check(it && it->status == pl::Status::Obsolete && !it->reasons.empty() && contains(it->reasons.front(), "Pression_Gaz : LREAL") && contains(it->reasons.front(), "Pression_Gaz : REAL"),
          "la raison dit l'interface avant et apres (" + (it && !it->reasons.empty() ? it->reasons.front() : std::string("?")) + ")");
}

// renommage d'une fonction IHM ; corps et signature
void fonctionIhm() {
    std::printf("-- une fonction IHM : corps, signature, renommage\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    const std::string fn = key(pl::ElementKind::Function, b.fnMoyenne), init = key(pl::ElementKind::Script, b.scriptInit);
    // le corps change, pas la signature : la fonction seule
    b.p.programs.functions[0].body = "VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := (a + b) * 0.5;";
    (void)b.build(pl::Mode::Start);
    check(b.generated() == std::set<std::string>{fn} && b.compiled() == std::set<std::string>{fn}, "corps change : la fonction seule (" + join(b.generated()) + ")");
    // la signature change : ses appelants aussi
    b.p.programs.functions[0].body = "VAR_INPUT\n    a : REAL;\n    b : REAL;\n    c : REAL := 0.0;\nEND_VAR\nMoyenne := (a + b + c) / 3.0;";
    (void)b.build(pl::Mode::Start);
    check(b.generated().count(fn) && b.generated().count(init), "signature changee : la fonction et le script qui l'appelle (" + join(b.generated()) + ")");
    // renommee sans ses appels : l'appelant est recompile et echoue
    b.p.programs.functions[0].name = "Moyenne3";
    b.p.programs.functions[0].body = "VAR_INPUT\n    a : REAL;\n    b : REAL;\n    c : REAL := 0.0;\nEND_VAR\nMoyenne3 := (a + b + c) / 3.0;";
    {
        const auto a = b.analysis();
        const auto* it = a.item(fn);
        check(it && it->status == pl::Status::Renamed && contains(it->reasons.front(), "Fonctions/Moyenne \xE2\x86\x92 IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Fonctions/Moyenne3"),
              "la fonction est Renommee (" + (it && !it->reasons.empty() ? it->reasons.front() : std::string("?")) + ")");
    }
    const auto r = b.build(pl::Mode::Start);
    check(!r.ok && b.generated().count(init), "renommee sans ses appels : Init est recompile et echoue");
    bool unknown = false;
    for (const auto& d : r.diagnostics) unknown = unknown || (d.element == init && d.blocking() && contains(d.message, "Moyenne"));
    check(unknown, "l'erreur est sur Init et nomme Moyenne");
    check(b.state(init) == pl::State::CompilationFailed, "Init : Compilation echouee dans l'arbre");
    // l'appel suit le renommage (ce que fait Renommer) : corrige
    b.script(b.scriptInit).body = "Debit := Moyenne3(1.0, 3.0);\nCompteur := 0;";
    const auto r2 = b.build(pl::Mode::Start);
    check(r2.ok && b.state(init) == pl::State::UpToDate, "l'appel renomme : Init se recompile sans erreur");
    // 1.11.16 : LA VRAIE COMMANDE Renommer (les appels suivent partout), puis le build incremental.
    const std::size_t followed = renameFunctionEverywhere(b.p, "Moyenne3", "Moyenne_Ponderee");
    for (auto& f : b.p.programs.functions)
        if (f.name == "Moyenne3") f.name = "Moyenne_Ponderee";
    check(followed >= 2 && contains(b.script(b.scriptInit).body, "Moyenne_Ponderee(1.0, 3.0)")
              && contains(b.p.programs.functions[0].body, "Moyenne_Ponderee := "),
          "Renommer (la commande) : l'appel de Init et le retour du corps suivent (" + std::to_string(followed) + " textes)");
    {
        const auto a = b.analysis();
        const auto* it = a.item(fn);
        check(it && it->status == pl::Status::Renamed, "... la fonction est Renommee dans l'arbre, tout de suite");
    }
    const auto r3 = b.build(pl::Mode::Start);
    check(r3.ok && b.generated().count(fn) && b.generated().count(init) && b.state(init) == pl::State::UpToDate && b.state(fn) == pl::State::UpToDate,
          "renommee par la commande : la fonction et son appelant refaits, sans erreur (" + join(b.generated()) + ")");
}

// 1.11.16 : un appel invalide a IHM_LOG fait echouer le build (une erreur bloquante, sur sa
// ligne, qui dit les niveaux permis) ; corrige, le build passe.
void ihmLogInvalide() {
    std::printf("-- IHM_LOG : un appel invalide fait echouer le build ; corrige, il passe\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    const std::string init = key(pl::ElementKind::Script, b.scriptInit);
    const std::string before = b.script(b.scriptInit).body;
    b.script(b.scriptInit).body = before + "\nIHM_LOG(BLABLA, 'x');";
    const auto r = b.build(pl::Mode::Start);
    const pl::Diagnostic* bad = nullptr;
    for (const auto& d : r.diagnostics)
        if (!bad && d.element == init && d.blocking() && contains(d.message, "BLABLA")) bad = &d;
    check(!r.ok && bad, "IHM_LOG(BLABLA, 'x') : le build \xC3\xA9" "choue, une erreur bloquante sur Init");
    check(bad && bad->line == 3 && contains(bad->message, "niveau"), "... sur sa ligne (3), qui dit le niveau attendu (" + (bad ? bad->message : std::string("?")) + ")");
    check(b.state(init) == pl::State::CompilationFailed, "Init : Compilation \xC3\xA9" "chou\xC3\xA9" "e dans l'arbre");
    b.script(b.scriptInit).body = before + "\nIHM_LOG(INFO, 'Init : {Compteur}');";
    const auto r2 = b.build(pl::Mode::Start);
    check(r2.ok && b.state(init) == pl::State::UpToDate, "IHM_LOG(INFO, 'Init : {Compteur}') : le build passe");
}

// 1.11.16 : LA REGLE DE L'ARRET SUR MODIFICATION (pl::runChange ; l'ecran l'applique a
// chaque analyse pendant la marche) - ce qui l'arrete, ce qui ne l'arrete pas, la carte.
void arretSurModification() {
    std::printf("-- la regle de l'arret sur modification : ce qui arrete la simulation, ce qui ne l'arrete pas\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    const auto start = b.analysis();
    const auto prints = pl::runPrints(start);
    std::unordered_map<std::string, std::string> paths;
    for (const auto& e : start.elements) paths[e.key] = e.path;
    check(prints.size() == start.elements.size() && !prints.empty(), "une empreinte d'ex\xC3\xA9" "cution par \xC3\xA9l\xC3\xA9ment au d\xC3\xA9marrage");
    auto c = pl::runChange(prints, paths, b.analysis(), true);
    check(!c.stop && c.changed.empty() && c.removed.empty(), "rien de chang\xC3\xA9 : la simulation continue");
    b.script(b.scriptInit).description = "autre";
    b.view(b.vueMes).description = "autre";
    c = pl::runChange(prints, paths, b.analysis(), true);
    check(!c.stop && c.changed.empty(), "une description (un script, une vue) : la simulation continue");
    b.script(b.scriptCycle).body = "Compteur := Compteur + 2;";
    c = pl::runChange(prints, paths, b.analysis(), true);
    check(c.stop && c.changed == std::vector<std::string>{key(pl::ElementKind::ViewScript, b.scriptCycle)} && c.compile == 1 && c.generate == 0,
          "un script modifi\xC3\xA9 par le d\xC3\xA9veloppeur : arr\xC3\xAAt, 1 \xC3\xA0 compiler");
    check(c.head == "1 \xC3\xA9l\xC3\xA9ment modifi\xC3\xA9 : 1 \xC3\xA0 compiler" && contains(c.card, "\xC2\xB7 Cycle (Script de vue, \xC3\xA0 compiler)"),
          "la carte : " + c.head + " / " + c.card);
    c = pl::runChange(prints, paths, b.analysis(), false);
    check(!c.stop && !c.changed.empty(), "le m\xC3\xAAme changement \xC3\xA9" "crit par la simulation (une recette, un utilisateur) : elle continue");
    b.view(b.vueAide).object(b.texteAide)->set("x", "70");
    c = pl::runChange(prints, paths, b.analysis(), true);
    check(c.stop && c.compile == 1 && c.generate == 1 && c.head == "2 \xC3\xA9l\xC3\xA9ments modifi\xC3\xA9s : 1 \xC3\xA0 compiler, 1 \xC3\xA0 g\xC3\xA9n\xC3\xA9rer",
          "un script et une vue : " + c.head);
    std::erase_if(b.p.alarms, [&b](const AlarmDef& a) { return a.id == b.alarme; });
    b.script(b.scriptInit).body += "\n(* encore *)";
    b.script(b.scriptMes).body = "Mode := 'Autre';";
    c = pl::runChange(prints, paths, b.analysis(), true);
    const std::size_t total = c.changed.size() + c.removed.size();
    check(c.stop && c.removed.size() == 1 && c.changed.size() >= 4 && contains(c.card, "\xE2\x80\xA6 et " + std::to_string(total - 3) + " autre(s)")
              && c.paths.size() == total && contains(c.paths.back(), "(supprim\xC3\xA9)"),
          "des modifi\xC3\xA9s et l'alarme supprim\xC3\xA9" "e : 3 lignes sur la carte, \xC2\xAB \xE2\x80\xA6 et N autre(s) \xC2\xBB, tous les chemins pour les Sorties (" + c.head + ")");
}

// suppression d'un symbole utilise
void suppressionSymbole() {
    std::printf("-- suppression d'un symbole utilise\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    const std::size_t before = b.artifacts.size();
    const std::string symKey = key(pl::ElementKind::Symbol, b.symVanne), vueKey = key(pl::ElementKind::View, b.vueSyn);
    b.p.views.erase(std::remove_if(b.p.views.begin(), b.p.views.end(), [&b](const View& v) { return v.id == b.symVanne; }), b.p.views.end());
    const auto a = b.analysis();
    check(a.deleted.size() >= 3, "le symbole, sa fonction, ses animations sont supprimes du cache (" + std::to_string(a.deleted.size()) + ")");
    check(a.item(vueKey) && a.item(vueKey)->status == pl::Status::MissingDependency, "la vue qui pose le symbole : dependance introuvable");
    const auto r = b.build(pl::Mode::Start);
    check(!r.ok, "le build echoue");
    bool e120 = false;
    for (const auto& d : r.diagnostics) e120 = e120 || (d.code == "E120" && d.element == vueKey && contains(d.message, "Sym_Vanne"));
    check(e120, "E120 Symbole introuvable : Sym_Vanne, sur Vue_Synoptique");
    check(b.state(vueKey) == pl::State::InvalidDependency, "Vue_Synoptique : Dependance invalide (son symbole est supprime)");
    check(b.cache.entries[vueKey].generation == pl::State::InvalidDependency && b.cache.entries[vueKey].failedHash == b.cache.entries[vueKey].contentHash,
          "le cache garde l'echec (pas \"Genere\")");
    check(b.artifacts.size() < before, "les artefacts du symbole sont retires");
    bool symGone = true;
    for (const auto& [rel, text] : b.artifacts) symGone = symGone && !contains(rel, "symbole_" + std::to_string(b.symVanne));
    check(symGone, "plus d'artefact du symbole");
    check(b.cache.entries.count(symKey) == 0, "plus d'entree du symbole dans le cache");
    // un nouveau demarrage : toujours bloque (l'erreur reste dite)
    const auto r2 = b.build(pl::Mode::Start);
    check(!r2.ok && r2.errors > 0, "toujours bloque tant que l'instance cite un symbole absent");
}

// echec de compilation ; correction apres echec
void echecEtCorrection() {
    std::printf("-- echec de compilation, puis correction\n");
    Bench b;
    fill(b);
    (void)b.build(pl::Mode::Start);
    const std::string init = key(pl::ElementKind::Script, b.scriptInit);
    b.script(b.scriptInit).body = "Debit := Moyenne(1.0, 3.0);\nCompteur := Compteur + ;";
    pl::Progress fin;
    const auto r = b.build(pl::Mode::Start, {}, false, [&fin](const pl::Progress& pr) { fin = pr; });
    check(!r.ok && r.errors > 0, "erreur de syntaxe : le build echoue, la simulation ne demarre pas");
    {
        // la fenetre ne dit plus « a venir » pour F et G : bloques, et pourquoi
        const auto f = static_cast<int>(pl::Phase::Start), g = static_cast<int>(pl::Phase::Restore);
        check(fin.phases[f] == pl::PhaseState::Cancelled && fin.phases[g] == pl::PhaseState::Cancelled && contains(fin.phaseNotes[f], "bloqu"),
              "build en echec : Demarrage et Restauration bloques (" + fin.phaseNotes[f] + "), jamais a venir");
    }
    const pl::Diagnostic* d = nullptr;
    for (const auto& x : r.diagnostics) if (x.element == init && x.blocking()) { d = &x; break; }
    check(d && d->line == 2 && d->step == "Compilation" && d->path == "IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Scripts/Init" && d->script == b.scriptInit,
          "le diagnostic : l'element, son chemin, la ligne 2, l'etape, le script (pour y aller)");
    check(d && !d->date.empty() && !d->file.empty(), "le diagnostic a sa date et son fichier");
    check(b.state(init) == pl::State::CompilationFailed, "Init : Compilation echouee");
    check(b.cache.entries[init].compilation == pl::State::CompilationFailed && !b.cache.entries[init].failedHash.empty(),
          "jamais marque compile si la compilation a echoue (l'empreinte du code en echec est gardee)");
    check(b.state(key(pl::ElementKind::ViewScript, b.scriptCycle)) == pl::State::UpToDate, "les autres restent a jour");
    // relancer sans corriger : toujours bloque, l'erreur redite
    const auto r2 = b.build(pl::Mode::Start);
    check(!r2.ok, "relancer sans corriger : toujours bloque");
    // corriger
    b.script(b.scriptInit).body = "Debit := Moyenne(1.0, 3.0);\nCompteur := Compteur + 1;";
    const auto r3 = b.build(pl::Mode::Start, {}, false, [&fin](const pl::Progress& pr) { fin = pr; });
    check(r3.ok && b.compiled() == std::set<std::string>{init}, "corrige : Init seul est recompile, sans erreur");
    check(fin.phases[static_cast<int>(pl::Phase::Start)] == pl::PhaseState::Done && fin.phases[static_cast<int>(pl::Phase::Restore)] == pl::PhaseState::Skipped,
          "build valide : Demarrage fait (la simulation demarre), rien a restaurer");
    check(b.state(init) == pl::State::UpToDate && b.cache.entries[init].diagnostics.empty(), "corrige : a jour, plus de diagnostic");
    const auto r4 = b.build(pl::Mode::Start);
    check(r4.upToDate, "puis le projet est a jour");
    // 1.11.15 : la phase G dit ce que la remanence de simulation rendra, ou pourquoi rien.
    {
        const auto g = static_cast<int>(pl::Phase::Restore);
        pl::Request rq{pl::Mode::Start, {}, false};
        rq.restore = "donn\xC3\xA9" "es du 2026-10-08 22:10:05 : rendues au d\xC3\xA9marrage";
        pl::Progress seen;
        (void)pl::run(b.p, b.api, rq, b.options(), [&seen](const pl::Progress& pr) { seen = pr; });
        check(seen.phases[g] == pl::PhaseState::Done && contains(seen.phaseNotes[g], "2026-10-08 22:10:05"),
              "1.11.15 : un instantane a rendre - la phase G le dit (" + seen.phaseNotes[g] + ")");
        pl::Request off{pl::Mode::Start, {}, false};
        off.restoreOff = true;
        (void)pl::run(b.p, b.api, off, b.options(), [&seen](const pl::Progress& pr) { seen = pr; });
        check(seen.phases[g] == pl::PhaseState::Skipped && contains(seen.phaseNotes[g], "sactiv"),
              "1.11.15 : la remanence decochee - la phase G le dit (" + seen.phaseNotes[g] + ")");
    }
}

// Generer seul, Compiler seul ; regeneration d'un element, d'une branche, de tout
void commandes() {
    std::printf("-- Generer, Compiler, Regenerer (element, branche, tout)\n");
    Bench b;
    fill(b);
    const auto g = b.build(pl::Mode::Generate);
    check(g.ok && g.compiled == 0 && g.generated > 0, "Generer : tout est genere, rien n'est compile");
    check(b.state(key(pl::ElementKind::Script, b.scriptInit)) == pl::State::CompilationRequired, "un script genere non compile : Compilation requise");
    check(b.state(key(pl::ElementKind::View, b.vueAide)) == pl::State::UpToDate, "une vue (sans code) generee : a jour");
    const auto g2 = b.build(pl::Mode::Generate);
    check(g2.tasks == 0 && !g2.upToDate, "Generer a nouveau : rien a generer, mais il reste a compiler (pas \"a jour\")");
    const auto c = b.build(pl::Mode::Compile);
    check(c.ok && c.generated == 0 && c.compiled > 0, "Compiler : compile sans regenerer");
    check(b.state(key(pl::ElementKind::Script, b.scriptInit)) == pl::State::UpToDate, "puis a jour");
    // Compiler un element a jour, choisi : il est recompile
    const std::string init = key(pl::ElementKind::Script, b.scriptInit);
    (void)b.build(pl::Mode::Compile, {init}, true);
    check(b.compiled() == std::set<std::string>{init} && b.generated().empty(), "Compiler un script a jour (choisi) : recompile, pas regenere");
    // Compiler un script modifie : genere puis compile
    b.script(b.scriptInit).body += "\nMode := 'X';";
    (void)b.build(pl::Mode::Compile, {init}, true);
    check(b.generated() == std::set<std::string>{init} && b.compiled() == std::set<std::string>{init}, "Compiler un script modifie : il est d'abord genere");
    // Regenerer un element : lui, meme a jour
    const std::string mes = key(pl::ElementKind::View, b.vueMes);
    (void)b.build(pl::Mode::Regenerate, {mes}, true);
    check(b.generated() == std::set<std::string>{mes} && b.compiled().empty(), "Regenerer Vue_Mesures : elle seule, sans compiler");
    // Regenerer une branche : tout IHM/Vues (vues, scripts de vue, animations, actions)
    (void)b.build(pl::Mode::RegenerateCompile, {"IHM/Vues"});
    bool onlyViews = !b.generated().empty();
    for (const auto& k : b.generated()) onlyViews = onlyViews && contains(b.analysis().element(k)->path, "IHM/Vues/");
    check(onlyViews && b.generated().count(key(pl::ElementKind::ViewScript, b.scriptCycle)) && b.generated().count(key(pl::ElementKind::Animations, b.vueAide)),
          "Regenerer et compiler IHM/Vues : toute la branche, rien d'autre (" + std::to_string(b.generated().size()) + " elements)");
    check(b.compiled().count(key(pl::ElementKind::ViewScript, b.scriptCycle)) && !b.compiled().count(init), "les scripts de la branche sont recompiles, pas Init");
    // Regenerer tout
    const auto all = b.build(pl::Mode::RegenerateCompile);
    check(all.ok && all.generated == pl::collect(b.p, b.api).size(), "Regenerer et compiler tout : tous les elements");
    // une dependance non generee hors de la selection est faite d'abord
    b.p.programs.functions[0].body = "VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := a;";
    b.script(b.scriptInit).body += "\nCompteur := 1;";
    (void)b.build(pl::Mode::GenerateCompile, {init}, true);
    check(b.generated().count(init) && b.generated().count(key(pl::ElementKind::Function, b.fnMoyenne)),
          "Generer et compiler Init : la fonction qu'il appelle, modifiee, est faite avec");
    // Nettoyer
    const auto cl = b.build(pl::Mode::Clean, {"IHM/Vues"});
    bool left = false;
    for (const auto& [rel, t] : b.artifacts) left = left || contains(rel, "-Vue_Mesures.txt");
    check(cl.ok && !left, "Nettoyer IHM/Vues : leurs artefacts sont supprimes");
    check(b.state(key(pl::ElementKind::View, b.vueMes)) == pl::State::NotGenerated, "puis Vue_Mesures : Non generee");
}

// cache absent, cache corrompu, artefact supprime, version du generateur (sur le disque)
void cacheEtArtefacts(const std::string& tmp) {
    std::printf("-- cache absent, corrompu, artefact supprime (disque)\n");
    Bench b;
    fill(b);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    b.folder = (fs::path(tmp) / ".xpg" / "build").string();
    const auto r1 = b.build(pl::Mode::Start);
    check(r1.ok && fs::exists(fs::path(b.folder) / "build-cache.txt"), "premier build : build-cache.txt ecrit");
    std::string text;
    check(core::readFileAll(fs::path(b.folder) / "build-cache.txt", text) && contains(text, "cache_build format=1") && text.substr(text.size() - 4) == "fin\n",
          "le cache : en-tete, format, derniere ligne \"fin\"");
    check(!fs::exists(fs::path(b.folder) / "build-cache.txt.tmp"), "pas de .tmp qui traine");
    const auto r2 = b.build(pl::Mode::Start);
    check(r2.upToDate, "relu du disque : a jour");
    check(fs::exists(fs::path(b.folder) / "build-cache.txt.bak") || r2.tasks == 0, "l'ancien cache est garde en .bak a chaque ecriture");
    // cache absent
    fs::remove(fs::path(b.folder) / "build-cache.txt", ec);
    const auto r3 = b.build(pl::Mode::Start);
    check(r3.ok && contains(r3.fullReason, "cache absent") && r3.generated == pl::collect(b.p, b.api).size(), "cache absent : regeneration complete, raison dite");
    bool said = false;
    for (const auto& l : r3.log) said = said || (l.severity == pl::Severity::Warning && contains(l.message, "R\xC3\xA9g\xC3\xA9n\xC3\xA9ration compl\xC3\xA8te : cache absent"));
    check(said, "le journal dit la regeneration complete et sa raison");
    // cache coupe (pas de "fin")
    {
        std::string t;
        (void)core::readFileAll(fs::path(b.folder) / "build-cache.txt", t);
        std::ofstream(fs::path(b.folder) / "build-cache.txt", std::ios::binary | std::ios::trunc) << t.substr(0, t.size() / 2);
    }
    const auto r4 = b.build(pl::Mode::Start);
    check(r4.ok && contains(r4.fullReason, "cache illisible") && r4.generated == pl::collect(b.p, b.api).size(), "cache coupe : regeneration complete (" + r4.fullReason + ")");
    // cache illisible (une ligne cassee)
    {
        std::string t;
        (void)core::readFileAll(fs::path(b.folder) / "build-cache.txt", t);
        std::ofstream(fs::path(b.folder) / "build-cache.txt", std::ios::binary | std::ios::trunc) << "cache_build format=1 generateur=\"1\n" << t;
    }
    const auto r5 = b.build(pl::Mode::Start);
    check(r5.ok && contains(r5.fullReason, "cache illisible"), "cache casse : regeneration complete (" + r5.fullReason + ")");
    // cache d'un autre format
    {
        std::string t;
        (void)core::readFileAll(fs::path(b.folder) / "build-cache.txt", t);
        const auto at = t.find("format=1");
        t.replace(at, 8, "format=9");
        std::ofstream(fs::path(b.folder) / "build-cache.txt", std::ios::binary | std::ios::trunc) << t;
    }
    const auto r6 = b.build(pl::Mode::Start);
    check(r6.ok && contains(r6.fullReason, "autre format"), "cache d'un autre format : regeneration complete (" + r6.fullReason + ")");
    // un artefact supprime a la main : lui seul
    const std::string rel = "ihm/08-vues/vue_" + std::to_string(b.vueMes) + "-Vue_Mesures.txt";
    check(fs::exists(fs::path(b.folder) / rel), "l'artefact de Vue_Mesures est sur le disque");
    fs::remove(fs::path(b.folder) / rel, ec);
    check(b.state(key(pl::ElementKind::View, b.vueMes)) == pl::State::GenerationRequired, "artefact supprime : Generation requise");
    (void)b.build(pl::Mode::Start);
    check(b.generated() == std::set<std::string>{key(pl::ElementKind::View, b.vueMes)} && fs::exists(fs::path(b.folder) / rel),
          "artefact supprime : Vue_Mesures seule est regeneree (" + join(b.generated()) + ")");
    // un artefact altere : pareil
    std::ofstream(fs::path(b.folder) / rel, std::ios::binary | std::ios::app) << "ajout a la main\n";
    (void)b.build(pl::Mode::Start);
    check(b.generated() == std::set<std::string>{key(pl::ElementKind::View, b.vueMes)}, "artefact altere : regenere");
    // une entree d'une ancienne version du generateur : obsolete
    {
        pl::Cache c = pl::loadCache(b.folder);
        c.entries[key(pl::ElementKind::View, b.vueAide)].generator = "0";
        (void)pl::saveCache(c, b.folder);
    }
    (void)b.build(pl::Mode::Start);
    check(b.generated() == std::set<std::string>{key(pl::ElementKind::View, b.vueAide)}, "generateur depasse : l'element est regenere");
    // le verrou : un autre processus vivant le tient ; un verrou trop vieux se reprend
    {
        const auto mine = pl::acquireLock(b.folder);
        check(mine.held, "le verrou se prend");
        pl::releaseLock(b.folder);
        std::ofstream(fs::path(b.folder) / "build.lock", std::ios::trunc) << "verrou pid=1 depuis=\"10:38\"\n";
        const auto other = pl::acquireLock(b.folder);
        check(!other.held && contains(other.owner, "PID 1"), "un verrou d'un autre processus bloque (" + other.owner + ")");
        fs::last_write_time(fs::path(b.folder) / "build.lock", fs::file_time_type::clock::now() - std::chrono::minutes(30), ec);
        const auto stale = pl::acquireLock(b.folder);
        check(stale.held, "un verrou de plus de 10 minutes se reprend");
        pl::releaseLock(b.folder);
        check(!fs::exists(fs::path(b.folder) / "build.lock"), "le verrou rendu disparait");
    }
    fs::remove_all(tmp, ec);
}

// un build annule : ce qui est fait reste, le reste attend ; rien de coupe
void annulation() {
    std::printf("-- annulation d'un build\n");
    Bench b;
    fill(b);
    std::atomic<bool> stop{false};
    std::size_t when = 0;
    const auto r = b.build(pl::Mode::Start, {}, false, [&](const pl::Progress& pr) { if (pr.done >= 5 && !stop) { stop = true; when = pr.done; } }, &stop);
    check(r.cancelled && !r.ok, "le build s'arrete a la demande (" + std::to_string(when) + " taches faites)");
    std::size_t gen = 0;
    for (const auto& [k, en] : b.cache.entries) gen += en.generation == pl::State::Generated ? 1 : 0;
    check(gen >= 5 && gen < pl::collect(b.p, b.api).size(), "les elements finis sont gardes, pas les autres (" + std::to_string(gen) + ")");
    bool whole = true;
    for (const auto& [rel, text] : b.artifacts) whole = whole && text.size() > 4 && text.substr(text.size() - 4) == "fin\n";
    check(whole, "aucun artefact a moitie ecrit");
    const auto r2 = b.build(pl::Mode::Start);
    check(r2.ok && r2.generated + gen == pl::collect(b.p, b.api).size(), "le build suivant ne fait que le reste");
}

// une coupure pendant une sauvegarde : l'ancienne version reste entiere
void coupureSauvegarde(const std::string& tmp) {
    std::printf("-- coupure pendant une sauvegarde\n");
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    const fs::path f = fs::path(tmp) / "donnees.txt";
    check(core::writeFileAtomic(f, "version 1\nfin\n").has_value(), "premiere ecriture");
    check(core::writeFileAtomic(f, "version 2\nfin\n").has_value() && fs::exists(f.string() + ".bak"), "deuxieme : l'ancienne en .bak");
    std::string t, bak;
    check(core::readFileAll(f, t) && t == "version 2\nfin\n" && core::readFileAll(f.string() + ".bak", bak) && bak == "version 1\nfin\n", "la cible et la copie de secours");
    // la coupure : un .tmp a moitie ecrit est reste ; la cible est intacte et le prochain ecrit passe
    std::ofstream(f.string() + ".tmp", std::ios::binary | std::ios::trunc) << "version 3\nfi";
    check(core::readFileAll(f, t) && t == "version 2\nfin\n", "apres une coupure (un .tmp coupe) : la cible est entiere");
    check(core::writeFileAtomic(f, "version 3\nfin\n").has_value() && core::readFileAll(f, t) && t == "version 3\nfin\n", "l'ecriture suivante remplace le .tmp coupe");
    check(!fs::exists(f.string() + ".tmp"), "plus de .tmp");
    // un dossier impossible : rien n'est remplace, l'erreur est dite
#ifndef _WIN32
    const fs::path ro = fs::path(tmp) / "lecture_seule";
    fs::create_directories(ro, ec);
    (void)core::writeFileAtomic(ro / "x.txt", "avant\n");
    fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec, ec);
    const auto s = core::writeFileAtomic(ro / "x.txt", "apres\n");
    const bool root = ::getuid() == 0;   // root ecrit partout : l'essai ne dit rien
    if (!root) check(!s.has_value() && contains(s.error().message(), "criture impossible"), "dossier en lecture seule : l'erreur est dite (" + (s.has_value() ? std::string("ok") : s.error().message()) + ")");
    check(core::readFileAll(ro / "x.txt", t) && (t == "avant\n" || root), "et l'ancienne version reste");
    fs::permissions(ro, fs::perms::owner_all, ec);
#endif
    fs::remove_all(tmp, ec);
}

// le cache : son texte se relit a l'identique
void formatDuCache() {
    std::printf("-- format du cache\n");
    Bench b;
    fill(b);
    b.script(b.scriptInit).body = "Debit := Moyenne(1.0, 3.0);\nCompteur := Compteur + ;";
    (void)b.build(pl::Mode::Start);
    const std::string text = pl::serialize(b.last.cache);
    const pl::Cache c = pl::parseCache(text);
    check(c.status == pl::Cache::Status::Ok && c.entries.size() == b.last.cache.entries.size(), "relu : toutes les entrees");
    bool same = true;
    for (const auto& [k, e] : b.last.cache.entries) {
        const auto it = c.entries.find(k);
        same = same && it != c.entries.end() && it->second.contentHash == e.contentHash && it->second.depsHash == e.depsHash && it->second.generation == e.generation
            && it->second.compilation == e.compilation && it->second.depHashes == e.depHashes && it->second.diagnostics.size() == e.diagnostics.size()
            && it->second.artifact == e.artifact && it->second.path == e.path;
    }
    check(same, "relu : empreintes, dependances, etats, diagnostics, artefacts identiques");
    check(pl::serialize(c) == text, "reecrit : le meme texte");
    // un texte a guillemets, retours a la ligne et accents passe
    const auto& init = c.entries.at(key(pl::ElementKind::Script, b.scriptInit));
    check(!init.diagnostics.empty() && init.diagnostics.front().line == 2, "un diagnostic relu garde sa ligne");
}

// 1.11.13 : DEUX FAUSSES ALERTES de Compiler et Generer, trouvees sur Armoire_Gaz (le build les
// rendait bloquantes) : une popup d'un symbole lit les parametres du symbole (1.11.10) ; une
// vue appelle la fonction d'une instance (Vanne_1.Etat(), 1.11.11). Et une vraie erreur reste,
// dite UNE fois (pas aussi par la validation, en d'autres mots).
void faussesAlertes() {
    std::printf("-- les fausses alertes de Compiler et Generer (popup de symbole, instance)\n");
    Bench b;
    fill(b);
    b.p.view(b.symVanne)->functions.push_back(HmiFunction{b.p.allocate(), "Etat", "BOOL", "Etat := V.Ouverte;", {}, false});
    View pop = makeView(b.p, "Pop_Vanne");
    pop.role = "popup";
    pop.ownerSymbol = b.symVanne;
    {
        const Id id = edit::add(b.p, pop, Kind::Text, 10, 10);
        pop.object(id)->name = "Titre";
        pop.object(id)->set("text", "Vanne {V.Position:0.0} %");
        const Id v = edit::add(b.p, pop, Kind::Indicator, 10, 40);
        pop.object(v)->name = "Voyant";
        pop.object(v)->setExpr("value", "V.Ouverte");
    }
    const Id popId = pop.id;
    b.p.views.push_back(pop);
    View* syn = b.p.view(b.vueSyn);
    const Id voyant = edit::add(b.p, *syn, Kind::Indicator, 600, 100);
    syn->object(voyant)->name = "Voyant_Vanne";
    syn->object(voyant)->setExpr("value", "Vanne_1.Etat()");
    const auto names = b.options().plcHasName;
    const auto errorsOf = [&](const std::vector<Issue>& issues, Id view, std::string_view word) {
        int n = 0;
        for (const auto& i : issues)
            if (i.severity == Issue::Severity::Error && (view == kNoId || i.view == view) && i.message.find(word) != std::string::npos) ++n;
        return n;
    };
    const auto comp = compileWith(b.p, names, {});
    check(errorsOf(comp, popId, "") == 0, "Compiler : la popup du symbole lit V (le parametre du symbole) sans erreur");
    check(errorsOf(comp, kNoId, "Vanne_1") == 0, "Compiler : Vanne_1.Etat() dans la vue, sans erreur");
    const auto gen = generateWith(b.p, names, GenerateOptions{});
    check(errorsOf(gen, popId, "inexistante") == 0, "Generer : la popup du symbole, sans \"variable inexistante\"");
    check(errorsOf(gen, kNoId, "Vanne_1") == 0, "Generer : l'instance Vanne_1, sans \"variable inexistante\"");
    const auto r = b.build(pl::Mode::Start);
    check(r.ok, "le build passe (" + std::to_string(r.errors) + " erreur(s))");
    // une vraie erreur : dite une fois, bloquante
    syn->object(voyant)->setExpr("value", "Inconnu_X > 1");
    const auto r2 = b.build(pl::Mode::Start);
    int said = 0;
    for (const auto& d : r2.diagnostics) said += d.blocking() && d.message.find("Inconnu_X") != std::string::npos ? 1 : 0;
    check(!r2.ok && r2.errors == 1 && said == 1, "une vraie erreur : bloquante, dite une seule fois (" + std::to_string(said) + " fois, "
                                                      + std::to_string(r2.errors) + " erreur(s))");
}

// Le gestionnaire de l'application (app/hmi/HmiBuild) : un vrai fil, poll a chaque
// "image", l'analyse apres une modification, un build a la fois, l'annulation.
void gestionnaire(const std::string& tmp) {
    std::printf("-- le gestionnaire de build (fil a part, poll, analyse, refus, annulation)\n");
    Bench b;
    fill(b);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    std::string folder;   // vide d'abord : le cache en memoire
    app::HmiBuildManager m([&]() -> std::optional<app::HmiBuildSetup> {
        app::HmiBuildSetup s;
        s.project = std::make_shared<const Project>(b.p);
        s.api = b.api;
        s.projectFolder = folder;
        s.plcHasName = b.options().plcHasName;
        s.plcPaths = b.options().plcPaths;
        return s;
    });
    int statusSignals = 0, finishedSignals = 0;
    std::shared_ptr<const pl::Report> got;
    core::ConnectionScope links;
    links += m.statusChanged->connect([&] { ++statusSignals; });
    links += m.finished->connect([&](std::shared_ptr<const pl::Report> r) { ++finishedSignals; got = r; });
    const auto pump = [&](double seconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < end) {
            (void)m.poll();
            if (!m.busy() && !m.building()) {
                (void)m.poll();
                if (!m.busy()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    // l'analyse de depart (sans rien construire)
    m.analyseNow();
    pump(10);
    pump(0.05);
    auto st = m.status();
    check(st && st->totals.elements >= 25 && st->totals.notGenerated == st->totals.elements, "analyse : tout est Non genere au depart (" + st->headline() + ")");
    check(statusSignals >= 1, "le signal statusChanged part");
    // un build : pendant ce temps, un second est refuse
    std::string why;
    check(m.start(pl::Request{pl::Mode::Start, {}, false}, &why), "un build part");
    check(m.building(), "il tourne");
    check(!m.start(pl::Request{pl::Mode::Start, {}, false}, &why) && contains(why, "en cours"), "un second build est refuse (" + why + ")");
    pump(20);
    check(!m.building() && finishedSignals == 1 && got && got->ok, "le build finit, le signal finished part avec le rapport");
    st = m.status();
    check(st && st->upToDate() && contains(st->headline(), "Projet \xC3\xA0 jour"), "apres le build : projet a jour (" + (st ? st->headline() : std::string("?")) + ")");
    const auto* fold = st ? st->folder("IHM/Vues") : nullptr;
    check(fold && fold->elements > 0 && fold->state == pl::State::UpToDate, "le dossier IHM/Vues est resume (a jour)");
    // une modification : l'analyse suit (300 ms apres), l'element est Modifie, son dossier aussi
    b.script(b.scriptInit).body += "\nCompteur := 2;";
    m.invalidate();
    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    pump(10);
    pump(0.05);
    st = m.status();
    const auto* sh = st ? st->of(key(pl::ElementKind::Script, b.scriptInit)) : nullptr;
    check(sh && sh->state == pl::State::Modified, "un script modifie : Modifie, sans rien construire");
    const auto* scripts = st ? st->folder("IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Scripts") : nullptr;
    check(scripts && scripts->state == pl::State::Modified && scripts->pending == 1, "son dossier dit 1 element a refaire");
    check(app::hmiStateLook(pl::State::Modified).glyph == "\xE2\x97\x8F" && app::hmiStateLook(pl::State::UpToDate).glyph == "\xE2\x9C\x93"
              && app::hmiStateLook(pl::State::InvalidDependency).glyph == "\xE2\x9B\x93" && app::hmiStateLook(pl::State::Obsolete).glyph == "\xE2\x8F\xB1",
          "les glyphes des etats");
    // LA COURSE de la 1.11.13 (corrigee avant livraison) : une analyse tourne, une nouvelle
    // modification la rend deja perimee, et Demarrer arrive - le build attend l'analyse et
    // part sur le meme fil, sans en relancer une (avant : un std::thread ecrase, std::terminate).
    m.analyseNow();
    (void)m.poll();                       // l'analyse part
    check(m.busy() && !m.building(), "une analyse tourne");
    m.analyseNow();                       // ... et une modification la rend perimee
    check(m.start(pl::Request{pl::Mode::Start, {}, false}, &why), "Demarrer pendant l'analyse : le build part (" + why + ")");
    check(m.building(), "et c'est bien le build qui tourne");
    pump(20);
    check(got && got->ok && got->generated == 1 && got->compiled == 1 && m.status()->upToDate(), "seul le script est refait, puis a jour");
    // relancer : rien
    check(m.start(pl::Request{pl::Mode::Start, {}, false}, &why), "le build suivant part");
    pump(20);
    check(got && got->upToDate && got->generated == 0 && m.status()->upToDate(), "rien n'a change : projet a jour");
    // sur le disque : le cache et le verrou
    folder = (fs::path(tmp) / "projet").string();
    fs::create_directories(folder, ec);
    check(m.start(pl::Request{pl::Mode::Start, {}, false}, &why), "un build sur le disque part");
    pump(20);
    check(got && got->ok && fs::exists(fs::path(pl::buildFolderOf(folder)) / "build-cache.txt") && !fs::exists(fs::path(pl::buildFolderOf(folder)) / "build.lock"),
          "le cache est ecrit, le verrou rendu");
    // annuler : le build s'arrete entre deux taches
    check(m.start(pl::Request{pl::Mode::RegenerateCompile, {}, false}, &why), "Regenerer et compiler part");
    m.cancel();
    pump(20);
    check(got && (got->cancelled || got->ok), "annule (ou deja fini) : le rapport le dit");
    fs::remove_all(tmp, ec);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    const std::string tmp = (fs::temp_directory_path() / ("xpg_build_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    (void)argv;
    collecte();
    premiereGenerationEtDemarrageSansModification();
    unSeulScriptEtUneVue();
    variableApi();
    fonctionIhm();
    ihmLogInvalide();               // 1.11.16
    arretSurModification();         // 1.11.16
    suppressionSymbole();
    echecEtCorrection();
    commandes();
    cacheEtArtefacts(tmp + "_cache");
    annulation();
    coupureSauvegarde(tmp + "_coupure");
    formatDuCache();
    faussesAlertes();
    gestionnaire(tmp + "_gestionnaire");
    std::printf("\n%d verification(s), %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
