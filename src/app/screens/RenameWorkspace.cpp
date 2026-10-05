// =============================================================================
//  app/screens/RenameWorkspace.cpp - lot 7 : renommer, en voyant tout ce qui suit
// -----------------------------------------------------------------------------
//  L'ecran relie le dialogue (RenameDialog) au projet : le plan se calcule sur
//  le programme ouvert et le document de l'IHM (project/RenamePlan +
//  makeHmiRenameSide), la commande passe par App::apply - un seul pas dans
//  l'historique, un seul Ctrl+Z, pour les deux cotes.
//
//  Les volets demandent un renommage par requestRename (RenameDialog.hpp) ; le
//  crochet est pose ici, une fois pour toute l'application : il ouvre le
//  dialogue sur l'ecran d'analyse s'il est au-dessus.
// =============================================================================
#include "Screens.hpp"

#include "../AnimationTablesPane.hpp"
#include "../ApiPanes.hpp"
#include "../App.hpp"
#include "../RenameDialog.hpp"
#include "../TypePanes.hpp"
#include "../VariablesPane.hpp"
#include "../hmi/HmiScriptPanes.hpp"         // lot API 8 : la variable IHM renommee reste choisie
#include "../hmi/HmiVariablePanes.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../project/RenamePlan.hpp"

#include <memory>
#include <algorithm>     // lot API 8 : les forcages enregistres (voir forcingFiles)
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace app {

namespace pr = project::rename;

namespace {

// Le volet d'un onglet de l'API (nul : pas ouvert, ou pas de ce genre).
template <typename Pane>
Pane* renamePaneOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<Pane*>(&frame->content()) : nullptr;
}

// Le crochet des volets : l'ecran d'analyse, s'il est au-dessus, ouvre le
// dialogue. Pose au chargement du programme (ce fichier n'est lie que dans
// l'application : les essais qui compilent les volets seuls n'en ont pas).
const bool kRenameHook = [] {
    renameRequestHook() = [](const std::string& kind, const std::string& name, const std::string& newName) {
        auto* manager = menu::MenuManager::instance();
        auto* screen = manager ? dynamic_cast<MainAnalysisScreen*>(manager->top()) : nullptr;
        if (!screen) return false;
        std::string why;
        // Pas de dialogue (un genre inconnu, un nom introuvable) : askRename le
        // dit dans la barre d'etat.
        if (newName.empty() || !screen->askRenameTo(kind, name, newName, &why)) screen->askRename(kind, name);
        return true;
    };
    return true;
}();

std::string plural(std::size_t n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

// ---- Lot API 8 : renommer - ce qui cite l'ancien nom A COTE du projet ----------------
//  * Les forcages enregistres de la simulation (<dossier du projet>/simulation/*.txt,
//    une ligne "nom = valeur" : SimulationPane) : une variable (globale ou
//    d'unite), une unite, un champ de DDT renommes y suivent (sans casse, comme
//    Control Expert ; un champ : les chemins qui passent par lui). Montres dans
//    l'onglet API, ecrits et defaits avec la commande (un seul Ctrl+Z). Un
//    fichier qui ne s'ecrit pas ne bloque rien : l'import dit les noms inconnus.
//  * Les dispositions des grafcets (<projet>.xpglayout, par grafcet : la section
//    sans SFC_) : une section renommee garde ses positions, copiees sous le
//    nouveau nom (l'ancienne reste : apres Ctrl+Z, le grafcet la retrouve).
std::filesystem::path pathOfUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string utf8Of(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Une ligne "nom = valeur" : pr::renameForcingLine (project/RenamePlan.cpp, essaye
// par renameplan_test) ; ici, les fichiers, le plan montre, la commande.
std::vector<std::string> linesOfText(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto nl = text.find('\n', start);
        const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
        out.push_back(text.substr(start, end - start));
        start = end;
    }
    return out;
}

struct ForcingFile {
    std::filesystem::path path;
    std::string           name;             // pour le plan : "forcages.txt"
    std::string           before, after;
};

// Les fichiers de forcages que ce plan change (aucun : pas de dossier de projet,
// pas de variable globale renommee, rien ne la cite).
std::vector<ForcingFile> forcingFiles(const std::string& projectFolder, const pr::Plan& plan) {
    std::vector<ForcingFile> out;
    std::vector<const pr::Rename*> roots;
    for (const auto& r : plan.renames)
        if (pr::citedByForcings(r)) roots.push_back(&r);
    if (projectFolder.empty() || roots.empty()) return out;
    std::error_code ec;
    const auto folder = pathOfUtf8(projectFolder) / "simulation";
    if (!std::filesystem::is_directory(folder, ec)) return out;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe) || it->path().extension() != ".txt") continue;
        std::ifstream in(it->path(), std::ios::binary);
        if (!in) continue;
        ForcingFile f;
        f.path = it->path();
        f.name = utf8Of(it->path().filename());
        f.before.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        for (auto line : linesOfText(f.before)) {
            std::string eol;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                eol.insert(eol.begin(), line.back());
                line.pop_back();
            }
            for (const auto* r : roots) line = pr::renameForcingLine(line, *r);
            f.after += line + eol;
        }
        if (f.after != f.before) out.push_back(std::move(f));
    }
    std::sort(out.begin(), out.end(), [](const ForcingFile& a, const ForcingFile& b) { return a.path < b.path; });
    return out;
}

// Les lignes changees, dans le plan (onglet API : le cote "Tables" est celui des
// tables d'animation).
void showForcings(pr::Plan& plan, const std::vector<ForcingFile>& files) {
    for (const auto& f : files) {
        const auto a = linesOfText(f.before), b = linesOfText(f.after);
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i] == b[i]) continue;
            const auto bare = [](std::string s) {
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                return s;
            };
            pr::Change c;
            c.tab = pr::Tab::Api;
            c.path = {"Simulation \xE2\x80\xBA for\xC3\xA7" "ages enregistr\xC3\xA9s", f.name};
            c.label = "ligne " + std::to_string(i + 1);
            c.line = static_cast<int>(i + 1);
            c.before = bare(a[i]);
            c.after = bare(b[i]);
            plan.changes.push_back(std::move(c));
        }
    }
}

// Ecrit (faire) ou remet (defaire) les fichiers de forcages.
class ForcingFilesCommand final : public core::ICommand {
public:
    ForcingFilesCommand(std::vector<ForcingFile> files, std::string label) : files_(std::move(files)), label_(std::move(label)) {}
    core::Status execute() override { return write(true); }
    core::Status undo() override { return write(false); }
    [[nodiscard]] std::string label() const override { return label_; }

private:
    core::Status write(bool after) {
        for (const auto& f : files_) {
            std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
            const std::string& text = after ? f.after : f.before;
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        return core::ok();
    }
    std::vector<ForcingFile> files_;
    std::string              label_;
};

// Le nom d'un grafcet : sa section sans SFC_ (project/Grafcet.cpp).
std::string chartKey(const std::string& section) {
    return section.size() > 4 && sameNoCase(std::string_view(section).substr(0, 4), "SFC_") ? section.substr(4) : section;
}
// ---- fin Lot API 8 : renommer - a cote du projet ----

} // namespace

void MainAnalysisScreen::askRename(const std::string& kind, const std::string& name) {
    (void)kRenameHook;
    std::string why;
    if (askRenameTo(kind, name, {}, &why) || !status_) return;
    status_->dismissTransient();
    status_->setMessage("Renommer : " + why);
}

bool MainAnalysisScreen::askRenameTo(const std::string& kindKey, const std::string& name, const std::string& newName, std::string* why) {
    const auto fail = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const auto kind = pr::kindFromKey(kindKey);
    if (!kind) return fail("\xC2\xAB " + kindKey + " \xC2\xBB ne se renomme pas ici (genres : " + std::string(pr::supportedKinds()) + ")");
    const auto project = app_.project();
    // Lot API 8 : une variable IHM, une vue se renomment aussi sans programme ouvert
    // (le plan n'a alors que sa moitie IHM ; sinon le nom tape ne ferait rien).
    if (!project && !pr::isHmiKind(*kind)) return fail("aucun projet ouvert");
    // La moitie IHM vit autant que le dialogue (ses fonctions la gardent).
    const std::shared_ptr<pr::HmiSide> hmi(makeHmiRenameSide(app_.hmi()));
    const auto first = pr::makePlan(project.get(), hmi.get(), *kind, name, {}, false);
    if (!first.found()) return fail(first.problem);
    const pr::Target target = first.target;
    const pr::Kind k = *kind;

    RenameDialog::Spec spec;
    spec.kind = k;
    spec.oldName = name;
    spec.initial = newName;
    spec.plan = [this, hmi, k, name](const std::string& n, bool withLinks) {
        auto plan = pr::makePlan(app_.project().get(), hmi.get(), k, name, n, withLinks);
        // ---- Lot API 8 : a cote du projet - les forcages enregistres, la disposition d'un grafcet ----
        showForcings(plan, forcingFiles(app_.projectFolder(), plan));
        for (const auto& r : plan.renames) {
            if (r.target.kind != pr::Kind::Section) continue;
            const auto from = chartKey(r.target.name), to = chartKey(r.to);
            const auto* layout = layoutFile_.find(from);
            if (!layout || layout->empty() || from == to) continue;
            pr::Change c;
            c.tab = pr::Tab::Api;
            c.path = {"Grafcet " + from, "disposition (positions des \xC3\xA9tapes)"};
            c.label = "grafcet";
            c.before = "chart " + from;
            c.after = "chart " + to;
            plan.changes.push_back(std::move(c));
        }
        // ---- fin Lot API 8 ----
        return plan;
    };
    spec.check = [this, hmi, target](const std::string& n, pr::Verdict* verdict) {
        return pr::nameProblem(app_.project().get(), hmi.get(), target, n, verdict);
    };
    // Ce que dit la barre d'etat une fois le dialogue ferme (apres le
    // rafraichissement des volets, qui la reecrit).
    auto said = std::make_shared<std::string>();
    const bool editable = static_cast<bool>(app_.document());
    if (editable || pr::isHmiKind(k)) {
        spec.apply = [this, hmi, k, name, said](const pr::Plan& shown, std::string* w) {
            // Le plan de nouveau, sur le projet de cet instant : ce qu'on fait est
            // exactement ce qui vient d'etre montre, ou le refus dit pourquoi.
            const auto plan = pr::makePlan(app_.project().get(), hmi.get(), k, name, shown.newName, shown.withLinks);
            if (!plan.ok()) {
                if (w) *w = plan.problem;
                return false;
            }
            const auto doc = app_.document();
            if (plan.touchesApi() && !doc) {
                if (w) *w = "le programme est un export lu tel quel : enregistre le projet en dossier pour le modifier";
                return false;
            }
            auto cmd = pr::makeCommand(doc, hmi.get(), plan);
            if (!cmd) {
                if (w) *w = "rien \xC3\xA0 faire";
                return false;
            }
            // Lot API 8 : les forcages enregistres suivent, dans la meme commande (un seul Ctrl+Z).
            if (auto files = forcingFiles(app_.projectFolder(), plan); !files.empty()) {
                auto group = std::make_unique<core::GroupCommand>(cmd->label());
                group->add(std::move(cmd));
                group->add(std::make_unique<ForcingFilesCommand>(std::move(files), group->label()));
                cmd = std::move(group);
            }
            const auto before = app_.commands().revision();
            app_.apply(std::move(cmd), /*refreshViews=*/plan.touchesApi());
            if (app_.commands().revision() == before) {
                // App::apply a dit pourquoi (un projet verrouille, un refus).
                if (w) *w = "la modification n'a pas \xC3\xA9t\xC3\xA9 faite";
                return false;
            }
            // Lot API 8 : un grafcet renomme garde sa disposition (copiee ; l'ancienne
            // reste, que Ctrl+Z retrouve - une entree de trop ne coute rien).
            for (const auto& r : plan.renames) {
                if (r.target.kind != pr::Kind::Section) continue;
                const auto from = chartKey(r.target.name), to = chartKey(r.to);
                const auto* layout = layoutFile_.find(from);
                if (!layout || layout->empty() || from == to || layoutFile_.find(to)) continue;
                const project::ChartLayout copy = *layout;
                layoutFile_.at(to) = copy;
                saveLayoutFile();
            }
            const auto api = plan.count(pr::Tab::Api) + plan.count(pr::Tab::Tables), ihm = plan.count(pr::Tab::Hmi);
            std::string text = plan.target.display + " s'appelle maintenant " + plan.newName;
            if (plan.renames.size() > 1)
                for (std::size_t i = 1; i < plan.renames.size(); ++i)
                    text += " (et " + plan.renames[i].target.name + " \xE2\x86\x92 " + plan.renames[i].to + ")";
            text += " : " + plural(api, "changement", "changements") + " dans le programme";
            if (ihm) text += ", " + std::to_string(ihm) + " dans l'IHM";
            text += ". Un seul Ctrl+Z d\xC3\xA9" "fait tout.";
            *said = std::move(text);
            return true;
        };
        spec.note = "Entr\xC3\xA9" "e : confirmer \xC2\xB7 \xC3\x89" "chap : annuler \xC2\xB7 le coin en bas \xC3\xA0 droite agrandit la fen\xC3\xAA" "tre";
    } else {
        spec.note = "Export lu tel quel : ce qui changerait est montr\xC3\xA9, mais rien ne peut \xC3\xAAtre renomm\xC3\xA9. "
                    "Enregistre d'abord le projet en dossier.";
    }

    app_.menus().ShowDialog(std::make_unique<RenameDialog>(std::move(spec)), [this, k, target, said](const menu::DialogResult& r) {
        if (!r.accepted() || r.payload.empty()) return;
        // Les volets viennent de se refaire (ProjectOpened passe avant la
        // fermeture) : la ligne renommee reste choisie.
        const std::string& to = r.payload;
        switch (k) {
            case pr::Kind::Variable:
                if (target.global) {
                    if (auto* pane = renamePaneOf<VariablesPane>(apiTab("variables"))) (void)pane->selectVariable(to);
                } else if (auto* units = renamePaneOf<UnitsPane>(apiTab("unites")); units && app_.project()
                           && target.unit < app_.project()->pous.size()) {
                    (void)units->selectVariable(app_.project()->strings.text(app_.project()->pous[target.unit].name), to);
                }
                break;
            case pr::Kind::Ddt:
                if (auto* pane = renamePaneOf<DerivedTypesPane>(apiTab("types"))) (void)pane->selectType(to);
                break;
            case pr::Kind::Dfb:
                if (auto* pane = renamePaneOf<DfbPane>(apiTab("dfb"))) (void)pane->selectBlock(to);
                break;
            case pr::Kind::Unit:
                if (auto* pane = renamePaneOf<UnitsPane>(apiTab("unites"))) (void)pane->selectUnit(to);
                break;
            case pr::Kind::Section:
                if (auto* pane = renamePaneOf<UnitsPane>(apiTab("unites"))) (void)pane->selectSection(to);
                break;
            case pr::Kind::Table:
                if (auto* pane = renamePaneOf<AnimationTablesPane>(apiTab("tables"))) (void)pane->selectTable(std::string_view(to));
                break;
            case pr::Kind::DdtField:
                // Lot API 8 : le champ renomme reste choisi (ses membres deplies le suivent).
                if (auto* pane = renamePaneOf<DerivedTypesPane>(apiTab("types"))) (void)pane->selectField(target.typeName, to, target.name);
                break;
            case pr::Kind::HmiVariable:
                // Lot API 8 : la ligne renommee reste choisie dans IHM > Variables IHM.
                if (auto* scripts = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts")); scripts && scripts->variablesPane() && app_.hmi())
                    if (const auto* v = app_.hmi()->project.variable(to)) scripts->variablesPane()->selectVariable(v->id);
                break;
            case pr::Kind::HmiView:
                break;
        }
        if (status_ && !said->empty()) {
            status_->dismissTransient();
            status_->setMessage(*said);
        }
    });
    if (why) why->clear();
    return true;
}

} // namespace app
