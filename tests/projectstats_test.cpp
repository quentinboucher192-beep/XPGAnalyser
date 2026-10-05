// tests/projectstats_test.cpp - lot API 7 : les statistiques du projet ouvert.
//
//   projectstats_test <MAST.XPG> [libs]
//
//  Sans ecran : project/ProjectStats, ce que l'onglet API > Statistiques
//  montre. Les tailles du modele (elementaires, chaines, tableaux, un DDT fait
//  a la main, une boucle qui ne boucle pas) ; la table des types (une ligne
//  par type, une borne nommee) ; la lecture du code (ecrites / lues, sorties
//  liees, SET, un parametre d'unite lie a une globale, ce que l'IHM ou un
//  superviseur peut ecrire) ; les nombres en francais. Sur le projet d'essai :
//  les lignes (par entree = total = par langage = par section, et les memes
//  que ApiChecks), les variables (total = globales + locales + parametres, les
//  genres font les globales), la memoire (un DDT = la somme de ses champs, les
//  parts font 100 %), « A regarder », le CSV - et le tout en moins de 200 ms.
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ApiChecks.hpp"
#include "../src/project/ProjectStats.hpp"
#include "../src/project/SharedLibrary.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace st = project::stats;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

domain::Index addVariable(domain::Project& p, const char* name, const char* type, domain::VariableScope scope, domain::Index owner) {
    domain::Variable v;
    v.name = p.strings.intern(name);
    v.type.name = p.strings.intern(type);
    v.scope = scope;
    v.owner = owner;
    p.variables.push_back(std::move(v));
    return static_cast<domain::Index>(p.variables.size() - 1);
}

domain::Index addSection(domain::Project& p, const char* name, const char* body, domain::Index owner) {
    domain::Section s;
    s.name = p.strings.intern(name);
    s.language = domain::PouLanguage::ST;
    s.body = body;
    s.lineCount = 1;
    s.owner = owner;
    p.sections.push_back(std::move(s));
    return static_cast<domain::Index>(p.sections.size() - 1);
}

const st::NamedVariable* findNamed(const std::vector<st::NamedVariable>& v, const std::string& name) {
    for (const auto& n : v)
        if (n.name == name) return &n;
    return nullptr;
}

const st::TypeRow* findRow(const st::MemoryStats& m, const std::string& name) {
    for (const auto& r : m.types)
        if (r.name == name) return &r;
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("1. Le modele de taille\n");
    {
        check(st::elementaryBytes("BOOL") == 1 && st::elementaryBytes("ebool") == 1 && st::elementaryBytes("INT") == 2
                  && st::elementaryBytes("udint") == 4 && st::elementaryBytes("TIME") == 4 && st::elementaryBytes("LREAL") == 8,
              "BOOL 1, EBOOL 1, INT 2, UDINT 4, TIME 4, LREAL 8 (sans la casse)");
        check(st::elementaryBytes("STRING") == 17 && st::elementaryBytes("string[100]") == 101 && st::elementaryBytes(" STRING [4] ") == 5,
              "STRING 17 ; STRING[n] n + 1");
        check(st::elementaryBytes("STRING[N]") == 0 && st::elementaryBytes("armoire") == 0 && st::elementaryBytes("Strings") == 0,
              "une longueur nommee, un DDT, Strings : pas elementaires");
        const auto f = st::SizeModel::flatten("ARRAY[0..3, 0..7] OF ARRAY[1..2] OF INT");
        check(f.ok && f.count == 64 && f.base == "INT", "un tableau de tableaux : 4 x 8 x 2 = 64 INT");
        const auto g = st::SizeModel::flatten("ARRAY[0..N] OF REAL");
        check(!g.ok && g.base == "REAL", "une borne nommee : l'element, et une taille inconnue");

        domain::Project p;
        domain::DerivedType dt;
        dt.name = p.strings.intern("Petit");
        dt.fields.push_back(addVariable(p, "a", "BOOL", domain::VariableScope::DerivedMember, 0));
        dt.fields.push_back(addVariable(p, "b", "INT", domain::VariableScope::DerivedMember, 0));
        dt.fields.push_back(addVariable(p, "c", "STRING[8]", domain::VariableScope::DerivedMember, 0));
        dt.fields.push_back(addVariable(p, "d", "ARRAY[0..3] OF REAL", domain::VariableScope::DerivedMember, 0));
        p.derivedTypes.push_back(std::move(dt));
        domain::DerivedType loop;                                   // se contient lui-meme
        loop.name = p.strings.intern("Boucle");
        loop.fields.push_back(addVariable(p, "x", "DINT", domain::VariableScope::DerivedMember, 1));
        loop.fields.push_back(addVariable(p, "moi", "Boucle", domain::VariableScope::DerivedMember, 1));
        p.derivedTypes.push_back(std::move(loop));
        p.buildIndices();
        st::SizeModel sizes(p);
        const auto petit = sizes.of("petit");
        check(petit.bytes == 1 + 2 + 9 + 16 && petit.genre == st::TypeGenre::Derived && petit.resolved && petit.name == "Petit",
              "un DDT : la somme de ses champs, 1 + 2 + 9 + 16 = 28 octets");
        check(sizes.of("ARRAY[1..10] OF Petit").bytes == 280, "dix Petit : 280 octets");
        const auto boucle = sizes.of("Boucle");
        check(boucle.bytes == 4, "un type qui se contient : il ne boucle pas (4 octets, le reste compte 0)");
        check(!sizes.of("Inconnu").resolved && sizes.of("Inconnu").genre == st::TypeGenre::Unknown, "un type inconnu : non resolu");
        const auto ton = sizes.of("TON");
        check(ton.genre == st::TypeGenre::Standard && ton.bytes == 10, "un TON : ses broches, 1 + 4 + 1 + 4 = 10 octets");
        check(sizes.unknownTypes().size() == 1 && sizes.unknownTypes().front() == "Inconnu", "les types inconnus : Inconnu, seul");
    }

    std::printf("1b. La table des types\n");
    {
        domain::Project p;
        addVariable(p, "Texte1", "STRING[32]", domain::VariableScope::Global, domain::kNoIndex);
        addVariable(p, "Texte2", "string [32]", domain::VariableScope::Global, domain::kNoIndex);
        addVariable(p, "Mesures", "ARRAY[0..N] OF INT", domain::VariableScope::Global, domain::kNoIndex);
        addVariable(p, "Compteur", "INT", domain::VariableScope::Global, domain::kNoIndex);
        p.buildIndices();
        const auto s = st::compute(p, nullptr, nullptr);
        const auto* str = findRow(s.memory, "STRING[32]");
        check(str && str->declared == 2 && str->instances == 2 && str->unitBytes == 33 && s.memory.types.size() == 2,
              "STRING[32] et string [32] : une seule ligne, 2 x 33 octets");
        const auto* ints = findRow(s.memory, "INT");
        check(ints && ints->declared == 2 && ints->instances == 2 && !ints->resolved && s.memory.unresolved == 1,
              "ARRAY[0..N] OF INT : un element compte, la taille dite incomplete (pas 0..0 lu par l'import)");
        check(s.memory.total == 66 + 4, "la memoire : 66 + 4 octets");
    }

    std::printf("2. La lecture du code\n");
    {
        domain::Project p;
        for (const char* g : {"Ecrite", "Lue", "LesDeux", "Sortie", "Posee", "Liee", "Jamais", "Seule", "Situee", "Init"})
            addVariable(p, g, "INT", domain::VariableScope::Global, domain::kNoIndex);
        p.variables[8].address = domain::Address::parse("%MW10");
        p.variables[8].located = true;
        p.variables[9].initValue = p.strings.intern("5");
        // Une unite dont le parametre "entree" est lie a la globale Liee, et une
        // variable locale qui cache la globale Seule.
        domain::Pou unit;
        unit.name = p.strings.intern("Unite");
        unit.kind = domain::PouKind::ProgramUnit;
        p.pous.push_back(std::move(unit));
        const auto param = addVariable(p, "entree", "INT", domain::VariableScope::InOut, 0);
        p.variables[param].attributes.emplace_back("EffectiveParameter", "Liee");
        p.pous[0].parameters.push_back(param);
        p.pous[0].locals.push_back(addVariable(p, "seule", "INT", domain::VariableScope::Local, 0));
        addSection(p, "Tache",
                   "(* Jamais := 1; *)\n"
                   "Ecrite := Lue + 1;\n"
                   "IF Lue > 0 THEN LesDeux := LesDeux + 1; END_IF;\n"
                   "Tempo(IN := TRUE, Q => Sortie);\n"
                   "SET(Posee);\n"
                   "Situee := Init + Jamais;\n",
                   domain::kNoIndex);
        const auto sec = addSection(p, "Code", "entree := entree + 1;\nseule := 2;\nx := Sortie;\ny := Posee;\n", 0);
        p.pous[0].sections.push_back(sec);
        addVariable(p, "Condition", "BOOL", domain::VariableScope::Global, domain::kNoIndex);
        p.sections[sec].activationCondition = p.strings.intern("Condition");
        // Lues, jamais ecrites par le code - mais ecrites dehors, peut-etre :
        // l'IHM (elle la lit, elle peut l'ecrire), un superviseur (%MW20).
        addVariable(p, "LueIhm", "INT", domain::VariableScope::Global, domain::kNoIndex);
        const auto consigne = addVariable(p, "Consigne", "INT", domain::VariableScope::Global, domain::kNoIndex);
        p.variables[consigne].address = domain::Address::parse("%MW20");
        p.variables[consigne].located = true;
        addSection(p, "Dehors", "z := LueIhm + Consigne;\n", domain::kNoIndex);
        p.buildIndices();
        std::map<std::string, st::ReadInfo> reads;
        reads["lueihm"] = {"LueIhm_IHM", 1};
        const auto s = st::compute(p, nullptr, &reads);
        const auto& vs = s.variables;
        check(findNamed(vs.writtenNeverRead, "Ecrite") && !findNamed(vs.writtenNeverRead, "LesDeux"), "Ecrite : ecrite, jamais lue ; LesDeux non");
        check(findNamed(vs.readNeverWritten, "Lue") != nullptr, "Lue : lue, jamais ecrite");
        check(!findNamed(vs.readNeverWritten, "Sortie") && !findNamed(vs.writtenNeverRead, "Sortie"), "Q => Sortie : ecrite (et lue plus loin)");
        check(!findNamed(vs.readNeverWritten, "Posee") && !findNamed(vs.writtenNeverRead, "Posee"), "SET(Posee) : ecrite (et lue plus loin)");
        check(findNamed(vs.readNeverWritten, "Jamais") != nullptr, "un commentaire n'ecrit pas : Jamais reste jamais ecrite");
        check(!findNamed(vs.unused, "Liee") && !findNamed(vs.readNeverWritten, "Liee") && !findNamed(vs.writtenNeverRead, "Liee"),
              "Liee : lue et ecrite par l'unite, sous le nom de son parametre");
        check(findNamed(vs.unused, "Seule") != nullptr, "Seule : la locale du meme nom la cache - pas utilisee");
        check(!findNamed(vs.writtenNeverRead, "Situee"), "Situee (%MW10) : ecrite pour qu'on la lise dehors");
        check(!findNamed(vs.readNeverWritten, "Init"), "Init : une valeur initiale, pas une alerte");
        check(!findNamed(vs.unused, "Condition"), "Condition : lue par une condition d'activation");
        check(!findNamed(vs.readNeverWritten, "LueIhm") && findNamed(vs.readByHmi, "LueIhm"), "LueIhm : l'IHM la lit (et peut l'ecrire) - pas une alerte");
        check(!findNamed(vs.readNeverWritten, "Consigne"), "Consigne (%MW20) : un superviseur peut l'ecrire - pas une alerte");
        check(vs.count[0] == 13 && vs.count[1] == 1 && vs.count[2] == 1, "13 globales, 1 locale, 1 parametre");
    }

    std::printf("3. Les nombres, en francais\n");
    {
        check(st::thousands(2912) == "2\xE2\x80\xAF" "912" && st::thousands(12) == "12", "2 912 (espace fine), 12");
        check(st::decimal(46.73, 1) == "46,7" && st::decimal(1234.5, 1) == "1\xE2\x80\xAF" "234,5", "46,7 ; 1 234,5");
        check(st::bytesText(412) == "412 o" && st::bytesText(16480) == "16,1 Ko" && st::bytesText(3u * 1024u * 1024u) == "3,0 Mo",
              "412 o, 16,1 Ko, 3,0 Mo");
        check(st::percentText(0.344) == "34 %" && st::percentText(0.001) == "< 1 %" && st::percentText(0.0) == "0 %", "34 %, < 1 %, 0 %");
    }

    std::printf("4. Sur le projet d'essai\n");
    if (argc < 2) {
        std::printf("       (sans MAST.XPG : rien d'essaye ici)\n");
    } else {
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        check(static_cast<bool>(imported), "le projet d'essai se lit");
        if (imported) {
            const auto& p = *imported->project;
            std::unique_ptr<project::SharedLibrary> lib;
            if (argc > 2) {
                lib = std::make_unique<project::SharedLibrary>(argv[2]);
                if (!lib->scan()) lib.reset();
            }
            std::map<std::string, st::ReadInfo> reads;
            reads["tssys"] = {"TsSys_IHM", 2};                         // une lecture de l'IHM, inventee
            const auto t0 = std::chrono::steady_clock::now();
            const auto s = st::compute(p, lib.get(), &reads);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            check(s.valid, "calcule");

            // ---- les lignes
            const auto& c = s.code;
            std::size_t byEntry = 0, bySection = 0, byLanguage = 0;
            bool entriesAddUp = true;
            for (const auto& e : c.entries) {
                byEntry += e.lines;
                std::size_t l = 0;
                for (const auto v : e.byLanguage) l += v;
                entriesAddUp = entriesAddUp && l == e.lines;
            }
            for (const auto& sec : c.sections) bySection += sec.lines;
            for (const auto v : c.byLanguage) byLanguage += v;
            check(byEntry == c.lines && bySection == c.lines && byLanguage == c.lines && entriesAddUp,
                  "les lignes : par entree = par section = par langage = total (" + std::to_string(c.lines) + ")");
            const auto api = project::api::entriesOf(p, c.task);
            bool same = api.size() == c.entries.size();
            for (std::size_t i = 0; same && i < api.size(); ++i)
                same = api[i].name == c.entries[i].name && api[i].lines == c.entries[i].lines && api[i].sections == c.entries[i].sections;
            check(same, "les memes entrees que l'ordre d'execution (ApiChecks) : " + std::to_string(c.entries.size()));
            check(c.entries.size() == 21 && c.taskSections == 18 && c.units == 3 && c.unitSections == 47,
                  "21 entrees de MAST : 18 sections, 3 unites (47 sections)");
            bool owners = c.sections.size() == c.taskSections + c.unitSections;
            for (const auto& sec : c.sections) {
                const bool ok = sec.unit.empty() ? sec.owner == domain::kNoIndex
                                                 : sec.owner < p.pous.size() && std::string(p.strings.text(p.pous[sec.owner].name)) == sec.unit;
                owners = owners && ok && sec.section < p.sections.size();
            }
            check(owners, "chaque section : son unite (son POU), ou aucune pour une section de la tache");
            std::size_t all = 0;
            for (const auto& sec : p.sections) all += sec.lineCount;
            check(c.lines + c.dfbLines + c.subroutineLines + c.otherLines == all, "avec le corps des DFB et le reste : toutes les lignes du projet");

            // ---- les variables
            const auto& vs = s.variables;
            std::size_t members = 0;
            for (const auto& v : p.variables) members += v.scope == domain::VariableScope::DerivedMember ? 1u : 0u;
            check(vs.total() == vs.count[0] + vs.count[1] + vs.count[2] && vs.total() + members == p.variables.size(),
                  "variables = globales + locales + parametres (les champs de DDT a part)");
            bool genresAddUp = true;
            for (std::size_t sc = 0; sc < st::kScopeCount; ++sc) {
                std::size_t g = 0;
                for (const auto n : vs.byGenre[sc]) g += n;
                genresAddUp = genresAddUp && g == vs.count[sc];
            }
            check(genresAddUp, "les genres font les globales (et les locales, et les parametres)");
            check(vs.count[0] == 251, "251 globales");
            check(vs.commented <= vs.total() && vs.globalsWithoutComment <= vs.count[0], "les commentees, les globales sans commentaire");
            check(findNamed(vs.readByHmi, "TsSys") != nullptr && vs.readByHmi.size() == 1, "lue par l'IHM : TsSys (et elle seule)");
            bool noLocated = true;
            for (const auto& n : vs.readNeverWritten) noLocated = noLocated && n.detail.empty();
            check(noLocated && !findNamed(vs.readNeverWritten, "NomArmoireModifs"),
                  "jamais ecrites : " + std::to_string(vs.readNeverWritten.size()) + ", aucune situee (NomArmoireModifs, %MW2050, n'y est plus)");
            for (const auto& d : vs.duplicates) check(d.variables.size() > 1, "adresse en double : " + d.address);

            // ---- la memoire
            const auto& m = s.memory;
            std::uint64_t rows = 0;
            double shares = 0.0;
            bool rowsConsistent = true;
            for (const auto& r : m.types) {
                rows += r.totalBytes;
                shares += r.share;
                rowsConsistent = rowsConsistent && r.totalBytes == r.instances * r.unitBytes;
            }
            check(rows == m.total && m.byScope[0] + m.byScope[1] + m.byScope[2] == m.total, "la memoire : les types font le total, les portees aussi");
            check(rowsConsistent, "chaque type : total = instances x taille");
            check(m.total == 0 || std::fabs(shares - 1.0) < 1e-9, "les parts font 100 %");
            // Un petit DDT de champs elementaires : la somme de ses champs.
            bool ddtChecked = false;
            st::SizeModel sizes(p);
            for (const auto& dt : p.derivedTypes) {
                std::uint64_t sum = 0;
                bool elementary = !dt.fields.empty();
                for (const auto f : dt.fields) {
                    const auto b = st::elementaryBytes(p.strings.text(p.variables[f].type.name));
                    elementary = elementary && b != 0;
                    sum += b;
                }
                if (!elementary) continue;
                const auto info = sizes.of(p.strings.text(dt.name));
                check(info.bytes == sum && info.resolved,
                      std::string(p.strings.text(dt.name)) + " (" + std::to_string(dt.fields.size()) + " champs) : " + std::to_string(sum) + " octets, la somme de ses champs");
                ddtChecked = true;
                break;
            }
            check(ddtChecked, "un DDT de champs elementaires trouve");
            if (const auto* cfg = findRow(m, "config_gaz")) {
                check(cfg->genre == st::TypeGenre::Derived && cfg->instances >= 40 && cfg->unitBytes == sizes.of("config_gaz").bytes,
                      "config_gaz : un DDT, " + std::to_string(cfg->instances) + " instances de " + std::to_string(cfg->unitBytes) + " octets");
            } else {
                check(false, "config_gaz dans la table des types");
            }

            // ---- a regarder, le CSV, le temps
            check(!s.findings.empty(), "a regarder : " + std::to_string(s.findings.size()) + " ligne(s)");
            bool keys = true;
            for (const auto& f : s.findings) keys = keys && (f.button.empty() == f.request.empty());
            check(keys, "chaque bouton a sa cle");
            check(s.late.size() == project::api::lateReads(p, c.task).size(), "les lectures avant l'ecriture : celles de l'ordre d'execution");
            const auto csv = st::toCsv(s);
            check(csv.find("Type;Genre;D\xC3\xA9" "clar\xC3\xA9" "es") != std::string::npos && csv.find("config_gaz;") != std::string::npos,
                  "le CSV : les chiffres, les entrees, les sections, les types");
            check(ms < 200.0, "calcule en " + std::to_string(ms) + " ms (moins de 200)");

            std::printf("\n  RESUME - %s\n", s.project.c_str());
            std::printf("  lignes de %s : %zu (%zu entrees : %zu sections, %zu unites / %zu sections) ; hors tache : DFB %zu (%zu), SR %zu, autres %zu\n",
                        c.task.c_str(), c.lines, c.entries.size(), c.taskSections, c.units, c.unitSections, c.dfbLines, c.dfbs,
                        c.subroutineLines, c.otherLines);
            for (std::size_t l = 0; l < st::kLanguageCount; ++l)
                if (c.byLanguage[l]) std::printf("    %s : %zu\n", std::string(st::languageName(static_cast<st::Language>(l))).c_str(), c.byLanguage[l]);
            std::printf("  variables : %zu (globales %zu, locales %zu, parametres %zu), situees %zu, commentees %zu (%.1f %%)\n", vs.total(),
                        vs.count[0], vs.count[1], vs.count[2], vs.located, vs.commented, vs.commentedShare() * 100.0);
            std::printf("    globales par genre : DFB %zu, standard %zu, DDT %zu, situees %zu, autres %zu\n", vs.byGenre[0][0], vs.byGenre[0][1],
                        vs.byGenre[0][2], vs.byGenre[0][3], vs.byGenre[0][4]);
            std::printf("    pas utilisees %zu, lues par l'IHM %zu, ecrites jamais lues %zu, jamais ecrites %zu, adresses en double %zu, E/S sans commentaire %zu\n",
                        vs.unused.size(), vs.readByHmi.size(), vs.writtenNeverRead.size(), vs.readNeverWritten.size(), vs.duplicates.size(),
                        vs.ioWithoutComment.size());
            std::printf("  memoire : %llu octets (globales %llu, locales %llu, parametres %llu), %zu declarations d'un type inconnu\n",
                        static_cast<unsigned long long>(m.total), static_cast<unsigned long long>(m.byScope[0]),
                        static_cast<unsigned long long>(m.byScope[1]), static_cast<unsigned long long>(m.byScope[2]), m.unresolved);
            for (std::size_t i = 0; i < m.types.size() && i < 8; ++i) {
                const auto& r = m.types[i];
                std::printf("    %-22s %-20s %4zu decl. %6llu inst. x %6llu o = %8llu o (%s)\n", r.name.c_str(),
                            std::string(st::typeGenreLabel(r.genre)).c_str(), r.declared, static_cast<unsigned long long>(r.instances),
                            static_cast<unsigned long long>(r.unitBytes), static_cast<unsigned long long>(r.totalBytes), st::percentText(r.share).c_str());
            }
            for (const auto& f : s.findings) std::printf("  a regarder : %s -- %s [%s]\n", f.title.c_str(), f.detail.c_str(), f.request.c_str());
            std::printf("  calcule en %.2f ms\n", s.milliseconds);
        }
    }

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "projectstats_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
