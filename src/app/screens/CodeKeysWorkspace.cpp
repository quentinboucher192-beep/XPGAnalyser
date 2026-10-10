// =============================================================================
//  app/screens/CodeKeysWorkspace.cpp - 1.12.2 : les raccourcis de Visual Studio
//  des editeurs de code, cote ecran
// -----------------------------------------------------------------------------
//  L'editeur (ui::MultiLineText avec setCommandKeys) fait seul ce qui touche son
//  texte. Il DEMANDE le reste (MultiLineText::setHostCommandHandler) :
//    - Aller a / Faire... (Ctrl+T, Ctrl+,) : Ctrl+K y commence un accord ;
//    - aller a la definition (F12) : le nom exact dans l'index d'Aller a ;
//    - les references (Maj+F12), rechercher partout (Ctrl+Maj+F) : IHM > Rechercher
//      (mot entier pour les references) ; dans l'API, le filtre de l'arbre ;
//    - renommer partout (F2, Ctrl+R puis Ctrl+R) : le dialogue de renommage ;
//    - generer (Ctrl+Maj+B), demarrer et arreter la simulation (F5, Maj+F5) ;
//    - fermer l'onglet (Ctrl+F4).
//  Ce que l'editeur dit (l'attente d'un accord, "Ligne copiee") va dans la barre
//  d'etat de l'ecran. Le profil (Visual Studio ou classique) se garde dans les
//  reglages de l'edition (editeur.raccourcis) et se change par Aller a.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../GoToSearch.hpp"
#include "../hmi/HmiDesignPanes.hpp"
#include "../../core/Edition.hpp"
#include "../../ui/KeyMap.hpp"

#include <algorithm>
#include <cctype>

namespace app {

namespace {

bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool isUnder(const ui::Widget* w, const ui::Widget* root) {
    for (; w; w = w->parent())
        if (w == root) return true;
    return false;
}

ui::StatusBar::Severity severityOf(ui::Tone t) {
    switch (t) {
        case ui::Tone::Error: return ui::StatusBar::Severity::Error;
        case ui::Tone::Warning: return ui::StatusBar::Severity::Warning;
        case ui::Tone::Ok: return ui::StatusBar::Severity::Success;
        default: return ui::StatusBar::Severity::Info;
    }
}

}   // namespace

void MainAnalysisScreen::installCodeEditorHooks() {
    ui::MultiLineText::setHostCommandHandler(
        [this](ui::MultiLineText& editor, std::string_view command) { return codeEditorCommand(editor, command); });
    ui::MultiLineText::setNoticeSink([this](const std::string& text, ui::Tone tone) {
        if (!status_) return;
        if (text.empty()) { status_->dismissTransient(); return; }
        status_->setTransientMessage(text, 8.0, severityOf(tone));
    });
}

void MainAnalysisScreen::removeCodeEditorHooks() {
    ui::MultiLineText::setHostCommandHandler({});
    ui::MultiLineText::setNoticeSink({});
}

bool MainAnalysisScreen::codeEditorCommand(ui::MultiLineText& editor, std::string_view command) {
    // Un editeur d'une fenetre (le script d'une action) ou d'un onglet detache : l'ecran
    // de derriere ne fait rien a sa place (la touche suit son chemin).
    if (!isUnder(&editor, widgetRoot())) return false;
    const auto say = [this](const std::string& text, ui::StatusBar::Severity s = ui::StatusBar::Severity::Info) {
        if (status_) status_->setTransientMessage(text, 6.0, s);
    };
    const std::string word = editor.symbolAtCaret();
    if (command == "allerA") { openGoTo(); return true; }
    if (command == "fermer") { (void)closeCurrentTab(); return true; }
    if (command == "generer") {
        if (!core::hasIhm()) return false;
        openHmiPane("generer");
        return true;
    }
    if (command == "simuler" || command == "arreter") {
        if (core::hasIhm()) return topBarLot8Action(command == "simuler" ? "hmi.start" : "hmi.stop");
        return topBarLot8Action(command == "simuler" ? "sim.run" : "sim.stop");
    }
    if (command == "rechercherPartout" || command == "references") {
        std::string text = word;
        if (editor.hasSelection()) {
            const auto sel = editor.selectedText();
            if (!sel.empty() && sel.find('\n') == std::string::npos) text = sel;
        }
        const bool whole = command == "references";
        if (core::hasIhm() && app_.hmi()) {
            openHmiPane("rechercher");
            if (auto* find = dynamic_cast<HmiFindPane*>(hmiTab("rechercher"))) {
                find->setFind(text);
                find->setWholeWord(whole);
                find->setScopeView(hmi::kNoId);
                find->search();
            }
            say(whole ? "Toutes les r\xC3\xA9" "f\xC3\xA9rences de \xC2\xAB " + text + " \xC2\xBB (Maj+F12) : IHM \xE2\x80\xBA Rechercher"
                      : "Rechercher \xC2\xAB " + text + " \xC2\xBB dans tout le projet (Ctrl+Maj+F)");
            return true;
        }
        setTreeFilter(text);
        focusTreeFilter();
        return true;
    }
    if (command == "allerDefinition") {
        if (word.empty()) { say("F12 : le curseur n'est pas sur un nom"); return true; }
        // Le nom exact dans l'index d'Aller a (variables, types, scripts, vues...) ;
        // un nom pointe (Moteur.Vitesse) : sa racine.
        std::string name = word;
        const auto bracket = name.find('[');
        if (bracket != std::string::npos) name.erase(bracket);
        std::vector<std::string> tries{ name };
        if (const auto dot = name.find('.'); dot != std::string::npos) tries.push_back(name.substr(0, dot));
        for (const auto& t : tries) {
            const auto found = goToSearchAll(t, -1);
            for (const auto& r : found.results) {
                if (r.group == gotosearch::GPane || r.group == gotosearch::GAction || r.group == gotosearch::GHelp
                    || r.group == gotosearch::GCode)
                    continue;
                if (!sameNoCase(r.title, t) && !sameNoCase(r.title, "SYS." + t)) continue;
                goToResult(r);
                say("D\xC3\xA9" "finition de " + t + " : " + r.groupTitle + " (Ctrl+- : revenir dans le code)");
                return true;
            }
        }
        // L'API : le double clic sur le nom (ce qu'il ouvre, une instance de grafcet...).
        if (core::hasApi()) { editor.symbolActivated->emit(word); return true; }
        say("F12 : \xC2\xAB " + word + " \xC2\xBB n'a pas de d\xC3\xA9" "finition \xC3\xA0 ouvrir (une locale se d\xC3\xA9" "clare dans l'onglet Variables du document ; F1 : la fiche d'une native)");
        return true;
    }
    if (command == "renommer") {
        if (word.empty()) { say("Renommer : le curseur n'est pas sur un nom"); return true; }
        std::string name = word;
        if (const auto dot = name.find_first_of(".["); dot != std::string::npos) name.erase(dot);
        if (const auto doc = app_.hmi()) {
            for (const auto& v : doc->project.programs.variables)
                if (sameNoCase(v.name, name)) { askRename("ihm-variable", v.name); return true; }
            for (const auto& v : doc->project.views)
                if (sameNoCase(v.name, name)) { askRename("vue", v.name); return true; }
        }
        if (core::hasApi() && app_.project()) { askRename("variable", name); return true; }
        say("Renommer : \xC2\xAB " + name + " \xC2\xBB n'est ni une variable IHM ni une vue (une d\xC3\xA9" "claration locale se renomme dans son onglet, F2 sur sa ligne)",
            ui::StatusBar::Severity::Warning);
        return true;
    }
    return false;   // "compiler" : le volet le fait a F7 et Ctrl+F7
}

void MainAnalysisScreen::setEditorKeyProfile(std::string_view key) {
    const auto profile = ui::keymap::profileFromKey(key);
    if (!profile) return;
    ui::keymap::setCurrent(*profile);
    app_.settings().set("editeur.raccourcis", std::string(ui::keymap::profileKey(*profile)));
    app_.settings().save();
    if (status_)
        status_->setTransientMessage(
            *profile == ui::keymap::Profile::VisualStudio
                ? "Raccourcis des \xC3\xA9" "diteurs : Visual Studio (Ctrl+K, Ctrl+C commente ; Ctrl+T : Aller \xC3\xA0 depuis un \xC3\xA9" "diteur)"
                : "Raccourcis des \xC3\xA9" "diteurs : classique (Ctrl+K reste Aller \xC3\xA0, F8 d\xC3\xA9marre la simulation, F12 la capture)",
            8.0, ui::StatusBar::Severity::Success);
}

}   // namespace app
