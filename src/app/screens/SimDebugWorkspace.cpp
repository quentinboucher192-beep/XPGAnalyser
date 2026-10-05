// =============================================================================
//  app/screens/SimDebugWorkspace.cpp - lot API 8 : Simulation > Debogage
// -----------------------------------------------------------------------------
//  L'ecran et le debogage :
//
//   * L'ONGLET Simulation > Debogage (SimDebugPane), ouvert comme les onglets
//     de l'API (un ApiFrame : la barre, le volet, la ligne d'aide), retrouve
//     par sa cle "debogage" (apiTab), ou qu'il soit.
//   * LES POINTS D'ARRET DANS L'ONGLET D'UNE SECTION : openDocument branche
//     l'editeur (wireSectionBreakpoints) - la colonne de la marge, F9 ; a
//     chaque image, les marges des sections ouvertes suivent les points
//     d'arret et la ligne d'arret (une signature comparee : rien ne se refait
//     tant que rien ne change). Les editeurs sont retrouves par un pointeur
//     garde avec la vie de leur signal : un onglet ferme les emporte.
//   * LE JOURNAL : chaque geste du debogage et chaque passage sur un point
//     d'arret passent par journalDebugEvent - LE point de branchement vers le
//     journal du Centre de simulation (SimJournal).
// =============================================================================
#include "Screens.hpp"

#include "../ApiPanes.hpp"
#include "../App.hpp"
#include "../SimDebugPane.hpp"
#include "../SimDebugText.hpp"
#include "../SimConditionDialog.hpp"      // Lot API 8 (2e partie) : la condition par clic droit dans la marge
#include "../TutorialsLot8.hpp"          // Lot API 8 : didacticiels et aide (Aide : la page de l'onglet)
#include "../hmi/HmiAskDialog.hpp"
#include "../../sim/Runtime.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <deque>

namespace app {

using namespace ui;
namespace sd = simdebug;

// Ce que l'ecran retient du debogage.
struct MainAnalysisScreen::SimDebugState {
    struct Editor {
        ui::MultiLineText*                    editor{nullptr};
        std::weak_ptr<core::Signal<std::size_t>> alive;     // expire avec l'editeur (son onglet ferme)
        domain::Index                         section{domain::kNoIndex};
    };
    std::vector<Editor>     editors;
    std::string             signature;      // ce que les marges montrent deja
    std::string             hitSeen;        // le dernier passage journalise
    std::deque<std::string> events;         // les derniers evenements (debugEvents)
    const void*             projectSeen{nullptr};
    std::uint64_t           revisionSeen{~std::uint64_t{0}};
};

namespace {

SimDebugPane* debugPaneOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<SimDebugPane*>(&frame->content()) : nullptr;
}

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// ---- Lot API 8 (2e partie) : le dialogue de la condition (clic droit dans la marge) ----
//  Le meme depuis l'onglet Debogage et depuis l'onglet d'une section : le point
//  de la ligne (pose au besoin), SimConditionDialog ; Valider : la condition,
//  Retirer le point : il s'en va ; Annuler : rien (le point pose reste, comme
//  dans la maquette).
using DebugJournal = std::function<void(const std::string&, const std::string&, const std::string&, int)>;
using DebugStatus = std::function<void(const std::string&)>;

std::string trimmedCopy(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

void openConditionDialog(App& app, const std::string& section, int line, const DebugJournal& journal, const DebugStatus& status) {
    const auto p = app.project();
    if (!p) return;
    auto& host = app.simulation();
    // Une section de bloc : l'instance ou le programme est en pause (ses valeurs).
    std::string instance;
    if (const auto index = sd::findSection(*p, section);
        index != domain::kNoIndex && host.state() == SimulationHost::State::Paused && host.lastBreakHit()) {
        const auto key = sd::sectionKey(*p, index);
        const auto levels = sd::stackLevels(host.lastBreakHit()->stack, host.lastBreakHit()->section);
        for (auto it = levels.rbegin(); it != levels.rend(); ++it)
            if (it->kind == sd::StackLevel::Kind::BlockSection && sd::sameSection(it->section, key)) {
                instance = it->instance;
                break;
            }
    }
    SimDebugPane::ConditionAsk ask;
    std::string why;
    if (!SimDebugPane::prepareConditionFor(*p, host, section, line, instance, ask, &why)) {
        if (status && !why.empty()) status(why);
        return;
    }
    if (ask.placed && journal)
        journal("point-arret", "Point d'arr\xC3\xAAt pos\xC3\xA9 : " + ask.section + ", ligne " + std::to_string(ask.line), ask.section, ask.line);
    SimConditionDialog::Spec spec;
    spec.title = ask.title;
    spec.code = ask.code;
    spec.condition = ask.condition;
    spec.ideas = ask.ideas;
    App* at = &app;
    spec.preview = [at, prefixes = ask.prefixes](const std::string& text) {
        return SimDebugPane::previewCondition(at->simulationRuntime(), prefixes, text);
    };
    app.menus().ShowDialog(std::make_unique<SimConditionDialog>(std::move(spec)),
                           [at, id = ask.id, number = ask.number, key = ask.section, line, journal, status](const menu::DialogResult& r) {
                               auto& h = at->simulation();
                               std::string said;
                               if (r.button == menu::DialogResult::Button::Ok) {
                                   const auto c = trimmedCopy(r.payload);
                                   if (!h.setBreakpointCondition(id, c)) return;
                                   said = "Point d'arr\xC3\xAAt " + std::to_string(number) + (c.empty() ? std::string(" : sans condition") : " : seulement si " + c);
                                   if (journal) journal("condition", said, key, line);
                               } else if (r.button == menu::DialogResult::Button::No) {
                                   if (!h.removeBreakpoint(id)) return;
                                   said = "Point d'arr\xC3\xAAt enlev\xC3\xA9 : " + key + ", ligne " + std::to_string(line);
                                   if (journal) journal("point-arret", said, key, line);
                               }
                               if (status && !said.empty()) status(said + ".");
                           });
}
// ---- fin Lot API 8 (2e partie) ----

} // namespace

// --------------------------------------------------------------------- l'onglet ----
void MainAnalysisScreen::openSimDebug() {
    if (!simDebug_) simDebug_ = std::make_shared<SimDebugState>();
    if (!app_.project()) {
        if (status_) status_->setTransientMessage("D\xC3\xA9" "bogage : ouvre d'abord un projet.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    if (!centre_) return;
    // Le centre doit etre a l'ecran (comme Affichage > Panneaux a afficher).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    if (auto* page = apiTab("debogage")) {
        if (showPage(page)) return;          // en onglet, ou dans sa fenetre detachee : devant
        apiTabs_.erase("debogage");
    }
    auto panel = std::make_unique<SimDebugPane>("analysis.sim.debogage.volet");
    auto* raw = panel.get();
    auto frame = std::make_unique<ApiFrame>("analysis.sim.debogage", std::move(panel));
    raw->attach(*frame);
    SimDebugPane::Hosts h;
    h.project = [this] { return app_.project(); };
    h.host = [this] { return &app_.simulation(); };
    h.attach = [this](std::string* why) { return attachSimulation(why); };
    h.goToLine = [this](const std::string& section, std::uint32_t line) { goToSectionLine(section, line); };
    h.request = [this](const std::string& key) { onApiRequest(key); };
    h.status = [this](const std::string& m) {
        if (!status_) return;
        status_->dismissTransient();
        status_->setMessage(m);
    };
    h.ask = [this](const std::string& title, const std::string& label, const std::string& text, const std::string& initial,
                   std::function<void(const std::string&)> done) {
        HmiAskDialog::Spec spec;
        spec.id = "dialog.simDebugAsk";
        spec.title = title;
        spec.text = text;
        HmiAskDialog::Option option;
        option.label = label;
        option.field = initial;
        option.hasField = true;
        spec.options.push_back(std::move(option));
        spec.confirm = title.rfind("Poser", 0) == 0 ? "Poser" : "OK";     // la maquette : Annuler / Poser
        spec.width = 640.f;
        app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [done = std::move(done)](const menu::DialogResult& r) {
            if (r.accepted() && done) done(HmiAskDialog::parse(r.payload).field);
        });
    };
    h.journal = [this](const std::string& kind, const std::string& text, const std::string& section, int line) {
        journalDebugEvent(kind, text, section, line);
    };
    // Lot API 8 (2e partie) : le clic droit dans la marge du code de l'onglet.
    h.askCondition = [this](const std::string& section, int line) {
        openConditionDialog(app_, section, line,
                            [this](const std::string& k, const std::string& t, const std::string& s, int l) { journalDebugEvent(k, t, s, l); },
                            [this](const std::string& m) {
                                if (!status_) return;
                                status_->dismissTransient();
                                status_->setMessage(m);
                            });
    };
    h.help = [this] {
        lot8::setPendingHelpAnchor(lot8::helpAnchorFor("debogage"));   // Lot API 8 : didacticiels et aide (sa page)
        (void)app_.actions().trigger("help.open", app_.commands());
    };
    raw->setHosts(std::move(h));
    auto* page = frame.get();
    const auto at = centre_->addTab(TabControl::Tab{"Simulation \xC2\xB7 D\xC3\xA9" "bogage", Icon::Halt, true, false}, std::move(frame));
    apiTabs_["debogage"] = page;
    centre_->setCurrentIndex(at);
}

// ------------------------------------------------- les sections : la marge ----
void MainAnalysisScreen::wireSectionBreakpoints(ui::MultiLineText& editor, domain::Index section) {
    if (!simDebug_) simDebug_ = std::make_shared<SimDebugState>();
    editor.setBreakpointGutter(true);
    // Poser ou enlever (un clic, F9) ; activer ou desactiver (Maj+clic, Ctrl+F9).
    const auto toggle = [this, section](std::size_t zeroBased, bool enableOnly) {
        const auto p = app_.project();
        if (!p || section >= p->sections.size()) return;
        auto& host = app_.simulation();
        const auto key = sd::sectionKey(*p, section);
        const int line = static_cast<int>(zeroBased) + 1;
        std::string said;
        bool found = false;
        for (const auto& b : host.breakpoints()) {
            if (SimDebugPane::isTransient(b.id) || !sd::sameSection(b.section, key) || b.line != line) continue;
            found = true;
            if (enableOnly) {
                (void)host.setBreakpointEnabled(b.id, !b.enabled);
                said = std::string("Point d'arr\xC3\xAAt ") + (b.enabled ? "d\xC3\xA9sactiv\xC3\xA9" : "activ\xC3\xA9") + " : " + key + ", ligne "
                     + std::to_string(line);
            } else {
                (void)host.removeBreakpoint(b.id);
                said = "Point d'arr\xC3\xAAt enlev\xC3\xA9 : " + key + ", ligne " + std::to_string(line);
            }
            break;
        }
        if (!found) {
            if (enableOnly) return;          // rien a activer sur cette ligne
            (void)host.addBreakpoint(key, line, {});
            said = "Point d'arr\xC3\xAAt pos\xC3\xA9 : " + key + ", ligne " + std::to_string(line);
        }
        journalDebugEvent("point-arret", said, key, line);
        if (status_) {
            status_->dismissTransient();
            status_->setMessage(said + (found ? std::string(".")
                                              : " \xE2\x80\x94 le programme s'y arr\xC3\xAAtera (Simulation \xE2\x80\xBA D\xC3\xA9" "bogage : Continuer, F5)."));
        }
        if (simDebug_) simDebug_->signature.clear();     // les marges suivent tout de suite
    };
    paneLinks_ += editor.breakpointToggled->connect([toggle](std::size_t line) { toggle(line, false); });
    paneLinks_ += editor.breakpointEnableToggled->connect([toggle](std::size_t line) { toggle(line, true); });
    // Lot API 8 (2e partie) : un clic droit dans la marge (sur le point, ou sur une
    // ligne sans point : il est pose d'abord) - le dialogue de sa condition.
    paneLinks_ += editor.breakpointConditionRequested->connect([this, section](std::size_t zeroBased) {
        const auto p = app_.project();
        if (!p || section >= p->sections.size()) return;
        openConditionDialog(app_, sd::sectionKey(*p, section), static_cast<int>(zeroBased) + 1,
                            [this](const std::string& k, const std::string& t, const std::string& s, int l) { journalDebugEvent(k, t, s, l); },
                            [this](const std::string& m) {
                                if (!status_) return;
                                status_->dismissTransient();
                                status_->setMessage(m);
                            });
        if (simDebug_) simDebug_->signature.clear();     // les marges suivent tout de suite
    });

    // Le survol d'un nom : sa valeur en direct - aussi une locale d'unite (Unite.nom
    // pour la simulation), et dans le corps d'un bloc, celle de l'instance ou le
    // programme est en pause.
    editor.setValueProvider([this, section](std::string_view symbol, std::string& out) {
        auto* rt = app_.simulationRuntime();
        const auto p = app_.project();
        if (!rt || symbol.empty()) return false;
        std::vector<std::string> prefixes;
        if (p && section < p->sections.size()) {
            const auto& s = p->sections[section];
            if (s.owner < p->pous.size()) {
                const auto& pou = p->pous[s.owner];
                if (pou.kind == domain::PouKind::ProgramUnit) {
                    prefixes.push_back(std::string(p->strings.text(pou.name)) + ".");
                } else if (pou.kind == domain::PouKind::FunctionBlockType) {
                    const auto& host = app_.simulation();
                    if (host.state() == SimulationHost::State::Paused && host.lastBreakHit()) {
                        const auto key = sd::sectionKey(*p, section);
                        const auto levels = sd::stackLevels(host.lastBreakHit()->stack, host.lastBreakHit()->section);
                        for (auto it = levels.rbegin(); it != levels.rend(); ++it)
                            if (it->kind == sd::StackLevel::Kind::BlockSection && sd::sameSection(it->section, key)) {
                                prefixes.push_back(it->instance + ".");
                                break;
                            }
                    }
                }
            }
        }
        prefixes.emplace_back();
        for (const auto& prefix : prefixes) {
            const std::string name = prefix + std::string(symbol);
            sim::Value v;
            if (!rt->get(name, v)) continue;
            out = std::string(symbol) + " = " + sd::formatValue(v);
            if (rt->isForced(name)) out += "   (forc\xC3\xA9" "e)";
            return true;
        }
        return false;
    });

    SimDebugState::Editor e;
    e.editor = &editor;
    e.alive = editor.breakpointToggled;
    e.section = section;
    simDebug_->editors.push_back(std::move(e));
    simDebug_->signature.clear();
}

bool MainAnalysisScreen::sectionMarginPoint(const std::string& section, int line, gfx::Point& where) {
    const auto p = app_.project();
    if (!p || line < 1 || !centre_) return false;
    const auto index = sd::findSection(*p, section);
    if (index == domain::kNoIndex) return false;
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    openDocument(index);
    const std::string editorId = "analysis.doc." + std::to_string(index) + ".code";
    auto* editor = widgetRoot() ? dynamic_cast<MultiLineText*>(widgetRoot()->findById(editorId)) : nullptr;
    if (!editor)
        if (auto* page = centre_->page(centre_->currentIndex())) editor = dynamic_cast<MultiLineText*>(page->findById(editorId));
    if (!editor) return false;
    const auto zeroBased = static_cast<std::size_t>(line - 1);
    if (editor->breakpointMarginPoint(zeroBased, where)) return true;
    // Pas en vue : la ligne vient a l'ecran (le point se lira apres le dessin).
    editor->goToLine(zeroBased);
    return false;
}

// ----------------------------------------------------------------- a chaque image ----
void MainAnalysisScreen::tickSimDebug(double now) {
    if (!simDebug_) simDebug_ = std::make_shared<SimDebugState>();
    auto& st = *simDebug_;
    auto* pane = debugPaneOf(apiTab("debogage"));
    // Le projet a change (une commande, un autre projet) : l'onglet relit le code.
    const auto p = app_.project();
    const auto revision = app_.commands().revision();
    if (p.get() != st.projectSeen || revision != st.revisionSeen) {
        st.projectSeen = p.get();
        st.revisionSeen = revision;
        st.signature.clear();
        if (pane) pane->refresh();
    }
    if (pane) pane->tick(now);

    const auto& host = app_.simulation();
    // Un passage sur un point d'arret : au journal, onglet ouvert ou non.
    std::string hitKey;
    const SimBreakHit* hit = nullptr;
    if (host.attached() && host.state() == SimulationHost::State::Paused && host.lastBreakHit()) {
        hit = &*host.lastBreakHit();
        hitKey = std::to_string(hit->id) + "|" + lowered(hit->section) + "|" + std::to_string(hit->line) + "|" + std::to_string(hit->scan);
    }
    if (hitKey != st.hitSeen) {
        st.hitSeen = hitKey;
        if (hit) {
            auto list = host.breakpoints();
            std::erase_if(list, [](const SimBreakpoint& b) { return SimDebugPane::isTransient(b.id); });
            sd::StateFacts f;
            f.attached = true;
            f.state = host.state();
            f.cycle = host.scanCount();
            f.hit = hit;
            for (std::size_t i = 0; i < list.size(); ++i)
                if (list[i].id == hit->id) {
                    f.breakpoint = &list[i];
                    f.number = i + 1;
                }
            if (!f.breakpoint) f.gesture = sd::LastGesture::RunToLine;
            journalDebugEvent("arret", sd::stateSentence(f), hit->section, hit->line);
        }
    }

    // Les marges des sections ouvertes : les points d'arret, la ligne d'arret.
    std::erase_if(st.editors, [](const SimDebugState::Editor& e) { return e.alive.expired(); });
    if (st.editors.empty() || !p) return;
    std::string signature = std::to_string(st.editors.size()) + "|";
    const auto list = host.breakpoints();
    for (const auto& b : list)
        if (!SimDebugPane::isTransient(b.id))
            signature += std::to_string(b.id) + (b.enabled ? "+" : "-") + (b.condition.empty() ? "" : "?") + lowered(b.section) + ":"
                       + std::to_string(b.line) + ";";
    if (hit) signature += "@" + lowered(hit->section) + ":" + std::to_string(hit->line);
    if (signature == st.signature) return;
    st.signature = std::move(signature);
    for (const auto& e : st.editors) {
        if (e.alive.expired() || !e.editor || e.section >= p->sections.size()) continue;
        const auto key = sd::sectionKey(*p, e.section);
        std::vector<MultiLineText::BreakMark> marks;
        for (const auto& b : list)
            if (!SimDebugPane::isTransient(b.id) && b.line > 0 && sd::sameSection(b.section, key))
                marks.push_back({static_cast<std::size_t>(b.line - 1), b.enabled, !b.condition.empty()});
        e.editor->setBreakpoints(std::move(marks));
        e.editor->setExecutionLine(hit && hit->line > 0 && sd::sameSection(hit->section, key) ? static_cast<std::size_t>(hit->line - 1)
                                                                                                : std::string::npos);
    }
}

// ------------------------------------------------------------------- le journal ----
//  LE POINT DE BRANCHEMENT : chaque evenement du debogage passe par ici et va au
//  journal du Centre de simulation (App::simJournal, source Debogage, en info comme
//  dans la maquette ; "aller a" : la ligne, sinon l'onglet Debogage). Le genre est
//  garde : un arret deja ecrit par le collecteur (SimulationHost::breakHit) au meme
//  endroit n'y fait pas deux lignes. Les 200 derniers restent aussi ici
//  (debugEvents, pour les scripts) ; le passage sur un point d'arret se dit aussi
//  dans la barre d'etat.
void MainAnalysisScreen::journalDebugEvent(const std::string& kind, const std::string& text, const std::string& section, int line) {
    if (!simDebug_) simDebug_ = std::make_shared<SimDebugState>();
    std::string entry = "[" + kind + "] " + text;
    if (!section.empty() && line > 0 && text.find(section) == std::string::npos) entry += " (" + section + ", ligne " + std::to_string(line) + ")";
    simDebug_->events.push_back(std::move(entry));
    while (simDebug_->events.size() > 200) simDebug_->events.pop_front();
    auto& journal = app_.simJournal();
    if (app_.simulation().attached()) journal.setCycle(app_.simulation().scanCount());
    (void)journal.add(SimSource::Debogage, SimSeverity::Info, kind, text, {},
                      !section.empty() && line > 0 ? SimJournal::goLine(section, line) : std::string("debogage"));
    if (kind == "arret" && status_) status_->setTransientMessage(text, 8.0, StatusBar::Severity::Warning);
}

std::vector<std::string> MainAnalysisScreen::debugEvents() const {
    if (!simDebug_) return {};
    return {simDebug_->events.begin(), simDebug_->events.end()};
}

} // namespace app
