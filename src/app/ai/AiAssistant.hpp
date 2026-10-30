// =============================================================================
//  app/ai/AiAssistant.hpp - l'Assistant IA : la seance (le CAIManager de la demande)
// -----------------------------------------------------------------------------
//  CE QUI VIT ENTRE LE PANNEAU ET LE MODELE, SANS UN WIDGET :
//    - les conversations (chargees de IA/conversations/, ecrites a chaque pas) et
//      celle qui est ouverte ; creer, choisir, renommer, supprimer ;
//    - l'envoi d'une demande et la GENERATION de sa reponse, par etats : en
//      attente, reflexion, ecriture mot a mot, finie - et Arreter ;
//    - les pieces jointes du prochain message (copiees dans IA/pieces-jointes/,
//      leur traitement simule, visible) ;
//    - le mode (Previsualisation, Direct), les reglages, la cle API (EN MEMOIRE
//      SEULEMENT : elle n'est jamais ecrite) ;
//    - appliquer ou refuser une proposition : la commande passe par l'hote (la
//      pile de l'application : un seul Ctrl+Z) ; une proposition perimee (la vue
//      a change) ne s'applique jamais.
//
//  LE FOURNISSEUR. Aujourd'hui la demonstration (AiDemo.hpp), sans fil ni
//  reseau : la reponse est calculee a l'envoi et "s'ecrit" au rythme de tick().
//  Demain un fournisseur reseau aura la meme forme (start, poll, cancel), sur un
//  fil de travail, lu a chaque image par tick() - l'audit, section 2.6.
//
//  L'HORLOGE est celle qu'on passe a tick() (les secondes de l'application) :
//  les essais et les seances de script sont reproductibles.
// =============================================================================
#pragma once

#include "AiDemo.hpp"
#include "AiStore.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app::ai {

class Assistant {
public:
    // Ce que l'application donne (l'ecran du projet ; un essai, les siens).
    struct Host {
        std::function<hmi::DocumentPtr()>     document;     // le projet IHM ouvert (nul : aucun)
        std::function<hmi::Id()>              activeView;   // la vue de l'onglet ouvert (kNoId : aucune)
        std::function<std::vector<hmi::Id>()> selection;    // la selection de cet editeur
        std::function<void(core::CommandPtr)> apply;        // empiler (Ctrl+Z)
        std::function<std::string()>          projectName, projectPath;
        Env                                   env;          // les noms de l'automate relie
    };

    // Les etats de la generation, dans l'ordre.
    enum class Phase : std::uint8_t { Idle, Waiting, Thinking, Writing };
    // Les durees de la demonstration (secondes) ; l'ecriture : des mots par seconde.
    static constexpr double kWaiting = 0.6, kThinking = 1.2, kWordsPerSecond = 36.0, kAttachSeconds = 1.2;

    explicit Assistant(Host host);

    // ---- le cycle de vie -----------------------------------------------------------
    // Relit les reglages et les conversations (les demonstrations a la premiere ouverture) ;
    // applique la retention. `problems` : les fichiers illisibles.
    void load(std::vector<std::string>* problems = nullptr);
    // A chaque image : la generation, le traitement des pieces jointes. Vrai : quelque chose a change.
    bool tick(double now);
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // ---- les conversations -----------------------------------------------------------
    [[nodiscard]] const std::vector<Conversation>& conversations() const noexcept { return conversations_; }
    [[nodiscard]] const Conversation* current() const noexcept;
    [[nodiscard]] int  currentIndex() const noexcept { return current_; }
    void               newConversation();
    bool               select(std::string_view id);
    bool               rename(std::string_view id, std::string title);
    bool               remove(std::string_view id);
    [[nodiscard]] int  indexOf(std::string_view id) const noexcept;

    // ---- envoyer, arreter ---------------------------------------------------------------
    // Faux et `why` : un texte vide, une generation en cours, des pieces jointes en traitement,
    // l'Assistant desactive.
    bool send(std::string text, std::string* why = nullptr);
    void stop();
    [[nodiscard]] Phase phase() const noexcept { return gen_.phase; }
    [[nodiscard]] bool  generating() const noexcept { return gen_.phase != Phase::Idle; }
    // Le message de l'assistant en cours d'ecriture (son indice ; -1 : aucun) et ce qui en est ecrit.
    [[nodiscard]] int         generatingMessage() const noexcept;
    [[nodiscard]] std::string shownText() const;
    // Les suggestions a montrer sous la derniere reponse (une demande non reconnue).
    [[nodiscard]] bool suggestionsAfterLast() const noexcept { return suggestionsAfter_ && !generating(); }

    // ---- les pieces jointes du prochain message ------------------------------------------
    struct Pending {
        Attachment attachment;
        double     started{-1};   // la seconde du debut du traitement (-1 : pas encore vu par tick)
        float      progress{0.f}; // 0..1
    };
    [[nodiscard]] const std::vector<Pending>& pending() const noexcept { return pending_; }
    bool attach(const std::string& path, std::string* why = nullptr);
    void detach(std::size_t index);
    [[nodiscard]] bool processing() const noexcept;

    // ---- les propositions -----------------------------------------------------------------
    // La proposition du message `message` de la conversation ouverte (nul : aucune).
    [[nodiscard]] const Proposal* proposal(int message) const noexcept;
    [[nodiscard]] Check check(int message) const;
    // L'apercu (une copie de la vue, rien ne change dans le projet).
    [[nodiscard]] Preview previewOf(int message) const;
    // Appliquer : UN pas d'annulation. Faux et `why` : perimee, vue absente, erreurs, plus a
    // l'etat "proposee". La confirmation d'une suppression multiple est a l'appelant (needsConfirm).
    bool apply(int message, std::string* why = nullptr);
    bool refuse(int message);
    [[nodiscard]] bool needsConfirm(int message) const;
    // Le choix de l'utilisateur pour une variable introuvable (une variable existante, ou la creer).
    bool chooseVariable(int message, VariableChoice choice);

    // ---- le mode, les reglages, la cle -----------------------------------------------------
    [[nodiscard]] bool directMode() const noexcept { return settings_.mode == "direct"; }
    void               setDirectMode(bool direct);
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    void               setSettings(Settings s);   // enregistre (parametres.json)
    // La cle API : en memoire pour la seance, jamais ecrite (l'ecran le dit).
    void setApiKey(std::string key) { apiKey_ = std::move(key); ++revision_; }
    [[nodiscard]] const std::string& apiKey() const noexcept { return apiKey_; }
    // L'etat de connexion, tel que l'en-tete le dit.
    [[nodiscard]] std::string connectionText() const;

    // Ce qui vient de se passer, pour la barre d'etat de l'ecran ("Proposition appliquee : ...").
    [[nodiscard]] std::string takeNotice() { auto n = std::move(notice_); notice_.clear(); return n; }
    // En mode Direct, une proposition qui demande confirmation attend l'appelant (son message).
    [[nodiscard]] int takeDirectConfirm() { const int m = directConfirm_; directConfirm_ = -1; return m; }

    [[nodiscard]] const Host& host() const noexcept { return host_; }
    // Le contexte d'une demande (la vue active, la selection), comme a l'envoi.
    [[nodiscard]] DemoContext context() const;

private:
    Conversation* cur() noexcept;
    Conversation* generatingConversation() noexcept;
    Conversation* touch(Conversation& c);   // la date ; la liste retriee : rend la conversation (deplacee)
    void save(Conversation& c);
    void finish();
    void autoApply(int message);

    struct Generation {
        Phase       phase{Phase::Idle};
        double      start{-1};      // la seconde de l'envoi (-1 : prise au prochain tick)
        double      writingFrom{0};
        int         message{-1};
        std::string conversation;   // l'identifiant de sa conversation (on peut en ouvrir une autre)
        std::string full;           // la reponse entiere
        std::size_t words{0}, shownWords{0};
        std::optional<Proposal> proposal;
        bool        suggestions{false};
    };

    Host                      host_;
    Settings                  settings_;
    std::string               apiKey_;
    std::vector<Conversation> conversations_;
    int                       current_{-1};
    Generation                gen_;
    std::vector<Pending>      pending_;
    bool                      suggestionsAfter_{false};
    std::string               notice_;
    int                       directConfirm_{-1};
    double                    lastNow_{0};
    std::uint64_t             revision_{1};
};

[[nodiscard]] std::string_view phaseLabel(Assistant::Phase) noexcept;   // "En attente", "R\xC3\xA9" "flexion"...

} // namespace app::ai
