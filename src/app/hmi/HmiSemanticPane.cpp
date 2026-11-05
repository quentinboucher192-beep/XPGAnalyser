// =============================================================================
//  app/hmi/HmiSemanticPane.cpp - 1.23.1 : IHM > Styles > Couleurs semantiques (voir HmiSemanticPane.hpp)
// =============================================================================
#include "HmiSemanticPane.hpp"

#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../hmi/HmiSemanticColors.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/ColorPalette.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;
using namespace hmikit;
namespace sem = hmi::semantic;

namespace {

// Le nom sans le @ ni les blancs du bord.
std::string bareName(std::string_view s) {
    std::string n = trimmed(std::string(s));
    if (!n.empty() && n.front() == '@') n = trimmed(n.substr(1));
    return n;
}

const hmi::SemanticColor* byId(const hmi::Project& p, Id id) {
    for (const auto& c : p.semanticColors)
        if (c.id == id) return &c;
    return nullptr;
}

// "#RRGGBB" ou "#RRGGBBAA".
bool hexColor(const std::string& v) {
    if (v.size() != 7 && v.size() != 9) return false;
    if (v.front() != '#') return false;
    return std::all_of(v.begin() + 1, v.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

} // namespace

HmiSemanticColorsPane::HmiSemanticColorsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActAdd, HmiGlyph::Plus, "Nouvelle couleur s\xC3\xA9mantique (Inser) : son nom, sa couleur dans la fiche", "Nouvelle");
    tools->add(ActDuplicate, HmiGlyph::Duplicate,
               "Dupliquer \xC3\xA0 partir de la couleur choisie (Ctrl+D) : une couleur \xC3\xA0 toi, m\xC3\xAAme une fournie", "Dupliquer \xC3\xA0 partir de");
    tools->add(ActCopy, HmiGlyph::Copy, "Copier \xC2\xAB @Nom \xC2\xBB (Ctrl+C) : \xC3\xA0 coller dans une case couleur ou une formule", "Copier @Nom");
    tools->separator();
    tools->add(ActRemove, HmiGlyph::Delete,
               "Retirer la couleur choisie (Suppr) : ce qui la cite garde sa couleur, \xC3\xA9" "crite en dur", "Retirer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // 1.24.0 : plusieurs couleurs choisies - Retirer retire les tiennes (une fournie reste, dite) ; Dupliquer : une a la fois.
    tools_->setEnabledWhen(ActDuplicate, [this] { return !selectedName().empty() && table_->selectedModelRows().size() <= 1; });
    tools_->setEnabledWhen(ActCopy, [this] { return !selectedName().empty(); });
    tools_->setEnabledWhen(ActRemove, [this] {
        for (const auto& n : several::chosen(*table_, order_))
            if (const auto* c = sem::find(doc_->project, n); c && !c->builtin) return true;
        return false;
    });
    tools_->setLabelWhen(ActRemove, [this] { return several::label("Retirer", table_ ? several::chosen(*table_, order_).size() : 0); });
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Nom (\xC3\xA0 citer)", 210.f}, {"Couleur", 150.f}, {"Description", 360.f}, {"Cit\xC3\xA9" "e", 70.f}});
    table->setSelectionMode(ui::SelectionMode::Extended);   // 1.24.0 : plusieurs couleurs a retirer d'un coup
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    tools_->bindListKeys(*table_, {ActRemove, ActDuplicate, ActAdd});
    grid_ = &static_cast<PG&>(addChild(std::make_unique<PG>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    // Le clic droit : les gestes de la barre seuls (Copier @Nom remplace le Copier des tableaux).
    table_->setExtraContextItems([this] { return contextItems(); });
    table_->setExtraContextOnly(true);
    links_ += table_->contextAction->connect([this](int a) { runAction(a); });
    const auto keys = table_->keyHandler();
    table_->setKeyHandler([this, keys](const ui::KeyDown& k) {
        if (k.key == ui::Key::C && k.mods.ctrl && !k.mods.alt && !k.mods.shift && !selectedName().empty()) {
            runAction(ActCopy);
            return true;
        }
        return keys ? keys(k) : false;
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiSemanticColorsPane::say(std::string text, bool error) {
    if (error) PG::refuse(text);
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiSemanticColorsPane::fail(std::string text, std::string* why) {
    say(text, true);
    if (why) *why = std::move(text);
    return false;
}

std::string HmiSemanticColorsPane::selectedName() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? std::string{} : order_[rows.front()];
}

Id HmiSemanticColorsPane::selectedColor() const {
    const std::string name = selectedName();
    if (name.empty()) return kNoId;
    const auto* c = sem::find(doc_->project, name);
    return c && !c->builtin ? c->id : kNoId;
}

bool HmiSemanticColorsPane::locked() const {
    const std::string name = selectedName();
    const auto* c = name.empty() ? nullptr : sem::find(doc_->project, name);
    return c && c->builtin;
}

bool HmiSemanticColorsPane::select(const std::string& raw) {
    const std::string name = bareName(raw);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], name)) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            rebuildProperties();
            return true;
        }
    return false;
}

void HmiSemanticColorsPane::refresh() {
    refreshing_ = true;
    const std::string keep = selectedName();
    const auto& p = doc_->project;
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> fixed;
    std::vector<std::optional<gfx::Color>> swatch;
    std::vector<bool> trouble;
    for (const auto* c : sem::all(p)) {
        order_.push_back(c->name);
        const std::size_t users = sem::users(p, c->name);
        rows.push_back({"@" + c->name, c->value, c->description, users ? std::to_string(users) : std::string("\xE2\x80\x94")});
        fixed.push_back(c->builtin);
        gfx::Color col;
        swatch.push_back(ui::parseHexColor(sem::resolve(p, c->value), col) ? std::optional<gfx::Color>(col) : std::nullopt);
        trouble.push_back(!sem::problems(p, *c).empty());
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Nom (\xC3\xA0 citer)", "Couleur", "Description", "Cit\xC3\xA9" "e"}, std::move(rows),
                                    [fixed, swatch, trouble](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle st;
                                        if (r >= fixed.size()) return st;
                                        if (c == 0) {
                                            // Un cadenas : fournie (verrouillee) ; une personne : a toi.
                                            st.bold = !fixed[r];
                                            st.icon = fixed[r] ? ui::Icon::Lock : ui::Icon::User;
                                            st.iconTone = fixed[r] ? ui::Tone::Muted : ui::Tone::Accent;
                                            if (trouble[r]) st.fgTone = ui::Tone::Warning;
                                        }
                                        if (c == 1) {
                                            st.monospace = true;
                                            if (swatch[r]) {
                                                ui::ColorPill pill;
                                                pill.text = "    ";
                                                pill.fill = *swatch[r];
                                                pill.border = gfx::Color{128, 128, 128, 255};
                                                pill.ink = *swatch[r];
                                                st.colorLead = pill;
                                            } else {
                                                st.fgTone = ui::Tone::Warning;
                                            }
                                        }
                                        if (c == 2 || (c == 3 && fixed[r])) st.fgTone = ui::Tone::Muted;
                                        return st;
                                    });
    table_->setModel(model_);
    bool kept = false;
    for (std::size_t i = 0; i < order_.size() && !keep.empty(); ++i)
        if (same(order_[i], keep)) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            kept = true;
        }
    if (!kept && !order_.empty()) table_->selectModelRows({0});
    refreshing_ = false;
    rebuildProperties();
    status_->setMessage(std::to_string(p.semanticColors.size()) + " couleur(s) \xC3\xA0 toi, " + std::to_string(sem::builtins().size())
                            + " fournies \xC2\xB7 une case couleur les cite par @Nom",
                        ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiSemanticColorsPane::rebuildProperties() {
    const auto& p = doc_->project;
    const std::string name = selectedName();
    const hmi::SemanticColor* c = name.empty() ? nullptr : sem::find(p, name);
    if (!c) {
        PG::Category cat;
        cat.name = "Couleurs s\xC3\xA9mantiques";
        cat.properties.push_back(prop("Pour commencer", "Nouvelle, ou Dupliquer \xC3\xA0 partir de (barre d'outils)", PG::ValueType::ReadOnly, {},
                                      "Une couleur sous un nom (Alarme, Marche, Fond), utilisable partout comme une constante : une case "
                                      "couleur \xC3\xA9" "crit @Alarme, une formule '@Alarme'. Changer la couleur change tout ce qui la cite."));
        grid_->setCategories({std::move(cat)});
        return;
    }
    const bool fixed = c->builtin;
    const Id id = c->id;
    const auto commitName = [this, id](std::string_view v) {
        message_.clear();
        return renameColor(id, std::string(v));
    };
    const auto commitValue = [this, id](std::string_view v) {
        message_.clear();
        return setValue(id, std::string(v));
    };
    const auto commitField = [this, id](const char* key) {
        return [this, id, key](std::string_view v) {
            message_.clear();
            return setField(id, key, std::string(v));
        };
    };
    PG::Category cat;
    cat.name = fixed ? "Couleur fournie (verrouill\xC3\xA9" "e)" : "Couleur s\xC3\xA9mantique";
    if (fixed)
        cat.properties.push_back(prop("Verrouill\xC3\xA9" "e", "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 en fait une \xC3\xA0 toi", PG::ValueType::ReadOnly, {},
                                      "Une couleur fournie (ISA-101) ne se modifie pas. Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 (Ctrl+D) en fait une couleur "
                                      "du projet, que tu r\xC3\xA8gles \xC3\xA0 ton go\xC3\xBBt."));
    cat.properties.push_back(prop("Nom", c->name, PG::ValueType::Text, fixed ? std::function<bool(std::string_view)>{} : commitName,
                                  "Son nom ; renommer fait suivre tout ce qui la cite (cases, formules, styles, fonds)."));
    cat.properties.push_back(prop("\xC3\x80 citer", sem::reference(c->name), PG::ValueType::ReadOnly, {},
                                  "Ce qu'une case couleur \xC3\xA9" "crit (la palette le propose en bas) ; dans une formule, entre apostrophes : '"
                                      + sem::reference(c->name) + "'."));
    cat.properties.push_back(prop("Couleur", c->value, PG::ValueType::Color, fixed ? std::function<bool(std::string_view)>{} : commitValue,
                                  "#RRGGBB, #RRGGBBAA, ou une autre couleur s\xC3\xA9mantique (@Autre)."));
    cat.properties.push_back(prop("Description", c->description, PG::ValueType::Text,
                                  fixed ? std::function<bool(std::string_view)>{} : commitField("description"), "Ce qu'elle dit, pour qui la choisit."));
    if (!fixed)
        cat.properties.push_back(prop("Dossier", c->folder, PG::ValueType::Text, commitField("dossier"), "Pour ranger la liste (facultatif)."));
    {
        const auto cites = sem::citations(p, c->name);
        std::vector<std::string> where;
        for (const auto& w : cites) where.push_back(w.where);
        cat.properties.push_back(prop("Cit\xC3\xA9" "e par", cites.empty() ? std::string("rien") : std::to_string(cites.size()) + " : " + fewOf(where, 4),
                                      PG::ValueType::ReadOnly, {}, joinList(where, "\n")));
    }
    if (const auto problems = sem::problems(p, *c); !problems.empty())
        cat.properties.push_back(prop("\xC3\x80 v\xC3\xA9rifier", joinList(problems), PG::ValueType::ReadOnly));
    grid_->setCategories({std::move(cat)});
}

std::string HmiSemanticColorsPane::checkName(const std::string& name, Id except) const {
    if (name.empty()) return "le nom est vide";
    if (name.find_first_of("@'\",;:=") != std::string::npos) return "le nom ne peut pas contenir @ ' \" , ; : =";
    if (sem::builtin(name)) return "@" + name + " est une couleur fournie : choisis un autre nom";
    if (!sem::nameFree(doc_->project, name, except)) return "@" + name + " existe d\xC3\xA9j\xC3\xA0";
    return {};
}

std::string HmiSemanticColorsPane::checkValue(const std::string& self, const std::string& value) const {
    std::string target;
    if (sem::isReference(value, &target)) {
        target = bareName(target);
        if (same(target, self)) return "@" + self + " ne peut pas se citer elle-m\xC3\xAAme";
        if (!sem::find(doc_->project, target)) return "@" + target + " : couleur s\xC3\xA9mantique inconnue";
        // Une boucle : @A vaut @B, @B vaut @A.
        std::string cur = target;
        for (int hop = 0; hop < 8; ++hop) {
            if (same(cur, self)) return "@" + self + " : les citations tourneraient en rond";
            const auto* c = sem::find(doc_->project, cur);
            std::string next;
            if (!c || !sem::isReference(c->value, &next)) break;
            cur = bareName(next);
        }
        return {};
    }
    if (!hexColor(value)) return "\xC2\xAB " + value + " \xC2\xBB : une couleur #RRGGBB, #RRGGBBAA ou @Autre";
    return {};
}

Id HmiSemanticColorsPane::addColor(const std::string& rawName, const std::string& rawValue, std::string* why) {
    std::string name = bareName(rawName);
    if (name.empty() || name == "Couleur") name = sem::uniqueName(doc_->project, name.empty() ? "Couleur" : name);
    const std::string value = trimmed(rawValue);
    if (const std::string e = checkName(name, kNoId); !e.empty()) { (void)fail(e, why); return kNoId; }
    if (const std::string e = checkValue(name, value); !e.empty()) { (void)fail(e, why); return kNoId; }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Nouvelle couleur @" + name, [&](hmi::Project& pr) {
        hmi::SemanticColor c;
        c.id = pr.allocate();
        c.name = name;
        c.value = value;
        made = c.id;
        pr.semanticColors.push_back(std::move(c));
    });
    if (!cmd) { (void)fail("non ajout\xC3\xA9" "e", why); return kNoId; }
    apply_(std::move(cmd));
    refresh();
    (void)select(name);
    say("@" + name + " ajout\xC3\xA9" "e : r\xC3\xA8gle sa couleur dans la fiche ; une case couleur la cite par @" + name + " (Ctrl+Z la retire).");
    return made;
}

Id HmiSemanticColorsPane::duplicateFrom(const std::string& rawFrom, const std::string& newName, std::string* why) {
    const std::string from = bareName(rawFrom);
    const auto* src = sem::find(doc_->project, from);
    if (!src) { (void)fail("@" + from + " : couleur inconnue", why); return kNoId; }
    const hmi::SemanticColor original = *src;
    std::string name = bareName(newName);
    if (!name.empty())
        if (const std::string e = checkName(name, kNoId); !e.empty()) { (void)fail(e, why); return kNoId; }
    Id made = kNoId;
    std::string madeName;
    auto cmd = hmi::changeProject(doc_, "Dupliquer @" + from, [&](hmi::Project& pr) {
        hmi::SemanticColor c = sem::duplicate(pr, original, name);
        made = c.id;
        madeName = c.name;
        pr.semanticColors.push_back(std::move(c));
    });
    if (!cmd) { (void)fail("non dupliqu\xC3\xA9" "e", why); return kNoId; }
    apply_(std::move(cmd));
    refresh();
    (void)select(madeName);
    say("@" + madeName + " : une copie \xC3\xA0 toi de @" + from + (original.builtin ? " (fournie)" : "") + " ; r\xC3\xA8gle-la dans la fiche (Ctrl+Z la retire).");
    return made;
}

bool HmiSemanticColorsPane::removeColor(Id color, std::string* why) {
    const auto* c = byId(doc_->project, color);
    if (!c) return fail("aucune couleur du projet choisie (une fournie ne se retire pas)", why);
    const std::string name = c->name;
    const std::size_t users = sem::users(doc_->project, name);
    std::size_t replaced = 0;
    auto cmd = hmi::changeProject(doc_, "Retirer @" + name, [&](hmi::Project& pr) {
        replaced = sem::forget(pr, name);
        std::erase_if(pr.semanticColors, [&](const hmi::SemanticColor& x) { return x.id == color; });
    });
    if (!cmd) return fail("non retir\xC3\xA9" "e", why);
    apply_(std::move(cmd));
    refresh();
    say("@" + name + " retir\xC3\xA9" "e (Ctrl+Z la rend)"
        + (users ? " : " + std::to_string(replaced) + " citation(s) gardent sa couleur, \xC3\xA9" "crite en dur." : std::string(".")));
    return true;
}

bool HmiSemanticColorsPane::renameColor(Id color, const std::string& raw, std::string* why) {
    const auto* c = byId(doc_->project, color);
    if (!c) return fail("une couleur fournie ne se renomme pas (Dupliquer \xC3\xA0 partir de\xE2\x80\xA6)", why);
    const std::string old = c->name;
    const std::string name = bareName(raw);
    if (name == old) return true;
    if (const std::string e = checkName(name, color); !e.empty()) return fail(e, why);
    std::size_t moved = 0;
    auto cmd = hmi::changeProject(doc_, "Renommer @" + old, [&](hmi::Project& pr) {
        moved = sem::renameReferences(pr, old, name);
        for (auto& x : pr.semanticColors)
            if (x.id == color) x.name = name;
    });
    if (!cmd) return fail("non renomm\xC3\xA9" "e", why);
    apply_(std::move(cmd));
    refresh();
    (void)select(name);
    say("@" + old + " devient @" + name + (moved ? " : " + std::to_string(moved) + " citation(s) suivent." : std::string(".")));
    return true;
}

bool HmiSemanticColorsPane::setValue(Id color, const std::string& raw, std::string* why) {
    const auto* c = byId(doc_->project, color);
    if (!c) return fail("une couleur fournie ne se modifie pas (Dupliquer \xC3\xA0 partir de\xE2\x80\xA6)", why);
    const std::string name = c->name;
    const std::string value = trimmed(raw);
    if (value == c->value) return true;
    if (const std::string e = checkValue(name, value); !e.empty()) return fail(e, why);
    auto cmd = hmi::changeProject(doc_, "Couleur de @" + name, [&](hmi::Project& pr) {
        for (auto& x : pr.semanticColors)
            if (x.id == color) x.value = value;
    });
    if (!cmd) return fail("non chang\xC3\xA9" "e", why);
    apply_(std::move(cmd));
    refresh();
    (void)select(name);
    const std::size_t users = sem::users(doc_->project, name);
    say("@" + name + " = " + value + (users ? " : " + std::to_string(users) + " citation(s) suivent." : std::string(".")));
    return true;
}

bool HmiSemanticColorsPane::setField(Id color, const std::string& key, const std::string& raw, std::string* why) {
    const auto* c = byId(doc_->project, color);
    if (!c) return fail("une couleur fournie ne se modifie pas (Dupliquer \xC3\xA0 partir de\xE2\x80\xA6)", why);
    if (key != "description" && key != "dossier") return fail("champ inconnu : " + key, why);
    const std::string value = key == "dossier" ? trimmed(raw) : raw;
    const std::string name = c->name;
    if ((key == "description" ? c->description : c->folder) == value) return true;
    auto cmd = hmi::changeProject(doc_, (key == "description" ? "Description de @" : "Dossier de @") + name, [&](hmi::Project& pr) {
        for (auto& x : pr.semanticColors)
            if (x.id == color) (key == "description" ? x.description : x.folder) = value;
    });
    if (!cmd) return fail("non chang\xC3\xA9", why);
    apply_(std::move(cmd));
    refresh();
    (void)select(name);
    return true;
}

std::vector<ui::PopupMenu::Item> HmiSemanticColorsPane::contextItems() const {
    std::vector<ui::PopupMenu::Item> items;
    const std::string name = selectedName();
    const auto add = [&](int action, std::string label, std::string key, ui::Icon icon, bool enabled, std::string whyNot = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.shortcut = std::move(key);
        it.icon = icon;
        it.enabled = enabled;
        it.disabledReason = std::move(whyNot);
        it.id = action;
        items.push_back(std::move(it));
    };
    if (!name.empty()) {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = "@" + name;
        head.shortcut = locked() ? "fournie, verrouill\xC3\xA9" "e" : "\xC3\xA0 toi";
        items.push_back(std::move(head));
    }
    add(ActAdd, "Nouvelle couleur s\xC3\xA9mantique", "Inser", ui::Icon::Plus, true);
    add(ActDuplicate, name.empty() ? std::string("Dupliquer \xC3\xA0 partir de\xE2\x80\xA6") : "Dupliquer \xC3\xA0 partir de \xC2\xAB @" + name + " \xC2\xBB",
        "Ctrl+D", ui::Icon::Duplicate, !name.empty(), "choisis une couleur");
    add(ActCopy, name.empty() ? std::string("Copier @Nom") : "Copier \xC2\xAB @" + name + " \xC2\xBB", "Ctrl+C", ui::Icon::Copy, !name.empty(),
        "choisis une couleur");
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    // 1.24.0 : plusieurs couleurs - << Retirer les 3 >> (les fournies restent, le compte rendu le dit).
    const std::size_t chosen = several::chosen(*table_, order_).size();
    add(ActRemove, several::label("Retirer", chosen), "Suppr", ui::Icon::Delete, tools_->isEnabled(ActRemove),
        locked() ? "une couleur fournie ne se retire pas" : "choisis une couleur \xC3\xA0 toi");
    if (chosen > 1)
        for (auto& it : items)
            if (it.id == ActDuplicate && it.enabled) {
                it.enabled = false;
                it.disabledReason = several::kOneAtATime;
            }
    return items;
}

void HmiSemanticColorsPane::runAction(int action) {
    const std::string name = selectedName();
    switch (action) {
        case ActAdd: (void)addColor(); break;
        case ActDuplicate:
            if (!name.empty()) (void)duplicateFrom(name);
            break;
        case ActCopy:
            if (!name.empty()) {
                ui::setClipboardText(sem::reference(name));
                say("\xC2\xAB " + sem::reference(name) + " \xC2\xBB copi\xC3\xA9 : colle-le dans une case couleur (ou '" + sem::reference(name)
                    + "' dans une formule).");
            }
            break;
        case ActRemove:
            // 1.24.0 : plusieurs - les tiennes partent en un seul Ctrl+Z (sans question, comme une seule) ; une fournie reste.
            if (const auto names = several::chosen(*table_, order_); names.size() > 1) {
                several::Batch b;
                b.verb = "Retirer";
                b.noun = "couleur";
                b.nouns = "couleurs";
                b.feminine = true;
                std::vector<Id> ids;
                for (const auto& n : names) {
                    const auto* c = sem::find(doc_->project, n);
                    b.names.push_back("@" + n);
                    ids.push_back(c && !c->builtin ? c->id : kNoId);
                }
                b.remove = [this, ids](std::size_t i, std::string* why) {
                    if (ids[i] == kNoId) {
                        if (why) *why = "fournie, elle ne se retire pas";
                        return false;
                    }
                    return removeColor(ids[i], why);
                };
                b.report = [this](const std::string& text, bool warning) { say(text + " (ce qui les cite garde leur couleur, \xC3\xA9" "crite en dur)", warning); };
                b.alive = tools_->triggered;
                several::run(std::move(b));
                break;
            }
            if (selectedColor() != kNoId) (void)removeColor(selectedColor());
            else if (!name.empty()) say("@" + name + " est fournie : elle ne se retire pas.", true);
            break;
        default: break;
    }
}

void HmiSemanticColorsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(420.f, b.w * 0.34f);
    table_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiSemanticColorsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
