// =============================================================================
//  web/WebCanvas.hpp - 1.12.31 : l'IHM en marche, dessinee et touchee sur la page
// -----------------------------------------------------------------------------
//  LE CANEVAS DU POSTE D'EXPLOITATION, SANS L'ECRAN DE L'APPLI. Comme
//  app::HmiLiveCanvas (la simulation et le poste) : chaque image, la vue courante
//  et les popups ouvertes sont EVALUEES contre le moteur (hmi::LiveView, comme
//  HmiLayerBuilder), puis dessinees par les VRAIS PEINTRES (paintHmiView) dans un
//  WebRenderer ; la vue occupe toute la page, selon le reglage du poste
//  (Configuration > Poste d'exploitation : Remplir, Garder les proportions,
//  Taille reelle). Les gestes de l'operateur suivent le meme chemin que dans
//  l'appli : l'objet sous le doigt (de la popup du dessus vers la vue), la partie
//  touchee d'un objet a parties (une ligne, un bouton, un onglet, un champ...),
//  puis les appels du moteur (press, release, objectPart, typeText...).
// =============================================================================
#pragma once
#ifdef XPG_WEB

#include "../app/hmi/HmiLot13Painter.hpp"
#include "../app/hmi/HmiPainter.hpp"
#include "../hmi/HmiLive.hpp"
#include "../hmi/HmiLoginMenu.hpp"
#include "../hmi/HmiPrompt.hpp"
#include "../hmi/HmiRuntime.hpp"
#include "../hmi/HmiSignature.hpp"
#include "../hmi/HmiSystemMenu.hpp"
#include "../ui/Theme.hpp"
#include "WebRenderer.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace app::web {

class WebCanvas {
public:
    void bind(hmi::Runtime* rt, const hmi::Project* p, const hmi::History* h) {
        rt_ = rt;
        project_ = p;
        history_ = h;
        live_.clear();
    }
    // Une image : les vues evaluees, dessinees sur une surface w x h (pixels de l'ecran).
    void paint(WebRenderer& r, float w, float h, double time);
    // Les gestes (pixels de l'ecran). button : 0 gauche, 1 droit, 2 milieu.
    void mouseDown(float x, float y, int button, int clicks, double now);
    void mouseMove(float x, float y, double now);
    void mouseUp(float x, float y, int button, double now);
    void wheel(double notches);
    void text(const std::string& utf8, double now);
    void editKey(int key, double now);
    // Une touche (son jeton, hmi/HmiKeys.hpp) ; vrai : un raccourci de la vue l'a prise.
    bool key(const std::string& token, bool ctrl, bool shift, bool alt, bool down, bool repeat, double now);
    // L'objet sous ce point, s'il en a un (les essais) ; son nom et sa vue.
    [[nodiscard]] std::string objectNameAt(float x, float y) const;
    // Le centre a l'ecran d'un objet par son nom (les essais : cliquer dessus).
    [[nodiscard]] bool objectCenter(const std::string& name, float& x, float& y) const;
    // Sa boite a l'ecran (en pixels du canevas) : les essais glissent un curseur.
    [[nodiscard]] bool objectBox(const std::string& name, float& x, float& y, float& w, float& h) const;

private:
    struct Layer {
        hmi::View          view;
        hmi::Frame         frame;
        bool               popup{false};
        float              veil{0};
        int                slot{-1};
        double             px{0}, py{0};
        bool               titleBar{false}, closeButton{false}, modal{true}, movable{false}, closeOutside{false};
        std::string        title;
        std::vector<hmi::Id> locked;
    };
    struct PopupRect {
        gfx::Rect   window, title, close, content;
        int         slot{-1};
        bool        modal{true}, movable{false}, closeOutside{false};
        std::size_t layer{0};
    };
    void buildLayers(double now);
    [[nodiscard]] std::size_t liveTopLayer() const;
    [[nodiscard]] hmi::Id objectAtLayer(std::size_t layer, gfx::Point p) const;
    [[nodiscard]] int layerUnder(gfx::Point p) const;
    void pressObject(std::size_t layer, gfx::Point p, int clicks, double now);
    [[nodiscard]] double dragFraction(std::size_t layer, const hmi::Object& o, gfx::Point p) const;

    hmi::Runtime*                        rt_{nullptr};
    const hmi::Project*                  project_{nullptr};
    const hmi::History*                  history_{nullptr};
    ui::Theme                            theme_{ui::Theme::dark()};
    std::map<hmi::Id, hmi::LiveView>     live_;
    std::map<hmi::Id, std::pair<hmi::View, hmi::DisplayOptions>> originals_;
    std::map<hmi::Id, std::pair<double, double>> lastPlaces_;
    std::map<std::string, hmi::TextTemplate> titles_;
    std::vector<Layer>                   layers_;
    std::vector<HmiViewport>             vps_;
    std::vector<PopupRect>               popupRects_;
    // Ce que l'IHM dessine d'elle-meme par-dessus la vue (comme HmiLiveCanvas) : leur
    // geometrie au dernier dessin, pour le clic. screen_ : le cadre de la vue a l'ecran.
    gfx::Rect                            screen_{};
    std::optional<hmi::LoginMenuLayout>  loginLayout_;
    hmi::LoginTab                        loginTab_{};
    std::optional<hmi::SignatureLayout>  signLayout_;
    std::optional<hmi::PromptLayout>     promptLayout_;
    std::optional<hmi::SystemMenuLayout> systemLayout_;
    int                                  systemTab_{0};
    LogoutWarningRects                   warning_{};
    hmi::Id                              down_{hmi::kNoId};
    std::size_t                          downLayer_{0};
    hmi::Id                              valueDrag_{hmi::kNoId};
    std::size_t                          valueDragLayer_{0};
    int                                  popupDrag_{-1};
    gfx::Point                           popupGrab_{};
    double                               popupStartX_{0}, popupStartY_{0};
};

} // namespace app::web

#endif // XPG_WEB
