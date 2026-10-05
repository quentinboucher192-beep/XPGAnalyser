// =============================================================================
//  hmi/HmiNotify.hpp - les notifications des alarmes : courriel et SMS (lot 14)
// -----------------------------------------------------------------------------
//  UNE ALARME PREVIENT QUELQU'UN. Le moteur donne chaque evenement d'alarme
//  (AlarmNotice : apparition, reapparition, acquittement, disparition) ; le
//  DISTRIBUTEUR choisit les destinataires (leurs priorites, leurs groupes,
//  l'astreinte a cette heure), applique l'anti-rafale et envoie, sur son fil :
//
//    le courriel   SMTP, sans TLS - le relais de l'usine, sur le reseau local
//                  (port 25) ; AUTH LOGIN si un utilisateur est donne. UTF-8,
//                  le sujet encode (RFC 2047), le corps en base64 ; une piece
//                  jointe possible (un rapport) ;
//    le SMS        une passerelle HTTP (un modem GSM, un boitier SMS, un
//                  service local) : une URL ou {numero} et {message} sont
//                  remplaces (encodes) - GET ; ou POST d'un corps.
//
//  L'ANTI-RAFALE. Attendre N secondes : acquittee ou disparue avant, rien ne
//  part (un defaut fugitif ne reveille personne). Une meme alarme au meme
//  destinataire : une fois en N secondes. Au plus N messages par heure, en
//  tout ; au-dela, le journal le dit.
//
//  LA BOITE D'ESSAI : un petit serveur SMTP (et HTTP pour les SMS) sur ce
//  poste, qui garde ce qu'il recoit. On regle le relais sur 127.0.0.1:2525 :
//  les messages arrivent dans Configuration > Notifications, onglet Boite
//  d'essai - sans relais, sans passerelle, sans reveiller l'astreinte.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiRuntime.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hmi::notify {

// Les champs d'un modele de message : {projet} {alarme} {message} {priorite}
// {groupe} {categorie} {etat} {heure} {date} {consigne} {utilisateur}.
[[nodiscard]] std::string expand(std::string_view tpl, const AlarmNotice&, std::string_view project);
// Le corps d'un courriel quand le projet n'en donne pas.
[[nodiscard]] std::string defaultBody();

// L'astreinte : ce destinataire est-il de service ce jour (1 lundi ... 7
// dimanche) a cette minute (0..1439) ? Sans astreinte : toujours.
[[nodiscard]] bool onDuty(const NotifyRecipient&, int weekday, int minutes);
// Recoit-il cet evenement : actif, la priorite, les groupes, l'astreinte, la
// disparition s'il la veut. L'acquittement ne se notifie pas.
[[nodiscard]] bool wants(const NotifyRecipient&, const AlarmNotice&, int weekday, int minutes);

// Le mot de passe SMTP garde dans le projet : masque, pas en clair (qui a le
// programme peut le retrouver : preferer un relais sans authentification).
[[nodiscard]] std::string maskSecret(std::string_view plain);
[[nodiscard]] std::string unmaskSecret(std::string_view masked);

// ---- le courriel ---------------------------------------------------------------
struct SmtpSettings {
    std::string host;
    int         port{25};
    std::string from{"ihm@usine.local"};
    std::string user, password;     // le mot de passe en clair (demasque)
    int         timeoutMs{10000};
    std::string helo{"xpg-ihm"};
};
[[nodiscard]] SmtpSettings smtpOf(const Notifications&);
struct Mail {
    std::vector<std::string> to;
    std::string              subject, body;
    std::string              attachmentName;                     // lot 14 : un rapport
    std::shared_ptr<const Bytes> attachment;
    std::string              attachmentType{"application/octet-stream"};
};
// Envoie ; `reply` : la derniere reponse du relais ("250 2.0.0 Ok: queued") ;
// faux : `why` dit ce qui a manque (connexion, authentification, refus).
bool sendMail(const SmtpSettings&, const Mail&, std::string* why, std::string* reply = nullptr);
// Le message tel qu'il part (en-tetes et corps, CRLF) - pour les essais.
[[nodiscard]] std::string composeMail(const SmtpSettings&, const Mail&, std::string_view dateHeader, std::string_view boundary);

// ---- le SMS par une passerelle HTTP -----------------------------------------------
[[nodiscard]] std::string urlEncode(std::string_view);
[[nodiscard]] std::string urlDecode(std::string_view);
struct HttpResult {
    int         status{0};          // 200...
    std::string body;
    std::string why;                // l'echec (connexion, delai, URL)
    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 300; }
};
// http:// seulement (une passerelle sur le reseau local).
[[nodiscard]] HttpResult httpRequest(const std::string& method, const std::string& url, const std::string& body,
                                     const std::string& contentType, int timeoutMs);
bool sendSms(const std::string& urlTemplate, const std::string& bodyTemplate, const std::string& phone, const std::string& text,
             int timeoutMs, std::string* why, int* status = nullptr);

// ---- le distributeur ------------------------------------------------------------------
struct Sent {
    std::string at;            // "2026-09-25 17:42:05"
    std::string channel;       // "Courriel", "SMS"
    std::string recipient;     // le nom ("Astreinte")
    std::string address;       // le courriel, le numero
    std::string alarm;         // l'alarme ; vide : un essai, un rapport
    std::string kind;          // "Apparition"... ; "Essai" ; "Rapport"
    std::string subject;       // le sujet, ou le texte du SMS
    bool        ok{false};
    bool        skipped{false};   // pas envoye : l'anti-rafale, l'acquittement avant le delai
    std::string result;        // "250 Ok", ou pourquoi
};

class Dispatcher {
public:
    Dispatcher();
    ~Dispatcher();
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void configure(const Notifications&, std::string projectName);
    // Un evenement d'alarme (fil de l'IHM) ; `now` : secondes (horloge du poste).
    void notice(const AlarmNotice&, double now, int weekday, int minutes);
    // Les delais ecoules partent en file.
    void tick(double now);
    // Un essai : a ce destinataire, sans filtre ni anti-rafale.
    void test(const NotifyRecipient&, bool sms);
    // Un rapport en piece jointe aux destinataires nommes ("Astreinte; Chef").
    void sendReport(const std::string& recipientNames, const std::string& subject, const std::string& body,
                    const std::string& fileName, std::shared_ptr<const Bytes> data, const std::string& contentType);
    // Ce qui a ete fait depuis le dernier appel (le journal, le volet).
    [[nodiscard]] std::vector<Sent> takeSent();
    [[nodiscard]] std::size_t waiting() const;      // en attente du delai
    [[nodiscard]] std::size_t queued() const;       // en file d'envoi
    [[nodiscard]] NotifyStats stats() const;
    // Attendre que la file soit vide (les essais), au plus timeoutMs.
    bool drain(int timeoutMs);
    void stop();

private:
    struct Job {
        std::string channel, recipient, address, alarm, kind, subject, text;
        std::vector<std::string> to;               // un courriel a plusieurs (un rapport)
        std::string attachmentName, attachmentType;
        std::shared_ptr<const Bytes> attachment;
    };
    struct Pending {
        AlarmNotice notice;
        double      due{0};
        int         weekday{1}, minutes{0};
    };
    void dispatch(const AlarmNotice&, double now, int weekday, int minutes);
    void enqueue(Job, double now);
    void run();
    void record(Sent);
    [[nodiscard]] Job mailJob(const NotifyRecipient&, const AlarmNotice&) const;
    [[nodiscard]] Job smsJob(const NotifyRecipient&, const AlarmNotice&) const;

    mutable std::mutex              mutex_;
    std::condition_variable         wake_;
    std::thread                     worker_;
    bool                            stopping_{false};
    bool                            busy_{false};
    Notifications                   settings_;
    std::string                     project_;
    std::deque<Job>                 queue_;
    std::vector<Pending>            pending_;
    std::map<std::string, double>   lastSent_;       // destinataire|canal|alarme -> heure
    std::deque<double>              hour_;           // les envois de la derniere heure
    std::vector<Sent>               done_;
    NotifyStats                     stats_;
    double                          lastFlood_{-1e9};
};

// ---- la boite d'essai -----------------------------------------------------------------
struct Received {
    std::string at;            // "17:42:05"
    std::string channel;       // "Courriel", "SMS"
    std::string from, to, subject, body;
    std::string attachment;    // "rapport_...pdf (12 345 octets)"
};

class TestServer {
public:
    TestServer();
    ~TestServer();
    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;
    // SMTP sur 127.0.0.1:smtpPort, HTTP sur smtpPort + 1 (0 : des ports libres).
    bool start(int smtpPort, std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] int  smtpPort() const noexcept { return smtpPort_; }
    [[nodiscard]] int  httpPort() const noexcept { return httpPort_; }
    [[nodiscard]] std::vector<Received> received() const;       // les 200 derniers, le plus recent en dernier
    [[nodiscard]] std::size_t count() const;
    void clear();

private:
    void run();
    void add(Received);
    std::atomic<bool>      running_{false};
    std::atomic<bool>      stop_{false};
    std::thread            thread_;
    int                    smtpPort_{0}, httpPort_{0};
    mutable std::mutex     mutex_;
    std::vector<Received>  received_;
    std::size_t            total_{0};
    struct Sockets;
    std::unique_ptr<Sockets> sockets_;
};

} // namespace hmi::notify
