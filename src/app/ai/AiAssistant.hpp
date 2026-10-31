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
//  LE FOURNISSEUR. La demonstration (AiDemo.hpp, Settings::provider "demo"),
//  sans fil ni reseau : la reponse est calculee a l'envoi et "s'ecrit" au rythme
//  de tick() - son chemin n'a pas change. 1.15.0 : UN VRAI FOURNISSEUR
//  (AiProvider.hpp : "anthropic") part sur son fil de travail ; tick() lit ses
//  evenements a chaque image (poll) : les etats, le texte au fil de l'eau, l'appel
//  d'outil -> proposalFromTool (AiPrompt.hpp) -> validate -> la carte, comme la
//  demonstration. La requete porte les consignes et le contexte (systemPrompt),
//  l'outil des operations, et les tours de la conversation : les textes, les
//  pieces jointes, les appels d'outil precedents et leur RESULTAT REEL
//  (appliquee, refusee, en attente, annulee par Ctrl+Z, ou l'erreur de
//  validation). Une erreur (cle refusee, modele introuvable, hors ligne, delai)
//  devient un message "erreur" de la conversation, et Reessayer le renvoie.
//  Arreter ferme la connexion ; le fournisseur arrete finit sur son fil, sans
//  bloquer l'interface (retiring_).
//
//  1.16.0 : L'IA INTEGREE (AiLocal.hpp, Settings::provider "integree", le choix par
//  defaut d'une installation neuve) suit le chemin de la demonstration : sa reponse est
//  calculee a l'envoi (des regles, sur ce PC : rapide, sans fil), puis "s'ecrit" au
//  rythme de tick() apres une courte reflexion ; sa proposition entre dans la
//  conversation comme celle d'un vrai fournisseur (la carte, l'apercu, le mode Direct,
//  un Ctrl+Z). Les tours precedents de la conversation lui donnent le sens d'une suite
//  ("et une autre", "la meme en bleu").
//
//  L'HORLOGE est celle qu'on passe a tick() (les secondes de l'application) :
//  les essais et les seances de script sont reproductibles.
// =============================================================================
#pragma once

#include "AiAnthropic.hpp"
#include "AiDemo.hpp"
#include "AiLearning.hpp"
#include "AiPlan.hpp"   // 1.19.0 : le plan d'action et le compte rendu
#include "AiPrompt.hpp"   // 1.20.0 : IncidentSource
#include "AiProvider.hpp"
#include "AiStore.hpp"
#include "AiUsage.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app::ai {

struct LocalReply;   // AiLocal.hpp

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
        // 1.16.0 (a la fin) : ouvrir une vue dans l'editeur (son onglet) - apres Appliquer une proposition qui
        // cree une vue (creer_vue), l'editeur l'ouvre. Nul : rien ne s'ouvre (un essai).
        std::function<void(hmi::Id)>          openView;
        // 1.16.0 (captures, a la fin) : lire un export de Control Expert joint (.XPG) comme l'automate de la seance
        // (IHM Studio n'en a pas d'autre) ; vrai et `said` : ce qui a ete lu ("891 variables globales et 28 types
        // (DDT)") ; faux et `said` : pourquoi. Nul : un .XPG joint reste une piece jointe ordinaire.
        std::function<bool(const std::string& path, std::string* said)> readPlc;
        // 1.19.0 (a la fin) : MONTRER un element du projet nomme dans le plan ou le compte rendu (une vue, un objet d'une
        // vue, une variable, un type, une fonction, un script, une alarme, un equipement, une ressource) : son onglet ou
        // son volet. `kind` : celui de plan::Ref. Faux : introuvable (supprime depuis). Nul : rien ne s'ouvre (un essai).
        std::function<bool(const std::string& kind, const std::string& name, const std::string& view)> reveal;
        // 1.20.0 (a la fin) : LES DOSSIERS D'INCIDENT du projet (en attente et valides) - l'IA integree repond aux
        // questions de diagnostic (<< Pourquoi cette alarme est-elle apparue ? >>...) depuis eux, l'IA distante en recoit
        // un resume borne. Nul : aucun (un essai) - rien ne change.
        IncidentSource incidents;
    };

    // Les etats de la generation, dans l'ordre. 1.15.0 : Proposing, l'appel d'outil s'ecrit (un
    // vrai fournisseur : les operations arrivent apres le texte).
    enum class Phase : std::uint8_t { Idle, Waiting, Thinking, Writing, Proposing };
    // Les durees de la demonstration (secondes) ; l'ecriture : des mots par seconde.
    static constexpr double kWaiting = 0.6, kThinking = 1.2, kWordsPerSecond = 36.0, kAttachSeconds = 1.2;

    explicit Assistant(Host host);
    ~Assistant();

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
    // 1.16.1 : la conversation de la reponse en cours (vide : aucune) - l'avertissement de fin de reponse.
    [[nodiscard]] std::string generatingConversationId() const { return generating() ? gen_.conversation : std::string{}; }
    [[nodiscard]] std::string shownText() const;
    // Les suggestions a montrer sous la derniere reponse (une demande non reconnue).
    [[nodiscard]] bool suggestionsAfterLast() const noexcept { return suggestionsAfter_ && !generating(); }
    // 1.16.0 : LES SUGGESTIONS (les puces de la conversation vide, et sous une reponse qui en a) : celles de
    // l'IA integree (apres une demande qu'elle n'a pas comprise : les formulations qu'elle propose), sinon
    // celles de la demonstration.
    [[nodiscard]] const std::vector<std::string>& suggestions() const;

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
    // 1.16.1 : appliquer en laissant de cote ce qui est en erreur (l'option << Appliquer malgre les erreurs >> ; le
    // panneau a demande confirmation). Les erreurs sont notees sous la reponse et dans le journal d'activite.
    bool applyIgnoringErrors(int message, std::string* why = nullptr);
    bool refuse(int message);
    // 1.16.1 : REVENIR SUR UN REFUS (un refus par megarde) - la proposition redevient << proposee >> ; la carte la
    // revalide (une vue changee depuis : elle le dit, et propose de redemander).
    bool unrefuse(int message);
    [[nodiscard]] bool needsConfirm(int message) const;
    // Le choix de l'utilisateur pour une variable introuvable (une variable existante, ou la creer).
    bool chooseVariable(int message, VariableChoice choice);
    // 1.14.2 : CE QU'EST DEVENUE UNE PROPOSITION APPLIQUEE, d'apres la vue telle qu'elle est (les
    // empreintes d'avant et d'apres Appliquer) : toujours appliquee ; annulee (Ctrl+Z, ou le projet
    // rouvert sans avoir ete enregistre : la vue est revenue a son etat d'avant) ; ou la vue a change
    // depuis (d'autres gestes, ou elle n'existe plus). NotApplied : pas une proposition appliquee.
    enum class Applied : std::uint8_t { NotApplied, Applied, Undone, Changed };
    [[nodiscard]] Applied appliedState(int message) const;
    // Reappliquer une proposition annulee : la vue est exactement celle de la proposition, elle
    // s'applique de nouveau (UN pas d'annulation, une nouvelle commande). Faux et `why` sinon.
    bool reapply(int message, std::string* why = nullptr);
    // 1.19.0 : LE PLAN D'ACTION de la proposition (ses etapes dans l'ordre d'execution, leurs etats) : celui garde a
    // l'application (les etats confirmes dans le projet ; << annulee >> apres un Ctrl+Z), sinon celui de la validation
    // du moment. Le meme plan que l'apercu : aucune divergence possible.
    [[nodiscard]] plan::Plan planOf(int message) const;
    // Le compte rendu reel (le projet d'avant et d'apres Appliquer) ; vide : pas encore appliquee.
    [[nodiscard]] std::optional<plan::Report> reportOf(int message) const;
    // Retirer (ou remettre) une etape du plan avant d'appliquer : les etapes qui en dependent sont ignorees a leur tour
    // (le plan est recalcule). Faux : la proposition n'est plus << proposee >>.
    bool setExcluded(int message, std::size_t op, bool excluded);

    // ---- le mode, les reglages, la cle -----------------------------------------------------
    [[nodiscard]] bool directMode() const noexcept { return settings_.mode == "direct"; }
    void               setDirectMode(bool direct);
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    void               setSettings(Settings s);   // enregistre (parametres.json)
    // La cle API : en memoire pour la seance, jamais ecrite en clair (l'ecran le dit). 1.15.0 :
    // "Memoriser la cle" la protege par Windows (DPAPI, IA/cle.bin, AiSecret.hpp) ; load() la relit.
    void setApiKey(std::string key) { apiKey_ = std::move(key); ++revision_; }
    [[nodiscard]] const std::string& apiKey() const noexcept { return apiKey_; }
    // Memoriser (protegee) ou oublier la cle de la seance. Faux et `why` : Linux (jamais ecrite),
    // pas de cle, Windows n'a pas pu la proteger.
    bool rememberKey(bool on, std::string* why = nullptr);
    [[nodiscard]] bool keyRemembered() const;
    // 1.18.0 (le gestionnaire des cles, AiKeys) : relire la cle ACTIVE du fournisseur choisi (la fenetre Cles API vient
    // de la changer) ; vide : aucune. Faux et un avis : elle ne se relit pas sur ce poste.
    bool reloadKey();
    // L'etat de connexion, tel que l'en-tete le dit : "Demonstration - aucune IA connectee",
    // "Anthropic - <modele>", "A configurer : choisis un modele et une cle"...
    [[nodiscard]] std::string connectionText() const;

    // ---- le fournisseur (1.15.0) ---------------------------------------------------------------
    using ProviderFactory = std::function<std::unique_ptr<Provider>(std::string_view key)>;
    // Un essai y met son faux fournisseur ; nul : makeProvider (AiProvider.hpp).
    void setProviderFactory(ProviderFactory f) { providerFactory_ = std::move(f); }
    // Le fournisseur des reglages n'est pas la demonstration.
    // Un fournisseur par le reseau (1.16.0 : providerUsesNetwork - "anthropic", "openai") ; "demo" et
    // "integree" restent sur le PC.
    [[nodiscard]] bool live() const noexcept { return providerUsesNetwork(settings_.provider); }
    // 1.16.0 : l'IA integree (AiLocal.hpp) - sur ce PC, sans connexion ni configuration.
    [[nodiscard]] bool local() const noexcept { return settings_.provider == "integree"; }
    // Il manque le modele ou la cle (l'en-tete dit "A configurer", un clic ouvre les parametres).
    [[nodiscard]] bool needsSetup() const;
    // Pret a envoyer ? Faux et `why` : ce qui manque, en francais.
    [[nodiscard]] bool ready(std::string* why = nullptr) const;
    // Ce que l'indicateur dit en plus de l'etat (une nouvelle tentative et son attente).
    [[nodiscard]] const std::string& statusDetail() const noexcept { return gen_.detail; }
    // Le dernier message de la conversation ouverte est une erreur (ou un arret) d'un vrai
    // fournisseur : Reessayer le retire et renvoie la demande.
    [[nodiscard]] bool canRetry() const;
    bool retry(std::string* why = nullptr);
    // "Ce qui part", avant le premier envoi d'une conversation a un vrai fournisseur (vide : rien
    // a dire) : la demande, la vue et ses objets, la selection, les variables, les pieces jointes.
    [[nodiscard]] std::string outgoingSummary() const;
    // La requete d'une conversation, ses messages [0, upTo) (essais ; l'envoi s'en sert) : les
    // tours, les resultats reels des appels d'outil. Ni consignes ni outil (ceux de l'envoi).
    [[nodiscard]] ProviderRequest requestFor(const Conversation&, std::size_t upTo) const;
    // Le resultat d'un appel d'outil tel que le tour suivant le dit au modele (texte, erreur ?).
    [[nodiscard]] std::pair<std::string, bool> toolResult(const Message&) const;

    // ---- la liste des modeles (Parametres : Lister les modeles, Tester la connexion) ----------
    // Sur un fil ; le resultat arrive par pollModels (tick l'appelle aussi). Faux et `why`.
    bool queryModels(const Settings& s, const std::string& key, std::string* why = nullptr);
    [[nodiscard]] bool modelsBusy() const noexcept { return modelQuery_ && modelQuery_->busy(); }
    bool               pollModels();   // vrai : un resultat vient d'arriver
    [[nodiscard]] const std::optional<ModelsResult>& models() const noexcept { return models_; }
    [[nodiscard]] std::uint64_t modelsRevision() const noexcept { return modelsRev_; }

    // Ce qui vient de se passer, pour la barre d'etat de l'ecran ("Proposition appliquee : ...").
    [[nodiscard]] std::string takeNotice() { auto n = std::move(notice_); notice_.clear(); return n; }
    // En mode Direct, une proposition qui demande confirmation attend l'appelant (son message).
    [[nodiscard]] int takeDirectConfirm() { const int m = directConfirm_; directConfirm_ = -1; return m; }

    [[nodiscard]] const Host& host() const noexcept { return host_; }
    // Le contexte d'une demande (la vue active, la selection), comme a l'envoi.
    [[nodiscard]] DemoContext context() const;
    // 1.17.0 : << Essayer une phrase >> (l'onglet Apprentissage) - l'IA integree la lit sur le projet et la vue
    // ouverts, SANS RIEN APPLIQUER ni rien mesurer : comprise ou non, et ce qu'elle proposerait.
    [[nodiscard]] learning::Trial tryPhrase(const std::string& phrase) const;

    // ---- 1.16.0 (FOURNISSEURS) : la limite et la consommation en direct, le budget ---------------
    // Ce qu'on sait de la limite du service et des jetons (les evenements Usage du fournisseur).
    [[nodiscard]] const LimitState& limitState() const noexcept { return limit_; }
    // La bande du panneau a l'instant `now` (la seconde de l'application : le compte a rebours avance).
    [[nodiscard]] UsageBand usageBand(double now) const;
    // Les cumuls du jour et du mois (IA/usage.json, relus apres chaque reponse).
    [[nodiscard]] const UsageTotals& usageTotals() const;
    // Le budget est atteint : un envoi a un fournisseur relie demande d'abord confirmation (vrai et
    // `question`, en francais). allowOverBudget : l'utilisateur a confirme, le prochain envoi passe.
    [[nodiscard]] bool budgetBlocks(std::string* question = nullptr) const;
    void allowOverBudget() noexcept { overBudgetOk_ = true; }
    // La fenetre de contexte du modele : celle des reglages, sinon celle que le service a dite
    // (Lister les modeles) ; 0 : inconnue.
    [[nodiscard]] int contextWindow() const;
    // La proposition part en texte (Outil : JSON dans le texte, ou un modele qui a refuse l'outil).
    [[nodiscard]] bool toolsInText() const;
    // Le service est sur ce PC (Ollama, LM Studio, le reseau de l'usine) : pas de limite.
    [[nodiscard]] bool localService() const;

private:
    Conversation* cur() noexcept;
    bool          applyWith(int message, std::string* why, bool ignoringErrors);   // 1.16.1
    Conversation* generatingConversation() noexcept;
    Conversation* touch(Conversation& c);   // la date ; la liste retriee : rend la conversation (deplacee)
    void save(Conversation& c);
    void finish();
    void autoApply(int message);
    // 1.15.0 : un vrai fournisseur.
    bool startLive(Conversation& c, std::string request, std::string* why);
    // 1.16.0 (captures) : les exports de Control Expert joints (.XPG...) lus comme l'automate de la seance
    // (Host::readPlc) ; le texte a dire (vide : aucun).
    [[nodiscard]] std::string readPlcAttachments(const std::vector<Attachment>& attachments);
    // 1.16.1 : l'IA integree sur la demande `text`, avec les `turns` premiers messages de `c` pour la suite des
    // demandes (<< et une autre >>).
    [[nodiscard]] LocalReply askLocal(const Conversation* c, std::size_t turns, const DemoContext& ctx, const std::string& text) const;
    void pollLive();
    void finishLive(const std::string& stopReason);
    void failLive(const ProviderEvent& e);
    void retire();                                    // le fournisseur en cours part finir sur son fil
    [[nodiscard]] Applied appliedOf(const Proposal&) const;

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
        bool        local{false};       // 1.16.0 : la reponse de l'IA integree (une reflexion plus courte)
        // 1.15.0 : un vrai fournisseur.
        bool                    live{false};
        std::string             text;          // recu jusqu'ici
        std::string             detail;        // une nouvelle tentative
        std::optional<ToolCall> tool;          // l'appel d'outil recu (complet)
        std::string             request;       // la demande de l'utilisateur
        std::string             provider, model;
        hmi::Id                 viewId{hmi::kNoId};
        std::string             fingerprint;   // l'empreinte de la vue a l'envoi
        // 1.16.0 (FOURNISSEURS) : les jetons recus (le dernier Usage fait foi) ; la proposition en texte.
        ProviderUsage           usage;
        bool                    textTool{false};
        // 1.20.0 : les lectures prealables (les parties d'un gros texte joint lues a part) - ajoutees a la consommation.
        ProviderUsage           preread;
    };

    Host                      host_;
    Settings                  settings_;
    std::string               apiKey_;
    std::vector<Conversation> conversations_;
    int                       current_{-1};
    Generation                gen_;
    std::vector<Pending>      pending_;
    bool                      suggestionsAfter_{false};
    std::vector<std::string>  localSuggestions_;   // 1.16.0 : celles de la derniere reponse de l'IA integree
    std::string               notice_;
    int                       directConfirm_{-1};
    double                    lastNow_{0};
    std::uint64_t             revision_{1};
    ProviderFactory                        providerFactory_;
    std::unique_ptr<Provider>              provider_;
    std::vector<std::unique_ptr<Provider>> retiring_;   // arretes : ils finissent sur leur fil
    std::unique_ptr<ModelQuery>            modelQuery_;
    std::optional<ModelsResult>            models_;
    std::uint64_t                          modelsRev_{0};
    // 1.16.0 (FOURNISSEURS) : la limite, les cumuls (et leur jour), le budget confirme, les fenetres de
    // contexte dites par les services (fournisseur|modele), le modele qui refuse l'outil.
    void recordLive();
    LimitState                             limit_;
    mutable UsageTotals                    totals_;
    mutable std::string                    totalsDay_;
    bool                                   overBudgetOk_{false};
    std::map<std::string, long long>       modelContext_;
    std::string                            noTools_;
    std::string                            modelsProvider_;
};

[[nodiscard]] std::string_view phaseLabel(Assistant::Phase) noexcept;   // "En attente", "R\xC3\xA9" "flexion"...

} // namespace app::ai
