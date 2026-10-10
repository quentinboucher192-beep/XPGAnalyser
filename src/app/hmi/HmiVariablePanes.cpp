// app/hmi/HmiVariablePanes.cpp - les variables IHM (dossiers, structures, tableaux,
// liaison) et les types IHM (lot 16).
#include "HmiVariablePanes.hpp"

#include "HmiAssetPanes.hpp"
#include "../RenameDialog.hpp"            // lot 7 : requestRename (en ligne : rien de plus a lier)
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiEnums.hpp"       // 1.10 (chantier E)
#include "../../hmi/HmiOperators.hpp"   // 1.10 (chantier S2)
#include "HmiOperatorPanes.hpp"           // 1.10 (chantier S2)
#include "HmiEnumPanes.hpp"               // 1.10 (chantier U) : le type enumeration
#include "../../hmi/HmiTypes.hpp"
#include "../../hmi/HmiTypeForms.hpp"     // 1.12.2 : Liste..., Vecteur..., Dictionnaire..., Tuple...
#include "../../core/AtomicFile.hpp"   // 1.11.16 : exporter les valeurs remanentes
#include "../../ui/Theme.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : les types, un seul catalogue
#include "HmiTypePicker.hpp"                // 1.11.19 (refonte, lot 6) : "Choisir un type..."

#include <algorithm>
#include <iterator>
#include <cctype>
#include <optional>

namespace app {

using hmi::Id;
using hmi::kNoId;
namespace ty = hmi::types;
namespace eq = hmi::equip;

namespace {

enum VarAction : int { VAdd = 1, VFolder, VType, VRename, VDuplicate, VDelete, VLink, VUnlink, VShow,
                       VTrend,                // 1.10 (chantier O) : la visualisation graphique
                       VRecalc,               // 1.11.8 : Recalculer la place memoire
                       VItems };              // 1.12.2 : les elements de la valeur initiale (une liste, un tableau...)
enum TypeAction : int { TAdd = 1, TMember, TUp, TDown, TRemoveMember, TDuplicate, TDelete,
                        TFolder,               // lot 21 : un dossier de types
                        TCopy, TPaste,         // 1.10 (chantier O) : les membres <-> Excel
                        TExport, TImport };    // 1.11.2 (decision 174) : les types voyagent (.xpgtypes)
// 1.10 (chantier O) : les entrees des volets dans le menu du clic droit d'une
// table. La table garde ses id (1 a 5, 99, 100 et plus) ; celles du volet :
// kMenuBase - action (des id negatifs, -1 etant un trait).
enum VarMenu : int { MTrend = 1, MCopyName, MRename, MDuplicate, MDelete,
                     MInternal, MInternalAll, MAttach, MAttachAll, MRecalc, MRestore,     // 1.11.8
                     MRetainReset, MRetainResetAll, MRetainExport, MRetainImport, MRetainWhere, MRetainCheck };   // 1.11.16

// 1.11.8 : le meme membre dans toutes les cases : chaque indice devient [*] ("[0].NOM" -> "[*].NOM").
std::string allElementsOf(std::string_view rel) {
    std::string out;
    for (std::size_t i = 0; i < rel.size(); ++i) {
        if (rel[i] != '[') {
            out += rel[i];
            continue;
        }
        const auto close = rel.find(']', i);
        if (close == std::string_view::npos) {
            out += rel.substr(i);
            break;
        }
        out += "[*]";
        i = close;
    }
    return out;
}
constexpr int kMenuBase = -1000;
enum Col : std::size_t { CName, CType, CInitial, CRetain, CEquipment, CAddress, CAccess, CPlace, CQuality, CCount };   // 1.11.16 : CRetain, apres Initiale

const std::string kDash = "\xE2\x80\x94";
const std::string kRW = "lecture, \xC3\xA9" "criture";
const std::string kRO = "lecture seule";
const std::string kArray = "Tableau\xE2\x80\xA6";
// 1.12.2 : les types objets (dans la memoire de l'IHM, sans equipement).
const std::string kList = "Liste\xE2\x80\xA6";
const std::string kVector = "Vecteur\xE2\x80\xA6";
const std::string kMap = "Dictionnaire (MAP)\xE2\x80\xA6";
const std::string kTuple = "Tuple\xE2\x80\xA6";
// La forme d'une entree Liste..., Vecteur... ; vide : pas une telle entree.
std::string shapeOfChoice(const std::string& text) {
    if (text == kList) return "liste";
    if (text == kVector) return "vecteur";
    if (text == kMap) return "map";
    if (text == kTuple) return "tuple";
    return {};
}
const std::string& kPickType = typepicker::kChoose;              // 1.11.19 (lot 6) : le selecteur de types
constexpr long long kShown = 200;       // les cases montrees d'un tableau deplie

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool sameText(std::string_view a, std::string_view b) { return upper(a) == upper(b); }

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// 1.11.16 : la case Remanente - oui, non ; liee : sans objet (l'equipement garde sa valeur).
std::string retainCellOf(const hmi::Variable& v) {
    if (v.bound()) return v.retain ? std::string("oui (li\xC3\xA9" "e : sans effet)") : std::string("\xE2\x80\x94");
    return v.retain ? "oui" : "non";
}
// oui / non, comme Excel ou une personne l'ecrivent (VRAI, x, 1, vide...) ; faux : illisible.
bool yesNo(std::string_view text, bool& out) {
    std::string u(text);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.back()))) u.pop_back();
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.front()))) u.erase(u.begin());
    if (u == "OUI" || u == "VRAI" || u == "TRUE" || u == "1" || u == "X" || u == "O" || u == "YES" || u.rfind("OUI (", 0) == 0) {
        out = true;
        return true;
    }
    if (u == "NON" || u == "FAUX" || u == "FALSE" || u == "0" || u == "N" || u == "NO" || u.empty() || u == "-" || u == "\xE2\x80\x94") {
        out = false;
        return true;
    }
    return false;
}

// "registres 43001 a 43002" (une adresse Modicon), "mots 3000 a 3001" (Schneider).
std::string placeOf(const std::string& address, const std::string& type) {
    if (address.empty()) return {};
    hmi::Variable probe;
    probe.type = type;
    hmi::comm::Point pt;
    if (!eq::placeEquipmentAddress(address, eq::registerType(probe), pt)) return {};
    return address.front() == '%' ? pt.placeText() : eq::modiconText(pt);
}

// `members` : la liste d'un membre de type IHM (une place fixe : ni liste, ni MAP, ni tuple).
std::vector<std::string> typeChoices(const hmi::Project& p, std::string_view except = {}, bool members = false) {
    std::vector<std::string> out;
    for (const auto& t : hmi::typereg::baseRegistry().names(hmi::typereg::UseVariable)) out.push_back(t);   // 1.11.19 : le registre
    for (const auto& t : p.programs.types)
        if (except.empty() || !sameText(t.name, except)) out.push_back(t.name);
    out.push_back(kArray);
    if (!members)
        for (const auto* k : {&kList, &kVector, &kMap, &kTuple}) out.push_back(*k);    // 1.12.2
    if (app::typepicker::available()) out.push_back(kPickType);
    return out;
}

bool validInitial(const hmi::Project& p, const std::string& type, const std::string& initial, std::string* why) {
    if (trimmed(initial).empty()) return true;
    // 1.12.2 : un type objet - un litteral que le moteur range dans son type ([1, 2, 3], ['a' := 1], (1, 'x')).
    if (ty::isRich(type)) {
        std::string w;
        if (hmi::typeform::valueFits(p, type, initial, &w)) return true;
        return fail(why, "valeur initiale illisible pour un " + ty::normalized(type) + " : " + w + " (" + std::string(hmi::typeform::valueHint(hmi::typeform::decompose(type).form)) + ")");
    }
    // 1.10 (chantier U) : une enumeration prend le nom d'une de ses valeurs (T_MODE#Auto, le texte, le nombre).
    if (const auto* e = hmi::findEnumeration(p, type)) {
        std::int64_t n = 0;
        if (hmi::enumNumberOf(*e, initial, n)) return true;
        std::string names;
        for (std::size_t i = 0; i < e->values.size() && i < 6; ++i) names += (names.empty() ? "" : ", ") + e->values[i].name;
        return fail(why, "valeur inconnue de " + e->name + " : " + names);
    }
    ty::Spec s;
    if (ty::isComposite(type) && ty::parseSpec(type, s) && !(s.array() && ty::isElementary(s.element)))
        return fail(why, "une structure prend les valeurs initiales de son type (Types IHM)");
    const auto f = ty::isComposite(type) ? ty::flatten(p, "x", type, initial) : ty::Flat{};
    std::vector<std::string> texts;
    if (ty::isComposite(type)) for (const auto& l : f.leaves) texts.push_back(l.initial);
    else texts.push_back(initial);
    for (const auto& text : texts) {
        if (text.empty()) continue;
        const auto e = hmi::Expression::compile(text);
        if (!e.valid()) return fail(why, "valeur initiale illisible : " + text + " (" + e.error() + ")");
    }
    return true;
}

// 1.10 (chantier U) : la valeur initiale d'une variable d'enumeration, montree
// << Auto (1) >> (vide : la premiere valeur ; inconnue : telle quelle, avec ?).
std::string enumInitialText(const hmi::HmiType& e, const std::string& initial) {
    std::int64_t n = 0;
    if (trimmed(initial).empty()) return e.values.empty() ? std::string{} : hmi::enumDisplay(e, e.values.front().value);
    if (hmi::enumNumberOf(e, initial, n)) return hmi::enumDisplay(e, n);
    return initial + " (?)";
}

// ---- 1.10 (chantier O) : la table des membres d'un type ------------------------
// Ctrl+C copie avec les titres du collage (Nom, Type, Valeur initiale...) ; le
// clic droit (sur une ligne, ou sous les lignes : coller dans un type vide)
// ouvre le menu du volet.
class MemberTable final : public ui::TableView {
public:
    using ui::TableView::TableView;
    std::function<bool()>           onCopy;
    std::function<void(gfx::Point)> onContextMenu;      // le menu de la table s'ouvre : le volet y met les siennes
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev);
            k && k->key == ui::Key::C && k->mods.ctrl && !k->mods.alt && focused() && !cellEditing() && onCopy && onCopy())
            return ui::EventResult::Consumed;
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right && onContextMenu) {
            const auto r = ui::TableView::onEvent(ev);      // la ligne choisie (comme Excel), le menu
            if (contextMenu() && contextMenu()->isOpen()) onContextMenu(d->pos);
            return r;
        }
        return ui::TableView::onEvent(ev);
    }
};

// L'apercu d'un collage de membres : ce qui sera ajoute, remplace, refuse, et
// les boutons Appliquer / Annuler.
class PastePreview final : public ui::Widget {
public:
    struct Line { std::string text; int tone{0}; };     // 0 : le resume, 1 : ajoute, 2 : remplace, 3 : refuse
    explicit PastePreview(std::string id) : ui::Widget(std::move(id)) {
        apply_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Appliquer", this->id() + ".appliquer")));
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", this->id() + ".annuler")));
    }
    void setLines(std::vector<Line> lines) {
        lines_ = std::move(lines);
        invalidate();
    }
    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return lines_; }
    [[nodiscard]] float wantedHeight() const {
        return 34.f + 18.f * static_cast<float>(std::min<std::size_t>(lines_.empty() ? 0 : lines_.size() - 1, 6));
    }
    [[nodiscard]] ui::Button& applyButton() noexcept { return *apply_; }
    [[nodiscard]] ui::Button& cancelButton() noexcept { return *cancel_; }
protected:
    void onLayout() override {
        const auto b = bounds();
        cancel_->setBounds({b.right() - 100.f, b.y + 5.f, 92.f, 24.f});
        apply_->setBounds({b.right() - 200.f, b.y + 5.f, 96.f, 24.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.headerBg);
        ctx.r.line({b.x, b.y}, {b.right(), b.y}, c.accent, 2.f);
        float y = b.y + 9.f;
        for (std::size_t i = 0; i < lines_.size() && i < 7; ++i) {
            const auto& l = lines_[i];
            const gfx::Color col = l.tone == 1 ? c.ok : l.tone == 2 ? c.info : l.tone == 3 ? c.error : c.text;
            ctx.r.drawText({b.x + 10.f, y}, l.text, i == 0 ? ctx.theme.font.uiBold : ctx.theme.font.smallUi, col);
            y += i == 0 ? 22.f : 18.f;
        }
    }
private:
    ui::Button*       apply_{nullptr};
    ui::Button*       cancel_{nullptr};
    std::vector<Line> lines_;
};

// ---- Lot API 8 : la table des variables ----------------------------------------
// F2 et le double-clic sur le nom d'une variable demandent d'abord au volet
// (onRename, onNameDoubleClick : le dialogue Renommer s'ouvre-t-il ?) ; sinon -
// un dossier, un membre, pas d'ecran d'analyse - la table fait comme avant (la
// case s'edite sur place).
class VarTable final : public ui::TableView {
public:
    using ui::TableView::TableView;
    std::function<bool()>               onRename;            // F2 sur la ligne choisie
    std::function<bool(ui::RowIndex)>   onNameDoubleClick;   // la ligne du modele
    std::function<void(gfx::Point)>    onContextMenu;       // 1.10 : le menu de la table s'ouvre
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        // 1.10 (chantier O) : le clic droit ouvre le menu de la table (la ligne
        // choisie si elle ne l'etait pas, comme Excel : Copier, Coller...) ; le
        // volet y met ses entrees en tete (Ouvrir une visualisation graphique...).
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right && onContextMenu) {
            const auto r = ui::TableView::onEvent(ev);
            if (contextMenu() && contextMenu()->isOpen()) onContextMenu(d->pos);
            return r;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev);
            k && k->key == ui::Key::F2 && k->mods.none() && !k->repeat && focused() && !cellEditing() && onRename && onRename())
            return ui::EventResult::Consumed;
        if (const auto* d = std::get_if<ui::MouseDown>(&ev);
            d && d->button == ui::MouseButton::Left && d->clickCount >= 2 && d->mods.none() && !cellEditing() && onNameDoubleClick)
            for (std::size_t i = 0; i < visibleRowCount(); ++i) {
                gfx::Rect cell{}, arrow{};
                if (!cellRect(i, CName, cell) || !cell.contains(d->pos)) continue;
                // La fleche d'une structure se deplie, comme avant.
                if (!(expanderRect(i, arrow) && arrow.contains(d->pos)) && onNameDoubleClick(viewRow(i))) return ui::EventResult::Consumed;
                break;
            }
        return ui::TableView::onEvent(ev);
    }
};

// La table en arbre : les lignes du volet, et ce qu'il accepte d'editer.
class VarModel final : public ui::ITableModel {
public:
    using Row = HmiVariablesPane::Row;
    using Editable = std::function<bool(std::size_t, std::size_t)>;
    using Choices = std::function<std::vector<std::string>(std::size_t, std::size_t)>;
    using Commit = std::function<bool(std::size_t, std::size_t, const std::string&)>;
    VarModel(std::vector<Row> rows, const hmi::Project* p, Editable e, Choices c, Commit m)
        : rows_(std::move(rows)), p_(p), editable_(std::move(e)), choices_(std::move(c)), commit_(std::move(m)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"Nom", "Type", "Initiale", "R\xC3\xA9manente", "\xC3\x89quipement", "Adresse", "Acc\xC3\xA8s", "Place Modbus", "Qualit\xC3\xA9 (en marche)"};
        return c < CCount ? h[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].cells.size() ? rows_[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= rows_.size()) return st;
        const Row& row = rows_[r];
        const auto tone = [](int t) {
            return t == 1 ? ui::Tone::Ok : t == 2 ? ui::Tone::Warning : t == 3 ? ui::Tone::Error : ui::Tone::None;
        };
        if (c == CName) {
            st.indent = static_cast<float>(row.depth) * 18.f;
            st.expander = row.expander;
            switch (row.kind) {
                case Row::Kind::Folder:
                    st.bold = true;
                    st.icon = row.expander == 1 ? ui::Icon::FolderOpen : ui::Icon::Folder;
                    st.iconTone = ui::Tone::Warning;
                    break;
                case Row::Kind::Variable: {
                    st.bold = true;
                    ty::Spec s;
                    const bool parsed = ty::parseSpec(row.type, s);
                    if (parsed && s.array()) {
                        st.icon = ui::Icon::AnimationTable;
                        st.iconTone = ui::Tone::Family2;
                    } else if (parsed && !ty::isElementary(s.element)) {
                        st.icon = ui::Icon::DerivedType;
                        st.iconTone = ui::Tone::Accent;
                    } else {
                        st.icon = row.cells.size() > CEquipment && row.cells[CEquipment] != "(locale)" ? ui::Icon::LocatedVariable : ui::Icon::Variable;
                    }
                    break;
                }
                case Row::Kind::Member: break;
                case Row::Kind::More: st.fgTone = ui::Tone::Muted; break;
            }
        }
        if (row.kind == Row::Kind::Folder && c != CName) st.fgTone = ui::Tone::Muted;
        if (row.kind == Row::Kind::Variable && c == CType && ty::isComposite(row.type)) st.fgTone = ui::Tone::Accent;
        if (c == CEquipment && row.cells.size() > c && (row.cells[c] == "(locale)" || row.kind == Row::Kind::Member)) st.fgTone = ui::Tone::Muted;
        if (c == CAccess && row.kind == Row::Kind::Member) st.fgTone = ui::Tone::Muted;
        if (c == CAddress) st.monospace = row.kind != Row::Kind::Folder;
        if (c == CAddress && row.kind == Row::Kind::Member && row.cells.size() > c && row.cells[c].find('\xE2') != std::string::npos)
            st.fgTone = ui::Tone::Warning;           // une adresse corrigee (le crayon)
        if (c == CQuality) st.fgTone = tone(row.tone);
        // 1.11.16 : Remanente - oui en vert, non en gris ; liee mais cochee : l'avertissement.
        if (c == CRetain && row.kind == Row::Kind::Variable && row.cells.size() > c)
            st.fgTone = row.cells[c] == "oui" ? ui::Tone::Ok : row.cells[c] == "non" || row.cells[c] == kDash ? ui::Tone::Muted : ui::Tone::Warning;
        (void)p_;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return editable_ && editable_(r, c); }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex r, std::size_t c) const override {
        return choices_ ? choices_(r, c) : std::vector<std::string>{};
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override { return commit_ && commit_(r, c, std::string(text)); }
    // Glisser une variable sur un dossier (ou sous les lignes : la racine) - pas
    // sur celui ou elle est deja.
    [[nodiscard]] bool canDrop(ui::RowIndex from, ui::RowIndex to) const override {
        if (from >= rows_.size() || rows_[from].kind != Row::Kind::Variable || !p_) return false;
        const auto* v = p_->variableById(rows_[from].var);
        if (!v) return false;
        if (to == ui::TableView::kNoRow) return !v->folder.empty();
        return to < rows_.size() && rows_[to].kind == Row::Kind::Folder && !sameText(rows_[to].key, v->folder);
    }
    // Lot 21 : PLUSIEURS LIGNES - des variables et des dossiers - sur un dossier
    // (ou sous les lignes : la racine) ; des variables entre deux variables :
    // reordonner.
    [[nodiscard]] bool canDropRows(const std::vector<ui::RowIndex>& from, ui::RowIndex to, ui::TreeView::DropWhere where) const override {
        using W = ui::TreeView::DropWhere;
        if (!p_ || from.empty()) return false;
        bool anyFolder = false;
        for (const auto f : from) {
            if (f >= rows_.size()) return false;
            const auto k = rows_[f].kind;
            if (k != Row::Kind::Variable && k != Row::Kind::Folder) return false;
            anyFolder = anyFolder || k == Row::Kind::Folder;
        }
        const auto folderOf = [&](const Row& r) {
            if (r.kind == Row::Kind::Folder) return ty::folderParent(r.key);
            const auto* v = p_->variableById(r.var);
            return v ? v->folder : std::string{};
        };
        if (to == ui::TableView::kNoRow || (to < rows_.size() && rows_[to].kind == Row::Kind::Folder)) {
            if (where != W::Into) return false;
            const std::string into = to == ui::TableView::kNoRow ? std::string{} : rows_[to].key;
            bool useful = false;
            for (const auto f : from) {
                const auto& r = rows_[f];
                if (r.kind == Row::Kind::Folder && (sameText(r.key, into) || (!into.empty() && upper(into).rfind(upper(r.key) + "/", 0) == 0)))
                    return false;                                   // pas dans lui-meme
                if (!sameText(folderOf(r), into)) useful = true;
            }
            return useful;
        }
        return to < rows_.size() && rows_[to].kind == Row::Kind::Variable && !anyFolder && where != W::Into;
    }
private:
    std::vector<Row>   rows_;
    const hmi::Project* p_;
    Editable editable_;
    Choices  choices_;
    Commit   commit_;
};

// Un modele simple : du texte, un style, et l'edition.
class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    using Editable = std::function<bool(std::size_t, std::size_t)>;
    using Choices = std::function<std::vector<std::string>(std::size_t, std::size_t)>;
    using Commit = std::function<bool(std::size_t, std::size_t, const std::string&)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {}, Editable e = {}, Choices ch = {},
         Commit m = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)), editable_(std::move(e)), choices_(std::move(ch)),
          commit_(std::move(m)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return editable_ && editable_(r, c); }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex r, std::size_t c) const override {
        return choices_ ? choices_(r, c) : std::vector<std::string>{};
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override { return commit_ && commit_(r, c, std::string(text)); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    Style    style_;
    Editable editable_;
    Choices  choices_;
    Commit   commit_;
};

using PG = ui::PropertyGrid;
PG::Property prop(std::string name, std::string value, PG::ValueType t = PG::ValueType::ReadOnly,
                  std::function<bool(std::string_view)> commit = {}, std::vector<std::string> choices = {}, std::string help = {}) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = commit ? t : PG::ValueType::ReadOnly;
    p.commit = std::move(commit);
    p.enumValues = std::move(choices);
    p.description = std::move(help);
    return p;
}

// Le bandeau des filtres : ses controles se placent a la main.
class FilterBar final : public ui::Widget {
public:
    explicit FilterBar(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        float x = b.x + 8.f;
        const float widths[] = {230.f, 300.f, 250.f, 250.f};
        for (std::size_t i = 0; i < children().size() && i < 4; ++i) {
            const float h = i < 2 ? 26.f : 24.f;
            children()[i]->setBounds({x, b.y + (b.h - h) * 0.5f, widths[i], h});
            x += widths[i] + 12.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
        ctx.r.line({bounds().x, bounds().bottom() - 1.f}, {bounds().right(), bounds().bottom() - 1.f}, ctx.theme.color.border, 1.f);
    }
};

} // namespace

// =============================================================================
//  Variables IHM
// =============================================================================
HmiVariablesPane::HmiVariablesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(VAdd, HmiGlyph::Plus, "Nouvelle variable IHM, dans le dossier choisi (son nom s'\xC3\xA9" "crit tout de suite)", "Variable");
    tools->add(VFolder, HmiGlyph::Plus, "Nouveau dossier, dans le dossier choisi (comme un filtre de Visual Studio : sans effet sur les noms)", "Dossier");
    tools->add(VType, HmiGlyph::Plus, "Nouveau type IHM (une structure) : l'onglet Types IHM", "Type");
    tools->separator();
    tools->add(VRename, HmiGlyph::Text, "Renommer (F2, ou double-clic sur le nom) : ce qui la cite suit, montr\xC3\xA9 avant ; un dossier se renomme sur place", "Renommer");
    tools->add(VDuplicate, HmiGlyph::Duplicate, "Dupliquer la variable", "Dupliquer");
    tools->add(VDelete, HmiGlyph::Delete, "Supprimer la variable ou le dossier (ses variables montent d'un cran) - Ctrl+Z le rend", "Supprimer");
    tools->add(VItems, HmiGlyph::List, "\xC3\x89" "diter les \xC3\xA9l\xC3\xA9ments de la valeur initiale (une liste, un vecteur, un tableau, un dictionnaire, "
               "un tuple) : un par ligne", "\xC3\x89l\xC3\xA9ments\xE2\x80\xA6");
    tools->separator();
    // 1.10 (chantier O) : les variables choisies dans une fenetre graphique temporaire.
    tools->add(VTrend, HmiGlyph::Trend, "Ouvrir une visualisation graphique des variables choisies (aussi au clic droit) : "
               "une fen\xC3\xAAtre temporaire, en direct pendant la simulation", "Visualisation graphique");
    tools->separator();
    tools->add(VLink, HmiGlyph::Link, "Lier \xC3\xA0 un \xC3\xA9quipement : le premier \xC3\xA9quipement Modbus, \xC3\xA0 la prochaine adresse libre (\xC3\xA0 changer dans la ligne)",
               "Lier \xC3\xA0 un \xC3\xA9quipement\xE2\x80\xA6");
    tools->add(VUnlink, HmiGlyph::Delete, "D\xC3\xA9lier : la variable reste dans l'IHM", "D\xC3\xA9lier");
    tools->add(VShow, HmiGlyph::Search, "Voir dans \xC3\x89quipements (Plan d'adressage)", "Voir dans \xC3\x89quipements");
    // 1.11.8 : apres avoir rendu des membres internes (clic droit sur un membre), leur place est rendue.
    tools->add(VRecalc, HmiGlyph::Refresh, "Recalculer la place m\xC3\xA9moire de la structure : les mots des membres internes sont rendus, "
               "les membres suivants se resserrent (Ctrl+Z la remet)", "Recalculer la place m\xC3\xA9moire");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto variableChosen = [this] { return selectedVariable() != kNoId && selectedPath().empty(); };
    tools_->setEnabledWhen(VRename, [this] { return selectedVariable() != kNoId || !selectedFolder().empty(); });
    tools_->setEnabledWhen(VDuplicate, variableChosen);
    tools_->setEnabledWhen(VItems, [this, variableChosen] {   // 1.12.2
        const auto* v = doc_->project.variableById(selectedVariable());
        ty::Spec s;
        return variableChosen() && v && app::itemseditor::available()
            && (ty::isRich(v->type) || (ty::parseSpec(v->type, s) && s.array() && ty::isElementary(s.element)));
    });
    tools_->setEnabledWhen(VDelete, [this] { return (selectedVariable() != kNoId && selectedPath().empty()) || !selectedFolder().empty(); });
    tools_->setEnabledWhen(VTrend, [this] { return table_ && !table_->selectedModelRows().empty(); });   // 1.10 (le clic dit s'il n'y a rien a tracer)
    tools_->setEnabledWhen(VLink, [this] {
        const auto* v = doc_->project.variableById(selectedVariable());
        return v && !v->bound() && !ty::isRich(v->type);   // 1.12.2 : un objet de l'IHM ne se lie pas
    });
    tools_->setEnabledWhen(VUnlink, [this] {
        const auto* v = doc_->project.variableById(selectedVariable());
        return v && v->bound();
    });
    tools_->setEnabledWhen(VShow, [this] {
        const auto* v = doc_->project.variableById(selectedVariable());
        return v && v->bound() && hosts_.showEquipment;
    });
    tools_->setEnabledWhen(VRecalc, [this] {
        const auto* v = doc_->project.variableById(selectedVariable());
        return v && v->bound() && ty::isComposite(v->type);
    });

    auto bar = std::make_unique<FilterBar>(base + ".filters");
    {
        auto box = std::make_unique<ui::DropDown>(base + ".folder");
        folderBox_ = &static_cast<ui::DropDown&>(bar->addChild(std::move(box)));
        auto search = std::make_unique<ui::InputText>(base + ".search");
        search->setPlaceholder("Rechercher : nom, type, \xC3\xA9quipement, adresse\xE2\x80\xA6");
        search_ = &static_cast<ui::InputText&>(bar->addChild(std::move(search)));
        expandAll_ = &static_cast<ui::Checkbox&>(bar->addChild(std::make_unique<ui::Checkbox>("d\xC3\xA9plier structures et tableaux", base + ".expandAll")));
        boundOnly_ = &static_cast<ui::Checkbox&>(bar->addChild(std::make_unique<ui::Checkbox>("seulement les variables li\xC3\xA9" "es", base + ".boundOnly")));
    }
    addChild(std::move(bar));

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto table = std::make_unique<VarTable>(base + ".table");     // lot API 8 : F2, double-clic sur le nom
    // ---- Lot API 8 : F2 et le double-clic sur le nom d'une variable ouvrent le dialogue ----
    table->onRename = [this] { return renameSelectedInDialog(); };
    table->onNameDoubleClick = [this](ui::RowIndex r) {
        return r < rows_.size() && rows_[r].kind == Row::Kind::Variable && renameInDialog(rows_[r].var);
    };
    table->onContextMenu = [this](gfx::Point at) { openContextMenu(at); };     // 1.10 (chantier O)
    // ---- fin Lot API 8 ----
    table->setColumns({{"Nom", 240.f, 80.f, true, false}, {"Type", 175.f, 60.f, true, false}, {"Initiale", 80.f, 40.f, true, false},
                       {"R\xC3\xA9manente", 92.f, 50.f, true, false},   // 1.11.16 : la remanence d'exploitation (pres de la valeur initiale)
                       {"\xC3\x89quipement", 140.f, 60.f, true, false}, {"Adresse", 86.f, 50.f, true, false}, {"Acc\xC3\xA8s", 112.f, 50.f, true, false},
                       {"Place Modbus", 180.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Qualit\xC3\xA9 (en marche)", 170.f, 60.f, true, false, true, ui::Align::Start, 0, false}});
    // Lot 20 : plusieurs lignes se choisissent (Maj, Ctrl) - pour les copier vers Excel.
    table->setSelectionMode(ui::SelectionMode::Extended);
    // Lot recherche : LES FILTRES DES COLONNES. Le volet les applique (Host) : ils
    // choisissent des variables ; les dossiers gardent leur compte, les membres
    // suivent leur variable. La place et la qualite (en marche) ne se filtrent pas.
    table->setColumnFiltersEnabled(true);
    table->setColumnFilterMode(ui::TableView::ColumnFilterMode::Host);
    table->setColumnValuesProvider([this](std::size_t col, const std::function<void(const std::string&)>& emit) {
        for (const auto& v : doc_->project.programs.variables)
            if (passes(v, static_cast<int>(col))) emit(cellOf(v, col));
    });
    table_ = &static_cast<ui::TableView&>(split->addPane(std::move(table), 0.76f, 400.f));
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
    grid->setShowDescriptionPane(true);
    grid->setNameColumnRatio(0.44f);
    props_ = &static_cast<ui::PropertyGrid&>(split->addPane(std::move(grid), 0.24f, 260.f));
    // Lot 20 : coller depuis Excel (Ctrl+V, le clic droit) - un seul Ctrl+Z.
    paste_.table = table_;
    paste_.keyColumn = CName;
    paste_.target = [this](const ui::TableView::PasteRequest& rq) { return pasteTarget(rq); };
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
        if (!rep.createdKeys.empty()) selectPath(rep.createdKeys.front());
        else if (!rep.updatedKeys.empty()) selectPath(rep.updatedKeys.front());
    };
    paste::bind(paste_);
    links_ += doc_->changed->connect([this](hmi::Id) { paste::forget(paste_); });
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    // 1.10 (chantier O) : le menu du clic droit est celui de la table ; les
    // entrees du volet y portent des id negatifs (kMenuBase - action).
    menu_ = table_->contextMenu();
    if (menu_)
        links_ += menu_->itemChosen->connect([this](int a) {
            if (a < kMenuBase && a > kMenuBase - 100) runMenu(kMenuBase - a);
        });

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedVariable();
        switch (a) {
            case VAdd: {
                std::string folder = selectedFolder();
                if (folder.empty())
                    if (const auto* v = doc_->project.variableById(sel)) folder = v->folder;
                std::string why;
                const Id made = addVariable(hmi::uniqueVariableName(doc_->project, "Variable"), "INT", folder, &why);
                if (made != kNoId) {
                    const int row = rowOf(doc_->project.variableById(made)->name);
                    // Lot API 8 : son premier nom s'ecrit sur place (rien ne la cite encore).
                    if (row >= 0 && table_->beginCellEdit(static_cast<ui::RowIndex>(row), CName)) naming_ = made;
                }
                break;
            }
            case VFolder: {
                const std::string parent = newFolderParent();
                std::string name = "Nouveau dossier";
                const auto exists = [&](const std::string& leaf) {
                    const std::string path = parent.empty() ? leaf : parent + "/" + leaf;
                    const auto all = ty::allFolders(doc_->project);
                    return std::any_of(all.begin(), all.end(), [&](const std::string& f) { return sameText(f, path); });
                };
                for (int i = 2; exists(name) && i < 1000; ++i) name = "Nouveau dossier " + std::to_string(i);
                const std::string path = parent.empty() ? name : parent + "/" + name;
                if (addFolder(path)) {
                    if (!parent.empty()) setExpanded(parent, true);
                    selectFolder(path);
                    const int row = rowOf(path);
                    if (row >= 0) (void)table_->beginCellEdit(static_cast<ui::RowIndex>(row), CName);
                }
                break;
            }
            case VType:
                if (hosts_.newType) hosts_.newType();
                break;
            case VRename: {
                // Lot 7 : une variable - le dialogue qui montre ce qui la cite (vues,
                // scripts, alarmes, tables d'animation) et le reecrit ; un dossier,
                // ou sans l'ecran d'analyse : dans sa case, comme avant.
                if (const auto* v = doc_->project.variableById(sel); v && selectedFolder().empty() && requestRename("ihm-variable", v->name)) break;
                const auto rowsSel = table_->selectedModelRows();
                if (!rowsSel.empty()) (void)table_->beginCellEdit(rowsSel.front(), CName);
                break;
            }
            case VDuplicate:
                if (sel) (void)duplicateVariable(sel);
                break;
            case VItems:   // 1.12.2 : la valeur initiale, un element par ligne
                if (const auto* v = doc_->project.variableById(sel)) {
                    const std::weak_ptr<int> alive = alive_;
                    app::itemseditor::ask({"\xC3\x89l\xC3\xA9ments de " + v->name, v->type, v->initial}, [this, alive, sel](const std::string& text) {
                        if (alive.expired()) return;
                        std::string why;
                        if (!setInitial(sel, text, &why)) say("Valeur initiale refus\xC3\xA9" "e : " + why, true);
                    });
                }
                break;
            case VDelete:
                if (!selectedFolder().empty()) (void)deleteFolder(selectedFolder());
                else if (sel && hosts_.removeVariable) hosts_.removeVariable(sel);
                else if (sel) (void)deleteVariable(sel);
                break;
            case VLink: {
                const hmi::Equipment* first = nullptr;
                for (const auto& e : doc_->project.equipments)
                    if (e.modbus()) { first = &e; break; }
                if (!first) {
                    say("Aucun \xC3\xA9quipement Modbus TCP/IP : ajoute-en un dans Configuration \xE2\x80\xBA \xC3\x89quipements", true);
                    break;
                }
                std::string why;
                if (!setEquipment(sel, first->name, &why)) say("Liaison refus\xC3\xA9" "e : " + why, true);
                break;
            }
            case VUnlink:
                (void)setEquipment(sel, {});
                break;
            case VShow:
                if (const auto* v = doc_->project.variableById(sel); v && hosts_.showEquipment) hosts_.showEquipment(v->name);
                break;
            case VTrend: (void)openTrend(); break;            // 1.10 (chantier O)
            case VRecalc: {                                    // 1.11.8
                std::string why;
                if (!recalculatePlace(sel, true, &why)) say("Refus\xC3\xA9 : " + why, true);
                break;
            }
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        if (!rows.empty() && rows.front() < rows_.size()) {
            const auto& r = rows_[rows.front()];
            selectedKey_ = (r.kind == Row::Kind::Folder ? "F:" : "V:") + upper(r.key);
        } else {
            selectedKey_.clear();
        }
        rebuildProperties();
    });
    // Glisser une variable sur un dossier la range (son nom ne change pas).
    // Lot 21 : plusieurs lignes (variables et dossiers) ; entre deux variables :
    // les reordonner. Un geste, une commande.
    table_->setRowDragEnabled(true);
    links_ += table_->rowsDropped->connect([this](const std::vector<ui::RowIndex>& from, ui::RowIndex to, ui::TreeView::DropWhere where) {
        dropRows(from, to, where);
    });
    links_ += table_->expanderClicked->connect([this](ui::RowIndex r) {
        if (r >= rows_.size()) return;
        const auto& row = rows_[r];
        setExpanded(row.key, row.expander != 1);
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (r >= rows_.size()) return;
        const auto& row = rows_[r];
        if (row.expander >= 0) setExpanded(row.key, row.expander != 1);
    });
    links_ += table_->cellEdited->connect([this](ui::RowIndex, std::size_t, const std::string&, bool ok) {
        if (!ok && !message_.empty()) status_->setMessage(message_, ui::StatusBar::Severity::Warning);
    });
    links_ += folderBox_->selectionChanged->connect([this](int) {
        if (syncing_) return;
        const auto* item = folderBox_->selectedItem();
        filterFolder_ = item ? item->value : std::string{};
        refresh();
    });
    links_ += search_->textChanged->connect([this](const std::string& text) {
        searchText_ = trimmed(text);
        query_ = ui::SearchQuery(searchText_);      // lot recherche
        table_->setHighlight(searchText_);
        refresh();
    });
    search_->setTooltip("Chaque mot dans le nom, le type, l'\xC3\xA9quipement, l'adresse, la description ou le dossier (ET), sans casse ni "
                        "accents ; \"une phrase\" ; -mot : l'exclure. Les entonnoirs des titres filtrent une colonne.");
    links_ += table_->columnFiltersChanged->connect([this] { refresh(); });
    links_ += expandAll_->stateChanged->connect([this](ui::Checkbox::State s) {
        expandAllOn_ = s == ui::Checkbox::State::Checked;
        refresh();
    });
    links_ += boundOnly_->stateChanged->connect([this](ui::Checkbox::State s) {
        boundOnlyOn_ = s == ui::Checkbox::State::Checked;
        refresh();
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
    // Lot API 8 : la recherche tapee, retenue d'une seance a l'autre (par projet) -
    // relue maintenant que son textChanged est branche ; les filtres des colonnes
    // de la table, eux, sont retenus par la table.
    searchMemory_.bindField(*search_);
}

void HmiVariablesPane::say(std::string text, bool warning) {
    message_ = std::move(text);
    status_->setMessage(message_, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
}

void HmiVariablesPane::apply(core::CommandPtr cmd) {
    if (cmd) apply_(std::move(cmd));
}

bool HmiVariablesPane::passes(const hmi::Variable& v, int skipColumn) const {
    if (boundOnlyOn_ && !v.bound()) return false;
    // Lot recherche : chaque mot (ou "phrase") dans le nom, le type, l'equipement,
    // l'adresse, la DESCRIPTION ou le dossier ; aucun -mot exclu. Puis les
    // filtres des colonnes.
    if (!query_.matches({v.name, v.type, v.equipment, v.address, v.description, v.folder})) return false;
    return ui::ColumnFilter::acceptsAll(table_->columnFilters(), [&](std::size_t c) { return cellOf(v, c); }, skipColumn);
}

std::string HmiVariablesPane::cellOf(const hmi::Variable& v, std::size_t column) const {
    // Comme emitVariable (sans la place ni la qualite : elles ne se filtrent pas).
    switch (column) {
        case CName: return v.name;
        case CType: return v.type;
        case CInitial: {
            if (const auto* e = hmi::findEnumeration(doc_->project, v.type)) return enumInitialText(*e, v.initial);   // 1.10 (chantier U)
            ty::Spec spec;
            if (ty::isComposite(v.type) && ty::parseSpec(v.type, spec) && !spec.array() && v.initial.empty()) return "(celles du type)";
            return v.initial;
        }
        case CEquipment: return v.bound() ? v.equipment : std::string("(locale)");
        case CAddress: return v.bound() ? (v.address.empty() ? std::string("(sans)") : v.address) : kDash;
        case CAccess: return v.bound() ? (v.readOnly ? kRO : kRW) : kDash;
        case CRetain: return retainCellOf(v);   // 1.11.16
        default: return {};
    }
}

void HmiVariablesPane::qualityOf(const hmi::Variable& owner, const std::string& path, bool leaf, std::string& text, int& tone) const {
    text = kDash;
    tone = 0;
    if (!owner.bound()) return;
    hmi::comm::Link* lk = hosts_.link ? hosts_.link(owner.equipment) : nullptr;
    if (!lk) {
        text = "(en marche)";
        return;
    }
    const auto rank = [](hmi::comm::Quality q) {
        switch (q) {
            case hmi::comm::Quality::Bad: return 4;
            case hmi::comm::Quality::Stale: return 3;
            case hmi::comm::Quality::Good: return 2;
            case hmi::comm::Quality::Pending: return 1;
            default: return 0;
        }
    };
    const auto describe = [&](hmi::comm::Quality q, const std::string& why) {
        tone = q == hmi::comm::Quality::Good ? 1 : q == hmi::comm::Quality::Stale ? 2 : q == hmi::comm::Quality::Bad ? 3 : 0;
        if (q == hmi::comm::Quality::Pending) return std::string("pas lue (aucune vue ne la montre)");
        if (q == hmi::comm::Quality::None) return std::string("en attente de la liaison");
        return std::string(hmi::comm::qualityName(q)) + (why.empty() ? std::string{} : " : " + why);
    };
    if (leaf) {
        std::string why;
        const auto q = lk->quality(path, &why);
        sim::Value raw;
        if ((q == hmi::comm::Quality::Good || q == hmi::comm::Quality::Stale) && lk->read(path, raw)) {
            hmi::Variable probe = owner;
            if (ty::isComposite(owner.type)) {
                probe.type = ty::typeOfPath(doc_->project, path);
                probe.rawMin = probe.rawMax = 0;
            }
            text = hmi::formatValue(eq::fromRegister(probe, raw));
            tone = q == hmi::comm::Quality::Good ? 1 : 2;
            return;
        }
        text = describe(q, why);
        return;
    }
    // Une structure, un tableau ou un membre compose : la pire des cases lues.
    hmi::comm::Quality worst = hmi::comm::Quality::None;
    std::string worstWhy;
    for (const auto& l : ty::leafVariables(doc_->project, owner)) {
        if (l.name.size() < path.size() || !sameText(std::string_view(l.name).substr(0, path.size()), path)) continue;
        std::string why;
        const auto q = lk->quality(l.name, &why);
        if (rank(q) > rank(worst)) {
            worst = q;
            worstWhy = why.empty() ? std::string{} : l.name + " : " + why;
        }
    }
    text = describe(worst, worst == hmi::comm::Quality::Good ? std::string{} : worstWhy);
}

void HmiVariablesPane::emitFolder(const std::string& folder, int depth) {
    const auto& p = doc_->project;
    for (const auto& f : folderChoices_) {
        if (!sameText(ty::folderParent(f), folder)) continue;
        // Le nombre de variables du dossier et de ses sous-dossiers (celles qui passent les filtres).
        std::size_t count = 0;
        for (const auto& v : p.programs.variables)
            if ((sameText(v.folder, f) || upper(v.folder).rfind(upper(f) + "/", 0) == 0) && passes(v)) ++count;
        // Lot recherche : un filtre de colonne cache aussi les dossiers vides, et ouvre les autres.
        const bool filtered = !table_->columnFilters().empty();
        if ((!searchText_.empty() || boundOnlyOn_ || filtered) && count == 0) continue;
        Row r;
        r.kind = Row::Kind::Folder;
        r.key = f;
        r.depth = depth;
        const bool isOpen = !collapsed_.count(upper(f)) || !searchText_.empty() || filtered;
        r.expander = isOpen ? 1 : 0;
        r.cells = {ty::folderLeaf(f), std::to_string(count) + " variable" + (count > 1 ? "s" : ""), "", "", "", "", "", "", ""};
        rows_.push_back(std::move(r));
        if (isOpen) emitFolder(f, depth + 1);
    }
    for (const auto& v : p.programs.variables)
        if (sameText(v.folder, folder) && passes(v)) emitVariable(v, depth);
}

void HmiVariablesPane::emitVariable(const hmi::Variable& v, int depth) {
    const auto& p = doc_->project;
    Row r;
    r.kind = Row::Kind::Variable;
    r.key = v.name;
    r.var = v.id;
    r.type = v.type;
    r.depth = depth;
    const auto* enumType = hmi::findEnumeration(p, v.type);       // 1.10 (chantier U) : une enumeration est un DINT
    const bool composite = ty::isComposite(v.type) && !enumType;
    const bool isOpen = composite && (expandAllOn_ || open_.count(upper(v.name)));
    r.expander = composite ? (isOpen ? 1 : 0) : -1;
    ty::Spec spec;
    const bool parsed = ty::parseSpec(v.type, spec);
    std::string initial = enumType ? enumInitialText(*enumType, v.initial) : v.initial;
    if (composite && parsed && !spec.array() && initial.empty()) initial = "(celles du type)";
    std::string place = kDash;
    if (v.bound()) place = composite ? ty::spanText(p, v) : placeOf(v.address, v.scaled() ? (v.rawType.empty() ? std::string("INT") : v.rawType) : v.type);
    else if (composite && parsed) place = spec.array() ? std::to_string(spec.length()) + " cases" : std::to_string(ty::weightOf(p, v.type, v.packBools).words) + " mots (non li\xC3\xA9" "e)";
    if (v.bound() && place.empty()) place = "adresse illisible";
    std::string quality;
    qualityOf(v, v.name, !composite, quality, r.tone);
    if (!composite && v.bound() && r.tone != 3 && quality != kDash && quality != "(en marche)") {
        // Une variable simple : sa qualite, sa valeur entre parentheses.
        if (const auto* lk = hosts_.link ? hosts_.link(v.equipment) : nullptr) {
            std::string why;
            const auto q = lk->quality(v.name, &why);
            quality = std::string(hmi::comm::qualityName(q)) + (q == hmi::comm::Quality::Good || q == hmi::comm::Quality::Stale ? " \xC2\xB7 " + quality : std::string{});
            if (q == hmi::comm::Quality::Pending) quality = "pas lue (aucune vue ne la montre)";
            if (q == hmi::comm::Quality::None) quality = "en attente de la liaison";
        }
    }
    r.cells = {v.name, v.type, initial, retainCellOf(v), v.bound() ? v.equipment : std::string("(locale)"),
               v.bound() ? (v.address.empty() ? std::string("(sans)") : v.address) : kDash, v.bound() ? (v.readOnly ? kRO : kRW) : kDash, place, quality};
    rows_.push_back(std::move(r));
    if (!isOpen) return;
    Leaves leaves;
    for (auto& l : ty::leafVariables(p, v)) {
        std::string key = upper(l.name);
        leaves.emplace(std::move(key), std::move(l));
    }
    emitMembers(v, leaves, v.name, {}, ty::normalized(v.type), depth + 1);
}

void HmiVariablesPane::emitMembers(const hmi::Variable& v, const Leaves& leaves, const std::string& path, const std::string& rel,
                                   const std::string& type, int depth) {
    const auto& p = doc_->project;
    ty::Spec s;
    if (!ty::parseSpec(type, s)) return;
    struct Child {
        std::string path, rel, type, description;
    };
    std::vector<Child> kids;
    long long hidden = 0;
    if (s.array()) {
        long long count = 0;
        const auto push = [&](const std::string& idx) {
            if (++count > kShown) {
                ++hidden;
                return;
            }
            kids.push_back({path + idx, rel + idx, s.element, {}});
        };
        if (s.dims == 1)
            for (long long i = s.low[0]; i <= s.high[0]; ++i) push("[" + std::to_string(i) + "]");
        else
            for (long long i = s.low[0]; i <= s.high[0]; ++i)
                for (long long j = s.low[1]; j <= s.high[1]; ++j) push("[" + std::to_string(i) + "," + std::to_string(j) + "]");
    } else if (!ty::isElementary(s.element)) {
        for (const auto& m : ty::membersOf(p, s.element))
            kids.push_back({path + "." + m.name, rel.empty() ? m.name : rel + "." + m.name, ty::normalized(m.type), m.description});
    }
    for (const auto& k : kids) {
        Row r;
        r.kind = Row::Kind::Member;
        r.key = k.path;
        r.var = v.id;
        r.rel = k.rel;
        r.type = k.type;
        r.depth = depth;
        const bool composite = ty::isComposite(k.type);
        const bool isOpen = composite && (expandAllOn_ || open_.count(upper(k.path)));
        r.expander = composite ? (isOpen ? 1 : 0) : -1;
        std::string initial, address, place;
        const hmi::Variable* leaf = nullptr;
        if (const auto it = leaves.find(upper(k.path)); it != leaves.end()) leaf = &it->second;
        // 1.11.8 : interne (gardee dans l'IHM) - une case, ou toutes les cases d'un membre compose.
        std::size_t inside = 0, internals = 0;
        if (v.bound()) {
            const std::string prefix = upper(k.path);
            for (auto it = leaves.lower_bound(prefix); it != leaves.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
                if (it->first.size() != prefix.size() && it->first[prefix.size()] != '.' && it->first[prefix.size()] != '[') continue;
                ++inside;
                internals += it->second.bound() ? 0 : 1;
            }
        }
        const bool internal = v.bound() && inside > 0 && internals == inside;
        if (leaf) {
            initial = leaf->initial;
            address = leaf->address;
            if (v.bound()) place = internal ? std::string("dans l'IHM") : placeOf(leaf->address, leaf->type);
            // Une adresse corrigee a la main : le crayon.
            for (const auto& pl : v.places)
                if (sameText(pl.path, k.rel) && !pl.address.empty()) address += " \xE2\x9C\x8E";
        } else if (composite && v.bound()) {
            // Un membre compose : de sa premiere case a sa derniere (dans l'ordre du type).
            std::vector<hmi::Variable> part;
            const std::string prefix = upper(k.path);
            for (auto it = leaves.lower_bound(prefix); it != leaves.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
                if (it->first.size() > prefix.size() && (it->first[prefix.size()] == '.' || it->first[prefix.size()] == '[')) part.push_back(it->second);
            place = internal ? std::string("dans l'IHM") : ty::spanTextOf(part, v.address);
            // Le depart : la case de plus petite adresse.
            long long best = -1;
            for (const auto& lf : part) {
                hmi::comm::Point pt;
                if (lf.address.empty() || !eq::placeEquipmentAddress(lf.address, eq::registerType(lf), pt)) continue;
                const long long key = static_cast<long long>(pt.offset) * 32 + (pt.encoding == hmi::comm::Encoding::BitOfWord ? pt.bit : 0);
                if (best < 0 || key < best) {
                    best = key;
                    address = lf.address;
                }
            }
            // 1.11.8 : le depart donne a la main (Vannes[2] -> %MW500) : le crayon.
            for (const auto& pl : v.places)
                if (sameText(pl.path, k.rel) && !pl.address.empty() && !address.empty()) address += " \xE2\x9C\x8E";
        }
        if (composite && !v.bound()) {
            ty::Spec ks;
            if (ty::parseSpec(k.type, ks)) place = ks.array() ? std::to_string(ks.length()) + " cases" : std::to_string(ty::weightOf(p, k.type, v.packBools).words) + " mots";
        }
        std::string quality;
        qualityOf(v, k.path, !composite, quality, r.tone);
        // 1.11.8 : l'adresse d'un membre compose (son depart) se change aussi ; pas celle d'un membre interne.
        r.editableAddress = v.bound() && !internal;
        std::string equipmentCell = v.bound() ? "\xE2\x86\xB3 " + v.name : std::string{};
        if (internal) equipmentCell = "interne (IHM)";
        else if (internals > 0) equipmentCell += " \xC2\xB7 " + std::to_string(internals) + " interne" + (internals > 1 ? "s" : "");
        if (internal) quality.clear();
        r.cells = {k.path, k.type, initial, std::string{}, equipmentCell, v.bound() && !internal ? address : std::string{},
                   !v.bound() ? std::string{} : internal ? std::string("IHM") : "(" + v.name + ")",
                   place.empty() && v.bound() ? std::string("sans place") : place, quality};
        rows_.push_back(std::move(r));
        if (isOpen) emitMembers(v, leaves, k.path, k.rel, k.type, depth + 1);
    }
    if (hidden > 0) {
        Row more;
        more.kind = Row::Kind::More;
        more.key = path + "#plus";
        more.var = v.id;
        more.depth = depth;
        more.cells = {"\xE2\x80\xA6 " + std::to_string(hidden) + " autres cases (d\xC3\xA9pli\xC3\xA9" "es : les " + std::to_string(kShown) + " premi\xC3\xA8res)",
                      "", "", "", "", "", "", "", ""};
        rows_.push_back(std::move(more));
    }
}

void HmiVariablesPane::refresh() {
    const auto& p = doc_->project;
    rows_.clear();
    folderChoices_ = ty::allFolders(p);
    // Le filtre des dossiers : tous, la racine, puis chaque dossier (en retrait).
    {
        std::vector<ui::DropDown::Item> items{{"Tous les dossiers", "", {}, true}, {"(racine)", "/", {}, true}};
        int selected = 0;
        for (const auto& f : folderChoices_) {
            const auto depth = std::count(f.begin(), f.end(), '/');
            items.push_back({std::string(static_cast<std::size_t>(depth) * 3, ' ') + ty::folderLeaf(f), f, {}, true});
            if (sameText(f, filterFolder_)) selected = static_cast<int>(items.size()) - 1;
        }
        if (filterFolder_ == "/") selected = 1;
        if (selected == 0 && !filterFolder_.empty() && filterFolder_ != "/") filterFolder_.clear();
        syncing_ = true;
        folderBox_->setItems(std::move(items));
        folderBox_->setSelectedIndex(selected);
        syncing_ = false;
    }
    if (filterFolder_.empty()) {
        emitFolder({}, 0);
    } else if (filterFolder_ == "/") {
        for (const auto& v : p.programs.variables)
            if (v.folder.empty() && passes(v)) emitVariable(v, 0);
    } else {
        Row r;
        r.kind = Row::Kind::Folder;
        r.key = filterFolder_;
        r.expander = 1;
        std::size_t count = 0;
        for (const auto& v : p.programs.variables)
            if ((sameText(v.folder, filterFolder_) || upper(v.folder).rfind(upper(filterFolder_) + "/", 0) == 0) && passes(v)) ++count;
        r.cells = {filterFolder_, std::to_string(count) + " variable" + (count > 1 ? "s" : ""), "", "", "", "", "", "", ""};
        rows_.push_back(std::move(r));
        emitFolder(filterFolder_, 1);
    }
    // Lot recherche : "37 sur 251" - les variables que gardent les filtres.
    {
        std::size_t kept = 0;
        for (const auto& v : p.programs.variables) kept += passes(v) ? 1u : 0u;
        table_->setColumnFilterCounts(kept, p.programs.variables.size());
    }
    model_ = std::make_shared<VarModel>(
        rows_, &doc_->project, [this](std::size_t r, std::size_t c) { return cellEditable(r, c); },
        [this](std::size_t r, std::size_t c) { return choicesFor(r, c); },
        [this](std::size_t r, std::size_t c, const std::string& text) { return commitCell(r, c, text); });
    syncing_ = true;
    table_->setModel(model_);
    int keep = -1;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if ((rows_[i].kind == Row::Kind::Folder ? "F:" : "V:") + upper(rows_[i].key) == selectedKey_) keep = static_cast<int>(i);
    if (keep >= 0) table_->selectModelRows({static_cast<ui::RowIndex>(keep)}, false);
    syncing_ = false;
    rebuildProperties();
    std::size_t bound = 0, composite = 0;
    for (const auto& v : p.programs.variables) {
        bound += v.bound() ? 1 : 0;
        composite += ty::isComposite(v.type) ? 1 : 0;
    }
    if (message_.empty())
        status_->setMessage(std::to_string(p.programs.variables.size()) + " variable(s) IHM dans " + std::to_string(folderChoices_.size())
                                + " dossier(s) \xC2\xB7 " + std::to_string(composite) + " structure(s) ou tableau(x) \xC2\xB7 " + std::to_string(bound)
                                + " li\xC3\xA9" "e(s) \xC3\xA0 un \xC3\xA9quipement (une modification ici se voit aussi dans \xC3\x89quipements \xE2\x80\xBA Plan d'adressage)");
    invalidate();
}

bool HmiVariablesPane::cellEditable(std::size_t row, std::size_t col) const {
    if (row >= rows_.size()) return false;
    const Row& r = rows_[row];
    switch (r.kind) {
        case Row::Kind::Folder: return col == CName && (filterFolder_.empty() || r.depth > 0);
        case Row::Kind::Variable: {
            const auto* v = doc_->project.variableById(r.var);
            if (!v) return false;
            // 1.12.2 : un type objet (LIST, VECTOR, MAP, TUPLE) vit dans l'IHM : ni equipement, ni adresse,
            // ni remanence ; sa valeur initiale est un litteral.
            const bool rich = ty::isRich(v->type);
            if (col == CName || col == CType) return true;
            if (col == CEquipment) return !rich;
            if (col == CInitial) {
                if (rich) return true;
                if (hmi::findEnumeration(doc_->project, v->type)) return true;   // 1.10 (chantier U) : la liste des valeurs
                ty::Spec s;
                return !ty::isComposite(v->type) || (ty::parseSpec(v->type, s) && s.array() && ty::isElementary(s.element));
            }
            if (col == CAddress || col == CAccess) return v->bound() && !rich;
            if (col == CRetain) return !v->bound() && !rich;   // 1.11.16 : une variable de l'IHM
            return false;
        }
        case Row::Kind::Member: return col == CAddress && r.editableAddress;
        case Row::Kind::More: return false;
    }
    return false;
}

std::vector<std::string> HmiVariablesPane::choicesFor(std::size_t row, std::size_t col) const {
    if (row >= rows_.size() || rows_[row].kind != Row::Kind::Variable) return {};
    const auto& p = doc_->project;
    if (col == CType) return typeChoices(p);
    if (col == CEquipment) {
        std::vector<std::string> out{""};
        for (const auto& e : p.equipments)
            if (e.modbus()) out.push_back(e.name);
        return out;
    }
    if (col == CAccess) return {kRW, kRO};
    if (col == CRetain) return {"oui", "non"};   // 1.11.16
    if (col == CInitial)   // 1.10 (chantier U) : une enumeration, ses valeurs << Auto (1) >>
        if (const auto* v = p.variableById(rows_[row].var))
            if (const auto* e = hmi::findEnumeration(p, v->type)) {
                std::vector<std::string> out;
                for (const auto& x : e->values) out.push_back(hmi::enumDisplay(*e, x.value));
                return out;
            }
    return {};
}

bool HmiVariablesPane::commitCell(std::size_t row, std::size_t col, const std::string& text) {
    if (row >= rows_.size()) return false;
    const Row r = rows_[row];
    std::string why;
    bool ok = false;
    // Lot API 8 : la variable qui vient d'etre creee (+ Variable) prend son premier nom sur place.
    const bool fresh = r.kind == Row::Kind::Variable && r.var == naming_;
    naming_ = hmi::kNoId;
    switch (r.kind) {
        case Row::Kind::Folder:
            ok = col == CName && renameFolder(r.key, text, &why);
            break;
        case Row::Kind::Variable:
            switch (col) {
                case CName:
                    // Lot API 8 : un nom tape ouvre le dialogue qui montre ce qui suit (le
                    // nouveau nom deja ecrit ; la case garde l'ancien, le dialogue renomme).
                    if (!fresh && renameInDialog(r.var, text)) {
                        message_.clear();
                        return false;
                    }
                    ok = setName(r.var, text, &why);
                    break;
                case CType:
                    if (text == kPickType) {
                        // 1.11.19 (lot 6) : le selecteur de types (les types d'une variable IHM).
                        const auto* v = doc_->project.variableById(r.var);
                        app::HmiTypePicker::Spec spec;
                        spec.field = "Type de " + (v ? v->name : std::string{});
                        spec.current = v ? v->type : std::string{};
                        spec.use = hmi::typereg::UseVariable;
                        spec.doc = doc_;
                        const Id id = r.var;
                        const std::weak_ptr<int> alive = alive_;
                        app::typepicker::ask(std::move(spec), [this, id, alive](const app::HmiTypePicker::Answer& a) {
                            if (alive.expired() || a.type.empty()) return;
                            std::string w;
                            if (!setType(id, a.type, &w)) say("Type refus\xC3\xA9 : " + w, true);
                        });
                        return false;
                    }
                    if (const std::string shape = shapeOfChoice(text); !shape.empty()) {
                        // 1.12.2 : Liste..., Vecteur..., Dictionnaire (MAP)..., Tuple... : la forme et le type
                        // des elements dans une fenetre ; sans hote, la forme sur le type actuel.
                        const auto* v = doc_->project.variableById(r.var);
                        const std::string current = v ? v->type : std::string("INT");
                        const Id id = r.var;
                        if (hosts_.shapedType) {
                            hosts_.shapedType(current, shape, [this, id](const std::string& type) {
                                std::string w;
                                if (!setType(id, type, &w)) say("Type refus\xC3\xA9 : " + w, true);
                            });
                            return true;
                        }
                        auto sh = hmi::typeform::decompose(current);
                        sh.form = hmi::typeform::fromText(shape).value_or(hmi::typeform::Form::List);
                        sh.parameter.clear();
                        sh.reference = false;
                        ok = setType(r.var, hmi::typeform::compose(sh), &why);
                    } else if (text == kArray) {
                        // Tableau... : les bornes et le type des cases, dans une fenetre.
                        const auto* v = doc_->project.variableById(r.var);
                        const std::string current = v ? v->type : std::string("INT");
                        const Id id = r.var;
                        if (hosts_.arrayType) {
                            hosts_.arrayType(current, [this, id](const std::string& type) {
                                std::string w;
                                if (!setType(id, type, &w)) say("Type refus\xC3\xA9 : " + w, true);
                            });
                            return true;
                        }
                        ty::Spec s;
                        const std::string element = ty::parseSpec(current, s) ? s.element : std::string("INT");
                        ok = setType(r.var, "ARRAY[0..9] OF " + element, &why);
                    } else {
                        ok = setType(r.var, text, &why);
                    }
                    break;
                case CInitial: ok = setInitial(r.var, text, &why); break;
                case CEquipment: ok = setEquipment(r.var, text, &why); break;
                case CAddress: ok = setAddress(r.var, text, &why); break;
                case CAccess: ok = setReadOnly(r.var, text == kRO, &why); break;
                case CRetain: {   // 1.11.16
                    bool on = false;
                    ok = yesNo(text, on) ? setRetain(r.var, on, &why) : fail(&why, "R\xC3\xA9manente : oui ou non");
                    break;
                }
                default: break;
            }
            break;
        case Row::Kind::Member:
            if (col == CAddress) {
                std::string address = text;
                if (const auto pen = address.find(" \xE2\x9C\x8E"); pen != std::string::npos) address.erase(pen);
                ok = setMemberAddress(r.var, r.rel, trimmed(address), &why);
            }
            break;
        case Row::Kind::More: break;
    }
    if (!ok && !why.empty()) say("Refus\xC3\xA9 : " + why, true);
    return ok;
}

std::string HmiVariablesPane::newFolderParent() const {
    if (const std::string f = selectedFolder(); !f.empty()) return f;
    if (const auto* v = doc_->project.variableById(selectedVariable())) return v->folder;
    return {};
}

// ---- la selection ------------------------------------------------------------------
Id HmiVariablesPane::selectedVariable() const {
    const auto sel = table_->selectedModelRows();
    if (sel.empty() || sel.front() >= rows_.size()) return kNoId;
    const auto& r = rows_[sel.front()];
    return r.kind == Row::Kind::Folder ? kNoId : r.var;
}

std::string HmiVariablesPane::selectedFolder() const {
    const auto sel = table_->selectedModelRows();
    if (sel.empty() || sel.front() >= rows_.size()) return {};
    const auto& r = rows_[sel.front()];
    return r.kind == Row::Kind::Folder ? r.key : std::string{};
}

std::string HmiVariablesPane::selectedPath() const {
    const auto sel = table_->selectedModelRows();
    if (sel.empty() || sel.front() >= rows_.size()) return {};
    const auto& r = rows_[sel.front()];
    return r.kind == Row::Kind::Member ? r.key : std::string{};
}

int HmiVariablesPane::rowOf(const std::string& key) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (sameText(rows_[i].key, key)) return static_cast<int>(i);
    return -1;
}

void HmiVariablesPane::selectVariable(Id id) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return;
    // Le dossier (et ses parents) deplie, le filtre retire s'il la cache.
    for (const auto& f : ty::folderChain(v->folder)) collapsed_.erase(upper(f));
    if (!filterFolder_.empty() && !(filterFolder_ == "/" ? v->folder.empty() : sameText(v->folder, filterFolder_)
                                                                                 || upper(v->folder).rfind(upper(filterFolder_) + "/", 0) == 0))
        filterFolder_.clear();
    // Lot recherche : la recherche, "seulement les liees" ou un filtre de colonne
    // qui la cache s'effacent (Aller a... y mene).
    if (!passes(*v)) {
        if (!searchText_.empty()) {
            search_->setText("");
            searchText_.clear();
            query_ = ui::SearchQuery{};
            table_->setHighlight({});
        }
        if (boundOnlyOn_ && !v->bound()) setBoundOnly(false);
        if (!passes(*v)) table_->clearColumnFilters();
    }
    selectedKey_ = "V:" + upper(v->name);
    refresh();
    if (const int r = rowOf(v->name); r >= 0) {
        hmiSelectModelRow(*table_, static_cast<std::size_t>(r));
        rebuildProperties();
    }
}

void HmiVariablesPane::selectFolder(const std::string& path) {
    for (const auto& f : ty::folderChain(ty::folderParent(path))) collapsed_.erase(upper(f));
    selectedKey_ = "F:" + upper(path);
    refresh();
    if (const int r = rowOf(path); r >= 0) {
        hmiSelectModelRow(*table_, static_cast<std::size_t>(r));
        rebuildProperties();
    }
}

void HmiVariablesPane::selectPath(const std::string& path) {
    const auto* v = doc_->project.variable(std::string_view(path).substr(0, path.find_first_of(".[")));
    if (!v) return;
    // Les parents du membre, deplies.
    open_.insert(upper(v->name));
    for (std::size_t i = 0; i < path.size(); ++i)
        if ((path[i] == '.' || path[i] == '[') && i > 0) open_.insert(upper(path.substr(0, i)));
    selectedKey_ = "V:" + upper(path);
    selectVariable(v->id);
    if (const int r = rowOf(path); r >= 0) {
        selectedKey_ = "V:" + upper(path);
        hmiSelectModelRow(*table_, static_cast<std::size_t>(r));
        rebuildProperties();
    }
}

void HmiVariablesPane::setExpanded(const std::string& path, bool on) {
    const std::string key = upper(path);
    bool folder = false;
    for (const auto& f : folderChoices_)
        if (upper(f) == key) folder = true;
    if (folder) {
        if (on) collapsed_.erase(key);
        else collapsed_.insert(key);
    } else {
        if (on) open_.insert(key);
        else open_.erase(key);
    }
    refresh();
}

bool HmiVariablesPane::expanded(const std::string& path) const {
    const int r = rowOf(path);
    return r >= 0 && rows_[static_cast<std::size_t>(r)].expander == 1;
}

void HmiVariablesPane::setFolderFilter(const std::string& folder) {
    filterFolder_ = folder;
    refresh();
}

void HmiVariablesPane::setSearch(const std::string& text) {
    search_->setText(text);
    searchText_ = trimmed(text);
    query_ = ui::SearchQuery(searchText_);          // lot recherche
    table_->setHighlight(searchText_);
    refresh();
}

void HmiVariablesPane::setExpandAll(bool on) {
    expandAll_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    expandAllOn_ = on;
    refresh();
}

void HmiVariablesPane::setBoundOnly(bool on) {
    boundOnly_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    boundOnlyOn_ = on;
    refresh();
}

// ---- les actions -----------------------------------------------------------------
bool HmiVariablesPane::change(Id id, std::string label, const std::function<void(hmi::Variable&)>& edit) {
    bool found = false;
    auto cmd = hmi::changeProject(doc_, std::move(label), [&](hmi::Project& p) {
        for (auto& v : p.programs.variables)
            if (v.id == id) {
                edit(v);
                found = true;
            }
    });
    apply(std::move(cmd));
    return found;
}

Id HmiVariablesPane::addVariable(const std::string& rawName, const std::string& rawType, const std::string& folder, std::string* why) {
    const std::string name = trimmed(rawName), type = ty::normalized(rawType);
    std::string reason;
    if (!hmi::isIdentifier(name)) reason = "nom invalide : lettres, chiffres et _";
    else if (doc_->project.variable(name)) reason = "'" + name + "' existe d\xC3\xA9j\xC3\xA0 (deux variables IHM ne peuvent pas porter le m\xC3\xAAme nom, casse comprise)";
    else if (!ty::validType(doc_->project, type, &reason)) {}
    else if (!folder.empty() && !ty::validFolder(folder, &reason)) {}
    if (!reason.empty()) {
        say("Variable refus\xC3\xA9" "e : " + reason, true);
        if (why) *why = reason;
        return kNoId;
    }
    Id made = kNoId;
    apply(hmi::changeProject(doc_, "Nouvelle variable " + name, [&](hmi::Project& p) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = type;
        v.initial = ty::isComposite(type) ? std::string{} : (type == "BOOL" ? "FALSE" : type == "STRING" ? "''" : "0");
        v.folder = folder;
        made = v.id;
        p.programs.variables.push_back(std::move(v));
    }));
    if (made == kNoId) return kNoId;
    selectVariable(made);
    say("Variable IHM cr\xC3\xA9\xC3\xA9" "e : " + name + " (" + type + ")" + (folder.empty() ? std::string{} : ", dans " + folder));
    return made;
}

bool HmiVariablesPane::setName(Id id, const std::string& raw, std::string* why) {
    const std::string name = trimmed(raw);
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (!hmi::isIdentifier(name)) return fail(why, "nom invalide : lettres, chiffres et _");
    if (const auto* other = doc_->project.variable(name); other && other->id != id)
        return fail(why, "'" + name + "' existe d\xC3\xA9j\xC3\xA0 (deux variables IHM ne peuvent pas porter le m\xC3\xAAme nom, casse comprise)");
    if (v->name == name) return true;
    const std::string before = v->name;
    change(id, "Renommer " + before + " en " + name, [&](hmi::Variable& x) { x.name = name; });
    selectedKey_ = "V:" + upper(name);
    refresh();
    say(before + " s'appelle maintenant " + name + " (les scripts et les vues qui l'\xC3\xA9" "crivent le signalent \xC3\xA0 Compiler)");
    return true;
}

// ---- Lot API 8 : renommer en voyant ce qui suit (RenameDialog, genre ihm-variable) ----
bool HmiVariablesPane::renameInDialog(Id id, const std::string& typed) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return false;
    const std::string wanted = trimmed(typed);
    if (!wanted.empty() && wanted == v->name) return false;          // rien ne change : sur place (rien a faire)
    return requestRename("ihm-variable", v->name, wanted);
}

bool HmiVariablesPane::renameSelectedInDialog() {
    // La ligne choisie (la derniere d'une selection de plusieurs) : une variable.
    const auto& sel = table_->selection();
    if (sel.empty() || sel.back() >= rows_.size() || rows_[sel.back()].kind != Row::Kind::Variable) return false;
    return renameInDialog(rows_[sel.back()].var);
}
// ---- fin Lot API 8 ----

bool HmiVariablesPane::setType(Id id, const std::string& raw, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    const std::string type = ty::normalized(raw);
    if (!ty::validType(doc_->project, type, why)) return false;
    if (const auto* e = hmi::findEnumeration(doc_->project, type)) {   // 1.10 (chantier U) : une enumeration, un DINT
        if (sameText(v->type, type)) return true;
        const std::string first = e->values.empty() ? std::string{} : e->values.front().name;
        const std::string name = e->name;
        change(id, "Type de " + v->name + " : " + name, [&](hmi::Variable& x) {
            x.type = name;
            x.initial = first;
            if (x.scaled()) x.rawMin = x.rawMax = x.engMin = x.engMax = 0;
        });
        say(v->name + " : " + name + " (\xC3\xA9num\xC3\xA9ration, un DINT) \xC2\xB7 valeur initiale " + (first.empty() ? std::string("vide") : first));
        return true;
    }
    // 1.12.2 : UN TYPE OBJET (LIST, VECTOR, MAP, TUPLE) vit dans la memoire de l'IHM : la variable
    // est deliee de son equipement (sa place n'a plus de sens) et n'est plus remanente ; sa valeur
    // initiale, si elle ne va plus, est videe.
    if (ty::isRich(type)) {
        if (v->type == type) return true;
        const bool wasBound = v->bound();
        const std::string was = v->equipment + (v->address.empty() ? std::string{} : " (" + v->address + ")");
        const bool wasRetained = v->retain;
        const std::string name = v->name;
        change(id, "Type de " + v->name + " : " + type, [&](hmi::Variable& x) {
            x.type = type;
            if (!validInitial(doc_->project, type, x.initial, nullptr)) x.initial.clear();
            x.equipment.clear();
            x.address.clear();
            x.readOnly = false;
            x.rawMin = x.rawMax = x.engMin = x.engMax = 0;
            x.rawType.clear();
            x.places.clear();
            x.internal.clear();
            x.compact = false;
            x.retain = false;
        });
        std::string text = name + " : " + type + " \xC2\xB7 " + std::string(hmi::typeform::summary(hmi::typeform::decompose(type).form));
        if (wasBound) text += " \xC2\xB7 d\xC3\xA9li\xC3\xA9" "e de " + was + " : " + ty::unbindableReason(type);
        if (wasRetained) text += " \xC2\xB7 R\xC3\xA9manente retir\xC3\xA9" "e";
        say(text, wasBound);
        return true;
    }
    if (ty::isComposite(type)) {
        const auto f = ty::flatten(doc_->project, v->name, type, {}, v->packBools);
        if (!f.ok()) return fail(why, f.error);
    }
    if (v->type == type) return true;
    const bool wasComposite = ty::isComposite(v->type);
    change(id, "Type de " + v->name + " : " + type, [&](hmi::Variable& x) {
        x.type = type;
        // Une valeur initiale qui ne va plus : celle du type.
        if (!validInitial(doc_->project, type, x.initial, nullptr) || (ty::isComposite(type) != wasComposite))
            x.initial = ty::isComposite(type) ? std::string{} : (type == "BOOL" ? "FALSE" : type == "STRING" ? "''" : "0");
        if (ty::isComposite(type) && x.scaled()) x.rawMin = x.rawMax = x.engMin = x.engMax = 0;
    });
    say(v->name + " : " + ty::summary(doc_->project, type, v->packBools));
    return true;
}

bool HmiVariablesPane::setInitial(Id id, const std::string& raw, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    std::string initial = trimmed(raw);
    if (!validInitial(doc_->project, v->type, initial, why)) return false;
    // 1.10 (chantier U) : une enumeration garde le nom de la valeur (<< Auto (1) >>, T_MODE#Auto, 1 -> Auto).
    if (const auto* e = hmi::findEnumeration(doc_->project, v->type); e && !initial.empty()) {
        std::int64_t n = 0;
        if (hmi::enumNumberOf(*e, initial, n))
            if (const auto* x = hmi::enumValueByNumber(*e, n)) initial = x->name;
    }
    if (v->initial == initial) return true;
    change(id, "Valeur initiale de " + v->name, [&](hmi::Variable& x) { x.initial = initial; });
    return true;
}

bool HmiVariablesPane::setDescription(Id id, const std::string& text) {
    const auto* v = doc_->project.variableById(id);
    if (!v || v->description == text) return v != nullptr;
    return change(id, "Description de " + v->name, [&](hmi::Variable& x) { x.description = text; });
}

bool HmiVariablesPane::setFolder(Id id, const std::string& raw, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    std::string folder = trimmed(raw);
    if (folder == "(racine)" || folder == "/") folder.clear();
    // Le dossier ecrit comme il existe deja (la casse d'un dossier ne se multiplie pas).
    for (const auto& f : ty::allFolders(doc_->project))
        if (sameText(f, folder)) folder = f;
    if (!folder.empty() && !ty::validFolder(folder, why)) return false;
    if (v->folder == folder) return true;
    const std::string name = v->name;
    apply(hmi::changeProject(doc_, "Ranger " + name + (folder.empty() ? " \xC3\xA0 la racine" : " dans " + folder), [&](hmi::Project& p) {
        for (auto& x : p.programs.variables)
            if (x.id == id) x.folder = folder;
        if (!folder.empty() && std::none_of(p.programs.folders.begin(), p.programs.folders.end(), [&](const std::string& f) { return sameText(f, folder); }))
            p.programs.folders.push_back(folder);
    }));
    selectVariable(id);
    say(name + (folder.empty() ? " est rang\xC3\xA9" "e \xC3\xA0 la racine" : " est rang\xC3\xA9" "e dans " + folder) + " (son nom ne change pas)");
    return true;
}

bool HmiVariablesPane::setEquipment(Id id, const std::string& raw, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    const std::string name = trimmed(raw);
    if (name.empty() || name == "(aucun)" || name == "(locale)") {
        if (!v->bound()) return true;
        const std::string was = v->equipment;
        change(id, "D\xC3\xA9lier " + v->name, [](hmi::Variable& x) {
            x.equipment.clear();
            x.address.clear();
            x.readOnly = false;
            x.rawMin = x.rawMax = x.engMin = x.engMax = 0;
            x.rawType.clear();
            x.places.clear();
        });
        say(v->name + " n'est plus li\xC3\xA9" "e \xC3\xA0 " + was + " : elle reste dans l'IHM");
        return true;
    }
    // 1.12.2 : un type objet (taille variable, ou sans plan memoire) ne tient pas dans un equipement.
    if (const std::string reason = ty::unbindableReason(v->type); !reason.empty()) return fail(why, v->name + " : " + reason);
    const auto* e = doc_->project.equipmentByName(name);
    if (!e) return fail(why, "\xC3\xA9quipement inconnu : " + name);
    if (!e->modbus()) return fail(why, e->name + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas de variables");
    if (sameText(v->equipment, e->name)) return true;
    std::string address = v->address;
    if (address.empty() || !v->bound()) {
        ty::Spec s;
        const bool bits = ty::parseSpec(v->type, s) && s.element == "BOOL" && !ty::isComposite(v->type);
        address = eq::nextFreeAddress(doc_->project, *e, bits ? sim::Type::Bool : ty::isComposite(v->type) ? sim::Type::Int : eq::typeOfName(v->type));
    }
    const std::string equipmentName = e->name;
    const bool wasRetained = v->retain;
    const std::string variableName = v->name;
    change(id, "Lier " + v->name + " \xC3\xA0 " + equipmentName, [&](hmi::Variable& x) {
        x.equipment = equipmentName;
        x.address = address;
        x.retain = false;   // 1.12.2 : l'equipement garde sa valeur (la case restait a « oui, sans effet »)
    });
    selectVariable(id);
    say(variableName + " est li\xC3\xA9" "e \xC3\xA0 " + equipmentName + " (" + address + ") : la m\xC3\xAAme ligne est dans \xC3\x89quipements \xE2\x80\xBA Plan d'adressage"
        + (wasRetained ? std::string(" \xC2\xB7 R\xC3\xA9manente retir\xC3\xA9" "e (l'\xC3\xA9quipement garde sa valeur)") : std::string{}));
    return true;
}

bool HmiVariablesPane::setAddress(Id id, const std::string& raw, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (const std::string reason = ty::unbindableReason(v->type); !reason.empty()) return fail(why, v->name + " : " + reason);   // 1.12.2
    if (!v->bound()) return fail(why, v->name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement (colonne \xC3\x89quipement)");
    const std::string address = trimmed(raw);
    if (address.empty()) return fail(why, "adresse vide (D\xC3\xA9lier pour la retirer de l'\xC3\xA9quipement)");
    if (ty::isComposite(v->type)) {
        // Le depart d'une structure : un mot (ou un bit si toutes ses cases sont des BOOL).
        if (ty::memberAddress(address, 0, -1, "INT", why).empty()) {
            if (!ty::bitArea(address)) return false;
            for (const auto& l : ty::leafVariables(doc_->project, *v))
                if (l.type != "BOOL") return fail(why, "le d\xC3\xA9part " + address + " est un bit : " + l.name + " (" + l.type + ") n'y tient pas");
        }
    } else {
        hmi::comm::Point pt;
        if (!eq::placeEquipmentAddress(address, eq::registerType(*v), pt, why)) return false;
    }
    if (v->address == address) return true;
    change(id, "Adresse de " + v->name + " : " + address, [&](hmi::Variable& x) { x.address = address; });
    if (const auto* now = doc_->project.variableById(id); now && ty::isComposite(now->type))
        say(now->name + " : " + ty::spanText(doc_->project, *now));
    return true;
}

bool HmiVariablesPane::setReadOnly(Id id, bool readOnly, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (v->readOnly == readOnly) return true;
    return change(id, (readOnly ? "Lecture seule : " : "Lecture et \xC3\xA9" "criture : ") + v->name, [&](hmi::Variable& x) { x.readOnly = readOnly; });
}

// ---- lot 20 : coller depuis Excel ----------------------------------------------
//  La cible du collage : les colonnes de la table (Place et Qualite sont
//  calculees : jamais ecrites), puis Description et Dossier (reconnues par leur
//  titre). Chaque case passe par l'action du volet, avec ses controles.
paste::Target HmiVariablesPane::pasteTarget(const ui::TableView::PasteRequest& rq) {
    paste::Target t;
    t.noun = "variable";
    t.nouns = "variables";
    t.feminine = true;
    const auto idOf = [this](const std::string& key) -> Id {
        const auto* v = doc_->project.variable(key);
        return v ? v->id : kNoId;
    };
    const auto col = [](std::string title, std::vector<std::string> aliases, int tableColumn,
                        std::function<bool(const std::string&, const std::string&, std::string*)> set, bool key = false) {
        paste::Column c;
        c.title = std::move(title);
        c.aliases = std::move(aliases);
        c.tableColumn = tableColumn;
        c.set = std::move(set);
        c.key = key;
        return c;
    };
    t.columns.push_back(col("Nom", {"Name", "Variable", "Mnemonique", "Symbole", "Tag", "Identifiant"}, CName, nullptr, true));
    t.columns.push_back(col("Type", {"Type de donnee", "DataType", "Data type"}, CType,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) { return setType(idOf(k), v, why); }));
    t.columns.push_back(col("Initiale", {"Valeur initiale", "Initial", "Init", "Initial value", "Valeur par defaut", "Defaut"}, CInitial,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) { return setInitial(idOf(k), v, why); }));
    t.columns.push_back(col("\xC3\x89quipement", {"Equipment", "Esclave", "Automate", "Liaison", "Device"}, CEquipment,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                                std::string name = v;
                                if (name == "-" || name == kDash || sameText(name, "(aucun)") || sameText(name, "aucun") || sameText(name, "(interne)")
                                    || sameText(name, "interne") || sameText(name, "locale"))
                                    name.clear();
                                return setEquipment(idOf(k), name, why);
                            }));
    t.columns.push_back(col("Adresse", {"Address", "Registre", "Adresse Modbus", "Mot", "Register"}, CAddress,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                                if (v == "-" || v == kDash || sameText(v, "(sans)")) return true;
                                return setAddress(idOf(k), v, why);
                            }));
    t.columns.push_back(col("Acc\xC3\xA8s", {"Access", "Lecture/ecriture", "R/W", "Mode"}, CAccess,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                                if (v == "-" || v == kDash) return true;
                                const std::string n = paste::normalizedTitle(v);
                                bool ro = false;
                                if (n == "lectureseule" || n == "lecture" || n == "r" || n == "ro" || n == "read" || n == "readonly" || n == "l")
                                    ro = true;
                                else if (n == "lectureecriture" || n == "ecriture" || n == "rw" || n == "le" || n == "readwrite" || n == "write" || n == "w")
                                    ro = false;
                                else
                                    return fail(why, "acc\xC3\xA8s inconnu : lecture seule, ou lecture/\xC3\xA9" "criture");
                                return setReadOnly(idOf(k), ro, why);
                            }));
    // 1.11.16 : Remanente - oui / non (VRAI, x, 1...) ; une variable liee : sans objet.
    t.columns.push_back(col("R\xC3\xA9manente", {"Remanente", "Retain", "Retentive", "Persistante", "Sauvegardee", "Memorisee"}, CRetain,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                                bool on = false;
                                if (!yesNo(v, on)) return fail(why, "R\xC3\xA9manente : oui ou non");
                                return setRetain(idOf(k), on, why);
                            }));
    t.columns.push_back(col("Place Modbus", {"Place"}, CPlace, nullptr));
    t.columns.push_back(col("Qualit\xC3\xA9 (en marche)", {"Qualite"}, CQuality, nullptr));
    t.columns.push_back(col("Description", {"Commentaire", "Comment", "Libelle", "Designation"}, -1,
                            [this, idOf](const std::string& k, const std::string& v, std::string*) { return setDescription(idOf(k), v); }));
    t.columns.push_back(col("Dossier", {"Folder", "Groupe", "Repertoire"}, -1,
                            [this, idOf](const std::string& k, const std::string& v, std::string* why) { return setFolder(idOf(k), v, why); }));
    t.exists = [this](const std::string& key) { return doc_->project.variable(key) != nullptr; };
    t.freeKey = [this](const std::string& key) {
        return hmi::isIdentifier(key) ? hmi::uniqueVariableName(doc_->project, key) : key;
    };
    // Creer : le type et le dossier de la ligne (un type inconnu : REAL, et la
    // case est marquee ; sans colonne Type : REAL, la mesure la plus courante).
    t.create = [this](const std::string& key, const std::map<std::string, std::string>& cells, paste::Notes& notes,
                      std::vector<std::string>& used, std::string* why) -> std::string {
        std::string type = "REAL";
        if (const auto it = cells.find("type"); it != cells.end()) {
            std::string reason;
            const std::string wanted = ty::normalized(it->second);
            if (ty::validType(doc_->project, wanted, &reason)) type = wanted;
            else notes.push_back({"Type", "type \xC2\xAB " + it->second + " \xC2\xBB inconnu \xE2\x80\x94 la variable est cr\xC3\xA9\xC3\xA9" "e en REAL"});
        }
        used.push_back("type");
        std::string folder;
        if (const auto it = cells.find("dossier"); it != cells.end()) {
            std::string reason;
            if (ty::validFolder(it->second, &reason)) folder = it->second;
            else notes.push_back({"Dossier", reason + " \xE2\x80\x94 la variable est \xC3\xA0 la racine"});
            used.push_back("dossier");
        }
        const Id made = addVariable(key, type, {}, why);
        if (made == kNoId) return {};
        if (!folder.empty()) {
            // Le dossier par l'action du volet : celui qui existe deja (sa casse),
            // ajoute a la liste des dossiers s'il est neuf.
            std::string w;
            if (!setFolder(made, folder, &w)) notes.push_back({"Dossier", w});
        }
        const auto* v = doc_->project.variableById(made);
        return v ? v->name : key;
    };
    // Sans la colonne Nom : les lignes collees vont dans les variables montrees,
    // a partir de la ligne choisie.
    for (std::size_t i = rq.anchorViewRow; i < table_->visibleRowCount(); ++i) {
        const auto r = table_->viewRow(i);
        if (r < rows_.size() && rows_[r].kind == Row::Kind::Variable) t.keysFromAnchor.push_back(rows_[r].key);
    }
    return t;
}

// ============================================================ 1.11.16 =====
//  LA REMANENCE D'EXPLOITATION, dans l'editeur : la case de chaque variable, et
//  le stockage du poste (hmi::retain) - lu pour la fiche, reinitialise, exporte,
//  importe, verifie. La simulation de l'editeur ne l'ecrit jamais.
bool HmiVariablesPane::setRetain(Id id, bool on, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (v->retain == on) return true;
    if (on && ty::isRich(v->type))   // 1.12.2
        return fail(why, v->name + " est un " + ty::normalized(v->type) + " : un objet de l'IHM, que R\xC3\xA9manente ne garde pas (des cases simples seulement)");
    if (on && v->bound())
        return fail(why, v->name + " est li\xC3\xA9" "e \xC3\xA0 " + v->equipment + " : sa valeur vient de l'\xC3\xA9quipement, qui la garde "
                         "(R\xC3\xA9manente vaut pour une variable de l'IHM)");
    return change(id, std::string(on ? "R\xC3\xA9manente : " : "Non r\xC3\xA9manente : ") + v->name, [&](hmi::Variable& x) { x.retain = on; });
}

std::string HmiVariablesPane::retainFile() const { return hosts_.retainFile ? hosts_.retainFile() : std::string{}; }

bool HmiVariablesPane::retainBusy(std::string* why) const {
    if (hosts_.stationRunning && hosts_.stationRunning()) {
        (void)fail(why, "le poste d'exploitation est en marche : il \xC3\xA9" "crit lui-m\xC3\xAAme ses valeurs (passe en conception d'abord)");
        return true;
    }
    // Un poste d'exploitation d'un autre processus (le verrou du stockage, encore frais).
    if (const std::string other = hmi::retain::lockedBy(retainFile()); !other.empty()) {
        (void)fail(why, "un poste d'exploitation en marche \xC3\xA9" "crit ce stockage (" + other + ") : arr\xC3\xAAte-le d'abord");
        return true;
    }
    return false;
}

const hmi::retain::Store* HmiVariablesPane::retainStore() const {
    const std::string file = retainFile();
    if (file.empty()) {
        retainCache_.reset();
        retainStamp_.clear();
        retainNote_.clear();
        retainReadable_ = false;
        return nullptr;
    }
    // L'empreinte : la date et la taille du fichier et de sa copie (relu s'il change).
    std::string stamp = file;
    for (const auto& f : {std::filesystem::path(file), std::filesystem::path(file + ".bak")}) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(f, ec);
        stamp += ec ? std::string("|-") : "|" + std::to_string(t.time_since_epoch().count());
        const auto size = std::filesystem::file_size(f, ec);
        stamp += ec ? std::string("|-") : "|" + std::to_string(size);
    }
    if (!retainCache_ || stamp != retainStamp_) {
        retainStamp_ = stamp;
        retainCache_.emplace();
        std::string why;
        bool backup = false;
        retainReadable_ = hmi::retain::load(file, *retainCache_, &why, &backup);
        std::error_code ec;
        retainNote_ = !retainReadable_ ? (std::filesystem::exists(file, ec) ? "stockage illisible (" + why + ")" : std::string{})
                    : backup           ? "stockage ab\xC3\xAEm\xC3\xA9 : sa copie de secours est reprise"
                                       : std::string{};
    }
    return retainReadable_ ? &*retainCache_ : nullptr;
}

bool HmiVariablesPane::resetRetained(Id variable, std::size_t* count, std::string* why) {
    if (count) *count = 0;
    const std::string file = retainFile();
    if (file.empty()) return fail(why, "projet jamais enregistr\xC3\xA9 : pas de stockage");
    if (variable != kNoId && !doc_->project.variableById(variable)) return fail(why, "variable introuvable");
    if (retainBusy(why)) return false;
    hmi::retain::Store s;
    std::string w;
    bool backup = false;
    std::error_code ec;
    if (!hmi::retain::load(file, s, &w, &backup)) {
        if (!std::filesystem::exists(file, ec)) return true;   // rien n'a jamais ete garde
        s = {};                                                 // illisible et sans copie : un stockage vide le remplace
        backup = true;
    }
    const std::size_t n = hmi::retain::reset(s, variable);
    if (count) *count = n;
    if (n == 0 && !backup) return true;
    s.project = doc_->project.config.name;
    s.date = hmi::simdata::nowStamp();
    if (const auto st = hmi::retain::save(file, s); !st) return fail(why, "\xC3\xA9" "criture impossible : " + st.error().message());
    return true;
}

bool HmiVariablesPane::exportRetained(const std::string& path, std::string* why) {
    const std::string file = retainFile();
    if (file.empty()) return fail(why, "projet jamais enregistr\xC3\xA9 : pas de stockage");
    if (trimmed(path).empty()) return fail(why, "pas de fichier");
    hmi::retain::Store s;
    std::string w;
    if (!hmi::retain::load(file, s, &w)) return fail(why, "rien \xC3\xA0 exporter : " + w);
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::u8string u8(path.begin(), path.end());
    const std::filesystem::path out(u8);
    std::error_code ec;
    if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
    const auto st = core::writeFileAtomic(out, ext == ".csv" ? hmi::retain::toCsv(s) : hmi::retain::serialize(s));
    if (!st) return fail(why, "\xC3\xA9" "criture impossible : " + st.error().message());
    if (why) *why = std::to_string(s.entries.size()) + " valeur(s) export\xC3\xA9" "e(s)";
    return true;
}

bool HmiVariablesPane::importRetained(const std::string& path, std::string* report) {
    const std::string file = retainFile();
    if (file.empty()) return fail(report, "projet jamais enregistr\xC3\xA9 : pas de stockage");
    if (retainBusy(report)) return false;
    const std::u8string u8(path.begin(), path.end());
    std::string text;
    if (trimmed(path).empty() || !core::readFileAll(std::filesystem::path(u8), text)) return fail(report, "impossible de lire " + path);
    hmi::retain::Store incoming, s;
    std::string w;
    if (!hmi::retain::parseAny(text, incoming, &w)) return fail(report, "fichier refus\xC3\xA9 : " + w);
    std::error_code ec;
    if (!hmi::retain::load(file, s, &w) && std::filesystem::exists(file, ec)) s = {};   // illisible : remplace
    const std::string now = hmi::simdata::nowStamp();
    const auto rep = hmi::retain::importInto(s, doc_->project, incoming, now);
    if (rep.variables == 0) return fail(report, "rien d'import\xC3\xA9 : " + rep.summary());
    s.project = doc_->project.config.name;
    s.date = now;
    if (const auto st = hmi::retain::save(file, s); !st) return fail(report, "\xC3\xA9" "criture impossible : " + st.error().message());
    if (report) *report = rep.summary();
    return true;
}

std::string HmiVariablesPane::retainIntegrity() const {
    std::size_t flagged = 0;
    for (const auto& v : doc_->project.programs.variables) flagged += v.retain && !v.bound() ? 1u : 0u;
    return std::to_string(flagged) + " variable(s) r\xC3\xA9manente(s). " + hmi::retain::checkIntegrity(retainFile(), doc_->project);
}

void HmiVariablesPane::askRetainExport() {
    const std::string file = retainFile();
    const std::string folder = file.empty() ? std::string{} : std::filesystem::path(file).parent_path().parent_path().parent_path().string();
    const std::string initial = ui::pathIn(folder, "exports/remanence_exploitation.csv");
    const auto write = [this](const std::string& path) {
        std::string why;
        if (exportRetained(path, &why)) say("Valeurs r\xC3\xA9manentes export\xC3\xA9" "es : " + why + " \xE2\x86\x92 " + path);
        else say("Exporter : " + why, true);
    };
    if (hosts_.askPath)
        hosts_.askPath("Exporter les valeurs r\xC3\xA9manentes",
                       "Les valeurs gard\xC3\xA9" "es par le poste d'exploitation. En .csv : un tableau pour Excel (Variable;Chemin;Type;Valeur;Date) ; "
                       "sinon le format du poste (.txt).",
                       initial, ui::saveFile("Tableau Excel (CSV)|*.csv|Format du poste|*.txt", initial, "Exporter les valeurs r\xC3\xA9manentes"), write);
    else
        write(initial);
}

void HmiVariablesPane::askRetainImport() {
    const std::string file = retainFile();
    const std::string folder = file.empty() ? std::string{} : std::filesystem::path(file).parent_path().parent_path().parent_path().string();
    const std::string initial = ui::pathIn(folder, "exports/remanence_exploitation.csv");
    const auto read = [this](const std::string& path) {
        std::string rep;
        const bool ok = importRetained(path, &rep);
        say(ok ? "Valeurs r\xC3\xA9manentes import\xC3\xA9" "es : " + rep + " \xE2\x80\x94 rendues au prochain lancement du poste." : "Importer : " + rep, !ok);
        if (hosts_.report) hosts_.report(ok ? "Import des valeurs r\xC3\xA9manentes" : "Import refus\xC3\xA9", ok ? rep + "." : rep, !ok);
        rebuildProperties();
    };
    if (hosts_.askPath)
        hosts_.askPath("Importer les valeurs r\xC3\xA9manentes",
                       "Un fichier fait par Exporter (le format du poste, ou un tableau CSV d'Excel : Variable;Chemin;Type;Valeur). "
                       "Ses valeurs remplacent celles des m\xC3\xAAmes variables ; les variables inconnues, non r\xC3\xA9manentes ou li\xC3\xA9" "es "
                       "sont \xC3\xA9" "cart\xC3\xA9" "es. L'ancien stockage reste en copie (.bak).",
                       initial, ui::openFile("Valeurs r\xC3\xA9manentes|*.csv;*.txt", initial, "Importer les valeurs r\xC3\xA9manentes"), read);
    else
        read(initial);
}

bool HmiVariablesPane::setPackBools(Id id, bool pack) {
    const auto* v = doc_->project.variableById(id);
    if (!v || v->packBools == pack) return v != nullptr;
    return change(id, std::string(pack ? "BOOL rang\xC3\xA9s 16 par mot : " : "BOOL un mot chacun : ") + v->name, [&](hmi::Variable& x) { x.packBools = pack; });
}

bool HmiVariablesPane::setMemberAddress(Id id, const std::string& rel, const std::string& raw, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (!v->bound()) return fail(why, v->name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement");
    const std::string address = trimmed(raw);
    const std::string path = v->name + (rel.empty() || rel.front() == '[' ? "" : ".") + rel;
    const std::string type = ty::typeOfPath(doc_->project, path);
    if (type.empty()) return fail(why, path + " : pas un membre de " + v->name);
    if (ty::isInternalMember(*v, rel)) return fail(why, path + " est interne (gard\xC3\xA9" "e dans l'IHM) : l'attribuer \xC3\xA0 " + v->equipment + " d'abord");
    if (!address.empty() && ty::isComposite(type)) {
        // 1.11.8 : le depart d'un membre compose (Vannes[2] -> %MW500) : ses cases le suivent. De la
        // meme sorte que celui de la variable (des mots, ou des bits si elle est dans une zone de bits).
        if (ty::bitArea(v->address)) {
            if (!ty::bitArea(address)) return fail(why, v->name + " est dans une zone de bits (" + v->address + ") : le d\xC3\xA9part de " + path + " aussi");
        } else if (ty::bitArea(address) || ty::memberAddress(address, 0, -1, "INT", why).empty()) {
            if (why && why->empty()) *why = "le d\xC3\xA9part de " + path + " est un mot (%MW, 4x, 40001), pas un bit";
            return false;
        }
    } else if (!address.empty()) {
        hmi::Variable probe;
        probe.type = type;
        hmi::comm::Point pt;
        if (!eq::placeEquipmentAddress(address, eq::registerType(probe), pt, why)) return false;
    }
    return change(id, "Adresse de " + path + (address.empty() ? " : calcul\xC3\xA9" "e" : " : " + address), [&](hmi::Variable& x) {
        auto& pl = x.places;
        pl.erase(std::remove_if(pl.begin(), pl.end(), [&](const hmi::MemberAddress& m) { return sameText(m.path, rel); }), pl.end());
        if (!address.empty()) pl.push_back({rel, address});
    });
}

bool HmiVariablesPane::setMembersInternal(Id id, const std::vector<std::string>& relPaths, bool internal, bool allElements, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (!v->bound()) return fail(why, v->name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement : ses membres sont d\xC3\xA9j\xC3\xA0 dans l'IHM");
    if (!ty::isComposite(v->type)) return fail(why, v->name + " n'a pas de membres");
    std::vector<std::string> wanted;
    for (const auto& r : relPaths) {
        const std::string rel = trimmed(r);
        if (rel.empty()) continue;
        if (ty::typeOfPath(doc_->project, v->name + (rel.front() == '[' ? "" : ".") + rel).empty())
            return fail(why, rel + " : pas un membre de " + v->name);
        const std::string pattern = allElements ? allElementsOf(rel) : rel;
        if (std::find(wanted.begin(), wanted.end(), pattern) == wanted.end()) wanted.push_back(pattern);
    }
    if (wanted.empty()) return fail(why, "aucun membre choisi");
    // Les cases de la variable (sans membres internes) : pour defaire un motif qui en couvre plus.
    hmi::Variable all = *v;
    all.internal.clear();
    std::vector<std::string> rels;
    for (const auto& l : ty::leafVariables(doc_->project, all)) {
        const std::string rest = l.name.substr(std::min(l.name.size(), v->name.size()));
        rels.push_back(!rest.empty() && rest.front() == '.' ? rest.substr(1) : rest);
    }
    std::vector<std::string> next = v->internal;
    for (const auto& pattern : wanted) {
        if (internal) {
            // Deja couvert : rien a faire ; sinon il remplace ceux qu'il couvre.
            if (std::any_of(next.begin(), next.end(), [&](const std::string& e) { return ty::memberCovers(e, pattern); })) continue;
            std::erase_if(next, [&](const std::string& e) { return ty::memberCovers(pattern, e); });
            next.push_back(pattern);
            continue;
        }
        // Attribuer : retirer ce qu'il couvre ; un motif plus large ([*].NOM pour [0].NOM) se defait
        // en ses cases, moins celles-ci.
        std::vector<std::string> keep, expanded;
        for (const auto& e : next) {
            if (ty::memberCovers(pattern, e)) continue;                       // lui, ou dessous
            if (!ty::memberCovers(e, pattern) && !std::any_of(rels.begin(), rels.end(), [&](const std::string& r) {
                    return ty::memberCovers(e, r) && ty::memberCovers(pattern, r);
                })) {
                keep.push_back(e);
                continue;
            }
            for (const auto& r : rels)
                if (ty::memberCovers(e, r) && !ty::memberCovers(pattern, r)) expanded.push_back(r);
        }
        for (auto& x : expanded) keep.push_back(std::move(x));
        next = std::move(keep);
    }
    if (next == v->internal) return true;
    std::string what;
    for (const auto& w : wanted) what += (what.empty() ? "" : ", ") + w;
    const std::string label = (internal ? "Interne : " : "Attribu\xC3\xA9 \xC3\xA0 " + v->equipment + " : ") + v->name + " " + what;
    const bool ok = change(id, label, [&](hmi::Variable& x) { x.internal = next; });
    if (ok)
        say(internal ? v->name + " " + what + " : interne, gard\xC3\xA9 dans l'IHM (l'\xC3\xA9quipement ne le lit ni ne l'\xC3\xA9" "crit). "
                           "Sa place reste r\xC3\xA9serv\xC3\xA9" "e : \xC2\xAB Recalculer la place m\xC3\xA9moire \xC2\xBB la rend."
                     : v->name + " " + what + " : attribu\xC3\xA9 \xC3\xA0 " + v->equipment + ".",
            false);
    return ok;
}

bool HmiVariablesPane::recalculatePlace(Id id, bool on, std::string* why) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return fail(why, "variable introuvable");
    if (!v->bound() || !ty::isComposite(v->type)) return fail(why, v->name + " : une structure ou un tableau li\xC3\xA9 \xC3\xA0 un \xC3\xA9quipement");
    const std::string before = ty::spanText(doc_->project, *v);
    hmi::Variable probe = *v;
    probe.compact = on;
    const std::string after = ty::spanText(doc_->project, probe);
    if (v->compact == on) {
        say(v->name + (on ? " : place d\xC3\xA9j\xC3\xA0 recalcul\xC3\xA9" "e (" : " : place d'origine (") + before + ")", false);
        return true;
    }
    const bool ok = change(id, (on ? "Recalculer la place m\xC3\xA9moire de " : "Place d'origine de ") + v->name,
                           [&](hmi::Variable& x) { x.compact = on; });
    if (ok) {
        if (v->internal.empty() && on)
            say(v->name + " : aucun membre interne, rien \xC3\xA0 rendre (" + after + "). Clic droit sur un membre : \xC2\xAB Rendre interne \xC2\xBB.", false);
        else
            say(v->name + (on ? " : place recalcul\xC3\xA9" "e - " : " : place d'origine - ") + after + " (avant : " + before + ")", false);
    }
    return ok;
}

std::vector<std::string> HmiVariablesPane::selectedMemberPaths() const {
    std::vector<std::string> out;
    const Id var = selectedVariable();
    for (const auto r : table_->selectedModelRows())
        if (r < rows_.size() && rows_[r].kind == Row::Kind::Member && rows_[r].var == var) out.push_back(rows_[r].rel);
    return out;
}

Id HmiVariablesPane::duplicateVariable(Id id) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return kNoId;
    const hmi::Variable copy = *v;
    const std::string name = hmi::uniqueVariableName(doc_->project, copy.name + "_copie");
    Id made = kNoId;
    apply(hmi::changeProject(doc_, "Dupliquer " + copy.name, [&](hmi::Project& p) {
        hmi::Variable x = copy;
        x.id = p.allocate();
        x.name = name;
        // La copie n'est pas liee : deux variables sur les memes registres se recouvriraient.
        x.equipment.clear();
        x.address.clear();
        x.places.clear();
        made = x.id;
        p.programs.variables.push_back(std::move(x));
    }));
    selectVariable(made);
    say(copy.name + " dupliqu\xC3\xA9" "e : " + name + " (pas li\xC3\xA9" "e)");
    return made;
}

bool HmiVariablesPane::deleteVariable(Id id) {
    const auto* v = doc_->project.variableById(id);
    if (!v) return false;
    const std::string name = v->name;
    apply(hmi::changeProject(doc_, "Supprimer la variable " + name, [&](hmi::Project& p) {
        auto& all = p.programs.variables;
        all.erase(std::remove_if(all.begin(), all.end(), [&](const hmi::Variable& x) { return x.id == id; }), all.end());
    }));
    say("Variable supprim\xC3\xA9" "e : " + name + " - Ctrl+Z la rend");
    return true;
}

bool HmiVariablesPane::addFolder(const std::string& raw, std::string* why) {
    const std::string path = trimmed(raw);
    if (!ty::validFolder(path, why)) return false;
    const auto all = ty::allFolders(doc_->project);
    if (std::any_of(all.begin(), all.end(), [&](const std::string& f) { return sameText(f, path); })) return fail(why, "le dossier " + path + " existe d\xC3\xA9j\xC3\xA0");
    apply(hmi::changeProject(doc_, "Nouveau dossier " + path, [&](hmi::Project& p) { p.programs.folders.push_back(path); }));
    say("Dossier cr\xC3\xA9\xC3\xA9 : " + path + " (un rangement : les noms des variables ne changent pas)");
    return true;
}

bool HmiVariablesPane::renameFolder(const std::string& path, const std::string& rawLeaf, std::string* why) {
    const std::string leaf = trimmed(rawLeaf);
    if (leaf.find('/') != std::string::npos) return fail(why, "un nom de dossier sans / (pour le ranger ailleurs, glisse-le dans l'arbre)");
    if (!ty::validFolder(leaf, why)) return false;
    const std::string parent = ty::folderParent(path);
    const std::string target = parent.empty() ? leaf : parent + "/" + leaf;
    if (target == path) return true;
    const auto all = ty::allFolders(doc_->project);
    if (!sameText(target, path) && std::any_of(all.begin(), all.end(), [&](const std::string& f) { return sameText(f, target); }))
        return fail(why, "le dossier " + target + " existe d\xC3\xA9j\xC3\xA0");
    const auto moved = [&](const std::string& f) -> std::optional<std::string> {
        if (sameText(f, path)) return target;
        if (upper(f).rfind(upper(path) + "/", 0) == 0) return target + f.substr(path.size());
        return std::nullopt;
    };
    apply(hmi::changeProject(doc_, "Renommer le dossier " + path + " en " + leaf, [&](hmi::Project& p) {
        for (auto& f : p.programs.folders)
            if (const auto m = moved(f)) f = *m;
        for (auto& v : p.programs.variables)
            if (const auto m = moved(v.folder)) v.folder = *m;
    }));
    if (collapsed_.erase(upper(path))) collapsed_.insert(upper(target));
    selectedKey_ = "F:" + upper(target);
    refresh();
    say("Dossier renomm\xC3\xA9 : " + path + " \xE2\x86\x92 " + target);
    return true;
}

bool HmiVariablesPane::deleteFolder(const std::string& path) {
    const std::string parent = ty::folderParent(path);
    std::size_t moved = 0;
    for (const auto& v : doc_->project.programs.variables)
        if (sameText(v.folder, path) || upper(v.folder).rfind(upper(path) + "/", 0) == 0) ++moved;
    apply(hmi::changeProject(doc_, "Supprimer le dossier " + path, [&](hmi::Project& p) {
        auto& fs = p.programs.folders;
        fs.erase(std::remove_if(fs.begin(), fs.end(), [&](const std::string& f) { return sameText(f, path) || upper(f).rfind(upper(path) + "/", 0) == 0; }),
                 fs.end());
        for (auto& v : p.programs.variables)
            if (sameText(v.folder, path) || upper(v.folder).rfind(upper(path) + "/", 0) == 0) v.folder = parent;
    }));
    say("Dossier supprim\xC3\xA9 : " + path + (moved ? " - ses " + std::to_string(moved) + " variable(s) sont dans " + (parent.empty() ? std::string("la racine") : parent) : std::string{})
        + " (Ctrl+Z le rend)");
    return true;
}

// Lot 21 : le depot d'un glisser de plusieurs lignes.
void HmiVariablesPane::dropRows(const std::vector<ui::RowIndex>& from, ui::RowIndex to, ui::TreeView::DropWhere where) {
    std::vector<Id> vars;
    std::vector<std::string> folders;
    for (const auto r : from) {
        if (r >= rows_.size()) continue;
        if (rows_[r].kind == Row::Kind::Variable) vars.push_back(rows_[r].var);
        else if (rows_[r].kind == Row::Kind::Folder) folders.push_back(rows_[r].key);
    }
    if (vars.empty() && folders.empty()) return;
    // Entre deux variables : reordonner, et prendre le dossier de la voisine.
    if (to != ui::TableView::kNoRow && to < rows_.size() && rows_[to].kind == Row::Kind::Variable) {
        if (!folders.empty() || where == ui::TreeView::DropWhere::Into) return;
        const Id anchor = rows_[to].var;
        const bool after = where == ui::TreeView::DropWhere::After;
        bool ok = false;
        apply(hmi::changeProject(doc_, "D\xC3\xA9placer " + std::to_string(vars.size()) + " variable(s)", [&](hmi::Project& p) {
            auto& list = p.programs.variables;
            const auto moved = [&](const hmi::Variable& v) { return std::find(vars.begin(), vars.end(), v.id) != vars.end(); };
            const auto at = std::find_if(list.begin(), list.end(), [&](const hmi::Variable& v) { return v.id == anchor; });
            if (at == list.end() || moved(*at)) return;
            const std::string folder = at->folder;
            std::vector<hmi::Variable> moving, rest;
            for (auto& v : list) (moved(v) ? moving : rest).push_back(std::move(v));
            auto it = std::find_if(rest.begin(), rest.end(), [&](const hmi::Variable& v) { return v.id == anchor; });
            if (after) ++it;
            for (auto& m : moving) m.folder = folder;
            rest.insert(it, std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
            list = std::move(rest);
            ok = true;
        }));
        if (ok) say(std::to_string(vars.size()) + " variable(s) d\xC3\xA9plac\xC3\xA9" "e(s) " + (after ? "apr\xC3\xA8s " : "avant ") + rows_[to].cells.front()
                    + " (Ctrl+Z les remet)");
        return;
    }
    // Sur un dossier, ou sous les lignes (la racine).
    const std::string into = to == ui::TableView::kNoRow || to >= rows_.size() ? std::string{} : rows_[to].key;
    std::size_t moved = 0;
    apply(hmi::changeProject(doc_, "Ranger dans " + (into.empty() ? std::string("la racine") : into), [&](hmi::Project& p) {
        const auto inside = [](const std::string& f, const std::string& root) {
            return sameText(f, root) || upper(f).rfind(upper(root) + "/", 0) == 0;
        };
        // Les dossiers d'abord : chacun (et ce qu'il contient) sous `into`.
        for (const auto& f : folders) {
            if (sameText(ty::folderParent(f), into)) continue;
            if (!into.empty() && inside(into, f)) continue;
            const std::string target = into.empty() ? ty::folderLeaf(f) : into + "/" + ty::folderLeaf(f);
            for (auto& x : p.programs.folders)
                if (inside(x, f)) x = target + x.substr(f.size());
            for (auto& v : p.programs.variables)
                if (inside(v.folder, f)) v.folder = target + v.folder.substr(f.size());
            ++moved;
        }
        for (auto& v : p.programs.variables)
            if (std::find(vars.begin(), vars.end(), v.id) != vars.end() && !sameText(v.folder, into)) {
                v.folder = into;
                ++moved;
            }
        if (!into.empty() && std::none_of(p.programs.folders.begin(), p.programs.folders.end(), [&](const std::string& f) { return sameText(f, into); }))
            p.programs.folders.push_back(into);
    }));
    if (moved) say(std::to_string(moved) + " \xC3\xA9l\xC3\xA9ment(s) rang\xC3\xA9(s) " + (into.empty() ? std::string("\xC3\xA0 la racine") : "dans " + into)
                   + " (les noms ne changent pas ; Ctrl+Z les remet)");
    if (vars.size() == 1) selectVariable(vars.front());
}

// ---- la grille -----------------------------------------------------------------------
void HmiVariablesPane::rebuildProperties() {
    const auto& p = doc_->project;
    std::vector<PG::Category> cats;
    const auto sel = table_->selectedModelRows();
    if (sel.empty() || sel.front() >= rows_.size()) {
        PG::Category c;
        c.name = "Variables IHM";
        c.properties.push_back(prop("Variables", std::to_string(p.programs.variables.size())));
        c.properties.push_back(prop("Dossiers", std::to_string(ty::allFolders(p).size())));
        c.properties.push_back(prop("Types IHM", std::to_string(p.programs.types.size())));
        // 1.11.16 : la remanence d'exploitation - combien de variables, et le stockage du poste.
        std::size_t flagged = 0;
        for (const auto& v : p.programs.variables) flagged += v.retain && !v.bound() ? 1u : 0u;
        c.properties.push_back(prop("R\xC3\xA9manentes (poste)", std::to_string(flagged)));
        c.properties.push_back(prop("Stockage du poste", retainIntegrity(), PG::ValueType::ReadOnly, {}, {},
                                    "Clic droit sur une variable : R\xC3\xA9initialiser, Exporter, Importer, Afficher l'emplacement, V\xC3\xA9rifier l'int\xC3\xA9grit\xC3\xA9."));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const Row row = rows_[sel.front()];
    if (row.kind == Row::Kind::Folder) {
        PG::Category c;
        c.name = "Dossier";
        const std::string path = row.key;
        c.properties.push_back(prop("Nom", ty::folderLeaf(path), PG::ValueType::Text, [this, path](std::string_view v) {
            std::string why;
            const bool ok = renameFolder(path, std::string(v), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, {}, "Un dossier range les variables, comme un filtre de Visual Studio : il ne change pas leur nom."));
        c.properties.push_back(prop("Chemin", path));
        c.properties.push_back(prop("Contenu", row.cells.size() > CType ? row.cells[CType] : std::string{}));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const auto* v = p.variableById(row.var);
    if (!v) {
        props_->setCategories({});
        return;
    }
    const Id id = v->id;
    const bool composite = ty::isComposite(v->type) && !hmi::findEnumeration(p, v->type);   // 1.10 (chantier U) : une enumeration est un DINT
    if (row.kind == Row::Kind::Member) {
        PG::Category c;
        c.name = "Membre";
        c.properties.push_back(prop("Chemin", row.key));
        c.properties.push_back(prop("Type", row.type));
        c.properties.push_back(prop("Variable", v->name + " (" + v->type + ")"));
        if (v->bound()) {
            // 1.11.8 : interne (garde dans l'IHM) ou attribue a l'equipement de la structure.
            const std::string rel = row.rel;
            const std::string entry = ty::internalEntryOf(*v, rel);
            const bool indexed = rel.find('[') != std::string::npos;
            const std::string attached = "attribu\xC3\xA9 \xC3\xA0 " + v->equipment;
            const std::string one = indexed ? "interne (cette case)" : "interne (gard\xC3\xA9 dans l'IHM)";
            const std::string all = "interne dans toutes les cases (" + allElementsOf(rel) + ")";
            std::vector<std::string> choices{attached, one};
            if (indexed) choices.push_back(all);
            const std::string current = entry.empty() ? attached : indexed && entry.find("[*]") != std::string::npos ? all : one;
            c.properties.push_back(prop("Liaison", current, PG::ValueType::Enum, [this, id, rel, attached, one, all](std::string_view text) {
                std::string why;
                const bool ok = text == attached ? setMembersInternal(id, {rel}, false, false, &why)
                              : text == all      ? setMembersInternal(id, {rel}, true, true, &why)
                                                 : setMembersInternal(id, {rel}, true, false, &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            }, choices, "Interne : le membre reste dans l'IHM (une variable IHM), l'\xC3\xA9quipement ne le lit ni ne l'\xC3\xA9" "crit ; sa place reste "
                   "r\xC3\xA9serv\xC3\xA9" "e jusqu'\xC3\xA0 \xC2\xAB Recalculer la place m\xC3\xA9moire \xC2\xBB. Aussi au clic droit, sur plusieurs lignes."));
        }
        if (v->bound() && row.editableAddress) {
            const std::string rel = row.rel;
            std::string address = row.cells.size() > CAddress ? row.cells[CAddress] : std::string{};
            if (const auto pen = address.find(" \xE2\x9C\x8E"); pen != std::string::npos) address.erase(pen);
            c.properties.push_back(prop("Adresse", address, PG::ValueType::Text, [this, id, rel](std::string_view text) {
                std::string why;
                const bool ok = setMemberAddress(id, rel, std::string(text), &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            }, {}, ty::isComposite(row.type)
                       ? "Le d\xC3\xA9part de ce membre : ses cases le suivent (1.11.8). Vide : la place calcul\xC3\xA9" "e. Un d\xC3\xA9part donn\xC3\xA9 porte un crayon."
                       : "Vide : la place calcul\xC3\xA9" "e (le d\xC3\xA9part + la place du membre). Une adresse corrig\xC3\xA9" "e porte un crayon dans le tableau."));
            c.properties.push_back(prop("Place Modbus", row.cells.size() > CPlace ? row.cells[CPlace] : std::string{}));
            c.properties.push_back(prop("En marche", row.cells.size() > CQuality ? row.cells[CQuality] : std::string{}));
        }
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    {
        PG::Category c;
        c.name = "Variable";
        c.properties.push_back(prop("Nom", v->name, PG::ValueType::Text, [this, id](std::string_view text) {
            // Lot API 8 : le dialogue qui montre ce qui suit (le nom tape deja ecrit) ; sans lui, sur place.
            if (renameInDialog(id, std::string(text))) return false;
            std::string why;
            const bool ok = setName(id, std::string(text), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, {}, "Unique parmi les variables IHM, casse comprise. Le dossier ne compte pas : deux dossiers ne peuvent pas avoir chacun leur Vitesse."));
        c.properties.push_back(prop("Type", v->type, PG::ValueType::Text, [this, id](std::string_view text) {
            std::string why;
            const bool ok = setType(id, std::string(text), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, {}, "INT, REAL, BOOL... ; un type IHM (T_Four) ; un tableau : ARRAY[0..9] OF REAL, ARRAY[0..3, 0..9] OF INT, ARRAY[1..4] OF T_Four."));
        std::vector<std::string> folders{"(racine)"};
        for (const auto& f : ty::allFolders(p)) folders.push_back(f);
        c.properties.push_back(prop("Dossier", v->folder.empty() ? std::string("(racine)") : v->folder, PG::ValueType::Enum, [this, id](std::string_view text) {
            std::string why;
            const bool ok = setFolder(id, std::string(text), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, folders, "Le rangement : sans effet sur le nom de la variable."));
        ty::Spec spec;
        const bool parsed = ty::parseSpec(v->type, spec);
        if (const auto* e = hmi::findEnumeration(p, v->type)) {   // 1.10 (chantier U) : la liste des valeurs, << Auto (1) >>
            std::vector<std::string> values;
            for (const auto& x : e->values) values.push_back(hmi::enumDisplay(*e, x.value));
            c.properties.push_back(prop("Initiale", enumInitialText(*e, v->initial), PG::ValueType::Enum, [this, id](std::string_view text) {
                std::string why;
                const bool ok = setInitial(id, std::string(text), &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            }, values, "Une valeur de " + e->name + " (\xC3\xA9num\xC3\xA9ration : la variable vaut un DINT, le nombre de la valeur)."));
        } else if (!composite || (parsed && spec.array() && ty::isElementary(spec.element)))
            c.properties.push_back(prop("Initiale", v->initial, PG::ValueType::Text, [this, id](std::string_view text) {
                std::string why;
                const bool ok = setInitial(id, std::string(text), &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            }, {}, composite ? "Une valeur pour toutes les cases (0), ou une liste case par case (1.5, 2, 3)." : "0, TRUE, 2.5, 'texte', T#5s"));
        else
            c.properties.push_back(prop("Initiale", "celles du type " + spec.element));
        c.properties.push_back(prop("Description", v->description, PG::ValueType::Text, [this, id](std::string_view text) {
            return setDescription(id, std::string(text));
        }));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "Liaison";
        std::vector<std::string> equipments{""};
        for (const auto& e : p.equipments)
            if (e.modbus()) equipments.push_back(e.name);
        c.properties.push_back(prop("\xC3\x89quipement", v->equipment, PG::ValueType::Enum, [this, id](std::string_view text) {
            std::string why;
            const bool ok = setEquipment(id, std::string(text), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, equipments, "Vide : la variable reste dans l'IHM. Li\xC3\xA9" "e, elle se lit et s'\xC3\xA9" "crit dans l'\xC3\xA9quipement (Configuration \xE2\x80\xBA \xC3\x89quipements)."));
        if (v->bound()) {
            c.properties.push_back(prop(composite ? "Adresse de d\xC3\xA9part" : "Adresse", v->address, PG::ValueType::Text, [this, id](std::string_view text) {
                std::string why;
                const bool ok = setAddress(id, std::string(text), &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            }, {}, "43001, 400101, 4x3001, %MW3000, HR3000 ; une structure part d'un mot, ses membres suivent."));
            c.properties.push_back(prop("Acc\xC3\xA8s", v->readOnly ? kRO : kRW, PG::ValueType::Enum, [this, id](std::string_view text) {
                return setReadOnly(id, text == kRO);
            }, {kRW, kRO}));
            if (composite)
                c.properties.push_back(prop("BOOL rang\xC3\xA9s 16 par mot", v->packBools ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                                            [this, id](std::string_view text) { return setPackBools(id, text == "TRUE"); }, {},
                                            "Coch\xC3\xA9 : les BOOL qui se suivent partagent un mot (bits 0 \xC3\xA0 15). D\xC3\xA9" "coch\xC3\xA9 : un mot chacun."));
            c.properties.push_back(prop("Place", composite ? ty::spanText(p, *v) : placeOf(v->address, v->type)));
            if (composite) {
                // 1.11.8 : les membres internes et leur place.
                std::string list;
                for (const auto& m : v->internal) list += (list.empty() ? "" : "; ") + m;
                c.properties.push_back(prop("Membres internes", list.empty() ? std::string("aucun") : list, PG::ValueType::ReadOnly, {}, {},
                                            "Gard\xC3\xA9s dans l'IHM : clic droit sur un membre, \xC2\xAB Rendre interne \xC2\xBB (ou sa ligne Liaison)."));
                const std::string kept = "r\xC3\xA9serv\xC3\xA9" "e (place d'origine)", freed = "rendue (place recalcul\xC3\xA9" "e)";
                c.properties.push_back(prop("Place des membres internes", v->compact ? freed : kept, PG::ValueType::Enum,
                                            [this, id, freed](std::string_view text) {
                                                std::string w;
                                                const bool ok = recalculatePlace(id, text == freed, &w);
                                                if (!ok) say("Refus\xC3\xA9 : " + w, true);
                                                return ok;
                                            },
                                            {kept, freed}, "Rendue : les mots que n'occupent que des membres internes sont rendus et les membres suivants se "
                                                "resserrent (le bouton \xC2\xAB Recalculer la place m\xC3\xA9moire \xC2\xBB). R\xC3\xA9serv\xC3\xA9" "e : chaque membre garde sa place d'origine."));
            }
        }
        cats.push_back(std::move(c));
    }
    {
        // ---- 1.11.16 : LA REMANENCE D'EXPLOITATION (le poste) - a part de celle de la simulation ----
        PG::Category c;
        c.name = "R\xC3\xA9manence (exploitation)";
        const std::string help = "Coch\xC3\xA9" "e : sur le poste d'exploitation, sa valeur est gard\xC3\xA9" "e \xC3\xA0 chaque changement (d'un bloc, "
                                 "au plus une \xC3\xA9" "criture par seconde) et rendue au lancement suivant, apr\xC3\xA8s les valeurs initiales et avant "
                                 "les scripts de D\xC3\xA9marrage. La simulation de l'\xC3\xA9" "diteur n'y \xC3\xA9" "crit jamais (sa r\xC3\xA9manence est \xC3\xA0 part).";
        if (v->bound())
            c.properties.push_back(prop("R\xC3\xA9manente", v->retain ? "oui (sans effet : li\xC3\xA9" "e)" : "sans objet (li\xC3\xA9" "e)", PG::ValueType::ReadOnly,
                                        {}, {}, "R\xC3\xA9manente : li\xC3\xA9" "e \xC3\xA0 " + v->equipment + ", sa valeur vient de l'\xC3\xA9quipement, qui la garde."));
        else
            c.properties.push_back(prop("R\xC3\xA9manente", v->retain ? "TRUE" : "FALSE", PG::ValueType::Boolean, [this, id](std::string_view text) {
                std::string why;
                const bool ok = setRetain(id, text == "TRUE", &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            }, {}, help));
        std::string initial = v->initial;
        if (const auto* e = hmi::findEnumeration(p, v->type)) initial = enumInitialText(*e, v->initial);
        else if (trimmed(initial).empty()) initial = composite ? std::string("celles du type") : std::string("la valeur par d\xC3\xA9" "faut du type");
        c.properties.push_back(prop("Initiale", initial, PG::ValueType::ReadOnly, {}, {},
                                    "Valeur initiale : reprise quand rien n'est gard\xC3\xA9, apr\xC3\xA8s une r\xC3\xA9initialisation, ou si le type a chang\xC3\xA9 sans conversion possible."));
        const std::string current = hosts_.currentValue ? hosts_.currentValue(v->name) : std::string{};
        c.properties.push_back(prop("Actuelle", current.empty() ? std::string("\xE2\x80\x94 (simulation arr\xC3\xAAt\xC3\xA9" "e)")
                                                                 : current + " (simulation de l'\xC3\xA9" "diteur : jamais gard\xC3\xA9" "e)",
                                    PG::ValueType::ReadOnly, {}, {},
                                    "Valeur actuelle : sur le poste d'exploitation, c'est la valeur du moment qui est gard\xC3\xA9" "e ; ici, celle de la simulation de "
                                    "l'\xC3\xA9" "diteur (s\xC3\xA9par\xC3\xA9" "e, jamais gard\xC3\xA9" "e)."));
        const auto* store = retainStore();
        const auto st = hmi::retain::stateOf(p, *v, store);
        c.properties.push_back(prop("Gard\xC3\xA9" "e", st.saved ? st.value : std::string("aucune"), PG::ValueType::ReadOnly, {}, {},
                                    "Derni\xC3\xA8re valeur sauvegard\xC3\xA9" "e par le poste d'exploitation (une structure : son nombre de cases)."));
        c.properties.push_back(prop("Gard\xC3\xA9" "e le", st.date.empty() ? kDash : st.date, PG::ValueType::ReadOnly, {}, {},
                                    "Date de derni\xC3\xA8re sauvegarde : une valeur qui ne change pas garde sa date."));
        c.properties.push_back(prop("\xC3\x89tat", retainNote_.empty() ? st.state : retainNote_ + " \xE2\x80\x94 " + st.state, PG::ValueType::ReadOnly,
                                    {}, {}, "\xC3\x89tat de sauvegarde, lu dans le stockage du poste. Clic droit : R\xC3\xA9initialiser cette variable r\xC3\xA9manente."));
        const std::string file = retainFile();
        c.properties.push_back(prop("Fichier", file.empty() ? std::string("projet jamais enregistr\xC3\xA9") : file, PG::ValueType::ReadOnly, {}, {},
                                    "Le stockage du poste (et sa copie .bak) : \xC3\xA9" "crit d'un bloc, hors des versions du projet."));
        cats.push_back(std::move(c));
    }
    if (composite && v->bound()) {
        PG::Category c;
        c.name = "Membres et adresses";
        std::vector<std::string> whys;
        const auto leaves = ty::leafVariables(p, *v, nullptr, &whys);
        const std::size_t shown = std::min<std::size_t>(leaves.size(), 64);
        for (std::size_t i = 0; i < shown; ++i) {
            const auto& l = leaves[i];
            const std::string rel = l.name.substr(v->name.size() + (l.name.size() > v->name.size() && l.name[v->name.size()] == '.' ? 1 : 0));
            bool fixed = false;
            for (const auto& pl : v->places)
                if (sameText(pl.path, rel)) fixed = true;
            const std::string why = i < whys.size() ? whys[i] : std::string{};
            if (!l.bound()) {                                   // 1.11.8 : un membre interne
                c.properties.push_back(prop(rel, "interne (IHM)", PG::ValueType::ReadOnly, {}, {}, l.type + " \xC2\xB7 gard\xC3\xA9 dans l'IHM"));
                continue;
            }
            c.properties.push_back(prop(rel + (fixed ? " \xE2\x9C\x8E" : ""), l.address.empty() ? "(" + why + ")" : l.address, PG::ValueType::Text,
                                        [this, id, rel](std::string_view text) {
                                            std::string w;
                                            const bool ok = setMemberAddress(id, rel, std::string(text), &w);
                                            if (!ok) say("Refus\xC3\xA9 : " + w, true);
                                            return ok;
                                        },
                                        {}, l.type + (fixed ? " \xC2\xB7 adresse corrig\xC3\xA9" "e (vide : la place calcul\xC3\xA9" "e)" : " \xC2\xB7 place calcul\xC3\xA9" "e")));
        }
        if (leaves.size() > shown) c.properties.push_back(prop("\xE2\x80\xA6", std::to_string(leaves.size() - shown) + " autres cases"));
        cats.push_back(std::move(c));
    }
    if (composite) {
        PG::Category c;
        c.name = "Poids et propri\xC3\xA9t\xC3\xA9s";
        const auto flat = ty::flatten(p, v->name, v->type, {}, v->packBools);
        if (flat.ok() && !flat.aggregates.empty()) {
            const auto& a = flat.aggregates.back();
            for (const auto& pi : ty::properties()) {
                if ((pi.arrayOnly && !a.array) || (pi.twoD && a.spec.dims != 2)) continue;
                sim::Value value;
                if (!ty::propertyValue(a, pi.name, value)) continue;
                c.properties.push_back(prop(v->name + "." + std::string(pi.name), value.display(), PG::ValueType::ReadOnly, {}, {}, std::string(pi.help)));
            }
        } else {
            c.properties.push_back(prop("Erreur", flat.error));
        }
        cats.push_back(std::move(c));
    }
    props_->setCategories(std::move(cats));
}

void HmiVariablesPane::onLayout() {
    const auto b = bounds();
    constexpr float kTools = 38.f, kFilter = 36.f, kStatus = 24.f;
    tools_->setBounds({b.x, b.y, b.w, kTools});
    if (children().size() > 1) children()[1]->setBounds({b.x, b.y + kTools, b.w, kFilter});
    split_->setBounds({b.x, b.y + kTools + kFilter, b.w, std::max(0.f, b.h - kTools - kFilter - kStatus)});
    status_->setBounds({b.x, b.y + b.h - kStatus, b.w, kStatus});
}

void HmiVariablesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

// ---- 1.10 (chantier O) : le clic droit, la fenetre graphique ----------------------
namespace {

constexpr std::size_t kTrendMax = 24;      // les courbes d'une fenetre, au plus

// Un type qui se trace : un nombre, un BOOL (pas un texte, pas une structure).
bool trendable(std::string_view type) {
    static const char* const kinds[] = {"BOOL", "EBOOL", "BYTE", "WORD", "DWORD", "SINT", "INT", "DINT", "LINT",
                                        "USINT", "UINT", "UDINT", "ULINT", "REAL", "LREAL", "TIME"};
    const std::string u = upper(trimmed(type));
    return std::any_of(std::begin(kinds), std::end(kinds), [&](const char* k) { return u == k; });
}

} // namespace

std::vector<std::string> HmiVariablesPane::trendPaths() const {
    std::vector<std::string> out;
    const auto push = [&out](const std::string& path) {
        if (out.size() < kTrendMax && std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    };
    const auto& p = doc_->project;
    for (const auto r : table_->selectedModelRows()) {
        if (r >= rows_.size()) continue;
        const auto& row = rows_[r];
        if (row.kind != Row::Kind::Variable && row.kind != Row::Kind::Member) continue;
        const auto* v = p.variableById(row.var);
        if (!v) continue;
        const std::string path = row.kind == Row::Kind::Variable ? v->name : row.key;
        const std::string type = row.kind == Row::Kind::Variable ? v->type : row.type;
        if (trendable(type)) {
            push(path);
            continue;
        }
        if (!ty::isComposite(type)) continue;              // un texte ne se trace pas
        const auto flat = ty::flatten(p, path, type);
        for (const auto& l : flat.leaves)
            if (trendable(l.type)) push(l.path);
    }
    return out;
}

HmiQuickTrend* HmiVariablesPane::openTrend() {
    auto paths = trendPaths();
    if (paths.empty()) {
        say("Rien \xC3\xA0 tracer : choisis une variable num\xC3\xA9rique ou BOOL (une structure : ses membres).", true);
        return nullptr;
    }
    auto w = std::make_unique<HmiQuickTrend>(id() + ".graphique" + std::to_string(++trendCount_), std::move(paths), hosts_.trendSource);
    // Le bouton + de la fenetre : les variables choisies ici (l'onglet) s'ajoutent.
    links_ += w->addRequested->connect([this, raw = w.get()] {
        const auto more = trendPaths();
        std::size_t added = 0;
        for (const auto& path : more) added += raw->addVariable(path) ? 1u : 0u;
        if (!added) say("Choisis dans l'onglet Variables IHM la variable \xC3\xA0 ajouter, puis + dans la fen\xC3\xAAtre.", true);
        else say(raw->title());
    });
    say(w->title() + (w->live() ? std::string(" \xE2\x80\x94 en direct.") : std::string(" \xE2\x80\x94 d\xC3\xA9marre la simulation pour voir les valeurs.")));
    if (hosts_.showTrend) return hosts_.showTrend(std::move(w));
    trends_.push_back(std::move(w));
    return trends_.back().get();
}

void HmiVariablesPane::openContextMenu(gfx::Point at) {
    if (!menu_) return;
    // Le menu de la table (Copier, Coller, Tout choisir, les filtres), puis les
    // entrees du volet en tete.
    table_->openContextMenu(at);
    const auto paths = trendPaths();
    const auto sel = table_->selectedModelRows();
    const bool any = !sel.empty();
    const bool one = sel.size() == 1 && sel.front() < rows_.size()
                  && (rows_[sel.front()].kind == Row::Kind::Variable || rows_[sel.front()].kind == Row::Kind::Folder);
    const bool oneVar = one && rows_[sel.front()].kind == Row::Kind::Variable;
    const std::string none = "choisis une variable (ou un dossier)";
    std::vector<ui::PopupMenu::Item> items;
    items.push_back({"Ouvrir une visualisation graphique", std::to_string(paths.size()) + (paths.size() > 1 ? " courbes" : " courbe"),
                     paths.empty() ? std::string("rien \xC3\xA0 tracer : une variable num\xC3\xA9rique ou BOOL") : std::string{},
                     ui::Icon::Chart, !paths.empty(), false, kMenuBase - MTrend});
    // Comme la maquette : ce qui ne se trace pas (un texte, une couleur) est dit.
    std::size_t noTrace = 0;
    for (const auto r : sel) {
        if (r >= rows_.size() || rows_[r].kind != Row::Kind::Variable) continue;
        const auto* v = doc_->project.variableById(rows_[r].var);       // le chemin trace : son nom (trendPaths)
        if (!v) continue;
        const std::string& key = v->name;
        const bool traced = std::any_of(paths.begin(), paths.end(), [&](const std::string& p) {
            return p == key || (p.size() > key.size() && p.compare(0, key.size(), key) == 0 && (p[key.size()] == '.' || p[key.size()] == '['));
        });
        if (!traced) ++noTrace;
    }
    if (noTrace)
        items.push_back({std::to_string(noTrace) + (noTrace > 1 ? " variables sans courbe" : " variable sans courbe") + " (texte, couleur\xE2\x80\xA6)",
                         "", "", ui::Icon::None, false, false, -1});
    items.push_back({"", "", "", ui::Icon::None, true, true, -1});
    items.push_back({"Copier le nom", "", any ? std::string{} : none, ui::Icon::None, any, false, kMenuBase - MCopyName});
    items.push_back({"Renommer\xE2\x80\xA6", "F2", one ? std::string{} : none, ui::Icon::None, one, false, kMenuBase - MRename});
    items.push_back({"Dupliquer", "", oneVar ? std::string{} : std::string("une variable \xC3\xA0 la fois"), ui::Icon::None, oneVar, false,
                     kMenuBase - MDuplicate});
    items.push_back({"Supprimer", "Suppr", one ? std::string{} : std::string("une ligne \xC3\xA0 la fois"), ui::Icon::Close, one, false, kMenuBase - MDelete});
    // 1.11.8 : un membre d'une structure liee - interne (garde dans l'IHM) ou attribue a son equipement.
    if (const auto* v = doc_->project.variableById(selectedVariable()); v && v->bound() && ty::isComposite(v->type)) {
        items.push_back({"", "", "", ui::Icon::None, true, true, -1});
        const auto members = selectedMemberPaths();
        const std::string none = "clic droit sur un membre (une ligne sous " + v->name + ")";
        std::size_t internals = 0;
        bool indexed = false;
        for (const auto& m : members) {
            internals += ty::isInternalMember(*v, m) ? 1 : 0;
            indexed = indexed || m.find('[') != std::string::npos;
        }
        const std::string what = members.size() == 1 ? v->name + (members.front().front() == '[' ? "" : ".") + members.front()
                                                     : std::to_string(members.size()) + " membres";
        const std::string every = members.size() == 1 ? v->name + (members.front().front() == '[' ? "" : ".") + allElementsOf(members.front()) : "toutes les cases";
        const bool canInternal = !members.empty() && internals < members.size();
        const bool canAttach = internals > 0;
        items.push_back({"Rendre interne (gard\xC3\xA9 dans l'IHM)", members.empty() ? std::string{} : what,
                         members.empty() ? none : !canInternal ? std::string("d\xC3\xA9j\xC3\xA0 interne") : std::string{}, ui::Icon::None, canInternal, false,
                         kMenuBase - MInternal});
        if (indexed)
            items.push_back({"Rendre interne dans toutes les cases", every, members.empty() ? none : std::string{}, ui::Icon::None, !members.empty(), false,
                             kMenuBase - MInternalAll});
        items.push_back({"Attribuer \xC3\xA0 " + v->equipment, members.empty() ? std::string{} : what,
                         members.empty() ? none : !canAttach ? std::string("d\xC3\xA9j\xC3\xA0 attribu\xC3\xA9 \xC3\xA0 l'\xC3\xA9quipement") : std::string{},
                         ui::Icon::None, canAttach, false, kMenuBase - MAttach});
        if (indexed && !v->internal.empty())
            items.push_back({"Attribuer dans toutes les cases", every, members.empty() ? none : std::string{}, ui::Icon::None, !members.empty(), false,
                             kMenuBase - MAttachAll});
        items.push_back({v->compact ? "Place d'origine (membres internes compris)" : "Recalculer la place m\xC3\xA9moire",
                         ty::spanText(doc_->project, *v), v->internal.empty() && !v->compact ? std::string("aucun membre interne : rien \xC3\xA0 rendre") : std::string{},
                         ui::Icon::None, !v->internal.empty() || v->compact, false, kMenuBase - (v->compact ? MRestore : MRecalc)});
    }
    // ---- 1.11.16 : la remanence d'exploitation ----
    {
        items.push_back({"", "", "", ui::Icon::None, true, true, -1});
        const auto* v = doc_->project.variableById(selectedVariable());
        const bool hasFile = !retainFile().empty();
        const std::string noFile = "projet jamais enregistr\xC3\xA9 : pas de stockage";
        const auto* store = hasFile ? retainStore() : nullptr;
        const bool saved = v && store && std::any_of(store->entries.begin(), store->entries.end(), [v](const hmi::retain::Entry& e) { return e.cell.variable == v->id; });
        items.push_back({"R\xC3\xA9initialiser cette variable r\xC3\xA9manente", v ? v->name : std::string{},
                         !hasFile ? noFile : !v ? std::string("choisis une variable") : !saved ? std::string("aucune valeur gard\xC3\xA9" "e pour elle") : std::string{},
                         ui::Icon::None, hasFile && saved, false, kMenuBase - MRetainReset});
        const std::size_t kept = store ? store->entries.size() : 0;
        items.push_back({"R\xC3\xA9initialiser toutes les variables r\xC3\xA9manentes", std::to_string(kept) + " valeur(s)",
                         !hasFile ? noFile : kept == 0 && retainNote_.empty() ? std::string("aucune valeur gard\xC3\xA9" "e") : std::string{}, ui::Icon::None,
                         hasFile && (kept > 0 || !retainNote_.empty()), false, kMenuBase - MRetainResetAll});
        items.push_back({"Exporter les valeurs r\xC3\xA9manentes\xE2\x80\xA6", "", !hasFile ? noFile : !store ? std::string("aucune valeur gard\xC3\xA9" "e") : std::string{},
                         ui::Icon::None, hasFile && store, false, kMenuBase - MRetainExport});
        items.push_back({"Importer les valeurs r\xC3\xA9manentes\xE2\x80\xA6", "", hasFile ? std::string{} : noFile, ui::Icon::None, hasFile, false,
                         kMenuBase - MRetainImport});
        items.push_back({"Afficher l'emplacement du stockage", "", hasFile ? std::string{} : noFile, ui::Icon::None, hasFile, false, kMenuBase - MRetainWhere});
        items.push_back({"V\xC3\xA9rifier l'int\xC3\xA9grit\xC3\xA9 des donn\xC3\xA9" "es r\xC3\xA9manentes", "", hasFile ? std::string{} : noFile, ui::Icon::None, hasFile, false,
                         kMenuBase - MRetainCheck});
    }
    items.push_back({"", "", "", ui::Icon::None, true, true, -1});
    for (const auto& it : menu_->items()) items.push_back(it);
    menu_->setItems(std::move(items));
}

void HmiVariablesPane::runMenu(int action) {
    switch (action) {
        case MTrend: (void)openTrend(); break;
        case MCopyName: {
            std::string names;
            for (const auto r : table_->selectedModelRows())
                if (r < rows_.size() && rows_[r].kind != Row::Kind::More) names += (names.empty() ? "" : "\r\n") + rows_[r].key;
            ui::setClipboardText(names);
            say("Copi\xC3\xA9 : " + names);
            break;
        }
        case MRename:    tools_->triggered->emit(VRename); break;
        case MDuplicate: tools_->triggered->emit(VDuplicate); break;
        case MDelete:    tools_->triggered->emit(VDelete); break;
        // 1.11.8 : les membres internes, la place memoire.
        case MInternal: case MInternalAll: case MAttach: case MAttachAll: {
            std::string why;
            if (!setMembersInternal(selectedVariable(), selectedMemberPaths(), action == MInternal || action == MInternalAll,
                                    action == MInternalAll || action == MAttachAll, &why))
                say("Refus\xC3\xA9 : " + why, true);
            break;
        }
        case MRecalc: case MRestore: {
            std::string why;
            if (!recalculatePlace(selectedVariable(), action == MRecalc, &why)) say("Refus\xC3\xA9 : " + why, true);
            break;
        }
        // ---- 1.11.16 : la remanence d'exploitation ----
        case MRetainReset: case MRetainResetAll: {
            const Id id = action == MRetainReset ? selectedVariable() : kNoId;
            const auto* v = doc_->project.variableById(id);
            if (action == MRetainReset && !v) break;
            const auto go = [this, id] {
                std::string why;
                std::size_t n = 0;
                const auto* var = doc_->project.variableById(id);
                if (!resetRetained(id, &n, &why)) say("R\xC3\xA9initialiser : " + why, true);
                else say(std::to_string(n) + " valeur(s) oubli\xC3\xA9" "e(s)" + (var ? " pour " + var->name : std::string{})
                         + " : le poste reprendra la valeur initiale au prochain lancement.");
                rebuildProperties();
            };
            const std::string what = v ? "la valeur gard\xC3\xA9" "e de \xC2\xAB " + v->name + " \xC2\xBB" : std::string("toutes les valeurs gard\xC3\xA9" "es");
            if (hosts_.confirm)
                hosts_.confirm(action == MRetainReset ? "R\xC3\xA9initialiser la variable r\xC3\xA9manente ?" : "R\xC3\xA9initialiser toutes les variables r\xC3\xA9manentes ?",
                               "Le poste d'exploitation oubliera " + what + " et reprendra la valeur initiale \xC3\xA0 son prochain lancement. "
                               "Ce n'est pas annulable par Ctrl+Z (l'ancien stockage reste en copie .bak).", "R\xC3\xA9initialiser", go);
            else
                go();
            break;
        }
        case MRetainExport: askRetainExport(); break;
        case MRetainImport: askRetainImport(); break;
        case MRetainWhere: {
            const std::string file = retainFile();
            ui::setClipboardText(file);
            say("Stockage du poste : " + file + " (chemin copi\xC3\xA9)");
            if (hosts_.report) hosts_.report("Emplacement du stockage", file + "\n\nLe chemin est copi\xC3\xA9 dans le presse-papiers.", false);
            break;
        }
        case MRetainCheck: {
            const std::string text = retainIntegrity();
            say(text, text.find("ab\xC3\xAEm\xC3\xA9") != std::string::npos || text.find("manque") != std::string::npos);
            if (hosts_.report) hosts_.report("Int\xC3\xA9grit\xC3\xA9 des donn\xC3\xA9" "es r\xC3\xA9manentes", text, text.find("ab\xC3\xAEm\xC3\xA9") != std::string::npos);
            break;
        }
        default: break;
    }
}
// ---- fin 1.10 ----

// =============================================================================
//  Types IHM
// =============================================================================
HmiTypesPane::HmiTypesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TAdd, HmiGlyph::Plus, "Nouveau type IHM : une structure (comme un DDT de l'automate) ou une \xC3\xA9num\xC3\xA9ration (T_MODE : Arret, Auto, Manu...)",
               "Nouveau type \xE2\x96\xBE");   // 1.10 (chantier U) : le menu Structure / Enumeration
    tools->add(TMember, HmiGlyph::Plus, "Nouveau membre du type choisi", "Membre");
    tools->add(TUp, HmiGlyph::Up, "Monter le membre (sa place Modbus change)", "Monter");
    tools->add(TDown, HmiGlyph::Down, "Descendre le membre", "Descendre");
    tools->add(TRemoveMember, HmiGlyph::Delete, "Supprimer le membre", "Supprimer le membre");
    // 1.10 (chantier O) : les membres <-> Excel.
    tools->add(TCopy, HmiGlyph::Copy, "Copier les membres choisis (Ctrl+C) : un tableau pour Excel, avec ses titres", "Copier");
    tools->add(TPaste, HmiGlyph::Paste, "Coller un tableau d'Excel (Ctrl+V) : un aper\xC3\xA7u de ce qui est ajout\xC3\xA9, remplac\xC3\xA9 ou refus\xC3\xA9, "
                                        "puis Appliquer (Ctrl+Z l'annule)", "Coller");
    tools->separator();
    tools->add(TDuplicate, HmiGlyph::Duplicate, "Dupliquer le type", "Dupliquer");
    tools->add(TDelete, HmiGlyph::Delete, "Supprimer le type (refus\xC3\xA9 tant qu'une variable ou un type l'emploie), ou le dossier "
                                          "choisi (ses types remontent d'un cran)", "Supprimer le type");
    tools->add(TFolder, HmiGlyph::Plus, "Nouveau dossier de types (sans effet sur les noms) : glisse des types dessus", "Dossier");
    tools->separator();   // 1.11.2 (decision 174)
    tools->add(TExport, HmiGlyph::Export,
               "Exporter des types IHM (.xpgtypes : \xC3\xA9num\xC3\xA9rations et structures) avec les types de leurs membres",
               "Exporter les types\xE2\x80\xA6");
    tools->add(TImport, HmiGlyph::Import,
               "Importer des types IHM (ou tout fichier fait par Exporter) d'un autre projet : renommer ou remplacer chaque nom en conflit, "
               "un seul Ctrl+Z",
               "Importer\xE2\x80\xA6");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setVisibleWhen(TExport, [this] { return static_cast<bool>(hosts_.exportItems); });
    tools_->setVisibleWhen(TImport, [this] { return static_cast<bool>(hosts_.importAny); });
    tools_->setEnabledWhen(TMember, [this] { return selectedType() != kNoId; });
    tools_->setEnabledWhen(TUp, [this] { return selectedType() != kNoId && selectedMember() > 0; });
    tools_->setEnabledWhen(TDown, [this] {
        const auto* t = doc_->project.hmiType(selectedType());
        return t && selectedMember() >= 0 && selectedMember() + 1 < static_cast<int>(t->members.size());
    });
    tools_->setEnabledWhen(TRemoveMember, [this] { return selectedType() != kNoId && selectedMember() >= 0; });
    tools_->setEnabledWhen(TDuplicate, [this] { return selectedType() != kNoId; });
    tools_->setEnabledWhen(TCopy, [this] { return selectedType() != kNoId && selectedMember() >= 0; });
    tools_->setEnabledWhen(TPaste, [this] { return selectedType() != kNoId; });
    tools_->setEnabledWhen(TDelete, [this] { return selectedType() != kNoId || (folders_ && !folders_->selectedFolder().empty()); });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".typesPanel", "TYPES IHM");
        auto table = std::make_unique<ui::TableView>(base + ".types");
        table->setColumns({{"Nom", 150.f}, {"Membres", 80.f, 40.f, true, true, true, ui::Align::End}, {"Mots", 60.f, 40.f, true, true, true, ui::Align::End},
                           {"Utilis\xC3\xA9 par", 170.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        types_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        split->addPane(std::move(panel), 0.30f, 220.f);
        // Lot 21 : les types ranges en dossiers ; le nom d'un type se change dans sa case.
        folders_ = std::make_unique<HmiFolderTable>(*types_, doc_, apply_);
        folders_->setItemEditing([](Id, std::size_t c) { return c == 0; },
                                 [this](Id type, std::size_t c, const std::string& text) {
                                     if (c != 0) return false;
                                     std::string why;
                                     const bool ok = renameType(type, text, &why);
                                     if (!ok) say("Refus\xC3\xA9 : " + why, true);
                                     return ok;
                                 });
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".membersPanel", "MEMBRES");
        membersPanel_ = panel.get();                                        // 1.10 (chantier U)
        auto table = std::make_unique<MemberTable>(base + ".members");     // 1.10 : Ctrl+C, le clic droit
        table->setColumns({{"Membre", 150.f, 60.f, true, false}, {"Type", 200.f, 60.f, true, false}, {"Initiale", 90.f, 40.f, true, false},
                           {"D\xC3\xA9" "calage", 90.f, 40.f, true, false}, {"Taille", 80.f, 40.f, true, false}, {"Description", 240.f, 60.f, true, false}});
        table->setSelectionMode(ui::SelectionMode::Extended);           // 1.10 : plusieurs membres a copier
        table->onCopy = [this] {
            (void)copyMembers();
            return true;
        };
        table->onContextMenu = [this](gfx::Point at) { openMemberMenu(at); };
        table->setPasteHandler([this](const ui::TableView::PasteRequest& rq) { (void)pasteMembers(rq.text); });
        members_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        // 1.10 (chantier S2) : sous les membres, la section Operateurs du type.
        auto mid = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".mid");
        mid->addPane(std::move(panel), 0.5f, 120.f);
        operators_ = &static_cast<HmiOperatorsPane&>(mid->addPane(std::make_unique<HmiOperatorsPane>(base + ".operators", doc_, apply_), 0.5f, 160.f));
        split->addPane(std::move(mid), 0.46f, 320.f);
    }
    {
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(true);
        grid->setNameColumnRatio(0.42f);
        props_ = &static_cast<ui::PropertyGrid&>(split->addPane(std::move(grid), 0.24f, 220.f));
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    // 1.10 (chantier O) : l'apercu d'un collage, le menu du clic droit des membres.
    {
        auto preview = std::make_unique<PastePreview>(base + ".apercu");
        auto* pv = &static_cast<PastePreview&>(addChild(std::move(preview)));
        pv->setVisibility(ui::Visibility::Collapsed);
        links_ += pv->applyButton().clicked->connect([this] { (void)confirmPaste(); });
        links_ += pv->cancelButton().clicked->connect([this] { cancelPaste(); });
        preview_ = pv;
        // Le menu du clic droit est celui de la table des membres ; ses entrees :
        // kMenuBase - l'outil (Copier, Coller, Nouveau membre...).
        menu_ = members_->contextMenu();
        if (menu_)
            links_ += menu_->itemChosen->connect([this](int a) {
                if (a < kMenuBase && a > kMenuBase - 100) tools_->triggered->emit(kMenuBase - a);
            });
    }

    // 1.10 (chantier U) : les valeurs d'une enumeration (a la place des membres), la
    // fenetre des valeurs, la fenetre << Nouvelle enumeration >>, le menu Nouveau type.
    {
        enums_ = &static_cast<HmiEnumValuesPane&>(addChild(std::make_unique<HmiEnumValuesPane>(base + ".valeurs", doc_, apply_)));
        enums_->setVisibility(ui::Visibility::Collapsed);
        links_ += enums_->message->connect([this](const std::string& text) { say(text); });
        enumDialog_ = &static_cast<HmiEnumDialog&>(addChild(std::make_unique<HmiEnumDialog>(base + ".valeurs.fenetre")));
        enums_->setDialogWidget(enumDialog_);
        create_ = &static_cast<HmiEnumCreatePanel&>(addChild(std::make_unique<HmiEnumCreatePanel>(base + ".nouvelleEnum")));
        links_ += create_->created->connect([this] {
            std::string why;
            if (createEnumerationFromPanel(&why) == kNoId) say("Refus\xC3\xA9 : " + why, true);
        });
        newMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".nouveauType")));
        links_ += newMenu_->itemChosen->connect([this](int a) {
            if (a == 1) tools_->triggered->emit(kMenuBase);           // Structure... : comme l'ancien + Type
            else if (a == 2) openEnumCreation();
        });
    }

    links_ += tools_->triggered->connect([this](int a) {
        const Id t = selectedType();
        std::string why;
        switch (a) {
            case TCopy: (void)copyMembers(); break;
            case TPaste: (void)pasteMembers(ui::clipboardText()); break;
            case TAdd: openNewTypeMenu(); break;                      // 1.10 (chantier U)
            case kMenuBase: {                                         // une structure
                std::string name = "T_Type";
                for (int i = 2; doc_->project.hmiTypeByName(name) && i < 1000; ++i) name = "T_Type" + std::to_string(i);
                const Id made = addType(name, &why);
                if (made != kNoId) {
                    selectType(made);
                    if (const int row = folders_->rowOfItem(made); row >= 0) (void)types_->beginCellEdit(static_cast<ui::RowIndex>(row), 0);
                }
                break;
            }
            case TFolder: (void)folders_->newFolder(); break;
            case TExport: if (hosts_.exportItems) hosts_.exportItems(t); break;   // 1.11.2 (decision 174)
            case TImport: if (hosts_.importAny) hosts_.importAny(); break;
            case TDelete: {
                if (t == kNoId) {
                    if (const auto f = folders_->selectedFolder(); !f.empty()) (void)folders_->deleteFolder(f);
                    break;
                }
                if (!deleteType(t, &why)) say("Refus\xC3\xA9 : " + why, true);
                break;
            }
            case TMember: {
                const auto* ty = doc_->project.hmiType(t);
                if (!ty) break;
                std::string name = "Membre";
                const auto taken = [&](const std::string& n) {
                    return std::any_of(ty->members.begin(), ty->members.end(), [&](const hmi::TypeMember& m) { return sameText(m.name, n); });
                };
                for (int i = 2; taken(name) && i < 1000; ++i) name = "Membre" + std::to_string(i);
                if (addMember(t, name, "INT", &why)) {
                    selectMember(static_cast<int>(doc_->project.hmiType(t)->members.size()) - 1);
                    (void)members_->beginCellEdit(static_cast<ui::RowIndex>(member_), 0);
                } else {
                    say("Refus\xC3\xA9 : " + why, true);
                }
                break;
            }
            case TUp: (void)moveMember(t, static_cast<std::size_t>(std::max(0, selectedMember())), -1); break;
            case TDown: (void)moveMember(t, static_cast<std::size_t>(std::max(0, selectedMember())), 1); break;
            case TRemoveMember: (void)removeMember(t, static_cast<std::size_t>(std::max(0, selectedMember()))); break;
            case TDuplicate: (void)duplicateType(t); break;
            default: break;
        }
    });
    links_ += types_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (syncing_) return;
        current_ = folders_->selectedItem();                  // lot 21 : une ligne de dossier ne choisit aucun type
        member_ = -1;
        refreshMembers();
        rebuildProperties();
    });
    links_ += members_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        member_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        rebuildProperties();
    });
    links_ += folders_->message->connect([this](const std::string& text) { say(text); });
    links_ += folders_->relayout->connect([this] { refresh(); });
    links_ += doc_->changed->connect([this](Id) {
        refresh();
        // 1.10 : un apercu en attente se refait sur le projet du moment.
        if (pending_) {
            pending_ = planMemberPaste(pending_->type, pendingText_);
            showPreview();
        }
    });
    refresh();
}

void HmiTypesPane::say(std::string text, bool warning) {
    message_ = std::move(text);
    status_->setMessage(message_, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
}

void HmiTypesPane::refresh() {
    const auto& p = doc_->project;
    if (!p.hmiType(current_)) current_ = p.programs.types.empty() ? kNoId : p.programs.types.front().id;
    typeOrder_.clear();
    std::map<Id, std::vector<std::string>> cells;
    std::map<Id, bool> broken;
    for (const auto& t : p.programs.types) {
        typeOrder_.push_back(t.id);
        const auto users = ty::usersOf(p, t.name);
        std::string used;
        for (std::size_t i = 0; i < users.size() && i < 3; ++i) used += (used.empty() ? "" : ", ") + users[i];
        if (users.size() > 3) used += " +" + std::to_string(users.size() - 3);
        if (hmi::isEnumeration(t)) {   // 1.10 (chantier U) : une enumeration, ses valeurs, un DINT (2 mots)
            broken[t.id] = !hmi::enumIssues(t).empty();
            cells[t.id] = {t.name, std::to_string(t.values.size()) + " val.", "2", used.empty() ? std::string("(aucune variable)") : used};
            continue;
        }
        const bool cycle = !ty::cycleOf(p, t.name).empty();
        broken[t.id] = cycle;
        cells[t.id] = {t.name, std::to_string(t.members.size()), cycle ? std::string("?") : std::to_string(ty::weightOf(p, t.name).words),
                       used.empty() ? std::string("(aucune variable)") : used};
    }
    syncing_ = true;
    // Lot 21 : la liste rangee en dossiers.
    folders_->rebuild(hmi::fold::List::Types, typeOrder_, {"Nom", "Membres", "Mots", "Utilis\xC3\xA9 par"},
                      [&cells](Id id) { return cells[id]; },
                      [&broken](Id id, std::size_t c) {
                          ui::CellStyle st;
                          if (c == 0) {
                              st.bold = true;
                              st.icon = ui::Icon::DerivedType;
                              st.iconTone = broken[id] ? ui::Tone::Error : ui::Tone::Accent;
                          }
                          if (c == 3) st.fgTone = ui::Tone::Muted;
                          return st;
                      });
    if (current_ != kNoId && folders_->selectedItem() != current_) folders_->selectItem(current_);
    syncing_ = false;
    refreshMembers();
    rebuildProperties();
    status_->setMessage(message_.empty() ? std::to_string(p.programs.types.size()) + " type(s) IHM : des structures, comme les DDT de l'automate. "
                                                                                   "Une variable de ce type a tous ses membres (Four1.Temperature)."
                                         : message_);
}

void HmiTypesPane::refreshMembers() {
    if (operators_)   // 1.10 (chantier S2) : les operateurs du type choisi
        if (const auto* t = doc_->project.hmiType(current_)) operators_->setOwner(hmi::ownerOfType(*t));
    const auto& p = doc_->project;
    const auto* t = p.hmiType(current_);
    // 1.10 (chantier U) : une enumeration montre ses valeurs a la place des membres.
    if (enums_) {
        const bool en = t && hmi::isEnumeration(*t);
        if (en) enums_->setType(t->id);
        else if (enums_->dialog() != HmiEnumValuesPane::Dialog::None) enums_->closeDialog();
        enums_->setVisibility(en ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (membersPanel_) membersPanel_->setVisibility(en ? ui::Visibility::Hidden : ui::Visibility::Visible);
        invalidateLayout();
        if (en) t = nullptr;   // pas de membres
    }
    std::vector<std::vector<std::string>> rows;
    if (t) {
        // Le decalage et la taille : la place Modbus de chaque membre dans la structure.
        const auto flat = ty::flatten(p, "x", t->name);
        for (const auto& m : t->members) {
            std::string offset = "?", size = "?";
            if (flat.ok()) {
                const std::string path = "x." + m.name;
                long long first = -1, words = 0;
                int bit = -1;
                for (const auto& l : flat.leaves)
                    if (l.path == path || l.path.rfind(path + ".", 0) == 0 || l.path.rfind(path + "[", 0) == 0) {
                        if (first < 0) {
                            first = l.word;
                            bit = l.bit;
                        }
                        words += l.words;
                    }
                if (first >= 0) {
                    offset = "mot " + std::to_string(first) + (bit >= 0 ? ", bit " + std::to_string(bit) : std::string{});
                    size = ty::isComposite(m.type) ? std::to_string(ty::weightOf(p, m.type).words) + " mots"
                         : bit >= 0 ? std::string("1 bit") : std::to_string(ty::wordsOf(m.type)) + (ty::wordsOf(m.type) > 1 ? " mots" : " mot");
                }
            }
            rows.push_back({m.name, m.type, m.initial, offset, size, m.description});
        }
    }
    const Id tid = current_;
    memberModel_ = std::make_shared<Rows>(
        std::vector<std::string>{"Membre", "Type", "Initiale", "D\xC3\xA9" "calage", "Taille", "Description"}, std::move(rows),
        [](ui::RowIndex, std::size_t c) {
            ui::CellStyle st;
            if (c == 0) st.bold = true;
            if (c == 3 || c == 4) st.fgTone = ui::Tone::Muted;
            return st;
        },
        [](std::size_t, std::size_t c) { return c == 0 || c == 1 || c == 2 || c == 5; },
        [this, tid](std::size_t, std::size_t c) {
            if (c != 1) return std::vector<std::string>{};
            const auto* t2 = doc_->project.hmiType(tid);
            return typeChoices(doc_->project, t2 ? t2->name : std::string_view{}, /*members*/ true);   // 1.12.2 : une place fixe
        },
        [this](std::size_t r, std::size_t c, const std::string& text) { return commitMemberCell(r, c, text); });
    syncing_ = true;
    members_->setModel(memberModel_);
    if (t && member_ >= 0 && member_ < static_cast<int>(t->members.size())) members_->selectModelRows({static_cast<ui::RowIndex>(member_)}, false);
    else member_ = -1;
    syncing_ = false;
}

bool HmiTypesPane::commitMemberCell(std::size_t row, std::size_t col, const std::string& text) {
    const auto* t = doc_->project.hmiType(current_);
    if (!t || row >= t->members.size()) return false;
    hmi::TypeMember m = t->members[row];
    const Id tid = t->id;
    if (col == 1 && text == kPickType) {
        // 1.11.19 (lot 6) : le selecteur de types (un membre : les types d'une variable IHM).
        app::HmiTypePicker::Spec spec;
        spec.field = "Type de " + t->name + "." + m.name;
        spec.current = m.type;
        spec.use = hmi::typereg::UseVariable;
        spec.fixedOnly = true;                    // 1.12.2 : un membre a une place fixe
        spec.doc = doc_;
        const std::size_t index = row;
        const std::weak_ptr<int> alive = alive_;
        app::typepicker::ask(std::move(spec), [this, tid, index, alive](const app::HmiTypePicker::Answer& a) {
            if (alive.expired() || a.type.empty()) return;
            const auto* t2 = doc_->project.hmiType(tid);
            if (!t2 || index >= t2->members.size()) return;
            hmi::TypeMember m2 = t2->members[index];
            m2.type = a.type;
            std::string why;
            if (!setMember(tid, index, m2, &why)) say("Refus\xC3\xA9 : " + why, true);
        });
        return false;
    }
    if (col == 1 && text == kArray) {
        const std::size_t index = row;
        if (hosts_.arrayType) {
            hosts_.arrayType(m.type, [this, tid, index](const std::string& type) {
                const auto* t2 = doc_->project.hmiType(tid);
                if (!t2 || index >= t2->members.size()) return;
                hmi::TypeMember m2 = t2->members[index];
                m2.type = type;
                std::string why;
                if (!setMember(tid, index, m2, &why)) say("Refus\xC3\xA9 : " + why, true);
            });
            return true;
        }
        ty::Spec s;
        m.type = "ARRAY[0..9] OF " + (ty::parseSpec(m.type, s) ? s.element : std::string("INT"));
    } else if (col == 0) m.name = trimmed(text);
    else if (col == 1) m.type = ty::normalized(text);
    else if (col == 2) m.initial = trimmed(text);
    else if (col == 5) m.description = text;
    else return false;
    std::string why;
    const bool ok = setMember(tid, row, m, &why);
    if (!ok) say("Refus\xC3\xA9 : " + why, true);
    return ok;
}

Id HmiTypesPane::selectedType() const { return doc_->project.hmiType(current_) ? current_ : kNoId; }
int HmiTypesPane::selectedMember() const { return member_; }

void HmiTypesPane::selectType(Id id) {
    current_ = id;
    member_ = -1;
    refresh();
}

void HmiTypesPane::selectMember(int index) {
    member_ = index;
    refreshMembers();
    rebuildProperties();
}

bool HmiTypesPane::change(Id id, std::string label, const std::function<bool(hmi::HmiType&, std::string*)>& edit, std::string* why) {
    const auto* t = doc_->project.hmiType(id);
    if (!t) return fail(why, "type introuvable");
    // Essaye sur une copie : un type refuse (cycle, membre en double) ne passe pas.
    hmi::Project trial = doc_->project;
    auto* tt = trial.hmiType(id);
    if (!edit(*tt, why)) return false;
    if (const auto cycle = ty::cycleOf(trial, tt->name); !cycle.empty()) return fail(why, "le type se contiendrait lui-m\xC3\xAAme : " + cycle);
    for (const auto& m : tt->members)
        if (!ty::validMemberType(trial, m.type, why)) return false;
    bool ok = true;
    apply_(hmi::changeProject(doc_, std::move(label), [&](hmi::Project& p) {
        if (auto* x = p.hmiType(id)) ok = edit(*x, nullptr);
    }));
    return ok;
}

Id HmiTypesPane::addType(const std::string& raw, std::string* why) {
    const std::string name = trimmed(raw);
    std::string reason;
    if (!hmi::isIdentifier(name)) reason = "nom invalide : lettres, chiffres et _";
    else if (ty::isElementary(name)) reason = name + " est un type \xC3\xA9l\xC3\xA9mentaire";
    else if (doc_->project.hmiTypeByName(name)) reason = "le type " + name + " existe d\xC3\xA9j\xC3\xA0";
    if (!reason.empty()) {
        if (why) *why = reason;
        return kNoId;
    }
    Id made = kNoId;
    apply_(hmi::changeProject(doc_, "Nouveau type " + name, [&](hmi::Project& p) {
        hmi::HmiType t;
        t.id = p.allocate();
        t.name = name;
        t.members = {{"Valeur", "REAL"}, {"Marche", "BOOL"}};
        made = t.id;
        p.programs.types.push_back(std::move(t));
    }));
    current_ = made;
    member_ = -1;
    refresh();
    say("Type IHM cr\xC3\xA9\xC3\xA9 : " + name + " (deux membres pour commencer : Valeur, Marche)");
    return made;
}

bool HmiTypesPane::renameType(Id id, const std::string& raw, std::string* why) {
    const auto* t = doc_->project.hmiType(id);
    if (!t) return fail(why, "type introuvable");
    const std::string name = trimmed(raw);
    if (!hmi::isIdentifier(name)) return fail(why, "nom invalide : lettres, chiffres et _");
    if (ty::isElementary(name)) return fail(why, name + " est un type \xC3\xA9l\xC3\xA9mentaire");
    if (const auto* o = doc_->project.hmiTypeByName(name); o && o->id != id) return fail(why, "le type " + name + " existe d\xC3\xA9j\xC3\xA0");
    if (t->name == name) return true;
    const std::string before = t->name;
    // Les variables et les membres qui l'emploient suivent.
    const auto rename = [&](std::string& type) {
        ty::Spec s;
        if (!ty::parseSpec(type, s) || !sameText(s.element, before)) return;
        s.element = name;
        type = ty::specText(s);
    };
    apply_(hmi::changeProject(doc_, "Renommer le type " + before + " en " + name, [&](hmi::Project& p) {
        for (auto& x : p.programs.types) {
            if (x.id == id) x.name = name;
            for (auto& m : x.members) rename(m.type);
        }
        for (auto& v : p.programs.variables) rename(v.type);
        // 1.11.18 (refonte, lot 3) : les declarations du modele des scripts et des fonctions aussi.
        hmi::forEachDeclarations(p, [&](std::vector<hmi::Declaration>& list) { for (auto& d : list) rename(d.type); });
        (void)hmi::renameTypeInOperators(p, before, name);   // 1.10 (chantier S2) : les operateurs suivent
        (void)hmi::renameEnumType(p, before, name);          // 1.10 (chantier E) : T_ANCIEN#x -> T_NOUVEAU#x
    }));
    say("Type renomm\xC3\xA9 : " + before + " \xE2\x86\x92 " + name + " (les variables et les membres qui l'emploient suivent)");
    return true;
}

bool HmiTypesPane::setTypeDescription(Id id, const std::string& text) {
    return change(id, "Description du type", [&](hmi::HmiType& t, std::string*) {
        t.description = text;
        return true;
    }, nullptr);
}

bool HmiTypesPane::deleteType(Id id, std::string* why) {
    const auto* t = doc_->project.hmiType(id);
    if (!t) return fail(why, "type introuvable");
    const auto users = ty::usersOf(doc_->project, t->name);
    if (!users.empty()) {
        std::string list;
        for (std::size_t i = 0; i < users.size() && i < 4; ++i) list += (list.empty() ? "" : ", ") + users[i];
        return fail(why, t->name + " est employ\xC3\xA9 par " + list + (users.size() > 4 ? "..." : ""));
    }
    const std::string name = t->name;
    apply_(hmi::changeProject(doc_, "Supprimer le type " + name, [&](hmi::Project& p) {
        auto& all = p.programs.types;
        all.erase(std::remove_if(all.begin(), all.end(), [&](const hmi::HmiType& x) { return x.id == id; }), all.end());
    }));
    current_ = kNoId;
    refresh();
    say("Type supprim\xC3\xA9 : " + name + " - Ctrl+Z le rend");
    return true;
}

Id HmiTypesPane::duplicateType(Id id) {
    const auto* t = doc_->project.hmiType(id);
    if (!t) return kNoId;
    std::string name = t->name + "_copie";
    for (int i = 2; doc_->project.hmiTypeByName(name) && i < 1000; ++i) name = t->name + "_copie" + std::to_string(i);
    const hmi::HmiType copy = *t;
    Id made = kNoId;
    apply_(hmi::changeProject(doc_, "Dupliquer le type " + copy.name, [&](hmi::Project& p) {
        hmi::HmiType x = copy;
        x.id = p.allocate();
        x.name = name;
        hmi::copyOperators(p, x.operators, copy.name, name);   // 1.10 (chantier S2)
        hmi::copyEnumerationLiterals(x, copy.name);           // 1.10 (chantier E) : <type>#x -> <copie>#x
        made = x.id;
        p.programs.types.push_back(std::move(x));
    }));
    selectType(made);
    return made;
}

bool HmiTypesPane::addMember(Id id, const std::string& raw, const std::string& type, std::string* why) {
    const std::string name = trimmed(raw);
    if (!hmi::isIdentifier(name)) return fail(why, "nom de membre invalide : lettres, chiffres et _");
    return change(id, "Nouveau membre " + name, [&](hmi::HmiType& t, std::string* w) {
        if (std::any_of(t.members.begin(), t.members.end(), [&](const hmi::TypeMember& m) { return sameText(m.name, name); }))
            return fail(w, "le membre " + name + " existe d\xC3\xA9j\xC3\xA0");
        t.members.push_back({name, ty::normalized(type)});
        return true;
    }, why);
}

bool HmiTypesPane::setMember(Id id, std::size_t index, const hmi::TypeMember& m, std::string* why) {
    if (!hmi::isIdentifier(m.name)) return fail(why, "nom de membre invalide : lettres, chiffres et _");
    if (!m.initial.empty()) {
        const auto e = hmi::Expression::compile(m.initial);
        if (!e.valid()) return fail(why, "valeur initiale illisible : " + e.error());
    }
    return change(id, "Membre " + m.name, [&](hmi::HmiType& t, std::string* w) {
        if (index >= t.members.size()) return fail(w, "membre introuvable");
        for (std::size_t i = 0; i < t.members.size(); ++i)
            if (i != index && sameText(t.members[i].name, m.name)) return fail(w, "le membre " + m.name + " existe d\xC3\xA9j\xC3\xA0");
        if (!ty::validMemberType(doc_->project, m.type, w)) return false;
        t.members[index] = m;
        return true;
    }, why);
}

bool HmiTypesPane::removeMember(Id id, std::size_t index) {
    const bool ok = change(id, "Supprimer un membre", [&](hmi::HmiType& t, std::string*) {
        if (index >= t.members.size()) return false;
        t.members.erase(t.members.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }, nullptr);
    if (ok) member_ = -1;
    refresh();
    return ok;
}

bool HmiTypesPane::moveMember(Id id, std::size_t index, int delta) {
    const bool ok = change(id, "D\xC3\xA9placer un membre", [&](hmi::HmiType& t, std::string*) {
        const long long to = static_cast<long long>(index) + delta;
        if (index >= t.members.size() || to < 0 || to >= static_cast<long long>(t.members.size())) return false;
        std::swap(t.members[index], t.members[static_cast<std::size_t>(to)]);
        return true;
    }, nullptr);
    if (ok) member_ = static_cast<int>(index) + delta;
    refresh();
    return ok;
}

void HmiTypesPane::rebuildProperties() {
    const auto& p = doc_->project;
    std::vector<PG::Category> cats;
    const auto* t = p.hmiType(current_);
    if (!t) {
        PG::Category c;
        c.name = "Types IHM";
        c.properties.push_back(prop("Types", std::to_string(p.programs.types.size()), PG::ValueType::ReadOnly, {}, {},
                                    "+ Type : une structure (Four : Temperature, Consigne, Vannes...). Une variable de ce type a tous ses membres."));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const Id id = t->id;
    if (hmi::isEnumeration(*t)) {   // 1.10 (chantier U) : la fiche d'une enumeration
        PG::Category c;
        c.name = "\xC3\x89num\xC3\xA9ration";
        c.properties.push_back(prop("Nom", t->name, PG::ValueType::Text, [this, id](std::string_view text) {
            std::string why;
            const bool ok = renameType(id, std::string(text), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, {}, "Renommer : les variables et les litt\xC3\xA9raux T_ANCIEN#x des scripts suivent."));
        c.properties.push_back(prop("Description", t->description, PG::ValueType::Text,
                                    [this, id](std::string_view text) { return setTypeDescription(id, std::string(text)); }));
        c.properties.push_back(prop("Genre", "\xC3\xA9num\xC3\xA9ration \xC2\xB7 DINT", PG::ValueType::ReadOnly, {}, {},
                                    "Une variable de ce type vaut un DINT : le nombre de sa valeur. Dans les scripts : " + t->name + "#Valeur, CASE, FOR EACH v IN "
                                        + t->name + "."));
        c.properties.push_back(prop("Valeurs", std::to_string(t->values.size())));
        const auto issues = hmi::enumIssues(*t);
        c.properties.push_back(prop("Contr\xC3\xB4les", issues.empty() ? std::string("valeurs valides") : issues.front().message));
        cats.push_back(std::move(c));
        // Les variables de ce type : la valeur initiale par la liste des valeurs.
        PG::Category vars;
        vars.name = "Variables IHM de type " + t->name;
        std::vector<std::string> choices;
        for (const auto& x : t->values) choices.push_back(hmi::enumDisplay(*t, x.value));
        for (const auto& v : p.programs.variables) {
            if (!sameText(v.type, t->name)) continue;
            const Id vid = v.id;
            vars.properties.push_back(prop(v.name, enumInitialText(*t, v.initial), PG::ValueType::Enum, [this, vid, id](std::string_view text) {
                const auto* t2 = doc_->project.hmiType(id);
                const auto* v2 = doc_->project.variableById(vid);
                std::int64_t n = 0;
                if (!t2 || !v2 || !hmi::enumNumberOf(*t2, text, n)) return false;
                const auto* x = hmi::enumValueByNumber(*t2, n);
                if (!x || v2->initial == x->name) return x != nullptr;
                const std::string name = x->name;
                apply_(hmi::changeProject(doc_, "Valeur initiale de " + v2->name + " : " + t2->name + "#" + name, [&](hmi::Project& pr) {
                    for (auto& w : pr.programs.variables)
                        if (w.id == vid) w.initial = name;
                }));
                return true;
            }, choices, "La valeur initiale (Ctrl+Z l'annule)."));
        }
        if (vars.properties.empty()) vars.properties.push_back(prop("Aucune", "aucune variable de ce type"));
        cats.push_back(std::move(vars));
        // Les emplois des valeurs dans les scripts.
        PG::Category uses;
        uses.name = "Emplois de " + t->name;
        std::size_t total = 0;
        for (const auto& x : t->values) {
            const auto u = enumValueUses(p, t->name, x.name);
            total += u.size();
            if (!u.empty()) uses.properties.push_back(prop(t->name + "#" + x.name, std::to_string(u.size()) + " emploi(s) : " + u.front().where));
        }
        if (total == 0) uses.properties.push_back(prop("Scripts", "aucun emploi"));
        cats.push_back(std::move(uses));
        props_->setCategories(std::move(cats));
        return;
    }
    {
        PG::Category c;
        c.name = "Type";
        c.properties.push_back(prop("Nom", t->name, PG::ValueType::Text, [this, id](std::string_view text) {
            std::string why;
            const bool ok = renameType(id, std::string(text), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }, {}, "Renommer un type : les variables et les membres qui l'emploient suivent."));
        c.properties.push_back(prop("Description", t->description, PG::ValueType::Text,
                                    [this, id](std::string_view text) { return setTypeDescription(id, std::string(text)); }));
        const auto w = ty::weightOf(p, t->name);
        c.properties.push_back(prop("Poids", std::to_string(w.words) + " mots, " + std::to_string(w.bytes) + " octets", PG::ValueType::ReadOnly, {}, {},
                                    "La place Modbus d'une variable de ce type (.Words, .Bytes dans les scripts)."));
        const auto users = ty::usersOf(p, t->name);
        std::string used;
        for (const auto& u : users) used += (used.empty() ? "" : ", ") + u;
        c.properties.push_back(prop("Utilis\xC3\xA9 par", used.empty() ? std::string("aucune variable") : used));
        cats.push_back(std::move(c));
    }
    if (member_ >= 0 && member_ < static_cast<int>(t->members.size())) {
        PG::Category c;
        c.name = "Membre choisi";
        const std::size_t index = static_cast<std::size_t>(member_);
        const hmi::TypeMember m = t->members[index];
        const auto edit = [this, id, index](std::function<void(hmi::TypeMember&)> fn) {
            return [this, id, index, fn](std::string_view text) {
                const auto* t2 = doc_->project.hmiType(id);
                if (!t2 || index >= t2->members.size()) return false;
                hmi::TypeMember m2 = t2->members[index];
                (void)text;
                fn(m2);
                std::string why;
                const bool ok = setMember(id, index, m2, &why);
                if (!ok) say("Refus\xC3\xA9 : " + why, true);
                return ok;
            };
        };
        c.properties.push_back(prop("Nom", m.name, PG::ValueType::Text, [edit](std::string_view text) {
            const std::string s(text);
            return edit([s](hmi::TypeMember& x) { x.name = s; })(text);
        }));
        c.properties.push_back(prop("Type", m.type, PG::ValueType::Text, [edit](std::string_view text) {
            const std::string s = ty::normalized(text);
            return edit([s](hmi::TypeMember& x) { x.type = s; })(text);
        }, {}, "INT, REAL, BOOL, STRING... ; un autre type IHM (T_Vanne) ; un tableau : ARRAY[1..3] OF T_Vanne."));
        c.properties.push_back(prop("Initiale", m.initial, PG::ValueType::Text, [edit](std::string_view text) {
            const std::string s(text);
            return edit([s](hmi::TypeMember& x) { x.initial = s; })(text);
        }, {}, "La valeur de d\xC3\xA9part de ce membre dans chaque variable du type (vide : 0, FALSE, '')."));
        c.properties.push_back(prop("Description", m.description, PG::ValueType::Text, [edit](std::string_view text) {
            const std::string s(text);
            return edit([s](hmi::TypeMember& x) { x.description = s; })(text);
        }));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "V\xC3\xA9rifications";
        const auto cycle = ty::cycleOf(p, t->name);
        c.properties.push_back(prop("Imbrication", cycle.empty() ? std::string("sans boucle") : "boucle : " + cycle));
        std::size_t props = 0;
        for (const auto& m : t->members) props += ty::property(m.name) ? 1 : 0;
        c.properties.push_back(prop("Noms", props ? std::to_string(props) + " membre(s) portent le nom d'une propri\xC3\xA9t\xC3\xA9 (le membre passe avant)"
                                                  : std::string("aucun conflit avec les propri\xC3\xA9t\xC3\xA9s")));
        cats.push_back(std::move(c));
    }
    props_->setCategories(std::move(cats));
}

// ---- 1.10 (chantier O) : les membres <-> Excel ---------------------------------------
namespace {

// Le type connu le plus proche d'un type inconnu (BOOOL -> BOOL, REEL -> REAL) :
// les types elementaires et ceux du projet ; vide si rien n'est assez proche.
std::string nearestType(const hmi::Project& p, std::string_view typed) {
    const std::string u = upper(trimmed(typed));
    if (u.empty()) return {};
    const auto dist = [](const std::string& a, const std::string& b) {
        std::vector<std::size_t> row(b.size() + 1);
        for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            std::size_t diag = row[0];
            row[0] = i;
            for (std::size_t j = 1; j <= b.size(); ++j) {
                const std::size_t up = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0u : 1u)});
                diag = up;
            }
        }
        return row[b.size()];
    };
    std::vector<std::string> known = {"BOOL", "BYTE", "WORD", "DWORD", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT",
                                      "REAL", "LREAL", "STRING", "TIME", "DATE", "TOD", "DT"};
    for (const auto& t : p.programs.types) known.push_back(upper(t.name));
    std::string best;
    std::size_t bestD = std::max<std::size_t>(2, u.size() / 3) + 1;
    for (const auto& k : known)
        if (const auto d = dist(u, k); d < bestD) {
            bestD = d;
            best = k;
        }
    for (const auto& t : p.programs.types)
        if (upper(t.name) == best) return t.name;            // le nom du type tel qu'il s'ecrit
    return best;
}

} // namespace

std::string HmiTypesPane::MemberPaste::summary() const {
    if (!error.empty()) return error;
    const auto part = [](std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); };
    return part(added.size(), "membre ajout\xC3\xA9", "membres ajout\xC3\xA9s") + ", " + part(replaced.size(), "remplac\xC3\xA9", "remplac\xC3\xA9s")
         + ", " + part(refused.size(), "refus\xC3\xA9", "refus\xC3\xA9s");
}

HmiTypesPane::MemberPaste HmiTypesPane::planMemberPaste(Id typeId, std::string_view text) const {
    MemberPaste plan;
    plan.type = typeId;
    const auto& p = doc_->project;
    const auto* t = p.hmiType(typeId);
    if (!t) {
        plan.error = "Choisis d'abord un type.";
        return plan;
    }
    plan.result = t->members;
    const auto grid = paste::parseGrid(text);
    if (grid.empty()) {
        plan.error = "Le presse-papiers ne contient pas de tableau.";
        return plan;
    }
    // Les colonnes : par leurs titres (une colonne Nom ou Membre), sinon par leur place.
    enum Field : int { FUnknown = -1, FName, FType, FInitial, FDescription, FComputed };
    const auto fieldOf = [](const std::string& title) -> int {
        const auto n = paste::normalizedTitle(title);
        if (n == "nom" || n == "membre" || n == "name" || n == "member" || n == "champ" || n == "field") return FName;
        if (n == "type" || n == "typededonnee" || n == "datatype") return FType;
        if (n == "valeurinitiale" || n == "initiale" || n == "initial" || n == "init" || n == "initialvalue" || n == "valeurpardefaut" || n == "defaut")
            return FInitial;
        if (n == "description" || n == "commentaire" || n == "comment" || n == "libelle") return FDescription;
        if (n == "decalage" || n == "taille" || n == "offset" || n == "size") return FComputed;
        return FUnknown;
    };
    std::vector<int> cols;
    std::size_t first = 0;
    {
        std::vector<int> byTitle;
        bool titles = false;
        for (const auto& cell : grid.front()) {
            byTitle.push_back(fieldOf(cell));
            if (byTitle.back() == FName) titles = true;
        }
        if (titles) {
            plan.titles = true;
            cols = byTitle;
            first = 1;
            for (std::size_t i = 0; i < byTitle.size(); ++i)
                if (byTitle[i] == FUnknown && !trimmed(grid.front()[i]).empty()) plan.ignored.push_back(trimmed(grid.front()[i]));
        } else {
            cols = {FName, FType, FInitial, FDescription};
        }
    }
    hmi::Project trial = p;                    // chaque ligne se controle sur une copie
    auto* tt = trial.hmiType(typeId);
    std::set<std::string> seen;                // les noms deja colles (majuscules)
    for (std::size_t li = first; li < grid.size(); ++li) {
        const auto& row = grid[li];
        std::string name, type, initial, description;
        bool hasType = false, hasInitial = false, hasDescription = false;
        for (std::size_t c = 0; c < row.size() && c < cols.size(); ++c) {
            switch (cols[c]) {
                case FName: name = trimmed(row[c]); break;
                case FType: type = trimmed(row[c]); hasType = !type.empty(); break;
                case FInitial: initial = trimmed(row[c]); hasInitial = !initial.empty(); break;
                case FDescription: description = row[c]; hasDescription = !trimmed(row[c]).empty(); break;
                default: break;
            }
        }
        const std::size_t line = li + 1;
        if (name.empty()) {
            if (hasType || hasInitial || hasDescription) plan.refused.push_back({line, {}, "pas de nom"});
            continue;
        }
        if (!hmi::isIdentifier(name)) {
            plan.refused.push_back({line, name, "nom invalide : lettres, chiffres et _"});
            continue;
        }
        if (!seen.insert(upper(name)).second) {
            plan.refused.push_back({line, name, "nom en double dans le collage"});
            continue;
        }
        const auto at = std::find_if(tt->members.begin(), tt->members.end(), [&](const hmi::TypeMember& m) { return sameText(m.name, name); });
        const bool exists = at != tt->members.end();
        const std::size_t index = exists ? static_cast<std::size_t>(at - tt->members.begin()) : tt->members.size();
        hmi::TypeMember m;
        if (exists) m = *at;
        else {
            m.name = name;
            m.type = "INT";
        }
        if (hasType) {
            const std::string norm = ty::normalized(type);
            std::string why;
            if (!ty::validMemberType(trial, norm, &why)) {
                const std::string near = nearestType(trial, type);    // 1.10 : comme la maquette (BOOOL -> BOOL)
                plan.refused.push_back({line, name, "type inconnu : " + type + (near.empty() ? std::string{} : " \xE2\x80\x94 veux-tu dire " + near + " ?")});
                continue;
            }
            m.type = norm;
        }
        if (hasInitial) m.initial = initial;
        if (hasDescription) m.description = description;
        std::string why;
        if (hasInitial && !validInitial(trial, m.type, m.initial, &why)) {
            plan.refused.push_back({line, name, why});
            continue;
        }
        const hmi::HmiType before = *tt;
        if (exists) tt->members[index] = m;
        else tt->members.push_back(m);
        if (const auto cycle = ty::cycleOf(trial, tt->name); !cycle.empty()) {
            *tt = before;
            plan.refused.push_back({line, name, "le type se contiendrait lui-m\xC3\xAAme : " + cycle});
            continue;
        }
        (exists ? plan.replaced : plan.added).push_back(m.name);
    }
    plan.result = tt->members;
    if (plan.added.empty() && plan.replaced.empty() && plan.refused.empty()) plan.error = "Rien \xC3\xA0 coller : aucune ligne avec un nom.";
    return plan;
}

bool HmiTypesPane::applyMemberPaste(const MemberPaste& plan, std::string* why) {
    if (plan.added.empty() && plan.replaced.empty()) return fail(why, plan.error.empty() ? std::string("rien \xC3\xA0 appliquer") : plan.error);
    const auto members = plan.result;
    const std::size_t n = plan.added.size() + plan.replaced.size();
    return change(plan.type, "Coller depuis Excel : " + std::to_string(n) + (n > 1 ? " membres" : " membre"),
                  [members](hmi::HmiType& t, std::string*) {
                      t.members = members;
                      return true;
                  },
                  why);
}

std::string HmiTypesPane::copyMembersText() const {
    const auto* t = doc_->project.hmiType(current_);
    if (!t) return {};
    std::vector<std::size_t> rows;
    for (const auto r : members_->selectedModelRows())
        if (r < t->members.size()) rows.push_back(static_cast<std::size_t>(r));
    std::sort(rows.begin(), rows.end());
    if (rows.empty()) return {};
    // Une case comme Excel l'ecrit : entre guillemets si elle contient une
    // tabulation, un retour ou un guillemet ("" pour un guillemet).
    const auto cell = [](const std::string& text) {
        if (text.find_first_of("\t\r\n\"") == std::string::npos) return text;
        std::string out = "\"";
        for (const char ch : text) {
            if (ch == '"') out += '"';
            out += ch;
        }
        return out + "\"";
    };
    std::string out = "Nom\tType\tValeur initiale\tDescription\tD\xC3\xA9" "calage\tTaille\r\n";
    for (const auto r : rows) {
        const auto& m = t->members[r];
        const std::string offset = memberModel_ ? memberModel_->cellText(static_cast<ui::RowIndex>(r), 3) : std::string{};
        const std::string size = memberModel_ ? memberModel_->cellText(static_cast<ui::RowIndex>(r), 4) : std::string{};
        out += cell(m.name) + "\t" + cell(m.type) + "\t" + cell(m.initial) + "\t" + cell(m.description) + "\t" + cell(offset) + "\t" + cell(size) + "\r\n";
    }
    return out;
}

std::size_t HmiTypesPane::copyMembers() {
    const std::string text = copyMembersText();
    if (text.empty()) {
        say("Choisis les membres \xC3\xA0 copier (Ctrl ou Maj + clic pour plusieurs).", true);
        return 0;
    }
    ui::setClipboardText(text);
    const auto n = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) - 1;
    say(std::to_string(n) + (n > 1 ? " membres copi\xC3\xA9s" : " membre copi\xC3\xA9") + " : colle-les dans Excel (avec leurs titres).");
    return n;
}

bool HmiTypesPane::pasteMembers(std::string_view text) {
    auto plan = planMemberPaste(selectedType(), text);
    if (!plan.error.empty() && plan.refused.empty()) {
        say(plan.error, true);
        return false;
    }
    pendingText_ = std::string(text);
    pending_ = std::move(plan);
    showPreview();
    say("Aper\xC3\xA7u du collage : " + pending_->summary() + ". Appliquer pour coller, Annuler pour laisser.");
    return true;
}

void HmiTypesPane::showPreview() {
    auto* pv = static_cast<PastePreview*>(preview_);
    if (!pv) return;
    if (!pending_) {
        pv->setLines({});
        pv->setVisibility(ui::Visibility::Collapsed);
        layout();
        return;
    }
    const auto& plan = *pending_;
    const auto* t = doc_->project.hmiType(plan.type);
    std::vector<PastePreview::Line> lines;
    lines.push_back({"Coller dans " + (t ? t->name : std::string("?")) + " : " + plan.summary()
                         + (plan.ignored.empty() ? std::string{} : " \xC2\xB7 colonne(s) ignor\xC3\xA9" "e(s) : " + [&] {
                               std::string out;
                               for (const auto& i : plan.ignored) out += (out.empty() ? "" : ", ") + i;
                               return out;
                           }()),
                     0});
    for (const auto& n : plan.added) lines.push_back({"+ " + n + " (ajout\xC3\xA9)", 1});
    for (const auto& n : plan.replaced) lines.push_back({"\xE2\x86\xBB " + n + " (remplac\xC3\xA9)", 2});
    for (const auto& r : plan.refused)
        lines.push_back({"\xE2\x9C\x95 ligne " + std::to_string(r.line) + (r.name.empty() ? std::string{} : " : " + r.name) + " \xE2\x80\x94 " + r.why, 3});
    if (lines.size() > 7) {
        const std::size_t more = lines.size() - 6;
        lines.resize(6);
        lines.push_back({"\xE2\x80\xA6 et " + std::to_string(more) + " autre(s)", 0});
    }
    pv->setLines(std::move(lines));
    pv->applyButton().setEnabled(!plan.added.empty() || !plan.replaced.empty());
    pv->setVisibility(ui::Visibility::Visible);
    layout();
}

bool HmiTypesPane::confirmPaste() {
    if (!pending_) return false;
    const MemberPaste plan = std::move(*pending_);
    pending_.reset();
    pendingText_.clear();
    std::string why;
    const bool ok = applyMemberPaste(plan, &why);
    showPreview();
    if (!ok) {
        say("Refus\xC3\xA9 : " + why, true);
        return false;
    }
    if (!plan.added.empty() || !plan.replaced.empty()) {
        const auto* t = doc_->project.hmiType(plan.type);
        const std::string& firstName = !plan.added.empty() ? plan.added.front() : plan.replaced.front();
        if (t)
            for (std::size_t i = 0; i < t->members.size(); ++i)
                if (sameText(t->members[i].name, firstName)) selectMember(static_cast<int>(i));
    }
    say("Coll\xC3\xA9 : " + plan.summary() + ". Ctrl+Z pour tout annuler.", !plan.refused.empty());
    return true;
}

void HmiTypesPane::cancelPaste() {
    if (!pending_) return;
    pending_.reset();
    pendingText_.clear();
    showPreview();
    say("Collage annul\xC3\xA9 : rien n'a chang\xC3\xA9.");
}

void HmiTypesPane::openMemberMenu(gfx::Point at) {
    if (!menu_) return;
    members_->openContextMenu(at);
    // Copier et Coller : ceux des membres (les titres du collage, l'apercu) ; de
    // la table, on garde Tout choisir et les filtres.
    const bool type = selectedType() != kNoId;
    const bool member = type && selectedMember() >= 0;
    std::vector<ui::PopupMenu::Item> items;
    items.push_back({"Copier (pour Excel)", "Ctrl+C", member ? std::string{} : std::string("choisis des membres"), ui::Icon::None, member, false,
                     kMenuBase - TCopy});
    items.push_back({"Coller depuis Excel\xE2\x80\xA6", "Ctrl+V", type ? std::string{} : std::string("choisis un type"), ui::Icon::None, type, false,
                     kMenuBase - TPaste});
    items.push_back({"", "", "", ui::Icon::None, true, true, -1});
    items.push_back({"Nouveau membre", "", type ? std::string{} : std::string("choisis un type"), ui::Icon::None, type, false, kMenuBase - TMember});
    items.push_back({"Supprimer le membre", "", member ? std::string{} : std::string("choisis un membre"), ui::Icon::Close, member, false,
                     kMenuBase - TRemoveMember});
    bool keep = false;
    for (const auto& it : menu_->items()) {
        if (!it.separator && it.id == 5) keep = true;                  // Tout choisir, et ce qui suit
        if (!keep) continue;
        if (items.back().separator == false && it.id == 5) items.push_back({"", "", "", ui::Icon::None, true, true, -1});
        items.push_back(it);
    }
    menu_->setItems(std::move(items));
}
// ---- fin 1.10 ----

void HmiTypesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38.f});
    status_->setBounds({b.x, b.y + b.h - 24.f, b.w, 24.f});
    // 1.10 : l'apercu d'un collage, entre la table et la barre d'etat.
    const float ph = preview_ && pending_ ? static_cast<PastePreview*>(preview_)->wantedHeight() : 0.f;
    if (preview_) preview_->setBounds({b.x, b.y + b.h - 24.f - ph, b.w, ph});
    split_->setBounds({b.x, b.y + 38.f, b.w, std::max(0.f, b.h - 62.f - ph)});
    // 1.10 (chantier U) : les valeurs d'une enumeration a la place des membres ; les
    // fenetres sur tout le volet.
    if (enums_ && enums_->visible() && membersPanel_) {
        split_->layout();
        enums_->setBounds(membersPanel_->bounds());
    }
    if (enumDialog_) enumDialog_->setBounds(b);
    if (create_) create_->setBounds(b);
    if (newMenu_) newMenu_->setBounds(b);           // un menu sans bornes ne se dessine pas
}

void HmiTypesPane::openNewTypeMenu() {
    if (!newMenu_) return;
    std::vector<ui::PopupMenu::Item> items;
    items.push_back({"Structure\xE2\x80\xA6", "", "", ui::Icon::None, true, false, 1});
    items.push_back({"\xC3\x89num\xC3\xA9ration\xE2\x80\xA6", "", "", ui::Icon::None, true, false, 2});
    newMenu_->setItems(std::move(items));
    const auto r = tools_->rectOf(TAdd);
    auto* root = rootWidget();
    const auto sb = root ? root->bounds() : bounds();
    newMenu_->openAt({r.x, r.bottom()}, {sb.w, sb.h});
}

void HmiTypesPane::openEnumCreation() {
    if (!create_) return;
    create_->open(doc_->project);
    layout();
}

Id HmiTypesPane::createEnumerationFromPanel(std::string* why) {
    if (!create_) return kNoId;
    const Id made = createEnumeration(doc_, apply_, create_->name(), create_->values(), why);
    if (made == kNoId) return kNoId;
    create_->close();
    current_ = made;
    member_ = -1;
    refresh();
    say("\xC3\x89num\xC3\xA9ration " + doc_->project.hmiType(made)->name + " cr\xC3\xA9\xC3\xA9" "e, avec toString et fromString pr\xC3\xA9remplis \xC2\xB7 Ctrl+Z annule");
    return made;
}

bool HmiTypesPane::showsEnumeration() const { return enums_ && enums_->visible(); }

void HmiTypesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
