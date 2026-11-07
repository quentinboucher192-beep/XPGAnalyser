// =============================================================================
//  app/ai/AiKeys.hpp - l'Assistant IA : le gestionnaire des cles API (1.18.0)
// -----------------------------------------------------------------------------
//  PLUSIEURS CLES NOMMEES ("Anthropic - perso", "OpenAI - bureau"), chacune d'un
//  fournisseur (anthropic, ou openai : les services compatibles OpenAI) ; une
//  ACTIVE par fournisseur - celle que l'Assistant emploie.
//
//    IA/cles/index.json   ce qui n'est pas secret : le nom, le fournisseur,
//                         l'adresse et le modele preferes, la fin de la cle
//                         ("7Q2c"), les dates, le dernier essai, la cle active
//                         de chaque fournisseur ;
//    IA/cles/<id>.bin     la cle, protegee comme IA/cle.bin (AiSecret : DPAPI
//                         sous Windows, liee a la session de l'utilisateur).
//
//  MEMORISEE PAR DEFAUT : une cle ajoutee est ecrite protegee (sans protection
//  - Linux - ou "pour cette seance seulement" : en memoire, jamais ecrite). Le
//  dossier IA/ est dans les reglages de l'utilisateur (%APPDATA%) : ni une
//  reinstallation ni une desinstallation n'y touchent.
//
//  JAMAIS EN CLAIR : ni dans index.json, ni dans un journal, ni dans un projet.
//
//  LA CLE LIEE A SES SERVEURS (securite) : une cle ne part que vers un serveur
//  (https://hote:port) avec lequel elle a ete ajoutee ou essayee avec succes.
//  Une adresse changee ailleurs que par l'utilisateur (un fichier, une faute) ne
//  recoit jamais la cle : le fournisseur refuse l'envoi et dit d'essayer la cle
//  pour ce serveur (Tester, ou Lister les modeles) - l'essai l'y lie. Une cle
//  d'avant (sans serveur) est liee a son premier succes.
//  La cle IA/cle.bin d'avant (1.15.0 a 1.17.x) devient une cle du gestionnaire
//  au premier lancement (migrateLegacy) : rien n'est a ressaisir.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::keys {

struct Entry {
    std::string id;            // "k3" (aussi le nom de son fichier)
    std::string name;          // "Anthropic - perso"
    std::string provider;      // "anthropic" | "openai"
    std::string endpoint;      // l'adresse preferee (vide : celle des Parametres)
    std::string model;         // le modele prefere (vide : celui des Parametres)
    std::string head;          // le debut connu, pas secret ("sk-ant-", "sk-") ; vide sinon
    std::string tail;          // les 4 derniers caracteres
    std::string added;         // "2026-10-31T13:20:05"
    std::string testedAt;      // vide : jamais essayee
    int         tested{0};     // 1 : acceptee ; -1 : refusee ; 0 : jamais
    std::string testNote;      // "12 modeles", "401 : cle refusee"
    bool        stored{false}; // memorisee sur ce PC (IA/cles/<id>.bin) ; faux : pour la seance
    // Les serveurs ou elle peut partir ("https://api.anthropic.com:443") ; vide : pas encore liee (une cle d'avant).
    std::vector<std::string> servers{};
    bool operator==(const Entry&) const = default;
};

// Le fournisseur d'un reglage de l'Assistant : "anthropic", "openai" ; vide : il n'emploie pas de cle (integree, demo).
[[nodiscard]] std::string providerOf(std::string_view settingsProvider);
[[nodiscard]] std::string providerLabel(std::string_view provider);   // "Anthropic", "Compatible OpenAI"

// Les cles, dans l'ordre d'ajout ; `provider` non vide : celles de ce fournisseur.
[[nodiscard]] std::vector<Entry> list(std::string_view provider = {});
[[nodiscard]] std::optional<Entry> find(std::string_view id);
[[nodiscard]] std::string activeId(std::string_view provider);
bool setActive(const std::string& id);

// Ajoute une cle (le secret : memorise si `remember` et si le poste sait proteger ; sinon pour la seance). Elle devient
// l'active de son fournisseur s'il n'en a pas. Rend son id ; vide et `why` : refusee (nom vide ou pris, cle vide).
std::string add(Entry e, const std::string& secret, bool remember, std::string* why = nullptr);
// Remplace le secret d'une cle (la meme regle de memorisation).
bool replace(const std::string& id, const std::string& secret, bool remember, std::string* why = nullptr);
// Le nom, le fournisseur, l'adresse, le modele (le reste ne change pas ici).
bool update(const Entry& e, std::string* why = nullptr);
// Retire la cle (son fichier aussi) ; l'active de son fournisseur passe a la suivante.
bool remove(const std::string& id);
// Le secret (nullopt et `why` : illisible - une autre session Windows, un autre PC).
[[nodiscard]] std::optional<std::string> secret(const std::string& id, std::string* why = nullptr);
// Le resultat d'un essai (Tester).
void noteTest(const std::string& id, bool ok, const std::string& note);

// "sk-ant-...<points>7Q2c" : ce que l'ecran montre (jamais plus).
[[nodiscard]] std::string masked(const Entry&);
[[nodiscard]] std::string folder();             // IA/cles
[[nodiscard]] std::size_t storedCount();         // les cles memorisees sur ce PC

// IA/cle.bin (1.15.0 a 1.17.x) -> une cle "Ma cle (<fournisseur>)", active ; rien si le gestionnaire en a deja.
bool migrateLegacy(std::string_view settingsProvider);

// Les essais : oublier les cles de la seance (pas les fichiers).
void resetSession();

// ---- la cle liee a ses serveurs ---------------------------------------------------------------------------------------
// Le serveur d'une adresse : "https://hote:port" (l'hote en minuscules) ; vide : adresse illisible.
[[nodiscard]] std::string serverOf(std::string_view url);
// La cle (son secret) peut-elle partir vers `url` ? Faux et `why` (en francais) : elle est liee a d'autres serveurs. Vrai :
// une cle inconnue du gestionnaire (saisie a l'instant, pas enregistree), une cle d'avant sans serveur, un serveur lie.
[[nodiscard]] bool allowedFor(const std::string& secret, std::string_view url, std::string* why = nullptr);
// Un succes avec ce secret vers `url` : les cles de ce secret sans serveur y sont liees ; `retest` (un essai explicite :
// Tester, Lister les modeles) : le serveur s'ajoute aussi a celles deja liees ailleurs.
void confirmServer(const std::string& secret, std::string_view url, bool retest);
// Lie la cle `id` a ce serveur (son ajout, l'adresse sous les yeux). Faux : cle inconnue ou adresse illisible.
bool bindServer(const std::string& id, std::string_view url);

} // namespace app::ai::keys
