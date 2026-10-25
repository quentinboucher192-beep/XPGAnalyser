// app/hmi/HmiFunctionDialog.cpp - 1.12.24 : "Nouvelle fonction" (voir l'en-tete).
#include "HmiFunctionDialog.hpp"
#include "HmiTypePicker.hpp"
#include "HmiUnitPanes.hpp"     // 1.13.0 (lot J6b) : l'unite du retour (le selecteur d'unites)
#include "../../ui/DialogFrame.hpp"

#include "../../core/FieldCodec.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};
constexpr float   kLabelW = 112.f;   // la colonne des libelles
constexpr float   kFieldH = 30.f;
constexpr float   kNoteH = 20.f;

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool noReturn(const std::string& t) {
    const std::string s = trimmed(t);
    return s.empty() || s == "Aucun" || s == "(aucun)";
}

// Un texte coupe en lignes a la largeur (les \n d'abord, puis les mots).
std::vector<std::string> wrapped(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto nl = s.find('\n', from);
        const std::string para = s.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        std::string line, word;
        const auto flush = [&] {
            if (word.empty()) return;
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(trial, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            word.clear();
        };
        for (const char c : para) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        out.push_back(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return out;
}

} // namespace

// ------------------------------------------------------------- TypeField ---
//  La case Retour : elle se montre comme un champ (le type, en chasse fixe) et s'ouvre comme un
//  bouton - le selecteur de types. Un Button : les scripts la designent par ce qu'elle montre.
class HmiNewFunctionDialog::TypeField final : public ui::Button {
public:
    // 1.13.0 (lot J6b) : la meme case pour l'unite du retour (" sans ", " Choisir une unite... ").
    explicit TypeField(std::string id, std::string none = "Aucun", std::string action = "Choisir un type\xE2\x80\xA6")
        : ui::Button(none, std::move(id)), none_(std::move(none)), action_(std::move(action)) {}
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, enabled() ? c.inputBg : c.panelBg);
        ctx.r.strokeRect(b, focused() ? c.accent : c.border, focused() ? 2.f : 1.f);
        const bool none = text() == none_;
        const std::string& action = action_;
        const float aw = ui::measureWidth(action, ctx.theme.font.ui) + 26.f;
        const float ty = b.y + (b.h - ctx.r.lineHeight(ctx.theme.font.mono)) * 0.5f;
        ctx.r.pushClip({b.x + 1.f, b.y, b.w - aw - 8.f, b.h});
        ctx.r.drawText({b.x + 8.f, ty}, text(), none ? ctx.theme.font.ui : ctx.theme.font.mono, none ? c.textMuted : c.text);
        ctx.r.popClip();
        // l'action, a droite : le selecteur (comme la case Type d'une variable)
        const float ax = b.right() - aw;
        ctx.r.fillRect({ax, b.y + 5.f, 1.f, b.h - 10.f}, c.border);
        ctx.r.drawText({ax + 10.f, b.y + (b.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, action, ctx.theme.font.ui,
                       enabled() ? c.accent : c.textDisabled);
    }
private:
    std::string none_, action_;
};

// ------------------------------------------------------------------ Body ---
class HmiNewFunctionDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiNewFunctionDialog& d) : d_(d) {}
    ui::Button* cancel{nullptr};
    gfx::Rect panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(760.f, r.w - 40.f);
        const float innerW = w - 40.f;
        const auto lines = wrapped(d_.explanation(), gfxFont(), innerW);
        explanation_ = lines;
        float h = 44.f + 14.f + static_cast<float>(lines.size()) * 21.f + 14.f;   // titre, texte
        const bool scope = d_.owner_ == Owner::View || d_.owner_ == Owner::Popup;
        const bool virt = d_.owner_ == Owner::Symbol;
        h += 5.f * (kFieldH + kNoteH + 6.f);                                      // nom, generiques, retour, unite, description
        if (scope || virt) h += 26.f + kNoteH + 6.f;
        h += 12.f + 70.f + 8.f + 20.f + 56.f;                                      // l'apercu, la note, les boutons
        h = std::min(h, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        const float left = panel.x + 20.f, fieldX = left + kLabelW, fieldW = panel.w - 40.f - kLabelW;
        float y = panel.y + 44.f + 14.f;
        textY_ = y;
        y += static_cast<float>(explanation_.size()) * 21.f + 14.f;
        nameY_ = y;
        if (d_.nameField_) d_.nameField_->setBounds({fieldX, y, fieldW, kFieldH});
        y += kFieldH + kNoteH + 6.f;
        genericsY_ = y;
        if (d_.genericsField_) d_.genericsField_->setBounds({fieldX, y, fieldW, kFieldH});
        y += kFieldH + kNoteH + 6.f;
        returnY_ = y;
        if (d_.returnField_) d_.returnField_->setBounds({fieldX, y, fieldW, kFieldH});
        y += kFieldH + kNoteH + 6.f;
        unitY_ = y;                                                               // 1.13.0 (lot J6b)
        if (d_.unitField_) d_.unitField_->setBounds({fieldX, y, fieldW, kFieldH});
        y += kFieldH + kNoteH + 6.f;
        scopeY_ = -1.f;
        if (scope) {
            scopeY_ = y;
            if (d_.publicRadio_) d_.publicRadio_->setBounds({fieldX, y, 130.f, 26.f});
            if (d_.privateRadio_) d_.privateRadio_->setBounds({fieldX + 140.f, y, 130.f, 26.f});
            y += 26.f + kNoteH + 6.f;
        } else if (virt) {
            scopeY_ = y;
            if (d_.virtualBox_) d_.virtualBox_->setBounds({fieldX, y, fieldW, 26.f});
            y += 26.f + kNoteH + 6.f;
        }
        descY_ = y;
        if (d_.descField_) d_.descField_->setBounds({fieldX, y, fieldW, kFieldH});
        y += kFieldH + kNoteH + 6.f;
        previewY_ = y + 4.f;
        float bx = panel.x + panel.w - 20.f;
        for (ui::Button* b : {static_cast<ui::Button*>(d_.ok_), cancel}) {
            if (!b) continue;
            const float bw = std::max(104.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            b->setBounds({bx, panel.y + panel.h - 46.f, bw, 32.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const float left = panel.x + 20.f, fieldX = left + kLabelW;
        ctx.r.pushClip({panel.x + 8.f, panel.y + th, panel.w - 16.f, panel.h - th});
        float y = textY_;
        for (const auto& line : explanation_) {
            ctx.r.drawText({left, y}, line, ctx.theme.font.ui, c.text);
            y += 21.f;
        }
        const auto label = [&](float at, const char* text) {
            ctx.r.drawText({left, at + (kFieldH - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, text, ctx.theme.font.ui, c.textMuted);
        };
        const auto note = [&](float at, const std::string& text, gfx::Color col) {
            if (!text.empty()) ctx.r.drawText({fieldX + 2.f, at + kFieldH + 3.f}, text, kSmall, col);
        };
        label(nameY_, "Nom");
        const std::string problem = d_.nameProblem();
        note(nameY_, problem.empty() ? d_.nameNote() : problem,
             !problem.empty() ? c.error : (d_.sameName() > 0 ? c.warning : c.textMuted));
        label(genericsY_, "G\xC3\xA9n\xC3\xA9riques");
        const std::string gprob = d_.genericsProblem();
        note(genericsY_, gprob.empty() ? d_.genericsNote() : gprob, gprob.empty() ? c.textMuted : c.error);
        label(returnY_, "Retour");
        note(returnY_, d_.returnNote(), c.textMuted);
        label(unitY_, "Unit\xC3\xA9");                                   // 1.13.0 (lot J6b)
        const std::string uprob = d_.unitProblem();
        note(unitY_, uprob.empty() ? d_.unitNote() : uprob, uprob.empty() ? c.textMuted : c.error);
        if (scopeY_ >= 0.f) {
            const bool virt = d_.owner_ == Owner::Symbol;
            ctx.r.drawText({left, scopeY_ + (26.f - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, virt ? "Red\xC3\xA9" "finir" : "Port\xC3\xA9" "e",
                           ctx.theme.font.ui, c.textMuted);
            const std::string scopeNote = virt
                ? (d_.answer_.isVirtual ? "virtuelle : une instance peut avoir la sienne (Fonctions de l'instance)"
                                        : "une seule version, la m\xC3\xAA" "me pour toutes les instances")
                : (d_.answer_.pub ? "publique : les autres vues l'appellent par " + d_.ownerName_ + "." + trimmed(d_.answer_.name) + "(\xE2\x80\xA6)"
                                  : "priv\xC3\xA9" "e : " + std::string(d_.owner_ == Owner::Popup ? "la popup" : "la vue") + " seule l'appelle");
            ctx.r.drawText({fieldX + 2.f, scopeY_ + 26.f + 1.f}, scopeNote, kSmall, c.textMuted);
        }
        label(descY_, "Description");
        // l'apercu : ce qui s'ecrira, comment elle s'appellera
        const gfx::Rect box{left, previewY_, panel.w - 40.f, 70.f};
        ctx.r.fillRect(box, ctx.theme.brand.codeBg);
        ctx.r.strokeRect(box, c.border, 1.f);
        ctx.r.drawText({box.x + 10.f, box.y + 8.f}, "Elle s'\xC3\xA9" "crira", kSmall, c.textMuted);
        ctx.r.drawText({box.x + 10.f, box.y + 26.f}, d_.declaration(), ctx.theme.font.mono, c.text);
        ctx.r.drawText({box.x + 10.f, box.y + 48.f}, d_.calls(), kSmall, c.textMuted);
        ctx.r.drawText({left, box.y + box.h + 10.f},
                       "Ses param\xC3\xA8tres, ses locales et ses constantes se d\xC3\xA9" "clarent ensuite dans ses onglets. Ctrl+Z la retire.",
                       kSmall, c.textMuted);
        ctx.r.popClip();
    }
private:
    [[nodiscard]] static gfx::FontId gfxFont() { return gfx::FontId{16}; }
    HmiNewFunctionDialog& d_;
    std::vector<std::string> explanation_;
    float textY_{0}, nameY_{0}, genericsY_{0}, returnY_{0}, unitY_{0}, scopeY_{-1}, descY_{0}, previewY_{0};
};

// ======================================================================= ======
HmiNewFunctionDialog::HmiNewFunctionDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    const hmi::View* v = spec_.doc && spec_.owner != hmi::kNoId ? spec_.doc->project.view(spec_.owner) : nullptr;
    if (v) {
        ownerName_ = v->name;
        owner_ = hmi::isSymbolView(*v) ? Owner::Symbol : v->role == "popup" ? Owner::Popup : Owner::View;
    }
    answer_.name = trimmed(spec_.name);
    if (answer_.name.empty() && spec_.doc) {
        if (v) {
            answer_.name = "Fonction";
            for (int k = 2; hmi::symbolFunction(*v, answer_.name); ++k) answer_.name = "Fonction" + std::to_string(k);
        } else {
            answer_.name = hmi::uniqueFunctionName(spec_.doc->project, "Fonction");
        }
    }
    answer_.returnType = noReturn(spec_.returnType) ? std::string{} : trimmed(spec_.returnType);
}

HmiNewFunctionDialog::~HmiNewFunctionDialog() = default;

menu::MenuTraits HmiNewFunctionDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiNewFunctionDialog::title() const {
    switch (owner_) {
    case Owner::View:   return "Nouvelle fonction de " + ownerName_;
    case Owner::Popup:  return "Nouvelle fonction de la popup " + ownerName_;
    case Owner::Symbol: return "Nouvelle fonction du symbole " + ownerName_;
    default:            return "Nouvelle fonction IHM";
    }
}

std::string HmiNewFunctionDialog::explanation() const {
    switch (owner_) {
    case Owner::View:
    case Owner::Popup: {
        const std::string where = owner_ == Owner::Popup ? "la popup" : "la vue";
        return "Une fonction de " + where + " s'appelle par son nom dans " + where +
               " (les fx des objets, les textes \xC3\xA0 trous, les scripts, les actions) ; publique, par " + ownerName_ +
               ".Nom(\xE2\x80\xA6) ailleurs. Elle rend sa valeur par RETURN.";
    }
    case Owner::Symbol:
        return "Une fonction du symbole s'appelle par son nom dans le symbole, et ailleurs par Vue.Instance.Nom(\xE2\x80\xA6) "
               "(l'instance pos\xC3\xA9" "e dans une vue). Virtuelle, une instance peut la red\xC3\xA9" "finir.";
    default:
        return "Une fonction IHM s'appelle par son nom partout : Moyenne(a, b) dans un script, une action ou, si elle rend "
               "une valeur, une expression de vue. Son corps part d'un mod\xC3\xA8le \xC3\xA0 compl\xC3\xA9ter.";
    }
}

int HmiNewFunctionDialog::sameName() const {
    if (!spec_.doc) return 0;
    const std::string n = trimmed(answer_.name);
    auto same = [&](const std::string& other) {
        if (other.size() != n.size()) return false;
        for (std::size_t i = 0; i < n.size(); ++i)
            if (std::toupper(static_cast<unsigned char>(other[i])) != std::toupper(static_cast<unsigned char>(n[i]))) return false;
        return true;
    };
    int count = 0;
    if (const hmi::View* v = spec_.owner != hmi::kNoId ? spec_.doc->project.view(spec_.owner) : nullptr) {
        for (const auto& f : v->functions) count += same(f.name) ? 1 : 0;
    } else {
        for (const auto& f : spec_.doc->project.programs.functions) count += same(f.name) ? 1 : 0;
    }
    return count;
}

std::string HmiNewFunctionDialog::nameProblem() const {
    const std::string n = trimmed(answer_.name);
    if (n.empty()) return "Son nom : des lettres, des chiffres et _.";
    if (!hmi::isIdentifier(n)) return "\xC2\xAB " + n + " \xC2\xBB n'est pas un nom : des lettres, des chiffres et _ (pas de chiffre en t\xC3\xAA" "te).";
    return {};
}

std::string HmiNewFunctionDialog::nameNote() const {
    const int n = sameName();
    if (n > 0)
        return "D\xC3\xA9j\xC3\xA0 pris : une surcharge (" + std::to_string(n + 1) + " fonctions " + trimmed(answer_.name) +
               ") ; ses param\xC3\xA8tres la distingueront.";
    return "Des lettres, des chiffres et _ ; un nom d\xC3\xA9j\xC3\xA0 pris fait une surcharge.";
}

std::string HmiNewFunctionDialog::returnNote() const {
    if (answer_.returnType.empty()) return "Aucun : une proc\xC3\xA9" "dure, appel\xC3\xA9" "e seule sur sa ligne (elle ne rend rien).";
    return "Elle rend un " + answer_.returnType + " : elle sert aussi dans une expression.";
}

std::string HmiNewFunctionDialog::genericsProblem() const {
    if (trimmed(answer_.generics).empty()) return {};
    std::vector<hmi::GenericParam> g;
    std::string why;
    return hmi::parseGenerics(answer_.generics, g, &why) ? std::string{} : why;
}

std::string HmiNewFunctionDialog::genericsNote() const {
    if (trimmed(answer_.generics).empty())
        return "Vide : une fonction ordinaire. T (ou T : ANY_NUM, ou K, V) : un param\xC3\xA8tre, le retour ou une locale peut \xC3\xAAtre de type T.";
    std::vector<hmi::GenericParam> g;
    (void)hmi::parseGenerics(answer_.generics, g, nullptr);
    std::string names;
    for (const auto& p : g) names += (names.empty() ? "" : ", ") + p.name;
    return "T se d\xC3\xA9" "duit des arguments (" + trimmed(answer_.name) + (names.empty() ? std::string("<\xE2\x80\xA6>") : "<" + names + ">")
         + "), ou se donne : " + trimmed(answer_.name) + "<INT>(...).";
}

std::string HmiNewFunctionDialog::declaration() const {
    const std::string n = trimmed(answer_.name).empty() ? std::string("?") : trimmed(answer_.name);
    // 1.13.0 : une fonction generique porte son TEMPLATE <...> devant FUNCTION.
    std::string head;
    if (!trimmed(answer_.generics).empty()) {
        std::vector<hmi::GenericParam> g;
        if (hmi::parseGenerics(answer_.generics, g, nullptr)) head = "TEMPLATE <" + hmi::genericsText(g) + "> ";
    }
    if (answer_.returnType.empty()) return head + "FUNCTION " + n + "        (* une proc\xC3\xA9" "dure *)";
    return head + "FUNCTION " + n + " : " + hmi::units::withUnit(answer_.returnType, answer_.returnUnit);   // 1.13.0 : REAL [bar]
}

std::string HmiNewFunctionDialog::calls() const {
    const std::string n = trimmed(answer_.name).empty() ? std::string("?") : trimmed(answer_.name);
    const std::string call = n + "(\xE2\x80\xA6)";
    switch (owner_) {
    case Owner::View:
    case Owner::Popup: {
        const std::string where = owner_ == Owner::Popup ? "la popup" : "la vue";
        return answer_.pub ? "Dans " + where + " : " + call + "   \xC2\xB7   ailleurs : " + ownerName_ + "." + call
                           : "Dans " + where + " seulement : " + call;
    }
    case Owner::Symbol:
        return "Dans le symbole : " + call + "   \xC2\xB7   ailleurs : Vue.Instance." + call +
               (answer_.isVirtual ? "   \xC2\xB7   virtuelle" : "");
    default:
        return "Partout : " + call;
    }
}

void HmiNewFunctionDialog::refresh() {
    syncing_ = true;
    if (nameField_ && nameField_->text() != answer_.name) nameField_->setText(answer_.name);
    if (genericsField_ && genericsField_->text() != answer_.generics) genericsField_->setText(answer_.generics);
    if (descField_ && descField_->text() != answer_.description) descField_->setText(answer_.description);
    if (returnField_) returnField_->setText(answer_.returnType.empty() ? std::string("Aucun") : answer_.returnType);
    if (unitField_) {                                                    // 1.13.0 (lot J6b)
        unitField_->setText(answer_.returnUnit.empty() ? std::string("sans") : answer_.returnUnit);
        unitField_->setEnabled(unitUsable());
    }
    if (scope_) scope_->setValue(answer_.pub ? 0 : 1);
    if (virtualBox_) virtualBox_->setState(answer_.isVirtual ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    syncing_ = false;
    if (ok_) {
        ok_->setEnabled(canConfirm());
        ok_->setTooltip(canConfirm() ? std::string{} : !nameProblem().empty() ? nameProblem() : !genericsProblem().empty() ? genericsProblem() : unitProblem());
    }
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
}

void HmiNewFunctionDialog::setName(const std::string& name) {
    answer_.name = name;
    refresh();
}
void HmiNewFunctionDialog::setDescription(const std::string& text) {
    answer_.description = text;
    refresh();
}
void HmiNewFunctionDialog::setReturnType(const std::string& type) {
    answer_.returnType = noReturn(type) ? std::string{} : trimmed(type);
    if (!unitUsable()) answer_.returnUnit.clear();   // 1.13.0 (lot J6b) : un retour non numerique n'a pas d'unite
    refresh();
}

// ---- 1.13.0 (lot J6b) : l'unite du retour ----
bool HmiNewFunctionDialog::unitUsable() const { return !answer_.returnType.empty() && unitui::usable(answer_.returnType); }

void HmiNewFunctionDialog::setReturnUnit(const std::string& unit) {
    std::string canon;
    const std::string t = trimmed(unit);
    answer_.returnUnit = spec_.doc && unitui::problem(spec_.doc->project, answer_.returnType, t, &canon).empty() ? canon : t;
    refresh();
}

std::string HmiNewFunctionDialog::unitProblem() const {
    if (answer_.returnUnit.empty()) return {};
    static const hmi::Project kEmpty;
    return unitui::problem(spec_.doc ? spec_.doc->project : kEmpty, answer_.returnType.empty() ? std::string("(aucun)") : answer_.returnType,
                           answer_.returnUnit);
}

std::string HmiNewFunctionDialog::unitNote() const {
    if (!unitUsable()) return "Seulement pour un retour num\xC3\xA9rique (REAL, INT\xE2\x80\xA6).";
    if (answer_.returnUnit.empty()) return "Sans unit\xC3\xA9 (comme avant) ; un appel la propage comme une variable de ce type.";
    static const hmi::Project kEmpty;
    return "Elle rend un " + hmi::units::withUnit(answer_.returnType, answer_.returnUnit) + " \xE2\x80\x94 "
         + unitui::describe(spec_.doc ? spec_.doc->project : kEmpty, answer_.returnUnit);
}

void HmiNewFunctionDialog::chooseReturnUnit() {
    if (!unitUsable() || !unitpicker::available()) return;
    HmiUnitPicker::Spec spec;
    spec.field = "Unit\xC3\xA9 du retour de " + (trimmed(answer_.name).empty() ? std::string("la fonction") : trimmed(answer_.name));
    spec.current = answer_.returnUnit;
    spec.type = answer_.returnType;
    spec.doc = spec_.doc;
    spec.recents = unitpicker::recents();
    const std::weak_ptr<int> alive = alive_;
    unitpicker::ask(std::move(spec), [this, alive](const HmiUnitPicker::Answer& a) {
        if (alive.expired() || a.manage) return;
        setReturnUnit(a.unit);
    });
}
void HmiNewFunctionDialog::setPublic(bool on) {
    answer_.pub = on;
    refresh();
}
void HmiNewFunctionDialog::setVirtual(bool on) {
    answer_.isVirtual = on;
    refresh();
}
void HmiNewFunctionDialog::setGenerics(const std::string& text) {
    answer_.generics = text;
    refresh();
}

// Le selecteur de types (comme la case Type d'une variable) : les types d'un retour, Aucun compris ;
// d'un symbole ou d'une popup generique, ses T et K.
void HmiNewFunctionDialog::chooseReturn() {
    if (!typepicker::available()) return;
    HmiTypePicker::Spec spec;
    spec.field = "Retour de " + (trimmed(answer_.name).empty() ? std::string("la fonction") : trimmed(answer_.name));
    spec.current = answer_.returnType.empty() ? std::string("Aucun") : answer_.returnType;
    spec.use = hmi::typereg::UseReturn;
    spec.doc = spec_.doc;
    // 1.13.0 : les generiques DE LA FONCTION en tete du selecteur (puce Generiques), plus ceux du symbole hote.
    const auto label = [](const std::string& of, const std::vector<hmi::GenericParam>& gens) {
        std::string names;
        for (const auto& g : gens) names += (names.empty() ? "" : ", ") + g.name;
        return of + "<" + names + ">";
    };
    std::vector<hmi::GenericParam> own;
    (void)hmi::parseGenerics(answer_.generics, own, nullptr);
    std::vector<hmi::GenericParam> gen = own;
    std::string ofLabel = own.empty() ? std::string{} : label(trimmed(answer_.name), own);
    if (const hmi::View* v = spec_.doc && spec_.owner != hmi::kNoId ? spec_.doc->project.view(spec_.owner) : nullptr;
        v && !v->generics.empty()) {
        for (const auto& g : v->generics)
            if (std::none_of(gen.begin(), gen.end(), [&](const hmi::GenericParam& x) { return x.name == g.name; })) gen.push_back(g);
        if (ofLabel.empty()) ofLabel = label(v->name, v->generics);   // la fonction n'a pas les siens : le symbole hote
    }
    if (!gen.empty()) {
        spec.generics = gen;
        spec.genericsOf = ofLabel;
    }
    const std::weak_ptr<int> alive = alive_;
    typepicker::ask(std::move(spec), [this, alive](const HmiTypePicker::Answer& a) {
        if (alive.expired() || a.type.empty()) return;
        setReturnType(a.type);
        if (returnField_) {
            focus().clear();
            focus().focus(descField_ ? static_cast<ui::Widget*>(descField_) : returnField_);
        }
    });
}

bool HmiNewFunctionDialog::confirm() {
    if (!canConfirm()) {
        if (nameField_) {
            focus().clear();
            focus().focus(nameField_);
        }
        return false;
    }
    finish(true);
    return true;
}

void HmiNewFunctionDialog::cancel() { finish(false); }

std::string HmiNewFunctionDialog::encode(const Answer& a) {
    return core::joinFields({trimmed(a.name), a.returnType, a.description, a.pub ? "1" : "0", a.isVirtual ? "1" : "0", a.generics, a.returnUnit});
}

bool HmiNewFunctionDialog::parse(const std::string& payload, Answer& out) {
    const auto v = core::splitFields(payload);
    if (v.size() < 5 || v[0].empty()) return false;
    out.name = v[0];
    out.returnType = v[1];
    out.description = v[2];
    out.pub = v[3] != "0";
    out.isVirtual = v[4] == "1";
    out.generics = v.size() > 5 ? v[5] : std::string{};   // 1.13.0 : TEMPLATE <T> (absent d'un ancien payload : aucun)
    out.returnUnit = v.size() > 6 ? v[6] : std::string{}; // 1.13.0 (lot J6b) : l'unite du retour
    return true;
}

void HmiNewFunctionDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? encode(answer_) : std::string{}});
}

core::Status HmiNewFunctionDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    {
        auto f = std::make_unique<ui::InputText>(base + ".name");
        f->setPlaceholder("Moyenne, EstEnDefaut, Formater\xE2\x80\xA6");
        f->setText(answer_.name);
        nameField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += nameField_->textChanged->connect([this](const std::string& t) {
            if (!syncing_) setName(t);
        });
        links_ += nameField_->editingDone->connect([this](const std::string&) {
            // Entree seulement (le champ perd aussi le focus quand on clique ailleurs) : voir FormDialog
            if (!syncing_ && nameField_->focused()) (void)confirm();
        });
    }
    {
        // 1.13.0 : la case Generiques (TEMPLATE <T>), au-dessus du Retour.
        auto f = std::make_unique<ui::InputText>(base + ".generics");
        f->setPlaceholder("vide, ou T, T : ANY_NUM, K\xE2\x80\xA6");
        f->setText(answer_.generics);
        genericsField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += genericsField_->textChanged->connect([this](const std::string& t) {
            if (!syncing_) setGenerics(t);
        });
        links_ += genericsField_->editingDone->connect([this](const std::string&) {
            if (!syncing_ && genericsField_->focused()) (void)confirm();
        });
    }
    {
        auto t = std::make_unique<TypeField>(base + ".return");
        returnField_ = &static_cast<TypeField&>(body->addChild(std::move(t)));
        links_ += returnField_->clicked->connect([this] { chooseReturn(); });
    }
    {
        // 1.13.0 (lot J6b) : l'unite du retour - la case ouvre le selecteur d'unites.
        auto u = std::make_unique<TypeField>(base + ".unit", "sans", "Choisir une unit\xC3\xA9\xE2\x80\xA6");
        unitField_ = &static_cast<TypeField&>(body->addChild(std::move(u)));
        links_ += unitField_->clicked->connect([this] { chooseReturnUnit(); });
    }
    if (owner_ == Owner::View || owner_ == Owner::Popup) {
        scope_ = std::make_shared<ui::RadioGroup>();
        publicRadio_ = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>("Publique", scope_, 0, base + ".public")));
        privateRadio_ = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>("Priv\xC3\xA9" "e", scope_, 1, base + ".private")));
        scope_->setValue(answer_.pub ? 0 : 1);
        links_ += scope_->valueChanged->connect([this](int v) {
            if (!syncing_) setPublic(v == 0);
        });
    } else if (owner_ == Owner::Symbol) {
        virtualBox_ = &static_cast<ui::Checkbox&>(body->addChild(std::make_unique<ui::Checkbox>(
            "Virtuelle : une instance peut la red\xC3\xA9" "finir", base + ".virtual")));
        links_ += virtualBox_->stateChanged->connect([this](ui::Checkbox::State s) {
            if (!syncing_) setVirtual(s == ui::Checkbox::State::Checked);
        });
    }
    {
        auto f = std::make_unique<ui::InputText>(base + ".description");
        f->setPlaceholder("ce qu'elle calcule ou fait (son infobulle dans l'aide \xC3\xA0 la saisie)");
        descField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += descField_->textChanged->connect([this](const std::string& t) {
            if (!syncing_) {
                answer_.description = t;
                if (body_) body_->invalidate();
            }
        });
        links_ += descField_->editingDone->connect([this](const std::string&) {
            if (!syncing_ && descField_->focused()) (void)confirm();
        });
    }
    body->cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { (void)confirm(); });
    setRoot(std::move(body));
    refresh();
    focus().focus(nameField_);
    return core::ok();
}

ui::EventResult HmiNewFunctionDialog::HandleEvent(const ui::InputEvent& ev) {
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            (void)confirm();
            return ui::EventResult::Consumed;
        }
    }
    return r;
}

} // namespace app
