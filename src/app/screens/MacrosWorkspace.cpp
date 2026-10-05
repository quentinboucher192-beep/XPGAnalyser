// =============================================================================
//  app/screens/MacrosWorkspace.cpp - lot macros 1 : l'onglet Macros et ses dialogues
// -----------------------------------------------------------------------------
//  L'ONGLET (MacrosPane) NE POSE AUCUN DIALOGUE : il les demande ici. Nouvelle
//  macro (depuis un modele), nouveau dossier, renommer (les RunMacro qui
//  l'appellent suivent), dupliquer, supprimer (dans la corbeille), restaurer,
//  garder des reponses sous un nom. Et Appliquer : la commande de la macro
//  va sur la pile de l'application - un Ctrl+Z reprend tout.
//
//  L'ARBRE DU PROJET (API > Macros) montre les memes dossiers ; on y glisse une
//  macro ou un dossier dans un autre, comme dans l'onglet.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../MacroFormView.hpp"
#include "../MacroEditorView.hpp"
#include "../MacrosPane.hpp"
#include "../../project/MacroFolders.hpp"
#include "../../project/MacroSpec.hpp"
#include "../../xls/MacroXls.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace app {

using namespace ui;
namespace mm = project::macro;
namespace fs = std::filesystem;
using NK = ProjectTreeModel::NodeKind;

namespace {

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string lowered(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Le modele "Vide", quand libs/Macros/_modeles manque.
const char* const kEmptyTemplate =
    "(* NouvelleMacro\n"
    "\n"
    "   Ce que fait la macro, en deux ou trois phrases : ce qu'elle lit, ce\n"
    "   qu'elle cree, ce qu'elle ne touche pas.\n"
    "\n"
    "   #! summary = Ce que fait la macro, en une phrase.\n"
    "   #! version = 1.00\n"
    "   #! categorie = Mes macros\n"
    "   #! param tache = Les sections cr\xC3\xA9\xC3\xA9" "es vont dans cette t\xC3\xA2" "che.\n"
    "   #! champ tache = tache\n"
    "   #! libelle tache = T\xC3\xA2" "che d'accueil des sections\n"
    "   #! appliquer = Appliquer\n"
    "*)\n"
    "\n"
    "tache := Ask('tache', 'Tache d accueil des sections', 'MAST');\n"
    "AskNow();\n"
    "\n"
    "Log('tache : ' + tache);\n";

struct Template {
    std::string name;       // le nom du fichier (sans .mac) : le premier mot de son en-tete
    std::string label;      // ce que dit la liste
    std::string source;
};

std::vector<Template> templates(const std::string& macrosFolder) {
    std::vector<Template> out;
    std::error_code ec;
    const auto dir = fs::path(macrosFolder) / "_modeles";
    if (fs::is_directory(dir, ec)) {
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".mac") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            Template t;
            t.name = f.stem().string();
            t.source = readAll(f.string());
            const auto spec = mm::parseMacroSpec(t.source, t.name);
            t.label = spec.summary.empty() ? t.name : spec.summary;
            if (t.label.size() > 90) t.label = t.label.substr(0, 87) + "\xE2\x80\xA6";
            out.push_back(std::move(t));
        }
    }
    if (std::none_of(out.begin(), out.end(), [](const Template& t) { return lowered(t.name).find("vide") != std::string::npos; }))
        out.insert(out.begin(), Template{"NouvelleMacro", "Vide : une question, un Log - \xC3\xA0 compl\xC3\xA9ter", kEmptyTemplate});
    return out;
}

// Les macros qui appellent `name` par RunMacro.
std::vector<std::string> callersOf(const project::SharedLibrary& library, const std::string& name) {
    std::vector<std::string> out;
    for (const auto& item : library.items()) {
        if (item.kind != project::LibraryItemKind::Macro || lowered(item.name) == lowered(name)) continue;
        auto source = readAll(item.path);
        if (project::SharedLibrary::replaceRunMacro(source, name, name) > 0) out.push_back(item.name);
    }
    return out;
}

} // namespace

// ================================================================== l'onglet ==
MacrosPane* MainAnalysisScreen::macrosPane() const {
    return dynamic_cast<MacrosPane*>(hmiTab("macros"));
}

mm::MacroMemory& MainAnalysisScreen::macroMemory() {
    if (!macroMemory_) {
        // A cote des reglages : settings.txt tient une valeur par ligne.
        const auto settings = app_.settings().path();
        const auto dir = settings.empty() ? fs::current_path() : fs::path(settings).parent_path();
        macroMemory_ = std::make_unique<mm::MacroMemory>((dir / "macros-memoire.txt").string());
        (void)macroMemory_->load();
    }
    return *macroMemory_;
}

void MainAnalysisScreen::openMacros(const std::string& select, bool launch) {
    if (!centre_) return;
    auto* pane = macrosPane();
    if (!pane) {
        auto made = std::make_unique<MacrosPane>("macros", project::SharedLibrary::defaultRoot(), &macroMemory());
        MacrosPane::Hosts h;
        h.project = [this] { return app_.document(); };
        h.projectKey = [this] {
            const auto folder = app_.projectFolder();
            if (!folder.empty()) return folder;
            auto p = app_.project();
            return p ? p->header.projectName : std::string{};
        };
        h.apply = [this](core::CommandPtr command, const std::string& summary) {
            // Deja execute par la macro : la pile ne le rejoue pas, et un
            // Ctrl+Z reprend tout.
            app_.apply(std::move(command), /*refreshViews=*/false);
            // Lot API 7 : une macro peut changer le programme SANS commande (une
            // mise a jour de la bibliotheque) : la simulation preparee avant se
            // refait au prochain Simuler, et les onglets de l'API se relisent
            // (le tableau de bord disait encore « 5 elements plus recents »).
            app_.noteProgramChanged();
            refreshApiPanes();
            refreshOpenDocuments();
            if (sections_) sections_->invalidate();
            if (treeModel_) treeModel_->refresh();
            if (explorer_) explorer_->invalidate();
            status_->setMessage(summary, StatusBar::Severity::Success);
        };
        h.stillApplied = [this](const void* command) {
            const auto& done = app_.commands().done();
            return std::any_of(done.begin(), done.end(), [command](const auto& e) { return e.command.get() == command; });
        };
        h.refuseApply = [this]() -> std::string {
            if (app_.manifest().state == project::State::Lock && !app_.projectFolder().empty())
                return "le projet est verrouill\xC3\xA9 (LOCK) : Projet > D\xC3\xA9verrouiller d'abord";
            return {};
        };
        h.editCode = [this](const std::string& name) { openMacro(name); };
        // Lot API 6 : Modifier, dans cet onglet.
        h.makeEditor = [this](const std::string& name) { return makeMacroEditor(name); };
        h.help = [this](const std::string& name) { openMacroHelp(name); };
        h.newMacro = [this](const std::string& folder) { askNewMacro(folder); };
        h.newFolder = [this](const std::string& parent) { askNewMacroFolder(parent); };
        h.renameMacro = [this](const std::string& name) { askRenameMacro(name); };
        h.duplicateMacro = [this](const std::string& name) { askDuplicateMacro(name); };
        h.deleteMacro = [this](const std::string& name) { askDeleteMacro(name); };
        h.renameFolder = [this](const std::string& folder) { askRenameMacroFolder(folder); };
        h.deleteFolder = [this](const std::string& folder) { askDeleteMacroFolder(folder); };
        h.restoreMacro = [this](const std::string& name) { restoreMacro(name); };
        h.purgeMacro = [this](const std::string& name) { askPurgeMacro(name); };
        h.saveProfile = [this](const std::string& name, const std::map<std::string, std::string>& answers) {
            askSaveMacroProfile(name, answers);
        };
        h.libraryChanged = [this] { refreshMacroList(); };
        h.status = [this](const std::string& text, bool error) {
            if (!status_) return;
            if (error) status_->setTransientMessage(text, 8.0, StatusBar::Severity::Warning);
            else status_->setMessage(text);
        };
        made->setHosts(std::move(h));
        pane = made.get();
        const auto tab = centre_->addTab(TabControl::Tab{"Macros", Icon::Play, /*closable=*/true, false}, std::move(made));
        hmiTabs_["macros"] = pane;
        centre_->setCurrentIndex(tab);
        pane->reload();     // les hotes sont poses : la fiche sait s'il y a un projet
    } else {
        centre_->setCurrentIndex(static_cast<std::size_t>(centre_->indexOf(pane)));
    }
    if (!select.empty()) {
        if (pane->spec(select)) pane->select(select);
        else pane->selectFolder(select);
    }
    if (launch && !select.empty()) pane->launch(select);
}

void MainAnalysisScreen::macrosChanged(const std::string& select) {
    refreshMacroList();
    refreshHelpLibrary();
    if (auto* pane = macrosPane()) {
        pane->reload();
        if (!select.empty()) {
            if (pane->spec(select)) pane->select(select);
            else pane->selectFolder(select);
        }
    }
}

void MainAnalysisScreen::openMacroHelp(const std::string& name) {
    refreshHelpLibrary();
    openHelpFor(help::targetForWord(helpLibrary(), name));
}

// ============================================================== les dialogues ==
void MainAnalysisScreen::askNewMacro(const std::string& folder) {
    project::SharedLibrary library(project::SharedLibrary::defaultRoot());
    (void)library.scan();
    const auto models = templates(library.macrosFolder());
    std::vector<std::string> modelChoices;
    for (const auto& t : models) modelChoices.push_back(t.label);
    // Les dossiers : ceux de l'onglet.
    std::vector<std::string> folderChoices = {"(\xC3\xA0 la racine)"};
    if (auto* pane = macrosPane())
        for (const auto& f : pane->layout().folders) folderChoices.push_back(f);
    std::string preset = folderChoices.front();
    for (const auto& f : folderChoices)
        if (mm::sameFolder(f, folder)) preset = f;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", "", "lettres, chiffres et _ : ImporterPompes", false, {}});
    fields.push_back({"Mod\xC3\xA8le", modelChoices.front(), "", false, modelChoices});
    fields.push_back({"Dossier", preset, "", false, folderChoices});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroNew", "Nouvelle macro",
            "Elle est \xC3\xA9" "crite dans libs/Macros et rang\xC3\xA9" "e dans le dossier choisi ; son code s'ouvre ensuite. "
            "Les mod\xC3\xA8les viennent de libs/Macros/_modeles : chacun pose d\xC3\xA9j\xC3\xA0 ses lignes #! (le formulaire).",
            std::move(fields), "Cr\xC3\xA9" "er"),
        [this, models](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 3) return;
            const std::string name = v[0];
            const auto check = mm::checkMacroName(name);
            if (check.verdict == mm::Verdict::Error) {
                status_->setTransientMessage("Nouvelle macro : " + check.message, 8.0, StatusBar::Severity::Warning);
                return;
            }
            const Template* model = &models.front();
            for (const auto& t : models)
                if (t.label == v[1]) model = &t;
            const std::string chosen = v[2].rfind("(", 0) == 0 ? std::string{} : v[2];
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            const auto source = project::SharedLibrary::renameInHeader(model->source, model->name, name);
            if (auto st = lib.createMacro(name, source, chosen); !st) {
                status_->setTransientMessage("Nouvelle macro : " + st.error().message(), 8.0, StatusBar::Severity::Warning);
                return;
            }
            macrosChanged(name);
            openMacro(name);
            status_->setMessage(name + " cr\xC3\xA9\xC3\xA9" "e dans libs/Macros" + (chosen.empty() ? std::string{} : ", dossier " + chosen)
                                    + ". Son code est ouvert ; Enregistrer l'\xC3\xA9" "crit.",
                                StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askNewMacroFolder(const std::string& parent) {
    std::vector<std::string> folderChoices = {"(\xC3\xA0 la racine)"};
    if (auto* pane = macrosPane())
        for (const auto& f : pane->layout().folders) folderChoices.push_back(f);
    std::string preset = folderChoices.front();
    for (const auto& f : folderChoices)
        if (mm::sameFolder(f, parent)) preset = f;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", "", "Mes imports", false, {}});
    fields.push_back({"Dans", preset, "", false, folderChoices});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroFolder", "Nouveau dossier de macros",
            "Un dossier range, il ne change rien aux noms : RunMacro trouve une macro o\xC3\xB9 qu'elle soit. "
            "Glisse ensuite des macros dessus.",
            std::move(fields), "Cr\xC3\xA9" "er"),
        [this](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2 || v[0].empty()) return;
            const std::string into = v[1].rfind("(", 0) == 0 ? std::string{} : v[1];
            const std::string path = into.empty() ? v[0] : into + "/" + v[0];
            auto* pane = macrosPane();
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            mm::MacroFolders folders(lib.macroFoldersFile());
            (void)folders.load();
            std::vector<std::pair<std::string, std::string>> known;
            for (const auto& item : lib.items())
                if (item.kind == project::LibraryItemKind::Macro)
                    known.emplace_back(item.name, pane && pane->spec(item.name) ? pane->spec(item.name)->category
                                                                              : mm::parseMacroSpec(readAll(item.path), item.name).category);
            folders.setMacros(std::move(known));
            std::string why;
            if (!folders.addFolder(path, &why)) {
                status_->setTransientMessage("Nouveau dossier : " + why, 8.0, StatusBar::Severity::Warning);
                return;
            }
            if (auto st = folders.save(); !st) {
                status_->setTransientMessage("dossiers.txt : " + st.error().message(), 8.0, StatusBar::Severity::Warning);
                return;
            }
            macrosChanged(path);
            status_->setMessage("Dossier " + path + " cr\xC3\xA9\xC3\xA9 : glisse des macros dessus.", StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askRenameMacro(const std::string& name) {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nouveau nom", name, "lettres, chiffres et _", false, {}});
    project::SharedLibrary library(project::SharedLibrary::defaultRoot());
    (void)library.scan();
    const auto callers = callersOf(library, name);
    std::string explanation = "Le fichier, l'index de libs/ et son dossier suivent. ";
    if (callers.empty()) explanation += "Aucune autre macro ne l'appelle.";
    else {
        explanation += "RunMacro('" + name + "') sera corrig\xC3\xA9 dans : ";
        for (std::size_t i = 0; i < callers.size(); ++i) explanation += (i ? ", " : "") + callers[i];
        explanation += ".";
    }
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroRename", "Renommer " + name, explanation, std::move(fields), "Renommer"),
        [this, name](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v[0].empty() || v[0] == name) return;
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            auto result = lib.renameMacro(name, v[0]);
            if (!result) {
                status_->setTransientMessage("Renommer : " + result.error().message(), 8.0, StatusBar::Severity::Warning);
                return;
            }
            macroMemory().renameMacro(name, v[0]);
            (void)macroMemory().save();
            // L'onglet du code de l'ancien nom ne designe plus rien : il se ferme.
            if (const auto at = macroTabs_.find(name); at != macroTabs_.end()) {
                if (at->second < static_cast<int>(centre_->tabCount())) closeDocument(static_cast<std::size_t>(at->second));
                macroTabs_.erase(name);
            }
            macrosChanged(v[0]);
            std::string said = name + " renomm\xC3\xA9" "e " + v[0];
            if (!result->empty()) {
                said += " ; RunMacro corrig\xC3\xA9 dans ";
                for (std::size_t i = 0; i < result->size(); ++i) said += (i ? ", " : "") + (*result)[i];
            }
            status_->setMessage(said, StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askDuplicateMacro(const std::string& name) {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom de la copie", name + "_copie", "lettres, chiffres et _", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroDuplicate", "Dupliquer " + name,
            "La copie est rang\xC3\xA9" "e dans le m\xC3\xAA" "me dossier ; l'originale n'est pas touch\xC3\xA9" "e.", std::move(fields), "Dupliquer"),
        [this, name](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v[0].empty()) return;
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            if (auto st = lib.duplicateMacro(name, v[0]); !st) {
                status_->setTransientMessage("Dupliquer : " + st.error().message(), 8.0, StatusBar::Severity::Warning);
                return;
            }
            macrosChanged(v[0]);
            status_->setMessage(v[0] + " : copie de " + name + ".", StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askDeleteMacro(const std::string& name) {
    project::SharedLibrary library(project::SharedLibrary::defaultRoot());
    (void)library.scan();
    const auto callers = callersOf(library, name);
    std::string message = name + " va dans la corbeille (libs/Macros/_corbeille) : Restaurer la remet dans son dossier.";
    if (!callers.empty()) {
        message += "\n\nAttention : ";
        for (std::size_t i = 0; i < callers.size(); ++i) message += (i ? ", " : "") + callers[i];
        message += callers.size() > 1 ? " l'appellent" : " l'appelle";
        message += " (RunMacro) et s'arr\xC3\xAAteront sans elle.";
    }
    app_.menus().ShowDialog(std::make_unique<MessageDialog>("Supprimer " + name, message, MessageDialog::Icon::Question, "Supprimer"),
                            [this, name](const menu::DialogResult& r) {
                                if (!r.accepted()) return;
                                project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
                                (void)lib.scan();
                                if (auto st = lib.deleteMacro(name); !st) {
                                    status_->setTransientMessage("Supprimer : " + st.error().message(), 8.0, StatusBar::Severity::Warning);
                                    return;
                                }
                                if (auto* pane = macrosPane(); pane && pane->step() != 0) pane->closeRun();
                                macrosChanged();
                                status_->setMessage(name + " est dans la corbeille : Restaurer la remet.", StatusBar::Severity::Success);
                            });
}

void MainAnalysisScreen::askRenameMacroFolder(const std::string& folder) {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nouveau nom", mm::folderLeaf(folder), "", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroFolderRename", "Renommer le dossier " + mm::folderLeaf(folder),
            "Ses macros et ses sous-dossiers suivent ; leurs noms ne changent pas.", std::move(fields), "Renommer"),
        [this, folder](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v[0].empty()) return;
            const auto parent = mm::folderParent(folder);
            const auto target = parent.empty() ? v[0] : parent + "/" + v[0];
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            mm::MacroFolders folders(lib.macroFoldersFile());
            (void)folders.load();
            std::vector<std::pair<std::string, std::string>> known;
            for (const auto& item : lib.items())
                if (item.kind == project::LibraryItemKind::Macro)
                    known.emplace_back(item.name, mm::parseMacroSpec(readAll(item.path), item.name).category);
            folders.setMacros(std::move(known));
            std::string why;
            if (!folders.renameFolder(folder, target, &why)) {
                status_->setTransientMessage("Renommer le dossier : " + why, 8.0, StatusBar::Severity::Warning);
                return;
            }
            (void)folders.save();
            macrosChanged(target);
            status_->setMessage("Dossier renomm\xC3\xA9 : " + target, StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askDeleteMacroFolder(const std::string& folder) {
    std::size_t count = 0;
    if (auto* pane = macrosPane()) count = pane->layout().countIn(folder, true);
    const auto parent = mm::folderParent(folder);
    const std::string where = parent.empty() ? std::string("\xC3\xA0 la racine") : "dans " + parent;
    const std::string message = count == 0 ? "Le dossier est vide."
                              : "Ses " + std::to_string(count) + " macro(s) et ses sous-dossiers remontent " + where
                                    + " : aucune macro n'est supprim\xC3\xA9" "e.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer le dossier " + mm::folderLeaf(folder), message, MessageDialog::Icon::Question, "Supprimer"),
        [this, folder](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            mm::MacroFolders folders(lib.macroFoldersFile());
            (void)folders.load();
            std::vector<std::pair<std::string, std::string>> known;
            for (const auto& item : lib.items())
                if (item.kind == project::LibraryItemKind::Macro)
                    known.emplace_back(item.name, mm::parseMacroSpec(readAll(item.path), item.name).category);
            folders.setMacros(std::move(known));
            std::string why;
            if (!folders.removeFolder(folder, &why)) {
                status_->setTransientMessage("Supprimer le dossier : " + why, 8.0, StatusBar::Severity::Warning);
                return;
            }
            (void)folders.save();
            macrosChanged(mm::folderParent(folder));
            status_->setMessage("Dossier " + folder + " supprim\xC3\xA9.", StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::restoreMacro(const std::string& name) {
    project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
    (void)lib.scan();
    if (auto st = lib.restoreMacro(name); !st) {
        status_->setTransientMessage("Restaurer : " + st.error().message(), 8.0, StatusBar::Severity::Warning);
        return;
    }
    macrosChanged(name);
    status_->setMessage(name + " restaur\xC3\xA9" "e dans son dossier.", StatusBar::Severity::Success);
}

void MainAnalysisScreen::askPurgeMacro(const std::string& name) {
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer " + name + " d\xC3\xA9" "finitivement",
                                        "Le fichier est effac\xC3\xA9 de la corbeille : il ne pourra plus \xC3\xAAtre restaur\xC3\xA9.",
                                        MessageDialog::Icon::Warning, "Supprimer d\xC3\xA9" "finitivement"),
        [this, name](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            if (auto st = lib.purgeMacro(name); !st) {
                status_->setTransientMessage(st.error().message(), 8.0, StatusBar::Severity::Warning);
                return;
            }
            macrosChanged();
            status_->setMessage(name + " effac\xC3\xA9" "e de la corbeille.", StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askSaveMacroProfile(const std::string& name, std::map<std::string, std::string> answers) {
    answers.erase("confirm");
    std::vector<FormDialog::Field> fields;
    std::string preset;
    if (auto p = app_.project()) preset = p->header.projectName;
    fields.push_back({"Nom du profil", preset, "Affaire 2024_06_264", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroProfile", "Garder ces r\xC3\xA9ponses",
            "La fiche de " + name + " proposera ensuite de la lancer avec ces r\xC3\xA9ponses, dans n'importe quel projet.",
            std::move(fields), "Garder"),
        [this, name, answers](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v[0].empty()) return;
            macroMemory().saveProfile(name, v[0], answers);
            (void)macroMemory().save();
            if (auto* pane = macrosPane()) pane->reload();
            status_->setMessage("Profil \xC2\xAB " + v[0] + " \xC2\xBB gard\xC3\xA9 pour " + name + ".", StatusBar::Severity::Success);
        });
}

// ================================================ l'arbre : glisser-deposer ==
//  Une macro ou un dossier de macros, glisse sur un dossier de macros (ou sur
//  le dossier Macros lui-meme : la racine) ; une macro entre deux macros. Les
//  predicats d'avant (l'ordre d'execution, les dossiers de l'IHM) restent.
void MainAnalysisScreen::wireMacroTreeDrag() {
    if (!explorer_ || !treeModel_) return;
    using Where = TreeView::DropWhere;
    const auto before = explorer_->dragPredicate();
    const auto beforeDrop = explorer_->dropPredicate();
    const auto isMacroNode = [this](NodeId n) {
        std::string f;
        return ProjectTreeModel::kindOf(n) == NK::Macro || (treeModel_ && treeModel_->macroFolderOf(n, f) && !f.empty());
    };
    explorer_->setDragPredicate([before, isMacroNode](NodeId n) { return isMacroNode(n) || (before && before(n)); });
    explorer_->setDropPredicate([this, beforeDrop, isMacroNode](NodeId dragged, NodeId target, Where where) {
        if (!isMacroNode(dragged)) return beforeDrop && beforeDrop(dragged, target, where);
        std::string targetFolder;
        const bool intoFolder = treeModel_->macroFolderOf(target, targetFolder);
        bool anyFolder = false;
        for (const auto n : explorer_->draggedNodes()) {
            if (!isMacroNode(n)) return false;
            std::string f;
            if (treeModel_->macroFolderOf(n, f)) {
                anyFolder = true;
                if (intoFolder && mm::insideFolder(targetFolder, f)) return false;
            }
        }
        if (intoFolder) return where == Where::Into;
        return ProjectTreeModel::kindOf(target) == NK::Macro && !anyFolder && where != Where::Into;
    });
    execOrderLinks_ += explorer_->dropped->connect([this, isMacroNode](NodeId dragged, NodeId target, Where where) {
        if (!isMacroNode(dragged)) return;
        auto nodes = explorer_->draggedNodes();
        if (nodes.empty()) nodes.push_back(dragged);
        std::vector<std::string> macros, folderPaths;
        for (const auto n : nodes) {
            std::string f;
            if (treeModel_->macroFolderOf(n, f)) folderPaths.push_back(f);
            else if (ProjectTreeModel::kindOf(n) == NK::Macro) macros.push_back(treeModel_->text(n));
        }
        project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
        (void)lib.scan();
        mm::MacroFolders folders(lib.macroFoldersFile());
        (void)folders.load();
        std::vector<std::pair<std::string, std::string>> known;
        for (const auto& item : lib.items())
            if (item.kind == project::LibraryItemKind::Macro)
                known.emplace_back(item.name, mm::parseMacroSpec(readAll(item.path), item.name).category);
        folders.setMacros(std::move(known));
        std::string targetFolder, said;
        if (treeModel_->macroFolderOf(target, targetFolder)) {
            std::size_t moved = 0;
            std::string why;
            for (const auto& f : folderPaths)
                if (!mm::sameFolder(mm::folderParent(f), targetFolder) && folders.moveFolder(f, targetFolder, &why)) ++moved;
            moved += folders.moveMacros(macros, targetFolder);
            if (moved == 0) {
                if (!why.empty()) status_->setTransientMessage("Rien n'est d\xC3\xA9plac\xC3\xA9 : " + why, 6.0);
                return;
            }
            said = std::to_string(moved) + " rang\xC3\xA9(s) dans " + (targetFolder.empty() ? std::string("Macros") : targetFolder);
        } else if (ProjectTreeModel::kindOf(target) == NK::Macro) {
            if (!folders.placeNear(macros, treeModel_->text(target), where == Where::After)) return;
            said = std::to_string(macros.size()) + " macro(s) plac\xC3\xA9" "e(s) " + (where == Where::After ? "apr\xC3\xA8s " : "avant ")
                 + treeModel_->text(target);
        } else {
            return;
        }
        if (auto st = folders.save(); !st) {
            status_->setTransientMessage("dossiers.txt : " + st.error().message(), 8.0);
            return;
        }
        macrosChanged();
        status_->setTransientMessage(said + " (libs/Macros/dossiers.txt)", 8.0, StatusBar::Severity::Success);
    });
}

} // namespace app
