#include "HmiActionsPanel.hpp"
#include "HmiActionDialogs.hpp"   // 1.11.9 : l'operation en arbre, le script, la formule de Maths
#include "HmiParamPanes.hpp"   // 1.9 : les arguments types d'Ouvrir une popup
#include "../../hmi/HmiActionKinds.hpp"
#include "../../hmi/HmiKeys.hpp"          // 1.11.23 : les raccourcis
#include "../../hmi/HmiExport.hpp"
#include "HmiAssist.hpp"
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : les champs a expression, partout pareils

#include "HmiAssetPanes.hpp"
#include "HmiIcons.hpp"
#include "../../hmi/HmiLoginMenu.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiSystemMenu.hpp"          // 1.9 : les onglets de Parametres systeme
#include "../../hmi/HmiMedia.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>

namespace app {

using hmi::Action;
using hmi::Id;
using hmi::kNoId;
using hmi::Operation;
using hmi::Trigger;
using hmi::TransitionKind;

namespace {

enum Act : int { AAdd = 1, ADuplicate, ARemove, AUp, ADown };

// Les lignes du volet : "#", puis les colonnes (l'action ; 1.11.23, un raccourci : la touche,
// le declencheur, l'action).
class ActionRows final : public ui::ITableModel {
public:
    ActionRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows)
        : headers_(std::move(headers)), rows_(std::move(rows)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size() + 1; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? std::string("#") : c - 1 < headers_.size() ? headers_[c - 1] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_.size()) return {};
        if (c == 0) return std::to_string(r + 1);
        return c - 1 < rows_[r].size() ? rows_[r][c - 1] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t c) const override {
        ui::CellStyle s;
        if (c == 1) s.icon = headers_.size() > 1 ? ui::Icon::Keyboard : ui::Icon::Play;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
};

// 1.11.23 : les declencheurs d'un raccourci, tels que l'inspecteur les propose.
struct KeyTriggerName { Trigger t; const char* label; };
constexpr KeyTriggerName kKeyTriggerNames[] = {
    {Trigger::KeyPress, "Front montant (touche enfonc\xC3\xA9" "e)"},
    {Trigger::KeyRelease, "Front descendant (touche rel\xC3\xA2" "ch\xC3\xA9" "e)"},
    {Trigger::KeyHold, "Dur\xC3\xA9" "e (touche maintenue)"},
    {Trigger::KeyRepeat, "R\xC3\xA9p\xC3\xA9tition (tant qu'elle est tenue)"},
};
std::string keyTriggerLabel(Trigger t) {
    for (const auto& n : kKeyTriggerNames) if (n.t == t) return n.label;
    return kKeyTriggerNames[0].label;
}
std::string keyTriggerShort(Trigger t) {
    switch (t) {
        case Trigger::KeyRelease: return "Front descendant";
        case Trigger::KeyHold:    return "Dur\xC3\xA9" "e";
        case Trigger::KeyRepeat:  return "R\xC3\xA9p\xC3\xA9tition";
        default:                  return "Front montant";
    }
}

// "\n" dans une case d'une ligne : le code d'un script, un message multiligne.
std::string escapeLines(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}
std::string unescapeLines(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
        else out += s[i];
    }
    return out;
}

std::vector<std::string> directionsFor(TransitionKind k) {
    switch (k) {
        case TransitionKind::Slide:  return {"gauche", "droite", "haut", "bas"};
        case TransitionKind::Zoom:   return {"avant", "arri\xC3\xA8re"};
        case TransitionKind::Rotate: return {"horaire", "antihoraire"};
        default:                     return {};
    }
}

// Les accents ne comptent pas pour le moteur ("arriere" = "arri\xC3\xA8re").
std::string plainDirection(std::string_view v) {
    return v == "arri\xC3\xA8re" ? std::string("arriere") : std::string(v);
}
std::string shownDirection(const std::string& v) { return v == "arriere" ? std::string("arri\xC3\xA8re") : v; }

const std::vector<std::string>& curves() {
    static const std::vector<std::string> c = {"lin\xC3\xA9" "aire", "acc\xC3\xA9l\xC3\xA9r\xC3\xA9" "e", "d\xC3\xA9" "c\xC3\xA9l\xC3\xA9r\xC3\xA9" "e",
                                               "douce", "rebond", "bezier(0.25, 0.1, 0.25, 1)"};
    return c;
}
std::string plainCurve(std::string_view v) {
    if (v == "lin\xC3\xA9" "aire") return "lineaire";
    if (v == "acc\xC3\xA9l\xC3\xA9r\xC3\xA9" "e") return "acceleree";
    if (v == "d\xC3\xA9" "c\xC3\xA9l\xC3\xA9r\xC3\xA9" "e") return "deceleree";
    return std::string(v);
}
std::string shownCurve(const std::string& v) {
    if (v == "lineaire") return "lin\xC3\xA9" "aire";
    if (v == "acceleree") return "acc\xC3\xA9l\xC3\xA9r\xC3\xA9" "e";
    if (v == "deceleree") return "d\xC3\xA9" "c\xC3\xA9l\xC3\xA9r\xC3\xA9" "e";
    return v;
}

} // namespace

HmiActionsPanel::HmiActionsPanel(std::string id, hmi::DocumentPtr doc, Id view, Apply apply, Scope scope)
    : ui::Widget(std::move(id)), scope_(scope), doc_(std::move(doc)), view_(view), apply_(std::move(apply)) {
    const std::string base = this->id();
    const bool keys = scope_ == Scope::Shortcuts;
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(AAdd, HmiGlyph::Plus, keys ? "Ajouter un raccourci : une touche et son action (Ctrl+Z le retire)"
                                          : "Ajouter une action (Ctrl+Z la retire)", "Ajouter");
    tools->add(ADuplicate, HmiGlyph::Duplicate, keys ? "Dupliquer le raccourci choisi" : "Dupliquer l'action choisie");
    tools->add(ARemove, HmiGlyph::Delete, keys ? "Retirer le raccourci choisi" : "Retirer l'action choisie");
    tools->add(AUp, HmiGlyph::Up, "Monter l'action (elles s'ex\xC3\xA9" "cutent dans l'ordre)");
    tools->add(ADown, HmiGlyph::Down, "Descendre l'action");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(ADuplicate, [this] { return selectedIndex() >= 0; });
    tools_->setEnabledWhen(ARemove, [this] { return selectedIndex() >= 0; });
    tools_->setEnabledWhen(AUp, [this] { return rowOf(selectedIndex()) > 0; });
    tools_->setEnabledWhen(ADown, [this] {
        const int row = rowOf(selectedIndex());
        return row >= 0 && row + 1 < static_cast<int>(rows_.size());
    });
    auto table = std::make_unique<ui::TableView>(base + ".list");
    if (keys)
        table->setColumns({{"#", 40.f, 32.f, false, false, true, ui::Align::End}, {"Touche", 120.f}, {"D\xC3\xA9" "clencheur", 140.f},
                           {"Action", 360.f}});
    else
        table->setColumns({{"#", 40.f, 32.f, false, false, true, ui::Align::End}, {"Action", 520.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(true);
    grid->setNameColumnRatio(0.46f);
    // 1.9 : dans l'argument d'un parametre d'Ouvrir une popup, l'aide des expressions
    // avec les noms du bon type d'abord (HmiParamPanes : argumentAssist).
    grid->setFieldAssist([this, others = assist::gridAssist(assist::sourcesFor(doc_)),
                          expression = assist::fieldAssist(assist::sourcesFor(doc_), false)](
                             std::string_view category, const ui::PropertyGrid::Property& p) -> ui::InputText::Assist {
        const auto arg = hmiparams::argumentField(category, p.name);
        if (arg.param.empty() || !p.commit) return others(category, p);
        return hmiparams::argumentAssist(
            expression, [doc = doc_]() -> const hmi::Project* { return doc ? &doc->project : nullptr; }, view_, arg.popup, arg.param);
    });
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));

    links_ += tools_->triggered->connect([this](int a) {
        const int sel = selectedIndex();
        switch (a) {
            case AAdd: {
                Action fresh;
                if (scope_ == Scope::Shortcuts) {
                    // 1.11.23 : la premiere touche libre de F2 a F12 (F1 : l'aide), Journaliser.
                    fresh.trigger = Trigger::KeyPress;
                    fresh.operation = Operation::Log;
                    const auto* list = actions();
                    for (int f = 2; f <= 12 && fresh.key.empty(); ++f) {
                        if (f == 11) continue;                       // F11 : le plein ecran du poste
                        const std::string k = "F" + std::to_string(f);
                        const bool used = list && std::any_of(list->begin(), list->end(),
                                                              [&k](const Action& a) { return hmi::triggerIsKey(a.trigger) && a.key == k; });
                        if (!used) fresh.key = k;
                    }
                    if (fresh.key.empty()) fresh.key = "F2";
                    fresh.value = "Raccourci " + fresh.key;
                } else if (owner_ == kNoId) {
                    fresh.trigger = Trigger::ViewOpen;
                    fresh.operation = Operation::Log;
                    fresh.value = "Vue ouverte";
                } else {
                    fresh.trigger = Trigger::Click;
                    fresh.operation = Operation::Set;
                }
                selectIndex(add(fresh));
                break;
            }
            case ADuplicate:
                if (const auto* list = actions(); list && sel >= 0) selectIndex(add((*list)[static_cast<std::size_t>(sel)]));
                break;
            case ARemove: (void)remove(sel); break;
            case AUp: if (move(sel, -1)) selectIndex(selected_); break;
            case ADown: if (move(sel, +1)) selectIndex(selected_); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selected_ = rows.empty() || rows.front() >= rows_.size() ? -1 : rows_[rows.front()];
        rebuildGrid();
    });
    refresh();
}

bool HmiActionsPanel::shows(const Action& a) const {
    // 1.11.23 : les raccourcis (les actions de la vue a declencheur Touche) dans leur volet ;
    // les autres actions dans celui des actions.
    return (scope_ == Scope::Shortcuts) == hmi::triggerIsKey(a.trigger);
}

int HmiActionsPanel::rowOf(int index) const {
    for (std::size_t r = 0; r < rows_.size(); ++r)
        if (rows_[r] == index) return static_cast<int>(r);
    return -1;
}

const std::vector<Action>* HmiActionsPanel::actions() const {
    const auto* v = doc_->project.view(view_);
    if (!v) return nullptr;
    if (owner_ == kNoId) return &v->actions;
    const auto* o = v->object(owner_);
    return o ? &o->actions : nullptr;
}

namespace {
// L'operation d'une action, et ce qu'elle entraine (une transition, un delai par defaut).
bool applyOperation(Action& n, Operation o) {
    // Les parametres sont ceux d'une operation (les references de Maths, les reglages du clavier) :
    // une autre operation repart sans eux.
    if (n.operation != o) n.params.clear();
    n.operation = o;
    if (hmi::operationOpensView(n.operation) && n.transition.kind == TransitionKind::Instant) {
        const bool popup = n.operation == Operation::Popup || n.operation == Operation::ChangePopup;
        n.transition.kind = popup ? TransitionKind::Fade : TransitionKind::Slide;
        n.transition.durationMs = popup ? 300 : 400;
    }
    return true;
}
} // namespace

void HmiActionsPanel::setDialogHost(DialogHost host, std::function<std::shared_ptr<const domain::Project>()> plc) {
    host_ = std::move(host);
    plc_ = std::move(plc);
    rebuildGrid();
}

bool HmiActionsPanel::openEditor(const std::string& name) {
    const auto* list = actions();
    const int index = selectedIndex();
    if (!host_ || !list || index < 0 || index >= static_cast<int>(list->size())) return false;
    const Action a = (*list)[static_cast<std::size_t>(index)];
    const std::string where = ownerName() + " \xC2\xB7 n\xC2\xB0 " + std::to_string(index + 1);
    // La reponse arrive plus tard : le volet (et la meme action) doit etre encore la.
    const std::weak_ptr<bool> alive = alive_;
    const Id view = view_, owner = owner_;
    const auto current = [this, alive, view, owner, index]() -> const Action* {
        if (alive.expired() || view_ != view || owner_ != owner) return nullptr;
        const auto* l = actions();
        return l && index < static_cast<int>(l->size()) ? &(*l)[static_cast<std::size_t>(index)] : nullptr;
    };
    if (name == "Op\xC3\xA9ration") {
        HmiOperationDialog::Spec spec;
        spec.current = a.operation;
        spec.where = where;
        host_(std::make_unique<HmiOperationDialog>(std::move(spec)), [this, current, index](const menu::DialogResult& r) {
            const auto* cur = current();
            const auto o = r.accepted() ? HmiOperationDialog::parse(r.payload) : std::nullopt;
            if (!cur || !o) return;
            Action next = *cur;
            if (applyOperation(next, *o)) (void)set(index, next);
        });
        return true;
    }
    if (name == "Code ST" && a.operation == Operation::RunScript) {
        HmiActionScriptDialog::Spec spec;
        spec.where = where;
        spec.code = a.value;
        spec.doc = doc_;
        spec.view = view_;
        if (plc_) spec.plc = plc_();
        host_(std::make_unique<HmiActionScriptDialog>(std::move(spec)), [this, current, index](const menu::DialogResult& r) {
            const auto* cur = current();
            if (!cur || !r.accepted()) return;
            Action next = *cur;
            next.value = r.payload;
            (void)set(index, next);
        });
        return true;
    }
    if ((name == "Formule" || name == "R\xC3\xA9" "f\xC3\xA9rences") && a.operation == Operation::Maths) {
        HmiMathsDialog::Spec spec;
        spec.where = where;
        spec.target = a.target;
        spec.formula = a.value;
        spec.refs = hmi::actionkinds::params(a);
        spec.doc = doc_;
        spec.view = view_;
        host_(std::make_unique<HmiMathsDialog>(std::move(spec)), [this, current, index](const menu::DialogResult& r) {
            const auto* cur = current();
            if (!cur || !r.accepted()) return;
            const auto answer = HmiMathsDialog::parse(r.payload);
            Action next = *cur;
            next.target = hmi::targetVariable(answer.target);
            next.value = answer.formula;
            next.params = hmi::actionkinds::formatParams(answer.refs);
            (void)set(index, next);
        });
        return true;
    }
    return false;
}

std::string HmiActionsPanel::ownerName() const {
    const auto* v = doc_->project.view(view_);
    if (!v) return {};
    if (owner_ == kNoId) return v->name + " (la vue)";
    const auto* o = v->object(owner_);
    return o ? o->name : std::string{};
}

void HmiActionsPanel::setOwner(Id object) {
    if (scope_ == Scope::Shortcuts) object = kNoId;          // 1.11.23 : les raccourcis sont a la vue
    if (object == owner_) { refresh(); return; }
    owner_ = object;
    selected_ = -1;
    refresh();
    if (!rows_.empty()) selectIndex(rows_.front());
}

int HmiActionsPanel::selectedIndex() const {
    const auto* list = actions();
    if (!list || selected_ < 0 || selected_ >= static_cast<int>(list->size())) return -1;
    if (!shows((*list)[static_cast<std::size_t>(selected_)])) return -1;
    return selected_;
}

void HmiActionsPanel::selectIndex(int index) {
    const auto* list = actions();
    if (!list || index < 0 || index >= static_cast<int>(list->size())) return;
    const int row = rowOf(index);
    if (row < 0) return;
    hmiSelectModelRow(*table_, static_cast<ui::RowIndex>(row));
    selected_ = index;
    rebuildGrid();
}

void HmiActionsPanel::refresh() {
    std::vector<std::vector<std::string>> rows;
    rows_.clear();
    const bool keys = scope_ == Scope::Shortcuts;
    if (const auto* list = actions())
        for (std::size_t i = 0; i < list->size(); ++i) {
            const auto& a = (*list)[i];
            if (!shows(a)) continue;
            rows_.push_back(static_cast<int>(i));
            if (keys) {
                // "Ctrl+F5", "Front montant", puis l'operation (ce que dit la liste des actions,
                // sans son declencheur).
                std::string what = hmi::describeAction(a);
                if (const auto arrow = what.find("\xE2\x86\x92 "); arrow != std::string::npos) what = what.substr(arrow + 4);
                rows.push_back({a.key.empty() ? std::string("(aucune)") : hmi::keys::label(a.key), keyTriggerShort(a.trigger), what});
            } else {
                rows.push_back({hmi::describeAction(a)});
            }
        }
    const std::size_t count = rows.size();
    model_ = std::make_shared<ActionRows>(keys ? std::vector<std::string>{"Touche", "D\xC3\xA9" "clencheur", "Action"}
                                               : std::vector<std::string>{"Action"},
                                          std::move(rows));
    const int keep = selected_;
    table_->setModel(model_);
    if (const int row = rowOf(keep); row >= 0) {
        hmiSelectModelRow(*table_, static_cast<ui::RowIndex>(row));
        selected_ = keep;
    } else {
        selected_ = -1;
    }
    rebuildGrid();
    if (count != lastCount_) {
        lastCount_ = count;
        countChanged->emit(count);
    }
    invalidate();
}

int HmiActionsPanel::add(Action a) {
    int made = -1;
    const Id owner = owner_;
    auto cmd = hmi::changeView(doc_, view_, "Ajouter une action", [&](hmi::Project&, hmi::View& v) {
        auto* list = owner == kNoId ? &v.actions : (v.object(owner) ? &v.object(owner)->actions : nullptr);
        if (!list) return;
        list->push_back(std::move(a));
        made = static_cast<int>(list->size()) - 1;
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return made;
}

bool HmiActionsPanel::set(int index, const Action& a) {
    const Id owner = owner_;
    bool done = false;
    auto cmd = hmi::changeView(doc_, view_, "R\xC3\xA9gler l'action", [&](hmi::Project&, hmi::View& v) {
        auto* list = owner == kNoId ? &v.actions : (v.object(owner) ? &v.object(owner)->actions : nullptr);
        if (!list || index < 0 || index >= static_cast<int>(list->size())) return;
        (*list)[static_cast<std::size_t>(index)] = a;
        done = true;
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    selected_ = index;
    refresh();
    return done;
}

bool HmiActionsPanel::remove(int index) {
    const Id owner = owner_;
    // La ligne d'avant (dans ce volet) sera choisie : son index, avant le retrait.
    const int row = rowOf(index);
    const int before = row > 0 ? rows_[static_cast<std::size_t>(row - 1)] : -1;
    auto cmd = hmi::changeView(doc_, view_, scope_ == Scope::Shortcuts ? "Retirer un raccourci" : "Retirer une action",
                               [&](hmi::Project&, hmi::View& v) {
        auto* list = owner == kNoId ? &v.actions : (v.object(owner) ? &v.object(owner)->actions : nullptr);
        if (!list || index < 0 || index >= static_cast<int>(list->size())) return;
        list->erase(list->begin() + index);
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    selected_ = before;
    refresh();
    return true;
}

bool HmiActionsPanel::move(int index, int delta) {
    const Id owner = owner_;
    // 1.11.23 : la voisine dans ce volet (les raccourcis et les actions se melent dans la liste).
    const int row = rowOf(index);
    const int toRow = row + delta;
    if (row < 0 || toRow < 0 || toRow >= static_cast<int>(rows_.size())) return false;
    const int to = rows_[static_cast<std::size_t>(toRow)];
    auto cmd = hmi::changeView(doc_, view_, "Ordonner les actions", [&](hmi::Project&, hmi::View& v) {
        auto* list = owner == kNoId ? &v.actions : (v.object(owner) ? &v.object(owner)->actions : nullptr);
        if (!list || index < 0 || to < 0 || index >= static_cast<int>(list->size()) || to >= static_cast<int>(list->size())) return;
        std::swap((*list)[static_cast<std::size_t>(index)], (*list)[static_cast<std::size_t>(to)]);
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    selected_ = to;
    refresh();
    return true;
}

void HmiActionsPanel::rebuildGrid() {
    using PG = ui::PropertyGrid;
    const auto* list = actions();
    const int index = selectedIndex();
    if (!list || index < 0) {
        PG::Category info;
        if (scope_ == Scope::Shortcuts) {
            // 1.11.23 : la section Raccourcis, vide ou rien de choisi.
            info.name = rows_.empty() ? "Aucun raccourci" : "Choisis un raccourci";
            PG::Property p;
            p.name = "Raccourcis de";
            p.value = ownerName();
            p.type = PG::ValueType::ReadOnly;
            p.description = "Un raccourci : une touche (F5, Ctrl+S, Maj+Entr\xC3\xA9" "e\xE2\x80\xA6) li\xC3\xA9" "e \xC3\xA0 une action, avec son "
                            "d\xC3\xA9" "clencheur : le front montant (la touche enfonc\xC3\xA9" "e), le front descendant (rel\xC3\xA2" "ch\xC3\xA9" "e), "
                            "une dur\xC3\xA9" "e (maintenue), une r\xC3\xA9p\xC3\xA9tition (tant qu'elle est tenue). Il part quand la vue "
                            "(ou la popup) est montr\xC3\xA9" "e en marche. \xC2\xAB Ajouter \xC2\xBB en cr\xC3\xA9" "e un \xC3\xA0 r\xC3\xA9gler ci-dessous.";
            info.properties.push_back(std::move(p));
            grid_->setCategories({std::move(info)});
            return;
        }
        info.name = list ? (list->empty() ? "Aucune action" : "Choisis une action") : "Rien \xC3\xA0 montrer";
        PG::Property p;
        p.name = "Actions de";
        p.value = ownerName();
        p.type = PG::ValueType::ReadOnly;
        p.description = owner_ == kNoId ? "Rien n'est choisi dans la vue : ce sont les actions de la vue (ouverture, fermeture, "
                                          "timer, fronts). Choisis un objet pour les siennes (clic, appui long...)."
                                        : "\xC2\xAB Ajouter \xC2\xBB cr\xC3\xA9" "e une action \xC2\xAB Clic \xE2\x86\x92 Mettre \xC3\xA0 1 \xC2\xBB \xC3\xA0 r\xC3\xA9gler ci-dessous.";
        info.properties.push_back(std::move(p));
        grid_->setCategories({std::move(info)});
        return;
    }
    const Action a = (*list)[static_cast<std::size_t>(index)];
    const auto commitWith = [this, index, a](auto&& change) {
        return [this, index, a, change](std::string_view v) {
            Action next = a;
            if (!change(next, v)) return false;
            return set(index, next) || next == a;
        };
    };
    const auto prop = [](std::string name, std::string value, PG::ValueType t, std::vector<std::string> choices,
                         std::function<bool(std::string_view)> commit, std::string help = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = t;
        p.enumValues = std::move(choices);
        p.commit = std::move(commit);
        p.description = std::move(help);
        return p;
    };
    std::vector<PG::Category> cats;
    if (scope_ == Scope::Shortcuts) {
        // ---- 1.11.23 : le raccourci - sa touche, son declencheur, sa duree ou sa periode
        PG::Category key;
        key.name = "Raccourci";
        const int row = rowOf(index);
        key.properties.push_back(prop("Raccourci de", ownerName() + "  \xC2\xB7  n\xC2\xB0 " + std::to_string(row + 1),
                                      PG::ValueType::ReadOnly, {}, nullptr,
                                      "Un raccourci de la vue : il part quand elle est montr\xC3\xA9" "e en marche (une popup du dessus "
                                      "passe avant la vue)."));
        std::string why;
        const auto chord = hmi::keys::parseChord(a.key, &why);
        std::string help = "La touche, avec ou sans Ctrl, Maj, Alt : F5, Ctrl+S, Maj+Entr\xC3\xA9" "e, Alt+Haut, A, 1. "
                           "Les lettres, les chiffres, F1 \xC3\xA0 F12, Entr\xC3\xA9" "e, \xC3\x89" "chap, Espace, Tab, Suppr, Inser, D\xC3\xA9" "but, "
                           "Fin, Page pr\xC3\xA9" "c, Page suiv, les fl\xC3\xA8" "ches.";
        if (!chord) help = "Touche illisible : " + why + ". " + help;
        else if (const auto r = hmi::keys::reserved(*chord); !r.empty()) help = "Attention : " + r + " - le raccourci ne partira pas partout. " + help;
        key.properties.push_back(prop("Touche", chord ? hmi::keys::label(*chord) : a.key, PG::ValueType::Text, {},
                                      commitWith([](Action& n, std::string_view v) {
                                          const auto c = hmi::keys::parseChord(v);
                                          if (!c) return false;
                                          n.key = hmi::keys::canonical(*c);
                                          if (n.operation == Operation::Log && n.value.rfind("Raccourci ", 0) == 0)
                                              n.value = "Raccourci " + hmi::keys::label(*c);   // le message d'exemple suit
                                          return true;
                                      }),
                                      help));
        std::vector<std::string> kinds;
        for (const auto& n : kKeyTriggerNames) kinds.emplace_back(n.label);
        key.properties.push_back(prop("D\xC3\xA9" "clencheur", keyTriggerLabel(a.trigger), PG::ValueType::Enum, kinds,
                                      commitWith([](Action& n, std::string_view v) {
                                          for (const auto& k : kKeyTriggerNames)
                                              if (v == k.label) {
                                                  n.trigger = k.t;
                                                  if (n.trigger == Trigger::KeyHold && n.delayMs <= 0) n.delayMs = hmi::keys::kHoldDefaultMs;
                                                  if (n.trigger == Trigger::KeyRepeat && n.delayMs <= 0) n.delayMs = hmi::keys::kRepeatDefaultMs;
                                                  return true;
                                              }
                                          return false;
                                      }),
                                      "Front montant : \xC3\xA0 l'appui (la r\xC3\xA9p\xC3\xA9tition du clavier ne compte pas). Front descendant : "
                                      "au rel\xC3\xA2" "chement, m\xC3\xAA" "me si la vue s'est ferm\xC3\xA9" "e entre-temps. Dur\xC3\xA9" "e : une fois, "
                                      "la touche tenue assez longtemps. R\xC3\xA9p\xC3\xA9tition : \xC3\xA0 chaque p\xC3\xA9riode, tant qu'elle est tenue."));
        if (a.trigger == Trigger::KeyHold || a.trigger == Trigger::KeyRepeat) {
            const bool hold = a.trigger == Trigger::KeyHold;
            key.properties.push_back(prop(hold ? "Dur\xC3\xA9" "e (ms)" : "P\xC3\xA9riode (ms)",
                                          std::to_string(a.delayMs > 0 ? a.delayMs : (hold ? hmi::keys::kHoldDefaultMs : hmi::keys::kRepeatDefaultMs)),
                                          PG::ValueType::Integer, {},
                                          commitWith([hold](Action& n, std::string_view v) {
                                              double ms = 0;
                                              if (!hmi::parseNumber(v, ms) || ms < (hold ? 10 : 50)) return false;
                                              n.delayMs = static_cast<int>(ms);
                                              return true;
                                          }),
                                          hold ? "Combien de temps la touche doit \xC3\xAAtre tenue (10 ms au moins)."
                                               : "Toutes les combien l'action repart tant que la touche est tenue (50 ms au moins)."));
        }
        key.properties.push_back(prop("Condition (facultative)", a.guard, PG::ValueType::Text, {},
                                      commitWith([](Action& n, std::string_view v) { n.guard = std::string(v); return true; }),
                                      "Le raccourci ne part que si cette expression est vraie. Ex. : SYS.UserLevel >= 2"));
        ui::exprfield::markWhole(key.properties.back(), ui::exprfield::Expect::Bool);
        cats.push_back(std::move(key));
    }
    // ---- le declencheur (et a qui est l'action : un objet, ou la vue)
    PG::Category trig;
    trig.name = "D\xC3\xA9" "clencheur";
    trig.properties.push_back(prop("Action de", ownerName() + "  \xC2\xB7  n\xC2\xB0 " + std::to_string(index + 1),
                                   PG::ValueType::ReadOnly, {}, nullptr,
                                   owner_ == kNoId ? "Une action de la vue : ouverture, fermeture, timer, fronts, changement."
                                                   : "Une action de l'objet choisi. Rien de choisi : celles de la vue."));
    std::vector<std::string> triggerNames;
    for (auto t : hmi::kTriggers) triggerNames.emplace_back(hmi::triggerLabel(t));
    trig.properties.push_back(prop("D\xC3\xA9" "clencheur", std::string(hmi::triggerLabel(a.trigger)), PG::ValueType::Enum, triggerNames,
                                   commitWith([](Action& n, std::string_view v) {
                                       const auto t = hmi::triggerFromLabel(v);
                                       if (!t) return false;
                                       n.trigger = *t;
                                       if (n.trigger == Trigger::LongPress && n.delayMs <= 0) n.delayMs = 800;
                                       if (n.trigger == Trigger::Timer && n.delayMs <= 0) n.delayMs = 1000;
                                       return true;
                                   }),
                                   "Ce qui lance l'action. Clic, double clic, appui long : sur l'objet. Ouverture, fermeture, "
                                   "timer : avec la vue. Fronts et changement : une expression surveill\xC3\xA9" "e \xC3\xA0 chaque cycle IHM."));
    if (hmi::triggerWatches(a.trigger))
        trig.properties.push_back(prop("Expression surveill\xC3\xA9" "e", a.watch, PG::ValueType::Text, {},
                                       commitWith([](Action& n, std::string_view v) { n.watch = std::string(v); return true; }),
                                       "Ex. : Armoires[0].bouteille_vide ; Niveau > 90.0 ; Etat = 5"));
    if (hmi::triggerWatches(a.trigger)) ui::exprfield::markWhole(trig.properties.back(), ui::exprfield::Expect::Bool);   // 1.10 (chantier K)
    if (a.trigger == Trigger::LongPress || a.trigger == Trigger::Timer)
        trig.properties.push_back(prop(a.trigger == Trigger::Timer ? "P\xC3\xA9riode (ms)" : "Dur\xC3\xA9" "e de l'appui (ms)",
                                       std::to_string(a.delayMs > 0 ? a.delayMs : (a.trigger == Trigger::Timer ? 1000 : 800)),
                                       PG::ValueType::Integer, {},
                                       commitWith([](Action& n, std::string_view v) {
                                           double ms = 0;
                                           if (!hmi::parseNumber(v, ms) || ms < 10) return false;
                                           n.delayMs = static_cast<int>(ms);
                                           return true;
                                       })));
    trig.properties.push_back(prop("Condition (facultative)", a.guard, PG::ValueType::Text, {},
                                   commitWith([](Action& n, std::string_view v) { n.guard = std::string(v); return true; }),
                                   "L'action ne part que si cette expression est vraie. Ex. : Mode_Manuel AND NOT Defaut"));
    ui::exprfield::markWhole(trig.properties.back(), ui::exprfield::Expect::Bool);   // 1.10 (chantier K)
    if (scope_ != Scope::Shortcuts) cats.push_back(std::move(trig));   // 1.11.23 : un raccourci a la sienne (plus haut)
    // ---- l'operation
    PG::Category op;
    op.name = "Op\xC3\xA9ration";
    std::vector<std::string> opNames;
    for (auto o : hmi::kOperations) opNames.emplace_back(hmi::operationLabel(o));
    op.properties.push_back(prop("Op\xC3\xA9ration", std::string(hmi::operationLabel(a.operation)), PG::ValueType::Enum, opNames,
                                 commitWith([](Action& n, std::string_view v) {
                                     const auto o = hmi::operationFromLabel(v);
                                     return o && applyOperation(n, *o);
                                 }),
                                 std::string(hmi::actionkinds::groupOf(a.operation)) + " \xE2\x80\x94 " + std::string(hmi::actionkinds::help(a.operation))));
    if (host_) {
        // 1.11.9 : un clic ouvre l'arbre des operations (rangees par familles, avec une recherche).
        op.properties.back().open = [this] { (void)openEditor("Op\xC3\xA9ration"); };
        op.properties.back().openTip = "Choisir dans l'arbre des op\xC3\xA9rations (familles, recherche)";
        op.properties.back().openOnClick = true;
    }
    if (hmi::operationWritesVariable(a.operation)) {
        op.properties.push_back(prop("Variable", a.target, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.target = hmi::targetVariable(v); return true; }),
                                     "Une variable IHM ou de l'automate : Compteur_Clics, Armoires[0].active. Dans un symbole ou une "
                                     "popup, une r\xC3\xA9" "f\xC3\xA9rence et ses membres : Vanne.CMD_OUV."));
        // 1.11.7 : la pastille fx et l'aide (les variables, les references du symbole), comme la Condition ;
        // le type attendu au bout du nom (BOOL pour Mettre a 1, Mettre a 0, Basculer).
        const auto expect = a.operation == Operation::Increment || a.operation == Operation::Decrement || a.operation == Operation::Maths
                                ? ui::exprfield::Expect::Number
                          : a.operation == Operation::Assign || a.operation == Operation::Keyboard ? ui::exprfield::Expect::Value
                                                                                                    : ui::exprfield::Expect::Bool;
        ui::exprfield::markWhole(op.properties.back(), expect);
    } else if (hmi::operationOpensView(a.operation)) {
        // Lot 8 : une popup s'ouvre parmi les popups (et les vues : toute vue peut
        // s'ouvrir par-dessus) ; une navigation vise une vue ordinaire.
        const bool popup = a.operation == Operation::Popup || a.operation == Operation::ChangePopup;
        std::vector<std::string> views;
        for (const auto& v : doc_->project.views)
            if (popup ? v.role == "popup" : v.role == "vue" || v.role.empty()) views.push_back(v.name);
        for (const auto& v : doc_->project.views)
            if (popup && v.role == "vue") views.push_back(v.name);
        if (std::find(views.begin(), views.end(), a.target) == views.end()) views.insert(views.begin(), a.target);
        op.properties.push_back(prop("Vue", a.target, PG::ValueType::Enum, views,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     popup ? "Les popups du projet d'abord, puis les vues : toute vue peut s'ouvrir par-dessus."
                                           : std::string{}));
        // Les parametres passes a la vue ouverte (ses noms declares).
        std::string declared;
        if (const auto* tv = doc_->project.viewByName(a.target))
            for (const auto& prm : tv->params) declared += (declared.empty() ? "" : ", ") + prm.name;
        op.properties.push_back(prop("Param\xC3\xA8tres", a.value, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Ce que re\xC3\xA7oit la vue ouverte : Moteur := Pompes[3]; Titre := 'Pompe 3'. Un chemin de "
                                     "variable la relie (on lit et on \xC3\xA9" "crit \xC3\xA0 travers), un calcul donne une valeur. "
                                     + (declared.empty() ? std::string("Cette vue ne d\xC3\xA9" "clare aucun param\xC3\xA8tre.")
                                                         : "Elle d\xC3\xA9" "clare : " + declared + ".")));
        if (a.operation == Operation::Popup) {
            std::vector<std::string> places{"(son r\xC3\xA9glage)"};
            for (const auto pl : hmi::kPopupPlacements) places.emplace_back(hmi::popupPlacementLabel(pl));
            const std::string shown = a.placement.empty() ? std::string("(son r\xC3\xA9glage)") : std::string(hmi::popupPlacementLabel(a.placement));
            if (std::find(places.begin(), places.end(), shown) == places.end()) places.push_back(shown);
            op.properties.push_back(prop("Position", shown, PG::ValueType::Enum, places,
                                         commitWith([](Action& n, std::string_view v) {
                                             n.placement = v.rfind("(son", 0) == 0 ? std::string{} : hmi::popupPlacementFromLabel(v);
                                             return true;
                                         }),
                                         "Centr\xC3\xA9" "e, sous l'objet cliqu\xC3\xA9, dans un coin, \xC3\xA0 sa derni\xC3\xA8re place, ou x,y "
                                         "(pixels de la vue du dessous). D\xC3\xA9j\xC3\xA0 ouverte : elle revient devant."));
        }
    } else if (a.operation == Operation::CenterPopup) {
        std::vector<std::string> views{""};
        for (const auto& v : doc_->project.views) if (v.role == "popup") views.push_back(v.name);
        if (std::find(views.begin(), views.end(), a.target) == views.end()) views.push_back(a.target);
        op.properties.push_back(prop("Popup", a.target, PG::ValueType::Enum, views,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Vide : celle du dessus. Elle revient au milieu de la vue."));
    } else if (a.operation == Operation::PreviousPopup) {
        op.properties.push_back(prop("Revient \xC3\xA0", "la popup d'avant (Changer de popup)", PG::ValueType::ReadOnly, {}, nullptr,
                                     "Chaque popup garde les popups qu'elle a remplac\xC3\xA9" "es (Changer de popup) : Popup pr\xC3\xA9" "c\xC3\xA9" "dente "
                                     "y revient, \xC3\xA0 sa place, avec ses param\xC3\xA8tres."));
    } else if (a.operation == Operation::CloseAllPopups) {
        op.properties.push_back(prop("Ferme", "toutes les popups ouvertes", PG::ValueType::ReadOnly, {}, nullptr, {}));
    } else if (a.operation == Operation::NavigateBack || a.operation == Operation::NavigateForward) {
        // Lot 12 : l'historique de navigation.
        op.properties.push_back(prop("Revient \xC3\xA0", a.operation == Operation::NavigateBack
                                         ? std::string("la vue d'avant (historique)") : std::string("la vue quitt\xC3\xA9" "e par Pr\xC3\xA9" "c\xC3\xA9" "dent"),
                                     PG::ValueType::ReadOnly, {}, nullptr,
                                     "Chaque navigation empile la vue quitt\xC3\xA9" "e (au plus 50) : Vue pr\xC3\xA9" "c\xC3\xA9" "dente y revient, avec "
                                     "ses param\xC3\xA8tres ; Vue suivante repart. SYS.CanGoBack, SYS.CanGoForward disent s'il y a de quoi."));
    } else if (a.operation == Operation::NavigateHome) {
        op.properties.push_back(prop("Ouvre", "la vue d'accueil", PG::ValueType::ReadOnly, {}, nullptr,
                                     "Celle du groupe de l'utilisateur connect\xC3\xA9 (Configuration \xE2\x80\xBA Utilisateurs : Vue de "
                                     "d\xC3\xA9marrage), sinon la vue de d\xC3\xA9marrage du projet. SYS.HomeView la donne."));
    } else if (a.operation == Operation::Logout) {
        op.properties.push_back(prop("Effet", "plus personne n'est connect\xC3\xA9 (niveau 0)", PG::ValueType::ReadOnly, {}, nullptr,
                                     "Comme le bouton D\xC3\xA9" "connexion de la biblioth\xC3\xA8que."));
    } else if (a.operation == Operation::CallScript) {
        std::vector<std::string> scripts;
        for (const auto& sc : doc_->project.programs.scripts) scripts.push_back(sc.name);
        if (std::find(scripts.begin(), scripts.end(), a.target) == scripts.end()) scripts.insert(scripts.begin(), a.target);
        op.properties.push_back(prop("Script g\xC3\xA9n\xC3\xA9ral", a.target, PG::ValueType::Enum, scripts,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; })));
    } else if (a.operation == Operation::AckAlarm) {
        // Toutes, un groupe, ou une alarme : les choix du projet.
        std::vector<std::string> targets{"*"};
        for (const auto& al : doc_->project.alarms)
            if (!al.group.empty() && std::find(targets.begin(), targets.end(), "groupe:" + al.group) == targets.end())
                targets.push_back("groupe:" + al.group);
        for (const auto& al : doc_->project.alarms) targets.push_back(al.name);
        if (std::find(targets.begin(), targets.end(), a.target) == targets.end()) targets.insert(targets.begin(), a.target);
        op.properties.push_back(prop("Alarmes \xC3\xA0 acquitter", a.target, PG::ValueType::Enum, targets,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "* : toutes ; groupe:Armoire A : un groupe ; sinon une alarme. Il faut la permission Acquitter."));
    } else if (a.operation == Operation::LoadRecipe) {
        std::vector<std::string> recipes;
        for (const auto& r : doc_->project.recipes) recipes.push_back(r.name);
        if (std::find(recipes.begin(), recipes.end(), a.target) == recipes.end()) recipes.insert(recipes.begin(), a.target);
        op.properties.push_back(prop("Recette", a.target, PG::ValueType::Enum, recipes,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; })));
        std::vector<std::string> sets;
        if (const auto* r = doc_->project.recipeByName(a.target))
            for (const auto& rec : r->records) sets.push_back(rec.name);
        if (std::find(sets.begin(), sets.end(), a.value) == sets.end()) sets.insert(sets.begin(), a.value);
        op.properties.push_back(prop("Jeu de valeurs", a.value, PG::ValueType::Enum, sets,
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Le jeu \xC3\xA9" "crit (tout ou rien) ; une expression peut le choisir en marche : Jeu_Choisi"));
    } else if (a.operation == Operation::ChangeUser) {
        std::vector<std::string> logins{"D\xC3\xA9" "connexion"};
        for (const auto& u : doc_->project.security.users) logins.push_back(u.login);
        if (std::find(logins.begin(), logins.end(), a.target) == logins.end()) logins.insert(logins.begin(), a.target);
        op.properties.push_back(prop("Utilisateur", a.target, PG::ValueType::Enum, logins,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Vide : le dialogue propose tous les utilisateurs. Un login : son mot de passe (ou son code) "
                                     "est demand\xC3\xA9 ; par expression : connect\xC3\xA9 si elle est vraie. D\xC3\xA9" "connexion : "
                                     "plus personne (niveau 0)."));
    }
    // ---- lot 6 : les ressources
    if (a.operation == Operation::RequestResource) {
        op.properties.push_back(prop("Extensions accept\xC3\xA9" "es", a.value, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Le filtre du s\xC3\xA9lecteur de fichiers : png;jpg;svg (vide : tout). Le fichier choisi "
                                     "devient une ressource du projet. Il faut la permission Administrer."));
        op.properties.push_back(prop("Variable (nom de la ressource)", a.target, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Facultatif : une variable STRING qui re\xC3\xA7oit le nom de la ressource ajout\xC3\xA9" "e "
                                     "(une image anim\xC3\xA9" "e ou un objet Image peuvent la montrer : =Image_Choisie)."));
    } else if (a.operation == Operation::BindTable) {
        std::vector<std::string> tables;
        if (const auto* v = doc_->project.view(view_))
            for (const auto& o : v->objects)
                if (o.kind == hmi::Kind::Table) tables.push_back(o.name);
        if (std::find(tables.begin(), tables.end(), a.target) == tables.end()) tables.insert(tables.begin(), a.target);
        op.properties.push_back(prop("Tableau", a.target, PG::ValueType::Enum, tables,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Un objet Tableau de cette vue."));
        std::vector<std::string> files{""};
        for (const auto& f : doc_->project.assets.files)
            files.push_back(f.name);
        if (std::find(files.begin(), files.end(), a.value) == files.end()) files.push_back(a.value);
        op.properties.push_back(prop("Base / fichier externe", a.value, PG::ValueType::Enum, files,
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Un fichier externe du projet (SQLite, Excel, CSV, base externe...) : le tableau montre ses lignes. "
                                     "Vide : il revient \xC3\xA0 sa source de l'\xC3\xA9" "diteur. Une expression peut le choisir en marche."));
    } else if (a.operation == Operation::PlaySound) {
        std::vector<std::string> sounds;
        for (const auto& r : doc_->project.assets.resources)
            if (r.kind() == hmi::MediaKind::Sound) sounds.push_back(r.name);
        if (std::find(sounds.begin(), sounds.end(), a.target) == sounds.end()) sounds.insert(sounds.begin(), a.target);
        op.properties.push_back(prop("Son (ressource)", a.target, PG::ValueType::Enum, sounds,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Un son du gestionnaire de ressources (WAV). Une expression peut le choisir en marche."));
    } else if (a.operation == Operation::ShowSystem) {
        // Lot 10 : le menu natif, sur l'onglet choisi ("" : Reglages) ; 1.9 : Simulation.
        const std::string tab(hmi::systemTabLabel(hmi::systemTabFrom(a.value)));
        op.properties.push_back(prop("Onglet", tab, PG::ValueType::Enum, {"R\xC3\xA9glages", "Diagnostic", "Simulation"},
                                     commitWith([](Action& n, std::string_view v) {
                                         n.value = v == "Diagnostic" || v == "Simulation" ? std::string(v) : std::string();
                                         return true;
                                     }),
                                     "Le menu natif Param\xC3\xA8tres syst\xC3\xA8me, par-dessus la vue : les r\xC3\xA9glages du poste "
                                     "(luminosit\xC3\xA9, veille, son, volume, d\xC3\xA9" "connexion, clavier, heure), le diagnostic "
                                     "(IHM, automate, utilisateur, alarmes, poste) ou la page Simulation (les esclaves simul\xC3\xA9s, "
                                     "permission Administrer). La croix, ou un clic dehors, le ferme."));
    } else if (a.operation == Operation::ShowLogin) {
        // Lot 12 : le menu natif de connexion, sur l'onglet choisi ("" : Connexion).
        std::vector<std::string> tabs;
        for (const auto& t : hmi::kLoginTabs) tabs.emplace_back(t.label);
        const std::string tab(hmi::loginTabLabel(hmi::loginTabFrom(a.value)));
        op.properties.push_back(prop("Onglet", tab, PG::ValueType::Enum, tabs,
                                     commitWith([](Action& n, std::string_view v) {
                                         n.value = v == "Connexion" ? std::string() : std::string(v);
                                         return true;
                                     }),
                                     "Le menu natif de connexion, par-dessus la vue : se connecter, son compte, et selon le niveau "
                                     "les comptes, les acc\xC3\xA8s (r\xC3\xB4les des groupes, d\xC3\xA9" "connexion automatique) et le journal "
                                     "des connexions. Un onglet que l'utilisateur ne voit pas : le menu s'ouvre sur Connexion et le dit."));
    } else if (a.operation == Operation::SetLanguage) {
        // Lot 13 : une langue du projet, la suivante, ou une expression qui donne le code.
        std::vector<std::string> codes{"suivante"};
        for (const auto& l : doc_->project.languages.list) codes.push_back(l.code);
        const std::string shown = a.target.empty() ? std::string("suivante") : a.target;
        if (std::find(codes.begin(), codes.end(), shown) == codes.end()) codes.push_back(shown);
        op.properties.push_back(prop("Langue", shown, PG::ValueType::Enum, codes,
                                     commitWith([](Action& n, std::string_view v) {
                                         n.target = v == "suivante" ? std::string() : std::string(v);
                                         return true;
                                     }),
                                     "Une langue du projet (Configuration > Langues) : ses textes s'affichent. \xC2\xAB suivante \xC2\xBB "
                                     "passe \xC3\xA0 la langue d'apr\xC3\xA8s (la premi\xC3\xA8re apr\xC3\xA8s la derni\xC3\xA8re). "
                                     "Une expression qui donne le code (Langue_Choisie) marche aussi. En marche : SYS.Language."));
    } else if (a.operation == Operation::SetTheme) {
        // Lot 13 : jour, nuit, ou l'autre.
        const std::string shown = a.target.empty() ? std::string("bascule") : a.target;
        op.properties.push_back(prop("Th\xC3\xA8me", shown, PG::ValueType::Enum, {"bascule", "jour", "nuit"},
                                     commitWith([](Action& n, std::string_view v) {
                                         n.target = v == "bascule" ? std::string() : std::string(v);
                                         return true;
                                     }),
                                     "Jour : des couleurs claires, pour un \xC3\xA9" "cran en plein jour ; nuit : les couleurs de la "
                                     "conception ; bascule : l'autre. En marche : SYS.Theme."));
    } else if (a.operation == Operation::ShelveAlarm || a.operation == Operation::UnshelveAlarm) {
        // Lot 11 : l'alarme choisie d'un clic (vide), une zone, une alarme ; remettre : aussi toutes.
        std::vector<std::string> targets{""};
        if (a.operation == Operation::UnshelveAlarm) targets.push_back("*");
        for (const auto& al : doc_->project.alarms)
            if (!al.group.empty() && std::find(targets.begin(), targets.end(), "groupe:" + al.group) == targets.end())
                targets.push_back("groupe:" + al.group);
        for (const auto& al : doc_->project.alarms) targets.push_back(al.name);
        if (std::find(targets.begin(), targets.end(), a.target) == targets.end()) targets.push_back(a.target);
        op.properties.push_back(prop("Alarme", a.target, PG::ValueType::Enum, targets,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Vide : l'alarme choisie d'un clic (bandeau, liste, r\xC3\xA9sum\xC3\xA9) ; groupe:Zone : toutes celles "
                                     "d'une zone ; sinon une alarme. Il faut la permission Acquitter."));
        if (a.operation == Operation::ShelveAlarm) {
            const auto semi = a.value.find(';');
            const std::string minutes = a.value.substr(0, semi);
            const std::string reason = semi == std::string::npos ? std::string{} : a.value.substr(semi + 1);
            op.properties.push_back(prop("Dur\xC3\xA9" "e (min)", minutes.empty() ? std::string("30") : minutes, PG::ValueType::Text, {},
                                         commitWith([](Action& n, std::string_view v) {
                                             const auto at = n.value.find(';');
                                             n.value = std::string(v) + (at == std::string::npos ? std::string(";") : n.value.substr(at));
                                             return true;
                                         }),
                                         "Le temps o\xC3\xB9 elle n'appara\xC3\xAEt plus (une expression : Duree_Choisie). 0 : sans limite. "
                                         "Au plus la limite de Configuration > Alarmes."));
            op.properties.push_back(prop("Raison", reason, PG::ValueType::Text, {},
                                         commitWith([](Action& n, std::string_view v) {
                                             const auto at = n.value.find(';');
                                             n.value = (at == std::string::npos ? n.value : n.value.substr(0, at)) + ";" + std::string(v);
                                             return true;
                                         }),
                                         "Pourquoi (texte \xC3\xA0 trous) : Capteur en essai ; {Raison_Saisie}. Le journal garde qui, quand, pourquoi."));
        }
    } else if (hmi::operationTargetsGif(a.operation)) {
        // Lot 16 : un GIF anime de la vue (ou d'une popup ouverte en marche).
        std::vector<std::string> gifs;
        if (const auto* v = doc_->project.view(view_))
            for (const auto& o : v->objects)
                if (o.kind == hmi::Kind::AnimatedGif) gifs.push_back(o.name);
        if (std::find(gifs.begin(), gifs.end(), a.target) == gifs.end()) gifs.insert(gifs.begin(), a.target);
        op.properties.push_back(prop("GIF anim\xC3\xA9", a.target, PG::ValueType::Enum, gifs,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Un objet GIF anim\xC3\xA9 de cette vue (son nom). En marche, il est cherch\xC3\xA9 dans les popups "
                                     "ouvertes puis dans la vue courante. IHM_GIF_JOUER('Nom') fait la m\xC3\xAAme chose dans un script."));
        const char* effect = a.operation == Operation::GifPlay    ? "le GIF joue (repart de la pause, sinon du d\xC3\xA9" "but)"
                           : a.operation == Operation::GifPause   ? "le GIF s'arr\xC3\xAAte sur l'image montr\xC3\xA9" "e ; Jouer le reprend l\xC3\xA0"
                           : a.operation == Operation::GifStop    ? "le GIF revient \xC3\xA0 sa premi\xC3\xA8re image et attend"
                                                                   : "le GIF repart du d\xC3\xA9" "but pour N tours";
        op.properties.push_back(prop("Effet", effect, PG::ValueType::ReadOnly, {}, nullptr, {}));
        if (a.operation == Operation::GifReplay)
            op.properties.push_back(prop("Nombre de tours (N)", a.value, PG::ValueType::Text, {},
                                         commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                         "Un nombre ou une expression : 3, Tours_Choisis. 0 : sans fin (en boucle) ; vide : une fois."));
    } else if (a.operation == Operation::SilenceAlarms) {
        op.properties.push_back(prop("Effet", "le son des alarmes se tait jusqu'\xC3\xA0 la prochaine apparition", PG::ValueType::ReadOnly, {},
                                     nullptr, "Les alarmes restent \xC3\xA0 acquitter : seul le son (et sa r\xC3\xA9p\xC3\xA9tition) s'arr\xC3\xAAte."));
    } else if (a.operation == Operation::Export) {
        std::vector<std::string> sources;
        for (const auto src : hmi::kExportSources) sources.emplace_back(src);
        for (const auto& r : doc_->project.recipes) sources.push_back("recette:" + r.name);
        if (const auto* v = doc_->project.view(view_))
            for (const auto& o : v->objects)
                if (o.kind == hmi::Kind::Trend || o.kind == hmi::Kind::Table || hmi::kindIsChart(o.kind) || o.kind == hmi::Kind::VariableTable
                    || o.kind == hmi::Kind::ProductionCounter || o.kind == hmi::Kind::AlarmStats || o.kind == hmi::Kind::History)
                    sources.push_back("objet:" + o.name);
        if (std::find(sources.begin(), sources.end(), a.target) == sources.end()) sources.insert(sources.begin(), a.target);
        op.properties.push_back(prop("Donn\xC3\xA9" "es", a.target.empty() ? std::string("alarmes") : a.target, PG::ValueType::Enum, sources,
                                     commitWith([](Action& n, std::string_view v) { n.target = std::string(v); return true; }),
                                     "Les alarmes en cours, l'historique, les \xC3\xA9v\xC3\xA9nements, le journal, les mesures, les jeux d'une "
                                     "recette, ou le contenu d'un objet de la vue."));
        op.properties.push_back(prop("Fichier", a.value, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Texte \xC3\xA0 trous ; son extension choisit le format : alarmes_{SYS.Date}.csv, .xlsx (Excel) ou .pdf. "
                                     "Le nom propos\xC3\xA9 dans le dossier exports du projet (voir Demander o\xC3\xB9 enregistrer)."));
        // ---- Lot API 8 : les exports qui demandent ou ----
        op.properties.push_back(prop("Demander o\xC3\xB9 enregistrer", a.askWhere ? "TRUE" : "FALSE", PG::ValueType::Boolean, {},
                                     commitWith([](Action& n, std::string_view v) { n.askWhere = hmi::parseBool(v, n.askWhere); return true; }),
                                     "Coch\xC3\xA9 (par d\xC3\xA9" "faut) : au clic, au double clic, \xC3\xA0 l'appui long, un dialogue demande o\xC3\xB9 "
                                     "l'enregistrer - exports/ du projet propos\xC3\xA9 (Exporter : comme avant), le bouton \xE2\x80\xA6 pour un "
                                     "autre dossier ou un autre nom ; au poste d'exploitation, un dialogue \xC3\xA0 grands boutons. "
                                     "D\xC3\xA9" "coch\xC3\xA9 : toujours dans exports/, sans question. Les autres d\xC3\xA9" "clencheurs "
                                     "(timer, fronts, changement, ouverture ou fermeture de vue) partent seuls : jamais de question, "
                                     "personne n'est l\xC3\xA0 pour r\xC3\xA9pondre."));
        if (a.trigger != Trigger::Click && a.trigger != Trigger::DoubleClick && a.trigger != Trigger::LongPress)
            op.properties.push_back(prop("En marche", "sans question (" + std::string(hmi::triggerLabel(a.trigger)) + " : part seul)",
                                         PG::ValueType::ReadOnly, {}, nullptr,
                                         "Seul un geste de l'op\xC3\xA9rateur (clic, double clic, appui long) fait demander o\xC3\xB9 "
                                         "enregistrer ; ce d\xC3\xA9" "clencheur-ci \xC3\xA9" "crit dans exports/ du projet."));
        // ---- fin Lot API 8 ----
    }
    if (a.operation == Operation::Increment || a.operation == Operation::Decrement)
        op.properties.push_back(prop("Pas", a.value.empty() ? std::string("1") : a.value, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Un nombre ou une expression : 1, 0.5, Pas_Reglage"));
    if (a.operation == Operation::Increment || a.operation == Operation::Decrement) {
        // 1.10 (chantier K) : un nombre n'est pas une expression (pas de pastille pleine).
        double literal = 0;
        ui::exprfield::markWhole(op.properties.back(), ui::exprfield::Expect::Number, !hmi::parseNumber(a.value, literal) && !a.value.empty());
    }
    if (a.operation == Operation::Assign)
        op.properties.push_back(prop("Valeur (expression)", a.value, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Ex. : Consigne + 1.5 ; 'Azote' ; T#5s ; Armoires[i].seuil_poids_saisi"));
    if (a.operation == Operation::Assign) ui::exprfield::markWhole(op.properties.back(), ui::exprfield::Expect::Value);   // 1.10 (chantier K)
    if (a.operation == Operation::RunScript) {
        op.properties.push_back(prop("Code ST", escapeLines(a.value), PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = unescapeLines(v); return true; }),
                                     "Du ST ex\xC3\xA9" "cut\xC3\xA9 tel quel ; \\n s\xC3\xA9pare les lignes. Ex. : Compteur := 0;\\nIHM_JOURNAL('remis \xC3\xA0 z\xC3\xA9ro');"));
        if (host_) {
            // 1.11.9 : le script dans sa petite fenetre (l'editeur des scripts, l'aide, les references).
            op.properties.back().open = [this] { (void)openEditor("Code ST"); };
            op.properties.back().openTip = "Modifier le script dans sa fen\xC3\xAAtre (aide \xC3\xA0 la saisie, variables et r\xC3\xA9" "f\xC3\xA9rences)";
        }
    }
    // 1.11.9 : Maths - la formule et ses references (la cible est la case Variable ci-dessus).
    if (a.operation == Operation::Maths) {
        namespace ak = hmi::actionkinds;
        const auto refs = ak::params(a);
        std::string issues;
        for (const auto& i : ak::checkMaths(refs, a.value)) issues += (issues.empty() ? "" : " ; ") + i.why;
        op.properties.push_back(prop("Formule", a.value, PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = std::string(v); return true; }),
                                     "Avec les noms des r\xC3\xA9" "f\xC3\xA9rences : (Mesure - Consigne) * 2. Les fonctions : ABS, SQRT, MIN, MAX, LIMIT..."
                                         + (issues.empty() ? std::string{} : " \xE2\x80\x94 \xC3\x80 revoir : " + issues)));
        if (host_) {
            op.properties.back().open = [this] { (void)openEditor("Formule"); };
            op.properties.back().openTip = "La formule, ses r\xC3\xA9" "f\xC3\xA9rences et le mode test, dans leur fen\xC3\xAAtre";
        }
        op.properties.push_back(prop("R\xC3\xA9" "f\xC3\xA9rences", ak::formatParams(refs), PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.params = std::string(v); return true; }),
                                     "Des variables nomm\xC3\xA9" "es : Mesure := Armoires[0].ana.PT1.mes; Consigne := Consigne_Four"));
        if (host_) {
            op.properties.back().open = [this] { (void)openEditor("R\xC3\xA9" "f\xC3\xA9rences"); };
            op.properties.back().openTip = "Ajouter, retirer, tester les r\xC3\xA9" "f\xC3\xA9rences dans la fen\xC3\xAAtre de la formule";
        }
    }
    // 1.11.9 : le clavier virtuel - ses reglages (la cible est la case Variable ci-dessus).
    if (a.operation == Operation::Keyboard) {
        namespace ak = hmi::actionkinds;
        const auto spec = ak::keyboardSpec(a);
        const auto edit = [&](auto&& change) {
            return commitWith([change](Action& n, std::string_view v) {
                auto k = ak::keyboardSpec(n);
                if (!change(k, v)) return false;
                ak::setKeyboardSpec(n, k);
                return true;
            });
        };
        op.properties.push_back(prop("Titre", spec.title, PG::ValueType::Text, {},
                                     edit([](ak::KeyboardSpec& k, std::string_view v) { k.title = std::string(v); return true; }),
                                     "Le texte du champ, \xC3\xA0 trous : Consigne de {Four1.Nom}. Vide : le nom de la variable."));
        static const char* kKeys[] = {"auto", "numerique", "complet"};
        static const char* kLabels[] = {"auto (selon le type de la variable)", "num\xC3\xA9rique", "complet (lettres)"};
        std::string shown = kLabels[0];
        for (int i = 0; i < 3; ++i)
            if (spec.keyboard == kKeys[i]) shown = kLabels[i];
        op.properties.push_back(prop("Clavier", shown, PG::ValueType::Enum, {kLabels[0], kLabels[1], kLabels[2]},
                                     edit([](ak::KeyboardSpec& k, std::string_view v) {
                                         for (int i = 0; i < 3; ++i)
                                             if (v == kLabels[i]) k.keyboard = kKeys[i];
                                         return true;
                                     })));
        op.properties.push_back(prop("Min", spec.min, PG::ValueType::Text, {},
                                     edit([](ak::KeyboardSpec& k, std::string_view v) { k.min = std::string(v); return true; }),
                                     "Vide : pas de limite. Un nombre ou une expression (Consigne_Min)."));
        ui::exprfield::markWhole(op.properties.back(), ui::exprfield::Expect::Number, false);
        op.properties.push_back(prop("Max", spec.max, PG::ValueType::Text, {},
                                     edit([](ak::KeyboardSpec& k, std::string_view v) { k.max = std::string(v); return true; }),
                                     "Vide : pas de limite. Une valeur hors des bornes est refus\xC3\xA9" "e, le champ le dit."));
        ui::exprfield::markWhole(op.properties.back(), ui::exprfield::Expect::Number, false);
        op.properties.push_back(prop("Unit\xC3\xA9", spec.unit, PG::ValueType::Text, {},
                                     edit([](ak::KeyboardSpec& k, std::string_view v) { k.unit = std::string(v); return true; }),
                                     "Montr\xC3\xA9" "e au bout du champ : degC, bar, %"));
        op.properties.push_back(prop("Caract\xC3\xA8res cach\xC3\xA9s", spec.mask ? "TRUE" : "FALSE", PG::ValueType::Boolean, {},
                                     edit([](ak::KeyboardSpec& k, std::string_view v) { k.mask = v == "TRUE"; return true; }),
                                     "Coch\xC3\xA9 : des points \xC3\xA0 la place des caract\xC3\xA8res (un code)."));
    }
    if (a.operation == Operation::Log)
        op.properties.push_back(prop("Message", escapeLines(a.value), PG::ValueType::Text, {},
                                     commitWith([](Action& n, std::string_view v) { n.value = unescapeLines(v); return true; }),
                                     "Un texte \xC3\xA0 trous : Niveau {Niveau:0.0} %, \xC3\xA9tat {Marche:marche|arr\xC3\xAAt}"));
    // 1.9 : Appliquer copie sur reference - le parametre (HmiParamPanes).
    if (a.operation == Operation::ApplyCopy)
        if (const auto* pv = doc_->project.view(view_)) {
            auto choices = hmiparams::applyCopyChoices(*pv);
            const std::string shown = hmiparams::applyCopyShown(a.target);
            if (std::find(choices.begin(), choices.end(), shown) == choices.end()) choices.push_back(shown);
            // Les autres parametres (pas en mode les deux), avec leur pastille (P4).
            std::string others;
            for (const auto& prm : pv->params)
                if (prm.mode != hmi::ParamMode::Both)
                    others += (others.empty() ? "" : ", ") + prm.name + " (" + std::string(hmi::params::paramModeBadge(prm.mode)) + ")";
            op.properties.push_back(prop("Param\xC3\xA8tre", shown, PG::ValueType::Enum, choices,
                                         commitWith([](Action& n, std::string_view v) { n.target = hmiparams::applyCopyTarget(v); return true; }),
                                         "Seuls les param\xC3\xA8tres en mode Les deux se choisissent : en R\xC3\xA9" "f\xC3\xA9rence tout est "
                                         "d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crit ; en Copie, l'original n'est jamais r\xC3\xA9\xC3\xA9" "crit. Seuls les membres "
                                         "modifi\xC3\xA9s depuis l'ouverture sont \xC3\xA9" "crits."
                                             + (others.empty() ? std::string{} : " Pas en mode les deux : " + others + ".")));
            // 1.9 (chantier U) : la pastille du mode choisi (P4) - "*" : tous ceux en mode les deux.
            {
                const auto* chosen = a.target == "*" ? nullptr : pv->param(a.target);
                if (a.target == "*" || chosen) op.properties.back().pill = hmiparams::modePill(chosen ? chosen->mode : hmi::ParamMode::Both);
            }
            op.properties.push_back(prop("\xC3\x89" "crit", hmiparams::applyCopyWrites(*pv, a.target), PG::ValueType::ReadOnly, {}, nullptr, {}));
        }
    cats.push_back(std::move(op));
    // 1.9 : les arguments d'Ouvrir une popup / Changer de popup, un par parametre.
    if (a.operation == Operation::Popup || a.operation == Operation::ChangePopup)
        if (const auto* tv = doc_->project.viewByName(a.target); tv && (tv->role == "popup" || !tv->params.empty()))
            cats.push_back(hmiparams::argumentsCategory(
                doc_->project, doc_->project.view(view_), *tv, a.value, hmiparams::program().get(),
                [this, index, a, popupName = tv->name](const std::string& param, const std::string& text) {
                    const auto* pv = doc_->project.viewByName(popupName);   // relue : les vues ont pu bouger
                    if (!pv) return false;
                    Action next = a;
                    next.value = hmiparams::withArgument(*pv, a.value, param, text);
                    return set(index, next) || next == a;
                }));
    // ---- la transition
    if (hmi::operationOpensView(a.operation) || a.operation == Operation::ClosePopup || a.operation == Operation::PreviousPopup
        || a.operation == Operation::CloseAllPopups || a.operation == Operation::NavigateBack || a.operation == Operation::NavigateForward
        || a.operation == Operation::NavigateHome) {
        PG::Category tr;
        tr.name = "Transition";
        std::vector<std::string> kinds;
        for (auto k : hmi::kTransitionKinds) kinds.emplace_back(hmi::transitionLabel(k));
        tr.properties.push_back(prop("Animation", std::string(hmi::transitionLabel(a.transition.kind)), PG::ValueType::Enum, kinds,
                                     commitWith([](Action& n, std::string_view v) {
                                         const auto k = hmi::transitionFromLabel(v);
                                         if (!k) return false;
                                         n.transition.kind = *k;
                                         const auto dirs = directionsFor(*k);
                                         if (!dirs.empty() && std::find(dirs.begin(), dirs.end(), shownDirection(n.transition.direction)) == dirs.end())
                                             n.transition.direction = plainDirection(dirs.front());
                                         return true;
                                     })));
        if (a.transition.kind != TransitionKind::Instant) {
            tr.properties.push_back(prop("Dur\xC3\xA9" "e (ms)", std::to_string(a.transition.durationMs), PG::ValueType::Integer, {},
                                         commitWith([](Action& n, std::string_view v) {
                                             double ms = 0;
                                             if (!hmi::parseNumber(v, ms) || ms < 0 || ms > 10000) return false;
                                             n.transition.durationMs = static_cast<int>(ms);
                                             return true;
                                         })));
            const auto dirs = directionsFor(a.transition.kind);
            if (!dirs.empty())
                tr.properties.push_back(prop("Direction", shownDirection(a.transition.direction), PG::ValueType::Enum, dirs,
                                             commitWith([](Action& n, std::string_view v) {
                                                 n.transition.direction = plainDirection(v);
                                                 return true;
                                             })));
            std::vector<std::string> cv = curves();
            if (std::find(cv.begin(), cv.end(), shownCurve(a.transition.easing)) == cv.end()) cv.push_back(shownCurve(a.transition.easing));
            tr.properties.push_back(prop("Courbe", shownCurve(a.transition.easing), PG::ValueType::Enum, cv,
                                         commitWith([](Action& n, std::string_view v) { n.transition.easing = plainCurve(v); return true; }),
                                         "Lin\xC3\xA9" "aire, acc\xC3\xA9l\xC3\xA9r\xC3\xA9" "e, d\xC3\xA9" "c\xC3\xA9l\xC3\xA9r\xC3\xA9" "e, douce, rebond, "
                                         "ou bezier(x1, y1, x2, y2) comme en CSS."));
            if (a.transition.kind == TransitionKind::Custom) {
                const auto num = [&](const char* name, double value, auto&& setter, const char* help) {
                    tr.properties.push_back(prop(name, hmi::formatNumber(value), PG::ValueType::Real, {},
                                                 commitWith([setter](Action& n, std::string_view v) {
                                                     double d = 0;
                                                     if (!hmi::parseNumber(v, d)) return false;
                                                     setter(n, d);
                                                     return true;
                                                 }),
                                                 help));
                };
                num("Opacit\xC3\xA9 de d\xC3\xA9part", a.transition.fromOpacity,
                    [](Action& n, double d) { n.transition.fromOpacity = std::clamp(d, 0.0, 1.0); }, "0 : invisible, 1 : opaque");
                num("D\xC3\xA9" "calage X de d\xC3\xA9part", a.transition.fromOffsetX,
                    [](Action& n, double d) { n.transition.fromOffsetX = d; }, "En largeur de vue : 0.5 = d\xC3\xA9" "cal\xC3\xA9" "e d'une demi-vue \xC3\xA0 droite");
                num("D\xC3\xA9" "calage Y de d\xC3\xA9part", a.transition.fromOffsetY,
                    [](Action& n, double d) { n.transition.fromOffsetY = d; }, "En hauteur de vue");
                num("\xC3\x89" "chelle de d\xC3\xA9part", a.transition.fromScale,
                    [](Action& n, double d) { n.transition.fromScale = std::max(0.05, d); }, "1 : taille normale");
                num("Angle de d\xC3\xA9part (\xC2\xB0)", a.transition.fromAngle,
                    [](Action& n, double d) { n.transition.fromAngle = d; }, "La vue arrive en tournant depuis cet angle");
            }
        }
        cats.push_back(std::move(tr));
    }
    grid_->setCategories(std::move(cats));
}

void HmiActionsPanel::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 34});
    const float listH = std::clamp(b.h * 0.32f, 70.f, 170.f);
    table_->setBounds({b.x, b.y + 34, b.w, listH});
    grid_->setBounds({b.x, b.y + 38 + listH, b.w, std::max(0.f, b.h - 38 - listH)});
}

void HmiActionsPanel::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
