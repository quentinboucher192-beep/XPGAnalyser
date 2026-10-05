// =============================================================================
//  app/hmi/HmiNotifyHost.hpp - les notifications appartiennent a l'application
//                              (lot 14)
// -----------------------------------------------------------------------------
//  Comme la liaison (HmiCommHost) : le distributeur des courriels et des SMS
//  ne depend d'aucun ecran. La simulation IHM et le poste d'exploitation lui
//  donnent les evenements d'alarme (Runtime::Hooks::alarmNotice) et les
//  rapports ecrits ; App le fait avancer a chaque image (les delais, la boite
//  d'essai qui suit la configuration).
//
//  LE JOURNAL DES ENVOIS : les 300 derniers (le volet Notifications les
//  montre), et une ligne par envoi pour le journal de l'IHM en marche.
// =============================================================================
#pragma once

#include "../../hmi/HmiNotify.hpp"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace app {

class NotifyHost {
public:
    NotifyHost() = default;
    ~NotifyHost();
    NotifyHost(const NotifyHost&) = delete;
    NotifyHost& operator=(const NotifyHost&) = delete;

    // Chaque image : les reglages du projet, la boite d'essai, les delais ecoules.
    void tick(const hmi::Project* project);
    void shutdown();

    // Un evenement d'alarme (la simulation, le poste).
    void notice(const hmi::AlarmNotice&);
    // Un rapport ecrit : a ses destinataires, en piece jointe.
    void report(const hmi::ReportOutput&);
    // Un essai a ce destinataire (courriel ou SMS), sans filtre, avec les
    // reglages de ce projet (enregistres ou non).
    void test(const hmi::Project* project, const hmi::NotifyRecipient&, bool sms);

    [[nodiscard]] hmi::NotifyStats stats() const;
    // Le journal des envois (le plus recent en dernier).
    [[nodiscard]] const std::deque<hmi::notify::Sent>& log() const noexcept { return log_; }
    [[nodiscard]] std::size_t waiting() const { return dispatcher_ ? dispatcher_->waiting() : 0; }
    [[nodiscard]] std::size_t queued() const { return dispatcher_ ? dispatcher_->queued() : 0; }
    // Ce qui merite une ligne du journal de l'IHM (un envoi, un echec, la rafale).
    [[nodiscard]] std::vector<std::string> takeEvents();

    // La boite d'essai (nulle : arretee).
    [[nodiscard]] const hmi::notify::TestServer* testBox() const noexcept { return box_ && box_->running() ? box_.get() : nullptr; }
    [[nodiscard]] const std::string& testBoxError() const noexcept { return boxError_; }
    // Les essais : attendre que la file se vide.
    bool drain(int timeoutMs);

private:
    void collect();
    std::unique_ptr<hmi::notify::Dispatcher> dispatcher_;
    std::unique_ptr<hmi::notify::TestServer> box_;
    hmi::Notifications                       settings_;
    std::string                              project_;
    bool                                     configured_{false};
    int                                      boxPort_{0};
    std::string                              boxError_;
    std::deque<hmi::notify::Sent>            log_;
    std::vector<std::string>                 events_;
};

// Le jour (1 lundi ... 7 dimanche) et la minute du jour, a l'heure du poste.
void localDayMinute(int& weekday, int& minutes);
// Des secondes qui ne reculent jamais (les delais, l'anti-rafale).
[[nodiscard]] double steadySeconds();

} // namespace app
