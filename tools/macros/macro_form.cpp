// =============================================================================
//  tools/macros/macro_form.cpp - le formulaire d'une macro, en ligne de commande
//  (lot macros 1)
// -----------------------------------------------------------------------------
//  Ce que l'onglet Macros montre, sans ecran : les champs que la macro atteint
//  avec ces reponses, leur genre, leur valeur et d'ou elle vient, puis le
//  bilan de l'apercu. Le vrai moteur (MacroSession), sur un vrai projet.
//
//      macro_form <projet.XPG> <libs> <Macro> [--exact] [--appliquer|--defaire] [cle=valeur ...] [tableau:nom=fichier.csv]
//      macro_form MAST.XPG libs ImporterClasseur classeur=01-station-complete.xlsm
// =============================================================================
#include "../../src/core/EventBus.hpp"
#include "../../src/import/ProjectImporter.hpp"
#include "../../src/project/MacroSession.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace project::macro;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "macro_form <projet.XPG> <libs> <Macro> [--exact] [--appliquer] [cle=valeur ...]\n");
        return 2;
    }
    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(argv[1]);
    if (!imported) {
        std::fprintf(stderr, "import : %s\n", imported.error().message().c_str());
        return 2;
    }
    auto project = imported->project;
    project::SharedLibrary library(argv[2]);
    (void)library.scan();
    const std::string source = library.macroSource(argv[3]);
    if (source.empty()) {
        std::fprintf(stderr, "macro introuvable : %s\n", argv[3]);
        return 2;
    }
    MacroSession session(project, argv[2], argv[3], source);
    bool exact = false, apply = false, undo = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--exact") == 0) { exact = true; continue; }
        if (std::strcmp(argv[i], "--appliquer") == 0) { apply = true; continue; }
        if (std::strcmp(argv[i], "--defaire") == 0) { apply = true; undo = true; continue; }
        const std::string a = argv[i];
        const auto eq = a.find('=');
        if (eq == std::string::npos) continue;
        // tableau:<nom>=<chemin.csv> : le tableau d'un "#! tableau" (OpenTable).
        if (a.rfind("tableau:", 0) == 0) {
            const auto why = session.setTable(a.substr(8, eq - 8), a.substr(eq + 1));
            if (!why.empty()) std::fprintf(stderr, "tableau %s : %s\n", a.substr(8, eq - 8).c_str(), why.c_str());
            continue;
        }
        session.setAnswer(a.substr(0, eq), a.substr(eq + 1));
    }
    const auto& out = session.refresh(exact);
    std::printf("%s : %zu tour(s) en %.2f s, %s%s\n", argv[3], out.rounds, out.seconds,
                out.complete ? "complet" : "incomplet", out.stopped ? " (AskNow)" : "");
    std::string group = "?";
    for (const auto& f : out.fields) {
        if (f.group != group) {
            group = f.group;
            std::printf("  [%s]\n", group.empty() ? "sans groupe" : group.c_str());
        }
        const char* origin = f.origin == MacroSession::Field::Origin::Typed ? "tapee"
                           : f.origin == MacroSession::Field::Origin::Remembered ? "retenue" : "proposee";
        std::printf("    %-14s %-15s %-45s = '%s' (%s)%s\n", f.question.key.c_str(), std::string(kindKey(f.spec.kind)).c_str(),
                    f.label.c_str(), f.value.c_str(), origin, f.advanced ? " [avance]" : "");
    }
    const auto& t = out.report.tally;
    std::printf("  bilan : %zu lignes lues, %zu variables (%zu instances), %zu types, %zu sections, %zu lignes, %zu imports ; %zu actions, %zu avertissements\n",
                t.rowsRead, t.variables, t.instances, t.types, t.sections, t.lines, t.imports, out.report.actions.size(), out.report.warnings.size());
    if (!out.failure.empty()) std::printf("  ECHEC : %s\n", out.failure.c_str());
    std::printf("  confirmation : %s\n", session.applyText().c_str());
    if (apply) {
        // L'etat d'avant, pour --defaire : les tailles, et les noms dans l'ordre.
        const auto signature = [&project] {
            std::string sig;
            for (const auto& v : project->variables) { sig += project->strings.text(v.name); sig += ';'; }
            for (const auto& s : project->sections) { sig += project->strings.text(s.name); sig += '|'; sig += s.body; }
            for (const auto& d : project->pous) { sig += project->strings.text(d.name); sig += ','; }
            for (const auto& d : project->derivedTypes) { sig += project->strings.text(d.name); sig += ','; }
            for (const auto& task : project->tasks) for (const auto i : task.sections) sig += std::to_string(i) + '.';
            return sig;
        };
        const auto before = signature();
        project::MacroReport report;
        auto command = session.apply(report);
        std::printf("  appliquer : %s, %zu actions\n", command ? "une commande" : (report.ok ? "rien a changer" : report.failure.c_str()),
                    report.actions.size());
        // --defaire : le Ctrl+Z de l'application, puis le projet compare a celui d'avant.
        if (undo && command) {
            const auto sections = project->sections.size(), variables = project->variables.size();
            const auto status = command->undo();
            std::printf("  defaire : %s ; sections %zu -> %zu, variables %zu -> %zu ; projet %s\n",
                        status ? "ok" : status.error().message().c_str(), sections, project->sections.size(), variables,
                        project->variables.size(), signature() == before ? "IDENTIQUE a celui d'avant" : "DIFFERENT de celui d'avant");
        }
    }
    return 0;
}
