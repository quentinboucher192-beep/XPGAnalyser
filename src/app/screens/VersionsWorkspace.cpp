// =============================================================================
//  app/screens/VersionsWorkspace.cpp - les versions du projet (lot 21)
// -----------------------------------------------------------------------------
//  CE QUE L'ECRAN APPORTE AU VOLET VERSIONS : les dialogues (creer, restaurer,
//  supprimer, extraire), la relecture du projet apres une restauration, l'arbre
//  (Versions, a la racine), l'onglet "Comparer", et la restauration d'UN
//  element (une vue, un script, une section...) en une commande - Ctrl+Z
//  l'annule, comme toute modification.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../VersionComparePane.hpp"
#include "../VersionsPane.hpp"
#include "../VersionClose.hpp"
#include "../TopBar.hpp"
#include "../tutorial/TutorialApp.hpp"   // 1.11.1 (T1) : tutorials::isSandboxFolder
#include "../../hmi/HmiVersionState.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../project/EditCommands.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <thread>
#include <map>
#include <system_error>

namespace app {

namespace ver = hmi::ver;
using ui::Icon;
using ui::StatusBar;
using ui::TabControl;

namespace {

std::string lowerText(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "2026-09-26 18:40" -> "26/09 a 18:40".
std::string whenText(const std::string& stamp) {
    if (stamp.size() < 16) return stamp;
    return stamp.substr(8, 2) + "/" + stamp.substr(5, 2) + " \xC3\xA0 " + stamp.substr(11, 5);
}

// Un element nomme d'une liste de l'IHM : le remettre tel qu'il etait dans `from`.
template <class T, class Name>
void restoreByName(std::vector<T>& now, const std::vector<T>& from, const std::string& name, Name nameOf) {
    const auto same = [&](const T& x) { return lowerText(nameOf(x)) == lowerText(name); };
    const auto old = std::find_if(from.begin(), from.end(), same);
    const auto cur = std::find_if(now.begin(), now.end(), same);
    if (old == from.end()) {
        if (cur != now.end()) now.erase(cur);          // il n'existait pas : il s'en va
    } else if (cur != now.end()) {
        *cur = *old;
    } else {
        now.push_back(*old);
    }
}

// Par identifiant (les vues, les scripts, les fonctions).
template <class T>
void restoreById(std::vector<T>& now, const std::vector<T>& from, hmi::Id id) {
    const auto old = std::find_if(from.begin(), from.end(), [&](const T& x) { return x.id == id; });
    const auto cur = std::find_if(now.begin(), now.end(), [&](const T& x) { return x.id == id; });
    if (old == from.end()) {
        if (cur != now.end()) now.erase(cur);
    } else if (cur != now.end()) {
        *cur = *old;
    } else {
        now.push_back(*old);
    }
}

}   // namespace

void MainAnalysisScreen::refreshVersions() {
    if (!treeModel_) return;
    const std::string folder = app_.projectFolder();
    std::vector<ProjectTreeModel::VersionRow> rows;
    auto store = ver::open(folder);
    const std::size_t unsaved = app_.pendingChanges();
    std::size_t changed = 0;
    std::vector<std::string> treeChanged;     // Lot API 8 : l'arbre du projet (le point orange : les categories changees)
    // 1.11.1 (T1, R111-14 / R111-24) : pas dans le bac a sable d'un tutoriel. La comparaison
    // relit tout le bac apres chaque remise en place (0,9 a 1,8 s) ; le bac est une copie
    // jetable : ni pastille ni points oranges. Les lignes des versions restent (des tutoriels les visent).
    if (store && store->last() && !tutorials::isSandboxFolder(folder))
        if (auto c = ver::compare(*store, store->last()->number, 0)) {
            changed = c->elements.size();
            for (const auto& e : c->elements) treeChanged.push_back(e.category);   // Lot API 8 : l'arbre du projet
        }
    ProjectTreeModel::VersionRow work;
    work.number = 0;
    const bool modified = changed > 0 || unsaved > 0 || !store || store->versions.empty();
    work.label = std::string("Travail en cours \xC2\xB7 ") + (modified ? "modifi\xC3\xA9" : "\xC3\xA0 jour");
    work.tone = modified ? ui::Tone::Warning : ui::Tone::Ok;
    if (changed > 0) work.badge = std::to_string(changed);
    rows.push_back(work);
    if (store)
        for (auto it = store->versions.rbegin(); it != store->versions.rend(); ++it) {
            ProjectTreeModel::VersionRow r;
            r.number = it->number;
            r.label = it->label();
            r.tone = it->state == ver::State::Delivered ? ui::Tone::Ok
                   : it->state == ver::State::Validated ? ui::Tone::Info
                   : it->state == ver::State::BeforeRestore ? ui::Tone::Warning : ui::Tone::Muted;
            rows.push_back(r);
        }
    treeModel_->setVersions(std::move(rows));
    setTreeChanges(treeChanged, store && store->last() ? store->last()->number : 0);   // Lot API 8 : l'arbre du projet
    if (auto* pane = dynamic_cast<VersionsPane*>(hmiTab("versions"))) {
        pane->setFolder(folder);
        pane->refresh();
    }
}

void MainAnalysisScreen::openVersions(int select) {
    openHmiPane("versions");
    if (auto* pane = dynamic_cast<VersionsPane*>(hmiTab("versions"))) {
        pane->setFolder(app_.projectFolder());
        if (select >= 0) pane->selectVersion(select);
    }
}

void MainAnalysisScreen::askCreateVersion() {
    const std::string folder = app_.projectFolder();
    if (folder.empty()) {
        if (status_) status_->setTransientMessage("Cr\xC3\xA9" "er une version : le projet n'a pas encore de dossier (Enregistrer sous\xE2\x80\xA6 d'abord)",
                                                  8.0, StatusBar::Severity::Warning);
        return;
    }
    auto store = ver::open(folder);
    const int next = store ? store->nextNumber() : 1;
    const std::size_t unsaved = app_.pendingChanges();
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", "", "Mise en service, Essais usine, Livr\xC3\xA9" "e au client\xE2\x80\xA6", false, {}});
    fields.push_back({"\xC3\x89tat", "Brouillon", "", false, {"Brouillon", "Valid\xC3\xA9" "e", "Livr\xC3\xA9" "e"}});
    fields.push_back({"Commentaire", "", "ce qui la distingue (essais faits, seuils ajust\xC3\xA9s\xE2\x80\xA6)", false, {}});
    if (unsaved > 0)
        fields.push_back({"Enregistrer d'abord le projet", "oui (" + std::to_string(unsaved) + " modification" + (unsaved > 1 ? "s)" : ")"), "",
                          false, {"oui (" + std::to_string(unsaved) + " modification" + (unsaved > 1 ? "s)" : ")"), "non : la version prend le projet tel qu'il est sur le disque"}});
    // Lot API 6 : Validee termine la version (FINISH), Livree la verrouille (LOCK).
    fields.push_back({"Mot de passe (Livr\xC3\xA9" "e seulement)", "", "Livr\xC3\xA9" "e : le projet sera verrouill\xC3\xA9 (LOCK)", true, {}});
    std::string text = "Num\xC3\xA9ro : V" + std::to_string(next)
                     + ".\nEntre dans la version : l'API, l'IHM et ses ressources, les donn\xC3\xA9" "es. Pas les exports, ni les historiques de marche."
                       "\n\xC3\x89tat : Brouillon laisse le projet en DEV ; Valid\xC3\xA9" "e le passe FINISH (la version est termin\xC3\xA9" "e) ; "
                       "Livr\xC3\xA9" "e le verrouille (LOCK, un mot de passe).";
    auto dialog = std::make_unique<FormDialog>("dialog.version", "Cr\xC3\xA9" "er une version", text, std::move(fields),
                                               "Cr\xC3\xA9" "er V" + std::to_string(next));
    // Entree dans le commentaire cree la version, comme avant le mot de passe
    // (le dernier champ, facultatif : Livree seulement).
    dialog->setEnterField(2);
    app_.menus().ShowDialog(std::move(dialog),
                       [this, folder, unsaved](const menu::DialogResult& r) {
                           if (!r.accepted()) return;
                           const auto v = FormDialog::split(r.payload);
                           if (v.size() < 3) return;
                           if (unsaved > 0 && v.size() > 3 && v[3].rfind("oui", 0) == 0) {
                               if (auto st = app_.saveProject(); !st) {
                                   app_.menus().ShowDialog(std::make_unique<MessageDialog>("Enregistrement impossible", st.error().message(),
                                                                                      MessageDialog::Icon::Error),
                                                      [](const menu::DialogResult&) {});
                                   return;
                               }
                           }
                           ver::State state = ver::State::Draft;
                           if (v[1].rfind("Valid", 0) == 0) state = ver::State::Validated;
                           if (v[1].rfind("Livr", 0) == 0) state = ver::State::Delivered;
                           // Lot API 6 : l'etat du projet suit - Validee termine, Livree verrouille.
                           const auto pstate = app_.manifest().state;
                           const std::size_t pw = unsaved > 0 ? 4 : 3;
                           const bool finish = state == ver::State::Validated && (pstate == project::State::Dev || pstate == project::State::New);
                           const bool deliver = state == ver::State::Delivered && pstate != project::State::Lock;
                           if (finish || deliver) {
                               const std::string password = v.size() > pw ? v[pw] : std::string{};
                               if (deliver && password.empty()) {
                                   app_.menus().ShowDialog(std::make_unique<MessageDialog>("Version non cr\xC3\xA9\xC3\xA9" "e",
                                                               "Livr\xC3\xA9" "e verrouille le projet : il faut un mot de passe.", MessageDialog::Icon::Warning),
                                                           [](const menu::DialogResult&) {});
                                   return;
                               }
                               auto made = closeVersion(app_, finish ? project::State::Finish : project::State::Lock, v[0], v[2], password);
                               if (!made) {
                                   if (status_) status_->setTransientMessage("Version non cr\xC3\xA9\xC3\xA9" "e : " + (made.error().context.empty() ? made.error().message() : made.error().context),
                                                                             8.0, StatusBar::Severity::Warning);
                                   return;
                               }
                               versionWatch_.checkedAt = -100.0;
                               refreshVersions();
                               openVersions(*made);
                               if (status_)
                                   status_->setTransientMessage("V" + std::to_string(*made) + (finish ? " valid\xC3\xA9" "e : le projet est FINISH." : " livr\xC3\xA9" "e : le projet est LOCK."),
                                                                8.0, StatusBar::Severity::Success);
                               return;
                           }
                           auto store2 = ver::open(folder);
                           if (!store2) {
                               if (status_) status_->setTransientMessage("Versions : " + store2.error().context, 8.0, StatusBar::Severity::Warning);
                               return;
                           }
                           auto made = ver::create(*store2, v[0], state, v[2], ver::defaultAuthor());
                           if (!made) {
                               if (status_) status_->setTransientMessage("Version non cr\xC3\xA9\xC3\xA9" "e : " + made.error().context, 8.0,
                                                                         StatusBar::Severity::Warning);
                               return;
                           }
                           refreshVersions();
                           openVersions(made->number);
                           if (status_)
                               status_->setTransientMessage(made->label() + " cr\xC3\xA9\xC3\xA9" "e : " + std::to_string(made->fileCount) + " fichiers, "
                                                                + ver::sizeText(made->newBytes) + " nouveaux ("
                                                                + (made->since.empty() ? std::string("la premi\xC3\xA8re") : made->since) + ")",
                                                            8.0, StatusBar::Severity::Success);
                       });
}

// ------------------------------------------------------ lot API 6 : la version en cours --
void MainAnalysisScreen::refreshVersionChip() {
    if (!topBar_) return;
    const std::string folder = app_.projectFolder();
    if (folder.empty() || !app_.project()) {
        topBar_->setVersion({});
        return;
    }
    auto& w = versionWatch_;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (w.folder != folder || now - w.checkedAt > 1.0) {
        w.checkedAt = now;
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto index = fs::path(folder) / "versions" / "index.txt";
        std::string stamp;
        if (fs::exists(index, ec)) {
            const auto t = fs::last_write_time(index, ec);
            stamp = std::to_string(static_cast<long long>(t.time_since_epoch().count())) + ":" + std::to_string(fs::file_size(index, ec));
        }
        if (w.folder != folder || stamp != w.stamp) {
            // 1.11.2 : un autre dossier, une autre version : le calcul en cours ne vaut plus.
            if (w.job) w.retired.push_back(std::move(w.job));
            w.folder = folder;
            w.stamp = stamp;
            auto store = ver::open(folder);
            w.store = store ? std::move(*store) : ver::Store{};
            w.comparedAt = -100.0;
            w.changes = 0;
            w.stale = true;
        }
    }
    // Combien d'elements ont change depuis la derniere version (DEV).
    // 1.11.2 (BLK, decision 203) : le blocage de 19 h 07 - cette comparaison relit et
    // analyse toute l'IHM deux fois (1,1 a 1,4 s sur Tunnel_Paris_CDG, plus de 8 s chez
    // le client), sur le fil de l'ecran, deux fois a l'ouverture puis toutes les 30 s.
    // Elle tourne maintenant dans un fil a part, relancee seulement quand le projet est
    // enregistre ou que versions/index.txt change : elle porte sur le disque, modifier
    // sans enregistrer n'y change rien. Plus de cycle de 30 s.
    for (auto it = w.retired.begin(); it != w.retired.end();)
        it = (*it)->done.load() ? w.retired.erase(it) : std::next(it);
    if (w.job && w.job->done.load()) {
        if (w.job->ok) w.changes = w.job->changes;
        w.job.reset();
        w.comparedAt = now;
    }
    const std::int64_t saved = app_.savedAtMs();
    // 1.11.1 (T1, R111-14 / R111-24) : pas dans le bac a sable d'un tutoriel (voir refreshVersions).
    if (app_.manifest().state == project::State::Dev && w.store.last() && !w.job && (w.stale || saved != w.comparedSave)
        && !tutorials::isSandboxFolder(folder)) {
        w.stale = false;
        w.comparedSave = saved;
        ++w.launched;
        auto job = std::make_unique<MainAnalysisScreen::VersionJob>();
        auto* j = job.get();
        j->thread = std::thread([j, store = w.store, number = w.store.last()->number] {
            if (auto c = ver::compare(store, number, 0)) {
                j->changes = c->elements.size();
                j->ok = true;
            }
            j->done.store(true);
        });
        w.job = std::move(job);
    }
    const auto st = ver::standing(app_.manifest().state, &w.store, w.changes, app_.pendingChanges());
    TopBar::VersionChip chip;
    chip.title = st.title;
    // ---- Lot API 8 : bandeau haut - "V48 . 3 modifications" (en DEV, depuis une version) ----
    if (app_.manifest().state == project::State::Dev && st.last > 0) {
        const std::size_t n = w.changes;
        chip.title = "V" + std::to_string(st.current) + " \xC2\xB7 "
                   + (n == 0 ? std::string("aucune modification") : std::to_string(n) + (n > 1 ? " modifications" : " modification"));
    }
    // ---- fin Lot API 8 : bandeau haut ----
    chip.subtitle = st.subtitle;
    chip.tone = st.tone;
    chip.tip = st.tip;
    for (const auto& h : st.heading) {
        TopBar::Entry e;
        e.label = h;
        e.heading = true;
        chip.menu.push_back(std::move(e));
    }
    const auto entry = [&](std::string label, std::string right, ui::Icon icon, std::string action, bool enabled = true, std::string why = {}) {
        TopBar::Entry e;
        e.label = std::move(label);
        e.shortcut = std::move(right);
        e.icon = icon;
        e.action = std::move(action);
        e.enabled = enabled;
        e.disabledReason = std::move(why);
        chip.menu.push_back(std::move(e));
    };
    const auto sep = [&] { TopBar::Entry e; e.separator = true; chip.menu.push_back(std::move(e)); };
    const std::string c = std::to_string(st.current), next = std::to_string(st.last + 1), last = std::to_string(st.last);
    sep();
    switch (app_.manifest().state) {
        case project::State::Dev:
            entry("Terminer la V" + c + "\xE2\x80\xA6", "\xE2\x86\x92 FINISH", ui::Icon::Ok, "version.finish");
            entry("Livrer la V" + c + " et verrouiller\xE2\x80\xA6", "\xE2\x86\x92 LOCK", ui::Icon::Lock, "version.deliver");
            // Lot API 8 : bandeau haut - l'instantane nomme s'appelle "Creer un essai..." (la maquette).
            entry("Cr\xC3\xA9" "er un essai\xE2\x80\xA6 (version interm\xC3\xA9" "diaire)", "reste en DEV", ui::Icon::Save, "version.draft");
            sep();
            entry(st.last ? "Ce qui a chang\xC3\xA9 depuis la V" + last : std::string("Ce qui a chang\xC3\xA9"), st.last ? std::to_string(w.changes) : std::string{},
                  ui::Icon::Search, "version.compare", st.last > 0, "aucune version encore");
            break;
        case project::State::Finish:
            entry("Livrer la V" + last + " et verrouiller\xE2\x80\xA6", "\xE2\x86\x92 LOCK", ui::Icon::Lock, "version.deliver", true);
            entry("Commencer la V" + next + " maintenant", "\xE2\x86\x92 DEV", ui::Icon::Document, "version.reopen");
            break;
        case project::State::Lock:
            entry("D\xC3\xA9verrouiller\xE2\x80\xA6", "\xE2\x86\x92 DEV, V" + next, ui::Icon::Lock, "project.unlock");
            break;
        case project::State::New:
            entry("Terminer la V" + c + "\xE2\x80\xA6", "", ui::Icon::Ok, "version.finish", false, "rien \xC3\xA0 terminer");
            entry("Commencer la V" + c + " maintenant", "\xE2\x86\x92 DEV", ui::Icon::Document, "version.reopen");
            break;
    }
    sep();
    entry("Les versions du projet", "", ui::Icon::History, "version.list");
    topBar_->setVersion(std::move(chip));
}

void MainAnalysisScreen::onVersionAction(const std::string& id) {
    if (id == "version.finish") { askFinishVersion(); return; }
    if (id == "version.deliver") { askDeliverVersion(); return; }
    if (id == "version.draft") { askCreateVersion(); return; }
    if (id == "version.list") { openVersions(0); return; }
    if (id == "version.compare") {
        auto store = ver::open(app_.projectFolder());
        if (store && store->last()) openVersionCompare(store->last()->number, 0, {});
        return;
    }
    if (id == "version.reopen") {
        const auto from = app_.manifest().state;
        if (from != project::State::Finish && from != project::State::New) return;
        if (auto st = app_.setProjectState(project::State::Dev); !st) {
            if (status_) status_->setTransientMessage("DEV : " + st.error().message(), 8.0, StatusBar::Severity::Warning);
            return;
        }
        versionWatch_.checkedAt = -100.0;
        refreshVersions();
        if (status_) status_->setTransientMessage("DEV : " + ver::standing(project::State::Dev, &versionWatch_.store, 0, 0).title + ".", 8.0,
                                                  StatusBar::Severity::Success);
    }
}

void MainAnalysisScreen::askFinishVersion() {
    const std::string folder = app_.projectFolder();
    if (folder.empty() || app_.manifest().state != project::State::Dev) return;
    auto store = ver::open(folder);
    const auto st = ver::standing(project::State::Dev, store ? &*store : nullptr, 0, app_.pendingChanges());
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", ver::defaultName(project::State::Finish), "Mise en service, Essais usine\xE2\x80\xA6", false, {}});
    fields.push_back({"Commentaire", "", "ce qui la distingue (essais faits, seuils ajust\xC3\xA9s\xE2\x80\xA6)", false, {}});
    const std::string c = std::to_string(st.current);
    const std::string text = "La V" + c + " devient une version VALID\xC3\x89" "E et le projet passe FINISH : il est exactement elle. "
                             "Le poste d'exploitation l'ouvrira ; la premi\xC3\xA8re modification commencera la V" + std::to_string(st.current + 1) + "."
                           + (app_.pendingChanges() > 0 ? "\nLe projet est d'abord enregistr\xC3\xA9 (" + std::to_string(app_.pendingChanges()) + " modification(s))." : std::string{});
    app_.menus().ShowDialog(std::make_unique<FormDialog>("dialog.finishVersion", "Terminer la V" + c, text, std::move(fields), "Terminer la V" + c),
                            [this](const menu::DialogResult& r) {
                                if (!r.accepted()) return;
                                const auto v = FormDialog::split(r.payload);
                                auto made = closeVersion(app_, project::State::Finish, v.empty() ? std::string{} : v[0], v.size() > 1 ? v[1] : std::string{});
                                if (!made) {
                                    app_.menus().ShowDialog(std::make_unique<MessageDialog>("Version non termin\xC3\xA9" "e", made.error().context.empty() ? made.error().message() : made.error().context,
                                                                                            MessageDialog::Icon::Warning),
                                                            [](const menu::DialogResult&) {});
                                    return;
                                }
                                versionWatch_.checkedAt = -100.0;
                                refreshVersions();
                                if (status_)
                                    status_->setTransientMessage("V" + std::to_string(*made) + " valid\xC3\xA9" "e : le projet est FINISH. La premi\xC3\xA8re modification commencera la V"
                                                                     + std::to_string(*made + 1) + ".", 10.0, StatusBar::Severity::Success);
                            });
}

void MainAnalysisScreen::askDeliverVersion() {
    const std::string folder = app_.projectFolder();
    const auto from = app_.manifest().state;
    if (folder.empty() || (from != project::State::Dev && from != project::State::Finish)) return;
    auto store = ver::open(folder);
    const bool promote = from == project::State::Finish && store && store->last() && !ver::changedSinceLast(*store);
    const int number = promote ? store->last()->number : (store ? store->nextNumber() : 1);
    const std::string n = std::to_string(number);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", promote && !store->last()->name.empty() ? store->last()->name : ver::defaultName(project::State::Lock), "Livr\xC3\xA9" "e au client\xE2\x80\xA6", false, {}});
    fields.push_back({"Commentaire", promote ? store->last()->comment : std::string{}, "", false, {}});
    fields.push_back({"Mot de passe", "", "pour d\xC3\xA9verrouiller (la cl\xC3\xA9 du PC ma\xC3\xAEtre marche aussi)", true, {}});
    fields.push_back({"Le mot de passe, encore", "", "", true, {}});
    const std::string text = (promote ? "La V" + n + " (valid\xC3\xA9" "e, rien n'a chang\xC3\xA9 depuis) devient LIVR\xC3\x89" "E"
                                      : "La V" + n + " est cr\xC3\xA9\xC3\xA9" "e, LIVR\xC3\x89" "E")
                           + ", et le projet est verrouill\xC3\xA9 (LOCK) : personne ne le modifie sans le mot de passe ou la cl\xC3\xA9 du PC ma\xC3\xAEtre. "
                             "D\xC3\xA9verrouiller commencera la V" + std::to_string(number + 1) + ".";
    app_.menus().ShowDialog(std::make_unique<FormDialog>("dialog.deliverVersion", "Livrer la V" + n + " et verrouiller", text, std::move(fields),
                                                         "Livrer et verrouiller"),
                            [this](const menu::DialogResult& r) {
                                if (!r.accepted()) return;
                                const auto v = FormDialog::split(r.payload);
                                const std::string password = v.size() > 2 ? v[2] : std::string{};
                                const auto warn = [this](const std::string& why) {
                                    app_.menus().ShowDialog(std::make_unique<MessageDialog>("Version non livr\xC3\xA9" "e", why, MessageDialog::Icon::Warning),
                                                            [](const menu::DialogResult&) {});
                                };
                                if (password.empty()) { warn("Verrouiller sans mot de passe verrouillerait le projet pour tout le monde, toi compris."); return; }
                                if (v.size() > 3 && v[3] != password) { warn("Les deux mots de passe diff\xC3\xA8rent."); return; }
                                auto made = closeVersion(app_, project::State::Lock, v.empty() ? std::string{} : v[0], v.size() > 1 ? v[1] : std::string{}, password);
                                if (!made) { warn(made.error().context.empty() ? made.error().message() : made.error().context); return; }
                                versionWatch_.checkedAt = -100.0;
                                refreshVersions();
                                if (status_)
                                    status_->setTransientMessage("V" + std::to_string(*made) + " livr\xC3\xA9" "e et verrouill\xC3\xA9" "e : le projet est LOCK.", 10.0,
                                                                 StatusBar::Severity::Success);
                            });
}

void MainAnalysisScreen::askRestoreVersion(int number) {
    const std::string folder = app_.projectFolder();
    auto store = ver::open(folder);
    if (!store || !store->find(number)) return;
    const auto* v = store->find(number);
    const std::size_t unsaved = app_.pendingChanges();
    HmiAskDialog::Spec spec;
    spec.id = "dialog.versionRestore";
    spec.title = "Restaurer " + v->label();
    spec.text = "Le projet revient \xC3\xA0 " + v->label() + " (" + whenText(v->date) + ") : l'API, l'IHM et ses ressources, les donn\xC3\xA9" "es.\n"
                "Rien ne se perd : une version \xC2\xAB Avant restauration de V" + std::to_string(number)
              + " \xC2\xBB garde d'abord le projet tel qu'il est"
              + (unsaved > 1   ? " (les " + std::to_string(unsaved) + " modifications non enregistr\xC3\xA9" "es sont enregistr\xC3\xA9" "es avant)"
                 : unsaved == 1 ? std::string(" (la modification non enregistr\xC3\xA9" "e l'est avant)")
                                : std::string{})
              + ".\nLe projet est ensuite relu ; l'historique Ctrl+Z repart de l\xC3\xA0.";
    spec.confirm = "Revenir \xC3\xA0 V" + std::to_string(number);
    spec.width = 680.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, folder, number, unsaved](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        if (unsaved > 0) {
            if (auto st = app_.saveProject(); !st) {
                app_.menus().ShowDialog(std::make_unique<MessageDialog>("Enregistrement impossible", st.error().message(), MessageDialog::Icon::Error),
                                   [](const menu::DialogResult&) {});
                return;
            }
        }
        auto store2 = ver::open(folder);
        if (!store2) return;
        auto before = ver::restore(*store2, number, ver::defaultAuthor());
        if (!before) {
            app_.menus().ShowDialog(std::make_unique<MessageDialog>("Restauration impossible", before.error().context, MessageDialog::Icon::Error),
                               [](const menu::DialogResult&) {});
            return;
        }
        const std::string kept = before->has_value() ? " ; " + (*before)->label() + " garde l'\xC3\xA9tat d'avant" : std::string{};
        // Le projet relu : l'ecran se refait (les onglets se ferment) ; le volet
        // Versions revient, et dit ce qui s'est passe.
        app_.openProjectFolder(folder);
        const std::string said = "Revenu \xC3\xA0 V" + std::to_string(number) + kept + ".";
        app_.events().publish(StatusNotice{said, 10.0});
        openVersions(number);
        if (auto* pane = dynamic_cast<VersionsPane*>(hmiTab("versions"))) pane->say(said);
    });
}

void MainAnalysisScreen::askDeleteVersion(int number) {
    const std::string folder = app_.projectFolder();
    auto store = ver::open(folder);
    if (!store || !store->find(number)) return;
    const auto* v = store->find(number);
    HmiAskDialog::Spec spec;
    spec.id = "dialog.versionDelete";
    spec.title = "Supprimer " + v->label();
    spec.text = "La version " + v->label() + " (" + whenText(v->date) + ") est retir\xC3\xA9" "e de la liste. Ce qu'elle partage avec les autres "
                "versions reste ; ce qui n'appartenait qu'\xC3\xA0 elle est effac\xC3\xA9 de versions/. Cela ne s'annule pas.";
    spec.confirm = "Supprimer V" + std::to_string(number);
    spec.danger = true;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, folder, number](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        auto store2 = ver::open(folder);
        if (!store2) return;
        if (auto st = ver::remove(*store2, number); !st) {
            if (status_) status_->setTransientMessage("Supprimer V" + std::to_string(number) + " : " + st.error().context, 8.0, StatusBar::Severity::Warning);
            return;
        }
        refreshVersions();
        if (status_) status_->setTransientMessage("V" + std::to_string(number) + " supprim\xC3\xA9" "e", 6.0, StatusBar::Severity::Success);
    });
}

// OU ECRIRE L'ARCHIVE se demande : exports/ du projet par defaut (Exporter, ou
// Entree dans le champ), ailleurs avec le bouton ... (l'explorateur).
void MainAnalysisScreen::exportVersion(int number) {
    const std::string folder = app_.projectFolder();
    auto store = ver::open(folder);
    if (!store || !store->find(number)) return;
    const auto* v = store->find(number);
    const std::filesystem::path dir = std::filesystem::path(folder) / "exports";
    const std::string stamp = v->date.size() >= 10 ? v->date.substr(0, 10) : std::string("version");
    const std::string name = std::filesystem::path(folder).filename().string() + "_V" + std::to_string(number) + "_" + stamp + ".zip";
    const std::string proposed = (dir / name).string();
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Archive", proposed, "", false, {}});
    const std::string title = "Exporter " + v->label();
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.versionExport", title,
                                                            "La version en archive (.zip), pour l'envoyer. Par d\xC3\xA9" "faut dans exports/ du projet ; "
                                                            "le bouton \xE2\x80\xA6 choisit un autre endroit.",
                                                            std::move(fields), "Exporter"),
                               0, ui::saveFile("Archives (.zip)|*.zip", proposed, title)),
        [this, folder, number, dir](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto values = FormDialog::split(r.payload);
            std::string target = values.empty() ? std::string{} : values[0];
            while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back()))) target.pop_back();
            while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front()))) target.erase(target.begin());
            if (target.size() >= 2 && target.front() == '"' && target.back() == '"') target = target.substr(1, target.size() - 2);
            if (target.empty()) return;
            std::filesystem::path path(target);
            if (path.is_relative()) path = dir / path;          // un nom seul : dans exports/
            if (!path.has_extension()) path += ".zip";
            auto store2 = ver::open(folder);
            const auto* v2 = store2 ? store2->find(number) : nullptr;
            if (!v2) return;
            std::error_code ec;
            if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
            if (auto st = ver::exportZip(*store2, number, path.string()); !st) {
                if (status_) status_->setTransientMessage("Exporter V" + std::to_string(number) + " : " + st.error().context, 8.0, StatusBar::Severity::Warning);
                return;
            }
            const auto size = std::filesystem::file_size(path, ec);
            if (status_)
                status_->setTransientMessage(v2->label() + " export\xC3\xA9" "e : " + path.string() + " (" + ver::sizeText(ec ? 0 : size) + ")", 8.0,
                                             StatusBar::Severity::Success);
        });
}

void MainAnalysisScreen::askExtractVersion(int number) {
    const std::string folder = app_.projectFolder();
    auto store = ver::open(folder);
    if (!store || !store->find(number)) return;
    const std::filesystem::path project(folder);
    const std::string suggested = (project.parent_path() / (project.filename().string() + "_V" + std::to_string(number))).string();
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Dossier", suggested, "un dossier neuf (il est cr\xC3\xA9\xC3\xA9)", false, {}});
    // Le bouton ... : OU creer le dossier (l'explorateur ; son nom reste).
    app_.menus().ShowDialog(FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.versionExtract", "Extraire V" + std::to_string(number) + " dans un dossier",
                                                    "Un projet complet, \xC3\xA0 ouvrir \xC3\xA0 c\xC3\xB4t\xC3\xA9 de celui-ci (Projet \xE2\x80\xBA Ouvrir) : "
                                                    "l'API, l'IHM et ses ressources, les donn\xC3\xA9" "es de la version.",
                                                    std::move(fields), "Extraire"),
                                                    0, ui::newFolder(suggested, "Extraire V" + std::to_string(number) + " : le dossier")),
                       [this, folder, number](const menu::DialogResult& r) {
                           if (!r.accepted()) return;
                           const auto v = FormDialog::split(r.payload);
                           if (v.empty() || v[0].empty()) return;
                           auto store2 = ver::open(folder);
                           if (!store2) return;
                           if (auto st = ver::extract(*store2, number, v[0]); !st) {
                               if (status_) status_->setTransientMessage("Extraire : " + st.error().context, 8.0, StatusBar::Severity::Warning);
                               return;
                           }
                           if (status_) status_->setTransientMessage("V" + std::to_string(number) + " extraite dans " + v[0], 8.0, StatusBar::Severity::Success);
                       });
}

void MainAnalysisScreen::openVersionCompare(int a, int b, const std::string& elementKey) {
    if (!centre_) return;
    const std::string folder = app_.projectFolder();
    auto* pane = dynamic_cast<VersionComparePane*>(hmiTab("comparer"));
    if (!pane) {
        auto made = std::make_unique<VersionComparePane>("versions.compare", folder, a, b);
        pane = made.get();
        VersionComparePane::Hosts hosts;
        hosts.restoreElement = [this](const ver::Element& e, int from, std::string* why) { return restoreVersionElement(e, from, why); };
        hosts.retitle = [this, pane](const std::string& title) {
            if (!centre_) return;
            const int i = centre_->indexOf(pane);
            if (i >= 0) centre_->setTabTitle(static_cast<std::size_t>(i), title);
        };
        pane->setHosts(std::move(hosts));
        const auto tab = centre_->addTab(TabControl::Tab{pane->title(), Icon::History, true, false}, std::move(made));
        hmiTabs_["comparer"] = pane;
        centre_->setCurrentIndex(tab);
    } else {
        pane->compare(a, b);
        const int i = centre_->indexOf(pane);
        if (i >= 0) {
            centre_->setTabTitle(static_cast<std::size_t>(i), pane->title());
            centre_->setCurrentIndex(static_cast<std::size_t>(i));
        }
    }
    pane->showElement(elementKey);
}

bool MainAnalysisScreen::restoreVersionElement(const ver::Element& e, int from, std::string* why) {
    const auto fail = [why](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    const std::string folder = app_.projectFolder();
    auto store = ver::open(folder);
    if (!store || !store->find(from)) return fail("la version V" + std::to_string(from) + " n'existe plus");
    // L'API : le texte d'une section (de tache, ou "unite/section" d'une unite ou d'un DFB).
    if (e.key.rfind("section:", 0) == 0) {
        auto project = app_.document();
        if (!project) return fail("pas de projet");
        if (e.fileA.empty()) return fail("la section n'existait pas dans V" + std::to_string(from));
        auto body = ver::contentOf(*store, from, e.fileA);
        if (!body) return fail(body.error().context);
        const std::string rest = e.key.substr(8);
        const auto slash = rest.find('/');
        const std::string pouName = slash == std::string::npos ? std::string{} : rest.substr(0, slash);
        const std::string name = slash == std::string::npos ? rest : rest.substr(slash + 1);
        domain::Index found = domain::kNoIndex;
        if (!pouName.empty()) {
            for (const auto& pou : project->pous)
                if (std::string(project->strings.text(pou.name)) == pouName)
                    for (auto si : pou.sections)
                        if (si < project->sections.size() && std::string(project->strings.text(project->sections[si].name)) == name) found = si;
        } else {
            for (std::size_t i = 0; i < project->sections.size() && found == domain::kNoIndex; ++i)
                if (std::string(project->strings.text(project->sections[i].name)) == name) found = static_cast<domain::Index>(i);
        }
        if (found == domain::kNoIndex) return fail("la section " + name + " n'est plus dans le projet");
        app_.apply(std::make_unique<project::SetSectionBodyCommand>(project, found, *body), /*refreshViews=*/false);
        refreshOpenDocuments();
        return true;
    }
    // L'IHM : une commande de projet (Ctrl+Z l'annule).
    auto doc = app_.hmi();
    if (!doc) return fail("pas d'IHM");
    auto old = ver::hmiOf(*store, from);
    if (!old) return fail("l'IHM de V" + std::to_string(from) + " est illisible : " + old.error().context);
    const hmi::Project& A = *old;
    const std::string key = e.key;
    const auto idAfter = [&key]() -> hmi::Id {
        const auto colon = key.find(':');
        try { return colon == std::string::npos ? hmi::kNoId : static_cast<hmi::Id>(std::stoul(key.substr(colon + 1))); } catch (...) { return hmi::kNoId; }
    };
    const auto nameAfter = [&key]() { const auto colon = key.find(':'); return colon == std::string::npos ? std::string{} : key.substr(colon + 1); };
    bool known = true;
    auto cmd = hmi::changeProject(doc, "Restaurer " + e.name + " depuis V" + std::to_string(from), [&](hmi::Project& p) {
        if (key.rfind("vue:", 0) == 0) restoreById(p.views, A.views, idAfter());
        else if (key.rfind("script:", 0) == 0) restoreById(p.programs.scripts, A.programs.scripts, idAfter());
        else if (key.rfind("fonction:", 0) == 0) restoreById(p.programs.functions, A.programs.functions, idAfter());
        else if (key.rfind("alarme:", 0) == 0) restoreByName(p.alarms, A.alarms, nameAfter(), [](const hmi::AlarmDef& x) { return x.name; });
        else if (key.rfind("recette:", 0) == 0) restoreByName(p.recipes, A.recipes, nameAfter(), [](const hmi::Recipe& x) { return x.name; });
        else if (key.rfind("style:", 0) == 0) restoreByName(p.styles, A.styles, nameAfter(), [](const hmi::Style& x) { return x.name; });
        else if (key.rfind("essai:", 0) == 0) restoreByName(p.scenarios, A.scenarios, nameAfter(), [](const hmi::TestScenario& x) { return x.name; });
        else if (key.rfind("rapport:", 0) == 0) restoreByName(p.reports, A.reports, nameAfter(), [](const hmi::Report& x) { return x.name; });
        else if (key.rfind("equipement:", 0) == 0) restoreByName(p.equipments, A.equipments, nameAfter(), [](const hmi::Equipment& x) { return x.name; });
        else if (key.rfind("externe:", 0) == 0) restoreByName(p.assets.files, A.assets.files, nameAfter(), [](const hmi::ExternalFile& x) { return x.name; });
        else if (key.rfind("ressource:", 0) == 0) restoreByName(p.assets.resources, A.assets.resources, nameAfter(), [](const hmi::Resource& x) { return x.name; });
        else if (key == "variables") p.programs.variables = A.programs.variables;
        else if (key == "types") p.programs.types = A.programs.types;
        else if (key == "config") { const auto created = p.config.created; p.config = A.config; p.config.created = created; }
        else if (key == "alarmes-reglages") p.alarmSettings = A.alarmSettings;
        else if (key == "securite") p.security = A.security;
        else if (key == "historiques") p.history = A.history;
        else if (key == "langues") p.languages = A.languages;
        else if (key == "unites") p.displays = A.displays;
        else if (key == "communication") p.comm = A.comm;
        else if (key == "poste") p.station = A.station;
        else if (key == "notifications") p.notify = A.notify;
        else if (key == "web") p.web = A.web;
        else if (key == "reseau-simule") p.simPorts = A.simPorts;
        else known = false;
        // Un identifiant repris d'une version ne doit pas croiser le compteur.
        p.nextId = std::max(p.nextId, A.nextId);
    });
    if (!known) return fail("cet \xC3\xA9l\xC3\xA9ment ne se restaure pas seul");
    if (!cmd) return fail("rien \xC3\xA0 changer : l'\xC3\xA9l\xC3\xA9ment est d\xC3\xA9j\xC3\xA0 comme dans V" + std::to_string(from));
    app_.apply(std::move(cmd), /*refreshViews=*/false);
    return true;
}

} // namespace app
