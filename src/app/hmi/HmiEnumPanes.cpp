// app/hmi/HmiEnumPanes.cpp - l'interface du type enumeration IHM (1.10, decision 15,
// chantier U ; la maquette : scene 14).
#include "HmiEnumPanes.hpp"

#include "../TablePaste.hpp"
#include "../../hmi/HmiEnums.hpp"
#include "../../hmi/HmiOperators.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {

enum EnumAction : int { EAdd = 1, EDuplicate, ERemove, EUp, EDown, EPaste, ERewrite };

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool sameName(std::string_view a, std::string_view b) { return upperOf(a) == upperOf(b); }
std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool parseNumber(std::string_view s, std::int64_t& out) {
    const std::string t = trim(s);
    if (t.empty()) return false;
    const char* b = t.data();
    const char* e = t.data() + t.size();
    if (*b == '+') ++b;
    const auto r = std::from_chars(b, e, out);
    return r.ec == std::errc{} && r.ptr == e;
}
std::string quoteFr(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }
std::string pad(std::size_t n) { return std::string(n, ' '); }

// Une commande nulle (rien n'a change) ne s'empile pas.
bool applyIf(const std::function<void(core::CommandPtr)>& apply, core::CommandPtr c) {
    if (!c || !apply) return false;
    apply(std::move(c));
    return true;
}

const std::string kWarn = "\xE2\x9A\xA0 ";   // le triangle
const std::string kOk = "\xE2\x9C\x93 ";     // la coche
const std::string kNo = "\xE2\x9C\x95 ";     // la croix

} // namespace

// =============================================================================
//  Les aides
// =============================================================================
std::vector<EnumUse> enumValueUses(const hmi::Project& p, std::string_view type, std::string_view value) {
    std::vector<EnumUse> out;
    const auto scan = [&](const std::string& where, const std::string& text) {
        if (text.find('#') == std::string::npos) return;
        // Les lignes que le renommage change : les emplois (hors chaines et commentaires).
        const std::string marked = hmi::renameEnumValueInText(text, type, value, std::string(value) + "_U_");
        if (marked == text) return;
        std::size_t a = 0, b = 0;
        int line = 1;
        while (a <= text.size() && b <= marked.size()) {
            const auto ea = text.find('\n', a);
            const auto eb = marked.find('\n', b);
            const std::string la = text.substr(a, ea == std::string::npos ? std::string::npos : ea - a);
            const std::string lb = marked.substr(b, eb == std::string::npos ? std::string::npos : eb - b);
            if (la != lb) out.push_back({where, line, trim(la)});
            if (ea == std::string::npos || eb == std::string::npos) break;
            a = ea + 1;
            b = eb + 1;
            ++line;
        }
    };
    const auto actions = [&](const std::string& where, const std::vector<hmi::Action>& list) {
        for (const auto& a : list) {
            scan(where, a.watch);
            scan(where, a.guard);
            if (a.operation != hmi::Operation::Log) scan(where, a.value);
            scan(where, a.params);                           // 1.11.6
        }
    };
    for (const auto& sc : p.programs.scripts)
        if (sc.lang == hmi::ScriptLang::ST) {
            scan(sc.name, sc.body);
            scan(sc.name, sc.watch);
        }
    for (const auto& f : p.programs.functions) scan(f.name, f.body);
    for (const auto& ty : p.programs.types)
        for (const auto& o : ty.operators) {
            const auto role = hmi::enumConversionRole(ty, o);
            scan(role.empty() ? hmi::operatorSignature(o) : std::string(role) + " (op\xC3\xA9rateur)", o.body);
        }
    for (const auto& v : p.views) {
        for (const auto& sc : v.scripts)
            if (sc.lang == hmi::ScriptLang::ST) scan(v.name + "." + sc.name, sc.body);
        for (const auto& o : v.operators) scan(v.name + " : " + hmi::operatorSignature(o), o.body);
        actions(v.name + " (actions)", v.actions);
        for (const auto& o : v.objects) {
            actions(v.name + "." + o.name, o.actions);
            for (const auto& pr : o.props) scan(v.name + "." + o.name, pr.expr);
        }
    }
    for (const auto& a : p.alarms) scan("Alarme " + a.name, a.condition);
    return out;
}

std::size_t enumUseScripts(const std::vector<EnumUse>& uses) {
    std::set<std::string> where;
    for (const auto& u : uses) where.insert(u.where);
    return where.size();
}

std::string enumDeclaration(std::string_view nameView, const std::vector<hmi::HmiEnumValue>& values, int lang) {
    const std::string name(nameView);
    std::size_t w = 4;
    for (const auto& v : values) w = std::max(w, v.name.size() + std::to_string(v.value).size());
    const auto text = [](const hmi::HmiEnumValue& v) { return v.text.empty() ? v.name : v.text; };
    std::string s;
    if (lang == 1) {
        s = "typedef enum {\n";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto& v = values[i];
            s += "    " + name + "_" + v.name + " = " + std::to_string(v.value) + (i + 1 < values.size() ? "," : " ")
               + pad(w + 2 - v.name.size() - std::to_string(v.value).size()) + "/* " + text(v) + " */\n";
        }
        s += "} " + name + ";\n";
        s += "const char *" + lowerOf(name) + "_to_string(" + name + " a);\n";
        s += name + " " + lowerOf(name) + "_from_string(const char *a);";
        return s;
    }
    if (lang == 2) {
        s = "enum class " + name + " : int32_t {\n";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto& v = values[i];
            s += "    " + v.name + " = " + std::to_string(v.value) + (i + 1 < values.size() ? "," : " ")
               + pad(w + 2 - v.name.size() - std::to_string(v.value).size()) + "// " + text(v) + "\n";
        }
        s += "};\n";
        s += "std::string to_string(" + name + " a);\n";
        s += name + " from_string(const std::string& a);";
        return s;
    }
    s = "TYPE " + name + " : (\n";
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto& v = values[i];
        s += "    " + v.name + " := " + std::to_string(v.value) + (i + 1 < values.size() ? "," : " ")
           + pad(w + 2 - v.name.size() - std::to_string(v.value).size()) + "(* " + text(v) + " *)\n";
    }
    s += ") DINT;\nEND_TYPE";
    return s;
}

std::vector<hmi::HmiEnumValue> parseEnumLines(std::string_view text) {
    std::vector<hmi::HmiEnumValue> out;
    bool any = false;
    std::int64_t best = -1;
    std::size_t a = 0;
    while (a <= text.size()) {
        const auto e = text.find('\n', a);
        std::string line = trim(text.substr(a, e == std::string_view::npos ? std::string_view::npos : e - a));
        a = e == std::string_view::npos ? text.size() + 1 : e + 1;
        if (line.empty()) continue;
        hmi::HmiEnumValue v;
        std::size_t i = 0;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != '=' && line[i] != '\'' && line[i] != '"') ++i;
        v.name = line.substr(0, i);
        std::string rest = trim(std::string_view(line).substr(i));
        bool numbered = false;
        if (!rest.empty() && rest.front() == '=') {
            rest = trim(std::string_view(rest).substr(1));
            std::size_t j = 0;
            while (j < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[j])) || (j == 0 && (rest[j] == '-' || rest[j] == '+')))) ++j;
            std::int64_t n = 0;
            if (j > 0 && parseNumber(rest.substr(0, j), n)) {
                v.value = n;
                numbered = true;
            }
            rest = trim(std::string_view(rest).substr(j));
        }
        if (!numbered) v.value = any ? best + 1 : 0;
        if (rest.size() >= 2 && (rest.front() == '\'' || rest.front() == '"') && rest.back() == rest.front())
            rest = rest.substr(1, rest.size() - 2);
        else if (!rest.empty() && (rest.front() == '\'' || rest.front() == '"'))
            rest = rest.substr(1);
        v.text = trim(rest);
        best = any ? std::max(best, v.value) : v.value;
        any = true;
        out.push_back(std::move(v));
    }
    return out;
}

std::string enumRowIssue(const hmi::HmiType& t, std::size_t index) {
    for (const auto& i : hmi::enumIssues(t))
        if (i.index == static_cast<int>(index)) return i.message;
    return {};
}

std::string EnumPaste::summary() const {
    return std::to_string(added) + " ajout\xC3\xA9" "e(s), " + std::to_string(replaced) + " remplac\xC3\xA9" "e(s), " + std::to_string(refused)
         + " refus\xC3\xA9" "e(s)";
}

EnumPaste planEnumPaste(const hmi::HmiType& t, std::string_view text) {
    EnumPaste plan;
    plan.result = t.values;
    auto grid = paste::parseGrid(text);
    if (grid.empty()) return plan;
    // Les colonnes : Nom, Valeur, Texte affiche, Description (titres reconnus, sinon dans cet ordre).
    int col[4] = {0, 1, 2, 3};
    {
        const auto& first = grid.front();
        int found[4] = {-1, -1, -1, -1};
        for (std::size_t c = 0; c < first.size(); ++c) {
            const std::string n = paste::normalizedTitle(first[c]);
            if (n == "nom" || n == "name") found[0] = static_cast<int>(c);
            else if (n == "valeur" || n == "value" || n == "nombre") found[1] = static_cast<int>(c);
            else if (n == "texteaffiche" || n == "texte" || n == "text" || n == "libelle") found[2] = static_cast<int>(c);
            else if (n == "description" || n == "commentaire" || n == "comment") found[3] = static_cast<int>(c);
        }
        if (found[0] >= 0) {
            plan.titles = true;
            for (int k = 0; k < 4; ++k) col[k] = found[k];
            grid.erase(grid.begin());
        }
    }
    const auto cell = [](const std::vector<std::string>& row, int c) { return c >= 0 && static_cast<std::size_t>(c) < row.size() ? trim(row[static_cast<std::size_t>(c)]) : std::string{}; };
    for (const auto& row : grid) {
        hmi::HmiEnumValue v;
        v.name = cell(row, col[0]);
        const std::string number = cell(row, col[1]);
        v.text = cell(row, col[2]);
        v.description = cell(row, col[3]);
        if (v.name.empty() && number.empty()) continue;
        const std::string label = v.name + " = " + number;
        int existing = -1;
        for (std::size_t i = 0; i < plan.result.size(); ++i)
            if (sameName(plan.result[i].name, v.name)) existing = static_cast<int>(i);
        std::int64_t n = 0;
        if (!parseNumber(number, n)) {
            hmi::HmiType sofar;
            sofar.values = plan.result;
            if (number.empty() && existing < 0) n = hmi::nextEnumNumber(sofar);
            else if (number.empty()) n = plan.result[static_cast<std::size_t>(existing)].value;
            else {
                plan.lines.push_back({3, label, "la valeur doit \xC3\xAAtre un entier (DINT)"});
                ++plan.refused;
                continue;
            }
        }
        v.value = n;
        hmi::HmiType trial = t;
        trial.values = plan.result;
        const std::size_t at = existing >= 0 ? static_cast<std::size_t>(existing) : trial.values.size();
        if (existing >= 0) {
            if (v.text.empty()) v.text = trial.values[at].text;
            if (v.description.empty()) v.description = trial.values[at].description;
            trial.values[at] = v;
        } else {
            trial.values.push_back(v);
        }
        std::string why = enumRowIssue(trial, at);
        if (why.empty()) {
            // Un doublon avec une ligne plus loin se signale sur elle : un constat de plus suffit.
            hmi::HmiType sofarType = t;
            sofarType.values = plan.result;
            const auto beforeIssues = hmi::enumIssues(sofarType);
            const auto afterIssues = hmi::enumIssues(trial);
            if (afterIssues.size() > beforeIssues.size()) why = afterIssues.back().message;
        }
        if (!why.empty()) {
            plan.lines.push_back({3, label, why});
            ++plan.refused;
            continue;
        }
        if (existing >= 0) {
            const auto& before = plan.result[at];
            plan.lines.push_back({2, label, "avant : " + std::to_string(before.value) + " " + quoteFr(hmi::enumText(before)) + " \xE2\x86\x92 "
                                                + quoteFr(hmi::enumText(v))});
            ++plan.replaced;
        } else {
            plan.lines.push_back({1, label, quoteFr(hmi::enumText(v))});
            ++plan.added;
        }
        plan.result = std::move(trial.values);
    }
    return plan;
}

Id createEnumeration(const hmi::DocumentPtr& doc, const std::function<void(core::CommandPtr)>& apply, const std::string& rawName,
                     const std::vector<hmi::HmiEnumValue>& values, std::string* why) {
    const std::string name = trim(rawName);
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return kNoId;
    };
    if (name.empty()) return fail("donne un nom");
    if (!hmi::validEnumName(name)) return fail("nom ST invalide (lettres, chiffres, \xC2\xAB _ \xC2\xBB ; pas de chiffre en t\xC3\xAAte)");
    if (hmi::types::isElementary(name)) return fail(name + " est un type \xC3\xA9l\xC3\xA9mentaire");
    if (doc->project.hmiTypeByName(name)) return fail("ce nom est d\xC3\xA9j\xC3\xA0 pris (un type IHM)");
    if (values.empty()) return fail("donne au moins une valeur");
    {
        hmi::HmiType trial;
        trial.kind = hmi::HmiTypeKind::Enumeration;
        trial.name = name;
        trial.values = values;
        if (const auto issues = hmi::enumIssues(trial); !issues.empty()) return fail(issues.front().message);
    }
    Id made = kNoId;
    applyIf(apply, hmi::changeProject(doc, "Nouvelle \xC3\xA9num\xC3\xA9ration " + name, [&](hmi::Project& p) {
        hmi::HmiType t = hmi::makeEnumeration(p, name);
        t.values = values;
        (void)hmi::regenerateEnumConversions(p, t);       // toString et fromString d'apres ces valeurs
        made = t.id;
        p.programs.types.push_back(std::move(t));
    }));
    return made;
}

// =============================================================================
//  La fenetre (renommer, supprimer, coller)
// =============================================================================
HmiEnumDialog::HmiEnumDialog(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    text_ = &static_cast<ui::MultiLineText&>(addChild(std::make_unique<ui::MultiLineText>(base + ".texte")));
    text_->setReadOnly(false);                       // le tableau colle se corrige, l'apercu suit
    links_ += text_->textChanged->connect([this](const std::string& t) {
        if (onText) onText(t);
    });
    for (std::size_t i = 0; i < 3; ++i) {
        buttons_[i] = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("", base + ".bouton" + std::to_string(i))));
        links_ += buttons_[i]->clicked->connect([this, i] {
            if (onButton) onButton(static_cast<int>(i));
        });
    }
    setVisibility(ui::Visibility::Collapsed);
}

void HmiEnumDialog::setup(std::string title, std::vector<Line> lines, std::vector<std::string> buttons, bool withText, std::string text,
                          bool danger) {
    title_ = std::move(title);
    lines_ = std::move(lines);
    note_.clear();
    withText_ = withText;
    count_ = std::min<std::size_t>(buttons.size(), 3);
    for (std::size_t i = 0; i < 3; ++i) {
        const bool on = i < count_;
        buttons_[i]->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (on) {
            buttons_[i]->setText(buttons[i]);
            buttons_[i]->setStyle(i + 1 == count_ ? (danger ? ui::Button::Style::Danger : ui::Button::Style::Primary) : ui::Button::Style::Default);
        }
    }
    text_->setVisibility(withText ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (withText) text_->setText(std::move(text));
    setVisibility(ui::Visibility::Visible);
    layout();
    invalidate();
}

void HmiEnumDialog::setLines(std::vector<Line> lines) {
    lines_ = std::move(lines);
    invalidate();
}
void HmiEnumDialog::setNote(std::string note) {
    note_ = std::move(note);
    invalidate();
}

gfx::Rect HmiEnumDialog::boxRect() const {
    const auto b = bounds();
    const float w = std::min(b.w - 24.f, withText_ ? 860.f : 660.f);
    const float rows = static_cast<float>(std::min<std::size_t>(lines_.size(), 14));
    const float h = std::min(b.h - 24.f, withText_ ? 380.f : 120.f + rows * 20.f);
    return {b.x + (b.w - w) * 0.5f, b.y + std::max(12.f, (b.h - h) * 0.35f), w, h};
}

void HmiEnumDialog::onLayout() {
    const auto r = boxRect();
    float x = r.right() - 12.f;
    for (std::size_t k = count_; k-- > 0;) {
        const float w = std::max(96.f, 28.f + 8.6f * static_cast<float>(buttons_[k]->text().size()));
        x -= w;
        buttons_[k]->setBounds({x, r.bottom() - 40.f, w, 28.f});
        x -= 8.f;
    }
    if (withText_) text_->setBounds({r.x + 14.f, r.y + 62.f, r.w * 0.48f - 14.f, r.h - 62.f - 54.f});
}

void HmiEnumDialog::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, gfx::Color{0, 0, 0, 110});
    const auto r = boxRect();
    ctx.r.fillRoundedRect(r, c.panelBg, 6.f);
    ctx.r.strokeRect(r, c.accent, 1.f);
    ctx.r.fillRect({r.x, r.y, r.w, 38.f}, c.headerBg);
    ctx.r.drawText({r.x + 14.f, r.y + 11.f}, title_, ctx.theme.font.uiBold, c.text);
    ctx.r.drawText({r.x + 14.f + 0.6f, r.y + 11.f}, title_, ctx.theme.font.uiBold, c.text);
    ctx.r.line({r.x, r.bottom() - 52.f}, {r.right(), r.bottom() - 52.f}, c.border, 1.f);
    if (!note_.empty()) ctx.r.drawText({r.x + 14.f, r.bottom() - 34.f}, note_, ctx.theme.font.smallUi, c.textMuted);
    if (withText_)
        ctx.r.drawText({r.x + 14.f, r.y + 44.f}, "LE TABLEAU COLL\xC3\x89 (Nom, Valeur, Texte affich\xC3\xA9, Description)", ctx.theme.font.smallUi,
                       c.textMuted);
    const float lx = withText_ ? r.x + r.w * 0.5f + 6.f : r.x + 14.f;
    float y = r.y + (withText_ ? 44.f : 50.f);
    if (withText_) {
        ctx.r.drawText({lx, y}, "APER\xC3\x87U", ctx.theme.font.smallUi, c.textMuted);
        y += 20.f;
    }
    ctx.r.pushClip({lx, r.y + 40.f, r.right() - lx - 10.f, r.h - 96.f});
    for (const auto& l : lines_) {
        if (y > r.bottom() - 60.f) break;
        const gfx::Color col = l.tone == 1 ? c.ok : l.tone == 2 ? c.info : l.tone == 3 ? c.error : l.tone == 4 ? c.textMuted : c.text;
        const auto font = l.mono ? ctx.theme.font.mono : l.tone == 5 ? ctx.theme.font.uiBold : ctx.theme.font.smallUi;
        ctx.r.drawText({lx, y}, l.text, font, col);
        if (l.tone == 5) ctx.r.drawText({lx + 0.6f, y}, l.text, font, col);
        y += l.tone == 5 ? 24.f : 19.f;
    }
    ctx.r.popClip();
}

ui::EventResult HmiEnumDialog::onEvent(const ui::InputEvent& ev) {
    // La fenetre est modale : rien ne passe dessous.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        if (onButton) onButton(-1);
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::MouseDown>(&ev) || std::get_if<ui::MouseUp>(&ev) || std::get_if<ui::MouseWheel>(&ev)) return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}

// =============================================================================
//  Les valeurs d'une enumeration
// =============================================================================
namespace {

class ValueRows final : public ui::ITableModel {
public:
    using Commit = std::function<bool(std::size_t, std::size_t, const std::string&)>;
    struct Row { std::vector<std::string> cells; int value{-1}; bool bad{false}; bool reason{false}; bool textIsName{false}; };
    ValueRows(std::vector<Row> rows, Commit commit) : rows_(std::move(rows)), commit_(std::move(commit)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 5; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"#", "Nom", "Valeur", "Texte affich\xC3\xA9", "Description"};
        return c < 5 ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].cells.size() ? rows_[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= rows_.size()) return st;
        const auto& row = rows_[r];
        if (row.reason) {
            st.spanRow = true;
            st.fgTone = ui::Tone::Error;
            return st;
        }
        if (c == 0) st.fgTone = ui::Tone::Muted;
        if (c == 1 || c == 2) st.monospace = true;
        if (c == 1) st.bold = true;
        if (c == 3 && row.textIsName) st.fgTone = ui::Tone::Muted;   // pas de texte : le nom s'affiche
        if (row.bad) st.fgTone = ui::Tone::Error;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return r < rows_.size() && !rows_[r].reason && c >= 1 && c <= 4; }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= rows_.size() || rows_[r].reason || c < 1 || !commit_) return false;
        return commit_(static_cast<std::size_t>(rows_[r].value), c - 1, std::string(text));
    }
private:
    std::vector<Row> rows_;
    Commit           commit_;
};

} // namespace

HmiEnumValuesPane::HmiEnumValuesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(EAdd, HmiGlyph::Plus, "Ajouter une valeur apr\xC3\xA8s la ligne choisie : la suivante de la plus grande, le nom \xC3\xA0 taper",
               "Ajouter une valeur");
    tools->add(EDuplicate, HmiGlyph::Duplicate, "Dupliquer la valeur choisie (un nom et un nombre libres)", "Dupliquer");
    tools->add(ERemove, HmiGlyph::Delete, "Supprimer la valeur choisie (Suppr) ; employ\xC3\xA9" "e dans un script : la fen\xC3\xAAtre pr\xC3\xA9vient",
               "Supprimer");
    tools->add(EUp, HmiGlyph::Up, "Monter la valeur : l'ordre est celui de FOR EACH v IN le type", "Monter");
    tools->add(EDown, HmiGlyph::Down, "Descendre la valeur", "Descendre");
    tools->separator();
    tools->add(EPaste, HmiGlyph::Paste, "Coller un tableau d'Excel (Nom, Valeur, Texte affich\xC3\xA9, Description) : un aper\xC3\xA7u, puis Appliquer",
               "Coller depuis Excel");
    tools->separator();
    tools->add(ERewrite, HmiGlyph::Refresh, "R\xC3\xA9\xC3\xA9" "crire toString et fromString d'apr\xC3\xA8s les valeurs (Ctrl+Z l'annule)",
               "R\xC3\xA9\xC3\xA9" "crire toString / fromString");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(EDuplicate, [this] { return current() && row_ >= 0; });
    tools_->setEnabledWhen(ERemove, [this] { return current() && row_ >= 0; });
    tools_->setEnabledWhen(EUp, [this] { return current() && row_ > 0; });
    tools_->setEnabledWhen(EDown, [this] {
        const auto* t = current();
        return t && row_ >= 0 && row_ + 1 < static_cast<int>(t->values.size());
    });
    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case EAdd: (void)addValue(); break;
            case EDuplicate: (void)duplicateValue(); break;
            case ERemove: (void)removeValue(); break;
            case EUp: (void)moveValue(-1); break;
            case EDown: (void)moveValue(1); break;
            case EPaste: (void)openPaste(ui::clipboardText()); break;
            case ERewrite: (void)rewriteConversions(); break;
            default: break;
        }
    });

    auto grid = std::make_unique<ui::TableView>(base + ".grille");
    grid->setColumns({{"#", 34.f, 28.f, false, false, true, ui::Align::End},
                      {"Nom", 150.f, 60.f, true, false},
                      {"Valeur", 80.f, 50.f, true, false, true, ui::Align::End},
                      {"Texte affich\xC3\xA9", 170.f, 60.f, true, false},
                      {"Description", 260.f, 60.f, true, false}});
    grid->setSelectionMode(ui::SelectionMode::Single);
    grid->setPasteHandler([this](const ui::TableView::PasteRequest& rq) { (void)openPaste(rq.text); });
    grid_ = &static_cast<ui::TableView&>(addChild(std::move(grid)));
    links_ += grid_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_ || rows.empty()) return;
        const auto r = static_cast<std::size_t>(rows.front());
        if (r < viewToValue_.size()) {
            const int v = viewToValue_[r];
            row_ = v >= 0 ? v : -1 - v;
        }
    });

    static const char* const kLangs[] = {"ST", "C", "C++"};
    for (int i = 0; i < 3; ++i) {
        langs_[i] = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(kLangs[i], base + ".decl." + lowerOf(kLangs[i] == std::string("C++") ? "cpp" : kLangs[i]))));
        links_ += langs_[i]->clicked->connect([this, i] { setDeclarationLanguage(i); });
    }
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

HmiEnumValuesPane::~HmiEnumValuesPane() = default;

void HmiEnumValuesPane::setDialogWidget(HmiEnumDialog* d) { dialog_ = d; }

const hmi::HmiType* HmiEnumValuesPane::current() const {
    const auto* t = doc_->project.hmiType(type_);
    return t && hmi::isEnumeration(*t) ? t : nullptr;
}

void HmiEnumValuesPane::setType(Id id) {
    if (id != type_) {
        type_ = id;
        row_ = 0;
        closeDialog();
    }
    refresh();
}

void HmiEnumValuesPane::say(std::string text, bool warning) {
    message_ = std::move(text);
    (void)warning;
    message->emit(message_);
}

void HmiEnumValuesPane::refresh() {
    const auto* t = current();
    std::vector<ValueRows::Row> rows;
    viewToValue_.clear();
    if (t) {
        const auto issues = hmi::enumIssues(*t);
        if (row_ >= static_cast<int>(t->values.size())) row_ = static_cast<int>(t->values.size()) - 1;
        for (std::size_t i = 0; i < t->values.size(); ++i) {
            const auto& v = t->values[i];
            std::string why;
            for (const auto& is : issues)
                if (is.index == static_cast<int>(i)) {
                    why = is.message;
                    break;
                }
            rows.push_back({{std::to_string(i + 1), v.name, std::to_string(v.value), v.text.empty() ? v.name : v.text, v.description}, static_cast<int>(i),
                            !why.empty(), false, v.text.empty()});
            viewToValue_.push_back(static_cast<int>(i));
            if (!why.empty()) {
                rows.push_back({{kWarn + why, "", "", "", ""}, static_cast<int>(i), true, true, false});
                viewToValue_.push_back(-1 - static_cast<int>(i));
            }
        }
    }
    model_ = std::make_shared<ValueRows>(std::move(rows), [this](std::size_t index, std::size_t col, const std::string& text) {
        return setCell(index, col, text);
    });
    syncing_ = true;
    grid_->setModel(model_);
    if (row_ >= 0) {
        const int vr = viewRowOf(row_);
        if (vr >= 0) grid_->selectModelRows({static_cast<ui::RowIndex>(vr)}, false);
    }
    syncing_ = false;
    invalidate();
}

int HmiEnumValuesPane::viewRowOf(int index) const {
    for (std::size_t r = 0; r < viewToValue_.size(); ++r)
        if (viewToValue_[r] == index) return static_cast<int>(r);
    return -1;
}

void HmiEnumValuesPane::selectValue(int index) {
    row_ = index;
    refresh();
}

std::string HmiEnumValuesPane::noteText() const {
    const auto* t = current();
    if (!t) return {};
    std::set<int> bad;
    for (const auto& is : hmi::enumIssues(*t)) bad.insert(is.index);
    if (!bad.empty())
        return std::to_string(bad.size()) + (bad.size() > 1 ? " lignes en d\xC3\xA9" "faut" : " ligne en d\xC3\xA9" "faut")
             + " : corrige-les (Compiler les signale aussi).";
    return kOk + "Valeurs valides \xC2\xB7 l'ordre est celui de FOR EACH v IN " + t->name
         + " \xC2\xB7 double-clic ou F2 : modifier une case ; Entr\xC3\xA9" "e, Tab, \xC3\x89" "chap";
}

std::string HmiEnumValuesPane::declarationText() const {
    const auto* t = current();
    return t ? enumDeclaration(t->name, t->values, lang_) : std::string{};
}

void HmiEnumValuesPane::setDeclarationLanguage(int lang) {
    lang_ = std::clamp(lang, 0, 2);
    invalidate();
}

bool HmiEnumValuesPane::change(std::string label, const std::function<bool(hmi::HmiType&)>& edit) {
    const auto* t = current();
    if (!t) return false;
    hmi::HmiType trial = *t;
    if (!edit(trial)) return false;
    const Id id = type_;
    bool ok = false;
    applyIf(apply_, hmi::changeProject(doc_, std::move(label), [&](hmi::Project& p) {
        if (auto* x = p.hmiType(id)) ok = edit(*x);
    }));
    refresh();
    return ok;
}

bool HmiEnumValuesPane::addValue() {
    const auto* t = current();
    if (!t) return false;
    hmi::HmiEnumValue v;
    v.name = hmi::enumValueByName(*t, "Nouveau") ? hmi::nextEnumName(*t, "Nouveau") : std::string("Nouveau");
    v.value = hmi::nextEnumNumber(*t);
    v.text = v.name;
    const std::size_t at = row_ >= 0 ? static_cast<std::size_t>(row_) + 1 : t->values.size();
    const bool ok = change("Ajouter la valeur " + v.name, [&](hmi::HmiType& x) {
        x.values.insert(x.values.begin() + static_cast<std::ptrdiff_t>(std::min(at, x.values.size())), v);
        return true;
    });
    if (!ok) return false;
    row_ = static_cast<int>(at);
    refresh();
    say("Valeur ajout\xC3\xA9" "e : " + std::to_string(v.value) + ", la suivante de la plus grande \xC2\xB7 tape son nom");
    if (const int vr = viewRowOf(row_); vr >= 0) (void)grid_->beginCellEdit(static_cast<ui::RowIndex>(vr), 1);
    return true;
}

bool HmiEnumValuesPane::duplicateValue() {
    const auto* t = current();
    if (!t || row_ < 0 || row_ >= static_cast<int>(t->values.size())) return false;
    hmi::HmiEnumValue v = t->values[static_cast<std::size_t>(row_)];
    const std::string base = v.name + "_";
    std::string name = v.name + "_2";
    for (int k = 3; hmi::enumValueByName(*t, name) && k < 1000; ++k) name = base + std::to_string(k);
    v.name = name;
    v.value = hmi::nextEnumNumber(*t);
    const std::size_t at = static_cast<std::size_t>(row_) + 1;
    if (!change("Dupliquer la valeur " + t->values[static_cast<std::size_t>(row_)].name, [&](hmi::HmiType& x) {
            x.values.insert(x.values.begin() + static_cast<std::ptrdiff_t>(std::min(at, x.values.size())), v);
            return true;
        }))
        return false;
    row_ = static_cast<int>(at);
    refresh();
    say("Valeur dupliqu\xC3\xA9" "e : " + v.name + " = " + std::to_string(v.value));
    return true;
}

bool HmiEnumValuesPane::removeValue() {
    const auto* t = current();
    if (!t || row_ < 0 || row_ >= static_cast<int>(t->values.size())) return false;
    const auto& v = t->values[static_cast<std::size_t>(row_)];
    auto uses = enumValueUses(doc_->project, t->name, v.name);
    if (!uses.empty()) {
        dialogKind_ = Dialog::Delete;
        uses_ = std::move(uses);
        dialogIndex_ = static_cast<std::size_t>(row_);
        dialogOld_ = v.name;
        dialogNew_.clear();
        showDialog();
        return false;
    }
    return removeValueAnyway(static_cast<std::size_t>(row_));
}

bool HmiEnumValuesPane::removeValueAnyway(std::size_t index) {
    const auto* t = current();
    if (!t || index >= t->values.size()) return false;
    const std::string name = t->values[index].name;
    if (!change("Supprimer la valeur " + name, [&](hmi::HmiType& x) {
            if (index >= x.values.size()) return false;
            x.values.erase(x.values.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }))
        return false;
    row_ = std::max(0, static_cast<int>(index) - 1);
    refresh();
    say("Valeur supprim\xC3\xA9" "e : " + name + " \xC2\xB7 Ctrl+Z la rend");
    return true;
}

bool HmiEnumValuesPane::moveValue(int delta) {
    const auto* t = current();
    if (!t || row_ < 0) return false;
    const long long to = static_cast<long long>(row_) + delta;
    if (to < 0 || to >= static_cast<long long>(t->values.size())) return false;
    const std::size_t from = static_cast<std::size_t>(row_);
    const std::string name = t->values[from].name;
    if (!change((delta < 0 ? "Monter la valeur " : "Descendre la valeur ") + name, [&](hmi::HmiType& x) {
            if (from >= x.values.size() || static_cast<std::size_t>(to) >= x.values.size()) return false;
            std::swap(x.values[from], x.values[static_cast<std::size_t>(to)]);
            return true;
        }))
        return false;
    row_ = static_cast<int>(to);
    refresh();
    say("Ordre chang\xC3\xA9 : c'est celui de FOR EACH v IN " + t->name);
    return true;
}

bool HmiEnumValuesPane::setCell(std::size_t index, std::size_t col, const std::string& text) {
    const auto* t = current();
    if (!t || index >= t->values.size()) return false;
    const auto& v = t->values[index];
    row_ = static_cast<int>(index);
    if (col == 0) {
        const std::string to = trim(text);
        if (to == v.name) return true;
        auto uses = enumValueUses(doc_->project, t->name, v.name);
        if (!uses.empty() && !to.empty()) {
            dialogKind_ = Dialog::Rename;
            uses_ = std::move(uses);
            dialogIndex_ = index;
            dialogOld_ = v.name;
            dialogNew_ = to;
            showDialog();
            return true;
        }
        return renameValue(index, to, false);
    }
    if (col == 1) {
        std::int64_t n = 0;
        if (!parseNumber(text, n)) {
            say("Refus\xC3\xA9 : la valeur doit \xC3\xAAtre un entier (DINT)", true);
            return false;
        }
        if (n == v.value) return true;
        const bool ok = change("Modifier la valeur de " + v.name, [&](hmi::HmiType& x) {
            if (index >= x.values.size()) return false;
            x.values[index].value = n;
            return true;
        });
        if (ok) {
            const auto* t2 = current();
            const std::string why = t2 ? enumRowIssue(*t2, index) : std::string{};
            say(why.empty() ? std::string("Modifi\xC3\xA9 \xC2\xB7 Ctrl+Z annule") : why, !why.empty());
        }
        return ok;
    }
    if (col == 2 || col == 3) {
        if ((col == 2 ? v.text : v.description) == text) return true;
        if (col == 2 && v.text.empty() && text == v.name) return true;   // la case montrait le nom
        const bool ok = change(std::string(col == 2 ? "Modifier le texte de " : "Modifier la description de ") + v.name, [&](hmi::HmiType& x) {
            if (index >= x.values.size()) return false;
            (col == 2 ? x.values[index].text : x.values[index].description) = text;
            return true;
        });
        if (ok)
            say(col == 2 ? std::string("Modifi\xC3\xA9 \xC2\xB7 Ctrl+Z annule \xC2\xB7 \xC2\xAB R\xC3\xA9\xC3\xA9" "crire toString / fromString \xC2\xBB les met \xC3\xA0 jour")
                         : std::string("Modifi\xC3\xA9 \xC2\xB7 Ctrl+Z annule"));
        return ok;
    }
    return false;
}

bool HmiEnumValuesPane::renameValue(std::size_t index, const std::string& raw, bool everywhere) {
    const auto* t = current();
    if (!t || index >= t->values.size()) return false;
    const std::string to = trim(raw);
    const std::string from = t->values[index].name;
    const std::string type = t->name;
    if (to == from) return true;
    const Id id = type_;
    std::size_t changed = 0;
    applyIf(apply_, hmi::changeProject(doc_, "Renommer " + type + "#" + from + " en " + to + (everywhere ? " partout" : ""), [&](hmi::Project& p) {
        auto* x = p.hmiType(id);
        if (!x || index >= x->values.size()) return;
        x->values[index].name = to;
        if (everywhere) changed = hmi::renameEnumValue(p, type, from, to);
    }));
    refresh();
    const auto* t2 = current();
    const std::string why = t2 ? enumRowIssue(*t2, index) : std::string{};
    if (!why.empty()) say(why, true);
    else if (everywhere) say("Renomm\xC3\xA9 partout : " + type + "#" + to + " (" + std::to_string(changed) + " texte(s), une commande) \xC2\xB7 Ctrl+Z annule");
    else say("Valeur renomm\xC3\xA9" "e : " + from + " \xE2\x86\x92 " + to + " \xC2\xB7 Ctrl+Z annule");
    return true;
}

bool HmiEnumValuesPane::rewriteConversions() {
    const auto* t = current();
    if (!t) return false;
    const Id id = type_;
    const std::string name = t->name;
    const bool done = applyIf(apply_, hmi::changeProject(doc_, "R\xC3\xA9\xC3\xA9" "crire toString et fromString de " + name, [&](hmi::Project& p) {
        if (auto* x = p.hmiType(id)) (void)hmi::regenerateEnumConversions(p, *x);
    }));
    refresh();
    if (!done) {
        say("toString et fromString suivent d\xC3\xA9j\xC3\xA0 les valeurs : rien \xC3\xA0 r\xC3\xA9\xC3\xA9" "crire");
        return false;
    }
    say("toString et fromString r\xC3\xA9\xC3\xA9" "crits d'apr\xC3\xA8s les " + std::to_string(t->values.size()) + " valeurs \xC2\xB7 Ctrl+Z annule");
    return true;
}

bool HmiEnumValuesPane::openPaste(std::string_view text) {
    const auto* t = current();
    if (!t) return false;
    pasteText_ = std::string(text);
    paste_ = planEnumPaste(*t, pasteText_);
    dialogKind_ = Dialog::Paste;
    uses_.clear();
    showDialog();
    return paste_.added + paste_.replaced > 0;
}

bool HmiEnumValuesPane::applyPaste() {
    if (dialogKind_ != Dialog::Paste) return false;
    const auto* t = current();
    if (!t) return false;
    paste_ = planEnumPaste(*t, pasteText_);
    if (paste_.added + paste_.replaced == 0) return false;
    const auto result = paste_.result;
    const std::string sum = paste_.summary();
    const Id id = type_;
    applyIf(apply_, hmi::changeProject(doc_, "Coller " + std::to_string(paste_.added + paste_.replaced) + " valeur(s) dans " + t->name,
                              [&](hmi::Project& p) {
                                  if (auto* x = p.hmiType(id)) x->values = result;
                              }));
    closeDialog();
    refresh();
    say(sum + " \xC2\xB7 Ctrl+Z annule");
    return true;
}

bool HmiEnumValuesPane::confirmRename(bool everywhere) {
    if (dialogKind_ != Dialog::Rename) return false;
    const std::size_t index = dialogIndex_;
    const std::string to = dialogNew_;
    closeDialog();
    return renameValue(index, to, everywhere);
}

bool HmiEnumValuesPane::confirmDelete() {
    if (dialogKind_ != Dialog::Delete) return false;
    const std::size_t index = dialogIndex_;
    const std::string type = current() ? current()->name : std::string{};
    const std::string name = dialogOld_;
    closeDialog();
    const bool ok = removeValueAnyway(index);
    if (ok) say(type + "#" + name + " supprim\xC3\xA9" "e : ses emplois sont en erreur (Compiler les montre) \xC2\xB7 Ctrl+Z annule", true);
    return ok;
}

void HmiEnumValuesPane::closeDialog() {
    dialogKind_ = Dialog::None;
    uses_.clear();
    if (dialog_) dialog_->setVisibility(ui::Visibility::Collapsed);
    if (parent()) parent()->layout();
}

void HmiEnumValuesPane::showDialog() {
    if (!dialog_) {
        dialog_ = &static_cast<HmiEnumDialog&>(addChild(std::make_unique<HmiEnumDialog>(id() + ".fenetre")));
        layout();
    }
    const auto* t = current();
    const std::string type = t ? t->name : std::string{};
    dialog_->onButton = {};
    dialog_->onText = {};
    if (dialogKind_ == Dialog::Rename || dialogKind_ == Dialog::Delete) {
        const bool del = dialogKind_ == Dialog::Delete;
        const std::size_t n = enumUseScripts(uses_);
        std::vector<HmiEnumDialog::Line> lines;
        std::string head = std::to_string(n) + (n > 1 ? " scripts emploient " : " script emploie ") + type + "#" + dialogOld_;
        if (del) {
            std::size_t branches = 0;
            for (const auto& u : uses_) {
                const auto colon = u.text.find(':');
                if (colon != std::string::npos && (colon + 1 >= u.text.size() || u.text[colon + 1] != '=')) ++branches;
            }
            if (branches) head += branches > 1 ? ", dont " + std::to_string(branches) + " branches de CASE" : std::string(", dont une branche de CASE");
            head += " : ses emplois deviendront des erreurs (Compiler les montre).";
        } else {
            head += " : renommer partout ?";
        }
        lines.push_back({head, 5, false});
        for (std::size_t i = 0; i < uses_.size() && i < 10; ++i)
            lines.push_back({uses_[i].where + "  ligne " + std::to_string(uses_[i].line) + "   " + uses_[i].text, 0, true});
        if (uses_.size() > 10) lines.push_back({"\xE2\x80\xA6 et " + std::to_string(uses_.size() - 10) + " autre(s)", 4, false});
        if (!del) {
            lines.push_back({"\xC2\xAB Renommer partout \xC2\xBB change aussi le texte des scripts (une seule commande, Ctrl+Z l'annule) ;", 4, false});
            lines.push_back({"\xC2\xAB La valeur seulement \xC2\xBB laisse les scripts en erreur.", 4, false});
        }
        const std::string title = del ? "Supprimer " + type + "#" + dialogOld_ + " ?" : "Renommer " + type + "#" + dialogOld_ + " en " + dialogNew_ + " ?";
        if (del)
            dialog_->setup(kWarn + title, std::move(lines), {"Annuler", "Supprimer quand m\xC3\xAAme"}, false, {}, true);
        else
            dialog_->setup(kWarn + title, std::move(lines), {"Annuler", "La valeur seulement", "Renommer partout"}, false);
        dialog_->onButton = [this, del](int b) {
            if (b <= 0) closeDialog();
            else if (del) (void)confirmDelete();
            else (void)confirmRename(b == 2);
        };
    } else if (dialogKind_ == Dialog::Paste) {
        const auto lines = [this] {
            std::vector<HmiEnumDialog::Line> out;
            for (const auto& l : paste_.lines) {
                const char* tag = l.kind == 1 ? "AJOUT\xC3\x89" "E    " : l.kind == 2 ? "REMPLAC\xC3\x89" "E " : "REFUS\xC3\x89" "E    ";
                out.push_back({std::string(tag) + l.label + "   " + l.detail, l.kind, false});
            }
            if (out.empty()) out.push_back({"Rien \xC3\xA0 coller : colle un tableau d'Excel (Nom, Valeur, Texte affich\xC3\xA9, Description).", 4, false});
            return out;
        };
        dialog_->setup("Coller depuis Excel dans " + type, lines(), {"Annuler", "Appliquer"}, true, pasteText_);
        dialog_->setNote(paste_.summary() + " \xC2\xB7 Ctrl+Z annule");
        dialog_->onText = [this, lines](const std::string& text) {
            pasteText_ = text;
            if (const auto* t2 = current()) paste_ = planEnumPaste(*t2, pasteText_);
            dialog_->setLines(lines());
            dialog_->setNote(paste_.summary() + " \xC2\xB7 Ctrl+Z annule");
        };
        dialog_->onButton = [this](int b) {
            if (b == 1) (void)applyPaste();
            else closeDialog();
        };
    }
    if (parent()) parent()->layout();
    layout();
}

void HmiEnumValuesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 36.f});
    const float right = std::clamp(b.w * 0.30f, 190.f, 360.f);
    grid_->setBounds({b.x, b.y + 36.f, b.w - right - 6.f, std::max(0.f, b.h - 36.f - 26.f)});
    const float bx = b.right() - right;
    for (int i = 0; i < 3; ++i) langs_[i]->setBounds({bx + right - 3.f * 52.f + static_cast<float>(i) * 52.f - 8.f, b.y + 40.f, 48.f, 24.f});
    if (dialog_ && dialog_->parent() == this) dialog_->setBounds(b);
}

ui::EventResult HmiEnumValuesPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev);
        k && k->key == ui::Key::Delete && k->mods.none() && !grid_->cellEditing() && dialogKind_ == Dialog::None && current()) {
        (void)removeValue();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void HmiEnumValuesPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    const auto* t = current();
    // Sous la grille : la note (valide, ou les lignes en defaut).
    const auto g = grid_->bounds();
    std::set<int> bad;
    if (t)
        for (const auto& is : hmi::enumIssues(*t)) bad.insert(is.index);
    ctx.r.drawText({g.x + 8.f, g.bottom() + 6.f}, noteText(), ctx.theme.font.smallUi, bad.empty() ? c.ok : c.error);
    // A droite : la declaration en ST | C | C++.
    const float right = std::clamp(b.w * 0.30f, 190.f, 360.f);
    const gfx::Rect d{b.right() - right, b.y + 36.f, right, b.h - 36.f};
    ctx.r.fillRect(d, c.headerBg);
    if (right >= 3.f * 52.f + 120.f) ctx.r.drawText({d.x + 10.f, d.y + 10.f}, "D\xC3\x89" "CLARATION", ctx.theme.font.smallUi, c.textMuted);
    ctx.r.line({d.x, d.y}, {d.x, d.bottom()}, c.border, 1.f);
    for (int i = 0; i < 3; ++i)
        if (i == lang_) ctx.r.strokeRect(langs_[i]->bounds(), c.accent, 2.f);
    if (!t) return;
    const gfx::Rect code{d.x + 8.f, d.y + 36.f, d.w - 16.f, d.h - 44.f};
    ctx.r.fillRect(code, c.inputBg);
    ctx.r.pushClip(code);
    float y = code.y + 8.f;
    const std::string text = declarationText();
    std::size_t a = 0;
    while (a <= text.size() && y < code.bottom()) {
        const auto e = text.find('\n', a);
        const std::string line = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
        const bool comment = line.find("(*") != std::string::npos || line.find("/*") != std::string::npos || line.find("//") != std::string::npos;
        ctx.r.drawText({code.x + 8.f, y}, line, ctx.theme.font.mono, comment && line.find(" = ") == std::string::npos && line.find(":=") == std::string::npos
                                                                         ? c.syntaxComment
                                                                         : c.text);
        y += 18.f;
        if (e == std::string::npos) break;
        a = e + 1;
    }
    ctx.r.popClip();
}

// =============================================================================
//  La fenetre << Nouvelle enumeration >>
// =============================================================================
HmiEnumCreatePanel::HmiEnumCreatePanel(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    name_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".nom")));
    values_ = &static_cast<ui::MultiLineText&>(addChild(std::make_unique<ui::MultiLineText>(base + ".valeurs")));
    values_->setReadOnly(false);
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    create_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er l'\xC3\xA9num\xC3\xA9ration", base + ".creer")));
    create_->setStyle(ui::Button::Style::Primary);
    static const char* const kLangs[] = {"ST", "C", "C++"};
    static const char* const kIds[] = {"st", "c", "cpp"};
    for (int i = 0; i < 3; ++i) {
        langs_[i] = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(kLangs[i], base + ".decl." + kIds[i])));
        links_ += langs_[i]->clicked->connect([this, i] { setLanguage(i); });
    }
    links_ += name_->textChanged->connect([this](const std::string&) {
        create_->setEnabled(ready());
        invalidate();
    });
    links_ += values_->textChanged->connect([this](const std::string&) {
        create_->setEnabled(ready());
        invalidate();
    });
    links_ += cancel_->clicked->connect([this] { close(); });
    links_ += create_->clicked->connect([this] {
        if (ready()) created->emit();
    });
    setVisibility(ui::Visibility::Collapsed);
}

void HmiEnumCreatePanel::open(const hmi::Project& p) {
    taken_.clear();
    for (const auto& t : p.programs.types) taken_.push_back(upperOf(t.name));
    std::string name = "T_VANNE";
    for (int k = 2; std::find(taken_.begin(), taken_.end(), upperOf(name)) != taken_.end() && k < 1000; ++k) name = "T_VANNE" + std::to_string(k);
    name_->setText(name);
    values_->setText("Fermee 'Ferm\xC3\xA9" "e'\nOuverte\nEn_Mouvement 'En mouvement'\nDefaut = 9 'En d\xC3\xA9" "faut'");
    lang_ = 0;
    open_ = true;
    setVisibility(ui::Visibility::Visible);
    create_->setEnabled(ready());
    layout();
    invalidate();
}

void HmiEnumCreatePanel::close() {
    open_ = false;
    setVisibility(ui::Visibility::Collapsed);
    if (parent()) parent()->layout();
}

void HmiEnumCreatePanel::setName(const std::string& n) {
    name_->setText(n);
    create_->setEnabled(ready());
    invalidate();
}
void HmiEnumCreatePanel::setValuesText(const std::string& t) {
    values_->setText(t);
    create_->setEnabled(ready());
    invalidate();
}
void HmiEnumCreatePanel::setLanguage(int lang) {
    lang_ = std::clamp(lang, 0, 2);
    invalidate();
}
std::string HmiEnumCreatePanel::name() const { return trim(name_->text()); }
std::vector<hmi::HmiEnumValue> HmiEnumCreatePanel::values() const { return parseEnumLines(values_->text()); }

std::string HmiEnumCreatePanel::nameIssue() const {
    const std::string n = name();
    if (n.empty()) return "donne un nom";
    if (!hmi::validEnumName(n)) {
        if (std::isdigit(static_cast<unsigned char>(n.front()))) return "nom ST invalide : il commence par un chiffre";
        if (hmi::validEnumName(n + "x") || hmi::types::isElementary(n)) return "c'est un mot r\xC3\xA9serv\xC3\xA9";
        return "nom ST invalide (lettres, chiffres, \xC2\xAB _ \xC2\xBB ; pas de chiffre en t\xC3\xAAte)";
    }
    if (hmi::types::isElementary(n)) return n + " est un type \xC3\xA9l\xC3\xA9mentaire";
    if (std::find(taken_.begin(), taken_.end(), upperOf(n)) != taken_.end()) return "ce nom est d\xC3\xA9j\xC3\xA0 pris (un type IHM)";
    return {};
}

std::string HmiEnumCreatePanel::valuesIssue() const {
    const auto v = values();
    if (v.empty()) return "donne au moins une valeur";
    hmi::HmiType trial;
    trial.kind = hmi::HmiTypeKind::Enumeration;
    trial.name = name();
    trial.values = v;
    const auto issues = hmi::enumIssues(trial);
    return issues.empty() ? std::string{} : issues.front().message;
}

std::string HmiEnumCreatePanel::preview() const {
    const auto v = values();
    const std::string n = name();
    return v.empty() || n.empty() ? std::string{} : enumDeclaration(n, v, lang_);
}

gfx::Rect HmiEnumCreatePanel::boxRect() const {
    const auto b = bounds();
    const float w = std::min(b.w - 24.f, 820.f);
    const float h = std::min(b.h - 24.f, 420.f);
    return {b.x + (b.w - w) * 0.5f, b.y + std::max(12.f, (b.h - h) * 0.3f), w, h};
}

void HmiEnumCreatePanel::onLayout() {
    const auto r = boxRect();
    const float half = r.w * 0.5f;
    name_->setBounds({r.x + 14.f, r.y + 68.f, half - 28.f, 28.f});
    values_->setBounds({r.x + 14.f, r.y + 146.f, half - 28.f, r.h - 146.f - 80.f});
    for (int i = 0; i < 3; ++i) langs_[i]->setBounds({r.right() - 14.f - 3.f * 50.f + static_cast<float>(i) * 50.f, r.y + 46.f, 46.f, 24.f});
    create_->setBounds({r.right() - 14.f - 190.f, r.bottom() - 42.f, 190.f, 30.f});
    cancel_->setBounds({r.right() - 14.f - 190.f - 8.f - 100.f, r.bottom() - 42.f, 100.f, 30.f});
}

void HmiEnumCreatePanel::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, gfx::Color{0, 0, 0, 110});
    const auto r = boxRect();
    ctx.r.fillRoundedRect(r, c.panelBg, 6.f);
    ctx.r.strokeRect(r, c.accent, 1.f);
    ctx.r.fillRect({r.x, r.y, r.w, 38.f}, c.headerBg);
    const std::string title = "Nouvelle \xC3\xA9num\xC3\xA9ration";
    ctx.r.drawText({r.x + 14.f, r.y + 11.f}, title, ctx.theme.font.uiBold, c.text);
    ctx.r.drawText({r.x + 14.6f, r.y + 11.f}, title, ctx.theme.font.uiBold, c.text);
    ctx.r.drawText({r.x + 200.f, r.y + 12.f}, "Types IHM", ctx.theme.font.smallUi, c.textMuted);
    const float half = r.w * 0.5f;
    const auto& f = ctx.theme.font;
    ctx.r.drawText({r.x + 14.f, r.y + 48.f}, "NOM DU TYPE", f.smallUi, c.textMuted);
    const std::string ni = nameIssue();
    ctx.r.drawText({r.x + 14.f, r.y + 102.f}, ni.empty() ? kOk + "nom libre" : kNo + ni, f.smallUi, ni.empty() ? c.ok : c.error);
    ctx.r.drawText({r.x + 14.f, r.y + 126.f}, "PREMI\xC3\x88RES VALEURS (une par ligne : Nom [= valeur] ['texte affich\xC3\xA9'])", f.smallUi, c.textMuted);
    const std::string vi = valuesIssue();
    const auto vals = values();
    ctx.r.drawText({r.x + 14.f, r.bottom() - 74.f},
                   vi.empty() ? std::to_string(vals.size()) + " valeur(s) \xC2\xB7 sans nombre : la suivante de la plus grande" : kNo + vi, f.smallUi,
                   vi.empty() ? c.textMuted : c.error);
    // A droite : l'apercu et les deux conversions.
    const float rx = r.x + half + 6.f;
    ctx.r.drawText({rx, r.y + 50.f}, "APER\xC3\x87U DE LA D\xC3\x89" "CLARATION", f.smallUi, c.textMuted);
    for (int i = 0; i < 3; ++i)
        if (i == lang_) ctx.r.strokeRect(langs_[i]->bounds(), c.accent, 2.f);
    const gfx::Rect code{rx, r.y + 76.f, r.right() - rx - 14.f, 150.f};
    ctx.r.fillRect(code, c.inputBg);
    ctx.r.pushClip(code);
    float y = code.y + 6.f;
    const std::string text = preview();
    std::size_t a = 0;
    while (!text.empty() && a <= text.size() && y < code.bottom()) {
        const auto e = text.find('\n', a);
        ctx.r.drawText({code.x + 8.f, y}, text.substr(a, e == std::string::npos ? std::string::npos : e - a), f.mono, c.text);
        y += 17.f;
        if (e == std::string::npos) break;
        a = e + 1;
    }
    ctx.r.popClip();
    const std::string n = name().empty() ? std::string("\xE2\x80\xA6") : name();
    float cy = code.bottom() + 12.f;
    ctx.r.drawText({rx, cy}, "CR\xC3\x89\xC3\x89" "ES AVEC LE TYPE", f.smallUi, c.textMuted);
    cy += 20.f;
    ctx.r.drawText({rx, cy}, "toString    TO_STRING(" + n + ") : STRING", f.mono, c.text);
    cy += 19.f;
    ctx.r.drawText({rx, cy}, "fromString  TO_" + n + "(STRING) : " + n, f.mono, c.text);
    cy += 22.f;
    ctx.r.drawText({rx, cy}, "Deux op\xC3\xA9rateurs de conversion pr\xC3\xA9remplis (le texte affich\xC3\xA9 ;", f.smallUi, c.textMuted);
    cy += 17.f;
    ctx.r.drawText({rx, cy}, "le nom ou le texte), modifiables. Fournis aussi : TO_INT, TO_" + n + "(entier).", f.smallUi, c.textMuted);
    ctx.r.line({r.x, r.bottom() - 54.f}, {r.right(), r.bottom() - 54.f}, c.border, 1.f);
    ctx.r.drawText({r.x + 14.f, r.bottom() - 34.f}, "Ctrl+Z annule la cr\xC3\xA9" "ation.", f.smallUi, c.textMuted);
}

ui::EventResult HmiEnumCreatePanel::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        close();
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::MouseDown>(&ev) || std::get_if<ui::MouseUp>(&ev) || std::get_if<ui::MouseWheel>(&ev)) return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}

} // namespace app
