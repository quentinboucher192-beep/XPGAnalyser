// IHM > Programmation generale > Variables systeme / Variables d'instances (lot 9).
// Lot 7 : rangees en dossiers (les domaines ; les vues, puis leurs objets).
#include "HmiPublicVarsPane.hpp"

#include "HmiIcons.hpp"
#include "HmiPanels.hpp"
#include "HmiPaneKit.hpp"

#include "../../hmi/HmiAlarmGroups.hpp"   // 1.11.1 : le groupe de l'IHM lie
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <map>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {

enum PublicAction : int { PCopy = 1, PRefresh, PHelp, PExpandAll, PCollapseAll };

// Lot 7 : le retrait d'un niveau de dossier (la fleche d'un dossier y tient).
constexpr float kFolderIndent = 16.f;

// Le modele d'un tableau : les lignes montrees - des dossiers (lot 7) et, sous
// ceux qui sont deplies, les variables qui passent le filtre.
class PublicRows final : public ui::ITableModel {
public:
    struct Line {
        std::string cells[HmiPublicVarsPane::ColumnCount];
        bool        writable{false}, live{false};
        // Lot 7 : un dossier (sa fleche, son compte en pastille), ou une variable en retrait.
        bool        folder{false}, open{false};
        int         depth{0};
        std::string badge;
        bool        structure{false};   // 1.9 : un dossier SYS.Slave.<nom> (une structure d'esclave)
        bool        member{false};      // 1.9 : un membre de cette structure (montre ".Name" sous elle)
        bool        tail{false};        // 1.11.1 (R1111-8) : sous le groupe d'alarmes ou une alarme (montre ".Acked")
    };
    explicit PublicRows(std::vector<Line> lines) : lines_(std::move(lines)) {}
    [[nodiscard]] std::size_t rowCount() const override { return lines_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return HmiPublicVarsPane::ColumnCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kHeaders[] = {"Chemin", "Appartient \xC3\xA0", "Type", "Acc\xC3\xA8s", "Valeur", "Description"};
        return c < HmiPublicVarsPane::ColumnCount ? kHeaders[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= lines_.size() || c >= HmiPublicVarsPane::ColumnCount) return {};
        const auto& l = lines_[r];
        if (l.member) {
            // 1.9 : sous sa structure, un membre se lit ".Name" (le chemin complet est
            // dans l'infobulle et se copie) ; son domaine est celui de la structure.
            if (c == HmiPublicVarsPane::Owner) return {};
            if (c == HmiPublicVarsPane::Path) {
                const auto dot = l.cells[c].rfind('.');
                if (dot != std::string::npos) return l.cells[c].substr(dot);
            }
        }
        if (l.tail && c == HmiPublicVarsPane::Path) {
            // 1.11.1 (R1111-8) : sous « Groupe d'alarmes » ou sous une alarme, le chemin entier
            // ("Vue_Synoptique.V_101.Alarmes.Course_Trop_Longue.Acked") etait coupe avant son
            // membre : on lit ".Acked", comme l'arbre du projet. Le chemin entier reste dans
            // l'infobulle, la copie et la recherche.
            const auto dot = l.cells[c].rfind('.');
            if (dot != std::string::npos) return l.cells[c].substr(dot);
        }
        return l.cells[c];
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= lines_.size()) return s;
        const auto& l = lines_[r];
        if (l.folder) {
            // Un dossier : son titre court sur toute la ligne, sa fleche, son compte.
            if (c == HmiPublicVarsPane::Path) {
                s.bold = true;
                s.spanRow = true;
                s.expander = l.open ? 1 : 0;
                s.indent = kFolderIndent * static_cast<float>(l.depth);
                s.icon = l.open ? ui::Icon::FolderOpen : ui::Icon::Folder;
                s.iconTone = ui::Tone::Accent;
                s.badge = l.badge;
                if (l.structure) {
                    // 1.9 : une structure d'esclave simule - son chemin, en chasse fixe.
                    s.monospace = true;
                    s.icon = ui::Icon::DerivedType;
                }
            }
            return s;
        }
        switch (c) {
            case HmiPublicVarsPane::Path:
                s.monospace = true;
                s.indent = kFolderIndent * static_cast<float>(l.depth);
                s.icon = l.writable ? ui::Icon::Variable : ui::Icon::Lock;
                s.iconTone = l.writable ? ui::Tone::Ok : ui::Tone::Muted;
                break;
            case HmiPublicVarsPane::Access:
                s.bold = true;
                s.fgTone = l.writable ? ui::Tone::Ok : ui::Tone::Muted;
                break;
            case HmiPublicVarsPane::Type: s.fgTone = ui::Tone::Accent; break;
            case HmiPublicVarsPane::Value:
                s.monospace = true;
                if (l.live) s.bold = true;
                else s.fgTone = ui::Tone::Muted;
                break;
            case HmiPublicVarsPane::About: s.fgTone = ui::Tone::Muted; break;
            default: break;
        }
        return s;
    }
    // Lot 7 : les colonnes ne trient plus (les titres ne sont pas cliquables) :
    // un tri melerait les dossiers et leurs lignes. L'ordre est celui des domaines,
    // puis des vues et de leurs objets.
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    // Lot 7 : l'infobulle d'une ligne - une variable avec sa valeur (en marche,
    // celle du moment : la table relit l'infobulle tant qu'elle est ouverte,
    // setValue la change deux fois par seconde) ; un dossier, son compte.
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= lines_.size()) return {};
        const auto& l = lines_[r];
        if (l.folder && l.structure)
            return l.cells[HmiPublicVarsPane::Path] + " : la structure d'un esclave simul\xC3\xA9, " + l.badge + " membres en lecture\n"
                 + "Une structure par esclave simul\xC3\xA9 : SYS.Slave.<nom> (le nom de l'\xC3\xA9quipement, espaces et accents remplac\xC3\xA9s par _).";
        if (l.folder)
            return l.cells[HmiPublicVarsPane::Path] + " : " + l.badge + " variable(s)\n"
                 + (l.open ? "La fl\xC3\xA8" "che (ou un double-clic) le replie." : "La fl\xC3\xA8" "che (ou un double-clic) le d\xC3\xA9plie.");
        std::string tip = l.cells[HmiPublicVarsPane::Path] + " : " + l.cells[HmiPublicVarsPane::Type] + " ("
                        + l.cells[HmiPublicVarsPane::Access] + ")";
        if (!l.cells[HmiPublicVarsPane::About].empty()) tip += "\n" + l.cells[HmiPublicVarsPane::About];
        const auto& value = l.cells[HmiPublicVarsPane::Value];
        if (!value.empty() && value != "-") tip += "\nValeur : " + value + (l.live ? "  (IHM en marche)" : "");
        tip += "\nDouble-clic : copier le chemin.";
        return tip;
    }
    // En marche : les valeurs changent, les lignes restent (le defilement et le choix aussi).
    void setValue(std::size_t row, std::string value, bool live) {
        if (row >= lines_.size() || lines_[row].folder) return;
        lines_[row].cells[HmiPublicVarsPane::Value] = std::move(value);
        lines_[row].live = live;
    }

private:
    std::vector<Line> lines_;
};

std::string lowerCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Le chemin d'un dossier : ses cles jusqu'au niveau `depth` compris ("Vue_A/Curseur").
template <class Folders>
std::string folderPath(const Folders& folders, std::size_t depth) {
    std::string p;
    for (std::size_t d = 0; d <= depth && d < folders.size(); ++d) {
        if (d) p += '/';
        p += folders[d].first;
    }
    return p;
}

} // namespace

HmiPublicVarsPane::HmiPublicVarsPane(std::string id, hmi::DocumentPtr doc) : ui::Widget(std::move(id)), doc_(std::move(doc)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(PCopy, HmiGlyph::Copy, "Copier le chemin de la ligne choisie (\xC3\xA0 coller dans un script, une expression, un texte \xC3\xA0 trous)",
               "Copier le chemin");
    tools->add(PRefresh, HmiGlyph::Refresh, "Relire les valeurs (celles de l'IHM en marche, sinon celles de l'\xC3\xA9" "diteur)", "Relire");
    tools->separator();
    // Lot 7 : les dossiers de l'onglet montre, tous d'un coup.
    tools->add(PExpandAll, HmiGlyph::Down, "D\xC3\xA9plier tous les dossiers de l'onglet", "Tout d\xC3\xA9plier");
    tools->add(PCollapseAll, HmiGlyph::Up, "Replier tous les dossiers de l'onglet", "Tout replier");
    tools->separator();
    tools->add(PHelp, HmiGlyph::Help, "L'aide de ces variables", "Aide");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(PCopy, [this] { return !selectedPath().empty(); });

    auto search = std::make_unique<ui::InputText>(base + ".search");
    search->setPlaceholder("Rechercher (chemin, objet, domaine, description)...");
    searchBox_ = &static_cast<ui::InputText&>(addChild(std::move(search)));

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    const char* titles[] = {"Variables syst\xC3\xA8me", "Variables d'instances"};
    const ui::Icon icons[] = {ui::Icon::Settings, ui::Icon::Screen};
    for (int t = 0; t < 2; ++t) {
        auto table = std::make_unique<ui::TableView>(base + ".table" + std::to_string(t));
        // Lot 7 : sans tri par les titres (voir PublicRows::less) - les dossiers
        // gardent leurs lignes.
        table->setColumns({{"Chemin", t == 0 ? 280.f : 360.f, 40.f, true, false},
                           {t == 0 ? "Domaine" : "Appartient \xC3\xA0", 170.f, 40.f, true, false},
                           {"Type", 70.f, 40.f, true, false},
                           {"Acc\xC3\xA8s", 60.f, 40.f, true, false},
                           {"Valeur", 180.f, 40.f, true, false},
                           {"Description", 520.f, 40.f, true, false}});
        table->setSelectionMode(ui::SelectionMode::Single);
        table->setTooltip(t == 0 ? "Les variables syst\xC3\xA8me, rang\xC3\xA9" "es par domaine : la fl\xC3\xA8" "che (ou un double-clic) d\xC3\xA9plie un dossier ; "
                                   "un double-clic sur une variable copie son chemin."
                                 : "Les variables d'instances, rang\xC3\xA9" "es par vue puis par objet : la fl\xC3\xA8" "che (ou un double-clic) d\xC3\xA9plie un dossier ; "
                                   "un double-clic sur une variable copie son chemin.");
        tables_[t] = table.get();
        tabs->addTab(ui::TabControl::Tab{titles[t], icons[t], false, false}, std::move(table));
    }
    tabs->setCurrentIndex(0);
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case PCopy: {
                const auto path = selectedPath();
                if (path.empty()) break;
                ui::setClipboardText(path);
                say("Copi\xC3\xA9 : " + path);
                break;
            }
            case PRefresh: refresh(); break;
            case PExpandAll:
            case PCollapseAll:
                setAllFoldersOpen(currentTab(), a == PExpandAll);
                break;
            case PHelp:
                if (hosts_.help) hosts_.help(currentTab() == System ? "variables-systeme" : "variables-instances");
                break;
            default: break;
        }
    });
    links_ += searchBox_->textChanged->connect([this](const std::string& t) {
        searchText_ = t;
        for (auto& closed : searchClosed_) closed.clear();   // une autre recherche : ses dossiers s'ouvrent
        applyFilter();
        for (auto* table : tables_) table->setScrollOffset(0.f);   // d'autres lignes : depuis le haut
    });
    for (int t = 0; t < 2; ++t) {
        // Un double-clic (ou Entree) : un dossier se deplie ou se replie ; une
        // variable : son chemin dans le presse-papiers.
        links_ += tables_[t]->activated->connect([this, t](ui::RowIndex row) {
            if (const auto* l = lineAt(t, row); l && l->folder) {
                toggleLine(t, row);
                return;
            }
            const auto path = selectedPath();
            if (path.empty()) return;
            ui::setClipboardText(path);
            say("Copi\xC3\xA9 : " + path);
        });
        // Lot 7 : la fleche d'un dossier (et les fleches gauche / droite).
        links_ += tables_[t]->expanderClicked->connect([this, t](ui::RowIndex row) { toggleLine(t, row); });
    }
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiPublicVarsPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiPublicVarsPane::showTab(int tab) {
    tabs_->setCurrentIndex(tab == Instances ? 1u : 0u);
    // En marche : l'onglet qu'on montre a ses valeurs du moment.
    if (live_) {
        updateValues(false);
        applyFilter();
    }
}
int HmiPublicVarsPane::currentTab() const { return tabs_->currentIndex() == 1 ? Instances : System; }

void HmiPublicVarsPane::setSearch(std::string text) {
    searchText_ = std::move(text);
    for (auto& closed : searchClosed_) closed.clear();
    searchBox_->setText(searchText_);
    applyFilter();
    for (auto* table : tables_) table->setScrollOffset(0.f);
}

void HmiPublicVarsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 6.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiPublicVarsPane::refresh() {
    rebuildRows();
    updateValues(true);
    applyFilter();
}

void HmiPublicVarsPane::rebuildRows() {
    for (auto& a : all_) a.clear();
    // ---- les variables systeme, par domaine (lot 7 : un dossier chacun)
    for (const auto& v : hmi::pub::kSysVars) {
        if (!hmi::pub::sysDomainShown(v.domain)) continue;      // 1.12.0 : XPGAnalyser IHM - ni Automate, ni Communication
        Row r;
        r.cells[Path] = "SYS." + std::string(v.name);
        r.cells[Owner] = std::string(hmi::pub::kSysDomains[v.domain]);
        r.cells[Type] = std::string(v.type);
        r.cells[Access] = std::string(hmi::pub::accessLabel(hmi::pub::sysAccess(v)));   // lot 13 : SYS.Language R/W
        r.cells[About] = std::string(v.text);
        r.writable = hmi::pub::sysAccess(v) == hmi::pub::Access::ReadWrite;
        r.folders.emplace_back(r.cells[Owner], r.cells[Owner]);
        all_[0].push_back(std::move(r));
    }
    // 1.9 : une structure par esclave simule - SYS.Slave.<nom>, seize membres en
    // lecture, un dossier chacune sous le domaine Esclaves simules.
    {
        const std::string domain(hmi::pub::kSysDomains[hmi::pub::kSlaveDomain]);
        for (const auto* e : hmi::pub::slaveEquipments(doc_->project)) {
            const std::string root = "SYS." + std::string(hmi::pub::kSlaveRoot) + "." + hmi::slaveKey(e->name);
            for (const auto& m : hmi::pub::kSlaveMembers) {
                Row r;
                r.cells[Path] = root + "." + std::string(m.name);
                r.cells[Owner] = domain;
                r.cells[Type] = std::string(m.type);
                r.cells[Access] = std::string(hmi::pub::accessLabel(m.access));
                r.cells[About] = std::string(m.text);
                r.member = true;
                r.folders = {{domain, domain}, {root, root}};
                all_[0].push_back(std::move(r));
            }
        }
    }
    // Les domaines dans leur ordre (celui de l'arbre), meme si la liste les mele.
    std::stable_sort(all_[0].begin(), all_[0].end(), [](const Row& a, const Row& b) {
        const auto rank = [](const Row& r) {
            for (std::size_t d = 0; d < hmi::pub::kSysDomainCount; ++d)
                if (r.cells[Owner] == hmi::pub::kSysDomains[d]) return d;
            return hmi::pub::kSysDomainCount;
        };
        return rank(a) < rank(b);
    });
    // ---- les vues, leurs objets et ce qu'ils publient (lot 7 : la vue, puis
    // "Variables de la vue" ou l'objet)
    static const std::string kViewVars = "Variables de la vue";
    const auto& p = doc_->project;
    for (const auto& v : p.views) {
        const std::string role = std::string(hmi::viewRoleLabel(v.role));
        const std::pair<std::string, std::string> viewFolder{v.name, v.name + " (" + role + ")"};
        for (const auto& m : hmi::pub::viewMembers(v)) {
            Row r;
            r.cells[Path] = v.name + "." + m.name;
            r.cells[Owner] = v.name + " (" + role + ")";
            r.cells[Type] = m.type;
            r.cells[Access] = std::string(hmi::pub::accessLabel(m.access));
            r.cells[Value] = m.value;
            r.cells[About] = m.text;
            r.writable = m.access == hmi::pub::Access::ReadWrite;
            r.folders = {viewFolder, {kViewVars, kViewVars}};
            all_[1].push_back(std::move(r));
        }
        for (const auto& o : v.objects) {
            const std::string owner = o.name + " (" + std::string(hmi::kindLabel(o.kind)) + ")";
            for (const auto& m : hmi::pub::objectMembers(o)) {
                Row r;
                r.cells[Path] = hmi::pub::instancePath(v, o, m.name);
                r.cells[Owner] = owner;
                r.cells[Type] = m.type;
                r.cells[Access] = std::string(hmi::pub::accessLabel(m.access));
                r.cells[Value] = m.value;
                std::string label, help;
                if (!m.text.empty()) r.cells[About] = m.text;
                else if (hmiPropertyInfo(m.key, label, help))
                    r.cells[About] = "\xC2\xAB " + label + " \xC2\xBB dans l'inspecteur" + (help.empty() ? std::string{} : " : " + help);
                else
                    r.cells[About] = "la propri\xC3\xA9t\xC3\xA9 " + m.key;
                r.writable = m.access == hmi::pub::Access::ReadWrite;
                r.folders = {viewFolder, {o.name, owner}};
                all_[1].push_back(std::move(r));
            }
            // 1.11.1 (decision 108) : tout ce que l'objet publie - les parametres d'une
            // instance (Vue.Pompe_3.Armoire), son groupe d'alarmes, puis ses alarmes.
            for (const auto& ip : hmi::pub::instanceParams(p, o)) {
                Row r;
                r.cells[Path] = hmi::pub::instancePath(v, o, ip.name);
                r.cells[Owner] = owner;
                r.cells[Type] = ip.type;
                r.cells[Access] = std::string(hmi::pub::accessLabel(hmi::pub::Access::Read));
                r.cells[Value] = ip.argument.empty() ? std::string{} : "= " + ip.argument;
                r.cells[About] = std::string("Param\xC3\xA8tre de l'instance : ce qu'il relie") + (ip.given ? "" : " (la valeur par d\xC3\xA9" "faut du symbole)")
                               + (ip.description.empty() ? std::string{} : " ; " + ip.description) + " ; \xC3\xA9" "cris la variable reli\xC3\xA9" "e";
                r.folders = {viewFolder, {o.name, owner}};
                all_[1].push_back(std::move(r));
            }
            if (!hmi::pub::hasAlarmGroup(o)) continue;
            const std::string group = hmi::objectGroupOf(v, o);
            std::string symbols;
            if (o.kind == hmi::Kind::SymbolInstance)
                if (const auto* sp = o.find("symbol")) symbols = sp->value;
            const auto* link = hmi::alarmGroupLinkOf(p, group, symbols);
            static const std::string kGroup = "Groupe d'alarmes";
            for (const auto& i : hmi::pub::kAlarmInfo) {
                Row r;
                r.cells[Path] = hmi::pub::instancePath(v, o, i.name);
                r.cells[Owner] = owner;
                r.cells[Type] = std::string(i.type);
                r.cells[Access] = std::string(hmi::pub::accessLabel(i.access));
                if (hmi::pub::same(i.name, "AlarmGroup")) r.cells[Value] = group;
                else if (hmi::pub::same(i.name, "AlarmLinkedGroup")) r.cells[Value] = link ? link->group : std::string{};
                r.cells[About] = std::string(i.text);
                r.tail = true;   // R1111-8
                r.folders = {viewFolder, {o.name, owner}, {kGroup, kGroup + " " + group + (link ? " \xE2\x86\x92 " + link->group : std::string{})}};
                all_[1].push_back(std::move(r));
            }
            static const std::string kAlarms(hmi::pub::kAlarmsRoot);
            for (const auto& a : hmi::pub::objectAlarmNames(p, v, o)) {
                for (const auto& m : hmi::pub::kAlarmMembers) {
                    Row r;
                    r.cells[Path] = v.name + "." + o.name + "." + kAlarms + "." + a.local + "." + std::string(m.name);
                    r.cells[Owner] = owner;
                    r.cells[Type] = std::string(m.type);
                    r.cells[Access] = std::string(hmi::pub::accessLabel(m.access));
                    const std::string_view n = m.name;
                    if (n == "Name") r.cells[Value] = a.full;
                    else if (n == "Enabled") r.cells[Value] = a.enabled ? "TRUE" : "FALSE";
                    else if (n == "Priority") r.cells[Value] = std::to_string(a.priority);
                    else if (n == "Message") r.cells[Value] = a.message;
                    else if (n == "Group") r.cells[Value] = link ? link->group : a.group;
                    r.cells[About] = std::string(m.text);
                    r.writable = m.access == hmi::pub::Access::ReadWrite;
                    r.tail = true;   // R1111-8
                    r.folders = {viewFolder, {o.name, owner}, {kAlarms, kAlarms}, {a.local, a.local + (a.enabled ? std::string{} : std::string(" (d\xC3\xA9" "coch\xC3\xA9" "e)"))}};
                    all_[1].push_back(std::move(r));
                }
            }
        }
    }
}

void HmiPublicVarsPane::updateValues(bool both) {
    live_ = false;
    std::string probe;
    if (hosts_.live && hosts_.live("SYS.Running", probe)) live_ = probe == "TRUE" || probe == "1";
    if (!live_) {
        // Les valeurs de l'editeur : rebuildRows les a posees (rien pour SYS).
        for (auto& r : all_[0]) { r.cells[Value] = "-"; r.live = false; }
        return;
    }
    // L'onglet affiche seulement (des milliers de lectures pour rien sinon),
    // ou les deux quand les lignes viennent d'etre refaites.
    for (int t = 0; t < 2; ++t) {
        if (!both && t != (currentTab() == Instances ? 1 : 0)) continue;
        for (auto& r : all_[t]) {
            std::string out;
            if (hosts_.live(r.cells[Path], out)) {
                r.cells[Value] = out;
                r.live = true;
            }
        }
    }
}

void HmiPublicVarsPane::applyFilter() {
    // Lot recherche : la recherche de toutes les listes - chaque mot (ou "phrase")
    // dans le chemin, l'objet, le type, la DESCRIPTION ou un dossier ; aucun
    // -mot exclu ; sans casse ni accents.
    const ui::SearchQuery query(searchText_);
    for (int t = 0; t < 2; ++t) {
        tables_[t]->setHighlight(searchText_);
        shown_[t].clear();
        for (std::size_t i = 0; i < all_[t].size(); ++i) {
            const auto& r = all_[t][i];
            if (!query.empty()) {
                // Lot 7 : les dossiers aussi (le nom d'une vue, d'un objet, d'un domaine).
                std::vector<std::string_view> texts{r.cells[Path], r.cells[Owner], r.cells[Type], r.cells[About]};
                for (const auto& f : r.folders) texts.emplace_back(f.second);
                if (!query.matches(texts)) continue;
            }
            shown_[t].push_back(i);
        }
        rebuildLines(t);
        if (t == 0) {
            // 1.9 : "207 + 3" - les variables, plus les structures d'esclaves simules.
            std::size_t plain = 0;
            std::set<std::string> structures;
            for (const auto i : shown_[0]) {
                if (!all_[0][i].member) ++plain;
                else if (all_[0][i].folders.size() > 1) structures.insert(all_[0][i].folders[1].first);
            }
            tabs_->setTabBadge(0, std::to_string(plain) + (structures.empty() ? std::string{} : " + " + std::to_string(structures.size())),
                               ui::Tone::Accent);
            continue;
        }
        tabs_->setTabBadge(static_cast<std::size_t>(t), std::to_string(shown_[t].size()), ui::Tone::Accent);
    }
    std::size_t writable = 0, objects = 0, plainSys = 0;
    for (const auto& r : all_[1]) writable += r.writable;
    for (const auto& r : all_[0]) plainSys += r.member ? 0 : 1;
    for (const auto& v : doc_->project.views) objects += v.objects.size();
    const std::size_t slaves = hmi::pub::slaveEquipments(doc_->project).size();
    std::string msg = std::to_string(plainSys) + " variables syst\xC3\xA8me (lecture seule) \xC2\xB7 "
                    + (slaves > 0 ? std::to_string(slaves) + " structure" + (slaves > 1 ? "s" : "") + " d'esclave" + (slaves > 1 ? "s" : "")
                                        + " (" + std::to_string(hmi::pub::kSlaveMemberCount) + " membres chacune) \xC2\xB7 "
                                  : std::string{})
                    + std::to_string(all_[1].size())
                    + " variables d'instances (" + std::to_string(doc_->project.views.size()) + " vues, " + std::to_string(objects)
                    + " objets ; " + std::to_string(writable) + " en R/W) \xC2\xB7 ";
    // La note du bas : comment se nomme une structure d'esclave.
    const bool note = slaves > 0 && currentTab() == System;
    if (note != slaveNote_) {
        slaveNote_ = note;
        invalidateLayout();
    }
    msg += live_ ? "valeurs de l'IHM en marche" : "valeurs de l'\xC3\xA9" "diteur (lance la simulation pour les voir en marche)";
    status_->setMessage(msg);
    invalidate();
}

// ---- lot 7 : les dossiers -----------------------------------------------------
//  Replies au depart. Pendant une recherche, tous ceux qui gardent une ligne
//  sont deplies - sauf ceux qu'on replie pendant cette recherche.
bool HmiPublicVarsPane::folderOpen(int tab, const std::string& folder) const {
    const int t = tab == Instances ? 1 : 0;
    const auto key = lowerCopy(folder);
    return searchText_.empty() ? open_[t].count(key) > 0 : searchClosed_[t].count(key) == 0;
}

void HmiPublicVarsPane::setFolderOpen(int tab, const std::string& folder, bool open) {
    const int t = tab == Instances ? 1 : 0;
    const auto key = lowerCopy(folder);
    if (open) {
        open_[t].insert(key);
        searchClosed_[t].erase(key);
    } else {
        open_[t].erase(key);
        if (!searchText_.empty()) searchClosed_[t].insert(key);
    }
    rebuildLines(t);
}

void HmiPublicVarsPane::setAllFoldersOpen(int tab, bool open) {
    const int t = tab == Instances ? 1 : 0;
    for (const auto& r : all_[t])
        for (std::size_t d = 0; d < r.folders.size(); ++d) {
            const auto key = lowerCopy(folderPath(r.folders, d));
            if (open) {
                open_[t].insert(key);
                searchClosed_[t].erase(key);
            } else {
                open_[t].erase(key);
                if (!searchText_.empty()) searchClosed_[t].insert(key);
            }
        }
    rebuildLines(t);
}

std::size_t HmiPublicVarsPane::foldersIn(int tab) const {
    const int t = tab == Instances ? 1 : 0;
    std::set<std::string> seen;
    for (const auto i : shown_[t])
        for (std::size_t d = 0; d < all_[t][i].folders.size(); ++d) seen.insert(lowerCopy(folderPath(all_[t][i].folders, d)));
    return seen.size();
}

std::size_t HmiPublicVarsPane::tableRowsIn(int tab) const { return lines_[tab == Instances ? 1 : 0].size(); }

const HmiPublicVarsPane::Line* HmiPublicVarsPane::lineAt(int t, ui::RowIndex line) const {
    const auto& lines = lines_[t == Instances ? 1 : 0];
    return line < lines.size() ? &lines[line] : nullptr;
}

void HmiPublicVarsPane::toggleLine(int t, ui::RowIndex line) {
    const auto* l = lineAt(t, line);
    if (!l || !l->folder) return;
    setFolderOpen(t == 1 ? Instances : System, l->path, !l->open);
}

void HmiPublicVarsPane::rebuildLines(int tab) {
    const int t = tab == Instances ? 1 : 0;
    // Ce qui est choisi (une variable, un dossier) et le defilement restent.
    std::string keepPath;
    bool keepFolder = false;
    if (const auto sel = tables_[t]->selectedModelRows(); !sel.empty())
        if (const auto* l = lineAt(t, sel.front())) {
            keepFolder = l->folder;
            keepPath = l->folder ? l->path : (l->row < all_[t].size() ? all_[t][l->row].cells[Path] : std::string{});
        }
    const float scroll = tables_[t]->scrollOffset();

    // Combien de variables chaque dossier en a, et combien il en montre.
    std::map<std::string, std::size_t> total, shown;
    for (const auto& r : all_[t])
        for (std::size_t d = 0; d < r.folders.size(); ++d) ++total[lowerCopy(folderPath(r.folders, d))];
    for (const auto i : shown_[t])
        for (std::size_t d = 0; d < all_[t][i].folders.size(); ++d) ++shown[lowerCopy(folderPath(all_[t][i].folders, d))];

    auto& lines = lines_[t];
    lines.clear();
    std::vector<std::string> chain;      // les dossiers deja poses, du haut vers le bas
    for (const auto i : shown_[t]) {
        const auto& r = all_[t][i];
        bool visible = true;             // tous ses dossiers au-dessus sont deplies
        for (std::size_t d = 0; d < r.folders.size(); ++d) {
            const std::string path = folderPath(r.folders, d);
            const std::string key = lowerCopy(path);
            const bool open = folderOpen(t == 1 ? Instances : System, path);
            if (chain.size() <= d || chain[d] != key) {
                chain.resize(d);
                chain.push_back(key);
                if (visible) {
                    Line f;
                    f.folder = true;
                    f.path = path;
                    f.label = r.folders[d].second;
                    f.depth = static_cast<int>(d);
                    f.shown = shown[key];
                    f.total = total[key];
                    f.open = open;
                    lines.push_back(std::move(f));
                }
            }
            visible = visible && open;
        }
        if (!visible) continue;
        Line v;
        v.row = i;
        v.depth = static_cast<int>(r.folders.size());
        lines.push_back(std::move(v));
    }

    std::vector<PublicRows::Line> model;
    model.reserve(lines.size());
    for (const auto& l : lines) {
        PublicRows::Line m;
        m.folder = l.folder;
        m.open = l.open;
        m.depth = l.depth;
        if (l.folder) {
            m.cells[Path] = l.label;
            m.structure = t == 0 && l.depth == 1 && l.label.rfind("SYS.", 0) == 0;      // 1.9 : SYS.Slave.<nom>
            // Combien il en montre ; sur combien quand la recherche en cache.
            m.badge = l.shown == l.total ? std::to_string(l.total) : std::to_string(l.shown) + " / " + std::to_string(l.total);
        } else {
            const auto& r = all_[t][l.row];
            for (std::size_t c = 0; c < ColumnCount; ++c) m.cells[c] = r.cells[c];
            m.writable = r.writable;
            m.live = r.live;
            m.member = t == 0 && r.member;
            m.tail = r.tail;
        }
        model.push_back(std::move(m));
    }
    models_[t] = std::make_shared<PublicRows>(std::move(model));
    tables_[t]->setModel(models_[t]);
    tables_[t]->setScrollOffset(scroll);
    if (!keepPath.empty())
        for (std::size_t k = 0; k < lines.size(); ++k) {
            const auto& l = lines[k];
            const bool same = keepFolder ? (l.folder && lowerCopy(l.path) == lowerCopy(keepPath))
                                         : (!l.folder && l.row < all_[t].size() && all_[t][l.row].cells[Path] == keepPath);
            if (!same) continue;
            tables_[t]->selectModelRows({static_cast<ui::RowIndex>(k)}, false);
            break;
        }
    invalidate();
}

bool HmiPublicVarsPane::selectPath(const std::string& path) {
    const int tab = hmi::pub::isSysRoot(path.substr(0, path.find('.'))) ? System : Instances;
    showTab(tab);
    const int t = tab == Instances ? 1 : 0;
    for (const auto i : shown_[t]) {
        const auto& r = all_[t][i];
        if (!hmi::pub::same(r.cells[Path], path)) continue;
        // Ses dossiers se deplient.
        for (std::size_t d = 0; d < r.folders.size(); ++d) {
            const auto key = lowerCopy(folderPath(r.folders, d));
            open_[t].insert(key);
            searchClosed_[t].erase(key);
        }
        rebuildLines(tab);
        for (std::size_t k = 0; k < lines_[t].size(); ++k)
            if (!lines_[t][k].folder && lines_[t][k].row == i) {
                tables_[t]->selectModelRows({static_cast<ui::RowIndex>(k)});
                return true;
            }
        return false;
    }
    return false;
}

std::string HmiPublicVarsPane::selectedPath() const {
    const int t = currentTab() == Instances ? 1 : 0;
    const auto sel = tables_[t]->selectedModelRows();
    if (sel.empty()) return {};
    const auto* l = lineAt(t, sel.front());
    if (!l || l->folder || l->row >= all_[t].size()) return {};
    return all_[t][l->row].cells[Path];
}

std::size_t HmiPublicVarsPane::rowsIn(int tab) const { return shown_[tab == Instances ? 1 : 0].size(); }

std::string HmiPublicVarsPane::cellAt(int tab, std::size_t row, std::size_t column) const {
    const int t = tab == Instances ? 1 : 0;
    if (row >= shown_[t].size() || column >= ColumnCount) return {};
    return all_[t][shown_[t][row]].cells[column];
}

void HmiPublicVarsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    searchBox_->setBounds({b.x + 8, b.y + 42, std::min(460.f, b.w - 16), 28});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    // 1.9 : la note des structures d'esclaves prend une ligne au-dessus de la barre d'etat.
    const float note = slaveNote_ ? 24.f : 0.f;
    tabs_->setBounds({b.x, b.y + 76, b.w, std::max(0.f, b.h - 100 - note)});
}

void HmiPublicVarsPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    // 1.9 : l'onglet a pu changer depuis la derniere disposition.
    if (const bool note = currentTab() == System && !hmi::pub::slaveEquipments(doc_->project).empty(); note != slaveNote_) {
        slaveNote_ = note;
        invalidateLayout();
    }
    if (slaveNote_) {
        // 1.9 : sous le tableau, comment se nomme une structure d'esclave simule.
        const auto b = bounds();
        const gfx::Rect strip{b.x, b.y + b.h - 48, b.w, 24};
        ctx.r.fillRect(strip, ctx.theme.color.panelBg);
        ctx.r.line({strip.x, strip.y}, {strip.right(), strip.y}, ctx.theme.color.border, 1.f);
        const auto f = ctx.theme.font.smallUi;
        ctx.r.drawText({strip.x + 10.f, strip.y + (strip.h - ctx.r.lineHeight(f)) / 2.f},
                       "Une structure par esclave simul\xC3\xA9 : SYS.Slave.<nom> (le nom de l'\xC3\xA9quipement, espaces et accents remplac\xC3\xA9s par _)",
                       f, ctx.theme.color.textMuted);
    }
    // En marche, les valeurs se relisent deux fois par seconde ; les lignes
    // restent (le defilement, la ligne choisie et les dossiers ouverts aussi).
    if (hosts_.live && ctx.time - lastValues_ >= 0.5) {
        lastValues_ = ctx.time;
        const bool was = live_;
        updateValues(false);
        if (live_ != was) {
            applyFilter();
        } else if (live_) {
            const int t = currentTab() == Instances ? 1 : 0;
            if (auto* rows = dynamic_cast<PublicRows*>(models_[t].get()))
                for (std::size_t k = 0; k < lines_[t].size(); ++k) {
                    const auto& l = lines_[t][k];
                    if (l.folder || l.row >= all_[t].size()) continue;
                    const auto& r = all_[t][l.row];
                    rows->setValue(k, r.cells[Value], r.live);
                }
            tables_[t]->invalidate();
        }
    }
}

} // namespace app
