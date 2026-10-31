// =============================================================================
//  app/ai/AiWorkspace.cpp - 1.14.2 : l'Assistant IA dans l'ecran du projet
// -----------------------------------------------------------------------------
//  LE VOLET (app/ai/AiPanel) NE FAIT AUCUN GESTE SUR L'APPLICATION : ses hotes
//  sont ici. Il est le quatrieme volet du separateur analysis.upper (IHM Studio),
//  replie au depart ; sa case "Assistant IA (Ctrl+Maj+I)" d'Affichage > Panneaux
//  le montre et le retient, comme les autres panneaux (la Disposition aussi :
//  sa ligne "assistant" de La fenetre).
//
//  Ce que la seance (ai::Assistant) demande a l'application :
//    - le document IHM (App::hmi), la vue de l'onglet ouvert (son HmiEditor) et
//      sa selection - jamais un widget de l'editeur pour le modifier ;
//    - empiler une commande : App::apply, comme l'editeur (un pas d'annulation) ;
//    - les noms de l'automate relie (les globales) : une variable de l'automate
//      existe aussi ; 1.15.0 : leur liste et leur type, pour le contexte du modele ;
//      1.16.0 : leurs commentaires et les types (DDT) de l'automate - l'Assistant
//      en propose des variables et des types IHM (AiAutomate.hpp) ;
//    - 1.16.0 : ouvrir la vue qu'une proposition vient de creer (son onglet) ;
//    - les fenetres (MenuManager::ShowDialog), la barre d'etat, la commande
//      ia.memoire (le centre de memoire, s'il est la).
// =============================================================================
#include "AiAutomate.hpp"
#include "AiPanel.hpp"

#include "../App.hpp"
#include "../BackgroundTasks.hpp"   // 1.16.1 : la cloche (une reponse de l'Assistant arrivee)
#include "../../core/Edition.hpp"
#include "../hmi/HmiCommPanes.hpp"   // 1.18.0 : le scanner (Detecter ce qui repond...)
#include "../hmi/HmiEditor.hpp"
#include "../../hmi/HmiDeclEdit.hpp"   // 1.19.0 : ouvrir un script nomme dans le plan (goToHmiUsage)
#include "../screens/Screens.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/TabArea.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>

namespace app {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// 1.14.2 : LA LARGEUR PAR DEFAUT DU VOLET (les captures a 1280 x 800 : l'inspecteur de l'editeur
// ne montrait plus que des "..."). Le separateur range ses volets en parts : la part du volet est
// calculee pour ai::defaultPanelWidth(fenetre), les autres volets montres gardant les leurs. Tant
// que la part est celle d'origine (0.26, MainAnalysisScreen.cpp) ou celle posee ici, elle suit la
// fenetre ; une autre est une largeur tiree par l'utilisateur : elle reste (et se retient, comme avant).
void fitAssistantWidth(ui::Splitter& split, ai::AiPanel& panel) {
    auto r = split.ratios();
    if (r.size() != 4 || split.children().size() < 4) return;
    float& mine = r[3];
    const bool untouched = std::abs(mine - 0.26f) < 1e-4f || (panel.autoRatio() > 0.f && std::abs(mine - panel.autoRatio()) < 1e-4f);
    if (!untouched) return;
    const float total = split.bounds().w - 15.f;   // trois poignees de 5 px
    if (total < 600.f) return;
    const auto surface = ui::surfaceSize();
    const float want = ai::defaultPanelWidth(surface.w > 0.f ? surface.w : split.bounds().w);
    float others = 0.f;
    for (std::size_t i = 0; i < 3; ++i)
        if (split.children()[i]->visibility() != ui::Visibility::Collapsed) others += r[i];
    const float ratio = want * others / std::max(1.f, total - want);
    if (std::abs(ratio - mine) < 1e-4f) return;
    mine = ratio;
    panel.setAutoRatio(ratio);
    split.setRatios(r);
}

} // namespace

ui::WidgetPtr MainAnalysisScreen::buildAssistantPanel() {
    ai::Assistant::Host core;
    core.document = [this] { return app_.hmi(); };
    // La vue de l'onglet ouvert : celle que l'Assistant modifie.
    const auto editor = [this]() -> HmiEditor* {
        if (!centre_ || centre_->tabCount() == 0) return nullptr;
        return dynamic_cast<HmiEditor*>(centre_->page(centre_->currentIndex()));
    };
    core.activeView = [editor] {
        auto* ed = editor();
        return ed ? ed->viewId() : hmi::kNoId;
    };
    core.selection = [editor] {
        auto* ed = editor();
        return ed ? ed->canvas().selection() : std::vector<hmi::Id>{};
    };
    core.apply = [this](core::CommandPtr c) {
        if (c) app_.apply(std::move(c), /*refreshViews=*/false);
    };
    core.projectName = [this] {
        const auto u = std::filesystem::path(std::u8string(app_.projectFolder().begin(), app_.projectFolder().end())).filename().u8string();
        return std::string(u.begin(), u.end());
    };
    core.projectPath = [this] { return app_.projectFolder(); };
    // Les globales de l'automate relie : une variable de l'automate existe aussi (sans casse). 1.16.0 : seulement
    // dans l'edition qui a l'automate (IHM Studio n'en a pas : son contexte ne dit plus qu'un automate est relie).
    if (core::hasApi())
        core.env.plcName = [this](std::string_view root) {
            const auto plc = app_.project();
            if (!plc) return false;
            const std::string want = upper(root);
            for (const auto& v : plc->variables)
                if (v.scope == domain::VariableScope::Global && upper(plc->strings.text(v.name)) == want) return true;
            return false;
        };
    // 1.15.0 : leurs listes (variables, commentaires, DDT) pour le contexte et les propositions. 1.16.0 (captures) :
    // sans automate relie (IHM Studio), celui d'un export de Control Expert JOINT a la conversation (.XPG) - lu par
    // readPlc, garde le temps de la seance.
    auto plcFile = std::make_shared<ai::PlcFile>();
    // Le .XPG joint passe d'abord (un choix explicite) ; sinon l'automate relie, dans l'edition qui en a un. Dans IHM
    // Studio, app_.project() n'est pas nul (le modele de l'application) mais vide d'automate : on ne le lit pas
    // (seconde passe des captures : le .XPG lu etait ignore).
    ai::fillPlcEnv(core.env, [this, plcFile]() -> std::shared_ptr<const domain::Project> {
        if (plcFile->project) return plcFile->project;
        return core::hasApi() ? app_.project() : nullptr;
    });
    core.env.plcSource = [plcFile] { return plcFile->project ? plcFile->name : std::string{}; };
    core.readPlc = [plcFile](const std::string& path, std::string* said) {
        std::string why;
        auto f = ai::readPlcFile(path, &why);
        if (!f) {
            if (said) *said = why;
            return false;
        }
        *plcFile = std::move(*f);
        if (said)
            *said = std::to_string(plcFile->variables) + (plcFile->variables > 1 ? " variables globales" : " variable globale") + " et "
                    + std::to_string(plcFile->types) + (plcFile->types > 1 ? " types (DDT)" : " type (DDT)");
        return true;
    };
    // 1.16.0 : la vue qu'une proposition vient de creer s'ouvre dans l'editeur.
    core.openView = [this](hmi::Id id) { openHmiView(id); };
    // 1.19.0 : un element nomme dans le plan d'action ou le compte rendu s'ouvre (son onglet, son volet).
    core.reveal = [this](const std::string& kind, const std::string& name, const std::string& view) {
        const auto doc = app_.hmi();
        if (!doc) return false;
        const auto& pr = doc->project;
        if (kind == "objet") {
            const hmi::View* v = view.empty() ? nullptr : pr.viewByName(view);
            if (!v) return false;
            for (const auto& o : v->objects)
                if (o.name == name) {
                    openHmiView(v->id, -1, o.id);
                    return true;
                }
            openHmiView(v->id);
            return true;
        }
        if (kind == "vue" || kind == "symbole" || kind == "popup" || kind == "modele" || kind == "entete" || kind == "pied") {
            const hmi::View* v = pr.viewByName(name);
            if (!v) return false;
            openHmiView(v->id);
            return true;
        }
        if (kind == "variable") {
            const hmi::Variable* v = pr.variable(name);
            if (!v) return false;
            askHmiVariable(v->id);
            return true;
        }
        if (kind == "fonction") {
            const auto* f = pr.functionByName(name);
            if (!f) return false;
            openHmiFunctions(f->id);
            return true;
        }
        if (kind == "script") {
            const auto* sc = pr.generalScript(name);
            if (!sc) return false;
            hmi::decledit::Usage u;
            u.script = sc->id;
            u.line = 1;
            goToHmiUsage(u);
            return true;
        }
        if (kind == "alarme") {
            if (!pr.alarmByName(name)) return false;
            openHmiPane("alarmes");
            return true;
        }
        if (kind == "equipement") {
            if (!pr.equipmentByName(name)) return false;
            openHmiPane("communication");
            return true;
        }
        if (kind == "ressource") {
            if (!pr.resourceByName(name)) return false;
            openHmiPane("ressources");
            return true;
        }
        if (kind == "type") {
            if (!pr.hmiTypeByName(name)) return false;
            openHmiPane("scripts");
            return true;
        }
        return false;
    };

    ai::AiPanel::UiHost ui;
    ui.showDialog = [this](menu::MenuPtr dialog, std::function<void(const menu::DialogResult&)> onClose) {
        app_.menus().ShowDialog(std::move(dialog), std::move(onClose));
    };
    ui.status = [this](const std::string& text) {
        if (status_) status_->setTransientMessage(text, 6.0);
    };
    ui.close = [this] { showAssistant(false); };
    ui.memoryAvailable = [this] { return app_.actions().find("ia.memoire") != nullptr; };
    ui.openMemory = [this] { (void)app_.actions().trigger("ia.memoire", app_.commands()); };
    // 1.18.0 : << Detecter ce qui repond... >> d'une carte appliquee : Communication > Scanner IP, la plage remplie.
    // Le scanner n'est PAS lance : c'est l'utilisateur qui clique Demarrer.
    ui.openScan = [this](const std::string& equipment, const std::string& range, std::string* why) {
        openHmiPane("communication");
        auto* comm = dynamic_cast<HmiCommPane*>(hmiTab("communication"));
        if (!comm) {
            if (why) *why = "L'onglet Communication n'a pas pu s'ouvrir (un projet IHM est-il ouvert ?).";
            return false;
        }
        comm->tabs().setCurrentIndex(static_cast<std::size_t>(HmiCommPane::TScanner));
        if (!range.empty() && !comm->setScanField("plage", range, why)) return false;
        if (status_)
            status_->setTransientMessage("Scanner IP : la plage de \xC2\xAB " + equipment + " \xC2\xBB est remplie ; D\xC3\xA9marrer lance la d\xC3\xA9tection.", 8.0);
        return true;
    };
    // 1.16.1 : << M'avertir des reponses >> - seulement si l'utilisateur ne regarde pas : la fenetre derriere une autre,
    // le panneau replie, ou une autre conversation ouverte. La cloche (Voir ouvre la conversation) ; la fenetre en
    // arriere-plan : son bouton clignote dans la barre des taches, avec le son de notification du systeme.
    ui.replied = [this](const std::string& conversation, const std::string& title, const std::string& excerpt, bool error) {
        const bool focused = app_.windowFocused();
        const auto* cur = aiPanel_ ? aiPanel_->assistant().current() : nullptr;
        if (focused && assistantShown() && cur && cur->id == conversation) return;   // il la voit arriver
        bgtasks::Notice n;
        n.key = "ia.reponse";
        n.group = "Assistant IA";
        n.title = error ? std::string("L'Assistant IA n'a pas pu r\xC3\xA9pondre") : std::string("R\xC3\xA9ponse de l'Assistant IA");
        n.detail = "\xC2\xAB " + title + " \xC2\xBB : " + excerpt;
        n.button = "Voir";
        n.action = "ia.reponse:" + conversation;
        n.tone = error ? "warning" : "info";
        bgtasks::post(std::move(n));
        if (!focused) app_.drawAttention(true);
        if (status_) status_->setTransientMessage(error ? "Assistant IA : la r\xC3\xA9ponse a \xC3\xA9" "chou\xC3\xA9" "e (la cloche)."
                                                       : "Assistant IA : une r\xC3\xA9ponse est arriv\xC3\xA9" "e (la cloche).", 6.0);
    };

    auto panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
    aiPanel_ = panel.get();
    return panel;
}

bool MainAnalysisScreen::assistantShown() const {
    for (const auto& p : panels_)
        if (std::string_view(p.key) == "view.assistant") return p.box->isChecked();
    return false;
}

void MainAnalysisScreen::showAssistant(bool show) {
    if (!aiPanel_) {
        if (status_) status_->setTransientMessage("L'Assistant IA est dans IHM Studio (l'\xC3\xA9" "diteur de vues).", 5.0);
        return;
    }
    for (const auto& p : panels_)
        if (std::string_view(p.key) == "view.assistant" && p.box->isChecked() != show)
            p.box->setState(show ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);   // replie et retient
    if (show) {
        aiPanel_->ensureLoaded();
        aiPanel_->prompt().takeFocus();
        bgtasks::withdraw("ia.reponse");   // 1.16.1 : l'avis de reponse est lu
    }
    if (status_)
        status_->setTransientMessage(show ? "Assistant IA (" + aiPanel_->assistant().connectionText()
                                                + ") : il propose des modifications de la vue ouverte. Ctrl+Maj+I le replie."
                                          : std::string("Assistant IA repli\xC3\xA9 (Ctrl+Maj+I le rouvre)."),
                                     4.0);
}

void MainAnalysisScreen::toggleAssistant() { showAssistant(!assistantShown()); }

// 1.16.1 : << Voir >> sur l'avis d'une reponse : le panneau, ouvert sur cette conversation.
void MainAnalysisScreen::showAssistantReply(const std::string& conversation) {
    showAssistant(true);
    if (!aiPanel_) return;
    (void)aiPanel_->assistant().select(conversation);
    aiPanel_->sync();
}

void MainAnalysisScreen::tickAssistant(double now) {
    if (!aiPanel_) return;
    // Montre au demarrage (la case retenue, la Disposition) : charge a la premiere image.
    if (assistantShown()) aiPanel_->ensureLoaded();
    if (aiPanel_->loaded()) aiPanel_->tick(now);
    if (upper_ && assistantShown()) fitAssistantWidth(*upper_, *aiPanel_);
}

} // namespace app
