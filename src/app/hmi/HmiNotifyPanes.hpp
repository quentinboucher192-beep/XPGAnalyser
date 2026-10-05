// =============================================================================
//  app/hmi/HmiNotifyPanes.hpp - Configuration > Notifications (lot 14)
// -----------------------------------------------------------------------------
//  QUI PREVENIR, ET COMMENT. A droite, les reglages : le relais SMTP de
//  l'usine (sans TLS ; un utilisateur si le relais le demande), la passerelle
//  SMS (une URL http:// ou {numero} et {message} sont remplaces), les modeles
//  des messages, l'anti-rafale ; la boite d'essai ; et le destinataire choisi.
//
//  Au milieu, trois onglets :
//    Destinataires  chacun : son courriel, son telephone, les priorites qu'il
//                   recoit, ses groupes, son astreinte (jours, heures) ;
//    Envois         ce qui est parti (ou pas, et pourquoi), le plus recent en
//                   haut - rafraichi chaque seconde ;
//    Boite d'essai  ce que la boite d'essai a recu : les courriels (sujet,
//                   texte, piece jointe) et les SMS.
//
//  ESSAYER AVEC LA BOITE D'ESSAI : un clic regle le relais sur 127.0.0.1:2525
//  et la passerelle sur http://127.0.0.1:2526, et ouvre la boite - les
//  notifications s'essaient sans relais, sans passerelle, sans reveiller
//  l'astreinte. TESTER : un message d'essai au destinataire choisi.
//
//  Chaque changement du projet est une commande : Ctrl+Z.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../TablePaste.hpp"
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

class NotifyHost;

class HmiNotifyPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiNotifyPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Les gestes du volet, pour les boutons, les scripts et les tests.
    // Un reglage : "actives", "smtp", "smtp_port", "expediteur", "utilisateur",
    // "delai", "sms_url", "sms_corps", "sujet", "corps", "sms", "attente",
    // "repetition", "max_heure", "boite", "boite_port".
    bool setSetting(const std::string& key, const std::string& value, std::string* why = nullptr);
    bool addRecipient(const std::string& name, const std::string& email = {}, const std::string& phone = {}, std::string* why = nullptr);
    bool removeRecipient(const std::string& name, std::string* why = nullptr);
    // "nom", "courriel", "telephone", "priorite", "groupes", "astreinte", "jours",
    // "de", "a", "disparition", "actif".
    bool setRecipientField(const std::string& name, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Le mot de passe du relais (vide : sans authentification) - masque.
    bool setSmtpPassword(const std::string& password);
    // La boite d'essai en un clic : relais et passerelle sur ce poste, boite ouverte, notifications actives.
    bool useTestBox();
    // Un message d'essai au destinataire choisi.
    bool test(bool sms, std::string* why = nullptr);

    [[nodiscard]] std::string selectedRecipient() const;
    void selectRecipient(const std::string& name);

    struct Hosts {
        std::function<NotifyHost*()> notify;          // le distributeur, la boite d'essai
        std::function<void()>        askPassword;     // le dialogue du mot de passe SMTP
    };
    void setHosts(Hosts h);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     recipients() noexcept { return *table_; }
    [[nodiscard]] ui::TableView&     sent() noexcept { return *sent_; }
    [[nodiscard]] ui::TableView&     box() noexcept { return *box_; }
    [[nodiscard]] ui::TabControl&    tabs() noexcept { return *tabs_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    // Rafraichir tout de suite les envois et la boite (les essais, les scripts).
    void refreshLive();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool change(const std::string& label, const std::function<void(hmi::Notifications&)>& fn);

    hmi::DocumentPtr       doc_;
    Apply                  apply_;
    Hosts                  hosts_;
    HmiToolStrip*          tools_{nullptr};
    ui::TabControl*        tabs_{nullptr};
    ui::TableView*         table_{nullptr};
    ui::TableView*         sent_{nullptr};
    ui::TableView*         box_{nullptr};
    ui::PropertyGrid*      grid_{nullptr};
    ui::StatusBar*         status_{nullptr};
    std::shared_ptr<ui::ITableModel> tableModel_, sentModel_, boxModel_;
    std::vector<std::string> order_;
    std::size_t            shownSent_{static_cast<std::size_t>(-1)}, shownBox_{static_cast<std::size_t>(-1)};
    std::string            shownBoxState_;
    double                 lastLive_{-10};
    bool                   refreshing_{false};
    std::string            message_;
    paste::Binding         paste_;         // lot 20 : coller des destinataires depuis Excel
    core::ConnectionScope  links_;
};

} // namespace app
