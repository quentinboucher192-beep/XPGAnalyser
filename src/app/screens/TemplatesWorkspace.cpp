// =============================================================================
//  app/screens/TemplatesWorkspace.cpp - les modeles de vues, exporter et
//  importer des vues (lot 20)
// -----------------------------------------------------------------------------
//  NOUVELLE VUE ouvre la galerie (HmiTemplateGallery) : les modeles de
//  l'application, MES MODELES (un dossier a cote des reglages : tous mes
//  projets les voient) et LES MODELES DU PROJET (dans le projet : ils voyagent
//  avec lui). Creer depuis un modele passe par hmi::pkg::instantiate, dans UNE
//  commande : Ctrl+Z retire la vue et ce qu'elle a apporte.
//
//  ENREGISTRER COMME MODELE : le nom, la categorie, la description, ou le
//  garder, et si les variables se choisiront a la creation.
//
//  EXPORTER LES VUES : les vues cochees et ce qu'elles emportent, dans
//  exports/<nom>.xpgvues. IMPORTER DES VUES : le paquet, compare au projet
//  (HmiImportDialog), puis tout en une commande.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../Settings.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiImportDialog.hpp"
#include "../hmi/HmiPanes.hpp"
#include "../hmi/HmiTemplateGallery.hpp"
#include "../../hmi/HmiDesign.hpp"
#include "../../hmi/HmiPackage.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../hmi/HmiTemplates.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

namespace app {

namespace fs = std::filesystem;
using Entry = HmiTemplateGallery::Entry;

namespace {

hmi::Id asId(std::uint64_t v) { return static_cast<hmi::Id>(v); }

// "Armoire gaz (4 cartes)" -> "Armoire_gaz_4_cartes" : un nom de fichier sans surprise.
std::string fileStem(const std::string& name) {
    std::string out;
    for (const char ch : name) {
        const auto u = static_cast<unsigned char>(ch);
        if (std::isalnum(u) || ch == '-' || ch == '_') out += ch;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("modele") : out;
}

// "2026-09-24 10:12" -> "24/09".
std::string shortDate(const std::string& stamp) {
    if (stamp.size() >= 10 && stamp[4] == '-' && stamp[7] == '-') return stamp.substr(8, 2) + "/" + stamp.substr(5, 2);
    return stamp;
}

void describeVariables(Entry& e, const hmi::pkg::Package& pkg, const hmi::Project& project) {
    const auto vars = hmi::pkg::templateVariables(pkg);
    if (vars.empty()) {
        e.variables = "Variables : aucune";
        return;
    }
    std::size_t missing = 0;
    for (const auto& n : vars)
        if (!project.variable(n)) ++missing;
    e.variables = "Variables : " + std::to_string(vars.size())
                + (missing == 0 ? std::string(" \xE2\x9C\x93 toutes pr\xC3\xA9sentes")
                                : " \xE2\x80\x94 " + std::to_string(missing) + " absente(s) : cr\xC3\xA9\xC3\xA9" "e(s) avec la vue");
    if (pkg.manifest.askVariables) e.variables += " \xC2\xB7 \xC3\xA0 choisir \xC3\xA0 la cr\xC3\xA9" "ation";
    e.variablesOk = missing == 0;
}

Entry packageEntry(std::string key, int source, const hmi::pkg::Package& pkg, const hmi::Project& project) {
    Entry e;
    e.key = std::move(key);
    e.source = source;
    const auto* v = hmi::pkg::mainView(pkg);
    e.name = !pkg.manifest.name.empty() ? pkg.manifest.name : v ? v->name : std::string("Mod\xC3\xA8le");
    e.category = pkg.manifest.category;
    e.description = pkg.manifest.description;
    if (!pkg.manifest.created.empty())
        e.origin = "Enregistr\xC3\xA9 le " + shortDate(pkg.manifest.created)
                 + (pkg.manifest.fromProject.empty() ? std::string{} : " depuis " + pkg.manifest.fromProject) + ".";
    e.contents = "Emporte : " + hmi::pkg::contentsText(pkg);
    describeVariables(e, pkg, project);
    e.preview = std::make_shared<hmi::Project>(pkg.content);
    if (v) {
        e.previewView = v->id;
        e.role = v->role;
        // 1.10.2 : un modele enregistre va avec son role (une popup : en tete
        // pour une popup ; 1.10.3 : seulement pour une popup).
        e.suits = {v->role.empty() ? std::string("vue") : v->role};
        e.width = v->width;
        e.height = v->height;
        e.viewDescription = v->description;
    }
    return e;
}

// Les modeles de l'application : une vue neuve remplie par fillFromTemplate,
// pour la vignette.
std::vector<Entry> appEntries(const hmi::Project& project, const std::string& role) {
    std::vector<Entry> out;
    for (const auto& t : hmi::design::viewTemplates()) {
        Entry e;
        e.key = "app:" + std::string(t.key);
        e.source = 0;
        e.name = std::string(t.label);
        e.description = std::string(t.description);
        // 1.10.2 : le role que le modele impose (une popup, un symbole...), et
        // ceux pour lesquels la galerie le propose en tete.
        e.role = t.role.empty() ? role : std::string(t.role);
        for (const auto r : t.suits) e.suits.emplace_back(r);
        if (t.key != "vide") e.viewDescription = std::string(t.description);
        int w = project.config.width, h = project.config.height;
        if (e.role == "popup") { w = std::min(640, w); h = std::min(400, h); }
        else if (e.role == "entete" || e.role == "pied") h = 80;
        else if (e.role == "symbole") { w = 240; h = 160; }
        // 1.10.2 : la taille du modele, s'il en a une (bornee par celle du projet).
        if (t.width > 0) w = std::min(t.width, project.config.width);
        if (t.height > 0) h = std::min(t.height, project.config.height);
        e.width = w;
        e.height = h;
        e.contents = "Emporte : rien (des objets de l'application)";
        auto preview = std::make_shared<hmi::Project>();
        preview->config = project.config;
        hmi::View v = hmi::makeView(*preview, e.name);
        v.width = w;
        v.height = h;
        v.role = e.role;
        if (t.key != "vide") hmi::design::fillFromTemplate(*preview, v, t.key);
        e.width = v.width;     // un modele peut fixer la sienne (la popup d'equipement)
        e.height = v.height;
        e.previewView = v.id;
        preview->views.push_back(std::move(v));
        e.preview = std::move(preview);
        out.push_back(std::move(e));
    }
    return out;
}

// Un paquet, depuis une cle de la galerie ("lib:<chemin>", "prj:<id>").
core::Result<hmi::pkg::Package> packageOf(const std::string& key, const hmi::Project& project) {
    if (key.rfind("lib:", 0) == 0) return hmi::pkg::readFile(key.substr(4));
    if (key.rfind("prj:", 0) == 0) {
        const auto id = static_cast<hmi::Id>(std::strtoull(key.c_str() + 4, nullptr, 10));
        for (const auto& m : project.viewTemplates)
            if (m.id == id && m.data) return hmi::pkg::fromZip(*m.data);
        return core::fail(core::ErrorCode::FileNotFound, "mod\xC3\xA8le du projet introuvable");
    }
    return core::fail(core::ErrorCode::InvalidArgument, "pas un paquet : " + key);
}

} // namespace

std::string MainAnalysisScreen::hmiTemplateFolder() const {
    return (fs::path(Settings::defaultPath()).parent_path() / "modeles-vues").string();
}

// =============================================================== Nouvelle vue ==
void MainAnalysisScreen::askNewHmiView(const std::string& role) {
    auto doc = app_.hmi();
    if (!doc) return;
    HmiTemplateGallery::Spec spec;
    spec.role = role.empty() ? std::string("vue") : role;
    // Le nom propose pour un role : Vue_3, Popup_2... (la liste Role le refait).
    spec.nameFor = [this](const std::string& r) {
        std::string base = "Vue";
        if (r == "popup") base = "Popup";
        else if (r == "entete") base = "Entete";
        else if (r == "pied") base = "Pied";
        else if (r == "modele") base = "Modele";
        else if (r == "symbole") base = "Symbole";
        auto d = app_.hmi();
        return d ? hmi::uniqueViewName(d->project, base) : base + "_1";
    };
    spec.appEntriesFor = [this](const std::string& r) {
        auto d = app_.hmi();
        return d ? appEntries(d->project, r) : std::vector<Entry>{};
    };
    spec.name = spec.nameFor(spec.role);
    spec.entries = appEntries(doc->project, spec.role);
    // Mes modeles : le dossier de la bibliotheque.
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& f : fs::directory_iterator(hmiTemplateFolder(), ec))
        if (f.is_regular_file() && f.path().extension() == hmi::pkg::kTemplateExtension) files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files)
        if (auto pkg = hmi::pkg::readFile(f.string())) spec.entries.push_back(packageEntry("lib:" + f.string(), 1, *pkg, doc->project));
    // Les modeles du projet.
    for (const auto& m : doc->project.viewTemplates)
        if (m.data)
            if (auto pkg = hmi::pkg::fromZip(*m.data)) {
                auto e = packageEntry("prj:" + std::to_string(m.id), 2, *pkg, doc->project);
                if (!m.name.empty()) e.name = m.name;
                spec.entries.push_back(std::move(e));
            }
    // Une popup : "Popup d'equipement" d'abord (comme avant le lot 20).
    spec.selected = spec.role == "popup" ? "app:equipement" : "app:vide";
    spec.nameTaken = [this](const std::string& n) {
        auto d = app_.hmi();
        return d && d->project.viewByName(n) != nullptr;
    };
    app_.menus().ShowDialog(std::make_unique<HmiTemplateGallery>(std::move(spec)), [this](const menu::DialogResult& r) {
        const auto a = HmiTemplateGallery::parse(r.payload);
        if (a.request == "importer") { askHmiImportTemplate(); return; }
        if (a.request == "gerer") { askHmiManageTemplates(); return; }
        if (!r.accepted()) return;
        createHmiViewFromTemplate(a.key, a.name, a.role, a.width, a.height, a.description);
    });
}

void MainAnalysisScreen::createHmiViewFromTemplate(const std::string& key, const std::string& name, const std::string& role,
                                                   int width, int height, const std::string& description) {
    auto doc = app_.hmi();
    if (!doc || name.empty()) return;
    // Un modele de l'application : comme avant (la vue neuve, remplie).
    if (key.rfind("app:", 0) == 0 || key.empty()) {
        const std::string tplKey = key.empty() ? std::string("vide") : key.substr(4);
        const auto* tpl = hmi::design::viewTemplate(tplKey);
        hmi::Id made = hmi::kNoId;
        auto cmd = hmiNewViewCommand(doc, name, width > 0 ? width : doc->project.config.width, height > 0 ? height : doc->project.config.height,
                                     description, &made, role, tpl ? std::string(tpl->key) : std::string{});
        if (!cmd) return;
        app_.apply(std::move(cmd), false);
        if (made != hmi::kNoId) openHmiView(made);
        if (tpl && tpl->key != "vide")
            status_->setTransientMessage("Vue cr\xC3\xA9\xC3\xA9" "e depuis le mod\xC3\xA8le " + std::string(tpl->label) + " : "
                                             + std::string(tpl->description) + " (Ctrl+Z la retire)",
                                         8.0, ui::StatusBar::Severity::Success);
        return;
    }
    auto pkg = packageOf(key, doc->project);
    if (!pkg) {
        status_->setTransientMessage("Mod\xC3\xA8le illisible : " + pkg.error().message(), 8.0, ui::StatusBar::Severity::Warning);
        return;
    }
    const auto make = [this, name, role, width, height, description](const hmi::pkg::Package& p,
                                                                  std::vector<std::pair<std::string, std::string>> replace) {
        auto current = app_.hmi();
        if (!current) return;
        hmi::Id made = hmi::kNoId;
        hmi::pkg::ImportResult res;
        const std::string label = "Nouvelle vue " + name + " (mod\xC3\xA8le " + (p.manifest.name.empty() ? std::string("?") : p.manifest.name) + ")";
        auto cmd = hmi::changeProject(current, label, [&](hmi::Project& pr) {
            made = hmi::pkg::instantiate(pr, p, name, role, replace, &res);
            if (hmi::View* v = pr.view(made)) {
                if (width > 0) v->width = width;
                if (height > 0) v->height = height;
                if (!description.empty()) v->description = description;
            }
        });
        if (!cmd) return;
        app_.apply(std::move(cmd), false);
        if (made != hmi::kNoId) openHmiView(made);
        std::string text = "Vue " + name + " cr\xC3\xA9\xC3\xA9" "e depuis le mod\xC3\xA8le " + p.manifest.name;
        const std::size_t extra = res.added.size() > 0 ? res.added.size() - 1 : 0;
        if (extra > 0) text += " : " + std::to_string(extra) + " \xC3\xA9l\xC3\xA9ment(s) ajout\xC3\xA9(s) au projet";
        status_->setTransientMessage(text + " (Ctrl+Z retire tout)", 8.0, ui::StatusBar::Severity::Success);
    };
    const auto vars = hmi::pkg::templateVariables(*pkg);
    if (!pkg->manifest.askVariables || vars.empty()) {
        make(*pkg, {});
        return;
    }
    // "A choisir a la creation" : un champ par variable (les huit premieres).
    std::vector<FormDialog::Field> fields;
    const std::size_t shown = std::min<std::size_t>(vars.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) fields.push_back({vars[i], vars[i], "la variable de cette vue", false, {}});
    auto shared = std::make_shared<hmi::pkg::Package>(std::move(*pkg));
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiTemplateVariables", "Les variables de " + name,
                                     "Le mod\xC3\xA8le \xC2\xAB " + shared->manifest.name + " \xC2\xBB lit ces variables. Donne celles de cette vue "
                                     "(Pression_Sud \xE2\x86\x92 Pression_Nord) ; une variable absente du projet est cr\xC3\xA9\xC3\xA9" "e."
                                     + (vars.size() > shown ? "\n(" + std::to_string(vars.size() - shown) + " autre(s) gard\xC3\xA9" "e(s) telle(s) quelle(s).)" : std::string{}),
                                     std::move(fields), "Cr\xC3\xA9" "er la vue"),
        [shared, vars, shown, make](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            std::vector<std::pair<std::string, std::string>> replace;
            for (std::size_t i = 0; i < shown && i < v.size(); ++i)
                if (!v[i].empty() && v[i] != vars[i]) replace.emplace_back(vars[i], v[i]);
            make(*shared, std::move(replace));
        });
}

// ======================================================= Enregistrer comme modele ==
void MainAnalysisScreen::askHmiSaveTemplate(std::uint64_t viewId) {
    auto doc = app_.hmi();
    const auto* view = doc ? doc->project.view(asId(viewId)) : nullptr;
    if (!doc || !view) {
        status_->setTransientMessage("Enregistrer comme mod\xC3\xA8le : ouvre ou choisis d'abord une vue", 6.0);
        return;
    }
    const auto pkg = hmi::pkg::collect(doc->project, {view->id});
    const std::string carried = hmi::pkg::contentsText(pkg);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", view->name, "le nom dans la galerie", false, {}});
    fields.push_back({"Cat\xC3\xA9gorie", view->role == "popup" ? std::string("Popups") : std::string("Synoptiques"), "",
                      false, {"Synoptiques", "Tableaux de bord", "Popups", "R\xC3\xA9glages", "Alarmes", "Autres"}});
    fields.push_back({"Description", view->description, "ce que montre la vue", false, {}});
    fields.push_back({"O\xC3\xB9", "Ma biblioth\xC3\xA8que (tous mes projets)", "", false,
                      {"Ma biblioth\xC3\xA8que (tous mes projets)", "Ce projet (voyage avec lui)"}});
    fields.push_back({"Les variables", "gard\xC3\xA9" "es telles quelles", "", false,
                      {"gard\xC3\xA9" "es telles quelles", "\xC3\xA0 choisir \xC3\xA0 la cr\xC3\xA9" "ation"}});
    const hmi::Id vid = view->id;
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiSaveTemplate", "Enregistrer " + view->name + " comme mod\xC3\xA8le",
                                     "Le mod\xC3\xA8le garde la vue telle qu'elle est, et ce dont elle a besoin. Emporte aussi : " + carried
                                         + ".\nMa biblioth\xC3\xA8que : le mod\xC3\xA8le sert dans tous tes projets. Ce projet : il voyage avec le projet "
                                           "(enregistr\xC3\xA9 dans ihm/modeles/, Ctrl+Z le retire).",
                                     std::move(fields), "Enregistrer"),
        [this, vid](const menu::DialogResult& r) {
            auto current = app_.hmi();
            if (!r.accepted() || !current || !current->project.view(vid)) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 5 || v[0].empty()) return;
            auto p = hmi::pkg::collect(current->project, {vid});
            p.manifest.kind = "modele";
            p.manifest.name = v[0];
            p.manifest.category = v[1];
            p.manifest.description = v[2];
            p.manifest.askVariables = v[4].rfind("\xC3\xA0 choisir", 0) == 0;
            const bool inProject = v[3].rfind("Ce projet", 0) == 0;
            if (inProject) {
                const auto bytes = std::make_shared<hmi::Bytes>(hmi::pkg::toZip(p));
                auto cmd = hmi::changeProject(current, "Mod\xC3\xA8le du projet " + v[0], [&](hmi::Project& pr) {
                    hmi::ViewTemplateFile m;
                    m.id = pr.allocate();
                    m.name = v[0];
                    m.category = v[1];
                    m.description = v[2];
                    m.created = p.manifest.created;
                    m.data = bytes;
                    // Un modele du meme nom : remplace.
                    std::erase_if(pr.viewTemplates, [&](const hmi::ViewTemplateFile& x) { return x.name == m.name; });
                    pr.viewTemplates.push_back(std::move(m));
                });
                if (cmd) app_.apply(std::move(cmd), false);
                status_->setTransientMessage("Mod\xC3\xA8le \xC2\xAB " + v[0] + " \xC2\xBB gard\xC3\xA9 dans le projet (Nouvelle vue \xE2\x80\xBA Mod\xC3\xA8les du projet ; Ctrl+Z le retire)",
                                             8.0, ui::StatusBar::Severity::Success);
                return;
            }
            std::error_code ec;
            fs::create_directories(hmiTemplateFolder(), ec);
            const fs::path file = fs::path(hmiTemplateFolder()) / (fileStem(v[0]) + std::string(hmi::pkg::kTemplateExtension));
            if (auto st = hmi::pkg::writeFile(p, file.string()); !st) {
                status_->setTransientMessage("Mod\xC3\xA8le non enregistr\xC3\xA9 : " + st.error().message(), 8.0, ui::StatusBar::Severity::Warning);
                return;
            }
            status_->setTransientMessage("Mod\xC3\xA8le \xC2\xAB " + v[0] + " \xC2\xBB dans ma biblioth\xC3\xA8que (" + file.filename().string()
                                             + ") : Nouvelle vue \xE2\x80\xBA Mes mod\xC3\xA8les, dans tous tes projets",
                                         8.0, ui::StatusBar::Severity::Success);
        });
}

// =========================================================== Exporter les vues ==
void MainAnalysisScreen::askHmiExportViews(std::uint64_t selectedView) {
    auto doc = app_.hmi();
    if (!doc || doc->project.views.empty()) return;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiExportViews";
    spec.title = "Exporter les vues";
    spec.text = "Les vues coch\xC3\xA9" "es partent avec ce dont elles ont besoin : leur \xC3\xA9" "cran mod\xC3\xA8le, leurs symboles, les popups que "
                "leurs actions ouvrent, leurs images, leurs styles, la liste de leurs variables. Un autre projet les importe "
                "(Vues \xE2\x80\xBA Importer des vues).";
    spec.listTitle = "Les vues";
    std::vector<hmi::Id> ids;
    for (const auto& v : doc->project.views) {
        if (v.role == "symbole") continue;      // un symbole part avec la vue qui s'en sert
        ids.push_back(v.id);
        spec.items.push_back({v.name, std::string(hmi::viewRoleLabel(v.role)) + " \xC2\xB7 " + std::to_string(v.objects.size()) + " objets",
                              v.id == asId(selectedView)});
    }
    const auto* first = doc->project.view(asId(selectedView));
    HmiAskDialog::Option file;
    file.label = "Fichier (un nom seul : dans exports/ du projet)";
    file.hasField = true;
    file.field = (first ? first->name : std::string("Vues")) + std::string(hmi::pkg::kViewsExtension);
    file.placeholder = "Armoires_Sud.xpgvues";
    // Le bouton ... : ailleurs que dans exports/ (l'explorateur, "Enregistrer sous").
    file.browse = ui::saveFile("Paquets de vues (.xpgvues)|*.xpgvues", ui::pathIn(app_.projectFolder(), "exports"), "Exporter les vues");
    spec.options.push_back(file);
    spec.confirm = "Exporter";
    const auto project = std::make_shared<hmi::Project>(doc->project);
    spec.confirmLabel = [ids, project](const std::vector<bool>& items, const std::vector<bool>&, int) {
        std::vector<hmi::Id> chosen;
        for (std::size_t i = 0; i < items.size() && i < ids.size(); ++i)
            if (items[i]) chosen.push_back(ids[i]);
        if (chosen.empty()) return std::string("Exporter");
        const auto p = hmi::pkg::collect(*project, chosen);
        return "Exporter " + std::to_string(chosen.size()) + (chosen.size() == 1 ? " vue" : " vues") + " (et " + hmi::pkg::contentsText(p) + ")";
    };
    spec.width = 760.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, ids](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto a = HmiAskDialog::parse(r.payload);
        std::vector<hmi::Id> chosen;
        for (std::size_t i = 0; i < a.items.size() && i < ids.size(); ++i)
            if (a.items[i]) chosen.push_back(ids[i]);
        if (chosen.empty()) {
            status_->setTransientMessage("Exporter : coche au moins une vue", 6.0, ui::StatusBar::Severity::Warning);
            return;
        }
        auto p = hmi::pkg::collect(current->project, chosen);
        std::string name = a.field.empty() ? std::string("Vues") : a.field;
        if (name.size() < 8 || name.compare(name.size() - 8, 8, hmi::pkg::kViewsExtension) != 0) name += std::string(hmi::pkg::kViewsExtension);
        p.manifest.kind = "vues";
        // Un chemin complet (choisi avec ...) : le paquet porte le nom du fichier.
        const auto slash = name.find_last_of("/\\");
        const std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
        p.manifest.name = leaf.size() > 8 ? leaf.substr(0, leaf.size() - 8) : std::string("Vues");
        hmi::ExportRequest rq;
        rq.fileName = name;
        rq.format = "XPGVUES";
        rq.source = "vues";
        rq.rows = chosen.size();
        rq.data = std::make_shared<hmi::Bytes>(hmi::pkg::toZip(p));
        std::string where;
        if (!writeHmiExport(rq, &where)) {
            status_->setTransientMessage("Export impossible : " + where, 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        status_->setTransientMessage(std::to_string(chosen.size()) + " vue(s) export\xC3\xA9" "e(s) dans " + where + " (et " + hmi::pkg::contentsText(p) + ")",
                                     8.0, ui::StatusBar::Severity::Success);
    });
}

// =========================================================== Importer des vues ==
void MainAnalysisScreen::askHmiImportViews(const std::string& path) {
    auto doc = app_.hmi();
    if (!doc) return;
    if (path.empty()) {
        // Le fichier : les paquets du dossier exports/ du projet sont proposes.
        std::string found;
        std::error_code ec;
        const std::string folder = app_.projectFolder();
        if (!folder.empty())
            for (const auto& f : fs::directory_iterator(fs::path(folder) / "exports", ec))
                if (f.path().extension() == hmi::pkg::kViewsExtension) found += (found.empty() ? "" : ", ") + f.path().filename().string();
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Fichier", "", "C:\\Echanges\\Armoires_Sud.xpgvues (ou le nom d'un fichier de exports/)", false, {}});
        app_.menus().ShowDialog(
            FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiImportViewsFile", "Importer des vues",
                                                                "Un paquet de vues (.xpgvues) fait par Exporter les vues, ici ou dans un autre projet."
                                                                    + (found.empty() ? std::string{} : "\nDans exports/ : " + found),
                                                                std::move(fields), "Ouvrir"),
                                   0, ui::openFile("Paquets de vues (.xpgvues)|*.xpgvues", ui::pathIn(folder, "exports"), "Importer des vues")),
            [this](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                const auto v = FormDialog::split(r.payload);
                if (v.empty() || v[0].empty()) return;
                std::string file = v[0];
                std::error_code ec2;
                if (!fs::exists(file, ec2) && !app_.projectFolder().empty()) {
                    const fs::path inExports = fs::path(app_.projectFolder()) / "exports" / file;
                    if (fs::exists(inExports, ec2)) file = inExports.string();
                }
                askHmiImportViews(file);
            });
        return;
    }
    auto pkg = hmi::pkg::readFile(path);
    if (!pkg) {
        status_->setTransientMessage("Importer : " + pkg.error().message(), 8.0, ui::StatusBar::Severity::Warning);
        return;
    }
    HmiImportDialog::Spec spec;
    spec.fileName = fs::path(path).filename().string();
    spec.manifest = pkg->manifest;
    spec.contents = hmi::pkg::contentsText(*pkg, true);
    spec.plan = hmi::pkg::plan(doc->project, *pkg);
    auto shared = std::make_shared<hmi::pkg::Package>(std::move(*pkg));
    auto planCopy = std::make_shared<hmi::pkg::Plan>(spec.plan);
    const std::string fileName = spec.fileName;
    app_.menus().ShowDialog(std::make_unique<HmiImportDialog>(std::move(spec)), [this, shared, planCopy, fileName](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        HmiImportDialog::applyChoices(*planCopy, HmiImportDialog::parse(r.payload));
        hmi::pkg::ImportResult res;
        auto cmd = hmi::changeProject(current, "Importer " + fileName, [&](hmi::Project& p) { res = hmi::pkg::importInto(p, *shared, *planCopy); });
        if (!cmd) {
            status_->setTransientMessage("Importer " + fileName + " : rien \xC3\xA0 changer (tout est d\xC3\xA9j\xC3\xA0 l\xC3\xA0)", 8.0);
            return;
        }
        app_.apply(std::move(cmd), false);
        if (!res.views.empty()) openHmiView(res.views.front());
        status_->setTransientMessage("Import\xC3\xA9 depuis " + fileName + " : " + res.summary() + " (Ctrl+Z annule tout l'import)", 10.0,
                                     ui::StatusBar::Severity::Success);
    });
}

// ====================================================== Exporter les symboles ==
// 1.11.2 (decision 162) : les symboles coches, dans UN fichier (.xpgsymboles),
// avec ce dont ils ont besoin (hmi::pkg::collectSymbols).
void MainAnalysisScreen::askHmiExportSymbols(std::uint64_t selectedSymbol) {
    auto doc = app_.hmi();
    if (!doc) return;
    std::vector<hmi::Id> ids;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiExportSymbols";
    spec.title = "Exporter les symboles";
    spec.text = "Les symboles coch\xC3\xA9s partent dans un seul fichier (.xpgsymboles) avec ce dont ils ont besoin : les symboles qu'ils "
                "contiennent, leurs images, leurs styles, leurs variables IHM et leurs types IHM. Un autre projet les importe "
                "(Symboles \xE2\x80\xBA Importer\xE2\x80\xA6).";
    spec.listTitle = "Les symboles";
    const hmi::View* first = nullptr;
    for (const auto& v : doc->project.views) {
        if (v.role != "symbole") continue;
        ids.push_back(v.id);
        const bool on = v.id == asId(selectedSymbol);
        if (on) first = &v;
        spec.items.push_back({v.name, std::to_string(v.objects.size()) + (v.objects.size() == 1 ? " objet" : " objets")
                                          + (v.params.empty() ? std::string{} : " \xC2\xB7 " + std::to_string(v.params.size()) + " param\xC3\xA8tre(s)"),
                              on});
    }
    if (ids.empty()) {
        status_->setTransientMessage("Exporter les symboles : ce projet n'a pas encore de symbole (Symboles \xE2\x80\xBA Nouveau symbole)", 8.0,
                                     ui::StatusBar::Severity::Warning);
        return;
    }
    HmiAskDialog::Option file;
    file.label = "Fichier (un nom seul : dans exports/ du projet)";
    file.hasField = true;
    file.field = (first ? first->name : std::string("Symboles")) + std::string(hmi::pkg::kSymbolsExtension);
    file.placeholder = "Vannes.xpgsymboles";
    file.browse = ui::saveFile("Symboles (.xpgsymboles)|*.xpgsymboles", ui::pathIn(app_.projectFolder(), "exports"), "Exporter les symboles");
    spec.options.push_back(file);
    spec.confirm = "Exporter";
    const auto project = std::make_shared<hmi::Project>(doc->project);
    spec.confirmLabel = [ids, project](const std::vector<bool>& items, const std::vector<bool>&, int) {
        std::vector<hmi::Id> chosen;
        for (std::size_t i = 0; i < items.size() && i < ids.size(); ++i)
            if (items[i]) chosen.push_back(ids[i]);
        if (chosen.empty()) return std::string("Exporter");
        const auto p = hmi::pkg::collectSymbols(*project, chosen);
        return "Exporter " + std::to_string(chosen.size()) + (chosen.size() == 1 ? " symbole" : " symboles") + " (et " + hmi::pkg::contentsText(p) + ")";
    };
    spec.width = 760.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, ids](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto a = HmiAskDialog::parse(r.payload);
        std::vector<hmi::Id> chosen;
        for (std::size_t i = 0; i < a.items.size() && i < ids.size(); ++i)
            if (a.items[i]) chosen.push_back(ids[i]);
        if (chosen.empty()) {
            status_->setTransientMessage("Exporter les symboles : coche au moins un symbole", 6.0, ui::StatusBar::Severity::Warning);
            return;
        }
        auto p = hmi::pkg::collectSymbols(current->project, chosen);
        const std::string ext(hmi::pkg::kSymbolsExtension);
        std::string name = a.field.empty() ? std::string("Symboles") : a.field;
        if (name.size() < ext.size() || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) name += ext;
        const auto slash = name.find_last_of("/\\");
        const std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
        p.manifest.name = leaf.size() > ext.size() ? leaf.substr(0, leaf.size() - ext.size()) : std::string("Symboles");
        hmi::ExportRequest rq;
        rq.fileName = name;
        rq.format = "XPGSYMBOLES";
        rq.source = "symboles";
        rq.rows = chosen.size();
        rq.data = std::make_shared<hmi::Bytes>(hmi::pkg::toZip(p));
        std::string where;
        if (!writeHmiExport(rq, &where)) {
            status_->setTransientMessage("Export impossible : " + where, 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        status_->setTransientMessage(std::to_string(chosen.size()) + (chosen.size() == 1 ? " symbole export\xC3\xA9" : " symboles export\xC3\xA9s")
                                         + " dans " + where + " (et " + hmi::pkg::contentsText(p) + ")",
                                     8.0, ui::StatusBar::Severity::Success);
    });
}

// ===================================================== Importer des symboles ==
// 1.11.2 (decision 162) : le fichier, compare au projet (HmiImportDialog : ce
// qu'il emporte, renommer ou remplacer chaque nom en conflit), puis tout en
// une commande (Ctrl+Z). Un fichier d'une version plus recente est refuse.
void MainAnalysisScreen::askHmiImportSymbols(const std::string& path) { askHmiImport(path); }

// 1.11.2 (decision 174) : "Importer..." ouvre tout paquet - vues, symboles, types
// IHM, fonctions IHM, scripts generaux - et dit ce qu'il apporte (le titre de la
// fenetre, "Le fichier emporte : ..."). Un modele (.xpgmodele) va a la galerie.
void MainAnalysisScreen::askHmiImport(const std::string& path, std::function<void()> then) {
    auto doc = app_.hmi();
    if (!doc) {
        if (then) then();
        return;
    }
    // Un nom seul : dans exports/ du projet (avec ou sans son extension).
    const auto resolve = [this](const std::string& name) {
        std::string file = name;
        std::error_code ec;
        if (!file.empty() && !fs::exists(file, ec) && !app_.projectFolder().empty()) {
            const fs::path inExports = fs::path(app_.projectFolder()) / "exports" / file;
            if (fs::exists(inExports, ec)) file = inExports.string();
            else
                for (const auto e : hmi::pkg::kAllExtensions)
                    if (fs::exists(inExports.string() + std::string(e), ec)) { file = inExports.string() + std::string(e); break; }
        }
        return file;
    };
    // 1.11.2 (decisions 174, 188) : la fenetre d'import elle-meme - le fichier (Parcourir... dedans, ou un
    // paquet lache sur l'appli), ce qu'il apporte (le titre), ce qu'il emporte, les noms en conflit ; un
    // fichier plus recent, illisible ou un modele : la fenetre le dit et Importer est grise.
    auto spec = HmiImportDialog::load(doc->project, resolve(path));
    spec.browse = ui::openFile("Paquets XpgAnalyzer|*.xpgvues;*.xpgsymboles;*.xpgtypes;*.xpgfonctions;*.xpgscripts",
                               ui::pathIn(app_.projectFolder(), "exports"), "Importer");
    auto loader = [this, resolve](const std::string& file) {
        auto d = app_.hmi();
        return d ? HmiImportDialog::load(d->project, resolve(file)) : HmiImportDialog::Spec{};
    };
    auto dialog = std::make_unique<HmiImportDialog>(std::move(spec), loader);
    auto state = dialog->state();
    app_.menus().ShowDialog(std::move(dialog), [this, state, then](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (r.accepted() && current && state->package) {
            hmi::pkg::Plan plan = state->plan;
            HmiImportDialog::applyChoices(plan, HmiImportDialog::parse(r.payload));
            const std::string fileName = state->fileName;
            hmi::pkg::ImportResult res;
            auto cmd = hmi::changeProject(current, "Importer " + fileName, [&](hmi::Project& p) { res = hmi::pkg::importInto(p, *state->package, plan); });
            if (!cmd) {
                status_->setTransientMessage("Importer " + fileName + " : rien \xC3\xA0 changer (tout est d\xC3\xA9j\xC3\xA0 l\xC3\xA0)", 8.0);
            } else {
                app_.apply(std::move(cmd), false);
                if (!res.views.empty()) openHmiView(res.views.front());
                status_->setTransientMessage("Import\xC3\xA9 depuis " + fileName + " : " + res.summary() + " (Ctrl+Z annule tout l'import)", 10.0,
                                             ui::StatusBar::Severity::Success);
            }
        }
        if (then) then();
    });
}

// ============================== Exporter des types IHM, des fonctions, des scripts ==
// 1.11.2 (decision 174) : les elements coches de Programmation generale, dans UN
// fichier (.xpgtypes, .xpgfonctions, .xpgscripts), avec ce dont ils ont besoin
// (hmi::pkg::collectPrograms).
void MainAnalysisScreen::askHmiExportPrograms(int kind, std::uint64_t selected) {
    auto doc = app_.hmi();
    if (!doc) return;
    using hmi::pkg::ProgramKind;
    const ProgramKind k = kind == 1 ? ProgramKind::Functions : kind == 2 ? ProgramKind::Scripts : ProgramKind::Types;
    struct Words { const char* title; const char* one; const char* many; const char* fallback; std::string_view ext; const char* format; const char* text; };
    const Words w = k == ProgramKind::Types
                        ? Words{"Exporter les types", "type", "types", "Types", hmi::pkg::kTypesExtension, "XPGTYPES",
                                "Les types IHM coch\xC3\xA9s (\xC3\xA9num\xC3\xA9rations et structures) partent dans un seul fichier (.xpgtypes) avec "
                                "les types de leurs membres. Un autre projet les importe (Types IHM \xE2\x80\xBA Importer)."}
                    : k == ProgramKind::Functions
                        ? Words{"Exporter les fonctions", "fonction", "fonctions", "Fonctions", hmi::pkg::kFunctionsExtension, "XPGFONCTIONS",
                                "Les fonctions IHM coch\xC3\xA9" "es partent dans un seul fichier (.xpgfonctions) avec ce dont elles ont besoin : les "
                                "fonctions qu'elles appellent, les types IHM et les variables IHM qu'elles emploient. Un autre projet les importe "
                                "(Fonctions \xE2\x80\xBA Importer)."}
                        : Words{"Exporter les scripts", "script", "scripts", "Scripts", hmi::pkg::kScriptsExtension, "XPGSCRIPTS",
                                "Les scripts g\xC3\xA9n\xC3\xA9raux coch\xC3\xA9s partent dans un seul fichier (.xpgscripts) avec ce dont ils ont "
                                "besoin : les fonctions IHM qu'ils appellent, les types IHM et les variables IHM qu'ils emploient. Un autre projet "
                                "les importe (Scripts g\xC3\xA9n\xC3\xA9raux \xE2\x80\xBA Importer)."};
    std::vector<hmi::Id> ids;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiExportPrograms";
    spec.title = w.title;
    spec.text = w.text;
    std::string first;
    const auto& pr = doc->project.programs;
    const auto add = [&](hmi::Id id, const std::string& name, const std::string& detail) {
        ids.push_back(id);
        const bool on = id == static_cast<hmi::Id>(selected);
        if (on) first = name;
        spec.items.push_back({name, detail, on});
    };
    if (k == ProgramKind::Types) {
        spec.listTitle = "Les types IHM";
        for (const auto& t : pr.types)
            add(t.id, t.name, t.kind == hmi::HmiTypeKind::Enumeration ? "\xC3\xA9num\xC3\xA9ration \xC2\xB7 " + std::to_string(t.values.size()) + " valeur(s)"
                                                                     : "structure \xC2\xB7 " + std::to_string(t.members.size()) + " membre(s)");
    } else if (k == ProgramKind::Functions) {
        spec.listTitle = "Les fonctions IHM";
        for (const auto& f : pr.functions) add(f.id, f.name, f.returnType.empty() ? std::string("sans retour") : f.returnType);
    } else {
        spec.listTitle = "Les scripts g\xC3\xA9n\xC3\xA9raux";
        for (const auto& sc : pr.scripts) add(sc.id, sc.name, std::string(hmi::eventLabel(sc.event)));
    }
    if (ids.empty()) {
        status_->setTransientMessage(std::string(w.title) + " : ce projet n'en a pas encore", 8.0, ui::StatusBar::Severity::Warning);
        return;
    }
    const std::string ext(w.ext);
    HmiAskDialog::Option file;
    file.label = "Fichier (un nom seul : dans exports/ du projet)";
    file.hasField = true;
    file.field = (first.empty() ? std::string(w.fallback) : first) + ext;
    file.placeholder = std::string(w.fallback) + ext;
    file.browse = ui::saveFile(std::string(w.fallback) + " (" + ext + ")|*" + ext, ui::pathIn(app_.projectFolder(), "exports"), w.title);
    spec.options.push_back(file);
    spec.confirm = "Exporter";
    const auto project = std::make_shared<hmi::Project>(doc->project);
    spec.confirmLabel = [ids, project, k, w](const std::vector<bool>& items, const std::vector<bool>&, int) {
        std::vector<hmi::Id> chosen;
        for (std::size_t i = 0; i < items.size() && i < ids.size(); ++i)
            if (items[i]) chosen.push_back(ids[i]);
        if (chosen.empty()) return std::string("Exporter");
        const auto p = hmi::pkg::collectPrograms(*project, k, chosen);
        const std::string more = hmi::pkg::contentsText(p);
        return "Exporter " + std::to_string(chosen.size()) + " " + (chosen.size() == 1 ? w.one : w.many)
             + (more == "rien d'autre" ? std::string{} : " (et " + more + ")");
    };
    spec.width = 760.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, ids, k, w, ext](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto a = HmiAskDialog::parse(r.payload);
        std::vector<hmi::Id> chosen;
        for (std::size_t i = 0; i < a.items.size() && i < ids.size(); ++i)
            if (a.items[i]) chosen.push_back(ids[i]);
        if (chosen.empty()) {
            status_->setTransientMessage(std::string(w.title) + " : coche au moins un \xC3\xA9l\xC3\xA9ment", 6.0, ui::StatusBar::Severity::Warning);
            return;
        }
        auto p = hmi::pkg::collectPrograms(current->project, k, chosen);
        std::string name = a.field.empty() ? std::string(w.fallback) : a.field;
        if (name.size() < ext.size() || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) name += ext;
        const auto slash = name.find_last_of("/\\");
        const std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
        p.manifest.name = leaf.size() > ext.size() ? leaf.substr(0, leaf.size() - ext.size()) : std::string(w.fallback);
        hmi::ExportRequest rq;
        rq.fileName = name;
        rq.format = w.format;
        rq.source = p.manifest.kind;
        rq.rows = chosen.size();
        rq.data = std::make_shared<hmi::Bytes>(hmi::pkg::toZip(p));
        std::string where;
        if (!writeHmiExport(rq, &where)) {
            status_->setTransientMessage("Export impossible : " + where, 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        const std::string more = hmi::pkg::contentsText(p);
        status_->setTransientMessage(std::to_string(chosen.size()) + " " + (chosen.size() == 1 ? w.one : w.many) + " export\xC3\xA9(s) dans " + where
                                         + (more == "rien d'autre" ? std::string{} : " (et " + more + ")"),
                                     8.0, ui::StatusBar::Severity::Success);
    });
}

// ======================================================= Importer un modele ==
void MainAnalysisScreen::askHmiImportTemplate() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier", "", "un mod\xC3\xA8le (.xpgmodele) ou des vues (.xpgvues)", false, {}});
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiImportTemplate", "Importer un mod\xC3\xA8le",
                                                            "Le mod\xC3\xA8le est copi\xC3\xA9 dans ma biblioth\xC3\xA8que : il sert ensuite dans tous tes projets "
                                                            "(Nouvelle vue \xE2\x80\xBA Mes mod\xC3\xA8les).",
                                                            std::move(fields), "Importer"),
                               0, ui::openFile("Mod\xC3\xA8les et vues|*.xpgmodele;*.xpgvues", ui::pathIn(app_.projectFolder(), "exports"),
                                               "Importer un mod\xC3\xA8le")),
        [this](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v[0].empty()) return;
            auto pkg = hmi::pkg::readFile(v[0]);
            if (!pkg) {
                status_->setTransientMessage("Mod\xC3\xA8le illisible : " + pkg.error().message(), 8.0, ui::StatusBar::Severity::Warning);
                return;
            }
            pkg->manifest.kind = "modele";
            if (pkg->manifest.name.empty()) pkg->manifest.name = fs::path(v[0]).stem().string();
            std::error_code ec;
            fs::create_directories(hmiTemplateFolder(), ec);
            const fs::path file = fs::path(hmiTemplateFolder()) / (fileStem(pkg->manifest.name) + std::string(hmi::pkg::kTemplateExtension));
            if (auto st = hmi::pkg::writeFile(*pkg, file.string()); !st) {
                status_->setTransientMessage("Mod\xC3\xA8le non copi\xC3\xA9 : " + st.error().message(), 8.0, ui::StatusBar::Severity::Warning);
                return;
            }
            status_->setTransientMessage("Mod\xC3\xA8le \xC2\xAB " + pkg->manifest.name + " \xC2\xBB ajout\xC3\xA9 \xC3\xA0 ma biblioth\xC3\xA8que", 8.0,
                                         ui::StatusBar::Severity::Success);
            askNewHmiView("vue");
        });
}

// ========================================================= Gerer mes modeles ==
void MainAnalysisScreen::askHmiManageTemplates() {
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& f : fs::directory_iterator(hmiTemplateFolder(), ec))
        if (f.is_regular_file() && f.path().extension() == hmi::pkg::kTemplateExtension) files.push_back(f.path());
    std::sort(files.begin(), files.end());
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiManageTemplates";
    spec.title = "G\xC3\xA9rer mes mod\xC3\xA8les";
    spec.text = files.empty() ? std::string("Ma biblioth\xC3\xA8que est vide : \xC2\xAB Enregistrer comme mod\xC3\xA8le \xC2\xBB depuis une vue (sa barre d'outils, "
                                            "le volet Vues ou le clic droit dans l'arbre).")
                              : std::string("Coche les mod\xC3\xA8les \xC3\xA0 retirer de ma biblioth\xC3\xA8que. Les vues d\xC3\xA9j\xC3\xA0 cr\xC3\xA9\xC3\xA9" "es "
                                            "depuis eux ne changent pas.");
    spec.listTitle = "Ma biblioth\xC3\xA8que";
    for (const auto& f : files) {
        std::string detail;
        if (auto p = hmi::pkg::readFile(f.string())) detail = p->manifest.category + (p->manifest.category.empty() ? "" : " \xC2\xB7 ") + hmi::pkg::contentsText(*p);
        spec.items.push_back({f.stem().string(), detail, false});
    }
    spec.note = "Le dossier : " + hmiTemplateFolder();
    spec.confirm = files.empty() ? "Fermer" : "Retirer";
    spec.danger = !files.empty();
    spec.confirmLabel = [](const std::vector<bool>& items, const std::vector<bool>&, int) {
        const auto n = static_cast<std::size_t>(std::count(items.begin(), items.end(), true));
        return n == 0 ? std::string("Fermer") : "Retirer " + std::to_string(n) + " mod\xC3\xA8le(s)";
    };
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, files](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = HmiAskDialog::parse(r.payload);
        std::size_t removed = 0;
        std::error_code ec2;
        for (std::size_t i = 0; i < a.items.size() && i < files.size(); ++i)
            if (a.items[i] && fs::remove(files[i], ec2)) ++removed;
        if (removed > 0)
            status_->setTransientMessage(std::to_string(removed) + " mod\xC3\xA8le(s) retir\xC3\xA9(s) de ma biblioth\xC3\xA8que", 6.0,
                                         ui::StatusBar::Severity::Success);
    });
}

} // namespace app
