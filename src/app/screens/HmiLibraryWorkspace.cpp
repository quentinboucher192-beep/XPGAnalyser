// =============================================================================
//  app/screens/HmiLibraryWorkspace.cpp - 1.13.0 (lot J8) : les fenetres des
//  espaces de noms et des bibliotheques (IHM > Espaces et bibliotheques)
// -----------------------------------------------------------------------------
//  RELIER : le fichier .xpgbib (le bouton ... ouvre l'explorateur), puis ce qu'il
//  apporte (son nom, sa version, son contenu, les avertissements) et sous quel nom
//  le relier (le sien, ou un autre en cas de collision d'espace). Relier est
//  refuse (et dit pourquoi) pour un fichier incompatible.
//  PUBLIER : les espaces coches, la version (correctif, mineur, majeur, ou une
//  autre), le fichier libs/<Nom>-<version>.xpgbib du projet.
//  METTRE A JOUR : le diff de l'interface - ajouts, retraits, signatures changees,
//  chaque rupture avec l'endroit du projet qui l'emploie - puis Appliquer (une
//  commande : Ctrl+Z la defait) ou Annuler (rien ne change).
//  Les gestes eux-memes sont ceux du volet (HmiLibrariesPane) : un seul chemin.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiLibraryPanes.hpp"
#include "../../hmi/HmiLibrary.hpp"
#include "../../hmi/HmiNames.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../ui/widgets/PathBrowse.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace app {

namespace {
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
std::string trimmedText(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}
} // namespace

// ---------------------------------------------------------------------------------- relier ---
void MainAnalysisScreen::askHmiLinkLibrary(const std::string& path) {
    auto doc = app_.hmi();
    if (!doc) return;
    openHmiPane("bibliotheques");
    const std::string folder = app_.projectFolder();
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiLinkLibrary";
    spec.title = "Relier une biblioth\xC3\xA8que";
    spec.text = "Une biblioth\xC3\xA8que est du code partag\xC3\xA9, reli\xC3\xA9 et non copi\xC3\xA9 : le projet garde son nom, sa version, son chemin et "
                "son interface ; son code reste dans le fichier .xpgbib. Range-la de pr\xC3\xA9" "f\xC3\xA9rence dans libs/ du projet : "
                "elle voyage avec lui.";
    HmiAskDialog::Option file;
    file.label = "Le fichier de la biblioth\xC3\xA8que (.xpgbib)";
    file.hasField = true;
    file.placeholder = "libs/Gaz-1.4.0.xpgbib";
    file.field = path;      // 1.14.0 (lot M5) : le fichier trouve par Chercher des fichiers
    file.browse = ui::openFile("Biblioth\xC3\xA8ques IHM (.xpgbib)|*.xpgbib", ui::pathIn(folder, "libs"), "Relier une biblioth\xC3\xA8que");
    const auto project = std::make_shared<hmi::Project>(doc->project);
    file.check = [project, folder](const std::string& path) -> std::string {
        if (trimmedText(path).empty()) return {};
        const auto plan = hmi::biblio::planLink(*project, trimmedText(path), folder);
        // Une collision d'espace se regle a l'etape suivante (Relier sous un autre nom).
        if (!plan.ok() && plan.package.manifest.name.empty()) return plan.refusal;
        return {};
    };
    spec.options.push_back(std::move(file));
    spec.confirm = "Lire la biblioth\xC3\xA8que";
    spec.width = 720.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, folder](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto a = HmiAskDialog::parse(r.payload);
        std::string path = trimmedText(a.field);
        if (path.empty()) return;
        if (std::filesystem::path(path).is_relative() && !folder.empty()) path = (std::filesystem::path(folder) / path).string();
        const auto plan = hmi::biblio::planLink(current->project, path, folder);
        if (plan.package.manifest.name.empty()) {
            status_->setTransientMessage("Relier : " + plan.refusal, 8.0, ui::StatusBar::Severity::Warning);
            return;
        }
        const auto& m = plan.package.manifest;
        HmiAskDialog::Spec next;
        next.id = "dialog.hmiLinkLibrary2";
        next.title = "Relier " + m.name + " " + m.version;
        next.text = m.name + " " + m.version + " \xC2\xB7 " + hmi::biblio::contentsText(plan.package.content)
                  + (m.description.empty() ? std::string{} : "\n" + m.description)
                  + (m.fromProject.empty() ? std::string{} : "\nPubli\xC3\xA9" "e depuis le projet " + m.fromProject + (m.created.empty() ? std::string{} : ", le " + m.created))
                  + "\nLe projet garde le lien : " + (plan.ok() ? plan.link.path : hmi::biblio::pathToRecord(path, folder));
        for (const auto& w : plan.warnings) next.text += "\n\xE2\x9A\xA0 " + w;
        if (!plan.ok()) next.text += "\n\xE2\x9C\x96 " + plan.refusal;
        HmiAskDialog::Option own;
        own.label = "Sous son nom : " + m.name;
        own.detail = "ses noms s'\xC3\xA9" "crivent " + m.name + ".Nom (ou Nom sous USING " + m.name + ";)";
        HmiAskDialog::Option alias;
        alias.label = "Sous un autre nom (une collision d'espace)";
        alias.hasField = true;
        alias.placeholder = m.name + "2";
        const auto project = std::make_shared<hmi::Project>(current->project);
        alias.check = [project, path, folder](const std::string& name) -> std::string {
            if (trimmedText(name).empty()) return {};
            return hmi::biblio::planLink(*project, path, folder, trimmedText(name)).refusal;
        };
        next.options = {own, alias};
        next.option = plan.ok() ? 0 : 1;
        next.confirm = "Relier";
        next.width = 720.f;
        app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(next)), [this, path](const menu::DialogResult& r2) {
            if (!r2.accepted()) return;
            const auto b = HmiAskDialog::parse(r2.payload);
            auto* pane = dynamic_cast<HmiLibrariesPane*>(hmiTab("bibliotheques"));
            if (!pane) return;
            std::string why;
            if (!pane->link(path, b.option == 1 ? trimmedText(b.field) : std::string{}, &why))
                status_->setTransientMessage("Relier : " + why, 8.0, ui::StatusBar::Severity::Warning);
            else
                status_->setTransientMessage(pane->message(), 8.0, ui::StatusBar::Severity::Success);
        });
    });
}

// --------------------------------------------------------------------------------- publier ---
void MainAnalysisScreen::askHmiPublishLibrary(const std::string& space) {
    auto doc = app_.hmi();
    if (!doc) return;
    openHmiPane("bibliotheques");
    const std::string folder = app_.projectFolder();
    const auto spaces = hmi::names::projectSpaces(doc->project);
    std::vector<std::string> names;
    for (const auto& s : spaces) if (s.total() > 0) names.push_back(s.ns);
    if (names.empty()) {
        status_->setTransientMessage("Publier : aucun espace de noms n'a d'\xC3\xA9l\xC3\xA9ment - range d'abord des types ou des fonctions dans un espace "
                                     "(leur champ Espace de noms)", 9.0, ui::StatusBar::Severity::Warning);
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiPublishLibrary";
    spec.title = "Publier une biblioth\xC3\xA8que";
    spec.text = "Les espaces coch\xC3\xA9s partent dans un fichier .xpgbib (libs/ du projet) : un autre projet le relie, sans le recopier. "
                "Le nom de la biblioth\xC3\xA8que est celui du premier espace coch\xC3\xA9 (son espace de t\xC3\xAAte).";
    spec.listTitle = "Les espaces de noms du projet";
    for (const auto& s : spaces) {
        if (s.total() == 0) continue;
        spec.items.push_back({s.ns, plural(s.types, "type", "types") + ", " + plural(s.functions, "fonction", "fonctions") + ", "
                                        + plural(s.scripts, "script", "scripts"),
                              space.empty() ? s.ns == names.front() : hmi::names::Index(doc->project).spelled(space) == s.ns});
    }
    // La version : celle d'une bibliotheque deja publiee de ce nom (libs/ du projet, ou reliee), et ses suivantes.
    std::string last;
    for (const auto& l : doc->project.libraries) if (l.name == names.front()) last = l.version;
    HmiAskDialog::Option fresh, patch, minor, major, other;
    using hmi::biblio::Bump;
    if (last.empty()) {
        fresh.label = "Premi\xC3\xA8re version : 1.0.0";
        spec.options.push_back(fresh);
    } else {
        patch.label = "Correctif : " + hmi::biblio::bumped(last, Bump::Patch);
        patch.detail = "sans changement de son interface";
        minor.label = "Mineur : " + hmi::biblio::bumped(last, Bump::Minor);
        minor.detail = "des ajouts : les projets reli\xC3\xA9s le prennent sans rien casser";
        major.label = "Majeur : " + hmi::biblio::bumped(last, Bump::Major);
        major.detail = "une rupture (un nom retir\xC3\xA9, une signature chang\xC3\xA9" "e) : un projet reli\xC3\xA9 en 1.x la refuse";
        spec.options = {patch, minor, major};
    }
    other.label = "Une autre version :";
    other.hasField = true;
    other.placeholder = "1.0.0";
    other.check = [](const std::string& v) -> std::string {
        if (trimmedText(v).empty() || hmi::biblio::parseVersion(v).ok) return {};
        return "majeur.mineur.correctif : 1.4.0";
    };
    spec.options.push_back(other);
    spec.note = "Ce qui ne voyage pas (une variable IHM, une fonction de la racine) est dit apr\xC3\xA8s la publication.";
    spec.confirm = "Publier";
    spec.width = 760.f;
    const std::vector<std::string> versions = last.empty()
                                                  ? std::vector<std::string>{"1.0.0"}
                                                  : std::vector<std::string>{hmi::biblio::bumped(last, Bump::Patch), hmi::biblio::bumped(last, Bump::Minor),
                                                                             hmi::biblio::bumped(last, Bump::Major)};
    std::vector<std::string> listed;
    for (const auto& s : spaces) if (s.total() > 0) listed.push_back(s.ns);
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, listed, versions, folder](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = HmiAskDialog::parse(r.payload);
        std::vector<std::string> chosen;
        for (std::size_t i = 0; i < a.items.size() && i < listed.size(); ++i) if (a.items[i]) chosen.push_back(listed[i]);
        if (chosen.empty()) {
            status_->setTransientMessage("Publier : coche au moins un espace", 6.0, ui::StatusBar::Severity::Warning);
            return;
        }
        hmi::biblio::PublishSpec ps;
        ps.name = chosen.front().substr(0, chosen.front().find('.'));
        ps.spaces = chosen;
        ps.version = a.option >= 0 && static_cast<std::size_t>(a.option) < versions.size() ? versions[static_cast<std::size_t>(a.option)]
                                                                                          : trimmedText(a.field);
        if (!hmi::biblio::parseVersion(ps.version).ok) {
            status_->setTransientMessage("Publier : version illisible \xC2\xAB " + ps.version + " \xC2\xBB (1.4.0)", 6.0, ui::StatusBar::Severity::Warning);
            return;
        }
        const std::string file = (std::filesystem::path(folder.empty() ? std::string(".") : folder) / "libs"
                                  / (ps.name + "-" + ps.version + std::string(hmi::biblio::kLibraryExtension))).string();
        auto* pane = dynamic_cast<HmiLibrariesPane*>(hmiTab("bibliotheques"));
        if (!pane) return;
        std::string why;
        if (!pane->publish(ps, file, &why)) status_->setTransientMessage("Publier : " + why, 8.0, ui::StatusBar::Severity::Warning);
        else status_->setTransientMessage(pane->message(), 10.0, ui::StatusBar::Severity::Success);
    });
}

// ----------------------------------------------------------------------------- mettre a jour ---
void MainAnalysisScreen::askHmiUpdateLibrary(const std::string& name) {
    auto doc = app_.hmi();
    if (!doc) return;
    openHmiPane("bibliotheques");
    const std::string folder = app_.projectFolder();
    const auto plan = hmi::biblio::planUpdate(doc->project, name, folder);
    if (!plan.refusal.empty()) {
        status_->setTransientMessage("Mettre \xC3\xA0 jour " + name + " : " + plan.refusal, 8.0, ui::StatusBar::Severity::Warning);
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiUpdateLibrary";
    spec.title = "Mettre \xC3\xA0 jour la biblioth\xC3\xA8que " + name;
    std::size_t added = 0, removed = 0, changed = 0;
    for (const auto& c : plan.changes) {
        using K = hmi::biblio::InterfaceChange::Kind;
        added += c.kind == K::Added;
        removed += c.kind == K::Removed;
        changed += c.kind == K::Changed;
        std::string line = (c.kind == K::Added ? "+ " : c.kind == K::Removed ? "\xE2\x88\x92 " : "~ ") + c.name + " (" + c.what + ")";
        std::string detail = c.kind == K::Added ? c.after : c.kind == K::Removed ? c.before : c.before + "  \xE2\x86\x92  " + c.after;
        if (!c.usedAt.empty()) detail += (detail.empty() ? "" : "  \xC2\xB7  ") + std::string("employ\xC3\xA9 par : ") + c.usedAt.front()
                                       + (c.usedAt.size() > 1 ? " (+" + std::to_string(c.usedAt.size() - 1) + ")" : std::string{});
        spec.items.push_back({line, detail, false});
    }
    spec.plainItems = true;
    spec.listTitle = "Son interface : ajouts (+), retraits (\xE2\x88\x92), signatures chang\xC3\xA9" "es (~)";
    const std::size_t breaks = plan.breakCount();
    spec.text = name + " : " + plan.from + " \xE2\x86\x92 " + plan.to + " \xC2\xB7 " + plural(added, "ajout", "ajouts") + ", "
              + plural(removed, "retrait", "retraits") + ", " + plural(changed, "changement", "changements")
              + (breaks ? "\n\xE2\x9A\xA0 " + plural(breaks, "rupture", "ruptures") + " : un nom employ\xC3\xA9 par le projet dispara\xC3\xAEt ou change "
                              "(G\xC3\xA9n\xC3\xA9rer le dira \xC3\xA0 sa place apr\xC3\xA8s la mise \xC3\xA0 jour)"
                        : std::string("\nAucune rupture : rien de ce que le projet emploie ne change."));
    if (plan.changes.empty()) spec.text += "\nSon interface ne change pas (la version seule).";
    spec.confirm = breaks ? "Appliquer malgr\xC3\xA9 les ruptures" : "Appliquer";
    spec.danger = breaks > 0;
    spec.width = 820.f;
    const std::string file = plan.file;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, name, file](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        auto* pane = dynamic_cast<HmiLibrariesPane*>(hmiTab("bibliotheques"));
        if (!pane) return;
        std::string why;
        if (!pane->update(name, file, &why)) status_->setTransientMessage("Mettre \xC3\xA0 jour : " + why, 8.0, ui::StatusBar::Severity::Warning);
        else status_->setTransientMessage(pane->message(), 8.0, ui::StatusBar::Severity::Success);
    });
}

// ------------------------------------------------------------------------------- localiser ---
void MainAnalysisScreen::askHmiLocateLibrary(const std::string& name) {
    auto doc = app_.hmi();
    if (!doc) return;
    openHmiPane("bibliotheques");
    const std::string folder = app_.projectFolder();
    const auto done = [this, name](std::string path) {
        auto* pane = dynamic_cast<HmiLibrariesPane*>(hmiTab("bibliotheques"));
        if (!pane || path.empty()) return;
        std::string why;
        if (!pane->relocate(name, path, &why)) status_->setTransientMessage("Localiser " + name + " : " + why, 8.0, ui::StatusBar::Severity::Warning);
        else status_->setTransientMessage(pane->message(), 6.0, ui::StatusBar::Severity::Success);
    };
    if (ui::browsePath(ui::openFile("Biblioth\xC3\xA8ques IHM (.xpgbib)|*.xpgbib", ui::pathIn(folder, "libs"), "Localiser la biblioth\xC3\xA8que " + name), "",
                       done))
        return;
    // Sans explorateur (essais, console) : le chemin se tape.
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiLocateLibrary";
    spec.title = "Localiser la biblioth\xC3\xA8que " + name;
    HmiAskDialog::Option file;
    file.label = "Son fichier (.xpgbib)";
    file.hasField = true;
    spec.options.push_back(file);
    spec.confirm = "Localiser";
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [done](const menu::DialogResult& r) {
        if (r.accepted()) done(trimmedText(HmiAskDialog::parse(r.payload).field));
    });
}

// ------------------------------------------------------------------------ espaces de noms ---
void MainAnalysisScreen::askHmiNewNamespace() {
    auto doc = app_.hmi();
    if (!doc) return;
    openHmiPane("bibliotheques");
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiNewNamespace";
    spec.title = "Nouvel espace de noms";
    spec.text = "Un espace de noms est un pr\xC3\xA9" "fixe : Gaz.T_Vanne, Util.Moyenne. Range ensuite un type, une fonction ou un script dedans "
                "(son champ Espace de noms).";
    HmiAskDialog::Option name;
    name.label = "Son nom (Gaz, Util, Gaz.Ligne)";
    name.hasField = true;
    const auto project = std::make_shared<hmi::Project>(doc->project);
    name.check = [project](const std::string& v) -> std::string {
        std::string why;
        if (trimmedText(v).empty() || hmi::names::validNewNamespace(*project, trimmedText(v), &why)) return {};
        return why;
    };
    spec.options.push_back(name);
    spec.confirm = "Cr\xC3\xA9" "er";
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        auto* pane = dynamic_cast<HmiLibrariesPane*>(hmiTab("bibliotheques"));
        if (!pane) return;
        std::string why;
        if (!pane->addNamespace(trimmedText(HmiAskDialog::parse(r.payload).field), &why))
            status_->setTransientMessage("Nouvel espace : " + why, 6.0, ui::StatusBar::Severity::Warning);
    });
}

void MainAnalysisScreen::askHmiRenameNamespace(const std::string& ns) {
    auto doc = app_.hmi();
    if (!doc) return;
    openHmiPane("bibliotheques");
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiRenameNamespace";
    spec.title = "Renommer l'espace " + ns;
    spec.text = "Ses types, ses fonctions, ses scripts, les USING et tous les noms complets (" + ns + ".Nom) suivent - en une commande "
                "(Ctrl+Z l'annule).";
    HmiAskDialog::Option name;
    name.label = "Son nouveau nom";
    name.hasField = true;
    name.field = ns;
    const auto project = std::make_shared<hmi::Project>(doc->project);
    name.check = [project, ns](const std::string& v) -> std::string {
        const std::string t = trimmedText(v);
        std::string why;
        if (t.empty() || t == ns || hmi::names::validNewNamespace(*project, t, &why)) return {};
        return why;
    };
    spec.options.push_back(name);
    spec.confirm = "Renommer";
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, ns](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        auto* pane = dynamic_cast<HmiLibrariesPane*>(hmiTab("bibliotheques"));
        if (!pane) return;
        std::string why;
        if (!pane->renameNamespace(ns, trimmedText(HmiAskDialog::parse(r.payload).field), &why))
            status_->setTransientMessage("Renommer l'espace : " + why, 6.0, ui::StatusBar::Severity::Warning);
    });
}

// ------------------------------------------------------------- une definition, en lecture seule ---
void MainAnalysisScreen::showHmiDefinition(const std::string& full) {
    auto doc = app_.hmi();
    if (!doc) return;
    const auto& p = doc->project;
    std::string text;
    std::string from;
    if (const hmi::HmiType* t = p.hmiTypeByName(full)) {
        text = "TYPE " + hmi::fullName(*t) + " :\n";
        for (const auto& m : t->members) text += "    " + m.name + " : " + m.type + (m.unit.empty() ? std::string{} : " [" + m.unit + "]") + ";\n";
        for (const auto& v : t->values) text += "    " + v.name + " := " + std::to_string(v.value) + ";\n";
        for (const auto& m : t->methods) text += "    METHOD " + m.name + (m.returnType.empty() ? std::string{} : " : " + m.returnType) + "\n";
        text += "END_TYPE";
        if (const auto* lib = p.libraryOf(t->id)) from = "biblioth\xC3\xA8que " + lib->name + " " + lib->foundVersion;
    } else if (const hmi::HmiFunction* f = p.functionByName(full)) {
        text = "FUNCTION " + hmi::fullName(*f) + " " + hmi::biblio::signatureOf(*f) + "\n" + f->body;
        if (const auto* lib = p.libraryOf(f->id)) from = "biblioth\xC3\xA8que " + lib->name + " " + lib->foundVersion;
    } else if (const hmi::Script* s = p.generalScript(full)) {
        text = "(* script " + hmi::fullName(*s) + " *)\n" + s->body;
        if (const auto* lib = p.libraryOf(s->id)) from = "biblioth\xC3\xA8que " + lib->name + " " + lib->foundVersion;
    } else {
        status_->setTransientMessage(full + " : introuvable", 5.0, ui::StatusBar::Severity::Warning);
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiDefinition";
    spec.title = full + (from.empty() ? std::string{} : "  \xC2\xB7  " + from + " - lecture seule");
    spec.text = text;
    spec.confirm = "Fermer";
    spec.cancel = "Retour";
    spec.width = 760.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [](const menu::DialogResult&) {});
}

} // namespace app
