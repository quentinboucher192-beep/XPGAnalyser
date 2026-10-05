// Configuration > Rapports (lot 14).
#include "HmiReportPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "../ExportTarget.hpp"          // lot API 8 : Generer demande ou
#include "../../hmi/HmiReports.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
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
    tools->add(CNow, HmiGlyph::Play, "G\xC3\xA9n\xC3\xA9rer maintenant : la p\xC3\xA9riode en cours, jusqu'\xC3\xA0 maintenant (demande o\xC3\xB9 : exports/ propos\xC3\xA9)", "G\xC3\xA9n\xC3\xA9rer maintenant");
    tools->add(CLast, HmiGlyph::Refresh, "La derni\xC3\xA8re p\xC3\xA9riode compl\xC3\xA8te (hier, la semaine pass\xC3\xA9" "e...) : demande o\xC3\xB9 (exports/ propos\xC3\xA9)", "Derni\xC3\xA8re p\xC3\xA9riode");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(CRemove, [this] { return !selectedReport().empty(); });
    tools_->setEnabledWhen(CNow, [this] { return !selectedReport().empty() && hosts_.write; });
    tools_->setEnabledWhen(CLast, [this] { return !selectedReport().empty() && hosts_.write; });

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto table = std::make_unique<ui::TableView>(base + ".reports");
    table->setColumns({{"Rapport", 190.f}, {"Quand", 230.f}, {"Format", 70.f}, {"Contenu", 280.f}, {"Destinataires", 200.f}, {"Prochain", 140.f},
                       {"Actif", 60.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    auto files = std::make_unique<ui::TableView>(base + ".files");
    files->setColumns({{"Fichier", 380.f}, {"\xC3\x89" "crit le", 160.f}, {"Taille", 90.f}});
    table_ = static_cast<ui::TableView*>(table.get());
    files_ = static_cast<ui::TableView*>(files.get());
    tabs->addTab({"Rapports", ui::Icon::Document}, std::move(table));
    tabs->addTab({"\xC3\x89" "crits", ui::Icon::Document}, std::move(files));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case CAdd: (void)addReport(); break;
            case CRemove: if (!selectedReport().empty()) (void)removeReport(selectedReport()); break;
            case CNow: case CLast: {
                // ---- Lot API 8 : les exports qui demandent ou ----
                // OU l'ecrire se demande, comme les autres exports d'un clic
                // (ExportTarget.hpp) : exports/ propose (Exporter : comme avant),
                // le bouton ... ailleurs. Les rapports PROGRAMMES (a l'heure dite)
                // partent seuls : exports/, sans question.
                const std::string name = selectedReport();
                if (name.empty()) break;
                const bool current = a == CNow;
                const auto* r = doc_->project.reportByName(name);
                const bool excel = r && r->format == "Excel";
                const auto go = [this, name, current] { (void)generate(name, current); };
                if (!askExportTarget("le rapport " + name + (excel ? " (Excel)" : " (PDF)"), excel ? "Classeurs Excel|*.xlsx" : "Documents PDF|*.pdf", go))
                    go();
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
        rows.push_back({r.name, whenText(r), r.format, contentText(r), r.recipients.empty() ? std::string("\xE2\x80\x94") : r.recipients, next,
                        r.enabled ? "oui" : "non"});
        on.push_back(r.enabled);
    }
    tableModel_ = std::make_shared<Rows>(std::vector<std::string>{"Rapport", "Quand", "Format", "Contenu", "Destinataires", "Prochain", "Actif"},
                                         std::move(rows), [on](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle st;
                                             if (r >= on.size()) return st;
                                             if (c == 0) {
                                                 st.bold = true;
                                                 st.icon = ui::Icon::Document;
                                             }
                                             if (c == 5) st.fgTone = on[r] ? ui::Tone::Accent : ui::Tone::None;
                                             return st;
                                         });
    table_->setModel(tableModel_);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], keep)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    tabs_->setTabBadge(0, std::to_string(order_.size()), ui::Tone::Accent);
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
    const auto* r = p.report(id);
    if (!r) {
        if (where) *where = "rapport introuvable";
        return false;
    }
    namespace rp = hmi::report;
    const double wall = hmi::wallEpoch();
    const double to = current ? wall + 1.0 : rp::lastDue(*r, wall);   // maintenant : ce qui vient d'arriver compris
    const double from = current ? rp::periodStart(*r, rp::nextDue(*r, wall)) : rp::periodStart(*r, to);
    const auto content = rp::build(p, h, *r, from, to, {}, hmi::wallStamp());
    const auto format = r->format == "Excel" ? hmi::ExportFormat::Excel : hmi::ExportFormat::Pdf;
    hmi::ExportRequest rq;
    rq.fileName = rp::fileName(*r, to, format);
    rq.format = std::string(hmi::exportFormatLabel(format));
    rq.source = "rapport:" + r->name;
    for (const auto& t : content.sections) rq.rows += t.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(rp::render(content, format));
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
        output->recipients = r->recipients;
    }
    return ok;
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
        c.properties.push_back(prop("Actif", tf(r->enabled), PG::ValueType::Boolean, field("actif"), "D\xC3\xA9" "coch\xC3\xA9 : il ne part plus tout seul."));
        c.properties.push_back(prop("Prochain", hosts_.next ? hosts_.next(r->id) : std::string{}, PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        PG::Category content;
        content.name = "Contenu";
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
    const float gridW = std::min(460.f, b.w * 0.32f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiReportsPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    if (ctx.time - lastLive_ >= 5.0) {       // le prochain depart, les fichiers ecrits
        lastLive_ = ctx.time;
        refreshFiles();
    }
}

} // namespace app
