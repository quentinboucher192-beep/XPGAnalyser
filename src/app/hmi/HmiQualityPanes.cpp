// IHM > Essais : les essais de reception (lot 13).
#include "HmiQualityPanes.hpp"
#include "../../core/Edition.hpp"   // 1.12.2 : XPGAnalyser IHM n'a pas d'automate
#include "../ExportTarget.hpp"
#include "HmiAssist.hpp"
#include "HmiPaneKit.hpp"

#include "../../hmi/HmiHistory.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;
using namespace hmikit;

namespace {

enum : int { TNew = 1, TDup, TDel, TStepAdd, TStepDel, TStepUp, TStepDown, TPlay, TStop, TRunAll, TPdf, TXlsx };

ui::Tone verdictTone(hmi::Verdict v) {
    switch (v) {
        case hmi::Verdict::Passed:  return ui::Tone::Ok;
        case hmi::Verdict::Failed:  return ui::Tone::Error;
        case hmi::Verdict::Error:   return ui::Tone::Error;
        case hmi::Verdict::Skipped: return ui::Tone::Muted;
        case hmi::Verdict::Info:    return ui::Tone::Muted;
        case hmi::Verdict::Pending: break;
    }
    return ui::Tone::None;
}

// Un nom sur : lettres, chiffres, _ ; unique parmi les essais.
std::string freeName(const hmi::Project& p, std::string wanted) {
    std::string base;
    for (const char c : wanted) base += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
    if (base.empty()) base = "Essai";
    std::string name = base;
    for (int k = 2; p.scenarioByName(name); ++k) name = base + "_" + std::to_string(k);
    return name;
}

std::string fileSafe(std::string s) {
    for (auto& c : s)
        if (std::string_view("\\/:*?\"<>| ").find(c) != std::string_view::npos) c = '_';
    return s;
}

} // namespace

HmiScenariosPane::HmiScenariosPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TNew, HmiGlyph::Plus, "Nouvel essai de r\xC3\xA9" "ception", "Nouvel essai");
    tools->add(TDup, HmiGlyph::Duplicate, "Dupliquer l'essai", "Dupliquer");
    tools->add(TDel, HmiGlyph::Delete, "Supprimer l'essai (Ctrl+Z le rend)", "Supprimer");
    tools->separator();
    tools->add(TStepAdd, HmiGlyph::Plus, "Ajouter un pas (apr\xC3\xA8s celui qui est choisi)", "Ajouter un pas");
    tools->add(TStepDel, HmiGlyph::Delete, "Supprimer le pas choisi", "Supprimer le pas");
    tools->add(TStepUp, HmiGlyph::Up, "Monter le pas");
    tools->add(TStepDown, HmiGlyph::Down, "Descendre le pas");
    tools->separator();
    tools->add(TPlay, HmiGlyph::Play, "Lancer l'essai dans la simulation : l'IHM red\xC3\xA9marre et rejoue chaque pas \xC3\xA0 l'\xC3\xA9" "cran", "Lancer");
    tools->add(TStop, HmiGlyph::Stop, "Arr\xC3\xAAter l'essai en cours (les pas restants : non jou\xC3\xA9s)", "Arr\xC3\xAAter");
    tools->add(TRunAll, HmiGlyph::Check,
               core::hasApi() ? "Tout lancer, sans \xC3\xA9" "cran : chaque essai d'un coup, au temps simul\xC3\xA9 (reli\xC3\xA9 \xC3\xA0 l'automate simul\xC3\xA9 s'il tourne)"
                              : "Tout lancer, sans \xC3\xA9" "cran : chaque essai d'un coup, au temps simul\xC3\xA9",   // 1.12.2
               "Tout lancer");
    tools->separator();
    tools->add(TPdf, HmiGlyph::Export, "Rapport PDF : l'essai choisi, sinon toute la campagne (dossier exports/)", "Rapport PDF");
    tools->add(TXlsx, HmiGlyph::Export, "Rapport Excel : l'essai choisi, sinon toute la campagne (dossier exports/)", "Rapport Excel");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    for (int a : {TDup, TDel, TStepAdd, TPlay}) tools_->setEnabledWhen(a, [this] { return selectedScenario() != kNoId; });
    for (int a : {TStepDel, TStepUp, TStepDown}) tools_->setEnabledWhen(a, [this] { return selectedStep() >= 0; });
    tools_->setEnabledWhen(TRunAll, [this] { return !doc_->project.scenarios.empty(); });

    auto list = std::make_unique<ui::TableView>(base + ".list");
    list->setColumns({{"Essai", 190.f}, {"Pas", 50.f, 40.f, true, true, true, ui::Align::End}, {"Dernier passage", 230.f}});
    list->setSelectionMode(ui::SelectionMode::Single);
    list_ = &static_cast<ui::TableView&>(addChild(std::move(list)));
    auto steps = std::make_unique<ui::TableView>(base + ".steps");
    steps->setColumns({{"N\xC2\xB0", 44.f, 36.f, true, false, true, ui::Align::End}, {"Pas", 330.f}, {"Verdict", 96.f}, {"D\xC3\xA9tail", 330.f}});
    steps->setSelectionMode(ui::SelectionMode::Single);
    steps_ = &static_cast<ui::TableView&>(addChild(std::move(steps)));
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sc = selectedScenario();
        const int st = selectedStep();
        switch (a) {
            case TNew: selectScenario(addScenario()); break;
            case TDup: if (sc != kNoId) selectScenario(duplicateScenario(sc)); break;
            case TDel:
                if (sc == kNoId) break;
                if (hosts_.remove) hosts_.remove(sc);
                else (void)removeScenario(sc);
                break;
            case TStepAdd:
                if (sc != kNoId) {
                    const int at = addStep(sc, {}, st);
                    if (at >= 0) selectStep(at);
                }
                break;
            case TStepDel: if (sc != kNoId && st >= 0) (void)removeStep(sc, st); break;
            case TStepUp: if (sc != kNoId && st >= 0 && moveStep(sc, st, -1)) selectStep(st - 1); break;
            case TStepDown: if (sc != kNoId && st >= 0 && moveStep(sc, st, +1)) selectStep(st + 1); break;
            case TPlay:
                if (sc == kNoId) break;
                if (hosts_.play) {
                    hosts_.play(sc);
                    say("Essai lanc\xC3\xA9 dans la simulation : les verdicts arrivent ici au fil des pas.");
                } else {
                    say("La simulation n'est pas disponible ici : Tout lancer joue les essais sans \xC3\xA9" "cran.", true);
                }
                break;
            case TStop:
                if (hosts_.stop) hosts_.stop();
                break;
            case TRunAll: (void)runAll(); break;
            // Lot 7 : ou exporter (ExportTarget.hpp) - exports/ par defaut, le bouton ... ailleurs.
            case TPdf:
                if (!askExportTarget("le rapport d'essais (PDF)", "Documents PDF|*.pdf", [this] { (void)exportReport(hmi::ExportFormat::Pdf); }))
                    (void)exportReport(hmi::ExportFormat::Pdf);
                break;
            case TXlsx:
                if (!askExportTarget("le rapport d'essais (Excel)", "Classeurs Excel|*.xlsx", [this] { (void)exportReport(hmi::ExportFormat::Excel); }))
                    (void)exportReport(hmi::ExportFormat::Excel);
                break;
            default: break;
        }
    });
    links_ += list_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (refreshing_) return;
        refreshSteps();
        rebuildProperties();
    });
    links_ += steps_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    links_ += doc_->reported->connect([this](Id) { refresh(); });
    refresh();
}

void HmiScenariosPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

Id HmiScenariosPane::selectedScenario() const {
    const auto rows = list_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? kNoId : order_[rows.front()];
}

int HmiScenariosPane::selectedStep() const {
    const auto* sc = doc_->project.scenario(shown_);
    const auto rows = steps_->selectedModelRows();
    if (!sc || rows.empty() || rows.front() >= sc->steps.size()) return -1;
    return static_cast<int>(rows.front());
}

void HmiScenariosPane::selectScenario(Id id) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == id) list_->selectModelRows({static_cast<ui::RowIndex>(i)});
    refreshSteps();
    rebuildProperties();
}

void HmiScenariosPane::selectStep(int index) {
    if (index >= 0) steps_->selectModelRows({static_cast<ui::RowIndex>(index)});
    else steps_->selectModelRows({});
    rebuildProperties();
}

void HmiScenariosPane::refresh() {
    refreshing_ = true;
    const Id keep = selectedScenario();
    const int keepStep = selectedStep();
    const auto& p = doc_->project;
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::Tone> tones;
    std::size_t passed = 0, failed = 0, played = 0;
    for (const auto& sc : p.scenarios) {
        order_.push_back(sc.id);
        const auto it = doc_->reports.find(sc.id);
        std::string last = "pas encore jou\xC3\xA9";
        ui::Tone tone = ui::Tone::Muted;
        if (it != doc_->reports.end()) {
            const auto& r = it->second;
            last = (r.done ? (r.ok() ? "r\xC3\xA9ussi \xC2\xB7 " : "\xC3\xA9" "chec \xC2\xB7 ") : std::string{}) + r.summary();
            tone = !r.done ? ui::Tone::Accent : r.ok() ? ui::Tone::Ok : ui::Tone::Error;
            if (r.done) {
                ++played;
                (r.ok() ? passed : failed) += 1;
            }
        }
        rows.push_back({sc.name, std::to_string(sc.steps.size()), last});
        tones.push_back(tone);
    }
    listModel_ = std::make_shared<Rows>(std::vector<std::string>{"Essai", "Pas", "Dernier passage"}, std::move(rows),
                                        [tones](ui::RowIndex r, std::size_t c) {
                                            ui::CellStyle s;
                                            if (r >= tones.size()) return s;
                                            if (c == 0) { s.icon = ui::Icon::Ok; s.iconTone = tones[r]; s.bold = true; }
                                            if (c == 2) s.fgTone = tones[r];
                                            return s;
                                        });
    list_->setModel(listModel_);
    if (keep != kNoId)
        for (std::size_t i = 0; i < order_.size(); ++i)
            if (order_[i] == keep) list_->selectModelRows({static_cast<ui::RowIndex>(i)});
    if (selectedScenario() == kNoId && !order_.empty()) list_->selectModelRows({0});
    refreshing_ = false;
    refreshSteps();
    if (keepStep >= 0 && selectedScenario() == keep) {
        const auto* sc = p.scenario(keep);
        if (sc && keepStep < static_cast<int>(sc->steps.size())) steps_->selectModelRows({static_cast<ui::RowIndex>(keepStep)});
    }
    rebuildProperties();
    std::string msg = std::to_string(p.scenarios.size()) + " essai(s)";
    if (played) msg += " \xC2\xB7 dernier passage : " + std::to_string(passed) + " r\xC3\xA9ussi(s), " + std::to_string(failed) + " en \xC3\xA9" "chec";
    status_->setMessage(msg, failed ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiScenariosPane::refreshSteps() {
    const bool was = refreshing_;
    refreshing_ = true;
    shown_ = selectedScenario();
    const auto* sc = doc_->project.scenario(shown_);
    std::vector<std::vector<std::string>> rows;
    std::vector<hmi::Verdict> verdicts;
    const hmi::ScenarioReport* rep = nullptr;
    if (sc)
        if (const auto it = doc_->reports.find(sc->id); it != doc_->reports.end()) rep = &it->second;
    if (sc)
        for (std::size_t i = 0; i < sc->steps.size(); ++i) {
            const hmi::StepResult r = rep && i < rep->steps.size() && rep->steps.size() == sc->steps.size() ? rep->steps[i] : hmi::StepResult{};
            rows.push_back({std::to_string(i + 1), hmi::describeStep(sc->steps[i]),
                            r.verdict == hmi::Verdict::Pending ? std::string{} : std::string(hmi::verdictLabel(r.verdict)), r.detail});
            verdicts.push_back(r.verdict);
        }
    const std::size_t running = rep && !rep->done ? rep->steps.size() - rep->count(hmi::Verdict::Pending) : static_cast<std::size_t>(-1);
    stepsModel_ = std::make_shared<Rows>(std::vector<std::string>{"N\xC2\xB0", "Pas", "Verdict", "D\xC3\xA9tail"}, std::move(rows),
                                         [verdicts, running](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle s;
                                             if (r >= verdicts.size()) return s;
                                             const ui::Tone tone = verdictTone(verdicts[r]);
                                             if (c == 1 && r == running) { s.bold = true; s.fgTone = ui::Tone::Accent; }
                                             if (c == 2) {
                                                 s.fgTone = tone;
                                                 s.bold = verdicts[r] == hmi::Verdict::Failed || verdicts[r] == hmi::Verdict::Error;
                                                 if (verdicts[r] == hmi::Verdict::Passed) { s.icon = ui::Icon::Ok; s.iconTone = tone; }
                                                 else if (verdicts[r] == hmi::Verdict::Failed || verdicts[r] == hmi::Verdict::Error) {
                                                     s.icon = ui::Icon::Error;
                                                     s.iconTone = tone;
                                                 }
                                             }
                                             if (c == 3 && (verdicts[r] == hmi::Verdict::Failed || verdicts[r] == hmi::Verdict::Error)) s.fgTone = tone;
                                             if (verdicts[r] == hmi::Verdict::Failed || verdicts[r] == hmi::Verdict::Error) s.bg = gfx::Color{229, 83, 75, 34};
                                             return s;
                                         });
    steps_->setModel(stepsModel_);
    refreshing_ = was;
}

void HmiScenariosPane::rebuildProperties() {
    const auto& p = doc_->project;
    const auto* sc = p.scenario(selectedScenario());
    if (!sc) {
        PG::Category c;
        c.name = "Essais de r\xC3\xA9" "ception";
        c.properties.push_back(prop("Essais", std::to_string(p.scenarios.size()), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Pour commencer", "Nouvel essai (barre d'outils)", PG::ValueType::ReadOnly, {},
                                    "Un essai rejoue sur l'IHM en marche ce que ferait l'op\xC3\xA9rateur (ouvrir une vue, cliquer, "
                                    "saisir, se connecter, signer, acquitter) et v\xC3\xA9rifie ce qui doit en r\xC3\xA9sulter. Chaque pas "
                                    "re\xC3\xA7oit son verdict ; le rapport s'exporte."));
        grid_->setCategories({std::move(c)});
        return;
    }
    const Id scId = sc->id;
    std::vector<PG::Category> cats;
    const int st = selectedStep();
    if (st >= 0 && st < static_cast<int>(sc->steps.size())) {
        const auto& step = sc->steps[static_cast<std::size_t>(st)];
        const auto kind = hmi::stepKind(step.action);
        const auto fields = hmi::stepFields(kind);
        auto commit = [this, scId, st](const char* key) {
            return [this, scId, st, key](std::string_view v) {
                message_.clear();
                return setStepField(scId, st, key, std::string(v));
            };
        };
        PG::Category c;
        c.name = "Pas " + std::to_string(st + 1);
        c.properties.push_back(prop("Action", step.action, PG::ValueType::Enum, commit("action"),
                                    "Ce que fait le pas. Ouvrir la vue, Cliquer, Saisir, \xC3\x89" "crire (le proc\xC3\xA9" "d\xC3\xA9), Attendre, "
                                    "Attendre que, V\xC3\xA9rifier, Se connecter, Se d\xC3\xA9" "connecter, Signer, Acquitter, Commentaire.",
                                    hmi::stepActions()));
        c.properties.push_back(prop("Cible", step.target, fields.target.empty() ? PG::ValueType::ReadOnly : PG::ValueType::Text,
                                    fields.target.empty() ? std::function<bool(std::string_view)>{} : commit("cible"),
                                    fields.target.empty() ? std::string("Ne sert pas pour cette action.") : "La cible : " + fields.target + "."));
        // Un mot de passe (Se connecter, Signer) ne s'affiche pas : des points ; en taper un autre le remplace.
        const bool secret = (kind == hmi::StepKind::Login || kind == hmi::StepKind::Sign) && !step.value.empty();
        static const std::string kMask = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";
        std::function<bool(std::string_view)> valueCommit;
        if (!fields.value.empty()) {
            valueCommit = commit("valeur");
            if (secret) valueCommit = [inner = valueCommit](std::string_view v) { return v == kMask || inner(v); };
        }
        c.properties.push_back(prop("Valeur", secret ? kMask : step.value, fields.value.empty() ? PG::ValueType::ReadOnly : PG::ValueType::Text,
                                    valueCommit,
                                    fields.value.empty() ? std::string("Ne sert pas pour cette action.")
                                                         : "La valeur : " + fields.value + (secret ? " (masqu\xC3\xA9 : en taper un autre le remplace)" : std::string{}) + "."));
        c.properties.push_back(prop("Attendu", step.expected, fields.expected.empty() ? PG::ValueType::ReadOnly : PG::ValueType::Text,
                                    fields.expected.empty() ? std::function<bool(std::string_view)>{} : commit("attendu"),
                                    fields.expected.empty() ? std::string("Ne sert pas pour cette action.") : "L'attendu : " + fields.expected + "."));
        c.properties.push_back(prop("Note", step.note, PG::ValueType::Text, commit("note"),
                                    "Ce que le pas v\xC3\xA9rifie, pour qui lira le rapport (le texte d'un Commentaire)."));
        if (const auto it = doc_->reports.find(scId); it != doc_->reports.end() && static_cast<std::size_t>(st) < it->second.steps.size()) {
            const auto& r = it->second.steps[static_cast<std::size_t>(st)];
            if (r.verdict != hmi::Verdict::Pending && r.verdict != hmi::Verdict::Info)
                c.properties.push_back(prop("Dernier verdict", std::string(hmi::verdictLabel(r.verdict)) + (r.detail.empty() ? std::string{} : " \xE2\x80\x94 " + r.detail),
                                            PG::ValueType::ReadOnly));
        }
        cats.push_back(std::move(c));
    }
    auto commitSc = [this, scId](const char* key) {
        return [this, scId, key](std::string_view v) {
            message_.clear();
            return setScenarioField(scId, key, std::string(v));
        };
    };
    PG::Category e;
    e.name = "Essai";
    e.properties.push_back(prop("Nom", sc->name, PG::ValueType::Text, commitSc("nom"), "Lettres, chiffres, _ ; unique."));
    e.properties.push_back(prop("Description", sc->description, PG::ValueType::Text, commitSc("description"),
                                "Ce que l'essai d\xC3\xA9montre (le titre du rapport)."));
    e.properties.push_back(prop("Pas", std::to_string(sc->steps.size()), PG::ValueType::ReadOnly));
    if (const auto it = doc_->reports.find(scId); it != doc_->reports.end())
        e.properties.push_back(prop("Dernier passage", it->second.started + " \xE2\x80\x94 " + it->second.summary(), PG::ValueType::ReadOnly));
    cats.push_back(std::move(e));
    grid_->setCategories(std::move(cats));
}

// ================================================================ les gestes ===
Id HmiScenariosPane::addScenario(const std::string& name) {
    Id made = kNoId;
    const std::string n = freeName(doc_->project, name);
    auto cmd = hmi::changeProject(doc_, "Nouvel essai " + n, [&](hmi::Project& p) {
        hmi::TestScenario sc;
        sc.id = p.allocate();
        sc.name = n;
        hmi::TestStep first;
        first.action = "Ouvrir la vue";
        if (const auto* v = p.view(p.config.startView)) first.target = v->name;
        sc.steps.push_back(first);
        made = sc.id;
        p.scenarios.push_back(std::move(sc));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Nouvel essai \xC2\xAB " + n + " \xC2\xBB : un premier pas ouvre la vue de d\xC3\xA9marrage ; ajoute les suivants.");
    return made;
}

Id HmiScenariosPane::duplicateScenario(Id id) {
    const auto* src = doc_->project.scenario(id);
    if (!src) return kNoId;
    Id made = kNoId;
    const std::string n = freeName(doc_->project, src->name + "_copie");
    const hmi::TestScenario copy = *src;
    auto cmd = hmi::changeProject(doc_, "Dupliquer l'essai " + src->name, [&](hmi::Project& p) {
        hmi::TestScenario sc = copy;
        sc.id = p.allocate();
        sc.name = n;
        made = sc.id;
        p.scenarios.push_back(std::move(sc));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return made;
}

bool HmiScenariosPane::removeScenario(Id id) {
    const auto* sc = doc_->project.scenario(id);
    if (!sc) return false;
    const std::string name = sc->name;
    auto cmd = hmi::changeProject(doc_, "Supprimer l'essai " + name, [&](hmi::Project& p) {
        std::erase_if(p.scenarios, [&](const hmi::TestScenario& s) { return s.id == id; });
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    doc_->reports.erase(id);
    refresh();
    say("Essai " + name + " supprim\xC3\xA9 (Ctrl+Z le rend).");
    return true;
}

bool HmiScenariosPane::setScenarioField(Id id, const std::string& key, const std::string& raw, std::string* why) {
    const auto* sc = doc_->project.scenario(id);
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (!sc) return fail("essai introuvable");
    const std::string value = trimmed(raw);
    if (key == "nom") {
        if (value.empty()) return fail("un essai a un nom");
        for (const char c : value)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return fail("nom : lettres, chiffres et _ seulement");
        if (const auto* other = doc_->project.scenarioByName(value); other && other->id != id) return fail("nom d\xC3\xA9j\xC3\xA0 pris : " + value);
    } else if (key != "description") {
        return fail("champ inconnu : " + key);
    }
    auto cmd = hmi::changeProject(doc_, "Essai " + sc->name + " : " + key, [&](hmi::Project& p) {
        if (auto* s = p.scenario(id)) (key == "nom" ? s->name : s->description) = key == "nom" ? value : raw;
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return true;
}

int HmiScenariosPane::addStep(Id id, hmi::TestStep step, int after) {
    const auto* sc = doc_->project.scenario(id);
    if (!sc) return -1;
    const int n = static_cast<int>(sc->steps.size());
    const int at = after < 0 || after >= n ? n : after + 1;
    if (step.action.empty()) step.action = "Cliquer";
    // L'action ecrite comme la liste l'ecrit ("verifier" -> "Verifier" accentue) ; inconnue : refusee.
    const auto kind = hmi::stepKind(step.action);
    if (kind == hmi::StepKind::Unknown) {
        say("action inconnue : " + step.action, true);
        return -1;
    }
    step.action = hmi::stepLabel(kind);
    auto cmd = hmi::changeProject(doc_, "Essai " + sc->name + " : ajouter un pas", [&](hmi::Project& p) {
        if (auto* s = p.scenario(id)) s->steps.insert(s->steps.begin() + at, step);
    });
    if (cmd) apply_(std::move(cmd));
    doc_->reports.erase(id);               // les verdicts ne correspondent plus aux pas
    refresh();
    selectScenario(id);
    return at;
}

bool HmiScenariosPane::removeStep(Id id, int index) {
    const auto* sc = doc_->project.scenario(id);
    if (!sc || index < 0 || index >= static_cast<int>(sc->steps.size())) return false;
    auto cmd = hmi::changeProject(doc_, "Essai " + sc->name + " : supprimer le pas " + std::to_string(index + 1), [&](hmi::Project& p) {
        if (auto* s = p.scenario(id)) s->steps.erase(s->steps.begin() + index);
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    doc_->reports.erase(id);
    refresh();
    return true;
}

bool HmiScenariosPane::moveStep(Id id, int index, int delta) {
    const auto* sc = doc_->project.scenario(id);
    const int n = sc ? static_cast<int>(sc->steps.size()) : 0;
    const int to = index + delta;
    if (!sc || index < 0 || index >= n || to < 0 || to >= n) return false;
    auto cmd = hmi::changeProject(doc_, "Essai " + sc->name + " : d\xC3\xA9placer le pas " + std::to_string(index + 1), [&](hmi::Project& p) {
        if (auto* s = p.scenario(id)) std::swap(s->steps[static_cast<std::size_t>(index)], s->steps[static_cast<std::size_t>(to)]);
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    doc_->reports.erase(id);
    refresh();
    return true;
}

bool HmiScenariosPane::setStepField(Id id, int index, const std::string& key, const std::string& raw, std::string* why) {
    const auto* sc = doc_->project.scenario(id);
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (!sc || index < 0 || index >= static_cast<int>(sc->steps.size())) return fail("pas introuvable");
    std::string value = raw;
    if (key == "action") {
        const auto k = hmi::stepKind(raw);
        if (k == hmi::StepKind::Unknown) return fail("action inconnue : " + raw);
        value = hmi::stepLabel(k);
    } else if (key != "cible" && key != "valeur" && key != "attendu" && key != "note") {
        return fail("champ inconnu : " + key);
    }
    auto cmd = hmi::changeProject(doc_, "Essai " + sc->name + " : pas " + std::to_string(index + 1) + ", " + key, [&](hmi::Project& p) {
        auto* s = p.scenario(id);
        if (!s) return;
        auto& st = s->steps[static_cast<std::size_t>(index)];
        (key == "action" ? st.action : key == "cible" ? st.target : key == "valeur" ? st.value : key == "attendu" ? st.expected : st.note) = value;
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectStep(index);
    return true;
}

// ============================================================ lancer, rapport ===
std::size_t HmiScenariosPane::runAll() {
    ::sim::Environment* plc = hosts_.plc ? hosts_.plc() : nullptr;
    std::size_t ok = 0;
    for (const auto& sc : doc_->project.scenarios) {
        auto rep = hmi::runScenario(doc_->project, sc, plc);
        ok += rep.ok();
        doc_->reports[sc.id] = std::move(rep);
    }
    doc_->reported->emit(kNoId);
    const std::size_t n = doc_->project.scenarios.size();
    say("Tout lancer (sans \xC3\xA9" "cran" + std::string(plc ? ", reli\xC3\xA9 \xC3\xA0 l'automate simul\xC3\xA9" : ", l'IHM seule") + ") : " + std::to_string(ok)
            + " / " + std::to_string(n) + " essai(s) r\xC3\xA9ussi(s)",
        ok != n);
    return ok;
}

bool HmiScenariosPane::exportReport(hmi::ExportFormat format, std::string* where) {
    const auto& p = doc_->project;
    hmi::ExportTable table;
    std::string name;
    if (const auto* sc = p.scenario(selectedScenario()); sc && doc_->reports.count(sc->id)) {
        table = hmi::reportTable(*sc, doc_->reports.at(sc->id));
        name = "essai_" + fileSafe(sc->name);
    } else {
        std::vector<std::pair<const hmi::TestScenario*, const hmi::ScenarioReport*>> runs;
        for (const auto& each : p.scenarios) {
            const auto it = doc_->reports.find(each.id);
            runs.emplace_back(&each, it == doc_->reports.end() ? nullptr : &it->second);
        }
        table = hmi::campaignTable(runs);
        name = "essais_" + fileSafe(p.config.name);
    }
    if (table.subtitle.empty()) table.subtitle = hmi::wallStamp().substr(0, 19);
    table.subtitle = p.config.name + " - " + table.subtitle;
    hmi::ExportRequest rq;
    rq.fileName = hmi::exportFileName(name + "_" + hmi::wallStamp().substr(0, 10), format);
    rq.format = std::string(hmi::exportFormatLabel(format));
    rq.source = "essais";
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(hmi::exportBytes(table, format));
    rq.origin = "IHM > Essais";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Rapport \xC3\xA9" "crit : " + path : "Rapport impossible \xC3\xA0 \xC3\xA9" "crire" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

void HmiScenariosPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float listW = std::min(470.f, b.w * 0.28f);
    const float gridW = std::min(420.f, b.w * 0.28f);
    list_->setBounds({b.x, b.y + 38, listW, h});
    steps_->setBounds({b.x + listW + 4, b.y + 38, std::max(0.f, b.w - listW - gridW - 8), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiScenariosPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
