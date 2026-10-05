#include "AboutDialog.hpp"

#include "App.hpp"
#include "Dossiers.hpp"
#include "screens/Screens.hpp"
#include "../core/Version.hpp"
#include "../hmi/HmiCommands.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace app {

    namespace {

        // __DATE__ ("Oct  2 2026") -> "02/10/2026", comme la pastille de l'accueil.
        std::string buildDate() {
            const std::string_view d = __DATE__;
            const std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
            const auto m = d.size() >= 11 ? months.find(d.substr(0, 3)) : std::string_view::npos;
            if (m == std::string_view::npos) return std::string(d);
            const int day = std::atoi(std::string(d.substr(4, 2)).c_str());
            char buf[32];
            std::snprintf(buf, sizeof buf, "%02d/%02d/%.4s", day % 100, static_cast<int>(m / 3 + 1), d.substr(7, 4).data());
            return std::string(buf) + " \xC3\xA0 " + std::string(__TIME__).substr(0, 5);
        }

        std::string systemName() {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__linux__)
            return "Linux";
#else
            return "autre syst\xC3\xA8me";
#endif
        }

        std::string compilerName() {
#if defined(_MSC_VER)
            return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
            return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
            return std::string("GCC ") + __VERSION__;
#else
            return "compilateur inconnu";
#endif
        }

        std::string foldersText() {
            const auto& e = dossiers::actuel();
            std::string out;
            for (const auto cle : dossiers::kCles)
                out += "  " + std::string(dossiers::libelle(cle)) + " : " + e[cle].chemin + "\n";
            if (!e.reglages.empty())  out += "  R\xC3\xA9glages : " + e.reglages + "\n";
            if (!e.programme.empty()) out += "  Programme : " + e.programme + "\n";
            return out;
        }

        // Le projet ouvert, sans ses donnees : son nom et ce qu'il contient.
        std::string projectText(const App& app) {
            const auto project = app.project();
            if (!project) return "  aucun projet ouvert\n";
            std::string name = project->header.projectName;
            if (const auto& folder = app.projectFolder(); !folder.empty()) {
                const auto slash = folder.find_last_of("/\\");
                name = slash == std::string::npos ? folder : folder.substr(slash + 1);
            }
            std::string out = "  " + (name.empty() ? std::string("(sans nom)") : name) + " : "
                            + std::to_string(project->sections.size()) + " sections";
            if (const auto hmi = app.hmi()) out += ", " + std::to_string(hmi->project.views.size()) + " vues de l'IHM";
            if (!project->header.productVersion.empty()) out += " ; Control Expert " + project->header.productVersion;
            return out + "\n";
        }

    } // namespace

    std::string aboutText(const App& app) {
        std::string out;
        out += std::string(XPG_ANALYZER_NAME) + " " + XPG_ANALYZER_VERSION + "\n";
        out += "Construit le " + buildDate() + " (" + compilerName() + ", " + systemName() + ")\n\n";
        out += "Les dossiers de l'application :\n" + foldersText() + "\n";
        out += "Le projet ouvert :\n" + projectText(app) + "\n";
        out += "Composants libres embarqu\xC3\xA9s et leurs licences : SDL3 (zlib), miniz (MIT), nanosvg (zlib), "
               "stb (domaine public), minimp3 (CC0).\n\n";
        out += "\xC2\xAB Copier les informations pour le support \xC2\xBB met dans le presse-papiers la version, "
               "le syst\xC3\xA8me, les dossiers et le r\xC3\xA9sum\xC3\xA9 du projet ouvert (nom, nombre de sections et de vues), "
               "jamais ses donn\xC3\xA9" "es.";
        return out;
    }

    std::string supportInformation(const App& app) {
        std::string out;
        out += std::string(XPG_ANALYZER_NAME) + " " + XPG_ANALYZER_VERSION + "\n";
        out += "Construit le " + buildDate() + " (" + compilerName() + ")\n";
        out += "Syst\xC3\xA8me : " + systemName() + "\n";
        out += "Dossiers :\n" + foldersText();
        out += "Projet ouvert :\n" + projectText(app);
        return out;
    }

    void showAboutDialog(App& app) {
        auto dialog = std::make_unique<MessageDialog>("\xC3\x80 propos d'XPGAnalyser", aboutText(app),
                                                      MessageDialog::Icon::Info,
                                                      "Copier les informations pour le support");
        dialog->setCancelLabel("Fermer");
        app.menus().ShowDialog(std::move(dialog), [&app](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            ui::setClipboardText(supportInformation(app));
            app.events().publish(StatusNotice{
                "Informations pour le support copi\xC3\xA9" "es : version, syst\xC3\xA8me, dossiers, projet (sans ses donn\xC3\xA9" "es).",
                6.0});
        });
    }

} // namespace app
