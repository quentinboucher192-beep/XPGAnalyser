#include "SfcPane.hpp"

#include "App.hpp"
#include "GrafcetCodePanel.hpp"
#include "GrafcetPanels.hpp"
#include "screens/Screens.hpp"

#include "../project/EditCommands.hpp"
#include "../project/GraphListing.hpp"
#include "../sim/Runtime.hpp"
#include "../sim/SfcRun.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace app {

    using namespace ui;

    namespace {

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        // "2,6 s", "1 min 05 s"
        std::string elapsedText(std::int64_t ms) {
            char buf[48];
            if (ms < 10000) std::snprintf(buf, sizeof buf, "%lld,%lld s", static_cast<long long>(ms / 1000),
                                          static_cast<long long>((ms % 1000) / 100));
            else if (ms < 60000) std::snprintf(buf, sizeof buf, "%lld s", static_cast<long long>(ms / 1000));
            else std::snprintf(buf, sizeof buf, "%lld min %02lld s", static_cast<long long>(ms / 60000),
                               static_cast<long long>((ms / 1000) % 60));
            return buf;
        }

        InputText::Suggestion suggestion(std::string text, std::string detail, Icon icon) {
            InputText::Suggestion out;
            out.text = std::move(text);
            out.detail = std::move(detail);
            out.icon = icon;
            return out;
        }

        // Le debut du nom sous le curseur (une variable, un chemin a.b[2].c, une adresse).
        std::size_t wordStart(std::string_view before) {
            std::size_t i = before.size();
            while (i > 0) {
                const char ch = before[i - 1];
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '%' || ch == '['
                    || ch == ']')
                    --i;
                else break;
            }
            return i;
        }

    } // namespace

    SfcPane::SfcPane(App& app, domain::Index section, std::string paneId)
        : Widget(std::move(paneId)), app_(app), section_(section), alive_(std::make_shared<bool>(true)) {
        auto bar = std::make_unique<ToolBar>(this->id() + ".bar");
        auto tool = [&](const char* text, const char* action, Icon icon, const char* why) {
            bar->addButton(text, action, icon).setTooltip(why);
        };
        tool("+ \xC3\x89tape", "sfc.addStep", Icon::Section,
             "Sous l'\xC3\xA9tape choisie : une transition puis une \xC3\xA9tape (sous une transition : une \xC3\xA9tape puis "
             "une transition). Ce qui est dessous descend.");
        tool("Divergence OU", "sfc.alt", Icon::StepOnce,
             "Sous l'\xC3\xA9tape choisie : une branche de plus (une transition, une \xC3\xA9tape, une transition et un saut)");
        tool("Divergence ET", "sfc.par", Icon::StepOnce,
             "Sous la transition choisie : une \xC3\xA9tape de plus, active en m\xC3\xAAme temps (convergence en ET avant la "
             "transition suivante)");
        tool("Saut", "sfc.jump", Icon::Force, "Sous la transition choisie : un saut vers une \xC3\xA9tape");
        bar->addSeparator();
        tool("R\xC3\xA9" "ceptivit\xC3\xA9", "sfc.cond", Icon::StepOnce,
             "La r\xC3\xA9" "ceptivit\xC3\xA9 de la transition choisie : une variable (NOT X), ou une expression ST "
             "(elle va dans une section de transition)");
        tool("Action", "sfc.action", Icon::Play,
             "Une action de plus sur l'\xC3\xA9tape choisie (ou modifier l'action choisie) : qualificatif, variable ou "
             "section, dur\xC3\xA9" "e");
        tool("\xC3\x89tape\xE2\x80\xA6", "sfc.step", Icon::Section,
             "L'\xC3\xA9tape choisie : son nom (renommer), initiale, supervision (min, max), retard, commentaire");
        tool("Supprimer", "sfc.remove", Icon::Close, "Supprimer ce qui est choisi (Suppr). Ctrl+Z le remet.");
        bar->addSeparator();
        tool("\xE2\x88\x92", "sfc.zoomOut", Icon::None, "Zoom arri\xC3\xA8re (Ctrl+molette)");
        tool("+", "sfc.zoomIn", Icon::None, "Zoom avant (Ctrl+molette)");
        tool("Ajuster", "sfc.fit", Icon::None, "Tout le GRAFCET dans la fen\xC3\xAAtre");
        bar->setActionSink([this](core::ActionId a) { command(std::string(a)); });
        bar_ = &static_cast<ToolBar&>(addChild(std::move(bar)));

        auto split = std::make_unique<Splitter>(Orientation::Horizontal, this->id() + ".split");
        auto centre = std::make_unique<Splitter>(Orientation::Vertical, this->id() + ".centre");
        view_ = &static_cast<GrafcetView&>(centre->addPane(std::make_unique<GrafcetView>(this->id() + ".view"), 0.78f, 160.f));
        code_ = &static_cast<GrafcetCodePanel&>(
            centre->addPane(std::make_unique<GrafcetCodePanel>(this->id() + ".code"), 0.22f, 90.f));
        split->addPane(std::move(centre), 0.74f, 280.f);
        auto table = std::make_unique<TableView>(this->id() + ".props");
        props_ = &static_cast<TableView&>(split->addPane(std::move(table), 0.26f, 200.f));
        addChild(std::move(split));
        auto strip = std::make_unique<StatusBar>(this->id() + ".summary");
        status_ = &static_cast<StatusBar&>(addChild(std::move(strip)));

        propsModel_ = std::make_shared<GrafcetRowsModel>(std::vector<std::string>{ "Champ", "Valeur" });
        props_->setModel(propsModel_);
        props_->setColumns({ { "Champ", 110.f }, { "Valeur", 260.f } });

        // ---- le dessin : la grille de Control Expert, l'etat en simulation -------------
        GrafcetView::External ext;
        ext.build = [this](const grafcet::MeasureText& measure, const project::ChartLayout& pending) {
            return sfc::buildDiagram(chart_, topo_, measure, &grid_, &pending.steps);
        };
        auto stepNamed = [this](int id) -> const sim::SfcChartRun::Step* {
            const auto* run = liveChart();
            if (!run || id < 0 || static_cast<std::size_t>(id) >= chart_.items.size()) return nullptr;
            return run->stepNamed(chart_.items[static_cast<std::size_t>(id)].name);
        };
        auto transAt = [this](int id) -> const sim::SfcChartRun::Trans* {
            const auto* run = liveChart();
            if (!run || id < 0 || static_cast<std::size_t>(id) >= chart_.items.size()) return nullptr;
            const auto& it = chart_.items[static_cast<std::size_t>(id)];
            for (const auto& t : run->trans) if (t.x == it.x && t.y == it.y) return &t;
            return nullptr;
        };
        ext.stepActive = [stepNamed](int id) -> std::optional<bool> {
            const auto* s = stepNamed(id);
            if (!s) return std::nullopt;
            return s->active;
        };
        ext.stepTime = [stepNamed](int id) -> std::string {
            const auto* s = stepNamed(id);
            return s && s->active ? elapsedText(s->elapsed) : std::string{};
        };
        ext.stepFault = [stepNamed](int id) {
            const auto* s = stepNamed(id);
            return s && (s->tminErr || s->tmaxErr);
        };
        ext.transitionTrue = [transAt](int id) -> std::optional<bool> {
            const auto* t = transAt(id);
            if (!t) return std::nullopt;
            return t->value;
        };
        ext.transitionEnabled = [transAt](int id) {
            const auto* t = transAt(id);
            return t && t->enabled;
        };
        ext.actionRunning = [this](int id) -> std::optional<bool> {
            const auto* run = liveChart();
            const auto [step, rank] = sfc::actionOf(id);
            if (!run || step < 0 || static_cast<std::size_t>(step) >= chart_.items.size()) return std::nullopt;
            const auto& acts = chart_.items[static_cast<std::size_t>(step)].actions;
            if (rank < 0 || static_cast<std::size_t>(rank) >= acts.size()) return std::nullopt;
            const auto* a = run->action(acts[static_cast<std::size_t>(rank)].name);
            if (!a) return std::nullopt;
            return a->q;
        };
        ext.stepName = [this](int id) {
            return id >= 0 && static_cast<std::size_t>(id) < chart_.items.size() ? chart_.items[static_cast<std::size_t>(id)].name
                                                                                  : std::string("?");
        };
        ext.forceStep = [this](int id, bool on) {
            auto* rt = runtime();
            if (!rt || id < 0 || static_cast<std::size_t>(id) >= chart_.items.size()) return false;
            return rt->sfcSetStep(runtimeSection(), chart_.items[static_cast<std::size_t>(id)].name, on);
        };
        ext.init = [this] {
            auto* rt = runtime();
            return rt && rt->sfcInit(runtimeSection());
        };
        ext.tooltip = [this, stepNamed, transAt](GrafcetPart part, int id) -> std::string {
            if (part == GrafcetPart::Action) {
                const auto [step, rank] = sfc::actionOf(id);
                if (step < 0 || static_cast<std::size_t>(step) >= chart_.items.size()) return {};
                const auto& acts = chart_.items[static_cast<std::size_t>(step)].actions;
                if (rank < 0 || static_cast<std::size_t>(rank) >= acts.size()) return {};
                const auto& a = acts[static_cast<std::size_t>(rank)];
                return sfc::normalQualifier(a.qualifier) + " " + a.name + (a.time.empty() ? "" : " (" + a.time + ")")
                     + "\n" + sfc::qualifierMeaning(a.qualifier) + (a.isSection ? "\nsection d'action" : "\nvariable");
            }
            if (id < 0 || static_cast<std::size_t>(id) >= chart_.items.size()) return {};
            const auto& it = chart_.items[static_cast<std::size_t>(id)];
            std::string out;
            if (it.kind == sfc::ItemKind::Step) {
                out = "\xC3\x89tape " + it.name + (it.initial() ? " \xC2\xB7 initiale" : "");
                if (!it.tmin.empty() || !it.tmax.empty()) out += "\nsupervision : min " + (it.tmin.empty() ? "-" : it.tmin)
                                                               + ", max " + (it.tmax.empty() ? "-" : it.tmax);
                if (!it.delay.empty()) out += "\nretard : " + it.delay;
                if (!it.comment.empty()) out += "\n" + it.comment;
                if (const auto* s = stepNamed(id)) {
                    out += s->active ? "\nactive depuis " + elapsedText(s->elapsed) : std::string("\ninactive");
                    if (s->tminErr) out += "\nquitt\xC3\xA9" "e avant son temps minimal (tminErr)";
                    if (s->tmaxErr) out += "\nactive plus que son temps maximal (tmaxErr)";
                }
            }
            else if (it.kind == sfc::ItemKind::Transition) {
                out = "Transition (" + std::to_string(it.x) + ", " + std::to_string(it.y) + ") : " + sfc::conditionText(chart_, it);
                if (const auto* t = transAt(id)) {
                    out += std::string("\n") + (t->enabled ? "valid\xC3\xA9" "e" : "non valid\xC3\xA9" "e")
                         + (t->value ? (*t->value ? " \xC2\xB7 vraie" : " \xC2\xB7 fausse") : "");
                    if (!t->why.empty()) out += "\n" + t->why;
                }
            }
            else if (it.kind == sfc::ItemKind::Jump) out = "Saut vers " + it.name;
            else if (it.kind == sfc::ItemKind::TextBox) out = it.text;
            else if (it.kind == sfc::ItemKind::Link) out = "Lien";
            else if (it.isBar()) out = it.kind == sfc::ItemKind::AltBranch ? "Divergence en OU" : it.kind == sfc::ItemKind::AltJoint
                                     ? "Convergence en OU" : it.kind == sfc::ItemKind::ParBranch ? "Divergence en ET" : "Convergence en ET";
            for (const auto& [i, m] : topo_.problems) if (i == id) out += "\n\xE2\x9A\xA0 " + m;
            return out;
        };
        view_->setExternal(std::move(ext));

        // ---- ce que le dessin demande -------------------------------------------------
        links_ += view_->stepSelected->connect([this](int) { showSelection(); });
        links_ += view_->transitionSelected->connect([this](int) { showSelection(); });
        links_ += view_->actionSelected->connect([this](int) { showSelection(); });
        links_ += view_->itemSelected->connect([this](int) { showSelection(); });
        links_ += view_->nothingSelected->connect([this] { showSelection(); });
        links_ += view_->editRequested->connect([this](GrafcetPart part, int id) {
            if (part == GrafcetPart::Step) editStep(id);
            else if (part == GrafcetPart::Transition) editCondition(id);
            else if (part == GrafcetPart::Action) {
                const auto [step, rank] = sfc::actionOf(id);
                editAction(step, rank);
            }
        });
        links_ += view_->removeRequested->connect([this](GrafcetPart part, int id) { removePart(part, id); });
        links_ += view_->placementChanged->connect([this] { onDrop(); });
        links_ += code_->applyRequested->connect([this] { applyCode(); });

        // l'aide a la saisie du volet Code : les noms du projet
        code_->editor().setCompletionProvider([this](std::string_view prefix, std::vector<MultiLineText::Completion>& out) {
            auto doc = app_.project();
            if (!doc) return;
            for (const auto& s : project::suggestionsFor(*doc, section_, prefix)) {
                MultiLineText::Completion c;
                c.text = s.text;
                c.detail = s.detail;
                c.rank = s.rank;
                out.push_back(std::move(c));
            }
        });
        reload();
    }

    SfcPane::~SfcPane() { *alive_ = false; }

    sim::Runtime* SfcPane::runtime() const { return app_.simulationRuntime(); }

    std::string SfcPane::runtimeSection() const {
        auto doc = app_.project();
        if (!doc || section_ >= doc->sections.size()) return {};
        const auto& s = doc->sections[section_];
        std::string scope;
        if (s.owner < doc->pous.size() && doc->pous[s.owner].kind == domain::PouKind::ProgramUnit)
            scope = std::string(doc->strings.text(doc->pous[s.owner].name)) + ".";
        return scope + std::string(doc->strings.text(s.name));
    }

    const sim::SfcChartRun* SfcPane::liveChart() const {
        auto* rt = runtime();
        return rt ? rt->sfcChart(runtimeSection()) : nullptr;
    }

    void SfcPane::reload() {
        auto doc = app_.project();
        if (!doc || section_ >= doc->sections.size()) return;
        const auto& body = doc->sections[section_].body;
        const auto part = view_->selectedPart();
        const int id = view_->selectedId();
        std::string keepName;
        int keepX = -1, keepY = -1;
        if ((part == GrafcetPart::Step || part == GrafcetPart::Transition || part == GrafcetPart::Item) && id >= 0
            && static_cast<std::size_t>(id) < chart_.items.size()) {
            keepName = chart_.items[static_cast<std::size_t>(id)].name;
            keepX = chart_.items[static_cast<std::size_t>(id)].x;
            keepY = chart_.items[static_cast<std::size_t>(id)].y;
        }
        body_ = body;
        chart_ = sfc::parse(body);
        rebuild();
        // la meme selection, retrouvee par son nom ou sa case
        if (part == GrafcetPart::Step && !keepName.empty()) {
            if (const int s = chart_.findStep(keepName); s >= 0) view_->select(GrafcetPart::Step, s);
            else view_->select(GrafcetPart::None, -1);
        }
        else if (part == GrafcetPart::Transition) {
            const int t = chart_.itemAt(keepX, keepY);
            view_->select(t >= 0 && chart_.items[static_cast<std::size_t>(t)].kind == sfc::ItemKind::Transition
                              ? GrafcetPart::Transition : GrafcetPart::None, t);
        }
        else if (part != GrafcetPart::None) view_->select(GrafcetPart::None, -1);
        showSelection();
    }

    void SfcPane::rebuild() {
        topo_ = sfc::resolve(chart_);
        auto doc = app_.project();
        const std::string name = doc && section_ < doc->sections.size()
            ? std::string(doc->strings.text(doc->sections[section_].name)) : std::string("SFC");
        view_->setChart(sfc::asGrafcet(chart_, topo_, name));
        view_->setChecks(sfc::checks(chart_, topo_));
        view_->rebuildDiagram();
    }

    void SfcPane::say(const std::string& text, bool error) {
        lastMessage_ = text;
        if (status_) status_->setTransientMessage(text, 6.0, error ? StatusBar::Severity::Warning : StatusBar::Severity::Success);
    }

    std::set<std::string> SfcPane::takenNames() const {
        auto doc = app_.project();
        return doc ? sfc::stepNamesInProject(*doc, section_) : std::set<std::string>{};
    }

    std::string SfcPane::newStepName() const {
        // le motif des noms du graphe : X3 -> X, S_1_4 -> S_1_
        std::string prefix = "S_";
        int best = -1;
        for (const auto& it : chart_.items) {
            if (it.kind != sfc::ItemKind::Step) continue;
            std::size_t k = it.name.size();
            while (k > 0 && std::isdigit(static_cast<unsigned char>(it.name[k - 1]))) --k;
            if (k == it.name.size() || k == 0) continue;
            const int n = std::atoi(it.name.c_str() + k);
            if (n > best) { best = n; prefix = it.name.substr(0, k); }
        }
        return sfc::freeStepName(chart_, takenNames(), prefix);
    }

    std::string SfcPane::freeTransitionSection() const {
        auto doc = app_.project();
        std::set<std::string> taken;
        if (doc)
            for (const auto& s : doc->sections) taken.insert(upper(doc->strings.text(s.name)));
        const std::string base = (chart_.programName.empty() ? std::string("SFC") : chart_.programName) + "_T";
        return sfc::freeSourceName(chart_, base, taken);
    }

    bool SfcPane::apply(const sfc::EditResult& r, const std::string& label) {
        if (!r.ok) {
            say(r.message.empty() ? std::string("rien n'a chang\xC3\xA9") : r.message, true);
            reload();   // le modele a pu bouger avant le refus : on repart du fichier
            return false;
        }
        auto doc = app_.document();
        if (!doc || section_ >= doc->sections.size()) return false;
        const std::string next = sfc::write(chart_);
        if (next == doc->sections[section_].body) {
            say("rien n'a chang\xC3\xA9 dans le programme", true);
            reload();
            return false;
        }
        app_.apply(std::make_unique<project::SfcEditCommand>(doc, section_, next, label), /*refreshViews=*/false);
        // le modele relu dans ce qui vient d'etre ecrit (meme ordre des elements : `select` reste bon)
        body_ = doc->sections[section_].body;
        chart_ = sfc::parse(body_);
        rebuild();
        if (r.select >= 0 && static_cast<std::size_t>(r.select) < chart_.items.size()) {
            const auto& it = chart_.items[static_cast<std::size_t>(r.select)];
            view_->select(it.kind == sfc::ItemKind::Step ? GrafcetPart::Step
                          : it.kind == sfc::ItemKind::Transition ? GrafcetPart::Transition : GrafcetPart::Item,
                          r.select);
            view_->reveal(it.kind == sfc::ItemKind::Step ? GrafcetPart::Step : GrafcetPart::Transition, r.select);
        }
        else if (r.select < 0) view_->select(GrafcetPart::None, -1);
        showSelection();
        say(r.message + " \xC2\xB7 Ctrl+Z d\xC3\xA9" "fait");
        edited->emit();
        return true;
    }

    // ------------------------------------------------------------------ les gestes
    void SfcPane::command(const std::string& id) {
        const auto part = view_->selectedPart();
        const int sel = view_->selectedId();
        if (id == "sfc.zoomIn") { view_->setZoom(view_->zoom() * 1.2f); return; }
        if (id == "sfc.zoomOut") { view_->setZoom(view_->zoom() / 1.2f); return; }
        if (id == "sfc.fit") { view_->fit(); return; }
        if (id == "sfc.addStep") { (void)addStepBelowSelection(); return; }
        if (id == "sfc.alt") {
            if (part != GrafcetPart::Step) { say("choisis l'\xC3\xA9tape d'o\xC3\xB9 part la divergence en OU", true); return; }
            apply(sfc::addAlternative(chart_, sel, newStepName()), "Divergence en OU");
            return;
        }
        if (id == "sfc.par") {
            if (part != GrafcetPart::Transition) { say("choisis la transition d'o\xC3\xB9 part la divergence en ET", true); return; }
            apply(sfc::addSimultaneous(chart_, sel, newStepName()), "Divergence en ET");
            return;
        }
        if (id == "sfc.jump") { askJump(sel); return; }
        if (id == "sfc.cond") {
            if (part != GrafcetPart::Transition) { say("choisis une transition", true); return; }
            editCondition(sel);
            return;
        }
        if (id == "sfc.action") {
            if (part == GrafcetPart::Action) {
                const auto [step, rank] = sfc::actionOf(sel);
                editAction(step, rank);
            }
            else if (part == GrafcetPart::Step) editAction(sel, -1);
            else say("choisis une \xC3\xA9tape (ou une de ses actions)", true);
            return;
        }
        if (id == "sfc.step") {
            if (part != GrafcetPart::Step) { say("choisis une \xC3\xA9tape", true); return; }
            editStep(sel);
            return;
        }
        if (id == "sfc.remove") { (void)removeSelection(); return; }
    }

    bool SfcPane::selectItem(int item) {
        if (item < 0 || static_cast<std::size_t>(item) >= chart_.items.size()) return false;
        const auto& it = chart_.items[static_cast<std::size_t>(item)];
        const auto part = it.kind == sfc::ItemKind::Step ? GrafcetPart::Step
                        : it.kind == sfc::ItemKind::Transition ? GrafcetPart::Transition : GrafcetPart::Item;
        view_->select(part, item);
        if (part != GrafcetPart::Item) view_->reveal(part, item);
        showSelection();
        return true;
    }

    bool SfcPane::selectStep(std::string_view name) { return selectItem(chart_.findStep(name)); }

    bool SfcPane::selectAt(int x, int y) {
        if (const int i = chart_.itemAt(x, y); i >= 0) return selectItem(i);
        for (std::size_t i = 0; i < chart_.items.size(); ++i)
            if (chart_.items[i].isBar() && chart_.items[i].x == x && chart_.items[i].y == y) return selectItem(static_cast<int>(i));
        return false;
    }

    bool SfcPane::addStepBelowSelection(const std::string& condition) {
        const auto part = view_->selectedPart();
        if (part != GrafcetPart::Step && part != GrafcetPart::Transition) {
            say("choisis l'\xC3\xA9tape (ou la transition) sous laquelle ajouter", true);
            return false;
        }
        return apply(sfc::addStepBelow(chart_, view_->selectedId(), newStepName(), condition, freeTransitionSection()),
                     "Ajouter une \xC3\xA9tape");
    }

    bool SfcPane::setConditionOfSelection(const std::string& text) {
        if (view_->selectedPart() != GrafcetPart::Transition) { say("choisis une transition", true); return false; }
        return apply(sfc::setCondition(chart_, view_->selectedId(), text, freeTransitionSection()),
                     "R\xC3\xA9" "ceptivit\xC3\xA9");
    }

    bool SfcPane::addActionToSelection(const std::string& qualifier, const std::string& name, bool isSection,
                                       const std::string& time) {
        if (view_->selectedPart() != GrafcetPart::Step) { say("choisis une \xC3\xA9tape", true); return false; }
        const int step = view_->selectedId();
        auto acts = chart_.items[static_cast<std::size_t>(step)].actions;
        sfc::Action a;
        a.qualifier = sfc::normalQualifier(qualifier);
        a.name = name;
        a.isSection = isSection;
        a.time = time;
        acts.push_back(a);
        return apply(sfc::setActions(chart_, step, std::move(acts)), "Action");
    }

    bool SfcPane::removeSelection() {
        const auto part = view_->selectedPart();
        const int id = view_->selectedId();
        if (part == GrafcetPart::None || id < 0) { say("rien de choisi", true); return false; }
        removePart(part, id);
        return true;
    }

    void SfcPane::removePart(GrafcetPart part, int id) {
        if (part == GrafcetPart::Action) {
            const auto [step, rank] = sfc::actionOf(id);
            if (step < 0 || static_cast<std::size_t>(step) >= chart_.items.size()) return;
            auto acts = chart_.items[static_cast<std::size_t>(step)].actions;
            if (rank < 0 || static_cast<std::size_t>(rank) >= acts.size()) return;
            const std::string name = acts[static_cast<std::size_t>(rank)].name;
            acts.erase(acts.begin() + rank);
            auto r = sfc::setActions(chart_, step, std::move(acts));
            if (r.ok) r.message = "action " + name + " retir\xC3\xA9" "e";
            apply(r, "Retirer une action");
            return;
        }
        apply(sfc::removeItem(chart_, id), "Supprimer");
    }

    void SfcPane::onDrop() {
        const auto pending = view_->pendingPlacement();
        view_->setPlacement(nullptr);
        for (const auto& [id, p] : pending.steps) {
            if (id < 0 || static_cast<std::size_t>(id) >= chart_.items.size()) continue;
            const auto* ds = view_->diagram().step(id);
            const float w = ds ? ds->box.w : 60.f;
            const auto [cx, cy] = grid_.cellAt({ p.x + w / 2.f, p.y + 20.f });
            const auto& it = chart_.items[static_cast<std::size_t>(id)];
            if (cx == it.x && cy == it.y) { rebuild(); continue; }
            apply(sfc::moveItem(chart_, id, cx, cy), "D\xC3\xA9placer");
            return;
        }
        rebuild();
    }

    // ------------------------------------------------------------------ les dialogues
    void SfcPane::editCondition(int item) {
        if (item < 0 || static_cast<std::size_t>(item) >= chart_.items.size()) return;
        const auto& t = chart_.items[static_cast<std::size_t>(item)];
        std::string current = t.cond == sfc::CondKind::Section ? sfc::conditionText(chart_, t)
                            : (t.invert ? "NOT " : "") + t.condText;
        if (t.cond == sfc::CondKind::Section)
            if (const auto* s = chart_.source("transitionSource", t.condText); s && s->language != domain::PouLanguage::ST) {
                say("la r\xC3\xA9" "ceptivit\xC3\xA9 est la section " + t.condText + " en "
                        + std::string(domain::toString(s->language)) + " : elle se lit dans le volet Code",
                    true);
                return;
            }
        std::vector<FormDialog::Field> fields;
        fields.push_back({ "R\xC3\xA9" "ceptivit\xC3\xA9", current, "Marche, NOT Arret, ou une expression : S_2.t > t#3s AND Pret",
                           false, {} });
        auto dialog = std::make_unique<FormDialog>("dialog.sfcCondition", "R\xC3\xA9" "ceptivit\xC3\xA9 de la transition",
            "Une variable BOOL (NOT devant pour l'inverser) s'\xC3\xA9" "crit dans la transition, comme Control Expert. "
            "Une expression s'\xC3\xA9" "crit dans une section de transition ST (cr\xC3\xA9\xC3\xA9" "e au besoin).",
            std::move(fields), "Appliquer");
        dialog->setFieldAssist(0, [this](std::string_view before, std::size_t& from, std::vector<InputText::Suggestion>& out) {
            auto doc = app_.project();
            if (!doc) return;
            from = wordStart(before);
            const auto prefix = before.substr(from);
            int n = 0;
            for (const auto& s : project::suggestionsFor(*doc, section_, prefix)) {
                if (s.kind == project::Suggestion::Kind::Keyword || s.kind == project::Suggestion::Kind::Type) continue;
                InputText::Suggestion sug;
                sug.text = s.text;
                sug.detail = s.detail;
                sug.icon = Icon::Variable;
                out.push_back(std::move(sug));
                if (++n >= 40) break;
            }
            // les etapes du graphe (Etape.x, Etape.t)
            for (const auto& it : chart_.items)
                if (it.kind == sfc::ItemKind::Step && upper(it.name).rfind(upper(prefix), 0) == 0) {
                    out.push_back(suggestion(it.name + ".x", "\xC3\xA9tape active", Icon::Section));
                    out.push_back(suggestion(it.name + ".t", "temps d'activit\xC3\xA9", Icon::Section));
                }
        });
        const std::weak_ptr<bool> alive = alive_;
        const int x = t.x, y = t.y;
        app_.menus().ShowDialog(std::move(dialog), [this, alive, x, y](const menu::DialogResult& r) {
            const auto a = alive.lock();
            if (!a || !*a || !r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            reload();
            const int ti = chart_.itemAt(x, y);
            apply(sfc::setCondition(chart_, ti, v[0], freeTransitionSection()), "R\xC3\xA9" "ceptivit\xC3\xA9");
        });
    }

    void SfcPane::editStep(int item) {
        if (item < 0 || static_cast<std::size_t>(item) >= chart_.items.size()) return;
        const auto& s = chart_.items[static_cast<std::size_t>(item)];
        if (s.kind != sfc::ItemKind::Step) return;
        std::vector<FormDialog::Field> fields;
        fields.push_back({ "Nom", s.name, "lettres, chiffres, _", false, {} });
        fields.push_back({ "Initiale", s.initial() ? "Oui" : "Non", "", false, { "Oui", "Non" } });
        fields.push_back({ "Temps minimal", s.tmin, "t#1s : quitt\xC3\xA9" "e avant, tminErr", false, {} });
        fields.push_back({ "Temps maximal", s.tmax, "t#10s : active plus longtemps, tmaxErr", false, {} });
        fields.push_back({ "Retard", s.delay, "t#500ms : ses transitions attendent", false, {} });
        fields.push_back({ "Commentaire", s.comment, "", false, {} });
        auto dialog = std::make_unique<FormDialog>("dialog.sfcStep", "\xC3\x89tape " + s.name,
            "Renommer suit le nom dans les sauts et les sections ST du GRAFCET (Etape.x, Etape.t). La supervision "
            "signale une \xC3\xA9tape quitt\xC3\xA9" "e trop t\xC3\xB4t ou rest\xC3\xA9" "e trop longtemps ; le retard "
            "fait attendre ses transitions.",
            std::move(fields), "Appliquer");
        const std::weak_ptr<bool> alive = alive_;
        const std::string was = s.name;
        app_.menus().ShowDialog(std::move(dialog), [this, alive, was](const menu::DialogResult& r) {
            const auto a = alive.lock();
            if (!a || !*a || !r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 6) return;
            reload();
            int si = chart_.findStep(was);
            if (si < 0) return;
            std::string message;
            if (v[0] != was) {
                const auto taken = takenNames();
                if (taken.count(upper(v[0]))) {
                    say("une \xC3\xA9tape d'un autre GRAFCET s'appelle d\xC3\xA9j\xC3\xA0 " + v[0], true);
                    return;
                }
                auto r1 = sfc::renameStep(chart_, si, v[0]);
                if (!r1.ok) { say(r1.message, true); reload(); return; }
                message = r1.message;
            }
            auto r2 = sfc::setStepProperties(chart_, si, v[1] == "Oui", v[2], v[3], v[4], v[5]);
            if (!message.empty()) r2.message = message;
            apply(r2, "\xC3\x89tape " + was);
        });
    }

    void SfcPane::editAction(int step, int rank) {
        if (step < 0 || static_cast<std::size_t>(step) >= chart_.items.size()) return;
        const auto& s = chart_.items[static_cast<std::size_t>(step)];
        sfc::Action a;
        if (rank >= 0 && static_cast<std::size_t>(rank) < s.actions.size()) a = s.actions[static_cast<std::size_t>(rank)];
        std::vector<std::string> quals;
        for (const auto& q : sfc::qualifiers()) quals.push_back(q + " - " + sfc::qualifierMeaning(q));
        std::string currentQual = quals.front();
        for (const auto& q : quals)
            if (q.rfind(sfc::normalQualifier(a.qualifier) + " -", 0) == 0) currentQual = q;
        std::vector<FormDialog::Field> fields;
        fields.push_back({ "Qualificatif", currentQual, "", false, quals });
        fields.push_back({ "Pilote", a.isSection ? "une section d'action" : "une variable", "", false,
                           { "une variable", "une section d'action" } });
        fields.push_back({ "Nom", a.name, "la variable BOOL, ou la section (cr\xC3\xA9\xC3\xA9" "e en ST au besoin)", false, {} });
        fields.push_back({ "Dur\xC3\xA9" "e", a.time, "t#2s - pour L, D, DS, SD, SL", false, {} });
        auto dialog = std::make_unique<FormDialog>("dialog.sfcAction",
            rank >= 0 ? "Action de " + s.name : "Nouvelle action sur " + s.name,
            "N tant que l'\xC3\xA9tape est active ; S met \xC3\xA0 1 jusqu'\xC3\xA0 un R ; L pendant la dur\xC3\xA9" "e ; "
            "D apr\xC3\xA8s la dur\xC3\xA9" "e ; P / P1 un cycle \xC3\xA0 l'activation ; P0 un cycle \xC3\xA0 la "
            "d\xC3\xA9sactivation ; DS retard\xC3\xA9" "e puis m\xC3\xA9moris\xC3\xA9" "e.",
            std::move(fields), rank >= 0 ? "Appliquer" : "Ajouter");
        dialog->setFieldAssist(2, [this](std::string_view before, std::size_t& from, std::vector<InputText::Suggestion>& out) {
            auto doc = app_.project();
            if (!doc) return;
            from = wordStart(before);
            int n = 0;
            for (const auto& sug : project::suggestionsFor(*doc, section_, before.substr(from))) {
                if (sug.kind != project::Suggestion::Kind::Variable && sug.kind != project::Suggestion::Kind::LocatedVariable)
                    continue;
                out.push_back(suggestion(sug.text, sug.detail, Icon::Variable));
                if (++n >= 40) break;
            }
            for (const auto& src : chart_.sources)
                if (src.tag == "actionSource") out.push_back(suggestion(src.name, "section d'action", Icon::Section));
        });
        const std::weak_ptr<bool> alive = alive_;
        const std::string stepName = s.name;
        app_.menus().ShowDialog(std::move(dialog), [this, alive, stepName, rank](const menu::DialogResult& r) {
            const auto lock = alive.lock();
            if (!lock || !*lock || !r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 4) return;
            reload();
            const int si = chart_.findStep(stepName);
            if (si < 0) return;
            auto acts = chart_.items[static_cast<std::size_t>(si)].actions;
            sfc::Action act;
            act.qualifier = v[0].substr(0, v[0].find(' '));
            act.isSection = v[1] == "une section d'action";
            act.name = v[2];
            act.time = v[3];
            if (rank >= 0 && static_cast<std::size_t>(rank) < acts.size()) acts[static_cast<std::size_t>(rank)] = act;
            else acts.push_back(act);
            apply(sfc::setActions(chart_, si, std::move(acts)), "Action de " + stepName);
        });
    }

    void SfcPane::askJump(int item) {
        const auto part = view_->selectedPart();
        if ((part != GrafcetPart::Transition && part != GrafcetPart::Step) || item < 0) {
            say("choisis la transition (ou l'\xC3\xA9tape) o\xC3\xB9 mettre le saut", true);
            return;
        }
        std::vector<std::string> steps;
        for (const auto& it : chart_.items) if (it.kind == sfc::ItemKind::Step) steps.push_back(it.name);
        if (steps.empty()) return;
        std::vector<FormDialog::Field> fields;
        fields.push_back({ "Vers l'\xC3\xA9tape", steps.front(), "", false, steps });
        const std::weak_ptr<bool> alive = alive_;
        const int x = chart_.items[static_cast<std::size_t>(item)].x, y = chart_.items[static_cast<std::size_t>(item)].y;
        app_.menus().ShowDialog(
            std::make_unique<FormDialog>("dialog.sfcJump", "Saut",
                "Le saut m\xC3\xA8ne \xC3\xA0 une \xC3\xA9tape sans tracer la liaison (Control Expert : jumpSFC).",
                std::move(fields), "Ajouter"),
            [this, alive, x, y](const menu::DialogResult& r) {
                const auto a = alive.lock();
                if (!a || !*a || !r.accepted()) return;
                const auto v = FormDialog::split(r.payload);
                if (v.empty()) return;
                reload();
                apply(sfc::addJump(chart_, chart_.itemAt(x, y), v[0]), "Saut");
            });
    }

    // ------------------------------------------------------------------ le volet Code
    void SfcPane::applyCode() {
        if (!code_->dirty()) return;
        const std::string text = code_->text();
        if (code_->target() == GrafcetCodePanel::Target::Transition) {
            const int ti = code_->targetId();
            if (ti < 0 || static_cast<std::size_t>(ti) >= chart_.items.size()) return;
            const auto& t = chart_.items[static_cast<std::size_t>(ti)];
            if (t.cond == sfc::CondKind::Section) {
                if (apply(sfc::setSourceBody(chart_, "transitionSource", t.condText, text), "R\xC3\xA9" "ceptivit\xC3\xA9"))
                    code_->markApplied(text);
                return;
            }
            std::string flat;
            for (const char ch : text) flat.push_back(ch == '\r' || ch == '\n' ? ' ' : ch);
            while (!flat.empty() && (flat.back() == ' ' || flat.back() == ';')) flat.pop_back();
            if (apply(sfc::setCondition(chart_, ti, flat, freeTransitionSection()), "R\xC3\xA9" "ceptivit\xC3\xA9"))
                code_->markApplied(text);
            return;
        }
        if (code_->target() == GrafcetCodePanel::Target::Action) {
            const auto [step, rank] = sfc::actionOf(code_->targetId());
            if (step < 0 || static_cast<std::size_t>(step) >= chart_.items.size()) return;
            const auto& acts = chart_.items[static_cast<std::size_t>(step)].actions;
            if (rank < 0 || static_cast<std::size_t>(rank) >= acts.size()) return;
            if (apply(sfc::setSourceBody(chart_, "actionSource", acts[static_cast<std::size_t>(rank)].name, text),
                      "Section d'action"))
                code_->markApplied(text);
        }
    }

    // ------------------------------------------------------------------ les Proprietes
    void SfcPane::showSelection() {
        std::vector<std::vector<std::string>> rows;
        auto add = [&](std::string field, std::string value) { rows.push_back({ std::move(field), std::move(value) }); };
        const auto part = view_->selectedPart();
        const int id = view_->selectedId();
        auto names = [this](const std::vector<int>& items) {
            std::string out;
            for (const int i : items)
                if (i >= 0 && static_cast<std::size_t>(i) < chart_.items.size())
                    out += (out.empty() ? "" : ", ") + chart_.items[static_cast<std::size_t>(i)].name;
            return out.empty() ? std::string("-") : out;
        };
        const sim::SfcChartRun* run = liveChart();
        bool codeShown = false;
        if (part == GrafcetPart::Step && id >= 0 && static_cast<std::size_t>(id) < chart_.items.size()) {
            const auto& s = chart_.items[static_cast<std::size_t>(id)];
            add("\xC3\x89tape", s.name + (s.initial() ? "  \xC2\xB7  initiale" : "")
                                   + (s.stepKind == sfc::StepKind::Macro ? "  \xC2\xB7  macro-\xC3\xA9tape" : ""));
            add("Case", std::to_string(s.x) + ", " + std::to_string(s.y));
            std::string in, out;
            for (const int t : topo_.into(id)) in += (in.empty() ? "" : ", ") + sfc::conditionText(chart_, chart_.items[static_cast<std::size_t>(t)]);
            for (const int t : topo_.outOf(id)) out += (out.empty() ? "" : ", ") + sfc::conditionText(chart_, chart_.items[static_cast<std::size_t>(t)]);
            add("Entr\xC3\xA9" "e(s)", in.empty() ? "-" : in);
            add("Sortie(s)", out.empty() ? "-" : out);
            for (std::size_t k = 0; k < s.actions.size(); ++k) {
                const auto& a = s.actions[k];
                add(k == 0 ? "Actions" : "", sfc::normalQualifier(a.qualifier) + "  " + a.name
                                               + (a.time.empty() ? "" : "  " + a.time) + (a.isSection ? "  (section)" : ""));
            }
            if (!s.tmin.empty() || !s.tmax.empty())
                add("Supervision", "min " + (s.tmin.empty() ? std::string("-") : s.tmin) + "  \xC2\xB7  max "
                                       + (s.tmax.empty() ? std::string("-") : s.tmax));
            if (!s.delay.empty()) add("Retard", s.delay);
            if (!s.comment.empty()) add("Commentaire", s.comment);
            add("Variables", s.name + ".x, " + s.name + ".t");
            if (run)
                if (const auto* st = run->stepNamed(s.name))
                    add("En simulation", (st->active ? "active depuis " + elapsedText(st->elapsed) : std::string("inactive"))
                                             + (st->tminErr ? "  \xC2\xB7  tminErr" : "") + (st->tmaxErr ? "  \xC2\xB7  tmaxErr" : ""));
            for (std::size_t k = 0; k < s.actions.size() && !codeShown; ++k) {
                const auto& a = s.actions[k];
                if (!a.isSection) continue;
                if (const auto* src = chart_.source("actionSource", a.name); src && src->language == domain::PouLanguage::ST) {
                    code_->show(GrafcetCodePanel::Target::Action, sfc::actionId(id, static_cast<int>(k)),
                                "Section d'action " + a.name + " (ST)", src->body, "Appliquer l'\xC3\xA9" "crit dans le GRAFCET");
                    code_->editor().setReadOnly(false);
                    codeShown = true;
                }
            }
        }
        else if (part == GrafcetPart::Transition && id >= 0 && static_cast<std::size_t>(id) < chart_.items.size()) {
            const auto& t = chart_.items[static_cast<std::size_t>(id)];
            const auto* tl = topo_.of(id);
            add("Transition", "case " + std::to_string(t.x) + ", " + std::to_string(t.y));
            add("R\xC3\xA9" "ceptivit\xC3\xA9", sfc::conditionText(chart_, t));
            add("De", tl ? names(tl->sources) + (tl->andJoin ? "  (convergence en ET)" : "") : "-");
            add("Vers", tl ? names(tl->targets) + (tl->andSplit ? "  (divergence en ET)" : "") : "-");
            const sfc::Source* src = t.cond == sfc::CondKind::Section ? chart_.source("transitionSource", t.condText) : nullptr;
            if (src) add("Section", src->name + " (" + std::string(domain::toString(src->language)) + ")");
            if (run)
                for (const auto& tr : run->trans)
                    if (tr.x == t.x && tr.y == t.y)
                        add("En simulation", std::string(tr.enabled ? "valid\xC3\xA9" "e" : "non valid\xC3\xA9" "e")
                                                 + (tr.value ? (*tr.value ? "  \xC2\xB7  vraie" : "  \xC2\xB7  fausse") : "")
                                                 + (tr.why.empty() ? "" : "  \xC2\xB7  " + tr.why));
            if (src && src->language == domain::PouLanguage::ST) {
                code_->show(GrafcetCodePanel::Target::Transition, id, "Section de transition " + src->name + " (ST)", src->body,
                            "une expression BOOL ; Appliquer l'\xC3\xA9" "crit dans le GRAFCET");
                code_->editor().setReadOnly(false);
            }
            else if (src) {
                domain::Section fake;
                fake.language = src->language;
                fake.body = src->body;
                std::string listing;
                for (const auto& line : project::graphListing(fake)) listing += line.text + "\n";
                code_->show(GrafcetCodePanel::Target::None, id, "Section de transition " + src->name + " ("
                                + std::string(domain::toString(src->language)) + ", lecture)", listing,
                            "un sch\xC3\xA9ma : la bobine " + src->name + " est la r\xC3\xA9" "ceptivit\xC3\xA9");
                code_->editor().setReadOnly(true);
            }
            else {
                code_->show(GrafcetCodePanel::Target::Transition, id, "R\xC3\xA9" "ceptivit\xC3\xA9",
                            (t.invert ? "NOT " : "") + t.condText,
                            "une variable (NOT X), ou une expression (elle part dans une section de transition ST)");
                code_->editor().setReadOnly(false);
            }
            codeShown = true;
        }
        else if (part == GrafcetPart::Action) {
            const auto [step, rank] = sfc::actionOf(id);
            if (step >= 0 && static_cast<std::size_t>(step) < chart_.items.size()) {
                const auto& acts = chart_.items[static_cast<std::size_t>(step)].actions;
                if (rank >= 0 && static_cast<std::size_t>(rank) < acts.size()) {
                    const auto& a = acts[static_cast<std::size_t>(rank)];
                    add("Action", sfc::normalQualifier(a.qualifier) + "  " + a.name);
                    add("Sens", sfc::qualifierMeaning(a.qualifier));
                    if (!a.time.empty()) add("Dur\xC3\xA9" "e", a.time);
                    add("\xC3\x89tape", chart_.items[static_cast<std::size_t>(step)].name);
                    add("Pilote", a.isSection ? "la section d'action " + a.name : "la variable " + a.name);
                    if (run)
                        if (const auto* ac = run->action(a.name)) add("En simulation", ac->q ? "en cours" : "arr\xC3\xAAt\xC3\xA9" "e");
                    if (a.isSection)
                        if (const auto* src = chart_.source("actionSource", a.name); src && src->language == domain::PouLanguage::ST) {
                            code_->show(GrafcetCodePanel::Target::Action, id, "Section d'action " + a.name + " (ST)", src->body,
                                        "Appliquer l'\xC3\xA9" "crit dans le GRAFCET");
                            code_->editor().setReadOnly(false);
                            codeShown = true;
                        }
                }
            }
        }
        else if (part == GrafcetPart::Item && id >= 0 && static_cast<std::size_t>(id) < chart_.items.size()) {
            const auto& it = chart_.items[static_cast<std::size_t>(id)];
            for (const auto& box : view_->diagram().items)
                if (box.id == id) add("\xC3\x89l\xC3\xA9ment", box.what);
            add("Case", std::to_string(it.x) + ", " + std::to_string(it.y));
            if (it.isBar()) add("Largeur", std::to_string(it.width) + " colonne(s)");
        }
        else {
            int steps = 0, trans = 0, actions = 0;
            for (const auto& it : chart_.items) {
                steps += it.kind == sfc::ItemKind::Step;
                trans += it.kind == sfc::ItemKind::Transition;
                actions += static_cast<int>(it.actions.size());
            }
            add("GRAFCET", chart_.programName.empty() ? std::string("-") : chart_.programName);
            add("Contenu", std::to_string(steps) + " \xC3\xA9tapes, " + std::to_string(trans) + " transitions, "
                               + std::to_string(actions) + " actions");
            add("Sections", std::to_string(chart_.sources.size()) + " (transitions et actions)");
            if (!chart_.editable) add("Modification", "impossible ici : " + (chart_.error.empty() ? std::string("pas de r\xC3\xA9seau") : chart_.error));
            if (run) add("En simulation", std::to_string(run->evolutions) + " \xC3\xA9volution(s)");
        }
        for (const auto& [i, m] : topo_.problems)
            if (i == id || (i < 0 && part == GrafcetPart::None)) add("\xE2\x9A\xA0", m);
        if (!codeShown && part != GrafcetPart::Transition)
            code_->showNothing("Choisis une transition (sa r\xC3\xA9" "ceptivit\xC3\xA9) ou une \xC3\xA9tape (sa section d'action).");
        propsModel_->setRows(std::move(rows));
        props_->invalidate();
    }

    std::string SfcPane::describe() const {
        std::string out = "SFC " + chart_.programName + " : " + std::to_string(chart_.steps().size()) + " etapes, "
                        + std::to_string(chart_.transitions().size()) + " transitions, "
                        + std::to_string(topo_.problems.size()) + " probleme(s)";
        const auto part = view_->selectedPart();
        const int id = view_->selectedId();
        if (part != GrafcetPart::None && id >= 0 && static_cast<std::size_t>(id) < chart_.items.size())
            out += " ; choisi : " + std::string(part == GrafcetPart::Step ? "etape " : part == GrafcetPart::Transition ? "transition " : "element ")
                 + chart_.items[static_cast<std::size_t>(id)].name;
        if (const auto* run = liveChart()) {
            out += " ; actives :";
            for (const auto& n : run->activeSteps()) out += " " + n;
        }
        return out;
    }

    // ------------------------------------------------------------------ les scripts
    bool runSfcScript(App& app, SfcPane& pane, const std::vector<std::string>& w, std::string& said, std::string& error) {
        auto arg = [&](std::size_t i) { return i < w.size() ? w[i] : std::string{}; };
        const std::string cmd = arg(0);
        auto isNumber = [](const std::string& t) {
            return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '-'; });
        };
        auto result = [&](bool ok) {
            if (!ok) error = pane.lastMessage().empty() ? std::string("refus\xC3\xA9") : pane.lastMessage();
            else said = pane.lastMessage();
            return ok;
        };
        if (cmd == "sfc-choisir") {
            bool ok = isNumber(arg(1)) && isNumber(arg(2)) ? pane.selectAt(std::atoi(arg(1).c_str()), std::atoi(arg(2).c_str()))
                                                         : pane.selectStep(arg(1));
            if (!ok) error = "rien \xC3\xA0 choisir : " + arg(1) + " " + arg(2);
            return ok;
        }
        if (cmd == "sfc-outil") { pane.command(arg(1)); said = pane.lastMessage(); return true; }
        if (cmd == "sfc-ajouter") return result(pane.addStepBelowSelection(arg(1)));
        if (cmd == "sfc-receptivite") return result(pane.setConditionOfSelection(arg(1)));
        if (cmd == "sfc-action") {
            const bool section = arg(3) == "section" || arg(4) == "section";
            const std::string time = arg(3) == "section" ? std::string{} : arg(3);
            return result(pane.addActionToSelection(arg(1), arg(2), section, time));
        }
        if (cmd == "sfc-supprimer") return result(pane.removeSelection());
        if (cmd == "sfc-annuler") {
            const auto r = app.actions().trigger("edit.undo", app.commands());
            pane.reload();
            if (!r) error = r.error().message();
            return static_cast<bool>(r);
        }
        if (cmd == "sfc-forcer" || cmd == "sfc-initialiser") {
            auto* rt = app.simulationRuntime();
            if (!rt) { error = "la simulation ne tourne pas"; return false; }
            const auto doc = app.project();
            std::string section;
            if (doc && pane.section() < doc->sections.size()) section = std::string(doc->strings.text(doc->sections[pane.section()].name));
            const bool ok = cmd == "sfc-initialiser" ? rt->sfcInit(section) : rt->sfcSetStep(section, arg(1), arg(2) != "0");
            if (!ok) error = "refus\xC3\xA9 par la simulation";
            return ok;
        }
        if (cmd == "sfc-etat") {
            said = pane.describe();
            if (!arg(1).empty() && said.find(arg(1)) == std::string::npos) {
                error = "attendu \"" + arg(1) + "\" dans : " + said;
                return false;
            }
            return true;
        }
        error = "commande inconnue";
        return false;
    }

    // ------------------------------------------------------------------ mise en page
    void SfcPane::onLayout() {
        const auto area = contentRect();
        const float barH = 30.f, stripH = 22.f;
        if (children().size() < 3) return;
        children()[0]->setBounds({ area.x, area.y, area.w, barH });
        children()[1]->setBounds({ area.x, area.y + barH + 2.f, area.w, std::max(0.f, area.h - barH - stripH - 6.f) });
        children()[2]->setBounds({ area.x, area.bottom() - stripH, area.w, stripH });
    }

    void SfcPane::onPaint(const PaintContext& ctx) {
        // la simulation va et vient : le dessin la suit a chaque image
        auto* rt = runtime();
        view_->setRuntime(rt && liveChart() ? rt : nullptr);
        // un corps change ailleurs (Ctrl+Z, une autre vue) : relu
        if (auto doc = app_.project(); doc && section_ < doc->sections.size() && doc->sections[section_].body != body_)
            reload();
        // "Aller a" (une reference croisee, la recherche) met le curseur du volet Texte sur
        // une ligne : le schema choisit l'element de cette ligne.
        if (auto* host = parent()) {
            if (auto* text = dynamic_cast<MultiLineText*>(host->findById(host->id() + ".code"))) {
                const auto line = text->caretLine();
                if (caretSeen_ && line != caretLine_) {
                    auto doc = app_.project();
                    if (doc && section_ < doc->sections.size()) {
                        const auto& sec = doc->sections[section_];
                        const auto head = project::graphListingHeaderLines(sec);
                        const auto lines = project::graphListing(sec);
                        if (line >= head && line - head < lines.size()) {
                            const auto& g = lines[line - head];
                            if (g.cellX >= 0 && g.cellY >= 0) (void)selectAt(g.cellX, g.cellY);
                        }
                    }
                }
                caretLine_ = line;
                caretSeen_ = true;
            }
        }
        std::string line;
        int steps = 0, trans = 0;
        for (const auto& it : chart_.items) {
            steps += it.kind == sfc::ItemKind::Step;
            trans += it.kind == sfc::ItemKind::Transition;
        }
        line = std::to_string(steps) + " \xC3\xA9tapes \xC2\xB7 " + std::to_string(trans) + " transitions";
        if (!topo_.problems.empty())
            line += " \xC2\xB7 " + std::to_string(topo_.problems.size()) + " probl\xC3\xA8me(s)";
        if (const auto* run = liveChart()) {
            std::string act;
            for (const auto& s : run->steps)
                if (s.active) act += (act.empty() ? "" : ", ") + s.name + " (" + elapsedText(s.elapsed) + ")";
            line += "  \xC2\xB7  en simulation : " + (act.empty() ? std::string("aucune \xC3\xA9tape active") : "actives " + act);
        }
        if (!chart_.editable) line += "  \xC2\xB7  lecture seule : " + (chart_.error.empty() ? std::string("pas de r\xC3\xA9seau") : chart_.error);
        if (line != statusLine_) {
            statusLine_ = line;
            status_->setMessage(line, topo_.problems.empty() ? gfx::Color{} : gfx::Color::rgb(0xE8C46F));
        }
        (void)ctx;
    }

} // namespace app
