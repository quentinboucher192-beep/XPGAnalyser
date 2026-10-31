// =============================================================================
//  app/hmi/HmiScanDialog.hpp - 1.21.0 : confirmer une decouverte reseau
// -----------------------------------------------------------------------------
//  AVANT TOUT SCAN, cette fenetre (Configuration > Equipements > Scanner IP,
//  Scanner ; ou l'Assistant IA, qui l'ouvre preremplie) :
//
//    Interface        les ports du PC (Ethernet, Wi-Fi) et leur reseau, ou
//                     aucune (une plage libre) ; la choisir remplit la plage ;
//    Debut, Fin       les adresses ; Masque : /24 ou 255.255.255.0 - valide par
//                     Entree, il fait de la plage tout le reseau du debut ;
//    Protocoles       Modbus TCP, S7, EtherNet/IP, OPC UA, web, imprimantes...
//                     (des connexions TCP ordinaires ; seul Modbus est interroge,
//                     en lecture seule : l'identification, fonction 43) ;
//    Ports en plus, Cadence (interrogations par seconde, 20 par defaut), les
//                     noms (DNS inverse), l'identification Modbus ;
//
//  et le RESUME de ce qui partira (hmi::discovery::summary) : l'interface, la
//  plage, le masque, les protocoles et les ports, le nombre d'adresses, la duree
//  estimee, la cadence, ce qui est envoye - et rien d'autre. Lancer reste gris
//  tant que la demande est refusee (plus de 4096 adresses, un masque illisible,
//  une cadence hors bornes) et dit pourquoi. RIEN NE PART AVANT LANCER.
//
//  La reponse (DialogResult::payload) : la demande (discovery::encodeRequest)
//  et l'empreinte des reglages montres (ligne "empreinte=") - l'appelant
//  refait le plan, verifie l'empreinte, puis donne l'accord (ipscan::grant).
// =============================================================================
#pragma once

#include "../../hmi/HmiDiscovery.hpp"
#include "../../hmi/HmiNetInfo.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiScanDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string                         id{"dialog.hmiScan"};
        hmi::discovery::Request             request;     // preremplie
        std::vector<hmi::netinfo::Adapter>  adapters;    // les ports du PC (lus, rien n'est envoye)
        std::string                         note;        // une ligne de plus en haut ("Propos\xC3\xA9 par l'Assistant IA : ...")
    };

    explicit HmiScanDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return "D\xC3\xA9" "couverte r\xC3\xA9seau : confirmer le scan"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) ----------------------------
    bool setInterface(const std::string& name, std::string* why = nullptr);   // vide : aucune (plage libre)
    void setStart(std::string text);
    void setEnd(std::string text);
    void setMask(std::string text);
    bool applyMask(std::string* why = nullptr);       // le masque fait de la plage tout le reseau du debut
    void setProtocol(const std::string& key, bool on);
    void setExtraPorts(std::string text);
    void setRate(std::string text);                    // "20" ; illisible : le plan le refuse
    void setNames(bool on);
    void setModbusId(bool on);
    [[nodiscard]] const hmi::discovery::Request& request() const noexcept { return request_; }
    [[nodiscard]] const hmi::discovery::Plan& plan() const noexcept { return plan_; }
    [[nodiscard]] bool canConfirm() const noexcept { return plan_.ok(); }
    [[nodiscard]] std::string confirmLabel() const;    // "Lancer le scan (254 adresses)"
    bool confirm();                                     // Lancer : faux si la demande est refusee
    void cancel();

    // La reponse : la demande et l'empreinte des reglages montres.
    [[nodiscard]] static std::string encode(const hmi::discovery::Request&, const hmi::discovery::Plan&);
    static bool parse(const std::string& payload, hmi::discovery::Request& request, std::string& signature);

protected:
    core::Status buildUi() override;

private:
    class Body;
    void recompute();
    void refresh();          // les champs depuis la demande
    void finish(bool ok);

    Spec                                 spec_;
    hmi::discovery::Request              request_;
    hmi::discovery::Plan                 plan_;
    std::string                          rateText_;
    std::vector<hmi::discovery::Interface> interfaces_;
    Body*                                body_{nullptr};
    ui::DropDown*                        interfaceBox_{nullptr};
    ui::InputText*                       startField_{nullptr};
    ui::InputText*                       endField_{nullptr};
    ui::InputText*                       maskField_{nullptr};
    ui::InputText*                       extraField_{nullptr};
    ui::InputText*                       rateField_{nullptr};
    std::vector<ui::Checkbox*>           protocolBoxes_;
    ui::Checkbox*                        namesBox_{nullptr};
    ui::Checkbox*                        modbusBox_{nullptr};
    ui::Button*                          ok_{nullptr};
    bool                                 syncing_{false};
    bool                                 done_{false};
    core::ConnectionScope                links_;
};

} // namespace app
