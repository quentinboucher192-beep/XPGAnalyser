// =============================================================================
//  app/screens/LisibleWorkspace.cpp - 1.8.0 : les icones au choix, l'export
//  lisible du programme, le comparateur de sections
// -----------------------------------------------------------------------------
//  LES ICONES. Clic droit > Definir l'icone... sur une section, une unite, un
//  bloc DFB (et ses sections), un type DDT, un script ou une fonction de l'IHM :
//  le petit menu (CodeIconPicker) montre les 18 icones et ce qu'elles disent ;
//  une commande (un Ctrl+Z) les pose sur tous les elements choisis. L'arbre,
//  les onglets, les volets, Aller a..., l'export les montrent.
//
//  L'EXPORT LISIBLE. Projet > Exporter le programme lisible... (Ctrl+Maj+E), le
//  clic droit sur API, une unite, une section : la boite (ProgramExportDialog)
//  choisit la portee, les formats (Excel, PDF, texte), le contenu, le dossier ;
//  puis l'ecriture part a cote (une COPIE du projet, un fil), la barre du haut
//  la suit, la cloche previent et ouvre le dossier.
//
//  LE COMPARATEUR. Des sections choisies (l'arbre, API > Unites de programme) :
//  un onglet (SectionComparePane), qui suit le projet.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../BackgroundTasks.hpp"
#include "../CodeIconPicker.hpp"
#include "../Dossiers.hpp"
#include "../ImportJob.hpp"
#include "../ProgramExportDialog.hpp"
#include "../SectionComparePane.hpp"
#include "../../core/Version.hpp"
#include "../../export/ProgramBook.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../project/CodeIconKeys.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <thread>

namespace app {

namespace book = exporter::book;
namespace ci = core::codeicons;
namespace pci = project::codeicons;
using NK = ProjectTreeModel::NodeKind;
using ui::StatusBar;

namespace {

std::filesystem::path utf8Path(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string leafOf(const std::string& path) {
    const auto p = utf8Path(path).filename().u8string();
    return std::string(p.begin(), p.end());
}

std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

std::string projectName(App& app) {
    if (!app.projectFolder().empty()) return app.manifest().name.empty() ? leafOf(app.projectFolder()) : app.manifest().name;
    if (const auto p = app.project(); p && !p->header.projectName.empty()) return p->header.projectName;
    return "Projet";
}

// "12 345" (l'espace fine insecable des milliers, comme l'export).
std::string thousands(std::size_t n) {
    std::string s = std::to_string(n), out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += s[i];
    }
    return out;
}
std::string plural(std::size_t n, const std::string& one, const std::string& many) { return thousands(n) + " " + (n > 1 ? many : one); }

std::tm localNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}
std::string isoToday() {
    const auto tm = localNow();
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}
std::string frenchNow() {
    const auto tm = localNow();
    char buf[96];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d \xC3\xA0 %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return buf;
}
std::string secondsText(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, s < 10.0 ? "%.1f s" : "%.0f s", s);
    std::string out(buf);
    for (auto& c : out)
        if (c == '.') c = ',';
    return out;
}

// Les formats : "xlsx,pdf,txt" (les scripts), dans l'ordre de la boite.
std::array<bool, 3> formatsOf(const std::string& list) {
    std::string l;
    for (const char c : list) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l.empty() || l == "tout" || l == "tous") return {true, true, true};
    return {l.find("xls") != std::string::npos || l.find("excel") != std::string::npos,
            l.find("pdf") != std::string::npos,
            l.find("txt") != std::string::npos || l.find("texte") != std::string::npos};
}

bool writeBytes(const std::string& path, const void* data, std::size_t size, std::string& why) {
    std::ofstream f(utf8Path(path), std::ios::binary | std::ios::trunc);
    if (!f) {
        why = leafOf(path) + " ne s'\xC3\xA9" "crit pas : le fichier est peut-\xC3\xAAtre ouvert (Excel, un lecteur PDF) ; ferme-le et recommence.";
        return false;
    }
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    f.close();
    if (!f) {
        why = leafOf(path) + " : l'\xC3\xA9" "criture a \xC3\xA9" "chou\xC3\xA9 (disque plein ?)";
        return false;
    }
    return true;
}

// Ecrire les formats choisis, dans le dossier ; `progress` (0..1) apres chaque etape.
bool writeBook(const domain::Project& p, const book::Options& o, const std::array<bool, 3>& formats, const std::string& folder,
               const std::string& stem, std::vector<std::string>& written, std::string& why,
               const std::function<void(float, const char*)>& progress) {
    std::error_code ec;
    std::filesystem::create_directories(utf8Path(folder), ec);
    if (ec && !std::filesystem::is_directory(utf8Path(folder))) {
        why = "le dossier " + folder + " ne se cr\xC3\xA9" "e pas : " + ec.message();
        return false;
    }
    if (progress) progress(0.05f, "Le programme dans l'ordre d'ex\xC3\xA9" "cution");
    const auto b = book::build(p, o);
    const int count = static_cast<int>(formats[0]) + static_cast<int>(formats[1]) + static_cast<int>(formats[2]);
    int doneCount = 0;
    const auto base = (utf8Path(folder) / utf8Path(stem)).u8string();
    const std::string stemPath(base.begin(), base.end());
    const auto step = [&](const char* what) {
        ++doneCount;
        if (progress) progress(0.1f + 0.9f * static_cast<float>(doneCount) / static_cast<float>(std::max(count, 1)), what);
    };
    if (formats[0]) {
        if (progress) progress(0.1f + 0.9f * static_cast<float>(doneCount) / static_cast<float>(std::max(count, 1)), "Excel");
        const auto bytes = book::toXlsx(b, o);
        const std::string path = stemPath + ".xlsx";
        if (!writeBytes(path, bytes.data(), bytes.size(), why)) return false;
        written.push_back(path);
        step("Excel");
    }
    if (formats[1]) {
        if (progress) progress(0.1f + 0.9f * static_cast<float>(doneCount) / static_cast<float>(std::max(count, 1)), "PDF");
        const auto bytes = book::toPdf(b, o);
        const std::string path = stemPath + ".pdf";
        if (!writeBytes(path, bytes.data(), bytes.size(), why)) return false;
        written.push_back(path);
        step("PDF");
    }
    if (formats[2]) {
        const auto txt = book::toText(b, o);
        const std::string path = stemPath + ".txt";
        if (!writeBytes(path, txt.data(), txt.size(), why)) return false;
        written.push_back(path);
        step("texte");
    }
    return true;
}

// Le nom d'une section pour les messages : "SFC_ManuB".
std::string sectionName(const domain::Project& p, domain::Index s) { return s < p.sections.size() ? text(p, p.sections[s].name) : std::string{}; }

// Deux noms qui se ressemblent (SFC_ManuA / SFC_ManuB) : la longueur du debut commun.
std::size_t commonPrefix(std::string_view a, std::string_view b) {
    std::size_t n = 0;
    while (n < a.size() && n < b.size() && std::tolower(static_cast<unsigned char>(a[n])) == std::tolower(static_cast<unsigned char>(b[n]))) ++n;
    return n;
}

} // namespace

// ============================================================ l'export en cours ===
struct MainAnalysisScreen::ExportJob {
    std::shared_ptr<const domain::Project> project;
    book::Options                          options;
    std::array<bool, 3>                    formats{};
    std::string                            folder, stem, what;
    bool                                   openFolder{true};
    int                                    task{0};
    float                                  shown{-1.f};        // le fil de l'interface seulement
    std::chrono::steady_clock::time_point  started{};
    std::atomic<float>                     fraction{0.f};
    std::atomic_bool                       done{false};
    std::mutex                             lock;
    std::vector<std::string>               written;            // sous verrou
    std::string                            error, step;        // sous verrou
    std::thread                            worker;
    ~ExportJob() {
        if (worker.joinable()) worker.join();
    }
};

// ===================================================================== icones ===
std::vector<std::string> MainAnalysisScreen::codeIconKeys(const std::vector<ui::NodeId>& nodes, std::string* what, std::string* from,
                                                          core::codeicons::Kind* kind) const {
    std::vector<std::string> keys;
    const auto p = app_.project();
    if (!p) return keys;
    const auto hmiDoc = app_.hmi();
    std::string firstWhat, firstName;
    ci::Kind firstKind = ci::Kind::Section;
    for (const auto n : nodes) {
        const auto k = ProjectTreeModel::kindOf(n);
        const auto i = ProjectTreeModel::indexOf(n);
        std::string key, name, word;
        ci::Kind kd = ci::Kind::Section;
        switch (k) {
            case NK::ExecStep:
            case NK::Section:
            case NK::DfbSection: {
                const auto s = k == NK::ExecStep ? (treeModel_ ? treeModel_->sectionOf(n) : domain::kNoIndex) : i;
                if (s >= p->sections.size()) break;
                key = pci::keyForSection(*p, s);
                name = text(*p, p->sections[s].name);
                kd = pci::kindOfSection(*p, s);
                word = kd == ci::Kind::DfbSection ? "la section de bloc " : "la section ";
                break;
            }
            case NK::ProgramUnit:
            case NK::DfbType:
                if (i >= p->pous.size()) break;
                key = pci::keyForPou(*p, i);
                name = text(*p, p->pous[i].name);
                kd = k == NK::ProgramUnit ? ci::Kind::Unit : ci::Kind::Dfb;
                word = k == NK::ProgramUnit ? "l'unit\xC3\xA9 " : "le bloc DFB ";
                break;
            case NK::DerivedType:
                if (i >= p->derivedTypes.size()) break;
                key = pci::keyForType(*p, i);
                name = text(*p, p->derivedTypes[i].name);
                kd = ci::Kind::Ddt;
                word = "le type ";
                break;
            case NK::HmiGeneralScript:
            case NK::HmiFunction: {
                if (!hmiDoc || !treeModel_) break;
                const auto id = static_cast<hmi::Id>(treeModel_->hmiIdOf(n));
                if (k == NK::HmiGeneralScript) {
                    for (const auto& sc : hmiDoc->project.programs.scripts)
                        if (sc.id == id) name = sc.name;
                    kd = ci::Kind::HmiScript;
                    word = "le script ";
                } else {
                    for (const auto& f : hmiDoc->project.programs.functions)
                        if (f.id == id) name = f.name;
                    kd = ci::Kind::HmiFunction;
                    word = "la fonction IHM ";
                }
                if (!name.empty()) key = ci::keyOf(kd, {}, name);
                break;
            }
            case NK::HmiViewScript: {
                if (!hmiDoc || !treeModel_) break;
                const auto* view = hmiDoc->project.view(static_cast<hmi::Id>(treeModel_->hmiViewOf(n)));
                const auto sub = ProjectTreeModel::subOf(n);
                if (!view || sub >= view->scripts.size()) break;
                name = view->scripts[sub].name;
                kd = ci::Kind::HmiScript;
                word = "le script de vue ";
                key = ci::keyOf(kd, view->name, name);
                break;
            }
            default: break;
        }
        if (key.empty() || std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
        keys.push_back(key);
        if (keys.size() == 1) {
            firstWhat = word + name;
            firstName = name;
            firstKind = kd;
        }
    }
    if (what) *what = keys.size() == 1 ? firstWhat : std::to_string(keys.size()) + " \xC3\xA9l\xC3\xA9ments";
    if (from) *from = firstName;
    if (kind) *kind = firstKind;
    return keys;
}

void MainAnalysisScreen::askCodeIcon(std::vector<std::string> keys, std::string what, std::string from, core::codeicons::Kind kind) {
    const auto p = app_.project();
    if (keys.empty() || !p) return;
    if (!app_.document()) {
        if (status_) status_->setTransientMessage("Ce projet ne se modifie pas (lecture seule) : pas d'ic\xC3\xB4ne.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    CodeIconPicker::Spec spec;
    spec.title = "Ic\xC3\xB4ne de " + what;
    // L'icone actuelle : celle de tous les elements choisis, si c'est la meme.
    std::string current;
    bool same = true;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const int icon = pci::iconOf(*p, keys[i]);
        const std::string k = icon >= 0 ? std::string(ci::info(static_cast<std::size_t>(icon)).key) : std::string{};
        if (i == 0) current = k;
        else if (k != current) same = false;
    }
    spec.current = same ? current : std::string{};
    spec.suggestion = std::string(ci::suggest(from, kind));
    app_.menus().ShowDialog(std::make_unique<CodeIconPicker>(std::move(spec)), [this, keys, what](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        std::string why;
        if (!setCodeIcon(keys, CodeIconPicker::parse(r.payload), &why) && status_ && !why.empty())
            status_->setTransientMessage("Ic\xC3\xB4ne de " + what + " : " + why, 8.0, StatusBar::Severity::Warning);
    });
}

bool MainAnalysisScreen::setCodeIcon(const std::vector<std::string>& keys, const std::string& icon, std::string* why) {
    auto doc = app_.document();
    if (!doc) {
        if (why) *why = "aucun projet modifiable";
        return false;
    }
    if (keys.empty()) {
        if (why) *why = "rien de choisi";
        return false;
    }
    if (!icon.empty() && ci::indexOf(icon) < 0) {
        if (why) *why = "ic\xC3\xB4ne inconnue : " + icon;
        return false;
    }
    std::vector<std::pair<std::string, std::string>> changes;
    changes.reserve(keys.size());
    for (const auto& k : keys) changes.emplace_back(k, icon);
    const std::string name = icon.empty() ? std::string{} : std::string(ci::info(static_cast<std::size_t>(ci::indexOf(icon))).name);
    const std::string label = icon.empty() ? std::string("Retirer l'ic\xC3\xB4ne") : "Ic\xC3\xB4ne \xC2\xAB " + name + " \xC2\xBB";
    const auto before = app_.commands().revision();
    app_.apply(std::make_unique<pci::SetCodeIconsCommand>(doc, std::move(changes), label), /*refreshViews=*/false);
    if (app_.commands().revision() == before) {
        if (why) *why = "refus\xC3\xA9 (le projet est-il verrouill\xC3\xA9 ?)";
        return false;
    }
    refreshCodeIcons();
    if (status_) {
        const std::string what = keys.size() == 1 ? std::string("1 \xC3\xA9l\xC3\xA9ment") : std::to_string(keys.size()) + " \xC3\xA9l\xC3\xA9ments";
        status_->setTransientMessage(icon.empty() ? "Ic\xC3\xB4ne retir\xC3\xA9" "e (" + what + ") ; Ctrl+Z la remet."
                                                  : "Ic\xC3\xB4ne \xC2\xAB " + name + " \xC2\xBB pos\xC3\xA9" "e (" + what + ") ; Ctrl+Z la retire.",
                                     6.0);
    }
    return true;
}

void MainAnalysisScreen::refreshCodeIcons() {
    if (explorer_) explorer_->invalidate();
    const auto p = app_.project();
    if (centre_ && p) {
        static constexpr std::string_view kDoc = "analysis.doc.";
        for (std::size_t i = 0; i < centre_->tabCount(); ++i) {
            const auto* page = centre_->page(i);
            if (!page) continue;
            const std::string& id = page->id();
            if (id.size() <= kDoc.size() || id.compare(0, kDoc.size(), kDoc) != 0) continue;
            const auto digits = id.substr(kDoc.size());
            if (digits.find_first_not_of("0123456789") != std::string::npos) continue;
            const auto s = static_cast<domain::Index>(std::stoul(digits));
            if (s >= p->sections.size()) continue;
            const int icon = pci::sectionIcon(*p, s);
            centre_->setTabIcon(i, icon >= 0 ? ui::codeIcon(icon) : ui::Icon::Section);
        }
    }
    refreshApiPanes();
}

// ============================================================== l'export lisible ===
void MainAnalysisScreen::askProgramExport(int scope, std::string unit, std::vector<domain::Index> sections) {
    const auto p = app_.project();
    if (!p) {
        if (status_) status_->setTransientMessage("Exporter le programme : ouvre d'abord un projet.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    if (exportJob_) {
        if (status_) status_->setTransientMessage("Un export est d\xC3\xA9j\xC3\xA0 en cours (barre du haut).", 6.0, StatusBar::Severity::Warning);
        return;
    }
    sections.erase(std::remove_if(sections.begin(), sections.end(), [&](domain::Index s) { return s >= p->sections.size(); }), sections.end());
    const std::string iso = isoToday();
    const std::string name = projectName(app_);
    struct Choice {
        book::Options::Scope       scope{book::Options::Scope::All};
        std::string                unit;
        std::vector<domain::Index> sections;
    };
    std::vector<Choice> choices;
    ProgramExportDialog::Spec spec;
    const auto describe = [&](const Choice& c, std::string label) {
        book::Options o;
        o.scope = c.scope;
        o.unit = c.unit;
        o.sections = c.sections;
        o.access = false;
        o.dfbAppendix = false;
        o.variablesAppendix = false;
        o.projectName = name;
        const auto b = book::build(*p, o);
        ProgramExportDialog::Scope s;
        s.label = std::move(label);
        s.detail = plural(b.sections.size(), "section", "sections") + ", " + plural(b.scopeLines, "ligne", "lignes");
        if (c.scope == book::Options::Scope::All) {
            s.detail += " \xC2\xB7 " + plural(b.programUnits, "unit\xC3\xA9", "unit\xC3\xA9s") + " \xC2\xB7 ";
            for (std::size_t t = 0; t < b.tasks.size(); ++t) s.detail += (t ? ", " : "") + b.tasks[t].name;
        }
        s.stem = book::fileStem(b, o, iso);
        spec.scopes.push_back(std::move(s));
        choices.push_back(c);
    };
    describe(Choice{}, "Tout le programme");
    if (!unit.empty()) describe(Choice{book::Options::Scope::Unit, unit, {}}, "L'unit\xC3\xA9 de programme " + unit);
    if (!sections.empty())
        describe(Choice{book::Options::Scope::Sections, {}, sections},
                 sections.size() == 1 ? "La section " + sectionName(*p, sections.front())
                                      : "Les " + std::to_string(sections.size()) + " sections choisies");
    spec.scope = 0;
    if (scope == 1 && !unit.empty()) spec.scope = 1;
    if (scope == 2 && !sections.empty()) spec.scope = static_cast<int>(choices.size()) - 1;
    {
        // Les annexes : combien de blocs, de lignes, de variables.
        std::size_t dfbs = 0, dfbLines = 0, globals = 0;
        for (const auto& pou : p->pous)
            if (pou.kind == domain::PouKind::FunctionBlockType && pou.userDefined) {
                ++dfbs;
                for (const auto s : pou.sections)
                    if (s < p->sections.size()) dfbLines += p->sections[s].lineCount;
            }
        for (const auto& v : p->variables) globals += v.scope == domain::VariableScope::Global ? 1u : 0u;
        spec.dfbDetail = plural(dfbs, "bloc", "blocs") + ", " + plural(dfbLines, "ligne", "lignes");
        spec.variablesDetail = plural(globals, "variable globale", "variables globales");
    }
    const auto join = [](const std::string& base, const char* leaf) {
        const auto u = (utf8Path(base) / leaf).u8string();
        return std::string(u.begin(), u.end());
    };
    spec.folder = !lastExportFolder_.empty()        ? lastExportFolder_
                : !app_.projectFolder().empty() ? join(app_.projectFolder(), "exports")
                                                : join(dossiers::actif(dossiers::Cle::Donnees), "exports");
    app_.menus().ShowDialog(std::make_unique<ProgramExportDialog>(std::move(spec)), [this, choices, name](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = ProgramExportDialog::parse(r.payload);
        if (a.scope < 0 || static_cast<std::size_t>(a.scope) >= choices.size()) return;
        const auto p2 = app_.project();
        if (!p2) return;
        if (exportJob_) return;
        const auto& c = choices[static_cast<std::size_t>(a.scope)];
        auto job = std::make_shared<ExportJob>();
        job->project = std::make_shared<const domain::Project>(*p2);   // le fil lit la copie
        job->options.scope = c.scope;
        job->options.unit = c.unit;
        job->options.sections = c.sections;
        job->options.guide = a.content[0];
        job->options.access = a.content[1];
        job->options.lineNumbers = a.content[2];
        job->options.roles = a.content[3];
        job->options.unitParams = a.content[4];
        job->options.dfbAppendix = a.content[5];
        job->options.variablesAppendix = a.content[6];
        job->options.exportedAt = frenchNow();
        job->options.appVersion = XPG_ANALYZER_VERSION;
        job->options.projectName = name;
        job->formats = a.formats;
        job->folder = a.folder;
        job->openFolder = a.openFolder;
        {
            book::Options light = job->options;
            light.access = light.dfbAppendix = light.variablesAppendix = false;
            job->stem = book::fileStem(book::build(*job->project, light), light, isoToday());
        }
        job->what = c.scope == book::Options::Scope::All ? std::string("le programme")
                  : c.scope == book::Options::Scope::Unit ? "l'unit\xC3\xA9 " + c.unit
                                                           : (c.sections.size() == 1 ? std::string("1 section") : std::to_string(c.sections.size()) + " sections");
        job->task = bgtasks::begin("Export lisible \xC2\xB7 " + job->what);
        job->started = std::chrono::steady_clock::now();
        ExportJob* raw = job.get();
        job->worker = std::thread([raw] {
            std::vector<std::string> written;
            std::string why;
            bool ok = false;
            try {
                ok = writeBook(*raw->project, raw->options, raw->formats, raw->folder, raw->stem, written, why, [raw](float f, const char* step) {
                    raw->fraction.store(f);
                    const std::lock_guard<std::mutex> g(raw->lock);
                    raw->step = step;
                });
            } catch (const std::exception& e) {
                why = std::string("export interrompu : ") + e.what();
            } catch (...) {
                why = "export interrompu";
            }
            {
                const std::lock_guard<std::mutex> g(raw->lock);
                raw->written = std::move(written);
                if (!ok) raw->error = why.empty() ? std::string("l'\xC3\xA9" "criture n'a pas abouti") : why;
            }
            raw->done.store(true, std::memory_order_release);
        });
        exportJob_ = std::move(job);
        if (status_) status_->setTransientMessage("Export lisible de " + exportJob_->what + " en cours : la barre du haut le suit.", 6.0);
    });
}

bool MainAnalysisScreen::exportProgramNow(int scope, const std::string& unit, const std::vector<domain::Index>& sections, const std::string& folder,
                                          const std::string& formats, std::vector<std::string>* written, std::string* why) {
    const auto p = app_.project();
    if (!p) {
        if (why) *why = "aucun projet";
        return false;
    }
    book::Options o;
    o.scope = scope == 1 ? book::Options::Scope::Unit : scope == 2 ? book::Options::Scope::Sections : book::Options::Scope::All;
    o.unit = unit;
    o.sections = sections;
    o.exportedAt = frenchNow();
    o.appVersion = XPG_ANALYZER_VERSION;
    o.projectName = projectName(app_);
    if (o.scope == book::Options::Scope::Unit && unit.empty()) {
        if (why) *why = "quelle unit\xC3\xA9 ?";
        return false;
    }
    if (o.scope == book::Options::Scope::Sections && sections.empty()) {
        if (why) *why = "aucune section";
        return false;
    }
    book::Options light = o;
    light.access = light.dfbAppendix = light.variablesAppendix = false;
    const auto stem = book::fileStem(book::build(*p, light), light, isoToday());
    std::vector<std::string> out;
    std::string reason;
    const bool ok = writeBook(*p, o, formatsOf(formats), folder, stem, out, reason, {});
    if (written) *written = out;
    if (why) *why = reason;
    if (ok) lastExportFolder_ = folder;
    return ok;
}

void MainAnalysisScreen::pollExportJob() {
    if (!exportJob_) return;
    auto& j = *exportJob_;
    const float f = j.fraction.load();
    if (j.task && std::abs(f - j.shown) >= 0.01f) {
        j.shown = f;
        bgtasks::progress(j.task, f);
    }
    if (!j.done.load(std::memory_order_acquire)) return;
    const auto job = std::move(exportJob_);
    exportJob_.reset();
    if (job->worker.joinable()) job->worker.join();
    if (job->task) bgtasks::end(job->task);
    std::vector<std::string> written;
    std::string error;
    {
        const std::lock_guard<std::mutex> g(job->lock);
        written = job->written;
        error = job->error;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - job->started).count();
    if (!error.empty()) {
        bgtasks::post({"export:programme", "Projet", "Export lisible impossible", error, {}, {}, "error"});
        app_.menus().ShowDialog(std::make_unique<MessageDialog>("Export lisible impossible", error, MessageDialog::Icon::Error),
                                [](const menu::DialogResult&) {});
        return;
    }
    lastExportFolder_ = job->folder;
    std::string names;
    for (const auto& w : written) names += (names.empty() ? "" : ", ") + leafOf(w);
    if (status_)
        status_->setMessage("Programme lisible export\xC3\xA9 en " + secondsText(secs) + " : " + names + " (dans " + job->folder + ").");
    bgtasks::post({"export:programme", "Projet", "Programme lisible export\xC3\xA9 (" + job->what + ")", names, "Ouvrir le dossier",
                   "program.export.open", "ok"});
    if (job->openFolder) {
        const auto err = dossiers::ouvrirDansLeSysteme(job->folder);
        if (!err.empty() && status_) status_->setTransientMessage(err, 8.0, StatusBar::Severity::Warning);
    }
}

// ================================================================ le comparateur ===
SectionComparePane* MainAnalysisScreen::sectionComparePane() const { return dynamic_cast<SectionComparePane*>(apiTab("comparer")); }

bool MainAnalysisScreen::openSectionCompare(std::vector<domain::Index> sections, std::string* why) {
    const auto p = app_.project();
    if (!p) {
        if (why) *why = "aucun projet";
        return false;
    }
    std::vector<domain::Index> chosen;
    for (const auto s : sections)
        if (s < p->sections.size() && std::find(chosen.begin(), chosen.end(), s) == chosen.end()) chosen.push_back(s);
    if (chosen.empty()) {
        if (why) *why = "aucune section";
        return false;
    }
    if (chosen.size() == 1) {
        // Une seule : celle dont le nom lui ressemble le plus (SFC_ManuA -> SFC_ManuB), du meme langage.
        const auto& a = p->sections[chosen.front()];
        const std::string an = text(*p, a.name);
        domain::Index best = domain::kNoIndex;
        std::size_t bestScore = 0;
        for (domain::Index s = 0; s < p->sections.size(); ++s) {
            if (s == chosen.front()) continue;
            const auto& b = p->sections[s];
            const std::size_t score = commonPrefix(an, text(*p, b.name)) * 4 + (b.language == a.language ? 2u : 0u)
                                    + (domain::taskOf(*p, s) == domain::taskOf(*p, chosen.front()) ? 1u : 0u);
            if (best == domain::kNoIndex || score > bestScore) {
                best = s;
                bestScore = score;
            }
        }
        if (best == domain::kNoIndex) {
            if (why) *why = "le projet n'a qu'une section";
            return false;
        }
        chosen.push_back(best);
    }
    openApiPane("comparer");
    auto* pane = sectionComparePane();
    if (!pane) {
        if (why) *why = "l'onglet ne s'ouvre pas";
        return false;
    }
    if (chosen.size() == 2) pane->setPair(chosen[0], chosen[1]);
    else pane->setSections(chosen);
    return true;
}

// ======================================================== les demandes, la barre ===
bool MainAnalysisScreen::lisibleRequest(const std::string& key) {
    const auto indices = [](std::string_view list) {
        std::vector<domain::Index> out;
        std::size_t at = 0;
        while (at < list.size()) {
            auto end = list.find(',', at);
            if (end == std::string_view::npos) end = list.size();
            const auto part = list.substr(at, end - at);
            if (!part.empty() && part.find_first_not_of("0123456789") == std::string_view::npos)
                out.push_back(static_cast<domain::Index>(std::stoul(std::string(part))));
            at = end + 1;
        }
        return out;
    };
    const auto starts = [&key](std::string_view prefix) { return key.size() >= prefix.size() && key.compare(0, prefix.size(), prefix) == 0; };
    if (starts("comparer:")) {
        std::string why;
        if (!openSectionCompare(indices(std::string_view(key).substr(9)), &why) && status_)
            status_->setTransientMessage("Comparer : " + why + ".", 6.0, StatusBar::Severity::Warning);
        return true;
    }
    if (starts("exporter:")) {
        const auto rest = key.substr(9);
        if (rest.rfind("unite:", 0) == 0) askProgramExport(1, rest.substr(6));
        else if (rest.rfind("sections:", 0) == 0) askProgramExport(2, {}, indices(std::string_view(rest).substr(9)));
        else askProgramExport(0);
        return true;
    }
    if (starts("icone:")) {
        // "icone:<cle>\n<cle>..." : les cles viennent du volet (Unites de programme).
        std::vector<std::string> keys;
        std::string rest = key.substr(6), one;
        for (const char c : rest + "\n") {
            if (c == '\n') {
                if (!one.empty()) keys.push_back(one);
                one.clear();
            } else {
                one += c;
            }
        }
        if (keys.empty()) return true;
        // Le nom et le genre du premier : "section:MAST/Init" -> Init, Section.
        const auto& first = keys.front();
        const auto colon = first.find(':');
        const std::string prefix = first.substr(0, colon);
        std::string name = colon == std::string::npos ? first : first.substr(colon + 1);
        if (const auto slash = name.rfind('/'); slash != std::string::npos) name = name.substr(slash + 1);
        ci::Kind kind = ci::Kind::Section;
        std::string word = "la section ";
        if (prefix == "unit") { kind = ci::Kind::Unit; word = "l'unit\xC3\xA9 "; }
        else if (prefix == "dfb") { kind = ci::Kind::Dfb; word = "le bloc DFB "; }
        else if (prefix == "dfbsection") { kind = ci::Kind::DfbSection; word = "la section de bloc "; }
        else if (prefix == "ddt") { kind = ci::Kind::Ddt; word = "le type "; }
        const std::string what = keys.size() == 1 ? word + name : std::to_string(keys.size()) + " \xC3\xA9l\xC3\xA9ments";
        askCodeIcon(std::move(keys), what, name, kind);
        return true;
    }
    return false;
}

bool MainAnalysisScreen::lisibleBarAction(std::string_view id) {
    if (id == "program.export") {
        askProgramExport(0);
        return true;
    }
    if (id == "program.export.open") {
        if (!lastExportFolder_.empty()) {
            const auto err = dossiers::ouvrirDansLeSysteme(lastExportFolder_);
            if (!err.empty() && status_) status_->setTransientMessage(err, 8.0, StatusBar::Severity::Warning);
        }
        return true;
    }
    if (id == "import.recap") {
        if (auto job = std::move(importReady_)) {
            importReady_.reset();
            bgtasks::withdraw("import:pret");
            deliverImport(job);
        }
        return true;
    }
    if (id == "import.cancel") {
        if (importJob_) {
            auto job = std::move(importJob_);
            importJob_.reset();
            job->cancel();
            if (!job->done()) droppedImports_.push_back(job);
            if (importTask_) {
                bgtasks::end(importTask_);
                importTask_ = 0;
            }
            importBackground_ = false;
            if (status_) status_->setTransientMessage("Import de " + job->source() + " annul\xC3\xA9 : le projet n'a pas chang\xC3\xA9.", 6.0);
        }
        return true;
    }
    return false;
}

} // namespace app
