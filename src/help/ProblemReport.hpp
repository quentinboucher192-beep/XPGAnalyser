// =============================================================================
//  help/ProblemReport.hpp - 1.11 (chantier T2) : Signaler un probleme (D7)
// -----------------------------------------------------------------------------
//  Le formulaire (ce qui s'est passe, comment le refaire, ce que tu attendais,
//  une capture), les pieces a cocher (la version, le systeme, le resume du
//  projet SANS SES DONNEES, le journal), l'apercu, le contenu du zip.
//
//  RIEN NE PART PAR LE RESEAU : l'ecran ecrit le zip dans le dossier
//  "signalements" des donnees de l'utilisateur (exporter::doc::zip), et c'est
//  l'utilisateur qui l'envoie.
//
//  Pur, sans ecran : l'ecran remplit les comptes du projet et lit le journal.
// =============================================================================
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace help::report {

struct Form {
    std::string what;            // ce qui s'est passe : obligatoire
    std::string howTo;           // comment le refaire
    std::string expected;        // ce que tu attendais
    std::string screenshotPng;   // le nom de la capture jointe ; vide : aucune
    bool withVersion{true};
    bool withSystem{true};
    bool withProject{true};
    bool withLog{true};
    // 1.11 : une piece jointe de plus, telle quelle. « Signaler le probleme » de la
    // fenetre du plantage (app/CrashDialogs) y met le rapport de crashs/. Vide : aucune.
    std::string attachedName;
    std::string attachedText;
};

// Le projet, en comptes seulement : ni noms, ni valeurs, ni code.
struct ProjectCounts {
    bool open{false};
    int  sections{0}, dfbs{0}, ddts{0}, variables{0};
    int  views{0}, objects{0}, scripts{0}, alarms{0};
};

struct Facts {
    std::string              version;   // "1.11.0"
    std::string              commit;    // "e0eea9b" ; vide : inconnu
    std::string              system;    // "Windows 11 (10.0.22631), 1920 x 1080, 125 %"
    ProjectCounts            project;
    std::vector<std::string> log;       // le journal de l'application, brut
};

// Le systeme, en un mot : "Windows", "macOS" ou "Linux" (tranche 5). L'appli peut y
// ajouter l'ecran.
[[nodiscard]] inline std::string systemName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

// "Preparer le zip" n'est possible qu'avec "ce qui s'est passe" rempli.
[[nodiscard]] bool canPrepare(const Form&);

// "Automate : 75 sections, 12 DFB, 30 DDT, 1240 variables. IHM : 18 vues..."
[[nodiscard]] std::string projectSummary(const ProjectCounts&);

// Une ligne du journal sans ses valeurs : les textes entre apostrophes ou
// guillemets, et ce qui suit un "=" ou un ":=", deviennent "...".
[[nodiscard]] std::string scrubLine(std::string_view line);
// Les `last` dernieres lignes, sans leurs valeurs.
[[nodiscard]] std::vector<std::string> filterLog(const std::vector<std::string>& lines, std::size_t last = 200);

// L'apercu, et ce que "Copier le texte" met dans le presse-papiers.
[[nodiscard]] std::string text(const Form&, const Facts&);

// "signalement-2026-10-02-1134.zip".
[[nodiscard]] std::string zipName(int year, int month, int day, int hour, int minute);

// Les fichiers du zip : signalement.txt, la capture (si jointe), journal.txt
// (si coche). `screenshotBytes` : le contenu du PNG ; vide : pas de capture.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> zipItems(const Form&, const Facts&,
                                                                        const std::string& screenshotBytes);

// "Preparer le zip" (tranche 3) : ecrit zipItems(...) en zip (exporter::doc::zip)
// dans <dataDir>/signalements/ (cree au besoin), sous zipName(...) ; un nom deja
// pris recoit "-2", "-3"... Rend le chemin du zip ; vide en cas d'echec (why dit
// pourquoi). Rien ne part par le reseau.
struct Stamp { int year{0}, month{0}, day{0}, hour{0}, minute{0}; };
[[nodiscard]] std::filesystem::path writeZip(const std::filesystem::path& dataDir, const Form&, const Facts&,
                                             const std::string& screenshotBytes, const Stamp& when,
                                             std::string* why = nullptr);

} // namespace help::report
