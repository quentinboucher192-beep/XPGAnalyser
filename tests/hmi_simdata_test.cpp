// =============================================================================
//  tests/hmi_simdata_test.cpp - 1.11.15 : la remanence de simulation
// -----------------------------------------------------------------------------
//  L'instantane (son format, un fichier coupe ou abime refuse), la prise des
//  variables IHM (simples, structures, tableaux, enumerations ; les liees non),
//  le retour (par identifiant stable : un renommage, un changement d'ordre ;
//  une variable supprimee ignoree, une nouvelle a sa valeur initiale ; les
//  types compatibles convertis, les autres refuses avec un avertissement) et
//  le moteur : les valeurs rendues avant les scripts de Demarrage.
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiSimData.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

class FakePlc final : public sim::Environment {
public:
    std::map<std::string, sim::Value> values;
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        it->second = v;
        return true;
    }
    bool exists(std::string_view n) override { return values.count(std::string(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

Variable variable(Project& p, std::string name, std::string type, std::string initial = {}) {
    Variable v;
    v.id = p.allocate();
    v.name = std::move(name);
    v.type = std::move(type);
    v.initial = std::move(initial);
    return v;
}

Script script(Project& p, std::string name, std::string event, std::string body) {
    Script sc;
    sc.id = p.allocate();
    sc.name = std::move(name);
    sc.event = std::move(event);
    sc.body = std::move(body);
    sc.lang = ScriptLang::ST;
    return sc;
}

HmiType structure(Project& p, std::string name, std::vector<std::pair<std::string, std::string>> members) {
    HmiType t;
    t.id = p.allocate();
    t.name = std::move(name);
    t.kind = HmiTypeKind::Structure;
    for (auto& [n, ty] : members) {
        TypeMember m;
        m.name = n;
        m.type = ty;
        t.members.push_back(m);
    }
    return t;
}

HmiType enumeration(Project& p, std::string name, std::vector<std::pair<std::string, std::int64_t>> values) {
    HmiType t;
    t.id = p.allocate();
    t.name = std::move(name);
    t.kind = HmiTypeKind::Enumeration;
    for (auto& [n, v] : values) t.values.push_back(HmiEnumValue{n, v, {}, {}});
    return t;
}

// Les cases d'un projet, par leur nom complet (la "memoire" d'un faux moteur).
struct Memory {
    std::map<std::string, sim::Value> cells;
    simdata::Reader reader() {
        return [this](const std::string& path) -> const sim::Value* {
            const auto it = cells.find(path);
            return it == cells.end() ? nullptr : &it->second;
        };
    }
    simdata::Slot slot() {
        return [this](const std::string& path) -> sim::Value* {
            const auto it = cells.find(path);
            return it == cells.end() ? nullptr : &it->second;
        };
    }
};

const simdata::Cell* cellOf(const std::vector<simdata::Cell>& cells, Id id, std::string_view path) {
    for (const auto& c : cells)
        if (c.variable == id && c.path == path) return &c;
    return nullptr;
}

void format() {
    std::printf("-- l'instantane : le format, un fichier coupe ou abime\n");
    simdata::Snapshot s;
    s.date = "2026-10-08 21:12:30";
    s.session = 3;
    s.cells.push_back({12, "Marche", "BOOL", "", sim::Value::boolean(true)});
    s.cells.push_back({13, "Compteur", "INT", "", sim::Value::integer(sim::Type::Int, -42)});
    s.cells.push_back({14, "Mesure", "REAL", "", sim::Value::real(21.537)});
    s.cells.push_back({15, "Duree", "TIME", "", sim::Value::time(90500)});
    s.cells.push_back({16, "Message", "STRING", "", sim::Value::text("Ligne \"A\"\nsuite = 2 # fin")});
    s.cells.push_back({17, "Four1", "T_Four", ".Temperature", sim::Value::real(812.25)});
    s.cells.push_back({18, "Consignes", "ARRAY[1..3] OF DINT", "[2]", sim::Value::integer(sim::Type::DInt, 100000)});
    simdata::TwinMemory t;
    t.equipment = 40;
    t.name = "Balance B";
    t.memory.push_back(SavedMemory{MemTable::Holding, 0, {1, 2, 65535}});
    t.memory.push_back(SavedMemory{MemTable::Coils, 5, {1, 0, 1}});
    s.twins.push_back(t);
    s.twins.push_back(simdata::TwinMemory{41, "Esclave vide", {}});   // une memoire toute a zero
    const std::string text = simdata::serialize(s);
    check(text.rfind(std::string(simdata::kHeader) + "\n", 0) == 0, "la premiere ligne dit ce qu'est le fichier");
    simdata::Snapshot back;
    std::string why;
    check(simdata::parse(text, back, &why), "relu (" + why + ")");
    check(back.date == s.date && back.session == 3 && back.cells.size() == 7 && back.twins.size() == 2, "la prise, les cases, les deux jumeaux");
    check(back.twins[1].equipment == 41 && back.twins[1].name == "Esclave vide" && back.twins[1].memory.empty(),
          "un esclave a la memoire toute a zero est garde aussi (il sera remis a zero)");
    bool same = back.cells.size() == s.cells.size();
    for (std::size_t i = 0; same && i < s.cells.size(); ++i)
        same = back.cells[i].variable == s.cells[i].variable && back.cells[i].name == s.cells[i].name && back.cells[i].declared == s.cells[i].declared
            && back.cells[i].path == s.cells[i].path && back.cells[i].value.type() == s.cells[i].value.type() && back.cells[i].value.equals(s.cells[i].value);
    check(same, "chaque case revient a l'identique (BOOL, INT negatif, REAL, TIME, STRING avec guillemets et retour, membre, case de tableau)");
    check(back.cells[2].value.asReal() == 21.537, "un REAL garde tous ses chiffres");
    check(back.twins[0].equipment == 40 && back.twins[0].memory.size() == 2 && back.twins[0].memory[0] == s.twins[0].memory[0]
              && back.twins[0].memory[1] == s.twins[0].memory[1],
          "la memoire de l'esclave simule : ses deux plages");

    // Coupe : la ligne de fin manque.
    const auto cut = text.substr(0, text.find("fin "));
    check(!simdata::parse(cut, back, &why) && why.find("coup") != std::string::npos && back.empty(), "un fichier coupe est refuse (" + why + ")");
    // La fin ne compte pas les memes lignes : une case a disparu.
    std::string missing = text;
    missing.erase(missing.find("case variable=13"), missing.find('\n', missing.find("case variable=13")) - missing.find("case variable=13") + 1);
    check(!simdata::parse(missing, back, &why) && why.find("incomplet") != std::string::npos, "une ligne perdue est vue (" + why + ")");
    // Une valeur abimee.
    std::string bad = text;
    bad.replace(bad.find("valeur=\"-42\""), 12, "valeur=\"x42\"");
    check(!simdata::parse(bad, back, &why) && why.find("illisible") != std::string::npos, "une valeur abimee est refusee (" + why + ")");
    check(!simdata::parse("bonjour\n", back, &why) && !simdata::parse("", back, &why), "un autre fichier, un fichier vide : refuses");
}

void conversions() {
    std::printf("-- les conversions : seulement les types compatibles\n");
    using sim::Type;
    const auto i = [](Type t, long long v) { return sim::Value::integer(t, v); };
    check(simdata::convert(i(Type::Int, -5), Type::DInt).has_value() && simdata::convert(i(Type::Int, -5), Type::DInt)->asInteger() == -5,
          "INT -> DINT");
    check(simdata::convert(i(Type::DInt, 1000), Type::Int).has_value(), "DINT -> INT : 1000 y tient");
    check(!simdata::convert(i(Type::DInt, 100000), Type::Int), "DINT -> INT : 100000 n'y tient pas (rien de tronque)");
    check(!simdata::convert(i(Type::Int, -1), Type::UInt), "INT -> UINT : -1 n'y tient pas");
    check(simdata::convert(i(Type::Int, 7), Type::Real) && simdata::convert(i(Type::Int, 7), Type::Real)->asReal() == 7.0, "INT -> REAL");
    check(!simdata::convert(sim::Value::real(2.5), Type::Int), "REAL -> INT : refuse");
    check(!simdata::convert(sim::Value::boolean(true), Type::Int) && !simdata::convert(i(Type::Int, 1), Type::Bool), "BOOL <-> INT : refuses");
    check(!simdata::convert(sim::Value::text("12"), Type::Int) && !simdata::convert(i(Type::Int, 12), Type::String), "STRING <-> INT : refuses");
    check(!simdata::convert(sim::Value::time(1000), Type::DInt), "TIME -> DINT : refuse");
}

void captureEtRetour() {
    std::printf("-- la prise et le retour : par identifiant stable\n");
    Project p;
    p.programs.types.push_back(structure(p, "T_Four", {{"Temperature", "REAL"}, {"Marche", "BOOL"}}));
    p.programs.types.push_back(enumeration(p, "T_MODE", {{"Arret", 0}, {"Auto", 1}, {"Manuel", 2}}));
    p.programs.variables.push_back(variable(p, "Compteur", "INT", "0"));
    p.programs.variables.push_back(variable(p, "Mesure", "REAL", "0"));
    p.programs.variables.push_back(variable(p, "Four1", "T_Four"));
    p.programs.variables.push_back(variable(p, "Consignes", "ARRAY[1..3] OF INT", "0"));
    p.programs.variables.push_back(variable(p, "Mode", "T_MODE", "Arret"));
    Variable liee = variable(p, "Pression", "REAL", "0");
    liee.equipment = "Balance B";
    liee.address = "40001";
    p.programs.variables.push_back(liee);
    p.programs.variables.push_back(variable(p, "Etat", "BOOL", "FALSE"));
    const Id compteur = p.programs.variables[0].id, mesure = p.programs.variables[1].id, four = p.programs.variables[2].id,
             consignes = p.programs.variables[3].id, mode = p.programs.variables[4].id, etat = p.programs.variables[6].id;

    Memory m;
    m.cells["Compteur"] = sim::Value::integer(sim::Type::Int, 41);
    m.cells["Mesure"] = sim::Value::real(3.5);
    m.cells["Four1.Temperature"] = sim::Value::real(812.0);
    m.cells["Four1.Marche"] = sim::Value::boolean(true);
    m.cells["Consignes[1]"] = sim::Value::integer(sim::Type::Int, 10);
    m.cells["Consignes[2]"] = sim::Value::integer(sim::Type::Int, 20);
    m.cells["Consignes[3]"] = sim::Value::integer(sim::Type::Int, 30);
    m.cells["Mode"] = sim::Value::integer(sim::Type::DInt, 2);
    m.cells["Pression"] = sim::Value::real(4.2);
    m.cells["Etat"] = sim::Value::boolean(true);
    const auto cells = simdata::captureVariables(p, m.reader());
    check(cells.size() == 9, "9 cases : 2 simples, 2 membres, 3 cases de tableau, l'enumeration, le BOOL (" + std::to_string(cells.size()) + ")");
    check(cellOf(cells, compteur, "") && cellOf(cells, compteur, "")->value.asInteger() == 41 && cellOf(cells, compteur, "")->name == "Compteur",
          "une variable simple : son identifiant, son nom, un chemin vide");
    check(!cellOf(cells, liee.id, ""), "une variable liee a un equipement n'est pas prise (sa valeur est dans son esclave)");
    check(cellOf(cells, four, ".Temperature") && cellOf(cells, four, ".Temperature")->value.asReal() == 812.0, "un membre : son chemin dans la variable");
    check(cellOf(cells, consignes, "[2]") && cellOf(cells, consignes, "[2]")->value.asInteger() == 20, "une case de tableau : son indice");
    check(cellOf(cells, mode, "") && cellOf(cells, mode, "")->declared == "T_MODE", "une enumeration : son nombre, son type declare");

    // Le meme projet : tout revient.
    {
        Memory fresh;
        fresh.cells = {{"Compteur", sim::Value::integer(sim::Type::Int, 0)}, {"Mesure", sim::Value::real(0)}, {"Four1.Temperature", sim::Value::real(0)},
                       {"Four1.Marche", sim::Value::boolean(false)}, {"Consignes[1]", sim::Value::integer(sim::Type::Int, 0)},
                       {"Consignes[2]", sim::Value::integer(sim::Type::Int, 0)}, {"Consignes[3]", sim::Value::integer(sim::Type::Int, 0)},
                       {"Mode", sim::Value::integer(sim::Type::DInt, 0)}, {"Pression", sim::Value::real(0)}, {"Etat", sim::Value::boolean(false)}};
        const auto r = simdata::restoreVariables(p, cells, fresh.slot());
        check(r.restored == 9 && r.converted == 0 && r.removed == 0 && r.incompatible == 0 && r.fresh == 0, "le meme projet : 9 valeurs rendues (" + r.summary() + ")");
        check(fresh.cells["Compteur"].asInteger() == 41 && fresh.cells["Four1.Temperature"].asReal() == 812.0 && fresh.cells["Consignes[3]"].asInteger() == 30
                  && fresh.cells["Mode"].asInteger() == 2 && fresh.cells["Etat"].isTruthy(),
              "les valeurs, a leur place");
        check(fresh.cells["Pression"].asReal() == 0.0, "la variable liee garde sa valeur (celle de l'equipement)");
    }

    // Le projet change : un renommage, un nouvel ordre, une suppression, un ajout, des types qui changent.
    Project q = p;
    q.programs.variables[0].name = "Compteur_Pieces";                       // renommee
    q.programs.variables[0].type = "DINT";                                   // INT -> DINT : convertie
    std::swap(q.programs.variables[1], q.programs.variables[3]);             // l'ordre change (Mesure <-> Consignes)
    for (auto& v : q.programs.variables) if (v.id == mesure) v.type = "BOOL";  // REAL -> BOOL : incompatible
    q.programs.variables.erase(std::remove_if(q.programs.variables.begin(), q.programs.variables.end(), [etat](const Variable& v) { return v.id == etat; }),
                               q.programs.variables.end());                  // supprimee
    q.programs.variables.push_back(variable(q, "Nouvelle", "INT", "5"));     // nouvelle
    q.programs.types[0].members.push_back(TypeMember{});                  // T_Four : un membre de plus
    q.programs.types[0].members.back().name = "Debit";
    q.programs.types[0].members.back().type = "REAL";
    q.programs.types[0].members.erase(q.programs.types[0].members.begin() + 1);   // ... et Marche retire
    q.programs.types[1].values.pop_back();                                   // T_MODE : Manuel (2) n'existe plus
    Memory after;
    after.cells = {{"Compteur_Pieces", sim::Value::integer(sim::Type::DInt, 0)}, {"Mesure", sim::Value::boolean(false)},
                   {"Four1.Temperature", sim::Value::real(0)}, {"Four1.Debit", sim::Value::real(0)},
                   {"Consignes[1]", sim::Value::integer(sim::Type::Int, 0)}, {"Consignes[2]", sim::Value::integer(sim::Type::Int, 0)},
                   {"Consignes[3]", sim::Value::integer(sim::Type::Int, 0)}, {"Mode", sim::Value::integer(sim::Type::DInt, 0)},
                   {"Pression", sim::Value::real(0)}, {"Nouvelle", sim::Value::integer(sim::Type::Int, 5)}};
    const auto r = simdata::restoreVariables(q, cells, after.slot());
    check(after.cells["Compteur_Pieces"].asInteger() == 41 && after.cells["Compteur_Pieces"].type() == sim::Type::DInt,
          "renommee et passee en DINT : sa valeur suit son identifiant, convertie");
    check(r.converted == 1, "une conversion comptee (" + r.summary() + ")");
    check(!after.cells["Mesure"].isTruthy() && r.incompatible >= 1, "REAL devenu BOOL : la valeur initiale, un avertissement");
    bool warned = false;
    for (const auto& w : r.warnings) warned = warned || (w.find("Mesure") != std::string::npos && w.find("incompatible") != std::string::npos);
    check(warned, "l'avertissement nomme la variable et dit incompatible");
    check(after.cells["Consignes[1]"].asInteger() == 10 && after.cells["Consignes[3]"].asInteger() == 30, "l'ordre change : chaque valeur reste a sa variable");
    check(r.removed == 1, "la variable supprimee : ignoree, comptee");
    check(r.fresh == 1 && after.cells["Nouvelle"].asInteger() == 5, "la nouvelle variable garde sa valeur initiale");
    check(after.cells["Four1.Temperature"].asReal() == 812.0 && after.cells["Four1.Debit"].asReal() == 0.0 && r.dropped == 1,
          "la structure change : le membre reste rendu, le nouveau a sa valeur initiale, le retire ignore");
    check(after.cells["Mode"].asInteger() == 0, "l'enumeration a perdu la valeur 2 : la valeur initiale");
    // Un type simple devenu une structure : refuse en bloc.
    Project s = p;
    s.programs.variables[0].type = "T_Four";
    Memory st;
    st.cells = {{"Compteur.Temperature", sim::Value::real(0)}, {"Compteur.Marche", sim::Value::boolean(false)}};
    const auto rs = simdata::restoreVariables(s, cells, st.slot());
    check(rs.incompatible >= 1 && st.cells["Compteur.Temperature"].asReal() == 0.0, "INT devenu une structure : incompatible, rien n'est verse dans ses membres");
}

void moteur() {
    std::printf("-- le moteur : les valeurs rendues avant les scripts de Demarrage\n");
    Project p;
    p.programs.variables.push_back(variable(p, "Compteur", "INT", "0"));
    p.programs.variables.push_back(variable(p, "Copie", "INT", "0"));
    p.programs.variables.push_back(variable(p, "Texte", "STRING", "'depart'"));
    p.programs.scripts.push_back(script(p, "Init", "Demarrage", "Copie := Compteur;"));
    p.programs.scripts.push_back(script(p, "Plus", "Appel", "Compteur := Compteur + 1;\nTexte := 'change';"));
    View v = makeView(p, "Vue");
    p.views = {v};
    p.config.startView = v.id;
    FakePlc plc;
    Runtime rt;
    rt.bind(&p, &plc);
    rt.start(0.0);
    std::string why;
    for (int k = 0; k < 3; ++k) (void)rt.callScript("Plus", 1.0 + k, &why);
    check(rt.variable("Compteur") && rt.variable("Compteur")->asInteger() == 3, "trois appels : Compteur = 3");
    const auto cells = rt.captureData();
    check(cells.size() == 3, "la prise : 3 cases");
    rt.stop(5.0, "essai");
    // Sans instantane : les valeurs initiales.
    rt.start(6.0);
    check(rt.variable("Compteur")->asInteger() == 0 && !rt.lastRestore(), "un demarrage sans instantane : les valeurs initiales, rien de rendu");
    rt.stop(7.0);
    // Avec : rendues, et le script de Demarrage les voit deja.
    rt.setStartData(cells);
    rt.start(8.0);
    check(rt.variable("Compteur")->asInteger() == 3 && rt.variable("Texte")->asString() == "change", "les valeurs rendues");
    check(rt.variable("Copie")->asInteger() == 3, "le script de Demarrage lit la valeur rendue (rendue avant lui)");
    check(rt.lastRestore() && rt.lastRestore()->restored == 3, "ce qui a ete fait : 3 valeurs rendues");
    bool said = false;
    for (const auto& e : rt.journal()) said = said || e.message.find("3 valeurs rendues") != std::string::npos;
    check(said, "le journal le dit (la Console)");
    rt.stop(9.0);
    // L'instantane ne sert qu'une fois : le demarrage suivant repart des valeurs initiales.
    rt.start(10.0);
    check(rt.variable("Compteur")->asInteger() == 0, "rendu une fois : le demarrage d'apres repart des valeurs initiales");
}

} // namespace

int main() {
    format();
    conversions();
    captureEtRetour();
    moteur();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
