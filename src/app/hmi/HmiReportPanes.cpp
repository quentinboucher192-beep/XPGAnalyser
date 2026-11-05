// Configuration > Rapports (lot 14).
#include "HmiReportPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "../ExportTarget.hpp"          // lot API 8 : Generer demande ou
#include "../../hmi/HmiReports.hpp"
#include "../../hmi/HmiReportLayout.hpp"   // 1.25.0 : la mise en page des rapports
#include "HmiReportLayoutPane.hpp"          // 1.25.0 : l'onglet Mise en page, l'apercu
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using hmi::Id;
using PG = ui::PropertyGrid;

namespace {

enum : int { CAdd = 1, CRemove, CNow, CLast };

const char* const kWeekdays[] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
const char* const kPeriods[] = {"Chaque jour", "Chaque semaine", "Chaque mois"};

std::string periodKeyOf(const std::string& v) {
    const std::string l = lower(trimmed(v));
    if (l == "jour" || l == "chaque jour" || l == "journalier" || l == "quotidien") return "jour";
    if (l == "semaine" || l == "chaque semaine" || l == "hebdomadaire") return "semaine";
    if (l == "mois" || l == "chaque mois" || l == "mensuel") return "mois";
    return {};
}

std::string periodChoice(const hmi::Report& r) { return r.period == "semaine" ? kPeriods[1] : r.period == "mois" ? kPeriods[2] : kPeriods[0]; }

std::string whenText(const hmi::Report& r) {
    if (r.period == "semaine") return std::string("chaque ") + kWeekdays[std::clamp(r.weekday, 1, 7) - 1] + " \xC3\xA0 " + r.time;
    if (r.period == "mois") return "le " + std::to_string(r.monthDay) + " de chaque mois \xC3\xA0 " + r.time;
    return "chaque jour \xC3\xA0 " + r.time;
}

std::string contentText(const hmi::Report& r) {
    std::vector<std::string> parts;
    if (r.alarms) parts.push_back("alarmes");
    const auto measures = splitList(r.measures);
    parts.push_back(measures.empty() ? std::string("mesures (archiv\xC3\xA9" "es)") : "mesures (" + std::to_string(measures.size()) + ")");
    if (r.production) parts.push_back("production");
    if (r.events) parts.push_back("\xC3\xA9v\xC3\xA9nements");
    return joinList(parts, ", ");
}

std::string sizeText(std::uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " o";
    if (bytes < 1024 * 1024) return std::to_string((bytes + 512) / 1024) + " Ko";
    return std::to_string(bytes / (1024 * 1024)) + "," + std::to_string((bytes % (1024 * 1024)) * 10 / (1024 * 1024)) + " Mo";
}

} // namespace

HmiReportsPane::HmiReportsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(CAdd, HmiGlyph::Plus, "Ajouter un rapport (chaque jour \xC3\xA0 06:00, en PDF)", "Ajouter");
    tools->add(CRemove, HmiGlyph::Delete, "Retirer le rapport choisi", "Retirer");
    tools->separator();
    // Lot API 8 : les deux demandent ou (exports/ propose) ; les envois programmes, non.
    // 1.25.0 : les deux ouvrent la fenetre << Generer le rapport maintenant >> (la periode, le format, la mise en page).
    tools->add(CNow, HmiGlyph::Play, "G\xC3\xA9n\xC3\xA9rer maintenant\xE2\x80\xA6 : la p\xC3\xA9riode en cours (ou une autre), le format, la mise en page, "
                                     "l'aper\xC3\xA7u ; puis exports/ (ou ailleurs)", "G\xC3\xA9n\xC3\xA9rer maintenant\xE2\x80\xA6");
    tools->add(CLast, HmiGlyph::Refresh, "La derni\xC3\xA8re p\xC3\xA9riode compl\xC3\xA8te (hier, la semaine pass\xC3\xA9" "e...)\xE2\x80\xA6 : la m\xC3\xAA" "me fen\xC3\xAAtre",
               "Derni\xC3\xA8re p\xC3\xA9riode\xE2\x80\xA6");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // 1.24.0 : plusieurs rapports choisis - Retirer les retire tous (un Ctrl+Z) ; Generer : un a la fois.
    tools_->setEnabledWhen(CRemove, [this] { return !selectedReport().empty(); });
    tools_->setLabelWhen(CRemove, [this] { return several::label("Retirer", table_ ? several::chosen(*table_, order_).size() : 0); });
    tools_->setEnabledWhen(CNow, [this] { return !selectedReport().empty() && hosts_.write && table_->selectedModelRows().size() <= 1; });
    tools_->setEnabledWhen(CLast, [this] { return !selectedReport().empty() && hosts_.write && table_->selectedModelRows().size() <= 1; });

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto table = std::make_unique<ui::TableView>(base + ".reports");
    table->setColumns({{"Rapport", 190.f}, {"Quand", 230.f}, {"Format", 70.f}, {"Mise en page", 150.f}, {"Contenu", 260.f}, {"Destinataires", 180.f},
                       {"Prochain", 140.f}, {"Actif", 60.f}});
    table->setSelectionMode(ui::SelectionMode::Extended);   // 1.24.0 : plusieurs rapports a retirer d'un coup
    auto files = std::make_unique<ui::TableView>(base + ".files");
    files->setColumns({{"Fichier", 380.f}, {"\xC3\x89" "crit le", 160.f}, {"Taille", 90.f}});
    // 1.25.0 (le client : << pourquoi il n'y a pas Aller a ou Ouvrir dans l'explorateur ici ? >>) : la colonne Fichier est un
    // chemin (relatif au dossier du projet) - le clic droit propose Ouvrir dans l'explorateur, Aller a, Copier le chemin.
    files->setPathColumn(0);
    table_ = static_cast<ui::TableView*>(table.get());
    // 1.12.27 : Suppr, Ctrl+D, Inser dans la liste - comme les boutons de la barre
    tools_->bindListKeys(*table_, {CRemove, 0, CAdd});
    files_ = static_cast<ui::TableView*>(files.get());
    tabs->addTab({"Rapports", ui::Icon::Document}, std::move(table));
    tabs->addTab({"\xC3\x89" "crits", ui::Icon::Document}, std::move(files));
    // 1.25.0 : l'onglet Mise en page (la maquette validee) - les mises en page, leurs reglages, l'apercu.
    auto layouts = std::make_unique<HmiReportLayoutPane>(base + ".layouts", doc_, [this](core::CommandPtr c) { apply_(std::move(c)); });
    layouts_ = layouts.get();
    tabs->addTab({"Mise en page", ui::Icon::Document}, std::move(layouts));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    // 1.25.0 : la fenetre << Generer le rapport maintenant >>, au-dessus de tout (le dernier enfant).
    sheet_ = &static_cast<HmiReportGenerateSheet&>(addChild(std::make_unique<HmiReportGenerateSheet>(base + ".generate", *this)));
    links_ += layouts_->layoutsChanged->connect([this] { refresh(); });
    links_ += tabs_->currentChanged->connect([this](std::size_t i) {
        if (i == 2) layouts_->setPreviewReport(selectedReport());
        invalidateLayout();
    });

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case CAdd: (void)addReport(); break;
            case CRemove:
                // 1.24.0 : plusieurs rapports - d'un coup, un seul Ctrl+Z (sans question, comme un seul).
                if (const auto names = several::chosen(*table_, order_); names.size() > 1) {
                    several::Batch b;
                    b.verb = "Retirer";
                    b.noun = "rapport";
                    b.nouns = "rapports";
                    b.names = names;
                    b.remove = [this, names](std::size_t i, std::string* why) { return removeReport(names[i], why); };
                    b.report = [this](const std::string& text, bool warning) { say(text + " (les fichiers d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crits restent)", warning); };
                    b.alive = tools_->triggered;
                    several::run(std::move(b));
                    break;
                }
                if (!selectedReport().empty()) (void)removeReport(selectedReport());
                break;
            case CNow: case CLast: {
                // 1.25.0 : la fenetre << Generer le rapport maintenant >> (la periode, le format, la mise en page, l'apercu) ;
                // Generer y ecrit dans exports/, Ailleurs... demande ou (lot API 8, ExportTarget.hpp). Les rapports
                // PROGRAMMES (a l'heure dite) partent seuls : exports/, sans question.
                const std::string name = selectedReport();
                if (!name.empty()) openGenerate(name, a == CNow);
                break;
            }
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiReportsPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiReportsPane::say(std::string text, bool error) {
    if (error) ui::PropertyGrid::refuse(text);   // 1.12.19 (la regle des cases) : un refus se dit sous la case
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiReportsPane::selectedReport() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? std::string{} : order_[rows.front()];
}

void HmiReportsPane::selectReport(const std::string& name) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], name)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildProperties();
}

void HmiReportsPane::refresh() {
    refreshing_ = true;
    const std::string keep = selectedReport();
    const auto& p = doc_->project;
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> on;
    const double wall = hmi::wallEpoch();
    for (const auto& r : p.reports) {
        order_.push_back(r.name);
        std::string next = hosts_.next ? hosts_.next(r.id) : std::string{};
        if (next.empty()) next = r.enabled ? hmi::report::stampOf(hmi::report::nextDue(r, wall)) : std::string("d\xC3\xA9sactiv\xC3\xA9");
        rows.push_back({r.name, whenText(r), r.format, hmi::report::layoutOf(p, r).name, contentText(r),
                        r.recipients.empty() ? std::string("\xE2\x80\x94") : r.recipients, next, r.enabled ? "oui" : "non"});
        on.push_back(r.enabled);
    }
    tableModel_ = std::make_shared<Rows>(std::vector<std::string>{"Rapport", "Quand", "Format", "Mise en page", "Contenu", "Destinataires", "Prochain", "Actif"},
                                         std::move(rows), [on](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle st;
                                             if (r >= on.size()) return st;
                                             if (c == 0) {
                                                 st.bold = true;
                                                 st.icon = ui::Icon::Document;
                                             }
                                             if (c == 6) st.fgTone = on[r] ? ui::Tone::Accent : ui::Tone::None;
                                             return st;
                                         });
    table_->setModel(tableModel_);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], keep)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    tabs_->setTabBadge(0, std::to_string(order_.size()), ui::Tone::Accent);
    tabs_->setTabBadge(2, std::to_string(hmi::report::builtInLayouts().size() + p.reportLayouts.size()), ui::Tone::Accent);   // 1.25.0
    refreshing_ = false;
    refreshFiles();
    rebuildProperties();
}

void HmiReportsPane::refreshFiles() {
    namespace fs = std::filesystem;
    const std::string folder = hosts_.projectFolder ? hosts_.projectFolder() : std::string{};
    struct Item {
        std::string name, when;
        std::uintmax_t size{0};
        fs::file_time_type time{};
    };
    std::vector<Item> items;
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(fs::path(folder) / "exports", ec)) {
        for (const auto& e : fs::directory_iterator(fs::path(folder) / "exports", ec)) {
            const std::string name = e.path().filename().string();
            if (name.rfind("rapport_", 0) != 0) continue;
            Item it;
            it.name = name;
            it.size = e.file_size(ec);
            it.time = e.last_write_time(ec);
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(it.time - fs::file_time_type::clock::now()
                                                                                              + std::chrono::system_clock::now());
            it.when = hmi::report::stampOf(static_cast<double>(std::chrono::system_clock::to_time_t(sys)), true);
            items.push_back(std::move(it));
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.time > b.time; });
    std::vector<std::vector<std::string>> rows;
    for (const auto& it : items) rows.push_back({"exports/" + it.name, it.when, sizeText(it.size)});
    const std::size_t n = rows.size();
    filesModel_ = std::make_shared<Rows>(std::vector<std::string>{"Fichier", "\xC3\x89" "crit le", "Taille"}, std::move(rows));
    files_->setModel(filesModel_);
    tabs_->setTabBadge(1, std::to_string(n), n ? ui::Tone::Ok : ui::Tone::None);
}

bool HmiReportsPane::change(const std::string& label, const std::function<void(hmi::Project&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, fn);
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiReportsPane::addReport(const std::string& rawName, std::string* why) {
    const auto& p = doc_->project;
    std::string name = trimmed(rawName);
    if (name.empty() || !hmi::isIdentifier(name)) {
        const std::string m = "nom \xC2\xAB " + name + " \xC2\xBB : lettres, chiffres et _";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    if (p.reportByName(name)) {
        if (rawName != "Rapport_journalier") {
            const std::string m = name + " existe d\xC3\xA9j\xC3\xA0";
            say(m, true);
            if (why) *why = m;
            return false;
        }
        const std::string base = name;
        for (int k = 2; p.reportByName(name); ++k) name = base + "_" + std::to_string(k);
    }
    hmi::Report r;
    r.name = name;
    if (!change("Ajouter le rapport " + name, [&](hmi::Project& x) {
            r.id = x.allocate();
            x.reports.push_back(r);
        }))
        return false;
    selectReport(name);
    say(name + " : chaque jour \xC3\xA0 06:00, en PDF - la p\xC3\xA9riode, l'heure, le contenu et les destinataires \xC3\xA0 droite.");
    return true;
}

bool HmiReportsPane::removeReport(const std::string& name, std::string* why) {
    if (!doc_->project.reportByName(name)) {
        const std::string m = "pas de rapport " + name;
        say(m, true);
        if (why) *why = m;
        return false;
    }
    if (!change("Retirer le rapport " + name, [&](hmi::Project& x) { std::erase_if(x.reports, [&](const hmi::Report& r) { return same(r.name, name); }); }))
        return false;
    say(name + " retir\xC3\xA9 (Ctrl+Z le rend ; les fichiers d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crits restent).");
    return true;
}

bool HmiReportsPane::setReportField(const std::string& name, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* current = doc_->project.reportByName(name);
    if (!current) return fail("pas de rapport " + name);
    hmi::Report next = *current;
    const std::string v = trimmed(raw);
    if (key == "nom") {
        if (!hmi::isIdentifier(v)) return fail("nom \xC2\xAB " + v + " \xC2\xBB : lettres, chiffres et _");
        if (!same(v, current->name) && doc_->project.reportByName(v)) return fail(v + " existe d\xC3\xA9j\xC3\xA0");
        next.name = v;
    } else if (key == "titre") {
        next.title = v;
    } else if (key == "periode") {
        const std::string k = periodKeyOf(v);
        if (k.empty()) return fail("p\xC3\xA9riode : chaque jour, chaque semaine, chaque mois");
        next.period = k;
    } else if (key == "heure") {
        int m = 0;
        if (!hmi::report::parseTime(v, m)) return fail("heure \xC2\xAB " + v + " \xC2\xBB : 06:00");
        char b[32];
        std::snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60);
        next.time = b;
    } else if (key == "jour_semaine") {
        int d = 0;
        for (int k = 0; k < 7; ++k)
            if (same(v, kWeekdays[k])) d = k + 1;
        if (d == 0) d = std::atoi(v.c_str());
        if (d < 1 || d > 7) return fail("jour : lundi ... dimanche (ou 1 \xC3\xA0 7)");
        next.weekday = d;
    } else if (key == "jour_mois") {
        const int d = std::atoi(v.c_str());
        if (d < 1 || d > 28) return fail("jour du mois : de 1 \xC3\xA0 28 (tous les mois l'ont)");
        next.monthDay = d;
    } else if (key == "format") {
        const std::string l = lower(v);
        if (l.find("excel") != std::string::npos || l == "xlsx") next.format = "Excel";
        else if (l == "pdf") next.format = "PDF";
        else return fail("format : PDF ou Excel");
    } else if (key == "alarmes" || key == "production" || key == "evenements" || key == "actif") {
        const bool on = yes(v);
        (key == "alarmes" ? next.alarms : key == "production" ? next.production : key == "evenements" ? next.events : next.enabled) = on;
    } else if (key == "mise_en_page") {   // 1.25.0 : sa mise en page ; << (par defaut : X) >> ou vide : celle par defaut
        if (v.empty() || v.rfind("(par d", 0) == 0) next.layout.clear();
        else if (const auto* l = hmi::report::findLayout(doc_->project, v)) next.layout = l->name;
        else return fail("mise en page inconnue : " + v + " (l'onglet Mise en page)");
    } else if (key == "mesures") {
        next.measures = joinList(splitList(v));
    } else if (key == "destinataires") {
        const auto names = splitList(v);
        std::vector<std::string> unknown;
        for (const auto& n : names)
            if (!doc_->project.recipientByName(n)) unknown.push_back(n);
        if (!unknown.empty())
            return fail("destinataire(s) inconnu(s) : " + joinList(unknown, ", ") + " (Configuration > Notifications)");
        next.recipients = joinList(names);
    } else {
        return fail("champ inconnu : " + key);
    }
    if (next == *current) return true;
    const Id id = current->id;
    if (!change("Rapport " + current->name, [&](hmi::Project& x) {
            if (auto* r = x.report(id)) *r = next;
        }))
        return false;
    if (key == "nom") selectReport(next.name);
    return true;
}

bool HmiReportsPane::generate(const std::string& name, bool current, std::string* where) {
    const auto* r = doc_->project.reportByName(name);
    std::string path;
    bool ok = false;
    if (!r) path = "pas de rapport " + name;
    else if (!hosts_.write) path = "\xC3\xA9" "criture impossible ici";
    else ok = hosts_.write(r->id, current, &path);
    if (where) *where = path;
    say(ok ? "Rapport \xC3\xA9" "crit : " + path : "Rapport non \xC3\xA9" "crit : " + path, !ok);
    refreshFiles();
    // L'onglet Ecrits montre exports/ : un rapport ecrit ailleurs (lot API 8, le
    // bouton ...) n'y est pas - la barre d'etat dit son chemin, l'onglet reste.
    if (ok && path.rfind("exports/", 0) == 0) tabs_->setCurrentIndex(1);
    return ok;
}

bool HmiReportsPane::writeOffline(const hmi::Project& p, const hmi::History& h, Id id, bool current,
                                  const std::function<bool(const hmi::ExportRequest&, std::string*)>& write, std::string* where,
                                  hmi::ReportOutput* output) {
    hmi::ReportOptions o;
    o.current = current;
    return writeOffline(p, h, id, o, write, where, output);
}

bool HmiReportsPane::writeOffline(const hmi::Project& p, const hmi::History& h, Id id, const hmi::ReportOptions& options,
                                  const std::function<bool(const hmi::ExportRequest&, std::string*)>& write, std::string* where,
                                  hmi::ReportOutput* output) {
    const bool current = options.current;
    const auto* r = p.report(id);
    if (!r) {
        if (where) *where = "rapport introuvable";
        return false;
    }
    namespace rp = hmi::report;
    const double wall = hmi::wallEpoch();
    double to = current ? wall + 1.0 : rp::lastDue(*r, wall);   // maintenant : ce qui vient d'arriver compris
    double from = current ? rp::periodStart(*r, rp::nextDue(*r, wall)) : rp::periodStart(*r, to);
    if (options.to > options.from) {   // 1.25.0 : une periode choisie
        from = options.from;
        to = options.to;
    }
    const auto content = rp::build(p, h, *r, from, to, {}, hmi::wallStamp());
    const std::string& formatName = options.format.empty() ? r->format : options.format;
    const auto format = formatName == "Excel" ? hmi::ExportFormat::Excel : hmi::ExportFormat::Pdf;
    // 1.25.0 : la mise en page choisie, sinon celle du rapport (sinon celle par defaut du projet).
    const hmi::ReportLayout* chosen = options.layout.empty() ? nullptr : rp::findLayout(p, options.layout);
    const hmi::ReportLayout& layout = chosen ? *chosen : rp::layoutOf(p, *r);
    hmi::ExportRequest rq;
    rq.fileName = rp::fileName(*r, to, format);
    rq.format = std::string(hmi::exportFormatLabel(format));
    rq.source = "rapport:" + r->name;
    for (const auto& t : content.sections) rq.rows += t.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(rp::render(p, content, format, layout));
    rq.origin = "Rapport " + r->name;
    std::string path;
    const bool ok = write && write(rq, &path);
    if (where) *where = ok ? path : (path.empty() ? std::string("aucun dossier d'export (le projet est-il enregistr\xC3\xA9 ?)") : path);
    if (ok && output) {
        output->report = r->id;
        output->name = r->name;
        output->title = content.title;
        output->from = rp::stampOf(from);
        output->to = rp::stampOf(to);
        output->fileName = rq.fileName;
        output->path = path;
        output->format = rq.format;
        output->data = rq.data;
        output->recipients = options.send ? r->recipients : std::string{};   // 1.25.0 : le fichier seul
    }
    return ok;
}

// ---- 1.25.0 : Generer avec ce qu'on a choisi ; la fenetre ; l'onglet Mise en page ----
bool HmiReportsPane::generateWith(const std::string& name, const hmi::ReportOptions& options, std::string* where, bool openAfter) {
    const auto* r = doc_->project.reportByName(name);
    std::string path;
    bool ok = false;
    if (!r) path = "pas de rapport " + name;
    else if (hosts_.writeWith) ok = hosts_.writeWith(r->id, options, &path);
    else if (hosts_.write) ok = hosts_.write(r->id, options.current, &path);
    else path = "\xC3\xA9" "criture impossible ici";
    if (where) *where = path;
    std::string opened;
    if (ok && openAfter) {
        if (!hosts_.openFile) opened = " (l'ouvrir : impossible ici)";
        else {
            std::string full = path;
            if (const std::string folder = hosts_.projectFolder ? hosts_.projectFolder() : std::string{}; !folder.empty() && path.rfind("exports/", 0) == 0)
                full = (std::filesystem::path(folder) / std::filesystem::path(path)).string();
            const std::string why = hosts_.openFile(full);
            opened = why.empty() ? std::string(" - ouvert") : " (l'ouvrir : " + why + ")";
        }
    }
    say(ok ? "Rapport \xC3\xA9" "crit : " + path + opened + (options.send ? std::string{} : std::string(" - sans l'envoyer")) : "Rapport non \xC3\xA9" "crit : " + path, !ok);
    refreshFiles();
    if (ok && path.rfind("exports/", 0) == 0) tabs_->setCurrentIndex(1);
    return ok;
}

void HmiReportsPane::openGenerate(const std::string& name, bool current) {
    if (!doc_->project.reportByName(name)) return;
    sheet_->open(name, current);
    invalidateLayout();
}

void HmiReportsPane::showLayouts(const std::string& select) {
    tabs_->setCurrentIndex(2);
    if (!select.empty()) (void)layouts_->selectLayout(select);
    layouts_->setPreviewReport(selectedReport());
    invalidateLayout();
}

// ============================================================================ 1.25.0 : Generer le rapport maintenant ===
namespace {
constexpr float kSheetW = 780.f, kSheetH = 470.f, kSheetPad = 16.f, kSheetTitleH = 44.f;
const char* const kEditLayouts = "Modifier les mises en page\xE2\x80\xA6";

// "2026-11-04 06:00" -> "04/11 06:00".
std::string shortDay(const std::string& stamp) {
    if (stamp.size() < 16) return stamp;
    return stamp.substr(8, 2) + "/" + stamp.substr(5, 2) + " " + stamp.substr(11, 5);
}
} // namespace

HmiReportGenerateSheet::HmiReportGenerateSheet(std::string id, HmiReportsPane& pane) : ui::Widget(std::move(id)), pane_(pane) {
    const std::string base = this->id();
    const char* const periods[] = {"La derni\xC3\xA8re", "En cours", "Choisir\xE2\x80\xA6"};
    for (int i = 0; i < 3; ++i) {
        auto t = std::make_unique<ui::ToggleButton>(periods[i], base + ".period" + std::to_string(i));
        auto* raw = &static_cast<ui::ToggleButton&>(addChild(std::move(t)));
        links_ += raw->toggled->connect([this, i](bool) {
            if (!syncing_) setPeriod(i);
        });
        periods_.push_back(raw);
    }
    fromBox_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".from")));
    toBox_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".to")));
    fromBox_->setPlaceholder("2026-11-01 06:00");
    toBox_->setPlaceholder("2026-11-02 06:00");
    const auto rangeEdited = [this](const std::string&) {
        if (syncing_) return;
        std::string why;
        if (setRange(fromBox_->text(), toBox_->text(), &why)) updatePreview();
    };
    links_ += fromBox_->editingDone->connect(rangeEdited);
    links_ += toBox_->editingDone->connect(rangeEdited);
    for (const char* f : {"PDF", "Excel"}) {
        auto t = std::make_unique<ui::ToggleButton>(f, base + ".format" + f);
        auto* raw = &static_cast<ui::ToggleButton&>(addChild(std::move(t)));
        const std::string name = f;
        links_ += raw->toggled->connect([this, name](bool) {
            if (!syncing_) setFormat(name);
        });
        formats_.push_back(raw);
    }
    layoutBox_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".layout")));
    links_ += layoutBox_->selectionChanged->connect([this](int i) {
        if (syncing_ || i < 0) return;
        if (static_cast<std::size_t>(i) >= layoutNames_.size()) {   // << Modifier les mises en page... >>
            const std::string keep = layout_;
            close();
            pane_.showLayouts(keep);
            return;
        }
        (void)setLayout(layoutNames_[static_cast<std::size_t>(i)]);
    });
    openBox_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Ouvrir le fichier", base + ".open")));
    sendBox_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("L'envoyer aux destinataires du rapport", base + ".send")));
    links_ += openBox_->stateChanged->connect([this](ui::Checkbox::State st) {
        if (!syncing_) openAfter_ = st == ui::Checkbox::State::Checked;
    });
    links_ += sendBox_->stateChanged->connect([this](ui::Checkbox::State st) {
        if (!syncing_) send_ = st == ui::Checkbox::State::Checked;
    });
    preview_ = &static_cast<HmiReportPreview&>(addChild(std::make_unique<HmiReportPreview>(base + ".preview")));
    preview_->setThumbnail(true);
    prev_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xE2\x97\x80", base + ".prev")));
    next_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xE2\x96\xB6", base + ".next")));
    links_ += prev_->clicked->connect([this] { preview_->setPage(preview_->page() > 0 ? preview_->page() - 1 : 0); invalidate(); });
    links_ += next_->clicked->connect([this] { preview_->setPage(preview_->page() + 1); invalidate(); });
    elsewhere_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ailleurs\xE2\x80\xA6", base + ".elsewhere")));
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("G\xC3\xA9n\xC3\xA9rer", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += elsewhere_->clicked->connect([this] { (void)generateElsewhere(); });
    links_ += cancel_->clicked->connect([this] { close(); });
    links_ += ok_->clicked->connect([this] { (void)generate(); });
}

void HmiReportGenerateSheet::open(const std::string& report, bool current) {
    const auto* r = pane_.document()->project.reportByName(report);
    if (!r) return;
    report_ = r->name;
    period_ = current ? Current : Last;
    format_ = r->format == "Excel" ? "Excel" : "PDF";
    layout_ = hmi::report::layoutOf(pane_.document()->project, *r).name;
    openAfter_ = true;
    send_ = !r->recipients.empty();
    from_.clear();
    to_.clear();
    open_ = true;
    sync();
    updatePreview();
    setVisibility(ui::Visibility::Visible);
    if (parent()) parent()->invalidateLayout();
    invalidateLayout();
}

void HmiReportGenerateSheet::close() {
    open_ = false;
    setVisibility(ui::Visibility::Collapsed);
    if (parent()) parent()->invalidateLayout();
}

std::vector<std::string> HmiReportGenerateSheet::layoutChoices() const {
    // Celle du rapport d'abord ; puis celles du projet, puis les fournies.
    const auto& p = pane_.document()->project;
    std::vector<std::string> out;
    const auto* r = p.reportByName(report_);
    if (r) out.push_back(hmi::report::layoutOf(p, *r).name);
    for (const auto& l : p.reportLayouts)
        if (std::find(out.begin(), out.end(), l.name) == out.end()) out.push_back(l.name);
    for (const auto& l : hmi::report::builtInLayouts())
        if (std::find(out.begin(), out.end(), l.name) == out.end()) out.push_back(l.name);
    return out;
}

void HmiReportGenerateSheet::sync() {
    syncing_ = true;
    for (int i = 0; i < 3; ++i) periods_[static_cast<std::size_t>(i)]->setChecked(i == period_);
    formats_[0]->setChecked(format_ == "PDF");
    formats_[1]->setChecked(format_ == "Excel");
    const bool chosen = period_ == Chosen;
    fromBox_->setVisibility(chosen ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    toBox_->setVisibility(chosen ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (chosen && from_.empty()) {
        const auto [a, b] = range();
        from_ = hmi::report::stampOf(a);
        to_ = hmi::report::stampOf(b);
    }
    if (fromBox_->text() != from_) fromBox_->setText(from_);
    if (toBox_->text() != to_) toBox_->setText(to_);
    layoutNames_ = layoutChoices();
    const auto& p = pane_.document()->project;
    const auto* r = p.reportByName(report_);
    const std::string own = r ? hmi::report::layoutOf(p, *r).name : std::string{};
    std::vector<ui::DropDown::Item> items;
    int at = 0;
    for (std::size_t i = 0; i < layoutNames_.size(); ++i) {
        const auto* l = hmi::report::findLayout(p, layoutNames_[i]);
        std::string label = (hmi::report::isBuiltIn(layoutNames_[i]) ? "(fournie) " : "") + layoutNames_[i];
        if (layoutNames_[i] == own) label += "  \xE2\x80\x94 celle du rapport";
        if (l) label += "  \xC2\xB7 " + hmi::report::pageText(*l);
        items.push_back({label, layoutNames_[i]});
        if (layoutNames_[i] == layout_) at = static_cast<int>(i);
    }
    items.push_back({kEditLayouts, "__modifier__"});
    layoutBox_->setItems(std::move(items));
    layoutBox_->setSelectedIndex(at);
    openBox_->setState(openAfter_ ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    sendBox_->setState(send_ ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    const std::size_t recipients = r ? splitList(r->recipients).size() : 0;
    sendBox_->setLabel(recipients ? "L'envoyer aux destinataires du rapport (" + std::to_string(recipients) + ")"
                                  : std::string("L'envoyer aux destinataires du rapport (il n'en a pas)"));
    sendBox_->setEnabled(recipients > 0);
    syncing_ = false;
    invalidate();
}

std::pair<double, double> HmiReportGenerateSheet::range() const {
    namespace rp = hmi::report;
    const auto* r = pane_.document()->project.reportByName(report_);
    hmi::Report rr = r ? *r : hmi::Report{};
    const double wall = hmi::wallEpoch();
    if (period_ == Chosen) {
        const double a = rp::epochOf(from_), b = rp::epochOf(to_);
        if (std::isfinite(a) && std::isfinite(b) && b > a) return {a, b};
    }
    if (period_ == Current) return {rp::periodStart(rr, rp::nextDue(rr, wall)), wall + 1.0};
    const double to = rp::lastDue(rr, wall);
    return {rp::periodStart(rr, to), to};
}

std::string HmiReportGenerateSheet::periodText(int period) const {
    namespace rp = hmi::report;
    const auto* r = pane_.document()->project.reportByName(report_);
    hmi::Report rr = r ? *r : hmi::Report{};
    const double wall = hmi::wallEpoch();
    if (period == Last) {
        const double to = rp::lastDue(rr, wall);
        return "La derni\xC3\xA8re (" + shortDay(rp::stampOf(rp::periodStart(rr, to))) + " \xE2\x86\x92 " + shortDay(rp::stampOf(to)) + ")";
    }
    if (period == Current) return "En cours (depuis " + shortDay(rp::stampOf(rp::periodStart(rr, rp::nextDue(rr, wall)))) + ")";
    return "Choisir\xE2\x80\xA6";
}

void HmiReportGenerateSheet::setPeriod(int period) {
    period_ = std::clamp(period, 0, 2);
    if (period_ != Chosen) {
        from_.clear();
        to_.clear();
    }
    sync();
    updatePreview();
    invalidateLayout();
}

bool HmiReportGenerateSheet::setRange(const std::string& from, const std::string& to, std::string* why) {
    namespace rp = hmi::report;
    const double a = rp::epochOf(trimmed(from)), b = rp::epochOf(trimmed(to));
    if (!std::isfinite(a) || !std::isfinite(b)) {
        if (why) *why = "la p\xC3\xA9riode : 2026-11-01 06:00 \xE2\x86\x92 2026-11-02 06:00";
        return false;
    }
    if (!(b > a)) {
        if (why) *why = "la fin vient apr\xC3\xA8s le d\xC3\xA9" "but";
        return false;
    }
    period_ = Chosen;
    from_ = rp::stampOf(a);
    to_ = rp::stampOf(b);
    sync();
    updatePreview();
    return true;
}

void HmiReportGenerateSheet::setFormat(const std::string& format) {
    format_ = format == "Excel" ? "Excel" : "PDF";
    sync();
}

bool HmiReportGenerateSheet::setLayout(const std::string& name) {
    const auto* l = hmi::report::findLayout(pane_.document()->project, name);
    if (!l) return false;
    layout_ = l->name;
    sync();
    updatePreview();
    return true;
}

void HmiReportGenerateSheet::setOpenAfter(bool on) {
    openAfter_ = on;
    sync();
}

void HmiReportGenerateSheet::setSend(bool on) {
    send_ = on;
    sync();
}

hmi::ReportOptions HmiReportGenerateSheet::options() const {
    hmi::ReportOptions o;
    o.current = period_ == Current;
    if (period_ == Chosen) {
        const auto [a, b] = range();
        o.from = a;
        o.to = b;
    }
    o.format = format_;
    o.layout = layout_;
    const auto* r = pane_.document()->project.reportByName(report_);
    o.send = send_ && r && !r->recipients.empty();
    return o;
}

std::string HmiReportGenerateSheet::fileHint() const {
    const auto* r = pane_.document()->project.reportByName(report_);
    if (!r) return {};
    const auto [a, b] = range();
    (void)a;
    return "\xC3\x89" "crit dans exports/ : "
         + hmi::report::fileName(*r, b, format_ == "Excel" ? hmi::ExportFormat::Excel : hmi::ExportFormat::Pdf);
}

void HmiReportGenerateSheet::updatePreview() {
    namespace rp = hmi::report;
    const auto& doc = pane_.document();
    const auto* r = doc->project.reportByName(report_);
    const auto* l = rp::findLayout(doc->project, layout_);
    if (!r || !l) {
        preview_->setDocument(nullptr);
        return;
    }
    const auto [a, b] = range();
    const auto content = rp::build(doc->project, doc->history, *r, a, b, {}, hmi::wallStamp());
    preview_->setDocument(std::make_shared<const rp::PageDoc>(rp::paginate(doc->project, content, *l)));
    preview_->setPage(0);
    invalidate();
}

bool HmiReportGenerateSheet::generate(std::string* where) {
    if (!open_) return false;
    const std::string report = report_;
    const auto o = options();
    const bool openAfter = openAfter_;
    close();
    return pane_.generateWith(report, o, where, openAfter);
}

bool HmiReportGenerateSheet::generateElsewhere() {
    if (!open_) return false;
    const std::string report = report_;
    const auto o = options();
    const bool openAfter = openAfter_;
    close();
    const bool excel = o.format == "Excel";
    auto* pane = &pane_;
    const auto go = [pane, report, o, openAfter] { (void)pane->generateWith(report, o, nullptr, openAfter); };
    if (!askExportTarget("le rapport " + report + (excel ? " (Excel)" : " (PDF)"), excel ? "Classeurs Excel|*.xlsx" : "Documents PDF|*.pdf", go)) go();
    return true;
}

void HmiReportGenerateSheet::onLayout() {
    const auto b = bounds();
    const float w = std::min(kSheetW, std::max(420.f, b.w - 40.f)), h = std::min(kSheetH, std::max(320.f, b.h - 40.f));
    panel_ = {std::floor(b.x + (b.w - w) * 0.5f), std::floor(b.y + std::max(10.f, (b.h - h) * 0.35f)), w, h};
    const float thumbW = std::min(250.f, w * 0.32f);
    const float x = panel_.x + kSheetPad, colW = w - thumbW - 3.f * kSheetPad;
    float y = panel_.y + kSheetTitleH + 10.f;
    labelsX_ = x;
    // La periode : trois boutons ; Choisir : du, au.
    rowsY_[0] = y;
    y += 20.f;
    const float pw = std::min(colW, 520.f);
    periods_[0]->setBounds({x, y, pw * 0.5f, 28.f});
    periods_[1]->setBounds({x + pw * 0.5f + 4.f, y, pw * 0.24f, 28.f});
    periods_[2]->setBounds({x + pw * 0.74f + 8.f, y, pw * 0.26f - 8.f, 28.f});
    y += 32.f;
    fromBox_->setBounds({x, y, (colW - 8.f) * 0.5f, 26.f});
    toBox_->setBounds({x + (colW + 8.f) * 0.5f, y, (colW - 8.f) * 0.5f, 26.f});
    if (period_ == Chosen) y += 30.f;
    y += 8.f;
    rowsY_[1] = y;
    y += 20.f;
    formats_[0]->setBounds({x, y, 70.f, 28.f});
    formats_[1]->setBounds({x + 74.f, y, 70.f, 28.f});
    y += 32.f + 16.f;
    rowsY_[2] = y;
    y += 20.f;
    layoutBox_->setBounds({x, y, colW, 28.f});
    y += 40.f;
    rowsY_[3] = y;
    y += 20.f;
    openBox_->setBounds({x, y, colW, 24.f});
    y += 26.f;
    sendBox_->setBounds({x, y, colW, 24.f});
    y += 28.f;
    rowsY_[4] = y;
    const float tx = panel_.x + w - thumbW - kSheetPad;
    preview_->setBounds({tx, panel_.y + kSheetTitleH + 10.f, thumbW, h - kSheetTitleH - 120.f});
    const float navY = panel_.y + h - 100.f;
    prev_->setBounds({tx + thumbW * 0.5f - 64.f, navY, 40.f, 26.f});
    next_->setBounds({tx + thumbW * 0.5f + 24.f, navY, 40.f, 26.f});
    const float by = panel_.y + h - 44.f;
    ok_->setBounds({panel_.x + w - kSheetPad - 110.f, by, 110.f, 30.f});
    cancel_->setBounds({panel_.x + w - kSheetPad - 228.f, by, 110.f, 30.f});
    elsewhere_->setBounds({panel_.x + w - kSheetPad - 346.f, by, 110.f, 30.f});
}

void HmiReportGenerateSheet::onPaint(const ui::PaintContext& ctx) {
    if (!open_) return;
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), gfx::Color{20, 24, 30, 110});
    ctx.r.fillRect({panel_.x + 4, panel_.y + 6, panel_.w, panel_.h}, gfx::Color{0, 0, 0, 70});
    ctx.r.fillRect(panel_, c.panelBg);
    ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
    ctx.r.fillRect({panel_.x, panel_.y, panel_.w, kSheetTitleH}, c.headerBg);
    ctx.r.drawText({panel_.x + kSheetPad, panel_.y + 12}, "G\xC3\xA9n\xC3\xA9rer le rapport maintenant \xE2\x80\x94 " + report_, ctx.theme.font.uiBold, c.text);
    const auto label = [&](float y, const std::string& t) { ctx.r.drawText({labelsX_, y}, t, ctx.theme.font.smallUi, c.textMuted); };
    label(rowsY_[0], "P\xC3\xA9riode \xC2\xB7 " + periodText(period_ == Chosen ? Last : period_));
    label(rowsY_[1], "Format");
    if (format_ == "Excel")
        ctx.r.drawText({labelsX_ + 156.f, rowsY_[1] + 26.f}, "Excel : une feuille par partie ; la mise en page r\xC3\xA8gle l'ordre et les parties.",
                       ctx.theme.font.smallUi, c.textMuted);
    label(rowsY_[2], "Mise en page");
    label(rowsY_[3], "Apr\xC3\xA8s l'\xC3\xA9" "criture");
    ctx.r.drawText({labelsX_, rowsY_[4]}, fileHint(), ctx.theme.font.smallUi, c.textMuted);
    // Sous l'apercu : la mise en page, sa page, le nombre de pages.
    const auto pr = preview_->bounds();
    const auto* l = hmi::report::findLayout(pane_.document()->project, layout_);
    const std::string caption = layout_ + (l ? " \xC2\xB7 " + hmi::report::pageText(*l) : std::string{}) + " \xC2\xB7 page "
                              + std::to_string(preview_->pageCount() ? preview_->page() + 1 : 0) + " / " + std::to_string(preview_->pageCount());
    ctx.r.drawText({pr.x, pr.y + pr.h + 6.f}, caption, ctx.theme.font.smallUi, c.text);
    ctx.r.drawText({panel_.x + kSheetPad, panel_.y + panel_.h - 38.f}, "L'aper\xC3\xA7u suit la mise en page choisie.", ctx.theme.font.smallUi, c.textMuted);
}

ui::EventResult HmiReportGenerateSheet::onEvent(const ui::InputEvent& ev) {
    if (!open_) return ui::EventResult::Ignored;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            close();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && !k->mods.ctrl && !k->mods.alt && !fromBox_->focused() && !toBox_->focused()) {
            (void)generate();
            return ui::EventResult::Consumed;
        }
    }
    // La fenetre garde tout : le volet dessous ne recoit rien tant qu'elle est ouverte.
    return ui::EventResult::Consumed;
}


void HmiReportsPane::rebuildProperties() {
    std::vector<PG::Category> cats;
    const auto* r = doc_->project.reportByName(selectedReport());
    if (r) {
        const std::string key = r->name;
        const auto field = [this, key](const char* f) {
            return [this, key, f](std::string_view v) {
                message_.clear();
                return setReportField(key, f, std::string(v));
            };
        };
        PG::Category c;
        c.name = "Rapport choisi";
        c.properties.push_back(prop("Nom", r->name, PG::ValueType::Text, field("nom"), "Il nomme le fichier : rapport_<nom>_<date>.pdf."));
        c.properties.push_back(prop("Titre", r->title, PG::ValueType::Text, field("titre"), "Vide : \xC2\xAB Rapport journalier \xE2\x80\x94 <projet> \xC2\xBB."));
        c.properties.push_back(prop("P\xC3\xA9riode", periodChoice(*r), PG::ValueType::Enum, field("periode"),
                                    "La p\xC3\xA9riode finit \xC3\xA0 l'heure de d\xC3\xA9part : de 06:00 la veille \xC3\xA0 06:00.",
                                    {kPeriods[0], kPeriods[1], kPeriods[2]}));
        c.properties.push_back(prop("Heure", r->time, PG::ValueType::Text, field("heure"), "L'heure de d\xC3\xA9part : 06:00 (le d\xC3\xA9" "but d'un poste)."));
        if (r->period == "semaine")
            c.properties.push_back(prop("Jour", kWeekdays[std::clamp(r->weekday, 1, 7) - 1], PG::ValueType::Enum, field("jour_semaine"), {},
                                        {kWeekdays[0], kWeekdays[1], kWeekdays[2], kWeekdays[3], kWeekdays[4], kWeekdays[5], kWeekdays[6]}));
        if (r->period == "mois")
            c.properties.push_back(prop("Jour du mois", std::to_string(r->monthDay), PG::ValueType::Integer, field("jour_mois"), "De 1 \xC3\xA0 28."));
        c.properties.push_back(prop("Format", r->format, PG::ValueType::Enum, field("format"),
                                    "PDF : la synth\xC3\xA8se, un graphique, les tableaux. Excel : une feuille par partie.", {"PDF", "Excel"}));
        {   // 1.25.0 : sa mise en page - celle par defaut, celles du projet, les fournies
            const auto& p = doc_->project;
            const std::string def = "(par d\xC3\xA9" "faut : " + hmi::report::defaultLayoutName(p) + ")";
            std::vector<std::string> choices{def};
            for (const auto& l : p.reportLayouts) choices.push_back(l.name);
            for (const auto& l : hmi::report::builtInLayouts()) choices.push_back(l.name);
            const std::string value = r->layout.empty() || !hmi::report::findLayout(p, r->layout) ? def : hmi::report::findLayout(p, r->layout)->name;
            c.properties.push_back(prop("Mise en page", value, PG::ValueType::Enum, field("mise_en_page"),
                                        "La page, l'en-t\xC3\xAAte, le pied, les parties et leur ordre : l'onglet Mise en page. Celle que prennent les "
                                        "\xC3\xA9" "critures \xC3\xA0 l'heure dite ; G\xC3\xA9n\xC3\xA9rer maintenant\xE2\x80\xA6 en propose une autre.",
                                        choices));
        }
        c.properties.push_back(prop("Actif", tf(r->enabled), PG::ValueType::Boolean, field("actif"), "D\xC3\xA9" "coch\xC3\xA9 : il ne part plus tout seul."));
        c.properties.push_back(prop("Prochain", hosts_.next ? hosts_.next(r->id) : std::string{}, PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        PG::Category content;
        content.name = "Contenu recueilli (l'ordre, ce qui s'imprime : la mise en page)";
        content.properties.push_back(prop("Alarmes", tf(r->alarms), PG::ValueType::Boolean, field("alarmes"),
                                          "Chaque apparition de la p\xC3\xA9riode (acquitt\xC3\xA9" "e par qui, sa dur\xC3\xA9" "e), puis les plus fr\xC3\xA9quentes."));
        content.properties.push_back(prop("Mesures", r->measures, PG::ValueType::Text, field("mesures"),
                                          "Pression; D\xC3\xA9" "bit : minimum, moyenne, maximum (les variables archiv\xC3\xA9" "es, Configuration > Historiques). Vide : toutes."));
        content.properties.push_back(prop("Production", tf(r->production), PG::ValueType::Boolean, field("production"),
                                          "Les compteurs de production : bons, rebuts, cadence, TRS."));
        content.properties.push_back(prop("\xC3\x89v\xC3\xA9nements", tf(r->events), PG::ValueType::Boolean, field("evenements"),
                                          "Les connexions, les recettes, les \xC3\xA9" "critures..."));
        cats.push_back(std::move(content));
        PG::Category send;
        send.name = "Envoi";
        send.properties.push_back(prop("Destinataires", r->recipients, PG::ValueType::Text, field("destinataires"),
                                       "Des destinataires des notifications (leurs noms) : le rapport part en pi\xC3\xA8" "ce jointe."));
        cats.push_back(std::move(send));
    }
    grid_->setCategories(std::move(cats));
}

void HmiReportsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    // 1.25.0 : l'onglet Mise en page prend toute la largeur (ses reglages sont les siens).
    const bool wide = tabs_->currentIndex() == 2;
    const float gridW = wide ? 0.f : std::min(460.f, b.w * 0.32f);
    grid_->setVisibility(wide ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - (wide ? 0.f : 4.f)), h});
    if (!wide) grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
    sheet_->setBounds(b);
    sheet_->setVisibility(sheet_->isOpen() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
}

void HmiReportsPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    if (ctx.time - lastLive_ >= 5.0) {       // le prochain depart, les fichiers ecrits
        lastLive_ = ctx.time;
        refreshFiles();
    }
}

} // namespace app
