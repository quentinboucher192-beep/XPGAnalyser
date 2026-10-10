#include "HmiScriptPanes.hpp"
#include "../Disposition.hpp"
#include "HmiAssist.hpp"

#include "HmiAssetPanes.hpp"
#include "HmiIcons.hpp"
#include "HmiAskDialog.hpp"                 // 1.11.3 : la fenetre d'import des scripts d'une vue
#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/PathBrowse.hpp"
#include "../../core/CallTrail.hpp"      // 1.10.4 : un diagnostic qui echoue va au journal interne
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiEnums.hpp"         // 1.10 (integration I2) : knownType connait les enumerations
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiScriptCheck.hpp"   // 1.10 : les erreurs des scripts, a leur place
#include "../../hmi/HmiTypes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <map>
#include <cctype>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;
using hmi::Script;
using hmi::ScriptLang;

namespace {

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    // Lot 16 : des cases editables (les colonnes de la liaison des variables IHM).
    using Editable = std::function<bool(std::size_t, std::size_t)>;
    using Choices = std::function<std::vector<std::string>(std::size_t, std::size_t)>;
    using Commit = std::function<bool(std::size_t, std::size_t, const std::string&)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {}, Editable e = {}, Choices ch = {},
         Commit m = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)), editable_(std::move(e)), choices_(std::move(ch)),
          commit_(std::move(m)) {}
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return editable_ && editable_(r, c); }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex r, std::size_t c) const override {
        return choices_ ? choices_(r, c) : std::vector<std::string>{};
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override { return commit_ && commit_(r, c, std::string(text)); }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return style_ ? style_(r, c) : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    Style style_;
    Editable editable_;
    Choices  choices_;
    Commit   commit_;
};

// L'editeur et, dessous, la barre du nom ou est le curseur (comme sous une
// section de l'automate). La barre garde sa hauteur meme vide : un code qui
// saute a chaque deplacement du curseur se lit mal.
class CodeArea final : public ui::Widget {
public:
    explicit CodeArea(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        constexpr float kBar = 22.f;
        if (children().size() < 2) return;
        // 1.11.18 (refonte, lot 5) : le bandeau de l'ancien format (des blocs VAR dans le
        // code), au-dessus du code, quand il est montre.
        float top = 0.f;
        if (children().size() > 2 && children()[2]->visible()) {
            top = HmiDeclBanner::kHeight;
            children()[2]->setBounds({b.x, b.y, b.w, top});
        }
        children()[0]->setBounds({b.x, b.y + top, b.w, std::max(0.f, b.h - kBar - 2.f - top)});
        children()[1]->setBounds({b.x, b.bottom() - kBar, b.w, kBar});
    }
};

// Lot 16 : la page des scripts dans les onglets de la Programmation generale -
// la barre d'outils, le partage, la barre d'etat.
class ScriptsPage final : public ui::Widget {
public:
    explicit ScriptsPage(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        if (children().size() < 3) return;
        children()[0]->setBounds({b.x, b.y, b.w, 38.f});
        children()[1]->setBounds({b.x, b.y + 38.f, b.w, std::max(0.f, b.h - 62.f)});
        children()[2]->setBounds({b.x, b.y + b.h - 24.f, b.w, 24.f});
    }
};

enum ToolAction : int { TAddSt = 1, TAddC, TAddCpp, TRename, TDelete, TCompile, TClear, TAddVar, TEditVar, TDeleteVar,
                        TFolder,               // lot 21 : un dossier de scripts
                        TFix,                  // 1.10 (I2) : Ajouter les valeurs manquantes (CASE sur une enumeration)
                        TExport, TImport,      // 1.11.2 (decision 174) : les scripts generaux voyagent (.xpgscripts)
                        TBuildGen, TBuildRegen, TBuildGenComp, TBuildState };   // 1.11.13 : la generation incrementale

ui::Language languageOf(ScriptLang l) {
    switch (l) {
        case ScriptLang::ST:  return ui::Language::StructuredText;
        case ScriptLang::C:   return ui::Language::C;
        case ScriptLang::Cpp: return ui::Language::Cpp;
    }
    return ui::Language::StructuredText;
}

std::size_t lineCount(const std::string& s) {
    if (s.empty()) return 0;
    return 1 + static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n')) - (s.back() == '\n' ? 1 : 0);
}

std::string triggerDetail(const Script& sc) {
    if (sc.event == "Cyclique") return "toutes les " + hmi::formatDuration(sc.periodMs / 1000.0);
    if (sc.event == "Changement") return sc.watch.empty() ? std::string("(expression manquante)") : "quand " + sc.watch + " change";
    if (sc.event == "Demarrage") return "au lancement de l'IHM";
    if (sc.event == "Appel") return "par une action ou IHM_APPELER";
    return {};
}

// 1.10 : les erreurs d'un script ST a leur place (noms, membres, appels,
// ecritures, types) - les memes que Compiler, pendant la frappe. Les
// problemes de chemins IHM (lot 16) deja dits a la meme ligne ne sont pas
// repetes.
std::string upperOf(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
std::vector<hmi::ScriptDiagnostic> placedDiagnostics(const hmi::Project& p, const Script& sc, const hmi::View* v,
                                                     std::vector<hmi::scriptcheck::Finding>* fixes = nullptr) {
    if (sc.lang != ScriptLang::ST) return {};
    const auto plc = assist::sourcesFor(nullptr).plc();     // le programme de l'automate (vide : pas d'automate)
    return placedScriptDiagnostics(p, sc.body, v, nullptr, plc.get(), fixes, &sc.decls);   // 1.11.18 : ses declarations du modele
}
// Le diagnostic complet d'un script : la syntaxe, les chemins IHM, les erreurs a leur place.
// 1.10 (I2) : `fixes` recoit les constats qui ont une correction proposee.
std::vector<hmi::ScriptDiagnostic> allDiagnostics(const hmi::Project& p, const Script& sc, const hmi::View* v,
                                                  std::vector<hmi::scriptcheck::Finding>* fixes = nullptr) {
    // 1.10 (integration I2) : un type IHM du projet (structure ou enumeration) est un
    // type de locale permis, comme dans Compiler (HmiCheck.cpp, knownType) - sans lui,
    // "m : T_MODE" etait une erreur dans l'editeur et pas dans Compiler.
    const hmi::TypeKnown knownType = [&p](std::string_view t) {
        return !hmi::types::membersOf(p, t).empty() || hmi::findEnumeration(p, t) != nullptr;
    };
    auto d = hmi::checkScript(sc, knownType);                        // 1.11.18 (lot 3) : avec ses declarations du modele
    const auto placed = placedDiagnostics(p, sc, v, fixes);
    if (sc.lang == ScriptLang::ST)                                   // lot 16
        for (const auto& pp : hmi::types::pathProblems(p, hmi::decl::codeOf(sc))) {
            const bool said = std::any_of(placed.begin(), placed.end(), [&](const hmi::ScriptDiagnostic& x) {
                return x.line == pp.line && x.message == pp.message;
            });
            if (!said) d.push_back({hmi::ScriptDiagnostic::Severity::Error, pp.line, pp.message});
        }
    d.insert(d.end(), placed.begin(), placed.end());
    return d;
}

std::string stateOf(const std::vector<hmi::ScriptDiagnostic>& d) {
    std::size_t errors = 0, warnings = 0;
    for (const auto& x : d) {
        errors += x.severity == hmi::ScriptDiagnostic::Severity::Error;
        warnings += x.severity == hmi::ScriptDiagnostic::Severity::Warning;
    }
    if (errors) return std::to_string(errors) + " erreur" + (errors > 1 ? "s" : "");
    if (warnings) return std::to_string(warnings) + " avertissement" + (warnings > 1 ? "s" : "");
    return "OK";
}

// Le libelle du declencheur d'un script general, et l'inverse.
const std::vector<std::string>& generalEventLabels() {
    static const std::vector<std::string> l = {"D\xC3\xA9marrage", "Cyclique", "Sur changement", "Appel\xC3\xA9"};
    return l;
}
std::string eventFromLabel(const std::string& label) {
    for (std::size_t i = 0; i < std::size(hmi::kGeneralEvents); ++i)
        if (generalEventLabels()[i] == label || hmi::kGeneralEvents[i] == label) return std::string(hmi::kGeneralEvents[i]);
    return {};
}

// 1.10 : la colonne Script des resultats - un script general : son nom ; un
// script de vue : la vue et l'evenement (Vue_Pompes . OnOpen).
std::string scriptWhere(const Script& sc, const hmi::View* v) {
    return v ? v->name + " \xC2\xB7 " + sc.event : sc.name;
}
// ... et pour un constat de Compiler (une fonction IHM, une action).
std::string issueWhere(const hmi::Project& p, const hmi::Issue& i) {
    if (const auto* sc = i.script != kNoId ? p.script(i.script) : nullptr) {
        const Id vid = p.viewOfScript(i.script);
        return scriptWhere(*sc, vid != kNoId ? p.view(vid) : nullptr);
    }
    if (i.category == "Fonction") return "Fonction " + i.property;
    if (i.category == "Action") {
        const auto* v = p.view(i.view);
        const auto* o = v && i.object != kNoId ? v->object(i.object) : nullptr;
        return (v ? v->name : std::string{}) + (o ? "." + o->name : std::string{}) + " \xC2\xB7 " + i.property;
    }
    return i.property;
}

} // namespace

// 1.10 : les fautes d'un code ST a leur place - les memes que Compiler.
std::vector<hmi::ScriptDiagnostic> placedScriptDiagnostics(const hmi::Project& p, std::string_view code, const hmi::View* view,
                                                           const hmi::HmiFunction* function, const domain::Project* plc,
                                                           std::vector<hmi::scriptcheck::Finding>* fixes,
                                                           const std::vector<hmi::Declaration>* decls) {
    std::vector<hmi::ScriptDiagnostic> out;
    hmi::scriptcheck::Scope scope;
    scope.project = &p;
    scope.view = view;
    scope.function = function;
    if (plc) {
        auto names = std::make_shared<std::set<std::string>>();
        for (const auto& gv : plc->variables)
            if (gv.scope == domain::VariableScope::Global) names->insert(upperOf(plc->strings.text(gv.name)));
        scope.plcKnown = [names](std::string_view r) { return names->count(upperOf(r)) > 0; };
        scope.plc = hmiPlcPaths(plc);
    }
    const auto findings = decls ? hmi::scriptcheck::check(scope, code, *decls, function ? hmi::decl::Role::Function : hmi::decl::Role::Script)
                                : hmi::scriptcheck::check(scope, code);
    for (const auto& f : findings) {
        hmi::ScriptDiagnostic d;
        d.severity = f.severity == hmi::scriptcheck::Finding::Severity::Error ? hmi::ScriptDiagnostic::Severity::Error
                                                                              : hmi::ScriptDiagnostic::Severity::Warning;
        d.line = f.line;
        d.message = f.message;
        d.column = f.column;
        d.length = f.length;
        out.push_back(std::move(d));
        if (fixes && f.fixLine > 0) fixes->push_back(f);     // 1.10 (I2) : sa correction proposee
    }
    return out;
}

std::vector<hmi::ScriptDiagnostic> guardedDiagnostics(const std::function<std::vector<hmi::ScriptDiagnostic>()>& compute, int line,
                                                     std::string_view where) noexcept {
    const int n = static_cast<int>(std::min<std::size_t>(where.size(), 60));
    try {
        return compute();
    } catch (const std::exception& e) {
        core::trail::notef(core::trail::Kind::Error, "diagnostic indisponible (%.*s) : %s", n, where.data(), e.what());
    } catch (...) {
        core::trail::notef(core::trail::Kind::Error, "diagnostic indisponible (%.*s) : exception inconnue", n, where.data());
    }
    try {
        hmi::ScriptDiagnostic d;
        d.severity = hmi::ScriptDiagnostic::Severity::Warning;
        d.line = std::max(1, line);
        d.message = kDiagnosticUnavailable;
        return {d};
    } catch (...) {
        return {};
    }
}

std::string diagnosticsTitle(const std::vector<hmi::ScriptDiagnostic>& diagnostics, std::string_view where) {
    int errors = 0, warnings = 0;
    for (const auto& d : diagnostics) {
        errors += d.severity == hmi::ScriptDiagnostic::Severity::Error;
        warnings += d.severity == hmi::ScriptDiagnostic::Severity::Warning;
    }
    if (!errors && !warnings) return "DIAGNOSTICS  \xC2\xB7  aucune erreur dans " + std::string(where);
    std::string t = "DIAGNOSTICS  \xC2\xB7  ";
    if (errors) t += std::to_string(errors) + (errors > 1 ? " erreurs" : " erreur");
    if (warnings) t += std::string(errors ? ", " : "") + std::to_string(warnings) + (warnings > 1 ? " avertissements" : " avertissement");
    return t + " dans " + std::string(where) + " (soulign\xC3\xA9" "s pendant que tu tapes)";
}

std::vector<ui::MultiLineText::Squiggle> squigglesOf(const std::vector<hmi::ScriptDiagnostic>& diagnostics) {
    std::vector<ui::MultiLineText::Squiggle> waves;
    for (const auto& d : diagnostics) {
        if (d.line <= 0 || d.column <= 0 || d.severity == hmi::ScriptDiagnostic::Severity::Info) continue;
        ui::MultiLineText::Squiggle w;
        w.line = static_cast<std::size_t>(d.line - 1);
        w.column = static_cast<std::uint32_t>(d.column - 1);
        w.length = static_cast<std::uint32_t>(std::max(1, d.length));
        w.tone = d.severity == hmi::ScriptDiagnostic::Severity::Error ? ui::Tone::Error : ui::Tone::Warning;
        w.message = d.message;
        // 1.12.2 : Ctrl+. remplace le nom souligne par celui que la faute propose.
        if (const auto at = d.message.find("veux-tu dire "); at != std::string::npos) {
            const auto from = at + 13;
            const auto end = d.message.find(" ?", from);
            if (end != std::string::npos && end > from) {
                auto fix = d.message.substr(from, end - from);
                if (fix.find(' ') == std::string::npos) w.fix = std::move(fix);
            }
        }
        waves.push_back(std::move(w));
    }
    return waves;
}

// 1.11.13 : la cle de build du script choisi, et l'etat que montre la barre.
std::string HmiScriptsPane::buildKey() const {
    const Id sel = selectedScript();
    if (sel == kNoId) return {};
    return std::string(general() ? "script:" : "script-vue:") + std::to_string(sel);
}

void HmiScriptsPane::refreshBuildState() {
    if (!hosts_.buildState || !tools_) return;
    const auto key = buildKey();
    const auto st = key.empty() ? std::pair<std::string, std::string>{"\xE2\x80\x94", "Aucun script choisi."} : hosts_.buildState(key);
    if (st.first == buildStateText_) return;
    buildStateText_ = st.first;
    tools_->setText(TBuildState, st.second.empty() ? std::string("L'\xC3\xA9tat de build du script choisi") : st.second, st.first);
}

HmiScriptsPane::HmiScriptsPane(std::string id, hmi::DocumentPtr doc, Apply apply, Id view)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)), view_(view) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    if (general()) {
        tools->add(TAddSt, HmiGlyph::Plus, "Nouveau script ST (ex\xC3\xA9" "cut\xC3\xA9 en simulation)", "ST");
        tools->add(TAddC, HmiGlyph::Plus, "Nouveau script C (\xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9)", "C");
        tools->add(TAddCpp, HmiGlyph::Plus, "Nouveau script C++ (\xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9)", "C++");
        tools->add(TRename, HmiGlyph::Text, "Renommer le script (les actions qui l'appellent suivent)", "Renommer");
        tools->add(TDelete, HmiGlyph::Delete, "Supprimer le script, ou le dossier choisi (ses scripts remontent d'un cran) - Ctrl+Z le rend",
                   "Supprimer");
        tools->add(TFolder, HmiGlyph::Plus, "Nouveau dossier de scripts (sans effet sur les noms) : glisse des scripts dessus", "Dossier");
        tools->add(TExport, HmiGlyph::Export,
                   "Exporter des scripts g\xC3\xA9n\xC3\xA9raux (.xpgscripts) avec ce dont ils ont besoin : fonctions IHM, types IHM, variables IHM",
                   "Exporter les scripts\xE2\x80\xA6");
        tools->add(TImport, HmiGlyph::Import,
                   "Importer des scripts (ou tout fichier fait par Exporter) d'un autre projet : renommer ou remplacer chaque nom en conflit, "
                   "un seul Ctrl+Z",
                   "Importer\xE2\x80\xA6");
    } else {
        tools->add(TClear, HmiGlyph::Delete, "Vider le script de cet \xC3\xA9v\xC3\xA9nement (Ctrl+Z le rend)", "Vider");
        // 1.11.3 : les scripts d'une vue (popup, symbole, modele, en-tete, pied) voyagent (.xpgst).
        tools->add(TExport, HmiGlyph::Export,
                   "Exporter les scripts de cette vue (OnOpen, OnCycle, OnClose) dans un fichier .xpgst, lisible et modifiable dans "
                   "n'importe quel \xC3\xA9" "diteur",
                   "Exporter\xE2\x80\xA6");
        tools->add(TImport, HmiGlyph::Import,
                   "Importer des scripts (.xpgst ; un .st va dans l'\xC3\xA9v\xC3\xA9nement choisi) : chacun coch\xC3\xA9, remplacer ou "
                   "ajouter \xC3\xA0 la suite, un seul Ctrl+Z",
                   "Importer\xE2\x80\xA6");
    }
    tools->separator();
    // 1.10 (maquette, scene 4) : les resultats ici, sous le code.
    // 1.11.13 : la generation incrementale - Generer, Regenerer, Compiler, Generer et compiler
    // le script choisi (seul ce qui a change est refait) ; son etat a droite.
    tools->add(TBuildGen, HmiGlyph::Refresh, "G\xC3\xA9n\xC3\xA9rer le script choisi (et ce dont il d\xC3\xA9pend, s'il le faut) : rien n'est refait s'il est \xC3\xA0 jour",
               "G\xC3\xA9n\xC3\xA9rer");
    tools->add(TBuildRegen, HmiGlyph::Refresh, "R\xC3\xA9g\xC3\xA9n\xC3\xA9rer le script choisi, m\xC3\xAAme \xC3\xA0 jour", "R\xC3\xA9g\xC3\xA9n\xC3\xA9rer");
    // 1.11.17 (refonte, lot 1) : le script actuel, lui seul ; le projet : IHM > Compiler.
    tools->add(TCompile, HmiGlyph::Code,
               "Compiler le script actuel (F7) : lui seul, ses fautes ici sous le code (un clic y m\xC3\xA8ne) et dans les Diagnostics "
               "du panneau du bas ; le projet entier : IHM > Compiler",
               "Compiler (F7)");
    tools->add(TBuildGenComp, HmiGlyph::Play, "G\xC3\xA9n\xC3\xA9rer et compiler le script choisi (son \xC3\xA9tat dans l'arbre suit)", "G\xC3\xA9n\xC3\xA9rer et compiler");
    tools->add(TBuildState, HmiGlyph::None, "L'\xC3\xA9tat de build du script choisi (un clic : les sorties du build)", "\xE2\x80\x94");
    // 1.10 (decision 15 ; integration I2) : la correction d'un avertissement de S1 -
    // un CASE sur une enumeration, sans ELSE, qui oublie des valeurs.
    tools->add(TFix, HmiGlyph::Plus,
               "Ajouter les valeurs manquantes : une branche vide pour chaque valeur que le CASE ne traite pas, "
               "avant son END_CASE (Ctrl+Z les retire)",
               "Ajouter les valeurs manquantes");
    if (general()) {
        tools->separator();
        tools->add(TAddVar, HmiGlyph::Plus, "Nouvelle variable IHM", "Variable");
        tools->add(TEditVar, HmiGlyph::Text, "Modifier la variable IHM", "Modifier");
        tools->add(TDeleteVar, HmiGlyph::Delete, "Supprimer la variable IHM", "Supprimer");
    }
    // Lot 16 : la Programmation generale range ses scripts dans le premier onglet.
    std::unique_ptr<ScriptsPage> page = general() ? std::make_unique<ScriptsPage>(base + ".page") : nullptr;
    ui::Widget& host = page ? static_cast<ui::Widget&>(*page) : static_cast<ui::Widget&>(*this);
    tools_ = &static_cast<HmiToolStrip&>(host.addChild(std::move(tools)));
    tools_->setEnabledWhen(TRename, [this] { return selectedScript() != kNoId; });
    tools_->setEnabledWhen(TDelete, [this] { return selectedScript() != kNoId || (folders_ && !folders_->selectedFolder().empty()); });
    tools_->setEnabledWhen(TClear, [this] { return selectedScript() != kNoId; });
    tools_->setEnabledWhen(TCompile, [this] { return selectedScript() != kNoId; });     // 1.11.17 : rien a compiler
    tools_->setEnabledWhen(TEditVar, [this] { return selectedVariable() != kNoId; });
    tools_->setEnabledWhen(TDeleteVar, [this] { return selectedVariable() != kNoId; });
    tools_->setVisibleWhen(TFix, [this] { return fixRow() >= 0; });     // 1.10 (I2) : seulement s'il y a a corriger
    for (const int a : {static_cast<int>(TBuildGen), static_cast<int>(TBuildRegen), static_cast<int>(TBuildGenComp), static_cast<int>(TBuildState)}) {   // 1.11.13
        tools_->setVisibleWhen(a, [this] { return static_cast<bool>(hosts_.build); });
        if (a != TBuildState) tools_->setEnabledWhen(a, [this] { return selectedScript() != kNoId; });
    }
    if (general()) {   // 1.11.2 (decision 174) : seulement avec un hote (l'ecran)
        tools_->setVisibleWhen(TExport, [this] { return static_cast<bool>(hosts_.exportItems); });
        tools_->setVisibleWhen(TImport, [this] { return static_cast<bool>(hosts_.importAny); });
    }

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".left");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".scriptsPanel", general() ? "SCRIPTS G\xC3\x89N\xC3\x89RAUX" : "SCRIPTS DE LA VUE");
        auto table = std::make_unique<ui::TableView>(base + ".scripts");
        if (general())
            table->setColumns({{"Nom", 220.f}, {"Langage", 80.f}, {"D\xC3\xA9" "clencheur", 130.f}, {"D\xC3\xA9tail", 230.f},   // lot 21 : Nom plus large (dossiers)
                               {"Lignes", 84.f, 40.f, true, true, true, ui::Align::End}, {"\xC3\x89tat", 120.f}});
        else
            table->setColumns({{"\xC3\x89v\xC3\xA9nement", 210.f}, {"Langage", 100.f},
                               {"Lignes", 84.f, 40.f, true, true, true, ui::Align::End}, {"\xC3\x89tat", 120.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        scripts_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        left->addPane(std::move(panel), general() ? 0.42f : 0.40f, 100.f);
        // Lot 21 : les scripts generaux ranges en dossiers (glisser une ou plusieurs lignes).
        if (general()) folders_ = std::make_unique<HmiFolderTable>(*scripts_, doc_, apply_);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".propsPanel", "PROPRI\xC3\x89T\xC3\x89S DU SCRIPT");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(false);
        grid->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
        grid->setNameColumnRatio(0.42f);
        props_ = &static_cast<ui::PropertyGrid&>(panel->setBody(std::move(grid)));
        left->addPane(std::move(panel), general() ? 0.28f : 0.60f, 90.f);
    }
    {
        auto table = std::make_unique<ui::TableView>(base + ".variables");
        // Lot 16 : les colonnes de la liaison, editables dans la case (double-clic, F2) -
        // les memes donnees que Equipements > Variables liees.
        table->setColumns({{"Nom", 150.f}, {"Type", 110.f}, {"Initiale", 80.f}, {"\xC3\x89quipement", 130.f}, {"Adresse", 90.f},
                           {"Acc\xC3\xA8s", 110.f}, {"Description", 220.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        if (general()) {
            auto panel = std::make_unique<HmiTitledPanel>(base + ".varsPanel", "VARIABLES IHM");
            variables_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
            left->addPane(std::move(panel), 0.30f, 80.f);
        } else {
            // Les scripts d'une vue n'ont pas de variables a eux : le tableau
            // existe (le code est le meme), cache.
            table->setVisibility(ui::Visibility::Collapsed);
            variables_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
        }
    }
    split->addPane(std::move(left), 0.42f, 260.f);

    auto right = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".right");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".editorPanel", "\xC3\x89" "DITEUR");
        auto area = std::make_unique<CodeArea>(base + ".code");
        auto ed = std::make_unique<ui::MultiLineText>(base + ".editor");
        ed->setLanguage(ui::Language::StructuredText);
        ed->setShowLineNumbers(true);
        ed->setReadOnly(false);
        ed->setTabInsertsSpaces(4);
        ed->setCommandKeys(true);            // 1.12.2 : les raccourcis de Visual Studio (accords Ctrl+K...)
        editor_ = &static_cast<ui::MultiLineText&>(area->addChild(std::move(ed)));
        auto bar = std::make_unique<ui::StatusBar>(base + ".symbol");
        bar->setTooltip("Le nom o\xC3\xB9 est le curseur : ce qu'il est, son type, son commentaire.");
        symbolBar_ = &static_cast<ui::StatusBar&>(area->addChild(std::move(bar)));
        // 1.11.18 (refonte, lot 5) : les onglets du code - Code (et le bandeau de l'ancien
        // format), Constantes, Variables : les declarations du modele du script montre.
        banner_ = &static_cast<HmiDeclBanner&>(area->addChild(std::make_unique<HmiDeclBanner>(base + ".declBanner")));
        codeTabs_ = &static_cast<HmiCodeTabs&>(panel->setBody(std::make_unique<HmiCodeTabs>(
            base + ".codeTabs", doc_, apply_, std::move(area), std::vector{hmi::decledit::Tab::Constants, hmi::decledit::Tab::Variables},
            banner_)));
        editorPanel_ = panel.get();
        // 1.11.21 : l'editeur prend toute la hauteur - ses fautes vont au panneau du bas
        // (Diagnostics, l'etape Saisie ; HmiLive.hpp), soulignees dans le code.
        right->addPane(std::move(panel), 1.f, 120.f);
    }
    split->addPane(std::move(right), 0.58f, 300.f);
    split_ = &static_cast<ui::Splitter&>(host.addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(host.addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    if (page) {
        auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
        tabs->addTab({"Scripts g\xC3\xA9n\xC3\xA9raux", ui::Icon::Code}, std::move(page));
        auto vars = std::make_unique<HmiVariablesPane>(base + ".vars", doc_, apply_);
        varsPane_ = vars.get();
        tabs->addTab({"Variables IHM", ui::Icon::Variable}, std::move(vars));
        auto types = std::make_unique<HmiTypesPane>(base + ".types", doc_, apply_);
        typesPane_ = types.get();
        tabs->addTab({"Types IHM", ui::Icon::DerivedType}, std::move(types));
        tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
        disposition::applyTabs(*tabs_, "prog", true);  // 1.12.3 : Projet > Disposition
    }

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedScript();
        switch (a) {
            case TAddSt: case TAddC: case TAddCpp: {
                const ScriptLang lang = a == TAddSt ? ScriptLang::ST : a == TAddC ? ScriptLang::C : ScriptLang::Cpp;
                if (hosts_.newScript) hosts_.newScript(lang);
                else (void)addScript(lang, "Script", "Appel");
                break;
            }
            case TRename: if (sel && hosts_.rename) hosts_.rename(sel); break;
            case TFolder: if (folders_) (void)folders_->newFolder(); break;
            case TExport:
                if (general()) {
                    if (hosts_.exportItems) hosts_.exportItems(sel);   // 1.11.2 (decision 174)
                } else if (const auto* v = doc_->project.view(view_)) {
                    // 1.11.3 : l'explorateur, puis le fichier.
                    if (hmi::scriptfile::fromView(*v).entries.empty()) {
                        say("Rien \xC3\xA0 exporter : les scripts de " + v->name + " sont vides.", true);
                        break;
                    }
                    const std::string name = v->name + "_scripts" + std::string(hmi::scriptfile::kExtension);
                    const std::weak_ptr<char> alive = alive_;
                    if (!ui::browsePath(ui::saveFile("Scripts XPGAnalyser|*.xpgst", ui::pathIn(hmiProjectFolder(), "exports"),
                                                     "Exporter les scripts de " + v->name),
                                        name, [this, alive](std::string path) {
                                            if (alive.expired()) return;
                                            std::string why;
                                            if (exportViewScripts(path, &why)) say("Scripts export\xC3\xA9s : " + path);
                                            else say(why, true);
                                        }))
                        say("Pas d'explorateur de fichiers ici.", true);
                }
                break;
            case TImport:
                if (general()) {
                    if (hosts_.importAny) hosts_.importAny();
                } else {
                    const std::weak_ptr<char> alive = alive_;
                    if (!ui::browsePath(ui::openFile("Scripts XPGAnalyser|*.xpgst;*.st;*.txt", ui::pathIn(hmiProjectFolder(), "exports"),
                                                     "Importer des scripts"),
                                        {}, [this, alive](std::string path) {
                                            if (alive.expired()) return;
                                            std::string why;
                                            if (!importViewScripts(path, &why)) say(why, true);
                                        }))
                        say("Pas d'explorateur de fichiers ici.", true);
                }
                break;
            case TDelete:
                if (!sel && folders_) {
                    if (const auto f = folders_->selectedFolder(); !f.empty()) (void)folders_->deleteFolder(f);
                    break;
                }
                if (!sel) break;
                if (hosts_.remove) hosts_.remove(sel);
                else (void)deleteScript(sel);
                break;
            case TClear: if (sel) (void)deleteScript(sel); break;
            case TCompile: compileCurrent(); break;   // 1.11.17 : le script actuel (ici, et son build)
            case TBuildGen: if (hosts_.build && sel) hosts_.build(hmi::pipeline::Mode::Generate, buildKey()); break;
            case TBuildRegen: if (hosts_.build && sel) hosts_.build(hmi::pipeline::Mode::Regenerate, buildKey()); break;
            case TBuildGenComp: if (hosts_.build && sel) hosts_.build(hmi::pipeline::Mode::GenerateCompile, buildKey()); break;
            case TBuildState: if (hosts_.buildOutputs) hosts_.buildOutputs(); break;
            case TFix:                        // 1.10 (I2) : Ajouter les valeurs manquantes
                if (const int r = fixRow(); r >= 0) (void)applyResultFix(static_cast<std::size_t>(r));
                break;
            case TAddVar:
                if (hosts_.newVariable) hosts_.newVariable();
                else (void)addVariable(hmi::uniqueVariableName(doc_->project, "Variable"), "INT", "0", {});
                break;
            case TEditVar: if (selectedVariable() && hosts_.editVariable) hosts_.editVariable(selectedVariable()); break;
            case TDeleteVar:
                if (!selectedVariable()) break;
                if (hosts_.removeVariable) hosts_.removeVariable(selectedVariable());
                else (void)deleteVariable(selectedVariable());
                break;
            default: break;
        }
    });
    links_ += scripts_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selectedRow_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        compiled_ = false;                 // 1.11.17 : un autre script - le Compiler d'avant ne le concerne pas
        compiledIssues_.clear();
        showSelected();
    });
    links_ += variables_->activated->connect([this](ui::RowIndex) {
        if (selectedVariable() && hosts_.editVariable) hosts_.editVariable(selectedVariable());
    });
    // 1.11.21 : la barre du volet (« 1 erreur(s), soulignee(s) pendant que tu tapes ») : un clic
    // ouvre le panneau du bas, ou elles sont listees.
    status_->setMessageClick([this] { if (hosts_.showDiagnostics) hosts_.showDiagnostics(); });
    links_ += editor_->caretSymbolChanged->connect([this](const std::string&) { updateSymbolLine(); });
    links_ += editor_->textChanged->connect([this](const std::string& text) {
        if (syncing_) return;
        const Id sel = selectedScript();
        if (sel) (void)setBody(sel, text);
        else if (!general() && selectedRow_ >= 0 && static_cast<std::size_t>(selectedRow_) < eventOrder_.size()) {
            // Un evenement sans script : la premiere frappe le cree (ST).
            const std::string event = eventOrder_[static_cast<std::size_t>(selectedRow_)];
            const auto* v = doc_->project.view(view_);
            if (!v) return;
            Id made = kNoId;
            auto cmd = hmi::changeView(doc_, view_, "Script " + event, [&](hmi::Project& p, hmi::View& vv) {
                Script sc;
                sc.id = p.allocate();
                sc.name = vv.name + "_" + event;
                sc.lang = ScriptLang::ST;
                sc.event = event;
                sc.body = text;
                made = sc.id;
                vv.scripts.push_back(std::move(sc));
            }, "viewscript:" + std::to_string(view_) + ":" + event);
            if (cmd) apply_(std::move(cmd));
        }
        updateDiagnostics();
    });
    if (folders_) {
        links_ += folders_->message->connect([this](const std::string& text) { say(text); });
        links_ += folders_->relayout->connect([this] { refresh(); });
    }
    // 1.11.18 (lot 5) : les grilles parlent dans la barre du volet ; leurs utilisations
    // menent au code ; le bandeau migre le script montre.
    links_ += codeTabs_->message->connect([this](const std::string& t, bool warning) { say(t, warning); });
    links_ += codeTabs_->usesRequested->connect([this](const std::string& name) { (void)goToNextUse(name); });
    links_ += codeTabs_->migrateRequested->connect([this] { (void)migrateCurrent(); });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
    if (!scriptOrder_.empty() && !folders_) hmiSelectModelRow(*scripts_, 0);
    if (folders_ && !scriptOrder_.empty()) folders_->selectItem(scriptOrder_.front());
}

std::vector<const Script*> HmiScriptsPane::list() const {
    std::vector<const Script*> out;
    if (general()) {
        for (const auto& sc : doc_->project.programs.scripts) out.push_back(&sc);
    } else if (const auto* v = doc_->project.view(view_)) {
        for (const auto& sc : v->scripts) out.push_back(&sc);
    }
    return out;
}

Id HmiScriptsPane::selectedScript() const {
    if (folders_) return folders_->selectedItem();              // lot 21 : general, rangee en dossiers
    if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= scriptOrder_.size()) return kNoId;
    return scriptOrder_[static_cast<std::size_t>(selectedRow_)];
}

std::string HmiScriptsPane::selectedEvent() const {
    if (general() || selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= eventOrder_.size()) return {};
    return eventOrder_[static_cast<std::size_t>(selectedRow_)];
}

const Script* HmiScriptsPane::current() const { return doc_->project.script(selectedScript()); }

void HmiScriptsPane::refresh() {
    const Id keepScript = selectedScript();
    const std::string keepEvent = selectedEvent();
    const Id keepVar = selectedVariable();
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> states;
    scriptOrder_.clear();
    eventOrder_.clear();
    if (general()) {
        // Lot 21 : la liste rangee en dossiers - ses cases, script par script.
        std::map<Id, std::vector<std::string>> cells;
        std::map<Id, std::string> stateById;
        for (const auto* sc : list()) {
            scriptOrder_.push_back(sc->id);
            const auto d = guardedDiagnostics([&] { return allDiagnostics(doc_->project, *sc, nullptr); }, 1, sc->name);   // 1.10.4 : jamais d'exception
            stateById[sc->id] = stateOf(d);
            cells[sc->id] = {sc->name, std::string(hmi::scriptLangKey(sc->lang)), std::string(hmi::eventLabel(sc->event)),
                             triggerDetail(*sc), std::to_string(lineCount(sc->body)), stateById[sc->id]};
        }
        folders_->rebuild(hmi::fold::List::Scripts, scriptOrder_,
                          {"Nom", "Langage", "D\xC3\xA9" "clencheur", "D\xC3\xA9tail", "Lignes", "\xC3\x89tat"},
                          [&cells](Id id) { return cells[id]; },
                          [&stateById](Id id, std::size_t col) {
                              ui::CellStyle s;
                              if (col == 0) s.icon = ui::Icon::Code;
                              if (col == 5) {
                                  const std::string& st = stateById[id];
                                  s.fgTone = st == "OK" ? ui::Tone::Ok : st.find("erreur") != std::string::npos ? ui::Tone::Error : ui::Tone::Warning;
                              }
                              return s;
                          });
    } else {
        for (const auto ev : hmi::kViewEvents) {
            const Script* found = nullptr;
            for (const auto* sc : list()) if (sc->event == ev) { found = sc; break; }
            eventOrder_.emplace_back(ev);
            scriptOrder_.push_back(found ? found->id : kNoId);
            states.push_back(found ? stateOf(guardedDiagnostics([&] { return allDiagnostics(doc_->project, *found, doc_->project.view(view_)); },
                                                                1, found->name))
                                   : std::string("(vide)"));
            rows.push_back({std::string(hmi::eventLabel(ev)), found ? std::string(hmi::scriptLangKey(found->lang)) : std::string("-"),
                            found ? std::to_string(lineCount(found->body)) : std::string("0"), states.back()});
        }
    }
    const std::size_t stateCol = general() ? 5 : 3;
    if (!general()) {
    scriptModel_ = std::make_shared<Rows>(
        general() ? std::vector<std::string>{"Nom", "Langage", "D\xC3\xA9" "clencheur", "D\xC3\xA9tail", "Lignes", "\xC3\x89tat"}
                  : std::vector<std::string>{"\xC3\x89v\xC3\xA9nement", "Langage", "Lignes", "\xC3\x89tat"},
        std::move(rows), [states, stateCol, this](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 0) s.icon = ui::Icon::Code;
            if (c == stateCol && r < states.size())
                s.fgTone = states[r] == "OK" ? ui::Tone::Ok : states[r] == "(vide)" ? ui::Tone::Muted
                         : states[r].find("erreur") != std::string::npos ? ui::Tone::Error : ui::Tone::Warning;
            (void)this;
            return s;
        });
    scripts_->setModel(scriptModel_);
    }

    std::vector<std::vector<std::string>> vars;
    variableOrder_.clear();
    std::vector<bool> bound;
    for (const auto& var : doc_->project.programs.variables) {
        variableOrder_.push_back(var.id);
        bound.push_back(var.bound());
        vars.push_back({var.name, var.type, var.initial, var.bound() ? var.equipment : std::string("(locale)"),
                        var.bound() ? var.address : std::string("\xE2\x80\x94"),
                        var.bound() ? (var.readOnly ? std::string("lecture seule") : std::string("lecture, \xC3\xA9" "criture")) : std::string("\xE2\x80\x94"),
                        var.description});
    }
    const std::vector<Id> order = variableOrder_;
    variableModel_ = std::make_shared<Rows>(
        std::vector<std::string>{"Nom", "Type", "Initiale", "\xC3\x89quipement", "Adresse", "Acc\xC3\xA8s", "Description"}, std::move(vars),
        [bound](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 0 && r < bound.size()) s.icon = bound[r] ? ui::Icon::LocatedVariable : ui::Icon::Variable;
            if ((c == 3 || c == 4 || c == 5) && r < bound.size() && !bound[r]) s.fgTone = ui::Tone::Muted;
            if (c == 4) s.monospace = true;
            return s;
        },
        [bound](std::size_t r, std::size_t c) { return c == 3 || ((c == 4 || c == 5) && r < bound.size() && bound[r]); },
        [this](std::size_t, std::size_t c) {
            std::vector<std::string> out;
            if (c == 3) {
                out.emplace_back();
                for (const auto& e : doc_->project.equipments)
                    if (e.modbus()) out.push_back(e.name);
            } else if (c == 5) {
                out = {"lecture, \xC3\xA9" "criture", "lecture seule"};
            }
            return out;
        },
        [this, order](std::size_t r, std::size_t c, const std::string& text) {
            if (r >= order.size() || !varsPane_) return false;
            std::string why;
            bool ok = false;
            if (c == 3) ok = varsPane_->setEquipment(order[r], text, &why);
            else if (c == 4) ok = varsPane_->setAddress(order[r], text, &why);
            else if (c == 5) ok = varsPane_->setReadOnly(order[r], text == "lecture seule", &why);
            if (!ok && !why.empty()) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        });
    variables_->setModel(variableModel_);

    // La selection d'avant, retrouvee (un evenement vide reste choisi).
    selectedRow_ = -1;
    if (general()) {
        (void)keepScript;              // lot 21 : la table rangee en dossiers garde sa selection
    } else {
        for (std::size_t i = 0; i < eventOrder_.size(); ++i)
            if (eventOrder_[i] == keepEvent) { hmiSelectModelRow(*scripts_, i); selectedRow_ = static_cast<int>(i); }
    }
    if (keepVar) selectVariable(keepVar);
    showSelected();
    const auto st = doc_->project.statistics();
    (void)st;
    std::string msg = general() ? std::to_string(doc_->project.programs.scripts.size()) + " script(s) g\xC3\xA9n\xC3\xA9raux, "
                                      + std::to_string(doc_->project.programs.variables.size()) + " variable(s) IHM"
                                : std::string("OnOpen \xC3\xA0 l'ouverture de la vue, OnCycle \xC3\xA0 chaque cycle IHM, OnClose \xC3\xA0 sa fermeture");
    status_->setMessage(msg);
    invalidate();
}

void HmiScriptsPane::showSelected() {
    const auto* sc = current();
    syncing_ = true;
    const std::string body = sc ? sc->body : std::string{};
    // 1.12.2 : le meme script dans un autre etat (annuler, retablir) garde la vue et met le
    // curseur au changement ; un autre script s'ouvre en haut.
    if (editor_->text() != body) {
        if (sc && sc->id == shownId_) editor_->reloadText(body);
        else editor_->setText(body);
    }
    shownId_ = sc ? sc->id : hmi::kNoId;
    editor_->setLanguage(languageOf(sc ? sc->lang : ScriptLang::ST));
    const bool editable = sc || !selectedEvent().empty();
    editor_->setReadOnly(!editable);
    syncing_ = false;
    std::string title = "\xC3\x89" "DITEUR";
    if (sc) title += "  \xC2\xB7  " + sc->name + "  \xC2\xB7  " + std::string(hmi::scriptLangKey(sc->lang)) + "  \xC2\xB7  "
                   + std::string(hmi::eventLabel(sc->event));
    else if (!selectedEvent().empty()) title += "  \xC2\xB7  " + selectedEvent() + " (vide : taper pour le cr\xC3\xA9" "er)";
    editorPanel_->setTitle(title);
    rebuildProperties();
    updateDiagnostics();
    refreshDeclarations();
}

// 1.11.18 (refonte, lot 5) : les grilles du script montre, les titres de leurs onglets
// (le nombre, rouge s'il y a une fautive) et le bandeau de l'ancien format.
void HmiScriptsPane::refreshDeclarations() {
    if (!codeTabs_) return;
    const auto at = currentPlace();
    std::string why;
    if (!at)
        why = !general() && !selectedEvent().empty()
                  ? "Cet \xC3\xA9v\xC3\xA9nement n'a pas encore de script : tape du code dans l'onglet Code pour le cr\xC3\xA9" "er, puis d\xC3\xA9" "clare ici."
                  : std::string("Choisis un script.");
    codeTabs_->setPlace(at, why);
}

std::optional<hmi::decledit::Place> HmiScriptsPane::currentPlace() const {
    const auto* sc = current();
    if (!sc) return std::nullopt;
    hmi::decledit::Place at;
    at.kind = general() ? hmi::decledit::Place::Kind::Script : hmi::decledit::Place::Kind::ViewScript;
    at.view = general() ? kNoId : view_;
    at.id = sc->id;
    at.label = general() ? "script " + sc->name : sc->name;
    return at;
}

void HmiScriptsPane::showCodeTab(std::size_t tab) {
    if (codeTabs_ && tab < codeTabs_->tabCount()) codeTabs_->setCurrentIndex(tab);
}

std::size_t HmiScriptsPane::currentCodeTab() const noexcept { return codeTabs_ ? codeTabs_->currentIndex() : 0; }

bool HmiScriptsPane::migrateCurrent() {
    const auto at = currentPlace();
    if (!at) {
        say("Migrer : aucun script choisi.", true);
        return false;
    }
    std::string report;
    const bool ok = migrateOne(doc_, apply_, *at, &report);
    say(report, !ok);
    return ok;
}

bool HmiScriptsPane::goToNextUse(const std::string& name) {
    showCodeTab(CodeTabCode);
    std::string said;
    const bool ok = selectNextUse(*editor_, name, &said);
    say(said, !ok);
    if (ok) updateSymbolLine();
    return ok;
}

bool HmiScriptsPane::showDeclaration(const std::string& name) { return codeTabs_->showDeclaration(name); }

void HmiScriptsPane::rebuildProperties() {
    using PG = ui::PropertyGrid;
    std::vector<PG::Category> cats;
    const auto* sc = current();
    if (!sc) { props_->setCategories({}); return; }
    PG::Category c;
    c.name = "Script";
    const Id id = sc->id;
    const auto prop = [](std::string name, std::string value, PG::ValueType t, std::vector<std::string> choices,
                         std::function<bool(std::string_view)> commit, std::string help = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = commit ? t : PG::ValueType::ReadOnly;
        p.enumValues = std::move(choices);
        p.commit = std::move(commit);
        p.description = std::move(help);
        return p;
    };
    if (general())
        c.properties.push_back(prop("Nom", sc->name, PG::ValueType::Text, {},
                                    [this, id](std::string_view v) { return renameScript(id, std::string(v)); }));
    c.properties.push_back(prop("Langage", std::string(hmi::scriptLangKey(sc->lang)), PG::ValueType::Enum, {"ST", "C", "C++"},
                                [this, id](std::string_view v) {
                                    const auto l = hmi::scriptLangFromKey(v);
                                    return l && setLanguage(id, *l);
                                },
                                "ST s'ex\xC3\xA9" "cute en simulation ; C et C++ sont \xC3\xA9" "dit\xC3\xA9s et v\xC3\xA9rifi\xC3\xA9s."));
    if (general()) {
        const Script copy = *sc;
        c.properties.push_back(prop("D\xC3\xA9" "clencheur", std::string(hmi::eventLabel(sc->event)), PG::ValueType::Enum,
                                    generalEventLabels(),
                                    [this, copy](std::string_view v) {
                                        const auto ev = eventFromLabel(std::string(v));
                                        return !ev.empty() && setTrigger(copy.id, ev, copy.periodMs, copy.watch);
                                    }));
        if (sc->event == "Cyclique")
            c.properties.push_back(prop("P\xC3\xA9riode (ms)", std::to_string(sc->periodMs), PG::ValueType::Integer, {},
                                        [this, copy](std::string_view v) {
                                            double ms = 0;
                                            return hmi::parseNumber(v, ms) && ms >= 10
                                                && setTrigger(copy.id, copy.event, static_cast<int>(ms), copy.watch);
                                        }));
        if (sc->event == "Changement")
            c.properties.push_back(prop("Expression surveill\xC3\xA9" "e", sc->watch, PG::ValueType::Text, {},
                                        [this, copy](std::string_view v) {
                                            return setTrigger(copy.id, copy.event, copy.periodMs, std::string(v));
                                        }));
        c.properties.push_back(prop("Description", sc->description, PG::ValueType::Text, {},
                                    [this, id](std::string_view v) {
                                        auto cmd = hmi::changeProject(doc_, "Description du script", [&](hmi::Project& p) {
                                            if (auto* s = p.script(id)) s->description = std::string(v);
                                        });
                                        if (cmd) apply_(std::move(cmd));
                                        return true;
                                    }));
    } else {
        c.properties.push_back(prop("\xC3\x89v\xC3\xA9nement", sc->event, PG::ValueType::ReadOnly, {}, nullptr));
    }
    c.properties.push_back(prop("Lignes", std::to_string(lineCount(sc->body)), PG::ValueType::ReadOnly, {}, nullptr));
    cats.push_back(std::move(c));
    props_->setCategories(std::move(cats));
}

void HmiScriptsPane::updateDiagnostics() {
    diagnostics_.clear();
    const auto* sc = current();
    // Lot 16 : les structures et tableaux IHM (un indice constant hors des bornes,
    // un membre inconnu, une propriete ecrite), en tapant. 1.10 : et les erreurs
    // a leur place (nom inconnu, membre, fonction, ecriture interdite, type) -
    // les memes que Compiler, sans l'attendre.
    std::vector<hmi::scriptcheck::Finding> fixes;     // 1.10 (I2) : les corrections proposees (S1)
    // 1.10.4 : rien ne sort d'un diagnostic (le plantage de la 1.10.3) - l'exception
    // va au journal interne et la ligne du curseur dit "diagnostic indisponible".
    if (sc) {
        const int line = editor_ ? static_cast<int>(editor_->caretLine()) + 1 : 1;
        diagnostics_ = guardedDiagnostics(
            [&] { return allDiagnostics(doc_->project, *sc, general() ? nullptr : doc_->project.view(view_), &fixes); }, line, sc->name);
    }
    // 1.10 : les lignes du tableau du bas - le script montre, a la place de ses
    // constats du dernier Compiler (ils suivent la frappe) ; les autres scripts
    // tels que Compiler les a trouves, dans son ordre.
    results_.clear();
    const auto mine = [&] {
        for (const auto& d : diagnostics_) {
            ResultRow r;
            r.d = d;
            r.where = sc ? scriptWhere(*sc, general() ? nullptr : doc_->project.view(view_)) : std::string{};
            r.script = sc ? sc->id : kNoId;
            r.here = true;
            for (const auto& f : fixes)                 // 1.10 (I2) : sa correction proposee
                if (f.line == d.line && f.column == d.column && f.message == d.message) {
                    r.fixLabel = f.fixLabel;
                    r.fixLine = f.fixLine;
                    r.fixText = f.fixText;
                }
            results_.push_back(std::move(r));
        }
    };
    bool placed = false;
    if (compiled_) {
        for (const auto& i : compiledIssues_) {
            if (sc && i.script == sc->id) {
                if (!placed) { mine(); placed = true; }
                continue;
            }
            ResultRow r;
            r.d.severity = i.severity == hmi::Issue::Severity::Error ? hmi::ScriptDiagnostic::Severity::Error
                         : i.severity == hmi::Issue::Severity::Warning ? hmi::ScriptDiagnostic::Severity::Warning
                                                                       : hmi::ScriptDiagnostic::Severity::Info;
            r.d.line = i.line;
            r.d.column = i.column;
            r.d.length = i.length;
            r.d.message = i.message;
            r.where = issueWhere(doc_->project, i);
            // Un script de ce volet : y aller ici (general : les scripts generaux ;
            // une vue : les siens).
            if (const auto* other = i.script != kNoId ? doc_->project.script(i.script) : nullptr) {
                const bool ours = general() ? doc_->project.viewOfScript(i.script) == kNoId : doc_->project.viewOfScript(i.script) == view_;
                if (ours) r.script = other->id;
            }
            r.issue = i;
            results_.push_back(std::move(r));
        }
    }
    if (!placed) {
        // Le script montre d'abord (Compiler n'y avait rien trouve, ou pas encore lance).
        std::vector<ResultRow> others = std::move(results_);
        results_.clear();
        mine();
        for (auto& r : others) results_.push_back(std::move(r));
    }
    ++liveRev_;                                   // 1.11.21 : le panneau du bas les relira
    std::vector<std::pair<std::size_t, gfx::Color>> marks;
    for (const auto& d : diagnostics_)
        // 1.10 : une faute a sa place est soulignee (plus bas) ; la ligne
        // entiere n'est marquee que pour une faute sans colonne (la syntaxe).
        if (d.line > 0 && d.column <= 0 && d.severity != hmi::ScriptDiagnostic::Severity::Info)
            marks.emplace_back(static_cast<std::size_t>(d.line - 1),
                               d.severity == hmi::ScriptDiagnostic::Severity::Error ? gfx::Color{231, 76, 60, 255}
                                                                                    : gfx::Color{241, 196, 15, 255});
    editor_->setMarkedLines(std::move(marks));
    // 1.10 : chaque faute a sa place soulignee pendant la frappe (rouge : erreur,
    // orange : avertissement), son numero de ligne teinte, son message dans
    // l'infobulle - les memes que Compiler, sans l'attendre.
    editor_->setSquiggles(squigglesOf(diagnostics_));
    std::size_t errors = 0, warnings = 0;
    for (const auto& d : diagnostics_) {
        errors += d.severity == hmi::ScriptDiagnostic::Severity::Error;
        warnings += d.severity == hmi::ScriptDiagnostic::Severity::Warning;
    }
    if (sc) {
        // 1.11.21 : la barre compte les fautes ; le panneau du bas les liste (un clic l'ouvre).
        std::string said = errors ? std::to_string(errors) + " erreur(s), soulign\xC3\xA9" "e(s) pendant que tu tapes"
                         : warnings ? std::to_string(warnings) + " avertissement(s), soulign\xC3\xA9" "(s)"
                                    : std::string("aucune erreur");
        if (errors || warnings) said += " \xE2\x80\x94 la liste : panneau du bas, Diagnostics (clic ici)";
        statusText_ = std::to_string(lineCount(sc->body)) + " ligne(s)  \xC2\xB7  " + std::string(hmi::scriptLangKey(sc->lang)) + "  \xC2\xB7  " + said;
        status_->setMessage(statusText_, errors || warnings ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
    }
}

// ---- 1.11.21 : les diagnostics du script montre, au panneau du bas (HmiLive.hpp) ----
std::vector<hmi::pipeline::Diagnostic> HmiScriptsPane::liveDiagnostics() const {
    std::vector<hmi::pipeline::Diagnostic> out;
    const auto* sc = current();
    if (!sc) return out;
    const std::string where = scriptWhere(*sc, general() ? nullptr : doc_->project.view(view_));
    for (const auto& r : results_) {
        if (!r.here) continue;
        auto x = liveDiagnostic(r.d, buildKey(), where, "Script",
                                r.fixLabel.empty() ? std::string{} : r.fixLabel + " (l'outil du volet)");
        x.script = sc->id;
        x.view = general() ? hmi::kNoId : view_;
        x.property = sc->name;
        out.push_back(std::move(x));
    }
    return out;
}

void HmiScriptsPane::goToLive(const hmi::pipeline::Diagnostic& d) {
    const auto* sc = current();
    if (!sc || d.script != sc->id) {
        if (d.script != hmi::kNoId) goTo(d.script, d.line, d.column, d.length);
        return;
    }
    // Une faute a sa place : le curseur dessus, ses caracteres selectionnes ; la faute d'une
    // declaration (ligne 0) : sa ligne, dans son onglet.
    if (d.line > 0) showCodeTab(CodeTabCode);
    if (d.line > 0 && d.column > 0)
        editor_->selectRange(static_cast<std::size_t>(d.line - 1), static_cast<std::uint32_t>(d.column - 1),
                             static_cast<std::uint32_t>(std::max(0, d.length)));
    else if (d.line > 0)
        editor_->goToLine(static_cast<std::size_t>(d.line - 1));
    else if (const auto name = declarationNamed(d.message); !name.empty())
        (void)showDeclaration(name);
    updateSymbolLine();
}

// 1.10 (maquette, scene 4) : Compiler sans quitter l'editeur. 1.11.17 (refonte des
// scripts, lot 1, spec. 13) : le SCRIPT ACTUEL seulement (CompileFocus) - avant, tous les
// scripts du projet ; le projet entier reste a IHM > Compiler.
std::size_t HmiScriptsPane::compileHere() {
    const Id sel = selectedScript();
    const auto* sc = sel != kNoId ? doc_->project.script(sel) : nullptr;
    if (!sc) {
        say("Compiler : aucun script choisi (le projet entier : IHM > Compiler).", true);
        return 0;
    }
    const auto plc = assist::sourcesFor(nullptr).plc();     // le programme de l'automate (vide : pas d'automate)
    hmi::NameExists names;
    if (plc) {
        auto known = std::make_shared<std::set<std::string>>();
        for (const auto& gv : plc->variables)
            if (gv.scope == domain::VariableScope::Global) known->insert(upperOf(plc->strings.text(gv.name)));
        names = [known](std::string_view r) { return known->count(upperOf(r)) > 0; };
    }
    hmi::CompileFocus focus;
    focus.scripts.insert(sel);
    compiledIssues_.clear();
    for (auto& i : hmi::compileWith(doc_->project, names, hmiPlcPaths(plc.get()), focus))
        if (i.script == sel && i.severity != hmi::Issue::Severity::Info) compiledIssues_.push_back(std::move(i));
    compiled_ = true;
    updateDiagnostics();
    const std::size_t n = compiledIssues_.size();
    say("Compiler le script " + sc->name + " : "
            + (n ? std::to_string(n) + (n > 1 ? " fautes (un clic sur un r\xC3\xA9sultat y m\xC3\xA8ne)" : " faute (un clic sur le r\xC3\xA9sultat y m\xC3\xA8ne)")
                 : std::string("aucune faute"))
            + " \xC2\xB7 le projet entier : IHM > Compiler",
        n > 0);
    return n;
}

void HmiScriptsPane::compileCurrent() {
    if (selectedScript() == kNoId) {
        say("Compiler : aucun script choisi (le projet entier : IHM > Compiler).", true);
        return;
    }
    (void)compileHere();
    if (hosts_.build) hosts_.build(hmi::pipeline::Mode::Compile, buildKey());   // 1.11.13 : son etat ; 1.11.17 : ses Diagnostics
}

// 1.10 (decision 15 ; integration I2) : la correction proposee - la ligne choisie
// dans le tableau du bas si elle en a une, sinon la premiere du script montre.
int HmiScriptsPane::fixRow() const {
    // 1.11.21 : plus de tableau sous l'editeur - celle de la ligne du curseur, sinon la premiere.
    const int caret = editor_ ? static_cast<int>(editor_->caretLine()) + 1 : 0;
    for (std::size_t i = 0; i < results_.size(); ++i)
        if (results_[i].here && results_[i].fixLine > 0 && results_[i].d.line == caret) return static_cast<int>(i);
    for (std::size_t i = 0; i < results_.size(); ++i)
        if (results_[i].here && results_[i].fixLine > 0) return static_cast<int>(i);
    return -1;
}

bool HmiScriptsPane::applyResultFix(std::size_t row) {
    if (row >= results_.size() || !results_[row].here || results_[row].fixLine <= 0) return false;
    const ResultRow r = results_[row];        // une copie : la commande refait le tableau
    const auto* sc = r.script != kNoId ? doc_->project.script(r.script) : nullptr;
    if (!sc) return false;
    hmi::scriptcheck::Finding f;
    f.fixLabel = r.fixLabel;
    f.fixLine = r.fixLine;
    f.fixText = r.fixText;
    std::string body = sc->body;
    if (!hmi::scriptcheck::applyFix(f, body) || body == sc->body) return false;
    const Id id = sc->id;
    const Id owner = doc_->project.viewOfScript(id);
    const std::string name = sc->name;
    const std::string label = r.fixLabel + " (" + name + ")";
    core::CommandPtr cmd = owner == kNoId
        ? hmi::changeProject(doc_, label, [&](hmi::Project& p) { if (auto* s = p.script(id)) s->body = body; })
        : hmi::changeView(doc_, owner, label, [&](hmi::Project&, hmi::View& vv) { for (auto& s : vv.scripts) if (s.id == id) s.body = body; });
    if (!cmd) return false;
    apply_(std::move(cmd));
    const auto n = static_cast<std::size_t>(std::count(r.fixText.begin(), r.fixText.end(), '\n'));
    say(r.fixLabel + " : " + std::to_string(n) + (n > 1 ? " branches ajout\xC3\xA9" "es" : " branche ajout\xC3\xA9" "e")
        + " dans " + name + ", avant la ligne " + std::to_string(r.fixLine) + " (Ctrl+Z les retire)");
    return true;
}

ui::EventResult HmiScriptsPane::onEvent(const ui::InputEvent& ev) {
    // 1.10 : F7 dans l'editeur de scripts - Compiler ici (ailleurs, l'ecran
    // ouvre IHM > Compiler). 1.11.17 : le script actuel, comme le bouton.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F7 && (k->mods.none() || (k->mods.ctrl && !k->mods.shift && !k->mods.alt)) && !k->repeat) {
        compileCurrent();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void HmiScriptsPane::setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                               std::function<bool(std::string_view, std::string&)> live) {
    assist_.hmi = [doc = doc_]() -> const hmi::Project* { return doc ? &doc->project : nullptr; };
    assist_.plc = std::move(plc);
    assist_.live = std::move(live);
    assist_.declarations = [this] {                                        // 1.11.18 (lot 3) : ses constantes et variables du modele
        const auto* sc = doc_ ? doc_->project.script(selectedScript()) : nullptr;
        return sc && sc->lang == ScriptLang::ST ? assist::declarationsPrefix(sc->decls, hmi::decl::Role::Script) : std::string{};
    };
    assist::attach(*editor_, assist_);
    updateSymbolLine();
}

void HmiScriptsPane::updateSymbolLine() {
    if (!symbolBar_) return;
    // 1.10 : le curseur sur une faute soulignee : son message (l'infobulle de la barre).
    {
        const std::string all = editor_->text();
        const std::size_t at = std::min(editor_->caretOffset(), all.size());
        std::size_t lineStart = 0;
        if (at > 0)
            if (const auto nl = all.rfind('\n', at - 1); nl != std::string::npos) lineStart = nl + 1;
        const int line = static_cast<int>(editor_->caretLine()) + 1;
        const int column = static_cast<int>(at - lineStart) + 1;
        for (const auto& d : diagnostics_)
            if (d.line == line && d.column > 0 && column >= d.column && column <= d.column + d.length) {
                const bool error = d.severity == hmi::ScriptDiagnostic::Severity::Error;
                symbolBar_->setMessage((error ? "Erreur : " : "Avertissement : ") + d.message,
                                       error ? gfx::Color{231, 76, 60, 255} : gfx::Color{241, 196, 15, 255});
                return;
            }
    }
    const auto symbol = editor_->symbolAtCaret();
    if (!assist_.hmi || symbol.empty() || editor_->language() != ui::Language::StructuredText) {
        symbolBar_->setMessage({});
        return;
    }
    // Dans un commentaire ou une chaine, un mot n'est pas un nom (sauf entre
    // accolades dans IHM_JOURNAL : c'est une variable).
    const std::string text = editor_->text();
    const auto at = std::min(editor_->caretOffset(), text.size());
    std::size_t start = at;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '_'
                         || text[start - 1] == '.' || text[start - 1] == '[' || text[start - 1] == ']'))
        --start;
    const auto where = assist::locate(std::string_view(text).substr(0, start));
    if (where.context != assist::Context::Code && where.context != assist::Context::Placeholder
        && where.context != assist::Context::Member) {
        symbolBar_->setMessage({});
        return;
    }
    const auto* hp = assist_.hmi();
    if (!hp) return;
    const auto plc = assist_.plc ? assist_.plc() : nullptr;
    const auto d = assist::describe(*hp, plc.get(), symbol, (assist_.declarations ? assist_.declarations() : std::string{}) + text);
    // Un mot-cle n'a rien a dire ; un nom en cours de frappe (la liste est
    // ouverte) n'est pas encore "inconnu".
    if (d.keyword || (!d.found && editor_->completionOpen())) { symbolBar_->setMessage({}); return; }
    std::string line = d.line;
    gfx::Color accent{0, 0, 0, 0};
    if (!d.found) accent = gfx::Color{232, 196, 111, 255};                 // ambre : le nom ne se resout pas
    std::string value;
    if (d.found && assist_.live && assist_.live(symbol, value)) {
        line += "   = " + value;
        accent = gfx::Color{127, 208, 138, 255};                           // vert : la valeur du moment
    }
    symbolBar_->setMessage(std::move(line), accent);
}

void HmiScriptsPane::selectScript(Id id) {
    if (folders_) {
        folders_->selectItem(id);
        showSelected();
        return;
    }
    for (std::size_t i = 0; i < scriptOrder_.size(); ++i)
        if (scriptOrder_[i] == id && id) {
            hmiSelectModelRow(*scripts_, i);
            selectedRow_ = static_cast<int>(i);
            showSelected();
            return;
        }
}

void HmiScriptsPane::selectEvent(std::string_view event) {
    for (std::size_t i = 0; i < eventOrder_.size(); ++i)
        if (eventOrder_[i] == event) {
            hmiSelectModelRow(*scripts_, i);
            selectedRow_ = static_cast<int>(i);
            showSelected();
            return;
        }
}

void HmiScriptsPane::goTo(Id script, int line) {
    // 1.11.16 : aller a un script (un diagnostic, la Console) montre l'onglet Scripts generaux,
    // meme si la Programmation generale etait sur Variables IHM ou Types IHM.
    showTab(TabScripts);
    selectScript(script);
    if (line > 0) {
        showCodeTab(CodeTabCode);             // 1.11.18 (lot 5) : le code, pas une grille
        editor_->goToLine(static_cast<std::size_t>(line - 1));
    }
}

// 1.10 : un constat de Compiler a sa colonne : le script, la ligne, les
// caracteres de la faute selectionnes (le nom inconnu, l'appel...).
void HmiScriptsPane::goTo(Id script, int line, int column, int length) {
    if (column <= 0) { goTo(script, line); return; }
    showTab(TabScripts);   // 1.11.16 (voir goTo)
    selectScript(script);
    if (line <= 0) return;
    showCodeTab(CodeTabCode);                 // 1.11.18 (lot 5)
    editor_->selectRange(static_cast<std::size_t>(line - 1), static_cast<std::uint32_t>(column - 1),
                         static_cast<std::uint32_t>(std::max(0, length)));
    updateSymbolLine();
}

// ---- 1.11.3 : exporter et importer les scripts d'une vue ----
bool HmiScriptsPane::exportViewScripts(const std::string& path, std::string* why) {
    const auto* v = doc_->project.view(view_);
    if (!v) { if (why) *why = "La vue n'existe plus."; return false; }
    const auto file = hmi::scriptfile::fromView(*v);
    if (file.entries.empty()) { if (why) *why = "Rien \xC3\xA0 exporter : les scripts de " + v->name + " sont vides."; return false; }
    return hmi::scriptfile::save(file, path, why);
}

bool HmiScriptsPane::importViewScripts(const std::string& path, std::string* why) {
    const auto* v = doc_->project.view(view_);
    if (!v) { if (why) *why = "La vue n'existe plus."; return false; }
    hmi::scriptfile::File f;
    if (!hmi::scriptfile::load(path, f, why)) return false;
    if (f.genre != hmi::scriptfile::Genre::ViewScripts) {
        if (why) *why = "Ce fichier contient des op\xC3\xA9rateurs (de " + f.source + ") : importe-le dans les Op\xC3\xA9rateurs d'un symbole ou d'un type IHM.";
        return false;
    }
    // Un .st sans bloc : un seul script, pour l'evenement choisi.
    if (f.plain && !f.entries.empty()) f.entries.front().event = selectedEvent().empty() ? std::string("OnOpen") : selectedEvent();
    const auto states = hmi::scriptfile::compareView(f, *v);
    app::HmiAskDialog::Spec spec;
    spec.id = "dialog.importViewScripts";
    spec.title = "Importer des scripts dans " + v->name;
    spec.text = (f.plain ? "Le fichier est un seul script : il va dans " + f.entries.front().event + "."
                         : "Le fichier vient de " + (f.source.empty() ? std::string("?") : f.source)
                               + (f.role.empty() ? std::string{} : " (" + f.role + ")") + " : coche les scripts \xC3\xA0 prendre.");
    spec.listTitle = "SCRIPTS DU FICHIER";
    bool different = false;
    for (std::size_t i = 0; i < f.entries.size(); ++i) {
        const auto st = i < states.size() ? states[i] : hmi::scriptfile::State::New;
        different = different || st == hmi::scriptfile::State::Different;
        spec.items.push_back({f.entries[i].event, hmi::scriptfile::describe(f, i, st, v), st != hmi::scriptfile::State::Same});
    }
    if (different) {
        spec.options.push_back({"Remplacer le script de la vue", "le code du fichier prend sa place", {}, false, {}, {}});
        spec.options.push_back({"Ajouter \xC3\xA0 la suite", "le code du fichier va apr\xC3\xA8s celui de la vue, sous un commentaire", {}, false, {}, {}});
    }
    spec.confirm = "Importer";
    spec.confirmLabel = [](const std::vector<bool>& items, const std::vector<bool>&, int) {
        const auto n = static_cast<std::size_t>(std::count(items.begin(), items.end(), true));
        return n == 0 ? std::string("Rien \xC3\xA0 importer") : "Importer " + std::to_string(n) + " script" + (n > 1 ? "s" : "");
    };
    spec.note = "Un seul Ctrl+Z annule l'import. Les scripts identiques sont d\xC3\xA9" "coch\xC3\xA9s : rien ne change pour eux.";
    auto* manager = menu::MenuManager::instance();
    if (!manager) {   // sans ecran (essais) : tout ce qui est coche, en remplacant
        std::vector<bool> chosen;
        for (const auto& it : spec.items) chosen.push_back(it.checked);
        (void)applyImport(f, chosen, hmi::scriptfile::Mode::Replace);
        return true;
    }
    const std::weak_ptr<char> alive = alive_;
    manager->ShowDialog(std::make_unique<app::HmiAskDialog>(std::move(spec)), [this, alive, f](const menu::DialogResult& r) {
        if (alive.expired() || !r.accepted()) return;
        const auto a = app::HmiAskDialog::parse(r.payload);
        (void)applyImport(f, a.items, a.option == 1 ? hmi::scriptfile::Mode::Append : hmi::scriptfile::Mode::Replace);
    });
    return true;
}

std::size_t HmiScriptsPane::applyImport(const hmi::scriptfile::File& f, const std::vector<bool>& chosen, hmi::scriptfile::Mode mode) {
    std::size_t changed = 0;
    const std::string plainEvent = selectedEvent().empty() ? std::string("OnOpen") : selectedEvent();
    auto cmd = hmi::changeView(doc_, view_, "Importer des scripts", [&](hmi::Project& p, hmi::View& vv) {
        changed = hmi::scriptfile::applyToView(p, vv, f, chosen, mode, plainEvent);
    });
    if (cmd && changed > 0) apply_(std::move(cmd));
    refresh();
    if (changed == 0) say("Rien n'a chang\xC3\xA9 : les scripts choisis sont identiques.");
    else say(std::to_string(changed) + " script" + (changed > 1 ? "s import\xC3\xA9s" : " import\xC3\xA9") + " (Ctrl+Z pour annuler).");
    return changed;
}

void HmiScriptsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

Id HmiScriptsPane::addScript(ScriptLang lang, std::string name, std::string event, std::string* why) {
    Script spec;
    spec.lang = lang;
    spec.name = std::move(name);
    spec.event = std::move(event);
    return addScript(std::move(spec), why);
}

Id HmiScriptsPane::addScript(Script spec, std::string* why) {
    if (!general()) return kNoId;
    if (!hmi::isIdentifier(spec.name)) {
        if (why) *why = "nom invalide : lettres, chiffres et _ (pas de chiffre en t\xC3\xAAte)";
        say("Script refus\xC3\xA9 : nom invalide (lettres, chiffres et _, pas de chiffre en t\xC3\xAAte)", true);
        return kNoId;
    }
    spec.name = hmi::uniqueScriptName(doc_->project, spec.name);
    if (std::find(std::begin(hmi::kGeneralEvents), std::end(hmi::kGeneralEvents), spec.event) == std::end(hmi::kGeneralEvents))
        spec.event = "Appel";
    spec.periodMs = std::max(10, spec.periodMs);
    if (spec.body.empty()) {
        if (spec.lang == ScriptLang::ST) spec.body = "(* " + spec.name + " *)\n";
        else spec.body = "// " + spec.name + " : " + std::string(hmi::scriptLangKey(spec.lang))
                       + ", edite et verifie (non execute en simulation)\n";
    }
    Id made = kNoId;
    const std::string name = spec.name, event = spec.event;
    const ScriptLang lang = spec.lang;
    auto cmd = hmi::changeProject(doc_, "Nouveau script " + name, [&](hmi::Project& p) {
        Script sc = spec;
        sc.id = p.allocate();
        made = sc.id;
        p.programs.scripts.push_back(std::move(sc));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectScript(made);
    say("Script cr\xC3\xA9\xC3\xA9 : " + name + " (" + std::string(hmi::scriptLangKey(lang)) + ", " + std::string(hmi::eventLabel(event)) + ")");
    return made;
}

bool HmiScriptsPane::renameScript(Id id, const std::string& name, std::string* why) {
    const auto* sc = doc_->project.script(id);
    if (!sc || !general()) return false;
    if (!hmi::isIdentifier(name)) { if (why) *why = "nom invalide"; say("Renommage refus\xC3\xA9 : nom invalide", true); return false; }
    const auto* other = doc_->project.generalScript(name);
    if (other && other->id != id) { if (why) *why = "nom d\xC3\xA9j\xC3\xA0 pris"; say("Renommage refus\xC3\xA9 : " + name + " existe d\xC3\xA9j\xC3\xA0", true); return false; }
    const std::string old = sc->name;
    std::size_t followed = 0;
    auto cmd = hmi::changeProject(doc_, "Renommer le script", [&](hmi::Project& p) {
        if (auto* s = p.script(id)) s->name = name;
        // Les actions "Script general" qui l'appelaient suivent.
        for (auto& v : p.views) {
            for (auto& a : v.actions) if (a.operation == hmi::Operation::CallScript && a.target == old) { a.target = name; ++followed; }
            for (auto& o : v.objects)
                for (auto& a : o.actions)
                    if (a.operation == hmi::Operation::CallScript && a.target == old) { a.target = name; ++followed; }
        }
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectScript(id);
    say("Renomm\xC3\xA9 en " + name + (followed ? " : " + std::to_string(followed) + " action(s) mise(s) \xC3\xA0 jour" : std::string{}));
    return true;
}

bool HmiScriptsPane::deleteScript(Id id) {
    const auto* sc = doc_->project.script(id);
    if (!sc) return false;
    const std::string name = sc->name;
    core::CommandPtr cmd;
    if (general()) {
        cmd = hmi::changeProject(doc_, "Supprimer " + name, [&](hmi::Project& p) {
            auto& v = p.programs.scripts;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Script& s) { return s.id == id; }), v.end());
        });
    } else {
        cmd = hmi::changeView(doc_, view_, "Vider " + name, [&](hmi::Project&, hmi::View& vv) {
            vv.scripts.erase(std::remove_if(vv.scripts.begin(), vv.scripts.end(), [&](const Script& s) { return s.id == id; }),
                             vv.scripts.end());
        });
    }
    if (cmd) apply_(std::move(cmd));
    refresh();
    say((general() ? "Supprim\xC3\xA9 : " : "Vid\xC3\xA9 : ") + name + " - Ctrl+Z le rend");
    return true;
}

bool HmiScriptsPane::setBody(Id id, const std::string& body) {
    const auto* sc = doc_->project.script(id);
    if (!sc || sc->body == body) return false;
    const Id owner = doc_->project.viewOfScript(id);
    core::CommandPtr cmd;
    if (owner == kNoId) {
        cmd = hmi::changeProject(doc_, "Saisie dans " + sc->name, [&](hmi::Project& p) {
            if (auto* s = p.script(id)) s->body = body;
        }, "script:" + std::to_string(id));
    } else {
        cmd = hmi::changeView(doc_, owner, "Saisie dans " + sc->name, [&](hmi::Project&, hmi::View& vv) {
            for (auto& s : vv.scripts) if (s.id == id) s.body = body;
        }, "viewscript:" + std::to_string(owner) + ":" + sc->event);
    }
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiScriptsPane::setTrigger(Id id, const std::string& event, int periodMs, const std::string& watch) {
    if (!doc_->project.script(id) || !general()) return false;
    auto cmd = hmi::changeProject(doc_, "D\xC3\xA9" "clencheur du script", [&](hmi::Project& p) {
        if (auto* s = p.script(id)) {
            s->event = event;
            s->periodMs = std::max(10, periodMs);
            s->watch = watch;
        }
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiScriptsPane::setLanguage(Id id, ScriptLang lang) {
    const auto* sc = doc_->project.script(id);
    if (!sc) return false;
    const Id owner = doc_->project.viewOfScript(id);
    core::CommandPtr cmd = owner == kNoId
        ? hmi::changeProject(doc_, "Langage du script", [&](hmi::Project& p) { if (auto* s = p.script(id)) s->lang = lang; })
        : hmi::changeView(doc_, owner, "Langage du script", [&](hmi::Project&, hmi::View& vv) {
              for (auto& s : vv.scripts) if (s.id == id) s.lang = lang;
          });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return true;
}

Id HmiScriptsPane::selectedVariable() const {
    const auto rows = variables_->selectedModelRows();
    if (rows.empty() || rows.front() >= variableOrder_.size()) return kNoId;
    return variableOrder_[rows.front()];
}

void HmiScriptsPane::selectVariable(Id id) {
    for (std::size_t i = 0; i < variableOrder_.size(); ++i)
        if (variableOrder_[i] == id) { hmiSelectModelRow(*variables_, i); return; }
}

namespace {
bool validVariable(const hmi::Project& p, Id self, const std::string& name, const std::string& type, const std::string& initial,
                   std::string* why) {
    if (!hmi::isIdentifier(name)) { if (why) *why = "nom invalide : lettres, chiffres et _"; return false; }
    if (const auto* other = p.variable(name); other && other->id != self) { if (why) *why = "'" + name + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
    // Lot 16 : un type IHM ou un tableau aussi.
    if (!hmi::types::validType(p, type, why)) return false;
    if (hmi::types::isComposite(type)) {
        const auto f = hmi::types::flatten(p, name, type, initial);
        if (!f.ok()) { if (why) *why = f.error; return false; }
        for (const auto& l : f.leaves)
            if (!l.initial.empty() && !hmi::Expression::compile(l.initial).valid()) {
                if (why) *why = "valeur initiale illisible : " + l.initial;
                return false;
            }
        return true;
    }
    if (!initial.empty()) {
        const auto e = hmi::Expression::compile(initial);
        if (!e.valid()) { if (why) *why = "valeur initiale illisible : " + e.error(); return false; }
    }
    return true;
}
} // namespace

Id HmiScriptsPane::addVariable(const std::string& name, const std::string& type, const std::string& initial,
                               const std::string& description, std::string* why) {
    std::string reason;
    if (!validVariable(doc_->project, kNoId, name, type, initial, &reason)) {
        if (why) *why = reason;
        say("Variable refus\xC3\xA9" "e : " + reason, true);
        return kNoId;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Nouvelle variable " + name, [&](hmi::Project& p) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = hmi::types::normalized(type);
        v.initial = initial;
        v.description = description;
        made = v.id;
        p.programs.variables.push_back(std::move(v));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectVariable(made);
    say("Variable IHM cr\xC3\xA9\xC3\xA9" "e : " + name + " (" + type + ")");
    return made;
}

bool HmiScriptsPane::updateVariable(Id id, const std::string& name, const std::string& type, const std::string& initial,
                                    const std::string& description, std::string* why) {
    std::string reason;
    if (!validVariable(doc_->project, id, name, type, initial, &reason)) {
        if (why) *why = reason;
        say("Modification refus\xC3\xA9" "e : " + reason, true);
        return false;
    }
    auto cmd = hmi::changeProject(doc_, "Modifier la variable " + name, [&](hmi::Project& p) {
        for (auto& v : p.programs.variables)
            if (v.id == id) { v.name = name; v.type = hmi::types::normalized(type); v.initial = initial; v.description = description; }
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectVariable(id);
    return true;
}

bool HmiScriptsPane::deleteVariable(Id id) {
    std::string name;
    for (const auto& v : doc_->project.programs.variables) if (v.id == id) name = v.name;
    if (name.empty()) return false;
    auto cmd = hmi::changeProject(doc_, "Supprimer la variable " + name, [&](hmi::Project& p) {
        auto& v = p.programs.variables;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::Variable& x) { return x.id == id; }), v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Variable supprim\xC3\xA9" "e : " + name + " - Ctrl+Z la rend");
    return true;
}

void HmiScriptsPane::onLayout() {
    const auto b = bounds();
    if (tabs_) {
        tabs_->setBounds(b);
        return;
    }
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

void HmiScriptsPane::showTab(std::size_t tab) {
    if (tabs_ && tab < tabs_->tabCount()) tabs_->setCurrentIndex(tab);
}

std::size_t HmiScriptsPane::currentTab() const noexcept { return tabs_ ? tabs_->currentIndex() : 0; }

void HmiScriptsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
