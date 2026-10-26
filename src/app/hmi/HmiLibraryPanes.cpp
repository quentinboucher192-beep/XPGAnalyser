// =============================================================================
//  app/hmi/HmiLibraryPanes.cpp - 1.13.0 (lot J8) : IHM > Espaces et bibliotheques
//  (voir HmiLibraryPanes.hpp)
// =============================================================================
#include "HmiLibraryPanes.hpp"
#include "HmiImages.hpp"        // hmiProjectFolder
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiNames.hpp"

#include <algorithm>

namespace app {

namespace {
using PG = ui::PropertyGrid;
using hmi::kNoId;
using hmikit::prop;

ui::Tone toneOf(hmi::LibraryState s) {
    switch (s) {
        case hmi::LibraryState::Linked: return ui::Tone::Ok;
        case hmi::LibraryState::Newer: return ui::Tone::Info;
        case hmi::LibraryState::Older: return ui::Tone::Warning;
        case hmi::LibraryState::Incompatible:
        case hmi::LibraryState::Missing:
        case hmi::LibraryState::Unreadable: return ui::Tone::Error;
    }
    return ui::Tone::None;
}
std::string countText(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
} // namespace

HmiLibrariesPane::HmiLibrariesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ALink, HmiGlyph::Link, "Relier une biblioth\xC3\xA8que\xE2\x80\xA6 : un fichier .xpgbib - son code reste dans le fichier, le projet garde le lien",
               "Relier");
    tools->add(APublish, HmiGlyph::Export, "Publier une biblioth\xC3\xA8que\xE2\x80\xA6 : un espace de noms du projet dans un fichier .xpgbib (nom, version)",
               "Publier");
    tools->add(AUpdate, HmiGlyph::Refresh, "Mettre \xC3\xA0 jour la biblioth\xC3\xA8que choisie : le diff de son interface, puis Appliquer (Ctrl+Z le d\xC3\xA9" "fait)",
               "Mettre \xC3\xA0 jour");
    tools->add(ALocate, HmiGlyph::Search, "Localiser\xE2\x80\xA6 : le fichier de la biblioth\xC3\xA8que a boug\xC3\xA9", "Localiser");
    tools->add(AUnlink, HmiGlyph::Delete, "D\xC3\xA9lier la biblioth\xC3\xA8que choisie (Ctrl+Z la relie)", "D\xC3\xA9lier");
    tools->separator();
    tools->add(ANewSpace, HmiGlyph::Plus, "Nouvel espace de noms (vide : tu y ranges ensuite des types, des fonctions, des scripts)", "Espace");
    tools->add(ARenameSpace, HmiGlyph::Text, "Renommer l'espace choisi : ses types, ses fonctions, ses scripts et tous leurs emplois suivent",
               "Renommer");
    tools->add(ARemoveSpace, HmiGlyph::Delete, "Retirer l'espace vide choisi", "Retirer");
    tools->separator();
    tools->add(AReload, HmiGlyph::Refresh, "Relire les biblioth\xC3\xA8ques sur le disque (un fichier a chang\xC3\xA9 \xC3\xA0 c\xC3\xB4t\xC3\xA9)", "Relire");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto hasLibrary = [this] { return !focusSpaces_ && !selectedLibrary().empty(); };
    tools_->setEnabledWhen(AUpdate, hasLibrary);
    tools_->setEnabledWhen(ALocate, hasLibrary);
    tools_->setEnabledWhen(AUnlink, hasLibrary);
    tools_->setEnabledWhen(ARenameSpace, [this] { return focusSpaces_ && !selectedNamespace().empty(); });
    tools_->setEnabledWhen(ARemoveSpace, [this] {
        if (!focusSpaces_) return false;
        const std::string ns = selectedNamespace();
        for (const auto& s : hmi::names::projectSpaces(doc_->project))
            if (hmikit::same(s.ns, ns)) return s.total() == 0;
        return false;
    });
    tools_->setEnabledWhen(AReload, [this] { return !doc_->project.libraries.empty(); });

    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".libsPanel", "BIBLIOTH\xC3\x88QUES RELI\xC3\x89" "ES");
        auto table = std::make_unique<ui::TableView>(base + ".libs");
        // 1.13.0 (correctifs F2) : "1.0.0 (trouvee 1.1.0)" et "plus recente disponible" tiennent (avant : 110 et 150 px, coupes).
        table->setColumns({{"Biblioth\xC3\xA8que", 140.f}, {"Version", 190.f}, {"\xC3\x89tat", 200.f}, {"Chemin", 220.f}, {"Contenu", 190.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        libraries_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        librariesPanel_ = &static_cast<HmiTitledPanel&>(addChild(std::move(panel)));
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".spacesPanel", "ESPACES DE NOMS DU PROJET");
        auto table = std::make_unique<ui::TableView>(base + ".spaces");
        table->setColumns({{"Espace", 180.f}, {"Types", 70.f, 40.f, true, true, true, ui::Align::End},
                           {"Fonctions", 80.f, 40.f, true, true, true, ui::Align::End},
                           {"Scripts", 70.f, 40.f, true, true, true, ui::Align::End}, {"Remarque", 260.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        spaces_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        spacesPanel_ = &static_cast<HmiTitledPanel&>(addChild(std::move(panel)));
    }
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(false);
    grid->setNameColumnRatio(0.38f);
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const std::string lib = selectedLibrary(), ns = selectedNamespace();
        switch (a) {
            case ALink: if (hosts_.askLink) hosts_.askLink(); else say("Relier : choisis un fichier .xpgbib (IHM \xE2\x80\xBA Biblioth\xC3\xA8ques).", true); break;
            case APublish: if (hosts_.askPublish) hosts_.askPublish(); break;
            case AUpdate:
                if (lib.empty()) break;
                if (hosts_.askUpdate) hosts_.askUpdate(lib);
                else {
                    std::string why;
                    if (!update(lib, {}, &why)) say(why, true);
                }
                break;
            case ALocate: if (!lib.empty() && hosts_.askLocate) hosts_.askLocate(lib); break;
            case AUnlink: if (!lib.empty()) { std::string why; if (!unlink(lib, &why)) say(why, true); } break;
            case AReload: reload(); break;
            case ANewSpace: if (hosts_.askNewNamespace) hosts_.askNewNamespace(); break;
            case ARenameSpace: if (!ns.empty() && hosts_.askRenameNamespace) hosts_.askRenameNamespace(ns); break;
            case ARemoveSpace: if (!ns.empty()) { std::string why; if (!removeNamespace(ns, &why)) say(why, true); } break;
            default: break;
        }
    });
    links_ += libraries_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (!rows.empty()) focusSpaces_ = false;
        rebuildProperties();
    });
    links_ += spaces_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (!rows.empty()) focusSpaces_ = true;
        rebuildProperties();
    });
    links_ += doc_->changed->connect([this](hmi::Id) { refresh(); });
    refresh();
}

std::string HmiLibrariesPane::projectFolder() { return hmiProjectFolder(); }

void HmiLibrariesPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiLibrariesPane::selectedLibrary() const {
    const int r = hmikit::selectedRow(*libraries_);
    return r >= 0 && static_cast<std::size_t>(r) < libraryNames_.size() ? libraryNames_[static_cast<std::size_t>(r)] : std::string{};
}

void HmiLibrariesPane::selectLibrary(const std::string& name) {
    for (std::size_t i = 0; i < libraryNames_.size(); ++i)
        if (hmikit::same(libraryNames_[i], name)) {
            focusSpaces_ = false;
            libraries_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
            rebuildProperties();
            return;
        }
}

std::string HmiLibrariesPane::selectedNamespace() const {
    const int r = hmikit::selectedRow(*spaces_);
    return r >= 0 && static_cast<std::size_t>(r) < spaceNames_.size() ? spaceNames_[static_cast<std::size_t>(r)] : std::string{};
}

void HmiLibrariesPane::selectNamespace(const std::string& ns) {
    for (std::size_t i = 0; i < spaceNames_.size(); ++i)
        if (hmikit::same(spaceNames_[i], ns)) {
            focusSpaces_ = true;
            spaces_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
            rebuildProperties();
            return;
        }
}

void HmiLibrariesPane::refresh() {
    const auto& p = doc_->project;
    const std::string keepLib = selectedLibrary(), keepNs = selectedNamespace();
    libraryNames_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::Tone> tones;
    for (const auto& l : p.libraries) {
        libraryNames_.push_back(l.name);
        const hmi::LoadedLibrary* ld = p.loadedLibrary(l.name);
        const hmi::LibraryState st = ld ? ld->state : hmi::LibraryState::Missing;
        std::string version = l.version;
        if (ld && !ld->foundVersion.empty() && ld->foundVersion != l.version) version += " (trouv\xC3\xA9" "e " + ld->foundVersion + ")";
        // 1.13.0 (correctifs F2) : le contenu de la VERSION RELIEE (son interface, ce que dit "Son interface" a cote) -
        // avant : celui du fichier trouve (Util reliee en 1.0.0 disait "1 type, 3 fonctions", ceux de la 1.1.0 trouvee).
        std::vector<std::string> parts;
        if (!l.types.empty()) parts.push_back(countText(l.types.size(), "type", "types"));
        if (!l.functions.empty()) parts.push_back(countText(l.functions.size(), "fonction", "fonctions"));
        if (!l.scripts.empty()) parts.push_back(countText(l.scripts.size(), "script", "scripts"));
        std::string contents;
        for (std::size_t k = 0; k < parts.size(); ++k) contents += (k ? ", " : "") + parts[k];
        if (contents.empty() && ld) {   // un lien sans cache d'interface (un projet d'avant) : ce qui a ete lu
            hmi::Project shown;
            shown.programs = ld->content;
            contents = hmi::biblio::contentsText(shown);
        }
        rows.push_back({l.name + (l.published.empty() ? std::string{} : " (" + l.published + ")"), version,
                        std::string(hmi::biblio::stateLabel(st)), l.path, contents});
        tones.push_back(toneOf(st));
    }
    libraries_->setModel(std::make_shared<hmikit::Rows>(
        std::vector<std::string>{"Biblioth\xC3\xA8que", "Version", "\xC3\x89tat", "Chemin", "Contenu"}, std::move(rows),
        [tones](ui::RowIndex r, std::size_t c) {
            ui::CellStyle cs;
            if (c == 0) {
                cs.icon = ui::Icon::Lock;          // une bibliotheque est en lecture seule
                cs.iconTone = r < tones.size() ? tones[r] : ui::Tone::None;
            }
            if (c == 2 && r < tones.size()) {
                cs.fgTone = tones[r];
                cs.bold = tones[r] == ui::Tone::Error;
            }
            return cs;
        }));
    spaceNames_.clear();
    std::vector<std::vector<std::string>> srows;
    for (const auto& s : hmi::names::projectSpaces(p)) {
        spaceNames_.push_back(s.ns);
        const std::string clash = hmi::names::reservedClash(p, s.ns);
        const std::string note = !clash.empty() ? clash : s.total() == 0 ? std::string("vide (gard\xC3\xA9)") : std::string{};
        srows.push_back({s.ns, std::to_string(s.types), std::to_string(s.functions), std::to_string(s.scripts), note});
    }
    spaces_->setModel(std::make_shared<hmikit::Rows>(
        std::vector<std::string>{"Espace", "Types", "Fonctions", "Scripts", "Remarque"}, std::move(srows),
        [](ui::RowIndex, std::size_t c) {
            ui::CellStyle cs;
            if (c == 0) cs.icon = ui::Icon::Folder;
            return cs;
        }));
    librariesPanel_->setTitle("BIBLIOTH\xC3\x88QUES RELI\xC3\x89" "ES (" + std::to_string(p.libraries.size()) + ")");
    spacesPanel_->setTitle("ESPACES DE NOMS DU PROJET (" + std::to_string(spaceNames_.size()) + ")");
    if (!keepLib.empty()) for (std::size_t i = 0; i < libraryNames_.size(); ++i) if (hmikit::same(libraryNames_[i], keepLib)) libraries_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    if (!keepNs.empty()) for (std::size_t i = 0; i < spaceNames_.size(); ++i) if (hmikit::same(spaceNames_[i], keepNs)) spaces_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    if (message_.empty())
        status_->setMessage(p.libraries.empty() && spaceNames_.empty()
                                ? std::string("Aucun espace ni biblioth\xC3\xA8que : range un type ou une fonction dans un espace (son champ Espace de noms), "
                                              "ou relie une biblioth\xC3\xA8que (Relier\xE2\x80\xA6).")
                                : countText(p.libraries.size(), "biblioth\xC3\xA8que reli\xC3\xA9" "e", "biblioth\xC3\xA8ques reli\xC3\xA9" "es") + ", "
                                      + countText(spaceNames_.size(), "espace du projet", "espaces du projet")
                                      + ". Une biblioth\xC3\xA8que est en lecture seule : son code reste dans son fichier.");
    rebuildProperties();
    invalidate();
}

void HmiLibrariesPane::rebuildProperties() {
    const auto& p = doc_->project;
    std::vector<PG::Category> cats;
    if (focusSpaces_) {
        const std::string ns = selectedNamespace();
        if (ns.empty()) { grid_->clearProperties(); return; }
        PG::Category c;
        c.name = "Espace " + ns;
        c.properties.push_back(prop("Nom", ns, PG::ValueType::Text, [this, ns](std::string_view v) {
            std::string why;
            const bool ok = renameNamespace(ns, std::string(v), &why);
            if (!ok) say(why, true);
            return ok;
        }, "Renommer l'espace : ses items, les USING et tous les noms complets (Gaz.T_Vanne) suivent, en une commande."));
        for (const auto& s : hmi::names::projectSpaces(p)) {
            if (!hmikit::same(s.ns, ns)) continue;
            c.properties.push_back(prop("Contenu", countText(s.types, "type", "types") + ", " + countText(s.functions, "fonction", "fonctions") + ", "
                                                       + countText(s.scripts, "script", "scripts"), PG::ValueType::ReadOnly));
        }
        c.properties.push_back(prop("S'emploie", ns + ".Nom partout ; USING " + ns + "; en t\xC3\xAAte d'un corps pour \xC3\xA9" "crire Nom seul",
                                    PG::ValueType::ReadOnly));
        if (const std::string clash = hmi::names::reservedClash(p, ns); !clash.empty())
            c.properties.push_back(prop("Attention", clash, PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        PG::Category items;
        items.name = "Ses \xC3\xA9l\xC3\xA9ments";
        const hmi::names::Index ix(p);
        for (const auto& name : ix.itemsOf(ns))
            items.properties.push_back(prop(hmi::qualifiedName(ns, name), std::string(hmi::names::itemKindLabel(ix.kindIn(ns, name))).substr(3),
                                            PG::ValueType::ReadOnly));
        if (items.properties.empty()) items.properties.push_back(prop("Aucun", "range un type, une fonction ou un script ici (son champ Espace de noms)",
                                                                      PG::ValueType::ReadOnly));
        cats.push_back(std::move(items));
        grid_->setCategories(std::move(cats));
        return;
    }
    const std::string name = selectedLibrary();
    const hmi::Library* l = name.empty() ? nullptr : p.libraryByName(name);
    if (!l) { grid_->clearProperties(); return; }
    const hmi::LoadedLibrary* ld = p.loadedLibrary(name);
    PG::Category c;
    c.name = "Biblioth\xC3\xA8que " + l->name;
    c.properties.push_back(prop("Espace de noms", l->name, PG::ValueType::ReadOnly));
    if (!l->published.empty()) c.properties.push_back(prop("Publi\xC3\xA9" "e sous", l->published, PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Version reli\xC3\xA9" "e", l->version, PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Plancher demand\xC3\xA9", (l->compat.empty() ? l->version : l->compat) + " (m\xC3\xAAme majeur, mineur sup\xC3\xA9rieur ou \xC3\xA9gal)",
                                PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Chemin", l->path, PG::ValueType::ReadOnly));
    if (ld) {
        c.properties.push_back(prop("\xC3\x89tat", std::string(hmi::biblio::stateLabel(ld->state)) + " \xC2\xB7 " + ld->message, PG::ValueType::ReadOnly));
        if (!ld->foundPath.empty()) c.properties.push_back(prop("Fichier trouv\xC3\xA9", ld->foundPath, PG::ValueType::ReadOnly));
        if (!ld->foundVersion.empty()) c.properties.push_back(prop("Version trouv\xC3\xA9" "e", ld->foundVersion, PG::ValueType::ReadOnly));
        if (ld->fromCache)
            c.properties.push_back(prop("Contenu", "l'interface seule (le cache du projet) : le code ne tourne pas", PG::ValueType::ReadOnly));
    }
    c.properties.push_back(prop("Lecture seule", "ses types et ses fonctions s'emploient, ils ne s'\xC3\xA9" "ditent pas", PG::ValueType::ReadOnly));
    cats.push_back(std::move(c));
    PG::Category face;
    face.name = "Son interface";
    for (const auto& t : l->types)
        face.properties.push_back(prop(hmi::qualifiedName(l->name, t.name), (t.kind.empty() ? std::string("structure") : t.kind)
                                                                                   + (t.members.empty() ? std::string{} : " \xC2\xB7 " + t.members),
                                       PG::ValueType::ReadOnly));
    for (const auto& f : l->functions)
        face.properties.push_back(prop(hmi::qualifiedName(l->name, f.name), f.signature, PG::ValueType::ReadOnly));
    for (const auto& s : l->scripts)
        face.properties.push_back(prop(hmi::qualifiedName(l->name, s), "script \xC2\xB7 IHM_APPELER('" + hmi::qualifiedName(l->name, s) + "')",
                                       PG::ValueType::ReadOnly));
    if (hosts_.openDefinition)
        for (auto& pr : face.properties) {
            const std::string full = pr.name;
            pr.dialog = [this, full] { hosts_.openDefinition(full); };
        }
    cats.push_back(std::move(face));
    grid_->setCategories(std::move(cats));
}

bool HmiLibrariesPane::commit(const std::string& label, const std::function<bool(hmi::Project&, std::string*)>& change, std::string* why,
                              const std::string& done) {
    std::string reason;
    bool ok = true;
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { ok = change(p, &reason); });
    if (!ok) {
        if (why) *why = reason;
        return false;
    }
    if (cmd) apply_(std::move(cmd));
    // 1.13.0 (correctifs F2) : la ligne d'etat dit le RESULTAT ("Mesures 1.0.0 reliee") ; `label` reste le nom de la
    // commande (Edition > Annuler Relier la bibliotheque Mesures 1.0.0).
    say((done.empty() ? label : done) + " (Ctrl+Z l'annule)");
    refresh();
    return true;
}

bool HmiLibrariesPane::link(const std::string& file, const std::string& alias, std::string* why) {
    const std::string folder = projectFolder();
    const auto plan = hmi::biblio::planLink(doc_->project, file, folder, alias);
    if (!plan.ok()) {
        if (why) *why = plan.refusal;
        return false;
    }
    const bool ok = commit("Relier la biblioth\xC3\xA8que " + plan.link.name + " " + plan.link.version,
                           [&](hmi::Project& p, std::string* w) {
                               if (hmi::biblio::link(p, plan, folder)) return true;
                               if (w) *w = "la biblioth\xC3\xA8que " + plan.link.name + " est d\xC3\xA9j\xC3\xA0 reli\xC3\xA9" "e";
                               return false;
                           },
                           why, plan.link.name + " " + plan.link.version + " reli\xC3\xA9" "e");
    if (ok) {
        if (!plan.warnings.empty()) say(plan.warnings.front());
        selectLibrary(plan.link.name);
    }
    return ok;
}

bool HmiLibrariesPane::unlink(const std::string& name, std::string* why) {
    return commit("D\xC3\xA9lier la biblioth\xC3\xA8que " + name, [&](hmi::Project& p, std::string* w) {
        if (hmi::biblio::unlink(p, name)) return true;
        if (w) *w = "aucune biblioth\xC3\xA8que " + name;
        return false;
    }, why, name + " d\xC3\xA9li\xC3\xA9" "e");
}

bool HmiLibrariesPane::relocate(const std::string& name, const std::string& file, std::string* why) {
    const std::string folder = projectFolder();
    std::string where;
    const bool ok = commit("Localiser la biblioth\xC3\xA8que " + name,
                           [&](hmi::Project& p, std::string* w) {
                               if (!hmi::biblio::relocate(p, name, file, folder, w)) return false;
                               if (const auto* l = p.libraryByName(name)) where = l->path;
                               return true;
                           },
                           why, name + " localis\xC3\xA9" "e");
    if (ok && !where.empty()) say(name + " localis\xC3\xA9" "e : " + where + " (Ctrl+Z l'annule)");
    return ok;
}

bool HmiLibrariesPane::update(const std::string& name, const std::string& file, std::string* why) {
    const std::string folder = projectFolder();
    const auto plan = hmi::biblio::planUpdate(doc_->project, name, folder, file);
    if (!plan.refusal.empty()) {
        if (why) *why = plan.refusal;
        return false;
    }
    return commit("Mettre \xC3\xA0 jour " + name + " : " + plan.from + " \xE2\x86\x92 " + plan.to,
                  [&](hmi::Project& p, std::string*) { return hmi::biblio::applyUpdate(p, plan, folder); }, why,
                  name + " mise \xC3\xA0 jour : " + plan.from + " \xE2\x86\x92 " + plan.to);
}

bool HmiLibrariesPane::publish(const hmi::biblio::PublishSpec& spec, const std::string& file, std::string* why) {
    const auto plan = hmi::biblio::collect(doc_->project, spec);
    if (!plan.refusal.empty()) {
        if (why) *why = plan.refusal;
        return false;
    }
    if (const auto st = hmi::biblio::writeFile(plan.package, file); !st) {
        if (why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
        return false;
    }
    say("Biblioth\xC3\xA8que " + spec.name + " " + spec.version + " publi\xC3\xA9" "e : " + file + " (" + hmi::biblio::contentsText(plan.package.content) + ")"
        + (plan.missing.empty() ? std::string{} : " - manque : " + plan.missing.front()));
    return true;
}

bool HmiLibrariesPane::addNamespace(const std::string& ns, std::string* why) {
    const bool ok = commit("Nouvel espace de noms " + ns, [&](hmi::Project& p, std::string* w) { return hmi::names::addEmptyNamespace(p, ns, w); }, why,
                           "Espace de noms " + ns + " cr\xC3\xA9\xC3\xA9");
    if (ok) selectNamespace(ns);
    return ok;
}

bool HmiLibrariesPane::renameNamespace(const std::string& from, const std::string& to, std::string* why) {
    std::string target = to;
    while (!target.empty() && target.back() == ' ') target.pop_back();
    if (hmikit::same(from, target)) return true;
    for (const auto& l : doc_->project.libraries)
        if (hmikit::same(l.name, from)) {
            if (why) *why = from + " est une biblioth\xC3\xA8que reli\xC3\xA9" "e : d\xC3\xA9lie-la et relie-la sous un autre nom";
            return false;
        }
    std::string w;
    if (!hmi::names::validNewNamespace(doc_->project, target, &w)) {
        if (why) *why = w;
        return false;
    }
    const bool ok = commit("Renommer l'espace " + from + " en " + target, [&](hmi::Project& p, std::string*) {
        hmi::names::renameNamespace(p, from, target);
        return true;
    }, why, "Espace " + from + " renomm\xC3\xA9 en " + target);
    if (ok) selectNamespace(target);
    return ok;
}

bool HmiLibrariesPane::removeNamespace(const std::string& ns, std::string* why) {
    for (const auto& s : hmi::names::projectSpaces(doc_->project))
        if (hmikit::same(s.ns, ns) && s.total() > 0) {
            if (why) *why = "l'espace " + ns + " range encore " + std::to_string(s.total()) + " \xC3\xA9l\xC3\xA9ment(s) : vide son champ Espace de noms d'abord";
            return false;
        }
    return commit("Retirer l'espace " + ns, [&](hmi::Project& p, std::string* w) {
        if (hmi::names::removeEmptyNamespace(p, ns)) return true;
        if (w) *w = "l'espace " + ns + " n'est pas un espace vide garde";
        return false;
    }, why, "Espace " + ns + " retir\xC3\xA9");
}

void HmiLibrariesPane::reload() {
    // Le contenu lu n'est pas une modification du projet (rien a annuler) : il se relit sur place.
    hmi::biblio::resolve(doc_->project, projectFolder());
    say("Biblioth\xC3\xA8ques relues sur le disque.");
    doc_->changed->emit(kNoId);
}

void HmiLibrariesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float gridW = std::min(460.f, b.w * 0.38f);
    const float h = std::max(0.f, b.h - 66);
    const float top = b.y + 40;
    librariesPanel_->setBounds({b.x, top, std::max(0.f, b.w - gridW - 4), h * 0.55f});
    spacesPanel_->setBounds({b.x, top + h * 0.55f + 2, std::max(0.f, b.w - gridW - 4), h * 0.45f - 2});
    grid_->setBounds({b.x + b.w - gridW, top, gridW, h});
}

} // namespace app
