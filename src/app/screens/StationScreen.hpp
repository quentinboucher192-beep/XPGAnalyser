// =============================================================================
//  app/screens/StationScreen.hpp - le poste d'exploitation (lot 14)
// -----------------------------------------------------------------------------
//  L'IHM SEULE. xpg_analyzer --ihm <dossier du projet> ouvre le projet et
//  vient ici : la vue de demarrage sur tout l'ecran, sans l'editeur, ni menu,
//  ni barre. L'IHM lit l'automate de Configuration > Communication (Modbus TCP),
//  ou le simulateur, qu'elle met en marche (Configuration > Poste
//  d'exploitation).
//
//  LE KIOSQUE. Fermer la fenetre (Alt+F4) ne fait rien ; la sortie se demande
//  par Ctrl+Alt+Q, ou cinq touchers du coin haut droit en trois secondes, et
//  demande le mot de passe de sortie (empreinte salee, dans le projet) : puis
//  " Passer en conception " (l'editeur, sans fermer) ou " Quitter
//  l'application ". Chaque tentative va au journal de l'IHM.
//
//  LES ECRANS SECONDAIRES. Chaque ecran configure (2, 3...) recoit une fenetre,
//  en plein ecran sur ce moniteur s'il existe (sinon une fenetre ordinaire),
//  qui montre sa vue en direct - avec son propre rendu et son propre cache
//  d'images.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../menu/IMenu.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gfx { class IRenderer; }
namespace hmi { struct ExportRequest; }   // lot API 8

namespace app {

class App;
class HmiSimulationPane;

class StationScreen final : public menu::WidgetMenu {
public:
    explicit StationScreen(App& app);
    ~StationScreen() override;

    ui::EventResult HandleEvent(const ui::InputEvent&) override;
    void            Render(gfx::IRenderer&, const menu::FrameContext&) override;
    // Echap ne sort pas du poste (le kiosque) : seul le dialogue de sortie le fait.
    [[nodiscard]] menu::MenuTraits traits() const override;

    [[nodiscard]] HmiSimulationPane* pane() noexcept { return pane_; }
    // La sortie : le dialogue du mot de passe de sortie.
    void askExit();
    // 1.9 : Ctrl+Alt+S - la page Simulation de Parametres systeme (si la fiche du
    // poste la garde : Station::simPage) ; sans la permission Administrer, la
    // carte du refus (et "Acces refuse" au journal).
    void openSimulationPage();
    // Les ecrans secondaires ouverts, et le rendu de l'ecran `display` (nul :
    // pas ouvert) - pour les captures.
    [[nodiscard]] std::size_t     secondaryCount() const noexcept;
    [[nodiscard]] gfx::IRenderer* screenRenderer(int display) const;
    // Une capture de l'ecran `display` (2, 3...) au prochain dessin ; son
    // resultat ensuite : vide si ecrite, sinon pourquoi (nullopt : pas encore).
    void                                     requestCapture(int display, std::string path);
    [[nodiscard]] std::optional<std::string> captureResult(const std::string& path);

    // ---- Lot API 8 : les exports qui demandent ou ----
    //  Un export de l'IHM qu'un geste de l'operateur a lance (le bouton
    //  d'export, l'action Exporter au clic, IHM_EXPORTER dans le script d'un
    //  clic) : le dialogue du poste, au doigt (StationExportDialog) - le chemin
    //  propose (exports/ du projet, sous le nom habituel), le bouton ...,
    //  Exporter / Annuler. Ecrit a la reponse, puis `done(ecrit, ou)` ; Annuler :
    //  done(false, ""). Au kiosque, l'explorateur (et un autre chemin) est a
    //  l'administrateur du poste, comme F1. Faux : pas de dossier de projet.
    bool askExport(const hmi::ExportRequest& rq, std::function<void(bool, const std::string&)> done);
    // ---- fin Lot API 8 ----

protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;

private:
    struct Secondary;
    void openSecondaries();
    void closeSecondaries();
    void paintSecondaries(double time);

    App&                                    app_;
    HmiSimulationPane*                      pane_{nullptr};
    std::vector<std::unique_ptr<Secondary>> screens_;
    std::vector<double>                     taps_;          // les touchers du coin haut droit
    double                                  now_{0};
    double                                  lastSecondary_{-1};
    double                                  lastSnapshot_{-1e9};   // lot 15 : l'etat garde pour la reprise
    std::string                             helpDenied_;           // lot 15 : F1 refuse (le dire une fois)
    bool                                    started_{false};  // onEnter revient apres chaque dialogue
    std::uint64_t                           epoch_{0};        // App::screenEpoch a l'ouverture des ecrans
    std::vector<std::pair<int, std::string>> captures_;      // (ecran, fichier) demandees
    std::map<std::string, std::string>      captureDone_;    // fichier -> "" ou pourquoi
    core::ConnectionScope                   links_;
};

} // namespace app
