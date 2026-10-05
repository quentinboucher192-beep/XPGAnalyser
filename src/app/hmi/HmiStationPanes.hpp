// =============================================================================
//  app/hmi/HmiStationPanes.hpp - Configuration > Poste d'exploitation (lot 14)
// -----------------------------------------------------------------------------
//  L'IHM SEULE, SUR LE POSTE DE L'ATELIER. xpg_analyzer --ihm <dossier du
//  projet> ouvre le projet et montre sa vue de demarrage sur tout l'ecran, sans
//  l'editeur (screens/StationScreen). Ce volet regle ce poste :
//
//    a droite    plein ecran, kiosque (fermer la fenetre ne fait rien ; la
//                sortie demande le mot de passe de sortie), la sortie par le
//                coin haut droit (ecran tactile), le curseur cache, le
//                simulateur lance au demarrage ; l'ecran choisi (son numero,
//                sa vue) ;
//    au milieu   Ecrans : l'ecran principal (la vue de demarrage et la
//                navigation) puis chaque ecran secondaire, un moniteur de plus
//                qui montre une vue en direct ; Lancement : la ligne de
//                commande, le lanceur, le demarrage avec la session, la
//                sortie, les moniteurs branches, et ce qui manque.
//
//  LES OUTILS : Essayer le poste (tout de suite, par-dessus l'editeur - la
//  sortie y ramene) ; Ajouter / Retirer un ecran ; Mot de passe de sortie ;
//  Creer le lanceur (lancer-poste.cmd / .sh dans le dossier du projet) ;
//  Demarrer avec la session / Ne plus demarrer.
//
//  Chaque changement du projet est une commande : Ctrl+Z. Le lanceur et le
//  demarrage avec la session sont sur le poste, pas dans le projet.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class HmiStationPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiStationPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Les gestes du volet, pour les boutons, les scripts et les tests.
    // Un reglage : "plein_ecran", "kiosque", "coin", "sans_curseur", "simulateur".
    bool setSetting(const std::string& key, const std::string& value, std::string* why = nullptr);
    // Un ecran secondaire ; `display` 0 : le prochain numero libre ; `view` vide :
    // la premiere vue qu'aucun ecran ne montre.
    bool addScreen(int display = 0, const std::string& view = {}, std::string* why = nullptr);
    bool removeScreen(int display, std::string* why = nullptr);
    // "ecran" (son numero), "vue".
    bool setScreenField(int display, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Le mot de passe de sortie (4 caracteres au moins ; vide : sortie libre).
    bool setExitPassword(const std::string& password, std::string* why = nullptr);
    // Le lanceur dans le dossier du projet.
    bool writeLauncher(std::string* where = nullptr);
    // Demarrer avec la session (sur ce poste, pour cet utilisateur).
    bool setAutostart(bool on, std::string* why = nullptr);
    [[nodiscard]] bool autostart() const;

    // L'ecran choisi dans la table : 1 le principal, 2.. un secondaire, 0 aucun.
    [[nodiscard]] int selectedScreen() const;
    void selectScreen(int display);

    struct Hosts {
        std::function<std::string()> executable;       // le programme (son chemin)
        std::function<std::string()> projectFolder;    // le dossier du projet (vide : pas enregistre)
        std::function<int()>         displays;         // les moniteurs branches
        std::function<void()>        tryStation;       // Essayer le poste
        std::function<void()>        askPassword;      // le dialogue du mot de passe de sortie
        // Lot 15 : le projet est-il FINISH (sinon : why dit son etat) - le
        // demarrage avec le PC l'exige.
        std::function<bool(std::string*)> finished;
    };
    void setHosts(Hosts h);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     screens() noexcept { return *screens_; }
    [[nodiscard]] ui::TableView&     launch() noexcept { return *launch_; }
    [[nodiscard]] ui::TabControl&    tabs() noexcept { return *tabs_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    // Les lignes de l'onglet Lancement ("Ligne de commande : ...").
    [[nodiscard]] const std::vector<std::string>& launchLines() const noexcept { return launchLines_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void refreshLaunch();
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool change(const std::string& label, const std::function<void(hmi::Station&)>& fn);
    [[nodiscard]] std::string projectName() const;
    [[nodiscard]] std::string folder() const;
    [[nodiscard]] std::string exe() const;

    hmi::DocumentPtr       doc_;
    Apply                  apply_;
    Hosts                  hosts_;
    HmiToolStrip*          tools_{nullptr};
    ui::TabControl*        tabs_{nullptr};
    ui::TableView*         screens_{nullptr};
    ui::TableView*         launch_{nullptr};
    ui::PropertyGrid*      grid_{nullptr};
    ui::StatusBar*         status_{nullptr};
    std::shared_ptr<ui::ITableModel> screensModel_, launchModel_;
    std::vector<int>         order_;            // le numero d'ecran de chaque ligne
    std::vector<std::string> launchLines_;
    double                   lastLive_{-10};
    bool                     refreshing_{false};
    std::string              message_;
    core::ConnectionScope    links_;
};

} // namespace app
