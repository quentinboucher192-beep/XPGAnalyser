// Configuration > Poste d'exploitation (lot 14).
#include "HmiStationPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiStationLaunch.hpp"
#include "../../hmi/HmiCrypto.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cstdlib>
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

enum : int { CTry = 1, CAdd, CRemove, CPassword, CLauncher, CAutoOn, CAutoOff };

constexpr int kMaxDisplay = 9;

ui::Tone toneOf(int t) {
    switch (t) {
        case 1: return ui::Tone::Ok;
        case 2: return ui::Tone::Warning;
        case 3: return ui::Tone::Error;
        case 4: return ui::Tone::Accent;
        default: return ui::Tone::None;
    }
}

// Les vues qu'un ecran peut montrer : les vues ordinaires (pas les modeles,
// en-tetes, pieds ni popups).
std::vector<std::string> screenViews(const hmi::Project& p) {
    std::vector<std::string> out;
    for (const auto& v : p.views)
        if (v.role == "vue") out.push_back(v.name);
    return out;
}

// Une vue par son nom, sans casse (on tape "synoptique").
const hmi::View* findView(const hmi::Project& p, const std::string& name) {
    if (const auto* v = p.viewByName(name)) return v;
    for (const auto& v : p.views)
        if (same(v.name, name)) return &v;
    return nullptr;
}

std::string startViewName(const hmi::Project& p) {
    if (const auto* v = p.view(p.config.startView)) return v->name;
    const auto views = screenViews(p);
    return views.empty() ? std::string{} : views.front();
}

} // namespace

HmiStationPane::HmiStationPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(CTry, HmiGlyph::Play,
               "Essayer le poste : l'IHM seule, en plein \xC3\xA9" "cran, tout de suite (Ctrl+Alt+Q, puis \xC2\xAB Passer en conception \xC2\xBB, "
               "ram\xC3\xA8ne ici ; Ctrl+Alt+S : la page Simulation)",
               "Essayer le poste");
    tools->separator();
    tools->add(CAdd, HmiGlyph::Plus, "Ajouter un \xC3\xA9" "cran secondaire : un moniteur de plus, qui montre une vue en direct", "Ajouter un \xC3\xA9" "cran");
    tools->add(CRemove, HmiGlyph::Delete, "Retirer l'\xC3\xA9" "cran secondaire choisi", "Retirer");
    tools->separator();
    tools->add(CPassword, HmiGlyph::Password, "Le mot de passe de sortie du poste (le kiosque le demande avant de rendre la main)",
               "Mot de passe de sortie");
    tools->add(CLauncher, HmiGlyph::Export,
               "Cr\xC3\xA9" "er le lanceur : lancer-poste.cmd (Windows) ou lancer-poste.sh dans le dossier du projet - un double clic ouvre le poste",
               "Cr\xC3\xA9" "er le lanceur");
    tools->add(CAutoOn, HmiGlyph::Check,
               "D\xC3\xA9marrer avec la session : le poste s'ouvre quand l'utilisateur de ce PC ouvre sa session (Windows : la cl\xC3\xA9 Run ; "
               "Linux : ~/.config/autostart)",
               "D\xC3\xA9marrer avec la session");
    tools->add(CAutoOff, HmiGlyph::Stop, "Ne plus d\xC3\xA9marrer le poste avec la session", "Ne plus d\xC3\xA9marrer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(CTry, [this] { return static_cast<bool>(hosts_.tryStation); });
    tools_->setEnabledWhen(CRemove, [this] { return selectedScreen() >= 2; });
    tools_->setEnabledWhen(CAdd, [this] { return doc_->project.station.screens.size() + 1 < static_cast<std::size_t>(kMaxDisplay); });
    tools_->setEnabledWhen(CLauncher, [this] { return !folder().empty(); });
    tools_->setEnabledWhen(CAutoOn, [this] { return !folder().empty() && !autostart(); });
    tools_->setEnabledWhen(CAutoOff, [this] { return autostart(); });

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto screens = std::make_unique<ui::TableView>(base + ".screens");
    screens->setColumns({{"\xC3\x89" "cran", 120.f}, {"Vue", 260.f}, {"Taille de la vue", 130.f}, {"Moniteur", 330.f}, {"R\xC3\xB4le", 420.f}});
    screens->setSelectionMode(ui::SelectionMode::Single);
    auto launch = std::make_unique<ui::TableView>(base + ".launch");
    launch->setColumns({{"Lancement", 230.f}, {"Valeur", 900.f}});
    screens_ = static_cast<ui::TableView*>(screens.get());
    launch_ = static_cast<ui::TableView*>(launch.get());
    tabs->addTab({"\xC3\x89" "crans", ui::Icon::Screen}, std::move(screens));
    tabs->addTab({"Lancement", ui::Icon::Station}, std::move(launch));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case CTry:
                if (hosts_.tryStation) hosts_.tryStation();
                break;
            case CAdd: (void)addScreen(); break;
            case CRemove: if (selectedScreen() >= 2) (void)removeScreen(selectedScreen()); break;
            case CPassword:
                if (hosts_.askPassword) hosts_.askPassword();
                break;
            case CLauncher: (void)writeLauncher(); break;
            case CAutoOn: case CAutoOff: (void)setAutostart(a == CAutoOn); break;
            default: break;
        }
    });
    links_ += screens_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiStationPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiStationPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiStationPane::projectName() const {
    const auto& n = doc_->project.config.name;
    return n.empty() ? std::string("IHM") : n;
}

std::string HmiStationPane::folder() const { return hosts_.projectFolder ? hosts_.projectFolder() : std::string{}; }

std::string HmiStationPane::exe() const {
    const std::string e = hosts_.executable ? hosts_.executable() : std::string{};
    return e.empty() ? station::currentExecutable() : e;
}

int HmiStationPane::selectedScreen() const {
    const auto rows = screens_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? 0 : order_[rows.front()];
}

void HmiStationPane::selectScreen(int display) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == display) screens_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildProperties();
}

bool HmiStationPane::autostart() const {
    // Lot 15 : une seule entree par PC - la notre si elle lance CE projet.
    const std::string current = station::autostartProject();
    if (current.empty() || folder().empty()) return false;
    std::error_code ec;
    return current == folder() || std::filesystem::equivalent(current, folder(), ec);
}

void HmiStationPane::refresh() {
    refreshing_ = true;
    const int keep = selectedScreen();
    const auto& p = doc_->project;
    const auto& st = p.station;
    const int monitors = hosts_.displays ? hosts_.displays() : 0;
    const auto monitorText = [&](int display) {
        if (monitors <= 0) return std::string("inconnu (le poste le dira au lancement)");
        if (display <= monitors) return "branch\xC3\xA9 (" + std::to_string(monitors) + " moniteur(s) sur ce PC)";
        return "absent sur ce PC : une fen\xC3\xAAtre ordinaire (" + std::to_string(monitors) + " moniteur(s))";
    };
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    {
        const std::string start = startViewName(p);
        const auto* v = p.viewByName(start);
        order_.push_back(1);
        rows.push_back({"1 (principal)", start.empty() ? std::string("\xE2\x80\x94") : start,
                        v ? std::to_string(v->width) + " \xC3\x97 " + std::to_string(v->height) : std::string("\xE2\x80\x94"), monitorText(1),
                        "la vue de d\xC3\xA9marrage, puis la navigation ; l'op\xC3\xA9rateur y commande"});
        tones.push_back(start.empty() ? 3 : 0);
    }
    for (const auto& e : st.screens) {
        const auto* v = p.viewByName(e.view);
        order_.push_back(e.display);
        rows.push_back({std::to_string(e.display), e.view.empty() ? std::string("\xE2\x80\x94") : e.view,
                        v ? std::to_string(v->width) + " \xC3\x97 " + std::to_string(v->height) : std::string("vue introuvable"), monitorText(e.display),
                        "cette vue en direct, sans commande (un mur d'images, un tableau de bord)"});
        tones.push_back(!v ? 3 : (monitors > 0 && e.display > monitors) ? 2 : 0);
    }
    screensModel_ = std::make_shared<Rows>(
        std::vector<std::string>{"\xC3\x89" "cran", "Vue", "Taille de la vue", "Moniteur", "R\xC3\xB4le"}, std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= tones.size()) return s;
            if (c == 0) s.bold = true;
            if ((c == 1 || c == 2) && tones[r] == 3) s.fgTone = ui::Tone::Error;
            if (c == 3 && tones[r] == 2) s.fgTone = ui::Tone::Warning;
            if (c == 0) s.icon = ui::Icon::Screen;
            return s;
        });
    screens_->setModel(screensModel_);
    tabs_->setTabBadge(0, std::to_string(order_.size()), ui::Tone::Accent);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == keep) screens_->selectModelRows({static_cast<ui::RowIndex>(i)});
    refreshing_ = false;
    refreshLaunch();
    rebuildProperties();
}

void HmiStationPane::refreshLaunch() {
    const auto& p = doc_->project;
    const auto& st = p.station;
    const std::string f = folder();
    const std::string program = exe();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    const auto add = [&](std::string a, std::string b, int tone) {
        rows.push_back({std::move(a), std::move(b)});
        tones.push_back(tone);
    };
    add("Ligne de commande", f.empty() ? std::string("enregistrez d'abord le projet dans un dossier") : station::commandLine(program, f),
        f.empty() ? 2 : 4);
    {
        std::error_code ec;
        const auto launcher = f.empty() ? std::filesystem::path{} : std::filesystem::path(f) / station::launcherName();
        const bool there = !launcher.empty() && std::filesystem::exists(launcher, ec);
        add("Lanceur", there ? launcher.string() + " (un double clic ouvre le poste)" : "pas encore : outil \xC2\xAB Cr\xC3\xA9" "er le lanceur \xC2\xBB",
            there ? 1 : 0);
    }
    {
        std::string where;
        const std::string current = station::autostartProject(&where);
        const bool on = autostart();
        std::string why;
        const bool finish = !hosts_.finished || hosts_.finished(&why);
        add("D\xC3\xA9marrer avec le PC", on ? "oui \xE2\x80\x94 " + where
                                    : !current.empty() ? "non : ce PC d\xC3\xA9marre d\xC3\xA9j\xC3\xA0 le poste d'un autre projet (" + current + ") - un seul par PC"
                                    : !finish ? "non : il faut un projet FINISH (" + why + ")"
                                              : std::string("non (outil \xC2\xAB D\xC3\xA9marrer avec la session \xC2\xBB)"),
            on ? 1 : !current.empty() || !finish ? 2 : 0);
    }
    add("Sortie", std::string("Ctrl+Alt+Q") + (st.cornerExit ? ", ou cinq touchers du coin haut droit en 3 s" : "")
                      + (st.kiosk ? (st.exitHash.empty() ? " ; aucun mot de passe : la sortie est libre" : " ; le mot de passe de sortie est demand\xC3\xA9")
                                  : " ; fermer la fen\xC3\xAAtre suffit (pas de kiosque)"),
        st.kiosk && st.exitHash.empty() ? 2 : 0);
    // 1.9 : le raccourci de la page Simulation.
    add("Page Simulation", st.simPage ? std::string("Ctrl+Alt+S : Param\xC3\xA8tres syst\xC3\xA8me \xE2\x80\xBA Simulation, permission Administrer (les esclaves simul\xC3\xA9s)")
                                      : std::string("aucune : la case \xC2\xAB Page Simulation \xC2\xBB est d\xC3\xA9" "coch\xC3\xA9" "e (Ctrl+Alt+S ne fait rien)"),
        0);
    const int monitors = hosts_.displays ? hosts_.displays() : 0;
    add("Moniteurs branch\xC3\xA9s", monitors > 0 ? std::to_string(monitors) : std::string("inconnu"), 0);
    add("Automate", p.comm.modbus() ? "Modbus TCP " + p.comm.host + ":" + std::to_string(p.comm.port) + (p.comm.demoServer ? " (le serveur de d\xC3\xA9monstration)" : "")
                                    : std::string("le simulateur") + (st.runSimulator ? ", lanc\xC3\xA9 au d\xC3\xA9marrage du poste" : ", \xC3\xA0 lancer"),
        4);
    // Ce qui manque.
    if (startViewName(p).empty()) add("\xC3\x80 corriger", "aucune vue \xC3\xA0 montrer : cr\xC3\xA9" "ez une vue (Vues)", 3);
    if (st.kiosk && st.exitHash.empty())
        add("\xC3\x80 v\xC3\xA9rifier", "kiosque sans mot de passe de sortie : n'importe qui peut quitter le poste (outil \xC2\xAB Mot de passe de sortie \xC2\xBB)", 2);
    for (const auto& e : st.screens)
        if (!p.viewByName(e.view)) add("\xC3\x80 corriger", "\xC3\xA9" "cran " + std::to_string(e.display) + " : vue \xC2\xAB " + e.view + " \xC2\xBB introuvable", 3);
    launchLines_.clear();
    for (const auto& r : rows) launchLines_.push_back(r[0] + " : " + r[1]);
    launchModel_ = std::make_shared<Rows>(std::vector<std::string>{"Lancement", "Valeur"}, std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
        ui::CellStyle s;
        if (r >= tones.size()) return s;
        if (c == 0) s.bold = true;
        if (c == 1) s.fgTone = toneOf(tones[r]);
        if (c == 0 && tones[r] == 3) s.icon = ui::Icon::Error;
        if (c == 0 && tones[r] == 2) s.icon = ui::Icon::Warning;
        return s;
    });
    launch_->setModel(launchModel_);
    int worst = 0;
    for (const int t : tones) worst = std::max(worst, t == 4 ? 0 : t);
    tabs_->setTabBadge(1, worst >= 3 ? "\xC3\xA0 corriger" : worst == 2 ? "\xC3\xA0 v\xC3\xA9rifier" : "pr\xC3\xAAt",
                       worst >= 3 ? ui::Tone::Error : worst == 2 ? ui::Tone::Warning : ui::Tone::Ok);
}

bool HmiStationPane::change(const std::string& label, const std::function<void(hmi::Station&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { fn(p.station); });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiStationPane::setSetting(const std::string& key, const std::string& raw, std::string* why) {
    const bool on = yes(trimmed(raw));
    struct Flag {
        const char* key;
        bool hmi::Station::*field;
        const char* label;
    };
    static const Flag kFlags[] = {
        {"plein_ecran", &hmi::Station::fullScreen, "Plein \xC3\xA9" "cran"},
        {"kiosque", &hmi::Station::kiosk, "Kiosque"},
        {"coin", &hmi::Station::cornerExit, "Sortie par le coin"},
        {"sans_curseur", &hmi::Station::hideCursor, "Cacher le curseur"},
        {"simulateur", &hmi::Station::runSimulator, "Lancer le simulateur"},
        // 1.9 : la page Simulation (Ctrl+Alt+S), les reperes des lectures simulees.
        {"page_simulation", &hmi::Station::simPage, "Page Simulation"},
        {"reperes_simules", &hmi::Station::simMarks, "Rep\xC3\xA9rer les lectures simul\xC3\xA9" "es"},
    };
    for (const auto& f : kFlags)
        if (key == f.key) {
            if (doc_->project.station.*(f.field) == on) return true;
            if (!change(std::string("Poste : ") + f.label, [&](hmi::Station& s) { s.*(f.field) = on; })) return false;
            if (key == "kiosque" && on && doc_->project.station.exitHash.empty())
                say("Kiosque : d\xC3\xA9" "finissez un mot de passe de sortie, sinon n'importe qui quitte le poste.", true);
            return true;
        }
    const std::string m = "r\xC3\xA9glage inconnu : " + key;
    say(m, true);
    if (why) *why = m;
    return false;
}

bool HmiStationPane::addScreen(int display, const std::string& rawView, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& p = doc_->project;
    const auto& st = p.station;
    const auto taken = [&](int d) { return std::any_of(st.screens.begin(), st.screens.end(), [d](const hmi::StationScreen& e) { return e.display == d; }); };
    if (display == 0) {
        display = 2;
        while (display <= kMaxDisplay && taken(display)) ++display;
    }
    if (display < 2 || display > kMaxDisplay) return fail("\xC3\xA9" "cran " + std::to_string(display) + " : de 2 \xC3\xA0 " + std::to_string(kMaxDisplay) + " (1 : le principal)");
    if (taken(display)) return fail("l'\xC3\xA9" "cran " + std::to_string(display) + " a d\xC3\xA9j\xC3\xA0 sa vue");
    std::string view = trimmed(rawView);
    const auto views = screenViews(p);
    if (view.empty()) {
        // La premiere vue que ni le principal ni un autre ecran ne montre.
        const std::string start = startViewName(p);
        for (const auto& v : views)
            if (!same(v, start) && std::none_of(st.screens.begin(), st.screens.end(), [&](const hmi::StationScreen& e) { return same(e.view, v); })) {
                view = v;
                break;
            }
        if (view.empty() && !views.empty()) view = views.front();
    }
    if (view.empty()) return fail("aucune vue \xC3\xA0 montrer");
    const auto* v = findView(p, view);
    if (!v || v->role != "vue") return fail("vue \xC2\xAB " + view + " \xC2\xBB introuvable (une vue ordinaire, pas un mod\xC3\xA8le ni une popup)");
    hmi::StationScreen e;
    e.display = display;
    e.view = v->name;
    if (!change("Poste : ajouter l'\xC3\xA9" "cran " + std::to_string(display), [&](hmi::Station& s) {
            s.screens.push_back(e);
            std::sort(s.screens.begin(), s.screens.end(), [](const hmi::StationScreen& a, const hmi::StationScreen& b) { return a.display < b.display; });
        }))
        return false;
    selectScreen(display);
    say("\xC3\x89" "cran " + std::to_string(display) + " : " + e.view + " en direct (un moniteur de plus ; absent, une fen\xC3\xAAtre ordinaire).");
    return true;
}

bool HmiStationPane::removeScreen(int display, std::string* why) {
    const auto& st = doc_->project.station;
    if (std::none_of(st.screens.begin(), st.screens.end(), [display](const hmi::StationScreen& e) { return e.display == display; })) {
        const std::string m = display == 1 ? std::string("l'\xC3\xA9" "cran principal reste : il montre la vue de d\xC3\xA9marrage")
                                           : "pas d'\xC3\xA9" "cran " + std::to_string(display);
        say(m, true);
        if (why) *why = m;
        return false;
    }
    if (!change("Poste : retirer l'\xC3\xA9" "cran " + std::to_string(display),
                [&](hmi::Station& s) { std::erase_if(s.screens, [display](const hmi::StationScreen& e) { return e.display == display; }); }))
        return false;
    say("\xC3\x89" "cran " + std::to_string(display) + " retir\xC3\xA9 (Ctrl+Z le rend).");
    return true;
}

bool HmiStationPane::setScreenField(int display, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& p = doc_->project;
    const auto& st = p.station;
    const auto it = std::find_if(st.screens.begin(), st.screens.end(), [display](const hmi::StationScreen& e) { return e.display == display; });
    if (it == st.screens.end()) return fail("pas d'\xC3\xA9" "cran " + std::to_string(display));
    const std::string v = trimmed(raw);
    if (key == "ecran") {
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || (end && *end) || n < 2 || n > kMaxDisplay) return fail("\xC3\x89" "cran : un num\xC3\xA9ro de 2 \xC3\xA0 " + std::to_string(kMaxDisplay));
        const int to = static_cast<int>(n);
        if (to == display) return true;
        if (std::any_of(st.screens.begin(), st.screens.end(), [to](const hmi::StationScreen& e) { return e.display == to; }))
            return fail("l'\xC3\xA9" "cran " + std::to_string(to) + " a d\xC3\xA9j\xC3\xA0 sa vue");
        if (!change("Poste : \xC3\xA9" "cran " + std::to_string(display) + " \xE2\x86\x92 " + std::to_string(to), [&](hmi::Station& s) {
                for (auto& e : s.screens)
                    if (e.display == display) e.display = to;
                std::sort(s.screens.begin(), s.screens.end(), [](const hmi::StationScreen& a, const hmi::StationScreen& b) { return a.display < b.display; });
            }))
            return false;
        selectScreen(to);
        return true;
    }
    if (key == "vue") {
        const auto* view = findView(p, v);
        if (!view || view->role != "vue") return fail("vue \xC2\xAB " + v + " \xC2\xBB introuvable (une vue ordinaire)");
        return change("Poste : la vue de l'\xC3\xA9" "cran " + std::to_string(display), [&](hmi::Station& s) {
            for (auto& e : s.screens)
                if (e.display == display) e.view = view->name;
        });
    }
    return fail("champ inconnu : " + key);
}

bool HmiStationPane::setExitPassword(const std::string& password, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (password.empty()) {
        if (doc_->project.station.exitHash.empty()) return true;
        if (!change("Poste : sortie libre", [](hmi::Station& s) {
                s.exitSalt.clear();
                s.exitHash.clear();
            }))
            return false;
        say("Mot de passe de sortie retir\xC3\xA9 : la sortie du poste est libre (Ctrl+Z le rend).");
        return true;
    }
    if (password.size() < 4) return fail("mot de passe de sortie trop court : 4 caract\xC3\xA8res au moins");
    const std::string salt = hmi::randomHex(16);
    const std::string hash = hmi::passwordHash(salt, password);
    if (!change("Poste : mot de passe de sortie", [&](hmi::Station& s) {
            s.exitSalt = salt;
            s.exitHash = hash;
        }))
        return false;
    say("Mot de passe de sortie d\xC3\xA9" "fini (gard\xC3\xA9 sous forme d'empreinte sal\xC3\xA9" "e dans le projet).");
    return true;
}

bool HmiStationPane::writeLauncher(std::string* where) {
    std::string path, why;
    const bool ok = station::writeLauncher(exe(), folder(), projectName(), &path, &why);
    if (where) *where = ok ? path : why;
    say(ok ? "Lanceur \xC3\xA9" "crit : " + path + " - un double clic ouvre le poste." : "Lanceur impossible : " + why, !ok);
    refreshLaunch();
    return ok;
}

bool HmiStationPane::setAutostart(bool on, std::string* why) {
    std::string where, reason;
    // Lot 15 : seul un projet FINISH demarre avec le PC.
    if (on && hosts_.finished && !hosts_.finished(&reason)) {
        say("D\xC3\xA9marrage avec le PC : il faut un projet FINISH (" + reason + ") - Projet > \xC3\x89tat.", true);
        if (why) *why = reason;
        refreshLaunch();
        return false;
    }
    const bool ok = station::setAutostart(projectName(), exe(), folder(), on, &where, &reason);
    if (!ok) {
        say("D\xC3\xA9marrage avec la session : " + reason, true);
        if (why) *why = reason;
    } else {
        say(on ? "Le poste d\xC3\xA9marrera avec la session (" + where + ")." : std::string("Le poste ne d\xC3\xA9marrera plus avec la session."));
    }
    refreshLaunch();
    return ok;
}

void HmiStationPane::rebuildProperties() {
    const auto& p = doc_->project;
    const auto& st = p.station;
    const auto commit = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setSetting(key, std::string(v));
        };
    };
    std::vector<PG::Category> cats;
    PG::Category post;
    post.name = "Poste";
    post.properties.push_back(prop("Plein \xC3\xA9" "cran", tf(st.fullScreen), PG::ValueType::Boolean, commit("plein_ecran"),
                                   "La vue de d\xC3\xA9marrage sur tout l'\xC3\xA9" "cran, sans barre de titre ni bordure."));
    post.properties.push_back(prop("Kiosque", tf(st.kiosk), PG::ValueType::Boolean, commit("kiosque"),
                                   "Fermer la fen\xC3\xAAtre (Alt+F4) ne fait rien, F11 non plus ; la sortie (Ctrl+Alt+Q) demande le mot de passe de sortie."));
    post.properties.push_back(prop("Sortie par le coin", tf(st.cornerExit), PG::ValueType::Boolean, commit("coin"),
                                   "Un \xC3\xA9" "cran tactile sans clavier : cinq touchers du coin haut droit en trois secondes demandent la sortie."));
    post.properties.push_back(prop("Cacher le curseur", tf(st.hideCursor), PG::ValueType::Boolean, commit("sans_curseur"),
                                   "Un \xC3\xA9" "cran tactile : pas de fl\xC3\xA8" "che au milieu de la vue."));
    post.properties.push_back(prop("Lancer le simulateur", tf(st.runSimulator), PG::ValueType::Boolean, commit("simulateur"),
                                   "Sur le simulateur (ou le serveur de d\xC3\xA9monstration) : l'automate simul\xC3\xA9 tourne d\xC3\xA8s le lancement du poste."));
    // 1.9 : la page Simulation de Parametres systeme, les reperes des lectures simulees.
    post.properties.push_back(prop("Page Simulation (Ctrl+Alt+S, permission Administrer)", tf(st.simPage), PG::ValueType::Boolean,
                                   commit("page_simulation"),
                                   "Ctrl+Alt+S ouvre Param\xC3\xA8tres syst\xC3\xA8me \xE2\x80\xBA Simulation : les esclaves simul\xC3\xA9s, ce que lit l'IHM, "
                                   "animer et forcer. Toujours r\xC3\xA9serv\xC3\xA9" "e \xC3\xA0 la permission Administrer ; d\xC3\xA9" "coch\xC3\xA9" "e, elle n'existe pas sur le poste."));
    post.properties.push_back(prop("Rep\xC3\xA9rer les lectures simul\xC3\xA9" "es", tf(st.simMarks), PG::ValueType::Boolean, commit("reperes_simules"),
                                   "Un cadre violet en tirets et une pastille sur les objets lus sur un esclave simul\xC3\xA9, et le bandeau "
                                   "LECTURES SIMUL\xC3\x89" "ES. D\xC3\xA9" "coch\xC3\xA9 : un poste de formation, tout simul\xC3\xA9, sans rep\xC3\xA8res."));
    post.properties.push_back(prop("Mot de passe de sortie",
                                   st.exitHash.empty() ? std::string("aucun : outil \xC2\xAB Mot de passe de sortie \xC2\xBB")
                                                       : "d\xC3\xA9" "fini (empreinte " + st.exitHash.substr(0, 12) + "...)",
                                   PG::ValueType::ReadOnly));
    cats.push_back(std::move(post));

    const int chosen = selectedScreen();
    if (chosen == 1) {
        PG::Category main;
        main.name = "\xC3\x89" "cran choisi";
        main.properties.push_back(prop("\xC3\x89" "cran", "1 (principal)", PG::ValueType::ReadOnly));
        main.properties.push_back(prop("Vue", startViewName(p), PG::ValueType::ReadOnly, {},
                                       "La vue de d\xC3\xA9marrage (Configuration), puis celles o\xC3\xB9 la navigation m\xC3\xA8ne."));
        cats.push_back(std::move(main));
    } else if (chosen >= 2) {
        const auto it = std::find_if(st.screens.begin(), st.screens.end(), [chosen](const hmi::StationScreen& e) { return e.display == chosen; });
        if (it != st.screens.end()) {
            const auto field = [this, chosen](const char* f) {
                return [this, chosen, f](std::string_view v) {
                    message_.clear();
                    return setScreenField(chosen, f, std::string(v));
                };
            };
            PG::Category sc;
            sc.name = "\xC3\x89" "cran choisi";
            sc.properties.push_back(prop("\xC3\x89" "cran", std::to_string(it->display), PG::ValueType::Integer, field("ecran"),
                                         "Le num\xC3\xA9ro du moniteur (2, 3...) dans l'ordre du syst\xC3\xA8me ; absent : une fen\xC3\xAAtre ordinaire."));
            sc.properties.push_back(prop("Vue", it->view, PG::ValueType::Enum, field("vue"), "La vue que cet \xC3\xA9" "cran montre en direct.", screenViews(p)));
            cats.push_back(std::move(sc));
        }
    }
    grid_->setCategories(std::move(cats));
}

void HmiStationPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(460.f, b.w * 0.32f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiStationPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    // Le lanceur, le demarrage avec la session : sur le disque, relus chaque
    // seconde et demie (un lanceur efface a la main se voit).
    if (ctx.time - lastLive_ >= 1.5) {
        lastLive_ = ctx.time;
        refreshLaunch();
    }
}

} // namespace app
