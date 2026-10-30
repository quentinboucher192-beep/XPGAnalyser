// =============================================================================
//  app/ai/AiStore.hpp - l'Assistant IA : ce qu'il garde sur le PC
// -----------------------------------------------------------------------------
//  LES FICHIERS DU CONTRAT (app/ai/AiPaths.hpp), dans IA/ des reglages de
//  l'utilisateur, jamais dans un projet :
//
//    conversations/<id>.json   une conversation : ses messages, leurs pieces
//                              jointes, l'etat de leurs propositions (et leurs
//                              operations, pour les appliquer apres un redemarrage
//                              si la vue n'a pas change) ;
//    pieces-jointes/<id>/<nom> la copie a l'identique d'un fichier joint ;
//    parametres.json           les reglages - JAMAIS la cle API ;
//    activite.jsonl            une ligne par evenement (le centre de memoire la lit).
//
//  UN CHAMP INCONNU SE GARDE : chaque structure porte `extra`, ce qu'elle n'a pas
//  compris a la lecture, remis tel quel a l'ecriture. Les dates sont en heure
//  locale ISO ("2026-10-30T17:35:12"). Chaque fichier s'ecrit a cote puis prend
//  la place de l'ancien (core::writeFileAtomic).
//
//  Racine vide (AiPaths : setRoot jamais appele) : rien ne s'ecrit, tout reste en
//  memoire - un essai qui ne veut pas de disque, ou un poste sans reglages.
// =============================================================================
#pragma once

#include "AiJson.hpp"
#include "AiOperations.hpp"

#include <cstdint>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

// ---- les structures -------------------------------------------------------------
struct Attachment {
    std::string   id;          // "p-20261030-173512-1"
    std::string   name;        // "plan.pdf"
    std::string   type;        // "pdf", "png", "xihm"... (l'extension, en minuscules)
    std::uint64_t bytes{0};
    std::string   file;        // relatif a IA/ : "pieces-jointes/<id>/plan.pdf" ; vide : pas copie (pas de racine)
    std::string   source;      // le chemin d'origine (en memoire seulement : pas dans le fichier)
    json::Value   extra;
};

// 1.15.0 : l'appel d'outil d'une reponse du modele, garde pour les tours suivants (l'API veut
// revoir l'appel, puis son resultat reel en tete du tour de l'utilisateur qui suit).
struct ToolCall {
    std::string id;       // "toolu_..."
    std::string name;     // kOperationsTool
    std::string input;    // l'entree, en JSON (telle que recue)
    std::string error;    // l'entree illisible ou invalide (vide : une proposition en est sortie)
    bool operator==(const ToolCall&) const = default;
};

struct Message {
    std::string             id;            // "m1"
    std::string             role;          // "utilisateur" | "assistant"
    std::string             date;          // ISO, heure locale
    std::string             text;          // markdown simple
    std::string             state;         // assistant : "fini" | "arrete" | "erreur"
    std::vector<Attachment> attachments;
    std::optional<Proposal> proposal;
    // 1.15.0 : une reponse d'un vrai fournisseur - lequel, quel modele (vides : la demonstration),
    // et son appel d'outil.
    std::string             provider, model;
    std::optional<ToolCall> tool;
    // 1.15.0 : ce que le LOGICIEL ajoute sous la reponse (une reponse coupee, refusee, une entree
    // d'outil illisible, l'erreur d'un envoi) : montre en retrait, jamais renvoye au modele comme
    // s'il l'avait ecrit.
    std::string             note;
    json::Value             extra;
};

struct ProjectRef {
    std::string name, path, view;
    json::Value extra;
};

struct Conversation {
    int                  format{1};
    std::string          id, title, created, modified;
    bool                 demo{false};
    ProjectRef           project;
    std::vector<Message> messages;
    json::Value          extra;
};

// Les reglages (parametres.json). La cle API n'en fait JAMAIS partie : l'Assistant la garde en
// memoire (Assistant::setApiKey) ; "Memoriser la cle" la protege par Windows (DPAPI, AiSecret.hpp).
struct Settings {
    bool        enabled{true};
    std::string provider{"demo"};          // "demo", "anthropic" (1.15.0), puis ceux a venir
    std::string model;                     // une case libre, vide par defaut : rien en dur
    std::string endpoint;                  // l'adresse de connexion (vide : celle du fournisseur ; sinon un relais)
    int         timeoutSeconds{60};        // l'inactivite : rien recu pendant ce delai, une erreur
    std::string effort;                    // 1.15.0 : "" (celui du modele), "low", "medium", "high"
    std::string mode{"previsualisation"};  // "previsualisation" | "direct"
    int         keepDays{0};               // 0 : toujours
    int         keepCount{0};              // 0 : sans limite
    bool        sendView{true}, sendSelection{true}, sendVariables{true}, sendAttachments{true};
    json::Value extra;
    bool operator==(const Settings&) const = default;
};

// Les fournisseurs proposes (cle, libelle, disponible).
struct ProviderInfo {
    std::string_view key, label;
    bool             available;
};
[[nodiscard]] const std::vector<ProviderInfo>& providers();

// ---- l'horloge et les identifiants -------------------------------------------------
// L'heure du PC ; un essai la fige (nul : l'horloge du PC).
void setClock(std::function<std::time_t()> clock);
[[nodiscard]] std::time_t now();
[[nodiscard]] std::string isoDate(std::time_t t);             // "2026-10-30T17:35:12"
[[nodiscard]] std::time_t parseIso(std::string_view iso);     // 0 : illisible
// "le 30/10 \xC3\xA0 17:35", "aujourd'hui \xC3\xA0 17:35", "hier", "17:35"...
[[nodiscard]] std::string shortDate(std::string_view iso, bool timeOnly = false);
// "c-20261030-173512-1" : le prefixe, la date, un compteur (unique dans la seance et sur le disque).
[[nodiscard]] std::string newId(std::string_view prefix);

// ---- le JSON des structures ---------------------------------------------------------
[[nodiscard]] json::Value  toJson(const Conversation&);
[[nodiscard]] Conversation conversationFromJson(const json::Value&);
[[nodiscard]] json::Value  toJson(const Settings&);
[[nodiscard]] Settings     settingsFromJson(const json::Value&);

// ---- le disque (rien sans racine) ---------------------------------------------------
// Les conversations du dossier, la plus recemment modifiee d'abord ; les fichiers illisibles
// sont dits dans `problems` (et laisses tels quels).
[[nodiscard]] std::vector<Conversation> loadConversations(std::vector<std::string>* problems = nullptr);
bool saveConversation(const Conversation&, std::string* why = nullptr);
// Le fichier de la conversation et les copies de ses pieces jointes.
bool deleteConversationFiles(const Conversation&);
[[nodiscard]] Settings loadSettings(bool* found = nullptr);
bool saveSettings(const Settings&, std::string* why = nullptr);
// Une ligne d'activite (type : conversation, fichier, suppression...).
void appendActivity(std::string_view type, std::string_view text);

// ---- les pieces jointes --------------------------------------------------------------
struct AttachmentFormat {
    std::string_view ext, label;
};
// PNG, JPG, PDF, TXT, CSV, XLSX, XML, JSON, et les formats du logiciel (.xihm, .xapi, le projet).
[[nodiscard]] const std::vector<AttachmentFormat>& attachmentFormats();
[[nodiscard]] bool acceptedFormat(std::string_view fileName);
// Le filtre du selecteur de fichiers ("png;jpg;jpeg;pdf;...").
[[nodiscard]] std::string attachmentFilter();
// Copie le fichier dans pieces-jointes/<id>/ (sans racine : il est seulement decrit). Faux et
// `why` : format refuse, fichier illisible, copie impossible.
bool addAttachment(const std::string& sourcePath, Attachment& out, std::string* why = nullptr);
// Le chemin complet de la copie (vide : pas copie).
[[nodiscard]] std::string attachmentPath(const Attachment&);
// "12 Ko", "1,3 Mo".
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

// La retention (Settings::keepDays, keepCount) : les conversations a retirer, les plus
// anciennes d'abord (jamais celle qui est ouverte : `keep`).
[[nodiscard]] std::vector<std::string> expiredConversations(const std::vector<Conversation>&, const Settings&, std::string_view keep = {});

} // namespace app::ai
