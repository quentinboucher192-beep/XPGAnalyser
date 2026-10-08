// =============================================================================
//  tests/hmi_retain_test.cpp - 1.11.16 : la remanence d'exploitation
// -----------------------------------------------------------------------------
//  Le stockage du poste (son format, un fichier coupe ou abime refuse, la copie
//  .bak reprise), la prise des seules variables cochees « Remanente » (jamais une
//  liee), la fusion datee par valeur, l'etat d'une variable pour l'editeur (a
//  jour, type change converti ou incompatible, non remanente, jamais
//  sauvegardee), reinitialiser, verifier l'integrite, l'echange avec Excel (CSV)
//  et l'import, la case dans le fichier du projet, et le moteur : les valeurs
//  rendues au lancement suivant, avant les scripts de Demarrage.
// =============================================================================
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRetain.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiSimData.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace hmi;
namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

class FakePlc final : public sim::Environment {
public:
    bool read(std::string_view, sim::Value&) override { return false; }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view) override { return false; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

Variable variable(Project& p, std::string name, std::string type, std::string initial = {}, bool retain = false) {
    Variable v;
    v.id = p.allocate();
    v.name = std::move(name);
    v.type = std::move(type);
    v.initial = std::move(initial);
    v.retain = retain;
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

simdata::Cell cell(const Variable& v, std::string path, sim::Value value) {
    simdata::Cell c;
    c.variable = v.id;
    c.name = v.name;
    c.declared = v.type;
    c.path = std::move(path);
    c.value = std::move(value);
    return c;
}

const retain::Entry* entryOf(const retain::Store& s, Id id, std::string_view path = {}) { return s.find(id, path); }

// Un projet d'essai : deux remanentes (INT, REAL), une non remanente, une liee cochee,
// une structure remanente.
struct Fixture {
    Project p;
    Id compteur{}, consigne{}, libre{}, lie{}, four{};
    Fixture() {
        p.programs.types.push_back(structure(p, "T_Four", {{"Temp", "REAL"}, {"Marche", "BOOL"}}));
        p.programs.variables.push_back(variable(p, "Compteur", "INT", "0", true));
        p.programs.variables.push_back(variable(p, "Consigne", "REAL", "20.5", true));
        p.programs.variables.push_back(variable(p, "Libre", "INT", "0", false));
        auto l = variable(p, "Lie", "INT", "0", true);
        l.equipment = "API";
        l.address = "%MW10";
        p.programs.variables.push_back(l);
        p.programs.variables.push_back(variable(p, "Four", "T_Four", {}, true));
        compteur = p.programs.variables[0].id;
        consigne = p.programs.variables[1].id;
        libre = p.programs.variables[2].id;
        lie = p.programs.variables[3].id;
        four = p.programs.variables[4].id;
    }
    Variable& var(Id id) {
        for (auto& v : p.programs.variables)
            if (v.id == id) return v;
        return p.programs.variables.front();
    }
};

void format() {
    std::printf("-- le format du stockage : relu tel quel ; coupe, abime ou etranger : refuse\n");
    Fixture f;
    retain::Store s;
    s.project = "Essai";
    s.date = "2026-10-08 21:10:05";
    s.entries.push_back({cell(f.var(f.compteur), "", sim::Value::integer(sim::Type::Int, 7)), "2026-10-08 21:10:00"});
    s.entries.push_back({cell(f.var(f.consigne), "", sim::Value::real(21.537)), "2026-10-08 21:10:05"});
    s.entries.push_back({cell(f.var(f.four), ".Marche", sim::Value::boolean(true)), "2026-10-08 21:09:00"});
    auto texte = variable(f.p, "Message", "STRING", {}, true);
    s.entries.push_back({cell(texte, "", sim::Value::text("Arr\xC3\xAAt \"urgent\" ; voir")), "2026-10-08 21:08:00"});
    const std::string text = retain::serialize(s);
    check(text.rfind(std::string(retain::kHeader), 0) == 0, "l'en-tete et sa version en premiere ligne");
    retain::Store back;
    std::string why;
    check(retain::parse(text, back, &why), "relu (" + why + ")");
    check(back.project == "Essai" && back.date == s.date && back.entries.size() == 4, "le projet, la date, 4 valeurs");
    const auto* c = entryOf(back, f.compteur);
    check(c && c->cell.value.asInteger() == 7 && c->date == "2026-10-08 21:10:00" && c->cell.declared == "INT", "Compteur : 7, sa date, son type declare");
    const auto* r = entryOf(back, f.consigne);
    check(r && r->cell.value.asReal() == 21.537, "un REAL garde tous ses chiffres");
    const auto* m = entryOf(back, f.four, ".Marche");
    check(m && m->cell.value.isTruthy(), "une case de structure : Four.Marche = TRUE");
    const auto* t = entryOf(back, texte.id);
    check(t && t->cell.value.asString() == "Arr\xC3\xAAt \"urgent\" ; voir", "un texte avec guillemets et point-virgule");
    // Coupe : pas de ligne de fin.
    const std::string cut = text.substr(0, text.find("fin valeurs"));
    check(!retain::parse(cut, back, &why) && has(why, "coup"), "un fichier coupe : refuse (" + why + ")");
    // La fin ne compte pas les memes valeurs.
    std::string wrong = text;
    wrong.replace(wrong.find("fin valeurs=4"), 13, "fin valeurs=5");
    check(!retain::parse(wrong, back, &why) && has(why, "incomplet"), "une fin qui ne compte pas les memes valeurs : refuse");
    check(!retain::parse("xpg-simulation 1\nfin\n", back, &why), "un autre fichier (la remanence de simulation) : refuse");
    check(!retain::parse(text + "valeur variable=1\n", back, &why), "des lignes apres la fin : refuse");
    check(!retain::parse("", back, &why), "un fichier vide : refuse");
}

void fichier() {
    std::printf("-- le fichier du poste : ecrit d'un bloc, sa copie .bak reprise s'il est abime\n");
    Fixture f;
    const fs::path dir = fs::temp_directory_path() / "xpg_retain_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path file = retain::fileOf(dir.string());
    check(file.filename() == "remanence_exploitation.txt" && file.parent_path().filename() == "historique"
              && file.parent_path().parent_path().filename() == "ihm",
          "le stockage : <projet>/ihm/historique/remanence_exploitation.txt");
    check(retain::fileOf("").empty(), "sans dossier de projet : pas de stockage");
    retain::Store s, back;
    std::string why;
    bool backup = true;
    check(!retain::load(file, back, &why, &backup) && !backup, "rien encore : rien a lire (" + why + ")");
    check(has(retain::checkIntegrity(file, f.p), "Aucun fichier"), "l'integrite : aucun fichier");
    s.project = "Essai";
    s.date = "2026-10-08 21:00:00";
    s.entries.push_back({cell(f.var(f.compteur), "", sim::Value::integer(sim::Type::Int, 3)), s.date});
    check(static_cast<bool>(retain::save(file, s)), "ecrit (le dossier se cree)");
    s.entries.front().cell.value = sim::Value::integer(sim::Type::Int, 4);
    s.entries.push_back({cell(f.var(f.consigne), "", sim::Value::real(18.0)), s.date});
    check(static_cast<bool>(retain::save(file, s)), "reecrit : l'ancien part en .bak");
    check(fs::exists(fs::path(file.string() + ".bak")), "la copie .bak existe");
    check(retain::load(file, back, &why, &backup) && !backup && back.entries.size() == 2, "relu : 2 valeurs, pas la copie");
    check(has(retain::checkIntegrity(file, f.p), "lisible et complet : 2 valeurs de 2 variables"), "l'integrite : lisible et complet, 2 valeurs");
    // Abime (une ecriture coupee par une panne) : la copie .bak est reprise.
    {
        std::string text;
        (void)core::readFileAll(file, text);
        text = text.substr(0, text.size() / 2);
        FILE* out = std::fopen(file.string().c_str(), "wb");
        if (out) {
            std::fwrite(text.data(), 1, text.size(), out);
            std::fclose(out);
        }
    }
    check(retain::load(file, back, &why, &backup) && backup && back.entries.size() == 1 && back.entries.front().cell.value.asInteger() == 3,
          "le fichier abime : sa copie .bak reprise (Compteur = 3) - " + why);
    const std::string integrity = retain::checkIntegrity(file, f.p);
    check(has(integrity, "ab\xC3\xAEm\xC3\xA9") && has(integrity, "copie de secours est lisible"), "l'integrite le dit : abime, la copie lisible");
    fs::remove(fs::path(file.string() + ".bak"), ec);
    check(!retain::load(file, back, &why, &backup) && back.entries.empty(), "abime et sans copie : rien (les valeurs initiales)");
    check(has(retain::checkIntegrity(file, f.p), "les valeurs initiales"), "l'integrite : le poste reprendra les valeurs initiales");
    fs::remove_all(dir, ec);
}

void verrou() {
    std::printf("-- le verrou : un seul poste ecrit le stockage (plusieurs instances)\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_retain_lock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path file = retain::fileOf(dir.string());
    fs::path lock = file;
    lock.replace_extension(".lock");
    auto l = retain::acquireLock(file);
    check(l.held && retain::lockedBy(file).empty() && fs::exists(lock), "le premier poste le prend (remanence_exploitation.lock)");
    check(retain::acquireLock(file).held, "le meme poste (son PID) le reprend");
    {
        FILE* out = std::fopen(lock.string().c_str(), "wb");
        const std::string t = "verrou pid=999999 depuis=\"2026-10-08 20:00:00\"\n";
        if (out) {
            std::fwrite(t.data(), 1, t.size(), out);
            std::fclose(out);
        }
    }
    l = retain::acquireLock(file);
    check(!l.held && has(l.owner, "PID 999999") && has(l.owner, "20:00:00"), "un autre poste en marche : refuse (" + l.owner + ")");
    check(has(retain::lockedBy(file), "PID 999999"), "lockedBy le dit (l'editeur refuse alors Reinitialiser et Importer)");
    retain::releaseLock(file);
    check(fs::exists(lock), "rendre le verrou : jamais celui d'un autre");
    fs::last_write_time(lock, fs::file_time_type::clock::now() - std::chrono::minutes(4), ec);
    check(retain::lockedBy(file).empty(), "personne ne l'a rafraichi depuis 3 minutes : perime");
    l = retain::acquireLock(file);
    check(l.held, "un verrou perime (un poste arrete brutalement) : repris");
    retain::refreshLock(file);
    check(retain::lockedBy(file).empty() && fs::exists(lock), "rafraichi : toujours le notre");
    fs::remove(lock, ec);
    retain::refreshLock(file);
    check(fs::exists(lock), "efface par quelqu'un : le rafraichissement le recree");
    retain::releaseLock(file);
    check(!fs::exists(lock), "rendu a l'arret : le fichier part");
    check(!retain::acquireLock({}).held, "sans stockage : pas de verrou");
    // Le dossier inaccessible (ici : "historique" est un fichier) : une erreur, pas un autre poste.
    const fs::path other = dir / "autre";
    fs::create_directories(other / "ihm", ec);
    if (FILE* f = std::fopen((other / "ihm" / "historique").string().c_str(), "wb")) std::fclose(f);
    l = retain::acquireLock(retain::fileOf(other.string()));
    check(!l.held && !l.error.empty() && l.owner.empty(), "un dossier inaccessible : le verrou ne s'ecrit pas, l'erreur le dit (" + l.error + ")");
    fs::remove_all(dir, ec);
}

void priseEtFusion() {
    std::printf("-- la prise : les seules remanentes (jamais une liee) ; la fusion datee par valeur\n");
    Fixture f;
    std::map<std::string, sim::Value> mem{{"Compteur", sim::Value::integer(sim::Type::Int, 5)},
                                          {"Consigne", sim::Value::real(19.5)},
                                          {"Libre", sim::Value::integer(sim::Type::Int, 9)},
                                          {"Lie", sim::Value::integer(sim::Type::Int, 1)},
                                          {"Four.Temp", sim::Value::real(812.5)},
                                          {"Four.Marche", sim::Value::boolean(true)}};
    const simdata::Reader read = [&mem](const std::string& path) -> const sim::Value* {
        const auto it = mem.find(path);
        return it == mem.end() ? nullptr : &it->second;
    };
    auto cells = retain::captureRetained(f.p, read);
    bool libre = false, lie = false;
    for (const auto& c : cells) {
        libre = libre || c.variable == f.libre;
        lie = lie || c.variable == f.lie;
    }
    check(cells.size() == 4 && !libre && !lie, "prises : Compteur, Consigne, Four.Temp, Four.Marche - ni Libre (non cochee) ni Lie (liee)");
    retain::Store s;
    check(retain::merge(s, f.p, cells, "2026-10-08 10:00:00") && s.entries.size() == 4 && s.date == "2026-10-08 10:00:00",
          "la premiere fusion : 4 valeurs, dateees");
    check(!retain::merge(s, f.p, cells, "2026-10-08 10:00:01"), "rien n'a change : pas d'ecriture a faire");
    check(entryOf(s, f.compteur)->date == "2026-10-08 10:00:00", "... et les dates restent");
    mem["Compteur"] = sim::Value::integer(sim::Type::Int, 6);
    cells = retain::captureRetained(f.p, read);
    check(retain::merge(s, f.p, cells, "2026-10-08 10:00:02"), "Compteur change : a ecrire");
    check(entryOf(s, f.compteur)->date == "2026-10-08 10:00:02" && entryOf(s, f.consigne)->date == "2026-10-08 10:00:00",
          "seul Compteur prend la nouvelle date");
    // Decochee : sa valeur gardee est ignoree au retour, puis part a la fusion suivante.
    f.var(f.consigne).retain = false;
    int ignored = 0;
    const auto back = retain::retainedCells(f.p, s, &ignored);
    bool consigne = false;
    for (const auto& c : back) consigne = consigne || c.variable == f.consigne;
    check(!consigne && ignored == 1, "Consigne decochee : sa valeur ignoree au lancement (1 variable ignoree)");
    cells = retain::captureRetained(f.p, read);
    check(retain::merge(s, f.p, cells, "2026-10-08 10:00:03") && !entryOf(s, f.consigne), "puis elle part du stockage");
    // Reinitialiser.
    check(retain::reset(s, f.compteur) == 1 && !entryOf(s, f.compteur), "reinitialiser Compteur : 1 valeur oubliee");
    check(retain::reset(s, kNoId) == 2 && s.entries.empty(), "reinitialiser tout : les 2 cases de Four");
}

void etats() {
    std::printf("-- l'etat d'une variable pour l'editeur : a jour, converti, incompatible, non remanente, jamais\n");
    Fixture f;
    retain::Store s;
    s.entries.push_back({cell(f.var(f.compteur), "", sim::Value::integer(sim::Type::Int, 12)), "2026-10-08 11:00:00"});
    s.entries.push_back({cell(f.var(f.four), ".Temp", sim::Value::real(800.0)), "2026-10-08 11:00:01"});
    s.entries.push_back({cell(f.var(f.four), ".Marche", sim::Value::boolean(false)), "2026-10-08 11:00:02"});
    auto st = retain::stateOf(f.p, f.var(f.compteur), &s);
    check(st.saved && st.value == "12" && st.date == "2026-10-08 11:00:00" && has(st.state, "\xC3\xA0 jour") && !st.problem,
          "Compteur : 12, sa date, a jour (" + st.state + ")");
    st = retain::stateOf(f.p, f.var(f.four), &s);
    check(st.saved && st.value == "2 cases" && st.date == "2026-10-08 11:00:02", "Four : 2 cases, la plus recente date");
    st = retain::stateOf(f.p, f.var(f.consigne), &s);
    check(!st.saved && has(st.state, "jamais sauvegard"), "Consigne : jamais sauvegardee");
    st = retain::stateOf(f.p, f.var(f.consigne), nullptr);
    check(has(st.state, "pas encore de fichier"), "sans stockage : pas encore de fichier");
    // Un type simple qui change : converti au lancement.
    f.var(f.compteur).type = "REAL";
    st = retain::stateOf(f.p, f.var(f.compteur), &s);
    check(!st.problem && has(st.state, "convertie"), "INT devient REAL : la valeur sera convertie (" + st.state + ")");
    // Un simple qui devient une structure : incompatible.
    f.var(f.compteur).type = "T_Four";
    st = retain::stateOf(f.p, f.var(f.compteur), &s);
    check(st.problem && has(st.state, "incompatible"), "INT devient T_Four : incompatible, la valeur initiale (" + st.state + ")");
    f.var(f.compteur).type = "INT";
    f.var(f.compteur).retain = false;
    st = retain::stateOf(f.p, f.var(f.compteur), &s);
    check(st.saved && has(st.state, "non r\xC3\xA9manente") && has(st.state, "ignor"), "decochee : sa valeur gardee sera ignoree");
    st = retain::stateOf(f.p, f.var(f.lie), &s);
    check(st.problem && has(st.state, "li\xC3\xA9" "e \xC3\xA0 API"), "liee : sa valeur vient de l'equipement");
}

void excel() {
    std::printf("-- l'echange avec Excel : le CSV ecrit et relu, les ecritures d'Excel comprises\n");
    Fixture f;
    retain::Store s;
    s.entries.push_back({cell(f.var(f.compteur), "", sim::Value::integer(sim::Type::Int, -7)), "2026-10-08 12:00:00"});
    s.entries.push_back({cell(f.var(f.consigne), "", sim::Value::real(21.5)), "2026-10-08 12:00:01"});
    s.entries.push_back({cell(f.var(f.four), ".Marche", sim::Value::boolean(true)), "2026-10-08 12:00:02"});
    auto texte = variable(f.p, "Message", "STRING", {}, true);
    f.p.programs.variables.push_back(texte);
    s.entries.push_back({cell(texte, "", sim::Value::text("=SOMME(A1) ; \"x\"")), "2026-10-08 12:00:03"});
    const std::string csv = retain::toCsv(s);
    check(csv.rfind("\xEF\xBB\xBF" "Variable;Chemin;Type;Valeur;Date;", 0) == 0, "le BOM (UTF-8 pour Excel) et la ligne des titres");
    check(has(csv, ";REAL;21,5;"), "un REAL a virgule (Excel en francais) : 21,5");
    check(has(csv, ";INT;-7;"), "un nombre negatif reste un nombre");
    check(has(csv, "\"'=SOMME(A1) ; \"\"x\"\"\""), "un texte qui commence par = : l'apostrophe (pas de formule), les guillemets doubles");
    retain::Store back;
    std::string why;
    check(retain::fromCsv(csv, back, &why), "relu (" + why + ")");
    check(back.entries.size() == 4, "4 valeurs");
    check(entryOf(back, f.consigne) && entryOf(back, f.consigne)->cell.value.asReal() == 21.5, "21,5 relu : 21.5");
    check(entryOf(back, f.compteur) && entryOf(back, f.compteur)->cell.value.asInteger() == -7, "-7 relu");
    check(entryOf(back, texte.id) && entryOf(back, texte.id)->cell.value.asString() == "=SOMME(A1) ; \"x\"", "le texte relu sans l'apostrophe");
    check(entryOf(back, f.four, ".Marche") && entryOf(back, f.four, ".Marche")->cell.value.isTruthy(), "Four.Marche relu : TRUE");
    check(retain::parseAny(csv, back, &why) && back.entries.size() == 4, "parseAny : un CSV");
    check(retain::parseAny(retain::serialize(s), back, &why) && back.entries.size() == 4, "parseAny : le format du poste");
    // Comme Excel le reenregistre : "sep=;", colonnes dans un autre ordre, VRAI/FAUX, 2,25, pas d'Id.
    const std::string excel = "sep=;\r\nValeur;Type;Variable;Chemin\r\n2,25;REAL;Consigne;\r\nVRAI;BOOL;Four;.Marche\r\n\r\n";
    check(retain::fromCsv(excel, back, &why) && back.entries.size() == 2, "sep=;, colonnes melangees, lignes vides : 2 valeurs (" + why + ")");
    check(back.entries[0].cell.value.asReal() == 2.25 && back.entries[1].cell.value.isTruthy() && back.entries[0].cell.variable == kNoId,
          "2,25 et VRAI lus ; sans Id (l'import ira par le nom)");
    check(retain::fromCsv("Variable,Type,Valeur\nCompteur,INT,4\n", back, &why) && back.entries.size() == 1, "un separateur ',' (Excel anglais)");
    check(retain::fromCsv("Variable\tType\tValeur\nCompteur\tINT\t4\n", back, &why) && back.entries.size() == 1, "une tabulation (le presse-papiers)");
    check(!retain::fromCsv("Variable;Valeur\nCompteur;4\n", back, &why) && has(why, "Type"), "sans colonne Type : refuse (" + why + ")");
    check(!retain::fromCsv("Variable;Type;Valeur\nCompteur;ENTIER;4\n", back, &why) && has(why, "type inconnu"), "un type inconnu : refuse, sa ligne dite");
    check(!retain::fromCsv("Variable;Type;Valeur\nCompteur;INT;quatre\n", back, &why) && has(why, "ligne 2"), "une valeur illisible : refusee, sa ligne dite");
}

void import() {
    std::printf("-- l'import : par identifiant si le nom concorde, sinon par nom ; le reste ecarte et compte\n");
    Fixture f;
    retain::Store s;
    s.entries.push_back({cell(f.var(f.compteur), "", sim::Value::integer(sim::Type::Int, 3)), "2026-10-08 13:00:00"});
    s.entries.push_back({cell(f.var(f.four), ".Temp", sim::Value::real(700.0)), "2026-10-08 13:00:00"});
    retain::Store incoming;
    std::string why;
    check(retain::fromCsv("Variable;Chemin;Type;Valeur\nCompteur;;INT;7\nconsigne;;REAL;1,5\nInconnue;;INT;1\nLibre;;INT;4\nLie;;INT;2\n", incoming, &why),
          "le CSV a importer (" + why + ")");
    const auto rep = retain::importInto(s, f.p, incoming, "2026-10-08 13:30:00");
    check(rep.values == 2 && rep.variables == 2 && rep.unknown == 1 && rep.notRetained == 2 && rep.incompatible == 0,
          "2 valeurs (Compteur, Consigne - le nom sans la casse), 1 inconnue, 2 ecartees (Libre non cochee, Lie liee) : " + rep.summary());
    check(entryOf(s, f.compteur) && entryOf(s, f.compteur)->cell.value.asInteger() == 7, "Compteur remplace : 7");
    check(entryOf(s, f.consigne) && entryOf(s, f.consigne)->cell.value.asReal() == 1.5 && entryOf(s, f.consigne)->cell.declared == "REAL",
          "Consigne : 1.5 (sans type declare : celui d'aujourd'hui)");
    check(entryOf(s, f.four, ".Temp") && entryOf(s, f.four, ".Temp")->cell.value.asReal() == 700.0, "Four, absent du fichier, garde sa valeur");
    check(has(rep.summary(), "2 valeurs import\xC3\xA9" "es") && has(rep.summary(), "1 variable inconnue"), "le compte rendu : " + rep.summary());
    // Un identifiant d'un autre projet (le nom ne concorde pas) : le nom l'emporte.
    retain::Store other;
    simdata::Cell c;
    c.variable = f.compteur;
    c.name = "Consigne";
    c.declared = "REAL";
    c.value = sim::Value::real(30.0);
    other.entries.push_back({c, "2026-10-08 14:00:00"});
    const auto rep2 = retain::importInto(s, f.p, other, "2026-10-08 14:00:00");
    check(rep2.values == 1 && entryOf(s, f.consigne)->cell.value.asReal() == 30.0 && entryOf(s, f.compteur)->cell.value.asInteger() == 7,
          "l'id de Compteur mais le nom Consigne : la valeur va a Consigne");
    // Un type declare different et incompatible : garde, mais compte (le poste reprendra la valeur initiale).
    retain::Store bad;
    simdata::Cell b;
    b.variable = f.four;
    b.name = "Four";
    b.declared = "INT";
    b.value = sim::Value::integer(sim::Type::Int, 5);
    bad.entries.push_back({b, ""});
    const auto rep3 = retain::importInto(s, f.p, bad, "2026-10-08 15:00:00");
    check(rep3.values == 1 && rep3.incompatible == 1 && has(rep3.summary(), "incompatible"), "un type incompatible : compte et dit (" + rep3.summary() + ")");
    check(entryOf(s, f.four) && entryOf(s, f.four)->date == "2026-10-08 15:00:00" && !entryOf(s, f.four, ".Temp"),
          "ses anciennes valeurs remplacees, la date de l'import");
}

void projet() {
    std::printf("-- la case Remanente dans le fichier du projet\n");
    Fixture f;
    const auto files = serializeProject(f.p);
    const auto r = parseProject([&files](const std::string& path, std::string& out) {
        for (const auto& pf : files)
            if (pf.path == path && pf.data) {
                out.assign(pf.data->begin(), pf.data->end());
                return true;
            }
        return false;
    });
    check(r.has_value(), "le projet relu");
    if (!r) return;
    std::map<std::string, bool> flags;
    for (const auto& v : r.value().programs.variables) flags[v.name] = v.retain;
    check(flags["Compteur"] && flags["Consigne"] && !flags["Libre"] && flags["Four"], "les cases Remanente relues (Libre ne l'est pas)");
}

void moteur() {
    std::printf("-- le moteur : les remanentes rendues au lancement suivant, avant les scripts de Demarrage\n");
    Fixture f;
    f.p.programs.variables.push_back(variable(f.p, "Copie", "INT", "0"));
    f.p.programs.scripts.push_back(script(f.p, "Init", "Demarrage", "Copie := Compteur;"));
    f.p.programs.scripts.push_back(script(f.p, "Plus", "Appel", "Compteur := Compteur + 1;\nLibre := Libre + 1;\nConsigne := 42.5;"));
    View v = makeView(f.p, "Vue");
    f.p.views = {v};
    f.p.config.startView = v.id;
    FakePlc plc;
    Runtime rt;
    rt.bind(&f.p, &plc);
    rt.start(0.0);
    std::string why;
    for (int k = 0; k < 4; ++k) (void)rt.callScript("Plus", 1.0 + k, &why);
    check(rt.variable("Compteur")->asInteger() == 4 && rt.variable("Libre")->asInteger() == 4, "quatre appels : Compteur = 4, Libre = 4");
    retain::Store s;
    (void)retain::merge(s, f.p, rt.captureData([](const Variable& x) { return x.retain; }), "2026-10-08 16:00:00");
    bool libre = false;
    for (const auto& e : s.entries) libre = libre || e.cell.variable == f.libre;
    check(!libre && entryOf(s, f.compteur) && entryOf(s, f.consigne), "gardees : Compteur et Consigne, pas Libre");
    rt.stop(6.0, "arr\xC3\xAAt");
    // Le lancement suivant (le fichier relu) : rendues apres les valeurs initiales, avant Demarrage.
    retain::Store read;
    check(retain::parse(retain::serialize(s), read, &why), "le stockage relu");
    int ignored = 0;
    rt.setStartData(retain::retainedCells(f.p, read, &ignored), "Variables r\xC3\xA9manentes restaur\xC3\xA9" "es");
    rt.start(7.0);
    check(rt.variable("Compteur")->asInteger() == 4 && rt.variable("Consigne")->asReal() == 42.5, "Compteur = 4, Consigne = 42.5 rendues");
    check(rt.variable("Libre")->asInteger() == 0, "Libre (non remanente) : sa valeur initiale");
    check(rt.variable("Copie")->asInteger() == 4, "le script de Demarrage voit la valeur rendue");
    bool said = false;
    for (const auto& e : rt.journal()) said = said || (has(e.message, "Variables r\xC3\xA9manentes restaur\xC3\xA9" "es") && e.kind == "R\xC3\xA9manence");
    check(said, "le journal le dit, sous R\xC3\xA9manence");
    rt.stop(8.0);
    // Un type change entre deux lancements : la regle de la simulation (ici INT -> REAL : converti).
    f.var(f.compteur).type = "REAL";
    rt.bind(&f.p, &plc);
    rt.setStartData(retain::retainedCells(f.p, read), "Variables r\xC3\xA9manentes restaur\xC3\xA9" "es");
    rt.start(9.0);
    check(rt.variable("Compteur") && rt.variable("Compteur")->type() == sim::Type::Real && rt.variable("Compteur")->asReal() == 4.0,
          "INT devenu REAL : 4 converti en 4.0");
    rt.stop(10.0);
    // Devenu une structure : incompatible, la valeur initiale, et le journal le dit.
    f.var(f.compteur).type = "T_Four";
    rt.bind(&f.p, &plc);
    rt.setStartData(retain::retainedCells(f.p, read), "Variables r\xC3\xA9manentes restaur\xC3\xA9" "es");
    rt.start(11.0);
    bool warned = false;
    for (const auto& e : rt.journal()) warned = warned || (has(e.message, "Compteur") && has(e.message, "incompatible"));
    check(warned, "INT devenu T_Four : incompatible, valeur initiale - le journal le dit");
    rt.stop(12.0);
}

} // namespace

int main() {
    format();
    fichier();
    verrou();
    priseEtFusion();
    etats();
    excel();
    import();
    projet();
    moteur();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
