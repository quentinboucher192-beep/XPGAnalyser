#include "HmiDesignPanes.hpp"

#include "HmiIcons.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>

namespace app {

using hmi::Id;
using hmi::kNoId;

using PG = ui::PropertyGrid;

namespace {

// Une table de chaines, dans l'ordre donne.
class Rows final : public ui::ITableModel {
public:
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<ui::CellStyle> styles = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(styles)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return headers_[c]; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return c == 0 && r < styles_.size() ? styles_[r] : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<ui::CellStyle> styles_;
};

PG::Property prop(std::string name, std::string value, PG::ValueType type, std::function<bool(std::string_view)> commit,
                  std::string help = {}, std::vector<std::string> choices = {}) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = type;
    p.commit = std::move(commit);
    p.description = std::move(help);
    p.enumValues = std::move(choices);
    return p;
}

// Les proprietes d'un style : leur libelle, leur genre de case.
struct StyleKey { const char* key; const char* label; PG::ValueType type; };
const StyleKey kStyleKeys[] = {
    {"fill", "Remplissage", PG::ValueType::Color},
    {"stroke", "Contour", PG::ValueType::Color},
    {"strokeWidth", "\xC3\x89paisseur du contour", PG::ValueType::Integer},
    {"radius", "Rayon des coins", PG::ValueType::Integer},
    {"opacity", "Opacit\xC3\xA9 (%)", PG::ValueType::Integer},
    {"textColor", "Couleur du texte", PG::ValueType::Color},
    {"font", "Police", PG::ValueType::Enum},
    {"fontSize", "Taille du texte", PG::ValueType::Integer},
    {"align", "Alignement", PG::ValueType::Enum},
    {"colorOn", "Couleur allum\xC3\xA9", PG::ValueType::Color},
    {"colorOff", "Couleur \xC3\xA9teint", PG::ValueType::Color},
    {"background", "Fond", PG::ValueType::Color},
};

hmi::Style* styleById(hmi::Project& p, Id id) {
    for (auto& s : p.styles) if (s.id == id) return &s;
    return nullptr;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

enum : int { SNew = 1, SDup, SDel, SFolder };

} // namespace

// =================================================================== styles ===
HmiStylesPane::HmiStylesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(SNew, HmiGlyph::Plus, "Nouveau style (fond sombre, texte blanc) : \xC3\xA0 r\xC3\xA9gler dans sa fiche", "Nouveau");
    tools->add(SDup, HmiGlyph::Duplicate, "Dupliquer le style", "Dupliquer");
    tools->add(SDel, HmiGlyph::Delete, "Supprimer le style (les objets gardent leurs valeurs et ne le citent plus), ou le dossier "
                                       "choisi (ses styles remontent d'un cran) - Ctrl+Z le rend",
               "Supprimer");
    tools->add(SFolder, HmiGlyph::Plus, "Nouveau dossier (sans effet sur les noms) : glisse des styles dessus pour les y ranger", "Dossier");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(SDup, [this] { return selectedStyle() != kNoId; });
    tools_->setEnabledWhen(SDel, [this] { return selectedStyle() != kNoId || !folders_->selectedFolder().empty(); });
    // Lot API 8 : chercher un style - la recherche de toute l'application.
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Rechercher : style, description, valeur, dossier\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 dans le nom du style, sa description, ses valeurs (fill #1B2028...) et son dossier - tous "
        "les mots (ET), sans casse ni accents ; \"une phrase\" entre guillemets ; -mot : l'exclure.")));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Style", 220.f}, {"Objets", 90.f, 50.f, true, true, true, ui::Align::End}, {"Valeurs", 330.f},
                       {"Description", 300.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    // Lot 21 : les styles ranges en dossiers ; glisser une ou plusieurs lignes.
    folders_ = std::make_unique<HmiFolderTable>(*table_, doc_, apply_);
    links_ += folders_->message->connect([this](const std::string& text) { say(text); });
    links_ += folders_->relayout->connect([this] { refresh(); });

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedStyle();
        if (a == SNew) selectStyle(addStyle());
        else if (a == SDup && sel != kNoId) selectStyle(duplicateStyle(sel));
        else if (a == SDel && sel != kNoId) (void)removeStyle(sel);
        else if (a == SDel) { if (const auto f = folders_->selectedFolder(); !f.empty()) (void)folders_->deleteFolder(f); }
        else if (a == SFolder) (void)folders_->newFolder();
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { rebuildProperties(); });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
    // Lot API 8 : la recherche, branchee, puis relue (retenue d'une seance a l'autre).
    links_ += search_->changed->connect([this] { refresh(); });
    search_->recall();
}

void HmiStylesPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

Id HmiStylesPane::selectedStyle() const { return folders_ ? folders_->selectedItem() : kNoId; }

void HmiStylesPane::selectStyle(Id id) {
    // Lot API 8 : un style demande (cree, duplique) que la recherche cache : elle s'efface.
    if (id != kNoId && !search_->text().empty() && folders_->rowOfItem(id) < 0)
        for (const auto& st : doc_->project.styles)
            if (st.id == id) {
                search_->setText("");       // -> refresh()
                break;
            }
    folders_->selectItem(id);
    rebuildProperties();
}

void HmiStylesPane::refresh() {
    const auto& p = doc_->project;
    std::vector<Id> shown;
    // Lot API 8 : la recherche - le nom, la description, les valeurs, le dossier.
    const ui::SearchQuery& query = search_->query();
    for (const auto& st : p.styles) {
        if (!query.empty()) {
            std::vector<std::string> texts{st.name, st.description, st.folder};
            for (const auto& prop : st.props) texts.push_back(prop.key + " " + prop.value);
            if (!query.matches(texts)) continue;
        }
        shown.push_back(st.id);
    }
    shown_ = shown.size();
    table_->setHighlight(search_->text());
    search_->setCount(shown.size(), p.styles.size());
    folders_->rebuild(hmi::fold::List::Styles, shown, {"Style", "Objets", "Valeurs", "Description"},
                      [&p](Id id) -> std::vector<std::string> {
                          const auto* s = p.style(id);
                          if (!s) return {};
                          std::string values;
                          for (const auto& prop : s->props) values += (values.empty() ? "" : ", ") + prop.key + " " + prop.value;
                          return {s->name, std::to_string(hmi::design::styleUsers(p, s->name)), values, s->description};
                      },
                      [](Id, std::size_t col) {
                          ui::CellStyle cs;
                          if (col == 0) cs.icon = ui::Icon::Settings;
                          return cs;
                      });
    if (message_.empty())
        status_->setMessage(std::to_string(p.styles.size()) + " style(s). Un objet cite un style par sa propri\xC3\xA9t\xC3\xA9 "
                            "\xC2\xAB Style nomm\xC3\xA9 \xC2\xBB (Apparence) ; l'\xC3\xA9" "diteur en cr\xC3\xA9" "e un depuis un objet.");
    rebuildProperties();
    invalidate();
}

void HmiStylesPane::rebuildProperties() {
    const Id sel = selectedStyle();
    const hmi::Style* s = sel != kNoId ? styleById(doc_->project, sel) : nullptr;
    if (!s) {
        grid_->clearProperties();
        return;
    }
    const auto commitWith = [this, sel](std::string key) {
        return [this, sel, key](std::string_view v) { return setStyleField(sel, key, std::string(v)); };
    };
    std::vector<PG::Category> cats;
    PG::Category head;
    head.name = "Style";
    head.properties.push_back(prop("Nom", s->name, PG::ValueType::Text, commitWith("nom"),
                                   "Unique. Renomm\xC3\xA9, les objets qui le citent suivent."));
    head.properties.push_back(prop("Description", s->description, PG::ValueType::Text, commitWith("description")));
    head.properties.push_back(prop("Objets qui le citent", std::to_string(hmi::design::styleUsers(doc_->project, s->name)),
                                   PG::ValueType::ReadOnly, {}));
    cats.push_back(std::move(head));
    PG::Category look;
    look.name = "Apparence (vide : le style ne la porte pas)";
    for (const auto& k : kStyleKeys) {
        std::string value;
        for (const auto& prop : s->props) if (prop.key == k.key) value = prop.value;
        std::vector<std::string> choices;
        if (std::string_view(k.key) == "font") choices = {"", "Sans", "Mono"};
        if (std::string_view(k.key) == "align") choices = {"", "gauche", "centre", "droite"};
        look.properties.push_back(prop(k.label, value, k.type, commitWith(k.key),
                                       "Chang\xC3\xA9" "e : les objets qui citent le style la prennent, sauf ceux qui l'avaient chang\xC3\xA9" "e "
                                       "eux-m\xC3\xAAmes.",
                                       std::move(choices)));
    }
    cats.push_back(std::move(look));
    grid_->setCategories(std::move(cats));
}

Id HmiStylesPane::addStyle(const std::string& name) {
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Nouveau style", [&](hmi::Project& p) {
        hmi::Style s;
        s.id = p.allocate();
        s.name = hmi::uniqueStyleName(p, name.empty() ? std::string("Style") : name);
        s.props = {{"fill", "#1B2028", ""}, {"stroke", "#3A4556", ""}, {"textColor", "#FFFFFF", ""}, {"fontSize", "16", ""}};
        made = s.id;
        p.styles.push_back(std::move(s));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Style cr\xC3\xA9\xC3\xA9 : \xC3\xA0 r\xC3\xA9gler dans sa fiche ; un objet le cite par \xC2\xAB Style nomm\xC3\xA9 \xC2\xBB.");
    return made;
}

Id HmiStylesPane::duplicateStyle(Id style) {
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Dupliquer le style", [&](hmi::Project& p) {
        const hmi::Style* src = styleById(p, style);
        if (!src) return;
        hmi::Style s = *src;
        s.id = p.allocate();
        s.name = hmi::uniqueStyleName(p, src->name + "_copie");
        made = s.id;
        p.styles.push_back(std::move(s));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return made;
}

bool HmiStylesPane::removeStyle(Id style) {
    std::string name;
    std::size_t freed = 0;
    auto cmd = hmi::changeProject(doc_, "Supprimer le style", [&](hmi::Project& p) {
        const hmi::Style* s = styleById(p, style);
        if (!s) return;
        name = s->name;
        freed = hmi::design::forgetStyle(p, name);
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    say("Style " + name + " supprim\xC3\xA9 : " + std::to_string(freed) + " objet(s) gardent leurs valeurs (Ctrl+Z le rend).");
    return true;
}

bool HmiStylesPane::setStyleField(Id style, const std::string& key, const std::string& raw, std::string* why) {
    const std::string value = trimmed(raw);
    std::string error;
    std::size_t followed = 0;
    std::string shownName;
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    const hmi::Style* current = styleById(doc_->project, style);
    if (!current) return fail("style introuvable");
    if (key == "nom") {
        if (value.empty()) return fail("un style a un nom");
        const auto* other = doc_->project.styleByName(value);
        if (other && other->id != style) return fail("'" + value + "' existe d\xC3\xA9j\xC3\xA0");
    } else if (key != "description") {
        const bool numeric = key == "strokeWidth" || key == "radius" || key == "opacity" || key == "fontSize";
        double n = 0;
        if (numeric && !value.empty() && (!hmi::parseNumber(value, n) || n < 0)) return fail(key + " : un nombre positif");
    }
    auto cmd = hmi::changeProject(doc_, "Style : " + key, [&](hmi::Project& p) {
        hmi::Style* s = styleById(p, style);
        if (!s) return;
        const hmi::Style before = *s;
        if (key == "nom") s->name = value;
        else if (key == "description") s->description = value;
        else {
            auto it = std::find_if(s->props.begin(), s->props.end(), [&](const hmi::Prop& x) { return x.key == key; });
            if (value.empty()) {
                if (it != s->props.end()) s->props.erase(it);
            } else if (it != s->props.end()) {
                it->value = value;
            } else {
                s->props.push_back({key, value, ""});
            }
        }
        const hmi::Style after = *s;
        shownName = after.name;
        followed = hmi::design::propagateStyle(p, before, after);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say(shownName + " : " + key + (value.empty() ? std::string(" retir\xC3\xA9") : " = " + value) + " \xE2\x80\x94 "
        + std::to_string(followed) + " objet(s) suivent (Ctrl+Z pour revenir).");
    return true;
}

void HmiStylesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float gridW = std::min(420.f, b.w * 0.4f);
    // Lot API 8 : la recherche sous la barre (au-dessus de la table), comme celle des alarmes.
    search_->setBounds({b.x + 8, b.y + 42, std::max(0.f, std::min(560.f, b.w - gridW - 20)), 28});
    table_->setBounds({b.x, b.y + 76, b.w - gridW - 4, std::max(0.f, b.h - 100)});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, std::max(0.f, b.h - 62)});
}

// ===================================================== rechercher / remplacer ===
HmiFindPane::HmiFindPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto f = std::make_unique<ui::InputText>(base + ".find");
    f->setPlaceholder("Rechercher : Armoires[0], Pompe_Marche, #2F6FD6...");
    find_ = &static_cast<ui::InputText&>(addChild(std::move(f)));
    auto r = std::make_unique<ui::InputText>(base + ".replace");
    r->setPlaceholder("Remplacer par (vide : effacer)");
    replace_ = &static_cast<ui::InputText&>(addChild(std::move(r)));
    scope_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".scope")));
    case_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Respecter la casse", base + ".case")));
    word_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Mot entier", base + ".word")));
    auto sb = std::make_unique<ui::Button>("Chercher", base + ".search");
    sb->setStyle(ui::Button::Style::Default);
    searchButton_ = &static_cast<ui::Button&>(addChild(std::move(sb)));
    auto rb = std::make_unique<ui::Button>("Remplacer tout", base + ".replaceAll");
    rb->setStyle(ui::Button::Style::Primary);
    replaceButton_ = &static_cast<ui::Button&>(addChild(std::move(rb)));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"O\xC3\xB9", 360.f}, {"Avant", 330.f}, {"Apr\xC3\xA8s", 330.f}, {"Fois", 60.f, 40.f, true, true, true, ui::Align::End}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += searchButton_->clicked->connect([this] { search(); });
    links_ += replaceButton_->clicked->connect([this] { (void)replaceAll(); });
    links_ += find_->textChanged->connect([this](const std::string&) { search(); });
    links_ += replace_->textChanged->connect([this](const std::string&) { search(); });
    links_ += scope_->selectionChanged->connect([this](int) { search(); });
    links_ += case_->stateChanged->connect([this](ui::Checkbox::State) { search(); });
    links_ += word_->stateChanged->connect([this](ui::Checkbox::State) { search(); });
    links_ += table_->activated->connect([this](ui::RowIndex row) {
        if (row < hits_.size() && hits_[row].view != kNoId) openHit->emit(hits_[row].view, hits_[row].object);
    });
    links_ += doc_->changed->connect([this](Id) {
        refreshScopes();
        search();
    });
    refreshScopes();
    search();
}

void HmiFindPane::refreshScopes() {
    const Id keep = scope_->selectedIndex() >= 0 && static_cast<std::size_t>(scope_->selectedIndex()) < scopes_.size()
                      ? scopes_[static_cast<std::size_t>(scope_->selectedIndex())]
                      : kNoId;
    scopes_ = {kNoId};
    std::vector<ui::DropDown::Item> items{{"Tout le projet", ""}};
    for (const auto& v : doc_->project.views) {
        scopes_.push_back(v.id);
        items.push_back({"Vue : " + v.name, v.name});
    }
    scope_->setItems(std::move(items));
    int at = 0;
    for (std::size_t i = 0; i < scopes_.size(); ++i) if (scopes_[i] == keep) at = static_cast<int>(i);
    scope_->setSelectedIndex(at);
}

hmi::design::FindOptions HmiFindPane::options() const {
    hmi::design::FindOptions o;
    o.matchCase = case_->isChecked();
    o.wholeWord = word_->isChecked();
    const int i = scope_->selectedIndex();
    o.view = i >= 0 && static_cast<std::size_t>(i) < scopes_.size() ? scopes_[static_cast<std::size_t>(i)] : kNoId;
    return o;
}

void HmiFindPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
}

void HmiFindPane::setFind(const std::string& text) { find_->setText(text); search(); }
void HmiFindPane::setReplace(const std::string& text) { replace_->setText(text); search(); }
void HmiFindPane::setMatchCase(bool on) { case_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked); search(); }
void HmiFindPane::setWholeWord(bool on) { word_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked); search(); }
void HmiFindPane::setScopeView(Id view) {
    for (std::size_t i = 0; i < scopes_.size(); ++i)
        if (scopes_[i] == view) scope_->setSelectedIndex(static_cast<int>(i));
    search();
}

void HmiFindPane::search() {
    hits_ = hmi::design::find(doc_->project, find_->text(), replace_->text(), options());
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles;
    std::size_t occurrences = 0;
    // Un extrait autour de la premiere occurrence (un long champ la cachait a
    // droite) ; les retours et separateurs en espaces ; coupe entre deux
    // caracteres UTF-8, jamais au milieu d'un.
    const auto excerpt = [](const std::string& s, std::size_t at) {
        const auto boundary = [&](std::size_t i) {
            while (i > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
            return i;
        };
        const std::size_t from = at > 24 ? boundary(at - 24) : 0;
        std::string one = from > 0 ? std::string("\xE2\x80\xA6") : std::string{};
        for (std::size_t i = from; i < s.size(); ++i) {
            const auto c = static_cast<unsigned char>(s[i]);
            one += c < 0x20 || c == 0x7F ? ' ' : s[i];
        }
        if (one.size() > 160) {
            std::size_t cut = 157;
            while (cut > 0 && (static_cast<unsigned char>(one[cut]) & 0xC0) == 0x80) --cut;
            one = one.substr(0, cut) + "\xE2\x80\xA6";
        }
        return one;
    };
    for (const auto& h : hits_) {
        rows.push_back({h.where, excerpt(h.before, h.first), excerpt(h.after, h.first), std::to_string(h.count)});
        ui::CellStyle cs;
        cs.icon = h.object != kNoId ? ui::Icon::Document : ui::Icon::Code;
        styles.push_back(cs);
        occurrences += h.count;
    }
    table_->setModel(std::make_shared<Rows>(std::vector<std::string>{"O\xC3\xB9", "Avant", "Apr\xC3\xA8s", "Fois"}, std::move(rows), std::move(styles)));
    if (find_->text().empty()) say("Tape ce qu'il faut chercher : l'aper\xC3\xA7u montre chaque champ touch\xC3\xA9, avant et apr\xC3\xA8s.");
    else if (hits_.empty()) say("\xC2\xAB " + find_->text() + " \xC2\xBB : introuvable" + (options().view != kNoId ? " dans cette vue." : "."));
    else
        say(std::to_string(occurrences) + " occurrence(s) dans " + std::to_string(hits_.size())
            + " champ(s). Remplacer tout : une seule commande (Ctrl+Z). Double-clic : ouvrir la vue.");
    invalidate();
}

std::size_t HmiFindPane::replaceAll() {
    const std::string text = find_->text(), replacement = replace_->text();
    if (text.empty()) { say("Rien \xC3\xA0 remplacer : le texte cherch\xC3\xA9 est vide.", true); return 0; }
    const auto o = options();
    std::size_t fields = 0;
    auto cmd = hmi::changeProject(doc_, "Remplacer " + text, [&](hmi::Project& p) { fields = hmi::design::replaceAll(p, text, replacement, o); });
    if (cmd) apply_(std::move(cmd));
    search();
    say(fields ? std::to_string(fields) + " champ(s) chang\xC3\xA9(s) : \xC2\xAB " + text + " \xC2\xBB \xE2\x86\x92 \xC2\xAB " + replacement
                     + " \xC2\xBB (Ctrl+Z pour revenir)."
               : std::string("Rien n'a chang\xC3\xA9."),
        fields == 0);
    return fields;
}

void HmiFindPane::onLayout() {
    const auto b = bounds();
    const float row = 34, pad = 8;
    const float half = (b.w - 3 * pad) / 2;
    find_->setBounds({b.x + pad, b.y + pad, half, row - 4});
    replace_->setBounds({b.x + 2 * pad + half, b.y + pad, half, row - 4});
    const float y2 = b.y + pad + row;
    scope_->setBounds({b.x + pad, y2, std::min(300.f, half * 0.6f), row - 6});
    float x = b.x + pad + std::min(300.f, half * 0.6f) + 12;
    case_->setBounds({x, y2 + 3, 170, row - 10});
    x += 176;
    word_->setBounds({x, y2 + 3, 130, row - 10});
    replaceButton_->setBounds({b.x + b.w - pad - 150, y2, 150, row - 6});
    searchButton_->setBounds({b.x + b.w - 2 * pad - 150 - 110, y2, 110, row - 6});
    const float top = y2 + row + 4;
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    table_->setBounds({b.x, top, b.w, std::max(0.f, b.y + b.h - 24 - top)});
}

} // namespace app
