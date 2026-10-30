// =============================================================================
//  app/ai/AiSecret.hpp - l'Assistant IA : la cle API, protegee
// -----------------------------------------------------------------------------
//  LA CLE N'EST JAMAIS ECRITE EN CLAIR : ni dans parametres.json, ni dans un
//  journal, ni dans l'executable.
//
//    - Windows : "Memoriser la cle" la chiffre par DPAPI (CryptProtectData,
//      liee a la session Windows de l'utilisateur : un autre compte, ou le
//      fichier copie sur un autre PC, ne la relit pas) dans IA/cle.bin ;
//    - Linux   : pas de coffre ici - la cle est gardee EN MEMOIRE pour la
//      seance seulement, et l'ecran le dit ;
//    - un relais d'entreprise (Parametres : l'adresse) peut detenir la cle :
//      le logiciel n'en a alors aucune.
//
//  LA PROTECTION EST INJECTABLE (setKeyProtector) : un essai, sous Linux, y met
//  une protection reversible pour verifier le chemin du fichier (et que la cle
//  n'y est pas en clair) ; DPAPI elle-meme se compile dans l'exe Windows.
// =============================================================================
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace app::ai {

// Le fichier de la cle protegee : IA/cle.bin (vide : pas de racine).
[[nodiscard]] std::string keyFile();

// La cle peut-elle etre memorisee sur ce poste ? (Windows : DPAPI ; Linux : non.)
[[nodiscard]] bool keyProtectionAvailable();
// Ce que l'ecran dit de la cle : "prot\xC3\xA9g\xC3\xA9" "e par Windows (DPAPI)"...
[[nodiscard]] std::string keyProtectionText();

// Memorise la cle (protegee) dans IA/cle.bin. Faux et `why` : pas de protection sur ce
// poste (Linux : rien n'est ecrit), pas de racine, ou l'ecriture a echoue.
bool storeKey(const std::string& key, std::string* why = nullptr);
// La cle memorisee (nullopt : aucune, ou illisible - un autre compte, un autre PC : `why`).
[[nodiscard]] std::optional<std::string> loadStoredKey(std::string* why = nullptr);
[[nodiscard]] bool hasStoredKey();
// Efface le fichier (vrai : il n'y est plus).
bool forgetStoredKey();

// La protection (essais) : deux fonctions inverses ; nulles : celle du systeme.
struct KeyProtector {
    std::function<std::optional<std::string>(const std::string&)> protect;
    std::function<std::optional<std::string>(const std::string&)> unprotect;
};
void setKeyProtector(KeyProtector p);

} // namespace app::ai
