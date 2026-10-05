// =============================================================================
//  app/hmi/HmiNetDiagram.hpp - le schema du reseau du PC (lot 15)
// -----------------------------------------------------------------------------
//  CE QUE VOIT L'ONGLET "Reseau du PC" : a gauche le PC (son nom, son systeme,
//  les equipements simules qu'il porte), au milieu ses ports Ethernet (et
//  Wi-Fi) avec leur adresse, a droite les equipements, regroupes par reseau et
//  relies au port qui les joint - une couleur par reseau. Un equipement qu'aucun
//  port ne joint va dans l'encadre "Hors reseau", avec ce qu'on peut y faire
//  (donner au port libre une adresse de son reseau, ou changer la sienne).
//
//  Le volet (HmiCommPane) calcule ce qu'il faut montrer (NetDiagram) ; le
//  schema le dessine et dit ce qu'on y clique.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace app {

struct NetDiagram {
    struct Port {
        std::string key;                  // le nom du port (Adapter::name)
        int         number{0};            // 1, 2... (le rond numerote)
        std::string title;                // "Ethernet 1"
        std::string card;                 // "Intel(R) Ethernet I219-LM"
        std::string link;                 // "branche, 1 Gb/s", "cable debranche"
        bool        up{false};
        bool        wifi{false};
        std::string ip, prefix;           // "192.168.1.20", "24" ; vide : sans adresse
        std::string detail;               // "passerelle 192.168.1.1 · fixe"
        std::string mac;                  // "MAC 00-1B-21-3A-4F-10"
        bool        automatic{false};     // 169.254.x.x
        int         net{-1};              // le reseau (nets) qu'il joint ; -1 : aucun equipement
        bool        selected{false};
        bool        pending{false};       // son adresse est en train de changer
    };
    struct Equip {
        std::string key;                  // le nom de l'equipement ; "\x01automate" : l'automate du projet
        std::string name;
        std::string tag;                  // "automate du projet"
        std::string line1;                // "192.168.1.30 · Modbus TCP/IP · esclave 1"
        std::string line2;                // "ping 2 ms · Modbus 5 ms"
        std::string tip;                  // l'infobulle (pourquoi injoignable)
        int         tone{0};              // 0 pas teste, 1 joignable, 2 attention, 3 injoignable, 4 simule, 5 desactive
        int         net{-1};              // -1 : hors reseau (ou simule, ou desactive)
        bool        simulated{false};
        bool        disabled{false};
        bool        selected{false};
    };
    struct Net {
        std::string label;                // "Reseau 192.168.1.0 / 24 · 4 equipements"
        int         port{-1};             // le port qui le joint (ports)
    };
    struct Outside {
        std::string equip;                // l'equipement
        std::string title;                // "Camera quai"
        std::string line;                 // "172.16.0.8 · Ethernet TCP/IP"
        std::string text;                 // pourquoi, et quoi faire
        std::string give;                 // "Donner au port 3 l'adresse 172.16.0.100 / 16" ; vide : aucun port libre
        std::string edit;                 // "Modifier l'adresse de Camera quai"
    };
    std::string              computer, system;
    std::vector<std::string> portsSummary;      // "3 Ethernet (2 branches)", "1 Wi-Fi"
    std::vector<Port>        ports;
    std::vector<Equip>       equips;             // aussi les simules et les desactives
    std::vector<Net>         nets;
    std::vector<Outside>     outside;
    std::string              outsideTitle;       // "Hors reseau : 1 equipement"
    std::string              hint;               // sous la legende
    std::string              empty;              // rien a montrer encore : ce texte
    // Lot 17 : le reseau simule (les ports du PC simule, les esclaves virtuels) ;
    // le bouton Reel | Simule en haut a gauche.
    bool                     simulated{false};
    bool                     toggle{true};       // montrer le bouton Reel | Simule
    std::string              toggleNote;         // a droite du bouton
};

class HmiNetDiagram final : public ui::Widget {
public:
    explicit HmiNetDiagram(std::string id = {});
    void setData(NetDiagram d);
    [[nodiscard]] const NetDiagram& data() const noexcept { return data_; }

    // Un clic sur un port, un equipement, un bouton de l'encadre Hors reseau.
    const core::SignalPtr<const std::string&> portClicked = core::Signal<const std::string&>::create();
    const core::SignalPtr<const std::string&> equipmentClicked = core::Signal<const std::string&>::create();
    const core::SignalPtr<int>                giveClicked = core::Signal<int>::create();
    const core::SignalPtr<int>                editClicked = core::Signal<int>::create();
    // Lot 17 : un equipement lache sur un port (l'equipement, le port) ; Suppr
    // sur l'equipement choisi ; le bouton Reel | Simule.
    const core::SignalPtr<const std::string&, const std::string&> equipmentDropped =
        core::Signal<const std::string&, const std::string&>::create();
    const core::SignalPtr<>                   deleteRequested = core::Signal<>::create();
    const core::SignalPtr<bool>               viewToggled = core::Signal<bool>::create();

    // Pour les scripts et les tests : ou est dessine un port, un equipement, un
    // bouton (give : "Donner au port", sinon "Modifier") ; faux s'il ne l'est pas.
    [[nodiscard]] bool portRect(std::string_view key, gfx::Rect& out) const;
    [[nodiscard]] bool equipmentRect(std::string_view key, gfx::Rect& out) const;
    [[nodiscard]] bool buttonRect(int outside, bool give, gfx::Rect& out) const;
    // Lot 17 : le bouton Reel (faux) ou Simule (vrai).
    [[nodiscard]] bool toggleRect(bool simulated, gfx::Rect& out) const;
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    // Lot 7 : l'infobulle de l'equipement survole, relue tant qu'elle est
    // ouverte (setData en apporte une nouvelle : joignable, injoignable).
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void place(gfx::IRenderer& r) const;          // les rectangles, dans le repere de l'ecran
    [[nodiscard]] int portAt(gfx::Point) const;
    [[nodiscard]] int equipAt(gfx::Point) const;
    [[nodiscard]] int buttonAt(gfx::Point, bool& give) const;

    [[nodiscard]] int toggleAt(gfx::Point) const;
    void cancelDrag();

    NetDiagram                        data_;
    mutable std::vector<gfx::Rect>    portRects_, equipRects_, pillRects_;
    mutable gfx::Rect                 toggleRects_[2]{};
    // Lot 17 : glisser un equipement sur un port.
    int                               pressEquip_{-1};
    bool                              dragging_{false};
    gfx::Point                        pressAt_{}, dragAt_{};
    int                               dropPort_{-1};
    mutable std::vector<gfx::Rect>    giveRects_, editRects_;
    mutable std::vector<gfx::Rect>    outsideTitleRects_;     // lot 17 : le nom d'un equipement hors reseau (on le tire)
    [[nodiscard]] gfx::Rect dragOrigin(int equip) const;
    mutable gfx::Rect                 pcRect_{}, outsideRect_{};
    mutable float                     contentH_{0};
    mutable bool                      placed_{false};
    float                             scroll_{0};
    int                               hoverPort_{-1}, hoverEquip_{-1}, hoverButton_{-1};
    bool                              hoverGive_{false};
};

// Les verifications d'un reglage (le port choisi) : une ligne par verification,
// son symbole (coche, point d'exclamation, croix, i) et son texte coupe a la
// largeur ; une note dessous.
class HmiCheckList final : public ui::Widget {
public:
    struct Item {
        int         tone{0};      // 1 bon, 2 attention, 3 mauvais, 4 info, 0 en cours
        std::string text;
    };
    explicit HmiCheckList(std::string id = {});
    void setItems(std::string title, std::vector<Item> items, std::string note = {});
    [[nodiscard]] const std::vector<Item>& items() const noexcept { return items_; }
    [[nodiscard]] const std::string& note() const noexcept { return note_; }
    // La hauteur qu'il faut pour tout montrer a cette largeur.
    [[nodiscard]] float heightFor(float width) const;

protected:
    void onPaint(const ui::PaintContext&) override;

private:
    std::string       title_, note_;
    std::vector<Item> items_;
};

// L'onglet Scanner IP : une barre d'avancement (ce qui est scanne, ou on en
// est) au-dessus du tableau des appareils trouves.
class HmiScanPage final : public ui::Widget {
public:
    explicit HmiScanPage(std::string id = {});
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    // fraction < 0 : pas de barre.
    void setProgress(std::string title, std::string detail, float fraction, bool running);
    [[nodiscard]] const std::string& title() const noexcept { return title_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    ui::TableView* table_{nullptr};
    std::string    title_, detail_;
    float          fraction_{-1};
    bool           running_{false};
};

} // namespace app
